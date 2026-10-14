#pragma once
#include "mgscoop/partner_net.hpp"
namespace mgscoop::partner_net::reliable_origin {
struct Slot {std::uint64_t incarnation=0,acceptedTick=0;std::uint16_t id=0;bool stored=false;};
inline bool Valid(const ReliableOrigin& o) noexcept {
 return o.incarnation&&o.session&&o.peer.address&&o.localRole<2&&o.peerRole<2&&o.localRole!=o.peerRole;
}
inline bool SameSession(const ReliableOrigin& a,const ReliableOrigin& b) noexcept {
 return Valid(a)&&Valid(b)&&a.incarnation==b.incarnation&&a.session==b.session&&
  a.peer.address==b.peer.address&&a.peer.port==b.peer.port&&a.localRole==b.localRole&&a.peerRole==b.peerRole;
}
inline bool Record(const ReliableOrigin& current,const ReliableOrigin& authenticatedPacket,
 std::uint16_t actualStoredId,std::uint64_t insertionGetTickCount64,Slot& out) noexcept {
 if(!SameSession(current,authenticatedPacket)||!insertionGetTickCount64)return false;
 out={current.incarnation,insertionGetTickCount64,actualStoredId,true};return true;
}
inline bool Observe(const ReliableOrigin& current,std::uint16_t actualFrontId,const Slot& slot,ReliableOrigin& out) noexcept {
 out={};if(!Valid(current)||!slot.stored||slot.incarnation!=current.incarnation||
  slot.id!=actualFrontId||!slot.acceptedTick)return false;
 out=current;out.message=actualFrontId;out.acceptedTick=slot.acceptedTick;return true;
}
} // namespace mgscoop::partner_net::reliable_origin
