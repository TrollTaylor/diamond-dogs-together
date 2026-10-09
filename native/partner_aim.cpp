// Original MGS Co-op aim parity, pure part (docs/native-puppet.md "Facing";
// partner_aim.hpp).
#include "mgscoop/partner_aim.hpp"
#include <climits>
#include <cmath>
#include <cstring>
#include "partner_player_contract.hpp"

namespace mgscoop::partner_aim {
namespace {
constexpr double kPi = 3.14159265358979323846;
namespace c = mgscoop::partner_player_contract;
bool Finite3(const double (&v)[3]) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
double WrapPi(double a) noexcept {
    a = std::remainder(a, 2.0 * kPi);
    return a <= -kPi ? a + 2.0 * kPi : a;
}
static_assert(c::gun_class == 0x3d4 && c::gun_stance == 0x3eb && c::gun3c0_camera_rel == 0x2000 && c::gun_b3d5_sight == 1,
              "the gun record fields GunDir reads (tools/partner_aim_sites.py)");
static_assert(c::pivot_stand_m == static_cast<float>(kPivotStandM) && c::pivot_crouch_m == static_cast<float>(kPivotCrouchM),
              "the pivot heights match 0x10499D0 kind 0");
} // namespace

double PivotHeight(std::uint8_t stance) noexcept {
    switch (stance) {
    case 2: return kPivotCrouchM;
    case 3: return kPivotProneM;
    default: return kPivotStandM;
    }
}

const char* DirReasonName(DirReason reason) noexcept {
    switch (reason) {
    case DirReason::idle: return "idle";
    case DirReason::unread: return "unread";
    case DirReason::camera: return "camera";
    case DirReason::sight: return "sight";
    case DirReason::gun_class: return "class";
    case DirReason::stance: return "stance";
    case DirReason::range: return "range";
    case DirReason::body: return "body";
    case DirReason::convention: return "convention";
    case DirReason::prone_frame: return "prone_frame";
    default: return "none";
    }
}

GunDirOut GunDir(const GunDirIn& in) noexcept {
    GunDirOut o{};
    if (!in.gun_ok || !in.rate_ok) { o.reason = DirReason::unread; return o; }
    if (in.gun3c0 & c::gun3c0_camera_rel) { o.reason = DirReason::camera; return o; }
    if (in.b3d5 == c::gun_b3d5_sight) { o.reason = DirReason::sight; return o; }
    if (in.gun_class == c::gun_class_other_path) { o.reason = DirReason::gun_class; return o; }
    if (in.stance < 1 || in.stance > 3) { o.reason = DirReason::stance; return o; }
    if (!std::isfinite(in.r5c) || !std::isfinite(in.r60) || in.r5c < 0.0f || in.r5c > 1.0f || in.r60 < 0.0f || in.r60 > 1.0f) {
        o.reason = DirReason::range;
        return o;
    }
    if (!in.body_ok || !std::isfinite(in.body_yaw)) { o.reason = DirReason::body; return o; }
    // 0x1043C80 composes a separate prone orientation before 0x1042F80
    // measures relative aim. That plugin frame is not a qualified read yet;
    // body yaw alone cannot turn prone rates into a world direction.
    if (in.stance == c::gun_stance_prone) { o.reason = DirReason::prone_frame; return o; }
    const double r5c = in.r5c, r60 = in.r60, deg = kPi / 180.0;
    // 0x108EC55: rec+0x344 = (r5c - 0.5) * Rp * 2, rec+0x348 = (0.5 - r60) * Ry * 2 by class.
    const unsigned row = in.gun_class == c::gun_class_narrow ? 1u : in.gun_class == c::gun_class_wide ? 2u : 0u;
    o.rel_pitch = (r5c - 0.5) * 2.0 * c::aim_range_deg[row][0] * deg;
    o.rel_yaw = (0.5 - r60) * 2.0 * c::aim_range_deg[row][1] * deg;
    o.pitch = o.rel_pitch;
    o.yaw = WrapPi(static_cast<double>(in.body_yaw) + o.rel_yaw);
    o.ok = std::isfinite(o.pitch) && std::isfinite(o.yaw);
    o.reason = o.ok ? DirReason::none : DirReason::range;
    return o;
}

std::int16_t PitchCdeg(double radians) noexcept {
    if (!std::isfinite(radians)) return 0;
    const double v = radians * 18000.0 / kPi;
    return static_cast<std::int16_t>(std::lround(v > kMaxPitchCdeg ? kMaxPitchCdeg : v < -kMaxPitchCdeg ? -kMaxPitchCdeg : v));
}

std::int16_t YawCdeg(double radians) noexcept {
    if (!std::isfinite(radians)) return 0;
    long v = std::lround(WrapPi(radians) * 18000.0 / kPi);
    if (v >= 18000) v -= 36000;
    if (v < -18000) v += 36000;
    return static_cast<std::int16_t>(v);
}

std::int32_t AngleCdeg(double pitch_a, double yaw_a, double pitch_b, double yaw_b) noexcept {
    if (!std::isfinite(pitch_a) || !std::isfinite(yaw_a) || !std::isfinite(pitch_b) || !std::isfinite(yaw_b)) return INT32_MIN;
    double a[3]{}, b[3]{};
    LookDir(pitch_a, yaw_a, a);
    LookDir(pitch_b, yaw_b, b);
    double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    dot = dot > 1.0 ? 1.0 : dot < -1.0 ? -1.0 : dot;
    return static_cast<std::int32_t>(std::lround(std::acos(dot) * 18000.0 / kPi));
}

CheckResult CheckStep(DirCheck& check, const GunDirOut& dir, const GunDirIn& in, const float (&body)[3], const float (&aimpt)[3]) noexcept {
    if (check.latched) return CheckResult::latched;
    if (!dir.ok) return CheckResult::skipped;
    // The reference: from the pivot (stand/crouch: up; prone: 0.5 m ahead along the body yaw) to the aim point.
    const double ahead = in.stance == c::gun_stance_prone ? static_cast<double>(c::pivot_prone_ahead_m) : 0.0;
    const double origin[3] = {body[0] + ahead * std::sin(in.body_yaw), body[1] + PivotHeight(in.stance),
                              body[2] + ahead * std::cos(in.body_yaw)};
    const double v[3] = {aimpt[0] - origin[0], aimpt[1] - origin[1], aimpt[2] - origin[2]};
    const double reach[3] = {static_cast<double>(aimpt[0]) - body[0], static_cast<double>(aimpt[1]) - body[1],
                             static_cast<double>(aimpt[2]) - body[2]};
    double ref_pitch = 0.0, ref_yaw = 0.0;
    const double dist = std::sqrt(reach[0] * reach[0] + reach[1] * reach[1] + reach[2] * reach[2]);
    if (!std::isfinite(dist) || dist < kCheckMinM || !DirAngles(v, ref_pitch, ref_yaw)) {
        check.have_prev = false;
        return CheckResult::skipped;
    }
    const bool settled = check.have_prev && AngleCdeg(ref_pitch, ref_yaw, check.prev_pitch, check.prev_yaw) < kCheckSettleCdeg;
    check.have_prev = true;
    check.prev_pitch = ref_pitch;
    check.prev_yaw = ref_yaw;
    const bool inside = in.r5c > kCheckEdge && in.r5c < 1.0 - kCheckEdge && in.r60 > kCheckEdge && in.r60 < 1.0 - kCheckEdge;
    if (!settled || !inside) return CheckResult::skipped;
    const std::int32_t err = AngleCdeg(dir.pitch, dir.yaw, ref_pitch, ref_yaw);
    ++check.checks;
    check.last_err_cdeg = err;
    if (err != INT32_MIN && err <= kCheckTolCdeg) {
        check.streak = 0;
        return CheckResult::hit;
    }
    ++check.misses;
    if (++check.streak >= kCheckLatch) {
        check.latched = true;
        return CheckResult::latched;
    }
    return CheckResult::miss;
}

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
    // Body stance can lag the sender, and older experimental v6 senders may
    // publish an unqualified prone direction. Keep the existing look fallback.
    if (in.setting == PartnerNativeAimPt::point && in.stance != c::gun_stance_prone && in.point_ok &&
        std::isfinite(in.point_pitch) && std::isfinite(in.point_yaw)) {
        LookDir(in.point_pitch, in.point_yaw, dir);
        out.src = Source::point;
    } else {
        if (!in.look_ok || !std::isfinite(in.look_pitch) || !std::isfinite(in.look_yaw)) { out.reason = Reason::no_look; return out; }
        LookDir(in.look_pitch, in.look_yaw, dir);
        out.src = Source::look;
    }
    if (!Finite3(dir)) { out.src = Source::none; out.reason = Reason::range; return out; }
    const double origin[3] = {in.body[0], in.body[1] + PivotHeight(in.stance), in.body[2]};
    float p[3]{};
    for (int i = 0; i < 3; ++i) p[i] = static_cast<float>(origin[i] + kTargetDistM * dir[i]);
    // The range rule the hook trusts: finite, inside the coordinate bound and
    // 20..200 m from the body (the pivot offset plus 100 m is about 100 m).
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
