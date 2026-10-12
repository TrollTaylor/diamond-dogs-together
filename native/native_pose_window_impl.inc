// Included inside partner_input_hook's anonymous namespace after GuardedCall.
// Actual worker-only hooks and exclusion, not a gameplay activation policy.
namespace px=native_pose_exclusion;
namespace cw=camera_mode_window;
namespace birth=native_pose_birth;
namespace sx=native_pose_selected_export;
namespace pcam=native_pose_camera_reader;
namespace holds=native_pose_hold_owners;
namespace cp=native_pose_carry_participants;
SRWLOCK g_pose_birth_lock=SRWLOCK_INIT;
birth::Receipt g_pose_birth{},g_pose_birth0{};std::uint64_t g_pose_birth_serial=0,g_pose_birth_epoch=1,g_pose_bound_birth=0;
px::Gate g_pose_gate;
std::atomic<bool> g_pose_installed{false},g_pose_original_view{false};bool g_pose_test=false;
volatile LONG g_pose_required=0,g_pose_fault=0,g_pose_held=0,g_pose_clear=0;
volatile LONG g_pose_windows=0,g_pose_suppressed=0,g_pose_hints=0,g_pose_local_exports=0;
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
using PoseConstructFn=void*(*)(void*,void*,void*);
using PoseDeleteFn=void*(*)(void*,unsigned);
using PoseRebindFn=void(*)(void*,std::uint32_t);
using PoseRetireFn=void(*)(void*,std::uint32_t,std::uint8_t);
using PoseHintFn=void(*)(void*,std::uint16_t,std::uint32_t,std::uint8_t,std::uint8_t,std::uint8_t,std::uint8_t);
PoseSelectedExportFn g_pose_selected_export=nullptr;PoseUpdateFn g_pose_cam_frame=nullptr;
std::uintptr_t g_pose_camera_stub=0;
PoseUpdateFn g_pose_update=nullptr,g_pose_destroy_block=nullptr,g_pose_destroy_entry=nullptr;
PoseDeleteFn g_pose_destroy_cs=nullptr;PoseHintFn g_pose_hint=nullptr;
PoseDeleteFn g_pose_delete_player=nullptr;PoseUpdateFn g_pose_reset_player=nullptr;
PoseRetireFn g_pose_retire_body=nullptr;
PoseConstructFn g_pose_construct=nullptr;
PoseRebindFn g_pose_rebind=nullptr;PoseUpdateFn g_pose_parts=nullptr;
bool PoseRequired() noexcept {return InterlockedCompareExchange(&g_pose_required,0,0)!=0;}
void PoseFail(const char* why) noexcept {
 const bool first=InterlockedCompareExchange(&g_pose_fault,1,0)==0;
 if(first)g_pose_reason=why;g_pose_gate.StopNewWriters();if(!first)return;
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
 s.windowSequence=static_cast<std::uint32_t>(g_window_seq);return s;
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
bool PoseRecordBirth(const birth::Completion& c,unsigned actualId=1) noexcept {
 AcquireSRWLockExclusive(&g_pose_birth_lock);++g_pose_birth_serial;if(!g_pose_birth_serial)++g_pose_birth_serial;
 const auto receipt=birth::Complete(c,g_pose_birth_serial,g_pose_birth_epoch);if(receipt.ready&&actualId<=1)(actualId?g_pose_birth:g_pose_birth0)=receipt;
 ReleaseSRWLockExclusive(&g_pose_birth_lock);return receipt.ready;
}
void PoseInvalidateBirth(std::uintptr_t at,unsigned kind) noexcept {
 AcquireSRWLockExclusive(&g_pose_birth_lock);++g_pose_birth_epoch;if(!g_pose_birth_epoch)++g_pose_birth_epoch;
 if(birth::InvalidatedBy(g_pose_birth,at,kind))g_pose_birth={};
 if(birth::InvalidatedBy(g_pose_birth0,at,kind))g_pose_birth0={};
 ReleaseSRWLockExclusive(&g_pose_birth_lock);
}
bool PoseBirthCurrent(const pi::Arm& a,const NativePoseJobContext& c0,bool bind=false) noexcept {
 AcquireSRWLockShared(&g_pose_birth_lock);const auto saved=g_pose_birth;ReleaseSRWLockShared(&g_pose_birth_lock);
 const bool current=birth::Current(saved,{a.P,a.blk1,a.ps1,c0.modelCS,c0.modelEntry});
 if(current&&bind)g_pose_bound_birth=saved.serial;
 return current&&(bind||saved.serial==g_pose_bound_birth);
}
bool PoseContextBirthCurrent(const NativePoseJobContext& c0) noexcept {
 pi::Arm a{};a.P=c0.player;a.blk1=c0.block;a.ps1=c0.state;return PoseBirthCurrent(a,c0);
}
bool PoseBegin(const pi::Arm* a) noexcept {
 if(!PoseRequired()){InterlockedIncrement(&g_window_seq);return true;}
 if(!a||t_pose.opened||g_pose_fault||g_shutdown||!PoseWorker()||GetCurrentThreadId()!=g_pose_thread||
    a->P!=g_pose_player||a->go!=g_pose_go||a->root1!=g_pose_root||a->blk1!=g_pose_block||
    !a->gen||a->rates_epoch!=g_pose_epoch||!a->enabled||!a->native||
    a->cam!=PartnerInputCam::sandbox||a->holdlog||partner_defer::InBatch()||(g_window_seq&1)){
  InterlockedIncrement(&g_pose_suppressed);g_pose_reason="pose_window_admission";return false;
 }
 if(!PoseParticipantsLive()){PoseFail("pose_participant_patch_drift");return false;}
 if(!g_pose_bindings.enabled&&!NativePoseWholePlayerLifetimeLive()){PoseFail("pose_whole_player_lifetime_missing");return false;}
 px::Admission admission{true,true,true,true,true,true,g_pose_generation,g_pose_epoch,g_pose_thread};
 if(g_pose_gate.TryWrite(admission,t_pose.writer)!=px::Status::entered){
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
  g_pose_reason=result.reason;InterlockedIncrement(&g_pose_suppressed);
  InterlockedIncrement(&g_window_seq);t_pose.opened=false;
  if(!g_pose_gate.EndWrite(g_pose_thread,g_pose_generation,g_pose_epoch,true,true,t_pose.writer))PoseFail("pose_capture_cleanup");
  return false;
 }
 t_pose.captured=true;g_pose_reason="pose_window_captured";return true;
}
void PoseEnd(bool postComplete) noexcept {
 if(!t_pose.opened){InterlockedIncrement(&g_window_seq);return;}
 bool restored=false;cw::Reply receipt{};
 auto read=PoseReader;auto sample=[]() noexcept {return PoseScope();};
 auto write=[](std::uintptr_t at,std::uint8_t before,std::uint8_t after) noexcept {
  if(!PoseOwned())return cw::WriteStatus::fault;
  __try {return static_cast<std::uint8_t>(_InterlockedCompareExchange8(reinterpret_cast<volatile char*>(at),
      static_cast<char>(after),static_cast<char>(before)))==before?cw::WriteStatus::written:cw::WriteStatus::conflict;}
  __except(EXCEPTION_EXECUTE_HANDLER){return cw::WriteStatus::fault;}
 };
 if(t_pose.captured)restored=cw::Restore(read,write,sample,t_pose.c0,receipt);
 const bool clean=postComplete&&restored&&!g_pose_fault;
 InterlockedIncrement(&g_window_seq);const auto context=t_pose.context;t_pose.opened=false;
 const bool released=g_pose_gate.EndWritePublish(g_pose_thread,g_pose_generation,g_pose_epoch,clean,true,t_pose.writer,[]()noexcept{
  if(g_pose_clear){
   const bool carryClosed=holds::Closed(g_pose_owners,true);
   InterlockedExchange(&g_pose_held,g_pose_owners.held?1:0);InterlockedExchange(&g_pose_clear,0);
   if(!g_pose_owners.held)g_pose_cs=g_pose_entry=0;
   if(carryClosed){g_pose_carry_pair={};g_pose_carry_birth0=0;}
  }
 });
 t_pose.cleanup=clean&&released;
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
 const bool accepted=!g_pose_before||g_pose_before(t_pose.context);
 return accepted&&!g_pose_fault;
}
bool PoseCarryBirth0Current(const cp::Pair& pair,bool bind=false) noexcept {
 AcquireSRWLockShared(&g_pose_birth_lock);const auto saved=g_pose_birth0;ReleaseSRWLockShared(&g_pose_birth_lock);
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
 if(g_pose_fault)return false;if(PoseOwned()&&!PoseCarryPairCurrent()){PoseFail("pose_carry_pair_changed");return false;}if(!PoseOwned()||!g_pose_after)return true;return g_pose_after(t_pose.context)&&!g_pose_fault;
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
  const bool filtered=matched&&pcam::SelectedReplica(reader,scope,g_base,caller,incomingBody,t_pose.context.player,t_pose.context.block,
      reinterpret_cast<std::uintptr_t>(object),senderPrimary,source,&stage);
  InterlockedExchange(&g_pose_cam_sender,filtered?1:0);
  const LONG filteredCall=filtered?InterlockedIncrement(&g_pose_cam_filtered):0;
  const LONG refusedCall=filtered?0:InterlockedIncrement(&g_pose_cam_refused);
  if(((filtered&&filteredCall==1)||(!filtered&&refusedCall==1))&&g_log){
   char line[384]{};
   if(SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),
      "pose_camera=%s; caller_rva=0x%08x; typed=%u; real=%u; selected=%u; target380=%u; selected384=%u; mode20c=%u; sequence=%u; odd=%u; consumer=%u; body_known=%u; incoming_body=%u; sender=%u; stage=%s\r\n",
      filtered?"selected_replica_filtered":"owned_refusal",within?static_cast<unsigned>(caller-g_base):0u,matched?1u:0u,
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
void PoseUpdate(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);g_pose_update(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
bool PoseDestroyRefused(std::uintptr_t object,unsigned kind) noexcept {
 PoseInvalidateBirth(object,kind);
 if(!PoseRequired()||!g_pose_held)return false;
 const bool own=(kind==0?object==g_pose_block:kind==1?object==g_pose_cs:kind==2?object==g_pose_entry:object==g_pose_go)||
     ((g_pose_owners.held&holds::carry)&&cp::MatchesTeardown(g_pose_carry_pair,object,kind));
 if(own){PoseFail("pose_live_model_teardown");return true;}return false;
}
// These entries execute before native whole-owner release. Child block guards
// alone cannot retain Q/message/controllers or the enclosing GO allocation.
void PoseInvalidateWholeBirth(std::uintptr_t go) noexcept {
 AcquireSRWLockExclusive(&g_pose_birth_lock);
 whole_player_lifetime_checks::InvalidateWhole(g_pose_birth,g_pose_birth_epoch,go);
 if(g_pose_birth0.ready&&g_pose_birth0.identity.player>=0x20&&g_pose_birth0.identity.player-0x20==go)g_pose_birth0={};
 ReleaseSRWLockExclusive(&g_pose_birth_lock);
}
bool PoseWholePlayerRefused(std::uintptr_t go) noexcept {
 // The shared reader must already be held: otherwise a writer could publish a
 // held lease between an early test and the original's first mutation.
 const bool retained=PoseRequired()&&g_pose_held&&go==g_pose_go;
 PoseInvalidateWholeBirth(go);
 if(retained){PoseFail("pose_live_whole_player_teardown");return true;}
 return false;
}
void* PoseDeletePlayer(void* object,unsigned flags) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return object;}
 void* result=object;
 __try {SetLastError(incoming);if(!PoseWholePlayerRefused(reinterpret_cast<std::uintptr_t>(object)))result=g_pose_delete_player(object,flags);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
void PoseResetPlayer(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseWholePlayerRefused(reinterpret_cast<std::uintptr_t>(object)))g_pose_reset_player(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
void PoseDestroyBlock(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),0))g_pose_destroy_block(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
void* PoseDestroyCs(void* object,unsigned flags) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return object;}void* result=object;
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),1))result=g_pose_destroy_cs(object,flags);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}return result;
}
void PoseRetireBody(void* object,std::uint32_t id,std::uint8_t callerControl) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),0))g_pose_retire_body(object,id,callerControl);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
void PoseDestroyEntry(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),2))g_pose_destroy_entry(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
void PoseRebindBody(void* object,std::uint32_t id) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),0))g_pose_rebind(object,id);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
}
void PoseApplyParts(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return;}
 __try {SetLastError(incoming);if(!PoseDestroyRefused(reinterpret_cast<std::uintptr_t>(object),3))g_pose_parts(object);outgoing=GetLastError();}
 __finally {PoseExitShared(read);SetLastError(outgoing);}
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
 SetLastError(incoming);g_pose_hint(ui,hint,priority,flag,enable,extra6,extra7);
}
struct PosePatch {
 std::uintptr_t rva=0,wrapper=0,relay=0;unsigned char original[5]{},patch[5]{};
};
PosePatch g_pose_patches[13]{};
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
bool PoseOriginalCode(std::uintptr_t at,void* out,std::size_t size) noexcept {
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
bool PosePinnedPrefix(std::uintptr_t rva,unsigned char (&out)[5]) noexcept {
 for(const auto& s:native_pose_birth_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:camera_mode_window_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:native_pose_window_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:carry_model_lifetime_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
 for(const auto& s:carry_model_retirement_contract::sites)if(s.rva==rva&&s.size>=5){std::memcpy(out,s.bytes,5);return true;}
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
 if(!cw::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_code_c0";return false;}
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
 if(!sx::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_selected_export_code_or_slot";return false;}
 if(!pcam::CodeGate(reader,g_base,span,mismatch)){g_pose_reason="pose_camera_reader_code_or_slot";return false;}
 const auto checkSites=[&](const auto& sites)noexcept {
  for(const auto& s:sites){if(s.rva>span||s.size>span-s.rva)return false;
   for(std::size_t off=0;off<s.size;){const auto n=(s.size-off)<sizeof(bytes)?s.size-off:sizeof(bytes);
    if(!reader(g_base+s.rva+off,bytes,n)||std::memcmp(bytes,s.bytes+off,n))return false;off+=n;}
  }return true;
 };
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
  {0x111cba0,reinterpret_cast<void*>(g_pose_camera_stub),reinterpret_cast<void**>(&g_pose_cam_frame)}};
 for(unsigned i=0;i<13;++i){
  auto& p=g_pose_patches[i];p.rva=hooks[i].rva;p.wrapper=reinterpret_cast<std::uintptr_t>(hooks[i].wrapper);
  unsigned char current[5]{};
  if(!PosePinnedPrefix(p.rva,p.original)||!PoseReader(g_base+p.rva,current,5)||std::memcmp(current,p.original,5)){
   g_pose_reason="pose_hook_prefix_changed";return false;
  }
 }
 unsigned created=0;for(;created<13;++created)
  if(MH_CreateHook(reinterpret_cast<void*>(g_base+hooks[created].rva),hooks[created].wrapper,hooks[created].original)!=MH_OK)break;
 if(created!=13){for(unsigned i=0;i<created;++i)MH_RemoveHook(reinterpret_cast<void*>(g_base+hooks[i].rva));return false;}
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
