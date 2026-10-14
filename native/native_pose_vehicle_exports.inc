// Include outside the anonymous namespace after carry_queue_lifetime_exports.
// This file defines a real distinct purpose, never the CarryLease API.
bool RegisterNativePoseVehicleProtectionProvider(const NativePoseVehicleProtectionBindings& bindings) noexcept {
 if(!bindings.provider||!bindings.readOriginal||!bindings.allocator||g_pose_bindings.enabled||PoseRequired()||PoseOwned()||
 g_pose_held||g_pose_fault||g_shutdown)return false;
 if(g_pose_vehicle_bindings.provider)return g_pose_vehicle_bindings.provider==bindings.provider&&
  g_pose_vehicle_bindings.readOriginal==bindings.readOriginal&&g_pose_vehicle_bindings.allocator==bindings.allocator;
 g_pose_vehicle_bindings=bindings;return true;
}
bool AcquireNativePoseVehicleLease(std::uint16_t localVehicle,NativePoseVehicleLease& out) noexcept {
 out={};const DWORD incoming=GetLastError();const auto finish=[&](bool ok) noexcept {SetLastError(incoming);return ok;};
 if((localVehicle&0xfe00u)!=0x6400u||g_pose_bindings.enabled||!PoseOwned()||!t_pose.job||g_pose_fault||
 g_shutdown||g_pose_clear||g_pose_owners.pending||!PoseWorker()||!g_pose_vehicle_bindings.provider)return finish(false);
 if(g_pose_owners.held&holds::vehicle){
  const NativePoseVehicleLease same{g_pose_owners.vehicleSerial,g_pose_generation,g_pose_epoch};
  if(localVehicle!=g_pose_vehicle_protection.vehicle||!NativePoseVehicleLeaseCurrent(same))return finish(false);
  out=same;return finish(true);
 }
 if(g_pose_owners.held||g_pose_held||g_pose_vehicle_bound){g_pose_vehicle_reason.store("vehicle_purpose_conflict",std::memory_order_release);return finish(false);}
 const auto serial=g_pose_vehicle_serial+1u;
 if(!serial)return finish(false);const NativePoseVehicleLease lease{serial,g_pose_generation,g_pose_epoch};
 vehicle_core::Snapshot a{},b{};NativePoseVehicleProtection protection{};
 if(!PoseVehicleReadBase(a,nullptr))return finish(false);
 const auto first=PoseVehicleOwner(a,lease,localVehicle,nullptr);
 if(!PoseVehicleProvider(NativePoseVehicleProtectionQuery::capture,first,protection)||
 !PoseVehicleQualify(protection,first,a)||!vehicle_core::AcquisitionReady(a)||!PoseVehicleReadBase(b,nullptr))return finish(false);
 const auto last=PoseVehicleOwner(b,lease,localVehicle,nullptr);
 if(!PoseVehicleQualify(protection,last,b)||!vehicle_core::Coherent(a,b)||!vehicle_core::SameOwnership(first,last)||
 !vehicle_core::AcquisitionReady(b)||!holds::AddVehicle(g_pose_owners,serial))return finish(false);
 g_pose_vehicle_serial=serial;g_pose_vehicle_hold=b;g_pose_vehicle_protection=protection;
 g_pose_vehicle_owner=PoseVehicleOwner(b,lease,localVehicle,nullptr);g_pose_vehicle_bound=true;
 g_pose_cs=b.pair.body[1].cs;g_pose_entry=b.pair.body[1].entry;InterlockedExchange(&g_pose_held,1);
 out=lease;g_pose_vehicle_reason.store("vehicle_acquired",std::memory_order_release);return finish(true);
}
bool NativePoseVehicleLeaseCurrent(const NativePoseVehicleLease& lease) noexcept {
 const DWORD incoming=GetLastError();vehicle_core::Snapshot snapshot{};NativePoseVehicleOwnership owner{};
 const bool accepted=PoseOwned()&&vehicle_core::LeaseSame(lease,{g_pose_owners.vehicleSerial,g_pose_generation,g_pose_epoch})&&
 PoseVehicleReadCurrent(snapshot,owner,nullptr,NativePoseVehicleProtectionQuery::current);
 SetLastError(incoming);return accepted;
}
bool NativePoseVehicleCleanupCurrent(const NativePoseVehicleLease& lease) noexcept {
 const DWORD incoming=GetLastError();vehicle_core::Snapshot snapshot{};NativePoseVehicleOwnership owner{};
 const bool accepted=PoseOwned()&&vehicle_core::LeaseSame(lease,{g_pose_owners.vehicleSerial,g_pose_generation,g_pose_epoch})&&
 PoseVehicleReadCurrent(snapshot,owner,nullptr,NativePoseVehicleProtectionQuery::cleanup);
 SetLastError(incoming);return accepted;
}
bool NativePoseVehicleHeld() noexcept {return PoseOwned()&&g_pose_vehicle_bound&&g_pose_owners.held==holds::vehicle;}
const char* NativePoseVehicleRefusalName() noexcept {return g_pose_vehicle_reason.load(std::memory_order_acquire);}
bool ReadNativePoseVehicleOwnership(NativePoseVehicleOwnership& out,const PoseSharedLease* ticket) noexcept {
 out={};const DWORD incoming=GetLastError();vehicle_core::Snapshot snapshot{};
 const bool accepted=PoseVehicleReadCurrent(snapshot,out,ticket,NativePoseVehicleProtectionQuery::current);
 SetLastError(incoming);return accepted;
}
bool ReadNativePoseVehicleGuardBinding(NativePoseVehicleGuardBinding& out,const PoseSharedLease* ticket) noexcept {
 out={};const DWORD incoming=GetLastError();
 // The lifecycle guard already owns admission. Invalidation/fault/pending
 // clear must not make the saved disposal ranges or callback owners vanish.
 const bool admitted=PoseOwned()?ticket==nullptr:PoseVehicleActualShared(ticket);
 const bool accepted=admitted&&!g_pose_bindings.enabled&&g_pose_installed&&g_pose_vehicle_bound&&g_pose_held&&
 g_pose_owners.held==holds::vehicle&&g_pose_owners.vehicleSerial==g_pose_vehicle_owner.lease.serial;
 if(accepted){out.retained=g_pose_vehicle_owner;out.protection=g_pose_vehicle_protection;
  out.purposes=g_pose_owners.held;out.pending=g_pose_owners.pending;}
 SetLastError(incoming);return accepted;
}
bool RequestNativePoseVehicleLeaseClear(const NativePoseVehicleLease& lease) noexcept {
 const DWORD incoming=GetLastError();vehicle_core::Snapshot a{},b{};NativePoseVehicleOwnership first{},last{};auto reader=PoseReader;
 const bool accepted=PoseOwned()&&vehicle_core::LeaseSame(lease,{g_pose_owners.vehicleSerial,g_pose_generation,g_pose_epoch})&&
 PoseVehicleRetireQueue(lease)&&
 PoseVehicleReadCurrent(a,first,nullptr,NativePoseVehicleProtectionQuery::retired)&&vehicle_core::Retired(reader,a)&&
 PoseVehicleReadCurrent(b,last,nullptr,NativePoseVehicleProtectionQuery::retired)&&vehicle_core::Coherent(a,b)&&
 vehicle_core::SameOwnership(first,last)&&vehicle_core::Retired(reader,b)&&holds::ClearVehicle(g_pose_owners,lease.serial);
 if(accepted)InterlockedExchange(&g_pose_clear,1);SetLastError(incoming);return accepted;
}
bool ReadNativePoseVehiclePartsPoll(NativePoseVehiclePartsContext& out) noexcept {
 const DWORD incoming=GetLastError();const bool accepted=PoseVehiclePartsContext(out);SetLastError(incoming);return accepted;
}
bool SubmitNativePoseVehicleRequest(const NativePoseVehicleLease& lease,const NativePoseVehicleRequest& request) noexcept {
 return PoseVehicleSubmit(lease,request);
}
