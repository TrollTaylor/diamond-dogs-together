#include "sleep_plugin_read.hpp"
#include <map>
#include <cstring>
#include <cstdio>
#include <initializer_list>
using namespace mgscoop::sleep_plugin_read;
unsigned good=0,bad=0;void Check(bool b,const char* l){if(b)++good;else{++bad;std::printf("FAIL %s\n",l);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 bool flip=false;unsigned scans=0;std::uintptr_t flipAt=0;std::uint8_t flipValue=0;
 template<class T>void Put(std::uintptr_t p,T x){unsigned char v[sizeof(T)]{};std::memcpy(v,&x,sizeof(x));for(unsigned i=0;i<sizeof(T);++i)bytes[p+i]=v[i];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {
  if(p==0x10000&&++scans==2&&flip)Put<std::uint8_t>(flipAt,flipValue);
  auto* v=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto j=bytes.find(p+i);if(j==bytes.end())return false;v[i]=j->second;}return true;
 }
};
Scope S(){Scope s{};s.exactGate=s.admitted=s.ready=s.armed=true;s.image=0x140000000;s.player=0x10000;s.data=0x20000;s.state=0x30000;s.thread=s.ownerThread=9;s.mission=s.ownedMission=30050;s.epoch=s.ownedEpoch=2;s.generation=s.ownedGeneration=3;s.now=1000;s.poll=900;s.sequenceBefore=s.sequenceAfter=8;return s;}
Memory Fixture(){Memory m;auto s=S();
 m.Put(s.player,s.image+0x2295490);m.Put(s.player+0x60,s.data);m.Put(s.data,s.image+0x23ad970);m.Put(s.data+0x1e8,s.state);m.Put<std::uint32_t>(s.state+0x40,2);m.Put<std::uintptr_t>(s.player+0xa8,0);m.Put<std::uint32_t>(s.player+0x204,1);m.Put<std::uint32_t>(s.player+0x208,2);m.Put<std::uint32_t>(s.player+0x20c,2);m.Put<std::uint32_t>(s.player+0x214,0);m.Put<std::uintptr_t>(s.data+8,0xc0000);m.Put<std::uintptr_t>(s.data+0x18,0xd0000);
 for(auto pair:{std::pair<unsigned,std::uintptr_t>{0x70,0x31000},{0x78,0x32000},{0x90,0x33000},{0x378,0x40000},{0x50,0x34000}})m.Put(s.player+pair.first,pair.second);
 for(unsigned k=0;k<2;++k){auto block=std::uintptr_t(0x40000+k*0x6a8),ps=std::uintptr_t(0x50000+k*0x1000),node=std::uintptr_t(0x60000+k*0x1000),prim=std::uintptr_t(0x80000+k*0x1000),rec=std::uintptr_t(0x70000+k*0x1000),bitmap=std::uintptr_t(0x90000+k*0x1000),actions=std::uintptr_t(0xa0000+k*0x1000);
  m.Put<std::uint16_t>(0x31000+k*2,static_cast<std::uint16_t>(k));m.Put<std::uint8_t>(0x32000+k,static_cast<std::uint8_t>(k));m.Put<std::uint8_t>(0x33000+k,3);m.Put(0x34000+k*8,block);m.Put(block,s.image+0x2295210);m.Put(block+0x138,s.player);m.Put<std::uint32_t>(block+8,1);m.Put<std::uint32_t>(block+0xc,k);m.Put(block+0x78,ps);m.Put(ps,s.image+(k?0x23b0c70:0x23b09f0));m.Put(ps+0x408,block);m.Put<std::uint32_t>(ps+8,k);
  m.Put(ps+0xd8,prim+0x10);m.Put(ps+0x500,node);m.Put(prim,s.image+0x23d5c18);m.Put(prim+0x10,s.image+0x23d5c40);m.Put(prim+0x30,s.image+0x23d5c70);m.Put(prim+0x48,s.image+0x23d5cb0);m.Put(prim+8,block);m.Put(prim+0x38,node);m.Put(prim+0x78,rec);
  m.Put(node,s.image+0x2306628);m.Put<std::uint32_t>(node+0x20,1);m.Put<std::uint32_t>(node+0x24,k);m.Put(node+0x38,prim+0x30);m.Put<std::uint32_t>(node+0x28,0);m.Put<std::uint32_t>(node+0x2c,0x19);m.Put(node+8,bitmap);m.Put<std::uint32_t>(bitmap,0);
  m.Put<std::uintptr_t>(rec,0);m.Put<std::uintptr_t>(rec+8,0);m.Put<std::uint8_t>(rec+0x10,0);m.Put<std::uint8_t>(rec+0x11,0);m.Put<std::uint8_t>(rec+0x12,0);m.Put<std::uint16_t>(rec+0x14,0);m.Put<std::uint32_t>(rec+0x20,1);m.Put<std::uint32_t>(rec+0x30,0);m.Put(ps+0x2d8,actions);m.Put<std::uint8_t>(actions,0);m.Put<std::uint16_t>(0xd0000+k*2,0);m.Put<std::uint32_t>(0xc0000+k*0x4c+0x34,0);
 }return m;
}
struct Mutation{std::uintptr_t p,v;unsigned width;const char* label;};
void Change(Memory& n,const Mutation& x){if(x.width==8)n.Put<std::uintptr_t>(x.p,x.v);else if(x.width==4)n.Put<std::uint32_t>(x.p,static_cast<std::uint32_t>(x.v));else if(x.width==2)n.Put<std::uint16_t>(x.p,static_cast<std::uint16_t>(x.v));else n.Put<std::uint8_t>(x.p,static_cast<std::uint8_t>(x.v));}
int main(){
 Memory gated;const std::uintptr_t image=0x140000000;std::uintptr_t mismatch=0;
 for(const auto& site:mgscoop::sleep_plugin_contract::sites)for(std::size_t j=0;j<site.size;++j)gated.bytes[image+site.rva+j]=site.bytes[j];
 for(const auto& slot:mgscoop::sleep_plugin_contract::slots)gated.Put<std::uintptr_t>(image+slot.vtable_rva+slot.offset,image+slot.target_rva);
 Check(CodeGate(gated,image,0xa01d000,mismatch),"all exact complete fingerprints and slots pass");
 for(const auto& site:mgscoop::sleep_plugin_contract::sites){gated.bytes[image+site.rva]^=1;Check(!CodeGate(gated,image,0xa01d000,mismatch),"any complete body byte change refused");gated.bytes[image+site.rva]^=1;}
 for(const auto& slot:mgscoop::sleep_plugin_contract::slots){gated.Put<std::uintptr_t>(image+slot.vtable_rva+slot.offset,image+slot.target_rva+1);Check(!CodeGate(gated,image,0xa01d000,mismatch),"any final slot target change refused");gated.Put<std::uintptr_t>(image+slot.vtable_rva+slot.offset,image+slot.target_rva);}
 Check(!CodeGate(gated,image,0x1000,mismatch),"truncated image refused");Check(!CodeGate(gated,0xfffffffffffffff0ull,0x1000,mismatch),"image overflow refused");
 auto s=S();auto m=Fixture();Reply r{};Check(ReadPair(m,s,r)&&r.ready,"qualified idle pair observed");Check(!r.body[0].callbackInitialized&&!r.body[0].nodeActive&&r.body[0].mode==0,"initial zero callback remains idle evidence");
 for(auto off:{0u,0x10u,0x30u,0x48u}){auto n=Fixture();n.Put<std::uintptr_t>(0x500d8,0x80000+off);Check(ReadPair(n,s,r),"all four final interface pointers normalize with backlinks");}
 for(unsigned mode=0;mode<=6;++mode){auto n=Fixture();n.Put<std::uint8_t>(0x70010,static_cast<std::uint8_t>(mode));n.Put<std::uintptr_t>(0x70000,ExpectedCallback(image,mode));n.Put<std::uint8_t>(0x70011,6);n.Put<std::uint16_t>(0x70014,0x0102);n.Put<std::uint32_t>(0x90000,1);n.Put<std::uint8_t>(0xa0000,0x19);n.Put<std::uint16_t>(0xd0000,mode==5?0x84:0x83);n.Put<std::uint32_t>(0xc0034,2);Check(ReadPair(n,s,r)&&r.body[0].mode==mode&&r.body[0].requested==6&&r.body[0].callbackInitialized&&r.body[0].nodeActive&&r.body[0].action==0x19&&r.body[0].lifeState==2,"every exact mode callback, pending recovery and native action decoded");}
 // Retail allocator/factory leave +C padding unspecified; every exact
 // callback consumer extracts and sign-extends only the low32 at +8.
 auto padded=Fixture();padded.Put<std::uint32_t>(0x7000c,0xfedcba98u);padded.Put<std::uint32_t>(0x7100c,0x12345678u);
 Check(ReadPair(padded,s,r)&&r.ready,"nonzero member-function padding accepted on both idle bodies");
 for(unsigned mode=0;mode<=6;++mode){auto n=padded;n.Put<std::uint8_t>(0x70010,static_cast<std::uint8_t>(mode));n.Put<std::uintptr_t>(0x70000,ExpectedCallback(image,mode));Check(ReadPair(n,s,r)&&r.body[0].mode==mode,"exact active callback accepts unspecified padding");}
 auto unreadPadding=Fixture();for(unsigned k=0;k<2;++k)for(unsigned j=0;j<4;++j)unreadPadding.bytes.erase(0x7000c+k*0x1000+j);
 Check(ReadPair(unreadPadding,s,r)&&r.ready,"padding not read or required to be readable");
 auto movingPadding=padded;movingPadding.flip=true;movingPadding.flipAt=0x7000c;movingPadding.flipValue=0x11;
 Check(ReadPair(movingPadding,s,r)&&r.ready,"padding-only snapshot change does not falsely reject coherence");
 for(auto adjustment:{std::int32_t(1),std::int32_t(-1),std::numeric_limits<std::int32_t>::min()}){auto n=padded;n.Put<std::int32_t>(0x70008,adjustment);Check(!ReadPair(n,s,r)&&!r.ready&&!std::strcmp(r.reason,"sleep_record0"),"nonzero signed adjustment still refused");}
 auto badSecond=padded;badSecond.Put<std::int32_t>(0x71008,-8);Check(!ReadPair(badSecond,s,r)&&!r.ready&&!std::strcmp(r.reason,"sleep_record1"),"wrong second-body signed adjustment refused");
 auto t=s;t.exactGate=false;Check(!ReadPair(m,t,r),"fingerprint refusal");t=s;t.thread++;Check(!ReadPair(m,t,r),"wrong thread");t=s;t.ownerThread=0;Check(!ReadPair(m,t,r),"absent owner thread");t=s;t.ownedGeneration++;Check(!ReadPair(m,t,r),"stale generation");t=s;t.ownedEpoch++;Check(!ReadPair(m,t,r),"stale epoch");t=s;t.generation=t.ownedGeneration=0;Check(!ReadPair(m,t,r),"zero generation");t=s;t.mission=t.ownedMission=10010;Check(!ReadPair(m,t,r),"unsupported mission");t=s;t.poll=249;Check(!ReadPair(m,t,r),"expired owner lease");t=s;t.poll=250;Check(ReadPair(m,t,r),"exact 750ms freshness limit");t=s;t.poll=1001;Check(!ReadPair(m,t,r),"backward clock");t=s;t.poll=0;Check(!ReadPair(m,t,r),"absent poll");t=s;t.batch=true;Check(!ReadPair(m,t,r),"input batch");t=s;t.sequenceBefore++;Check(!ReadPair(m,t,r),"odd input sequence");t=s;t.sequenceAfter+=2;Check(!ReadPair(m,t,r),"crossed input window");t=s;t.ready=false;Check(!ReadPair(m,t,r),"unready scene");t=s;t.armed=false;Check(!ReadPair(m,t,r),"disarmed scene");
 for(auto x:{Mutation{0x10204,0x101,4,"online refusal"},{0x100a8,0x4000,8,"online sync refusal"},{0x10214,1,4,"nonlocal owner"},{0x10208,4,4,"four bodies"},{0x32001,0,1,"shared body pool"},{0x33001,2,1,"loading body"},{0x50000,0x1423b0000,8,"wrong PS type"},{0x50408,0x406a8,8,"wrong PS owner"},{0x500d8,0,8,"missing sleep table"},{0x80000,0x1423d5b08,8,"constructor/destructor base type"},{0x80010,0x1423d5b30,8,"partially destroyed plugin"},{0x80008,0x406a8,8,"wrong plugin owner"},{0x80038,0x61000,8,"wrong plugin node"},{0x60038,0x81030,8,"wrong node plugin"},{0x60000,0x142306700,8,"wrong scheduler type"},{0x60020,2,4,"multi-body node"},{0x60024,1,4,"wrong node base id"},{0x80078,0xfffffffffffffff0ull,8,"record overflow"},{0x70008,1,8,"unknown callback adjustment"},{0x70010,7,1,"invalid native mode"},{0x70011,7,1,"invalid pending mode"},{0x70000,0x1412cb2b0,8,"wrong mode callback"},{0x510d8,0x80010,8,"shared plugin"},{0x81078,0x70000,8,"shared mode records"},{0x61008,0x90000,8,"shared node bitmap"},{0x512d8,0xa0000,8,"shared action array"},{0xc0034,3,4,"unknown life state"}}){auto n=Fixture();Change(n,x);r.ready=true;r.body[0].mode=5;Check(!ReadPair(n,s,r)&&!r.ready&&r.body[0].mode==0,x.label);}
 auto partial=Fixture();partial.bytes.erase(0x70014);Check(!ReadPair(partial,s,r)&&!r.ready,"partial failed read exports no payload");
 for(auto x:{Mutation{0x70012,1,1,"counter mutation"},{0x60028,1,4,"node state mutation"},{0xa0000,0x19,1,"native action mutation"},{0xc0034,2,4,"life mutation"}}){auto n=Fixture();n.flip=true;n.flipAt=x.p;n.flipValue=static_cast<std::uint8_t>(x.v);Check(!ReadPair(n,s,r)&&!std::strcmp(r.reason,"state_changed")&&!r.ready,"two-snapshot transition race refused");}
 auto retired=Fixture();retired.Put<std::uint8_t>(0x70010,5);retired.Put<std::uintptr_t>(0x70000,0);Check(!ReadPair(retired,s,r),"rest state with retired callback refused");
 for(auto x:{Mutation{0x500d8,0,8,"sleep_slot0"},{0x80010,0x1423d5b30,8,"sleep_type0"},{0x80008,0x406a8,8,"sleep_primary0"},{0x60038,0x81030,8,"sleep_node0"},{0x80078,0,8,"sleep_records0"},{0x70010,7,1,"sleep_record0"},{0x70000,0x1412cb2b0,8,"sleep_callback0"},{0xc0034,3,4,"sleep_action_life0"}}){auto n=Fixture();Change(n,x);Check(!ReadPair(n,s,r)&&!std::strcmp(r.reason,x.label),"first-failure constant stage label");}
 Check(!Range(0xfffffffffffffff0ull,0x20),"overflow guarded");std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
