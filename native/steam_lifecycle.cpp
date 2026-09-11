#include "mgscoop/steam_lifecycle.hpp"

#include <algorithm>

namespace mgscoop {

SteamInitTicket SteamLifecycleTracker::begin_initialization(std::uint64_t thread) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        ++evidence_.init_entries;
        if (evidence_.init_entries == 1) evidence_.first_init_thread = thread;
        else if (evidence_.first_init_thread != thread) evidence_.multiple_init_threads = true;
        for (auto& active : active_) {
            if (active == 0 && next_ticket_ != 0) {
                active = next_ticket_++;
                ++evidence_.active_observations;
                evidence_.maximum_overlap = std::max(evidence_.maximum_overlap, evidence_.active_observations);
                return {active};
            }
        }
        lost_observations_.fetch_add(1, std::memory_order_relaxed);
    } catch (...) {
        // Diagnostics must not prevent the original function from running.
        lost_observations_.fetch_add(1, std::memory_order_relaxed);
    }
    return {};
}

void SteamLifecycleTracker::complete_initialization(SteamInitTicket ticket, SteamInitCompletion completion) noexcept {
    if (ticket.value == 0) return; // Begin already recorded loss for this attempt.
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& active : active_) {
            if (active != ticket.value) continue;
            active = 0;
            --evidence_.active_observations;
            switch (completion) {
                case SteamInitCompletion::ReturnedFalse: ++evidence_.false_returns; break;
                case SteamInitCompletion::ReturnedTrue: ++evidence_.true_returns; break;
                case SteamInitCompletion::Threw: ++evidence_.exceptional_exits; break;
                default:
                    ++evidence_.invalid_completions;
                    return;
            }
            evidence_.last_completed_ticket = ticket.value;
            evidence_.last_completion = completion;
            return;
        }
        ++evidence_.invalid_completions;
    } catch (...) {
        lost_observations_.fetch_add(1, std::memory_order_relaxed);
    }
}

SteamLifecycleEvidence SteamLifecycleTracker::snapshot() const noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        auto result = evidence_;
        result.snapshot_available = true;
        result.lost_observations = lost_observations_.load(std::memory_order_relaxed);
        return result;
    } catch (...) {
        SteamLifecycleEvidence result;
        result.lost_observations = lost_observations_.load(std::memory_order_relaxed);
        return result;
    }
}

bool forward_observed_steam_init(SteamInitCall original, SteamLifecycleTracker& tracker,
                                const SteamInitObservationContext& context) {
    const auto ticket = tracker.begin_initialization(context.thread);
    if (context.sink) {
        context.sink(context.sink_context, {SteamInitStage::Entered, ticket, context.thread, SteamInitCompletion::ReturnedFalse});
    }
    context.write_error(context.incoming_error);
    bool result;
    try {
        result = original();
    } catch (...) {
        const auto outgoing_error = context.read_error();
        tracker.complete_initialization(ticket, SteamInitCompletion::Threw);
        if (context.sink) {
            context.sink(context.sink_context, {SteamInitStage::Completed, ticket, context.thread, SteamInitCompletion::Threw});
        }
        context.write_error(outgoing_error);
        throw;
    }
    const auto outgoing_error = context.read_error();
    const auto completion = result ? SteamInitCompletion::ReturnedTrue : SteamInitCompletion::ReturnedFalse;
    tracker.complete_initialization(ticket, completion);
    if (context.sink) {
        context.sink(context.sink_context, {SteamInitStage::Completed, ticket, context.thread, completion});
    }
    context.write_error(outgoing_error);
    return result;
}

} // namespace mgscoop
