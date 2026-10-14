#include "native_pose_vehicle_policy.hpp"
#include "native_pose_hold_owners.hpp"
#include "native_pose_exclusion.hpp"
#include <cstdio>
#include <map>
namespace policy=mgscoop::native_pose_vehicle_policy;
namespace pub=mgscoop::partner_input_hook;
namespace owners=mgscoop::native_pose_hold_owners;
namespace birth=mgscoop::native_pose_birth;
namespace vc=mgscoop::vehicle_controller_read;
namespace px=mgscoop::native_pose_exclusion;
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T> void Put(std::uintptr_t at,T value){const auto* raw=reinterpret_cast<const unsigned char*>(&value);for(std::size_t n=0;n<sizeof(value);++n)bytes[at+n]=raw[n];}
 bool operator()(std::uintptr_t at,void* out,std::size_t size){auto* raw=static_cast<unsigned char*>(out);for(std::size_t n=0;n<size;++n){const auto p=bytes.find(at+n);if(p==bytes.end())return false;raw[n]=p->second;}return true;}
};
struct Rig {
 static constexpr std::uintptr_t image=0x140000000;
 policy::Snapshot s{};Memory memory;
 Rig(){
  s.lifecycle=9;s.pair.player=0x100000;s.pair.go=s.pair.player-0x20;s.pair.data=0x200000;s.pair.scriptState=0x300000;s.pair.vars=image+0x2c01360;
  for(unsigned id=0;id<2;++id){auto& b=s.pair.body[id];b.id=id;b.pool=id;b.block=0x400000+id*0x1000;b.state=0x500000+id*0x1000;
   b.cs=0x600000+id*0x1000;b.entry=0x700000+id*0x1000;b.modelAllocation=0x800000+id*0x1000;b.callbackSlab=0x900000+id*0x1000;
   b.callbackCount=4;b.freeCallbacks=4;b.treeCount=2;b.carrier=0xa00000+id*0x1000;b.recipient=0xb00000+id*0x1000;
   birth::Completion c{};c.identity={s.pair.player,b.block,b.state,b.cs,b.entry};c.installedAtEntry=c.installedAtExit=c.workerAtEntry=c.workerAtExit=true;
   c.exactCaller=c.normalReturn=c.typedPair=true;c.entryThread=c.exitThread=77;c.lifecycleEpoch=s.lifecycle;s.births[id]=birth::Complete(c,11+id,s.lifecycle);
   auto& v=s.controllers.body[id];v.block=b.block;v.primary=0xc00000+id*0x1000;v.node=0xd00000+id*0x1000;
   v.storage=0xe00000+id*0x1000;v.header=v.storage;v.record=v.storage+0x30;v.bitmap=0xf00000+id*0x1000;v.id=id;v.pool=static_cast<std::uint8_t>(id);v.part=3;
   const std::uint16_t nil=0xffff;std::memcpy(v.raw+0x44,&nil,2);std::memcpy(v.raw+0x46,&nil,2);
  }
  s.controllers.player=s.pair.player;s.controllers.data=s.pair.data;s.controllers.state=s.pair.scriptState;
  s.queue.player=s.pair.player;s.queue.data=s.pair.data;s.queue.root=s.pair.data-0x20;s.queue.snapshot=s.pair.scriptState;
  s.queue.slab=0x1100000;s.queue.head=0x1100020;s.queue.queued=0x1100100;s.queue.bytes=0x1000;s.queue.used=0x1000;s.queue.flags=1;s.queue.alignment=16;
  s.queue.descriptors=1;s.queue.list[0]={s.queue.head,image+0x645940,0,s.queue.queued,0x40,2};memory.Put(s.queue.queued+0x48,std::uint8_t(0));
  s.engine.world=0x1200000;s.engine.registry=image+0x2c38008;
  s.unit.service=0x1300000;s.unit.object=0x1400020;s.unit.records=0x1500000;s.unit.bitmap=0x1600000;s.unit.unit=0x1500400;
  s.unit.metadata=0x1700000;s.unit.variantTable=0x1800000;s.unit.variant=0x1900000;s.unit.vehicle=0x6408;s.unit.allocated=23;
  s.unit.type=32;s.unit.baseCount=4;s.unit.count=4;s.unit.liveWord=1u<<8;s.unit.rider[0]=9728;
  s.allocation.object=s.unit.object;s.allocation.primary=s.unit.object-0x20;s.allocation.slab=0x1a00000;s.allocation.head=0x1a00020;
  s.allocation.records=s.unit.records;s.allocation.bytes=s.allocation.used=0x10000;s.allocation.count=23;
  s.resource.primary=0x1b00000;s.resource.interface=s.resource.primary+0x20;s.resource.slab=0x1c00000;s.resource.head=0x1c00020;s.resource.bytes=s.resource.used=0x1000;
  s.lookup.provider=0x1d00000;s.lookup.holder=0x1e00000;s.lookup.descriptor=0x1f00000;s.lookup.primary=s.unit.object-0x20;
  s.lookup.query=0x2000000;s.lookup.queryOwner=s.lookup.primary;
  s.registryCycle.head=s.registryCycle.next=s.registryCycle.previous=0x1e0020;s.registryCycle.flags=0x3e;
  s.ui.primary=0x2100000;s.ui.interface=s.ui.primary+0x20;s.ui.provider=0x2200000;s.ui.providerVT=image+0x2200000;s.ui.getter=image+0xee0000;
  s.camera.player=s.pair.player;s.camera.data=s.pair.data;s.camera.component=0x2300000;s.camera.holder=0x2400000;
  s.camera.resource=0x2500000;s.camera.interface=s.camera.resource+0x40;
  mgscoop::vehicle_lifetime_read::Callbacks(image,s.pair,s.controllers,s.callbacks);
 }
 pub::NativePoseVehicleOwnership Owner()const{
  pub::NativePoseVehicleOwnership o{};o.lease={41,5,8};o.image=image;o.player=s.pair.player;o.go=s.pair.go;o.data=s.pair.data;o.state=s.pair.scriptState;o.vars=s.pair.vars;
  o.birthLifecycle=s.lifecycle;o.vehicle=s.unit.vehicle;o.queueRoot=s.queue.root;o.queueSlab=s.queue.slab;o.queueHead=s.queue.head;o.queued=s.queue.queued;
  o.snapshot=s.queue.snapshot;o.queueBytes=s.queue.bytes;o.thread=77;o.sequence=6;o.purposes=4;o.sharedSerial=3;
  for(unsigned k=0;k<2;++k){const auto& b=s.pair.body[k];const auto& c=s.controllers.body[k];o.block[k]=b.block;o.ps[k]=b.state;o.cs[k]=b.cs;o.entry[k]=b.entry;
   o.modelAllocation[k]=b.modelAllocation;o.callbackSlab[k]=b.callbackSlab;o.controller[k]=c.primary;o.controllerNode[k]=c.node;o.controllerStorage[k]=c.storage;
   o.controllerRecord[k]=c.record;o.callbackCount[k]=b.callbackCount;o.callbackMask[k]=s.callbacks.eventMask[k];o.pool[k]=b.pool;o.birth[k]=s.births[k].serial;
   o.phase[k]=c.raw[0x14];o.nextPhase[k]=c.raw[0x15];o.nativeBinding[k]=s.engine.bound[k];o.currentVehicle[k]=vc::Word(c.raw+0x44);
   o.candidate[k]=vc::Word(c.raw+0x46);o.seat[k]=c.raw[0x17]&15u;}return o;
 }
 void Mount(unsigned body,unsigned seat){auto& b=s.pair.body[body];b.freeCallbacks-=2;
  b.callbacks[0]={image+0x124a530,s.controllers.body[body].primary,0x95558436a7c8ULL,1,body};
  b.callbacks[1]={image+0x124a530,s.controllers.body[body].primary,0x376fd458e70aULL,1,body};
  s.engine.bound[body]=s.unit.unit;s.unit.rider[seat]=static_cast<std::uint16_t>(body);
  s.controllers.body[body].raw[0x14]=seat?2:3;s.controllers.body[body].raw[0x17]=static_cast<unsigned char>(seat);
  std::memcpy(s.controllers.body[body].raw+0x44,&s.unit.vehicle,2);
  mgscoop::vehicle_lifetime_read::Callbacks(image,s.pair,s.controllers,s.callbacks);
 }
};
unsigned checks=0,failures=0;void Check(bool ok,const char* name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
int main(){Rig a,b;
 Check(policy::BirthsCurrent(a.s),"both full completed model births required");
 Check(policy::SameRetention(a.s,b.s)&&policy::Coherent(a.s,b.s),"two immediate unchanged native owners coherent");
 b.s.births[0].ready=false;Check(!policy::SameRetention(a.s,b.s),"local0 uncompleted birth refuses");
 b=Rig{};++b.s.births[1].serial;Check(!policy::SameRetention(a.s,b.s),"body1 completed birth replacement refuses");
 b=Rig{};++b.s.lifecycle;Check(!policy::SameRetention(a.s,b.s),"lifecycle change refuses even at same pointer");
 b=Rig{};++b.s.pair.body[0].modelAllocation;Check(!policy::SameRetention(a.s,b.s),"local0 model allocator retained");
 b=Rig{};++b.s.pair.body[1].callbackSlab;Check(!policy::SameRetention(a.s,b.s),"body1 native callback slab retained");
 b=Rig{};++b.s.queue.slab;Check(!policy::SameRetention(a.s,b.s),"distinct Q slab retained");
 b=Rig{};++b.s.queue.list[0].stride;Check(!policy::SameRetention(a.s,b.s),"Q descriptor contents retained");
 b=Rig{};++b.s.controllers.body[0].storage;Check(!policy::SameRetention(a.s,b.s),"local0 vehicle controller holder retained");
 b=Rig{};++b.s.controllers.body[1].record;Check(!policy::SameRetention(a.s,b.s),"body1 exact vehicle record retained");
 b=Rig{};++b.s.unit.metadata;Check(!policy::SameRetention(a.s,b.s),"vehicle metadata producer retained");
 b=Rig{};++b.s.allocation.slab;Check(!policy::SameRetention(a.s,b.s),"VehicleGO record allocator retained");
 b=Rig{};++b.s.resource.slab;Check(!policy::SameRetention(a.s,b.s),"bitmap resource allocator retained");
 b=Rig{};++b.s.lookup.query;Check(!policy::SameRetention(a.s,b.s),"real BF5450 query proxy retained");
 b=Rig{};++b.s.ui.provider;Check(!policy::SameRetention(a.s,b.s),"actual shared HUD provider retained");
 b=Rig{};++b.s.camera.interface;Check(!policy::SameRetention(a.s,b.s),"real camera resource interface retained");
 b=Rig{};b.Mount(1,0);Check(policy::SameRetention(a.s,b.s)&&!policy::Coherent(a.s,b.s),"natural vehicle callbacks may enter between frames, not across immediate closure");
 Check(policy::AcquisitionReady(a.s),"actual idle peer requires two free native callback slots");
 Check(!policy::AcquisitionReady(b.s),"already mounted peer cannot acquire second lease");
 Check(policy::LifecycleCompatible(b.s)&&policy::RequestReady(b.s,{6,1,0}),"genuine driver can submit original native exit request");
 Check(!policy::RequestReady(b.s,{3,1,2})&&!policy::RequestReady(b.s,{2,1,0}),"driver and passenger native seat kinds remain distinct");
 b=Rig{};b.Mount(1,2);Check(policy::LifecycleCompatible(b.s)&&policy::RequestReady(b.s,{6,1,2}),"genuine rear passenger can request native exit");
 b.s.engine.bound[1]+=0x80;Check(!policy::LifecycleCompatible(b.s),"typed P120 must bind exact retained native unit");
 b=Rig{};b.Mount(1,2);b.s.unit.rider[2]=0xffff;Check(!policy::LifecycleCompatible(b.s),"phase2 without physical rider occupancy refuses");
 b=Rig{};b.Mount(1,2);b.s.unit.rider[3]=1;Check(!policy::LifecycleCompatible(b.s),"duplicate actual peer seats refuse");
 b=Rig{};b.s.unit.rider[0]=1;Check(policy::RequestReady(b.s,{3,1,0}),"original CB9470 physical claim can precede kind9 import while phase0");
 b=Rig{};Check(!policy::RequestReady(b.s,{3,1,0}),"no success-return or phase byte substitutes for native occupancy");
 b=Rig{};b.Mount(1,0);b.s.controllers.body[1].raw[0x14]=4;Check(!policy::LifecycleCompatible(b.s),"unqualified phase branch cannot ride under replica lease");
 b=Rig{};b.Mount(1,0);b.s.callbacks.eventMask[1]=1;Check(!policy::LifecycleCompatible(b.s),"partial native callback initialization retains failure");
 b=Rig{};b.Mount(1,0);std::uint16_t otherVehicle=0x6409;std::memcpy(b.s.controllers.body[1].raw+0x44,&otherVehicle,2);
 Check(!policy::LifecycleCompatible(b.s),"current vehicle target cannot drift to another allocation index");
 b=Rig{};b.Mount(1,0);b.s.pair.body[1].callbacks[0].event=0x62bcc8f4fcb6ULL;
 mgscoop::vehicle_lifetime_read::CallbackCensus corrected{};
 Check(!mgscoop::vehicle_lifetime_read::Callbacks(Rig::image,b.s.pair,b.s.controllers,corrected),"descriptor payload hash is not actual registered event key");
 b=Rig{};b.Mount(1,0);b.s.pair.body[1].callbacks[0].event|=0xabcd000000000000ULL;
 Check(mgscoop::vehicle_lifetime_read::Callbacks(Rig::image,b.s.pair,b.s.controllers,corrected),"native low48 event matching preserves high tag bits");
 policy::RegistryCycle cycle=a.s.registryCycle;
 Check(policy::RegistryNoWork(cycle),"genuine healthy3E retained vehicle class has no removal/work");
 cycle.flags=4;Check(!policy::RegistryNoWork(cycle),"pendingBF4480 class work refuses");
 cycle=a.s.registryCycle;cycle.flags|=1;Check(!policy::RegistryNoWork(cycle),"pending native class removal refuses");
 cycle=a.s.registryCycle;cycle.pending=1;Check(!policy::RegistryNoWork(cycle),"queued vehicle class replacement refuses");
 cycle=a.s.registryCycle;cycle.next+=8;Check(!policy::RegistryNoWork(cycle),"nonempty actual class sentinel refuses");
 b=Rig{};b.s.registryCycle.head+=16;Check(!policy::SameRetention(a.s,b.s),"retained vehicle class sentinel allocation cannot change");
 Memory code;const auto contractImage=Rig::image;
 for(const auto& site:mgscoop::vehicle_pose_callback_contract::sites)for(std::size_t n=0;n<site.size;++n)code.Put(contractImage+site.rva+n,site.bytes[n]);
 code.Put(contractImage+0x22e6070+0x248,contractImage+0xaf23f0);code.Put(contractImage+0x22e6070+0x250,contractImage+0xaf37b0);
 code.Put(contractImage+0x23aa770,contractImage+0xfd4910);code.Put(contractImage+0x23ab570,contractImage+0x9d20f0);
 std::uintptr_t mismatch=0;
 Check(mgscoop::vehicle_pose_callback_contract::CodeGate(code,contractImage,0xa01d000,mismatch),"full callback producer/callee/cleanup/event-data/None-factory contract gate");
 code.Put(contractImage+0x2adcb38,std::uint8_t(0));
 Check(!mgscoop::vehicle_pose_callback_contract::CodeGate(code,contractImage,0xa01d000,mismatch)&&mismatch==0x2adcb38,"registered native event data drift refuses before any callback invocation");
 b=Rig{};b.s.pair.body[1].freeCallbacks=1;Check(!policy::AcquisitionReady(b.s),"one callback slot cannot admit native init pair");
 b=Rig{};b.s.unit.rider[2]=1;Check(!policy::AcquisitionReady(b.s),"stale native occupancy refuses acquisition");
 b=Rig{};b.s.queue.snapshot+=8;Check(!policy::AcquisitionReady(b.s),"Q exact script snapshot owner required");
 Check(policy::Retired(a.memory,a.s),"native idle, no P120, no callbacks, no seat, consumed request qualifies retirement");
 b=Rig{};b.s.controllers.body[1].raw[0x14]=6;Check(!policy::Retired(b.memory,b.s),"native exit in progress retains lease");
 b=Rig{};b.s.controllers.body[1].raw[0x15]=2;Check(!policy::Retired(b.memory,b.s),"queued next native phase retains lease");
 b=Rig{};b.s.engine.bound[1]=b.s.unit.unit;Check(!policy::Retired(b.memory,b.s),"P120 physical owner must clear");
 b=Rig{};b.s.unit.rider[3]=1;Check(!policy::Retired(b.memory,b.s),"any remaining real peer rider retains lease");
 b=Rig{};b.memory.Put(b.s.queue.queued+0x48,std::uint8_t(9));Check(!policy::Retired(b.memory,b.s),"unconsumed natural requestkind9 retains Q");
 b=Rig{};b.memory.bytes.clear();Check(!policy::Retired(b.memory,b.s),"unreadable queue cannot establish retirement");
 b=Rig{};b.s.callbacks.retired[1]=false;Check(!policy::Retired(b.memory,b.s),"phase0 cannot conceal retained native callback");
 b=Rig{};std::memcpy(b.s.controllers.body[1].raw+0x44,&b.s.unit.vehicle,2);Check(!policy::Retired(b.memory,b.s),"native current target must become FFFF");
 b=Rig{};b.Mount(0,0);Check(policy::Retired(b.memory,b.s),"local0 real driver occupancy remains original during peer retirement");
 auto owner=a.Owner();auto protect=policy::ProtectionFrom(a.s,owner,101);
 Check(policy::ProtectionShape(protect,owner),"typed actual module protection receipt shape");
 auto wrong=protect;wrong.guardMask&=~pub::nativeVehicleCameraGuard;Check(!policy::ProtectionShape(wrong,owner),"missing actual camera hook keeps admission off");
 wrong=protect;wrong.guardMask&=~pub::nativeVehicleCallbackGuard;Check(!policy::ProtectionShape(wrong,owner),"missing actual callback lifetime hook keeps admission off");
 wrong=protect;wrong.guardMask&=~pub::nativeVehicleAllocationGuard;Check(!policy::ProtectionShape(wrong,owner),"missing actual allocation protection keeps admission off");
 wrong=protect;wrong.installationSerial=0;Check(!policy::ProtectionShape(wrong,owner),"boolean-like zero installation receipt refuses");
 wrong=protect;++wrong.leaseSerial;Check(!policy::ProtectionShape(wrong,owner),"actual lease serial bound");
 wrong=protect;++wrong.generation;Check(!policy::ProtectionShape(wrong,owner),"actual native generation bound");
 wrong=protect;++wrong.sceneEpoch;Check(!policy::ProtectionShape(wrong,owner),"actual scene epoch bound");
 wrong=protect;++wrong.vehicle;Check(!policy::ProtectionShape(wrong,owner),"resolved local unit bound");
 wrong=protect;++wrong.queryProxy;Check(!policy::SameProtection(protect,wrong),"proxy replacement detected independently of callback boolean");
 wrong=protect;++wrong.uiGetter;Check(!policy::SameProtection(protect,wrong),"native shared HUD getter exact binding retained");
 wrong=protect;++wrong.cameraResource;Check(!policy::SameProtection(protect,wrong),"native camera resource exact binding retained");
 wrong=protect;++wrong.resourceBytes;Check(!policy::SameProtection(protect,wrong),"resource allocation extent retained");
 auto otherOwner=owner;++otherOwner.sharedSerial;Check(!policy::SameOwnership(owner,otherOwner),"shared receipt serial cannot be cached from another poll");
 otherOwner=owner;++otherOwner.birth[0];Check(!policy::SameOwnership(owner,otherOwner),"both complete births included in public receipt");
 otherOwner=owner;otherOwner.callbackMask[1]=3;Check(!policy::SameOwnership(owner,otherOwner),"native callback state part of immediate ownership closure");
 pub::NativePoseVehiclePartsContext parts{};parts.ownership=owner;parts.protection=protect;parts.callerRva=0x9c2a2a;parts.flags=0x2001;parts.status[0]=parts.status[1]=3;
 Check(policy::PartsShape(parts)&&policy::SameParts(parts,parts),"separate vehicle parts context under actual even shared receipt");
 auto badParts=parts;badParts.ownership.purposes=3;Check(!policy::PartsShape(badParts),"carry purpose cannot alias vehicle parts permission");
 badParts=parts;badParts.ownership.pending=4;Check(!policy::PartsShape(badParts),"pending clear cannot admit parts work");
 badParts=parts;badParts.ownership.sequence|=1;Check(!policy::PartsShape(badParts),"writer cannot pose as cold shared parts");
 badParts=parts;badParts.ownership.sharedSerial=0;Check(!policy::PartsShape(badParts),"no synthetic shared ticket");
 badParts=parts;badParts.dirty=1;Check(!policy::PartsShape(badParts),"local0 dirty bit never hidden");
 badParts=parts;badParts.dirty=2;Check(!policy::PartsShape(badParts),"body1 dirty bit never hidden");
 badParts=parts;badParts.profileRefresh=1;Check(!policy::PartsShape(badParts),"native desired/current refresh blocks parts original");
 badParts=parts;badParts.ownership.cs[0]=badParts.ownership.cs[1];Check(!policy::PartsShape(badParts),"two independent actual model CS owners required");
 badParts=parts;++badParts.profileFace;Check(!policy::SameParts(parts,badParts),"late original profile change faults retained purpose");
 owners::Owners hold{};Check(owners::AddVehicle(hold,41)&&hold.held==4&&hold.vehicleSerial==41,"vehicle has distinct owner bit and serial");
 Check(!owners::AddSleep(hold)&&!owners::AddCarry(hold,7),"sleep/carry cannot overwrite native vehicle retention");
 Check(!owners::ClearSleep(hold)&&!owners::ClearCarry(hold,41),"waking or carry end cannot release vehicle");
 Check(!owners::ClearVehicle(hold,42)&&owners::ClearVehicle(hold,41),"exact native vehicle owner requests retirement");
 Check(!owners::Closed(hold,false)&&hold.held==4&&hold.pending==4&&hold.vehicleSerial==41,"failed cleanup preserves pending retained obligation");
 Check(!owners::Closed(hold,true)&&!hold.held&&!hold.vehicleSerial,"clean vehicle retirement preserves Closed carry-only return semantics");
 px::Admission admission{true,true,true,true,true,true,5,8,77};px::Gate gate;px::WriteTicket writer;owners::Owners retained{};
 owners::AddVehicle(retained,41);owners::ClearVehicle(retained,41);
 Check(gate.TryWrite(admission,writer)==px::Status::entered,"actual exclusion writer receipt");
 bool published=false;Check(!gate.EndWritePublish(77,5,8,false,true,writer,[&]() noexcept {published=true;owners::Closed(retained,true);})&&
 !published&&retained.held==4&&writer.Armed(),"actual cleanup failure cannot publish vehicle clear");
 px::Gate good;px::WriteTicket goodWriter;owners::Owners clean{};owners::AddVehicle(clean,41);owners::ClearVehicle(clean,41);
 Check(good.TryWrite(admission,goodWriter)==px::Status::entered&&good.EndWritePublish(77,5,8,true,true,goodWriter,[&]() noexcept {owners::Closed(clean,true);})&&
 !clean.held&&good.Idle(),"only successful actual writer publish clears vehicle");
 px::ReadTicket read;Check(good.TryRead(77,read)==px::Status::entered&&good.OwnsRead(77,read)&&!good.OwnsRead(78,read),"typed shared admission cannot be manufactured by thread boolean");
 good.StopNewWriters();Check(good.OwnsRead(77,read),"stopped new writers do not erase existing lifecycle guard ticket");
 Check(good.EndRead(77,read)&&!good.OwnsRead(77,read),"released shared ticket grants no retained binding inspection");
 std::printf("Vehicle core retention: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
