#pragma once
#include "mgscoop/sleep_partner_test.hpp"
#include "sleep_partner_owned_read.hpp"
#include "sleep_partner_lease.hpp"
#include <limits>
namespace mgscoop::sleep_partner_controls {
using sleep_partner_test::Target;
namespace ro=sleep_partner_owned_read;namespace sl=sleep_partner_lease;
inline bool SamePeerCycle(const Target& a,const Target& b) noexcept {
 return a.mission==b.mission&&a.worldEpoch==b.worldEpoch&&a.peerGeneration==b.peerGeneration;
}
inline bool SameTargetScene(const Target& a,const Target& b) noexcept {
 return a.mission==b.mission&&a.worldEpoch==b.worldEpoch&&a.peerGeneration==b.peerGeneration&&a.armGeneration==b.armGeneration&&a.armEpoch==b.armEpoch;
}
inline bool FreshTarget(const Target& t,std::uint64_t now) noexcept {
 return t.pairedReady&&t.fieldReady&&!t.demo&&!t.stopping&&t.poll&&now>=t.poll&&now-t.poll<=250&&
 (t.mission==30010||t.mission==30020||t.mission==30050)&&t.worldEpoch&&t.peerGeneration&&t.armGeneration&&t.armEpoch;
}
// This is the actual adapter's immediate scalar-write predicate. It accepts
// no prior-window receipt, local-player masks, projected even sequence, or
// retained read-only admission. Actual typed owned-job validation follows it.
inline bool CasWindow(const partner_input_hook::NativePoseJobContext& c,const ro::Scope& captured,
                      const ro::Scope& current,bool zero,bool worker,bool lifetime,bool carry) noexcept {
 return worker&&lifetime&&!carry&&zero&&ro::SameScope(captured,current)&&current.sequenceAfter==c.sequence&&
 c.selectedBodyOwned&&c.selectedBody==1&&c.modelReady&&c.birthSerial&&current.birthSerial==c.birthSerial&&
 current.go==c.go&&current.block==c.block&&current.ps==c.state&&current.modelCS==c.modelCS&&
 current.modelEntry==c.modelEntry&&current.player==c.player&&current.thread==c.thread;
}
// A retained lease has one entry watchdog, before any early return or CAS.
// Successful first reads must not starve wake/release deadlines by refreshing
// lastSeen while a second read or exchange keeps refusing.
inline const char* Watchdog(bool preparing,std::uint64_t preparedAt,const sl::Lease& l,std::uint64_t now) noexcept {
 if(!preparing&&l.phase==sl::Phase::empty)return nullptr;
 if(!now||(preparing&&(!preparedAt||now<preparedAt))||
    (l.phase!=sl::Phase::empty&&(!l.seededAt||!l.lastSeen||now<l.seededAt||now<l.lastSeen))||
    (l.wakeAt&&now<l.wakeAt))return "partner_sleep_clock";
 if(preparing&&now-preparedAt>sl::kAcquireMs)return "partner_sleep_prepare_timeout";
 if(l.phase!=sl::Phase::empty){
  if(now-l.lastSeen>sl::kRetireMs)return "partner_sleep_observation_timeout";
  if(!l.nativeEntered&&now-l.seededAt>sl::kAcquireMs)return "partner_sleep_onset_timeout";
  if(l.wakeAt&&now-l.wakeAt>sl::kRetireMs)return "partner_sleep_wake_timeout";
 }return nullptr;
}
inline bool ResumeTimers(std::uint64_t pausedAt,std::uint64_t now,std::uint64_t& preparedAt,sl::Lease& l) noexcept {
 if(!pausedAt||now<pausedAt)return false;const auto pause=now-pausedAt;
 const auto max=std::numeric_limits<std::uint64_t>::max();
 if(preparedAt>max-pause||(l.seededAt&&l.seededAt>max-pause)||(l.wakeAt&&l.wakeAt>max-pause))return false;
 preparedAt+=pause;if(l.seededAt)l.seededAt+=pause;if(l.wakeAt)l.wakeAt+=pause;
 if(l.lastSeen)l.lastSeen=now;return true;
}
inline bool ReleaseSample(const sl::Lease& l,const sl::Scope& scope,const sl::Sample& a) noexcept {
 return l.phase==sl::Phase::retired&&l.wakeAuthorized&&!sl::ContextRefusal(scope,a)&&sl::Same(l.owner,a.owner)&&
 sl::SameMaximum(l,a)&&sl::RetiredSample(a);
}
// Closed carries only bookkeeping proof; it never calls an old native owner
// after the writer release. The independent fresh native release pair must
// already have been accepted while the writer was still held.
inline bool ClosedOwner(const partner_input_hook::NativePoseJobContext& c,const sl::Identity& owner,bool cleanup) noexcept {
 return cleanup&&c.sequence&&!(c.sequence&1u)&&c.thread==owner.ownerThread&&c.go+0x20==owner.player&&c.player==owner.player&&c.block==owner.block&&c.state==owner.ps&&
 c.modelCS==owner.cs&&c.birthSerial==owner.birthSerial&&c.generation==owner.armGeneration&&c.sceneEpoch==owner.armEpoch;
}
}
