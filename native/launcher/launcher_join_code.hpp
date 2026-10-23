#pragma once
// Diamond Dogs Together: the join-code check behind the Join tab's live
// validation. Pure (no I/O, no Steam): built on the loader's own MGS2 parser
// (native/steam_join_code.cpp), so the launcher accepts exactly what the game
// accepts. Steam codes only: MGS2-SSSSS-SSSSS-SSSSS-SSSSS-CC@<SteamID64>.
#include <cstdint>
#include <string>

#include "launcher_model.hpp"

namespace ddt {

// Checks a pasted or typed code. Tolerant like the loader: blanks, case,
// dashes, O->0 and I/L->1; a pasted Steam invite string ("+mgscoop_join
// <code>") is accepted too. own_steam_id is the player's own SteamID64
// (0 = unknown); the player's own code is refused. An empty field gives
// {false, ""} (nothing to say yet).
CodeCheck CheckJoinCode(const std::string& text, std::uint64_t own_steam_id);

// The canonical code for a secret and host, "" when either is invalid.
std::string FormatJoinCode(const std::string& secret20, std::uint64_t host_steam_id);

}  // namespace ddt
