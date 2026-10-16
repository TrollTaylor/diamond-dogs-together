// Included inside partner_input_hook's anonymous namespace after GuardedCall.
// Actual worker-only hooks and exclusion, not a gameplay activation policy.
namespace px=native_pose_exclusion;
namespace cw=camera_mode_window;
namespace birth=native_pose_birth;
namespace sx=native_pose_selected_export;
namespace pcam=native_pose_camera_reader;
namespace vfoot=native_pose_vehicle_camera_foot;
namespace vw=carry_vars_pair_word_window;
namespace msg=native_pose_message_lifetime;
namespace holds=native_pose_hold_owners;
namespace cp=native_pose_carry_participants;
namespace route=carry_dispatch_lifetime;
namespace groups=native_pose_hook_groups;
SRWLOCK g_pose_birth_lock=SRWLOCK_INIT;
birth::Receipt g_pose_birth{},g_pose_birth0{};std::uint64_t g_pose_birth_serial=0,g_pose_birth_epoch=1,g_pose_bound_birth=0;
px::Gate g_pose_gate;
std::atomic<bool> g_pose_installed{false},g_pose_original_view{false};bool g_pose_test=false;
volatile LONG g_pose_required=0,g_pose_fault=0,g_pose_held=0,g_pose_clear=0;
volatile LONG g_pose_windows=0,g_pose_suppressed=0,g_pose_hints=0,g_pose_local_exports=0;
bool g_pose_vehicle_foot_camera_test=false;
decltype(NativePoseVehicleProtectionBindings::readOriginal) g_pose_vehicle_foot_camera_reader=nullptr;
volatile LONG g_pose_cam_vehicle_foot_filtered=0;
volatile LONG g_pose_cam_owned=0,g_pose_cam_shared=0,g_pose_cam_caller=0,g_pose_cam_owner=0,g_pose_cam_body=-1,g_pose_cam_sender=0,g_pose_cam_filtered=0,g_pose_cam_refused=0;
std::uintptr_t g_pose_player=0,g_pose_go=0,g_pose_root=0,g_pose_block=0;
std::uint32_t g_pose_generation=0,g_pose_epoch=0,g_pose_thread=0,g_pose_pool=0;
std::uintptr_t g_pose_cs=0,g_pose_entry=0;
const char* g_pose_reason="off";
const char* g_pose_birth_reason="never";
volatile LONG g_birth_enter=0,g_birth_ready=0,g_birth_exact=0,g_birth_body1=0,g_birth_model=0,g_birth_done=0,g_birth_overlap=0;
volatile LONG g_birth_count=0,g_birth_base=0,g_birth_caller=0;
detail::NativePoseTestBindings g_pose_bindings{};
NativePoseWorkerBindings g_pose_worker{};
holds::Owners g_pose_owners{};cp::Pair g_pose_carry_pair{};
std::uint64_t g_pose_carry_serial=0,g_pose_carry_birth0=0;
namespace vehicle_core=native_pose_vehicle_policy;
vehicle_core::Snapshot g_pose_vehicle_hold{};
NativePoseVehicleOwnership g_pose_vehicle_owner{};
NativePoseVehicleProtection g_pose_vehicle_protection{};
NativePoseVehicleProtectionBindings g_pose_vehicle_bindings{};
std::uint64_t g_pose_vehicle_serial=0;bool g_pose_vehicle_bound=false;
std::atomic<const char*> g_pose_vehicle_reason{"vehicle_unbound"};
bool PoseVehiclePairCurrent() noexcept;
bool PoseVehiclePartsContext(NativePoseVehiclePartsContext&) noexcept;
void PoseVehiclePublishClear() noexcept;
route::Observation g_pose_route_hold{};bool g_pose_route_bound=false;
bool PoseRoutePatchesLive() noexcept;
msg::Observation g_pose_message_hold{};bool g_pose_message_bound=false;
struct LocalCarrySlice {px::WriteTicket ticket;NativePoseLocalCarryContext context{};bool opened=false;};
thread_local LocalCarrySlice t_local_carry;std::uint64_t g_local_carry_nonce=0;
bool PoseMessagePatchesLive() noexcept;
void PoseQueuePublishClear() noexcept;
void PoseDeferredPublishClear() noexcept;
std::int32_t* PoseCarryOwnedOp1(void*,std::int32_t*,std::uint32_t,void*,std::uintptr_t)noexcept;
bool PoseCarryOwnedOp1Reply(void*,std::uint32_t,void*,const void*,std::uintptr_t)noexcept;
bool PoseCarryOwnedRecipientNoop(void*,std::uint32_t,std::int32_t*,void*)noexcept;
bool PoseCarryOp2Active()noexcept;
std::int32_t* PoseCarrySharedOp2(void*,std::int32_t*,std::uint32_t,void*,std::uintptr_t,const PoseSharedLease&)noexcept;
bool PoseCarrySharedOp2Recipient(void*,std::uint32_t,std::int32_t*,void*,const PoseSharedLease&)noexcept;
bool PoseLocalCarryQuery(NativePoseLocalCarryContext&) noexcept;
vw::Shadow g_pose_pair_word_shadow{};
NativePoseJobContext g_pose_pair_word_closed{};bool g_pose_pair_word_closed_clean=false;
thread_local vw::Token t_pose_pair_word_token;
NativePosePartsPollValidator g_pose_parts_validator=nullptr;
std::atomic<NativePoseDualReadObserver> g_pose_dual_read_observer{nullptr};
struct PosePartsPoll {const PoseSharedLease* read=nullptr;std::uintptr_t go=0,caller=0;};
thread_local PosePartsPoll t_parts;
NativePoseBeforeFn g_pose_before=nullptr;NativePoseAfterFn g_pose_after=nullptr;NativePoseClosedFn g_pose_closed=nullptr;
struct PoseWindow {
 px::WriteTicket writer;cw::Token c0;NativePoseJobContext context{};
 bool opened=false,captured=false,job=false,cleanup=false;
};
thread_local PoseWindow t_pose;
struct PoseReaders {
 px::ReadTicket tickets[32];std::uint64_t serial[32]{};std::uint64_t next=0;
};
thread_local PoseReaders t_readers;
using PoseUpdateFn=void(*)(void*);
using PoseSelectedExportFn=void(*)(void*,std::uint32_t);
using PoseMessageUnregisterFn=void(*)(void*,void*);
using PoseMessageSetterFn=void(*)(void*,void*);
using PoseMessageDeleteMapFn=std::uint32_t(*)(void*,std::uint32_t);
using PoseMessageInsertMapFn=std::uint32_t(*)(void*,std::uint32_t,std::uint64_t);
using PoseVarsInitFn=void(*)();
using PoseVarsLuaFn=int(*)(void*);
using PoseVarsPairFn=bool(*)(void*,std::uint32_t);
using PoseVarsMessageFn=void(*)(void*,std::uint32_t,void*,const void*);
using PoseConstructFn=void*(*)(void*,void*,void*);
using PoseDeleteFn=void*(*)(void*,unsigned);
using PoseRebindFn=void(*)(void*,std::uint32_t);
using PoseRetireFn=void(*)(void*,std::uint32_t,std::uint8_t);
using PoseHintFn=void(*)(void*,std::uint16_t,std::uint32_t,std::uint8_t,std::uint8_t,std::uint8_t,std::uint8_t);
PoseSelectedExportFn g_pose_selected_export=nullptr;PoseUpdateFn g_pose_cam_frame=nullptr;
PoseVarsInitFn g_pose_vars_init=nullptr;PoseVarsLuaFn g_pose_vars_lua=nullptr,g_pose_vars_reset=nullptr,g_pose_vars_bulk_reset=nullptr;
PoseUpdateFn g_pose_vars_export=nullptr;PoseVarsPairFn g_pose_vars_pair=nullptr;PoseVarsMessageFn g_pose_vars_message=nullptr;
bool PoseVarsPatchesLive() noexcept;
std::uintptr_t g_pose_camera_stub=0;
PoseUpdateFn g_pose_update=nullptr,g_pose_destroy_block=nullptr,g_pose_destroy_entry=nullptr;
PoseDeleteFn g_pose_destroy_cs=nullptr;PoseHintFn g_pose_hint=nullptr;
using PoseMessageCtorFn=void*(*)(void*);
PoseMessageCtorFn g_pose_message_ctor=nullptr;PoseDeleteFn g_pose_message_dtor=nullptr;
PoseMessageUnregisterFn g_pose_message_unregister=nullptr;PoseMessageSetterFn g_pose_message_setter=nullptr;
PoseUpdateFn g_pose_message_dispatch=nullptr;PoseMessageDeleteMapFn g_pose_message_delete_map=nullptr;
PoseMessageInsertMapFn g_pose_message_insert_map=nullptr;PoseUpdateFn g_pose_message_arena_reset=nullptr;
PoseDeleteFn g_pose_delete_player=nullptr;PoseUpdateFn g_pose_reset_player=nullptr;
PoseRetireFn g_pose_retire_body=nullptr;
PoseConstructFn g_pose_construct=nullptr;
using PoseRouteCtorFn=void*(*)(void*,void*);
using PoseRouteRegistryCtorFn=void*(*)(void*);using PoseRouteReplaceFn=void(*)();
PoseRouteCtorFn g_pose_route_ctor=nullptr;PoseDeleteFn g_pose_route_delete=nullptr,g_pose_registry_delete=nullptr;
PoseRouteRegistryCtorFn g_pose_registry_ctor=nullptr;PoseUpdateFn g_pose_registry_destroy=nullptr;
PoseRouteReplaceFn g_pose_registry_replace=nullptr;PoseUpdateFn g_pose_route_class_update=nullptr,g_pose_route_class_register=nullptr;
PoseRebindFn g_pose_rebind=nullptr;PoseUpdateFn g_pose_parts=nullptr;
bool PoseRequired() noexcept {return InterlockedCompareExchange(&g_pose_required,0,0)!=0;}
void PoseFail(const char* why) noexcept {
 const bool first=InterlockedCompareExchange(&g_pose_fault,1,0)==0;
 if(first)g_pose_reason=why;g_pose_gate.StopNewWriters();if(!first)return;
 const DWORD incoming=GetLastError();
 if(g_log){char line[384]{};const auto thread=GetCurrentThreadId();
  if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
   "pose_fail;reason=%s;thread=%u;seq=%u;held=%u;clear=%u;opened=%u;job=%u;consumer=%u;generation=%u;epoch=%u;owned=%u\r\n",
   why,thread,static_cast<unsigned>(g_window_seq),g_pose_held?1u:0u,g_pose_clear?1u:0u,t_pose.opened?1u:0u,t_pose.job?1u:0u,
   static_cast<unsigned>(t_pose.context.consumer),g_pose_generation,g_pose_epoch,
   t_pose.opened&&g_pose_gate.Owns(thread,g_pose_generation,g_pose_epoch,t_pose.writer)?1u:0u)))g_log(line);
 }SetLastError(incoming);
 if(g_pose_bindings.enabled){if(g_pose_bindings.refuse)g_pose_bindings.refuse(why);}
 else if(g_pose_worker.refuse)g_pose_worker.refuse(why);
}
bool PoseWorker() noexcept {
 if(g_pose_bindings.enabled)return true;
#ifdef MGS_BACKGROUND_WORKER
 return g_pose_worker.admitted&&g_pose_worker.refuse&&g_pose_worker.admitted();
#else
 return false;
#endif
}
template<class T> bool PoseRead(std::uintptr_t p,T& value) noexcept {
 SIZE_T size=0;return p&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),&value,sizeof(value),&size)&&size==sizeof(value);
}
bool PoseReader(std::uintptr_t p,void* out,std::size_t n) noexcept {
 SIZE_T size=0;return p&&n&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,n,&size)&&size==n;
}
bool PoseOwned() noexcept {
 return t_pose.opened&&g_pose_gate.Owns(GetCurrentThreadId(),g_pose_generation,g_pose_epoch,t_pose.writer);
}
volatile LONG g_pose_nested_trace_logged=0;
void PoseTraceNestedGlobal(const char* entry,std::uintptr_t caller) noexcept {
 const DWORD incoming=GetLastError();
 if(PoseOwned()&&InterlockedCompareExchange(&g_pose_nested_trace_logged,1,0)==0&&g_log){
  char line[256]{};const auto rva=caller>=g_base&&caller-g_base<0xa01d000?caller-g_base:0;
  if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
   "pose_nested_global_trace;entry=%s;caller_rva=%llx;seq=%u;consumer=%u;selected=%u;job=%u;held=%u;fault=%u\r\n",
   entry,static_cast<unsigned long long>(rva),static_cast<unsigned>(g_window_seq),t_pose.context.consumer,
   t_pose.context.selectedBody,t_pose.job?1u:0u,g_pose_held?1u:0u,g_pose_fault?1u:0u)))g_log(line);
 }SetLastError(incoming);
}
bool PoseEnterShared(PoseSharedLease& out) noexcept {
 out={};if(!g_pose_installed)return true;const auto tid=GetCurrentThreadId();
 unsigned slot=32;for(unsigned i=0;i<32;++i)if(!t_readers.tickets[i].Armed()){slot=i;break;}
 if(slot==32){PoseFail("pose_reader_depth");return false;}
 const auto started=GetTickCount64();
 for(;;){
  const auto result=g_pose_gate.TryRead(tid,t_readers.tickets[slot]);
  if(result==px::Status::entered){
   ++t_readers.next;if(!t_readers.next)++t_readers.next;
   t_readers.serial[slot]=t_readers.next;out={true,tid,slot,t_readers.next};return true;
  }
  if(result!=px::Status::busy){PoseFail(result==px::Status::nested_writer?"pose_nested_global":"pose_reader_refused");return false;}
  const auto now=GetTickCount64();if(now<started||now-started>=250){PoseFail("pose_foreign_reader_timeout");return false;}
  Sleep(0); // private bounded wait; never forward while writer remains active
 }
}
void PoseExitShared(PoseSharedLease& out) noexcept {
 if(!out.tracked)return;
 if(out.thread!=GetCurrentThreadId()||out.slot>=32||t_readers.serial[out.slot]!=out.serial||
    !g_pose_gate.EndRead(out.thread,t_readers.tickets[out.slot])){PoseFail("pose_reader_cleanup");return;}
 out.tracked=false;
}
bool PoseRootRequired(void* root) noexcept {
 const auto at=reinterpret_cast<std::uintptr_t>(root);
 return PoseRequired()&&(at==g_pose_root||at==static_cast<std::uintptr_t>(g_wrap_root1));
}
bool PoseJobRequired(void* go,std::uint32_t block) noexcept {
 if(!PoseRequired())return false;
 if(g_pose_fault)return true; // private failure cannot resume another unwrapped player job
 if(block!=g_pose_pool)return false;
 if(reinterpret_cast<std::uintptr_t>(go)!=g_pose_go)PoseFail("pose_job_owner_changed");
 return true;
}
cw::Scope PoseScope() noexcept {
 cw::Scope s{};s.exactGate=g_pose_installed;s.ownedBody1=PoseRequired();
 s.nativeSandbox=g_pose_test;s.serialized=PoseOwned();s.batch=partner_defer::InBatch();
 s.stopping=g_shutdown!=0;s.faulted=g_pose_fault!=0;s.image=g_base;s.player=g_pose_player;
 s.thread=GetCurrentThreadId();s.ownerThread=g_pose_thread;s.generation=g_pose_generation;s.sceneEpoch=g_pose_epoch;
 s.windowSequence=static_cast<std::uint32_t>(g_window_seq);
 s.presentationSetter=&coop_carry::ReadPresentationSetter;return s;
}
bool PoseModel(const pi::Arm& a,NativePoseJobContext& c0,const char** failure=nullptr,unsigned actualId=1) noexcept {
 const auto stage=[&](const char* why) noexcept {if(failure)*failure=why;};
 std::uintptr_t cs=0,csAfter=0,psCs=0,entries=0,entriesAfter=0,vt=0,psVt=0,owner=0,psBlock=0,blockPS=0,blockVT=0,rebind=0;
 std::uint32_t count=0,base=0,countAfter=0,baseAfter=0;
 stage("birth_model_ps_type");
 if(!a.ps1||!PoseRead(a.ps1,psVt)||(psVt!=g_base+c::ps_vtable_rva&&psVt!=g_base+c::ps_vtable_alt_rva))return false;
 stage("birth_model_block_owner");
 // Exact SetStance FF5B51 reads PS408->block, block138->P. PS138 has
 // no Player-owner contract and is deliberately not read or constrained.
 if(!PoseRead(a.ps1+c::ps_blk,psBlock)||psBlock!=a.blk1||!PoseRead(a.blk1+0x138,owner)||owner!=a.P||
    !PoseRead(a.blk1,blockVT)||blockVT!=g_base+c::block_vtable_rva||
    !PoseRead(blockVT+0xf8,rebind)||rebind!=g_base+0x9cc850||
    !PoseRead(a.blk1+0x78,blockPS)||blockPS!=a.ps1)return false;
 stage("birth_model_cs_owner");
 if(!PoseRead(a.blk1+0x10,cs)||!cs||!PoseRead(a.ps1+0x238,psCs)||psCs!=cs||
    !PoseRead(cs,vt)||vt!=g_base+0x22e6070||!PoseRead(cs+8,count)||!count||count>4||
    !PoseRead(cs+0xc,base)||base>actualId||actualId>1||actualId-base>=count||!PoseRead(cs+0x10,entries)||!entries||(entries&7u))return false;
 stage("birth_model_pair");
 if(!PoseRead(a.blk1+0x10,csAfter)||csAfter!=cs||!PoseRead(cs+8,countAfter)||countAfter!=count||
    !PoseRead(cs+0xc,baseAfter)||baseAfter!=base||!PoseRead(cs+0x10,entriesAfter)||entriesAfter!=entries||
    !PoseRead(a.ps1,psVt)||(psVt!=g_base+c::ps_vtable_rva&&psVt!=g_base+c::ps_vtable_alt_rva)||
    !PoseRead(a.ps1+c::ps_blk,psBlock)||psBlock!=a.blk1||!PoseRead(a.blk1+0x138,owner)||owner!=a.P||
    !PoseRead(a.blk1,blockVT)||blockVT!=g_base+c::block_vtable_rva||
    !PoseRead(a.blk1+0x78,blockPS)||blockPS!=a.ps1||!PoseRead(a.ps1+0x238,psCs)||psCs!=cs||
    !PoseRead(cs,vt)||vt!=g_base+0x22e6070)return false;
 c0.modelCS=cs;c0.modelEntry=entries+std::uintptr_t(actualId-base)*0xc0;c0.modelReady=true;stage("birth_model_ready");return true;
}
bool PoseParticipantsLive() noexcept;
std::uint64_t PoseBirthEpoch() noexcept {
 AcquireSRWLockShared(&g_pose_birth_lock);const auto epoch=g_pose_birth_epoch;ReleaseSRWLockShared(&g_pose_birth_lock);return epoch;
}
#include "native_pose_activation_impl.inc"
bool PoseRecordBirth(const birth::Completion& c,unsigned actualId=1) noexcept {
 AcquireSRWLockExclusive(&g_pose_birth_lock);++g_pose_birth_serial;if(!g_pose_birth_serial)++g_pose_birth_serial;
 const auto receipt=birth::Complete(c,g_pose_birth_serial,g_pose_birth_epoch);if(receipt.ready&&actualId<=1)(actualId?g_pose_birth:g_pose_birth0)=receipt;
 ReleaseSRWLockExclusive(&g_pose_birth_lock);return receipt.ready;
}
void PoseInvalidateBirth(std::uintptr_t at,unsigned kind) noexcept {
 AcquireSRWLockExclusive(&g_pose_birth_lock);++g_pose_birth_epoch;if(!g_pose_birth_epoch)++g_pose_birth_epoch;
 if(birth::InvalidatedBy(g_pose_birth,at,kind))g_pose_birth={};
 if(birth::InvalidatedBy(g_pose_birth0,at,kind))g_pose_birth0={};
 PoseInvalidateActivationLocked(at,kind);
 ReleaseSRWLockExclusive(&g_pose_birth_lock);
}
bool PoseBirthCurrent(const pi::Arm& a,const NativePoseJobContext& c0,bool bind=false) noexcept {
 AcquireSRWLockShared(&g_pose_birth_lock);const auto saved=PoseObservedBirthLocked(1);ReleaseSRWLockShared(&g_pose_birth_lock);
 const bool current=birth::Current(saved,{a.P,a.blk1,a.ps1,c0.modelCS,c0.modelEntry});
 if(current&&bind)g_pose_bound_birth=saved.serial;
 return current&&(bind||saved.serial==g_pose_bound_birth);
}
bool PoseContextBirthCurrent(const NativePoseJobContext& c0) noexcept {
 pi::Arm a{};a.P=c0.player;a.blk1=c0.block;a.ps1=c0.state;return PoseBirthCurrent(a,c0);
}
namespace parts_policy=sleep_parts_policy;
bool PosePartsContext(NativePoseSleepPartsContext& out) noexcept {
 out={};const auto* ticket=t_parts.read;const auto tid=GetCurrentThreadId();const auto seq=static_cast<std::uint32_t>(g_window_seq);
 if(g_pose_bindings.enabled||!ticket||!ticket->tracked||ticket->thread!=tid||ticket->slot>=32||!ticket->serial||
 t_readers.serial[ticket->slot]!=ticket->serial||!g_pose_gate.OwnsRead(tid,t_readers.tickets[ticket->slot])||
 !PoseWorker()||!PoseRequired()||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
 g_pose_fault||g_shutdown||g_pose_clear||g_pose_owners.pending||g_pose_owners.held!=holds::sleep||!g_pose_held||
 t_pose.opened||t_pose.job||(seq&1u)||!seq||partner_defer::InBatch()||tid!=g_pose_thread||
 t_parts.go!=g_pose_go||t_parts.caller!=g_base+0x9c2a2a||!PoseParticipantsLive()||
 !NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive())return false;
 pi::Arm arm{};if(!ReadArm(arm)||!arm.enabled||!arm.native||arm.holdlog||arm.cam!=PartnerInputCam::sandbox||
 arm.gen!=g_pose_generation||arm.rates_epoch!=g_pose_epoch||arm.P!=g_pose_player||arm.go!=g_pose_go||
 arm.root1!=g_pose_root||arm.blk1!=g_pose_block)return false;
 NativePoseJobContext a{},b{};if(!PoseModel(arm,a)||!PoseModel(arm,b)||a.modelCS!=b.modelCS||a.modelEntry!=b.modelEntry||
 a.modelCS!=g_pose_cs||a.modelEntry!=g_pose_entry||!PoseBirthCurrent(arm,b))return false;
 native_pose_teardown_diagnostic::Model expected[2]{};expected[1]={arm.P,arm.blk1,arm.ps1,b.modelCS,b.modelEntry,1};
 native_pose_parts_diagnostic::Witness w{};auto reader=PoseReader;
 if(!native_pose_parts_diagnostic::Pair(reader,g_base,g_pose_go,expected,w)||!parts_policy::NoRemoteWork(w))return false;
 std::uintptr_t q=0,state=0,sync=1;
 if(!PoseRead(arm.P+0x60,q)||q!=w.q||!PoseRead(q+0x1e8,state)||!state||!PoseRead(arm.P+0xa8,sync)||sync)return false;
 pi::Arm after{};if(!ReadArm(after)||after.gen!=arm.gen||after.rates_epoch!=arm.rates_epoch||after.P!=arm.P||
 after.go!=arm.go||after.blk1!=arm.blk1||after.ps1!=arm.ps1||!after.enabled||!after.native||
 !PoseBirthCurrent(after,b)||static_cast<std::uint32_t>(g_window_seq)!=seq||partner_defer::InBatch()||
 !g_pose_gate.OwnsRead(tid,t_readers.tickets[ticket->slot])||g_pose_fault||g_pose_clear||g_pose_owners.pending||
 g_pose_owners.held!=holds::sleep)return false;
 out={g_base,arm.P,arm.go,q,state,arm.blk1,arm.ps1,b.modelCS,b.modelEntry,arm.gen,arm.rates_epoch,tid,seq,0x9c2a2a,
 g_pose_bound_birth,ticket->serial,w.flags,(w.mask180|w.mask184)&2u,w.parts[1],w.pool[1]};
 return parts_policy::ContextValid(out);
}
// Composition requires the installed metadata/provider lifetime stages; these
// current private routines verify actual owned patches and typed held routes.
bool PoseRouteCurrent() noexcept;
bool PoseMessageCurrent() noexcept;
namespace dual_parts=dual_parts_core_policy;
namespace local_profile=dual_parts_local_profile;
thread_local const char* t_dual_parts_reason="unavailable";
volatile LONG g_dual_parts_refused=0;
bool PoseDualPartsContext(NativePoseDualPartsContext& out) noexcept {
 out={};t_dual_parts_reason="dual_shared_scope";const auto* ticket=t_parts.read;const auto tid=GetCurrentThreadId();const auto seq=static_cast<std::uint32_t>(g_window_seq);
 if(g_pose_bindings.enabled||!ticket||!ticket->tracked||ticket->thread!=tid||ticket->slot>=32||!ticket->serial||
 t_readers.serial[ticket->slot]!=ticket->serial||!g_pose_gate.OwnsRead(tid,t_readers.tickets[ticket->slot])||
 !PoseWorker()||!PoseRequired()||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
 g_pose_fault||g_shutdown||g_pose_clear||g_pose_owners.pending||g_pose_owners.held!=(holds::sleep|holds::carry)||!g_pose_held||
 !g_pose_owners.carrySerial||!g_pose_carry_birth0||!g_pose_bound_birth||
 !dual_parts::SharedDomains(t_pose.opened,PoseOwned(),t_local_carry.opened)||(seq&1u)||!seq||
 partner_defer::InBatch()||tid!=g_pose_thread||t_parts.go!=g_pose_go||t_parts.caller!=g_base+0x9c2a2a||
 !PoseParticipantsLive()||!NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||
 !NativePoseDispatchInterfaceLifetimeLive()||!PoseRouteCurrent()||!PoseMessageCurrent())return false;
 t_dual_parts_reason="dual_current_arm";
 const auto serial=g_pose_owners.carrySerial;const auto born0=g_pose_carry_birth0,born1=g_pose_bound_birth;
 pi::Arm arm{};if(!ReadArm(arm)||!arm.enabled||!arm.native||arm.holdlog||arm.cam!=PartnerInputCam::sandbox||
 arm.gen!=g_pose_generation||arm.rates_epoch!=g_pose_epoch||arm.P!=g_pose_player||arm.go!=g_pose_go||
 arm.root1!=g_pose_root||arm.blk1!=g_pose_block)return false;
 t_dual_parts_reason="dual_both_models";auto reader=PoseReader;cp::Pair pair{},afterPair{};
 if(!cp::ReadPair(reader,g_base,arm.P,pair)||!cp::Same(pair,g_pose_carry_pair)||pair.go!=g_pose_go||
 pair.body[1].block!=arm.blk1||pair.body[1].state!=arm.ps1||pair.body[1].cs!=g_pose_cs||pair.body[1].entry!=g_pose_entry)return false;
 birth::Receipt birth0{},birth1{};
 AcquireSRWLockShared(&g_pose_birth_lock);birth0=PoseObservedBirthLocked(0);birth1=PoseObservedBirthLocked(1);ReleaseSRWLockShared(&g_pose_birth_lock);
 const auto birthsMatch=[&](const cp::Pair& value) noexcept {return dual_parts::BirthsCurrent(value,birth0,birth1,born0,born1);};
 t_dual_parts_reason="dual_both_births";if(!birthsMatch(pair))return false;
 native_pose_teardown_diagnostic::Model expected[2]{};
 for(unsigned k=0;k<2;++k)expected[k]={pair.player,pair.body[k].block,pair.body[k].state,pair.body[k].cs,pair.body[k].entry,k};
 native_pose_parts_diagnostic::Witness w{},late{};
 local_profile::Profile profile{},afterProfile{};
 t_dual_parts_reason="dual_current_parts";
 if(!native_pose_parts_diagnostic::Pair(reader,g_base,g_pose_go,expected,w)||!dual_parts::NoWork(w)||w.q!=pair.data)return false;
 t_dual_parts_reason="dual_local_profile";if(!local_profile::ReadPair(reader,g_base,pair,profile,&t_dual_parts_reason))return false;
 // Reacquire actual current identities; never reuse the exclusive body1 or
 // selected0 carrier writer Scope as a fabricated shared receipt.
 t_dual_parts_reason="dual_late_pair";pi::Arm fresh{};birth::Receipt fresh0{},fresh1{};
 if(!ReadArm(fresh)||!fresh.enabled||!fresh.native||fresh.holdlog||fresh.cam!=PartnerInputCam::sandbox||
 fresh.gen!=arm.gen||fresh.rates_epoch!=arm.rates_epoch||fresh.P!=arm.P||fresh.go!=arm.go||fresh.root1!=arm.root1||fresh.blk1!=arm.blk1||fresh.ps1!=arm.ps1||
 !cp::ReadPair(reader,g_base,fresh.P,afterPair)||!cp::Same(pair,afterPair)||
 !cp::CoherentCallbacks(pair.body[0],afterPair.body[0])||!cp::CoherentCallbacks(pair.body[1],afterPair.body[1])||
 !local_profile::ReadPair(reader,g_base,afterPair,afterProfile)||!local_profile::Same(profile,afterProfile)||
 !native_pose_parts_diagnostic::Pair(reader,g_base,g_pose_go,expected,late)||!dual_parts::NoWork(late)||!native_pose_parts_diagnostic::Same(w,late))return false;
 t_dual_parts_reason="dual_late_scope";AcquireSRWLockShared(&g_pose_birth_lock);fresh0=PoseObservedBirthLocked(0);fresh1=PoseObservedBirthLocked(1);ReleaseSRWLockShared(&g_pose_birth_lock);
 if(fresh0.serial!=born0||fresh1.serial!=born1||!birth::Current(fresh0,birth0.identity)||!birth::Current(fresh1,birth1.identity)||
 !dual_parts::BirthsCurrent(afterPair,fresh0,fresh1,born0,born1)||static_cast<std::uint32_t>(g_window_seq)!=seq||partner_defer::InBatch()||
 !g_pose_gate.OwnsRead(tid,t_readers.tickets[ticket->slot])||t_readers.serial[ticket->slot]!=ticket->serial||
 g_pose_fault||g_shutdown||g_pose_clear||g_pose_owners.pending||g_pose_owners.held!=(holds::sleep|holds::carry)||
 g_pose_owners.carrySerial!=serial||g_pose_carry_birth0!=born0||g_pose_bound_birth!=born1||
 !PoseParticipantsLive()||!NativePoseDispatchInterfaceLifetimeLive()||!PoseRouteCurrent()||!PoseMessageCurrent())return false;
 NativePoseDualPartsContext current{};current.image=g_base;current.player=pair.player;current.go=pair.go;current.data=pair.data;
 current.state=pair.scriptState;current.vars=pair.vars;current.generation=arm.gen;current.sceneEpoch=arm.rates_epoch;current.thread=tid;
 current.sequence=seq;current.callerRva=0x9c2a2a;current.purposes=g_pose_owners.held;current.pending=g_pose_owners.pending;
 current.birthSerial[0]=born0;current.birthSerial[1]=born1;current.sharedSerial=ticket->serial;current.carrySerial=serial;
 current.flags=w.flags;current.dirty=(w.mask180|w.mask184)&3u;current.local0ProfileCurrent=true;
 current.profileType=profile.type;current.profileParts=profile.parts;current.profileCamo=profile.camo;current.profileEncoded=profile.currentEncoded;
 current.profileAux=profile.currentAux;current.profileFace=profile.currentFace;current.profileSelector=profile.selector;current.profileRefresh=profile.refresh;
 for(unsigned k=0;k<2;++k){current.block[k]=pair.body[k].block;current.ps[k]=pair.body[k].state;current.modelCS[k]=pair.body[k].cs;
  current.modelEntry[k]=pair.body[k].entry;current.pool[k]=w.pool[k];current.status[k]=w.parts[k];}
 t_dual_parts_reason="dual_reply_shape";if(!dual_parts::ContextValid(current))return false;out=current;t_dual_parts_reason="dual_ready";return true;
}
void PoseDualPartsRefusalLog() noexcept {
 const DWORD incoming=GetLastError();
 if(InterlockedCompareExchange(&g_dual_parts_refused,1,0)==0&&g_log){char line[256]{};
  if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
   "pose_dual_parts=refused;stage=%s;purposes=%u;pending=%u;sequence=%u;held=%u;fault=%u;thread=%u\r\n",
   t_dual_parts_reason,static_cast<unsigned>(g_pose_owners.held),static_cast<unsigned>(g_pose_owners.pending),
   static_cast<unsigned>(g_window_seq),g_pose_held?1u:0u,g_pose_fault?1u:0u,GetCurrentThreadId())))g_log(line);
 }SetLastError(incoming);
}
#include "native_pose_vehicle_impl.inc"
namespace reject=native_pose_reject_diagnostic;
volatile LONG g_pose_reject_logged=0;
void PoseRejectDiagnostic(reject::Stage stage,const char* detail,const pi::Arm* arm,
 std::uint32_t consumer=UINT32_MAX,std::uint32_t skip=UINT32_MAX) noexcept {
 const DWORD incoming=GetLastError();
 if(!PoseRequired()||!g_log){SetLastError(incoming);return;}
 const auto old=static_cast<std::uint32_t>(InterlockedOr(&g_pose_reject_logged,static_cast<LONG>(reject::Bit(stage))));
 if(!reject::First(old,stage)){SetLastError(incoming);return;}
 reject::Snapshot snapshot{};auto reader=PoseReader;
 const bool paired=reject::Pair(reader,g_base,g_pose_player,snapshot);
 const bool owner=arm&&arm->P==g_pose_player&&arm->go==g_pose_go&&arm->root1==g_pose_root&&arm->blk1==g_pose_block;
 char line[512]{};
 if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
  "pose_reject;stage=%s;detail=%s;consumer=%u;skip=%u;paired=%u;c0=%u;flags=%x;count=%u;realized=%u;real=%u;selected=%u;active=%u;request=%u;seq=%u;held=%u;fault=%u;armowner=%u;armgen=%u;posegen=%u;enabled=%u;native=%u;ready=%u;inputfault=%u;inj=%u;batch=%u\r\n",
  reject::Name(stage),detail?detail:"unknown",consumer,skip,paired?1u:0u,snapshot.c0?1u:0u,
  snapshot.flags,snapshot.count,snapshot.realized,snapshot.real,snapshot.selected,snapshot.active,snapshot.requested,
  static_cast<unsigned>(g_window_seq),g_pose_held?1u:0u,g_pose_fault?1u:0u,owner?1u:0u,
  arm?arm->gen:0u,g_pose_generation,arm&&arm->enabled?1u:0u,arm&&arm->native?1u:0u,
  g_ready?1u:0u,g_faulted?1u:0u,g_inj_valid?1u:0u,partner_defer::InBatch()?1u:0u)))g_log(line);
 SetLastError(incoming);
}
bool PoseBegin(const pi::Arm* a) noexcept {
 if(!PoseRequired()){InterlockedIncrement(&g_window_seq);return true;}
 if(!a||t_pose.opened||g_pose_fault||g_shutdown||!PoseWorker()||GetCurrentThreadId()!=g_pose_thread||
    a->P!=g_pose_player||a->go!=g_pose_go||a->root1!=g_pose_root||a->blk1!=g_pose_block||
    !a->gen||a->rates_epoch!=g_pose_epoch||!a->enabled||!a->native||
    a->cam!=PartnerInputCam::sandbox||a->holdlog||partner_defer::InBatch()||(g_window_seq&1)){
  PoseRejectDiagnostic(reject::Stage::admission,"pose_window_admission",a);
  InterlockedIncrement(&g_pose_suppressed);g_pose_reason="pose_window_admission";return false;
 }
 if(!PoseParticipantsLive()){PoseFail("pose_participant_patch_drift");return false;}
 if(!g_pose_bindings.enabled&&!NativePoseWholePlayerLifetimeLive()){PoseFail("pose_whole_player_lifetime_missing");return false;}
 px::Admission admission{true,true,true,true,true,true,g_pose_generation,g_pose_epoch,g_pose_thread};
 if(g_pose_gate.TryWrite(admission,t_pose.writer)!=px::Status::entered){
  PoseRejectDiagnostic(reject::Stage::busy,"pose_window_busy",a);
  InterlockedIncrement(&g_pose_suppressed);g_pose_reason="pose_window_busy";return false;
 }
 if(a->gen!=g_pose_generation){
  // Only inactive experimental presentation may adopt input/config generation.
  // The same typed constructor birth and scene are proved under the old writer.
  // No input window or live pose lease exists; no game field is changed.
  NativePoseJobContext x{},y{};const auto oldGeneration=g_pose_generation;
  const bool healthy=!g_pose_held&&!g_pose_clear&&PoseModel(*a,x)&&PoseModel(*a,y)&&
      x.modelCS==y.modelCS&&x.modelEntry==y.modelEntry&&PoseBirthCurrent(*a,y);
  if(!healthy){
   PoseFail("pose_generation_live_or_changed");
   if(!g_pose_gate.EndWrite(g_pose_thread,oldGeneration,g_pose_epoch,true,true,t_pose.writer))PoseFail("pose_generation_refusal_cleanup");
   return false;
  }
  g_pose_generation=a->gen;MemoryBarrier();
  if(!g_pose_gate.EndWrite(g_pose_thread,oldGeneration,g_pose_epoch,true,true,t_pose.writer)){PoseFail("pose_generation_cleanup");return false;}
  admission.generation=g_pose_generation;
  if(g_pose_gate.TryWrite(admission,t_pose.writer)!=px::Status::entered){
   InterlockedIncrement(&g_pose_suppressed);g_pose_reason="pose_generation_reacquire_busy";return false;
  }
  // Post-reacquire typed birth/model is checked below before the first Pre.
 }
 InterlockedIncrement(&g_window_seq);t_pose.opened=true;t_pose.captured=false;t_pose.job=false;t_pose.cleanup=false;
 t_pose.context={a->P,a->go,a->blk1,a->ps1,0,0,a->gen,a->rates_epoch,g_pose_thread,static_cast<std::uint32_t>(g_window_seq)};
 PoseModel(*a,t_pose.context);
 if(!t_pose.context.modelReady||!PoseBirthCurrent(*a,t_pose.context)){
  PoseFail("pose_actor_birth_changed");InterlockedIncrement(&g_window_seq);t_pose.opened=false;
  if(!g_pose_gate.EndWrite(g_pose_thread,g_pose_generation,g_pose_epoch,true,true,t_pose.writer))PoseFail("pose_birth_refusal_cleanup");
  return false;
 }
 if(g_pose_held&&(!t_pose.context.modelReady||t_pose.context.modelCS!=g_pose_cs||t_pose.context.modelEntry!=g_pose_entry)){
  PoseFail("pose_held_model_changed");InterlockedIncrement(&g_window_seq);t_pose.opened=false;
  if(!g_pose_gate.EndWrite(g_pose_thread,g_pose_generation,g_pose_epoch,true,true,t_pose.writer))PoseFail("pose_model_refusal_cleanup");
  return false;
 }
 auto read=PoseReader;auto sample=[]() noexcept {return PoseScope();};cw::Reply result{};
 if(!cw::Capture(read,sample,t_pose.c0,result)){
  PoseRejectDiagnostic(reject::Stage::capture,result.reason,a);
  g_pose_reason=result.reason;InterlockedIncrement(&g_pose_suppressed);
  InterlockedIncrement(&g_window_seq);t_pose.opened=false;
  if(!g_pose_gate.EndWrite(g_pose_thread,g_pose_generation,g_pose_epoch,true,true,t_pose.writer))PoseFail("pose_capture_cleanup");
  return false;
 }
 t_pose.captured=true;g_pose_reason="pose_window_captured";return true;
}
bool PosePairWordCas(std::uintptr_t at,std::uint16_t expected,std::uint16_t desired,std::uint16_t& observed) noexcept {
 observed=0;if(!PoseOwned()||!t_pose.job||at!=(g_base+0x2c01360+0x10e)||(at&1u))return false;
 __try {observed=static_cast<std::uint16_t>(_InterlockedCompareExchange16(reinterpret_cast<volatile short*>(at),static_cast<short>(desired),static_cast<short>(expected)));return observed==expected;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool PosePairWordRestore() noexcept {
 if(g_pose_bindings.enabled||!t_pose_pair_word_token.active())return true;
 auto read=PoseReader;auto cas=PosePairWordCas;
 return vw::After(read,cas,t_pose.context,g_base,g_pose_pair_word_shadow,t_pose_pair_word_token);
}
bool PosePairWordBefore() noexcept {
 if(g_pose_bindings.enabled)return true;
 if(t_pose_pair_word_token.active()){PoseFail("pose_pair_word_old_token");return false;}
 // Input-generation adoption has already been proved by PoseBegin under the
 // same actor/model birth and actual writer. Preserve the remote word while
 // updating only mod bookkeeping after the preceding successful Closed.
 if(g_pose_pair_word_shadow.bound&&g_pose_pair_word_shadow.owner.generation!=t_pose.context.generation){
  auto old=g_pose_pair_word_shadow.owner;old.generation=t_pose.context.generation;
  vw::Owner current{};auto read=PoseReader;
  const bool adopt=!g_pose_held&&g_pose_pair_word_closed_clean&&
   g_pose_pair_word_closed.birthSerial==t_pose.context.birthSerial&&
   g_pose_pair_word_closed.sequence==g_pose_pair_word_shadow.owner.sequence+1u&&
   vw::ReadOwner(read,t_pose.context,g_base,current)&&vw::SameLease(old,current);
  if(!adopt){PoseFail("pose_pair_word_generation");return false;}
  g_pose_pair_word_shadow.owner.generation=t_pose.context.generation;
 }
 if(!vw::Renew(t_pose_pair_word_token)){PoseFail("pose_pair_word_renew");return false;}
 auto read=PoseReader;auto cas=PosePairWordCas;
 if(!vw::Before(read,cas,t_pose.context,g_base,g_pose_pair_word_shadow,t_pose_pair_word_token)){PoseFail("pose_pair_word_capture");return false;}
 return true;
}
void PoseEnd(bool postComplete) noexcept {
 native_work_timing::Scope measured(native_work_timing::Kind::pose_end);
 if(!t_pose.opened){InterlockedIncrement(&g_window_seq);return;}
 const bool pairRestored=PosePairWordRestore();if(!pairRestored)PoseFail("pose_pair_word_cleanup");
 bool restored=false;cw::Reply receipt{};
 auto read=PoseReader;auto sample=[]() noexcept {return PoseScope();};
 auto write=[](std::uintptr_t at,std::uint8_t before,std::uint8_t after) noexcept {
  if(!PoseOwned())return cw::WriteStatus::fault;
  __try {return static_cast<std::uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(at),
      static_cast<char>(after),static_cast<char>(before)))==before?cw::WriteStatus::written:cw::WriteStatus::conflict;}
  __except(EXCEPTION_EXECUTE_HANDLER){return cw::WriteStatus::fault;}
 };
 if(t_pose.captured)restored=cw::Restore(read,write,sample,t_pose.c0,receipt);
 const bool clean=postComplete&&restored&&pairRestored&&!g_pose_fault;
 InterlockedIncrement(&g_window_seq);const auto context=t_pose.context;t_pose.opened=false;
 const bool released=g_pose_gate.EndWritePublish(g_pose_thread,g_pose_generation,g_pose_epoch,clean,true,t_pose.writer,[]()noexcept{
  if(g_pose_clear){
   const bool hadVehicle=(g_pose_owners.held&holds::vehicle)!=0;
   const bool carryClosed=holds::Closed(g_pose_owners,true);
   const bool vehicleClosed=hadVehicle&&!(g_pose_owners.held&holds::vehicle);
   InterlockedExchange(&g_pose_held,g_pose_owners.held?1:0);InterlockedExchange(&g_pose_clear,0);
   if(!g_pose_owners.held)g_pose_cs=g_pose_entry=0;
   if(vehicleClosed)PoseVehiclePublishClear();
   if(carryClosed){PoseQueuePublishClear();g_pose_carry_pair={};g_pose_carry_birth0=0;g_pose_message_hold={};g_pose_message_bound=false;g_pose_route_hold={};g_pose_route_bound=false;}
  }
 });
 t_pose.cleanup=clean&&released;
 g_pose_pair_word_closed=context;g_pose_pair_word_closed.sequence=static_cast<std::uint32_t>(g_window_seq);g_pose_pair_word_closed_clean=t_pose.cleanup;
 if(!t_pose.cleanup)PoseFail(restored?"pose_post_cleanup_failed":receipt.reason);
 if(t_pose.cleanup)InterlockedIncrement(&g_pose_windows);
 if(t_pose.job&&g_pose_closed){auto closed=context;closed.sequence=static_cast<std::uint32_t>(g_window_seq);g_pose_closed(closed,t_pose.cleanup);}
}
void PoseDispatchOriginal(void* root,std::uint32_t phase) noexcept {
 if(PoseRequired()&&g_pose_fault)return;
 if(PoseRootRequired(root)&&!PoseOwned()){InterlockedIncrement(&g_pose_suppressed);g_pose_reason="pose_dispatch_outside_window";return;}
 if(PoseOwned()){
  if(!PoseRootRequired(root)){PoseFail("pose_nested_body0_dispatch");return;}
  g_original(root,phase);return;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read))return;
 __try {g_original(root,phase);}__finally {PoseExitShared(read);}
}
bool PoseAllowCall(const Call& k) noexcept {
 if(!PoseRequired())return true;
 if(g_pose_fault)return false;
 const bool body1=k.entry==kJobEntry?reinterpret_cast<std::uintptr_t>(k.ctx)==g_pose_go:PoseRootRequired(k.root);
 if(body1&&!PoseOwned()){InterlockedIncrement(&g_pose_suppressed);g_pose_reason="pose_callback_outside_window";return false;}
 if(PoseOwned()&&!body1){PoseFail("pose_nested_other_callback");return false;}return true;
}
bool PoseCarryPairCurrent() noexcept;
bool PoseBefore(const pi::Arm& a,const Save& save,const pi::Injection& j,pi::Consumer consumer) noexcept {
 if(!PoseOwned())return true;
 // This receipt is sampled after successful Pre and every weapon/rates helper.
 // selected1 is authorized only if this exact Save owned the original P218.
 t_pose.context.selectedBodyOwned=save.p218_saved;
 std::uint32_t selectedAfter=0;std::uintptr_t actualPP=0,actualSource=0,vtAfter=0;
 if(!PoseRead(a.P+c::p_local_id,t_pose.context.selectedBody)||!PoseRead(a.P+c::p_pad,actualPP)||actualPP!=a.pp||
    !PoseRead(a.pp+c::pp_src,actualSource)||actualSource!=a.src||!PoseRead(a.pp,t_pose.context.windowPPVT)||
    t_pose.context.windowPPVT!=WindowVtable(a.hold)||!PoseRead(a.P+c::p_local_id,selectedAfter)||
    selectedAfter!=t_pose.context.selectedBody||!PoseRead(a.pp,vtAfter)||vtAfter!=t_pose.context.windowPPVT){
  PoseFail("pose_pad_ownership_drift");return false;
 }
 t_pose.context.pp=a.pp;t_pose.context.source=a.src;t_pose.context.birthSerial=g_pose_bound_birth;
 if(!save.p218_saved||t_pose.context.selectedBody!=1){PoseFail("pose_selected_body_drift");return false;}
 t_pose.context.modelReady=false;t_pose.context.modelCS=t_pose.context.modelEntry=0;
 PoseModel(a,t_pose.context);
 if(!t_pose.context.modelReady||!PoseBirthCurrent(a,t_pose.context)){PoseFail("pose_actor_birth_changed");return false;}
 if(g_pose_held&&(!t_pose.context.modelReady||t_pose.context.modelCS!=g_pose_cs||t_pose.context.modelEntry!=g_pose_entry)){
  PoseFail("pose_held_model_changed");return false;
 }
 if(!PoseCarryPairCurrent()){PoseFail("pose_carry_pair_changed");return false;}
 t_pose.context.consumer=consumer;t_pose.context.held=j.held;t_pose.context.press=j.edge_press;
 t_pose.context.release=j.edge_release;t_pose.context.magnitude=static_cast<float>(j.mag);t_pose.job=true;
 if(!PosePairWordBefore())return false;
 if(!PoseVehiclePairCurrent()){PoseFail("pose_vehicle_pair_changed");return false;}
 const bool accepted=!g_pose_before||g_pose_before(t_pose.context);
 return accepted&&!g_pose_fault;
}
bool PoseCarryBirth0Current(const cp::Pair& pair,bool bind=false) noexcept {
 AcquireSRWLockShared(&g_pose_birth_lock);const auto saved=PoseObservedBirthLocked(0);ReleaseSRWLockShared(&g_pose_birth_lock);
 const auto& b=pair.body[0];const bool current=birth::Current(saved,{pair.player,b.block,b.state,b.cs,b.entry});
 if(current&&bind)g_pose_carry_birth0=saved.serial;return current&&(bind||saved.serial==g_pose_carry_birth0);
}
bool PoseCarryPairCurrent() noexcept {
 if(!(g_pose_owners.held&holds::carry))return true;
 cp::Pair current{};auto read=PoseReader;
 return PoseOwned()&&!g_pose_fault&&cp::ReadPair(read,g_base,g_pose_player,current)&&
    cp::Same(current,g_pose_carry_pair)&&PoseCarryBirth0Current(current)&&PoseContextBirthCurrent(t_pose.context);
}
bool PoseAfter() noexcept {
 if(!PoseOwned())return !g_pose_fault;
 bool accepted=!g_pose_fault;
 if(accepted&&!PoseCarryPairCurrent()){PoseFail("pose_carry_pair_changed");accepted=false;}
 if(accepted&&!PoseVehiclePairCurrent()){PoseFail("pose_vehicle_pair_changed");accepted=false;}
 if(accepted&&g_pose_after)accepted=g_pose_after(t_pose.context)&&!g_pose_fault;
 if(!PosePairWordRestore()){PoseFail("pose_pair_word_restore");accepted=false;}
 return accepted&&!g_pose_fault;
}
bool PoseVarsNativeOwner(void* object,std::uint32_t id,bool phase) noexcept {
 if(!PoseOwned()||!t_pose.job||g_pose_fault||!t_pose_pair_word_token.active()||id!=1||!PoseVarsPatchesLive())return false;
 // A local0 sender must run in its own qualified unshadowed operation.
 // Writing its result into body1 shadow would lose local0 gameplay state.
 cp::Pair current{};auto read=PoseReader;if(!cp::ReadPair(read,g_base,g_pose_player,current))return false;
 if(!PoseCarryPairCurrent()||!PoseVehiclePairCurrent())return false;
 return reinterpret_cast<std::uintptr_t>(object)==current.body[id].carrier+(phase?0x30u:0u);
}
void PoseVarsMessage(void* object,std::uint32_t id,void* result,const void* message) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 if(PoseOwned()){
  if((g_pose_owners.held&holds::carry)&&id==0){PoseCarryOwnedOp1Reply(object,id,result,message,reinterpret_cast<std::uintptr_t>(_ReturnAddress()));return;}
  if(!PoseVarsNativeOwner(object,id,true)){PoseFail("pose_pair_word_message_owner");SetLastError(incoming);return;}
  SetLastError(incoming);g_pose_vars_message(object,id,result,message);return;
 }
 PoseTraceNestedGlobal("PoseVarsMessage",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);g_pose_vars_message(object,id,result,message);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
bool PoseVarsPair(void* object,std::uint32_t id) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;bool result=false;
 if(PoseOwned()){
  if(!PoseVarsNativeOwner(object,id,false)){PoseFail("pose_pair_word_predicate_owner");SetLastError(incoming);return false;}
  SetLastError(incoming);return g_pose_vars_pair(object,id);
 }
 PoseTraceNestedGlobal("PoseVarsPair",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return false;}
 __try {SetLastError(incoming);result=g_pose_vars_pair(object,id);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
void PoseVarsExport(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseVarsExport",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);g_pose_vars_export(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
int PoseVarsLua(void* lua) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;int result=0;
 PoseTraceNestedGlobal("PoseVarsLua",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return 0;}
 __try {SetLastError(incoming);result=g_pose_vars_lua(lua);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
int PoseVarsReset(void* lua) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;int result=0;
 PoseTraceNestedGlobal("PoseVarsReset",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return 0;}
 __try {SetLastError(incoming);if(PoseRequired()&&g_pose_held)PoseFail("pose_live_vars_reset");else result=g_pose_vars_reset(lua);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
int PoseVarsBulkReset(void* lua) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;int result=0;
 PoseTraceNestedGlobal("PoseVarsBulkReset",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return 0;}
 __try {SetLastError(incoming);if(PoseRequired()&&g_pose_held)PoseFail("pose_live_vars_bulk_reset");else result=g_pose_vars_bulk_reset(lua);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
void PoseVarsInit() noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseVarsInit",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(PoseRequired()&&g_pose_held)PoseFail("pose_live_vars_init");else g_pose_vars_init();outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
bool PoseMessageHeld() noexcept {return g_pose_message_bound&&(g_pose_owners.held&holds::carry)!=0;}
bool PoseMessageCurrent() noexcept {
 if(!PoseMessageHeld()||!PoseMessagePatchesLive())return false;
 msg::Observation current{};auto read=PoseReader;
 return msg::ReadPair(read,g_base,g_pose_player,current)&&msg::SameLease(current,g_pose_message_hold);
}
bool PoseMessageCapture(const cp::Pair& pair) noexcept {
 if(!PoseOwned()||!t_pose.job||!PoseMessagePatchesLive())return false;
 msg::Observation current{};auto read=PoseReader;
 if(!msg::ReadPair(read,g_base,pair.player,current))return false;
 g_pose_message_hold=current;g_pose_message_bound=true;return true;
}
void* PoseMessageCtor(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* result=object;
 PoseTraceNestedGlobal("PoseMessageCtor",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);if(PoseMessageHeld()&&reinterpret_cast<std::uintptr_t>(object)==g_pose_message_hold.provider)PoseFail("pose_held_message_ctor");else result=g_pose_message_ctor(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
void* PoseMessageDtor(void* object,unsigned flags) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* result=object;
 PoseTraceNestedGlobal("PoseMessageDtor",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);if(PoseMessageHeld()&&reinterpret_cast<std::uintptr_t>(object)==g_pose_message_hold.provider)PoseFail("pose_held_message_dtor");else result=g_pose_message_dtor(object,flags);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
void PoseMessageUnregister(void* provider,void* interface) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseMessageUnregister",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(PoseMessageHeld()&&msg::ProtectsInterface(g_pose_message_hold,reinterpret_cast<std::uintptr_t>(interface)))PoseFail("pose_held_message_unregister");else g_pose_message_unregister(provider,interface);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
void PoseMessageSetter(void* object,void* listener) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 // The complete leaf writes only [object+10,object+18). A distinct native
 // object cannot alter this retained provider/slab. It may remain stock inside
 // a current body1 job; overlapping fields always refuse before the store.
 if(PoseOwned()){
  if(PoseMessageHeld()&&msg::ProtectsSetter(g_pose_message_hold,reinterpret_cast<std::uintptr_t>(object))){PoseFail("pose_held_message_listener");SetLastError(incoming);return;}
  SetLastError(incoming);g_pose_message_setter(object,listener);return;
 }
 PoseTraceNestedGlobal("PoseMessageSetter",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(PoseMessageHeld()&&msg::ProtectsSetter(g_pose_message_hold,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_held_message_listener");else g_pose_message_setter(object,listener);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
void PoseMessageDispatch(void* provider) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseMessageDispatch",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);g_pose_message_dispatch(provider);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
bool PoseLocalFlagMapWritable(std::uintptr_t at,std::size_t n) noexcept {
 if(!at||!n||at>UINTPTR_MAX-n)return false;const auto end=at+n;
 while(at<end){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<const void*>(at),&m,sizeof(m))||m.State!=MEM_COMMIT||(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
  const auto access=m.Protect&0xffu;if(access!=PAGE_READWRITE&&access!=PAGE_WRITECOPY&&access!=PAGE_EXECUTE_READWRITE&&access!=PAGE_EXECUTE_WRITECOPY)return false;
  const auto base=reinterpret_cast<std::uintptr_t>(m.BaseAddress);if(base>UINTPTR_MAX-m.RegionSize||base+m.RegionSize<=at)return false;at=base+m.RegionSize<end?base+m.RegionSize:end;
 }return true;
}
bool PoseOriginalCode(std::uintptr_t at,void* out,std::size_t size) noexcept;
volatile LONG g_pose_flag_map_refusal_logged=0;
struct PoseFlagMapSetContinuation {bool active=false;NativePoseJobContext context{};std::uintptr_t map=0,storage=0;std::uint32_t key=0;std::uint16_t capacity=0,previous=0xffff;};
thread_local PoseFlagMapSetContinuation t_flag_map_set;
bool PoseFlagMapSetScope(const PoseFlagMapSetContinuation& token,const NativePoseJobContext& now,std::uintptr_t map,const owned_flag_map::Observation& observation,std::uint32_t key)noexcept{
 const auto& before=token.context;return token.active&&token.map==map&&token.storage==observation.storage&&token.capacity==observation.capacity&&token.key==key&&
 before.player==now.player&&before.go==now.go&&before.block==now.block&&before.state==now.state&&before.modelCS==now.modelCS&&before.modelEntry==now.modelEntry&&
 before.generation==now.generation&&before.sceneEpoch==now.sceneEpoch&&before.thread==now.thread&&before.sequence==now.sequence&&before.birthSerial==now.birthSerial&&
 before.pp==now.pp&&before.source==now.source&&before.windowPPVT==now.windowPPVT&&before.consumer==now.consumer;
}

bool PoseOwnedFlagMap(void* object,std::uint32_t key,std::uint64_t value,bool insert,owned_flag_map::Origin origin,std::uint32_t& result,DWORD incoming,DWORD& outgoing) noexcept {
 native_work_timing::Scope measured(native_work_timing::Kind::vars_map);
 namespace fm=owned_flag_map;NativePoseJobContext before{},after{};msg::Observation protectedBefore{},protectedAfter{};
 fm::Observation original{},expected{},observed{};auto reader=PoseReader;std::uintptr_t mismatch=0;
 const auto refuse=[&](const char* stage) noexcept {
  const DWORD saved=GetLastError();
  if(InterlockedCompareExchange(&g_pose_flag_map_refusal_logged,1,0)==0&&g_log){char line[256]{};
   if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"pose_flag_map=refused;stage=%s;insert=%u;key=%u;capacity=%u;used=%u;free=%u;seq=%u;consumer=%u\r\n",
    stage,insert?1u:0u,key,unsigned(original.capacity),unsigned(original.used),unsigned(original.free),unsigned(g_window_seq),t_pose.context.consumer)))g_log(line);
  }SetLastError(saved);return false;
 };
 if(!ReadNativePoseOwnedJobContext(before)||!t_pose_pair_word_token.active())return refuse("owned_context");
 if(!fm::CodeGate(PoseOriginalCode,g_base,0xa01d000,mismatch))return refuse("native_code");
 if(!msg::ReadPair(reader,g_base,before.player,protectedBefore))return refuse("current_provider");
 if(PoseMessageHeld()&&!msg::SameLease(protectedBefore,g_pose_message_hold))return refuse("held_provider");
 const fm::Protected protect{protectedBefore.provider,protectedBefore.slab,vw::Access::owner(t_pose_pair_word_token).vars+0x10e};
 const auto address=reinterpret_cast<std::uintptr_t>(object);
 if(!fm::ReadPair(reader,address,protect,original))return refuse("map_pair_or_topology");
 // Every native write is within these qualified ranges; no allocation/free.
 if(!PoseLocalFlagMapWritable(address+0x0a,4)||!PoseLocalFlagMapWritable(original.storage,std::size_t(original.capacity)*10))return refuse("writable_footprint");
 std::uint32_t expectedResult=0xffff;
 if(origin==fm::Origin::set){
  if(t_flag_map_set.active&&(!insert||!PoseFlagMapSetScope(t_flag_map_set,before,address,original,key)))return refuse("set_continuation_scope");
  if(!fm::PredictSet(original,key,value,insert,t_flag_map_set.active,t_flag_map_set.previous,expected,expectedResult))return refuse("set_or_prediction");
 }else if(origin!=fm::Origin::clear||!fm::PredictOrigin(original,key,value,insert,expected,expectedResult))return refuse("origin_prediction");
 SetLastError(incoming);result=insert?g_pose_message_insert_map(object,key,value):g_pose_message_delete_map(object,key);outgoing=GetLastError();
 const bool current=fm::ReadPair(reader,address,protect,observed)&&fm::Same(expected,observed)&&result==expectedResult&&
  msg::ReadPair(reader,g_base,before.player,protectedAfter)&&msg::Same(protectedBefore,protectedAfter)&&
  ReadNativePoseOwnedJobContext(after)&&after.player==before.player&&after.go==before.go&&after.block==before.block&&after.state==before.state&&
  after.modelCS==before.modelCS&&after.modelEntry==before.modelEntry&&after.generation==before.generation&&after.sceneEpoch==before.sceneEpoch&&
  after.thread==before.thread&&after.sequence==before.sequence&&after.birthSerial==before.birthSerial;
 if(current&&origin==fm::Origin::set){
  if(insert)t_flag_map_set={};
  else t_flag_map_set={true,after,address,original.storage,key,original.capacity,static_cast<std::uint16_t>(expectedResult)};
 }
 if(!current)refuse("native_postcondition");SetLastError(outgoing);return current;
}
std::uint32_t PoseMessageDeleteMap(void* map,std::uint32_t key) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uint32_t result=0xffff;
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 const auto origin=owned_flag_map::Source(g_base,caller,false);
 if(PoseOwned()&&origin!=owned_flag_map::Origin::none){
  SetLastError(incoming);const bool passed=PoseOwnedFlagMap(map,key,0,false,origin,result,incoming,outgoing);
  if(!passed)PoseFail("pose_owned_flag_map_delete");SetLastError(outgoing);return result;
 }
 PoseTraceNestedGlobal("PoseMessageDeleteMap",caller);
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);if(PoseMessageHeld()&&reinterpret_cast<std::uintptr_t>(map)==g_pose_message_hold.provider+0x2340&&key==g_pose_message_hold.key)PoseFail("pose_held_message_key_delete");else result=g_pose_message_delete_map(map,key);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
std::uint32_t PoseMessageInsertMap(void* map,std::uint32_t key,std::uint64_t value) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uint32_t result=0xffff;
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 const auto origin=owned_flag_map::Source(g_base,caller,true);
 if(PoseOwned()&&origin!=owned_flag_map::Origin::none){
  SetLastError(incoming);const bool passed=PoseOwnedFlagMap(map,key,value,true,origin,result,incoming,outgoing);
  if(!passed)PoseFail("pose_owned_flag_map_insert");SetLastError(outgoing);return result;
 }
 PoseTraceNestedGlobal("PoseMessageInsertMap",caller);
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);result=g_pose_message_insert_map(map,key,value);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
void PoseMessageArenaReset(void* arena) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseMessageArenaReset",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);
  if(PoseMessageHeld()&&msg::ProtectsArena(g_pose_message_hold,reinterpret_cast<std::uintptr_t>(arena)))PoseFail("pose_held_message_arena_reset");
  else g_pose_message_arena_reset(arena);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseSelectedExport(void* state,std::uint32_t actualId) noexcept {
 const DWORD incoming=GetLastError();const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 if(PoseOwned()&&caller==g_base+native_pose_selected_export_contract::callerReturn){
  sx::Scope scope{};const auto& c0=t_pose.context;NativePoseJobContext x{},y{};pi::Arm a{};
  a.P=c0.player;a.blk1=c0.block;a.ps1=c0.state;
  scope.codeGate=g_pose_installed&&g_pose_original_view.load(std::memory_order_acquire)&&g_pose_selected_export;
  scope.worker=PoseWorker();scope.writer=PoseOwned();scope.birthCurrent=PoseContextBirthCurrent(c0);
  scope.modelPair=PoseModel(a,x)&&PoseModel(a,y)&&x.modelCS==y.modelCS&&x.modelEntry==y.modelEntry;
  scope.selectedOwned=c0.selectedBodyOwned;scope.generation=c0.generation;scope.currentGeneration=g_pose_generation;
  scope.sceneEpoch=c0.sceneEpoch;scope.currentEpoch=g_pose_epoch;scope.thread=GetCurrentThreadId();scope.ownerThread=g_pose_thread;
  scope.sequence=static_cast<std::uint32_t>(g_window_seq);scope.contextSequence=c0.sequence;
  scope.birthSerial=c0.birthSerial;scope.currentBirth=g_pose_bound_birth;scope.image=g_base;scope.caller=caller;
  scope.player=c0.player;scope.block=c0.block;scope.state=c0.state;scope.cs=c0.modelCS;scope.entry=c0.modelEntry;
  scope.currentPlayer=g_pose_player;scope.currentBlock=g_pose_block;scope.currentState=a.ps1;
  scope.currentCS=y.modelCS;scope.currentEntry=y.modelEntry;scope.argumentState=reinterpret_cast<std::uintptr_t>(state);scope.actualBody=actualId;
  std::uint32_t localAgain=0,selectedAgain=0;
  const bool localPair=PoseRead(c0.player+0x214,scope.realLocal)&&PoseRead(c0.player+0x218,scope.selected)&&
      PoseRead(c0.player+0x214,localAgain)&&localAgain==scope.realLocal&&PoseRead(c0.player+0x218,selectedAgain)&&selectedAgain==scope.selected;
  if(!g_pose_fault&&t_pose.job&&localPair&&PoseParticipantsLive()&&sx::Suppress(scope))InterlockedIncrement(&g_pose_local_exports);
  else PoseFail("pose_local_export_owner_changed");
  SetLastError(incoming);return; // owned refusal NEVER enters local0 cache writer
 }
 SetLastError(incoming);g_pose_selected_export(state,actualId); // unchanged stock caller paths
}
// Exact complete retail111CBA0 consumes only RCX=CAM secondary. Incoming
// RDX is overwritten before use; R8/R9 and XMM arguments are not consumed.
// The complete body has no defined result; same ABI as the native phase slot.
// The extra EDX census parameter is consumed only at the exact indexed caller.
// The original frame method still consumes only its established RCX argument.
__declspec(noinline) void PoseCameraFrame(void* object,std::uint32_t incomingBody,std::uintptr_t senderPrimary) noexcept {
 const DWORD incoming=GetLastError();
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 if(PoseOwned()){
  InterlockedIncrement(&g_pose_cam_owned);
  const auto within=caller>=g_base&&caller-g_base<0xa01d000;
  InterlockedExchange(&g_pose_cam_caller,within?static_cast<LONG>(caller-g_base):0);
  pcam::Witness witness{};auto reader=PoseReader;
  const bool matched=pcam::Pair(reader,g_base,t_pose.context.player,reinterpret_cast<std::uintptr_t>(object),witness);
  InterlockedExchange(&g_pose_cam_owner,matched?1:0);
  const auto indexed=pcam::Incoming(caller,g_base,incomingBody);
  InterlockedExchange(&g_pose_cam_body,indexed.known?static_cast<LONG>(indexed.body):-1);
  pcam::Slice scope{};scope.owned=PoseOwned();scope.job=t_pose.job;scope.worker=g_pose_test&&PoseWorker();
  scope.birthCurrent=PoseContextBirthCurrent(t_pose.context);scope.modelReady=t_pose.context.modelReady;
  scope.selectedOwned=t_pose.context.selectedBodyOwned;scope.selected=t_pose.context.selectedBody;scope.consumer=static_cast<std::uint32_t>(t_pose.context.consumer);
  scope.sameLease=t_pose.context.generation==g_pose_generation&&t_pose.context.sceneEpoch==g_pose_epoch&&t_pose.context.thread==g_pose_thread;
  scope.fault=g_pose_fault!=0;scope.generation=g_pose_generation;scope.epoch=g_pose_epoch;
  scope.thread=GetCurrentThreadId();scope.ownerThread=g_pose_thread;scope.sequence=static_cast<std::uint32_t>(g_window_seq);
  scope.contextSequence=t_pose.context.sequence;scope.birthSerial=t_pose.context.birthSerial;
  pcam::SenderWitness source{};const char* stage=matched?"camera_selected_scope":"camera_local_view";
  const bool normalFiltered=matched&&pcam::SelectedReplica(reader,scope,g_base,caller,incomingBody,t_pose.context.player,t_pose.context.block,
      reinterpret_cast<std::uintptr_t>(object),senderPrimary,source,&stage);
  bool footFiltered=false;
  if(!normalFiltered&&matched&&witness.mode==4&&g_pose_vehicle_foot_camera_test){
   // An exact negative leaf filter. Do not skip the native job, manufacture
   // execution health, admit a camera method, or borrow a retained guard lease.
   auto sample=[&]()noexcept {
    vfoot::Scope s{};s.camera=scope;s.enabled=g_pose_vehicle_foot_camera_test;
    s.exactNative=!g_pose_bindings.enabled&&g_pose_installed&&g_pose_original_view.load(std::memory_order_acquire)&&
       g_pose_vehicle_foot_camera_reader&&g_pose_vehicle_foot_camera_reader==g_pose_vehicle_bindings.readOriginal;
    s.held=g_pose_held!=0||g_pose_owners.held!=0;s.pending=g_pose_clear!=0||g_pose_owners.pending!=0;
    s.retained=g_pose_vehicle_bound;s.stopping=g_shutdown!=0;s.camera.owned=PoseOwned();
    s.camera.birthCurrent=PoseContextBirthCurrent(t_pose.context);s.camera.fault=g_pose_fault!=0;
    s.camera.sequence=static_cast<std::uint32_t>(g_window_seq);s.camera.contextSequence=t_pose.context.sequence;
    s.camera.sameLease=t_pose.context.generation==g_pose_generation&&t_pose.context.sceneEpoch==g_pose_epoch&&
        t_pose.context.thread==g_pose_thread&&t_pose.context.birthSerial==g_pose_bound_birth;
    return s;
   };
   vfoot::Receipt foot{};footFiltered=vfoot::Filter(reader,sample,g_base,caller,incomingBody,t_pose.context.player,
      t_pose.context.block,reinterpret_cast<std::uintptr_t>(object),senderPrimary,foot,&stage);
  }
  const bool filtered=normalFiltered||footFiltered;
  InterlockedExchange(&g_pose_cam_sender,filtered?1:0);
  const LONG filteredCall=filtered?InterlockedIncrement(&g_pose_cam_filtered):0;
  const LONG footFilteredCall=footFiltered?InterlockedIncrement(&g_pose_cam_vehicle_foot_filtered):0;
  const LONG refusedCall=filtered?0:InterlockedIncrement(&g_pose_cam_refused);
  if(((filtered&&filteredCall==1)||(footFiltered&&footFilteredCall==1)||(!filtered&&refusedCall==1))&&g_log){
   char line[384]{};
   if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
      "pose_camera=%s; caller_rva=0x%08x; typed=%u; real=%u; selected=%u; target380=%u; selected384=%u; mode20c=%u; sequence=%u; odd=%u; consumer=%u; body_known=%u; incoming_body=%u; sender=%u; stage=%s\r\n",
      footFiltered?"vehicle_foot_filtered":filtered?"selected_replica_filtered":"owned_refusal",within?static_cast<unsigned>(caller-g_base):0u,matched?1u:0u,
      witness.realLocal,witness.selected,witness.target,witness.targetSelected,witness.mode,
      static_cast<unsigned>(g_window_seq),static_cast<unsigned>(g_window_seq)&1u,
      static_cast<unsigned>(t_pose.context.consumer),indexed.known?1u:0u,indexed.body,filtered?1u:0u,stage)))g_log(line);
  }
  if(filtered){SetLastError(incoming);return;} // exact body1 phase2 global-view callback only
  // Do not invent a nested camera caller or silently skip an unqualified
  // phase. Preserve the held writer/model and stop this private test first.
  PoseFail("pose_camera_owned_call_unqualified");SetLastError(incoming);return;
 }
 PoseSharedLease read{};
 if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try {SetLastError(incoming);g_pose_cam_frame(object);outgoing=GetLastError();InterlockedIncrement(&g_pose_cam_shared);}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
bool PoseCameraStubLive() noexcept {
 if(!g_pose_camera_stub)return false;MEMORY_BASIC_INFORMATION mbi{};
 if(VirtualQuery(reinterpret_cast<void*>(g_pose_camera_stub),&mbi,sizeof(mbi))!=sizeof(mbi)||
  mbi.AllocationBase!=reinterpret_cast<void*>(g_pose_camera_stub)||mbi.State!=MEM_COMMIT||mbi.Type!=MEM_PRIVATE||mbi.Protect!=PAGE_EXECUTE_READ||
  mbi.RegionSize<pcam::shimSize)return false;
 auto reader=PoseReader;return pcam::ShimMatches(reader,g_pose_camera_stub,reinterpret_cast<std::uintptr_t>(&PoseCameraFrame));
}
bool PoseCreateCameraStub() noexcept {
 if(g_pose_camera_stub)return PoseCameraStubLive();
 void* memory=near_code::AllocateNear(g_base+0x111cba5,pcam::shimSize);if(!memory)return false;
 unsigned char bytes[pcam::shimSize]{};pcam::Shim(reinterpret_cast<std::uintptr_t>(&PoseCameraFrame),bytes);
 std::memcpy(memory,bytes,sizeof(bytes));DWORD previous=0;
 if(!VirtualProtect(memory,sizeof(bytes),PAGE_EXECUTE_READ,&previous)||!FlushInstructionCache(GetCurrentProcess(),memory,sizeof(bytes)))return false;
 g_pose_camera_stub=reinterpret_cast<std::uintptr_t>(memory);return PoseCameraStubLive();
 // Never free an admitted or potentially executing entry. The tiny allocation
 // remains owned by this private process until its recorded cleanup terminates it.
}

bool PoseRouteHeld() noexcept {return g_pose_route_bound&&(g_pose_owners.held&holds::carry)!=0;}
bool PoseRouteCurrent() noexcept {
 if(!PoseRouteHeld()||!PoseRoutePatchesLive())return false;route::Observation now{};auto read=PoseReader;
 return route::ReadPair(read,g_base,g_pose_player,now)&&route::Identity(now,g_pose_route_hold)&&route::NoClassWork(now);
}
bool PoseRouteCapture(const cp::Pair& pair,route::Observation& out) noexcept {
 out={};if(!PoseOwned()||!t_pose.job||!PoseRoutePatchesLive())return false;auto read=PoseReader;
 return route::ReadPair(read,g_base,pair.player,out)&&out.go==pair.go&&route::NoClassWork(out);
}
__declspec(noinline) void* PoseRouteCtor(void* object,void* owner) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* result=object;
 PoseTraceNestedGlobal("PoseRouteCtor",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);
  if(PoseVehicleRegistryRefuses(0xbf4690,reinterpret_cast<std::uintptr_t>(object),reinterpret_cast<std::uintptr_t>(owner)))PoseFail("pose_vehicle_registry_lifecycle");
  else if(PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::interface_ctor,reinterpret_cast<std::uintptr_t>(object),reinterpret_cast<std::uintptr_t>(owner)))PoseFail("pose_retained_dispatch_interface_ctor");
  else result=g_pose_route_ctor(object,owner);outgoing=GetLastError();
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
 return result;
}
__declspec(noinline) void* PoseRouteDelete(void* object,unsigned flags) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* result=object;
 PoseTraceNestedGlobal("PoseRouteDelete",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);
  if(PoseVehicleRegistryRefuses(0xbf4a80,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_vehicle_registry_lifecycle");
  else if(PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::interface_delete,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_retained_dispatch_interface_delete");
  else result=g_pose_route_delete(object,flags);outgoing=GetLastError();
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
 return result;
}
__declspec(noinline) void* PoseRegistryCtor(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* result=object;
 PoseTraceNestedGlobal("PoseRegistryCtor",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);
  if(PoseVehicleRegistryRefuses(0xbf8a20,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_vehicle_registry_lifecycle");
  else if(PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::registry_ctor,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_retained_dispatch_registry_ctor");
  else result=g_pose_registry_ctor(object);outgoing=GetLastError();
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
 return result;
}
__declspec(noinline) void PoseRegistryDestroy(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseRegistryDestroy",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);
  if(PoseVehicleRegistryRefuses(0xbf8c10,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_vehicle_registry_lifecycle");
  else if(PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::registry_destruct,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_retained_dispatch_registry_destruct");
  else g_pose_registry_destroy(object);outgoing=GetLastError();
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void* PoseRegistryDelete(void* object,unsigned flags) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* result=object;
 PoseTraceNestedGlobal("PoseRegistryDelete",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return result;}
 __try {SetLastError(incoming);
  if(PoseVehicleRegistryRefuses(0xbf8f20,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_vehicle_registry_lifecycle");
  else if(PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::registry_delete,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_retained_dispatch_registry_delete");
  else result=g_pose_registry_delete(object,flags);outgoing=GetLastError();
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
 return result;
}
__declspec(noinline) void PoseRegistryReplace() noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseRegistryReplace",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);
  if(PoseVehicleRegistryRefuses(0xbf9060,0))PoseFail("pose_vehicle_registry_lifecycle");
  else if(PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::registry_replace,0))PoseFail("pose_retained_dispatch_registry_replace");
  else g_pose_registry_replace();outgoing=GetLastError();
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseRouteClassRegister(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseTraceNestedGlobal("PoseRouteClassRegister",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);
  if(PoseVehicleRegistryRefuses(0xbf91b0,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_vehicle_registry_lifecycle");
  else if(PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::class_register,reinterpret_cast<std::uintptr_t>(object)))PoseFail("pose_retained_dispatch_class_register");
  else g_pose_route_class_register(object);outgoing=GetLastError();
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseRouteClassUpdate(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;PoseTraceNestedGlobal("PoseRouteClassUpdate",reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
 PoseSharedLease read{};
 if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {
  const bool held=PoseRouteHeld()&&route::Refuse(g_pose_route_hold,route::Entry::class_update,reinterpret_cast<std::uintptr_t>(object));
  const bool vehicleHeld=g_pose_vehicle_bound&&g_pose_owners.held==holds::vehicle&&
   reinterpret_cast<std::uintptr_t>(object)==g_pose_vehicle_protection.registryHolder;
  if(vehicleHeld&&!PoseVehicleRegistryClassCurrent(read))PoseFail("pose_vehicle_registry_class_work");
  else if(held&&!PoseRouteCurrent())PoseFail("pose_retained_dispatch_class_work");
  else {
   SetLastError(incoming);g_pose_route_class_update(object);outgoing=GetLastError();
   if(vehicleHeld&&!PoseVehicleRegistryClassCurrent(read))PoseFail("pose_vehicle_registry_class_changed");
   if(held&&!PoseRouteCurrent())PoseFail("pose_dispatch_class_changed");
  }
 } __finally {PoseExitShared(read);SetLastError(outgoing);}
}

void PoseUpdate(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);g_pose_update(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
namespace td=native_pose_teardown_diagnostic;
volatile LONG g_pose_teardown_logged=0;
void PoseTeardownDiagnostic(std::uintptr_t object,unsigned kind,std::uint32_t hook,std::uintptr_t caller,std::uint32_t argument,std::uint32_t auxiliary,
 const birth::Receipt& localBirth,const birth::Receipt& partnerBirth) noexcept {
 const DWORD incoming=GetLastError();
 if(!g_log||InterlockedCompareExchange(&g_pose_teardown_logged,1,0)!=0){SetLastError(incoming);return;}
 td::Model expected[2]{};
 if(localBirth.ready)expected[0]={localBirth.identity.player,localBirth.identity.block,localBirth.identity.state,localBirth.identity.cs,localBirth.identity.entry,0};
 expected[1]={g_pose_player,g_pose_block,0,g_pose_cs,g_pose_entry,1};
 if(partnerBirth.ready&&partnerBirth.identity.block==g_pose_block)expected[1].state=partnerBirth.identity.state;
 if(g_pose_owners.held&holds::carry){for(unsigned id=0;id<2;++id){const auto& b=g_pose_carry_pair.body[id];expected[id]={g_pose_carry_pair.player,b.block,b.state,b.cs,b.entry,id};}}
 if(g_pose_owners.held&holds::vehicle){for(unsigned id=0;id<2;++id){const auto& b=g_pose_vehicle_hold.pair.body[id];expected[id]={g_pose_vehicle_hold.pair.player,b.block,b.state,b.cs,b.entry,id};}}
 td::Snapshot snapshot{};auto reader=PoseReader;const bool paired=td::Pair(reader,g_base,object,hook,expected,snapshot);
 if(hook==0x9cd030){native_pose_parts_diagnostic::Witness parts{};
  const bool partsPair=native_pose_parts_diagnostic::Pair(reader,g_base,object,expected,parts);char partsLine[512]{};
  if(SUCCEEDED(StringCchPrintfA(partsLine,ARRAYSIZE(partsLine),
   "pose_parts;paired=%u;q180=%x;q184=%x;flags=%x;count=%u;realized=%u;local=%u;selected=%u;id0=%u;id1=%u;pool0=%u;pool1=%u;status0=%u;status1=%u;block0=%u;block1=%u;caller_rva=%x\r\n",
   partsPair?1u:0u,parts.mask180,parts.mask184,parts.flags,parts.count,parts.realized,parts.local,parts.selected,
   static_cast<unsigned>(parts.actualId[0]),static_cast<unsigned>(parts.actualId[1]),static_cast<unsigned>(parts.pool[0]),static_cast<unsigned>(parts.pool[1]),
   static_cast<unsigned>(parts.parts[0]),static_cast<unsigned>(parts.parts[1]),parts.blockMatched[0]?1u:0u,parts.blockMatched[1]?1u:0u,td::Rva(g_base,caller))))g_log(partsLine);
 }
 char line[768]{};
 if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
  "pose_teardown;hook=%x;kind=%u;caller_rva=%x;argument=%u;auxiliary=%u;range=%u;count=%u;base=%u;paired=%u;typed0=%u;typed1=%u;same0=%u;same1=%u;roles0=%u;roles1=%u;retained0=%u;retained1=%u;type_rva=%x;slot=%u;slot_fn=%x;slot_match=%u;seq=%u;thread=%u;owned=%u;opened=%u;job=%u;consumer=%u;held=%u;purposes=%u\r\n",
  hook,kind,td::Rva(g_base,caller),argument,auxiliary,snapshot.rangeKnown?1u:0u,snapshot.count,snapshot.base,paired?1u:0u,snapshot.typed[0]?1u:0u,snapshot.typed[1]?1u:0u,
  snapshot.same[0]?1u:0u,snapshot.same[1]?1u:0u,snapshot.roles[0],snapshot.roles[1],td::Roles(object,expected[0]),td::Roles(object,expected[1]),
  snapshot.objectTypeRva,snapshot.slot,snapshot.slotFunctionRva,snapshot.slotMatched?1u:0u,
  static_cast<unsigned>(g_window_seq),GetCurrentThreadId(),PoseOwned()?1u:0u,t_pose.opened?1u:0u,t_pose.job?1u:0u,
  static_cast<unsigned>(t_pose.context.consumer),g_pose_held?1u:0u,static_cast<unsigned>(g_pose_owners.held))))g_log(line);
 SetLastError(incoming);
}
bool PoseDestroyRefused(std::uintptr_t object,unsigned kind,std::uint32_t hook,std::uintptr_t caller,std::uint32_t argument=UINT32_MAX,std::uint32_t auxiliary=UINT32_MAX) noexcept {
 birth::Receipt localBirth{},partnerBirth{};
 if(PoseRequired()&&g_pose_held){AcquireSRWLockShared(&g_pose_birth_lock);localBirth=PoseObservedBirthLocked(0);partnerBirth=PoseObservedBirthLocked(1);ReleaseSRWLockShared(&g_pose_birth_lock);}
 PoseInvalidateBirth(object,kind);
 if(!PoseRequired()||!g_pose_held)return false;
 const bool own=(kind==0?object==g_pose_block:kind==1?object==g_pose_cs:kind==2?object==g_pose_entry:object==g_pose_go)||
     ((g_pose_owners.held&holds::carry)&&cp::MatchesTeardown(g_pose_carry_pair,object,kind))||
     ((g_pose_owners.held&holds::vehicle)&&cp::MatchesTeardown(g_pose_vehicle_hold.pair,object,kind));
 if(own){PoseTeardownDiagnostic(object,kind,hook,caller,argument,auxiliary,localBirth,partnerBirth);PoseFail("pose_live_model_teardown");return true;}return false;
}
// These entries execute before native whole-owner release. Child block guards
// alone cannot retain Q/message/controllers or the enclosing GO allocation.
void PoseInvalidateWholeBirth(std::uintptr_t go) noexcept {
 AcquireSRWLockExclusive(&g_pose_birth_lock);
 whole_player_lifetime_checks::InvalidateWhole(g_pose_birth,g_pose_birth_epoch,go);
 if(g_pose_birth0.ready&&g_pose_birth0.identity.player>=0x20&&g_pose_birth0.identity.player-0x20==go)g_pose_birth0={};
 PoseInvalidateActivationLocked(go,4);
 ReleaseSRWLockExclusive(&g_pose_birth_lock);
}
bool PoseWholePlayerRefused(std::uintptr_t go,std::uint32_t hook,std::uintptr_t caller,std::uint32_t argument) noexcept {
 // The shared reader must already be held: otherwise a writer could publish a
 // held lease between an early test and the original's first mutation.
 const bool retained=PoseRequired()&&g_pose_held&&go==g_pose_go;
 birth::Receipt localBirth{},partnerBirth{};if(retained){AcquireSRWLockShared(&g_pose_birth_lock);localBirth=PoseObservedBirthLocked(0);partnerBirth=PoseObservedBirthLocked(1);ReleaseSRWLockShared(&g_pose_birth_lock);}
 PoseInvalidateWholeBirth(go);
 if(retained){PoseTeardownDiagnostic(go,3,hook,caller,argument,UINT32_MAX,localBirth,partnerBirth);PoseFail("pose_live_whole_player_teardown");return true;}
 return false;
}
__declspec(noinline) void* PoseDeletePlayer(void* object,unsigned flags) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return object;}
 void* result=object;
 __try {SetLastError(incoming);if(!PoseWholePlayerRefused(reinterpret_cast<std::uintptr_t>(object),0x9bb7a0,caller,flags))result=g_pose_delete_player(object,flags);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
__declspec(noinline) void PoseResetPlayer(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseWholePlayerRefused(reinterpret_cast<std::uintptr_t>(object),0x9c2f70,caller,UINT32_MAX))g_pose_reset_player(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseDestroyBlock(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),0,0x9bb260,caller,UINT32_MAX))g_pose_destroy_block(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void* PoseDestroyCs(void* object,unsigned flags) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return object;}void* result=object;
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),1,0xaf0140,caller,flags))result=g_pose_destroy_cs(object,flags);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
__declspec(noinline) void PoseRetireBody(void* object,std::uint32_t id,std::uint8_t callerControl) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),0,0x9bfc90,caller,id,callerControl))g_pose_retire_body(object,id,callerControl);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseDestroyEntry(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),2,0xb6cd60,caller,UINT32_MAX))g_pose_destroy_entry(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseRebindBody(void* object,std::uint32_t id) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),0,0x9cc850,caller,id))g_pose_rebind(object,id);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseApplyParts(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {
  SetLastError(incoming);
  const bool retained=PoseRequired()&&g_pose_held&&reinterpret_cast<std::uintptr_t>(object)==g_pose_go;
  if(retained&&g_pose_owners.held==holds::sleep){
   if(t_parts.read){PoseFail("pose_parts_reentrant");}
   else{
    t_parts={&read,reinterpret_cast<std::uintptr_t>(object),caller};
    NativePoseSleepPartsContext before{},pre{},after{};
    const bool admitted=PosePartsContext(before)&&g_pose_parts_validator&&g_pose_parts_validator()&&
     PosePartsContext(pre)&&parts_policy::Same(before,pre);
    if(admitted){
     // No native dirty-bit clearing/shadow or birth invalidation. Exact current
     // target branch does no work; original preserves all local0 processing.
     SetLastError(incoming);PoseActivationPartsOriginal(object);outgoing=GetLastError();
     if(!PosePartsContext(after)||!parts_policy::Same(pre,after)||!g_pose_parts_validator())
      PoseFail("pose_parts_late_target_changed");
     SetLastError(outgoing); // preserve the native original through proof reads
    }else PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),3,0x9cd030,caller,UINT32_MAX);
   }
  }else if(retained&&g_pose_owners.held==(holds::sleep|holds::carry)){
   if(t_parts.read)PoseFail("pose_dual_parts_reentrant");
   else{
    t_parts={&read,reinterpret_cast<std::uintptr_t>(object),caller};
    NativePoseDualPartsContext before{},pre{},after{};
    bool admitted=false;
    if(PoseDualPartsContext(before)){
     t_dual_parts_reason="dual_module_admission";
     if(g_pose_parts_validator&&g_pose_parts_validator()&&PoseDualPartsContext(pre)){
      t_dual_parts_reason="dual_before_changed";admitted=SameNativePoseDualPartsPoll(before,pre);
     }
    }
    if(admitted){
     SetLastError(incoming);PoseActivationPartsOriginal(object);outgoing=GetLastError();
     const bool current=PoseDualPartsContext(after);
     const bool same=current&&SameNativePoseDualPartsPoll(pre,after);
     const bool module=same&&g_pose_parts_validator();
     if(!current||!same||!module){
      if(current)t_dual_parts_reason=same?"dual_module_late_refused":"dual_after_changed";
      PoseDualPartsRefusalLog();PoseFail("pose_dual_parts_late_changed");
     }
     if(current&&same&&module){const auto observe=g_pose_dual_read_observer.load(std::memory_order_acquire);if(observe)observe(after);}
     SetLastError(outgoing);
    }else {PoseDualPartsRefusalLog();PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),3,0x9cd030,caller,UINT32_MAX);}
   }
  }else if(retained&&g_pose_owners.held==holds::vehicle){
   if(t_parts.read)PoseFail("pose_vehicle_parts_reentrant");
   else{
    t_parts={&read,reinterpret_cast<std::uintptr_t>(object),caller};
    NativePoseVehiclePartsContext before{},pre{},after{};
    const bool admitted=PoseVehiclePartsContext(before)&&PoseVehiclePartsContext(pre)&&vehicle_core::SameParts(before,pre);
    if(admitted){
     // Exact both-body no-work and local0 desired/current profile are freshly
     // proved. Original native processing runs with no dirty-bit shadow.
     SetLastError(incoming);PoseActivationPartsOriginal(object);outgoing=GetLastError();
     if(!PoseVehiclePartsContext(after)||!vehicle_core::SameParts(pre,after))PoseFail("pose_vehicle_parts_late_changed");
     SetLastError(outgoing);
    }else PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),3,0x9cd030,caller,UINT32_MAX);
   }
  }else if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),3,0x9cd030,caller,UINT32_MAX))PoseActivationPartsOriginal(object);
  outgoing=GetLastError();
 }
 __finally {t_parts={};PoseExitShared(read);SetLastError(outgoing);}
}
__declspec(noinline) void PoseSleepHint(void* ui,std::uint16_t hint,std::uint32_t priority,std::uint8_t flag,
 std::uint8_t enable,std::uint8_t extra6,std::uint8_t extra7) noexcept {
 const DWORD incoming=GetLastError();
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 if(PoseOwned()&&caller==g_base+0x12cad45&&hint==0x5d&&priority==2&&!flag&&enable==1&&!extra6&&!extra7){
  const auto at=reinterpret_cast<std::uintptr_t>(ui);std::uintptr_t expected=0,expectedAfter=0,owner=0,vt=0,primary=0;
  const bool typed=at>=0x20&&PoseRead(g_pose_player+0xd8,expected)&&expected==at&&
      PoseRead(at,vt)&&vt==g_base+0x23af700&&PoseRead(at-0x20,primary)&&primary==g_base+0x23af6d0&&
      PoseRead(at+0x28,owner)&&owner==g_pose_player&&PoseRead(g_pose_player+0xd8,expectedAfter)&&expectedAfter==expected&&
      PoseRead(at,vt)&&vt==g_base+0x23af700&&PoseRead(at-0x20,primary)&&primary==g_base+0x23af6d0&&
      PoseRead(at+0x28,owner)&&owner==g_pose_player;
  if(!typed)PoseFail("pose_sleep_hint_owner");
  else InterlockedIncrement(&g_pose_hints);
  SetLastError(incoming);return; // mandatory refusal never enters the shared hint producer
 }
 if(teammate_targeting::FilterPartnerPrompt(ui,0x3c0,hint,priority,enable,caller)){SetLastError(incoming);return;}
 SetLastError(incoming);g_pose_hint(ui,hint,priority,flag,enable,extra6,extra7);
}
struct PosePatch {
 std::uintptr_t rva=0,wrapper=0,relay=0;unsigned char original[5]{},patch[5]{};
};
PosePatch g_pose_patches[groups::total]{};
bool PosePatchOwned(const PosePatch& p) noexcept {
 unsigned char current[5]{};if(!PoseReader(g_base+p.rva,current,5)||std::memcmp(current,p.patch,5)||current[0]!=0xe9)return false;
 std::int32_t relative=0;std::memcpy(&relative,current+1,4);
 const auto relay=static_cast<std::uintptr_t>(static_cast<std::int64_t>(g_base+p.rva+5)+relative);
 if(relay!=p.relay)return false;if(relay==p.wrapper)return pcam::OwnedEntry(p.rva,p.wrapper,g_pose_camera_stub,p.rva==0x111cba0&&PoseCameraStubLive());
 unsigned char jump[14]{};std::uintptr_t target=0;
 if(!PoseReader(relay,jump,sizeof(jump))||jump[0]!=0xff||jump[1]!=0x25||jump[2]||jump[3]||jump[4]||jump[5])return false;
 std::memcpy(&target,jump+6,8);if(target!=p.wrapper)return false;
 return pcam::OwnedEntry(p.rva,p.wrapper,g_pose_camera_stub,p.rva==0x111cba0&&PoseCameraStubLive());
}
bool PoseRoutePatchesLive() noexcept {
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
 !g_pose_route_ctor||!g_pose_route_delete||!g_pose_registry_ctor||!g_pose_registry_destroy||!g_pose_registry_delete||!g_pose_registry_replace||!g_pose_route_class_update||!g_pose_route_class_register)return false;
 for(unsigned i=0;i<carry_dispatch_lifetime_contract::hookCount;++i)if(g_pose_patches[groups::routeFirst+i].rva!=carry_dispatch_lifetime_contract::hookRvas[i]||!PosePatchOwned(g_pose_patches[groups::routeFirst+i]))return false;
 // Recognize only the carry adapter's real recipient pointer receipt. Do not
 // change the eight core hook ownership checks above or originalize data.
 auto raw=PoseReader;
 for(const auto& slot:carry_dispatch_lifetime_contract::slots)
  if(!route::SlotLive(raw,&coop_carry::OverlayOriginal,g_base,slot))return false;
 return true;
}

bool PoseVarsPatchesLive() noexcept {
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
    !g_pose_vars_init||!g_pose_vars_lua||!g_pose_vars_reset||!g_pose_vars_bulk_reset||!g_pose_vars_export||!g_pose_vars_pair||!g_pose_vars_message)return false;
 constexpr std::uintptr_t expected[]={0x11301b0,0x1129d70,0x12ce870,0x9dc5f0,0x9aaa90,0x996b70,0x9ab9d0};
 for(unsigned n=0;n<7;++n)if(g_pose_patches[groups::varsFirst+n].rva!=expected[n]||!PosePatchOwned(g_pose_patches[groups::varsFirst+n]))return false;return true;
}
bool PoseMessagePatchesLive() noexcept {
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
  !g_pose_message_ctor||!g_pose_message_dtor||!g_pose_message_unregister||!g_pose_message_setter||!g_pose_message_dispatch||!g_pose_message_delete_map||!g_pose_message_insert_map||!g_pose_message_arena_reset)return false;
 const auto& expected=native_pose_message_lifetime_contract::hookRvas;
 for(unsigned i=0;i<native_pose_message_lifetime_contract::hookCount;++i)if(g_pose_patches[groups::providerFirst+i].rva!=expected[i]||!PosePatchOwned(g_pose_patches[groups::providerFirst+i]))return false;
 for(const auto& slot:native_pose_message_lifetime_contract::slots){std::uintptr_t v=0;if(!PoseRead(g_base+slot.rva,v)||v!=g_base+slot.target)return false;}
 for(const auto& slot:carry_message_provider_contract::slots){std::uintptr_t v=0;if(!PoseRead(g_base+slot.rva,v)||v!=g_base+slot.target)return false;}
 return true;
}
bool PoseOriginalCode(std::uintptr_t at,void* out,std::size_t size) noexcept {
 native_work_timing::Scope measured(native_work_timing::Kind::original_view);
 if(!at||!size||at>UINTPTR_MAX-size||!PoseReader(at,out,size))return false;
 // The early input hook owns only this call's four displacement bytes.
 // Require its current E8, exact rel32-to-owned-stub and FF25-to-our-wrapper.
 const auto call=g_base+c::hook_site,callEnd=call+5;
 if(at<callEnd&&at+size>call+1){
  unsigned char entry[5]{},stub[14]{};std::int32_t rel=0;std::uintptr_t target=0;
  if(!g_ready||!g_stub||!PoseReader(call,entry,5)||entry[0]!=0xe8)return false;
  std::memcpy(&rel,entry+1,4);
  const auto destination=static_cast<std::uintptr_t>(static_cast<std::int64_t>(callEnd)+rel);
  if(destination!=reinterpret_cast<std::uintptr_t>(g_stub)||!PoseReader(destination,stub,14)||
     stub[0]!=0xff||stub[1]!=0x25||stub[2]||stub[3]||stub[4]||stub[5])return false;
  std::memcpy(&target,stub+6,8);if(target!=reinterpret_cast<std::uintptr_t>(&InputDispatchHook))return false;
  unsigned char original[4]{};const auto displacement=c::hook_rel32_orig;std::memcpy(original,&displacement,4);
  const auto first=at>call+1?at:call+1,last=at+size<callEnd?at+size:callEnd;
  std::memcpy(static_cast<unsigned char*>(out)+(first-at),original+(first-call-1),last-first);
 }
 if(!g_pose_original_view.load(std::memory_order_acquire))return true;
 for(const auto& p:g_pose_patches){
  const auto start=g_base+p.rva,end=start+5;if(at>=end||at+size<=start)continue;
  if(!PosePatchOwned(p))return false;
  const auto first=at>start?at:start,last=at+size<end?at+size:end;
  std::memcpy(static_cast<unsigned char*>(out)+(first-at),p.original+(first-start),last-first);
 }return true;
}
// Actual origin wrappers share the existing exclusion and lifetime domain.
#include "carry_op0_origin_impl.inc"
#include "carry_queue_lifetime_impl.inc"
#include "carry_deferred_copy_impl.inc"
#include "carry_op1_owned_impl.inc"
#include "carry_op2_shared_impl.inc"
__declspec(noinline) void* PoseConstructBlock(void* object,void* descriptor,void* payload) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;const auto thread=GetCurrentThreadId();
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 InterlockedIncrement(&g_birth_enter);
 const bool enteredReady=g_pose_installed&&g_pose_original_view.load(std::memory_order_acquire)&&PoseWorker();
 if(enteredReady)InterlockedIncrement(&g_birth_ready);
 const bool exactCaller=caller==g_base+native_pose_birth_contract::callerReturn;
 if(exactCaller)InterlockedIncrement(&g_birth_exact);
 InterlockedExchange(&g_birth_caller,caller>=g_base&&caller-g_base<0xa01d000?static_cast<LONG>(caller-g_base):0);
 PoseSharedLease read{};if(!PoseEnterShared(read)){
  SetLastError(incoming);
  if(g_pose_bindings.enabled)return descriptor; // synthetic seam never installs retail constructor
#ifdef MGS_BACKGROUND_WORKER
  // There is no native constructor failure return. The qualified private
  // process must end before its creator can publish an uninitialized block.
  // Required is retained from explicit private/copied-save admission, even
  // though Refuse has already withdrawn the worker's Active flag.
  if(g_pose_installed&&PoseRequired()&&g_pose_test){
   TerminateProcess(GetCurrentProcess(),0x4d475301u);
   for(;;)Sleep(1000); // if termination fails, never resume native creator
  }
#endif
  // Installed readers can fail only for an active/poisoned writer, so this
  // branch is a broken internal contract. Hold caller for owned supervisor.
  for(;;)Sleep(1000);
 }
 const auto birthEpoch=PoseBirthEpoch();
 void* result=descriptor;bool normal=false;
 __try {SetLastError(incoming);result=g_pose_construct(object,descriptor,payload);outgoing=GetLastError();normal=true;}
 __finally {
  const auto block=reinterpret_cast<std::uintptr_t>(object);std::uint32_t count=0,id=0;
  const bool metadata=PoseRead(block+8,count)&&PoseRead(block+0xc,id);
  InterlockedExchange(&g_birth_count,count<=16?static_cast<LONG>(count):0);
  InterlockedExchange(&g_birth_base,id<16?static_cast<LONG>(id):0);
  if(metadata&&count==1&&id<=1){
   if(id==1)InterlockedIncrement(&g_birth_body1);const char* why="birth_not_entry_ready";
   if(normal&&enteredReady&&result==descriptor&&exactCaller&&g_pose_installed&&
      g_pose_original_view.load(std::memory_order_acquire)&&PoseWorker()&&!g_pose_fault){
    std::uintptr_t owner=0,ps=0;pi::Arm a{};NativePoseJobContext x{},y{};why="birth_block_owner";
    if(PoseRead(block+0x138,owner)&&owner&&PoseRead(block+0x78,ps)&&ps){
     a.P=owner;a.blk1=block;a.ps1=ps;
     if(PoseModel(a,x,&why,id)&&PoseModel(a,y,&why,id)&&x.modelCS==y.modelCS&&x.modelEntry==y.modelEntry){
      if(id==1)InterlockedIncrement(&g_birth_model);birth::Completion c{};c.identity={owner,block,ps,y.modelCS,y.modelEntry};
      c.installedAtEntry=enteredReady;c.installedAtExit=true;c.workerAtEntry=true;c.workerAtExit=PoseWorker();
      c.exactCaller=true;c.normalReturn=true;c.typedPair=true;c.entryThread=thread;c.exitThread=GetCurrentThreadId();c.lifecycleEpoch=birthEpoch;
      if(PoseRecordBirth(c,id)){if(id==1)InterlockedIncrement(&g_birth_done);why="birth_completed";}
      else {if(id==1)InterlockedIncrement(&g_birth_overlap);why="birth_lifecycle_overlap";}
     }
    }
   }else if(!exactCaller)why="birth_unknown_caller";
   else if(!normal||result!=descriptor)why="birth_abnormal_return";
   else if(enteredReady)why="birth_exit_unready";
   if(id==1)g_pose_birth_reason=why;
  }
  PoseExitShared(read);SetLastError(outgoing);
 }return result;
}
#ifdef MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER
bool PoseActivationHooksLive() noexcept {
 if(!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire))return false;
 for(unsigned n=0;n<groups::activationCount;++n){const auto& p=g_pose_patches[groups::activationFirst+n];
  if(p.rva!=groups::expected[groups::activationFirst+n]||!PosePatchOwned(p))return false;
 }return g_pose_activate&&g_pose_activation_factory;
}
#endif
bool PosePinnedPrefix(std::uintptr_t rva,unsigned char (&out)[5]) noexcept {
#ifdef MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER
 for(const auto& s:native_pose_activation_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
#endif
 for(const auto& site:carry_native_queue_contract::sites)if(site.rva==rva&&site.size>=5){std::memcpy(out,site.bytes,5);return true;}
 for(const auto& site:carry_queue_lifetime_contract::sites)if(site.rva==rva&&site.size>=5){std::memcpy(out,site.bytes,5);return true;}
 for(const auto& site:carry_op0_origin_contract::sites)if(site.rva==rva&&site.size>=5){std::memcpy(out,site.bytes,5);return true;}
 for(const auto& s:carry_dispatch_lifetime_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:native_pose_birth_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:camera_mode_window_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:native_pose_window_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:carry_model_lifetime_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:carry_model_retirement_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:native_pose_message_lifetime_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:carry_vars_pair_word_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:native_pose_vars_bulk_reset_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:native_pose_selected_export_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:native_pose_camera_reader_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:whole_player_lifetime_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 return false;
}
bool PoseInstall(const void* image,std::size_t span,bool fingerprint) noexcept {
 if(g_pose_installed)return reinterpret_cast<std::uintptr_t>(image)==g_base;
 if(!fingerprint||!image||reinterpret_cast<std::uintptr_t>(image)!=g_base||!g_ready||!PoseWorker()){g_pose_reason="pose_install_admission";return false;}
 std::uintptr_t currentPlayer=0;
 if(!PoseRead(g_base+c::player_slot_rva,currentPlayer)||currentPlayer){g_pose_reason="pose_install_requires_player_absent";return false;}
 auto reader=PoseOriginalCode;std::uintptr_t mismatch=0;
#ifdef MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER
 if(!activation::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_activation_full_code_or_slot";return false;}
#endif
 if(span<0xa01d000||!PoseCarryOriginCode(mismatch)){g_pose_reason="pose_carry_origin_full_code";return false;}
 if(!PoseQueueCode(mismatch)){g_pose_reason="pose_carry_q_lifetime_code";return false;}
 if(!PoseQueuedCode(mismatch)){g_pose_reason="pose_carry_op1_full_code";return false;}
 if(!PoseDeferredCode(mismatch)||!PoseDeferredPinImport()){g_pose_reason="pose_carry_deferred_setter_or_import";return false;}
 if(!dual_parts_local_profile_contract::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_dual_profile_full_code";return false;}
 if(!sleep_parts_contract::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_parts_full_code";return false;}
 if(!cw::CodeGate(reader,g_base,span,mismatch,&coop_carry::ReadPresentationSetter)){g_pose_reason="pose_code_c0";return false;}
 if(!route::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_dispatch_lifetime_code";return false;}
 unsigned char bytes[8192]{};
 for(const auto& s:native_pose_window_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!reader(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){g_pose_reason="pose_full_code";return false;}
 }
 for(const auto& s:carry_model_lifetime_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!reader(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){g_pose_reason="pose_full_code";return false;}
 }
 for(const auto& s:carry_model_retirement_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!reader(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){g_pose_reason="pose_full_code";return false;}
 }
 for(const auto& s:native_pose_birth_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!reader(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){g_pose_reason="pose_birth_code";return false;}
 }
 if(!msg::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_message_lifetime_full_code";return false;}
 if(!vw::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_vars_word_full_code";return false;}
 if(!sx::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_selected_export_code_or_slot";return false;}
 if(!pcam::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_camera_reader_code_or_slot";return false;}
 const auto checkSites=[&](const auto& sites)noexcept {
  for(const auto& s:sites){if(s.rva>span||s.size>span-s.rva)return false;
   for(std::size_t off=0;off<s.size;){const auto n=(s.size-off)<sizeof(bytes)?s.size-off:sizeof(bytes);
    if(!reader(g_base+s.rva+off,bytes,n)||std::memcmp(bytes,s.bytes+off,n))return false;off+=n;}
  }return true;
 };
 if(!checkSites(native_pose_vars_bulk_reset_contract::sites)){g_pose_reason="pose_vars_bulk_reset_code";return false;}
 if(!checkSites(carry_owned_handshake_contract::sites)||!checkSites(carry_request_retirement_contract::sites)){g_pose_reason="pose_carry_full_code";return false;}
 for(const auto& s:carry_plugin_contract::sites){if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!reader(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){g_pose_reason="pose_carry_type_code";return false;}}
 for(const auto& s:carry_plugin_contract::slots){std::uintptr_t got=0;const auto at=s.vtable_rva+s.offset;if(at>span||8>span-at||!PoseRead(g_base+at,got)||got!=g_base+s.target_rva){g_pose_reason="pose_carry_slots";return false;}}
 for(const auto& s:carry_owned_handshake_contract::slots){std::uintptr_t got=0;if(s.rva>span||8>span-s.rva||!PoseRead(g_base+s.rva,got)||got!=g_base+s.target){g_pose_reason="pose_recipient_slots";return false;}}
 for(const auto& s:carry_request_retirement_contract::slots){std::uintptr_t got=0;if(s.rva>span||8>span-s.rva||!PoseRead(g_base+s.rva,got)||got!=g_base+s.target){g_pose_reason="pose_queue_slots";return false;}}
 if(!whole_player_lifetime_checks::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_whole_player_code_or_slot";return false;}
 for(const auto& s:native_pose_window_contract::slots){
  std::uintptr_t got=0;if(s.rva>span||8>span-s.rva||!PoseRead(g_base+s.rva,got)||got!=g_base+s.target){g_pose_reason="pose_block_rebind_slot";return false;}
 }
 for(const auto& s:carry_model_lifetime_contract::slots){
  std::uintptr_t got=0;if(s.rva>span||8>span-s.rva||!PoseRead(g_base+s.rva,got)||got!=g_base+s.target)return false;
 }
 std::uintptr_t slot=0;if(!PoseRead(g_base+0x23af700+0x268,slot)||slot!=g_base+0xfe8770)return false;
 const auto status=MH_Initialize();if(status!=MH_OK&&status!=MH_ERROR_ALREADY_INITIALIZED)return false;
 HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
    reinterpret_cast<LPCWSTR>(&PoseUpdate),&pinned))return false;
 if(!PoseCreateCameraStub()){g_pose_reason="pose_camera_stub_refused";return false;}
 struct Hook {std::uintptr_t rva;void* wrapper;void** original;};
 const Hook hooks[]={{0x11f6760,reinterpret_cast<void*>(&PoseUpdate),reinterpret_cast<void**>(&g_pose_update)},
  {0xfe8770,reinterpret_cast<void*>(&PoseSleepHint),reinterpret_cast<void**>(&g_pose_hint)},
  {0x9bb260,reinterpret_cast<void*>(&PoseDestroyBlock),reinterpret_cast<void**>(&g_pose_destroy_block)},
  {0xaf0140,reinterpret_cast<void*>(&PoseDestroyCs),reinterpret_cast<void**>(&g_pose_destroy_cs)},
  {0xb6cd60,reinterpret_cast<void*>(&PoseDestroyEntry),reinterpret_cast<void**>(&g_pose_destroy_entry)},
  {0x9bfc90,reinterpret_cast<void*>(&PoseRetireBody),reinterpret_cast<void**>(&g_pose_retire_body)},
  {0x9cc850,reinterpret_cast<void*>(&PoseRebindBody),reinterpret_cast<void**>(&g_pose_rebind)},
  {0x9cd030,reinterpret_cast<void*>(&PoseApplyParts),reinterpret_cast<void**>(&g_pose_parts)},
  {0x9c3cd0,reinterpret_cast<void*>(&PoseConstructBlock),reinterpret_cast<void**>(&g_pose_construct)},
  {0x9bb7a0,reinterpret_cast<void*>(&PoseDeletePlayer),reinterpret_cast<void**>(&g_pose_delete_player)},
  {0x9c2f70,reinterpret_cast<void*>(&PoseResetPlayer),reinterpret_cast<void**>(&g_pose_reset_player)},
  {0xff5510,reinterpret_cast<void*>(&PoseSelectedExport),reinterpret_cast<void**>(&g_pose_selected_export)},
  {0x111cba0,reinterpret_cast<void*>(g_pose_camera_stub),reinterpret_cast<void**>(&g_pose_cam_frame)},
  {0x11301b0,reinterpret_cast<void*>(&PoseVarsMessage),reinterpret_cast<void**>(&g_pose_vars_message)},
  {0x1129d70,reinterpret_cast<void*>(&PoseVarsPair),reinterpret_cast<void**>(&g_pose_vars_pair)},
  {0x12ce870,reinterpret_cast<void*>(&PoseVarsExport),reinterpret_cast<void**>(&g_pose_vars_export)},
  {0x9dc5f0,reinterpret_cast<void*>(&PoseVarsReset),reinterpret_cast<void**>(&g_pose_vars_reset)},
  {0x9aaa90,reinterpret_cast<void*>(&PoseVarsLua),reinterpret_cast<void**>(&g_pose_vars_lua)},
  {0x996b70,reinterpret_cast<void*>(&PoseVarsInit),reinterpret_cast<void**>(&g_pose_vars_init)},
  {0x9ab9d0,reinterpret_cast<void*>(&PoseVarsBulkReset),reinterpret_cast<void**>(&g_pose_vars_bulk_reset)},
  {0x17fea0,reinterpret_cast<void*>(&PoseMessageCtor),reinterpret_cast<void**>(&g_pose_message_ctor)},
  {0x1802a0,reinterpret_cast<void*>(&PoseMessageDtor),reinterpret_cast<void**>(&g_pose_message_dtor)},
  {0x180640,reinterpret_cast<void*>(&PoseMessageUnregister),reinterpret_cast<void**>(&g_pose_message_unregister)},
  {0x2dd300,reinterpret_cast<void*>(&PoseMessageSetter),reinterpret_cast<void**>(&g_pose_message_setter)},
  {0x180760,reinterpret_cast<void*>(&PoseMessageDispatch),reinterpret_cast<void**>(&g_pose_message_dispatch)},
  {0x180a20,reinterpret_cast<void*>(&PoseMessageDeleteMap),reinterpret_cast<void**>(&g_pose_message_delete_map)},
  {0x180c10,reinterpret_cast<void*>(&PoseMessageInsertMap),reinterpret_cast<void**>(&g_pose_message_insert_map)},
  {0x3a1db30,reinterpret_cast<void*>(&PoseMessageArenaReset),reinterpret_cast<void**>(&g_pose_message_arena_reset)},
  {0xbf4690,reinterpret_cast<void*>(&PoseRouteCtor),reinterpret_cast<void**>(&g_pose_route_ctor)},
  {0xbf4a80,reinterpret_cast<void*>(&PoseRouteDelete),reinterpret_cast<void**>(&g_pose_route_delete)},
  {0xbf8a20,reinterpret_cast<void*>(&PoseRegistryCtor),reinterpret_cast<void**>(&g_pose_registry_ctor)},
  {0xbf8c10,reinterpret_cast<void*>(&PoseRegistryDestroy),reinterpret_cast<void**>(&g_pose_registry_destroy)},
  {0xbf8f20,reinterpret_cast<void*>(&PoseRegistryDelete),reinterpret_cast<void**>(&g_pose_registry_delete)},
  {0xbf9060,reinterpret_cast<void*>(&PoseRegistryReplace),reinterpret_cast<void**>(&g_pose_registry_replace)},
  {0xbf9620,reinterpret_cast<void*>(&PoseRouteClassUpdate),reinterpret_cast<void**>(&g_pose_route_class_update)},
  {0xbf91b0,reinterpret_cast<void*>(&PoseRouteClassRegister),reinterpret_cast<void**>(&g_pose_route_class_register)},
  {0xbf3a80,reinterpret_cast<void*>(&PoseCarryDispatch),reinterpret_cast<void**>(&g_pose_carry_dispatch)},
  {0x11268b0,reinterpret_cast<void*>(&PoseCarryRecipientMessage),reinterpret_cast<void**>(&g_pose_carry_recipient_message)},
  {0xfdea70,reinterpret_cast<void*>(&PoseQueueCtor),reinterpret_cast<void**>(&g_pose_queue_ctor)},
  {0xfdeb40,reinterpret_cast<void*>(&PoseQueueDestruct),reinterpret_cast<void**>(&g_pose_queue_destruct)},
  {0xfdeba0,reinterpret_cast<void*>(&PoseQueueDelete),reinterpret_cast<void**>(&g_pose_queue_delete)},
  {0xfdef60,reinterpret_cast<void*>(&PoseQueueInit),reinterpret_cast<void**>(&g_pose_queue_init)},
  {0xfdf010,reinterpret_cast<void*>(&PoseQueueAllocate),reinterpret_cast<void**>(&g_pose_queue_allocate)},
  {0xfe0260,reinterpret_cast<void*>(&PoseQueueRelease),reinterpret_cast<void**>(&g_pose_queue_release)},
  {0xfdffc0,reinterpret_cast<void*>(&PoseQueueFree),reinterpret_cast<void**>(&g_pose_queue_free)},
  {0x5c4910,reinterpret_cast<void*>(&PoseQueueAllocatorFree),reinterpret_cast<void**>(&g_pose_queue_allocator_free)},
  {0xfe00d0,reinterpret_cast<void*>(&PoseQueueSetter),reinterpret_cast<void**>(&g_pose_queue_setter)}
#ifdef MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER
  ,{0x9cb200,reinterpret_cast<void*>(&PoseActivateBody),reinterpret_cast<void**>(&g_pose_activate)}
  ,{0xb6c8f0,reinterpret_cast<void*>(&PoseActivationFactory),reinterpret_cast<void**>(&g_pose_activation_factory)}
#endif
 };

 constexpr auto hookCount=sizeof(hooks)/sizeof(hooks[0]);
 static_assert(hookCount==sizeof(g_pose_patches)/sizeof(g_pose_patches[0]),"every actual hook needs a prefix ownership receipt");
 static_assert(hookCount==groups::total,"preserve existing groups plus optional activation hooks");
 if(!groups::ValidateOrder([&](std::size_t n) noexcept {return hooks[n].rva;}))return false;
 if(!route::InitializeHookReceipts(hooks,g_pose_patches,
  [](std::uintptr_t rva,unsigned char (&bytes)[5]) noexcept {return PosePinnedPrefix(rva,bytes);},
  [](std::uintptr_t rva,void* bytes,std::size_t n) noexcept {return PoseReader(g_base+rva,bytes,n);})){g_pose_reason="pose_hook_prefix_changed";return false;}
 unsigned created=0;for(;created<hookCount;++created)
  if(MH_CreateHook(reinterpret_cast<void*>(g_base+hooks[created].rva),hooks[created].wrapper,hooks[created].original)!=MH_OK)break;
 if(created!=hookCount){for(unsigned i=0;i<created;++i)MH_RemoveHook(reinterpret_cast<void*>(g_base+hooks[i].rva));return false;}
 bool queued=true;for(const auto& hook:hooks)if(MH_QueueEnableHook(reinterpret_cast<void*>(g_base+hook.rva))!=MH_OK)queued=false;
 if(!queued||MH_ApplyQueued()!=MH_OK){
  for(const auto& hook:hooks){MH_DisableHook(reinterpret_cast<void*>(g_base+hook.rva));MH_RemoveHook(reinterpret_cast<void*>(g_base+hook.rva));}
  return false;
 }
 bool owned=true;
 for(auto& p:g_pose_patches){
  if(!PoseReader(g_base+p.rva,p.patch,5)||p.patch[0]!=0xe9){owned=false;break;}
  std::int32_t relative=0;std::memcpy(&relative,p.patch+1,4);
  p.relay=static_cast<std::uintptr_t>(static_cast<std::int64_t>(g_base+p.rva+5)+relative);
  if(!PosePatchOwned(p)){owned=false;break;}
 }
 if(!owned){
  for(const auto& hook:hooks){MH_DisableHook(reinterpret_cast<void*>(g_base+hook.rva));MH_RemoveHook(reinterpret_cast<void*>(g_base+hook.rva));}
  g_pose_reason="pose_hook_patch_ownership";return false;
 }
 g_pose_original_view.store(true,std::memory_order_release);
 g_pose_installed.store(true,std::memory_order_release);g_pose_reason="pose_window_installed";return true;
}

bool PoseLocalCarryOwned() noexcept {
 return t_local_carry.opened&&g_pose_gate.Owns(GetCurrentThreadId(),g_pose_generation,g_pose_epoch,t_local_carry.ticket);
}
bool PoseLocalCarryQuery(NativePoseLocalCarryContext& out) noexcept {
 out={};if(g_pose_bindings.enabled||!PoseLocalCarryOwned()||g_pose_fault||g_shutdown||!g_pose_test||!PoseRequired()||!PoseWorker()||
  GetCurrentThreadId()!=g_pose_thread||partner_defer::InBatch()||!(g_window_seq&1)||!g_pose_held||
  !(g_pose_owners.held&holds::carry)||(g_pose_owners.pending&holds::carry)||
  !PoseMessageCurrent()||!PoseVarsPatchesLive()||!NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||!PoseParticipantsLive())return false;
 const auto& expected=t_local_carry.context;
 if(expected.carryLease.serial!=g_pose_owners.carrySerial||expected.carryLease.generation!=g_pose_generation||
  expected.carryLease.sceneEpoch!=g_pose_epoch||expected.sequence!=static_cast<std::uint32_t>(g_window_seq)||!expected.sliceNonce)return false;
 cp::Pair current{};auto read=PoseReader;std::uint32_t real=0,selected=0;
 if(!cp::ReadPair(read,g_base,g_pose_player,current)||!cp::Same(current,g_pose_carry_pair)||!PoseCarryBirth0Current(current)||
  !PoseContextBirthCurrent(t_pose.context)||!PoseRead(current.player+0x214,real)||real||!PoseRead(current.player+0x218,selected)||selected||
  current.body[1].block!=t_pose.context.block||current.body[1].state!=t_pose.context.state||
  current.body[1].cs!=t_pose.context.modelCS||current.body[1].entry!=t_pose.context.modelEntry)return false;
 out=expected;out.image=g_base;out.player=current.player;out.go=current.go;out.data=current.data;
 out.block1=current.body[1].block;out.state1=current.body[1].state;out.modelCS1=current.body[1].cs;out.modelEntry1=current.body[1].entry;
 out.armGeneration=g_pose_generation;out.armEpoch=g_pose_epoch;out.thread=g_pose_thread;
 out.birth1=g_pose_bound_birth;out.carrierBirth0=g_pose_carry_birth0;out.carrySerial=g_pose_owners.carrySerial;
 out.realLocal=real;out.selected=selected;out.sleepHeld=(g_pose_owners.held&holds::sleep)!=0&&(g_pose_owners.pending&holds::sleep)==0;
 return true;
}
bool PoseParticipantsLive() noexcept {
 if(g_pose_bindings.enabled)return g_pose_installed;
 if(!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||!g_wrap_ready||
    !partner_defer::H1Installed()||!partner_defer::H2Installed()||!partner_defer::Armed()||
    partner_defer::CurrentMode()!=partner_defer::Mode::on||partner_defer::ShuttingDown()||
    !PatchLive(reinterpret_cast<const void*>(g_base))||!partner_defer::PatchLive(reinterpret_cast<const void*>(g_base))||
    !partner_local_detour::Installed()||!partner_local_detour::PatchLive(reinterpret_cast<const void*>(g_base)))return false;
 for(const auto& p:g_pose_patches)if(!PosePatchOwned(p))return false;
 const struct {std::size_t offset;std::uintptr_t wrapper;} slots[]={
  {c::svc_vt_active,reinterpret_cast<std::uintptr_t>(&WrapSvcActive)},
  {c::svc_vt_all,reinterpret_cast<std::uintptr_t>(&WrapSvcAll)},
  {c::svc_vt_id,reinterpret_cast<std::uintptr_t>(&WrapSvcId)}};
 for(const auto& slot:slots){std::uintptr_t got=0;if(!PoseRead(g_base+c::svc_vtable_rva+slot.offset,got)||got!=slot.wrapper)return false;}
 const struct {std::uintptr_t site,stub,wrapper;} calls[]={
  {c::walk7_site,reinterpret_cast<std::uintptr_t>(g_walk7_stub),reinterpret_cast<std::uintptr_t>(&WrapWalk7)},
  {c::walk11_site,reinterpret_cast<std::uintptr_t>(g_walk11_stub),reinterpret_cast<std::uintptr_t>(&WrapWalk11)}};
 for(const auto& call:calls){
  unsigned char bytes[5]{},stub[14]{};std::int32_t rel=0;std::uintptr_t target=0;
  if(!call.stub||!PoseReader(g_base+call.site,bytes,5)||bytes[0]!=0xe8)return false;
  std::memcpy(&rel,bytes+1,4);
  const auto to=static_cast<std::uintptr_t>(static_cast<std::int64_t>(g_base+call.site+5)+rel);
  if(to!=call.stub||!PoseReader(to,stub,14)||stub[0]!=0xff||stub[1]!=0x25||stub[2]||stub[3]||stub[4]||stub[5])return false;
  std::memcpy(&target,stub+6,8);if(target!=call.wrapper)return false;
 }
 // Revalidate the dispatch stub target in addition to its existing rel32 test.
 unsigned char original[4]{};return PoseOriginalCode(g_base+c::hook_site+1,original,sizeof(original));
}
