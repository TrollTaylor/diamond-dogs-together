#pragma once
#include "vehicle_native_protection.hpp"
#include "coop_passenger.hpp"
#include "vehicle_registry_read.hpp"
#include "vehicle_lifetime_read.hpp"
#include "vehicle_resource_lifetime.hpp"
#include "vehicle_cleanup_camera_read.hpp"
#include "native_pose_carry_participants.hpp"
#include "vehicle_controller_read.hpp"
#include "vehicle_native_protection_contract.hpp"
namespace mgscoop::vehicle_native_protection::pure {
namespace ph=partner_input_hook;
namespace pd=coop_passenger::detail;
namespace cp=native_pose_carry_participants;
namespace vr=vehicle_registry_read;
namespace vl=vehicle_lifetime_read;
namespace rc=vehicle_resource_lifetime;
namespace cam=vehicle_cleanup_camera_read;
template<class Reader>bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept {
 mismatch=0;if(!pd::Range(image,span)||span!=0xa01d000)return false;unsigned char bytes[256]{};
 for(const auto& s:vehicle_native_protection_contract::sites){if(s.rva>span||s.size>span-s.rva){mismatch=s.rva;return false;}
  for(std::size_t off=0;off<s.size;){const auto n=s.size-off<sizeof(bytes)?s.size-off:sizeof(bytes);
   if(!r(image+s.rva+off,bytes,n)||std::memcmp(bytes,s.bytes+off,n)){mismatch=s.rva;return false;}off+=n;}}
 for(const auto& s:vehicle_native_protection_contract::slots){std::uintptr_t target=0;
  if(!pd::Get(r,image+s.rva,target)||target!=image+s.target){mismatch=s.rva;return false;}}return true;
}
struct Witness {
 pd::EngineRead engine{};pd::Unit unit{};vr::Lookup registry{};
 vl::Allocation allocation{};rc::Owner resource{};vl::UI ui{};cam::Owner camera{};
 cp::Pair models{};vehicle_controller_read::Observation controllers{};
 vl::CallbackCensus callbacks{};
};
inline bool Ownership(const ph::NativePoseVehicleOwnership& o)noexcept {
 if(!o.image||!o.lease.serial||!o.lease.generation||!o.lease.sceneEpoch||!o.player||!o.data||!o.state||
  !o.thread||!o.birth[0]||!o.birth[1]||o.birth[0]==o.birth[1]||!o.birthLifecycle||
  (o.vehicle&0xfe00u)!=0x6400u||!o.queueSlab||!o.queueHead||!o.queueBytes||!o.queued)return false;
 for(unsigned k=0;k<2;++k)if(!o.block[k]||!o.ps[k]||!o.cs[k]||!o.entry[k]||!o.modelAllocation[k]||
  !o.callbackSlab[k]||!o.controller[k]||!o.controllerNode[k]||!o.controllerStorage[k]||!o.controllerRecord[k]||o.callbackCount[k]<2)return false;
 return o.block[0]!=o.block[1]&&o.ps[0]!=o.ps[1]&&o.cs[0]!=o.cs[1]&&o.entry[0]!=o.entry[1]&&
  o.controller[0]!=o.controller[1]&&o.controllerStorage[0]!=o.controllerStorage[1];
}
// Called only by the registered immutable provider with a genuinely issued
// core ownership. This pure validator is not a way to manufacture that receipt.
template<class Reader>bool ReadControllers(Reader& r,const ph::NativePoseVehicleOwnership& o,vehicle_controller_read::Observation& out)noexcept {
 out={};out.player=o.player;out.data=o.data;out.state=o.state;std::uintptr_t vt=0,p=0;
 for(unsigned k=0;k<2;++k){auto& b=out.body[k];b.id=k;b.block=o.block[k];b.primary=o.controller[k];
  b.node=o.controllerNode[k];b.storage=o.controllerStorage[k];b.record=o.controllerRecord[k];
  std::uint32_t n=0,base=0,row=0;std::uintptr_t used=0,bytes=0,stride=0,count=0;
  std::uint16_t alignment=0,flags=0;std::uint32_t tag=0;
  if(!pd::Get(r,b.block+0x128,p)||p!=b.primary||!pd::Get(r,b.primary,vt)||vt!=o.image+0x23cbea8||
   !pd::Get(r,b.primary+0x10,vt)||vt!=o.image+0x23cbee0||!pd::Get(r,b.primary+0x30,vt)||vt!=o.image+0x23cbf10||
   !pd::Get(r,b.primary+0x48,vt)||vt!=o.image+0x23cbf50||!pd::Get(r,b.primary+8,p)||p!=b.block||
   !pd::Get(r,b.primary+0x38,p)||p!=b.node||!pd::Get(r,b.node,vt)||vt!=o.image+0x2306628||
   !pd::Get(r,b.node+0x20,n)||n!=1||!pd::Get(r,b.node+0x24,base)||base!=k||!pd::Get(r,b.node+0x2c,row)||row!=0x23||
   !pd::Get(r,b.node+0x38,p)||p!=b.primary+0x30||!pd::Get(r,b.primary+0x50,p)||p!=b.storage||
   !pd::Get(r,b.primary+0x58,b.header)||b.header!=b.storage||!pd::Get(r,b.primary+0x60,bytes)||bytes!=0x270||
   !pd::Get(r,b.primary+0x68,used)||used!=bytes||!pd::Get(r,b.primary+0x70,alignment)||alignment!=16||
   !pd::Get(r,b.primary+0x72,flags)||!(flags&1u)||!pd::Get(r,b.primary+0x74,tag)||tag!=0x1c0048||
   !pd::Get(r,b.primary+0x78,p)||p!=b.record||b.record!=b.storage+0x30||
   !pd::Get(r,b.header,vt)||vt!=o.image+0x645940||!pd::Get(r,b.header+0x10,p)||p!=b.record||
   !pd::Get(r,b.header+0x18,stride)||stride!=0x240||!pd::Get(r,b.header+0x20,count)||count!=1||
   !pd::Get(r,b.node+8,b.bitmap)||!pd::Get(r,b.bitmap,b.bitmapValue)||
   !r(b.record+0x40,b.raw,0x0c)||!r(b.record+0x50,b.raw+0x10,0x38))return false;
  const unsigned phase=b.raw[0x14],next=b.raw[0x15];
  if(phase>7||next>7||vehicle_controller_read::Adjustment(b.raw+8)||
   (vehicle_controller_read::Pointer(b.raw)!=o.image+vehicle_controller_contract::phase_callbacks[phase]&&
    !(phase==0&&!vehicle_controller_read::Pointer(b.raw))))return false;
 }
 return true;
}
inline bool Callbacks(std::uintptr_t image,const cp::Pair& models,const vehicle_controller_read::Observation& control,vl::CallbackCensus& out)noexcept {
 out={};out.retired[0]=out.retired[1]=true;
 for(unsigned k=0;k<2;++k){const auto& b=models.body[k];out.free[k]=b.freeCallbacks;out.count[k]=b.callbackCount;
  for(unsigned n=0;n<b.callbackCount;++n){const auto& cb=b.callbacks[n];
   if(cb.owner==control.body[1-k].primary)return false;
   if(cb.owner!=control.body[k].primary){if(cb.function==image+0x124a530)return false;continue;}
   if(cb.function!=image+0x124a530||cb.body!=k||cb.refs<1)return false;
   const auto key=cb.event&0xffffffffffffULL;
   // Registered event globals2ADCB38/40, not the descriptor payload at+10.
   const unsigned bit=key==0x95558436a7c8ULL?1u:key==0x376fd458e70aULL?2u:0u;
   if(!bit||(out.eventMask[k]&bit))return false;
   ++out.present[k];out.eventMask[k]|=bit;out.retired[k]=false;
  }
 }
 return true;
}
template<class Reader>bool ReadOnce(Reader& r,const ph::NativePoseVehicleOwnership& o,Witness& out)noexcept {
 out={};if(!Ownership(o))return false;std::uintptr_t manager=0,service=0,p=0;
 if(!pd::ReadEngine(r,o.image,0xa01d000,o.player,out.engine)||out.engine.registry!=o.image+0x2c38008||
  !pd::Get(r,out.engine.world+0x98,manager)||!manager||!pd::Get(r,manager+0x2a8,service)||
  !pd::Get(r,o.image+0x2c20730,p)||p!=service||
  !pd::ReadUnit(r,o.image,service,o.vehicle,0,out.unit)||
  !vr::Observe(r,o.image,0xa01d000,out.engine,out.unit,out.registry)||!vr::Callable(r,o.image,out.unit,out.registry)||
  !vl::ReadAllocation(r,o.image,0xa01d000,service,out.allocation)||
  !rc::Read(r,o.image,out.unit,out.resource)||
  !vl::ReadUI(r,o.image,0xa01d000,o.player,out.engine,out.ui)||
  out.ui.dispatcherVT!=o.image+0x224bea0||out.ui.providerVT!=o.image+0x2278ea0||out.ui.getter!=o.image+0x20f90||
  !cam::Read(r,o.image,o.player,o.data,out.camera)||
  !cp::ReadPair(r,o.image,o.player,out.models)||!ReadControllers(r,o,out.controllers)||
  !pure::Callbacks(o.image,out.models,out.controllers,out.callbacks))return false;
 if(out.models.go!=o.go||out.models.data!=o.data||out.models.scriptState!=o.state||out.models.vars!=o.vars||
  out.allocation.object!=out.unit.object||out.allocation.records!=out.unit.records||out.allocation.bitmap!=out.unit.bitmap)return false;
 for(unsigned k=0;k<2;++k){const auto& b=out.models.body[k];if(b.block!=o.block[k]||b.state!=o.ps[k]||b.cs!=o.cs[k]||b.entry!=o.entry[k]||
   b.modelAllocation!=o.modelAllocation[k]||b.callbackSlab!=o.callbackSlab[k]||b.callbackCount!=o.callbackCount[k]||b.pool!=o.pool[k]||
   out.controllers.body[k].raw[0x14]!=o.phase[k]||out.controllers.body[k].raw[0x15]!=o.nextPhase[k]||
   out.engine.bound[k]!=o.nativeBinding[k])return false;}
 return true;
}
inline bool Same(const Witness& a,const Witness& b)noexcept {
 return pd::Same(a.engine,b.engine)&&pd::Same(a.unit,b.unit)&&vr::Same(a.registry,b.registry)&&vl::Same(a.allocation,b.allocation)&&
  rc::Same(a.resource,b.resource)&&vl::Same(a.ui,b.ui)&&cam::Same(a.camera,b.camera)&&cp::Same(a.models,b.models)&&
  cp::CoherentCallbacks(a.models.body[0],b.models.body[0])&&cp::CoherentCallbacks(a.models.body[1],b.models.body[1])&&
  vehicle_controller_read::Same(a.controllers,b.controllers);
}
template<class Reader>bool Observe(Reader& r,const ph::NativePoseVehicleOwnership& o,Witness& out)noexcept {
 Witness a{},b{};out={};if(!ReadOnce(r,o,a)||!ReadOnce(r,o,b)||!Same(a,b))return false;out=b;return true;
}
inline ph::NativePoseVehicleProtection Receipt(const ph::NativePoseVehicleOwnership& o,const Witness& w,std::uint64_t serial,std::uint32_t actualMask)noexcept {
 ph::NativePoseVehicleProtection p{};p.installationSerial=serial;p.leaseSerial=o.lease.serial;p.generation=o.lease.generation;p.sceneEpoch=o.lease.sceneEpoch;
 p.guardMask=actualMask;p.image=o.image;p.player=o.player;p.data=o.data;p.world=w.engine.world;p.registry=w.engine.registry;
 p.service=w.unit.service;p.vehicleObject=w.unit.object;p.records=w.unit.records;p.bitmap=w.unit.bitmap;p.unit=w.unit.unit;
 p.metadata=w.unit.metadata;p.variantTable=w.unit.variantTable;p.variant=w.unit.variant;
 p.vehicleSlab=w.allocation.slab;p.vehicleHead=w.allocation.head;p.vehicleBytes=w.allocation.bytes;
 p.resource=w.resource.primary;p.resourceInterface=w.resource.interface;p.resourceSlab=w.resource.slab;p.resourceHead=w.resource.head;p.resourceBytes=w.resource.bytes;
 p.registryProvider=w.registry.provider;p.registryHolder=w.registry.holder;p.registryDescriptor=w.registry.descriptor;p.queryProxy=w.registry.query;
 p.uiPrimary=w.ui.primary;p.uiInterface=w.ui.interface;p.uiProvider=w.ui.provider;p.uiProviderVT=w.ui.providerVT;p.uiGetter=w.ui.getter;
 p.cameraComponent=w.camera.component;p.cameraHolder=w.camera.holder;p.cameraResource=w.camera.resource;p.cameraInterface=w.camera.interface;
 p.vehicle=o.vehicle;p.allocated=w.unit.allocated;p.type=w.unit.type;p.seats=w.unit.count;return p;
}
inline bool Held(const ph::NativePoseVehicleGuardBinding& b)noexcept {
 const auto& o=b.retained;const auto& p=b.protection;
 return (b.purposes&4u)&&o.lease.serial&&p.leaseSerial==o.lease.serial&&p.installationSerial&&p.guardMask==ph::nativeVehicleAllGuards&&
  o.image==p.image&&o.player==p.player&&o.data==p.data&&p.generation==o.lease.generation&&p.sceneEpoch==o.lease.sceneEpoch;
}
inline bool Contains(std::uintptr_t p,std::uint64_t bytes,std::uintptr_t value)noexcept{return p&&bytes&&value>=p&&value-p<bytes;}
inline bool Free(const ph::NativePoseVehicleGuardBinding& b,std::uintptr_t p)noexcept {
 if(!Held(b)||!p)return false;const auto& x=b.protection;const auto& o=b.retained;
 if(p==x.vehicleObject-0x20||p==x.queryProxy||p==x.resource||p==x.metadata||p==x.variantTable||p==x.variant||
  p==x.registryHolder||p==x.registryDescriptor||p==x.cameraResource||p==x.cameraHolder||p==x.uiPrimary||p==x.uiProvider||
  Contains(x.vehicleSlab,x.vehicleBytes,p)||Contains(x.resourceSlab,x.resourceBytes,p)||Contains(o.queueSlab,o.queueBytes,p))return true;
 for(unsigned k=0;k<2;++k)if(p==o.block[k]||p==o.ps[k]||p==o.cs[k]||p==o.modelAllocation[k]||p==o.callbackSlab[k]||
  p==o.controller[k]||Contains(o.controllerStorage[k],0x270,p))return true;return false;
}
inline bool Allocator(const ph::NativePoseVehicleGuardBinding& b,std::uintptr_t header)noexcept {
 return Held(b)&&(header==b.protection.vehicleObject-0x20+0x60||header==b.protection.resource+0x38);
}
inline unsigned Event(std::uint64_t event)noexcept {
 const auto key=event&0xffffffffffffULL;
 return key==0x95558436a7c8ULL?1u:key==0x376fd458e70aULL?2u:0u;
}
inline bool VehicleCallback(const ph::NativePoseVehicleGuardBinding& b,std::uintptr_t entry,
 std::uintptr_t owner,std::uintptr_t function,std::uint32_t body,std::uint64_t event)noexcept {
 return Held(b)&&entry==b.retained.entry[1]&&owner==b.retained.controller[1]&&
  function==b.retained.image+0x124a530&&body==1&&Event(event);
}
inline bool CallbackSlot(const ph::NativePoseVehicleGuardBinding& b,std::uintptr_t slot)noexcept {
 if(!Held(b)||slot<b.retained.callbackSlab[1])return false;
 const auto delta=slot-b.retained.callbackSlab[1];return !(delta%0x60)&&delta/0x60<b.retained.callbackCount[1];
}
inline bool Mutation(const ph::NativePoseVehicleGuardBinding& b,std::uintptr_t rva,std::uintptr_t object,std::uint32_t index=UINT32_MAX)noexcept {
 if(!Held(b))return false;const auto& p=b.protection;
 switch(rva){
  case 0xb2e840:case 0xb2d750:case 0xb361b0:case 0xb2f630:return object==p.vehicleObject-0x20;
  // BF4A80 and the other registry heads are already core-owned/composed.
  case 0xbf98d0:return object==p.registryProvider&&index==std::uint32_t(p.vehicle>>9);
  case 0x16f9580:case 0x16fb950:case 0x16f63c0:return object==p.resource;
  case 0x16f7c30:case 0x16f82c0:return object==p.resourceInterface&&index==(p.vehicle&0x1ffu);
  case 0xb2ac60:return object==p.vehicleObject&&index==std::uint32_t(p.type>>4);
  case 0x129e1c0:case 0x129e9d0:return object==p.cameraResource;
  case 0x94b650:return object==p.uiProvider;
  default:return false; // A27150 actual seat exit is deliberately permitted.
 }
}
struct Cleanup {
 ph::NativePoseVehicleOwnership ownership{};ph::NativePoseVehicleProtection protection{};
 std::uintptr_t controllerInterface=0;std::uint32_t thread=0,depth=0;
};
inline bool CleanupCurrent(const Cleanup& c,const ph::NativePoseVehicleOwnership& o,std::uint32_t actualThread)noexcept {
 return c.depth==1&&actualThread==c.thread&&o.thread==actualThread&&(o.sequence&1u)&&(o.purposes&4u)&&
  c.ownership.lease.serial==o.lease.serial&&c.ownership.lease.generation==o.lease.generation&&
  c.ownership.lease.sceneEpoch==o.lease.sceneEpoch&&c.ownership.birthLifecycle==o.birthLifecycle&&
  c.ownership.birth[0]==o.birth[0]&&c.ownership.birth[1]==o.birth[1]&&c.ownership.player==o.player&&
  c.controllerInterface==o.controller[1]+0x30&&c.protection.leaseSerial==o.lease.serial;
}
inline bool CleanupLeaf(const Cleanup& c,const ph::NativePoseVehicleOwnership& o,std::uintptr_t rva,std::uintptr_t object,std::uintptr_t caller,unsigned body,std::uint32_t thread)noexcept {
 if(!CleanupCurrent(c,o,thread)||body!=1)return false;
 if(rva==0x129f590)return object==c.protection.cameraInterface&&caller==o.image+0x124cc44;
 if(rva==0xfe7000)return object==c.protection.uiInterface&&caller==o.image+0x124ccfe;
 if(rva==0xfe71b0)return object==c.protection.uiInterface&&caller==o.image+0x124ca59;
 return false;
}
}
