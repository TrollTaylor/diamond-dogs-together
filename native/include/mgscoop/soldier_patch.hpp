#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::soldier_patch {
// Only at the serialized allocation/runtime boundary; no game memory writes.
bool AdoptMissionThread(std::uint32_t previous_thread) noexcept;
bool Activate(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
bool Deactivate() noexcept;
}
