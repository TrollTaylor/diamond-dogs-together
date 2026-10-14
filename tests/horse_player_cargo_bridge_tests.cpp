#ifndef MGS_BRIDGE_TEST_HEADER
#define MGS_BRIDGE_TEST_HEADER "horse_player_cargo_bridge_read.hpp"
#endif
#include MGS_BRIDGE_TEST_HEADER

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
int main(){const auto s=S();Memory code;std::uintptr_t mismatch=0;
 for(const auto& p:mgscoop::horse_player_cargo_bridge_contract::sites)for(std::size_t j=0;j<p.size;++j)code.bytes[s.image+p.rva+j]=p.bytes[j];
 for(const auto& p:mgscoop::horse_player_cargo_bridge_contract::slots)code.Put<std::uintptr_t>(s.image+p.rva,s.image+p.target);
 Check(pc::CodeGate(code,s.image,0xa01d000,mismatch),"full chained code and independent closed leaves baseline");
 for(const auto& p:mgscoop::horse_player_cargo_bridge_contract::sites){auto m=code;m.bytes[s.image+p.rva+p.size-1]^=1;
  Check(!pc::CodeGate(m,s.image,0xa01d000,mismatch)&&mismatch==p.rva,"every final byte fingerprint refuses mutation");}
 for(const auto& p:mgscoop::horse_player_cargo_bridge_contract::slots){auto m=code;m.Put<std::uintptr_t>(s.image+p.rva,s.image+0x645940);
  Check(!pc::CodeGate(m,s.image,0xa01d000,mismatch)&&mismatch==p.rva,"every helper interface target is pinned");}
 Check(!pc::CodeGate(code,s.image,0x1000,mismatch),"short image boundary refused");
 Check(!pc::CodeGate(code,UINTPTR_MAX-12,0xa01d000,mismatch),"image address overflow refused");
 for(auto clip:NativeRecognized){Check(pc::Source(clip)==pc::ClipSource::carrier_load_bone_1d,"independent decoded native clip recognizer");
  Check(pc::Source(clip+1)==pc::ClipSource::other,"adjacent unrecognized native clip refused");
  Check(pc::Source(clip|(1ull<<48))==pc::ClipSource::other,"no guessed high tag masking of native recognizer");}
 auto m=BridgeFixture();pc::Observation o{};const char* why="unset";
 Check(pc::Observe(m,s,o,why)&&!std::strcmp(why,"pc_observed"),"two current idle helper and physical owners");
 Check(!o.body[0].currentCarry&&!o.body[1].currentCarry&&!o.body[0].matrixMatches,"idle census has no native cargo assertion");
 char line[160]{};Check(pc::Line(o,true,why,line,sizeof(line))&&std::strstr(line,"pcBridge;ok=1"),"bounded no-pointer diagnostic");
 Check(!pc::Line(o,true,why,line,16),"short diagnostic refused");
 for(unsigned carried=0;carried<2;++carried){auto c=CarryFixture(carried);
  Check(pc::Observe(c,s,o,why)&&o.body[carried].currentCarry&&o.body[carried].matrixMatches,"both roles current original native matrix chain");
  Check(o.body[carried].profile.mode==3&&o.body[carried].helper.stride==0x24,"carried mode3 and exact helper allocation observed");
  c.Put<float>(0x2d0090+carried*0x1000,0.5f);Check(pc::Observe(c,s,o,why)&&!o.body[carried].matrixMatches,"matrix discrepancy remains observer data");
 }
 struct Mutation{std::uintptr_t p,v;unsigned n;};
 for(auto z:{Mutation{0x300000,s.image+0x2339c00,8},{0x300020,s.image+0x2363c80,8},
  {0xd0078,0,8},{0x302010,0x301000,8},{0x300050,0x306001,8},{0x300030,0x305000,8},
  {0x300068,1,4},{0x304000,s.image+0x645941,8},{0x304010,0x307000,8},{0x304018,0x40,8},
  {0x304020,2,8},{0x300060,1,8},{0x20018,1,8},{0x306000,0x7fc00000,4}}){auto c=BridgeFixture();
  if(z.n==8)c.Put<std::uintptr_t>(z.p,z.v);else c.Put<std::uint32_t>(z.p,static_cast<std::uint32_t>(z.v));
  Check(!pc::Observe(c,s,o,why),"foreign or malformed exact helper current closure refused");}
 auto h=CarryFixture();h.Put<std::uint16_t>(0xe1030,0x1a00);
 Check(!pc::Observe(h,s,o,why)&&!std::strcmp(why,"pc_carrier_owner"),"horse ID cannot stand in for Player carrier ID");
 h=CarryFixture();h.Put<std::uintptr_t>(0xe1028,0x70010);
 Check(!pc::Observe(h,s,o,why)&&!std::strcmp(why,"pc_carrier_owner"),"Horse unit or guessed shifted Player context refused");
 h=CarryFixture();h.Put<std::uint32_t>(0x70110,0x7fc00000);
 Check(!pc::Observe(h,s,o,why)&&!std::strcmp(why,"pc_carrier_matrix"),"non-finite native source matrix refused");
 h=CarryFixture();h.Put<std::uint32_t>(0x111004,1);
 Check(pc::Observe(h,s,o,why)&&!o.body[1].currentCarry,"pending original recipient release never claims stable carry");
 h=CarryFixture();h.Put<std::uint16_t>(0x36002,5);
 Check(pc::Observe(h,s,o,why)&&!o.body[1].currentCarry,"physical matrix equality does not override native action");
 h=BridgeFixture();h.bytes.erase(0x306020);Check(!pc::Observe(h,s,o,why),"partial helper control memory refused");
 h=BridgeFixture();h.flip=true;Check(!pc::Observe(h,s,o,why),"paired current source change refused");
 auto stale=s;stale.thread++;Check(!pc::Observe(m,stale,o,why),"readonly scope never borrows native callback thread");
 stale=s;stale.ownedGeneration++;Check(!pc::Observe(m,stale,o,why),"replaced scene refused");
 stale=s;stale.sequenceAfter+=2;Check(!pc::Observe(m,stale,o,why),"changed current input window refused");
 stale=s;stale.now+=1000;Check(!pc::Observe(m,stale,o,why),"expired paired source refused");
 std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
