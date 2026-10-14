#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <climits>
#include "mgscoop/sleep_partner_test.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_player.hpp"
#include "sleep_partner_owned_graph.hpp"
#include "sleep_partner_lease.hpp"
#include "sleep_partner_controls.hpp"
#include "sleep_partner_transition_read.hpp"
#include "mgscoop/native_pose_sleep_parts.hpp"
#include "sleep_parts_policy.hpp"
#include "sleep_parts_read.hpp"
#include "sleep_parts_diagnostic.hpp"
#include "sleep_recovery_contract.hpp"
#include "sleep_rest_maintenance.hpp"
#include "sleep_partner_carry_proof.hpp"
#include "sleep_carry_context_proof.hpp"
#include "sleep_deferred_carry_proof.hpp"
#include "sleep_queued_carry_proof.hpp"
#include "sleep_loaded_requests.hpp"
#include "sleep_seed_contract.hpp"
#include "sleep_body0_transition_contract.hpp"

namespace mgscoop::sleep_partner_test {
namespace {
namespace hk=partner_input_hook;namespace ro=sleep_partner_owned_read;
namespace graph=sleep_partner_owned_graph;namespace lease=sleep_partner_lease;namespace policy=sleep_partner_controls;namespace transition=sleep_partner_transition_read;namespace rest=sleep_rest_maintenance;
SRWLOCK lock=SRWLOCK_INIT;
std::uintptr_t imageBase=0;
hk::NativePoseWorkerBindings worker{};LogFn logger=nullptr;
CarrierSliceQuery carrierSliceQuery=nullptr;DualPartsQuery dualPartsQuery=nullptr;
DeferredCarryQuery deferredCarryQuery=nullptr;QueuedCarryQuery queuedCarryQuery=nullptr;std::uint64_t nextLeaseNonce=0,leaseNonce=0;Target seedTarget{};
bool(*wholeLifetime)() noexcept=nullptr;bool(*carryHeld)() noexcept=nullptr;
bool configured=false,enabled=false,preparing=false,wakeRequested=false,clearPending=false,fault=false;
Target target{},ownedTarget{},completedTarget{};bool completedCycle=false;
lease::Lease heldLease{};lease::Sample preparedOwner{};
std::uintptr_t graphMog=0,graphLifetime=0;transition::Observation ownedTransition{};
std::uint64_t preparedAt=0,pausedAt=0;
unsigned emptyRetiredSamples=0;std::uint32_t emptyRetiredWindow=0;
unsigned restWrites=0;std::uint64_t restFirstAt=0;
Status cached{};
struct Preserve {DWORD error=GetLastError();~Preserve(){SetLastError(error);}};
bool Worker() noexcept {
#ifdef MGS_BACKGROUND_WORKER
 return worker.admitted&&worker.refuse&&worker.admitted();
#else
 return false;
#endif
}
bool Active() noexcept {return preparing||heldLease.phase!=lease::Phase::empty;}
bool Lifetime() noexcept {return wholeLifetime&&wholeLifetime();}
bool Carry() noexcept {return carryHeld&&carryHeld();}
template<class T> bool Get(std::uintptr_t at,T& out) noexcept {
 SIZE_T n=0;return at&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),&out,sizeof(out),&n)&&n==sizeof(out);
}
bool Read(std::uintptr_t at,void* out,std::size_t size) noexcept {
 SIZE_T n=0;return ro::Range(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&n)&&n==size;
}
void Reason(const char* why) noexcept {
 const bool changed=!cached.reason||std::strcmp(cached.reason,why)!=0;
 cached.reason=why;cached.configured=configured;cached.enabled=enabled;cached.active=Active();
 cached.held=preparing||heldLease.phase!=lease::Phase::empty;cached.wakePending=wakeRequested;
 cached.clearPending=clearPending;cached.phase=preparing?1u:heldLease.phase==lease::Phase::empty?0u:static_cast<std::uint32_t>(heldLease.phase)+1u;
 if(changed&&logger){char line[320]{};std::snprintf(line,sizeof(line),"[sleep_partner] ready=%u enabled=%u active=%u phase=%u mode=%u life=%u action=%u stance=%u zero=%u buttons=%u birth=%llu gen=%u win=%u requests=%u stage=%s",configured?1u:0u,enabled?1u:0u,Active()?1u:0u,cached.phase,cached.mode,cached.lifeState,cached.qAction,cached.stance,cached.zero,cached.computedButtons,static_cast<unsigned long long>(cached.birthSerial),cached.peerGeneration,cached.window,cached.requests,why);logger(line);}
}
void Abort(const char* why) noexcept {
 fault=true;Reason(why);
 if(worker.refuse)worker.refuse(why); // owned private supervisor; never drop lease or unlock
}
ro::Scope Scope(const hk::NativePoseJobContext& c) noexcept {
 ro::Scope s{};const auto status=hk::NativePoseWindowStatus();const auto now=GetTickCount64();
 const auto& request=Active()?ownedTarget:target;
 s.exactGate=configured;s.privateWorker=s.copiedSaves=Worker();s.required=status.required;
 s.ownedWindow=hk::NativePoseOwnedWindow();s.serialized=s.ownedWindow;
 s.participantsLive=status.installed&&s.ownedWindow&&!status.faulted&&Lifetime();
 s.selectedBodyOwned=c.selectedBodyOwned;s.modelReady=c.modelReady;s.batch=partner_defer::InBatch();s.faulted=status.faulted||fault;
 s.pairedReady=target.pairedReady;s.fieldReady=target.fieldReady;s.demo=target.demo;s.stopping=target.stopping;
 s.image=imageBase;s.player=c.player;s.go=c.go;s.block=c.block;s.ps=c.state;s.modelCS=c.modelCS;s.modelEntry=c.modelEntry;
 s.pp=c.pp;s.source=c.source;s.windowPPVT=c.windowPPVT;s.birthSerial=c.birthSerial;
 Get(c.player+0x60,s.data);if(s.data)Get(s.data+0x1e8,s.state);
 s.mission=target.mission;s.ownedMission=request.mission;s.epoch=target.worldEpoch;s.ownedEpoch=request.worldEpoch;
 s.generation=target.peerGeneration;s.ownedGeneration=request.peerGeneration;
 s.armGeneration=c.generation;s.armEpoch=c.sceneEpoch;
 s.thread=GetCurrentThreadId();s.ownerThread=c.thread;
 s.sequenceBefore=c.sequence;s.sequenceAfter=hk::WindowSeq();s.now=now;s.poll=target.poll;
 if(c.selectedBody!=1||!c.birthSerial||status.birthSerial!=c.birthSerial||
    c.generation!=request.armGeneration||c.sceneEpoch!=request.armEpoch)s.modelReady=false;
 return s;
}
struct Native {
 ro::Scope scope{};graph::Observation observed{};transition::Observation supplement{};lease::Sample sample{};lease::Scope proof{};
 bool requestsPresent=false,zero=false;std::uint32_t pool=0;
 rest::Descriptor restDescriptor{};bool restDescriptorReady=false;
};
bool ReadNative(const hk::NativePoseJobContext& c,bool requests,Native& out,const char*& why,bool updateCache=true) noexcept {
 out={};if(updateCache)cached.stanceObserved=false;const auto s=Scope(c);why="partner_sleep_owned_scope";
 if(const auto refused=ro::ScopeRefusal(s)){why=refused;return false;}
 graph::Observation a{},b{},after{};transition::Observation ta{},tb{},tc{};
 rest::Descriptor da{},db{},dc{};bool dra=false,drb=false,drc=false;
 if(!graph::ReadOnce(Read,s,a,&why)||!transition::ReadOnce(Read,s,ta,&why))return false;
 if(a.owner.body[1].mode==5)dra=rest::ReadDescriptor(Read,s,a.owner.body[1],da);
 const auto middle=Scope(c);if(!ro::SameScope(s,middle)){why="partner_sleep_scope_changed";return false;}
 if(!graph::ReadOnce(Read,middle,b,&why)||!transition::ReadOnce(Read,middle,tb,&why))return false;
 if(b.owner.body[1].mode==5)drb=rest::ReadDescriptor(Read,middle,b.owner.body[1],db);
 if(!graph::Same(a,b)||!transition::Same(ta,tb)){why="partner_sleep_pair_changed";return false;}
 sleep_loaded_requests::Reply names{};
 if(requests&&!sleep_loaded_requests::Read(Read,b.graph[1],names)){why=names.reason;return false;}
 const auto finalScope=Scope(c);if(!ro::SameScope(s,finalScope)){why="partner_sleep_scope_changed";return false;}
 if(!graph::ReadOnce(Read,finalScope,after,&why)||!transition::ReadOnce(Read,finalScope,tc,&why))return false;
 if(after.owner.body[1].mode==5)drc=rest::ReadDescriptor(Read,finalScope,after.owner.body[1],dc);
 if(!graph::Same(b,after)||!transition::Same(tb,tc)){why="partner_sleep_final_changed";return false;}
 const auto completed=Scope(c);if(!ro::SameScope(s,completed)){why="partner_sleep_scope_changed";return false;}
 out.scope=completed;out.observed=after;out.supplement=tc;out.pool=after.owner.poolIndex[1];out.requestsPresent=requests&&names.ready&&names.found==0xffff;
 out.restDescriptorReady=dra&&drb&&drc&&rest::Same(da,db)&&rest::Same(db,dc);
 out.restDescriptor=out.restDescriptorReady?dc:rest::Descriptor{};
 const auto& body=after.owner.body[1];const auto& g=after.graph[1];const auto& value=after.owner.values[1];
 out.sample.owner={completed.player,completed.data,body.lifeRecord,body.block,body.ps,body.primary,body.node,
 body.records,body.bitmap,g.cs,g.controller,completed.mission,completed.generation,completed.thread,
 completed.armGeneration,completed.armEpoch,completed.epoch,completed.birthSerial};
 out.sample.health=value.health;out.sample.maximumHealth=value.maximumHealth;out.sample.stun=value.stun;out.sample.maximumStun=value.maximumStun;
 out.sample.lifeState=body.lifeState;out.sample.mode=body.mode;out.sample.requested=body.requested;out.sample.action=body.action;
 out.sample.qAction=body.qAction;out.sample.stance=value.psStance;out.sample.cause=2;out.sample.nodeActive=body.nodeActive;
 out.sample.graphCurrent=g.currentNode;out.sample.graphSleepCandidate=g.sleepHashCandidate;out.sample.graphHash=g.stateHash;
 out.zero=ro::Zero(after.owner.pad);
 out.proof.exactRetailGate=out.proof.sleepCodeGate=out.proof.seedCodeGate=configured;
 out.proof.workerActive=out.proof.threadAdmitted=out.proof.copiedSave=Worker();out.proof.coherent=true;
 out.proof.admitted=out.proof.pairedOffline=completed.pairedReady;
 out.proof.selectedBodyOwned=completed.selectedBodyOwned;out.proof.ownedOddWindow=true;
 out.proof.participantsLive=completed.participantsLive;out.proof.controlsZero=out.zero;
 out.proof.thread=completed.thread;out.proof.sequenceBefore=completed.sequenceBefore;out.proof.sequenceAfter=completed.sequenceAfter;
 out.proof.batchBefore=out.proof.batchAfter=false;out.proof.now=completed.now;out.proof.poll=completed.poll;
 if(updateCache){
 cached.mode=body.mode;cached.lifeState=body.lifeState;cached.qAction=body.qAction;cached.stance=value.psStance;
 cached.psPacked=value.psPacked;cached.qStance=value.qStance;cached.psAction=body.action;cached.stanceObserved=true;cached.stanceAt=completed.now;
 cached.computedButtons=after.owner.pad.computedButtons;cached.zero=out.zero?1u:0u;cached.window=c.sequence;
 cached.birthSerial=c.birthSerial;cached.peerGeneration=completed.generation;cached.requests=names.found;
 }
 if(!ro::CoherentTargetStance(after.owner)){why="partner_sleep_stance_pair";return false;}
 if(Active()&&(heldLease.phase!=lease::Phase::empty||preparedOwner.owner.player)&&
    (!lease::Same(heldLease.phase==lease::Phase::empty?preparedOwner.owner:heldLease.owner,out.sample.owner)||
     g.mog!=graphMog||g.lifetime!=graphLifetime||!transition::SameOwner(ownedTransition,tc))){why="partner_sleep_owner_changed";return false;}
 why="partner_sleep_read_ready";return true;
}
bool Cas(const hk::NativePoseJobContext& c,const Native& n,const lease::Exchange& change) noexcept {
 const auto now=Scope(c);
 if(!policy::CasWindow(c,n.scope,now,n.zero,Worker(),Lifetime(),Carry())||
    !hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(c.go),n.pool))return false;
 const auto at=n.sample.owner.life+change.offset;if(!at||(at&3u))return false;
 __try {return static_cast<std::uint32_t>(_InterlockedCompareExchange(reinterpret_cast<volatile long*>(at),
             static_cast<long>(change.desired),static_cast<long>(change.expected)))==change.expected;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void CapturePreparation(const Native& n) noexcept {
 restWrites=0;restFirstAt=0;leaseNonce=0;seedTarget={};preparedOwner=n.sample;ownedTarget=target;graphMog=n.observed.graph[1].mog;graphLifetime=n.observed.graph[1].lifetime;
 ownedTransition=n.supplement;preparedAt=n.scope.now;preparing=true;emptyRetiredSamples=0;emptyRetiredWindow=0;
}
bool RetirePreparation(const hk::NativePoseJobContext& c,const Native& n) noexcept {
 if(lease::ContextRefusal(n.proof,n.sample)||!lease::RetiredSample(n.sample)||!n.zero){emptyRetiredSamples=0;emptyRetiredWindow=0;return true;}
 if(!emptyRetiredSamples||emptyRetiredWindow!=c.sequence){++emptyRetiredSamples;emptyRetiredWindow=c.sequence;}
 if(emptyRetiredSamples<2){Reason("partner_sleep_empty_retire_second");return true;}
 Native proof{};const char* why=nullptr;
 if(!ReadNative(c,false,proof,why)||lease::ContextRefusal(proof.proof,proof.sample)||!lease::RetiredSample(proof.sample)||!proof.zero){Reason(why?why:"partner_sleep_empty_release");return true;}
 if(!hk::RequestNativePoseLeaseClear()){Abort("partner_sleep_clear_refused");return false;}
 clearPending=true;Reason("partner_sleep_empty_cleanup_pending");return true;
}
bool MaintainNativeRest(const hk::NativePoseJobContext& c,Native& n,bool before) noexcept {
 // Onset/recovery and a genuine grant keep the original lifecycle. Maintenance
 // is never performed from After, a shared poll or a Carry-paused checkpoint.
 if(!before||!enabled||wakeRequested||!target.asleep||heldLease.phase!=lease::Phase::active||
    !heldLease.nativeEntered||n.sample.mode!=5)return true;
 if(!n.restDescriptorReady){Abort("partner_sleep_rest_descriptor");return false;}
 const auto plan=rest::MaintainRest(n.proof,n.sample,heldLease,target,seedTarget,n.restDescriptor,before,enabled,wakeRequested,Carry());
 if(!plan.valid){Abort(plan.reason);return false;}
 if(!plan.count)return true;
 Native fresh{};const char* why=nullptr;
 if(!ReadNative(c,false,fresh,why)){Abort(why?why:"partner_sleep_rest_before_read");return false;}
 if(!fresh.restDescriptorReady||!ro::SameScope(n.scope,fresh.scope)||!graph::Same(n.observed,fresh.observed)||
    !transition::Same(n.supplement,fresh.supplement)||!rest::Same(n.restDescriptor,fresh.restDescriptor)||
    !rest::SameRestSample(n.sample,fresh.sample,false)){Abort("partner_sleep_rest_before_changed");return false;}
 const auto current=rest::MaintainRest(fresh.proof,fresh.sample,heldLease,target,seedTarget,fresh.restDescriptor,
                                      before,enabled,wakeRequested,Carry());
 if(!current.valid||current.count!=1||current.changes[0].offset!=0x2c||current.changes[0].desired!=lease::Bits(0.f)||
    current.changes[0].expected!=plan.changes[0].expected){Abort("partner_sleep_rest_plan_changed");return false;}
 // Cas performs the original immediate actual owner/window/zero-pad proof;
 // expected bits refuse any intervening native regeneration or threshold.
 if(!Cas(c,fresh,current.changes[0])){Abort("partner_sleep_rest_cas_refused");return false;}
 Native after{};why=nullptr;
 if(!ReadNative(c,false,after,why)){Abort(why?why:"partner_sleep_rest_after_read");return false;}
 if(!after.restDescriptorReady||!ro::SameScope(fresh.scope,after.scope)||!transition::Same(fresh.supplement,after.supplement)||
    !rest::Same(fresh.restDescriptor,after.restDescriptor)||!rest::SameRestSample(fresh.sample,after.sample,true)){
  Abort("partner_sleep_rest_after_changed");return false;
 }
 const auto verified=rest::MaintainRest(after.proof,after.sample,heldLease,target,seedTarget,after.restDescriptor,
                                       before,enabled,wakeRequested,Carry());
 if(!verified.valid||verified.count){Abort("partner_sleep_rest_zero_unproved");return false;}
 // Observe receives the new actual sample, never the invalidated pre-CAS one.
 n=after;if(restWrites!=UINT_MAX)++restWrites;
 if(!restFirstAt){restFirstAt=n.scope.now;if(logger){char line[320]{};
  std::snprintf(line,sizeof(line),"[sleep_partner_rest] writes=%u;window=%u;gen=%u;mode=%u;life=%u;hash=%llx;stun=%x;maximum=%x;descriptor=%x",
   restWrites,c.sequence,target.peerGeneration,n.sample.mode,n.sample.lifeState,static_cast<unsigned long long>(n.sample.graphHash),
   current.changes[0].expected,lease::Bits(n.sample.maximumStun),n.restDescriptor.flags);logger(line);}}
 return true;
}
bool Step(const hk::NativePoseJobContext& c,bool before) noexcept {
 if(fault)return false;
 if(!Active()&&(!enabled||!target.asleep||(completedCycle&&policy::SamePeerCycle(target,completedTarget))))return true;
 if(!Worker()||!Lifetime()){if(Active())Abort("partner_sleep_lifetime_or_worker");else Reason("partner_sleep_lifetime_or_worker");return !Active();}
 if(const auto recovery=lease::RecoveryDeadline(heldLease,GetTickCount64())){Abort(recovery);return false;}
 if(Carry()){
  if(Active()&&!pausedAt)pausedAt=GetTickCount64();Reason("partner_sleep_carry_suspended");return true;
 }
 if(pausedAt){
  const auto now=GetTickCount64();
  if(!policy::ResumeTimers(pausedAt,now,preparedAt,heldLease)){Abort("partner_sleep_pause_clock");return false;}
  pausedAt=0;
 }
 const auto now=GetTickCount64();
 if(const auto expired=policy::Watchdog(preparing,preparedAt,heldLease,now)){Abort(expired);return false;}
 if(!policy::FreshTarget(target,now)||(Active()&&!policy::SameTargetScene(target,ownedTarget))){
  if(Active())Abort("partner_sleep_target_stale_or_changed");else Reason("partner_sleep_target_stale_or_changed");
  return !Active();
 }
 Native n{};const char* why=nullptr;
 if(!ReadNative(c,before&&heldLease.phase==lease::Phase::empty,n,why)){
  Reason(why?why:"partner_sleep_read");
  const auto lastGood=heldLease.phase==lease::Phase::empty?preparedAt:heldLease.lastSeen;
  if(Active()&&now>=lastGood&&now-lastGood>lease::kAcquireMs){Abort(cached.reason);return false;}
  return !Active(); // mandatory original suppressed while retained unreadable owner
 }
 if(!Active()){
  if(!before)return true;
  // Hold model lifetime BEFORE any stance call or scalar mutation. This frame
  // may not yet have a neutral Pre; no scalar write is allowed until it does.
  if(!hk::SetNativePoseLeaseHeld()){Reason("partner_sleep_hold_refused");return false;}
  CapturePreparation(n);Reason("partner_sleep_prepare");
 }
 if(!target.asleep||!enabled)wakeRequested=true;
 if(heldLease.phase==lease::Phase::empty){
  if(wakeRequested)return RetirePreparation(c,n);
  // CapturePreparation records the completed native-read time. The Step
  // timestamp predates that read; compare the deadline with a fresh clock.
  if(const auto expired=policy::Watchdog(preparing,preparedAt,heldLease,GetTickCount64())){Abort(expired);return false;}
  if(!before)return true;
  if(!n.zero){Reason("partner_sleep_pad_not_zero");return true;}
  if(n.sample.mode||n.sample.requested||n.sample.lifeState||n.sample.nodeActive){Abort("partner_sleep_prepare_native_active");return false;}
  if(n.sample.stance!=1){
   auto addresses=partner_player::AddressesFor(reinterpret_cast<const void*>(imageBase));std::uint8_t ps=0,q=0;
   if(!hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(c.go),n.pool)){Abort("partner_sleep_stand_window");return false;}
   if(const auto refused=partner_player::SetStance(addresses,1,false,1,ps,q)){Reason(refused);return true;}
   Reason("partner_sleep_stand_requested");return true;
  }
  const auto plan=lease::Seed(n.proof,n.sample,heldLease,n.requestsPresent);
  if(!plan.valid){Reason(plan.reason);return true;}
  std::uint64_t candidateNonce=0;
  if(!sleep_partner_carry_proof::AdvanceNonce(nextLeaseNonce,candidateNonce)){Abort("partner_sleep_nonce_exhausted");return false;}
  const bool first=Cas(c,n,plan.changes[0]);heldLease=lease::Commit(n.sample,n.scope.now,first,false);
  if(first){leaseNonce=candidateNonce;seedTarget=target;}
  if(!first){Reason("partner_sleep_first_cas_refused");return true;}
  Native next{};const char* stage=nullptr;
  bool second=false;
  if(ReadNative(c,false,next,stage)&&lease::Repair(next.proof,next.sample,heldLease).valid)second=Cas(c,next,plan.changes[1]);
  if(second){heldLease.phase=lease::Phase::seeded;preparing=false;Reason("partner_sleep_seeded");return true;}
  // A partial first CAS is owned. Exact same-idle repair is one bounded CAS;
  // regardless of repair receipt, this private cycle aborts without unlock.
  Native repair{};const char* repairWhy=nullptr;
  if(ReadNative(c,false,repair,repairWhy)){
   const auto fix=lease::Repair(repair.proof,repair.sample,heldLease);
   if(fix.valid)lease::MarkRepaired(heldLease,Cas(c,repair,fix.changes[0]));
  }
  Abort(stage?stage:"partner_sleep_second_cas_refused");return false;
 }
 if(wakeRequested)lease::RequestWake(heldLease,heldLease.owner.generation,n.scope.now);
 if(before&&wakeRequested){
  const auto plan=lease::Wake(n.proof,n.sample,heldLease);
  if(plan.valid&&plan.count&&!Cas(c,n,plan.changes[0])){Reason("partner_sleep_wake_cas_refused");return true;}
  // Mutation invalidates the sample; leave natural mode5->6 scheduler intact.
  if(plan.valid&&plan.count){Reason("partner_sleep_wake_requested");return true;}
 }
 if(!MaintainNativeRest(c,n,before))return false;
 const auto receipt=lease::Observe(heldLease,n.proof,n.sample,c.sequence);Reason(receipt.reason);
 if(receipt.labAbortRequired){Abort(receipt.reason);return false;}
 if(!before&&receipt.retired&&heldLease.wakeAuthorized&&!clearPending){
  Native release{};const char* stage=nullptr;
  if(!ReadNative(c,false,release,stage)||!policy::ReleaseSample(heldLease,release.proof,release.sample)||!release.zero){
   Reason(stage?stage:"partner_sleep_release_proof");return true;
  }
  if(!hk::RequestNativePoseLeaseClear()){Abort("partner_sleep_clear_refused");return false;}
  clearPending=true;Reason("partner_sleep_cleanup_pending");
 }
 return true;
}
sleep_partner_carry_proof::Retained CarryRetained() noexcept {
 sleep_partner_carry_proof::Retained r{};r.configured=configured;r.enabled=enabled;r.preparing=preparing;r.wakePending=wakeRequested;
 r.clearPending=clearPending;r.faulted=fault;r.worker=Worker();r.lifetime=Lifetime();r.image=imageBase;r.modelEntry=ownedTransition.entry;r.nonce=leaseNonce;
 r.owned=seedTarget;r.current=target;r.lease=heldLease;return r;
}
} // namespace
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 const Preserve preserve;const auto base=reinterpret_cast<std::uintptr_t>(image);
 auto reader=hk::ReadNativePoseOriginalCode;
 return sleep_plugin_read::CodeGate(reader,base,span,mismatch)&&sleep_graph_read::CodeGate(reader,base,span,mismatch)&&
        sleep_seed_contract::CodeGate(reader,base,span,mismatch)&&sleep_body0_transition_contract::CodeGate(reader,base,span,mismatch)&&
        sleep_partner_stance_contract::CodeGate(reader,base,span,mismatch)&&sleep_recovery_contract::CodeGate(reader,base,span,mismatch);
}
bool Configure(const void* image,std::size_t span,bool exact,const hk::NativePoseWorkerBindings& bindings,LogFn log) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 bool ok=!Active()&&!enabled&&!fault&&exact&&image&&span&&bindings.admitted&&bindings.refuse;
 std::uintptr_t mismatch=0;if(ok)ok=CodeGate(image,span,mismatch);
 if(ok){imageBase=reinterpret_cast<std::uintptr_t>(image);worker=bindings;logger=log;configured=true;Reason("partner_sleep_configured");}
 ReleaseSRWLockExclusive(&lock);return ok;
}
void BindLifetimeQuery(bool(*live)() noexcept) noexcept {AcquireSRWLockExclusive(&lock);if(!Active()&&!enabled)wholeLifetime=live;ReleaseSRWLockExclusive(&lock);}
void BindCarryHeldQuery(bool(*live)() noexcept) noexcept {AcquireSRWLockExclusive(&lock);if(!Active()&&!enabled)carryHeld=live;ReleaseSRWLockExclusive(&lock);}
bool PublishTarget(const Target& value) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 const bool ok=configured&&Worker();if(ok)target=value;ReleaseSRWLockExclusive(&lock);return ok;
}
bool Enable(bool value) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 const bool ok=!value||(configured&&!fault&&Worker()&&Lifetime());
 if(ok){enabled=value;if(!value&&Active())wakeRequested=true;Reason(value?"partner_sleep_armed":Active()?"partner_sleep_disable_wake_pending":"partner_sleep_off");}
 ReleaseSRWLockExclusive(&lock);return ok;
}
bool RequestWake() noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);const bool ok=Active()&&Worker()&&!fault;
 if(ok){wakeRequested=true;Reason("partner_sleep_wake_queued");}ReleaseSRWLockExclusive(&lock);return ok;
}
bool Before(const hk::NativePoseJobContext& c) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);const bool ok=Step(c,true);ReleaseSRWLockExclusive(&lock);return ok;
}
bool After(const hk::NativePoseJobContext& c) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);const bool ok=Step(c,false);ReleaseSRWLockExclusive(&lock);return ok;
}
void Closed(const hk::NativePoseJobContext& c,bool cleanup) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 if(clearPending){
  const auto& owner=heldLease.phase==lease::Phase::empty?preparedOwner.owner:heldLease.owner;
  if(!policy::ClosedOwner(c,owner,cleanup)){
   Abort("partner_sleep_closed_cleanup_refused");
  }else{
   if(restWrites&&logger){char line[192]{};const auto now=GetTickCount64();
    std::snprintf(line,sizeof(line),"[sleep_partner_rest] closed=1;writes=%u;elapsed_ms=%llu",restWrites,
     static_cast<unsigned long long>(now>=restFirstAt?now-restFirstAt:0));logger(line);}
   completedTarget=ownedTarget;completedCycle=true;preparing=false;heldLease={};preparedOwner={};clearPending=false;wakeRequested=false;preparedAt=0;leaseNonce=0;seedTarget={};
   graphMog=graphLifetime=0;ownedTransition={};emptyRetiredSamples=0;emptyRetiredWindow=0;Reason("partner_sleep_retired");
  }
 }else if(Active()&&!cleanup)Abort("partner_sleep_window_cleanup_failed");
 ReleaseSRWLockExclusive(&lock);
}
namespace {
volatile LONG partsRefusalLogged=0;
void PartsRefusal(const char* stage,const char* detail,unsigned index,const hk::NativePoseSleepPartsContext* c,
 const sleep_parts_graph::Sample* raw,const lease::Sample* sample,bool locked)noexcept {
 const Preserve preserve;if(InterlockedCompareExchange(&partsRefusalLogged,1,0)!=0||!logger)return;
 const auto now=GetTickCount64();const auto age=[&](std::uint64_t at)noexcept{return sleep_parts_diagnostic::Age(now,at);};
 const auto phase=locked?static_cast<unsigned>(heldLease.phase):UINT32_MAX;
 const auto* b=raw?&raw->observed.owner.body[1]:nullptr;const auto* g=raw?&raw->observed.graph[1]:nullptr;
 char line[1536]{};
 std::snprintf(line,sizeof(line),
 "[sleep_parts_refusal] stage=%s;detail=%s;read=%u;typed=%u;phase=%u;preparing=%u;clear=%u;paused=%u;wake_req=%u;wake_auth=%u;entered=%u;active=%u;fault=%u;last_ms=%u;seed_ms=%u;wake_ms=%u;recovery_ms=%u;poll_ms=%u;paired=%u;field=%u;demo=%u;stop=%u;asleep=%u;mission=%u,%u;gen=%u,%u;epoch=%u,%u;arm=%u,%u;win=%u;birth=%llu;shared=%llu;mode=%u;request=%u;life=%u;ps_action=%u;q_action=%u;stance=%u;packed=%x;q_stance=%u;node=%u;graph=%u;candidate=%u;index=%u;hash=%llx;hp=%x,%x;stun=%x,%x;owner=%u;maximum=%u\r\n",
 stage,detail?detail:"none",index,sample?1u:0u,phase,locked&&preparing?1u:0u,locked&&clearPending?1u:0u,locked&&pausedAt?1u:0u,
 locked&&wakeRequested?1u:0u,locked&&heldLease.wakeAuthorized?1u:0u,locked&&heldLease.nativeEntered?1u:0u,locked&&Active()?1u:0u,locked&&fault?1u:0u,
 locked?age(heldLease.lastSeen):UINT32_MAX,locked?age(heldLease.seededAt):UINT32_MAX,locked?age(heldLease.wakeAt):UINT32_MAX,locked?age(heldLease.recoveryAt):UINT32_MAX,locked?age(target.poll):UINT32_MAX,
 locked&&target.pairedReady?1u:0u,locked&&target.fieldReady?1u:0u,locked&&target.demo?1u:0u,locked&&target.stopping?1u:0u,locked&&target.asleep?1u:0u,
 locked?target.mission:0u,locked?ownedTarget.mission:0u,locked?target.peerGeneration:0u,locked?ownedTarget.peerGeneration:0u,
 locked?static_cast<unsigned>(target.worldEpoch):0u,locked?static_cast<unsigned>(ownedTarget.worldEpoch):0u,
 locked?target.armGeneration:0u,locked?ownedTarget.armGeneration:0u,c?c->sequence:0u,
 static_cast<unsigned long long>(c?c->birthSerial:0),static_cast<unsigned long long>(c?c->sharedSerial:0),
 b?static_cast<unsigned>(b->mode):0u,b?static_cast<unsigned>(b->requested):0u,b?b->lifeState:0u,b?static_cast<unsigned>(b->action):0u,b?static_cast<unsigned>(b->qAction):0u,
 raw?(raw->psPacked&15u):0u,raw?raw->psPacked:0u,raw?static_cast<unsigned>(raw->qStance):0u,b&&b->nodeActive?1u:0u,g&&g->currentNode?1u:0u,g&&g->sleepHashCandidate?1u:0u,
 g?g->index:0u,static_cast<unsigned long long>(g?g->stateHash:0),
 raw?lease::Bits(raw->health):0u,raw?lease::Bits(raw->maximumHealth):0u,raw?lease::Bits(raw->stun):0u,raw?lease::Bits(raw->maximumStun):0u,
 locked&&sample&&lease::Same(heldLease.phase==lease::Phase::empty?preparedOwner.owner:heldLease.owner,sample->owner)?1u:0u,
 locked&&sample&&(heldLease.phase==lease::Phase::empty?lease::Bits(preparedOwner.maximumStun)==lease::Bits(sample->maximumStun):lease::SameMaximum(heldLease,*sample))?1u:0u);
 logger(line);
}
}
bool AllowPartsPoll() noexcept {
 const Preserve preserve;if(!TryAcquireSRWLockShared(&lock)){PartsRefusal("lock_busy",nullptr,0,nullptr,nullptr,nullptr,false);return false;}
 const auto run=[]()noexcept {
  const auto now=GetTickCount64();
  const auto reject=[](const char* stage,const char* detail,unsigned i,const hk::NativePoseSleepPartsContext* c=nullptr,
   const sleep_parts_graph::Sample* raw=nullptr,const lease::Sample* sample=nullptr)noexcept{
    PartsRefusal(stage,detail,i,c,raw,sample,true);return false;
  };
  if(!configured)return reject("configured",nullptr,0);
  if(!Worker())return reject("worker",nullptr,0);
  if(!Lifetime())return reject("lifetime",nullptr,0);
  if(fault)return reject("fault",nullptr,0);
  if(clearPending)return reject("clear_pending",nullptr,0);
  if(pausedAt)return reject("paused",nullptr,0);
  if(!Active())return reject("inactive",nullptr,0);
  if(!policy::FreshTarget(target,now))return reject("target_fresh",nullptr,0);
  if(!policy::SameTargetScene(target,ownedTarget))return reject("target_scene",nullptr,0);
  if(const auto why=policy::Watchdog(preparing,preparedAt,heldLease,now))return reject("watchdog",why,0);
  hk::NativePoseSleepPartsContext c[3]{};
  sleep_parts_graph::Sample a{},b{},last{};
  const auto read=[&](unsigned index,sleep_parts_graph::Sample& out)noexcept {
   if(!hk::ReadNativePoseSleepPartsPoll(c[index]))return reject("context_get",nullptr,index,&c[index]);
   if(!sleep_parts_policy::ContextValid(c[index]))return reject("context_valid",nullptr,index,&c[index]);
   if(c[index].image!=imageBase||c[index].thread!=GetCurrentThreadId()||
    c[index].generation!=ownedTarget.armGeneration||c[index].sceneEpoch!=ownedTarget.armEpoch)return reject("context_owner",nullptr,index,&c[index]);
   sleep_parts_plugin::Scope s{};s.exactGate=configured;s.admitted=target.pairedReady;s.ready=s.armed=true;
   s.image=imageBase;s.player=c[index].player;s.data=c[index].data;s.state=c[index].state;
   s.mission=target.mission;s.ownedMission=ownedTarget.mission;s.generation=target.peerGeneration;s.ownedGeneration=ownedTarget.peerGeneration;
   s.epoch=target.worldEpoch;s.ownedEpoch=ownedTarget.worldEpoch;s.thread=c[index].thread;s.ownerThread=GetCurrentThreadId();
   s.sequenceBefore=c[index].sequence;s.sequenceAfter=hk::WindowSeq();s.now=now;s.poll=target.poll;s.batch=partner_defer::InBatch();
   const char* why=nullptr;if(!sleep_parts_graph::Read(Read,s,out,&why))return reject("typed_read",why,index,&c[index],&out);
   const auto& body=out.observed.owner.body[1];const auto& graph=out.observed.graph[1];
   if(body.block!=c[index].block||body.ps!=c[index].ps||graph.cs!=c[index].modelCS||graph.entry!=c[index].modelEntry||
    graph.mog!=graphMog||graph.lifetime!=graphLifetime)return reject("native_owner",nullptr,index,&c[index],&out);
   lease::Sample sample{};sample.owner={c[index].player,c[index].data,body.lifeRecord,body.block,body.ps,body.primary,body.node,
    body.records,body.bitmap,graph.cs,graph.controller,target.mission,target.peerGeneration,c[index].thread,
    c[index].generation,c[index].sceneEpoch,target.worldEpoch,c[index].birthSerial};
   sample.health=out.health;sample.maximumHealth=out.maximumHealth;sample.stun=out.stun;sample.maximumStun=out.maximumStun;
   sample.lifeState=body.lifeState;sample.mode=body.mode;sample.requested=body.requested;sample.action=body.action;sample.qAction=body.qAction;
   sample.stance=static_cast<std::uint8_t>(out.psPacked&15u);sample.cause=2;sample.nodeActive=body.nodeActive;
   sample.graphCurrent=graph.currentNode;sample.graphSleepCandidate=graph.sleepHashCandidate;sample.graphHash=graph.stateHash;
   if(!sleep_parts_policy::CurrentSample(preparing,preparedAt,preparedOwner,heldLease,sample,now))
    return reject("current_sample",sleep_parts_diagnostic::Explain(preparing,preparedAt,preparedOwner,heldLease,sample,now),index,&c[index],&out,&sample);
   return true;
  };
  if(!read(0,a)||!read(1,b))return false;
  if(!sleep_parts_policy::Same(c[0],c[1]))return reject("context_pair",nullptr,1,&c[1],&b);
  if(!sleep_parts_graph::Same(a,b))return reject("native_pair",nullptr,1,&c[1],&b);
  if(!read(2,last))return false;
  if(!sleep_parts_policy::Same(c[1],c[2]))return reject("context_final_pair",nullptr,2,&c[2],&last);
  if(!sleep_parts_graph::Same(b,last))return reject("native_final_pair",nullptr,2,&c[2],&last);
  hk::NativePoseSleepPartsContext final{};
  if(!hk::ReadNativePoseSleepPartsPoll(final))return reject("context_final",nullptr,3);
  if(!sleep_parts_policy::Same(c[2],final))return reject("context_final_scope",nullptr,3,&final,&last);
  return true;
 };
 const bool ok=run();ReleaseSRWLockShared(&lock);return ok;
}
bool SuppressForcedStance() noexcept {
 const Preserve preserve;if(!TryAcquireSRWLockShared(&lock))return true;
 const bool suppress=Active();ReleaseSRWLockShared(&lock);return suppress;
}
bool BindCarrierSliceQuery(CarrierSliceQuery query) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 const bool ok=query&&configured&&!enabled&&!Active()&&!fault&&(!carrierSliceQuery||carrierSliceQuery==query);
 if(ok)carrierSliceQuery=query;ReleaseSRWLockExclusive(&lock);return ok;
}
bool ReadCarryLeaseProof(CarryLeaseProof& out) noexcept {
 const Preserve preserve;out={};if(!TryAcquireSRWLockShared(&lock))return false;
 CarrierSliceSnapshot first{},second{},last{};CarryLeaseProof candidate{},after{};
 bool ok=false;
 if(carrierSliceQuery&&configured&&Worker()&&Lifetime()&&carrierSliceQuery(first)&&first.thread==GetCurrentThreadId()){
  sleep_partner_carry_proof::Retained retained{};
  retained.configured=configured;retained.enabled=enabled;retained.preparing=preparing;retained.wakePending=wakeRequested;
  retained.clearPending=clearPending;retained.faulted=fault;retained.worker=Worker();retained.lifetime=Lifetime();
  retained.image=imageBase;retained.modelEntry=ownedTransition.entry;retained.nonce=leaseNonce;
  retained.owned=seedTarget;retained.current=target;retained.lease=heldLease;
  ok=sleep_partner_carry_proof::Build(retained,first,GetTickCount64(),candidate)&&carrierSliceQuery(second)&&
     sleep_partner_carry_proof::SameSlice(first,second)&&
     sleep_partner_carry_proof::Build(retained,second,GetTickCount64(),after)&&sleep_partner_carry_proof::SameProof(candidate,after)&&
     carrierSliceQuery(last)&&sleep_partner_carry_proof::SameSlice(second,last);
  if(ok){candidate.issuedAt=GetTickCount64();ok=sleep_partner_carry_proof::Build(retained,last,candidate.issuedAt,after);if(ok)out=after;}
 }
 ReleaseSRWLockShared(&lock);return ok;
}
bool CarryLeaseProofCurrent(const CarryLeaseProof& proof) noexcept {
 const Preserve preserve;const auto now=GetTickCount64();
 if(!proof.leaseNonce||!proof.issuedAt||now<proof.issuedAt||now-proof.issuedAt>250)return false;
 CarryLeaseProof current{};
 return ReadCarryLeaseProof(current)&&sleep_partner_carry_proof::SameProof(proof,current);
}
bool ReadCarryPreparationProof(CarryPreparationProof& out) noexcept {
 const Preserve preserve;out={};if(!TryAcquireSRWLockShared(&lock))return false;
 hk::NativePoseJobContext c[3]{};Native a{},b{};CarryPreparationProof first{},last{};const char* why=nullptr;
 const auto retained=CarryRetained();bool ok=false;
 if(!pausedAt&&sleep_carry_context_proof::Ready(retained,GetTickCount64(),false)&&!Carry()&&
    hk::ReadNativePoseOwnedJobContext(c[0])&&c[0].thread==GetCurrentThreadId()&&ReadNative(c[0],false,a,why,false)&&
    hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(c[0].go),a.pool)&&
    sleep_carry_context_proof::Preparation(retained,c[0],a.proof,a.sample,first)&&
    hk::ReadNativePoseOwnedJobContext(c[1])&&sleep_carry_context_proof::SameJob(c[0],c[1])&&ReadNative(c[1],false,b,why,false)&&
    graph::Same(a.observed,b.observed)&&transition::Same(a.supplement,b.supplement)&&
    sleep_carry_context_proof::Preparation(retained,c[1],b.proof,b.sample,last)&&sleep_carry_context_proof::SamePreparation(first,last)&&
    hk::ReadNativePoseOwnedJobContext(c[2])&&sleep_carry_context_proof::SameJob(c[1],c[2])&&
    hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(c[2].go),b.pool)&&
    sleep_carry_context_proof::Ready(retained,GetTickCount64(),false)){out=last;ok=true;}
 ReleaseSRWLockShared(&lock);return ok;
}
bool CarryPreparationProofCurrent(const CarryPreparationProof& proof) noexcept {
 const Preserve preserve;const auto now=GetTickCount64();
 if(!proof.leaseNonce||!proof.issuedAt||now<proof.issuedAt||now-proof.issuedAt>250)return false;
 CarryPreparationProof current{};return ReadCarryPreparationProof(current)&&sleep_carry_context_proof::SamePreparation(proof,current);
}
bool BindDualPartsQuery(DualPartsQuery query) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 const bool ok=query&&configured&&!enabled&&!Active()&&!fault&&(!dualPartsQuery||dualPartsQuery==query);
 if(ok)dualPartsQuery=query;ReleaseSRWLockExclusive(&lock);return ok;
}
bool ReadDualPartsSleepCycle(DualPartsSleepCycleProof& out) noexcept {
 const Preserve preserve;out={};if(!TryAcquireSRWLockShared(&lock))return false;
 const auto retained=CarryRetained();hk::NativePoseDualPartsContext c[3]{};sleep_parts_graph::Sample a{},b{},last{};
 const auto read=[&](unsigned index,sleep_parts_graph::Sample& value)noexcept {
  if(!dualPartsQuery||!dualPartsQuery(c[index])||c[index].thread!=GetCurrentThreadId()||
     !sleep_carry_context_proof::SharedContext(retained,c[index],GetTickCount64()))return false;
  // Genuine current shared/even receipt supplies execution owner and sequence;
  // no old job, local0 named mask or odd writer is projected into this scope.
  sleep_parts_plugin::Scope scope{};scope.exactGate=configured;scope.admitted=target.pairedReady;scope.ready=scope.armed=true;
  scope.image=c[index].image;scope.player=c[index].player;scope.data=c[index].data;scope.state=c[index].state;
  scope.mission=target.mission;scope.ownedMission=seedTarget.mission;scope.epoch=target.worldEpoch;scope.ownedEpoch=seedTarget.worldEpoch;
  scope.generation=target.peerGeneration;scope.ownedGeneration=seedTarget.peerGeneration;
  scope.thread=c[index].thread;scope.ownerThread=GetCurrentThreadId();scope.sequenceBefore=c[index].sequence;scope.sequenceAfter=hk::WindowSeq();
  scope.now=GetTickCount64();scope.poll=target.poll;scope.batch=partner_defer::InBatch();
  if(!sleep_parts_graph::Read(Read,scope,value))return false;
  const auto& body=value.observed.owner.body[1];const auto& g=value.observed.graph[1];const auto& owner=heldLease.owner;
  // The seed owner thread is immutable bookkeeping, not execution admission.
  return body.block==owner.block&&body.ps==owner.ps&&body.lifeRecord==owner.life&&body.primary==owner.plugin&&body.node==owner.node&&
   body.records==owner.records&&body.bitmap==owner.bitmap&&g.cs==owner.cs&&g.controller==owner.graph&&g.entry==ownedTransition.entry&&
   g.mog==graphMog&&g.lifetime==graphLifetime&&body.lifeState==2&&
   std::isfinite(value.health)&&std::isfinite(value.maximumHealth)&&value.health>0&&value.health<=value.maximumHealth&&value.maximumHealth>=1&&value.maximumHealth<=65535&&
   std::isfinite(value.stun)&&value.stun>=0&&value.stun<=value.maximumStun&&lease::Bits(value.maximumStun)==heldLease.maximumStunBits;
 };
 bool ok=read(0,a)&&read(1,b)&&hk::SameNativePoseDualPartsPoll(c[0],c[1])&&sleep_parts_graph::Same(a,b)&&
   read(2,last)&&hk::SameNativePoseDualPartsPoll(c[1],c[2])&&sleep_parts_graph::Same(b,last);
 hk::NativePoseDualPartsContext final{};
 if(ok)ok=dualPartsQuery(final)&&final.thread==GetCurrentThreadId()&&hk::SameNativePoseDualPartsPoll(c[2],final)&&
   sleep_carry_context_proof::SharedContext(retained,final,GetTickCount64());
 if(ok){const auto& o=heldLease.owner;const auto& current=c[2];
  out.image=imageBase;out.player=o.player;out.go=current.go;out.data=o.data;out.life=o.life;out.block1=o.block;out.state1=o.ps;out.plugin=o.plugin;out.node=o.node;
  out.records=o.records;out.bitmap=o.bitmap;out.modelCS1=o.cs;out.modelEntry1=ownedTransition.entry;out.graph=o.graph;
  out.mission=o.mission;out.peerGeneration=o.generation;out.peerSequence=target.peerSequence;out.seedPeerSequence=seedTarget.peerSequence;out.localRole=target.localRole;out.peerCause=2;
  out.armGeneration=o.armGeneration;out.armEpoch=o.armEpoch;out.thread=current.thread;out.sequence=current.sequence;out.worldEpoch=o.epoch;out.leaseNonce=leaseNonce;
  out.birth1=o.birthSerial;out.carrierBirth0=current.birthSerial[0];out.sharedSerial=current.sharedSerial;out.carrySerial=current.carrySerial;
  out.acceptedAt=target.poll;out.lastNativeAt=heldLease.lastSeen;out.seededAt=heldLease.seededAt;out.observedAt=GetTickCount64();out.paused=pausedAt!=0;
 }
 ReleaseSRWLockShared(&lock);return ok;
}
bool BindDeferredCarryQuery(DeferredCarryQuery query) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 const bool ok=query&&configured&&!enabled&&!Active()&&!fault&&(!deferredCarryQuery||deferredCarryQuery==query);
 if(ok)deferredCarryQuery=query;ReleaseSRWLockExclusive(&lock);return ok;
}
bool ReadDeferredCarrySleepProof(DeferredCarrySleepProof& out) noexcept {
 const Preserve preserve;out={};if(!TryAcquireSRWLockShared(&lock))return false;
 const auto retained=CarryRetained();hk::NativePoseDeferredCarryContext context[3]{},final{};Native a{},b{};const char* why=nullptr;bool ok=false;
 if(deferredCarryQuery&&Carry()&&sleep_carry_context_proof::Ready(retained,GetTickCount64(),true)&&
    deferredCarryQuery(context[0])&&context[0].job.thread==GetCurrentThreadId()&&sleep_deferred_carry_proof::ContextCurrent(retained,context[0],GetTickCount64())&&
    ReadNative(context[0].job,false,a,why,false)&&hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(context[0].job.go),a.pool)&&
    sleep_deferred_carry_proof::Physical(retained,context[0].job,a.proof,a.sample)&&
    deferredCarryQuery(context[1])&&sleep_deferred_carry_proof::SameContext(context[0],context[1])&&sleep_deferred_carry_proof::ContextCurrent(retained,context[1],GetTickCount64())&&
    ReadNative(context[1].job,false,b,why,false)&&ro::SameScope(a.scope,b.scope)&&graph::Same(a.observed,b.observed)&&transition::Same(a.supplement,b.supplement)&&
    rest::SameRestSample(a.sample,b.sample,false)&&
    sleep_deferred_carry_proof::Physical(retained,context[1].job,b.proof,b.sample)&&
    deferredCarryQuery(context[2])&&sleep_deferred_carry_proof::SameContext(context[1],context[2])&&sleep_deferred_carry_proof::ContextCurrent(retained,context[2],GetTickCount64())&&
    hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(context[2].job.go),b.pool)&&
    deferredCarryQuery(final)&&sleep_deferred_carry_proof::SameContext(context[2],final)&&sleep_deferred_carry_proof::ContextCurrent(retained,final,GetTickCount64())){
  out.submitted=final.submission.submittedSleep;out.job=final.job;out.queue=final.queue;out.carrierRecord=final.carrierRecord;out.recipientRecord=final.recipientRecord;
  out.currentPeerSequence=target.peerSequence;out.submitNonce=final.submission.submitNonce;out.submittedAt=final.submission.submittedAt;
  out.intentNonce=final.intentNonce;out.intentAt=final.intentAt;out.birth0=final.birth0;out.birth1=final.birth1;out.currentAcceptedAt=target.poll;out.observedAt=GetTickCount64();
  out.priorSequence=final.priorSequence;out.copyReserved=final.copyReserved;ok=true;
 }
 ReleaseSRWLockShared(&lock);return ok;
}
bool DeferredCarrySleepProofCurrent(const DeferredCarrySleepProof& proof) noexcept {
 const Preserve preserve;const auto now=GetTickCount64();if(!proof.observedAt||now<proof.observedAt||now-proof.observedAt>250)return false;
 DeferredCarrySleepProof current{};return ReadDeferredCarrySleepProof(current)&&sleep_deferred_carry_proof::SameProof(proof,current);
}
bool BindQueuedCarryQuery(QueuedCarryQuery query) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 const bool ok=query&&configured&&!enabled&&!Active()&&!fault&&(!queuedCarryQuery||queuedCarryQuery==query);
 if(ok)queuedCarryQuery=query;ReleaseSRWLockExclusive(&lock);return ok;
}
bool ReadQueuedCarrySleepProof(QueuedCarrySleepProof& out) noexcept {
 const Preserve preserve;out={};if(!TryAcquireSRWLockShared(&lock))return false;
 const auto retained=CarryRetained();hk::NativePoseQueuedCarryContext context[3]{},final{};Native a{},b{};const char* why=nullptr;bool ok=false;
 if(queuedCarryQuery&&Carry()&&sleep_carry_context_proof::Ready(retained,GetTickCount64(),true)&&
    queuedCarryQuery(context[0])&&context[0].job.thread==GetCurrentThreadId()&&sleep_queued_carry_proof::ContextCurrent(retained,context[0],GetTickCount64())&&
    ReadNative(context[0].job,false,a,why,false)&&hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(context[0].job.go),a.pool)&&
    sleep_queued_carry_proof::Physical(retained,context[0],a.proof,a.sample)&&
    queuedCarryQuery(context[1])&&sleep_queued_carry_proof::SameContext(context[0],context[1])&&sleep_queued_carry_proof::ContextCurrent(retained,context[1],GetTickCount64())&&
    ReadNative(context[1].job,false,b,why,false)&&ro::SameScope(a.scope,b.scope)&&graph::Same(a.observed,b.observed)&&transition::Same(a.supplement,b.supplement)&&
    rest::SameRestSample(a.sample,b.sample,false)&&sleep_queued_carry_proof::Physical(retained,context[1],b.proof,b.sample)&&
    queuedCarryQuery(context[2])&&sleep_queued_carry_proof::SameContext(context[1],context[2])&&sleep_queued_carry_proof::ContextCurrent(retained,context[2],GetTickCount64())&&
    hk::RequireNativePoseOwnedJob(reinterpret_cast<void*>(context[2].job.go),b.pool)&&
    queuedCarryQuery(final)&&sleep_queued_carry_proof::SameContext(context[2],final)&&sleep_queued_carry_proof::ContextCurrent(retained,final,GetTickCount64())){
  out.submitted=final.submission.submittedSleep;out.job=final.job;out.queue=final.queue;out.carrierRecord=final.carrierRecord;out.recipientRecord=final.recipientRecord;
  out.message=final.message;out.result=final.result;out.service=final.service;out.currentPeerSequence=target.peerSequence;
  out.submitNonce=final.submission.submitNonce;out.submittedAt=final.submission.submittedAt;out.intentNonce=final.intentNonce;out.intentAt=final.intentAt;
  out.birth0=final.birth0;out.birth1=final.birth1;out.currentAcceptedAt=target.poll;out.observedAt=GetTickCount64();out.priorSequence=final.priorSequence;
  out.current=final.current;out.next=final.next;out.nativeAction=final.nativeAction;out.acknowledged=final.acknowledged;ok=true;
 }
 ReleaseSRWLockShared(&lock);return ok;
}
bool QueuedCarrySleepProofCurrent(const QueuedCarrySleepProof& proof) noexcept {
 const Preserve preserve;const auto now=GetTickCount64();if(!proof.observedAt||now<proof.observedAt||now-proof.observedAt>250)return false;
 QueuedCarrySleepProof current{};return ReadQueuedCarrySleepProof(current)&&sleep_queued_carry_proof::SameProof(proof,current);
}
Status GetStatus() noexcept {
 const Preserve preserve;Status out{};if(!TryAcquireSRWLockShared(&lock)){out.reason="busy";return out;}
 out=cached;ReleaseSRWLockShared(&lock);return out;
}
} // namespace mgscoop::sleep_partner_test
