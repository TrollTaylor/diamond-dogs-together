#include "sleep_parts_diagnostic.hpp"
#include <cstdio>
#include <cstring>
namespace d=mgscoop::sleep_parts_diagnostic;namespace p=mgscoop::sleep_parts_policy;namespace l=mgscoop::sleep_partner_lease;
l::Sample A(){l::Sample a{};a.owner={1,2,4,8,16,24,32,40,48,56,64,30010,4,88,1,3,3,4};a.health=a.maximumHealth=6600;a.stun=a.maximumStun=3300;a.stance=1;a.action=0x44;a.qAction=1;a.cause=2;a.graphCurrent=true;return a;}
int main(){unsigned good=0,bad=0;const auto Check=[&](bool ok,const char* why){if(ok)++good;else{++bad;std::printf("FAIL %s\n",why);}};
 const auto initial=A();auto retained=l::Commit(initial,1000,true,true);retained.nativeEntered=true;retained.phase=l::Phase::wakePending;retained.wakeAuthorized=true;retained.wakeAt=1050;
 for(unsigned k=0;k<28;++k){auto lease=retained;auto current=initial;
  if(k==0)current.graphSleepCandidate=true;if(k==1){current.mode=6;current.qAction=0x85;current.action=0x19;current.nodeActive=true;current.requested=6;}
  if(k==2)lease.lastSeen=349;if(k==3)current.maximumStun=3000;if(k==4)current.owner.birthSerial++;if(k==5)current.graphCurrent=false;
  if(k==6)current.health=0;if(k==7)current.stun=65536;if(k==8)current.stance=4;if(k==9)current.cause=1;if(k==10)current.lifeState=3;
  if(k==11)lease.phase=l::Phase::quarantined;if(k==12)lease.seededAt=0;if(k==13)lease.wakeAt=5000;if(k==14)lease.phase=l::Phase::retired;
  if(k==15){lease.phase=l::Phase::retired;current.graphSleepCandidate=true;}if(k==16){current.mode=6;current.qAction=0x85;current.action=0x19;current.nodeActive=true;}
  if(k>=17&&k<23){current.mode=5;current.qAction=0x84;current.action=0x19;current.nodeActive=true;current.lifeState=2;if(k==18)current.nodeActive=false;if(k==19)current.action=0x44;if(k==20)current.qAction=1;if(k==21)current.requested=5;if(k==22)current.lifeState=0;}
  if(k==23){current.mode=4;current.qAction=0x83;current.action=0x19;current.nodeActive=true;current.lifeState=2;}if(k==24){lease.phase=l::Phase::empty;}
  if(k==25){lease.phase=l::Phase::empty;current.maximumStun=3000;}if(k==26){lease.nativeEntered=false;current.lifeState=2;current.stun=0;lease.phase=l::Phase::seeded;lease.wakeAt=0;}if(k==27)current.owner.generation++;
  const bool actual=p::CurrentSample(false,0,initial,lease,current,1100);const auto explain=d::Explain(false,0,initial,lease,current,1100);
  Check(actual==(explain==nullptr),"diagnostic label exactly mirrors unchanged permission for current lifecycle cases");
 }
 auto recovery=initial;recovery.graphSleepCandidate=true;Check(!std::strcmp(d::Explain(false,0,initial,retained,recovery,1100),"mode0_not_retired"),"idle with lingering recovery graph identifies strict retirement predicate without allowing it");
 recovery=initial;recovery.mode=6;recovery.qAction=0x85;recovery.action=0x19;recovery.nodeActive=true;recovery.requested=6;
 Check(!std::strcmp(d::Explain(false,0,initial,retained,recovery,1100),"mode6_request"),"actual mode6 pending6 detail is distinct from graph/owner/staleness");
 Check(d::Age(1100,1000)==100&&d::Age(1100,0)==UINT32_MAX&&d::Age(999,1000)==UINT32_MAX,"missing/backward timestamps are explicit sentinel");
 Check(d::Age(UINT64_MAX,1)==UINT32_MAX,"diagnostic age saturation never wraps");
 std::printf("sleep_parts_diagnostic_tests: %u passed; %u failed\n",good,bad);return bad?1:0;
}
