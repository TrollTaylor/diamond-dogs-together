#include "mgscoop/worker_debug.hpp"
#include "mgscoop/debug_menu.hpp"
#include "mgscoop/partner_native.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
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
            for (;;) {
                Ws(); if (!String()) return false; Ws();
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
                  doc.find("\"per_s\":null")!=std::string::npos && doc.find("\"ring\":[]")!=std::string::npos))return 1;
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
        if(!check(n>0 && n<8192 && ValidJson(doc)))return 1;
        if(!check(doc.find("\"reason\":\"q\\\"b\\\\c\\u0001\"")!=std::string::npos && doc.find("\"age_ms\":null")!=std::string::npos &&
                  doc.find("\"nested\":1,\"foreign\":2,\"unwound\":3,\"drain_block\":4,\"drain_in_batch\":5}")!=std::string::npos &&
                  doc.find("\"err_mm\":null")!=std::string::npos && doc.find(",\"off\",")!=std::string::npos))return 1;
        // Too small a buffer: nothing (never a truncated member).
        char small[64];
        if(!check(mgscoop::worker_debug::FormatNativeJson(t,small,sizeof(small))==0 && small[0]==0))return 1;
    }
    // The checker itself rejects the classic writer slips.
    for(const char* bad:{"{\"a\":1,}","{\"a\":1","{\"a\":[1,2}","{\"a\":01}","{\"a\":\"x\ny\"}","{\"a\":1}}","{\"a\":nul}"})
        if(!check(!ValidJson(bad)))return 1;
    if(!check(ValidJson("{\"a\":[1,-2.5e3,true,null,{\"b\":\"\\u00e9\"}]}")))return 1;
    std::printf("worker debug policy %u PASS; gameplay command validation remains the existing debug queue; the state.json native member is valid JSON\n",checks);return 0;
}
