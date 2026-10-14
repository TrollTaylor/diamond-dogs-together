#pragma once
#include "mgscoop/native_pose_window.hpp"
namespace mgscoop::partner_input_hook {
struct NativePoseCarryQueueContext {
 NativePoseJobContext job{};
 std::uintptr_t root=0,slab=0,queued=0,snapshot=0;
 std::uint64_t bytes=0,carrySerial=0;
};
// Actual installed lifecycle patches only; not setter/transfer permission.
bool NativePoseCarryQueueLifetimeLive() noexcept;
// Selected1/odd owned job only; current immutable retained Q tuple is re-read.
bool ReadNativePoseCarryQueueContext(NativePoseCarryQueueContext&) noexcept;
} // namespace mgscoop::partner_input_hook
