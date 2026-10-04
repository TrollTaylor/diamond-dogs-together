// Original MGS Co-op offline partner spike: receipt state machine.
// docs/offline-partner-spike-plan.md 2.2, 2.3 item 9, 2.6, 2.7, and the
// animation / movement / appearance experiments (docs/partner-anim-appearance.md).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include "mgscoop/partner_driver.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_driver {
namespace {
namespace pp = partner_player;
namespace c = partner_player_contract;

void Emit(const Hooks& h, const char* line) noexcept {
    if (h.log) h.log(h.context, line);
}
void EmitFormat(const Hooks& h, const char* format, ...) noexcept {
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) Emit(h, line);
}
// One line of a capped family: the first kTransitionLogCap lines are logged,
// then "<head>=capped; limit=64" (with "; kind=<kind>" when given) once.
void EmitCappedV(LogCap& cap, const Hooks& h, const char* head, const char* kind, const char* format, va_list arguments) noexcept {
    if (cap.lines >= kTransitionLogCap) {
        if (!cap.capped) {
            cap.capped = true;
            if (kind) EmitFormat(h, "%s=capped; limit=64; kind=%s\r\n", head, kind);
            else EmitFormat(h, "%s=capped; limit=64\r\n", head);
        }
        return;
    }
    char line[512]{};
    if (SUCCEEDED(StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments))) {
        ++cap.lines;
        Emit(h, line);
    }
}
void EmitCapped(LogCap& cap, const Hooks& h, const char* head, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    EmitCappedV(cap, h, head, nullptr, format, arguments);
    va_end(arguments);
}
// The partner_stance families (set, refused, reverted) share one head.
void EmitStance(LogCap& cap, const Hooks& h, const char* kind, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    EmitCappedV(cap, h, "partner_stance", kind, format, arguments);
    va_end(arguments);
}
std::uint32_t Clamp32(std::uint64_t value) noexcept {
    return value > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(value);
}

void LogWriteRefusal(Mission& m, const Hooks& h, const char* what, const char* reason) noexcept {
    if (m.write_refusal_logs >= kCappedLogs) return;
    ++m.write_refusal_logs;
    EmitFormat(h, "partner_write=refused; op=%s; reason=%s\r\n", what, reason ? reason : "unknown");
}
// Realized/parts transitions share one cap; the first ones are always logged.
void LogTransition(Mission& m, const Hooks& h, const char* line) noexcept {
    if (m.transition_logs < kTransitionLogCap) {
        ++m.transition_logs;
        Emit(h, line);
    } else if (!m.transitions_capped) {
        m.transitions_capped = true;
        Emit(h, "partner_realized=capped; limit=64\r\n");
    }
}
// partner_cfg=refused / partner_cfg=ready flips inside the tick.
void LogCfgFlip(Mission& m, const Hooks& h, const char* line) noexcept {
    if (m.cfg_logs < kTransitionLogCap) {
        ++m.cfg_logs;
        Emit(h, line);
    } else if (!m.cfg_capped) {
        m.cfg_capped = true;
        Emit(h, "partner_cfg=capped; limit=64\r\n");
    }
}
// A native call raised inside SEH: no more native calls this mission.
void NoteFault(Mission& m, const Hooks& h, const char* function) noexcept {
    if (m.native_disabled) return;
    m.native_disabled = true;
    EmitFormat(h, "partner_call=fault; fn=%s; native_calls=disabled\r\n", function);
}
bool IsFault(const char* reason) noexcept { return reason && std::strcmp(reason, "fault") == 0; }

bool ClearBits(Driver& d, const Hooks& h, const char* reason, bool terminating) noexcept {
    Mission& m = d.mission;
    if (!m.bits_set) return false;
    std::uint32_t q194 = 0, q198 = 0;
    if (const char* refused = h.clear_bits(h.context, terminating, q194, q198)) {
        LogWriteRefusal(m, h, "clear_bits", refused);
        return false;
    }
    m.bits_set = false;
    EmitFormat(h, "partner_bits=cleared; reason=%s\r\n", reason);
    return true;
}
void RestorePolicy(Driver& d, const Hooks& h, bool terminating) noexcept {
    Mission& m = d.mission;
    if (!m.policy_relaxed) return;
    std::uint8_t previous = 0;
    if (const char* refused = h.set_policy(h.context, false, m.policy_previous, terminating, previous)) {
        LogWriteRefusal(m, h, "policy_restore", refused);
        return;
    }
    m.policy_relaxed = false;
    Emit(h, "partner_policy=restored\r\n");
}

void Probe(Driver& d, const Hooks& h, const pp::State& st, const Clock& clock) noexcept {
    Mission& m = d.mission;
    const std::uint32_t key[kProbeKeys] = {st.ids[0], st.ids[1], st.pool[0], st.pool[1], st.parts[0], st.parts[1], st.rbits,
        st.s0, st.s8, st.q170, st.q180, st.q194, st.q198, st.glob, st.pb_guard, st.pb_flag[0], st.pb_flag[1],
        st.pb_state[0], st.pb_state[1], d.settings.stage};
    const std::uint64_t now = clock.now_ms;
    const bool changed = !m.probe_logged || std::memcmp(key, m.probe_key, sizeof(key)) != 0;
    const bool due = !m.probe_logged || (changed && now - m.probe_ms >= 1000) || now - m.probe_ms >= 10000;
    if (!due || m.probe_lines >= kProbeLineCap) return;
    char line[pp::probe_capacity]{};
    if (!pp::FormatProbe(st, clock.tick_ms, d.settings.stage, line, sizeof(line))) return;
    Emit(h, line);
    ++m.probe_lines;
    m.probe_logged = true;
    m.probe_ms = now;
    std::memcpy(m.probe_key, key, sizeof(key));
}

// partner_anim_probe=1: per-slot animation fields, change-only, at most 10 Hz.
void AnimProbe(Mission& m, const Hooks& h, const pp::State& st, bool anim_ok, const pp::Anim& anim, std::uint64_t now) noexcept {
    if (!anim_ok || (m.anim_logged && now - m.anim_ms < 100)) return;
    std::uint8_t psw[2] = {0, 0};
    if (h.census) {
        pp::Census census{};
        h.census(h.context, st, census);
        psw[0] = census.k[0].psw;
        psw[1] = census.k[1].psw;
    }
    const pp::Anim& last = m.anim;
    if (m.anim_logged && std::memcmp(anim.act, last.act, sizeof(anim.act)) == 0 && std::memcmp(anim.q28, last.q28, sizeof(anim.q28)) == 0 &&
        std::memcmp(anim.stance, last.stance, sizeof(anim.stance)) == 0 && std::memcmp(anim.speed, last.speed, sizeof(anim.speed)) == 0 &&
        std::memcmp(anim.qd8, last.qd8, sizeof(anim.qd8)) == 0 && std::memcmp(psw, m.anim_psw, sizeof(psw)) == 0) return;
    m.anim = anim;
    std::memcpy(m.anim_psw, psw, sizeof(psw));
    m.anim_logged = true;
    m.anim_ms = now;
    EmitFormat(h, "partner_anim; act=%04x,%04x; s=%u,%u; v=%u,%u; q28=%u,%u; qd8=%04x,%04x; psw=%02x,%02x; rec1=0x%08x\r\n",
               unsigned(anim.act[0]), unsigned(anim.act[1]), unsigned(anim.stance[0]), unsigned(anim.stance[1]),
               unsigned(anim.speed[0]), unsigned(anim.speed[1]), unsigned(anim.q28[0]), unsigned(anim.q28[1]), unsigned(anim.qd8[0]),
               unsigned(anim.qd8[1]), unsigned(psw[0]), unsigned(psw[1]), st.rec_ok ? st.rec1_status : 0u);
}

// Resends at most once per kResendMs, at most kCappedLogs times per mission.
bool ResendDue(unsigned sends, std::uint64_t last_ms, std::uint64_t now) noexcept {
    return sends < kCappedLogs && (sends == 0 || now - last_ms >= kResendMs);
}

// F3c mirror lifecycle. Returns true when MIRROR_OFF is due this tick.
bool MirrorOffDue(Driver& d, const Hooks& h, const pp::State& st, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    if (!m.mirror_requested) return false;
    const bool on = (st.flags & c::flag_mirror) != 0;
    if (on && !m.mirror_seen) {
        m.mirror_seen = true;
        m.mirror_seen_ms = now;
        EmitFormat(h, "partner_mirror=on; flags=0x%08x\r\n", st.flags);
    }
    if (!on && m.mirror_seen) {
        EmitFormat(h, "partner_mirror=off; ms_on=%u\r\n", Clamp32(now - m.mirror_seen_ms));
        m.mirror_requested = m.mirror_seen = false;
        return false;
    }
    const bool due = d.settings.stage < 1 || now - m.mirror_ms >= kMirrorMaxMs;
    if (!due) return false;
    if (!on) {
        // Never applied (Lua lacked the binding): nothing to undo.
        m.mirror_requested = false;
        return false;
    }
    if (!ResendDue(m.mirror_off_sends, m.mirror_off_ms, now)) return false;
    if (!st.mirror_object) {
        // Mirror off (0x9ccb40) dereferences the engine mirror object unchecked.
        if (!m.mirror_refused_logged) {
            m.mirror_refused_logged = true;
            Emit(h, "partner_mirror=refused; reason=engine_object\r\n");
        }
        return false;
    }
    ++m.mirror_off_sends;
    m.mirror_off_ms = now;
    return true;
}

void LogCensus(const Hooks& h, const pp::State& st) noexcept {
    if (!h.census || !h.format_census) {
        Emit(h, "partner_census=refused; reason=unavailable\r\n");
        return;
    }
    pp::Census census{};
    h.census(h.context, st, census);
    for (std::uint32_t k = 0; k < 2; ++k) {
        char line[pp::census_capacity]{};
        if (h.format_census(h.context, census, k, line, sizeof(line))) Emit(h, line);
    }
}

std::int32_t MetresToMm(float value) noexcept { return pp::ToMm(value); }

// The desired look of instance 1 this tick (none for look=copy without a
// pending parts=explicit). `remote_rev` and `forced_rev` form its key.
struct Desired {
    bool valid;
    partner::Look look;
    std::uint16_t remote_rev;
    std::uint32_t forced_rev;
    bool avatar_as_snake;
    const char* source;
};
Desired DesiredLook(Driver& d, const Hooks& h, bool looks_ok, const pp::Looks& looks, const pp::State& st) noexcept {
    Mission& m = d.mission;
    Desired out{};
    const Settings& set = d.settings;
    partner::Look raw{};
    const char* source = nullptr;
    std::uint16_t remote_rev = 0;
    if (set.look == PartnerLook::quiet) { raw = partner::quiet_look; source = "quiet"; }
    else if (set.look == PartnerLook::snake) { raw = partner::snake_look; source = "snake"; }
    else if (set.look == PartnerLook::net) {
        const RemoteLook remote = h.sample_look ? h.sample_look(h.context) : RemoteLook{};
        if (!remote.valid) return out;
        raw = remote.look;
        remote_rev = remote.revision;
        source = "net";
    } else {
        if (!d.parts_explicit_pending || !looks_ok) return out;
        raw = looks.look[st.local];
        source = "copy";
    }
    bool avatar = false;
    partner::Look look{};
    if (const char* refused = partner::NormalizeLook(raw, look, avatar)) {
        if (m.look_refusal != refused) {
            m.look_refusal = refused;
            EmitCapped(m.look_cap, h, "partner_look", "partner_look=refused; reason=%s; source=%s; type=%u; parts=%u; camo=%u; hand=%u; rev=%u\r\n",
                       refused, source, unsigned(raw.type), unsigned(raw.parts), unsigned(raw.camo), unsigned(raw.hand), unsigned(remote_rev));
        }
        return out;
    }
    m.look_refusal = nullptr;
    out.valid = true;
    out.look = look;
    out.remote_rev = remote_rev;
    out.forced_rev = d.look_forced_rev;
    out.avatar_as_snake = avatar;
    out.source = source;
    return out;
}
bool SameKey(const Mission& m, const Desired& desired) noexcept {
    return m.look_applied_any && m.look_applied_remote_rev == desired.remote_rev && m.look_applied_forced_rev == desired.forced_rev &&
           partner::SameLook(m.look_applied, desired.look);
}
bool LookMatches(const partner::Look& actual, const partner::Look& wanted) noexcept {
    return actual.type == wanted.type && actual.parts == wanted.parts && actual.camo == wanted.camo && actual.hand == wanted.hand &&
           actual.face == wanted.face;
}

namespace pin = mgscoop::partner_input;

// Round 3: a capped change-only input/body/pad log family.
void EmitInput(Mission& m, const Hooks& h, const char* format, ...) noexcept {
    if (m.input_stats_lines >= kInputStatsCap) {
        if (!m.input_stats_capped) { m.input_stats_capped = true; Emit(h, "partner_input=capped; limit=600\r\n"); }
        return;
    }
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) { ++m.input_stats_lines; Emit(h, line); }
}

const char* WordsText(const Settings& set, char (&out)[4]) noexcept {
    FormatPartnerWords(set.input_words, out);
    return out;
}

// Build the Arm from the resolved targets and the current settings.
pin::Arm BuildArm(const Driver& d, const Settings& set, const pp::InputTargets& t, const pp::State& st, const partner::Tick& tick,
                  std::uint64_t now, bool enabled) noexcept {
    const Mission& m = d.mission;
    pin::Arm a{};
    a.gen = m.input_gen;
    a.enabled = enabled;
    a.holdlog = false;  // set by the caller for I5
    a.p218 = m.p218_active;
    a.cam = set.input_cam;
    a.hold = set.input_hold;
    a.P = t.P;
    a.pp = t.pp;
    a.src = t.src;
    a.cam_object = t.cam;
    a.root0 = t.root0;
    a.root1 = t.root1;
    a.blk1 = t.blk1;
    a.rec1 = t.rec1;
    a.plugin1 = t.plugin1;
    a.p_vtable = t.p_vt;
    a.pp_vtable = t.pp_vt;
    a.cam_vtable = t.cam_vt;
    for (unsigned i = 0; i < 4; ++i) a.plugin_vt[i] = t.plugin_vt[i];
    a.pool1 = t.pool1;
    a.config.mode = set.input;
    a.config.mask = set.input_mask;
    a.config.words[0] = static_cast<pin::Word>(set.input_words[0]);
    a.config.words[1] = static_cast<pin::Word>(set.input_words[1]);
    a.config.words[2] = static_cast<pin::Word>(set.input_words[2]);
    a.config.yaw_sign = set.input_yaw == PartnerInputYaw::rot ? 1 : set.input_yaw == PartnerInputYaw::rotneg ? -1 : 0;
    a.config.rstick = set.input_rstick;
    a.config.triggers = set.input_triggers;
    a.config.stick_deg = set.stick_deg;
    a.config.stick_pct = set.stick_pct;
    a.config.stale_ms = set.stale_ms;
    a.host_cam_cdeg = tick.cam_cdeg;
    a.tap_seq = d.tap_seq;
    a.tap_bits = m.tap_bits;
    a.press_bits = m.press_bits;
    a.press_until_ms = m.press_until_ms;
    (void)st;
    (void)now;
    return a;
}

// Disarms the input window: publishes a disarmed Arm and logs the transition
// (windows = block-1 windows since the arm).
void DisarmInput(Driver& d, const Hooks& h, const char* reason) noexcept {
    Mission& m = d.mission;
    if (m.input_armed) {
        pin::Arm a{};
        a.gen = m.input_gen;
        if (h.publish_arm) h.publish_arm(h.context, a);
        m.input_armed = false;
        m.have_arm = false;
        unsigned windows = 0;
        if (h.drain_input_stats) {
            InputStats s{};
            h.drain_input_stats(h.context, s);
            windows = s.window.windows - m.arm_base.windows;
        }
        EmitInput(m, h, "partner_input=disarmed; reason=%s; gen=%u; windows=%u\r\n", reason, m.input_gen, windows);
    }
}
// Always publishes a disarmed Arm (no log): used where the mission state may
// not know about an Arm still published (MissionStart, refused ticks).
void PublishDisarmed(Driver& d, const Hooks& h) noexcept {
    pin::Arm a{};
    a.gen = d.mission.input_gen;
    if (h.publish_arm) h.publish_arm(h.context, a);
}

// Reverts the DM patch (if ours) and drops pending play state; logs the reason.
void RevertDm(Driver& d, const Hooks& h, const char* reason) noexcept {
    Mission& m = d.mission;
    if (m.dm_patched && h.dm_patch) {
        if (m.dm_play_pending && h.dm_clear_pending && h.dm_clear_pending(h.context)) Emit(h, "partner_dm=cleared_pending\r\n");
        m.dm_patched = h.dm_patch(h.context, false, reason);
    }
    m.dm_play_pending = false;
}

void TickDm(Driver& d, const Hooks& h, const pp::State& st, const Settings& set, const Clock& clock, bool terminating,
            std::uint32_t& ops, std::uint8_t& gani) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    if (set.dm == PartnerDm::off) {
        if (m.dm_patched) RevertDm(d, h, "trigger");
        d.dmplay_pending = d.dmstop_pending = d.dm_census_pending = false;
        return;
    }
    if (!h.dm_targets) { d.dmplay_pending = d.dmstop_pending = false; return; }
    pp::DmTargets targets{};
    h.dm_targets(h.context, st, targets);
    pp::DmState dm{};
    const bool dm_ok = h.read_dm && h.read_dm(h.context, st, targets, dm);
    const bool realized = pp::Realized(st, partner::partner_index);
    const bool parts_loaded = realized && st.parts[1] == c::parts_loaded_state;
    // Census: the first eligible tick (whatever the verdict), once more when
    // instance 1 is loaded (once per realize), and on census=1. Never per tick.
    if ((d.dm_census_pending || !m.dm_census_logged || (parts_loaded && !m.dm_census_loaded)) && h.format_dm_census) {
        d.dm_census_pending = false;
        m.dm_census_logged = true;
        if (parts_loaded) m.dm_census_loaded = true;
        char line[320]{};
        if (h.format_dm_census(h.context, targets, line, sizeof(line))) Emit(h, line);
    }
    // dm=on: patch once stage 3, parts loaded and input off all hold (the two
    // levers are mutually exclusive: an armed window or input!=off refuses).
    if (set.dm == PartnerDm::on && !m.dm_patched) {
        const char* refused = (m.input_armed || set.input != PartnerInput::off) ? "input_active" : nullptr;
        if (refused) {
            if (m.dm_refused_state != refused) {
                m.dm_refused_state = refused;
                EmitFormat(h, "partner_dm=refused; reason=%s\r\n", refused);
            }
        } else {
            m.dm_refused_state = nullptr;
            if (set.stage >= 3 && parts_loaded && !terminating && h.dm_patch) m.dm_patched = h.dm_patch(h.context, true, "trigger");
        }
    }
    // The partner's DM entry/End write CAM+0x1F0/+0x200 = 0 for any id
    // (0x1198D9D, 0x11992E9): log once when that would meet a host camera
    // connect or the host's own DM while ours is active.
    if (dm_ok && dm.q18[1] == c::q18_dm_act) {
        const bool conflict = dm.q18[0] == c::q18_dm_act || (dm.cam_ok && (dm.cam1f0 || dm.cam200));
        if (conflict && !m.dm_conflict_logged) {
            m.dm_conflict_logged = true;
            EmitFormat(h, "partner_dm=conflict; q18_0=0x%x; cam1f0=0x%llx; cam200=%u\r\n", unsigned(dm.q18[0]),
                       static_cast<unsigned long long>(dm.cam1f0), dm.cam200);
        }
    } else {
        m.dm_conflict_logged = false;
    }
    // 2 Hz probe (change-only).
    if (dm_ok && h.format_dm_probe && now - m.dm_probe_ms >= kDmProbeMs && m.dm_probe_lines < kDmProbeCap) {
        const bool changed = !m.dm_last_ok || std::memcmp(&dm, &m.dm_last, sizeof(dm)) != 0;
        if (changed) {
            m.dm_probe_ms = now;
            m.dm_last = dm;
            m.dm_last_ok = true;
            char line[320]{};
            if (h.format_dm_probe(h.context, dm, m.dm_patched, line, sizeof(line))) { Emit(h, line); ++m.dm_probe_lines; }
        }
    }
    // Pending play consumed? bit 4 of g+0x388 still set 2 s later -> unconsumed.
    if (m.dm_play_pending && dm_ok) {
        if (!(dm.g388 & 4)) {
            m.dm_play_pending = false;
        } else if (now - m.dm_play_ms >= kDmUnconsumedMs) {
            m.dm_play_pending = false;
            EmitFormat(h, "partner_dm=unconsumed; ms=%u\r\n", Clamp32(now - m.dm_play_ms));
            if (h.dm_clear_pending && h.dm_clear_pending(h.context)) Emit(h, "partner_dm=cleared_pending\r\n");
        }
    }
    // One-shots.
    if (d.dmplay_pending) {
        d.dmplay_pending = false;
        const bool busy = dm_ok && (dm.q18[0] == c::q18_dm_act || dm.q18[1] == c::q18_dm_act);
        const bool pending = dm_ok && (dm.g388 & 0x3C) != 0;
        // Without a locator the entry writes CAM+0x1F0 = 0 and CAM+0x200 = 0
        // (0x1198D9D): a no-op only while both already are 0.
        const bool cam_idle = dm_ok && dm.cam_ok && dm.cam1f0 == 0 && dm.cam200 == 0;
        if (!m.dm_patched) Emit(h, "partner_dm=refused; reason=not_patched\r\n");
        else if (!parts_loaded) Emit(h, "partner_dm=refused; reason=parts\r\n");
        else if (!dm_ok || busy || pending) Emit(h, "partner_dm=refused; reason=busy\r\n");
        else if (!cam_idle) Emit(h, "partner_dm=refused; reason=cam_connect\r\n");
        else {
            ops |= partner::op::dm_play;
            gani = d.dmplay_gani;
            m.dm_play_pending = true;
            m.dm_play_ms = now;
            m.dm_pending_gani = d.dmplay_gani;
            EmitFormat(h, "partner_dm=play; gani=%u\r\n", unsigned(d.dmplay_gani));
        }
    }
    if (d.dmstop_pending) {
        d.dmstop_pending = false;
        const bool ok = dm_ok && dm.q18[1] == c::q18_dm_act && dm.q18[0] != c::q18_dm_act;
        if (!m.dm_patched) Emit(h, "partner_dm=refused; reason=not_patched\r\n");
        else if (!ok) Emit(h, "partner_dm=refused; reason=not_in_dm\r\n");
        else { ops |= partner::op::dm_stop; Emit(h, "partner_dm=stop\r\n"); }
    }
}

// The input census line (census=1 and once per arm transition, capped).
void LogInputCensus(Mission& m, const Hooks& h, const pp::InputTargets& targets) noexcept {
    if (!h.format_input_census || m.input_census_lines >= kRefusedCap) return;
    char line[pp::input_census_capacity]{};
    if (h.format_input_census(h.context, targets, line, sizeof(line))) {
        ++m.input_census_lines;
        Emit(h, line);
    }
}

void TickInput(Driver& d, const Hooks& h, const LabConfig& lab, const pp::State& st, const Settings& set, const partner::Tick& tick,
               const Clock& clock, bool terminating, bool parts_loaded, bool pose_valid, const partner::Pose& pose, bool relink,
               std::uint32_t& ops) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    // Diagnostic P+0x218 window: pending request, then auto-expiry. Hazardous
    // (P+0x218 is paired with P+0x214 / the local block P+0x58, e.g. 0x11AD05D
    // and 0xFE8F85): it needs the lab key and the camera sandbox.
    if (d.p218_pending) {
        d.p218_pending = false;
        if (d.p218_on) {
            if (!lab.partner_input_p218_allow) Emit(h, "partner_input=p218_refused; reason=not_allowed\r\n");
            else if (set.input_cam != PartnerInputCam::sandbox) Emit(h, "partner_input=p218_refused; reason=cam\r\n");
            else {
                m.p218_active = true;
                m.p218_ms = now;
                Emit(h, "partner_input=p218; on=1; hazard=index0_routing; restart_after=1\r\n");
            }
        } else if (m.p218_active) {
            m.p218_active = false;
            Emit(h, "partner_input=p218; on=0\r\n");
        }
    }
    if (m.p218_active && set.input_cam != PartnerInputCam::sandbox) {
        m.p218_active = false;
        Emit(h, "partner_input=p218; on=0; reason=cam\r\n");
    }
    if (m.p218_active && now - m.p218_ms >= kP218MaxMs) {
        m.p218_active = false;
        Emit(h, "partner_input=p218_expired\r\n");
    }
    // holdlog=0|1 (I5): effective only with the lab key.
    if (d.holdlog_changed) {
        d.holdlog_changed = false;
        if (set.holdlog && !lab.partner_input_holdlog) Emit(h, "partner_input=holdlog_refused; reason=not_allowed\r\n");
        else EmitFormat(h, "partner_input=holdlog; on=%u\r\n", set.holdlog ? 1u : 0u);
    }
    // Hook statistics, drained once per tick. A recorded window fault disables
    // native calls, the window and the DM patch for the rest of the mission.
    InputStats s{};
    if (h.drain_input_stats) h.drain_input_stats(h.context, s);
    if (s.window.us_max > m.us_max_acc) m.us_max_acc = s.window.us_max;
    if (s.window.fault_phase && !m.input_faulted) {
        m.input_faulted = true;
        m.native_disabled = true;
        EmitFormat(h, "partner_call=fault; fn=input_window; native_calls=disabled\r\n");
        DisarmInput(d, h, "fault");
        if (m.dm_patched) RevertDm(d, h, "fault");
    }
    // Sender: the local pad, every tick, independent of arming (stage >= 1).
    if (set.isend && set.stage >= 1 && h.read_pad && h.submit_input) {
        pin::PadRaw raw{};
        if (h.read_pad(h.context, st, raw)) {
            const pin::PadSnapshot snap = pin::Effective(raw);
            m.sender_w4c |= snap.w4c;
            m.sender_w50 |= snap.w50;
            m.sender_w54 |= snap.w54;
            pin::InputPacket pkt = pin::Quantize(snap, tick.cam_cdeg);
            pkt.w4c = m.sender_w4c;
            pkt.w50 = m.sender_w50;
            pkt.w54 = m.sender_w54;
            if (h.submit_input(h.context, pkt)) { m.sender_w4c = m.sender_w50 = m.sender_w54 = 0; }
        }
    }
    // Pad probe (read-only), change-only, at most 20 Hz.
    if (set.pad_probe && h.read_pad && now - m.pad_probe_ms >= kPadProbeMs && m.pad_probe_lines < kPadProbeCap) {
        pin::PadRaw raw{};
        if (h.read_pad(h.context, st, raw)) {
            const pin::PadSnapshot snap = pin::Effective(raw);
            const pin::DirState dir = pin::CheckDir(snap, raw.pp_mask[1]);
            m.pad_probe_ms = now;
            ++m.pad_probe_lines;
            EmitFormat(h,
                "partner_pad; held=0x%x; w4c=0x%x; w50=0x%x; w54=0x%x; lx=%d; ly=%d; rx=%d; ry=%d; lt=%u; rt=%u; mag=%d; "
                "force=%d; dir_mrad=%d; dir_check=%s; masks=0x%x,0x%x,0x%x; kb=0x%02x; cam_cdeg=%d\r\n",
                snap.held, snap.w4c, snap.w50, snap.w54, pin::Milli(snap.lx), pin::Milli(snap.ly), pin::Milli(snap.rx),
                pin::Milli(snap.ry), unsigned(pin::Quantize(snap, 0).lt), unsigned(pin::Quantize(snap, 0).rt),
                pin::Milli(snap.mag), pin::Milli(snap.force), pin::Milli(snap.dir), pin::DirStateName(dir), raw.pp_mask[0],
                raw.pp_mask[1], raw.pp_mask[2], unsigned(raw.kb_flags), tick.cam_cdeg);
        }
    }
    // The block-1 targets (pure reads), resolved at most once per tick.
    pp::InputTargets targets{};
    const char* census = nullptr;
    bool resolved = false;
    const auto resolve = [&]() noexcept {
        if (resolved) return;
        resolved = true;
        if (h.input_targets) census = h.input_targets(h.context, st, targets);
        else census = targets.refusal = "unavailable";
    };
    // census=1: logged now, armed or not, failing verdicts included (I0).
    if (d.input_census_pending) {
        d.input_census_pending = false;
        resolve();
        LogInputCensus(m, h, targets);
    }
    const bool input_apply = set.apply == PartnerApply::input;
    // input=off: no window; keep the proven behaviour.
    if (set.input == PartnerInput::off) {
        DisarmInput(d, h, "trigger");
        d.tap_pending = d.press_pending = false;
        return;
    }
    // Arming conditions in order; the first failure is the refusal reason.
    char reason[48] = {0};
    const auto fail = [&](const char* text) noexcept { if (!reason[0]) StringCchCopyA(reason, sizeof(reason), text); };
    if (m.input_faulted) fail("faulted");
    if (m.native_disabled) fail("native_disabled");
    if (!(set.apply == PartnerApply::input || set.apply == PartnerApply::xform)) fail("apply");
    if (set.anim != PartnerAnim::off) fail("anim");
    if (set.dm == PartnerDm::on) fail("dm");
    if (set.stage < 3) fail("stage");
    resolve();
    // "busy": a window was open across every read attempt (hook thread !=
    // Lua thread). An armed window keeps its published Arm for this tick.
    const bool busy = census && std::strcmp(census, "busy") == 0;
    const bool keep_arm = busy && m.input_armed;
    if (!reason[0] && census && !keep_arm) { StringCchPrintfA(reason, sizeof(reason), "census_%s", census); }
    // Placement (apply=input): a Lua Warp places the body while disarmed, on
    // the first parts load and on a net relink (lost -> live). It runs only
    // when nothing else refuses arming, takes this whole tick (publishing a
    // disarmed Arm), and arming re-evaluates next tick.
    if (!reason[0] && input_apply && pose_valid && set.stage >= 1) {
        const bool first = !m.input_armed && !m.input_placed && parts_loaded;
        if (relink || first) {
            ops |= partner::op::warp;
            m.input_placed = true;
            DisarmInput(d, h, "relink");  // logs only if it was armed
            if (!m.input_armed) {
                pin::Arm idle{};
                idle.gen = m.input_gen;
                if (h.publish_arm) h.publish_arm(h.context, idle);
            }
            EmitFormat(h, "partner_input_place; reason=%s; err_mm=0; via=warp\r\n", relink ? "relink" : "first");
            return;
        }
    }
    if (input_apply && !m.input_placed) fail("placing");
    if (set.input == PartnerInput::net && h.net_running && !h.net_running(h.context)) fail("net");
    // Lazy hook install.
    if (!reason[0]) {
        if (!lab.partner_input_hook) fail("hook_disabled");
        else if (h.install_input_hook) {
            if (const char* why = h.install_input_hook(h.context)) StringCchPrintfA(reason, sizeof(reason), "install_%s", why);
        } else fail("unavailable");
    }
    if (terminating) fail("terminating");
    // One-shot acceptance (only while about to be armed and within the mask).
    const bool will_arm = reason[0] == 0;
    if (d.tap_pending) {
        d.tap_pending = false;
        if (!will_arm) Emit(h, "partner_input=tap_refused; reason=not_armed\r\n");
        else if (d.tap_bits_pending & ~set.input_mask) Emit(h, "partner_input=tap_refused; reason=mask\r\n");
        else {
            ++d.tap_seq;
            m.tap_bits = d.tap_bits_pending;
            EmitFormat(h, "partner_input=tap; bits=0x%x; frames=4\r\n", d.tap_bits_pending);
        }
    }
    if (d.press_pending) {
        d.press_pending = false;
        if (!will_arm) Emit(h, "partner_input=tap_refused; reason=not_armed\r\n");
        else if (d.press_bits_pending & ~set.input_mask) Emit(h, "partner_input=tap_refused; reason=mask\r\n");
        else {
            m.press_bits = d.press_bits_pending;
            m.press_until_ms = now + d.press_ms_pending;
            EmitFormat(h, "partner_input=press; bits=0x%x; ms=%u\r\n", d.press_bits_pending, d.press_ms_pending);
        }
    }
    if (!will_arm) {
        if (std::strcmp(m.input_refused_text, reason) != 0) {
            StringCchCopyA(m.input_refused_text, sizeof(m.input_refused_text), reason);
            if (m.input_refused_logs < kRefusedCap) {
                ++m.input_refused_logs;
                EmitFormat(h, "partner_input=refused; reason=%s\r\n", reason);
            } else if (!m.input_refused_capped) {
                m.input_refused_capped = true;
                Emit(h, "partner_input=refused; reason=capped; limit=64\r\n");
            }
        }
        DisarmInput(d, h, "refused");
        return;
    }
    m.input_refused_text[0] = 0;
    // Build and publish the Arm (gen bumps only when a tracked field changes).
    // A busy tick keeps the Arm already published (its targets are unread).
    if (!keep_arm) {
        pin::Arm arm = BuildArm(d, set, targets, st, tick, now, true);
        arm.holdlog = lab.partner_input_holdlog && set.holdlog;
        if (!m.have_arm || !pin::SameArm(arm, m.last_arm)) ++m.input_gen;
        arm.gen = m.input_gen;
        if (h.publish_arm) h.publish_arm(h.context, arm);
        m.last_arm = arm;
        m.have_arm = true;
    }
    if (!m.input_armed) {
        m.input_armed = true;
        m.arm_base = s.window;
        m.stats_base = s.window;
        m.stats_base_ms = now;
        m.input_stats_ms = now;
        m.input_stale_logged = false;
        m.us_max_acc = 0;
        char words[4]{};
        EmitInput(m, h, "partner_input=armed; mode=%s; gen=%u; apply=%s; cam=%s; hold=%s; yaw=%s; words=%s; mask=0x%x; correct_mm=%u; p218=%u\r\n",
                  PartnerInputName(set.input), m.input_gen, PartnerApplyName(set.apply), PartnerInputCamName(set.input_cam),
                  PartnerInputHoldName(set.input_hold), PartnerInputYawName(set.input_yaw), WordsText(set, words), set.input_mask,
                  set.correct_mm, m.p218_active ? 1u : 0u);
        LogInputCensus(m, h, targets);  // once per arm
    }
    // Correction (apply=input, armed): a rare xform snap when the body drifts.
    if (input_apply && set.correct_mm > 0 && m.body_ok && m.have_cmd) {
        const std::uint32_t err = partner::DistanceMm(m.cmd.x_mm, m.cmd.y_mm, m.cmd.z_mm, MetresToMm(m.body_pos[0]),
                                                      MetresToMm(m.body_pos[1]), MetresToMm(m.body_pos[2]));
        if (err > set.correct_mm) {
            if (!m.correct_over_ms) m.correct_over_ms = now;
            if (now - m.correct_over_ms >= kCorrectHoldMs && (!m.correct_last_ms || now - m.correct_last_ms >= kCorrectGapMs)) {
                m.correct_last_ms = now;
                m.correct_over_ms = 0;
                alignas(16) float p[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                                          static_cast<float>(pose.z_mm / 1000.0), 0.0f};
                alignas(16) float q[4]{};
                partner::YawToQuat(static_cast<float>(pose.yaw_mrad / 1000.0), q);
                const char* refused = m.native_disabled ? "native_disabled"
                                    : !h.apply_transform ? "unavailable"
                                    : h.apply_transform(h.context, terminating, p, q, PartnerReset::none);
                if (IsFault(refused)) NoteFault(m, h, "input_correct");
                if (refused) {
                    // Disarm, warp, and re-arm next tick.
                    ops |= partner::op::warp;
                    DisarmInput(d, h, "correct");
                    EmitFormat(h, "partner_input_place; reason=err; err_mm=%u; via=warp\r\n", err);
                } else {
                    EmitFormat(h, "partner_input_place; reason=err; err_mm=%u; via=xform\r\n", err);
                }
            }
        } else {
            m.correct_over_ms = 0;
        }
    }
    // Stale/live transitions (net only, once this arm has run a window): the
    // state of the most recent window (a missing sample is stale too).
    const std::uint32_t windows_armed = s.window.windows - m.arm_base.windows;
    if (set.input == PartnerInput::net && windows_armed > 0) {
        const bool stale = s.window.last_stale != 0;
        if (!m.input_stale_logged || stale != m.input_stale) {
            m.input_stale_logged = true;
            m.input_stale = stale;
            if (!stale) EmitInput(m, h, "partner_input=live\r\n");
            else if (s.window.last_age_ms == pin::kNoSample) EmitInput(m, h, "partner_input=stale; age_ms=none\r\n");
            else EmitInput(m, h, "partner_input=stale; age_ms=%u\r\n", s.window.last_age_ms);
        }
    }
    // 1 Hz line: windows since the arm; per_s, root1_calls, skips and stale
    // are this interval's deltas (per_s normalized to one second).
    if (now - m.input_stats_ms >= kInputStatsMs) {
        const pin::WindowStats& b = m.stats_base;
        const std::uint64_t dt = now > m.stats_base_ms ? now - m.stats_base_ms : 0;
        const std::uint32_t dw = s.window.windows - b.windows;
        const std::uint32_t per_s = dt ? Clamp32(static_cast<std::uint64_t>(dw) * 1000u / dt) : 0u;
        char age[16]{};
        if (s.window.last_age_ms == pin::kNoSample) StringCchCopyA(age, sizeof(age), "none");
        else StringCchPrintfA(age, sizeof(age), "%u", s.window.last_age_ms);
        EmitInput(m, h,
            "partner_input; mode=%s; windows=%u; per_s=%u; root1_calls=%u; skips=%u; skip=%s; stale=%u; age_ms=%s; held=0x%x; "
            "lx=%d; ly=%d; dir_mrad=%d; mag=%d; cam_dirty=0x%x; cam_restored=%u; hold_calls=%u; us_max=%u; hook_thread=%u; lua_thread=%u\r\n",
            PartnerInputName(set.input), windows_armed, per_s, s.window.root1_calls - b.root1_calls, s.window.skips - b.skips,
            pin::SkipName(s.window.last_skip), s.window.stale - b.stale, age, s.window.last_held, s.window.last_lx,
            s.window.last_ly, s.window.last_dir, s.window.last_mag, s.window.cam_dirty_or, s.window.cam_restored - b.cam_restored,
            s.window.hold_calls - b.hold_calls, m.us_max_acc, s.window.hook_thread, static_cast<unsigned>(GetCurrentThreadId()));
        m.input_stats_ms = now;
        m.stats_base = s.window;
        m.stats_base_ms = now;
        m.us_max_acc = 0;
    }
    // Hold log lines (I5), change-only per button.
    for (unsigned i = 0; i < s.hold_count && m.hold_logs < kHoldLogCap; ++i) {
        const pin::HoldRecord& r = s.holds[i];
        bool dup = false;
        for (unsigned j = 0; j < m.hold_last_count; ++j)
            if (m.hold_last[j].btn == r.btn && m.hold_last[j].frames == r.frames && m.hold_last[j].result == r.result) dup = true;
        if (dup) continue;
        if (m.hold_last_count < 8) m.hold_last[m.hold_last_count++] = r;
        ++m.hold_logs;
        EmitFormat(h, "partner_holdlog; btn=%u; frames=%u; result=%u\r\n", r.btn, r.frames, r.result);
    }
    // Body line (2 Hz): position/heading/speed and the slot-1 anim fields.
    if (h.read_body && m.body_ok && now - m.body_ms >= kBodyMs && m.body_lines < kBodyCap) {
        const std::int32_t bx = MetresToMm(m.body_pos[0]), by = MetresToMm(m.body_pos[1]), bz = MetresToMm(m.body_pos[2]);
        const float body_yaw = partner::QuatToYaw(m.body_quat);
        std::uint32_t speed = 0;
        std::int32_t heading = 0;
        if (m.body_prev_ok && now > m.body_prev_ms) {
            const double dx = bx - m.body_prev_mm[0], dz = bz - m.body_prev_mm[2];
            const double dist = std::sqrt(dx * dx + (static_cast<double>(by - m.body_prev_mm[1])) * (by - m.body_prev_mm[1]) + dz * dz);
            speed = Clamp32(static_cast<std::uint64_t>(dist * 1000.0 / (now - m.body_prev_ms)));
            if (dx != 0.0 || dz != 0.0) heading = pp::ToMrad(static_cast<float>(partner::NormalizeMrad(
                static_cast<std::int64_t>(std::atan2(dx, dz) * 1000.0))) / 1000.0f);
        }
        pp::Anim banim{};
        const bool banim_ok = h.read_anim && h.read_anim(h.context, st, banim);
        std::uint8_t psw = 0;
        if (h.census) {
            pp::Census cen{};
            h.census(h.context, st, cen);
            psw = cen.k[1].psw;
        }
        m.body_ms = now;
        ++m.body_lines;
        EmitFormat(h,
            "partner_body; R=1; body_mm=%d,%d,%d; yaw_mrad=%d; speed_mmps=%u; heading_mrad=%d; cam_mrad=%d; act=%u; s=%u; v=%u; psw=0x%02x\r\n",
            bx, by, bz, pp::ToMrad(body_yaw), speed, heading, partner::CdegToMrad(tick.cam_cdeg),
            banim_ok ? unsigned(banim.act[1]) : 0u, banim_ok ? unsigned(banim.stance[1]) : 0u, banim_ok ? unsigned(banim.speed[1]) : 0u,
            unsigned(psw));
        m.body_prev_mm[0] = bx; m.body_prev_mm[1] = by; m.body_prev_mm[2] = bz;
        m.body_prev_ms = now;
        m.body_prev_ok = true;
    }
    // The patch-live check.
    if (h.input_hook_live && !h.input_hook_live(h.context)) {
        Emit(h, "partner_input_hook=lost\r\n");
        DisarmInput(d, h, "hook_lost");
    }
}
} // namespace

void Configure(Driver& d, const LabConfig& lab) noexcept {
    d = Driver{};
    d.settings.stage = lab.partner_stage;
    d.settings.source = lab.partner_source;
    d.settings.apply = lab.partner_apply;
    d.settings.anim = lab.partner_anim;
    d.settings.aset = lab.partner_aset;
    d.settings.reset = lab.partner_reset;
    d.settings.look = lab.partner_look;
    d.settings.probe_ps = false;
    d.settings.input = lab.partner_input;
    d.settings.input_cam = lab.partner_input_cam;
    d.settings.input_hold = lab.partner_input_hold;
    d.settings.input_yaw = lab.partner_input_yaw;
    d.settings.input_words[0] = lab.partner_input_words[0];
    d.settings.input_words[1] = lab.partner_input_words[1];
    d.settings.input_words[2] = lab.partner_input_words[2];
    d.settings.input_mask = lab.partner_input_mask;
    d.settings.input_rstick = lab.partner_input_rstick;
    d.settings.input_triggers = lab.partner_input_triggers;
    d.settings.stick_deg = static_cast<std::uint16_t>(lab.partner_input_stick_deg);
    d.settings.stick_pct = static_cast<std::uint8_t>(lab.partner_input_stick_pct);
    d.settings.correct_mm = lab.partner_input_correct_mm;
    d.settings.stale_ms = lab.partner_input_stale_ms;
    d.settings.isend = lab.partner_input_send;
    d.settings.pad_probe = lab.partner_pad_probe;
    d.settings.look_gate = lab.partner_look_gate;
    d.settings.look_recover = lab.partner_look_recover;
    d.settings.dm = lab.partner_dm;
    d.settings.holdlog = lab.partner_input_holdlog;
    d.look_forced_rev = 1;
}

void MissionStart(Driver& d, const Hooks& h) noexcept {
    // A missed MissionEnd/Stop must not leave the window armed or the DM
    // patch applied into the next mission: tear down before the reset, and
    // always publish a disarmed Arm. The hook's recorded fault is cleared
    // (input_faulted is per mission).
    DisarmInput(d, h, "mission_start");
    if (d.mission.dm_patched) RevertDm(d, h, "mission_start");
    PublishDisarmed(d, h);
    if (h.reset_input_fault) h.reset_input_fault(h.context);
    d.mission = Mission{};
    d.mission.anim_source = -1;
    d.mission.last_status = pose::Status::none;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
    d.tap_pending = d.press_pending = d.dmplay_pending = d.dmstop_pending = d.p218_pending = false;
    d.input_census_pending = d.dm_census_pending = false;
    if (!h.take_trigger) return;
    char text[partner::max_trigger + 1]{};
    std::size_t length = 0;
    if (h.take_trigger(h.context, text, partner::max_trigger, length) != TriggerRead::none)
        Emit(h, "partner_trigger=discarded; reason=stale\r\n");
}

void MissionEnd(Driver& d, const Hooks& h) noexcept {
    DisarmInput(d, h, "mission_end");
    if (d.mission.dm_patched) RevertDm(d, h, "mission_end");
    d.mission.cfg_ready = false;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
    d.tap_pending = d.press_pending = d.dmplay_pending = d.dmstop_pending = d.p218_pending = false;
}

bool Cfg(Driver& d, const Hooks& h, const char* precheck, std::uint64_t now_ms) noexcept {
    Mission& m = d.mission;
    const char* reason = precheck;
    pp::State st{};
    if (!reason) {
        const pp::Refusal refusal = h.read_state(h.context, st);
        if (refusal != pp::Refusal::none) reason = pp::RefusalName(refusal);
    }
    if (reason) {
        m.cfg_ready = false;
        DisarmInput(d, h, "cfg_refused");
        if (m.dm_patched) RevertDm(d, h, "cfg_refused");
        EmitFormat(h, "partner_cfg=refused; reason=%s\r\n", reason);
        return false;
    }
    char line[256]{};
    if (pp::FormatReady(st, line, sizeof(line))) Emit(h, line);
    m.cfg_ready = true;
    m.stopped = false;
    m.refusal = nullptr;
    m.start_ms = now_ms;
    m.flags = st.flags;
    return true;
}

void LuaLog(Driver& d, const Hooks& h, const char* source, std::size_t size) noexcept {
    Mission& m = d.mission;
    if (m.lua_logs >= kLuaLogCap) {
        if (!m.lua_logs_capped) {
            m.lua_logs_capped = true;
            Emit(h, "partner_lua=capped; limit=64\r\n");
        }
        return;
    }
    ++m.lua_logs;
    char line[partner::log_line_capacity]{};
    if (partner::FormatLogLine(source, size, line, sizeof(line))) Emit(h, line);
}

void Stop(Driver& d, const Hooks& h, PartnerTeardown teardown, bool terminating) noexcept {
    Mission& m = d.mission;
    unsigned cleared = 0;
    DisarmInput(d, h, "stop");
    if (m.dm_patched) RevertDm(d, h, "stop");
    if (teardown == PartnerTeardown::clear && ClearBits(d, h, "stop", terminating)) cleared = 1;
    RestorePolicy(d, h, terminating);
    m.stopped = true;
    m.cfg_ready = false;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
    d.tap_pending = d.press_pending = d.dmplay_pending = d.dmstop_pending = d.p218_pending = false;
    EmitFormat(h, "partner_stop; cleared=%u; teardown=%s\r\n", cleared, PartnerTeardownName(teardown));
}

void ApplyTrigger(Driver& d, const Hooks& h, const partner::Trigger& trigger, bool terminating) noexcept {
    Mission& m = d.mission;
    const std::uint32_t stage = trigger.stop ? 0 : trigger.has_stage ? trigger.stage : d.settings.stage;
    if (trigger.has_source && trigger.source != d.settings.source) m.have_cmd = false;
    if (trigger.has_source) d.settings.source = trigger.source;
    if (trigger.has_apply) {
        if (trigger.apply != d.settings.apply) { m.hybrid_placed = false; m.xform_logged = false; }
        d.settings.apply = trigger.apply;
    }
    if (trigger.has_anim) d.settings.anim = trigger.anim;
    if (trigger.has_aset) d.settings.aset = trigger.aset;
    if (trigger.has_reset) d.settings.reset = trigger.reset;
    if (trigger.has_probe) d.settings.probe_ps = trigger.probe_ps;
    if (trigger.has_look) {
        d.settings.look = trigger.look;
        ++d.look_forced_rev;
    }
    if (trigger.has_anim || trigger.has_aset) {
        // A new animation setting starts its stance and array logging afresh.
        m.stance = partner::StanceDebounce{};
        m.stance_watch = false;
        m.stance_refused_state = nullptr;
        m.anim_write_logged = false;
        m.anim_source = -1;
    }
    // Round 3 settings.
    if (trigger.has_input) d.settings.input = trigger.input;
    if (trigger.has_stick) { d.settings.stick_deg = static_cast<std::uint16_t>(trigger.stick_deg);
        d.settings.stick_pct = static_cast<std::uint8_t>(trigger.stick_pct); }
    if (trigger.has_cam) d.settings.input_cam = trigger.cam;
    if (trigger.has_hold) d.settings.input_hold = trigger.hold;
    if (trigger.has_yaw) d.settings.input_yaw = trigger.yaw;
    if (trigger.has_words) { d.settings.input_words[0] = trigger.words[0]; d.settings.input_words[1] = trigger.words[1];
        d.settings.input_words[2] = trigger.words[2]; }
    if (trigger.has_bits) d.settings.input_mask = trigger.bits;
    if (trigger.has_correct) d.settings.correct_mm = trigger.correct_mm;
    if (trigger.has_isend) d.settings.isend = trigger.isend;
    if (trigger.has_padprobe) d.settings.pad_probe = trigger.padprobe;
    if (trigger.has_dm) d.settings.dm = trigger.dm;
    if (trigger.has_lookgate) d.settings.look_gate = trigger.lookgate;
    if (trigger.has_lookrecover) d.settings.look_recover = trigger.lookrecover;
    if (trigger.has_holdlog) { d.settings.holdlog = trigger.holdlog; d.holdlog_changed = true; }
    d.settings.stage = stage;
    char words_text[4]{};
    FormatPartnerWords(d.settings.input_words, words_text);
    EmitFormat(h, "partner_stage=changed; stage=%u; source=%s; apply=%s; anim=%s; aset=%s; reset=%s; look=%s; probe=%s; "
               "lookgate=%u; lookrecover=%u; input=%s; cam=%s; hold=%s; yaw=%s; words=%s; mask=0x%x; correct=%u; isend=%u; dm=%s; via=trigger\r\n",
               stage, PartnerSourceName(d.settings.source), PartnerApplyName(d.settings.apply), PartnerAnimName(d.settings.anim),
               PartnerAsetName(d.settings.aset), PartnerResetName(d.settings.reset), PartnerLookName(d.settings.look),
               d.settings.probe_ps ? "ps" : "off", d.settings.look_gate ? 1u : 0u, d.settings.look_recover ? 1u : 0u,
               PartnerInputName(d.settings.input), PartnerInputCamName(d.settings.input_cam),
               PartnerInputHoldName(d.settings.input_hold), PartnerInputYawName(d.settings.input_yaw), words_text,
               d.settings.input_mask, d.settings.correct_mm, d.settings.isend ? 1u : 0u, PartnerDmName(d.settings.dm));
    if (trigger.census) d.census_pending = d.input_census_pending = d.dm_census_pending = true;
    // Round 3 one-shots.
    if (trigger.tap) { d.tap_pending = true; d.tap_bits_pending = trigger.tap_bits; }
    if (trigger.press) { d.press_pending = true; d.press_bits_pending = trigger.press_bits; d.press_ms_pending = trigger.press_ms; }
    if (trigger.has_p218) { d.p218_pending = true; d.p218_on = trigger.p218; }
    if (stage >= 1 && trigger.dmplay) { d.dmplay_pending = true; d.dmplay_gani = trigger.dmplay_gani; }
    if (trigger.dmstop) d.dmstop_pending = true;
    if (stage >= 1) {
        if (trigger.parts_explicit) {
            // look=copy: one SET_PARTS with the local look (F3b); otherwise the
            // desired look is re-applied once.
            if (d.settings.look == PartnerLook::copy) d.parts_explicit_pending = true;
            else ++d.look_forced_rev;
        }
        if (trigger.mirror_on) {
            // Mirror mode copies the local look over instance 1 (0x9CE786).
            if (d.settings.look == PartnerLook::copy) d.mirror_pending = true;
            else Emit(h, "partner_trigger=ignored; token=mirror=on; reason=look\r\n");
        }
    } else {
        // Stage 0 (or stop): one-shots never wait for a later stage or mission.
        d.parts_explicit_pending = false;
        d.mirror_pending = false;
        if (trigger.parts_explicit) Emit(h, "partner_trigger=ignored; token=parts=explicit; reason=stage\r\n");
        if (trigger.mirror_on) Emit(h, "partner_trigger=ignored; token=mirror=on; reason=stage\r\n");
    }
    if (stage < 3) {
        ClearBits(d, h, "trigger", terminating);
        RestorePolicy(d, h, terminating);
        // Back at stage 3, the bits again wait for a freshly applied pose.
        m.pose_applied = false;
    }
}

void PollTrigger(Driver& d, const Hooks& h, bool terminating) noexcept {
    if (!h.take_trigger) return;
    char text[partner::max_trigger + 1]{};
    std::size_t length = 0;
    const TriggerRead read = h.take_trigger(h.context, text, partner::max_trigger, length);
    if (read == TriggerRead::none) return;
    partner::Trigger trigger{};
    if (read != TriggerRead::read || length > partner::max_trigger || !partner::ParseTrigger(text, length, trigger)) {
        Emit(h, "partner_trigger=rejected; reason=parse\r\n");
        return;
    }
    ApplyTrigger(d, h, trigger, terminating);
}

bool Tick(Driver& d, const Hooks& h, const LabConfig& lab, const char* source, std::size_t size, const Clock& clock,
          bool terminating, partner::Reply& reply) noexcept {
    Mission& m = d.mission;
    if (!m.cfg_ready || m.stopped || terminating) return false;
    partner::Tick tick{};
    if (!partner::ParseTick(source, size, tick)) {
        if (m.tick_rejects < kCappedLogs) {
            ++m.tick_rejects;
            Emit(h, "partner_tick=rejected; reason=parse\r\n");
        }
        // Without a tick nothing re-validates the window or the DM patch.
        DisarmInput(d, h, "tick_rejected");
        if (m.dm_patched) RevertDm(d, h, "tick_rejected");
        return false;
    }
    ++m.ticks;
    const std::uint64_t now = clock.now_ms;
    if (m.ticks % kTriggerPollTicks == 0) PollTrigger(d, h, terminating);
    pp::State st{};
    const pp::Refusal refusal = h.read_state(h.context, st);
    if (refusal != pp::Refusal::none) {
        const char* name = pp::RefusalName(refusal);
        if (m.refusal != name) {
            m.refusal = name;
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_cfg=refused; reason=%s\r\n", name))) LogCfgFlip(m, h, line);
        }
        // The state guards failed (count, local, online bit, sync, ...): no
        // window and no DM patch while they fail; both re-arm on their own.
        DisarmInput(d, h, "cfg_refused");
        if (m.dm_patched) RevertDm(d, h, "cfg_refused");
        return false;
    }
    if (m.refusal) {
        m.refusal = nullptr;
        char line[256]{};
        if (pp::FormatReady(st, line, sizeof(line))) LogCfgFlip(m, h, line);
        m.flags = st.flags;
    }
    // P+0x204, change-only (E2 checks bit 0x2000; the probe line has no flags).
    if (st.flags != m.flags) {
        m.flags = st.flags;
        if (m.flags_logs < kTransitionLogCap) {
            ++m.flags_logs;
            EmitFormat(h, "partner_flags=0x%08x\r\n", st.flags);
        } else if (!m.flags_capped) {
            m.flags_capped = true;
            Emit(h, "partner_flags=capped; limit=64\r\n");
        }
    }
    // Cadence and units diagnostics.
    if (!m.rate_ms) { m.rate_ms = now; m.rate_ticks = 0; }
    ++m.rate_ticks;
    if (now - m.rate_ms >= 10000) {
        const bool focused = h.focused && h.focused(h.context);
        EmitFormat(h, "partner_tick=rate; hz=%u; focused=%u\r\n", Clamp32(m.rate_ticks * 1000 / (now - m.rate_ms)),
                   focused ? 1u : 0u);
        m.rate_ms = now;
        m.rate_ticks = 0;
    }
    const std::int32_t yaw_abs = tick.yaw_cdeg < 0 ? -tick.yaw_cdeg : tick.yaw_cdeg;
    if (yaw_abs > m.yaw_abs_max) m.yaw_abs_max = yaw_abs;
    if (!m.units_logged || now - m.units_ms >= 30000) {
        m.units_logged = true;
        m.units_ms = now;
        EmitFormat(h, "partner_units; lua_pos_mm=%d,%d,%d; mem_pos_mm=%d,%d,%d; yaw_cdeg=%d; yaw_abs_max_cdeg=%d; cam_cdeg=%d\r\n",
                   tick.x_mm, tick.y_mm, tick.z_mm, st.vars_ok ? pp::ToMm(st.vars_pos[0]) : INT32_MIN,
                   st.vars_ok ? pp::ToMm(st.vars_pos[1]) : INT32_MIN, st.vars_ok ? pp::ToMm(st.vars_pos[2]) : INT32_MIN,
                   tick.yaw_cdeg, m.yaw_abs_max, tick.cam_cdeg);
    }
    const Settings& set = d.settings;
    // Local animation fields and looks (sender side; anim=mirror source).
    pp::Anim anim{};
    const bool anim_ok = h.read_anim && h.read_anim(h.context, st, anim);
    const sync::AnimState local_anim = anim_ok ? pp::AnimSlot(anim, st.local) : sync::AnimState{};
    pp::Looks looks{};
    const bool looks_ok = h.read_looks && h.read_looks(h.context, st, looks);
    // Pose source.
    partner::Pose pose{};
    bool pose_valid = false;
    partner::Status status = partner::Status::synthetic;
    std::uint32_t age_ms = 0;
    sync::AnimState remote_anim{};
    pose::Status sample_status = pose::Status::none;
    if (set.source == PartnerSource::net) {
        const pose::Sample sample = h.sample_remote ? h.sample_remote(h.context, lab.partner_interp_ms) : pose::Sample{};
        sample_status = sample.status;
        status = sample.status == pose::Status::live ? partner::Status::live
               : sample.status == pose::Status::stale ? partner::Status::stale : partner::Status::lost;
        if (sample.valid) {
            pose = partner::PoseFromMetres(sample.x, sample.y, sample.z, sample.yaw);
            pose_valid = true;
            age_ms = sample.age_ms;
            if (sample.anim_valid) remote_anim = sync::AnimState{sample.act, sample.anim_stance, sample.anim_speed, sync::AnimFlag::Valid};
        }
    } else if (set.source == PartnerSource::orbit) {
        pose = partner::OrbitPose(tick, lab.partner_offset_cm, now - m.start_ms);
        pose_valid = true;
    } else {
        pose = partner::FixedPose(tick, lab.partner_offset_cm);
        pose_valid = true;
    }
    // Realize bits (stage 3): from the tick after the first applied pose, so
    // that S+0x20[1] is already in the area; re-ORed while lost.
    if (set.stage >= 3 && m.pose_applied) {
        const bool lost = m.bits_set && (st.q194 & 2u) == 0;
        if (lost && m.bits_lost_logs < kCappedLogs) {
            ++m.bits_lost_logs;
            EmitFormat(h, "partner_bits=lost; R=1; q194=0x%x; q198=0x%x\r\n", st.q194, st.q198);
        }
        if (!m.bits_set || lost) {
            std::uint32_t q194 = 0, q198 = 0;
            if (const char* refused = h.or_bits(h.context, terminating, q194, q198)) {
                LogWriteRefusal(m, h, "or_bits", refused);
            } else if (!m.bits_set) {
                m.bits_set = true;
                m.bits_ms = now;
                EmitFormat(h, "partner_bits=set; R=1; q194=0x%x; q198=0x%x\r\n", q194, q198);
            }
        }
    }
    if (lab.partner_force_exists && set.stage >= 2) {
        if (st.s0 & 2u) {
            m.s0_missing_ticks = 0;
        } else if (++m.s0_missing_ticks > kForceExistsAfterTicks) {
            m.s0_missing_ticks = 0;
            if (const char* refused = h.force_exists(h.context, terminating)) {
                LogWriteRefusal(m, h, "force_exists", refused);
            } else if (m.s0_forced_logs < kCappedLogs) {
                ++m.s0_forced_logs;
                Emit(h, "partner_s0=forced; R=1\r\n");
            }
        }
    }
    if (lab.partner_policy_relax && set.stage >= 3 && !m.policy_relaxed && !m.policy_refused_logged) {
        std::uint8_t previous = 0;
        if (const char* refused = h.set_policy(h.context, true, 0, terminating, previous)) {
            m.policy_refused_logged = true;
            EmitFormat(h, "partner_policy=refused; reason=%s\r\n", refused);
        } else {
            m.policy_relaxed = true;
            m.policy_previous = previous;
            Emit(h, "partner_policy=relaxed\r\n");
        }
    }
    const bool realized = pp::Realized(st, partner::partner_index);
    const bool parts_loaded = realized && st.parts[1] == c::parts_loaded_state;
    const bool slot = partner::WritesSlot(set.stage, set.apply, pose_valid);
    if (slot) {
        const float xyzw[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                               static_cast<float>(pose.z_mm / 1000.0), static_cast<float>(pose.yaw_mrad / 1000.0)};
        if (const char* refused = h.write_slot(h.context, terminating, xyzw)) LogWriteRefusal(m, h, "slot", refused);
    }
    // Movement drivers xform / hybrid (the slot write above keeps S+0x20 as
    // Warp would). The body readback compares the previous command.
    bool transform_warp = false;
    const bool transform = PartnerApplyIsTransform(set.apply);
    // lost -> live on the net source this tick (hybrid and apply=input relink).
    const bool relink = set.source == PartnerSource::net && m.last_status == pose::Status::lost && sample_status == pose::Status::live;
    if ((transform || set.apply == PartnerApply::input) && parts_loaded && h.read_body) {
        m.body_ok = h.read_body(h.context, st, m.body_pos, m.body_quat);
    } else {
        m.body_ok = false;
    }
    if (transform && m.body_ok && m.have_cmd && now - m.xform_check_ms >= 1000) {
        m.xform_check_ms = now;
        const std::int32_t bx = MetresToMm(m.body_pos[0]), by = MetresToMm(m.body_pos[1]), bz = MetresToMm(m.body_pos[2]);
        const float body_yaw = partner::QuatToYaw(m.body_quat);
        const std::int32_t yaw_err = partner::NormalizeMrad(static_cast<std::int64_t>(pp::ToMrad(body_yaw)) - m.cmd.yaw_mrad);
        EmitFormat(h, "partner_xform; R=1; apply=%s; cmd_mm=%d,%d,%d; body_mm=%d,%d,%d; err_mm=%u; yaw_err_mrad=%d; reset=%s\r\n",
                   PartnerApplyName(set.apply), m.cmd.x_mm, m.cmd.y_mm, m.cmd.z_mm, bx, by, bz,
                   partner::DistanceMm(m.cmd.x_mm, m.cmd.y_mm, m.cmd.z_mm, bx, by, bz), yaw_err < 0 ? -yaw_err : yaw_err,
                   PartnerResetName(set.reset));
    }
    if (transform && set.stage >= 1 && pose_valid) {
        const char* hybrid_reason = nullptr;
        if (set.apply == PartnerApply::hybrid && parts_loaded) {
            if (!m.hybrid_placed) hybrid_reason = "first";
            else if (relink)
                hybrid_reason = "relink";
            else if (m.body_ok && m.have_cmd &&
                     partner::DistanceMm(m.cmd.x_mm, m.cmd.y_mm, m.cmd.z_mm, MetresToMm(m.body_pos[0]), MetresToMm(m.body_pos[1]),
                                         MetresToMm(m.body_pos[2])) > kHybridErrorMm)
                hybrid_reason = "err";
        }
        if (hybrid_reason) {
            transform_warp = true;
            m.hybrid_placed = true;
            EmitCapped(m.hybrid_cap, h, "partner_hybrid", "partner_hybrid=warp; reason=%s\r\n", hybrid_reason);
        } else {
            alignas(16) float p[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                                      static_cast<float>(pose.z_mm / 1000.0), 0.0f};
            alignas(16) float q[4]{};
            partner::YawToQuat(static_cast<float>(pose.yaw_mrad / 1000.0), q);
            const char* refused = m.native_disabled ? "native_disabled"
                                : !h.apply_transform ? "unavailable"
                                : h.apply_transform(h.context, terminating, p, q, set.reset);
            if (IsFault(refused)) NoteFault(m, h, "xform");
            if (refused) {
                // The partner keeps moving through the proven Lua Warp.
                transform_warp = true;
                if (!m.xform_logged || m.xform_state != refused) {
                    m.xform_logged = true;
                    m.xform_state = refused;
                    EmitCapped(m.xform_cap, h, "partner_xform", "partner_xform=refused; reason=%s; fallback=warp\r\n", refused);
                }
            } else if (!m.xform_logged || m.xform_state) {
                m.xform_logged = true;
                m.xform_state = nullptr;
                EmitCapped(m.xform_cap, h, "partner_xform", "partner_xform=applied; R=1; apply=%s; reset=%s\r\n",
                           PartnerApplyName(set.apply), PartnerResetName(set.reset));
            }
        }
    }
    m.last_status = sample_status;
    // Animation application (instance 1 only, stage 3 with parts loaded).
    const bool anim_needed = set.anim != PartnerAnim::off && set.stage >= 3;
    if (anim_needed) {
        const sync::AnimState src = set.anim == PartnerAnim::mirror ? local_anim : remote_anim;
        const bool src_valid = (src.flags & sync::AnimFlag::Valid) != 0;
        if (m.anim_source != (src_valid ? 1 : 0)) {
            m.anim_source = src_valid ? 1 : 0;
            EmitCapped(m.anim_cap, h, "partner_anim", "partner_anim=source; anim=%s; valid=%u; act=%u; s=%u; v=%u\r\n",
                       PartnerAnimName(set.anim), src_valid ? 1u : 0u, unsigned(src.act), unsigned(src.stance), unsigned(src.speed));
        }
        if (!parts_loaded) {
            // Unrealized or reloading (a look apply runs the 3->4->0->1->3
            // parts cycle): the body may come back standing, so the stance is
            // applied again once the parts are loaded.
            m.stance = partner::StanceDebounce{};
            m.stance_watch = false;
        }
        if (src_valid && parts_loaded) {
            if (set.aset == PartnerAset::q || set.aset == PartnerAset::qs) {
                // Only the live-observed locomotion ids are copied (other action
                // ids feed action checks and the PlayerInfo special cases).
                const char* refused = !partner::LocomotionAct(src.act) ? "act_domain"
                                    : h.write_anim ? h.write_anim(h.context, terminating, src.act, src.stance, src.speed) : "unavailable";
                if (!m.anim_write_logged || m.anim_write_state != refused) {
                    m.anim_write_logged = true;
                    m.anim_write_state = refused;
                    if (refused) EmitCapped(m.anim_cap, h, "partner_anim", "partner_aset=q; state=refused; reason=%s; act=%u\r\n", refused,
                                            unsigned(src.act));
                    else EmitCapped(m.anim_cap, h, "partner_anim", "partner_aset=q; state=applied; act=%u; s=%u; v=%u\r\n",
                                    unsigned(src.act), unsigned(src.stance), unsigned(src.speed));
                }
            }
            if (set.aset != PartnerAset::q) {
                const std::uint8_t from = m.stance.applied;
                const std::uint8_t to = partner::DebounceStance(m.stance, src.stance, now, kStanceHoldMs);
                if (to) {
                    const bool request = set.aset == PartnerAset::r;
                    std::uint8_t psw = 0, q30 = 0;
                    const char* refused = m.native_disabled ? "native_disabled"
                                        : request ? (!h.request_stance ? "unavailable" : h.request_stance(h.context, terminating, to))
                                                  : (!h.set_stance ? "unavailable" : h.set_stance(h.context, terminating, to, psw, q30));
                    if (IsFault(refused)) NoteFault(m, h, request ? "stance_request" : "set_stance");
                    m.stance_summary_dirty = true;
                    if (refused) {
                        // Not committed: the same target is retried after
                        // kStanceRetryMs (a transient refusal must not stick).
                        partner::RetryStance(m.stance, now, kStanceRetryMs);
                        ++m.stance_refusals;
                        if (m.stance_refused_state != refused) {
                            m.stance_refused_state = refused;
                            if (request)
                                EmitStance(m.stance_refused_cap, h, "refused", "partner_stance=req; R=1; from=%u; to=%u; result=%s\r\n",
                                           unsigned(from), unsigned(to), refused);
                            else
                                EmitStance(m.stance_refused_cap, h, "refused", "partner_stance=refused; R=1; to=%u; reason=%s\r\n",
                                           unsigned(to), refused);
                        }
                    } else {
                        partner::CommitStance(m.stance, to);
                        m.stance_refused_state = nullptr;
                        m.stance_last = to;
                        if (request) {
                            ++m.stance_reqs;
                            EmitStance(m.stance_cap, h, "set", "partner_stance=req; R=1; from=%u; to=%u; result=ok\r\n",
                                       unsigned(from), unsigned(to));
                        } else {
                            ++m.stance_sets;
                            EmitStance(m.stance_cap, h, "set",
                                       "partner_stance=set; R=1; id=1; from=%u; to=%u; psw=0x%02x; q30=%u\r\n", unsigned(from),
                                       unsigned(to), unsigned(psw), unsigned(q30));
                            m.stance_watch = true;
                            m.stance_set_ms = now;
                            m.stance_expected_psw = static_cast<std::uint8_t>(to | (to << 4));
                        }
                    }
                }
            }
        }
    }
    if (m.stance_watch) {
        if (now - m.stance_set_ms > kStanceWatchMs || !h.census) {
            m.stance_watch = false;
        } else {
            pp::Census census{};
            h.census(h.context, st, census);
            if (!census.k[1].stance_refusal && census.k[1].psw != m.stance_expected_psw) {
                m.stance_watch = false;
                ++m.stance_reverts;
                m.stance_summary_dirty = true;
                EmitStance(m.stance_revert_cap, h, "reverted",
                           "partner_stance=reverted; R=1; psw=0x%02x; expected=0x%02x; ms_after_set=%u\r\n", unsigned(census.k[1].psw),
                           unsigned(m.stance_expected_psw), Clamp32(now - m.stance_set_ms));
            }
        }
    }
    // Stance counters keep counting after the line caps (Play-OfflineCoop's
    // status line and long sessions read this).
    if (m.stance_summary_dirty && (!m.stance_summary_ms || now - m.stance_summary_ms >= kStanceSummaryMs)) {
        m.stance_summary_dirty = false;
        m.stance_summary_ms = now;
        EmitFormat(h, "partner_stance=summary; last=%u; sets=%u; reqs=%u; refused=%u; reverts=%u\r\n", unsigned(m.stance_last),
                   m.stance_sets, m.stance_reqs, m.stance_refusals, m.stance_reverts);
    }
    // Census: census=1, or once per realize when a native feature needs it.
    const bool native_feature = transform || (set.anim != PartnerAnim::off && set.aset != PartnerAset::q);
    if (d.census_pending || (native_feature && parts_loaded && !m.census_logged)) {
        d.census_pending = false;
        if (parts_loaded) m.census_logged = true;
        LogCensus(h, st);
    }
    // All-active mode, as Lua reports it, and the 0x2000 re-assert.
    bool reassert = false;
    if (set.stage >= 2 && (tick.luaflags & partner::luaflag::all_active) && !(st.flags & c::flag_all_active) &&
        ResendDue(m.allactive_resends, m.allactive_resend_ms, now)) {
        reassert = true;
        ++m.allactive_resends;
        m.allactive_resend_ms = now;
        EmitFormat(h, "partner_allactive=lost; flags=0x%08x; reassert=%u\r\n", st.flags, m.allactive_resends);
    }
    if (tick.luaflags & partner::luaflag::all_active) {
        if (!m.all_active_ms) m.all_active_ms = now;
    } else {
        m.all_active_ms = 0;
    }
    // Appearance: verification of the last apply, then this tick's desired look.
    if (m.look_verify) {
        if (!realized || st.parts[1] != c::parts_loaded_state) {
            m.look_cycle_seen = true;
            m.look_match_ms = 0;
        } else if (looks_ok && LookMatches(looks.look[1], m.look_applied) && !(st.q180 & 2u)) {
            if (!m.look_match_ms) m.look_match_ms = now;
            if (m.look_cycle_seen || now - m.look_match_ms >= kLookSettleMs) {
                m.look_verify = false;
                const partner::Look& l = looks.look[1];
                EmitCapped(m.look_cap, h, "partner_look",
                           "partner_look=loaded; R=1; q40=%u; q48=%u; q50=%u; q58=%u; q60=%u; q68=%u; ms_after_apply=%u; cycle=%u\r\n",
                           unsigned(l.type), unsigned(l.parts), unsigned(l.camo), unsigned(l.hand), unsigned(l.face),
                           unsigned(l.face_equip), Clamp32(now - m.look_applied_ms), m.look_cycle_seen ? 1u : 0u);
            }
        } else {
            m.look_match_ms = 0;
        }
        if (m.look_verify && now - m.look_applied_ms > kPartsTimeoutMs) {
            m.look_verify = false;
            EmitCapped(m.look_cap, h, "partner_look", "partner_look=timeout; R=1; state=%u; q40=%u; q180=0x%x; realized=%u\r\n",
                       unsigned(st.parts[1]), looks_ok ? unsigned(looks.look[1].type) : 0xFFu, st.q180, realized ? 1u : 0u);
        }
    }
    const Desired desired = DesiredLook(d, h, looks_ok, looks, st);
    // Part L: SET_PARTS is safe only when the game will read Q again before it
    // builds the body (state 3/4, or unrealized/realized-state-0 with no look
    // yet). State 1 builds from the current Q and never completes a changed
    // look, so a look written there is deferred.
    const std::uint8_t ps = st.parts[1];
    const bool q_no_look = looks_ok && looks.look[1].parts == 0xFF;
    const bool look_write_safe = !set.look_gate
        ? true
        : realized ? (ps == c::parts_loaded_state || ps == 4 || (ps == 0 && q_no_look)) : (ps == 0 || ps == 3 || ps == 4);
    // Stall recovery (B): a realized instance stuck in state 1 with a changed
    // look gets the descriptor look re-sent so 0x9CB200 matches the loaded block.
    bool recover_now = false;
    partner::Look recover_look{};
    if (set.look_recover && set.look != PartnerLook::copy && realized && ps == 1 && (st.q180 & 2u) && st.pb_ok &&
        st.pb_flag[1] == 3 && st.req_ok && looks_ok &&
        (looks.look[1].type != st.req_look.type || looks.look[1].parts != st.req_look.parts)) {
        const std::uint32_t key = static_cast<std::uint32_t>(st.req_look.type) | (static_cast<std::uint32_t>(st.req_look.parts) << 8);
        if (!m.stall_since_ms || m.stall_key != key) { m.stall_since_ms = now; m.stall_key = key; m.stall_logged = false; }
        if (now - m.stall_since_ms >= kStallMs && !m.stall_logged) {
            m.stall_logged = true;
            EmitCapped(m.look_cap, h, "partner_look",
                       "partner_look=stall; R=1; state=1; q=%u,%u; req=%u,%u,%u,%u,%u,%u; q180=0x%x; held_ms=%u\r\n",
                       unsigned(looks.look[1].type), unsigned(looks.look[1].parts), unsigned(st.req_look.type),
                       unsigned(st.req_look.parts), unsigned(st.req_look.camo), unsigned(st.req_look.hand),
                       unsigned(st.req_look.face), unsigned(st.req_look.face_equip), st.q180, Clamp32(now - m.stall_since_ms));
            bool avatar = false;
            if (partner::NormalizeLook(st.req_look, recover_look, avatar)) {
                EmitCapped(m.look_cap, h, "partner_look", "partner_look=stall; R=1; recover=refused; reason=normalize\r\n");
            } else {
                recover_now = true;
            }
        }
    } else {
        m.stall_since_ms = 0;
    }
    if (set.look == PartnerLook::net && !desired.valid && m.all_active_ms && !m.look_waiting_logged && now - m.all_active_ms >= kLookWaitMs) {
        m.look_waiting_logged = true;
        EmitCapped(m.look_cap, h, "partner_look", "partner_look=waiting; R=1; ms_after_allactive=%u; fallback=copy\r\n",
                   Clamp32(now - m.all_active_ms));
    }
    bool set_parts = false;
    bool want_set = false;
    if (desired.valid) {
        if (set.look == PartnerLook::copy) {
            // parts=explicit with look=copy: the F3b one-shot (stage >= 1).
            want_set = set.stage >= 1;
        } else {
            want_set = set.stage >= 3 && (tick.luaflags & partner::luaflag::all_active) &&
                       (!SameKey(m, desired) || !(tick.luaflags & partner::luaflag::parts_copied)) &&
                       (!m.look_applied_any || now - m.look_applied_ms >= kLookMinGapMs);
        }
        set_parts = want_set && look_write_safe;
        if (want_set && !look_write_safe) {
            const std::uint32_t key = (static_cast<std::uint32_t>(desired.look.type) << 16) |
                                      (static_cast<std::uint32_t>(desired.look.parts) << 24) |
                                      (static_cast<std::uint32_t>(ps) << 8) | (q_no_look ? 1u : 0u);
            if (!m.look_deferred_logged || m.look_deferred_key != key) {
                m.look_deferred_logged = true;
                m.look_deferred_key = key;
                EmitCapped(m.look_cap, h, "partner_look", "partner_look=deferred; R=1; state=%u; realized=%u; q48=%u; rev=%u\r\n",
                           unsigned(ps), realized ? 1u : 0u, q_no_look ? 255u : unsigned(looks_ok ? looks.look[1].parts : 0), desired.forced_rev);
            }
        }
    }
    if (recover_now) set_parts = true;  // the recovery write bypasses the gate
    // A known explicit look replaces the copy; look=net withholds COPY until a
    // look arrives (or kLookWaitMs after all-active), so the first request
    // already uses the remote look (C).
    const bool copy_fallback_due = m.all_active_ms && now - m.all_active_ms >= kLookWaitMs;
    const bool no_copy = set.look != PartnerLook::copy && (desired.valid || (set.look_gate && !copy_fallback_due));
    if (set.look != PartnerLook::copy && !desired.valid && set.look_gate && !copy_fallback_due && set.stage >= 3 &&
        (tick.luaflags & partner::luaflag::all_active) && !m.copy_withheld_logged) {
        m.copy_withheld_logged = true;
        EmitCapped(m.look_cap, h, "partner_look", "partner_look=copy_withheld; R=1; wait_ms=5000\r\n");
    }
    // A fresh mirror=on request restarts the window instead of ending it.
    const bool mirror_off = !d.mirror_pending && MirrorOffDue(d, h, st, now);
    const std::uint32_t ops = partner::OpsFor(partner::OpInput{set.stage, set.apply, pose_valid, tick.luaflags, reassert, set_parts,
                                                               d.mirror_pending, mirror_off, no_copy, transform_warp, false, false});
    const partner::Look apply_look = recover_now ? recover_look : desired.look;
    const char* apply_source = recover_now ? "recover" : desired.source;
    if (ops & partner::op::set_parts_explicit) {
        if (!recover_now && set.look == PartnerLook::copy) d.parts_explicit_pending = false;
        if (h.write_face_equip) {
            const std::uint8_t fe = recover_now ? st.req_look.face_equip : desired.look.face_equip;
            if (const char* refused = h.write_face_equip(h.context, terminating, fe)) LogWriteRefusal(m, h, "face_equip", refused);
        }
        m.look_applied_any = true;
        // A recovery write leaves the desired key unmatched so A re-sends it.
        m.look_applied_remote_rev = recover_now ? 0xFFFFu : desired.remote_rev;
        m.look_applied_forced_rev = recover_now ? 0xFFFFFFFFu : desired.forced_rev;
        m.look_applied = apply_look;
        m.look_applied_ms = now;
        m.look_verify = true;
        m.look_cycle_seen = false;
        m.look_match_ms = 0;
        if (recover_now) m.stall_since_ms = 0;
        const partner::Look& l = apply_look;
        if (recover_now)
            EmitCapped(m.look_cap, h, "partner_look", "partner_look=stall_recover; R=1; type=%u; parts=%u; camo=%u; hand=%u; face=%u; fe=%u\r\n",
                       unsigned(l.type), unsigned(l.parts), unsigned(l.camo), unsigned(l.hand), unsigned(l.face),
                       unsigned(st.req_look.face_equip));
        EmitCapped(m.look_cap, h, "partner_look",
                   "partner_look=apply; R=1; source=%s; type=%u; parts=%u; camo=%u; hand=%u; face=%u; fe=%u; rev=%u; avatar_as_snake=%u\r\n",
                   apply_source, unsigned(l.type), unsigned(l.parts), unsigned(l.camo), unsigned(l.hand), unsigned(l.face),
                   unsigned(recover_now ? st.req_look.face_equip : desired.look.face_equip),
                   unsigned(recover_now ? 0u : desired.remote_rev), (!recover_now && desired.avatar_as_snake) ? 1u : 0u);
    }
    if (ops & partner::op::mirror_on) {
        d.mirror_pending = false;
        m.mirror_requested = true;
        m.mirror_seen = false;
        m.mirror_refused_logged = false;
        m.mirror_ms = now;
        m.mirror_off_sends = 0;
    }
    // Transitions are logged immediately (shared cap against realize thrash).
    char line[160]{};
    if (realized && !m.realized) {
        // hybrid placement, census and stance debounce were reset at mission
        // start or unrealize; this tick may already have used them.
        m.realized = true;
        m.realized_ms = now;
        m.parts_loaded = false;
        m.parts_timeout_logged = false;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_realized=1; R=1; id=%04x; pool=%02x; ms_after_bits=%u\r\n",
                unsigned(st.ids[1]), unsigned(st.pool[1]), m.bits_set ? Clamp32(now - m.bits_ms) : 0u))) LogTransition(m, h, line);
    } else if (!realized && m.realized) {
        m.realized = false;
        m.hybrid_placed = false;
        m.census_logged = false;
        m.dm_census_loaded = false;
        m.stance = partner::StanceDebounce{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_realized=0; R=1; ms_after_realize=%u\r\n",
                Clamp32(now - m.realized_ms)))) LogTransition(m, h, line);
    }
    if (m.realized && !m.parts_loaded) {
        if (st.parts[1] == c::parts_loaded_state) {
            m.parts_loaded = true;
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_parts=loaded; R=1; ms_after_realize=%u\r\n",
                    Clamp32(now - m.realized_ms)))) LogTransition(m, h, line);
        } else if (!m.parts_timeout_logged && now - m.realized_ms > kPartsTimeoutMs) {
            m.parts_timeout_logged = true;
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_parts=timeout; R=1; state=%u; pb_state=%u\r\n",
                    unsigned(st.parts[1]), st.pb_state[1]))) LogTransition(m, h, line);
        }
    }
    // 1 Hz: the pose commanded on the previous tick against the slot now.
    const bool applying = (ops & partner::op::warp) != 0 || slot;
    if (applying && m.have_cmd && now - m.warp_check_ms >= 1000) {
        m.warp_check_ms = now;
        const std::int32_t sx = pp::ToMm(st.slot[1][0]), sy = pp::ToMm(st.slot[1][1]), sz = pp::ToMm(st.slot[1][2]);
        EmitFormat(h, "partner_warp_check; R=1; apply=%s; cmd_mm=%d,%d,%d; slot_mm=%d,%d,%d; err_mm=%u\r\n",
                   PartnerApplyName(set.apply), m.cmd.x_mm, m.cmd.y_mm, m.cmd.z_mm, sx, sy, sz,
                   partner::DistanceMm(m.cmd.x_mm, m.cmd.y_mm, m.cmd.z_mm, sx, sy, sz));
    }
    // Sender: the local pose with its anim extension, and the local look once loaded.
    if (h.submit_local) h.submit_local(h.context, tick, local_anim);
    if (h.submit_look && looks_ok && st.local < 2 && st.parts[st.local] == c::parts_loaded_state)
        h.submit_look(h.context, looks.look[st.local]);
    Probe(d, h, st, clock);
    if (lab.partner_anim_probe) AnimProbe(m, h, st, anim_ok, anim, now);
    // Both looks, change-only at most 1 Hz, while an explicit look is used.
    const bool looks_changed = !partner::SameLook(looks.look[0], m.looks.look[0]) || !partner::SameLook(looks.look[1], m.looks.look[1]);
    if (looks_ok && (set.look != PartnerLook::copy || lab.partner_anim_probe) &&
        (!m.looks_logged || (looks_changed && now - m.looks_ms >= 1000)) && m.probe_lines < kProbeLineCap) {
        m.looks_logged = true;
        m.looks_ms = now;
        m.looks = looks;
        ++m.probe_lines;
        const partner::Look& a = looks.look[0];
        const partner::Look& b = looks.look[1];
        EmitFormat(h, "partner_looks; look0=%u,%u,%u,%u,%u,%u; look1=%u,%u,%u,%u,%u,%u; parts=%u,%u\r\n", unsigned(a.type),
                   unsigned(a.parts), unsigned(a.camo), unsigned(a.hand), unsigned(a.face), unsigned(a.face_equip), unsigned(b.type),
                   unsigned(b.parts), unsigned(b.camo), unsigned(b.hand), unsigned(b.face), unsigned(b.face_equip), unsigned(st.parts[0]),
                   unsigned(st.parts[1]));
    }
    // probe=ps: the per-id PS fields of both instances at 4 Hz (read-only).
    if (set.probe_ps && h.format_ps_probe && now - m.ps_probe_ms >= kPsProbeMs && m.ps_probe_lines < kProbeLineCap) {
        m.ps_probe_ms = now;
        for (std::uint32_t k = 0; k < 2; ++k) {
            char ps_line[512]{};
            if (h.format_ps_probe(h.context, st, k, ps_line, sizeof(ps_line))) {
                Emit(h, ps_line);
                ++m.ps_probe_lines;
            }
        }
    }
    if (applying || (transform && pose_valid && set.stage >= 1)) {
        m.cmd = pose;
        m.have_cmd = true;
        m.pose_applied = true;
    }
    // Round 3: input lever (arming, placement, correction, sender, logs) and
    // the direct-motion lever. Either may add op bits and the DM gani.
    // TickInput runs first: a trigger that turns input off and dm on (with a
    // dmplay) disarms here, so TickDm can patch and play in the same tick.
    std::uint32_t extra_ops = 0;
    std::uint8_t gani = 0;
    TickInput(d, h, lab, st, set, tick, clock, terminating, parts_loaded, pose_valid, pose, relink, extra_ops);
    TickDm(d, h, st, set, clock, terminating, extra_ops, gani);
    // apply=input without an armed window falls back to the proven Lua Warp
    // every tick (refused, faulted, install refused, input=off, stage < 3).
    if (set.apply == PartnerApply::input && !m.input_armed && pose_valid && set.stage >= 1) {
        if (!(extra_ops & partner::op::warp)) {
            extra_ops |= partner::op::warp;
            if (!m.input_fallback) {
                m.input_fallback = true;
                EmitInput(m, h, "partner_input_place; reason=fallback; err_mm=0; via=warp\r\n");
            }
        }
    } else if (m.input_armed) {
        m.input_fallback = false;
    }
    reply = partner::Reply{ops | extra_ops, partner::partner_index, pose.x_mm, pose.y_mm, pose.z_mm, pose.yaw_mrad, status, age_ms,
                           (ops & partner::op::set_parts_explicit) ? apply_look : partner::Look{}, gani};
    return true;
}

} // namespace mgscoop::partner_driver
