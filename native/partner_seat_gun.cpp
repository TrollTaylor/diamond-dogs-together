// Original MGS Co-op native puppet: the partner's raised gun on the seated body 1 (partner_seat_gun.hpp), pure part.
#include "mgscoop/partner_seat_gun.hpp"

namespace mgscoop::partner_seat_gun {
namespace {
constexpr double kPi = 3.14159265358979323846;
double WrapPi(double a) noexcept {
    a = std::fmod(a + kPi, 2.0 * kPi);
    if (a < 0.0) a += 2.0 * kPi;
    return a - kPi;
}
float Clamp01(double v, bool& clamped) noexcept {
    if (v < 0.0) { clamped = true; return 0.0f; }
    if (v > 1.0) { clamped = true; return 1.0f; }
    return static_cast<float>(v);
}
} // namespace

const char* Gate(const GateIn& in) noexcept {
    if (!in.enabled) return "key_off";
    if (!in.installed) return "not_installed";
    if (in.uncertain) return "uncertain";
    if (!in.seated) return "not_seated";
    if (!in.passenger) return "driver";
    if (!in.realized) return "unrealized";
    if (in.block) return in.block;
    if (in.other_gesture) return "busy";
    return nullptr;
}

bool RemoteAiming(const AimIn& in) noexcept {
    return (in.pose_live && in.pose_aiming) || (in.input && in.input_age_ms <= kSampleStaleMs && in.input_aiming);
}

StepOut Step(State& s, const char* gate_refusal, bool aiming, std::uint64_t now_ms) noexcept {
    StepOut out{Edge::none, nullptr};
    s.refusal = gate_refusal;
    if (gate_refusal) {
        if (s.want) {
            s.want = false;
            ++s.releases;
            out = {Edge::release, "gate"};
        }
        return out;
    }
    if (aiming) s.last_aim_ms = now_ms;
    if (s.want) {
        // A backwards clock never releases: only a positive gap past the debounce does.
        if (!aiming && now_ms > s.last_aim_ms && now_ms - s.last_aim_ms >= kReleaseMs) {
            s.want = false;
            ++s.releases;
            out = {Edge::release, "aim"};
        }
        return out;
    }
    if (aiming && (s.retry_ms == 0 || now_ms >= s.retry_ms)) {
        s.want = true;
        s.retry_ms = 0;
        ++s.starts;
        out = {Edge::start, nullptr};
    }
    return out;
}

void HookEnded(State& s, std::uint64_t now_ms) noexcept {
    s.want = false;
    s.retry_ms = now_ms + kRetryMs;
}

RatesOut Rates(const RatesIn& in) noexcept {
    RatesOut o{};
    if (!in.dir_ok || !in.body_ok || !std::isfinite(in.pitch) || !std::isfinite(in.yaw) || !std::isfinite(in.body_yaw)) return o;
    const double deg = 180.0 / kPi;
    const double rel_yaw = WrapPi(in.yaw - in.body_yaw) * deg;
    const double rel_pitch = in.pitch * deg;
    // 0x108EC55: rel_pitch = (r5c - 0.5) * 2 * Rp, rel_yaw = (0.5 - r60) * 2 * Ry.
    bool clamped = false;
    o.r5c = Clamp01(0.5 + rel_pitch / (2.0 * kPitchRangeDeg), clamped);
    o.r60 = Clamp01(0.5 - rel_yaw / (2.0 * kYawRangeDeg), clamped);
    o.clamped = clamped;
    o.ok = RateOk(o.r5c) && RateOk(o.r60);
    return o;
}

} // namespace mgscoop::partner_seat_gun
