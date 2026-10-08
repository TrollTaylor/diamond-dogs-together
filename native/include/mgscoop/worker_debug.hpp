#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_native { struct Telemetry; }

namespace mgscoop::worker_debug {
struct Request { std::uint64_t sequence{}, expires{}; std::uint32_t mission{}, generation{}; char text[201]{}; };
// A copied debug command, never Lua source or a native address.
bool Parse(const char*, std::size_t, const char* run, const char* role, Request&) noexcept;
// True for worker overlay requests ("menu ..."), which never reach the game queue.
bool IsMenuText(const char* text) noexcept;
bool Timely(const Request&, std::uint64_t now, std::uint64_t previous) noexcept;
// The worker state.json "native" member exactly as Publish appends it (a
// leading comma, then "native":{...}); its length, or 0 (out[0] = 0) when it
// does not fit in `capacity` bytes including the terminator.
std::size_t FormatNativeJson(const partner_native::Telemetry& telemetry, char* out, std::size_t capacity) noexcept;
// Called exclusively from the existing natural Lua command receipt owner.
void PollOwner() noexcept;
}
