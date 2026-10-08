#pragma once
// Round 3/4 input lever, Win32 part (docs/partner-anim-appearance.md "Round 3"
// and "Round 4"): a near stub and rel32 patch at the block-1 dispatch call
// 0x9C2B51, the hook itself, the seqlock the driver publishes the Arm through,
// and the drained statistics. The portable pad logic is partner_input; the
// pure reads that build the Arm are partner_player. Lock order and threading:
// the driver runs on the Lua tick thread (publishes the Arm, drains stats); the
// hook runs on whatever thread runs the player update 0x9C1F00 and never
// blocks or takes a lock other than the non-blocking SRW the sample uses.
//
// Round 4 additions:
// - Every window installs a per-window PP vtable copy whose key-manager slots
//   (+0xE8..+0x138) answer from the injected words instead of the host
//   keyboard, and whose hold classifiers (+0x90, +0x140) follow hold=none|
//   pass|own|net (own: partner_input::ClassifyHold over a history of what
//   instance 1 was fed, one entry per main window).
// - PP+0x278 (mouse-look gate) and PP+0x4B0 (rebind request) are shadowed:
//   instance 1 sees and writes its own copy, the host keeps its value.
// - wrap=census|window: pass-through wrappers on the other walks of the block
//   trees (UpdateTree service slots +0x28/+0x30/+0x40, calls 0x9C1DA3 and
//   0x9C1AFF) that count block-1 calls per phase/thread/nesting, and with
//   wrap=window run the same window (the main window's injection of this
//   frame) around block 1's calls on the hook thread.
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_input.hpp"

namespace mgscoop::partner_input_hook {

using LogFn = void (*)(const char* line) noexcept;
// partner_net::ConsumeInput: false when the lock is busy (reuse the previous
// level fields, no edges).
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
// The wrap entries (wrap=census|window), installed lazily once per process
// after Install; never removed (with wrap=off the wrappers only forward).
// nullptr on success, otherwise a static reason (also logged).
const char* InstallWrap() noexcept;
bool WrapInstalled() noexcept;
// The per-window vtable copies (hold none / hold log / own+net / pass); 0
// before install. They are only ever live inside a window: the Lua-thread
// reads refuse them.
inline constexpr std::size_t kVtableCopies = 4;
void VtableCopies(std::uintptr_t out[kVtableCopies]) noexcept;
// Odd while a window (or the block-0 hold log) has fields swapped in; it
// changes on every window. Lua-thread readers of PP/src retry or refuse
// unless it is even and unchanged across their read.
std::uint32_t WindowSeq() noexcept;
// Clears a recorded fault so the window may run again (MissionStart only;
// the driver keeps its own per-mission input_faulted) and the hold history.
void ResetFault() noexcept;

// The driver publishes this each tick (seqlock: odd, copy, even). Copied by
// value; pointers are read inside the hook under SEH and re-validated.
void PublishArm(const partner_input::Arm& arm) noexcept;

// Wrap census counters (process-lifetime totals; the driver logs deltas).
// Entry e: 0 svc+0x28 (active walk), 1 svc+0x30 (all ids), 2 svc+0x40 (per
// id), 3 call 0x9C1DA3 (phase 7), 4 call 0x9C1AFF (phase 11).
inline constexpr std::size_t kWrapEntries = 5;
struct WrapStats {
    bool installed;
    std::uint32_t calls0[kWrapEntries];       // root == block 0's root
    std::uint32_t calls1[kWrapEntries];       // root == block 1's root
    std::uint32_t same_thread[kWrapEntries];  // block-1 calls on the hook thread
    std::uint32_t nested[kWrapEntries];       // block-1 calls inside an open window
    std::uint32_t windows[kWrapEntries];      // wrap=window windows run
    std::uint32_t phase_mask[kWrapEntries];   // phases seen for block 1 (bit = phase & 31)
    std::uint32_t other_calls;                // calls for any other root (all entries)
};

// Drained by the driver. Counters are process-lifetime totals (the driver
// logs deltas); us_max is cleared by each drain.
struct Stats {
    partner_input::WindowStats window;
    partner_input::HoldRecord holds[8];
    unsigned hold_count;
    WrapStats wrap;
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
// A 14-byte near stub (BuildStub) within rel32 reach of next_ip, then the
// rel32 of the E8 call at `site` (expected orig_rel) retargeted to it with a
// locked dword exchange (near_code::PatchDword). nullptr on success, else
// bytes | allocate_near | range | protect | exchange | cache_line |
// protect_restore; on protect_restore the patch IS live and stub_out is set.
const char* RetargetCall(std::uintptr_t site, std::uintptr_t next_ip, std::uint32_t orig_rel, const void* target,
                         void*& stub_out) noexcept;
// Which injected word a PP key-manager slot answers from (pure): +0xE8/+0x110/
// +0x118 pressed, +0xF0/+0x120/+0x128 released, +0x100/+0x108/+0x130/+0x138
// held, +0xF8 (km vt+0x148, not decoded) nothing.
enum class KmWord : std::uint8_t { held, press, release, zero, none };
KmWord KmSlotWord(std::size_t slot_offset) noexcept;
// Whether the slot takes a binding index (true) or a key handle (false).
bool KmSlotByIndex(std::size_t slot_offset) noexcept;
}

} // namespace mgscoop::partner_input_hook
