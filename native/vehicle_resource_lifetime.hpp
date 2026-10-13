#pragma once
#include "coop_passenger.hpp"
namespace mgscoop::vehicle_resource_lifetime {
namespace pd=coop_passenger::detail;
struct Owner {
 std::uintptr_t primary=0,interface=0,vtable=0,secondaryVT=0,slab=0,head=0,bitmap=0,records=0,vehicleObject=0,metadataArray=0,metadata=0;
 std::uint64_t bytes=0,used=0;std::uint16_t alignment=0,flags=0,count=0;std::uint32_t tag=0;
};
template<class Reader> bool Read(Reader& r,std::uintptr_t image,const pd::Unit& unit,Owner& out) noexcept {
 out={};std::uintptr_t vehiclePrimary=0,slot=0,other=0;
 if(unit.object<0x20||unit.vehicle==0xffff||!unit.allocated||!unit.records||!unit.bitmap||!unit.metadata)return false;
 vehiclePrimary=unit.object-0x20;
 if(!pd::Get(r,vehiclePrimary+0x1c8,out.interface)||out.interface<0x20)return false;
 out.primary=out.interface-0x20;
 // Exact16F8260 constructor final interfaces.16F9550 returns primary+28;
 // B2E840 then subtracts8 and stores the secondary+20 interface at1C8.
 if(!pd::Get(r,out.primary,out.vtable)||out.vtable!=image+0x24410e8||!pd::Get(r,out.interface,out.secondaryVT)||out.secondaryVT!=image+0x2441120||
  !pd::Get(r,image+0x24410e8+0x18,slot)||slot!=image+0x16f9550||
  !pd::Get(r,out.primary+0x28,out.bitmap)||out.bitmap!=unit.bitmap||
  !pd::Get(r,out.primary+0xd8,out.vehicleObject)||out.vehicleObject!=unit.object||
  !pd::Get(r,out.primary+0xe8,out.records)||out.records!=unit.records||
  !pd::Get(r,out.primary+0x110,out.count)||out.count!=unit.allocated||
  !pd::Get(r,vehiclePrimary+0x40,out.metadataArray)||!pd::Range(out.metadataArray,16*8)||
  !pd::Get(r,out.metadataArray+std::uintptr_t(unit.type>>4)*8,out.metadata)||out.metadata!=unit.metadata||
  !pd::Get(r,out.primary+0x38,out.slab)||!out.slab||
  !pd::Get(r,out.primary+0x40,out.head)||!out.head||
  !pd::Get(r,out.primary+0x48,out.bytes)||!out.bytes||out.bytes>0x1000000||
  !pd::Get(r,out.primary+0x50,out.used)||out.used>out.bytes||
  !pd::Get(r,out.primary+0x58,out.alignment)||out.alignment<16||(out.alignment&(out.alignment-1u))||
  (out.slab&(out.alignment-1u))||!pd::Get(r,out.primary+0x5a,out.flags)||!(out.flags&1u)||
  !pd::Get(r,out.primary+0x5c,out.tag)||out.tag!=0x4003d||!pd::Range(out.slab,static_cast<std::size_t>(out.bytes))||
  !pd::Get(r,vehiclePrimary+0x1c8,other)||other!=out.interface)return false;
 return true;
}
inline bool Same(const Owner& a,const Owner& b) noexcept {
 return a.primary==b.primary&&a.interface==b.interface&&a.vtable==b.vtable&&a.secondaryVT==b.secondaryVT&&a.slab==b.slab&&a.head==b.head&&a.bitmap==b.bitmap&&
  a.records==b.records&&a.vehicleObject==b.vehicleObject&&a.metadataArray==b.metadataArray&&a.metadata==b.metadata&&
  a.bytes==b.bytes&&a.used==b.used&&a.alignment==b.alignment&&a.flags==b.flags&&a.count==b.count&&a.tag==b.tag;
}
struct Retained {
 pd::Unit unit{};Owner resource{};std::uintptr_t vehicleSlab=0,queryProxy=0;
 std::uint64_t vehicleBytes=0;
};
// Call only behind the installed core shared/exclusive admission, its distinct
// actual vehicle purpose and complete current typed owner closure. This pure
// dispatch classifier is not itself a ticket, lease or hook-live assertion.
inline bool Refuses(const Retained& hold,std::uintptr_t image,std::uintptr_t hook,std::uintptr_t object,
 std::uint32_t index=UINT32_MAX) noexcept {
 if(!hold.unit.object||hold.unit.object<0x20||!hold.resource.primary||!hold.queryProxy)return false;
 const auto primary=hold.unit.object-0x20;
 if(hook==image+0xb2e840||hook==image+0xb2d750||hook==image+0xb361b0||hook==image+0xb2f630)return object==primary;
 if(hook==image+0xbf4a80)return object==hold.queryProxy;
 if(hook==image+0x16f9580||hook==image+0x16fb950||hook==image+0x16f63c0)return object==hold.resource.primary;
 if(hook==image+0x16f7c30||hook==image+0x16f82c0)return object==hold.resource.interface&&index==(hold.unit.vehicle&0x1ffu);
 if(hook==image+0x5c4910)return object==primary+0x60||object==hold.resource.primary+0x38;
 // B2AC60 removes a metadata category via VehicleGO secondary. All other
 // categories retain the original behavior; native passenger exit A27150 is
 // deliberately absent and remains allowed on the exact retained allocation.
 if(hook==image+0xb2ac60)return object==hold.unit.object&&index==std::uint32_t(hold.unit.type>>4);
 return false;
}
inline bool Contains(std::uintptr_t base,std::uint64_t bytes,std::uintptr_t p) noexcept {
 return base&&bytes&&p>=base&&p-base<bytes;
}
inline bool ProtectedFree(const Retained& hold,std::uintptr_t p) noexcept {
 return p&&((hold.unit.object>=0x20&&p==hold.unit.object-0x20)||p==hold.queryProxy||p==hold.resource.primary||
  p==hold.unit.metadata||p==hold.unit.variantTable||p==hold.unit.variant||
  Contains(hold.vehicleSlab,hold.vehicleBytes,p)||Contains(hold.resource.slab,hold.resource.bytes,p));
}
}
