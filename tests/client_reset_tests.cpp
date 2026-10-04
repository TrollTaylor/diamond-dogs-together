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

    std::printf("%u client-reset checks: reason spellings, nothing-to-reset refusal, synchronous restore, "
                "owned close/await/restore sequence, bounded wait, re-request, caller failure and clear; "
                "no game or filesystem calls\n", checks);
    return 0;
}
