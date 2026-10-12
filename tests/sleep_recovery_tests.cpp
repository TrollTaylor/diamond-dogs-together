#include "sleep_parts_fixture.hpp"
#include "sleep_parts_policy.hpp"
#include "sleep_parts_read.hpp"
#include "sleep_partner_controls.hpp"
#include "sleep_recovery_contract.hpp"
namespace sl=mgscoop::sleep_partner_lease;namespace sp=mgscoop::sleep_parts_policy;namespace ctl=mgscoop::sleep_partner_controls;
sl::Sample RecoveryActor(){sl::Sample a{};a.owner={0x10000,0x20000,0xc004c,0x406a8,0x51000,0x81000,0x61000,0x71000,0x91000,0xe1000,0x121000,30050,17,88,91,9,2,107};a.health=a.maximumHealth=6600;a.stun=a.maximumStun=3300;a.action=0x44;a.qAction=1;a.stance=1;a.cause=2;a.graphCurrent=true;return a;}
sl::Scope RecoveryScope(std::uint64_t now,std::uint32_t seq){sl::Scope s{};s.exactRetailGate=s.sleepCodeGate=s.seedCodeGate=s.workerActive=s.threadAdmitted=s.copiedSave=s.coherent=s.admitted=s.pairedOffline=s.selectedBodyOwned=s.ownedOddWindow=s.participantsLive=s.controlsZero=true;s.thread=88;s.sequenceBefore=s.sequenceAfter=seq;s.now=s.poll=now;return s;}
sl::Sample RecoveryIdle(std::uint64_t hash){auto a=RecoveryActor();a.action=0x19;a.qAction=0x85;a.graphHash=hash;return a;}
sl::Lease NativeLease(){auto l=sl::Commit(RecoveryActor(),1000,true,true);l.nativeEntered=true;l.phase=sl::Phase::active;return l;}
int main(){
 constexpr std::uint64_t hashes[]={0x40d8fda5ca06ull,0x1edb72845e25ull,0xa3d5c9d31392ull,0x9df96634ca50ull};
 for(auto hash:hashes){
  auto a=RecoveryIdle(hash);auto l=NativeLease();const auto before=l;
  Check(!sp::CurrentSample(false,0,RecoveryActor(),l,a,1100)&&l.recoveryAt==0&&l.lastSeen==before.lastSeen,"passive shared poll cannot invent the recovery clock");
  auto scope=RecoveryScope(1100,11);auto receipt=sl::Observe(l,scope,a,11);
  Check(l.recoveryAt==1100&&receipt.holdControls&&receipt.suppressProne&&!receipt.retired&&!receipt.labAbortRequired&&!l.wakeAuthorized,"genuine current odd observation can start natural recovery retention before host grant");
  Check(sp::CurrentSample(false,0,RecoveryActor(),l,a,1120),"four exact current loaded recovery hashes admit shared holder only");
  Check(!sl::RetiredSample(a)&&!ctl::ReleaseSample(l,scope,a),"holder is never a retirement or input-release receipt");
  scope=RecoveryScope(1200,13);receipt=sl::Observe(l,scope,a,13);
  Check(l.recoveryAt==1100&&!receipt.retired,"later owned recovery read renews freshness but never the absolute clock");
  auto mode6=a;mode6.mode=6;mode6.nodeActive=true;auto first=NativeLease();scope=RecoveryScope(1100,11);receipt=sl::Observe(first,scope,mode6,11);
  Check(first.recoveryAt==1100&&!receipt.retired&&first.nativeEntered,"strict owned mode6 captures immutable deadline before its first mode0 shared poll");
  Check(sp::CurrentSample(false,0,RecoveryActor(),first,a,1120),"mode6-before-mode0 acquisition race cannot manufacture or miss passive clock");
 }
 for(unsigned k=0;k<22;++k){
  auto a=RecoveryIdle(hashes[0]);auto l=NativeLease();sl::Observe(l,RecoveryScope(1100,11),a,11);
  if(k==0)a.graphHash=0;if(k==1)a.graphHash=0x1234567890abull;if(k==2)a.graphHash=0xf2364261c9d4ull;
  if(k==3)a.mode=4;if(k==4)a.requested=6;if(k==5)a.lifeState=1;if(k==6)a.nodeActive=true;
  if(k==7)a.action=0x44;if(k==8)a.qAction=1;if(k==9)a.graphCurrent=false;if(k==10)a.graphSleepCandidate=true;
  if(k==11)a.owner.birthSerial++;if(k==12)a.owner.armGeneration++;if(k==13)a.owner.cs+=8;
  if(k==14)a.maximumStun=3000;if(k==15)a.health=0;if(k==16)a.stance=4;if(k==17)a.cause=1;
  if(k==18)l.phase=sl::Phase::retired;if(k==19)l.phase=sl::Phase::quarantined;if(k==20)l.nativeEntered=false;if(k==21)l.lastSeen=349;
  Check(!sp::CurrentSample(false,0,RecoveryActor(),l,a,1120),"unknown hash/native state/life/action/node/graph/owner/max/phase/staleness cannot borrow holder");
 }
 for(unsigned k=0;k<7;++k){
  auto l=NativeLease();auto a=RecoveryIdle(hashes[0]);auto scope=RecoveryScope(1100,11);
  if(k==0)scope.sequenceBefore=scope.sequenceAfter=12;if(k==1)scope.controlsZero=false;if(k==2)scope.coherent=false;
  if(k==3)scope.selectedBodyOwned=false;if(k==4)a.owner.birthSerial++;if(k==5)a.maximumStun=3000;if(k==6)scope.sequenceAfter=13;
  const auto receipt=sl::Observe(l,scope,a,scope.sequenceBefore);Check(l.recoveryAt==0&&!receipt.retired,"refused/fabricated/even/nonzero/mismatched owner cannot start recovery clock");
 }
 {
  auto a=RecoveryIdle(hashes[0]);auto l=NativeLease();sl::Observe(l,RecoveryScope(1100,11),a,11);
  const auto captured=l.recoveryAt;auto scope=RecoveryScope(7100,13);auto receipt=sl::Observe(l,scope,a,13);
  Check(l.recoveryAt==captured&&!receipt.labAbortRequired&&sp::CurrentSample(false,0,RecoveryActor(),l,a,7100),"six-second absolute boundary retains controls with fresh true read");
  Check(!sp::CurrentSample(false,0,RecoveryActor(),l,a,7101),"fresh passive packet cannot extend expired recovery gap");
  Check(ctl::Watchdog(false,0,l,7101)!=nullptr,"direct entry watchdog expires even though lastSeen is freshly owned");
  scope=RecoveryScope(7101,15);receipt=sl::Observe(l,scope,a,15);
  Check(receipt.labAbortRequired&&receipt.holdControls&&!receipt.retired&&l.recoveryAt==captured&&l.lastSeen==7100,"expiry abort retains lease/control and does not renew native timestamp");
  Check(sl::RecoveryDeadline(l,1099)!=nullptr,"absolute recovery clock reversal fails closed");
 }
 {
  auto a=RecoveryIdle(hashes[0]);auto l=NativeLease();sl::Observe(l,RecoveryScope(1100,11),a,11);
  Check(sl::RequestWake(l,l.owner.generation,1200)&&l.recoveryAt==1100,"later canonical wake grant cannot reset natural recovery deadline");
  auto idle=RecoveryActor();auto scope=RecoveryScope(1300,13);auto receipt=sl::Observe(l,scope,idle,13);
  Check(!receipt.retired&&l.retiredSamples==1,"real strict idle still needs a second owned window");
  scope=RecoveryScope(1400,15);receipt=sl::Observe(l,scope,idle,15);
  Check(receipt.retired&&ctl::ReleaseSample(l,scope,idle),"second genuine idle plus actual authorized scope remains separate release proof");
  Check(!sl::RecoveryDeadline(l,8000),"genuinely retired waiting grant has no pending recovery deadline");
  scope=RecoveryScope(1500,17);receipt=sl::Observe(l,scope,a,17);
  Check(receipt.labAbortRequired&&receipt.holdControls&&!receipt.retired,"retired receipt cannot admit lingering or reentered native action");
 }
 {
  auto a=RecoveryIdle(hashes[0]);auto l=NativeLease();sl::Observe(l,RecoveryScope(1100,11),a,11);
  auto scope=RecoveryScope(1300,13);auto idle=RecoveryActor();sl::Observe(l,scope,idle,13);scope=RecoveryScope(1400,15);const auto receipt=sl::Observe(l,scope,idle,15);
  Check(receipt.retired&&receipt.holdControls&&!l.wakeAuthorized&&!ctl::ReleaseSample(l,scope,idle),"natural retirement cannot release without canonical wake grant");
 }
 {
  auto l=NativeLease();auto a=RecoveryIdle(hashes[0]);sl::Observe(l,RecoveryScope(1100,11),a,11);std::uint64_t preparedAt=1000;
  Check(ctl::ResumeTimers(1200,11200,preparedAt,l)&&l.recoveryAt==1100&&sl::RecoveryDeadline(l,11200),"carry pause shifts old deadlines but never the absolute recovery clock");
 }
 {
  auto scope=S();scope.sequenceBefore=scope.sequenceAfter=12;auto memory=OwnedFixture();memory.Put<std::uint32_t>(scope.player+0x218,0);
  memory.Put<std::uint64_t>(0x200f00,hashes[0]);memory.Put<std::uint8_t>(0xa1000,0x19);memory.Put<std::uint16_t>(0xd0002,0x85);
  mgscoop::sleep_parts_graph::Sample a{},b{};
  Check(mgscoop::sleep_parts_graph::Read(memory,scope,a)&&a.observed.graph[1].currentNode&&a.observed.graph[1].stateHash==hashes[0],"current loaded graph node supplies exact recovery hash through existing real shared reader");
  memory.Put<std::uint64_t>(0x200f00,hashes[1]);
  Check(mgscoop::sleep_parts_graph::Read(memory,scope,b)&&!mgscoop::sleep_parts_graph::Same(a,b),"current recovery hash change is caught by mandatory paired graph equality");
 }
 {
  Memory memory;const auto image=std::uintptr_t(0x140000000);std::uintptr_t mismatch=0;
  for(const auto& pin:mgscoop::sleep_recovery_contract::sites)for(std::size_t i=0;i<pin.size;++i)memory.bytes[image+pin.rva+i]=pin.bytes[i];
  for(const auto& slot:mgscoop::sleep_recovery_contract::slots)memory.Put(image+slot.rva,image+slot.target);
  Check(mgscoop::sleep_recovery_contract::CodeGate(memory,image,0xa01d000,mismatch),"all23 full native wake/exit spans and5slots required");
  const auto pin=mgscoop::sleep_recovery_contract::sites[9];memory.bytes[image+pin.rva+pin.size-1]^=1;
  Check(!mgscoop::sleep_recovery_contract::CodeGate(memory,image,0xa01d000,mismatch),"late complete wake-body byte drift refuses gate");
  memory.bytes[image+pin.rva+pin.size-1]^=1;const auto slot=mgscoop::sleep_recovery_contract::slots[0];memory.Put(image+slot.rva,image+slot.target+1);
  Check(!mgscoop::sleep_recovery_contract::CodeGate(memory,image,0xa01d000,mismatch),"exact exit dispatch slot drift refuses gate");
 }
 std::printf("sleep_recovery_tests: %u passed; %u failed\n",good,bad);return bad?1:0;
}
