#include "mgscoop/partner_ride_wire.hpp"
#include <cmath>
namespace mgscoop::partner_ride_wire {
bool IsRide(const std::uint8_t* b, std::size_t n) noexcept { return b && n && b[0]==kMagic; }
std::uint8_t VersionFor(const Packet& p) noexcept {
    return p.kind==Kind::vehicle && (p.seat<=kSeatMax || p.vehicle!=kVehicleUnknown) ? kVersion : kVersion1;
}
bool Encode(const Packet& p, std::uint8_t (&b)[kSize]) noexcept {
    if (static_cast<unsigned>(p.kind)>5 || !p.revision) return false;
    const std::uint8_t version=VersionFor(p);
    if (version==kVersion && p.seat>kSeatMax && p.seat!=kSeatUnknown) return false;
    b[0]=kMagic; b[1]=version; b[2]=static_cast<std::uint8_t>(p.kind);
    b[3]=version==kVersion ? p.seat : 0;
    b[4]=static_cast<std::uint8_t>(p.revision); b[5]=static_cast<std::uint8_t>(p.revision>>8);
    const std::uint16_t vehicle=version==kVersion ? p.vehicle : 0;
    b[6]=static_cast<std::uint8_t>(vehicle); b[7]=static_cast<std::uint8_t>(vehicle>>8);
    for(unsigned i=0;i<4;++i) b[8+i]=static_cast<std::uint8_t>(p.sender_ms>>(i*8));
    return true;
}
bool Decode(const std::uint8_t* b, std::size_t n, Packet& out) noexcept {
    if(!b || n!=kSize || b[0]!=kMagic || b[2]>5) return false;
    Packet p{}; p.kind=static_cast<Kind>(b[2]); p.revision=static_cast<std::uint16_t>(b[4]|(b[5]<<8));
    if(b[1]==kVersion1) {
        if(b[3] || b[6] || b[7]) return false;
    } else if(b[1]==kVersion) {
        const std::uint16_t vehicle=static_cast<std::uint16_t>(b[6]|(b[7]<<8));
        if(b[3]>kSeatMax && b[3]!=kSeatUnknown) return false;
        // Only a vehicle carries a seat; any other kind is a malformed v2.
        if(p.kind!=Kind::vehicle && (b[3]!=kSeatUnknown || vehicle!=kVehicleUnknown)) return false;
        p.seat=b[3]; p.vehicle=vehicle;
    } else return false;
    if(!p.revision) return false;
    p.version=b[1];
    for(unsigned i=0;i<4;++i) p.sender_ms|=static_cast<std::uint32_t>(b[8+i])<<(i*8);
    out=p; return true;
}
bool Accept(Store& s, const std::uint8_t* b, std::size_t n, std::uint32_t epoch, std::uint64_t now) noexcept {
    Packet p{}; if(!epoch || !Decode(b,n,p)) { ++s.invalid; return false; }
    if(s.have && s.epoch==epoch) {
        const auto rev=static_cast<std::uint16_t>(p.revision-s.packet.revision);
        const auto ms=static_cast<std::uint32_t>(p.sender_ms-s.packet.sender_ms);
        if(rev>=0x8000 || (!rev && !SameState(p,s.packet)) || ms>=0x80000000u) { ++s.stale; return false; }
        if(!rev && !ms) { ++s.duplicates; return false; }
    }
    s.have=true; s.packet=p; s.epoch=epoch; s.received_ms=now; ++s.received; return true;
}
bool Live(const Store& s, std::uint32_t epoch, std::uint64_t now) noexcept {
    return s.have && epoch && s.epoch==epoch && now>=s.received_ms && now-s.received_ms<=kLeaseMs;
}
void Placed(Visibility& s, std::uint64_t now) noexcept { s.place_ms=now; s.retry_ms=now; }
bool EncodeExt(const Ext& e, std::uint8_t (&b)[kExtSize]) noexcept {
    for(auto& x : b) x=0;
    if(!e.revision || (e.features & ~ExtFeature::Known) || (e.seat>kSeatMax && e.seat!=kSeatUnknown)) return false;
    // A vehicle id is a type-50 id or unknown; a category is 0..15 or unknown.
    if(e.vehicle!=kVehicleUnknown && (e.vehicle>>9)!=kVehicleType) return false;
    if(e.category!=kCategoryUnknown && e.category>15) return false;
    if(e.type!=kTypeUnknown && e.category!=static_cast<std::uint8_t>(e.type>>4)) return false;
    b[0]=kMagic; b[1]=kVersionExt; b[2]=e.features; b[3]=e.category;
    b[4]=static_cast<std::uint8_t>(e.revision); b[5]=static_cast<std::uint8_t>(e.revision>>8);
    b[6]=static_cast<std::uint8_t>(e.vehicle); b[7]=static_cast<std::uint8_t>(e.vehicle>>8);
    b[8]=e.seat; b[9]=e.type;
    for(unsigned i=0;i<4;++i) b[12+i]=static_cast<std::uint8_t>(e.sender_ms>>(i*8));
    return true;
}
bool DecodeExt(const std::uint8_t* b, std::size_t n, Ext& out) noexcept {
    if(!IsExt(b,n) || b[10] || b[11]) return false;
    Ext e{};
    e.features=b[2]; e.category=b[3];
    e.revision=static_cast<std::uint16_t>(b[4]|(b[5]<<8));
    e.vehicle=static_cast<std::uint16_t>(b[6]|(b[7]<<8));
    e.seat=b[8]; e.type=b[9];
    for(unsigned i=0;i<4;++i) e.sender_ms|=static_cast<std::uint32_t>(b[12+i])<<(i*8);
    std::uint8_t check[kExtSize]{};
    if(!EncodeExt(e,check)) return false;  // the same field rules both ways
    out=e; return true;
}
bool AcceptExt(ExtStore& s, const std::uint8_t* b, std::size_t n, std::uint32_t epoch, std::uint64_t now) noexcept {
    Ext e{}; if(!epoch || !DecodeExt(b,n,e)) { ++s.invalid; return false; }
    if(s.have && s.epoch==epoch && static_cast<std::uint32_t>(e.sender_ms-s.ext.sender_ms)>=0x80000000u) { ++s.stale; return false; }
    s.have=true; s.ext=e; s.epoch=epoch; s.received_ms=now; ++s.received; return true;
}
bool ExtLive(const ExtStore& s, std::uint32_t epoch, std::uint64_t now) noexcept {
    return s.have && epoch && s.epoch==epoch && now>=s.received_ms && now-s.received_ms<=kLeaseMs;
}
std::uint8_t RemoteCategory(const ExtStore& s, const Packet& ride, std::uint32_t epoch, std::uint64_t now) noexcept {
    if(!ExtLive(s,epoch,now) || !(s.ext.features & ExtFeature::SeatId) || ride.kind!=Kind::vehicle) return kCategoryUnknown;
    if(s.ext.revision!=ride.revision || s.ext.vehicle!=ride.vehicle || s.ext.seat!=ride.seat) return kCategoryUnknown;
    return s.ext.category;
}
void Observe(Visibility& s, const In& in) noexcept {
    // Ring identity is stronger evidence than clocks/positions: a real reset
    // may preserve both. Legacy/synthetic generation0 keeps prior behavior.
    if(in.generation) {
        if(s.generation && s.generation!=in.generation) s.have_sample=s.stable=false;
        s.generation=in.generation;
    }
    const bool heli=in.anim_valid && in.remote_act>=kHeliActFirst && in.remote_act<=kHeliActLast;
    // Seat mode: a live vehicle ride in a usable seat is drawn seated, not hidden.
    const bool seat_ok=in.seat_mode && !heli && in.ride_live && in.kind==Kind::vehicle && SeatUsable(in.seat,in.vehicle);
    const bool riding=heli || (in.ride_live && in.kind!=Kind::foot && !seat_ok);
    const bool stale=in.ride_have && !in.ride_live;
    const bool foot=!heli && (!in.ride_have || (in.ride_live && in.kind==Kind::foot));
    if(s.air && in.body_anim_valid && in.body_act!=25) s.air=false;
    if(in.body_anim_valid && in.body_act==40) {
        if(s.dead_retry && in.rearmed) s.dead_locked=true;
        if(!s.dead_retry || s.dead_locked) { s.hidden=true; s.reason="dead"; }
    }
    // The air candidate (falling body, standing remote, right after our own
    // placement) must persist kAirHoldMs: a teleport placement's few frames of
    // act 25 never hide.
    const bool air_candidate=in.body_anim_valid && in.body_valid && in.anim_valid && in.body_act==25 && in.remote_act!=25 &&
        in.remote_act!=26 && s.place_ms && in.now_ms>=s.place_ms && in.now_ms-s.place_ms<=1500 && std::fabs(in.y-in.body_y)>=2.0;
    if(!air_candidate) s.air_pending=false;
    else if(!s.air_pending) { s.air_pending=true; s.air_since_ms=in.now_ms; }
    else if(in.now_ms>=s.air_since_ms && in.now_ms-s.air_since_ms>=kAirHoldMs) s.air=true;
    s.seated=seat_ok && !s.air && !s.dead_locked && in.lease_live;
    if(riding || stale || s.air || s.dead_locked || !in.lease_live) {
        s.hidden=true; s.reason=s.dead_locked ? "dead" : s.air ? "air" : riding ? "ride" : "ride_stale";
    } else if(s.seated) {
        // Seated is shown at once (the driver's seat flow places and seats it).
        s.hidden=false; s.reason=nullptr;
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
