// Pure part of MGS_COOP_HORSE_DRIVE_V1 (native/include/mgscoop/horse_drive.hpp; mgs_coop_core).
#include "mgscoop/horse_drive.hpp"
#include <charconv>
#include <cmath>
#include <cstring>

namespace mgscoop::horse_drive {
namespace {
std::uint64_t g_placed_ms = 0, g_body_ms = 0, g_seat_ms = 0;
float g_seat_back = 0.f, g_seat_up = 0.f;
bool g_seat_pose = true;
std::uint64_t g_box_ms = 0;
float g_box[3]{}, g_box_yaw = 0.f;
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
    const float dx = anchored[0] - stream[0], dy = anchored[1] - stream[1], dz = anchored[2] - stream[2];
    return std::isfinite(dx) && std::isfinite(dy) && std::isfinite(dz) && dx * dx + dy * dy + dz * dz <= kAnchorMaxM * kAnchorMaxM;
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
    std::int64_t v[8]{}, seat[2]{};
    if (word("cfg")) {
        r.verb = Verb::cfg;
    } else if (word("read")) {
        r.verb = Verb::read;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1])) return false;
    } else if (word("hold0") || word("hold1")) {
        r.verb = p[-1] == '1' ? Verb::hold1 : Verb::hold0;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1]) || !number(-1000000000, 1000000000, v[2]) ||
            !number(-1000000000, 1000000000, v[3]) || !number(-1000000000, 1000000000, v[4]) || !number(-31416, 31416, v[5]) ||
            !number(0, 15, v[7]))
            return false;
    } else if (word("free0")) {
        r.verb = Verb::free0;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1])) return false;
    } else if (word("pad")) {
        r.verb = Verb::pad;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1])) return false;
    } else if (word("seat1")) {
        r.verb = Verb::seat1;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1]) || !number(0, 1500, seat[0]) || !number(-500, 1500, seat[1]) ||
            !number(0, 1, v[6]))
            return false;
    } else if (word("pbox")) {
        r.verb = Verb::pbox;
        if (!number(30010, 30020, v[0]) || !number(1, 65535, v[1]) || !number(-1000000000, 1000000000, v[2]) ||
            !number(-1000000000, 1000000000, v[3]) || !number(-1000000000, 1000000000, v[4]) || !number(-31416, 31416, v[5]))
            return false;
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
    r.reset = static_cast<std::uint8_t>(v[7]);
    r.back = static_cast<std::int32_t>(seat[0]);
    r.up = static_cast<std::int32_t>(seat[1]);
    if (r.verb == Verb::seat1) {
        r.seat_pose = static_cast<std::uint8_t>(v[6]);
        r.anchor = 0;
    }
    out = r;
    return true;
}

const char* HoldRefusal(const HoldScope& s) noexcept {
    if (!s.share || !(s.cargo || s.pillion)) return "off";
    if (!s.pins) return "pins";
    if (!s.field) return "scope";
    if (!s.body) return "body";
    if (s.mounted) return "mounted";
    return nullptr;
}

bool CachedBody1Usable(const BodyClass& b0, const BodyClass& c) noexcept {
    return c.t_vt && c.t68 && c.t70 && c.t58 && b0.t_vt == c.t_vt && b0.t68 == c.t68 && b0.t70 == c.t70 && b0.t58 == c.t58;
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

const char* SeatRefusal(const SeatScope& s) noexcept {
    if (!s.pillion) return "off";
    if (!s.pins) return "pins";
    if (!s.field) return "scope";
    if (!s.riding) return "not_riding";
    return nullptr;
}
bool RawAction(std::uint32_t src_held, std::uint32_t kb_held) noexcept {
    return ((src_held ? src_held : kb_held) & 0x20u) != 0;
}
void RearSeat(const float (&rider)[3], float yaw, float back, float up, float (&out)[3]) noexcept {
    out[0] = rider[0] - back * std::sin(yaw);
    out[1] = rider[1] + up;
    out[2] = rider[2] - back * std::cos(yaw);
}
void NoteSeat(std::int32_t back_mm, std::int32_t up_mm, bool pose, std::uint64_t now_ms) noexcept {
    g_seat_pose = pose;
    g_seat_back = static_cast<float>(back_mm) / 1000.f;
    g_seat_up = static_cast<float>(up_mm) / 1000.f;
    g_seat_ms = now_ms ? now_ms : 1;
}
bool SeatLease(std::uint64_t now_ms, float& back, float& up, bool& pose) noexcept {
    if (!g_seat_ms || now_ms < g_seat_ms || now_ms - g_seat_ms > kSeatMs) return false;
    back = g_seat_back;
    up = g_seat_up;
    pose = g_seat_pose;
    return true;
}

bool HoldSeat(SeatHold& h, bool lease, float& back, float& up, bool& pose) noexcept {
    if (lease) {
        h.live = true;
        h.miss = 0;
        h.back = back;
        h.up = up;
        h.pose = pose;
        return true;
    }
    // The review of M4: the grace only with world_horse_shot named (h.grace); at the defaults a lapse ends the seat.
    if (h.grace && h.live && h.miss < kSeatGraceTicks) {
        ++h.miss;
        back = h.back;
        up = h.up;
        pose = h.pose;
        return true;
    }
    h.live = false;
    h.miss = 0;
    return false;
}

void NoteShotBox(std::int32_t x_mm, std::int32_t y_mm, std::int32_t z_mm, std::int32_t yaw_mrad, std::uint64_t now_ms) noexcept {
    g_box[0] = static_cast<float>(x_mm) / 1000.f;
    g_box[1] = static_cast<float>(y_mm) / 1000.f;
    g_box[2] = static_cast<float>(z_mm) / 1000.f;
    g_box_yaw = static_cast<float>(yaw_mrad) / 1000.f;
    g_box_ms = now_ms ? now_ms : 1;
}
bool ShotBox(std::uint64_t now_ms, float (&pos)[3], float& yaw) noexcept {
    if (!g_box_ms || now_ms < g_box_ms || now_ms - g_box_ms > kBoxMs) return false;
    std::memcpy(pos, g_box, sizeof(g_box));
    yaw = g_box_yaw;
    return true;
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
void NoteSeatErr(SeatErr& e, const float (&seat)[3], const float (&body)[3]) noexcept {
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(seat[i]) || !std::isfinite(body[i])) return;
    const float dx = body[0] - seat[0], dz = body[2] - seat[2], dy = body[1] - seat[1];
    const float xz = std::sqrt(dx * dx + dz * dz);
    if (xz > e.xz_max) e.xz_max = xz;
    if (std::fabs(dy) > std::fabs(e.dy_max)) e.dy_max = dy;
    ++e.n;
}
void Reset() noexcept {
    g_placed_ms = g_body_ms = g_seat_ms = g_box_ms = 0;
    g_body_ok = false;
}

} // namespace mgscoop::horse_drive
