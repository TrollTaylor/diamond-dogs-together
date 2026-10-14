#include "../native/coop_sleep_policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
namespace {unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"coop sleep %u: %s\n",checks,why);std::exit(1);}}}
int main(){
 namespace p=mgscoop::coop_sleep_policy;
 p::Sample healthy{};healthy.health=6600;healthy.maximumHealth=6600;healthy.stun=3300;healthy.maximumStun=3300;
 healthy.qAction=1;healthy.action=0x44;healthy.graphCurrent=true;healthy.graphNode=0x10000;healthy.graphHash=0x1234;
 Check(p::Seed(healthy),"actual configured 3300 maximum is eligible, not initializer-only 3000");
 Check(p::Retired(healthy),"healthy native idle is retired");
 auto s=healthy;s.health=1234;Check(p::Seed(s),"sleep keeps positive partial health without healing");
 s=healthy;s.health=0;Check(!p::Seed(s),"dead body never seeded");
 s=healthy;s.health=6601;Check(!p::Valid(s),"overmaximum health rejected");
 s=healthy;s.maximumStun=0;Check(!p::Seed(s),"zero maximum refused");
 s=healthy;s.stun=-1;Check(!p::Valid(s),"negative stun refused");
 s=healthy;s.stun=3301;Check(!p::Valid(s),"overmaximum stun refused");
 s=healthy;s.stun=std::numeric_limits<float>::quiet_NaN();Check(!p::Valid(s),"nonfinite stun refused");
 s=healthy;s.stun=0;Check(!p::Seed(s),"no depleted idle reseed");
 s=healthy;s.qAction=0x1d;Check(!p::Seed(s),"fallback crawl does not grant stock sleep admission");
 s=healthy;s.lifeState=1;Check(!p::Seed(s),"faint cause is not overwritten");
 s=healthy;s.lifeState=2;Check(!p::Seed(s),"existing sleep is not taken over");
 s=healthy;s.carried=true;Check(!p::Seed(s)&&!p::Retired(s),"attached body is not healthy idle");
 s=healthy;s.nodeActive=true;Check(!p::Seed(s)&&!p::Retired(s),"live scheduler prevents seed or retirement");
 s=healthy;s.sleepGraph=true;Check(!p::Retired(s),"old sleep graph prevents retirement");
 s=healthy;s.graphCurrent=false;Check(!p::Retired(s),"missing graph is not retirement");
 p::Sample rest=healthy;rest.lifeState=2;rest.stun=12;rest.mode=5;rest.nodeActive=true;rest.qAction=0x84;rest.action=0x19;rest.sleepGraph=true;
 Check(p::Entered(rest),"native rest marks actual entry");
 Check(p::Maintain(rest,true,false),"positive game-regenerated stun can be held at zero in actual rest");
 Check(!p::Maintain(rest,false,false),"no maintenance before native entry");
 Check(!p::Maintain(rest,true,true),"accepted wake stops maintenance");
 Check(p::Wake(rest),"actual unattached rest permits stock wake scalar");
 Check(p::CarryReady(rest),"original rest graph permits pickup");
 auto onset=rest;onset.mode=4;onset.qAction=0x83;Check(!p::CarryReady(onset),"pickup cannot interrupt native falling-asleep graph");
 auto pendingRest=rest;pendingRest.requested=6;Check(!p::CarryReady(pendingRest),"native waking teammate cannot be picked up");
 auto carriedRest=rest;carriedRest.carried=true;carriedRest.qAction=0x2b;Check(p::CarryReady(carriedRest),"existing carried sleep preserves current permission");
 auto wrongRest=rest;wrongRest.sleepGraph=false;Check(!p::CarryReady(wrongRest),"foreign graph cannot permit sleeping pickup");
 float target=0;Check(p::WakeStun(rest,target)&&target==rest.maximumStun,"wake restores current full stun without clearing native life");
 auto nativeLife=[](p::Sample& n){if(n.stun>=n.maximumStun)n.lifeState=0;else if(n.lifeState==0&&n.stun<=0)n.lifeState=2;};
 auto depleted=rest;depleted.stun=0;depleted.lifeState=0;nativeLife(depleted);
 Check(depleted.lifeState==2,"depleted forced-life wake restarts native sleep");
 auto restored=rest;Check(p::WakeStun(restored,target),"owned rest has wake restoration plan");restored.stun=target;nativeLife(restored);
 Check(restored.lifeState==0&&!p::Retired(restored),"full stun allows native life wake but retains active graph duty");
 auto attached=rest;attached.carried=true;attached.qAction=0x2b;target=7;
 Check(!p::WakeStun(attached,target)&&target==0,"carried body cannot restore stun before native putdown");
 auto invalidWake=rest;invalidWake.maximumStun=0;Check(!p::WakeStun(invalidWake,target),"invalid current maximum cannot plan wake scalar");

 s=rest;s.stun=0;Check(!p::Maintain(s,true,false),"already zero does not write");
 Check(p::Wake(s),"zero rest still permits waking");
 s=rest;s.carried=true;s.qAction=0x2b;Check(p::Maintain(s,true,false),"carried sleeper keeps depleted stun");
 Check(!p::Wake(s)&&!p::Retired(s),"carrier must put down before waking");
 s=rest;s.requested=6;Check(p::Wake(s),"pending native wake is compatible");
 Check(!p::Maintain(s,true,false),"pending native wake is not overwritten");
 s=rest;s.mode=4;s.qAction=0x83;Check(p::Entered(s),"standing onset uses native mode4");
 Check(!p::Wake(s)&&!p::Maintain(s,true,false),"onset is not forced into rest or wake");
 s=rest;s.mode=6;s.qAction=0x85;s.lifeState=0;Check(!p::Retired(s)&&!p::Maintain(s,true,false),"wake animation retains control duty");
 s=healthy;s.action=0x19;s.qAction=0x85;Check(!p::Retired(s),"mode0 with lingering recovery actions is not retirement");
 s=healthy;s.requested=4;Check(!p::Seed(s)&&!p::Retired(s),"pending native onset prevents release");
 for(std::uint16_t action:{std::uint16_t{0x83},std::uint16_t{0x84},std::uint16_t{0x85},std::uint16_t{0x2a},std::uint16_t{0x2b},std::uint16_t{0x2c}}){
  s=healthy;s.qAction=action;Check(!p::Retired(s),"sleep/carry action cannot release controls");
 }
 p::Retirement r{};Check(!p::ObserveRetirement(r,healthy,100),"first real idle observation remains pending");
 Check(!p::ObserveRetirement(r,healthy,100),"same tick does not fabricate second receipt");
 Check(p::ObserveRetirement(r,healthy,101),"two distinct coherent idle ticks retire");
 Check(!p::ObserveRetirement(r,rest,102)&&r.observations==0,"native reentry invalidates retirement");
 Check(!p::ObserveRetirement(r,healthy,103),"fresh first idle starts over");
 Check(!p::ObserveRetirement(r,healthy,102)&&r.observations==1,"reversed clock cannot reuse older retirement");
 Check(!p::ObserveRetirement(r,healthy,0)&&r.observations==0,"zero timestamp cannot authorize release");
 std::printf("coop sleep policy: %u checks passed\n",checks);
}
