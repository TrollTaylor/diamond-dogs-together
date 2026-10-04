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
// PP+0x10/+0x14/+0x18 are the left stick magnitude, force and direction the
// PP update derives each frame (0x12070F3); the direction is
// atan2f(LX, LY) (0x12070CC, xmm0 = LX, xmm1 = LY).
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
    float mag, force, dir;
    std::uint8_t flags;  // Flag bits (Valid always set)
};
// Replicates the PP getters above exactly (masks included).
PadSnapshot Effective(const PadRaw& raw) noexcept;
// Whether PP+0x18 equals atan2f(lx, ly) (mod 2 pi, within 1e-3) for this
// snapshot: na while mag <= 0.01 or the left stick is masked (PP+0x2C bit 0).
enum class DirState : std::uint8_t { na, ok, bad };
DirState CheckDir(const PadSnapshot& snapshot, std::uint32_t stick_mask) noexcept;
const char* DirStateName(DirState state) noexcept;

// The 40-byte input frame payload values (player_sync encodes them).
struct InputPacket {
    std::uint8_t flags;        // Flag bits, subset of Flag::Known
    std::uint32_t held, w4c, w50, w54;
    std::int16_t lx, ly, rx, ry;  // clamp(v, -1, 1) * 32767; -32768 never used
    std::uint8_t lt, rt;          // clamp(v, 0, 1) * 255
    std::uint16_t lmag, lforce;   // clamp(PP+0x10 / +0x14, 0, 1) * 65535
    std::int16_t ldir;            // PP+0x18 * 10000, |x| <= 31416
    std::int16_t cam_cdeg;        // sender camera yaw in centidegrees, [-18000, 17999]
};
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
// The newest received input (partner_net::LatestInput).
struct Sample {
    bool valid;
    std::uint32_t seq;
    std::uint32_t age_ms;
    InputPacket packet;
};
// Per arm generation, hook-thread only.
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
    float mag, force, dir;     // PP+0x10/+0x14/+0x18
    float rmag, rforce, rdir;  // PP+0x1C/+0x20/+0x24
    bool stale;
};
// One window's injected pad (rules in partner_input.cpp). Updates `edge`.
Injection Compose(const Config& config, const Sample& sample, std::int32_t host_cam_cdeg, Overlay overlay,
                  EdgeState& edge) noexcept;

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
// POD only. A change of any field bumps `gen`, which makes the hook reset the
// edge state, the tap state and instance 1's private pad masks.
struct Arm {
    std::uint32_t gen;
    bool enabled;   // the block-1 window runs
    bool holdlog;   // block 0's hold classifier is logged (I5)
    bool p218;      // diagnostic P+0x218 = 1 inside the window (I3x)
    PartnerInputCam cam;
    PartnerInputHold hold;
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
enum class Skip : std::uint8_t { none, player, p_vtable, flags, sync, pp, src, cam, ids, root, plugin, rec, count };
const char* SkipName(Skip skip) noexcept;
struct WindowStats {
    std::uint32_t windows, root1_calls, skips, stale, faults, hold_calls, cam_restored;
    std::uint32_t cam_dirty_or;
    std::uint32_t us_max;
    std::uint32_t hook_thread;
    std::uint32_t gen_seen;
    Skip last_skip;
    std::uint32_t last_held;
    std::int32_t last_lx, last_ly, last_dir, last_mag;  // x1000
    std::uint32_t last_age_ms;
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
// 0x13A..0x150 (pointer-like) are never touched.
struct CamRange {
    std::uint16_t begin, end;
};
inline constexpr CamRange kCamRanges[] = {
    {0x10, 0x20}, {0x120, 0x130}, {0x170, 0x180}, {0x1F0, 0x208}, {0x208, 0x210}, {0x230, 0x238},
    {0x270, 0x290}, {0x2D8, 0x2E0}, {0x310, 0x328}, {0x328, 0x338}, {0x408, 0x410}, {0x4A0, 0x4A8}};
inline constexpr std::size_t kCamRangeCount = sizeof(kCamRanges) / sizeof(kCamRanges[0]);
inline constexpr std::size_t kCamSaveBytes = 0xB8;  // sum of the ranges

} // namespace mgscoop::partner_input
