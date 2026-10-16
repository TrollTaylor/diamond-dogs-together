#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_native { struct Telemetry; }
namespace mgscoop::steam_state { struct SelftestReport; }
namespace mgscoop::save_handoff_state { struct View; }

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
// The worker state.json "steam" member (docs/steam-online.md 14), the same
// way: a leading comma, then "steam":{...} ("lpair":null unless stage L ran).
std::size_t FormatSteamJson(const steam_state::SelftestReport& report, char* out, std::size_t capacity) noexcept;
// The worker state.json save handoff keys (join-fix phase 2 B9) exactly as
// Publish inserts them after world_pair_wait, with the trailing comma:
// "handoff":"<token>","handoff_pct":N,"travel_unlocked":0|1,
std::size_t FormatHandoffJson(const save_handoff_state::View& view, char* out, std::size_t capacity) noexcept;
// Called exclusively from the existing natural Lua command receipt owner.
void PollOwner() noexcept;
}
