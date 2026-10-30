#pragma once
// Offline partner spike: the native state machine behind the partner receipts
// (docs/offline-partner-spike-plan.md 2.2, 2.3 item 9, 2.6 and 2.7), plus the
// animation, movement-driver and appearance experiments of
// docs/partner-anim-appearance.md.
//
// Decision logic only. Every effect goes through Hooks: game reads and the
// guarded writes/calls (partner_player), the remote pose ring, look and
// transmit (partner_net), the partner.trigger file and the log. The script
// observer owns threading (runtime thread, receipt lock), the receipt
// plumbing and the real hooks; tests/partner_driver_tests.cpp drives the same
// code with fakes on deterministic timelines. Not thread safe: callers serialize.
//
// Log caps per mission: Lua notes 64; refused writes, lost bits, tick parse
// rejections, forced exists and all-active re-asserts 8; probe lines 400;
// partner_cfg refused/ready flips 64 (then "partner_cfg=capped; limit=64");
// realized/parts transitions 64 (then "partner_realized=capped; limit=64");
// partner_flags changes 64 (then "partner_flags=capped; limit=64"); xform,
// hybrid and look lines 64 each (then "<head>=capped; limit=64"); the anim
// family (partner_anim=source, partner_aset=q) 64 ("partner_anim=capped");
// partner_stance set/req, refused and reverted 64 each
// ("partner_stance=capped; limit=64; kind=set|refused|reverted"), plus the
// uncapped partner_stance=summary at most every kStanceSummaryMs while its
// counters change; partner_ps probe lines 400. dm=map (partner_dmmap): the
// table, failed and released lines uncapped (bounded by triggers and
// missions); switch and playing lines share kDmMapSwitchCap (then
// "partner_dmmap=capped; limit=600; kind=switch"); deferred, lost,
// stop_timeout, unconsumed, adopted, abandoned and bad_gani 64 each
// ("partner_dmmap=capped; limit=64; kind=<family>"); the summary every
// kDmMapSummaryMs, kDmMapSummaryCap lines ("...; limit=720; kind=summary").
// 0.13.6: the partner_recon event families and partner_body_safety rotate
// (log_budget: kReconCapLimits / kBodySafetyCapLimits, a window cap with a
// resumed summary and a per-mission ceiling) instead of 64 per mission.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_dmmap.hpp"
#include "mgscoop/partner_act_log.hpp"
#include "mgscoop/partner_dm_overlay.hpp"
#include "mgscoop/partner_facing.hpp"
#include "mgscoop/partner_source_turns.hpp"
#include "mgscoop/partner_input.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_aim_point.hpp"
#include "mgscoop/partner_aim.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_caps_wire.hpp"
#include "mgscoop/partner_equip_wire.hpp"
#include "mgscoop/partner_ride_wire.hpp"
#include "mgscoop/partner_mount_policy.hpp"
#include "mgscoop/partner_vehicle.hpp"
#include "mgscoop/partner_board.hpp"
#include "mgscoop/partner_pass_camera.hpp"
#include "mgscoop/equipment_stock_writer.hpp"
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/partner_reload.hpp"
#include "mgscoop/partner_slots.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/partner_native_net.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_recon.hpp"
#include "mgscoop/partner_body_safety.hpp"
#include "mgscoop/log_budget.hpp"
#include "mgscoop/partner_traverse.hpp"
#include "mgscoop/partner_icon.hpp"
#include "mgscoop/partner_ui_filter.hpp"
#include "mgscoop/partner_gesture.hpp"
#include "mgscoop/partner_gesture_policy.hpp"
#include "mgscoop/partner_seat_gun.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/player_sync.hpp"
#include "mgscoop/pose_interp.hpp"

namespace mgscoop::partner_driver {

inline constexpr unsigned kProbeKeys = 20;
inline constexpr unsigned kProbeLineCap = 400;
inline constexpr unsigned kLuaLogCap = 64;
inline constexpr unsigned kCappedLogs = 8;
inline constexpr unsigned kTransitionLogCap = 64;
// 0.13.6: the partner_recon event families rotate (64 lines per 2 min window,
// 1024 per family and mission; "partner_recon=capped; limit=64; kind=..",
// "partner_recon=resumed; kind=..; dropped=..", "...; limit=1024; kind=..; scope=mission"),
// and the partner_body_safety lines the same with 512 per mission.
inline constexpr log_budget::Limits kReconCapLimits{64, 120000, 1024};
inline constexpr log_budget::Limits kBodySafetyCapLimits{64, 120000, 512};
// The body-safety re-show waits at most this long for the engine to unrealize body 1.
inline constexpr std::uint64_t kReshowUnrealizeMs = 2500;
inline constexpr std::uint64_t kTriggerPollTicks = 30;
inline constexpr unsigned kForceExistsAfterTicks = 10;
inline constexpr std::uint64_t kPartsTimeoutMs = 15000;
// F3c: the mirror diagnostic lasts at most this long (spec section 3, E3).
inline constexpr std::uint64_t kMirrorMaxMs = 10000;
// SET_ALLACTIVE re-assert and MIRROR_OFF resend: at most once per second, at
// most kCappedLogs times per mission.
inline constexpr std::uint64_t kResendMs = 1000;
// Stance changes are applied after holding this long (debounce), and a
// partner_stance=reverted line is logged when the stance word changes on its
// own within kStanceWatchMs of a set.
inline constexpr std::uint32_t kStanceHoldMs = 150;
inline constexpr std::uint64_t kStanceWatchMs = 500;
// A refused stance call is retried for the same target after this long; the
// stance counters are summarized at most this often.
inline constexpr std::uint32_t kStanceRetryMs = 1000;
inline constexpr std::uint64_t kStanceSummaryMs = 10000;
// hybrid: a full Warp when the body is this far from the commanded pose.
inline constexpr std::uint32_t kHybridErrorMm = 1500;
// Looks: at most one SET_PARTS per kLookMinGapMs; "loaded" needs the parts
// state cycle (or kLookSettleMs of a stable match); timeout after kPartsTimeoutMs;
// look=net logs partner_look=waiting once kLookWaitMs after all-active.
inline constexpr std::uint64_t kLookMinGapMs = 2000;
inline constexpr std::uint64_t kLookSettleMs = 3000;
inline constexpr std::uint64_t kLookWaitMs = 5000;
// probe=ps cadence (4 Hz).
inline constexpr std::uint64_t kPsProbeMs = 250;
// Round 3. The look fix: stall held this long before recovery; COPY withheld
// for look=net until a look arrives or this long after all-active (= kLookWaitMs).
inline constexpr std::uint64_t kStallMs = 1000;
// Input lever cadences and the diagnostic P+0x218 window's auto-expiry.
inline constexpr std::uint64_t kInputStatsMs = 1000;
inline constexpr std::uint64_t kBodyMs = 500;
inline constexpr std::uint64_t kPadProbeMs = 50;
inline constexpr std::uint64_t kP218MaxMs = 20000;
// Round 4 reconciliation lines: the 1 Hz partner_recon line (3600 per
// mission = one hour), each event family 64 lines (the counters continue in
// the 1 Hz line), the 2 Hz partner_body line 3600 per mission, and the 1 Hz
// partner_wrap census line 600 per mission.
inline constexpr std::uint64_t kReconLineMs = 1000;
inline constexpr unsigned kReconLineCap = 3600;
inline constexpr unsigned kWrapLineCap = 600;
// partner_body speed: over the oldest same-epoch sample of the ring (32 Lua
// ticks, ~530 ms at 60 Hz), divided by the actual span, which must be at
// least kSpeedSpanMs (na otherwise).
inline constexpr std::uint64_t kSpeedSpanMs = 150;
inline constexpr unsigned kBodyRing = 32;
// The corrective stance input of the reconciliation (StanceFix): PlayerPad
// STANCE (0x2); a hold lasts this long (>= 12 windows: classify(1,12) == 3).
inline constexpr std::uint32_t kStanceBit = 0x2;
inline constexpr std::uint32_t kStanceFixHoldMs = 400;
inline constexpr std::uint64_t kDmUnconsumedMs = 2000;
inline constexpr std::uint64_t kDmProbeMs = 500;
inline constexpr unsigned kInputStatsCap = 600;
inline constexpr unsigned kBodyCap = 3600;
inline constexpr unsigned kPadProbeCap = 400;
inline constexpr unsigned kHoldLogCap = 200;
inline constexpr unsigned kRefusedCap = 64;
inline constexpr unsigned kDmProbeCap = 400;
// dm=map summary cadence and caps (one hour of summaries; switch/playing lines).
inline constexpr std::uint64_t kDmMapSummaryMs = 5000;
inline constexpr unsigned kDmMapSummaryCap = 720;
inline constexpr unsigned kDmMapSwitchCap = 600;
// partner_lost=hide (lab-tested only): the realize bits are cleared after the
// net sample has been lost this long; the next live sample re-realizes.
inline constexpr std::uint64_t kLostHideMs = 10000;
// Round-1 actions track (docs/anim-round1-actions.md): the DM overlay's own
// line cap per mission (partner_overlay=..., then "partner_overlay=capped").
// The act census lines have their own caps (partner_act_log).
inline constexpr unsigned kOverlayLogCap = 400;
// Drained input-hook statistics (mirror of partner_input_hook::Stats).
struct InputStats {
    partner_input::WindowStats window;
    partner_input::HoldRecord holds[8];
    unsigned hold_count;
    partner_input_hook::WrapStats wrap;
    partner_input_hook::NativeStats native;
};
// Native puppet (docs/native-puppet.md): log cadences and caps.
inline constexpr std::uint64_t kNativeLineMs = 1000;
inline constexpr unsigned kNativeLineCap = 3600;   // partner_defer / partner_native 1 Hz lines per mission
inline constexpr unsigned kNativeEventCap = 64;    // transitions, enter steps, gun changes, manifests
inline constexpr std::uint64_t kNativeMatchWindowMs = 10000;
inline constexpr std::uint64_t kAimMatchMs = 300;
// Anim round 1 weapons (docs/anim-round1-weapons.md): line cadences and caps.
inline constexpr std::uint64_t kWeaponCtxMs = 100;          // partner_weapon_ctx: at most 10 lines/s, no cap
inline constexpr std::uint64_t kNativeGunChangeMs = 100;    // partner_native_gun change lines
inline constexpr unsigned kNativeGunChangeCap = 3600;
inline constexpr std::uint64_t kAimIntentLineMs = 100;      // partner_aim_intent change lines
inline constexpr unsigned kAimIntentLineCap = 2000;
inline constexpr unsigned kAnimWeaponCap = 256;             // partner_weapon=unsupported / dry / rate lines (each family)
inline constexpr unsigned kCensusLinesPerSecond = 20;       // partner_fire_census per-request lines
inline constexpr unsigned kCensusLineCap = 4000;
inline constexpr std::uint64_t kAimActHoldMs = 150;         // the Arm's aim act expiry (a stale Arm drops it)
inline constexpr std::uint64_t kAimReadMs = 250;
// What the observer reports about the native installs (H0 input hook, wraps,
// H2, H1, and the phase-1 local detour P1, installed with defer=on).
struct NativeInstall {
    bool h0, wrap, h1, h2;
    bool live_h0, live_h1, live_h2;
    bool p1, live_p1;
};

enum class TriggerRead : std::uint8_t { none, read, unreadable };

// The newest look received from the partner (partner_net::SampleLook).
struct RemoteLook {
    bool valid;
    partner::Look look;
    std::uint16_t revision;
};

struct Hooks {
    void* context;
    void (*log)(void* context, const char* line) noexcept;
    partner_player::Refusal (*read_state)(void* context, partner_player::State& out) noexcept;
    // Guarded writes: nullptr on success, otherwise a static reason.
    const char* (*or_bits)(void* context, bool terminating, std::uint32_t& q194, std::uint32_t& q198) noexcept;
    const char* (*clear_bits)(void* context, bool terminating, std::uint32_t& q194, std::uint32_t& q198) noexcept;
    const char* (*write_slot)(void* context, bool terminating, const float (&xyzw)[4]) noexcept;
    const char* (*force_exists)(void* context, bool terminating) noexcept;
    const char* (*set_policy)(void* context, bool relax, std::uint8_t restore_value, bool terminating,
                              std::uint8_t& previous) noexcept;
    bool (*read_anim)(void* context, const partner_player::State& state, partner_player::Anim& out) noexcept;
    pose::Sample (*sample_remote)(void* context, std::uint32_t delay_ms) noexcept;
    void (*submit_local)(void* context, const partner::Tick& tick, const sync::AnimState& anim) noexcept;
    // partner.trigger: reads at most `capacity` bytes and deletes the file.
    TriggerRead (*take_trigger)(void* context, char* text, std::size_t capacity, std::size_t& length) noexcept;
    bool (*focused)(void* context) noexcept;
    // --- Animation, movement drivers and appearance. Any of these may be null
    // (the feature then refuses with reason=unavailable).
    bool (*read_looks)(void* context, const partner_player::State& state, partner_player::Looks& out) noexcept;
    bool (*submit_look)(void* context, const partner::Look& look) noexcept;
    RemoteLook (*sample_look)(void* context) noexcept;
    const char* (*write_anim)(void* context, bool terminating, std::uint16_t act, std::uint8_t stance, std::uint8_t speed) noexcept;
    const char* (*write_face_equip)(void* context, bool terminating, std::uint8_t value) noexcept;
    const char* (*set_stance)(void* context, bool terminating, std::uint8_t stance, std::uint8_t& psw, std::uint8_t& q30) noexcept;
    const char* (*request_stance)(void* context, bool terminating, std::uint8_t stance) noexcept;
    const char* (*apply_transform)(void* context, bool terminating, const float (&pos)[4], const float (&quat)[4],
                                   PartnerReset reset) noexcept;
    bool (*read_body)(void* context, const partner_player::State& state, float (&pos)[4], float (&quat)[4]) noexcept;
    // Pure-read census of both instances; the formatter writes one
    // "partner_census; k=.." line (partner_player::FormatCensus in the observer).
    const char* (*census)(void* context, const partner_player::State& state, partner_player::Census& out) noexcept;
    std::size_t (*format_census)(void* context, const partner_player::Census& census, std::uint32_t k, char* out,
                                 std::size_t capacity) noexcept;
    std::size_t (*format_ps_probe)(void* context, const partner_player::State& state, std::uint32_t k, char* out,
                                   std::size_t capacity) noexcept;
    // --- Round 3 input lever (any may be null; the feature then refuses).
    const char* (*input_targets)(void* context, const partner_player::State& state, partner_player::InputTargets& out) noexcept;
    std::size_t (*format_input_census)(void* context, const partner_player::InputTargets& targets, char* out,
                                       std::size_t capacity) noexcept;
    bool (*read_pad)(void* context, const partner_player::State& state, partner_input::PadRaw& out) noexcept;
    bool (*submit_input)(void* context, const partner_input::InputPacket& packet) noexcept;
    bool (*net_running)(void* context) noexcept;
    // Lazy hook install (nullptr ok, else a reason; "hook_disabled" when the
    // lab key forbids it), publish, drain, and the 1 Hz patch-live check.
    const char* (*install_input_hook)(void* context) noexcept;
    void (*publish_arm)(void* context, const partner_input::Arm& arm) noexcept;
    void (*drain_input_stats)(void* context, InputStats& out) noexcept;
    bool (*input_hook_live)(void* context) noexcept;
    // --- Round 3 direct motion.
    const char* (*dm_targets)(void* context, const partner_player::State& state, partner_player::DmTargets& out) noexcept;
    bool (*read_dm)(void* context, const partner_player::State& state, const partner_player::DmTargets& targets,
                    partner_player::DmState& out) noexcept;
    std::size_t (*format_dm_census)(void* context, const partner_player::DmTargets& targets, char* out, std::size_t capacity) noexcept;
    std::size_t (*format_dm_probe)(void* context, const partner_player::DmState& dm, bool patched, char* out,
                                   std::size_t capacity) noexcept;
    bool (*dm_patch)(void* context, bool on, const char* reason) noexcept;  // apply/revert; returns the resulting patched state
    bool (*dm_clear_pending)(void* context) noexcept;
    // Clears the hook's recorded window fault (MissionStart; may be null).
    void (*reset_input_fault)(void* context) noexcept;
    // --- Round 4 (any may be null; the feature then refuses or logs na).
    // Position-only body set (T.vt+0x58) for nudges.
    const char* (*apply_position)(void* context, bool terminating, const float (&pos)[4]) noexcept;
    // The game's per-id velocity PS+0x268 (diagnostic).
    bool (*read_velocity)(void* context, const partner_player::State& state, float (&vel)[4]) noexcept;
    // partner_net::LatestInput (read-only; the recon's input age and stick).
    bool (*latest_input)(void* context, partner_input::Sample& out) noexcept;
    // Lazy install of the wrap entries (nullptr ok, else a reason).
    const char* (*install_wrap)(void* context) noexcept;
    // The interpolation delay for the net source given lab.ini
    // partner_interp_ms (partner_net::EffectiveInterpMs in the observer: the
    // adaptive delay with partner_interp=auto). Null: the configured value.
    std::uint32_t (*interp_ms)(void* context, std::uint32_t configured) noexcept;
    // Process-lifetime verified runtime archive bound; nullptr is baseline13.
    unsigned (*dm_motion_count)(void* context) noexcept = nullptr;
    pose::RemoteFrame (*sample_remote_with_turns)(void* context, std::uint32_t delay_ms,
                                                const pose::HistoryCursor& cursor) noexcept = nullptr;
    // Read-only local body transform (instance 0) for facing telemetry; null: unread.
    bool (*read_local_body)(void* context, const partner_player::State& state, float (&pos)[4], float (&quat)[4]) noexcept = nullptr;
    // --- Native puppet (docs/native-puppet.md; any may be null: native then
    // refuses with reason=install_unavailable and stays in fallback).
    // Lazy install of H0 + wraps + H2 + H1 (once per process): nullptr ok, else
    // a static reason ("hook_disabled" when lab partner_input_hook=0).
    const char* (*install_defer)(void* context, PartnerDefer mode) noexcept = nullptr;
    void (*native_install)(void* context, NativeInstall& out) noexcept = nullptr;
    void (*drain_defer_stats)(void* context, partner_defer::Stats& out) noexcept = nullptr;
    // Arms/disarms the deferral and sets its mode (census|on).
    void (*set_defer)(void* context, bool armed, PartnerDefer mode) noexcept = nullptr;
    // valid=false clears the identity.
    void (*publish_identity)(void* context, const partner_defer::Identity& identity, bool valid) noexcept = nullptr;
    void (*publish_wrap_roots)(void* context, std::uintptr_t root0, std::uintptr_t root1, bool census) noexcept = nullptr;
    const char* (*native_targets)(void* context, const partner_player::State& state, const partner_player::InputTargets& targets,
                                  partner_player::NativeTargets& out) noexcept = nullptr;
    bool (*read_gun1)(void* context, const partner_player::State& state, partner_player::Gun1& out) noexcept = nullptr;
    bool (*read_look_source)(void* context, const partner_player::State& state, partner_player::LookSource& out) noexcept = nullptr;
    std::uint32_t (*window_seq)(void* context) noexcept = nullptr;
    bool (*link_live)(void* context) noexcept = nullptr;
    std::uint32_t (*link_session)(void* context) noexcept = nullptr;
    // Reliable channel 2 (equipment manifest): drained every tick in every drive mode.
    bool (*drain_reliable)(void* context, std::uint8_t channel, std::uint8_t* out, std::size_t capacity, std::size_t& size) noexcept = nullptr;
    // Native rates (docs/native-puppet.md "Rates"): instance `index`'s rate
    // record (partner_player::ReadRates), and the hook's forget (MissionStart/End)
    // of the ending mission's rates epoch (its Arm never applies again).
    bool (*read_rates)(void* context, const partner_player::State& state, std::uint32_t index, partner_player::RateRead& out) noexcept =
        nullptr;
    void (*forget_rates)(void* context, std::uint32_t epoch) noexcept = nullptr;
    // Whether the build carries the ammo experiment (MGS_LOCAL_STOCK46_AMMO_TEST); telemetry only.
    bool ammo_experiment = false;
    // --- Native weapons (docs/native-puppet.md "Weapons"; any may be null:
    // FIRE then stays masked with fire_refused=unavailable, the sender sends
    // no magazine and no manifest).
    // Lazy install of the RequestBullet owner filter (once per process), armed
    // for `owner` (instance 1's GameObjectId): nullptr ok, else a static reason.
    const char* (*install_fire)(void* context, std::uint16_t owner) noexcept = nullptr;
    void (*fire_stats)(void* context, partner_fire_filter::Stats& out) noexcept = nullptr;
    void (*fire_census)(void* context, partner_fire_filter::Census& out) noexcept = nullptr;
    // Instance k's gun record (0: the local player, the v4 magazine sender).
    bool (*read_gun)(void* context, const partner_player::State& state, std::uint32_t k, partner_player::Gun1& out) noexcept = nullptr;
    // The local loadout (partner_player::ReadLoadout): vars and the type
    // tables only, no partner-state gate (the early manifest runs in any mission).
    bool (*read_loadout)(void* context, partner_player::Loadout& out) noexcept = nullptr;
    // Reliable channel `channel` (append mode since the capability message
    // shares channel 2: latest would replace the other's unsent message):
    // true when queued.
    bool (*send_reliable)(void* context, std::uint8_t channel, const std::uint8_t* bytes, std::size_t size) noexcept = nullptr;
    // Native polish (docs/native-puppet.md "Facing", "Weapons"): whether any
    // batch is in flight or the game window is closing (partner_defer::InBatch
    // or ShuttingDown: facing governor writes and the look source install wait), the look source hook (D3, naim=looksrc;
    // nullptr or a static refusal) and the shot noise filter (F3).
    bool (*in_batch)(void* context) noexcept = nullptr;
    const char* (*install_look_source)(void* context) noexcept = nullptr;
    const char* (*install_noise)(void* context) noexcept = nullptr;
    // Online play (docs/native-puppet.md "Online"): reliable channel `channel`
    // in append mode (the capability message shares channel 2 with the
    // manifest; true when queued). Null: no capabilities are sent.
    bool (*send_reliable_append)(void* context, std::uint8_t channel, const std::uint8_t* bytes, std::size_t size) noexcept = nullptr;
    // Aim parity (docs/native-puppet.md "Facing", the aim-target mirror; any
    // may be null: the mirror then stays off and the telemetry unread): the
    // target publication the drain window writes, the mirror's statistics
    // (with the debug override) and the read-only aim telemetry.
    void (*publish_aim)(void* context, const partner_aim_point::Publication& publication) noexcept = nullptr;
    void (*aim_stats)(void* context, partner_aim_point::Stats& out) noexcept = nullptr;
    bool (*read_aim)(void* context, const partner_player::State& state, partner_player::AimRead& out) noexcept = nullptr;
    // The partner's weapon switch (docs/native-puppet.md "Weapons" W4/W6; may
    // be null: the follower then has no plan and refuses): instance k's slots
    // (partner_player::ReadSlots; 0 = the local player, the v5 sender's slot)
    // and the equip type of a stock id (partner_player::EquipType).
    bool (*read_slots)(void* context, const partner_player::State& state, std::uint32_t k, partner_player::Slots& out) noexcept = nullptr;
    bool (*equip_type)(void* context, std::uint32_t equip, std::uint8_t& type) noexcept = nullptr;
    // Input frame v5 negotiation (docs/native-puppet.md "Wire"; may be null:
    // v5 always, both copies one build): whether the peer was seen sending v5
    // (partner_net::PeerSendsInputV5).
    bool (*peer_input_v5)(void* context) noexcept = nullptr;
    bool (*peer_input_v6)(void* context) noexcept = nullptr;
    // Private native-pose lease: no normal stance/animation/DM command may override it.
    bool (*native_pose_suppressed)(void* context) noexcept = nullptr;
    // Actual payload+origin dequeued together; old hooks remain optional.
    bool (*drain_reliable_origin)(void*,std::uint8_t,std::uint8_t*,std::size_t,std::size_t&,partner_net::ReliableOrigin&) noexcept = nullptr;
    bool (*receive_vehicle)(void*,const partner_net::ReliableOrigin&,const std::uint8_t*,std::size_t) noexcept = nullptr;
    bool (*native_vehicle_attached)(void*,const partner_player::State&) noexcept = nullptr;
    // Fresh mount intent or current native cleanup lease, matched to the
    // existing Arm. Keeps that Arm; never creates a new admission.
    bool (*native_vehicle_job)(void*,const partner_player::State&,const partner_input::Arm&) noexcept = nullptr;
    // --- The seated vehicle rider (partner_native_ride=seat, docs/anim-round1-seat.md;
    // any null: the seat refuses with reason=unavailable and the ride hides as before).
    // Arms the four partner_ride_seat dwords for `seat` (true: armed) / reverts them.
    bool (*ride_seat_arm)(void* context, std::uint8_t seat) noexcept = nullptr;
    void (*ride_seat_revert)(void* context, const char* reason) noexcept = nullptr;
    // Round-1 actions (docs/anim-round1-actions.md "DM overlay"): the hook
    // point of a camera-skip module (the DM entry/End camera-connect stores
    // skipped while armed). While it returns true the DM overlay may play with
    // a non-zero local camera connect (CAM+0x1F0/+0x200); null or false keeps
    // the cam_connect refusal. Nothing in this track arms one.
    bool (*dm_camera_skip_armed)(void* context) noexcept = nullptr;
    bool (*support_ready)(void*,const partner_player::State&,std::uint16_t equip) noexcept = nullptr;
    // --- WS3 vehicles (docs/vehicle-ws3.md; null: the census, the seat-by-id
    // offer and the ride addendum's category refuse with reason=unavailable).
    // Read-only vehicle unit and realized body read (partner_vehicle::ReadLive).
    void (*vehicle_read)(void* context, std::uint16_t id, partner_vehicle::Read& out) noexcept = nullptr;
    // The seat-by-id write (partner_player::WriteSeatTarget), only on the
    // player-update thread with the seat dwords armed: nullptr on success.
    const char* (*seat_target)(void* context, const partner_player::SeatTargetRequest& request,
                               partner_player::SeatTargetResult& out) noexcept = nullptr;
    // The unknown remote slot/equip debounce (partner_input::HoldIdentity in
    // partner_net's receive path): the equip of this tick's support verdict
    // (Mission::support_equip, 0 = none), published every driver tick. May be null.
    void (*publish_support_identity)(void* context, std::uint16_t equip) noexcept = nullptr;
    // Input frame v7 (WS4 v2, the sender's C4 place word): whether the peer was
    // seen sending v7 (partner_net::PeerSendsInputV7). Null: v7 is never sent.
    bool (*peer_input_v7)(void* context) noexcept = nullptr;
    // --- F1 vehicles (docs/native-puppet.md "Vehicle boarding"; any null: partner_ride_board
    // logs reason=unavailable once and nothing is installed, published or reported).
    // Lazy install of the two board call-site redirects (partner_board::Install, after the
    // code gate; nullptr ok, else a static reason), their per-tick live check, the policy word
    // the wrappers read (partner_board::Pack; 0 = off) and their counters.
    const char* (*board_install)(void* context) noexcept = nullptr;
    bool (*board_live)(void* context) noexcept = nullptr;
    void (*board_publish)(void* context, std::uint64_t policy) noexcept = nullptr;
    void (*board_stats)(void* context, partner_board::Stats& out) noexcept = nullptr;
    // The local passenger report (partner_board::ReadLocalPassenger: act 0x6E, P+0x120 /
    // Q+0xE0, rider[2|3] == local); reads only. False: unread.
    bool (*local_passenger)(void* context, const partner_player::State& state, partner_board::LocalPassenger& out) noexcept = nullptr;
    // F3 traversal icon (docs/native-puppet.md "Traversal icon"; null: no
    // census line): the read-only census of block 0's and block 1's action
    // plugin lists (partner_icon::ReadCensus), once per mission with the key on.
    void (*icon_census)(void* context, const partner_player::InputTargets& targets, partner_icon::Census& out) noexcept = nullptr;
    // --- Gestures (docs/native-puppet.md "Gestures"; any null: the sender
    // sends nothing and a received gesture refuses with reason=unavailable).
    // Both ids' upper act byte Q+0x20 (partner_player::ReadUpper).
    bool (*read_upper)(void* context, const partner_player::State& state, std::uint8_t (&out)[2]) noexcept = nullptr;
    // Lazy install of the body-1 update hook (partner_gesture::Install): nullptr ok, else a static reason.
    const char* (*gesture_install)(void* context) noexcept = nullptr;
    // partner_gesture::Step: publishes the command, drains the receipts.
    void (*gesture_step)(void* context, const partner_gesture::Command& command, partner_gesture::Status& out) noexcept = nullptr;
    // --- F1 passenger camera (docs/native-puppet.md "Passenger camera"; passenger mode only;
    // any null: one partner_ride=pass_camera; state=install; result=refused; reason=unavailable
    // and nothing is connected). Lazy install (partner_pass_camera::Install: the pinned blocks,
    // after the code gate; no patch), the connect / clear of the vehicle's ignore-collision
    // handle on the local camera (partner_pass_camera::Connect: returns the plan's reason, `out`
    // read after the write), the write counters and the local camera's connect fields
    // (partner_pass_camera::ReadCamera; reads only).
    const char* (*pass_camera_install)(void* context) noexcept = nullptr;
    const char* (*pass_camera_connect)(void* context, const partner_player::State& state, std::uint16_t vehicle, bool on,
                                       partner_pass_camera::Camera& out) noexcept = nullptr;
    void (*pass_camera_stats)(void* context, partner_pass_camera::Stats& out) noexcept = nullptr;
    bool (*pass_camera_read)(void* context, const partner_player::State& state, partner_pass_camera::Camera& out) noexcept = nullptr;
    // 0.13.6 online ride: clears the stale DM request bits of `mask` a seat
    // episode left (partner_player::ClearDmBits: bits 4/8/0x10, request byte 0
    // == 1, Stop/GoNext only without the viewer's own DM); returns the bits
    // cleared. May be null (nothing is cleared).
    std::uint8_t (*dm_clear_stale)(void* context, std::uint8_t mask) noexcept = nullptr;
    // M6 review R3 (partner_ride_front / partner_ride_cab): rider[seat] of this copy's vehicle `id` (u16;
    // 0xFFFF empty), read only (partner_vehicle::ReadLive and the record's rider words). False: unread
    // (the seat addendum then reports seat 1 taken). May be null.
    bool (*seat_rider)(void* context, std::uint16_t id, std::uint8_t seat, std::uint16_t& out) noexcept = nullptr;
    // M6 review R4: the seat-1 remap skip (partner_board::SetRemapSkip), from this tick only. May be null
    // (seat 1 is then never offered).
    bool (*board_remap)(void* context, bool skip) noexcept = nullptr;
    // Design 8.27: the passenger camera's lab trace (partner_pass_camera::ReadView; reads only) and the lab lever's
    // mode override (partner_pass_camera::Override: -1 none). Either may be null (no trace; no override).
    bool (*pass_camera_view)(void* context, const partner_player::State& state, partner_pass_camera::View& out) noexcept = nullptr;
    int (*pass_camera_override)(void* context) noexcept = nullptr;
    // The shot guard's write of the local camera's look pitch (partner_pass_camera::WritePitch; a fault counts in the
    // stats and latches the mission) and the current module's vtable rva (ModeVtable: the guard writes only under the aim
    // module, design 8.28; the trace's mvt). May be null (no guard; mvt=0).
    bool (*pass_camera_pitch)(void* context, const partner_player::State& state, float pitch) noexcept = nullptr;
    std::uintptr_t (*pass_camera_mvt)(void* context, const partner_player::State& state) noexcept = nullptr;
    // The shot guard's rotation write (partner_pass_camera::WriteRotation). May be null (the pitch only).
    bool (*pass_camera_rotation)(void* context, const partner_player::State& state, const float (&rot)[4]) noexcept = nullptr;
    // M7 seated shots, S1 (partner_seat_shot.hpp; docs/native-puppet.md "Seat shot (S1)"): publish / withdraw the local
    // seated passenger for the fire filter's RequestBullet hook (partner_fire_filter::PublishSeatShot / WithdrawSeatShot),
    // its counters and last plan (ReadSeatShot), the lab lever's override (-1 none). Any null: nothing is published.
    void (*seat_shot_publish)(void* context, const partner_seat_shot::Pub& pub) noexcept = nullptr;
    void (*seat_shot_withdraw)(void* context) noexcept = nullptr;
    bool (*seat_shot_stats)(void* context, partner_fire_filter::SeatShotStats& out) noexcept = nullptr;
    int (*seat_shot_override)(void* context) noexcept = nullptr;
};
// M7 seated shots: the per-mission cap of the partner_seat_shot lines.
inline constexpr unsigned kSeatShotLogCap = 300;
// A native sender whose peer never sent v5 sends v4 frames plus one v5 probe
// this often (an older build drops only the probe; the edge sets repeat).
inline constexpr std::uint64_t kInputV5ProbeMs = 2000;
// Native weapons cadences and caps.
inline constexpr std::uint64_t kEquipReadMs = 1000;    // loadout read / manifest send at most this often
inline constexpr unsigned kWeaponEventCap = 64;        // fire gate transitions, manifests sent, apply verdicts
// The seated vehicle rider (docs/anim-round1-seat.md): the play is re-issued
// after kSeatConfirmMs unconfirmed (at most kSeatPlays per episode); a stop is
// re-sent after kSeatStopMs and given up (patches reverted anyway) after
// kSeatExitMs (kSeatNextMs for the GetOff advance); the body farther than
// kSeatFarMm from the remote's streamed seated pose for kSeatFarMs falls back
// to hide; get-in only within kSeatGetInMs of the vehicle kind; the 1 Hz
// partner_ride_seat=status line and the event lines have their own caps.
inline constexpr std::uint64_t kSeatConfirmMs = 2000, kSeatStopMs = 1000, kSeatExitMs = 3000, kSeatNextMs = 4000;
inline constexpr std::uint64_t kSeatFarMs = 1000, kSeatGetInMs = 2500, kSeatStatusMs = 1000, kSeatReleaseMs = 1500;
inline constexpr std::uint32_t kSeatFarMm = 3000;
inline constexpr unsigned kSeatPlays = 3, kSeatStatusCap = 600;
// 0.13.6 online ride (the 2026-10-22 session): the engine's realize policy
// 0x121B5E0 tests body 1's SLOT (*(S+0x20))[1] x/z against the loaded area
// (0x14121B8B7 `shl rdi,4` / 0x14121B8BE `add rdi,[r9+0x20]`, 0x14121B8C2 /
// 0x14121B8CC the z/x loads, 0x14121B92C the area call; R9 = S), never the
// body; the seated DM moves only the
// body, so the slot stayed at the boarding spot and body 1 unrealized about
// 340 m into a ride. While seated the slot follows the body, checked at most
// every kSeatSlotFollowMs and written when it drifted more than
// kSeatSlotFollowMm (so it stays well within 100 m of the vehicle).
inline constexpr std::uint64_t kSeatSlotFollowMs = 1000;
inline constexpr std::uint32_t kSeatSlotFollowMm = 25000;
// The visible fallback (SeatPhase::fallback): the by-id gate is re-checked this
// often; once the vehicle exists here the normal release / arm / play take over.
inline constexpr std::uint64_t kSeatFallbackRetryMs = 2000;
// A temporary refusal (pending, dm_read, release, host_dm) is not latched: the
// same (vehicle, seat) is retried until it was refused this many times in a row
// without a confirmed seat, then it hides as in 0.13.5 until the remote's ride
// changes (a refusal that lasts the whole ride must not blink body 1 every 3-4 s).
inline constexpr unsigned kSeatTransientTries = 3;
// WS3 ride census cadence and cap (partner_ride_census=1).
inline constexpr std::uint64_t kRideCensusMs = 1000;
inline constexpr unsigned kRideCensusCap = 400;
// F1 vehicles (partner_ride_board): a board counter line at most once a second
// per site and kind (the pre-query runs every frame near the vehicle). Two caps
// of kBoardLogCap per mission: the counter lines (Board::count_cap) and every
// other board line (Board::cap), so the counters never crowd out a latch line.
inline constexpr std::uint64_t kBoardLogMs = 1000;
inline constexpr unsigned kBoardLogCap = 200;
// Design 8.27: the passenger camera's aim drop / restore lines per mission (the lab trace has its own cap).
inline constexpr unsigned kPassCameraAimCap = 120;
// The seat flow's phases (Mission::seat.phase). fallback (0.13.6): the remote
// rides a seat this copy cannot play because the vehicle is missing or
// unrealized here (Mother Base by id: absent, local_read, unrealized; free roam:
// absent only, SeatFallbackHere): body 1 stays
// realized and visible, Warped to the remote's streamed seated pose every tick,
// instead of hidden. Never for a vehicle that exists here (a pending request or
// the viewer's DM is retried, not Warped), never into the viewer's vehicle.
enum class SeatPhase : std::uint8_t { idle, release, arm, play, seated, exit, settle, fallback };
const char* SeatPhaseName(SeatPhase phase) noexcept;

// Times passed in by the caller: QPC milliseconds (cadence, poses, net) and
// GetTickCount64 (the probe's t_ms, comparable with the other log lines).
struct Clock {
    std::uint64_t now_ms;
    std::uint64_t tick_ms;
};

// Process-level settings: lab.ini at install, then partner.trigger.
struct Settings {
    std::uint32_t stage;
    PartnerSource source;
    PartnerApply apply;
    PartnerAnim anim;
    PartnerAset aset;
    PartnerReset reset;
    PartnerLook look;
    bool probe_ps;
    // Round 3.
    PartnerInput input;
    PartnerInputCam input_cam;
    PartnerInputHold input_hold;
    PartnerInputYaw input_yaw;
    std::uint8_t input_words[3];
    std::uint32_t input_mask;
    bool input_rstick, input_triggers;
    std::uint16_t stick_deg;
    std::uint8_t stick_pct;
    std::uint32_t correct_mm;
    std::uint32_t stale_ms;
    bool isend;
    bool pad_probe;
    bool look_gate, look_recover;
    PartnerDm dm;
    // dm=map: the [stance][move] clip table and the speed bands.
    PartnerDmTable dm_map;
    PartnerDmSpeeds dm_speed;
    // holdlog=1 trigger (effective only with lab partner_input_holdlog=1).
    bool holdlog;
    // Round 4 (docs/partner-anim-appearance.md "Round 4").
    PartnerRecon recon;
    std::int32_t align_ms;
    std::uint32_t nudge_mmps;
    PartnerSlot slot;
    PartnerRunaway runaway;
    PartnerWrap wrap;
    // Native puppet (docs/native-puppet.md).
    PartnerDrive drive;
    PartnerDefer defer;      // effective: off unless drive=native
    PartnerLocal local;
    PartnerNativeAim naim;
    bool ngun;
    PartnerNativeFire nfire;
    // Runtime gate, set by the native tick (never from lab.ini): the fire
    // filter is installed, live and armed for instance 1 and its gun is a gun
    // class. EffectiveSettings passes FIRE only with nfire=visual and this.
    bool fire_live;
    bool support_live; // current support gate (type 12; WS4 v1: C4 / arm lanes), independent of an earlier gun class
    std::uint32_t steer_deg, trim_pct;
    PartnerNativeRates nrates;
    // The facing governor mode (partner_recon FaceMode while native) and the
    // partner's shot noise (filter: FIRE only while the noise filter is live).
    PartnerNativeFace nface;
    PartnerNativeNoise nnoise;
    // The aim-target mirror (lab.ini; the debug command "aimpt" overrides it).
    PartnerNativeAimPt naimpt;
    // The partner's own reload and weapon switch (docs/native-puppet.md
    // "Weapons"): partner_native_reload and partner_native_slot (nreload=,
    // nslot= triggers; play runs nslot=call as write).
    PartnerNativeReload nreload;
    PartnerNativeSlot nslot;
    // What body 1 carries (lab.ini partner_native_equip; the observer runs the writers).
    PartnerNativeEquip nequip;
    PartnerNativeRide nride;  // qualified visibility policy; off until live acceptance
    PartnerRideSeatEnter seat_enter;  // nride=seat: direct loop or get-in chain
    PartnerRideSeatExit seat_exit;    // nride=seat: stop or get-off advance
    // WS3 vehicles (docs/vehicle-ws3.md): the ride census lines (lab only,
    // default off), the seat-by-id path (B1) and RIDE_TILT (A1) (both default
    // on, lab and play, capability-gated by the peer's ride addendum).
    bool ride_census, seat_id, ride_tilt;
    // F1 vehicles (lab.ini partner_ride_board; docs/native-puppet.md "Vehicle boarding"):
    // the board block / passenger hook and the local passenger report. Off by default; play forces off.
    PartnerRideBoard ride_board;
    // M6 seat 1 (lab.ini partner_ride_front / partner_ride_cab; docs/vehicles-companions-design.md 6.6):
    // with passenger, the light vehicle's front seat / the truck cab seat. Off unless named, lab and play.
    bool ride_front = false, ride_cab = false;
    // The passenger camera's mode (lab.ini partner_pass_camera, design 8.27; the lab lever passcam overrides it on a
    // lab copy) and the lab trace (partner_pass_camera_trace, lab only).
    PartnerPassCamera pass_camera = PartnerPassCamera::aim;
    bool pass_camera_trace = false;
    // Weapons4: the stand-in follower and gate (lab.ini partner_native_standin).
    PartnerNativeStandin nstandin;
    // Anim round 1 weapons (docs/anim-round1-weapons.md; lab.ini
    // partner_native_unsupported, _dry_stop, _aim_intent, _reload_rate,
    // partner_fire_census).
    bool nunsupported, ndry;
    bool nsupport; // lab-only native thrown equipment (type 12), default off
    // WS4 v1 (lab.ini partner_native_placed / partner_native_arm; both need
    // nsupport): placed C4 (type 13, 391..395, slot 3) and arm holds (type 14,
    // only 516 for now, slot 6, HOLD only: FIRE stays stripped).
    bool nplaced, narm;
    // WS4 v2 (lab.ini partner_native_place_mirror; needs nplaced): the remote's
    // C4 place / detonate mirrored into body 1's record (partner_place_mirror).
    bool nplace_mirror;
    PartnerNativeAimIntent naim_intent;
    PartnerNativeReloadRate nreload_rate;
    bool fire_census;
    // Set only in the effective copy: the native settings apply this tick.
    bool native_active;
    // lab.ini mode=play (Configure): the trigger file enables the native drive
    // there only inside the profile lab.ini admitted (nprofile != off); the
    // raw keys stay refused (kPlayRefusedNativeDrive).
    bool play_mode;
    // Online play (docs/native-puppet.md "Online"): the native profile lab.ini
    // expanded, the arbiter's network policy, the stall hold (stall_ms while
    // native), the native hold classifier and the sender's edge repeats.
    PartnerNativeProfile nprofile;
    PartnerNativeNet nnet;
    PartnerNativeStall nstall;
    std::uint32_t stall_ms;
    PartnerNativeHold nhold;
    std::uint32_t edge_repeats;
    // Round-1 actions track: the DM overlay lever and the act census lines.
    PartnerNativeOverlay noverlay;
    bool act_log;
    bool stance_fast;  // the native stance window (partner_native_stance_fast)
    bool traverse;     // traversal replay (partner_native_traverse)
    bool cbox;         // F2: the native cardboard box on body 1 (partner_native_cbox)
    bool ngesture;     // gestures on body 1 and their sender (partner_native_gesture)
    bool seat_gun;     // M7: the remote passenger's raised gun on the seated body 1, this copy's seated v6 direction (partner_seat_gun)
    // M7 seated shots, S1 (lab.ini partner_seat_shot; partner_seat_shot.hpp): the local seated passenger's bullets start
    // past the ridden vehicle. Key off: the lab copy still plans (census lines, nothing moved); play publishes nothing.
    bool seat_shot;
    PartnerNativeMount nmount;  // horse / D-Walker rider hide by act (partner_native_mount, docs/mounts.md)
    bool ui_filter;    // G1: body 1's HUD hints swallowed in its drain job (partner_native_ui_filter)
    // G5: the other ride hides unrealize body 1, and the net-quality rebase after
    // a ride logs partner_ride=net_rebased (partner_native_unrealize_all, "Ride hides").
    bool unrealize_all;
};

// A capped change-only log family (one per experiment head).
struct LogCap {
    unsigned lines;
    bool capped;
};

// Per-mission state, reset at runtime_enter.
struct Mission {
    bool cfg_ready, stopped;
    std::uint32_t mission_code;
    // Free-roam safety latch: nearby fresh packets place the slot before
    // realization; distant/unloaded partners never receive Warp fallbacks.
    bool range_hidden, range_needs_place;
    LogCap range_cap;
    partner_ride_wire::Visibility visibility;
    LogCap ride_cap;       // partner_ride=rx (on a state/revision change only)
    LogCap ride_vis_cap;   // partner_ride=hide|foot_stable|seat_show
    // partner_native_mount=hide: the act-rule episode (partner_mount=hide|end lines, capped).
    partner_mount_policy::Episode mount;
    LogCap mount_cap;
    // partner_native_unrealize_all (G5): the other ride hides' episode (heli, vehicle, horse kind 3;
    // partner_ride=unrealize|unrealize_end lines) and the partner_ride=net_rebased line of the
    // link-quality rebase after a ride (net_quality_rebase, below), on their own cap.
    partner_mount_policy::RideEpisode ride_ep;
    LogCap unrealize_cap;
    // The seated vehicle rider (partner_native_ride=seat; docs/anim-round1-seat.md).
    struct Seat {
        SeatPhase phase;
        bool manual;                 // the debug seat_dm lever owns the DM (Lua issued it)
        bool armed;                  // partner_ride_seat dwords armed by us
        bool confirmed;              // body 1 plays a seated state of `seat`
        bool loop_confirmed;         // body 1 transitioned from GetOn into the seated loop
        bool getoff_confirmed;       // body 1 transitioned from the loop into GetOff
        bool next_sent;              // the exit advanced to GetOff (RequestToGoNextDirectMotion)
        bool lua_refused;            // Lua refused the current request (LuaLog event=ride_seat_refused)
        std::uint16_t vehicle;       // the episode's vehicle GameObject id and native seat
        std::uint8_t seat;
        std::uint8_t entry;          // partner::seat_entry bits of the played chain
        std::uint8_t loop_entry;     // the loop's index in that chain
        unsigned plays, stops;
        std::uint64_t phase_ms, issue_ms, confirm_ms, far_since_ms, status_ms;
        const char* exit_reason;
        bool latch_exit;             // the exit blocks this (vehicle, seat) until the ride changes
        // A (vehicle, seat) refused or ended this ride: seat mode is off for it
        // (it hides) until the remote's ride state leaves it.
        bool blocked;
        std::uint16_t block_vehicle;
        std::uint8_t block_seat;
        const char* block_reason;
        // When the received ride became this vehicle/seat (the get-in window).
        std::uint64_t vehicle_since_ms;
        std::uint16_t vehicle_rev;
        bool vehicle_live;
        bool seat_live;
        // Seat by id (WS3 B1): the id path was offered for this episode
        // (seat_entry::by_id), its gate reason, and whether this play's DM
        // record was targeted (target_done; target_wrote: by us, not a name).
        bool byid, target_done, target_wrote;
        const char* byid_reason;
        std::uint8_t remote_category, local_category;
        // 0.13.6 online ride. fallback_want: the start's by-id reason that shows
        // body 1 without a seat (absent, local_read, unrealized) once the release ran;
        // fallback_reason: why the fallback phase holds; exit_quiet: the exit
        // left the fallback (nothing of ours plays on body 1: no stop);
        // body_gone: the exit found body 1 unrealized (no stop either);
        // exit_link: the exit was caused by the link (lease, ride_stale: the
        // settle keeps that gap in the link-quality window, no rebase);
        // req_bits: the global DM request bits this episode sent (Stop 8,
        // GoNext 0x10), cleared at settle when the DM could not consume them.
        const char* fallback_want;
        const char* fallback_reason;
        std::uint64_t fallback_ms, fallback_check_ms;
        unsigned fallback_warps;
        bool exit_quiet, body_gone, exit_link;
        std::uint8_t req_bits;
        // The slot follow while seated (kSeatSlotFollowMs / kSeatSlotFollowMm).
        std::uint64_t slot_check_ms;
        unsigned slot_follows;
        // The arm's own-stale-request check (at most every kSeatStopMs).
        std::uint64_t stale_check_ms;
        // Temporary refusals of (transient_vehicle, transient_seat) in a row
        // without a confirmed seat (kSeatTransientTries; kept across a settle).
        unsigned transient_refusals;
        std::uint16_t transient_vehicle;
        std::uint8_t transient_seat;
    } seat;
    bool ride_seated;      // the seat flow owns body 1 this tick (vehicle_attached gates)
    // WS3 ride census (partner_ride_census=1): one line per ridden vehicle every
    // kRideCensusMs (the local one and the remote driver's copy), own cap.
    LogCap census_cap;
    std::uint64_t census_ms;
    LogCap seat_cap;       // partner_ride_seat event lines
    // F1 vehicles (partner_ride_board): the board counters last logged, the
    // last published policy, a phase-1 back seat awaiting the local passenger
    // report, and the mission-scoped uncertain latch (passenger then publishes
    // block; partner_ride=board_uncertain once).
    struct Board {
        partner_board::Stats seen;   // the process counters at this mission's first board tick
        bool seen_have;
        std::uint32_t logged_block[2], logged_steer[2], steer_count, faults_seen;
        std::uint64_t block_log_ms[2], steer_log_ms[2];
        std::uint64_t policy;
        bool policy_logged;
        partner_board::Policy logged;
        bool steer_pending;
        std::uint8_t steer_seat;
        std::uint16_t steer_vehicle;
        std::uint64_t steer_ms;
        const char* uncertain;
        bool live_refused;
        LogCap cap;         // every other board line: policy, local_passenger, seated, refused, uncertain
        LogCap count_cap;   // the periodic board_block / board_passenger counter lines
    } board;
    // F1 passenger camera (passenger mode): the per-boarding episode (connect /
    // refused / release lines, under board.cap), the connect's counters at this
    // mission's first tick and the mission latch (fault: nothing is connected
    // again this mission).
    struct PassCamera {
        partner_pass_camera::Episode episode;
        partner_pass_camera::Stats seen;
        bool seen_have;
        const char* uncertain;
        bool uncertain_logged;
        // Design 8.27: the aim drop (mode aim), its lines (aim_cap) and the lab trace (trace_cap, its last line).
        partner_pass_camera::AimDrop aim;
        partner_pass_camera::Guard guard;
        LogCap aim_cap, trace_cap;
        std::uint64_t trace_ms, trace_aim_ms;
        std::uint8_t mode_logged;  // the last effective mode logged + 1 (0: none yet)
    } pass_camera;
    // M7 seated shots, S1: the configured line, the published (vehicle, seat, apply) and the last refusal logged, the
    // last plan seq logged and the hook's counters at this mission's start (lines under cap).
    struct SeatShot {
        bool configured_logged, published, apply, stats_have;
        std::uint16_t vehicle;
        std::uint8_t seat;
        const char* refusal_logged;
        std::uint32_t last_seq;
        partner_fire_filter::SeatShotStats seen;
        LogCap cap;
    } seat_shot;
    // The last partner_ride_seat=unavailable reason and its (vehicle, seat):
    // why seat mode hides a live vehicle ride (mission, hooks, blocked, ...).
    const char* seat_unavailable;
    std::uint16_t seat_unavailable_vehicle;
    std::uint8_t seat_unavailable_seat;
    unsigned seat_status_lines;
    bool seat_status_capped;
    // The local PlayerStatus census of the receipt's st= (logged on change).
    bool status_have;
    std::uint16_t status_bits;
    std::uint16_t active_item;  // the local tick's item= id (0 unknown)
    // Receiver cardboard box: the remote manifest's box state last logged. No reply
    // carries op::cbox (no Lua apply); with partner_native_cbox the observer's
    // partner_cbox_equip puts the box on body 1 natively from cbox_want / cbox_id /
    // cbox_seq (F2, docs/native-puppet.md "Cardboard box").
    bool cbox_pending, cbox_want, cbox_applied;
    std::uint16_t cbox_id, cbox_id_applied;
    std::uint8_t cbox_left;
    std::uint32_t cbox_seq;      // +1 per logged change (the native apply's edge, one attempt each)
    std::uint64_t cbox_off_ms;   // when the remote's box last went off (the recon hold's grace)
    bool cbox_hold;              // the recon hold for a boxed body 1 (partner_cbox_policy::ReconHold), last tick
    LogCap cbox_cap;             // partner_cbox=hold lines
    LogCap status_cap;
    const char* visibility_logged;
    std::uint64_t visibility_place_try_ms;
    std::uint64_t start_ms, ticks;
    bool pose_applied, have_cmd;
    partner::Pose cmd;
    bool bits_set;
    std::uint64_t bits_ms;
    unsigned bits_lost_logs, write_refusal_logs;
    bool realized, parts_loaded, parts_timeout_logged;
    std::uint64_t realized_ms;
    unsigned transition_logs;
    bool transitions_capped;
    unsigned s0_missing_ticks, s0_forced_logs;
    bool policy_relaxed, policy_refused_logged;
    std::uint8_t policy_previous;
    unsigned lua_logs;
    bool lua_logs_capped;
    const char* refusal;
    unsigned cfg_logs;
    bool cfg_capped;
    unsigned tick_rejects;
    std::uint32_t flags;
    unsigned flags_logs;
    bool flags_capped;
    unsigned allactive_resends;
    std::uint64_t allactive_resend_ms;
    bool mirror_requested, mirror_seen, mirror_refused_logged;
    std::uint64_t mirror_ms, mirror_seen_ms, mirror_off_ms;
    unsigned mirror_off_sends;
    unsigned probe_lines;
    bool probe_logged;
    std::uint64_t probe_ms;
    std::uint32_t probe_key[kProbeKeys];
    std::uint64_t warp_check_ms;
    std::uint64_t rate_ms, rate_ticks;
    bool units_logged;
    std::uint64_t units_ms;
    std::int32_t yaw_abs_max;
    bool anim_logged;
    std::uint64_t anim_ms;
    partner_player::Anim anim;
    std::uint8_t anim_psw[2];
    // Native calls: disabled for the rest of the mission after a fault.
    bool native_disabled;
    // Movement drivers.
    const char* xform_state;   // nullptr = applied, else the refusal last logged
    bool xform_logged;
    LogCap xform_cap, hybrid_cap;
    bool hybrid_placed;
    bool body_ok;
    float body_pos[4], body_quat[4];
    std::uint64_t xform_check_ms;
    pose::Status last_status;
    // Animation application.
    partner::StanceDebounce stance;
    bool stance_watch;
    std::uint8_t stance_expected_psw;
    std::uint64_t stance_set_ms;
    LogCap anim_cap;                 // partner_anim=source, partner_aset=q
    LogCap stance_cap;               // partner_stance=set, partner_stance=req (ok)
    LogCap stance_refused_cap;       // partner_stance=refused, partner_stance=req (refused)
    LogCap stance_revert_cap;        // partner_stance=reverted
    const char* stance_refused_state;  // last refusal reason logged (change-only); nullptr after a success
    std::uint8_t stance_last;        // last stance the game accepted (survives debounce resets)
    std::uint32_t stance_sets, stance_reqs, stance_refusals, stance_reverts;
    bool stance_summary_dirty;
    std::uint64_t stance_summary_ms;
    const char* anim_write_state;
    bool anim_write_logged;
    std::int8_t anim_source;  // -1 not logged yet, else the last logged validity (0/1)
    // Census: once per realize when a native feature needs it, or census=1.
    bool census_logged;
    std::uint64_t ps_probe_ms;
    unsigned ps_probe_lines;
    // --- Round 3: look fix.
    std::uint64_t stall_since_ms;
    std::uint32_t stall_key;       // desired (type|parts<<8) the stall was seen against
    bool stall_logged;             // partner_look=stall emitted for stall_key
    bool recovering;               // a recovery SET_PARTS is in flight
    bool copy_withheld_logged;
    std::uint32_t look_deferred_key;  // last logged (desired key<<16 | state<<8 | q48bit)
    bool look_deferred_logged;
    // --- Round 3: input lever.
    bool input_armed;
    std::uint32_t input_gen;
    char input_refused_text[48];   // last refusal logged (change-only)
    unsigned input_refused_logs;
    bool input_refused_capped;
    partner_input::SenderEdges sender;  // the input frame v2 edge sets (isend)
    bool input_faulted;
    partner_input::Arm last_arm;   // last published (for the gen bump via SameArm)
    bool have_arm;
    std::uint32_t tap_bits;
    std::uint32_t press_bits;
    std::uint64_t press_until_ms;
    std::uint64_t p218_ms;         // when the diagnostic P+0x218 window was armed
    bool p218_active;
    std::uint64_t input_stats_ms;
    unsigned input_stats_lines;
    bool input_stats_capped;
    std::uint64_t body_ms;
    unsigned body_lines;
    bool body_capped;
    // partner_body speed: horizontal body samples tagged with the epoch of our
    // last hard write (placement, snap, pin, yaw, Warp fallback, disarm).
    struct BodySample { std::uint64_t t_ms; std::int32_t x_mm, z_mm; std::uint32_t epoch; };
    BodySample body_ring[kBodyRing];
    unsigned body_head, body_count;
    std::uint32_t body_epoch;
    // Weapons4: the body lease of the generalized writer: bumped at each
    // realized transition (realize and unrealize), never by a warp or a
    // placement (those bump the motion epoch body_epoch).
    std::uint32_t realize_gen;
    std::uint64_t pad_probe_ms;
    unsigned pad_probe_lines;
    bool pad_probe_capped;
    unsigned hold_logs;
    bool hold_capped;
    partner_input::HoldRecord hold_last[8];
    unsigned hold_last_count;
    bool input_stale;              // last logged stale/live state
    bool input_stale_logged;
    // --- Round 4: reconciliation (apply=input).
    partner_recon::State recon;
    // Traversal replay (partner_traverse): the machine, its hold (fed to the
    // next StepRecon) and its log family.
    partner_traverse::State traverse;
    bool traverse_hold;
    LogCap traverse_cap;
    // F3 traversal icon: this tick's request for the Arm (kind, the process-
    // unique id Driver::icon_seq gave the machine's episode icon_episode), the
    // request log state (icon_req: a request is open, from its first raised
    // tick until the request phase ends; stats at its start; logged once),
    // the 2 s body-act window after a grant and the once-per-mission census.
    partner_traverse::Icon icon_kind;
    std::uint32_t icon_seq, icon_episode;
    bool icon_req, icon_logged;
    partner_traverse::Icon icon_req_kind;
    std::uint32_t icon_req_seq;
    partner_icon::Stats icon_base;
    std::uint64_t icon_grant_ms;
    std::uint16_t icon_act_last;
    bool icon_census_done;
    LogCap icon_cap;
    // G1 partner HUD hints (partner_ui_filter): seeded from the process totals
    // on the mission's first tick with the hooks installed; the totals at the
    // last swallowed line and its time, the census keys already logged, the
    // patch-lost latch and the log family.
    bool ui_filter_seeded;
    partner_ui_filter::Stats ui_filter_base;
    std::uint64_t ui_filter_ms;
    std::uint32_t ui_filter_keys_logged;
    bool ui_filter_lost_logged;
    LogCap ui_filter_cap;
    bool recon_active;             // apply=input with input != off this mission (recon state in use)
    bool recon_last_ok;            // the previous recon action was executed
    partner_recon::Out recon_out;  // the last step (logs, status)
    bool recon_have_out;
    std::uint32_t recon_delay_ms, recon_pose_age_ms;
    bool recon_in_valid;
    bool hold_stripped; // live remote HOLD refused by the effective native mask/gate
    bool dash_fix;
    bool carry_hold; // instance 1 is in the stock carried state (coop_carry): recon holds
    bool vehicle_job; // current verified demand keeps native Arm until original exit
    partner_native::DashState dash;
    std::uint32_t recon_in_age_ms;
    std::int32_t recon_in_r;       // x1000
    double recon_target[3];
    double recon_target_yaw;
    bool recon_remote_ok;            // the aligned sample's anim fields (StepRecon)
    std::uint16_t recon_remote_act;
    bool recon_target_ok;
    bool recon_target_live;          // the aligned sample is live (StepRecon)
    std::uint64_t recon_line_ms;
    unsigned recon_lines;
    bool recon_lines_capped;
    // Event families (place, placed, snap, runaway, frozen, stance, yaw, stop, diverging, refused,
    // stance_fix, input_pin, lag): kReconCapLimits each (rotating; 0.13.6, was 64 per mission).
    log_budget::Rotating recon_cap[13];
    std::uint64_t log_now_ms;        // this tick's clock for the rotating caps
    // 0.13.6 body safety (partner_body_safety, docs/native-puppet.md "Body safety").
    partner_body_safety::StuckState body_stuck;
    partner_body_safety::DropState body_drop;
    partner_body_safety::PutDownState body_putdown;
    bool reshow_capable;             // the re-show flow runs this tick (free-roam net or the ride guard)
    const char* body_reshow;         // a re-show requested by TickInput (executed in the range block next tick)
    std::uint64_t body_reshow_ms;    // when it was requested (dropped after 1 s)
    // The re-show hold: body 1 kept hidden (deferred replies; clear_all_active while
    // Lua's all-active mode is on, as the mount hide does) until the engine reports
    // it unrealized or kReshowUnrealizeMs passed; then the slot is placed at the target.
    bool reshow_wait;
    std::uint64_t reshow_since_ms;
    const char* reshow_reason;
    log_budget::Rotating body_safety_cap;
    const char* recon_refused;     // last refusal logged (change-only)
    std::uint32_t recon_snaps_total;
    std::uint32_t recon_fixes;     // corrective stance inputs this mission (StanceFix)
    // Wrap census line (deltas against the last line).
    bool wrap_install_tried;
    partner_input_hook::WrapStats wrap_base;
    std::uint64_t wrap_line_ms;
    unsigned wrap_lines;
    unsigned input_census_lines;            // partner_input_census lines this mission (cap kRefusedCap)
    std::uint32_t us_max_acc;               // max window time since the last 1 Hz line
    // Window statistics are hook totals: the line logs deltas against these.
    partner_input::WindowStats arm_base;    // at the last arm transition
    partner_input::WindowStats stats_base;  // at the last 1 Hz line
    std::uint64_t stats_base_ms;
    bool input_fallback;                    // apply=input fell back to Warp (logged on the transition)
    // --- Round 3: direct motion.
    bool dm_patched;
    bool dm_census_logged;
    bool dm_census_loaded;          // the census was logged with instance 1 loaded (once per realize)
    const char* dm_refused_state;   // last change-only dm=on refusal (input_active)
    bool dm_conflict_logged;
    std::uint64_t dm_probe_ms;
    unsigned dm_probe_lines;
    bool dm_probe_capped;
    partner_player::DmState dm_last;
    bool dm_last_ok;
    std::uint64_t dm_play_ms;      // when our last play request went out (unconsumed watch)
    bool dm_play_pending;
    std::uint8_t dm_pending_gani;
    // dm=map (partner_dmmap): the state machine, the speed of the commanded
    // pose, one cap per event family, the switch/playing and summary caps, and
    // the previous reply's look/Warp ops (loss attribution).
    partner_dmmap::State dmmap;
    partner_dmmap::SpeedRing dmmap_speed;
    partner_dmmap::TurnState dmmap_turn;
    partner_source_turns::State dmmap_source_turns;
    pose::RemoteFrame dmmap_source_frame;
    // Receiver facing (partner_facing): the applied yaw for xform/hybrid on the
    // net source, and per-tick runtime evidence for the worker capture.
    partner_facing::State facing;
    partner_facing::Ring facing_ring;
    bool facing_active;
    float facing_target_yaw, facing_applied_yaw;  // radians, valid while facing_active
    bool facing_source_body, facing_source_logged;  // sender: body-transform yaw published (else vars)
    LogCap facing_cap;
    PartnerSource dmmap_speed_source;
    bool dmmap_table_logged;
    LogCap dmmap_cap[7];  // deferred, lost, stop_timeout, unconsumed, adopted, abandoned, bad_gani
    unsigned dmmap_switch_lines;
    bool dmmap_switch_capped;
    std::uint64_t dmmap_summary_ms;
    unsigned dmmap_summary_lines;
    bool dmmap_summary_capped;
    bool dmmap_prev_look, dmmap_prev_warp;
    // A failed patch is not retried every tick (cleared by any dm= trigger and
    // at mission start); a failed revert is not retried by the per-tick off path.
    bool dm_patch_failed, dm_revert_failed;
    bool anim_dm_logged;   // partner_anim=deferred; reason=dm_map (once per suppression)
    // Appearance.
    bool look_applied_any;
    std::uint16_t look_applied_remote_rev;
    std::uint32_t look_applied_forced_rev;
    partner::Look look_applied;
    std::uint64_t look_applied_ms;
    bool look_verify, look_cycle_seen;
    std::uint64_t look_match_ms;
    const char* look_refusal;
    bool look_waiting_logged;
    std::uint64_t all_active_ms;
    LogCap look_cap;
    bool looks_logged;
    std::uint64_t looks_ms;
    partner_player::Looks looks;
    // partner_lost=hide: since when the net sample is lost, and whether the
    // realize bits were cleared for it.
    std::uint64_t lost_since_ms;
    bool lost_hidden;
    // --- Native puppet (docs/native-puppet.md).
    partner_native::State native;
    partner_native::Out native_out;   // the last arbiter step (native_out.active: effective settings)
    bool native_defer_armed;          // we armed the deferral (set_defer true)
    bool native_identity;             // an identity is published
    bool native_roots;                // census wrap roots are published (PublishWrapRoots)
    partner_player::NativeTargets native_targets;
    bool native_targets_ok;
    partner_defer::Stats defer_last;  // the previous drain (deltas for the arbiter)
    bool defer_last_ok;
    partner_defer::Stats defer_line;  // at the last 1 Hz partner_defer line
    std::uint64_t defer_line_ms;
    unsigned defer_lines;
    bool defer_lines_capped;
    partner_input_hook::WrapStats wrap_native_last;
    partner_input_hook::NativeStats hook_native_last;
    bool hook_native_last_ok;
    std::uint32_t native_drain_us_acc;   // max drain time since the last arbiter step
    LogCap native_cap;                   // transitions
    LogCap native_enter_cap;             // enter steps
    LogCap native_gun_cap;               // gun change lines
    LogCap native_equip_cap;             // manifest lines
    bool native_latched_logged;
    bool native_fault_logged;
    std::uint64_t native_line_ms;
    unsigned native_lines;
    bool native_lines_capped;
    unsigned native_drain_lines;         // 1 Hz partner_native_drain lines (own cap)
    bool native_drain_lines_capped;
    std::uint32_t ring_p1_last;          // p1_forced total at the previous ring entry
    unsigned native_gun_lines;
    std::uint64_t native_gun_line_ms;
    std::uint32_t native_gun_last;       // gun3c0 | active << 31 at the last change line
    partner_native::SteerState steer;
    double steer_rad, mag_scale;
    std::int32_t steer_mdeg;
    std::uint64_t runaway_since_ms;      // recon runaway pin (report.runaway_active) held since (0 = not)
    // Parity windows (10 s) and aim match.
    std::uint64_t match_window_ms;
    std::uint32_t match_ticks, stance_hits, move_hits, stance_pct, move_pct;
    bool aim_last;
    std::uint64_t aim_since_ms;
    bool aim_match;
    // The 1 s rates window.
    std::uint64_t rate_ms_native;
    partner_defer::Stats rate_defer;
    partner_input_hook::WrapStats rate_wrap;
    partner_input_hook::NativeStats rate_hook;
    bool rate_ok;
    partner_native::Telemetry tele;
    // Equipment manifest (channel 2): store and counters.
    partner_equip_wire::Store equip;
    std::uint32_t equip_logged_rev;
    // Native rates (docs/native-puppet.md "Rates"): both records (read at most
    // every 100 ms), the remote's wire rates (input frame v4) and the log key.
    std::uint64_t rates_read_ms;
    bool rates_id0_ok, rates_id1_ok;
    partner_player::RateRead rates_id0, rates_id1;
    bool rates_remote_ok;
    std::uint16_t rates_remote[3];
    std::uint64_t rates_remote_ms;
    LogCap native_rates_cap;
    std::uint64_t rates_logged_key;
    bool rates_logged;
    // --- Native weapons (docs/native-puppet.md "Weapons").
    const char* fire_refusal;          // why FIRE is masked this tick (static; nullptr: live)
    const char* fire_logged;           // the verdict of the last partner_fire= transition line
    bool fire_logged_once;
    bool weapon_ok, weapon_known;      // instance 1 showed a gun class this mission (latched; see TickWeapons)
    std::uintptr_t gun1_prim, gun1_rec;  // the mirror's gun record (last readable one; 0 = none)
    // The local instance's gun record, read once per native tick (TickNative,
    // before TickInput's v4 sender uses it in the same tick).
    partner_player::Gun1 gun0;
    bool gun0_ok;
    partner_fire_filter::Stats fire_rate;  // at the previous ~1 s rate window (started at fire_rate_ms)
    std::uint64_t fire_rate_ms;
    bool fire_rate_ok;
    std::uint32_t fire_suppressed_s;
    unsigned fire_lines;
    bool fire_census_logged;
    LogCap weapon_cap;
    // The receiver's apply verdict (the sender lives on the Driver).
    std::uint16_t equip_apply_rev;
    std::uint32_t equip_apply_loaded;
    char equip_apply[12];
    LogCap equip_apply_cap;
    // --- Native polish (docs/native-puppet.md "Facing", "Weapons").
    // The published Arm injects a neutral pad (placement, verify, retry).
    bool input_neutral;
    // The weapon gate (F4): the remote's gun equip differs from body 1's
    // (FIRE and RELOAD stripped in every native window, the mirror frozen).
    bool weapon_mismatch, weapon_logged_once;
    bool weapon_logged;                 // the verdict of the last gate line
    std::uint32_t weapon_strip;
    std::int32_t remote_gun_equip;      // kNull without a v4 Equip
    // remote_gun_equip as a stock id (a customized alias of the received
    // manifest maps to its slot's base id; kNull without a v4 Equip).
    std::int32_t remote_gun_base;
    // The look source hook (D3): the last install verdict (static; nullptr ok).
    const char* look_src_refusal;
    bool look_src_tried;
    // The 1 Hz partner_native_face line and the last placement's yaw verdict.
    unsigned face_lines;
    // Input frame v5 negotiation: the last v5 probe, the probes sent and
    // whether the peer's v5 was logged this mission.
    std::uint64_t v5_probe_ms;
    std::uint32_t v5_probes;
    bool v5_peer_logged;
    std::uint64_t v6_probe_ms;
    std::uint32_t v6_probes;
    bool v6_peer_logged;
    std::uint64_t v7_probe_ms;
    std::uint32_t v7_probes;
    bool v7_peer_logged;
    partner_aim::DirCheck dir_check;
    partner_aim::DirReason dir_reason;
    bool dir_reason_logged;
    bool dir_riding;                 // partner_aim::RideGate: the local player rode at the last aim sample
    std::int32_t place_yaw_err_mdeg;
    // --- Online play (docs/native-puppet.md "Online").
    // The receiver's link quality (re-entry gate) and the online log lines.
    partner_native_net::Quality net_quality;
    // 0.13.6: a seat episode settled or a ride hide ended (the native arbiter
    // did not observe the link meanwhile): the next TickNative restarts the
    // quality window (partner_native_net::Rebase) instead of counting the
    // whole ride as one arrival gap.
    bool net_quality_rebase;
    std::uint64_t net_line_ms;
    unsigned net_lines;
    std::uint64_t net_line_key;
    bool net_quiet_logged;
    LogCap net_cap;
    // Body 1 never showed a gun class this mission (the gun profile strips
    // HOLD, RELOAD and FIRE: no empty-handed aiming), logged on change.
    bool weapon_absent, weapon_absent_logged, weapon_logged_absent;
    // Aim parity: the last aim telemetry read (every tick while the native
    // drive is active and either side aims, else every kAimReadMs) and the 1 Hz partner_native_aim
    // line (own cap, logged while its key changes).
    std::uint64_t aim_read_ms;
    bool aim_read_ok;
    partner_player::AimRead aim_read;
    unsigned aim_lines;
    std::uint64_t aim_line_ms, aim_line_key;
    // --- The partner's own reload and weapon switch (docs/native-puppet.md "Weapons").
    // The overlay tap bumped this tick (a second tap in the same tick ORs its bits).
    std::uint64_t tap_bump_ms;
    bool tap_bumped;
    // The receiver's event machine (partner_reload) and its last output (the
    // Arm's reload_hold/reload_reconcile/reload_release); the sender's v5 counters.
    partner_reload::Machine reload;
    partner_reload::Out reload_out;
    partner_reload::SenderState sender_gun;
    LogCap reload_cap;
    // The sender hold (partner_input::HoldSender, run 0c9269ce): a failed local
    // gun/slot read republishes the last good one for kSenderHoldMs; its lines
    // (partner_input=sender_hold, own cap) carry the read's refusal.
    partner_input::SenderHold sender_hold;
    LogCap sender_hold_cap;
    // The remote's latest live v5 gun group (remote_v5: GunFlag::Valid) and slot.
    bool remote_v5;
    std::uint8_t remote_gun_flags, remote_gun_mode, remote_reload_seq, remote_switch_seq, remote_slot;
    // The local player's slots (the v5 sender's inv_slot) and body 1's (the
    // follower's plan and telemetry), read once per native tick.
    partner_player::Slots slots0, slots1;
    bool slots0_ok, slots1_ok;
    // The plan for body 1's slots (partner_slots) from the received manifest
    // (types per manifest revision) and body 1's handles; the last logged key.
    partner_slots::Plan slot_plan;
    bool slot_plan_ok;
    std::uint16_t slot_types_rev;
    std::uint8_t slot_types[3];
    bool slot_types_ok[3];
    std::uint64_t slot_plan_key;
    LogCap slot_cap;
    // The follower's last logged verdict (hook totals: writes, refusals).
    std::uint32_t slot_logged_writes, slot_logged_refused, slot_logged_last;
    // The generalized weapon gate (W5): the verdict, body 1's in-hand type
    // (kNull unread) and whether it is mid-switch.
    partner_slots::Gate weapon_gate;
    std::int32_t body_type;
    bool body_switching;
    // Native support through an input stall (partner_support_policy StallHold,
    // critic #4): the newest v5 frame is stale but inside the stall hold and
    // names slot 3 with support_equip, the equip of the last support verdict
    // (0 = none); the observer keeps the writer context's slot/equip on that
    // verdict while its own newest v5 frame is within stall_ms
    // (partner_support_policy::ContextHold; never on support_stall alone).
    bool support_stall;
    std::uint16_t support_equip;
    // The last logged type verdict (partner_weapon=projectile|type_ok lines).
    bool weapon_type_logged, weapon_type_logged_once;
    // Weapons4: body 1 holds the plan's stand-in for the remote's current slot
    // (Gate::standin, or a projectile stand-in), the slot it maps from/to and
    // the last logged stand-in key; the time spent with a stand-in and with
    // the gun absent (ms, this mission; bounded owner-tick accounting); body 1's
    // gun mode (0xFF unread: the writer's body_busy).
    bool weapon_standin, weapon_standin_logged;
    std::uint8_t standin_remote_slot, standin_body_slot;
    std::uint32_t standin_key;
    std::uint64_t weapon_ms_at;
    std::uint32_t standin_ms, absent_ms;
    std::uint8_t body_gun_mode;
    // --- Anim round 1 weapons (docs/anim-round1-weapons.md).
    // The remote's v4 magazine of the latest live sample (auto-fire stop).
    bool remote_mag_ok;
    std::uint16_t remote_mag, remote_mag_cap;
    // A1: the unsupported-equipment verdict (partner_slots JudgeUnsupported),
    // the remote's in-hand equip type (kNull unread; read once per equip) and
    // the last logged key (own cap).
    partner_slots::Unsupported unsupported;
    std::int32_t remote_equip_type;
    std::int32_t remote_type_equip;     // the equip remote_equip_type belongs to (kNull: none)
    std::uint64_t unsupported_key;
    bool unsupported_logged;
    LogCap unsupported_cap;
    // State-driven aim intent: the driver's gate (partner_slots AimGate), the
    // remote's act is an aim act (live pose), the last logged key and the hook
    // totals at the last line (own cap, change lines at most every kAimIntentLineMs).
    partner_slots::AimRefusal aim_refusal;
    bool aim_gate_ok, aim_act;
    std::uint64_t aim_intent_key, aim_intent_ms;
    bool aim_intent_logged;
    unsigned aim_intent_lines;
    // Auto-fire stop (partner_slots DryStep) and its lines.
    partner_slots::DryState dry;
    bool dry_strip;
    LogCap dry_cap;
    // Reload rate adaptation (partner_reload RateLearn) and its lines.
    partner_reload::RateAdapt rate_adapt;
    LogCap rate_cap;
    // partner_weapon_ctx: one line per change of the weapon context, at most
    // every kWeaponCtxMs (later changes coalesce into the next line; no cap).
    std::uint64_t ctx_key, ctx_ms, ctx_seen_key;
    bool ctx_have;
    unsigned ctx_changes;
    // The mirror's rec+0x3E8 rewrites for a slot change / a base above the
    // capacity (hook totals at the last partner_reload=shadow line; own cap).
    std::uint32_t shadow_slot_logged, shadow_cap_logged;
    bool shadow_logged;
    LogCap shadow_cap;
    // The anim round 1 weapon settings line (once per mission with the gun stage).
    bool anim_settings_logged;
    // partner_native_gun change lines: own cap, at most every kNativeGunChangeMs
    // (later changes counted in the next line's changes=).
    std::uint64_t native_gun_change_ms;
    unsigned native_gun_change_lines, native_gun_changes;
    // The fire/projectile census (partner_fire_census=1): the ring cursor, the
    // per-second budget and mission totals of its lines.
    std::uint32_t census_cursor;
    bool census_cursor_ok;
    std::uint64_t census_second_ms;
    unsigned census_second_lines, census_lines;
    std::uint32_t census_lost, census_skipped;
    bool census_install_logged;
    // --- Round-1 actions track (docs/anim-round1-actions.md).
    // The act census (log only) and the remote's dropped pad presses.
    partner_act_log::ActState act_log;
    partner_act_log::PadState pad_census;
    // The DM overlay lever: its machine, the last step (the next tick's native
    // suspend reason) and its line cap.
    partner_dm_overlay::State overlay;
    partner_dm_overlay::Out overlay_out;
    LogCap overlay_cap;
    // --- Gestures (partner_gesture, docs/native-puppet.md "Gestures").
    // This tick's upper act bytes (both ids), the newest received edge waiting
    // for the gate (driver clock), the request in flight (its seq stays in the
    // hook's command until a receipt names it or kRequestMs passes), the
    // stance/moving it was chosen for, the hook's last status, the mission
    // latch and the line family.
    bool gesture_upper_ok;
    std::uint8_t gesture_upper[2];
    bool gesture_pending;
    partner_gesture_policy::Packet gesture_rx_packet;
    std::uint64_t gesture_rx_ms;
    partner_gesture::Request gesture_request;
    std::uint8_t gesture_request_stance;
    bool gesture_request_moving;
    bool gesture_active, gesture_uncertain, gesture_patch_lost_logged, gesture_configured_logged;
    std::uint32_t gesture_rx, gesture_requests, gesture_refused;
    // The hook's last step (for a request no body-1 update answered): body 1's
    // identity verdict (nullptr resolved) and the process count of body-1 updates.
    const char* gesture_hook_resolve;
    std::uint32_t gesture_hook_body_calls;
    LogCap gesture_cap;
    // --- M7 seat gun (partner_seat_gun.hpp, docs/native-puppet.md "Seat gun"): the hold decision, the seq of the
    // hold's gun_start request (the hook's hold_seq), the seq the hook last reported active, the refusal last logged,
    // the hold's start and the rates line clock, and the line family.
    partner_seat_gun::State seat_gun;
    std::uint32_t seat_gun_seq, gesture_active_seq;
    const char* seat_gun_refusal_logged;
    bool seat_gun_configured_logged;
    std::uint64_t seat_gun_since_ms, seat_gun_rates_ms;
    LogCap seat_gun_cap;
};

struct Driver {
    Settings settings;
    bool parts_explicit_pending;
    bool mirror_pending;
    bool census_pending;
    // Round 3 one-shots (applied on the next tick with the armed/time context).
    bool tap_pending;
    std::uint32_t tap_bits_pending;
    bool press_pending;
    std::uint32_t press_bits_pending, press_ms_pending;
    bool dmplay_pending;
    std::uint8_t dmplay_gani;
    bool dmstop_pending;
    // dm=map: Rearm the mapper on the next map tick (dm=map, dmmap=, dmspeed= triggers).
    bool dmmap_rearm;
    bool p218_pending, p218_on;
    bool holdlog_changed;
    // census=1 also latches these, consumed by the input/DM census (TickInput
    // logs the input census even with input=off; failing verdicts included).
    bool input_census_pending, dm_census_pending;
    // tap= sequence: process-lifetime (the hook keeps its last seen value
    // across gens and missions, so a value is never reused).
    std::uint32_t tap_seq;
    // F3: the icon request id, process-lifetime for the same reason (the hook
    // grants at most once per id; 0 is never used).
    std::uint32_t icon_seq;
    // rtap=1: one test reload tap through the event machine on the next native tick.
    bool rtap_pending;
    // Round-1 actions: the DM overlay debug one-shot (overlay=, oloop=, oms=),
    // applied on the next tick.
    bool overlay_pending;
    partner_dm_overlay::Request overlay_request;
    // Round 4: a placement requested by a trigger (place=1, or a change of
    // apply/input/source), consumed by the next reconciliation step.
    partner_recon::PlaceReason recon_place_request;
    // Bumped by every look= trigger and by parts=explicit outside look=copy:
    // a new value re-applies the desired look once.
    std::uint32_t look_forced_rev;
    // Native rates: the mirror's epoch (bumped at every MissionStart, never 0
    // once a mission ran), published with the Arm.
    std::uint32_t rates_epoch;
    // Online play: the capability exchange outlives missions (it is link
    // session scoped): this copy's sender pacing, the remote's last message
    // and the revision last logged; a shot or ammo leak in play strips FIRE and
    // RELOAD for the rest of the mission (leak_strip: the pad bits, 0 before;
    // cleared at MissionStart, two-PC test 2026-10-23 item 3).
    partner_caps_wire::Sender caps_tx;
    partner_caps_wire::Store caps_rx;
    std::uint16_t caps_logged_rev;
    std::uint32_t caps_logged_epoch;
    std::uint8_t caps_logged_drive, caps_logged_reason;
    // The receiver link session of the last logged partner_caps=rx line (a new
    // link session logs the remote's view again).
    std::uint32_t caps_logged_link;
    std::uint32_t leak_strip;
    // Refused channel-2 messages (0.13.6 online-net-epoch): one partner_equip=refused
    // / partner_caps=refused line per change of (receiver link session, reason),
    // under a process-lifetime budget; the manifest counters are process-lifetime
    // too (the mission store's own counters restart every MissionStart).
    bool equip_refused_logged, caps_refused_logged;
    std::uint32_t equip_refused_link, caps_refused_link;
    std::uint8_t equip_refused_result, caps_refused_result;
    LogCap equip_rx_cap, caps_rx_cap;
    std::uint32_t equip_rx_received, equip_rx_invalid, equip_rx_stale, equip_rx_duplicates;
    // The generalized preload and writer (docs/native-puppet.md "Generalized
    // writer"), process-lifetime: the last accepted manifest of any mission
    // and its link session (the RECEIVER's own: the preload at the next
    // allocation, before that mission's own manifest arrives), and what the
    // observer (the natural allocation and mission owner) last reported;
    // telemetry copies it.
    partner_equip_wire::Manifest equip_cache;
    bool equip_cache_have;
    std::uint32_t equip_cache_epoch;
    // The loadout sender (docs/native-puppet.md "Loadout mirroring"),
    // process-lifetime: this copy's manifest (a new revision on change), whether
    // it still has to go out (a change or a new link session), the session it
    // went out in, the last loadout read (at most every kEquipReadMs, from the
    // driver tick in partner missions and the observer's ~100 ms path in every
    // other mission: the title and the ACC too) and its base-id scan.
    partner_equip_wire::Manifest equip_tx;
    bool equip_tx_have, equip_tx_dirty, equip_tables_ok;
    std::uint32_t equip_tx_epoch;
    std::uint64_t equip_read_ms;
    partner_player::Loadout equip_loadout;
    // The sender's own line budget (partner_equip_base / partner_equip=sent),
    // reset at MissionStart: the title or ACC after a long mission still logs
    // its sends (the mission's weapon_cap may be spent by then).
    LogCap equip_tx_cap;
    partner_ride_wire::Store ride_rx;
    // The last logged received ride state (partner_ride=rx on a change only).
    partner_ride_wire::Packet ride_rx_logged;
    bool ride_rx_logged_have;
    std::uint32_t ride_rx_logged_epoch;
    partner_ride_wire::Packet ride_tx;
    bool ride_tx_have, ride_tx_dirty;
    std::uint32_t ride_tx_epoch;
    std::uint64_t ride_tx_ms;
    bool ride_invalid_logged;
    // WS3 vehicles (docs/vehicle-ws3.md): the ride wire v3 addendum (category
    // and the seat-id / tilt features), received and sent (link-session scoped),
    // and the vehicle the local player rides (the tick's veh=, 0xFFFF none).
    partner_ride_wire::ExtStore ride_ext;
    partner_ride_wire::Ext ride_ext_tx;
    bool ride_ext_tx_have;
    std::uint32_t ride_ext_tx_epoch;
    std::uint64_t ride_ext_tx_ms;
    bool ride_ext_logged;
    // M6 review R3: the v4 seat addendum (the peer draws a seat-1 passenger; the seat 1 of the vehicle it
    // drives is taken on its copy), received and sent (link-session scoped), and this copy's rider[1] read
    // of the vehicle its player drives (every kSeatRiderMs while it drives).
    partner_ride_wire::SeatExtStore ride_seat_ext;
    partner_ride_wire::SeatExt ride_seat_ext_tx;
    bool ride_seat_ext_tx_have;
    std::uint32_t ride_seat_ext_tx_epoch;
    std::uint64_t ride_seat_ext_tx_ms;
    std::uint16_t seat_rider_vehicle, seat_rider_value;
    bool seat_rider_read;
    std::uint64_t seat_rider_ms;
    // Seats on (docs/vehicles-companions-design.md 8.25, D16's residual): the seat this copy draws the peer's seat-1
    // ride in while its own D-Dog took seat 1 of the vehicle its player drives (partner_board::DrawSeat), latched per
    // ride, and the rider[1..3] read behind it (every kSeatRiderMs while that ride lasts).
    struct SeatDraw {
        std::uint16_t vehicle = 0xFFFF;  // the latched ride's vehicle (0xFFFF: the peer's own seat is drawn)
        std::uint8_t seat = 0xFF;        // the latched back seat
        std::uint16_t read_vehicle = 0xFFFF;
        std::uint64_t read_ms = 0;
        bool riders_ok = false;
        std::uint16_t rider[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
        bool logged = false;             // a seat_draw line is out for the latch (one release line follows)
    } seat_draw;
    std::uint16_t local_vehicle;
    // F1 vehicles: the local player as a stock back-seat passenger (the native
    // report; Lua says foot for act 0x6E), kept apart from local_vehicle (the
    // vehicle the viewer DRIVES: RIDE_TILT and the receiver's local_vehicle gate).
    partner_board::LocalPassenger local_passenger;
    // The board hook's lazy install (once per process) and its result.
    bool board_install_tried, board_installed;
    const char* board_refusal;
    // Review R4 (process scoped, like the remap dword): whether this tick has the seat-1 remap skipped,
    // a failed remap write (seat 1 is then never offered again), the seat-1 steer count seen and its time.
    bool board_remap_skip, board_remap_failed;
    std::uint32_t board_seat_one_seen;
    std::uint64_t board_seat_one_ms;
    // The passenger camera's lazy install (once per process, passenger mode) and its result.
    bool pass_camera_install_tried, pass_camera_installed;
    const char* pass_camera_refusal;
    equipment_stock_writer::Status equip_status;
    std::uint16_t preload_ids[3];
    std::uint8_t preload_count;
    char preload_verdict[24], preload_reason[32];
    std::uint32_t preload_mission, preload_new_hashes;
    // Item 2: the preload query had no remote manifest (preload_source: an
    // equipment_stock_probe::PreloadSource, none or predicted): the next new
    // remote manifest logs equip_preload=late_manifest once (which bases the
    // preload covered: guns this load, or after the next one).
    bool preload_late_watch;
    std::uint8_t preload_source;
    // Gestures (docs/native-puppet.md "Gestures"), process-lifetime: the
    // upper-act edge sender (link-session scoped) and its one pending message,
    // the receiver's store, the request serial (never reused), the hook's
    // mission epoch (bumped at every MissionStart/MissionEnd/Stop: the hook
    // drops a running gesture without a write) and the lazy install verdict.
    partner_gesture_policy::Sender gesture_tx;
    bool gesture_tx_pending;
    partner_gesture_policy::Packet gesture_tx_packet;
    std::uint8_t gesture_tx_upper;
    std::uint64_t gesture_tx_ms;
    partner_gesture_policy::Store gesture_rx;
    std::uint32_t gesture_seq, gesture_epoch;
    bool gesture_install_tried;
    const char* gesture_install_refusal;
    Mission mission;
};

// The effective settings' native_active (EffectiveSettings): the native drive is
// configured and the arbiter's last step is active. Driver::settings is the
// configured copy, whose native_active is always false, so code that reads
// d.settings (TickNative's stall hold, the observer's ContextHold) uses this.
inline bool NativeActive(const Driver& d) noexcept {
    return d.settings.drive == PartnerDrive::native && d.mission.native_out.active;
}

// WS3 vehicles: the vehicle the local player rides (0xFFFF none) and whether
// the peer's live ride addendum advertises RIDE_TILT (docs/vehicle-ws3.md).
std::uint16_t LocalVehicle(const Driver& driver) noexcept;
bool RideTiltPeer(const Driver& driver, std::uint32_t epoch, std::uint64_t now_ms) noexcept;
// G4 truck test levers (vehicle_door, lab only; docs/native-puppet.md "Truck test levers"): the
// peer's live ride packet (this link epoch, inside the lease) of kind vehicle with a vehicle-type
// id gives that id and the peer's seat (true); otherwise false with 0xFFFF / 0xFF. Read only.
bool RidePeerVehicle(const Driver& driver, std::uint32_t epoch, std::uint64_t now_ms, std::uint16_t& vehicle,
                     std::uint8_t& seat) noexcept;

// Read-only admission from the same effective settings as Tick. The caller
// must serialize with driver ticks; no caller-supplied active flag is accepted.
// Base dm=map remains the ordinary fallback after the native drive leaves.
bool NativeSleepPresentationReady(const Driver& driver) noexcept;

// Install: settings from lab.ini, everything else cleared.
void Configure(Driver& driver, const LabConfig& lab) noexcept;
// runtime_enter: fresh mission state, pending one-shots dropped, and a
// partner.trigger left over from earlier is deleted unapplied
// ("partner_trigger=discarded; reason=stale").
void MissionStart(Driver& driver, const Hooks& hooks, std::uint32_t mission_code = 30050) noexcept;
// termination_returned: no more ticks; the input window is disarmed, the DM
// patch reverted, and pending one-shots dropped.
void MissionEnd(Driver& driver, const Hooks& hooks) noexcept;
// CFG receipt. `precheck` is the observer's refusal reason (not_runtime_thread,
// mission, code_gate) or nullptr; then ReadState decides. Logs
// partner_cfg=ready|refused. True means the reply is "return true".
bool Cfg(Driver& driver, const Hooks& hooks, const char* precheck, std::uint64_t now_ms) noexcept;
// LOG receipt: "partner_lua=<payload>", at most kLuaLogCap per mission.
void LuaLog(Driver& driver, const Hooks& hooks, const char* source, std::size_t size) noexcept;
// SEAT receipt (the debug seat_dm lever, docs/anim-round1-seat.md): arm=true
// arms the seat dwords for (seat, vehicle) on a realized, DM-idle body 1 and
// hands body 1 to a manual seat (Lua then plays its own DM); arm=false ends it
// (Lua stops the DM; the patches revert once DM End ran). True when accepted.
bool SeatManual(Driver& driver, const Hooks& hooks, bool arm, std::uint8_t seat, std::uint16_t vehicle, std::uint64_t now_ms,
                bool terminating) noexcept;
// STOP receipt (allowed while terminating): clears our bits when
// teardown=clear, restores the policy byte, drops pending one-shots, stops
// ticking and logs partner_stop.
void Stop(Driver& driver, const Hooks& hooks, PartnerTeardown teardown, bool terminating) noexcept;
// partner.trigger contents, already parsed.
void ApplyTrigger(Driver& driver, const Hooks& hooks, const partner::Trigger& trigger, bool terminating) noexcept;
// Reads, deletes, parses and applies partner.trigger (if present).
void PollTrigger(Driver& driver, const Hooks& hooks, bool terminating) noexcept;
// The equipment manifest link (docs/native-puppet.md "Loadout mirroring"):
// drains reliable channel 2 (the received manifest goes to the mission store
// and the session cache) and runs this copy's loadout sender (read at most
// every kEquipReadMs; sent on change and on a new link session; nothing
// without a link session or without partner_drive=native /
// partner_input_send=1). Never reads partner state: the observer calls it on
// its ~100 ms path in any mission (the title and the ACC too), the tick in
// partner missions. now_ms: the driver clock.
void TickEquipLink(Driver& driver, const Hooks& hooks, std::uint64_t now_ms) noexcept;
// Only the drain (the preload query at the natural allocation owner: the
// first mission after connecting plans from a manifest received just before).
void DrainEquipLink(Driver& driver, const Hooks& hooks) noexcept;
// The manifest the generalized preload and writer plan from (pure): the
// mission store's when it was received in (or seeded for) the receiver's
// current link session `link_session` (any, when 0: no session yet), else the
// session cache of that same link session. Never another link session's, and
// never compared with the sender's own session number (manifest.epoch).
bool RemoteManifest(const Driver& driver, std::uint32_t link_session, partner_equip_wire::Manifest& out) noexcept;
// This copy's own last sent manifest (pure; false before the first loadout
// read): the allocation preload's prediction of a peer that restarted on this
// copy's save (equipment_stock_probe::ChoosePreloadSource, two-PC test
// 2026-10-23 item 2).
bool OwnManifest(const Driver& driver, partner_equip_wire::Manifest& out) noexcept;
// The per-frame tick (2.3 item 9 outline). False leaves the receipt literal
// (Lua then does nothing); true fills `reply`.
bool Tick(Driver& driver, const Hooks& hooks, const LabConfig& lab, const char* source, std::size_t size,
          const Clock& clock, bool terminating, partner::Reply& reply) noexcept;

// Quiet tick pairing (2.2): a replaced tick receipt logs neither its entry nor
// its return line. Armed by every partner receipt (only a replaced tick sets
// it), consumed by the very next return observation on the same thread.
struct Quiet {
    bool pending;
};
inline bool LogsEntry(partner::Receipt kind) noexcept { return kind != partner::Receipt::tick; }
inline void Arm(Quiet& quiet, partner::Receipt kind, bool replaced) noexcept {
    quiet.pending = kind == partner::Receipt::tick && replaced;
}
inline bool ConsumeReturn(Quiet& quiet) noexcept {
    const bool pending = quiet.pending;
    quiet.pending = false;
    return pending;
}

} // namespace mgscoop::partner_driver
