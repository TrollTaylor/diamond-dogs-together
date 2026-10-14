#include "vehicle_mirror_state.hpp"
#include <cstdio>
using namespace mgscoop::vehicle_mirror_state;
unsigned checks=0,failures=0;
void Check(bool yes,const char* message){++checks;if(!yes){++failures;std::printf("FAIL %s\n",message);}}
struct Backend {
 Unit unit{};unsigned admits=0,submits=0,releases=0;bool resolve=true,current=true,accept=true,writeSeat=true,queue=true,bound=false,attached=false,exited=false,release=true,replaceAfterAdmission=false;
 Backend(){unit.service=1;unit.object=2;unit.records=3;unit.bitmap=4;unit.unit=5;unit.metadata=6;unit.variantTable=7;unit.variant=8;unit.vehicle=0x6402;unit.allocated=23;unit.type=0x20;unit.baseCount=4;unit.count=4;unit.liveWord=4;}
 bool Resolve(Name,Unit& out){out=unit;return resolve;}
 bool Current(const Unit&,Unit& out){out=unit;return current;}
 bool Admit(const Unit&,unsigned char seat,std::uint16_t rider){++admits;if(writeSeat)unit.rider[seat]=rider;if(replaceAfterAdmission)++unit.unit;return accept;}
 bool Submit(std::uint16_t vehicle,unsigned char phase,unsigned char seat,unsigned char){++submits;Check(vehicle==unit.vehicle&&seat<4&&(phase==2||phase==3||phase==6),"native submission uses remapped local ID and legal phase");return queue;}
 bool Bound(const Lease&){return bound;}
 bool Attached(const Lease&){return attached;}
 bool Exited(const Lease&){return exited;}
 bool Release(std::uint64_t purpose){++releases;Check(purpose==9,"exact purpose released");return release;}
};
Link MakeLink(bool guest=false){Link l{};l.serial=11;l.mission=30010;l.epoch=2;l.localRole=guest?1:0;l.peerRole=guest?0:1;return l;}
Receipt Mount(const Link& l){Receipt r{};r.role=l.peerRole;r.phase=3;r.seat=0;r.mission=l.mission;r.epoch=l.epoch;r.mount=3;r.sequence=1;return r;}
Scope MakeScope(const Link& l){Scope s{};s.current=true;s.link=l;s.generation=4;s.thread=5;s.sequence=7;s.player=6;s.primary=7;s.record=8;s.purpose=9;s.now=1000;return s;}
int main(){
 const auto link=MakeLink();const auto mounted=Mount(link);unsigned char wire[wireSize]{};Receipt decoded{};
 Check(Encode(mounted,wire,sizeof(wire))&&Decode(wire,sizeof(wire),decoded)&&Same(decoded.name,mounted.name)&&decoded.mount==3,"logical name wire roundtrip");
 Check(Get16(wire+12)==0&&Get32(wire+16)==3,"wire contains authored index and revision, no native vehicle ID");
 wire[28]=1;Check(!Decode(wire,sizeof(wire),decoded),"reserved byte drift refuses");
 Check(!Decode(wire,sizeof(wire)-1,decoded),"truncated wire refuses");
 auto malformed=mounted;malformed.name.index=13;Check(!Encode(malformed,wire,sizeof(wire)),"bounded light vehicle name");
 malformed=mounted;malformed.phase=1;Check(!Valid(malformed),"entry phase is not source proof");
 malformed=mounted;malformed.seat=1;Check(!Valid(malformed),"driver entry side1 cannot be passenger");
 Inbox inbox{};Check(inbox.Receive(link,mounted,1000),"fresh authenticated peer receipt");
 Check(!inbox.Receive(link,mounted,1001),"duplicate receipt refuses");
 auto old=mounted;old.sequence=0;Check(!inbox.Receive(link,old,1002),"zero sequence refuses");
 old=mounted;old.role=link.localRole;old.sequence=2;Check(!inbox.Receive(link,old,1002),"local role cannot impersonate peer");
 old=mounted;old.epoch=1;old.sequence=2;Check(!inbox.Receive(link,old,1002),"prior mission epoch refuses");
 old=mounted;old.mission=30020;old.sequence=2;Check(!inbox.Receive(link,old,1002),"other region refuses");
 auto scope=MakeScope(link);Lease lease{};Backend native;
 Check(!Step(native,scope,inbox,lease)&&lease.phase==Phase::queued&&native.admits==1,"physical admission precedes natural request, no attachment claim");
 Check(native.unit.rider[0]==1&&lease.source.role==1,"peer driver maps to actual body1");
 native.bound=true;Check(!Step(native,scope,inbox,lease)&&lease.phase==Phase::bound,"P120 binding alone not physical pose receipt");
 native.attached=true;Check(Step(native,scope,inbox,lease)&&lease.phase==Phase::attached,"actual pose receipt completes attachment");
 Check(native.admits==1&&native.submits==1,"heartbeats do not repeat physical admission");
 auto exit=mounted;exit.event=Event::exit;exit.phase=0;exit.sequence=2;
 Check(inbox.Receive(link,exit,1001),"source natural exit received");
 Check(!Step(native,scope,inbox,lease)&&lease.phase==Phase::exiting&&native.releases==0,"exit request retains purpose and physical obligation");
 native.exited=true;Check(!Step(native,scope,inbox,lease)&&native.releases==0,"exit bool cannot release occupied seat");
 native.unit.rider[0]=0xffff;Check(Step(native,scope,inbox,lease)&&lease.phase==Phase::idle&&native.releases==1,"verified native deactivation and empty seat release");
 auto late=mounted;late.sequence=3;Check(!inbox.Receive(link,late,1002),"late mounted heartbeat cannot reverse exit");
 {
  const auto swapped=MakeLink(true);Inbox i{};auto r=Mount(swapped);Check(i.Receive(swapped,r,1000),"host peer accepted on guest");
  Backend b;Lease l{};auto s=MakeScope(swapped);Step(b,s,i,l);Check(l.phase==Phase::queued&&b.unit.rider[0]==1,"role swap still maps remote player to actual1");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;b.unit.rider[0]=0;Check(!Step(b,scope,i,l)&&l.phase==Phase::idle&&!b.admits,"local driver is not overwritten");
  b.unit.rider[0]=9728;Check(!Step(b,scope,i,l)&&!b.admits,"stock NPC driver preserved");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;b.accept=false;Step(b,scope,i,l);Check(l.phase==Phase::quarantined&&b.unit.rider[0]==1&&!b.releases&&!b.submits,"partial false native admission retains physical obligation");
  Step(b,scope,i,l);Check(b.admits==1&&!b.releases,"quarantine never retries or frees live obligation");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;b.replaceAfterAdmission=true;Step(b,scope,i,l);Check(l.phase==Phase::quarantined&&!b.submits&&!b.releases,"allocation replacement after native call quarantines");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;b.queue=false;Step(b,scope,i,l);Check(l.phase==Phase::quarantined&&b.unit.rider[0]==1&&!b.releases,"failed queue after physical admission retains lease");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;b.writeSeat=false;b.accept=false;Step(b,scope,i,l);Check(l.phase==Phase::pending,"failed admission without seat retains cancellable pending intent");
  auto e=exit;i.Receive(link,e,1001);Step(b,scope,i,l);Check(l.phase==Phase::idle&&b.releases==1,"pre-admission source exit cancels exact purpose");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;Step(b,scope,i,l);auto s=scope;++s.generation;Step(b,s,i,l);Check(l.phase==Phase::quarantined&&!b.releases,"owner generation loss never exits old allocation");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;auto s=scope;s.sequence=8;Step(b,s,i,l);Check(!b.admits&&l.phase==Phase::idle,"cold even sequence cannot invoke backend");
  s=scope;s.current=false;Step(b,s,i,l);Check(!b.admits,"false actual owned context refuses");
  s=scope;s.now=2501;Step(b,s,i,l);Check(!b.admits,"expired source before admission refuses");
 }
 {
  Receipt passenger=mounted;passenger.phase=2;passenger.seat=2;passenger.driverMount=14;Inbox i{};Check(i.Receive(link,passenger,1000),"native peer passenger receipt");
  Receipt driver=mounted;driver.role=link.localRole;driver.mount=14;Backend b;b.unit.rider[0]=0;Lease l{};
  Step(b,scope,i,l);Check(!b.admits,"passenger requires current local driver's native source receipt");
  auto wrong=driver;wrong.mount=13;Step(b,scope,i,l,&wrong);Check(!b.admits,"passenger driver revision must match");
  wrong=driver;wrong.name.index=1;Step(b,scope,i,l,&wrong);Check(!b.admits,"passenger logical vehicle must match driver");
  Step(b,scope,i,l,&driver);Check(l.phase==Phase::queued&&b.unit.rider[0]==0&&b.unit.rider[2]==1,"local driver and peer passenger occupy separate real seats");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;auto s=scope;
  Step(b,s,i,l);s.driveCurrent=false;s.link={};
  Check(!Step(b,s,i,l)&&l.phase==Phase::queued&&l.exitPending&&b.submits==1&&!b.releases,
   "expired link keeps already queued physical obligation without another mount or guessed exit");
  Check(Owned(l,s)&&!DriveOwned(l,s,i),"native ownership survives link loss while driving authority ends");
  b.bound=true;Check(!Step(b,s,i,l)&&l.phase==Phase::exiting&&l.releaseRequested&&b.submits==2,
   "actual queued native binding permits phase6 after link loss");
  Step(b,s,i,l);Check(b.submits==2&&!b.releases,"phase6 is not repeated during cleanup");
  b.exited=true;b.unit.rider[0]=0xffff;Check(Step(b,s,i,l)&&l.phase==Phase::idle&&b.releases==1,
   "actual unbound empty native seat permits release without fresh network source");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;Step(b,scope,i,l);b.bound=b.attached=true;Step(b,scope,i,l);
  auto s=scope;s.now=2501;Check(!Step(b,s,i,l)&&l.phase==Phase::exiting&&l.exitPending&&b.submits==2,
   "source timeout exits a current attached native driver instead of losing identity");
  s=scope;s.current=false;Check(!Step(b,s,i,l)&&l.phase==Phase::quarantined&&!b.releases,
   "native Arm or ownership loss during exit quarantines; stale guard never grants cleanup permission");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;b.writeSeat=false;b.accept=false;Step(b,scope,i,l);
  auto s=scope;s.link={};s.driveCurrent=false;Check(!Step(b,s,i,l)&&l.phase==Phase::idle&&b.releases==1&&!b.submits,
   "source loss before native admission cancels only the verified empty pending purpose");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;auto s=scope;s.driveCurrent=false;
  Check(!Step(b,s,i,l)&&l.phase==Phase::idle&&!b.admits,"cleanup scope never creates a new mounted intent");
  s=scope;Step(b,s,i,l);const auto revision=l.source.mount;
  auto newer=mounted;newer.sequence=2;++newer.mount;i.Receive(link,newer,1001);b.bound=true;
  Check(!Step(b,s,i,l)&&l.phase==Phase::exiting&&l.source.mount==revision&&b.admits==1,
   "new mount revision retires the old physical owner rather than transplanting authority");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;Step(b,scope,i,l);b.bound=true;Step(b,scope,i,l);
  auto changed=link;++changed.serial;Inbox other{};auto r=mounted;r.sequence=2;other.Receive(changed,r,1001);
  auto s=scope;s.link=changed;Check(Owned(l,s)&&!DriveOwned(l,s,other),"new channel incarnation cannot inherit old native drive authority");
  Check(!Step(b,s,other,l)&&l.phase==Phase::exiting&&b.admits==1&&b.submits==2,
   "native cleanup uses retained old lease after channel replacement");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;Step(b,scope,i,l);b.bound=true;Step(b,scope,i,l);
  auto changed=scope;++changed.primary;Check(!Step(b,changed,i,l)&&l.phase==Phase::quarantined&&b.submits==1&&!b.releases,
   "replaced native controller cannot be cleaned using the old retained source");
 }
 {
  Inbox i{};i.Receive(link,mounted,1000);Lease l{};Backend b;Step(b,scope,i,l);
  l.phase=Phase::admitted;auto s=scope;s.driveCurrent=false;s.link={};
  Check(!Step(b,s,i,l)&&l.phase==Phase::quarantined&&b.submits==1&&!b.releases&&b.unit.rider[0]==1,
   "accepted but unqueued seat is quarantined after authority loss; no guessed phase0 disposal");
 }
 {
  Inbox i{};Check(ReceiveCaptured(i,link,link,mounted,1000,2000)&&i.received==1000,
   "dequeue retains exact actual accepted GetTickCount64 age instead of refreshing it");
  auto next=mounted;next.sequence=2;Check(!ReceiveCaptured(i,link,link,next,1000,2501)&&i.sequence==1,
   "old queued native receipt cannot be freshened by dequeue time");
  auto prior=link;--prior.serial;Check(!ReceiveCaptured(i,link,prior,next,2000,2001),
   "actual old queue incarnation cannot be stamped with current link");
  Check(!ReceiveCaptured(i,link,link,next,0,2001),"missing accepted clock refuses");
  Check(!ReceiveCaptured(i,link,link,next,3000,2001),"future or incomparable accepted clock refuses");
  Check(ReceiveCaptured(i,link,link,next,2000,2001)&&i.received==2000,"fresh same-origin queued receipt accepted at original insertion tick");
 }
 std::printf("Vehicle mirror: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
