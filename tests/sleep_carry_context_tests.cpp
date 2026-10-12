#include "sleep_carry_context_proof.hpp"
#include "sleep_partner_bridge.hpp"
#include <cstdio>
#include <limits>
namespace cp=mgscoop::sleep_partner_carry_proof;namespace cx=mgscoop::sleep_carry_context_proof;namespace sl=mgscoop::sleep_partner_lease;namespace st=mgscoop::sleep_partner_test;namespace hk=mgscoop::partner_input_hook;
cp::Retained R(){cp::Retained r{};r.configured=r.enabled=r.worker=r.lifetime=true;r.image=0x140000000;r.modelEntry=0xf1000;r.nonce=7;
 r.owned={true,true,false,false,true,30050,17,91,9,2,900};r.owned.peerSequence=9;r.owned.localRole=1;r.owned.peerCause=2;r.owned.peerDown=r.owned.peerReady=true;r.current=r.owned;r.current.poll=1000;r.current.peerSequence=10;
 sl::Sample a{};a.owner={0x10000,0x20000,0xc004c,0x406a8,0x51000,0x81000,0x61000,0x71000,0x91000,0xe1000,0x121000,30050,17,88,91,9,2,107};a.stun=a.maximumStun=3300;
 r.lease=sl::Commit(a,1000,true,true);r.lease.phase=sl::Phase::active;r.lease.nativeEntered=true;return r;}
sl::Sample A(){auto a=sl::Sample{};a.owner=R().lease.owner;a.health=a.maximumHealth=6600;a.stun=100;a.maximumStun=3300;a.mode=5;a.lifeState=2;a.nodeActive=true;a.action=0x19;a.qAction=0x84;a.stance=3;a.cause=2;a.graphCurrent=a.graphSleepCandidate=true;a.graphHash=0xc31867300b57ull;return a;}
sl::Scope S(){sl::Scope s{};s.exactRetailGate=s.sleepCodeGate=s.seedCodeGate=s.workerActive=s.threadAdmitted=s.copiedSave=s.coherent=s.admitted=s.pairedOffline=s.selectedBodyOwned=s.ownedOddWindow=s.participantsLive=s.controlsZero=true;s.thread=88;s.sequenceBefore=s.sequenceAfter=11;s.now=1100;s.poll=1000;return s;}
hk::NativePoseJobContext J(){hk::NativePoseJobContext c{};const auto o=R().lease.owner;c.player=o.player;c.go=c.player-0x20;c.block=o.block;c.state=o.ps;c.modelCS=o.cs;c.modelEntry=R().modelEntry;c.generation=91;c.sceneEpoch=9;c.thread=88;c.sequence=11;c.modelReady=c.selectedBodyOwned=true;c.selectedBody=1;c.pp=0x150000;c.source=0x170000;c.windowPPVT=0x160000;c.birthSerial=107;return c;}
hk::NativePoseDualPartsContext D(){hk::NativePoseDualPartsContext c{};const auto r=R();const auto o=r.lease.owner;c.image=r.image;c.player=o.player;c.go=c.player-0x20;c.data=o.data;c.state=0x30000;c.vars=0x142c01360;
 c.block[0]=0x40000;c.ps[0]=0x50000;c.modelCS[0]=0xe0000;c.modelEntry[0]=0xf0000;c.birthSerial[0]=106;
 c.block[1]=o.block;c.ps[1]=o.ps;c.modelCS[1]=o.cs;c.modelEntry[1]=r.modelEntry;c.birthSerial[1]=o.birthSerial;
 c.generation=91;c.sceneEpoch=9;c.thread=99;c.sequence=12;c.callerRva=0x9c2a2a;c.purposes=3;c.sharedSerial=13;c.carrySerial=14;c.flags=0x1a487;c.pool[1]=1;c.status[0]=c.status[1]=3;c.local0ProfileCurrent=true;return c;}
int main(){unsigned good=0,bad=0;const auto Check=[&](bool ok,const char* why){if(ok)++good;else{++bad;std::printf("FAIL %s\n",why);}};
 {
  auto r=R();auto j=J();auto a=A();st::CarryPreparationProof proof{};
  Check(cx::Preparation(r,j,S(),a,proof)&&proof.leaseNonce==7&&proof.peerSequence==10&&proof.seedPeerSequence==9&&proof.sequence==11,"actual selected1 preparation seals current and original accepted seed rows without selected0 projection");
  auto same=proof;same.issuedAt++;Check(cx::SamePreparation(proof,same),"query time cannot renew accepted packet");same.peerSequence++;Check(!cx::SamePreparation(proof,same),"new accepted row requires new current query before acquisition");
  for(unsigned k=0;k<23;++k){r=R();j=J();a=A();auto scope=S();if(k==0)r.preparing=true;if(k==1)r.wakePending=true;if(k==2)r.clearPending=true;if(k==3)r.faulted=true;if(k==4)r.nonce=0;
   if(k==5)r.current.peerCause=1;if(k==6)r.current.peerReady=false;if(k==7)r.current.peerDown=false;if(k==8)r.current.localRole=0;if(k==9)r.current.peerSequence=0;if(k==10)r.current.poll=849;
   if(k==11)r.lease.recoveryAt=1050;if(k==12)j.sequence=12;if(k==13)j.selectedBody=0;if(k==14)j.birthSerial++;if(k==15)scope.controlsZero=false;if(k==16)a.lifeState=0;if(k==17)a.requested=6;
   if(k==18)a.graphHash=0x40d8fda5ca06ull;if(k==19)a.nodeActive=false;if(k==20)a.maximumStun=3000;if(k==21)r.lease.lastSeen=349;if(k==22)j.windowPPVT=0;
   Check(!cx::Preparation(r,j,scope,a,proof),"stale/mixed cause/grant/clear/owner/source/state/graph/current-window preparation rejected");
  }
 }
 {
  const auto r=R();const auto d=D();Check(cx::SharedContext(r,d,1100)&&d.thread!=r.lease.owner.ownerThread,"real shared receipt has distinct execution thread, never projected from seed writer");
  for(unsigned k=0;k<19;++k){auto held=r;auto c=d;if(k==0)c.sequence=11;if(k==1)c.sharedSerial=0;if(k==2)c.carrySerial=0;if(k==3)c.purposes=1;if(k==4)c.pending=1;
   if(k==5)c.birthSerial[0]=c.birthSerial[1];if(k==6)c.block[0]=c.block[1];if(k==7)c.local0ProfileCurrent=false;if(k==8)c.dirty=1;if(k==9)c.status[0]=1;
   if(k==10)c.callerRva=0x9c2d3c;if(k==11)c.generation++;if(k==12)c.flags|=0x100;if(k==13)held.current.asleep=false;if(k==14)held.current.peerGeneration++;if(k==15)held.lease.wakeAuthorized=true;
   if(k==16)held.lease.recoveryAt=1050;if(k==17)c.modelEntry[1]+=8;if(k==18)held.current.localRole=2;
   Check(!cx::SharedContext(held,c,1100),"missing/odd ticket wrong dual models/profile/cycle/role/scene/retirement denies shared authority");
  }
  auto staleCheckpoint=r;staleCheckpoint.lease.lastSeen=1;Check(cx::SharedContext(staleCheckpoint,d,1100)&&!cx::Ready(staleCheckpoint,1100,false),"only separate current shared physical path can continue paused Carry without renewing stale native checkpoint");
 }
 {
  auto r=R();st::CarrierSliceSnapshot c{};const auto o=r.lease.owner;c.image=r.image;c.player=o.player;c.go=c.player-0x20;c.data=o.data;c.block1=o.block;c.state1=o.ps;c.modelCS1=o.cs;c.modelEntry1=r.modelEntry;
  c.armGeneration=91;c.armEpoch=9;c.thread=88;c.sequence=13;c.birth1=107;c.carrierBirth0=106;c.sliceNonce=99;c.carrySerial=14;c.sleepHeld=true;
  st::CarryLeaseProof proof{};Check(cp::Build(r,c,1100,proof)&&proof.sliceNonce==99&&proof.carrySerial==14&&proof.acceptedAt==1000,"distinct actual selected0 slice retains original exclusive proof");
  c.selected=1;Check(!cp::Build(r,c,1100,proof),"selected1 job cannot masquerade as carrier0 force slice");c.selected=0;c.sequence=12;Check(!cp::Build(r,c,1100,proof),"shared even receipt cannot become selected0 exclusive proof");
 }
 {
  std::uint64_t counter=0,nonce=0;Check(cp::AdvanceNonce(counter,nonce)&&nonce==1&&cp::AdvanceNonce(counter,nonce)&&nonce==2,"monotonic seed nonce never reuses a retired cycle");
  counter=std::numeric_limits<std::uint64_t>::max();Check(!cp::AdvanceNonce(counter,nonce)&&nonce==0,"exhaustion never wraps a new usable cycle");
 }
 {
  namespace bridge=mgscoop::sleep_partner_bridge;bridge::Context ctx{};ctx.now=1000;ctx.mission=30050;ctx.epoch=2;ctx.role=bridge::ConfiguredRole(mgscoop::LabRole::host);ctx.thread=88;ctx.worker=ctx.admitted=ctx.naturalOwner=ctx.fieldReady=true;
  bridge::ArmOwner arm{};arm.ready=true;arm.generation=91;arm.epoch=9;arm.thread=88;arm.at=1000;bridge::Record record{false,30050,1,33,17,1,2,1,0,2};const auto accepted=bridge::Accept(bridge::Ledger{},record,ctx,arm);
  Check(accepted.ready&&accepted.target.peerSequence==33&&accepted.target.localRole==1&&accepted.target.peerCause==2&&accepted.target.peerDown&&accepted.target.peerReady,"actual accepted bridge publishes bounded cause2/seq/configured local role");
 }
 std::printf("sleep_carry_context_tests: %u passed; %u failed\n",good,bad);return bad?1:0;
}
