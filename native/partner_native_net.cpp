// Original MGS Co-op native puppet online: link quality, reason codes and the
// native.net telemetry (docs/native-puppet.md "Online").
#include "mgscoop/partner_native_net.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mgscoop::partner_native_net {
namespace {
// Append-only: the index + 1 is the wire code (capability exchange).
constexpr const char* kReasons[] = {
    "ok", "mission_start", "legacy", "configured", "unlatched", "cooldown", "settling", "dm_revert_failed", "defer_census",
    "not_installed", "hook_lost", "thread", "faulted", "terminating", "stage", "range_hidden", "unrealized", "parts", "link",
    "no_input", "input_stale", "defer_off", "lost", "wrap_in_batch", "drain_block", "unwindowed", "diverging", "runaway", "health",
    "cost", "enter_timeout_dm", "enter_timeout_place", "install_bytes", "install_slot", "install_changed", "install_protect",
    "install_allocate_near", "install_range", "install_alignment", "install_cache_line", "install_hook", "install_hook_disabled",
    "install_scope", "install_exchange", "install_unavailable", "install_disabled", "install_vt_copy", "install_wrap", "install_gate",
    "install_other", "net_input", "net_link", "net_quiet", "remote_quiet", "net_quality", "net_flap", "remote_caps", "warmup", "ride", "ride_stale", "air", "dead"};
constexpr std::size_t kReasonCount = sizeof(kReasons) / sizeof(kReasons[0]);
static_assert(kReasonCount < kReasonUnknown, "reason codes fit one byte");

struct Writer {
    char* out;
    std::size_t capacity, used;
    bool ok;
    void Add(const char* format, ...) noexcept {
        if (!ok) return;
        va_list args;
        va_start(args, format);
        const int n = std::vsnprintf(out + used, capacity - used, format, args);
        va_end(args);
        if (n < 0 || static_cast<std::size_t>(n) >= capacity - used) {
            ok = false;
            return;
        }
        used += static_cast<std::size_t>(n);
    }
    void Text(const char* s, std::size_t capacity_of_s) noexcept {
        Add("\"");
        for (std::size_t i = 0; s && i < capacity_of_s && s[i]; ++i) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c == '"' || c == '\\') Add("\\%c", c);
            else if (c < 32 || c > 126) Add("\\u%04x", static_cast<unsigned>(c));
            else Add("%c", c);
        }
        Add("\"");
    }
};
const char* Bool(bool value) noexcept { return value ? "true" : "false"; }
} // namespace

void Reset(Quality& q) noexcept { q = Quality{}; }

void Rebase(Quality& q) noexcept {
    q.have = false;
    q.last_seq = 0;
    q.last_rx_ms = 0;
    q.head = q.count = 0;
    q.p50_ms = q.p95_ms = 0;
    // good / good_since_ms stay (as 0.13.5 kept them up to the first frame after
    // the ride): a late or bad first frame, or a gap after it, still turns it
    // bad through Observe; a good one re-enters native after the settle alone.
}

void Observe(Quality& q, const QualityIn& in, const QualityParams& p) noexcept {
    const std::uint64_t now = in.now_ms;
    bool bad = !in.link_live || !in.valid || in.age_ms > p.gap_bad_ms;
    if (in.valid) {
        const std::uint64_t rx = now >= in.age_ms ? now - in.age_ms : 0;
        if (!q.have) {
            q.have = true;
            q.last_seq = in.seq;
            q.last_rx_ms = rx;
        } else if (in.seq != q.last_seq) {
            const std::uint64_t gap64 = rx >= q.last_rx_ms ? rx - q.last_rx_ms : 0;
            const std::uint32_t gap = gap64 > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(gap64);
            q.last_seq = in.seq;
            q.last_rx_ms = rx;
            q.head = (q.head + 1) % kArrivals;
            q.at_ms[q.head] = rx;
            q.gap_ms[q.head] = gap;
            if (q.count < kArrivals) ++q.count;
            ++q.samples;
            if (gap > 100) ++q.gaps_100;
            if (gap > 250) ++q.gaps_250;
            if (gap > 1000) ++q.gaps_1000;
            if (gap > q.max_gap_ms) q.max_gap_ms = gap;
            // A gap that just ended is a bad moment even though the frame is fresh.
            if (gap > p.gap_bad_ms) bad = true;
        }
    }
    // The window's percentiles (at most kArrivals samples, insertion sort).
    std::uint32_t window[kArrivals];
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < q.count; ++i) {
        const std::uint32_t at = (q.head + kArrivals - i) % kArrivals;
        if (now >= q.at_ms[at] && now - q.at_ms[at] > p.window_ms) break;
        std::uint32_t v = q.gap_ms[at];
        std::uint32_t k = n++;
        while (k && window[k - 1] > v) {
            window[k] = window[k - 1];
            --k;
        }
        window[k] = v;
    }
    q.p50_ms = n ? window[(n - 1) / 2] : 0;
    q.p95_ms = n ? window[(n * 95 + 99) / 100 - 1] : 0;
    if (n && (q.p95_ms >= p.p95_max_ms || window[n - 1] > p.gap_bad_ms)) bad = true;
    if (bad) {
        q.good = false;
    } else if (!q.good) {
        q.good = true;
        q.good_since_ms = now;
    }
}

std::uint32_t GoodMs(const Quality& q, std::uint64_t now_ms) noexcept {
    if (!q.good || now_ms < q.good_since_ms) return 0;
    const std::uint64_t ms = now_ms - q.good_since_ms;
    return ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(ms);
}

std::uint8_t ReasonCode(const char* reason) noexcept {
    if (!reason || !reason[0]) return kReasonNone;
    for (std::size_t i = 0; i < kReasonCount; ++i)
        if (std::strcmp(reason, kReasons[i]) == 0) return static_cast<std::uint8_t>(i + 1);
    return kReasonUnknown;
}

const char* ReasonName(std::uint8_t code) noexcept {
    if (code == kReasonNone) return "";
    if (code > kReasonCount) return "other";
    return kReasons[code - 1];
}

const char* DriveName(Drive drive) noexcept {
    switch (drive) {
    case Drive::off: return "off";
    case Drive::clips: return "clips";
    case Drive::native: return "native";
    default: return "unknown";
    }
}

std::size_t FormatNetJson(const NetTelemetry& t, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    Writer w{out, capacity, 0, true};
    w.Add("{\"policy\":");
    w.Text(t.policy, sizeof(t.policy));
    w.Add(",\"stall\":");
    w.Text(t.stall, sizeof(t.stall));
    w.Add(",\"stall_ms\":%u,\"hold\":", t.stall_ms);
    w.Text(t.hold, sizeof(t.hold));
    w.Add(",\"edge_repeats\":%u,\"quality_good\":%s,\"quality_good_ms\":%u,", t.edge_repeats, Bool(t.quality_good), t.quality_good_ms);
    w.Add("\"gaps_100\":%u,\"gaps_250\":%u,\"gaps_1000\":%u,\"max_gap_ms\":%u,\"ia_p50_ms\":%u,\"ia_p95_ms\":%u,", t.gaps_100, t.gaps_250,
          t.gaps_1000, t.max_gap_ms, t.ia_p50_ms, t.ia_p95_ms);
    w.Add("\"net_leaves\":%u,\"hard_leaves\":%u,\"net_reentries\":%u,\"quiet_episodes\":%u,\"gap_snaps\":%u,\"remote_quiet\":%s,\"flap\":%s,",
          t.net_leaves, t.hard_leaves, t.net_reentries, t.quiet_episodes, t.gap_snaps, Bool(t.remote_quiet), Bool(t.flap));
    w.Add("\"last_net_reason\":");
    w.Text(t.last_net_reason, sizeof(t.last_net_reason));
    w.Add(",\"local_drive\":\"%s\",\"local_reason\":", DriveName(t.local_drive));
    w.Text(t.local_reason, sizeof(t.local_reason));
    w.Add(",\"caps\":{\"rx\":%s,\"sent\":%u,\"received\":%u,\"invalid\":%u,\"stale\":%u,\"link_epoch\":%u,\"remote_drive\":\"%s\","
          "\"remote_reason\":",
          Bool(t.caps_rx), t.caps_sent, t.caps_received, t.caps_invalid, t.caps_stale, t.caps_link_epoch, DriveName(t.remote_drive));
    w.Text(t.remote_reason, sizeof(t.remote_reason));
    w.Add(",\"remote_profile\":");
    w.Text(t.remote_profile, sizeof(t.remote_profile));
    w.Add(",\"remote_policy\":");
    w.Text(t.remote_policy, sizeof(t.remote_policy));
    w.Add(",\"remote_v4\":%s,\"refused\":%s}}", Bool(t.remote_v4), Bool(t.remote_caps_refused));
    if (!w.ok) {
        out[0] = 0;
        return 0;
    }
    return w.used;
}

} // namespace mgscoop::partner_native_net
