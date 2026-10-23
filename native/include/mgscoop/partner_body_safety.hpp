#pragma once
// Partner body safety on the viewer's copy (0.13.6; docs/native-puppet.md "Body
// safety"; the first two-PC Steam session of 2026-10-22). Pure, portable and
// deterministic decisions about instance 1 (the partner's body) only: every
// observation and time is passed in, nothing allocates or touches the game.
// partner_driver feeds them once per tick and executes the result through the
// flows it already owns (the reconciliation's Warp placement, the xform pin and
// the re-show flow of the seat exit). The viewer's own player is never an input
// or an output here.
//
//  - Stuck (recovery): a failed placement (partner_recon=placed; ok=0) while body
//    1 falls, cliff-drops or hangs at a ledge (acts 25, 28, 54..56) and the remote
//    is on foot. The Warp does not take on a hanging body (session H:11366..11724:
//    three rounds of ok=0, 18 s at 22 m) and the reconciliation only retried every
//    5 s; re-show it at the target instead (bits cleared, slot placed, realized).
//  - Drop (prevention): body 1 enters a cliff drop or fall (28/25) from an on-foot
//    act while the remote showed no vertical act (traversal, fall, ledge) within
//    kDropMatchMs and kept its height (within kDropLevelMm) for kDropMatchMs: the
//    input-driven body walked off an edge the remote never crossed (latency,
//    speed mismatch). Place it back at the target (reason=vertical) at once
//    instead of waiting for the 10 m far rule. A landing (26) at the remote's
//    level keeps the episode open: the session's body (H:11285..11309) vaulted
//    the railing, landed on the lip outside it at the deck's height, then fell
//    again into the ledge hang. Body 1's own cliff drop and ledge grab enter the
//    traversal hold (partner_traverse native phase), which only blocks new episodes.
//  - Put-down (carry): while the remote is carried or asleep (42..44, 131, 132)
//    and body 1 is released (put-down 44, asleep 131/132, fall/land 25/26 after
//    it), a body more than kPutDownDropMm below the target is falling off a
//    platform edge (session H:8992..9071: 79 m in 3 s; the carry hold kept every
//    correction off during act 44). Place it at the target (Warp), pin it there
//    (xform) every tick until it holds, then hand it back. Never while the horse
//    cargo owns body 1 (Lua places it on the horse every tick).
#include <cstdint>

namespace mgscoop::partner_body_safety {

// Act classes (Q+0x18; names in partner_act_log.cpp).
// On foot: locomotion and aim acts 1..20.
bool OnFootAct(std::uint16_t act) noexcept;
// Off its footing: fall 25, cliff drop 28, ledge hang 54..56.
bool StuckAct(std::uint16_t act) noexcept;
// A drop edge: fall 25 or cliff drop 28.
bool DropAct(std::uint16_t act) noexcept;
// A remote act that moves it vertically: traversal 21..24, fall/land/evade/cliff
// 25..28, ledge 54..56.
bool VerticalAct(std::uint16_t act) noexcept;
// Carried 42..44, sleep onset 131, sleep 132.
bool CarriedOrAsleepAct(std::uint16_t act) noexcept;
// Released from a carry: put-down 44, asleep 131/132, fall/land 25/26.
bool ReleasedAct(std::uint16_t act) noexcept;

// ---- Stuck: recovery after a failed placement -------------------------------
inline constexpr std::uint32_t kReshowGapMs = 3000;      // at most one re-show per this
inline constexpr std::uint32_t kReshowMissionCap = 20;   // re-shows per mission
struct StuckState {
    std::uint64_t last_ms;
    std::uint32_t count;
};
struct StuckIn {
    std::uint64_t now_ms;
    bool placed_fail;      // the reconciliation reported placed ok=0 this tick
    bool capable;          // the re-show flow runs here (free-roam net or the ride guard)
    bool body_anim_ok;
    std::uint16_t body_act;
    bool remote_valid;
    std::uint16_t remote_act;
};
enum class StuckVerdict : std::uint8_t { none, reshow, not_capable, rate, cap };
const char* StuckVerdictName(StuckVerdict verdict) noexcept;
// none unless placed_fail with body 1 in a StuckAct and the remote on foot.
StuckVerdict StuckStep(StuckState& state, const StuckIn& in) noexcept;

// ---- Drop: prevention at the edge --------------------------------------------
inline constexpr std::uint32_t kDropMatchMs = 500;       // a remote vertical act this close matches the drop
inline constexpr std::uint32_t kDropLevelMm = 500;       // the remote's height stayed within this
inline constexpr std::uint32_t kDropPlaceGapMs = 2000;   // at most one prevention placement per this
inline constexpr std::uint32_t kDropLandWaitMs = 2500;   // a landing (26) at the level keeps the episode open this long
struct DropState {
    bool have_prev;
    std::uint16_t prev_body_act;
    std::uint64_t remote_vertical_ms;  // the remote's last vertical act (0 none)
    std::uint64_t drop_ms;             // an open episode: the body's drop edge (0 none)
    std::uint16_t drop_act;
    double drop_remote_y;              // the remote's height at the edge (mm)
    std::uint64_t last_place_ms;
    std::uint32_t places, matched, refused;
};
struct DropIn {
    std::uint64_t now_ms;
    // The reconciliation tracks body 1 and could place it (armed; no cardboard
    // box, carry or put-down hold).
    bool enabled;
    // A traversal holds the reconciliation's own placements (partner_traverse):
    // no new episode opens. An open one continues: body 1's own cliff drop (28)
    // or ledge grab (54..56) enters that hold on the tick after its edge.
    bool traverse_hold;
    bool body_anim_ok;
    std::uint16_t body_act;
    bool body_ok;
    double body_y;      // mm
    bool remote_valid;
    std::uint16_t remote_act;
    bool target_ok;     // the time-aligned remote pose, live
    double target_y;    // mm
};
enum class DropVerdict : std::uint8_t { none, opened, matched, place, level, remote_off_foot, landed, lost, rate };
const char* DropVerdictName(DropVerdict verdict) noexcept;
struct DropOut {
    DropVerdict verdict;
    std::uint16_t act;      // the drop act of the episode
    std::uint32_t ms;       // since the drop edge
    double remote_dy;       // the remote's height change since the edge (mm)
};
DropOut DropStep(DropState& state, const DropIn& in) noexcept;

// ---- Put-down: a released body falling off a platform edge ---------------------
inline constexpr std::uint32_t kPutDownDropMm = 2000;    // body this far below the target: falling
inline constexpr std::uint32_t kPutDownHoldMm = 500;     // vertical error within this ...
inline constexpr std::uint32_t kPutDownHoldXzMm = 1000;  // ... and horizontal within this ...
inline constexpr std::uint32_t kPutDownHoldMs = 500;     // ... held this long ends the episode
inline constexpr std::uint32_t kPutDownPlaceGapMs = 1000;
inline constexpr std::uint32_t kPutDownPlaces = 4;       // Warps per episode, then the pin alone
inline constexpr std::uint32_t kPutDownMaxMs = 8000;     // episode bound
struct PutDownState {
    bool active;
    std::uint64_t start_ms, hold_since, last_place_ms;
    std::uint32_t places, pins, episodes;
};
struct PutDownIn {
    std::uint64_t now_ms;
    bool body_ok;
    double body[3];      // mm
    bool body_anim_ok;
    std::uint16_t body_act;
    bool remote_valid;
    std::uint16_t remote_act;
    bool target_ok;      // live
    double target[3];    // mm
    // The horse cargo holds body 1 (coop_carry's cargo hold, not a stock carry):
    // Lua places it on the horse every tick. No episode; an active one ends.
    bool cargo;
};
enum class PutDownAction : std::uint8_t { none, place, pin };
struct PutDownOut {
    PutDownAction action;
    bool started, ended;
    const char* end_reason;   // static: held, scope, cargo, timeout
    double dy;                // target y - body y (mm; positive: body below)
    double err_xz;            // horizontal error (mm)
    std::uint32_t ms;         // since the episode start
};
// While active the guard owns body 1 (the driver skips the reconciliation's
// own execution and the carry hold for that tick).
PutDownOut PutDownStep(PutDownState& state, const PutDownIn& in) noexcept;

} // namespace mgscoop::partner_body_safety
