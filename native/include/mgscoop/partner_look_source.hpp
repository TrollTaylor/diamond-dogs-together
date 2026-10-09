#pragma once
// Native puppet facing D3 (docs/native-puppet.md "Facing"): the remote's look
// at its source. PS phase 0 (0xFF28D0) computes every player's look pair
// {pitch, yaw} with its only call 0xFF29FC -> 0xFFAC00(PS, id); offline
// (0xFF3A70 false: S_IS_ONLINE clear) that rotates a constant by the VIEWER's
// camera quaternion *(*(P+0x138)+0xB8)+0x90 and stores the world pitch/yaw at
// *(PS+0x290) + (id - PS+8)*8 (0xFFAD38..0xFFAD57). The aim states read it in
// the same phase-0 walk (0x109F170), before the existing post-phase-0 look
// write (partner_input_hook LookWrite) lands: instance 1's aim twist ran on
// the viewer's look (act 12 oscillation, crouch aim toward the viewer's
// camera; hidden runs bec857fb/b9528). This hook follows 0xFFAC00 with the
// remote's look for instance 1 only, so every phase-0 consumer reads it.
//
// Mechanism (all RVAs in mgsvtpp.exe 1.0.15.4; tools/generate_partner_contract.py
// --check pins the bytes, the single caller, the padding and the cache line):
// - a near stub (unwind data registered with RtlAddFunctionTable): push rbx,
//   rsi; call 0xFFAC00(rcx, edx); LookAfter(PS, id); pop; ret (rax is not
//   read by the caller: 0xFF2A01 reloads edx/rcx);
// - `jmp rel32` to the stub in the int3 padding 0xFFAD66..0xFFAD6F (after
//   0xFFAC00's ret at 0xFFAD65, outside every function, never a branch target);
// - the call's rel32 0xFF29FD..0xFF2A00 retargeted from 0xFFAC00 (ff 81 00 00)
//   to 0xFFAD66 (65 83 00 00): only bytes 0xFF29FD..FE change, written with one
//   locked compare-exchange of the aligned qword 0xFF29F8 (the rel32 itself
//   straddles the cache line at 0xFF2A00).
// LookAfter runs on whatever thread runs PS phase 0 (block 0's job may run on
// a worker): atomics only. It writes only while partner_input_hook published
// the remote's look for the current drain window (the hook thread, instance
// 1's PS, ids[1] == 1, aim looksrc, a live sample), and only when the game
// did not skip the computation (per-id record bit 1). Installed once per
// process with defer=on and partner_native_aim=looksrc, never removed.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_look_source {

using LogFn = void (*)(const char* line) noexcept;

// nullptr on success (or already installed), otherwise a static reason (also
// logged as look_src=refused; reason=..): bytes (a pinned site differs) |
// padding | caller (the call's rel32 is not 0xFFAC00's) | align | cache_line |
// allocate_near | range | protect | unwind | changed | disabled.
const char* Install(const void* base, LogFn log) noexcept;
bool Installed() noexcept;
// True while the qword at 0xFF29F8 holds the retargeted call and the padding
// jump still reaches the stub.
bool PatchLive(const void* base) noexcept;

// The hook thread, inside a drain window, before block 1's job: the look the
// stub writes for (ps, id) on this thread; Withdraw after the job (also on
// unwind). Any other thread, PS or id is left alone.
void Publish(std::uintptr_t ps, std::uint32_t id, std::uint32_t idx, float pitch, float yaw) noexcept;
void Withdraw() noexcept;

// Process totals.
struct Stats {
    bool installed;
    std::uint32_t calls;     // LookAfter calls (every player, every phase 0)
    std::uint32_t hits;      // remote looks written into instance 1's slot
    std::uint32_t skipped;   // the game skipped the computation (record bit 1)
    std::uint32_t refused;   // published but the PS/id/slot did not validate
    bool faulted;            // an access fault: no more writes this process
};
void ReadStats(Stats& out) noexcept;

namespace detail {
// The stub (layout fixed; tests decode it) and its unwind data.
inline constexpr std::size_t kStubSize = 0x30;
inline constexpr std::size_t kStubTargetAt = 0x0D;  // imm64 of mov rax, 0xFFAC00
inline constexpr std::size_t kStubAfterAt = 0x1E;   // imm64 of mov rax, LookAfter
void BuildStub(unsigned char (&out)[kStubSize], std::uintptr_t target, std::uintptr_t after) noexcept;
// UNWIND_INFO for the stub's prolog (push rbx, push rsi, sub rsp, 0x28).
inline constexpr std::size_t kUnwindSize = 12;
void BuildUnwind(unsigned char (&out)[kUnwindSize]) noexcept;
// The retargeted qword (original bytes with the rel32 low word of the call
// pointing at the padding), or 0 when the original does not match.
std::uint64_t RetargetQword(std::uint64_t original) noexcept;
// The entry the stub calls after 0xFFAC00 (exposed for fixture tests).
void LookAfter(void* ps, std::uint32_t id) noexcept;
void ResetForTest() noexcept;
}

} // namespace mgscoop::partner_look_source
