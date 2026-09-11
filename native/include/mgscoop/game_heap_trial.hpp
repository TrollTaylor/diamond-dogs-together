#pragma once
#include <cstddef>
// Caller establishes whole-executable identity and the exact owner-thread
// receipt before entry. This performs two paired heap allocations, not host
// creation, and never installs an allocator detour.
bool RunGameHeapTrial(void* verified_base, std::size_t span,
                      void (*log)(const char*) noexcept) noexcept;
