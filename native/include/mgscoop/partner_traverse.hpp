#pragma once
// Traversal replay for the native puppet (docs/native-puppet.md "Traversal").
// Pure state machine, no game access.
//
// The remote's ACTION (E) presses never reach body 1 from its pad (ACTION also
// means ride, knife kill and pickup in the receiver's world), so a ladder, a
// drain pipe or a step-on (climbing onto a ledge, also out of a ledge hang)
// the remote starts with ACTION never starts on body 1. Instead, when the
// remote's act enters one of those acts, body 1 is placed once at the remote's
// aligned pose through the reconciliation's own placement, and only after that
// placement has been verified (window armed again, pad not neutral) one
// 4-frame ACTION overlay tap is requested (retried a bounded number of times
// while the remote stays in the traversal). While the replay waits and while
// body 1 runs a traversal, the reconciliation's far/vertical/runaway
// placements are held off: a placement disarms the window or holds the pad
// neutral, which is what dropped the 0.13.2 taps (live log: remote act 21,
// tap, then `partner_recon=place; reason=vertical` with a neutral pad).
//
// Act ids are the exe's own writers of Q+0x18[id] (1.0.15.4, static census of
// `mov word [act + id*2], imm` after the [+0x138]+0x60 -> +0x18 chain, owners
// named by Ghidra): 21 LadderActionImpl, 22 PipeActionPluginImpl, 23 and 28
// ClimbActionPluginImpl, 24 StepOnActionImpl, 54..56 EludeActionPluginImpl
// (ledge hang). The 0.13.2 whitelist's 49/50/59 are HasBeenFulton/Fulton
// acts and are not traversals. In the 0.13.2 user session the remote's ACTION
// presses were dropped right around 21 and 24 (partner_pad_dropped action>0),
// while 28 (hop) and 54 (grab while falling) were matched natively by body 1
// (EVADE and falling reach it), so only 21/22/24 are replayed.
#include <cstdint>

namespace mgscoop::partner_traverse {

// Acts the remote starts with ACTION: replayed with a tap.
inline constexpr bool ReplayAct(std::uint16_t act) noexcept { return act == 21 || act == 22 || act == 24; }
// Every traversal act body 1 may be running natively (recon placements held).
inline constexpr bool TraverseAct(std::uint16_t act) noexcept {
    return (act >= 21 && act <= 24) || act == 28 || (act >= 54 && act <= 56);
}
// Body acts from which an ACTION start is plausible: locomotion (1..8; 7 is a
// prone side roll, 8 a running step) and the ledge hang (climb up = step-on).
inline constexpr bool SourceAct(std::uint16_t act) noexcept { return (act >= 1 && act <= 8) || (act >= 54 && act <= 56); }

enum class Phase : std::uint8_t { idle, place, tap, native };

struct Params {
    std::uint32_t place_ms = 1200;      // placement must verify within this long
    std::uint32_t entry_ms = 1500;      // body 1 must enter a traversal act within this long of the first tap
    std::uint32_t retap_ms = 350;       // next tap while body 1 has not entered
    std::uint32_t taps = 3;             // taps per episode
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
    bool tap;            // bump one ACTION overlay tap now
    bool hold;           // hold the reconciliation's far/vertical/runaway placements
    // Edge events for the log (at most one per tick).
    enum class Event : std::uint8_t { none, start, placed, tapped, entered, done, abort } event;
    const char* reason;  // start: placed|near; abort/done: the reason
};

struct State {
    Phase phase = Phase::idle;
    std::uint64_t phase_ms = 0, tap_ms = 0, left_ms = 0, remote_trav_ms = 0;
    std::uint16_t remote_prev = 0;
    std::uint16_t act = 0;  // the remote act that started the episode
    std::uint16_t from = 0; // body 1's act when the episode started (a hang stays until the step-on)
    bool hold_off = false;  // remote_left released; do not re-grab while the body is still climbing
    std::uint32_t taps = 0;
    bool have_prev = false;
    bool saw_placing = false;
    // Totals (telemetry).
    std::uint32_t episodes = 0, entered = 0, aborted = 0;
};

Out Step(State& state, const In& in, const Params& params = Params{}) noexcept;
const char* PhaseName(Phase phase) noexcept;
const char* EventName(Out::Event event) noexcept;

} // namespace mgscoop::partner_traverse
