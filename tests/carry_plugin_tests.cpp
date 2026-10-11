#include "carry_plugin_row32_read.hpp"
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
int main(){
 Memory gated;const std::uintptr_t image=0x140000000;std::uintptr_t mismatch=0;
 for(const auto& site:mgscoop::carry_plugin_contract::sites)for(std::size_t j=0;j<site.size;++j)gated.bytes[image+site.rva+j]=site.bytes[j];
 for(const auto& slot:mgscoop::carry_plugin_contract::slots)gated.Put<std::uintptr_t>(image+slot.vtable_rva+slot.offset,image+slot.target_rva);
 Check(CodeGate(gated,image,0xa01d000,mismatch),"all complete exact fingerprints pass");
 const auto first=mgscoop::carry_plugin_contract::sites[0].rva;gated.bytes[image+first]^=1;
 Check(!CodeGate(gated,image,0xa01d000,mismatch)&&mismatch==first,"changed executable code refuses exact gate");gated.bytes[image+first]^=1;
 Check(!CodeGate(gated,image,0x1000,mismatch),"truncated image refuses gate");Check(!CodeGate(gated,0xfffffffffffffff0ull,0x1000,mismatch),"image overflow refuses gate");
 gated.Put<std::uintptr_t>(image+0x23b9b50+8,image+0x645940);Check(!CodeGate(gated,image,0xa01d000,mismatch),"changed live native phase target refuses gate");
 auto s=S();auto m=Fixture();Reply r{};Check(ReadPair(m,s,r)&&r.ready,"qualified paired read");Check(r.selectorAccepts&&r.selectorTarget==1,"actual cached carry selector");Check(!r.body[0].nodeActive,"inactive plugin is observable");Check(r.body[0].target==0xffff,"gun target cannot substitute plugin target");
 auto t=s;t.exactGate=false;Check(!ReadPair(m,t,r),"fingerprint gate refused");t=s;t.thread++;Check(!ReadPair(m,t,r),"unnatural thread refused");t=s;t.ownedGeneration++;Check(!ReadPair(m,t,r),"old generation refused");t=s;t.ownedEpoch++;Check(!ReadPair(m,t,r),"old epoch refused");t=s;t.mission=t.ownedMission=10010;Check(!ReadPair(m,t,r),"unsupported mission refused");t=s;t.poll=249;Check(!ReadPair(m,t,r),"stale lease refused");t=s;t.batch=true;Check(!ReadPair(m,t,r),"batch refused");t=s;t.sequenceBefore++;Check(!ReadPair(m,t,r),"input injection refused");t=s;t.sequenceAfter+=2;Check(!ReadPair(m,t,r),"crossed input window refused");
 struct Mutation{std::uintptr_t p,v;unsigned width;const char* label;};
 for(auto x:{Mutation{0x10204,0x501,4,"online flag refused"},{0x100a8,0x4000,8,"sync owner refused"},{0x10214,1,4,"nonlocal owner refused"},{0x10208,4,4,"four bodies refused"},{0x32001,0,1,"shared pool refused"},{0x33001,2,1,"loading parts refused"},{0x6002c,0x3d,4,"wrong component row refused"},{0x60024,1,4,"stale component body refused"},{0x80000,0x1423b9480,8,"base constructor vtable refused"},{0x80038,0x61000,8,"wrong node backlink refused"},{0x40548,0x81030,8,"wrong phase instance refused"},{0x80068,0xfffffffffffffff0ull,8,"record range overflow refused"},{0x8010,0,8,"missing bytes refused"}}){auto n=Fixture();if(x.width==8)n.Put<std::uintptr_t>(x.p,x.v);else if(x.width==4)n.Put<std::uint32_t>(x.p,static_cast<std::uint32_t>(x.v));else n.Put<std::uint8_t>(x.p,static_cast<std::uint8_t>(x.v));if(x.p==0x8010)n.bytes.erase(0x70078);Check(!ReadPair(n,s,r),x.label);}
 for(auto x:{Mutation{0xb0a60,0x81000,8,"selector owner separate"},{0xb1268,0x4b,4,"selector kinds separate"},{0xb1288,0,4,"selector valid bit required"},{0xb0ae4,0x2000,4,"selector suppression2000"},{0xb0ae4,0x4000,4,"selector suppression4000"},{0x10204,1,4,"selector player flag required"}}){auto n=Fixture();if(x.width==8)n.Put<std::uintptr_t>(x.p,x.v);else n.Put<std::uint32_t>(x.p,static_cast<std::uint32_t>(x.v));Check(ReadPair(n,s,r)&&!r.selectorAccepts,x.label);}
 auto row33=Fixture();row33.Put<std::uint32_t>(0x6002c,0x33);Check(!ReadPair(row33,s,r),"following row33 cannot substitute carry row32");
 auto wrongSlot=Fixture();wrongSlot.Put<std::uintptr_t>(0x40320,0);wrongSlot.Put<std::uintptr_t>(0x40328,0x60000);Check(!ReadPair(wrongSlot,s,r),"old node slot refuses even with correct type at following slot");
 auto wrongPhase=Fixture();wrongPhase.Put<std::uintptr_t>(0x40548,0);wrongPhase.Put<std::uintptr_t>(0x40550,0x80030);Check(!ReadPair(wrongPhase,s,r),"old phase slot refuses even with correct type at following slot");
 auto decoy=Fixture();decoy.Put<std::uintptr_t>(0x40328,0xdead0000);decoy.Put<std::uintptr_t>(0x40550,0xdead0030);Check(ReadPair(decoy,s,r),"unrelated following row does not change exact carry row binding");
 auto race=Fixture();race.flip=true;Check(!ReadPair(race,s,r)&&!std::strcmp(r.reason,"state_changed"),"two-sample target race refused");auto c=Fixture();c.Put<std::uint16_t>(0x70078,1);c.Put<std::uint32_t>(0xa0000,1u<<22);Check(ReadPair(c,s,r)&&r.body[0].target==1&&(r.body[0].psFlags&(1u<<22)),"carrier side tuple observable only");Check(!Range(0xfffffffffffffff0ull,0x20),"overflow checked");std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;}
