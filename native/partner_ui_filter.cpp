// Original MGS Co-op G1: the partner body's HUD hint requests never reach the
// viewer's HUD (partner_ui_filter.hpp, docs/native-puppet.md "Partner HUD
// hints"). One MinHook per unfiltered UiControllerImpl hint-queue leaf; a
// request is swallowed only inside the scope Begin/End opens around block 1's
// drain job for the UiControllerImpl of that job's P.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <strsafe.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/partner_ui_filter.hpp"
#include "mgscoop/link_crypto.hpp"
#include "partner_player_contract.hpp"

#pragma intrinsic(_ReturnAddress)

namespace mgscoop::partner_ui_filter {
namespace {
namespace c = mgscoop::partner_player_contract;
using detail::LeafFn;

constexpr std::uintptr_t kMax = 0x7fffffffffff;

std::uintptr_t g_image = 0;
std::size_t g_span = 0;
LeafFn g_original[kQueues] = {};
volatile LONG g_install_state = 0;
const char* g_install_result = nullptr;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_shutdown{false};
LogFn g_log = nullptr;

// The drain scope. Begin/End run on the hook thread only; any thread reads
// g_active and g_tid, and only the scoped thread reads g_ui.
volatile LONG g_active = 0;
volatile LONG g_tid = 0;
std::uintptr_t g_ui = 0;
std::uint64_t g_digest = 0;
bool g_digest_ok = false;
volatile LONG g_faulted = 0;
// Counters (process totals).
volatile LONG g_windows = 0, g_refused = 0, g_last_refusal = 0, g_foreign = 0, g_passed = 0, g_changed = 0, g_faults = 0;
volatile LONG g_swallowed[kQueues] = {};
// Distinct swallowed keys: written only by the scoped (hook) thread, the count
// published after the entry.
volatile LONG64 g_key[kKeys] = {};
volatile LONG g_keys = 0;

void Log(const char* line) noexcept {
    if (g_log) g_log(line);
}

template <class T> bool Rd(std::uintptr_t at, T& out) noexcept {
    __try {
        std::memcpy(&out, reinterpret_cast<const void*>(at), sizeof(T));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// FNV-1a over the five request records (ui+0x1E0..+0x370).
bool RecordsDigest(std::uintptr_t ui, std::uint64_t& out) noexcept {
    __try {
        std::uint64_t h = 0xCBF29CE484222325ull;
        const auto* p = reinterpret_cast<const unsigned char*>(ui + kRecordsBegin);
        for (std::size_t i = 0; i < kRecordsEnd - kRecordsBegin; ++i) {
            h ^= p[i];
            h *= 0x100000001B3ull;
        }
        out = h;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ReadIdentity(std::uintptr_t P, Identity& id) noexcept {
    __try {
        id.P = P;
        std::memcpy(&id.ui, reinterpret_cast<const void*>(P + kPUi), sizeof(id.ui));
        if (id.ui < kUiPrimary || (id.ui & 7u) != 0) return true;  // CheckIdentity names it
        std::memcpy(&id.ui_vt, reinterpret_cast<const void*>(id.ui), sizeof(id.ui_vt));
        std::memcpy(&id.primary_vt, reinterpret_cast<const void*>(id.ui - kUiPrimary), sizeof(id.primary_vt));
        std::memcpy(&id.owner, reinterpret_cast<const void*>(id.ui + kUiOwner), sizeof(id.owner));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void Refuse(Refusal why) noexcept {
    InterlockedIncrement(&g_refused);
    InterlockedExchange(&g_last_refusal, static_cast<LONG>(why));
}

void RecordKey(std::uint64_t key) noexcept {
    const LONG n = g_keys;
    for (LONG i = 0; i < n; ++i)
        if (static_cast<std::uint64_t>(g_key[i]) == key) return;
    if (n >= static_cast<LONG>(kKeys)) return;
    InterlockedExchange64(&g_key[n], static_cast<LONG64>(key));
    InterlockedExchange(&g_keys, n + 1);
}

std::uint32_t CallerRva(std::uintptr_t caller) noexcept {
    return g_image && caller >= g_image && caller - g_image < g_span ? static_cast<std::uint32_t>(caller - g_image) : 0u;
}

// The five leaf detours (same ABI as the leaves: rcx ui, dx id, r8d priority,
// r9b flag, three stack bytes). A swallowed request never enters the leaf, so
// the record keeps whatever the viewer's own update queued.
template <unsigned Q>
__declspec(noinline) void LeafDetour(void* ui, std::uint16_t id, std::uint32_t priority, std::uint8_t flag, std::uint8_t a5, std::uint8_t a6,
                                     std::uint8_t a7) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const DWORD error = GetLastError();
    const bool active = g_active != 0;
    const bool same = active && static_cast<DWORD>(g_tid) == GetCurrentThreadId();
    if (RouteHint(active, same, g_shutdown.load(std::memory_order_acquire), reinterpret_cast<std::uintptr_t>(ui), g_ui) == Route::swallow) {
        InterlockedIncrement(&g_swallowed[Q]);
        RecordKey(Key(Q, id, priority, CallerRva(caller)));
        SetLastError(error);
        return;
    }
    if (same) InterlockedIncrement(&g_foreign);
    InterlockedIncrement(&g_passed);
    SetLastError(error);
    g_original[Q](ui, id, priority, flag, a5, a6, a7);
}
constexpr LeafFn kDetours[kQueues] = {&LeafDetour<0>, &LeafDetour<1>, &LeafDetour<2>, &LeafDetour<3>, &LeafDetour<4>};

bool SlotsOk(std::uintptr_t image) noexcept {
    for (const Queue& q : kQueue) {
        std::uintptr_t slot = 0;
        if (!Rd(image + kUiVtableRva + q.slot, slot) || slot != image + q.leaf_rva) return false;
    }
    return true;
}

bool PinsOk(std::uintptr_t image) noexcept {
    if (sizeof(c::ui_filter_bodies) / sizeof(c::ui_filter_bodies[0]) != kQueues) return false;
    for (unsigned k = 0; k < kQueues; ++k) {
        const auto& pin = c::ui_filter_bodies[k];
        if (pin.rva != kQueue[k].leaf_rva || pin.size != kLeafSize) return false;
        unsigned char bytes[kLeafSize]{};
        std::uint8_t digest[link::kSha256Size]{};
        __try {
            std::memcpy(bytes, reinterpret_cast<const void*>(image + pin.rva), kLeafSize);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        link::Sha256Digest(bytes, kLeafSize, digest);
        if (std::memcmp(digest, pin.sha256, sizeof(digest)) != 0) return false;
    }
    return true;
}

void RemoveHooks(std::uintptr_t image, unsigned created) noexcept {
    for (unsigned k = 0; k < created; ++k) {
        void* target = reinterpret_cast<void*>(image + kQueue[k].leaf_rva);
        MH_DisableHook(target);
        MH_RemoveHook(target);
    }
}
} // namespace

const char* RefusalName(Refusal why) noexcept {
    switch (why) {
    case Refusal::not_installed: return "not_installed";
    case Refusal::shutdown: return "shutdown";
    case Refusal::latched: return "latched";
    case Refusal::player: return "player";
    case Refusal::ui: return "ui";
    case Refusal::align: return "align";
    case Refusal::vtable: return "vtable";
    case Refusal::primary: return "primary";
    case Refusal::owner: return "owner";
    case Refusal::fault: return "fault";
    default: return "none";
    }
}

const char* CallerName(std::uint32_t rva) noexcept {
    // Return addresses of the static census (docs/native-puppet.md "Partner HUD hints").
    switch (rva) {
    case 0x11EB5A6: case 0x11EBFF4: case 0x11EC60C: case 0x11EDB17: return "ladder";
    case 0x114FC4E: return "climb";
    case 0x1216899: return "pipe";
    case 0x11A20C3: case 0x11A2BCB: case 0x11A7922: case 0x11A85D8: case 0x11A920B: return "hang";
    case 0x11D03A7: case 0x11D5132: case 0x11D9C87: case 0x11D9DB8: case 0x11D9F83: return "interactive";
    case 0x12C178C: case 0x12C1DBC: case 0x12C2130: return "trash_box";
    case 0x1223E28: return "ride_heli";
    case 0x122F4E5: return "ride_horse";
    case 0x124C816: return "ride_vehicle";
    case 0x12610A1: case 0x126154C: return "ride_walker";
    case 0x1053CE4: return "attack";
    default: return "other";
    }
}

const char* Install(const void* base, std::size_t span, LogFn log) noexcept {
    if (InterlockedCompareExchange(&g_install_state, 1, 0) != 0)
        return g_installed.load(std::memory_order_acquire) ? nullptr : (g_install_result ? g_install_result : "disabled");
    const DWORD error = GetLastError();
    g_log = log;
    const auto image = reinterpret_cast<std::uintptr_t>(base);
    const char* why = nullptr;
    if (!image || image > kMax - span || span < kUiVtableRva + kQueue[kQueues - 1].slot + 8) why = "image";
    else if (!SlotsOk(image)) why = "slots";
    else if (!PinsOk(image)) why = "pins";
    if (!why) {
        const MH_STATUS init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) why = "minhook";
    }
    HMODULE pinned = nullptr;
    if (!why && !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&Install),
                                    &pinned)) why = "module";
    if (!why) {
        g_image = image;
        g_span = span;
        unsigned created = 0;
        for (; created < kQueues; ++created) {
            void* target = reinterpret_cast<void*>(image + kQueue[created].leaf_rva);
            if (MH_CreateHook(target, reinterpret_cast<void*>(kDetours[created]), reinterpret_cast<void**>(&g_original[created])) != MH_OK) break;
        }
        if (created != kQueues) {
            RemoveHooks(image, created);
            why = "hook";
        } else {
            for (unsigned k = 0; k < kQueues && !why; ++k)
                if (MH_EnableHook(reinterpret_cast<void*>(image + kQueue[k].leaf_rva)) != MH_OK) why = "enable";
            if (why) RemoveHooks(image, kQueues);
        }
    }
    g_install_result = why;
    if (!why) g_installed.store(true, std::memory_order_release);
    char line[256]{};
    if (!why)
        StringCchPrintfA(line, ARRAYSIZE(line),
                         "partner_ui_filter=installed; vtable=0x23af700; queues=0x240,0x248,0x250,0x258,0x260; "
                         "leaves=0xfe84f0,0xfe8970,0xfe8560,0xfe87e0,0xfe8480; pins=%u; scope=drain_job_body1\r\n",
                         static_cast<unsigned>(kQueues));
    else StringCchPrintfA(line, ARRAYSIZE(line), "partner_ui_filter=install_refused; reason=%s; hooks=none\r\n", why);
    Log(line);
    SetLastError(error);
    return why;
}

bool Installed() noexcept { return g_installed.load(std::memory_order_acquire); }

bool PatchLive() noexcept {
    if (!g_installed.load(std::memory_order_acquire) || !g_image) return false;
    for (const Queue& q : kQueue) {
        std::uint8_t first = 0;
        if (!Rd(g_image + q.leaf_rva, first) || first != 0xE9) return false;
    }
    return true;
}

void Shutdown() noexcept {
    g_shutdown.store(true, std::memory_order_release);
    InterlockedExchange(&g_active, 0);
}

void Begin(std::uintptr_t P) noexcept {
    End();
    if (!g_installed.load(std::memory_order_acquire) || g_shutdown.load(std::memory_order_acquire)) return;  // key off: silent
    if (g_faulted) {
        Refuse(Refusal::latched);
        return;
    }
    Identity id{};
    if (!ReadIdentity(P, id)) {
        InterlockedIncrement(&g_faults);
        InterlockedExchange(&g_faulted, 1);
        Refuse(Refusal::fault);
        return;
    }
    const Refusal why = CheckIdentity(id, g_image);
    if (why != Refusal::none) {
        Refuse(why);
        return;
    }
    g_digest_ok = RecordsDigest(id.ui, g_digest);
    g_ui = id.ui;
    InterlockedExchange(&g_tid, static_cast<LONG>(GetCurrentThreadId()));
    InterlockedIncrement(&g_windows);
    InterlockedExchange(&g_active, 1);
}

void End() noexcept {
    if (!g_active || static_cast<DWORD>(g_tid) != GetCurrentThreadId()) return;
    InterlockedExchange(&g_active, 0);
    std::uint64_t after = 0;
    if (g_digest_ok && RecordsDigest(g_ui, after) && after != g_digest) InterlockedIncrement(&g_changed);
    g_digest_ok = false;
    InterlockedExchange(&g_tid, 0);
    g_ui = 0;
}

void ResetMission() noexcept { InterlockedExchange(&g_faulted, 0); }

void ReadStats(Stats& out) noexcept {
    out = Stats{};
    out.installed = g_installed.load(std::memory_order_acquire);
    out.live = PatchLive();
    out.latched = g_faulted != 0;
    out.windows = static_cast<std::uint32_t>(g_windows);
    out.refused = static_cast<std::uint32_t>(g_refused);
    out.last_refusal = static_cast<std::uint8_t>(g_last_refusal);
    for (unsigned k = 0; k < kQueues; ++k) out.swallowed[k] = static_cast<std::uint32_t>(g_swallowed[k]);
    out.foreign = static_cast<std::uint32_t>(g_foreign);
    out.passed = static_cast<std::uint32_t>(g_passed);
    out.changed = static_cast<std::uint32_t>(g_changed);
    out.faults = static_cast<std::uint32_t>(g_faults);
    const LONG n = g_keys;
    out.keys = static_cast<std::uint32_t>(n < 0 ? 0 : n > static_cast<LONG>(kKeys) ? kKeys : n);
    for (std::uint32_t i = 0; i < out.keys; ++i) out.key[i] = static_cast<std::uint64_t>(g_key[i]);
}

namespace detail {
void BindForTest(const void* base, const LeafFn (&originals)[kQueues]) noexcept {
    g_image = reinterpret_cast<std::uintptr_t>(base);
    g_span = 0x10000000;
    for (unsigned k = 0; k < kQueues; ++k) g_original[k] = originals[k];
    g_installed.store(true, std::memory_order_release);
    InterlockedExchange(&g_install_state, 1);
}
void ResetForTest() noexcept {
    InterlockedExchange(&g_active, 0);
    InterlockedExchange(&g_tid, 0);
    g_ui = 0;
    g_digest_ok = false;
    g_installed.store(false, std::memory_order_release);
    g_shutdown.store(false, std::memory_order_release);
    InterlockedExchange(&g_install_state, 0);
    g_install_result = nullptr;
    g_image = 0;
    g_span = 0;
    for (auto& f : g_original) f = nullptr;
    InterlockedExchange(&g_faulted, 0);
    InterlockedExchange(&g_windows, 0);
    InterlockedExchange(&g_refused, 0);
    InterlockedExchange(&g_last_refusal, 0);
    InterlockedExchange(&g_foreign, 0);
    InterlockedExchange(&g_passed, 0);
    InterlockedExchange(&g_changed, 0);
    InterlockedExchange(&g_faults, 0);
    for (auto& s : g_swallowed) InterlockedExchange(&s, 0);
    for (auto& k : g_key) InterlockedExchange64(&k, 0);
    InterlockedExchange(&g_keys, 0);
}
LeafFn Detour(unsigned queue) noexcept { return queue < kQueues ? kDetours[queue] : nullptr; }
} // namespace detail

} // namespace mgscoop::partner_ui_filter
