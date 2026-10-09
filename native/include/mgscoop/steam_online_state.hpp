#pragma once
// Shared state of the Steam online path (docs/steam-online.md 10.2, 11, 14)
// between the loader's owner context (steam_online), the F9 overlay
// (debug_menu_ui) and the worker state.json (worker_debug). Copied data only:
// the UI never calls Steam; it posts requests that the owner pass consumes.
// Seqlock/SRW-protected constant-initialized POD; nothing allocates.
#include <cstddef>
#include <cstdint>

namespace mgscoop::steam_state {

// ---- Self-test report (lab only; worker state.json "steam" and steam_status).
struct PairResult {
    bool ran;
    std::uint32_t hs_ms, frames, frames_rx, rel, rel_rx, rtt_ms;
    std::uint64_t release, received;
    std::uint32_t bad_identity;
};
// Stage F/D (docs/steam-online.md 20.6): the function-pointer dispatch.
struct FnResult {
    bool ran;                 // stage F started
    bool skipped;             // steam_selftest_fn=0
    char gate[24];            // the dispatch gate ("" = ok, else its reason)
    bool set204, reset204;    // F1: SetConfigValue(204, Global, Ptr) set and reset
    std::uint32_t pre_pump;   // F2: our 1221 calls before the first RunCallbacks (must be 0)
    std::uint32_t status_cb;  // F2: ConnectionStatusFn calls in total
    bool conn_ok;             // F2: a call for the surviving end (hConn a)
    std::int32_t old_state, new_state;  // that call's old / new state (3 -> 4|5)
    bool thread_ok;           // that call ran on the self-test thread (inside our RunCallbacks)
    std::uint32_t extra;      // calls after the first match (closing a, pumped once)
    bool d_install, d_uninstall;  // D: the play path Install(204+205) / Uninstall
    std::uint32_t d_pumps, d_dispatched, faults;
    char reason[24];          // "" or why stage F/D failed
};
struct SelftestReport {
    bool configured;          // steam_selftest set in the lab
    char verdict[12];         // "" (running/not started) | pass | fail | refused
    char stage[8];            // wait | G | P | L | F | M | T | done
    char reason[48];
    bool api, client, logged_on, messages, sockets, utils, rtti, shape;
    std::int32_t relay;       // GetRelayNetworkStatus(nullptr); -1 unread
    PairResult pair;          // stage P (in-process pair)
    PairResult lpair;         // stage L (network loopback; steam_selftest=2 only)
    std::int32_t inert_recv, inert_state;
    bool inert_accept;
    std::int64_t getter_delta;
    FnResult fn;              // stage F/D
};
// "ok" (stage F/D passed), "fail", "skip" (steam_selftest_fn=0) or "none" (not reached).
const char* FnVerdict(const FnResult& fn) noexcept;
void PublishReport(const SelftestReport& report) noexcept;
SelftestReport GetReport() noexcept;
// The steam_status reply (<= 160 characters):
// "steam_selftest;verdict=pass;reason=;stage=done;pair=500/500;rel=64/64;release=564/564;relay=100;fn=ok"
// Before a verdict: "steam_selftest;verdict=pending;stage=<s>". Unconfigured:
// "steam_selftest;verdict=off".
bool FormatStatusLine(const SelftestReport& report, char* out, std::size_t capacity) noexcept;

// ---- F9 Partner / Co-op -> Online (play mode with net_transport=steam).
inline constexpr unsigned kMaxFriends = 64;
struct Friend {
    std::uint64_t id;
    char name[48];        // printable ASCII ('?' for anything else)
    bool in_game;         // playing app 287700
    bool invited;         // invited within the last 10 s
};
struct OnlineSnapshot {
    bool available;       // play + steam transport configured
    bool host;
    bool accepting;
    bool waiting;         // guest without a host (S2)
    bool code_ready;      // host: a join code can be copied
    char steam[24];       // "ok", "acquiring" or the steam=disabled reason
    char state[24];       // link state name
    char route[12];       // relay | direct | unknown | none
    char reason[32];      // the link reject / disabled reason, if any
    std::uint32_t rtt_ms;
    char peer[24];        // masked "steam:...1234" or ""
    char peer_name[48];
    char last_result[96]; // last invite / join result
    bool friends_loading;
    std::uint32_t friend_count;
    Friend friends[kMaxFriends];
    std::uint32_t generation;
};
void PublishOnline(const OnlineSnapshot& snapshot) noexcept;
OnlineSnapshot GetOnline() noexcept;
// Owner/transport side: updates only the link fields of the published snapshot.
void PublishLink(const char* state, const char* route, const char* reason, std::uint32_t rtt_ms, const char* peer, bool waiting,
                 bool accepting) noexcept;

// UI -> owner requests (one slot each; newest wins).
void RequestAccepting(bool accepting) noexcept;
bool TakeAccepting(bool& accepting) noexcept;
// The Invite page heartbeat (UI frame time, GetTickCount64 ms).
void NoteInvitePage(std::uint64_t now_ms) noexcept;
std::uint64_t InvitePageMs() noexcept;
bool RequestInvite(std::uint64_t id) noexcept;
bool TakeInvite(std::uint64_t& id) noexcept;
// A pasted code (secret-bearing: copied out once and cleared).
inline constexpr std::size_t kPasteCapacity = 96;
bool RequestJoin(const char* text) noexcept;
bool TakeJoin(char (&out)[kPasteCapacity]) noexcept;

// Host: builds the MGS2 code (registered by steam_online; secret-bearing,
// the caller clears its buffer). False when no code is available.
using CodeProvider = bool (*)(char* out, std::size_t capacity) noexcept;
void SetCodeProvider(CodeProvider provider) noexcept;
bool BuildCode(char* out, std::size_t capacity) noexcept;

// Tests only.
void ResetForTest() noexcept;

}  // namespace mgscoop::steam_state
