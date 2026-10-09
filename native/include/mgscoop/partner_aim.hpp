#pragma once
// Aim parity (docs/native-puppet.md "Facing", "The aim-target mirror"), pure
// part: no Win32 calls and no game memory (mgs_coop_core). partner_aim_point
// writes the target this computes into instance 1's network aim slot inside
// block 1's drain window; partner_player::ReadAim reads the telemetry.
//
// Why (mgsvtpp.exe 1.0.15.4, static analysis; tools/partner_aim_sites.py pins
// every site): every gun state's update (0x108E7D0) recomputes the per-id
// aim parameters r5c/r60 (rate record +0x5C/+0x60) from the gun's aim point
// gunRec+0x150, which gun phase 5 (0x1096F90) rebuilds every frame as a ray
// from the viewer's camera through Q+0x10[id], the per-player network aim
// target. Only the synchronizer feeds it (S_IS_ONLINE), so offline instance
// 1's gun aimed at a fixed far point (the world origin in every capture) and
// the aim-turn predicate 0x10A0AB0 (|(0.5 - r60) * pi| > 80 degrees, called
// by stand/crouch aim idle) started act 12/15 over and over while the look
// slot (our write) pulled the body back. A target 100 m along the remote's
// aim makes the game's own pipeline compute the remote's aim point, pose and
// turn decisions.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::partner_aim {

// The target: the partner's eyes (body + 1.6 m up) plus 100 m along the aim.
inline constexpr double kEyeHeightM = 1.6;
inline constexpr double kTargetDistM = 100.0;
// Hook-side refusals: a coordinate at or beyond 65536 m, a target outside
// 20..200 m of the body, a publication older than 250 ms.
inline constexpr double kMaxCoordM = 65536.0;
inline constexpr double kMinRangeM = 20.0, kMaxRangeM = 200.0;
inline constexpr std::uint64_t kStaleMs = 250;

enum class Source : std::uint8_t { none, look, point };
const char* SourceName(Source source) noexcept;

// Why there is no target (driver) or why the hook did not write (hook). The
// hook latches `fault` for the mission; every other reason is per frame.
//   off       the setting is off (or the debug override)
//   inactive  the native drive is not active, or partner_native_gun=0
//   no_body   instance 1's body position is unread
//   no_look   no live v3 look sample (the remote's xflags Look)
//   range     a non-finite, too far or too near target
//   online    S_IS_ONLINE (P+0x204 bit 8) or the alternate dispatch is set
//   owner     instance 1 is the local player (P+0x214 == 1 or P+0x218 == 1)
//   q_vtable  Q = *(P+0x60) is not the pinned class 0x23AD970
//   slots     Q's vtable +0x18/+0x20/+0x28 are not 0xFE0080/0xFE02F0/0xFDEDD0
//   count     id 1 is not below *(u32)(*(Q+0x1E8)+0x40)
//   array     *(Q+0x10) null or not 16-aligned
//   record    *(Q+0x1F0) null or not 16-aligned
//   stale     the publication is older than kStaleMs (the slot keeps its value)
//   epoch     the publication belongs to a forgotten mission
//   chain     a restore found the chain changed (dropped without writing)
//   shutdown  the game window is closing
//   fault     an access fault (latched until MissionStart)
//   torn      the publication changed while being read (retried next frame)
enum class Reason : std::uint8_t {
    none, off, inactive, no_body, no_look, range, online, owner, q_vtable, slots, count, array, record, stale, epoch, chain,
    shutdown, fault, torn
};
const char* ReasonName(Reason reason) noexcept;

// The direction of a look pair {pitch, yaw} in the convention of 0xFFAC00
// (pitch = atan2(-y, |xz|), yaw = atan2(x, z)) and 0x1042F80:
// (sin yaw cos pitch, -sin pitch, cos yaw cos pitch).
void LookDir(double pitch, double yaw, double (&out)[3]) noexcept;
// The inverse: false for a zero-length or non-finite direction.
bool DirAngles(const double (&d)[3], double& pitch, double& yaw) noexcept;

struct TargetIn {
    PartnerNativeAimPt setting;  // the effective setting (lab.ini or the debug override)
    bool active;                 // the native drive is active with partner_native_gun=1
    bool body_ok;
    float body[3];               // instance 1's T+0x20 (metres)
    bool look_ok;                // a live v3 sample with xflags Look
    float look_pitch, look_yaw;  // the sender's *(PS0+0x290)[local]
    bool point_ok;               // point: the sender's own aim direction (not on the wire yet; v5 is the gun group)
    float point_dir[3];
};
struct TargetOut {
    bool want;    // the mirror should hold a target (false: restore the game's slot)
    bool valid;   // p holds a target
    Source src;
    Reason reason;
    float p[3];
};
// want = setting != off && active. A wanted target without a body or a look
// is not valid (the hook keeps its last write). point uses point_dir when
// point_ok, else the look (src says which).
TargetOut Target(const TargetIn& in) noexcept;
// Finite and every |coordinate| below kMaxCoordM.
bool CoordOk(const float (&p)[3]) noexcept;

// The aim-turn predicate 0x10A0AB0 on a rate record's +0x60 (r60):
// |(0.5 - r60) * (pi/2) * 2| > pi/2 - 10 degrees (false for non-finite).
bool TurnPredicate(float r60) noexcept;
// What it tests, in centidegrees: (0.5 - r60) * 18000 (INT32_MIN when non-finite).
std::int32_t RelYawCdeg(float r60) noexcept;

// The debug command "aimpt off|look|point|lab|status" (a natively handled
// worker/F9 command; "lab" clears the override back to lab.ini).
enum class Command : std::uint8_t { set, clear, status };
struct ParsedCommand {
    Command kind;
    PartnerNativeAimPt value;
};
// True when `text` is an aimpt command at all ("aimpt" or "aimpt ...").
bool IsCommand(const char* text) noexcept;
// False for a non-aimpt text or an argument other than off|look|point|lab|
// status (no argument means status).
bool ParseCommand(const char* text, ParsedCommand& out) noexcept;

} // namespace mgscoop::partner_aim
