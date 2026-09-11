#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::enemy_targeting {
// Read-only, repeated roster observation; does not establish playable actors.
bool HasTwoPresentMembers(std::uintptr_t owned_session) noexcept;
bool Initialize(void* base,std::size_t span) noexcept;
// All snapshot/commit calls must come from the installation (mission) thread.
// nullptr means throttled. Other returns are Lua source, valid until next call.
const char* Snapshot(std::uintptr_t owned_host) noexcept;
// Same mapped roster/record reader; accepts an established owned host or client.
const char* RespawnContext(std::uintptr_t owned_session) noexcept;
bool Commit(std::uintptr_t owned_host,unsigned int index) noexcept;
void Clear() noexcept;
// UINT_MAX means forward the original offense selector.
unsigned int SelectedIndex() noexcept;
}
