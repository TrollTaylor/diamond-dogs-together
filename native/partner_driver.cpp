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
#include "mgscoop/partner_upper_probe.hpp"
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

namespace pnat = mgscoop::partner_native;

// Native puppet (docs/native-puppet.md 3.3): the settings that take effect
// this tick. While the arbiter is entering or active the native drive's
// fixed values replace the configured ones; never written to lab.ini.
Settings EffectiveSettings(const Settings& base, bool active) noexcept {
    Settings e = base;
    e.defer = EffectivePartnerDefer(base.drive, base.defer);
    e.native_active = active && base.drive == PartnerDrive::native;
    if (!e.native_active) return e;
    pnat::DriveSettings ds{};
    ds.apply = base.apply;
    ds.input = base.input;
    ds.dm = base.dm;
    ds.anim = base.anim;
    ds.wrap = base.wrap;
    ds.hold = base.input_hold;
    for (unsigned i = 0; i < 3; ++i) ds.words[i] = base.input_words[i];
    ds.cam = base.input_cam;
    ds.recon = base.recon;
    ds.slot = base.slot;
    ds.p218 = false;
    ds.mask = base.input_mask;
    const pnat::DriveSettings x = pnat::Effective(ds, true, pnat::NativeConfig{base.ngun, base.nfire, base.naim});
    e.apply = x.apply;
    e.input = x.input;
    e.dm = x.dm;
    e.anim = x.anim;
    e.wrap = x.wrap;
    e.input_hold = x.hold;
    for (unsigned i = 0; i < 3; ++i) e.input_words[i] = x.words[i];
    e.input_cam = x.cam;
    e.recon = x.recon;
    e.slot = x.slot;
    e.input_mask = x.mask;
    if (!e.correct_mm) e.correct_mm = 3000;  // correct=0 would turn the reconciliation off
    return e;
}

// The sender's v3 fields (this copy runs the native drive). PP+0x14 and the
// camera are swapped inside windows: only an even, unchanged WindowSeq counts.
pin::V3Source SenderV3(const Driver& d, const Hooks& h, const pp::State& st, const pin::PadSnapshot& snap) noexcept {
    (void)d;
    pin::V3Source v{};
    v.inv_slot = pin::kInvSlotUnknown;
    v.aiming = (snap.held & 0x8u) != 0;  // HOLD level
    if (!h.read_look_source) return v;
    const std::uint32_t seq0 = h.window_seq ? h.window_seq(h.context) : 0;
    pp::LookSource src{};
    if (!h.read_look_source(h.context, st, src)) return v;
    const std::uint32_t seq1 = h.window_seq ? h.window_seq(h.context) : 0;
    const bool quiet = !(seq0 & 1u) && seq0 == seq1;
    v.look_valid = src.look_ok;
    v.look_pitch = src.pitch;
    v.look_yaw = src.yaw;
    v.cam_valid = src.cam_ok && quiet;
    v.cam270 = src.cam270;
    v.cam274 = src.cam274;
    v.heading_valid = src.heading_ok && quiet;
    v.heading_rad = src.heading;
    return v;
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
    a.wrap = set.wrap;
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
    // Native puppet: native window rules, isLocal policy, the look override's
    // slot (only with a validated look slot), the GO identity and steering.
    a.native = set.native_active;
    a.local_all = set.native_active && set.local == PartnerLocal::all;
    a.naim = set.native_active ? set.naim : PartnerNativeAim::off;
    a.go = set.native_active && m.native_targets_ok ? m.native_targets.go : 0;
    const bool look_slot = set.native_active && m.native_targets_ok && !m.native_targets.look_refusal;
    a.ps1 = look_slot ? m.native_targets.ps1 : 0;
    a.ps1_idx = look_slot ? m.native_targets.ps1_idx : 0;
    if (!look_slot) a.naim = PartnerNativeAim::off;
    a.steer_rad = set.native_active ? m.steer_rad : 0.0;
    a.mag_scale = set.native_active && m.mag_scale > 0.0 ? m.mag_scale : 1.0;
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

namespace pdm = mgscoop::partner_dmmap;
static_assert(partner::max_gani == kPartnerDmGaniMax, "GANI bound");

// dm=map event families (one 64-line cap each).
enum DmMapFamily : unsigned { kDmDeferred, kDmLost, kDmStopTimeout, kDmUnconsumed, kDmAdopted, kDmAbandoned, kDmBadGani, kDmFamilies };
static_assert(kDmFamilies == sizeof(Mission::dmmap_cap) / sizeof(LogCap), "one cap per dm=map family");
const char* DmMapFamilyName(unsigned family) noexcept {
    static const char* const names[] = {"deferred", "lost", "stop_timeout", "unconsumed", "adopted", "abandoned", "bad_gani"};
    return family < kDmFamilies ? names[family] : "other";
}
void EmitDmMap(Mission& m, const Hooks& h, unsigned family, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    EmitCappedV(m.dmmap_cap[family < kDmFamilies ? family : kDmDeferred], h, "partner_dmmap", DmMapFamilyName(family), format, arguments);
    va_end(arguments);
}
// partner_dmmap=switch / =playing share one counter (kDmMapSwitchCap).
void EmitDmMapSwitch(Mission& m, const Hooks& h, const char* format, ...) noexcept {
    if (m.dmmap_switch_lines >= kDmMapSwitchCap) {
        if (!m.dmmap_switch_capped) {
            m.dmmap_switch_capped = true;
            EmitFormat(h, "partner_dmmap=capped; limit=%u; kind=switch\r\n", kDmMapSwitchCap);
        }
        return;
    }
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) {
        ++m.dmmap_switch_lines;
        Emit(h, line);
    }
}
// A gani index as logged: the number, or "none" (kNoGani).
struct GaniText {
    char text[8];
};
GaniText Gani(std::uint8_t gani) noexcept {
    GaniText out{};
    if (gani == pdm::kNoGani) StringCchCopyA(out.text, ARRAYSIZE(out.text), "none");
    else StringCchPrintfA(out.text, ARRAYSIZE(out.text), "%u", unsigned(gani));
    return out;
}
void LogDmMapTable(const Settings& set, const Hooks& h, const char* reason) noexcept {
    char cells[40]{};
    char speeds[24]{};
    FormatPartnerDmMap(set.dm_map, cells);
    FormatPartnerDmSpeeds(set.dm_speed, speeds);
    EmitFormat(h, "partner_dmmap=table; cells=%s; speeds=%s; reason=%s\r\n", cells, speeds, reason);
}

// Reverts the DM patch (if ours) and drops pending play state; logs the reason.
// A mapped clip still ours is abandoned (left looping, as dm=on leaves one).
void RevertDm(Driver& d, const Hooks& h, const char* reason) noexcept {
    partner_upper_probe::Clear(reason);
    Mission& m = d.mission;
    if (m.dmmap.owned) {
        EmitDmMap(m, h, kDmAbandoned, "partner_dmmap=abandoned; reason=%s; gani=%s\r\n", reason, Gani(m.dmmap.cur).text);
        pdm::Forget(m.dmmap);
    }
    if (m.dm_patched && h.dm_patch) {
        if (m.dm_play_pending && h.dm_clear_pending && h.dm_clear_pending(h.context)) Emit(h, "partner_dm=cleared_pending\r\n");
        m.dm_patched = h.dm_patch(h.context, false, reason);
        if (m.dm_patched) m.dm_revert_failed = true;
    }
    m.dm_play_pending = false;
}

unsigned DmMotionCount(const Hooks& h) noexcept {
    const unsigned count = h.dm_motion_count ? h.dm_motion_count(h.context) : 13;
    return count <= kPartnerDmGaniMax + 1 ? count : 0;
}

// dm=map: one step of the mapper (partner_dmmap) and its log lines. Runs while
// dm=map, and after it until the release finished. `tick_ops` are this reply's
// ops so far (look and Warp gates); the play/stop go into `ops`.
void TickDmMap(Driver& d, const Hooks& h, const Settings& set, const Clock& clock, bool terminating, const partner::Tick& tick,
               const pdm::Observation& obs, std::uint32_t tick_ops, bool dm_ok, const pp::DmState& dm, bool parts_loaded,
               std::uint32_t& ops, std::uint8_t& gani) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    const bool map = set.dm == PartnerDm::map;
    if (map && !m.dmmap_table_logged) {
        m.dmmap_table_logged = true;
        LogDmMapTable(set, h, "start");
    }
    if (d.dmmap_rearm) {
        d.dmmap_rearm = false;
        pdm::Rearm(m.dmmap);
        m.dmmap_turn = {};
        m.dmmap_source_turns = {};
    }
    pdm::In in{};
    in.map = map;
    in.blocked = terminating                            ? pdm::Block::terminating
               : (m.native_disabled || m.input_faulted) ? pdm::Block::fault
               : set.stage < 3                          ? pdm::Block::stage
               : !PartnerApplyIsTransform(set.apply)    ? pdm::Block::apply
               : set.anim != PartnerAnim::off           ? pdm::Block::anim
               : m.input_armed                          ? pdm::Block::input
                                                        : pdm::Block::none;
    // The class: lost keeps the stance (idle cell), stale freezes the candidate.
    const pdm::State& s = m.dmmap;
    const std::uint8_t prev_move = s.cand == pdm::kNoGani ? pdm::kMoveUnknown : s.cand_cls.move;
    in.target_ok = true;
    if (!obs.lost && !obs.fresh) {
        in.target_ok = false;
        in.cls = s.cand_cls;
        in.cls.via = pdm::Via::stale;
    } else {
        in.cls = pdm::Classify(obs, set.dm_speed, prev_move, s.cand_cls.stance);
    }
    const unsigned motion_count = DmMotionCount(h);
    in.available_ganis = static_cast<std::uint8_t>(motion_count);
    const std::uint32_t unavailable = ~((1u << motion_count) - 1u);
    in.target = pdm::ResolveTarget(set.dm_map, in.cls, s.bad | unavailable);
    in.responsive = obs.flag_move <= 3 || obs.sprint || obs.gun_actions != 0;
    in.measured = set.source == PartnerSource::net && obs.fresh && !obs.lost && obs.speed_ok && in.cls.via == pdm::Via::speed;
    // Retail stationary body yaw turns while aiming. Aim alone can coexist
    // with lower-body pivots; firing/reloading and every other action still
    // interrupt their collector and pending finite requests.
    const bool gun_busy = (obs.gun_actions & ~sync::ActionFlag::Aiming) != 0;
    in.stance_only_urgent = pdm::StanceUrgency(s, in.cls, gun_busy);
    in.urgent = in.cls.move != 0 || gun_busy || in.stance_only_urgent;
    const bool turn_allowed = motion_count >= 21 && map && in.blocked == pdm::Block::none && parts_loaded && obs.fresh && !obs.lost;
    std::uint8_t turn = pdm::kNoGani;
    std::uint32_t turn_token = 0;
    if (set.source == PartnerSource::net && h.sample_remote_with_turns) {
        const auto& frame = m.dmmap_source_frame;
        const bool body_transition = m.look_verify || (tick_ops & (partner::op::set_parts_explicit |
            partner::op::warp | partner::op::set_all_active)) != 0 ||
            ((tick_ops & partner::op::copy_parts) && !(tick.luaflags & partner::luaflag::parts_copied));
        const auto result = partner_source_turns::Step(m.dmmap_source_turns, m.dmmap_turn, frame,
            frame.now_ms > now ? frame.now_ms : now, turn_allowed && !body_transition,
            set.dm_speed, in.cls.stance, in.cls.move == 0 && !gun_busy);
        if (result.cancel_queue) pdm::CancelFiniteEvents(m.dmmap);
        in.urgent = in.urgent || result.busy;
        if (result.busy) in.stance_only_urgent = false;
        turn = result.event.gani;
        turn_token = result.event.token;
        if (turn_token) EmitDmMapSwitch(m, h,
            "partner_dmmap=source_turn; gani=%u; token=%u; generation=%u; sender_ms=%lld; receive_ms=%llu; owner_ms=%llu; age_ms=%u\r\n",
            unsigned(turn), unsigned(turn_token), unsigned(result.event.generation), static_cast<long long>(result.event.sender_ms),
            static_cast<unsigned long long>(result.event.receive_ms), static_cast<unsigned long long>(now),
            Clamp32((frame.now_ms > now ? frame.now_ms : now) - result.event.receive_ms));
    } else {
        turn = pdm::ObserveTurn(m.dmmap_turn, now, turn_allowed, obs.yaw, in.cls.stance, in.cls.move != 0 || gun_busy);
        turn_token = m.dmmap_turn.event_token;
    }
    if (turn != pdm::kNoGani && !(s.bad & (1u << turn))) {
        in.target = turn;
        in.finite_token = turn_token;
        in.responsive = true;
    }
    in.dm_ok = dm_ok;
    in.patched = m.dm_patched;
    in.parts_loaded = parts_loaded;
    in.in_dm = dm_ok && dm.q18[1] == c::q18_dm_act;
    in.host_dm = dm_ok && dm.q18[0] == c::q18_dm_act;
    in.pending = dm_ok && (dm.g388 & 0x3C) != 0;
    in.our_pending = m.dm_play_pending;
    in.cam_idle = dm_ok && dm.cam_ok && dm.cam1f0 == 0 && dm.cam200 == 0;
    in.look_busy = (tick_ops & partner::op::set_parts_explicit) != 0 ||
                   ((tick_ops & partner::op::copy_parts) && !(tick.luaflags & partner::luaflag::parts_copied)) || m.look_verify;
    in.allactive = (tick_ops & partner::op::set_all_active) != 0;
    in.warp = (tick_ops & partner::op::warp) != 0;
    in.prev_look = m.dmmap_prev_look;
    in.prev_warp = m.dmmap_prev_warp;
    in.now_ms = now;
    const pdm::Out out = pdm::Step(m.dmmap, in);
    if (out.action == pdm::Action::play) {
        ops |= partner::op::dm_play;
        gani = out.gani;
        m.dm_play_pending = true;
        m.dm_play_ms = now;
        m.dm_pending_gani = out.gani;
    } else if (out.action == pdm::Action::stop) {
        ops |= partner::op::dm_stop;
    }
    const int speed = obs.speed_ok ? static_cast<int>(obs.speed_mmps > 0x7FFFFFFFu ? 0x7FFFFFFFu : obs.speed_mmps) : -1;
    switch (out.event) {
    case pdm::Event::step:
        EmitDmMapSwitch(m, h,
                        "partner_dmmap=switch; step=%s; from=%s; to=%s; stance=%u; move=%u; speed_mmps=%d; via=%s; stance_via=%s; "
                        "wait_ms=%u\r\n",
                        out.step_stop ? "stop" : "play", Gani(out.from).text, Gani(out.gani).text, unsigned(in.cls.stance),
                        unsigned(in.cls.move), speed, pdm::ViaName(in.cls.via), pdm::ViaName(in.cls.stance_via), out.ms);
        break;
    case pdm::Event::playing:
        EmitDmMapSwitch(m, h, "partner_dmmap=playing; gani=%s; after_ms=%u\r\n", Gani(out.gani).text, out.ms);
        break;
    case pdm::Event::deferred:
        EmitDmMap(m, h, kDmDeferred, "partner_dmmap=deferred; reason=%s; phase=%s; want=%s; cur=%s\r\n", out.reason,
                  pdm::PhaseName(s.phase), Gani(s.cand).text, Gani(s.cur).text);
        break;
    case pdm::Event::stop_timeout:
    case pdm::Event::dm_read:
        EmitDmMap(m, h, kDmStopTimeout, "partner_dmmap=stop_timeout; gani=%s; ms=%u; failures=%u; backoff_ms=%u; reason=%s\r\n",
                  Gani(out.gani).text, out.ms, s.failures, s.backoff_ms, out.reason);
        break;
    case pdm::Event::unconsumed:
        EmitDmMap(m, h, kDmUnconsumed, "partner_dmmap=unconsumed; gani=%s; ms=%u; failures=%u; backoff_ms=%u\r\n", Gani(out.gani).text,
                  out.ms, s.failures, s.backoff_ms);
        break;
    case pdm::Event::lost:
        EmitDmMap(m, h, kDmLost, "partner_dmmap=lost; gani=%s; played_ms=%u; cause=%s; failures=%u\r\n", Gani(out.gani).text, out.ms,
                  pdm::CauseName(out.cause), s.failures);
        break;
    case pdm::Event::finished:
        EmitDmMapSwitch(m, h, "partner_dmmap=finished; gani=%s; played_ms=%u; once=1\r\n", Gani(out.gani).text, out.ms);
        break;
    case pdm::Event::failed:
        EmitFormat(h, "partner_dmmap=failed; failures=%u; last=%s\r\n", s.failures, out.reason ? out.reason : "unknown");
        break;
    case pdm::Event::adopted:
        EmitDmMap(m, h, kDmAdopted, "partner_dmmap=adopted; reason=active_clip\r\n");
        break;
    case pdm::Event::released:
        EmitFormat(h, "partner_dmmap=released; result=%s; to=%s; ms=%u\r\n", out.reason ? out.reason : "unknown", PartnerDmName(set.dm),
                   out.ms);
        break;
    default:
        break;
    }
    if (out.bad_gani != pdm::kNoGani)
        EmitDmMap(m, h, kDmBadGani, "partner_dmmap=bad_gani; gani=%u; reason=%s; strikes=%u\r\n", unsigned(out.bad_gani),
                  out.event == pdm::Event::lost ? "ended" : "unconsumed", unsigned(s.strikes[out.bad_gani]));
    // Summary (the counters keep counting after the line caps).
    if (!m.dmmap_summary_ms || now - m.dmmap_summary_ms >= kDmMapSummaryMs) {
        m.dmmap_summary_ms = now;
        if (m.dmmap_summary_lines < kDmMapSummaryCap) {
            ++m.dmmap_summary_lines;
            EmitFormat(h,
                       "partner_dmmap; phase=%s; cur=%s; want=%s; stance=%u; move=%u; via=%s; speed_mmps=%d; switches=%u; plays=%u; "
                       "stops=%u; deferrals=%u; timeouts=%u; unconsumed=%u; lost=%u; adopted=%u; bad=0x%x; failures=%u; deferred=%s\r\n",
                       pdm::PhaseName(s.phase), Gani(s.cur).text, Gani(s.cand).text, unsigned(in.cls.stance), unsigned(in.cls.move),
                       pdm::ViaName(in.cls.via), speed, s.switches, s.plays, s.stops, s.deferrals, s.timeouts, s.unconsumed, s.lost,
                       s.adopted, unsigned(s.bad), s.failures, s.deferred ? s.deferred : "none");
        } else if (!m.dmmap_summary_capped) {
            m.dmmap_summary_capped = true;
            EmitFormat(h, "partner_dmmap=capped; limit=%u; kind=summary\r\n", kDmMapSummaryCap);
        }
    }
}

void TickDm(Driver& d, const Hooks& h, const pp::State& st, const Settings& set, const Clock& clock, bool terminating,
            const partner::Tick& tick, const pdm::Observation& obs, std::uint32_t tick_ops, std::uint32_t& ops,
            std::uint8_t& gani) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    const bool map = set.dm == PartnerDm::map;
    // dm=off: revert at once, unless a mapped clip is still ours (then the
    // mapper releases it first, below, and the patch is reverted after).
    const bool releasing = set.dm == PartnerDm::off && m.dm_patched && m.dmmap.owned && h.dm_targets;
    if (set.dm == PartnerDm::off && !releasing) {
        if (m.dm_patched && !m.dm_revert_failed) RevertDm(d, h, "trigger");
        pdm::Idle(m.dmmap);
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
    // dm=on|map: patch once stage 3, parts loaded and input off all hold (the
    // two levers are mutually exclusive: an armed window or input!=off
    // refuses). No patch after a fault, and a failed patch is not retried
    // until the next dm= trigger.
    if ((set.dm == PartnerDm::on || map) && !m.dm_patched) {
        const char* refused = (m.input_armed || set.input != PartnerInput::off) ? "input_active"
                            : m.native_disabled                                  ? "native_disabled"
                            : m.dm_patch_failed                                  ? "patch_failed"
                                                                                 : nullptr;
        if (refused) {
            if (m.dm_refused_state != refused) {
                m.dm_refused_state = refused;
                EmitFormat(h, "partner_dm=refused; reason=%s\r\n", refused);
            }
        } else {
            m.dm_refused_state = nullptr;
            if (set.stage >= 3 && parts_loaded && !terminating && h.dm_patch) {
                m.dm_patched = h.dm_patch(h.context, true, "trigger");
                if (!m.dm_patched) m.dm_patch_failed = true;
            }
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
    // dm=map, and the release of its clip after map was turned off.
    if (map || m.dmmap.phase != pdm::Phase::idle)
        TickDmMap(d, h, set, clock, terminating, tick, obs, tick_ops, dm_ok, dm, parts_loaded, ops, gani);
    if (set.dm == PartnerDm::off) {
        // The release finished (or gave up): now the patch goes.
        if (!m.dmmap.owned && m.dmmap.phase == pdm::Phase::idle && m.dm_patched && !m.dm_revert_failed) RevertDm(d, h, "trigger");
        d.dmplay_pending = d.dmstop_pending = false;
        return;
    }
    // The mapper owns the DM: manual one-shots are refused (one reply never
    // carries both 128 and 256).
    if (map || m.dmmap.phase != pdm::Phase::idle) {
        const char* reason = map ? "map" : "map_release";
        if (d.dmplay_pending) {
            d.dmplay_pending = false;
            EmitFormat(h, "partner_dm=refused; reason=%s\r\n", reason);
        }
        if (d.dmstop_pending) {
            d.dmstop_pending = false;
            EmitFormat(h, "partner_dm=refused; reason=%s\r\n", reason);
        }
        return;
    }
    // One-shots.
    if (d.dmplay_pending) {
        d.dmplay_pending = false;
        const bool busy = dm_ok && (dm.q18[0] == c::q18_dm_act || dm.q18[1] == c::q18_dm_act);
        const bool pending = dm_ok && (dm.g388 & 0x3C) != 0;
        // Without a locator the entry writes CAM+0x1F0 = 0 and CAM+0x200 = 0
        // (0x1198D9D): a no-op only while both already are 0.
        const bool cam_idle = dm_ok && dm.cam_ok && dm.cam1f0 == 0 && dm.cam200 == 0;
        if (d.dmplay_gani >= DmMotionCount(h)) Emit(h, "partner_dm=refused; reason=motion_not_in_archive\r\n");
        else if (!m.dm_patched) Emit(h, "partner_dm=refused; reason=not_patched\r\n");
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

namespace pr = mgscoop::partner_recon;

// What TickInput hands back to Tick: the Reply pose for a reconciliation
// placement (the time-aligned target), and whether the fallback Warp is
// suppressed (the reconciliation places/verifies itself). The fallback Warp
// itself keeps the proven 100 ms interpolated pose.
struct InputTick {
    bool pose_override;
    partner::Pose pose;
    bool suppress_fallback;
};

// Reconciliation event families (one 64-line cap each).
enum ReconFamily : unsigned { kRcPlace, kRcPlaced, kRcSnap, kRcRunaway, kRcFrozen, kRcStance, kRcYaw, kRcStop, kRcDiverge, kRcRefused,
                             kRcFix, kRcInputPin, kRcFamilies };
static_assert(kRcFamilies == sizeof(Mission::recon_cap) / sizeof(LogCap), "one cap per recon family");
const char* ReconFamilyName(unsigned family) noexcept {
    static const char* const names[] = {"place", "placed", "snap", "runaway", "frozen", "stance", "yaw", "stop_check", "diverging", "refused",
                                        "stance_fix", "input_pin"};
    return family < kRcFamilies ? names[family] : "other";
}
void EmitRecon(Mission& m, const Hooks& h, unsigned family, const char* format, ...) noexcept {
    va_list arguments;
    va_start(arguments, format);
    EmitCappedV(m.recon_cap[family < kRcFamilies ? family : kRcRefused], h, "partner_recon", ReconFamilyName(family), format, arguments);
    va_end(arguments);
}
std::int32_t Mm(double value) noexcept {
    if (!std::isfinite(value)) return INT32_MIN;
    if (value >= 2147483647.0) return INT32_MAX;
    if (value <= -2147483647.0) return -INT32_MAX;
    return static_cast<std::int32_t>(std::llround(value));
}

pr::Params ReconParams(const Settings& set) noexcept {
    pr::Params p{};
    // correct=0 keeps the round 3 meaning "never correct": placement only.
    p.recon = set.correct_mm == 0 ? PartnerRecon::off : set.recon;
    p.runaway = set.runaway;
    if (set.correct_mm) p.snap_moving_mm = set.correct_mm;
    if (p.snap_idle_mm > p.snap_moving_mm) p.snap_idle_mm = p.snap_moving_mm;
    p.cap_moving_mmps = set.nudge_mmps;
    p.cap_idle_mmps = set.nudge_mmps / 2;
    p.cap_prone_mmps = set.nudge_mmps / 4 < 150 ? set.nudge_mmps / 4 : 150;
    p.input_stale_ms = set.stale_ms;
    // Native puppet: steering corrects; nudges, snaps and pins are safety nets.
    if (set.native_active) {
        p = pr::NativeParams(p);
        p.recon = PartnerRecon::track;
    }
    return p;
}

// One reconciliation step from this tick's observations.
pr::Out StepRecon(Driver& d, const Hooks& h, const Settings& set, const Clock& clock, bool armable, bool pose_valid,
                  const partner::Pose& pose, bool relink, bool anim_ok, const pp::Anim& anim, const sync::AnimState& remote_anim) noexcept {
    Mission& m = d.mission;
    pr::In in{};
    in.now_ms = clock.now_ms;
    in.armable = armable;
    in.place_request = d.recon_place_request;
    d.recon_place_request = pr::PlaceReason::none;
    in.body_ok = m.body_ok;
    for (unsigned i = 0; i < 3; ++i) in.body[i] = static_cast<double>(m.body_pos[i]) * 1000.0;
    in.body_yaw = partner::QuatToYaw(m.body_quat);
    in.last_ok = m.recon_last_ok;
    in.relink = relink;
    // Input: the newest received frame (net), or the local stick modes.
    pin::Sample latest{};
    const bool latest_ok = set.input == PartnerInput::net && h.latest_input && h.latest_input(h.context, latest) && latest.valid;
    in.input_net = set.input == PartnerInput::net;
    in.in_valid = in.input_net ? latest_ok : true;
    in.in_age_ms = latest_ok ? latest.age_ms : 0;
    if (latest_ok) {
        const double lx = latest.packet.lx / 32767.0, ly = latest.packet.ly / 32767.0;
        in.in_r = std::sqrt(lx * lx + ly * ly);
        if (in.in_r > 1.0) in.in_r = 1.0;
    } else if (set.input == PartnerInput::stick) {
        in.in_r = static_cast<double>(set.stick_pct > 100 ? 100 : set.stick_pct) / 100.0;
    }
    m.recon_in_valid = latest_ok;
    m.recon_in_age_ms = in.in_age_ms;
    m.recon_in_r = static_cast<std::int32_t>(std::lround(in.in_r * 1000.0));
    // Target: the remote pose aligned with the input being consumed (net), or
    // the synthetic pose (fixed/orbit).
    m.recon_delay_ms = 0;
    m.recon_pose_age_ms = 0;
    if (set.source == PartnerSource::net) {
        const std::uint32_t delay = pr::AlignedDelayMs(in.input_net && latest_ok, in.in_age_ms, set.align_ms);
        const pose::Sample sample = h.sample_remote ? h.sample_remote(h.context, delay) : pose::Sample{};
        m.recon_delay_ms = delay;
        m.recon_pose_age_ms = sample.age_ms;
        in.target_ok = sample.valid;
        if (sample.valid) {
            in.target[0] = static_cast<double>(sample.x) * 1000.0;
            in.target[1] = static_cast<double>(sample.y) * 1000.0;
            in.target[2] = static_cast<double>(sample.z) * 1000.0;
            in.target_yaw = static_cast<double>(sample.yaw);
        }
        in.target_live = sample.status == pose::Status::live;
        in.target_snapped = sample.snapped;
        in.remote_anim_valid = sample.anim_valid;
        in.remote_act = sample.act;
        in.remote_stance = sample.anim_stance;
    } else {
        in.target_ok = pose_valid;
        in.target[0] = pose.x_mm; in.target[1] = pose.y_mm; in.target[2] = pose.z_mm;
        in.target_yaw = pose.yaw_mrad / 1000.0;
        in.target_live = pose_valid;
        in.remote_anim_valid = (remote_anim.flags & sync::AnimFlag::Valid) != 0;
        in.remote_act = remote_anim.act;
        in.remote_stance = remote_anim.stance;
    }
    m.recon_target_ok = in.target_ok;
    for (unsigned i = 0; i < 3; ++i) m.recon_target[i] = in.target[i];
    m.recon_target_yaw = in.target_yaw;
    in.body_anim_ok = anim_ok;
    in.body_act = anim_ok ? anim.act[1] : 0;
    in.body_stance = anim_ok ? anim.stance[1] : 0;
    const pr::Out out = pr::Step(m.recon, in, ReconParams(set));
    m.recon_out = out;
    m.recon_have_out = true;
    return out;
}

void LogReconEvents(Driver& d, const Hooks& h, const Settings& set, const pr::Out& o, bool anim_ok, const pp::Anim& anim) noexcept {
    Mission& m = d.mission;
    const pr::Report& r = o.report;
    const std::uint32_t ev = o.events;
    char age[16]{};
    if (m.recon_in_valid) StringCchPrintfA(age, sizeof(age), "%u", m.recon_in_age_ms);
    else StringCchCopyA(age, sizeof(age), "none");
    if (ev & pr::Event::place) {
        char body[48]{};
        if (m.body_ok)
            StringCchPrintfA(body, sizeof(body), "%d,%d,%d", MetresToMm(m.body_pos[0]), MetresToMm(m.body_pos[1]), MetresToMm(m.body_pos[2]));
        else StringCchCopyA(body, sizeof(body), "na");
        EmitRecon(m, h, kRcPlace,
                  "partner_recon=place; reason=%s; via=warp; target_mm=%d,%d,%d; yaw_mrad=%d; body_mm=%s; err_mm=%d; delay_ms=%u; "
                  "pose_age_ms=%u; in_age_ms=%s; tries=%u\r\n",
                  pr::PlaceReasonName(o.place_reason), Mm(o.pos[0]), Mm(o.pos[1]), Mm(o.pos[2]), Mm(o.yaw * 1000.0), body,
                  m.body_ok ? Mm(r.err_mm) : -1, m.recon_delay_ms, m.recon_pose_age_ms, age, m.recon.tries);
    }
    if (ev & pr::Event::placed_ok)
        EmitRecon(m, h, kRcPlaced, "partner_recon=placed; ok=1; err_mm=%d; tries=%u; ms=%u\r\n", Mm(r.place_err_mm), r.place_tries,
                  r.place_ms);
    if (ev & pr::Event::placed_fail)
        EmitRecon(m, h, kRcPlaced, "partner_recon=placed; ok=0; err_mm=%d; tries=%u; ms=%u; next=fallback_warp,retry_ms=5000\r\n",
                  Mm(r.place_err_mm), r.place_tries, r.place_ms);
    if (ev & pr::Event::snap) {
        ++m.recon_snaps_total;
        EmitRecon(m, h, kRcSnap,
                  "partner_recon=snap; reason=%s; via=xform; err_mm=%d; dy_mm=%d; v_free_mmps=%d; v_remote_mmps=%d; snaps_10s=%u\r\n",
                  r.remote_moving ? "far_moving" : r.prone ? "far_prone" : "far_idle", Mm(r.err_mm), Mm(r.dy_mm), Mm(r.v_free_mmps),
                  Mm(r.v_remote_mmps), r.snaps_window);
    }
    if (ev & pr::Event::diverging)
        EmitRecon(m, h, kRcDiverge, "partner_recon=diverging; snaps_10s=3; next=place,pin\r\n");
    if (ev & pr::Event::runaway)
        EmitRecon(m, h, kRcRunaway,
                  "partner_recon=runaway; remote=%s; v_free_mmps=%d; v_remote_mmps=%d; dv_mmps=%d; dv_along_mmps=%d; dv_cross_mmps=%d; "
                  "err_mm=%d; remote_idle_ms=%u; act=%u; s=%u; v=%u; action=%s\r\n",
                  r.runaway_moving ? "moving" : "idle", Mm(r.v_free_mmps), Mm(r.v_remote_mmps), Mm(r.dv_mmps), Mm(r.dv_along_mmps),
                  Mm(r.dv_cross_mmps), Mm(r.err_mm), r.idle_ms,
                  anim_ok ? unsigned(anim.act[1]) : 0u, anim_ok ? unsigned(anim.stance[1]) : 0u, anim_ok ? unsigned(anim.speed[1]) : 0u,
                  PartnerRunawayName(set.runaway));
    if (ev & pr::Event::runaway_end)
        EmitRecon(m, h, kRcRunaway, "partner_recon=runaway_end; ms=%u; reason=%s\r\n", r.runaway_ms,
                  r.remote_moving ? "remote_moving" : "stopped");
    if (ev & pr::Event::lag)
        EmitRecon(m, h, kRcRunaway, "partner_recon=lag; v_free_mmps=%d; v_remote_mmps=%d; dv_along_mmps=%d; err_mm=%d; action=nudge_along\r\n",
                  Mm(r.v_free_mmps), Mm(r.v_remote_mmps), Mm(r.dv_along_mmps), Mm(r.err_mm));
    if (ev & pr::Event::lag_end) EmitRecon(m, h, kRcRunaway, "partner_recon=lag_end; err_mm=%d\r\n", Mm(r.err_mm));
    if (ev & pr::Event::pin_end) EmitRecon(m, h, kRcDiverge, "partner_recon=pin_end\r\n");
    if (ev & pr::Event::frozen) EmitRecon(m, h, kRcFrozen, "partner_recon=frozen; reason=%s\r\n", pr::FrozenName(r.frozen));
    if (ev & pr::Event::unfrozen) EmitRecon(m, h, kRcFrozen, "partner_recon=unfrozen\r\n");
    if (ev & pr::Event::stance)
        EmitRecon(m, h, kRcStance, "partner_recon=stance; from=%u; to=%u; suspend_ms=%u\r\n", unsigned(r.stance_from),
                  unsigned(r.stance_to), r.suspend_ms);
    if (ev & pr::Event::stance_mismatch)
        EmitRecon(m, h, kRcStance, "partner_recon=stance_mismatch; remote=%u; body=%u; ms=%u\r\n", unsigned(m.recon.stance_applied),
                  anim_ok ? unsigned(anim.stance[1]) : 0u, r.mismatch_ms);
    if (ev & pr::Event::yaw) EmitRecon(m, h, kRcYaw, "partner_recon=yaw; err_mrad=%d\r\n", Mm(r.yaw_err_mrad));
    if (ev & pr::Event::stop_check)
        EmitRecon(m, h, kRcStop, "partner_recon=stop_check; ok=%u; ms=%u; v_free_mmps=%d\r\n", r.stop_ok == 1 ? 1u : 0u, r.stop_ms,
                  Mm(r.v_free_mmps));
    if (ev & pr::Event::input_pin)
        EmitRecon(m, h, kRcInputPin, "partner_recon=input_pin; in_age_ms=%s; err_mm=%d; reason=input_stale\r\n", age, Mm(r.err_mm));
    if (ev & pr::Event::input_pin_end)
        EmitRecon(m, h, kRcInputPin, "partner_recon=input_pin_end; ms=%u; reason=%s\r\n", r.input_pin_ms,
                  m.recon_in_valid ? "input_live" : "pose_stale");
}

// Executes a nudge/snap/pin/yaw (a placement is the caller's Warp op).
// Returns whether it reached the body.
bool ExecuteRecon(Driver& d, const Hooks& h, const pr::Out& o, bool terminating) noexcept {
    Mission& m = d.mission;
    alignas(16) float pos[4] = {static_cast<float>(o.pos[0] / 1000.0), static_cast<float>(o.pos[1] / 1000.0),
                                static_cast<float>(o.pos[2] / 1000.0), 0.0f};
    const bool nudge = o.action == pr::Action::nudge;
    const char* refused = nullptr;
    if (m.native_disabled) {
        refused = "native_disabled";
    } else if (nudge) {
        refused = h.apply_position ? h.apply_position(h.context, terminating, pos) : "unavailable";
    } else {
        alignas(16) float q[4]{};
        partner::YawToQuat(static_cast<float>(o.yaw), q);
        refused = h.apply_transform ? h.apply_transform(h.context, terminating, pos, q, PartnerReset::none) : "unavailable";
    }
    if (IsFault(refused)) NoteFault(m, h, nudge ? "recon_nudge" : "recon_xform");
    if (refused) {
        if (m.recon_refused != refused) {
            m.recon_refused = refused;
            EmitRecon(m, h, kRcRefused, "partner_recon=refused; action=%s; reason=%s; fallback=%s\r\n", pr::ActionName(o.action),
                      refused, nudge ? "none" : "place");
        }
        // A refused xform places the body with a Warp on the next tick.
        if (!nudge) d.recon_place_request = pr::PlaceReason::rearm;
        return false;
    }
    m.recon_refused = nullptr;
    if (!nudge) ++m.body_epoch;
    return true;
}

// The 1 Hz partner_recon line (apply=input). recon= is the effective setting
// (correct=0 turns the reconciliation off whatever recon= says).
void ReconLine(Mission& m, const Hooks& h, const Settings& set, bool anim_ok, const pp::Anim& anim, std::uint64_t now) noexcept {
    if (!m.recon_have_out || now - m.recon_line_ms < kReconLineMs) return;
    m.recon_line_ms = now;
    if (m.recon_lines >= kReconLineCap) {
        if (!m.recon_lines_capped) { m.recon_lines_capped = true; Emit(h, "partner_recon=capped; limit=3600; kind=line\r\n"); }
        return;
    }
    ++m.recon_lines;
    const pr::Out& o = m.recon_out;
    const pr::Report& r = o.report;
    char age[16]{}, body[48]{}, target[48]{}, stop[4]{};
    if (m.recon_in_valid) StringCchPrintfA(age, sizeof(age), "%u", m.recon_in_age_ms);
    else StringCchCopyA(age, sizeof(age), "none");
    if (m.body_ok) StringCchPrintfA(body, sizeof(body), "%d,%d,%d", MetresToMm(m.body_pos[0]), MetresToMm(m.body_pos[1]), MetresToMm(m.body_pos[2]));
    else StringCchCopyA(body, sizeof(body), "na");
    if (m.recon_target_ok) StringCchPrintfA(target, sizeof(target), "%d,%d,%d", Mm(m.recon_target[0]), Mm(m.recon_target[1]), Mm(m.recon_target[2]));
    else StringCchCopyA(target, sizeof(target), "na");
    StringCchCopyA(stop, sizeof(stop), r.stop_ok < 0 ? "na" : r.stop_ok ? "1" : "0");
    EmitFormat(h,
        "partner_recon; mode=%s; recon=%s; err_mm=%d; dy_mm=%d; along_mm=%d; cross_mm=%d; v_free_mmps=%d; v_remote_mmps=%d; dv_mmps=%d; "
        "body_mm=%s; target_mm=%s; yaw_err_mrad=%d; nudges=%u; nudge_mm=%d; snaps=%u; places=%u; pins=%u; yaws=%u; act=%u; s=%u; "
        "rs=%u; frozen=%s; delay_ms=%u; pose_age_ms=%u; in_age_ms=%s; in_r=%d; stop_ok=%s; runaway=%u; input_pin=%u; fixes=%u\r\n",
        pr::ModeName(o.mode), PartnerReconName(ReconParams(set).recon), Mm(r.err_mm), Mm(r.dy_mm), Mm(r.along_mm), Mm(r.cross_mm),
        Mm(r.v_free_mmps), Mm(r.v_remote_mmps), Mm(r.dv_mmps), body, target, Mm(r.yaw_err_mrad), r.nudges, Mm(r.nudge_mm), r.snaps,
        r.places, r.pins, r.yaws, anim_ok ? unsigned(anim.act[1]) : 0u, anim_ok ? unsigned(anim.stance[1]) : 0u,
        unsigned(m.recon.stance_applied), pr::FrozenName(r.frozen), m.recon_delay_ms, m.recon_pose_age_ms, age, m.recon_in_r, stop,
        r.runaway_active ? 1u : 0u, r.input_pin ? 1u : 0u, m.recon_fixes);
}

// The 1 Hz wrap census line (wrap != off, entries installed): per entry the
// block-1 calls, same-thread, nested and window deltas plus the phases seen.
void WrapLine(Mission& m, const Hooks& h, const Settings& set, const partner_input_hook::WrapStats& w, std::uint64_t now) noexcept {
    if (set.wrap == PartnerWrap::off || !w.installed || now - m.wrap_line_ms < kInputStatsMs || m.wrap_lines >= kWrapLineCap) return;
    m.wrap_line_ms = now;
    ++m.wrap_lines;
    const partner_input_hook::WrapStats& b = m.wrap_base;
    char parts[5][64]{};
    for (std::size_t e = 0; e < partner_input_hook::kWrapEntries; ++e)
        StringCchPrintfA(parts[e], sizeof(parts[e]), "%u,%u,%u,%u,0x%x", w.calls1[e] - b.calls1[e], w.same_thread[e] - b.same_thread[e],
                         w.nested[e] - b.nested[e], w.windows[e] - b.windows[e], w.phase_mask[e]);
    EmitFormat(h, "partner_wrap; mode=%s; svc28=%s; svc30=%s; svc40=%s; walk7=%s; walk11=%s; r0=%u,%u,%u,%u,%u; other=%u\r\n",
               PartnerWrapName(set.wrap), parts[0], parts[1], parts[2], parts[3], parts[4], w.calls0[0] - b.calls0[0],
               w.calls0[1] - b.calls0[1], w.calls0[2] - b.calls0[2], w.calls0[3] - b.calls0[3], w.calls0[4] - b.calls0[4],
               w.other_calls - b.other_calls);
    m.wrap_base = w;
}

// The 2 Hz partner_body line (apply=input or an input mode), from Tick so no
// early return can hide it. speed_mmps is horizontal over same-epoch samples
// at least kSpeedSpanMs apart (na otherwise: our own placements, snaps, pins
// and Warps start a new epoch); own_mmps is the reconciliation's estimate of
// the game's own motion (readback minus our writes); vel_mmps the game's
// PS+0x268 per-id velocity (diagnostic).
// Sender facing: vars.playerRotY is the control direction. It steps at once on
// W -> W+A while the body turns over about 250 ms (hidden run
// 442f60813e23409da970d49be0c21788, facing ring). Publish the local instance-0
// body-transform yaw when it is a finite unit quaternion read this tick at the
// published position; otherwise fall back to vars.playerRotY.
constexpr std::uint32_t kLocalBodyMaxMm = 2000;
bool LocalBodyYaw(const Hooks& h, const pp::State& st, const partner::Tick& tick, float& yaw) noexcept {
    float pos[4]{}, quat[4]{};
    if (!h.read_local_body || st.local != 0 || !h.read_local_body(h.context, st, pos, quat)) return false;
    double norm = 0.0;
    for (const float v : quat) {
        if (!std::isfinite(v)) return false;
        norm += static_cast<double>(v) * v;
    }
    if (std::fabs(norm - 1.0) > 0.01) return false;
    for (unsigned i = 0; i < 3; ++i)
        if (!std::isfinite(pos[i]) || std::fabs(pos[i]) > 1000000.0f) return false;
    if (partner::DistanceMm(tick.x_mm, tick.y_mm, tick.z_mm, MetresToMm(pos[0]), MetresToMm(pos[1]), MetresToMm(pos[2])) >
        kLocalBodyMaxMm)
        return false;
    yaw = partner::QuatToYaw(quat);
    return std::isfinite(yaw);
}

// One facing evidence sample per net tick (worker state.json "facing"):
// the local published and body-transform yaw (the sender side of this game),
// the interpolated remote target, the applied yaw, the partner body readback
// and the mapped clip. Degrees; NaN when unread.
void RecordFacing(Mission& m, const partner::Tick& tick, std::uint64_t now, bool local_body, float local_body_yaw,
                  bool pose_valid, const partner::Pose& pose, partner_facing::Snap snap) noexcept {
    namespace pf = partner_facing;
    pf::Sample s{};
    s.ms = static_cast<std::uint32_t>(now);
    s.source_yaw = static_cast<float>(tick.yaw_cdeg) * 0.01f;
    s.source_body_yaw = s.target_yaw = s.applied_yaw = s.body_yaw = NAN;
    if (local_body) s.source_body_yaw = local_body_yaw * pf::kDegPerRad;
    if (pose_valid) {
        s.target_yaw = static_cast<float>(pose.yaw_mrad) * 0.001f * pf::kDegPerRad;
        s.applied_yaw = m.facing_active ? m.facing_applied_yaw * pf::kDegPerRad : s.target_yaw;
    }
    if (m.body_ok) s.body_yaw = partner::QuatToYaw(m.body_quat) * pf::kDegPerRad;
    s.clip = m.dmmap.cur;
    s.move = m.dmmap.cand == pdm::kNoGani ? pdm::kMoveUnknown : m.dmmap.cand_cls.move;
    s.snap = snap;
    pf::Push(m.facing_ring, s);
}

void BodyLine(Driver& d, const Hooks& h, const pp::State& st, const Settings& set, const partner::Tick& tick, const Clock& clock,
              bool parts_loaded) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    if (!m.body_ok || !parts_loaded) return;
    const std::int32_t bx = MetresToMm(m.body_pos[0]), by = MetresToMm(m.body_pos[1]), bz = MetresToMm(m.body_pos[2]);
    m.body_head = (m.body_head + 1) % kBodyRing;
    m.body_ring[m.body_head] = Mission::BodySample{now, bx, bz, m.body_epoch};
    if (m.body_count < kBodyRing) ++m.body_count;
    if (!(set.apply == PartnerApply::input || set.input != PartnerInput::off)) return;
    if (now - m.body_ms < kBodyMs) return;
    if (m.body_lines >= kBodyCap) {
        if (!m.body_capped) { m.body_capped = true; Emit(h, "partner_body=capped; limit=3600\r\n"); }
        return;
    }
    m.body_ms = now;
    ++m.body_lines;
    char speed[16] = "na", own[16] = "na", vel[16] = "na";
    std::int32_t heading = 0;
    const Mission::BodySample& newest = m.body_ring[m.body_head];
    // The oldest same-epoch sample (oldest first), over the actual span.
    for (unsigned i = m.body_count; i-- > 1;) {
        const Mission::BodySample& old = m.body_ring[(m.body_head + kBodyRing - i) % kBodyRing];
        if (old.epoch != newest.epoch) continue;
        if (newest.t_ms < old.t_ms + kSpeedSpanMs) break;  // newer ones are shorter still
        const double dx = static_cast<double>(newest.x_mm) - old.x_mm, dz = static_cast<double>(newest.z_mm) - old.z_mm;
        StringCchPrintfA(speed, sizeof(speed), "%u", Clamp32(static_cast<std::uint64_t>(std::sqrt(dx * dx + dz * dz) * 1000.0 /
                                                                                         static_cast<double>(newest.t_ms - old.t_ms))));
        if (dx != 0.0 || dz != 0.0) heading = partner::NormalizeMrad(static_cast<std::int64_t>(std::atan2(dx, dz) * 1000.0));
        break;
    }
    if (m.recon_active && m.recon_have_out) StringCchPrintfA(own, sizeof(own), "%d", Mm(m.recon_out.report.v_free_mmps));
    float v[4]{};
    if (h.read_velocity && h.read_velocity(h.context, st, v) && std::isfinite(v[0]) && std::isfinite(v[2]))
        StringCchPrintfA(vel, sizeof(vel), "%d", Mm(std::sqrt(static_cast<double>(v[0]) * v[0] + static_cast<double>(v[2]) * v[2]) * 1000.0));
    pp::Anim banim{};
    const bool banim_ok = h.read_anim && h.read_anim(h.context, st, banim);
    std::uint8_t psw = 0;
    if (h.census) {
        pp::Census cen{};
        h.census(h.context, st, cen);
        psw = cen.k[1].psw;
    }
    EmitFormat(h,
        "partner_body; R=1; body_mm=%d,%d,%d; yaw_mrad=%d; speed_mmps=%s; own_mmps=%s; vel_mmps=%s; heading_mrad=%d; cam_mrad=%d; "
        "act=%u; s=%u; v=%u; psw=0x%02x; epoch=%u; snaps=%u\r\n",
        bx, by, bz, pp::ToMrad(partner::QuatToYaw(m.body_quat)), speed, own, vel, heading, partner::CdegToMrad(tick.cam_cdeg),
        banim_ok ? unsigned(banim.act[1]) : 0u, banim_ok ? unsigned(banim.stance[1]) : 0u, banim_ok ? unsigned(banim.speed[1]) : 0u,
        unsigned(psw), m.body_epoch, m.recon_snaps_total);
}

void TickInput(Driver& d, const Hooks& h, const LabConfig& lab, const pp::State& st, const Settings& set, const partner::Tick& tick,
               const Clock& clock, bool terminating, bool parts_loaded, bool pose_valid, const partner::Pose& pose, bool relink,
               bool anim_ok, const pp::Anim& anim, const sync::AnimState& remote_anim, std::uint32_t& ops, InputTick& out) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    (void)parts_loaded;
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
    if (m.p218_active && set.native_active) {
        m.p218_active = false;
        Emit(h, "partner_input=p218; on=0; reason=native\r\n");
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
    // Input frame v2: every tick's snapshot feeds the edge sets (press/release
    // edges, 12-frame taps and holds), sent or not.
    if (set.isend && set.stage >= 1 && h.read_pad && h.submit_input) {
        pin::PadRaw raw{};
        if (h.read_pad(h.context, st, raw)) {
            const pin::PadSnapshot snap = pin::Effective(raw);
            pin::SenderObserve(m.sender, snap);
            pin::InputPacket pkt = pin::Quantize(snap, tick.cam_cdeg);
            pin::SenderFill(m.sender, pkt);
            // Native puppet: v3 (look, camera, heading) only when this copy runs
            // the native drive; the peer refuses aim with no_v3 otherwise.
            if (d.settings.drive == PartnerDrive::native) pin::Fill3(pkt, SenderV3(d, h, st, snap));
            if (h.submit_input(h.context, pkt)) pin::SenderSent(m.sender);
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
        if (m.recon_active) {
            pr::Reset(m.recon);
            m.recon_active = false;
            m.recon_have_out = false;
        }
        return;
    }
    // Arming conditions in order; the first failure is the refusal reason.
    char reason[48] = {0};
    const auto fail = [&](const char* text) noexcept { if (!reason[0]) StringCchCopyA(reason, sizeof(reason), text); };
    if (m.input_faulted) fail("faulted");
    if (m.native_disabled) fail("native_disabled");
    if (!(set.apply == PartnerApply::input || set.apply == PartnerApply::xform)) fail("apply");
    if (set.anim != PartnerAnim::off) fail("anim");
    if (set.dm == PartnerDm::on || set.dm == PartnerDm::map || d.mission.dmmap.phase != pdm::Phase::idle) fail("dm");
    if (set.stage < 3) fail("stage");
    // Native: the deferral is armed before the native Arm is published (the
    // arbiter requests it from the place step on), so no frame runs block 1's
    // job inline in a batch under a native Arm.
    if (set.native_active && !m.native_defer_armed) fail("native_defer");
    resolve();
    // "busy": a window was open across every read attempt (hook thread !=
    // Lua thread). An armed window keeps its published Arm for this tick.
    const bool busy = census && std::strcmp(census, "busy") == 0;
    const bool keep_arm = busy && m.input_armed;
    if (!reason[0] && census && !keep_arm) { StringCchPrintfA(reason, sizeof(reason), "census_%s", census); }
    if (set.input == PartnerInput::net && h.net_running && !h.net_running(h.context)) fail("net");
    // Lazy hook install.
    if (!reason[0]) {
        if (!lab.partner_input_hook) fail("hook_disabled");
        else if (h.install_input_hook) {
            if (const char* why = h.install_input_hook(h.context)) StringCchPrintfA(reason, sizeof(reason), "install_%s", why);
        } else fail("unavailable");
    }
    // Lazy wrap install (wrap=census|window, after the main hook).
    if (!reason[0] && set.wrap != PartnerWrap::off && !m.wrap_install_tried && h.install_wrap) {
        m.wrap_install_tried = true;
        h.install_wrap(h.context);
        m.wrap_base = s.wrap;
        m.wrap_line_ms = now;
    }
    if (terminating) fail("terminating");
    const bool will_arm = reason[0] == 0;
    // Round 4: reconciliation of the input-driven body (apply=input). It
    // decides placement (window disarmed, Lua Warp to the aligned target),
    // verification, and the corrections executed below.
    pr::Out ro{};
    bool recon_used = false;
    if (input_apply) {
        if (!m.recon_active) {
            pr::Reset(m.recon);
            m.recon_active = true;
        }
        recon_used = true;
        ro = StepRecon(d, h, set, clock, will_arm, pose_valid, pose, relink, anim_ok, anim, remote_anim);
        LogReconEvents(d, h, set, ro, anim_ok, anim);
    } else if (m.recon_active) {
        pr::Reset(m.recon);
        m.recon_active = false;
        m.recon_have_out = false;
    }
    m.recon_last_ok = false;
    if (recon_used) {
        if (ro.action == pr::Action::place) {
            ops |= partner::op::warp;
            out.pose_override = true;
            out.pose = partner::PoseFromMetres(static_cast<float>(ro.pos[0] / 1000.0), static_cast<float>(ro.pos[1] / 1000.0),
                                               static_cast<float>(ro.pos[2] / 1000.0), static_cast<float>(ro.yaw));
            m.recon_last_ok = true;
            ++m.body_epoch;
        } else if (ro.action != pr::Action::none) {
            m.recon_last_ok = ExecuteRecon(d, h, ro, terminating);
        }
        // Placing or verifying: no per-tick Warp on top (waiting for a live
        // pose keeps the fallback, so the partner never freezes). The
        // fallback Warp itself (window not armed) keeps the proven 100 ms
        // interpolated pose: the aligned target extrapolates the newest
        // snapshot and would overshoot stops and jitter on turns.
        if (ro.action == pr::Action::place || ro.mode == pr::Mode::verify) out.suppress_fallback = true;
    }
    const bool arm_now = will_arm && (!recon_used || ro.arm);
    // The reconciliation's corrective stance input (stance level): one tap
    // toward the remote's stance, or a hold for prone, through the overlay.
    if (recon_used && ro.stance_fix != pr::StanceFix::none && arm_now) {
        const bool hold = ro.stance_fix == pr::StanceFix::hold;
        if (!(set.input_mask & kStanceBit)) {
            EmitRecon(m, h, kRcFix, "partner_recon=stance_fix; refused=mask; remote=%u; body=%u\r\n", unsigned(ro.report.fix_remote),
                      unsigned(ro.report.fix_body));
        } else {
            if (hold) {
                m.press_bits = kStanceBit;
                m.press_until_ms = now + kStanceFixHoldMs;
            } else {
                ++d.tap_seq;
                m.tap_bits = kStanceBit;
            }
            ++m.recon_fixes;
            EmitRecon(m, h, kRcFix, "partner_recon=stance_fix; remote=%u; body=%u; via=%s; ms=%u; fixes=%u\r\n",
                      unsigned(ro.report.fix_remote), unsigned(ro.report.fix_body), pr::StanceFixName(ro.stance_fix),
                      hold ? kStanceFixHoldMs : static_cast<std::uint32_t>(pin::kTapFrames), m.recon_fixes);
        }
    }
    // One-shot acceptance (only while about to be armed and within the mask).
    if (d.tap_pending) {
        d.tap_pending = false;
        if (!arm_now) Emit(h, "partner_input=tap_refused; reason=not_armed\r\n");
        else if (d.tap_bits_pending & ~set.input_mask) Emit(h, "partner_input=tap_refused; reason=mask\r\n");
        else {
            ++d.tap_seq;
            m.tap_bits = d.tap_bits_pending;
            EmitFormat(h, "partner_input=tap; bits=0x%x; frames=4\r\n", d.tap_bits_pending);
        }
    }
    if (d.press_pending) {
        d.press_pending = false;
        if (!arm_now) Emit(h, "partner_input=tap_refused; reason=not_armed\r\n");
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
        if (m.input_armed) ++m.body_epoch;
        DisarmInput(d, h, "refused");
        if (recon_used) ReconLine(m, h, set, anim_ok, anim, now);
        return;
    }
    m.input_refused_text[0] = 0;
    if (!arm_now) {
        // The reconciliation holds the window off (placement, verification,
        // runaway=disarm, a failed placement): the window stays disarmed.
        if (m.input_armed) ++m.body_epoch;
        const char* why = ro.mode == pr::Mode::verify || ro.mode == pr::Mode::place ? "place"
                        : ro.mode == pr::Mode::pin ? "runaway" : "place_failed";
        const bool was_armed = m.input_armed;
        DisarmInput(d, h, why);
        if (!was_armed) PublishDisarmed(d, h);
        ReconLine(m, h, set, anim_ok, anim, now);
        if (h.input_hook_live && !h.input_hook_live(h.context)) Emit(h, "partner_input_hook=lost\r\n");
        return;
    }
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
        EmitInput(m, h,
                  "partner_input=armed; mode=%s; gen=%u; apply=%s; cam=%s; hold=%s; yaw=%s; words=%s; mask=0x%x; correct_mm=%u; "
                  "recon=%s; slot=%s; wrap=%s; p218=%u\r\n",
                  PartnerInputName(set.input), m.input_gen, PartnerApplyName(set.apply), PartnerInputCamName(set.input_cam),
                  PartnerInputHoldName(set.input_hold), PartnerInputYawName(set.input_yaw), WordsText(set, words), set.input_mask,
                  set.correct_mm, PartnerReconName(ReconParams(set).recon), PartnerSlotName(set.slot), PartnerWrapName(set.wrap),
                  m.p218_active ? 1u : 0u);
        LogInputCensus(m, h, targets);  // once per arm
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
    // are this interval's deltas (per_s normalized to one second). Round 4
    // adds the words, the edges injected, the synthesized taps, the hold
    // classifier answers for btn 1 (h0..h3), the key-manager stub calls and
    // the shadowed PP field changes.
    if (now - m.input_stats_ms >= kInputStatsMs) {
        const pin::WindowStats& b = m.stats_base;
        const std::uint64_t dt = now > m.stats_base_ms ? now - m.stats_base_ms : 0;
        const std::uint32_t dw = s.window.windows - b.windows;
        const std::uint32_t per_s = dt ? Clamp32(static_cast<std::uint64_t>(dw) * 1000u / dt) : 0u;
        char age[16]{};
        if (s.window.last_age_ms == pin::kNoSample) StringCchCopyA(age, sizeof(age), "none");
        else StringCchPrintfA(age, sizeof(age), "%u", s.window.last_age_ms);
        char words[4]{};
        EmitInput(m, h,
            "partner_input; mode=%s; windows=%u; per_s=%u; root1_calls=%u; skips=%u; skip=%s; stale=%u; age_ms=%s; held=0x%x; "
            "lx=%d; ly=%d; dir_mrad=%d; mag=%d; cam_dirty=0x%x; cam_restored=%u; hold_calls=%u; us_max=%u; hook_thread=%u; lua_thread=%u; "
            "words=%s; hold=%s; press_n=%u; release_n=%u; synth_taps=%u; h=%u,%u,%u,%u; km=%u; shadow=%u\r\n",
            PartnerInputName(set.input), windows_armed, per_s, s.window.root1_calls - b.root1_calls, s.window.skips - b.skips,
            pin::SkipName(s.window.last_skip), s.window.stale - b.stale, age, s.window.last_held, s.window.last_lx,
            s.window.last_ly, s.window.last_dir, s.window.last_mag, s.window.cam_dirty_or, s.window.cam_restored - b.cam_restored,
            s.window.hold_calls - b.hold_calls, m.us_max_acc, s.window.hook_thread, static_cast<unsigned>(GetCurrentThreadId()),
            WordsText(set, words), PartnerInputHoldName(set.input_hold), s.window.press_n - b.press_n, s.window.release_n - b.release_n,
            s.window.synth_taps - b.synth_taps, s.window.hold_own[0] - b.hold_own[0], s.window.hold_own[1] - b.hold_own[1],
            s.window.hold_own[2] - b.hold_own[2], s.window.hold_own[3] - b.hold_own[3], s.window.km_calls - b.km_calls,
            s.window.shadow_changes - b.shadow_changes);
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
    if (recon_used) ReconLine(m, h, set, anim_ok, anim, now);
    WrapLine(m, h, set, s.wrap, now);
    // The patch-live check.
    if (h.input_hook_live && !h.input_hook_live(h.context)) {
        Emit(h, "partner_input_hook=lost\r\n");
        DisarmInput(d, h, "hook_lost");
    }
}

// --- Native puppet (docs/native-puppet.md 7) -----------------------------------
// The GO identity and the census wrap roots name per-mission objects: they are
// withdrawn at mission boundaries and once the native drive is no longer
// configured (the deferral is disarmed first by every caller).
void WithdrawNativeTargets(Mission& m, const Hooks& h) noexcept {
    if (m.native_identity && h.publish_identity) h.publish_identity(h.context, partner_defer::Identity{}, false);
    m.native_identity = false;
    if (m.native_roots && h.publish_wrap_roots) h.publish_wrap_roots(h.context, 0, 0, false);
    m.native_roots = false;
}

void EmitNative(Mission& m, const Hooks& h, LogCap& cap, const char* head, const char* format, ...) noexcept {
    if (cap.lines >= kNativeEventCap) {
        if (!cap.capped) {
            cap.capped = true;
            EmitFormat(h, "%s=capped; limit=%u\r\n", head, kNativeEventCap);
        }
        return;
    }
    char line[384]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) { ++cap.lines; Emit(h, line); }
    (void)m;
}

// Reliable channel 2 (equipment manifest): drained every tick in every drive
// mode so the sender's window never stalls; the receiver only stores it.
void DrainEquip(Driver& d, const Hooks& h) noexcept {
    if (!h.drain_reliable) return;
    Mission& m = d.mission;
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    for (unsigned i = 0; i < 32; ++i) {
        std::uint8_t message[128]{};
        std::size_t size = 0;
        if (!h.drain_reliable(h.context, partner_equip_wire::kChannel, message, sizeof(message), size)) break;
        if (partner_equip_wire::Accept(m.equip, message, size, epoch) && m.equip.manifest.revision != m.equip_logged_rev) {
            m.equip_logged_rev = m.equip.manifest.revision;
            const auto& e = m.equip.manifest;
            EmitNative(m, h, m.native_equip_cap, "partner_equip",
                       "partner_equip=manifest; rev=%u; inv=%u; slots=%u:%u,%u:%u,%u:%u\r\n", unsigned(e.revision), unsigned(e.inv_slot),
                       unsigned(e.slots[0].base_id), unsigned(e.slots[0].alias), unsigned(e.slots[1].base_id), unsigned(e.slots[1].alias),
                       unsigned(e.slots[2].base_id), unsigned(e.slots[2].alias));
        }
    }
}

std::uint32_t Rate(std::uint32_t now, std::uint32_t before, std::uint64_t dt_ms) noexcept {
    return dt_ms ? Clamp32(static_cast<std::uint64_t>(now - before) * 1000u / dt_ms) : 0u;
}

void CopyText(char* out, std::size_t capacity, const char* text) noexcept {
    StringCchCopyA(out, capacity, text ? text : "");
}

// Arbiter step, identity/wrap-root publication, lazy install, deferral arming,
// steering for the next Arm, telemetry and the native log lines.
void TickNative(Driver& d, const Hooks& h, const LabConfig& lab, const pp::State& st, const partner::Tick& tick, const Clock& clock,
                bool terminating, bool realized, bool parts_loaded, bool anim_ok, const pp::Anim& anim,
                const sync::AnimState& remote_anim, std::uint32_t remote_flags, std::uint32_t remote_equip) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    const std::uint64_t now = clock.now_ms;
    const bool configured = set.drive == PartnerDrive::native;
    const PartnerDefer defer = EffectivePartnerDefer(set.drive, set.defer);
    // Statistics: the deferral (process totals) and the hook's wrap/native parts.
    partner_defer::Stats ds{};
    if (h.drain_defer_stats) h.drain_defer_stats(h.context, ds);
    InputStats is{};
    if (h.drain_input_stats) h.drain_input_stats(h.context, is);
    if (is.window.us_max > m.us_max_acc) m.us_max_acc = is.window.us_max;
    const partner_defer::Stats& db = m.defer_last_ok ? m.defer_last : ds;
    const partner_input_hook::WrapStats& wb = m.hook_native_last_ok ? m.wrap_native_last : is.wrap;
    const partner_input_hook::NativeStats& nb = m.hook_native_last_ok ? m.hook_native_last : is.native;
    pnat::In in{};
    in.w50_b1_s0 = ds.w50_b1_s0 - db.w50_b1_s0;
    in.deferred = ds.deferred - db.deferred;
    in.drains = (ds.drains[0] - db.drains[0]) + (ds.drains[1] - db.drains[1]);
    in.windowed = ds.windowed - db.windowed;
    in.unwindowed = ds.unwindowed - db.unwindowed;
    in.lost = (ds.lost - db.lost) + (ds.torn - db.torn) + (ds.gate_closed - db.gate_closed);
    in.wrap_in_batch = is.wrap.wrap_in_batch - wb.wrap_in_batch;
    in.frames = is.native.frames - nb.frames;
    in.drain_block = is.native.drain_block_refused - nb.drain_block_refused;
    in.drain_us_max = ds.drain_us_max;
    if (ds.drain_us_max > m.native_drain_us_acc) m.native_drain_us_acc = ds.drain_us_max;
    m.defer_last = ds;
    m.defer_last_ok = true;
    m.wrap_native_last = is.wrap;
    m.hook_native_last = is.native;
    m.hook_native_last_ok = true;
    if (ds.faulted && !m.native_fault_logged && configured) {
        m.native_fault_logged = true;
        EmitFormat(h, "partner_defer=fault; phase=%s; code=0x%08x; deferral=disabled\r\n", partner_defer::FaultPhaseName(ds.fault_phase),
                   ds.fault_code);
    }
    // Identity and wrap roots: every tick where the input targets resolve,
    // independent of arming (census needs block classes without an Arm).
    pp::InputTargets targets{};
    const char* tref = "unavailable";
    if (configured && set.stage >= 3 && realized && h.input_targets) tref = h.input_targets(h.context, st, targets);
    m.native_targets_ok = false;
    if (configured && !tref && h.native_targets) {
        pp::NativeTargets nt{};
        if (!h.native_targets(h.context, st, targets, nt)) {
            m.native_targets = nt;
            m.native_targets_ok = true;
        }
    }
    if (m.native_targets_ok) {
        if (h.publish_identity)
            h.publish_identity(h.context, partner_defer::Identity{m.native_targets.go, m.native_targets.pool0, m.native_targets.pool1}, true);
        m.native_identity = true;
        if (h.publish_wrap_roots) {
            h.publish_wrap_roots(h.context, targets.root0, targets.root1, defer != PartnerDefer::off);
            m.native_roots = true;
        }
    } else if (m.native_identity && !m.native_out.active && !(tref && std::strcmp(tref, "busy") == 0)) {
        if (h.publish_identity) h.publish_identity(h.context, partner_defer::Identity{}, false);
        m.native_identity = false;
    }
    // Lazy install (once per process; the refusal is sticky).
    const char* install_refusal = nullptr;
    if (configured && defer != PartnerDefer::off) {
        if (!h.install_defer) install_refusal = "unavailable";
        else if (!lab.partner_input_hook) install_refusal = "hook_disabled";
        else if (set.stage >= 3 && realized && parts_loaded && !terminating) install_refusal = h.install_defer(h.context, defer);
    }
    NativeInstall ni{};
    if (h.native_install) h.native_install(h.context, ni);
    // The arbiter.
    in.now_ms = now;
    in.configured = configured;
    in.defer_on = defer == PartnerDefer::on;
    in.terminating = terminating;
    in.installed_h0 = ni.h0;
    in.installed_wrap = ni.wrap;
    in.installed_h2 = ni.h2;
    in.installed_h1 = ni.h1;
    in.live_h0 = ni.live_h0;
    in.live_h1 = ni.live_h1;
    in.live_h2 = ni.live_h2;
    in.installed_p1 = ni.p1;
    in.live_p1 = ni.live_p1;
    in.install_refusal = install_refusal;
    in.thread_ok = ds.batch_threads_seen == 1 && ds.batch_thread != 0 && ds.batch_thread == ds.hook_thread && ds.batches >= 120;
    in.faulted = m.input_faulted || ds.faulted || is.window.fault_phase != 0;
    in.realized = realized;
    in.parts_loaded = parts_loaded;
    in.range_hidden = m.range_hidden;
    in.stage = set.stage;
    in.link_live = h.link_live ? h.link_live(h.context) : (h.net_running && h.net_running(h.context));
    pin::Sample latest{};
    const bool latest_ok = h.latest_input && h.latest_input(h.context, latest) && latest.valid;
    in.input_valid = latest_ok;
    in.input_age_ms = latest_ok ? latest.age_ms : 0xFFFFFFFFu;
    in.dm_idle = m.dmmap.phase == pdm::Phase::idle && !m.dmmap.owned;
    in.dm_patched = m.dm_patched;
    in.dm_revert_failed = m.dm_revert_failed;
    in.window_armed = m.input_armed;
    in.recon_placed = m.input_armed && m.recon_active;
    in.snaps_10s = m.recon_active && m.recon_have_out ? m.recon_out.report.snaps_window : 0;
    // Only a true runaway pin with an idle remote feeds the runaway timer (a
    // diverge or input pin has its own reason: diverging snaps, input_stale;
    // a moving overrun pin recovers on its own once the speeds match).
    if (m.recon_active && m.recon_have_out && m.recon_out.report.runaway_idle) {
        if (!m.runaway_since_ms) m.runaway_since_ms = now ? now : 1;
        in.runaway_ms = Clamp32(now - m.runaway_since_ms);
    } else {
        m.runaway_since_ms = 0;
    }
    const pnat::Mode before = m.native.mode;
    const pnat::Out out = pnat::Step(m.native, in);
    m.native_out = out;
    // Requests in publication order: leaving disarms the deferral first, then
    // the window, then rearms dm=map; entering arms the deferral from the
    // place step on, before TickInput may publish the native Arm (it refuses
    // with native_defer until then).
    if (out.req_defer_disarm || (!out.active && m.native_defer_armed)) {
        if (h.set_defer) h.set_defer(h.context, false, defer);
        m.native_defer_armed = false;
    }
    if (out.req_disarm) DisarmInput(d, h, "native_leave");
    if (out.req_dmmap_rearm) d.dmmap_rearm = true;
    if (out.req_epoch) ++m.body_epoch;
    if (out.req_defer_arm && !m.native_defer_armed) {
        if (h.set_defer) h.set_defer(h.context, true, defer);
        m.native_defer_armed = true;
    }
    // drive=legacy again (trigger): nothing native stays published.
    if (!configured && !out.active) WithdrawNativeTargets(m, h);
    if (out.transition && configured) {
        EmitNative(m, h, m.native_cap, "partner_native", "partner_native=%s; reason=%s; fallbacks=%u; since_ms=%u\r\n",
                   pnat::ModeName(out.mode), out.reason ? out.reason : "none", m.native.fallbacks, Clamp32(now - m.native.since_ms));
        if (out.mode == pnat::Mode::latched && !m.native_latched_logged) {
            m.native_latched_logged = true;
            EmitFormat(h, "partner_native=latched; reason=%s; fallbacks=%u\r\n", out.reason ? out.reason : "none", m.native.fallbacks);
        }
    }
    if (out.enter_step)
        EmitNative(m, h, m.native_enter_cap, "partner_native_enter", "partner_native=enter; step=%s; ms=%u\r\n", out.enter_step,
                   Clamp32(now - m.native.since_ms));
    (void)before;
    // Steering for the next Arm (entering/active only; SameArm ignores it).
    if (out.active && m.recon_active && m.recon_have_out && m.recon_target_ok && m.body_ok) {
        pnat::SteerIn si{};
        si.now_ms = now;
        si.e_x = m.recon_target[0] - static_cast<double>(m.body_pos[0]) * 1000.0;
        si.e_z = m.recon_target[2] - static_cast<double>(m.body_pos[2]) * 1000.0;
        si.v_x = m.recon.v_remote[0];
        si.v_z = m.recon.v_remote[1];
        si.in_r = static_cast<double>(m.recon_in_r) / 1000.0;
        si.frozen = m.recon_out.mode == pr::Mode::frozen || m.recon_out.mode == pr::Mode::pin;
        const bool body_prone = anim_ok && anim.stance[1] == 2;
        const bool remote_prone = (remote_anim.flags & sync::AnimFlag::Valid) && remote_anim.stance == 2;
        si.prone_transition = body_prone != remote_prone;
        si.walk_key = latest_ok && (latest.packet.held & (1u << 25)) != 0;
        pnat::SteerParams sp{};
        sp.max_deg = static_cast<double>(set.steer_deg);
        sp.trim_pct = static_cast<double>(set.trim_pct);
        const pnat::SteerOut so = pnat::Steer(m.steer, si, sp);
        m.steer_rad = so.bias_rad;
        m.mag_scale = so.mag_scale;
    } else {
        m.steer = pnat::SteerState{};
        m.steer_rad = 0.0;
        m.mag_scale = 1.0;
    }
    m.steer_mdeg = static_cast<std::int32_t>(std::lround(m.steer_rad * 180000.0 / 3.14159265358979323846));
    if (!configured) return;
    // Gun (telemetry reads only).
    pp::Gun1 gun{};
    const bool gun_ok = h.read_gun1 && h.read_gun1(h.context, st, gun);
    const bool remote_aiming = (latest_ok && (latest.packet.xflags & pin::XFlag::Aiming)) || (remote_flags & sync::ActionFlag::Aiming);
    const bool body_aiming = gun_ok && gun.node_active && (gun.gun3c0 & c::gun_flag_hold);
    if (remote_aiming == body_aiming) {
        if (!m.aim_since_ms) m.aim_since_ms = now ? now : 1;
        if (now - m.aim_since_ms >= kAimMatchMs) m.aim_match = true;
    } else {
        m.aim_since_ms = 0;
        m.aim_match = false;
    }
    const std::uint32_t gun_key = gun_ok ? (gun.gun3c0 | (gun.node_active ? 0x80000000u : 0u)) : 0xFFFFFFFFu;
    if (gun_key != m.native_gun_last || (set.ngun && now - m.native_gun_line_ms >= kNativeLineMs && m.native_gun_lines < kNativeLineCap)) {
        const bool change = gun_key != m.native_gun_last;
        m.native_gun_last = gun_key;
        if (!change) { m.native_gun_line_ms = now; ++m.native_gun_lines; }
        char line[256]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "partner_native_gun; node=%u; gun3c0=0x%x; b2=%u; b18=%u; mode=%u; remote_flags=0x%x; equip=%u,na; aim_match=%u; aim_err_mdeg=na; read=%s\r\n",
                gun_ok && gun.node_active ? 1u : 0u, gun.gun3c0, (gun.gun3c0 & c::gun_flag_in_hand) ? 1u : 0u,
                (gun.gun3c0 & c::gun_flag_hold) ? 1u : 0u, unsigned(gun.mode), remote_flags, remote_equip, m.aim_match ? 1u : 0u,
                gun_ok ? "ok" : (gun.refusal ? gun.refusal : "unavailable")))) {
            if (change) EmitNative(m, h, m.native_gun_cap, "partner_native_gun", "%s", line);
            else Emit(h, line);
        }
    }
    // Parity windows (10 s).
    const std::uint32_t body_speed = m.recon_have_out ? static_cast<std::uint32_t>(m.recon_out.report.v_free_mmps < 0 ? 0 : m.recon_out.report.v_free_mmps) : 0u;
    const std::uint32_t remote_speed = m.recon_have_out ? static_cast<std::uint32_t>(m.recon_out.report.v_remote_mmps < 0 ? 0 : m.recon_out.report.v_remote_mmps) : 0u;
    const bool remote_anim_valid = (remote_anim.flags & sync::AnimFlag::Valid) != 0;
    if (anim_ok && remote_anim_valid) {
        ++m.match_ticks;
        if (anim.stance[1] == remote_anim.stance) ++m.stance_hits;
        if ((body_speed > 500) == (remote_speed > 500)) ++m.move_hits;
    }
    if (!m.match_window_ms) m.match_window_ms = now ? now : 1;
    if (now - m.match_window_ms >= kNativeMatchWindowMs) {
        m.stance_pct = m.match_ticks ? m.stance_hits * 100u / m.match_ticks : 0u;
        m.move_pct = m.match_ticks ? m.move_hits * 100u / m.match_ticks : 0u;
        m.match_ticks = m.stance_hits = m.move_hits = 0;
        m.match_window_ms = now;
    }
    // Telemetry (every tick; rates per ~1 s window).
    pnat::Telemetry& t = m.tele;
    t.configured = configured;
    t.mode = out.mode;
    CopyText(t.reason, sizeof(t.reason), out.reason);
    CopyText(t.left_reason, sizeof(t.left_reason), m.native.left_reason);
    t.since_ms = m.native.since_ms;
    t.fallbacks = m.native.fallbacks;
    t.latched = out.mode == pnat::Mode::latched;
    // Phase-1 detour and the last windowed drain (the rates below).
    t.p1_installed = ni.p1;
    t.p1_live = ni.live_p1;
    t.drain_held = is.native.drain_held;
    t.drain_mag = is.native.drain_mag;
    t.drain_view = is.native.drain_view;
    t.drain_local_pre = is.native.drain_local_pre;
    t.drain_mask28 = is.native.drain_mask28;
    t.drain_mask2c = is.native.drain_mask2c;
    t.drain_mask30 = is.native.drain_mask30;
    t.rec_ok = is.native.rec_ok;
    t.rec_act = is.native.rec_act;
    t.rec_next = is.native.rec_next;
    t.rec_mag = is.native.rec_ok ? is.native.rec_mag : pnat::kNull;
    t.rec_heading_mrad = is.native.rec_ok ? is.native.rec_heading_mrad : pnat::kNull;
    t.setting = defer;
    t.h0 = ni.h0; t.h1 = ni.h1; t.h2 = ni.h2; t.wrap = ni.wrap;
    t.thread_ok = in.thread_ok;
    t.armed = m.native_defer_armed;
    t.batch_thread = ds.batch_thread;
    t.hook_thread = ds.hook_thread;
    t.lua_thread = static_cast<std::uint32_t>(GetCurrentThreadId());
    t.batch_threads_seen = ds.batch_threads_seen;
    t.id1_pass = ds.id1_pass;
    t.scope_unbalanced = ds.scope_unbalanced;
    t.faults = ds.faulted ? 1u : 0u;
    t.fault_code = ds.fault_code;
    CopyText(t.fault_phase, sizeof(t.fault_phase), partner_defer::FaultPhaseName(ds.fault_phase));
    if (ds.spin_max > t.spin_max) t.spin_max = ds.spin_max;
    t.look_writes = is.native.look_writes;
    t.look_skipped_rec = is.native.look_skipped_rec;
    CopyText(t.look_refused, sizeof(t.look_refused),
             set.naim == PartnerNativeAim::off ? "off" : partner_input_hook::LookRefusalName(is.native.look_last));
    t.input_live = latest_ok && latest.age_ms <= set.stale_ms;
    t.input_age_ms = latest_ok ? latest.age_ms : 0xFFFFFFFFu;
    t.input_version = latest_ok ? latest.packet.version : 0u;
    t.held = is.window.last_held;
    t.mask = EffectiveSettings(set, true).input_mask;
    t.latch27c = is.native.latch_last;
    t.lx = is.window.last_lx;
    t.ly = is.window.last_ly;
    t.cam_cdeg_remote = latest_ok ? latest.packet.cam_cdeg : pnat::kNull;
    t.cam_cdeg_host = tick.cam_cdeg;
    t.heading_err_mrad = pnat::kNull;
    if (latest_ok && (latest.packet.xflags & pin::XFlag::Heading) && is.window.last_dir != INT32_MIN) {
        const double composed = pin::Heading(static_cast<double>(tick.cam_cdeg) * 3.14159265358979323846 / 18000.0,
                                             static_cast<float>(is.window.last_lx) / 1000.0f, static_cast<float>(is.window.last_ly) / 1000.0f);
        const double err = pin::NormalizeRadians(latest.packet.heading_mrad / 1000.0 - composed);
        t.heading_err_mrad = static_cast<std::int32_t>(std::lround(err * 1000.0));
    }
    t.steer_mdeg = m.steer_mdeg;
    t.mag_scale_pct = static_cast<std::int32_t>(std::lround(m.mag_scale * 100.0));
    t.body_valid = anim_ok;
    t.act = anim_ok ? anim.act[1] : 0u;
    t.stance = anim_ok ? anim.stance[1] : 0u;
    t.speed = anim_ok ? anim.speed[1] : 0u;
    t.speed_mmps = body_speed;
    t.remote_act = remote_anim_valid ? remote_anim.act : 0u;
    t.remote_stance = remote_anim_valid ? remote_anim.stance : 0u;
    t.remote_speed = remote_anim_valid ? remote_anim.speed : 0u;
    t.remote_speed_mmps = remote_speed;
    t.stance_match_pct = m.stance_pct;
    t.move_match_pct = m.move_pct;
    CopyText(t.recon_mode, sizeof(t.recon_mode), m.recon_active && m.recon_have_out ? pr::ModeName(m.recon_out.mode) : "off");
    t.err_mm = m.recon_have_out ? Mm(m.recon_out.report.err_mm) : pnat::kNull;
    t.along_mm = m.recon_have_out ? Mm(m.recon_out.report.along_mm) : pnat::kNull;
    t.cross_mm = m.recon_have_out ? Mm(m.recon_out.report.cross_mm) : pnat::kNull;
    t.dy_mm = m.recon_have_out ? Mm(m.recon_out.report.dy_mm) : pnat::kNull;
    t.nudges = m.recon_have_out ? m.recon_out.report.nudges : 0u;
    t.snaps = m.recon_snaps_total;
    t.places = m.recon_have_out ? m.recon_out.report.places : 0u;
    t.runaway = m.recon_active && m.recon_have_out && m.recon_out.report.runaway_active;
    t.runaway_idle = m.runaway_since_ms != 0;
    t.lag = m.recon_active && m.recon_have_out && m.recon_out.report.lag;
    t.stop_grace = m.recon_active && m.recon_have_out && m.recon_out.report.stop_grace;
    t.dv_along_mmps = m.recon_have_out ? Mm(m.recon_out.report.dv_along_mmps) : pnat::kNull;
    t.dv_cross_mmps = m.recon_have_out ? Mm(m.recon_out.report.dv_cross_mmps) : pnat::kNull;

    t.gun_enabled = set.ngun;
    t.gun_read_ok = gun_ok;
    t.node_active = gun_ok && gun.node_active;
    t.gun3c0 = gun.gun3c0;
    t.mode2c1 = gun.mode;
    t.remote_flags = remote_flags;
    t.remote_equip = remote_equip;
    t.body_equip = pnat::kNull;
    t.aim_match = m.aim_match;
    t.aim_err_mdeg = pnat::kNull;
    t.manifest_rev = m.equip.have ? m.equip.manifest.revision : 0u;
    t.ammo_experiment = h.ammo_experiment;
    if (!m.rate_ok) {
        m.rate_ok = true;
        m.rate_ms_native = now;
        m.rate_defer = ds;
        m.rate_wrap = is.wrap;
        m.rate_hook = is.native;
    } else if (now - m.rate_ms_native >= kNativeLineMs) {
        const std::uint64_t dt = now - m.rate_ms_native;
        const partner_defer::Stats& r = m.rate_defer;
        t.per_s_valid = true;
        t.batches = Rate(ds.batches, r.batches, dt);
        t.w50_b0_s0 = Rate(ds.w50_b0_s0, r.w50_b0_s0, dt);
        t.w50_b1_s0 = Rate(ds.w50_b1_s0, r.w50_b1_s0, dt);
        t.w50_subs = Rate(ds.w50_subs, r.w50_subs, dt);
        t.w50_inline = Rate(ds.w50_inline, r.w50_inline, dt);
        t.ret_other = Rate(ds.ret_other, r.ret_other, dt);
        t.deferred = Rate(ds.deferred, r.deferred, dt);
        t.drains0 = Rate(ds.drains[0], r.drains[0], dt);
        t.drains1 = Rate(ds.drains[1], r.drains[1], dt);
        t.windowed = Rate(ds.windowed, r.windowed, dt);
        t.unwindowed = Rate(ds.unwindowed, r.unwindowed, dt);
        t.lost = Rate(ds.lost, r.lost, dt);
        t.gate_closed = Rate(ds.gate_closed, r.gate_closed, dt);
        t.torn = Rate(ds.torn, r.torn, dt);
        t.entry_changed = Rate(ds.entry_changed, r.entry_changed, dt);
        t.dup = Rate(ds.dup, r.dup, dt);
        t.stale_frame = Rate(is.native.views[2], m.rate_hook.views[2], dt);
        t.wrap_in_batch = Rate(is.wrap.wrap_in_batch, m.rate_wrap.wrap_in_batch, dt);
        t.svc28_b1 = Rate(is.wrap.calls1[0], m.rate_wrap.calls1[0], dt);
        t.svc28_b1_same = Rate(is.wrap.same_thread[0], m.rate_wrap.same_thread[0], dt);
        t.svc28_b1_nested = Rate(is.wrap.nested[0], m.rate_wrap.nested[0], dt);
        t.walk7_b1 = Rate(is.wrap.calls1[3], m.rate_wrap.calls1[3], dt);
        t.nested = Rate(ds.nested_batches, r.nested_batches, dt);
        t.foreign = Rate(ds.foreign_batches, r.foreign_batches, dt);
        t.unwound = Rate(ds.unwound, r.unwound, dt);
        t.drain_block = Rate(is.native.drain_block_refused, m.rate_hook.drain_block_refused, dt);
        t.drain_in_batch = Rate(is.native.drain_in_batch, m.rate_hook.drain_in_batch, dt);
        t.p1_forced = Rate(is.native.p1_forced, m.rate_hook.p1_forced, dt);
        t.p1_refused = Rate(is.native.p1_refused, m.rate_hook.p1_refused, dt);
        t.p1_windows = Rate(is.native.p1_windows, m.rate_hook.p1_windows, dt);
        t.drain_us_max = m.native_drain_us_acc;
        // The 1 Hz partner_defer line (installed deferral only) and partner_native line.
        if (ni.h2 && m.defer_lines < kNativeLineCap) {
            ++m.defer_lines;
            // Longer than EmitFormat's 512 bytes at worst: its own buffer.
            char line[1024]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "partner_defer; batches=%u; threads=%u; w50_b0=%u; w50_b1=%u; subs=%u; ret_other=%u; inline=%u; deferred=%u; drains=%u,%u; "
                "windowed=%u; unwindowed=%u; lost=%u; gate_closed=%u; torn=%u; entry_changed=%u; dup=%u; stale_frame=%u; wrap_in_batch=%u; "
                "nested=%u; foreign=%u; unwound=%u; drain_block=%u; drain_in_batch=%u; "
                "id1_pass=%u; us_max=%u; spin_max=%u; batch_thread=%u; hook_thread=%u; lua_thread=%u; faults=%u\r\n",
                ds.batches - r.batches, ds.batch_threads_seen, ds.w50_b0_s0 - r.w50_b0_s0, ds.w50_b1_s0 - r.w50_b1_s0,
                ds.w50_subs - r.w50_subs, ds.ret_other - r.ret_other, ds.w50_inline - r.w50_inline, ds.deferred - r.deferred,
                ds.drains[0] - r.drains[0], ds.drains[1] - r.drains[1], ds.windowed - r.windowed, ds.unwindowed - r.unwindowed,
                ds.lost - r.lost, ds.gate_closed - r.gate_closed, ds.torn - r.torn, ds.entry_changed - r.entry_changed, ds.dup - r.dup,
                is.native.views[2] - m.rate_hook.views[2], is.wrap.wrap_in_batch - m.rate_wrap.wrap_in_batch,
                ds.nested_batches - r.nested_batches, ds.foreign_batches - r.foreign_batches, ds.unwound - r.unwound,
                is.native.drain_block_refused - m.rate_hook.drain_block_refused, is.native.drain_in_batch - m.rate_hook.drain_in_batch,
                ds.id1_pass, m.native_drain_us_acc, t.spin_max, ds.batch_thread, ds.hook_thread, t.lua_thread, ds.faulted ? 1u : 0u)))
                Emit(h, line);
        } else if (ni.h2 && !m.defer_lines_capped) {
            m.defer_lines_capped = true;
            Emit(h, "partner_defer=capped; limit=3600\r\n");
        }
        if (m.native_lines < kNativeLineCap) {
            ++m.native_lines;
            char e1[16]{}, e2[16]{}, e3[16]{}, e4[16]{};
            const auto num = [](char (&out)[16], std::int32_t v) noexcept {
                if (v == pnat::kNull) StringCchCopyA(out, sizeof(out), "na");
                else StringCchPrintfA(out, sizeof(out), "%d", v);
            };
            num(e1, t.err_mm); num(e2, t.along_mm); num(e3, t.cross_mm); num(e4, t.heading_err_mrad);
            EmitFormat(h, "partner_native; mode=%s; err_mm=%s; along=%s; cross=%s; steer_mdeg=%d; mag_pct=%d; stance=%u,%u; speed_mmps=%u,%u; "
                          "heading_err_mrad=%s; match=%u,%u; latch=%u; look_writes=%u; left=%s\r\n",
                       pnat::ModeName(out.mode), e1, e2, e3, t.steer_mdeg, t.mag_scale_pct, t.stance, t.remote_stance, t.speed_mmps,
                       t.remote_speed_mmps, e4, t.stance_match_pct, t.move_match_pct, t.latch27c, t.look_writes,
                       t.left_reason[0] ? t.left_reason : "none");
        } else if (!m.native_lines_capped) {
            m.native_lines_capped = true;
            Emit(h, "partner_native=capped; limit=3600\r\n");
        }
        // What block 1's update did with the pad in the last windowed drain:
        // phase-1 detour calls forced/refused and drain windows (per second),
        // the injection the drain got, isLocal before the window, instance 1's
        // private PP masks, its base action record (act, next act, sampled
        // move magnitude/heading) and the body's act.
        if (m.native_drain_lines < kNativeLineCap) {
            ++m.native_drain_lines;
            char r1[16]{}, r2[16]{};
            const auto num = [](char (&o)[16], std::int32_t v) noexcept {
                if (v == pnat::kNull) StringCchCopyA(o, sizeof(o), "na");
                else StringCchPrintfA(o, sizeof(o), "%d", v);
            };
            num(r1, t.rec_mag); num(r2, t.rec_heading_mrad);
            EmitFormat(h, "partner_native_drain; p1=%u,%u,%u; p1_installed=%u; p1_live=%u; held=0x%x; mag=%d; view=%u; local_pre=0x%x; "
                          "masks=0x%x,0x%x,0x%x; rec_act=%u; rec_next=%u; rec_mag=%s; rec_heading_mrad=%s; body_act=%u\r\n",
                       t.p1_forced, t.p1_refused, t.p1_windows, t.p1_installed ? 1u : 0u, t.p1_live ? 1u : 0u, t.drain_held, t.drain_mag,
                       t.drain_view, t.drain_local_pre, t.drain_mask28, t.drain_mask2c, t.drain_mask30, t.rec_act, t.rec_next, r1, r2, t.act);
        } else if (!m.native_drain_lines_capped) {
            m.native_drain_lines_capped = true;
            Emit(h, "partner_native_drain=capped; limit=3600\r\n");
        }
        m.native_drain_us_acc = 0;
        t.spin_max = 0;
        m.rate_ms_native = now;
        m.rate_defer = ds;
        m.rate_wrap = is.wrap;
        m.rate_hook = is.native;
    }
    pnat::RingEntry e{};
    e.ms = static_cast<std::uint32_t>(clock.tick_ms);
    e.mode = static_cast<std::uint8_t>(out.mode);
    e.remote_stance = static_cast<std::uint8_t>(t.remote_stance);
    e.body_stance = static_cast<std::uint8_t>(t.stance);
    e.remote_mmps = static_cast<std::int32_t>(remote_speed);
    e.body_mmps = static_cast<std::int32_t>(body_speed);
    e.err_mm = t.err_mm;
    e.steer_mdeg = t.steer_mdeg;
    e.held = t.held;
    e.gun3c0 = gun.gun3c0;
    e.act = static_cast<std::uint16_t>(t.act);
    e.rec_act = static_cast<std::uint8_t>(t.rec_act);
    e.rec_next = static_cast<std::uint8_t>(t.rec_next);
    e.rec_mag = t.rec_mag;
    const std::uint32_t p1_delta = t.ring.count ? is.native.p1_forced - m.ring_p1_last : 0u;  // totals are process-wide
    m.ring_p1_last = is.native.p1_forced;
    e.p1_forced = static_cast<std::uint16_t>(p1_delta > 0xFFFFu ? 0xFFFFu : p1_delta);
    // The reconciliation's step of the previous tick (TickInput runs after this).
    const bool recon_on = m.recon_active && m.recon_have_out;
    e.recon_mode = recon_on ? static_cast<std::uint8_t>(m.recon_out.mode) : pnat::kReconOff;
    e.recon_action = recon_on ? static_cast<std::uint8_t>(m.recon_out.action) : pnat::kReconOff;
    e.warp = m.dmmap_prev_warp;
    pnat::Push(t.ring, e);
}
} // namespace

void Configure(Driver& d, const LabConfig& lab) noexcept {
    partner_upper_probe::Clear("configure");
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
    d.settings.dm_map = lab.partner_dm_map;
    d.settings.dm_speed = lab.partner_dm_speed;
    d.settings.holdlog = lab.partner_input_holdlog;
    d.settings.recon = lab.partner_recon;
    d.settings.align_ms = lab.partner_recon_align_ms;
    d.settings.nudge_mmps = lab.partner_recon_nudge_mmps;
    d.settings.slot = lab.partner_input_slot;
    d.settings.runaway = lab.partner_recon_runaway;
    d.settings.wrap = lab.partner_input_wrap;
    d.settings.drive = lab.partner_drive;
    d.settings.defer = lab.partner_input_defer;
    d.settings.local = lab.partner_input_local;
    d.settings.naim = lab.partner_native_aim;
    d.settings.ngun = lab.partner_native_gun;
    d.settings.nfire = lab.partner_native_fire;
    d.settings.steer_deg = lab.partner_native_steer_deg;
    d.settings.trim_pct = lab.partner_native_trim_pct;
    d.settings.play_mode = lab.mode == LabMode::play;
    d.look_forced_rev = 1;
    partner_recon::Reset(d.mission.recon);
    partner_dmmap::Reset(d.mission.dmmap);
    partner_native::Reset(d.mission.native, d.settings.drive == PartnerDrive::native);
    d.mission.mag_scale = 1.0;
}

void MissionStart(Driver& d, const Hooks& h, std::uint32_t mission_code) noexcept {
    partner_upper_probe::Clear("mission_start");
    // A missed MissionEnd/Stop must not leave the window armed or the DM
    // patch applied into the next mission: tear down before the reset, and
    // always publish a disarmed Arm. The hook's recorded fault is cleared
    // (input_faulted is per mission).
    // Native puppet: the deferral is disarmed before the window (publication
    // order); the pending records and batch flags are never touched here.
    if (h.set_defer && d.settings.drive == PartnerDrive::native)
        h.set_defer(h.context, false, EffectivePartnerDefer(d.settings.drive, d.settings.defer));
    WithdrawNativeTargets(d.mission, h);
    DisarmInput(d, h, "mission_start");
    if (d.mission.dm_patched) RevertDm(d, h, "mission_start");
    PublishDisarmed(d, h);
    if (h.reset_input_fault) h.reset_input_fault(h.context);
    d.mission = Mission{};
    partner_native::Reset(d.mission.native, d.settings.drive == PartnerDrive::native);
    d.mission.mag_scale = 1.0;
    d.mission.mission_code = mission_code;
    d.mission.range_hidden = mission_code == 30010 || mission_code == 30020;
    d.mission.range_needs_place = d.mission.range_hidden;
    d.mission.anim_source = -1;
    d.mission.last_status = pose::Status::none;
    partner_recon::Reset(d.mission.recon);
    partner_dmmap::Reset(d.mission.dmmap);
    d.dmmap_rearm = false;
    d.recon_place_request = partner_recon::PlaceReason::none;
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
    partner_upper_probe::Clear("mission_end");
    if (h.set_defer && d.mission.native_defer_armed)
        h.set_defer(h.context, false, EffectivePartnerDefer(d.settings.drive, d.settings.defer));
    d.mission.native_defer_armed = false;
    WithdrawNativeTargets(d.mission, h);
    DisarmInput(d, h, "mission_end");
    if (d.mission.dm_patched) RevertDm(d, h, "mission_end");
    d.mission.cfg_ready = false;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
    d.tap_pending = d.press_pending = d.dmplay_pending = d.dmstop_pending = d.p218_pending = false;
}

bool Cfg(Driver& d, const Hooks& h, const char* precheck, std::uint64_t now_ms) noexcept {
    partner_upper_probe::Clear("cfg");
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
    partner_upper_probe::Clear("stop");
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
    partner_upper_probe::ClearTrigger(trigger);
    Mission& m = d.mission;
    const std::uint32_t stage = trigger.stop ? 0 : trigger.has_stage ? trigger.stage : d.settings.stage;
    // Round 4: a change of the movement driver, the input mode or the pose
    // source while apply=input runs places the body again (I6 switched
    // input=stick -> net without a new placement). Compared before any
    // setting is overwritten.
    const bool replace = (trigger.has_apply && trigger.apply != d.settings.apply) ||
                         (trigger.has_input && trigger.input != d.settings.input) ||
                         (trigger.has_source && trigger.source != d.settings.source);
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
    if (replace) d.recon_place_request = partner_recon::PlaceReason::rearm;
    if (trigger.place) d.recon_place_request = partner_recon::PlaceReason::manual;
    if (trigger.has_recon) d.settings.recon = trigger.recon;
    if (trigger.has_align) d.settings.align_ms = trigger.align_ms;
    if (trigger.has_slot) d.settings.slot = trigger.slot;
    if (trigger.has_runaway) d.settings.runaway = trigger.runaway;
    if (trigger.has_wrap) d.settings.wrap = trigger.wrap;
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
    if (trigger.has_dm) {
        // Any dm= trigger retries a failed patch/revert; dm=map rearms the mapper.
        d.settings.dm = trigger.dm;
        m.dm_patch_failed = m.dm_revert_failed = false;
        m.dm_refused_state = nullptr;
        if (trigger.dm == PartnerDm::map) d.dmmap_rearm = true;
    }
    if (trigger.has_dmmap) {
        d.settings.dm_map = trigger.dmmap;
        d.dmmap_rearm = true;
    }
    if (trigger.has_dmspeed) {
        d.settings.dm_speed = trigger.dmspeed;
        d.dmmap_rearm = true;
    }
    if (trigger.has_lookgate) d.settings.look_gate = trigger.lookgate;
    if (trigger.has_lookrecover) d.settings.look_recover = trigger.lookrecover;
    if (trigger.has_holdlog) { d.settings.holdlog = trigger.holdlog; d.holdlog_changed = true; }
    // Native puppet keys. defer=on never installs by itself (the tick decides);
    // drive=native after a mission latch clears the latch.
    const bool native_keys = trigger.has_drive || trigger.has_defer || trigger.has_local || trigger.has_naim || trigger.has_ngun ||
                             trigger.has_nfire || trigger.has_steer;
    // Play mode refuses the native drive in lab.ini (kPlayRefusedNativeDrive);
    // the trigger file can never turn it on either.
    const bool native_refused = d.settings.play_mode &&
                                ((trigger.has_drive && trigger.drive != PartnerDrive::legacy) ||
                                 (trigger.has_defer && trigger.defer != PartnerDefer::off));
    if (native_refused) Emit(h, "partner_native=trigger_refused; reason=play; drive=legacy; defer=off\r\n");
    if (trigger.has_drive && !native_refused) {
        d.settings.drive = trigger.drive;
        if (trigger.drive == PartnerDrive::native) partner_native::Unlatch(m.native, m.native.since_ms);
    }
    if (trigger.has_defer && !native_refused) d.settings.defer = trigger.defer;
    if (trigger.has_local) d.settings.local = trigger.local;
    if (trigger.has_naim) d.settings.naim = trigger.naim;
    if (trigger.has_ngun) d.settings.ngun = trigger.ngun;
    if (trigger.has_nfire) d.settings.nfire = trigger.nfire;
    if (trigger.has_steer) d.settings.steer_deg = trigger.steer_deg;
    if (native_keys)
        EmitFormat(h, "partner_native=trigger; drive=%s; defer=%s; local=%s; naim=%s; ngun=%u; nfire=%u; steer=%u; mask=0x%x\r\n",
                   PartnerDriveName(d.settings.drive), PartnerDeferName(EffectivePartnerDefer(d.settings.drive, d.settings.defer)),
                   PartnerLocalName(d.settings.local), PartnerNativeAimName(d.settings.naim), d.settings.ngun ? 1u : 0u,
                   d.settings.nfire ? 1u : 0u, d.settings.steer_deg,
                   partner_native::NativeMask(d.settings.input_mask, d.settings.ngun, d.settings.nfire));
    // correct=0 keeps the round 3 meaning (no corrections) and overrides
    // recon=track|pin: say so, so a later recon= is never silently ignored.
    if ((trigger.has_recon || trigger.has_correct) && d.settings.correct_mm == 0 && d.settings.recon != PartnerRecon::off)
        EmitFormat(h, "partner_recon=off; reason=correct0; requested=%s; fix=correct=3000\r\n", PartnerReconName(d.settings.recon));
    d.settings.stage = stage;
    char words_text[4]{};
    FormatPartnerWords(d.settings.input_words, words_text);
    EmitFormat(h, "partner_stage=changed; stage=%u; source=%s; apply=%s; anim=%s; aset=%s; reset=%s; look=%s; probe=%s; "
               "lookgate=%u; lookrecover=%u; input=%s; cam=%s; hold=%s; yaw=%s; words=%s; mask=0x%x; correct=%u; isend=%u; dm=%s; "
               "recon=%s; align=%d; slot=%s; runaway=%s; wrap=%s; via=trigger\r\n",
               stage, PartnerSourceName(d.settings.source), PartnerApplyName(d.settings.apply), PartnerAnimName(d.settings.anim),
               PartnerAsetName(d.settings.aset), PartnerResetName(d.settings.reset), PartnerLookName(d.settings.look),
               d.settings.probe_ps ? "ps" : "off", d.settings.look_gate ? 1u : 0u, d.settings.look_recover ? 1u : 0u,
               PartnerInputName(d.settings.input), PartnerInputCamName(d.settings.input_cam),
               PartnerInputHoldName(d.settings.input_hold), PartnerInputYawName(d.settings.input_yaw), words_text,
               d.settings.input_mask, d.settings.correct_mm, d.settings.isend ? 1u : 0u, PartnerDmName(d.settings.dm),
               PartnerReconName(d.settings.recon), d.settings.align_ms, PartnerSlotName(d.settings.slot),
               PartnerRunawayName(d.settings.runaway), PartnerWrapName(d.settings.wrap));
    // The dm=map table has its own line (confirms dmmap=/dmspeed= in any dm mode).
    if (trigger.has_dmmap || trigger.has_dmspeed) LogDmMapTable(d.settings, h, "trigger");
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
        if (d.settings.source == PartnerSource::net && (m.mission_code == 30010 || m.mission_code == 30020))
            m.range_needs_place = true;
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
    DrainEquip(d, h);  // reliable channel 2, every tick in every drive mode
    if (!m.cfg_ready || m.stopped || terminating) { partner_upper_probe::Clear("tick_inactive"); return false; }
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
    // Native puppet: the effective settings of the arbiter's last step (the
    // configured ones unless it is entering or active).
    const Settings set_effective = EffectiveSettings(d.settings, m.native_out.active);
    const Settings& set = set_effective;
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
    bool sample_snapped = false;  // a snap-sized remote jump held, never lerped
    std::uint32_t remote_flags = 0, remote_equip = 0;  // native gun telemetry
    // dm=map: the remote (net) or the local player (orbit/fixed) as observed.
    partner_dmmap::Observation dmobs{};
    dmobs.flag_move = partner_dmmap::kMoveUnknown;
    if (set.source == PartnerSource::net) {
        const std::uint32_t delay = h.interp_ms ? h.interp_ms(h.context, lab.partner_interp_ms) : lab.partner_interp_ms;
        pose::Sample sample{};
        if (h.sample_remote_with_turns) {
            m.dmmap_source_frame = h.sample_remote_with_turns(h.context, delay, m.dmmap_source_turns.cursor);
            sample = m.dmmap_source_frame.visual;
        } else sample = h.sample_remote ? h.sample_remote(h.context, delay) : pose::Sample{};
        sample_status = sample.status;
        sample_snapped = sample.snapped;
        status = sample.status == pose::Status::live ? partner::Status::live
               : sample.status == pose::Status::stale ? partner::Status::stale : partner::Status::lost;
        if (sample.valid) {
            pose = partner::PoseFromMetres(sample.x, sample.y, sample.z, sample.yaw);
            pose_valid = true;
            age_ms = sample.age_ms;
            if (sample.anim_valid) remote_anim = sync::AnimState{sample.act, sample.anim_stance, sample.anim_speed, sync::AnimFlag::Valid};
            dmobs.stance = sample.stance;
            remote_flags = sample.flags;
            remote_equip = sample.equip_id;
            dmobs.sprint = (sample.flags & sync::ActionFlag::Sprinting) != 0;
            dmobs.gun_actions = sample.equip_id ? sample.flags & (sync::ActionFlag::Aiming | sync::ActionFlag::Firing | sync::ActionFlag::Reloading) : 0;
            // Authoritative movement flags avoid waiting for a 150 ms speed
            // window when the sender has just started walking or running.
            if (sample.flags & sync::ActionFlag::Running) dmobs.flag_move = 2;
            else if (sample.flags & sync::ActionFlag::Walking) dmobs.flag_move = 1;
        }
        dmobs.fresh = sample.valid && sample.status == pose::Status::live;
        dmobs.lost = !sample.valid || sample.status == pose::Status::lost;
        dmobs.anim_move = true;
        if (sample.valid && sample.anim_valid) {
            dmobs.anim_valid = true;
            dmobs.act = sample.act;
            dmobs.anim_stance = sample.anim_stance;
            dmobs.anim_speed = sample.anim_speed;
            // Idle-encoded acts can persist while the pose moves. Keep the
            // movement unknown so measured speed can still decide the band.
        }
    } else {
        if (set.source == PartnerSource::orbit) pose = partner::OrbitPose(tick, lab.partner_offset_cm, now - m.start_ms);
        else pose = partner::FixedPose(tick, lab.partner_offset_cm);
        pose_valid = true;
        dmobs.fresh = true;
        dmobs.stance = tick.stance;
        dmobs.sprint = tick.move == 3;
        dmobs.gun_actions = tick.equip_id ? tick.action_flags : 0;
        if (set.source == PartnerSource::fixed) dmobs.flag_move = tick.move;
        if (local_anim.flags & sync::AnimFlag::Valid) {
            dmobs.anim_valid = true;
            dmobs.act = local_anim.act;
            dmobs.anim_stance = local_anim.stance;
            dmobs.anim_speed = local_anim.speed;
        }
    }
    dmobs.yaw = static_cast<float>(pose.yaw_mrad) * 0.001f;
    // Sender: publish the local body facing (LocalBodyYaw), else vars.playerRotY.
    // Same wire field and units (centidegrees).
    float local_body_yaw = 0.0f;
    const bool local_body = LocalBodyYaw(h, st, tick, local_body_yaw);
    partner::Tick sent = tick;
    if (local_body) sent.yaw_cdeg = static_cast<std::int32_t>(std::lround(local_body_yaw * 5729.57795f));
    if (local_body != m.facing_source_body || !m.facing_source_logged) {
        m.facing_source_logged = true;
        m.facing_source_body = local_body;
        EmitCapped(m.facing_cap, h, "partner_facing", "partner_facing=source; yaw=%s\r\n", local_body ? "body" : "vars");
    }
    const bool free_roam_net = set.source == PartnerSource::net &&
                               (m.mission_code == 30010 || m.mission_code == 30020);
    if (free_roam_net) {
        if (m.realized && !pp::Realized(st, partner::partner_index)) {
            partner_upper_probe::Clear("range_unrealized");
            m.range_needs_place = true;
            m.pose_applied = m.have_cmd = false;
            if (m.bits_set) ClearBits(d, h, "range_rearm", terminating);
        }
        // Distance is a conservative streaming safety bound, not an engine
        // area query. Hysteresis prevents realization thrash at its edge.
        const std::uint32_t distance = pose_valid ? partner::DistanceMm(pose.x_mm, pose.y_mm, pose.z_mm,
                                                                         tick.x_mm, tick.y_mm, tick.z_mm) : UINT32_MAX;
        const bool fresh = pose_valid && sample_status == pose::Status::live;
        if (!m.range_hidden && pose_valid && distance >= 200000) {
            partner_upper_probe::Clear("range_hidden");
            m.range_hidden = m.range_needs_place = true;
            m.pose_applied = m.have_cmd = m.hybrid_placed = false;
            partner_dmmap::SpeedReset(m.dmmap_speed);
            partner_dmmap::CancelFiniteEvents(m.dmmap);
            m.dmmap_turn = {};
            m.dmmap_source_turns = {};
            EmitCapped(m.range_cap, h, "partner_range", "partner_range=hide; distance_mm=%u; reason=far\r\n", distance);
        }
        if (m.range_hidden) {
            DisarmInput(d, h, "range_hidden");
            PublishDisarmed(d, h);
            if (m.dm_patched) RevertDm(d, h, "range_hidden");
            // Clear only bits set by this driver; retry a refused restore.
            if (m.bits_set) ClearBits(d, h, "range_hidden", terminating);
            if (fresh && distance <= 150000 && !m.bits_set) {
                m.range_hidden = false;
                m.lost_hidden = false;
                m.lost_since_ms = 0;
                EmitCapped(m.range_cap, h, "partner_range", "partner_range=near; distance_mm=%u; next=place\r\n", distance);
            }
        }
        const auto deferred_reply = [&]() noexcept {
            // Published readiness is conservative while latched/placing.
            // Clearing realize bits does not prove the engine unloaded yet.
            m.realized = m.parts_loaded = m.body_ok = false;
            // Keep the peer informed while our local partner is latched.
            if (h.submit_local) h.submit_local(h.context, sent, local_anim);
            if (h.submit_look && looks_ok && st.local < 2 && st.parts[st.local] == c::parts_loaded_state)
                h.submit_look(h.context, looks.look[st.local]);
            Probe(d, h, st, clock);
            reply = partner::Reply{0, partner::partner_index, pose.x_mm, pose.y_mm, pose.z_mm, pose.yaw_mrad,
                                   status, age_ms, partner::Look{}, 0};
            return true;
        };
        if (m.range_hidden) return deferred_reply();
        if (m.range_needs_place) {
            if (fresh && set.stage >= 1 && !m.bits_set) {
                float xyzw[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                                 static_cast<float>(pose.z_mm / 1000.0), static_cast<float>(pose.yaw_mrad / 1000.0)};
                const char* refused = h.write_slot ? h.write_slot(h.context, terminating, xyzw) : "unavailable";
                if (refused) LogWriteRefusal(m, h, "range_place", refused);
                else {
                    m.cmd = pose;
                    m.have_cmd = m.pose_applied = true;
                    m.range_needs_place = false;
                    EmitCapped(m.range_cap, h, "partner_range", "partner_range=placed; next=realize\r\n");
                }
            }
            return deferred_reply();
        }
        // A stale packet cannot start/restart realization after placement.
        if (!fresh && !m.bits_set) return deferred_reply();
    }
    // The commanded pose's horizontal speed (orbit and net; fixed uses the flags).
    if (m.dmmap_speed_source != set.source) {
        m.dmmap_speed_source = set.source;
        partner_dmmap::SpeedReset(m.dmmap_speed);
        partner_dmmap::CancelFiniteEvents(m.dmmap);
        m.dmmap_turn = {};
        m.dmmap_source_turns = {};
    }
    const bool pose_jump = pose_valid && partner_dmmap::SpeedPush(m.dmmap_speed, now, pose.x_mm, pose.z_mm);
    if (pose_jump) {
        m.dmmap_turn = {};
        m.dmmap_source_turns = {};
        partner_dmmap::CancelFiniteEvents(m.dmmap);
    }
    dmobs.speed_ok = set.source != PartnerSource::fixed && partner_dmmap::SpeedMmps(m.dmmap_speed, dmobs.speed_mmps);
    // partner_lost=hide (lab-tested only; freeze, the default, holds the pose
    // with dm speed 0 and stays realized): after kLostHideMs of a lost net
    // sample the realize bits are cleared; the next live sample re-realizes.
    if (set.source == PartnerSource::net && lab.partner_lost == PartnerLost::hide) {
        if (sample_status == pose::Status::live) {
            m.lost_since_ms = 0;
            if (m.lost_hidden) {
                m.lost_hidden = false;
                Emit(h, "partner_lost=show\r\n");
            }
        } else if (sample_status == pose::Status::lost) {
            if (!m.lost_since_ms) m.lost_since_ms = now;
            if (!m.lost_hidden && m.bits_set && now - m.lost_since_ms >= kLostHideMs &&
                ClearBits(d, h, "lost_hide", terminating)) {
                m.lost_hidden = true;
                EmitFormat(h, "partner_lost=hide; after_ms=%u\r\n", Clamp32(now - m.lost_since_ms));
            }
        }
    }
    // Realize bits (stage 3): from the tick after the first applied pose, so
    // that S+0x20[1] is already in the area; re-ORed while lost.
    if (set.stage >= 3 && m.pose_applied && !m.lost_hidden) {
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
    const bool transform = PartnerApplyIsTransform(set.apply);
    // The body readback first: with apply=input the slot publishes it.
    if ((transform || set.apply == PartnerApply::input) && parts_loaded && h.read_body) {
        m.body_ok = h.read_body(h.context, st, m.body_pos, m.body_quat);
    } else {
        m.body_ok = false;
    }
    // lost -> live on the net source this tick (hybrid and apply=input relink).
    const bool relink = set.source == PartnerSource::net && m.last_status == pose::Status::lost && sample_status == pose::Status::live;
    // Receiver facing (partner_facing.hpp): xform/hybrid on the net source turn
    // the body toward the interpolated remote yaw at a bounded, eased rate, so
    // a 30 Hz direction step (W -> W+A) is not a one-packet snap. Position and
    // the reply/Warp pose stay exact; every lifecycle discontinuity snaps.
    partner::Pose body_pose = pose;
    auto facing_snap = partner_facing::Snap::none;
    m.facing_active = transform && set.source == PartnerSource::net && pose_valid && parts_loaded;
    if (m.facing_active) {
        facing_snap = sample_status != pose::Status::live ? partner_facing::Snap::stale
                    : pose_jump || sample_snapped        ? partner_facing::Snap::teleport
                    : relink                             ? partner_facing::Snap::relink
                    : m.facing.valid && m.facing.epoch != m.body_epoch ? partner_facing::Snap::warp
                                                         : partner_facing::Snap::none;
        m.facing_target_yaw = static_cast<float>(pose.yaw_mrad) * 0.001f;
        const std::uint32_t snaps = m.facing.snaps;
        m.facing_applied_yaw = partner_facing::Step(m.facing, m.facing_target_yaw, now,
                                                    partner_facing::ParamsForDps(lab.partner_facing_dps), facing_snap);
        facing_snap = m.facing.snaps != snaps ? m.facing.last_snap : partner_facing::Snap::none;
        m.facing.epoch = m.body_epoch;
        body_pose.yaw_mrad = pp::ToMrad(m.facing_applied_yaw);
    } else if (m.facing.valid) {
        m.facing.valid = false;  // the next applied tick snaps (first)
    }
    const bool slot = partner::WritesSlot(set.stage, set.apply, pose_valid);
    if (slot) {
        float xyzw[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                         static_cast<float>(pose.z_mm / 1000.0), static_cast<float>(body_pose.yaw_mrad / 1000.0)};
        bool write = true;
        // Round 4, apply=input: S+0x20[1] carries the body (what the game
        // publishes for a local player), so pos1/rec1 are real readbacks;
        // before the body is readable (realization) it still carries the pose.
        if (set.apply == PartnerApply::input && m.body_ok) {
            if (set.slot == PartnerSlot::body) {
                xyzw[0] = m.body_pos[0]; xyzw[1] = m.body_pos[1]; xyzw[2] = m.body_pos[2];
                xyzw[3] = partner::QuatToYaw(m.body_quat);
            } else if (set.slot == PartnerSlot::none) {
                write = false;
            }
        }
        if (write)
            if (const char* refused = h.write_slot(h.context, terminating, xyzw)) LogWriteRefusal(m, h, "slot", refused);
    }
    // Movement drivers xform / hybrid (the slot write above keeps S+0x20 as
    // Warp would). The body readback compares the previous command.
    bool transform_warp = false;
    if (relink) partner_upper_probe::Clear("relink");
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
    if (transform && set.stage >= 1 && pose_valid && (!free_roam_net || parts_loaded)) {
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
            ++m.body_epoch;
            EmitCapped(m.hybrid_cap, h, "partner_hybrid", "partner_hybrid=warp; reason=%s\r\n", hybrid_reason);
        } else {
            alignas(16) float p[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                                      static_cast<float>(pose.z_mm / 1000.0), 0.0f};
            alignas(16) float q[4]{};
            partner::YawToQuat(static_cast<float>(body_pose.yaw_mrad / 1000.0), q);
            const char* refused = m.native_disabled ? "native_disabled"
                                : !h.apply_transform ? "unavailable"
                                : h.apply_transform(h.context, terminating, p, q, set.reset);
            if (IsFault(refused)) NoteFault(m, h, "xform");
            if (refused) {
                // Free roam waits for the validated native body to recover;
                // an unrealized/parts race must not become an unchecked Warp.
                transform_warp = !free_roam_net;
                if (transform_warp) ++m.body_epoch;
                if (!m.xform_logged || m.xform_state != refused) {
                    m.xform_logged = true;
                    m.xform_state = refused;
                    EmitCapped(m.xform_cap, h, "partner_xform", "partner_xform=refused; reason=%s; fallback=%s\r\n",
                               refused, free_roam_net ? "defer" : "warp");
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
    if (set.source == PartnerSource::net) RecordFacing(m, tick, now, local_body, local_body_yaw, pose_valid, pose, facing_snap);
    // Animation application (instance 1 only, stage 3 with parts loaded). Not
    // while a dm=map clip is ours: aset=q|qs would overwrite Q+0x18[1] before
    // the mapper reads it, so its stop could never be confirmed.
    const bool anim_dm = set.anim != PartnerAnim::off && set.stage >= 3 && m.dmmap.owned;
    if (anim_dm && !m.anim_dm_logged) {
        m.anim_dm_logged = true;
        EmitCapped(m.anim_cap, h, "partner_anim", "partner_anim=deferred; reason=dm_map\r\n");
    } else if (!anim_dm) {
        m.anim_dm_logged = false;
    }
    const bool anim_needed = set.anim != PartnerAnim::off && set.stage >= 3 && !m.dmmap.owned;
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
    std::uint32_t ops = partner::OpsFor(partner::OpInput{set.stage, set.apply, pose_valid, tick.luaflags, reassert, set_parts,
                                                       d.mirror_pending, mirror_off, no_copy, transform_warp, false, false});
    if (free_roam_net && !parts_loaded) ops &= ~partner::op::warp;
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
        ++m.body_epoch;
        m.census_logged = false;
        m.dm_census_loaded = false;
        m.stance = partner::StanceDebounce{};
        // A mapped clip ends with the body: forget it (no lost line), new speed.
        partner_dmmap::Forget(m.dmmap);
        partner_dmmap::SpeedReset(m.dmmap_speed);
        m.dmmap_turn = {};
        m.dmmap_source_turns = {};
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
    if (h.submit_local) h.submit_local(h.context, sent, local_anim);
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
        m.cmd = body_pose;
        m.have_cmd = true;
        m.pose_applied = true;
    }
    // Round 3: input lever (arming, placement, correction, sender, logs) and
    // the direct-motion lever. Either may add op bits and the DM gani.
    // TickInput runs first: a trigger that turns input off and dm on (with a
    // dmplay) disarms here, so TickDm can patch and play in the same tick.
    std::uint32_t extra_ops = 0;
    std::uint8_t gani = 0;
    InputTick itick{};
    // Native puppet: the arbiter steps first; its requests (disarm on leaving,
    // dm=map rearm) and settings take effect for the input and DM levers now.
    TickNative(d, h, lab, st, tick, clock, terminating, realized, parts_loaded, anim_ok, anim, remote_anim, remote_flags, remote_equip);
    const Settings set_levers = EffectiveSettings(d.settings, m.native_out.active);
    TickInput(d, h, lab, st, set_levers, tick, clock, terminating, parts_loaded, pose_valid, pose, relink, anim_ok, anim, remote_anim,
              extra_ops, itick);
    TickDm(d, h, st, set_levers, clock, terminating, tick, dmobs, ops | extra_ops, extra_ops, gani);
    // apply=input without an armed window falls back to the proven Lua Warp
    // every tick (refused, faulted, install refused, input=off, stage < 3),
    // except while the reconciliation places/verifies the body itself.
    const partner::Pose reply_pose = itick.pose_override ? itick.pose : pose;
    if (set_levers.apply == PartnerApply::input && !m.input_armed && pose_valid && set.stage >= 1 && !itick.suppress_fallback &&
        (!free_roam_net || parts_loaded)) {
        if (!(extra_ops & partner::op::warp)) {
            extra_ops |= partner::op::warp;
            ++m.body_epoch;
            if (!m.input_fallback) {
                m.input_fallback = true;
                EmitInput(m, h, "partner_input_place; reason=fallback; err_mm=0; via=warp\r\n");
            }
        }
    } else if (m.input_armed) {
        m.input_fallback = false;
    }
    BodyLine(d, h, st, set_levers, tick, clock, parts_loaded);
    // dm=map loss attribution: this reply's look ops and Warp, seen next tick.
    const std::uint32_t final_ops = ops | extra_ops;
    m.dmmap_prev_warp = (final_ops & partner::op::warp) != 0;
    m.dmmap_prev_look = (final_ops & partner::op::set_parts_explicit) != 0 ||
                        ((final_ops & partner::op::copy_parts) && !(tick.luaflags & partner::luaflag::parts_copied)) ||
                        (final_ops & partner::op::set_all_active) != 0;
    reply = partner::Reply{ops | extra_ops, partner::partner_index, reply_pose.x_mm, reply_pose.y_mm, reply_pose.z_mm,
                           reply_pose.yaw_mrad, status, age_ms, (ops & partner::op::set_parts_explicit) ? apply_look : partner::Look{},
                           gani};
    return true;
}

} // namespace mgscoop::partner_driver
