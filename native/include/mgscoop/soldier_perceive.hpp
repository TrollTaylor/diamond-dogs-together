#pragma once
// Enemy sync M7 (docs/world-sync-design.md 7.13): the soldier perception filter patch. In the per-soldier reaction chain
// (0x1413262F0 calls 0x141325530 for perceived category-5 entries, the players), a player-type target (GameObjectId type
// 0) is skipped unless its index equals the FOB offense player index (vtable 0x1425564F0 slot 0x220, 0x141E02770: the
// local player offline). So no soldier ever reacts to body 1, the partner. The patch turns the `je 0x141325692` at
// 0x14132562B (74 65) into `jmp` (EB 65): every player-type entry goes on to the stock checks (0x141325692: the per-target
// ignore test, then the distance/priority scoring 0x141325E90). Offline only players 0 and 1 exist.
// Static evidence: mgs-coop-re/m7 (Ghidra 12.0 project mgsvtpp-local, mgsvtpp.exe 1.0.15.4 SHA-256 085c2f82...bb45):
// the block below, the vtable slot, the 18 offense-index call sites (only this one filters perception).
#include <cstddef>
#include <cstdint>

namespace mgscoop::soldier_perceive {

inline constexpr std::uintptr_t kBlockRva = 0x13255F0;  // mov rax,[rcx] .. jne 0x1413255c0 (0x50 bytes)
inline constexpr std::size_t kBlockSize = 0x50;
inline constexpr std::uintptr_t kPatchRva = 0x132562B;  // the dword 74 65 8b 55 (je +0x65; mov edx,[rbp-0x44] head)
inline constexpr std::size_t kPatchOffset = kPatchRva - kBlockRva;
inline constexpr std::uint32_t kOld = 0x558b6574u, kNew = 0x558b65ebu;
// The block as in the retail executable (the patched dword included as 74 65 8b 55).
extern const unsigned char kBlock[kBlockSize];

// Pure: the live block equals kBlock, the patched dword (kPatchOffset .. +4) either original or patched. *patched (when
// given) is true for the patched form.
bool BlockMatches(const unsigned char* live, std::size_t size, bool* patched = nullptr) noexcept;

#ifdef _WIN32
using LogFn = void (*)(const char* line) noexcept;
// `base` is the image base. Patch() verifies the block and exchanges the dword (near_code::PatchDword), logging
// soldier_perceive=patched; Revert() restores it (soldier_perceive=reverted; reason=...). A mismatch or a failed exchange
// logs soldier_perceive=refused and changes nothing. Idempotent.
bool Patch(const void* base, LogFn log) noexcept;
void Revert(const void* base, LogFn log, const char* reason) noexcept;
bool Patched() noexcept;
#endif

} // namespace mgscoop::soldier_perceive
