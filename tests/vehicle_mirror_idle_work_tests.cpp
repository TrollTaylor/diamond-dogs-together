#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <future>
#include "vehicle_mirror_idle_work.hpp"
#include "mgscoop/native_pose_window.hpp"
namespace s=mgscoop::vehicle_mirror_state;
namespace p=mgscoop::vehicle_mirror_idle_work;
unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %u: %s\n",checks,why);std::exit(1);}}
s::Inbox Mounted(){s::Inbox i{};i.link={42,30010,2,0,1};i.receipt.event=s::Event::mounted;i.receipt.role=1;
 i.receipt.mission=30010;i.receipt.epoch=2;i.receipt.phase=3;i.receipt.seat=0;i.receipt.mount=1;i.receipt.sequence=1;
 i.received=100;i.sequence=1;return i;}
void RetainedCases(){
 namespace ph=mgscoop::partner_input_hook;ph::NativePoseVehicleLease lease{17,2,3};
 ph::PoseSharedLease ticket{true,99,1,42};ph::NativePoseVehicleOwnership a{},b{},out{};
 a.lease=lease;a.image=0x140000000ull;a.player=0x10000;a.go=0x10100;a.data=0x10200;a.state=0x10300;a.block[1]=0x10400;
 a.birth[0]=6;a.birth[1]=7;a.birthLifecycle=8;a.purposes=4;a.vehicle=0x6408;a.thread=99;a.sequence=16;
 a.phase[1]=6;a.nativeBinding[1]=0x10600;a.currentVehicle[1]=0x6408;a.seat[1]=0;b=a;unsigned calls=0;
 auto current=[&](ph::NativePoseVehicleOwnership& owner,const ph::PoseSharedLease* actual) noexcept {
  if(actual!=&ticket||!actual->tracked||actual->serial!=42)return false;owner=++calls==1?a:b;return true;
 };
 Check(p::ReadRetained(current,&ticket,lease,0x6408,99,out)&&calls==2&&out.phase[1]==6,
  "cold actual shared ownership read preserves phase6 without a fresh link/writer-only held predicate");
 calls=0;Check(!p::ReadRetained(current,nullptr,lease,0x6408,99,out)&&!out.lease.serial,"cold fabricated/absent shared ticket refuses actual current reader");
 for(unsigned field=0;field<19;++field){b=a;calls=0;
  switch(field){case 0:b.lease.serial++;break;case 1:b.lease.generation++;break;case 2:b.lease.sceneEpoch++;break;
   case 3:b.image++;break;case 4:b.go++;break;case 5:b.thread++;break;case 6:b.player++;break;case 7:b.data++;break;
   case 8:b.state++;break;case 9:b.block[1]++;break;case 10:b.pool[1]++;break;case 11:b.birth[0]++;break;
   case 12:b.birth[1]++;break;case 13:b.birthLifecycle++;break;case 14:b.sequence+=2;break;case 15:b.phase[1]++;break;
   case 16:b.currentVehicle[1]++;break;case 17:b.nativeBinding[1]++;break;case 18:b.pending=4;break;}
  Check(!p::ReadRetained(current,&ticket,lease,0x6408,99,out)&&!out.lease.serial,
   "late body/birth/resource/phase/lease replacement refuses current cleanup retention");
 }
 b=a;calls=0;a.pending=4;Check(!p::ReadRetained(current,&ticket,lease,0x6408,99,out),"pending clear is not renewed admission");a.pending=0;
 calls=0;Check(!p::ReadRetained(current,&ticket,lease,0x6409,99,out),"different local unit cannot borrow current vehicle lease");
 calls=0;Check(!p::ReadRetained(current,&ticket,lease,0x6408,100,out),"foreign coordinator thread cannot borrow current vehicle lease");
}
int main(){p::Duty cold{};s::Inbox empty{};auto mounted=Mounted();
 Check(!p::FrameRequired(cold,false,true,empty,100),"cold empty mailbox can skip vehicle-only census");
 Check(p::FrameRequired(cold,false,false,empty,100),"busy mailbox cannot prove absence");
 Check(p::FrameRequired(cold,true,true,empty,100),"actual owned core vehicle always requires full frame");
 for(unsigned phase=1;phase<8;++phase){auto d=cold;d.phase=static_cast<s::Phase>(phase);
  Check(p::FrameRequired(d,false,true,empty,100),"pending/admitted/queued/bound/attached/exit/quarantine duty never uses idle path");}
 for(unsigned field=0;field<10;++field){auto d=cold;
  switch(field){case 0:d.purpose=1;break;case 1:d.leasePurpose=1;break;case 2:d.posePurpose=1;break;
   case 3:d.poseMount=1;break;case 4:d.clearing=true;break;case 5:d.retained=true;break;case 6:d.exitPending=true;break;
   case 7:d.releaseRequested=true;break;case 8:d.reply=true;break;case 9:d.invalid=true;break;}
  Check(p::FrameRequired(d,false,true,empty,100),"every partial retained duty forces full native validation");
  Check(p::FrameRequired(d,false,true,mounted,2000),"stale source cannot erase retained or cleanup duty");
 }
 Check(p::FrameRequired(cold,false,true,mounted,1600),"1500ms fresh demand boundary");
 Check(!p::FrameRequired(cold,false,true,mounted,1601),"expired cold demand does not create a new admission");
 Check(!p::FrameRequired(cold,false,true,mounted,99),"backward clock cannot authorize a cold mount");
 auto exiting=mounted;exiting.receipt.event=s::Event::exit;exiting.receipt.phase=0;
 Check(!p::FrameRequired(cold,false,true,exiting,110),"valid exit with completely retired module has no remaining vehicle work");
 exiting.receipt.role=0;Check(p::FrameRequired(cold,false,true,exiting,110),"unknown fresh role metadata takes full path");
 exiting=mounted;exiting.receipt.event=s::Event::exit;exiting.receipt.phase=0;exiting.sequence++;
 Check(p::FrameRequired(cold,false,true,exiting,110),"partial mailbox sequence mismatch takes full path");
 auto partial=empty;partial.sequence=1;Check(p::FrameRequired(cold,false,true,partial,100),"empty timestamp with nonempty sequence is not proven idle");
 partial=empty;partial.receipt.mount=1;Check(p::FrameRequired(cold,false,true,partial,100),"partial mount metadata is not proven idle");
 // Source copy never consumes an event; arrival after an idle snapshot is
 // seen next window. This matches the real locked InboxCopy behavior.
 Check(!p::FrameRequired(cold,false,true,empty,100),"idle snapshot before concurrent arrival");
 Check(p::FrameRequired(cold,false,true,mounted,101),"idle to natural mounted receipt triggers full frame next window");
 Check(mounted.sequence==1&&mounted.received==100,"classification never drains/freshens source receipt");
 // A real held lease remains a full-current shared read after link expiry.
 // The module now keys it by its saved purpose under the actual shared
 // ticket, rather than the owned-writer-only NativePoseVehicleHeld result.
 auto retained=cold;retained.purpose=17;retained.retained=true;
 Check(p::FrameRequired(retained,false,true,empty,2000),"cold writer predicate false cannot erase current shared cleanup duty");
 retained.phase=s::Phase::exiting;Check(p::FrameRequired(retained,false,true,mounted,10000),"phase6 after source expiry always takes native ownership closure");
 retained.phase=s::Phase::quarantined;Check(p::FrameRequired(retained,false,true,empty,10000),"replaced owner remains quarantined/full rather than idle");
 // Deterministic concurrent receive-lock fixture: busy means full, then a
 // stored fresh event is observed without changing its insertion age.
 std::mutex lock;s::Inbox shared{};std::promise<void> locked,release;auto releaseFuture=release.get_future();
 std::thread producer([&]{std::lock_guard<std::mutex> guard(lock);locked.set_value();releaseFuture.wait();shared=Mounted();});
 locked.get_future().wait();bool read=false;s::Inbox copy{};
 {std::unique_lock<std::mutex> guard(lock,std::try_to_lock);read=guard.owns_lock();if(read)copy=shared;}
 Check(!read&&p::FrameRequired(cold,false,read,copy,101),"multithread receive in progress cannot take idle shortcut");
 release.set_value();producer.join();{std::lock_guard<std::mutex> guard(lock);copy=shared;}
 Check(p::FrameRequired(cold,false,true,copy,101)&&copy.received==100,"event survives receiver contention and drives full validation next window");
 RetainedCases();std::printf("%u idle/demand lifecycle checks passed; no game\n",checks);
}
