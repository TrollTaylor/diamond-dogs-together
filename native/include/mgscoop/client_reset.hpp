#pragma once
#include <cstddef>
#include <cstdint>

// Pure client-join reset/re-arm sequencer (synthesis gap G11).
//
// Today a client join latches `attempted`/`client_join_consumed` for the whole
// process and leaves the borrowed FobTarget at +0x10=2, +0x48=1 after the
// native 0x7EE380 request, so any retry is refused and the game must restart.
// This machine sequences a safe recovery without owning any game memory itself:
// the native caller drives the *existing verified* owned-session close path and
// the recorded-field restore, and reports what it observes back here each tick.
//
// Sequence: Request -> (owned session) close_session, then await_slots until the
// game's session singleton slots are empty, then restore_rearm (restore the
// recorded pre-call fields and clear the latches), then done. With no owned
// session (a synchronous refusal that never created one) the machine restores
// and re-arms immediately. Anything unsafe or unresolved refuses with a reason.
//
// It is pure and portable so it is unit tested; it performs no I/O, no game
// call and holds no pointers. The caller alone decides the close is safe
// through the dynamite_session_port owned-session/thread contracts.

namespace mgscoop {

enum class ClientResetReason : std::uint8_t {
    none, manual_key, trigger_file, session_error, bad_state, pending_timeout
};
const char* ClientResetReasonName(ClientResetReason reason) noexcept;

enum class ClientResetAction : std::uint8_t {
    idle,           // no reset in progress
    refuse,         // stop; see refusal()
    close_session,  // caller: initiate the verified owned-session close (once)
    await_slots,    // caller: keep waiting for the session slots to empty
    restore_rearm,  // caller: restore the recorded fields and clear the latches
    done            // restore_rearm acknowledged; machine is idle again
};

class ClientResetMachine {
public:
    // Bounded wait for the close to release the singleton slots. At 60 Hz this
    // is ~10 s; a reset that does not release by then refuses rather than hang.
    static constexpr unsigned kMaxWaitTicks = 600;

    bool active() const noexcept { return phase_ == Phase::closing || phase_ == Phase::awaiting || phase_ == Phase::restoring; }
    ClientResetReason reason() const noexcept { return reason_; }
    const char* refusal() const noexcept { return refusal_; }
    unsigned resets_completed() const noexcept { return resets_completed_; }

    // Begin a reset. `latched` is true when a join was consumed/attempted (so
    // there is something to re-arm); `owns_session` is true when the native
    // adapter currently owns a client session that must be closed first.
    ClientResetAction Request(ClientResetReason reason, bool latched, bool owns_session) noexcept {
        if (phase_ != Phase::idle) return CurrentAction();  // one reset at a time
        reason_ = reason;
        refusal_ = "";
        wait_ticks_ = 0;
        if (!latched && !owns_session) { phase_ = Phase::refused; refusal_ = "nothing_to_reset"; return ClientResetAction::refuse; }
        if (owns_session) { phase_ = Phase::closing; return ClientResetAction::close_session; }
        phase_ = Phase::restoring;
        return ClientResetAction::restore_rearm;
    }

    // Advance an in-progress reset. `owns_session`/`slots_released` are the
    // adapter's fresh observations. In the closing phase the caller has already
    // initiated the close; here we only wait for the slots to drain.
    ClientResetAction Tick(bool owns_session, bool slots_released) noexcept {
        switch (phase_) {
        case Phase::closing:
        case Phase::awaiting:
            if (!owns_session && slots_released) { phase_ = Phase::restoring; return ClientResetAction::restore_rearm; }
            phase_ = Phase::awaiting;
            if (++wait_ticks_ > kMaxWaitTicks) { phase_ = Phase::refused; refusal_ = "slots_not_released"; return ClientResetAction::refuse; }
            return ClientResetAction::await_slots;
        case Phase::restoring:
            return ClientResetAction::restore_rearm;
        default:
            return CurrentAction();
        }
    }

    // The caller performed restore_rearm successfully: re-armed and latches clear.
    void CompleteRestore() noexcept {
        if (phase_ == Phase::restoring) { ++resets_completed_; phase_ = Phase::idle; wait_ticks_ = 0; }
    }

    // The caller could not perform the current action safely (e.g. the owned
    // close gate refused, or the restore context no longer matched).
    void Fail(const char* refusal) noexcept {
        phase_ = Phase::refused;
        refusal_ = refusal ? refusal : "unspecified";
    }

    // Return to idle after a refusal has been reported, so a later trigger can
    // try again (for example once the operator fixes the host).
    void Clear() noexcept { phase_ = Phase::idle; reason_ = ClientResetReason::none; refusal_ = ""; wait_ticks_ = 0; }

private:
    enum class Phase : std::uint8_t { idle, closing, awaiting, restoring, refused };
    ClientResetAction CurrentAction() const noexcept {
        switch (phase_) {
        case Phase::closing: return ClientResetAction::close_session;
        case Phase::awaiting: return ClientResetAction::await_slots;
        case Phase::restoring: return ClientResetAction::restore_rearm;
        case Phase::refused: return ClientResetAction::refuse;
        default: return ClientResetAction::idle;
        }
    }
    Phase phase_ = Phase::idle;
    ClientResetReason reason_ = ClientResetReason::none;
    const char* refusal_ = "";
    unsigned wait_ticks_ = 0;
    unsigned resets_completed_ = 0;
};

inline const char* ClientResetReasonName(ClientResetReason reason) noexcept {
    switch (reason) {
    case ClientResetReason::manual_key: return "manual_key";
    case ClientResetReason::trigger_file: return "trigger_file";
    case ClientResetReason::session_error: return "session_error";
    case ClientResetReason::bad_state: return "bad_state";
    case ClientResetReason::pending_timeout: return "pending_timeout";
    default: return "none";
    }
}

} // namespace mgscoop
