#pragma once
// Native puppet, phase-1 local decision (docs/native-puppet.md "Phase-1
// detour"). The base action plugin's phase 1 (0x109E130) chooses the local
// branch (event 4: the act handlers sample the pad's move intent, PP+0x10 /
// PP+0x14 into rec+0x2C0 / +0x2C4, which is what starts locomotion) or the
// remote branch (event 5: network follow) by `id == P+0x218`, never by the
// isLocal bit the windows set. Instance 1 (id 1, P+0x218 = 0) therefore never
// samples the injected pad. The detour forces the local branch for exactly one
// (P, id, thread) while partner_input_hook's drain window runs block 1's
// deferred job; everywhere else (block 0, other threads, outside a drain) the
// original decision stands. P+0x218 itself is never written (its pairing with
// P+0x214 / P+0x58 is hazardous: 0x11AD05D, 0xFE8F85).
//
// Patch (all bytes contract-verified, refused on any mismatch):
//   1. a near stub (setne bpl as at 0x109E182; when [state].id == edx and
//      [state].p == rax and the TEB thread id == [state].tid: bpl = 0 and
//      hits++, else when only the id matched: refused++; jmp 0x109E186),
//   2. jmp rel32 to the stub written into the int3 padding at 0x109E128
//      (outside every function, never a branch target; not executed yet),
//   3. the 4 bytes of `setne bpl` at 0x109E182 replaced by `jmp 0x109E128`
//      (eb a4 cc cc) in one locked dword exchange inside one cache line.
// rax (P, read again at 0x109E19D) and rcx are preserved; flags are
// recomputed at 0x109E186. Installed once per process, never removed.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_local_detour {

using LogFn = void (*)(const char* line) noexcept;

// nullptr on success (or already installed), otherwise a static reason (also
// logged): bytes | allocate_near | range | protect | exchange | cache_line |
// protect_restore | disabled (an earlier install failed).
const char* Install(const void* base, LogFn log) noexcept;
bool Installed() noexcept;
// True while 0x109E182 still jumps to the padding and the padding to our stub.
bool PatchLive(const void* base) noexcept;

// Drain window only (partner_input_hook::RunJobWindow): instance `id` of P `p`
// takes phase 1's local branch on the calling thread until Release.
void Force(std::uintptr_t p, std::uint32_t id) noexcept;
void Release() noexcept;

// Process-lifetime totals (the driver logs deltas).
struct Stats {
    bool installed;
    std::uint32_t forced;    // phase-1 calls switched to the local branch
    std::uint32_t refused;   // the id matched but the P or the thread did not
    std::uint32_t windows;   // Force calls (drain windows with the detour armed)
};
void ReadStats(Stats& out) noexcept;

namespace detail {
// The stub (pure): setne bpl; push rcx; mov rcx, state; the id/P/thread checks;
// pop rcx; jmp [rip] back. `state` is the address of the 24-byte state
// {i32 id, i32 hits, i32 tid, i32 refused, u64 p}; `back` the absolute return.
inline constexpr std::size_t kStubSize = 0x44;
inline constexpr std::size_t kStubStateAt = 0x07;  // the mov rcx imm64
inline constexpr std::size_t kStubBackAt = 0x3C;   // the jmp [rip] target
void BuildStub(unsigned char (&out)[kStubSize], std::uintptr_t state, std::uintptr_t back) noexcept;
// `jmp rel32` from `site` to `target`; false when out of rel32 reach.
bool BuildJump(unsigned char (&out)[5], std::uintptr_t site, std::uintptr_t target) noexcept;
// Test only: forget an install (the test frees its synthetic image).
void ResetForTest() noexcept;
// Test only: the published force state.
struct ForceState { std::int32_t id; std::uint32_t tid; std::uintptr_t p; };
ForceState ReadForceForTest() noexcept;
}

} // namespace mgscoop::partner_local_detour
