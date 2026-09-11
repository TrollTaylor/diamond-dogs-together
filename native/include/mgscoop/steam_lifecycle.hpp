#pragma once

#include "mgscoop/steam_runtime.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace mgscoop {

enum class SteamInitCompletion { ReturnedFalse, ReturnedTrue, Threw };
enum class SteamInitStage { Entered, Completed };

struct SteamInitTicket {
    std::uint64_t value = 0;
};

struct SteamLifecycleEvidence {
    bool snapshot_available = false;
    std::uint64_t init_entries = 0;
    std::uint64_t true_returns = 0;
    std::uint64_t false_returns = 0;
    std::uint64_t exceptional_exits = 0;
    std::uint64_t active_observations = 0;
    std::uint64_t maximum_overlap = 0;
    std::uint64_t first_init_thread = 0;
    bool multiple_init_threads = false;
    std::uint64_t last_completed_ticket = 0;
    SteamInitCompletion last_completion = SteamInitCompletion::ReturnedFalse;
    std::uint64_t invalid_completions = 0;
    std::uint64_t lost_observations = 0;
    // This evidence never supplies SteamLifecycle::EngineInitialized. A captured
    // true return cannot establish complete shutdown coverage or synchronization.
};

class SteamLifecycleTracker {
public:
    static constexpr std::size_t kMaxActiveObservations = 16;

    SteamInitTicket begin_initialization(std::uint64_t thread) noexcept;
    void complete_initialization(SteamInitTicket ticket, SteamInitCompletion completion) noexcept;
    SteamLifecycleEvidence snapshot() const noexcept;

private:
    // This lock protects only bounded bookkeeping. It is never held while calling
    // the original SDK export, logging, invoking a sink, or entering game code.
    mutable std::mutex mutex_;
    std::array<std::uint64_t, kMaxActiveObservations> active_{};
    SteamLifecycleEvidence evidence_;
    std::uint64_t next_ticket_ = 1;
    std::atomic<std::uint64_t> lost_observations_{0};
};

using SteamInitCall = bool(MGSCOOP_STEAM_CALL*)();

struct SteamInitEvent {
    SteamInitStage stage;
    SteamInitTicket ticket;
    std::uint64_t thread;
    // Meaningful only for Completed events.
    SteamInitCompletion completion;
};

struct SteamInitObservationContext {
    std::uint64_t thread = 0;
    std::uint32_t incoming_error = 0;
    std::uint32_t (*read_error)() noexcept = nullptr;
    void (*write_error)(std::uint32_t) noexcept = nullptr;
    void (*sink)(void* context, const SteamInitEvent& event) noexcept = nullptr;
    void* sink_context = nullptr;
};

// Transparent forwarding helper with injectable error access and event sink.
// Preconditions: original/read_error/write_error are non-null; sinks do not throw.
// The original is called exactly once and its bool or C++ exception propagates.
// C++ exception exits are observed; an SEH/process-termination exit may leave an
// outstanding ticket and must never be reported as successful initialization.
bool forward_observed_steam_init(SteamInitCall original, SteamLifecycleTracker& tracker,
                                const SteamInitObservationContext& context);

} // namespace mgscoop
