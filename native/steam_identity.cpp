// Original co-op lab Steam interface forwarding proxies (identity + save).
#include "mgscoop/steam_identity.hpp"

#include "mgscoop/save_redirect.hpp"

#include <cstring>

namespace mgscoop::lab {

std::size_t encode_forward_thunk(std::uint8_t* out, const void* real, unsigned slot) noexcept {
    std::size_t n = 0;
    out[n++] = 0x48;
    out[n++] = 0xB9;  // mov rcx, imm64
    const std::uint64_t imm = reinterpret_cast<std::uint64_t>(real);
    std::memcpy(out + n, &imm, 8);
    n += 8;
    out[n++] = 0x48;
    out[n++] = 0x8B;
    out[n++] = 0x01;  // mov rax, [rcx]
    out[n++] = 0xFF;
    out[n++] = 0xA0;  // jmp qword ptr [rax + disp32]
    const std::uint32_t disp = slot * 8u;
    std::memcpy(out + n, &disp, 4);
    n += 4;
    while (n < kThunkStride) out[n++] = 0xCC;  // int3 padding
    return n;
}

}  // namespace mgscoop::lab

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace mgscoop::lab {

void* build_forwarding_proxy(void* real, unsigned slot_count, const ProxyOverride* overrides,
                             unsigned override_count, void* context) noexcept {
    if (!real || !slot_count) return nullptr;
    for (unsigned i = 0; i < override_count; ++i)
        if (overrides[i].slot >= slot_count) return nullptr;
    auto* code = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, static_cast<SIZE_T>(slot_count) * kThunkStride, MEM_COMMIT | MEM_RESERVE,
                     PAGE_READWRITE));
    auto* vtable = static_cast<const void**>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, static_cast<SIZE_T>(slot_count) * sizeof(void*)));
    auto* header = static_cast<ProxyHeader*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(ProxyHeader)));
    if (!code || !vtable || !header) {
        if (vtable) HeapFree(GetProcessHeap(), 0, vtable);
        if (header) HeapFree(GetProcessHeap(), 0, header);
        if (code) VirtualFree(code, 0, MEM_RELEASE);
        return nullptr;
    }
    for (unsigned slot = 0; slot < slot_count; ++slot) {
        const void* override_fn = nullptr;
        for (unsigned i = 0; i < override_count; ++i)
            if (overrides[i].slot == slot) { override_fn = overrides[i].fn; break; }
        if (override_fn) {
            vtable[slot] = override_fn;
        } else {
            std::uint8_t* at = code + static_cast<std::size_t>(slot) * kThunkStride;
            encode_forward_thunk(at, real, slot);
            vtable[slot] = at;
        }
    }
    DWORD previous = 0;
    if (!VirtualProtect(code, static_cast<SIZE_T>(slot_count) * kThunkStride, PAGE_EXECUTE_READ, &previous)) {
        HeapFree(GetProcessHeap(), 0, vtable);
        HeapFree(GetProcessHeap(), 0, header);
        VirtualFree(code, 0, MEM_RELEASE);
        return nullptr;
    }
    FlushInstructionCache(GetCurrentProcess(), code, static_cast<SIZE_T>(slot_count) * kThunkStride);
    header->vtable = vtable;
    header->real = real;
    header->context = context;
    return header;
}

namespace identity {
namespace {
Settings g_settings{};
bool g_configured = false;
void* g_user_real = nullptr;
void* g_user_proxy = nullptr;
void* g_friends_real = nullptr;
void* g_friends_proxy = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;
volatile long long g_learned_peer = 0;
// Double-buffered peer persona: the game may hold the returned pointer, so a
// changed name is written to the inactive buffer and then published.
char g_peer_names[2][64]{};
volatile long g_peer_name_index = -1;  // -1 = no name received
volatile long g_ticket_armed = 0;
volatile long g_logged_ticket = 0;
volatile long g_logged_folder = 0;

void log_once(volatile long* flag, const char* line) noexcept {
    if (g_settings.log && InterlockedExchange(flag, 1) == 0) g_settings.log(line);
}

void* real_of(void* self) noexcept { return static_cast<ProxyHeader*>(self)->real; }

template <typename Fn>
Fn real_slot(void* real, unsigned slot) noexcept {
    auto* const* vtable = *static_cast<void* const* const*>(real);
    return reinterpret_cast<Fn>(vtable[slot]);
}

bool is_fake_self(std::uint64_t id) noexcept { return g_settings.self_id != 0 && id == g_settings.self_id; }
bool is_peer(std::uint64_t id) noexcept {
    if (id == 0) return false;
    if (g_settings.peer_id != 0 && id == g_settings.peer_id) return true;
    const auto learned = static_cast<std::uint64_t>(g_learned_peer);
    return learned != 0 && id == learned;
}

// ----- ISteamUser overrides -----
// GetSteamID returns CSteamID by value through a hidden return pointer.
std::uint64_t* MGSCOOP_STEAM_CALL User_GetSteamID(void* /*self*/, std::uint64_t* out) noexcept {
    if (out) *out = g_settings.self_id;
    return out;
}
bool MGSCOOP_STEAM_CALL User_GetUserDataFolder(void* self, char* buf, int cub) noexcept {
    if (g_settings.save_dir[0]) {
        if (save::format_user_data_folder(g_settings.save_dir, buf, cub)) return true;
        // Fail closed: never hand out the real userdata folder when a redirect
        // is configured.
        if (buf && cub > 0) buf[0] = 0;
        log_once(&g_logged_folder, "saves=refused; reason=user_data_folder_does_not_fit\r\n");
        return false;
    }
    using Fn = bool(MGSCOOP_STEAM_CALL*)(void*, char*, int);
    return real_slot<Fn>(real_of(self), steam::user_slot::GetUserDataFolder)(real_of(self), buf, cub);
}
std::uint32_t MGSCOOP_STEAM_CALL User_GetAuthSessionTicket(void* self, void* ticket, int cb_max,
                                                           std::uint32_t* pcb) noexcept {
    using Fn = std::uint32_t(MGSCOOP_STEAM_CALL*)(void*, void*, int, std::uint32_t*);
    const std::uint32_t handle =
        real_slot<Fn>(real_of(self), steam::user_slot::GetAuthSessionTicket)(real_of(self), ticket, cb_max, pcb);
    if (!g_ticket_armed) {
        log_once(&g_logged_ticket, "identity=ticket_unrewritten; reason=master_redirect_inactive\r\n");
        return handle;
    }
    // fuse reads the SteamID from ticket bytes 12..19 (little-endian). Rewrite
    // it to the fake id so the local master server tells the two players apart.
    if (g_settings.self_id && ticket && pcb && *pcb >= 20 && cb_max >= 20)
        std::memcpy(static_cast<std::uint8_t*>(ticket) + 12, &g_settings.self_id, 8);
    return handle;
}

// ----- ISteamFriends overrides (no loopback id ever reaches real Steam) -----
const char* MGSCOOP_STEAM_CALL Friends_GetPersonaName(void* self) noexcept {
    if (g_settings.self_name[0]) return g_settings.self_name;
    using Fn = const char*(MGSCOOP_STEAM_CALL*)(void*);
    return real_slot<Fn>(real_of(self), steam::friends_slot::GetPersonaName)(real_of(self));
}
const char* MGSCOOP_STEAM_CALL Friends_GetFriendPersonaName(void* self, std::uint64_t id) noexcept {
    if (is_fake_self(id)) return Friends_GetPersonaName(self);
    if (is_peer(id)) return peer_name();
    using Fn = const char*(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t);
    return real_slot<Fn>(real_of(self), steam::friends_slot::GetFriendPersonaName)(real_of(self), id);
}
void MGSCOOP_STEAM_CALL Friends_ActivateGameOverlayToUser(void* self, const char* dialog,
                                                          std::uint64_t id) noexcept {
    if (is_loopback_id(id)) return;  // no real overlay for a loopback identity
    using Fn = void(MGSCOOP_STEAM_CALL*)(void*, const char*, std::uint64_t);
    real_slot<Fn>(real_of(self), steam::friends_slot::ActivateGameOverlayToUser)(real_of(self), dialog, id);
}
bool MGSCOOP_STEAM_CALL Friends_RequestUserInformation(void* self, std::uint64_t id, bool name_only) noexcept {
    if (is_loopback_id(id)) return false;  // nothing pending: the persona is already known locally
    using Fn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t, bool);
    return real_slot<Fn>(real_of(self), steam::friends_slot::RequestUserInformation)(real_of(self), id, name_only);
}
}  // namespace

bool needs_user_proxy(const Settings& settings) noexcept {
    return settings.self_id != 0 || settings.save_dir[0] != 0;
}
bool needs_friends_proxy(const Settings& settings) noexcept {
    return settings.self_id != 0 || settings.loopback || settings.peer_id != 0;
}

bool configure(const Settings& settings) noexcept {
    if (settings.self_id != 0 && !steam::is_individual_steamid(settings.self_id)) return false;
    if (settings.self_id != 0 && !settings.loopback) return false;  // a fake id needs the loopback gate
    if (settings.peer_id != 0 && !steam::is_individual_steamid(settings.peer_id)) return false;
    AcquireSRWLockExclusive(&g_lock);
    g_settings = settings;
    g_settings.self_name[sizeof(g_settings.self_name) - 1] = 0;
    g_settings.save_dir[sizeof(g_settings.save_dir) - 1] = 0;
    g_configured = true;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

bool configured() noexcept { return g_configured; }

void note_peer(std::uint64_t id, const char* name) noexcept {
    if (!steam::is_individual_steamid(id)) return;
    AcquireSRWLockExclusive(&g_lock);
    g_learned_peer = static_cast<long long>(id);
    if (name && name[0]) {
        const long current = g_peer_name_index;
        if (current < 0 || std::strncmp(g_peer_names[current], name, sizeof(g_peer_names[0]) - 1) != 0) {
            const long next = current < 0 ? 0 : 1 - current;
            std::size_t i = 0;
            for (; name[i] && i + 1 < sizeof(g_peer_names[next]); ++i) g_peer_names[next][i] = name[i];
            g_peer_names[next][i] = 0;
            InterlockedExchange(&g_peer_name_index, next);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

bool is_loopback_id(std::uint64_t id) noexcept { return is_fake_self(id) || is_peer(id); }

const char* peer_name() noexcept {
    const long index = g_peer_name_index;
    return index < 0 ? kPeerFallbackName : g_peer_names[index];
}

void arm_ticket_rewrite(bool armed) noexcept { InterlockedExchange(&g_ticket_armed, armed ? 1 : 0); }
bool ticket_rewrite_armed() noexcept { return g_ticket_armed != 0; }

void* wrap_user(void* real) noexcept {
    if (!real || !g_configured || !needs_user_proxy(g_settings)) return real;
    AcquireSRWLockExclusive(&g_lock);
    if (real == g_user_real && g_user_proxy) {
        void* cached = g_user_proxy;
        ReleaseSRWLockExclusive(&g_lock);
        return cached;
    }
    ProxyOverride overrides[3];
    unsigned n = 0;
    if (g_settings.self_id != 0) {
        overrides[n++] = {steam::user_slot::GetSteamID, reinterpret_cast<const void*>(&User_GetSteamID)};
        overrides[n++] = {steam::user_slot::GetAuthSessionTicket,
                          reinterpret_cast<const void*>(&User_GetAuthSessionTicket)};
    }
    if (g_settings.save_dir[0])
        overrides[n++] = {steam::user_slot::GetUserDataFolder,
                          reinterpret_cast<const void*>(&User_GetUserDataFolder)};
    void* proxy = build_forwarding_proxy(real, steam::user_slot::kCount, overrides, n, &g_settings);
    if (proxy) {
        g_user_real = real;
        g_user_proxy = proxy;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return proxy ? proxy : real;
}

void* wrap_friends(void* real) noexcept {
    if (!real || !g_configured || !needs_friends_proxy(g_settings)) return real;
    AcquireSRWLockExclusive(&g_lock);
    if (real == g_friends_real && g_friends_proxy) {
        void* cached = g_friends_proxy;
        ReleaseSRWLockExclusive(&g_lock);
        return cached;
    }
    ProxyOverride overrides[4] = {
        {steam::friends_slot::GetPersonaName, reinterpret_cast<const void*>(&Friends_GetPersonaName)},
        {steam::friends_slot::GetFriendPersonaName, reinterpret_cast<const void*>(&Friends_GetFriendPersonaName)},
        {steam::friends_slot::ActivateGameOverlayToUser,
         reinterpret_cast<const void*>(&Friends_ActivateGameOverlayToUser)},
        {steam::friends_slot::RequestUserInformation,
         reinterpret_cast<const void*>(&Friends_RequestUserInformation)},
    };
    void* proxy = build_forwarding_proxy(real, steam::friends_slot::kCount, overrides, 4, &g_settings);
    if (proxy) {
        g_friends_real = real;
        g_friends_proxy = proxy;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return proxy ? proxy : real;
}

void reset_for_test() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_settings = Settings{};
    g_configured = false;
    g_user_real = g_user_proxy = nullptr;
    g_friends_real = g_friends_proxy = nullptr;
    g_learned_peer = 0;
    g_peer_name_index = -1;
    g_peer_names[0][0] = g_peer_names[1][0] = 0;
    g_ticket_armed = 0;
    g_logged_ticket = 0;
    g_logged_folder = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

}  // namespace identity
}  // namespace mgscoop::lab
#endif  // _WIN32
