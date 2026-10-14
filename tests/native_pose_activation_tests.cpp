#include <cstdio>
#include <cstdlib>
#include "native_pose_activation.hpp"
namespace a=mgscoop::native_pose_activation;namespace b=mgscoop::native_pose_birth;
unsigned checks=0;void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"failed %s\n",what);std::exit(1);}}
a::ActivationCompletion Good(){
 a::ActivationCompletion c{};c.before=c.after={0x1020,0x2000,0x3000,0x4000,0x5000};
 c.partsGo=0x1000;c.rowBefore=c.rowAfter=0x6000;c.factoryEntry=0x5000;c.frame=7;c.operation=13;c.lifecycleEpoch=3;
 c.body=1;c.threadBefore=c.threadAfter=11;
 c.installedBefore=c.installedAfter=c.workerBefore=c.workerAfter=true;
 c.exactActivationCaller=c.normalReturn=c.alTrue=c.bit14After=true;
 c.exactFactoryCaller=c.factoryNormal=c.factoryOutputZero=c.factoryOutputIdentity=true;c.factoryCalls=1;return c;
}
a::FinalCompletion Finish(const a::ActivationCompletion& c){
 a::FinalCompletion f{};f.identity=c.after;f.row=c.rowAfter;f.partsGo=c.partsGo;f.frame=c.frame;f.operation=c.operation;f.thread=c.threadAfter;
 f.originalPartsReturned=f.installed=f.worker=f.parts3=f.bit14=f.pairedNativeResourceClosure=true;return f;
}
int main(){
 const auto c=Good();auto p=a::Stage(c,3);auto f=Finish(c);auto r=a::Complete(p,f,9);
 Check(p.ready&&r.ready,"native factory witness plus completed final parts");
 Check(a::Observed(r).origin==b::Origin::actorActivation,"distinct activation provenance");
 Check(a::Current(r,c.after),"current actual activation");
 auto v=c;v.normalReturn=false;Check(!a::Stage(v,3).ready,"original activation exception");
 v=c;v.alTrue=false;Check(!a::Stage(v,3).ready,"original false result");
 v=c;v.bit14Before=true;Check(!a::Stage(v,3).ready,"AL true no-op does not stage");
 v=c;v.bit14After=false;Check(!a::Stage(v,3).ready,"original did not activate native row");
 v=c;v.factoryOutputZero=false;Check(!a::Stage(v,3).ready,"actual nested factory failed");
 v=c;v.factoryOutputIdentity=false;Check(!a::Stage(v,3).ready,"native output pointer replaced");
 v=c;v.factoryNormal=false;Check(!a::Stage(v,3).ready,"nested factory exception");
 v=c;v.factoryCalls=0;Check(!a::Stage(v,3).ready,"no genuine factory invocation");
 v=c;v.factoryCalls=2;Check(!a::Stage(v,3).ready,"ambiguous nested factories");
 v=c;v.exactFactoryCaller=false;Check(!a::Stage(v,3).ready,"unknown factory caller");
 v=c;v.exactActivationCaller=false;Check(!a::Stage(v,3).ready,"unknown activation caller");
 v=c;v.factoryEntry+=0xc0;Check(!a::Stage(v,3).ready,"other actual CS row factory");
 v=c;v.after.entry+=0xc0;Check(!a::Stage(v,3).ready,"row replacement during original");
 v=c;v.rowAfter+=0x20;Check(!a::Stage(v,3).ready,"PS source row replacement");
 v=c;v.partsGo+=0x20;Check(!a::Stage(v,3).ready,"foreign GO parts updater");
 v=c;v.body=2;Check(!a::Stage(v,3).ready,"not ordinary two-player actor");
 v=c;v.frame=0;Check(!a::Stage(v,3).ready,"no original parts frame");
 v=c;v.threadAfter++;Check(!a::Stage(v,3).ready,"thread drift");
 v=c;v.installedBefore=false;Check(!a::Stage(v,3).ready,"native entered before installation");
 v=c;v.installedAfter=false;Check(!a::Stage(v,3).ready,"owned hooks lost");
 v=c;v.workerBefore=false;Check(!a::Stage(v,3).ready,"entry lab admission missing");
 v=c;v.workerAfter=false;Check(!a::Stage(v,3).ready,"exit lab admission missing");
 Check(!a::Stage(c,4).ready,"matching lifecycle callback during factory invalidates witness");
 auto x=f;x.originalPartsReturned=false;Check(!a::Complete(p,x,10).ready,"before 9CD030 normal return");
 x=f;x.parts3=false;Check(!a::Complete(p,x,10).ready,"before parts3");
 x=f;x.pairedNativeResourceClosure=false;Check(!a::Complete(p,x,10).ready,"native resources not final");
 x=f;x.bit14=false;Check(!a::Complete(p,x,10).ready,"retired source row");
 x=f;x.frame++;Check(!a::Complete(p,x,10).ready,"other parts frame");
 x=f;x.operation++;Check(!a::Complete(p,x,10).ready,"same pointers/frame but another activation is not the sampled witness");
 x=f;x.identity.state+=0x10;Check(!a::Complete(p,x,10).ready,"late PS replacement");
 x=f;x.identity.cs+=0x10;Check(!a::Complete(p,x,10).ready,"late CS replacement");
 x=f;x.thread++;Check(!a::Complete(p,x,10).ready,"late different thread");
 Check(!a::Complete(p,f,0).ready,"zero serial never published");
 for(unsigned kind=0;kind<3;++kind){p=a::Stage(c,3);r=a::Complete(p,f,9);
  const auto at=kind==0?c.after.block:kind==1?c.after.cs:c.after.entry;
  Check(a::Invalidate(p,r,at,kind)&&!p.ready&&!r.ready,"matching teardown clears both pending and published");
  Check(!a::Complete(p,f,10).ready,"ABA same pointer cannot recover retired proof");
 }
 p=a::Stage(c,3);r=a::Complete(p,f,9);
 Check(!a::Invalidate(p,r,0x9000,0)&&p.ready&&r.ready,"other body teardown retains actual completed factory witness");
 Check(a::Complete(p,f,10).ready,"other body epoch activity does not fabricate or erase matching witness");
 Check(a::Invalidate(p,r,c.partsGo,4)&&!p.ready&&!r.ready,"whole GO reset clears activation");
 p=a::Stage(c,3);r=a::Complete(p,f,9);a::DiscardFrame(p,c.frame);
 Check(!p.ready&&!a::Complete(p,f,10).ready&&r.serial==9,"published witness consumed once");
 auto c2=c;c2.frame++;p=a::Stage(c2,3);auto f2=Finish(c2);
 Check(a::Complete(p,f2,10).ready,"subsequent genuine false-to-true reactivation");
 auto changed=c.after;changed.block+=0x10;Check(!a::Current(r,changed),"changed block cannot use stale receipt");
 b::Completion ctor{};ctor.identity=c.after;ctor.entryThread=ctor.exitThread=11;ctor.lifecycleEpoch=3;
 ctor.installedAtEntry=ctor.installedAtExit=ctor.workerAtEntry=ctor.workerAtExit=ctor.exactCaller=ctor.normalReturn=ctor.typedPair=true;
 auto original=b::Complete(ctor,8,3);Check(original.origin==b::Origin::constructor,"constructor remains explicit");
 Check(a::Effective(original,r).origin==b::Origin::actorActivation,"newer genuine activation evidence");
 original.serial=11;Check(a::Effective(original,r).origin==b::Origin::constructor,"newer genuine constructor evidence");
 r={};Check(a::Effective(original,r).serial==11,"OFF/absent activation uses constructor");
 std::printf("%u activation lifecycle checks passed\n",checks);
}
