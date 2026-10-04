// Original MGS Co-op near-code helpers (moved out of master_server_patch.cpp).
#include "mgscoop/near_code.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#endif

namespace mgscoop::near_code {

bool Rel32(std::uint64_t next_ip, std::uint64_t target, std::int32_t& out) noexcept {
    const auto delta = static_cast<std::int64_t>(target - next_ip);  // two's complement wrap is intended
    if (delta < INT32_MIN || delta > INT32_MAX) return false;
    out = static_cast<std::int32_t>(delta);
    return true;
}

bool NearWindow(std::uint64_t next_ip, std::uint64_t size, std::uint64_t granularity,
                std::uint64_t min_address, std::uint64_t max_address,
                std::uint64_t& low, std::uint64_t& high) noexcept {
    low = high = 0;
    if (!size || !granularity || (granularity & (granularity - 1)) || min_address > max_address) return false;
    if (size > kRel32Margin) return false;
    std::uint64_t lo = next_ip > kRel32Margin ? next_ip - kRel32Margin : 0;
    if (lo < min_address) lo = min_address;
    if (lo > UINT64_MAX - (granularity - 1)) return false;
    lo = (lo + granularity - 1) & ~(granularity - 1);
    // The whole block must stay addressable: start + size - 1 <= next_ip + margin.
    std::uint64_t hi = next_ip > UINT64_MAX - kRel32Margin ? UINT64_MAX : next_ip + kRel32Margin;
    hi = hi - size + 1;
    if (max_address < size - 1) return false;
    if (hi > max_address - (size - 1)) hi = max_address - (size - 1);
    hi &= ~(granularity - 1);
    if (lo > hi) return false;
    low = lo;
    high = hi;
    return true;
}

#ifdef _WIN32
unsigned long WritableFor(unsigned long protection) noexcept {
    const DWORD base = protection & 0xffu;
    return (base == PAGE_EXECUTE || base == PAGE_EXECUTE_READ || base == PAGE_EXECUTE_READWRITE ||
            base == PAGE_EXECUTE_WRITECOPY) ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE;
}

void* AllocateNear(std::uintptr_t next_ip, std::size_t size) noexcept {
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    const std::uint64_t granularity = system.dwAllocationGranularity ? system.dwAllocationGranularity : 0x10000u;
    std::uint64_t low = 0, high = 0;
    if (!NearWindow(next_ip, size, granularity, reinterpret_cast<std::uintptr_t>(system.lpMinimumApplicationAddress),
                    reinterpret_cast<std::uintptr_t>(system.lpMaximumApplicationAddress), low, high)) return nullptr;
    auto try_at = [&](std::uint64_t address) noexcept -> void* {
        void* block = VirtualAlloc(reinterpret_cast<void*>(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (block && reinterpret_cast<std::uintptr_t>(block) != address) { VirtualFree(block, 0, MEM_RELEASE); return nullptr; }
        return block;
    };
    // Downward from the instruction (below the image is normally free), then upward.
    std::uint64_t cursor = next_ip;
    for (int regions = 0; regions < 4096 && cursor >= low; ++regions) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info))) break;
        const std::uint64_t region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const std::uint64_t end = region + info.RegionSize;
        if (info.State == MEM_FREE && end - region >= size) {
            std::uint64_t candidate = (cursor < end - size ? cursor : end - size) & ~(granularity - 1);
            if (candidate > high) candidate = high;
            for (int tries = 0; tries < 64 && candidate >= region && candidate >= low; ++tries) {
                if (void* block = try_at(candidate)) return block;
                if (candidate < granularity) break;
                candidate -= granularity;
            }
        }
        if (region <= low || region == 0) break;
        cursor = region - 1;
    }
    cursor = next_ip;
    for (int regions = 0; regions < 4096 && cursor <= high; ++regions) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info))) break;
        const std::uint64_t region = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
        const std::uint64_t end = region + info.RegionSize;
        if (info.State == MEM_FREE) {
            std::uint64_t candidate = ((cursor > region ? cursor : region) + granularity - 1) & ~(granularity - 1);
            if (candidate < low) candidate = low;
            for (int tries = 0; tries < 64 && candidate <= high && candidate + size <= end; ++tries) {
                if (void* block = try_at(candidate)) return block;
                candidate += granularity;
            }
        }
        if (end <= cursor) break;
        cursor = end;
    }
    return nullptr;
}

const char* PatchDword(void* site, std::uint32_t expected, std::uint32_t value) noexcept {
    if (!site || CrossesCacheLine(reinterpret_cast<std::uintptr_t>(site), 4)) return "cache_line";
    MEMORY_BASIC_INFORMATION info{};
    DWORD previous = 0, ignored = 0;
    if (!VirtualQuery(site, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        !VirtualProtect(site, 4, WritableFor(info.Protect), &previous)) return "protect";
    // One locked 32-bit exchange inside one cache line: a concurrent decoder
    // sees either the old or the new instruction bytes, never a mix.
    const long observed = _InterlockedCompareExchange(static_cast<volatile long*>(site), static_cast<long>(value),
                                                      static_cast<long>(expected));
    FlushInstructionCache(GetCurrentProcess(), site, 4);
    const bool restored = VirtualProtect(site, 4, previous, &ignored) != 0;
    if (observed != static_cast<long>(expected)) return "changed";
    return restored ? nullptr : "protect_restore";
}
#endif

} // namespace mgscoop::near_code
