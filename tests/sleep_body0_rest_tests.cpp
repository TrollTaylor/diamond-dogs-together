#include "sleep_parts_fixture.hpp"
#include "sleep_body0_rest.hpp"
#include <cstdio>
namespace br=mgscoop::sleep_body0_rest;namespace bl=mgscoop::sleep_body0_lease;
bl::Sample LocalRest(){bl::Sample a{};a.owner={0x10000,0x20000,0xc0000,0x40000,0x50000,0x80000,0x60000,0x70000,0x90000,0xe0000,0x120000,30050,17,88,2};
 a.health=a.maximumHealth=6600;a.stun=200;a.maximumStun=3300;a.lifeState=2;a.mode=5;a.nodeActive=true;a.action=0x19;a.qAction=0x84;a.stance=1;a.cause=2;a.graphCurrent=a.graphSleepCandidate=true;return a;}
bl::Scope LocalScope(){bl::Scope s{};s.exactRetailGate=s.sleepCodeGate=s.seedCodeGate=s.workerActive=s.threadAdmitted=s.copiedSave=s.coherent=s.admitted=s.pairedOffline=s.namedButtonsLocked=s.namedTriggersLocked=s.analogCapQualified=true;
 s.thread=88;s.sequenceBefore=s.sequenceAfter=10;s.now=1100;s.poll=1000;return s;}
bl::Lease LocalLease(){auto l=bl::Commit(LocalRest(),1000,true,true);l.phase=bl::Phase::active;l.nativeEntered=true;return l;}
br::Descriptor LocalDescriptor(){return {0x50000,0x270000,0,0x80};}
int main(){
 auto actor=LocalRest();auto scope=LocalScope();auto held=LocalLease();constexpr std::uint64_t hash=0xc31867300b57ull;
 auto p=br::Maintain(scope,actor,held,hash,LocalDescriptor(),true,true,false,false);
 Check(p.valid&&p.count==1&&p.changes[0].offset==0x2c&&p.changes[0].expected==bl::Bits(200.f)&&!p.changes[0].desired,"local fresh even natural cap proposes exactly one conditional Stun2Czero");
 for(unsigned k=0;k<25;++k){auto a=actor;auto s=scope;auto l=held;auto d=LocalDescriptor();auto h=hash;bool tick=true,canonical=true,wake=false,blocked=false;
  if(k==0)tick=false;if(k==1)canonical=false;if(k==2)wake=true;if(k==3)blocked=true;if(k==4)s.admitted=false;if(k==5)s.pairedOffline=false;
  if(k==6)s.sequenceAfter=12;if(k==7)s.batchAfter=true;if(k==8)s.namedButtonsLocked=false;if(k==9)s.analogCapQualified=false;
  if(k==10)s.poll=349;if(k==11)l.nativeEntered=false;if(k==12)l.wakeAuthorized=true;if(k==13)l.wakeAt=1050;
  if(k==14)a.maximumStun=3000;if(k==15)a.owner.epoch++;if(k==16)a.lifeState=0;if(k==17)a.requested=6;
  if(k==18)a.stun=a.maximumStun;if(k==19)a.mode=6;if(k==20)h=0x40d8fda5ca06ull;if(k==21)d.base=1;
  if(k==22)d.flags|=1u<<18;if(k==23)a.stance=4;if(k==24)d.records=0;
  Check(!br::Maintain(s,a,l,h,d,tick,canonical,wake,blocked).valid,"only local current canonical rest/masks/cap/noCarry/noWake descriptor and native phase authorize scalar write");
 }
 auto weak=scope;weak.analogCapQualified=false;p=br::Maintain(weak,actor,held,hash,LocalDescriptor(),true,true,false,false);
 Check(!p.valid&&!p.count&&!std::strcmp(p.reason,"sleep_rest_body0_zero_pending"),"V10 retained observation cannot become write permission");
 auto zeroActor=actor;zeroActor.stun=0;p=br::Maintain(weak,zeroActor,held,hash,LocalDescriptor(),true,true,false,false);
 Check(p.valid&&!p.count,"already-zero under readonly receipt does not need scalar-write permission");
 Check(br::ObserveZero(false,true).valid&&!br::ObserveZero(false,true).clearPending,"retained natural zero preserves pending clock without renewing it");
 Check(br::ObserveZero(true,false).valid&&br::ObserveZero(true,false).clearPending,"strict exact-sequence zero can close a pending write requirement");
 Check(!br::ObserveZero(false,false).valid,"zero value without real strict or retained observation refuses");
 Check(!br::PendingDeadline(0,8000)&&br::PendingDeadline(1100,7101),"zero-only readonly observations start no timer while an existing nonzero deadline remains absolute");
 Check(!br::PendingDeadline(1100,7100)&&br::PendingDeadline(1100,7101)&&br::PendingDeadline(1100,1099),"pending strict-cap deadline absolute6s and backward clock refusal");
 {
  const auto started=held.seededAt;unsigned writes=0;bool ok=true;
  for(unsigned k=1;k<=300;++k){scope.now=scope.poll=1000+k*100;scope.sequenceBefore=scope.sequenceAfter=10+k*2;held.lastSeen=scope.now;actor.stun+=27.5f;
   const auto plan=br::Maintain(scope,actor,held,hash,LocalDescriptor(),true,true,false,false);if(!plan.valid||plan.count!=1){ok=false;break;}actor.stun=0;++writes;
  }
  Check(ok&&writes==300&&held.seededAt==started&&actor.lifeState==2&&actor.mode==5&&actor.health==6600,"local canonical30s maintenance outlasts native12s gain without changingHP/life/mode/seed time");
  Check(bl::RequestWake(held,held.owner.generation,scope.now)&&bl::Wake(scope,actor,held).valid,"natural owned wake plan remains state2to0 after maintenance");
 }
 {
  const auto a=LocalRest();auto b=a;b.stun=0;Check(br::SameSample(a,b,true)&&!br::SameSample(a,b,false),"postCAS coherence permits exactly new sampled stunzero");
  for(unsigned k=0;k<6;++k){b=a;b.stun=0;if(k==0)b.health-=1;if(k==1)b.lifeState=0;if(k==2)b.requested=6;if(k==3)b.owner.life+=4;if(k==4)b.maximumStun=3000;if(k==5)b.qAction=0x85;
   Check(!br::SameSample(a,b,true),"changed health/owner/max/phase/action cannot fabricate local postzero receipt");}
 }
 {
  auto memory=OwnedFixture();memory.Put<std::uint32_t>(0x10218,0);auto s=S();s.generation=s.ownedGeneration=17;
  mgscoop::sleep_graph_read::Observation observed{};const char* why=nullptr;
  memory.Put<std::uintptr_t>(0x50280,0x270000);memory.Put<std::uint32_t>(0x270014,0x80);br::Descriptor d{};
  Check(mgscoop::sleep_graph_read::ReadOnce(memory,s,observed,&why)&&br::ReadDescriptor(memory,s,observed.owner.body[0],d)&&d.base==0,"exact actual0 minusPSbase0 descriptor index0 proof");
  for(auto mutation:{Mutation{0x10218,1,4,"selected"},{0x10214,1,4,"local"},{0x50008,1,4,"base"},{0x50408,0x406a8,8,"owner"},{0x50280,0,8,"records"}}){auto changed=memory;Change(changed,mutation);
   Check(!br::ReadDescriptor(changed,s,observed.owner.body[0],d),"replacement/local/selected/base/descriptor ambiguity rejects local maintenance");}
 }
 std::printf("sleep_body0_rest_tests: %u passed; %u failed\n",good,bad);return bad?1:0;
}
