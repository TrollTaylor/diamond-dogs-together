#pragma once
// F1 vehicles, passenger camera (docs/native-puppet.md "Vehicle boarding",
// "Passenger camera"; pure part partner_pass_camera_policy.hpp). The stock
// VehicleCamera module's CanActivate (0x12D0F60, true only for act 0x6F driver
// and 0x70 get-off) is reached only through slot +0x18 of its two vtables
// (0x23D67F0 the live module, 0x23D6720 its base); both slots are swapped
// (near_code::PatchQword, the partner_fire_filter slot pattern) to a wrapper
// that returns the stock answer, and also true for the local back-seat
// passenger the driver tick armed (act 0x6E on that vehicle, the module's own
// camera being the local player's). The stock module then activates, connects
// the vehicle (CAM+0x1F0 = 0x88..|id) and deactivates on its own; nothing here
// writes the camera or calls a game function besides the original CanActivate.
//
// Lazy install from the driver after the partner code gate passed (passenger
// mode only), once per process; never reverted (disarmed, the wrapper is the
// stock function). Every block it relies on is re-checked first (VerifyImage,
// contract pass_camera_blocks) and both slots must hold the original.
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_pass_camera_policy.hpp"

namespace mgscoop::partner_pass_camera {

using LogFn = void (*)(const char* line) noexcept;
using NowFn = std::uint64_t (*)() noexcept;

// nullptr when both slots point at the wrapper (also on a second call), else a
// static reason: base / contract / span / read / bytes (VerifyImage), slot_live
// or slot_base (a slot does not hold 0x12D0F60), a PatchQword reason for the
// live slot, or base_<reason> (the base slot failed; the live one was swapped
// back). Logs partner_pass_camera=installed|refused once.
const char* Install(const void* base, std::size_t span, NowFn now, LogFn log) noexcept;
bool Installed() noexcept;
const char* InstallRefusal() noexcept;
// Both slots still hold the wrapper (the driver checks it every tick).
bool PatchLive(const void* base) noexcept;
// The driver tick's armed word (Pack); 0 = off.
void Publish(std::uint64_t word) noexcept;
std::uint64_t Published() noexcept;

// Process-lifetime counters (the driver logs them; the wrapper only counts).
struct Stats {
    std::uint32_t calls;        // every CanActivate through the wrapper
    std::uint32_t stock_true;   // the stock answer was true (driver, get-off)
    std::uint32_t grants;       // our extension returned true (armed passenger)
    std::uint32_t refusals;     // armed, the live check said no
    std::uint32_t faults;       // SEH-caught reads / original call
    const char* last_reason;    // the last armed decision's reason (static)
    std::uint16_t last_act;     // the act it read
};
void ReadStats(Stats& out) noexcept;

// The local player's camera (P+0xB8, vtable checked) for the driver's lines:
// CAM+0x1F0 / +0x1F8 / +0x200 / +0x340 / +0x33C and the VehicleCamera's index
// among the six modules. Reads only (SEH). False (ok false): unread.
bool ReadCamera(const void* base, std::uintptr_t P, Camera& out) noexcept;

namespace detail {
// The pinned blocks against an image of at least `span` bytes: nullptr or
// base / contract / span / read / bytes, with that block's rva in mismatch_rva.
const char* VerifyImage(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept;
// Fixture seams (never called in the game): bind the original CanActivate,
// the clock and the expected camera / module vtables without patching; call
// the wrapper directly.
using CanActivateFn = bool (*)(void* module);
void BindForTest(CanActivateFn original, NowFn now, std::uintptr_t cam_vtable, std::uintptr_t module_vtable,
                 std::uintptr_t module_base_vtable) noexcept;
bool HookForTest(void* module) noexcept;
void ResetForTest() noexcept;
} // namespace detail

} // namespace mgscoop::partner_pass_camera
