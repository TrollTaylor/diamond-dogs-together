#pragma once
// Melee on the partner (docs/native-puppet.md "Melee"): the attacker's copy decides whether its own
// CQC strike (the punch combo, local act 113 / 114: the acts the remote's CQC presses were census'd
// with in the 2026-10-31 two-PC session) reached the partner's body 1, and hands one hit to the
// friendly-hit queue (downed_player::QueueFriendlyHit, cause 2: the victim's own copy applies the
// stock player sleep, which wakes on its own after 30 s or with a revive). The stock strike never
// reaches body 1's damage path (no downed_defense / downed_damage line for any of ~70 close strikes in
// that session), so the reach test is this module's own: horizontal distance, height and a cone
// around the attacker's body facing. Pure and portable: no game access.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_melee {

inline constexpr std::uint16_t kStrikeAct = 113;     // CQC strike (punch combo)
inline constexpr std::uint16_t kStrikeEndAct = 114;  // the combo's last strike
inline constexpr float kReachM = 1.6f;               // horizontal, body origin to body origin
inline constexpr float kHeightM = 1.0f;              // |y1 - y0|
inline constexpr float kConeCos = 0.5f;              // +-60 degrees around the attacker's facing
inline constexpr std::uint32_t kRepeatMs = 1500;     // one hit per strike edge, at most one per 1.5 s
inline constexpr unsigned kLineCap = 64;             // partner_melee lines per mission

bool StrikeAct(std::uint16_t act) noexcept;

enum class Verdict : std::uint8_t { none, land, off, no_local, no_body, range, height, angle, repeat };
const char* VerdictName(Verdict v) noexcept;

struct In {
    bool enabled = false;
    bool local_valid = false;     // the local act and position read this tick
    std::uint16_t act = 0;        // the local player's act (Q+0x18[0])
    float pos0[3]{};              // local body origin (x, y, z), metres
    float yaw0 = 0;               // local body facing, radians: forward = (sin, cos) in (x, z)
    bool body_valid = false;      // body 1 realized with a readable position
    float pos1[3]{};
    std::uint64_t now_ms = 0;
};
struct State {
    std::uint16_t last_act = 0;
    std::uint64_t last_land_ms = 0;
    bool landed = false;
    std::uint32_t strikes = 0, lands = 0;
};
struct Out {
    Verdict verdict = Verdict::none;  // none: no new strike this tick
    bool queue = false;               // hand one hit to the friendly-hit queue
    std::int32_t dist_cm = 0, dy_cm = 0, angle_deg = 0;
};
void Reset(State& s) noexcept;
Out Step(State& s, const In& in) noexcept;
// "partner_melee=strike; act=..; verdict=..; dist_cm=..; dy_cm=..; angle_deg=..; strikes=..; lands=.."
std::size_t FormatStrike(const State& s, std::uint16_t act, const Out& o, bool queued, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::partner_melee
