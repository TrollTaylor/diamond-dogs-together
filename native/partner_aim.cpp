// Original MGS Co-op aim parity, pure part (docs/native-puppet.md "Facing";
// partner_aim.hpp).
#include "mgscoop/partner_aim.hpp"
#include <climits>
#include <cmath>
#include <cstring>

namespace mgscoop::partner_aim {
namespace {
constexpr double kPi = 3.14159265358979323846;
bool Finite3(const double (&v)[3]) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
} // namespace

const char* SourceName(Source source) noexcept {
    switch (source) {
    case Source::look: return "look";
    case Source::point: return "point";
    default: return "none";
    }
}

const char* ReasonName(Reason reason) noexcept {
    switch (reason) {
    case Reason::off: return "off";
    case Reason::inactive: return "inactive";
    case Reason::no_body: return "no_body";
    case Reason::no_look: return "no_look";
    case Reason::range: return "range";
    case Reason::online: return "online";
    case Reason::owner: return "owner";
    case Reason::q_vtable: return "q_vtable";
    case Reason::slots: return "slots";
    case Reason::count: return "count";
    case Reason::array: return "array";
    case Reason::record: return "record";
    case Reason::stale: return "stale";
    case Reason::epoch: return "epoch";
    case Reason::chain: return "chain";
    case Reason::shutdown: return "shutdown";
    case Reason::fault: return "fault";
    case Reason::torn: return "torn";
    default: return "none";
    }
}

void LookDir(double pitch, double yaw, double (&out)[3]) noexcept {
    const double cp = std::cos(pitch);
    out[0] = std::sin(yaw) * cp;
    out[1] = -std::sin(pitch);
    out[2] = std::cos(yaw) * cp;
}

bool DirAngles(const double (&d)[3], double& pitch, double& yaw) noexcept {
    pitch = yaw = 0.0;
    if (!Finite3(d)) return false;
    const double xz = std::sqrt(d[0] * d[0] + d[2] * d[2]);
    if (xz + std::fabs(d[1]) < 1e-9) return false;
    pitch = std::atan2(-d[1], xz);
    yaw = std::atan2(d[0], d[2]);
    return true;
}

bool CoordOk(const float (&p)[3]) noexcept {
    for (const float v : p)
        if (!std::isfinite(v) || std::fabs(static_cast<double>(v)) >= kMaxCoordM) return false;
    return true;
}

TargetOut Target(const TargetIn& in) noexcept {
    TargetOut out{};
    if (in.setting == PartnerNativeAimPt::off) { out.reason = Reason::off; return out; }
    if (!in.active) { out.reason = Reason::inactive; return out; }
    out.want = true;
    if (!in.body_ok) { out.reason = Reason::no_body; return out; }
    double dir[3]{};
    if (in.setting == PartnerNativeAimPt::point && in.point_ok) {
        const double d[3] = {in.point_dir[0], in.point_dir[1], in.point_dir[2]};
        const double n = Finite3(d) ? std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) : 0.0;
        if (!(n > 1e-6)) { out.reason = Reason::range; return out; }
        for (int i = 0; i < 3; ++i) dir[i] = d[i] / n;
        out.src = Source::point;
    } else {
        if (!in.look_ok || !std::isfinite(in.look_pitch) || !std::isfinite(in.look_yaw)) { out.reason = Reason::no_look; return out; }
        LookDir(in.look_pitch, in.look_yaw, dir);
        out.src = Source::look;
    }
    const double eye[3] = {in.body[0], in.body[1] + kEyeHeightM, in.body[2]};
    float p[3]{};
    for (int i = 0; i < 3; ++i) p[i] = static_cast<float>(eye[i] + kTargetDistM * dir[i]);
    // The range rule the hook trusts: finite, inside the coordinate bound and
    // 20..200 m from the body (the eye offset plus 100 m is about 101 m).
    double dist2 = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double d = static_cast<double>(p[i]) - in.body[i];
        dist2 += d * d;
    }
    const double dist = std::sqrt(dist2);
    if (!CoordOk(p) || !std::isfinite(dist) || dist < kMinRangeM || dist > kMaxRangeM) {
        out.src = Source::none;
        out.reason = Reason::range;
        return out;
    }
    std::memcpy(out.p, p, sizeof(p));
    out.valid = true;
    out.reason = Reason::none;
    return out;
}

bool TurnPredicate(float r60) noexcept {
    if (!std::isfinite(r60)) return false;
    // 0x10A0AE8..0x10A0B2B in float as the game computes it: (0.5 - r60) *
    // (pi/2) * 2.0, |.| > pi/2 - 1 deg * 10.
    const float half_pi = static_cast<float>(kPi / 2.0);
    const float x = std::fabs((0.5f - r60) * half_pi * 2.0f);
    const float limit = half_pi - static_cast<float>(kPi / 180.0) * 10.0f;
    return x > limit;
}

std::int32_t RelYawCdeg(float r60) noexcept {
    if (!std::isfinite(r60)) return INT32_MIN;
    const double v = (0.5 - static_cast<double>(r60)) * 18000.0;
    if (v > 1e9 || v < -1e9) return INT32_MIN;
    return static_cast<std::int32_t>(std::lround(v));
}

bool IsCommand(const char* text) noexcept {
    return text && std::strncmp(text, "aimpt", 5) == 0 && (text[5] == 0 || text[5] == ' ');
}

bool ParseCommand(const char* text, ParsedCommand& out) noexcept {
    out = ParsedCommand{Command::status, PartnerNativeAimPt::off};
    if (!IsCommand(text)) return false;
    const char* arg = text + 5;
    while (*arg == ' ') ++arg;
    if (!*arg || std::strcmp(arg, "status") == 0) return true;
    if (std::strcmp(arg, "lab") == 0) { out.kind = Command::clear; return true; }
    PartnerNativeAimPt value{};
    if (!ParsePartnerNativeAimPt(arg, std::strlen(arg), value)) return false;
    out.kind = Command::set;
    out.value = value;
    return true;
}

} // namespace mgscoop::partner_aim
