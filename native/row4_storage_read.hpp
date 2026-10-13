#pragma once
#include "player_row4_inactive_read.hpp"
namespace mgscoop::row4_storage {
namespace r4=player_row4_inactive;
struct Pair {std::uintptr_t image=0,player=0;r4::Observation body[2]{};std::uint64_t bytes[2]{};};
inline bool Overlap(std::uintptr_t p,std::size_t n,std::uintptr_t q,std::size_t m)noexcept{return r4::Range(p,n)&&r4::Range(q,m)&&p<q+m&&q<p+n;}
inline bool Identity(const Pair& a,const Pair& b)noexcept{
 if(a.image!=b.image||a.player!=b.player||a.bytes[0]!=b.bytes[0]||a.bytes[1]!=b.bytes[1])return false;
 for(unsigned id=0;id<2;++id){const auto& x=a.body[id];const auto& y=b.body[id];
  if(x.player!=y.player||x.block!=y.block||x.state!=y.state||x.node!=y.node||x.primary!=y.primary||x.cell!=y.cell||x.client!=y.client||x.slab!=y.slab||x.descriptor!=y.descriptor||x.records!=y.records||x.actualId!=y.actualId)return false;
 }return true;
}
inline bool Same(const Pair& a,const Pair& b)noexcept{return Identity(a,b)&&r4::Same(a.body[0],b.body[0])&&r4::Same(a.body[1],b.body[1]);}
inline bool Valid(const Pair& p)noexcept{
 if(!r4::Range(p.image,0xa01d000)||!r4::Range(p.player,0x388))return false;
 for(unsigned id=0;id<2;++id){const auto& b=p.body[id];
  if(b.player!=p.player||b.actualId!=id||!r4::Range(b.primary,0x60)||!r4::Range(b.cell,0x18)||!r4::Range(b.client,0x68)||p.bytes[id]<0x108||p.bytes[id]>0x100000||!r4::Range(b.slab,static_cast<std::size_t>(p.bytes[id]))||!r4::Range(b.descriptor,0x28)||!r4::Range(b.records,0xe0)||(b.flags&8u))return false;
  const auto within=[&](std::uintptr_t at,std::size_t n)noexcept{return at>=b.slab&&at-b.slab<=p.bytes[id]&&n<=p.bytes[id]-(at-b.slab);};
  if(!within(b.descriptor,0x28)||!within(b.records,0xe0)||Overlap(b.descriptor,0x28,b.records,0xe0))return false;
 }
 return true;
}
// Extra exact storage footprint for the ALREADY qualified generic5C4910 hook.
// This does not manufacture a Q Owner or grant native disposal permission.
// Invalid incoming/held ranges refuse; genuine Q and callback gates stay required.
inline bool Protected(const Pair& p,std::uintptr_t at,std::size_t n)noexcept{
 if(!Valid(p)||!r4::Range(at,n))return true;
 for(unsigned id=0;id<2;++id){const auto& b=p.body[id];
  if(Overlap(at,n,b.primary,0x60)||Overlap(at,n,b.cell,0x18)||Overlap(at,n,b.client,0x68)||Overlap(at,n,b.slab,static_cast<std::size_t>(p.bytes[id])))return true;
 }return false;
}
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block0,std::uintptr_t block1,Pair& out,const char*& why)noexcept{
 out={};Pair q{};q.image=image;q.player=player;std::uintptr_t v=0;std::uint32_t flags=0,n=0;
 if(!r4::Range(image,0xa01d000)||!r4::Range(player,0x388)||!r4::Read(r,image+0x2a82c30,v)||v!=player||!r4::Read(r,player,v)||v!=image+0x2295490||!r4::Read(r,player+0xa8,v)||v||!r4::Read(r,player+0x204,flags)||!(flags&1u)||(flags&0x100u)||!r4::Read(r,player+0x208,n)||n!=2||!r4::Read(r,player+0x20c,n)||n!=2||!r4::Read(r,player+0x214,n)||n){why="current_offline_roster";return false;}
 if(!r4::ReadOnce(r,image,player,block0,0,q.body[0],why)||!r4::ReadOnce(r,image,player,block1,1,q.body[1],why)||!r4::Read(r,q.body[0].client+0x38,q.bytes[0])||!r4::Read(r,q.body[1].client+0x38,q.bytes[1])||!Valid(q)){why="row4_storage";return false;}
 const auto& a=q.body[0];const auto& b=q.body[1];
 if(a.block==b.block||a.state==b.state||a.node==b.node||a.primary==b.primary||Overlap(a.cell,0x18,b.cell,0x18)||Overlap(a.client,0x68,b.client,0x68)||Overlap(a.slab,static_cast<std::size_t>(q.bytes[0]),b.slab,static_cast<std::size_t>(q.bytes[1]))||Overlap(a.client,0x68,a.slab,static_cast<std::size_t>(q.bytes[0]))||Overlap(b.client,0x68,b.slab,static_cast<std::size_t>(q.bytes[1]))){why="row4_storage_alias";return false;}
 out=q;why="readonly_storage_tuple";return true;
}
template<class R>bool ReadPair(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block0,std::uintptr_t block1,Pair& out,const char*& why)noexcept{
 out={};Pair a{},b{};if(!ReadOnce(r,image,player,block0,block1,a,why)||!ReadOnce(r,image,player,block0,block1,b,why))return false;
 if(!Same(a,b)){why="row4_storage_changed";return false;}out=b;return true;
}
} // namespace mgscoop::row4_storage
