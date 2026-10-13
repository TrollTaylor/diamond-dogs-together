#pragma once
#include "coop_passenger.hpp"
namespace mgscoop::vehicle_cleanup_camera_read {
namespace pd=coop_passenger::detail;
struct Owner {
 std::uintptr_t player=0,data=0,component=0,holder=0,resource=0,interface=0;
 std::uint32_t realLocal=0,resourceLocal=0,flags=0;
};
template<class Reader> bool Read(Reader& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t data,Owner& out) noexcept {
 out={};out.player=player;out.data=data;std::uintptr_t vt=0,owner=0,slot=0;
 if(!pd::Get(r,player+0xb8,out.component)||out.component<0x20||
  !pd::Get(r,out.component,vt)||vt!=image+0x23b8250||
  !pd::Get(r,out.component-0x20,vt)||vt!=image+0x23b8218||
  !pd::Get(r,out.component+0x348,owner)||owner!=player||
  !pd::Get(r,image+0x23b8250+0x58,slot)||slot!=image+0x1115540||
  !pd::Get(r,out.component+0xba8,out.holder)||!pd::Range(out.holder,0x18)||
  !pd::Get(r,out.holder+0x10,out.resource)||!pd::Range(out.resource,0x140)||
  !pd::Get(r,out.resource,vt)||vt!=image+0x23d1ec8||
  !pd::Get(r,image+0x23d1ec8+0x18,slot)||slot!=image+0x129e990)return false;
 out.interface=out.resource+0x40;
 return pd::Get(r,out.interface,vt)&&vt==image+0x23d1f80&&
  pd::Get(r,image+0x23d1f80+0x68,slot)&&slot==image+0x129f120&&
  pd::Get(r,image+0x23d1f80+0x70,slot)&&slot==image+0x129f590&&
  pd::Get(r,out.resource+0x28,owner)&&owner==out.component&&
  pd::Get(r,out.resource+0xf8,owner)&&owner==data&&
  pd::Get(r,player+0x214,out.realLocal)&&!out.realLocal&&
  pd::Get(r,out.resource+0x104,out.resourceLocal)&&out.resourceLocal==out.realLocal&&
  pd::Get(r,out.interface+0xa4,out.flags);
}
inline bool Same(const Owner& a,const Owner& b) noexcept {
 return a.player==b.player&&a.data==b.data&&a.component==b.component&&a.holder==b.holder&&a.resource==b.resource&&
  a.interface==b.interface&&a.realLocal==b.realLocal&&a.resourceLocal==b.resourceLocal&&a.flags==b.flags;
}
// Pure leaf-call filter membership only. Runtime must independently establish
// actual selected1 job, real vehicle purpose, cleanup TLS(primary+30,body1)
// and a current two-read owner closure before suppressing the original void
// producer. A caller-supplied boolean or this helper alone cannot enable it.
inline bool CleanupReset(std::uintptr_t image,const Owner& owner,std::uintptr_t object,std::uintptr_t caller,unsigned body) noexcept {
 return image&&body==1&&caller==image+0x124cc44&&owner.player&&owner.data&&owner.interface==object&&
  owner.resource&&owner.component&&!owner.realLocal&&!owner.resourceLocal;
}
}
