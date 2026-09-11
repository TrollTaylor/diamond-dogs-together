#pragma once
#include <cstddef>
namespace mgscoop::soldier_patch {
bool Activate(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
bool Deactivate() noexcept;
}
