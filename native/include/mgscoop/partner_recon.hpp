#pragma once
// Round 4: reconciliation of instance 1's body while the input window drives
// its locomotion (apply=input; docs/partner-anim-appearance.md "Round 4").
// Pure, portable and deterministic: every observation and time is passed in,
// nothing allocates, throws or touches the game. partner_driver feeds one In
// per Lua tick and executes the returned Out through partner_player.
//
// Why: live round 3 snapped the body with an xform every ~1 s (err ~3.1 m)
// because the old correction compared T+0x20 with the remote pose 100 ms in
// the past and teleported after 1.5 m held 500 ms, while the body ran on its
// own (a stuck moving state). This module
//   - places the body once (a Lua Warp with the window disarmed) at the
//     time-aligned remote pose and verifies the placement before arming;
//   - corrects drift with small position-only nudges at <= 15 Hz (T.vt+0x58:
//     position only, the body's own rotation and locomotion untouched);
//   - snaps with an xform only on a large sustained error, warps (a new
//     placement) only on a teleport, relink, far or vertical error;
//   - pins the body to the remote pose (the proven smooth mover) when it keeps
//     moving while the remote stands still, or when snaps repeat (diverging);
//   - freezes all corrections while the pose or the input is stale, during
//     stance changes and non-locomotion acts;
//   - aligns the facing of an idle body with the idle remote's (legacy rule),
//     or, with the native facing governor (FaceMode idle|aim), turns a quiet
//     body toward the remote's yaw at a bounded eased rate, snaps it once
//     after a discontinuity and backs off when the game contests a write.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_facing.hpp"

namespace mgscoop::partner_recon {

enum class Action : std::uint8_t {
    none,
    place,  // disarm the window this tick and Lua-Warp to pos/yaw (Warp's resets included)
    nudge,  // position-only set at pos (T.vt+0x58), rotation untouched; y = the body's own
    snap,   // xform at pos/yaw (T.vt+0x70 + T.vt+0x68, no resets)
    pin,    // xform at pos/yaw (the smooth mover, every tick)
    yaw,    // xform at the body position with yaw (legacy idle rule)
    face,   // xform at pos/yaw: a facing governor step or snap, or a placement's yaw fix (Out::face_epoch)
};
const char* ActionName(Action action) noexcept;

enum class Mode : std::uint8_t { idle, place, verify, track, pin, frozen, fallback };
const char* ModeName(Mode mode) noexcept;

enum class PlaceReason : std::uint8_t { none, first, rearm, relink, teleport, far_error, vertical, diverging, manual, retry };
const char* PlaceReasonName(PlaceReason reason) noexcept;

enum class Frozen : std::uint8_t { none, pose_stale, input_stale, act, remote_act, stance, settle, body };
const char* FrozenName(Frozen reason) noexcept;

// Facing governor (docs/native-puppet.md "Facing"). legacy: the round-4 idle
// rule (Action::yaw: acts 1..6, > 25 degrees for 800 ms, 2 s gap). off: no
// facing correction. idle: turns of a quiet idle body and one settle snap after
// a discontinuity. aim: idle plus quiet aim turns (body and remote in an aim
// idle act).
enum class FaceMode : std::uint8_t { legacy, off, idle, aim };
const char* FaceModeName(FaceMode mode) noexcept;
// What the governor is doing (Report): off (nothing to do or not allowed),
// idle / aim (a turn in progress), settle (the snap window after a
// placement, snap or teleport, or the body in fall/land acts 25/26),
// contested (latched off after repeated contests).
enum class FaceState : std::uint8_t { off, idle, settle, aim, contested };
const char* FaceStateName(FaceState state) noexcept;

// Event bits of one step (the driver logs one line per bit).
namespace Event {
inline constexpr std::uint32_t place = 1u << 0;
inline constexpr std::uint32_t placed_ok = 1u << 1;
inline constexpr std::uint32_t placed_fail = 1u << 2;
inline constexpr std::uint32_t snap = 1u << 3;
inline constexpr std::uint32_t runaway = 1u << 4;
inline constexpr std::uint32_t runaway_end = 1u << 5;
inline constexpr std::uint32_t frozen = 1u << 6;      // the frozen reason changed to non-none
inline constexpr std::uint32_t unfrozen = 1u << 7;
inline constexpr std::uint32_t stance = 1u << 8;
inline constexpr std::uint32_t stance_mismatch = 1u << 9;
inline constexpr std::uint32_t yaw = 1u << 10;
inline constexpr std::uint32_t stop_check = 1u << 11;
inline constexpr std::uint32_t diverging = 1u << 12;
inline constexpr std::uint32_t pin_end = 1u << 13;
inline constexpr std::uint32_t stance_fix = 1u << 14;     // Out::stance_fix set this step
inline constexpr std::uint32_t input_pin = 1u << 15;      // net input stale with a live pose: pinned
inline constexpr std::uint32_t input_pin_end = 1u << 16;  // input live again: tracking resumes (settle)
inline constexpr std::uint32_t lag = 1u << 17;            // the body falls behind the moving remote (no pin; along nudges)
inline constexpr std::uint32_t lag_end = 1u << 18;
inline constexpr std::uint32_t face_start = 1u << 19;     // a governor turn started (Report face_state idle|aim)
inline constexpr std::uint32_t face_end = 1u << 20;       // a turn ended (on target, or aborted: Report face_reason)
inline constexpr std::uint32_t face_snap = 1u << 21;      // the settle snap
inline constexpr std::uint32_t face_contest = 1u << 22;   // the game rotated the body after our write (backoff)
inline constexpr std::uint32_t face_latched = 1u << 23;   // repeated contests: the governor is off for face_latch_ms
inline constexpr std::uint32_t place_yaw_fix = 1u << 24;  // verify: the placed body's yaw was off, one xform at the placed yaw
}

// A corrective stance input the driver injects through the tap/press overlay.
enum class StanceFix : std::uint8_t { none, tap, hold };
const char* StanceFixName(StanceFix fix) noexcept;

struct Params {
    PartnerRecon recon = PartnerRecon::track;
    PartnerRunaway runaway = PartnerRunaway::pin;
    // Hard snaps (xform) and placements (Warp).
    std::uint32_t snap_moving_mm = 3000, snap_moving_hold_ms = 300;
    std::uint32_t snap_idle_mm = 2000, snap_idle_hold_ms = 1000;
    std::uint32_t warp_mm = 10000;
    std::uint32_t vertical_mm = 2000, vertical_hold_ms = 500;
    std::uint32_t teleport_jump_mm = 5000, body_jump_mm = 3000;
    // Per-tick motion faster than this is a discontinuity, never fed into the
    // velocity estimates (a sprint is about 6 m/s).
    std::uint32_t max_speed_mmps = 20000;
    std::uint32_t hard_gap_ms = 1500;
    std::uint32_t diverge_snaps = 3, diverge_window_ms = 10000;
    std::uint32_t diverge_pin_ms = 2000, diverge_pin_max_ms = 30000, diverge_reset_ms = 60000;
    // Nudges (position only).
    std::uint32_t dead_moving_mm = 300, dead_idle_mm = 200, dead_prone_mm = 400;
    std::uint32_t nudge_period_ms = 66;
    std::uint32_t tau_moving_ms = 600, tau_idle_ms = 800;
    std::uint32_t cap_moving_mmps = 600, cap_idle_mmps = 300, cap_prone_mmps = 150;
    // Filters and classifications.
    std::uint32_t ema_ms = 150;
    std::uint32_t idle_mmps = 150, idle_hold_ms = 300, moving_mmps = 500;
    double idle_r = 0.1, moving_r = 0.2;
    // Runaway: the body moves on its own. Idle remote: |v_free| > runaway_mmps;
    // moving remote: |v_free - v_remote| > runaway_mmps while the filtered
    // error is >= runaway_moving_mm and growing (>= runaway_grow_mm in 300 ms),
    // both held runaway_hold_ms. Ends when |v_free - v_remote| stays below the
    // exit speed (idle / moving remote) for runaway_exit_ms.
    std::uint32_t runaway_mmps = 800, runaway_hold_ms = 300, runaway_exit_mmps = 200, runaway_exit_ms = 500;
    std::uint32_t runaway_moving_mm = 1000, runaway_grow_mm = 50, runaway_exit_moving_mmps = 400;
    // Native actions (0 = off, the round-4 rule above). runaway_split: the
    // moving runaway compares the body's own velocity with the remote's along
    // and across the remote's direction of motion and fires only on an overrun
    // (dv_along > thr) or a sideways drift (|dv_cross| > thr), thr =
    // max(runaway_mmps, runaway_rel_pct % of |v_remote|); a body that is only
    // slower (dv_along < -thr, error growing) raises `lag` instead, which never
    // pins (the along-track nudges pull it). Nudges along the remote's motion
    // are capped by clamp(cap_along_pct % of |v_remote|, cap_along_min_mmps,
    // cap_along_max_mmps) (prone: the _prone_ bounds) instead of the moving cap.
    bool runaway_split = false;
    std::uint32_t runaway_rel_pct = 20;
    std::uint32_t cap_along_pct = 0, cap_along_min_mmps = 300, cap_along_max_mmps = 1500;
    std::uint32_t cap_along_prone_min_mmps = 150, cap_along_prone_max_mmps = 400;
    // Stop grace: after the stick is released (stop check start) the idle
    // runaway is suppressed this long while the body's own speed does not
    // rise above its speed at release (a dash-stop slide decelerates).
    std::uint32_t stop_grace_ms = 0;
    // Net input stale this long while the pose is live: pin to the pose until
    // the input is live again (no far/vertical Warps while the pad is neutral).
    std::uint32_t input_pin_ms = 500;
    // Idle facing (FaceMode::legacy).
    std::uint32_t yaw_mrad = 436, yaw_hold_ms = 800, yaw_gap_ms = 2000, yaw_still_mmps = 100, yaw_still_ms = 300;
    // Facing governor (FaceMode idle|aim). It never fights locomotion: it acts
    // only while the remote stick r < face_stick_r, the remote's speed <
    // face_remote_mmps and the body's own speed < face_body_mmps have each
    // held face_quiet_ms; any stick input aborts a turn in the same tick.
    //   idle: body act 1/3/5 (the remote in the same idle act when known);
    //     enter when |err| > face_enter_cdeg held face_enter_ms and the
    //     body's own yaw rate (readback minus our writes) < face_idle_rate_dps
    //     (a pivot or turn animation the game still plays is never fought),
    //     turn at face_dps (accel face_accel_dps2) until |err| < face_exit_cdeg.
    //     A tick gap longer than partner_facing::kMaxStepMs restarts the eased
    //     turn from the body's readback (never a snap onto the target).
    //   settle: within face_settle_ms of a placement, snap or teleport, or
    //     while the body is in act 25/26: one snap to the target when |err| >
    //     face_settle_cdeg (fall/land and aim acts allowed).
    //   aim (FaceMode::aim): body act 11/12/14/15, the remote in one of them,
    //     |err| > face_aim_cdeg held face_aim_hold_ms and the body's own yaw
    //     rate (readback minus our writes) < face_aim_rate_dps: turn at
    //     face_aim_dps (accel face_aim_accel_dps2).
    //   contest: a readback more than face_contest_cdeg from our previous
    //     write (no write in between) means the game rotated the body: stop
    //     and back off face_backoff_ms; face_contests within
    //     face_contest_window_ms latch the governor off for face_latch_ms.
    FaceMode face = FaceMode::legacy;
    double face_stick_r = 0.1;
    std::uint32_t face_remote_mmps = 150, face_body_mmps = 100, face_quiet_ms = 200;
    std::uint32_t face_enter_cdeg = 500, face_exit_cdeg = 100, face_enter_ms = 200;
    std::uint32_t face_dps = 180, face_accel_dps2 = 3600, face_idle_rate_dps = 30;
    std::uint32_t face_settle_ms = 1500, face_settle_cdeg = 1000;
    std::uint32_t face_aim_cdeg = 1000, face_aim_hold_ms = 400, face_aim_rate_dps = 20, face_aim_dps = 90, face_aim_accel_dps2 = 1800;
    std::uint32_t face_contest_cdeg = 300, face_backoff_ms = 1000, face_contests = 3, face_contest_window_ms = 5000;
    std::uint32_t face_latch_ms = 10000;
    // Native placement (0/false = the round-4 behaviour). neutral_place: the
    // place, verify and retry ticks keep an enabled window with a neutral pad
    // (Out::neutral) instead of disarming it, so block 1's deferred job never
    // runs without a window (it would read the viewer's pad). place_yaw_cdeg:
    // verify also needs |yaw err| <= this; otherwise one xform at the placed
    // yaw (Action::face), then verify again. teleport_window_mm: a target
    // that moved more than this horizontally within teleport_window_ms (no
    // locomotion is that fast) is a teleport (placed at once).
    bool neutral_place = false;
    std::uint32_t place_yaw_cdeg = 0;
    std::uint32_t teleport_window_mm = 0, teleport_window_ms = 200;
    // Stance.
    std::uint32_t stance_debounce_ms = 150, stance_suspend_ms = 700, stance_suspend_prone_ms = 1500, stance_dead_ms = 1000;
    std::uint32_t stance_mismatch_ms = 1500;
    // Stance level correction (net input only): a mismatch held this long asks
    // for one tap toward the remote's stance (a hold for prone), at most every
    // stance_fix_gap_ms.
    bool stance_fix = true;
    std::uint32_t stance_fix_ms = 2000, stance_fix_gap_ms = 2000;
    // Placement.
    std::uint32_t place_ok_mm = 500, place_tries = 3, place_retry_ms = 250, place_fail_retry_ms = 5000, settle_ms = 700;
    // Input staleness (net input).
    std::uint32_t input_stale_ms = 250;
    // Catch-up after an input gap (docs/native-puppet.md "Online"): the first
    // hard snap during a net input gap of at least gap_snap_ms, or within
    // gap_snap_window_ms after it ended, is counted as a gap snap and never
    // toward diverging (the body is behind because packets were missing, not
    // because it diverges). gap_snap_ms 0 = off.
    std::uint32_t gap_snap_ms = 300, gap_snap_window_ms = 2000;
    // Stop check (diagnostic): the stick released after a push.
    double stop_from_r = 0.3, stop_to_r = 0.1;
    std::uint32_t stop_window_ms = 800, stop_mmps = 200;
};

struct In {
    std::uint64_t now_ms;
    // Nothing else refuses arming the window this tick (the driver's own
    // checks: stage, targets, hook, net, faults). false = fallback: no action,
    // and the next armable tick places the body first.
    bool armable;
    PlaceReason place_request;  // external request this tick (manual, mode change), else none
    // Body readback T+0x20 (mm) and its yaw (rad); body_ok false when unreadable.
    bool body_ok;
    double body[3];
    double body_yaw;
    // The previous Out's action was executed (its write reached the body).
    bool last_ok;
    // Target: the time-aligned remote pose (mm, rad) and its sampler state.
    bool target_ok;
    double target[3];
    double target_yaw;
    bool target_live;     // pose status live (newest snapshot <= 500 ms old)
    bool target_snapped;  // the sampler held a >5 m jump (teleport)
    bool relink;          // pose status lost -> live this tick
    // Input (net): validity, age and the stick magnitude 0..1 after rotation.
    bool input_net;
    bool in_valid;
    std::uint32_t in_age_ms;
    double in_r;
    // Slot-1 animation fields (Q+0x18[1], Q+0x30[1]) and the remote's (anim extension).
    bool body_anim_ok;
    std::uint16_t body_act;
    std::uint8_t body_stance;
    bool remote_anim_valid;
    std::uint16_t remote_act;
    std::uint8_t remote_stance;
};

struct Report {
    double err_mm, dy_mm, along_mm, cross_mm;
    double v_free_mmps, v_remote_mmps;
    // The body's own velocity minus the remote's, along and across the
    // remote's direction of motion (cross = |dv| below moving_mmps).
    double dv_along_mmps, dv_cross_mmps;
    double yaw_err_mrad;
    double d_filtered_mm;
    bool remote_idle, remote_moving, prone;
    Frozen frozen;
    std::int8_t stop_ok;  // -1 unknown, 0, 1
    // nudges/pins/yaws/nudge_mm since the last placement; snaps/places since
    // the reconciliation started (Reset), placements included.
    std::uint32_t nudges, snaps, places, pins, yaws;
    double nudge_mm;      // total nudge distance since the last placement
    std::uint32_t snaps_window;  // snaps within the diverging window
    std::uint32_t gap_snaps;     // snaps after an input gap since Reset (never in snaps_window)
    std::uint32_t place_tries;
    double place_err_mm;  // verify: |body - placed target|
    std::uint32_t place_ms;
    std::uint32_t runaway_ms;   // how long the runaway pin lasted (runaway_end)
    std::uint32_t idle_ms;      // how long the remote has been idle
    std::uint8_t stance_from, stance_to;
    std::uint32_t suspend_ms;
    std::uint32_t mismatch_ms;
    std::uint32_t stop_ms;
    PlaceReason place_reason;
    bool runaway_active;
    bool runaway_moving;        // the runaway was detected with a moving remote
    // The active runaway pin while the remote is idle (the only kind the
    // native arbiter's runaway timer counts; a moving overrun pin recovers).
    bool runaway_idle;
    bool lag;                   // runaway_split: the body is behind a moving remote
    bool stop_grace;            // the idle runaway is suppressed after a stick release
    double dv_mmps;             // |v_free - v_remote|
    bool input_pin;             // pinned because the net input is stale
    std::uint32_t input_pin_ms; // how long the input pin lasted (input_pin_end)
    std::uint8_t fix_body, fix_remote;  // stance_fix: body and remote stance
    // Facing governor: its state and why it is not acting (static), the
    // error target - body, the target and body yaw (mrad), the body's own yaw
    // rate (deg/s, readback minus our writes), and its counters since Reset.
    FaceState face_state;
    const char* face_reason;
    double face_err_mrad, face_target_mrad, face_body_mrad, face_rate_dps;
    std::uint32_t face_writes, face_snaps, face_contests, face_turns;
    // verify: the placed body's yaw error (mrad) at the verdict.
    double place_yaw_err_mrad;
};

struct Out {
    Action action;
    double pos[3];
    double yaw;
    bool arm;        // the window may be armed this tick
    Mode mode;
    PlaceReason place_reason;  // with Action::place
    StanceFix stance_fix;      // a corrective stance input (armed only)
    // neutral_place: arm is true with a neutral pad (place, verify, retry).
    bool neutral;
    // Action::face: this write is the start or end of a turn, a snap or a
    // placement's yaw fix (the driver bumps the body epoch); small steps do not.
    bool face_epoch;
    std::uint32_t events;
    Report report;
};

// The filter/timer state (reset with Reset at MissionStart; plain data).
struct State {
    Mode mode;
    bool started;          // at least one step
    bool need_place;
    PlaceReason place_reason;
    bool placed_once;      // the first placement of the mission happened
    bool armable_prev;
    std::uint64_t prev_ms;
    // Target.
    bool have_target;
    double prev_target[3];
    double v_remote[2];
    // Body.
    bool have_body;
    double prev_body[3];
    bool wrote;            // the previous action wrote last_write
    double last_write[3];
    Action last_action;
    double v_free[2];
    double e_f[2];
    bool have_ef;
    // Classifications and timers.
    std::uint64_t idle_since, still_since, runaway_since, runaway_exit_since, runaway_start;
    std::uint64_t over_since, vertical_since, yaw_since;
    std::uint64_t last_hard_ms, last_nudge_ms, last_yaw_ms;
    std::uint64_t snap_times[8];
    std::uint32_t snap_count;
    std::uint64_t pin_until, last_diverge_ms;
    std::uint32_t pin_ms_next;
    bool runaway_active;
    bool runaway_moving;
    bool runaway_latched;  // runaway=off: one event per episode
    // Lag (runaway_split) and the stop grace.
    std::uint64_t lag_since;
    bool lag;
    std::uint64_t grace_since;
    double grace_speed;
    // Net input staleness and the input pin.
    std::uint64_t input_stale_since, input_pin_start;
    bool input_pin;
    // d history for "not shrinking".
    double d_hist[32];
    std::uint64_t d_hist_ms[32];
    std::uint32_t d_head, d_count;
    // Placement.
    double placed_target[3];
    std::uint64_t place_ms, retry_at;
    std::uint32_t tries;
    std::uint64_t settle_until;
    // Stance.
    std::uint8_t stance_applied, stance_candidate;
    std::uint64_t stance_since, suspend_until, dead_until, mismatch_since;
    bool mismatch_logged;
    std::uint64_t last_stance_fix_ms;
    // Stop check.
    double prev_r;
    bool stop_pending;
    std::uint64_t stop_since;
    std::int8_t stop_ok;
    // Frozen reason last reported.
    Frozen frozen;
    // Counters: nudges/pins/yaws/nudge_mm since the last placement, snaps and
    // places since Reset (a placement never hides the snaps that caused it).
    std::uint32_t nudges, snaps, places, pins, yaws;
    double nudge_mm;
    // The yaw of the last xform-class write (place, snap, pin, yaw, face) of
    // the previous step (the body's own yaw rate excludes it).
    bool wrote_yaw;
    double last_write_yaw;
    // Facing governor.
    FaceState face_state;
    const char* face_reason;
    bool face_turning;
    FaceState face_turn_kind;            // idle or aim
    partner_facing::State face_turn;     // the eased turn (yaw = last applied)
    bool face_wrote;                     // the previous step was a governor write
    double face_last_write;
    std::uint64_t face_quiet_since, face_err_since, face_aim_since;
    std::uint64_t face_backoff_until, face_latch_until, face_settle_until;
    bool face_settle_done;
    std::uint64_t face_contest_ms[4];
    std::uint32_t face_contest_count;
    bool face_have_prev;
    double face_prev_yaw, face_own_rate;  // rad/s (EMA)
    std::uint32_t face_writes, face_snaps, face_contests, face_turns;
    // Windowed teleport: recent target positions (newest at tp_head).
    double tp_pos[16][3];
    std::uint64_t tp_ms[16];
    std::uint32_t tp_head, tp_count;
    // Placement: the placed yaw and whether verify already fixed it once.
    double placed_yaw;
    bool place_yaw_fixed;
    // Input gaps (gap snaps): a gap of at least gap_snap_ms is open or ended
    // at gap_end_ms and its snap allowance is unused.
    bool in_gap, gap_pending;
    std::uint64_t gap_end_ms;
    std::uint32_t gap_snaps;
};
void Reset(State& state) noexcept;

Out Step(State& state, const In& in, const Params& params) noexcept;
// Native puppet preset (docs/native-puppet.md 8.2): steering is the main
// corrector, nudges are capped low (moving/idle 300 mm/s, prone 150), snaps
// only at 3 m held 300 ms moving / 2 m held 1 s idle, the runaway pin is a
// safety net (1500 mm/s, 2 m), and stale input never pins (the arbiter leaves
// after 1000 ms of stale input). Native actions: the split moving runaway
// (overrun or sideways only, thr = max(1500, 20 % of |v_remote|); a slower
// body is `lag`), along-track nudges up to clamp(15 % of |v_remote|, 300,
// 1500) mm/s (prone 150..400) and a 1200 ms stop grace for the idle runaway.
// The facing governor (FaceMode::idle unless `face` says otherwise),
// neutral placements, the placement yaw check (10 degrees) and the windowed
// teleport (3 m in 200 ms). Everything else stays `base`.

Params NativeParams(const Params& base, FaceMode face = FaceMode::idle) noexcept;

// Helpers (also used by tests): wrap radians to (-pi, pi]; the exponential
// smoothing factor 1 - exp(-dt/tau).
double WrapRad(double radians) noexcept;
double Alpha(double dt_ms, double tau_ms) noexcept;
// The time-aligned target delay: clamp(in_age - 16 + align, 1, 150) ms (1
// without a valid input sample). The pose sampler extrapolates the newest
// snapshot by at most 100 ms (pose_interp), so the target follows the remote
// state that the input being consumed produced.
std::uint32_t AlignedDelayMs(bool in_valid, std::uint32_t in_age_ms, std::int32_t align_ms) noexcept;

} // namespace mgscoop::partner_recon
