#include "mgscoop/worker_debug.hpp"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/nametag_core.hpp"
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
                  doc.find("\"warp\",\"mag\",\"body_yaw_cdeg\",\"face_err_cdeg\"],\"ring\":[")!=std::string::npos &&
                  doc.find("false,0,0,0]")!=std::string::npos))return 1;
        // Native polish members (docs/native-puppet.md "Facing", "Weapons"): present in every shape.
        if(!check(doc.find("\"steer_err_mrad\":0,\"cam_diff_cdeg\":0")!=std::string::npos &&
                  doc.find("\"rings\":{\"on\":false,\"windows\":0,\"refused\":0,\"changed\":0,\"last\":\"\"}")!=std::string::npos &&
                  doc.find("\"noise\":{\"mode\":\"\",\"installed\":false,\"live\":false,\"dropped\":0,\"passed\":0}")!=std::string::npos &&
                  doc.find("\"weapon\":{\"kb_remote\":false,\"remote_gun_equip\":0,\"remote_gun_base\":0,\"mismatch\":false,\"strip\":0}")!=std::string::npos &&
                  doc.find("\"trigger\":{\"requested\":0,\"shot_drains\":0,\"fire_modes\":0}")!=std::string::npos &&
                  doc.find("\"store\":{\"writes\":0,\"refused\":0,\"last\":\"\"}}")!=std::string::npos &&
                  doc.find("\"facing\":{\"setting\":\"\",\"mode\":\"\",\"err_cdeg\":0,")!=std::string::npos &&
                  doc.find("\"look\":{\"src\":\"\",\"src_hits_s\":0,")!=std::string::npos))return 1;
        t.face_err_cdeg=t.face_target_cdeg=t.face_body_cdeg=t.face_rate_dps=pn::kNull;t.place_yaw_err_mdeg=pn::kNull;
        t.look_remote_cdeg[0]=t.look_remote_cdeg[1]=t.look_slot_cdeg[0]=t.look_slot_cdeg[1]=t.look_err_mdeg=pn::kNull;
        t.steer_err_mrad=t.cam_diff_cdeg=pn::kNull;t.mag_inj=t.mag_inj_cap=t.remote_gun_equip=t.remote_gun_base=pn::kNull;
        strcpy_s(t.face_setting,"idle");strcpy_s(t.face_mode,"contested");strcpy_s(t.face_reason,"lat\"ched");strcpy_s(t.look_src,"refused");
        strcpy_s(t.ring_last,"cap");strcpy_s(t.store_last,"y_vt");strcpy_s(t.noise_mode,"filter");
        t.face_writes=7;t.face_contests=3;t.look_src_hits_s=120;t.kb_remote=true;t.weapon_mismatch=true;t.strip_bits=0x50;
        for(unsigned i=0;i<pn::kRing;++i){pn::RingEntry e{};e.body_yaw_cdeg=(i%2)?pn::kNull:-17999;e.face_err_cdeg=pn::kNull;pn::Push(t.ring,e);}
        const auto polish=NativeDocument(t,&n);
        if(!check(n>0 && n<12288 && ValidJson(polish)))return 1;
        if(!check(polish.find("\"facing\":{\"setting\":\"idle\",\"mode\":\"contested\",\"err_cdeg\":null,\"target_cdeg\":null,\"body_cdeg\":null,"
                              "\"rate_dps\":null,\"writes\":7,\"snaps\":0,\"contests\":3,\"turns\":0,\"reason\":\"lat\\\"ched\",\"place_yaw_err_mdeg\":null,")!=std::string::npos &&
                  polish.find("\"look\":{\"src\":\"refused\",\"src_hits_s\":120,")!=std::string::npos &&
                  polish.find("\"remote_cdeg\":[null,null],\"slot_cdeg\":[null,null],\"err_mdeg\":null}")!=std::string::npos &&
                  polish.find("\"steer_err_mrad\":null,\"cam_diff_cdeg\":null")!=std::string::npos &&
                  polish.find("\"inj\":null,\"inj_cap\":null,\"store\":{\"writes\":0,\"refused\":0,\"last\":\"y_vt\"}}")!=std::string::npos &&
                  polish.find("\"weapon\":{\"kb_remote\":true,\"remote_gun_equip\":null,\"remote_gun_base\":null,\"mismatch\":true,\"strip\":80}")!=std::string::npos &&
                  polish.find(",-17999,null]")!=std::string::npos && polish.find(",null,null]")!=std::string::npos))return 1;
        t.face_err_cdeg=t.face_target_cdeg=t.face_body_cdeg=t.face_rate_dps=0;t.place_yaw_err_mdeg=0;
        t.look_remote_cdeg[0]=t.look_remote_cdeg[1]=t.look_slot_cdeg[0]=t.look_slot_cdeg[1]=t.look_err_mdeg=0;
        t.steer_err_mrad=t.cam_diff_cdeg=0;t.mag_inj=t.mag_inj_cap=t.remote_gun_equip=t.remote_gun_base=0;
        t.gun_cap=t.gun_mag=t.gun_stock=t.gun_class=t.gun_b3d5=t.gun_slot=pn::kNull;t.remote_mag=t.remote_mag_cap=pn::kNull;
        t.shot_count=pn::kNull;t.ammo_digest=pn::kNull;t.ammo_leaks=3;t.census_noise_vt=pn::kNull;t.census_noise_fn=0x2337de8;
        strcpy_s(t.fire_mode,"visual");strcpy_s(t.fire_refused,"by\"tes");strcpy_s(t.mag_last,"no_v4");strcpy_s(t.equip_apply,"already");
        t.fire_suppressed=0xFFFFFFFFu;t.mag_written=0xFFFFu;t.equip_tx_base[1]=46;t.equip_rx_alias[1]=873;
        for(unsigned i=0;i<pn::kRing;++i){pn::RingEntry e{};e.mag=(i%2)?0xFFFF:5;pn::Push(t.ring,e);}
        const auto weapons=NativeDocument(t,&n);
        if(!check(n>0 && n<12288 && ValidJson(weapons)))return 1;
        if(!check(weapons.find("\"cap\":null,\"mag\":null,\"stock\":null,\"class\":null,\"b3d5\":null,\"slot\":null")!=std::string::npos &&
                  weapons.find("\"fire\":{\"mode\":\"visual\"")!=std::string::npos && weapons.find("\"refused\":\"by\\\"tes\"")!=std::string::npos &&
                  weapons.find("\"suppressed\":4294967295,")!=std::string::npos && weapons.find("\"noise_vt\":null,\"noise_fn\":36929000")!=std::string::npos &&
                  weapons.find("\"remote\":null,\"remote_cap\":null,\"local\":0,\"local_cap\":0")!=std::string::npos && weapons.find("\"shot_count\":null")!=std::string::npos &&
                  weapons.find("\"ammo_leaks\":3,\"ammo_vars\":null}")!=std::string::npos &&
                  weapons.find("\"tx_base\":[0,46,0]")!=std::string::npos && weapons.find("\"rx_alias\":[0,873,0]")!=std::string::npos &&
                  weapons.find("\"apply\":\"already\"")!=std::string::npos && weapons.find(",null,0,0]")!=std::string::npos &&
                  weapons.find(",5,0,0]")!=std::string::npos))return 1;
        // Too small a buffer: nothing (never a truncated member).
        char small[64];
        if(!check(mgscoop::worker_debug::FormatNativeJson(t,small,sizeof(small))==0 && small[0]==0))return 1;
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
                  pass.find("\"reason\":\"q\\\"x\"}")!=std::string::npos))return 1;
        char small[32];
        if(!check(mgscoop::worker_debug::FormatSteamJson(r,small,sizeof(small))==0 && small[0]==0))return 1;
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
