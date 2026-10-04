// Original MGS Co-op offline partner spike: receipt state machine.
// docs/offline-partner-spike-plan.md 2.2, 2.3 item 9, 2.6, 2.7.
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

void AnimProbe(Mission& m, const Hooks& h, const pp::State& st, std::uint64_t now) noexcept {
    if (m.anim_logged && now - m.anim_ms < 100) return;
    pp::Anim anim{};
    if (!h.read_anim || !h.read_anim(h.context, st, anim)) return;
    const pp::Anim& last = m.anim;
    if (m.anim_logged && anim.q18 == last.q18 && anim.q28 == last.q28 && anim.qd8 == last.qd8 && anim.q30 == last.q30 &&
        anim.q38 == last.q38 && anim.m1d8 == last.m1d8 && anim.m1e0 == last.m1e0 && anim.m1f8 == last.m1f8) return;
    m.anim = anim;
    m.anim_logged = true;
    m.anim_ms = now;
    EmitFormat(h, "partner_anim; q18=%04x; q28=%02x; qd8=%04x; q30=%u; q38=%u; m1d8=%08x; m1e0=%08x; m1f8=%08x\r\n",
               unsigned(anim.q18), unsigned(anim.q28), unsigned(anim.qd8), anim.q30, anim.q38, anim.m1d8, anim.m1e0, anim.m1f8);
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
} // namespace

void Configure(Driver& d, const LabConfig& lab) noexcept {
    d = Driver{};
    d.settings.stage = lab.partner_stage;
    d.settings.source = lab.partner_source;
    d.settings.apply = lab.partner_apply;
}

void MissionStart(Driver& d, const Hooks& h) noexcept {
    d.mission = Mission{};
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
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
    EmitFormat(h, "partner_stop; cleared=%u; teardown=%s\r\n", cleared, PartnerTeardownName(teardown));
}

void ApplyTrigger(Driver& d, const Hooks& h, const partner::Trigger& trigger, bool terminating) noexcept {
    Mission& m = d.mission;
    const std::uint32_t stage = trigger.stop ? 0 : trigger.has_stage ? trigger.stage : d.settings.stage;
    if (trigger.has_source && trigger.source != d.settings.source) m.have_cmd = false;
    if (trigger.has_source) d.settings.source = trigger.source;
    if (trigger.has_apply) d.settings.apply = trigger.apply;
    d.settings.stage = stage;
    EmitFormat(h, "partner_stage=changed; stage=%u; source=%s; apply=%s; via=trigger\r\n", stage,
               PartnerSourceName(d.settings.source), PartnerApplyName(d.settings.apply));
    if (stage >= 1) {
        if (trigger.parts_explicit) d.parts_explicit_pending = true;
        if (trigger.mirror_on) d.mirror_pending = true;
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
    // Pose source.
    partner::Pose pose{};
    bool pose_valid = false;
    partner::Status status = partner::Status::synthetic;
    std::uint32_t age_ms = 0;
    if (set.source == PartnerSource::net) {
        const pose::Sample sample = h.sample_remote ? h.sample_remote(h.context, lab.partner_interp_ms) : pose::Sample{};
        status = sample.status == pose::Status::live ? partner::Status::live
               : sample.status == pose::Status::stale ? partner::Status::stale : partner::Status::lost;
        if (sample.valid) {
            pose = partner::PoseFromMetres(sample.x, sample.y, sample.z, sample.yaw);
            pose_valid = true;
            age_ms = sample.age_ms;
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
    const bool slot = partner::WritesSlot(set.stage, set.apply, pose_valid);
    if (slot) {
        const float xyzw[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                               static_cast<float>(pose.z_mm / 1000.0), static_cast<float>(pose.yaw_mrad / 1000.0)};
        if (const char* refused = h.write_slot(h.context, terminating, xyzw)) LogWriteRefusal(m, h, "slot", refused);
    }
    // All-active: Lua latched it, but P+0x204 bit 0x2000 is clear again.
    bool reassert = false;
    if (set.stage >= 2 && (tick.luaflags & partner::luaflag::all_active) && !(st.flags & c::flag_all_active) &&
        ResendDue(m.allactive_resends, m.allactive_resend_ms, now)) {
        reassert = true;
        ++m.allactive_resends;
        m.allactive_resend_ms = now;
        EmitFormat(h, "partner_allactive=lost; flags=0x%08x; reassert=%u\r\n", st.flags, m.allactive_resends);
    }
    // A fresh mirror=on request restarts the window instead of ending it.
    const bool mirror_off = !d.mirror_pending && MirrorOffDue(d, h, st, now);
    const std::uint32_t ops = partner::OpsFor(partner::OpInput{set.stage, set.apply, pose_valid, tick.luaflags, reassert,
                                                               d.parts_explicit_pending, d.mirror_pending, mirror_off});
    if (ops & partner::op::set_parts_explicit) d.parts_explicit_pending = false;
    if (ops & partner::op::mirror_on) {
        d.mirror_pending = false;
        m.mirror_requested = true;
        m.mirror_seen = false;
        m.mirror_refused_logged = false;
        m.mirror_ms = now;
        m.mirror_off_sends = 0;
    }
    // Transitions are logged immediately (shared cap against realize thrash).
    const bool realized = pp::Realized(st, partner::partner_index);
    char line[160]{};
    if (realized && !m.realized) {
        m.realized = true;
        m.realized_ms = now;
        m.parts_loaded = false;
        m.parts_timeout_logged = false;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_realized=1; R=1; id=%04x; pool=%02x; ms_after_bits=%u\r\n",
                unsigned(st.ids[1]), unsigned(st.pool[1]), m.bits_set ? Clamp32(now - m.bits_ms) : 0u))) LogTransition(m, h, line);
    } else if (!realized && m.realized) {
        m.realized = false;
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
    if (h.submit_local) h.submit_local(h.context, tick);
    Probe(d, h, st, clock);
    if (lab.partner_anim_probe) AnimProbe(m, h, st, now);
    if (applying) {
        m.cmd = pose;
        m.have_cmd = true;
        m.pose_applied = true;
    }
    reply = partner::Reply{ops, partner::partner_index, pose.x_mm, pose.y_mm, pose.z_mm, pose.yaw_mrad, status, age_ms};
    return true;
}

} // namespace mgscoop::partner_driver
