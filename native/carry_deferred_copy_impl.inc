// Included after actual Q lifetime implementation, inside core anonymous namespace.
namespace dcopy=carry_deferred_copy;
using PoseQueueSetterFn=void*(*)(void*,std::uint32_t,const void*);
PoseQueueSetterFn g_pose_queue_setter=nullptr;
std::uintptr_t g_pose_memcpy_expected=0;
NativePoseDeferredCarryValidator g_pose_deferred_sleep_current=nullptr;
dcopy::Ledger g_pose_deferred_copy{};
struct PoseDeferredCopyTls {std::uint64_t intent=0;std::uint32_t thread=0,sequence=0;bool active=false;};
thread_local PoseDeferredCopyTls t_deferred_copy;
void PoseDeferredPublishClear()noexcept{g_pose_deferred_copy={};}
bool PoseDeferredCode(std::uintptr_t& mismatch)noexcept{
 mismatch=0;unsigned char bytes[128]{};
 for(const auto& s:carry_native_queue_contract::sites){if(s.rva>0xa01d000||s.size>0xa01d000-s.rva||s.size>sizeof(bytes)||!PoseOriginalCode(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}}
 for(const auto& s:carry_native_queue_contract::slots){std::uintptr_t got=0;if(s.rva>0xa01d000||8>0xa01d000-s.rva||!PoseRead(g_base+s.rva,got)||got!=g_base+s.target){mismatch=s.rva;return false;}}
 return true;
}
bool PoseDeferredImportCurrent()noexcept{
 std::uintptr_t got=0;return g_pose_memcpy_expected&&PoseRead(g_base+carry_native_queue_contract::memcpyIatRva,got)&&got==g_pose_memcpy_expected;
}
bool PoseDeferredPinImport()noexcept{
 const auto module=GetModuleHandleW(L"MSVCR110.dll");if(!module)return false;
 const auto expected=reinterpret_cast<std::uintptr_t>(GetProcAddress(module,"memcpy"));std::uintptr_t got=0;HMODULE pinned=nullptr;
 if(!expected||!PoseRead(g_base+carry_native_queue_contract::memcpyIatRva,got)||got!=expected||
  !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(expected),&pinned)||pinned!=module)return false;
 g_pose_memcpy_expected=expected;return true;
}
bool PoseDeferredPatchesLive()noexcept{
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_queue_setter||!g_pose_original_view.load(std::memory_order_acquire)||
  g_pose_patches[groups::deferredFirst].rva!=carry_native_queue_contract::setterRva||!PosePatchOwned(g_pose_patches[groups::deferredFirst]))return false;
 std::uintptr_t mismatch=0;return PoseDeferredCode(mismatch)&&PoseDeferredImportCurrent()&&PoseQueuePatchesLive()&&NativePoseCarrySenderHooksLive();
}
bool PoseDeferredSameJob(const NativePoseJobContext& a,const NativePoseJobContext& b)noexcept{
 return a.player==b.player&&a.go==b.go&&a.block==b.block&&a.state==b.state&&a.modelCS==b.modelCS&&a.modelEntry==b.modelEntry&&
 a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.thread==b.thread&&a.sequence==b.sequence&&a.consumer==b.consumer&&
 a.held==b.held&&a.press==b.press&&a.release==b.release&&a.magnitude==b.magnitude&&a.modelReady==b.modelReady&&
 a.selectedBodyOwned==b.selectedBodyOwned&&a.selectedBody==b.selectedBody&&a.pp==b.pp&&a.source==b.source&&a.windowPPVT==b.windowPPVT&&a.birthSerial==b.birthSerial;
}
dcopy::Key PoseDeferredKey(const cop::Intent& i)noexcept{return {i.submission,i.intent,i.carry,i.at,i.generation,i.epoch,i.prior};}
bool PoseDeferredQuery(NativePoseDeferredCarryContext& out)noexcept{
 out={};NativePoseJobContext job{},end{};NativePoseCarryQueueContext queue{},queueAfter{};
 if(!PoseDeferredPatchesLive()||!ReadNativePoseOwnedJobContext(job)||!ReadNativePoseCarryQueueContext(queue)||!PoseDeferredSameJob(job,queue.job)||
  job.held||job.press||job.release||!std::isfinite(job.magnitude)||job.magnitude!=0||!g_pose_force_a_confirmed||g_pose_fault||g_shutdown||
  !g_pose_force_a_submit_nonce||g_pose_force_a_serial!=g_pose_owners.carrySerial||!g_pose_force_a_submitted_at)return false;
 cop::Intent intent{};NativePoseSubmittedCarryContext receipt{};
 if(!TryAcquireSRWLockShared(&g_pose_carry_intent_lock))return false;intent=g_pose_carry_intent;receipt=g_pose_carry_intent_receipt;ReleaseSRWLockShared(&g_pose_carry_intent_lock);
 const auto now=GetTickCount64();const auto key=PoseDeferredKey(intent);
 if(!cop::Current(intent,g_pose_force_a_submit_nonce,g_pose_owners.carrySerial,g_pose_generation,g_pose_epoch,now)||
  !dcopy::Fresh(key,g_pose_force_a_submitted_at,now)||receipt.submitNonce!=intent.submission||receipt.carrySerial!=intent.carry||receipt.submittedAt!=g_pose_force_a_submitted_at||
  receipt.armGeneration!=job.generation||receipt.armEpoch!=job.sceneEpoch||receipt.player!=job.player||receipt.go!=job.go||receipt.data!=g_pose_queue_owner.data||
  receipt.block1!=job.block||receipt.state1!=job.state||receipt.modelCS1!=job.modelCS||receipt.modelEntry1!=job.modelEntry||receipt.birth1!=job.birthSerial||
  receipt.carrierBirth0!=g_pose_carry_birth0||!receipt.sleepHeld||!receipt.carryHeld||receipt.handlerRva!=0x11268b0||!receipt.sharedSerial||!receipt.thread||!receipt.sequence||(receipt.sequence&1u))return false;
 cp::Pair pair{},after{};auto read=PoseReader;std::uint8_t prior=0,actual=0xff,offline=0;std::uint16_t target=0xffff,previous=0xffff,flags=0;
 unsigned char bytes[0x80]{},bytesAfter[0x80]{};
 if(!cp::ReadPair(read,g_base,job.player,pair)||!cp::Same(pair,g_pose_carry_pair)||!PoseOp0BothBirthsCurrent(pair)||!PoseCarryNativeRest(pair)||
  !PoseRead(pair.body[1].recipientRecords+0x12,prior)||prior!=intent.prior||!PoseRead(pair.body[0].carrierRecords+0x78,target)||target!=1||
  !PoseRead(pair.body[0].carrierRecords+0x7c,previous)||previous!=1||!cop::ReadCarrierTag(read,pair.body[0].carrierRecords,actual,offline)||actual||offline!=1||
  !PoseRead(pair.body[0].carrierRecords+0x6a2,flags)||!(flags&0x1000u)||!PoseReader(queue.queued,bytes,sizeof(bytes)))return false;
 const bool reserved=g_pose_deferred_copy.phase==dcopy::Phase::reserved;
 if(reserved){if(!t_deferred_copy.active||t_deferred_copy.intent!=intent.intent||t_deferred_copy.thread!=job.thread||t_deferred_copy.sequence!=job.sequence||
   !dcopy::CurrentReserved(g_pose_deferred_copy,key,true,bytes))return false;}
 else if(g_pose_deferred_copy.phase!=dcopy::Phase::empty||bytes[0x48]==14||bytes[0x48]==15)return false;
 if(!cp::ReadPair(read,g_base,pair.player,after)||!cp::Same(pair,after)||!cp::CoherentCallbacks(pair.body[0],after.body[0])||!cp::CoherentCallbacks(pair.body[1],after.body[1])||
  !PoseOp0BothBirthsCurrent(after)||!PoseCarryNativeRest(after)||!ReadNativePoseOwnedJobContext(end)||!PoseDeferredSameJob(job,end)||
  !ReadNativePoseCarryQueueContext(queueAfter)||!PoseDeferredSameJob(job,queueAfter.job)||queueAfter.root!=queue.root||queueAfter.slab!=queue.slab||
  queueAfter.queued!=queue.queued||queueAfter.snapshot!=queue.snapshot||queueAfter.bytes!=queue.bytes||queueAfter.carrySerial!=queue.carrySerial||
  !PoseReader(queue.queued,bytesAfter,sizeof(bytesAfter))||std::memcmp(bytes,bytesAfter,sizeof(bytes))||!PoseDeferredImportCurrent())return false;
 cop::Intent late{};if(!TryAcquireSRWLockShared(&g_pose_carry_intent_lock))return false;late=g_pose_carry_intent;ReleaseSRWLockShared(&g_pose_carry_intent_lock);
 if(!late.accepted||late.queued||!dcopy::Same(PoseDeferredKey(late),key)||g_pose_fault)return false;
 out.image=g_base;out.carrierRecord=pair.body[0].carrierRecords;out.recipientRecord=pair.body[1].recipientRecords;out.job=job;out.queue=queue;out.submission=receipt;
 out.intentNonce=intent.intent;out.intentAt=intent.at;out.birth0=g_pose_carry_birth0;out.birth1=job.birthSerial;out.priorSequence=prior;out.copyReserved=reserved;return true;
}
// No direct native activation is registered here. A composed worker consumer
// may call this only after Sleep.Before released its lock in this actual job.
bool PoseDeferredCopyRequest()noexcept{
 NativePoseDeferredCarryContext before{},reserved{},after{};unsigned char bytes[0x80]{};
 if(!g_pose_deferred_sleep_current||!PoseDeferredQuery(before)||!g_pose_deferred_sleep_current(before.submission.submittedSleep)||
  !PoseReader(before.queue.queued,bytes,sizeof(bytes))||!PoseLocalFlagMapWritable(before.queue.queued+0x40,32))return false;
 const dcopy::Key key{before.submission.submitNonce,before.intentNonce,before.submission.carrySerial,before.intentAt,before.job.generation,before.job.sceneEpoch,before.priorSequence};
 if(!dcopy::Reserve(g_pose_deferred_copy,key,before.submission.submittedAt,GetTickCount64(),g_base,bytes))return false;
 const auto saved=t_deferred_copy;t_deferred_copy={before.intentNonce,before.job.thread,before.job.sequence,true};
 bool completed=false;const DWORD incoming=GetLastError();DWORD outgoing=incoming;
 __try{
  if(!PoseDeferredQuery(reserved)||!reserved.copyReserved||!g_pose_deferred_sleep_current(reserved.submission.submittedSleep)||
   ql::ProtectedRange(g_pose_queue_owner,reinterpret_cast<std::uintptr_t>(&g_pose_deferred_copy.payload),sizeof(g_pose_deferred_copy.payload)))PoseFail("pose_carry_copy_reserved_preflight");
  else{
   SetLastError(incoming);const auto returned=reinterpret_cast<std::uintptr_t>(g_pose_queue_setter(reinterpret_cast<void*>(g_pose_queue_owner.data),1,&g_pose_deferred_copy.payload));outgoing=GetLastError();
   unsigned char final[0x80]{};
   const bool copied=PoseDeferredQuery(after)&&after.copyReserved&&PoseDeferredSameJob(reserved.job,after.job)&&after.intentNonce==reserved.intentNonce&&
    g_pose_deferred_sleep_current(after.submission.submittedSleep)&&PoseReader(after.queue.queued,final,sizeof(final))&&
    dcopy::Commit(g_pose_deferred_copy,key,returned,after.queue.queued,final);
   if(!copied)PoseFail("pose_carry_copy_post");
   else if(!TryAcquireSRWLockExclusive(&g_pose_carry_intent_lock))PoseFail("pose_carry_copy_commit_busy");
   else{const bool current=g_pose_carry_intent.accepted&&!g_pose_carry_intent.queued&&dcopy::Same(PoseDeferredKey(g_pose_carry_intent),key);
    if(current)g_pose_carry_intent.queued=true;ReleaseSRWLockExclusive(&g_pose_carry_intent_lock);
    if(!current)PoseFail("pose_carry_copy_commit_changed");else completed=true;}
  }
 }__finally{t_deferred_copy=saved;SetLastError(outgoing);}return completed&&!g_pose_fault;
}
#include "native_pose_vehicle_queue_impl.inc"
void* PoseQueueSetter(void* object,std::uint32_t actual,const void* payload)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* returned=nullptr;
 // The mod transaction uses the retained trampoline. Native held index1
 // setters have no offline qualified producer and cannot overwrite the lease.
 if(PoseOwned()){
  if(g_pose_owners.held&holds::vehicle){PoseFail("pose_vehicle_owned_native_queue_setter");SetLastError(incoming);return returned;}
  if(g_pose_owners.held&holds::carry){PoseFail("pose_carry_owned_native_queue_setter");SetLastError(incoming);return returned;}
  SetLastError(incoming);return g_pose_queue_setter(object,actual,payload);
 }
 PoseSharedLease ticket{};if(!PoseEnterShared(ticket)){SetLastError(incoming);return returned;}
 __try{
  if(g_pose_owners.held&holds::vehicle)PoseVehicleStockLocalQueue(object,actual,payload,ticket,returned,outgoing);
  else if(!ticket.tracked||!(g_pose_owners.held&holds::carry)){SetLastError(incoming);returned=g_pose_queue_setter(object,actual,payload);outgoing=GetLastError();}
  else{
   // Initial narrow slice admits only stock local0's known pure-size payload;
   // never arbitrary virtual size getters or foreign Q instances while held.
   std::uintptr_t vt=0;std::uint32_t real=1,selected=1;std::size_t size=0;unsigned char old[0x80]{},now[0x80]{},input[32]{};
   const auto p=reinterpret_cast<std::uintptr_t>(payload);const auto q=reinterpret_cast<std::uintptr_t>(object);std::uintptr_t mismatch=0;
   const bool typed=PoseCarrySharedTicket(&ticket)&&actual==0&&q==g_pose_queue_owner.data&&PoseQueueCurrent()&&PoseDeferredCode(mismatch)&&PoseDeferredImportCurrent()&&
    PoseRead(g_pose_player+0x214,real)&&!real&&PoseRead(g_pose_player+0x218,selected)&&!selected&&PoseRead(p,vt)&&
    ((vt==g_base+0x23ab570&&(size=16))||(vt==g_base+0x23aa950&&(size=32)))&&PoseReader(p,input,size)&&input[8]!=15&&
    !ql::ProtectedRange(g_pose_queue_owner,p,size)&&PoseReader(g_pose_queue_owner.queued,old,sizeof(old))&&PoseLocalFlagMapWritable(g_pose_queue_owner.queued,size);
   if(!typed)PoseFail("pose_carry_foreign_queue_setter");
   else{
    SetLastError(incoming);returned=g_pose_queue_setter(object,actual,payload);outgoing=GetLastError();
    unsigned char expected[0x80]{};std::memcpy(expected,old,sizeof(expected));std::memcpy(expected,input,size);
    if(reinterpret_cast<std::uintptr_t>(returned)!=g_pose_queue_owner.queued||!PoseReader(g_pose_queue_owner.queued,now,sizeof(now))||std::memcmp(expected,now,sizeof(now))||!PoseQueueCurrent()||!PoseDeferredImportCurrent())PoseFail("pose_carry_local0_queue_post");
   }
  }
 }__finally{PoseExitShared(ticket);SetLastError(outgoing);}return returned;
}
