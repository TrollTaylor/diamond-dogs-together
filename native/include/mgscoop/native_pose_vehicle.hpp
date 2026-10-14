#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::partner_input_hook {
struct PoseSharedLease;
struct NativePoseVehicleLease {std::uint64_t serial=0;std::uint32_t generation=0,sceneEpoch=0;};
enum class NativePoseVehicleProtectionQuery : std::uint8_t { capture,current,parts,retired,cleanup };
inline constexpr std::uint32_t nativeVehicleAllocationGuard=1u,nativeVehicleCallbackGuard=2u,
 nativeVehicleUIGuard=4u,nativeVehicleCameraGuard=8u,nativeVehicleRegistryGuard=16u,
 nativeVehicleAllGuards=31u;
// Issued by the actual core writer or an existing actual shared ticket. The
// two births, model allocations, callback slabs and Q owner are independently
// reread by the core. No caller-supplied context opens a writer or shared read.
struct NativePoseVehicleOwnership {
 NativePoseVehicleLease lease{};
 std::uintptr_t image=0,player=0,go=0,data=0,state=0,vars=0;
 std::uintptr_t block[2]{},ps[2]{},cs[2]{},entry[2]{},modelAllocation[2]{},callbackSlab[2]{};
 std::uintptr_t controller[2]{},controllerNode[2]{},controllerStorage[2]{},controllerRecord[2]{};
 std::uint64_t birth[2]{},birthLifecycle=0;
 std::uintptr_t queueRoot=0,queueSlab=0,queueHead=0,queued=0,snapshot=0;
 std::uint64_t queueBytes=0;
 std::uint32_t thread=0,sequence=0,purposes=0,pending=0,pool[2]{},callbackCount[2]{},callbackMask[2]{};
 std::uint64_t sharedSerial=0;
 std::uint16_t vehicle=0xffff;
 std::uint16_t currentVehicle[2]{0xffff,0xffff},candidate[2]{0xffff,0xffff};
 std::uint8_t seat[2]{};
 std::uint8_t phase[2]{},nextPhase[2]{};
 std::uintptr_t nativeBinding[2]{};
};
// The module returns a typed receipt from its installed native lifecycle and
// UI hooks, not a configuration boolean. Core independently qualifies every
// owner/address below through exact retail final types and two pure reads.
// installationSerial/mask describe actual owned guards. They are immutable
// for the entire vehicle lease, including native exit and deferred clear.
struct NativePoseVehicleProtection {
 std::uint64_t installationSerial=0,leaseSerial=0;
 std::uint32_t generation=0,sceneEpoch=0,guardMask=0;
 std::uintptr_t image=0,player=0,data=0,world=0,registry=0;
 std::uintptr_t service=0,vehicleObject=0,records=0,bitmap=0,unit=0,metadata=0,variantTable=0,variant=0;
 std::uintptr_t vehicleSlab=0,vehicleHead=0;std::uint64_t vehicleBytes=0;
 std::uintptr_t resource=0,resourceInterface=0,resourceSlab=0,resourceHead=0;std::uint64_t resourceBytes=0;
 std::uintptr_t registryProvider=0,registryHolder=0,registryDescriptor=0,queryProxy=0;
 std::uintptr_t uiPrimary=0,uiInterface=0,uiProvider=0,uiProviderVT=0,uiGetter=0;
 std::uintptr_t cameraComponent=0,cameraHolder=0,cameraResource=0,cameraInterface=0;
 std::uint16_t vehicle=0xffff,allocated=0;std::uint8_t type=0,seats=0;
};
using NativePoseVehicleProtectionProvider=bool(*)(NativePoseVehicleProtectionQuery,
 const NativePoseVehicleOwnership&,NativePoseVehicleProtection&) noexcept;
struct NativePoseVehicleGuardBinding;
enum class NativePoseVehicleAllocatorDisposition : std::uint8_t { refuse,unchanged };
struct NativePoseVehicleProtectionBindings {
 NativePoseVehicleProtectionProvider provider=nullptr;
 // Only verified owned patch bytes may be overlaid; every other byte remains
 // actual. Core gates the complete existing vehicle controller contract.
 bool(*readOriginal)(std::uintptr_t,void*,std::size_t) noexcept=nullptr;
 // Core owns5C4910. This immutable pure pre-disposal classifier runs under
 // its real admission, before native child descriptor/reset/slab mutations.
 NativePoseVehicleAllocatorDisposition(*allocator)(const NativePoseVehicleGuardBinding&,
  std::uintptr_t header) noexcept=nullptr;
};
// One compiled native provider, bound before opt-in. No provider or incomplete
// actual guard receipt keeps native replica admission disabled.
bool RegisterNativePoseVehicleProtectionProvider(const NativePoseVehicleProtectionBindings&) noexcept;
bool AcquireNativePoseVehicleLease(std::uint16_t localVehicle,NativePoseVehicleLease&) noexcept;
bool NativePoseVehicleLeaseCurrent(const NativePoseVehicleLease&) noexcept;
// Current native cleanup ownership remains independent of fresh network drive
// authority. Faults or replaced native owners still refuse; no saved guard
// binding substitutes for this genuine owned writer/current receipt.
bool NativePoseVehicleCleanupCurrent(const NativePoseVehicleLease&) noexcept;
bool RequestNativePoseVehicleLeaseClear(const NativePoseVehicleLease&) noexcept;
struct NativePoseVehicleRequest {std::uint8_t phase=0,cycle=0,seat=4;};
// Exact native kind9 payload is constructed internally and copied through the
// installed setter's retained trampoline. Requires fresh native rider1
// occupancy on this retained local unit; no direct queue/seat/phase writes.
bool SubmitNativePoseVehicleRequest(const NativePoseVehicleLease&,const NativePoseVehicleRequest&) noexcept;
bool NativePoseVehicleHeld() noexcept;
const char* NativePoseVehicleRefusalName() noexcept; // bounded read-only diagnostic
bool ReadNativePoseVehicleOwnership(NativePoseVehicleOwnership&,const PoseSharedLease* actualShared=nullptr) noexcept;
struct NativePoseVehicleGuardBinding {
 NativePoseVehicleOwnership retained{};NativePoseVehicleProtection protection{};
 std::uint32_t purposes=0,pending=0;
};
// Retention-only: preserves the original immutable binding even after a
// fault or pending clear. Native hook guards may refuse disposal using it;
// it grants no pose/native-call/seat/exit or release permission.
bool ReadNativePoseVehicleGuardBinding(NativePoseVehicleGuardBinding&,const PoseSharedLease* actualShared=nullptr) noexcept;
struct NativePoseVehiclePartsContext {
 NativePoseVehicleOwnership ownership{};NativePoseVehicleProtection protection{};
 std::uint32_t callerRva=0,flags=0,dirty=0,status[2]{};
 std::uint8_t profileType=0,profileParts=0,profileCamo=0,profileEncoded=0,profileAux=0;
 std::uint16_t profileFace=0,profileSelector=0;std::uint32_t profileRefresh=0;
};
bool ReadNativePoseVehiclePartsPoll(NativePoseVehiclePartsContext&) noexcept;
} // namespace mgscoop::partner_input_hook
