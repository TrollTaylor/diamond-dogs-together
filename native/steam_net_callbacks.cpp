// Original MGS Co-op Steam callback objects for 1251 and 337 (enqueue only).
#include "mgscoop/steam_net_callbacks.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace mgscoop::steam_cb {
namespace {
SRWLOCK g_lock = SRWLOCK_INIT;
std::uint64_t g_queue[kQueueSize]{};
unsigned g_head = 0, g_count = 0;
bool g_join_full = false;
steam_net::JoinRequested g_join{};
std::uint64_t g_join_ms = 0;
bool g_join_seen = false;
volatile LONG g_requests = 0, g_coalesced = 0, g_dropped = 0, g_bad_identity = 0, g_joins = 0, g_join_throttled = 0, g_faults = 0;

void Push(std::uint64_t id) noexcept {
    bool coalesced = false, dropped = false;
    AcquireSRWLockExclusive(&g_lock);
    for (unsigned i = 0; i < g_count; ++i)
        if (g_queue[(g_head + i) % kQueueSize] == id) { coalesced = true; break; }
    if (!coalesced) {
        if (g_count == kQueueSize) dropped = true;
        else g_queue[(g_head + g_count++) % kQueueSize] = id;
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedIncrement(coalesced ? &g_coalesced : dropped ? &g_dropped : &g_requests);
}

// C-style SEH frames: no object with a destructor lives here.
void Run1251(void*, void* payload) {
    __try {
        OnSessionRequest(payload);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_faults);
    }
}
void RunIo1251(void* self, void* payload, bool, std::uint64_t) { Run1251(self, payload); }
int Size1251(void*) { return steam_net::kSessionRequestSize; }
void Run337(void*, void* payload) {
    __try {
        OnJoinRequested(payload, GetTickCount64());
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        InterlockedIncrement(&g_faults);
    }
}
void RunIo337(void* self, void* payload, bool, std::uint64_t) { Run337(self, payload); }
int Size337(void*) { return steam_net::kJoinRequestedSize; }

// Slot order as the game's own CCallback vtables: Run(p, io, call), Run(p), size.
const void* const g_vt1251[3] = {reinterpret_cast<const void*>(&RunIo1251), reinterpret_cast<const void*>(&Run1251),
                                 reinterpret_cast<const void*>(&Size1251)};
const void* const g_vt337[3] = {reinterpret_cast<const void*>(&RunIo337), reinterpret_cast<const void*>(&Run337),
                                reinterpret_cast<const void*>(&Size337)};
// flags start at 0: the GameServer bit stays clear, RegisterCallback sets bit 0.
steam_net::CallbackObject g_cb1251{g_vt1251, 0, {0, 0, 0}, 0};
steam_net::CallbackObject g_cb337{g_vt337, 0, {0, 0, 0}, 0};
}  // namespace

steam_net::CallbackObject* SessionRequestObject() noexcept { return &g_cb1251; }
steam_net::CallbackObject* JoinRequestObject() noexcept { return &g_cb337; }
const void* const* SessionRequestVtable() noexcept { return g_vt1251; }
const void* const* JoinRequestVtable() noexcept { return g_vt337; }

void OnSessionRequest(const void* payload) noexcept {
    if (!payload) return;
    std::uint64_t id = 0;
    if (!steam_net::IdentitySteamId(*static_cast<const steam_net::Identity*>(payload), id)) {
        InterlockedIncrement(&g_bad_identity);
        return;
    }
    Push(id);
}

void OnJoinRequested(const void* payload, std::uint64_t now_ms) noexcept {
    if (!payload) return;
    bool throttled = false;
    AcquireSRWLockExclusive(&g_lock);
    if (g_join_seen && now_ms - g_join_ms < kJoinMinGapMs) {
        throttled = true;
    } else {
        const auto* bytes = static_cast<const std::uint8_t*>(payload);
        auto* out = reinterpret_cast<std::uint8_t*>(&g_join);
        for (std::size_t i = 0; i < sizeof(g_join); ++i) out[i] = bytes[i];
        g_join_full = true;
        g_join_seen = true;
        g_join_ms = now_ms;
    }
    ReleaseSRWLockExclusive(&g_lock);
    InterlockedIncrement(throttled ? &g_join_throttled : &g_joins);
}

bool PopSessionRequest(std::uint64_t& id) noexcept {
    id = 0;
    AcquireSRWLockExclusive(&g_lock);
    const bool any = g_count != 0;
    if (any) {
        id = g_queue[g_head];
        g_head = (g_head + 1) % kQueueSize;
        --g_count;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return any;
}

bool TakeJoinRequest(steam_net::JoinRequested& out) noexcept {
    AcquireSRWLockExclusive(&g_lock);
    const bool any = g_join_full;
    if (any) {
        out = g_join;
        SecureZeroMemory(&g_join, sizeof(g_join));
        g_join_full = false;
    }
    ReleaseSRWLockExclusive(&g_lock);
    return any;
}

Stats GetStats() noexcept {
    return Stats{static_cast<std::uint32_t>(g_requests), static_cast<std::uint32_t>(g_coalesced), static_cast<std::uint32_t>(g_dropped),
                 static_cast<std::uint32_t>(g_bad_identity), static_cast<std::uint32_t>(g_joins),
                 static_cast<std::uint32_t>(g_join_throttled), static_cast<std::uint32_t>(g_faults)};
}

void ResetForTest() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_head = g_count = 0;
    g_join_full = g_join_seen = false;
    g_join_ms = 0;
    SecureZeroMemory(&g_join, sizeof(g_join));
    ReleaseSRWLockExclusive(&g_lock);
    g_requests = g_coalesced = g_dropped = g_bad_identity = g_joins = g_join_throttled = g_faults = 0;
}

}  // namespace mgscoop::steam_cb
