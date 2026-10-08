#pragma once
// Receiver-side partner facing (apply=xform/hybrid, net source). Pure, portable
// and deterministic: every time is passed in, nothing allocates or touches the
// game. vars.playerRotY (= S+0x20[0] yaw) is the control direction: on
// W -> W+A it steps 45 degrees in one tick while the body turns over about
// 250 ms (hidden run 442f6081...). The sender therefore publishes the local
// body-transform yaw and falls back to vars.playerRotY when that read fails,
// and this limiter is the receiver's safety net: a stepped target (the
// fallback, a late packet) turns at a bounded eased rate instead of snapping
// in one 33 ms lerp, while genuine body turns stay below the limits:
//   - ease-in: the turn rate grows by at most accel * dt per tick;
//   - ease-out: the rate never exceeds sqrt(2 * accel * |error|), so it brakes
//     onto the target without overshoot;
//   - a target that moves no faster than the limits is followed exactly (a
//     step that reaches the target lands on it and keeps the actual rate), so
//     smooth native turns and stationary pivots are unchanged.
// Position is never touched. Lifecycle discontinuities snap exactly.
// Yaw is radians with forward = (sin yaw, cos yaw) on (x, z); Fox is
// right-handed (y up), so increasing yaw turns left, seen from above.
#include <cmath>
#include <cstdint>

namespace mgscoop::partner_facing {

inline constexpr std::uint32_t kDefaultRateDps = 900;   // lab.ini partner_facing_dps (safety net)
inline constexpr std::uint32_t kMaxRateDps = 1440;
inline constexpr std::uint32_t kAccelPerRate = 20;      // full rate after 50 ms
inline constexpr std::uint64_t kMaxStepMs = 250;        // longer owner gaps snap
inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kDegPerRad = 57.2957795f;

enum class Snap : std::uint8_t { none, first, off, stale, teleport, relink, warp, unrealized, gap, clock };
inline const char* SnapName(Snap snap) noexcept {
    switch (snap) {
    case Snap::first: return "first";
    case Snap::off: return "off";
    case Snap::stale: return "stale";
    case Snap::teleport: return "teleport";
    case Snap::relink: return "relink";
    case Snap::warp: return "warp";
    case Snap::unrealized: return "unrealized";
    case Snap::gap: return "gap";
    case Snap::clock: return "clock";
    default: return "none";
    }
}

struct Params {
    float max_rate{};  // rad/s; 0 = exact target (off)
    float accel{};     // rad/s^2
};
inline Params ParamsForDps(std::uint32_t dps) noexcept {
    if (dps > kMaxRateDps) dps = kMaxRateDps;
    const float rate = static_cast<float>(dps) / kDegPerRad;
    return Params{rate, rate * static_cast<float>(kAccelPerRate)};
}

inline float Wrap(float radians) noexcept {
    if (!std::isfinite(radians)) return 0.0f;
    return std::remainder(radians, 2.0f * kPi);
}

struct State {
    bool valid{};
    float yaw{};          // applied, wrapped
    float rate{};         // rad/s, signed (positive turns left)
    std::uint64_t ms{};
    Snap last_snap{};
    std::uint32_t snaps{};
    std::uint32_t epoch{};  // caller's body epoch at the last step
};

// One owner tick. `snap` is the caller's lifecycle reason (teleport, relink,
// warp, unrealized, stale); none lets the smoother decide (first, off, gap,
// clock). Returns the applied yaw. A repeated tick time holds the yaw.
inline float Step(State& s, float target, std::uint64_t now_ms, const Params& p, Snap snap) noexcept {
    target = Wrap(target);
    if (snap == Snap::none) {
        if (!s.valid) snap = Snap::first;
        else if (!(p.max_rate > 0.0f) || !(p.accel > 0.0f)) snap = Snap::off;
        else if (now_ms < s.ms) snap = Snap::clock;
        else if (now_ms - s.ms > kMaxStepMs) snap = Snap::gap;
    }
    if (snap != Snap::none) {
        if (snap != Snap::off || s.last_snap != Snap::off) { s.last_snap = snap; ++s.snaps; }
        s.valid = true;
        s.yaw = target;
        s.rate = 0.0f;
        s.ms = now_ms;
        return target;
    }
    if (now_ms == s.ms) return s.yaw;
    const float dt = static_cast<float>(now_ms - s.ms) * 0.001f;
    s.ms = now_ms;
    const float error = Wrap(target - s.yaw);
    if (error == 0.0f) { s.rate = 0.0f; return s.yaw; }
    float want = std::sqrt(2.0f * p.accel * std::fabs(error));
    if (want > p.max_rate) want = p.max_rate;
    want = std::copysign(want, error);
    float rate = s.rate;
    const bool braking = rate * want > 0.0f && std::fabs(want) <= std::fabs(rate);
    if (braking) rate = want;  // the ease-out curve itself decelerates
    else {
        const float limit = p.accel * dt, delta = want - rate;
        rate += delta > limit ? limit : delta < -limit ? -limit : delta;
    }
    const float step = rate * dt;
    if (step * error > 0.0f && std::fabs(step) >= std::fabs(error)) {
        s.yaw = target;
        s.rate = error / dt;
        return s.yaw;
    }
    s.yaw = Wrap(s.yaw + step);
    s.rate = rate;
    return s.yaw;
}

// Per-tick runtime evidence for the worker capture: degrees, NaN when unread.
struct Sample {
    std::uint32_t ms{};
    float source_yaw{};       // local published yaw (vars.playerRotY)
    float source_body_yaw{};  // local body transform yaw (instance 0)
    float target_yaw{};       // interpolated remote yaw
    float applied_yaw{};      // yaw given to the partner body
    float body_yaw{};         // partner body readback (previous apply)
    std::uint8_t clip{0xFF}, move{0xFF};
    Snap snap{};
};
inline constexpr std::size_t kRing = 48;
struct Ring {
    Sample samples[kRing]{};
    std::uint8_t head{}, count{};  // head = next write
};
inline void Push(Ring& ring, const Sample& sample) noexcept {
    ring.samples[ring.head] = sample;
    ring.head = static_cast<std::uint8_t>((ring.head + 1) % kRing);
    if (ring.count < kRing) ++ring.count;
}
// i = 0 is the oldest retained sample.
inline const Sample& At(const Ring& ring, std::size_t i) noexcept {
    return ring.samples[(ring.head + kRing - ring.count + i) % kRing];
}

} // namespace mgscoop::partner_facing
