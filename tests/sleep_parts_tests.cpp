#include "sleep_parts_fixture.hpp"
#include "sleep_parts_read.hpp"
#include "sleep_parts_policy.hpp"
#include "sleep_parts_contract.hpp"
#include "native_pose_exclusion.hpp"
namespace part=mgscoop::sleep_parts_policy;namespace pg=mgscoop::sleep_parts_graph;namespace sl=mgscoop::sleep_partner_lease;
part::Context C(){part::Context c{};c.image=0x140000000;c.player=0x10000;c.go=0xffe0;c.data=0x20000;c.state=0x30000;c.block=0x406a8;c.ps=0x51000;c.modelCS=0xe1000;c.modelEntry=0xf1000;c.generation=91;c.sceneEpoch=9;c.thread=88;c.sequence=12;c.callerRva=0x9c2a2a;c.birthSerial=107;c.sharedSerial=43;c.flags=0x1a487;c.remoteStatus=3;c.remotePool=1;return c;}
sl::Sample A(){sl::Sample a{};const auto c=C();a.owner={c.player,c.data,0xc004c,c.block,c.ps,0x81000,0x61000,0x91000,0xb1000,c.modelCS,0x121000,30050,17,c.thread,91,9,2,107};a.health=a.maximumHealth=6600;a.stun=a.maximumStun=3300;a.stance=1;a.action=0x44;a.qAction=1;a.cause=2;a.graphCurrent=true;return a;}
mgscoop::native_pose_parts_diagnostic::Witness W(){mgscoop::native_pose_parts_diagnostic::Witness w{};w.count=w.realized=2;w.local=w.selected=0;w.actualId[0]=0;w.actualId[1]=1;w.pool[0]=4;w.pool[1]=7;w.parts[0]=w.parts[1]=3;w.blockMatched[1]=true;w.flags=0x1a487;return w;}
// Branch-effect model from exact pinned9CD030, independent of the permission
// helper. It records the actual target calls/writes that the no-op skips.
struct Effects {unsigned localUpdates=0,remoteProfile=0,remoteDirtyClears=0,remoteRetires=0,remoteConfig=0;};
void NativePoll(const mgscoop::native_pose_parts_diagnostic::Witness& w,Effects& e){
 ++e.localUpdates;const bool dil=(w.mask180&2)!=0||(w.mask184&2)!=0;
 if(w.mask180&2)++e.remoteProfile;if(w.mask184&2)++e.remoteDirtyClears;
 if(w.parts[1]==3&&dil)++e.remoteRetires;
 if(w.local==1||((w.flags&0x6000)==0x6000))++e.remoteConfig;
}
int main(){
 {auto w=W();Effects e{};Check(part::NoRemoteWork(w),"actual accepted nonliteralpool idle branch");NativePoll(w,e);Check(e.localUpdates==1&&e.remoteProfile==0&&e.remoteDirtyClears==0&&e.remoteRetires==0&&e.remoteConfig==0,"original local0 update runs while pinned body1 route makes no call or write");}
 for(unsigned k=0;k<3;++k){auto w=W();if(k==0)w.mask180=1;if(k==1)w.mask184=1;if(k==2){w.parts[0]=1;w.blockMatched[0]=false;}Effects e{};Check(part::NoRemoteWork(w),"local0 pending/configuration/replacement does not borrow body1 lifetime");NativePoll(w,e);Check(e.localUpdates==1&&e.remoteRetires==0&&e.remoteConfig==0,"allowed local0 work leaves retained body1 untouched");}
 for(unsigned k=0;k<13;++k){auto w=W();if(k==0)w.mask180=2;if(k==1)w.mask184=2;if(k==2)w.parts[1]=4;if(k==3)w.flags|=0x4000;if(k==4)w.local=1;if(k==5)w.selected=1;if(k==6)w.actualId[1]=0;if(k==7)w.pool[1]=w.pool[0];if(k==8)w.pool[1]=16;if(k==9)w.blockMatched[1]=false;if(k==10)w.count=1;if(k==11)w.realized=1;if(k==12)w.flags|=0x100;Check(!part::NoRemoteWork(w),"remote dirty/status/copy/online/mapping ambiguity never forwards original");}
 {auto before=C(),after=before;after.flags^=0x18000;Check(part::Same(before,after),"expected body0 appearance flag changes do not invalidate same target");Check(part::ContextValid(before),"actual shared-even selected0 context supported");}
 for(unsigned k=0;k<10;++k){auto before=C(),after=before;if(k==0)after.remotePending=2;if(k==1)after.remoteStatus=4;if(k==2)after.remotePool=7;if(k==3)after.block+=0x6a8;if(k==4)after.birthSerial++;if(k==5)after.sequence+=2;if(k==6)after.sharedSerial++;if(k==7)after.callerRva=0x9c2d3c;if(k==8)after.flags|=0x100;if(k==9)after.flags|=0x4000;Check(!part::Same(before,after),"late target work owner/caller/ticket/sequence drift cannot use earlier proof");}
 {auto c=C();c.sequence=13;Check(!part::ContextValid(c),"odd job receipt cannot authorize shared poll");c=C();c.sharedSerial=0;Check(!part::ContextValid(c),"diagnostic status without a real private ticket cannot authorize poll");}
 auto idle=A(),prepared=idle;sl::Lease empty{};
 Check(part::CurrentSample(true,1000,prepared,empty,idle,1100),"genuine owned unseeded preparation retained");
 auto seeded=sl::Commit(idle,1000,true,true);auto phaseSample=idle;phaseSample.stun=0;phaseSample.lifeState=2;
 Check(part::CurrentSample(false,0,prepared,seeded,phaseSample,1100),"committed seed before natural onset retained without scalar write");
 for(unsigned mode=4;mode<=6;++mode){phaseSample=idle;phaseSample.stun=0;phaseSample.mode=static_cast<std::uint8_t>(mode);phaseSample.nodeActive=true;phaseSample.action=0x19;phaseSample.qAction=static_cast<std::uint16_t>(0x7f+mode);phaseSample.stance=3;phaseSample.lifeState=mode==6?0:2;auto l=seeded;l.nativeEntered=true;l.phase=mode==6?sl::Phase::wakePending:sl::Phase::active;l.wakeAuthorized=mode==6;l.wakeAt=mode==6?1050:0;Check(part::CurrentSample(false,0,prepared,l,phaseSample,1100),"actual onset/rest/granted wake phases preserve genuine retained lease");}
 {auto l=seeded;l.nativeEntered=true;l.wakeAuthorized=true;l.phase=sl::Phase::retired;l.wakeAt=1050;Check(part::CurrentSample(false,0,prepared,l,idle,1100),"healthy idle after two-sample wake before Closed remains held permission");auto sample=idle;sample.mode=6;sample.action=0x19;sample.qAction=0x85;sample.nodeActive=true;sample.stance=3;Check(!part::CurrentSample(false,0,prepared,l,sample,1100),"retired receipt rejects recovery re-entry");l.phase=sl::Phase::wakePending;Check(part::CurrentSample(false,0,prepared,l,sample,1100),"fresh granted recovery remains held before retirement");}
 for(unsigned k=0;k<11;++k){auto l=seeded;auto sample=idle;l.nativeEntered=true;l.phase=sl::Phase::active;if(k==0)sample.owner.generation++;if(k==1)sample.owner.birthSerial++;if(k==2)sample.owner.block+=8;if(k==3)sample.maximumStun=3000;if(k==4)sample.lifeState=1;if(k==5)sample.graphCurrent=false;if(k==6)sample.health=0;if(k==7)sample.stance=4;if(k==8)l.lastSeen=349;if(k==9)l.phase=sl::Phase::quarantined;if(k==10)sample.qAction=0x84;Check(!part::CurrentSample(false,0,prepared,l,sample,1100),"stale replaced mixed-life unhealthy or quarantined checkpoint cannot allow parts");}
 auto scope=S();scope.sequenceBefore=scope.sequenceAfter=12;auto memory=OwnedFixture();memory.Put<std::uint32_t>(scope.player+0x218,0);pg::Sample read{};const char* why=nullptr;
 Check(pg::Read(memory,scope,read,&why)&&read.observed.owner.body[1].ps==0x51000,"dedicated real even target reader; no owned-odd/local0mask fiction");
 {auto localChanged=memory;localChanged.Put<std::uint8_t>(0x33000,1);localChanged.bytes.erase(0x50000);localChanged.bytes.erase(0x80000);localChanged.bytes.erase(0xe0000);Check(pg::Read(localChanged,scope,read,&why),"body0 loading/status or old model allocation is not sampled by held body1 reader");}
 for(auto mutation:{Mutation{0x33001,1,1,"instance1"},{0x32001,16,1,"instance1"},{0x406a8+0x138,0x10008,8,"block_ps1"},{0x142a82c30,0x10008,8,"player"},{0x10218,1,4,"player"},{0x100a8,0x20008,8,"player"},{0x180100,0,4,"stance"},{0x190001,3,1,"stance"}}){auto changed=memory;Change(changed,mutation);Check(!pg::Read(changed,scope,read,&why),"target identity/stance/online/selection mismatch refuses current typed reader");}
 {auto changed=memory;changed.scans=0;changed.flip=true;changed.flipAt=0x180100;changed.flipValue=0x21;pg::Sample a{},b{};Check(pg::Read(changed,scope,a)&&pg::Read(changed,scope,b)&&!pg::Same(a,b),"full packed target stance tear caught in current sample pair");}
 {mgscoop::native_pose_exclusion::Gate gate,other; mgscoop::native_pose_exclusion::ReadTicket ticket;Check(gate.TryRead(88,ticket)==mgscoop::native_pose_exclusion::Status::entered&&gate.OwnsRead(88,ticket),"actual same gate ticket acquired");Check(!gate.OwnsRead(89,ticket)&&!other.OwnsRead(88,ticket),"thread/crossgate cannot forge ownership");Check(gate.EndRead(88,ticket)&&!gate.OwnsRead(88,ticket),"ended shared receipt never remains permission");}

 {Memory gated;const auto image=std::uintptr_t(0x140000000);std::uintptr_t mismatch=0;
  for(const auto& pin:mgscoop::sleep_parts_contract::sites)for(std::size_t i=0;i<pin.size;++i)gated.bytes[image+pin.rva+i]=pin.bytes[i];
  for(const auto& slot:mgscoop::sleep_parts_contract::slots)gated.Put(image+slot.rva,image+slot.target);
  Check(mgscoop::sleep_parts_contract::CodeGate(gated,image,0xa01d000,mismatch),"complete caller/native producer/poll byte and slot proof");
  const auto last=mgscoop::sleep_parts_contract::sites[16];gated.bytes[image+last.rva+last.size-1]^=1;
  Check(!mgscoop::sleep_parts_contract::CodeGate(gated,image,0xa01d000,mismatch),"late switch-table/body byte cannot bypass full gate");gated.bytes[image+last.rva+last.size-1]^=1;
  auto slot=mgscoop::sleep_parts_contract::slots[0];gated.Put(image+slot.rva,image+slot.target+1);
  Check(!mgscoop::sleep_parts_contract::CodeGate(gated,image,0xa01d000,mismatch),"exact virtual producer slot drift refused");
  Check(!mgscoop::sleep_parts_contract::CodeGate(gated,image,0x1000,mismatch),"short image cannot authorize complete control-flow proof");
 }
 std::printf("sleep_parts_tests: %u passed; %u failed\n",good,bad);return bad?1:0;
}
