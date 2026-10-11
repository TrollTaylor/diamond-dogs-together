#include "carry_info_policy.hpp"
#include <cstdio>
#include <limits>
#include <cstring>
using namespace mgscoop::carry_info::detail;
unsigned passed=0,failed=0;
void Check(bool ok,const char* label){if(ok)++passed;else{++failed;std::printf("FAIL %s\n",label);}}
Scope LiveScope(){Scope s{};s.verified=s.admitted=s.ready=s.armed=true;s.thread=s.ownerThread=71;s.mission=s.ownedMission=30050;s.epoch=s.ownedEpoch=19;s.generation=s.ownedGeneration=7;s.now=2000;s.poll=1900;s.sequenceBefore=s.sequenceAfter=20;return s;}
Identity LiveIdentity(){Identity i{};i.player=0x1000;i.data=0x2000;i.bitset=0x3000;i.exists=0x4000;i.locals=0x5000;i.slots=0x6000;i.count=2;i.realized=2;i.local=0;i.ids[0]=0;i.ids[1]=1;i.pool[0]=2;i.pool[1]=4;i.parts[0]=i.parts[1]=3;for(unsigned k=0;k<2;++k){auto& b=i.body[k];auto n=k*0x10000;b.block=0x7000+n;b.phase=0x8000+n;b.cs=0x9000+n;b.transform=0xa000+n;b.psRecord=0xb000+n;b.life=0xc000+n;b.flags=0xd000+n;b.model=0xe000+n;b.joints=0xf000+n;b.gun=0x11000+n;b.lifeValue=100;b.maximum=100;}return i;}
int main(){
 Check(sizeof(Message)==64&&alignof(Message)==16,"exact aligned stock message ABI");
 Message fresh{};Check(MessageFresh(fresh),"only stock query initialized");
 Check(ValidateReply(fresh,-1)==QueryResult::rejected,"native refusal stays refused");
 Check(ValidateReply(fresh,0)==QueryResult::anomaly,"missing handled is not readiness");
 Message accepted{};accepted.handled=1;accepted.radius=.45f;accepted.height=.8f;accepted.origin[3]=1;
 Check(ValidateReply(accepted,0)==QueryResult::eligible,"nullable native attachment still eligible");
 accepted.attachment=0x12345678;Check(ValidateReply(accepted,0)==QueryResult::eligible,"opaque attachment is not dereferenced");
 Check(!MessageFresh(accepted),"completed query cannot be reused");
 auto s=LiveScope();Check(!ScopeRefusal(s),"qualified owner lease accepted");
 auto t=s;t.mission=t.ownedMission=10010;Check(ScopeRefusal(t)!=nullptr,"unsupported mission refused independently"); t=s;t.epoch=t.ownedEpoch=0;Check(ScopeRefusal(t)!=nullptr,"zero epoch refused"); t=s;t.generation=t.ownedGeneration=0;Check(ScopeRefusal(t)!=nullptr,"zero generation refused"); t=s;t.mission=t.ownedMission=30010;Check(!ScopeRefusal(t),"Afghanistan scope supported"); t=s;t.mission=t.ownedMission=30020;Check(!ScopeRefusal(t),"Africa scope supported"); t=s;t.thread++;Check(std::strcmp(ScopeRefusal(t),"owner_thread")==0,"worker thread cannot query");
 t=s;t.ownerThread=0;Check(ScopeRefusal(t)!=nullptr,"missing natural owner refused");
 t=s;t.poll=t.now+1;Check(std::strcmp(ScopeRefusal(t),"scope_expired")==0,"reversed clock refuses lease");
 t=s;t.poll=t.now-751;Check(ScopeRefusal(t)!=nullptr,"stale lease refused");
 t=s;t.poll=t.now-750;Check(!ScopeRefusal(t),"lease endpoint exact");
 t=s;t.mission++;Check(ScopeRefusal(t)!=nullptr,"mission transition refused");
 t=s;t.epoch++;Check(ScopeRefusal(t)!=nullptr,"reused mission epoch refused");
 t=s;t.generation++;Check(ScopeRefusal(t)!=nullptr,"old incapacitation generation refused");
 t=s;t.sequenceBefore++;t.sequenceAfter++;Check(ScopeRefusal(t)!=nullptr,"injected input window refused");
 t=s;t.sequenceAfter+=2;Check(ScopeRefusal(t)!=nullptr,"closed but crossed input window refused");
 t=s;t.batchBefore=true;Check(ScopeRefusal(t)!=nullptr,"native batch refused");
 t=s;t.batchAfter=true;Check(ScopeRefusal(t)!=nullptr,"batch races refused");
 t=s;t.failed=true;Check(std::strcmp(ScopeRefusal(t),"fault_latched")==0,"native fault not retried");
 t=s;t.armed=false;Check(ScopeRefusal(t)!=nullptr,"unarmed body refused");
 t=s;t.admitted=false;Check(ScopeRefusal(t)!=nullptr,"Lua cannot claim native admission");
 t=s;t.verified=false;Check(ScopeRefusal(t)!=nullptr,"missing exact-image gate refused");
 auto a=LiveIdentity(),b=a;Check(SameIdentity(a,b),"both typed bodies coherent");
 b=a;b.player++;Check(!SameIdentity(a,b),"new player incarnation refused");
 b=a;b.pool[1]++;Check(!SameIdentity(a,b),"reused partner slot refused");
 b=a;b.parts[1]=2;Check(!SameIdentity(a,b),"unloading partner refused");
 b=a;b.body[1].phase++;Check(!SameIdentity(a,b),"replaced phase component refused");
 b=a;b.body[1].life++;Check(!SameIdentity(a,b),"replaced health record refused");
 b=a;b.body[1].lifeValue=90;Check(!SameIdentity(a,b),"changed query HP refused");
 b=a;b.body[1].qFlags=1u<<9;Check(!SameIdentity(a,b),"changed query flag refused");
 b=a;b.body[1].actionBits[5]=1u<<14;Check(!SameIdentity(a,b),"changed blocked action refused");
 b=a;b.body[0].gunTarget=1;Check(!SameIdentity(a,b),"changing native target not published");
 b=a;b.body[1].joints++;Check(!SameIdentity(a,b),"replaced query model refused");
 Check(!CarrierGunTargetsPartner(0,1,1),"target alone never proves carrying");
 Check(!CarrierGunTargetsPartner(1u<<22,0xffff,1),"carry flag without target not partner carry");
 Check(!CarrierGunTargetsPartner(1u<<22,12,1),"carry flag on ordinary soldier not teammate");
 Check(CarrierGunTargetsPartner(1u<<22,1,1),"stock flag plus target observed only");
 const auto original=accepted;
 for(unsigned k=0;k<15;++k){auto m=original;m.padding[k]=1;Check(ValidateReply(m,0)==QueryResult::anomaly,"unexpected native reply overwrite latches");}
 for(unsigned k=0;k<4;++k){auto m=original;m.origin[k]=std::numeric_limits<float>::quiet_NaN();Check(ValidateReply(m,0)==QueryResult::anomaly,"nonfinite geometry refused");}
 auto m=original;m.hash^=1;Check(ValidateReply(m,0)==QueryResult::anomaly,"message command changed refused");
 m=original;m.radius=0;Check(ValidateReply(m,0)==QueryResult::anomaly,"wrong geometry shape refused");
 m=original;m.handled=2;Check(ValidateReply(m,0)==QueryResult::anomaly,"unknown handled value refused");
 m=original;Check(ValidateReply(m,-1)==QueryResult::anomaly,"contradictory handled refusal rejected");
 std::printf("carry_info_tests: %u passed, %u failed\n",passed,failed);return failed?1:0;
}

