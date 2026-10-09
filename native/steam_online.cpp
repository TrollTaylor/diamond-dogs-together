// Original MGS Co-op Steam online owner context: acquisition gates, the F9
// online requests, the rich-presence beacon and the lab self-test.
#include "mgscoop/steam_online.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <strsafe.h>
#include <cstdarg>
#include <initializer_list>

#include "mgscoop/coop_hud.hpp"
#include "mgscoop/early_patches.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/steam_join_code.hpp"
#include "mgscoop/steam_link_harness.hpp"
#include "mgscoop/steam_net_abi.hpp"
#include "mgscoop/steam_net_dispatch.hpp"
#include "mgscoop/steam_net_gates.hpp"
#include "mgscoop/steam_observer.hpp"
#include "mgscoop/steam_online_state.hpp"
#include "steam_shim_contract.hpp"

namespace mgscoop::steam_online {
namespace {
namespace sn = steam_net;
namespace sc = steam_shim_contract;

// ---- Configuration and the observer's notes (written before OwnerPass reads them).
Config g_config{};
volatile LONG g_configured = 0;
HMODULE g_api_module = nullptr;
bool g_api_sha = false;
bool g_owner_hook = false;
std::uint64_t g_hook_ms = 0;
volatile LONG g_shutdown = 0;
SRWLOCK g_owner = SRWLOCK_INIT;

// ---- Acquisition (owner context; published once through g_phase).
enum Phase : LONG { kPending = 0, kReady = 1, kDisabled = 2 };
volatile LONG g_phase = kPending;
char g_reason[32]{};
// Fault latch (5.9): once set, no thread of ours makes another Steam call
// (the transport polls it through SteamNetApi::halt).
volatile LONG g_halt = 0;
unsigned g_step = 0;
// Resolved pinned exports.
struct Exports {
    sn::SteamClientFn client, user, friends;
    sn::GetHSteamFn hsteam_user, hsteam_pipe;
    sn::GetGenericInterfaceFn generic;
    sn::BLoggedOnFn logged_on;
    sn::GetSteamIdFn steam_id;
    sn::GetFriendCountFn friend_count;
    sn::GetFriendByIndexFn friend_by_index;
    sn::GetFriendRelationshipFn relationship;
    sn::GetFriendPersonaNameFn persona;
    sn::GetFriendGamePlayedFn game_played;
    sn::SetRichPresenceFn set_rich_presence;
    sn::GetFriendRichPresenceFn friend_rich_presence;
    sn::InviteUserToGameFn invite;
};
Exports g_x{};
void* g_client = nullptr;
std::int32_t g_hsteam_user = 0, g_hsteam_pipe = 0;
void* g_user = nullptr;
void* g_friends = nullptr;
steam_gate::ModuleView g_view{};
std::uint64_t g_self = 0;
bool g_logged_on = false;
lab::SteamNetApi g_api{};
bool g_rtti_ok = false, g_shape_ok = false;

// ---- Play: the owner's online work.
struct FriendScan {
    bool active;
    std::int32_t count, index;
    std::uint64_t started_ms;
    steam_state::Friend list[steam_state::kMaxFriends];
    unsigned n;
};
FriendScan g_scan{};
steam_state::Friend g_friends_shown[steam_state::kMaxFriends]{};
unsigned g_friends_shown_n = 0;
std::uint64_t g_beacon_candidates[32]{};
unsigned g_beacon_candidate_n = 0, g_beacon_cursor = 0;
std::uint64_t g_beacon_poll_ms = 0;
bool g_beacon_set = false;
struct InviteSent { std::uint64_t id, ms; };
InviteSent g_invites[16]{};
unsigned g_invite_next = 0;
std::uint64_t g_persona_id = 0;
char g_persona[48]{};
char g_last_result[96]{};
std::uint64_t g_publish_ms = 0;
bool g_shutdown_done = false;

// ---- Self-test.
volatile LONG g_selftest_started = 0;
steam_state::SelftestReport g_report{};

void Log(const char* line) noexcept {
    if (g_config.log) g_config.log(line);
}
void LogF(const char* format, ...) noexcept {
    char line[320]{};
    va_list args;
    va_start(args, format);
    const HRESULT hr = StringCchVPrintfA(line, ARRAYSIZE(line), format, args);
    va_end(args);
    if (SUCCEEDED(hr)) Log(line);
}
template <std::size_t N>
void Copy(char (&dst)[N], const char* src) noexcept {
    std::size_t i = 0;
    for (; src && src[i] && i + 1 < N; ++i) {
        const auto c = static_cast<unsigned char>(src[i]);
        dst[i] = (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?';
    }
    dst[i] = 0;
}

// ---- C-style SEH thunks (no object with a destructor in these frames).
void* CallPtr(sn::SteamClientFn f, bool* fault) {
    __try { return f(); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return nullptr; }
}
std::int32_t CallHandle(sn::GetHSteamFn f, bool* fault) {
    __try { return f(); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return 0; }
}
void* CallGeneric(sn::GetGenericInterfaceFn f, void* client, std::int32_t user, std::int32_t pipe, const char* version, bool* fault) {
    __try { return f(client, user, pipe, version); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return nullptr; }
}
bool CallLoggedOn(sn::BLoggedOnFn f, void* user, bool* fault) {
    __try { return f(user); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return false; }
}
std::uint64_t CallSteamId(sn::GetSteamIdFn f, void* user, bool* fault) {
    __try { return f(user); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return 0; }
}
std::int32_t CallFriendCount(sn::GetFriendCountFn f, void* friends, bool* fault) {
    __try { return f(friends, sn::kFriendFlagImmediate); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return 0; }
}
std::uint64_t CallFriendByIndex(sn::GetFriendByIndexFn f, void* friends, std::int32_t index, bool* fault) {
    __try { return f(friends, index, sn::kFriendFlagImmediate); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return 0; }
}
std::int32_t CallRelationship(sn::GetFriendRelationshipFn f, void* friends, std::uint64_t id, bool* fault) {
    __try { return f(friends, id); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return 0; }
}
// Copies the name inside the frame (Steam's buffer is read only here).
void CallPersona(sn::GetFriendPersonaNameFn f, void* friends, std::uint64_t id, char* out, std::size_t capacity, bool* fault) {
    __try {
        const char* name = f(friends, id);
        std::size_t i = 0;
        for (; name && name[i] && i + 1 < capacity; ++i) {
            const auto c = static_cast<unsigned char>(name[i]);
            out[i] = (c >= 0x20 && c < 0x7f) ? static_cast<char>(c) : '?';
        }
        out[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        out[0] = 0;
    }
}
bool CallGamePlayed(sn::GetFriendGamePlayedFn f, void* friends, std::uint64_t id, std::uint8_t* info, bool* fault) {
    __try { return f(friends, id, info); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return false; }
}
bool CallSetRichPresence(sn::SetRichPresenceFn f, void* friends, const char* key, const char* value, bool* fault) {
    __try { return f(friends, key, value); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return false; }
}
void CallFriendRichPresence(sn::GetFriendRichPresenceFn f, void* friends, std::uint64_t id, const char* key, char* out,
                            std::size_t capacity, bool* fault) {
    __try {
        const char* value = f(friends, id, key);
        std::size_t i = 0;
        for (; value && value[i] && i + 1 < capacity; ++i) out[i] = value[i];
        out[i] = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        out[0] = 0;
    }
}
bool CallInvite(sn::InviteUserToGameFn f, void* friends, std::uint64_t id, const char* connect, bool* fault) {
    __try { return f(friends, id, connect); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return false; }
}
bool SafeRead(void*, std::uintptr_t address, std::uint64_t& out) noexcept {
    __try {
        out = *reinterpret_cast<const volatile std::uint64_t*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        out = 0;
        return false;
    }
}
const char* GuardedCheckInterface(std::uintptr_t object, const steam_gate::InterfaceRule& rule) noexcept {
    __try {
        return steam_gate::CheckInterface(g_view, object, rule, &SafeRead, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "fault";
    }
}
const char* GuardedForwardedSlot(std::uintptr_t object, unsigned slot, const char* inner_rtti) noexcept {
    __try {
        return steam_gate::CheckForwardedSlot(g_view, object, slot, inner_rtti, &SafeRead, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "gate_fault";
    }
}
const char* GuardedMessagesOwner(std::uintptr_t messages, std::uintptr_t sockets) noexcept {
    __try {
        return steam_gate::CheckMessagesOwner(g_view, messages, sockets, sn::kRttiMessagesImpl, sn::kMessagesSubobjectOffset,
                                              sn::kMessagesOwnerSockets, &SafeRead, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "gate_fault";
    }
}
const char* GuardedCheckClient(std::uintptr_t object) noexcept {
    __try {
        return steam_gate::CheckClient(g_view, object, sn::kRttiClient, sn::kClientGenericInterfaceSlot, &SafeRead, nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "fault";
    }
}
bool GuardedModuleView(const std::uint8_t* base, std::size_t size) noexcept {
    __try {
        return steam_gate::ReadModuleView(base, size, g_view);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
bool GuardedBytes(const std::uint8_t* at, const std::uint8_t* expected, std::uint32_t size) noexcept {
    __try {
        for (std::uint32_t i = 0; i < size; ++i)
            if (at[i] != expected[i]) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// An exception inside steamclient (any phase, any of our threads): latch,
// log once, and stop every Steam call of ours.
void LatchFault(const char* where) noexcept {
    if (InterlockedExchange(&g_halt, 1)) return;
    Copy(g_reason, "fault");
    InterlockedExchange(&g_phase, kDisabled);
    LogF("steam=disabled; reason=fault; where=%s\r\n", where ? where : "?");
    Log("steam=fault; note=steamclient may be unusable for this process; restart the game\r\n");
}

// A gate refusal during acquisition.
void Disable(const char* reason) noexcept {
    if (reason && lstrcmpA(reason, "fault") == 0) {
        LatchFault("acquire");
        return;
    }
    if (g_phase != kPending) return;
    Copy(g_reason, reason);
    InterlockedExchange(&g_phase, kDisabled);  // full barrier: g_reason is complete
    LogF("steam=disabled; reason=%s\r\n", g_reason);
}

// The transport/self-test threads' fault latch (SteamNetApi::on_fault).
void OnApiFault(void*, const char* where) noexcept { LatchFault(where); }

// ---- Gate 1: the hash-pinned steam_api64 exports (no Steam call).
const char* GateApi() noexcept {
    if (!g_api_module || !g_api_sha) return "api";
    MODULEINFO info{};
    if (!K32GetModuleInformation(GetCurrentProcess(), g_api_module, &info, sizeof(info)) || info.SizeOfImage != sc::kSteamSizeOfImage)
        return "api";
    const auto* base = static_cast<const std::uint8_t*>(info.lpBaseOfDll);
    for (const auto& named : sc::kOnlineExports) {
        const sc::ByteGate& gate = *named.gate;
        if (reinterpret_cast<const std::uint8_t*>(GetProcAddress(g_api_module, named.name)) != base + gate.rva) return "api";
        if (!GuardedBytes(base + gate.rva, gate.bytes, gate.size)) return "api";
    }
    const struct { const char* name; const sc::ByteGate* gate; } getters[] = {
        {"SteamUser", &sc::steam_user_getter}, {"SteamFriends", &sc::steam_friends_getter}};
    for (const auto& g : getters) {
        if (reinterpret_cast<const std::uint8_t*>(GetProcAddress(g_api_module, g.name)) != base + g.gate->rva) return "api";
        if (!GuardedBytes(base + g.gate->rva, g.gate->bytes, g.gate->size)) return "api";
    }
    for (const sc::ByteGate* gate : {&sc::steam_run_callbacks, &sc::steam_run_callbacks_inner, &sc::steam_run_reentrancy, &sc::steam_dispatch})
        if (!GuardedBytes(base + gate->rva, gate->bytes, gate->size)) return "api";
    const auto at = [&](const sc::ByteGate& gate) noexcept { return const_cast<std::uint8_t*>(base + gate.rva); };
    g_x.client = reinterpret_cast<sn::SteamClientFn>(at(sc::steam_client_export));
    g_x.user = reinterpret_cast<sn::SteamClientFn>(at(sc::steam_user_getter));
    g_x.friends = reinterpret_cast<sn::SteamClientFn>(at(sc::steam_friends_getter));
    g_x.hsteam_user = reinterpret_cast<sn::GetHSteamFn>(at(sc::steam_hsteamuser_export));
    g_x.hsteam_pipe = reinterpret_cast<sn::GetHSteamFn>(at(sc::steam_hsteampipe_export));
    g_x.generic = reinterpret_cast<sn::GetGenericInterfaceFn>(at(sc::steam_generic_interface_export));
    g_x.logged_on = reinterpret_cast<sn::BLoggedOnFn>(at(sc::steam_blogged_on_export));
    g_x.steam_id = reinterpret_cast<sn::GetSteamIdFn>(at(sc::steam_get_steam_id_export));
    g_x.friend_count = reinterpret_cast<sn::GetFriendCountFn>(at(sc::steam_friend_count_export));
    g_x.friend_by_index = reinterpret_cast<sn::GetFriendByIndexFn>(at(sc::steam_friend_by_index_export));
    g_x.relationship = reinterpret_cast<sn::GetFriendRelationshipFn>(at(sc::steam_friend_relationship_export));
    g_x.persona = reinterpret_cast<sn::GetFriendPersonaNameFn>(at(sc::steam_friend_persona_export));
    g_x.game_played = reinterpret_cast<sn::GetFriendGamePlayedFn>(at(sc::steam_friend_game_played_export));
    g_x.set_rich_presence = reinterpret_cast<sn::SetRichPresenceFn>(at(sc::steam_set_rich_presence_export));
    g_x.friend_rich_presence = reinterpret_cast<sn::GetFriendRichPresenceFn>(at(sc::steam_friend_rich_presence_export));
    g_x.invite = reinterpret_cast<sn::InviteUserToGameFn>(at(sc::steam_invite_user_export));
    return nullptr;
}

// ---- Gate 3: steamclient64 mapped from the Steam install folder (no Steam call).
const char* GateClientModule() noexcept {
    const HMODULE client = GetModuleHandleW(L"steamclient64.dll");
    if (!client) return "client_module";
    wchar_t module_path[MAX_PATH * 2]{};
    const DWORD n = GetModuleFileNameW(client, module_path, ARRAYSIZE(module_path));
    if (!n || n >= ARRAYSIZE(module_path)) return "client_module";
    wchar_t steam_path[MAX_PATH * 2]{};
    DWORD bytes = sizeof(steam_path);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, steam_path, &bytes) != ERROR_SUCCESS)
        return "client_module";
    if (!steam_gate::PathUnder(module_path, steam_path)) return "client_module";
    MODULEINFO info{};
    if (!K32GetModuleInformation(GetCurrentProcess(), client, &info, sizeof(info))) return "client_module";
    if (!GuardedModuleView(static_cast<const std::uint8_t*>(info.lpBaseOfDll), info.SizeOfImage)) return "client_module";
    return nullptr;
}

const char* Interface(const char* version, void*& out, const steam_gate::InterfaceRule& rule, bool& fault) noexcept {
    out = CallGeneric(g_x.generic, g_client, g_hsteam_user, g_hsteam_pipe, version, &fault);
    if (fault) return "fault";
    if (!out) return "iface_missing";
    return GuardedCheckInterface(reinterpret_cast<std::uintptr_t>(out), rule);
}

bool SelftestMode() noexcept { return !g_config.play_steam && g_config.selftest != 0; }
// Play needs Sockets012 only for the function-pointer dispatch (section 20).
bool WantDispatch() noexcept { return SelftestMode() ? g_config.selftest_fn : steam_dispatch::UsesFnptr(g_config.dispatch); }

// Section 20's call-free gate on the cached interfaces (static strings).
const char* DispatchGate() noexcept {
    if (!g_api.sockets) return "sockets_missing";
    if (!g_api.utils) return "utils_missing";
    const auto sockets = reinterpret_cast<std::uintptr_t>(g_api.sockets), utils = reinterpret_cast<std::uintptr_t>(g_api.utils);
    if (const char* r = GuardedForwardedSlot(sockets, sn::kSocketsRunCallbacks, sn::kRttiSocketsInner)) {
        LogF("steam=dispatch_gate; check=sockets_slot41; reason=%s\r\n", r);
        return "sockets_slot41";
    }
    if (const char* r = GuardedForwardedSlot(utils, sn::kUtilsSetConfigValue, sn::kRttiUtilsInner)) {
        LogF("steam=dispatch_gate; check=utils_slot16; reason=%s\r\n", r);
        return "utils_slot16";
    }
    if (const char* r = GuardedMessagesOwner(reinterpret_cast<std::uintptr_t>(g_api.messages), sockets)) {
        LogF("steam=dispatch_gate; check=messages_owner; reason=%s\r\n", r);
        return r;
    }
    return nullptr;
}

// One acquisition step per pass (<= 4 Steam calls). Returns the calls spent.
unsigned AcquireStep() noexcept {
    bool fault = false;
    const char* reason = nullptr;
    unsigned calls = 0;
    switch (g_step) {
    case 0:  // gate 1
        reason = GateApi();
        g_report.api = reason == nullptr;
        break;
    case 1:  // gate 2 (4 trivial getters)
        g_client = CallPtr(g_x.client, &fault);
        g_hsteam_user = fault ? 0 : CallHandle(g_x.hsteam_user, &fault);
        g_hsteam_pipe = fault ? 0 : CallHandle(g_x.hsteam_pipe, &fault);
        g_user = fault ? nullptr : CallPtr(g_x.user, &fault);
        calls = 4;
        if (!fault) reason = steam_gate::InitializedGate(g_client, g_hsteam_user, g_hsteam_pipe, g_user);
        break;
    case 2:  // gates 3 and 4 (no Steam call)
        reason = GateClientModule();
        if (!reason) reason = GuardedCheckClient(reinterpret_cast<std::uintptr_t>(g_client));
        g_report.client = reason == nullptr;
        break;
    case 3: {  // gate 5
        g_friends = CallPtr(g_x.friends, &fault);
        g_logged_on = !fault && CallLoggedOn(g_x.logged_on, g_user, &fault);
        g_self = fault ? 0 : CallSteamId(g_x.steam_id, g_user, &fault);
        calls = 3;
        g_report.logged_on = g_logged_on;
        if (fault) break;
        reason = steam_gate::IdentityGate(SelftestMode(), g_logged_on, g_self, g_config.expected_self, g_config.host, g_config.host_id);
        break;
    }
    case 4: {  // gates 6 and 7: Messages002, acquired once and cached forever (C4)
        void* messages = nullptr;
        reason = Interface(sn::kMessagesVersion, messages, steam_gate::InterfaceRule{sn::kRttiMessages, sn::kMessagesSlots, true}, fault);
        calls = 1;
        g_rtti_ok = reason == nullptr || (reason && lstrcmpA(reason, "iface_shape") == 0);
        g_shape_ok = reason == nullptr;
        g_report.rtti = g_rtti_ok;
        g_report.shape = g_shape_ok;
        g_report.messages = reason == nullptr;
        if (!reason) g_api.messages = static_cast<sn::ISteamNetworkingMessages002Abi*>(messages);
        break;
    }
    case 5: {  // Utils004 (play: relay warm-up; self-test: relay status); never refuses play
        void* utils = nullptr;
        const char* r = Interface(sn::kUtilsVersion, utils, steam_gate::InterfaceRule{sn::kRttiUtils, sn::kUtilsDeclared, false}, fault);
        calls = 1;
        if (!r) g_api.utils = static_cast<sn::ISteamNetworkingUtils004Abi*>(utils);
        g_report.utils = r == nullptr;
        if (fault) break;
        if (SelftestMode() && r) reason = r;
        break;
    }
    case 6: {  // Sockets012 (self-test; play only for the function-pointer dispatch, never refusing play)
        if (!SelftestMode() && !WantDispatch()) break;
        void* sockets = nullptr;
        const char* r = Interface(sn::kSocketsVersion, sockets, steam_gate::InterfaceRule{sn::kRttiSockets, sn::kSocketsDeclared, false}, fault);
        calls = 1;
        g_report.sockets = r == nullptr;
        if (!r) g_api.sockets = static_cast<sn::ISteamNetworkingSockets012Abi*>(sockets);
        if (fault) break;
        if (SelftestMode()) reason = r;
        else if (r) LogF("steam=dispatch_gate; check=sockets; reason=%s\r\n", r);
        break;
    }
    case 7: {  // the dispatch gate (call-free; refuses only the function pointer)
        g_api.dispatch_refused = WantDispatch() ? DispatchGate() : "off";
        break;
    }
    default: {
        g_api.on_fault = &OnApiFault;
        g_api.halt = &g_halt;
        char self[24]{};
        steam_code::FormatPeer(g_self, self);
        LogF("steam=acquire; logged_on=%u; self=%s; messages=ok; rtti=ok; shape=forwarder; utils=%s; sockets=%s; dispatch=%s; variant=%s\r\n",
             g_logged_on ? 1u : 0u, self, g_api.utils ? "ok" : "missing", g_api.sockets ? "ok" : "off",
             g_api.dispatch_refused ? g_api.dispatch_refused : "ok", SelftestMode() ? "selftest" : steam_dispatch::VariantName(g_config.dispatch));
        InterlockedExchange(&g_phase, kReady);
        return 0;
    }
    }
    if (fault) reason = "fault";
    if (reason) {
        Disable(reason);
        return calls;
    }
    ++g_step;
    return calls;
}

// ---- Play: friends, invites, persona, beacon.
bool InvitePageOpen(std::uint64_t now) noexcept {
    const std::uint64_t seen = steam_state::InvitePageMs();
    return seen && now >= seen && now - seen < 1500;
}

void FinishScan() noexcept {
    // In-game friends first, then by name.
    for (unsigned i = 1; i < g_scan.n; ++i) {
        const steam_state::Friend f = g_scan.list[i];
        unsigned j = i;
        const auto before = [&](const steam_state::Friend& a, const steam_state::Friend& b) noexcept {
            if (a.in_game != b.in_game) return a.in_game;
            return lstrcmpiA(a.name, b.name) < 0;
        };
        while (j > 0 && before(f, g_scan.list[j - 1])) {
            g_scan.list[j] = g_scan.list[j - 1];
            --j;
        }
        g_scan.list[j] = f;
    }
    for (unsigned i = 0; i < g_scan.n; ++i) g_friends_shown[i] = g_scan.list[i];
    g_friends_shown_n = g_scan.n;
    g_beacon_candidate_n = 0;
    for (unsigned i = 0; i < g_scan.n && g_beacon_candidate_n < 32; ++i)
        if (g_scan.list[i].in_game) g_beacon_candidates[g_beacon_candidate_n++] = g_scan.list[i].id;
    g_beacon_cursor = 0;
    g_scan.active = false;
}

// Friend list (host): relationship 3, app 287700 first; one friend per pass.
unsigned ScanFriends(std::uint64_t now, unsigned budget, bool for_page) noexcept {
    unsigned spent = 0;
    bool fault = false;
    if (!g_scan.active) {
        const std::uint64_t interval = for_page ? 5000 : 60000;
        if (g_scan.started_ms && now - g_scan.started_ms < interval) return 0;
        g_scan = FriendScan{};
        g_scan.started_ms = now;
        g_scan.count = CallFriendCount(g_x.friend_count, g_friends, &fault);
        spent = 1;
        if (fault) { LatchFault("friends"); return spent; }
        g_scan.active = true;
        if (g_scan.count <= 0) { FinishScan(); return spent; }
    }
    while (g_scan.active && spent + 4 <= budget) {
        const std::uint64_t id = CallFriendByIndex(g_x.friend_by_index, g_friends, g_scan.index, &fault);
        const std::int32_t relation = fault ? 0 : CallRelationship(g_x.relationship, g_friends, id, &fault);
        char name[48]{};
        if (!fault) CallPersona(g_x.persona, g_friends, id, name, sizeof(name), &fault);
        std::uint8_t game[sn::kFriendGameInfoBuffer]{};
        const bool playing = !fault && CallGamePlayed(g_x.game_played, g_friends, id, game, &fault);
        spent += 4;
        if (fault) { LatchFault("friends"); return spent; }
        std::uint64_t game_id = 0;
        for (unsigned i = 0; i < 8; ++i) game_id |= static_cast<std::uint64_t>(game[i]) << (8 * i);
        if (relation == sn::kFriendRelationshipFriend && steam::is_individual_steamid(id) && g_scan.n < steam_state::kMaxFriends) {
            steam_state::Friend& f = g_scan.list[g_scan.n++];
            f.id = id;
            Copy(f.name, name);
            f.in_game = playing && (game_id & 0xFFFFFFu) == sn::kAppId;
        }
        if (++g_scan.index >= g_scan.count) FinishScan();
    }
    return spent;
}

bool InvitedRecently(std::uint64_t id, std::uint64_t now) noexcept {
    for (const auto& i : g_invites)
        if (i.id == id && now - i.ms < 10000) return true;
    return false;
}

unsigned HandleInvite(std::uint64_t now) noexcept {
    std::uint64_t id = 0;
    if (!steam_state::TakeInvite(id)) return 0;
    char peer[24]{};
    steam_code::FormatPeer(id, peer);
    if (!steam::is_individual_steamid(id) || InvitedRecently(id, now)) {
        StringCchPrintfA(g_last_result, ARRAYSIZE(g_last_result), "Invite to %s skipped (sent less than 10 s ago).", peer);
        return 0;
    }
    char connect[steam_code::kInviteBuffer]{};
    if (!partner_net::BuildSteamInvite(connect, sizeof(connect))) {
        StringCchCopyA(g_last_result, ARRAYSIZE(g_last_result), "No join code yet: Steam networking is not ready.");
        return 0;
    }
    bool fault = false;
    const bool ok = CallInvite(g_x.invite, g_friends, id, connect, &fault);
    link::SecureZero(connect, sizeof(connect));
    if (fault) { LatchFault("invite"); return 1; }
    g_invites[g_invite_next] = InviteSent{id, now};
    g_invite_next = (g_invite_next + 1) % 16;
    if (ok) partner_net::AddSteamKnown(id, lab::KnownKind::invited);
    if (ok) coop_hud::Post(coop_hud::EventKind::invite_sent, 0, peer);  // a toast once the F9 menu closes
    LogF("steam=invite; to=%s; ok=%u\r\n", peer, ok ? 1u : 0u);
    StringCchPrintfA(g_last_result, ARRAYSIZE(g_last_result), ok ? "Invite sent to %s." : "Steam refused the invite to %s.", peer);
    return 1;
}

unsigned Beacon(const partner_net::SteamInfo& info, std::uint64_t now, unsigned budget) noexcept {
    bool fault = false;
    if (!info.host) {
        // Guest: "this player is joining that account" (friends only see it).
        const bool want = info.beacon_wanted && !g_shutdown && info.peer;
        if (want && !g_beacon_set) {
            char value[24]{};
            steam_code::FormatId64(sn::AccountOf(info.peer), value, sizeof(value));
            const bool ok = CallSetRichPresence(g_x.set_rich_presence, g_friends, sn::kRichPresenceKey, value, &fault);
            if (fault) { LatchFault("beacon"); return 1; }
            g_beacon_set = true;
            LogF("steam=beacon; set=1; ok=%u\r\n", ok ? 1u : 0u);
            return 1;
        }
        if (!want && g_beacon_set) {
            const bool ok = CallSetRichPresence(g_x.set_rich_presence, g_friends, sn::kRichPresenceKey, "", &fault);
            if (fault) { LatchFault("beacon"); return 1; }
            g_beacon_set = false;
            LogF("steam=beacon; set=0; ok=%u\r\n", ok ? 1u : 0u);
            return 1;
        }
        return 0;
    }
    // Host: while accepting and not connected, every 5 s, <= 4 in-game friends per pass.
    const partner_net::LinkInfo link = partner_net::GetLinkInfo();
    if (!info.accepting || link.state == link::LinkState::connected) return 0;
    unsigned spent = 0;
    if (!g_beacon_candidate_n || g_scan.active) spent += ScanFriends(now, budget, false);
    if (!g_beacon_candidate_n || now - g_beacon_poll_ms < 5000) return spent;
    char self[24]{};
    steam_code::FormatId64(sn::AccountOf(g_self), self, sizeof(self));
    unsigned checked = 0;
    while (checked < 4 && spent < budget && g_beacon_cursor < g_beacon_candidate_n) {
        const std::uint64_t id = g_beacon_candidates[g_beacon_cursor++];
        char value[64]{};
        CallFriendRichPresence(g_x.friend_rich_presence, g_friends, id, sn::kRichPresenceKey, value, sizeof(value), &fault);
        ++spent;
        ++checked;
        if (fault) { LatchFault("beacon"); return spent; }
        if (lstrcmpA(value, self) == 0) partner_net::AddSteamKnown(id, lab::KnownKind::beacon);
    }
    if (g_beacon_cursor >= g_beacon_candidate_n) {
        g_beacon_cursor = 0;
        g_beacon_poll_ms = now;
    }
    return spent;
}

void PublishOnline(const partner_net::SteamInfo& info, std::uint64_t now) noexcept {
    if (now - g_publish_ms < 250) return;
    g_publish_ms = now;
    steam_state::OnlineSnapshot s{};
    s.available = g_config.play_steam;
    s.host = g_config.host;
    s.accepting = info.accepting;
    s.waiting = info.waiting;
    s.code_ready = info.code_ready;
    Copy(s.steam, g_phase == kDisabled ? g_reason : g_phase == kReady ? (info.reason[0] ? info.reason : "ok") : "acquiring");
    const partner_net::LinkInfo link = partner_net::GetLinkInfo();
    Copy(s.state, partner_net::LinkStateName(link.state));
    Copy(s.route, sn::RouteName(info.route));
    s.rtt_ms = link.rtt_ms;
    if (info.peer) steam_code::FormatPeer(info.peer, s.peer);
    if (info.peer && info.peer == g_persona_id) Copy(s.peer_name, g_persona);
    Copy(s.last_result, g_last_result);
    s.friends_loading = g_scan.active;
    s.friend_count = g_friends_shown_n;
    for (unsigned i = 0; i < g_friends_shown_n; ++i) {
        s.friends[i] = g_friends_shown[i];
        s.friends[i].invited = InvitedRecently(s.friends[i].id, now);
    }
    steam_state::PublishOnline(s);
}

void PlayPass(std::uint64_t now) noexcept {
    const partner_net::SteamInfo info = partner_net::GetSteamInfo();
    unsigned budget = 4;
    // Accept toggle (no Steam call).
    bool accepting = true;
    if (steam_state::TakeAccepting(accepting) && info.host) {
        partner_net::SetSteamAccepting(accepting);
        LogF("steam=accepting; on=%u\r\n", accepting ? 1u : 0u);
    }
    // Pasted code (guest; parsed and armed on the transport thread).
    char pasted[steam_state::kPasteCapacity]{};
    if (steam_state::TakeJoin(pasted)) {
        // A disabled or faulted Steam path never drains the paste mailbox:
        // say so instead of "joining".
        const bool off = g_phase == kDisabled || g_halt || info.reason[0];
        const bool posted = !info.host && !off && partner_net::PostSteamJoin(pasted);
        StringCchCopyA(g_last_result, ARRAYSIZE(g_last_result),
                       posted ? "Code submitted; joining..."
                       : info.host ? "Join is only for a waiting guest."
                                   : "Steam networking is off; restart the game or use a LAN/VPN code.");
        link::SecureZero(pasted, sizeof(pasted));
    }
    if (g_phase != kReady || !info.active) {
        PublishOnline(info, now);
        return;
    }
    if (info.host) budget -= HandleInvite(now);
    // The peer's persona name (one call on change).
    if (budget && info.peer && info.peer != g_persona_id) {
        bool fault = false;
        CallPersona(g_x.persona, g_friends, info.peer, g_persona, sizeof(g_persona), &fault);
        --budget;
        if (fault) { LatchFault("persona"); return; }
        g_persona_id = info.peer;
    }
    if (budget && info.host && InvitePageOpen(now)) {
        const unsigned spent = ScanFriends(now, budget, true);
        budget = spent >= budget ? 0 : budget - spent;
    }
    if (budget && g_phase == kReady) Beacon(info, now, budget);
    PublishOnline(info, now);
}

// ---- Self-test (lab; docs/steam-online.md 14).
lab::SteamTransport g_ta, g_tb;
lab::LinkHarnessEnd g_ha, g_hb;
lab::SteamNetApi g_test_api{};

int TestResolve(void*, lab::SteamNetApi& out, const char*& reason) noexcept {
    out = g_test_api;
    reason = nullptr;
    return 1;
}
bool SendA(void*, const lab::Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    const lab::WireChunk chunk{data, size};
    return g_ta.transmit_to(to, &chunk, 1, true);
}
bool SendB(void*, const lab::Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    const lab::WireChunk chunk{data, size};
    return g_tb.transmit_to(to, &chunk, 1, true);
}
std::uint64_t Ms() noexcept { return partner_net::NowMs(); }
void DatagramA(void*, const std::uint8_t* d, std::size_t n, const lab::Endpoint& f) noexcept { g_ha.OnDatagram(d, n, f, Ms()); }
void DatagramB(void*, const std::uint8_t* d, std::size_t n, const lab::Endpoint& f) noexcept { g_hb.OnDatagram(d, n, f, Ms()); }
void IdleA(void*) noexcept { g_ha.OnIdle(Ms()); }
void IdleB(void*) noexcept { g_hb.OnIdle(Ms()); }

bool TryCreatePair(sn::ISteamNetworkingSockets012Abi* s, std::uint32_t* a, std::uint32_t* b, bool loopback, bool* fault) {
    __try { return s->CreateSocketPair(a, b, loopback, nullptr, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return false; }
}
void TryCloseConnection(sn::ISteamNetworkingSockets012Abi* s, std::uint32_t h, bool* fault) {
    __try { s->CloseConnection(h, 0, nullptr, false); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; }
}
std::int32_t TryRelayStatus(sn::ISteamNetworkingUtils004Abi* u, bool* fault) {
    __try { return u->GetRelayNetworkStatus(nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return -1; }
}
std::int32_t TryInertReceive(sn::ISteamNetworkingMessages002Abi* m, void** out, bool* fault) {
    __try { return m->ReceiveMessagesOnChannel(sn::kLinkChannel, out, 32); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return -1; }
}
void TryReleaseMessage(void* message, bool* fault) {
    __try {
        const sn::MessageReleaseFn release = sn::MessageRelease(message);
        if (release) release(message);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
    }
}
std::int32_t TryInertInfo(sn::ISteamNetworkingMessages002Abi* m, const sn::Identity* id, void* info, bool* fault) {
    __try { return m->GetSessionConnectionInfo(*id, info, nullptr); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return -1; }
}
bool TryInertAccept(sn::ISteamNetworkingMessages002Abi* m, const sn::Identity* id, bool* fault) {
    __try { return m->AcceptSessionWithUser(*id); } __except (EXCEPTION_EXECUTE_HANDLER) { *fault = true; return true; }
}

// Stage F (docs/steam-online.md 20.6): proves the function-pointer mechanism
// with one account. F1 sets and resets 204 (Global); F2 sets 201 on both ends
// of an in-process pair, closes b, and expects ConnectionStatusFn for a
// (Connected -> ClosedByPeer) only from our own RunCallbacks on this thread;
// D runs the play path's Install(204+205) / 2 s of Pump / Uninstall.
const char* RunDispatchStages(std::uint64_t deadline, steam_state::FnResult& r) noexcept {
    namespace sd = steam_dispatch;
    r.ran = true;
    Copy(r.gate, g_test_api.dispatch_refused ? g_test_api.dispatch_refused : "");
    if (g_test_api.dispatch_refused) return "dispatch_gate";
    bool fault = false;
    // F1: the global value, set and reset (no pump: nothing can be dispatched).
    r.set204 = sd::TrySetConfigPtr(g_test_api.utils, sn::kConfigMessagesSessionRequest, sn::kConfigScopeGlobal, 0, &sd::SessionRequestFn, &fault);
    if (!fault) r.reset204 = sd::TrySetConfigPtr(g_test_api.utils, sn::kConfigMessagesSessionRequest, sn::kConfigScopeGlobal, 0, nullptr, &fault);
    if (fault) return "fault";
    if (!r.set204 || !r.reset204) return "f1_set_config";
    // F2: the queue-then-RunCallbacks path, with 201 on a connection pair.
    std::uint32_t a = 0, b = 0;
    if (!TryCreatePair(g_test_api.sockets, &a, &b, false, &fault) || fault || !a || !b) return fault ? "fault" : "f2_pair";
    const bool set_a = sd::TrySetConfigPtr(g_test_api.utils, sn::kConfigConnectionStatusChanged, sn::kConfigScopeConnection,
                                           static_cast<std::intptr_t>(a), &sd::ConnectionStatusFn, &fault);
    const bool set_b = !fault && sd::TrySetConfigPtr(g_test_api.utils, sn::kConfigConnectionStatusChanged, sn::kConfigScopeConnection,
                                                     static_cast<std::intptr_t>(b), &sd::ConnectionStatusFn, &fault);
    const char* reason = nullptr;
    if (fault) reason = "fault";
    else if (!set_a || !set_b) reason = "f2_set_config";
    sd::ResetStatusRecord();
    if (!reason) TryCloseConnection(g_test_api.sockets, b, &fault);
    if (!reason && fault) reason = "fault";
    if (!reason) {
        Sleep(20);  // let steamclient queue the close (a pump must still be needed)
        r.pre_pump = sd::GetStatusRecord().calls;
        const DWORD self = GetCurrentThreadId();
        const std::uint64_t until = Ms() + 1000;
        while (!fault && Ms() < until && Ms() < deadline) {
            sd::TryRunCallbacks(g_test_api.sockets, &fault);
            const sd::StatusRecord record = sd::GetStatusRecord();
            for (unsigned i = 0; i < record.calls && i < sd::kStatusEntries; ++i) {
                const sd::StatusEntry& e = record.entries[i];
                if (e.conn != a || (e.new_state != sn::kConnClosedByPeer && e.new_state != sn::kConnProblemDetectedLocally)) continue;
                r.conn_ok = true;
                r.old_state = e.old_state;
                r.new_state = e.new_state;
                r.thread_ok = e.thread == self;
                break;
            }
            if (r.conn_ok) break;
            Sleep(5);
        }
        if (fault) reason = "fault";
        else if (!r.conn_ok) reason = "f2_no_callback";
    }
    const std::uint32_t matched = sd::GetStatusRecord().calls;
    if (!fault) TryCloseConnection(g_test_api.sockets, a, &fault);
    if (!fault) sd::TryRunCallbacks(g_test_api.sockets, &fault);
    const sd::StatusRecord after = sd::GetStatusRecord();
    r.status_cb = after.calls;
    r.extra = after.calls >= matched ? after.calls - matched : 0;
    if (fault) return "fault";
    if (reason) return reason;
    // D: the play code path (Install 204+205, pump 2 s, Uninstall).
    const sd::Stats before = sd::GetStats();
    const sd::Api api{g_test_api.utils, g_test_api.sockets};
    const char* install_reason = nullptr;
    r.d_install = sd::Install(api, static_cast<std::uint8_t>(sd::kRequest | sd::kFailed), install_reason, fault);
    if (fault) return "fault";
    if (!r.d_install) return "d_install";
    const std::uint64_t d_until = Ms() + 2000;
    while (!fault && Ms() < d_until && Ms() < deadline) {
        sd::Pump(api, fault);
        ++r.d_pumps;
        Sleep(10);
    }
    if (!fault) sd::Uninstall(api, fault);
    if (fault) return "fault";
    r.d_uninstall = sd::Installed() == 0;
    const sd::Stats end = sd::GetStats();
    r.d_dispatched = end.dispatched - before.dispatched;
    r.faults = end.faults;
    if (!r.d_uninstall) return "d_uninstall";
    if (r.faults) return "fn_fault";
    return nullptr;
}

void Stage(const char* stage) noexcept {
    Copy(g_report.stage, stage);
    steam_state::PublishReport(g_report);
    LogF("steam_selftest=stage; stage=%s\r\n", stage);
}
void Verdict(const char* verdict, const char* reason) noexcept {
    Copy(g_report.verdict, verdict);
    Copy(g_report.reason, reason ? reason : "");
    if (lstrcmpA(verdict, "refused") != 0) Copy(g_report.stage, "done");
    steam_state::PublishReport(g_report);
    char line[160]{};
    steam_state::FormatStatusLine(g_report, line, sizeof(line));
    LogF("steam_selftest=done; verdict=%s; reason=%s; %s\r\n", verdict, reason ? reason : "", line);
}

void RandomSecret(char (&out)[link::kSecretChars]) noexcept {
    std::uint8_t bytes[link::kSecretChars]{};
    BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    for (std::size_t i = 0; i < link::kSecretChars; ++i) out[i] = link::kCrockford[bytes[i] & 31];
    link::SecureZero(bytes, sizeof(bytes));
}

// Stage P / L: two transports on the Connection leg of an in-process pair.
const char* RunPair(bool network_loopback, std::uint64_t deadline, steam_state::PairResult& r) noexcept {
    r = steam_state::PairResult{};
    r.ran = true;
    r.frames = 500;
    r.rel = 64;
    bool fault = false;
    std::uint32_t a = 0, b = 0;
    if (!TryCreatePair(g_test_api.sockets, &a, &b, network_loopback, &fault) || fault || !a || !b) return fault ? "fault" : "pair_create";
    lab::SteamTransportConfig ca{}, cb{};
    ca.role = lab::SteamRole::host;
    cb.role = lab::SteamRole::guest;
    ca.leg = cb.leg = lab::SteamLeg::connection;
    ca.conn = a;
    cb.conn = b;
    ca.resolve = cb.resolve = &TestResolve;
    ca.log = cb.log = nullptr;
    lab::SteamSink sa{}, sb{};
    sa.on_datagram = &DatagramA;
    sa.on_idle = &IdleA;
    sb.on_datagram = &DatagramB;
    sb.on_idle = &IdleB;
    char secret[link::kSecretChars]{};
    RandomSecret(secret);
    const std::uint64_t start = Ms();
    g_ha.Start(link::Role::host, false, lab::Endpoint{}, secret, "selftest", &SendA, nullptr, start);
    g_hb.Start(link::Role::guest, false, lab::kConnectionEndpoint, secret, "selftest", &SendB, nullptr, start);
    link::SecureZero(secret, sizeof(secret));
    const char* reason = nullptr;
    if (!g_ta.start(ca, sa) || !g_tb.start(cb, sb)) reason = "pair_thread";
    while (!reason && !(g_ha.Connected() && g_hb.Connected()) && Ms() - start < 2000 && Ms() < deadline) Sleep(2);
    if (!reason && !(g_ha.Connected() && g_hb.Connected())) reason = "pair_handshake";
    if (!reason) {
        r.hs_ms = static_cast<std::uint32_t>(Ms() - start);
        for (std::uint32_t i = 0; i < r.frames && Ms() < deadline; ++i) {
            while (g_tb.steam_stats().ring_depth > 32 && Ms() < deadline) Sleep(1);
            g_hb.SendFrame(i, Ms());
        }
        const std::uint64_t wait = Ms();
        while (g_ha.GetCounters().frames_rx < r.frames && Ms() - wait < 3000 && Ms() < deadline) Sleep(2);
        for (std::uint32_t i = 0; i < r.rel && Ms() < deadline;) {
            const link::ReliableResult q = g_ha.SendReliable(2, i);
            if (q == link::ReliableResult::queued || q == link::ReliableResult::replaced) ++i;
            else Sleep(5);
        }
        const std::uint64_t rel_wait = Ms();
        while (g_hb.GetCounters().rel_rx < r.rel && Ms() - rel_wait < 5000 && Ms() < deadline) Sleep(5);
        r.frames_rx = static_cast<std::uint32_t>(g_ha.GetCounters().frames_rx);
        r.rel_rx = static_cast<std::uint32_t>(g_hb.GetCounters().rel_rx);
        r.rtt_ms = g_hb.RttMs();
    }
    g_ta.stop();
    g_tb.stop();
    const lab::SteamTransportStats x = g_ta.steam_stats(), y = g_tb.steam_stats();
    r.release = x.release + y.release;
    r.received = x.received + y.received;
    r.bad_identity = static_cast<std::uint32_t>(x.bad_identity + y.bad_identity);
    TryCloseConnection(g_test_api.sockets, a, &fault);
    TryCloseConnection(g_test_api.sockets, b, &fault);
    if (fault) LatchFault("selftest");
    if (fault || x.faults || y.faults) return "fault";
    if (reason) return reason;
    if (r.bad_identity) return "pair_identity";
    if (r.frames_rx != r.frames || g_ha.GetCounters().frames_bad) return "pair_frames";
    if (r.rel_rx != r.rel || g_hb.GetCounters().rel_bad) return "pair_reliable";
    if (r.release != r.received) return "pair_release";
    if (r.hs_ms >= 2000) return "pair_handshake";
    return nullptr;
}

DWORD WINAPI SelftestThread(void*) {
    const std::uint64_t deadline = Ms() + 20000;
    const unsigned long long callbacks_before = SteamObserverCallbackCount();
    const unsigned long long getter_before = SteamObserverGetterForwards();
    Log("steam_selftest=start\r\n");
    // Stage G: the owner context's gates 1-7 plus Sockets012/Utils004.
    Stage("G");
    while (g_phase == kPending && Ms() < deadline) Sleep(20);
    if (g_phase != kReady) {
        Verdict("fail", g_phase == kDisabled ? g_reason : "acquire_timeout");
        return 0;
    }
    g_test_api = g_api;
    g_test_api.on_fault = &OnApiFault;
    g_test_api.halt = &g_halt;
    bool fault = false;
    g_report.relay = g_test_api.utils ? TryRelayStatus(g_test_api.utils, &fault) : -1;
    if (fault) { LatchFault("selftest"); Verdict("fail", "fault"); return 0; }
    if (!g_test_api.sockets || !g_test_api.utils || !g_test_api.messages) { Verdict("fail", "iface_missing"); return 0; }
    // Stage P: the in-process pair (no network).
    Stage("P");
    if (const char* reason = RunPair(false, deadline, g_report.pair)) { Verdict("fail", reason); return 0; }
    // Stage L: the network-loopback pair (steam_selftest=2 only: binds UDP sockets).
    if (g_config.selftest >= 2) {
        Stage("L");
        if (const char* reason = RunPair(true, deadline, g_report.lpair)) { Verdict("fail", reason); return 0; }
    }
    // Stage F/D: the function-pointer dispatch (section 20).
    if (g_config.selftest_fn) {
        Stage("F");
        const char* reason = RunDispatchStages(deadline, g_report.fn);
        LogF("steam_dispatch=selftest; gate=%s; set204=%u; reset204=%u; pre_pump=%u; status_cb=%u; conn_ok=%u; old=%d; new=%d; "
             "thread_ok=%u; extra=%u; d_install=%u; d_pumps=%u; d_dispatched=%u; d_uninstall=%u; faults=%u; reason=%s\r\n",
             g_report.fn.gate[0] ? g_report.fn.gate : "ok", g_report.fn.set204 ? 1u : 0u, g_report.fn.reset204 ? 1u : 0u,
             g_report.fn.pre_pump, g_report.fn.status_cb, g_report.fn.conn_ok ? 1u : 0u, g_report.fn.old_state, g_report.fn.new_state,
             g_report.fn.thread_ok ? 1u : 0u, g_report.fn.extra, g_report.fn.d_install ? 1u : 0u, g_report.fn.d_pumps,
             g_report.fn.d_dispatched, g_report.fn.d_uninstall ? 1u : 0u, g_report.fn.faults, reason ? reason : "");
        if (reason) {
            Copy(g_report.fn.reason, reason);
            if (lstrcmpA(reason, "fault") == 0) LatchFault("selftest_dispatch");
            Verdict("fail", reason);
            return 0;
        }
        if (lstrcmpA(steam_state::FnVerdict(g_report.fn), "ok") != 0) {
            Copy(g_report.fn.reason, "fn_check");
            Verdict("fail", "fn_check");
            return 0;
        }
    } else {
        g_report.fn.skipped = true;
        Log("steam_dispatch=selftest; skipped=1; key=steam_selftest_fn\r\n");
    }
    // Stage M: Messages stays inert (no peer, nothing pending).
    Stage("M");
    void* messages[32]{};
    g_report.inert_recv = TryInertReceive(g_test_api.messages, messages, &fault);
    for (std::int32_t i = 0; !fault && i < g_report.inert_recv && i < 32; ++i) TryReleaseMessage(messages[i], &fault);
    sn::Identity self{};
    sn::MakeSteamIdentity(g_self, self);
    static std::uint8_t info[sn::kConnInfoSize];
    for (auto& v : info) v = 0;
    g_report.inert_state = fault ? -1 : TryInertInfo(g_test_api.messages, &self, info, &fault);
    bool zero = true;
    for (auto v : info) zero = zero && v == 0;
    g_report.inert_accept = fault ? true : TryInertAccept(g_test_api.messages, &self, &fault);
    if (fault) { LatchFault("selftest"); Verdict("fail", "fault"); return 0; }
    if (g_report.inert_recv != 0) { Verdict("fail", "inert_receive"); return 0; }
    if (g_report.inert_state != 0 || !zero) { Verdict("fail", "inert_state"); return 0; }
    if (g_report.inert_accept) { Verdict("fail", "inert_accept"); return 0; }
    // Stage T: the game's own networking untouched; no 1251/337 in the lab.
    Stage("T");
    Sleep(250);
    g_report.getter_delta = static_cast<std::int64_t>(SteamObserverGetterForwards() - getter_before);
    if (g_report.getter_delta != 0) { Verdict("fail", "getter_delta"); return 0; }
    if (lab::InitWrapperInstalled()) { Verdict("fail", "callbacks_registered"); return 0; }
    if (SteamObserverCallbackCount() <= callbacks_before) { Verdict("fail", "callbacks_stalled"); return 0; }
    Verdict("pass", nullptr);
    return 0;
}

void MaybeStartSelftest(std::uint64_t now) noexcept {
    if (!g_config.selftest || g_selftest_started || g_config.fake_identity) return;
    if (!g_hook_ms || now - g_hook_ms < 10000) return;
    // The owner context finishes stage G first (it owns g_report until then).
    if (InterlockedCompareExchange(&g_phase, 0, 0) == kPending) return;
    if (InterlockedExchange(&g_selftest_started, 1)) return;
    HANDLE thread = CreateThread(nullptr, 0, &SelftestThread, nullptr, 0, nullptr);
    if (thread) CloseHandle(thread);
    else Verdict("fail", "thread");
}
}  // namespace

void NoteApiModule(void* steam_api, bool sha_verified) noexcept {
    g_api_module = static_cast<HMODULE>(steam_api);
    g_api_sha = sha_verified;
}

void NoteOwnerHook(bool installed) noexcept {
    g_owner_hook = installed;
    g_hook_ms = GetTickCount64();
}

void Configure(const Config& config) noexcept {
    if (InterlockedCompareExchange(&g_configured, 0, 0)) return;
    g_config = config;
    g_report = steam_state::SelftestReport{};
    g_report.relay = -1;
    if (config.selftest) {
        g_report.configured = true;
        Copy(g_report.stage, "wait");
        if (config.fake_identity) {
            Copy(g_report.stage, "G");
            Copy(g_report.verdict, "refused");
            Copy(g_report.reason, "fake_identity");
            Log("steam_selftest=done; verdict=refused; reason=fake_identity\r\n");
        } else if (!g_owner_hook) {
            Copy(g_report.verdict, "refused");
            Copy(g_report.reason, "owner_unavailable");
            Log("steam_selftest=done; verdict=refused; reason=owner_unavailable\r\n");
        }
        steam_state::PublishReport(g_report);
    }
    if (config.play_steam) {
        steam_state::SetCodeProvider(&partner_net::BuildSteamJoinCode);
        steam_state::OnlineSnapshot s{};
        s.available = true;
        s.host = config.host;
        Copy(s.steam, g_owner_hook ? "acquiring" : "owner_unavailable");
        steam_state::PublishOnline(s);
        if (!g_owner_hook) {
            Copy(g_reason, "owner_unavailable");
            InterlockedExchange(&g_phase, kDisabled);
            Log("steam=disabled; reason=owner_unavailable\r\n");
        }
    }
    if (!config.play_steam && !(config.selftest && !config.fake_identity)) {
        // Inert: nothing to acquire. Resolve answers "disabled" for safety.
        Copy(g_reason, "not_configured");
        InterlockedExchange(&g_phase, kDisabled);
    }
    InterlockedExchange(&g_configured, 1);
}

void OwnerPass() noexcept {
    if (!InterlockedCompareExchange(&g_configured, 0, 0)) return;
    if (g_shutdown_done || g_halt) return;
    if (!TryAcquireSRWLockExclusive(&g_owner)) return;
    const std::uint64_t now = GetTickCount64();
    if (g_shutdown) {
        // One last call at most: clear the guest beacon; then no Steam call.
        if (g_beacon_set && g_phase == kReady && !g_halt) {
            bool fault = false;
            CallSetRichPresence(g_x.set_rich_presence, g_friends, sn::kRichPresenceKey, "", &fault);
            g_beacon_set = false;
        }
        g_shutdown_done = true;
        ReleaseSRWLockExclusive(&g_owner);
        return;
    }
    unsigned budget = 4;
    while (g_phase == kPending && budget) {
        const unsigned spent = AcquireStep();
        if (!spent) {
            if (g_phase != kPending) break;
            continue;  // a call-free step: run the next one in the same pass
        }
        budget = spent >= budget ? 0 : budget - spent;
        break;  // at most one calling step per pass
    }
    if (SelftestMode()) MaybeStartSelftest(now);
    if (g_config.play_steam) PlayPass(now);
    ReleaseSRWLockExclusive(&g_owner);
}

int Resolve(void*, lab::SteamNetApi& out, const char*& reason) noexcept {
    reason = nullptr;
    const LONG phase = InterlockedCompareExchange(&g_phase, 0, 0);
    if (phase == kReady) {
        out = g_api;
        return 1;
    }
    if (phase == kDisabled) {
        reason = g_reason;
        return -1;
    }
    return 0;
}

void Shutdown() noexcept {
    InterlockedExchange(&g_shutdown, 1);
}

bool StatusLine(char* out, std::size_t capacity) noexcept {
    return steam_state::FormatStatusLine(steam_state::GetReport(), out, capacity);
}

}  // namespace mgscoop::steam_online
