#pragma once
// Offline partner spike: the native state machine behind the partner receipts
// (docs/offline-partner-spike-plan.md 2.2, 2.3 item 9, 2.6 and 2.7).
//
// Decision logic only. Every effect goes through Hooks: game reads and the
// guarded writes (partner_player), the remote pose ring and transmit
// (partner_net), the partner.trigger file and the log. The script observer
// owns threading (runtime thread, receipt lock), the receipt plumbing and the
// real hooks; tests/partner_driver_tests.cpp drives the same code with fakes
// on deterministic timelines. Not thread safe: callers serialize.
//
// Log caps per mission: Lua notes 64; refused writes, lost bits, tick parse
// rejections, forced exists and all-active re-asserts 8; probe lines 400;
// partner_cfg refused/ready flips 64 (then "partner_cfg=capped; limit=64");
// realized/parts transitions 64 (then "partner_realized=capped; limit=64");
// partner_flags changes 64 (then "partner_flags=capped; limit=64").
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_receipt.hpp"
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

enum class TriggerRead : std::uint8_t { none, read, unreadable };

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
    void (*submit_local)(void* context, const partner::Tick& tick) noexcept;
    // partner.trigger: reads at most `capacity` bytes and deletes the file.
    TriggerRead (*take_trigger)(void* context, char* text, std::size_t capacity, std::size_t& length) noexcept;
    bool (*focused)(void* context) noexcept;
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
};

// Per-mission state, reset at runtime_enter.
struct Mission {
    bool cfg_ready, stopped;
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
};

struct Driver {
    Settings settings;
    bool parts_explicit_pending;
    bool mirror_pending;
    Mission mission;
};

// Install: settings from lab.ini, everything else cleared.
void Configure(Driver& driver, const LabConfig& lab) noexcept;
// runtime_enter: fresh mission state, pending one-shots dropped, and a
// partner.trigger left over from earlier is deleted unapplied
// ("partner_trigger=discarded; reason=stale").
void MissionStart(Driver& driver, const Hooks& hooks) noexcept;
// termination_returned: no more ticks; pending one-shots dropped.
void MissionEnd(Driver& driver) noexcept;
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
