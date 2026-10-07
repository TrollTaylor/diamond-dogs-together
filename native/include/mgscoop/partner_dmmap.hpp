#pragma once
// dm=map: the direct-motion clip of instance 1 follows the remote player's
// stance and speed (docs/partner-anim-appearance.md "D-map"). Pure, portable
// and deterministic: every observation and time is passed in, nothing
// allocates, throws or touches the game. partner_driver feeds one In per Lua
// tick and turns the returned Out into DM_PLAY (128) / DM_STOP (256) op bits.
//
// Why: the direct-motion lever (dm=on, live D series) plays any resident gani
// on instance 1, but one clip at a time: a looping clip stays active, a second
// play is refused while one runs, so a clip change is stop, wait until
// q18[1] leaves the DM act, then play. This module
//   - classifies the remote into [stance][move] (PlayerStatus flags, then the
//     horizontal speed of the pose, then the anim act/speed on the net source)
//     and resolves the gani through the 3x4 table;
//   - holds a new candidate kHoldMs and a playing clip kDwellMs before a switch;
//   - sends nothing while the host is in DM, a camera connect is set or any
//     request is pending (DM End clears g+0x388 0x3C globally), and never a
//     play in the same reply as SET_PARTS, an executed COPY, SET_ALLACTIVE or
//     WARP (a look reload or a Warp would end the clip);
//   - reconciles against q18[1] every tick (confirm, lost with a cause, stop
//     confirmed, timeouts with a doubling backoff, failed after kMaxFailures);
//   - strikes a gani that is never consumed or ends on its own within
//     kLoopMinMs; two strikes make it bad for the mission (until Rearm), and
//     the stance's idle cell is played instead, so a clip that is not a loop
//     never replays forever;
//   - releases its clip (one stop, wait, timeout) when map is turned off.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::partner_dmmap {

inline constexpr std::uint8_t kNoGani = 0xFF;          // nothing / unknown (adopted) / no usable clip
inline constexpr std::uint8_t kMoveUnknown = 0xFF;
inline constexpr std::uint64_t kHoldMs = 200;          // candidate stable before a switch
inline constexpr std::uint64_t kDwellMs = 300;         // current clip played before a switch
inline constexpr std::uint64_t kMeasuredHoldMs = 70;   // fresh validated pose speed, including idle
inline constexpr std::uint64_t kMeasuredDwellMs = 100;
inline constexpr std::uint64_t kConfirmMs = 2500;      // play sent -> q18[1]==0x8b (the native watch is 2000)
inline constexpr std::uint64_t kStopWaitMs = 1000;     // stop sent -> q18[1] left the DM act
inline constexpr std::uint64_t kReleaseMs = 1500;      // leaving map: give up waiting
inline constexpr std::uint64_t kLoopMinMs = 3000;      // a clip ending on its own sooner is a strike
inline constexpr std::uint32_t kBackoffMs = 1000;      // doubled per failure ...
inline constexpr std::uint32_t kBackoffMaxMs = 8000;   // ... up to this
inline constexpr unsigned kMaxFailures = 5;            // consecutive -> failed
inline constexpr unsigned kStrikes = 2;                // per gani -> bad for the mission (until Rearm)
inline constexpr std::uint32_t kSpeedHystMmps = 150;
inline constexpr std::uint64_t kSpeedSpanMinMs = 100;
inline constexpr std::uint64_t kSpeedSpanMaxMs = 200;
inline constexpr std::uint64_t kSpeedSampleMs = 10;    // history spacing; newest observation is always retained
inline constexpr std::uint32_t kSpeedJumpMmps = 20000;
inline constexpr std::size_t kSpeedRing = 32;
static_assert(kPartnerDmGaniMax < 32, "the bad mask is 32 bits");

// How the class was decided (move: flags, speed, anim, none; lost; stale).
enum class Via : std::uint8_t { none, flags, speed, anim, lost, stale };
const char* ViaName(Via via) noexcept;
struct Observation {
    bool fresh;               // net: valid && live; orbit/fixed: true
    bool lost;                // net: !valid or status lost
    std::uint8_t stance;      // PlayerStatus stance 0..2
    bool sprint;              // wire Sprinting / local tick.move == 3
    std::uint8_t flag_move;   // fixed: tick.move (0..3), else kMoveUnknown
    bool anim_valid;          // stance fallback (all sources)
    bool anim_move;           // act/speed may decide move (net only)
    std::uint16_t act;
    std::uint8_t anim_stance, anim_speed;
    bool speed_ok;
    std::uint32_t speed_mmps;
    float yaw;
    std::uint8_t gun_actions;
};
struct Class { std::uint8_t stance, move; Via via, stance_via; };
// The move band of a horizontal speed with kSpeedHystMmps hysteresis around
// prev_move (kMoveUnknown: no hysteresis): up needs threshold + H, down
// threshold - H, the move holds inside the dead band.
std::uint8_t MoveForSpeed(std::uint32_t speed_mmps, const PartnerDmSpeeds& speeds, std::uint8_t prev_move) noexcept;
// Stance: the flags, or the anim stance (squat/crawl) when the flags say
// stand. Move, first match: sprint -> 3; fixed flag_move 0..2; speed; anim
// (net: odd idle acts 0, moving acts 2 at anim speed 3, else 1); else 0.
// A lost observation keeps prev_stance with move 0 (via lost).
Class Classify(const Observation& o, const PartnerDmSpeeds& speeds, std::uint8_t prev_move, std::uint8_t prev_stance) noexcept;
inline std::uint8_t Cell(const Class& c) noexcept { return static_cast<std::uint8_t>(c.stance * 4u + c.move); }
// table[Cell]; a bad gani falls back to the stance's idle cell, then kNoGani.
std::uint8_t ResolveTarget(const PartnerDmTable& table, const Class& c, std::uint32_t bad) noexcept;
// Verified resident transition clips 13..20 play once, unlike locomotion loops.
bool OneShot(std::uint8_t gani) noexcept;
inline constexpr std::uint64_t kTurnSettleMs = 70;
inline constexpr std::uint64_t kTurnCollectMs = 600;
struct TurnState {
    bool valid{}, collecting{};
    float yaw{}, activity_yaw{}, accumulated{};
    std::uint64_t began_ms{}, changed_ms{}, sample_ms{}, until_ms{};
    std::uint8_t gani{kNoGani}, stance{};
    std::uint32_t event_token{};  // changes only when a collected pivot is committed
};
// Only idle stand/squat turns; movement/loss interrupts and teleports reset.
// Collect the full pivot until its yaw settles, with a bounded collection window.
std::uint8_t ObserveTurn(TurnState& state, std::uint64_t now_ms, bool fresh, float yaw, std::uint8_t stance, bool moving) noexcept;

// Horizontal speed of the commanded pose: newest against the oldest entry
// within kSpeedSpanMaxMs (at least kSpeedSpanMinMs apart). Time going
// backwards or a step faster than kSpeedJumpMmps (a teleport) restarts it.
struct SpeedRing {
    struct Entry { std::uint64_t t_ms; std::int32_t x_mm, z_mm; };
    Entry entries[kSpeedRing];
    std::uint64_t anchor_ms;  // newest slot's creation time; overwrites do not move the history cadence
    std::uint8_t head, count;
};
void SpeedReset(SpeedRing& ring) noexcept;
bool SpeedPush(SpeedRing& ring, std::uint64_t t_ms, std::int32_t x_mm, std::int32_t z_mm) noexcept;
bool SpeedMmps(const SpeedRing& ring, std::uint32_t& out) noexcept;

enum class Phase : std::uint8_t { idle, wait, play_sent, playing, stop_sent, backoff, failed, release };
const char* PhaseName(Phase phase) noexcept;
enum class Block : std::uint8_t { none, terminating, fault, stage, apply, anim, input };
const char* BlockName(Block block) noexcept;
enum class Cause : std::uint8_t { none, ended, warp, look, parts, host };
const char* CauseName(Cause cause) noexcept;
enum class Action : std::uint8_t { none, play, stop };
enum class Event : std::uint8_t { none, step, playing, deferred, stop_timeout, unconsumed, dm_read, lost, finished, failed, adopted, released };
struct In {
    bool map;                  // settings.dm == map
    Block blocked;
    bool target_ok;            // false: freeze (stale remote)
    std::uint8_t target;       // ResolveTarget(...)
    std::uint32_t finite_token; // nonzero discrete request identity; held copies are not new events
    bool responsive;           // authoritative movement/action: one frame hold
    bool measured;             // fresh net pose speed: shorter start/stop hold
    bool urgent;               // a move/action can interrupt a one-shot
    Class cls;
    bool dm_ok, patched, parts_loaded;
    bool in_dm;                // q18[1] == 0x8b
    bool host_dm;              // q18[0] == 0x8b
    bool pending;              // (g388 & 0x3C) != 0
    bool our_pending;          // Mission::dm_play_pending
    bool cam_idle;             // cam_ok && cam1f0 == 0 && cam200 == 0
    bool look_busy;            // SET_PARTS, an executed COPY, or a look apply still verifying
    bool allactive;            // this reply has SET_ALLACTIVE (2)
    bool warp;                 // this reply has WARP (1)
    bool prev_look, prev_warp; // the previous reply had a look op / WARP (loss attribution)
    std::uint64_t now_ms;
    std::uint8_t available_ganis = kPartnerDmGaniMax + 1;
};
struct Out {
    Action action;
    std::uint8_t gani;         // play: gani; step: "to"; playing/lost/unconsumed/stop_timeout: the clip
    Event event;
    std::uint8_t from;         // step: the clip stopped before (or kNoGani)
    bool step_stop;            // step: a stop (else a play)
    const char* reason;        // deferred reason / failure kind / released result
    std::uint32_t ms;          // step: play wait after the stop, stop candidate held; playing: after the play;
                               // lost: played; failures: since the request; released: since the release began
    Cause cause;               // lost
    std::uint8_t bad_gani;     // kNoGani, or the gani that just became bad (logged in addition to event)
};
struct State {
    Phase phase;
    Phase release_from;        // the phase the current release began in
    bool owned;
    std::uint8_t cur, sent, from, cand;
    std::uint8_t completed_once;
    std::uint64_t cur_event, sent_event, cand_event;
    Class cand_cls;
    std::uint64_t cand_ms, sent_ms, play_ms, stop_ms, retry_ms, release_ms;
    bool stop_done, release_stop_sent;
    std::uint32_t backoff_ms;
    unsigned failures;
    const char* deferred;      // static strings; compared with strcmp
    const char* last_failure;
    std::uint32_t bad;         // bit g: gani g unusable
    std::uint8_t strikes[kPartnerDmGaniMax + 1];
    std::uint32_t switches, plays, stops, deferrals, timeouts, unconsumed, lost, adopted, bad_ganis;
    struct FiniteEvent { std::uint64_t id; std::uint8_t gani; Class cls; std::uint64_t requested_ms; };
    static constexpr std::size_t finite_capacity = 4;  // includes the playing request, with at most three waiting
    FiniteEvent finite_events[finite_capacity];
    std::uint8_t finite_count;
    std::uint32_t observed_finite_token;
    std::uint8_t observed_finite_gani;
    std::uint64_t next_finite_id;
    std::uint32_t finite_dropped;
};
// Drop queued finite requests at explicit source/teleport lifecycle resets.
// Current DM ownership and native stop gates remain the caller's responsibility.
void CancelFiniteEvents(State& state) noexcept;
void Reset(State& s) noexcept;   // all zero, phase idle, cur/sent/from/cand = kNoGani
void Rearm(State& s) noexcept;   // failures/backoff 0, deferred null, bad/strikes cleared; failed -> (owned ? playing, play_ms 0 : wait); backoff -> retry_ms 0
void Forget(State& s) noexcept;  // owned false, cur kNoGani, stop_done/release_stop_sent false; idle, failed and backoff kept, else wait
void Idle(State& s) noexcept;    // Forget + phase idle, candidate and deferral cleared (statistics kept)
// One tick: reconcile (R), leave (L, map off), blocked (B) or decide (D). At
// most one Action and one Event per call.
Out Step(State& s, const In& in) noexcept;

} // namespace mgscoop::partner_dmmap
