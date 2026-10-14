#pragma once
#include "mgscoop/native_pose_vehicle.hpp"
#include "native_pose_birth.hpp"
#include "carry_queue_lifetime.hpp"
#include "native_pose_carry_participants.hpp"
#include "native_pose_parts_diagnostic.hpp"
#include "dual_parts_local_profile.hpp"
#include "vehicle_pose_core_read.hpp"
#include "vehicle_lifetime_read.hpp"
#include "vehicle_resource_lifetime.hpp"
#include "vehicle_registry_read.hpp"
#include "vehicle_cleanup_camera_read.hpp"
#include "vehicle_pose_callback_contract.hpp"
namespace mgscoop::native_pose_vehicle_policy {
namespace pub=partner_input_hook;
namespace cp=native_pose_carry_participants;
namespace pd=coop_passenger::detail;
namespace ql=carry_queue_lifetime;
namespace birth=native_pose_birth;
namespace vc=vehicle_controller_read;
struct RegistryCycle {std::uintptr_t head=0,next=0,previous=0;std::uint64_t pending=0;std::uint32_t flags=0;};
inline bool RegistryNoWork(const RegistryCycle& c) noexcept {
 return c.head&&c.next==c.head&&c.previous==c.head&&!c.pending&&
 (!(c.flags&4u)||((c.flags&8u)&&!(c.flags&1u)));
}
inline bool SameCycle(const RegistryCycle& a,const RegistryCycle& b) noexcept {
 return a.head==b.head&&a.next==b.next&&a.previous==b.previous&&a.pending==b.pending&&a.flags==b.flags;
}
struct Snapshot {
 cp::Pair pair{};ql::Owner queue{};birth::Receipt births[2]{};std::uint64_t lifecycle=0;
 vc::Observation controllers{};pd::EngineRead engine{};pd::Unit unit{};
 vehicle_lifetime_read::CallbackCensus callbacks{};
 vehicle_lifetime_read::Allocation allocation{};vehicle_resource_lifetime::Owner resource{};
 vehicle_registry_read::Lookup lookup{};vehicle_lifetime_read::UI ui{};
 vehicle_cleanup_camera_read::Owner camera{};
 RegistryCycle registryCycle{};
};
inline bool SameUnitOwner(const pd::Unit& a,const pd::Unit& b) noexcept {
 return a.service==b.service&&a.object==b.object&&a.records==b.records&&a.bitmap==b.bitmap&&a.unit==b.unit&&
 a.metadata==b.metadata&&a.variantTable==b.variantTable&&a.variant==b.variant&&a.vehicle==b.vehicle&&
 a.allocated==b.allocated&&a.type==b.type&&a.baseCount==b.baseCount&&a.extraCount==b.extraCount&&a.count==b.count;
}
inline bool SameControllerOwner(const vc::Observation& a,const vc::Observation& b) noexcept {
 if(a.player!=b.player||a.data!=b.data||a.state!=b.state||a.ids!=b.ids||a.pool!=b.pool||a.parts!=b.parts||
 a.blocks!=b.blocks||a.blockTable!=b.blockTable)return false;
 for(unsigned k=0;k<2;++k){const auto& x=a.body[k];const auto& y=b.body[k];
  if(x.block!=y.block||x.primary!=y.primary||x.node!=y.node||x.storage!=y.storage||x.header!=y.header||
   x.record!=y.record||x.bitmap!=y.bitmap||x.id!=y.id||x.pool!=y.pool||x.part!=y.part)return false;
 }return true;
}
inline bool BirthsCurrent(const Snapshot& s) noexcept {
 if(!s.lifecycle||!s.births[0].serial||!s.births[1].serial||s.births[0].serial==s.births[1].serial)return false;
 for(unsigned k=0;k<2;++k){const auto& b=s.pair.body[k];if(!birth::Current(s.births[k],{s.pair.player,b.block,b.state,b.cs,b.entry}))return false;}
 return true;
}
inline bool SameRetention(const Snapshot& a,const Snapshot& b) noexcept {
 return BirthsCurrent(b)&&a.lifecycle==b.lifecycle&&a.births[0].serial==b.births[0].serial&&a.births[1].serial==b.births[1].serial&&
 cp::Same(a.pair,b.pair)&&ql::Same(a.queue,b.queue)&&SameControllerOwner(a.controllers,b.controllers)&&SameUnitOwner(a.unit,b.unit)&&
 vehicle_lifetime_read::Same(a.allocation,b.allocation)&&vehicle_resource_lifetime::Same(a.resource,b.resource)&&
 a.lookup.provider==b.lookup.provider&&a.lookup.holder==b.lookup.holder&&a.lookup.descriptor==b.lookup.descriptor&&
 a.lookup.primary==b.lookup.primary&&a.lookup.query==b.lookup.query&&a.lookup.queryOwner==b.lookup.queryOwner&&a.lookup.queryMode==b.lookup.queryMode&&
 a.registryCycle.head==b.registryCycle.head&&RegistryNoWork(b.registryCycle)&&
 vehicle_lifetime_read::Same(a.ui,b.ui)&&a.camera.player==b.camera.player&&a.camera.data==b.camera.data&&
 a.camera.component==b.camera.component&&a.camera.holder==b.camera.holder&&a.camera.resource==b.camera.resource&&a.camera.interface==b.camera.interface;
}
inline bool Coherent(const Snapshot& a,const Snapshot& b) noexcept {
 return SameRetention(a,b)&&cp::CoherentCallbacks(a.pair.body[0],b.pair.body[0])&&cp::CoherentCallbacks(a.pair.body[1],b.pair.body[1])&&
 vc::Same(a.controllers,b.controllers)&&pd::Same(a.engine,b.engine)&&pd::Same(a.unit,b.unit)&&
 vehicle_registry_read::Same(a.lookup,b.lookup)&&vehicle_cleanup_camera_read::Same(a.camera,b.camera)&&SameCycle(a.registryCycle,b.registryCycle);
}
inline bool LeaseSame(const pub::NativePoseVehicleLease& a,const pub::NativePoseVehicleLease& b) noexcept {
 return a.serial&&a.serial==b.serial&&a.generation&&a.generation==b.generation&&a.sceneEpoch&&a.sceneEpoch==b.sceneEpoch;
}
inline bool SameOwnership(const pub::NativePoseVehicleOwnership& a,const pub::NativePoseVehicleOwnership& b) noexcept {
 if(!LeaseSame(a.lease,b.lease)||a.image!=b.image||a.player!=b.player||a.go!=b.go||a.data!=b.data||a.state!=b.state||a.vars!=b.vars||
 a.birthLifecycle!=b.birthLifecycle||a.queueRoot!=b.queueRoot||a.queueSlab!=b.queueSlab||a.queueHead!=b.queueHead||a.queued!=b.queued||
 a.snapshot!=b.snapshot||a.queueBytes!=b.queueBytes||a.thread!=b.thread||a.sequence!=b.sequence||a.purposes!=b.purposes||a.pending!=b.pending||
 a.sharedSerial!=b.sharedSerial||a.vehicle!=b.vehicle)return false;
 for(unsigned k=0;k<2;++k)if(a.block[k]!=b.block[k]||a.ps[k]!=b.ps[k]||a.cs[k]!=b.cs[k]||a.entry[k]!=b.entry[k]||
 a.modelAllocation[k]!=b.modelAllocation[k]||a.callbackSlab[k]!=b.callbackSlab[k]||a.controller[k]!=b.controller[k]||a.controllerNode[k]!=b.controllerNode[k]||
 a.controllerStorage[k]!=b.controllerStorage[k]||a.controllerRecord[k]!=b.controllerRecord[k]||a.birth[k]!=b.birth[k]||a.pool[k]!=b.pool[k]||
 a.callbackCount[k]!=b.callbackCount[k]||a.callbackMask[k]!=b.callbackMask[k]||a.phase[k]!=b.phase[k]||a.nextPhase[k]!=b.nextPhase[k]||
 a.nativeBinding[k]!=b.nativeBinding[k]||a.currentVehicle[k]!=b.currentVehicle[k]||a.candidate[k]!=b.candidate[k]||a.seat[k]!=b.seat[k])return false;
 return true;
}
inline bool ProtectionShape(const pub::NativePoseVehicleProtection& p,const pub::NativePoseVehicleOwnership& o) noexcept {
 return p.installationSerial&&p.leaseSerial==o.lease.serial&&p.generation==o.lease.generation&&p.sceneEpoch==o.lease.sceneEpoch&&
 p.guardMask==pub::nativeVehicleAllGuards&&p.image==o.image&&p.player==o.player&&p.data==o.data&&p.vehicle==o.vehicle&&
 (p.vehicle&0xfe00u)==0x6400u&&p.allocated&&p.allocated<=512&&(p.vehicle&0x1ffu)<p.allocated&&p.seats&&p.seats<=4&&
 p.world&&p.registry&&p.service&&p.vehicleObject&&p.records&&p.bitmap&&p.unit&&p.metadata&&p.variantTable&&
 p.vehicleSlab&&p.vehicleHead&&p.vehicleBytes&&p.resource&&p.resourceInterface&&p.resourceSlab&&p.resourceHead&&p.resourceBytes&&
 p.registryProvider&&p.registryHolder&&p.registryDescriptor&&p.queryProxy&&p.uiPrimary&&p.uiInterface&&p.uiProvider&&p.uiProviderVT&&p.uiGetter&&
 p.cameraComponent&&p.cameraHolder&&p.cameraResource&&p.cameraInterface;
}
inline pub::NativePoseVehicleProtection ProtectionFrom(const Snapshot& s,const pub::NativePoseVehicleOwnership& o,
 std::uint64_t installationSerial) noexcept {
 pub::NativePoseVehicleProtection p{};p.installationSerial=installationSerial;p.leaseSerial=o.lease.serial;
 p.generation=o.lease.generation;p.sceneEpoch=o.lease.sceneEpoch;p.guardMask=pub::nativeVehicleAllGuards;
 p.image=o.image;p.player=o.player;p.data=o.data;p.world=s.engine.world;p.registry=s.engine.registry;
 const auto& u=s.unit;p.service=u.service;p.vehicleObject=u.object;p.records=u.records;p.bitmap=u.bitmap;p.unit=u.unit;
 p.metadata=u.metadata;p.variantTable=u.variantTable;p.variant=u.variant;p.vehicle=u.vehicle;p.allocated=u.allocated;p.type=u.type;p.seats=u.count;
 p.vehicleSlab=s.allocation.slab;p.vehicleHead=s.allocation.head;p.vehicleBytes=s.allocation.bytes;
 p.resource=s.resource.primary;p.resourceInterface=s.resource.interface;p.resourceSlab=s.resource.slab;p.resourceHead=s.resource.head;p.resourceBytes=s.resource.bytes;
 p.registryProvider=s.lookup.provider;p.registryHolder=s.lookup.holder;p.registryDescriptor=s.lookup.descriptor;p.queryProxy=s.lookup.query;
 p.uiPrimary=s.ui.primary;p.uiInterface=s.ui.interface;p.uiProvider=s.ui.provider;p.uiProviderVT=s.ui.providerVT;p.uiGetter=s.ui.getter;
 p.cameraComponent=s.camera.component;p.cameraHolder=s.camera.holder;p.cameraResource=s.camera.resource;p.cameraInterface=s.camera.interface;
 return p;
}
inline bool SameProtection(const pub::NativePoseVehicleProtection& a,const pub::NativePoseVehicleProtection& b) noexcept {
 // Compare named values; padding never carries native proof.
 return a.installationSerial==b.installationSerial&&a.leaseSerial==b.leaseSerial&&a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.guardMask==b.guardMask&&
 a.image==b.image&&a.player==b.player&&a.data==b.data&&a.world==b.world&&a.registry==b.registry&&a.service==b.service&&a.vehicleObject==b.vehicleObject&&
 a.records==b.records&&a.bitmap==b.bitmap&&a.unit==b.unit&&a.metadata==b.metadata&&a.variantTable==b.variantTable&&a.variant==b.variant&&
 a.vehicleSlab==b.vehicleSlab&&a.vehicleHead==b.vehicleHead&&a.vehicleBytes==b.vehicleBytes&&a.resource==b.resource&&a.resourceInterface==b.resourceInterface&&
 a.resourceSlab==b.resourceSlab&&a.resourceHead==b.resourceHead&&a.resourceBytes==b.resourceBytes&&a.registryProvider==b.registryProvider&&
 a.registryHolder==b.registryHolder&&a.registryDescriptor==b.registryDescriptor&&a.queryProxy==b.queryProxy&&a.uiPrimary==b.uiPrimary&&a.uiInterface==b.uiInterface&&
 a.uiProvider==b.uiProvider&&a.uiProviderVT==b.uiProviderVT&&a.uiGetter==b.uiGetter&&a.cameraComponent==b.cameraComponent&&a.cameraHolder==b.cameraHolder&&
 a.cameraResource==b.cameraResource&&a.cameraInterface==b.cameraInterface&&a.vehicle==b.vehicle&&a.allocated==b.allocated&&a.type==b.type&&a.seats==b.seats;
}
template<class Reader> bool ReadResources(Reader& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t data,
 const pub::NativePoseVehicleProtection& p,Snapshot& s) noexcept {
 constexpr std::size_t span=0xa01d000;
 if(!pd::ReadEngine(r,image,span,player,s.engine)||!pd::ReadUnit(r,image,p.service,p.vehicle,p.unit,s.unit)||
 !vehicle_lifetime_read::ReadAllocation(r,image,span,p.service,s.allocation)||
 !vehicle_resource_lifetime::Read(r,image,s.unit,s.resource)||
 !vehicle_registry_read::Observe(r,image,span,s.engine,s.unit,s.lookup)||!vehicle_registry_read::Callable(r,image,s.unit,s.lookup)||
 !pd::Get(r,s.lookup.descriptor+0x38,s.registryCycle.flags)||!pd::Get(r,s.lookup.holder+0x10,s.registryCycle.head)||
 !pd::Range(s.registryCycle.head,0x10)||!pd::Get(r,s.registryCycle.head,s.registryCycle.next)||
 !pd::Get(r,s.registryCycle.head+8,s.registryCycle.previous)||!pd::Get(r,s.lookup.holder+0x18,s.registryCycle.pending)||
 !RegistryNoWork(s.registryCycle)||
 !vehicle_lifetime_read::ReadUI(r,image,span,player,s.engine,s.ui)||
 !vehicle_cleanup_camera_read::Read(r,image,player,data,s.camera))return false;
 return true;
}
inline bool AcquisitionReady(const Snapshot& s) noexcept {
 const auto& b=s.controllers.body[1];
 for(unsigned seat=0;seat<s.unit.count;++seat)if(s.unit.rider[seat]==1)return false;
 return BirthsCurrent(s)&&s.pair.data==s.queue.data&&s.pair.player==s.queue.player&&s.queue.snapshot==s.pair.scriptState&&
 s.pair.body[1].freeCallbacks>=2&&s.callbacks.retired[1]&&!s.callbacks.present[1]&&!s.callbacks.eventMask[1]&&
 b.raw[0x14]==0&&b.raw[0x15]==0&&!s.engine.bound[1];
}
inline bool LifecycleCompatible(const Snapshot& s) noexcept {
 const auto& b=s.controllers.body[1];const auto phase=b.raw[0x14];
 if(phase!=0&&phase!=1&&phase!=2&&phase!=3&&phase!=6)return false;
 const auto target=vc::Word(b.raw+0x44);
 if(target!=0xffff&&target!=s.unit.vehicle)return false;
 if(s.engine.bound[1]&&s.engine.bound[1]!=s.unit.unit)return false;
 if(s.callbacks.eventMask[1]&&s.callbacks.eventMask[1]!=3)return false;
 unsigned peerSeats=0;for(unsigned seat=0;seat<s.unit.count;++seat)if(s.unit.rider[seat]==1)++peerSeats;
 if(peerSeats>1)return false;
 if(phase==0)return !s.engine.bound[1]&&!s.callbacks.eventMask[1];
 if(phase==2||phase==3){const unsigned seat=b.raw[0x17]&15u;
  return target==s.unit.vehicle&&s.engine.bound[1]==s.unit.unit&&s.callbacks.eventMask[1]==3&&
   seat<s.unit.count&&s.unit.rider[seat]==1;
 }return true;
}
inline bool RequestReady(const Snapshot& s,const pub::NativePoseVehicleRequest& request) noexcept {
 if((request.phase!=2&&request.phase!=3&&request.phase!=6)||request.seat>=s.unit.count||
 s.unit.rider[request.seat]!=1||!LifecycleCompatible(s))return false;
 if(request.phase==3&&request.seat!=0)return false;
 if(request.phase==2&&(request.seat<2||request.seat>3))return false;
 if(request.phase==6){const auto& b=s.controllers.body[1];
  if((b.raw[0x14]!=2&&b.raw[0x14]!=3&&b.raw[0x14]!=6)||s.engine.bound[1]!=s.unit.unit||
   (b.raw[0x17]&15u)!=request.seat)return false;
 }return true;
}
inline bool PhysicallyRetired(const Snapshot& s) noexcept {
 const auto& b=s.controllers.body[1];
 if(!BirthsCurrent(s)||b.raw[0x14]||b.raw[0x15]||vc::Word(b.raw+0x44)!=0xffff||s.engine.bound[1]||
 !s.callbacks.retired[1]||s.callbacks.present[1]||s.callbacks.eventMask[1])return false;
 for(unsigned seat=0;seat<s.unit.count;++seat)if(s.unit.rider[seat]==1)return false;
 return true;
}
template<class Reader> bool Retired(Reader& r,const Snapshot& s) noexcept {
 std::uint8_t kind=9;return PhysicallyRetired(s)&&ql::Read(r,s.queue.queued+0x40+8,kind)&&kind!=9;
}
inline bool NoPartsWork(const native_pose_parts_diagnostic::Witness& w) noexcept {
 return w.count==2&&w.realized==2&&w.local==0&&w.selected==0&&w.actualId[0]==0&&w.actualId[1]==1&&
 w.pool[0]<16&&w.pool[1]<16&&w.pool[0]!=w.pool[1]&&w.blockMatched[0]&&w.blockMatched[1]&&w.parts[0]==3&&w.parts[1]==3&&
 ((w.mask180|w.mask184)&3u)==0&&(w.flags&1u)&&!(w.flags&0x100u)&&(w.flags&0x6000u)==0x2000u;
}
inline bool PartsShape(const pub::NativePoseVehiclePartsContext& p) noexcept {
 const auto& o=p.ownership;
 for(unsigned k=0;k<2;++k)if(!o.block[k]||!o.ps[k]||!o.cs[k]||!o.entry[k]||!o.modelAllocation[k]||!o.callbackSlab[k]||
  !o.controller[k]||!o.controllerNode[k]||!o.controllerStorage[k]||!o.controllerRecord[k]||o.callbackCount[k]<2)return false;
 if(o.block[0]==o.block[1]||o.ps[0]==o.ps[1]||o.cs[0]==o.cs[1]||o.entry[0]==o.entry[1]||
 o.modelAllocation[0]==o.modelAllocation[1]||o.callbackSlab[0]==o.callbackSlab[1]||o.controller[0]==o.controller[1])return false;
 return ProtectionShape(p.protection,o)&&o.image&&o.player>=0x20&&o.go==o.player-0x20&&o.data&&o.state&&o.vars&&o.birthLifecycle&&
 o.queueRoot&&o.queueSlab&&o.queueHead&&o.queued&&o.snapshot&&o.queueBytes&&o.lease.serial&&o.lease.generation&&o.lease.sceneEpoch&&o.purposes==4&&
 !o.pending&&o.thread&&o.sequence&&!(o.sequence&1u)&&o.sharedSerial&&o.birth[0]&&o.birth[1]&&o.birth[0]!=o.birth[1]&&
 o.pool[0]<16&&o.pool[1]<16&&o.pool[0]!=o.pool[1]&&p.callerRva==0x9c2a2a&&p.status[0]==3&&p.status[1]==3&&
 !p.dirty&&(p.flags&1u)&&!(p.flags&0x100u)&&(p.flags&0x6000u)==0x2000u&&!(p.profileRefresh&1u);
}
inline bool SameParts(const pub::NativePoseVehiclePartsContext& a,const pub::NativePoseVehiclePartsContext& b) noexcept {
 return SameOwnership(a.ownership,b.ownership)&&SameProtection(a.protection,b.protection)&&a.callerRva==b.callerRva&&
 a.flags==b.flags&&a.dirty==b.dirty&&a.status[0]==b.status[0]&&a.status[1]==b.status[1]&&a.profileType==b.profileType&&
 a.profileParts==b.profileParts&&a.profileCamo==b.profileCamo&&a.profileEncoded==b.profileEncoded&&a.profileAux==b.profileAux&&
 a.profileFace==b.profileFace&&a.profileSelector==b.profileSelector&&a.profileRefresh==b.profileRefresh;
}
} // namespace mgscoop::native_pose_vehicle_policy
