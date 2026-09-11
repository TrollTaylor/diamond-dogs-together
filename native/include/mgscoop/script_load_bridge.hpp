#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::script_bridge {
// Windows x64 luaL_loadbuffer boundary. No Lua state is retained by this bridge.
using LoadBuffer = int(*)(void*, const char*, std::size_t, const char*);
struct Replacement { const char* bytes = nullptr; std::size_t size = 0; };
using Select = bool(*)(void* context, const char* original, std::size_t size,
                      const char* name, Replacement& replacement) noexcept;
using Observe = void(*)(void* context, int result, bool replaced) noexcept;
struct Policy {
    void* context = nullptr;
    Select select = nullptr;
    Observe observe = nullptr;
};
enum class InstallResult { installed, invalid, bytes_differ, already_installed, hook_error };

// Caller must establish executable identity, function ABI and natural lifecycle
// before installing on game code. This module deliberately has no game address
// or automatic loader installation. Policy/context/replacement storage must stay
// alive and immutable while calls can occur; callbacks must be thread safe.
InstallResult Install(LoadBuffer target, const std::uint8_t* expected,
                      std::size_t expected_size, Policy policy) noexcept;

// For a controlled fixture or a caller that has stopped ALL possible entrants.
// An idle sample alone is insufficient. Do not use for live hot-unloading.
bool RemoveAfterCallersStopped() noexcept;
constexpr std::size_t max_replacement_bytes = 4 * 1024 * 1024;
}
