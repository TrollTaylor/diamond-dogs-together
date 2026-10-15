#pragma once
// DirectMotion overlay lever for graph-0 action states (docs/anim-round1-actions.md
// "DM overlay"; critic P2.10, action-anims 7.2). Pure, portable and
// deterministic (mgs_coop_core): every observation and time is passed in;
// nothing allocates, throws or touches the game. partner_driver feeds one In
// per tick and executes the Out.
//
// Default OFF (lab.ini partner_native_overlay=off|debug|map). On request (the
// debug command overlay_state / overlay_stop, or with map the remote-act map)
// the overlay
//   1. suspends the native drive: the arbiter's suspend reason "overlay"
//      (partner_native In::suspend_reason) leaves without counting a fallback,
//      without the latch and without the re-entry hold; the driver disarms the
//      input window and the deferral; body 1 stays realized and is placed by
//      the fallback xform (apply=xform: the lever refuses any other apply);
//   2. owns the DM: TickDm and dm=map stay out (a mapped clip is released first);
//   3. applies the comparator patch (partner_dm_patch 0x1198E25);
//   4. requests the entry's named graph-0 state (or resident clip path) through
//      the existing DM reply: DM_PLAY with the reply value ReplyCode(index,
//      loop) = 64 | loop << 5 | index, which lua/coop/partner_sync.lua maps to
//      OVERLAY[index] (the same order as the catalogue below). It plays only
//      when no DM is active on either body (q18 != 139), no request is pending
//      (g+0x388 & 0x3C), this reply carries no Warp/look op, and the local
//      camera connect is idle (CAM+0x1F0/+0x200 zero: DM entry and End write
//      both to 0 for any id) unless a camera-skip module is armed (In::cam_skip:
//      the Hooks::dm_camera_skip_armed hook point; nothing here arms one);
//   5. confirms q18[1] == 139 within confirm_ms, then plays for the debug
//      request's ms, or while the remote's act stays mapped (map);
//   6. on stop, finish or timeout: DM_STOP (never while the local body is in DM:
//      Stop is global), waits until q18[1] != 139 (one retry), releases, and
//      the native drive re-enters at once.
// Every catalogue state's StrCode64 was checked against the retail
// TppPlayer2_layers.mog (present once in the shared node pool) and against the
// SQ-4 graph 2/3 node list (absent: a graph-0 state the named-state branch
// 0x1199140 -> controller vt+0x170 can request); the clip paths are PathCode64
// entries of player2_resident.mtar (motion-levers verified-clips.tsv).
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::partner_dm_overlay {

struct Entry {
    const char* name;  // the debug token (overlay_state <name>)
    const char* lua;   // partner_sync.lua OVERLAY[index], the DM entry's first field
    std::uint64_t code;  // StrCode64 (48 bits) of a state; PathCode64 of a clip path
    bool path;         // a clip path (plays in stateDirectPlay) rather than a named state
    bool loop;         // the default loop flag
};
inline constexpr std::uint8_t kNone = 0xFF;
inline constexpr std::uint8_t kReplyBase = 64;  // reply values 64..127: overlay entries
inline constexpr std::uint8_t kMaxEntries = 32;
std::uint8_t Count() noexcept;
// nullptr outside the catalogue.
const Entry* At(std::uint8_t index) noexcept;
// The entry named `name` (exact, case-sensitive), or kNone.
std::uint8_t Find(const char* name, std::size_t size) noexcept;
std::uint8_t ReplyCode(std::uint8_t index, bool loop) noexcept;
bool DecodeReply(std::uint8_t code, std::uint8_t& index, bool& loop) noexcept;
// The DM probe's rec+0x18 for a playing entry: its StrCode64 for a named state,
// stateDirectPlay (0x81582ffa24b9) for a clip path.
std::uint64_t ExpectedRec18(std::uint8_t index) noexcept;

// The remote-act map (mode map; critic 6 / P2.10): damage 29/75/76 ->
// stateDamageBigFront (the hit direction is not on the wire), death 40/41 ->
// stateDead, box 77/78 -> stateCBoxIdle/stateCBoxMove, ledge hang 54..56 ->
// stateEludeIdle, ladder 21/23 -> stateClimbIdle / MoveFront / MoveBack by the
// remote's vertical speed (+-kClimbMmps). kNone for every other act.
inline constexpr std::int32_t kClimbMmps = 250;
std::uint8_t MapAct(std::uint16_t act, std::int32_t vy_mmps) noexcept;

enum class Phase : std::uint8_t { idle, suspend, patch, play_sent, playing, stop_sent };
const char* PhaseName(Phase phase) noexcept;
enum class Source : std::uint8_t { none, debug, map };
const char* SourceName(Source source) noexcept;

struct Params {
    std::uint32_t suspend_ms = 3000;   // native clear + dm=map released, at most
    std::uint32_t confirm_ms = 2500;   // play sent -> q18[1] == 139
    std::uint32_t stop_wait_ms = 1000; // stop sent -> q18[1] != 139 (then one retry)
    std::uint32_t map_hold_ms = 150;   // a mapped remote act held this long starts
    std::uint32_t map_exit_ms = 250;   // unmapped this long stops
    std::uint32_t map_dwell_ms = 500;  // playing at least this long before a switch
    std::uint32_t map_max_ms = 60000;  // a looping map overlay at most this long
    std::uint32_t once_max_ms = 5000;  // a one-shot map overlay at most this long
    std::uint32_t cooldown_ms = 500;   // after a release, before the next map start
    std::uint32_t debug_ms = 3000;     // a debug request without ms
    std::uint32_t max_ms = 60000;      // a debug request's ms bound
    std::uint32_t replays = 1;         // a loop the game ended is replayed this often
};

// A debug command (applied on the next tick).
struct Request {
    bool start, stop;
    std::uint8_t index;
    bool loop;
    std::uint32_t ms;  // 0: Params::debug_ms
};

struct In {
    std::uint64_t now_ms;
    PartnerNativeOverlay mode;
    Request debug;
    // The driver's eligibility: nullptr, or a static reason (terminating,
    // stage, parts, range, ride, carry, vehicle, pose_lease, fault,
    // patch_failed, dm_revert_failed, apply, body_state).
    const char* blocked;
    // Body 1 is still there (loaded, not ride/range hidden, not terminating):
    // without it a blocked overlay gives up without a (global) DM_STOP.
    bool body_present;
    bool native_clear;  // the native drive is not active, its window and deferral disarmed
    bool map_idle;      // dm=map owns no clip and is not releasing one
    bool patched;       // the comparator patch is applied
    // The DM read (partner_player::DmState).
    bool dm_ok;
    std::uint16_t q18[2];
    std::uint8_t g388;
    std::uint64_t rec18;
    bool cam_ok;
    std::uint64_t cam1f0;
    std::uint32_t cam200;
    bool cam_skip;    // a camera-skip module is armed (never set by this module)
    bool reply_busy;  // this reply carries a Warp, SET_PARTS, SET_ALLACTIVE or an executed COPY
    // The remote (map): a live sample with its anim extension; y for the ladder.
    bool remote_valid;
    std::uint16_t remote_act;
    std::int32_t remote_y_mm;
    bool body_valid;
    std::uint16_t body_act;
};

struct Out {
    bool suspend;      // ask the arbiter to suspend the native drive (next tick)
    bool owns_dm;      // TickDm and dm=map stay out this tick
    bool release_map;  // ... except to release dm=map's clip (TickDm with dm=off)
    bool patch;        // apply the comparator patch
    bool play;         // DM_PLAY with reply_code
    std::uint8_t reply_code;
    bool stop;         // DM_STOP
    bool clear_pending;  // clear our unconsumed request
    // One telemetry event per step (static): request, refused, patch, deferred,
    // play, playing, conflict, stop, stop_retry, stop_timeout, finished, lost,
    // replay, unconsumed, released, abort; nullptr: none.
    const char* event;
    const char* reason;  // static detail of the event
    std::uint8_t index;
    bool loop;
    Source source;
    std::uint32_t ms;    // event-dependent: after_ms / played_ms / waited
    bool code_match;     // playing: rec+0x18 is the expected code
};

struct State {
    Phase phase;
    Source source;
    std::uint8_t index;
    bool loop;
    std::uint64_t since_ms, start_ms, play_ms, playing_ms, deadline_ms, stop_ms;
    std::uint8_t stop_tries, replays;
    const char* stop_reason;  // a stop is wanted (static reason)
    const char* deferred;     // the deferral reason last reported (change-only)
    bool conflict_logged;
    // Map: the candidate entry and since when; since when unmapped; the remote
    // act the overlay started for; a finished one-shot entry is not replayed
    // while the remote stays in its act (done_index); a pending switch.
    std::uint8_t map_cand;
    std::uint64_t map_cand_ms, unmapped_ms;
    std::uint8_t done_index;
    std::uint8_t next_index;
    bool switching;
    std::uint64_t cooldown_until;
    // The remote's vertical speed (EMA, mm/s) for the ladder entries.
    bool have_y;
    std::int32_t prev_y;
    std::uint64_t prev_y_ms;
    double vy;
    // Counters since Reset.
    std::uint32_t requests, refusals, plays, confirms, stops, finishes, losts, unconsumed, aborts;
};

void Reset(State& state) noexcept;
Out Step(State& state, const In& in, const Params& params = Params{}) noexcept;
// Whether the overlay suspends the native drive (any phase but idle).
inline bool Active(const State& state) noexcept { return state.phase != Phase::idle; }

} // namespace mgscoop::partner_dm_overlay
