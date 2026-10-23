// Bounded diagnostic log budgets (0.13.6): see log_budget.hpp.
#include "mgscoop/log_budget.hpp"

namespace mgscoop::log_budget {

Decision Admit(Rotating& st, const Limits& limits, std::uint64_t now) noexcept {
    Decision d{};
    if (st.closed) {
        ++st.dropped;
        d.verdict = Verdict::drop;
        return d;
    }
    if (!st.started || now < st.window_start || (limits.window_ms && now - st.window_start >= limits.window_ms)) {
        st.started = true;
        st.window_start = now;
        st.lines = 0;
        st.capped = false;
    }
    if (st.total >= limits.per_mission) {
        st.closed = true;
        ++st.dropped;
        d.verdict = Verdict::drop_closed;
        return d;
    }
    if (st.lines >= limits.per_window) {
        ++st.dropped;
        d.verdict = st.capped ? Verdict::drop : Verdict::drop_capped;
        st.capped = true;
        return d;
    }
    ++st.lines;
    ++st.total;
    d.dropped = st.dropped;
    d.verdict = st.dropped ? Verdict::emit_resumed : Verdict::emit;
    st.dropped = 0;
    return d;
}

Take TakeToken(Bucket& b, std::uint32_t capacity, std::uint32_t refill_per_s, std::uint64_t now) noexcept {
    Take t{};
    if (!b.started) {
        b.started = true;
        b.tokens = static_cast<double>(capacity);
        b.last_ms = now;
    } else if (now > b.last_ms) {
        b.tokens += static_cast<double>(now - b.last_ms) * static_cast<double>(refill_per_s) / 1000.0;
        if (b.tokens > capacity) b.tokens = static_cast<double>(capacity);
        b.last_ms = now;
    } else if (now < b.last_ms) {
        b.last_ms = now;  // a clock step back: no refill, no penalty
    }
    if (b.tokens < 1.0) {
        ++b.dropped;
        return t;
    }
    b.tokens -= 1.0;
    t.emit = true;
    t.report_dropped = b.dropped;
    b.dropped = 0;
    return t;
}

} // namespace mgscoop::log_budget
