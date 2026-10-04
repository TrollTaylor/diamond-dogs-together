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
//
// Queue policy: an event is consumed only when at least one object for its id
// is tracked. A 1202 posted while no 1202 object is tracked stays queued (the
// host may create its session after the guest started asking) until it is
// older than kMaxEventAgeMs; a 1203 with no listener is dropped (nobody knows
// that session). Posting an event identical to the newest pending event for the
// same remote is coalesced.
#include <cstddef>
#include <cstdint>

namespace mgscoop::lab::callbacks {

constexpr std::uint64_t kMaxEventAgeMs = 10000;

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

// Invoke the registered callback(s) for queued events on the current thread
// (the one that just returned from the real RunCallbacks). Returns the number
// of Run invocations. Safe against a concurrent or reentrant unregister: an
// object is re-checked as tracked immediately before each invocation, under
// the same lock Unregister takes. drain() uses GetTickCount64; drain_at() takes
// the clock explicitly (tests).
unsigned drain() noexcept;
unsigned drain_at(std::uint64_t now_ms) noexcept;

// Number of pending queued events (test/diagnostic).
unsigned pending() noexcept;
// Number of tracked objects for an id (test/diagnostic).
unsigned tracked(int id) noexcept;

void reset_for_test() noexcept;

// Test hook: the clock used to stamp posted events (null = GetTickCount64).
void set_clock_for_test(std::uint64_t (*now_ms)() noexcept) noexcept;

}  // namespace mgscoop::lab::callbacks
