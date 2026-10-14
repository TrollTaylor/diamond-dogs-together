#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
namespace mgscoop::player_row4_inactive {
struct Observation {std::uintptr_t player=0,block=0,state=0,node=0,primary=0,cell=0,client=0,slab=0,descriptor=0,records=0;std::uint32_t actualId=0,flags=0;};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& v)noexcept{return Range(p,sizeof(v))&&r(p,&v,sizeof(v));}
inline bool Same(const Observation& a,const Observation& b)noexcept{return a.player==b.player&&a.block==b.block&&a.state==b.state&&a.node==b.node&&a.primary==b.primary&&a.cell==b.cell&&a.client==b.client&&a.slab==b.slab&&a.descriptor==b.descriptor&&a.records==b.records&&a.actualId==b.actualId&&a.flags==b.flags;}
// Witness only. No native call/dispatch permission. Caller must own current
// serialized model/tree/client storage lifetime and exact full code/slot gates.
// ROOT40 is a plain18-byte instance-list cell; it has NO virtual type/refcount.
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,unsigned actualId,Observation& out,const char*& why)noexcept{
 out={};const auto fail=[&](const char* reason)noexcept{why=reason;return false;};
 if(!Range(image,0xa01d000)||!Range(player,0x410)||!Range(block,0x6a8)||actualId>1)return fail("scope");
 Observation q{};q.player=player;q.block=block;q.actualId=actualId;std::uintptr_t vt=0,p=0;std::uint32_t n=0,base=0,row=0;
 if(!Read(r,block,vt)||vt!=image+0x2295210||!Read(r,block+8,n)||n!=1||!Read(r,block+0xc,base)||base!=actualId||!Read(r,block+0x138,p)||p!=player||!Read(r,block+0x78,q.state)||!Range(q.state,0x410)||!Read(r,q.state,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||!Read(r,q.state+8,base)||base!=actualId||!Read(r,q.state+0x408,p)||p!=block)return fail("body_owner");
 if(!Read(r,block+0x210,q.node)||!Range(q.node,0x70)||(q.node&7u)||!Read(r,q.node,vt)||vt!=image+0x2306628||!Read(r,q.node+0x20,n)||n!=1||!Read(r,q.node+0x24,base)||base!=actualId||!Read(r,q.node+0x2c,row)||row!=4||!Read(r,q.node+0x38,p)||p<0x30)return fail("node");
 q.primary=p-0x30;
 if(!Range(q.primary,0x60)||(q.primary&7u)||!Read(r,q.primary,vt)||vt!=image+0x23cd798||!Read(r,q.primary+0x10,vt)||vt!=image+0x23cd7c0||!Read(r,q.primary+0x30,vt)||vt!=image+0x23cd7f0||!Read(r,q.primary+8,p)||p!=block||!Read(r,q.primary+0x38,p)||p!=q.node||!Read(r,block+0x3d8,p)||p!=q.primary+0x30)return fail("phase_owner");
 std::uintptr_t iface=0,linked=0,previous=0,next=0;
 if(!Read(r,q.primary+0x40,q.cell)||!Range(q.cell,0x18)||(q.cell&7u)||!Read(r,q.primary+0x48,iface)||iface<0x20||(iface&7u)||!Read(r,q.cell+0x10,q.client)||q.client!=iface-0x20||!Range(q.client,0x68)||!Read(r,q.client,vt)||vt!=image+0x2347e50||!Read(r,q.client+8,p)||p!=0x3dfd3f482579ULL||!Read(r,q.client+0x10,p)||p!=q.cell||!Read(r,iface,vt)||vt!=image+0x2347e80||!Read(r,q.cell,previous)||!Read(r,q.cell+8,next))return fail("client_association");
 if((previous&&(!Range(previous,0x18)||(previous&7u)||!Read(r,previous+8,linked)||linked!=q.cell))||(next&&(!Range(next,0x18)||(next&7u)||!Read(r,next,linked)||linked!=q.cell))||previous==q.cell||next==q.cell)return fail("cell_links");
 std::uint64_t bytes=0,used=0,stride=0,count=0;std::uint16_t flags=0;std::uintptr_t callback=0,array=0,following=0;
 if(!Read(r,q.client+0x58,base)||base!=actualId||!Read(r,q.client+0x28,q.slab)||!q.slab||(q.slab&15u)||!Read(r,q.client+0x38,bytes)||bytes<0x108||bytes>0x100000||!Range(q.slab,static_cast<std::size_t>(bytes))||!Read(r,q.client+0x40,used)||used>bytes||!Read(r,q.client+0x4a,flags)||!(flags&1u)||!Read(r,q.client+0x30,q.descriptor)||!Read(r,q.client+0x50,q.records))return fail("allocation");
 const auto within=[&](std::uintptr_t x,std::size_t z)noexcept{return Range(x,z)&&x>=q.slab&&x-q.slab<=bytes&&z<=bytes-(x-q.slab);};
 if((q.descriptor&7u)||(q.records&15u)||!within(q.descriptor,0x28)||!within(q.records,0xe0)||!Read(r,q.descriptor,callback)||callback!=image+0x645940||!Read(r,q.descriptor+8,following)||following||!Read(r,q.descriptor+0x10,array)||array!=q.records||!Read(r,q.descriptor+0x18,stride)||stride!=0xe0||!Read(r,q.descriptor+0x20,count)||count!=1)return fail("descriptor");
 if(q.descriptor<q.records+0xe0&&q.records<q.descriptor+0x28)return fail("array_alias");
 if(!Read(r,q.records+0xd8,q.flags))return fail("flags_read");if(q.flags&8u)return fail("transition_pending");
 out=q;why="inactive_witness";return true;
}
template<class R>bool ReadPair(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,unsigned actualId,Observation& out,const char*& why)noexcept{
 out={};Observation a{},b{};if(!ReadOnce(r,image,player,block,actualId,a,why)||!ReadOnce(r,image,player,block,actualId,b,why))return false;if(!Same(a,b)){why="pair_changed";return false;}out=a;return true;
}
}
