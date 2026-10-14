#pragma once
#include "mgscoop/sleep_partner_test.hpp"
#include "sleep_partner_controls.hpp"
#include <limits>
namespace mgscoop::sleep_partner_carry_proof {
namespace st=sleep_partner_test;namespace sl=sleep_partner_lease;namespace ctl=sleep_partner_controls;
struct Retained {
 bool configured=false,enabled=false,preparing=false,wakePending=false,clearPending=false,faulted=false,worker=false,lifetime=false;
 std::uintptr_t image=0,modelEntry=0;std::uint64_t nonce=0;
 st::Target owned{},current{};sl::Lease lease{};
};
inline bool AdvanceNonce(std::uint64_t& counter,std::uint64_t& value) noexcept {
 value=0;if(counter==std::numeric_limits<std::uint64_t>::max())return false;value=++counter;return true;
}
inline bool AcceptedSleep(const st::Target& t) noexcept {
 return t.asleep&&t.peerDown&&t.peerReady&&t.peerCause==2&&t.peerSequence&&t.peerSequence<=65535&&(t.localRole==1||t.localRole==2);
}
inline bool SameSlice(const st::CarrierSliceSnapshot& a,const st::CarrierSliceSnapshot& b) noexcept {
 return a.image==b.image&&a.player==b.player&&a.go==b.go&&a.data==b.data&&a.block1==b.block1&&a.state1==b.state1&&
 a.modelCS1==b.modelCS1&&a.modelEntry1==b.modelEntry1&&a.armGeneration==b.armGeneration&&a.armEpoch==b.armEpoch&&
 a.thread==b.thread&&a.sequence==b.sequence&&a.realLocal==b.realLocal&&a.selected==b.selected&&a.birth1==b.birth1&&
 a.carrierBirth0==b.carrierBirth0&&a.sliceNonce==b.sliceNonce&&a.carrySerial==b.carrySerial&&a.sleepHeld==b.sleepHeld;
}
inline bool Build(const Retained& r,const st::CarrierSliceSnapshot& c,std::uint64_t now,st::CarryLeaseProof& out) noexcept {
 out={};const auto& l=r.lease;const auto& o=l.owner;
 if(!r.configured||!r.enabled||r.preparing||r.wakePending||r.clearPending||r.faulted||!r.worker||!r.lifetime||!r.nonce||
    l.phase!=sl::Phase::active||!l.nativeEntered||l.wakeAuthorized||l.wakeAt||l.recoveryAt||!l.seededAt||!l.lastSeen||
    now<l.seededAt||now<l.lastSeen||now-l.lastSeen>sl::kFreshMs||!ctl::FreshTarget(r.current,now)||
    !AcceptedSleep(r.owned)||!AcceptedSleep(r.current)||!ctl::SameTargetScene(r.owned,r.current)||
    r.owned.localRole!=r.current.localRole)return false;
 if(!c.sleepHeld||!c.sliceNonce||!c.carrySerial||!c.carrierBirth0||!c.birth1||c.carrierBirth0==c.birth1||
    !c.sequence||!(c.sequence&1u)||c.realLocal||c.selected||!c.thread||c.thread!=o.ownerThread||
    !r.image||r.image>std::numeric_limits<std::uintptr_t>::max()-0xa01d000||c.image!=r.image||c.player!=o.player||c.go+0x20!=o.player||c.data!=o.data||c.block1!=o.block||c.state1!=o.ps||
    c.modelCS1!=o.cs||c.modelEntry1!=r.modelEntry||c.birth1!=o.birthSerial||c.armGeneration!=o.armGeneration||c.armEpoch!=o.armEpoch||
    !o.player||!o.data||!o.life||!o.block||!o.ps||!o.plugin||!o.node||!o.records||!o.bitmap||!o.cs||!o.graph||!r.modelEntry||
    o.mission!=r.current.mission||o.epoch!=r.current.worldEpoch||o.generation!=r.current.peerGeneration||
    o.armGeneration!=r.current.armGeneration||o.armEpoch!=r.current.armEpoch)return false;
 out.image=c.image;out.player=o.player;out.go=c.go;out.data=o.data;out.life=o.life;out.block1=o.block;out.state1=o.ps;
 out.plugin=o.plugin;out.node=o.node;out.records=o.records;out.bitmap=o.bitmap;out.modelCS1=o.cs;out.modelEntry1=r.modelEntry;out.graph=o.graph;
 out.mission=o.mission;out.worldEpoch=o.epoch;out.peerGeneration=o.generation;out.peerSequence=r.current.peerSequence;out.seedPeerSequence=r.owned.peerSequence;
 out.localRole=r.current.localRole;out.peerCause=2;out.armGeneration=o.armGeneration;out.armEpoch=o.armEpoch;out.thread=c.thread;out.sequence=c.sequence;
 out.leaseNonce=r.nonce;out.birth1=o.birthSerial;out.carrierBirth0=c.carrierBirth0;out.sliceNonce=c.sliceNonce;out.carrySerial=c.carrySerial;
 out.acceptedAt=r.current.poll;out.lastNativeAt=l.lastSeen;out.seededAt=l.seededAt;out.issuedAt=now;return true;
}
inline bool SameProof(const st::CarryLeaseProof& a,const st::CarryLeaseProof& b) noexcept {
 // issuedAt is a query time, never permission to renew an old accepted packet.
 return a.image==b.image&&a.player==b.player&&a.go==b.go&&a.data==b.data&&a.life==b.life&&a.block1==b.block1&&a.state1==b.state1&&
 a.plugin==b.plugin&&a.node==b.node&&a.records==b.records&&a.bitmap==b.bitmap&&a.modelCS1==b.modelCS1&&a.modelEntry1==b.modelEntry1&&a.graph==b.graph&&
 a.mission==b.mission&&a.worldEpoch==b.worldEpoch&&a.peerGeneration==b.peerGeneration&&a.peerSequence==b.peerSequence&&a.seedPeerSequence==b.seedPeerSequence&&
 a.localRole==b.localRole&&a.peerCause==b.peerCause&&a.armGeneration==b.armGeneration&&a.armEpoch==b.armEpoch&&a.thread==b.thread&&a.sequence==b.sequence&&
 a.leaseNonce==b.leaseNonce&&a.birth1==b.birth1&&a.carrierBirth0==b.carrierBirth0&&a.sliceNonce==b.sliceNonce&&a.carrySerial==b.carrySerial&&
 a.acceptedAt==b.acceptedAt&&a.lastNativeAt==b.lastNativeAt&&a.seededAt==b.seededAt;
}
}
