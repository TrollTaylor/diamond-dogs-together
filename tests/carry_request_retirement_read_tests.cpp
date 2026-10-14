#include "carry_recipient_read.hpp"
#include "carry_owned_handshake.hpp"
#include "carry_request_retirement_read.hpp"
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

namespace rt=mgscoop::carry_request_retirement_read;
Memory QueuedFixture(){auto m=Fixture();auto s=S();auto root=s.data-0x20,slab=std::uintptr_t(0x150000),head=slab+0x80,array=slab+0x100;
 m.Put(root,s.image+0x23ad938);m.Put(s.data+0x1a0,slab);m.Put(s.data+0x1a8,head);m.Put<std::uint64_t>(s.data+0x1b0,0x1000);m.Put<std::uint64_t>(s.data+0x1b8,0x1000);m.Put<std::uint16_t>(s.data+0x1c2,1);m.Put(s.data+0x1d8,array);
 m.Put(head,s.image+0x645940);m.Put<std::uintptr_t>(head+8,0);m.Put(head+0x10,array);m.Put<std::uint64_t>(head+0x18,0x40);m.Put<std::uint64_t>(head+0x20,2);
 for(unsigned k=0;k<2;++k){mgscoop::carry_request_payload::RecipientRequest packet{};mgscoop::carry_request_payload::BuildRecipient(s.image,0xa01d000,1-k,k,0xfa,packet);packet.force=0;m.Put(array+k*0x40,packet);}return m;
}
int main(){auto s=S();rt::Observation o{};auto m=QueuedFixture();
 Check(rt::ReadPair(m,s,o)&&o.array==0x150100&&o.packet[0].sequence==0&&o.packet[1].carrier==0,"typed current two-body request allocation");
 struct Mutation{std::uintptr_t p,v;unsigned width;const char* label;};
 for(auto z:{Mutation{0x1ffe0,0x1423ad868,8,"constructor base root refused"},{0x201a0,0,8,"missing allocation slab"},{0x201b0,0x100001,8,"unbounded allocation refused"},{0x201b8,0x1001,8,"used range exceeds owned capacity"},{0x201c2,0,2,"unrealized first allocator pass"},{0x201d8,0x160000,8,"request array outside slab"},{0x201d8,0x150101,8,"unaligned request array"},{0x201a8,0x160000,8,"descriptor outside slab"},{0x150080,0x140645941,8,"foreign request release callback"},{0x150088,0x150080,8,"self-cycle list refused"},{0x150090,0x150200,8,"matching owned request descriptor absent"},{0x150098,0x20,8,"current-record stride cannot substitute request stride"},{0x1500a0,1,8,"wrong request allocation body count"}}){auto f=QueuedFixture();if(z.width==8)f.Put<std::uintptr_t>(z.p,z.v);else f.Put<std::uint16_t>(z.p,static_cast<std::uint16_t>(z.v));Check(!rt::ReadPair(f,s,o),z.label);}
 auto f=QueuedFixture();f.bytes.erase(0x15011f);Check(!rt::ReadPair(f,s,o),"partial current packet refused");f=QueuedFixture();f.flip=true;Check(!rt::ReadPair(f,s,o),"native owner race refuses request snapshot");
 f=QueuedFixture();f.Put<std::uintptr_t>(0x150088,0x1500b0);f.Put<std::uintptr_t>(0x1500b0,s.image+0x645940);f.Put<std::uintptr_t>(0x1500b8,0x150080);f.Put<std::uintptr_t>(0x1500c0,0x150280);f.Put<std::uint64_t>(0x1500c8,0x20);f.Put<std::uint64_t>(0x1500d0,2);Check(!rt::ReadPair(f,s,o),"two-node allocation cycle refused");
 f=QueuedFixture();f.Put<std::uintptr_t>(0x150088,0x1500b0);f.Put<std::uintptr_t>(0x1500b0,s.image+0x645940);f.Put<std::uintptr_t>(0x1500b8,0);f.Put<std::uintptr_t>(0x1500c0,0x150100);f.Put<std::uint64_t>(0x1500c8,0x40);f.Put<std::uint64_t>(0x1500d0,2);Check(!rt::ReadPair(f,s,o),"duplicate request ownership descriptors refused");
 auto t=s;t.generation++;Check(!rt::ReadPair(m,t,o),"foreign generation request snapshot refused");t=s;t.sequenceAfter+=2;Check(!rt::ReadPair(m,t,o),"changed external window refuses request snapshot");t=s;t.sequenceBefore=t.sequenceAfter=9;Check(!rt::ReadPair(m,t,o),"outside-snapshot helper refuses odd owned job; no retained write permission");
 Check(rt::ReadPair(m,s,o),"unchanged request snapshot succeeds");auto other=o;other.packet[1].sequence++;Check(!rt::Same(o,other),"packet coherence includes sequence");other=o;other.descriptor+=8;Check(!rt::Same(o,other),"allocation descriptor coherence");
 Memory gate;for(auto p:mgscoop::carry_request_retirement_contract::sites)for(std::size_t i=0;i<p.size;++i)gate.bytes[s.image+p.rva+i]=p.bytes[i];for(auto p:mgscoop::carry_request_retirement_contract::slots)gate.Put<std::uintptr_t>(s.image+p.rva,s.image+p.target);std::uintptr_t mismatch=0;
 Check(rt::CodeGate(gate,s.image,0xa01d000,mismatch),"complete initializer neutral queue gate");gate.bytes[s.image+0xfd49b9]^=1;Check(!rt::CodeGate(gate,s.image,0xa01d000,mismatch)&&mismatch==0xfd4970,"modified neutral constructor refused");gate.bytes[s.image+0xfd49b9]^=1;gate.Put<std::uintptr_t>(s.image+0x23ad9b0,s.image+0xfe01b0);Check(!rt::CodeGate(gate,s.image,0xa01d000,mismatch),"current-buffer setter cannot substitute request setter");Check(!rt::CodeGate(gate,s.image,0x1000,mismatch),"short exact gate image refused");
 std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
