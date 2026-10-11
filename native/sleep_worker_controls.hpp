#pragma once
#include <cstdint>
#include "sleep_body0_lease.hpp"
namespace mgscoop::sleep_worker_controls {
inline constexpr std::uint32_t kDownedName=0x6786ae0bu; // exact existing Fox.StrCode32 proof
struct Entry {std::uint32_t name=0,bits=0;};
struct Masks {Entry table[3][8]{};std::uint32_t aggregate[3]{};};
inline bool NamedFull(const Masks& m) noexcept {
 bool buttons=false,triggers=false;
 for(unsigned t=0;t<3;++t){std::uint32_t combined=0;for(unsigned i=0;i<8;++i){const auto& e=m.table[t][i];if(!e.name)continue;
  for(unsigned j=0;j<i;++j)if(m.table[t][j].name==e.name)return false;
  combined|=e.bits;if(e.name==kDownedName){if(t==0)buttons=e.bits==0xffffffffu;if(t==1&&e.bits)return false;if(t==2)triggers=e.bits==3;}
 }if(combined!=m.aggregate[t])return false;}
 return buttons&&triggers;
}
struct Receipt {
 std::uintptr_t player=0,data=0,life=0,pad=0,source=0;
 std::uint32_t generation=0,thread=0,sequence=0;std::uint64_t at=0,serial=0;
 bool zero=false;
};
inline bool FreshZero(const Receipt& a,const Receipt& current,std::uint64_t now) noexcept {
 return a.zero&&a.serial&&a.at&&now>=a.at&&now-a.at<=750&&a.player==current.player&&a.data==current.data&&a.life==current.life&&a.pad==current.pad&&a.source==current.source&&a.generation==current.generation&&a.thread==current.thread&&a.sequence==current.sequence&&!(a.sequence&1u);
}
// Retention proves only current read-only suppression/lease freshness after a
// committed seed. The exact FreshZero sequence comparison above stays intact.
struct RetentionInputs {
 float magnitudeBefore=1.f,magnitudeAfter=1.f;
 bool namedMasksBefore=false,namedMasksAfter=false;
 std::uint32_t sequenceBefore=0,sequenceAfter=0;
 bool batchBefore=false,batchAfter=false;
 std::uint64_t now=0;
};
inline sleep_body0_lease::ObservationProof RetainedObservation(const Receipt& a,const Receipt& current,const RetentionInputs& in) noexcept {
 namespace sl=sleep_body0_lease;
 if(!a.zero||!a.serial||!a.at)return {false,"sleep_observe_zero_receipt_missing"};
 if(!in.now||in.now<a.at||in.now-a.at>sl::kFreshMs)return {false,"sleep_observe_zero_receipt_stale"};
 if(!a.player||!a.data||!a.life||!a.pad||!a.source||!a.generation||!a.thread||a.player!=current.player||a.data!=current.data||a.life!=current.life||a.pad!=current.pad||a.source!=current.source||a.generation!=current.generation||a.thread!=current.thread)return {false,"sleep_observe_zero_owner_changed"};
 if((a.sequence&1u)||(in.sequenceBefore&1u)||in.sequenceBefore!=in.sequenceAfter||current.sequence!=in.sequenceBefore||current.sequence<a.sequence||in.batchBefore||in.batchAfter)return {false,"sleep_observe_zero_window"};
 if(!in.namedMasksBefore||!in.namedMasksAfter)return {false,"sleep_observe_zero_named_masks"};
 if(sl::Bits(in.magnitudeBefore)!=sl::Bits(0.f)||sl::Bits(in.magnitudeAfter)!=sl::Bits(0.f))return {false,"sleep_observe_zero_current_nonzero"};
 return {true,"sleep_observe_retained_zero",current.player,current.data,current.life,current.generation,current.thread,current.sequence,in.now};
}
// Early read/CAS returns share the same bounded lease deadline as Observe.
inline bool PendingExpired(const sleep_body0_lease::Lease& l,bool prepared,std::uint64_t downAt,std::uint64_t now) noexcept {
 namespace sl=sleep_body0_lease;
 if(l.phase==sl::Phase::empty)return prepared&&now>=downAt&&now-downAt>sl::kAcquireMs;
 return (!l.nativeEntered&&l.seededAt&&now>=l.seededAt&&now-l.seededAt>sl::kAcquireMs)||
  (l.wakeAt&&now>=l.wakeAt&&now-l.wakeAt>sl::kRetireMs)||
  (l.lastSeen&&now>=l.lastSeen&&now-l.lastSeen>sl::kRetireMs);
}
inline bool CasWindow(const sleep_body0_lease::Scope& sampled,std::uint32_t owner,std::uint32_t thread,std::uint32_t currentSequence,bool batch,bool worker) noexcept {
 return sampled.analogCapQualified&&sampled.namedButtonsLocked&&sampled.namedTriggersLocked&&owner&&owner==thread&&sampled.thread==owner&&worker&&!batch&&!(currentSequence&1u)&&
  sampled.sequenceBefore==sampled.sequenceAfter&&currentSequence==sampled.sequenceAfter;
}
// The final release is an independent read-only proof after the real named
// masks have been removed. Never invent locks/cap to call the lease observer.
inline bool ReleaseSample(const sleep_body0_lease::Lease& l,
 const sleep_body0_lease::Scope& s,const sleep_body0_lease::Sample& a) noexcept {
 namespace sl=sleep_body0_lease;
 return l.phase==sl::Phase::retired&&l.wakeAuthorized&&sl::Same(l.owner,a.owner)&&
  s.exactRetailGate&&s.sleepCodeGate&&s.seedCodeGate&&s.workerActive&&s.threadAdmitted&&s.copiedSave&&s.coherent&&
  s.thread&&s.thread==a.owner.ownerThread&&!s.batchBefore&&!s.batchAfter&&!(s.sequenceBefore&1u)&&s.sequenceBefore==s.sequenceAfter&&
  s.now&&s.poll&&s.now>=s.poll&&s.now-s.poll<=sl::kFreshMs&&s.now>=l.lastSeen&&
  a.owner.epoch&&a.owner.generation&&(a.owner.mission==30010||a.owner.mission==30020||a.owner.mission==30050)&&
  a.owner.player&&a.owner.data&&a.owner.life&&!(a.owner.life&3u)&&a.owner.block&&a.owner.ps&&a.owner.plugin&&a.owner.node&&a.owner.records&&a.owner.bitmap&&a.owner.cs&&a.owner.graph&&
  std::isfinite(a.health)&&std::isfinite(a.maximumHealth)&&a.health>0&&a.health<=a.maximumHealth&&a.maximumHealth>=1&&a.maximumHealth<=65535&&
  sl::ValidStun(a)&&sl::SameMaximum(l,a)&&a.cause==2&&sl::RetiredSample(a);
}
}
