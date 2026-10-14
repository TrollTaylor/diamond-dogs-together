#ifndef MGS_BRIDGE_TEST_HEADER
#define MGS_BRIDGE_TEST_HEADER "horse_player_cargo_bridge_read.hpp"
#endif
#include MGS_BRIDGE_TEST_HEADER
#ifndef MGS_CARGO_PAIR_TEST_HEADER
#define MGS_CARGO_PAIR_TEST_HEADER "horse_cargo_pair_read.hpp"
#endif
#include MGS_CARGO_PAIR_TEST_HEADER

#include <map>
#include <cstring>
#include <cstdio>
using namespace mgscoop::carry_plugin_read;
unsigned good=0,bad=0;void Check(bool b,const char* l){if(b)++good;else{++bad;std::printf("FAIL %s\n",l);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 bool flip=false;unsigned scans=0;
 template<class T>void Put(std::uintptr_t p,T x){unsigned char v[sizeof(T)]{};std::memcpy(v,&x,sizeof(x));for(unsigned i=0;i<sizeof(T);++i)bytes[p+i]=v[i];}
 void Zero(std::uintptr_t p,unsigned n){for(unsigned i=0;i<n;++i)bytes[p+i]=0;}
 bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {
  if(p==0x10000&&++scans==2&&flip)Put<std::uint16_t>(0x70000+0x78,1);
  auto* v=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto j=bytes.find(p+i);if(j==bytes.end())return false;v[i]=j->second;}return true;
 }
};
Scope S(){Scope s{};s.exactGate=s.admitted=s.ready=s.armed=true;s.image=0x140000000;s.player=0x10000;s.data=0x20000;s.state=0x30000;s.thread=s.ownerThread=9;s.mission=s.ownedMission=30050;s.epoch=s.ownedEpoch=2;s.generation=s.ownedGeneration=3;s.now=1000;s.poll=900;s.sequenceBefore=s.sequenceAfter=8;return s;}
Memory Fixture(){Memory m;auto s=S();
 m.Put(s.player,s.image+0x2295490);m.Put(s.player+0x60,s.data);m.Put(s.data,s.image+0x23ad970);m.Put(s.data+0x1e8,s.state);m.Put<std::uint32_t>(s.state+0x40,2);m.Put<std::uintptr_t>(s.player+0xa8,0);m.Put<std::uint32_t>(s.player+0x204,0x401);m.Put<std::uint32_t>(s.player+0x208,2);m.Put<std::uint32_t>(s.player+0x20c,2);m.Put<std::uint32_t>(s.player+0x214,0);
 for(auto pair:{std::pair<unsigned,std::uintptr_t>{0x70,0x31000},{0x78,0x32000},{0x90,0x33000},{0x378,0x40000},{0x50,0x34000},{0xd0,0xb0000}})m.Put(s.player+pair.first,pair.second);
 for(unsigned k=0;k<2;++k){auto block=std::uintptr_t(0x40000+k*0x6a8),ps=std::uintptr_t(0x50000+k*0x1000),node=std::uintptr_t(0x60000+k*0x1000),prim=std::uintptr_t(0x80000+k*0x1000),rec=std::uintptr_t(0x70000+k*0x1000),psrec=std::uintptr_t(0xa0000+k*0x1000),bitmap=std::uintptr_t(0x90000+k*0x1000);
  m.Put<std::uint16_t>(0x31000+k*2,static_cast<std::uint16_t>(k));m.Put<std::uint8_t>(0x32000+k,static_cast<std::uint8_t>(k));m.Put<std::uint8_t>(0x33000+k,3);m.Put(0x34000+k*8,block);m.Put(block,s.image+0x2295210);m.Put(block+0x138,s.player);m.Put<std::uint32_t>(block+8,1);m.Put<std::uint32_t>(block+0xc,k);m.Put(block+0x78,ps);m.Put(ps,s.image+0x23b0c70);m.Put(ps+0x408,block);m.Put<std::uint32_t>(ps+8,k);
  m.Put(block+0x320,node);m.Put(node,s.image+0x2306628);m.Put<std::uint32_t>(node+0x20,1);m.Put<std::uint32_t>(node+0x24,k);m.Put<std::uint32_t>(node+0x2c,0x32);m.Put(block+0xb8,prim);m.Put(prim,s.image+0x23b9ac0);m.Put(prim+0x10,s.image+0x23b9b18);m.Put(prim+0x30,s.image+0x23b9b50);m.Put(prim+8,block);m.Put(prim+0x38,node);m.Put(block+0x548,prim+0x30);m.Put(node+0x38,prim+0x30);m.Put(prim+0x68,rec);m.Put(node+8,bitmap);m.Put<std::uint32_t>(bitmap,0);
  m.Put<std::uint16_t>(rec+0x78,0xffff);m.Put<std::uint16_t>(rec+0x7c,0xffff);m.Put<std::uint16_t>(rec+0x7a,static_cast<std::uint16_t>(k));m.Zero(rec+0x380,5);m.Zero(rec+0x6a2,8);m.Put(ps+0x280,psrec);m.Put<std::uint32_t>(psrec,0);
 }
 m.Put<std::uintptr_t>(0xb0000,s.image+0x22e8f00);m.Put<std::uintptr_t>(0xb0010,s.player);m.Put<std::uintptr_t>(0xb0a60,0x80000);m.Put<std::uint16_t>(0xb0aa0,1);m.Put<std::uint32_t>(0xb0a6c,0x2d);m.Put<std::uint32_t>(0xb1268,0x2d);m.Put<std::uint32_t>(0xb0ae4,0);m.Put<std::uint32_t>(0xb1288,0x20);return m;
}

namespace rr=mgscoop::carry_recipient_read;
namespace pc=mgscoop::horse_player_cargo_bridge_read;
Memory RecipientFixture(){auto m=Fixture();auto s=S();
 for(unsigned id=0;id<2;++id){const auto block=std::uintptr_t(0x40000+id*0x6a8),node=std::uintptr_t(0xc0000+id*0x1000),prim=std::uintptr_t(0xd0000+id*0x1000),rec=std::uintptr_t(0xe0000+id*0x1000),desc=std::uintptr_t(0xf0000+id*0x1000),bits=std::uintptr_t(0x110000+id*0x1000);
  m.Put(block+0x210,node);m.Put(block+0x438,prim+0x30);m.Put(node,s.image+0x2306628);m.Put<std::uint32_t>(node+0x20,1);m.Put<std::uint32_t>(node+0x24,id);m.Put<std::uint32_t>(node+0x2c,0x10);m.Put(node+0x38,prim+0x30);m.Put(prim,s.image+0x23b8d78);m.Put(prim+0x10,s.image+0x23b8da0);m.Put(prim+0x30,s.image+0x23b8dd0);m.Put(prim+0x48,s.image+0x23b8e10);m.Put(prim+8,block);m.Put(prim+0x38,node);m.Put(prim+0x88,rec);m.Put(prim+0x58,desc);m.Put(desc,s.image+0x645940);m.Put(desc+0x10,rec);m.Put<std::uint64_t>(desc+0x18,0x60);m.Put<std::uint64_t>(desc+0x20,1);m.Put(node+8,bits);m.Put(node+0x10,bits+4);m.Put<std::uint32_t>(bits,0);m.Put<std::uint32_t>(bits+4,0);m.Zero(rec,0x60);m.Put<std::uint16_t>(rec+0x30,0xffff);m.Put<std::uintptr_t>(prim+0x80,0x120000+id*0x1000);
 }return m;
}

Memory PhysicalFixture(){auto m=RecipientFixture();const auto s=S();
 for(unsigned k=0;k<2;++k){const auto block=std::uintptr_t(0x40000+k*0x6a8),ps=std::uintptr_t(0x50000+k*0x1000);
  const auto cs=std::uintptr_t(0x2c0000+k*0x1000),entry=std::uintptr_t(0x2c4000+k*0x1000),entity=std::uintptr_t(0x2c8000+k*0x1000);
  const auto list=std::uintptr_t(0x2ca000+k*0x1000),component=std::uintptr_t(0x2d0000+k*0x1000),physical=std::uintptr_t(0x2d6000+k*0x1000),transform=std::uintptr_t(0x2d8000+k*0x1000);
  m.Put(block+0x10,cs);m.Put(ps+0x238,cs);m.Put(cs,s.image+0x22e6070);m.Put<std::uint32_t>(cs+8,1);m.Put<std::uint32_t>(cs+0xc,k);m.Put(cs+0x10,entry);
  m.Put(entry+0x60,transform);m.Put(entry+0x70,entity);m.Put<std::uint32_t>(entity+0x28,1);m.Put(entity+0x30,list);m.Put(list,component);
  m.Put(component,s.image+0x24c97f8);m.Put<std::uint32_t>(component+0x10,0x7f1402f4);m.Put(component+0x68,transform);m.Put(component+0x80,s.image+0x24c9830);
  m.Put(ps+0x270,physical);m.Put<std::uintptr_t>(physical,component+0x80);m.Put<std::uint32_t>(component+0xd0,0);
  for(unsigned j=0;j<16;++j)m.Put<float>(component+0x90+j*4,j%5==0?1.0f:0.0f);
 }return m;
}
inline constexpr std::uint64_t NativeRecognized[]={0x85d76e43b4d2ull,0x1481a04bdc75ull,0x17181e562e2bull,0x2695ebdaf98dull,0x51addebcd428ull,0xb217b8afae92ull,0xd7b458464921ull,0xf6b9cb46d3eeull};

Memory BridgeFixture(){auto m=PhysicalFixture();const auto s=S();
 m.Put<std::uintptr_t>(s.data+0x18,0x36000);m.Put<std::uint16_t>(0x36000,1);m.Put<std::uint16_t>(0x36002,5);
 for(unsigned id=0;id<2;++id){const auto prim=std::uintptr_t(0xd0000+id*0x1000),payload=std::uintptr_t(0x300000+id*0x1000);
  const auto header=std::uintptr_t(0x302000+id*0x1000),desc=std::uintptr_t(0x304000+id*0x1000),records=std::uintptr_t(0x306000+id*0x1000);
  m.Put(prim+0x80,payload+0x20);m.Put(prim+0x78,header);m.Put(header+0x10,payload);
  m.Put(payload,s.image+0x2339c18);m.Put(payload+0x20,s.image+0x2339c50);m.Put(payload+0x50,records);
  m.Put(payload+0x30,desc);m.Put<std::uint32_t>(payload+0x68,id);m.Put<std::uintptr_t>(payload+0x60,0x308000+id*0x1000);
  m.Put(desc,s.image+0x645940);m.Put(desc+0x10,records);m.Put<std::uint64_t>(desc+0x18,0x24);m.Put<std::uint64_t>(desc+0x20,1);
  m.Zero(records,0x24);m.Put<float>(records+0x18,1);
  const auto component=std::uintptr_t(0x2d0000+id*0x1000),profiles=std::uintptr_t(0x30a000+id*0x1000),profile=std::uintptr_t(0x30c000+id*0x1000);
  m.Put<std::uint16_t>(component+0x40,9);m.Put(component+0x48,profiles);m.Zero(profiles,9*8);
  m.Put(profiles+8*8,profile);m.Put<std::uint32_t>(profile+0xc,UINT32_MAX);m.Put<std::uint16_t>(component+0x70,0);
 }
 return m;
}
Memory CarryFixture(unsigned carried=1){auto m=BridgeFixture();const unsigned carrier=1-carried;
 m.Put<std::uint32_t>(0x110000+carried*0x1000,1);m.Put<std::uint32_t>(0x90000+carrier*0x1000,1);
 m.Put<std::uintptr_t>(0xe0028+carried*0x1000,0x70000+carrier*0x1000);
 m.Put<std::uint16_t>(0xe0030+carried*0x1000,static_cast<std::uint16_t>(carrier));
 m.Put<std::uint8_t>(0xe0010+carried*0x1000,3);m.Put<std::uint16_t>(0x7007c+carrier*0x1000,static_cast<std::uint16_t>(carried));
 m.Put<std::uint32_t>(0xa0000+carrier*0x1000,1u<<22);m.Put<std::uint16_t>(0x36000+carried*2,0x2b);
 m.Put<std::uint64_t>(0xe0050+carried*0x1000,0xd7b458464921ull);m.Put<std::uint32_t>(0x30c00c+carried*0x1000,3);
 for(unsigned j=0;j<16;++j)m.Put<float>(0x70110+carrier*0x1000+j*4,j%5==0?1.0f:0);
 return m;
}

namespace hn=mgscoop::horse_cargo_native_read;
struct HorseFixture {Memory memory;hn::Census census{};std::uintptr_t image=0x140000000;
 HorseFixture(){auto& c=census;c.receiver=0x4f0020;c.component=0x4f5000;c.records=0x4f8000;c.kind=0x4fa000;c.realized_indices=0x4fc000;c.class_index=13;c.count=c.declared_total=c.realized_count=c.declared_realized=2;c.realized_mask=3;
  memory.Put<std::uintptr_t>(0x4f0000,image+0x2362620);memory.Put(c.receiver,image+0x2362650);memory.Put<std::uint16_t>(c.receiver+8,13);memory.Put(c.receiver+0x18,c.kind);memory.Put<std::uint32_t>(c.receiver+0xc,2);memory.Put(c.receiver+0x68,c.component);memory.Put(c.component,image+0x2336cf0);memory.Put(c.component-0x20,image+0x2367b80);memory.Put<std::uint32_t>(c.component+0x10,2);memory.Put(c.component+8,c.records);memory.Put<std::uint32_t>(c.kind+0x44,2);memory.Put(c.kind+0x28,c.realized_indices);
  memory.Put<std::uint16_t>(c.realized_indices,1);memory.Put<std::uint16_t>(c.realized_indices+2,0);memory.Put<std::uint32_t>(c.receiver+0xf0,1);memory.Put<std::uintptr_t>(c.receiver+0x110,0x500000);
  for(unsigned native=0;native<2;++native){const unsigned logical=1-native;const auto primary=std::uintptr_t(0x501000+native*0x1000),descriptor=std::uintptr_t(0x505000+native*0x1000),record=std::uintptr_t(0x509000+native*0x1000),cs=std::uintptr_t(0x50d000+native*0x1000);const auto sd=c.records+logical*0x2f0;
   const auto service=std::uintptr_t(0x511000+native*0x1000),row=std::uintptr_t(0x515000+native*0x1000),holder=std::uintptr_t(0x519000+native*0x1000);
   memory.Put<std::uintptr_t>(0x500000+native*8,service);memory.Put(service,image+0x22eaf70);memory.Put(service+0xa8,c.receiver);
   memory.Put(service+0x10,cs);memory.Put<std::uint32_t>(service+0x54,native);memory.Put<std::uint32_t>(service+0x58,1);
   for(unsigned k=0;k<7;++k)memory.Put<std::uintptr_t>(service+0xb0+k*8,0);
   memory.Put(service+0xb0+3*8,row);memory.Put(row,image+0x2306628);memory.Put(row+0x30,holder);memory.Put(holder+0x10,primary);
   memory.Put(row+0x38,primary+0x20);memory.Put(primary+0x28,row);memory.Put(primary,image+0x2363c48);memory.Put(primary+0x20,image+0x2363c80);memory.Put(primary+0x70,c.receiver);memory.Put(primary+0x78,descriptor);memory.Put<std::uint32_t>(primary+0x98,native);memory.Put<std::uint32_t>(descriptor+0x54,native);memory.Put<std::uint32_t>(descriptor+0x58,1);memory.Put(descriptor+0x10,cs);memory.Put(primary+0x80,cs);memory.Put(primary+0xc0,record);memory.Put(record,image+0x2337a88);memory.Put(record+0x268,sd);memory.Put<std::uint32_t>(record+0x284,logical);memory.Put(sd,record+0x70);memory.Put(record+0x70,image+0x2363af8);memory.Put<std::uint16_t>(record+0x70+0x164,0xffff);memory.Put<std::uint16_t>(sd+0x10,0xffff);memory.Put<std::uint16_t>(sd+0xe,0xffff);memory.Put<std::uint8_t>(sd+0x2de,0);memory.Put<std::uint8_t>(sd+0x2ec,0);memory.Put<std::uint8_t>(sd+0x2dd,0);memory.Put<std::uint8_t>(record+0x260,1);
   for(unsigned k=0;k<16;++k)memory.Put<float>(record+0x70+0x90+k*4,k%5==0?1.f:0.f);
   c.actors[logical].record=sd;c.actors[logical].ride_link=record+0x70;c.actors[logical].game_id=static_cast<std::uint16_t>(0x1a00|logical);c.actors[logical].realized=true;
  }
 }
};


namespace pair=mgscoop::horse_cargo_pair_read;
struct PairFixture {
 Memory memory{};pair::Current current{};
 PairFixture(bool carrying=false,unsigned victim=1){HorseFixture h;memory=carrying?CarryFixture(victim):BridgeFixture();
  for(const auto& item:h.memory.bytes){if(memory.bytes.count(item.first))std::abort();memory.bytes.insert(item);}
  current.carry=S();current.horse=h.census;current.buddySelection=0x990000;current.activeBuddy=1;current.loadingGeneration=21;
  auto& b=current.births;const auto& s=current.carry;
  b.image=s.image;b.player=s.player;b.data=s.data;b.state=s.state;b.thread=s.thread;b.sequence=s.sequenceBefore;
  b.lifecycle=71;b.generation=12;b.sceneEpoch=13;
  for(unsigned k=0;k<2;++k){b.birth[k]=0x100000005ull+k;b.block[k]=0x40000+k*0x6a8;b.ps[k]=0x50000+k*0x1000;b.cs[k]=0x2c0000+k*0x1000;b.entry[k]=0x2c4000+k*0x1000;}
 }
 void Slot(unsigned id){memory.Put<std::uint16_t>(0x50a070+0x164,static_cast<std::uint16_t>(id));memory.Put<std::uint16_t>(0x4f8000+0x10,static_cast<std::uint16_t>(id));memory.Put<std::uint8_t>(0x4f8000+0x2de,1);}
};
struct Source {pair::Current value{};unsigned calls=0;unsigned mutation=0;bool operator()(pair::Current& out,const char*& why) noexcept {
 out=value;++calls;why="source";
 if(calls==3){switch(mutation){case 1:out.carry.sequenceBefore+=2;out.carry.sequenceAfter+=2;out.births.sequence+=2;break;
 case 2:out.carry.batch=true;break;case 3:++out.births.birth[1];break;case 4:++out.births.lifecycle;break;
 case 5:++out.carry.generation;++out.carry.ownedGeneration;break;case 6:++out.carry.epoch;++out.carry.ownedEpoch;break;
 case 7:++out.buddySelection;break;case 8:out.horse.actors[0].ride_link+=0x440;break;
 case 9:++out.carry.mission;++out.carry.ownedMission;break;case 10:out.carry.armed=false;break;
 case 11:++out.carry.thread;++out.carry.ownerThread;++out.births.thread;break;
 case 12:out.births.birth[1]+=0x100000000ull;break;case 13:++out.births.cs[0];break;
 case 14:out.carry.now+=1000;break;case 15:++out.births.generation;break;case 16:++out.births.sceneEpoch;break;
 case 17:++out.horse.controller;break;case 18:++out.loadingGeneration;break;default:break;}}
 return true;
}};
template<class Sites>void SitesCode(Memory& m,std::uintptr_t image,const Sites& sites){for(const auto& p:sites)for(std::size_t j=0;j<p.size;++j)m.bytes[image+p.rva+j]=p.bytes[j];}
template<class Slots>void SlotsCode(Memory& m,std::uintptr_t image,const Slots& slots){for(const auto& p:slots)m.Put<std::uintptr_t>(image+p.rva,image+p.target);}
Memory Code(){Memory m;const auto image=S().image;
 SitesCode(m,image,mgscoop::horse_cargo_census_contract::sites);SitesCode(m,image,mgscoop::carry_plugin_contract::sites);SitesCode(m,image,mgscoop::carry_owned_handshake_contract::sites);
 SitesCode(m,image,mgscoop::horse_player_physical_contract::sites);SitesCode(m,image,mgscoop::horse_physical_profile_contract::sites);
 SitesCode(m,image,mgscoop::horse_player_cargo_bridge_contract::sites);SitesCode(m,image,mgscoop::horse_cargo_native_v2_contract::sites);
 for(const auto& p:mgscoop::carry_plugin_contract::slots)m.Put<std::uintptr_t>(image+p.vtable_rva+p.offset,image+p.target_rva);
 SlotsCode(m,image,mgscoop::carry_owned_handshake_contract::slots);SlotsCode(m,image,mgscoop::horse_player_physical_contract::slots);
 SlotsCode(m,image,mgscoop::horse_physical_profile_contract::slots);SlotsCode(m,image,mgscoop::horse_player_cargo_bridge_contract::slots);
 SlotsCode(m,image,mgscoop::horse_cargo_native_v2_contract::slots);return m;
}
int main(){pair::Observation o{};const char* why="none";
 auto f=PairFixture{};Source source{f.current};
 Check(pair::Observe(f.memory,source,0x1a00,o,why)&&source.calls==3,"three fresh genuine context captures bracket current native reads");
 Check(o.horse.nativeIndex==1&&o.horse.cargo==0xffff&&!o.body[0].slot&&!o.body[1].slot,"actual ordinal lookup differs from logical Horse ID; free slot stays free");
 Check(o.birth[0]==0x100000005ull&&o.birth[1]==0x100000006ull,"true birth serials retain high32 bits");
 Check(!o.horse.anchorFresh&&!o.body[0].horseMatrix&&!o.body[1].horseMatrix,"free finite identity anchor never asserts loaded or fresh");
 for(unsigned victim=0;victim<2;++victim){auto c=PairFixture{true,victim};Source captured{c.current};
  Check(pair::Observe(c.memory,captured,0x1a00,o,why)&&o.player.body[victim].currentCarry&&o.player.body[victim].matrixMatches,"both natural recipient roles retain native carrier-bone source");
  Check(!o.body[victim].slot&&!o.body[victim].horseMatrix&&o.body[victim].oldCarryLink,"native shoulder carry is not Horse cargo");
  c.Slot(victim);captured={c.current};Check(pair::Observe(c.memory,captured,0x1a00,o,why)&&o.body[victim].slot&&o.body[victim].horseMatrix&&o.body[victim].oldCarryLink,"coherent Horse slot and equality remain distinct from uncleared old carry");
  const auto matrix=std::uintptr_t(0x50a070+0x90);c.memory.Put<float>(matrix+12*4,6.0f);captured={c.current};
  Check(pair::Observe(c.memory,captured,0x1a00,o,why)&&o.body[victim].slot&&!o.body[victim].horseMatrix,"slot acceptance does not invent physical matrix follow");
 }
 auto native=PairFixture{};native.Slot(1);Source slot{native.current};
 Check(pair::Observe(native.memory,slot,0x1a00,o,why)&&o.body[1].slot&&o.body[1].horseMatrix&&!o.body[1].oldCarryLink&&!o.player.body[1].currentCarry,"retired old carry and current native slot recorded without attachment permission");
 auto other=PairFixture{};other.Slot(700);Source unrelated{other.current};
 Check(pair::Observe(other.memory,unrelated,0x1a00,o,why)&&!o.body[0].slot&&!o.body[1].slot,"unrelated occupied native unit does not become Player cargo");
 for(unsigned mutate=1;mutate<=18;++mutate){Source race{f.current,0,mutate};Check(!pair::Observe(f.memory,race,0x1a00,o,why),"last fresh capture refuses changed scope, serial, scene, sequence, batch, buddy and source");}
 for(unsigned k=0;k<2;++k){auto wrong=f.current;wrong.births.cs[k]++;Source mismatch{wrong};Check(!pair::Observe(f.memory,mismatch,0x1a00,o,why)&&!std::strcmp(why,"cargo_birth_current"),"current birth receipt must name actual body model");}
 auto wrong=f.current;wrong.births.birth[1]=wrong.births.birth[0];Source alias{wrong};Check(!pair::Observe(f.memory,alias,0x1a00,o,why),"aliased birth identities refused");
 wrong=f.current;wrong.births.thread++;Source foreignThread{wrong};Check(!pair::Observe(f.memory,foreignThread,0x1a00,o,why),"Lua observer never rewrites thread authority to physical callback");
 wrong=f.current;wrong.carry.sequenceBefore=wrong.carry.sequenceAfter=9;wrong.births.sequence=9;Source odd{wrong};Check(!pair::Observe(f.memory,odd,0x1a00,o,why),"odd native window refused");
 auto parts=f;parts.memory.Put<std::uint8_t>(0x33001,1);Source lostParts{parts.current};Check(!pair::Observe(parts.memory,lostParts,0x1a00,o,why),"actual current Player parts loss refused without synthetic parts flags");
 auto missing=f;missing.memory.bytes.erase(0x2d1000+0x90+60);Source unreadable{missing.current};Check(!pair::Observe(missing.memory,unreadable,0x1a00,o,why),"partial final physical matrix refused");
 wrong=f.current;wrong.carry.ownedGeneration++;Source wrongScene{wrong};Check(!pair::Observe(f.memory,wrongScene,0x1a00,o,why),"genuine carry-owned generation mismatch refused");
 auto retired=native;retired.memory.Put<std::uint16_t>(0x4f8010,0xffff);Source incoherent{retired.current};Check(!pair::Observe(retired.memory,incoherent,0x1a00,o,why),"one cleared stock occupancy field fails coherent slot read");
 struct RaceReader {Memory& m;unsigned hits=0;bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {
  if(p==0x50a070+0x164&&++hits==2){m.Put<std::uint16_t>(p,0);m.Put<std::uint16_t>(0x4f8010,0);}
  return m(p,out,n);}};auto changing=native;RaceReader rr{changing.memory};Source race{changing.current};
 Check(!pair::Observe(rr,race,0x1a00,o,why)&&!std::strcmp(why,"cargo_pair_changed"),"coherent native cargo replacement across player/horse samples refused");
 Source normal{f.current};Check(pair::Observe(f.memory,normal,0x1a00,o,why),"diagnostic baseline restored");
 char line[161]{};Check(pair::Line(o,true,why,line,sizeof(line))&&std::strlen(line)<=160&&std::strstr(line,"cargoPair;ok=1"),"compact relationship status never emits addresses");
 Check(!pair::Line(o,true,why,line,12),"truncated diagnostic refused");
 auto moving=f.current;moving.horse.actors[0].position[0]+=9;moving.carry.now+=1;Check(pair::SameCurrent(f.current,moving),"moving source position and observation time alone do not fabricate owner replacement");
 auto code=Code();std::uintptr_t mismatch=0;Check(pair::CodeGate(code,S().image,0xa01d000,mismatch),"all six complete normalized-original code gates admitted");
 for(const auto& p:mgscoop::horse_cargo_census_contract::sites){auto changed=code;changed.bytes[S().image+p.rva+p.size-1]^=1;Check(!pair::CodeGate(changed,S().image,0xa01d000,mismatch),"every capacity pin final byte participates in normalized gate");}
 for(const auto& p:mgscoop::horse_player_cargo_bridge_contract::sites){auto changed=code;changed.bytes[S().image+p.rva+p.size-1]^=1;Check(!pair::CodeGate(changed,S().image,0xa01d000,mismatch),"every final bridge byte participates in combined gate");}
 for(const auto& p:mgscoop::horse_cargo_native_v2_contract::sites){auto changed=code;changed.bytes[S().image+p.rva+p.size-1]^=1;Check(!pair::CodeGate(changed,S().image,0xa01d000,mismatch),"every final Horse native byte participates in combined gate");}
 for(const auto& p:mgscoop::horse_player_cargo_bridge_contract::slots){auto changed=code;changed.Put<std::uintptr_t>(S().image+p.rva,0);Check(!pair::CodeGate(changed,S().image,0xa01d000,mismatch),"every helper slot target participates in combined gate");}
 for(const auto& p:mgscoop::horse_cargo_native_v2_contract::slots){auto changed=code;changed.Put<std::uintptr_t>(S().image+p.rva,0);Check(!pair::CodeGate(changed,S().image,0xa01d000,mismatch),"every Horse slot target participates in combined gate");}
 std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
