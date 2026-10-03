#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::full_close_patch {
bool AdoptMissionThread(std::uint32_t previous_thread) noexcept;
// Only the owned-session teardown gate may arm this immediately before the
// original CloseSession Lua call. The returned/failed receipt restores it.
// outgoing_mission is captured by the native allocation lifecycle, not read
// from the destination mission ID or supplied by a network peer.
bool Activate(void* verified_base,std::size_t span,std::uint16_t outgoing_mission,
              void (*log)(const char*) noexcept) noexcept;
bool Deactivate() noexcept;
}
