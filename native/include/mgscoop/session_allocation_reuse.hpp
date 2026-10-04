#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Idempotent session-allocation preparation (synthesis gap G11 retry path).
//
// PrepareSessionAllocation installs the allocation bridge's MinHook detour on
// BlockHeapAlloc (base+0xDCB50) the first time a host or client request runs,
// and the detour stays installed for the life of the process (no live removal
// is attempted). A second request in the same process - a client retry after a
// reset - must therefore not re-hash that entry against the unhooked contract
// or ask the bridge to install again. Instead it reuses the existing install
// when this adapter performed it for the same image base, and it verifies the
// hooked entry: MinHook replaces exactly the first instruction of the verified
// body (48 89 5C 24 08, five bytes) with JMP rel32 (E9) and leaves every later
// byte untouched. The remaining 37 session bodies are still hashed.

namespace mgscoop {

enum class AllocationPrepare : std::uint8_t {
    install,  // no install yet: hash every body, then install the bridge
    reuse,    // this adapter installed it for this base: verify the hooked entry
    refuse    // installed for a different base: never reuse
};

constexpr AllocationPrepare DecideAllocationPrepare(std::uintptr_t installed_base, std::uintptr_t base) noexcept {
    return installed_base == 0 ? AllocationPrepare::install
         : installed_base == base ? AllocationPrepare::reuse
         : AllocationPrepare::refuse;
}

// Length of the verified first instruction that MinHook's JMP rel32 replaces.
constexpr std::size_t kDetourPatchBytes = 5;

// True when `live` is `original` with only its first five bytes replaced by a
// JMP rel32. `original` must start with exactly one five-byte instruction.
inline bool DetouredEntryMatches(const unsigned char* live, const unsigned char* original, std::size_t count) noexcept {
    if (!live || !original || count <= kDetourPatchBytes) return false;
    return live[0] == 0xE9 &&
        std::memcmp(live + kDetourPatchBytes, original + kDetourPatchBytes, count - kDetourPatchBytes) == 0;
}

} // namespace mgscoop
