#pragma once
#include <cstddef>
#include <cstdint>

// Pure client-join reset/re-arm logic (synthesis gap G11).
//
// Without a reset, a client join latches `attempted`/`client_join_consumed` for
// the whole process and leaves the borrowed FobTarget at +0x10=2, +0x48=1 after
// a successful native 0x7EE380 request, so any retry is refused and the game
// must restart. This header sequences a safe recovery without owning any game
// memory: the native caller performs the recorded-field restore and reports
// what it observes back here.
//
// Sequence: Request -> (owned session) close_session, then await_slots until the
// game's session singleton slots are empty, then restore_rearm (restore the
// recorded pre-call fields and clear the latches), then done. With no owned
// session (a synchronous refusal that never created one, or a session the game
// already released) the machine restores and re-arms immediately. Anything
// unsafe or unresolved refuses with a reason.
//
// Current policy (game_script_observer.cpp): close_session is answered with a
// refusal (`owned_session_active`). No guest has ever connected and a
// mid-mission close of a live client session is not statically verified, so
// the close/await phases are kept, tested and unused until a live run proves
// the close path.
//
// Restore-in-place versus Dynamite's fresh-object model. Dynamite allocates a
// zeroed FobTarget (0x80 bytes) and a SessionConnectInfo (>= 0xD8 bytes, +0xB8
// host id, +0xC8 non-null) for every request. That was evaluated and rejected
// for this exe because its allocation/lifetime requirements cannot be verified
// statically: (1) 0x7EE380 hands target+0x60 to 0x1A5D810, an obfuscated
// routine whose retention of the pointer is unknown; (2) the game's own code
// reads the target through server+0x30 -> +0x28 (the borrowed object), so a
// private object is not the one IsConnectReady and the mission flow consult;
// (3) a FOX-heap (0xDBAB0) allocation made in mission context may land in the
// mission block and dangle after the mission, and freeing it needs the matching
// FOX header/free path; (4) the object would need the 0x2223508 vtable and
// constructor state. Restore-in-place allocates nothing, keeps game ownership,
// and reverts only fields whose writers are decoded (see
// dynamite_session_port.cpp).
//
// It is pure and portable so it is unit tested; it performs no I/O, no game
// call and holds no pointers.

namespace mgscoop {

enum class ClientResetReason : std::uint8_t {
    none, manual_key, trigger_file, session_error, bad_state, pending_timeout, session_released
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
    // there is something to re-arm); `owns_session` is true when a live client
    // session still backs the borrowed target and must be closed first; `ready`
    // is true when the scene can accept a re-armed join (co-op rules active, no
    // termination or pending cleanup). A re-arm that could not be used is
    // refused (`not_ready`) and leaves the latches in place.
    ClientResetAction Request(ClientResetReason reason, bool latched, bool owns_session, bool ready = true) noexcept {
        if (phase_ != Phase::idle) return CurrentAction();  // one reset at a time
        reason_ = reason;
        refusal_ = "";
        wait_ticks_ = 0;
        if (!latched && !owns_session) { phase_ = Phase::refused; refusal_ = "nothing_to_reset"; return ClientResetAction::refuse; }
        if (owns_session) { phase_ = Phase::closing; return ClientResetAction::close_session; }
        if (!ready) { phase_ = Phase::refused; refusal_ = "not_ready"; return ClientResetAction::refuse; }
        phase_ = Phase::restoring;
        return ClientResetAction::restore_rearm;
    }

    // Advance an in-progress reset. `owns_session`/`slots_released` are the
    // adapter's current observations. In the closing phase the caller has
    // already initiated the close; here we only wait for the slots to drain.
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

// A live client session still backs the borrowed target when this adapter
// owns one, or when a recorded request's singleton slots are not yet empty
// (for example a request that started but whose created session failed the
// ownership checks). A consumed latch with neither is safe to re-arm.
constexpr bool ClientResetOwnsSession(bool adapter_owns_client, bool has_record, bool record_slots_empty) noexcept {
    return adapter_owns_client || (has_record && !record_slots_empty);
}

// The borrowed FobTarget fields may be reverted only when each holds either the
// value 0x7EE380 leaves after a successful request (+0x10==2, +0x48==1) or its
// recorded pre-call value (synchronous refusal, or an idempotent reset). Every
// false return of 0x7EE380 happens before +0x10=1 is written (0x7EE4A7), so no
// other value is expected; anything else means the game moved on and the
// restore must refuse.
constexpr bool ClientRequestFieldsRestorable(std::uint32_t state, std::uint8_t flag,
                                             std::uint32_t pre_state, std::uint8_t pre_flag) noexcept {
    return (state == 2 || state == pre_state) && (flag == 1 || flag == pre_flag);
}

// Lua literal the reset poll returns after a successful re-arm. A client-check
// run re-arms its diagnostic, never a real join.
constexpr const char* ClientRearmLiteral(bool diagnostic) noexcept {
    return diagnostic ? "return 'rearm_diagnostic'" : "return 'rearm'";
}

// Health of the adapter's owned client session, from a consistent two-sample
// read (state at +0x90, flags E1 started / E3 preparing close / E4 error).
enum class ClientSessionHealth : std::uint8_t {
    unknown,      // not owned, unreadable, inconsistent, or a close is in progress
    connecting,   // state 4-6, or 7 without both members present
    established,  // state 7, started, two present members
    error,        // E4 set
    bad_state,    // any non-client state on a client session (8 and above)
    released      // the game emptied the session singleton slots
};
const char* ClientSessionHealthName(ClientSessionHealth health) noexcept;

// Same mapping as ReadDynamiteSessionStatus for a client, plus bad_state for
// states the client path never uses.
constexpr ClientSessionHealth ClassifyClientSession(std::uint32_t state, bool started, bool closing,
                                                    bool error, bool two_present) noexcept {
    if (error) return ClientSessionHealth::error;
    if (closing) return ClientSessionHealth::unknown;
    if (state >= 4 && state <= 6) return ClientSessionHealth::connecting;
    if (state == 7) return started && two_present ? ClientSessionHealth::established : ClientSessionHealth::connecting;
    return ClientSessionHealth::bad_state;
}

// Watches one submitted client request for the automatic reset triggers:
// session error, a bad (8+) state, a game-released session, or a pending
// timeout without client_established. Each submission yields at most one
// automatic reset request, and the number of automatic re-arms per process is
// bounded so a host that never answers cannot cause an endless retry loop.
class ClientAutoResetWatch {
public:
    static constexpr std::uint64_t kPendingTimeoutMs = 60000;
    static constexpr std::uint64_t kSampleIntervalMs = 1000;
    static constexpr unsigned kMaxAutomaticResets = 5;

    bool watching() const noexcept { return watching_; }
    bool established_seen() const noexcept { return established_; }
    unsigned automatic_resets() const noexcept { return automatic_resets_; }

    void Submitted(std::uint64_t now_ms) noexcept {
        watching_ = true; established_ = false; submitted_ms_ = now_ms; last_sample_ms_ = now_ms;
    }
    void Stop() noexcept { watching_ = false; }
    bool SampleDue(std::uint64_t now_ms) const noexcept {
        return watching_ && now_ms - last_sample_ms_ >= kSampleIntervalMs;
    }
    // Returns the reason for an automatic reset, once per submission.
    ClientResetReason Observe(ClientSessionHealth health, std::uint64_t now_ms) noexcept {
        if (!watching_) return ClientResetReason::none;
        last_sample_ms_ = now_ms;
        ClientResetReason reason = ClientResetReason::none;
        switch (health) {
        case ClientSessionHealth::established: established_ = true; break;
        case ClientSessionHealth::error: reason = ClientResetReason::session_error; break;
        case ClientSessionHealth::bad_state: reason = ClientResetReason::bad_state; break;
        case ClientSessionHealth::released: reason = ClientResetReason::session_released; break;
        default: break;
        }
        if (reason == ClientResetReason::none && !established_ && now_ms - submitted_ms_ >= kPendingTimeoutMs)
            reason = ClientResetReason::pending_timeout;
        if (reason != ClientResetReason::none) watching_ = false;
        return reason;
    }
    bool AutomaticResetAllowed() const noexcept { return automatic_resets_ < kMaxAutomaticResets; }
    void CountAutomaticReset() noexcept { if (automatic_resets_ < kMaxAutomaticResets) ++automatic_resets_; }

private:
    bool watching_ = false;
    bool established_ = false;
    std::uint64_t submitted_ms_ = 0;
    std::uint64_t last_sample_ms_ = 0;
    unsigned automatic_resets_ = 0;
};

inline const char* ClientResetReasonName(ClientResetReason reason) noexcept {
    switch (reason) {
    case ClientResetReason::manual_key: return "manual_key";
    case ClientResetReason::trigger_file: return "trigger_file";
    case ClientResetReason::session_error: return "session_error";
    case ClientResetReason::bad_state: return "bad_state";
    case ClientResetReason::pending_timeout: return "pending_timeout";
    case ClientResetReason::session_released: return "session_released";
    default: return "none";
    }
}

inline const char* ClientSessionHealthName(ClientSessionHealth health) noexcept {
    switch (health) {
    case ClientSessionHealth::connecting: return "connecting";
    case ClientSessionHealth::established: return "established";
    case ClientSessionHealth::error: return "error";
    case ClientSessionHealth::bad_state: return "bad_state";
    case ClientSessionHealth::released: return "released";
    default: return "unknown";
    }
}

} // namespace mgscoop
