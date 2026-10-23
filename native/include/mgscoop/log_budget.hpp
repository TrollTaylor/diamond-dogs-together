#pragma once
// Bounded diagnostic log budgets (0.13.6). Pure and portable.
//
// The first two-PC session (2026-10-22) lost diagnostics to mission-scoped caps
// (partner_recon=capped; limit=64; kind=frozen about 4 min into a 25 min Mother
// Base mission, which then hid the carry hold of the 79 m put-down fall) and to
// the world LOG window at a mission arrival (world=log_cap; dropped=52). These
// budgets keep every family bounded while letting later events through.
#include <cstdint>

namespace mgscoop::log_budget {

// Rotating per-family cap: at most per_window lines in each window_ms window,
// then one "capped" line; the first line of a later window is preceded by one
// "resumed" line with the dropped count; at most per_mission lines in all, then
// one final capped line and nothing more until the state is reset (mission start).
struct Limits {
    std::uint32_t per_window;
    std::uint32_t window_ms;
    std::uint32_t per_mission;
};
struct Rotating {
    bool started, capped, closed;
    std::uint64_t window_start;
    std::uint32_t lines;     // in the current window
    std::uint32_t total;     // since the reset
    std::uint32_t dropped;   // since the last emitted line
};
enum class Verdict : std::uint8_t {
    emit,           // log the line
    emit_resumed,   // log "resumed; dropped=<Decision::dropped>", then the line
    drop_capped,    // log the window's capped line once, drop the line
    drop_closed,    // log the mission's final capped line once, drop the line
    drop,           // drop silently
};
struct Decision {
    Verdict verdict;
    std::uint32_t dropped;   // emit_resumed: lines dropped before this one
};
Decision Admit(Rotating& state, const Limits& limits, std::uint64_t now_ms) noexcept;

// Token bucket: bursts of up to `capacity` lines, refilled at refill_per_s
// lines per second (the steady-state bound). The first line after drops
// reports the dropped count (the caller logs one summary before it).
struct Bucket {
    bool started;
    double tokens;
    std::uint64_t last_ms;
    std::uint32_t dropped;
};
struct Take {
    bool emit;
    std::uint32_t report_dropped;   // > 0: log the summary with this count before the line
};
Take TakeToken(Bucket& bucket, std::uint32_t capacity, std::uint32_t refill_per_s, std::uint64_t now_ms) noexcept;

} // namespace mgscoop::log_budget
