#include "sleep_queued_carry_proof.hpp"
#include <cstdio>
namespace qc=mgscoop::sleep_queued_carry_proof;namespace cp=mgscoop::sleep_partner_carry_proof;namespace st=mgscoop::sleep_partner_test;namespace sl=mgscoop::sleep_partner_lease;namespace hk=mgscoop::partner_input_hook;
unsigned good=0,bad=0;void Check(bool ok,const char* why){if(ok)++good;else{++bad;std::printf("FAIL %s\n",why);}}
cp::Retained Held(){cp::Retained r{};r.configured=r.enabled=r.worker=r.lifetime=true;r.image=0x140000000;r.modelEntry=0xf1000;r.nonce=7;
 r.owned={true,true,false,false,true,30050,17,91,9,2,900};r.owned.peerSequence=9;r.owned.localRole=1;r.owned.peerCause=2;r.owned.peerDown=r.owned.peerReady=true;r.current=r.owned;r.current.poll=1000;r.current.peerSequence=10;
 sl::Sample a{};a.owner={0x10000,0x20000,0xc004c,0x406a8,0x51000,0x81000,0x61000,0x71000,0x91000,0xe1000,0x121000,30050,17,88,91,9,2,107};a.stun=a.maximumStun=3300;
 r.lease=sl::Commit(a,1000,true,true);r.lease.phase=sl::Phase::active;r.lease.nativeEntered=true;return r;}
hk::NativePoseJobContext Job(){hk::NativePoseJobContext j{};const auto o=Held().lease.owner;j.player=o.player;j.go=j.player-0x20;j.block=o.block;j.state=o.ps;j.modelCS=o.cs;j.modelEntry=Held().modelEntry;j.generation=91;j.sceneEpoch=9;j.thread=88;j.sequence=13;j.modelReady=j.selectedBodyOwned=true;j.selectedBody=1;j.pp=0x150000;j.source=0x170000;j.windowPPVT=0x160000;j.birthSerial=107;return j;}
qc::Context Context(){const auto r=Held();const auto o=r.lease.owner;st::CarrierSliceSnapshot slice{};slice.image=r.image;slice.player=o.player;slice.go=o.player-0x20;slice.data=o.data;slice.block1=o.block;slice.state1=o.ps;slice.modelCS1=o.cs;slice.modelEntry1=r.modelEntry;slice.armGeneration=91;slice.armEpoch=9;slice.thread=88;slice.sequence=11;slice.birth1=107;slice.carrierBirth0=106;slice.sliceNonce=12;slice.carrySerial=14;slice.sleepHeld=true;
 qc::Context c{};c.image=r.image;c.carrierRecord=0x2b0000;c.recipientRecord=0x2c0000;c.job=Job();c.queue.job=c.job;c.queue.root=o.data-0x20;c.queue.slab=0x280000;c.queue.queued=0x280040;c.queue.snapshot=0x30000;c.queue.bytes=0x200;c.queue.carrySerial=14;
 auto& x=c.submission;cp::Build(r,slice,1010,x.submittedSleep);x.image=r.image;x.player=o.player;x.go=slice.go;x.data=o.data;x.block1=o.block;x.state1=o.ps;x.modelCS1=o.cs;x.modelEntry1=r.modelEntry;x.armGeneration=91;x.armEpoch=9;x.thread=99;x.sequence=12;x.handlerRva=0x11268b0;x.birth1=107;x.carrierBirth0=106;x.carrySerial=14;x.sharedSerial=13;x.submitNonce=15;x.submittedAt=1050;x.sleepHeld=x.carryHeld=true;
 c.intentNonce=18;c.intentAt=1080;c.birth0=106;c.birth1=107;c.priorSequence=255;c.message=0x310000;c.result=0x320000;c.service=r.image+0x2c38008;c.current=c.next=1;c.nativeAction=0x84;return c;}
sl::Sample Actor(){sl::Sample a{};a.owner=Held().lease.owner;a.health=a.maximumHealth=6600;a.stun=100;a.maximumStun=3300;a.mode=5;a.lifeState=2;a.nodeActive=true;a.action=0x19;a.qAction=0x84;a.stance=1;a.cause=2;a.graphCurrent=a.graphSleepCandidate=true;a.graphHash=0xc31867300b57ull;return a;}
sl::Scope Scope(){sl::Scope s{};s.exactRetailGate=s.sleepCodeGate=s.seedCodeGate=s.workerActive=s.threadAdmitted=s.copiedSave=s.coherent=s.admitted=s.pairedOffline=s.selectedBodyOwned=s.ownedOddWindow=s.participantsLive=s.controlsZero=true;s.thread=88;s.sequenceBefore=s.sequenceAfter=13;s.now=1100;s.poll=1000;return s;}
int main(){const auto held=Held();const auto context=Context();
 Check(qc::ContextCurrent(held,context,1100)&&context.job.thread!=context.submission.thread,"current selected1 op1 writer is distinct from original shared thread/even receipt");
 Check(qc::Physical(held,context,Scope(),Actor()),"real neutral current scope/native sleep phase1 qualifies current queued authority");
 auto pickup=Actor();pickup.graphSleepCandidate=false;pickup.graphHash=0x2bd47bc9b95eull;pickup.action=0x44;
 Check(qc::Physical(held,context,Scope(),pickup),"natural phase1 carried graph and new coherent PS action do not project old resting graph/PS19");
 auto phase2=context;phase2.current=phase2.next=2;phase2.nativeAction=0x2a;pickup.qAction=0x2a;
 Check(qc::ContextCurrent(held,phase2,1100)&&qc::Physical(held,phase2,Scope(),pickup),"exact native phase2/action2A continues retained canonical sleep under current typed carried graph");
 auto phase3=phase2;phase3.current=phase3.next=3;
 Check(qc::ContextCurrent(held,phase3,1100)&&qc::Physical(held,phase3,Scope(),pickup),"exact phase3/action2A is distinct from unqualified attachment permission");
 for(unsigned k=0;k<53;++k){auto r=held;auto c=context;
  if(k==0)c.job.sequence=12;if(k==1)c.job.selectedBody=0;if(k==2)c.job.selectedBodyOwned=false;if(k==3)c.job.modelReady=false;
  if(k==4)c.job.thread=99;if(k==5)c.job.birthSerial++;if(k==6)c.job.generation++;if(k==7)c.job.source=0;if(k==8)c.queue.job.pp++;
  if(k==9)c.queue.carrySerial++;if(k==10)c.queue.root++;if(k==11)c.queue.slab++;if(k==12)c.queue.queued=c.queue.slab+c.queue.bytes;if(k==13)c.queue.snapshot=0;
  if(k==14)c.intentNonce=0;if(k==15)c.intentAt=1101;if(k==16)c.intentAt=1049;if(k==17)c.intentAt=1801;if(k==18)c.message=0;
  if(k==19)c.birth0=c.birth1;if(k==20)c.birth0++;if(k==21)c.birth1++;if(k==22)c.submission.sharedSerial=0;if(k==23)c.submission.submitNonce=0;
  if(k==24)c.submission.handlerRva=0;if(k==25)c.submission.sequence=11;if(k==26)c.submission.sleepHeld=false;if(k==27)c.submission.carryHeld=false;
  if(k==28)c.submission.submittedSleep.leaseNonce++;if(k==29)c.submission.submittedSleep.issuedAt=1051;if(k==30)c.submission.submittedSleep.peerCause=1;if(k==31)c.submission.submittedSleep.seedPeerSequence++;
  if(k==32)c.submission.submittedSleep.thread=99;if(k==33)c.submission.submittedSleep.life+=4;if(k==34)r.current.asleep=false;if(k==35)r.current.peerReady=false;
  if(k==36)r.current.poll=849;if(k==37)r.current.localRole=2;if(k==38)r.lease.wakeAuthorized=true;if(k==39)r.lease.recoveryAt=1050;if(k==40)r.faulted=true;if(k==41)r.current.peerGeneration++;
  if(k==42)c.current=0;if(k==43)c.current=c.next=4;if(k==44)c.next=2;if(k==45)c.nativeAction=0x2a;if(k==46)c.result=0;if(k==47)c.service=0;
  if(k==48)c.job.held=1;if(k==49)c.job.press=1;if(k==50)c.job.release=1;if(k==51)c.job.magnitude=1;if(k==52)r.lease.nativeEntered=false;
  Check(!qc::ContextCurrent(r,c,1100),"current natural owned job/queue/phase/action and original sealed canonical cycle reject mismatch");}
 auto ongoing=held;ongoing.current.poll=5000;ongoing.current.peerSequence=12;auto ongoingScope=Scope();ongoingScope.now=ongoingScope.poll=5000;
 Check(qc::ContextCurrent(ongoing,phase2,5000)&&qc::Physical(ongoing,phase2,ongoingScope,pickup)&&5000-context.intentAt>750,"committed native queued lifecycle does not renew or reuse old750ms handoff deadline; fresh physical/canonical authority is required");
 Check(context.intentAt==1080&&context.submission.submittedAt==1050&&context.submission.submittedSleep.issuedAt==1010,"ongoing queries retain exact original history times");
 auto history=context;history.intentAt=1801;ongoing.current.poll=1801;Check(!qc::ContextCurrent(ongoing,history,1801),"original submission to successful op0 history remains bounded750ms");
 auto ack=context;ack.acknowledged=true;Check(qc::ContextCurrent(held,ack,1100)&&!qc::SameContext(context,ack),"actual native reply acknowledgement may change across calls and needs new proof within current phase");
 for(unsigned k=0;k<22;++k){auto r=held;auto c=context;auto a=Actor();auto sc=Scope();
  if(k==0)sc.controlsZero=false;if(k==1)sc.ownedOddWindow=false;if(k==2)sc.batchBefore=true;if(k==3)sc.batchAfter=true;if(k==4)sc.sequenceAfter=15;
  if(k==5)sc.poll=349;if(k==6)a.lifeState=0;if(k==7)a.mode=6;if(k==8)a.requested=6;if(k==9)a.nodeActive=false;if(k==10)a.qAction=0x85;
  if(k==11)a.stance=0;if(k==12)a.stance=4;if(k==13)a.graphCurrent=false;if(k==14)a.maximumStun=3000;if(k==15)a.owner.life+=4;if(k==16)a.health=0;
  if(k==17)r.current.asleep=false;if(k==18)r.wakePending=true;if(k==19)c.nativeAction=0x2a;if(k==20)c.current=c.next=2;if(k==21)r.clearPending=true;
  Check(!qc::Physical(r,c,sc,a),"queued authority refuses wake/healthy/replaced/malformed scope and mismatched current native phase/action/max");}
 st::QueuedCarrySleepProof proof{},current{};proof.submitted=current.submitted=context.submission.submittedSleep;proof.job=current.job=context.job;proof.queue=current.queue=context.queue;
 proof.carrierRecord=current.carrierRecord=context.carrierRecord;proof.recipientRecord=current.recipientRecord=context.recipientRecord;proof.message=current.message=context.message;
 proof.result=current.result=context.result;proof.service=current.service=context.service;proof.currentPeerSequence=current.currentPeerSequence=10;
 proof.submitNonce=current.submitNonce=15;proof.submittedAt=current.submittedAt=1050;proof.intentNonce=current.intentNonce=18;proof.intentAt=current.intentAt=1080;
 proof.birth0=current.birth0=106;proof.birth1=current.birth1=107;proof.currentAcceptedAt=current.currentAcceptedAt=1000;proof.priorSequence=current.priorSequence=255;
 proof.current=current.current=1;proof.next=current.next=1;proof.nativeAction=current.nativeAction=0x84;current.observedAt=1200;
 Check(qc::SameProof(proof,current),"fresh observedAt does not renew sealed issuedAt/intent/submission");
 for(unsigned k=0;k<13;++k){auto changed=current;
  if(k==0)changed.acknowledged=true;if(k==1)changed.intentNonce++;if(k==2)changed.priorSequence++;if(k==3)changed.queue.queued+=16;
  if(k==4)changed.job.source++;if(k==5)changed.currentPeerSequence++;if(k==6)changed.submitted.issuedAt++;if(k==7)changed.currentAcceptedAt++;
  if(k==8)changed.message++;if(k==9)changed.current++;if(k==10)changed.next++;if(k==11)changed.nativeAction=0x2a;if(k==12)changed.job.magnitude=-0.f;
  Check(!qc::SameProof(proof,changed),"late actual queued/job/result/phase/ack/sealed timestamp drift cannot renew old proof");}
 unsigned delegated=0;auto read=[&](st::QueuedCarrySleepProof& out)noexcept{out=current;return true;};
 auto accepted=[&](const st::QueuedCarrySleepProof& p)noexcept{++delegated;return qc::SameProof(p,current);};
 Check(qc::Validate(proof.submitted,read,accepted)&&delegated==1,"actual production adapter first performs full sealed equality then fresh current natural op1 requery");
 current.acknowledged=true;Check(qc::Validate(proof.submitted,read,accepted)&&delegated==2,"post-reply validator obtains a new acknowledged proof; earlier proof is not reused");
 auto expected=proof.submitted;expected.issuedAt++;Check(!qc::Validate(expected,read,accepted)&&delegated==2,"renewed original issuedAt refuses before current delegation");
 auto missing=[](st::QueuedCarrySleepProof&)noexcept{return false;};Check(!qc::Validate(proof.submitted,missing,accepted),"missing real op1 provider cannot use cached/old deferred status");
 auto lost=[](const st::QueuedCarrySleepProof&)noexcept{return false;};Check(!qc::Validate(proof.submitted,read,lost),"native reply/current queue loss refuses independent current delegation");
 std::printf("sleep_queued_carry_tests: %u passed; %u failed\n",good,bad);return bad?1:0;
}
