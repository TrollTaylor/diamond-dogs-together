#pragma once
#include "sleep_partner_carry_proof.hpp"
#include "sleep_parts_policy.hpp"
namespace mgscoop::sleep_carry_context_proof {
namespace cp=sleep_partner_carry_proof;namespace sl=sleep_partner_lease;namespace st=sleep_partner_test;namespace hk=partner_input_hook;
inline bool Ready(const cp::Retained& r,std::uint64_t now,bool currentSharedPhysical)noexcept {
 const auto& l=r.lease;return r.configured&&r.enabled&&!r.preparing&&!r.wakePending&&!r.clearPending&&!r.faulted&&r.worker&&r.lifetime&&r.nonce&&r.image&&r.modelEntry&&
 l.phase==sl::Phase::active&&l.nativeEntered&&!l.wakeAuthorized&&!l.wakeAt&&!l.recoveryAt&&l.seededAt&&now>=l.seededAt&&
 l.lastSeen&&now>=l.lastSeen&&(currentSharedPhysical||now-l.lastSeen<=sl::kFreshMs)&&
 cp::AcceptedSleep(r.owned)&&cp::AcceptedSleep(r.current)&&sleep_partner_controls::FreshTarget(r.current,now)&&sleep_partner_controls::SameTargetScene(r.owned,r.current)&&
 r.owned.localRole==r.current.localRole&&r.lease.owner.mission==r.current.mission&&r.lease.owner.epoch==r.current.worldEpoch&&
 r.lease.owner.generation==r.current.peerGeneration&&r.lease.owner.armGeneration==r.current.armGeneration&&r.lease.owner.armEpoch==r.current.armEpoch;
}
inline bool SameJob(const hk::NativePoseJobContext& a,const hk::NativePoseJobContext& b)noexcept {
 return a.player==b.player&&a.go==b.go&&a.block==b.block&&a.state==b.state&&a.modelCS==b.modelCS&&a.modelEntry==b.modelEntry&&
 a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.thread==b.thread&&a.sequence==b.sequence&&a.consumer==b.consumer&&
 a.modelReady==b.modelReady&&a.selectedBodyOwned==b.selectedBodyOwned&&a.selectedBody==b.selectedBody&&a.pp==b.pp&&a.source==b.source&&
 a.windowPPVT==b.windowPPVT&&a.birthSerial==b.birthSerial;
}
inline bool Preparation(const cp::Retained& r,const hk::NativePoseJobContext& c,const sl::Scope& scope,const sl::Sample& a,st::CarryPreparationProof& out)noexcept {
 out={};if(!Ready(r,scope.now,false)||sl::ContextRefusal(scope,a)||!sl::Same(r.lease.owner,a.owner)||!sl::SameMaximum(r.lease,a)||
 !c.modelReady||!c.selectedBodyOwned||c.selectedBody!=1||!c.pp||!c.source||!c.windowPPVT||!c.sequence||!(c.sequence&1u)||
 scope.sequenceBefore!=c.sequence||scope.sequenceAfter!=c.sequence||c.thread!=a.owner.ownerThread||c.player!=a.owner.player||c.go+0x20!=c.player||
 c.block!=a.owner.block||c.state!=a.owner.ps||c.modelCS!=a.owner.cs||c.modelEntry!=r.modelEntry||c.birthSerial!=a.owner.birthSerial||
 c.generation!=a.owner.armGeneration||c.sceneEpoch!=a.owner.armEpoch||
 a.mode!=5||a.lifeState!=2||!a.nodeActive||a.action!=0x19||a.qAction!=0x84||(a.requested!=0)||!a.graphCurrent||!a.graphSleepCandidate||
 (a.graphHash!=0xd7654fb0a3f2ull&&a.graphHash!=0xc31867300b57ull)||a.stance<1||a.stance>3)return false;
 const auto& o=a.owner;
 out.image=r.image;out.player=o.player;out.go=c.go;out.data=o.data;out.life=o.life;out.block1=o.block;out.state1=o.ps;out.plugin=o.plugin;out.node=o.node;
 out.records=o.records;out.bitmap=o.bitmap;out.modelCS1=o.cs;out.modelEntry1=r.modelEntry;out.graph=o.graph;out.mission=o.mission;out.peerGeneration=o.generation;
 out.peerSequence=r.current.peerSequence;out.seedPeerSequence=r.owned.peerSequence;out.localRole=r.current.localRole;out.peerCause=2;out.armGeneration=o.armGeneration;
 out.armEpoch=o.armEpoch;out.thread=c.thread;out.sequence=c.sequence;out.worldEpoch=o.epoch;out.leaseNonce=r.nonce;out.birth1=o.birthSerial;
 out.acceptedAt=r.current.poll;out.lastNativeAt=r.lease.lastSeen;out.seededAt=r.lease.seededAt;out.issuedAt=scope.now;out.graphHash=a.graphHash;return true;
}
inline bool SamePreparation(const st::CarryPreparationProof& a,const st::CarryPreparationProof& b)noexcept {
 return a.image==b.image&&a.player==b.player&&a.go==b.go&&a.data==b.data&&a.life==b.life&&a.block1==b.block1&&a.state1==b.state1&&
 a.plugin==b.plugin&&a.node==b.node&&a.records==b.records&&a.bitmap==b.bitmap&&a.modelCS1==b.modelCS1&&a.modelEntry1==b.modelEntry1&&a.graph==b.graph&&
 a.mission==b.mission&&a.peerGeneration==b.peerGeneration&&a.peerSequence==b.peerSequence&&a.seedPeerSequence==b.seedPeerSequence&&a.localRole==b.localRole&&
 a.peerCause==b.peerCause&&a.armGeneration==b.armGeneration&&a.armEpoch==b.armEpoch&&a.thread==b.thread&&a.sequence==b.sequence&&a.worldEpoch==b.worldEpoch&&
 a.leaseNonce==b.leaseNonce&&a.birth1==b.birth1&&a.acceptedAt==b.acceptedAt&&a.lastNativeAt==b.lastNativeAt&&a.seededAt==b.seededAt&&a.graphHash==b.graphHash;
}
// A fresh actual shared ticket has its own execution thread. The immutable
// seed owner thread is bookkeeping, not permission to project the old writer.
inline bool SharedContext(const cp::Retained& r,const hk::NativePoseDualPartsContext& c,std::uint64_t now)noexcept {
 const auto& o=r.lease.owner;return Ready(r,now,true)&&c.image==r.image&&c.player==o.player&&c.go+0x20==c.player&&c.data==o.data&&c.state&&c.vars&&
 c.block[1]==o.block&&c.ps[1]==o.ps&&c.modelCS[1]==o.cs&&c.modelEntry[1]==r.modelEntry&&c.birthSerial[1]==o.birthSerial&&
 c.block[0]&&c.ps[0]&&c.modelCS[0]&&c.modelEntry[0]&&c.birthSerial[0]&&c.birthSerial[0]!=c.birthSerial[1]&&
 c.block[0]!=c.block[1]&&c.ps[0]!=c.ps[1]&&c.modelCS[0]!=c.modelCS[1]&&c.modelEntry[0]!=c.modelEntry[1]&&
 c.generation==o.armGeneration&&c.sceneEpoch==o.armEpoch&&c.thread&&c.sequence&&!(c.sequence&1u)&&c.callerRva==0x9c2a2a&&
 c.purposes==3&&!c.pending&&c.sharedSerial&&c.carrySerial&&c.local0ProfileCurrent&&!c.dirty&&c.pool[0]<16&&c.pool[1]<16&&c.pool[0]!=c.pool[1]&&
 c.status[0]==3&&c.status[1]==3&&(c.flags&1u)&&!(c.flags&0x100u)&&(c.flags&0x6000u)==0x2000u;
}
} // namespace mgscoop::sleep_carry_context_proof
