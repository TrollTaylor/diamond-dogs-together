#pragma once
// Shared helpers for in-memory code patches that need a buffer within rel32
// reach of an instruction (the master-server lea retarget and the round 3
// input hook stub) and for replacing one code dword atomically. The pure
// parts (Rel32, NearWindow, CrossesCacheLine) are portable and unit tested;
// AllocateNear, WritableFor and PatchDword are Win32 only.
#include <cstddef>
#include <cstdint>

namespace mgscoop::near_code {

// 2 GiB minus 64 KiB: the rel32 reach kept by every near allocation.
inline constexpr std::uint64_t kRel32Margin = 0x7fff0000ull;

// rel32 displacement from next_ip to target; false when out of int32 range.
bool Rel32(std::uint64_t next_ip, std::uint64_t target, std::int32_t& out) noexcept;

// Allocation-granularity aligned [low, high] start addresses for a `size`-byte
// block that a rel32 at next_ip can reach with a 64 KiB safety margin, clamped
// to [min_address, max_address]. False when empty or granularity is not a power of two.
bool NearWindow(std::uint64_t next_ip, std::uint64_t size, std::uint64_t granularity,
                std::uint64_t min_address, std::uint64_t max_address,
                std::uint64_t& low, std::uint64_t& high) noexcept;

// True when [address, address + size) spans two 64-byte cache lines (a locked
// exchange there would be a split lock and is refused).
inline bool CrossesCacheLine(std::uint64_t address, std::size_t size) noexcept {
    return size == 0 || (address & 63u) + size > 64u;
}

#ifdef _WIN32
// Protection that keeps the current execute right while adding write access.
unsigned long WritableFor(unsigned long protection) noexcept;
// A committed PAGE_READWRITE block of `size` bytes whose every byte a rel32
// at next_ip reaches (downward first, then upward). nullptr when none is free.
// The caller sets the final protection and never frees a block code jumps to.
void* AllocateNear(std::uintptr_t next_ip, std::size_t size) noexcept;
// Replaces the 4 code bytes at `site` with `value` in one locked
// compare-exchange against `expected`, flushes the instruction cache and
// restores the page protection. nullptr on success, otherwise
// "cache_line" (the dword spans two lines), "protect", "changed" (the dword
// was not `expected`; nothing written) or "protect_restore".
const char* PatchDword(void* site, std::uint32_t expected, std::uint32_t value) noexcept;
// The same for an 8-byte-aligned data pointer (a vtable slot in read-only
// data): one locked 64-bit compare-exchange, protection restored. nullptr on
// success, otherwise "alignment", "protect", "changed" or "protect_restore".
const char* PatchQword(void* site, std::uint64_t expected, std::uint64_t value) noexcept;
#endif

} // namespace mgscoop::near_code
