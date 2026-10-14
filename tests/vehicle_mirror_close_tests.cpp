#include "vehicle_mirror_close_read.hpp"
#include <cstdio>
namespace close=mgscoop::vehicle_mirror_close_read;
namespace ph=mgscoop::partner_input_hook;
unsigned checks=0,failures=0;
void Check(bool yes,const char* why){++checks;if(!yes){++failures;std::printf("FAIL %s\n",why);}}
ph::NativePoseJobContext Job(){ph::NativePoseJobContext c{};
 c.player=1;c.go=2;c.block=3;c.state=4;c.modelCS=5;c.modelEntry=6;c.generation=7;c.sceneEpoch=8;c.thread=9;c.sequence=11;
 c.held=10;c.press=11;c.release=12;c.magnitude=0.5f;c.modelReady=true;c.selectedBodyOwned=true;c.selectedBody=1;
 c.pp=13;c.source=14;c.windowPPVT=15;c.birthSerial=16;return c;}
int main(){
 const auto requested=Job();auto closed=requested;closed.sequence=12;ph::NativePoseStatus status{};status.installed=true;
 Check(close::Published(requested,closed,12,true,status),"real same-job even EndWritePublish permits mirror bookkeeping release");
 Check(!close::Published(requested,closed,11,true,status),"actual still-odd sequence refuses release");
 Check(!close::Published(requested,closed,12,false,status),"failed Post/C0/writer cleanup cannot release persistent obligation");
 auto bad=status;bad.held=true;Check(!close::Published(requested,closed,12,true,bad),"actual core hold must be gone");
 bad=status;bad.owned=true;Check(!close::Published(requested,closed,12,true,bad),"actual writer must be closed");
 bad=status;bad.faulted=true;Check(!close::Published(requested,closed,12,true,bad),"fault is not successful release");
 bad=status;bad.installed=false;Check(!close::Published(requested,closed,12,true,bad),"uninstalled status cannot prove core publication");
 auto c=closed;c.sequence=14;Check(!close::Published(requested,c,14,true,status),"later unrelated closed window cannot clear this lease");
 c=closed;c.sequence=11;Check(!close::Published(requested,c,11,true,status),"odd context is not a Closed receipt");
 c=closed;++c.birthSerial;Check(!close::Published(requested,c,12,true,status),"changed body birth refuses");
 c=closed;++c.generation;Check(!close::Published(requested,c,12,true,status),"changed generation refuses");
 c=closed;++c.sceneEpoch;Check(!close::Published(requested,c,12,true,status),"changed native scene epoch refuses");
 c=closed;++c.thread;Check(!close::Published(requested,c,12,true,status),"foreign job thread refuses");
 c=closed;++c.block;Check(!close::Published(requested,c,12,true,status),"replaced actual body block refuses");
 c=closed;++c.modelCS;Check(!close::Published(requested,c,12,true,status),"replaced model CS refuses");
 c=closed;++c.modelEntry;Check(!close::Published(requested,c,12,true,status),"replaced model entry refuses");
 c=closed;++c.pp;Check(!close::Published(requested,c,12,true,status),"replaced actual PP owner refuses");
 c=closed;c.consumer=mgscoop::partner_input::Consumer::drain1;
 Check(!close::Published(requested,c,12,true,status),"different input consumer is not same completed job");
 c=closed;c.selectedBodyOwned=false;Check(!close::Published(requested,c,12,true,status),"unowned selected body refuses");
 c=closed;c.modelReady=false;Check(!close::Published(requested,c,12,true,status),"unready model receipt refuses");
 c=closed;c.magnitude=0.75f;Check(!close::Published(requested,c,12,true,status),"different input job payload refuses");
 std::printf("Vehicle exact Closed publication: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
