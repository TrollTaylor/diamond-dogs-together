// Include before PoseQueueSetter, after the real setter/import definitions.
// All mod submissions use its retained trampoline. Native index1 writes may
// not overwrite a held replica queue merely because the writer is selected1.
bool PoseVehicleSetterLive() noexcept {
 if(g_pose_bindings.enabled||!g_pose_queue_setter||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
 g_pose_patches[groups::deferredFirst].rva!=carry_native_queue_contract::setterRva||
 !PosePatchOwned(g_pose_patches[groups::deferredFirst]))return false;
 std::uintptr_t mismatch=0;return PoseDeferredCode(mismatch)&&PoseDeferredImportCurrent()&&PoseQueuePatchesLive()&&PoseVehicleCodeLive();
}
struct PoseVehicleKind9 {
 std::uintptr_t vt=0;std::uint8_t kind=9,padding[7]{};
 std::uint16_t vehicle=0xffff;std::uint8_t phase=0,cycle=0,seat=4,reserved[3]{};
};
static_assert(sizeof(PoseVehicleKind9)==24&&offsetof(PoseVehicleKind9,vehicle)==0x10&&
 offsetof(PoseVehicleKind9,phase)==0x12&&offsetof(PoseVehicleKind9,seat)==0x14,"exact retail kind9 ABI");
PoseVehicleKind9 g_pose_vehicle_last_request{};std::uint64_t g_pose_vehicle_request_owner=0;
void PoseVehicleQueuePublishClear() noexcept {g_pose_vehicle_last_request={};g_pose_vehicle_request_owner=0;}
bool PoseVehicleSubmit(const NativePoseVehicleLease& lease,const NativePoseVehicleRequest& request) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;vehicle_core::Snapshot a{},b{};NativePoseVehicleOwnership first{},last{};
 unsigned char before[0x80]{},after[0x80]{},expected[0x80]{};
 const auto operation=request.phase==6?NativePoseVehicleProtectionQuery::cleanup:NativePoseVehicleProtectionQuery::current;
 if(!PoseOwned()||!PoseVehicleSetterLive()||!vehicle_core::LeaseSame(lease,{g_pose_owners.vehicleSerial,g_pose_generation,g_pose_epoch})||
 !PoseVehicleReadCurrent(a,first,nullptr,operation)||!vehicle_core::RequestReady(a,request)||
 !PoseReader(a.queue.queued,before,sizeof(before))){SetLastError(incoming);return false;}
 PoseVehicleKind9 payload{};payload.vt=g_base+0x23aa770;payload.vehicle=a.unit.vehicle;
 payload.phase=request.phase;payload.cycle=request.cycle;payload.seat=request.seat;
 std::memcpy(expected,before,sizeof(expected));std::memcpy(expected+0x40,&payload,sizeof(payload));
 // Exact imported memcpy-backed setter. No direct queue/phase/seat write.
 SetLastError(incoming);const auto result=reinterpret_cast<std::uintptr_t>(g_pose_queue_setter(reinterpret_cast<void*>(a.pair.data),1,&payload));
 outgoing=GetLastError();
 const bool accepted=result==a.queue.queued+0x40&&PoseReader(a.queue.queued,after,sizeof(after))&&
 !std::memcmp(after,expected,sizeof(after))&&PoseVehicleReadCurrent(b,last,nullptr,operation)&&
 vehicle_core::Coherent(a,b)&&vehicle_core::SameOwnership(first,last)&&vehicle_core::RequestReady(b,request)&&PoseVehicleSetterLive();
 if(!accepted)PoseFail("pose_vehicle_queue_submit_post");
 else {g_pose_vehicle_last_request=payload;g_pose_vehicle_request_owner=lease.serial;}
 SetLastError(outgoing);return accepted&&!g_pose_fault;
}
bool PoseVehicleRetireQueue(const NativePoseVehicleLease& lease) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;vehicle_core::Snapshot a{},b{};NativePoseVehicleOwnership first{},last{};
 unsigned char before[0x80]{},after[0x80]{},expected[0x80]{};
 if(!PoseOwned()||!PoseVehicleSetterLive()||!vehicle_core::LeaseSame(lease,{g_pose_owners.vehicleSerial,g_pose_generation,g_pose_epoch})||
 !PoseVehicleReadCurrent(a,first,nullptr,NativePoseVehicleProtectionQuery::cleanup)||!vehicle_core::PhysicallyRetired(a)||
 !PoseReader(a.queue.queued,before,sizeof(before))){SetLastError(incoming);return false;}
 if(before[0x48]!=9){SetLastError(incoming);return true;}
 if(g_pose_vehicle_request_owner!=lease.serial||g_pose_vehicle_last_request.phase!=6||
 g_pose_vehicle_last_request.vehicle!=a.unit.vehicle||std::memcmp(before+0x40,&g_pose_vehicle_last_request,sizeof(g_pose_vehicle_last_request))){
  PoseFail("pose_vehicle_unretired_queue_owner");SetLastError(incoming);return false;
 }
 struct alignas(16) None {std::uintptr_t vt=0;std::uint8_t kind=0xff,padding[7]{};} payload{};
 static_assert(sizeof(payload)==16,"exact native None request size");
 // Complete original FD4970 case0 is a pure placement constructor. RAX is
 // incidental and ignored; constructor output is verified instead of treating
 // a native return value as success. No direct queued kind byte is written.
 using Factory=void(*)(void*,void*,std::uint32_t);
 SetLastError(incoming);reinterpret_cast<Factory>(g_base+0xfd4970)(nullptr,&payload,0);outgoing=GetLastError();
 if(payload.vt!=g_base+0x23ab570||payload.kind){PoseFail("pose_vehicle_none_constructor");SetLastError(outgoing);return false;}
 std::memcpy(expected,before,sizeof(expected));std::memcpy(expected+0x40,&payload,sizeof(payload));
 SetLastError(outgoing);const auto result=reinterpret_cast<std::uintptr_t>(g_pose_queue_setter(reinterpret_cast<void*>(a.pair.data),1,&payload));outgoing=GetLastError();
 const bool accepted=result==a.queue.queued+0x40&&PoseReader(a.queue.queued,after,sizeof(after))&&!std::memcmp(after,expected,sizeof(after))&&
 PoseVehicleReadCurrent(b,last,nullptr,NativePoseVehicleProtectionQuery::cleanup)&&vehicle_core::Coherent(a,b)&&
 vehicle_core::SameOwnership(first,last)&&vehicle_core::PhysicallyRetired(b)&&PoseVehicleSetterLive();
 if(!accepted)PoseFail("pose_vehicle_none_queue_post");SetLastError(outgoing);return accepted&&!g_pose_fault;
}
bool PoseVehicleStockLocalQueue(void* object,std::uint32_t actual,const void* payload,const PoseSharedLease& ticket,
 void*& returned,DWORD& outgoing) noexcept {
 const DWORD incoming=GetLastError();returned=nullptr;
 vehicle_core::Snapshot a{},b{};NativePoseVehicleOwnership first{},last{};
 std::uintptr_t vt=0,getter=0;unsigned char stub[6]{},before[0x80]{},after[0x80]{},expected[0x80]{};
 alignas(16) unsigned char snapshot[0x40]{};std::uint32_t size=0;
 const auto pointer=reinterpret_cast<std::uintptr_t>(payload);
 const bool typed=actual==0&&PoseVehicleSetterLive()&&PoseVehicleReadCurrent(a,first,&ticket,NativePoseVehicleProtectionQuery::cleanup)&&
 reinterpret_cast<std::uintptr_t>(object)==a.pair.data&&PoseRead(pointer,vt)&&
 vt>=g_base+0x208d000&&vt<g_base+0x2a00000&&PoseRead(vt,getter)&&
 getter>=g_base+0x1000&&getter<g_base+0x208d000&&PoseReader(getter,stub,sizeof(stub))&&stub[0]==0xb8&&stub[5]==0xc3;
 if(typed)std::memcpy(&size,stub+1,4);
 if(!typed||size<9||size>sizeof(snapshot)||ql::ProtectedRange(a.queue,pointer,size)||
 !PoseReader(pointer,snapshot,size)||std::memcmp(snapshot,&vt,sizeof(vt))||!PoseReader(a.queue.queued,before,sizeof(before))){
  PoseFail("pose_vehicle_foreign_queue_setter");SetLastError(incoming);return false;
 }
 std::memcpy(expected,before,sizeof(expected));std::memcpy(expected,snapshot,size);
 // FE00D0 only calls this proven pure literal-size getter and imported memcpy.
 // Give it the bounded immutable snapshot, avoiding a second virtual call on
 // a changing foreign payload pointer. All opaque native bytes are preserved.
 SetLastError(incoming);returned=g_pose_queue_setter(object,0,snapshot);outgoing=GetLastError();
 unsigned char finalStub[6]{};
 const bool accepted=reinterpret_cast<std::uintptr_t>(returned)==a.queue.queued&&PoseReader(a.queue.queued,after,sizeof(after))&&
 !std::memcmp(after,expected,sizeof(after))&&PoseReader(getter,finalStub,sizeof(finalStub))&&!std::memcmp(stub,finalStub,sizeof(stub))&&
 PoseVehicleReadCurrent(b,last,&ticket,NativePoseVehicleProtectionQuery::cleanup)&&vehicle_core::Coherent(a,b)&&
 vehicle_core::SameOwnership(first,last)&&PoseVehicleSetterLive();
 if(!accepted)PoseFail("pose_vehicle_local0_queue_post");SetLastError(outgoing);return accepted&&!g_pose_fault;
}
