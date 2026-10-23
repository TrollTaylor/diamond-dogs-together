#pragma once
// Native puppet online (docs/native-puppet.md "Online"), pure part: the
// receiver's link-quality tracker that gates re-entry after a network
// fallback, the stable reason codes the capability exchange and
// link-status.json carry, and the "native.net" telemetry with its JSON
// writer. No Win32 calls and no game memory (mgs_coop_core).
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_native_net {

// --- Link quality ---------------------------------------------------------------
// Fed once per driver tick with the newest received input frame (its sequence
// and age; partner_net::LatestInput). The arrival time of a new newest frame is
// now - age; the gap to the previous newest frame is one inter-arrival sample.
// Good: the link is up, a frame is younger than gap_bad_ms, the last window_ms
// held no gap over gap_bad_ms and their p95 is below p95_max_ms.
struct QualityParams {
    std::uint32_t window_ms = 2000;
    std::uint32_t p95_max_ms = 120;
    std::uint32_t gap_bad_ms = 250;
};
inline constexpr std::size_t kArrivals = 128;
struct Quality {
    bool have;                      // a newest frame was seen
    std::uint32_t last_seq;
    std::uint64_t last_rx_ms;       // receive time of the newest frame
    std::uint64_t at_ms[kArrivals]; // receive time of each sample (ring)
    std::uint32_t gap_ms[kArrivals];
    std::uint32_t head, count;
    bool good;
    std::uint64_t good_since_ms;
    // Since the last Reset (the mission): inter-arrival gaps over 100, 250
    // and 1000 ms, the longest one, and the p50/p95 of the current window.
    std::uint32_t gaps_100, gaps_250, gaps_1000, max_gap_ms;
    std::uint32_t p50_ms, p95_ms;
    std::uint32_t samples;
};
struct QualityIn {
    std::uint64_t now_ms;
    bool link_live;
    bool valid;            // a newest frame exists
    std::uint32_t seq;
    std::uint32_t age_ms;
};
void Reset(Quality& quality) noexcept;
void Observe(Quality& quality, const QualityIn& in, const QualityParams& params = QualityParams{}) noexcept;
// How long the quality has been continuously good (0 while bad).
std::uint32_t GoodMs(const Quality& quality, std::uint64_t now_ms) noexcept;

// --- Reason codes ---------------------------------------------------------------
// The arbiter's static reasons as stable one-byte codes (index + 1 into the
// table; 0 none, 0xFF a reason outside the table). Codes travel in the
// capability exchange, so the table only ever grows at its end.
inline constexpr std::uint8_t kReasonNone = 0, kReasonUnknown = 0xFF;
std::uint8_t ReasonCode(const char* reason) noexcept;
// "" for kReasonNone, "other" for an unknown code.
const char* ReasonName(std::uint8_t code) noexcept;

// What a copy shows for one side's partner body (link-status.json
// partner_drive / remote_drive): off (the clip partner chosen: legacy drive),
// clips (the native drive configured, the clip fallback now), native
// (entering or active).
enum class Drive : std::uint8_t { unknown, off, clips, native };
const char* DriveName(Drive drive) noexcept;

// --- Telemetry (state.json "native.net") -----------------------------------------
struct NetTelemetry {
    char policy[8];          // lab | online
    char stall[8];           // neutral | hold
    char hold[4];            // own | net
    std::uint32_t stall_ms, edge_repeats;
    bool quality_good;
    std::uint32_t quality_good_ms;
    std::uint32_t gaps_100, gaps_250, gaps_1000, max_gap_ms, ia_p50_ms, ia_p95_ms;
    std::uint32_t net_leaves, hard_leaves, net_reentries, quiet_episodes, gap_snaps;
    bool remote_quiet, flap;
    char last_net_reason[16];
    // What this copy shows and what the remote reports (capability exchange).
    Drive local_drive;
    char local_reason[24];
    bool caps_rx;
    std::uint32_t caps_sent, caps_received, caps_invalid;
    // Refused for want of a link session or as an older revision, and the
    // receiver link session the stored capabilities arrived in (0 none).
    std::uint32_t caps_stale, caps_link_epoch;
    Drive remote_drive;
    char remote_reason[24];
    char remote_profile[8];
    char remote_policy[8];
    bool remote_v4;
    bool remote_caps_refused;
};
// The JSON object (no leading comma); 0 when it does not fit.
std::size_t FormatNetJson(const NetTelemetry& telemetry, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::partner_native_net
