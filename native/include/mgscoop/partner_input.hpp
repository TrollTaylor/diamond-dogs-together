#pragma once
// Round 3 input lever (docs/partner-anim-appearance.md "Round 3"): the pure
// part of driving instance 1 from its own virtual pad. No Win32 calls and no
// game memory: partner_player reads the pad, partner_input_hook swaps it in
// around block 1's phase-8 dispatch (0x9C2B51), partner_net carries it.
//
// The PlayerPad object PP = *(P+0xB0) (vtable 0x23C8D40, 42 slots) reads the
// raw pad device src = *(PP+8). Its getters, replicated by Effective():
//   +0x20 held  = (src+0x48 ?: PP+0x460) & ~PP+0x28          (0x1205F00)
//   +0x30       = (src+0x4C ?: PP+0x464) & ~PP+0x28          (0x1205DB0)
//   +0x40       = (src+0x50 ?: PP+0x468) & ~PP+0x28          (0x1205E80)
//   +0x58       = (src+0x54 ?: PP+0x46C) & ~PP+0x28          (0x1205EE0)
//   +0x68 X(i)  = 0 if bit i of PP+0x2C; i0: PP+0x488&1 ? PP+0x470 : src+0x58;
//                 i1: PP+0x488&2 ? PP+0x474 : src+0x60       (0x1205F40)
//   +0x70 Y(i)  = same with PP+0x478 / src+0x5C, PP+0x47C / src+0x64 (0x1205FD0)
//   +0x78 T(i)  = i0: &4 ? PP+0x480 : src+0x68; i1: &8 ? PP+0x484 : src+0x6C;
//                 0 if bit i of PP+0x30                      (0x1206060)
// PP+0x10/+0x14/+0x18 are the left stick magnitude, the world move heading
// and the stick direction the PP update derives each frame (0x12070F3):
//   PP+0x10 = deadzoned |stick|,
//   PP+0x14 = src->vt+0x00(src, LX, LY) (call 0x12070E3, store 0x12070F9) =
//             wrap(camera yaw + atan2(LX, LY) - pi) (live: W/D/A/S/idle with
//             cam 30 mrad give 30/-1541/1600/-3112/-3112 mrad),
//   PP+0x18 = atan2f(LX, LY) (0x12070CC, xmm0 = LX, xmm1 = LY).
// Round 4: the window writes PP+0x14 as that heading (it wrote a 0..1 value).
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_support_policy.hpp"

namespace mgscoop::partner_input {

// What partner_player::ReadPad copies (little-endian, as in memory).
struct PadRaw {
    std::uint32_t src_w[4];   // src+0x48 held, +0x4C, +0x50, +0x54
    float src_f[6];           // src+0x58 LX0, +0x5C LY0, +0x60 X1, +0x64 Y1, +0x68 T0, +0x6C T1
    float pp_derived[6];      // PP+0x10 mag, +0x14 force, +0x18 dir, +0x1C, +0x20, +0x24 (right stick)
    std::uint32_t pp_mask[3]; // PP+0x28 buttons, +0x2C sticks, +0x30 triggers
    std::uint32_t kb_w[4];    // PP+0x460..+0x46C (keyboard words)
    float kb_f[6];            // PP+0x470 X0, +0x474 X1, +0x478 Y0, +0x47C Y1, +0x480 T0, +0x484 T1
    std::uint8_t kb_flags;    // byte PP+0x488 (bit 0..3 stick/trigger overrides, 0x10 key bindings)
};

// Sender flag bits (also the input frame flags byte).
namespace Flag {
inline constexpr std::uint8_t Valid = 1 << 0;
inline constexpr std::uint8_t SenderMasked = 1 << 1;    // any of PP+0x28/+0x2C/+0x30 non-zero
inline constexpr std::uint8_t SenderKeyboard = 1 << 2;  // src+0x48 == 0 && PP+0x460 != 0, or PP+0x488 & 0x0F
inline constexpr std::uint8_t Known = 0x07;
}

struct PadSnapshot {
    std::uint32_t held, w4c, w50, w54;
    float lx, ly, rx, ry, lt, rt;
    float mag, force, dir;  // PP+0x10 / +0x14 (heading) / +0x18
    std::uint8_t flags;  // Flag bits (Valid always set)
};
// Replicates the PP getters above exactly (masks included).
PadSnapshot Effective(const PadRaw& raw) noexcept;
// Whether PP+0x18 equals atan2f(lx, ly) (mod 2 pi, within 1e-3) for this
// snapshot: na while mag <= 0.01 or the left stick is masked (PP+0x2C bit 0).
enum class DirState : std::uint8_t { na, ok, bad };
DirState CheckDir(const PadSnapshot& snapshot, std::uint32_t stick_mask) noexcept;
const char* DirStateName(DirState state) noexcept;

// The input frame payload values (player_sync encodes them). Version 2
// (round 4, 48 bytes) adds an edge set with its own sequence and the sender's
// 12-frame hold/tap words; version 1 (40 bytes) is still decoded.
struct InputPacket {
    std::uint8_t version;      // 1 or 2 (decoded); encode always writes 2
    std::uint8_t flags;        // Flag bits, subset of Flag::Known
    std::uint32_t held;        // Effective sticky held (sender's masks applied)
    std::uint32_t w4c;         // v2: "now" (src+0x4C level); v1: OR-latched w4c
    std::uint32_t w50, w54;    // v2: the edge set's press / release; v1: OR-latched words
    std::int16_t lx, ly, rx, ry;  // clamp(v, -1, 1) * 32767; -32768 never used
    std::uint8_t lt, rt;          // clamp(v, 0, 1) * 255
    std::uint16_t lmag, lforce;   // clamp(PP+0x10 / +0x14, 0, 1) * 65535 (lforce: informational)
    std::int16_t ldir;            // PP+0x18 * 10000, |x| <= 31416
    std::int16_t cam_cdeg;        // sender camera yaw in centidegrees, [-18000, 17999]
    // v2 only (0 in a decoded v1 frame).
    std::uint8_t edge_seq;     // 0: no edge set in this frame (w50 = w54 = tap12 = 0); else 1..255
    std::uint8_t edge_rep;     // 0 first send of edge_seq, 1..2 repeats (loss tolerance; saturates at 2 for more repeats)
    std::uint32_t long12;      // level: bits the sender has held for >= 12 frames
    std::uint32_t tap12;       // edge set: bits released after a run of 1..11 frames
    // v3 only (native puppet, docs/native-puppet.md 6.1; 0 otherwise). Each
    // group is 0 unless its xflags bit is set.
    float look_pitch, look_yaw;   // sender *(PS0+0x290)[local]: |pitch| <= 1.6, |yaw| <= pi + 1e-3
    float cam270, cam274;         // sender CAM+0x270 / +0x274: finite, |x| <= 64
    std::int16_t heading_mrad;    // sender PP+0x14 wrapped to (-pi, pi] x 1000, |x| <= 3142
    std::uint8_t xflags;          // XFlag bits
    std::uint8_t inv_slot;        // 0..15, 0xFF unknown
    std::uint16_t equip_rev;      // 0 = no manifest
    // v4 only (native rates and weapons, docs/native-puppet.md "Rates" and
    // "Weapons"; 0 otherwise). Each group is 0 unless its xflags bit is set.
    // Rates: the sender's own +0x48 dash, +0x3C crawl and +0x40 crawl factor
    // rates x 1000 (partner_rates::Wire), each 500..2000 with xflags Rates.
    std::uint16_t rate_dash, rate_crawl, rate_crawl_factor;
    // Weapons: the sender's magazine as its getter reads it and its capacity,
    // with xflags Mag (then 1 <= mag_cap <= kMaxMagCap and mag <= mag_cap).
    std::uint16_t mag, mag_cap;
    // Weapons: the sender's gun record equip id (rec+0x24C), with xflags
    // Equip (then 1..65535; 0 without). The receiver compares it with the
    // equip body 1 carries (the weapon gate).
    std::uint16_t gun_equip;
    // v5 only (docs/native-puppet.md "Weapons", R2/W1; 0 otherwise): the
    // sender's gun state. gun_flags (GunFlag bits; without GunFlag::Valid the
    // other three are 0), its current gun mode rec+0x2C0 (11/12 reload, 8 weapon
    // change, 7 aim), and two counters its tick advances (mod 256): reload_seq
    // on each entry into a reload mode, switch_seq on each change of its
    // current slot. A v5 sender also fills the v3 inv_slot with its current slot.
    std::uint8_t gun_flags, gun_mode, reload_seq, switch_seq;
    // v6 only (docs/native-puppet.md "The gun direction"; 0 otherwise): the
    // sender's gun direction (partner_aim::GunDir: world pitch, > 0 looks down,
    // and world yaw) in centidegrees, with GunFlag::AimDir (which needs Valid and
    // Aiming): pitch within +-partner_aim::kMaxPitchCdeg, yaw [-18000, 17999].
    std::int16_t aim_pitch_cdeg, aim_yaw_cdeg;
    // v7 only (docs/anim-round2-throws.md "WS4 v2"; 0 otherwise): the sender's
    // C4 place word, rec+0x3CC bits 18..23 shifted down (bit 0 the place
    // request, bit 1 the detonate request, bits 2-3 the placed surface class,
    // bits 4-5 the preview class), with GunFlag::Place (which needs Valid and a
    // gun mode 43..47); else 0.
    std::uint8_t place_bits;
};
namespace GunFlag {
inline constexpr std::uint8_t Valid = 1 << 0;      // the group is valid (the sender's gun record was read)
inline constexpr std::uint8_t InHand = 1 << 1;     // rec+0x3C0 bit 2
inline constexpr std::uint8_t Aiming = 1 << 2;     // rec+0x3C0 bit 18
inline constexpr std::uint8_t Reloading = 1 << 3;  // rec+0x2C0 is 11 or 12
inline constexpr std::uint8_t Changing = 1 << 4;   // rec+0x2C0 is 8 or rec+0x3C0 bit 7 (a weapon change)
inline constexpr std::uint8_t Known = 0x1F;        // v5
inline constexpr std::uint8_t AimDir = 1 << 5;     // v6 only: aim_pitch_cdeg / aim_yaw_cdeg hold the gun direction
inline constexpr std::uint8_t KnownV6 = 0x3F;
static_assert((AimDir & Known) == 0 && KnownV6 == (Known | AimDir), "the v6 gun flag is outside the v5 set");
inline constexpr std::uint8_t Place = 1 << 6;      // v7 only: place_bits holds the sender's C4 place word
inline constexpr std::uint8_t KnownV7 = 0x7F;
static_assert((Place & KnownV6) == 0 && KnownV7 == (KnownV6 | Place), "the v7 gun flag is outside the v6 set");
}
// v7: the place word's range and the gun modes that may carry it (the place
// family 43..47: place-hold, ready, end, place, detonate).
inline constexpr std::uint8_t kMaxPlaceBits = 0x3F;
constexpr bool PlaceWireMode(std::uint8_t mode) noexcept { return mode >= 43 && mode <= 47; }
inline constexpr std::int16_t kMaxAimPitchCdeg = 9000;
namespace XFlag {
inline constexpr std::uint8_t Look = 1 << 0;
inline constexpr std::uint8_t Cam = 1 << 1;
inline constexpr std::uint8_t Heading = 1 << 2;
inline constexpr std::uint8_t Aiming = 1 << 3;  // HOLD level
inline constexpr std::uint8_t Known = 0x0F;      // v3
inline constexpr std::uint8_t Rates = 1 << 4;    // v4 only: rate_dash / rate_crawl / rate_crawl_factor
inline constexpr std::uint8_t Mag = 1 << 5;      // v4 only: mag / mag_cap
// v4 only: the sender's PlayerPad is in gamepad mode (PP+0x488 bit 6 clear,
// PP vt+0x98 false). Absent: keyboard semantics (the trigger's history path
// then uses the single held word, 0x1096192).
inline constexpr std::uint8_t Gamepad = 1 << 6;
inline constexpr std::uint8_t Equip = 1 << 7;    // v4 only: gun_equip
inline constexpr std::uint8_t KnownV4 = 0xFF;
static_assert((Rates & Known) == 0 && (Mag & Known) == 0 && (Rates & Mag) == 0 && (Gamepad & (Known | Rates | Mag)) == 0 &&
                  (Equip & (Known | Rates | Mag | Gamepad)) == 0 && KnownV4 == (Known | Rates | Mag | Gamepad | Equip),
              "v4 xflags are distinct and outside the v3 set");
}
inline constexpr std::uint16_t kMaxMagCap = 999;
inline constexpr float kMaxLookPitch = 1.6f;
inline constexpr float kMaxLookYaw = 3.14259265f;  // pi + 1e-3
inline constexpr float kMaxCamField = 64.0f;
inline constexpr std::int16_t kMaxHeadingMrad = 3142;
inline constexpr std::uint8_t kInvSlotUnknown = 0xFF;
inline constexpr std::uint8_t kInvSlotMax = 15;
// Repeats of an edge set after its first send: kEdgeRepeats by default,
// SenderEdges::repeats (lab.ini partner_input_edge_repeats, play 5) up to
// kEdgeRepeatsMax. The wire edge_rep saturates at kEdgeRepeats (what a
// 0.11.1 receiver accepts); receivers tolerate up to kEdgeRepeatsMax.
inline constexpr std::uint8_t kEdgeRepeats = 2;
inline constexpr std::uint8_t kEdgeRepeatsMax = 8;
inline constexpr std::uint32_t kHoldFrames = 12;  // classify(btn, 12) at every stance/subject/action site
inline constexpr std::int16_t kMaxDir = 31416;
inline constexpr std::int32_t kCamMin = -18000, kCamMax = 17999;
// Normalizes centidegrees to [-18000, 17999].
std::int32_t NormalizeCdeg(std::int64_t cdeg) noexcept;
// The clamps above; non-finite values become 0.
InputPacket Quantize(const PadSnapshot& snapshot, std::int32_t cam_cdeg) noexcept;
// Whether every value is inside the wire ranges (player_sync checks the same).
// Versions 1..7; a v1/v2 packet carries no v3 field, a v1..v3 packet no v4
// field (xflags Rates and Mag are v4 only), a v1..v4 packet no gun group, a
// v1..v5 packet no gun direction (GunFlag::AimDir is v6 only) and a v1..v6
// packet no place word (GunFlag::Place is v7 only).
bool ValidPacket(const InputPacket& packet) noexcept;
// The sender's v3 fields (read on the driver tick; invalid groups have their
// valid flag clear). Fill3 sets version 3 and the v3 fields of a v2 packet.
struct V3Source {
    bool look_valid;
    float look_pitch, look_yaw;
    bool cam_valid;
    float cam270, cam274;
    bool heading_valid;
    float heading_rad;
    bool aiming;
    std::uint8_t inv_slot;
    std::uint16_t equip_rev;
};
void Fill3(InputPacket& packet, const V3Source& source) noexcept;
// Version 4 (native rates and weapons): Fill3's fields plus the sender's
// rates (xflags Rates, when rates_valid and each is within 500..2000 milli)
// and its magazine (xflags Mag, when mag_valid, 1 <= mag_cap <= kMaxMagCap and
// mag <= mag_cap). An invalid group leaves its flag clear and its fields 0.
struct V4Source {
    V3Source v3;
    bool rates_valid;
    std::uint16_t rate_dash, rate_crawl, rate_crawl_factor;
    bool mag_valid;
    std::uint32_t mag, mag_cap;
    bool gamepad;         // xflags Gamepad
    bool equip_valid;     // xflags Equip when equip is 1..65535
    std::uint32_t equip;
};
void Fill4(InputPacket& packet, const V4Source& source) noexcept;
// Version 5 (the partner's own reload and weapon switch): Fill4's fields plus
// the sender's gun group (GunFlag::Valid only when gun_valid; flags outside
// GunFlag::Known are dropped). The sender's current slot goes into v4.v3.inv_slot.
struct V5Source {
    V4Source v4;
    bool gun_valid;
    std::uint8_t gun_flags, gun_mode, reload_seq, switch_seq;
};
void Fill5(InputPacket& packet, const V5Source& source) noexcept;
// Version 6 (the gun direction): Fill5's fields plus the sender's gun
// direction (GunFlag::AimDir) when aim_valid, the gun group is valid with
// GunFlag::Aiming and both angles are inside the wire ranges; else the group
// stays clear and the fields 0 (the receiver then aims along the look).
struct V6Source {
    V5Source v5;
    bool aim_valid;
    std::int16_t aim_pitch_cdeg, aim_yaw_cdeg;
};
void Fill6(InputPacket& packet, const V6Source& source) noexcept;
// Version 7 (WS4 v2, the C4 place and detonate): Fill6's fields plus the
// sender's place word (GunFlag::Place) when place_valid, the gun group is
// valid, its mode is in the place family (PlaceWireMode) and the word fits
// kMaxPlaceBits; else the flag stays clear and place_bits 0.
struct V7Source {
    V6Source v6;
    bool place_valid;
    std::uint8_t place_bits;
};
void Fill7(InputPacket& packet, const V7Source& source) noexcept;

// Sender hold (docs/anim-round2-throws.md "Unknown-slot glitch", run 0c9269ce).
// The v4/v5 weapon fields come from two reads of the local player per tick: its
// gun record (ReadGun k=0 with its fields: the equip, the magazine and the v5 gun
// group) and its slot row (ReadSlots: inv_slot). One failed gun read used to send
// a frame with neither XFlag::Equip nor GunFlag::Valid, which the receiver reads as
// remote_slot=255 / remote_equip unknown (its remote_v5 needs GunFlag::Valid) and
// which aborted a held throw. A failed read now republishes the last good read's
// fields for up to kSenderHoldMs; a good read (whatever it says, an empty hand
// included) always replaces them, and beyond the bound the unknown goes out.
inline constexpr std::uint64_t kSenderHoldMs = 250;
struct SenderHold {
    bool gun;                     // a good gun read was recorded
    std::uint64_t gun_ms;
    bool equip_valid, mag_valid, gun_valid;
    std::uint32_t equip, mag, mag_cap;
    std::uint8_t gun_flags, gun_mode, reload_seq, switch_seq;
    bool slot;                    // a good slot read was recorded
    std::uint64_t slot_ms;
    std::uint8_t inv_slot;
    bool gun_holding, slot_holding;   // the current held runs (log transitions)
    std::uint64_t gun_since_ms, slot_since_ms;
    std::uint32_t gun_held, slot_held;
};
enum class HoldEvent : std::uint8_t { none, start, held, resumed, expired };
const char* HoldEventName(HoldEvent event) noexcept;
struct SenderHoldOut {
    HoldEvent gun, slot;
    std::uint32_t gun_ms, slot_ms;     // the age of the good read at start, the run's length at resumed/expired
    std::uint32_t gun_held, slot_held; // frames the run republished
};
// Whether a failed read at now_ms is still inside the hold of the last good one.
bool SenderHoldLive(bool have, std::uint64_t good_ms, std::uint64_t now_ms) noexcept;
// v: the frame's sources as this tick read them; gun_read / slot_read: whether
// the gun record (with its fields) and the slot row were read. Records the good
// parts and fills the failed parts from the hold while it is live.
SenderHoldOut HoldSender(SenderHold& hold, V5Source& v, bool gun_read, bool slot_read, std::uint64_t now_ms) noexcept;

// Sources of the three edge words src+0x4C/+0x50/+0x54 inside the window.
enum class Word : std::uint8_t { zero = kPartnerWordZero, press = kPartnerWordPress, release = kPartnerWordRelease,
                                 held = kPartnerWordHeld, net = kPartnerWordNet };

struct Config {
    PartnerInput mode;
    std::uint32_t mask;        // only these held/word bits reach instance 1
    Word words[3];
    std::int8_t yaw_sign;      // +1 rot, -1 rotneg, 0 none
    bool rstick, triggers;     // pass the partner's right stick / triggers (net only)
    std::uint16_t stick_deg;   // stick mode
    std::uint8_t stick_pct;
    std::uint32_t stale_ms;    // net: older input is treated as neutral
    // net: a stall hold (partner_native_stall=hold): a sample older than
    // stale_ms but at most stall_ms keeps its sticks, look and gun fields (no
    // new edges; held bits follow the stale grace). 0 or <= stale_ms: off.
    std::uint32_t stall_ms;
    // Bits removed from the remote's sample only (its held level, words,
    // edges, long12 and tap12), never from the overlay: RELOAD with
    // partner_native_reload=event, whose reloads arrive as overlay taps. Not
    // compared by SameArm (a change never resets instance 1's private masks).
    std::uint32_t sample_strip;
};
// The newest received input (partner_net::LatestInput / ConsumeInput). The
// edge fields are the edge sets accumulated since the last ConsumeInput (by
// edge_seq; a v1 frame contributes its w50/w54 every time); they are 0 in
// LatestInput samples.
struct Sample {
    bool valid;
    std::uint32_t seq;
    std::uint32_t age_ms;
    InputPacket packet;
    std::uint32_t edge_press, edge_release, edge_tap12;
};
// Receiver side (partner_net): edge sets of accepted frames, accumulated
// until the window consumes them. A v2 frame adds its press/release/tap12
// once per new edge_seq (0 adds nothing, a repeat of the last seq adds
// nothing); a v1 frame adds its w50/w54 every time.
struct InputEdges {
    std::uint32_t press, release, tap12;
    std::uint8_t last_seq;  // 0 = none seen
    std::uint32_t sets;     // edge sets accepted (statistics)
};
void AccumulateEdges(InputEdges& edges, const InputPacket& packet) noexcept;
// Receiver side (partner_net, under its lock, before an accepted frame is stored):
// the unknown remote slot/equip debounce (partner_support_policy::StepIdentity).
// support_equip: the driver's last support verdict (0 = none). A held frame gets
// slot 3, XFlag::Equip with that equip and, when it lacked GunFlag::Valid, the gun
// group of the last complete frame (GunFlag::AimDir and GunFlag::Place dropped:
// their angles and place word are not carried); its input (held bits, sticks,
// edges) is untouched. Returns the verdict.
struct IdentityHold {
    partner_support_policy::IdentityTrack track;
    std::uint8_t gun_flags, gun_mode, reload_seq, switch_seq;  // the last complete frame's gun group
};
partner_support_policy::Identity PacketIdentity(const InputPacket& packet) noexcept;
partner_support_policy::IdHold HoldIdentity(IdentityHold& hold, InputPacket& packet, std::uint16_t support_equip,
                                            std::uint64_t now_ms) noexcept;
// Hook-thread only; survives Arm gen bumps (see Arm).
//   net_held: the masked net held level of the last live window. A stall
//     (stale input) keeps it for kStaleGraceMs without edges (the hold history
//     is frozen), then releases it silently (no release edge; the history
//     restarts), so one network hiccup never toggles crouch/stand.
//   resync: set by the hook for the first window after a disarm. That window
//     injects the current level without edges (prev_held = held) and drops the
//     edge sets accumulated while disarmed, so stale taps are never replayed.
struct EdgeState {
    std::uint32_t prev_held;
    std::uint32_t last_seq;
    std::uint32_t net_held;
    bool resync;
    std::uint32_t prev_overlay; // explicit one-shots press even a remotely held bit
};
// How long a stall keeps the last live held level (age of the newest frame).
inline constexpr std::uint32_t kStaleGraceMs = 1000;
// A main window this long after the previous one (skips, pass mode) resyncs
// like the first window after a disarm.
inline constexpr std::uint64_t kResyncGapMs = 250;
// Held bits added by the tap/press one-shots this frame.
struct Overlay {
    std::uint32_t bits;
};
struct Injection {
    bool write;   // false: pass-through (mode pass/off)
    std::uint32_t held;
    std::uint32_t w[3];  // src+0x4C, +0x50, +0x54
    float lx, ly, rx, ry, lt, rt;
    float mag, heading, dir;   // PP+0x10/+0x14/+0x18 (heading = wrap(host cam + dir - pi))
    float rmag, rforce, rdir;  // PP+0x1C/+0x20/+0x24
    bool stale;
    // The edges of this window independent of the words setting (the
    // key-manager stubs answer pressed/released from these), the sender's
    // 12-frame words (hold=net) and whether the net input was live.
    std::uint32_t edge_press, edge_release;
    std::uint32_t long12, tap12;
    bool live;
    std::uint32_t synth;  // bits synthesized this window from a sub-sample tap
    // Hold history control for the hook: freeze (a stall keeps the level, the
    // run does not grow) and reset (a silent release or a resync: restart).
    bool hist_freeze, hist_reset;
    bool resynced;        // this window was a resync (edges dropped)
    // Native puppet: the live v3 sample's look (xflags Look) and camera fields
    // (xflags Cam); absent otherwise.
    bool look_valid, cam_valid, aiming;
    float look_pitch, look_yaw, cam270, cam274;
    // Native weapons: the live v4 sample's magazine and capacity (xflags Mag); absent otherwise.
    bool ammo_valid;
    std::uint16_t ammo, ammo_cap;
    // Native weapons: the live v4 sample's pad mode (xflags Gamepad; false =
    // keyboard semantics) and the sender's gun equip (xflags Equip).
    bool gamepad;
    bool equip_valid;
    std::uint16_t equip;
    // Native weapons (v5): the live sample's gun group (gun_valid: GunFlag::Valid)
    // and the sender's current slot (slot_valid: a v5 frame with inv_slot <= 15).
    bool gun_valid;
    std::uint8_t gun_flags, gun_mode, reload_seq, switch_seq;
    bool slot_valid;
    std::uint8_t inv_slot;
    // WS4 v2 (v7): the live sample's C4 place word (place_valid: GunFlag::Place).
    bool place_valid;
    std::uint8_t place_bits;
    // Native support (partner_support_policy StepFrame, critic #4): this frame
    // is a stall freeze of the last admitted support frame (its HOLD|FIRE
    // level, no edges); a consumer admits it with freshness waived only, every
    // other support gate rechecked. Copied by LevelOnly/Strip, cleared by Neutral.
    bool support_frozen;
};
// One window's injected pad (rules in partner_input.cpp). Updates `edge`.
// Held bits: overlay | (live net held); a bit that was pressed and released
// between two samples (sample edge set, or tap12) and is in neither this nor
// the previous window's held is injected for exactly this window (the next
// window then carries its release edge).
// Native puppet (docs/native-puppet.md 5.6): steer_rad is added to the stick
// direction of a live net sample (positive = left = world heading up) and
// mag_scale scales its radius and PP+0x10 magnitude (clamped to 0..1).
Injection Compose(const Config& config, const Sample& sample, std::int32_t host_cam_cdeg, Overlay overlay,
                  EdgeState& edge, double steer_rad = 0.0, double mag_scale = 1.0) noexcept;
// wrap(cam + atan2(lx, ly) - pi) in radians, atan2(0, 0) = 0 (PP+0x14).
double Heading(double cam_rad, float lx, float ly) noexcept;
// The level-only copy of a window's injection for the wrap windows of the
// same frame (and later frames until the next main window): held words and
// sticks stay, every edge goes (press/release words, net words, the
// key-manager press/release answers, tap12), so an edge consumer in a wrapped
// sub-phase never sees an edge twice.
Injection LevelOnly(const Injection& injection, const Config& config) noexcept;

// --- Native puppet: one frame injection, consumed once per consumer ----------
// The main window (phase 8) composes the frame injection; each consumer that
// runs block-1 code in the same frame (the drains of the two batch passes, the
// phase-7 walk; walk11 only runs in frames without drains) gets its edges on
// first use and the level afterwards, so every edge is delivered once per
// consumer as the game's own once-per-frame latch would.
enum class Consumer : std::uint8_t { main, drain0, drain1, walk7, walk11 };
inline constexpr std::size_t kConsumers = 5;
// An injection older than this is not replayed: the consumer gets Neutral().
inline constexpr std::uint64_t kFrameMaxAgeMs = 100;
enum class ConsumerView : std::uint8_t { full, level, neutral };
// write stays, every level, stick, edge and word is zero (stale set).
Injection Neutral(const Injection& frame) noexcept;
// The injection without `bits` anywhere (held, the three words, the edges,
// long12, tap12 and the synthesized taps): the weapon gate's FIRE | RELOAD.
Injection Strip(const Injection& injection, std::uint32_t bits) noexcept;
// Native support stall freeze (critic #4): `bits` held exactly at `level`
// (within the mask) with no edge of them anywhere (the three words by their
// setting, the edges, tap12 and the synthesized taps; long12 is a level and a
// stale frame carries none).
Injection Freeze(const Injection& injection, std::uint32_t bits, std::uint32_t level, const Config& config) noexcept;
// Native support FIRE buffer (C(c)): `held` added to the level, `press` and
// `release` added as edges, each word by its setting (net words unchanged).
// A bit added as held or press is never also a release edge of the same frame
// (a delivered tap's own release, which Compose emits one frame after the
// synthesized tap, is cleared here; the buffer sends it on the next frame).
Injection AddEdges(const Injection& injection, std::uint32_t held, std::uint32_t press, std::uint32_t release,
                   const Config& config) noexcept;
// age_ms > kFrameMaxAgeMs: Neutral; frame_id != last_used: the full frame
// (last_used = frame_id); otherwise LevelOnly. `view` (optional) tells which.
Injection ForConsumer(const Injection& frame, std::uint32_t frame_id, std::uint32_t& last_used, std::uint64_t age_ms,
                      const Config& config, ConsumerView* view = nullptr) noexcept;

// --- Hold classifier replica (hold=own) --------------------------------------
// src->vt+0x10 = 0x434ED0(src, btn, frames), verified: bit = 1 << (btn & 31);
// released = src+0x54 & bit; if !released && !(src+0x48 & bit) -> 0; walk the
// newest-first u32 history *(src+8) for i < n = src+0x40: skip leading zeros,
// count the first run (newest = the run includes entry 0), stop at count >=
// frames or at the first zero after the run; then released && count < frames
// -> 2 (tap); newest -> count < frames ? 1 (undecided) : 3 (hold); else 0.
// PP vt+0x90 (0x1205C30) first returns 0 when the button is masked
// (bt [PP+0x28], btn at 0x1205C37).
inline constexpr std::uint32_t kHoldHistory = 128;
inline constexpr std::uint32_t kHoldHistoryUsed = 120;  // the src history length (ctor arg 0x78 at 0x120617F)
struct HoldHistory {
    std::uint32_t ring[kHoldHistory];
    std::uint32_t head;   // index of the newest entry
    std::uint32_t count;  // valid entries (missing ones read as 0, like the zeroed game history)
};
void HoldPush(HoldHistory& history, std::uint32_t held) noexcept;
std::uint32_t HoldAt(const HoldHistory& history, std::uint32_t index_newest_first) noexcept;
std::uint32_t ClassifyHold(const HoldHistory& history, std::uint32_t held_word, std::uint32_t release_word, std::uint32_t btn,
                           std::uint32_t frames) noexcept;
// hold=net for frames == kHoldFrames with a live sample: tap12 -> 2, held &&
// long12 -> 3, held -> 1, else 0. Returns kHoldUseOwn when it does not apply.
inline constexpr std::uint32_t kHoldUseOwn = 0xFFFFFFFFu;
std::uint32_t NetHoldCode(bool live, std::uint32_t held, std::uint32_t long12, std::uint32_t tap12, std::uint32_t btn,
                          std::uint32_t frames) noexcept;

// --- PC key bindings ----------------------------------------------------------
// PP+0x390[i] (26 handles) are the key-manager handles of binding i, which the
// PP update maps to bit (1 << i) of PP+0x460..0x46C (0x1206C21..0x1206CF2,
// handles compared masked to 48 bits; 0xB8A0BF169F98 = unbound). The PP
// key-manager slots (+0xE8..+0x140) query the host keyboard directly; the
// window answers them from the injected words through these lookups.
inline constexpr std::uint32_t kBindingCount = 26;
inline constexpr std::uint64_t kHandleMask = 0xFFFFFFFFFFFFull;
inline constexpr std::uint64_t kUnboundHandle = 0xB8A0BF169F98ull;
// The binding index of `handle` (first match), or -1 (unbound or unknown).
int BindingIndex(const std::uint64_t (&handles)[kBindingCount], std::uint64_t handle) noexcept;

// --- Sender edge sets (input frame v2) ----------------------------------------
// Fed with every tick's snapshot (sent or not): press/release edges are the
// snapshot's w50/w54 OR the edges derived from consecutive held samples; a bit
// released after a run of 1..11 ticks joins tap12; long12 is the level of bits
// held for >= 12 ticks. SenderFill() puts the pending edge set into a packet:
// a new set gets a new edge_seq (1..255) and is repeated kEdgeRepeats more
// times unless newer edges start the next set; SenderSent() commits a
// successful submit (a refused submit sends the same content next time).
struct SenderEdges {
    std::uint32_t last_held;
    bool have_last;
    std::uint32_t press, release, tap12;  // latched, not yet in a set
    std::uint8_t run[32];
    std::uint32_t long12;
    std::uint8_t seq;                     // last edge_seq used (0 = none yet)
    std::uint8_t reps_left;               // repeats still due for `seq`
    std::uint32_t cur_press, cur_release, cur_tap12;  // the set being sent/repeated
    bool filled_new;                      // the last SenderFill() opened a new set
    std::uint8_t repeats;                 // repeats per set (0: kEdgeRepeats; at most kEdgeRepeatsMax)
};
void SenderObserve(SenderEdges& state, const PadSnapshot& snapshot) noexcept;
// The effective repeats per set (state.repeats clamped; 0 means kEdgeRepeats).
std::uint8_t SenderRepeats(const SenderEdges& state) noexcept;
void SenderFill(SenderEdges& state, InputPacket& packet) noexcept;
void SenderSent(SenderEdges& state) noexcept;

// Tap/press one-shots, evaluated once per hook window: a new tap sequence
// gives its bits for kTapFrames windows; a press gives its bits while
// now_ms < press_until_ms.
inline constexpr std::uint32_t kTapFrames = 4;
struct TapState {
    std::uint32_t last_seq;
    std::uint32_t frames_left;
    std::uint32_t bits;
};
std::uint32_t StepOverlay(TapState& state, std::uint32_t tap_seq, std::uint32_t tap_bits, std::uint32_t press_bits,
                          std::uint64_t press_until_ms, std::uint64_t now_ms) noexcept;

// Integer conversions for log lines (x1000, non-finite -> INT32_MIN).
std::int32_t Milli(float value) noexcept;
// Radians normalized to (-pi, pi].
double NormalizeRadians(double radians) noexcept;

// --- Arm: what the driver publishes to the hook (seqlock in the hook) -------
// POD only. A change of any field bumps `gen`, which makes the hook reset
// instance 1's private pad masks. The edge state, the tap state and the last
// sample survive a gen bump on purpose: resetting them replayed the last tap
// (tap_seq is cumulative) and turned a held bit into a synthetic press. The
// driver's tap_seq is process-lifetime, so a new mission never reuses a value.
struct Arm {
    std::uint32_t gen;
    bool enabled;   // the block-1 window runs
    bool holdlog;   // block 0's hold classifier is logged (I5)
    bool p218;      // diagnostic P+0x218 = 1 inside the window (I3x)
    PartnerInputCam cam;
    PartnerInputHold hold;
    PartnerWrap wrap;
    std::uintptr_t P, pp, src, cam_object, root0, root1, blk1, rec1, plugin1;
    std::uintptr_t p_vtable, pp_vtable, cam_vtable;
    std::uintptr_t plugin_vt[4];
    std::uint8_t pool1;
    Config config;
    std::int32_t host_cam_cdeg;
    std::uint32_t tap_seq, tap_bits, press_bits;
    std::uint64_t press_until_ms;
    // Native puppet (docs/native-puppet.md 5): native windows (frame injection
    // per consumer, HOLD latch, svc entries never window, look write), isLocal
    // in walk windows (local=all; the main window and drains always set it),
    // the look override, the GO identity and
    // instance 1's look slot (validated on the driver tick, re-checked in the
    // hook), and the steering applied to the composed stick.
    bool native;
    bool local_all;
    PartnerNativeAim naim;
    std::uintptr_t go, ps1;
    std::uint32_t ps1_idx;
    double steer_rad;   // not compared by SameArm (like host_cam_cdeg)
    double mag_scale;   // not compared by SameArm; 0 is treated as 1
    // Native rates (docs/native-puppet.md "Rates"; not compared by SameArm):
    // mirror the remote's wire rates {dash, crawl, crawl factor} into instance
    // 1's own rate record `rates_rec` of `rates_sp` (id rates_id, validated on
    // the driver tick, re-validated in the drain window) for this epoch;
    // rates_mirror false restores what the mirror changed.
    bool rates_mirror;
    std::uintptr_t rates_sp, rates_rec;
    std::uint32_t rates_id, rates_epoch;
    std::uint16_t rates_remote[3];
    // Native weapons (docs/native-puppet.md "Weapons"), not compared by
    // SameArm: the magazine mirror of drain windows (mag_mirror) on instance
    // 1's gun record gun_rec of prim gun_prim (both from the driver tick's
    // ReadGun1; the hook re-validates prim's vtable, prim+8 == blk1 and
    // prim+0x58 == gun_rec before any write; 0 = no mirror) and the
    // shoot-count leak check (always, read-only).
    bool mag_mirror;
    std::uintptr_t gun_prim, gun_rec;
    // Native placement (partner_recon neutral_place; not compared by SameArm):
    // every native window of this Arm injects Neutral() (no held bit, no
    // stick, the HOLD latch bit 0 cleared), so block 1's deferred job never
    // runs without a window while the body is placed and verified.
    bool neutral;
    // Native weapons (not compared by SameArm): bits every native window
    // strips from its injection (FIRE | RELOAD while the remote's gun is not
    // the one body 1 carries or body 1 is mid-switch: the weapon gate, the
    // magazine mirror freezes; FIRE alone for an unqualified weapon type).
    std::uint32_t strip;
    // The partner's own reload (docs/native-puppet.md "Weapons" R4; not
    // compared by SameArm): the setting (mirror never freezes the magazine
    // mirror), the driver's event machine holding it (pending/entered; a stuck
    // body reload stays held until its native exit), the queued tap's
    // reconcile (the mirror writes the remote's magazine although the remote
    // reloads: body 1's next reload needs its magazine below capacity) and the
    // release (body 1's reload mode passed partner_reload release_ms: a lost
    // exit no longer freezes the mirror).
    PartnerNativeReload reload;
    bool reload_hold, reload_reconcile, reload_release;
    // The weapon switch (W4; not compared by SameArm): the slot follower
    // (off|write|call), the slots the driver's plan admits (bit s) and the
    // body's equip in each (the follower re-checks the handle against it).
    PartnerNativeSlot slot_follow;
    std::uint8_t slot_ok;
    std::uint16_t slot_equip[3];
    // Weapons4: bit r set: the remote's slot r is followed with the plan's
    // stand-in standin_slot[r] (partner_slots Plan::map); clear: with slot r
    // itself (the follower's own empty/plan/mismatch checks decide).
    std::uint8_t standin_mask;
    std::uint8_t standin_slot[3];
    // Anim round 1 (docs/anim-round1-weapons.md; not compared by SameArm): the
    // state-driven aim intent (aim_intent: the driver's partner_slots AimGate
    // passed with partner_native_aim_intent=state; aim_act_until_ms: the
    // remote's act is an aim act, honoured until then, so a stale Arm drops it)
    // adds HOLD to a native window's live sample (partner_slots AimFrame), and
    // the reload rate factor (partner_reload RateTarget; 0: no rate write).
    bool aim_intent;
    std::uint64_t aim_act_until_ms;
    float reload_rate_k;
    bool native_support; // key selected; every consumer rechecks the fresh slot/equip and safety owners
    // WS4 v1 (not compared by SameArm; both need native_support): the placed C4
    // lane (type 13, 391..395, slot 3) and the arm lane (type 14, only 516,
    // slot 6, HOLD only), lab keys partner_native_placed / partner_native_arm.
    bool native_placed, native_arm;
    // WS4 v2 (not compared by SameArm; needs native_placed): the remote's C4
    // place (46) / detonate (47) mirrored into body 1's rec+0x3CC before its
    // job (partner_place_mirror), lab key partner_native_place_mirror.
    bool native_place_mirror;
    // Native support C(b) (not compared by SameArm): instance 0's gun prim and
    // record from the driver tick's ReadGun(k=0). The hook re-validates the
    // prim (vtable, prim+0x58 == record, not body 1's) before it reads the
    // viewer's gun mode, read-only, to attribute a slot-3 stock change between
    // two body-1 jobs to the viewer's own throw (0 = none).
    std::uintptr_t viewer_gun_prim, viewer_gun_rec;
    // F3 traversal icon (docs/native-puppet.md "Traversal icon"; not compared
    // by SameArm, so a request never bumps gen): body 1's pending icon request
    // from partner_traverse (partner_icon::Kind, 0 = none), its process-unique
    // id and its expiry (a stale Arm drops it). A drain window swaps the padop
    // vtable copy in only while all three hold (partner_icon::SwapWanted).
    std::uint8_t icon_kind;
    std::uint32_t icon_seq;
    std::uint64_t icon_until_ms;
};

// Every field except gen, host_cam_cdeg, steer/mag, the rates and gun fields, neutral, strip, the icon request and the one-shots.
bool SameArm(const Arm& a, const Arm& b) noexcept;

// Hook statistics drained by the driver (the hook keeps atomics).
// state: P+0x208 count != 2, P+0x214 local index != 0, or the Q vtable changed.
enum class Skip : std::uint8_t { none, player, p_vtable, flags, sync, state, pp, src, cam, ids, root, plugin, rec, count };
const char* SkipName(Skip skip) noexcept;
// Counters are process-lifetime totals (the driver logs deltas); us_max is
// cleared by each drain. last_* describe the most recent composed window:
// last_age_ms is kNoSample when that window had no valid sample, and
// last_stale is 1 when it injected a neutral pad because the net input was
// missing or older than stale_ms (net mode only).
inline constexpr std::uint32_t kNoSample = 0xFFFFFFFFu;
struct WindowStats {
    std::uint32_t windows, root1_calls, skips, stale, faults, hold_calls, cam_restored;
    // Round 4: hold=own/net classifier answers for btn 1 by code 0..3, the
    // key-manager stub calls, sub-sample taps synthesized, edges injected, and
    // the PP+0x278 / PP+0x4B0 values instance 1 changed (shadowed).
    std::uint32_t hold_own[4];
    std::uint32_t km_calls;
    std::uint32_t synth_taps;
    std::uint32_t press_n, release_n;
    std::uint32_t shadow_changes;
    std::uint32_t cam_dirty_or;
    std::uint32_t us_max;
    std::uint32_t hook_thread;
    std::uint32_t gen_seen;
    Skip last_skip;
    std::uint32_t last_held;
    std::int32_t last_lx, last_ly, last_dir, last_mag;  // x1000
    std::uint32_t last_age_ms;
    std::uint8_t last_stale;
    std::uint8_t fault_phase;   // 0 none, 1 pre, 2 call, 3 post
    std::uint32_t fault_code;
};
const char* FaultPhaseName(std::uint8_t phase) noexcept;
// One hold classifier observation (holdlog, block 0 only).
struct HoldRecord {
    std::uint32_t btn, frames, result;
};

// Camera POD ranges compared/restored inside the window (CAM-relative,
// [begin, end)). Bit i of the dirty mask is range i. CAM+0xB0 and
// 0x13A..0x150 (pointer-like) are never touched. Round 4 adds the vector
// CAM+0x150 (movaps at 0x10CDFCC) and the request block the camera setters
// write for any id: CAM+0xD38 flags with +0xD80..0xD97 (vt+0xE0 0x111A920),
// +0xDB0..0xDBF (vt+0xF8 0x111A710), +0xDD0..0xDDF (vt+0x100 0x111A760) and
// +0xE9C + 16k, k < 5 (vt+0x128 0x111A8A0). CAM = object+0x20 of a 0x11A0-byte
// object (factory 0x1114024), so every range is inside it.
struct CamRange {
    std::uint16_t begin, end;
};
inline constexpr CamRange kCamRanges[] = {
    {0x10, 0x20}, {0x120, 0x130}, {0x170, 0x180}, {0x1F0, 0x208}, {0x208, 0x210}, {0x230, 0x238},
    {0x270, 0x290}, {0x2D8, 0x2E0}, {0x310, 0x328}, {0x328, 0x338}, {0x408, 0x410}, {0x4A0, 0x4A8},
    {0x150, 0x160}, {0xD38, 0xD3C}, {0xD80, 0xD98}, {0xDB0, 0xDC0}, {0xDD0, 0xDE0}, {0xE9C, 0xEEC}};
inline constexpr std::size_t kCamRangeCount = sizeof(kCamRanges) / sizeof(kCamRanges[0]);
inline constexpr std::size_t kCamSaveBytes = 0x154;  // sum of the ranges

} // namespace mgscoop::partner_input
