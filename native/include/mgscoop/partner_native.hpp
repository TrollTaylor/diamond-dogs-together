#pragma once
// Native puppet (docs/native-puppet.md), pure part: the arbiter that switches
// the partner body (player instance 1) between the native drive (the remote's
// pad through the game's own locomotion/action/gun systems, block 1's
// per-block job deferred onto the main thread inside the input window) and
// the proven dm=map + xform fallback; the effective settings while native;
// steering; the HOLD latch rule; the effective mask; health; the telemetry
// ring. No Win32 calls and no game memory (mgs_coop_core).
//
// States: off (drive=legacy), fallback, entering, active, leaving, latched.
//   fallback -> entering: every precondition held kEnterHoldMs continuously and
//     at least kCooldownMs after the last leaving.
//   entering: the native settings apply (dm off, apply=input, input=net), the
//     driver's DM machinery stops the clip and reverts the patch, the
//     reconciliation places and verifies the body and arms the window; then
//     active (the driver arms the deferral last). Timeouts and a failed DM
//     revert leave.
//   active -> leaving: hard reasons immediately, soft reasons after a hold.
//   leaving (one step): defer disarmed -> window disarmed -> dm=map rearmed,
//     body epoch bumped; then fallback, or latched after kLatchFallbacks
//     fallbacks in one mission (until MissionStart or a drive=native trigger).
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::partner_native {

enum class Mode : std::uint8_t { off, fallback, entering, active, leaving, latched };
const char* ModeName(Mode mode) noexcept;

// Thresholds (unit-tested; the driver uses the defaults).
struct Params {
    std::uint32_t enter_hold_ms = 2000;     // fallback: preconditions held this long
    std::uint32_t cooldown_ms = 5000;       // ... and this long after the last leaving
    std::uint32_t input_age_ms = 250;       // live input: age <= this
    std::uint32_t dm_timeout_ms = 2000;     // entering: DM stop / revert
    std::uint32_t place_timeout_ms = 3000;  // entering: placement + arm
    std::uint32_t stale_hold_ms = 1000;     // active: input older than input_age_ms
    std::uint32_t link_hold_ms = 500;       // active: link down
    std::uint32_t health_window_ms = 1000;  // health accumulates over >= this
    std::uint32_t health_pct = 90;          // deferred >= 90% of w50_b1_s0, windowed >= 90% of drains
    std::uint32_t health_windows = 2;       // consecutive bad windows (after the first, warm-up)
    std::uint32_t diverging_snaps = 3;      // snaps in 10 s
    std::uint32_t runaway_ms = 2000;
    std::uint32_t cost_us = 4000;           // drain_us_max per window
    std::uint32_t cost_windows = 3;         // consecutive windows over cost_us
    std::uint32_t latch_fallbacks = 3;      // per mission
    std::uint32_t min_batches = 120;        // thread_ok needs this many observed batches
};

// One driver tick. Counters are deltas since the previous Step.
struct In {
    std::uint64_t now_ms;
    bool configured;        // partner_drive=native
    bool defer_on;          // partner_input_defer=on (census never activates)
    bool terminating;
    bool installed_h0, installed_h2, installed_h1, installed_wrap;
    bool live_h0, live_h1, live_h2;
    const char* install_refusal;  // nullptr, or the static reason the lazy install gave
    bool thread_ok;         // batch_threads_seen == 1 && batch_thread == hook_thread && batches >= min_batches
    bool faulted;           // window, defer (lost/torn/scope/gate) or look fault
    bool realized, parts_loaded, range_hidden;
    std::uint32_t stage;
    bool link_live, input_valid;
    std::uint32_t input_age_ms;
    bool dm_idle, dm_patched, dm_revert_failed;
    bool window_armed, recon_placed;
    // Health deltas (block-1 sub-0 W50 calls, deferred, drains, windowed and
    // unwindowed drains, lost records, windows refused inside a batch, frames).
    std::uint32_t w50_b1_s0, deferred, drains, windowed, unwindowed, lost, wrap_in_batch, frames;
    std::uint32_t drain_us_max;   // max since the previous Step
    std::uint32_t snaps_10s;      // reconciliation snaps in the last 10 s
    std::uint32_t runaway_ms;     // how long the runaway condition has held (0 = not)
};

struct Out {
    Mode mode;
    bool active;          // the native effective settings apply (entering or active)
    const char* reason;   // why the current mode (static)
    bool transition;      // the mode changed in this Step
    // Driver requests, in the publication order of docs/native-puppet.md 4.5.
    bool req_defer_arm;      // active: identity and window are published; arm the deferral last
    bool req_defer_disarm;   // leaving: first
    bool req_disarm;         // leaving: then the input window
    bool req_dmmap_rearm;    // leaving: then dm=map
    bool req_epoch;          // leaving: body epoch ++
    const char* enter_step;  // entering: "dm_stop" | "dm_revert" | "place" | "armed" when it changes, else nullptr
};

struct State {
    Mode mode;
    const char* reason;
    std::uint64_t since_ms;
    bool ready;                     // fallback: every precondition holds since ready_since_ms
    std::uint64_t ready_since_ms;
    bool left;                      // a leaving happened this mission (at left_ms)
    std::uint64_t left_ms;
    std::uint32_t fallbacks;        // this mission
    std::uint8_t enter_step;        // 0 none, 1 dm_stop, 2 dm_revert, 3 place, 4 armed
    std::uint64_t step_since_ms;
    bool stale, link_down;
    std::uint64_t stale_since_ms, link_since_ms;
    // Health and cost windows (active only).
    std::uint64_t window_start_ms;
    std::uint32_t win_w50, win_deferred, win_drains, win_windowed, win_us_max;
    std::uint32_t windows_done, health_bad, cost_bad;
    bool dm_latch;                  // a failed DM revert latched native off for the mission
};

// MissionStart: fallback (configured) or off; per-mission counters cleared.
void Reset(State& state, bool configured) noexcept;
// A drive=native trigger: a latched mission returns to fallback.
void Unlatch(State& state, std::uint64_t now_ms) noexcept;
Out Step(State& state, const In& in, const Params& params = Params{}) noexcept;

// --- Effective settings while native is active (never written to lab.ini) ---
struct DriveSettings {
    PartnerApply apply;
    PartnerInput input;
    PartnerDm dm;
    PartnerAnim anim;
    PartnerWrap wrap;
    PartnerInputHold hold;
    std::uint8_t words[3];
    PartnerInputCam cam;
    PartnerRecon recon;
    PartnerSlot slot;
    bool p218;
    std::uint32_t mask;
};
struct NativeConfig {
    bool gun, fire;
    PartnerNativeAim aim;
};
// active: apply=input, input=net, dm=off, anim=off, wrap=window, hold=own,
// words=hpr, cam=sandbox, recon=track, slot=body, p218 off, mask =
// NativeMask(base.mask); otherwise base unchanged.
DriveSettings Effective(const DriveSettings& base, bool active, const NativeConfig& config) noexcept;
// configured & PartnerNativeAllowed(gun, fire): FIRE never without gun.
std::uint32_t NativeMask(std::uint32_t configured, bool gun, bool fire) noexcept;

// --- HOLD latch PP+0x27C (gun eligibility 0x1054210 reads bit 0) -------------
// (old & ~1) | (((w4c & ~mask28) >> 3) & 1): the latch writer 0x12072A6 over
// the injected "now" word with instance 1's private PP+0x28.
std::uint32_t Latch27c(std::uint32_t old, std::uint32_t w4c, std::uint32_t mask28) noexcept;
// POST: bit 0 back to the saved value, every other bit as the window left it.
inline std::uint32_t LatchRestore(std::uint32_t now, std::uint32_t saved) noexcept { return (now & ~1u) | (saved & 1u); }

// --- Steering (docs/native-puppet.md 8.2) ------------------------------------
struct SteerParams {
    double max_deg = 15.0;          // partner_native_steer_deg (0 disables)
    double rate_dps = 30.0;         // toward the target bias
    double return_dps = 60.0;       // back to 0 while inactive
    double min_speed_mmps = 500.0;
    double min_in_r = 0.4;
    double cross_dead_mm = 300.0;
    double lookahead_min_mm = 1500.0;
    double trim_pct = 0.0;          // partner_native_trim_pct
};
struct SteerIn {
    std::uint64_t now_ms;
    double e_x, e_z;     // target - body (horizontal, mm)
    double v_x, v_z;     // remote velocity (mm/s)
    double in_r;         // injected stick magnitude 0..1
    bool frozen, prone_transition, walk_key;
};
struct SteerState {
    double bias_rad;
    std::uint64_t last_ms;
    bool valid;
};
struct SteerOut {
    double bias_rad;     // steer_rad for Compose (positive = left = heading up)
    double mag_scale;    // 1 unless trimming
    double along_mm, cross_mm;
    bool active;
};
SteerOut Steer(SteerState& state, const SteerIn& in, const SteerParams& params) noexcept;
inline constexpr double kTrimFloor = 0.42;  // start threshold 0.4 (0x2104860) plus margin

// --- Telemetry ring (one entry per owner tick, oldest first) -------------------
struct RingEntry {
    std::uint32_t ms;
    std::uint8_t mode, remote_stance, body_stance;
    std::int32_t remote_mmps, body_mmps, err_mm, steer_mdeg;
    std::uint32_t held, gun3c0;
};
inline constexpr std::size_t kRing = 48;
struct Ring {
    RingEntry entries[kRing]{};
    std::uint8_t head{}, count{};
};
inline void Push(Ring& ring, const RingEntry& entry) noexcept {
    ring.entries[ring.head] = entry;
    ring.head = static_cast<std::uint8_t>((ring.head + 1) % kRing);
    if (ring.count < kRing) ++ring.count;
}
inline const RingEntry& At(const Ring& ring, std::size_t i) noexcept {
    return ring.entries[(ring.head + kRing - ring.count + i) % kRing];
}

} // namespace mgscoop::partner_native
