// Included after the actual carry parts reader, inside the existing core
// anonymous namespace. Vehicle purpose never manufactures a carry receipt.
bool PoseQueuePatchesLive() noexcept;
void PoseVehicleQueuePublishClear() noexcept;
bool PoseVehicleActualShared(const PoseSharedLease* ticket) noexcept {
 const auto tid=GetCurrentThreadId();
 return ticket&&ticket->tracked&&ticket->thread==tid&&ticket->slot<32&&ticket->serial&&
 t_readers.serial[ticket->slot]==ticket->serial&&g_pose_gate.OwnsRead(tid,t_readers.tickets[ticket->slot]);
}
bool PoseVehicleDomain(const PoseSharedLease* ticket,std::uint32_t& selected) noexcept {
 selected=UINT32_MAX;const auto seq=static_cast<std::uint32_t>(g_window_seq);
 if(PoseOwned()){
  if(ticket||!t_pose.opened||!t_pose.job||!t_pose.context.selectedBodyOwned||t_pose.context.selectedBody!=1||
   !t_pose.context.modelReady||!(seq&1u)||GetCurrentThreadId()!=g_pose_thread)return false;
  selected=1;return true;
 }
 if(!PoseVehicleActualShared(ticket)||t_pose.opened||t_local_carry.opened||(seq&1u)||
  !seq||GetCurrentThreadId()!=g_pose_thread)return false;
 selected=0;return true;
}
thread_local bool t_vehicle_provider_call=false;
bool PoseVehicleCodeLive() noexcept {
 if(!g_pose_vehicle_bindings.provider||!g_pose_vehicle_bindings.readOriginal||!g_pose_vehicle_bindings.allocator)return false;
 auto reader=g_pose_vehicle_bindings.readOriginal;std::uintptr_t mismatch=0;
 return PoseRoutePatchesLive()&&vehicle_controller_read::CodeGate(reader,g_base,0xa01d000,mismatch)&&
  vehicle_pose_callback_contract::CodeGate(reader,g_base,0xa01d000,mismatch)&&route::CodeGate(reader,g_base,0xa01d000,mismatch);
}
bool PoseVehicleReadBase(vehicle_core::Snapshot& out,const PoseSharedLease* ticket) noexcept {
 out={};std::uint32_t selected=0;const auto seq=static_cast<std::uint32_t>(g_window_seq);
 g_pose_vehicle_reason.store("vehicle_actual_domain",std::memory_order_release);
 if(g_pose_bindings.enabled||!PoseVehicleDomain(ticket,selected)||!PoseWorker()||!PoseRequired()||!g_pose_test||
 !g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||g_pose_fault||g_shutdown||
 partner_defer::InBatch()||!NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||
 !PoseParticipantsLive()||!PoseQueuePatchesLive()||!PoseVehicleCodeLive())return false;
 pi::Arm arm{},late{};auto reader=PoseReader;
 g_pose_vehicle_reason.store("vehicle_current_arm_models_queue",std::memory_order_release);
 if(!ReadArm(arm)||!arm.enabled||!arm.native||arm.holdlog||arm.cam!=PartnerInputCam::sandbox||
 arm.gen!=g_pose_generation||arm.rates_epoch!=g_pose_epoch||arm.P!=g_pose_player||arm.go!=g_pose_go||
 arm.root1!=g_pose_root||arm.blk1!=g_pose_block||!cp::ReadPair(reader,g_base,arm.P,out.pair)||
 out.pair.go!=arm.go||out.pair.body[1].block!=arm.blk1||out.pair.body[1].state!=arm.ps1||
 !carry_queue_lifetime::ReadOwner(reader,g_base,arm.P,out.queue)||out.queue.data!=out.pair.data||
 out.queue.snapshot!=out.pair.scriptState)return false;
 AcquireSRWLockShared(&g_pose_birth_lock);out.births[0]=PoseObservedBirthLocked(0);out.births[1]=PoseObservedBirthLocked(1);
 out.lifecycle=g_pose_birth_epoch;ReleaseSRWLockShared(&g_pose_birth_lock);
 g_pose_vehicle_reason.store("vehicle_both_births",std::memory_order_release);
 if(!vehicle_core::BirthsCurrent(out)||out.births[1].serial!=g_pose_bound_birth)return false;
 if(selected==1){NativePoseJobContext c0{};if(!ReadNativePoseOwnedJobContext(c0))return false;const auto& b=out.pair.body[1];
  if(c0.player!=out.pair.player||c0.go!=out.pair.go||c0.block!=b.block||c0.state!=b.state||c0.modelCS!=b.cs||
   c0.modelEntry!=b.entry||c0.birthSerial!=out.births[1].serial||c0.generation!=arm.gen||c0.sceneEpoch!=arm.rates_epoch||
   !NativePoseVarsPairWordOwnedWindow())return false;
 }
 vehicle_pose_core_read::CoreScope scope{g_base,out.pair.player,out.pair.data,out.pair.scriptState,selected};
 g_pose_vehicle_reason.store("vehicle_typed_controllers_callbacks",std::memory_order_release);
 if(!vehicle_pose_core_read::ReadOnce(reader,scope,out.controllers)||
 !coop_passenger::detail::ReadEngine(reader,g_base,0xa01d000,out.pair.player,out.engine)||
 !vehicle_lifetime_read::Callbacks(g_base,out.pair,out.controllers,out.callbacks))return false;
 g_pose_vehicle_reason.store("vehicle_late_scope",std::memory_order_release);
 if(!ReadArm(late)||!late.enabled||!late.native||late.holdlog||late.gen!=arm.gen||late.rates_epoch!=arm.rates_epoch||
 late.P!=arm.P||late.go!=arm.go||late.root1!=arm.root1||late.blk1!=arm.blk1||late.ps1!=arm.ps1||
 static_cast<std::uint32_t>(g_window_seq)!=seq||!PoseVehicleDomain(ticket,selected)||partner_defer::InBatch()||g_pose_fault)return false;
 return true;
}
NativePoseVehicleOwnership PoseVehicleOwner(const vehicle_core::Snapshot& s,const NativePoseVehicleLease& lease,
 std::uint16_t vehicle,const PoseSharedLease* ticket) noexcept {
 NativePoseVehicleOwnership o{};o.lease=lease;o.image=g_base;o.player=s.pair.player;o.go=s.pair.go;o.data=s.pair.data;
 o.state=s.pair.scriptState;o.vars=s.pair.vars;o.birthLifecycle=s.lifecycle;o.vehicle=vehicle;
 o.thread=GetCurrentThreadId();o.sequence=static_cast<std::uint32_t>(g_window_seq);o.purposes=g_pose_owners.held;o.pending=g_pose_owners.pending;
 o.sharedSerial=ticket?ticket->serial:0;o.queueRoot=s.queue.root;o.queueSlab=s.queue.slab;o.queueHead=s.queue.head;
 o.queued=s.queue.queued;o.snapshot=s.queue.snapshot;o.queueBytes=s.queue.bytes;
 for(unsigned k=0;k<2;++k){const auto& b=s.pair.body[k];const auto& v=s.controllers.body[k];
  o.block[k]=b.block;o.ps[k]=b.state;o.cs[k]=b.cs;o.entry[k]=b.entry;o.modelAllocation[k]=b.modelAllocation;
  o.callbackSlab[k]=b.callbackSlab;o.callbackCount[k]=b.callbackCount;o.callbackMask[k]=s.callbacks.eventMask[k];
  o.pool[k]=b.pool;o.birth[k]=s.births[k].serial;o.controller[k]=v.primary;o.controllerNode[k]=v.node;
  o.controllerStorage[k]=v.storage;o.controllerRecord[k]=v.record;o.phase[k]=v.raw[0x14];o.nextPhase[k]=v.raw[0x15];o.nativeBinding[k]=s.engine.bound[k];
  o.currentVehicle[k]=vehicle_controller_read::Word(v.raw+0x44);o.candidate[k]=vehicle_controller_read::Word(v.raw+0x46);o.seat[k]=v.raw[0x17]&15u;
 }return o;
}
bool PoseVehicleProvider(NativePoseVehicleProtectionQuery query,const NativePoseVehicleOwnership& owner,
 NativePoseVehicleProtection& out) noexcept {
 out={};if(!g_pose_vehicle_bindings.provider||t_vehicle_provider_call)return false;
 g_pose_vehicle_reason.store("vehicle_installed_guard_provider",std::memory_order_release);
 const DWORD incoming=GetLastError();t_vehicle_provider_call=true;
 const bool accepted=g_pose_vehicle_bindings.provider(query,owner,out);
 t_vehicle_provider_call=false;SetLastError(incoming);
 return accepted&&vehicle_core::ProtectionShape(out,owner);
}
bool PoseVehicleQualify(const NativePoseVehicleProtection& p,const NativePoseVehicleOwnership& owner,
 vehicle_core::Snapshot& s) noexcept {
 auto reader=PoseReader;
 g_pose_vehicle_reason.store("vehicle_typed_resources_registry_ui_camera",std::memory_order_release);
 return vehicle_core::ProtectionShape(p,owner)&&vehicle_core::ReadResources(reader,g_base,owner.player,owner.data,p,s)&&
 vehicle_core::SameProtection(p,vehicle_core::ProtectionFrom(s,owner,p.installationSerial));
}
bool PoseVehicleReadCurrent(vehicle_core::Snapshot& out,NativePoseVehicleOwnership& owner,const PoseSharedLease* ticket,
 NativePoseVehicleProtectionQuery operation,bool allowPending=false) noexcept {
 out={};owner={};if(!g_pose_vehicle_bound||!g_pose_held||g_pose_owners.held!=holds::vehicle||
 !g_pose_owners.vehicleSerial||(!allowPending&&(g_pose_clear||g_pose_owners.pending)))return false;
 const NativePoseVehicleLease lease{g_pose_owners.vehicleSerial,g_pose_generation,g_pose_epoch};
 vehicle_core::Snapshot a{},b{};NativePoseVehicleProtection p{};
 if(!PoseVehicleReadBase(a,ticket))return false;
 const auto first=PoseVehicleOwner(a,lease,g_pose_vehicle_protection.vehicle,ticket);
 if(!PoseVehicleQualify(g_pose_vehicle_protection,first,a)||!vehicle_core::SameRetention(g_pose_vehicle_hold,a)||
 !PoseVehicleProvider(operation,first,p)||!vehicle_core::SameProtection(p,g_pose_vehicle_protection)||
 !PoseVehicleReadBase(b,ticket))return false;
 const auto last=PoseVehicleOwner(b,lease,g_pose_vehicle_protection.vehicle,ticket);
 if(!PoseVehicleQualify(p,last,b)||!vehicle_core::Coherent(a,b)||!vehicle_core::SameOwnership(first,last)||
 !vehicle_core::SameRetention(g_pose_vehicle_hold,b)||g_pose_owners.vehicleSerial!=lease.serial||
 g_pose_owners.held!=holds::vehicle||(!allowPending&&(g_pose_clear||g_pose_owners.pending))||g_pose_fault)return false;
 out=b;owner=last;g_pose_vehicle_reason.store("vehicle_current",std::memory_order_release);return true;
}
bool PoseVehiclePairCurrent() noexcept {
 if(!(g_pose_owners.held&holds::vehicle))return true;
 vehicle_core::Snapshot current{};NativePoseVehicleOwnership owner{};
 const auto op=g_pose_owners.pending?NativePoseVehicleProtectionQuery::retired:NativePoseVehicleProtectionQuery::cleanup;
 if(!PoseVehicleReadCurrent(current,owner,nullptr,op,true))return false;
 if(g_pose_owners.pending){auto reader=PoseReader;return vehicle_core::Retired(reader,current);}
 return true;
}
bool PoseVehicleRegistryRefuses(std::uintptr_t hook,std::uintptr_t object,std::uintptr_t owner=0) noexcept {
 if(!g_pose_vehicle_bound||g_pose_owners.held!=holds::vehicle)return false;
 const auto& p=g_pose_vehicle_protection;
 if(hook==0xbf4690)return object==p.queryProxy||owner==p.vehicleObject-0x20;
 if(hook==0xbf4a80)return object==p.queryProxy;
 if(hook==0xbf8a20||hook==0xbf8c10||hook==0xbf8f20)return object==p.registryProvider;
 if(hook==0xbf9060)return true;
 if(hook==0xbf91b0)return object==p.registryHolder;
 return false;
}
bool PoseVehicleRegistryClassCurrent(const PoseSharedLease& read) noexcept {
 vehicle_core::Snapshot current{};NativePoseVehicleOwnership owner{};
 return PoseVehicleReadCurrent(current,owner,&read,NativePoseVehicleProtectionQuery::cleanup)&&
  vehicle_core::RegistryNoWork(current.registryCycle);
}
void PoseVehiclePublishClear() noexcept {
 // Called only in the actual successful EndWritePublish callback, after
 // holds::Closed removed this distinct pending purpose. No early release.
 if(g_pose_owners.held&holds::vehicle)return;
 PoseVehicleQueuePublishClear();
 g_pose_vehicle_hold={};g_pose_vehicle_owner={};g_pose_vehicle_protection={};g_pose_vehicle_bound=false;
}
bool PoseVehiclePartsContext(NativePoseVehiclePartsContext& out) noexcept {
 out={};const auto* ticket=t_parts.read;
 if(!ticket||t_parts.go!=g_pose_go||t_parts.caller!=g_base+0x9c2a2a||
 !dual_parts::SharedDomains(t_pose.opened,PoseOwned(),t_local_carry.opened))return false;
 vehicle_core::Snapshot a{},b{};NativePoseVehicleOwnership first{},last{};
 if(!PoseVehicleReadCurrent(a,first,ticket,NativePoseVehicleProtectionQuery::parts))return false;
 native_pose_teardown_diagnostic::Model expected[2]{};
 for(unsigned k=0;k<2;++k){const auto& m=a.pair.body[k];expected[k]={a.pair.player,m.block,m.state,m.cs,m.entry,k};}
 native_pose_parts_diagnostic::Witness parts{},late{};local_profile::Profile profile{},afterProfile{};auto reader=PoseReader;
 if(!native_pose_parts_diagnostic::Pair(reader,g_base,g_pose_go,expected,parts)||!vehicle_core::NoPartsWork(parts)||parts.q!=a.pair.data||
 !local_profile::ReadPair(reader,g_base,a.pair,profile)||
 !PoseVehicleReadCurrent(b,last,ticket,NativePoseVehicleProtectionQuery::parts)||!vehicle_core::Coherent(a,b)||
 !vehicle_core::SameOwnership(first,last)||!local_profile::ReadPair(reader,g_base,b.pair,afterProfile)||!local_profile::Same(profile,afterProfile)||
 !native_pose_parts_diagnostic::Pair(reader,g_base,g_pose_go,expected,late)||!vehicle_core::NoPartsWork(late)||!native_pose_parts_diagnostic::Same(parts,late))return false;
 out.ownership=last;out.protection=g_pose_vehicle_protection;out.callerRva=0x9c2a2a;out.flags=parts.flags;
 out.dirty=(parts.mask180|parts.mask184)&3u;out.status[0]=parts.parts[0];out.status[1]=parts.parts[1];
 out.profileType=profile.type;out.profileParts=profile.parts;out.profileCamo=profile.camo;out.profileEncoded=profile.currentEncoded;
 out.profileAux=profile.currentAux;out.profileFace=profile.currentFace;out.profileSelector=profile.selector;out.profileRefresh=profile.refresh;
 return vehicle_core::PartsShape(out);
}
