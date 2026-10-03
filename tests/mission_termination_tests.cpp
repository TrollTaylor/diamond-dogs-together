#include "mgscoop/mission_termination.hpp"
#include "mgscoop/mission_initialization.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
unsigned checks=0;
void Check(bool value) { ++checks; if(!value) { std::fprintf(stderr,"failed check %u\n",checks); std::exit(1); } }
}
int main() {
    mgscoop::MissionInitialization initialize;
    Check(!initialize.Complete());
    Check(!initialize.Returned(60896));
    Check(!initialize.Enter(0));
    Check(initialize.Enter(60896)); // Actual Mother Base initialization worker.
    Check(!initialize.Complete()); // Gameplay cannot begin after entry alone.
    Check(!initialize.Enter(61560));
    Check(!initialize.Returned(61560));
    Check(!initialize.Complete());
    Check(initialize.Returned(60896));
    Check(initialize.Complete()); // Gameplay thread is separately supplied later.
    Check(!initialize.Enter(60896) && !initialize.Returned(60896));
    initialize.Reset();
    Check(!initialize.Complete());
    Check(initialize.Enter(61560) && initialize.Returned(61560));
    Check(initialize.Complete());
    mgscoop::MissionTermination scope;
    unsigned transfers=0;
    auto transfer=[&](std::uint32_t previous) { ++transfers; Check(previous==18240); return true; };
    Check(!scope.Returned(18240)); // A return alone cannot unlock the next scene.
    Check(!scope.Enter(false,18240,29328,false,transfer));
    Check(!scope.Enter(true,0,29328,false,transfer));
    Check(!scope.Enter(true,18240,0,false,transfer));
    Check(!scope.Enter(true,18240,29328,true,transfer)); // Never move a live session.
    Check(transfers==0 && !scope.Active());
    Check(!scope.Enter(true,18240,29328,false,[](auto){return false;}));
    Check(!scope.Active());
    // Live 0.10.31 failure: ACC runs on 18240 but terminates on 29328.
    Check(scope.Enter(true,18240,29328,false,transfer));
    Check(transfers==1 && scope.Active() && scope.OnThread(29328));
    Check(!scope.OnThread(18240) && !scope.OnThread(0));
    Check(!scope.Enter(true,18240,29328,false,transfer));
    Check(!scope.Enter(true,18240,18240,false,transfer));
    Check(!scope.Returned(18240));
    Check(scope.Active()); // Yield/exception or foreign return leaves scope owned.
    Check(scope.Returned(29328));
    Check(!scope.Active() && !scope.Returned(29328));
    // Subsequent scene keeps its runtime owner. Same-thread owned host/client
    // cleanup is permitted without invoking the metadata transfer callback.
    for(bool owned : {false,true}) {
        Check(scope.Enter(true,18240,18240,owned,transfer));
        Check(transfers==1);
        Check(scope.OnThread(18240) && !scope.OnThread(29328));
        Check(!scope.Returned(29328));
        Check(scope.Returned(18240));
    }
    // Repeated worker unloads do not retain a stale termination owner.
    for(unsigned i=0;i<100;++i) {
        Check(scope.Enter(true,18240,29328+i,false,transfer));
        Check(scope.Returned(29328+i));
    }
    std::printf("%u mission termination checks passed (simulated callbacks; no game calls)\n",checks);
}
