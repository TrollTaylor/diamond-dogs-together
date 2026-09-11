#pragma once
#include <cstddef>
namespace mgscoop::marker_loading {
bool Begin(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
void End() noexcept;
bool IsActive() noexcept;
}
