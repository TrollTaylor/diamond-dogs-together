#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cmath>
#include <cstring>
#include <cstdio>
#include "mgscoop/sleep_partner_test.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_player.hpp"
#include "sleep_partner_owned_graph.hpp"
#include "sleep_partner_lease.hpp"
#include "sleep_partner_controls.hpp"
#include "sleep_partner_transition_read.hpp"
#include "sleep_loaded_requests.hpp"
#include "sleep_seed_contract.hpp"
#include "sleep_body0_transition_contract.hpp"

namespace mgscoop::sleep_partner_test {
namespace {
namespace hk=partner_input_hook;namespace ro=sleep_partner_owned_read;
namespace graph=sleep_partner_owned_graph;namespace lease=sleep_partner_lease;namespace policy=sleep_partner_controls;namespace transition=sleep_partner_transition_read;
SRWLOCK lock=SRWLOCK_INIT;
std::uintptr_t imageBase=0;
hk::NativePoseWorkerBindings worker{};LogFn logger=nullptr;
bool(*wholeLifetime)() noexcept=nullptr;bool(*carryHeld)() noexcept=nullptr;
bool configured=false,enabled=false,preparing=false,wakeRequested=false,clearPending=false,fault=false;
Target target{},ownedTarget{},completedTarget{};bool completedCycle=false;
lease::Lease heldLease{};lease::Sample preparedOwner{};
std::uintptr_t graphMog=0,graphLifetime=0;transition::Observation ownedTransition{};
std::uint64_t preparedAt=0,pausedAt=0;
unsigned emptyRetiredSamples=0;std::uint32_t emptyRetiredWindow=0;
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
};
bool ReadNative(const hk::NativePoseJobContext& c,bool requests,Native& out,const char*& why) noexcept {
 out={};cached.stanceObserved=false;const auto s=Scope(c);why="partner_sleep_owned_scope";
 if(const auto refused=ro::ScopeRefusal(s)){why=refused;return false;}
 graph::Observation a{},b{},after{};transition::Observation ta{},tb{},tc{};
 if(!graph::ReadOnce(Read,s,a,&why)||!transition::ReadOnce(Read,s,ta,&why))return false;
 const auto middle=Scope(c);if(!ro::SameScope(s,middle)){why="partner_sleep_scope_changed";return false;}
 if(!graph::ReadOnce(Read,middle,b,&why)||!transition::ReadOnce(Read,middle,tb,&why))return false;
 if(!graph::Same(a,b)||!transition::Same(ta,tb)){why="partner_sleep_pair_changed";return false;}
 sleep_loaded_requests::Reply names{};
 if(requests&&!sleep_loaded_requests::Read(Read,b.graph[1],names)){why=names.reason;return false;}
 const auto finalScope=Scope(c);if(!ro::SameScope(s,finalScope)){why="partner_sleep_scope_changed";return false;}
 if(!graph::ReadOnce(Read,finalScope,after,&why)||!transition::ReadOnce(Read,finalScope,tc,&why))return false;
 if(!graph::Same(b,after)||!transition::Same(tb,tc)){why="partner_sleep_final_changed";return false;}
 const auto completed=Scope(c);if(!ro::SameScope(s,completed)){why="partner_sleep_scope_changed";return false;}
 out.scope=completed;out.observed=after;out.supplement=tc;out.pool=after.owner.poolIndex[1];out.requestsPresent=requests&&names.ready&&names.found==0xffff;
 const auto& body=after.owner.body[1];const auto& g=after.graph[1];const auto& value=after.owner.values[1];
 out.sample.owner={completed.player,completed.data,body.lifeRecord,body.block,body.ps,body.primary,body.node,
 body.records,body.bitmap,g.cs,g.controller,completed.mission,completed.generation,completed.thread,
 completed.armGeneration,completed.armEpoch,completed.epoch,completed.birthSerial};
 out.sample.health=value.health;out.sample.maximumHealth=value.maximumHealth;out.sample.stun=value.stun;out.sample.maximumStun=value.maximumStun;
 out.sample.lifeState=body.lifeState;out.sample.mode=body.mode;out.sample.requested=body.requested;out.sample.action=body.action;
 out.sample.qAction=body.qAction;out.sample.stance=value.psStance;out.sample.cause=2;out.sample.nodeActive=body.nodeActive;
 out.sample.graphCurrent=g.currentNode;out.sample.graphSleepCandidate=g.sleepHashCandidate;
 out.zero=ro::Zero(after.owner.pad);
 out.proof.exactRetailGate=out.proof.sleepCodeGate=out.proof.seedCodeGate=configured;
 out.proof.workerActive=out.proof.threadAdmitted=out.proof.copiedSave=Worker();out.proof.coherent=true;
 out.proof.admitted=out.proof.pairedOffline=completed.pairedReady;
 out.proof.selectedBodyOwned=completed.selectedBodyOwned;out.proof.ownedOddWindow=true;
 out.proof.participantsLive=completed.participantsLive;out.proof.controlsZero=out.zero;
 out.proof.thread=completed.thread;out.proof.sequenceBefore=completed.sequenceBefore;out.proof.sequenceAfter=completed.sequenceAfter;
 out.proof.batchBefore=out.proof.batchAfter=false;out.proof.now=completed.now;out.proof.poll=completed.poll;
 cached.mode=body.mode;cached.lifeState=body.lifeState;cached.qAction=body.qAction;cached.stance=value.psStance;
 cached.psPacked=value.psPacked;cached.qStance=value.qStance;cached.psAction=body.action;cached.stanceObserved=true;cached.stanceAt=completed.now;
 cached.computedButtons=after.owner.pad.computedButtons;cached.zero=out.zero?1u:0u;cached.window=c.sequence;
 cached.birthSerial=c.birthSerial;cached.peerGeneration=completed.generation;cached.requests=names.found;
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
 preparedOwner=n.sample;ownedTarget=target;graphMog=n.observed.graph[1].mog;graphLifetime=n.observed.graph[1].lifetime;
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
bool Step(const hk::NativePoseJobContext& c,bool before) noexcept {
 if(fault)return false;
 if(!Active()&&(!enabled||!target.asleep||(completedCycle&&policy::SamePeerCycle(target,completedTarget))))return true;
 if(!Worker()||!Lifetime()){if(Active())Abort("partner_sleep_lifetime_or_worker");else Reason("partner_sleep_lifetime_or_worker");return !Active();}
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
  if(now<preparedAt||now-preparedAt>lease::kAcquireMs){Abort("partner_sleep_prepare_timeout");return false;}
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
  const bool first=Cas(c,n,plan.changes[0]);heldLease=lease::Commit(n.sample,n.scope.now,first,false);
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
} // namespace
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 const Preserve preserve;const auto base=reinterpret_cast<std::uintptr_t>(image);
 auto reader=hk::ReadNativePoseOriginalCode;
 return sleep_plugin_read::CodeGate(reader,base,span,mismatch)&&sleep_graph_read::CodeGate(reader,base,span,mismatch)&&
        sleep_seed_contract::CodeGate(reader,base,span,mismatch)&&sleep_body0_transition_contract::CodeGate(reader,base,span,mismatch)&&
        sleep_partner_stance_contract::CodeGate(reader,base,span,mismatch);
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
   completedTarget=ownedTarget;completedCycle=true;preparing=false;heldLease={};preparedOwner={};clearPending=false;wakeRequested=false;preparedAt=0;
   graphMog=graphLifetime=0;ownedTransition={};emptyRetiredSamples=0;emptyRetiredWindow=0;Reason("partner_sleep_retired");
  }
 }else if(Active()&&!cleanup)Abort("partner_sleep_window_cleanup_failed");
 ReleaseSRWLockExclusive(&lock);
}
bool SuppressForcedStance() noexcept {
 const Preserve preserve;if(!TryAcquireSRWLockShared(&lock))return true;
 const bool suppress=Active();ReleaseSRWLockShared(&lock);return suppress;
}
Status GetStatus() noexcept {
 const Preserve preserve;Status out{};if(!TryAcquireSRWLockShared(&lock)){out.reason="busy";return out;}
 out=cached;ReleaseSRWLockShared(&lock);return out;
}
} // namespace mgscoop::sleep_partner_test
