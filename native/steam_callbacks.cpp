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
};

// One lock guards tracking and the queue; drain holds it while invoking, and a
// cross-thread unregister blocks until an in-progress dispatch finishes.
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
unsigned g_queue_head = 0;
unsigned g_queue_count = 0;

const void* expected_vptr(int id) noexcept {
    if (id == kCallbackP2PSessionRequest) return g_vptr_1202;
    if (id == kCallbackP2PSessionConnectFail) return g_vptr_1203;
    return nullptr;
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

bool enqueue(const Event& ev) noexcept {
    lock();
    bool ok = g_queue_count < kMaxQueued;
    if (ok) {
        g_queue[(g_queue_head + g_queue_count) % kMaxQueued] = ev;
        ++g_queue_count;
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
    return enqueue({kCallbackP2PSessionRequest, remote, 0});
}
bool post_connect_fail(std::uint64_t remote, std::uint8_t error) noexcept {
    return enqueue({kCallbackP2PSessionConnectFail, remote, error});
}

unsigned drain() noexcept {
    unsigned dispatched = 0;
    lock();
    while (g_queue_count) {
        const Event ev = g_queue[g_queue_head];
        g_queue_head = (g_queue_head + 1) % kMaxQueued;
        --g_queue_count;
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
        for (unsigned i = 0; i < g_tracked_count; ++i) {
            if (g_tracked[i].id == ev.id && invoke(g_tracked[i].cb, ev.id, payload)) ++dispatched;
        }
    }
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
    g_queue_head = g_queue_count = 0;
    g_vptr_1202 = g_vptr_1203 = nullptr;
    unlock();
}

}  // namespace mgscoop::lab::callbacks
