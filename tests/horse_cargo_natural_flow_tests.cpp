#ifndef MGS_HORSE_FLOW_TEST_HEADER
#define MGS_HORSE_FLOW_TEST_HEADER "horse_cargo_natural_flow.hpp"
#endif
#include MGS_HORSE_FLOW_TEST_HEADER
#include "carry_horse_hint_read.hpp"
#include <map>
#include <cstdio>
namespace f=mgscoop::horse_cargo_natural_flow;namespace hk=mgscoop::partner_input_hook;
unsigned passed=0,failed=0;void Check(bool value,const char* name){if(value)++passed;else{++failed;std::printf("FAIL %s\n",name);}}
struct Memory {std::map<std::uintptr_t,unsigned char> bytes;
 template<class T>void Put(std::uintptr_t at,T value){const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t n=0;n<sizeof(value);++n)bytes[at+n]=p[n];}
 bool operator()(std::uintptr_t at,void* out,std::size_t size) noexcept {auto* p=static_cast<unsigned char*>(out);for(std::size_t n=0;n<size;++n){const auto it=bytes.find(at+n);if(it==bytes.end())return false;p[n]=it->second;}return true;}
};
struct Fixture {f::Sample before{},after{};hk::NativeHorseFlowCall call{};
 Fixture(std::uint16_t victim=0x3201){auto& o=before.owner;auto& b=o.births;
  b.image=0x140000000;b.player=0x10000;b.data=0x20000;b.state=0x30000;b.thread=19;b.sequence=8;b.lifecycle=72;b.generation=21;b.sceneEpoch=23;
  o.sharedSerial=0x100000006ull;o.at=1000;
  for(unsigned k=0;k<2;++k){b.birth[k]=0x100000003ull+k;b.block[k]=0x40000+k*0x1000;b.ps[k]=0x50000+k*0x1000;b.cs[k]=0x60000+k*0x1000;b.entry[k]=0x70000+k*0x1000;o.carrierRecord[k]=0x80000+k*0x1000;o.recipientRecord[k]=0x90000+k*0x1000;}
  auto& h=before.horse;h.gameId=6656;h.family=0xa0000;h.kind=0xb0000;h.realized=0xc0000;h.controllerArray=0xd0000;h.service=0xe0000;h.primary=0xf0000;h.receiver=h.primary+0x20;h.descriptor=0x100000;h.records=h.record=0x110000;h.sdRecord=0x120000;h.unit=0x110070;h.cs=0x130000;h.nativeIndex=h.nativeBase=1;h.nativeCount=1;
  h.primaryType=0x2363c48;h.interfaceType=0x2363c80;h.matrixFinite=true;h.cargo=h.sdCargo=0xffff;
  before.packet={0x5167c60042b5ull,victim,1,0,0};auto& c=before.carrier;
  c.record=o.carrierRecord[0];c.victim=c.previous=victim;c.actual=0;c.phase=0x17;c.prior=3;c.horse=6656;
  for(unsigned k=0;k<2;++k){before.player[k].record=o.recipientRecord[k];before.player[k].carrier=0xffff;}
  if(victim<2){auto& p=before.player[victim];p.carrierRecord=c.record;p.carrier=0;p.resource=0x140000;p.phase=3;}
  call={b.image+0x1136f57,b.image+0x2c38008,0x150000,0x150080,0x150080,6656};
  after=before;after.owner.at++;after.packet.unit=h.unit;after.horse.cargo=after.horse.sdCargo=victim;after.horse.occupied=1;
 }
};
int main(){Fixture npc;auto r=f::Complete(npc.before,npc.after,npc.call,0);
 Check(r.closed&&r.slotChanged&&r.slotMatches&&!r.playerTarget&&!std::strcmp(r.reason,"horse_flow_slot_committed"),"original sleeping guard sender and actual slot commit observed");
 Fixture player{1};r=f::Complete(player.before,player.after,player.call,0);
 Check(r.closed&&r.playerTarget&&r.oldCarryLink&&r.recipientPhase==3,"Player slot commit remains distinct from old shoulder attachment");
 Check(!player.after.horse.anchorFresh,"observer never fabricates original anchor producer freshness");
 auto repeated=player;repeated.before.horse=player.after.horse;r=f::Complete(repeated.before,repeated.after,repeated.call,0);
 Check(r.closed&&!r.slotChanged&&!std::strcmp(r.reason,"horse_flow_slot_existing"),"same occupant idempotent reply does not invent a new native commit");
 auto refused=player;refused.after.horse=refused.before.horse;refused.after.packet.unit=0;r=f::Complete(refused.before,refused.after,refused.call,-1);
 Check(r.closed&&!r.slotMatches&&!std::strcmp(r.reason,"horse_flow_no_commit"),"original refused reply stays a no-commit observation");
 auto unloaded=player;unloaded.before=player.after;unloaded.before.packet.kind=2;unloaded.before.packet.unit=0;unloaded.before.carrier.phase=7;
 unloaded.after=unloaded.before;unloaded.after.owner.at++;unloaded.after.horse.cargo=unloaded.after.horse.sdCargo=0xffff;unloaded.after.horse.occupied=0;unloaded.call.caller=unloaded.before.owner.births.image+0x113a814;
 r=f::Complete(unloaded.before,unloaded.after,unloaded.call,0);
 Check(r.closed&&r.freeAfter&&r.oldCarryLink&&!std::strcmp(r.reason,"horse_flow_slot_retired"),"stock horse kind2 retirement never asserts Player graph or wake cleanup");
 r=f::Complete(unloaded.before,unloaded.after,unloaded.call,-1);
 Check(r.closed&&r.freeAfter&&!std::strcmp(r.reason,"horse_flow_unload_unsettled"),"failed native reply with a free slot is not promoted to successful original unload");
 auto free=unloaded;free.before.horse=free.after.horse;r=f::Complete(free.before,free.after,free.call,0);
 Check(r.closed&&!r.slotChanged&&!std::strcmp(r.reason,"horse_flow_slot_already_free"),"empty native unload is not a newly retired occupant");
 for(unsigned mode=0;mode<17;++mode){auto bad=player;
  switch(mode){case 0:bad.after.owner.births.birth[1]+=0x100000000ull;break;case 1:bad.after.owner.sharedSerial++;break;
  case 2:bad.after.owner.births.sequence+=2;break;case 3:bad.after.owner.births.thread++;break;case 4:bad.after.owner.births.lifecycle++;break;
  case 5:bad.after.owner.births.generation++;break;case 6:bad.after.owner.births.sceneEpoch++;break;case 7:bad.after.owner.births.cs[0]++;break;
  case 8:bad.after.owner.carrierRecord[0]++;break;case 9:bad.after.owner.recipientRecord[1]++;break;case 10:bad.after.horse.unit++;break;
  case 11:bad.after.horse.receiver++;break;case 12:bad.after.horse.bridge.holder++;break;case 13:bad.after.horse.nativeIndex++;break;
  case 14:bad.after.horse.sdRecord++;break;case 15:bad.after.owner.at+=1000;break;case 16:bad.after.owner.births.ps[1]++;break;}
  r=f::Complete(bad.before,bad.after,bad.call,0);Check(!r.closed,"fresh natural owner/birth/lifecycle/ticket/thread/window/horse replacement refused");
 }
 for(unsigned mode=0;mode<8;++mode){auto bad=player;
  switch(mode){case 0:bad.call.caller++;break;case 1:bad.call.service++;break;case 2:bad.call.target++;break;case 3:bad.before.carrier.actual=1;break;
  case 4:bad.before.carrier.phase=3;break;case 5:bad.before.packet.body=0;break;case 6:bad.before.packet.unit=bad.before.horse.unit;break;case 7:bad.before.packet.variant=1;break;}
  r=f::Complete(bad.before,bad.after,bad.call,0);Check(!r.closed,"invented caller, local replica, state or packet does not become stock loading");
 }
 auto wrong=player;wrong.call.returned++;Check(!f::Complete(wrong.before,wrong.after,wrong.call,0).closed,"original result pointer ABI identity retained");
 wrong=player;wrong.after.packet.hash++;Check(!f::Complete(wrong.before,wrong.after,wrong.call,0).closed,"message identity must survive stock dispatcher");
 wrong=player;wrong.after.packet.kind=2;Check(!f::Complete(wrong.before,wrong.after,wrong.call,0).closed,"load must not become a fabricated unload");
 auto alias=player.before.owner;alias.births.birth[1]=alias.births.birth[0];Check(!f::SameOwner(alias,alias),"aliased birth receipts refused");
 alias=player.before.owner;alias.births.image=0;Check(!f::SameOwner(alias,alias),"absent actual image refused");
 alias=player.before.owner;alias.births.entry[1]=alias.births.entry[0];Check(!f::SameOwner(alias,alias),"aliased actual native model entries refused");
 Memory memory;const auto image=player.before.owner.births.image;
 for(const auto& p:mgscoop::horse_cargo_flow_contract::sites)for(std::size_t j=0;j<p.size;++j)memory.bytes[image+p.rva+j]=p.bytes[j];
 for(const auto& p:mgscoop::horse_cargo_native_v2_contract::sites)for(std::size_t j=0;j<p.size;++j)memory.bytes[image+p.rva+j]=p.bytes[j];
 for(const auto& p:mgscoop::horse_cargo_native_v2_contract::slots)memory.Put(image+p.rva,image+p.target);
 std::uintptr_t mismatch=0;Check(f::CodeGate(memory,image,0xa01d000,mismatch),"all actual original sender receiver cleanup and service chains pinned");
 for(const auto& p:mgscoop::horse_cargo_flow_contract::sites){auto changed=memory;changed.bytes[image+p.rva+p.size-1]^=1;
  Check(!f::CodeGate(changed,image,0xa01d000,mismatch),"every complete chained original source final byte refuses mutation");}
 memory.Put<std::uint64_t>(0x150000,0x5167c60042b5ull);memory.Put<std::uint16_t>(0x150008,1);
 memory.Put<std::uint32_t>(0x15000c,1);memory.Put<std::uintptr_t>(0x150018,0);memory.Put<std::uint8_t>(0x150020,0);
 // Native reserved wordA and other gaps are padding, not fabricated zeroes.
 memory.Put<std::uint16_t>(0x15000a,0xa55a);f::Packet packet{};
 Check(f::ReadPacket(memory,0x150000,packet)&&packet.body==1&&packet.kind==1,"stock sender padding is not promoted to a readiness gate");
 memory.bytes.erase(0x150020);Check(!f::ReadPacket(memory,0x150000,packet),"partial native packet refused");
 namespace hint=mgscoop::carry_horse_hint_read;
 hint::Hint h{};h.victim=1;h.near=6656;h.gate4=0x3030;h.gate6=2;h.prompt=7;h.action=0xd;
 Check(!std::strcmp(hint::Meaning(h),"candidate_gate_missing"),"actual nearby horse and prompt7 with f6=2 never announce usable load");
 h.gate6|=0x8000;
 Check(!std::strcmp(hint::Meaning(h),"candidate_input_blocked"),"stock prompt7 actionD false branch remains blocked");
 h.prompt=6;h.action=0xc;
 Check(!std::strcmp(hint::Meaning(h),"candidate_unqualified"),"prompt6 candidate still requires actual finder/input receipt");
 h.phase=0x17;Check(!std::strcmp(hint::Meaning(h),"stock_load_phase"),"real stock phase17 is distinguished from nearby presentation");
 h.phase=7;Check(!std::strcmp(hint::Meaning(h),"stock_unload_phase"),"stock unload phase separated from reusable load availability");
 h.phase=3;h.near=0xffff;
 Check(!std::strcmp(hint::Meaning(h),"no_horse_candidate"),"no nearby horse cannot become horse-load availability");
 h.near=0x1c00;Check(!std::strcmp(hint::Meaning(h),"no_horse_candidate"),"road vehicle candidate is not a horse hint");
 h.near=6656;char hintLine[161]{};
 Check(hint::Line(h,hintLine,sizeof(hintLine))&&std::strlen(hintLine)<=160,"compact hint reports raw stock fields without pointers or availability claims");
 Check(!hint::Line(h,hintLine,10),"hint truncation refuses");
 const auto record=player.before.owner.carrierRecord[0];
 memory.Put(record+0x78,h.victim);memory.Put(record+0x6a0,h.near);memory.Put(record+0x538,h.mount);
 memory.Put(record+0x6a2,h.flags);memory.Put(record+0x6a4,h.gate4);memory.Put(record+0x6a6,h.gate6);memory.Put(record+0x6a8,h.gate8);
 memory.Put(record+0x81,h.phase);memory.Put(record+0x380,h.closure);memory.Put(record+0x381,h.next);
 memory.Put(record+0x6ac,h.prompt);memory.Put(record+0x6b0,h.action);hint::Hint readHint{};
 Check(hint::Read(memory,player.before.owner,readHint)&&hint::Same(h,readHint),"raw hint reads close over genuine paired carrier record without calling stock kind5");
 memory.bytes.erase(record+0x6a6);Check(!hint::Read(memory,player.before.owner,readHint),"partial permission word refuses a hint");
 auto hintWrong=player.before.owner;hintWrong.births.birth[1]=hintWrong.births.birth[0];
 Check(!hint::Read(memory,hintWrong,readHint),"aliased current owners cannot supply a hint");
 auto changedHint=h;changedHint.action++;
 Check(!hint::Same(h,changedHint),"presentation action change refuses repeated hint equality");
 char line[161]{};r=f::Complete(player.before,player.after,player.call,0);
 Check(f::Line(r,1,line,sizeof(line))&&std::strlen(line)<=160&&std::strstr(line,"horseFlow;read=1"),"bounded no-pointer actual slot and residual link line");
 Check(!f::Line(r,1,line,12),"truncation refused");
 std::printf("%u passed, %u failed\n",passed,failed);return failed?1:0;
}
