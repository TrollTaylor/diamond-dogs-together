// 0.13.6 bounded log budgets (log_budget): the rotating per-family cap of the
// partner_recon / partner_body_safety families and the world LOG token bucket.
// The 0.13.5 caps were mission-scoped (64 lines, then silence for the rest of
// the mission) and 200 per fixed 10 s window; these cases fail against them.
#include "mgscoop/log_budget.hpp"
#include <cstdio>
#include <cstdlib>

namespace lb = mgscoop::log_budget;

namespace {
unsigned g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::fprintf(stderr, "log_budget_tests failed: %s\n", what);
        std::exit(1);
    }
}

void RotatingCases() {
    const lb::Limits limits{64, 120000, 1024};
    lb::Rotating st{};
    std::uint64_t t = 1000;
    for (unsigned i = 0; i < 64; ++i) Check(lb::Admit(st, limits, t + i).verdict == lb::Verdict::emit, "first 64 lines emitted");
    Check(lb::Admit(st, limits, t + 100).verdict == lb::Verdict::drop_capped, "65th: the capped line once");
    for (unsigned i = 0; i < 10; ++i) Check(lb::Admit(st, limits, t + 200 + i).verdict == lb::Verdict::drop, "then silent");
    // The session's failure: a carry_hold line ~5 min later was lost for good. Now a later window opens.
    const auto d = lb::Admit(st, limits, t + 120000);
    Check(d.verdict == lb::Verdict::emit_resumed && d.dropped == 11, "the next window resumes with the dropped count");
    Check(lb::Admit(st, limits, t + 120001).verdict == lb::Verdict::emit, "then plain lines");
    // A window that never fills drops nothing.
    lb::Rotating quiet{};
    for (unsigned w = 0; w < 30; ++w)
        for (unsigned i = 0; i < 10; ++i) Check(lb::Admit(quiet, limits, w * 120000ull + i).verdict == lb::Verdict::emit, "quiet family");
    Check(quiet.total == 300 && quiet.dropped == 0, "counts");
    // The mission bound: never more than per_mission lines, then one final capped line and silence.
    lb::Rotating busy{};
    unsigned emitted = 0, closed = 0, capped = 0;
    for (unsigned w = 0; w < 40; ++w)
        for (unsigned i = 0; i < 100; ++i) {
            const auto v = lb::Admit(busy, limits, w * 120000ull + i).verdict;
            if (v == lb::Verdict::emit || v == lb::Verdict::emit_resumed) ++emitted;
            if (v == lb::Verdict::drop_closed) ++closed;
            if (v == lb::Verdict::drop_capped) ++capped;
        }
    // 16 full windows emit 1024; the first 15 end with a window capped line, the 16th with the final one.
    Check(emitted == 1024 && closed == 1 && capped == 15, "1024 lines, one final capped line, one capped line per full window");
    Check(lb::Admit(busy, limits, 99999999).verdict == lb::Verdict::drop, "closed for the mission");
    // A clock step back starts a new window (no stuck family).
    lb::Rotating back{};
    for (unsigned i = 0; i < 65; ++i) lb::Admit(back, limits, 500000 + i);
    Check(lb::Admit(back, limits, 1000).verdict == lb::Verdict::emit_resumed, "clock step back: new window");
    // Mission reset: a value-initialized state starts fresh.
    busy = lb::Rotating{};
    Check(lb::Admit(busy, limits, 5).verdict == lb::Verdict::emit, "reset");
}

void BucketCases() {
    // The 30010 arrival burst (F:13250..13598): ~250 lines in under 10 s. 0.13.5 dropped 52.
    lb::Bucket b{};
    unsigned emitted = 0;
    for (unsigned i = 0; i < 252; ++i) emitted += lb::TakeToken(b, 600, 20, 1000 + i * 30).emit ? 1u : 0u;
    Check(emitted == 252, "an arrival burst of 252 lines in 7.5 s passes");
    // Steady state stays bounded: a flood of 100 lines/s for 60 s emits at most burst + 20/s.
    lb::Bucket f{};
    emitted = 0;
    std::uint32_t reported = 0;
    for (unsigned i = 0; i < 6000; ++i) {
        const auto take = lb::TakeToken(f, 600, 20, i * 10ull);
        if (take.emit) ++emitted;
        reported += take.report_dropped;
    }
    Check(emitted <= 600 + 60 * 20 + 1 && emitted >= 600 + 59 * 20, "flood bounded by burst + refill");
    Check(reported + f.dropped == 6000 - emitted, "every dropped line is reported once");
    // The first line after drops reports them; the next does not.
    lb::Bucket g{};
    for (unsigned i = 0; i < 600; ++i) lb::TakeToken(g, 600, 20, 0);
    Check(!lb::TakeToken(g, 600, 20, 0).emit && !lb::TakeToken(g, 600, 20, 0).emit, "empty: dropped");
    auto take = lb::TakeToken(g, 600, 20, 100);
    Check(take.emit && take.report_dropped == 2, "refilled after 50 ms: emitted with the dropped count");
    take = lb::TakeToken(g, 600, 20, 200);
    Check(take.emit && take.report_dropped == 0, "then plain");
    // A clock step back neither refills nor locks.
    lb::Bucket c{};
    lb::TakeToken(c, 2, 20, 10000);
    lb::TakeToken(c, 2, 20, 10000);
    Check(!lb::TakeToken(c, 2, 20, 5000).emit, "step back: no refill");
    Check(lb::TakeToken(c, 2, 20, 5050).emit, "refills from the new clock");
}
} // namespace

int main() {
    RotatingCases();
    BucketCases();
    std::printf("log_budget_tests: %u passed\n", g_checks);
    return 0;
}
