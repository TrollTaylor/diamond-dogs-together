// Original MGS Co-op offline partner spike receipt helpers (pure, portable).
#include "mgscoop/partner_receipt.hpp"
#include <cmath>
#include <cstring>

namespace mgscoop::partner {
namespace {
constexpr double kPi = 3.14159265358979323846;

template <std::size_t N>
bool Exact(const char* source, std::size_t size, const char (&literal)[N]) noexcept {
    return size == N - 1 && std::memcmp(source, literal, N - 1) == 0;
}
template <std::size_t N>
bool Prefixed(const char* source, std::size_t size, const char (&prefix)[N]) noexcept {
    return size > N - 1 && std::memcmp(source, prefix, N - 1) == 0;
}
bool Digit(char c) noexcept { return c >= '0' && c <= '9'; }
bool PayloadChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || Digit(c) ||
           c == '_' || c == '=' || c == ':' || c == ',' || c == '.' || c == ';' || c == '-';
}
bool Blank(char c) noexcept { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// Appends text; false when the output (plus terminator) would not fit.
bool Append(char* out, std::size_t capacity, std::size_t& at, const char* text, std::size_t length) noexcept {
    if (length >= capacity - at) return false;
    std::memcpy(out + at, text, length);
    at += length;
    out[at] = 0;
    return true;
}
bool AppendInt(char* out, std::size_t capacity, std::size_t& at, std::int64_t value) noexcept {
    char digits[24]{};
    std::size_t n = sizeof(digits);
    const bool negative = value < 0;
    std::uint64_t magnitude = negative ? 0 - static_cast<std::uint64_t>(value) : static_cast<std::uint64_t>(value);
    do { digits[--n] = static_cast<char>('0' + magnitude % 10); magnitude /= 10; } while (magnitude && n > 1);
    if (negative) digits[--n] = '-';
    return Append(out, capacity, at, digits + n, sizeof(digits) - n);
}

// One strict signed decimal integer; advances p on success.
bool ParseInt(const char*& p, const char* end, std::int64_t min, std::int64_t max, std::int32_t& out) noexcept {
    const char* q = p;
    bool negative = false;
    if (q < end && *q == '-') { negative = true; ++q; }
    if (q >= end || !Digit(*q)) return false;
    if (*q == '0' && q + 1 < end && Digit(q[1])) return false;  // leading zero
    std::int64_t value = 0;
    unsigned digits = 0;
    while (q < end && Digit(*q)) {
        if (++digits > 10) return false;
        value = value * 10 + (*q - '0');
        ++q;
    }
    if (negative && value == 0) return false;  // "-0"
    if (negative) value = -value;
    if (value < min || value > max) return false;
    out = static_cast<std::int32_t>(value);
    p = q;
    return true;
}

std::int32_t ClampMm(double value) noexcept {
    if (!std::isfinite(value)) return 0;
    if (value > max_pos_mm) return max_pos_mm;
    if (value < -max_pos_mm) return -max_pos_mm;
    return static_cast<std::int32_t>(std::llround(value));
}

bool EqualsToken(const char* token, std::size_t size, const char* literal) noexcept {
    const std::size_t length = std::strlen(literal);
    return size == length && std::memcmp(token, literal, length) == 0;
}
bool StartsToken(const char* token, std::size_t size, const char* prefix, std::size_t& value_at) noexcept {
    const std::size_t length = std::strlen(prefix);
    if (size < length || std::memcmp(token, prefix, length) != 0) return false;
    value_at = length;
    return true;
}
} // namespace

Receipt Classify(const char* source, std::size_t size, const char* name) noexcept {
    if (!source || !name || size == 0 || size > max_receipt) return Receipt::none;
    // Byte-wise so a shorter name is never read past its terminator; receipts
    // never contain NUL, so a terminator in either string is a mismatch.
    for (std::size_t i = 0; i < size; ++i)
        if (source[i] == 0 || name[i] != source[i]) return Receipt::none;
    if (name[size] != 0) return Receipt::none;
    if (Exact(source, size, cfg_source)) return Receipt::cfg;
    if (Exact(source, size, stop_source)) return Receipt::stop;
    if (Prefixed(source, size, tick_prefix)) return Receipt::tick;
    if (Prefixed(source, size, log_prefix)) return Receipt::log;
    return Receipt::none;
}

bool ParseTick(const char* source, std::size_t size, Tick& out) noexcept {
    constexpr std::size_t skip = sizeof(tick_prefix) - 1;
    if (!source || size <= skip || size > max_receipt || std::memcmp(source, tick_prefix, skip) != 0) return false;
    const char* p = source + skip;
    const char* end = source + size;
    struct Bound { std::int64_t min, max; };
    static constexpr Bound bounds[9] = {
        {0, max_frame}, {-max_pos_mm, max_pos_mm}, {-max_pos_mm, max_pos_mm}, {-max_pos_mm, max_pos_mm},
        {-max_angle_cdeg, max_angle_cdeg}, {-max_angle_cdeg, max_angle_cdeg}, {0, 2}, {0, 3}, {0, 3}};
    std::int32_t values[9]{};
    for (unsigned i = 0; i < 9; ++i) {
        if (i) {
            if (p >= end || *p != ' ') return false;
            ++p;
        }
        if (!ParseInt(p, end, bounds[i].min, bounds[i].max, values[i])) return false;
    }
    if (p != end) return false;
    out.frame = values[0];
    out.x_mm = values[1]; out.y_mm = values[2]; out.z_mm = values[3];
    out.yaw_cdeg = values[4]; out.cam_cdeg = values[5];
    out.stance = static_cast<std::uint8_t>(values[6]);
    out.move = static_cast<std::uint8_t>(values[7]);
    out.luaflags = static_cast<std::uint8_t>(values[8]);
    return true;
}

std::size_t FormatReply(const Reply& reply, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    std::size_t at = 0;
    const std::int64_t values[13] = {reply.op, reply.index, reply.x_mm, reply.y_mm, reply.z_mm, reply.yaw_mrad,
                                     static_cast<std::int64_t>(reply.status), reply.age_ms, reply.look.type, reply.look.parts,
                                     reply.look.camo, reply.look.hand, reply.look.face};
    const unsigned count = (reply.op & op::set_parts_explicit) ? 13u : 8u;
    bool ok = Append(out, capacity, at, "return ", 7);
    for (unsigned i = 0; ok && i < count; ++i) ok = (i == 0 || Append(out, capacity, at, ",", 1)) && AppendInt(out, capacity, at, values[i]);
    if (!ok) { out[0] = 0; return 0; }
    return at;
}

const char* NormalizeLook(const Look& in, Look& out, bool& avatar_as_snake) noexcept {
    if (in.type > 6 || in.type == 4) return "type";
    if (in.parts > 27) return "parts";
    if (in.camo > 116) return "camo";
    if (in.hand > 7) return "hand";
    Look look = in;
    bool avatar = false;
    if (look.type == 6) look = quiet_look;
    else if (look.type == 5) look = Look{5, 26, 115, 0, 0, 0};
    else if (look.type == 3) { look.type = 0; avatar = true; }
    out = look;
    avatar_as_snake = avatar;
    return nullptr;
}

std::size_t SanitizeLogPayload(const char* payload, std::size_t size, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    if (!payload || size == 0 || size > max_log_payload || size >= capacity) return 0;
    for (std::size_t i = 0; i < size; ++i) out[i] = PayloadChar(payload[i]) ? payload[i] : '_';
    out[size] = 0;
    return size;
}

std::size_t FormatLogLine(const char* source, std::size_t size, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    constexpr std::size_t skip = sizeof(log_prefix) - 1;
    if (!source || size <= skip || size > max_receipt || std::memcmp(source, log_prefix, skip) != 0) return 0;
    char clean[max_log_payload + 1]{};
    const std::size_t length = SanitizeLogPayload(source + skip, size - skip, clean, sizeof(clean));
    if (!length) return 0;
    std::size_t at = 0;
    bool ok = Append(out, capacity, at, "partner_lua=", 12);
    for (std::size_t i = 0; ok && i < length; ++i)
        ok = clean[i] == ';' ? Append(out, capacity, at, "; ", 2) : Append(out, capacity, at, clean + i, 1);
    if (ok) ok = Append(out, capacity, at, "\r\n", 2);
    if (!ok) { out[0] = 0; return 0; }
    return at;
}

bool ParseTrigger(const char* text, std::size_t size, Trigger& out) noexcept {
    if (!text || size == 0 || size > max_trigger) return false;
    std::size_t i = 0;
    if (size >= 3 && static_cast<unsigned char>(text[0]) == 0xef && static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) i = 3;
    Trigger result{};
    result.source = PartnerSource::fixed;
    result.apply = PartnerApply::warp;
    unsigned tokens = 0;
    while (i < size) {
        while (i < size && Blank(text[i])) ++i;
        if (i >= size) break;
        const std::size_t start = i;
        while (i < size && !Blank(text[i])) {
            const auto c = static_cast<unsigned char>(text[i]);
            if (c < 0x21 || c > 0x7e) return false;
            ++i;
        }
        const char* token = text + start;
        const std::size_t length = i - start;
        ++tokens;
        std::size_t value_at = 0;
        if (EqualsToken(token, length, "stop")) {
            if (result.stop) return false;
            result.stop = true;
        } else if (StartsToken(token, length, "stage=", value_at)) {
            if (result.has_stage || length != value_at + 1 || token[value_at] < '0' || token[value_at] > '3') return false;
            result.has_stage = true;
            result.stage = static_cast<std::uint32_t>(token[value_at] - '0');
        } else if (StartsToken(token, length, "source=", value_at)) {
            if (result.has_source || !ParsePartnerSource(token + value_at, length - value_at, result.source)) return false;
            result.has_source = true;
        } else if (StartsToken(token, length, "apply=", value_at) || StartsToken(token, length, "move=", value_at)) {
            if (result.has_apply || !ParsePartnerApply(token + value_at, length - value_at, result.apply)) return false;
            result.has_apply = true;
        } else if (StartsToken(token, length, "anim=", value_at)) {
            if (result.has_anim || !ParsePartnerAnim(token + value_at, length - value_at, result.anim)) return false;
            result.has_anim = true;
        } else if (StartsToken(token, length, "aset=", value_at)) {
            if (result.has_aset || !ParsePartnerAset(token + value_at, length - value_at, result.aset)) return false;
            result.has_aset = true;
        } else if (StartsToken(token, length, "reset=", value_at)) {
            if (result.has_reset || !ParsePartnerReset(token + value_at, length - value_at, result.reset)) return false;
            result.has_reset = true;
        } else if (StartsToken(token, length, "look=", value_at)) {
            if (result.has_look || !ParsePartnerLook(token + value_at, length - value_at, result.look)) return false;
            result.has_look = true;
        } else if (EqualsToken(token, length, "probe=ps") || EqualsToken(token, length, "probe=off")) {
            if (result.has_probe) return false;
            result.has_probe = true;
            result.probe_ps = length == 8;  // "probe=ps"
        } else if (EqualsToken(token, length, "census=1")) {
            if (result.census) return false;
            result.census = true;
        } else if (EqualsToken(token, length, "parts=explicit")) {
            if (result.parts_explicit) return false;
            result.parts_explicit = true;
        } else if (EqualsToken(token, length, "mirror=on")) {
            if (result.mirror_on) return false;
            result.mirror_on = true;
        } else {
            return false;
        }
    }
    if (tokens == 0 || (result.stop && tokens != 1)) return false;
    out = result;
    return true;
}

std::uint32_t OpsFor(const OpInput& input) noexcept {
    std::uint32_t ops = 0;
    const bool warp_driver = input.apply == PartnerApply::warp || input.apply == PartnerApply::both;
    const bool transform_driver = PartnerApplyIsTransform(input.apply);
    if (input.stage >= 1 && input.pose_valid && (warp_driver || (transform_driver && input.transform_warp))) ops |= op::warp;
    if (input.stage >= 2) {
        if (!(input.luaflags & luaflag::all_active) || input.reassert_all_active) ops |= op::set_all_active;
    } else if (input.luaflags & luaflag::all_active) {
        ops |= op::clear_all_active;
    }
    if (input.stage >= 3 && !input.no_copy) ops |= op::copy_parts;
    if (input.stage >= 1 && input.parts_explicit) ops |= op::set_parts_explicit;
    if (input.stage >= 1 && input.mirror_on) ops |= op::mirror_on;
    if (input.mirror_off) ops |= op::mirror_off;
    return ops;
}

bool WritesSlot(std::uint32_t stage, PartnerApply apply, bool pose_valid) noexcept {
    return stage >= 1 && apply != PartnerApply::warp && pose_valid;
}

void YawToQuat(float yaw, float (&quat)[4]) noexcept {
    const double half = std::isfinite(yaw) ? static_cast<double>(yaw) * 0.5 : 0.0;
    quat[0] = 0.0f;
    quat[1] = static_cast<float>(std::sin(half));
    quat[2] = 0.0f;
    quat[3] = static_cast<float>(std::cos(half));
}

float QuatToYaw(const float (&quat)[4]) noexcept {
    // v' = v + 2w(u x v) + 2 u x (u x v), u = (x, y, z), v = (0, 0, 1).
    const double x = quat[0], y = quat[1], z = quat[2], w = quat[3];
    const double rx = 2.0 * (x * z + w * y);
    const double rz = 1.0 - 2.0 * (x * x + y * y);
    if (!std::isfinite(rx) || !std::isfinite(rz)) return 0.0f;
    return static_cast<float>(std::atan2(rx, rz));
}

std::uint8_t DebounceStance(StanceDebounce& state, std::uint8_t stance, std::uint64_t now_ms, std::uint32_t hold_ms) noexcept {
    if (stance < 1 || stance > 3) {
        state.candidate = 0;
        return 0;
    }
    if (stance == state.applied) {
        state.candidate = 0;
        return 0;
    }
    if (stance != state.candidate) {
        state.candidate = stance;
        state.since_ms = now_ms;
    }
    if (now_ms - state.since_ms < hold_ms) return 0;
    state.applied = stance;
    state.candidate = 0;
    return stance;
}

namespace {
// Normalizes in double before rounding, so +-pi stays +-3142 mrad.
std::int32_t MradFromRadians(double radians) noexcept {
    if (!std::isfinite(radians)) return 0;
    radians = std::fmod(radians + kPi, 2.0 * kPi);
    if (radians < 0) radians += 2.0 * kPi;
    return static_cast<std::int32_t>(std::llround((radians - kPi) * 1000.0));
}
} // namespace

std::int32_t NormalizeMrad(std::int64_t mrad) noexcept { return MradFromRadians(static_cast<double>(mrad) / 1000.0); }

std::int32_t CdegToMrad(std::int32_t cdeg) noexcept {
    // cdeg / 100 degrees -> radians.
    return MradFromRadians(static_cast<double>(cdeg) * kPi / 18000.0);
}

Pose FixedPose(const Tick& tick, std::uint32_t offset_cm) noexcept {
    const double cam = static_cast<double>(tick.cam_cdeg) * kPi / 18000.0;
    const double offset_mm = static_cast<double>(offset_cm) * 10.0;
    Pose pose{};
    pose.x_mm = ClampMm(tick.x_mm + offset_mm * std::sin(cam));
    pose.y_mm = tick.y_mm;
    pose.z_mm = ClampMm(tick.z_mm + offset_mm * std::cos(cam));
    pose.yaw_mrad = CdegToMrad(tick.yaw_cdeg);
    return pose;
}

Pose OrbitPose(const Tick& tick, std::uint32_t offset_cm, std::uint64_t t_ms) noexcept {
    const double phase = static_cast<double>(t_ms % orbit_period_ms) / orbit_period_ms * 2.0 * kPi;
    const double radius_mm = static_cast<double>(offset_cm) * 10.0;
    Pose pose{};
    pose.x_mm = ClampMm(tick.x_mm + radius_mm * std::sin(phase));
    pose.y_mm = tick.y_mm;
    pose.z_mm = ClampMm(tick.z_mm + radius_mm * std::cos(phase));
    // d/dphase (sin, cos) = (cos, -sin) = (sin(phase + pi/2), cos(phase + pi/2)).
    pose.yaw_mrad = MradFromRadians(phase + kPi / 2.0);
    return pose;
}

Pose PoseFromMetres(float x, float y, float z, float yaw) noexcept {
    Pose pose{};
    pose.x_mm = ClampMm(static_cast<double>(x) * 1000.0);
    pose.y_mm = ClampMm(static_cast<double>(y) * 1000.0);
    pose.z_mm = ClampMm(static_cast<double>(z) * 1000.0);
    pose.yaw_mrad = MradFromRadians(static_cast<double>(yaw));
    return pose;
}

std::uint32_t DistanceMm(std::int32_t ax, std::int32_t ay, std::int32_t az,
                         std::int32_t bx, std::int32_t by, std::int32_t bz) noexcept {
    const double dx = static_cast<double>(ax) - bx, dy = static_cast<double>(ay) - by, dz = static_cast<double>(az) - bz;
    const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(distance < 4294967295.0)) return 0xFFFFFFFFu;
    return static_cast<std::uint32_t>(std::llround(distance));
}

sync::PlayerTransformPacket MakeTransform(const Tick& tick, std::uint32_t sequence, std::uint32_t tick_ms) noexcept {
    sync::PlayerTransformPacket packet{};
    packet.sequence = sequence;
    packet.x = static_cast<float>(tick.x_mm / 1000.0);
    packet.y = static_cast<float>(tick.y_mm / 1000.0);
    packet.z = static_cast<float>(tick.z_mm / 1000.0);
    packet.rot_y = static_cast<float>(CdegToMrad(tick.yaw_cdeg) / 1000.0);
    packet.equip_id = 0;
    packet.stance = tick.stance <= 2 ? tick.stance : 0;
    packet.action_flags = tick.move == 3 ? sync::ActionFlag::Sprinting : 0;
    packet.tick_ms = tick_ms;
    return packet;
}

} // namespace mgscoop::partner
