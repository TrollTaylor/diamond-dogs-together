#pragma once
// Pure helpers for the offline partner spike receipts compiled by
// lua/coop/partner_sync.lua through the luaL_loadbuffer observer
// (docs/offline-partner-spike-plan.md 2.2). No Win32 calls and no game memory:
// the script observer owns threading, logging and every game access.
//
// Receipts (the chunk name always equals the source, as loadstring sets it;
// at most max_receipt bytes):
//   return 'MGS_COOP_PARTNER_CFG_V1'                      -> return true|false
//   return 'MGS_COOP_PARTNER_V1' -- <frame> <x_mm> <y_mm> <z_mm> <yaw_cdeg> <cam_cdeg> <stance> <move> <luaflags> [<action_flags> <equip_id>]
//        [ ride=<0..5> [ veh=<0..65535> seat=<0..3|255>] [ st=0x<hex>]]
//        -> return <op>,<R>,<x_mm>,<y_mm>,<z_mm>,<yaw_mrad>,<status>,<age_ms>   (quiet; per frame)
//           followed by ,<type>,<parts>,<camo>,<hand>,<face> when op has SET_PARTS (16), DM_PLAY (128)
//           or RIDE_SEAT (512) (zeros unless SET_PARTS), then ,<gani> (0..30) when op has DM_PLAY or
//           RIDE_SEAT (0 unless DM_PLAY), then ,<vehicle>,<seat>,<action>,<entry> when op has RIDE_SEAT
//   return 'MGS_COOP_PARTNER_LOG_V1' -- <payload>          -> logs partner_lua=<payload>; return true
//   return 'MGS_COOP_PARTNER_STOP_V1'                     -> return true
//   return 'MGS_COOP_PARTNER_SEAT_V1' -- arm <seat> <vehicle> | stop
//        -> return true|false (debug seat_dm lever: arms/ends the manual seat; docs/anim-round1-seat.md)
// Integers only, so neither Lua's nor the C runtime's locale is involved.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/player_sync.hpp"

namespace mgscoop::partner {

enum class Receipt : unsigned char { none, cfg, tick, log, stop, seat };

inline constexpr char cfg_source[] = "return 'MGS_COOP_PARTNER_CFG_V1'";
inline constexpr char stop_source[] = "return 'MGS_COOP_PARTNER_STOP_V1'";
inline constexpr char tick_prefix[] = "return 'MGS_COOP_PARTNER_V1' -- ";
inline constexpr char log_prefix[] = "return 'MGS_COOP_PARTNER_LOG_V1' -- ";
inline constexpr char seat_prefix[] = "return 'MGS_COOP_PARTNER_SEAT_V1' -- ";
inline constexpr std::size_t max_receipt = 200;
inline constexpr std::size_t max_log_payload = 160;
// Always fits FormatReply output (8 signed 32-bit integers, the 5 bounded look
// values, the gani index, the 4 RIDE_SEAT values, commas, "return "): at most
// 133 characters with every op bit and every field at its widest
// (tests/partner_receipt_tests.cpp).
inline constexpr std::size_t reply_capacity = 160;
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
// The seated vehicle rider (partner_native_ride=seat, docs/anim-round1-seat.md):
// the vehicle id, native seat, action and entry follow the gani value. action
// 1 plays Player.RequestToPlayDirectMotion{{"coopRide",R},[GetOn],Loop,[GetOff]}
// with the receiver's own locator name of that vehicle, "CNP_ppos_c" and
// "MTP_GLOBAL_C" on every entry; action 2 advances the playing chain to its
// GetOff entry (RequestToGoNextDirectMotion). Never sent with DM_PLAY/DM_STOP.
inline constexpr std::uint32_t ride_seat = 512;
// Cardboard box on body 1 (Player.ChangeEquip, public wrapper 0x9D0D60,
// instance index in R8D). Two values follow a 20-wide reply: the box equip
// id (0x1C8..0x1E2) and 1/0 for toActive. Padded through the seat fields so
// those two values stay at the end whether or not RIDE_SEAT is also set.
inline constexpr std::uint32_t cbox = 1024;
}
namespace seat_action {
inline constexpr std::uint8_t play = 1, next = 2;
}
// RIDE_SEAT entry bits: the chain starts with the seat's GetOn / ends with its GetOff.
// by_id (WS3 B1, docs/vehicle-ws3.md): the native id path is armed for this
// play (realized same-category copy): Lua may play an unnamed vehicle's chain
// untargeted (locator "") instead of refusing no_name; native then targets it.
namespace seat_entry {
inline constexpr std::uint8_t get_on = 1, get_off = 2, by_id = 4;
}
// Highest gani index of the Lua table (partner_sync.lua GANI).
inline constexpr std::uint8_t max_gani = kPartnerDmGaniMax;  // partner_sync.lua GANI 0..25
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
    // Optional suffix: executed gun fire, requested aim/reload (not an
    // assertion that the engine accepted the input), and current gun ID.
    std::uint8_t action_flags;
    std::uint16_t equip_id;
    bool has_ride;
    std::uint8_t ride_kind;  // optional named suffix ride=0..5
    // Optional after ride=: " veh=<id> seat=<s>" (the local vehicle GameObject id,
    // 65535 none, and the local native seat 0..3, 255 unknown) and " st=0x<hex>"
    // (status_bits, docs/anim-round1-seat.md; absent = 0 when ride= is present).
    bool has_vehicle;
    std::uint16_t vehicle;
    std::uint8_t seat;
    bool has_status;
    std::uint16_t status;
    // Optional after st= (or after ride=/seat when st= is absent): " item=<id>".
    bool has_item;
    std::uint16_t item;
};
// The local PlayerStatus census bits of " st=" (lua/coop/partner_ride.lua; a
// status the binding table lacks is never set).
namespace status_bit {
inline constexpr std::uint16_t binocle = 1u << 0, subject = 1u << 1, cbox = 1u << 2, cbox_evade = 1u << 3, trash_box = 1u << 4,
                               trash_box_half_open = 1u << 5, behind = 1u << 6, carry = 1u << 7, cqc_continuous = 1u << 8,
                               on_horse = 1u << 9, on_vehicle = 1u << 10, on_helicopter = 1u << 11;
inline constexpr std::uint16_t all = 0xFFF;
}
// "binocle,subject,..." (or "none") into out; the length written.
std::size_t FormatStatusBits(std::uint16_t bits, char* out, std::size_t capacity) noexcept;
inline constexpr std::int32_t max_frame = 999999;
inline constexpr std::int32_t max_pos_mm = 1000000000;   // matches sync::max_coord_component (1e6 m)
inline constexpr std::int32_t max_angle_cdeg = 3600000;  // +-36000 degrees, normalized later
// Strict: the tick prefix, then exactly 9 or 11 signed decimal integers (11 may end in ride=0..5, optionally
// followed by veh=<0..65535> seat=<0..3|255>, then optionally st=0x<1..3 lowercase hex, at most 0xfff, no
// leading zero>, then optionally item=<0..65534>) separated by single spaces; no '+', no leading zeros, no "-0", nothing trailing, every
// value within its bound. `out` is written only on success.
bool ParseTick(const char* source, std::size_t size, Tick& out) noexcept;
// The debug seat receipt (Receipt::seat): "arm <seat 0..3> <vehicle 0..65534>" or "stop".
struct SeatRequest {
    bool arm;
    std::uint8_t seat;
    std::uint16_t vehicle;
};
bool ParseSeatReceipt(const char* source, std::size_t size, SeatRequest& out) noexcept;
// A LOG receipt whose payload starts "event=ride_seat_refused;id=<vehicle>;seat=<0..3>;" (the
// receiver Lua refused a RIDE_SEAT request): its vehicle and seat.
bool ParseSeatRefusal(const char* source, std::size_t size, std::uint16_t& vehicle, std::uint8_t& seat) noexcept;

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
    Look look;  // appended (5 values, no face_equip) when op has set_parts_explicit (zeros with dm_play/ride_seat only)
    std::uint8_t gani;  // appended after the look values when op has dm_play (0 with ride_seat only)
    // Appended after the gani when op has ride_seat.
    std::uint16_t seat_vehicle = 0;
    std::uint8_t seat = 0, seat_action = 0, seat_entry = 0;
    // Appended after the seat fields when op has cbox (the prefix is padded to 18).
    std::uint16_t cbox_item = 0;
    bool cbox_active = false;
};
// "return <op>,<R>,<x_mm>,<y_mm>,<z_mm>,<yaw_mrad>,<status>,<age_ms>"
// [",<type>,<parts>,<camo>,<hand>,<face>" when op & (set_parts_explicit | dm_play | ride_seat)]
// [",<gani>" when op & (dm_play | ride_seat)] [",<vehicle>,<seat>,<action>,<entry>" when op & ride_seat].
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
//   | dm=off|probe|on|map | dmplay=<0..25> | dmstop=1   (dmplay/dmstop one-shots)
//   | dmmap=<12 x 0..25, comma-separated> | dmspeed=<idle>:<run>:<dash>   (dm=map settings)
//   | lookgate=0|1 | lookrecover=0|1
// Round 4 (docs/partner-anim-appearance.md "Round 4"):
//   | hold=none|pass|own|net | recon=off|track|pin | align=<-50..150>
//   | slot=remote|body|none | runaway=pin|disarm|off | wrap=off|census|window
//   | place=1   (one-shot: a new placement)
// Round-1 actions (docs/anim-round1-actions.md "DM overlay"):
//   | overlay=<partner_dm_overlay entry>|stop | oloop=0|1 | oms=<100..60000>   (one-shots)
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
    // Native puppet (docs/native-puppet.md). defer=on requests deferral but
    // never installs patches by itself; drive=native after a mission latch
    // clears the latch.
    bool has_drive;
    PartnerDrive drive;
    bool has_defer;
    PartnerDefer defer;
    bool has_local;
    PartnerLocal local;
    bool has_naim;
    PartnerNativeAim naim;
    bool has_ngun, ngun;
    bool has_nfire;
    PartnerNativeFire nfire;  // nfire=0|off|1|visual
    bool has_steer;
    std::uint32_t steer_deg;
    // The partner's own reload and weapon switch (docs/native-puppet.md
    // "Weapons"): nreload=mirror|raw|event, nslot=off|write|call (play: call
    // runs as write) and the one-shot rtap=1 (one RELOAD tap through the
    // event machine when its preconditions hold; refused otherwise).
    bool has_nreload;
    PartnerNativeReload nreload;
    bool has_nslot;
    PartnerNativeSlot nslot;
    bool rtap;
    // Round-1 actions (docs/anim-round1-actions.md "DM overlay"; the debug
    // commands overlay_state / overlay_stop): overlay=<partner_dm_overlay
    // catalogue entry>|stop, and with an entry oloop=0|1 (default: the entry's)
    // and oms=<100..60000> (default 3000). One-shots.
    bool overlay, overlay_stop;
    std::uint8_t overlay_index;
    bool has_oloop, oloop;
    bool has_oms;
    std::uint32_t oms;
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
// archive/research-2026-10-23:docs/anim-appearance-findings-2026-10-04.md). Other action ids (CQC, climb,
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
// gun flags/equipment plus the exact reported locomotion class).
sync::PlayerTransformPacket MakeTransform(const Tick& tick, std::uint32_t sequence, std::uint32_t tick_ms) noexcept;

// Partner pack selection receipts (lua/coop/player_package_selection.lua,
// docs/player-package-selection.md), logged by the script observer as
// allocation receipts:
//   return 'MGS_COOP_PACK_SELECTED_V1' -- mission=<code>; single_controller=true; layout=<name>|none
//   return 'MGS_COOP_PACK_REFUSED_V1' -- mission=<code>; reason=<token>; scene=<scene>; path=<path>
// ClassifyPack reads capped diagnostic kind/mission/scene for inspection.
// Native mission feedback uses ClassifyPackState below. Anything malformed is
// none; an absent or unknown diagnostic scene is other.
enum class PackReceipt : std::uint8_t { none, selected, refused, clear };
enum class PackScene : std::uint8_t { none, demo_stage, hangar, story, layout, other };
inline constexpr std::size_t max_pack_receipt = 255;
struct PackResult {
    PackReceipt kind;
    std::uint32_t mission;  // 1..999999, or 0 for compact clear
    PackScene scene;        // refused only (none otherwise)
};
// Legacy capped diagnostic parser; it is not mission state.
PackResult ClassifyPack(const char* source, std::size_t size) noexcept;
// Uncapped per-request state (no paths or free text):
//   return 'MGS_COOP_PACK_STATE_V1' -- mission=<code>; result=clear|selected|refused; scene=none|<scene>
// Clear accepts mission 0 for an invalid/unknown request; other results need
// 1..999999. Unknown fields, scenes, duplicate fields and trailing text refuse.
inline constexpr std::size_t max_pack_state_receipt = 96;
PackResult ClassifyPackState(const char* source, std::size_t size) noexcept;
// Applies only compact state to the pending refusal. Diagnostic receipts and
// malformed state cannot change it. A new clear/selection resets it.
std::uint32_t UpdatePackState(std::uint32_t previous, const char* source, std::size_t size) noexcept;
// "demo_stage", "hangar", "story", "layout", "other" ("" for none).
const char* PackSceneName(PackScene scene) noexcept;
// Inverse of PackSceneName for the five scene names (none otherwise).
PackScene PackSceneFromName(const char* name) noexcept;

} // namespace mgscoop::partner
