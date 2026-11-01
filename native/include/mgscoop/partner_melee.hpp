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

// The user's decision (melee-one): ONE connecting CQC press knocks the partner down, as it does a soldier.
// Hidden run 1526e58a: ONE press with the partner 1.1 m in front gave 113 (219 ms) then 114 (549 ms), so a 114 straight
// out of a 113 is that press's own strike (counted by `presses`, one per 113 edge): the press lands once, on its 113 or
// on that 114 (run 9018ae47: 113 at 142 cm, its 114 at 84 cm), never twice.
// The 2026-10-31 census of the remote's presses: each press is its own edge into 113 (held 0.2-1.1 s, back to stand
// between presses); its 114 comes straight out of 113 (same tick, held about 0.5 s). A 114 from any other act
// (running 2 -> 114, held up to 3 s) was the start of a CQC takedown of a soldier (the host's Neutralize att=local
// lined up with both), so it is not a strike on the partner.
inline constexpr std::uint16_t kStrikeAct = 113;     // a CQC press's strike (one 113 edge per press)
inline constexpr std::uint16_t kFinisherAct = 114;   // the press's strike out of its 113 (seen within kComboMs)
inline constexpr std::uint32_t kComboMs = 600;       // the last 113 tick to the 114 edge
inline constexpr float kReachM = 1.6f;               // horizontal, body origin to body origin
inline constexpr float kHeightM = 1.0f;              // |y1 - y0|
inline constexpr float kConeCos = 0.5f;              // +-60 degrees around the attacker's facing
inline constexpr std::uint32_t kRepeatMs = 1500;     // one hit per press, at most one per 1.5 s
inline constexpr unsigned kLineCap = 64;             // partner_melee lines per mission
// melee-target: what the stock strike is aimed at. A press aimed at a soldier or a Mother Base staff member is the stock
// CQC acting on that target, which sets PlayerStatus CQC_CONTINUOUS (the tick's st= bit 0x100; set in the CQC plugin's
// target state, which reads the target id). fb1031 (Mother Base): the one 113 -> 114 press on staff showed the bit right
// after its 114 (none during the 113), and three staff takedowns showed it; no press at the partner ever did (runs
// 4bc7b732, 1526e58a, 9018ae47, af0b88b6). So a strike in reach is HELD at its edge: the bit seen since the press's 113
// edge is verdict=target (no hit; the press stays blocked), else it lands kTargetGraceMs after the press's 113 ends (its
// 114 edge), or kHoldCapMs after the held edge. Without the st= census (an old Lua) it lands at once, as before.
inline constexpr std::uint32_t kTargetGraceMs = 250;
inline constexpr std::uint32_t kHoldCapMs = 900;

bool StrikeAct(std::uint16_t act) noexcept;

// repeat: the press already landed, or a land less than kRepeatMs ago; takedown: a 114 without a 113 before it; busy:
// the partner is in a CQC hold (91..96), a trash box or toilet (79..89), on a ladder or pipe (21..24) or a ledge
// (54..56); prone: stance 3 (prone or asleep); hold: in reach, waiting for the stock target (Resolve decides); target:
// the stock CQC acted on another target (a soldier or staff member) during the press: no hit.
enum class Verdict : std::uint8_t { none, land, off, no_local, no_body, range, height, angle, repeat, takedown, busy, prone, hold, target };
bool PartnerBusyAct(std::uint16_t act) noexcept;
const char* VerdictName(Verdict v) noexcept;

struct In {
    bool enabled = false;
    bool local_valid = false;     // the local act and position read this tick
    std::uint16_t act = 0;        // the local player's act (Q+0x18[0])
    float pos0[3]{};              // local body origin (x, y, z), metres
    float yaw0 = 0;               // local body facing, radians: forward = (sin, cos) in (x, z)
    bool body_valid = false;      // body 1 realized, shown, on foot (not seated or riding), its position read back
    float pos1[3]{};              // body 1's drawn position (the body readback, never the seat-stale slot)
    bool remote_valid = false;    // the partner's act and stance from the anim wire this tick
    std::uint16_t remote_act = 0;
    std::uint8_t remote_stance = 0;  // 1 stand, 2 squat, 3 crawl
    std::uint64_t now_ms = 0;
    bool status_valid = false;    // the local PlayerStatus census (the tick's st=) is known
    bool cqc = false;             // CQC_CONTINUOUS this tick: the stock CQC is acting on a target
};
struct State {
    std::uint16_t last_act = 0;
    std::uint64_t last_land_ms = 0;
    bool landed = false;
    bool jab_seen = false;
    std::uint64_t last_jab_ms = 0;  // the last tick the local act was 113
    std::uint32_t presses = 0;       // 113 edges (one per press; the 114 out of a 113 is the same press)
    std::uint32_t landed_press = 0;  // the press of the last land (its other strike edge is no second hit)
    std::uint32_t strikes = 0, lands = 0;
    // melee-target: the current press's stock target signal, and the strike held until it is known.
    std::uint64_t press_ms = 0;      // the current press's 113 edge
    bool cqc_seen = false;           // CQC_CONTINUOUS seen since press_ms
    std::uint64_t cqc_ms = 0;        // its first tick
    bool held = false;
    std::uint32_t held_press = 0;
    std::uint16_t held_act = 0;
    std::uint64_t held_ms = 0;       // the held edge
    bool held_ended = false;         // the local act left 113 since the held edge (its 114 edge)
    std::uint64_t held_end_ms = 0;
    std::int32_t held_dist_cm = 0, held_dy_cm = 0, held_angle_deg = 0;
    bool blocked = false;            // a press decided verdict=target: its other edge is no hit either
    std::uint32_t blocked_press = 0;
    std::uint32_t targets = 0;
};
struct Out {
    Verdict verdict = Verdict::none;  // none: no new strike (or no decision) this tick
    bool queue = false;               // hand one hit to the friendly-hit queue
    std::int32_t dist_cm = 0, dy_cm = 0, angle_deg = 0;
    std::uint16_t act = 0;            // the strike act the line is about (Resolve: the held edge's act)
    std::int32_t hold_ms = -1;        // Resolve: the held edge to the decision (-1: not held)
    std::int32_t cqc_ms = -1;         // the press's 113 edge to CQC_CONTINUOUS (-1: not seen)
    std::int32_t end_ms = -1;         // Resolve: the press's 113 end (its 114 edge) to the decision (-1: still in 113)
};
void Reset(State& s) noexcept;
// Call Resolve, then Step, on every tick: Resolve decides a held strike (land, target, off, no_local; none while it still
// waits), Step judges a new strike edge.
Out Resolve(State& s, const In& in) noexcept;
Out Step(State& s, const In& in) noexcept;
// "partner_melee=strike; act=..; verdict=..; queued=..; dist_cm=..; dy_cm=..; angle_deg=..; strikes=..; lands=..;
//  hold_ms=..; cqc_ms=..; end_ms=..; targets=.."
std::size_t FormatStrike(const State& s, std::uint16_t act, const Out& o, bool queued, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::partner_melee
