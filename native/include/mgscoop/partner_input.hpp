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
    std::uint8_t edge_rep;     // 0 first send of edge_seq, 1..2 repeats (loss tolerance)
    std::uint32_t long12;      // level: bits the sender has held for >= 12 frames
    std::uint32_t tap12;       // edge set: bits released after a run of 1..11 frames
};
inline constexpr std::uint8_t kEdgeRepeats = 2;
inline constexpr std::uint32_t kHoldFrames = 12;  // classify(btn, 12) at every stance/subject/action site
inline constexpr std::int16_t kMaxDir = 31416;
inline constexpr std::int32_t kCamMin = -18000, kCamMax = 17999;
// Normalizes centidegrees to [-18000, 17999].
std::int32_t NormalizeCdeg(std::int64_t cdeg) noexcept;
// The clamps above; non-finite values become 0.
InputPacket Quantize(const PadSnapshot& snapshot, std::int32_t cam_cdeg) noexcept;
// Whether every value is inside the wire ranges (player_sync checks the same).
bool ValidPacket(const InputPacket& packet) noexcept;

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
// Hook-thread only; survives Arm gen bumps (see Arm).
struct EdgeState {
    std::uint32_t prev_held;
    std::uint32_t last_seq;
};
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
};
// One window's injected pad (rules in partner_input.cpp). Updates `edge`.
// Held bits: overlay | (live net held); a bit that was pressed and released
// between two samples (sample edge set, or tap12) and is in neither this nor
// the previous window's held is injected for exactly this window (the next
// window then carries its release edge).
Injection Compose(const Config& config, const Sample& sample, std::int32_t host_cam_cdeg, Overlay overlay,
                  EdgeState& edge) noexcept;
// wrap(cam + atan2(lx, ly) - pi) in radians, atan2(0, 0) = 0 (PP+0x14).
double Heading(double cam_rad, float lx, float ly) noexcept;

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
};
void SenderObserve(SenderEdges& state, const PadSnapshot& snapshot) noexcept;
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
};
bool SameArm(const Arm& a, const Arm& b) noexcept;  // every field except gen, host_cam_cdeg and the one-shots

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
