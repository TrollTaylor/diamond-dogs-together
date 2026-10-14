#include "native_observer_negative_work.hpp"
#include "native_pose_activation.hpp"
#include "qualified_sleep_lookup_observer.hpp"
#include <cstdio>
namespace n=mgscoop::native_observer_negative_work;
namespace a=mgscoop::native_pose_activation;
namespace q=mgscoop::qualified_sleep_lookup_observer;
unsigned passed=0,failed=0;
void Check(bool ok,const char* label){if(ok)++passed;else{++failed;std::printf("FAIL %s\n",label);}}
a::ActivationCompletion Genuine(){a::ActivationCompletion c{};c.before=c.after={0x1020,0x2000,0x3000,0x4000,0x5000};
 c.partsGo=0x1000;c.rowBefore=c.rowAfter=0x6000;c.factoryEntry=0x5000;c.frame=7;c.operation=13;c.lifecycleEpoch=3;
 c.body=1;c.threadBefore=c.threadAfter=11;c.installedBefore=c.installedAfter=c.workerBefore=c.workerAfter=true;
 c.exactActivationCaller=c.normalReturn=c.alTrue=c.bit14After=true;c.exactFactoryCaller=c.factoryNormal=c.factoryOutputZero=c.factoryOutputIdentity=true;c.factoryCalls=1;return c;}
int main(){
 const auto real=Genuine();Check(n::ActivationCandidate(true,true,true,1)&&n::ActivationAfter(true,true,true,1)&&a::Stage(real,3).ready,"real native factory admission keeps all existing publication checks");
 unsigned permitted=0;for(unsigned flags=0;flags<8;++flags)for(unsigned body=0;body<4;++body){const bool candidate=n::ActivationCandidate((flags&1)!=0,(flags&2)!=0,(flags&4)!=0,body);if(candidate)++permitted;
  if(!candidate){auto c=real;c.installedBefore=false;Check(!a::Stage(c,3).ready,"cheap negative cannot publish a native activation witness");}}
 Check(permitted==2,"only exact framed same-thread ordinary body0/1 candidate enters expensive closure");
 for(unsigned flags=0;flags<8;++flags)for(unsigned factories=0;factories<3;++factories){const bool candidate=(flags&1)!=0,normal=(flags&2)!=0,al=(flags&4)!=0;
  if(!n::ActivationAfter(candidate,normal,al,factories)){auto c=real;c.installedAfter=false;Check(!a::Stage(c,3).ready,"no post authority from rejected candidate or missing actual factory");}}
 auto cold=real;cold.factoryCalls=0;cold.bit14Before=true;Check(!n::ActivationAfter(true,true,true,0)&&!a::Stage(cold,3).ready,"ALtrue no-factory no-op cannot be a birth regardless of same readable identity");
 auto stale=real;stale.lifecycleEpoch++;Check(n::ActivationAfter(true,true,true,1)&&!a::Stage(stale,3).ready,"positive optimization path cannot bypass replaced lifecycle");
 auto unknown=real;unknown.exactFactoryCaller=false;Check(!a::Stage(unknown,3).ready,"actual nested caller still mandatory");
 auto ambiguous=real;ambiguous.factoryCalls=2;Check(!a::Stage(ambiguous,3).ready,"multiple native factories never publish");
 const q::Gate g{true,true,true,true,true,false,0x140000000};q::Event foreign{};foreign.path=q::Path::setup;foreign.returnAddress=g.image+0xdead;foreign.spec=1;
 unsigned dereferences=0,originals=0;auto observe=[&](const q::Event& e){if(!q::Natural(g,e))return false;++dereferences;return q::Covered(e.id);};
 const auto forward=[&](const q::Event& e){const bool observed=observe(e);++originals;return observed;};
 Check(!forward(foreign)&&dereferences==0&&originals==1,"exact negative caller filter keeps foreign malformed native original forwarded with no spec reads");
 foreign.returnAddress=g.image+0x1a6d88d;foreign.id=q::coveredIds[0];Check(forward(foreign)&&dereferences==1&&originals==2,"accepted original call still proceeds to full actual input observation");
 auto missing=g;missing.fullCode=false;Check(!q::Admitted(missing),"cheap natural caller never replaces full code/profile/current-owner admission");
 std::printf("%u passed, %u failed\n",passed,failed);return failed?1:0;
}
