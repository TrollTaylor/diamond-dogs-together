#include "mgscoop/worker_debug.hpp"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/nametag_core.hpp"
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/steam_online_state.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>
namespace {
// Strict RFC 8259 syntax check (no extensions): the worker state.json must
// stay parseable by ConvertFrom-Json and the coordinator's tools.
struct JsonCheck {
    const char* p; const char* e;
    void Ws() { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p; }
    bool Lit(const char* s) { const std::size_t n = std::strlen(s); if (static_cast<std::size_t>(e - p) >= n && !std::memcmp(p, s, n)) { p += n; return true; } return false; }
    bool Digit() const { return p < e && *p >= '0' && *p <= '9'; }
    bool String() {
        if (p >= e || *p != '"') return false;
        for (++p; p < e && *p != '"'; ++p) {
            const auto c = static_cast<unsigned char>(*p);
            if (c < 0x20) return false;
            if (c != '\\') continue;
            if (++p >= e) return false;
            if (*p == 'u') { for (int i = 0; i < 4; ++i) if (++p >= e || !std::isxdigit(static_cast<unsigned char>(*p))) return false; }
            else if (!std::strchr("\"\\/bfnrt", *p)) return false;
        }
        if (p >= e) return false;
        ++p; return true;
    }
    bool Number() {
        if (p < e && *p == '-') ++p;
        if (!Digit()) return false;
        if (*p == '0') ++p; else while (Digit()) ++p;
        if (p < e && *p == '.') { ++p; if (!Digit()) return false; while (Digit()) ++p; }
        if (p < e && (*p == 'e' || *p == 'E')) { ++p; if (p < e && (*p == '+' || *p == '-')) ++p; if (!Digit()) return false; while (Digit()) ++p; }
        return true;
    }
    bool Value(int depth) {
        if (depth > 32) return false;
        Ws();
        if (p >= e) return false;
        if (*p == '{') {
            ++p; Ws();
            if (p < e && *p == '}') { ++p; return true; }
            // Duplicate member names are refused too: ConvertFrom-Json rejects them.
            std::vector<std::string> keys;
            for (;;) {
                Ws();
                const char* key = p;
                if (!String()) return false;
                std::string name(key, p);
                for (const auto& k : keys) if (k == name) return false;
                keys.push_back(std::move(name));
                Ws();
                if (p >= e || *p != ':') return false;
                ++p; if (!Value(depth + 1)) return false; Ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == '}') { ++p; return true; }
                return false;
            }
        }
        if (*p == '[') {
            ++p; Ws();
            if (p < e && *p == ']') { ++p; return true; }
            for (;;) {
                if (!Value(depth + 1)) return false; Ws();
                if (p < e && *p == ',') { ++p; continue; }
                if (p < e && *p == ']') { ++p; return true; }
                return false;
            }
        }
        if (*p == '"') return String();
        if (Lit("true") || Lit("false") || Lit("null")) return true;
        return Number();
    }
};
bool ValidJson(const std::string& text) {
    JsonCheck c{text.data(), text.data() + text.size()};
    if (!c.Value(0)) return false;
    c.Ws();
    return c.p == c.e;
}
// The "native" member wrapped the way Publish embeds it.
std::string NativeDocument(const mgscoop::partner_native::Telemetry& t, std::size_t* length = nullptr) {
    static char buffer[16384];
    const std::size_t n = mgscoop::worker_debug::FormatNativeJson(t, buffer, sizeof(buffer));
    if (length) *length = n;
    return std::string("{\"schema\":2") + std::string(buffer, n) + "}";
}
} // namespace
int main() {
    using namespace mgscoop::worker_debug;
    constexpr char run[]="0123456789abcdef0123456789abcdef";
    const std::string prefix=std::string("MGSWORKERDEBUG1 ")+run+" host ";
    Request request{};unsigned checks=0;
    auto check=[&](bool value){++checks;if(!value){std::printf("failed %u\n",checks);return false;}return true;};
    const auto valid=prefix+"1 1001 30010 2 weather rain\n";
    if(!check(Parse(valid.data(),valid.size(),run,"host",request) && request.sequence==1 && !std::strcmp(request.text,"weather rain")))return 1;
    if(!check(Timely(request,1000,0)) || !check(!Timely(request,1001,0)) || !check(!Timely(request,1000,1)))return 1;
    request.expires=16001;if(!check(!Timely(request,1000,0)))return 1;
    for(const auto& text:{prefix+"0 1001 30010 2 time noon",prefix+"1 0 30010 2 time noon",prefix+"1 1001 30010 2 ",prefix+"1 1001 30010 2  time noon",prefix+"1 1001 30010 2 time noon ",prefix+"1 1001 30010 2 time\nnoon",prefix+"1 1001 30010 2 time noon\r\n",prefix+"18446744073709551616 1001 30010 2 time noon",prefix+"1 1001 30010 2 "+std::string(201,'x')}) {
        if(!check(!Parse(text.data(),text.size(),run,"host",request)))return 1;
    }
    if(!check(!Parse(valid.data(),valid.size(),run,"guest",request)))return 1;
    std::string nul=prefix+"1 1001 30010 2 time noon";nul[20]='\0';if(!check(!Parse(nul.data(),nul.size(),run,"host",request)))return 1;
    for(const auto& text:{prefix+"1 1001 0 2 position",prefix+"1 1001 30010 0 position",prefix+"1 1001 65536 2 position",prefix+"1 1001 30010 4294967296 position",prefix+"1 1001 30010 2 "+std::string(201,'x')}) {
        if(!check(!Parse(text.data(),text.size(),run,"host",request)))return 1;
    }
    namespace d=mgscoop::debug_menu;
    d::Configure(true,true,true);d::BeginMission(30010);const auto before=d::GetSnapshot();
    if(!check(d::QueueCommandForGeneration("position",before.mission,before.generation)))return 1;
    d::Command popped{};if(!check(d::Pop(popped)))return 1;
    d::BeginMission(30020);
    if(!check(!d::QueueCommandForGeneration("position",before.mission,before.generation)))return 1;
    const auto after=d::GetSnapshot();
    if(!check(!d::QueueCommandForGeneration("position",before.mission,after.generation)) || !check(!d::QueueCommandForGeneration("position",after.mission,0)))return 1;
    if(!check(d::QueueCommandForGeneration("position",after.mission,after.generation)))return 1;
    if(!check(d::Pop(popped) && popped.generation==after.generation))return 1;
    // Native puppet state.json member (docs/native-puppet.md 11.1): valid JSON
    // in every shape (no rates yet, a full ring, nulls, hostile text).
    namespace pn=mgscoop::partner_native;
    {
        pn::Telemetry t{};
        std::size_t n=0;
        const auto doc=NativeDocument(t,&n);
        if(!check(n>0 && ValidJson(doc) && doc.find(",\"native\":{\"drive\":\"legacy\",\"mode\":\"off\"")!=std::string::npos &&
                  doc.find("\"per_s\":null")!=std::string::npos && doc.find("\"ring\":[]")!=std::string::npos &&
                  doc.find("\"runaway\":false,\"runaway_idle\":false,\"lag\":false,\"stop_grace\":false,\"dv_along_mmps\":0")!=std::string::npos &&
                  doc.find("\"rates\":{\"setting\":\"off\",\"id0\":null,\"id1\":null,\"id1_read\":\"\",\"remote\":{\"valid\":false,\"age_ms\":0,")!=std::string::npos))return 1;
    }
    {
        pn::Telemetry t{};
        t.configured=true;t.rates_setting=mgscoop::PartnerNativeRates::mirror;t.rates_id0_ok=t.rates_id1_ok=true;
        for(unsigned i=0;i<6;++i){t.rates_id0[i]=1200;t.rates_id1[i]=1000;}
        t.rates_id1[4]=pn::kNull;t.rates_remote_ok=true;t.rates_remote_age_ms=0xFFFFFFFFu;t.rates_remote[0]=1200;
        strcpy_s(t.rates_mirror,"ok");strcpy_s(t.rates_reason,"none");strcpy_s(t.rates_id1_refusal,"ok");t.rates_writes=3;
        const auto doc=NativeDocument(t);
        if(!check(ValidJson(doc) && doc.find("\"rates\":{\"setting\":\"mirror\",\"id0\":{\"crawl\":1200,")!=std::string::npos &&
                  doc.find("\"dash_factor\":null,\"dash_rate\":1000}")!=std::string::npos &&
                  doc.find("\"remote\":{\"valid\":true,\"age_ms\":null,\"dash\":1200,")!=std::string::npos &&
                  doc.find("\"mirror\":\"ok\",\"reason\":\"none\",\"writes\":3,")!=std::string::npos))return 1;
    }
    {
        pn::Telemetry t{};
        t.configured=true;t.mode=pn::Mode::active;t.setting=mgscoop::PartnerDefer::on;t.per_s_valid=true;
        strcpy_s(t.reason,"q\"b\\c\x01");strcpy_s(t.fault_phase,"job");strcpy_s(t.look_refused,"no_v3");strcpy_s(t.recon_mode,"track");
        t.batches=480;t.nested=1;t.foreign=2;t.unwound=3;t.drain_block=4;t.drain_in_batch=5;t.fault_code=0xC0000005u;
        t.lx=pn::kNull;t.ly=-1000;t.cam_cdeg_remote=pn::kNull;t.cam_cdeg_host=-17999;t.heading_err_mrad=pn::kNull;t.steer_mdeg=-15000;
        t.err_mm=pn::kNull;t.along_mm=pn::kNull;t.cross_mm=-1;t.dy_mm=pn::kNull;t.body_equip=pn::kNull;t.aim_err_mdeg=pn::kNull;
        t.input_age_ms=0xFFFFFFFFu;t.since_ms=0xFFFFFFFFFFFFFFFFull;t.gun3c0=0xFFFFFFFFu;
        for(unsigned i=0;i<pn::kRing+5;++i){
            pn::RingEntry e{};e.ms=i;e.mode=static_cast<std::uint8_t>(i%7);e.err_mm=(i%2)?pn::kNull:-static_cast<std::int32_t>(i);
            e.remote_mmps=-1;e.steer_mdeg=static_cast<std::int32_t>(i)*-100;e.held=0xFFFFFFFFu;pn::Push(t.ring,e);
        }
        std::size_t n=0;
        const auto doc=NativeDocument(t,&n);
        if(!check(n>0 && n<12288 && ValidJson(doc)))return 1;
        if(!check(doc.find("\"reason\":\"q\\\"b\\\\c\\u0001\"")!=std::string::npos && doc.find("\"age_ms\":null")!=std::string::npos &&
                  doc.find("\"nested\":1,\"foreign\":2,\"unwound\":3,\"drain_block\":4,\"drain_in_batch\":5,\"unwindowed_native\":0}")!=std::string::npos &&
                  doc.find("\"err_mm\":null")!=std::string::npos && doc.find(",\"off\",")!=std::string::npos))return 1;
        // Native weapons members (docs/native-puppet.md "Weapons"): nulls for unread values.
        if(!check(doc.find("\"fields_ok\":false,\"cap\":0,")!=std::string::npos && doc.find("\"fire\":{\"mode\":\"\",")!=std::string::npos &&
                  doc.find("\"mag\":{\"remote\":0,")!=std::string::npos && doc.find("\"leak\":{\"shot_count\":0,")!=std::string::npos &&
                  doc.find("\"equip\":{\"tx_rev\":0,\"tx_base\":[0,0,0]")!=std::string::npos &&
                  doc.find("\"warp\",\"mag\",\"body_yaw_cdeg\",\"face_err_cdeg\",\"r60_1\",\"pred1\",\"gmode\",\"rmode\",\"hslot\"],\"ring\":[")!=std::string::npos &&
                  doc.find("false,0,0,0,0,0,0,0,0]")!=std::string::npos))return 1;
        // Aim parity member (docs/native-puppet.md "Facing", the aim-target mirror): present in every shape.
        if(!check(doc.find("\"aim\":{\"setting\":\"\",\"override\":false,\"state\":\"\",\"reason\":\"\",\"src\":\"\",\"writes\":0,")!=std::string::npos &&
                  doc.find("\"target_mm\":[0,0,0],\"written_mm\":[0,0,0],\"slot\":\"\",\"q10_1_mm\":[0,0,0],\"q10_0_mm\":[0,0,0],\"aimpt1_mm\":[0,0,0],\"aimpt0_mm\":[0,0,0],")!=std::string::npos &&
                  doc.find("\"pred1\":0,\"pred0\":0,\"gun_state\":[0,0],\"b3d5\":[0,0],\"path1\":\"\",\"path0\":\"\"}")!=std::string::npos))return 1;
        // The partner's own reload and weapon switch (docs/native-puppet.md "Weapons"): present in every shape.
        if(!check(doc.find("\"reload\":{\"setting\":\"\",\"state\":\"\",\"last\":\"\",\"remote_valid\":false,")!=std::string::npos &&
                  doc.find("\"probe1\":{\"b20\":0,\"b24\":0,\"shots\":0,\"mode_changes\":0,\"mode\":0}}")!=std::string::npos &&
                  doc.find("\"slots\":{\"setting\":\"\",\"remote_slot\":0,")!=std::string::npos &&
                  doc.find("\"read_ok\":false,\"handles\":[null,null,null],\"plan\":[\"\",\"\",\"\"],\"ok_mask\":0,")!=std::string::npos &&
                  doc.find("\"hand_slot\":0,\"slot_src\":0}")!=std::string::npos &&
                  doc.find("\"writer\":{\"active\":false,\"dispatches\":0,\"bound\":0,\"refused\":0,\"slots\":[{\"want\":0,\"equip\":0,"
                           "\"tier\":\"\",\"family\":\"\",\"state\":\"\",\"reason\":\"\",\"handle\":0,\"want_type\":0,\"candidates\":0,"
                           "\"candidate\":0},")!=std::string::npos &&
                  doc.find("\"completed_late\":0,\"queued_taps\":0,\"reconcile\":false,\"len_ms\":0,\"len_max_ms\":0,\"remote_len_ms\":0,"
                           "\"refill_ms\":0,\"probe1\":")!=std::string::npos &&
                  doc.find("\"tx_type\":[0,0,0],\"rx_type\":[0,0,0],\"rx_version\":0,\"apply\":")!=std::string::npos &&
                  doc.find("\"setting\":\"\",\"resident_fields\":[\"index\",\"id\",\"type\",\"receiver\",\"flags\",\"refs\"],"
                           "\"resident\":null,\"preload\":{\"ids\":[],\"verdict\":\"\",\"reason\":\"\",\"new_hashes\":0}}")!=std::string::npos))return 1;
        // Native polish members (docs/native-puppet.md "Facing", "Weapons"): present in every shape.
        if(!check(doc.find("\"steer_err_mrad\":0,\"cam_diff_cdeg\":0")!=std::string::npos &&
                  doc.find("\"rings\":{\"on\":false,\"windows\":0,\"refused\":0,\"changed\":0,\"last\":\"\"}")!=std::string::npos &&
                  doc.find("\"noise\":{\"mode\":\"\",\"installed\":false,\"live\":false,\"dropped\":0,\"passed\":0}")!=std::string::npos &&
                  doc.find("\"weapon\":{\"kb_remote\":false,\"remote_gun_equip\":0,\"remote_gun_base\":0,\"mismatch\":false,\"strip\":0,\"absent\":false,\"leak_strip\":0,"
                           "\"gate\":\"\",\"body_type\":0,\"switching\":false}")!=std::string::npos &&
                  doc.find("\"trigger\":{\"requested\":0,\"shot_drains\":0,\"fire_modes\":0}")!=std::string::npos &&
                  doc.find("\"store\":{\"writes\":0,\"refused\":0,\"last\":\"\"},\"reload_seen\":0,\"frozen\":0,\"reconciles\":0,"
                           "\"m3e8_writes\":0}")!=std::string::npos &&
                  doc.find("\"facing\":{\"setting\":\"\",\"mode\":\"\",\"err_cdeg\":0,")!=std::string::npos &&
                  doc.find("\"look\":{\"src\":\"\",\"src_hits_s\":0,")!=std::string::npos))return 1;
        t.face_err_cdeg=t.face_target_cdeg=t.face_body_cdeg=t.face_rate_dps=pn::kNull;t.place_yaw_err_mdeg=pn::kNull;
        t.look_remote_cdeg[0]=t.look_remote_cdeg[1]=t.look_slot_cdeg[0]=t.look_slot_cdeg[1]=t.look_err_mdeg=pn::kNull;
        t.steer_err_mrad=t.cam_diff_cdeg=pn::kNull;t.mag_inj=t.mag_inj_cap=t.remote_gun_equip=t.remote_gun_base=pn::kNull;
        strcpy_s(t.face_setting,"idle");strcpy_s(t.face_mode,"contested");strcpy_s(t.face_reason,"lat\"ched");strcpy_s(t.look_src,"refused");
        strcpy_s(t.ring_last,"cap");strcpy_s(t.store_last,"y_vt");strcpy_s(t.noise_mode,"filter");
        t.face_writes=7;t.face_contests=3;t.look_src_hits_s=120;t.kb_remote=true;t.weapon_mismatch=true;t.strip_bits=0x50;t.weapon_absent=true;t.leak_strip=0x50;
        for(unsigned i=0;i<pn::kRing;++i){pn::RingEntry e{};e.body_yaw_cdeg=(i%2)?pn::kNull:-17999;e.face_err_cdeg=pn::kNull;pn::Push(t.ring,e);}
        const auto polish=NativeDocument(t,&n);
        if(!check(n>0 && n<12288 && ValidJson(polish)))return 1;
        if(!check(polish.find("\"facing\":{\"setting\":\"idle\",\"mode\":\"contested\",\"err_cdeg\":null,\"target_cdeg\":null,\"body_cdeg\":null,"
                              "\"rate_dps\":null,\"writes\":7,\"snaps\":0,\"contests\":3,\"turns\":0,\"reason\":\"lat\\\"ched\",\"place_yaw_err_mdeg\":null,")!=std::string::npos &&
                  polish.find("\"look\":{\"src\":\"refused\",\"src_hits_s\":120,")!=std::string::npos &&
                  polish.find("\"remote_cdeg\":[null,null],\"slot_cdeg\":[null,null],\"err_mdeg\":null}")!=std::string::npos &&
                  polish.find("\"steer_err_mrad\":null,\"cam_diff_cdeg\":null")!=std::string::npos &&
                  polish.find("\"inj\":null,\"inj_cap\":null,\"store\":{\"writes\":0,\"refused\":0,\"last\":\"y_vt\"},\"reload_seen\":0,")!=std::string::npos &&
                  polish.find("\"weapon\":{\"kb_remote\":true,\"remote_gun_equip\":null,\"remote_gun_base\":null,\"mismatch\":true,\"strip\":80,\"absent\":true,\"leak_strip\":80,")!=std::string::npos &&
                  polish.find(",-17999,null,0,0,0,0,0]")!=std::string::npos && polish.find(",null,null,0,0,0,0,0]")!=std::string::npos))return 1;
        // The aim member with every value unread (nulls) and hostile text; the ring's r60_1/pred1 unread.
        t.aim_point_err_mm=pn::kNull;t.aim_override=true;
        for(unsigned i=0;i<3;++i){t.aim_target_mm[i]=t.aim_written_mm[i]=pn::kNull;for(unsigned k=0;k<2;++k){t.aim_q10_mm[k][i]=t.aim_point_mm[k][i]=pn::kNull;}}
        strcpy_s(t.aim_slot,"na");
        for(unsigned k=0;k<2;++k){
            t.aim_yaw_cdeg[k]=t.aim_pitch_cdeg[k]=t.aim_rel_yaw_cdeg[k]=t.aim_r5c[k]=t.aim_r60[k]=t.aim_pred[k]=pn::kNull;
            t.aim_gun_state[k]=t.aim_b3d5[k]=pn::kNull;strcpy_s(t.aim_path[k],"na");
        }
        strcpy_s(t.aim_setting,"look");strcpy_s(t.aim_state,"refused");strcpy_s(t.aim_reason,"q_\"vt");strcpy_s(t.aim_src,"none");
        t.aim_writes=0xFFFFFFFFu;t.aim_overwritten=2;
        for(unsigned i=0;i<pn::kRing;++i){pn::RingEntry e{};e.r60_1=(i%2)?pn::kNull:953;e.pred1=static_cast<std::int8_t>((i%2)?-1:1);pn::Push(t.ring,e);}
        const auto aim=NativeDocument(t,&n);
        if(!check(n>0 && n<16384 && ValidJson(aim)))return 1;
        if(!check(aim.find("\"aim\":{\"setting\":\"look\",\"override\":true,\"state\":\"refused\",\"reason\":\"q_\\\"vt\",\"src\":\"none\",\"writes\":4294967295,")!=std::string::npos &&
                  aim.find("\"overwritten\":2,")!=std::string::npos &&
                  aim.find("\"target_mm\":[null,null,null],\"written_mm\":[null,null,null],\"slot\":\"na\",\"q10_1_mm\":[null,null,null],")!=std::string::npos &&
                  aim.find("\"aimpt_err_mm\":null,\"aim_yaw1_cdeg\":null,\"aim_pitch1_cdeg\":null,")!=std::string::npos &&
                  aim.find("\"r60_1\":null,")!=std::string::npos && aim.find("\"pred1\":null,\"pred0\":null,\"gun_state\":[null,null],")!=std::string::npos &&
                  aim.find("\"path1\":\"na\",\"path0\":\"na\"}")!=std::string::npos &&
                  aim.find(",953,1,0,0,0]")!=std::string::npos && aim.find(",null,null,0,0,0]")!=std::string::npos))return 1;
        // Read values.
        t.aim_r60[1]=953;t.aim_pred[1]=1;t.aim_rel_yaw_cdeg[1]=-8154;t.aim_yaw_cdeg[1]=-15400;t.aim_target_mm[0]=-12500;t.aim_point_err_mm=31;
        t.aim_written_mm[0]=-12499;strcpy_s(t.aim_slot,"ours");
        strcpy_s(t.aim_path[1],"cam");t.aim_gun_state[1]=7;
        const auto aim2=NativeDocument(t,&n);
        if(!check(ValidJson(aim2) && aim2.find("\"target_mm\":[-12500,null,null],\"written_mm\":[-12499,null,null],\"slot\":\"ours\",")!=std::string::npos &&
                  aim2.find("\"aimpt_err_mm\":31,\"aim_yaw1_cdeg\":-15400,")!=std::string::npos &&
                  aim2.find("\"rel_yaw1_cdeg\":-8154,")!=std::string::npos && aim2.find("\"r60_1\":953,")!=std::string::npos &&
                  aim2.find("\"pred1\":1,")!=std::string::npos && aim2.find("\"gun_state\":[null,7],")!=std::string::npos &&
                  aim2.find("\"path1\":\"cam\"")!=std::string::npos))return 1;
        for(unsigned k=0;k<2;++k){t.aim_r60[k]=t.aim_pred[k]=t.aim_rel_yaw_cdeg[k]=t.aim_yaw_cdeg[k]=t.aim_pitch_cdeg[k]=t.aim_r5c[k]=0;
            t.aim_gun_state[k]=t.aim_b3d5[k]=0;}
        t.aim_point_err_mm=0;
        for(unsigned i=0;i<3;++i){t.aim_target_mm[i]=t.aim_written_mm[i]=0;for(unsigned k=0;k<2;++k){t.aim_q10_mm[k][i]=t.aim_point_mm[k][i]=0;}}
        t.face_err_cdeg=t.face_target_cdeg=t.face_body_cdeg=t.face_rate_dps=0;t.place_yaw_err_mdeg=0;
        t.look_remote_cdeg[0]=t.look_remote_cdeg[1]=t.look_slot_cdeg[0]=t.look_slot_cdeg[1]=t.look_err_mdeg=0;
        t.steer_err_mrad=t.cam_diff_cdeg=0;t.mag_inj=t.mag_inj_cap=t.remote_gun_equip=t.remote_gun_base=0;
        t.gun_cap=t.gun_mag=t.gun_stock=t.gun_class=t.gun_b3d5=t.gun_slot=pn::kNull;t.remote_mag=t.remote_mag_cap=pn::kNull;
        t.shot_count=pn::kNull;t.ammo_digest=pn::kNull;t.ammo_leaks=3;t.census_noise_vt=pn::kNull;t.census_noise_fn=0x2337de8;
        strcpy_s(t.fire_mode,"visual");strcpy_s(t.fire_refused,"by\"tes");strcpy_s(t.mag_last,"no_v4");strcpy_s(t.equip_apply,"already");
        t.fire_suppressed=0xFFFFFFFFu;t.mag_written=0xFFFFu;t.equip_tx_base[1]=46;t.equip_rx_alias[1]=873;
        t.equip_tx_type[1]=4;t.equip_rx_type[0]=1;t.equip_rx_version=2;
        for(unsigned i=0;i<pn::kRing;++i){pn::RingEntry e{};e.mag=(i%2)?0xFFFF:5;pn::Push(t.ring,e);}
        const auto weapons=NativeDocument(t,&n);
        if(!check(n>0 && n<12288 && ValidJson(weapons)))return 1;
        if(!check(weapons.find("\"cap\":null,\"mag\":null,\"stock\":null,\"class\":null,\"b3d5\":null,\"slot\":null")!=std::string::npos &&
                  weapons.find("\"fire\":{\"mode\":\"visual\"")!=std::string::npos && weapons.find("\"refused\":\"by\\\"tes\"")!=std::string::npos &&
                  weapons.find("\"suppressed\":4294967295,")!=std::string::npos && weapons.find("\"noise_vt\":null,\"noise_fn\":36929000")!=std::string::npos &&
                  weapons.find("\"remote\":null,\"remote_cap\":null,\"local\":0,\"local_cap\":0")!=std::string::npos && weapons.find("\"shot_count\":null")!=std::string::npos &&
                  weapons.find("\"ammo_leaks\":3,\"ammo_vars\":null}")!=std::string::npos &&
                  weapons.find("\"tx_base\":[0,46,0]")!=std::string::npos && weapons.find("\"rx_alias\":[0,873,0]")!=std::string::npos &&
                  weapons.find("\"tx_type\":[0,4,0],\"rx_type\":[1,0,0],\"rx_version\":2,\"apply\":\"already\"")!=std::string::npos && weapons.find(",null,0,0,0,0,0,0,0]")!=std::string::npos &&
                  weapons.find(",5,0,0,0,0,0,0,0]")!=std::string::npos))return 1;
        // Reload and switch members with values, nulls and hostile text (the ring's 0xFF bytes are null).
        t.gun_mode_cur=12;t.gun_b26=1;t.gun_upper_req=pn::kNull;t.gun_m3e4=1;t.gun_m3e6=pn::kNull;t.gun_m3e8=8;t.gun_hand_slot=1;t.gun_slot_src=7;
        strcpy_s(t.reload_setting,"event");strcpy_s(t.reload_state,"entered");strcpy_s(t.reload_last,"time\"out");
        t.reload_remote_valid=t.reload_remote_reloading=t.reload_hold=true;t.reload_remote_mode=11;t.reload_remote_seq=pn::kNull;
        t.reload_starts=2;t.reload_taps=3;t.reload_entered=2;t.reload_completed=1;t.reload_fallbacks=1;t.reload_sample_strip=0x40;
        t.mag_reload_seen=4;t.mag_frozen=90;t.mag_reconciles=2;t.mag_m3e8_writes=1;t.probe1_b20=5;t.probe1_mode=11;
        t.reload_completed_late=1;t.reload_queued_taps=2;t.reload_reconcile=true;t.reload_len_ms=3020;t.reload_len_max_ms=8300;
        t.reload_remote_len_ms=2015;t.reload_refill_ms=1560;
        strcpy_s(t.slot_setting,"write");strcpy_s(t.slot_last,"empty");strcpy_s(t.weapon_gate,"switching");
        t.slot_remote=2;t.slot_remote_seq=pn::kNull;t.slot_body=1;t.slot_target=pn::kNull;t.slots_ok=true;t.slot_filled[1]=true;
        t.slot_equip[1]=46;t.slot_type[1]=4;strcpy_s(t.slot_tier[0],"empty");strcpy_s(t.slot_tier[1],"same");strcpy_s(t.slot_tier[2],"empty");
        t.slot_ok_mask=2;t.slot_refused=1;t.body_type=pn::kNull;t.body_switching=true;
        for(unsigned i=0;i<pn::kRing;++i){pn::RingEntry e{};e.gmode=(i%2)?0xFF:11;e.rmode=12;e.hslot=0xFF;pn::Push(t.ring,e);}
        const auto rs=NativeDocument(t,&n);
        if(!check(n>0 && n<16384 && ValidJson(rs)))return 1;
        if(!check(rs.find("\"mode\":12,\"b26\":1,\"upper_req\":null,\"m3e4\":1,\"m3e6\":null,\"m3e8\":8,\"hand_slot\":1,\"slot_src\":7}")!=std::string::npos &&
                  rs.find("\"reload\":{\"setting\":\"event\",\"state\":\"entered\",\"last\":\"time\\\"out\",\"remote_valid\":true,"
                          "\"remote_reloading\":true,\"remote_mode\":11,\"remote_seq\":null,\"hold\":true,\"stuck\":false,\"starts\":2,\"taps\":3,")!=std::string::npos &&
                  rs.find("\"sample_strip\":64,\"completed_late\":1,\"queued_taps\":2,\"reconcile\":true,\"len_ms\":3020,\"len_max_ms\":8300,"
                          "\"remote_len_ms\":2015,\"refill_ms\":1560,\"probe1\":{\"b20\":5,\"b24\":0,\"shots\":0,\"mode_changes\":0,"
                          "\"mode\":11}}")!=std::string::npos &&
                  rs.find("\"reload_seen\":4,\"frozen\":90,\"reconciles\":2,\"m3e8_writes\":1}")!=std::string::npos &&
                  rs.find("\"slots\":{\"setting\":\"write\",\"remote_slot\":2,\"remote_seq\":null,\"body_slot\":1,\"hand_slot\":1,\"target\":null,"
                          "\"read_ok\":true,\"handles\":[null,{\"equip\":46,\"type\":4},null],\"plan\":[\"empty\",\"same\",\"empty\"],\"ok_mask\":2,"
                          "\"writes\":0,\"refused\":1,\"last\":\"empty\",\"faulted\":false,\"switching\":true,\"writer\":{")!=std::string::npos &&
                  rs.find("\"gate\":\"switching\",\"body_type\":null,\"switching\":true}")!=std::string::npos &&
                  rs.find(",11,12,null]")!=std::string::npos && rs.find(",null,12,null]")!=std::string::npos))return 1;
        // The generalized writer, the census (all 48 entries) and the preload, with hostile text.
        strcpy_s(t.equip_setting,"substitute");t.writer_active=true;t.writer_dispatches=3;t.writer_bound=2;t.writer_refused=1;
        for(unsigned i=0;i<3;++i){
            t.writer_want[i]=static_cast<std::uint16_t>(i==1?46:343);t.writer_equip[i]=static_cast<std::uint16_t>(i==1?46:27);
            strcpy_s(t.writer_tier[i],i==1?"a1":"c");strcpy_s(t.writer_family[i],i==1?"optional":"plain");
            strcpy_s(t.writer_state[i],i==2?"refused":"bound");strcpy_s(t.writer_reason[i],i==2?"remote_slot_not\"empty":"ok");
            t.writer_handle[i]=0x60000u|(i==1?(46u<<5)|4u:(27u<<5)|1u);
            t.writer_want_type[i]=static_cast<std::uint8_t>(i==1?4:1);t.writer_candidates[i]=i==1?0u:3u;
            t.writer_candidate[i]=static_cast<std::uint16_t>(i==1?0:27);
        }
        t.resident_ok=true;t.resident_count=48;
        for(unsigned i=0;i<48;++i) t.resident[i]={static_cast<std::uint16_t>(500+i),4,157,3,static_cast<std::uint8_t>(i),0xFFFFFFFFu};
        t.preload_count=2;t.preload_ids[0]=343;t.preload_ids[1]=9;strcpy_s(t.preload_verdict,"queued_verified");strcpy_s(t.preload_reason,"o\"k");
        t.preload_new_hashes=5;
        const auto ws=NativeDocument(t,&n);
        if(!check(n>0 && n<16384 && ValidJson(ws)))return 1;
        if(!check(ws.find("\"writer\":{\"active\":true,\"dispatches\":3,\"bound\":2,\"refused\":1,\"slots\":[{\"want\":343,\"equip\":27,"
                          "\"tier\":\"c\",\"family\":\"plain\",\"state\":\"bound\",\"reason\":\"ok\",\"handle\":394081,\"want_type\":1,"
                          "\"candidates\":3,\"candidate\":27},")!=std::string::npos &&
                  ws.find("\"reason\":\"remote_slot_not\\\"empty\"")!=std::string::npos &&
                  ws.find("\"setting\":\"substitute\",\"resident_fields\":")!=std::string::npos &&
                  ws.find("\"resident\":[[0,500,4,157,3,4294967295],[1,501,")!=std::string::npos &&
                  ws.find(",[47,547,4,157,3,4294967295]],\"preload\":{\"ids\":[343,9],\"verdict\":\"queued_verified\",\"reason\":\"o\\\"k\","
                          "\"new_hashes\":5}}")!=std::string::npos))return 1;
        // Too small a buffer: nothing (never a truncated member).
        char small[64];
        if(!check(mgscoop::worker_debug::FormatNativeJson(t,small,sizeof(small))==0 && small[0]==0))return 1;
    }
    // Online play member native.net (docs/native-puppet.md "Online"): present and valid by default,
    // and complete (never null) with every string at capacity and every counter at its maximum.
    {
        pn::Telemetry t{};
        std::size_t n=0;
        const auto doc=NativeDocument(t,&n);
        if(!check(n>0 && ValidJson(doc) && doc.find(",\"net\":{\"policy\":\"\",\"stall\":\"\",\"stall_ms\":0,")!=std::string::npos &&
                  doc.find("\"caps\":{\"rx\":false,\"sent\":0,")!=std::string::npos && doc.find("\"net\":null")==std::string::npos))return 1;
        auto& net=t.net;
        const auto fill=[](char* text,std::size_t capacity,char c){for(std::size_t i=0;i+1<capacity;++i)text[i]=c;text[capacity-1]=0;};
        fill(net.policy,sizeof(net.policy),'p');fill(net.stall,sizeof(net.stall),'s');fill(net.hold,sizeof(net.hold),'h');
        fill(net.last_net_reason,sizeof(net.last_net_reason),'r');fill(net.local_reason,sizeof(net.local_reason),'l');
        fill(net.remote_reason,sizeof(net.remote_reason),'q');fill(net.remote_profile,sizeof(net.remote_profile),'f');
        fill(net.remote_policy,sizeof(net.remote_policy),'o');
        net.stall_ms=net.edge_repeats=net.quality_good_ms=net.gaps_100=net.gaps_250=net.gaps_1000=net.max_gap_ms=0xFFFFFFFFu;
        net.ia_p50_ms=net.ia_p95_ms=net.net_leaves=net.hard_leaves=net.net_reentries=net.quiet_episodes=net.gap_snaps=0xFFFFFFFFu;
        net.caps_sent=net.caps_received=net.caps_invalid=0xFFFFFFFFu;net.quality_good=net.remote_quiet=net.flap=net.caps_rx=true;
        net.remote_v4=net.remote_caps_refused=true;net.local_drive=mgscoop::partner_native_net::Drive::native;
        net.remote_drive=mgscoop::partner_native_net::Drive::clips;
        const auto full=NativeDocument(t,&n);
        if(!check(n>0 && ValidJson(full) && full.find("\"net\":null")==std::string::npos &&
                  full.find("\"local_drive\":\"native\",\"local_reason\":\"lllllllllllllllllllllll\"")!=std::string::npos &&
                  full.find("\"remote_v4\":true,\"refused\":true}}")!=std::string::npos))return 1;
    }
    // Partner name tag member (docs/partner-nametag.md): valid JSON in every shape
    // (defaults, Latin-1 and hostile names, nulls for non-finite values).
    namespace tag=mgscoop::nametag;
    {
        static char buffer[8192];
        const auto document=[&](const tag::Telemetry& t,const tag::RenderStats& r,std::size_t* length=nullptr){
            const std::size_t n=tag::FormatJson(t,r,buffer,sizeof(buffer));
            if(length)*length=n;
            return std::string("{\"schema\":2")+std::string(buffer,n)+"}";
        };
        tag::Telemetry t{};tag::RenderStats r{};std::size_t n=0;
        auto doc=document(t,r,&n);
        if(!check(n>0 && ValidJson(doc) && doc.find(",\"nametag\":{\"on\":false,\"crosshair\":\"aim\",\"state\":\"hidden\",\"reason\":\"ok\",\"name\":\"\",")!=std::string::npos &&
                  doc.find("\"camera\":{\"ok\":false,\"refusal\":\"none\",\"vp_vt\":\"0x0\",")!=std::string::npos &&
                  doc.find("\"render\":{\"frames\":0,")!=std::string::npos))return 1;
        t.on=t.visible=t.body_ok=t.projected=t.hover=t.aim=t.crosshair_on=true;t.crosshair=tag::Crosshair::always;t.reason=tag::Reason::too_far;
        strcpy_s(t.name,"Jos\xC3\xA9 \"q\\");strcpy_s(t.local_name,"Host\x01Tag");t.name_src=tag::NameSource::remote;t.local_src=tag::LocalSource::steam;
        t.name_tx=0xFFFFFFFFu;t.dist_m=NAN;t.w=INFINITY;t.ndc[0]=-0.5f;t.miss_yaw_deg=-11.25f;t.aim_src=tag::AimSource::gun;t.hud=3;
        t.seq=0xFFFFFFFFFFFFFFFFull;t.camera.ok=true;t.camera.refusal=tag::Refusal::torn;t.camera.vp_vt=0x20f1c08;t.camera.cam_vt=0x20f08c8;
        t.camera.px[0]=1920;t.camera.px[1]=1080;t.camera.rect[2]=t.camera.rect[3]=1.0f;t.camera.vfov_deg=50.2f;t.camera.far_m=-NAN;
        r.frames=r.drawn=7;r.crosshair=true;r.label_box[2]=1000.5f;r.ring[2]=10.0f;r.bb[0]=1920;r.display[1]=1080;r.alpha=1.0f;r.age_ms=0xFFFFFFFFu;
        doc=document(t,r,&n);
        if(!check(n>0 && n<4096 && ValidJson(doc)))return 1;
        if(!check(doc.find("\"name\":\"Jos\\u00e9 \\\"q\\\\\"")!=std::string::npos && doc.find("\"local_name\":\"Host\\u0001Tag\"")!=std::string::npos &&
                  doc.find("\"reason\":\"far\"")!=std::string::npos && doc.find("\"dist_m\":null")!=std::string::npos &&
                  doc.find("\"w\":null")!=std::string::npos && doc.find("\"far\":null")!=std::string::npos &&
                  doc.find("\"vp_vt\":\"0x20f1c08\",\"cam_vt\":\"0x20f08c8\",\"vp_px\":[1920,1080]")!=std::string::npos &&
                  doc.find("\"refusal\":\"torn\"")!=std::string::npos && doc.find("\"aim_src\":\"gun\",\"crosshair_on\":true,\"hud\":3,")!=std::string::npos &&
                  doc.find("\"snapshot_seq\":18446744073709551615")!=std::string::npos && doc.find("\"age_ms\":4294967295}}")!=std::string::npos))return 1;
        // Invalid UTF-8 in a name (never produced by the sanitizer) still yields valid JSON.
        strcpy_s(t.name,"a\xC3(\xFF\xE6\x97\xA5");
        doc=document(t,r,&n);
        if(!check(n>0 && ValidJson(doc)))return 1;
        char small[64];
        if(!check(tag::FormatJson(t,r,small,sizeof(small))==0 && small[0]==0))return 1;
    }
    // The Steam self-test member (docs/steam-online.md 14).
    {
        mgscoop::steam_state::SelftestReport r{};
        static char buffer[4096];
        std::size_t n = mgscoop::worker_debug::FormatSteamJson(r, buffer, sizeof(buffer));
        const std::string off = std::string("{\"schema\":2") + std::string(buffer, n) + "}";
        if(!check(n>0 && ValidJson(off) && off.find("\"steam\":{\"selftest\":\"off\"")!=std::string::npos &&
                  off.find("\"pair\":null,\"lpair\":null")!=std::string::npos))return 1;
        r.configured=true;std::snprintf(r.verdict,sizeof(r.verdict),"pass");std::snprintf(r.stage,sizeof(r.stage),"done");
        r.api=r.client=r.logged_on=r.messages=r.sockets=r.utils=r.rtti=r.shape=true;r.relay=100;
        r.pair=mgscoop::steam_state::PairResult{true,4,500,500,64,64,1,564,564,0};
        std::snprintf(r.reason,sizeof(r.reason),"q\"x");
        n = mgscoop::worker_debug::FormatSteamJson(r, buffer, sizeof(buffer));
        const std::string pass = std::string("{\"schema\":2") + std::string(buffer, n) + "}";
        if(!check(ValidJson(pass) && pass.find("\"selftest\":\"pass\",\"stage\":\"done\",\"api\":1,\"client\":1,\"logged_on\":1,"
                  "\"messages\":1,\"sockets\":1,\"utils\":1,\"rtti\":1,\"shape\":1,\"relay\":100,\"pair\":{\"hs_ms\":4,\"frames\":500,"
                  "\"frames_rx\":500,\"rel\":64,\"rel_rx\":64,\"rtt_ms\":1,\"release\":564,\"leak\":0")!=std::string::npos &&
                  pass.find("\"lpair\":null,\"inert\":{\"recv\":0,\"state\":0,\"accept\":0},\"trip\":{\"getter_delta\":0}")!=std::string::npos &&
                  pass.find("\"reason\":\"q\\\"x\",\"fn\":null}")!=std::string::npos))return 1;
        char small[32];
        if(!check(mgscoop::worker_debug::FormatSteamJson(r,small,sizeof(small))==0 && small[0]==0))return 1;
        // Stage F/D (docs/steam-online.md 20.6).
        r.fn.ran=true;r.fn.set204=r.fn.reset204=r.fn.conn_ok=r.fn.thread_ok=r.fn.d_install=r.fn.d_uninstall=true;
        r.fn.status_cb=2;r.fn.old_state=3;r.fn.new_state=4;r.fn.d_pumps=190;r.fn.extra=1;
        n = mgscoop::worker_debug::FormatSteamJson(r, buffer, sizeof(buffer));
        const std::string fn = std::string("{\"schema\":2") + std::string(buffer, n) + "}";
        if(!check(ValidJson(fn) && fn.find("\"fn\":{\"verdict\":\"ok\",\"ran\":1,\"skipped\":0,\"gate\":\"\",\"set204\":1,\"reset204\":1,"
                  "\"pre_pump\":0,\"status_cb\":2,\"conn_ok\":1,\"old\":3,\"new\":4,\"thread_ok\":1,\"extra\":1,\"d_install\":1,"
                  "\"d_pumps\":190,\"d_dispatched\":0,\"d_uninstall\":1,\"faults\":0,\"reason\":\"\"}")!=std::string::npos))return 1;
        r.fn=mgscoop::steam_state::FnResult{};r.fn.skipped=true;
        n = mgscoop::worker_debug::FormatSteamJson(r, buffer, sizeof(buffer));
        if(!check(ValidJson(std::string("{\"schema\":2") + std::string(buffer, n) + "}") &&
                  std::string(buffer,n).find("\"fn\":{\"verdict\":\"skip\"")!=std::string::npos))return 1;
    }
    // The in-game status line member (coop_hud).
    {
        namespace hud=mgscoop::coop_hud;
        hud::ResetForTest();
        hud::Configure(hud::Mode::compact,true);
        hud::Line line{};std::snprintf(line.text,sizeof(line.text),"CO-OP [connected] \"Snake\" - Steam relay 61 ms");
        hud::NoteFrame(&line,"Connected to \\Snake",false,1,1,1234);
        char out[512]{};
        const std::size_t n=hud::FormatJson(hud::GetTelemetry(),out,sizeof(out));
        const std::string doc=std::string("{\"schema\":2")+std::string(out,n)+"}";
        if(!check(n>0 && ValidJson(doc) && doc.find(",\"hud\":{\"enabled\":true,\"mode\":\"compact\",\"frames\":1,\"hidden\":0,")!=std::string::npos))return 1;
        char small[16];
        if(!check(hud::FormatJson(hud::GetTelemetry(),small,sizeof(small))==0 && small[0]==0))return 1;
        hud::ResetForTest();
    }
    // The checker itself rejects the classic writer slips.
    for(const char* bad:{"{\"a\":1,}","{\"a\":1","{\"a\":[1,2}","{\"a\":01}","{\"a\":\"x\ny\"}","{\"a\":1}}","{\"a\":nul}",
                         "{\"a\":1,\"a\":2}","{\"o\":{\"m\":1,\"b\":2,\"m\":3}}"})
        if(!check(!ValidJson(bad)))return 1;
    if(!check(ValidJson("{\"a\":[1,-2.5e3,true,null,{\"b\":\"\\u00e9\"}]}")))return 1;
    // The same member name in different objects is fine (gun.mag, native.mag, drain.mag).
    if(!check(ValidJson("{\"mag\":{\"mag\":1},\"o\":{\"mag\":[{\"mag\":2},{\"mag\":3}]}}")))return 1;
    std::printf("worker debug policy %u PASS; gameplay command validation remains the existing debug queue; the state.json native and nametag members are valid JSON\n",checks);return 0;
}
