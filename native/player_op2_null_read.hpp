#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
namespace mgscoop::player_op2_null {
struct Observation {std::uintptr_t player=0,block=0,state=0,node=0,primary=0,slab=0,descriptor=0,records=0;std::uint32_t actualId=0;};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& v)noexcept{return Range(p,sizeof(v))&&r(p,&v,sizeof(v));}
inline bool Same(const Observation& a,const Observation& b)noexcept{return a.player==b.player&&a.block==b.block&&a.state==b.state&&a.node==b.node&&a.primary==b.primary&&a.slab==b.slab&&a.descriptor==b.descriptor&&a.records==b.records&&a.actualId==b.actualId;}
// Current typed null-record witness only. Caller separately owns real
// serialized scope/model/birth, code/slot gates and storage lifetime.
// This does not authorize full Player-tree op2 dispatch or any native call.
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,unsigned actualId,Observation& out,const char*& reason)noexcept{
 out={};const auto fail=[&](const char* why)noexcept{reason=why;return false;};
 if(!Range(image,0xa01d000)||!Range(player,0x410)||!Range(block,0x6a8)||actualId>1)return fail("scope");
 Observation q{};q.player=player;q.block=block;q.actualId=actualId;std::uintptr_t vt=0,p=0;std::uint32_t count=0,base=0,row=0;
 if(!Read(r,block,vt)||vt!=image+0x2295210||!Read(r,block+8,count)||count!=1||!Read(r,block+0xc,base)||base!=actualId||!Read(r,block+0x138,p)||p!=player||!Read(r,block+0x78,q.state)||!Range(q.state,0x410)||!Read(r,q.state,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||!Read(r,q.state+8,base)||base!=actualId||!Read(r,q.state+0x408,p)||p!=block)return fail("body_owner");
 if(!Read(r,block+0x220,q.node)||!Range(q.node,0x70)||(q.node&7u)||!Read(r,q.node,vt)||vt!=image+0x2306628||!Read(r,q.node+0x20,count)||count!=1||!Read(r,q.node+0x24,base)||base!=actualId||!Read(r,q.node+0x2c,row)||row!=0x12||!Read(r,q.node+0x38,p)||p<0x30)return fail("node");
 q.primary=p-0x30;
 if(!Range(q.primary,0x70)||(q.primary&7u)||!Read(r,q.primary,vt)||vt!=image+0x23cffa8||!Read(r,q.primary+0x10,vt)||vt!=image+0x23cffd0||!Read(r,q.primary+0x30,vt)||vt!=image+0x23d0000||!Read(r,q.primary+8,p)||p!=block||!Read(r,q.primary+0x38,p)||p!=q.node||!Read(r,block+0x448,p)||p!=q.primary+0x30)return fail("phase_owner");
 std::uint64_t bytes=0,used=0,stride=0,allocated=0;std::uint16_t flags=0;std::uintptr_t callback=0,next=0,array=0;
 if(!Read(r,q.primary+0x40,q.slab)||!q.slab||(q.slab&15u)||!Read(r,q.primary+0x50,bytes)||bytes<0x58||bytes>0x100000||!Range(q.slab,static_cast<std::size_t>(bytes))||!Read(r,q.primary+0x58,used)||used>bytes||!Read(r,q.primary+0x62,flags)||!(flags&1u)||!Read(r,q.primary+0x48,q.descriptor)||!Read(r,q.primary+0x68,q.records))return fail("allocation");
 const auto within=[&](std::uintptr_t x,std::size_t n)noexcept{return Range(x,n)&&x>=q.slab&&x-q.slab<=bytes&&n<=bytes-(x-q.slab);};
 if((q.descriptor&7u)||(q.records&15u)||!within(q.descriptor,0x28)||!within(q.records,0x30)||!Read(r,q.descriptor,callback)||callback!=image+0x645940||!Read(r,q.descriptor+8,next)||next||!Read(r,q.descriptor+0x10,array)||array!=q.records||!Read(r,q.descriptor+0x18,stride)||stride!=0x30||!Read(r,q.descriptor+0x20,allocated)||allocated!=1)return fail("descriptor");
 if(q.descriptor<q.records+0x30&&q.records<q.descriptor+0x28)return fail("array_alias");
 if(!Read(r,q.records+0x20,p))return fail("record_read");
 if(p)return fail("record_not_null");out=q;reason="null_record";return true;
}
template<class R>bool ReadPair(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,unsigned actualId,Observation& out,const char*& reason)noexcept{
 out={};Observation a{},b{};if(!ReadOnce(r,image,player,block,actualId,a,reason)||!ReadOnce(r,image,player,block,actualId,b,reason))return false;
 if(!Same(a,b)){reason="pair_changed";return false;}out=a;return true;
}
}
