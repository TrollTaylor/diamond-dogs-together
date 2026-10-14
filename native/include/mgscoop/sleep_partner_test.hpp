#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/native_pose_window.hpp"
#include "mgscoop/native_pose_dual_parts.hpp"
#include "mgscoop/native_pose_carry_queue.hpp"
namespace mgscoop::partner_input_hook {struct NativePoseDeferredCarryContext;struct NativePoseQueuedCarryContext;}
namespace mgscoop::sleep_partner_test {
// Private/copied-save experiment only. This interface makes no production
// downed/network promise. Target must be accepted fresh native/Lua peer state.
struct Target {
 bool pairedReady=false,fieldReady=false,demo=false,stopping=false,asleep=false;
 std::uint32_t mission=0,peerGeneration=0,armGeneration=0,armEpoch=0;
 std::uint16_t worldEpoch=0;std::uint64_t poll=0;
 std::uint32_t peerSequence=0,localRole=0,peerCause=0;
 bool peerDown=false,peerReady=false;
};
using LogFn=void(*)(const char*) noexcept;
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept;
bool Configure(const void* image,std::size_t span,bool exactRetailAndPartnerGate,
               const partner_input_hook::NativePoseWorkerBindings&,LogFn) noexcept;
void BindLifetimeQuery(bool(*actualWholePlayerDomainLive)() noexcept) noexcept;
void BindCarryHeldQuery(bool(*actualCarryRecipientHeld)() noexcept) noexcept;
bool PublishTarget(const Target&) noexcept; // fresh natural owner only, no retail call
bool Enable(bool) noexcept; // disable queues wake, never discards lease
bool RequestWake() noexcept; // queues; strict current odd-window CAS only
bool Before(const partner_input_hook::NativePoseJobContext&) noexcept;
bool After(const partner_input_hook::NativePoseJobContext&) noexcept;
void Closed(const partner_input_hook::NativePoseJobContext&,bool cleanupComplete) noexcept;
bool AllowPartsPoll() noexcept; // actual shared/even getter + fresh retained body1 read; no mutation/lease refresh
bool SuppressForcedStance() noexcept; // root/driver uses while preparing/leased; existing normal source behavior otherwise
struct Status {
 bool configured=false,enabled=false,active=false,held=false,wakePending=false,clearPending=false;
 std::uint32_t phase=0,mode=0,lifeState=0,qAction=0,stance=0,computedButtons=0,zero=0;
 std::uint32_t peerGeneration=0,window=0,requests=0;
 std::uint64_t birthSerial=0;const char* reason="off";
 bool stanceObserved=false;std::uint32_t psPacked=0,qStance=0,psAction=0;std::uint64_t stanceAt=0;
};
Status GetStatus() noexcept; // bounded cache, never raw pointers
// Internal native-only query, bound immutably before enable. The provider must
// invoke the installed actual carrier0 slice query; never fill it from status,
// a job context, a fabricated Scope or caller-controlled booleans. It must not
// re-enter this consumer. Distinct exclusive slice has real odd WindowSeq,
// P214/P218==0, clean preceding Closed and both participant lifetimes held.
struct CarrierSliceSnapshot {
 std::uintptr_t image=0,player=0,go=0,data=0,block1=0,state1=0,modelCS1=0,modelEntry1=0;
 std::uint32_t armGeneration=0,armEpoch=0,thread=0,sequence=0,realLocal=0,selected=0;
 std::uint64_t birth1=0,carrierBirth0=0,sliceNonce=0,carrySerial=0;
 bool sleepHeld=false;
};
using CarrierSliceQuery=bool(*)(CarrierSliceSnapshot&) noexcept;
bool BindCarrierSliceQuery(CarrierSliceQuery) noexcept;
// Ownership/authority snapshot only: native mode5/Q84/life2 and all returned
// native identities must independently match the carrier's selected0 reader.
// No cached active flag, native calls, selection override or mode permission.
struct CarryLeaseProof {
 std::uintptr_t image=0,player=0,go=0,data=0,life=0,block1=0,state1=0,plugin=0,node=0,records=0,bitmap=0,modelCS1=0,modelEntry1=0,graph=0;
 std::uint32_t mission=0,peerGeneration=0,peerSequence=0,seedPeerSequence=0,localRole=0,peerCause=0;
 std::uint32_t armGeneration=0,armEpoch=0,thread=0,sequence=0;
 std::uint16_t worldEpoch=0;
 std::uint64_t leaseNonce=0,birth1=0,carrierBirth0=0,sliceNonce=0,carrySerial=0;
 std::uint64_t acceptedAt=0,lastNativeAt=0,seededAt=0,issuedAt=0;
};
bool ReadCarryLeaseProof(CarryLeaseProof&) noexcept; // default-null provider refuses
bool CarryLeaseProofCurrent(const CarryLeaseProof&) noexcept; // query again in SAME slice before Start
// Separate genuine selected1 job receipt before dual Carry acquisition.
struct CarryPreparationProof {
 std::uintptr_t image=0,player=0,go=0,data=0,life=0,block1=0,state1=0,plugin=0,node=0,records=0,bitmap=0,modelCS1=0,modelEntry1=0,graph=0;
 std::uint32_t mission=0,peerGeneration=0,peerSequence=0,seedPeerSequence=0,localRole=0,peerCause=0,armGeneration=0,armEpoch=0,thread=0,sequence=0;
 std::uint16_t worldEpoch=0;std::uint64_t leaseNonce=0,birth1=0,acceptedAt=0,lastNativeAt=0,seededAt=0,issuedAt=0,graphHash=0;
};
bool ReadCarryPreparationProof(CarryPreparationProof&) noexcept;
bool CarryPreparationProofCurrent(const CarryPreparationProof&) noexcept;
// Separate real shared/even dual-parts query. This seals current accepted cause2
// authority, not a resting/carried label, native command or old writer Scope.
using DualPartsQuery=bool(*)(partner_input_hook::NativePoseDualPartsContext&) noexcept;
bool BindDualPartsQuery(DualPartsQuery) noexcept;
struct DualPartsSleepCycleProof {
 std::uintptr_t image=0,player=0,go=0,data=0,life=0,block1=0,state1=0,plugin=0,node=0,records=0,bitmap=0,modelCS1=0,modelEntry1=0,graph=0;
 std::uint32_t mission=0,peerGeneration=0,peerSequence=0,seedPeerSequence=0,localRole=0,peerCause=0,armGeneration=0,armEpoch=0,thread=0,sequence=0;
 std::uint16_t worldEpoch=0;std::uint64_t leaseNonce=0,birth1=0,carrierBirth0=0,sharedSerial=0,carrySerial=0,acceptedAt=0,lastNativeAt=0,seededAt=0,observedAt=0;
 bool paused=false;
};
bool ReadDualPartsSleepCycle(DualPartsSleepCycleProof&) noexcept;
// Distinct current selected1 writer + immutable completed op0 metadata.
// Neither selected0 nor submitted/shared snapshots supply this execution scope.
using DeferredCarryQuery=bool(*)(partner_input_hook::NativePoseDeferredCarryContext&) noexcept;
bool BindDeferredCarryQuery(DeferredCarryQuery) noexcept;
struct DeferredCarrySleepProof {
 CarryLeaseProof submitted{};
 partner_input_hook::NativePoseJobContext job{};
 partner_input_hook::NativePoseCarryQueueContext queue{};
 std::uintptr_t carrierRecord=0,recipientRecord=0;
 std::uint32_t currentPeerSequence=0;
 std::uint64_t submitNonce=0,submittedAt=0,intentNonce=0,intentAt=0,birth0=0,birth1=0,currentAcceptedAt=0,observedAt=0;
 std::uint8_t priorSequence=0;bool copyReserved=false;
};
bool ReadDeferredCarrySleepProof(DeferredCarrySleepProof&) noexcept;
bool DeferredCarrySleepProofCurrent(const DeferredCarrySleepProof&) noexcept;
// Distinct actual natural op1 selected1 handshake. Graph/action are re-read
// in the current queued phase; original submission remains immutable metadata.
using QueuedCarryQuery=bool(*)(partner_input_hook::NativePoseQueuedCarryContext&) noexcept;
bool BindQueuedCarryQuery(QueuedCarryQuery) noexcept;
struct QueuedCarrySleepProof {
 CarryLeaseProof submitted{};
 partner_input_hook::NativePoseJobContext job{};
 partner_input_hook::NativePoseCarryQueueContext queue{};
 std::uintptr_t carrierRecord=0,recipientRecord=0,message=0,result=0,service=0;
 std::uint32_t currentPeerSequence=0;
 std::uint64_t submitNonce=0,submittedAt=0,intentNonce=0,intentAt=0,birth0=0,birth1=0,currentAcceptedAt=0,observedAt=0;
 std::uint8_t priorSequence=0,current=0,next=0;std::uint16_t nativeAction=0;
 bool acknowledged=false; // completed reply, never physical attachment
};
bool ReadQueuedCarrySleepProof(QueuedCarrySleepProof&) noexcept;
bool QueuedCarrySleepProofCurrent(const QueuedCarrySleepProof&) noexcept;
} // namespace mgscoop::sleep_partner_test
