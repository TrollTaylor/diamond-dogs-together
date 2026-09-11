#pragma once
#include <cstddef>

namespace mgscoop::player_ghost {
// Requires the fingerprinted game base and the pre-Mission.Start Lua lifecycle.
// Process-lifetime two-byte patch. No disk writes, detours or retained callbacks.
bool Install(void* verified_base, std::size_t span, void (*log)(const char*) noexcept) noexcept;
}
