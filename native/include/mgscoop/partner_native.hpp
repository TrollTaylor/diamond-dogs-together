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
//     driver's DM machinery stops the clip and reverts the patch; from the
//     place step on the deferral is armed (req_defer_arm), and only then does
//     the driver publish the native Arm (a frame between a native Arm and the
//     deferral would run block 1's job inline in a batch); the
//     reconciliation places and verifies the body and arms the window; then
//     active. Timeouts and a failed DM revert leave.
//   active -> leaving: hard reasons immediately, soft reasons after a hold.
//   leaving (one step): defer disarmed -> window disarmed -> dm=map rearmed,
//     body epoch bumped; then fallback, or latched after kLatchFallbacks
//     fallbacks in one mission (until MissionStart or a drive=native trigger).
#include <cstddef>
#include <climits>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_native_net.hpp"

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
    // Online policy (partner_native_net=online; docs/native-puppet.md
    // "Online"). Network reasons (net_input, net_link, net_quiet, and
    // range_hidden / unrealized: the partner streamed out) leave without
    // counting toward the latch; re-entry after one waits net_cooldown_ms and
    // then for the link quality to hold quality_ms. A quiet remote (link up,
    // its input and poses stale: paused or loading) keeps the body neutral in
    // place for up to quiet_hold_ms. More than flap_leaves network leaves in
    // flap_window_ms keep the clips until the quality held flap_good_ms.
    bool online = false;
    std::uint32_t net_cooldown_ms = 2000;
    std::uint32_t quality_ms = 2000;
    std::uint32_t quiet_hold_ms = 10000;
    std::uint32_t flap_leaves = 6;
    std::uint32_t flap_window_ms = 300000;
    std::uint32_t flap_good_ms = 30000;
};
// The online policy's thresholds: Params{} plus online, and the input-stale
// and link-down holds at 1500 ms.
Params OnlineParams() noexcept;
// Whether `reason` is a network reason under the online policy (no latch).
bool NetReason(const char* reason) noexcept;

// One driver tick. Counters are deltas since the previous Step.
struct In {
    std::uint64_t now_ms;
    bool configured;        // partner_drive=native
    bool defer_on;          // partner_input_defer=on (census never activates)
    bool terminating;
    bool installed_h0, installed_h2, installed_h1, installed_wrap;
    bool live_h0, live_h1, live_h2;
    // The phase-1 local detour (partner_local_detour; installed with defer=on).
    bool installed_p1, live_p1;
    const char* install_refusal;  // nullptr, or the static reason the lazy install gave
    bool thread_warmup;    // the expected game thread is still gathering min_batches
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
    // Drains refused because the deferred job's block was not instance 1's.
    std::uint32_t drain_block;
    // Drains whose job ran without a window because no enabled native Arm was
    // published (a disarm under the native drive: the job read the viewer's
    // pad). A hard reason in active (placements keep a neutral window).
    std::uint32_t drain_disarmed;
    std::uint32_t drain_us_max;   // max since the previous Step
    std::uint32_t snaps_10s;      // reconciliation snaps in the last 10 s
    std::uint32_t runaway_ms;     // how long the runaway condition has held (0 = not)
    // Online policy: the link is up but the remote's input and poses are
    // stale; how long the link quality has been good
    // (partner_native_net::GoodMs); the remote's capabilities say it sends
    // no input frames (never entering: remote_caps).
    bool remote_quiet;
    std::uint32_t quality_good_ms;
    bool remote_caps_refused;
    const char* visibility_reason;  // qualified ride/air/dead guard; null normally
    // A suspend sub-mode (docs/anim-round1-actions.md "DM overlay"): while set,
    // entering/active leave and fallback stays blocked with this reason, but
    // the leave counts no fallback (no latch) and, once it clears, fallback
    // re-enters at once (no cooldown, no settling hold) unless another blocker
    // showed up meanwhile. kSuspendOverlay is the DM overlay lever's reason.
    const char* suspend_reason;
};
inline constexpr char kSuspendOverlay[] = "overlay";

struct Out {
    Mode mode;
    bool active;          // the native effective settings apply (entering or active)
    const char* reason;   // why the current mode (static)
    bool transition;      // the mode changed in this Step
    // Driver requests, in the publication order of docs/native-puppet.md 4.5.
    bool req_defer_arm;      // entering from the place step on, and active: identity published; arm the deferral before the native Arm
    bool req_defer_disarm;   // leaving: first
    bool req_disarm;         // leaving: then the input window
    bool req_dmmap_rearm;    // leaving: then dm=map
    bool req_epoch;          // leaving: body epoch ++
    const char* enter_step;  // entering: "dm_stop" | "dm_revert" | "place" | "armed" when it changes, else nullptr
    // This step belongs to a suspend cycle (the suspend leave, its fallback, or
    // the immediate re-entry through active): the driver logs it on the
    // suspending lever's own cap instead of the transition cap.
    bool suspend_cycle;
};

struct State {
    Mode mode;
    const char* reason;
    std::uint64_t since_ms;
    bool ready;                     // fallback: every precondition holds since ready_since_ms
    std::uint64_t ready_since_ms;
    bool left;                      // a leaving happened this mission (at left_ms)
    std::uint64_t left_ms;
    const char* left_reason;        // why the last leaving happened (static; kept through cooldown/settling)
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
    // Online policy (Params::online).
    bool left_net;                  // the last leaving was a network reason
    bool quiet;                     // active: the remote is quiet since quiet_since_ms
    std::uint64_t quiet_since_ms;
    bool flap;                      // too many network leaves: clips until the quality holds
    std::uint64_t net_times[8];     // the most recent network leaves (flap guard)
    std::uint8_t net_count;
    std::uint32_t net_leaves, hard_leaves, net_reentries, quiet_episodes;
    const char* last_net_reason;    // static; nullptr before any
    // Suspend sub-mode: left for In::suspend_reason (re-enter at once when it
    // clears), resuming (entering after a suspend, until active), suspends.
    bool suspended, resumed;
    std::uint32_t suspends;
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
    // partner_native_hold=net: the hold classifier answers from the sender's
    // 12-frame words (frame-rate free) instead of the receiver's own history.
    bool hold_net = false;
};
// active: apply=input, input=net, dm=off, anim=off, wrap=window, hold=own
// (net with config.hold_net), words=hpr, cam=sandbox, recon=track,
// slot=body, p218 off, mask = NativeMask(base.mask); otherwise base unchanged.
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
    bool hold_stripped{}; // live raw HOLD was actually refused by the pad gate
    double body_speed_mmps{};
    bool hold_parity_ready{}; // caller qualified remote aim-only frozen state
};
struct SteerState {
    double bias_rad;
    std::uint64_t last_ms;
    bool valid;
    double hold_scale; // bounded HOLD-strip parity scale, initialized to 1
};
struct SteerOut {
    double bias_rad;     // steer_rad for Compose (positive = left = heading up)
    double mag_scale;    // 1 unless trimming
    double along_mm, cross_mm;
    bool active;
};
SteerOut Steer(SteerState& state, const SteerIn& in, const SteerParams& params) noexcept;
inline constexpr double kTrimFloor = 0.42;  // start threshold 0.4 (0x2104860) plus margin

// A bounded repeat of the already admitted DASH tap repairs a press consumed
// during a turn/start transition. No ACTION or native call is introduced.
struct DashIn {
    std::uint64_t now_ms;
    bool active, input_live, mask_dash, hold_stripped;
    bool body_ok, remote_ok, remote_sprinting;
    std::uint16_t body_act, remote_act;
    std::uint8_t body_stance, remote_stance, body_speed, remote_speed;
    double in_r;
};
struct DashState {
    std::uint64_t since_ms, last_ms, window_ms;
    std::uint32_t attempts;
};
bool DashRelatch(DashState& state, const DashIn& in) noexcept;

// --- Weapons (docs/native-puppet.md "Weapons"), pure --------------------------
// The gun record fields (rec = *(prim+0x58) + (id - node base)*0x610) the
// magazine getter 0x10435D0, CanReload 0x1051F30 and the reload gate in
// 0x103E2D0 read (offsets in partner_player_contract.hpp; the hook
// static_asserts they match these rules).
struct GunFields {
    std::uint32_t flags27c;   // rec+0x27C: bit 9 sub magazine, bit 19 half, bit 20 double, class (>>6)&7
    std::uint32_t flags280;   // rec+0x280: bit 19 no reload
    std::uint32_t gun3c0;     // rec+0x3C0: bit 28 full magazine, bit 29 capacity-based stock check, bit 20 reload request
    std::uint32_t equip;      // rec+0x24C
    std::uint16_t cap, cap_sub, mag, mag_sub, stock, stock_sub;  // rec+0x210/0x212, 0x25C/0x25E, 0x260/0x262
    std::uint16_t w23a;       // rec+0x23A (bits 4/5 lower the capacity-based stock check, 0x104F1C0)
};
inline constexpr std::uint32_t kGunClassGun = 1;
// PlayerPad bits (lab_config kPartnerNativeFireBit / kPartnerNativeGunBits): FIRE and RELOAD.
inline constexpr std::uint32_t kPadFire = 0x10, kPadReload = 0x40;
inline std::uint32_t GunClass(std::uint32_t flags27c) noexcept { return (flags27c >> 6) & 7u; }
// (flags27c bit 9 ? cap_sub : cap), doubled for equip 0x42B..0x42E with bit
// 20, halved with bit 19 (0x10435FD, 0x1051FA0).
std::uint32_t GunCapacity(const GunFields& gun) noexcept;
// The getter: capacity with gun3c0 bit 28, else mag_sub (bit 9) or mag.
std::uint32_t GunMagazine(const GunFields& gun) noexcept;
// The live magazine field: 0x25E with flags27c bit 9, else 0x25C (0x1058ABF).
inline std::size_t GunMagazineOffset(std::uint32_t flags27c) noexcept { return (flags27c & 0x200u) ? 0x25Eu : 0x25Cu; }
// CanReload replica (telemetry): no-reload bit clear, stock check (capacity
// based with gun3c0 bit 29, else stock - magazine != 0) and magazine < capacity.
bool GunCanReload(const GunFields& gun) noexcept;
// The reload gate before 0x1094770 (0x103E3A4): class 1, (flags27c & 0x1800)
// != 0x800, equip != 0 and rec+0x3D5 != 4.
bool GunReloadGate(const GunFields& gun, std::uint8_t b3d5) noexcept;

// Magazine mirror. Instance 1's non-local shots never spend its magazine
// (the ammo call 0x1041650 is local-only), so the remote's count decides
// whether the partner can fire (magazine > 0) and reload (magazine <
// capacity): the remote's (mag, cap) mapped onto the partner's capacity.
// Full stays full, empty stays empty, anything between maps into
// 1..cap-1 (rounded up). A FIRE press edge leaves 1 instead of 0 only when
// the remote fired a round: it still has one (a partner capacity of 1) or
// its previous magazine was loaded (the press spent its last round in the
// same sample). A press on an already empty remote is a dry fire and stays 0.
struct MagIn {
    std::uint16_t remote_mag, remote_cap;  // the sender's v4 fields (remote_cap 1..kMaxMagCap, mag <= cap)
    std::uint32_t cap;                     // the partner's GunCapacity (1..kMaxMagCap)
    bool fire_edge;
    bool remote_was_loaded;                // the previous remote magazine the mirror acted on was > 0
};
inline constexpr std::uint16_t kMaxMagCap = 999;
std::uint32_t MirrorMagazine(const MagIn& in) noexcept;

// Hook-thread history of the last magazine sample accepted by every checked
// store/record gate. Preparing an attempt never consumes a changed sample,
// slot change or reload reconcile; a transient refusal can retry unchanged
// input. Commit only after successful writes or a validated no-write result.
struct MagazineHistory {
    bool seen=false, freeze_pending=false, slot_seen=false;
    std::uint16_t remote_mag=0, remote_cap=0;
    std::uint8_t body_slot=0;
};
// slot_changed (anim round 1): body 1's slot rec+0x3DA differs from the last
// committed one; the write then also rewrites the completion base rec+0x3E8
// (the shadow kept the previous weapon's count: 41 of 11 after a rifle ->
// handgun switch, 0x1054A3C..0x1054A4E adds to it without the capacity bound).
struct MagazineAttempt {
    bool needed, reconcile, remote_was_loaded, slot_changed;
};
constexpr MagazineAttempt PrepareMagazine(const MagazineHistory& h, std::uint16_t remote_mag,
                                         std::uint16_t remote_cap, std::uint8_t body_slot,
                                         bool fire_edge, bool reload_edge) noexcept {
    const bool slot_changed=h.slot_seen && body_slot!=h.body_slot;
    const bool changed=!h.seen || remote_mag!=h.remote_mag || remote_cap!=h.remote_cap ||
                       h.freeze_pending || slot_changed;
    return {fire_edge || reload_edge || changed, h.freeze_pending, h.seen && h.remote_mag>0, slot_changed};
}
// The completion base rule (anim round 1): the mirror's write sets rec+0x3E8
// to the mirrored magazine on a reconcile, on a slot change, and whenever the
// base exceeds the capacity of the weapon in hand (never while body 1 reloads:
// the mirror is frozen then). The non-local completion builds the new magazine
// as base + min(rounds, capacity - base) with unsigned 16-bit arithmetic, so a
// base above the capacity overflows the magazine.
constexpr bool ShadowRewrite(bool reconcile, bool slot_changed, std::uint32_t shadow, std::uint32_t cap) noexcept {
    return reconcile || slot_changed || shadow > cap;
}
inline void CommitMagazine(MagazineHistory& h, std::uint16_t remote_mag,
                           std::uint16_t remote_cap, std::uint8_t body_slot) noexcept {
    h.seen=true;h.remote_mag=remote_mag;h.remote_cap=remote_cap;
    h.slot_seen=true;h.body_slot=body_slot;h.freeze_pending=false;
}

// --- Telemetry ring (one entry per owner tick, oldest first) -------------------
struct RingEntry {
    std::uint32_t ms;
    std::uint8_t mode, remote_stance, body_stance;
    std::int32_t remote_mmps, body_mmps, err_mm, steer_mdeg;
    std::uint32_t held, gun3c0;
    // Locomotion evidence: the body's act (anim array), instance 1's base
    // action record after the last windowed drain (act, requested next act,
    // sampled move magnitude x1000; kNull when unread), phase-1 calls forced
    // local since the previous entry, and the reconciliation step of the
    // previous driver tick (the one err_mm/body_mmps come from): its mode and
    // action (kReconOff when the reconciliation was off) and whether that
    // tick's reply placed the body by Warp.
    std::uint16_t act;
    std::uint8_t rec_act, rec_next;
    std::int32_t rec_mag;
    std::uint16_t p1_forced;
    std::uint8_t recon_mode, recon_action;
    bool warp;
    // Weapons: instance 1's magazine (getter rule; 0xFFFF unread) and its
    // reload request (rec+0x3C0 bit 20, also in gun3c0).
    std::uint16_t mag;
    // Facing: the body's yaw and the facing error target - body (centidegrees,
    // kNull unread).
    std::int32_t body_yaw_cdeg, face_err_cdeg;
    // Aim parity: instance 1's aim yaw parameter r60 x1000 (kNull unread) and
    // the aim-turn predicate on it (1/0, -1 unread).
    std::int32_t r60_1;
    std::int8_t pred1;
    // Reload and weapon switch: body 1's gun mode rec+0x2C0, the remote's v5
    // gun mode and body 1's slot in hand rec+0x3D9 (0xFF unread).
    std::uint8_t gmode, rmode, hslot;
};
inline constexpr std::uint8_t kReconOff = 0xFF;
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

// --- Telemetry (driver-owned POD, copied into the worker state.json "native") --
// Rates are per second over the driver's last ~1 s interval (per_s_valid
// false before the first interval). kNull marks an unread signed value.
inline constexpr std::int32_t kNull = INT32_MIN;
struct Telemetry {
    bool configured;
    Mode mode;
    char reason[32];
    // The reason of the last leaving ("" before any): "cooldown"/"settling"
    // replace `reason` in fallback, this keeps the cause.
    char left_reason[32];
    std::uint64_t since_ms;
    std::uint32_t fallbacks;
    bool latched;
    // Online play (docs/native-puppet.md "Online"): the network policy, link
    // quality, network/hard leaves and the capability exchange ("native.net").
    partner_native_net::NetTelemetry net;
    // Phase-1 detour (per second, per_s_valid) and the last windowed drain
    // (partner_input_hook::NativeStats).
    bool p1_installed, p1_live;
    std::uint32_t p1_forced, p1_refused, p1_windows;
    std::uint32_t drain_held, drain_view, drain_local_pre, drain_mask28, drain_mask2c, drain_mask30;
    std::int32_t drain_mag;
    bool rec_ok;
    std::uint32_t rec_act, rec_next;
    std::int32_t rec_mag, rec_heading_mrad;
    // Deferral.
    PartnerDefer setting;
    bool h0, h1, h2, wrap, thread_ok, armed;
    std::uint32_t batch_thread, hook_thread, lua_thread, batch_threads_seen;
    bool per_s_valid;
    std::uint32_t batches, w50_b0_s0, w50_b1_s0, w50_subs, w50_inline, ret_other, deferred, drains0, drains1, windowed, unwindowed;
    std::uint32_t lost, gate_closed, torn, entry_changed, dup, stale_frame, wrap_in_batch;
    // Nested hook-thread batches, batches on other threads, records dropped
    // by an unwound batch, drains refused for a block that is not instance 1's
    // and drains refused because a batch was in flight (per second).
    std::uint32_t nested, foreign, unwound, drain_block, drain_in_batch;
    // Drains without a window under a disabled Arm while the drive was active (per second).
    std::uint32_t unwindowed_native;
    std::uint32_t id1_pass, scope_unbalanced, drain_us_max, spin_max, faults, fault_code;
    char fault_phase[12];
    // Wrap census of block 1 (per second) and the look write.
    std::uint32_t svc28_b1, svc28_b1_same, svc28_b1_nested, walk7_b1, look_writes, look_skipped_rec;
    char look_refused[12];
    // Input.
    bool input_live;
    std::uint32_t input_age_ms, input_version, held, mask, latch27c;
    std::int32_t lx, ly, cam_cdeg_remote, cam_cdeg_host, heading_err_mrad, steer_mdeg, mag_scale_pct;
    // The camera-relative stick rotation check (kNull while |stick| < 0.1:
    // with a neutral stick heading_err_mrad was the two cameras' difference)
    // and that difference itself (remote - host camera yaw).
    std::int32_t steer_err_mrad, cam_diff_cdeg;
    // Body vs remote (animation arrays of instance 1 and the remote's anim state).
    bool body_valid;
    std::uint32_t act, stance, speed, speed_mmps, remote_act, remote_stance, remote_speed, remote_speed_mmps;
    std::uint32_t stance_match_pct, move_match_pct;
    // Reconciliation.
    char recon_mode[12];
    std::int32_t err_mm, along_mm, cross_mm, dy_mm;
    std::uint32_t nudges, snaps, places;
    // runaway: a runaway pin is active (moving overrun or idle); runaway_idle:
    // the idle-remote runaway the arbiter times; lag: the body is behind a
    // moving remote (no pin); stop_grace: the idle runaway is suppressed after
    // a stick release; dv along/across the remote's motion (kNull unread).
    bool runaway, runaway_idle, lag, stop_grace;
    std::int32_t dv_along_mmps, dv_cross_mmps;

    // Gun: timestamp of current fields copied by the natural owner, independent of F9.
    std::uint64_t gun_tick_ms;
    bool weapon_gate_current; // false when the gate retained its previous verdict
    bool gun_enabled, gun_read_ok, node_active, aim_match, ammo_experiment;
    std::uint32_t gun3c0, mode2c1, remote_flags, remote_equip, manifest_rev;
    std::int32_t body_equip, aim_err_mdeg;
    // Native rates (docs/native-puppet.md "Rates"): the setting; instance 0's
    // (the local player) and instance 1's (the partner body) rate records
    // rec+0x3C..+0x50 x 1000 in partner_rates::Field order (ok false: unread,
    // `refusal` names why); the remote's wire rates {dash +0x48, crawl +0x3C,
    // crawl factor +0x40} x 1000 and their age; the mirror state off|wait|
    // ok|latched with its reason, and the hook's process totals.
    PartnerNativeRates rates_setting;
    bool rates_id0_ok, rates_id1_ok;
    std::int32_t rates_id0[6], rates_id1[6];
    char rates_id1_refusal[16];
    bool rates_remote_ok;
    std::uint32_t rates_remote_age_ms;
    std::uint32_t rates_remote[3];
    char rates_mirror[8];
    char rates_reason[12];
    std::uint32_t rates_writes, rates_reapplied, rates_restores, rates_drops;
    // Weapons (docs/native-puppet.md "Weapons"). Instance 1's gun record
    // (fields_ok false: the values below are kNull/0): capacity, magazine
    // (getter rule), stock, equip, class, the no-reload bit, rec+0x3D5, the
    // slot, the reload request bit 20, the CanReload replica and the reload gate.
    bool gun_fields_ok;
    std::int32_t gun_cap, gun_mag, gun_stock, gun_class, gun_b3d5, gun_slot;
    std::uint32_t gun_noreload, gun_b20, gun_can_reload, gun_gate;
    // Fire visuals: setting, the filter (installed, live, armed), whether FIRE
    // passes the effective mask and why not, the filter's process totals and
    // suppressed per second; the read-only census of the bullet and noise
    // interfaces (RVAs, kNull when unread).
    char fire_mode[8];
    bool fire_installed, fire_live, fire_armed, fire_effective;
    char fire_refused[16];
    std::uint32_t fire_suppressed, fire_passed, fire_passed_player, fire_owner_disarmed, fire_suppressed_s;
    bool census_ok, census_bullets_ok;
    std::int64_t census_noise_vt, census_noise_fn;
    // Magazine mirror (drain windows) and the leak checks: the remote's
    // latest v4 magazine (kNull without), writes/refusals, the last values and
    // verdict, the viewer's shoot count (kNull unread) and the drain windows
    // that changed it, and drains that left instance 1's reload request set.
    std::int32_t remote_mag, remote_mag_cap;
    // The viewer's own magazine and capacity (instance 0, getter rule; kNull
    // unread): unchanged while only the remote fires (leak evidence).
    std::int32_t local_mag, local_mag_cap;
    std::uint32_t mag_writes, mag_refused, mag_fire_edges, mag_reload_edges, mag_cap, mag_written;
    char mag_last[8];
    bool mag_faulted, mag_leak_off;  // mag_leak_off: an ammo leak turned the mirror off until MissionStart
    std::uint32_t shot_leaks, reload_drains;
    std::int64_t shot_count;
    // The viewer's weapon/ammo vars block (vars+0x548..+0x670): drain windows
    // whose job changed it (instance 1's reload must never touch the viewer's
    // inventory) and its digest after the last drain (kNull unread).
    std::uint32_t ammo_leaks;
    std::int64_t ammo_digest;
    // F1/F2/F3/F4 (docs/native-puppet.md "Weapons"): the sample rings of the
    // drain windows, the magazine store row, the injected remote magazine,
    // the noise filter and the weapon gate (the remote's gun equip vs body 1's).
    bool ring_on;
    std::uint32_t ring_windows, ring_refused, ring_changed;
    char ring_last[8];
    std::uint32_t store_writes, store_refused;
    char store_last[8];
    std::int32_t mag_inj, mag_inj_cap;
    char noise_mode[8];
    bool noise_installed, noise_live;
    std::uint32_t noise_dropped, noise_passed;
    bool kb_remote;                   // the remote's pad is in keyboard mode (no xflags Gamepad)
    std::int32_t remote_gun_equip;    // kNull without xflags Equip
    std::int32_t remote_gun_base;     // as a stock id (the received manifest maps an alias); kNull without
    bool weapon_mismatch;
    std::uint32_t strip_bits;
    // Online play (docs/native-puppet.md "Online"): body 1 never showed a gun
    // class this mission under the gun profile (HOLD, RELOAD, FIRE stripped),
    // and the process-wide play leak strip (FIRE | RELOAD after a shot or ammo leak; 0 none).
    bool weapon_absent;
    bool weapon_standin;
    std::uint32_t weapon_standin_ms, weapon_absent_ms;
    std::uint32_t leak_strip;
    // The partner's trigger after each windowed drain (process totals):
    // shots it requested (rec+0x3C0 bit 24, summed over rec+0x3DF), drains
    // that requested one, and drains that left the gun in a fire mode
    // (rec+0x2C0 9 or 10).
    std::uint32_t fire_requested, fire_shot_drains, fire_modes;
    // The partner's own reload (docs/native-puppet.md "Weapons" R1..R6). Body
    // 1's gun record: the current mode rec+0x2C0, the upper-body request
    // *(Q+0x20)[1] (5 = reload), the reload shadows rec+0x3E4/+0x3E6/+0x3E8,
    // the slot in hand rec+0x3D9 and the slot source rec+0x3DE (kNull unread),
    // rec+0x3C0 bit 26 (a reload mode is active).
    std::int32_t gun_mode_cur, gun_upper_req, gun_m3e4, gun_m3e6, gun_m3e8, gun_hand_slot, gun_slot_src;
    std::uint32_t gun_b26;
    // The event machine (partner_reload): the setting, its state, the last
    // fallback reason, the remote's v5 gun group (mode and counter kNull
    // without), whether it holds or released a stuck body reload, the mission
    // counters and the sample strip of the published Arm.
    char reload_setting[8], reload_state[8], reload_last[12];
    bool reload_remote_valid, reload_remote_reloading, reload_hold, reload_stuck;
    std::int32_t reload_remote_mode, reload_remote_seq;
    std::uint32_t reload_starts, reload_taps, reload_entered, reload_completed, reload_stuck_n, reload_fallbacks, reload_body_entries;
    std::uint32_t reload_sample_strip;
    // F1 (run 53e36944): exits after stuck_ms (counted as completed too), the
    // queued taps and whether one asks for the reconcile now; the last and
    // longest native reload length, the remote's last mode-11/12 span and
    // body_enter -> body 1's own first magazine change (milliseconds, 0
    // until measured).
    std::uint32_t reload_completed_late, reload_queued_taps;
    bool reload_reconcile;
    // A body reload mode past release_ms (a lost exit): released now, and how
    // often this mission (the mirror stops waiting for the exit).
    bool reload_release;
    std::uint32_t reload_releases;
    std::uint32_t reload_len_ms, reload_len_max_ms, reload_remote_len_ms, reload_refill_ms;
    // The magazine mirror's freeze (hook process totals): RELOAD edges seen
    // (written or not), drains frozen, reconcile writes and rec+0x3E8 writes.
    std::uint32_t mag_reload_seen, mag_frozen, mag_reconciles, mag_m3e8_writes;
    // The post-phase-1 probe (hook process totals): phase-1 calls that left
    // the reload request bit 20 / the trigger's bit 24 set, shots, rec+0x2C0
    // changes across phase 1 and the last mode seen there.
    std::uint32_t probe1_b20, probe1_b24, probe1_shots, probe1_mode_changes, probe1_mode;
    // The weapon switch (W4/W6): the follower setting, the remote's v5 slot
    // and switch counter (kNull without), body 1's current slot (row+0x36;
    // kNull unread), the follower's last acted-on slot (kNull none), body 1's
    // handles per slot (filled: a weapon; equip/type then), the plan's tiers
    // and admitted mask, the follower's totals, last verdict and fault latch,
    // the generalized gate's verdict, body 1's in-hand type (kNull unread) and
    // whether it is mid-switch.
    char slot_setting[8], slot_last[12], weapon_gate[12];
    std::int32_t slot_remote, slot_remote_seq, slot_body, slot_target, body_type;
    bool slots_ok;
    bool slot_filled[3];
    std::uint16_t slot_equip[3];
    std::uint8_t slot_type[3];
    char slot_tier[3][8];
    std::uint8_t slot_ok_mask;
    std::uint8_t slot_map[3];  // remote slot -> admitted body slot, 0xFF none
    std::uint32_t slot_writes, slot_refused;
    bool slot_faulted, body_switching;
    // Loadout: what this copy sends (manifest revision, base ids, aliases,
    // tables verified) and the apply verdict of the received manifest.
    std::uint32_t equip_tx_rev;
    std::uint32_t equip_tx_base[3], equip_tx_alias[3];
    bool equip_tables_ok;
    std::uint32_t equip_rx_base[3], equip_rx_alias[3];
    // Manifest version 2: each slot's own gun type (0 unknown) sent and
    // received, and the received manifest's version (0 none).
    std::uint8_t equip_tx_type[3], equip_rx_type[3];
    std::uint32_t equip_rx_version;
    // The received manifest's binding and the receiver's counters (0.13.6
    // online-net-epoch; process-lifetime): the sender's session number (identity
    // only), this copy's link session it arrived in, accepted / invalid /
    // no-link (stale) / duplicate messages and the last refusal reason.
    std::uint32_t equip_rx_sender_epoch, equip_rx_link_epoch;
    std::uint32_t equip_rx_received, equip_rx_invalid, equip_rx_stale, equip_rx_duplicates;
    char equip_rx_refused[12];
    char equip_apply[12];
    // The generalized writer and preload (W2/W3/W6; docs/native-puppet.md
    // "Generalized writer"): the setting (stock46|manifest|substitute), per
    // slot the manifest's base id, the planned equip, the tier (a1|c|d|none),
    // the family (optional|plain|none), the state, its reason and the
    // dispatched handle; the counters; the stock cache census
    // (native.equip.resident: the used entries of 48) and the preload verdict.
    char equip_setting[12];
    bool writer_active;
    char writer_shared[8];
    std::uint32_t writer_gen, writer_resyncs, writer_kept;
    std::uint16_t writer_want[3], writer_equip[3];
    char writer_tier[3][6], writer_family[3][10], writer_state[3][12], writer_reason[3][32];
    std::uint32_t writer_handle[3];
    // Per slot: the manifest's type of the slot (0 unknown), the tier C
    // candidates tried and the last one (0 none).
    std::uint8_t writer_want_type[3];
    std::uint32_t writer_candidates[3];
    std::uint16_t writer_candidate[3];
    std::uint8_t writer_retries[3];
    std::uint32_t writer_refs_before[3], writer_refs_after[3], writer_binder[3][3];
    char writer_bank[3][8];
    std::uint32_t writer_dispatches, writer_bound, writer_refused;
    bool resident_ok;
    std::uint8_t resident_count;
    struct Resident {
        std::uint16_t equip;
        std::uint8_t type, receiver, flags, index;
        std::uint32_t refs;
        std::uint8_t viewer;
    } resident[48];
    std::uint16_t preload_ids[3];
    std::uint8_t preload_count;
    char preload_verdict[24], preload_reason[32];
    std::uint32_t preload_new_hashes;
    // Facing governor (partner_recon; docs/native-puppet.md "Facing"): its
    // mode off|idle|settle|aim|contested, the error, target and body yaw
    // (centidegrees, kNull unread), the body's own yaw rate (deg/s), the
    // counters since the mission start and why it is not acting.
    char face_setting[8];
    char face_mode[12];
    std::int32_t face_err_cdeg, face_target_cdeg, face_body_cdeg, face_rate_dps;
    std::uint32_t face_writes, face_snaps, face_contests, face_turns;
    char face_reason[12];
    // Placement verdicts: the last placement's yaw error at verify (mdeg).
    std::int32_t place_yaw_err_mdeg;
    std::uint32_t neutral_windows;
    // The look (D3): the source hook, its writes per second, the remote's
    // look and instance 1's slot after the last windowed drain (centidegrees
    // {pitch, yaw}, kNull unread) and the yaw error slot - remote (mdeg).
    char look_src[12];
    std::uint32_t look_src_hits_s, look_src_hits, look_src_refused, look_src_skipped;
    std::int32_t look_remote_cdeg[2], look_slot_cdeg[2], look_err_mdeg;
    // Aim parity (docs/native-puppet.md "Facing", the aim-target mirror): the
    // effective setting (lab.ini or the debug override), the mirror's state
    // off|wait|ok|hold|refused|latched with its reason, the target source,
    // the hook's process totals; the target, the hook's last write (the slot
    // must equal it: the driver tick and the drain share one thread, so the
    // target leads it by at most one tick), both network aim slots Q+0x10[k]
    // and both gun aim points gunRec[k]+0x150 (mm, kNull unread), the aim
    // point's distance from the target (mm); the world aim direction of each
    // gun (aim point - (body + 1.6 m), centidegrees), the yaw the aim-turn
    // predicate tests ((0.5 - r60) * 18000, centidegrees), the aim parameters
    // r5c/r60 x1000 and the predicate (1/0, kNull unread), the gun state
    // rec+0x2C0, rec+0x3D5 and the aim point path cam|bone|na.
    char aim_setting[8];
    bool aim_override;
    char aim_state[8];
    char aim_reason[12];
    char aim_src[8];
    std::uint32_t aim_writes, aim_refused, aim_holds, aim_overwritten, aim_restores, aim_drops;
    std::int32_t aim_target_mm[3], aim_written_mm[3], aim_q10_mm[2][3], aim_point_mm[2][3], aim_point_err_mm;
    // Q+0x10[1] against the hook's last write: ours (every axis within 1 mm),
    // other (restored, or something else wrote it), na (either unread).
    char aim_slot[8];
    std::int32_t aim_yaw_cdeg[2], aim_pitch_cdeg[2], aim_rel_yaw_cdeg[2];
    std::int32_t aim_r5c[2], aim_r60[2], aim_pred[2];
    std::int32_t aim_gun_state[2], aim_b3d5[2];
    char aim_path[2][8];
    std::int32_t gun_pitch_cdeg[2], gun_yaw_cdeg[2], remote_dir_cdeg[2], dir_err_cdeg[2];
    char dir_refused[16];
    std::uint32_t dir_checks, dir_misses;
    Ring ring;
};


} // namespace mgscoop::partner_native
