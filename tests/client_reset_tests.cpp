// Pure client-reset state-machine checks: no game, Steam or filesystem access.
#include "mgscoop/client_reset.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
} // namespace

int main() {
    using namespace mgscoop;
    using A = ClientResetAction;

    // Reason spellings.
    Check(std::strcmp(ClientResetReasonName(ClientResetReason::manual_key), "manual_key") == 0, "reason manual");
    Check(std::strcmp(ClientResetReasonName(ClientResetReason::trigger_file), "trigger_file") == 0, "reason trigger");
    Check(std::strcmp(ClientResetReasonName(ClientResetReason::session_error), "session_error") == 0, "reason error");
    Check(std::strcmp(ClientResetReasonName(ClientResetReason::bad_state), "bad_state") == 0, "reason bad_state");
    Check(std::strcmp(ClientResetReasonName(ClientResetReason::pending_timeout), "pending_timeout") == 0, "reason timeout");
    Check(std::strcmp(ClientResetReasonName(ClientResetReason::none), "none") == 0, "reason none");

    // Nothing to reset: no latch and no owned session refuses.
    {
        ClientResetMachine m;
        Check(m.Request(ClientResetReason::manual_key, false, false) == A::refuse, "nothing to reset refuses");
        Check(std::strcmp(m.refusal(), "nothing_to_reset") == 0, "nothing_to_reset refusal");
        Check(!m.active(), "refused is not active");
    }

    // Synchronous-failure path: latched, no owned session -> immediate restore.
    {
        ClientResetMachine m;
        Check(m.Request(ClientResetReason::manual_key, true, false) == A::restore_rearm, "latched no-session restores");
        Check(m.reason() == ClientResetReason::manual_key, "reason recorded");
        Check(m.active(), "restoring is active");
        m.CompleteRestore();
        Check(!m.active(), "idle after restore");
        Check(m.resets_completed() == 1, "one reset counted");
    }

    // Owned session path: close -> wait for slots -> restore -> done.
    {
        ClientResetMachine m;
        Check(m.Request(ClientResetReason::session_error, true, true) == A::close_session, "owned session closes first");
        // Session still present, slots not released: keep awaiting.
        Check(m.Tick(true, false) == A::await_slots, "await while owned");
        Check(m.Tick(true, true) == A::await_slots, "await while still owned even if slots look empty");
        Check(m.Tick(false, false) == A::await_slots, "await while slots not yet empty");
        // Ownership cleared and slots empty: restore.
        Check(m.Tick(false, true) == A::restore_rearm, "restore when released and empty");
        m.CompleteRestore();
        Check(m.resets_completed() == 1, "reset counted after owned close");
        Check(!m.active(), "idle after owned reset");
    }

    // Bounded wait: slots never release -> refuse after kMaxWaitTicks.
    {
        ClientResetMachine m;
        Check(m.Request(ClientResetReason::pending_timeout, true, true) == A::close_session, "timeout reset closes");
        A last = A::await_slots;
        for (unsigned i = 0; i < ClientResetMachine::kMaxWaitTicks; ++i) {
            last = m.Tick(true, false);
            Check(last == A::await_slots, "await within bound");
        }
        last = m.Tick(true, false);
        Check(last == A::refuse, "refuse past bound");
        Check(std::strcmp(m.refusal(), "slots_not_released") == 0, "slots refusal");
        Check(!m.active(), "refused after bound not active");
    }

    // A second Request while already in progress does not restart; it reports
    // the current action.
    {
        ClientResetMachine m;
        Check(m.Request(ClientResetReason::manual_key, true, true) == A::close_session, "first request closes");
        Check(m.Request(ClientResetReason::trigger_file, true, true) == A::close_session, "re-request returns current action");
        Check(m.reason() == ClientResetReason::manual_key, "reason unchanged by re-request");
    }

    // Caller-side failure (e.g. close gate refused) stops with its reason, then
    // Clear allows another attempt.
    {
        ClientResetMachine m;
        Check(m.Request(ClientResetReason::bad_state, true, true) == A::close_session, "bad_state closes");
        m.Fail("owned_session_context");
        Check(std::strcmp(m.refusal(), "owned_session_context") == 0, "fail reason recorded");
        Check(!m.active(), "failed not active");
        m.Clear();
        Check(m.reason() == ClientResetReason::none, "cleared reason");
        Check(m.Request(ClientResetReason::manual_key, true, false) == A::restore_rearm, "reset usable after clear");
    }

    // Tick before any Request is idle.
    {
        ClientResetMachine m;
        Check(m.Tick(false, false) == A::idle, "idle tick");
        Check(m.resets_completed() == 0, "no resets without request");
    }

    // Readiness: a latched, sessionless reset in a scene that cannot accept a
    // re-armed join refuses (not_ready) instead of re-arming into a blocked poll.
    {
        ClientResetMachine m;
        Check(m.Request(ClientResetReason::trigger_file, true, false, false) == A::refuse, "not ready refuses");
        Check(std::strcmp(m.refusal(), "not_ready") == 0, "not_ready refusal");
        Check(m.resets_completed() == 0, "not_ready does not count a reset");
        m.Clear();
        Check(m.Request(ClientResetReason::trigger_file, true, false, true) == A::restore_rearm, "ready re-arms after clear");
        // nothing_to_reset is reported ahead of readiness.
        ClientResetMachine n;
        Check(n.Request(ClientResetReason::manual_key, false, false, false) == A::refuse &&
              std::strcmp(n.refusal(), "nothing_to_reset") == 0, "nothing_to_reset before not_ready");
        // An owned session is reported (close_session) regardless of readiness;
        // the observer answers it with owned_session_active.
        ClientResetMachine o;
        Check(o.Request(ClientResetReason::pending_timeout, true, true, false) == A::close_session, "owned ahead of ready");
    }

    // Ownership decision used by the observer.
    Check(!ClientResetOwnsSession(false, false, false), "no owner, no record");
    Check(!ClientResetOwnsSession(false, false, true), "no owner, no record (slots ignored)");
    Check(ClientResetOwnsSession(true, false, false), "adapter owns");
    Check(ClientResetOwnsSession(true, true, true), "adapter owns even with empty record slots");
    Check(ClientResetOwnsSession(false, true, false), "record with live slots owns");
    Check(!ClientResetOwnsSession(false, true, true), "record with empty slots is released");

    // Field restore predicate (pre-call state 0 / flag 0 as required by the prerequisites).
    Check(ClientRequestFieldsRestorable(2, 1, 0, 0), "post-request values restorable");
    Check(ClientRequestFieldsRestorable(0, 0, 0, 0), "untouched (sync refusal) restorable");
    Check(ClientRequestFieldsRestorable(2, 0, 0, 0), "state written, flag untouched restorable");
    Check(ClientRequestFieldsRestorable(0, 1, 0, 0), "flag written, state pre restorable");
    Check(!ClientRequestFieldsRestorable(1, 0, 0, 0), "intermediate state 1 refuses");
    Check(!ClientRequestFieldsRestorable(3, 1, 0, 0), "other state refuses");
    Check(!ClientRequestFieldsRestorable(2, 2, 0, 0), "other flag refuses");
    Check(ClientRequestFieldsRestorable(5, 7, 5, 7), "recorded non-zero pre-values accepted");

    // Re-arm literals.
    Check(std::strcmp(ClientRearmLiteral(false), "return 'rearm'") == 0, "real re-arm literal");
    Check(std::strcmp(ClientRearmLiteral(true), "return 'rearm_diagnostic'") == 0, "diagnostic re-arm literal");
    Check(std::strcmp(ClientResetReasonName(ClientResetReason::session_released), "session_released") == 0, "reason released");

    // Session health classification (mirrors ReadDynamiteSessionStatus for a client).
    {
        using H = ClientSessionHealth;
        Check(ClassifyClientSession(4, false, false, false, false) == H::connecting, "state 4 connecting");
        Check(ClassifyClientSession(6, true, false, false, true) == H::connecting, "state 6 connecting");
        Check(ClassifyClientSession(7, true, false, false, true) == H::established, "state 7 started two established");
        Check(ClassifyClientSession(7, false, false, false, true) == H::connecting, "state 7 not started connecting");
        Check(ClassifyClientSession(7, true, false, false, false) == H::connecting, "state 7 one member connecting");
        Check(ClassifyClientSession(8, true, false, false, true) == H::bad_state, "state 8 bad");
        Check(ClassifyClientSession(9, false, false, false, false) == H::bad_state, "state 9 bad");
        Check(ClassifyClientSession(3, true, false, false, false) == H::bad_state, "host state on client bad");
        Check(ClassifyClientSession(5, true, false, true, false) == H::error, "E4 error wins");
        Check(ClassifyClientSession(8, true, true, true, false) == H::error, "E4 error wins over closing");
        Check(ClassifyClientSession(5, true, true, false, false) == H::unknown, "E3 closing unknown");
        Check(std::strcmp(ClientSessionHealthName(H::released), "released") == 0, "health name released");
        Check(std::strcmp(ClientSessionHealthName(H::unknown), "unknown") == 0, "health name unknown");
    }

    // Automatic reset watch.
    {
        using H = ClientSessionHealth;
        using R = ClientResetReason;
        ClientAutoResetWatch w;
        Check(!w.watching() && !w.SampleDue(5000), "idle watch never samples");
        Check(w.Observe(H::error, 5000) == R::none, "idle watch never fires");

        // Pending timeout: 60 s without client_established.
        w.Submitted(1000);
        Check(w.watching() && !w.SampleDue(1999) && w.SampleDue(2000), "sample interval 1 s");
        Check(w.Observe(H::connecting, 2000) == R::none, "connecting within timeout");
        Check(!w.SampleDue(2999) && w.SampleDue(3000), "interval restarts after a sample");
        Check(w.Observe(H::unknown, 60999) == R::none, "just before the timeout");
        Check(w.Observe(H::connecting, 61000) == R::pending_timeout, "timeout at 60 s");
        Check(!w.watching(), "one automatic request per submission");
        Check(w.Observe(H::error, 62000) == R::none, "stopped watch is silent");

        // Established before the timeout suppresses pending_timeout, but a later
        // error still fires.
        w.Submitted(0);
        Check(w.Observe(H::established, 1000) == R::none && w.established_seen(), "established seen");
        Check(w.Observe(H::connecting, 120000) == R::none, "no timeout after established");
        Check(w.Observe(H::error, 121000) == R::session_error, "error after established fires");

        // Immediate triggers.
        w.Submitted(0); Check(w.Observe(H::error, 1000) == R::session_error, "error fires");
        w.Submitted(0); Check(w.Observe(H::bad_state, 1000) == R::bad_state, "bad state fires");
        w.Submitted(0); Check(w.Observe(H::released, 1000) == R::session_released, "release fires");
        w.Submitted(0); w.Stop(); Check(w.Observe(H::error, 1000) == R::none, "stopped by reset");

        // Bounded automatic re-arms.
        for (unsigned i = 0; i < ClientAutoResetWatch::kMaxAutomaticResets; ++i) {
            Check(w.AutomaticResetAllowed(), "automatic reset allowed below cap");
            w.CountAutomaticReset();
        }
        Check(!w.AutomaticResetAllowed() && w.automatic_resets() == ClientAutoResetWatch::kMaxAutomaticResets, "cap reached");
        w.CountAutomaticReset();
        Check(w.automatic_resets() == ClientAutoResetWatch::kMaxAutomaticResets, "count saturates at cap");
    }

    // End-to-end decision sequence for the observer's reset of a synchronous
    // refusal: latched, no owner, no record -> restore -> re-armed; a second
    // reset with nothing latched refuses nothing_to_reset.
    {
        ClientResetMachine m;
        const bool owns = ClientResetOwnsSession(false, false, false);
        Check(m.Request(ClientResetReason::trigger_file, true, owns, true) == A::restore_rearm, "sync refusal reset restores");
        m.CompleteRestore();
        Check(m.Request(ClientResetReason::trigger_file, false, owns, true) == A::refuse &&
              std::strcmp(m.refusal(), "nothing_to_reset") == 0, "second reset has nothing to reset");
        m.Clear();
        // A started request whose game session was released: record slots empty.
        const bool released_owns = ClientResetOwnsSession(false, true, true);
        Check(m.Request(ClientResetReason::session_released, true, released_owns, true) == A::restore_rearm,
              "released session restores in place");
        Check(ClientRequestFieldsRestorable(2, 1, 0, 0), "released session fields restorable");
    }

    std::printf("%u client-reset checks: reason spellings, nothing-to-reset/not-ready refusals, synchronous restore, "
                "owned close/await/restore sequence, bounded wait, re-request, caller failure and clear, ownership and "
                "field-restore predicates, re-arm literals, session health, automatic reset watch and cap; "
                "no game or filesystem calls\n", checks);
    return 0;
}
