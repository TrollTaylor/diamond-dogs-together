#include "mgscoop/steam_lifecycle.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace mgscoop;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

thread_local std::uint32_t fake_error = 0;
thread_local unsigned original_calls = 0;
thread_local std::uint32_t seen_incoming_error = 0;
thread_local bool return_value = false;
thread_local bool throw_original = false;
struct OriginalFailure { int value; };

std::uint32_t read_error() noexcept { return fake_error; }
void write_error(std::uint32_t value) noexcept { fake_error = value; }

// Keep C++ linkage for the intentional throwing fixture: MSVC /EHsc assumes
// extern-C definitions never throw. Its no-argument calling convention matches.
bool MGSCOOP_STEAM_CALL original_init() {
    ++original_calls;
    seen_incoming_error = fake_error;
    fake_error = 0x9876u;
    if (throw_original) throw OriginalFailure{42};
    return return_value;
}

struct RecordedEvents {
    std::array<SteamInitEvent, 2> events{};
    unsigned count = 0;
    SteamLifecycleTracker* tracker = nullptr;
    bool snapshot_available_in_sink = false;
};

void record_event(void* context, const SteamInitEvent& event) noexcept {
    auto& recorded = *static_cast<RecordedEvents*>(context);
    if (recorded.count < recorded.events.size()) recorded.events[recorded.count] = event;
    ++recorded.count;
    // This would deadlock if tracker bookkeeping held its mutex across the sink.
    recorded.snapshot_available_in_sink = recorded.tracker->snapshot().snapshot_available;
    fake_error = 0xbadu; // Deliberately exercise transparent error restoration.
}

void forwards_bool_and_exception_once() {
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        SteamLifecycleTracker tracker;
        RecordedEvents recorded;
        recorded.tracker = &tracker;
        original_calls = 0;
        seen_incoming_error = 0;
        return_value = scenario == 1;
        throw_original = scenario == 2;
        fake_error = 0x1234u;
        const SteamInitObservationContext context{7, fake_error, read_error, write_error, record_event, &recorded};
        bool caught = false;
        try {
            const bool returned = forward_observed_steam_init(original_init, tracker, context);
            require(scenario != 2 && returned == return_value, "original boolean result must be preserved");
        } catch (const OriginalFailure& error) {
            require(scenario == 2 && error.value == 42, "original exception object must propagate unchanged");
            caught = true;
        }
        require(caught == throw_original && original_calls == 1, "every normal or exceptional invocation forwards exactly once");
        require(seen_incoming_error == 0x1234u && fake_error == 0x9876u, "original incoming and outgoing error state survives instrumentation");
        require(recorded.count == 2 && recorded.snapshot_available_in_sink, "start and completion sinks run outside the tracker lock");
        require(recorded.events[0].stage == SteamInitStage::Entered && recorded.events[1].stage == SteamInitStage::Completed,
                "event order is start then completion");
        require(recorded.events[0].ticket.value != 0 && recorded.events[0].ticket.value == recorded.events[1].ticket.value,
                "one ticket connects both observations");
        const auto evidence = tracker.snapshot();
        require(evidence.active_observations == 0 && evidence.init_entries == 1, "completion retires active observation");
        require(evidence.true_returns == (scenario == 1 ? 1u : 0u) && evidence.false_returns == (scenario == 0 ? 1u : 0u) &&
                    evidence.exceptional_exits == (scenario == 2 ? 1u : 0u), "outcome counters reflect original behavior");
    }
}

void initially_unobserved_and_duplicate_completion() {
    SteamLifecycleTracker tracker;
    auto evidence = tracker.snapshot();
    require(evidence.snapshot_available && evidence.init_entries == 0 && evidence.true_returns == 0,
            "late observer installation begins unobserved, not initialized");
    const auto ticket = tracker.begin_initialization(10);
    evidence = tracker.snapshot();
    require(evidence.active_observations == 1 && evidence.true_returns == 0, "entry is not successful initialization evidence");
    tracker.complete_initialization(ticket, SteamInitCompletion::ReturnedFalse);
    tracker.complete_initialization(ticket, SteamInitCompletion::ReturnedTrue);
    evidence = tracker.snapshot();
    require(evidence.false_returns == 1 && evidence.true_returns == 0 && evidence.invalid_completions == 1,
            "duplicate completion cannot manufacture success");
    const auto retry = tracker.begin_initialization(10);
    require(retry.value != ticket.value, "subsequent initialization has a distinct ticket");
    tracker.complete_initialization(retry, SteamInitCompletion::ReturnedTrue);
    evidence = tracker.snapshot();
    require(evidence.false_returns == 1 && evidence.true_returns == 1 && evidence.last_completed_ticket == retry.value,
            "retry preserves evidence of both attempts");
}

void bounded_overlap_and_capacity() {
    SteamLifecycleTracker tracker;
    std::array<SteamInitTicket, SteamLifecycleTracker::kMaxActiveObservations> tickets{};
    for (std::size_t i = 0; i < tickets.size(); ++i) tickets[i] = tracker.begin_initialization(i + 1);
    const auto overflow = tracker.begin_initialization(99);
    require(overflow.value == 0, "tracker capacity does not overwrite another active observation");
    tracker.complete_initialization(overflow, SteamInitCompletion::ReturnedTrue);
    for (std::size_t i = tickets.size(); i > 0; --i) tracker.complete_initialization(tickets[i - 1], SteamInitCompletion::ReturnedTrue);
    const auto evidence = tracker.snapshot();
    require(evidence.active_observations == 0 && evidence.true_returns == tickets.size(), "out-of-order completions retire only matching tickets");
    require(evidence.maximum_overlap == tickets.size() && evidence.multiple_init_threads, "overlap and thread changes remain visible");
    require(evidence.lost_observations == 1 && evidence.init_entries == tickets.size() + 1,
            "capacity loss is explicit, never claimed as complete observation");
}

void concurrent_bookkeeping() {
    SteamLifecycleTracker tracker;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> release{false};
    std::vector<std::thread> threads;
    constexpr unsigned thread_count = 4;
    constexpr unsigned attempts = 250;
    for (unsigned index = 0; index < thread_count; ++index) {
        threads.emplace_back([&, index] {
            const auto first = tracker.begin_initialization(index + 1);
            ready.fetch_add(1);
            while (!release.load()) std::this_thread::yield();
            tracker.complete_initialization(first, SteamInitCompletion::ReturnedTrue);
            for (unsigned attempt = 1; attempt < attempts; ++attempt) {
                const auto ticket = tracker.begin_initialization(index + 1);
                tracker.complete_initialization(ticket, SteamInitCompletion::ReturnedTrue);
            }
        });
    }
    while (ready.load() != thread_count) std::this_thread::yield();
    release.store(true);
    for (auto& thread : threads) thread.join();
    const auto evidence = tracker.snapshot();
    require(evidence.init_entries == thread_count * attempts && evidence.true_returns == thread_count * attempts,
            "concurrent init observations do not lose or double count events");
    require(evidence.active_observations == 0 && evidence.invalid_completions == 0 && evidence.lost_observations == 0,
            "concurrent completions leave consistent evidence");
    require(evidence.maximum_overlap == thread_count && evidence.multiple_init_threads,
            "known concurrent lifecycle activity is reported without choosing an owner thread");
}
} // namespace

int main() {
    try {
        forwards_bool_and_exception_once();
        initially_unobserved_and_duplicate_completion();
        bounded_overlap_and_capacity();
        concurrent_bookkeeping();
        std::cout << "steam_lifecycle_tests: transparent init forwarding and passive lifecycle evidence passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "steam_lifecycle_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
