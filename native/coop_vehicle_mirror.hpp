#pragma once
#include "vehicle_mirror_state.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "vehicle_native_protection.hpp"
namespace mgscoop::coop_vehicle_mirror {
using Log=void(*)(const char*) noexcept;
using Purpose=partner_input_hook::NativePoseVehicleLease;
struct Bindings {
 // Internal coordinator bindings, immutable after install. Link comes from
 // the authenticated live channel; fixed authored names are resolved locally.
 bool(*link)(vehicle_mirror_state::Link&) noexcept=nullptr;
 bool(*resolve)(vehicle_mirror_state::Name,std::uint16_t&) noexcept=nullptr;
 // Queue only into this actual expected incarnation under Session's tx lock.
 bool(*send)(const vehicle_mirror_state::Link&,const unsigned char*,std::size_t) noexcept=nullptr;
 // Original code view overlays only proved owned entry patches. It must cover
 // core, passenger, protection and this module through an acyclic chain.
 bool(*readOriginal)(std::uintptr_t,void*,std::size_t) noexcept=nullptr;
};
bool Install(const void* image,std::size_t span,bool exactRetailAccepted,const Bindings&,Log) noexcept;
bool SetEnabled(bool) noexcept; // private worker, defaults off; live disable refuses
void TickLocal() noexcept; // ordinary admitted coordinator thread, cold selected0
bool IsMessage(const unsigned char*,std::size_t) noexcept;
bool ReceiveAuthenticated(const vehicle_mirror_state::Link& authenticatedMessageLink,const unsigned char*,std::size_t,
                          std::uint64_t authenticatedAcceptedGetTickCount64) noexcept;
struct ReplicaRide {
 bool attached=false,driveAuthorized=false,exiting=false;
 vehicle_mirror_state::Name name{};vehicle_mirror_state::Link link{};
 std::uint8_t phase=0,seat=4;std::uint32_t mount=0;std::uint64_t observed=0;
 Purpose lease{};std::uintptr_t player=0,block=0;std::uint64_t birth=0;
};
// Current logical attachment receipt lets the coordinator suppress foot Warp
// and assign vehicle transform authority to the actual driver. No UI/seat
// claim is produced before the natural attachment callback was observed.
bool ReadReplicaRide(ReplicaRide&) noexcept;
// Read-only coordinator retention. A fresh authenticated mount intent keeps
// the existing native Arm available to Before; a real current vehicle lease
// keeps it through phase6 even after source expiry. Neither is an attachment.
struct NativeDemand {
 bool retained=false;Purpose lease{};
 std::uintptr_t player=0,data=0,state=0,block=0;std::uint64_t birth=0;
 std::uint32_t generation=0,sceneEpoch=0;std::uint8_t pool=0;
};
bool ReadNativeDemand(NativeDemand&) noexcept;
// Coordinator combines these with its existing sleep/carry Before/Closed.
// Before independently obtains the actual core-owned selected1 receipt.
bool Before() noexcept;
void Closed(const partner_input_hook::NativePoseJobContext&,bool cleanupComplete) noexcept;
// Existing passenger native-pose detour forwards only after stock1259B80,
// with the exact native caller captured before entering its trampoline.
void NativePoseAfter(std::uintptr_t primary,unsigned body,std::uintptr_t record,std::uintptr_t nativeCaller) noexcept;
bool StatusLine(char*,std::size_t) noexcept;
bool Shutdown() noexcept;
// Layer this on a real/core-original memory read in the coordinator's code
// view. It restores only this module's recorded, still-owned entry prefixes.
bool OverlayOriginal(std::uintptr_t at,void* bytes,std::size_t size) noexcept;
// Typed real-hook receipt; valid while installed even before private opt-in.
// It is independently reread by the protection provider. No configuration
// value can manufacture an installation serial, owned relay or native entry.
bool ReadUIEvent9Receipt(vehicle_native_protection::UIEvent9Receipt&) noexcept;
}
