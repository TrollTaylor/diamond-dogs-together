#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include "coop_passenger.hpp"
namespace mgscoop::vehicle_mirror_state {
enum class NameKind : std::uint8_t { light=1,truck=2 };
struct Name {NameKind kind=NameKind::light;std::uint16_t index=0;};
inline bool Valid(Name n) noexcept {return (n.kind==NameKind::light&&n.index<13)||(n.kind==NameKind::truck&&n.index<6);}
inline bool Same(Name a,Name b) noexcept{return a.kind==b.kind&&a.index==b.index;}
struct Link {std::uint64_t serial=0;std::uint32_t mission=0;std::uint16_t epoch=0;std::uint8_t localRole=0,peerRole=1;};
inline bool Valid(const Link& l) noexcept{return l.serial&&l.epoch&&(l.mission==30010||l.mission==30020||l.mission==30050)&&l.localRole<2&&l.peerRole<2&&l.localRole!=l.peerRole;}
inline bool Same(const Link& a,const Link& b) noexcept{return a.serial==b.serial&&a.mission==b.mission&&a.epoch==b.epoch&&a.localRole==b.localRole&&a.peerRole==b.peerRole;}
enum class Event : std::uint8_t { mounted=1,exit=2 };
struct Receipt {
 Event event=Event::mounted;std::uint8_t role=0,phase=0,seat=4,cycle=0;
 Name name{};std::uint32_t mission=0,mount=0,driverMount=0,sequence=0;std::uint16_t epoch=0;
};
inline bool Valid(const Receipt& r) noexcept {
 if(!Valid(r.name)||r.role>1||!r.epoch||!r.mount||!r.sequence||
 (r.mission!=30010&&r.mission!=30020&&r.mission!=30050))return false;
 if(r.event==Event::exit)return r.phase==0&&(r.seat==0||r.seat==2||r.seat==3);
 return r.event==Event::mounted&&((r.phase==3&&r.seat==0&&!r.driverMount)||
 (r.phase==2&&(r.seat==2||r.seat==3)&&r.driverMount));
}
inline bool Newer(std::uint32_t a,std::uint32_t b) noexcept {return a&&a>b;}
constexpr std::size_t wireSize=32;
inline void Put16(unsigned char* p,std::uint16_t x) noexcept {p[0]=static_cast<unsigned char>(x);p[1]=static_cast<unsigned char>(x>>8);}
inline void Put32(unsigned char* p,std::uint32_t x) noexcept {for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(x>>(i*8));}
inline std::uint16_t Get16(const unsigned char* p) noexcept{return static_cast<std::uint16_t>(p[0]|(p[1]<<8));}
inline std::uint32_t Get32(const unsigned char* p) noexcept{return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
inline bool Encode(const Receipt& r,unsigned char* p,std::size_t n) noexcept {
 if(!p||n!=wireSize||!Valid(r))return false;std::memset(p,0,n);p[0]='V';p[1]=1;p[2]=static_cast<unsigned char>(r.event);p[3]=r.role;
 Put32(p+4,r.mission);Put16(p+8,r.epoch);p[10]=static_cast<unsigned char>(r.name.kind);p[11]=r.phase;
 Put16(p+12,r.name.index);p[14]=r.seat;p[15]=r.cycle;Put32(p+16,r.mount);Put32(p+20,r.driverMount);Put32(p+24,r.sequence);return true;
}
inline bool Decode(const unsigned char* p,std::size_t n,Receipt& r) noexcept {
 r={};if(!p||n!=wireSize||p[0]!='V'||p[1]!=1||Get32(p+28))return false;
 r.event=static_cast<Event>(p[2]);r.role=p[3];r.mission=Get32(p+4);r.epoch=Get16(p+8);
 r.name.kind=static_cast<NameKind>(p[10]);r.phase=p[11];r.name.index=Get16(p+12);r.seat=p[14];r.cycle=p[15];
 r.mount=Get32(p+16);r.driverMount=Get32(p+20);r.sequence=Get32(p+24);return Valid(r);
}
using Unit=coop_passenger::detail::Unit;
inline bool SameAllocation(const Unit& a,const Unit& b) noexcept {
 return a.service==b.service&&a.object==b.object&&a.records==b.records&&a.bitmap==b.bitmap&&a.unit==b.unit&&
 a.metadata==b.metadata&&a.variantTable==b.variantTable&&a.variant==b.variant&&a.vehicle==b.vehicle&&
 a.allocated==b.allocated&&a.type==b.type&&a.baseCount==b.baseCount&&a.extraCount==b.extraCount&&
 a.count==b.count&&a.liveWord==b.liveWord;
}
enum class Phase : std::uint8_t { idle,pending,admitted,queued,bound,attached,exiting,quarantined };
struct Scope {
 bool current=false,driveCurrent=true;Link link{};std::uint32_t generation=0,thread=0,sequence=0;
 std::uintptr_t player=0,primary=0,record=0;std::uint64_t purpose=0,now=0;
};
struct Lease {
 Phase phase=Phase::idle;Link link{};Receipt source{};Unit unit{};
 std::uint32_t generation=0,thread=0;std::uintptr_t player=0,primary=0,record=0;
 std::uint64_t purpose=0,received=0;bool releaseRequested=false,exitPending=false;
};
struct Inbox {
 Link link{};Receipt receipt{};std::uint64_t received=0;std::uint32_t sequence=0;
 bool Receive(const Link& current,const Receipt& r,std::uint64_t now) noexcept {
  if(!Valid(current)||!Valid(r)||r.role!=current.peerRole||r.mission!=current.mission||r.epoch!=current.epoch||!now)return false;
  if(!Same(link,current)){link=current;sequence=0;receipt={};received=0;}
  if(!Newer(r.sequence,sequence))return false;
  if(received&&r.mount<receipt.mount)return false;
  if(received&&r.mount==receipt.mount&&(!Same(r.name,receipt.name)||r.seat!=receipt.seat||r.driverMount!=receipt.driverMount))return false;
  if(received&&r.mount==receipt.mount&&receipt.event==Event::exit&&r.event!=Event::exit)return false;
  receipt=r;received=now;sequence=r.sequence;return true;
 }
};
inline bool Owned(const Lease& l,const Scope& s) noexcept {
 // This is native identity only. Authentication expiry cannot erase a real
 // physical seat, callback or allocator obligation. The exact core writer,
 // Arm, births and lease still have to remain current.
 return s.current&&l.generation==s.generation&&l.thread==s.thread&&
 l.player==s.player&&l.primary==s.primary&&l.record==s.record&&l.purpose&&l.purpose==s.purpose&&
 s.generation&&s.thread&&s.player&&s.primary&&s.record&&(s.sequence&1u);
}
inline bool Fresh(std::uint64_t now,std::uint64_t at) noexcept{return at&&now>=at&&now-at<=1500;}
inline bool ReceiveCaptured(Inbox& inbox,const Link& current,const Link& actualMessageOrigin,
 const Receipt& receipt,std::uint64_t actualAcceptedGetTickCount64,std::uint64_t currentGetTickCount64) noexcept {
 return Valid(actualMessageOrigin)&&Same(current,actualMessageOrigin)&&
  Fresh(currentGetTickCount64,actualAcceptedGetTickCount64)&&
  inbox.Receive(actualMessageOrigin,receipt,actualAcceptedGetTickCount64);
}
inline bool DriveOwned(const Lease& l,const Scope& s,const Inbox& in) noexcept {
 return Owned(l,s)&&s.driveCurrent&&Valid(s.link)&&Same(l.link,s.link)&&Same(in.link,l.link)&&
  Fresh(s.now,in.received)&&in.receipt.event==Event::mounted&&in.receipt.mount==l.source.mount&&
  Same(in.receipt.name,l.source.name)&&in.receipt.role==l.source.role&&in.receipt.phase==l.source.phase&&
  in.receipt.seat==l.source.seat&&in.receipt.driverMount==l.source.driverMount;
}
// Backend methods are actual owned native reads, stock calls and purpose
// receipts. The same controller is exercised with a fake backend in fixtures.
// No success result alone advances past physical admission or attachment.
template<class Backend> bool Step(Backend& b,const Scope& s,const Inbox& in,Lease& l,
 const Receipt* currentLocalDriver=nullptr) noexcept {
 if(l.phase==Phase::quarantined)return false;
 if(l.phase!=Phase::idle&&!Owned(l,s)){l.phase=Phase::quarantined;return false;}
 if(l.phase==Phase::idle){
  if(!s.current||!s.driveCurrent||!Valid(s.link)||!Same(s.link,in.link)||!Fresh(s.now,in.received)||in.receipt.event!=Event::mounted||
   !s.generation||!s.thread||!s.player||!s.primary||!s.record||!s.purpose||!(s.sequence&1u))return false;
  Unit unit{};if(!b.Resolve(in.receipt.name,unit)||!unit.unit||!unit.count||unit.count>4||in.receipt.seat>=unit.count)return false;
  if(in.receipt.phase==3){if(unit.rider[0]!=0xffff)return false;}
  else if(!currentLocalDriver||!Valid(*currentLocalDriver)||currentLocalDriver->event!=Event::mounted||
   currentLocalDriver->phase!=3||currentLocalDriver->seat||currentLocalDriver->mount!=in.receipt.driverMount||
   currentLocalDriver->role!=s.link.localRole||currentLocalDriver->mission!=s.link.mission||currentLocalDriver->epoch!=s.link.epoch||
   !Same(currentLocalDriver->name,in.receipt.name)||unit.rider[0]!=0||unit.rider[in.receipt.seat]!=0xffff)return false;
  l.phase=Phase::pending;l.link=s.link;l.source=in.receipt;l.unit=unit;l.generation=s.generation;l.thread=s.thread;
  l.player=s.player;l.primary=s.primary;l.record=s.record;l.purpose=s.purpose;l.received=in.received;
 }
 const bool wantsExit=!DriveOwned(l,s,in);if(wantsExit)l.exitPending=true;
 if(wantsExit&&l.phase==Phase::pending){if(b.Release(l.purpose))l={};return false;}
 if(l.phase==Phase::pending){
  Unit before{};if(!b.Current(l.unit,before)||!SameAllocation(before,l.unit)||before.rider[l.source.seat]!=0xffff)return false;
  const bool accepted=b.Admit(before,l.source.seat,1);Unit after{};
  // Even a false native return can leave an obligation. Never discard it.
  if(!b.Current(before,after)||!SameAllocation(before,after)){l.phase=Phase::quarantined;return false;}
  if(after.rider[l.source.seat]==1){l.unit=after;l.phase=accepted?Phase::admitted:Phase::quarantined;}
  else if(after.rider[l.source.seat]!=0xffff){l.phase=Phase::quarantined;return false;}
  else return false;
 }
 if(l.phase==Phase::admitted){
  // A native seat accepted but not queued cannot be guessed out of phase0.
  // If authority expires here, retain and quarantine the physical obligation.
  if(l.exitPending){l.phase=Phase::quarantined;return false;}
  if(!b.Submit(l.unit.vehicle,l.source.phase,l.source.seat,l.source.cycle)){l.phase=Phase::quarantined;return false;}
  l.phase=Phase::queued;
 }
 if(l.phase==Phase::queued&&b.Bound(l))l.phase=Phase::bound;
 if(l.phase==Phase::bound&&b.Attached(l))l.phase=Phase::attached;
 // An already queued native entry may finish its accepted binding, then exit.
 // No new admission or mount request is produced after source expiry.
 if(l.exitPending&&(l.phase==Phase::bound||l.phase==Phase::attached))l.phase=Phase::exiting;
 if(l.phase==Phase::exiting){
  Unit current{};if(!b.Current(l.unit,current)||!SameAllocation(l.unit,current)){l.phase=Phase::quarantined;return false;}
  if(current.rider[l.source.seat]!=1&&current.rider[l.source.seat]!=0xffff){l.phase=Phase::quarantined;return false;}
  if(!l.releaseRequested){if(!b.Submit(l.unit.vehicle,6,l.source.seat,l.source.cycle))return false;l.releaseRequested=true;}
  if(!b.Exited(l))return false;
  Unit retired{};if(!b.Current(l.unit,retired)||!SameAllocation(l.unit,retired)||retired.rider[l.source.seat]!=0xffff)return false;
  if(!b.Release(l.purpose))return false;l={};return true;
 }
 return l.phase==Phase::attached;
}
} // namespace mgscoop::vehicle_mirror_state
