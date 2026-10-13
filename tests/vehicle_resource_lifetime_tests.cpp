#include "vehicle_resource_lifetime.hpp"
#include <map>
#include <cstdio>
namespace resource=mgscoop::vehicle_resource_lifetime;
namespace pd=mgscoop::coop_passenger::detail;
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T> void Put(std::uintptr_t p,T value){const auto* raw=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[p+i]=raw[i];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n){auto* raw=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto x=bytes.find(p+i);if(x==bytes.end())return false;raw[i]=x->second;}return true;}
};
struct Rig {
 static constexpr std::uintptr_t image=0x140000000,vehicle=0x100000,object=0x200000,slab=0x300000,metadataArray=0x400000;
 pd::Unit unit{};Memory m;
 Rig(){unit.object=vehicle+0x20;unit.vehicle=0x6408;unit.allocated=23;unit.records=0x500000;unit.bitmap=0x600000;unit.metadata=0x700000;unit.type=32;
  m.Put(vehicle+0x1c8,object+0x20);m.Put(object,image+0x24410e8);m.Put(object+0x20,image+0x2441120);
  m.Put(image+0x24410e8+0x18,image+0x16f9550);m.Put(object+0x28,unit.bitmap);m.Put(object+0xd8,unit.object);
  m.Put(object+0xe8,unit.records);m.Put(object+0x110,unit.allocated);m.Put(vehicle+0x40,metadataArray);m.Put(metadataArray+2*8,unit.metadata);
  m.Put(object+0x38,slab);m.Put(object+0x40,slab+0x100);m.Put(object+0x48,std::uint64_t(0x1000));m.Put(object+0x50,std::uint64_t(0x1000));
  m.Put(object+0x58,std::uint16_t(16));m.Put(object+0x5a,std::uint16_t(1));m.Put(object+0x5c,std::uint32_t(0x4003d));
 }
 bool Read(resource::Owner& out){return resource::Read(m,image,unit,out);}
};
unsigned checks=0,failures=0;void Check(bool x,const char* name){++checks;if(!x){++failures;std::printf("FAIL %s\n",name);}}
int main(){Rig r;resource::Owner a{},b{};
 Check(r.Read(a)&&a.vehicleObject==r.unit.object,"synthetic exact native bitmap resource owner");
 Check(r.Read(b)&&resource::Same(a,b),"same two-read resource allocation identity");
 r=Rig{};r.m.Put(Rig::object+0xd8,Rig::vehicle+0x30);Check(!r.Read(b),"D8 exact VehicleGO secondary owner required");
 r=Rig{};r.m.Put(Rig::object+0xe8,r.unit.records+0x80);Check(!r.Read(b),"exact borrowed VehicleGO records required");
 r=Rig{};r.m.Put(Rig::object+0x28,r.unit.bitmap+4);Check(!r.Read(b),"exact borrowed live bitmap required");
 r=Rig{};r.m.Put(Rig::object+0x110,std::uint16_t(24));Check(!r.Read(b),"resource native extent matches actual allocation");
 r=Rig{};r.m.Put(Rig::metadataArray+16,r.unit.metadata+8);Check(!r.Read(b),"metadata category bidirectionally closes service unit");
 r=Rig{};r.m.Put(Rig::object+0x5c,std::uint32_t(0x4003e));Check(!r.Read(b),"internal native holder tag4003D required");
 r=Rig{};r.m.Put(Rig::object+0x58,std::uint16_t(12));Check(!r.Read(b),"invalid alignment refuses");
 r=Rig{};r.m.Put(Rig::object+0x5a,std::uint16_t(0));Check(!r.Read(b),"unallocated internal holder refuses");
 r=Rig{};r.m.Put(Rig::object,Rig::image+0x2440fe0);Check(!r.Read(b)&&b.vtable==Rig::image+0x2440fe0,"unknown resource class remains read-only evidence");
 resource::Retained h{};h.unit=Rig{}.unit;h.resource=a;h.queryProxy=0x800000;h.vehicleSlab=0x900000;h.vehicleBytes=0x1000;
 Check(resource::Refuses(h,Rig::image,Rig::image+0x16f82c0,a.interface,8),"held unit native deactivation refused by exact index");
 Check(!resource::Refuses(h,Rig::image,Rig::image+0x16f82c0,a.interface,9),"other unit native deactivation preserved");
 Check(!resource::Refuses(h,Rig::image,Rig::image+0x16f82c0,a.interface+8,8),"other resource preserved");
 Check(resource::Refuses(h,Rig::image,Rig::image+0xb2e840,Rig::vehicle),"same VehicleGO initializer protected");
 Check(resource::Refuses(h,Rig::image,Rig::image+0xb2f630,Rig::vehicle),"same VehicleGO whole-instance reset protected");
 Check(resource::Refuses(h,Rig::image,Rig::image+0xbf4a80,h.queryProxy),"same descriptor query proxy protected");
 Check(resource::Refuses(h,Rig::image,Rig::image+0x5c4910,Rig::vehicle+0x60)&&resource::Refuses(h,Rig::image,Rig::image+0x5c4910,a.primary+0x38),"both actual holder disposals protected");
 Check(!resource::Refuses(h,Rig::image,Rig::image+0xa27150,h.unit.service,2),"original native passenger physical exit remains allowed");
 Check(resource::Refuses(h,Rig::image,Rig::image+0xb2ac60,h.unit.object,2)&&!resource::Refuses(h,Rig::image,Rig::image+0xb2ac60,h.unit.object,3),"only exact held metadata category protected");
 Check(resource::ProtectedFree(h,h.vehicleSlab+0x100)&&resource::ProtectedFree(h,a.slab+0x100)&&resource::ProtectedFree(h,h.queryProxy),"native slab/proxy freed range protected");
 Check(!resource::ProtectedFree(h,h.vehicleSlab+h.vehicleBytes)&&!resource::ProtectedFree(h,0xdeadbeef),"foreign frees remain original");
 std::printf("Vehicle resource: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
