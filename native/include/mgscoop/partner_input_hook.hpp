#pragma once
// Round 3 input lever, Win32 part (docs/partner-anim-appearance.md "Round 3"):
// a near stub and rel32 patch at the block-1 dispatch call 0x9C2B51, the hook
// itself, the seqlock the driver publishes the Arm through, and the drained
// statistics. The portable pad logic is partner_input; the pure reads that
// build the Arm are partner_player. Lock order and threading: the driver runs
// on the Lua tick thread (publishes the Arm, drains stats); the hook runs on
// whatever thread runs the player update 0x9C1F00 and never blocks or takes a
// lock other than the non-blocking SRW the sample uses.
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_input.hpp"

namespace mgscoop::partner_input_hook {

using LogFn = void (*)(const char* line) noexcept;
// partner_net::LatestInput: false when the lock is busy (reuse the previous).
using SampleFn = bool (*)(partner_input::Sample& out) noexcept;
// 0xBF61D0(root, phase) and the PP hold classifier 0x1205C30(PP, btn, frames).
using DispatchFn = void (*)(void* root, std::uint32_t phase) noexcept;
using HoldFn = std::uint32_t (*)(void* pp, std::uint32_t btn, std::uint32_t frames) noexcept;

// Installs once per process (idempotent; the patch and stub are never removed,
// the proxy DLL never unloads). `base` is the image base. Returns nullptr on
// success, otherwise a static reason (also logged):
//   bytes | allocate_near | range | protect | exchange | vt_copy | disabled.
const char* Install(const void* base, LogFn log, SampleFn sample) noexcept;
bool Installed() noexcept;
// The two per-window vtable copies (hold none / hold log); 0 before install.
// They are only ever live inside a window: the Lua-thread reads refuse them.
void VtableCopies(std::uintptr_t out[2]) noexcept;
// Odd while a window (or the block-0 hold log) has fields swapped in; it
// changes on every window. Lua-thread readers of PP/src retry or refuse
// unless it is even and unchanged across their read.
std::uint32_t WindowSeq() noexcept;
// Clears a recorded fault so the window may run again (MissionStart only;
// the driver keeps its own per-mission input_faulted).
void ResetFault() noexcept;

// The driver publishes this each tick (seqlock: odd, copy, even). Copied by
// value; pointers are read inside the hook under SEH and re-validated.
void PublishArm(const partner_input::Arm& arm) noexcept;

// Drained by the driver. Counters are process-lifetime totals (the driver
// logs deltas); us_max is cleared by each drain.
struct Stats {
    partner_input::WindowStats window;
    partner_input::HoldRecord holds[8];
    unsigned hold_count;
};
void DrainStats(Stats& out) noexcept;
// True while the 5 bytes at 0x9C2B51 still call our stub (1 Hz check).
bool PatchLive(const void* base) noexcept;

namespace detail {
// Test seams (also used by Install). BlockMatches ignores the retargetable
// rel32 operand; BuildStub writes the 14-byte absolute jmp.
bool BlockMatches(const unsigned char* block) noexcept;
void BuildStub(unsigned char out[14], const void* target) noexcept;
std::size_t BlockSize() noexcept;
}

} // namespace mgscoop::partner_input_hook
