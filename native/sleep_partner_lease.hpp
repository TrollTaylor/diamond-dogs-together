#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace mgscoop::sleep_partner_lease {
// Experimental worker-only pure plans. Caller owns exact typed reads and CAS;
// no native function, runtime memory read/write, input unlock or pose claim here.
inline constexpr std::uint64_t kAcquireMs=6000,kRetireMs=6000,kFreshMs=750;
struct Identity {
 std::uintptr_t player=0,data=0,life=0,block=0,ps=0,plugin=0,node=0,records=0,bitmap=0,cs=0,graph=0;
 std::uint32_t mission=0,generation=0,ownerThread=0,armGeneration=0,armEpoch=0; std::uint16_t epoch=0;std::uint64_t birthSerial=0;
};
inline bool Same(const Identity& a,const Identity& b) noexcept {
 return a.player==b.player&&a.data==b.data&&a.life==b.life&&a.block==b.block&&a.ps==b.ps&&a.plugin==b.plugin&&a.node==b.node&&a.records==b.records&&a.bitmap==b.bitmap&&a.cs==b.cs&&a.graph==b.graph&&a.mission==b.mission&&a.generation==b.generation&&a.ownerThread==b.ownerThread&&a.epoch==b.epoch&&a.armGeneration==b.armGeneration&&a.armEpoch==b.armEpoch&&a.birthSerial==b.birthSerial;
}
struct Sample {
 Identity owner{};
 float health=0,maximumHealth=0,stun=0,maximumStun=0;
 std::uint32_t lifeState=0;
 std::uint8_t mode=0,requested=0,action=0,stance=0,cause=0;
 std::uint16_t qAction=0;
 bool nodeActive=false,graphCurrent=false,graphSleepCandidate=false;
};
struct Scope {
 bool exactRetailGate=false,sleepCodeGate=false,seedCodeGate=false;
 bool workerActive=false,threadAdmitted=false,copiedSave=false,coherent=false;
 bool admitted=false,pairedOffline=false,batchBefore=false,batchAfter=false;
 bool selectedBodyOwned=false,ownedOddWindow=false,participantsLive=false,controlsZero=false;
 std::uint32_t thread=0,sequenceBefore=0,sequenceAfter=0;
 std::uint64_t now=0,poll=0;
};
inline bool ValidStun(const Sample& a) noexcept {return std::isfinite(a.stun)&&std::isfinite(a.maximumStun)&&a.maximumStun>=1.f&&a.maximumStun<=65535.f&&a.stun>=0.f&&a.stun<=a.maximumStun;}
inline const char* ContextRefusal(const Scope& s,const Sample& a) noexcept {
 if(!s.exactRetailGate||!s.sleepCodeGate||!s.seedCodeGate)return "sleep_seed_gate";
 if(!s.workerActive||!s.threadAdmitted||!s.copiedSave)return "sleep_seed_lab_only";
 if(!s.coherent||!s.selectedBodyOwned||!s.ownedOddWindow||!s.participantsLive||!s.thread||s.thread!=a.owner.ownerThread)return "sleep_seed_owner";
 if(s.batchBefore||s.batchAfter||!(s.sequenceBefore&1u)||s.sequenceBefore!=s.sequenceAfter)return "sleep_seed_window";
 if(!s.now||!s.poll||s.now<s.poll||s.now-s.poll>kFreshMs)return "sleep_seed_stale";
 if(!a.owner.birthSerial||!a.owner.armGeneration||!a.owner.armEpoch||!a.owner.epoch||!a.owner.generation||(a.owner.mission!=30010&&a.owner.mission!=30020&&a.owner.mission!=30050))return "sleep_seed_scene";
 if(!a.owner.player||!a.owner.data||!a.owner.life||(a.owner.life&3u)||!a.owner.block||!a.owner.ps||!a.owner.plugin||!a.owner.node||!a.owner.records||!a.owner.bitmap||!a.owner.cs||!a.owner.graph)return "sleep_seed_identity";
 if(!std::isfinite(a.health)||!std::isfinite(a.maximumHealth)||a.health<=0||a.health>a.maximumHealth||a.maximumHealth<1||a.maximumHealth>65535)return "sleep_seed_health";
 if(!ValidStun(a)||a.lifeState>2)return "sleep_seed_stun";
 if(!s.controlsZero||a.cause!=2)return "sleep_seed_controls";
 return nullptr;
}
// Mutation admission always requires the current actual owned odd-window zero-input proof.
inline const char* Refusal(const Scope& s,const Sample& a,bool newSeed) noexcept {
 if(const auto why=ContextRefusal(s,a))return why;
 if(newSeed&&(!s.admitted||!s.pairedOffline))return "sleep_seed_admission";
 return nullptr;
}
inline std::uint32_t Bits(float f) noexcept {std::uint32_t b=0;std::memcpy(&b,&f,4);return b;}
struct Exchange {std::uint32_t offset=0,expected=0,desired=0;};
struct Plan {bool valid=false;unsigned count=0;Exchange changes[2]{};const char* reason="sleep_seed_refused";};
enum class Phase:std::uint8_t {empty,seeded,active,wakePending,retired,quarantined};
struct Lease {
 Identity owner{};Phase phase=Phase::empty;
 std::uint32_t originalStunBits=0,maximumStunBits=0;std::uint64_t seededAt=0,wakeAt=0,lastSeen=0,retiredSeenAt=0;
 unsigned retiredSamples=0;std::uint32_t retiredWindow=0;bool nativeEntered=false,wakeAuthorized=false;
};
inline bool SameMaximum(const Lease& l,const Sample& a) noexcept {return Bits(a.maximumStun)==l.maximumStunBits;}
inline bool Held(const Lease& l) noexcept {return l.phase!=Phase::empty&&l.phase!=Phase::retired;}
// Every onset/rest/wake hash must exist in the same live typed graph cache.
// Node presence is an experiment prerequisite, never proof of loaded clips.
inline Plan Seed(const Scope& s,const Sample& a,const Lease& l,bool requestsPresent) noexcept {
 Plan p{};if(const auto why=Refusal(s,a,true)){p.reason=why;return p;}
 if(Held(l)||(l.phase==Phase::retired&&l.owner.generation==a.owner.generation)){p.reason="sleep_seed_existing_lease";return p;}
 if(!requestsPresent||!a.graphCurrent||a.graphSleepCandidate){p.reason="sleep_seed_graph";return p;}
 if(a.mode||a.requested||a.nodeActive||a.lifeState||a.stun<=0||a.stance!=1||a.action!=0x44||(a.qAction!=1&&a.qAction!=5)){p.reason="sleep_seed_not_standing_idle";return p;}
 p.valid=true;p.count=2;p.changes[0]={0x2c,Bits(a.stun),Bits(0.f)};p.changes[1]={0x34,0,2};p.reason="sleep_seed_two_cas";return p;
}
// Commit only after the adapter reports the two exact CAS results. A partial
// write keeps ownership and restrictions; never silently discard repair duty.
inline Lease Commit(const Sample& a,std::uint64_t now,bool stunCas,bool stateCas) noexcept {
 Lease l{};if(!stunCas)return l;l.owner=a.owner;l.originalStunBits=Bits(a.stun);l.maximumStunBits=Bits(a.maximumStun);l.seededAt=now;l.lastSeen=now;l.phase=stateCas?Phase::seeded:Phase::quarantined;return l;
}
inline Plan Repair(const Scope& s,const Sample& a,const Lease& l) noexcept {
 Plan p{};if(const auto why=Refusal(s,a,false)){p.reason=why;return p;}
 if(l.phase!=Phase::quarantined||!Same(l.owner,a.owner)){p.reason="sleep_seed_repair_owner";return p;}
 if(!SameMaximum(l,a)){p.reason="sleep_seed_repair_maximum_changed";return p;}
 // Native regrowth, another state, or admitted mode prevents restoring an old
 // scalar. Only our untouched zero in the same idle owner can be repaired.
 if(a.mode||a.requested||a.nodeActive||a.lifeState||Bits(a.stun)!=Bits(0.f)||a.action!=0x44||(a.qAction!=1&&a.qAction!=5)||a.stance!=1||!a.graphCurrent||a.graphSleepCandidate){p.reason="sleep_seed_repair_changed";return p;}
 p.valid=true;p.count=1;p.changes[0]={0x2c,Bits(0.f),l.originalStunBits};p.reason="sleep_seed_repair_cas";return p;
}
inline bool MarkRepaired(Lease& l,bool exactCasSucceeded) noexcept {
 if(l.phase!=Phase::quarantined||!exactCasSucceeded)return false;l.phase=Phase::retired;return true;
}
inline bool RequestWake(Lease& l,std::uint32_t generation,std::uint64_t now) noexcept {
 if(l.phase==Phase::empty||l.phase==Phase::quarantined||generation!=l.owner.generation||!now||now<l.lastSeen)return false;
 if(!l.wakeAt)l.wakeAt=now;l.wakeAuthorized=true;if(l.phase!=Phase::retired)l.phase=Phase::wakePending;return true;
}
inline Plan Wake(const Scope& s,const Sample& a,const Lease& l) noexcept {
 Plan p{};if(const auto why=Refusal(s,a,false)){p.reason=why;return p;}
 if(l.phase!=Phase::wakePending||!Same(l.owner,a.owner)){p.reason="sleep_wake_owner";return p;}
 if(!SameMaximum(l,a)){p.reason="sleep_wake_maximum_changed";return p;}
 if(a.lifeState==0){p.valid=true;p.reason="sleep_wake_natural_pending";return p;}
 if(a.mode!=5||!a.nodeActive||a.action!=0x19||a.qAction!=0x84||a.lifeState!=2||(a.requested!=0&&a.requested!=6)){p.reason="sleep_wake_wait_rest";return p;}
 p.valid=true;p.count=1;p.changes[0]={0x34,2,0};p.reason="sleep_wake_state_cas";return p;
}
struct Receipt {bool holdControls=false,suppressProne=false,retired=false,labAbortRequired=false;const char* reason="sleep_lease_absent";};
inline bool RetiredSample(const Sample& a) noexcept {
 return a.mode==0&&a.requested==0&&!a.nodeActive&&a.lifeState==0&&a.action!=0x19&&a.qAction!=0x83&&a.qAction!=0x84&&a.qAction!=0x85&&a.stance>=1&&a.stance<=3&&a.graphCurrent&&!a.graphSleepCandidate;
}
// An accepted early wake may retire an unseeded preparation. The adapter must
// independently prove the same typed owner/scope; capture maximum bits here too.
inline Lease RetireEmpty(const Sample& a,std::uint64_t now) noexcept {
 Lease l{};if(!now||!ValidStun(a)||!RetiredSample(a))return l;
 l.owner=a.owner;l.maximumStunBits=Bits(a.maximumStun);l.phase=Phase::retired;l.wakeAuthorized=true;l.lastSeen=now;return l;
}
// Each observation is a CURRENT strict owned odd-window zero-input proof.
// No projected even sequence, local0 named mask or old cap receipt is accepted.
inline Receipt Observe(Lease& l,const Scope& s,const Sample& a,std::uint32_t currentWindow) noexcept {
 Receipt r{};if(l.phase==Phase::empty)return r;r.holdControls=true;r.suppressProne=l.phase!=Phase::quarantined;
 const auto why=Refusal(s,a,false);
 const bool maximumChanged=!SameMaximum(l,a);
 if(why||currentWindow!=s.sequenceBefore||!Same(l.owner,a.owner)||maximumChanged||s.now<l.lastSeen){l.retiredSamples=0;l.retiredSeenAt=0;r.labAbortRequired=maximumChanged||(s.now>=l.lastSeen&&s.now-l.lastSeen>kRetireMs)||(l.wakeAt&&s.now>=l.wakeAt&&s.now-l.wakeAt>kRetireMs)||(!l.nativeEntered&&s.now>=l.seededAt&&s.now-l.seededAt>kAcquireMs);r.reason=why?why:maximumChanged?"sleep_lease_maximum_changed":"sleep_lease_changed";return r;}
 if(l.phase==Phase::retired){
  // A previous retirement receipt is not permission to unlock a later native
  // state. Re-entry or action/life/graph drift retains the lease and lock.
  if(!RetiredSample(a)){r.holdControls=true;r.suppressProne=true;r.labAbortRequired=true;r.reason="sleep_native_retired_drift";return r;}
  l.lastSeen=s.now;r.retired=true;r.holdControls=!l.wakeAuthorized;r.suppressProne=false;r.reason=l.wakeAuthorized?"sleep_native_retired":"sleep_native_retired_grant_pending";return r;
 }
 if(l.phase==Phase::quarantined){r.reason="sleep_seed_partial_repair_pending";r.labAbortRequired=true;return r;}
 l.lastSeen=s.now;
 // A committed seed must suppress forced Prone before the first natural
 // scheduler tick, or the standing onset would become the prone onset.
 r.suppressProne=true;
 if((a.mode==4||a.mode==5||a.mode==6)&&a.nodeActive&&a.action==0x19&&(a.qAction==0x83||a.qAction==0x84||a.qAction==0x85)){
  l.nativeEntered=true;if(l.phase==Phase::seeded)l.phase=Phase::active;
 }
 if(l.nativeEntered&&RetiredSample(a)){
  if(!l.retiredSamples||currentWindow!=l.retiredWindow){++l.retiredSamples;l.retiredSeenAt=s.now;l.retiredWindow=currentWindow;}
  if(l.retiredSamples>=2){l.phase=Phase::retired;r.holdControls=!l.wakeAuthorized;r.suppressProne=false;r.retired=true;r.reason="sleep_native_retired";return r;}
  r.reason="sleep_native_retire_second_sample";return r;
 }
 l.retiredSamples=0;l.retiredSeenAt=0;
 const bool acquireExpired=!l.nativeEntered&&s.now-l.seededAt>kAcquireMs;
 const bool retireExpired=l.wakeAt&&s.now>=l.wakeAt&&s.now-l.wakeAt>kRetireMs;
 r.labAbortRequired=acquireExpired||retireExpired;r.reason=r.labAbortRequired?"sleep_native_lab_abort_required":l.phase==Phase::wakePending?"sleep_native_retire_pending":"sleep_native_locked";return r;
}
} // namespace mgscoop::sleep_partner_lease
