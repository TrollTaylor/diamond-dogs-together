#pragma once
#include <cstddef>
namespace mgscoop::master_server {
// Point the game's master-server gate URL at `url` (e.g. a local fuse instance,
// http://127.0.0.1:6667/tppstm/gate) in memory only. An empty or null url is a
// no-op that returns true. Must verify the exact 1.0.15.4 instruction/string
// bytes before writing; refuses (returns false, logs a reason) on any mismatch.
// No executable disk write.
bool Install(void* base, std::size_t span, const char* url, void (*log)(const char*) noexcept) noexcept;
}
