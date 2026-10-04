#pragma once
// Pure helpers for the offline partner spike receipts compiled by
// lua/coop/partner_sync.lua through the luaL_loadbuffer observer
// (docs/offline-partner-spike-plan.md 2.2). No Win32 calls and no game memory:
// the script observer owns threading, logging and every game access.
//
// Receipts (the chunk name always equals the source, as loadstring sets it;
// at most max_receipt bytes):
//   return 'MGS_COOP_PARTNER_CFG_V1'                      -> return true|false
//   return 'MGS_COOP_PARTNER_V1' -- <frame> <x_mm> <y_mm> <z_mm> <yaw_cdeg> <cam_cdeg> <stance> <move> <luaflags>
//        -> return <op>,<R>,<x_mm>,<y_mm>,<z_mm>,<yaw_mrad>,<status>,<age_ms>   (quiet; per frame)
//   return 'MGS_COOP_PARTNER_LOG_V1' -- <payload>          -> logs partner_lua=<payload>; return true
//   return 'MGS_COOP_PARTNER_STOP_V1'                     -> return true
// Integers only, so neither Lua's nor the C runtime's locale is involved.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/player_sync.hpp"

namespace mgscoop::partner {

enum class Receipt : unsigned char { none, cfg, tick, log, stop };

inline constexpr char cfg_source[] = "return 'MGS_COOP_PARTNER_CFG_V1'";
inline constexpr char stop_source[] = "return 'MGS_COOP_PARTNER_STOP_V1'";
inline constexpr char tick_prefix[] = "return 'MGS_COOP_PARTNER_V1' -- ";
inline constexpr char log_prefix[] = "return 'MGS_COOP_PARTNER_LOG_V1' -- ";
inline constexpr std::size_t max_receipt = 200;
inline constexpr std::size_t max_log_payload = 160;
// Always fits FormatReply output (8 signed 32-bit integers, commas, "return ").
inline constexpr std::size_t reply_capacity = 128;
// Fits FormatLogLine output: "partner_lua=" + payload with ';' -> "; " + CRLF.
inline constexpr std::size_t log_line_capacity = 12 + 2 * max_log_payload + 3;
// Index of the partner's player instance. Offline the local index is 0.
inline constexpr std::uint32_t partner_index = 1;

// op bits returned to Lua.
namespace op {
inline constexpr std::uint32_t warp = 1;
inline constexpr std::uint32_t set_all_active = 2;
inline constexpr std::uint32_t copy_parts = 4;
inline constexpr std::uint32_t clear_all_active = 8;
inline constexpr std::uint32_t set_parts_explicit = 16;  // variant F3b
inline constexpr std::uint32_t mirror_on = 32;           // diagnostic F3c
// Extension (not in the spec's op list): ends the F3c mirror diagnostic. Sent
// natively at most 10 s after MIRROR_ON (or on stop / stage 0), only while
// P+0x204 bit 0x4000 is set and the engine mirror object is non-null.
inline constexpr std::uint32_t mirror_off = 64;
}
// luaflags reported by Lua in every tick.
namespace luaflag {
inline constexpr std::uint32_t all_active = 1;
inline constexpr std::uint32_t parts_copied = 2;
}
// status values returned to Lua.
enum class Status : std::uint8_t { synthetic = 0, live = 1, stale = 2, lost = 3 };

// Exact receipts only (bytewise name == source, NUL-free, size bound).
Receipt Classify(const char* source, std::size_t size, const char* name) noexcept;

struct Tick {
    std::int32_t frame;     // 0..max_frame
    std::int32_t x_mm, y_mm, z_mm;
    std::int32_t yaw_cdeg;  // vars.playerRotY * 100 (degrees)
    std::int32_t cam_cdeg;  // vars.playerCameraRotation[1] * 100
    std::uint8_t stance;    // 0 stand, 1 squat, 2 crawl
    std::uint8_t move;      // 0 none, 1 walk, 2 run, 3 dash
    std::uint8_t luaflags;  // luaflag bits
};
inline constexpr std::int32_t max_frame = 999999;
inline constexpr std::int32_t max_pos_mm = 1000000000;   // matches sync::max_coord_component (1e6 m)
inline constexpr std::int32_t max_angle_cdeg = 3600000;  // +-36000 degrees, normalized later
// Strict: the tick prefix, then exactly 9 signed decimal integers separated by
// single spaces; no '+', no leading zeros, no "-0", nothing trailing, every
// value within its bound. `out` is written only on success.
bool ParseTick(const char* source, std::size_t size, Tick& out) noexcept;

struct Reply {
    std::uint32_t op;
    std::uint32_t index;
    std::int32_t x_mm, y_mm, z_mm;
    std::int32_t yaw_mrad;
    Status status;
    std::uint32_t age_ms;
};
// "return <op>,<R>,<x_mm>,<y_mm>,<z_mm>,<yaw_mrad>,<status>,<age_ms>".
// Returns the length without terminator, or 0 (empty output) if it does not fit.
std::size_t FormatReply(const Reply& reply, char* out, std::size_t capacity) noexcept;

// Copies 1..max_log_payload bytes, replacing anything outside
// [A-Za-z0-9_=:,.;-] with '_' (as partner_sync.lua does before sending).
// Returns the length written without terminator, or 0 when empty/too long.
std::size_t SanitizeLogPayload(const char* payload, std::size_t size, char* out, std::size_t capacity) noexcept;
// For a classified LOG receipt: "partner_lua=<sanitized payload>\r\n" with
// every ';' expanded to "; " (the protocol field separator). Returns the
// length, or 0 for an empty, oversized or non-LOG source.
std::size_t FormatLogLine(const char* source, std::size_t size, char* out, std::size_t capacity) noexcept;

// partner.trigger: one line of whitespace-separated tokens, each at most once:
//   stop | stage=0..3 | source=fixed|orbit|net | apply=warp|slot|both
//   | parts=explicit (one-shot op 16) | mirror=on (one-shot op 32)
// "stop" must be alone. Anything else (unknown token, repeat, empty) is refused.
inline constexpr std::size_t max_trigger = 63;
struct Trigger {
    bool stop;
    bool has_stage;
    std::uint32_t stage;
    bool has_source;
    PartnerSource source;
    bool has_apply;
    PartnerApply apply;
    bool parts_explicit;
    bool mirror_on;
};
bool ParseTrigger(const char* text, std::size_t size, Trigger& out) noexcept;

// Native stage policy (2.2). Stage 0 probe only; 1 WARP; 2 + SET_ALLACTIVE;
// 3 + COPY_PARTS. apply=slot removes WARP (native writes the slot instead);
// WARP and the slot write need a valid pose (source=net: a received packet).
// SET_ALLACTIVE (idempotent in Lua) is sent at stage >= 2 while Lua has not
// latched all-active mode, or when the caller re-asserts it because P+0x204
// bit 0x2000 was lost although Lua latched it (rate-limited by the caller).
// CLEAR_ALLACTIVE is sent instead when the stage is below 2 while Lua still
// reports all-active mode. Set-type one-shots need stage >= 1; MIRROR_OFF is a
// restoration and is sent at any stage.
struct OpInput {
    std::uint32_t stage;
    PartnerApply apply;
    bool pose_valid;
    std::uint32_t luaflags;
    bool reassert_all_active;
    bool parts_explicit;
    bool mirror_on;
    bool mirror_off;
};
std::uint32_t OpsFor(const OpInput& input) noexcept;
bool WritesSlot(std::uint32_t stage, PartnerApply apply, bool pose_valid) noexcept;

// Pose in integer millimetres and milliradians (yaw normalized to [-pi, pi)).
struct Pose {
    std::int32_t x_mm, y_mm, z_mm;
    std::int32_t yaw_mrad;
};
std::int32_t NormalizeMrad(std::int64_t mrad) noexcept;
std::int32_t CdegToMrad(std::int32_t cdeg) noexcept;
// Heading convention [S]: forward = (sin yaw, 0, cos yaw).
// fixed: local position + offset * (sin cam, 0, cos cam), facing the local yaw.
Pose FixedPose(const Tick& tick, std::uint32_t offset_cm) noexcept;
// orbit: radius offset around the local position, 8 s period, yaw tangent.
inline constexpr std::uint32_t orbit_period_ms = 8000;
Pose OrbitPose(const Tick& tick, std::uint32_t offset_cm, std::uint64_t t_ms) noexcept;
// Metres/radians to a clamped integer pose (non-finite values become 0).
Pose PoseFromMetres(float x, float y, float z, float yaw) noexcept;
// Integer Euclidean distance in millimetres (saturating).
std::uint32_t DistanceMm(std::int32_t ax, std::int32_t ay, std::int32_t az,
                         std::int32_t bx, std::int32_t by, std::int32_t bz) noexcept;

// The local pose as a sync transform (metres, radians in [-pi, pi], stance,
// Sprinting when move == 3).
sync::PlayerTransformPacket MakeTransform(const Tick& tick, std::uint32_t sequence, std::uint32_t tick_ms) noexcept;

} // namespace mgscoop::partner
