#include "sleep_parts_fixture.hpp"
#include "sleep_rest_maintenance.hpp"
#include <limits>
namespace rest=mgscoop::sleep_rest_maintenance;namespace sl=mgscoop::sleep_partner_lease;namespace ctl=mgscoop::sleep_partner_controls;
sl::Sample RestActor(){sl::Sample a{};a.owner={0x10000,0x20000,0xc004c,0x406a8,0x51000,0x81000,0x61000,0x71000,0x91000,0xe1000,0x121000,30050,17,88,91,9,2,107};a.health=a.maximumHealth=6600;a.stun=250;a.maximumStun=3300;a.lifeState=2;a.mode=5;a.nodeActive=true;a.action=0x19;a.qAction=0x84;a.stance=3;a.cause=2;a.graphCurrent=a.graphSleepCandidate=true;a.graphHash=0xc31867300b57ull;return a;}
sl::Scope RestScope(){sl::Scope s{};s.exactRetailGate=s.sleepCodeGate=s.seedCodeGate=s.workerActive=s.threadAdmitted=s.copiedSave=s.coherent=s.admitted=s.pairedOffline=s.selectedBodyOwned=s.ownedOddWindow=s.participantsLive=s.controlsZero=true;s.thread=88;s.sequenceBefore=s.sequenceAfter=11;s.now=1100;s.poll=1000;return s;}
mgscoop::sleep_partner_test::Target RestTarget(){auto target=mgscoop::sleep_partner_test::Target{true,true,false,false,true,30050,17,91,9,2,1000};target.peerSequence=9;target.localRole=1;target.peerCause=2;target.peerDown=target.peerReady=true;return target;}
sl::Lease RestLease(){auto l=sl::Commit(RestActor(),1000,true,true);l.nativeEntered=true;l.phase=sl::Phase::active;return l;}
rest::Descriptor RestDescriptor(){return {0x51000,0x270000,1,0x80};}
sl::Plan Plan(const sl::Sample& a,const sl::Lease& l,const rest::Descriptor& d=RestDescriptor()){return rest::MaintainRest(RestScope(),a,l,RestTarget(),RestTarget(),d,true,true,false,false);}
int main(){
 auto actor=RestActor();auto lease=RestLease();auto plan=Plan(actor,lease);
 Check(plan.valid&&plan.count==1&&plan.changes[0].offset==0x2c&&plan.changes[0].expected==sl::Bits(250.f)&&plan.changes[0].desired==sl::Bits(0.f),"fresh strict owned rest proposes only one sampled stun-to-zero CAS");
 actor.stun=0;plan=Plan(actor,lease);Check(plan.valid&&plan.count==0,"already-zero rest needs no write or repeated seed");actor=RestActor();
 for(auto hash:{0xc31867300b57ull,0xd7654fb0a3f2ull}){actor.graphHash=hash;Check(Plan(actor,lease).valid,"only both complete native rest-request branches admitted");}actor=RestActor();
 for(unsigned k=0;k<34;++k){
  auto current=actor;auto held=lease;auto scope=RestScope();auto target=RestTarget();auto ownedTarget=target;auto desc=RestDescriptor();
  bool before=true,enabled=true,wake=false,carry=false;
  if(k==0)before=false;if(k==1)enabled=false;if(k==2)wake=true;if(k==3)carry=true;
  if(k==4)scope.controlsZero=false;if(k==5)scope.sequenceBefore=scope.sequenceAfter=12;if(k==6)scope.sequenceAfter=13;
  if(k==7)scope.coherent=false;if(k==8)scope.participantsLive=false;if(k==9)scope.admitted=false;if(k==10)scope.pairedOffline=false;
  if(k==11)target.asleep=false;if(k==12)target.poll=849;if(k==13)target.demo=true;if(k==14)target.stopping=true;if(k==15)target.pairedReady=false;
  if(k==16)target.peerGeneration++;if(k==17)ownedTarget.worldEpoch++;if(k==18)held.phase=sl::Phase::wakePending;if(k==19)held.nativeEntered=false;
  if(k==20)held.wakeAuthorized=true;if(k==21)held.wakeAt=1050;if(k==22)held.recoveryAt=1050;if(k==23)held.lastSeen=349;
  if(k==24)current.owner.birthSerial++;if(k==25)current.maximumStun=3000;if(k==26)current.lifeState=0;if(k==27)current.requested=6;
  if(k==28)current.mode=6;if(k==29)current.graphHash=0x40d8fda5ca06ull;if(k==30)current.stun=current.maximumStun;
  if(k==31)desc.flags|=1u<<18;if(k==32)desc.ps+=8;if(k==33)desc.records=0;
  const auto refused=rest::MaintainRest(scope,current,held,target,ownedTarget,desc,before,enabled,wake,carry);
  Check(!refused.valid&&refused.count==0,"After/carry/grant/stale scope/target/lease/full stun/queued recovery/descriptor ambiguity never proposes a write");
 }
 for(unsigned k=0;k<8;++k){auto current=actor;if(k==0)current.nodeActive=false;if(k==1)current.action=0x44;if(k==2)current.qAction=0x85;
  if(k==3)current.cause=1;if(k==4)current.graphCurrent=false;if(k==5)current.graphSleepCandidate=false;if(k==6)current.health=0;if(k==7)current.stun=std::numeric_limits<float>::quiet_NaN();
  Check(!Plan(current,lease).valid,"native action/node/graph/cause/health/nonfinite drift refused");
 }
 {
  auto current=actor;plan=Plan(current,lease);auto liveStun=plan.changes[0].expected;
  const auto hp=sl::Bits(current.health),state=current.lifeState;const bool exchanged=liveStun==plan.changes[0].expected;if(exchanged)liveStun=plan.changes[0].desired;
  Check(exchanged&&liveStun==sl::Bits(0.f)&&hp==sl::Bits(current.health)&&state==current.lifeState,"one conditional float exchange does not touch HP/life state or native phase");
  liveStun=sl::Bits(251.f);const bool raced=liveStun==plan.changes[0].expected;if(raced)liveStun=plan.changes[0].desired;
  Check(!raced&&liveStun==sl::Bits(251.f),"new native float value refuses stale expected bits; no repair to old value");
  current.requested=6;Check(!Plan(current,lease).valid,"a later native wake request cannot be converted back to sleep");
  Check(sl::RequestWake(lease,lease.owner.generation,1200)&&!Plan(actor,lease).valid,"canonical wake transfers to existing wake planner without maintenance");
 }
 {
  auto memory=OwnedFixture();const auto scope=OS();og::Observation observed{};const char* why=nullptr;
  memory.Put<std::uintptr_t>(scope.ps+0x280,0x270000);memory.Put<std::uint32_t>(0x270014,0x80);
  Check(og::ReadOnce(memory,scope,observed,&why),"genuine typed graph fixture supplies target body before descriptor read");
  rest::Descriptor first{},last{};
  Check(rest::ReadDescriptor(memory,scope,observed.owner.body[1],first)&&first.base==1&&first.ps==scope.ps&&first.records==0x270000&&first.flags==0x80,"exact relative index0 PS280+14 flag read");
  memory.Put<std::uint32_t>(0x270014,0x40080);
  Check(rest::ReadDescriptor(memory,scope,observed.owner.body[1],last)&&!rest::Same(first,last),"instant recovery flag changes cannot borrow paired descriptor proof");
  for(auto mutation:{Mutation{0x10218,0,4,"selected"},{0x10214,1,4,"local"},{0x142a82c30,0x10008,8,"player"},{0x51008,0,4,"base"},{0x51408,0x40000,8,"owner"},{0x51280,0,8,"records"}}){
   auto changed=memory;Change(changed,mutation);rest::Descriptor snapshot{};
   Check(!rest::ReadDescriptor(changed,scope,observed.owner.body[1],snapshot),"actual selected body/local/global owner/base/descriptor replacement refuses typed witness");
  }
 }
 {
  auto sample=RestActor();auto held=RestLease();auto scope=RestScope();auto owned=RestTarget();auto current=owned;
  const auto seed=held.seededAt;const auto hp=sl::Bits(sample.health);unsigned writes=0;bool complete=true;
  // Native gain would reach3300 in12s. Fresh accepted canonical sleep holds
  // until30s; each receipt uses the current odd window and actual new float.
  for(unsigned k=1;k<=300;++k){scope.now=1000+k*100;scope.poll=scope.now;scope.sequenceBefore=scope.sequenceAfter=11+k*2;
   current.poll=scope.now;current.peerSequence=9+k;held.lastSeen=scope.now;sample.stun+=27.5f;
   const auto maintenance=rest::MaintainRest(scope,sample,held,current,owned,RestDescriptor(),true,true,false,false);
   if(!maintenance.valid||maintenance.count!=1||maintenance.changes[0].expected!=sl::Bits(sample.stun)){complete=false;break;}
   sample.stun=0;++writes;
  }
  Check(complete&&writes==300&&sample.mode==5&&sample.lifeState==2&&held.seededAt==seed&&sl::Bits(sample.health)==hp,"canonical30s rest survives ordinary12s regeneration using only owned conditional stun exchanges");
  current.asleep=false;current.peerDown=false;current.peerCause=0;
  Check(!rest::MaintainRest(scope,sample,held,current,owned,RestDescriptor(),true,true,false,false).valid,"accepted healthy/reset state cannot maintain old sleep");
  Check(sl::RequestWake(held,held.owner.generation,scope.now)&&sl::Wake(scope,sample,held).valid,"authorized wake keeps existing natural state2-to0 plan after duration maintenance");
 }
 {
  auto first=RestActor(),second=first;
  Check(rest::SameRestSample(first,second,false),"same fresh rest pair before exchange is coherent");second.stun=0;
  Check(!rest::SameRestSample(first,second,false)&&rest::SameRestSample(first,second,true),"only a successful exchange permits exactly observed stunzero");
  for(unsigned k=0;k<9;++k){second=first;second.stun=0;if(k==0)second.health-=1;if(k==1)second.maximumStun=3000;if(k==2)second.lifeState=0;
   if(k==3)second.requested=6;if(k==4)second.mode=6;if(k==5)second.owner.birthSerial++;if(k==6)second.qAction=0x85;
   if(k==7)second.graphHash=0x40d8fda5ca06ull;if(k==8)second.stun=1;
   Check(!rest::SameRestSample(first,second,true),"postCAS health/max/phase/owner/action/graph/stun drift cannot fabricate observation or release");
  }
  for(unsigned k=0;k<7;++k){auto current=RestTarget();auto owned=current;if(k==0)current.peerCause=1;if(k==1)current.peerDown=false;
   if(k==2)current.peerReady=false;if(k==3)current.peerSequence=0;if(k==4)current.localRole=0;if(k==5)owned.peerCause=0;if(k==6)current.localRole=2;
   Check(!rest::MaintainRest(RestScope(),first,RestLease(),current,owned,RestDescriptor(),true,true,false,false).valid,"only actual accepted ready cause2 current and immutable seed roles authorize maintenance");
  }
 }
 std::printf("sleep_rest_maintenance_tests: %u passed; %u failed\n",good,bad);return bad?1:0;
}
