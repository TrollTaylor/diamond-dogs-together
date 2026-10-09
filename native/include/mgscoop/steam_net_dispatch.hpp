#pragma once
// Function-pointer dispatch of Steam networking callbacks (docs/steam-online.md
// section 20): code joins between players who are not Steam friends no longer
// depend on the 2015 steam_api's callback manager alone.
//
// ISteamNetworkingUtils004::SetConfigValue (slot 16) stores a Ptr config value
// (Global scope for 204 Callback_MessagesSessionRequest and 205
// Callback_MessagesSessionFailed; Connection scope for 201
// Callback_ConnectionStatusChanged). steamclient then queues the callback for
// ISteamNetworkingSockets012::RunCallbacks (slot 41), which calls the function
// with rcx = the payload on the calling thread, outside every Steam lock. It
// still posts the same callback to the game's pipe, so the Init wrapper's
// 1251 object stays a second source; the game registers neither id.
//
// Threads: Install/Pump/Uninstall run on our Steam transport thread (or the
// lab self-test thread) only; the functions below run inside our own
// RunCallbacks call. They only copy and enqueue (steam_cb queue, mailboxes),
// never block on a game lock, never log, never call Steam and run under
// __try. While a pointer is set the transport pumps at least every 50 ms
// (steamclient asserts once 100 callbacks back up); it resets the pointers
// before the thread stops (stop() and the window-close shutdown). After a
// fault no Steam call follows, so a pointer may stay set: only incoming
// session requests then grow the queue (logged). Constant-initialized state;
// nothing allocates; dinput8.dll is never unloaded, so the functions stay valid.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/steam_net_abi.hpp"

namespace mgscoop::steam_dispatch {

// lab.ini net_steam_dispatch (play): which sources find a code join.
//   both     function pointer + the Init wrapper's 1251 (+ polling)   (default)
//   fnptr    function pointer only (the guest keeps 337 for invites)
//   callback the Init wrapper only (the behaviour before section 20)
//   poll     neither: polling of known ids and the friends beacon only
using Variant = SteamDispatch;
inline const char* VariantName(Variant value) noexcept { return SteamDispatchName(value); }
inline bool UsesFnptr(Variant value) noexcept { return value == Variant::both || value == Variant::fnptr; }
// The SteamAPI_Init wrapper: 1251 (host) / 337 (guest). fnptr keeps the
// guest's 337 (invites need it); poll registers nothing.
inline bool UsesInitWrapper(Variant value, bool host) noexcept {
    return value == Variant::both || value == Variant::callback || (value == Variant::fnptr && !host);
}

// Install bits: 204 (session request) and 205 (session failed).
inline constexpr std::uint8_t kRequest = 1;
inline constexpr std::uint8_t kFailed = 2;
// The host installs both, the guest only 205 (it never accepts a request).
inline std::uint8_t InstallBits(Variant value, bool host) noexcept {
    if (!UsesFnptr(value)) return 0;
    return host ? static_cast<std::uint8_t>(kRequest | kFailed) : kFailed;
}

// ---- The functions steamclient calls (rcx = payload). Exposed for tests and
// the self-test; C-style __try bodies.
void SessionRequestFn(void* payload);   // 1251: SteamNetworkingIdentity (136 bytes)
void SessionFailedFn(void* payload);    // 1252: SteamNetConnectionInfo_t (0x2B8)
void ConnectionStatusFn(void* payload); // 1221: {hConn; info; old state} (0x2C8; self-test only)

// ---- Calls into steamclient (C-style SEH thunks; `fault` set on an exception).
bool TrySetConfigPtr(steam_net::ISteamNetworkingUtils004Abi* utils, std::int32_t value, std::int32_t scope, std::intptr_t object,
                     steam_net::ConfigCallbackFn fn, bool* fault);
void TryRunCallbacks(steam_net::ISteamNetworkingSockets012Abi* sockets, bool* fault);

struct Api {
    steam_net::ISteamNetworkingUtils004Abi* utils = nullptr;
    steam_net::ISteamNetworkingSockets012Abi* sockets = nullptr;
};
// Sets 204 and/or 205 (`bits`); all or nothing: on a false return any value
// already set is reset again. reason: set_204 | set_205 | no_interface | fault.
bool Install(const Api& api, std::uint8_t bits, const char*& reason, bool& fault) noexcept;
// One RunCallbacks call (counted).
void Pump(const Api& api, bool& fault) noexcept;
// Resets every installed value (null = the default), then drains once.
void Uninstall(const Api& api, bool& fault) noexcept;
std::uint8_t Installed() noexcept;

// ---- 1252 mailbox (newest wins): the guest's "why did the join fail".
inline constexpr std::size_t kDebugChars = 64;
struct SessionFailed {
    std::uint64_t peer;          // SteamID64 (0 when the identity is not a SteamID)
    std::int32_t state;          // info+0xB0
    std::int32_t end_reason;     // info+0xB4 (ESteamNetConnectionEnd)
    char debug[kDebugChars + 1]; // info+0xB8, printable ASCII only ('?' otherwise)
    std::uint32_t sequence;      // 1, 2, ... per stored failure
};
bool TakeSessionFailed(SessionFailed& out) noexcept;

// ---- 1221 record (self-test stage F): the first kStatusEntries calls.
inline constexpr unsigned kStatusEntries = 4;
struct StatusEntry {
    std::uint32_t conn;          // hConn
    std::int32_t new_state;      // info+0xB0
    std::int32_t old_state;      // +0x2C0
    std::uint32_t thread;        // the calling thread
};
struct StatusRecord {
    std::uint32_t calls;         // ConnectionStatusFn calls since the last reset
    StatusEntry entries[kStatusEntries];
};
StatusRecord GetStatusRecord() noexcept;
void ResetStatusRecord() noexcept;

struct Stats {
    std::uint32_t installed;     // bits now set
    std::uint32_t installs, uninstalls, install_failed;
    std::uint64_t pumps;
    std::uint32_t dispatched;    // calls of our functions (any id)
    std::uint32_t req_fn;        // 1251 with a valid identity
    std::uint32_t failed_fn;     // 1252
    std::uint32_t status_fn;     // 1221
    std::uint32_t bad_payload;   // 1251 identity not an individual SteamID
    std::uint32_t faults;        // __except inside one of our functions
};
Stats GetStats() noexcept;
// Tests only: counters, mailbox and record.
void ResetForTest() noexcept;

}  // namespace mgscoop::steam_dispatch
