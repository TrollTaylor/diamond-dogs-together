// Original co-op lab synthetic P2P callback registry and dispatch.
#include "mgscoop/steam_callbacks.hpp"

#include "mgscoop/steam_abi.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace mgscoop::lab::callbacks {
namespace {
using steam::kCallbackFlagRegistered;
using steam::kCallbackFlagsOffset;
using steam::kCallbackIdOffset;
using steam::kCallbackRunSlot;
using steam::kCallbackP2PSessionRequest;
using steam::kCallbackP2PSessionConnectFail;

constexpr unsigned kMaxTracked = 8;
constexpr unsigned kMaxQueued = 128;

struct Tracked {
    void* cb;
    int id;
};
struct Event {
    int id;
    std::uint64_t remote;
    std::uint8_t error;
    std::uint64_t queued_ms;
};

// One recursive lock guards tracking and the queue. drain() holds it while
// invoking, so a cross-thread unregister blocks until the dispatch finishes,
// and a same-thread (reentrant) unregister from inside Run is seen by the
// re-check before the next invocation.
CRITICAL_SECTION g_lock;
INIT_ONCE g_lock_once = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK init_lock(PINIT_ONCE, PVOID, PVOID*) noexcept {
    InitializeCriticalSection(&g_lock);
    return TRUE;
}
void lock() noexcept {
    InitOnceExecuteOnce(&g_lock_once, init_lock, nullptr, nullptr);
    EnterCriticalSection(&g_lock);
}
void unlock() noexcept { LeaveCriticalSection(&g_lock); }

const void* g_vptr_1202 = nullptr;
const void* g_vptr_1203 = nullptr;
Tracked g_tracked[kMaxTracked]{};
unsigned g_tracked_count = 0;
Event g_queue[kMaxQueued]{};
unsigned g_queue_count = 0;  // g_queue[0..count) in FIFO order
std::uint64_t (*g_clock)() noexcept = nullptr;
bool g_draining = false;

std::uint64_t now_ms() noexcept { return g_clock ? g_clock() : GetTickCount64(); }

const void* expected_vptr(int id) noexcept {
    if (id == kCallbackP2PSessionRequest) return g_vptr_1202;
    if (id == kCallbackP2PSessionConnectFail) return g_vptr_1203;
    return nullptr;
}

bool is_tracked(void* cb, int id) noexcept {
    for (unsigned i = 0; i < g_tracked_count; ++i)
        if (g_tracked[i].cb == cb && g_tracked[i].id == id) return true;
    return false;
}
bool any_tracked(int id) noexcept {
    for (unsigned i = 0; i < g_tracked_count; ++i)
        if (g_tracked[i].id == id) return true;
    return false;
}

// Validate a tracked object against the game's exact CCallback shape, then call
// its Run slot with the payload. Returns true if invoked.
bool invoke(void* cb, int id, const void* payload) noexcept {
    if (!cb) return false;
    const void* want = expected_vptr(id);
    if (!want) return false;
    auto* bytes = static_cast<const unsigned char*>(cb);
    const void* vptr = *reinterpret_cast<void* const*>(bytes);
    if (vptr != want) return false;
    const std::uint8_t flags = *reinterpret_cast<const std::uint8_t*>(bytes + kCallbackFlagsOffset);
    if ((flags & kCallbackFlagRegistered) == 0) return false;
    const std::int32_t cb_id = *reinterpret_cast<const std::int32_t*>(bytes + kCallbackIdOffset);
    if (cb_id != id) return false;
    auto* const* vtable = *reinterpret_cast<void* const* const*>(cb);
    using RunFn = void(MGSCOOP_STEAM_CALL*)(void*, const void*);
    reinterpret_cast<RunFn>(vtable[kCallbackRunSlot])(cb, payload);
    return true;
}

void remove_at(unsigned index) noexcept {
    for (unsigned j = index; j + 1 < g_queue_count; ++j) g_queue[j] = g_queue[j + 1];
    g_queue[--g_queue_count] = Event{};
}

bool enqueue(int id, std::uint64_t remote, std::uint8_t error) noexcept {
    lock();
    // Coalesce with the newest pending event for the same remote.
    bool ok = true, coalesced = false;
    for (unsigned i = g_queue_count; i-- > 0;) {
        if (g_queue[i].remote != remote) continue;
        coalesced = g_queue[i].id == id && g_queue[i].error == error;
        break;
    }
    if (!coalesced) {
        ok = g_queue_count < kMaxQueued;
        if (ok) g_queue[g_queue_count++] = {id, remote, error, now_ms()};
    }
    unlock();
    return ok;
}
}  // namespace

void configure(const void* vptr_1202, const void* vptr_1203) noexcept {
    lock();
    g_vptr_1202 = vptr_1202;
    g_vptr_1203 = vptr_1203;
    unlock();
}

void on_register(void* cb, int id) noexcept {
    if (!cb || (id != kCallbackP2PSessionRequest && id != kCallbackP2PSessionConnectFail)) return;
    lock();
    bool present = false;
    for (unsigned i = 0; i < g_tracked_count; ++i)
        if (g_tracked[i].cb == cb) { g_tracked[i].id = id; present = true; break; }
    if (!present && g_tracked_count < kMaxTracked) g_tracked[g_tracked_count++] = {cb, id};
    unlock();
}

void on_unregister(void* cb) noexcept {
    if (!cb) return;
    lock();
    for (unsigned i = 0; i < g_tracked_count; ++i) {
        if (g_tracked[i].cb == cb) {
            g_tracked[i] = g_tracked[--g_tracked_count];
            g_tracked[g_tracked_count] = {nullptr, 0};
            break;
        }
    }
    unlock();
}

bool post_session_request(std::uint64_t remote) noexcept {
    return enqueue(kCallbackP2PSessionRequest, remote, 0);
}
bool post_connect_fail(std::uint64_t remote, std::uint8_t error) noexcept {
    return enqueue(kCallbackP2PSessionConnectFail, remote, error);
}

unsigned drain() noexcept { return drain_at(now_ms()); }

unsigned drain_at(std::uint64_t now) noexcept {
    unsigned dispatched = 0;
    lock();
    if (g_draining) {  // no nested drain from inside a Run handler
        unlock();
        return 0;
    }
    g_draining = true;
    // Only events present at entry are considered; events posted during
    // dispatch (reentrantly) are appended after `limit` and wait for the next
    // drain. Each event is removed before its handlers run.
    unsigned limit = g_queue_count;
    for (unsigned i = 0; i < limit;) {
        const Event ev = g_queue[i];
        if (now > ev.queued_ms && now - ev.queued_ms > kMaxEventAgeMs) {  // stale
            remove_at(i);
            --limit;
            continue;
        }
        if (!any_tracked(ev.id)) {
            if (ev.id == kCallbackP2PSessionRequest) {
                ++i;  // keep: wait for a listener
            } else {
                remove_at(i);  // 1203 with no listener: nobody knows that session
                --limit;
            }
            continue;
        }
        remove_at(i);
        --limit;
        steam::P2PSessionRequest_t request{};
        steam::P2PSessionConnectFail_t fail{};
        const void* payload = nullptr;
        if (ev.id == kCallbackP2PSessionRequest) {
            request.steam_id_remote = ev.remote;
            payload = &request;
        } else {
            fail.steam_id_remote = ev.remote;
            fail.p2p_session_error = ev.error;
            payload = &fail;
        }
        // Snapshot the matching objects, then re-check each one immediately
        // before invoking: a Run handler may unregister (itself or another).
        void* targets[kMaxTracked];
        unsigned target_count = 0;
        for (unsigned t = 0; t < g_tracked_count; ++t)
            if (g_tracked[t].id == ev.id) targets[target_count++] = g_tracked[t].cb;
        for (unsigned t = 0; t < target_count; ++t)
            if (is_tracked(targets[t], ev.id) && invoke(targets[t], ev.id, payload)) ++dispatched;
    }
    g_draining = false;
    unlock();
    return dispatched;
}

unsigned pending() noexcept {
    lock();
    const unsigned n = g_queue_count;
    unlock();
    return n;
}

unsigned tracked(int id) noexcept {
    lock();
    unsigned n = 0;
    for (unsigned i = 0; i < g_tracked_count; ++i)
        if (g_tracked[i].id == id) ++n;
    unlock();
    return n;
}

void reset_for_test() noexcept {
    lock();
    for (auto& t : g_tracked) t = {nullptr, 0};
    g_tracked_count = 0;
    g_queue_count = 0;
    g_vptr_1202 = g_vptr_1203 = nullptr;
    g_clock = nullptr;
    g_draining = false;
    unlock();
}

void set_clock_for_test(std::uint64_t (*clock)() noexcept) noexcept {
    lock();
    g_clock = clock;
    unlock();
}

}  // namespace mgscoop::lab::callbacks
