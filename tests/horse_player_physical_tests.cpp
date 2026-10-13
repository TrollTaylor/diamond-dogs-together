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

namespace hp=mgscoop::horse_player_physical_read;
Memory PhysicalFixture(){auto m=Fixture();const auto s=S();
 for(unsigned k=0;k<2;++k){const auto block=std::uintptr_t(0x40000+k*0x6a8),ps=std::uintptr_t(0x50000+k*0x1000);
  const auto cs=std::uintptr_t(0xc0000+k*0x1000),entry=std::uintptr_t(0xc4000+k*0x1000),entity=std::uintptr_t(0xc8000+k*0x1000);
  const auto list=std::uintptr_t(0xca000+k*0x1000),component=std::uintptr_t(0xd0000+k*0x1000),physical=std::uintptr_t(0xd6000+k*0x1000),transform=std::uintptr_t(0xd8000+k*0x1000);
  m.Put(block+0x10,cs);m.Put(ps+0x238,cs);m.Put(cs,s.image+0x22e6070);m.Put<std::uint32_t>(cs+8,1);m.Put<std::uint32_t>(cs+0xc,k);m.Put(cs+0x10,entry);
  m.Put(entry+0x60,transform);m.Put(entry+0x70,entity);m.Put<std::uint32_t>(entity+0x28,1);m.Put(entity+0x30,list);m.Put(list,component);
  m.Put(component,s.image+0x24c97f8);m.Put<std::uint32_t>(component+0x10,0x7f1402f4);m.Put(component+0x68,transform);m.Put(component+0x80,s.image+0x24c9830);
  m.Put(ps+0x270,physical);m.Put<std::uintptr_t>(physical,component+0x80);m.Put<std::uint32_t>(component+0xd0,0);
  for(unsigned j=0;j<16;++j)m.Put<float>(component+0x90+j*4,j%5==0?1.0f:0.0f);
 }return m;
}
int main(){Memory gated;const auto s=S();std::uintptr_t mismatch=0;
 for(const auto& site:mgscoop::horse_player_physical_contract::sites)for(std::size_t j=0;j<site.size;++j)gated.bytes[s.image+site.rva+j]=site.bytes[j];
 for(const auto& slot:mgscoop::horse_player_physical_contract::slots)gated.Put<std::uintptr_t>(s.image+slot.rva,s.image+slot.target);
 Check(hp::CodeGate(gated,s.image,0xa01d000,mismatch),"all complete physical fingerprints pass");
 for(const auto& site:mgscoop::horse_player_physical_contract::sites){auto x=gated;const auto tail=s.image+site.rva+site.size-1;x.bytes[tail]^=1;Check(!hp::CodeGate(x,s.image,0xa01d000,mismatch)&&mismatch==site.rva,"each complete site includes final byte");}
 for(const auto& slot:mgscoop::horse_player_physical_contract::slots){auto x=gated;x.Put<std::uintptr_t>(s.image+slot.rva,s.image+0x645940);Check(!hp::CodeGate(x,s.image,0xa01d000,mismatch)&&mismatch==slot.rva,"each physical native slot target refuses mutation");}
 Check(!hp::CodeGate(gated,s.image,0x1000,mismatch),"short image refused");
 auto m=PhysicalFixture();hp::Observation observed{};const char* reason="unset";
 Check(hp::Observe(m,s,observed,&reason)&&!std::strcmp(reason,"physical_observed"),"both actual typed physical destinations read twice");
 Check(observed.body[0].physicalInterface==0xd0080&&observed.body[1].physicalInterface==0xd1080,"per-body native physical destination retained");
 Check(observed.body[1].ownerTransform==observed.body[1].transform&&observed.body[1].tag==0x7f1402f4,"current model owner and tag crosslinked");
 char line[160]{};Check(hp::Line(observed,true,reason,line,sizeof(line))&&std::strstr(line,"ready=1"),"bounded native diagnostic reply");
 Check(!hp::Line(observed,true,reason,line,12),"diagnostic truncation refused");
 struct Mutation{std::uintptr_t p,v;unsigned width;const char* label;};
 for(const auto x:{Mutation{0xc0000,0x1422e7800,8,"foreign CS class refused"},{0xc0008,2,4,"multi-body model refused"},{0xc000c,1,4,"model base ID mismatch refused"},{0x40010,0xc1000,8,"body CS backlink mismatch refused"},{0xc0010,0xfffffffffffffff0ull,8,"model array overflow refused"},{0xc4070,0,8,"missing current Entity refused"},{0xc8028,129,4,"component list unbounded refused"},{0xc8030,3,8,"unaligned component list refused"},{0xd0010,0,4,"missing component tag refused"},{0xd0000,0x1423e1410,8,"base physical construction class refused"},{0xd0080,0x1423e1410,8,"wrong physical interface class refused"},{0xd0068,0xd9000,8,"borrowed foreign transform refused"},{0xd6000,0xd1080,8,"wrong current native physical slot refused"},{0xd0d0,0,4,"missing physical matrix refused"}}){auto n=PhysicalFixture();if(x.width==8)n.Put<std::uintptr_t>(x.p,x.v);else n.Put<std::uint32_t>(x.p,static_cast<std::uint32_t>(x.v));if(x.p==0xd0d0)n.bytes.erase(0xd0090);Check(!hp::Observe(n,s,observed,&reason),x.label);}
 auto duplicate=PhysicalFixture();duplicate.Put<std::uint32_t>(0xc8028,2);duplicate.Put<std::uintptr_t>(0xca008,0xe0000);duplicate.Put<std::uint32_t>(0xe0010,0x7f1402f4);Check(!hp::Observe(duplicate,s,observed,&reason)&&!std::strcmp(reason,"physical_tag_duplicate"),"duplicate physical tag refused");
 auto unrelated=PhysicalFixture();unrelated.Put<std::uint32_t>(0xc8028,2);unrelated.Put<std::uintptr_t>(0xca008,0xe0000);unrelated.Put<std::uint32_t>(0xe0010,0x12345678);Check(hp::Observe(unrelated,s,observed,&reason),"unrelated bounded component allowed");
 auto invalid=PhysicalFixture();invalid.Put<std::uint32_t>(0xd0090,0x7fc00000);Check(!hp::Observe(invalid,s,observed,&reason)&&!std::strcmp(reason,"physical_matrix_invalid"),"non-finite native matrix refused");
 auto alias=PhysicalFixture();alias.Put<std::uintptr_t>(0xc5060,0xd8000);alias.Put<std::uintptr_t>(0xd1068,0xd8000);Check(!hp::Observe(alias,s,observed,&reason)&&!std::strcmp(reason,"physical_body_alias"),"shared transform owner across players refused");
 auto race=PhysicalFixture();race.flip=true;Check(!hp::Observe(race,s,observed,&reason)&&!std::strcmp(reason,"physical_changed"),"native carry context change across reads refused");
 auto old=s;old.ownedGeneration++;Check(!hp::Observe(m,old,observed,&reason),"stale scope refused");
 auto unknown=PhysicalFixture();unknown.Put<std::uintptr_t>(0xd0000,s.image+0x24c9990);Check(!hp::Observe(unknown,s,observed,&reason)&&observed.body[0].componentType==0x24c9990,"diagnostic retains refused final subclass RVA only");
 std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
