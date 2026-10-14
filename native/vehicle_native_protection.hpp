#pragma once
#include <cstddef>
#include <cstdint>
#include "mgscoop/native_pose_vehicle.hpp"
namespace mgscoop::vehicle_native_protection {
// Exported only by the compiled vehicle-mirror module after its actual hook
// have been installed. Every field is rechecked here. The existing event9
// filter wraps the original flags3 method; it does not clone that method.
// There is no caller-controlled `live` or `enabled` boolean in this receipt.
struct UIEvent9Receipt {
 std::uint64_t installationSerial=0;
 std::uintptr_t image=0,entry=0,detour=0,relay=0;
 unsigned char entryBytes[16]{},relayBytes[14]{};
};
struct Bindings {
 bool(*event9)(UIEvent9Receipt&) noexcept=nullptr;
 void(*fault)(const char*) noexcept=nullptr;
};
// Explicit private-worker installation only. This module never installs on
// load, enables a seat adapter, acquires a purpose, or issues a game request.
bool Install(const void* image,std::size_t span,bool exactRetailAccepted,const Bindings&) noexcept;
bool Installed() noexcept;
// Real owned entry/relay/trampoline bytes, the core5C4910 composition and the
// external event9 entry wrapper must all remain current.
bool ProtectionLive() noexcept;
bool ReadOriginal(std::uintptr_t,void*,std::size_t) noexcept;
bool StatusLine(char*,std::size_t) noexcept;
}
