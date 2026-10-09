// Original MGS Co-op Steam online shared state (UI mailbox, self-test report).
#include "mgscoop/steam_online_state.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>

namespace mgscoop::steam_state {
namespace {
SRWLOCK g_report_lock = SRWLOCK_INIT;
SelftestReport g_report{};
SRWLOCK g_online_lock = SRWLOCK_INIT;
OnlineSnapshot g_online{};
SRWLOCK g_request_lock = SRWLOCK_INIT;
bool g_accepting_pending = false, g_accepting_value = true;
std::uint64_t g_invite_id = 0;
bool g_invite_pending = false;
char g_join[kPasteCapacity]{};
bool g_join_pending = false;
volatile LONG64 g_invite_page_ms = 0;
void* volatile g_provider = nullptr;

template <std::size_t N>
void Copy(char (&dst)[N], const char* src) noexcept {
    std::size_t i = 0;
    for (; src && src[i] && i + 1 < N; ++i) dst[i] = src[i];
    dst[i] = 0;
}
}  // namespace

void PublishReport(const SelftestReport& report) noexcept {
    AcquireSRWLockExclusive(&g_report_lock);
    g_report = report;
    ReleaseSRWLockExclusive(&g_report_lock);
}

SelftestReport GetReport() noexcept {
    AcquireSRWLockShared(&g_report_lock);
    const SelftestReport report = g_report;
    ReleaseSRWLockShared(&g_report_lock);
    return report;
}

bool FormatStatusLine(const SelftestReport& r, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    int n = 0;
    if (!r.configured) n = std::snprintf(out, capacity, "steam_selftest;verdict=off");
    else if (!r.verdict[0]) n = std::snprintf(out, capacity, "steam_selftest;verdict=pending;stage=%s", r.stage[0] ? r.stage : "wait");
    else
        n = std::snprintf(out, capacity, "steam_selftest;verdict=%s;reason=%s;stage=%s;pair=%u/%u;rel=%u/%u;release=%llu/%llu;relay=%d",
                          r.verdict, r.reason, r.stage, r.pair.frames_rx, r.pair.frames, r.pair.rel_rx, r.pair.rel,
                          static_cast<unsigned long long>(r.pair.release), static_cast<unsigned long long>(r.pair.received), r.relay);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return false;
    }
    return true;
}

void PublishOnline(const OnlineSnapshot& snapshot) noexcept {
    AcquireSRWLockExclusive(&g_online_lock);
    const std::uint32_t generation = g_online.generation + 1;
    g_online = snapshot;
    g_online.generation = generation;
    ReleaseSRWLockExclusive(&g_online_lock);
}

OnlineSnapshot GetOnline() noexcept {
    AcquireSRWLockShared(&g_online_lock);
    const OnlineSnapshot snapshot = g_online;
    ReleaseSRWLockShared(&g_online_lock);
    return snapshot;
}

void PublishLink(const char* state, const char* route, const char* reason, std::uint32_t rtt_ms, const char* peer, bool waiting,
                 bool accepting) noexcept {
    AcquireSRWLockExclusive(&g_online_lock);
    Copy(g_online.state, state);
    Copy(g_online.route, route);
    Copy(g_online.reason, reason);
    Copy(g_online.peer, peer);
    g_online.rtt_ms = rtt_ms;
    g_online.waiting = waiting;
    g_online.accepting = accepting;
    ++g_online.generation;
    ReleaseSRWLockExclusive(&g_online_lock);
}

void RequestAccepting(bool accepting) noexcept {
    AcquireSRWLockExclusive(&g_request_lock);
    g_accepting_pending = true;
    g_accepting_value = accepting;
    ReleaseSRWLockExclusive(&g_request_lock);
}

bool TakeAccepting(bool& accepting) noexcept {
    AcquireSRWLockExclusive(&g_request_lock);
    const bool any = g_accepting_pending;
    accepting = g_accepting_value;
    g_accepting_pending = false;
    ReleaseSRWLockExclusive(&g_request_lock);
    return any;
}

void NoteInvitePage(std::uint64_t now_ms) noexcept { InterlockedExchange64(&g_invite_page_ms, static_cast<LONG64>(now_ms)); }
std::uint64_t InvitePageMs() noexcept { return static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_invite_page_ms, 0, 0)); }

bool RequestInvite(std::uint64_t id) noexcept {
    if (!id) return false;
    AcquireSRWLockExclusive(&g_request_lock);
    g_invite_id = id;
    g_invite_pending = true;
    ReleaseSRWLockExclusive(&g_request_lock);
    return true;
}

bool TakeInvite(std::uint64_t& id) noexcept {
    AcquireSRWLockExclusive(&g_request_lock);
    const bool any = g_invite_pending;
    id = g_invite_id;
    g_invite_pending = false;
    g_invite_id = 0;
    ReleaseSRWLockExclusive(&g_request_lock);
    return any;
}

bool RequestJoin(const char* text) noexcept {
    if (!text || !text[0]) return false;
    std::size_t n = 0;
    while (text[n] && n < kPasteCapacity) ++n;
    if (n >= kPasteCapacity) return false;
    AcquireSRWLockExclusive(&g_request_lock);
    for (std::size_t i = 0; i < kPasteCapacity; ++i) g_join[i] = i < n ? text[i] : 0;
    g_join_pending = true;
    ReleaseSRWLockExclusive(&g_request_lock);
    return true;
}

bool TakeJoin(char (&out)[kPasteCapacity]) noexcept {
    AcquireSRWLockExclusive(&g_request_lock);
    const bool any = g_join_pending;
    for (std::size_t i = 0; i < kPasteCapacity; ++i) out[i] = any ? g_join[i] : 0;
    SecureZeroMemory(g_join, sizeof(g_join));
    g_join_pending = false;
    ReleaseSRWLockExclusive(&g_request_lock);
    return any;
}

void SetCodeProvider(CodeProvider provider) noexcept {
    InterlockedExchangePointer(&g_provider, reinterpret_cast<void*>(provider));
}

bool BuildCode(char* out, std::size_t capacity) noexcept {
    const auto provider = reinterpret_cast<CodeProvider>(InterlockedCompareExchangePointer(&g_provider, nullptr, nullptr));
    if (!out || !capacity) return false;
    out[0] = 0;
    return provider && provider(out, capacity);
}

void ResetForTest() noexcept {
    AcquireSRWLockExclusive(&g_report_lock);
    g_report = SelftestReport{};
    ReleaseSRWLockExclusive(&g_report_lock);
    AcquireSRWLockExclusive(&g_online_lock);
    g_online = OnlineSnapshot{};
    ReleaseSRWLockExclusive(&g_online_lock);
    AcquireSRWLockExclusive(&g_request_lock);
    g_accepting_pending = g_invite_pending = g_join_pending = false;
    g_accepting_value = true;
    g_invite_id = 0;
    SecureZeroMemory(g_join, sizeof(g_join));
    ReleaseSRWLockExclusive(&g_request_lock);
    g_invite_page_ms = 0;
    SetCodeProvider(nullptr);
}

}  // namespace mgscoop::steam_state
