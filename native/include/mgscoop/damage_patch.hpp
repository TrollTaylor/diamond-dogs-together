#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::damage {
// Serialized lifecycle handoff after the previous callback returned. Transfers
// control metadata only; active/pending restoration state is preserved.
bool AdoptMissionThread(std::uint32_t previous_thread) noexcept;
// Same Lua lifecycle owner sets and clears the one-byte native damage bypass.
// Clearing also verifies/restores code-page protection; no executable disk write.
bool SetActive(void* base,std::size_t span,bool enabled,void (*log)(const char*) noexcept) noexcept;
}
