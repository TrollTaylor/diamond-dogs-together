#pragma once
#include <cstddef>
namespace mgscoop::trap_patch {
bool Install(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
}
