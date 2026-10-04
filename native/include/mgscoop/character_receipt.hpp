#pragma once
// Pure helpers for the character-selection receipts compiled by
// lua/coop/character.lua through the luaL_loadbuffer observer. No Win32 calls,
// no game memory: the script observer owns threading, logging and the native
// Ocelot/Quiet reset bypass (mgscoop::player_type).
//
// Receipts (the chunk name always equals the source, as loadstring sets it):
//   return 'MGS_COOP_CHARACTER_V1'             -> return '<character>','<fallback>',<readback>
//   return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'   -> return true|false (bypass now active)
//   return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'  -> return true|false (original bytes in place)
//   return 'MGS_COOP_CHARACTER_REPORT_V1' -- <payload>    log only, never replaced
//   return 'MGS_COOP_CHARACTER_READBACK_V1' -- <payload>  log only, never replaced
//   return 'MGS_COOP_CHARACTER_PLAYERS_V1'     -> return true,<limit>,<raw0>[,<raw1>]
//                                                 | return false,'<stage>'
// The PLAYERS reply guards the lab readback: Lua calls Player.GetPlayerType
// only for indices below min(limit, 2) of a ready sample
// (mgscoop::player_type::SampleTypes), because the binding has no null checks.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::character {

enum class Receipt : unsigned char { none, query, bypass_on, bypass_off, report, readback, players };

inline constexpr char query_source[] = "return 'MGS_COOP_CHARACTER_V1'";
inline constexpr char bypass_on_source[] = "return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'";
inline constexpr char bypass_off_source[] = "return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'";
inline constexpr char players_source[] = "return 'MGS_COOP_CHARACTER_PLAYERS_V1'";
inline constexpr char report_prefix[] = "return 'MGS_COOP_CHARACTER_REPORT_V1' -- ";
inline constexpr char readback_prefix[] = "return 'MGS_COOP_CHARACTER_READBACK_V1' -- ";
// Longest accepted report/readback payload (the text after "-- ").
inline constexpr std::size_t max_payload = 192;
// Capacity that always fits FormatQuery and FormatLogLine output.
inline constexpr std::size_t max_line = 2 * max_payload + 32;

// Exact receipts only. Returns none for anything else, including a matching
// source compiled under a different chunk name.
Receipt Classify(const char* source, std::size_t size, const char* name) noexcept;

// Writes the query replacement, e.g. "return 'quiet','dd_male',true". The
// character is '' for game_default (no coop.ini or character=default).
// Returns the length written without the terminator, or 0 if it does not fit.
std::size_t FormatQuery(const CoopConfig& coop, bool readback, char* out, std::size_t capacity) noexcept;

// True only when the configured character itself is OCELOT or QUIET, the two
// types the native UpdatePartsStatus reset reverts to SNAKE outside FOB.
bool NeedsResetBypass(const CoopConfig& coop) noexcept;

// For report/readback receipts: validates the payload ([A-Za-z0-9_=:,.;-],
// 1..max_payload bytes) and writes "character=<payload>\r\n" or
// "character_readback=<payload>\r\n" with every ';' expanded to "; ".
bool FormatLogLine(Receipt kind, const char* source, std::size_t size, char* out, std::size_t capacity) noexcept;

// Writes the PLAYERS reply. Ready (count 1..2 raw types, 1 <= limit <= 4):
// "return true,<limit>,<raw0>[,<raw1>]". Otherwise "return false,'<stage>'",
// where a stage outside [a-z_]{1,32} becomes 'unknown' and an inconsistent
// ready sample becomes 'invalid_sample'. Returns the length written without
// the terminator, or 0 (empty output) if it does not fit.
std::size_t FormatPlayersReply(bool ready, const char* stage, std::uint32_t limit, const unsigned char* types,
                               std::size_t count, char* out, std::size_t capacity) noexcept;

} // namespace mgscoop::character
