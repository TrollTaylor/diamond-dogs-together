#include "horse_player_physical_read.hpp"
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


#include "horse_cargo_native_read.hpp"
namespace hn=mgscoop::horse_cargo_native_read;
struct HorseFixture {Memory memory;hn::Census census{};std::uintptr_t image=0x140000000;
 HorseFixture(){auto& c=census;c.receiver=0xf0020;c.component=0xf5000;c.records=0xf8000;c.kind=0xfa000;c.realized_indices=0xfc000;c.class_index=13;c.count=c.declared_total=c.realized_count=c.declared_realized=2;c.realized_mask=3;
  memory.Put<std::uintptr_t>(0xf0000,image+0x2362620);memory.Put(c.receiver,image+0x2362650);memory.Put<std::uint16_t>(c.receiver+8,13);memory.Put(c.receiver+0x18,c.kind);memory.Put<std::uint32_t>(c.receiver+0xc,2);memory.Put(c.receiver+0x68,c.component);memory.Put(c.component,image+0x2336cf0);memory.Put(c.component-0x20,image+0x2367b80);memory.Put<std::uint32_t>(c.component+0x10,2);memory.Put(c.component+8,c.records);memory.Put<std::uint32_t>(c.kind+0x44,2);memory.Put(c.kind+0x28,c.realized_indices);
  memory.Put<std::uint16_t>(c.realized_indices,1);memory.Put<std::uint16_t>(c.realized_indices+2,0);memory.Put<std::uint32_t>(c.receiver+0xf0,1);memory.Put<std::uintptr_t>(c.receiver+0x110,0x100000);
  for(unsigned native=0;native<2;++native){const unsigned logical=1-native;const auto primary=std::uintptr_t(0x101000+native*0x1000),descriptor=std::uintptr_t(0x105000+native*0x1000),record=std::uintptr_t(0x109000+native*0x1000),cs=std::uintptr_t(0x10d000+native*0x1000);const auto sd=c.records+logical*0x2f0;
   const auto service=std::uintptr_t(0x111000+native*0x1000),row=std::uintptr_t(0x115000+native*0x1000),holder=std::uintptr_t(0x119000+native*0x1000);
   memory.Put<std::uintptr_t>(0x100000+native*8,service);memory.Put(service,image+0x22eaf70);memory.Put(service+0xa8,c.receiver);
   memory.Put(service+0x10,cs);memory.Put<std::uint32_t>(service+0x54,native);memory.Put<std::uint32_t>(service+0x58,1);
   for(unsigned k=0;k<7;++k)memory.Put<std::uintptr_t>(service+0xb0+k*8,0);
   memory.Put(service+0xb0+3*8,row);memory.Put(row,image+0x2306628);memory.Put(row+0x30,holder);memory.Put(holder+0x10,primary);
   memory.Put(row+0x38,primary+0x20);memory.Put(primary+0x28,row);memory.Put(primary,image+0x2363c48);memory.Put(primary+0x20,image+0x2363c80);memory.Put(primary+0x70,c.receiver);memory.Put(primary+0x78,descriptor);memory.Put<std::uint32_t>(primary+0x98,native);memory.Put<std::uint32_t>(descriptor+0x54,native);memory.Put<std::uint32_t>(descriptor+0x58,1);memory.Put(descriptor+0x10,cs);memory.Put(primary+0x80,cs);memory.Put(primary+0xc0,record);memory.Put(record,image+0x2337a88);memory.Put(record+0x268,sd);memory.Put<std::uint32_t>(record+0x284,logical);memory.Put(sd,record+0x70);memory.Put(record+0x70,image+0x2363af8);memory.Put<std::uint16_t>(record+0x70+0x164,0xffff);memory.Put<std::uint16_t>(sd+0x10,0xffff);memory.Put<std::uint16_t>(sd+0xe,0xffff);memory.Put<std::uint8_t>(sd+0x2de,0);memory.Put<std::uint8_t>(sd+0x2ec,0);memory.Put<std::uint8_t>(sd+0x2dd,0);memory.Put<std::uint8_t>(record+0x260,1);
   for(unsigned k=0;k<16;++k)memory.Put<float>(record+0x70+0x90+k*4,k%5==0?1.f:0.f);
   c.actors[logical].record=sd;c.actors[logical].ride_link=record+0x70;c.actors[logical].game_id=static_cast<std::uint16_t>(0x1a00|logical);c.actors[logical].realized=true;
  }
 }
};
int main(){HorseFixture f;hn::Observation o{};const char* why="none";
 Check(hn::Observe(f.memory,f.image,f.census,0x1a00,o,why),"fully typed free native Horse cargo observed twice");Check(o.nativeIndex==1&&o.nativeBase==1&&o.primary==0x102000&&o.logicalIndex==0,"shuffled logical6656 maps actual nativeOrdinal1 and second controller");Check(o.unit==0x10a070&&o.sdRecord==0xf8000&&!o.anchorFresh,"exact raw/unit/Sd link and finite identity never claims producer freshness");
 Check(hn::Observe(f.memory,f.image,f.census,0x1a01,o,why)&&o.nativeIndex==0&&o.primary==0x101000,"second logical horse maps nativeOrdinal0");
 auto cargo=f;cargo.memory.Put<std::uint16_t>(0x10a070+0x164,1);cargo.memory.Put<std::uint16_t>(0xf8000+0x10,1);cargo.memory.Put<std::uint8_t>(0xf8000+0x2de,1);Check(hn::Observe(cargo.memory,cargo.image,cargo.census,0x1a00,o,why)&&o.cargo==1&&o.occupied==1&&!o.anchorFresh,"coherent actual Player1 cargo recognized without native method calls");cargo.memory.Put<std::uint16_t>(0xf8000+0xe,0);cargo.memory.Put<std::uint8_t>(0xf8000+0x2dd,1);Check(hn::Observe(cargo.memory,cargo.image,cargo.census,0x1a00,o,why)&&o.driver==0&&o.driverOccupied==1,"driver and cargo are independent stock slots");
 auto skip=cargo;skip.memory.Put<std::uint8_t>(0xf8000+0x2ec,1);Check(hn::Observe(skip.memory,skip.image,skip.census,0x1a00,o,why)&&o.anchorSkip==1&&!o.anchorFresh,"native2EC skip observed without fabricated fresh anchor");
 auto base=f;base.memory.Put<std::uintptr_t>(0x102000,base.image+0x2363b60);base.memory.Put<std::uintptr_t>(0x102020,base.image+0x2363b90);Check(hn::Observe(base.memory,base.image,base.census,0x1a00,o,why),"full base constructor address-point pair also typed");
 struct Mutation {std::uintptr_t at,value;unsigned width;const char* name;};
 for(const auto x:{Mutation{0xf0020,0x1423624a8,8,"wrong family address point"},{0xf0028,12,2,"wrong category"},{0xfc000,0,2,"duplicate realized logical map"},{0xf0110,0,4,"zero group divisor"},{0xf0130,0,8,"null native controller array"},{0x100008,0x102000,8,"motion payload substituted for service"},{0x102000,0x142363c50,8,"unverified adjacent native class"},{0x102020,0x142363b90,8,"mismatched derived/base secondary pair"},{0x102070,0xf0000,8,"foreign family owner"},{0x102098,0,4,"descriptor and native base disagree"},{0x106058,0,4,"zero native records"},{0x106058,3,4,"unbounded native records"},{0x102080,0x10d000,8,"wrong current character model"},{0x10a000,0x142337a80,8,"adjacent raw record type"},{0x10a268,0xf82f0,8,"raw backlink borrowed from other logical horse"},{0x10a284,1,4,"wrong logical actor mapping"},{0xf8000,0x109070,8,"other horse native unit borrowed"},{0x10a070,0x142363ae8,8,"unit still base construction type"},{0x10a1d4,1,2,"unit cargo disagrees with Sd target"},{0xf82de,2,1,"cargo flag invalid"},{0xf82ec,2,1,"native anchor skip flag invalid"},{0xf82dd,2,1,"driver occupied flag invalid"},{0x10a100,0x7fc00000,4,"nonfinite cargo anchor"}}){auto n=f;if(x.width==8)n.memory.Put<std::uintptr_t>(x.at,x.value);else if(x.width==4)n.memory.Put<std::uint32_t>(x.at,static_cast<std::uint32_t>(x.value));else if(x.width==2)n.memory.Put<std::uint16_t>(x.at,static_cast<std::uint16_t>(x.value));else n.memory.Put<std::uint8_t>(x.at,static_cast<std::uint8_t>(x.value));Check(!hn::Observe(n.memory,n.image,n.census,0x1a00,o,why),x.name);}
 Check(hn::Observe(f.memory,f.image,f.census,0x1a00,o,why)&&o.service==0x112000&&o.bridge.row==0x116000&&o.bridge.holder==0x11a000&&o.bridge.selected==3,"actual22EAF70 service bridged through retained row/holder to message interface");
 for(const auto x:{Mutation{0x112000,0x1422eaf78,8,"wrong current motion service type"},{0x1120a8,0xf0000,8,"service borrows foreign Horse family"},
  {0x112010,0x10d000,8,"service model differs from motion payload model"},{0x112054,0,4,"service native base differs from payload descriptor"},
  {0x116000,0x142306620,8,"resource row adjacent base type refused"},{0x116030,0,8,"null resource holder refused"},
  {0x11a010,0x101000,8,"other Horse payload borrowed from holder"},{0x116038,0x102000,8,"message interface adjustment required"},
  {0x102028,0x115000,8,"message interface resource-row backlink changed"},{0x1120b0,0x116000,8,"duplicate same-index Horse module refused"}}){auto n=f;
  if(x.width==8)n.memory.Put<std::uintptr_t>(x.at,x.value);else n.memory.Put<std::uint32_t>(x.at,static_cast<std::uint32_t>(x.value));
  Check(!hn::Observe(n.memory,n.image,n.census,0x1a00,o,why),x.name);}
 auto occupiedWithoutBody=f;occupiedWithoutBody.memory.Put<std::uint8_t>(0xf82de,1);Check(!hn::Observe(occupiedWithoutBody.memory,occupiedWithoutBody.image,occupiedWithoutBody.census,0x1a00,o,why),"occupied cargo cannot have FFFF target");
 auto stale=f;stale.census.actors[0].ride_link++;Check(!hn::Observe(stale.memory,stale.image,stale.census,0x1a00,o,why),"stale supplied typed horse census refused");
 struct Race {Memory& memory;unsigned reads=0;bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {if(p==0xf0020&&++reads==2){memory.Put<std::uint16_t>(0x10a1d4,0);memory.Put<std::uint16_t>(0xf8010,0);memory.Put<std::uint8_t>(0xf82de,1);}return memory(p,out,n);}};auto changed=cargo;Race race{changed.memory};Check(!hn::Observe(race,changed.image,changed.census,0x1a00,o,why)&&!std::strcmp(why,"horse_cargo_changed"),"coherent occupancy changes across double reads refused");
 struct Source {hn::Census first{};unsigned calls=0;bool change=false;bool operator()(hn::Census& out,const char*& reason) noexcept {
  out=first;if(++calls==2&&change)out.actors[0].ride_link+=0x440;reason="fixture_source";return true;}};
 auto current=f;Source source{current.census};Check(hn::ObserveCurrent(current.memory,current.image,source,0x1a00,o,why)&&source.calls==2,"full source closure before/after paired native observations and final recheck");
 Source replaced{current.census,0,true};Check(!hn::ObserveCurrent(current.memory,current.image,replaced,0x1a00,o,why)&&!std::strcmp(why,"horse_source_changed"),"same-scene current actor/unit replacement detected by source closure");
 auto poseOnly=current.census;poseOnly.actors[0].position[0]+=1;Check(hn::SameSource(current.census,poseOnly),"moving source position alone does not invalidate retained typed identity");
 auto topology=current.census;topology.controller++;Check(!hn::SameSource(current.census,topology),"source owner controller replacement detected");
 char serviceLine[161]{},candidatesLine[161]{};Check(hn::Observe(current.memory,current.image,current.census,0x1a00,o,why)&&
  mgscoop::horse_native_service_bridge::Line(o.bridge,true,why,serviceLine,sizeof(serviceLine))&&
  mgscoop::horse_native_service_bridge::CandidatesLine(o.bridge,candidatesLine,sizeof(candidatesLine)),"separate bounded current service and holder candidate diagnostic lines");
 Memory code;for(const auto& site:mgscoop::horse_cargo_native_v2_contract::sites)for(std::size_t k=0;k<site.size;++k)code.bytes[f.image+site.rva+k]=site.bytes[k];for(const auto& slot:mgscoop::horse_cargo_native_v2_contract::slots)code.Put(f.image+slot.rva,f.image+slot.target);std::uintptr_t mismatch=0;Check(hn::CodeGate(code,f.image,0xa01d000,mismatch),"all exact native route and cargo lifecycle spans admitted");for(const auto& site:mgscoop::horse_cargo_native_v2_contract::sites){auto tail=code;tail.bytes[f.image+site.rva+site.size-1]^=1;Check(!hn::CodeGate(tail,f.image,0xa01d000,mismatch)&&mismatch==site.rva,"every full native span checks final byte");}for(const auto& slot:mgscoop::horse_cargo_native_v2_contract::slots){auto foreign=code;foreign.Put<std::uintptr_t>(f.image+slot.rva,f.image+0x645940);Check(!hn::CodeGate(foreign,f.image,0xa01d000,mismatch),"each original selector/receiver slot target must match");}
 Check(hn::Observe(cargo.memory,cargo.image,cargo.census,0x1a00,o,why),"status fixture restored");char line[161]{};Check(hn::Line(o,true,why,line,sizeof(line))&&std::strstr(line,"idx=1,1,1")&&std::strstr(line,"fresh=0"),"bounded native index/cargo/anchor telemetry");Check(!hn::Line(o,true,why,line,12),"native cargo status truncation refused");std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
