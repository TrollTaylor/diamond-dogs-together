#pragma once
// Round 3 direct-motion lever (docs/partner-anim-appearance.md "Round 3",
// part D): the one reversible 4-byte comparator patch at 0x1198E25 that makes
// the offline DM consumer compare the request index with the body id (the
// online semantics), so a Lua request {name, 1} reaches instance 1. Code
// gated: it re-checks the 28-byte block at 0x1198E12 before writing and uses
// one locked dword exchange (near_code::PatchDword).
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_dm_patch {

using LogFn = void (*)(const char* line) noexcept;

// `base` is the image base. Patch() applies 0x1198E25 and logs
// partner_dm=patched; Revert() undoes it and logs partner_dm=reverted with the
// reason. Both re-verify the block; a mismatch or a failed exchange logs
// partner_dm=refused and leaves the state unchanged. Idempotent.
bool Patch(const void* base, LogFn log) noexcept;
void Revert(const void* base, LogFn log, const char* reason) noexcept;
bool Patched() noexcept;

} // namespace mgscoop::partner_dm_patch
