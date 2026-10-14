#include "sleep_partner_bridge.hpp"
#include "mgscoop/lab_status.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace mgscoop::sleep_partner_bridge;
unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::exit(1);}}
mgscoop::partner_input::Arm Arm(){mgscoop::partner_input::Arm a{};a.gen=91;a.enabled=a.native=true;a.cam=mgscoop::PartnerInputCam::sandbox;a.P=0x10000;a.go=0xffe0;a.blk1=0x406a8;a.ps1=0x51000;a.pp=0x150000;a.src=0x170000;a.root0=0x30000;a.root1=0x40000;a.pool1=7;a.ps1_idx=0;a.rates_epoch=9;return a;}
int main(){Record r{};const std::string live=std::string(kPrefix)+"30050 2 1 11 17 1 2 1 0";
 Check(Parse(live.data(),live.size(),r)&&!r.clear&&r.generation==17&&r.cause==2&&r.down==1,"accepted peer sleep has exactly nine bounded fields");
 const std::string clear=std::string(kPrefix)+"0 0 0 0 0 0 0 0 0";Check(Parse(clear.data(),clear.size(),r)&&r.clear,"only entirely zero record is explicit clear");
 for(const char* tail:{"30050 2 1 11 17 1 2 1","30050 2 1 11 17 1 2 1 0 0","30050 2 1 11 17 1 2 1 -1","30050 2 1 11 17 1 2 1 NaN","30050 2 1 11 17 1 2 1 0\n","30050 2 1 11 17 1 2 1 0.5","30050 2 3 11 17 1 2 1 0","30050 0 1 11 17 1 2 1 0","30050 2 1 0 17 1 2 1 0","30050 2 1 65536 17 1 2 1 0","30050 2 1 11 0 1 2 1 0","30050 2 1 11 17 1 0 1 0","30050 2 1 11 17 0 2 1 0","30050 2 1 11 17 1 2 0 0","30050 2 1 11 17 1 2 1 501","30050 2 1 11 4294967296 1 2 1 0","0 0 0 0 0 0 0 0 1"}){const auto bad=std::string(kPrefix)+tail;Check(!Parse(bad.data(),bad.size(),r),"malformed stale ambiguous or inconsistent peer record refuses");}
 auto a=Arm();auto owner=CaptureArm(a,true,88,1000);Check(owner.ready&&owner.generation==91&&owner.epoch==9&&owner.thread==88,"actual published Arm retains generation epoch and natural owner; PS relative index0 valid");
 for(unsigned k=0;k<11;++k){auto bad=a;bool natural=true;if(k==0)natural=false;if(k==1)bad.enabled=false;if(k==2)bad.native=false;if(k==3)bad.gen=0;if(k==4)bad.rates_epoch=0;if(k==5)bad.holdlog=true;if(k==6)bad.go+=8;if(k==7)bad.ps1=0;if(k==8)bad.pp=0;if(k==9)bad.pool1=16;if(k==10)bad.ps1_idx=2;Check(!CaptureArm(bad,natural,88,1000).ready,"disabled stale or unqualified Arm cannot mint ownership");}
 Parse(live.data(),live.size(),r);Context c{};c.worker=c.admitted=c.naturalOwner=c.fieldReady=true;c.mission=30050;c.epoch=2;c.role=1;c.thread=88;c.now=1000;
 // offline-v1 has no online host/client session role; accepted world authority
 // still comes from the fixed config used by ConfigureWorld.
 {mgscoop::lab::Status telemetry{};telemetry.role=mgscoop::lab::Role::none;
  for(auto configured:{mgscoop::LabRole::host,mgscoop::LabRole::guest}){
   mgscoop::LabConfig config{};config.present=true;config.mode=mgscoop::LabMode::lab;config.role=configured;
   auto row=r;auto scoped=c;row.role=configured==mgscoop::LabRole::host?1u:2u;
   scoped.role=ConfiguredRole(config.role);
   auto offline=Accept({},row,scoped,owner);
   Check(telemetry.role==mgscoop::lab::Role::none&&offline.ready&&offline.target.asleep,
         "offline configured host and guest accept their genuine peer row despite absent online-session diagnostic role");
   scoped.role=static_cast<std::uint32_t>(telemetry.role);
   Check(!Accept({},row,scoped,owner).ready&&Accept({},row,scoped,owner).clear,
         "using offline diagnostic none instead of world config cannot authorize a peer target");
   scoped.role=ConfiguredRole(configured==mgscoop::LabRole::host?mgscoop::LabRole::guest:mgscoop::LabRole::host);
   Check(!Accept({},row,scoped,owner).ready,"configured opposite local role cannot accept this owner row");
  }
  for(auto absent:{mgscoop::LabRole::none,static_cast<mgscoop::LabRole>(255)}){
   auto scoped=c;scoped.role=ConfiguredRole(absent);
   Check(scoped.role==0&&!Accept({},r,scoped,owner).ready&&Accept({},r,scoped,owner).clear,
         "absent and unknown config roles withdraw instead of defaulting to host");
  }
 }
 Ledger empty{};auto p=Accept(empty,r,c,owner);Check(p.ready&&p.target.asleep&&p.target.peerGeneration==17&&p.target.armGeneration==91&&p.target.armEpoch==9&&p.target.poll==1000,"accepted peer sleep becomes target using actual current Arm");auto remembered=p.next;
 c.now=1100;r.age=100;auto duplicate=Accept(remembered,r,c,owner);Check(duplicate.ready&&duplicate.target.poll==1000,"repeat presentation poll preserves original accepted timestamp");c.now=1200;r.age=100;duplicate=Accept(duplicate.next,r,c,owner);Check(duplicate.ready&&duplicate.target.poll==1000,"repeat with slow Lua clock cannot refresh old packet");c.now=1251;owner.at=1251;r.age=100;duplicate=Accept(duplicate.next,r,c,owner);Check(!duplicate.ready&&duplicate.clear,"same received record expires despite continued Lua polls");
 c.now=1100;owner.at=1100;r.age=0;r.sequence=12;r.down=0;r.cause=0;auto healthy=Accept(remembered,r,c,owner);Check(healthy.ready&&!healthy.target.asleep&&healthy.target.peerGeneration==17,"fresh same-generation accepted healthy grant queues native recovery");
 // A genuinely accepted healthy row is not a forever-valid wake grant.
 {const auto healthyRow=healthy.next.record;auto hc=c;auto ho=owner;
  auto delayed=healthyRow;delayed.age=251;
  auto expired=Accept(remembered,delayed,hc,ho);
  Check(!expired.ready&&expired.clear&&!expired.next.have&&!expired.target.pairedReady&&expired.target.poll==0,"stale healthy receive age cannot authorize native recovery");
  hc.now=1350;ho.at=1350;auto repeat=healthyRow;repeat.age=250;
  auto kept=Accept(healthy.next,repeat,hc,ho);
  Check(kept.ready&&!kept.target.asleep&&kept.target.poll==1100,"known healthy duplicate keeps original accepted time at freshness boundary");
  hc.now=1351;ho.at=1351;
  expired=Accept(kept.next,repeat,hc,ho);
  Check(!expired.ready&&expired.clear&&!expired.next.have,"known healthy duplicate expires without a newer accepted packet");
  hc=c;ho=owner;auto reordered=healthyRow;reordered.sequence=10;
  auto refused=Accept(remembered,reordered,hc,ho);
  Check(!refused.ready&&!refused.clear&&refused.next.have&&refused.next.record.down==1&&refused.next.record.generation==17,"reordered healthy row cannot replace current accepted sleep");
  for(auto wrongGeneration:{16u,18u}){auto wrong=healthyRow;wrong.generation=wrongGeneration;
   refused=Accept(remembered,wrong,hc,ho);
   Check(!refused.ready&&!refused.clear&&refused.next.record.down==1&&refused.next.record.generation==17,"healthy grant from a different generation cannot retire current sleep");
  }
  for(unsigned k=0;k<10;++k){auto stale=hc;auto staleOwner=ho;
   if(k==0)staleOwner.at=849;if(k==1)staleOwner.ready=false;if(k==2)stale.thread=99;
   if(k==3)stale.epoch=3;if(k==4)stale.mission=30010;if(k==5)stale.role=2;
   if(k==6)stale.stopping=true;if(k==7)stale.fieldReady=false;if(k==8)stale.admitted=false;
   if(k==9)staleOwner.at=stale.now+1;
   expired=Accept(healthy.next,healthyRow,stale,staleOwner);
   Check(!expired.ready&&expired.clear&&!expired.next.have&&!expired.target.pairedReady,"known healthy target withdraws on stale Arm scene owner termination or clock reversal");
  }
 }
 r.sequence=13;r.down=1;r.cause=2;Check(!Accept(healthy.next,r,c,owner).ready,"same peer generation cannot reenter sleep after accepted healthy");r.generation=18;Check(Accept(healthy.next,r,c,owner).ready,"new accepted incapacity generation can begin new cycle");
 r.sequence=11;r.generation=17;Check(!Accept(healthy.next,r,c,owner).ready,"reordered accepted-record receipt cannot replace later row");r.sequence=13;r.generation=16;Check(!Accept(remembered,r,c,owner).ready,"older generation refused even with newer sequence");r=remembered.record;r.cause=1;Check(!Accept(remembered,r,c,owner).ready,"same sequence cannot swap sleep and injury causes");r=remembered.record;r.age=1;c.now=1100;auto age=Accept(remembered,r,c,owner);r.age=0;Check(!Accept(age.next,r,c,owner).ready,"receive age cannot move backwards within one accepted record");
 bool Context::* gates[]={&Context::worker,&Context::admitted,&Context::naturalOwner,&Context::fieldReady};r=remembered.record;for(auto gate:gates){auto bad=c;bad.*gate=false;Check(!Accept(empty,r,bad,owner).ready&&Accept(empty,r,bad,owner).clear,"full native world/field/worker owner admission is mandatory");}
 for(unsigned k=0;k<8;++k){auto bad=c;auto wrong=owner;if(k==0)bad.demo=true;if(k==1)bad.stopping=true;if(k==2)bad.role=2;if(k==3)bad.epoch=3;if(k==4)bad.mission=30010;if(k==5)bad.thread=99;if(k==6)wrong.at=849;if(k==7)wrong.ready=false;Check(!Accept(empty,r,bad,wrong).ready,"demo stop role scene thread and stale Arm withdraw target");}
 auto wrap=remembered;wrap.record.sequence=65535;wrap.record.generation=0xffffffffu;r=wrap.record;r.sequence=1;r.generation=1;c.now=1100;owner.at=1100;Check(Accept(wrap,r,c,owner).ready,"sequence and generation wrapping use half-range accepted order");
 auto wound=remembered.record;wound.generation=18;wound.sequence=12;wound.cause=1;Check(Accept(remembered,wound,c,owner).ready&&!Accept(remembered,wound,c,owner).target.asleep,"cause1 remains ordinary wound fallback and never native sleep");
 std::printf("sleep_partner_bridge_tests: %u checks passed\n",checks);return 0;
}
