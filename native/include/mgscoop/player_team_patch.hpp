#pragma once
#include <cstddef>
namespace mgscoop::player_team {
// Dynamite role predicates and defender/offense-index rules, installed before co-op.
// Inactive hooks forward to the game's original functions.
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept;
// Disabling always switches hooks to forwarding; false means a soldier branch
// still needs restoration. Callers must retain cleanup work in that case.
bool SetActive(bool enabled) noexcept;
bool IsActive() noexcept;
}
