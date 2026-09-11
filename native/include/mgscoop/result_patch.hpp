#pragma once
#include <cstddef>
namespace mgscoop::result_patch {
bool Install(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
void BeginCoopScene() noexcept;
void BeginNextMission() noexcept;
}
