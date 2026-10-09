// Original MGS Co-op function-pointer dispatch of Steam networking callbacks.
#include "mgscoop/steam_net_dispatch.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "mgscoop/steam_net_callbacks.hpp"

namespace mgscoop::steam_dispatch {
namespace {
namespace sn = steam_net;

volatile LONG g_installed = 0;
volatile LONG g_installs = 0, g_uninstalls = 0, g_install_failed = 0;
volatile LONG64 g_pumps = 0;
volatile LONG g_dispatched = 0, g_req_fn = 0, g_failed_fn = 0, g_status_fn = 0, g_bad_payload = 0, g_faults = 0;

SRWLOCK g_failed_lock = SRWLOCK_INIT;
SessionFailed g_failed{};
bool g_failed_full = false;
std::uint32_t g_failed_sequence = 0;

SRWLOCK g_status_lock = SRWLOCK_INIT;
StatusRecord g_status{};

std::int32_t I32(const std::uint8_t* p) noexcept {
    std::int32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= static_cast<std::int32_t>(static_cast<std::uint32_t>(p[i]) << (8 * i));
    return v;
}

bool Digit(char c) noexcept { return c >= '0' && c <= '9'; }
// 1..3 digits at p; their count (0 when none or more than 3).
std::size_t Octet(const char* p) noexcept {
    std::size_t n = 0;
    while (n < 4 && Digit(p[n])) ++n;
    return n >= 1 && n <= 3 ? n : 0;
}

// The bodies copy Steam's buffer before taking any lock: a fault while
// reading it is caught by the caller's __except and never leaves a lock held.
void SessionRequestBody(const void* payload) noexcept {
    InterlockedIncrement(&g_dispatched);
    if (!payload) return;
    sn::Identity identity{};
    const auto* from = static_cast<const std::uint8_t*>(payload);
    auto* to = reinterpret_cast<std::uint8_t*>(&identity);
    for (std::size_t i = 0; i < sizeof(identity); ++i) to[i] = from[i];
    std::uint64_t id = 0;
    InterlockedIncrement(sn::IdentitySteamId(identity, id) ? &g_req_fn : &g_bad_payload);
    steam_cb::OnSessionRequestFrom(&identity, steam_cb::kSourceFnptr);
}

void SessionFailedBody(const void* payload) noexcept {
    InterlockedIncrement(&g_dispatched);
    InterlockedIncrement(&g_failed_fn);
    if (!payload) return;
    const auto* info = static_cast<const std::uint8_t*>(payload);
    SessionFailed copy{};
    sn::Identity identity{};
    auto* to = reinterpret_cast<std::uint8_t*>(&identity);
    for (std::size_t i = 0; i < sizeof(identity); ++i) to[i] = info[sn::kConnInfoIdentity + i];
    std::uint64_t id = 0;
    if (sn::IdentitySteamId(identity, id)) copy.peer = id;
    copy.state = I32(info + sn::kConnInfoState);
    copy.end_reason = I32(info + sn::kConnInfoEndReason);
    std::size_t n = 0;
    for (; n < kDebugChars && n < sn::kConnInfoEndDebugSize; ++n) {
        const auto c = info[sn::kConnInfoEndDebug + n];
        if (!c) break;
        copy.debug[n] = (c >= 0x20 && c < 0x7F && c != '"' && c != '\\') ? static_cast<char>(c) : '?';
    }
    copy.debug[n] = 0;
    char masked[kDebugChars + 1]{};
    MaskAddresses(copy.debug, masked, sizeof(masked));
    for (std::size_t i = 0; i < sizeof(masked); ++i) copy.debug[i] = masked[i];
    AcquireSRWLockExclusive(&g_failed_lock);
    copy.sequence = ++g_failed_sequence;
    g_failed = copy;
    g_failed_full = true;
    ReleaseSRWLockExclusive(&g_failed_lock);
}

void StatusBody(const void* payload) noexcept {
    InterlockedIncrement(&g_dispatched);
    InterlockedIncrement(&g_status_fn);
    if (!payload) return;
    const auto* p = static_cast<const std::uint8_t*>(payload);
    const auto conn = static_cast<std::uint32_t>(I32(p + sn::kStatusChangedConn));
    const std::int32_t now_state = I32(p + sn::kStatusChangedInfo + sn::kConnInfoState);
    const std::int32_t old_state = I32(p + sn::kStatusChangedOldState);
    const StatusEntry entry{conn, now_state, old_state, static_cast<std::uint32_t>(GetCurrentThreadId())};
    AcquireSRWLockExclusive(&g_status_lock);
    if (g_status.calls < kStatusEntries) g_status.entries[g_status.calls] = entry;
    ++g_status.calls;
    ReleaseSRWLockExclusive(&g_status_lock);
}
}  // namespace

void MaskAddresses(const char* in, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return;
    std::size_t o = 0;
    const char* p = in ? in : "";
    const char* const start = p;
    while (*p && o + 1 < capacity) {
        // a.b.c.d with 1..3 digits each, not inside a longer dotted number.
        const bool boundary = p == start || !(Digit(p[-1]) || p[-1] == '.');
        std::size_t len[4]{}, at = 0, total = 0;
        bool quad = boundary;
        for (unsigned k = 0; quad && k < 4; ++k) {
            len[k] = Octet(p + at);
            if (!len[k]) quad = false;
            at += len[k];
            if (quad && k < 3) {
                if (p[at] != '.') quad = false;
                ++at;
            }
        }
        if (quad && (Digit(p[at]) || (p[at] == '.' && Digit(p[at + 1])))) quad = false;
        if (quad) {
            total = len[0] + 1 + len[1];  // keep a.b
            for (std::size_t i = 0; i < total && o + 1 < capacity; ++i) out[o++] = p[i];
            for (const char* x = ".x.x"; *x && o + 1 < capacity; ++x) out[o++] = *x;
            p += at;
            continue;
        }
        out[o++] = *p++;
    }
    out[o] = 0;
}

// C-style SEH frames: no object with a destructor lives here.
void SessionRequestFn(void* payload) {
    __try {
        SessionRequestBody(payload);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_faults);
    }
}
void SessionFailedFn(void* payload) {
    __try {
        SessionFailedBody(payload);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_faults);
    }
}
void ConnectionStatusFn(void* payload) {
    __try {
        StatusBody(payload);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_faults);
    }
}

bool TrySetConfigPtr(sn::ISteamNetworkingUtils004Abi* utils, std::int32_t value, std::int32_t scope, std::intptr_t object,
                     sn::ConfigCallbackFn fn, bool* fault) {
    // pArg points at the function pointer (steamclient stores *(void**)pArg);
    // null resets the value to its default.
    sn::ConfigCallbackFn slot = fn;
    __try {
        return utils->SetConfigValue(value, scope, object, sn::kConfigTypePtr, fn ? static_cast<const void*>(&slot) : nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
        return false;
    }
}

void TryRunCallbacks(sn::ISteamNetworkingSockets012Abi* sockets, bool* fault) {
    __try {
        sockets->RunCallbacks();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *fault = true;
    }
}

bool Install(const Api& api, std::uint8_t bits, const char*& reason, bool& fault) noexcept {
    reason = nullptr;
    fault = false;
    if (!bits) return true;
    if (!api.utils || !api.sockets) {
        reason = "no_interface";
        InterlockedIncrement(&g_install_failed);
        return false;
    }
    std::uint8_t done = 0;
    if (bits & kRequest) {
        if (!TrySetConfigPtr(api.utils, sn::kConfigMessagesSessionRequest, sn::kConfigScopeGlobal, 0, &SessionRequestFn, &fault) || fault)
            reason = fault ? "fault" : "set_204";
        else
            done = static_cast<std::uint8_t>(done | kRequest);
    }
    if (!reason && (bits & kFailed)) {
        if (!TrySetConfigPtr(api.utils, sn::kConfigMessagesSessionFailed, sn::kConfigScopeGlobal, 0, &SessionFailedFn, &fault) || fault)
            reason = fault ? "fault" : "set_205";
        else
            done = static_cast<std::uint8_t>(done | kFailed);
    }
    if (reason) {
        // All or nothing (no further Steam call after a fault).
        if (!fault && (done & kRequest))
            TrySetConfigPtr(api.utils, sn::kConfigMessagesSessionRequest, sn::kConfigScopeGlobal, 0, nullptr, &fault);
        InterlockedIncrement(&g_install_failed);
        if (!fault) InterlockedExchange(&g_installed, 0);
        else InterlockedExchange(&g_installed, done);
        return false;
    }
    InterlockedExchange(&g_installed, done);
    InterlockedIncrement(&g_installs);
    return true;
}

void Pump(const Api& api, bool& fault) noexcept {
    fault = false;
    if (!api.sockets) return;
    TryRunCallbacks(api.sockets, &fault);
    InterlockedIncrement64(&g_pumps);
}

void Uninstall(const Api& api, bool& fault) noexcept {
    fault = false;
    const auto bits = static_cast<std::uint8_t>(InterlockedCompareExchange(&g_installed, 0, 0));
    if (!bits || !api.utils) return;
    if (bits & kRequest)
        TrySetConfigPtr(api.utils, sn::kConfigMessagesSessionRequest, sn::kConfigScopeGlobal, 0, nullptr, &fault);
    if (!fault && (bits & kFailed))
        TrySetConfigPtr(api.utils, sn::kConfigMessagesSessionFailed, sn::kConfigScopeGlobal, 0, nullptr, &fault);
    if (fault) return;
    InterlockedExchange(&g_installed, 0);
    InterlockedIncrement(&g_uninstalls);
    // Nothing new is queued now; drain what was queued before the reset.
    Pump(api, fault);
}

std::uint8_t Installed() noexcept { return static_cast<std::uint8_t>(InterlockedCompareExchange(&g_installed, 0, 0)); }

bool TakeSessionFailed(SessionFailed& out) noexcept {
    AcquireSRWLockExclusive(&g_failed_lock);
    const bool any = g_failed_full;
    if (any) {
        out = g_failed;
        g_failed_full = false;
    }
    ReleaseSRWLockExclusive(&g_failed_lock);
    return any;
}

StatusRecord GetStatusRecord() noexcept {
    AcquireSRWLockShared(&g_status_lock);
    const StatusRecord record = g_status;
    ReleaseSRWLockShared(&g_status_lock);
    return record;
}

void ResetStatusRecord() noexcept {
    AcquireSRWLockExclusive(&g_status_lock);
    g_status = StatusRecord{};
    ReleaseSRWLockExclusive(&g_status_lock);
}

Stats GetStats() noexcept {
    Stats s{};
    s.installed = static_cast<std::uint32_t>(InterlockedCompareExchange(&g_installed, 0, 0));
    s.installs = static_cast<std::uint32_t>(g_installs);
    s.uninstalls = static_cast<std::uint32_t>(g_uninstalls);
    s.install_failed = static_cast<std::uint32_t>(g_install_failed);
    s.pumps = static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_pumps, 0, 0));
    s.dispatched = static_cast<std::uint32_t>(g_dispatched);
    s.req_fn = static_cast<std::uint32_t>(g_req_fn);
    s.failed_fn = static_cast<std::uint32_t>(g_failed_fn);
    s.status_fn = static_cast<std::uint32_t>(g_status_fn);
    s.bad_payload = static_cast<std::uint32_t>(g_bad_payload);
    s.faults = static_cast<std::uint32_t>(g_faults);
    return s;
}

void ResetForTest() noexcept {
    InterlockedExchange(&g_installed, 0);
    g_installs = g_uninstalls = g_install_failed = 0;
    InterlockedExchange64(&g_pumps, 0);
    g_dispatched = g_req_fn = g_failed_fn = g_status_fn = g_bad_payload = g_faults = 0;
    AcquireSRWLockExclusive(&g_failed_lock);
    g_failed = SessionFailed{};
    g_failed_full = false;
    g_failed_sequence = 0;
    ReleaseSRWLockExclusive(&g_failed_lock);
    ResetStatusRecord();
}

}  // namespace mgscoop::steam_dispatch
