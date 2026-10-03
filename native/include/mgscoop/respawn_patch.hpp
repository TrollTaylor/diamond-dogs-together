#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::respawn_patch {
// Transfers control ownership after the previous callback returned, retaining
// active/pending restoration state. The lifecycle adapter serializes callers.
bool AdoptMissionThread(std::uint32_t previous_thread) noexcept;
bool SetActive(void* base,std::size_t span,bool enabled,void (*log)(const char*) noexcept) noexcept;
}
