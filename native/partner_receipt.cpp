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

// "0x" then 1..8 hex digits (either case).
bool ParseHex32(const char* text, std::size_t size, std::uint32_t& out) noexcept {
    if (size < 3 || size > 10 || text[0] != '0' || text[1] != 'x') return false;
    std::uint32_t value = 0;
    for (std::size_t i = 2; i < size; ++i) {
        const char c = text[i];
        std::uint32_t digit = 0;
        if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
        else return false;
        value = (value << 4) | digit;
    }
    out = value;
    return true;
}
// Unsigned decimal without leading zeros (or "0"), within [min, max].
bool ParseDec(const char* text, std::size_t size, std::uint32_t min, std::uint32_t max, std::uint32_t& out) noexcept {
    if (size == 0 || size > 10 || (size > 1 && text[0] == '0')) return false;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < size; ++i) {
        if (!Digit(text[i])) return false;
        value = value * 10 + static_cast<std::uint64_t>(text[i] - '0');
    }
    if (value < min || value > max) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}
bool ParseFlag(const char* text, std::size_t size, bool& out) noexcept {
    if (size != 1 || (text[0] != '0' && text[0] != '1')) return false;
    out = text[0] == '1';
    return true;
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
    std::int32_t action_flags = 0, equip_id = 0;
    if (p != end) {
        // The suffix is atomic; a partially upgraded producer cannot silently
        // retain a prior gun/action. Locomotion is carried separately by move.
        if (*p++ != ' ' || !ParseInt(p, end, 0, 11, action_flags) ||
            p == end || *p++ != ' ' || !ParseInt(p, end, 0, 65535, equip_id)) return false;
        constexpr std::uint8_t gun_flags = sync::ActionFlag::Aiming | sync::ActionFlag::Firing | sync::ActionFlag::Reloading;
        if (action_flags & ~gun_flags) return false;
    }
    if (p != end) return false;
    Tick parsed{};
    parsed.frame = values[0];
    parsed.x_mm = values[1]; parsed.y_mm = values[2]; parsed.z_mm = values[3];
    parsed.yaw_cdeg = values[4]; parsed.cam_cdeg = values[5];
    parsed.stance = static_cast<std::uint8_t>(values[6]);
    parsed.move = static_cast<std::uint8_t>(values[7]);
    parsed.luaflags = static_cast<std::uint8_t>(values[8]);
    parsed.action_flags = static_cast<std::uint8_t>(action_flags);
    parsed.equip_id = static_cast<std::uint16_t>(equip_id);
    out = parsed;
    return true;
}

std::size_t FormatReply(const Reply& reply, char* out, std::size_t capacity) noexcept {
    if (!out || capacity == 0) return 0;
    out[0] = 0;
    std::size_t at = 0;
    const bool look = (reply.op & op::set_parts_explicit) != 0;
    const bool dm = (reply.op & op::dm_play) != 0;
    const std::int64_t values[14] = {reply.op, reply.index, reply.x_mm, reply.y_mm, reply.z_mm, reply.yaw_mrad,
                                     static_cast<std::int64_t>(reply.status), reply.age_ms, look ? reply.look.type : 0,
                                     look ? reply.look.parts : 0, look ? reply.look.camo : 0, look ? reply.look.hand : 0,
                                     look ? reply.look.face : 0, reply.gani};
    const unsigned count = dm ? 14u : look ? 13u : 8u;
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
    if (in.type == 0 && SnakeFaceIndex(in.face, in.face_equip) >= snake_face_entries) return "face";
    Look look = in;
    bool avatar = false;
    if (look.type == 6) look = quiet_look;
    else if (look.type == 5) look = Look{5, 26, 115, 0, 0, 0};
    else if (look.type == 3) { look.type = 0; look.face = 0; look.face_equip = 0; avatar = true; }
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
        } else if (StartsToken(token, length, "input=", value_at)) {
            if (result.has_input || !ParsePartnerInput(token + value_at, length - value_at, result.input)) return false;
            result.has_input = true;
        } else if (StartsToken(token, length, "stick=", value_at)) {
            if (result.has_stick || !ParsePartnerStick(token + value_at, length - value_at, result.stick_deg, result.stick_pct))
                return false;
            result.has_stick = true;
        } else if (StartsToken(token, length, "tap=", value_at)) {
            if (result.tap || !ParseHex32(token + value_at, length - value_at, result.tap_bits)) return false;
            result.tap = true;
        } else if (StartsToken(token, length, "press=", value_at)) {
            const char* v = token + value_at;
            const std::size_t n = length - value_at;
            std::size_t colon = 0;
            while (colon < n && v[colon] != ':') ++colon;
            if (result.press || colon >= n || !ParseHex32(v, colon, result.press_bits) ||
                !ParseDec(v + colon + 1, n - colon - 1, 50, 5000, result.press_ms)) return false;
            result.press = true;
        } else if (StartsToken(token, length, "cam=", value_at)) {
            if (result.has_cam || !ParsePartnerInputCam(token + value_at, length - value_at, result.cam)) return false;
            result.has_cam = true;
        } else if (StartsToken(token, length, "hold=", value_at)) {
            if (result.has_hold || !ParsePartnerInputHold(token + value_at, length - value_at, result.hold)) return false;
            result.has_hold = true;
        } else if (StartsToken(token, length, "yaw=", value_at)) {
            if (result.has_yaw || !ParsePartnerInputYaw(token + value_at, length - value_at, result.yaw)) return false;
            result.has_yaw = true;
        } else if (StartsToken(token, length, "words=", value_at)) {
            if (result.has_words || !ParsePartnerWords(token + value_at, length - value_at, result.words)) return false;
            result.has_words = true;
        } else if (StartsToken(token, length, "bits=", value_at)) {
            if (result.has_bits || !ParseHex32(token + value_at, length - value_at, result.bits)) return false;
            result.has_bits = true;
        } else if (StartsToken(token, length, "correct=", value_at)) {
            if (result.has_correct || !ParseDec(token + value_at, length - value_at, 0, 10000, result.correct_mm)) return false;
            result.has_correct = true;
        } else if (StartsToken(token, length, "isend=", value_at)) {
            if (result.has_isend || !ParseFlag(token + value_at, length - value_at, result.isend)) return false;
            result.has_isend = true;
        } else if (StartsToken(token, length, "p218=", value_at)) {
            if (result.has_p218 || !ParseFlag(token + value_at, length - value_at, result.p218)) return false;
            result.has_p218 = true;
        } else if (StartsToken(token, length, "padprobe=", value_at)) {
            if (result.has_padprobe || !ParseFlag(token + value_at, length - value_at, result.padprobe)) return false;
            result.has_padprobe = true;
        } else if (StartsToken(token, length, "holdlog=", value_at)) {
            if (result.has_holdlog || !ParseFlag(token + value_at, length - value_at, result.holdlog)) return false;
            result.has_holdlog = true;
        } else if (StartsToken(token, length, "dm=", value_at)) {
            if (result.has_dm || !ParsePartnerDm(token + value_at, length - value_at, result.dm)) return false;
            result.has_dm = true;
        } else if (StartsToken(token, length, "dmplay=", value_at)) {
            std::uint32_t gani = 0;
            if (result.dmplay || !ParseDec(token + value_at, length - value_at, 0, max_gani, gani)) return false;
            result.dmplay = true;
            result.dmplay_gani = static_cast<std::uint8_t>(gani);
        } else if (EqualsToken(token, length, "dmstop=1")) {
            if (result.dmstop) return false;
            result.dmstop = true;
        } else if (StartsToken(token, length, "dmmap=", value_at)) {
            if (result.has_dmmap || !ParsePartnerDmMap(token + value_at, length - value_at, result.dmmap)) return false;
            result.has_dmmap = true;
        } else if (StartsToken(token, length, "dmspeed=", value_at)) {
            if (result.has_dmspeed || !ParsePartnerDmSpeeds(token + value_at, length - value_at, result.dmspeed)) return false;
            result.has_dmspeed = true;
        } else if (StartsToken(token, length, "lookgate=", value_at)) {
            if (result.has_lookgate || !ParseFlag(token + value_at, length - value_at, result.lookgate)) return false;
            result.has_lookgate = true;
        } else if (StartsToken(token, length, "lookrecover=", value_at)) {
            if (result.has_lookrecover || !ParseFlag(token + value_at, length - value_at, result.lookrecover)) return false;
            result.has_lookrecover = true;
        } else if (StartsToken(token, length, "recon=", value_at)) {
            if (result.has_recon || !ParsePartnerRecon(token + value_at, length - value_at, result.recon)) return false;
            result.has_recon = true;
        } else if (StartsToken(token, length, "align=", value_at)) {
            if (result.has_align || !ParsePartnerSigned(token + value_at, length - value_at, -50, 150, result.align_ms)) return false;
            result.has_align = true;
        } else if (StartsToken(token, length, "slot=", value_at)) {
            if (result.has_slot || !ParsePartnerSlot(token + value_at, length - value_at, result.slot)) return false;
            result.has_slot = true;
        } else if (StartsToken(token, length, "runaway=", value_at)) {
            if (result.has_runaway || !ParsePartnerRunaway(token + value_at, length - value_at, result.runaway)) return false;
            result.has_runaway = true;
        } else if (StartsToken(token, length, "wrap=", value_at)) {
            if (result.has_wrap || !ParsePartnerWrap(token + value_at, length - value_at, result.wrap)) return false;
            result.has_wrap = true;
        } else if (EqualsToken(token, length, "place=1")) {
            if (result.place) return false;
            result.place = true;
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
    const bool transform_driver = PartnerApplyIsTransform(input.apply) || input.apply == PartnerApply::input;
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
    if (input.stage >= 1 && input.dm_play) ops |= op::dm_play;
    if (input.dm_stop) ops |= op::dm_stop;
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
        // A new value gets a fresh attempt (the retry backoff is per candidate).
        state.candidate = stance;
        state.since_ms = now_ms;
        state.retry_ms = 0;
    }
    if (now_ms - state.since_ms < hold_ms || now_ms < state.retry_ms) return 0;
    return stance;
}

void CommitStance(StanceDebounce& state, std::uint8_t stance) noexcept {
    state.applied = stance;
    state.candidate = 0;
    state.retry_ms = 0;
}

void RetryStance(StanceDebounce& state, std::uint64_t now_ms, std::uint32_t retry_ms) noexcept {
    state.retry_ms = now_ms + retry_ms;
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
    packet.equip_id = tick.equip_id;
    packet.stance = tick.stance <= 2 ? tick.stance : 0;
    constexpr std::uint8_t gun_flags = sync::ActionFlag::Aiming | sync::ActionFlag::Firing | sync::ActionFlag::Reloading;
    packet.action_flags = tick.action_flags & gun_flags;
    packet.action_flags |= tick.move == 3 ? sync::ActionFlag::Sprinting :
                           tick.move == 2 ? sync::ActionFlag::Running :
                           tick.move == 1 ? sync::ActionFlag::Walking : 0;
    packet.tick_ms = tick_ms;
    return packet;
}

} // namespace mgscoop::partner
