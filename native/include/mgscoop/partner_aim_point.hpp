#pragma once
// Aim parity, Win32 part (docs/native-puppet.md "Facing", "The aim-target
// mirror"; the pure rules are partner_aim). The driver tick publishes a
// target (seqlock); inside block 1's drain window, before its job, the hook
// thread writes it into instance 1's network aim slot exactly as the game's
// own synchronizer methods would: Q+0x10[1].xyz (Q vt+0x28 0xFDEDD0's store)
// and record 1 of *(Q+0x1F0) (stride 0x30: current and previous sample = the
// target, the interpolation timers +0x24/+0x28/+0x2C = 0: Q vt+0x18 0xFE0080
// twice plus vt+0x28), so the interpolation vt+0x20 0xFE02F0, if anything
// ran it, still yields the target (duration 0 -> fraction 1.0, 0xFDECB0).
// Instance 0's slot is never touched; S_IS_ONLINE is never touched.
//
// Threading: Publish/SetOverride/Forget/Shutdown on any thread (atomics and a
// seqlock); Step only on the hook thread inside RunJobWindow (no batch in
// flight, P/ids validated: ids[1] == 1, block == pool1); Idle only on the hook
// thread at the main dispatch with no batch in flight. Every game access is
// SEH-contained; a fault latches the mirror off until the next mission epoch.
// Restore: the originals saved at the first write of an epoch (Q+0x10[1] and
// record 1, 0x40 bytes) go back at the next main dispatch once no enabled
// native Arm is published (or the publication no longer wants a target),
// after re-validating the chain; a value the game itself wrote since our last
// write keeps the game's value. MissionStart/End forget without writing.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_aim_point {

using LogFn = void (*)(const char* line) noexcept;

// The image base and the log (partner_input_hook::Install binds them once).
void Bind(const void* base, LogFn log) noexcept;

// The driver tick's publication (partner_aim::Target plus its clock and the
// mission epoch; ms is the partner_net QPC clock, the hook's NowMs base).
struct Publication {
    bool want, valid;
    float p[3];
    std::uint8_t src;       // partner_aim::Source
    std::uint8_t reason;    // partner_aim::Reason (why not valid)
    std::uint64_t ms;
    std::uint32_t epoch;
};
void Publish(const Publication& publication) noexcept;

// The hook thread, inside block 1's drain window (after the magazine mirror,
// before the job): P is the validated player object of the Arm.
void Step(std::uintptr_t P, std::uint64_t now_ms) noexcept;
// The hook thread at the main dispatch: true when a restore (or a forget) is
// due; `native_armed` is whether an enabled native Arm is published.
bool IdleDue(bool native_armed) noexcept;
// Restores (or drops) what the mirror wrote; never while a batch is in flight
// (the caller checks partner_defer::InBatch).
void Idle() noexcept;
// MissionStart/End (any thread): drop the state without writing; a
// publication of `epoch` never writes again; a latched fault clears.
void Forget(std::uint32_t epoch) noexcept;
// The game window is closing: no more writes or restores.
void Shutdown() noexcept;

// The debug override ("aimpt ..."): -1 none (lab.ini), else the value of
// PartnerNativeAimPt. Read by the driver tick through Stats.
void SetOverride(int value) noexcept;
int Override() noexcept;
// "aimpt off|look|point|lab|status": true when `text` is an aimpt command
// (handled natively, never queued to Lua); ok false for a bad argument.
bool HandleCommand(const char* text, char* result, std::size_t capacity, bool& ok) noexcept;

struct Stats {
    bool applied;               // the slot holds our target; the originals are saved
    bool latched;               // a fault: off until the next epoch
    std::uint8_t state;         // State below
    std::uint8_t reason;        // partner_aim::Reason of the last step
    std::uint32_t writes;       // drains that wrote the slot
    std::uint32_t refused;      // drains that refused (any reason but none/off/stale/torn)
    std::uint32_t holds;        // drains that kept the slot (no look, stale, torn)
    std::uint32_t overwritten;  // drains that found Q+0x10[1] changed since our last write
    std::uint32_t restores, drops;
    bool seen_ok;
    float q10_seen[3];          // Q+0x10[1] read before the last write (or restore)
    bool written_ok;
    float written[3];           // our last write
    int override_value = -1;    // the debug override (-1 none)
};
enum class State : std::uint8_t { off, wait, ok, hold, refused, latched };
const char* StateName(State state) noexcept;
void ReadStats(Stats& out) noexcept;

namespace detail {
// The hook's clock (QPC milliseconds, the same base as partner_net::NowMs).
std::uint64_t NowMs() noexcept;
void ResetForTest() noexcept;
}

} // namespace mgscoop::partner_aim_point
