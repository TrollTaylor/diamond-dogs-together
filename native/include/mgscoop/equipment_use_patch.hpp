#pragma once
#include <cstddef>
namespace mgscoop::equipment_use {
// Exact-body gated hook; forwards unchanged outside prepared co-op scenes.
bool Install(void* verified_base, std::size_t span, void (*log)(const char*) noexcept) noexcept;
}
