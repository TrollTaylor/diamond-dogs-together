// Original MGS Co-op offline partner spike: receipt state machine.
// docs/offline-partner-spike-plan.md 2.2, 2.3 item 9, 2.6, 2.7, and the
// animation / movement / appearance experiments (docs/partner-anim-appearance.md).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
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
    d.look_forced_rev = 1;
}

void MissionStart(Driver& d, const Hooks& h) noexcept {
    d.mission = Mission{};
    d.mission.anim_source = -1;
    d.mission.last_status = pose::Status::none;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
    if (!h.take_trigger) return;
    char text[partner::max_trigger + 1]{};
    std::size_t length = 0;
    if (h.take_trigger(h.context, text, partner::max_trigger, length) != TriggerRead::none)
        Emit(h, "partner_trigger=discarded; reason=stale\r\n");
}

void MissionEnd(Driver& d) noexcept {
    d.mission.cfg_ready = false;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
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
    if (teardown == PartnerTeardown::clear && ClearBits(d, h, "stop", terminating)) cleared = 1;
    RestorePolicy(d, h, terminating);
    m.stopped = true;
    m.cfg_ready = false;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
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
    d.settings.stage = stage;
    EmitFormat(h, "partner_stage=changed; stage=%u; source=%s; apply=%s; anim=%s; aset=%s; reset=%s; look=%s; probe=%s; via=trigger\r\n",
               stage, PartnerSourceName(d.settings.source), PartnerApplyName(d.settings.apply), PartnerAnimName(d.settings.anim),
               PartnerAsetName(d.settings.aset), PartnerResetName(d.settings.reset), PartnerLookName(d.settings.look),
               d.settings.probe_ps ? "ps" : "off");
    if (trigger.census) d.census_pending = true;
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
    if (transform && parts_loaded && h.read_body) {
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
            else if (set.source == PartnerSource::net && m.last_status == pose::Status::lost && sample_status == pose::Status::live)
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
    if (set.look == PartnerLook::net && !desired.valid && m.all_active_ms && !m.look_waiting_logged && now - m.all_active_ms >= kLookWaitMs) {
        m.look_waiting_logged = true;
        EmitCapped(m.look_cap, h, "partner_look", "partner_look=waiting; R=1; ms_after_allactive=%u; fallback=copy\r\n",
                   Clamp32(now - m.all_active_ms));
    }
    bool set_parts = false;
    if (desired.valid) {
        if (set.look == PartnerLook::copy) {
            // parts=explicit with look=copy: the F3b one-shot (stage >= 1).
            set_parts = set.stage >= 1;
        } else {
            set_parts = set.stage >= 3 && (tick.luaflags & partner::luaflag::all_active) &&
                        (!SameKey(m, desired) || !(tick.luaflags & partner::luaflag::parts_copied)) &&
                        (!m.look_applied_any || now - m.look_applied_ms >= kLookMinGapMs);
        }
    }
    // A known explicit look replaces the copy (the copy stays the fallback
    // while look=net has not received one).
    const bool no_copy = set.look != PartnerLook::copy && desired.valid;
    // A fresh mirror=on request restarts the window instead of ending it.
    const bool mirror_off = !d.mirror_pending && MirrorOffDue(d, h, st, now);
    const std::uint32_t ops = partner::OpsFor(partner::OpInput{set.stage, set.apply, pose_valid, tick.luaflags, reassert, set_parts,
                                                               d.mirror_pending, mirror_off, no_copy, transform_warp});
    if (ops & partner::op::set_parts_explicit) {
        if (set.look == PartnerLook::copy) d.parts_explicit_pending = false;
        if (h.write_face_equip) {
            if (const char* refused = h.write_face_equip(h.context, terminating, desired.look.face_equip))
                LogWriteRefusal(m, h, "face_equip", refused);
        }
        m.look_applied_any = true;
        m.look_applied_remote_rev = desired.remote_rev;
        m.look_applied_forced_rev = desired.forced_rev;
        m.look_applied = desired.look;
        m.look_applied_ms = now;
        m.look_verify = true;
        m.look_cycle_seen = false;
        m.look_match_ms = 0;
        const partner::Look& l = desired.look;
        EmitCapped(m.look_cap, h, "partner_look",
                   "partner_look=apply; R=1; source=%s; type=%u; parts=%u; camo=%u; hand=%u; face=%u; fe=%u; rev=%u; avatar_as_snake=%u\r\n",
                   desired.source, unsigned(l.type), unsigned(l.parts), unsigned(l.camo), unsigned(l.hand), unsigned(l.face),
                   unsigned(l.face_equip), unsigned(desired.remote_rev), desired.avatar_as_snake ? 1u : 0u);
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
    reply = partner::Reply{ops, partner::partner_index, pose.x_mm, pose.y_mm, pose.z_mm, pose.yaw_mrad, status, age_ms,
                           (ops & partner::op::set_parts_explicit) ? desired.look : partner::Look{}};
    return true;
}

} // namespace mgscoop::partner_driver
