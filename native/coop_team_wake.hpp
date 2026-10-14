#pragma once
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace mgscoop::team_wake {
inline constexpr char v1[]="return 'MGS_COOP_TEAM_V1' -- ";
inline constexpr char v2[]="return 'MGS_COOP_TEAM_V2' -- ";
static_assert(sizeof(v1)==sizeof(v2));
struct Receipt {
    std::uint32_t mission{},epoch{},peerGeneration{},grantedGeneration{};
    bool localDown{},peerDown{},peerFresh{},version2{};
    std::uint8_t localCause{},peerCause{};
};
inline bool Supported(std::uint32_t mission) noexcept {return mission==30010||mission==30020||mission==30050;}
inline bool Valid(const Receipt& r) noexcept {
    return Supported(r.mission)&&r.epoch&&r.epoch<=65535&&r.localCause<=2&&r.peerCause<=2&&
        r.localDown==(r.localCause!=0)&&r.peerDown==(r.peerCause!=0)&&
        (r.peerFresh||(!r.peerDown&&!r.peerCause&&!r.peerGeneration&&!r.grantedGeneration))&&
        (!r.version2||!r.peerDown||r.peerGeneration>0)&&
        (r.version2||(!r.peerGeneration&&!r.grantedGeneration))&&
        (!r.grantedGeneration||(r.peerFresh&&r.peerDown&&r.peerGeneration));
}
inline bool Recognized(const char* text,std::size_t size) noexcept {
    constexpr auto n=sizeof(v1)-1;
    return text&&size>=n&&(!std::memcmp(text,v1,n)||!std::memcmp(text,v2,n));
}
inline bool Parse(const char* text,std::size_t size,Receipt& out) noexcept {
    out={};constexpr auto n=sizeof(v1)-1;
    if(!Recognized(text,size)||size<=n||size>n+128) return false;
    const bool second=!std::memcmp(text,v2,n);const unsigned count=second?9u:7u;
    const char* p=text+n;const char* end=text+size;std::uint32_t values[9]{};
    for(unsigned i=0;i<count;++i){
        const auto r=std::from_chars(p,end,values[i]);
        if(r.ec!=std::errc()||r.ptr==p)return false;
        p=r.ptr;if(i+1!=count){if(p==end||*p!=' ')return false;++p;}
    }
    if(p!=end||values[2]>1||values[3]>2||values[4]>1||values[5]>2||values[6]>1)return false;
    Receipt r{};r.mission=values[0];r.epoch=values[1];r.localDown=values[2]!=0;r.localCause=static_cast<std::uint8_t>(values[3]);
    r.peerDown=values[4]!=0;r.peerCause=static_cast<std::uint8_t>(values[5]);r.peerFresh=values[6]!=0;r.version2=second;
    r.peerGeneration=values[7];r.grantedGeneration=values[8];if(!Valid(r))return false;out=r;return true;
}
struct Context {
    std::uint32_t mission{},epoch{};std::uint64_t now{},observedAt{};
    bool admitted{},ownerLive{},terminating{};
};
struct Decision {bool known{},peerDown{},peerWake{},peerAsleep{},peerCarryable{},putDown{};};
inline Decision Evaluate(const Receipt& r,const Context& c) noexcept {
    Decision d{};
    if(!Valid(r)||!c.admitted||!c.ownerLive||c.terminating||r.mission!=c.mission||r.epoch!=c.epoch||
        !r.peerFresh||c.now<c.observedAt||c.now-c.observedAt>500)return d;
    d.known=true;d.peerDown=r.peerDown;
    d.peerWake=r.version2&&r.peerDown&&r.peerGeneration>0&&r.grantedGeneration==r.peerGeneration;
    d.peerAsleep=r.peerDown&&r.peerCause==2&&!d.peerWake;
    d.peerCarryable=r.peerDown&&!d.peerWake;d.putDown=d.peerWake||!r.peerDown;
    return d;
}
} // namespace mgscoop::team_wake
