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
#include "vehicle_mirror_state.hpp"
#include "mgscoop/partner_aim.hpp"
#include "mgscoop/partner_rates.hpp"
#include "mgscoop/partner_upper_probe.hpp"
#include "mgscoop/coop_carry.hpp"
#include "mgscoop/partner_ride_seat.hpp"
#include "mgscoop/coop_hud.hpp"
#include "partner_player_contract.hpp"

namespace mgscoop::partner_driver {
namespace {
namespace pp = partner_player;
namespace c = partner_player_contract;
namespace pam = mgscoop::partner_aim;

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
    if (!e.native_active) {
        // Native weapons: with the native drive configured, FIRE reaches
        // instance 1 only through the native gate below (visual fire with a
        // live owner filter), never through the fallback or a legacy input
        // lever a trigger armed (the gun stage's mask carries FIRE).
        if (base.drive == PartnerDrive::native) e.input_mask &= ~kPartnerNativeFireBit;
        return e;
    }
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
    // FIRE only as visual fire with the owner filter live (fire_live, set by TickWeapons).
    const bool fire = base.nfire == PartnerNativeFire::visual && base.fire_live;
    const pnat::DriveSettings x =
        pnat::Effective(ds, true, pnat::NativeConfig{base.ngun, fire, base.naim, base.nhold == PartnerNativeHold::net});
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

// Native rates (docs/native-puppet.md "Rates"): the remote's wire rates stay
// live this long after the last v4 frame carrying them; both records are read
// at most every kRatesReadMs.
constexpr std::uint64_t kRatesRemoteMs = 2000;
constexpr std::uint64_t kRatesReadMs = 100;
bool RatesRemoteLive(const Mission& m, std::uint64_t now) noexcept {
    return m.rates_remote_ok && now >= m.rates_remote_ms && now - m.rates_remote_ms <= kRatesRemoteMs;
}
partner_rates::Record RatesRecord(const pp::RateRead& r) noexcept {
    partner_rates::Record out{};
    for (unsigned i = 0; i < partner_rates::kFields; ++i) out.f[i] = r.f[i];
    return out;
}
std::int32_t RateMilli(float value) noexcept {
    if (!std::isfinite(value) || value > 2000000.0f || value < -2000000.0f) return INT32_MIN;
    return static_cast<std::int32_t>(std::lround(static_cast<double>(value) * 1000.0));
}

// The sender's v4 groups: the local player's own movement rates (the peer
// mirrors them; none with partner_native_rates=off) and its magazine (the
// local instance's gun record: a gun class with an equip id, the magazine as
// its getter reads it and the capacity).
pin::V4Source SenderV4(const Driver& d, const Hooks& h, const pp::State& st, const pin::PadSnapshot& snap, std::uint8_t kb_flags) noexcept {
    pin::V4Source v{};
    // The pad mode the peer's trigger needs (PP+0x488 bit 6 = keyboard mode,
    // PP vt+0x98; read outside windows like the rest of the pad).
    v.gamepad = (kb_flags & 0x40u) == 0;
    v.v3 = SenderV3(d, h, st, snap);
    if (d.equip_tx_have) v.v3.equip_rev = d.equip_tx.revision;
    if (d.settings.nrates != PartnerNativeRates::off) {
        partner_rates::Wire w{};
        v.rates_valid = d.mission.rates_id0_ok && partner_rates::ToWire(RatesRecord(d.mission.rates_id0), w);
        v.rate_dash = w.dash;
        v.rate_crawl = w.crawl;
        v.rate_crawl_factor = w.crawl_factor;
    }
    // The local gun record TickNative read this tick (drive=native: it always runs first).
    (void)st;
    const pp::Gun1& g = d.mission.gun0;
    if (d.mission.gun0_ok && g.fields_ok && pnat::GunClass(g.fields.flags27c) == pnat::kGunClassGun && g.fields.equip != 0) {
        v.mag_cap = pnat::GunCapacity(g.fields);
        v.mag = pnat::GunMagazine(g.fields);
        v.mag_valid = v.mag_cap >= 1 && v.mag_cap <= pin::kMaxMagCap && v.mag <= v.mag_cap;
    }
    // The gun the local player holds (the peer's weapon gate).
    if (d.mission.gun0_ok && g.fields_ok && g.fields.equip >= 1 && g.fields.equip <= 0xFFFFu) {
        v.equip_valid = true;
        v.equip = g.fields.equip;
    }
    return v;
}

// Input frame v5 (docs/native-puppet.md "Weapons" R2/W1): v4 plus the local
// player's gun group (its record read by TickNative this tick: in hand,
// aiming, reloading, changing, the mode rec+0x2C0 and the reload/switch
// counters partner_reload::SenderStep advanced) and its current slot (row+0x36).
pin::V5Source SenderV5(const Driver& d, const Hooks& h, const pp::State& st, const pin::PadSnapshot& snap, std::uint8_t kb_flags) noexcept {
    pin::V5Source v{};
    v.v4 = SenderV4(d, h, st, snap, kb_flags);
    const Mission& m = d.mission;
    if (m.slots0_ok && m.slots0.nibble <= pin::kInvSlotMax) v.v4.v3.inv_slot = m.slots0.nibble;
    const pp::Gun1& g = m.gun0;
    if (m.gun0_ok && g.fields_ok && m.sender_gun.have) {
        v.gun_valid = true;
        std::uint8_t flags = 0;
        if (g.gun3c0 & c::gun_flag_in_hand) flags |= pin::GunFlag::InHand;
        if (g.gun3c0 & c::gun_flag_hold) flags |= pin::GunFlag::Aiming;
        if (partner_reload::ReloadMode(g.mode_cur)) flags |= pin::GunFlag::Reloading;
        if (g.mode_cur == c::gun_mode_change || (g.gun3c0 & c::gun3c0_switch)) flags |= pin::GunFlag::Changing;
        v.gun_flags = flags;
        v.gun_mode = g.mode_cur;
        v.reload_seq = m.sender_gun.reload_seq;
        v.switch_seq = m.sender_gun.switch_seq;
    }
    return v;
}

pam::GunDirIn GunDirectionInput(const pp::AimRead& r, unsigned k) noexcept {
    pam::GunDirIn i{};
    i.gun_ok = r.gun_ok[k]; i.rate_ok = r.rate_ok[k]; i.body_ok = r.body_ok[k];
    i.gun_class = r.gun_class[k]; i.stance = r.stance[k]; i.b3d5 = r.b3d5[k]; i.gun3c0 = r.gun_flags[k];
    i.r5c = r.r5c[k]; i.r60 = r.r60[k]; i.body_yaw = r.body_yaw[k];
    return i;
}
pin::V6Source SenderV6(Driver& d, const Hooks& h, const pp::State& st, const pin::V5Source& v5) noexcept {
    pin::V6Source v{};
    v.v5 = v5;
    Mission& m = d.mission;
    pam::DirReason why = pam::DirReason::idle;
    if (v5.gun_valid && (v5.gun_flags & pin::GunFlag::Aiming)) {
        pp::AimRead r{};
        pam::GunDirIn gi{};
        if (h.read_aim && h.read_aim(h.context, st, r)) gi = GunDirectionInput(r, 0);
        const pam::GunDirOut dir = pam::GunDir(gi);
        why = dir.reason;
        if (dir.ok) {
            pam::CheckStep(m.dir_check, dir, gi, r.body[0], r.aimpt[0]);
            if (m.dir_check.latched) why = pam::DirReason::convention;
            else {
                v.aim_valid = true;
                v.aim_pitch_cdeg = pam::PitchCdeg(dir.pitch);
                v.aim_yaw_cdeg = pam::YawCdeg(dir.yaw);
            }
        }
    }
    if (!m.dir_reason_logged || why != m.dir_reason) {
        m.dir_reason_logged = true; m.dir_reason = why;
        EmitFormat(h, "aim_dir=%s; reason=%s; checks=%u; misses=%u\r\n",
                   v.aim_valid ? "valid" : why == pam::DirReason::idle ? "idle" : "refused",
                   pam::DirReasonName(why), m.dir_check.checks, m.dir_check.misses);
    }
    return v;
}

// One overlay tap (the input window injects `bits` for kTapFrames windows):
// a new tap_seq, or, for a second tap in the same tick (a reload tap and a
// stance fix), the bits ORed into this tick's tap.
void BumpTap(Driver& d, Mission& m, std::uint32_t bits, std::uint64_t now) noexcept {
    if (m.tap_bumped && m.tap_bump_ms == now) {
        m.tap_bits |= bits;
        return;
    }
    ++d.tap_seq;
    m.tap_bits = bits;
    m.tap_bump_ms = now;
    m.tap_bumped = true;
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
    // Online play: the stall hold only under the native drive.
    a.config.stall_ms = set.native_active && set.nstall == PartnerNativeStall::hold ? set.stall_ms : 0u;
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
    // Native rates: mirror the remote's rates into instance 1's own record
    // (validated on this tick, re-validated in the drain window), or restore.
    const bool rates_record = m.rates_id1_ok && m.rates_id1.sp && m.rates_id1.rec;
    a.rates_mirror = set.native_active && set.nrates == PartnerNativeRates::mirror && rates_record && RatesRemoteLive(m, now);
    a.rates_sp = rates_record ? m.rates_id1.sp : 0;
    a.rates_rec = rates_record ? m.rates_id1.rec : 0;
    a.rates_id = rates_record ? m.rates_id1.id : 0;
    a.rates_epoch = d.rates_epoch;
    for (unsigned i = 0; i < 3; ++i) a.rates_remote[i] = m.rates_remote[i];
    // Native weapons: the magazine mirror on instance 1's gun record (gun
    // stage only; the hook re-validates the prim chain before a write).
    a.mag_mirror = set.native_active && set.ngun && m.gun1_rec != 0 && m.gun1_prim != 0;
    a.gun_prim = a.mag_mirror ? m.gun1_prim : 0;
    a.gun_rec = a.mag_mirror ? m.gun1_rec : 0;
    // The weapon gate (F4/W5): FIRE and RELOAD stripped while the remote's gun
    // is not body 1's or body 1 is mid-switch, FIRE alone for an unqualified type.
    a.strip = set.native_active ? m.weapon_strip : 0u;
    // Drop DASH only while a live raw HOLD was actually stripped. Admitted
    // combat HOLD and the weapon gate remain unchanged.
    if (set.native_active && m.hold_stripped) a.strip |= partner_pad::kDash;
    // The partner's own reload and weapon switch (docs/native-puppet.md
    // "Weapons"; not compared by SameArm): the mirror's freeze inputs, the
    // remote's raw RELOAD/CARRY stripped from its sample with reload=event and a
    // live v5 group (its reloads then arrive as overlay taps), and the slot
    // follower with the plan's admitted slots (play runs call as write).
    const bool gun_stage = set.native_active && set.ngun;
    a.reload = gun_stage ? set.nreload : PartnerNativeReload::mirror;
    a.reload_hold = gun_stage && m.reload_out.hold;
    a.reload_reconcile = gun_stage && m.reload_out.reconcile;
    a.reload_release = gun_stage && m.reload_out.release;
    a.config.sample_strip = gun_stage && set.nreload == PartnerNativeReload::event && m.remote_v5 ? pnat::kPadReload : 0u;
    a.slot_follow = !gun_stage ? PartnerNativeSlot::off
                    : (set.play_mode && set.nslot == PartnerNativeSlot::call) ? PartnerNativeSlot::write
                                                                              : set.nslot;
    a.slot_ok = m.slot_plan_ok ? m.slot_plan.ok_mask : static_cast<std::uint8_t>(0);
    for (unsigned s = 0; s < 3; ++s) a.slot_equip[s] = m.slot_plan_ok ? m.slot_plan.equip[s] : static_cast<std::uint16_t>(0);
    // Weapons4: a remote slot body 1 follows with the plan's stand-in.
    a.standin_mask = 0;
    for (unsigned r = 0; r < 3; ++r) {
        a.standin_slot[r] = static_cast<std::uint8_t>(r);
        if (m.slot_plan_ok && m.slot_plan.map[r] < 3 && m.slot_plan.map[r] != r) {
            a.standin_mask = static_cast<std::uint8_t>(a.standin_mask | (1u << r));
            a.standin_slot[r] = m.slot_plan.map[r];
        }
    }
    // Anim round 1: the state-driven aim intent (the hook adds HOLD per window,
    // partner_slots AimFrame) and the reload rate factor (0: no rate write).
    a.aim_intent = set.native_active && set.ngun && m.aim_gate_ok;
    a.aim_act_until_ms = a.aim_intent && m.aim_act ? now + kAimActHoldMs : 0u;
    a.reload_rate_k = gun_stage && set.nreload_rate == PartnerNativeReloadRate::adapt && !m.rate_adapt.latched ? m.rate_adapt.k : 0.0f;
    (void)st;
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
        m.input_neutral = false;
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
    if (h.native_pose_suppressed && h.native_pose_suppressed(h.context)) return;
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
    if(h.native_pose_suppressed && h.native_pose_suppressed(h.context)) return;
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
                             kRcFix, kRcInputPin, kRcLag, kRcFamilies };
static_assert(kRcFamilies == sizeof(Mission::recon_cap) / sizeof(LogCap), "one cap per recon family");
const char* ReconFamilyName(unsigned family) noexcept {
    static const char* const names[] = {"place", "placed", "snap", "runaway", "frozen", "stance", "yaw", "stop_check", "diverging", "refused",
                                        "stance_fix", "input_pin", "lag"};
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

pr::FaceMode FaceModeFor(PartnerNativeFace face) noexcept {
    switch (face) {
    case PartnerNativeFace::off: return pr::FaceMode::off;
    case PartnerNativeFace::aim: return pr::FaceMode::aim;
    default: return pr::FaceMode::idle;
    }
}
// Milliradians to millidegrees.
std::int32_t Mdeg(double mrad) noexcept { return Mm(mrad * 57.29577951308232); }

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
        p = pr::NativeParams(p, FaceModeFor(set.nface));
        p.recon = PartnerRecon::track;
        if (!set.stance_fast) {
            // partner_native_stance_fast=0: the previous stance rule (2 s, no guards).
            const pr::Params legacy{};
            p.stance_fix_ms = legacy.stance_fix_ms;
            p.stance_fix_gap_ms = legacy.stance_fix_gap_ms;
            p.stance_pad_quiet_ms = p.stance_body_stable_ms = p.stance_settle_ms = p.stance_state_settle_ms = 0;
        }
    }
    return p;
}

// Stance level guards (docs/anim-round1-actions.md, partner_recon Params
// stance_*): the native gun stage and a carry hold the corrective STANCE input
// (a stance change cancels a reload, a weapon change or an aim), and a downed
// remote (the coop_hud teammate view, copied from the world Lua owner) is
// mirrored lying. Static reasons; nullptr while nothing holds it.
const char* StanceBlock(const Mission& m, const Settings& set, std::uint8_t remote_flags) noexcept {
    if (!set.native_active) return nullptr;
    if (m.carry_hold) return "carry";
    if (!set.ngun) return nullptr;
    if (m.reload_out.hold || m.reload_out.reconcile) return "reload";
    if (m.body_switching) return "switch";
    if (m.remote_v5 && (m.remote_gun_flags & (pin::GunFlag::Aiming | pin::GunFlag::Reloading | pin::GunFlag::Changing))) return "gun";
    if (remote_flags & (sync::ActionFlag::Aiming | sync::ActionFlag::Firing | sync::ActionFlag::Reloading)) return "gun";
    return nullptr;
}
bool RemoteDowned(std::uint64_t tick_ms) noexcept {
    coop_hud::DownedView v{};
    return coop_hud::GetDowned(v) && v.teammateDown && tick_ms >= v.publish_ms && tick_ms - v.publish_ms <= 1500;
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
    m.hold_stripped = set.native_active && latest_ok && latest.age_ms <= set.stale_ms &&
        (latest.packet.held & partner_pad::kHold) != 0 &&
        (!(set.input_mask & partner_pad::kHold) || (m.weapon_strip & partner_pad::kHold) != 0);
    in.hold_stripped = m.hold_stripped;
    m.recon_in_valid = latest_ok;
    m.recon_in_age_ms = in.in_age_ms;
    m.recon_in_r = static_cast<std::int32_t>(std::lround(in.in_r * 1000.0));
    // Target: the remote pose aligned with the input being consumed (net), or
    // the synthetic pose (fixed/orbit).
    m.recon_delay_ms = 0;
    m.recon_pose_age_ms = 0;
    std::uint8_t remote_speed = remote_anim.speed;
    bool remote_sprinting = remote_speed == 3;
    std::uint8_t remote_action_flags = 0;  // sync::ActionFlag of the sampled remote pose (stance guards)
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
        in.target_time_valid = sample.valid && sample.time_valid;
        in.target_sender_ms = sample.sender_ms;
        in.target_jump = sample.jump;
        in.target_jump_ms = sample.jump_ms;
        in.target_generation = sample.generation;
        in.remote_anim_valid = sample.anim_valid;
        in.remote_act = sample.act;
        in.remote_stance = sample.anim_stance;
        remote_speed = sample.anim_speed;
        remote_sprinting = (sample.flags & sync::ActionFlag::Sprinting) != 0;
        remote_action_flags = sample.flags;
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
    // Stance level guards: STANCE in the remote's live input (held, its "now"
    // word, the edge set, a short tap or a long hold), the gun/carry gate and a
    // downed remote (native only; the legacy rule has no guards).
    in.stance_pad = latest_ok && latest.age_ms <= set.stale_ms &&
                    ((latest.packet.held | latest.packet.w4c | latest.packet.w50 | latest.packet.tap12 | latest.packet.long12) &
                     kStanceBit) != 0;
    in.stance_block = set.stance_fast ? StanceBlock(m, set, remote_action_flags) : nullptr;
    in.remote_downed = set.native_active && set.stance_fast && RemoteDowned(clock.tick_ms);
    const pr::Out out = pr::Step(m.recon, in, ReconParams(set));
    pnat::DashIn di{};
    di.now_ms = in.now_ms;
    di.active = set.native_active && out.arm && !out.neutral && out.mode == pr::Mode::track;
    di.input_live = latest_ok && latest.age_ms <= set.stale_ms && in.target_live;
    di.mask_dash = (set.input_mask & partner_pad::kDash) != 0 && !(m.weapon_strip & partner_pad::kDash);
    di.hold_stripped = m.hold_stripped;
    di.body_ok = in.body_anim_ok; di.remote_ok = in.remote_anim_valid;
    di.body_act = in.body_act; di.remote_act = in.remote_act;
    di.body_stance = in.body_stance; di.remote_stance = in.remote_stance;
    di.body_speed = anim_ok ? anim.speed[1] : 0;
    di.remote_speed = remote_speed;
    di.remote_sprinting = remote_sprinting;
    di.in_r = in.in_r;
    m.dash_fix = pnat::DashRelatch(m.dash, di);
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
                  "pose_age_ms=%u; in_age_ms=%s; tries=%u; source_jump=%u; body_jump=%u; sender_span_ms=%u\r\n",
                  pr::PlaceReasonName(o.place_reason), Mm(o.pos[0]), Mm(o.pos[1]), Mm(o.pos[2]), Mm(o.yaw * 1000.0), body,
                  m.body_ok ? Mm(r.err_mm) : -1, m.recon_delay_ms, m.recon_pose_age_ms, age, m.recon.tries,
                  r.source_jump ? 1u : 0u, r.body_jump ? 1u : 0u, r.sender_span_ms);
    }
    if (ev & pr::Event::placed_ok) {
        m.place_yaw_err_mdeg = Mdeg(r.place_yaw_err_mrad);
        EmitRecon(m, h, kRcPlaced, "partner_recon=placed; ok=1; err_mm=%d; yaw_err_mdeg=%d; tries=%u; ms=%u\r\n", Mm(r.place_err_mm),
                  m.place_yaw_err_mdeg, r.place_tries, r.place_ms);
    }
    if (ev & pr::Event::place_yaw_fix)
        EmitRecon(m, h, kRcPlaced, "partner_recon=place_yaw_fix; yaw_err_mdeg=%d; err_mm=%d; via=xform\r\n", Mdeg(r.place_yaw_err_mrad),
                  Mm(r.place_err_mm));
    // The facing governor (one cap with the legacy yaw line).
    if (ev & pr::Event::face_start)
        EmitRecon(m, h, kRcYaw, "partner_recon=face_start; mode=%s; err_mdeg=%d; rate_dps=%d; act=%u\r\n",
                  pr::FaceStateName(r.face_state), Mdeg(r.face_err_mrad), Mm(r.face_rate_dps), anim_ok ? unsigned(anim.act[1]) : 0u);
    if (ev & pr::Event::face_end)
        EmitRecon(m, h, kRcYaw, "partner_recon=face_end; reason=%s; err_mdeg=%d; writes=%u\r\n", r.face_reason ? r.face_reason : "none",
                  Mdeg(r.face_err_mrad), r.face_writes);
    if (ev & pr::Event::face_snap)
        EmitRecon(m, h, kRcYaw, "partner_recon=face_snap; err_mdeg=%d; act=%u; snaps=%u\r\n", Mdeg(r.face_err_mrad),
                  anim_ok ? unsigned(anim.act[1]) : 0u, r.face_snaps);
    if (ev & pr::Event::face_contest)
        EmitRecon(m, h, kRcYaw, "partner_recon=face_contest; err_mdeg=%d; contests=%u; backoff_ms=1000\r\n", Mdeg(r.face_err_mrad),
                  r.face_contests);
    if (ev & pr::Event::face_latched)
        EmitRecon(m, h, kRcYaw, "partner_recon=face_latched; contests=%u; off_ms=10000\r\n", r.face_contests);
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
        EmitRecon(m, h, kRcLag, "partner_recon=lag; v_free_mmps=%d; v_remote_mmps=%d; dv_along_mmps=%d; err_mm=%d; action=nudge_along\r\n",
                  Mm(r.v_free_mmps), Mm(r.v_remote_mmps), Mm(r.dv_along_mmps), Mm(r.err_mm));
    if (ev & pr::Event::lag_end) EmitRecon(m, h, kRcLag, "partner_recon=lag_end; err_mm=%d\r\n", Mm(r.err_mm));
    if (ev & pr::Event::pin_end) EmitRecon(m, h, kRcDiverge, "partner_recon=pin_end\r\n");
    if (ev & pr::Event::frozen) EmitRecon(m, h, kRcFrozen, "partner_recon=frozen; reason=%s\r\n", pr::FrozenName(r.frozen));
    if (ev & pr::Event::unfrozen) EmitRecon(m, h, kRcFrozen, "partner_recon=unfrozen\r\n");
    if (ev & pr::Event::stance)
        EmitRecon(m, h, kRcStance, "partner_recon=stance; from=%u; to=%u; suspend_ms=%u\r\n", unsigned(r.stance_from),
                  unsigned(r.stance_to), r.suspend_ms);
    if (ev & pr::Event::stance_mismatch)
        EmitRecon(m, h, kRcStance, "partner_recon=stance_mismatch; remote=%u; body=%u; ms=%u\r\n", unsigned(m.recon.stance_applied),
                  anim_ok ? unsigned(anim.stance[1]) : 0u, r.mismatch_ms);
    if (ev & pr::Event::stance_wait)
        EmitRecon(m, h, kRcStance, "partner_recon=stance_wait; reason=%s; remote=%u; body=%u; ms=%u\r\n", r.fix_wait ? r.fix_wait : "none",
                  unsigned(r.fix_remote), unsigned(r.fix_body), r.mismatch_ms);
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
    // A facing write (governor step/snap, placement yaw fix): never while a
    // batch is in flight, and a refusal only skips it (no placement).
    const bool face = o.action == pr::Action::face;
    const char* refused = nullptr;
    if (m.native_disabled) {
        refused = "native_disabled";
    } else if (nudge) {
        refused = h.apply_position ? h.apply_position(h.context, terminating, pos) : "unavailable";
    } else if (face && h.in_batch && h.in_batch(h.context)) {
        refused = "in_batch";
    } else {
        alignas(16) float q[4]{};
        partner::YawToQuat(static_cast<float>(o.yaw), q);
        refused = h.apply_transform ? h.apply_transform(h.context, terminating, pos, q, PartnerReset::none) : "unavailable";
    }
    if (IsFault(refused)) NoteFault(m, h, nudge ? "recon_nudge" : face ? "recon_face" : "recon_xform");
    if (refused) {
        if (m.recon_refused != refused) {
            m.recon_refused = refused;
            EmitRecon(m, h, kRcRefused, "partner_recon=refused; action=%s; reason=%s; fallback=%s\r\n", pr::ActionName(o.action),
                      refused, nudge || face ? "none" : "place");
        }
        // A refused xform places the body with a Warp on the next tick.
        if (!nudge && !face) d.recon_place_request = pr::PlaceReason::rearm;
        return false;
    }
    m.recon_refused = nullptr;
    // Small facing steps keep the body epoch (same position); the start and
    // end of a turn, a snap and a placement yaw fix bump it.
    if (!nudge && (!face || o.face_epoch)) ++m.body_epoch;
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

// Sending the viewer's own controls is independent of the remote body's
// visibility. No target resolution, hook install, Arm or body effects here.
void SendLocalInput(Driver& d, const Hooks& h, const pp::State& st, const Settings& set, const partner::Tick& tick,
                    const Clock& clock, bool refresh_local) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    // Sender: the local pad, every tick, independent of arming (stage >= 1).
    // Input frame v2: every tick's snapshot feeds the edge sets (press/release
    // edges, 12-frame taps and holds), sent or not.
    if (set.isend && set.stage >= 1 && h.read_pad && h.submit_input) {
        pin::PadRaw raw{};
        if (h.read_pad(h.context, st, raw)) {
            if (refresh_local) {
                // Same checked local sources as TickNative/TickOnline; never
                // touch instance1 while its visibility gate is holding it.
                m.gun0 = pp::Gun1{};
                m.gun0_ok = h.read_gun && h.read_gun(h.context, st, 0, m.gun0);
                m.slots0 = pp::Slots{};
                m.slots0_ok = h.read_slots && h.read_slots(h.context, st, 0, m.slots0);
                partner_reload::SenderStep(m.sender_gun, m.gun0_ok && m.gun0.fields_ok, m.gun0.mode_cur,
                                          m.slots0_ok ? m.slots0.nibble : static_cast<std::uint8_t>(0xFF));
                if (d.settings.nrates == PartnerNativeRates::off || !h.read_rates) {
                    m.rates_id0_ok = false; m.rates_id0 = pp::RateRead{};
                } else if (!m.rates_read_ms || now-m.rates_read_ms >= kRatesReadMs) {
                    m.rates_read_ms = now ? now : 1;
                    m.rates_id0_ok = h.read_rates(h.context, st, 0, m.rates_id0);
                }
            }
            const pin::PadSnapshot snap = pin::Effective(raw);
            pin::SenderObserve(m.sender, snap);
            m.sender.repeats = static_cast<std::uint8_t>(d.settings.edge_repeats > pin::kEdgeRepeatsMax ? pin::kEdgeRepeatsMax
                                                                                                     : d.settings.edge_repeats);
            pin::InputPacket pkt = pin::Quantize(snap, tick.cam_cdeg);
            pin::SenderFill(m.sender, pkt);
            // Native puppet: v4 (look, camera, heading, the sender's own movement
            // rates, its magazine and gun) whatever this copy's own drive is: the
            // peer's native drive needs it even when this copy shows clips
            // (online play; all reads are pure, see TickNative's sender sources).
            // v5 adds the gun group (reload and weapon switch); a peer build
            // without v5 (0.11.1) refuses the whole 116-byte frame, so v5 goes
            // out only once the peer sent v5 (plus a probe every kInputV5ProbeMs
            // so a v5 peer can learn ours).
            {
                const pin::V5Source v5 = SenderV5(d, h, st, snap, raw.kb_flags);
                const bool peer_v5 = !h.peer_input_v5 || h.peer_input_v5(h.context);
                if (peer_v5 && h.peer_input_v5 && !m.v5_peer_logged) {
                    m.v5_peer_logged = true;
                    EmitFormat(h, "partner_input=v5_peer; probes=%u\r\n", m.v5_probes);
                }
                const bool probe = !peer_v5 && (!m.v5_probe_ms || now - m.v5_probe_ms >= kInputV5ProbeMs);
                const bool peer_v6 = h.peer_input_v6 && h.peer_input_v6(h.context);
                const bool probe6 = h.peer_input_v6 && !peer_v6 && (!m.v6_probe_ms || now - m.v6_probe_ms >= kInputV5ProbeMs);
                if (peer_v6 || probe6) pin::Fill6(pkt, SenderV6(d, h, st, v5));
                else if (peer_v5 || probe) pin::Fill5(pkt, v5);
                else pin::Fill4(pkt, v5.v4);
                if (peer_v6 && !m.v6_peer_logged) {
                    m.v6_peer_logged = true;
                    EmitFormat(h, "partner_input=v6_peer; probes=%u\r\n", m.v6_probes);
                }
                if (probe6) { m.v6_probe_ms = now ? now : 1; ++m.v6_probes; }
                if (probe) {
                    m.v5_probe_ms = now;
                    ++m.v5_probes;
                }
            }
            if (h.submit_input(h.context, pkt)) pin::SenderSent(m.sender);
        }
    }
}

void SendHiddenLocalInput(Driver& d, const Hooks& h, const pp::State& st, const Settings& set,
                          const partner::Tick& tick, const Clock& clock, bool terminating) noexcept {
    if (terminating || !d.mission.cfg_ready || d.mission.stopped || st.local != 0 ||
        !pp::Realized(st, 0) || st.parts[0] != c::parts_loaded_state ||
        !h.link_session || !h.link_session(h.context) || !h.link_live || !h.link_live(h.context)) return;
    SendLocalInput(d, h, st, set, tick, clock, true);
}

bool VehicleJobCurrent(const Driver& d,const Hooks& h,const pp::State& st,bool terminating) noexcept {
    const auto& m=d.mission;
    return !terminating&&!m.input_faulted&&!m.native_disabled&&m.input_armed&&m.have_arm&&m.native_out.active&&
        m.native_defer_armed&&d.settings.drive==PartnerDrive::native&&h.native_vehicle_job&&
        h.native_vehicle_job(h.context,st,m.last_arm);
}
bool VehicleAttached(const Hooks& h,const pp::State& st) noexcept {
    return h.native_vehicle_attached&&h.native_vehicle_attached(h.context,st);
}
void TickInput(Driver& d, const Hooks& h, const LabConfig& lab, const pp::State& st, const Settings& set, const partner::Tick& tick,
               const Clock& clock, bool terminating, bool parts_loaded, bool pose_valid, const partner::Pose& pose, bool relink,
               bool anim_ok, const pp::Anim& anim, const sync::AnimState& remote_anim, std::uint32_t& ops, InputTick& out) noexcept {
    Mission& m = d.mission;
    const std::uint64_t now = clock.now_ms;
    (void)parts_loaded;
    // A real current lease (including phase6 cleanup after wire expiry), or
    // fresh authenticated mount intent, keeps the exact already-admitted Arm.
    // Hook composition still expires stale input; no drive sample is refreshed.
    if(VehicleJobCurrent(d,h,st,terminating)){
        SendLocalInput(d,h,st,set,tick,clock,false);
        out.suppress_fallback=VehicleAttached(h,st);
        return;
    }
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
    SendLocalInput(d, h, st, set, tick, clock, false);
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
    // A teammate carried by the local player (stock player carry, coop_carry):
    // the native attachment owns instance 1's transform and stance. No
    // placement, correction, stance fix or fallback Warp until it is put down.
    const bool carry_hold = recon_used && mgscoop::coop_carry::PartnerCarryHold();
    if (carry_hold) {
        out.suppress_fallback = true;
        if (!m.carry_hold) EmitRecon(m, h, kRcFrozen, "partner_recon=carry_hold; on=1\r\n");
        m.carry_hold = true;
    } else if (m.carry_hold) {
        m.carry_hold = false;
        EmitRecon(m, h, kRcFrozen, "partner_recon=carry_hold; on=0\r\n");
    }
    if (recon_used && !carry_hold) {
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
    if (recon_used && !carry_hold && ro.stance_fix != pr::StanceFix::none && arm_now) {
        const bool hold = ro.stance_fix == pr::StanceFix::hold;
        if (!(set.input_mask & kStanceBit)) {
            EmitRecon(m, h, kRcFix, "partner_recon=stance_fix; refused=mask; remote=%u; body=%u\r\n", unsigned(ro.report.fix_remote),
                      unsigned(ro.report.fix_body));
        } else {
            if (hold) {
                m.press_bits = kStanceBit;
                m.press_until_ms = now + kStanceFixHoldMs;
            } else {
                BumpTap(d, m, kStanceBit, now);
            }
            ++m.recon_fixes;
            EmitRecon(m, h, kRcFix, "partner_recon=stance_fix; remote=%u; body=%u; via=%s; ms=%u; fixes=%u; after_ms=%u\r\n",
                      unsigned(ro.report.fix_remote), unsigned(ro.report.fix_body), pr::StanceFixName(ro.stance_fix),
                      hold ? kStanceFixHoldMs : static_cast<std::uint32_t>(pin::kTapFrames), m.recon_fixes, ro.report.mismatch_ms);
        }
    }
    if (m.dash_fix && arm_now && set.native_active && !m.hold_stripped &&
        (set.input_mask & partner_pad::kDash) && !(m.weapon_strip & partner_pad::kDash)) {
        BumpTap(d, m, partner_pad::kDash, now);
        EmitRecon(m, h, kRcFix, "partner_recon=dashfix; act=%u; speed=%u; attempts=%u\r\n",
                  anim_ok ? unsigned(anim.act[1]) : 0u, anim_ok ? unsigned(anim.speed[1]) : 0u, m.dash.attempts);
    }
    // One-shot acceptance (only while about to be armed and within the mask).
    if (d.tap_pending) {
        d.tap_pending = false;
        if (!arm_now) Emit(h, "partner_input=tap_refused; reason=not_armed\r\n");
        else if (d.tap_bits_pending & ~set.input_mask) Emit(h, "partner_input=tap_refused; reason=mask\r\n");
        else {
            BumpTap(d, m, d.tap_bits_pending, now);
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
    // Native placements keep an enabled window with a neutral pad (block 1's
    // deferred job never runs unwindowed while the body is placed/verified).
    const bool neutral = recon_used && ro.neutral && set.native_active;
    if (!keep_arm) {
        pin::Arm arm = BuildArm(d, set, targets, st, tick, now, true);
        arm.holdlog = lab.partner_input_holdlog && set.holdlog;
        arm.neutral = neutral;
        if (!m.have_arm || !pin::SameArm(arm, m.last_arm)) ++m.input_gen;
        arm.gen = m.input_gen;
        if (h.publish_arm) h.publish_arm(h.context, arm);
        m.last_arm = arm;
        m.have_arm = true;
    }
    if (!keep_arm && neutral != m.input_neutral) {
        EmitInput(m, h, "partner_input=neutral; on=%u; recon=%s\r\n", neutral ? 1u : 0u, pr::ModeName(ro.mode));
        m.input_neutral = neutral;
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
// EmitNative with its own limit (anim round 1 families).
void EmitNativeN(const Hooks& h, LogCap& cap, unsigned limit, const char* head, const char* format, ...) noexcept {
    if (cap.lines >= limit) {
        if (!cap.capped) {
            cap.capped = true;
            EmitFormat(h, "%s=capped; limit=%u\r\n", head, limit);
        }
        return;
    }
    char line[512]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) { ++cap.lines; Emit(h, line); }
}

// Reliable channel 2 (equipment manifest): drained every tick in every drive
// mode so the sender's window never stalls; the receiver only stores it.
void DrainEquip(Driver& d, const Hooks& h, std::uint64_t now) noexcept {
    if (!h.drain_reliable && !h.drain_reliable_origin) return;
    Mission& m = d.mission;
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    for (unsigned i = 0; i < 32; ++i) {
        std::uint8_t message[128]{};
        std::size_t size = 0;
        partner_net::ReliableOrigin origin{};
        const bool authenticated=h.drain_reliable_origin!=nullptr;
        const bool drained=authenticated?h.drain_reliable_origin(h.context,partner_equip_wire::kChannel,message,sizeof(message),size,origin):
            h.drain_reliable(h.context,partner_equip_wire::kChannel,message,sizeof(message),size);
        if(!drained)break;
        if(size==vehicle_mirror_state::wireSize&&message[0]=='V'){
            if(authenticated&&h.receive_vehicle)h.receive_vehicle(h.context,origin,message,size);
            continue;
        }
        // Teammate carry events (coop_carry, first byte 0x4B) share channel 2.
        if (coop_carry::IsCarryMessage(message, size)) {
            coop_carry::Receive(message, size);
            continue;
        }
        if (partner_ride_wire::IsRide(message, size)) {
            if (partner_ride_wire::Accept(d.ride_rx, message, size, epoch, now)) {
                // Heartbeats repeat the revision: only a new state or revision is logged.
                const auto& p = d.ride_rx.packet;
                if (!d.ride_rx_logged_have || d.ride_rx_logged_epoch != epoch || d.ride_rx_logged.revision != p.revision ||
                    !partner_ride_wire::SameState(d.ride_rx_logged, p)) {
                    d.ride_rx_logged = p;
                    d.ride_rx_logged_have = true;
                    d.ride_rx_logged_epoch = epoch;
                    EmitCapped(m.ride_cap, h, "partner_ride", "partner_ride=rx; kind=%u; rev=%u; epoch=%u; v=%u; seat=%u; vehicle=%u\r\n",
                               unsigned(p.kind), unsigned(p.revision), epoch, unsigned(p.version), unsigned(p.seat), unsigned(p.vehicle));
                }
            } else if (!d.ride_invalid_logged && (d.ride_rx.invalid || d.ride_rx.stale)) {
                d.ride_invalid_logged = true;
                Emit(h, "partner_ride=refused; reason=packet_or_session\r\n");
            }
            continue;
        }
        // Online play: the capability message shares channel 2 (first byte 0x43).
        if (partner_caps_wire::IsCaps(message, size)) {
            if (partner_caps_wire::Accept(d.caps_rx, message, size, epoch) &&
                (d.caps_rx.caps.epoch != d.caps_logged_epoch || d.caps_rx.caps.revision != d.caps_logged_rev)) {
                const partner_caps_wire::Caps& c = d.caps_rx.caps;
                const bool changed_view = !d.caps_logged_epoch || c.drive != d.caps_logged_drive || c.reason != d.caps_logged_reason;
                d.caps_logged_epoch = c.epoch;
                d.caps_logged_rev = c.revision;
                if (changed_view) {
                    d.caps_logged_drive = c.drive;
                    d.caps_logged_reason = c.reason;
                    EmitNative(m, h, m.net_cap, "partner_caps",
                               "partner_caps=rx; drive=%s; reason=%s; profile=%u; policy=%s; features=0x%02x; v4=%u; epoch=%u; rev=%u\r\n",
                               partner_native_net::DriveName(static_cast<partner_native_net::Drive>(c.drive)),
                               partner_native_net::ReasonName(c.reason), unsigned(c.profile), c.policy ? "online" : "lab",
                               unsigned(c.features), (c.features & partner_caps_wire::Feature::InputSender) ? 1u : 0u, c.epoch,
                               unsigned(c.revision));
                }
            }
            continue;
        }
        const bool accepted = partner_equip_wire::Accept(m.equip, message, size, epoch);
        if (accepted) {
            d.equip_cache = m.equip.manifest;
            d.equip_cache_have = true;
            d.equip_cache_epoch = epoch;
            // New content (also under the stored revision number: a 0.12.0 sender
            // restarts at 1 every mission): the per-revision slot types and apply
            // verdict are recomputed (revisions are never 0).
            m.slot_types_rev = 0;
            m.equip_apply_rev = 0;
        }
        if (accepted) {
            m.equip_logged_rev = m.equip.manifest.revision;
            const auto& e = m.equip.manifest;
            EmitNative(m, h, m.native_equip_cap, "partner_equip",
                       "partner_equip=manifest; rev=%u; inv=%u; slots=%u:%u,%u:%u,%u:%u; type=%u,%u,%u; v=%u; mission=%u\r\n",
                       unsigned(e.revision), unsigned(e.inv_slot), unsigned(e.slots[0].base_id), unsigned(e.slots[0].alias),
                       unsigned(e.slots[1].base_id), unsigned(e.slots[1].alias), unsigned(e.slots[2].base_id), unsigned(e.slots[2].alias),
                       unsigned(e.slots[0].type), unsigned(e.slots[1].type), unsigned(e.slots[2].type), unsigned(e.version),
                       m.mission_code);
        }
    }
}

void SendRide(Driver& d, const Hooks& h, const partner::Tick& tick, std::uint64_t now) noexcept {
    namespace rw = partner_ride_wire;
    if (!tick.has_ride || !h.send_reliable_append) return;
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    if (!epoch || !(h.link_live && h.link_live(h.context))) return;
    const auto kind = static_cast<rw::Kind>(tick.ride_kind);
    // Version 2 (a vehicle's native seat and GameObject id) only for a vehicle;
    // a seat or vehicle change is a new revision, sent at once.
    rw::Packet want = d.ride_tx;
    want.kind = kind;
    const bool vehicle = kind == rw::Kind::vehicle && tick.has_vehicle;
    want.seat = vehicle && tick.seat <= rw::kSeatMax ? tick.seat : rw::kSeatUnknown;
    want.vehicle = vehicle && tick.vehicle != rw::kVehicleUnknown ? tick.vehicle : rw::kVehicleUnknown;
    if (!d.ride_tx_have || !rw::SameState(d.ride_tx, want)) {
        d.ride_tx.kind = want.kind;
        d.ride_tx.seat = want.seat;
        d.ride_tx.vehicle = want.vehicle;
        d.ride_tx.revision = static_cast<std::uint16_t>(d.ride_tx.revision + 1);
        if (!d.ride_tx.revision) d.ride_tx.revision = 1;
        d.ride_tx_have = d.ride_tx_dirty = true;
    }
    if (!d.ride_tx_dirty && d.ride_tx_epoch == epoch && now >= d.ride_tx_ms && now-d.ride_tx_ms < rw::kHeartbeatMs) return;
    auto packet = d.ride_tx;
    packet.sender_ms = static_cast<std::uint32_t>(now);
    std::uint8_t bytes[rw::kSize]{};
    if (rw::Encode(packet, bytes) && h.send_reliable_append(h.context, rw::kChannel, bytes, sizeof(bytes))) {
        d.ride_tx_epoch = epoch; d.ride_tx_ms = now; d.ride_tx_dirty = false;
    }
}

// The loadout sender (docs/native-puppet.md "Loadout mirroring"): this copy's
// manifest (pure reads, at most once per kEquipReadMs), a new revision on
// change, resent after a new link session. Driver-level: it outlives missions
// and also runs outside them (the title and the ACC, from the observer's
// ~100 ms path), so the receiver has it before its first allocation. Nothing
// without a link session, for a copy that neither drives natively nor sends
// input frames, or for a loadout without any weapon (vars not loaded yet).
// Reads vars and the type tables only (never partner state).
void SendEquip(Driver& d, const Hooks& h, std::uint64_t now) noexcept {
    namespace ew = partner_equip_wire;
    Mission& m = d.mission;
    const Settings& set = d.settings;
    if ((set.drive != PartnerDrive::native && !set.isend) || !h.read_loadout || !h.send_reliable) return;
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    if (!epoch) return;
    if (!d.equip_read_ms || now - d.equip_read_ms >= kEquipReadMs) {
        d.equip_read_ms = now ? now : 1;
        pp::Loadout lo{};
        if (h.read_loadout(h.context, lo) && lo.ok && (lo.weapons[0] || lo.weapons[1] || lo.weapons[2])) {
            ew::Manifest mf{};
            mf.inv_slot = ew::kInvSlotUnknown;
            for (unsigned i = 0; i < ew::kSlots; ++i) {
                mf.slots[i].base_id = lo.base[i];
                mf.slots[i].alias = lo.weapons[i];
                mf.slots[i].type = ew::GunType(lo.type[i]) ? lo.type[i] : static_cast<std::uint8_t>(0);
                std::memcpy(mf.slots[i].recipe, lo.chimera + i * ew::kRecipe, ew::kRecipe);
            }
            d.equip_tables_ok = lo.tables_ok;
            if (!d.equip_tx_have || !ew::SameSlots(mf, d.equip_tx)) {
                mf.revision = static_cast<std::uint16_t>(d.equip_tx_have && d.equip_tx.revision != 0xFFFFu ? d.equip_tx.revision + 1 : 1);
                d.equip_tx = mf;
                d.equip_tx_have = true;
                d.equip_tx_dirty = true;
                d.equip_loadout = lo;
                // How each carried slot's base id came about (once per revision).
                for (unsigned i = 0; i < ew::kSlots; ++i)
                    if (lo.weapons[i])
                        EmitNative(m, h, d.equip_tx_cap, "partner_weapon",
                                   "partner_equip_base; rev=%u; slot=%u; alias=%u; type=%u; receiver=%u; alias_receiver=%u; base=%u; "
                                   "scan=%s; tables=%u\r\n",
                                   unsigned(mf.revision), i, unsigned(lo.weapons[i]), unsigned(mf.slots[i].type),
                                   unsigned(lo.chimera[i * ew::kRecipe]), unsigned(lo.alias_receiver[i]), unsigned(lo.base[i]),
                                   lo.scan[i] ? lo.scan[i] : "none", lo.tables_ok ? 1u : 0u);
            }
        }
    }
    if (d.equip_tx_have && epoch != d.equip_tx_epoch) d.equip_tx_dirty = true;
    if (!d.equip_tx_have || !d.equip_tx_dirty) return;
    ew::Manifest out = d.equip_tx;
    out.epoch = epoch;
    std::uint8_t bytes[ew::kSize]{};
    if (!ew::Encode(out, bytes) || !h.send_reliable(h.context, ew::kChannel, bytes, sizeof(bytes))) return;
    d.equip_tx_dirty = false;
    d.equip_tx_epoch = epoch;
    const auto& s = d.equip_tx.slots;
    EmitNative(m, h, d.equip_tx_cap, "partner_weapon",
               "partner_equip=sent; rev=%u; epoch=%u; base=%u,%u,%u; alias=%u,%u,%u; receiver=%u,%u,%u; type=%u,%u,%u; tables=%u; "
               "mission=%u\r\n",
               unsigned(d.equip_tx.revision), epoch, unsigned(s[0].base_id), unsigned(s[1].base_id), unsigned(s[2].base_id),
               unsigned(s[0].alias), unsigned(s[1].alias), unsigned(s[2].alias), unsigned(s[0].recipe[0]), unsigned(s[1].recipe[0]),
               unsigned(s[2].recipe[0]), unsigned(s[0].type), unsigned(s[1].type), unsigned(s[2].type), d.equip_tables_ok ? 1u : 0u,
               m.mission_code);
}

std::uint32_t Rate(std::uint32_t now, std::uint32_t before, std::uint64_t dt_ms) noexcept {
    return dt_ms ? Clamp32(static_cast<std::uint64_t>(now - before) * 1000u / dt_ms) : 0u;
}

void CopyText(char* out, std::size_t capacity, const char* text) noexcept {
    StringCchCopyA(out, capacity, text ? text : "");
}

// The static "noise_<why>" reason for a noise filter refusal.
const char* NoiseReason(const char* why) noexcept {
    if (!why) return nullptr;
    if (std::strcmp(why, "bytes") == 0) return "noise_bytes";
    if (std::strcmp(why, "slot") == 0) return "noise_slot";
    if (std::strcmp(why, "disabled") == 0) return "noise_disabled";
    if (std::strcmp(why, "changed") == 0) return "noise_changed";
    return "noise";
}

// The weapon gate's view of the remote's gun record equip: a customized
// weapon's alias maps to the base id of the received manifest slot that
// carries it (the sender's own loadout); a stock id, an alias no slot names
// or no manifest yet stays itself (then compared raw: a mismatch masks).
std::uint32_t RemoteGunBase(const Mission& m, std::uint32_t equip) noexcept {
    if (m.equip.have)
        for (const partner_equip_wire::Slot& s : m.equip.manifest.slots)
            if (s.alias == equip && s.base_id != 0) return s.base_id;
    return equip;
}
// The remote's current slot (0..2; 3 unknown): its live v5 slot, else the
// received manifest's slot whose alias (or base id) is its in-hand equip.
std::uint8_t RemoteCurrentSlot(const Mission& m, std::uint32_t equip) noexcept {
    if (m.remote_v5 && m.remote_slot < 3) return m.remote_slot;
    if (m.equip.have && equip != 0)
        for (std::uint8_t s = 0; s < 3; ++s)
            if (m.equip.manifest.slots[s].alias == equip || m.equip.manifest.slots[s].base_id == equip) return s;
    return 3;
}

// --- Native weapons (docs/native-puppet.md "Weapons") ---------------------------
// The fire gate (visual fire only while the owner filter is live), the
// mirror's gun record, the 1 Hz partner_fire line, the read-only census, the
// loadout manifest this copy sends and the apply verdict of the received one.
// `ready`: the lazy-install preconditions of the native drive hold.
void TickWeapons(Driver& d, const Hooks& h, const pp::State& st, bool ready, bool gun_ok, const pp::Gun1& gun,
                 const partner_input_hook::NativeStats& ns, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    Settings& set = d.settings;
    // Instance 1's gun class and the mirror's record. The verdict latches once
    // a gun class was seen this mission (body 1 only ever carries stock46): a
    // holster/draw or an unreadable tick never toggles the effective mask (a
    // mask change bumps the Arm gen and resets instance 1's private pad
    // masks). The record is the last readable gun-class one; the hook
    // re-validates it and refuses a non-gun class at write time.
    m.body_gun_mode = gun_ok && gun.fields_ok ? gun.mode_cur : static_cast<std::uint8_t>(0xFF);
    if (gun_ok && gun.fields_ok) {
        m.weapon_known = true;
        if (pnat::GunClass(gun.fields.flags27c) == pnat::kGunClassGun) {
            m.weapon_ok = true;
            m.gun1_prim = gun.prim;
            m.gun1_rec = gun.rec;
        }
    }
    const char* why = nullptr;
    partner_fire_filter::Stats fs{};
    const bool have_stats = h.fire_stats != nullptr;
    if (have_stats) h.fire_stats(h.context, fs);
    const std::uint16_t owner = st.ids[1];
    if (set.nfire != PartnerNativeFire::visual) why = "off";
    else if (!set.ngun) why = "gun_off";
    else if (!h.install_fire || !have_stats) why = "unavailable";
    else if (!fs.installed && !ready) why = "not_ready";
    // A player id distinct from the local instance's (whose shots must never be dropped).
    else if (owner == 0xFFFFu || (owner & 0xFE00u) != 0 || owner == st.ids[0]) why = "owner";
    // No install before body 1 showed a gun class: a body that cannot fire
    // (a build without the stock46 writer) never gets the vtable patch.
    else if (!m.weapon_known) why = "gun_read";
    else if (!m.weapon_ok) why = "weapon";
    else if (const char* refused = h.install_fire(h.context, owner)) why = refused;
    else {
        h.fire_stats(h.context, fs);
        if (!fs.live) why = "hook_lost";
        else if (!fs.armed || fs.owner != owner) why = "disarmed";
        // F3: the partner's shot noise is dropped on this machine too (FIRE
        // stays masked unless the noise filter is live; noise=keep is a lab key).
        else if (set.nnoise == PartnerNativeNoise::filter) {
            if (!h.install_noise) why = "noise_unavailable";
            else if (const char* noise = h.install_noise(h.context)) why = NoiseReason(noise);
            else {
                h.fire_stats(h.context, fs);
                if (!fs.noise_live) why = "noise_lost";
            }
        }
    }
    set.fire_live = why == nullptr;
    // F4: the weapon gate. Body 1 carries one gun (stock46); while the
    // remote's gun record holds another weapon, FIRE and RELOAD are stripped
    // in every native window and the magazine mirror is frozen. A customized
    // weapon's record holds its alias (the lab save's back slot: alias 873 of
    // base 46), so the remote's equip is compared as the stock id of the
    // received manifest slot carrying it. Kept while either side is unread.
    bool weapon_judged = false;
    m.weapon_standin = false;
    m.standin_remote_slot = m.standin_body_slot = partner_slots::kNoSlot;
    m.remote_gun_base = m.remote_gun_equip == pnat::kNull
                            ? pnat::kNull : static_cast<std::int32_t>(RemoteGunBase(m, static_cast<std::uint32_t>(m.remote_gun_equip)));
    // W5: body 1 mid-switch (rec+0x3C0 bit 7 or mode 8) and its in-hand type
    // (the equip type table; stock46's type 4 is pinned by the writer contract,
    // descriptor 0x6244, when the table is unread).
    m.body_switching = gun_ok && ((gun.gun3c0 & c::gun3c0_switch) != 0 || (gun.fields_ok && gun.mode_cur == c::gun_mode_change));
    std::uint8_t body_type = 0;
    const bool body_type_ok = gun_ok && gun.fields_ok && gun.fields.equip != 0 && h.equip_type && h.equip_type(h.context, gun.fields.equip, body_type);
    m.body_type = body_type_ok ? static_cast<std::int32_t>(body_type)
                  : (gun_ok && gun.fields_ok && gun.fields.equip == 46) ? 4 : pnat::kNull;
    if (gun_ok && gun.fields_ok && gun.fields.equip != 0 && m.remote_gun_equip != pnat::kNull) {
        partner_slots::GateIn gi{};
        gi.remote_equip = static_cast<std::uint32_t>(m.remote_gun_equip);
        gi.remote_base = static_cast<std::uint32_t>(m.remote_gun_base);
        gi.body_equip = gun.fields.equip;
        // W5: the plan's substitute (tier sub) in the remote's current slot counts as its weapon:
        // the live v5 slot, else the manifest slot carrying the remote's in-hand equip (alias or base).
        if (m.slot_plan_ok && m.equip.have) {
            const std::uint8_t r = RemoteCurrentSlot(m, static_cast<std::uint32_t>(m.remote_gun_equip));
            gi.substitute = r < 3 && m.slot_plan.tier[r] == partner_slots::Tier::sub && m.slot_plan.equip[r] == gun.fields.equip;
            const auto s = r < 3 ? m.slot_plan.map[r] : partner_slots::kNoSlot;
            gi.standin = set.nstandin == PartnerNativeStandin::on && r < 3 && s < 3 && s != r &&
                m.slot_plan.tier[r] != partner_slots::Tier::same && m.slot_plan.tier[r] != partner_slots::Tier::sub &&
                m.slot_plan.equip[s] == gun.fields.equip;
            if(gi.standin) {m.weapon_standin=true;m.standin_remote_slot=r;m.standin_body_slot=s;}
        }
        gi.switching = m.body_switching;
        gi.type_ok = m.body_type != pnat::kNull;
        gi.body_type = gi.type_ok ? static_cast<std::uint8_t>(m.body_type) : static_cast<std::uint8_t>(0);
        m.weapon_gate = set.ngun ? partner_slots::Judge(gi) : partner_slots::Gate::match;
        weapon_judged = true;
    }
    // Anim round 1, A1: the unsupported-equipment gate. The remote's in-hand
    // equip type (read once per equip: the raw id, an alias through the type
    // getter's alias path, else the manifest base's) and its live v5 slot; a
    // support weapon, an item or the bionic arm in the remote's hands strips HOLD,
    // FIRE and RELOAD (0x58) whatever body 1 holds.
    if (m.remote_gun_equip == pnat::kNull || m.remote_gun_equip == 0) {
        m.remote_type_equip = 0;
        m.remote_equip_type = pnat::kNull;
    } else if (m.remote_type_equip != m.remote_gun_equip) {
        m.remote_type_equip = m.remote_gun_equip;
        std::uint8_t t = 0;
        const bool raw_ok = h.equip_type && h.equip_type(h.context, static_cast<std::uint32_t>(m.remote_gun_equip), t) && t != 0;
        const bool base_ok = !raw_ok && m.remote_gun_base != pnat::kNull && m.remote_gun_base != m.remote_gun_equip && h.equip_type &&
                             h.equip_type(h.context, static_cast<std::uint32_t>(m.remote_gun_base), t) && t != 0;
        m.remote_equip_type = raw_ok || base_ok ? static_cast<std::int32_t>(t) : pnat::kNull;
    }
    {
        partner_slots::UnsupportedIn ui{};
        ui.slot_valid = m.remote_v5 && m.remote_slot <= pin::kInvSlotMax;
        ui.slot = m.remote_slot;
        ui.type_ok = m.remote_equip_type != pnat::kNull;
        ui.type = ui.type_ok ? static_cast<std::uint8_t>(m.remote_equip_type) : static_cast<std::uint8_t>(0);
        m.unsupported = set.ngun && set.nunsupported ? partner_slots::JudgeUnsupported(ui) : partner_slots::Unsupported::no;
    }
    if (m.unsupported != partner_slots::Unsupported::no) {
        m.weapon_gate = partner_slots::Gate::unsupported;
        m.weapon_standin = false;
        m.standin_remote_slot = m.standin_body_slot = partner_slots::kNoSlot;
    } else if (!weapon_judged && m.weapon_gate == partner_slots::Gate::unsupported) {
        // The remote put the equipment away while body 1 is unread: FIRE and
        // RELOAD stay stripped (mismatch) until the next readable tick judges.
        m.weapon_gate = partner_slots::Gate::mismatch;
    }
    m.tele.weapon_gate_current = weapon_judged;
    if (!set.ngun) m.weapon_gate = partner_slots::Gate::match;
    m.weapon_mismatch = m.weapon_gate == partner_slots::Gate::mismatch;
    m.weapon_strip = partner_slots::StripBits(m.weapon_gate);
    // Gun profile (online play, docs/native-puppet.md "Online"): until body 1
    // shows a gun class this mission (no gun on it: another mission, a build
    // without the gun pieces, the writer still waiting), HOLD, RELOAD and
    // FIRE are stripped too, so the partner never aims empty-handed.
    m.weapon_absent = set.ngun && !m.weapon_ok;
    if (m.weapon_absent) m.weapon_strip |= kPartnerNativeGunBits | kPartnerNativeFireBit;
    if (set.ngun && (!m.weapon_absent_logged || m.weapon_absent != m.weapon_logged_absent)) {
        m.weapon_absent_logged = true;
        m.weapon_logged_absent = m.weapon_absent;
        EmitNative(m, h, m.weapon_cap, "partner_weapon", "partner_weapon=%s; body_gun=%s; strip=0x%x\r\n",
                   m.weapon_absent ? "absent" : "present", m.weapon_absent ? "absent" : "present", m.weapon_strip);
    }
    // Play: any leak of instance 1's job into the viewer's save-backed vars (a
    // shot leak: vars shoot count; an ammo leak: the weapon/ammo vars block,
    // with or without the mirror, which also turns itself off: mag.leak_off;
    // process totals) strips FIRE and RELOAD for the rest of the process, so
    // neither runs again against a co-op save that persists.
    if (set.play_mode && !d.leak_strip && (ns.shot_leaks || ns.ammo_leaks || ns.mag_leak_off)) {
        d.leak_strip = pnat::kPadFire | pnat::kPadReload;
        EmitFormat(h, "partner_weapon=fire_reload_off; reason=%s; shot_leaks=%u; ammo_leaks=%u; strip=0x%x; scope=process\r\n",
                   ns.shot_leaks ? "shot_leak" : "ammo_leak", ns.shot_leaks, ns.ammo_leaks, d.leak_strip);
    }
    m.weapon_strip |= d.leak_strip;
    // Anim round 1: the auto-fire stop (partner_slots DryStep). Instance 1's
    // non-local shots never spend its magazine, so once the remote's magazine
    // has read 0 for kDryGraceMs (its last round's FIRE edge still passes) FIRE
    // is stripped until a reload starts or the magazine reads above 0.
    {
        const bool remote_reloading = m.remote_v5 && ((m.remote_gun_flags & pin::GunFlag::Reloading) != 0 ||
                                                       partner_reload::ReloadMode(m.remote_gun_mode));
        const bool was = m.dry_strip;
        const bool active = set.ngun && set.ndry && set.nfire == PartnerNativeFire::visual;
        m.dry_strip = active && partner_slots::DryStep(m.dry, partner_slots::DryIn{now, m.remote_mag_ok, m.remote_mag, remote_reloading});
        if (!active) { m.dry.dry = false; m.dry.zero_ms = 0; }
        if (m.dry_strip) m.weapon_strip |= pnat::kPadFire;
        if (m.dry_strip != was)
            EmitNativeN(h, m.dry_cap, kAnimWeaponCap, "partner_fire_dry",
                        "partner_fire=%s; remote_mag=%u; remote_cap=%u; reason=%s; stops=%u; strip=0x%x\r\n",
                        m.dry_strip ? "dry_stop" : "dry_clear", unsigned(m.remote_mag), unsigned(m.remote_mag_cap),
                        m.dry_strip ? "remote_empty" : remote_reloading ? "reload" : !m.remote_mag_ok ? "no_mag" : m.remote_mag ? "mag" : "off",
                        m.dry.stops, m.weapon_strip);
    }
    // Anim round 1: the unsupported line (own cap, on change of the key).
    {
        const std::uint64_t key = m.unsupported == partner_slots::Unsupported::no ? 0u
            : (1ull | (static_cast<std::uint64_t>(m.unsupported) << 1) | (static_cast<std::uint64_t>(m.remote_slot) << 4) |
               (static_cast<std::uint64_t>(static_cast<std::uint32_t>(m.remote_gun_equip)) << 12) |
               (static_cast<std::uint64_t>(static_cast<std::uint8_t>(m.remote_equip_type)) << 44));
        if (key != m.unsupported_key || !m.unsupported_logged) {
            const bool was = m.unsupported_logged && m.unsupported_key != 0;
            m.unsupported_logged = true;
            m.unsupported_key = key;
            if (key || was)
                EmitNativeN(h, m.unsupported_cap, kAnimWeaponCap, "partner_weapon_unsupported",
                            "partner_weapon=%s; remote_slot=%u; remote_equip=%d; type=%d; reason=%s; body_equip=%u; strip=0x%x\r\n",
                            key ? "unsupported" : "unsupported_end", unsigned(m.remote_slot), m.remote_gun_equip, m.remote_equip_type,
                            partner_slots::UnsupportedName(m.unsupported), gun_ok && gun.fields_ok ? gun.fields.equip : 0u,
                            m.weapon_strip);
        }
    }
    if(m.weapon_ms_at && now>=m.weapon_ms_at && now-m.weapon_ms_at<=250u) {
        const auto dt=Clamp32(now-m.weapon_ms_at);
        if(m.weapon_standin) m.standin_ms=Clamp32(static_cast<std::uint64_t>(m.standin_ms)+dt);
        if(m.weapon_absent) m.absent_ms=Clamp32(static_cast<std::uint64_t>(m.absent_ms)+dt);
    }
    m.weapon_ms_at=now;
    const std::uint32_t standin_key=m.weapon_standin ?
        1u | (std::uint32_t(m.standin_remote_slot)<<1) | (std::uint32_t(m.standin_body_slot)<<3) | (gun.fields.equip<<5) : 0u;
    if(!m.weapon_standin_logged || m.standin_key!=standin_key) {
        const bool previous=m.weapon_standin_logged && m.standin_key!=0;
        m.weapon_standin_logged=true;m.standin_key=standin_key;
        if(m.weapon_standin || previous) {
            EmitNative(m,h,m.weapon_cap,"partner_weapon",
                "partner_weapon=%s; remote_equip=%d; remote_slot=%u; body_slot=%u; body_equip=%u; body_type=%d; strip=0x%x\r\n",
                m.weapon_standin ? "standin" : "standin_end",m.remote_gun_equip,unsigned(m.standin_remote_slot),
                unsigned(m.standin_body_slot),gun.fields.equip,m.body_type,m.weapon_strip);
            if(m.weapon_standin && set.nfire==PartnerNativeFire::visual)
                EmitNative(m,h,m.weapon_cap,"partner_weapon","partner_fire=%s; reason=weapon_standin\r\n",
                    set.fire_live && !m.weapon_strip ? "live" : "masked");
        }
    }
    // An unqualified weapon type (FIRE alone stripped): its own capped line on change.
    const bool type_masked = m.weapon_gate == partner_slots::Gate::projectile;
    if (set.ngun && weapon_judged && m.weapon_gate != partner_slots::Gate::switching && m.weapon_gate != partner_slots::Gate::unsupported &&
        (!m.weapon_type_logged_once || m.weapon_type_logged != type_masked) && (type_masked || m.weapon_type_logged_once)) {
        m.weapon_type_logged_once = true;
        m.weapon_type_logged = type_masked;
        EmitNative(m, h, m.weapon_cap, "partner_weapon", "partner_weapon=%s; body_equip=%u; body_type=%d; strip=0x%x\r\n",
                   type_masked ? "projectile" : "type_ok", gun.fields.equip, m.body_type, m.weapon_strip);
    }
    if (set.ngun && weapon_judged && m.weapon_gate != partner_slots::Gate::switching && m.weapon_gate != partner_slots::Gate::unsupported &&
        (!m.weapon_logged_once || m.weapon_logged != m.weapon_mismatch)) {
        const bool first = !m.weapon_logged_once;
        m.weapon_logged_once = true;
        m.weapon_logged = m.weapon_mismatch;
        EmitNative(m, h, m.weapon_cap, "partner_weapon", "partner_weapon=%s; remote_equip=%d; remote_base=%d; body_equip=%u; strip=0x%x\r\n",
                   m.weapon_mismatch ? "mismatch" : "match", m.remote_gun_equip, m.remote_gun_base, gun.fields.equip, m.weapon_strip);
        // The fire verdict line of a visual-fire run (a first match is not a change).
        if (set.nfire == PartnerNativeFire::visual && (m.weapon_mismatch || !first))
            EmitNative(m, h, m.weapon_cap, "partner_weapon", "partner_fire=%s; reason=%s; remote_equip=%d; body_equip=%u\r\n",
                       m.weapon_mismatch ? "masked" : "live", m.weapon_mismatch ? "weapon_mismatch" : "weapon_match", m.remote_gun_equip,
                       gun.fields.equip);
    }
    m.fire_refusal = why;
    const bool same_verdict = m.fire_logged_once && (m.fire_logged == why || (m.fire_logged && why && std::strcmp(m.fire_logged, why) == 0));
    if (set.nfire == PartnerNativeFire::visual && !same_verdict) {
        m.fire_logged_once = true;
        m.fire_logged = why;
        EmitNative(m, h, m.weapon_cap, "partner_weapon", "partner_fire=%s; reason=%s; owner=%u; installed=%u; live=%u; armed=%u\r\n",
                   why ? "masked" : "live", why ? why : "ok", unsigned(owner), fs.installed ? 1u : 0u, fs.live ? 1u : 0u,
                   fs.armed ? 1u : 0u);
    }
    // Suppressed per second and the 1 Hz partner_fire line while anything changed.
    if (have_stats && fs.installed) {
        if (!m.fire_rate_ok) {
            m.fire_rate_ok = true;
            m.fire_rate = fs;
            m.fire_rate_ms = now;
        } else if (now - m.fire_rate_ms >= kNativeLineMs) {
            const std::uint64_t dt = now - m.fire_rate_ms;
            m.fire_suppressed_s = Rate(fs.suppressed, m.fire_rate.suppressed, dt);
            if ((fs.suppressed != m.fire_rate.suppressed || fs.owner_disarmed != m.fire_rate.owner_disarmed) &&
                m.fire_lines < kNativeLineCap) {
                ++m.fire_lines;
                EmitFormat(h, "partner_fire; suppressed=%u; passed=%u; passed_player=%u; owner_disarmed=%u; per_s=%u; owner=%u; "
                              "last_owner=%u; thread=%u; mag_writes=%u; shot_leaks=%u; ammo_leaks=%u\r\n",
                           fs.suppressed - m.fire_rate.suppressed, fs.passed - m.fire_rate.passed, fs.passed_player - m.fire_rate.passed_player,
                           fs.owner_disarmed - m.fire_rate.owner_disarmed, m.fire_suppressed_s, unsigned(fs.owner), unsigned(fs.last_owner),
                           fs.last_thread, ns.mag_writes, ns.shot_leaks, ns.ammo_leaks);
            }
            m.fire_rate = fs;
            m.fire_rate_ms = now;
        }
    }
    // The read-only census of the bullet and shot noise interfaces, once per mission.
    if (fs.installed && !m.fire_census_logged && h.fire_census) {
        partner_fire_filter::Census cs{};
        h.fire_census(h.context, cs);
        if (cs.ok) {
            m.fire_census_logged = true;
            m.tele.census_ok = true;
            m.tele.census_bullets_ok = cs.bullets_ok;
            m.tele.census_noise_vt = cs.noise_vt_in_image ? static_cast<std::int64_t>(cs.noise_vt) : pnat::kNull;
            m.tele.census_noise_fn = cs.noise_fn0_in_image ? static_cast<std::int64_t>(cs.noise_fn0) : pnat::kNull;
            EmitFormat(h, "partner_fire_census; bullets_vt=0x%llx; bullets_ok=%u; noise_vt=0x%llx; noise_vt_rva=%u; noise_fn0=0x%llx; "
                          "noise_fn0_rva=%u; native_calls=0\r\n",
                       static_cast<unsigned long long>(cs.bullets_vt), cs.bullets_ok ? 1u : 0u,
                       static_cast<unsigned long long>(cs.noise_vt), cs.noise_vt_in_image ? 1u : 0u,
                       static_cast<unsigned long long>(cs.noise_fn0), cs.noise_fn0_in_image ? 1u : 0u);
        }
    }
    // Apply verdict of the received manifest for the back slot (where the
    // qualified stock46 writer puts instance 1's weapon): already when the
    // partner's loaded equip is that slot's base id; unsupported otherwise
    // (only stock46 is qualified to load; a generalized writer is open).
    // Re-judged when the revision or the loaded equip changes (logged, capped).
    if (m.equip.have && gun_ok && gun.fields_ok &&
        (m.equip.manifest.revision != m.equip_apply_rev || gun.fields.equip != m.equip_apply_loaded)) {
        m.equip_apply_rev = m.equip.manifest.revision;
        m.equip_apply_loaded = gun.fields.equip;
        const std::uint16_t want = m.equip.manifest.slots[1].base_id;
        const std::uint32_t loaded = gun.fields.equip;
        const char* result = want == 0 ? "unknown" : loaded == want ? "already" : loaded == 0 ? "no_weapon" : "unsupported";
        CopyText(m.equip_apply, sizeof(m.equip_apply), result);
        EmitNative(m, h, m.equip_apply_cap, "partner_equip_apply",
                   "partner_equip=apply; rev=%u; slot=1; want=%u; loaded=%u; result=%s; writer=%s\r\n",
                   unsigned(m.equip.manifest.revision), unsigned(want), loaded, result, PartnerNativeEquipName(set.nequip));
    }
}

// The partner's own reload (docs/native-puppet.md "Weapons" R3): the event
// machine on the remote's v5 gun group and body 1's gun record; a tap goes
// through the input overlay (RELOAD for kTapFrames windows: the press word
// while aiming, the hold classifier's tap when holstered). Its hold/stuck
// output reaches the hook with the next Arm (the mirror's freeze).
void TickReload(Driver& d, const Hooks& h, const pnat::Out& out, bool gun_ok, const pp::Gun1& gun, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    partner_reload::In ri{};
    ri.now_ms = now;
    ri.setting = set.ngun ? set.nreload : PartnerNativeReload::mirror;
    ri.active = out.active && set.ngun && m.input_armed && !m.input_neutral;
    ri.remote_valid = m.remote_v5;
    ri.remote_mode = m.remote_gun_mode;
    ri.remote_seq = m.remote_reload_seq;
    ri.remote_reloading = m.remote_v5 && (m.remote_gun_flags & pin::GunFlag::Reloading) != 0;
    const bool f = gun_ok && gun.fields_ok;
    ri.body_ok = f;
    ri.body_mode = f ? gun.mode_cur : static_cast<std::uint8_t>(0);
    ri.gun_class = f && pnat::GunClass(gun.fields.flags27c) == pnat::kGunClassGun;
    ri.gate = f && pnat::GunReloadGate(gun.fields, gun.b3d5);
    ri.can_reload = f && pnat::GunCanReload(gun.fields);
    ri.weapon_match = (m.weapon_strip & pnat::kPadReload) == 0;
    // Anim round 1: only the gate's own transient verdicts wait (never the
    // unsupported gate, the gun-absent strip or the play leak strip).
    ri.weapon_wait = (m.weapon_gate == partner_slots::Gate::mismatch || m.weapon_gate == partner_slots::Gate::switching) &&
                     !m.weapon_absent && !d.leak_strip;
    ri.body_mag_ok = f;
    ri.body_mag = f ? static_cast<std::uint16_t>(pnat::GunMagazine(gun.fields)) : static_cast<std::uint16_t>(0);
    ri.force_tap = d.rtap_pending;
    d.rtap_pending = false;
    m.reload_out = partner_reload::Step(m.reload, ri);
    const char* tap_refused = nullptr;
    if (m.reload_out.tap) {
        if (!(EffectiveSettings(set, true).input_mask & pnat::kPadReload)) tap_refused = "mask";
        else BumpTap(d, m, pnat::kPadReload, now);
    }
    // Body 1's own reload mode (every setting: how long it reloaded, its magazine and base after).
    if (m.reload_out.body_event && set.ngun)
        EmitNative(m, h, m.reload_cap, "partner_reload",
                   "partner_reload=%s; setting=%s; body_mode=%u; mag=%u; cap=%u; m3e8=%u; b26=%u; upper=%u; ms=%u\r\n",
                   m.reload_out.body_event, PartnerNativeReloadName(set.nreload), unsigned(ri.body_mode),
                   f ? pnat::GunMagazine(gun.fields) : 0u, f ? pnat::GunCapacity(gun.fields) : 0u, f ? unsigned(gun.m3e8) : 0u,
                   (gun.gun3c0 & c::gun3c0_reload_mode) ? 1u : 0u, gun.upper_ok ? unsigned(gun.upper_req) : 0xFFu,
                   Clamp32(now - m.reload.body_since_ms));
    // Every native exit of an entered reload (also after a stuck report): its
    // length against the remote's mode-11/12 span. gun.rate (instance 1's
    // rec+0x310) is not read: that read is not contract-pinned.
    const bool completed = m.reload_out.event && (std::strcmp(m.reload_out.event, "complete") == 0 ||
                                                  std::strcmp(m.reload_out.event, "complete_late") == 0);
    if (completed)
        EmitNative(m, h, m.reload_cap, "partner_reload",
                   "partner_reload=%s; setting=%s; len_ms=%u; remote_len_ms=%u; refill_ms=%u; mag=%u; m3e8=%u; completed=%u; "
                   "entered=%u; late=%u; queued_tap=%u\r\n",
                   m.reload_out.event, PartnerNativeReloadName(set.nreload), m.reload_out.len_ms, m.reload.remote_len_ms,
                   m.reload.refill_ms, f ? pnat::GunMagazine(gun.fields) : 0u, f ? unsigned(gun.m3e8) : 0u, m.reload.completed,
                   m.reload.entered, m.reload.completed_late, m.reload_out.queued_tap ? 1u : 0u);
    else if (m.reload_out.event || tap_refused)
        EmitNative(m, h, m.reload_cap, "partner_reload",
                   "partner_reload=%s; setting=%s; state=%s; body_mode=%u; remote_mode=%u; seq=%u; mag=%u; m3e8=%u; taps=%u; reason=%s\r\n",
                   tap_refused ? "tap_refused" : m.reload_out.event, PartnerNativeReloadName(set.nreload),
                   partner_reload::StateName(m.reload.state), unsigned(ri.body_mode), unsigned(ri.remote_mode), unsigned(ri.remote_seq),
                   f ? pnat::GunMagazine(gun.fields) : 0u, f ? unsigned(gun.m3e8) : 0u, m.reload.taps,
                   tap_refused ? tap_refused : m.reload_out.reason ? m.reload_out.reason : "none");
    // Anim round 1: the reload rate. One paired reload (body 1's native length and
    // the remote's) updates k with partner_native_reload_rate=adapt (logged with
    // off too: the ratio is the parity evidence); the hook's write at body_exit.
    if (m.reload_out.learn) {
        const bool adapt = set.nreload_rate == PartnerNativeReloadRate::adapt;
        const float k_before = m.rate_adapt.k;
        const partner_reload::RateVerdict v = adapt ? partner_reload::RateLearn(m.rate_adapt, m.reload_out.learn_body_ms,
                                                                                m.reload_out.learn_remote_ms)
                                                    : partner_reload::RateVerdict::latched;
        const float ratio = m.reload_out.learn_remote_ms ? static_cast<float>(m.reload_out.learn_body_ms) /
                                                               static_cast<float>(m.reload_out.learn_remote_ms) : 0.0f;
        EmitNativeN(h, m.rate_cap, kAnimWeaponCap, "partner_reload_rate",
                    "partner_reload=rate; setting=%s; verdict=%s; body_ms=%u; remote_ms=%u; ratio_milli=%d; k_milli=%d,%d; samples=%u; "
                    "latched=%u\r\n",
                    PartnerNativeReloadRateName(set.nreload_rate), adapt ? partner_reload::RateVerdictName(v) : "off",
                    m.reload_out.learn_body_ms, m.reload_out.learn_remote_ms, static_cast<int>(ratio * 1000.0f),
                    static_cast<int>(k_before * 1000.0f), static_cast<int>(m.rate_adapt.k * 1000.0f), m.rate_adapt.samples,
                    m.rate_adapt.latched ? 1u : 0u);
    }
    // The remote's next reload queued while body 1 still reloaded: one tap at its exit.
    if (m.reload_out.queued_tap)
        EmitNative(m, h, m.reload_cap, "partner_reload",
                   "partner_reload=queued_tap; setting=%s; state=%s; remote_mode=%u; seq=%u; mag=%u; taps=%u; reconcile=1; reason=%s\r\n",
                   PartnerNativeReloadName(set.nreload), partner_reload::StateName(m.reload.state), unsigned(ri.remote_mode),
                   unsigned(ri.remote_seq), f ? pnat::GunMagazine(gun.fields) : 0u, m.reload.taps, tap_refused ? tap_refused : "none");
}

// The weapon switch (docs/native-puppet.md "Weapons" W4/W6): body 1's slots,
// the plan from the received manifest (types read once per revision), the
// plan line on change and the follower's verdict lines (hook totals).
void TickSlots(Driver& d, const Hooks& h, const pp::State& st, bool realized, bool gun_ok, const pp::Gun1& gun,
               const partner_input_hook::NativeStats& ns) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    m.slots1 = pp::Slots{};
    m.slots1_ok = set.ngun && realized && h.read_slots && h.read_slots(h.context, st, 1, m.slots1);
    // The remote's slot types: the manifest's own (version 2: the slot's gun
    // type, also when no base id matched), else the equip type table of its
    // base id (a version 1 sender): once per manifest revision, retried while one is unread.
    bool types_missing = false;
    for (unsigned s = 0; s < 3; ++s)
        types_missing = types_missing || (m.equip.have && m.equip.manifest.slots[s].base_id != 0 && !m.slot_types_ok[s]);
    if (m.equip.have && (m.equip.manifest.revision != m.slot_types_rev || types_missing)) {
        m.slot_types_rev = m.equip.manifest.revision;
        for (unsigned s = 0; s < 3; ++s) {
            m.slot_types[s] = 0;
            const partner_equip_wire::Slot& ms = m.equip.manifest.slots[s];
            if (partner_equip_wire::GunType(ms.type)) {
                m.slot_types[s] = ms.type;
                m.slot_types_ok[s] = true;
                continue;
            }
            m.slot_types_ok[s] = ms.base_id != 0 && h.equip_type && h.equip_type(h.context, ms.base_id, m.slot_types[s]);
        }
    }
    partner_slots::PlanIn pin_{};
    pin_.body_ok = m.slots1_ok;
    pin_.standin = set.nstandin == PartnerNativeStandin::on;
    for (unsigned s = 0; s < 3; ++s) {
        pin_.body[s] = m.slots1_ok ? partner_slots::Decode(m.slots1.handles[s]) : partner_slots::Handle{};
        pin_.remote_base[s] = m.equip.have ? m.equip.manifest.slots[s].base_id : static_cast<std::uint16_t>(0);
        pin_.remote_type[s] = m.slot_types[s];
        pin_.remote_type_ok[s] = m.equip.have && m.slot_types_ok[s];
    }
    m.slot_plan = partner_slots::MakePlan(pin_);
    m.slot_plan_ok = m.slots1_ok;
    if (set.ngun && m.slots1_ok) {
        std::uint64_t key = 1469598103934665603ull;
        const auto mix = [&key](std::uint64_t v) noexcept { key = (key ^ v) * 1099511628211ull; };
        for (unsigned s = 0; s < 3; ++s) {
            mix(static_cast<std::uint64_t>(m.slot_plan.tier[s]));
            mix(m.slots1.handles[s]);
            mix(pin_.remote_base[s]);
            mix(m.slot_plan.map[s]);
        }
        mix(m.slots1.nibble);
        if (key != m.slot_plan_key) {
            m.slot_plan_key = key;
            const auto& p = m.slot_plan;
            EmitNative(m, h, m.slot_cap, "partner_slot",
                       "partner_slot=plan; follow=%s; body_slot=%u; tiers=%s,%s,%s; body=%u:%u,%u:%u,%u:%u; remote_base=%u,%u,%u; ok=0x%x; map=%d,%d,%d; standin=%s\r\n",
                       PartnerNativeSlotName(set.nslot), unsigned(m.slots1.nibble), partner_slots::TierName(p.tier[0]),
                       partner_slots::TierName(p.tier[1]), partner_slots::TierName(p.tier[2]), unsigned(pin_.body[0].equip),
                       unsigned(pin_.body[0].type), unsigned(pin_.body[1].equip), unsigned(pin_.body[1].type), unsigned(pin_.body[2].equip),
                       unsigned(pin_.body[2].type), unsigned(pin_.remote_base[0]), unsigned(pin_.remote_base[1]),
                       unsigned(pin_.remote_base[2]), unsigned(p.ok_mask), p.map[0] < 3 ? int(p.map[0]) : -1,
                       p.map[1] < 3 ? int(p.map[1]) : -1, p.map[2] < 3 ? int(p.map[2]) : -1,
                       PartnerNativeStandinName(set.nstandin));
        }
    }
    // The follower's verdicts (process totals of the hook): a write or a new refusal.
    if (ns.slot_writes != m.slot_logged_writes || ns.slot_refused != m.slot_logged_refused) {
        const bool wrote = ns.slot_writes != m.slot_logged_writes;
        m.slot_logged_writes = ns.slot_writes;
        m.slot_logged_refused = ns.slot_refused;
        m.slot_logged_last = static_cast<std::uint32_t>(ns.slot_last);
        EmitNative(m, h, m.slot_cap, "partner_slot",
                   "partner_slot=%s; reason=%s; target=%u; remote_seq=%u; body_slot=%u; hand_slot=%u; writes=%u; refused=%u\r\n",
                   wrote ? "write" : "refused", partner_input_hook::SlotRefusalName(ns.slot_last), ns.slot_target,
                   unsigned(m.remote_switch_seq), m.slots1_ok ? unsigned(m.slots1.nibble) : 0xFFu,
                   gun_ok && gun.fields_ok ? unsigned(gun.hand_slot) : 0xFFu, ns.slot_writes, ns.slot_refused);
    }
}

// --- Anim round 1 weapons (docs/anim-round1-weapons.md) -------------------------
// The state-driven aim intent's gate (partner_slots AimGate) for the next Arm,
// the remote's aim act, and the partner_aim_intent line (own cap, change lines
// at most every kAimIntentLineMs; the hook decides per window, AimFrame).
void TickAimIntent(Driver& d, const Hooks& h, bool latest_ok, const pin::Sample& latest, const sync::AnimState& remote_anim,
                   bool pose_live, const partner_input_hook::NativeStats& ns, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    const bool act_valid = pose_live && (remote_anim.flags & sync::AnimFlag::Valid) != 0;
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    partner_slots::AimGateIn gi{};
    gi.enabled = set.ngun && set.naim_intent == PartnerNativeAimIntent::state;
    gi.judged = m.weapon_ok && !m.weapon_absent;
    gi.gate = m.weapon_gate;
    gi.slot_valid = m.remote_v5 && m.remote_slot <= pin::kInvSlotMax;
    gi.slot = m.remote_slot;
    gi.sleeping = h.native_pose_suppressed && h.native_pose_suppressed(h.context);
    gi.carried = m.carry_hold;
    gi.hidden = m.visibility.hidden;
    gi.riding = partner_ride_wire::Live(d.ride_rx, epoch, now) && d.ride_rx.packet.kind != partner_ride_wire::Kind::foot;
    gi.act_valid = act_valid;
    gi.act = remote_anim.act;
    m.aim_refusal = partner_slots::AimGate(gi);
    m.aim_gate_ok = m.aim_refusal == partner_slots::AimRefusal::none;
    m.aim_act = act_valid && partner_slots::AimAct(remote_anim.act);
    if (set.ngun && !m.anim_settings_logged) {
        m.anim_settings_logged = true;
        EmitFormat(h, "partner_weapon=settings; unsupported=%u; dry_stop=%u; aim_intent=%s; reload_rate=%s; fire_census=%u\r\n",
                   set.nunsupported ? 1u : 0u, set.ndry ? 1u : 0u, PartnerNativeAimIntentName(set.naim_intent),
                   PartnerNativeReloadRateName(set.nreload_rate), set.fire_census ? 1u : 0u);
    }
    if (!gi.enabled) return;
    // The driver's view of the frame rule on the newest sample (telemetry only).
    const bool live = latest_ok && latest.age_ms <= set.stale_ms;
    partner_slots::AimFrameIn fi{};
    fi.gate_ok = m.aim_gate_ok;
    fi.live = live;
    fi.pad_hold = live && (latest.packet.held & partner_pad::kHold) != 0;
    fi.v5_valid = live && latest.packet.version >= 5 && (latest.packet.gun_flags & pin::GunFlag::Valid) != 0;
    fi.v5_aiming = live && (latest.packet.gun_flags & pin::GunFlag::Aiming) != 0;
    fi.act_aim = m.aim_act;
    const partner_slots::AimSource src = partner_slots::AimFrame(fi);
    const std::uint64_t key = static_cast<std::uint64_t>(src) | (static_cast<std::uint64_t>(m.aim_refusal) << 4) |
                              (static_cast<std::uint64_t>(m.weapon_gate) << 8) | (m.aim_act ? 1ull << 12 : 0ull);
    if ((key == m.aim_intent_key && m.aim_intent_logged) || (m.aim_intent_ms && now - m.aim_intent_ms < kAimIntentLineMs)) return;
    if (m.aim_intent_lines >= kAimIntentLineCap) return;
    ++m.aim_intent_lines;
    m.aim_intent_key = key;
    m.aim_intent_logged = true;
    m.aim_intent_ms = now ? now : 1;
    EmitFormat(h, "partner_aim_intent=%s; source=%s; refusal=%s; remote_act=%u; act_aim=%u; flags=0x%x; pad_hold=%u; gate=%s; slot=%u; "
                  "added_flag=%u; added_act=%u; pad_windows=%u%s\r\n",
               m.aim_gate_ok ? "on" : "off", partner_slots::AimSourceName(src), partner_slots::AimRefusalName(m.aim_refusal),
               act_valid ? unsigned(remote_anim.act) : 0u, m.aim_act ? 1u : 0u, live ? unsigned(latest.packet.gun_flags) : 0u,
               fi.pad_hold ? 1u : 0u, partner_slots::GateName(m.weapon_gate), unsigned(m.remote_slot), ns.aim_added_flag,
               ns.aim_added_act, ns.aim_pad_windows, m.aim_intent_lines == kAimIntentLineCap ? "; capped=1" : "");
}

// partner_weapon_ctx: one line per change of the weapon context (the remote's
// slot, in-hand equip and its type, its act outside locomotion, body 1's slot
// and equip, the gate), at most every kWeaponCtxMs; the changes in between are
// counted in the next line (no cap: bounded by the change rate).
void TickWeaponCtx(Driver& d, const Hooks& h, const sync::AnimState& remote_anim, bool pose_live, bool gun_ok, const pp::Gun1& gun,
                   const partner_input_hook::NativeStats& ns, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    if (!set.ngun) return;
    // The completion base rewrites (a slot change, a base above the capacity): one line per new write.
    if (!m.shadow_logged) {
        m.shadow_logged = true;
        m.shadow_slot_logged = ns.mag_m3e8_slot;
        m.shadow_cap_logged = ns.mag_m3e8_cap;
    } else if (ns.mag_m3e8_slot != m.shadow_slot_logged || ns.mag_m3e8_cap != m.shadow_cap_logged) {
        const bool slot = ns.mag_m3e8_slot != m.shadow_slot_logged;
        m.shadow_slot_logged = ns.mag_m3e8_slot;
        m.shadow_cap_logged = ns.mag_m3e8_cap;
        EmitNativeN(h, m.shadow_cap, kAnimWeaponCap, "partner_reload_shadow",
                    "partner_reload=shadow; reason=%s; slot_writes=%u; cap_writes=%u; body_slot=%u; mag=%u; cap=%u; m3e8=%u\r\n",
                    slot ? "slot" : "cap", ns.mag_m3e8_slot, ns.mag_m3e8_cap, gun_ok && gun.fields_ok ? unsigned(gun.slot) : 0xFFu,
                    gun_ok && gun.fields_ok ? pnat::GunMagazine(gun.fields) : 0u, gun_ok && gun.fields_ok ? pnat::GunCapacity(gun.fields) : 0u,
                    gun_ok ? unsigned(gun.m3e8) : 0u);
    }
    const unsigned act = pose_live && (remote_anim.flags & sync::AnimFlag::Valid) ? unsigned(remote_anim.act) : 0u;
    const unsigned act_key = act >= 1 && act <= 6 ? 0u : act;
    const unsigned body_slot = m.slots1_ok ? unsigned(m.slots1.nibble & 0xFu) : 0xFFu;
    const std::uint32_t body_equip = gun_ok && gun.fields_ok ? gun.fields.equip : 0u;
    std::uint64_t key = 1469598103934665603ull;
    const auto mix = [&key](std::uint64_t v) noexcept { key = (key ^ v) * 1099511628211ull; };
    mix(m.remote_slot);
    mix(static_cast<std::uint32_t>(m.remote_gun_equip));
    mix(static_cast<std::uint32_t>(m.remote_equip_type));
    mix(act_key);
    mix(body_slot);
    mix(body_equip);
    mix(static_cast<std::uint64_t>(m.weapon_gate));
    mix(static_cast<std::uint64_t>(m.unsupported));
    if (key != m.ctx_seen_key) {
        m.ctx_seen_key = key;
        ++m.ctx_changes;
    }
    if (m.ctx_have && key == m.ctx_key) return;
    if (m.ctx_have && now - m.ctx_ms < kWeaponCtxMs) return;
    m.ctx_have = true;
    m.ctx_key = key;
    m.ctx_ms = now;
    EmitFormat(h, "partner_weapon_ctx; remote_slot=%u; remote_equip=%d; type=%d; remote_act=%u; body_slot=%u; body_equip=%u; gate=%s; "
                  "unsupported=%s; strip=0x%x; aim=%s; dry=%u; changes=%u\r\n",
               unsigned(m.remote_slot), m.remote_gun_equip, m.remote_equip_type, act, body_slot, body_equip,
               partner_slots::GateName(m.weapon_gate), partner_slots::UnsupportedName(m.unsupported), m.weapon_strip,
               partner_slots::AimRefusalName(m.aim_refusal), m.dry_strip ? 1u : 0u, m.ctx_changes);
    m.ctx_changes = 0;
}

// The fire/projectile census (lab key partner_fire_census=1): recording in the
// filter's hooks follows the setting; the driver drains the ring and logs each
// request (at most kCensusLinesPerSecond per second, kCensusLineCap per
// mission; the rest counted) and a 5 s summary while anything changed.
void TickFireCensus(Driver& d, const Hooks& h, std::uint64_t now) noexcept {
    namespace ff = partner_fire_filter;
    Mission& m = d.mission;
    const Settings& set = d.settings;
    ff::SetCensus(set.fire_census);
    if (!set.fire_census) return;
    ff::CensusTotals t{};
    ff::ReadCensusTotals(t);
    // The four forwarding slots (projectile, rocket, thrown, placed): one line once they were tried.
    const auto slot = [&t](unsigned i) noexcept {
        return t.slot_installed[i] ? (t.slot_live[i] ? "live" : "lost") : t.slot_refusal[i] ? t.slot_refusal[i] : "off";
    };
    bool tried = false;
    for (unsigned i = 0; i < ff::kCensusSlots; ++i) tried = tried || t.slot_installed[i] || t.slot_refusal[i];
    if (!m.census_install_logged && tried) {
        m.census_install_logged = true;
        EmitFormat(h, "partner_fire_census=on; projectile=%s; rocket=%s; thrown=%s; placed=%s\r\n", slot(0), slot(1), slot(2), slot(3));
    }
    if (!m.census_second_ms || now - m.census_second_ms >= 1000) {
        if (m.census_second_ms && (m.census_skipped || m.census_lost) && m.census_lines < kCensusLineCap) {
            ++m.census_lines;
            EmitFormat(h, "partner_fire_census=summary; bullets=%u; noises=%u; projectiles=%u; rockets=%u; thrown=%u; placed=%u; "
                          "recorded=%u; skipped=%u; lost=%u; slots=%s,%s,%s,%s\r\n",
                       t.bullets, t.noises, t.slot_requests[0], t.slot_requests[1], t.slot_requests[2], t.slot_requests[3], t.recorded,
                       m.census_skipped, m.census_lost, slot(0), slot(1), slot(2), slot(3));
            m.census_skipped = m.census_lost = 0;
        }
        m.census_second_ms = now ? now : 1;
        m.census_second_lines = 0;
    }
    ff::CensusEntry buf[64];
    std::uint32_t lost = 0;
    const std::size_t n = ff::DrainCensus(m.census_cursor, buf, 64, lost);
    m.census_lost += lost;
    for (std::size_t i = 0; i < n; ++i) {
        const ff::CensusEntry& e = buf[i];
        if (m.census_second_lines >= kCensusLinesPerSecond || m.census_lines >= kCensusLineCap) { ++m.census_skipped; continue; }
        ++m.census_second_lines;
        ++m.census_lines;
        EmitFormat(h, "partner_fire_census=%s; owner=0x%x; owner_index=%u; owner_type=%u; id=%u; b=%u,%u,%u; aux=%u; dropped=%u; "
                      "fields=%u; thread=%u; seq=%u\r\n",
                   ff::CensusKindName(e.kind), unsigned(e.owner), unsigned(e.owner & 0x1FFu), unsigned(e.owner >> 9), unsigned(e.id),
                   unsigned(e.b0), unsigned(e.b1), unsigned(e.b2), unsigned(e.aux), e.dropped ? 1u : 0u, e.fields_ok ? 1u : 0u,
                   e.thread, e.seq);
    }
}

// --- Online play (docs/native-puppet.md "Online") -------------------------------
// What this copy shows for the partner body (native, clips or off, with the
// reason), the sender's v4 sources when the native tick does not read them (a
// copy that shows clips still sends what the peer's native drive needs), the
// capability message, the native.net telemetry and its log lines. Every drive mode.
void TickOnline(Driver& d, const Hooks& h, const pp::State& st, std::uint64_t now, const pnat::Out& out, const pnat::In& in,
                bool caps_live, bool configured) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    namespace pnn = partner_native_net;
    namespace caps = partner_caps_wire;
    // The sender's sources (pure reads; TickNative reads them itself when configured).
    if (!configured && set.isend) {
        m.gun0 = pp::Gun1{};
        m.gun0_ok = h.read_gun && h.read_gun(h.context, st, 0, m.gun0);
        // Input frame v5 (R2/W1): the local player's current slot and the
        // reload/switch counters, so the peer's native reload and slot
        // follower work against a copy that shows clips too.
        m.slots0 = pp::Slots{};
        m.slots0_ok = h.read_slots && h.read_slots(h.context, st, 0, m.slots0);
        partner_reload::SenderStep(m.sender_gun, m.gun0_ok && m.gun0.fields_ok, m.gun0.mode_cur,
                                   m.slots0_ok ? m.slots0.nibble : static_cast<std::uint8_t>(0xFF));
        if (set.nrates != PartnerNativeRates::off && h.read_rates && (!m.rates_read_ms || now - m.rates_read_ms >= kRatesReadMs)) {
            m.rates_read_ms = now ? now : 1;
            m.rates_id0_ok = h.read_rates(h.context, st, 0, m.rates_id0);
        }
    }
    // The shown drive and why: after a leaving, cooldown and settling show its cause.
    const pnn::Drive drive = !configured ? pnn::Drive::off : out.active ? pnn::Drive::native : pnn::Drive::clips;
    const char* reason = !configured ? "legacy" : out.reason ? out.reason : "none";
    if (configured && !out.active && m.native.left_reason &&
        (std::strcmp(reason, "cooldown") == 0 || std::strcmp(reason, "settling") == 0))
        reason = m.native.left_reason;
    // The capability message (append mode on channel 2; pacing in partner_caps_wire).
    caps::Caps next{};
    next.features = static_cast<std::uint8_t>((set.isend ? caps::Feature::InputSender : 0) | (configured ? caps::Feature::NativeDrive : 0) |
                                              (set.ngun ? caps::Feature::NativeGun : 0) |
                                              (set.nnet == PartnerNativeNet::online ? caps::Feature::Online : 0) |
                                              (set.nhold == PartnerNativeHold::net ? caps::Feature::HoldNet : 0) |
                                              (set.nstall == PartnerNativeStall::hold ? caps::Feature::StallHold : 0));
    next.profile = static_cast<std::uint8_t>(set.nprofile);
    next.drive = static_cast<std::uint8_t>(drive);
    next.reason = pnn::ReasonCode(reason);
    next.mode = static_cast<std::uint8_t>(out.mode);
    next.policy = set.nnet == PartnerNativeNet::online ? 1 : 0;
    const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
    if (h.send_reliable_append && caps::Due(d.caps_tx, next, epoch, now)) {
        std::uint8_t bytes[caps::kSize]{};
        if (caps::Encode(next, bytes) && h.send_reliable_append(h.context, caps::kChannel, bytes, sizeof(bytes))) caps::Sent(d.caps_tx, next, now);
    }
    // native.net.
    pnn::NetTelemetry& n = m.tele.net;
    CopyText(n.policy, sizeof(n.policy), PartnerNativeNetName(set.nnet));
    CopyText(n.stall, sizeof(n.stall), PartnerNativeStallName(set.nstall));
    CopyText(n.hold, sizeof(n.hold), PartnerNativeHoldName(set.nhold));
    n.stall_ms = set.stall_ms;
    n.edge_repeats = pin::SenderRepeats(m.sender);
    n.quality_good = m.net_quality.good;
    n.quality_good_ms = in.quality_good_ms;
    n.gaps_100 = m.net_quality.gaps_100;
    n.gaps_250 = m.net_quality.gaps_250;
    n.gaps_1000 = m.net_quality.gaps_1000;
    n.max_gap_ms = m.net_quality.max_gap_ms;
    n.ia_p50_ms = m.net_quality.p50_ms;
    n.ia_p95_ms = m.net_quality.p95_ms;
    n.net_leaves = m.native.net_leaves;
    n.hard_leaves = m.native.hard_leaves;
    n.net_reentries = m.native.net_reentries;
    n.quiet_episodes = m.native.quiet_episodes;
    n.gap_snaps = m.recon_have_out ? m.recon_out.report.gap_snaps : 0u;
    n.remote_quiet = in.remote_quiet;
    n.flap = m.native.flap;
    CopyText(n.last_net_reason, sizeof(n.last_net_reason), m.native.last_net_reason ? m.native.last_net_reason : "");
    n.local_drive = drive;
    CopyText(n.local_reason, sizeof(n.local_reason), reason);
    n.caps_rx = caps_live;
    n.caps_sent = d.caps_tx.sent;
    n.caps_received = d.caps_rx.received;
    n.caps_invalid = d.caps_rx.invalid;
    const caps::Caps& rc = d.caps_rx.caps;
    n.remote_drive = caps_live ? static_cast<pnn::Drive>(rc.drive) : pnn::Drive::unknown;
    CopyText(n.remote_reason, sizeof(n.remote_reason), caps_live ? pnn::ReasonName(rc.reason) : "");
    CopyText(n.remote_profile, sizeof(n.remote_profile),
             caps_live ? PartnerNativeProfileName(static_cast<PartnerNativeProfile>(rc.profile)) : "");
    CopyText(n.remote_policy, sizeof(n.remote_policy), caps_live ? (rc.policy ? "online" : "lab") : "");
    n.remote_v4 = caps_live && (rc.features & caps::Feature::InputSender) != 0;
    n.remote_caps_refused = in.remote_caps_refused;
    // Log lines (play, or a configured native drive): the quiet episodes and a
    // 1 Hz summary while anything in it changed.
    if (!configured && !set.play_mode) return;
    if (m.native.quiet != m.net_quiet_logged) {
        m.net_quiet_logged = m.native.quiet;
        EmitNative(m, h, m.net_cap, "partner_native_net", "partner_native_net=%s; episodes=%u; input_age_ms=%u\r\n",
                   m.native.quiet ? "quiet" : "resume", m.native.quiet_episodes, in.input_age_ms);
    }
    if (m.net_line_ms && now - m.net_line_ms < kNativeLineMs) return;
    m.net_line_ms = now ? now : 1;
    std::uint64_t key = 1469598103934665603ull;
    const auto mix = [&key](std::uint64_t v) noexcept { key = (key ^ v) * 1099511628211ull; };
    mix(static_cast<std::uint64_t>(drive));
    mix(pnn::ReasonCode(reason));
    mix(n.quality_good ? 1u : 0u);
    mix(n.gaps_250);
    mix(n.net_leaves);
    mix(n.hard_leaves);
    mix(n.net_reentries);
    mix(n.quiet_episodes);
    mix(n.gap_snaps);
    mix(static_cast<std::uint64_t>(n.remote_drive));
    mix(caps_live ? rc.reason : 0u);
    if (key == m.net_line_key) return;
    m.net_line_key = key;
    if (m.net_lines >= kNativeLineCap) return;
    ++m.net_lines;
    EmitFormat(h, "partner_native_net; policy=%s; drive=%s; reason=%s; quality=%u; good_ms=%u; p50=%u; p95=%u; max_gap=%u; gaps=%u,%u,%u; "
                  "net_leaves=%u; hard_leaves=%u; reentries=%u; quiet=%u; gap_snaps=%u; flap=%u; remote=%s/%s\r\n",
               n.policy, pnn::DriveName(drive), reason, n.quality_good ? 1u : 0u, n.quality_good_ms, n.ia_p50_ms, n.ia_p95_ms,
               n.max_gap_ms, n.gaps_100, n.gaps_250, n.gaps_1000, n.net_leaves, n.hard_leaves, n.net_reentries, n.quiet_episodes,
               n.gap_snaps, n.flap ? 1u : 0u, pnn::DriveName(n.remote_drive), caps_live ? pnn::ReasonName(rc.reason) : "none");
}

// Arbiter step, identity/wrap-root publication, lazy install, deferral arming,
// steering for the next Arm, telemetry and the native log lines.
// --- Aim parity (docs/native-puppet.md "Facing", the aim-target mirror) ------
// The target 100 m along the remote's aim from the partner's eyes, published
// every native tick for the drain window (partner_aim_point), and the
// read-only aim telemetry of both instances.
std::int32_t AimMm(float metres) noexcept {
    if (!std::isfinite(metres) || std::fabs(metres) > 2.0e6f) return pnat::kNull;
    return static_cast<std::int32_t>(std::llround(static_cast<double>(metres) * 1000.0));
}
std::int32_t AimCdeg(double radians) noexcept {
    if (!std::isfinite(radians)) return pnat::kNull;
    return static_cast<std::int32_t>(std::llround(radians * 18000.0 / 3.14159265358979323846));
}
void TickAim(Driver& d, const Hooks& h, const pp::State& st, bool configured, bool realized, bool latest_ok, const pin::Sample& latest,
             bool aiming, std::uint64_t now) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    pnat::Telemetry& t = m.tele;
    partner_aim_point::Stats as{};
    as.override_value = -1;
    if (h.aim_stats) h.aim_stats(h.context, as);
    const bool override_on = as.override_value >= 0 && as.override_value <= static_cast<int>(PartnerNativeAimPt::point);
    const PartnerNativeAimPt setting = override_on ? static_cast<PartnerNativeAimPt>(as.override_value) : set.naimpt;
    pam::TargetIn in{};
    in.setting = setting;
    in.active = configured && m.native_out.active && set.ngun;
    in.body_ok = m.body_ok;
    for (unsigned i = 0; i < 3; ++i) in.body[i] = m.body_pos[i];
    in.look_ok = latest_ok && latest.packet.version >= 3 && (latest.packet.xflags & pin::XFlag::Look) && latest.age_ms <= set.stale_ms;
    in.look_pitch = latest.packet.look_pitch;
    in.look_yaw = latest.packet.look_yaw;
    in.point_ok = latest_ok && latest.age_ms <= set.stale_ms && latest.packet.version >= 6 &&
                  (latest.packet.gun_flags & pin::GunFlag::AimDir);
    in.point_pitch = static_cast<float>(latest.packet.aim_pitch_cdeg * pam::kCdegToRad);
    in.point_yaw = static_cast<float>(latest.packet.aim_yaw_cdeg * pam::kCdegToRad);
    // Read the receiver's current stance before constructing the target; no stale height.
    if (configured && realized && h.read_aim) {
        m.aim_read_ms = now ? now : 1;
        m.aim_read = pp::AimRead{};
        m.aim_read_ok = h.read_aim(h.context, st, m.aim_read);
    }
    in.stance = m.aim_read_ok && m.aim_read.gun_ok[1] ? m.aim_read.stance[1] : 0;
    const pam::TargetOut to = pam::Target(in);
    if (h.publish_aim) {
        partner_aim_point::Publication pub{};
        pub.want = to.want;
        pub.valid = to.valid;
        for (unsigned i = 0; i < 3; ++i) pub.p[i] = to.p[i];
        pub.src = static_cast<std::uint8_t>(to.src);
        pub.reason = static_cast<std::uint8_t>(to.reason);
        pub.ms = now;
        pub.epoch = d.rates_epoch;
        h.publish_aim(h.context, pub);
    }
    // Telemetry reads: every tick while the native drive is active and either
    // side aims, else at kAimReadMs.
    if (!configured || !realized || !h.read_aim) {
        m.aim_read_ok = false;
    } else if ((m.native_out.active && aiming) || !m.aim_read_ms || now - m.aim_read_ms >= kAimReadMs) {
        m.aim_read_ms = now ? now : 1;
        m.aim_read = pp::AimRead{};
        m.aim_read_ok = h.read_aim(h.context, st, m.aim_read);
    }
    const pp::AimRead& r = m.aim_read;
    const bool rok = m.aim_read_ok;
    CopyText(t.aim_setting, sizeof(t.aim_setting), PartnerNativeAimPtName(setting));
    t.aim_override = override_on;
    const auto state = static_cast<partner_aim_point::State>(as.state);
    if (!h.publish_aim) {
        CopyText(t.aim_state, sizeof(t.aim_state), "off");
        CopyText(t.aim_reason, sizeof(t.aim_reason), "unavailable");
    } else if (!to.want) {
        CopyText(t.aim_state, sizeof(t.aim_state), state == partner_aim_point::State::latched ? "latched" : "off");
        CopyText(t.aim_reason, sizeof(t.aim_reason), pam::ReasonName(to.reason));
    } else {
        CopyText(t.aim_state, sizeof(t.aim_state), partner_aim_point::StateName(state));
        CopyText(t.aim_reason, sizeof(t.aim_reason), pam::ReasonName(to.valid ? static_cast<pam::Reason>(as.reason) : to.reason));
    }
    CopyText(t.aim_src, sizeof(t.aim_src), pam::SourceName(to.src));
    t.aim_writes = as.writes;
    t.aim_refused = as.refused;
    t.aim_holds = as.holds;
    t.aim_overwritten = as.overwritten;
    t.aim_restores = as.restores;
    t.aim_drops = as.drops;
    for (unsigned i = 0; i < 3; ++i) {
        t.aim_target_mm[i] = to.valid ? AimMm(to.p[i]) : pnat::kNull;
        t.aim_written_mm[i] = as.written_ok ? AimMm(as.written[i]) : pnat::kNull;
    }
    for (unsigned k = 0; k < 2; ++k) {
        const bool gun = rok && r.gun_ok[k];
        for (unsigned i = 0; i < 3; ++i) {
            t.aim_q10_mm[k][i] = rok && r.q_ok ? AimMm(r.q10[k][i]) : pnat::kNull;
            t.aim_point_mm[k][i] = gun ? AimMm(r.aimpt[k][i]) : pnat::kNull;
        }
        // The world aim direction: the aim point seen from the body's eyes.
        double pitch = 0.0, yaw = 0.0;
        bool dir_ok = false;
        if (gun && r.body_ok[k]) {
            const double dv[3] = {static_cast<double>(r.aimpt[k][0]) - r.body[k][0],
                                  static_cast<double>(r.aimpt[k][1]) - (r.body[k][1] + pam::kEyeHeightM),
                                  static_cast<double>(r.aimpt[k][2]) - r.body[k][2]};
            dir_ok = pam::DirAngles(dv, pitch, yaw);
        }
        t.aim_yaw_cdeg[k] = dir_ok ? AimCdeg(yaw) : pnat::kNull;
        t.aim_pitch_cdeg[k] = dir_ok ? AimCdeg(pitch) : pnat::kNull;
        const bool rate = rok && r.rate_ok[k];
        const std::int32_t rel = rate ? pam::RelYawCdeg(r.r60[k]) : pnat::kNull;
        t.aim_rel_yaw_cdeg[k] = rel == INT32_MIN ? pnat::kNull : rel;
        t.aim_r5c[k] = rate ? AimMm(r.r5c[k]) : pnat::kNull;
        t.aim_r60[k] = rate ? AimMm(r.r60[k]) : pnat::kNull;
        t.aim_pred[k] = rate ? (pam::TurnPredicate(r.r60[k]) ? 1 : 0) : pnat::kNull;
        t.aim_gun_state[k] = gun ? static_cast<std::int32_t>(r.gun_state[k]) : pnat::kNull;
        t.aim_b3d5[k] = gun ? static_cast<std::int32_t>(r.b3d5[k]) : pnat::kNull;
        CopyText(t.aim_path[k], sizeof(t.aim_path[k]),
                 !gun ? "na" : ((r.path_flags[k] & c::gun3d0_bone_path) && r.b3d5[k] != c::gun_b3d5_sight) ? "bone" : "cam");
    }
        for (unsigned k = 0; k < 2; ++k) {
        const pam::GunDirOut dir = pam::GunDir(GunDirectionInput(r, k));
        t.gun_pitch_cdeg[k] = rok && dir.ok ? pam::PitchCdeg(dir.pitch) : pnat::kNull;
        t.gun_yaw_cdeg[k] = rok && dir.ok ? pam::YawCdeg(dir.yaw) : pnat::kNull;
    }
    t.remote_dir_cdeg[0] = in.point_ok ? latest.packet.aim_pitch_cdeg : pnat::kNull;
    t.remote_dir_cdeg[1] = in.point_ok ? latest.packet.aim_yaw_cdeg : pnat::kNull;
    t.dir_err_cdeg[0] = in.point_ok && t.gun_pitch_cdeg[1] != pnat::kNull ? t.gun_pitch_cdeg[1] - t.remote_dir_cdeg[0] : pnat::kNull;
    t.dir_err_cdeg[1] = in.point_ok && t.gun_yaw_cdeg[1] != pnat::kNull
                          ? pin::NormalizeCdeg(t.gun_yaw_cdeg[1] - t.remote_dir_cdeg[1]) : pnat::kNull;
    CopyText(t.dir_refused, sizeof(t.dir_refused), pam::DirReasonName(m.dir_reason));
    t.dir_checks = m.dir_check.checks; t.dir_misses = m.dir_check.misses;
    // The slot against the hook's last write (both read this tick, after the
    // last drain: the driver tick and the drain share the game's main thread).
    {
        bool known = true, same = true;
        for (unsigned i = 0; i < 3; ++i) {
            const std::int32_t s = t.aim_q10_mm[1][i], w = t.aim_written_mm[i];
            const std::int64_t diff = static_cast<std::int64_t>(s) - static_cast<std::int64_t>(w);
            if (s == pnat::kNull || w == pnat::kNull) known = false;
            else if (diff > 1 || diff < -1) same = false;
        }
        CopyText(t.aim_slot, sizeof(t.aim_slot), !known ? "na" : same ? "ours" : "other");
    }
    // The aim point against what the gun pipeline was given: the hook's last
    // write while the mirror holds the slot (the target leads it by a tick
    // while the aim or the body moves), else this tick's target.
    t.aim_point_err_mm = pnat::kNull;
    const bool ours = std::strcmp(t.aim_slot, "ours") == 0;
    if ((ours || to.valid) && rok && r.gun_ok[1]) {
        double e2 = 0.0;
        for (unsigned i = 0; i < 3; ++i) {
            const double dd = static_cast<double>(r.aimpt[1][i]) - (ours ? as.written[i] : to.p[i]);
            e2 += dd * dd;
        }
        t.aim_point_err_mm = Mm(std::sqrt(e2) * 1000.0);
    }
    // The 1 Hz partner_native_aim line (own cap) while its key changes.
    if (!configured || m.aim_lines >= kNativeLineCap || (m.aim_line_ms && now - m.aim_line_ms < kNativeLineMs)) return;
    std::uint64_t key = 1469598103934665603ull;
    const auto mix = [&key](std::uint64_t v) noexcept { key = (key ^ v) * 1099511628211ull; };
    mix(static_cast<std::uint64_t>(setting));
    mix(as.state);
    mix(as.reason);
    mix(as.writes ? 1u : 0u);
    mix(as.refused);
    mix(as.overwritten);
    mix(static_cast<std::uint64_t>(static_cast<unsigned char>(t.aim_slot[0])) | (static_cast<std::uint64_t>(static_cast<unsigned char>(t.aim_slot[1])) << 8));
    mix(static_cast<std::uint32_t>(t.aim_pred[1]));
    mix(static_cast<std::uint32_t>(t.aim_gun_state[1]));
    mix(t.aim_rel_yaw_cdeg[1] == pnat::kNull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(t.aim_rel_yaw_cdeg[1] / 500));
    mix(t.aim_point_err_mm == pnat::kNull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(t.aim_point_err_mm / 500));
    if (key == m.aim_line_key) return;
    m.aim_line_key = key;
    m.aim_line_ms = now ? now : 1;
    ++m.aim_lines;
    char v[12][16]{};
    const auto num = [](char (&o)[16], std::int32_t value) noexcept {
        if (value == pnat::kNull) StringCchCopyA(o, sizeof(o), "na");
        else StringCchPrintfA(o, sizeof(o), "%d", value);
    };
    num(v[0], t.aim_q10_mm[1][0]); num(v[1], t.aim_q10_mm[1][1]); num(v[2], t.aim_q10_mm[1][2]);
    num(v[3], t.aim_target_mm[0]); num(v[4], t.aim_target_mm[1]); num(v[5], t.aim_target_mm[2]);
    num(v[6], t.aim_point_err_mm); num(v[7], t.aim_yaw_cdeg[1]); num(v[8], t.aim_yaw_cdeg[0]); num(v[9], t.aim_rel_yaw_cdeg[1]);
    num(v[10], t.aim_r60[1]); num(v[11], t.aim_pred[1]);
    char line[512]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "partner_native_aim; setting=%s; override=%u; state=%s; reason=%s; src=%s; writes=%u; refused=%u; holds=%u; overwritten=%u; "
            "restores=%u; q10=%s,%s,%s; slot=%s; target=%s,%s,%s; err_mm=%s; yaw1=%s; yaw0=%s; rel1=%s; r60_1=%s; pred1=%s; gun1=%d; "
            "path1=%s\r\n",
            t.aim_setting, override_on ? 1u : 0u, t.aim_state, t.aim_reason, t.aim_src, t.aim_writes, t.aim_refused, t.aim_holds,
            t.aim_overwritten, t.aim_restores, v[0], v[1], v[2], t.aim_slot, v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11],
            t.aim_gun_state[1] == pnat::kNull ? -1 : t.aim_gun_state[1], t.aim_path[1])))
        Emit(h, line);
}

// --- Round-1 actions track (docs/anim-round1-actions.md) --------------------
// The act census (log only) and the DM overlay lever. Their call sites: Tick
// (TickActLog before the ride/range guards; TickOverlay in front of TickDm),
// TickNative (the overlay's suspend reason and its transition lines),
// ApplyTrigger (the overlay one-shots), Configure/MissionStart/MissionEnd.
namespace pal = mgscoop::partner_act_log;
namespace pdo = mgscoop::partner_dm_overlay;

// partner_overlay=... lines: their own cap (kOverlayLogCap per mission).
void EmitOverlay(Mission& m, const Hooks& h, const char* format, ...) noexcept {
    if (m.overlay_cap.lines >= kOverlayLogCap) {
        if (!m.overlay_cap.capped) {
            m.overlay_cap.capped = true;
            EmitFormat(h, "partner_overlay=capped; limit=%u\r\n", kOverlayLogCap);
        }
        return;
    }
    char line[384]{};
    va_list arguments;
    va_start(arguments, format);
    const HRESULT result = StringCchVPrintfA(line, ARRAYSIZE(line), format, arguments);
    va_end(arguments);
    if (SUCCEEDED(result)) {
        ++m.overlay_cap.lines;
        Emit(h, line);
    }
}

// The act census (log only; partner_act_log): an edge line for every change of
// the remote's act (the interpolated sample's anim extension) and of body 1's
// act, and per second the remote's pad presses body 1's window did not pass
// (`mask`: this tick's effective input mask). Runs before the ride and range
// guards, so a hidden rider's acts are logged too.
void TickActLog(Driver& d, const Hooks& h, const pp::State& st, const Clock& clock, bool anim_ok, const pp::Anim& anim,
                const sync::AnimState& remote_anim, bool remote_live, std::uint32_t mask) noexcept {
    Mission& m = d.mission;
    if (!d.settings.act_log) return;
    pal::ActIn ai{};
    ai.now_ms = clock.now_ms;
    ai.tick_ms = clock.tick_ms;
    ai.remote_valid = remote_live && (remote_anim.flags & sync::AnimFlag::Valid) != 0;
    ai.remote_act = remote_anim.act;
    ai.body_valid = anim_ok && pp::Realized(st, partner::partner_index);
    ai.body_act = anim_ok ? anim.act[1] : 0;
    const pal::ActOut ao = pal::Step(m.act_log, ai);
    char line[256]{};
    for (unsigned i = 0; i < ao.count; ++i)
        if (pal::FormatLine(ao.lines[i], line, sizeof(line))) Emit(h, line);
    if (ao.capped_now) EmitFormat(h, "partner_act=capped; limit=%u\r\n", pal::kActLineCap);
    if (ao.summary && pal::FormatSummary(ao, line, sizeof(line))) Emit(h, line);
    // The remote's dropped presses (its input frames' edge sets, before this copy's mask).
    pin::Sample latest{};
    const bool latest_ok = h.latest_input && h.latest_input(h.context, latest) && latest.valid;
    const bool armed = m.input_armed && !m.input_neutral;
    const bool native = armed && m.native_out.active && d.settings.drive == PartnerDrive::native;
    pal::PadIn pi{};
    pi.now_ms = clock.now_ms;
    pi.valid = latest_ok && latest.age_ms <= d.settings.stale_ms;
    pi.seq = latest.seq;
    pi.version = latest.packet.version;
    pi.held = latest.packet.held;
    pi.press = latest.packet.w50;
    pi.edge_seq = latest.packet.edge_seq;
    pi.mask = armed ? mask & ~(native ? m.weapon_strip : 0u) : 0u;
    pi.native = native;
    const pal::PadOut po = pal::PadStep(m.pad_census, pi);
    if (po.line && pal::FormatPad(po, line, sizeof(line))) Emit(h, line);
    if (po.capped_now) EmitFormat(h, "partner_pad_dropped=capped; limit=%u\r\n", pal::kPadLineCap);
}

// The DM overlay's eligibility (a static reason, or nullptr): body 1 loaded and
// on foot under the driver, apply=xform (the lever's placement), no carry,
// ride or range hide, private pose lease, fault or DM latch, and body 1 not in
// a state another mirror owns (sleep, carry, carried, dead).
const char* OverlayBlocked(const Driver& d, const Hooks& h, const pp::State& st, bool terminating, bool parts_loaded, bool anim_ok,
                           const pp::Anim& anim) noexcept {
    const Mission& m = d.mission;
    const Settings& set = d.settings;
    if (terminating) return "terminating";
    if (set.stage < 3) return "stage";
    if (!parts_loaded) return "parts";
    if (m.range_hidden) return "range";
    if (m.visibility.hidden || m.visibility_logged) return "ride";
    if (m.carry_hold || mgscoop::coop_carry::PartnerCarryHold()) return "carry";
    if (m.vehicle_job || VehicleAttached(h, st)) return "vehicle";
    if (m.ride_seated) return "seat";
    if (h.native_pose_suppressed && h.native_pose_suppressed(h.context)) return "pose_lease";
    if (m.native_disabled || m.input_faulted) return "fault";
    if (m.dm_patch_failed) return "patch_failed";
    if (m.dm_revert_failed) return "dm_revert_failed";
    if (!PartnerApplyIsTransform(set.apply)) return "apply";
    if (anim_ok && anim.act[1] != c::q18_dm_act && partner_recon::StateAct(anim.act[1])) return "body_state";
    return nullptr;
}

// The DM overlay lever (partner_dm_overlay): one step and its effects (the
// patch, DM_PLAY/DM_STOP into `ops`, the unconsumed clear, its lines).
// Returns whether TickDm stays out this tick; release_map: run TickDm with
// dm=off first (dm=map releases its clip before the overlay patches).
bool TickOverlay(Driver& d, const Hooks& h, const pp::State& st, const Clock& clock, bool terminating, bool parts_loaded, bool anim_ok,
                 const pp::Anim& anim, const sync::AnimState& remote_anim, bool remote_live, std::int32_t remote_y_mm,
                 const partner::Tick& tick, std::uint32_t tick_ops, std::uint32_t& ops, std::uint8_t& gani, bool& release_map) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    release_map = false;
    const bool pending = d.overlay_pending;
    d.overlay_pending = false;
    if (set.noverlay == PartnerNativeOverlay::off && !pending && !pdo::Active(m.overlay)) {
        m.overlay_out = pdo::Out{};
        return false;
    }
    const std::uint64_t now = clock.now_ms;
    pdo::In in{};
    in.now_ms = now;
    in.mode = set.noverlay;
    if (pending) in.debug = d.overlay_request;
    in.blocked = OverlayBlocked(d, h, st, terminating, parts_loaded, anim_ok, anim);
    in.body_present = !terminating && set.stage >= 3 && parts_loaded && !m.range_hidden && !m.visibility.hidden && !m.visibility_logged;
    in.native_clear = !m.native_out.active && !m.input_armed && !m.native_defer_armed;
    in.map_idle = m.dmmap.phase == pdm::Phase::idle && !m.dmmap.owned;
    in.patched = m.dm_patched;
    pp::DmState dm{};
    if (h.dm_targets && h.read_dm) {
        pp::DmTargets targets{};
        h.dm_targets(h.context, st, targets);
        in.dm_ok = h.read_dm(h.context, st, targets, dm);
    }
    in.q18[0] = dm.q18[0];
    in.q18[1] = dm.q18[1];
    in.g388 = dm.g388;
    in.rec18 = dm.rec18;
    in.cam_ok = dm.cam_ok;
    in.cam1f0 = dm.cam1f0;
    in.cam200 = dm.cam200;
    in.cam_skip = h.dm_camera_skip_armed && h.dm_camera_skip_armed(h.context);
    in.reply_busy = (tick_ops & (partner::op::warp | partner::op::set_parts_explicit | partner::op::set_all_active)) != 0 ||
                    ((tick_ops & partner::op::copy_parts) && !(tick.luaflags & partner::luaflag::parts_copied)) || m.look_verify;
    in.remote_valid = remote_live && (remote_anim.flags & sync::AnimFlag::Valid) != 0;
    in.remote_act = remote_anim.act;
    in.remote_y_mm = remote_y_mm;
    in.body_valid = anim_ok;
    in.body_act = anim_ok ? anim.act[1] : 0;
    const pdo::Out o = pdo::Step(m.overlay, in);
    m.overlay_out = o;
    if (o.patch && !m.dm_patched && h.dm_patch && !terminating) {
        m.dm_patched = h.dm_patch(h.context, true, "overlay");
        if (!m.dm_patched) m.dm_patch_failed = true;
    }
    if (o.play) {
        ops |= partner::op::dm_play;
        gani = o.reply_code;
        m.dm_play_ms = now;
        m.dm_pending_gani = o.reply_code;
    }
    if (o.stop) ops |= partner::op::dm_stop;
    if (o.clear_pending && h.dm_clear_pending && h.dm_clear_pending(h.context)) Emit(h, "partner_dm=cleared_pending\r\n");
    // Our request is pending only between DM_PLAY and its confirmation (RevertDm clears it then).
    if (o.owns_dm) m.dm_play_pending = m.overlay.phase == pdo::Phase::play_sent;
    if (o.event) {
        const pdo::Entry* e = pdo::At(o.index);
        char extra[96]{};
        if (o.play) StringCchPrintfA(extra, ARRAYSIZE(extra), "; gani=%u", unsigned(o.reply_code));
        else if (std::strcmp(o.event, "playing") == 0)
            StringCchPrintfA(extra, ARRAYSIZE(extra), "; rec18=0x%llx; code_match=%u; rec248=0x%02x", static_cast<unsigned long long>(dm.rec18),
                             o.code_match ? 1u : 0u, unsigned(dm.rec248));
        else if (std::strcmp(o.event, "deferred") == 0 || std::strcmp(o.event, "conflict") == 0)
            StringCchPrintfA(extra, ARRAYSIZE(extra), "; cam1f0=0x%llx; cam200=%u; g388=0x%02x", static_cast<unsigned long long>(dm.cam1f0),
                             dm.cam200, unsigned(dm.g388));
        EmitOverlay(m, h,
                    "partner_overlay=%s; state=%s; loop=%u; source=%s; reason=%s; ms=%u; phase=%s; native=%s; remote_act=%u; body_act=%u; "
                    "q18=0x%x,0x%x%s\r\n",
                    o.event, e ? e->name : "none", o.loop ? 1u : 0u, pdo::SourceName(o.source), o.reason ? o.reason : "none", o.ms,
                    pdo::PhaseName(m.overlay.phase), pnat::ModeName(m.native_out.mode), unsigned(remote_anim.act),
                    anim_ok ? unsigned(anim.act[1]) : 0u, unsigned(dm.q18[0]), unsigned(dm.q18[1]), extra);
    }
    release_map = o.release_map;
    return o.owns_dm;
}

void TickNative(Driver& d, const Hooks& h, const LabConfig& lab, const pp::State& st, const partner::Tick& tick, const Clock& clock,
                bool terminating, bool realized, bool parts_loaded, bool anim_ok, const pp::Anim& anim,
                const sync::AnimState& remote_anim, std::uint32_t remote_flags, std::uint32_t remote_equip, bool pose_live) noexcept {
    Mission& m = d.mission;
    const Settings& set = d.settings;
    const std::uint64_t now = clock.now_ms;
    // Vehicle demand has its own source-expiry/exit machine. Do not let the
    // foot arbiter destroy its native job, Arm or physical callback lifecycle.
    // Faulted/replaced owners fail VehicleJobCurrent and use normal refusal.
    if(VehicleJobCurrent(d,h,st,terminating))return;
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
    in.drain_disarmed = is.native.drain_disarmed - nb.drain_disarmed;
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
    in.thread_warmup = ds.batch_threads_seen == 1 && ds.batch_thread != 0 && ds.batch_thread == ds.hook_thread &&
                       ds.batches < pnat::Params{}.min_batches;
    in.thread_ok = ds.batch_threads_seen == 1 && ds.batch_thread != 0 && ds.batch_thread == ds.hook_thread &&
                   ds.batches >= pnat::Params{}.min_batches;
    in.faulted = m.input_faulted || ds.faulted || is.window.fault_phase != 0;
    in.realized = realized;
    in.parts_loaded = parts_loaded;
    in.range_hidden = m.range_hidden;
    in.stage = set.stage;
    in.link_live = h.link_live ? h.link_live(h.context) : (h.net_running && h.net_running(h.context));
    pin::Sample latest{};
    const bool latest_ok = h.latest_input && h.latest_input(h.context, latest) && latest.valid;
    if (latest_ok && latest.packet.version >= 4 && (latest.packet.xflags & pin::XFlag::Rates)) {
        m.rates_remote[0] = latest.packet.rate_dash;
        m.rates_remote[1] = latest.packet.rate_crawl;
        m.rates_remote[2] = latest.packet.rate_crawl_factor;
        m.rates_remote_ok = true;
        m.rates_remote_ms = now;
    }
    in.input_valid = latest_ok;
    in.input_age_ms = latest_ok ? latest.age_ms : 0xFFFFFFFFu;
    // Online play (docs/native-puppet.md "Online"): the link quality (the
    // re-entry gate after a network fallback), a quiet remote (the link is up
    // but its input and poses are stale: paused or loading) and the remote's
    // capabilities of this link session (no input frames: remote_caps).
    const pnat::Params params = set.nnet == PartnerNativeNet::online ? pnat::OnlineParams() : pnat::Params{};
    partner_native_net::Observe(m.net_quality, partner_native_net::QualityIn{now, in.link_live, latest_ok, latest_ok ? latest.seq : 0u,
                                                                             latest_ok ? latest.age_ms : 0u});
    in.quality_good_ms = partner_native_net::GoodMs(m.net_quality, now);
    in.remote_quiet = in.link_live && (!latest_ok || latest.age_ms > params.input_age_ms) && !pose_live;
    const std::uint32_t caps_session = h.link_session ? h.link_session(h.context) : 0;
    const bool caps_live = d.caps_rx.have && caps_session && d.caps_rx.caps.epoch == caps_session;
    in.remote_caps_refused = caps_live && !(d.caps_rx.caps.features & partner_caps_wire::Feature::InputSender);
    in.dm_idle = m.dmmap.phase == pdm::Phase::idle && !m.dmmap.owned;
    in.dm_patched = m.dm_patched;
    in.dm_revert_failed = m.dm_revert_failed;
    // A neutral window (placement) is armed but not yet placed.
    in.window_armed = m.input_armed && !m.input_neutral;
    in.recon_placed = m.input_armed && !m.input_neutral && m.recon_active;
    in.snaps_10s = m.recon_active && m.recon_have_out && !m.carry_hold ? m.recon_out.report.snaps_window : 0;
    // Only a true runaway pin with an idle remote feeds the runaway timer (a
    // diverge or input pin has its own reason: diverging snaps, input_stale;
    // a moving overrun pin recovers on its own once the speeds match).
    // A carried body (coop_carry) is moved by its carrier, never a runaway.
    if (m.carry_hold) {
        m.runaway_since_ms = 0;
    } else if (m.recon_active && m.recon_have_out && m.recon_out.report.runaway_idle && m.input_neutral) {
        if (!m.runaway_since_ms) m.runaway_since_ms = now ? now : 1;
        in.runaway_ms = Clamp32(now - m.runaway_since_ms);
    } else {
        m.runaway_since_ms = 0;
    }
    // Round-1 actions: the DM overlay suspends the drive (partner_dm_overlay;
    // no fallback counted, no latch, no re-entry hold).
    in.suspend_reason = m.overlay_out.suspend ? pnat::kSuspendOverlay : nullptr;
    const pnat::Mode before = m.native.mode;
    const pnat::Out out = pnat::Step(m.native, in, params);
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
    if (out.transition && configured && out.suspend_cycle) {
        // A DM overlay suspend/resume: on the overlay's cap (the transition cap stays for real leaves).
        EmitOverlay(m, h, "partner_overlay=native; mode=%s; reason=%s; suspends=%u\r\n", pnat::ModeName(out.mode),
                    out.reason ? out.reason : "none", m.native.suspends);
    } else if (out.transition && configured) {
        EmitNative(m, h, m.native_cap, "partner_native", "partner_native=%s; reason=%s; fallbacks=%u; since_ms=%u\r\n",
                   pnat::ModeName(out.mode), out.reason ? out.reason : "none", m.native.fallbacks, Clamp32(now - m.native.since_ms));
        if (out.mode == pnat::Mode::latched && !m.native_latched_logged) {
            m.native_latched_logged = true;
            EmitFormat(h, "partner_native=latched; reason=%s; fallbacks=%u\r\n", out.reason ? out.reason : "none", m.native.fallbacks);
        }
    }
    if (out.enter_step && out.suspend_cycle)
        EmitOverlay(m, h, "partner_overlay=native; step=%s; ms=%u\r\n", out.enter_step, Clamp32(now - m.native.since_ms));
    else if (out.enter_step)
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
        const bool body_prone = anim_ok && anim.stance[1] == 3;
        const bool remote_prone = (remote_anim.flags & sync::AnimFlag::Valid) && remote_anim.stance == 3;
        si.prone_transition = body_prone != remote_prone;
        si.walk_key = latest_ok && (latest.packet.held & (1u << 25)) != 0;
        si.hold_stripped = m.hold_stripped && latest_ok && latest.age_ms <= set.stale_ms;
        si.body_speed_mmps = m.recon_out.report.v_free_mmps;
        // A stripped remote aim can freeze position correction while body 1
        // is in ordinary locomotion. Magnitude parity is still safe only for
        // matching stand/crouch stances and the existing stable aim acts.
        // Fresh rmb+w/strafe captures (9c1cc321 / 87cd76f3) also qualify
        // standing aim-walk act 13. Position corrections remain frozen.
        const bool remote_aim = remote_anim.act == 11 || remote_anim.act == 12 ||
            (remote_anim.act == 13 && remote_anim.stance == 1) ||
            remote_anim.act == 14 || remote_anim.act == 15;
        si.hold_parity_ready = si.hold_stripped && m.recon_out.mode == pr::Mode::frozen &&
            m.recon_out.report.frozen == pr::Frozen::remote_act && remote_aim && anim_ok &&
            anim.act[1] >= 1 && anim.act[1] <= 6 && anim.stance[1] >= 1 && anim.stance[1] <= 2 &&
            (remote_anim.flags & sync::AnimFlag::Valid) && remote_anim.stance == anim.stance[1];
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
    // Online play: the sender's v4 sources for a copy that shows clips, the
    // capability exchange and the native.net telemetry (every drive mode).
    TickOnline(d, h, st, now, out, in, caps_live, configured);
    if (!configured) return;
    // D3 (naim=looksrc): the look source hook, lazily once per process with
    // defer=on, never while a batch is in flight (retried next tick).
    if (set.naim == PartnerNativeAim::looksrc && defer == PartnerDefer::on && set.stage >= 3 && realized && parts_loaded && !terminating &&
        h.install_look_source && !(h.in_batch && h.in_batch(h.context))) {
        m.look_src_refusal = h.install_look_source(h.context);
        m.look_src_tried = true;
    }
    // Gun (telemetry reads only).
    pp::Gun1 gun{};
    const bool gun_ok = h.read_gun1 && h.read_gun1(h.context, st, gun);
    // F4: the remote's gun equip (input frame v4 xflags Equip) against body 1's.
    m.remote_gun_equip = latest_ok && latest.packet.version >= 4 && (latest.packet.xflags & pin::XFlag::Equip)
                             ? static_cast<std::int32_t>(latest.packet.gun_equip) : pnat::kNull;
    // The local gun record (v4 magazine sender and telemetry), once per tick.
    m.gun0 = pp::Gun1{};
    m.gun0_ok = h.read_gun && h.read_gun(h.context, st, 0, m.gun0);
    // Input frame v5 (R2/W1): the local player's current slot and the reload/switch counters.
    m.slots0 = pp::Slots{};
    m.slots0_ok = h.read_slots && h.read_slots(h.context, st, 0, m.slots0);
    partner_reload::SenderStep(m.sender_gun, m.gun0_ok && m.gun0.fields_ok, m.gun0.mode_cur,
                               m.slots0_ok ? m.slots0.nibble : static_cast<std::uint8_t>(0xFF));
    // The remote's latest live v5 gun group and slot.
    m.remote_v5 = latest_ok && latest.age_ms <= set.stale_ms && latest.packet.version >= 5 &&
                  (latest.packet.gun_flags & pin::GunFlag::Valid) != 0;
    m.remote_gun_flags = m.remote_v5 ? latest.packet.gun_flags : static_cast<std::uint8_t>(0);
    m.remote_gun_mode = m.remote_v5 ? latest.packet.gun_mode : static_cast<std::uint8_t>(0);
    m.remote_reload_seq = m.remote_v5 ? latest.packet.reload_seq : static_cast<std::uint8_t>(0);
    m.remote_switch_seq = m.remote_v5 ? latest.packet.switch_seq : static_cast<std::uint8_t>(0);
    m.remote_slot = m.remote_v5 && latest.packet.inv_slot <= pin::kInvSlotMax ? latest.packet.inv_slot : pin::kInvSlotUnknown;
    // Anim round 1: the remote's v4 magazine of the newest live sample (the auto-fire stop).
    m.remote_mag_ok = latest_ok && latest.age_ms <= set.stale_ms && latest.packet.version >= 4 && (latest.packet.xflags & pin::XFlag::Mag);
    m.remote_mag = m.remote_mag_ok ? latest.packet.mag : static_cast<std::uint16_t>(0);
    m.remote_mag_cap = m.remote_mag_ok ? latest.packet.mag_cap : static_cast<std::uint16_t>(0);
    const bool remote_aiming = (latest_ok && (latest.packet.xflags & pin::XFlag::Aiming)) || (remote_flags & sync::ActionFlag::Aiming);
    const bool body_aiming = gun_ok && gun.node_active && (gun.gun3c0 & c::gun_flag_hold);
    if (remote_aiming == body_aiming) {
        if (!m.aim_since_ms) m.aim_since_ms = now ? now : 1;
        if (now - m.aim_since_ms >= kAimMatchMs) m.aim_match = true;
    } else {
        m.aim_since_ms = 0;
        m.aim_match = false;
    }
    // Native weapons: the fire gate, the mirror's record, the census and the loadout manifest.
    {
        const bool ready = configured && defer == PartnerDefer::on && set.stage >= 3 && realized && parts_loaded && !terminating;
        TickWeapons(d, h, st, ready, gun_ok, gun, is.native, now);
        TickReload(d, h, out, gun_ok, gun, now);
        TickSlots(d, h, st, realized, gun_ok, gun, is.native);
        // Anim round 1: the aim intent's gate, the weapon context line, the census
        // and the reload rate the hook applied (at body 1's reload exit).
        TickAimIntent(d, h, latest_ok, latest, remote_anim, pose_live, is.native, now);
        TickWeaponCtx(d, h, remote_anim, pose_live, gun_ok, gun, is.native, now);
        TickFireCensus(d, h, now);
        if (set.ngun && set.nreload_rate == PartnerNativeReloadRate::adapt && m.reload_out.body_event &&
            std::strcmp(m.reload_out.body_event, "body_exit") == 0)
            EmitNativeN(h, m.rate_cap, kAnimWeaponCap, "partner_reload_rate",
                        "partner_reload=rate_applied; k_milli=%d; reloads=%u; writes=%u; refused=%u; last=%s; native_milli=%d; "
                        "written_milli=%d; faulted=%u; len_ms=%u\r\n",
                        static_cast<int>(m.rate_adapt.k * 1000.0f), is.native.rate_reloads, is.native.rate_writes,
                        is.native.rate_refused, partner_input_hook::RateRefusalName(static_cast<partner_input_hook::RateRefusal>(is.native.rate_last)),
                        is.native.rate_native_milli, is.native.rate_written_milli, is.native.rate_faulted ? 1u : 0u, m.reload_out.len_ms);
    }
    // Aim parity: the target for the drain window and the aim telemetry.
    TickAim(d, h, st, configured, realized, latest_ok, latest, remote_aiming || body_aiming, now);
    const std::uint32_t gun_key = gun_ok ? (gun.gun3c0 | (gun.node_active ? 0x80000000u : 0u)) : 0xFFFFFFFFu;
    // Anim round 1: a change line at most every kNativeGunChangeMs on its own
    // cap (the old 64-line cap went blind about 30 s into a mission), the 1 Hz
    // line also while the state keeps changing; changes= counts the changes
    // since the previous line.
    const bool gun_changed = gun_key != m.native_gun_last;
    if (gun_changed) { m.native_gun_last = gun_key; ++m.native_gun_changes; }
    const bool change_due = gun_changed && now - m.native_gun_change_ms >= kNativeGunChangeMs &&
                            m.native_gun_change_lines < kNativeGunChangeCap;
    const bool periodic_due = set.ngun && now - m.native_gun_line_ms >= kNativeLineMs && m.native_gun_lines < kNativeLineCap;
    if (change_due || periodic_due) {
        const bool change = change_due;
        if (change) { m.native_gun_change_ms = now; ++m.native_gun_change_lines; }
        else { m.native_gun_line_ms = now; ++m.native_gun_lines; }
        char line[512]{};
        const bool f = gun_ok && gun.fields_ok;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "partner_native_gun; node=%u; gun3c0=0x%x; b2=%u; b18=%u; b20=%u; mode=%u; remote_flags=0x%x; equip=%u,%u; cap=%u; mag=%u; "
                "stock=%u; cls=%u; noreload=%u; b3d5=%u; slot=%u; can_reload=%u; gate=%u; aim_match=%u; aim_err_mdeg=na; fire=%s; read=%s; "
                "changes=%u\r\n",
                gun_ok && gun.node_active ? 1u : 0u, gun.gun3c0, (gun.gun3c0 & c::gun_flag_in_hand) ? 1u : 0u,
                (gun.gun3c0 & c::gun_flag_hold) ? 1u : 0u, (gun.gun3c0 & c::gun3c0_reload) ? 1u : 0u, unsigned(gun.mode), remote_flags,
                remote_equip, f ? gun.fields.equip : 0u, f ? pnat::GunCapacity(gun.fields) : 0u, f ? pnat::GunMagazine(gun.fields) : 0u,
                f ? unsigned((gun.fields.flags27c & 0x200u) ? gun.fields.stock_sub : gun.fields.stock) : 0u,
                f ? pnat::GunClass(gun.fields.flags27c) : 0u, f && (gun.fields.flags280 & c::gun280_noreload) ? 1u : 0u,
                f ? unsigned(gun.b3d5) : 0u, f ? unsigned(gun.slot) : 0u, f && pnat::GunCanReload(gun.fields) ? 1u : 0u,
                f && pnat::GunReloadGate(gun.fields, gun.b3d5) ? 1u : 0u, m.aim_match ? 1u : 0u,
                set.nfire != PartnerNativeFire::visual ? "off" : m.fire_refusal ? m.fire_refusal : "live",
                gun_ok ? (gun.fields_ok ? "ok" : "fields") : (gun.refusal ? gun.refusal : "unavailable"), m.native_gun_changes))) {
            m.native_gun_changes = 0;
            if (change && m.native_gun_change_lines == kNativeGunChangeCap)
                EmitFormat(h, "partner_native_gun=capped; limit=%u; kind=change; periodic=1\r\n", kNativeGunChangeCap);
            Emit(h, line);
        }
    }
    // Native rates: both records at most every kRatesReadMs (read-only; the
    // mirror itself runs in the drain window through the Arm).
    // The sender's own record (id0) does not depend on instance 1 being realized.
    if (set.nrates == PartnerNativeRates::off || !h.read_rates) {
        m.rates_id0_ok = m.rates_id1_ok = false;
        m.rates_id0 = m.rates_id1 = pp::RateRead{};
    } else if (!m.rates_read_ms || now - m.rates_read_ms >= kRatesReadMs) {
        m.rates_read_ms = now ? now : 1;
        m.rates_id0_ok = h.read_rates(h.context, st, 0, m.rates_id0);
        m.rates_id1_ok = realized && h.read_rates(h.context, st, 1, m.rates_id1);
    }
    if (!realized && h.read_rates && set.nrates != PartnerNativeRates::off) {
        m.rates_id1_ok = false;
        m.rates_id1 = pp::RateRead{};
        m.rates_id1.refusal = "unrealized";
    }
    const bool rates_remote_live = RatesRemoteLive(m, now);
    const char* rates_mirror = "off";
    const char* rates_reason = set.nrates == PartnerNativeRates::log ? "log" : "none";
    if (set.nrates == PartnerNativeRates::mirror) {
        if (is.native.rates_latched) {
            rates_mirror = "latched";
            rates_reason = partner_rates::ReasonName(static_cast<partner_rates::Reason>(is.native.rates_reason));
        } else if (is.native.rates_applied) {
            rates_mirror = "ok";
        } else {
            rates_mirror = "wait";
            rates_reason = !m.native_out.active ? "inactive" : !rates_remote_live ? "no_remote" : !m.rates_id1_ok ? "no_record" : "pending";
        }
    }
    if (set.nrates != PartnerNativeRates::off) {
        const auto triple = [](const pp::RateRead& r, bool ok, std::int32_t (&out)[3]) noexcept {
            out[0] = ok ? RateMilli(r.f[partner_rates::kDash]) : INT32_MIN;
            out[1] = ok ? RateMilli(r.f[partner_rates::kCrawl]) : INT32_MIN;
            out[2] = ok ? RateMilli(r.f[partner_rates::kCrawlFactor]) : INT32_MIN;
        };
        std::int32_t r0[3]{}, r1[3]{};
        triple(m.rates_id0, m.rates_id0_ok, r0);
        triple(m.rates_id1, m.rates_id1_ok, r1);
        std::uint64_t key = 1469598103934665603ull;
        const auto mix = [&key](std::uint64_t v) noexcept { key = (key ^ v) * 1099511628211ull; };
        for (unsigned i = 0; i < 3; ++i) {
            mix(static_cast<std::uint32_t>(r0[i]));
            mix(static_cast<std::uint32_t>(r1[i]));
            mix(rates_remote_live ? m.rates_remote[i] : 0u);
        }
        mix(reinterpret_cast<std::uintptr_t>(rates_mirror));
        mix(reinterpret_cast<std::uintptr_t>(rates_reason));
        if (!m.rates_logged || key != m.rates_logged_key) {
            m.rates_logged = true;
            m.rates_logged_key = key;
            EmitNative(m, h, m.native_rates_cap, "partner_native_rates",
                       "partner_native_rates; setting=%s; mirror=%s; reason=%s; id0=%d,%d,%d; id1=%d,%d,%d; id1_read=%s; remote=%u,%u,%u; "
                       "remote_live=%u; writes=%u; reapplied=%u; restores=%u; drops=%u\r\n",
                       PartnerNativeRatesName(set.nrates), rates_mirror, rates_reason, r0[0], r0[1], r0[2], r1[0], r1[1], r1[2],
                       m.rates_id1_ok ? "ok" : (m.rates_id1.refusal ? m.rates_id1.refusal : "unread"), unsigned(m.rates_remote[0]),
                       unsigned(m.rates_remote[1]), unsigned(m.rates_remote[2]), rates_remote_live ? 1u : 0u, is.native.rates_writes,
                       is.native.rates_reapplied, is.native.rates_restores, is.native.rates_drops);
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
    t.steer_err_mrad = pnat::kNull;
    t.cam_diff_cdeg = latest_ok ? pin::NormalizeCdeg(static_cast<std::int64_t>(latest.packet.cam_cdeg) - tick.cam_cdeg) : pnat::kNull;
    // The camera-relative stick rotation check, only with a live stick (with a
    // neutral stick it was the two cameras' yaw difference: cam_diff_cdeg).
    const double stick_r = latest_ok ? std::sqrt(static_cast<double>(latest.packet.lx) * latest.packet.lx +
                                                 static_cast<double>(latest.packet.ly) * latest.packet.ly) / 32767.0 : 0.0;
    if (latest_ok && stick_r >= 0.1 && (latest.packet.xflags & pin::XFlag::Heading) && is.window.last_dir != INT32_MIN) {
        const double composed = pin::Heading(static_cast<double>(tick.cam_cdeg) * 3.14159265358979323846 / 18000.0,
                                             static_cast<float>(is.window.last_lx) / 1000.0f, static_cast<float>(is.window.last_ly) / 1000.0f);
        const double err = pin::NormalizeRadians(latest.packet.heading_mrad / 1000.0 - composed);
        t.heading_err_mrad = t.steer_err_mrad = static_cast<std::int32_t>(std::lround(err * 1000.0));
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

    t.gun_tick_ms = clock.tick_ms;
    t.gun_enabled = set.ngun;
    t.gun_read_ok = gun_ok;
    t.node_active = gun_ok && gun.node_active;
    t.gun3c0 = gun.gun3c0;
    t.mode2c1 = gun.mode;
    t.remote_flags = remote_flags;
    t.remote_equip = remote_equip;
    t.body_equip = pnat::kNull;
    t.aim_match = m.aim_match;
    // D3: the remote's look and instance 1's slot after the last windowed drain.
    t.look_remote_cdeg[0] = is.native.look_ok ? is.native.look_remote_cdeg[0] : pnat::kNull;
    t.look_remote_cdeg[1] = is.native.look_ok ? is.native.look_remote_cdeg[1] : pnat::kNull;
    t.look_slot_cdeg[0] = is.native.look_ok ? is.native.look_slot_cdeg[0] : pnat::kNull;
    t.look_slot_cdeg[1] = is.native.look_ok ? is.native.look_slot_cdeg[1] : pnat::kNull;
    t.look_err_mdeg = pnat::kNull;
    if (is.native.look_ok && t.look_slot_cdeg[1] != pnat::kNull && t.look_remote_cdeg[1] != pnat::kNull)
        t.look_err_mdeg = pin::NormalizeCdeg(static_cast<std::int64_t>(t.look_slot_cdeg[1]) - t.look_remote_cdeg[1]) * 10;
    t.aim_err_mdeg = t.look_err_mdeg;
    CopyText(t.look_src, sizeof(t.look_src), set.naim != PartnerNativeAim::looksrc ? "off"
                                             : is.native.look_src_installed ? "installed"
                                             : !m.look_src_tried ? "pending" : "refused");
    t.look_src_hits = is.native.look_src_hits;
    t.look_src_refused = is.native.look_src_refused;
    t.look_src_skipped = is.native.look_src_skipped;
    t.manifest_rev = m.equip.have ? m.equip.manifest.revision : 0u;
    t.ammo_experiment = h.ammo_experiment;
    t.rates_setting = set.nrates;
    t.rates_id0_ok = m.rates_id0_ok;
    t.rates_id1_ok = m.rates_id1_ok;
    for (unsigned i = 0; i < 6; ++i) {
        t.rates_id0[i] = m.rates_id0_ok ? RateMilli(m.rates_id0.f[i]) : pnat::kNull;
        t.rates_id1[i] = m.rates_id1_ok ? RateMilli(m.rates_id1.f[i]) : pnat::kNull;
    }
    CopyText(t.rates_id1_refusal, sizeof(t.rates_id1_refusal),
             m.rates_id1_ok ? "ok" : (m.rates_id1.refusal ? m.rates_id1.refusal : "unread"));
    t.rates_remote_ok = rates_remote_live;
    t.rates_remote_age_ms = m.rates_remote_ok && now >= m.rates_remote_ms ? Clamp32(now - m.rates_remote_ms) : 0xFFFFFFFFu;
    for (unsigned i = 0; i < 3; ++i) t.rates_remote[i] = m.rates_remote_ok ? m.rates_remote[i] : 0u;
    CopyText(t.rates_mirror, sizeof(t.rates_mirror), rates_mirror);
    CopyText(t.rates_reason, sizeof(t.rates_reason), rates_reason);
    t.rates_writes = is.native.rates_writes;
    t.rates_reapplied = is.native.rates_reapplied;
    t.rates_restores = is.native.rates_restores;
    t.rates_drops = is.native.rates_drops;
    // Weapons.
    {
        const bool f = gun_ok && gun.fields_ok;
        t.gun_fields_ok = f;
        t.body_equip = f ? static_cast<std::int32_t>(gun.fields.equip) : pnat::kNull;
        t.gun_cap = f ? static_cast<std::int32_t>(pnat::GunCapacity(gun.fields)) : pnat::kNull;
        t.gun_mag = f ? static_cast<std::int32_t>(pnat::GunMagazine(gun.fields)) : pnat::kNull;
        t.gun_stock = f ? static_cast<std::int32_t>((gun.fields.flags27c & 0x200u) ? gun.fields.stock_sub : gun.fields.stock) : pnat::kNull;
        t.gun_class = f ? static_cast<std::int32_t>(pnat::GunClass(gun.fields.flags27c)) : pnat::kNull;
        t.gun_b3d5 = f ? static_cast<std::int32_t>(gun.b3d5) : pnat::kNull;
        t.gun_slot = f ? static_cast<std::int32_t>(gun.slot) : pnat::kNull;
        t.gun_noreload = f && (gun.fields.flags280 & c::gun280_noreload) ? 1u : 0u;
        t.gun_b20 = gun_ok && (gun.gun3c0 & c::gun3c0_reload) ? 1u : 0u;
        t.gun_can_reload = f && pnat::GunCanReload(gun.fields) ? 1u : 0u;
        t.gun_gate = f && pnat::GunReloadGate(gun.fields, gun.b3d5) ? 1u : 0u;
        CopyText(t.fire_mode, sizeof(t.fire_mode), PartnerNativeFireName(set.nfire));
        partner_fire_filter::Stats fs{};
        if (h.fire_stats) h.fire_stats(h.context, fs);
        t.fire_installed = fs.installed;
        t.fire_live = fs.live;
        t.fire_armed = fs.armed;
        t.fire_effective = (EffectiveSettings(set, true).input_mask & kPartnerNativeFireBit) != 0;
        CopyText(t.fire_refused, sizeof(t.fire_refused), set.nfire != PartnerNativeFire::visual ? "off" : m.fire_refusal ? m.fire_refusal : "none");
        t.fire_suppressed = fs.suppressed;
        t.fire_passed = fs.passed;
        t.fire_passed_player = fs.passed_player;
        t.fire_owner_disarmed = fs.owner_disarmed;
        t.fire_suppressed_s = m.fire_suppressed_s;
        if (!t.census_ok) t.census_noise_vt = t.census_noise_fn = pnat::kNull;
        const bool mag_live = latest_ok && latest.packet.version >= 4 && (latest.packet.xflags & pin::XFlag::Mag);
        t.remote_mag = mag_live ? latest.packet.mag : pnat::kNull;
        t.remote_mag_cap = mag_live ? latest.packet.mag_cap : pnat::kNull;
        const bool local = m.gun0_ok && m.gun0.fields_ok;
        t.local_mag = local ? static_cast<std::int32_t>(pnat::GunMagazine(m.gun0.fields)) : pnat::kNull;
        t.local_mag_cap = local ? static_cast<std::int32_t>(pnat::GunCapacity(m.gun0.fields)) : pnat::kNull;
        const partner_input_hook::NativeStats& n = is.native;
        t.mag_writes = n.mag_writes;
        t.mag_refused = n.mag_refused;
        t.mag_fire_edges = n.mag_fire_edges;
        t.mag_reload_edges = n.mag_reload_edges;
        t.mag_cap = n.mag_cap;
        t.mag_written = n.mag_written;
        CopyText(t.mag_last, sizeof(t.mag_last), partner_input_hook::MagRefusalName(n.mag_last));
        t.mag_faulted = n.mag_faulted;
        t.mag_leak_off = n.mag_leak_off;
        t.shot_leaks = n.shot_leaks;
        t.reload_drains = n.reload_drains;
        t.shot_count = n.shot_count == 0xFFFFFFFFu ? pnat::kNull : static_cast<std::int64_t>(n.shot_count);
        t.ammo_leaks = n.ammo_leaks;
        t.ammo_digest = n.ammo_digest ? static_cast<std::int64_t>(n.ammo_digest) : pnat::kNull;
        t.ring_on = n.ring_on;
        t.ring_windows = n.ring_windows;
        t.ring_refused = n.ring_refused;
        t.ring_changed = n.ring_changed;
        CopyText(t.ring_last, sizeof(t.ring_last), partner_input_hook::RingRefusalName(n.ring_last));
        t.store_writes = n.store_writes;
        t.store_refused = n.store_refused;
        CopyText(t.store_last, sizeof(t.store_last), partner_input_hook::StoreRefusalName(n.store_last));
        t.mag_inj = n.mag_inj == 0xFFFFu ? pnat::kNull : static_cast<std::int32_t>(n.mag_inj);
        t.mag_inj_cap = n.mag_inj_cap == 0xFFFFu ? pnat::kNull : static_cast<std::int32_t>(n.mag_inj_cap);
        CopyText(t.noise_mode, sizeof(t.noise_mode), PartnerNativeNoiseName(set.nnoise));
        t.noise_installed = fs.noise_installed;
        t.noise_live = fs.noise_live;
        t.noise_dropped = fs.noise_dropped;
        t.noise_passed = fs.noise_passed;
        t.kb_remote = !(latest_ok && latest.packet.version >= 4 && (latest.packet.xflags & pin::XFlag::Gamepad));
        t.remote_gun_equip = m.remote_gun_equip;
        t.remote_gun_base = m.remote_gun_base;
        t.weapon_mismatch = m.weapon_mismatch;
        t.strip_bits = m.weapon_strip;
        t.weapon_absent = m.weapon_absent;
        t.leak_strip = d.leak_strip;
        t.fire_requested = n.fire_requested;
        t.fire_shot_drains = n.fire_shot_drains;
        t.fire_modes = n.fire_modes;
        if (set.nfire == PartnerNativeFire::visual && !m.fire_refusal && m.weapon_gate != partner_slots::Gate::match)
            CopyText(t.fire_refused, sizeof(t.fire_refused), m.weapon_gate == partner_slots::Gate::mismatch      ? "weapon_mismatch"
                                                             : m.weapon_gate == partner_slots::Gate::switching   ? "weapon_switch"
                                                             : m.weapon_gate == partner_slots::Gate::unsupported ? "weapon_unsup"
                                                                                                                 : "weapon_type");
        // Anim round 1: the auto-fire stop strips FIRE with any gate.
        if (set.nfire == PartnerNativeFire::visual && !m.fire_refusal && m.dry_strip && m.weapon_gate == partner_slots::Gate::match)
            CopyText(t.fire_refused, sizeof(t.fire_refused), "remote_dry");
        CopyText(t.weapon_gate, sizeof(t.weapon_gate), partner_slots::GateName(m.weapon_gate));
        t.body_type = m.body_type;
        t.body_switching = m.body_switching;
        // The partner's own reload (R1..R6).
        t.gun_mode_cur = f ? static_cast<std::int32_t>(gun.mode_cur) : pnat::kNull;
        t.gun_upper_req = gun_ok && gun.upper_ok ? static_cast<std::int32_t>(gun.upper_req) : pnat::kNull;
        t.gun_m3e4 = f ? static_cast<std::int32_t>(gun.m3e4) : pnat::kNull;
        t.gun_m3e6 = f ? static_cast<std::int32_t>(gun.m3e6) : pnat::kNull;
        t.gun_m3e8 = f ? static_cast<std::int32_t>(gun.m3e8) : pnat::kNull;
        t.gun_hand_slot = f ? static_cast<std::int32_t>(gun.hand_slot) : pnat::kNull;
        t.gun_slot_src = f ? static_cast<std::int32_t>(gun.slot_src) : pnat::kNull;
        t.gun_b26 = gun_ok && (gun.gun3c0 & c::gun3c0_reload_mode) ? 1u : 0u;
        CopyText(t.reload_setting, sizeof(t.reload_setting), PartnerNativeReloadName(set.nreload));
        CopyText(t.reload_state, sizeof(t.reload_state), partner_reload::StateName(m.reload.state));
        CopyText(t.reload_last, sizeof(t.reload_last), m.reload.last_reason ? m.reload.last_reason : "none");
        t.reload_remote_valid = m.remote_v5;
        t.reload_remote_reloading = m.remote_v5 && (m.remote_gun_flags & pin::GunFlag::Reloading) != 0;
        t.reload_remote_mode = m.remote_v5 ? static_cast<std::int32_t>(m.remote_gun_mode) : pnat::kNull;
        t.reload_remote_seq = m.remote_v5 ? static_cast<std::int32_t>(m.remote_reload_seq) : pnat::kNull;
        t.reload_hold = m.reload_out.hold;
        t.reload_stuck = m.reload_out.stuck;
        t.reload_starts = m.reload.starts;
        t.reload_taps = m.reload.taps;
        t.reload_entered = m.reload.entered;
        t.reload_completed = m.reload.completed;
        t.reload_stuck_n = m.reload.stuck;
        t.reload_completed_late = m.reload.completed_late;
        t.reload_queued_taps = m.reload.queued_taps;
        t.reload_reconcile = m.reload_out.reconcile;
        t.reload_release = m.reload_out.release;
        t.reload_releases = m.reload.releases;
        t.reload_len_ms = m.reload.len_ms;
        t.reload_len_max_ms = m.reload.len_max_ms;
        t.reload_remote_len_ms = m.reload.remote_len_ms;
        t.reload_refill_ms = m.reload.refill_ms;
        t.reload_fallbacks = m.reload.fallbacks;
        t.reload_body_entries = m.reload.body_entries;
        t.reload_sample_strip = m.have_arm ? m.last_arm.config.sample_strip : 0u;
        t.mag_reload_seen = n.mag_reload_seen;
        t.mag_frozen = n.mag_frozen;
        t.mag_reconciles = n.mag_reconciles;
        t.mag_m3e8_writes = n.mag_m3e8_writes;
        t.probe1_b20 = n.p1_b20;
        t.probe1_b24 = n.p1_b24;
        t.probe1_shots = n.p1_shots;
        t.probe1_mode_changes = n.p1_mode_changes;
        t.probe1_mode = n.p1_mode;
        // The weapon switch (W4/W6).
        CopyText(t.slot_setting, sizeof(t.slot_setting), PartnerNativeSlotName(set.nslot));
        CopyText(t.slot_last, sizeof(t.slot_last), partner_input_hook::SlotRefusalName(n.slot_last));
        t.slot_remote = m.remote_slot <= pin::kInvSlotMax ? static_cast<std::int32_t>(m.remote_slot) : pnat::kNull;
        t.slot_remote_seq = m.remote_v5 ? static_cast<std::int32_t>(m.remote_switch_seq) : pnat::kNull;
        t.slot_body = m.slots1_ok ? static_cast<std::int32_t>(m.slots1.nibble) : pnat::kNull;
        t.slot_target = n.slot_target <= 15u ? static_cast<std::int32_t>(n.slot_target) : pnat::kNull;
        t.slots_ok = m.slots1_ok;
        for (unsigned s = 0; s < 3; ++s) {
            const partner_slots::Handle hs = m.slots1_ok ? partner_slots::Decode(m.slots1.handles[s]) : partner_slots::Handle{};
            t.slot_filled[s] = hs.filled;
            t.slot_equip[s] = hs.equip;
            t.slot_type[s] = hs.type;
            CopyText(t.slot_tier[s], sizeof(t.slot_tier[s]), m.slot_plan_ok ? partner_slots::TierName(m.slot_plan.tier[s]) : "unknown");
        }
        t.slot_ok_mask = m.slot_plan_ok ? m.slot_plan.ok_mask : static_cast<std::uint8_t>(0);
        for(unsigned s=0;s<3;++s) t.slot_map[s]=m.slot_plan_ok && m.slot_plan.map[s]<3 ? m.slot_plan.map[s] : partner_slots::kNoSlot;
        t.weapon_standin=m.weapon_standin;t.weapon_standin_ms=m.standin_ms;t.weapon_absent_ms=m.absent_ms;
        t.slot_writes = n.slot_writes;
        t.slot_refused = n.slot_refused;
        t.slot_faulted = n.slot_faulted;
        t.equip_tx_rev = d.equip_tx_have ? d.equip_tx.revision : 0u;
        for (unsigned i = 0; i < 3; ++i) {
            t.equip_tx_base[i] = d.equip_tx_have ? d.equip_tx.slots[i].base_id : 0u;
            t.equip_tx_alias[i] = d.equip_tx_have ? d.equip_tx.slots[i].alias : 0u;
            t.equip_tx_type[i] = d.equip_tx_have ? d.equip_tx.slots[i].type : 0u;
            t.equip_rx_type[i] = m.equip.have ? m.equip.manifest.slots[i].type : 0u;
            t.equip_rx_base[i] = m.equip.have ? m.equip.manifest.slots[i].base_id : 0u;
            t.equip_rx_alias[i] = m.equip.have ? m.equip.manifest.slots[i].alias : 0u;
        }
        t.equip_tables_ok = d.equip_tables_ok;
        t.equip_rx_version = m.equip.have ? m.equip.manifest.version : 0u;
        CopyText(t.equip_apply, sizeof(t.equip_apply), m.equip_apply[0] ? m.equip_apply : "none");
        // The generalized writer and preload (the observer's last report).
        const equipment_stock_writer::Status& ws = d.equip_status;
        CopyText(t.equip_setting, sizeof(t.equip_setting), PartnerNativeEquipName(set.nequip));
        t.writer_active = ws.active;
        CopyText(t.writer_shared,sizeof(t.writer_shared),equipment_stock_writer::SharedModeName(ws.shared));
        t.writer_gen=ws.gen;t.writer_resyncs=ws.resyncs;t.writer_kept=ws.kept;
        for (unsigned i = 0; i < 3; ++i) {
            const equipment_stock_writer::SlotStatus& ss = ws.slots[i];
            t.writer_want[i] = ss.want;
            t.writer_equip[i] = ss.equip;
            CopyText(t.writer_tier[i], sizeof(t.writer_tier[i]), equipment_stock_writer::TierName(ss.tier));
            CopyText(t.writer_family[i], sizeof(t.writer_family[i]), ss.equip ? (ss.optional ? "optional" : "plain") : "none");
            CopyText(t.writer_state[i], sizeof(t.writer_state[i]), equipment_stock_writer::SlotStateName(ss.state));
            CopyText(t.writer_reason[i], sizeof(t.writer_reason[i]), ss.reason[0] ? ss.reason : "none");
            t.writer_handle[i] = ss.handle;
            t.writer_want_type[i] = ss.want_type;
            t.writer_candidates[i] = ss.candidates;
            t.writer_candidate[i] = ss.candidate;
            t.writer_retries[i]=ss.retries;t.writer_refs_before[i]=ss.refs_before;t.writer_refs_after[i]=ss.refs_after;
            CopyText(t.writer_bank[i],sizeof(t.writer_bank[i]),equipment_stock_writer::BankName(ss.bank));
            for(unsigned word=0;word<3;++word) t.writer_binder[i][word]=ss.binder[word];
        }
        t.writer_dispatches = ws.dispatches;
        t.writer_bound = ws.bound;
        t.writer_refused = ws.refused;
        t.resident_ok = ws.census_ok;
        t.resident_count = ws.resident_count <= 48 ? ws.resident_count : static_cast<std::uint8_t>(48);
        for (unsigned i = 0; i < t.resident_count; ++i) {
            const equipment_stock_writer::ResidentEntry& r = ws.resident[i];
            t.resident[i] = {r.equip, r.type, r.receiver, r.flags, r.index, r.refs, r.viewer};
        }
        t.preload_count = d.preload_count <= 3 ? d.preload_count : static_cast<std::uint8_t>(3);
        for (unsigned i = 0; i < 3; ++i) t.preload_ids[i] = d.preload_ids[i];
        CopyText(t.preload_verdict, sizeof(t.preload_verdict), d.preload_verdict[0] ? d.preload_verdict : "none");
        CopyText(t.preload_reason, sizeof(t.preload_reason), d.preload_reason[0] ? d.preload_reason : "none");
        t.preload_new_hashes = d.preload_new_hashes;
    }
    // The facing governor (partner_recon) and the placement verdicts.
    {
        const bool fr = m.recon_active && m.recon_have_out;
        const pr::Report& r = m.recon_out.report;
        CopyText(t.face_setting, sizeof(t.face_setting), PartnerNativeFaceName(set.nface));
        CopyText(t.face_mode, sizeof(t.face_mode), fr ? pr::FaceStateName(r.face_state) : "off");
        CopyText(t.face_reason, sizeof(t.face_reason), fr && r.face_reason ? r.face_reason : "none");
        const auto cdeg = [](double mrad) noexcept { return Mm(mrad * 5.729577951308232); };
        t.face_err_cdeg = fr && m.body_ok && m.recon_target_ok ? cdeg(r.face_err_mrad) : pnat::kNull;
        t.face_target_cdeg = fr && m.recon_target_ok ? cdeg(r.face_target_mrad) : pnat::kNull;
        t.face_body_cdeg = m.body_ok ? cdeg(static_cast<double>(partner::QuatToYaw(m.body_quat)) * 1000.0) : pnat::kNull;
        t.face_rate_dps = fr ? Mm(r.face_rate_dps) : pnat::kNull;
        t.face_writes = fr ? r.face_writes : 0u;
        t.face_snaps = fr ? r.face_snaps : 0u;
        t.face_contests = fr ? r.face_contests : 0u;
        t.face_turns = fr ? r.face_turns : 0u;
        t.place_yaw_err_mdeg = m.recon_have_out ? m.place_yaw_err_mdeg : pnat::kNull;
        t.neutral_windows = is.native.neutral_windows;
    }
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
        t.unwindowed_native = Rate(is.native.drain_disarmed, m.rate_hook.drain_disarmed, dt);
        t.look_src_hits_s = Rate(is.native.look_src_hits, m.rate_hook.look_src_hits, dt);
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
        // The 1 Hz facing line (own cap): the governor, both acts, the look.
        if (m.face_lines < kNativeLineCap && (set.nface != PartnerNativeFace::off || set.naim != PartnerNativeAim::off)) {
            ++m.face_lines;
            char e1[16]{}, e2[16]{};
            const auto num = [](char (&o)[16], std::int32_t v) noexcept {
                if (v == pnat::kNull) StringCchCopyA(o, sizeof(o), "na");
                else StringCchPrintfA(o, sizeof(o), "%d", v);
            };
            num(e1, t.face_err_cdeg == pnat::kNull ? pnat::kNull : t.face_err_cdeg * 10);
            num(e2, t.look_err_mdeg);
            EmitFormat(h, "partner_native_face; mode=%s; setting=%s; err_mdeg=%s; act=%u,%u; writes=%u; snaps=%u; contests=%u; reason=%s; "
                          "look_err_mdeg=%s; look_src=%s; src_hits_s=%u; neutral=%u\r\n",
                       t.face_mode, t.face_setting, e1, t.act, t.remote_act, t.face_writes, t.face_snaps, t.face_contests, t.face_reason, e2,
                       t.look_src, t.look_src_hits_s, m.input_neutral ? 1u : 0u);
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
    e.mag = t.gun_mag == pnat::kNull ? static_cast<std::uint16_t>(0xFFFF) : static_cast<std::uint16_t>(t.gun_mag);
    e.body_yaw_cdeg = t.face_body_cdeg;
    e.face_err_cdeg = t.face_err_cdeg;
    e.r60_1 = t.aim_r60[1];
    e.pred1 = static_cast<std::int8_t>(t.aim_pred[1] == pnat::kNull ? -1 : t.aim_pred[1]);
    e.gmode = t.gun_mode_cur == pnat::kNull ? static_cast<std::uint8_t>(0xFF) : static_cast<std::uint8_t>(t.gun_mode_cur);
    e.rmode = m.remote_v5 ? m.remote_gun_mode : static_cast<std::uint8_t>(0xFF);
    e.hslot = t.gun_hand_slot == pnat::kNull ? static_cast<std::uint8_t>(0xFF) : static_cast<std::uint8_t>(t.gun_hand_slot);
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

#include "partner_driver_seat.inc"
} // namespace

const char* SeatPhaseName(SeatPhase phase) noexcept {
    switch (phase) {
    case SeatPhase::release: return "release";
    case SeatPhase::arm: return "arm";
    case SeatPhase::play: return "play";
    case SeatPhase::seated: return "seated";
    case SeatPhase::exit: return "exit";
    case SeatPhase::settle: return "settle";
    default: return "idle";
    }
}

bool SeatManual(Driver& d, const Hooks& h, bool arm, std::uint8_t seat, std::uint16_t vehicle, std::uint64_t now,
                bool terminating) noexcept {
    Mission& m = d.mission;
    auto& s = m.seat;
    const auto refuse = [&](const char* reason) noexcept {
        EmitSeat(m, h, "partner_ride_seat=refused; vehicle=%u; seat=%u; name=-; state=-; reason=%s; manual=1\r\n", unsigned(vehicle),
                 unsigned(seat), reason);
        return false;
    };
    if (!arm) {
        if (!s.manual || s.phase == SeatPhase::idle) return refuse("not_manual");
        SeatExit(m, h, "manual_stop", false, now);
        EmitSeat(m, h, "partner_ride_seat=exit_request; vehicle=%u; seat=%u; reason=manual_stop; via=lua\r\n", unsigned(s.vehicle),
                 unsigned(s.seat));
        return true;
    }
    if (terminating) return refuse("terminating");
    if (!m.cfg_ready || m.stopped) return refuse("inactive");
    if (!SeatMission(m.mission_code)) return refuse("mission");
    if (d.settings.stage < 3 || d.settings.source != PartnerSource::net) return refuse("stage");
    if (!SeatHooks(h) || !h.read_state) return refuse("unavailable");
    if (h.native_pose_suppressed && h.native_pose_suppressed(h.context)) return refuse("native_pose");
    if (s.phase != SeatPhase::idle) return refuse("busy");
    if (seat > partner_ride_wire::kSeatMax || (vehicle >> 9) != partner_ride_wire::kVehicleType) return refuse("vehicle_type");
    pp::State st{};
    if (h.read_state(h.context, st) != pp::Refusal::none) return refuse("state");
    if (!pp::Realized(st, partner::partner_index) || st.parts[1] != c::parts_loaded_state || !m.bits_set) return refuse("unrealized");
    pp::DmTargets targets{};
    pp::DmState dm{};
    h.dm_targets(h.context, st, targets);
    if (!h.read_dm(h.context, st, targets, dm)) return refuse("dm_read");
    if (dm.q18[0] == c::q18_dm_act || dm.q18[1] == c::q18_dm_act || (dm.g388 & 0x3C)) return refuse("busy");
    if (m.dmmap.owned || m.dmmap.phase != pdm::Phase::idle) pdm::Forget(m.dmmap);
    SeatOwnBody(d, h, now);
    if (!m.dm_patched) {
        m.dm_patched = h.dm_patch(h.context, true, "seat");
        if (!m.dm_patched) { m.dm_patch_failed = true; return refuse("dm_patch"); }
    }
    if (!h.ride_seat_arm(h.context, seat)) return refuse("seat_patch");
    s.manual = true;
    s.armed = true;
    s.vehicle = vehicle;
    s.seat = seat;
    s.entry = 0;
    s.loop_entry = 0;
    s.plays = 1;
    s.stops = 0;
    s.confirmed = s.next_sent = s.latch_exit = s.lua_refused = false;
    s.exit_reason = nullptr;
    s.far_since_ms = s.status_ms = s.confirm_ms = 0;
    s.issue_ms = now;
    SeatGo(s, SeatPhase::seated, now);
    m.ride_seated = true;
    EmitSeat(m, h, "partner_ride_seat=armed; vehicle=%u; seat=%u; name=lua; state=lua; reason=manual; cam1f0=0x%llx; cam200=%u\r\n",
             unsigned(vehicle), unsigned(seat), static_cast<unsigned long long>(dm.cam1f0), dm.cam200);
    return true;
}

bool NativeSleepPresentationReady(const Driver& d) noexcept {
    const auto& m = d.mission;
    const auto effective = EffectiveSettings(d.settings, m.native_out.active);
    return m.native_out.active && effective.native_active && effective.dm == PartnerDm::off &&
           !m.dm_patched && !m.dm_revert_failed && !m.dm_play_pending &&
           !d.dmplay_pending && !d.dmstop_pending && !m.dmmap.owned &&
           m.dmmap.phase == partner_dmmap::Phase::idle;
}

void TickEquipLink(Driver& d, const Hooks& h, std::uint64_t now_ms) noexcept {
    DrainEquip(d, h, now_ms);
    SendEquip(d, h, now_ms);
}

void DrainEquipLink(Driver& d, const Hooks& h) noexcept { DrainEquip(d, h, 0); }

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
    d.settings.fire_live = false;
    d.settings.steer_deg = lab.partner_native_steer_deg;
    d.settings.trim_pct = lab.partner_native_trim_pct;
    d.settings.nrates = lab.partner_native_rates;
    d.settings.nface = lab.partner_native_face;
    d.settings.nnoise = lab.partner_native_noise;
    d.settings.naimpt = lab.partner_native_aimpt;
    d.settings.nreload = lab.partner_native_reload;
    d.settings.nslot = lab.partner_native_slot;
    d.settings.nequip = lab.partner_native_equip;
    d.settings.nride = lab.partner_native_ride;
    d.settings.seat_enter = lab.partner_ride_seat_enter;
    d.settings.seat_exit = lab.partner_ride_seat_exit;
    d.settings.nstandin = lab.partner_native_standin;
    d.settings.nunsupported = lab.partner_native_unsupported;
    d.settings.ndry = lab.partner_native_dry_stop;
    d.settings.naim_intent = lab.partner_native_aim_intent;
    d.settings.nreload_rate = lab.partner_native_reload_rate;
    d.settings.fire_census = lab.partner_fire_census;

    d.settings.play_mode = lab.mode == LabMode::play;
    d.settings.nprofile = lab.partner_native_profile;
    d.settings.nnet = lab.partner_native_net;
    d.settings.nstall = lab.partner_native_stall;
    d.settings.stall_ms = lab.partner_native_stall_ms;
    d.settings.nhold = lab.partner_native_hold;
    d.settings.edge_repeats = lab.partner_input_edge_repeats;
    // Round-1 actions track (docs/anim-round1-actions.md).
    d.settings.noverlay = lab.partner_native_overlay;
    d.settings.act_log = lab.partner_act_log;
    d.settings.stance_fast = lab.partner_native_stance_fast;
    // Play never calls SetCurrentSlot itself (docs/native-puppet.md "Release gating"): the
    // setting the telemetry reports is the one that runs (BuildArm downgrades it again).
    if (d.settings.play_mode && d.settings.nslot == PartnerNativeSlot::call) d.settings.nslot = PartnerNativeSlot::write;
    d.look_forced_rev = 1;
    partner_recon::Reset(d.mission.recon);
    partner_dmmap::Reset(d.mission.dmmap);
    partner_native::Reset(d.mission.native, d.settings.drive == PartnerDrive::native);
    d.mission.mag_scale = 1.0;
    partner_dm_overlay::Reset(d.mission.overlay);
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
    // Native rates: the old mission's record is never written again (before
    // the disarmed Arm, so the hook drops instead of restoring, and an Arm of
    // the old epoch never applies again); a new epoch.
    if (h.forget_rates) h.forget_rates(h.context, d.rates_epoch);
    if (!++d.rates_epoch) d.rates_epoch = 1;
    DisarmInput(d, h, "mission_start");
    if (d.mission.dm_patched) RevertDm(d, h, "mission_start");
    // The seat dwords never outlive a mission (a missed MissionEnd included).
    if (h.ride_seat_revert) h.ride_seat_revert(h.context, "mission_start");
    PublishDisarmed(d, h);
    if (h.reset_input_fault) h.reset_input_fault(h.context);
    d.mission = Mission{};
    // Weapons4: the writer's body lease starts a generation; body 1's gun mode is unread.
    d.mission.realize_gen = 1;
    d.mission.body_gun_mode = 0xFF;
    // The remote's manifest of this link session (received in any earlier
    // mission, the title or the ACC): the slot plan and the weapon gate start
    // with it; a new revision replaces it as before.
    d.equip_tx_cap = LogCap{};
    const std::uint32_t session = h.link_session ? h.link_session(h.context) : 0;
    if (d.equip_cache_have && session && d.equip_cache_epoch == session) {
        d.mission.equip.have = true;
        d.mission.equip.manifest = d.equip_cache;
        d.mission.equip_logged_rev = d.equip_cache.revision;
    }
    d.settings.fire_live = false;  // the native tick re-derives it (filter live, gun class)
    partner_native::Reset(d.mission.native, d.settings.drive == PartnerDrive::native);
    d.mission.mag_scale = 1.0;
    d.mission.mission_code = mission_code;
    d.mission.range_hidden = mission_code == 30010 || mission_code == 30020;
    d.mission.range_needs_place = d.mission.range_hidden ||
        (d.settings.source == PartnerSource::net && d.settings.nride != PartnerNativeRide::off);
    if (d.settings.nride != PartnerNativeRide::off && d.settings.source == PartnerSource::net) {
        d.mission.visibility.hidden = true;
        d.mission.visibility.reason = "ride_stale";
    }
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
    d.rtap_pending = false;
    // Round-1 actions: the DM overlay starts idle; an enabled lever says so once per mission.
    partner_dm_overlay::Reset(d.mission.overlay);
    d.overlay_pending = false;
    if (d.settings.noverlay != PartnerNativeOverlay::off)
        EmitFormat(h, "partner_actions=configured; overlay=%s; act_log=%u\r\n", PartnerNativeOverlayName(d.settings.noverlay),
               d.settings.act_log ? 1u : 0u);
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
    // Native rates: dropped without writing; the ending mission's Arm (still
    // published until the disarm below lands) never applies the mirror again.
    if (h.forget_rates) h.forget_rates(h.context, d.rates_epoch);
    DisarmInput(d, h, "mission_end");
    if (d.mission.dm_patched) RevertDm(d, h, "mission_end");
    if (h.ride_seat_revert) h.ride_seat_revert(h.context, "mission_end");
    d.mission.seat.armed = false;
    d.mission.seat.phase = SeatPhase::idle;
    d.mission.ride_seated = false;
    d.mission.cfg_ready = false;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
    d.tap_pending = d.press_pending = d.dmplay_pending = d.dmstop_pending = d.p218_pending = false;
    d.overlay_pending = false;
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
    std::uint16_t refused_vehicle = 0;
    std::uint8_t refused_seat = 0;
    if (partner::ParseSeatRefusal(source, size, refused_vehicle, refused_seat) && m.seat.phase != SeatPhase::idle && !m.seat.manual &&
        refused_vehicle == m.seat.vehicle && refused_seat == m.seat.seat)
        m.seat.lua_refused = true;
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
    if (h.ride_seat_revert) h.ride_seat_revert(h.context, "stop");
    m.seat.armed = false;
    m.seat.phase = SeatPhase::idle;
    m.ride_seated = false;
    if (teardown == PartnerTeardown::clear && ClearBits(d, h, "stop", terminating)) cleared = 1;
    RestorePolicy(d, h, terminating);
    m.stopped = true;
    m.cfg_ready = false;
    d.parts_explicit_pending = false;
    d.mirror_pending = false;
    d.census_pending = false;
    d.tap_pending = d.press_pending = d.dmplay_pending = d.dmstop_pending = d.p218_pending = false;
    d.overlay_pending = false;
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
    // Play mode refuses the raw native keys in lab.ini (kPlayRefusedNativeDrive):
    // the trigger toggles drive=native|legacy only inside the native profile
    // lab.ini admitted (partner_native_profile, which also fixed the deferral)
    // and never changes the deferral.
    const bool drive_refused = d.settings.play_mode && trigger.has_drive && trigger.drive == PartnerDrive::native &&
                               d.settings.nprofile == PartnerNativeProfile::off;
    const bool defer_refused = d.settings.play_mode && trigger.has_defer;
    if (drive_refused) Emit(h, "partner_native=trigger_refused; reason=play_profile; drive=legacy\r\n");
    if (defer_refused) Emit(h, "partner_native=trigger_refused; reason=play; key=defer\r\n");
    // The lab-only native experiments lab.ini refuses in play (local=all,
    // naim=lookcam) stay refused through the trigger too, and the gun keys only
    // narrow what the profile admitted (ngun=1 / nfire=visual need the gun profile).
    const bool gun_profile = d.settings.nprofile == PartnerNativeProfile::gun;
    const bool local_refused = d.settings.play_mode && trigger.has_local && trigger.local != PartnerLocal::main;
    const bool naim_refused = d.settings.play_mode && trigger.has_naim && trigger.naim == PartnerNativeAim::lookcam;
    const bool ngun_refused = d.settings.play_mode && trigger.has_ngun && trigger.ngun && !gun_profile;
    const bool nfire_refused = d.settings.play_mode && trigger.has_nfire && trigger.nfire != PartnerNativeFire::off && !gun_profile;
    if (local_refused) Emit(h, "partner_native=trigger_refused; reason=play; key=local\r\n");
    if (naim_refused) Emit(h, "partner_native=trigger_refused; reason=play; key=naim\r\n");
    if (ngun_refused) Emit(h, "partner_native=trigger_refused; reason=play_profile; key=ngun\r\n");
    if (nfire_refused) Emit(h, "partner_native=trigger_refused; reason=play_profile; key=nfire\r\n");
    if (trigger.has_drive && !drive_refused) {
        d.settings.drive = trigger.drive;
        if (trigger.drive == PartnerDrive::native) partner_native::Unlatch(m.native, m.native.since_ms);
    }
    if (trigger.has_defer && !defer_refused) d.settings.defer = trigger.defer;
    if (trigger.has_local && !local_refused) d.settings.local = trigger.local;
    if (trigger.has_naim && !naim_refused) d.settings.naim = trigger.naim;
    if (trigger.has_ngun && !ngun_refused) d.settings.ngun = trigger.ngun;
    if (trigger.has_nfire && !nfire_refused) d.settings.nfire = trigger.nfire;
    if (trigger.has_steer) d.settings.steer_deg = trigger.steer_deg;
    if (trigger.has_nreload) d.settings.nreload = trigger.nreload;
    if (trigger.has_nslot)
        d.settings.nslot = d.settings.play_mode && trigger.nslot == PartnerNativeSlot::call ? PartnerNativeSlot::write : trigger.nslot;
    if (trigger.has_nreload || trigger.has_nslot)
        EmitFormat(h, "partner_native=trigger_weapons; nreload=%s; nslot=%s\r\n", PartnerNativeReloadName(d.settings.nreload),
                   PartnerNativeSlotName(d.settings.nslot));
    if (trigger.rtap) {
        d.rtap_pending = true;
        Emit(h, "partner_reload=rtap; queued=1\r\n");
    }
    if (native_keys)
        EmitFormat(h, "partner_native=trigger; drive=%s; defer=%s; local=%s; naim=%s; ngun=%u; nfire=%s; steer=%u; mask=0x%x\r\n",
                   PartnerDriveName(d.settings.drive), PartnerDeferName(EffectivePartnerDefer(d.settings.drive, d.settings.defer)),
                   PartnerLocalName(d.settings.local), PartnerNativeAimName(d.settings.naim), d.settings.ngun ? 1u : 0u,
                   PartnerNativeFireName(d.settings.nfire), d.settings.steer_deg,
                   partner_native::NativeMask(d.settings.input_mask, d.settings.ngun, d.settings.nfire == PartnerNativeFire::visual));
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
    // Round-1 actions: the DM overlay debug one-shots (overlay_state / overlay_stop), applied on the next tick.
    if (trigger.overlay || trigger.overlay_stop) {
        const partner_dm_overlay::Entry* entry = partner_dm_overlay::At(trigger.overlay_index);
        d.overlay_pending = true;
        d.overlay_request = partner_dm_overlay::Request{};
        d.overlay_request.start = trigger.overlay;
        d.overlay_request.stop = trigger.overlay_stop;
        d.overlay_request.index = trigger.overlay ? trigger.overlay_index : partner_dm_overlay::kNone;
        d.overlay_request.loop = trigger.has_oloop ? trigger.oloop : (entry && entry->loop);
        d.overlay_request.ms = trigger.has_oms ? trigger.oms : 0u;
        EmitOverlay(m, h, "partner_overlay=queued; state=%s; loop=%u; ms=%u; setting=%s\r\n",
                    trigger.overlay_stop ? "stop" : (entry ? entry->name : "none"), d.overlay_request.loop ? 1u : 0u, d.overlay_request.ms,
                    PartnerNativeOverlayName(d.settings.noverlay));
    }
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
        if (d.settings.source == PartnerSource::net && (m.mission_code == 30010 || m.mission_code == 30020 ||
            d.settings.nride != PartnerNativeRide::off))
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
    // A skipped/read-refused tick cannot reuse the previous gun view,
    // including two receipts within the same millisecond.
    m.tele.gun_tick_ms = 0;
    m.tele.weapon_gate_current = false;
    if (terminating) { partner_upper_probe::Clear("tick_terminating"); return false; }
    TickEquipLink(d, h, clock.now_ms);  // reliable channel 2 and the loadout sender, every tick in every drive mode
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
    // The local PlayerStatus census (receipt st=, docs/anim-round1-seat.md): logged on change.
    if (tick.has_ride) {
        const std::uint16_t bits = tick.has_status ? tick.status : 0;
        if (!m.status_have || bits != m.status_bits) {
            m.status_have = true;
            m.status_bits = bits;
            char names[160]{};
            partner::FormatStatusBits(bits, names, sizeof(names));
            EmitCapped(m.status_cap, h, "partner_status", "partner_status=local; st=0x%x; names=%s; ride=%u; vehicle=%u; seat=%u\r\n",
                       unsigned(bits), names[0] ? names : "?", unsigned(tick.ride_kind), tick.has_vehicle ? unsigned(tick.vehicle) : 65535u,
                       tick.has_vehicle ? unsigned(tick.seat) : 255u);
        }
    }
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
    SendRide(d, h, tick, now);
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
    pose::Sample visibility_sample{};
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
        visibility_sample = sample;
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
    // Round-1 actions: the act census (log only), before the ride/range guards.
    TickActLog(d, h, st, clock, anim_ok, anim, remote_anim, set.source == PartnerSource::net && sample_status == pose::Status::live,
               set.input_mask);
    const bool vehicle_job=VehicleJobCurrent(d,h,st,terminating);
    const bool vehicle_attached=VehicleAttached(h,st);
    if(m.vehicle_job&&!vehicle_job){
        d.recon_place_request=partner_recon::PlaceReason::rearm;
        m.recon_active=m.recon_have_out=false;m.hybrid_placed=false;
    }
    m.vehicle_job=vehicle_job;
    const bool free_roam_net = !vehicle_job&&set.source == PartnerSource::net &&
                               (m.mission_code == 30010 || m.mission_code == 30020);
    const bool ride_guard = !vehicle_job&&set.source == PartnerSource::net && set.nride != PartnerNativeRide::off;
    // partner_native_ride=seat (docs/anim-round1-seat.md): free roam only, every hook present, no private pose lease.
    const bool seat_capable = ride_guard && set.nride == PartnerNativeRide::seat && SeatMission(m.mission_code) && SeatHooks(h) &&
                              !(h.native_pose_suppressed && h.native_pose_suppressed(h.context));
    const std::uint32_t ride_epoch = h.link_session ? h.link_session(h.context) : 0;
    const bool ride_live = d.ride_rx.have && d.ride_rx.epoch == ride_epoch && partner_ride_wire::Live(d.ride_rx, ride_epoch, now);
    SeatObserveRide(m.seat, ride_live, d.ride_rx.packet, now);
    if (ride_guard) {
        namespace rw = partner_ride_wire;
        if (m.recon_have_out && (m.recon_out.events & partner_recon::Event::placed_ok) &&
            m.recon.place_ms > m.visibility.place_ms) rw::Placed(m.visibility, m.recon.place_ms);
        float body_pos[4]{}, body_quat[4]{};
        const bool body_read = h.read_body && h.read_body(h.context, st, body_pos, body_quat);
        const std::uint32_t epoch = h.link_session ? h.link_session(h.context) : 0;
        rw::In vi{};
        vi.fresh = pose_valid && sample_status == pose::Status::live;
        vi.time_valid = visibility_sample.time_valid;
        vi.sender_ms = visibility_sample.sender_ms;
        vi.generation = visibility_sample.generation;
        vi.jump = visibility_sample.jump;
        vi.anim_valid = visibility_sample.anim_valid;
        vi.remote_act = visibility_sample.act;
        vi.body_anim_valid = anim_ok; vi.body_act = anim.act[1];
        vi.body_valid = body_read; vi.body_y = body_pos[1];
        vi.x = visibility_sample.x; vi.y = visibility_sample.y; vi.z = visibility_sample.z;
        vi.ride_have = d.ride_rx.have && d.ride_rx.epoch == epoch;
        vi.ride_live = rw::Live(d.ride_rx, epoch, now);
        vi.kind = d.ride_rx.packet.kind;
        vi.seat_mode = seat_capable && !SeatBlocked(m.seat, d.ride_rx.packet);
        vi.seat = d.ride_rx.packet.seat;
        vi.vehicle = d.ride_rx.packet.vehicle;
        vi.lease_live = epoch && h.link_live && h.link_live(h.context);
        vi.rearmed = m.bits_set;
        vi.now_ms = now;
        const bool hidden_before = m.visibility.hidden;
        rw::Observe(m.visibility, vi);
        if (m.visibility.hidden && !hidden_before) {
            m.range_needs_place = true; m.pose_applied = m.have_cmd = m.hybrid_placed = false;
            partner_dmmap::SpeedReset(m.dmmap_speed); partner_dmmap::CancelFiniteEvents(m.dmmap);
            m.dmmap_turn = {}; m.dmmap_source_turns = {};
        }
    }
    if (free_roam_net || ride_guard) {
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
        // The seated vehicle rider owns body 1 while active (before the range
        // and ride hides: its exit stops the DM and reverts the dwords first).
        if (m.seat.phase != SeatPhase::idle || (seat_capable && m.visibility.seated)) {
            SeatTickIn sin{};
            sin.capable = seat_capable;
            sin.seated = m.visibility.seated;
            sin.ride_live = ride_live;
            sin.kind = d.ride_rx.packet.kind;
            sin.ride_seat = d.ride_rx.packet.seat;
            sin.ride_vehicle = d.ride_rx.packet.vehicle;
            sin.lease_live = ride_epoch && h.link_live && h.link_live(h.context);
            sin.remote_anim_valid = visibility_sample.valid && visibility_sample.anim_valid;
            sin.remote_act = visibility_sample.act;
            sin.pose_valid = pose_valid;
            sin.pose = pose;
            sin.realized = pp::Realized(st, partner::partner_index);
            sin.parts_loaded = sin.realized && st.parts[1] == c::parts_loaded_state;
            sin.distance_mm = distance;
            sin.now = now;
            sin.terminating = terminating;
            const SeatTickOut seat_out = TickSeat(d, h, st, sin);
            if (seat_out.own) {
                // The deferred reply (the local sender continues), the seat's
                // ops only, and the body's real readiness (nametag).
                const bool realized_now = sin.realized, parts_now = sin.parts_loaded;
                if (h.submit_local) h.submit_local(h.context, sent, local_anim);
                SendHiddenLocalInput(d, h, st, set, tick, clock, terminating);
                if (h.submit_look && looks_ok && st.local < 2 && st.parts[st.local] == c::parts_loaded_state)
                    h.submit_look(h.context, looks.look[st.local]);
                Probe(d, h, st, clock);
                SeatReportRealized(m, h, realized_now, parts_now, now);
                m.body_ok = false;
                reply = partner::Reply{seat_out.ops, partner::partner_index, pose.x_mm, pose.y_mm, pose.z_mm, pose.yaw_mrad,
                                       status, age_ms, partner::Look{}, 0};
                reply.seat_vehicle = seat_out.vehicle;
                reply.seat = seat_out.seat;
                reply.seat_action = seat_out.action;
                reply.seat_entry = seat_out.entry;
                return true;
            }
        }
        m.ride_seated = false;
        if (free_roam_net && !m.range_hidden && pose_valid && distance >= 200000) {
            partner_upper_probe::Clear("range_hidden");
            m.range_hidden = m.range_needs_place = true;
            m.pose_applied = m.have_cmd = m.hybrid_placed = false;
            partner_dmmap::SpeedReset(m.dmmap_speed);
            partner_dmmap::CancelFiniteEvents(m.dmmap);
            m.dmmap_turn = {};
            m.dmmap_source_turns = {};
            EmitCapped(m.range_cap, h, "partner_range", "partner_range=hide; distance_mm=%u; reason=far\r\n", distance);
        }
        const bool visibility_restoring = ride_guard && m.visibility_logged && (m.bits_set || m.dm_patched);
        if (ride_guard && (m.visibility.hidden || visibility_restoring)) {
            const char* why = m.visibility.reason ? m.visibility.reason : "ride_stale";
            partner_upper_probe::Clear(why);
            // Native visibility leaves do not spend the hard fallback latch.
            pnat::In blocked{}; blocked.configured = set.drive == PartnerDrive::native;
            blocked.defer_on = true; blocked.now_ms = now; blocked.visibility_reason = why;
            blocked.dm_revert_failed = m.dm_revert_failed;
            m.native_out = pnat::Step(m.native, blocked, pnat::Params{});
            if (m.native_out.req_epoch) ++m.body_epoch;
            if (m.native_out.req_dmmap_rearm) d.dmmap_rearm = true;
            if (h.set_defer) h.set_defer(h.context, false, EffectivePartnerDefer(set.drive, set.defer));
            m.native_defer_armed = false;
            WithdrawNativeTargets(m, h);
            DisarmInput(d, h, why); PublishDisarmed(d, h);
            if (m.dm_patched) RevertDm(d, h, why);
            if (m.bits_set) ClearBits(d, h, why, terminating);
            if (why != m.visibility_logged) {
                m.visibility_logged = why;
                EmitCapped(m.ride_vis_cap, h, "partner_ride", "partner_ride=hide; reason=%s; act=%u; body_act=%u\r\n",
                           why, unsigned(visibility_sample.act), anim_ok ? unsigned(anim.act[1]) : 0u);
            }
            m.tele.configured = set.drive == PartnerDrive::native;
            m.tele.mode = m.native_out.mode;
            CopyText(m.tele.reason, sizeof(m.tele.reason), why);
            m.tele.latched = m.native_out.mode == pnat::Mode::latched;
            m.tele.armed = m.tele.input_live = m.tele.body_valid = false;
            m.tele.net.local_drive = set.drive == PartnerDrive::native ? partner_native_net::Drive::clips : partner_native_net::Drive::off;
            CopyText(m.tele.net.local_reason, sizeof(m.tele.net.local_reason), why);
            d.settings.fire_live = false;
            if (!m.visibility.hidden && !m.bits_set && !m.dm_patched) {
                m.visibility_logged = nullptr;
                EmitCapped(m.ride_vis_cap, h, "partner_ride", m.visibility.seated ? "partner_ride=seat_show; next=place\r\n"
                                                                                  : "partner_ride=foot_stable; next=place\r\n");
            }
        } else if (ride_guard && m.visibility_logged) {
            m.visibility_logged = nullptr;
            EmitCapped(m.ride_vis_cap, h, "partner_ride", m.visibility.seated ? "partner_ride=seat_show; next=place\r\n"
                                                                              : "partner_ride=foot_stable; next=place\r\n");
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
            SendHiddenLocalInput(d, h, st, set, tick, clock, terminating);
            if (h.submit_look && looks_ok && st.local < 2 && st.parts[st.local] == c::parts_loaded_state)
                h.submit_look(h.context, looks.look[st.local]);
            Probe(d, h, st, clock);
            reply = partner::Reply{0, partner::partner_index, pose.x_mm, pose.y_mm, pose.z_mm, pose.yaw_mrad,
                                   status, age_ms, partner::Look{}, 0};
            return true;
        };
        if (m.range_hidden || (ride_guard && (m.visibility.hidden || ((m.bits_set || m.dm_patched) && m.visibility_logged))))
            return deferred_reply();
        if (m.range_needs_place) {
            if (fresh && set.stage >= 1 && !m.bits_set &&
                (!ride_guard || !m.visibility_place_try_ms || now-m.visibility_place_try_ms >= 1000)) {
                if (ride_guard) m.visibility_place_try_ms = now;
                float xyzw[4] = {static_cast<float>(pose.x_mm / 1000.0), static_cast<float>(pose.y_mm / 1000.0),
                                 static_cast<float>(pose.z_mm / 1000.0), static_cast<float>(pose.yaw_mrad / 1000.0)};
                const char* refused = h.write_slot ? h.write_slot(h.context, terminating, xyzw) : "unavailable";
                if (refused) LogWriteRefusal(m, h, "range_place", refused);
                else {
                    m.cmd = pose;
                    m.have_cmd = m.pose_applied = true;
                    m.range_needs_place = false;
                    if (ride_guard) partner_ride_wire::Placed(m.visibility, now);
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
    if (!vehicle_job&&set.source == PartnerSource::net && lab.partner_lost == PartnerLost::hide) {
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
    const bool body_attached = vehicle_attached || m.ride_seated;
    const bool transform = !body_attached&&PartnerApplyIsTransform(set.apply);
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
    const bool slot = !body_attached&&partner::WritesSlot(set.stage, set.apply, pose_valid);
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
    const bool native_pose_held = body_attached||(h.native_pose_suppressed && h.native_pose_suppressed(h.context));
    const bool anim_needed = !native_pose_held && set.anim != PartnerAnim::off && set.stage >= 3 && !m.dmmap.owned;
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
        ++m.realize_gen;
        m.parts_loaded = false;
        m.parts_timeout_logged = false;
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_realized=1; R=1; id=%04x; pool=%02x; ms_after_bits=%u\r\n",
                unsigned(st.ids[1]), unsigned(st.pool[1]), m.bits_set ? Clamp32(now - m.bits_ms) : 0u))) LogTransition(m, h, line);
    } else if (!realized && m.realized) {
        m.realized = false;
        m.hybrid_placed = false;
        ++m.body_epoch;
        ++m.realize_gen;
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
    TickNative(d, h, lab, st, tick, clock, terminating, realized, parts_loaded, anim_ok, anim, remote_anim, remote_flags, remote_equip,
               set.source == PartnerSource::net && sample_status == pose::Status::live);
    const Settings set_levers = EffectiveSettings(d.settings, m.native_out.active);
    TickInput(d, h, lab, st, set_levers, tick, clock, terminating, parts_loaded, pose_valid, pose, relink, anim_ok, anim, remote_anim,
              extra_ops, itick);
    // Round-1 actions: the DM overlay lever owns the DM while it runs; dm=map
    // stands down (TickDm with dm=off only to release a clip it still owns).
    // A seat episode blocks the overlay (OverlayBlocked: seat), so one already
    // playing winds itself down before the seat takes the DM.
    bool overlay_release_map = false;
    const bool overlay_dm = !vehicle_attached &&
        TickOverlay(d, h, st, clock, terminating, parts_loaded, anim_ok, anim, remote_anim,
                    set.source == PartnerSource::net && sample_status == pose::Status::live, pose_valid ? pose.y_mm : 0, tick, ops | extra_ops,
                    extra_ops, gani, overlay_release_map);
    if (!body_attached && (!overlay_dm || overlay_release_map)) {
        Settings dm_levers = set_levers;
        if (overlay_release_map) dm_levers.dm = PartnerDm::off;
        TickDm(d, h, st, dm_levers, clock, terminating, tick, dmobs, ops | extra_ops, extra_ops, gani);
    }
    // apply=input without an armed window falls back to the proven Lua Warp
    // every tick (refused, faulted, install refused, input=off, stage < 3),
    // except while the reconciliation places/verifies the body itself.
    const partner::Pose reply_pose = itick.pose_override ? itick.pose : pose;
    if (!body_attached&&set_levers.apply == PartnerApply::input && (!m.input_armed || m.input_neutral) && pose_valid && set.stage >= 1 && !itick.suppress_fallback &&
        (!free_roam_net || parts_loaded)) {
        if (!(extra_ops & partner::op::warp)) {
            extra_ops |= partner::op::warp;
            ++m.body_epoch;
            if (!m.input_fallback) {
                m.input_fallback = true;
                EmitInput(m, h, "partner_input_place; reason=fallback; err_mm=0; via=warp\r\n");
            }
        }
    } else if (m.input_armed && !m.input_neutral) {
        m.input_fallback = false;
    }
    BodyLine(d, h, st, set_levers, tick, clock, parts_loaded);
    // dm=map loss attribution: this reply's look ops and Warp, seen next tick.
    const std::uint32_t final_ops = (VehicleAttached(h,st) || m.ride_seated)?((ops|extra_ops)&~partner::op::warp):(ops|extra_ops);
    m.dmmap_prev_warp = (final_ops & partner::op::warp) != 0;
    m.dmmap_prev_look = (final_ops & partner::op::set_parts_explicit) != 0 ||
                        ((final_ops & partner::op::copy_parts) && !(tick.luaflags & partner::luaflag::parts_copied)) ||
                        (final_ops & partner::op::set_all_active) != 0;
    reply = partner::Reply{final_ops, partner::partner_index, reply_pose.x_mm, reply_pose.y_mm, reply_pose.z_mm,
                           reply_pose.yaw_mrad, status, age_ms, (ops & partner::op::set_parts_explicit) ? apply_look : partner::Look{},
                           gani};
    return true;
}

} // namespace mgscoop::partner_driver
