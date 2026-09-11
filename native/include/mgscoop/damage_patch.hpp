#pragma once
#include <cstddef>
namespace mgscoop::damage {
// Same Lua lifecycle owner sets and clears the one-byte native damage bypass.
// Clearing also verifies/restores code-page protection; no executable disk write.
bool SetActive(void* base,std::size_t span,bool enabled,void (*log)(const char*) noexcept) noexcept;
}
