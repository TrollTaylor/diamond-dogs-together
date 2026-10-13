#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_input.hpp"
namespace mgscoop::partner_input_hook {
enum class JobWindowOutcome : std::uint8_t { windowed,legacy,suppressed };
struct NativePoseJobContext {
 std::uintptr_t player=0,go=0,block=0,state=0,modelCS=0,modelEntry=0;
 std::uint32_t generation=0,sceneEpoch=0,thread=0,sequence=0;
 partner_input::Consumer consumer=partner_input::Consumer::drain0;
 std::uint32_t held=0,press=0,release=0;float magnitude=0;
 bool modelReady=false,selectedBodyOwned=false;
 std::uint32_t selectedBody=0;
 std::uintptr_t pp=0,source=0,windowPPVT=0;
 std::uint64_t birthSerial=0;
};
using NativePoseBeforeFn=bool(*)(const NativePoseJobContext&) noexcept;
using NativePoseAfterFn=bool(*)(const NativePoseJobContext&) noexcept;
using NativePoseClosedFn=void(*)(const NativePoseJobContext&,bool cleanupComplete) noexcept;
// Internal native consumers only; no Lua addresses or unchecked game calls.
// Closed runs only after Post/C0 restore/EndWindow/actual writer release.
// A false cleanup never clears a persistent lease.
void RegisterNativePoseJobHooks(NativePoseBeforeFn,NativePoseAfterFn,NativePoseClosedFn) noexcept;
struct NativePoseWorkerBindings {
 // Probe-owned callback freshly proves worker thread + copied saves.
 bool(*admitted)() noexcept=nullptr;
 void(*refuse)(const char*) noexcept=nullptr;
};
bool BindNativePoseWorker(const NativePoseWorkerBindings&) noexcept; // before install, immutable after
const char* InstallNativePoseWindow(const void* image,std::size_t span,bool fullRetailFingerprintPassed) noexcept;
// Worker/private/copied-save only. True opts into mandatory body1 windows.
// Off cannot discard a live test/feature requirement: restart owned lab process.
bool SetNativePoseWindowTest(bool enabled) noexcept;
// Actual bytes first; overlay only verified owned entry-JMP bytes from frozen
// contracts. Patch/relay drift returns false; all other bytes stay actual.
bool ReadNativePoseOriginalCode(std::uintptr_t at,void* out,std::size_t size) noexcept;
bool NativePoseRequiredForJob(void* go,std::uint32_t block) noexcept;
bool NativePoseOwnedWindow() noexcept;
// Fresh actual selected1 job receipt. Derived from the installed core writer,
// current Arm/model/birth/owned PP; no caller-manufactured context is accepted.
bool ReadNativePoseOwnedJobContext(NativePoseJobContext&) noexcept;
// Actual whole-GO deleting/reset hooks installed and owned patches still valid.
// False in synthetic fixtures; it cannot be enabled by a configuration bool.
bool NativePoseWholePlayerLifetimeLive() noexcept;
// Actual FF5510 export-filter entry/trampoline and owned current patch.
// This does not claim the hidden S0/S1/Warp isolation scenario has passed.
bool NativePoseLocalCacheIsolationLive() noexcept;
// Real current seven-writer hook ownership; cleanup retains faulted writer
// authority solely for exact unchanged pair-word restoration.
bool NativePoseVarsPairWordOwnedWindow() noexcept;
bool NativePoseVarsPairWordCleanupWindow(const NativePoseJobContext&) noexcept;
// Exact retained go/pool; refuses and faults a nested foreign job while owned.
// Never substitute the broader fault-inclusive Required predicate.
bool RequireNativePoseOwnedJob(void* go,std::uint32_t block) noexcept;
// Separate purposes: waking sleep cannot release an active native carry.
struct NativePoseCarryLease {std::uint64_t serial=0;std::uint32_t generation=0,sceneEpoch=0;};
bool AcquireNativePoseCarryRecipientLease(NativePoseCarryLease&) noexcept;
bool NativePoseCarryRecipientLeaseCurrent(const NativePoseCarryLease&) noexcept;
bool RequestNativePoseCarryRecipientLeaseClear(const NativePoseCarryLease&) noexcept;
bool NativePoseCarryRecipientHeld() noexcept;
bool SetNativePoseLeaseHeld() noexcept; // current admitted Before/After only
bool RequestNativePoseLeaseClear() noexcept; // deferred until successful cleanup
struct PoseSharedLease {bool tracked=false;std::uint32_t thread=0,slot=0;std::uint64_t serial=0;};
bool EnterNativePoseShared(PoseSharedLease&) noexcept;
void ExitNativePoseShared(PoseSharedLease&) noexcept;
JobWindowOutcome RunJobWindowOutcome(void* go,const void* param,std::uint32_t block,
 void(*job)(void*,const void*) noexcept,partner_input::Consumer consumer) noexcept;
struct NativePoseStatus {
 bool installed=false,required=false,held=false,faulted=false,owned=false;
 std::uint32_t windows=0,suppressed=0,hintsSuppressed=0;
 const char* reason="off";
 bool born1=false;std::uint64_t birthSerial=0; // completed typed receipt, current match checked again at opt-in
 std::uint32_t birthEntered=0,birthReady=0,birthExactCaller=0,birthBody1=0,birthModel=0,birthCompleted=0,birthOverlap=0;
 std::uint32_t birthCount=0,birthBase=0,birthCallerRva=0;const char* birthStage="never";
 std::uint32_t localExportsSuppressed=0;
 std::uint32_t cameraOwnedCalls=0,cameraSharedCalls=0,cameraOwnedCallerRva=0,cameraOwnedOwnerMatched=0;
 std::uint32_t cameraOwnedBody=UINT32_MAX; // census only; unknown caller sentinel
 std::uint32_t cameraOwnedSenderMatched=0,cameraOwnedFiltered=0;
};
// Read-only current model/birth census. A cold read grants no shared ticket,
// selected-body writer, native-call permission or pose-purpose ownership.
struct NativePoseBirthCensus {
 std::uintptr_t image=0,player=0,data=0,state=0,block[2]{},ps[2]{},cs[2]{},entry[2]{};
 std::uint64_t birth[2]{},lifecycle=0;
 std::uint32_t generation=0,sceneEpoch=0,thread=0,sequence=0;
};
bool ReadNativePoseBirthCensus(NativePoseBirthCensus&) noexcept;
struct NativePoseDualPartsContext;
// Passive private-worker observer after the real current dual-parts closure.
// This callback grants no writer, native-call, or lease-release authority.
using NativePoseDualReadObserver=void(*)(const NativePoseDualPartsContext&) noexcept;
bool RegisterNativePoseDualReadObserver(NativePoseDualReadObserver) noexcept;
NativePoseStatus NativePoseWindowStatus() noexcept;
namespace detail {
struct NativePoseTestBindings {bool enabled=false;void(*refuse)(const char*) noexcept=nullptr;};
void BindNativePoseForTest(const NativePoseTestBindings&) noexcept;
bool CompleteNativePoseBirthForTest(const partner_input::Arm&) noexcept;
bool ResetNativePoseForTest() noexcept; // synthetic fixture only; no gate reset
}
}
