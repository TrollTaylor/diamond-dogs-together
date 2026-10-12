#pragma once
#include "mgscoop/native_pose_sleep_parts.hpp"
#include "native_pose_parts_diagnostic.hpp"
#include "sleep_partner_lease.hpp"
namespace mgscoop::sleep_parts_policy {
namespace sl=sleep_partner_lease;using Context=partner_input_hook::NativePoseSleepPartsContext;
inline bool NoRemoteWork(const native_pose_parts_diagnostic::Witness& w)noexcept {
 return w.count==2&&w.realized==2&&w.local==0&&w.selected==0&&w.actualId[0]==0&&w.actualId[1]==1&&
 w.pool[0]<16&&w.pool[1]<16&&w.pool[0]!=w.pool[1]&&w.blockMatched[1]&&w.parts[1]==3&&
 ((w.mask180|w.mask184)&2u)==0&&(w.flags&1u)&&(w.flags&0x100u)==0&&
 (w.flags&0x6000u)==0x2000u;
}
inline bool Same(const Context& a,const Context& b)noexcept {
 return a.image==b.image&&a.player==b.player&&a.go==b.go&&a.data==b.data&&a.state==b.state&&
 a.block==b.block&&a.ps==b.ps&&a.modelCS==b.modelCS&&a.modelEntry==b.modelEntry&&
 a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.thread==b.thread&&a.sequence==b.sequence&&
 a.callerRva==0x9c2a2a&&b.callerRva==a.callerRva&&a.birthSerial==b.birthSerial&&a.sharedSerial==b.sharedSerial&&
 // Local0 model and pending bit0 may legitimately change inside the original.
 (a.flags&0x6101u)==(b.flags&0x6101u)&&a.remotePending==b.remotePending&&a.remoteStatus==b.remoteStatus&&a.remotePool==b.remotePool;
}
inline bool ContextValid(const Context& c)noexcept {
 return c.image&&c.player&&c.go<=UINTPTR_MAX-0x20&&c.go+0x20==c.player&&c.data&&c.state&&c.block&&c.ps&&c.modelCS&&c.modelEntry&&
 c.generation&&c.sceneEpoch&&c.thread&&c.sequence&&!(c.sequence&1u)&&c.callerRva==0x9c2a2a&&c.birthSerial&&c.sharedSerial&&
 c.remotePending==0&&c.remoteStatus==3&&c.remotePool<16&&(c.flags&1u)&&!(c.flags&0x100u)&&(c.flags&0x6000u)==0x2000u;
}
inline bool CurrentSample(bool preparing,std::uint64_t preparedAt,const sl::Sample& prepared,const sl::Lease& l,
 const sl::Sample& a,std::uint64_t now)noexcept {
 const auto& own=l.phase==sl::Phase::empty?prepared.owner:l.owner;
 if(!now||!sl::Same(own,a.owner)||!a.graphCurrent||!std::isfinite(a.health)||!std::isfinite(a.maximumHealth)||
 a.health<=0||a.health>a.maximumHealth||a.maximumHealth<1||a.maximumHealth>65535||!sl::ValidStun(a)||
 a.stance<1||a.stance>3||a.cause!=2||a.lifeState>2)return false;
 if(l.phase==sl::Phase::empty)return preparing&&preparedAt&&now>=preparedAt&&now-preparedAt<=sl::kAcquireMs&&
 sl::Bits(prepared.maximumStun)==sl::Bits(a.maximumStun)&&sl::RetiredSample(a);
 if((l.phase!=sl::Phase::seeded&&l.phase!=sl::Phase::active&&l.phase!=sl::Phase::wakePending&&l.phase!=sl::Phase::retired)||!l.lastSeen||now<l.lastSeen||now-l.lastSeen>sl::kFreshMs||
 !sl::SameMaximum(l,a)||!l.seededAt||now<l.seededAt||(!l.nativeEntered&&now-l.seededAt>sl::kAcquireMs)||
 (l.wakeAt&&(now<l.wakeAt||now-l.wakeAt>sl::kRetireMs)))return false;
 if(l.phase==sl::Phase::retired)return l.nativeEntered&&sl::RetiredSample(a);
 if(a.mode==0){
  if(a.lifeState==2)return !l.nativeEntered&&l.phase==sl::Phase::seeded&&!a.nodeActive&&!a.graphSleepCandidate&&
   a.action==0x44&&(a.qAction==1||a.qAction==5)&&a.stance==1&&(a.requested==0||a.requested==4);
  // Retained idle is still held; this permission never clears/unlocks the lease.
  return l.nativeEntered&&sl::RetiredSample(a);
 }
 if(!a.nodeActive||a.action!=0x19||a.qAction!=std::uint16_t(0x7f+a.mode))return false;
 if(a.mode==4)return a.lifeState==2&&(a.requested==0||a.requested==5);
 if(a.mode==5)return (a.requested==0||a.requested==6)&&(a.lifeState==2||(a.lifeState==0&&l.wakeAuthorized));
 if(a.mode==6)return a.lifeState==0&&l.nativeEntered&&a.requested==0;
 return false;
}
}
