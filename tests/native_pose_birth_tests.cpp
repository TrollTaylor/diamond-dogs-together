#include <cstdio>
#include <cstdlib>
#include "native_pose_birth.hpp"
namespace b=mgscoop::native_pose_birth;
unsigned checks=0;void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"failed %s\n",what);std::exit(1);}}
int main(){
 b::Completion c{};c.identity={0x1000,0x2000,0x3000,0x4000,0x5000};c.entryThread=c.exitThread=7;c.lifecycleEpoch=1;
 c.installedAtEntry=c.installedAtExit=c.workerAtEntry=c.workerAtExit=c.exactCaller=c.normalReturn=c.typedPair=true;
 auto r=b::Complete(c,1,1);Check(b::Current(r,c.identity),"completed current birth");
 auto v=c;v.installedAtEntry=false;Check(!b::Complete(v,2,1).ready,"old/inflight actor began before install");
 v=c;v.installedAtExit=false;Check(!b::Complete(v,2,1).ready,"install unavailable on completion");
 v=c;v.normalReturn=false;Check(!b::Complete(v,2,1).ready,"exception does not produce birth");
 v=c;v.exactCaller=false;Check(!b::Complete(v,2,1).ready,"unknown constructor caller");
 v=c;v.typedPair=false;Check(!b::Complete(v,2,1).ready,"typed object drift");
 v=c;v.exitThread=8;Check(!b::Complete(v,2,1).ready,"owner thread drift");
 v=c;v.workerAtEntry=false;Check(!b::Complete(v,2,1).ready,"nonprivate entry");
 v=c;v.workerAtExit=false;Check(!b::Complete(v,2,1).ready,"admission loss");
 Check(!b::Complete(c,0,1).ready,"zero receipt");
 Check(!b::Complete(c,2,2).ready,"teardown after typed sample but before publication refuses stale birth");
 v=c;v.lifecycleEpoch=0;Check(!b::Complete(v,2,1).ready,"missing before-original invalidation receipt");
 auto id=c.identity;id.state+=16;Check(!b::Current(r,id),"replaced PS is not constructor-owned state");
 id=c.identity;id.cs+=16;Check(!b::Current(r,id),"replaced model cannot opt in");
 id=c.identity;id.player+=16;Check(!b::Current(r,id),"replaced player");
 Check(b::InvalidatedBy(r,c.identity.block,0),"whole block destruction");
 Check(b::InvalidatedBy(r,c.identity.cs,1),"CS deletion");
 Check(b::InvalidatedBy(r,c.identity.entry,2),"entry deletion");
 Check(!b::InvalidatedBy(r,0x6000,0),"unrelated lifetime does not clear receipt");
 std::printf("%u birth checks passed\n",checks);
}
