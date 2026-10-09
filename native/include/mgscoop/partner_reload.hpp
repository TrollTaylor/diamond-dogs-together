#pragma once
// The partner's own reload (docs/native-puppet.md "Weapons", R1..R6), pure:
// no Win32 calls and no game memory (mgs_coop_core).
//
// Body 1 already enters its own native reload when the remote's R press
// reaches it (the reload enter 0x1077A36 writes rec+0x3C0 = ((aim | 0x200002)
// << 5) | old, the transition 0x1040A00 sets rec+0x2C0 = 11/12). The magazine
// mirror broke it: it wrote the remote's refilled magazine while body 1 was
// still reloading, the completion 0x1054900 (non-local branch) then computed
// from its own base rec+0x3E8 and the reload mode never left. The fixes:
//   raw   - the mirror freezes while either side reloads and reconciles once
//           afterwards (MirrorFrozen; the hook also writes rec+0x3E8).
//   event - raw plus the explicit event: input frame v5 carries the sender's
//           gun mode and a reload counter; when the remote enters 11/12 the
//           driver taps RELOAD through the input overlay until body 1 enters its
//           own reload mode (Machine/Step), and the raw R bit (also CARRY) is
//           stripped from the remote's sample. Holstered and empty-magazine
//           auto reloads arrive the same way (the sender's mode, not its pad).
// Every refusal falls back to the plain mirror (no animation, the magazine
// still follows the remote).
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::partner_reload {
// rec+0x2C0 gun modes (the mode table 0x2C556A0, contract-pinned).
inline constexpr std::uint8_t kModeAim = 7, kModeChange = 8, kModeReload = 11, kModeReload2 = 12;
inline constexpr bool ReloadMode(std::uint8_t mode) noexcept { return mode == kModeReload || mode == kModeReload2; }

enum class State : std::uint8_t { idle, pending, entered };
const char* StateName(State state) noexcept;

struct Params {
    std::uint32_t verify_ms = 250;   // a tap body 1 did not answer with a reload mode within this is retried
    std::uint32_t max_taps = 3;      // taps per remote reload before the fallback (timeout)
    // A body reload mode older than this is reported stuck (telemetry only:
    // the machine stays entered and the mirror frozen until the native exit).
    // Body 1 reloads at its own playback rate (rec+0x310 of instance 1, about
    // 1.0 offline: 3.02 s for stock46, run 53e36944); shell-by-shell weapons
    // take several seconds.
    std::uint32_t stuck_ms = 8000;
};

// One driver tick.
struct In {
    std::uint64_t now_ms;
    PartnerNativeReload setting;
    // The native drive is active with the gun stage and an armed, non-neutral
    // window (a tap can reach body 1).
    bool active;
    // The latest live sample is v5 with GunFlag::Valid: the sender's gun mode,
    // reload counter and its reloading flag.
    bool remote_valid;
    std::uint8_t remote_mode, remote_seq;
    bool remote_reloading;
    // Body 1's gun record this tick (rec+0x2C0) and the reload preconditions:
    // a gun class, the reload gate 0x103E3A4, the CanReload replica and the
    // weapon gate (the remote's gun is body 1's).
    bool body_ok;
    std::uint8_t body_mode;
    bool gun_class, gate, can_reload, weapon_match;
    // Body 1's magazine as its getter reads it (refill_ms telemetry: the first
    // change the body itself makes after body_enter; the mirror is frozen then).
    bool body_mag_ok;
    std::uint16_t body_mag;
    // A one-shot test request (partner.trigger rtap=1, the worker's
    // "partner rtap=1"): one tap now when the preconditions hold (event only).
    bool force_tap;
};
struct Out {
    bool tap;            // bump the overlay tap with RELOAD now
    bool hold;           // the mirror freezes (pending or entered)
    bool stuck;          // body 1's reload mode passed stuck_ms (telemetry; the mirror stays frozen until the exit)
    // static: start | tap | retry | entered | complete | complete_late | stuck | fallback, or nullptr
    const char* event;
    const char* reason;  // static fallback reason, or nullptr
    // Body 1's own reload mode this tick, every setting (the RA evidence of
    // mirror and raw too): "body_enter" | "body_exit", or nullptr.
    const char* body_event;
    // With body_exit: the native reload's length (body_exit - body_enter).
    std::uint32_t len_ms;
    // The remote started another reload while body 1 was still entered and is
    // still reloading at body 1's exit: one tap now (tap is set, the state is
    // pending again).
    bool queued_tap;
    // While that queued tap is pending: no hold, and the mirror writes the
    // remote's magazine although the remote reloads (body 1's own completion
    // filled its magazine; its next reload request needs it below capacity).
    bool reconcile;
};
// Per mission (Reset at MissionStart). Counters are mission totals.
struct Machine {
    State state;
    bool have_seq;
    std::uint8_t last_seq;
    std::uint64_t since_ms, tap_ms, body_since_ms;
    std::uint32_t taps_this;
    bool body_reload, stuck_latched;
    // A remote reload start seen while entered (queued for body 1's exit) and
    // the pending tap it caused (the reconcile instead of the hold).
    bool queued, queued_reconcile;
    std::uint32_t starts, taps, entered, completed, completed_late, stuck, fallbacks, body_entries, queued_taps;
    const char* last_reason;
    // Telemetry (milliseconds; 0 until measured): the last and longest native
    // reload (body_exit - body_enter), the remote's last mode-11/12 span and
    // body_enter -> the first magazine change body 1 made itself.
    std::uint32_t len_ms, len_max_ms, remote_len_ms, refill_ms;
    bool remote_in_reload, refill_seen, enter_mag_ok;
    std::uint64_t remote_since_ms;
    std::uint16_t enter_mag;
};
void Reset(Machine& machine) noexcept;
// Fallback reasons: inactive (no armed native gun window), weapon (the weapon
// gate strips RELOAD), body (body 1's gun record unread), cls (not a gun
// class), gate (the reload gate), no_reload (the CanReload replica: full,
// no stock or the no-reload bit), timeout (max_taps taps without a reload
// mode), remote_done (the remote left its reload before body 1 entered).
// entered lasts until body 1's own exit (every exit counts: complete, or
// complete_late after stuck_ms); a stuck latch never releases it.
Out Step(Machine& machine, const In& in, const Params& params = Params{}) noexcept;

// The hook's freeze rule for the magazine mirror: mirror never freezes; raw and
// event freeze while the remote reloads (v5 flag; not while the driver's queued
// tap asks for the reconcile), while the driver holds, or while body 1 sits in
// a reload mode, however long (the reconcile runs once, after body 1's native
// exit, never at a stuck latch).
inline constexpr bool MirrorFrozen(PartnerNativeReload setting, bool remote_reloading, std::uint8_t body_mode, bool hold,
                                   bool reconcile) noexcept {
    return setting != PartnerNativeReload::mirror && ((remote_reloading && !reconcile) || hold || ReloadMode(body_mode));
}

// Sender side (input frame v5): reload_seq advances on each entry into a
// reload mode, switch_seq on each change of the current slot (0..15), both
// mod 256, only between two valid reads (an unreadable tick never counts).
struct SenderState {
    bool have;
    std::uint8_t mode, slot, reload_seq, switch_seq;
};
void SenderStep(SenderState& state, bool valid, std::uint8_t mode, std::uint8_t slot) noexcept;
} // namespace mgscoop::partner_reload
