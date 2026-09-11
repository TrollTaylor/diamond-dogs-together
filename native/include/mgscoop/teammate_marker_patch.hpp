#pragma once
#include <cstddef>
namespace mgscoop::teammate_marker {
// Remap an existing FOB enemy icon during the owned co-op session. Does not
// create markers, extend their lifetime or send network messages.
bool Install(void* base, std::size_t span, void (*log)(const char*) noexcept) noexcept;
}
