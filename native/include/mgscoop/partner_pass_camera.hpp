#pragma once
// F1 vehicles, passenger camera (docs/native-puppet.md "Vehicle boarding",
// "Passenger camera"; pure part partner_pass_camera_policy.hpp). A stock
// back-seat passenger (act 0x6E) keeps the fallback camera module, whose
// collision ray hits the jeep. While the driver tick reports the local
// back-seat passenger, Connect writes that vehicle's stock ignore-collision
// handle to the local player's camera (CAM = *(P+0xB8): CAM+0x1F0 =
// 0x8800000000000000 | (0x6400 | idx), CAM+0x200 = 1, the VehicleCamera's own
// encoding), so every module's collision query (PlayerCamera vt+0xA8
// 0x1121D30) skips the jeep. Never while the VehicleCamera is the active module
// (the driver, the get-off: stock owns the field) and never over another
// owner. The stock get-off (VehicleCamera Deactivate) and the ride's
// FinishState clear it; Connect(on = false) clears only our own handle.
// No code or vtable is patched; nothing is called; every read and write is
// under SEH; the camera's vtable must be PlayerCamera.
//
// Install verifies the pinned blocks the connect relies on (contract
// pass_camera_blocks) once per process, after the partner code gate.
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_pass_camera_policy.hpp"

namespace mgscoop::partner_pass_camera {

using LogFn = void (*)(const char* line) noexcept;

// nullptr when every pinned block matches (also on a second call), else a
// static reason: base / contract / span / read / bytes (VerifyImage). Logs
// partner_pass_camera=installed|refused once.
const char* Install(const void* base, std::size_t span, LogFn log) noexcept;
bool Installed() noexcept;
const char* InstallRefusal() noexcept;

// The local player's camera (P+0xB8, vtable checked) for the driver's lines:
// CAM+0x1F0 / +0x1F8 / +0x200 / +0x340 / +0x33C and the VehicleCamera's index
// among the six modules. Reads only (SEH). False (ok false): unread.
bool ReadCamera(const void* base, std::uintptr_t P, Camera& out) noexcept;

// on: ConnectPlan, and the handle write when it says set; off: ReleasePlan, and
// the clear when it says clear. Returns the plan's static reason (connect /
// held / stock_module / other_owner / camera; ours / not_ours / ...) or "fault"
// when a write faulted. `out` is the camera read after the write. The driver
// calls it only after Install succeeded.
const char* Connect(const void* base, std::uintptr_t P, std::uint16_t vehicle, bool on, Camera& out) noexcept;

// Process-lifetime counters (the driver logs them).
struct Stats {
    std::uint32_t writes;   // handle writes (set)
    std::uint32_t clears;   // our handle cleared
    std::uint32_t faults;   // SEH-caught writes
    std::uint32_t angles;   // shot-guard angle writes (design 8.25)
};
void ReadStats(Stats& out) noexcept;

// Design 8.25: the lab trace's read of the local camera (partner_pass_camera_policy View: CAM+0xE0, +0x90,
// +0x268..0x274, +0x204, +0xD38). Reads only, under SEH; false when the camera is not the PlayerCamera.
bool ReadView(const void* base, std::uintptr_t P, View& out) noexcept;
// The current mode's module vtable rva (CAM+0xBD0[CAM+0x340]; 0 when unread): the trace's mvt.
std::uintptr_t ModeVtable(const void* base, std::uintptr_t P) noexcept;

// Design 8.25, the shot guard (mode aim): writes the local camera's look pitch to CAM+0x268 / +0x270 and its yaw to
// CAM+0x26C / +0x274 (the stock look update's order). Only to the PlayerCamera (vtable checked), under SEH; false on a
// fault (counted in Stats::faults: the driver latches the mission) or an unread camera. Counted in Stats::angles.
bool WriteAngles(const void* base, std::uintptr_t P, float pitch, float yaw) noexcept;

// The lab lever "passcam off|connect|aim|lab|status" (docs/native-puppet.md "Passenger camera"): this copy's mode
// override (-1: none, else a Mode). HandleCommand is false for any other text; for a passcam command it fills
// `result` and sets ok (false outside the lab or for bad arguments; nothing changes then).
int Override() noexcept;
bool HandleCommand(const char* text, bool lab, char* result, std::size_t capacity, bool& ok) noexcept;

namespace detail {
// The pinned blocks against an image of at least `span` bytes: nullptr or
// base / contract / span / read / bytes, with that block's rva in mismatch_rva.
const char* VerifyImage(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept;
void ResetForTest() noexcept;
} // namespace detail

} // namespace mgscoop::partner_pass_camera
