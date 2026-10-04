#pragma once
// Synthetic Steam P2P callback injection for the co-op lab loopback mode.
//
// The game registers one CCallback for 1202 (P2PSessionRequest_t) and one for
// 1203 (P2PSessionConnectFail_t) via SteamAPI_RegisterCallback. The lab wraps
// RegisterCallback / UnregisterCallback (both always forward to the real API)
// to track those objects, and drains a synthetic event queue right after the
// real SteamAPI_RunCallbacks returns, on that same thread. Dispatch validates
// each tracked object's vptr, registered flag and callback-id against the exact
// game values before invoking the Run slot (slot 1, verified against the
// steam_api64 dispatch at RVA 0x2DF7 which calls [vtable+8]).
#include <cstddef>
#include <cstdint>

namespace mgscoop::lab::callbacks {

// Expected CCallback vptrs from the running game image (absolute addresses
// 0x24BF400 for 1202, 0x24BF418 for 1203). When either is null, dispatch for
// that id is disabled (fail safe). Testable with any two pointers.
void configure(const void* vptr_1202, const void* vptr_1203) noexcept;

// Track objects the game registers/unregisters. Only 1202/1203 are retained.
void on_register(void* cb, int id) noexcept;
void on_unregister(void* cb) noexcept;

// Queue a synthetic event (enqueue only; never invokes game code). Safe from
// the transport receive thread. Returns false if the queue is full.
bool post_session_request(std::uint64_t remote) noexcept;
bool post_connect_fail(std::uint64_t remote, std::uint8_t error) noexcept;

// Invoke the registered callback(s) for every queued event on the current
// thread (the one that just returned from the real RunCallbacks). Returns the
// number of events dispatched. Safe against a concurrent unregister.
unsigned drain() noexcept;

// Number of pending queued events (test/diagnostic).
unsigned pending() noexcept;
// Number of tracked objects for an id (test/diagnostic).
unsigned tracked(int id) noexcept;

void reset_for_test() noexcept;

}  // namespace mgscoop::lab::callbacks
