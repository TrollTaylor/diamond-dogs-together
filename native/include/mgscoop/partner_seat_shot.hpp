#pragma once
// M7 seated shots, S1 (docs/native-puppet.md "Seat shot (S1)"; docs/vehicles-companions-design.md 8.30, 8.31). Pure part: the
// ridden vehicle's box, the plan that moves one bullet's start past it, and the lab lever's parser. No game code.
//
// Why (static study m6m7b/shoot2, [O] = read in mgsvtpp.exe 1.0.15.4, contract-pinned in
// tools/generate_partner_contract.py SEAT_SHOT_SITES):
// - [O] the shot target gunRec+0x140 is Q+0x10[id] (0x108B0FC), which the PlayerCamera update writes for the local
//   player from its look ray (0x111931C); with the passenger camera's handle in CAM+0x1F0 that ray skips the ridden
//   vehicle, so the reticle can be red on a guard behind the jeep;
// - [O] the bullet starts at the weapon's muzzle on the body (GunShotOriginDir 0x1044FF0, called at 0x1041214), which on
//   a seated passenger is low inside the cabin, and flies to that target;
// - [O] the bullet's hit loop skips only the shooter's own character (type 3, sub 9, id == shooter; 0xD2DC60), never a
//   vehicle: the first hit of a line across the jeep is the jeep [I strong; the S1 A/B run is the proof].
// S1: for the local seated passenger only, the RequestBullet slot hook (partner_fire_filter Hook) forwards a COPY of
// the record whose start is moved along its own direction to where the line leaves the ridden vehicle's box (+5 cm),
// never past the aim target (-0.3 m). The caller's record is never written: the model group's shot handler re-uses it
// for every pellet (0x130342E writes each pellet's direction into record+0x10 and calls the slot again, 0x130343C).
// Local only: the shooter's copy decides hits (world sync carries the result); no wire change.
//
// The vehicle frame (the record of partner_vehicle: +0x20 position, +0x2C yaw): forward = (sin yaw, 0, cos yaw), right
// = (-cos yaw, 0, sin yaw) (lua debug goto_vehicle's passenger side, yaw - pi/2), up = +y. The record's position is
// near the ground: the partner_vehicle_door answers put the jeep's front seats 0.75-0.83 m above it and 0.3 m behind
// it (25608), the truck's cab seats 1.59 m above it and 0.93 m ahead of it (25602). The light box hugs the jeep (about
// 5 cm a side, 8.31): a start past a bigger box would skip a wall or rock the jeep stands against, and a smaller one only
// leaves the start inside the jeep (the stock shot). The target clamp keeps every hit at or before the target. A start
// outside the box is never moved: the seated muzzle is always inside, so the pose is stale or wrong.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace mgscoop::partner_seat_shot {

enum class Class : std::uint8_t { none = 0, light = 1, truck = 2 };
inline const char* ClassName(Class c) noexcept {
    switch (c) {
    case Class::light: return "light";
    case Class::truck: return "truck";
    default: return "none";
    }
}
// The vehicle category (record +0x6C >> 4): 1, 2 light vehicles, 3, 4 trucks, as GetVehicleType 0x55E7F0 (cat+0x1B6)
// [O: 25603 and the africa jeep 25608 log cat 1 (runs 66ad131f, 8a920ec8, 41dcbf22); cat 2 and the trucks are I]. A truck seats a passenger only in its cab
// (seat 1); a light vehicle in 1 (front, M6) and the back seats 2 / 3. Anything else: none (no shift).
inline Class ClassFor(std::uint8_t category, std::uint8_t seat) noexcept {
    if (seat < 1 || seat > 3) return Class::none;
    if (category == 1 || category == 2) return Class::light;
    if ((category == 3 || category == 4) && seat == 1) return Class::truck;
    return Class::none;
}

// The box in the vehicle frame: its centre (forward, up offsets from the record) and half extents (forward, side, up).
struct BoxSpec {
    float fwd, up, half_fwd, half_side, half_up;
};
inline constexpr BoxSpec kLightBox{-0.2f, 1.0f, 2.2f, 1.0f, 1.25f};   // 4.4 x 2.0 x 2.5 m (jeep ~4.2 x 1.9 m [I])
inline constexpr BoxSpec kTruckBox{-0.5f, 1.6f, 4.2f, 1.45f, 1.9f};   // 8.4 x 2.9 x 3.8 m
inline const BoxSpec* SpecFor(Class c) noexcept {
    return c == Class::light ? &kLightBox : c == Class::truck ? &kTruckBox : nullptr;
}

inline constexpr float kExitPadM = 0.05f;     // the start lands this far past the box
inline constexpr float kTargetGapM = 0.3f;    // and never closer than this to the aim target
inline constexpr float kMaxShiftM = 9.0f;     // no exit of these boxes is farther (sanity)
inline constexpr std::uint64_t kFreshMs = 50;  // the floor of the freshness window (20 m/s is 1 m in 50 ms)
// The freshness window grows as the ridden vehicle slows (review of 8.33: at 25-30 fps a 50 ms window on a 15.6 ms clock
// judged parked shots stale, and a stale shot is the stock one that hits the jeep): the pose may drift at most kDriftM, the
// window is never under kFreshMs nor over kMaxAgeMs (a driver tick that stopped). Speed unknown (< 0): kFreshMs.
inline constexpr std::uint64_t kMaxAgeMs = 250;
inline constexpr float kDriftM = 1.0f;
inline constexpr std::size_t kRecordBytes = 0x34;  // RequestBullet's slot copy 0xD32690 reads +0x00..+0x32
inline constexpr std::size_t kPos = 0x00, kDir = 0x10;  // record +0x00 start (x, y, z, w), +0x10 direction

// What the driver publishes for the hook (any thread reads it through partner_fire_filter's seqlock).
struct Pub {
    std::uint16_t owner = 0xFFFF;   // the local player's shot owner (record +0x2A = id & 0x1FF, type bits 0)
    std::uint16_t vehicle = 0xFFFF;
    std::uint8_t seat = 0xFF;
    Class cls = Class::none;
    bool apply = false;             // false: plan and count only (the lab census); true: forward the moved copy
    bool target_ok = false;
    float pos[3]{};                 // the vehicle record +0x20
    float yaw = 0;                  // +0x2C, radians
    float target[3]{};              // Q+0x10[local] (the look ray's hit: the shot's target)
    std::uint64_t ms = 0;           // publish time (the hook's clock)
    float speed = -1.0f;            // the vehicle's speed from the driver's last two publications, m/s (-1 unknown)
};
// The longest a publication stays fresh at this speed, ms.
inline std::uint64_t MaxAgeMs(float speed) noexcept {
    if (!std::isfinite(speed) || speed < 0.0f) return kFreshMs;
    const float ms = speed > 1e-3f ? kDriftM / speed * 1000.0f : static_cast<float>(kMaxAgeMs);
    if (!(ms > static_cast<float>(kFreshMs))) return kFreshMs;
    if (ms >= static_cast<float>(kMaxAgeMs)) return kMaxAgeMs;
    return static_cast<std::uint64_t>(ms);
}
inline bool Fresh(std::uint64_t now, std::uint64_t ms, float speed) noexcept { return now >= ms && now - ms <= MaxAgeMs(speed); }

struct Plan {
    // shift (moved and applied) | census (would move; apply off) | class | nonfinite | dir | miss (the line does
    // not cross the box ahead) | outside (the start is not inside the box: a stale or wrong pose) | far | target (no
    // target read) | near (the target is inside the box: nothing left to move).
    const char* result = "unset";
    bool move = false;              // the copy's start changes (shift only)
    bool inside = false;            // the start was inside the box
    float t = 0;                    // the move along the direction, m
    float exit = 0;                 // the line's exit from the box, m (0 when it does not cross)
    float dist = -1;                // |target - start|, m (-1 unknown)
    float o_local[3]{};             // the start in the vehicle frame (forward, right, up) from the record, m
    float d_local[3]{};             // the unit direction in the vehicle frame
    float pos[3]{};                 // the moved start (world)
};

inline bool Finite3(const float* v) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
inline void Axes(float yaw, float f[3], float r[3]) noexcept {
    const float s = std::sin(yaw), c = std::cos(yaw);
    f[0] = s; f[1] = 0.0f; f[2] = c;
    r[0] = -c; r[1] = 0.0f; r[2] = s;
}
// A world vector in the vehicle frame (forward, right, up).
inline void ToLocal(float yaw, const float w[3], float out[3]) noexcept {
    float f[3], r[3];
    Axes(yaw, f, r);
    out[0] = w[0] * f[0] + w[2] * f[2];
    out[1] = w[0] * r[0] + w[2] * r[2];
    out[2] = w[1];
}

// The slab test: the line o + t d (box-centred local coordinates) against half extents h. True when the line meets
// the box at some t > 0; t_in may be <= 0 (the start is inside).
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4723)  // /O2 inlines a constant 0 direction component into the guarded division
#endif
inline bool RayBox(const float o[3], const float d[3], const float h[3], float& t_in, float& t_out) noexcept {
    float lo = -std::numeric_limits<float>::infinity(), hi = std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-6f) {
            if (o[i] < -h[i] || o[i] > h[i]) return false;
            continue;
        }
        float a = (-h[i] - o[i]) / d[i], b = (h[i] - o[i]) / d[i];
        if (a > b) { const float x = a; a = b; b = x; }
        if (a > lo) lo = a;
        if (b < hi) hi = b;
        if (lo > hi) return false;
    }
    if (!(hi > 0.0f)) return false;
    t_in = lo;
    t_out = hi;
    return true;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// One bullet: start and direction from the record, the publication. Never reads anything else.
inline Plan PlanShot(const Pub& p, const float start[3], const float dir[3]) noexcept {
    Plan out{};
    const BoxSpec* spec = SpecFor(p.cls);
    if (!spec) { out.result = "class"; return out; }
    if (!Finite3(start) || !Finite3(dir) || !Finite3(p.pos) || !std::isfinite(p.yaw)) { out.result = "nonfinite"; return out; }
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    if (!(len > 1e-3f)) { out.result = "dir"; return out; }
    const float d[3] = {dir[0] / len, dir[1] / len, dir[2] / len};
    const float rel[3] = {start[0] - p.pos[0], start[1] - p.pos[1], start[2] - p.pos[2]};
    ToLocal(p.yaw, rel, out.o_local);
    ToLocal(p.yaw, d, out.d_local);
    if (p.target_ok && Finite3(p.target)) {
        const float g[3] = {p.target[0] - start[0], p.target[1] - start[1], p.target[2] - start[2]};
        out.dist = std::sqrt(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    }
    const float o[3] = {out.o_local[0] - spec->fwd, out.o_local[1], out.o_local[2] - spec->up};
    const float h[3] = {spec->half_fwd, spec->half_side, spec->half_up};
    float t_in = 0, t_out = 0;
    if (!RayBox(o, out.d_local, h, t_in, t_out)) { out.result = "miss"; return out; }
    out.inside = t_in <= 0.0f;
    out.exit = t_out;
    if (!out.inside) { out.result = "outside"; return out; }
    if (t_out > kMaxShiftM) { out.result = "far"; return out; }
    if (out.dist < 0.0f) { out.result = "target"; return out; }
    float t = t_out + kExitPadM;
    if (t > out.dist - kTargetGapM) t = out.dist - kTargetGapM;
    if (!(t > 0.01f)) { out.result = "near"; return out; }
    out.t = t;
    for (int i = 0; i < 3; ++i) out.pos[i] = start[i] + d[i] * t;
    out.move = p.apply;
    out.result = p.apply ? "shift" : "census";
    return out;
}

// The lab lever "seatshot on|off|lab|status" (lab only): this copy's override of the key (-1 none, 0 off, 1 on).
// False for any other text; for a seatshot command it fills result (ok false on a refusal) and the new override.
inline bool HandleCommand(const char* text, bool lab, int current, int& next, char* result, std::size_t capacity,
                          bool& ok) noexcept {
    next = current;
    ok = false;
    if (!text || std::strncmp(text, "seatshot", 8) != 0 || (text[8] != '\0' && text[8] != ' ')) return false;
    const char* arg = text[8] ? text + 9 : "";
    const char* refusal = nullptr;
    if (!lab) refusal = "lab_only";
    else if (std::strcmp(arg, "on") == 0) next = 1;
    else if (std::strcmp(arg, "off") == 0) next = 0;
    else if (std::strcmp(arg, "lab") == 0) next = -1;
    else if (std::strcmp(arg, "status") != 0) refusal = "args";
    if (refusal) {
        if (result && capacity) std::snprintf(result, capacity, "seatshot=refused; reason=%s; use=on|off|lab|status", refusal);
        next = current;
        return true;
    }
    ok = true;
    if (result && capacity)
        std::snprintf(result, capacity, "seatshot=%s; override=%s", next < 0 ? "lab" : next ? "on" : "off", next < 0 ? "none" : "set");
    return true;
}

} // namespace mgscoop::partner_seat_shot
