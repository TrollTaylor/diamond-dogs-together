#include "carry_queue_lifetime.hpp"
#include <cstdio>
#include <cstring>
#include <map>
namespace q=mgscoop::carry_queue_lifetime;
struct Memory {std::map<std::uintptr_t,unsigned char> bytes;
 template<class T>void Put(std::uintptr_t a,T v){const auto* p=reinterpret_cast<const unsigned char*>(&v);for(std::size_t n=0;n<sizeof(v);++n)bytes[a+n]=p[n];}
 bool operator()(std::uintptr_t a,void* out,std::size_t n){for(std::size_t i=0;i<n;++i){auto x=bytes.find(a+i);if(x==bytes.end())return false;static_cast<unsigned char*>(out)[i]=x->second;}return true;}
};
constexpr std::uintptr_t image=0x140000000,P=0x10000,Q=0x20020,S=0x30000,V=0x40000,R0=0x50000,R1=0x60000;
Memory Good(){Memory m;m.Put(image+0x2a82c30,P);m.Put(P,image+0x2295490);m.Put(P+0x204,std::uint32_t{1});m.Put(P+0x208,std::uint32_t{2});m.Put(P+0x20c,std::uint32_t{2});m.Put(P+0x214,std::uint32_t{0});m.Put(P+0x60,Q);
 m.Put(Q-0x20,image+0x23ad938);m.Put(Q,image+0x23ad970);m.Put(Q-0x20+0x1e8,R0);m.Put(Q-0x20+0x1f0,R1);m.Put(Q+0x1e8,V);m.Put(V+0x40,std::uint32_t{2});
 m.Put(Q+0x1a0,S);m.Put(Q+0x1a8,S+0x200);m.Put(Q+0x1b0,std::uint64_t{0x400});m.Put(Q+0x1b8,std::uint64_t{0x300});m.Put(Q+0x1c0,std::uint16_t{0x10});m.Put(Q+0x1c2,std::uint16_t{1});m.Put(Q+0x1d8,S+0x100);
 m.Put(S+0x200,image+0x645940);m.Put(S+0x208,std::uintptr_t{0});m.Put(S+0x210,S+0x100);m.Put(S+0x218,std::uint64_t{0x40});m.Put(S+0x220,std::uint64_t{2});return m;}
void Foreign(Memory& m,std::uintptr_t h,std::uintptr_t slab=0x70000){m.Put(h,slab);m.Put(h+8,slab+0x100);m.Put(h+0x10,std::uint64_t{0x300});m.Put(h+0x18,std::uint64_t{0x200});m.Put(h+0x22,std::uint16_t{1});m.Put(slab+0x100,image+0x645940);m.Put(slab+0x108,std::uintptr_t{0});m.Put(slab+0x110,slab+0x200);m.Put(slab+0x118,std::uint64_t{0x40});m.Put(slab+0x120,std::uint64_t{2});}
int main(){unsigned count=0,failed=0;auto check=[&](bool v,const char* n){++count;if(!v){++failed;std::printf("FAIL %s\n",n);}};
 auto m=Good();q::Owner owner{};check(q::ReadOwner(m,image,P,owner)&&owner.data==Q&&owner.queued==S+0x100&&owner.descriptors==1,"complete current typed Q/slab/descriptor graph");
 auto bad=m;bad.Put(Q-0x20,image+0x23ad868);check(!q::ReadOwner(bad,image,P,owner),"transient base constructor VT is not final Q");
 bad=m;bad.Put(Q,image+0x23ad8a0);check(!q::ReadOwner(bad,image,P,owner),"transient secondary VT refuses");
 bad=m;bad.Put(P+0x60,Q+0x20);check(!q::ReadOwner(bad,image,P,owner),"current P data replacement refuses");
 bad=m;bad.Put(P+0x204,std::uint32_t{0x101});check(!q::ReadOwner(bad,image,P,owner),"online synchronizer mode refuses");
 bad=m;bad.Put(V+0x40,std::uint32_t{1});check(!q::ReadOwner(bad,image,P,owner),"actual native snapshot count must be2");
 bad=m;bad.Put(Q+0x1c2,std::uint16_t{0});check(!q::ReadOwner(bad,image,P,owner),"allocator measurement pass is not live slab");
 bad=m;bad.Put(Q+0x1c0,std::uint16_t{3});check(!q::ReadOwner(bad,image,P,owner),"invalid allocator alignment refuses");
 bad=m;bad.Put(Q+0x1b8,std::uint64_t{0x401});check(!q::ReadOwner(bad,image,P,owner),"used bytes beyond capacity refuse");
 bad=m;bad.Put(Q+0x1d8,S+0x3c0);check(!q::ReadOwner(bad,image,P,owner),"two queues must fit whole allocation");
 bad=m;bad.Put(Q+0x1a8,S+0x100);check(!q::ReadOwner(bad,image,P,owner),"descriptor cannot alias written queue bytes");
 bad=m;bad.Put(S+0x208,S+0x200);check(!q::ReadOwner(bad,image,P,owner),"descriptor cycle refuses");
 bad=m;bad.Put(S+0x220,std::uint64_t{1});check(!q::ReadOwner(bad,image,P,owner),"queue descriptor count is native2");
 bad=m;bad.Put(S+0x218,std::uint64_t{0x20});check(!q::ReadOwner(bad,image,P,owner),"queue descriptor stride is native40");
 bad=m;bad.Put(S+0x200,image+0x645943);check(!q::ReadOwner(bad,image,P,owner),"queue descriptor callback must be exact no-op");
 bad=m;bad.Put(S+0x210,S+0x180);check(!q::ReadOwner(bad,image,P,owner),"orphan requested queue cannot be retained");
 bad=m;bad.Put(S+0x208,S+0x280);bad.Put(S+0x280,image+0x645940);bad.Put(S+0x288,std::uintptr_t{0});bad.Put(S+0x290,S+0x108);bad.Put(S+0x298,std::uint64_t{0x10});bad.Put(S+0x2a0,std::uint64_t{2});
 check(!q::ReadOwner(bad,image,P,owner),"other descriptor array cannot partially overlap owned queued slots");
 bad.Put(S+0x290,S+0xe0);bad.Put(S+0x298,std::uint64_t{0x40});bad.Put(S+0x2a0,std::uint64_t{4});check(!q::ReadOwner(bad,image,P,owner),"other descriptor array cannot contain owned queued slots");
 check(q::ReadOwner(m,image,P,owner),"recover original independent owner");const auto original=owner;
 auto drift=[&](std::uintptr_t a,void* o,std::size_t n){if(a==P+0x60){static unsigned calls=0;++calls;if(calls==2){const auto other=Q+0x40;std::memcpy(o,&other,n);return true;}}return m(a,o,n);};check(!q::ReadOwner(drift,image,P,owner),"rebind between coherent reads refuses");owner=original;
 check(q::ProtectsObject(owner,Q-0x20)&&q::ProtectsObject(owner,Q-0x20+0x1c0,true),"root and independent embedded allocator identify held lifetime");
 check(!q::ProtectsObject(owner,Q)&&!q::ProtectsObject(owner,Q+0x1c0,true),"secondary offset cannot masquerade as root or header");
 auto foreign=m;Foreign(foreign,0x80000);check(q::ForeignFree(foreign,image,0x80000,owner),"foreign no-op destructor footprint is fully disjoint");
 foreign=m;Foreign(foreign,0x80000,S);check(!q::ForeignFree(foreign,image,0x80000,owner),"different header sharing held allocation refuses");
 foreign=m;Foreign(foreign,0x80000,Q-0x20);check(!q::ForeignFree(foreign,image,0x80000,owner),"foreign base overlapping held root refuses");
 foreign=m;Foreign(foreign,0x80000,V);check(!q::ForeignFree(foreign,image,0x80000,owner),"foreign allocation overlapping current borrowed snapshot refuses");
 foreign=m;Foreign(foreign,0x80000,R0);check(!q::ForeignFree(foreign,image,0x80000,owner),"borrowed resource object cannot be freed through alias header");
 foreign=m;Foreign(foreign,0x70010);check(!q::ForeignFree(foreign,image,0x70010,owner),"allocator header inside its own freed allocation refuses");
 foreign=m;Foreign(foreign,0x80000);foreign.Put(0x70100,image+0xaf0140);check(!q::ForeignFree(foreign,image,0x80000,owner),"unqualified destructive callback never forwards from pointer inequality");
 foreign=m;Foreign(foreign,0x80000);foreign.Put(0x70108,std::uintptr_t{0x70100});check(!q::ForeignFree(foreign,image,0x80000,owner),"foreign descriptor cycle refuses before native call");
 foreign=m;Foreign(foreign,0x80000);foreign.Put(0x70110,std::uintptr_t{0x71000});check(!q::ForeignFree(foreign,image,0x80000,owner),"foreign callback array outside its slab refuses");
 foreign=m;Foreign(foreign,0x80000);foreign.Put(0x70120,std::uint64_t{0});foreign.Put(0x70100,std::uintptr_t{0});check(q::ForeignFree(foreign,image,0x80000,owner),"zero-count descriptor performs no callback");
 foreign=m;Foreign(foreign,0x80000);foreign.Put(0x80000,std::uintptr_t{0});foreign.Put(0x80008,std::uintptr_t{0});check(q::ForeignFree(foreign,image,0x80000,owner),"cold empty foreign allocator only resets disjoint header");
 check(!q::ForeignFree(foreign,image,Q-0x20+0x1c0,owner),"exact held allocator always refuses disposal");
 check(q::ProtectedRange(owner,R0+0x10,8)&&q::ProtectedRange(owner,R1+0x17,1),"known resource fields through18 are protected");
 check(!q::ForeignFree(foreign,image,R0+0x10,owner)&&!q::ForeignFree(foreign,image,R1+0x17,owner),"suffix allocator header overlaps cannot bypass resource guard");
 foreign=m;Foreign(foreign,0x80000,R0+8);check(!q::ForeignFree(foreign,image,0x80000,owner),"suffix-only freed allocation overlaps full known resource range");

 check(!q::ProtectedRange(owner,0x90000,0x100)&&q::ProtectedRange(owner,S+0x3ff,1)&&q::ProtectedRange(owner,Q-0x20+0x21f,1),"exact footprint endpoints are protected");
 check(q::ProtectedRange(owner,UINTPTR_MAX-3,8),"wrapping ranges are never disjoint");
 std::printf("carry_queue_lifetime: %u assertions, %u failures; source-only fixture\n",count,failed);return failed?1:0;
}
