// Original partner name tag core (docs/partner-nametag.md). Pure: no game,
// Windows or ImGui calls.
#include "mgscoop/nametag_core.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mgscoop::nametag {
namespace {
constexpr float kDegPerRad = 57.2957795f;
bool Finite(float v) noexcept { return std::isfinite(v) != 0; }
float Clamp(float v, float lo, float hi) noexcept { return v < lo ? lo : v > hi ? hi : v; }
Vec3 Sub(Vec3 a, Vec3 b) noexcept { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 Add(Vec3 a, Vec3 b) noexcept { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 Scale(Vec3 a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }
float Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }
bool Equals(const char* text, std::size_t size, const char* literal) noexcept {
    const std::size_t n = std::strlen(literal);
    return size == n && std::memcmp(text, literal, n) == 0;
}
void CopyMatrix(Mat4& out, const float (&in)[4][4]) noexcept { std::memcpy(out.m, in, sizeof(out.m)); }
bool MatrixFinite(const float (&m)[4][4]) noexcept {
    for (const auto& row : m) for (const float v : row) if (!Finite(v)) return false;
    return true;
}
} // namespace

const char* CrosshairName(Crosshair value) noexcept {
    switch (value) {
    case Crosshair::always: return "always";
    case Crosshair::off: return "off";
    default: return "aim";
    }
}
bool ParseCrosshair(const char* text, std::size_t size, Crosshair& out) noexcept {
    if (!text) return false;
    if (Equals(text, size, "aim")) { out = Crosshair::aim; return true; }
    if (Equals(text, size, "always")) { out = Crosshair::always; return true; }
    if (Equals(text, size, "off") || Equals(text, size, "0")) { out = Crosshair::off; return true; }
    return false;
}

const char* RefusalName(Refusal value) noexcept {
    static constexpr const char* names[] = {"none", "contract", "thread", "selector", "handle", "viewport", "sub_viewport",
        "vp_vtable", "camera", "cam_vtable", "ortho", "size", "rect", "nonfinite", "proj_form", "torn"};
    const auto i = static_cast<std::size_t>(value);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "?";
}

Mat4 Multiply(const Mat4& a, const Mat4& b) noexcept {
    Mat4 out{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            out.m[i][j] = (a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]) + (a.m[i][2] * b.m[2][j] + a.m[i][3] * b.m[3][j]);
    return out;
}

Refusal ValidateCamera(const CameraRaw& raw, const CameraExpect& expect, Camera& out) noexcept {
    out = {};
    if (raw.vp_vtable == expect.sub_vp_vtable) return Refusal::sub_viewport;
    if (raw.vp_vtable != expect.vp_vtable) return Refusal::vp_vtable;
    if (!raw.cam) return Refusal::camera;
    if (raw.cam_vtable != expect.cam_vtable) return Refusal::cam_vtable;
    if (raw.cam_flags & 4u) return Refusal::ortho;
    if (raw.px_w < kMinPixels || raw.px_w > kMaxPixels || raw.px_h < kMinPixels || raw.px_h > kMaxPixels) return Refusal::size;
    for (const float v : raw.rect) if (!Finite(v)) return Refusal::rect;
    const float x0 = raw.rect[0], y0 = raw.rect[1], w = raw.rect[2], h = raw.rect[3];
    if (x0 < 0.0f || y0 < 0.0f || x0 >= 1.0f || y0 >= 1.0f || !(w > 0.0f) || !(h > 0.0f) || x0 + w > 1.001f || y0 + h > 1.001f)
        return Refusal::rect;
    if (!MatrixFinite(raw.proj) || !MatrixFinite(raw.view) || !MatrixFinite(raw.viewproj) || !Finite(raw.near_m) ||
        !Finite(raw.far_m) || !Finite(raw.pixel_aspect)) return Refusal::nonfinite;
    const auto& p = raw.proj;
    constexpr float eps = 1e-3f;
    const float off[] = {p[0][1], p[0][2], p[0][3], p[1][0], p[1][2], p[1][3], p[3][0], p[3][1]};
    bool form = std::fabs(p[2][3] - 1.0f) <= eps && std::fabs(p[3][3]) <= eps && p[0][0] < -0.05f && p[0][0] > -20.0f &&
        p[1][1] > 0.05f && p[1][1] < 20.0f && std::fabs(p[2][0]) <= 0.05f && std::fabs(p[2][1]) <= 0.05f;
    for (const float v : off) form = form && std::fabs(v) <= eps;
    if (!form) return Refusal::proj_form;
    CopyMatrix(out.proj, raw.proj); CopyMatrix(out.view, raw.view); CopyMatrix(out.viewproj, raw.viewproj);
    const Mat4 product = Multiply(out.view, out.proj);
    float worst = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float scale = 1.0f;
        for (int j = 0; j < 4; ++j) scale = std::fmax(scale, std::fabs(out.viewproj.m[i][j]));
        for (int j = 0; j < 4; ++j) worst = std::fmax(worst, std::fabs(product.m[i][j] - out.viewproj.m[i][j]) / scale);
    }
    out.vp_err = worst;
    std::memcpy(out.rect, raw.rect, sizeof(out.rect));
    out.px_w = raw.px_w; out.px_h = raw.px_h;
    out.vfov_deg = 2.0f * std::atan(1.0f / p[1][1]) * kDegPerRad;
    out.near_m = raw.near_m; out.far_m = raw.far_m;
    if (!(worst <= kTornTolerance)) return Refusal::torn;
    return Refusal::none;
}

Projected Project(const Mat4& viewproj, Vec3 world) noexcept {
    Projected out{};
    const float v[4] = {world.x, world.y, world.z, 1.0f};
    float clip[4]{};
    for (int j = 0; j < 4; ++j) clip[j] = v[0] * viewproj.m[0][j] + v[1] * viewproj.m[1][j] + v[2] * viewproj.m[2][j] + v[3] * viewproj.m[3][j];
    out.w = clip[3];
    out.in_front = Finite(clip[3]) && clip[3] > kMinW;
    if (out.in_front) { out.ndc[0] = clip[0] / clip[3]; out.ndc[1] = clip[1] / clip[3]; }
    if (!Finite(out.ndc[0]) || !Finite(out.ndc[1])) { out.in_front = false; out.ndc[0] = out.ndc[1] = 0.0f; }
    return out;
}

Vec3 Transform(const Mat4& m, Vec3 p) noexcept {
    return {p.x * m.m[0][0] + p.y * m.m[1][0] + p.z * m.m[2][0] + m.m[3][0],
            p.x * m.m[0][1] + p.y * m.m[1][1] + p.z * m.m[2][1] + m.m[3][1],
            p.x * m.m[0][2] + p.y * m.m[1][2] + p.z * m.m[2][2] + m.m[3][2]};
}

void NdcToDisplay(const float (&rect)[4], const float (&ndc)[2], float width, float height, float& x, float& y) noexcept {
    x = width * (rect[0] + rect[2] * (ndc[0] + 1.0f) * 0.5f);
    y = height * (rect[1] + rect[3] * (1.0f - ndc[1]) * 0.5f);
}

float StanceHeight(std::uint32_t stance) noexcept {
    return stance == 1 ? kSquatHeight : stance == 2 ? kProneHeight : kStandHeight;
}

Capsule BodyCapsule(Vec3 foot, float yaw, std::uint32_t stance) noexcept {
    Capsule c{};
    c.radius = kCapsuleRadius;
    if (stance == 2) {
        const Vec3 centre{foot.x, foot.y + 0.25f, foot.z};
        const Vec3 forward{std::sin(yaw) * kProneHalfLength, 0.0f, std::cos(yaw) * kProneHalfLength};
        c.a = Sub(centre, forward); c.b = Add(centre, forward);
    } else {
        c.a = {foot.x, foot.y + 0.15f, foot.z};
        c.b = {foot.x, foot.y + StanceHeight(stance) - 0.15f, foot.z};
    }
    return c;
}

float QuatYaw(const float (&q)[4]) noexcept {
    return std::atan2(2.0f * (q[3] * q[1] + q[0] * q[2]), 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]));
}

Aim CentreRay(const Mat4& view, const Mat4& proj, const Capsule& world) noexcept {
    Aim out{};
    out.distance = INFINITY;
    const Vec3 a = Transform(view, world.a), b = Transform(view, world.b);
    // NDC (0,0): x*P00 + z*P20 = 0 and y*P11 + z*P21 = 0 for z = 1 (P10 = P01 = P30 = P31 = 0, validated).
    Vec3 d{-proj.m[2][0] / proj.m[0][0], -proj.m[2][1] / proj.m[1][1], 1.0f};
    const float length = std::sqrt(Dot(d, d));
    if (!Finite(length) || length <= 0.0f) return out;
    d = Scale(d, 1.0f / length);
    // Closest points between the ray segment [min, max]*d and the capsule axis (Ericson 5.1.9).
    const Vec3 p1 = Scale(d, kRayMin), d1 = Scale(d, kRayMax - kRayMin), d2 = Sub(b, a), r = Sub(p1, a);
    const float aa = Dot(d1, d1), e = Dot(d2, d2), f = Dot(d2, r);
    constexpr float tiny = 1e-8f;
    float s = 0.0f, t = 0.0f;
    if (e <= tiny) { t = 0.0f; s = Clamp(-Dot(d1, r) / aa, 0.0f, 1.0f); }
    else {
        const float c = Dot(d1, r), bb = Dot(d1, d2), denom = aa * e - bb * bb;
        s = denom > tiny ? Clamp((bb * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
        t = (bb * s + f) / e;
        if (t < 0.0f) { t = 0.0f; s = Clamp(-c / aa, 0.0f, 1.0f); }
        else if (t > 1.0f) { t = 1.0f; s = Clamp((bb - c) / aa, 0.0f, 1.0f); }
    }
    const Vec3 c1 = Add(p1, Scale(d1, s)), c2 = Add(a, Scale(d2, t)), gap = Sub(c1, c2);
    out.distance = std::sqrt(Dot(gap, gap));
    out.t = kRayMin + s * (kRayMax - kRayMin);
    out.hit = Finite(out.distance) && out.distance <= world.radius;
    const Vec3 centre = Scale(Add(a, b), 0.5f);
    const float sx = proj.m[0][0] < 0.0f ? -1.0f : 1.0f, sy = proj.m[1][1] < 0.0f ? -1.0f : 1.0f;
    out.miss_yaw_deg = std::remainder(std::atan2(sx * centre.x, centre.z) - std::atan2(sx * d.x, d.z), 6.2831853f) * kDegPerRad;
    out.miss_pitch_deg = (std::atan2(sy * centre.y, std::hypot(centre.x, centre.z)) - std::atan2(sy * d.y, std::hypot(d.x, d.z))) * kDegPerRad;
    return out;
}

float EaseToward(Ease& ease, float target, std::uint64_t now_ms, std::uint32_t tau_ms) noexcept {
    if (!Finite(target)) return ease.value;
    if (!ease.valid || now_ms < ease.ms || !tau_ms || !Finite(ease.value)) {
        ease = {true, target, now_ms};
        return target;
    }
    const float dt = static_cast<float>(now_ms - ease.ms);
    const float k = 1.0f - std::exp(-dt / static_cast<float>(tau_ms));
    ease.value += (target - ease.value) * k;
    ease.ms = now_ms;
    return ease.value;
}

float FadeAlpha(float distance_m) noexcept {
    if (!Finite(distance_m) || distance_m <= 40.0f) return 1.0f;
    if (distance_m >= 150.0f) return 0.4f;
    return 1.0f - 0.6f * (distance_m - 40.0f) / 110.0f;
}

std::size_t SanitizeName(const char* text, std::size_t size, char (&out)[kNameBytes + 1]) noexcept {
    std::memset(out, 0, sizeof(out));
    if (!text) return 0;
    std::size_t used = 0, points = 0, i = 0;
    bool space = false;
    const auto emit = [&](std::uint32_t cp) noexcept {
        if (points >= kNameChars) return;
        if (cp < 0x80) out[used++] = static_cast<char>(cp);
        else { out[used++] = static_cast<char>(0xC0 | (cp >> 6)); out[used++] = static_cast<char>(0x80 | (cp & 0x3F)); }
        ++points;
    };
    while (i < size && points < kNameChars) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t length = 1, need = 0;
        std::uint32_t min = 0;
        if (lead < 0x80) cp = lead;
        else if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1Fu; need = 1; min = 0x80; }
        else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0Fu; need = 2; min = 0x800; }
        else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07u; need = 3; min = 0x10000; }
        else cp = 0xFFFD;
        if (need) {
            std::size_t valid = 0;  // continuation bytes present
            while (valid < need && i + 1 + valid < size && (static_cast<unsigned char>(text[i + 1 + valid]) & 0xC0) == 0x80) {
                cp = (cp << 6) | (static_cast<unsigned char>(text[i + 1 + valid]) & 0x3Fu);
                ++valid;
            }
            length = 1 + valid;  // a broken sequence (its maximal subpart) becomes one '?'
            if (valid != need || cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;  // truncated/overlong/surrogate/range
        }
        i += length;
        const bool control = cp < 0x20 || (cp >= 0x7F && cp <= 0x9F);
        const bool invisible = cp == 0xAD || cp == 0x061C || cp == 0x200E || cp == 0x200F || (cp >= 0x202A && cp <= 0x202E) ||
            (cp >= 0x2066 && cp <= 0x2069) || (cp >= 0x200B && cp <= 0x200D) || cp == 0x2060 || cp == 0xFEFF;
        if (control || invisible) continue;
        if (cp == 0x20 || cp == 0xA0) { space = points != 0; continue; }
        if (!(cp < 0x7F || (cp >= 0xA1 && cp <= 0xFF))) cp = '?';
        if (space) {
            if (points + 1 >= kNameChars) break;  // never end on the collapsed space
            emit(' ');
            space = false;
        }
        emit(cp);
    }
    out[used] = 0;
    return points;
}

const char* FallbackName(bool viewer_is_host) noexcept { return viewer_is_host ? "Player 2" : "Host"; }

bool LabNameValid(const char* text, std::size_t size) noexcept {
    if (!text || !size || size > kNameChars || text[0] == ' ' || text[size - 1] == ' ') return false;
    for (std::size_t i = 0; i < size; ++i) {
        const char c = text[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '.' || c == '-'))
            return false;
    }
    return true;
}

std::size_t EncodeName(const char* name, std::uint32_t session, std::uint8_t (&out)[kWireMax]) noexcept {
    std::memset(out, 0, sizeof(out));
    char clean[kNameBytes + 1]{};
    SanitizeName(name ? name : "", name ? std::strlen(name) : 0, clean);
    const std::size_t length = std::strlen(clean);
    out[0] = kMagic; out[1] = kVersion; out[2] = 0; out[3] = static_cast<std::uint8_t>(length);
    for (unsigned i = 0; i < 4; ++i) out[4 + i] = static_cast<std::uint8_t>(session >> (8 * i));
    std::memcpy(out + kWireHeader, clean, length);
    return kWireHeader + length;
}

const char* WireErrorName(WireError value) noexcept {
    switch (value) {
    case WireError::size: return "size";
    case WireError::magic: return "magic";
    case WireError::version: return "version";
    case WireError::flags: return "flags";
    case WireError::length: return "length";
    default: return "none";
    }
}

WireError DecodeName(const std::uint8_t* bytes, std::size_t size, char (&out)[kNameBytes + 1], std::uint32_t& session) noexcept {
    std::memset(out, 0, sizeof(out));
    session = 0;
    if (!bytes || size < kWireHeader || size > kWireMax) return WireError::size;
    if (bytes[0] != kMagic) return WireError::magic;
    if (bytes[1] != kVersion) return WireError::version;
    if (bytes[2] != 0) return WireError::flags;
    if (bytes[3] > kNameBytes || size != kWireHeader + bytes[3]) return WireError::length;
    for (unsigned i = 0; i < 4; ++i) session |= static_cast<std::uint32_t>(bytes[4 + i]) << (8 * i);
    SanitizeName(reinterpret_cast<const char*>(bytes + kWireHeader), bytes[3], out);
    return WireError::none;
}

bool IsCommand(const char* text) noexcept {
    return text && std::strncmp(text, "nametag", 7) == 0 && (text[7] == 0 || text[7] == ' ');
}

bool ParseCommand(const char* text, Command& out) noexcept {
    out = {};
    if (!IsCommand(text) || text[7] != ' ') return false;
    const char* rest = text + 8;
    if (!std::strcmp(rest, "on")) { out.op = CommandOp::on; return true; }
    if (!std::strcmp(rest, "off")) { out.op = CommandOp::off; return true; }
    if (!std::strcmp(rest, "status")) { out.op = CommandOp::status; return true; }
    if (!std::strncmp(rest, "crosshair ", 10)) {
        const char* value = rest + 10;
        if (!std::strcmp(value, "0") || !ParseCrosshair(value, std::strlen(value), out.crosshair)) return false;
        out.op = CommandOp::crosshair;
        return true;
    }
    return false;
}

const char* ReasonName(Reason value) noexcept {
    static constexpr const char* names[] = {"ok", "off", "camera", "terminating", "hud", "no_partner", "range", "lost", "no_body",
        "behind", "offscreen", "far", "stale", "shutdown"};
    const auto i = static_cast<std::size_t>(value);
    return i < sizeof(names) / sizeof(names[0]) ? names[i] : "?";
}
const char* LocalSourceName(LocalSource value) noexcept {
    switch (value) {
    case LocalSource::lab: return "lab";
    case LocalSource::self: return "self";
    case LocalSource::coop: return "coop";
    case LocalSource::steam: return "steam";
    default: return "none";
    }
}
const char* AimSourceName(AimSource value) noexcept {
    switch (value) {
    case AimSource::gun: return "gun";
    case AimSource::hold: return "hold";
    default: return "none";
    }
}

namespace {
struct Writer {
    char* bytes{}; std::size_t limit{}, used{}; bool ok{true};
    void Add(const char* format, ...) noexcept {
        if (!ok) return;
        va_list args; va_start(args, format);
        const int n = std::vsnprintf(bytes + used, limit - used, format, args);
        va_end(args);
        if (n < 0 || static_cast<std::size_t>(n) >= limit - used) { ok = false; return; }
        used += static_cast<std::size_t>(n);
    }
    void Bool(bool v) noexcept { Add(v ? "true" : "false"); }
    void Float(float v) noexcept { if (Finite(v)) Add("%.4f", static_cast<double>(v)); else Add("null"); }
    void Floats(const float* v, unsigned n) noexcept { Add("["); for (unsigned i = 0; i < n; ++i) { if (i) Add(","); Float(v[i]); } Add("]"); }
    // UTF-8 in, JSON out: printable ASCII as is, a 2-byte sequence as its \u00XX code point, anything else escaped per byte.
    void String(const char* s, std::size_t capacity) noexcept {
        Add("\"");
        for (std::size_t i = 0; s && i < capacity && s[i];) {
            const auto c = static_cast<unsigned char>(s[i]);
            if (c == '"' || c == '\\') { Add("\\%c", c); ++i; }
            else if (c >= 0x20 && c < 0x7F) { Add("%c", c); ++i; }
            else if ((c & 0xE0) == 0xC0 && i + 1 < capacity && (static_cast<unsigned char>(s[i + 1]) & 0xC0) == 0x80) {
                Add("\\u%04x", ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu)); i += 2;
            } else { Add(c < 0x20 ? "\\u%04x" : "\\ufffd", static_cast<unsigned>(c)); ++i; }
        }
        Add("\"");
    }
};
} // namespace

std::size_t FormatJson(const Telemetry& t, const RenderStats& r, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    Writer j{out, capacity};
    j.Add(",\"nametag\":{\"on\":"); j.Bool(t.on);
    j.Add(",\"crosshair\":\"%s\",\"state\":\"%s\",\"reason\":\"%s\",\"name\":", CrosshairName(t.crosshair),
          t.visible ? "visible" : "hidden", ReasonName(t.reason));
    j.String(t.name, sizeof(t.name));
    j.Add(",\"name_src\":\"%s\",\"local_name\":", t.name_src == NameSource::remote ? "remote" : "fallback");
    j.String(t.local_name, sizeof(t.local_name));
    j.Add(",\"local_src\":\"%s\",\"name_tx\":%u,\"name_rx\":%u,\"name_invalid\":%u,\"link_session\":%u,\"body_ok\":",
          LocalSourceName(t.local_src), t.name_tx, t.name_rx, t.name_invalid, t.link_session);
    j.Bool(t.body_ok);
    j.Add(",\"body\":"); j.Floats(t.body, 3); j.Add(",\"foot\":"); j.Floats(t.foot, 3); j.Add(",\"head\":"); j.Floats(t.head, 3);
    j.Add(",\"stance\":%u,\"dist_m\":", t.stance); j.Float(t.dist_m);
    j.Add(",\"projected\":"); j.Bool(t.projected);
    j.Add(",\"ndc\":"); j.Floats(t.ndc, 2); j.Add(",\"w\":"); j.Float(t.w);
    j.Add(",\"miss_yaw_deg\":"); j.Float(t.miss_yaw_deg); j.Add(",\"miss_pitch_deg\":"); j.Float(t.miss_pitch_deg);
    j.Add(",\"hover\":"); j.Bool(t.hover); j.Add(",\"aim\":"); j.Bool(t.aim);
    j.Add(",\"aim_src\":\"%s\",\"crosshair_on\":", AimSourceName(t.aim_src)); j.Bool(t.crosshair_on);
    j.Add(",\"hud\":%u,\"snapshot_seq\":%llu,\"snapshot_tick_ms\":%llu", t.hud, static_cast<unsigned long long>(t.seq),
          static_cast<unsigned long long>(t.tick_ms));
    const auto& c = t.camera;
    j.Add(",\"camera\":{\"ok\":"); j.Bool(c.ok);
    j.Add(",\"refusal\":\"%s\",\"vp_vt\":\"0x%llx\",\"cam_vt\":\"0x%llx\",\"vp_px\":[%u,%u],\"rect\":", RefusalName(c.refusal),
          static_cast<unsigned long long>(c.vp_vt), static_cast<unsigned long long>(c.cam_vt), c.px[0], c.px[1]);
    j.Floats(c.rect, 4);
    j.Add(",\"vfov_deg\":"); j.Float(c.vfov_deg); j.Add(",\"near\":"); j.Float(c.near_m); j.Add(",\"far\":"); j.Float(c.far_m);
    j.Add(",\"vp_err\":"); j.Float(c.vp_err);
    j.Add(",\"torn\":%u,\"thread\":%u}", c.torn, c.thread);
    j.Add(",\"render\":{\"frames\":%u,\"drawn\":%u,\"stale_frames\":%u,\"hidden_frames\":%u,\"last_px\":", r.frames, r.drawn,
          r.stale_frames, r.hidden_frames);
    j.Floats(r.last_px, 2); j.Add(",\"label_box\":"); j.Floats(r.label_box, 4);
    j.Add(",\"crosshair\":"); j.Bool(r.crosshair);
    j.Add(",\"crosshair_frames\":%u,\"ring\":", r.crosshair_frames); j.Floats(r.ring, 3);
    j.Add(",\"bb\":[%u,%u],\"display\":[%u,%u],\"alpha\":", r.bb[0], r.bb[1], r.display[0], r.display[1]); j.Float(r.alpha);
    j.Add(",\"present_thread\":%u,\"age_ms\":%u}}", r.present_thread, r.age_ms);
    if (!j.ok) { out[0] = 0; return 0; }
    return j.used;
}

std::size_t FormatStatus(const Telemetry& t, const RenderStats& r, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    char name[kNameChars + 1]{};
    std::size_t n = 0;
    for (std::size_t i = 0; t.name[i] && i < kNameBytes && n < kNameChars;) {
        const auto c = static_cast<unsigned char>(t.name[i]);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
        name[n++] = plain ? static_cast<char>(c) : '_';
        i += (c & 0xE0) == 0xC0 ? 2 : 1;  // one '_' per Latin-1 code point
    }
    const float d = Finite(t.dist_m) ? (t.dist_m > 9999.9f ? 9999.9f : t.dist_m) : 0.0f;
    const auto px = [](float v) noexcept { return Finite(v) && v > -99999.0f && v < 99999.0f ? static_cast<int>(std::lround(v)) : 0; };
    const int written = std::snprintf(out, capacity,
        "nametag;on=%u;xh=%s;st=%s;why=%s;name=%s;src=%s;d=%.1f;stance=%u;px=%d,%d;hov=%u;aim=%u;stale_frames=%u",
        t.on ? 1u : 0u, CrosshairName(t.crosshair), t.visible ? "visible" : "hidden", ReasonName(t.reason), name,
        t.name_src == NameSource::remote ? "remote" : "fallback", static_cast<double>(d), t.stance, px(r.last_px[0]), px(r.last_px[1]),
        t.hover ? 1u : 0u, t.aim ? 1u : 0u, r.stale_frames > 9999999u ? 9999999u : r.stale_frames);
    if (written < 0) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(written) < capacity ? static_cast<std::size_t>(written) : capacity - 1;
}

} // namespace mgscoop::nametag
