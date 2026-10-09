#include "mgscoop/worker_debug.hpp"
#include <charconv>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/partner_native_net.hpp"
#include "mgscoop/partner_recon.hpp"
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/steam_online_state.hpp"

namespace mgscoop::worker_debug {
bool Parse(const char* text, std::size_t size, const char* run, const char* role, Request& out) noexcept {
    out = {};
    if (!text || !run || !role || size == 0 || size > 384 || std::strlen(run) != 32 ||
        (std::strcmp(role,"host") && std::strcmp(role,"guest"))) return false;
    for (unsigned i=0;i<32;++i) if (!((run[i]>='0' && run[i]<='9') || (run[i]>='a' && run[i]<='f'))) return false;
    if (text[size-1]=='\n') --size;
    for (std::size_t i=0;i<size;++i) if (text[i]<32 || text[i]>126) return false;
    const char* p=text; const char* end=text+size;
    auto word=[&](const char* value) noexcept {
        const auto length=std::strlen(value);
        if (static_cast<std::size_t>(end-p)<=length || std::memcmp(p,value,length) || p[length]!=' ') return false;
        p+=length+1;return true;
    };
    if (!word("MGSWORKERDEBUG1") || !word(run) || !word(role)) return false;
    auto number=[&](std::uint64_t& value) noexcept {
        const char* start=p;
        while(p<end && *p>='0' && *p<='9') ++p;
        if(p==start || p==end || *p!=' ' || std::from_chars(start,p,value).ec!=std::errc{}) return false;
        ++p;return true;
    };
    std::uint64_t mission=0, generation=0;
    if(!number(out.sequence) || !number(out.expires) || !number(mission) || !number(generation) ||
       !out.sequence || !out.expires || !mission || mission>65535 || !generation || generation>UINT32_MAX ||
       p==end || end-p>200 || *p==' ' || end[-1]==' ') return false;
    out.mission=static_cast<std::uint32_t>(mission);out.generation=static_cast<std::uint32_t>(generation);
    std::memcpy(out.text,p,static_cast<std::size_t>(end-p)); return true;
}
bool IsMenuText(const char* text) noexcept { return text && std::strncmp(text,"menu ",5)==0; }
bool Timely(const Request& request,std::uint64_t now,std::uint64_t previous) noexcept {
    return request.sequence>previous && request.expires>now && request.expires-now<=15000;
}
// The state.json writer (worker builds) and its "native" member (every build:
// unit-tested for JSON validity).
namespace {
struct Json {
    char* bytes{}; std::size_t limit{}, used{}; bool ok{true};
    void Add(const char* format, ...) noexcept {
        if(!ok) return;
        va_list args; va_start(args,format);
        const int n=vsnprintf_s(bytes+used,limit-used,_TRUNCATE,format,args);
        va_end(args);if(n<0){ok=false;return;}used+=static_cast<std::size_t>(n);
    }
    void String(const char* s,std::size_t capacity) noexcept {
        Add("\"");
        for(std::size_t i=0;s && i<capacity && s[i];++i) {
            const auto c=static_cast<unsigned char>(s[i]);
            if(c=='"' || c=='\\') Add("\\%c",c);
            else if(c<32 || c>126) Add("\\u%04x",static_cast<unsigned>(c));
            else Add("%c",c);
        }
        Add("\"");
    }
    void Float(float value) noexcept { if(std::isfinite(value)) Add("%.5f",static_cast<double>(value));else Add("null"); }
    void Position(const float (&v)[3]) noexcept {Add("[");for(unsigned i=0;i<3;++i){if(i)Add(",");Float(v[i]);}Add("]");}
};
const char* Bool(bool value) noexcept {return value?"true":"false";}
// Native puppet object (docs/native-puppet.md 11.1): rates per second over the
// driver's last ~1 s interval, null when unread.
void NativeJson(Json& j,const partner_native::Telemetry& n) noexcept {
    const auto i32=[&](std::int32_t v) noexcept { if(v==partner_native::kNull) j.Add("null"); else j.Add("%d",v); };
    j.Add(",\"native\":{\"drive\":\"%s\",\"mode\":\"%s\",\"reason\":",n.configured?"native":"legacy",partner_native::ModeName(n.mode));
    j.String(n.reason,sizeof(n.reason));
    j.Add(",\"left_reason\":");j.String(n.left_reason,sizeof(n.left_reason));
    j.Add(",\"since_ms\":%llu,\"fallbacks\":%u,\"latched\":%s",static_cast<unsigned long long>(n.since_ms),n.fallbacks,Bool(n.latched));
    // Online play (docs/native-puppet.md "Online"): policy, link quality, leaves and the capability exchange.
    // Written in place (no stack copy): null only when the document is full.
    j.Add(",\"net\":");
    if(j.ok) {
        const std::size_t size=partner_native_net::FormatNetJson(n.net,j.bytes+j.used,j.limit-j.used);
        if(size) j.used+=size; else j.Add("null");
    }
    // Phase-1 detour (rates per second) and the last windowed drain.
    j.Add(",\"p1\":{\"installed\":%s,\"live\":%s,\"per_s\":",Bool(n.p1_installed),Bool(n.p1_live));
    if(!n.per_s_valid) j.Add("null");
    else j.Add("{\"forced\":%u,\"refused\":%u,\"windows\":%u}",n.p1_forced,n.p1_refused,n.p1_windows);
    j.Add("},\"drain\":{\"held\":%u,\"mag\":%d,\"view\":%u,\"local_pre\":%u,\"mask28\":%u,\"mask2c\":%u,\"mask30\":%u,",
        n.drain_held,n.drain_mag,n.drain_view,n.drain_local_pre,n.drain_mask28,n.drain_mask2c,n.drain_mask30);
    j.Add("\"rec_ok\":%s,\"rec_act\":%u,\"rec_next\":%u,\"rec_mag\":",Bool(n.rec_ok),n.rec_act,n.rec_next);i32(n.rec_mag);
    j.Add(",\"rec_heading_mrad\":");i32(n.rec_heading_mrad);j.Add("}");
    j.Add(",\"defer\":{\"setting\":\"%s\",\"h0\":%s,\"h1\":%s,\"h2\":%s,\"wrap\":%s,\"thread_ok\":%s,\"armed\":%s,",
        PartnerDeferName(n.setting),Bool(n.h0),Bool(n.h1),Bool(n.h2),Bool(n.wrap),Bool(n.thread_ok),Bool(n.armed));
    j.Add("\"batch_thread\":%u,\"hook_thread\":%u,\"lua_thread\":%u,\"batch_threads_seen\":%u,\"per_s\":",
        n.batch_thread,n.hook_thread,n.lua_thread,n.batch_threads_seen);
    if(!n.per_s_valid) j.Add("null");
    else {
        j.Add("{\"batches\":%u,\"w50_b0_s0\":%u,\"w50_b1_s0\":%u,\"w50_subs\":%u,\"w50_inline\":%u,\"ret_other\":%u,\"deferred\":%u,",
            n.batches,n.w50_b0_s0,n.w50_b1_s0,n.w50_subs,n.w50_inline,n.ret_other,n.deferred);
        j.Add("\"drains0\":%u,\"drains1\":%u,\"windowed\":%u,\"unwindowed\":%u,\"lost\":%u,\"gate_closed\":%u,\"torn\":%u,",
            n.drains0,n.drains1,n.windowed,n.unwindowed,n.lost,n.gate_closed,n.torn);
        j.Add("\"entry_changed\":%u,\"dup\":%u,\"stale_frame\":%u,\"wrap_in_batch\":%u,",n.entry_changed,n.dup,n.stale_frame,n.wrap_in_batch);
        j.Add("\"nested\":%u,\"foreign\":%u,\"unwound\":%u,\"drain_block\":%u,\"drain_in_batch\":%u,\"unwindowed_native\":%u}",
            n.nested,n.foreign,n.unwound,n.drain_block,n.drain_in_batch,n.unwindowed_native);
    }
    j.Add(",\"id1_pass\":%u,\"scope_unbalanced\":%u,\"drain_us_max\":%u,\"spin_max\":%u,\"faults\":%u,\"fault_phase\":",
        n.id1_pass,n.scope_unbalanced,n.drain_us_max,n.spin_max,n.faults);
    j.String(n.fault_phase,sizeof(n.fault_phase));
    j.Add(",\"fault_code\":%u}",n.fault_code);
    j.Add(",\"wrap\":{\"svc28_b1\":%u,\"svc28_b1_same\":%u,\"svc28_b1_nested\":%u,\"walk7_b1\":%u,\"look_writes\":%u,\"look_skipped_rec\":%u,\"look_refused\":",
        n.svc28_b1,n.svc28_b1_same,n.svc28_b1_nested,n.walk7_b1,n.look_writes,n.look_skipped_rec);
    j.String(n.look_refused,sizeof(n.look_refused));
    j.Add("},\"input\":{\"live\":%s,\"age_ms\":",Bool(n.input_live));
    if(n.input_age_ms==0xFFFFFFFFu) j.Add("null"); else j.Add("%u",n.input_age_ms);
    j.Add(",\"version\":%u,\"held\":%u,\"mask\":%u,\"lx\":",n.input_version,n.held,n.mask);i32(n.lx);
    j.Add(",\"ly\":");i32(n.ly);j.Add(",\"cam_cdeg_remote\":");i32(n.cam_cdeg_remote);j.Add(",\"cam_cdeg_host\":");i32(n.cam_cdeg_host);
    j.Add(",\"heading_err_mrad\":");i32(n.heading_err_mrad);
    j.Add(",\"steer_err_mrad\":");i32(n.steer_err_mrad);j.Add(",\"cam_diff_cdeg\":");i32(n.cam_diff_cdeg);
    j.Add(",\"steer_mdeg\":%d,\"mag_scale_pct\":%d,\"latch27c\":%u}",n.steer_mdeg,n.mag_scale_pct,n.latch27c);
    j.Add(",\"body\":{\"valid\":%s,\"act\":%u,\"stance\":%u,\"speed\":%u,\"speed_mmps\":%u,\"remote_act\":%u,\"remote_stance\":%u,",
        Bool(n.body_valid),n.act,n.stance,n.speed,n.speed_mmps,n.remote_act,n.remote_stance);
    j.Add("\"remote_speed\":%u,\"remote_speed_mmps\":%u,\"stance_match_pct\":%u,\"move_match_pct\":%u}",
        n.remote_speed,n.remote_speed_mmps,n.stance_match_pct,n.move_match_pct);
    j.Add(",\"recon\":{\"mode\":");j.String(n.recon_mode,sizeof(n.recon_mode));
    j.Add(",\"err_mm\":");i32(n.err_mm);j.Add(",\"along_mm\":");i32(n.along_mm);j.Add(",\"cross_mm\":");i32(n.cross_mm);
    j.Add(",\"dy_mm\":");i32(n.dy_mm);
    j.Add(",\"nudges\":%u,\"snaps\":%u,\"places\":%u,\"runaway\":%s,\"runaway_idle\":%s,\"lag\":%s,\"stop_grace\":%s,\"dv_along_mmps\":",
        n.nudges,n.snaps,n.places,Bool(n.runaway),Bool(n.runaway_idle),Bool(n.lag),Bool(n.stop_grace));
    i32(n.dv_along_mmps);j.Add(",\"dv_cross_mmps\":");i32(n.dv_cross_mmps);j.Add("}");
    j.Add(",\"gun\":{\"enabled\":%s,\"read_ok\":%s,\"node_active\":%s,\"gun3c0\":%u,\"bit2\":%u,\"bit18\":%u,\"mode2c1\":%u,",
        Bool(n.gun_enabled),Bool(n.gun_read_ok),Bool(n.node_active),n.gun3c0,(n.gun3c0>>2)&1u,(n.gun3c0>>18)&1u,n.mode2c1);
    j.Add("\"remote_flags\":%u,\"remote_equip\":%u,\"body_equip\":",n.remote_flags,n.remote_equip);i32(n.body_equip);
    j.Add(",\"aim_match\":%s,\"aim_err_mdeg\":",Bool(n.aim_match));i32(n.aim_err_mdeg);
    j.Add(",\"manifest_rev\":%u,\"ammo_experiment\":%s,\"fields_ok\":%s,\"cap\":",n.manifest_rev,Bool(n.ammo_experiment),Bool(n.gun_fields_ok));
    i32(n.gun_cap);j.Add(",\"mag\":");i32(n.gun_mag);j.Add(",\"stock\":");i32(n.gun_stock);j.Add(",\"class\":");i32(n.gun_class);
    j.Add(",\"b3d5\":");i32(n.gun_b3d5);j.Add(",\"slot\":");i32(n.gun_slot);
    j.Add(",\"noreload\":%u,\"b20\":%u,\"can_reload\":%u,\"gate\":%u",n.gun_noreload,n.gun_b20,n.gun_can_reload,n.gun_gate);
    // The partner's own reload: rec+0x2C0, bit 26, *(Q+0x20)[1], the shadows, the slot in hand and its source.
    j.Add(",\"mode\":");i32(n.gun_mode_cur);j.Add(",\"b26\":%u,\"upper_req\":",n.gun_b26);i32(n.gun_upper_req);
    j.Add(",\"m3e4\":");i32(n.gun_m3e4);j.Add(",\"m3e6\":");i32(n.gun_m3e6);j.Add(",\"m3e8\":");i32(n.gun_m3e8);
    j.Add(",\"hand_slot\":");i32(n.gun_hand_slot);j.Add(",\"slot_src\":");i32(n.gun_slot_src);j.Add("}");
    // Native weapons (docs/native-puppet.md "Weapons").
    const auto i64=[&](std::int64_t v) noexcept { if(v==partner_native::kNull) j.Add("null"); else j.Add("%lld",static_cast<long long>(v)); };
    j.Add(",\"fire\":{\"mode\":");j.String(n.fire_mode,sizeof(n.fire_mode));
    j.Add(",\"installed\":%s,\"live\":%s,\"armed\":%s,\"effective\":%s,\"refused\":",
        Bool(n.fire_installed),Bool(n.fire_live),Bool(n.fire_armed),Bool(n.fire_effective));
    j.String(n.fire_refused,sizeof(n.fire_refused));
    j.Add(",\"suppressed\":%u,\"passed\":%u,\"passed_player\":%u,\"owner_disarmed\":%u,\"suppressed_per_s\":%u,",
        n.fire_suppressed,n.fire_passed,n.fire_passed_player,n.fire_owner_disarmed,n.fire_suppressed_s);
    j.Add("\"census\":{\"ok\":%s,\"bullets_ok\":%s,\"noise_vt\":",Bool(n.census_ok),Bool(n.census_bullets_ok));
    i64(n.census_noise_vt);j.Add(",\"noise_fn\":");i64(n.census_noise_fn);j.Add("}}");
    j.Add(",\"mag\":{\"remote\":");i32(n.remote_mag);j.Add(",\"remote_cap\":");i32(n.remote_mag_cap);
    j.Add(",\"local\":");i32(n.local_mag);j.Add(",\"local_cap\":");i32(n.local_mag_cap);
    j.Add(",\"writes\":%u,\"refused\":%u,\"fire_edges\":%u,\"reload_edges\":%u,\"cap\":%u,\"written\":%u,\"last\":",
        n.mag_writes,n.mag_refused,n.mag_fire_edges,n.mag_reload_edges,n.mag_cap,n.mag_written);
    j.String(n.mag_last,sizeof(n.mag_last));j.Add(",\"faulted\":%s,\"leak_off\":%s,\"inj\":",Bool(n.mag_faulted),Bool(n.mag_leak_off));
    i32(n.mag_inj);j.Add(",\"inj_cap\":");i32(n.mag_inj_cap);
    j.Add(",\"store\":{\"writes\":%u,\"refused\":%u,\"last\":",n.store_writes,n.store_refused);j.String(n.store_last,sizeof(n.store_last));
    // The freeze of the partner's own reload (R4): RELOAD edges seen, frozen drains, reconciles, rec+0x3E8 writes.
    j.Add("},\"reload_seen\":%u,\"frozen\":%u,\"reconciles\":%u,\"m3e8_writes\":%u}",n.mag_reload_seen,n.mag_frozen,n.mag_reconciles,
        n.mag_m3e8_writes);
    // Weapons F1 (the drain windows' sample rings), F3 (noise), F4 (the weapon gate) and the trigger.
    j.Add(",\"rings\":{\"on\":%s,\"windows\":%u,\"refused\":%u,\"changed\":%u,\"last\":",Bool(n.ring_on),n.ring_windows,n.ring_refused,
        n.ring_changed);
    j.String(n.ring_last,sizeof(n.ring_last));
    j.Add("},\"noise\":{\"mode\":");j.String(n.noise_mode,sizeof(n.noise_mode));
    j.Add(",\"installed\":%s,\"live\":%s,\"dropped\":%u,\"passed\":%u}",Bool(n.noise_installed),Bool(n.noise_live),n.noise_dropped,
        n.noise_passed);
    j.Add(",\"weapon\":{\"kb_remote\":%s,\"remote_gun_equip\":",Bool(n.kb_remote));i32(n.remote_gun_equip);
    j.Add(",\"remote_gun_base\":");i32(n.remote_gun_base);
    j.Add(",\"mismatch\":%s,\"strip\":%u,\"absent\":%s,\"leak_strip\":%u,\"gate\":",Bool(n.weapon_mismatch),n.strip_bits,Bool(n.weapon_absent),
        n.leak_strip);j.String(n.weapon_gate,sizeof(n.weapon_gate));
    j.Add(",\"body_type\":");i32(n.body_type);j.Add(",\"switching\":%s}",Bool(n.body_switching));
    j.Add(",\"trigger\":{\"requested\":%u,\"shot_drains\":%u,\"fire_modes\":%u}",n.fire_requested,n.fire_shot_drains,n.fire_modes);
    // The partner's own reload (docs/native-puppet.md "Weapons" R1..R6): the event machine, the remote's v5
    // group and the post-phase-1 probe (bit 20 / bit 24 exist only inside the drain).
    j.Add(",\"reload\":{\"setting\":");j.String(n.reload_setting,sizeof(n.reload_setting));
    j.Add(",\"state\":");j.String(n.reload_state,sizeof(n.reload_state));
    j.Add(",\"last\":");j.String(n.reload_last,sizeof(n.reload_last));
    j.Add(",\"remote_valid\":%s,\"remote_reloading\":%s,\"remote_mode\":",Bool(n.reload_remote_valid),Bool(n.reload_remote_reloading));
    i32(n.reload_remote_mode);j.Add(",\"remote_seq\":");i32(n.reload_remote_seq);
    j.Add(",\"hold\":%s,\"stuck\":%s,\"starts\":%u,\"taps\":%u,\"entered\":%u,\"completed\":%u,\"stuck_n\":%u,\"fallbacks\":%u,",
        Bool(n.reload_hold),Bool(n.reload_stuck),n.reload_starts,n.reload_taps,n.reload_entered,n.reload_completed,n.reload_stuck_n,
        n.reload_fallbacks);
    j.Add("\"body_entries\":%u,\"sample_strip\":%u,",n.reload_body_entries,n.reload_sample_strip);
    // F1: every native exit counts (complete_late after stuck_ms), the queued taps, and the lengths (ms, 0 unmeasured).
    j.Add("\"completed_late\":%u,\"queued_taps\":%u,\"reconcile\":%s,\"len_ms\":%u,\"len_max_ms\":%u,\"remote_len_ms\":%u,"
          "\"refill_ms\":%u,",n.reload_completed_late,n.reload_queued_taps,Bool(n.reload_reconcile),n.reload_len_ms,n.reload_len_max_ms,
        n.reload_remote_len_ms,n.reload_refill_ms);
    j.Add("\"probe1\":{\"b20\":%u,\"b24\":%u,\"shots\":%u,\"mode_changes\":%u,\"mode\":%u}}",
        n.probe1_b20,n.probe1_b24,n.probe1_shots,n.probe1_mode_changes,n.probe1_mode);
    // The weapon switch (W4/W6): body 1's slots, the plan and the follower.
    j.Add(",\"slots\":{\"setting\":");j.String(n.slot_setting,sizeof(n.slot_setting));
    j.Add(",\"remote_slot\":");i32(n.slot_remote);j.Add(",\"remote_seq\":");i32(n.slot_remote_seq);
    j.Add(",\"body_slot\":");i32(n.slot_body);j.Add(",\"hand_slot\":");i32(n.gun_hand_slot);
    j.Add(",\"target\":");i32(n.slot_target);j.Add(",\"read_ok\":%s,\"handles\":[",Bool(n.slots_ok));
    for(unsigned s=0;s<3;++s){
        if(s)j.Add(",");
        if(!n.slots_ok || !n.slot_filled[s]) j.Add("null");
        else j.Add("{\"equip\":%u,\"type\":%u}",unsigned(n.slot_equip[s]),unsigned(n.slot_type[s]));
    }
    j.Add("],\"plan\":[");
    for(unsigned s=0;s<3;++s){if(s)j.Add(",");j.String(n.slot_tier[s],sizeof(n.slot_tier[s]));}
    j.Add("],\"ok_mask\":%u,\"writes\":%u,\"refused\":%u,\"last\":",unsigned(n.slot_ok_mask),n.slot_writes,n.slot_refused);
    j.String(n.slot_last,sizeof(n.slot_last));
    j.Add(",\"faulted\":%s,\"switching\":%s",Bool(n.slot_faulted),Bool(n.body_switching));
    // The generalized writer (W3): per slot what it planned and did.
    j.Add(",\"writer\":{\"active\":%s,\"dispatches\":%u,\"bound\":%u,\"refused\":%u,\"slots\":[",Bool(n.writer_active),
        n.writer_dispatches,n.writer_bound,n.writer_refused);
    for(unsigned s=0;s<3;++s){
        if(s)j.Add(",");
        j.Add("{\"want\":%u,\"equip\":%u,\"tier\":",unsigned(n.writer_want[s]),unsigned(n.writer_equip[s]));
        j.String(n.writer_tier[s],sizeof(n.writer_tier[s]));j.Add(",\"family\":");j.String(n.writer_family[s],sizeof(n.writer_family[s]));
        j.Add(",\"state\":");j.String(n.writer_state[s],sizeof(n.writer_state[s]));
        j.Add(",\"reason\":");j.String(n.writer_reason[s],sizeof(n.writer_reason[s]));
        j.Add(",\"handle\":%u,\"want_type\":%u,\"candidates\":%u,\"candidate\":%u}",n.writer_handle[s],unsigned(n.writer_want_type[s]),
            n.writer_candidates[s],unsigned(n.writer_candidate[s]));
    }
    j.Add("]}}");
    // Facing (partner_recon governor) and the look (D3).
    j.Add(",\"facing\":{\"setting\":");j.String(n.face_setting,sizeof(n.face_setting));
    j.Add(",\"mode\":");j.String(n.face_mode,sizeof(n.face_mode));
    j.Add(",\"err_cdeg\":");i32(n.face_err_cdeg);j.Add(",\"target_cdeg\":");i32(n.face_target_cdeg);
    j.Add(",\"body_cdeg\":");i32(n.face_body_cdeg);j.Add(",\"rate_dps\":");i32(n.face_rate_dps);
    j.Add(",\"writes\":%u,\"snaps\":%u,\"contests\":%u,\"turns\":%u,\"reason\":",n.face_writes,n.face_snaps,n.face_contests,n.face_turns);
    j.String(n.face_reason,sizeof(n.face_reason));
    j.Add(",\"place_yaw_err_mdeg\":");i32(n.place_yaw_err_mdeg);j.Add(",\"neutral_windows\":%u}",n.neutral_windows);
    j.Add(",\"look\":{\"src\":");j.String(n.look_src,sizeof(n.look_src));
    j.Add(",\"src_hits_s\":");if(!n.per_s_valid) j.Add("null"); else j.Add("%u",n.look_src_hits_s);
    j.Add(",\"src_hits\":%u,\"src_refused\":%u,\"src_skipped\":%u,\"remote_cdeg\":[",n.look_src_hits,n.look_src_refused,n.look_src_skipped);
    i32(n.look_remote_cdeg[0]);j.Add(",");i32(n.look_remote_cdeg[1]);j.Add("],\"slot_cdeg\":[");
    i32(n.look_slot_cdeg[0]);j.Add(",");i32(n.look_slot_cdeg[1]);j.Add("],\"err_mdeg\":");i32(n.look_err_mdeg);j.Add("}");
    // Aim parity (docs/native-puppet.md "Facing", the aim-target mirror): [k] = instance k (1 = the partner body).
    const auto vec=[&](const std::int32_t (&v)[3]) noexcept {j.Add("[");i32(v[0]);j.Add(",");i32(v[1]);j.Add(",");i32(v[2]);j.Add("]");};
    const auto pair=[&](const std::int32_t (&v)[2]) noexcept {j.Add("[");i32(v[0]);j.Add(",");i32(v[1]);j.Add("]");};
    j.Add(",\"aim\":{\"setting\":");j.String(n.aim_setting,sizeof(n.aim_setting));
    j.Add(",\"override\":%s,\"state\":",Bool(n.aim_override));j.String(n.aim_state,sizeof(n.aim_state));
    j.Add(",\"reason\":");j.String(n.aim_reason,sizeof(n.aim_reason));j.Add(",\"src\":");j.String(n.aim_src,sizeof(n.aim_src));
    j.Add(",\"writes\":%u,\"refused\":%u,\"holds\":%u,\"overwritten\":%u,\"restores\":%u,\"drops\":%u,\"target_mm\":",
        n.aim_writes,n.aim_refused,n.aim_holds,n.aim_overwritten,n.aim_restores,n.aim_drops);
    vec(n.aim_target_mm);j.Add(",\"written_mm\":");vec(n.aim_written_mm);j.Add(",\"slot\":");j.String(n.aim_slot,sizeof(n.aim_slot));
    j.Add(",\"q10_1_mm\":");vec(n.aim_q10_mm[1]);j.Add(",\"q10_0_mm\":");vec(n.aim_q10_mm[0]);
    j.Add(",\"aimpt1_mm\":");vec(n.aim_point_mm[1]);j.Add(",\"aimpt0_mm\":");vec(n.aim_point_mm[0]);
    j.Add(",\"aimpt_err_mm\":");i32(n.aim_point_err_mm);
    j.Add(",\"aim_yaw1_cdeg\":");i32(n.aim_yaw_cdeg[1]);j.Add(",\"aim_pitch1_cdeg\":");i32(n.aim_pitch_cdeg[1]);
    j.Add(",\"aim_yaw0_cdeg\":");i32(n.aim_yaw_cdeg[0]);j.Add(",\"aim_pitch0_cdeg\":");i32(n.aim_pitch_cdeg[0]);
    j.Add(",\"rel_yaw1_cdeg\":");i32(n.aim_rel_yaw_cdeg[1]);j.Add(",\"rel_yaw0_cdeg\":");i32(n.aim_rel_yaw_cdeg[0]);
    j.Add(",\"r5c_1\":");i32(n.aim_r5c[1]);j.Add(",\"r60_1\":");i32(n.aim_r60[1]);
    j.Add(",\"r5c_0\":");i32(n.aim_r5c[0]);j.Add(",\"r60_0\":");i32(n.aim_r60[0]);
    j.Add(",\"pred1\":");i32(n.aim_pred[1]);j.Add(",\"pred0\":");i32(n.aim_pred[0]);
    j.Add(",\"gun_state\":");pair(n.aim_gun_state);j.Add(",\"b3d5\":");pair(n.aim_b3d5);
    j.Add(",\"path1\":");j.String(n.aim_path[1],sizeof(n.aim_path[1]));j.Add(",\"path0\":");j.String(n.aim_path[0],sizeof(n.aim_path[0]));
    j.Add("}");
    j.Add(",\"leak\":{\"shot_count\":");i64(n.shot_count);
    j.Add(",\"shot_leaks\":%u,\"reload_drains\":%u,\"ammo_leaks\":%u,\"ammo_vars\":",n.shot_leaks,n.reload_drains,n.ammo_leaks);
    i64(n.ammo_digest);j.Add("}");
    j.Add(",\"equip\":{\"tx_rev\":%u,\"tx_base\":[%u,%u,%u],\"tx_alias\":[%u,%u,%u],\"tables_ok\":%s,",
        n.equip_tx_rev,n.equip_tx_base[0],n.equip_tx_base[1],n.equip_tx_base[2],n.equip_tx_alias[0],n.equip_tx_alias[1],n.equip_tx_alias[2],
        Bool(n.equip_tables_ok));
    j.Add("\"rx_base\":[%u,%u,%u],\"rx_alias\":[%u,%u,%u],",n.equip_rx_base[0],n.equip_rx_base[1],n.equip_rx_base[2],
        n.equip_rx_alias[0],n.equip_rx_alias[1],n.equip_rx_alias[2]);
    j.Add("\"tx_type\":[%u,%u,%u],\"rx_type\":[%u,%u,%u],\"rx_version\":%u,\"apply\":",unsigned(n.equip_tx_type[0]),
        unsigned(n.equip_tx_type[1]),unsigned(n.equip_tx_type[2]),unsigned(n.equip_rx_type[0]),unsigned(n.equip_rx_type[1]),
        unsigned(n.equip_rx_type[2]),n.equip_rx_version);
    j.String(n.equip_apply,sizeof(n.equip_apply));
    // The generalized writer's setting, the stock cache census and the preload (W2).
    j.Add(",\"setting\":");j.String(n.equip_setting,sizeof(n.equip_setting));
    // The used stock cache entries, compact: [index, id, type, receiver, flags, refs].
    j.Add(",\"resident_fields\":[\"index\",\"id\",\"type\",\"receiver\",\"flags\",\"refs\"],\"resident\":");
    if(!n.resident_ok) j.Add("null");
    else {
        j.Add("[");
        for(unsigned i=0;i<n.resident_count && i<48;++i){
            const auto& r=n.resident[i];
            j.Add("%s[%u,%u,%u,%u,%u,%u]",i?",":"",unsigned(r.index),unsigned(r.equip),unsigned(r.type),unsigned(r.receiver),
                unsigned(r.flags),r.refs);
        }
        j.Add("]");
    }
    j.Add(",\"preload\":{\"ids\":[");
    for(unsigned i=0;i<n.preload_count && i<3;++i) j.Add("%s%u",i?",":"",unsigned(n.preload_ids[i]));
    j.Add("],\"verdict\":");j.String(n.preload_verdict,sizeof(n.preload_verdict));
    j.Add(",\"reason\":");j.String(n.preload_reason,sizeof(n.preload_reason));
    j.Add(",\"new_hashes\":%u}}",n.preload_new_hashes);
    // Native rates (docs/native-puppet.md "Rates"): x1000, records in rec+0x3C..+0x50 order.
    const auto record=[&](bool ok,const std::int32_t (&f)[6]) noexcept {
        if(!ok){j.Add("null");return;}
        j.Add("{\"crawl\":");i32(f[0]);j.Add(",\"crawl_factor\":");i32(f[1]);j.Add(",\"crawl_rate\":");i32(f[2]);
        j.Add(",\"dash\":");i32(f[3]);j.Add(",\"dash_factor\":");i32(f[4]);j.Add(",\"dash_rate\":");i32(f[5]);j.Add("}");
    };
    j.Add(",\"rates\":{\"setting\":\"%s\",\"id0\":",PartnerNativeRatesName(n.rates_setting));record(n.rates_id0_ok,n.rates_id0);
    j.Add(",\"id1\":");record(n.rates_id1_ok,n.rates_id1);
    j.Add(",\"id1_read\":");j.String(n.rates_id1_refusal,sizeof(n.rates_id1_refusal));
    j.Add(",\"remote\":{\"valid\":%s,\"age_ms\":",Bool(n.rates_remote_ok));
    if(n.rates_remote_age_ms==0xFFFFFFFFu) j.Add("null"); else j.Add("%u",n.rates_remote_age_ms);
    j.Add(",\"dash\":%u,\"crawl\":%u,\"crawl_factor\":%u},\"mirror\":",n.rates_remote[0],n.rates_remote[1],n.rates_remote[2]);
    j.String(n.rates_mirror,sizeof(n.rates_mirror));j.Add(",\"reason\":");j.String(n.rates_reason,sizeof(n.rates_reason));
    j.Add(",\"writes\":%u,\"reapplied\":%u,\"restores\":%u,\"drops\":%u}",n.rates_writes,n.rates_reapplied,n.rates_restores,n.rates_drops);
    j.Add(",\"ring_fields\":[\"ms\",\"mode\",\"remote_stance\",\"body_stance\",\"remote_mmps\",\"body_mmps\",\"err_mm\",\"steer_mdeg\",\"held\",\"gun3c0\","
          "\"act\",\"rec_act\",\"rec_next\",\"rec_mag\",\"p1_forced\",\"recon_mode\",\"recon_action\",\"warp\",\"mag\",\"body_yaw_cdeg\","
          "\"face_err_cdeg\",\"r60_1\",\"pred1\",\"gmode\",\"rmode\",\"hslot\"],\"ring\":[");
    for(std::size_t i=0;i<n.ring.count;++i){
        const auto& e=partner_native::At(n.ring,i);if(i)j.Add(",");
        j.Add("[%u,\"%s\",%u,%u,%d,%d,",e.ms,partner_native::ModeName(static_cast<partner_native::Mode>(e.mode)),unsigned(e.remote_stance),
            unsigned(e.body_stance),e.remote_mmps,e.body_mmps);
        i32(e.err_mm);j.Add(",%d,%u,%u,%u,%u,%u,",e.steer_mdeg,e.held,e.gun3c0,unsigned(e.act),unsigned(e.rec_act),unsigned(e.rec_next));
        i32(e.rec_mag);
        const bool recon=e.recon_mode!=partner_native::kReconOff;
        j.Add(",%u,\"%s\",\"%s\",%s,",unsigned(e.p1_forced),
            recon?partner_recon::ModeName(static_cast<partner_recon::Mode>(e.recon_mode)):"off",
            recon?partner_recon::ActionName(static_cast<partner_recon::Action>(e.recon_action)):"off",Bool(e.warp));
        if(e.mag==0xFFFFu) j.Add("null,"); else j.Add("%u,",unsigned(e.mag));
        i32(e.body_yaw_cdeg);j.Add(",");i32(e.face_err_cdeg);j.Add(",");i32(e.r60_1);
        if(e.pred1<0) j.Add(",null"); else j.Add(",%d",int(e.pred1));
        const auto byte=[&](std::uint8_t v) noexcept { if(v==0xFFu) j.Add(",null"); else j.Add(",%u",unsigned(v)); };
        byte(e.gmode);byte(e.rmode);byte(e.hslot);j.Add("]");
    }
    j.Add("]}");
}
// Steam self-test object (docs/steam-online.md 14).
void SteamJson(Json& j,const steam_state::SelftestReport& r) noexcept {
    const auto flag=[](bool v) noexcept { return v?1u:0u; };
    j.Add(",\"steam\":{\"selftest\":");
    j.String(!r.configured?"off":r.verdict[0]?r.verdict:"pending",16);
    j.Add(",\"stage\":");j.String(r.stage,sizeof(r.stage));
    j.Add(",\"api\":%u,\"client\":%u,\"logged_on\":%u,\"messages\":%u,\"sockets\":%u,\"utils\":%u,\"rtti\":%u,\"shape\":%u,\"relay\":%d",
        flag(r.api),flag(r.client),flag(r.logged_on),flag(r.messages),flag(r.sockets),flag(r.utils),flag(r.rtti),flag(r.shape),r.relay);
    const auto pair=[&](const char* key,const steam_state::PairResult& p) noexcept {
        j.Add(",\"%s\":",key);
        if(!p.ran){j.Add("null");return;}
        const unsigned long long leak=p.received>=p.release?p.received-p.release:0ull;
        j.Add("{\"hs_ms\":%u,\"frames\":%u,\"frames_rx\":%u,\"rel\":%u,\"rel_rx\":%u,\"rtt_ms\":%u,\"release\":%llu,\"leak\":%llu,\"bad_identity\":%u}",
            p.hs_ms,p.frames,p.frames_rx,p.rel,p.rel_rx,p.rtt_ms,static_cast<unsigned long long>(p.release),leak,p.bad_identity);
    };
    pair("pair",r.pair);pair("lpair",r.lpair);
    j.Add(",\"inert\":{\"recv\":%d,\"state\":%d,\"accept\":%u},\"trip\":{\"getter_delta\":%lld},\"reason\":",
        r.inert_recv,r.inert_state,flag(r.inert_accept),static_cast<long long>(r.getter_delta));
    j.String(r.reason,sizeof(r.reason));
    // Stage F/D (docs/steam-online.md 20.6): the function-pointer dispatch.
    const steam_state::FnResult& f=r.fn;
    j.Add(",\"fn\":");
    if(!f.ran && !f.skipped){j.Add("null");}
    else{
        j.Add("{\"verdict\":\"%s\",\"ran\":%u,\"skipped\":%u,\"gate\":",steam_state::FnVerdict(f),flag(f.ran),flag(f.skipped));
        j.String(f.gate,sizeof(f.gate));
        j.Add(",\"set204\":%u,\"reset204\":%u,\"pre_pump\":%u,\"status_cb\":%u,\"conn_ok\":%u,\"old\":%d,\"new\":%d,\"thread_ok\":%u,"
              "\"extra\":%u,\"d_install\":%u,\"d_pumps\":%u,\"d_dispatched\":%u,\"d_uninstall\":%u,\"faults\":%u,\"reason\":",
            flag(f.set204),flag(f.reset204),f.pre_pump,f.status_cb,flag(f.conn_ok),f.old_state,f.new_state,flag(f.thread_ok),f.extra,
            flag(f.d_install),f.d_pumps,f.d_dispatched,flag(f.d_uninstall),f.faults);
        j.String(f.reason,sizeof(f.reason));
        j.Add("}");
    }
    j.Add("}");
}
} // namespace
std::size_t FormatSteamJson(const steam_state::SelftestReport& report, char* out, std::size_t capacity) noexcept {
    if(!out || !capacity) return 0;
    Json j{out,capacity};
    SteamJson(j,report);
    if(!j.ok){out[0]=0;return 0;}
    return j.used;
}
std::size_t FormatNativeJson(const partner_native::Telemetry& telemetry, char* out, std::size_t capacity) noexcept {
    if(!out || !capacity) return 0;
    Json j{out,capacity};
    NativeJson(j,telemetry);
    if(!j.ok){out[0]=0;return 0;}
    return j.used;
}
}

#if defined(_WIN32) && defined(MGS_BACKGROUND_WORKER) && MGS_BACKGROUND_WORKER
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/debug_overlay.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "mgscoop/nametag.hpp"
namespace mgscoop::worker_debug {
namespace {
std::uint64_t last_poll{}, last_sequence{}, accepted_sequence{};
bool last_queued{}, queued_menu{};
std::uint32_t queued_ref{};
// Owner thread only: one static 64 KB buffer instead of a large game-thread stack frame
char json_bytes[65536]{};
char nametag_json[4096]{};
void Publish(const worker_scenario::RunContext& c) noexcept {
    const auto s=debug_menu::GetSnapshot();Json j{json_bytes,sizeof(json_bytes)};
    j.Add("{\"schema\":2,\"queued_kind\":\"%s\",\"queued_ref\":%u,",queued_menu?"menu":"command",queued_ref);
    j.Add("\"run_id\":\"%s\",\"role\":\"%s\",\"pid\":%u,\"process_start_filetime\":%llu,\"tick_ms\":%llu,\"debug_sequence\":%llu,\"queued_sequence\":%llu,\"last_queued\":%s,",
        c.run_id,c.role,c.pid,c.process_start_filetime,GetTickCount64(),last_sequence,accepted_sequence,Bool(last_queued));
    j.Add("\"mission\":%u,\"generation\":%u,\"enabled\":%s,\"offline\":%s,\"saves_redirected\":%s,\"gameplay_ready\":%s,\"terminating\":%s,\"realized\":%s,\"body_valid\":%s,\"pose_valid\":%s,",
        s.mission,s.generation,Bool(s.enabled),Bool(s.offline),Bool(s.saves_redirected),Bool(s.gameplay_ready),Bool(s.terminating),Bool(s.realized),Bool(s.body_valid),Bool(s.pose_valid));
    j.Add("\"world_level\":%u,\"world_paired\":%s,\"world_effective\":%u,\"remote_action_flags\":%u,\"remote_equip\":%u,\"remote_act\":%u,\"mapped_clip\":%u,\"motion_count\":%u,\"remote_age_ms\":%u,",
        s.world_level,Bool(s.world_paired),s.world_effective,s.remote_action_flags,s.remote_equip,s.remote_act,s.mapped_clip,s.motion_count,s.remote_age_ms);
    j.Add("\"local_anim_valid\":%s,\"local_act\":%u,\"local_anim_stance\":%u,\"local_anim_speed\":%u,\"remote_anim_valid\":%s,\"remote_stance\":%u,\"remote_anim_stance\":%u,\"remote_anim_speed\":%u,\"dm_stance\":%u,\"dm_move\":%u,\"dm_phase\":",
        Bool(s.local_anim_valid),s.local_act,s.local_anim_stance,s.local_anim_speed,Bool(s.remote_anim_valid),s.remote_stance,s.remote_anim_stance,s.remote_anim_speed,s.dm_stance,s.dm_move);
    j.String(s.dm_phase,sizeof(s.dm_phase));j.Add(",\"local_yaw_valid\":%s,\"player_yaw\":",Bool(s.local_yaw_valid));j.Float(s.player_yaw);
    j.Add(",\"remote_yaw\":");j.Float(s.remote_yaw);j.Add(",\"body_yaw\":");j.Float(s.body_yaw);j.Add(",");
    j.Add("\"dm_speed_valid\":%s,\"dm_speed_mmps\":%u,",Bool(s.dm_speed_valid),s.dm_speed_mmps);
    j.Add("\"player_pos\":");j.Position(s.player_pos);j.Add(",\"remote_pos\":");j.Position(s.remote_pos);j.Add(",\"body_pos\":");j.Position(s.body_pos);
    j.Add(",\"heading_error_deg\":");j.Float(s.heading_error_deg);j.Add(",\"position_error_m\":");j.Float(s.position_error_m);
    // Receiver facing evidence: per owner tick, oldest first (degrees, null unread; clip/move 255 none).
    j.Add(",\"facing\":{\"active\":%s,\"limit_dps\":%u,\"snaps\":%u,\"last_snap\":",Bool(s.facing_active),s.facing_limit_dps,s.facing_snaps);
    j.String(s.facing_last_snap,sizeof(s.facing_last_snap));
    j.Add(",\"target_yaw\":");j.Float(s.facing_target_yaw);j.Add(",\"applied_yaw\":");j.Float(s.facing_applied_yaw);
    j.Add(",\"rate_dps\":");j.Float(s.facing_rate_dps);
    j.Add(",\"ring_fields\":[\"ms\",\"source_yaw\",\"source_body_yaw\",\"target_yaw\",\"applied_yaw\",\"body_yaw\",\"clip\",\"move\",\"snap\"],\"ring\":[");
    for(std::size_t i=0;i<s.facing_ring.count;++i){
        const auto& f=partner_facing::At(s.facing_ring,i);if(i)j.Add(",");
        j.Add("[%u,",f.ms);j.Float(f.source_yaw);j.Add(",");j.Float(f.source_body_yaw);j.Add(",");j.Float(f.target_yaw);j.Add(",");
        j.Float(f.applied_yaw);j.Add(",");j.Float(f.body_yaw);j.Add(",%u,%u,\"%s\"]",unsigned(f.clip),unsigned(f.move),partner_facing::SnapName(f.snap));
    }
    j.Add("]}");
    NativeJson(j,s.native);
    // Partner name tag (docs/partner-nametag.md): game-thread telemetry plus the Present counters.
    if(nametag::FormatJson(nametag::GetTelemetry(),nametag::GetRenderStats(),nametag_json,sizeof(nametag_json)))j.Add("%s",nametag_json);
    SteamJson(j,steam_state::GetReport());
    // The in-game status line and toasts (coop_hud; Present side).
    char hud_json[512]{};
    if(coop_hud::FormatJson(coop_hud::GetTelemetry(),hud_json,sizeof(hud_json)))j.Add("%s",hud_json);
    j.Add(",\"queue_depth\":%u,\"last_id\":%u,\"completed\":%u,\"rejected\":%u,\"last_command\":",s.queue_depth,s.last_id,s.completed,s.rejected);
    j.String(s.last_command,sizeof(s.last_command));j.Add(",\"last_result\":");j.String(s.last_result,sizeof(s.last_result));j.Add(",\"refusal\":");j.String(s.refusal,sizeof(s.refusal));
    j.Add(",\"history\":[");
    for(unsigned i=0;i<8;++i){
        char line[260]{};const auto& r=s.receipts[i];
        if(r.result[0])StringCchPrintfA(line,ARRAYSIZE(line),"#%u %s %s -> %s",r.id,r.status==0?"OK":r.status==1?"REFUSED":r.status==2?"PENDING":"QUEUE",r.command,r.result);
        if(i)j.Add(",");j.String(line,sizeof(line));
    }
    j.Add("],\"receipts\":[");
    for(unsigned i=0;i<debug_menu::kReceiptCount;++i){
        const auto& r=s.receipts[i];if(i)j.Add(",");
        j.Add("{\"id\":%u,\"status\":%u,\"command\":",r.id,static_cast<unsigned>(r.status));j.String(r.command,sizeof(r.command));
        j.Add(",\"result\":");j.String(r.result,sizeof(r.result));j.Add("}");
    }
    j.Add("],\"enemy_nearby\":[");
    for(unsigned i=0;i<s.enemy_nearby_count && i<debug_menu::kNearbyCount;++i){
        const auto& e=s.enemy_nearby[i];if(i)j.Add(",");
        j.Add("{\"name\":");j.String(e.name,sizeof(e.name));j.Add(",\"hash\":%u,\"state\":%u,\"flags\":%u,\"distance_mm\":%d}",e.hash,e.state,e.flags,e.distance_mm);
    }
    j.Add("],\"enemy_nearby_radius\":%u,\"enemy_camps_loaded\":%s,\"enemy_spawn_valid\":%s,\"enemy_spawn_hash\":%u,\"enemy_spawn_pos_mm\":[%d,%d,%d]",
        s.enemy_nearby_radius,Bool(s.enemy_camps_loaded),Bool(s.enemy_spawn_valid),s.enemy_spawn_hash,s.enemy_spawn_pos_mm[0],s.enemy_spawn_pos_mm[1],s.enemy_spawn_pos_mm[2]);
    j.Add(",\"host\":%s,\"infinite_ammo\":%s,\"bookmark_mask\":%u,\"god_mode\":%s,\"fulton_infinite\":%s,\"slowmo_active\":%s,\"game_clock_s\":%u,\"player_life\":%d,\"player_life_max\":%d,\"guard_reason\":",
        Bool(s.host),Bool(s.infinite_ammo),s.bookmark_mask,Bool(s.god_mode),Bool(s.fulton_infinite),Bool(s.slowmo_active),s.game_clock_s,s.player_life,s.player_life_max);
    j.String(s.guard_reason,sizeof(s.guard_reason));
    const ULONGLONG tick=GetTickCount64();
    j.Add(",\"pending_id\":%u,\"pending_kind\":",s.pending_id);j.String(s.pending_kind,sizeof(s.pending_kind));
    j.Add(",\"pending_frames\":%u,\"interim\":%u,\"telemetry_age_ms\":%llu,\"link_state\":",s.pending_frames,s.interim,
        s.published_tick64 && tick>=s.published_tick64?tick-s.published_tick64:0ull);
    j.String(s.link_state,sizeof(s.link_state));
    j.Add(",\"rtt_ms\":%u,\"jitter_ms\":%u,\"loss_permille\":%u,\"interp_ms\":%u,\"stage\":%u",s.rtt_ms,s.jitter_ms,s.loss_permille,s.interp_ms,s.stage);
    const struct {const char* key;const char* value;std::size_t size;} texts[]={{"source",s.source,sizeof(s.source)},{"apply",s.apply,sizeof(s.apply)},
        {"anim",s.anim,sizeof(s.anim)},{"aset",s.aset,sizeof(s.aset)},{"dm",s.dm,sizeof(s.dm)},{"look",s.look,sizeof(s.look)},
        {"overlay_category",s.overlay_category,sizeof(s.overlay_category)},{"overlay_search",s.overlay_search,sizeof(s.overlay_search)},
        {"overlay_result",s.overlay_result,sizeof(s.overlay_result)},{"last_ui_action",s.last_ui_action,sizeof(s.last_ui_action)}};
    for(const auto& t:texts){j.Add(",\"%s\":",t.key);j.String(t.value,t.size);}
    j.Add(",\"input_armed\":%s,\"parts_loaded\":%s,\"range_hidden\":%s,\"remote_valid\":%s,\"world_configured\":%s,\"world_link_up\":%s",
        Bool(s.input_armed),Bool(s.parts_loaded),Bool(s.range_hidden),Bool(s.remote_valid),Bool(s.world_configured),Bool(s.world_link_up));
    j.Add(",\"overlay_open\":%s,\"overlay_frames\":%u,\"overlay_request_serial\":%u,\"overlay_result_serial\":%u",
        Bool(s.overlay_open),s.overlay_frames,s.overlay_request_serial,s.overlay_result_serial);
    j.Add(",\"enemy_camps\":[");for(unsigned i=0;i<s.enemy_camp_count && i<32;++i){if(i)j.Add(",");j.Add("{\"name\":");j.String(s.enemy_camps[i].name,sizeof(s.enemy_camps[i].name));j.Add(",\"distance_mm\":%d,\"route_count\":%u}",s.enemy_camps[i].distance_mm,s.enemy_camps[i].route_count);}
    j.Add("],\"enemy_routes_camp\":");j.String(s.enemy_routes_camp,sizeof(s.enemy_routes_camp));j.Add(",\"enemy_routes\":[");
    for(unsigned i=0;i<s.enemy_route_count && i<64;++i){if(i)j.Add(",");j.Add("{\"name\":");j.String(s.enemy_routes[i].name,sizeof(s.enemy_routes[i].name));j.Add(",\"index\":%u}",s.enemy_routes[i].index);}
    j.Add("],\"worker_input\":{");
    const auto input = debug_overlay::GetWorkerInputStats();
    j.Add("\"raw_keyboard_posted\":%u", input.raw_keyboard_posted);
    j.Add(",\"raw_mouse_posted\":%u", input.raw_mouse_posted);
    j.Add(",\"raw_post_failed\":%u", input.raw_post_failed);
    j.Add(",\"raw_mouse_axes_posted\":%u", input.raw_mouse_axes_posted);
    j.Add(",\"raw_mouse_buttons_posted\":%u", input.raw_mouse_buttons_posted);
    j.Add(",\"raw_keyboard_reads\":%u", input.raw_keyboard_reads);
    j.Add(",\"raw_mouse_reads\":%u", input.raw_mouse_reads);
    j.Add(",\"raw_mouse_axes_reads\":%u", input.raw_mouse_axes_reads);
    j.Add(",\"raw_mouse_buttons_reads\":%u", input.raw_mouse_buttons_reads);
    j.Add(",\"raw_keyboard_packets_read\":%u", input.raw_keyboard_packets_read);
    j.Add(",\"raw_mouse_packets_read\":%u", input.raw_mouse_packets_read);
    j.Add(",\"raw_mouse_axis_packets_read\":%u", input.raw_mouse_axis_packets_read);
    j.Add(",\"raw_mouse_button_packets_read\":%u", input.raw_mouse_button_packets_read);
    j.Add(",\"raw_read_refused\":%u", input.raw_read_refused);
    j.Add(",\"cursor_success\":%u", input.cursor_success);
    j.Add(",\"cursor_null\":%u", input.cursor_null);
    j.Add(",\"cursor_thread_refused\":%u", input.cursor_thread_refused);
    j.Add(",\"cursor_window_refused\":%u", input.cursor_window_refused);
    j.Add(",\"cursor_geometry_refused\":%u", input.cursor_geometry_refused);
    j.Add(",\"cursor_map_refused\":%u", input.cursor_map_refused);
    j.Add(",\"cursor_window_match\":%u", input.cursor_window_match);
    j.Add(",\"cursor_window_mismatch\":%u", input.cursor_window_mismatch);
    j.Add(",\"cursor_present_missing\":%u", input.cursor_present_missing);
    j.Add(",\"di_mouse_state_calls\":%u", input.di_mouse_state_calls);
    j.Add(",\"di_mouse_state_supplied\":%u", input.di_mouse_state_supplied);
    j.Add(",\"di_mouse_state_axes\":%u", input.di_mouse_state_axes);
    j.Add(",\"di_mouse_state_buttons\":%u", input.di_mouse_state_buttons);
    j.Add(",\"di_mouse_buffer_calls\":%u", input.di_mouse_buffer_calls);
    j.Add(",\"di_mouse_buffer_events\":%u", input.di_mouse_buffer_events);
    j.Add(",\"di_mouse_buffer_peek_events\":%u", input.di_mouse_buffer_peek_events);
    j.Add("}}\n");if(!j.ok)return;
    wchar_t path[32768]{},temporary[32768]{};
    if(FAILED(StringCchPrintfW(path,ARRAYSIZE(path),L"%s\\%S.state.json",c.run_dir,c.role)) ||
       FAILED(StringCchPrintfW(temporary,ARRAYSIZE(temporary),L"%s.%llu.tmp",path,GetTickCount64())))return;
    HANDLE file=CreateFileW(temporary,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    BY_HANDLE_FILE_INFORMATION info{};DWORD written=0;
    const bool ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
        WriteFile(file,j.bytes,static_cast<DWORD>(j.used),&written,nullptr) && written==j.used;
    CloseHandle(file);
    // Bounded retry: sync clients/scanners can briefly hold the published file.
    for(int attempt=0;ok&&attempt<20;++attempt){if(MoveFileExW(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))break;
        const DWORD error=GetLastError();if((error!=ERROR_ACCESS_DENIED&&error!=ERROR_SHARING_VIOLATION)||attempt==19){DeleteFileW(temporary);break;}Sleep(5);}
}
}
void PollOwner() noexcept {
    if(!worker_scenario::Active() || !worker_scenario::ThreadAdmitted())return;
    const DWORD incoming=GetLastError();const auto now=GetTickCount64();
    if(last_poll && now-last_poll<100){SetLastError(incoming);return;}last_poll=now;
    const auto c=worker_scenario::GetContext();wchar_t path[32768]{};
    if(SUCCEEDED(StringCchPrintfW(path,ARRAYSIZE(path),L"%s\\%S.debug",c.run_dir,c.role))) {
        HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file!=INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};char text[385]{};DWORD read=0;
            const bool ok=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT) &&
                GetFileSizeEx(file,&size) && size.QuadPart>0 && size.QuadPart<=384 && ReadFile(file,text,384,&read,nullptr) && read==size.QuadPart;
            CloseHandle(file);Request request{};
            if(ok && Parse(text,read,c.run_id,c.role,request) && request.sequence>last_sequence) {
                const bool timely=Timely(request,now,last_sequence);last_sequence=request.sequence;
                // "menu ..." drives the worker-drawn overlay; everything else is a game command.
                // Both record the reference (command id or overlay serial) the runner pairs with.
                const bool menu=IsMenuText(request.text);
                const std::uint32_t ref=!timely?0:menu?debug_menu::RequestMenu(request.text,request.mission,request.generation):
                    debug_menu::QueueCommandForGeneration(request.text,request.mission,request.generation);
                last_queued=ref!=0;
                if(last_queued){accepted_sequence=request.sequence;queued_menu=menu;queued_ref=ref;}
            }
        }
    }
    Publish(c);SetLastError(incoming);
}
}
#else
namespace mgscoop::worker_debug {void PollOwner() noexcept {}}
#endif
