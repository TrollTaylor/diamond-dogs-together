#pragma once
// Partner name tag: the game-thread source (docs/partner-nametag.md). Reads
// the game's own render viewport and camera (native/nametag_contract.hpp, the
// same unlocked reads the player update makes on this thread), the partner
// body's transform through partner_player's validated chain and the local gun
// record; exchanges the display names on reliable link channel 3; publishes
// one Frame per Lua tick to the overlay. Every read goes through
// ReadProcessMemory; nothing is written to the game and no game function is
// called. Lua runtime thread only (the observer's partner tick).
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/nametag_core.hpp"
#include "mgscoop/partner_player.hpp"

namespace mgscoop::nametag_game {
using LogFn = void (*)(const char* line) noexcept;
struct InstallConfig {
    const void* base;
    std::size_t span;
    LogFn log;
    bool session_ok;              // offline-v1 partner session (the observer's partner_on)
    bool host;
    const LabConfig* lab;
    const CoopConfig* coop;
    // The Steam persona (raw UTF-8), asked only when no configured name exists.
    bool (*steam_persona)(char* out, std::size_t capacity) noexcept;
};
// Verifies every contract site in the running image, resolves the local name
// and settings and configures nametag:: (logs nametag=ready|off|refused).
void Install(const InstallConfig& config) noexcept;
bool Ready() noexcept;
void MissionStart(std::uint32_t mission) noexcept;
void MissionEnd() noexcept;
struct TickInput {
    const partner_player::Addresses* addresses;
    bool owner_thread;            // the Lua runtime thread (the camera reads' precedent)
    bool terminating;
    nametag::Reason partner_hidden;  // ok while the partner body is shown
    std::uint32_t hud;            // debug_menu::LuaHud()
    std::uint32_t interp_ms;      // the remote sample delay (stance)
    bool tick_aim;                // the local tick's HOLD level (action_flags Aiming)
};
void Tick(const TickInput& input) noexcept;
// "nametag ..." text commands, handled natively (never Lua or the game).
// Returns false for any other text; ok receives final ok/refused.
bool HandleCommand(const char* text, char* result, std::size_t capacity, bool& ok) noexcept;
} // namespace mgscoop::nametag_game
