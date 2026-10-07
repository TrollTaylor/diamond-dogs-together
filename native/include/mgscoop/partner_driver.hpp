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
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_dmmap.hpp"
#include "mgscoop/partner_source_turns.hpp"
#include "mgscoop/partner_input.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_recon.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/player_sync.hpp"
#include "mgscoop/pose_interp.hpp"

namespace mgscoop::partner_driver {

inline constexpr unsigned kProbeKeys = 20;
inline constexpr unsigned kProbeLineCap = 400;
inline constexpr unsigned kLuaLogCap = 64;
inline constexpr unsigned kCappedLogs = 8;
inline constexpr unsigned kTransitionLogCap = 64;
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
// Drained input-hook statistics (mirror of partner_input_hook::Stats).
struct InputStats {
    partner_input::WindowStats window;
    partner_input::HoldRecord holds[8];
    unsigned hold_count;
    partner_input_hook::WrapStats wrap;
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
};

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
    bool recon_active;             // apply=input with input != off this mission (recon state in use)
    bool recon_last_ok;            // the previous recon action was executed
    partner_recon::Out recon_out;  // the last step (logs, status)
    bool recon_have_out;
    std::uint32_t recon_delay_ms, recon_pose_age_ms;
    bool recon_in_valid;
    std::uint32_t recon_in_age_ms;
    std::int32_t recon_in_r;       // x1000
    double recon_target[3];
    double recon_target_yaw;
    bool recon_target_ok;
    std::uint64_t recon_line_ms;
    unsigned recon_lines;
    bool recon_lines_capped;
    LogCap recon_cap[12];          // event families (place, placed, snap, runaway, frozen, stance, yaw, stop, diverging, refused, stance_fix, input_pin)
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
    // Round 4: a placement requested by a trigger (place=1, or a change of
    // apply/input/source), consumed by the next reconciliation step.
    partner_recon::PlaceReason recon_place_request;
    // Bumped by every look= trigger and by parts=explicit outside look=copy:
    // a new value re-applies the desired look once.
    std::uint32_t look_forced_rev;
    Mission mission;
};

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
// STOP receipt (allowed while terminating): clears our bits when
// teardown=clear, restores the policy byte, drops pending one-shots, stops
// ticking and logs partner_stop.
void Stop(Driver& driver, const Hooks& hooks, PartnerTeardown teardown, bool terminating) noexcept;
// partner.trigger contents, already parsed.
void ApplyTrigger(Driver& driver, const Hooks& hooks, const partner::Trigger& trigger, bool terminating) noexcept;
// Reads, deletes, parses and applies partner.trigger (if present).
void PollTrigger(Driver& driver, const Hooks& hooks, bool terminating) noexcept;
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
