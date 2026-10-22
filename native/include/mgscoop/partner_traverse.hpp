#pragma once
// Traversal replay for the native puppet (docs/native-puppet.md "Traversal").
// Pure state machine, no game access.
//
// The remote's ACTION (E) presses never reach body 1 from its pad (ACTION also
// means ride, knife kill and pickup in the receiver's world), so a ladder, a
// drain pipe, a wall climb or a step-on (climbing onto a ledge, also out of a
// ledge hang) the remote starts with ACTION never starts on body 1. Instead,
// when the remote's act enters one of those acts, body 1 is placed once at the
// remote's aligned pose through the reconciliation's own placement, and only
// after that placement has been verified (window armed again, pad not neutral)
// the machine requests body 1's action icon for that plugin (Out::icon, a level
// with a per-episode icon_seq) until body 1 enters a traversal act or entry_ms
// passes. While the replay waits and while body 1 runs a traversal, the
// reconciliation's far/vertical/runaway placements are held off (a placement
// disarms the window or holds the pad neutral).
//
// The icon request (F3, 0.13.5; native/partner_icon.*): a stock start goes
// through the HUD action icon, whose builder runs only for the local player
// (0x141155B87) and whose completion (IsCompletedActionIcon 0x140AFD870, padop
// vt+0x28) compares the slot's plugin with block 0's, so body 1's polls (which
// pass block 1's own plugin objects) never complete. Inside instance 1's drain
// window a padop vtable copy answers body 1's query of the requested plugin by
// running that plugin's own ExecActionButtonFunction for idx 1, once per
// icon_seq. Unproven and off by default (lab key partner_native_traverse, play
// forces 0) until a hidden-worker FRAME shows body 1 on a ladder.
//
// Act ids are the exe's own writers of Q+0x18[id] (1.0.15.4, static census of
// `mov word [act + id*2], imm` after the [+0x138]+0x60 -> +0x18 chain, owners
// named by Ghidra): 21 LadderActionImpl, 22 PipeActionPluginImpl, 23
// ClimbActionPluginImpl (wall climb), 24 StepOnActionImpl, 28 CliffActionImpl
// (cliff drop), 54..56 EludeActionPluginImpl (ledge hang). The 0.13.2
// whitelist's 49/50/59 are HasBeenFulton/Fulton acts and are not traversals.
// 25..28 and 54..56 (falls, evade, drops, the grab while falling) start
// without ACTION and are matched natively by body 1 (EVADE and falling reach
// it), so only 21..24 are requested and never 25..28 or 54..56 as targets.
#include <cstdint>

namespace mgscoop::partner_traverse {

// Acts the remote starts through the action icon: replayed with an icon request.
inline constexpr bool ReplayAct(std::uint16_t act) noexcept { return act >= 21 && act <= 24; }
// Every traversal act body 1 may be running natively (recon placements held).
inline constexpr bool TraverseAct(std::uint16_t act) noexcept {
    return (act >= 21 && act <= 24) || act == 28 || (act >= 54 && act <= 56);
}
// The action plugin whose icon starts a replay act (partner_icon::Kind has the
// same values): 21 ladder, 22 pipe, 23 climb, 24 step-on; none for any other act.
enum class Icon : std::uint8_t { none = 0, ladder = 1, pipe = 2, climb = 3, step_on = 4 };
inline constexpr Icon IconFor(std::uint16_t act) noexcept {
    return act == 21 ? Icon::ladder : act == 22 ? Icon::pipe : act == 23 ? Icon::climb : act == 24 ? Icon::step_on : Icon::none;
}
const char* IconName(Icon icon) noexcept;
// The ladder start's own source mask (LadderActionImpl::CheckEnableStartLadder
// 0x1411E9B70 -> FUN_1411E9A70, mask 0x18200001E): acts 1..4, 25, 31 and 32.
// The game refuses a ladder from any other act (prone 5/6, 7, 8 included).
inline constexpr std::uint64_t kLadderSourceMask = 0x18200001EULL;
inline constexpr bool LadderSourceAct(std::uint16_t act) noexcept { return act < 64 && ((kLadderSourceMask >> act) & 1u) != 0; }
// Body acts from which the plugin's own Exec can start `replay` (the stock
// gates; Exec enforces them anyway, this keeps futile requests and their
// telemetry out): a ladder only from the mask above (FUN_1411E9A70); a pipe
// (FUN_141210510) or a wall climb (CheckActionState 0x14114A900) from acts
// 1..20; a step-on (FUN_141293790, Exec's act 0x36 path) from 1..20, 31, 32
// and the ledge hang 54 (climb up). Never from an unknown replay act.
inline constexpr bool SourceAct(std::uint16_t replay, std::uint16_t act) noexcept {
    switch (replay) {
    case 21: return LadderSourceAct(act);
    case 22:
    case 23: return act >= 1 && act <= 20;
    case 24: return (act >= 1 && act <= 20) || act == 31 || act == 32 || act == 54;
    default: return false;
    }
}

enum class Phase : std::uint8_t { idle, place, request, native };

struct Params {
    std::uint32_t place_ms = 1200;      // placement must verify within this long
    std::uint32_t entry_ms = 1500;      // body 1 must enter a traversal act within this long of the request phase
    std::uint32_t grace_ms = 1500;      // placements stay held this long after body 1 left the traversal
    std::uint32_t native_max_ms = 45000;  // longest held traversal (a long ladder or pipe)
    double near_mm = 300.0;             // no placement when body 1 is already this close ...
    double near_yaw_rad = 0.35;         // ... and faces within this
    double far_mm = 8000.0;             // no replay when body 1 is farther (recon places it normally)
};

struct In {
    std::uint64_t now_ms;
    bool enabled;       // native drive active, traversal key on
    bool blocked;       // a carry, a downed remote, an overlay, a seat or a vehicle owns body 1
    bool armed;         // the window is armed this tick with a live (not neutral) pad
    bool placing;       // the reconciliation is placing or verifying this tick
    bool remote_ok;
    std::uint16_t remote_act;  // the time-aligned sample's act (the target's timebase)
    bool body_ok;
    std::uint16_t body_act;
    bool target_ok;
    double target[3];  // mm
    double target_yaw;
    double body[3];    // mm
    double body_yaw;
};

struct Out {
    bool request_place;  // ask the reconciliation for one placement now
    // Body 1's icon request: a level, non-none on every tick of the request
    // phase whose body act is a source act of the episode's replay (window
    // armed, not placing); icon_seq is the episode's id (0 with icon none).
    Icon icon;
    std::uint32_t icon_seq;
    bool hold;           // hold the reconciliation's far/vertical/runaway placements
    // Edge events for the log (at most one per tick).
    enum class Event : std::uint8_t { none, start, placed, requested, entered, done, abort } event;
    const char* reason;  // start: placed|near; abort/done: the reason
};

struct State {
    Phase phase = Phase::idle;
    std::uint64_t phase_ms = 0, left_ms = 0, remote_trav_ms = 0;
    std::uint16_t remote_prev = 0;
    std::uint16_t act = 0;  // the remote act that started the episode
    std::uint16_t from = 0; // body 1's act when the episode started (a hang stays until the step-on)
    bool hold_off = false;  // remote_left released; do not re-grab while the body is still climbing
    bool requested = false; // this episode's icon level was raised at least once
    bool request_logged = false;  // its `requested` event went out
    std::uint32_t icon_seq = 0;  // the current episode's id (bumped by every start, never 0 after one)
    bool have_prev = false;
    bool saw_placing = false;
    // Totals (telemetry).
    std::uint32_t episodes = 0, requests = 0, entered = 0, aborted = 0;
};

Out Step(State& state, const In& in, const Params& params = Params{}) noexcept;
const char* PhaseName(Phase phase) noexcept;
const char* EventName(Out::Event event) noexcept;

} // namespace mgscoop::partner_traverse
