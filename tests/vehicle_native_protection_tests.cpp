#include "vehicle_native_protection_read.hpp"
#include "vehicle_native_protection_contract.hpp"
#include <map>
#include <cstring>
#include <cstdio>
namespace p=mgscoop::vehicle_native_protection::pure;
namespace ph=mgscoop::partner_input_hook;
unsigned good=0,bad=0;void Check(bool b,const char* why){if(b)++good;else{++bad;std::printf("FAIL %s\n",why);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T>void Put(std::uintptr_t at,T value){unsigned char x[sizeof(T)]{};std::memcpy(x,&value,sizeof(T));for(std::size_t k=0;k<sizeof(T);++k)bytes[at+k]=x[k];}
 void Zero(std::uintptr_t at,std::size_t n){for(std::size_t k=0;k<n;++k)bytes[at+k]=0;}
 bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept {auto* x=static_cast<unsigned char*>(out);
  for(std::size_t k=0;k<n;++k){const auto it=bytes.find(at+k);if(it==bytes.end())return false;x[k]=it->second;}return true;}
};
ph::NativePoseVehicleGuardBinding Binding(){ph::NativePoseVehicleGuardBinding b{};auto& o=b.retained;auto& r=b.protection;
 o.image=0x140000000;o.lease={51,7,9};o.player=0x10000;o.go=o.player-0x20;o.data=0x20000;o.state=0x30000;o.vars=o.image+0x2c01360;
 o.thread=77;o.sequence=31;o.purposes=b.purposes=4;o.vehicle=0x6403;o.birth[0]=0x100000003ULL;o.birth[1]=0x200000004ULL;o.birthLifecycle=8;
 o.queueSlab=0x700000;o.queueHead=o.queueSlab;o.queueBytes=0x1000;o.queued=o.queueSlab+0x30;
 for(unsigned k=0;k<2;++k){o.block[k]=0x100000+k*0x10000;o.ps[k]=0x200000+k*0x10000;o.cs[k]=0x300000+k*0x10000;o.entry[k]=0x400000+k*0x10000;
  o.modelAllocation[k]=0x500000+k*0x10000;o.callbackSlab[k]=o.modelAllocation[k]+0x270;o.callbackCount[k]=5;
  o.controller[k]=0x600000+k*0x10000;o.controllerNode[k]=o.controller[k]+0x1000;o.controllerStorage[k]=o.controller[k]+0x2000;o.controllerRecord[k]=o.controllerStorage[k]+0x30;}
 r.installationSerial=4;r.leaseSerial=o.lease.serial;r.generation=o.lease.generation;r.sceneEpoch=o.lease.sceneEpoch;r.guardMask=31;
 r.image=o.image;r.player=o.player;r.data=o.data;r.vehicle=o.vehicle;r.allocated=23;r.type=0x12;r.seats=4;
 r.vehicleObject=0x800020;r.records=0x810000;r.bitmap=0x820000;r.unit=r.records+3*0x80;r.metadata=0x830000;r.variantTable=0x840000;r.variant=0x850000;
 r.vehicleSlab=0x810000;r.vehicleHead=r.vehicleSlab;r.vehicleBytes=0x10000;r.resource=0x860000;r.resourceInterface=r.resource+0x20;
 r.resourceSlab=0x870000;r.resourceHead=r.resourceSlab;r.resourceBytes=1248;r.registryProvider=0x880000;r.registryHolder=0x890000;r.registryDescriptor=0x8a0000;r.queryProxy=0x8b0000;
 r.uiPrimary=0x8c0000;r.uiInterface=r.uiPrimary+0x20;r.uiProvider=o.image+0x2a5d110;r.uiProviderVT=o.image+0x2278ea0;r.uiGetter=o.image+0x20f90;
 r.cameraResource=0x8d0000;r.cameraInterface=r.cameraResource+0x40;r.cameraHolder=0x8e0000;return b;
}
Memory Controllers(const ph::NativePoseVehicleOwnership& o){Memory m;
 for(unsigned k=0;k<2;++k){const auto a=o.controller[k],node=o.controllerNode[k],storage=o.controllerStorage[k],rec=o.controllerRecord[k],bitmap=a+0x3000;
  m.Put(o.block[k]+0x128,a);m.Put(a,o.image+0x23cbea8);m.Put(a+0x10,o.image+0x23cbee0);m.Put(a+0x30,o.image+0x23cbf10);m.Put(a+0x48,o.image+0x23cbf50);
  m.Put(a+8,o.block[k]);m.Put(a+0x38,node);m.Put(node,o.image+0x2306628);m.Put<std::uint32_t>(node+0x20,1);m.Put<std::uint32_t>(node+0x24,k);m.Put<std::uint32_t>(node+0x2c,0x23);m.Put(node+0x38,a+0x30);
  m.Put(a+0x50,storage);m.Put(a+0x58,storage);m.Put<std::uintptr_t>(a+0x60,0x270);m.Put<std::uintptr_t>(a+0x68,0x270);m.Put<std::uint16_t>(a+0x70,16);m.Put<std::uint16_t>(a+0x72,1);m.Put<std::uint32_t>(a+0x74,0x1c0048);m.Put(a+0x78,rec);
  m.Put(storage,o.image+0x645940);m.Put(storage+0x10,rec);m.Put<std::uintptr_t>(storage+0x18,0x240);m.Put<std::uintptr_t>(storage+0x20,1);m.Put(node+8,bitmap);m.Put<std::uint32_t>(bitmap,1);
  m.Zero(rec+0x40,0xc);m.Zero(rec+0x50,0x38);m.Put<std::uintptr_t>(rec+0x40,o.image+mgscoop::vehicle_controller_contract::phase_callbacks[4]);m.Put<std::uint8_t>(rec+0x54,4);m.Put<std::uint8_t>(rec+0x55,4);}
 return m;
}
int main(){const auto b=Binding();const auto& o=b.retained;const auto& r=b.protection;
 Check(p::Ownership(o)&&p::Held(b),"full width true owner synthetic fixture only");
 for(unsigned mask=0;mask<31;++mask){auto x=b;x.protection.guardMask=mask;Check(!p::Held(x),"incomplete installed masks refused");}
 for(unsigned k=0;k<6;++k){auto x=b;switch(k){case 0:x.protection.installationSerial=0;break;case 1:x.protection.leaseSerial++;break;case 2:x.protection.generation++;break;case 3:x.protection.sceneEpoch++;break;case 4:x.protection.player++;break;default:x.purposes=0;break;}
  Check(!p::Held(x)&&!p::Free(x,r.unit)&&!p::Allocator(x,r.resource+0x38),"unowned receipt grants no guard");}
 const std::uintptr_t pointers[]={r.vehicleObject-0x20,r.queryProxy,r.resource,r.metadata,r.variantTable,r.variant,r.registryHolder,r.registryDescriptor,r.cameraResource,r.cameraHolder,r.uiPrimary,r.uiProvider,
  r.vehicleSlab,r.vehicleSlab+r.vehicleBytes-1,r.resourceSlab,r.resourceSlab+r.resourceBytes-1,o.queueSlab,o.queueSlab+o.queueBytes-1,
  o.block[0],o.block[1],o.ps[0],o.ps[1],o.cs[0],o.cs[1],o.modelAllocation[0],o.modelAllocation[1],o.callbackSlab[0],o.callbackSlab[1],o.controller[0],o.controller[1],o.controllerStorage[0]+0x26f};
 for(const auto at:pointers)Check(p::Free(b,at),"exact owner or range retained");
 for(const auto at:{std::uintptr_t(0),r.vehicleSlab+r.vehicleBytes,r.resourceSlab+r.resourceBytes,o.queueSlab+o.queueBytes,o.controllerStorage[0]+0x270,std::uintptr_t(0x999999)})Check(!p::Free(b,at),"unrelated free remains original");
 Check(p::Allocator(b,r.vehicleObject+0x40)&&p::Allocator(b,r.resource+0x38)&&!p::Allocator(b,r.resource+0x40),"5C4910 composed exact holder addresses");
 for(auto at:{std::uintptr_t(0xb2e840),std::uintptr_t(0xb2d750),std::uintptr_t(0xb361b0),std::uintptr_t(0xb2f630)}){
  Check(p::Mutation(b,at,r.vehicleObject-0x20)&&!p::Mutation(b,at,r.vehicleObject),"vehicle primary scoped");}
 for(auto at:{std::uintptr_t(0x16f9580),std::uintptr_t(0x16fb950),std::uintptr_t(0x16f63c0)})Check(p::Mutation(b,at,r.resource),"resource primary scoped");
 Check(p::Mutation(b,0x16f82c0,r.resourceInterface,3)&&!p::Mutation(b,0x16f82c0,r.resourceInterface,4),"exact instance bitmap deactivate protected");
 Check(p::Mutation(b,0xb2ac60,r.vehicleObject,1)&&!p::Mutation(b,0xb2ac60,r.vehicleObject,0),"only held metadata category");
 Check(p::Mutation(b,0xbf98d0,r.registryProvider,0x32)&&!p::Mutation(b,0xbf98d0,r.registryProvider,0x31),"exact class registry registration");
 Check(!p::Mutation(b,0xbf4a80,r.queryProxy)&&!p::Mutation(b,0xa27150,r.unit),"existing proxy and physical seat exit never competing hooks");
 auto m=Controllers(o);mgscoop::vehicle_controller_read::Observation control{};
 Check(p::ReadControllers(m,o,control)&&control.body[1].record==o.controllerRecord[1],"complete actual controller layout read");
 for(unsigned k=0;k<2;++k){auto wrong=m;wrong.Put(o.controller[k]+0x38,o.controllerNode[1-k]);Check(!p::ReadControllers(wrong,o,control),"cross body native node refused");}
 m=Controllers(o);Check(p::ReadControllers(m,o,control),"controller callback fixture reset");
 mgscoop::native_pose_carry_participants::Pair models{};models.body[0].callbackCount=models.body[1].callbackCount=5;models.body[0].freeCallbacks=3;models.body[1].freeCallbacks=5;
 for(unsigned k=0;k<2;++k){auto& cb=models.body[0].callbacks[k];cb.function=o.image+0x124a530;cb.owner=o.controller[0];cb.refs=1;cb.body=0;cb.event=k?0x376fd458e70aULL:0x95558436a7c8ULL;}
 mgscoop::vehicle_lifetime_read::CallbackCensus cb{};Check(p::Callbacks(o.image,models,control,cb)&&cb.eventMask[0]==3&&!cb.eventMask[1]&&cb.retired[1],"actual registered event keys and retired peer");
 models.body[0].callbacks[0].event=0x62bcc8f4fcb6ULL;Check(!p::Callbacks(o.image,models,control,cb),"old descriptor label is not native event");
 models.body[0].callbacks[0].event=0x95558436a7c8ULL;models.body[0].callbacks[0].owner=o.controller[1];Check(!p::Callbacks(o.image,models,control,cb),"cross body callback refused");
 Check(p::VehicleCallback(b,o.entry[1],o.controller[1],o.image+0x124a530,1,0x95558436a7c8ULL),"actual remote callback signature");
 Check(!p::VehicleCallback(b,o.entry[0],o.controller[1],o.image+0x124a530,1,0x95558436a7c8ULL)&&!p::VehicleCallback(b,o.entry[1],o.controller[1],o.image+0x124a530,0,0x95558436a7c8ULL),"callback entry and actual body required");
 Check(p::CallbackSlot(b,o.callbackSlab[1]+4*0x60)&&!p::CallbackSlot(b,o.callbackSlab[1]+5*0x60)&&!p::CallbackSlot(b,o.callbackSlab[0]),"callback slot range and exact strides");
 p::Cleanup cleanup{};cleanup.ownership=o;cleanup.protection=r;cleanup.controllerInterface=o.controller[1]+0x30;cleanup.depth=1;cleanup.thread=o.thread;
 Check(p::CleanupLeaf(cleanup,o,0x129f590,r.cameraInterface,o.image+0x124cc44,1,o.thread),"exact original cleanup camera leaf");
 Check(p::CleanupLeaf(cleanup,o,0xfe7000,r.uiInterface,o.image+0x124ccfe,1,o.thread)&&p::CleanupLeaf(cleanup,o,0xfe71b0,r.uiInterface,o.image+0x124ca59,1,o.thread),"exact original cleanup UI leaves");
 for(unsigned k=0;k<6;++k){auto x=cleanup;auto own=o;switch(k){case 0:x.depth=0;break;case 1:x.depth=2;break;case 2:own.birth[1]+=0x100000000ULL;break;case 3:own.sequence++;break;case 4:own.purposes=2;break;default:own.lease.serial++;break;}
  Check(!p::CleanupLeaf(x,own,0x129f590,r.cameraInterface,o.image+0x124cc44,1,o.thread),"current cleanup lifetime/owner required");}
 Check(!p::CleanupLeaf(cleanup,o,0x129f590,r.cameraInterface,o.image+0x124cc45,1,o.thread)&&!p::CleanupLeaf(cleanup,o,0xfe7000,r.uiInterface,o.image+0x124ccfe,0,o.thread),"unrelated caller and local0 untouched");
 unsigned char nativeUi[0x550]{},shadow[0x550]{};std::memset(nativeUi,0x7f,sizeof(nativeUi));std::memset(shadow,0x7f,sizeof(shadow));
 for(const auto offset:{0x544,0x4a4,0x4cc,0x4f4}){const std::uint32_t zero=0;std::memcpy(shadow+offset,&zero,4);}
 bool nativeUntouched=true;for(unsigned char value:nativeUi)nativeUntouched=nativeUntouched&&value==0x7f;
 Check(nativeUntouched&&shadow[0x544]==0&&shadow[0x543]==0x7f&&shadow[0x548]==0x7f,"original four DWORD stores isolated to private shadow");
 Memory pins;for(const auto& s:mgscoop::vehicle_native_protection_contract::sites)for(std::size_t k=0;k<s.size;++k)pins.bytes[o.image+s.rva+k]=s.bytes[k];
 for(const auto& s:mgscoop::vehicle_native_protection_contract::slots)pins.Put(o.image+s.rva,o.image+s.target);
 std::uintptr_t mismatch=0;Check(p::CodeGate(pins,o.image,0xa01d000,mismatch),"complete native original code and slots");
 for(const auto& s:mgscoop::vehicle_native_protection_contract::sites){for(const auto off:{std::size_t(0),s.size/2,s.size-1}){
  auto wrong=pins;wrong.bytes[o.image+s.rva+off]^=1;Check(!p::CodeGate(wrong,o.image,0xa01d000,mismatch)&&mismatch==s.rva,"complete span entry middle and tail mutations refused");}}
 for(const auto& s:mgscoop::vehicle_native_protection_contract::slots){auto wrong=pins;wrong.Put(o.image+s.rva,o.image+s.target+1);
  Check(!p::CodeGate(wrong,o.image,0xa01d000,mismatch)&&mismatch==s.rva,"actual getter slot drift refused");}
 std::printf("vehicle_native_protection: %u passed, %u failed\n",good,bad);return bad?1:0;
}
