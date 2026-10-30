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

// The target: the partner's gun pivot (body + PivotHeight(stance) up) plus
// 100 m along the aim. kEyeHeightM stays the origin of the eye-based telemetry
// (native.aim aim_yaw/aim_pitch).
inline constexpr double kEyeHeightM = 1.6;
inline constexpr double kTargetDistM = 100.0;
// The gun pivot heights of 0x10499D0 (weapon kind 0: stand 0.6 m and crouch
// 0.2 m up, 0.17 m to the side) and prone 0x211C640 (0.5 m ahead, 0 up).
inline constexpr double kPivotStandM = 0.6, kPivotCrouchM = 0.2, kPivotProneM = 0.0;
// gunRec+0x3EB (rate record +0x64 + 1): 1 stand, 2 crouch, 3 prone; anything
// else (unread) aims from the standing pivot.
double PivotHeight(std::uint8_t stance) noexcept;
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
    bool point_ok;               // point: a live v6 sample with GunFlag::AimDir (the sender's own gun direction)
    float point_pitch, point_yaw;  // radians, world (GunDir's convention: pitch > 0 looks down)
    std::uint8_t stance;         // body 1's gunRec+0x3EB (the origin's pivot height; 0 unknown: stand)
};
struct TargetOut {
    bool want;    // the mirror should hold a target (false: restore the game's slot)
    bool valid;   // p holds a target
    Source src;
    Reason reason;
    float p[3];
};
// want = setting != off && active. A wanted target without a body or a look
// is not valid (the hook keeps its last write). point uses the sender's gun
// direction when point_ok, else the look (src says which); both aim from the
// body's pivot height (PivotHeight(stance)).
TargetOut Target(const TargetIn& in) noexcept;
// Finite and every |coordinate| below kMaxCoordM.
bool CoordOk(const float (&p)[3]) noexcept;

// The aim-turn predicate 0x10A0AB0 on a rate record's +0x60 (r60):
// |(0.5 - r60) * (pi/2) * 2| > pi/2 - 10 degrees (false for non-finite).
bool TurnPredicate(float r60) noexcept;
// What it tests, in centidegrees: (0.5 - r60) * 18000 (INT32_MIN when non-finite).
std::int32_t RelYawCdeg(float r60) noexcept;

// --- The gun direction (input frame v6; docs/native-puppet.md "The gun direction")
// The remote's gun does not aim along its camera look: it converges on the
// camera ray's first hit (gunRec+0x150) from a pivot beside the camera ray, so
// near hits (prone, crouched, a camera pointed at the ground) turned the look
// mirror up to 16 degrees off. For stand/crouch the sender inverts its own
// aim pose r5c/r60 with 0x108EC55 and adds its body yaw. Prone uses a separate
// plugin orientation in 0x1043C80 before 0x1042F80, which is not a qualified
// read: refuse that direction and use the existing camera-look fallback.
// Constants: partner_player_contract.hpp (tools/partner_aim_sites.py).
//   none        a direction
//   idle        not aiming (GunFlag::Aiming clear): no group, not a refusal
//   unread      the gun or rate record was not read
//   camera      gunRec+0x3C0 bit 13 (camera-relative angles; class 5 resets r to 0.5)
//   sight       b3d5 1: the local sight writer 0x108F740 maps r to the camera (+-30 deg)
//   class       gunRec+0x3D4 == 3 (0x108ED30, another mapping)
//   stance      gunRec+0x3EB not 1..3
//   range       r5c or r60 not finite or outside 0..1
//   body        no finite body yaw
//   convention  the runtime self-check latched (CheckStep)
//   prone_frame the separate prone orientation is not qualified
//   ride        the local player rides (RideGate): no check, no direction
//   seat        M7 seat gun: a back-seat passenger's direction (GunDirSeated), sent unchecked
enum class DirReason : std::uint8_t { none, idle, unread, camera, sight, gun_class, stance, range, body, convention, prone_frame, ride, seat };
const char* DirReasonName(DirReason reason) noexcept;
struct GunDirIn {
    bool gun_ok;                 // the gun record (class, stance, flags) was read
    std::uint8_t gun_class;      // gunRec+0x3D4
    std::uint8_t stance;         // gunRec+0x3EB
    std::uint8_t b3d5;           // gunRec+0x3D5
    std::uint32_t gun3c0;        // gunRec+0x3C0
    bool rate_ok;                // the rate record was read
    float r5c, r60;              // its +0x5C / +0x60
    bool body_ok;
    float body_yaw;              // radians, the body quaternion's yaw (partner::QuatToYaw)
};
struct GunDirOut {
    bool ok;
    DirReason reason;
    double rel_pitch, rel_yaw;   // radians, the game's inverse (gunRec+0x344/+0x348 for these r)
    double pitch, yaw;           // radians, world (yaw wrapped to (-pi, pi])
};
GunDirOut GunDir(const GunDirIn& in) noexcept;
// M7 seat gun: a back-seat passenger's direction. The stock passenger aims with the standing upper-body set (posture
// 0 over the seated legs; UpdateAttackType has no posture for act 0x6E; run 6fc3098b: posture=0, mode 6/7/9), so its
// stance byte is not an on-foot stance: GunDir with standing in its place. ok gives reason seat. It is sent unchecked:
// CheckStep never runs while the local player rides (RideGate), so a seated aim never latches the self-check.
GunDirOut GunDirSeated(const GunDirIn& in) noexcept;
// The wire's centidegrees: pitch clamped to [-kMaxPitchCdeg, kMaxPitchCdeg],
// yaw normalized to [-18000, 17999] (0 for a non-finite angle).
inline constexpr std::int16_t kMaxPitchCdeg = 9000;
std::int16_t PitchCdeg(double radians) noexcept;
std::int16_t YawCdeg(double radians) noexcept;
inline constexpr double kCdegToRad = 3.14159265358979323846 / 18000.0;
// The angle between two directions given as {pitch, yaw} (radians), in
// centidegrees (INT32_MIN for a non-finite input).
std::int32_t AngleCdeg(double pitch_a, double yaw_a, double pitch_b, double yaw_b) noexcept;
// The sender's runtime self-check: a direction qualifies when the gun aim point
// lies at least kCheckMinM from the body, both r values are inside
// (kCheckEdge, 1 - kCheckEdge) (not clamped) and the reference direction (the
// pivot to the aim point) moved less than kCheckSettleCdeg since the previous
// sample (the pose smoothing lags a moving aim). A qualifying direction more
// than kCheckTolCdeg off the reference is a miss; kCheckLatch consecutive
// misses latch the refusal (convention) until the mission ends (a new DirCheck).
inline constexpr double kCheckMinM = 40.0, kCheckEdge = 0.01;
inline constexpr std::int32_t kCheckTolCdeg = 200, kCheckSettleCdeg = 100;
inline constexpr std::uint32_t kCheckLatch = 20;
struct DirCheck {
    std::uint32_t checks, misses, streak;
    bool latched;
    bool have_prev;
    double prev_pitch, prev_yaw;
    std::int32_t last_err_cdeg;  // the last qualifying sample's error (0 before the first)
};
enum class CheckResult : std::uint8_t { skipped, hit, miss, latched };
// `body` = the body position (T+0x20), `aimpt` = gunRec+0x150, both metres.
// A latched check returns latched without counting.
CheckResult CheckStep(DirCheck& check, const GunDirOut& dir, const GunDirIn& in, const float (&body)[3], const float (&aimpt)[3]) noexcept;
// The local player rides (drives or sits in a vehicle, the helicopter, D-Horse):
// the seated aim pivot never matches the on-foot reference (session 2026-10-22:
// 20 of 20 misses in a jeep and in the helicopter latched the check for the rest
// of the mission). The sender then skips CheckStep and sends no v6 direction
// (DirReason::ride). Every ride edge (on or off) restarts the miss streak and the
// settle reference; it never clears a latch earned on foot. Returns `riding`.
bool RideGate(DirCheck& check, bool riding, bool& was_riding) noexcept;

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
