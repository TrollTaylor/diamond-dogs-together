#pragma once
// Our two Steam callbacks (docs/steam-online.md 6.1-6.2): hand-built
// CCallbackBase objects for 1251 (SteamNetworkingMessagesSessionRequest_t,
// host) and 337 (GameRichPresenceJoinRequested_t, guest), registered once by
// the SteamAPI_Init IAT wrapper (early_patches) and never unregistered.
//
// The handlers run on the game's callback threads inside steam_api64's
// dispatch: they never block, log or call Steam. Each body runs under __try
// and only enqueues:
//   1251: identity type 16 / size 8 / individual SteamID -> a 16-entry queue
//         (SRW lock held only for the push), duplicates coalesced, full ->
//         dropped (counted). The same queue takes the requests of the
//         function-pointer dispatch (steam_dispatch, config 204): each entry
//         carries the mask of the sources that reported it.
//   337:  the 264-byte payload -> a single-slot mailbox (newest wins, at most
//         one per 2 s); parsing happens on the transport thread.
// Constant-initialized static state; nothing allocates.
#include <cstddef>
#include <cstdint>
#include "mgscoop/steam_net_abi.hpp"

namespace mgscoop::steam_cb {

inline constexpr unsigned kQueueSize = 16;
inline constexpr std::uint64_t kJoinMinGapMs = 2000;

// The registered objects (static storage; RegisterCallback sets flags/id).
steam_net::CallbackObject* SessionRequestObject() noexcept;
steam_net::CallbackObject* JoinRequestObject() noexcept;
// The static vtables (slot 0 Run(p, io, call), 1 Run(p), 2 GetCallbackSizeBytes).
const void* const* SessionRequestVtable() noexcept;
const void* const* JoinRequestVtable() noexcept;

// Where a session request came from (docs/steam-online.md section 20).
inline constexpr std::uint8_t kSourceCallback = 1;  // 1251 through the 2015 steam_api (Init wrapper)
inline constexpr std::uint8_t kSourceFnptr = 2;     // config 204 + Sockets012::RunCallbacks
// "callback", "fnptr", "fnptr+callback" (or "none").
const char* SourceName(std::uint8_t sources) noexcept;
// Transport thread: the oldest queued request (SteamID64) and its sources;
// false when empty.
bool PopSessionRequest(std::uint64_t& id, std::uint8_t& sources) noexcept;
// A request from a source other than the registered 1251 object (the
// function-pointer dispatch): the 136-byte identity, validated as 1251.
void OnSessionRequestFrom(const void* payload, std::uint8_t source) noexcept;
// Transport thread: takes the mailbox (copied out, slot cleared).
bool TakeJoinRequest(steam_net::JoinRequested& out) noexcept;

struct Stats {
    std::uint32_t requests;      // accepted pushes
    std::uint32_t coalesced;     // duplicates of a queued id
    std::uint32_t dropped;       // queue full
    std::uint32_t bad_identity;  // not type 16 / size 8 / individual
    std::uint32_t joins;         // 337 posts stored
    std::uint32_t join_throttled;
    std::uint32_t faults;        // __except in a handler
    std::uint32_t req_cb;        // valid requests per source (before coalescing)
    std::uint32_t req_fn;
};
Stats GetStats() noexcept;

// The handler bodies (also what the vtables call). `now_ms` is the 337 rate
// clock (the vtable passes GetTickCount64()).
void OnSessionRequest(const void* payload) noexcept;
void OnJoinRequested(const void* payload, std::uint64_t now_ms) noexcept;
// Tests only: empties the queue, the mailbox and the counters.
void ResetForTest() noexcept;

}  // namespace mgscoop::steam_cb
