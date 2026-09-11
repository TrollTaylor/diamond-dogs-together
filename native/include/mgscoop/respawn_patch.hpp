#pragma once
#include <cstddef>
namespace mgscoop::respawn_patch {
bool SetActive(void* base,std::size_t span,bool enabled,void (*log)(const char*) noexcept) noexcept;
}
