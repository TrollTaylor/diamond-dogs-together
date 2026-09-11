#pragma once
#include <cstddef>
namespace mgscoop::full_close_patch {
// Only the owned-session teardown gate may arm this immediately before the
// original CloseSession Lua call. The returned/failed receipt restores it.
bool Activate(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
bool Deactivate() noexcept;
}
