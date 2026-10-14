// Included after PoseModel/PoseBirthEpoch. Private OFF-by-default observation;
// all native originals run exactly once with their unchanged actual arguments.
namespace activation=native_pose_activation;
activation::Pending g_pose_activation_pending[2]{};
activation::Receipt g_pose_activation_receipt[2]{};
volatile LONG g_activation_enter=0,g_activation_factory=0,g_activation_staged=0,g_activation_published=0;
volatile LONG g_activation_noop=0,g_activation_refused=0,g_activation_invalidation_entries=0;
volatile LONG g_activation_invalidated=0,g_activation_abnormal=0;
// Passive last-event stage: 1 inactive,2 unknown caller,3 unframed,4 identity,
// 5 native false,6 factory/transition refused,7 AL-true no-op,8 pending,
// 9 final closure refused,10 published,11 witness replaced,12 abnormal.
volatile LONG g_activation_last_body=-1,g_activation_last_caller=0,g_activation_last_stage=0;
birth::Receipt PoseObservedBirthLocked(unsigned id) noexcept {
 const auto constructor=id?g_pose_birth:g_pose_birth0;
#ifdef MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER
 return activation::Effective(constructor,g_pose_activation_receipt[id]);
#else
 return constructor;
#endif
}
void PoseInvalidateActivationLocked(std::uintptr_t at,unsigned kind) noexcept {
#ifdef MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER
 InterlockedIncrement(&g_activation_invalidation_entries);
 for(unsigned id=0;id<2;++id)
  if(activation::Invalidate(g_pose_activation_pending[id],g_pose_activation_receipt[id],at,kind))
   InterlockedIncrement(&g_activation_invalidated);
#else
 (void)at;(void)kind;
#endif
}
#ifdef MGS_LOCAL_NATIVE_ACTIVATION_OBSERVER
using PoseActivateFn=std::uint8_t(*)(void*,std::uint32_t,std::uint32_t,std::uint32_t,
 std::uint32_t,std::uint32_t,std::uint32_t,void*);
using PoseActivationFactoryFn=std::int32_t*(*)(void*,std::int32_t*,void*);
PoseActivateFn g_pose_activate=nullptr;PoseActivationFactoryFn g_pose_activation_factory=nullptr;
bool PoseActivationHooksLive() noexcept;
struct ActivationPartsFrame {std::uintptr_t go=0;std::uint32_t thread=0;std::uint64_t serial=0;bool original=false;unsigned stagedMask=0;};
thread_local ActivationPartsFrame t_activation_parts;
struct ActivationFactoryFrame {activation::ActivationCompletion* completion=nullptr;std::uint32_t thread=0;};
thread_local ActivationFactoryFrame t_activation_factory;
volatile LONG64 g_activation_frame_serial=0;std::uint64_t g_activation_operation_serial=0;
bool PoseActivationEligible() noexcept {
 native_work_timing::Scope measured(native_work_timing::Kind::activation_eligible);
 return !g_pose_bindings.enabled&&g_pose_installed&&g_pose_original_view.load(std::memory_order_acquire)&&
  !g_pose_fault&&!g_shutdown&&PoseWorker()&&PoseActivationHooksLive();
}
// Canonical native Player/body pool/PS/CS/row identity. Readability alone is
// insufficient and this helper never publishes any lifetime evidence.
bool PoseActivationIdentity(std::uintptr_t block,unsigned id,birth::Identity& out,
 std::uintptr_t& row,bool& bit14) noexcept {
 out={};row=0;bit14=false;if(id>1||!block)return false;
 std::uintptr_t player=0,ps=0,canonical=0,vt=0,ids=0,pools=0,blocks=0,table=0,mapped=0,rows=0;
 std::uint32_t count=0,base=0,flags=0;std::uint16_t actual=UINT16_MAX;std::uint8_t pool=255;
 if(!PoseRead(block+0x138,player)||!player||player<0x20||!PoseRead(block+0x78,ps)||!ps||
  !PoseRead(g_base+c::player_slot_rva,canonical)||canonical!=player||
  !PoseRead(player,vt)||vt!=g_base+0x2295490||!PoseRead(player+0x204,flags)||!(flags&1u)||(flags&0x100u)||
  !PoseRead(player+0x208,count)||count!=2||!PoseRead(player+0x70,ids)||!ids||
  !PoseRead(player+0x78,pools)||!pools||!PoseRead(ids+id*2,actual)||actual!=id||
  !PoseRead(pools+id,pool)||pool>=16||!PoseRead(player+0x378,blocks)||!blocks||
  block!=blocks+std::uintptr_t(pool)*0x6a8||!PoseRead(player+0x50,table)||!table||
  !PoseRead(table+pool*8,mapped)||mapped!=block||!PoseRead(block+8,count)||count!=1||
  !PoseRead(block+0xc,base)||base!=id||!PoseRead(ps+8,base)||base!=id)return false;
 pi::Arm arm{};arm.P=player;arm.blk1=block;arm.ps1=ps;NativePoseJobContext model{};
 if(!PoseModel(arm,model,nullptr,id)||!PoseRead(model.modelCS+8,count)||count!=1||
  !PoseRead(model.modelCS+0xc,base)||base!=id||!PoseRead(ps+0x280,rows)||!rows||
  !cp::Range(rows,0x20)||!PoseRead(rows+0x10,flags))return false;
 out={player,block,ps,model.modelCS,model.modelEntry};row=rows;bit14=(flags&(1u<<14))!=0;return true;
}
struct ActivationNativeResource {std::uintptr_t actor=0,trees=0,tree=0;std::uint8_t index=0,count=0;};
bool PoseActivationResource(const cp::Body& body,ActivationNativeResource& out) noexcept {
 out={};std::uintptr_t owner=0;
 // These are actual B6C8F0 inputs/backlinks, not invented actor type tags.
 return PoseRead(body.entry+0x58,out.actor)&&out.actor&&
  PoseRead(out.actor+0x48,out.trees)&&out.trees&&PoseRead(out.actor+0x59,out.count)&&out.count&&
  PoseRead(out.actor+0x6a,out.index)&&PoseRead(out.trees+std::uintptr_t(out.index)*8,out.tree)&&out.tree&&
  PoseRead(out.tree+0x60,owner)&&owner==body.entry;
}
bool PoseSameActivationResource(const ActivationNativeResource& a,const ActivationNativeResource& b) noexcept {
 return a.actor==b.actor&&a.trees==b.trees&&a.tree==b.tree&&a.index==b.index&&a.count==b.count;
}
bool PoseActivationFinal(const activation::Pending& pending,activation::FinalCompletion& out) noexcept {
 out={};const auto& a=pending.completion;
 if(!pending.ready||!t_activation_parts.original||t_activation_parts.serial!=a.frame||
  t_activation_parts.go!=a.partsGo||t_activation_parts.thread!=a.threadAfter||!PoseActivationEligible())return false;
 birth::Identity first{},second{};std::uintptr_t row0=0,row1=0,parts=0;bool bit0=false,bit1=false;
 std::uint8_t status0=0,status1=0;cp::Body model0{},model1{};ActivationNativeResource resource0{},resource1{};
 auto reader=PoseReader;
 if(!PoseActivationIdentity(a.after.block,a.body,first,row0,bit0)||!birth::Same(first,a.after)||
  row0!=a.rowAfter||!bit0||!PoseRead(first.player+0x90,parts)||!parts||
  !PoseRead(parts+a.body,status0)||status0!=3||
  !cp::ReadModel(reader,g_base,first.player,first.block,a.body,model0)||!PoseActivationResource(model0,resource0)||
  !cp::ReadModel(reader,g_base,first.player,first.block,a.body,model1)||!PoseActivationResource(model1,resource1)||
  !cp::SameBody(model0,model1)||!cp::CoherentCallbacks(model0,model1)||
  !PoseSameActivationResource(resource0,resource1)||!PoseActivationIdentity(a.after.block,a.body,second,row1,bit1)||
  !birth::Same(first,second)||row0!=row1||!bit1||!PoseRead(parts+a.body,status1)||status1!=3)return false;
 out.identity=second;out.row=row1;out.partsGo=t_activation_parts.go;out.frame=t_activation_parts.serial;out.operation=a.operation;
 out.thread=GetCurrentThreadId();out.originalPartsReturned=true;out.installed=true;out.worker=PoseWorker();
 out.parts3=true;out.bit14=true;out.pairedNativeResourceClosure=true;return true;
}
void PoseActivationFinishFrame(bool normal) noexcept {
 native_work_timing::Scope measured(native_work_timing::Kind::activation_final);
 for(unsigned id=0;id<2;++id){activation::Pending pending{};
  if(!(t_activation_parts.stagedMask&(1u<<id)))continue;
  AcquireSRWLockShared(&g_pose_birth_lock);pending=g_pose_activation_pending[id];ReleaseSRWLockShared(&g_pose_birth_lock);
  if(!pending.ready||pending.completion.frame!=t_activation_parts.serial)continue;
  activation::FinalCompletion final{};const bool qualified=normal&&PoseActivationFinal(pending,final);
  AcquireSRWLockExclusive(&g_pose_birth_lock);
  const auto& current=g_pose_activation_pending[id];
  const bool same=current.ready&&current.completion.frame==pending.completion.frame&&current.completion.operation==pending.completion.operation&&
   birth::Same(current.completion.after,pending.completion.after)&&current.completion.rowAfter==pending.completion.rowAfter;
  if(qualified&&same){
   const auto next=g_pose_birth_serial+1?g_pose_birth_serial+1:1;
   const auto receipt=activation::Complete(current,final,next);
   if(receipt.ready){g_pose_birth_serial=next;g_pose_activation_receipt[id]=receipt;InterlockedIncrement(&g_activation_published);InterlockedExchange(&g_activation_last_stage,10);}
   else InterlockedIncrement(&g_activation_refused);
  }else if(same){InterlockedIncrement(&g_activation_refused);InterlockedExchange(&g_activation_last_stage,9);}
  else InterlockedExchange(&g_activation_last_stage,11);
  activation::DiscardFrame(g_pose_activation_pending[id],t_activation_parts.serial);
  ReleaseSRWLockExclusive(&g_pose_birth_lock);
 }
}
void PoseActivationPartsOriginal(void* object) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;bool normal=false;
 auto measured=native_work_timing::Begin(native_work_timing::Kind::activation_parts);
 const auto previous=t_activation_parts;const auto previousFactory=t_activation_factory;
 t_activation_parts={};t_activation_factory={};
 // Parts polling is frequent; full-body gates run only at installation and
 // owned head/relay + typed readers only at actual activation/finalization.
 // This frame describes a natural original call and grants no authority.
 // Fresh worker admission and actual hook ownership are required separately
 // at activation entry/exit/finalization; avoid their cost on ordinary polls.
 if(!previous.original&&!g_pose_bindings.enabled&&g_pose_installed&&
  g_pose_original_view.load(std::memory_order_acquire)&&!g_pose_fault&&!g_shutdown){
  auto serial=static_cast<std::uint64_t>(InterlockedIncrement64(&g_activation_frame_serial));
  if(!serial)serial=static_cast<std::uint64_t>(InterlockedIncrement64(&g_activation_frame_serial));
  t_activation_parts={reinterpret_cast<std::uintptr_t>(object),GetCurrentThreadId(),serial,true};
 }
 __try {SetLastError(incoming);g_pose_parts(object);outgoing=GetLastError();normal=true;}
 __finally {
  if(t_activation_parts.original){if(!normal)InterlockedIncrement(&g_activation_abnormal);PoseActivationFinishFrame(normal);}
  t_activation_parts=previous;t_activation_factory=previousFactory;SetLastError(outgoing);native_work_timing::End(measured);
 }
}
__declspec(noinline) std::int32_t* PoseActivationFactory(void* entry,std::int32_t* output,void* payload) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;bool normal=false;std::int32_t* result=nullptr;
 auto* completion=t_activation_factory.completion;
 const bool candidate=completion&&t_activation_factory.thread==GetCurrentThreadId()&&
  completion->after.entry==reinterpret_cast<std::uintptr_t>(entry);
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 if(candidate)++completion->factoryCalls;
 __try {SetLastError(incoming);result=g_pose_activation_factory(entry,output,payload);outgoing=GetLastError();normal=true;}
 __finally {
  if(candidate){std::int32_t value=-1;
   completion->factoryEntry=reinterpret_cast<std::uintptr_t>(entry);
   completion->exactFactoryCaller=caller==g_base+native_pose_activation_contract::factoryReturn;
   completion->factoryNormal=normal;completion->factoryOutputIdentity=result==output;
   completion->factoryOutputZero=normal&&result==output&&PoseRead(reinterpret_cast<std::uintptr_t>(output),value)&&value==0;
   if(completion->exactFactoryCaller&&completion->factoryOutputZero)InterlockedIncrement(&g_activation_factory);
  }SetLastError(outgoing);
 }return result;
}
__declspec(noinline) std::uint8_t PoseActivateBody(void* block,std::uint32_t id,std::uint32_t control3,
 std::uint32_t control4,std::uint32_t control5,std::uint32_t control6,std::uint32_t control7,void* opaque8) noexcept {
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uint8_t result=0;bool normal=false;
 InterlockedIncrement(&g_activation_enter);activation::ActivationCompletion completion{};
 const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());const auto previous=t_activation_factory;
 InterlockedExchange(&g_activation_last_body,id<=1?static_cast<LONG>(id):-1);
 InterlockedExchange(&g_activation_last_caller,caller>=g_base&&caller-g_base<0xa01d000?static_cast<LONG>(caller-g_base):0);
 completion.body=id;completion.threadBefore=GetCurrentThreadId();
 completion.partsGo=t_activation_parts.go;completion.frame=t_activation_parts.serial;
 completion.exactActivationCaller=caller==g_base+native_pose_activation_contract::activationReturn;
 const bool framed=native_observer_negative_work::ActivationCandidate(completion.exactActivationCaller,
  t_activation_parts.original,t_activation_parts.thread==completion.threadBefore,id);
 completion.lifecycleEpoch=framed?PoseBirthEpoch():0;
 completion.installedBefore=framed&&PoseActivationEligible();completion.workerBefore=completion.installedBefore;
 const bool candidate=framed&&completion.installedBefore&&PoseActivationIdentity(reinterpret_cast<std::uintptr_t>(block),id,
   completion.before,completion.rowBefore,completion.bit14Before)&&completion.partsGo==completion.before.player-0x20;
 InterlockedExchange(&g_activation_last_stage,!completion.installedBefore?1:!completion.exactActivationCaller?2:!t_activation_parts.original?3:!candidate?4:6);
 if(candidate){AcquireSRWLockExclusive(&g_pose_birth_lock);++g_activation_operation_serial;if(!g_activation_operation_serial)++g_activation_operation_serial;
  completion.operation=g_activation_operation_serial;ReleaseSRWLockExclusive(&g_pose_birth_lock);}
 completion.after=completion.before;t_activation_factory={candidate?&completion:nullptr,completion.threadBefore};
 __try {SetLastError(incoming);result=g_pose_activate(block,id,control3,control4,control5,control6,control7,opaque8);
  outgoing=GetLastError();normal=true;}
 __finally {
  t_activation_factory=previous;completion.normalReturn=normal;completion.alTrue=result==1;
  completion.threadAfter=GetCurrentThreadId();
  const bool inspectAfter=native_observer_negative_work::ActivationAfter(candidate,normal,completion.alTrue,completion.factoryCalls);
  completion.installedAfter=inspectAfter&&PoseActivationEligible();completion.workerAfter=completion.installedAfter;
  if(candidate&&normal&&completion.alTrue&&!completion.factoryCalls){
   // No factory witness can publish a resource birth. Do not pretend this is
   // a qualified post-call no-op or synthesize a new identity/receipt.
   InterlockedExchange(&g_activation_last_stage,13);
   auto noWitness=native_work_timing::Begin(native_work_timing::Kind::activation_no_witness);native_work_timing::End(noWitness);
  }else if(inspectAfter&&PoseActivationIdentity(reinterpret_cast<std::uintptr_t>(block),id,
   completion.after,completion.rowAfter,completion.bit14After)){
   AcquireSRWLockExclusive(&g_pose_birth_lock);
   const auto pending=activation::Stage(completion,g_pose_birth_epoch);
   if(pending.ready){g_pose_activation_pending[id]=pending;t_activation_parts.stagedMask|=1u<<id;InterlockedIncrement(&g_activation_staged);InterlockedExchange(&g_activation_last_stage,8);}
   else if(completion.bit14Before){InterlockedIncrement(&g_activation_noop);InterlockedExchange(&g_activation_last_stage,7);}
   else InterlockedIncrement(&g_activation_refused);
   ReleaseSRWLockExclusive(&g_pose_birth_lock);
  }else if(candidate){if(!normal)InterlockedIncrement(&g_activation_abnormal);InterlockedIncrement(&g_activation_refused);InterlockedExchange(&g_activation_last_stage,normal?5:12);}
  SetLastError(outgoing);
 }return result;
}
#else
inline void PoseActivationPartsOriginal(void* object) noexcept {g_pose_parts(object);}
#endif
