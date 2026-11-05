// Pure part of MGS_COOP_HORSE_DRIVE_V1 (native/include/mgscoop/horse_drive.hpp; mgs_coop_core).
#include "mgscoop/horse_drive.hpp"
#include <charconv>
#include <cmath>
#include <cstring>

namespace mgscoop::horse_drive {
namespace {
std::uint64_t g_placed_ms = 0, g_body_ms = 0;
float g_body[3]{};
bool g_body_ok = false;
}

void Matrix(float x, float y, float z, float yaw, float (&m)[16]) noexcept {
    const float s = std::sin(yaw), c = std::cos(yaw);
    const float rows[16] = {c, 0.f, -s, 0.f, 0.f, 1.f, 0.f, 0.f, s, 0.f, c, 0.f, x, y, z, 1.f};
    std::memcpy(m, rows, sizeof(rows));
}

void HorseUnderRider(const float (&body)[3], float yaw, float (&out)[3]) noexcept {
    out[0] = body[0] - kSeatForward * std::sin(yaw);
    out[1] = body[1] - kSeatUp;
    out[2] = body[2] - kSeatForward * std::cos(yaw);
}

bool AnchorUsable(const float (&stream)[3], const float (&anchored)[3]) noexcept {
    (void)stream;
    (void)anchored;
    return false;  // red
}

bool Parse(const char* text, std::size_t size, Request& out) noexcept {
    out = Request{};
    if (!text || !size || size > 120) return false;
    const char* p = text;
    const char* const end = text + size;
    const auto word = [&](const char* w) noexcept {
        const std::size_t n = std::strlen(w);
        if (static_cast<std::size_t>(end - p) < n || std::memcmp(p, w, n) != 0) return false;
        if (p + n != end && p[n] != ' ') return false;
        p += n;
        return true;
    };
    const auto number = [&](std::int64_t lo, std::int64_t hi, std::int64_t& v) noexcept {
        if (p == end || *p != ' ') return false;
        ++p;
        const auto r = std::from_chars(p, end, v);
        if (r.ec != std::errc() || r.ptr == p || v < lo || v > hi) return false;
        p = r.ptr;
        return true;
    };
    Request r{};
    std::int64_t v[7]{};
    if (word("cfg")) {
        r.verb = Verb::cfg;
    } else if (word("read")) {
        r.verb = Verb::read;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1])) return false;
    } else if (word("place")) {
        r.verb = Verb::place;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1]) || !number(-1000000000, 1000000000, v[2]) ||
            !number(-1000000000, 1000000000, v[3]) || !number(-1000000000, 1000000000, v[4]) || !number(-31416, 31416, v[5]) ||
            !number(0, 1, v[6]))
            return false;
    } else {
        return false;
    }
    if (p != end) return false;
    if (r.verb != Verb::cfg && v[0] != 30010 && v[0] != 30020) return false;
    r.mission = static_cast<std::uint32_t>(v[0]);
    r.epoch = static_cast<std::uint32_t>(v[1]);
    r.x = static_cast<std::int32_t>(v[2]);
    r.y = static_cast<std::int32_t>(v[3]);
    r.z = static_cast<std::int32_t>(v[4]);
    r.yaw = static_cast<std::int32_t>(v[5]);
    r.anchor = static_cast<std::uint8_t>(v[6]);
    out = r;
    return true;
}

const char* PlaceRefusal(const PlaceScope& s) noexcept {
    if (!s.key) return "off";
    if (!s.pins) return "pins";
    if (!s.field) return "scope";
    if (!s.census) return "census";
    if (!s.realized) return "unrealized";
    if (s.ridden) return "ridden";
    if (s.pending) return "pending";
    if (!s.cs) return "cs";
    return nullptr;
}

std::uint32_t Generation(std::uintptr_t records, std::uintptr_t receiver, std::uint32_t mask) noexcept {
    std::uint64_t h = 1469598103934665603ull;
    const std::uint64_t parts[3] = {static_cast<std::uint64_t>(records), static_cast<std::uint64_t>(receiver), mask};
    for (std::uint64_t part : parts)
        for (unsigned i = 0; i < 8; ++i) {
            h ^= (part >> (8 * i)) & 0xffu;
            h *= 1099511628211ull;
        }
    const auto g = static_cast<std::uint32_t>(h ^ (h >> 32)) & 0x7fffffffu;
    return g ? g : 1u;
}

void NotePlaced(std::uint64_t now_ms) noexcept { g_placed_ms = now_ms ? now_ms : 1; }
bool PlacedFresh(std::uint64_t now_ms) noexcept {
    return g_placed_ms && now_ms >= g_placed_ms && now_ms - g_placed_ms <= kFreshMs;
}
void NoteRiderBody(const float (&p)[3], std::uint64_t now_ms) noexcept {
    if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) return;
    std::memcpy(g_body, p, sizeof(g_body));
    g_body_ms = now_ms ? now_ms : 1;
    g_body_ok = true;
}
bool RiderBody(std::uint64_t now_ms, float (&p)[3]) noexcept {
    if (!g_body_ok || now_ms < g_body_ms || now_ms - g_body_ms > kAnchorMs) return false;
    std::memcpy(p, g_body, sizeof(g_body));
    return true;
}
void Reset() noexcept {
    g_placed_ms = g_body_ms = 0;
    g_body_ok = false;
}

} // namespace mgscoop::horse_drive
