#pragma once
#include "sleep_carry_context_proof.hpp"
#include "mgscoop/native_pose_carry_deferred.hpp"
namespace mgscoop::sleep_deferred_carry_proof {
namespace cx=sleep_carry_context_proof;namespace cp=sleep_partner_carry_proof;namespace st=sleep_partner_test;namespace hk=partner_input_hook;
using Context=hk::NativePoseDeferredCarryContext;
inline bool SameQueue(const hk::NativePoseCarryQueueContext& a,const hk::NativePoseCarryQueueContext& b)noexcept {
 return cx::SameJob(a.job,b.job)&&a.root==b.root&&a.slab==b.slab&&a.queued==b.queued&&a.snapshot==b.snapshot&&a.bytes==b.bytes&&a.carrySerial==b.carrySerial;
}
inline bool SameContext(const Context& a,const Context& b)noexcept {
 const auto& x=a.submission;const auto& y=b.submission;
 return a.image==b.image&&a.carrierRecord==b.carrierRecord&&a.recipientRecord==b.recipientRecord&&cx::SameJob(a.job,b.job)&&SameQueue(a.queue,b.queue)&&
 a.intentNonce==b.intentNonce&&a.intentAt==b.intentAt&&a.birth0==b.birth0&&a.birth1==b.birth1&&a.priorSequence==b.priorSequence&&a.copyReserved==b.copyReserved&&
 x.image==y.image&&x.player==y.player&&x.go==y.go&&x.data==y.data&&x.block1==y.block1&&x.state1==y.state1&&x.modelCS1==y.modelCS1&&x.modelEntry1==y.modelEntry1&&
 x.armGeneration==y.armGeneration&&x.armEpoch==y.armEpoch&&x.thread==y.thread&&x.sequence==y.sequence&&x.handlerRva==y.handlerRva&&x.birth1==y.birth1&&x.carrierBirth0==y.carrierBirth0&&
 x.carrySerial==y.carrySerial&&x.sharedSerial==y.sharedSerial&&x.submitNonce==y.submitNonce&&x.submittedAt==y.submittedAt&&x.sleepHeld==y.sleepHeld&&x.carryHeld==y.carryHeld&&
 cp::SameProof(x.submittedSleep,y.submittedSleep)&&x.submittedSleep.issuedAt==y.submittedSleep.issuedAt;
}
// Old even ticket/thread are immutable evidence of successful op0 only.
// Execution admission comes solely from the re-queried real job/queue writer.
inline bool ContextCurrent(const cp::Retained& r,const Context& c,std::uint64_t now)noexcept {
 if(!cx::Ready(r,now,true)||!c.image||!c.carrierRecord||!c.recipientRecord||c.carrierRecord==c.recipientRecord||
 !c.intentNonce||!c.intentAt||now<c.intentAt||now-c.intentAt>750||!c.birth0||!c.birth1||c.birth0==c.birth1)return false;
 const auto& x=c.submission;const auto& p=x.submittedSleep;const auto& o=r.lease.owner;const auto& j=c.job;const auto& q=c.queue;
 if(!x.sleepHeld||!x.carryHeld||!x.sharedSerial||!x.submitNonce||!x.submittedAt||x.handlerRva!=0x11268b0||!x.thread||!x.sequence||(x.sequence&1u)||
 c.intentAt<x.submittedAt||c.intentAt-x.submittedAt>750||!p.issuedAt||p.issuedAt>x.submittedAt||
 !p.leaseNonce||p.leaseNonce!=r.nonce||!p.sliceNonce||!p.sequence||!(p.sequence&1u)||x.sequence<=p.sequence||
 !p.thread||p.thread!=o.ownerThread||!p.seededAt||p.seededAt!=r.lease.seededAt||!p.acceptedAt||p.acceptedAt>p.issuedAt||
 !p.lastNativeAt||p.lastNativeAt>p.issuedAt||p.lastNativeAt>r.lease.lastSeen||p.peerCause!=2||!p.peerSequence||p.peerSequence>65535||
 p.seedPeerSequence!=r.owned.peerSequence||p.localRole!=r.current.localRole||p.mission!=r.current.mission||p.worldEpoch!=r.current.worldEpoch||
 p.peerGeneration!=r.current.peerGeneration||p.armGeneration!=r.current.armGeneration||p.armEpoch!=r.current.armEpoch)return false;
 if(c.image!=r.image||x.image!=c.image||x.player!=o.player||x.go+0x20!=x.player||x.data!=o.data||x.block1!=o.block||x.state1!=o.ps||
 x.modelCS1!=o.cs||x.modelEntry1!=r.modelEntry||x.armGeneration!=o.armGeneration||x.armEpoch!=o.armEpoch||
 x.birth1!=o.birthSerial||c.birth1!=x.birth1||c.birth0!=x.carrierBirth0||!x.carrySerial||x.carrySerial!=p.carrySerial||
 p.image!=x.image||p.player!=x.player||p.go!=x.go||p.data!=x.data||p.block1!=x.block1||p.state1!=x.state1||p.modelCS1!=x.modelCS1||p.modelEntry1!=x.modelEntry1||
 p.birth1!=x.birth1||p.carrierBirth0!=x.carrierBirth0||p.life!=o.life||p.plugin!=o.plugin||p.node!=o.node||p.records!=o.records||p.bitmap!=o.bitmap||p.graph!=o.graph)return false;
 if(!j.modelReady||!j.selectedBodyOwned||j.selectedBody!=1||!j.pp||!j.source||!j.windowPPVT||!j.sequence||!(j.sequence&1u)||j.sequence<=x.sequence||
 j.thread!=o.ownerThread||j.player!=o.player||j.go!=x.go||j.block!=o.block||j.state!=o.ps||j.modelCS!=o.cs||j.modelEntry!=r.modelEntry||
 j.generation!=o.armGeneration||j.sceneEpoch!=o.armEpoch||j.birthSerial!=o.birthSerial||!cx::SameJob(j,q.job)||q.carrySerial!=x.carrySerial||
 o.data<0x20||q.root!=o.data-0x20||!q.slab||(q.slab&15u)||q.bytes<0x80||q.bytes>0x100000||q.slab>UINTPTR_MAX-q.bytes||
 !q.queued||(q.queued&15u)||q.queued<q.slab||q.queued-q.slab>q.bytes||0x80>q.bytes-(q.queued-q.slab)||!q.snapshot)return false;
 return true;
}
inline bool Physical(const cp::Retained& r,const hk::NativePoseJobContext& j,const sleep_partner_lease::Scope& scope,const sleep_partner_lease::Sample& a)noexcept {
 namespace sl=sleep_partner_lease;
 return cx::Ready(r,scope.now,true)&&!sl::ContextRefusal(scope,a)&&sl::Same(r.lease.owner,a.owner)&&sl::SameMaximum(r.lease,a)&&
 scope.sequenceBefore==j.sequence&&scope.sequenceAfter==j.sequence&&scope.thread==j.thread&&j.thread==a.owner.ownerThread&&
 j.selectedBodyOwned&&j.selectedBody==1&&j.birthSerial==a.owner.birthSerial&&
 a.mode==5&&!a.requested&&a.lifeState==2&&a.nodeActive&&a.action==0x19&&a.qAction==0x84&&a.stance>=1&&a.stance<=3&&
 a.graphCurrent&&a.graphSleepCandidate&&(a.graphHash==0xd7654fb0a3f2ull||a.graphHash==0xc31867300b57ull);
}
inline bool SameProof(const st::DeferredCarrySleepProof& a,const st::DeferredCarrySleepProof& b)noexcept {
 return cp::SameProof(a.submitted,b.submitted)&&a.submitted.issuedAt==b.submitted.issuedAt&&cx::SameJob(a.job,b.job)&&SameQueue(a.queue,b.queue)&&
 a.carrierRecord==b.carrierRecord&&a.recipientRecord==b.recipientRecord&&a.currentPeerSequence==b.currentPeerSequence&&a.submitNonce==b.submitNonce&&a.submittedAt==b.submittedAt&&
 a.intentNonce==b.intentNonce&&a.intentAt==b.intentAt&&a.birth0==b.birth0&&a.birth1==b.birth1&&a.currentAcceptedAt==b.currentAcceptedAt&&a.priorSequence==b.priorSequence&&a.copyReserved==b.copyReserved;
}
template<class Read,class Current>bool Validate(const st::CarryLeaseProof& expected,Read&& read,Current&& current)noexcept {
 st::DeferredCarrySleepProof actual{};
 if(!read(actual)||!expected.issuedAt||expected.issuedAt!=actual.submitted.issuedAt||!cp::SameProof(expected,actual.submitted))return false;
 return current(actual); // distinct actual selected1 requery, never legacy shared/selected0 Current
}
} // namespace mgscoop::sleep_deferred_carry_proof
