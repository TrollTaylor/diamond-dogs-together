#include "sleep_worker_controls.hpp"
#include <cstdio>
#include <limits>
#include <initializer_list>
namespace sc=mgscoop::sleep_worker_controls;
namespace sl=mgscoop::sleep_body0_lease;
unsigned checks=0,failed=0;
void Check(bool value,const char* label){++checks;if(!value){++failed;std::printf("FAIL %s\n",label);}}
sc::Masks Full(){sc::Masks m{};m.table[0][1]={sc::kDownedName,0xffffffffu};m.table[2][6]={sc::kDownedName,3};m.aggregate[0]=0xffffffffu;m.aggregate[2]=3;return m;}
sl::Sample Sample(){sl::Sample a{};a.owner={0x1000,0x2000,0x3000,0x4000,0x5000,0x6000,0x7000,0x8000,0x9000,0xa000,0xb000,30010,7,3,2};a.health=100;a.maximumHealth=1000;a.stun=500;a.maximumStun=3000;a.cause=2;a.action=0x44;a.qAction=1;a.stance=1;a.graphCurrent=true;return a;}
sl::Scope Scope(){sl::Scope s{};s.exactRetailGate=s.sleepCodeGate=s.seedCodeGate=s.workerActive=s.threadAdmitted=s.copiedSave=s.coherent=true;s.thread=3;s.sequenceBefore=s.sequenceAfter=8;s.now=1000;s.poll=990;return s;}
int main(){
 const auto full=Full();Check(sc::NamedFull(full),"exact names across nonzero indices");
 for(unsigned t=0;t<3;++t){auto m=full;m.aggregate[t]^=0x100;Check(!sc::NamedFull(m),"aggregate mismatch");}
 {auto m=full;m.table[0][1].name=123;Check(!sc::NamedFull(m),"unrelated full pause mask");}
 {auto m=full;m.table[2][6].name=123;Check(!sc::NamedFull(m),"unrelated trigger mask");}
 {auto m=full;m.table[0][7]=m.table[0][1];Check(!sc::NamedFull(m),"duplicate own name");}
 for(unsigned t=0;t<3;++t){auto m=full;m.table[t][0]={99,0};m.table[t][7]={99,0};Check(!sc::NamedFull(m),"duplicate foreign name");}
 {auto m=full;m.table[1][2]={sc::kDownedName,1};m.aggregate[1]=1;Check(!sc::NamedFull(m),"own stick lock changes natural recovery");}
 {auto m=full;m.table[1][2]={sc::kDownedName,0};Check(sc::NamedFull(m),"empty own stick entry permitted");}
 {auto m=full;m.table[1][3]={22,2};m.aggregate[1]=2;Check(sc::NamedFull(m),"coherent independent retail right-stick mask");}
 for(unsigned bit=0;bit<32;++bit){auto m=full;m.table[0][1].bits&=~(1u<<bit);m.aggregate[0]=m.table[0][1].bits;Check(!sc::NamedFull(m),"own buttons require every bit");}
 for(unsigned mask=0;mask<8;++mask){auto m=full;m.table[2][6].bits=mask;m.aggregate[2]=mask;Check(sc::NamedFull(m)==(mask==3),"exact trigger bits");}
 sc::Receipt r{1,2,4,8,16,7,3,8,1000,1,true};Check(sc::FreshZero(r,r,1000),"fresh natural zero receipt");Check(sc::FreshZero(r,r,1750),"boundary zero lease");Check(!sc::FreshZero(r,r,1751),"expired zero lease");Check(!sc::FreshZero(r,r,999),"reversed zero clock");
 for(unsigned k=0;k<5;++k){auto current=r;std::uintptr_t* ptrs[]={&current.player,&current.data,&current.life,&current.pad,&current.source};++*ptrs[k];Check(!sc::FreshZero(r,current,1000),"current typed pointer changed");}
 {auto q=r;++q.generation;Check(!sc::FreshZero(r,q,1000),"stale cycle receipt");}
 {auto q=r;++q.thread;Check(!sc::FreshZero(r,q,1000),"foreign completion thread");}
 {auto q=r;q.sequence+=2;Check(!sc::FreshZero(r,q,1000),"intervening partner window");}
 {auto q=r;q.sequence|=1;Check(!sc::FreshZero(q,q,1000),"receipt during owned window");}
 {auto q=r;q.serial=0;Check(!sc::FreshZero(q,q,1000),"receipt not generated");}
 {auto q=r;q.zero=false;Check(!sc::FreshZero(q,q,1000),"nonzero native output");}
 auto a=Sample();const auto s=Scope();sl::Lease l{};l.owner=a.owner;l.maximumStunBits=sl::Bits(a.maximumStun);l.phase=sl::Phase::retired;l.wakeAuthorized=true;l.lastSeen=980;
 Check(sc::ReleaseSample(l,s,a),"actual retirement after masks removed");
 {auto q=s;q.namedButtonsLocked=q.namedTriggersLocked=q.analogCapQualified=true;Check(sc::ReleaseSample(l,q,a),"preflight retirement before masks removed");}
 {auto q=l;q.wakeAuthorized=false;Check(!sc::ReleaseSample(q,s,a),"no accepted grant");}
 for(auto phase:{sl::Phase::empty,sl::Phase::seeded,sl::Phase::active,sl::Phase::wakePending,sl::Phase::quarantined}){auto q=l;q.phase=phase;Check(!sc::ReleaseSample(q,s,a),"held lease cannot release");}
 for(unsigned k=0;k<11;++k){auto q=a;std::uintptr_t* ptrs[]={&q.owner.player,&q.owner.data,&q.owner.life,&q.owner.block,&q.owner.ps,&q.owner.plugin,&q.owner.node,&q.owner.records,&q.owner.bitmap,&q.owner.cs,&q.owner.graph};++*ptrs[k];Check(!sc::ReleaseSample(l,s,q),"retired typed owner changed");}
 for(unsigned k=0;k<4;++k){auto q=a;if(k==0)++q.owner.mission;if(k==1)++q.owner.generation;if(k==2)++q.owner.ownerThread;if(k==3)++q.owner.epoch;Check(!sc::ReleaseSample(l,s,q),"retired logical owner changed");}
 for(unsigned k=0;k<7;++k){auto q=s;bool* flags[]={&q.exactRetailGate,&q.sleepCodeGate,&q.seedCodeGate,&q.workerActive,&q.threadAdmitted,&q.copiedSave,&q.coherent};*flags[k]=false;Check(!sc::ReleaseSample(l,q,a),"retirement gate refused");}
 {auto q=s;q.thread=4;Check(!sc::ReleaseSample(l,q,a),"foreign retirement thread");}
 {auto q=s;q.batchBefore=true;Check(!sc::ReleaseSample(l,q,a),"retirement inside batch before");}
 {auto q=s;q.batchAfter=true;Check(!sc::ReleaseSample(l,q,a),"retirement inside batch after");}
 {auto q=s;q.sequenceBefore=q.sequenceAfter=9;Check(!sc::ReleaseSample(l,q,a),"odd retired window");}
 {auto q=s;q.sequenceAfter+=2;Check(!sc::ReleaseSample(l,q,a),"window changed around retirement");}
 {auto q=s;q.poll=249;Check(!sc::ReleaseSample(l,q,a),"stale retired poll");}
 {auto q=s;q.now=900;Check(!sc::ReleaseSample(l,q,a),"clock behind retirement receipt");}
 for(unsigned mode=4;mode<=6;++mode){auto q=a;q.mode=static_cast<std::uint8_t>(mode);Check(!sc::ReleaseSample(l,s,q),"native sleep reentered after mask release");}
 {auto q=a;q.requested=6;Check(!sc::ReleaseSample(l,s,q),"late requested mode");}
 {auto q=a;q.nodeActive=true;Check(!sc::ReleaseSample(l,s,q),"late active node");}
 {auto q=a;q.lifeState=2;Check(!sc::ReleaseSample(l,s,q),"late sleep life state");}
 {auto q=a;q.action=0x19;Check(!sc::ReleaseSample(l,s,q),"late sleep action");}
 for(unsigned action=0x83;action<=0x85;++action){auto q=a;q.qAction=static_cast<std::uint16_t>(action);Check(!sc::ReleaseSample(l,s,q),"late natural pose action");}
 {auto q=a;q.graphCurrent=false;Check(!sc::ReleaseSample(l,s,q),"current graph invalidated");}
 {auto q=a;q.graphSleepCandidate=true;Check(!sc::ReleaseSample(l,s,q),"sleep hash remains current");}
 {auto q=a;q.stun=std::numeric_limits<float>::quiet_NaN();Check(!sc::ReleaseSample(l,s,q),"invalid stun scalar");}
 {auto q=a;q.maximumStun=2999;Check(!sc::ReleaseSample(l,s,q),"stun record type changed");}
 {auto q=a;q.health=0;Check(!sc::ReleaseSample(l,s,q),"dead retirement owner");}
 {auto q=a;q.health=1001;Check(!sc::ReleaseSample(l,s,q),"incoherent health mirror");}
 {auto q=a;q.cause=1;Check(!sc::ReleaseSample(l,s,q),"injury cannot release sleep lease");}
 // Revalidate the entire partial-seed admission rather than nonsleep retirement.
 {auto q=a;q.stun=0;auto scope=s;scope.namedButtonsLocked=scope.namedTriggersLocked=scope.analogCapQualified=true;
  auto partial=sl::Commit(a,1000,true,false);Check(sl::Repair(scope,q,partial).valid,"unchanged partial seed can proceed");
  for(unsigned stance=2;stance<=3;++stance){auto changed=q;changed.stance=static_cast<std::uint8_t>(stance);Check(sl::RetiredSample(changed)&&!sl::Repair(scope,changed,partial).valid,"stance drift after stun CAS refuses state CAS");}
  {auto changed=q;changed.action=0x45;Check(sl::RetiredSample(changed)&&!sl::Repair(scope,changed,partial).valid,"PS drift after stun CAS refuses state CAS");}
  {auto changed=q;changed.qAction=6;Check(sl::RetiredSample(changed)&&!sl::Repair(scope,changed,partial).valid,"Q drift after stun CAS refuses state CAS");}
  {auto changed=scope;changed.analogCapQualified=false;Check(!sl::Repair(changed,q,partial).valid,"new partial zero receipt required");}
 }
 // The same watchdog applies when the wrapper returns before Observe.
 {sl::Lease q{};Check(!sc::PendingExpired(q,false,1000,7001),"no prepared or held lease");Check(!sc::PendingExpired(q,true,1000,7000),"prepare deadline boundary");Check(sc::PendingExpired(q,true,1000,7001),"standing prepare early-read timeout");Check(!sc::PendingExpired(q,true,1000,999),"reversed prepare time holds");
  q.phase=sl::Phase::seeded;q.seededAt=q.lastSeen=1000;Check(sc::PendingExpired(q,true,1000,7001),"final read never reached acquisition watchdog");
  q.phase=sl::Phase::active;q.nativeEntered=true;q.lastSeen=7000;Check(!sc::PendingExpired(q,true,1000,7001),"fresh active read does not expire old seed");Check(sc::PendingExpired(q,true,1000,13001),"continued early read failure loses observed lease");
  q.phase=sl::Phase::wakePending;q.wakeAt=2000;q.lastSeen=8000;Check(sc::PendingExpired(q,true,1000,8001),"repeated wake CAS failure reaches deadline");
  q.phase=sl::Phase::retired;q.lastSeen=8000;q.wakeAt=0;Check(!sc::PendingExpired(q,true,1000,14000),"mask-release read grace boundary");Check(sc::PendingExpired(q,true,1000,14001),"mask-release repeated read failure bounded");
 }
 {auto exact=s;exact.analogCapQualified=exact.namedButtonsLocked=exact.namedTriggersLocked=true;Check(sc::CasWindow(exact,3,3,8,false,true),"exact immediate pre-CAS window");Check(!sc::CasWindow(exact,3,4,8,false,true),"foreign pre-CAS thread");Check(!sc::CasWindow(exact,3,3,10,false,true),"window completed immediately before CAS");Check(!sc::CasWindow(exact,3,3,9,false,true),"window entered immediately before CAS");Check(!sc::CasWindow(exact,3,3,8,true,true),"deferred batch immediately before CAS");Check(!sc::CasWindow(exact,3,3,8,false,false),"worker lease lost immediately before CAS");auto q=exact;q.sequenceBefore=6;Check(!sc::CasWindow(q,3,3,8,false,true),"sample itself crossed a window");q=exact;q.thread=4;Check(!sc::CasWindow(q,3,3,8,false,true),"sample belongs to foreign owner");}

 for(float maximum:{1.f,1500.f,3300.f,65535.f}){auto configured=Sample();configured.maximumStun=maximum;configured.stun=maximum;auto retired=sl::RetireEmpty(configured,980);Check(sc::ReleaseSample(retired,s,configured),"configured maximum exact retired release");auto drift=configured;drift.maximumStun=std::nextafter(maximum,maximum+1.f);Check(!sc::ReleaseSample(retired,s,drift),"one-bit configured maximum drift refuses release");}
 for(float maximum:{0.f,-1.f,65536.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){auto invalid=a;invalid.stun=0;invalid.maximumStun=maximum;Check(!sc::ReleaseSample(l,s,invalid),"invalid configured maximum release refuses");}
 {auto changed=a;changed.maximumStun=3300;Check(!sc::ReleaseSample(l,s,changed),"otherwise valid reconfigured maximum cannot release old lease");}

 // Intervening complete partner windows invalidate mutation cap, not a paired
 // current-zero read-only observation of the same owned native sleep cycle.
 {auto prior=r;auto sampleOwner=Sample().owner;prior.player=sampleOwner.player;prior.data=sampleOwner.data;prior.life=sampleOwner.life;auto current=prior;current.sequence=12;sc::RetentionInputs in{};in.magnitudeBefore=in.magnitudeAfter=0;in.namedMasksBefore=in.namedMasksAfter=true;in.sequenceBefore=in.sequenceAfter=12;in.now=1100;
  Check(!sc::FreshZero(prior,current,in.now),"intervening window still refuses exact mutation cap");auto observation=sc::RetainedObservation(prior,current,in);Check(observation.retainedNaturalZero,"paired current zero retains recent same-owner observation");
  auto scope=Scope();scope.now=scope.poll=1100;scope.sequenceBefore=scope.sequenceAfter=12;scope.namedButtonsLocked=scope.namedTriggersLocked=true;scope.analogCapQualified=false;
  auto actor=Sample();actor.maximumStun=3300;actor.stun=3300;scope.admitted=scope.pairedOffline=true;Check(!sl::Seed(scope,actor,sl::Lease{},true).valid,"read-only observation cannot seed");
  auto lease=sl::Commit(actor,1000,true,true);auto sleeping=actor;sleeping.stun=0;sleeping.lifeState=2;sleeping.mode=5;sleeping.action=0x19;sleeping.qAction=0x84;sleeping.nodeActive=true;sleeping.graphSleepCandidate=true;
  auto receipt=sl::Observe(lease,scope,sleeping,observation);Check(receipt.holdControls&&receipt.suppressProne&&lease.nativeEntered&&lease.lastSeen==1100&&!receipt.labAbortRequired,"read-only native rest updates lease freshness and retains restrictions");
  sl::RequestWake(lease,actor.owner.generation,1100);Check(!sl::Wake(scope,sleeping,lease).valid,"read-only observation cannot wake CAS");Check(!sc::CasWindow(scope,3,3,12,false,true),"immediate CAS rejects read-only scope");
  auto partial=sl::Commit(actor,1000,true,false);auto zero=actor;zero.stun=0;Check(!sl::Repair(scope,zero,partial).valid,"read-only observation cannot repair CAS");receipt=sl::Observe(partial,scope,zero,observation);Check(receipt.holdControls&&partial.lastSeen==1000,"read-only observation cannot extend partial lease");
  for(unsigned k=0;k<7;++k){auto changed=observation;if(k==0)++changed.player;if(k==1)++changed.data;if(k==2)++changed.life;if(k==3)++changed.generation;if(k==4)++changed.thread;if(k==5)changed.sequence+=2;if(k==6)++changed.at;auto held=sl::Commit(actor,1000,true,true);auto boundReceipt=sl::Observe(held,scope,sleeping,changed);Check(boundReceipt.holdControls&&held.lastSeen==1000,"read-only proof binds current Sample/Scope exactly");}
  auto noProof=sl::Commit(actor,1000,true,true);receipt=sl::Observe(noProof,scope,sleeping);Check(receipt.holdControls&&noProof.lastSeen==1000,"ordinary Observe retains exact cap requirement");
  sleeping=actor;scope.now=scope.poll=1200;in.now=1200;observation=sc::RetainedObservation(prior,current,in);sl::Observe(lease,scope,sleeping,observation);scope.now=scope.poll=1300;in.now=1300;observation=sc::RetainedObservation(prior,current,in);receipt=sl::Observe(lease,scope,sleeping,observation);Check(lease.phase==sl::Phase::retired&&receipt.retired&&receipt.holdControls,"read-only retired samples cannot authorize control release");
  scope.now=scope.poll=1400;in.now=1400;observation=sc::RetainedObservation(prior,current,in);receipt=sl::Observe(lease,scope,sleeping,observation);Check(receipt.holdControls&&receipt.retired,"late authorized retired observation still holds controls");auto drift=sleeping;drift.mode=5;drift.action=0x19;drift.qAction=0x84;drift.nodeActive=true;drift.lifeState=2;receipt=sl::Observe(lease,scope,drift,observation);Check(receipt.holdControls&&receipt.labAbortRequired&&!receipt.retired,"read-only receipt cannot conceal retired native re-entry");Check(!sc::ReleaseSample(lease,scope,drift),"separate actual release still refuses current native state");
  in.now=1100;for(unsigned k=0;k<12;++k){auto invalid=in;if(k==0)invalid.magnitudeBefore=0.01f;if(k==1)invalid.magnitudeAfter=0.01f;if(k==2)invalid.magnitudeBefore=-0.f;if(k==3)invalid.magnitudeAfter=std::numeric_limits<float>::quiet_NaN();if(k==4)invalid.namedMasksBefore=false;if(k==5)invalid.namedMasksAfter=false;if(k==6)invalid.sequenceBefore=13;if(k==7)invalid.sequenceAfter=14;if(k==8)invalid.batchBefore=true;if(k==9)invalid.batchAfter=true;if(k==10)invalid.now=999;if(k==11)invalid.now=1751;Check(!sc::RetainedObservation(prior,current,invalid).retainedNaturalZero,"actual magnitude/masks/window/freshness retention refuses");}
  for(unsigned k=0;k<10;++k){auto changed=prior;if(k==0)changed.zero=false;if(k==1)changed.serial=0;if(k==2)changed.at=0;if(k==3)changed.sequence=9;if(k==4)++changed.player;if(k==5)++changed.data;if(k==6)++changed.life;if(k==7)++changed.pad;if(k==8)++changed.generation;if(k==9)++changed.thread;Check(!sc::RetainedObservation(changed,current,in).retainedNaturalZero,"natural receipt owner/generation/schema refuses");}
  {auto changed=current;++changed.source;Check(!sc::RetainedObservation(prior,changed,in).retainedNaturalZero,"pad source replacement refuses retention");}
  {auto changed=current;changed.sequence=10;Check(!sc::RetainedObservation(prior,changed,in).retainedNaturalZero,"current sample sequence must match actual pair");}
  {auto changed=prior;changed.sequence=14;Check(!sc::RetainedObservation(changed,current,in).retainedNaturalZero,"future receipt sequence cannot retain older observation");}
 }
 // Frequent completed input/job ordering must not starve read-only lastSeen.
 {auto actor=Sample();actor.stun=actor.maximumStun=3300;auto lease=sl::Commit(actor,1000,true,true);actor.stun=0;actor.lifeState=2;actor.mode=5;actor.action=0x19;actor.qAction=0x84;actor.nodeActive=true;actor.graphSleepCandidate=true;
  for(unsigned k=0;k<80;++k){auto prior=r;prior.player=actor.owner.player;prior.data=actor.owner.data;prior.life=actor.owner.life;prior.at=1000+k*100;prior.sequence=8+k*4;auto current=prior;current.sequence+=2;sc::RetentionInputs in{};in.magnitudeBefore=in.magnitudeAfter=0;in.namedMasksBefore=in.namedMasksAfter=true;in.sequenceBefore=in.sequenceAfter=current.sequence;in.now=prior.at+10;
   auto observation=sc::RetainedObservation(prior,current,in);auto scope=Scope();scope.now=scope.poll=in.now;scope.sequenceBefore=scope.sequenceAfter=current.sequence;scope.namedButtonsLocked=scope.namedTriggersLocked=true;
   const auto receipt=sl::Observe(lease,scope,actor,observation);Check(!sc::FreshZero(prior,current,in.now)&&observation.retainedNaturalZero&&lease.lastSeen==in.now&&receipt.holdControls&&!receipt.labAbortRequired,"natural zero plus intervening window sustains read-only lease");
  }
  auto scope=Scope();scope.now=scope.poll=lease.lastSeen+sl::kRetireMs+1;scope.namedButtonsLocked=scope.namedTriggersLocked=true;const auto receipt=sl::Observe(lease,scope,actor);Check(receipt.holdControls&&receipt.labAbortRequired,"lost actual zero proof still has bounded cleanup");
 }
 std::printf("sleep worker controls: %u checks / %u failures\n",checks,failed);return failed?1:0;
}
