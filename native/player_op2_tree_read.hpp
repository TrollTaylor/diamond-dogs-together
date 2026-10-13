#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
namespace mgscoop::player_op2_tree {
inline constexpr unsigned capacity=70;
struct Node {std::uintptr_t node=0,phase=0,parent=0,child=0,sibling=0;std::uint32_t row=0,phaseVtRva=0,handlerRva=0,depth=0;};
struct Observation {std::uintptr_t player=0,block=0,state=0;std::uint32_t actualId=0,count=0;Node nodes[capacity]{};};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& v)noexcept{return Range(p,sizeof(v))&&r(p,&v,sizeof(v));}
inline bool Same(const Observation& a,const Observation& b)noexcept{
 if(a.player!=b.player||a.block!=b.block||a.state!=b.state||a.actualId!=b.actualId||a.count!=b.count)return false;
 for(unsigned i=0;i<a.count;++i){const auto& x=a.nodes[i];const auto& y=b.nodes[i];if(x.node!=y.node||x.phase!=y.phase||x.parent!=y.parent||x.child!=y.child||x.sibling!=y.sibling||x.row!=y.row||x.phaseVtRva!=y.phaseVtRva||x.handlerRva!=y.handlerRva||x.depth!=y.depth)return false;}return true;
}
// Census ONLY of the actual registered message tree used by9C8570/BF6490.
// No handler invocation/admission or arbitrary ID/index scan. Caller owns real
// current model+scene+serialized lifetime/code receipts. Export only row and
// bounded RVAs/depth/count; pointer fields remain private to paired comparison.
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,unsigned actualId,Observation& out,const char*& why)noexcept{
 out={};const auto fail=[&](const char* s)noexcept{why=s;return false;};
 if(!Range(image,0xa01d000)||!Range(player,0x410)||!Range(block,0x6a8)||actualId>1)return fail("scope");
 Observation q{};q.player=player;q.block=block;q.actualId=actualId;std::uintptr_t p=0,vt=0,root=0;std::uint32_t n=0,base=0;
 if(!Read(r,image+0x2a82c30,p)||p!=player||!Read(r,player,vt)||vt!=image+0x2295490||!Read(r,block,vt)||vt!=image+0x2295210||!Read(r,block+8,n)||n!=1||!Read(r,block+0xc,base)||base!=actualId||!Read(r,block+0x138,p)||p!=player||!Read(r,block+0x78,q.state)||!Range(q.state,0x410)||!Read(r,q.state,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||!Read(r,q.state+8,base)||base!=actualId||!Read(r,q.state+0x408,p)||p!=block||!Read(r,block+0x190,root)||!root)return fail("body_root");
 struct Pending{std::uintptr_t at=0,parent=0;unsigned depth=0;};Pending pending[capacity]{};unsigned stack=1;pending[0]={root,0,0};
 while(stack){const auto next=pending[--stack];if(!Range(next.at,0x70)||(next.at&7u)||q.count==capacity)return fail("tree_bound");
  for(unsigned i=0;i<q.count;++i)if(q.nodes[i].node==next.at)return fail("tree_cycle");
  auto& v=q.nodes[q.count++];v.node=next.at;v.depth=next.depth;
  if(!Read(r,v.node,vt)||vt!=image+0x2306628||!Read(r,v.node+0x20,n)||n!=1||!Read(r,v.node+0x24,base)||base!=actualId||!Read(r,v.node+0x2c,v.row)||v.row>=capacity||!Read(r,block+0x190+v.row*8,p)||p!=v.node||!Read(r,v.node+0x38,v.phase)||!Range(v.phase,0x10)||!Read(r,block+0x3b8+v.row*8,p)||p!=v.phase||!Read(r,v.phase+8,p)||p!=v.node)return fail("registered_node");
  if(!Read(r,v.node+0x58,v.parent)||v.parent!=next.parent||!Read(r,v.node+0x50,v.child)||!Read(r,v.node+0x40,v.sibling)||!Read(r,v.phase,vt)||vt<image+0x208d000||vt>=image+0x2a00000||!Range(vt,0x30)||!Read(r,vt+0x28,p)||p<image+0x1000||p>=image+0x208d000)return fail("phase_or_links");
  v.phaseVtRva=static_cast<std::uint32_t>(vt-image);v.handlerRva=static_cast<std::uint32_t>(p-image);
  for(unsigned i=0;i+1<q.count;++i)if(q.nodes[i].row==v.row)return fail("row_alias");
  if(v.sibling){if(stack==capacity)return fail("tree_stack");pending[stack++]={v.sibling,next.parent,next.depth};}
  if(v.child){if(stack==capacity||next.depth>=capacity-1)return fail("tree_stack");pending[stack++]={v.child,v.node,next.depth+1};}
 }
 out=q;why="census_only";return true;
}
template<class R>bool ReadPair(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,unsigned actualId,Observation& out,const char*& why)noexcept{
 out={};Observation a{},b{};if(!ReadOnce(r,image,player,block,actualId,a,why)||!ReadOnce(r,image,player,block,actualId,b,why))return false;if(!Same(a,b)){why="tree_changed";return false;}out=a;return true;
}
}
