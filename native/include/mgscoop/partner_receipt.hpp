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
//           followed by ,<type>,<parts>,<camo>,<hand>,<face> when op has SET_PARTS (16) or DM_PLAY (128)
//           (zeros unless SET_PARTS), and then ,<gani> (0..12) when op has DM_PLAY (128)
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
// Always fits FormatReply output (8 signed 32-bit integers, the 5 bounded look
// values, the gani index, commas, "return "): at most 114 characters.
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
// SET_PARTS: Player.SetPartsInfoAtInstanceIndex with the 5 look values that
// follow in the reply (type, parts, camo, hand, face). Was F3b (local vars).
inline constexpr std::uint32_t set_parts_explicit = 16;
inline constexpr std::uint32_t mirror_on = 32;           // diagnostic F3c
// Extension (not in the spec's op list): ends the F3c mirror diagnostic. Sent
// natively at most 10 s after MIRROR_ON (or on stop / stage 0), only while
// P+0x204 bit 0x4000 is set and the engine mirror object is non-null.
inline constexpr std::uint32_t mirror_off = 64;
// Round 3 direct-motion lever (needs the comparator patch at 0x1198E25):
// Player.RequestToPlayDirectMotion{{"coopLoco",1},{GANI[gani],true,"","","",true}}
// with the gani index that follows the 5 look values, and
// Player.RequestToStopDirectMotion().
inline constexpr std::uint32_t dm_play = 128;
inline constexpr std::uint32_t dm_stop = 256;
}
// Highest gani index of the Lua table (partner_sync.lua GANI).
inline constexpr std::uint8_t max_gani = kPartnerDmGaniMax;  // partner_sync.lua GANI 0..12
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

// A player appearance as Player.SetPartsInfoAtInstanceIndex (0x9DF530) takes
// it, plus face_equip (Q+0x68), which no Lua binding sets (written natively).
struct Look {
    std::uint8_t type;   // PlayerType: SNAKE 0, DD_MALE 1, DD_FEMALE 2, AVATAR 3, LIQUID 4, OCELOT 5, QUIET 6
    std::uint8_t parts;  // PlayerPartsType (QUIET 27, OCELOT 26, NORMAL_SCARF 1)
    std::uint8_t camo;   // PlayerCamoType (QUIET 116, OCELOT 115, OLIVEDRAB 0)
    std::uint8_t hand;   // PlayerHandType (NONE 0, NORMAL 1 .. SILVER 7)
    std::uint16_t face;
    std::uint8_t face_equip;
};
inline bool SameLook(const Look& a, const Look& b) noexcept {
    return a.type == b.type && a.parts == b.parts && a.camo == b.camo && a.hand == b.hand && a.face == b.face &&
           a.face_equip == b.face_equip;
}
// Fixed test looks (lab look=quiet / look=snake).
inline constexpr Look quiet_look{6, 27, 116, 0, 0, 0};
inline constexpr Look snake_look{0, 1, 0, 1, 0, 0};
// One validation for every look that reaches SetPartsInfo (received or fixed):
// type > 6 or LIQUID (4) refused ("type"), parts > 27 ("parts"), camo > 116
// ("camo"), hand > 7 ("hand"), and a SNAKE (0) face whose package-table
// index is out of range ("face", see SnakeFaceIndex). QUIET (6) and OCELOT (5) are forced to their own parts/camo with
// hand 0, face 0, face_equip 0 (the 0x9CDB12 keep path); AVATAR (3) becomes
// SNAKE (0) with face 0, face_equip 0 and avatar_as_snake (its 0x4F-byte
// avatar record is neither sent nor settable through SetPartsInfo, and its
// face id is an avatar face, not a Snake face). nullptr means accepted and
// `out` written; otherwise the static reason, `out` untouched.
// The Snake face package selector 0xAE95D0 (called from the parts package
// builder at 0x9B4565 with type, parts, face, face_equip) indexes the 16-byte
// table at 0x2A827D0 with face + 3 when face_equip is 1 or 2 (0xAE962D,
// 0xAE967D..0xAE9687) and no bounds check; the table has snake_face_entries
// path entries (index 6 onward are float constants). face_equip is otherwise
// only compared (==3/4/5 at 0x9B1035, !=0 at 0xAE9360/0xAE9220) and is passed
// through. A DD (1/2) face goes to the staff face database lookup (0x9B45B6)
// and is passed through unchecked (that lookup was not traced).
inline constexpr std::uint32_t snake_face_entries = 6;
inline std::uint32_t SnakeFaceIndex(std::uint16_t face, std::uint8_t face_equip) noexcept {
    return static_cast<std::uint32_t>(face) + ((face_equip == 1 || face_equip == 2) ? 3u : 0u);
}
const char* NormalizeLook(const Look& in, Look& out, bool& avatar_as_snake) noexcept;

struct Reply {
    std::uint32_t op;
    std::uint32_t index;
    std::int32_t x_mm, y_mm, z_mm;
    std::int32_t yaw_mrad;
    Status status;
    std::uint32_t age_ms;
    Look look;  // appended (5 values, no face_equip) when op has set_parts_explicit (zeros with dm_play only)
    std::uint8_t gani;  // appended after the look values when op has dm_play
};
// "return <op>,<R>,<x_mm>,<y_mm>,<z_mm>,<yaw_mrad>,<status>,<age_ms>"
// [",<type>,<parts>,<camo>,<hand>,<face>" when op & (set_parts_explicit | dm_play)]
// [",<gani>" when op & dm_play].
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

// partner.trigger: one line of whitespace-separated tokens, each key at most once:
//   stop | stage=0..3 | source=fixed|orbit|net
//   | apply=warp|slot|both|xform|hybrid|input (move= is the same key; one of the two)
//   | anim=off|mirror|net | aset=q|s|qs|r | reset=n|a|c|ac
//   | look=copy|net|quiet|snake | probe=ps|off
//   | census=1 (one-shot component census) | parts=explicit (one-shot op 16)
//   | mirror=on (one-shot op 32)
// Round 3 (docs/partner-anim-appearance.md "Round 3"):
//   | input=off|pass|neutral|stick|net | stick=<0..359>:<0..100>
//   | tap=0x<hex> | press=0x<hex>:<50..5000>   (one-shots, 1..8 hex digits)
//   | cam=off|detect|sandbox | hold=none|pass | yaw=rot|rotneg|none
//   | words=<3 of 0prhn> | bits=0x<hex> | correct=<0..10000>
//   | isend=0|1 | p218=0|1 | padprobe=0|1 | holdlog=0|1
//   | dm=off|probe|on|map | dmplay=<0..12> | dmstop=1   (dmplay/dmstop one-shots)
//   | dmmap=<12 x 0..12, comma-separated> | dmspeed=<idle>:<run>:<dash>   (dm=map settings)
//   | lookgate=0|1 | lookrecover=0|1
// Round 4 (docs/partner-anim-appearance.md "Round 4"):
//   | hold=none|pass|own|net | recon=off|track|pin | align=<-50..150>
//   | slot=remote|body|none | runaway=pin|disarm|off | wrap=off|census|window
//   | place=1   (one-shot: a new placement)
// Decimal values have no leading zeros. "stop" must be alone. Anything else
// (unknown token, repeat, empty) is refused.
inline constexpr std::size_t max_trigger = 255;
struct Trigger {
    bool stop;
    bool has_stage;
    std::uint32_t stage;
    bool has_source;
    PartnerSource source;
    bool has_apply;
    PartnerApply apply;
    bool has_anim;
    PartnerAnim anim;
    bool has_aset;
    PartnerAset aset;
    bool has_reset;
    PartnerReset reset;
    bool has_look;
    PartnerLook look;
    bool has_probe;
    bool probe_ps;
    bool census;
    bool parts_explicit;
    bool mirror_on;
    // Round 3.
    bool has_input;
    PartnerInput input;
    bool has_stick;
    std::uint32_t stick_deg, stick_pct;
    bool tap;
    std::uint32_t tap_bits;
    bool press;
    std::uint32_t press_bits, press_ms;
    bool has_cam;
    PartnerInputCam cam;
    bool has_hold;
    PartnerInputHold hold;
    bool has_yaw;
    PartnerInputYaw yaw;
    bool has_words;
    std::uint8_t words[3];
    bool has_bits;
    std::uint32_t bits;
    bool has_correct;
    std::uint32_t correct_mm;
    bool has_isend, isend;
    bool has_p218, p218;
    bool has_padprobe, padprobe;
    bool has_holdlog, holdlog;
    bool has_dm;
    PartnerDm dm;
    bool dmplay;
    std::uint8_t dmplay_gani;
    bool dmstop;
    bool has_dmmap;
    PartnerDmTable dmmap;
    bool has_dmspeed;
    PartnerDmSpeeds dmspeed;
    bool has_lookgate, lookgate;
    bool has_lookrecover, lookrecover;
    // Round 4.
    bool has_recon;
    PartnerRecon recon;
    bool has_align;
    std::int32_t align_ms;
    bool has_slot;
    PartnerSlot slot;
    bool has_runaway;
    PartnerRunaway runaway;
    bool has_wrap;
    PartnerWrap wrap;
    bool place;
};
bool ParseTrigger(const char* text, std::size_t size, Trigger& out) noexcept;

// Native stage policy (2.2). Stage 0 probe only; 1 WARP; 2 + SET_ALLACTIVE;
// 3 + COPY_PARTS. apply=slot removes WARP (native writes the slot instead);
// apply=xform/hybrid/input send WARP only when the caller says a full Warp is
// due (transform_warp: hybrid placement/correction, input placement/relink,
// or an xform the native side refused, so the partner never stops moving).
// DM_PLAY (stage >= 1) and DM_STOP (any stage) are the direct-motion one-shots. WARP and the slot write need a
// valid pose (source=net: a received packet).
// SET_ALLACTIVE (idempotent in Lua) is sent at stage >= 2 while Lua has not
// latched all-active mode, or when the caller re-asserts it because P+0x204
// bit 0x2000 was lost although Lua latched it (rate-limited by the caller).
// CLEAR_ALLACTIVE is sent instead when the stage is below 2 while Lua still
// reports all-active mode. COPY_PARTS is sent at stage 3 unless no_copy (an
// explicit look is known: SET_PARTS replaces the copy). Set-type one-shots
// (parts = SET_PARTS due, mirror_on) need stage >= 1; MIRROR_OFF is a
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
    bool no_copy;
    bool transform_warp;
    bool dm_play;
    bool dm_stop;
};
std::uint32_t OpsFor(const OpInput& input) noexcept;
// Native S+0x20 slot write: apply slot, both, xform, hybrid or input (stage >= 1, valid pose).
bool WritesSlot(std::uint32_t stage, PartnerApply apply, bool pose_valid) noexcept;

// Facing as Warp builds it from a yaw (the 0x9C5F80 command handler): the
// quaternion (x, y, z, w) = (0, sin(yaw/2), 0, cos(yaw/2)); and back, as the
// game's 0x1F0300 does: rotate (0, 0, 1) by q and take atan2(x, z).
void YawToQuat(float yaw, float (&quat)[4]) noexcept;
float QuatToYaw(const float (&quat)[4]) noexcept;

// Stance of the anim fields as SetStance takes it (1 stand, 2 squat, 3 crawl;
// 0 = unknown, never applied) and a debounce for applying it on change only:
// a new value must hold for `hold_ms` before it is handed out. The caller
// commits it with CommitStance only when the game call succeeded; a refused
// call keeps the candidate and RetryStance makes it due again after
// `retry_ms`, so a transient refusal (component not ready, parts reloading)
// never leaves the partner in a stale stance.
struct StanceDebounce {
    std::uint8_t applied;    // last value the game accepted (0 = none yet)
    std::uint8_t candidate;  // value currently being held
    std::uint64_t since_ms;  // when candidate was first seen
    std::uint64_t retry_ms;  // no candidate is handed out before this (after a refusal)
};
// Returns the stance to apply now (1..3) or 0. Does not commit it.
std::uint8_t DebounceStance(StanceDebounce& state, std::uint8_t stance, std::uint64_t now_ms, std::uint32_t hold_ms) noexcept;
// The game accepted `stance`: it becomes `applied` and the candidate clears.
void CommitStance(StanceDebounce& state, std::uint8_t stance) noexcept;
// The call for the current candidate was refused: retry it at now + retry_ms
// (a different candidate starts its own hold without the backoff).
void RetryStance(StanceDebounce& state, std::uint64_t now_ms, std::uint32_t retry_ms) noexcept;

// The locomotion ids written to *(Q+0x18)[1] (aset=q): 1..6, the whole domain
// observed live for the local slot (stand/crouch/prone x idle/moving,
// docs/anim-appearance-findings-2026-10-04.md). Other action ids (CQC, climb,
// 0x79 special-cased by the PlayerInfo builder 0x12CE870, ...) are never
// copied onto the partner.
inline constexpr std::uint16_t locomotion_act_min = 1;
inline constexpr std::uint16_t locomotion_act_max = 6;
inline bool LocomotionAct(std::uint16_t act) noexcept { return act >= locomotion_act_min && act <= locomotion_act_max; }

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
