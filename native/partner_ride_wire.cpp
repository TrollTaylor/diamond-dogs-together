#include "mgscoop/partner_ride_wire.hpp"
#include <cmath>
namespace mgscoop::partner_ride_wire {
bool IsRide(const std::uint8_t* b, std::size_t n) noexcept { return b && n && b[0]==kMagic; }
bool Encode(const Packet& p, std::uint8_t (&b)[kSize]) noexcept {
    if (static_cast<unsigned>(p.kind)>5 || !p.revision) return false;
    b[0]=kMagic; b[1]=kVersion; b[2]=static_cast<std::uint8_t>(p.kind); b[3]=0;
    b[4]=static_cast<std::uint8_t>(p.revision); b[5]=static_cast<std::uint8_t>(p.revision>>8); b[6]=b[7]=0;
    for(unsigned i=0;i<4;++i) b[8+i]=static_cast<std::uint8_t>(p.sender_ms>>(i*8));
    return true;
}
bool Decode(const std::uint8_t* b, std::size_t n, Packet& out) noexcept {
    if(!b || n!=kSize || b[0]!=kMagic || b[1]!=kVersion || b[2]>5 || b[3] || b[6] || b[7]) return false;
    Packet p{}; p.kind=static_cast<Kind>(b[2]); p.revision=static_cast<std::uint16_t>(b[4]|(b[5]<<8));
    if(!p.revision) return false;
    for(unsigned i=0;i<4;++i) p.sender_ms|=static_cast<std::uint32_t>(b[8+i])<<(i*8);
    out=p; return true;
}
bool Accept(Store& s, const std::uint8_t* b, std::size_t n, std::uint32_t epoch, std::uint64_t now) noexcept {
    Packet p{}; if(!epoch || !Decode(b,n,p)) { ++s.invalid; return false; }
    if(s.have && s.epoch==epoch) {
        const auto rev=static_cast<std::uint16_t>(p.revision-s.packet.revision);
        const auto ms=static_cast<std::uint32_t>(p.sender_ms-s.packet.sender_ms);
        if(rev>=0x8000 || (!rev && p.kind!=s.packet.kind) || ms>=0x80000000u) { ++s.stale; return false; }
        if(!rev && !ms) { ++s.duplicates; return false; }
    }
    s.have=true; s.packet=p; s.epoch=epoch; s.received_ms=now; ++s.received; return true;
}
bool Live(const Store& s, std::uint32_t epoch, std::uint64_t now) noexcept {
    return s.have && epoch && s.epoch==epoch && now>=s.received_ms && now-s.received_ms<=kLeaseMs;
}
void Placed(Visibility& s, std::uint64_t now) noexcept { s.place_ms=now; s.retry_ms=now; }
void Observe(Visibility& s, const In& in) noexcept {
    // Ring identity is stronger evidence than clocks/positions: a real reset
    // may preserve both. Legacy/synthetic generation0 keeps prior behavior.
    if(in.generation) {
        if(s.generation && s.generation!=in.generation) s.have_sample=s.stable=false;
        s.generation=in.generation;
    }
    const bool heli=in.anim_valid && in.remote_act>=kHeliActFirst && in.remote_act<=kHeliActLast;
    const bool riding=heli || (in.ride_live && in.kind!=Kind::foot);
    const bool stale=in.ride_have && !in.ride_live;
    const bool foot=!heli && (!in.ride_have || (in.ride_live && in.kind==Kind::foot));
    if(s.air && in.body_anim_valid && in.body_act!=25) s.air=false;
    if(in.body_anim_valid && in.body_act==40) {
        if(s.dead_retry && in.rearmed) s.dead_locked=true;
        if(!s.dead_retry || s.dead_locked) { s.hidden=true; s.reason="dead"; }
    }
    if(in.body_anim_valid && in.body_valid && in.anim_valid && in.body_act==25 && in.remote_act!=25 && in.remote_act!=26 &&
       s.place_ms && in.now_ms>=s.place_ms && in.now_ms-s.place_ms<=1500 && std::fabs(in.y-in.body_y)>=2.0) s.air=true;
    if(riding || stale || s.air || s.dead_locked || !in.lease_live) {
        s.hidden=true; s.reason=s.dead_locked ? "dead" : s.air ? "air" : riding ? "ride" : "ride_stale";
    }
    const bool valid=in.fresh && in.lease_live && in.time_valid && !in.jump && foot && in.anim_valid &&
                     !s.air && !s.dead_locked && std::isfinite(in.x) && std::isfinite(in.y) && std::isfinite(in.z);
    if(!valid || (s.have_sample && (in.sender_ms<s.sender_ms || in.sender_ms-s.sender_ms>1000))) {
        s.have_sample=s.stable=false;
        if(s.hidden && !s.reason) s.reason="ride_stale";
        return;
    }
    if(!s.have_sample) { s.have_sample=true; s.sender_ms=in.sender_ms; s.x=in.x; s.y=in.y; s.z=in.z; s.stable=false; return; }
    if(in.sender_ms==s.sender_ms) return;
    const double dt=static_cast<double>(in.sender_ms-s.sender_ms)*0.001;
    const double dx=in.x-s.x, dz=in.z-s.z;
    const bool calm=std::hypot(dx,dz)/dt<8.0 && std::fabs(in.y-s.y)/dt<0.5;
    if(!calm) s.stable=false;
    else if(!s.stable) { s.stable=true; s.stable_since=s.sender_ms; }
    s.sender_ms=in.sender_ms; s.x=in.x; s.y=in.y; s.z=in.z;
    if(s.hidden && s.stable && in.sender_ms-s.stable_since>=500) {
        s.hidden=false; s.reason=nullptr;
        if(in.body_anim_valid && in.body_act==40) s.dead_retry=true;
    }
}
}
