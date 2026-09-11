#include <cstdint>
#include <intrin.h>

void AllocationFixtureRecord(std::uintptr_t);
void* AllocationFixtureMake(std::uint64_t, std::uint64_t, std::uint32_t);

// Keep this in a separate translation unit with whole-program optimization
// disabled: a detour target needs an actual ABI boundary in the fixture.
__declspec(noinline) void* Original(std::uint64_t size,
    std::uint64_t alignment, std::uint32_t tag) {
    AllocationFixtureRecord(reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
    return AllocationFixtureMake(size, alignment, tag);
}
