#pragma once
// Pure helpers for the world sync receipts compiled by lua/coop/world_sync.lua
// through the luaL_loadbuffer observer (docs/world-sync-protocol.md
// "Receipts"), and the lab world.trigger grammar. No Win32 calls and no game
// memory: game_script_observer owns threading and logging, world_link the core.
//
// Receipts (the chunk name always equals the source, as loadstring sets it;
// NUL-free, at most max_receipt bytes):
//   return 'MGS_COOP_WORLD_CFG_V1' -- <mission> <phase>     phase 0 query, 1 start
//        -> return <level>,<role>,<proto>,<link>,<epoch>,<probe>,<mode>,<session>,
//                  <remote_mission>,<remote_level>,<wait>,<handoff>,<lock>,<census>,<vehicle_gone>,<companion>
//        (16 values; r[9..11] are the partner's mission and world level, 0
//        unknown, and the raw pair-wait code world::pair_wait 0..10; r[12] is
//        the save handoff code save_handoff_state::LuaCode 0..6 and r[13] the
//        travel lock 0|1, both filled by world_link::HandleCfg, join-fix
//        phase 2 P2-1; r[14] the WS4 attacker census 0|1 (lab key
//        partner_placed_census, lab only), also from HandleCfg; r[15] the
//        0.13.6 VEHICLE_GONE module 0|1 (lab key world_vehicle_gone, lab and
//        play), also from HandleCfg; r[16] the M1 host-owned companion module 0|1
//        (lab key companion_host_owned, lab and play), also from HandleCfg;
//        Lua treats a missing value as 0)
//   return 'MGS_COOP_WORLD_TX_V1' -- <op> <v1> ... <vn>      n = the op's Lua field count
//        -> return 1 | return 0,<reason>   (TxStatus)
//   return 'MGS_COOP_WORLD_RX_V1' -- <mask>                  1 control, 3 control+world+U
//        -> return <queue>,<op>,<v...>  or  return 0,0,<flags>,<qctrl>,<qworld>,<epoch>
//   return 'MGS_COOP_WORLD_LOG_V1' -- <payload>              1..160 bytes -> logs world_lua=...
//   return 'MGS_COOP_WORLD_WARP_V1' -- <seq> <goId>          guest spawn sync: one native placement of
//        the grant recorded for <seq> (world_wire.hpp Grant) on GameObjectId <goId>
//        -> return {r=,h=,leaf=} | return {refused="<reason>"} (game_script_observer)
//   return 'MGS_COOP_WORLD_NOTE_V1' -- <code> <a> <b>        an on-screen world note
//        (code 1..15, a and b 0..4294967295, no sign; world_link::HandleNote)
//        -> return true (handled or rate-gated) | return false (no meaning yet)
//   return 'MGS_COOP_WORLD_SAVE_V1' -- <op> <a> <b> <c> <d> <e>   the save handoff report
//        (join-fix phase 2 P2-2; exactly 6 integers, no sign; save_handoff::HandleLua):
//        op 1 REPORT a=mission 0..99999 b=title 0|1 c=story 0..2 d=saving 0|1 e=ingame 0|1;
//        op 2 CONTINUE a=target 1..99999 b=title 0|1 c=story 0..2 d=e=0;
//        op 3 DECLINE a=1 second_continue|2 f9 b..e=0
//        -> return true | return false (module off or rate-gated)
// Integers only: an optional '-' (i32 fields only), no '+', no leading zeros,
// no "-0", no '.', single spaces, nothing trailing; u32 up to 4294967295.
#include <cstddef>
#include <cstdint>
#include "mgscoop/world_wire.hpp"

namespace mgscoop::world {

enum class Receipt : unsigned char { none, cfg, tx, rx, log, warp, note, save };

inline constexpr char cfg_prefix[] = "return 'MGS_COOP_WORLD_CFG_V1' -- ";
inline constexpr char tx_prefix[] = "return 'MGS_COOP_WORLD_TX_V1' -- ";
inline constexpr char rx_prefix[] = "return 'MGS_COOP_WORLD_RX_V1' -- ";
inline constexpr char log_prefix[] = "return 'MGS_COOP_WORLD_LOG_V1' -- ";
inline constexpr char warp_prefix[] = "return 'MGS_COOP_WORLD_WARP_V1' -- ";
inline constexpr char note_prefix[] = "return 'MGS_COOP_WORLD_NOTE_V1' -- ";
inline constexpr char save_prefix[] = "return 'MGS_COOP_WORLD_SAVE_V1' -- ";
inline constexpr std::size_t max_receipt = 240;
inline constexpr std::size_t max_log_payload = 160;
// The longest reply: "return " and 17 values (queue, op, 15 fields) of at
// most 11 characters each with their commas (211 bytes).
inline constexpr std::size_t reply_capacity = 224;
// "world_lua=" + payload with ';' -> "; " + "; t_ms=<20 digits>" + CRLF.
inline constexpr std::size_t log_line_capacity = 10 + 2 * max_log_payload + 30;
inline constexpr std::size_t max_trigger = 255;

// Exact receipts only (bytewise name == source, NUL-free, size bound).
Receipt Classify(const char* source, std::size_t size, const char* name) noexcept;
// mission 0..99999, phase 0|1. Outputs are written only on success.
bool ParseCfg(const char* source, std::size_t size, std::uint32_t& mission, std::uint8_t& phase) noexcept;
// syntax (malformed integers or spacing), unknown_op (not a Lua-sendable op),
// bad_args (wrong count, a negative unsigned value or a value out of range).
TxStatus ParseTx(const char* source, std::size_t size, std::uint8_t& code, std::int64_t (&values)[kMaxValues],
                 unsigned& count) noexcept;
// mask 1 or 3 only.
bool ParseRx(const char* source, std::size_t size, std::uint8_t& mask) noexcept;
// WARP_V1: seq 1..65535, goId 0..65535 (strict integers). Outputs only on success.
bool ParseWarp(const char* source, std::size_t size, std::uint16_t& seq, std::uint32_t& go_id) noexcept;
// NOTE_V1: code 1..15, a and b 0..4294967295 (strict integers, no sign).
// Outputs only on success.
bool ParseNote(const char* source, std::size_t size, std::uint8_t& code, std::uint32_t& a, std::uint32_t& b) noexcept;
// SAVE_V1: exactly 6 strict integers (no sign) with the P2-2 ranges per op
// (see the grammar above). Outputs only on success.
bool ParseSave(const char* source, std::size_t size, std::uint8_t& op, std::uint32_t& a, std::uint32_t& b, std::uint32_t& c,
               std::uint32_t& d, std::uint32_t& e) noexcept;

// NUL-terminated replies; 0 when they do not fit.
std::size_t FormatCfgReply(const CfgReply& reply, char* out, std::size_t capacity) noexcept;
std::size_t FormatTxReply(TxStatus status, char* out, std::size_t capacity) noexcept;
std::size_t FormatRxReply(const RxReply& reply, char* out, std::size_t capacity) noexcept;
// "world_lua=<payload>; t_ms=<now>\r\n" with the partner sanitizer
// ([A-Za-z0-9_=:,.;-], others -> '_') and ';' expanded to "; ". 0 when the
// payload is empty or longer than max_log_payload.
std::size_t FormatLogLine(const char* source, std::size_t size, std::uint64_t now_ms, char* out, std::size_t capacity) noexcept;

// world.trigger: one ASCII line (an optional UTF-8 BOM and trailing CR/LF are
// ignored), blank-separated tokens, each key at most once, in any order:
// world=0..3 world_probe=<name>|off goto=30010|30020 goto_lz=0|1 (only with
// goto). On success `canonical` gets "world=.. world_probe=.. goto=.. goto_lz=.."
// (present keys, that order). On failure `reason` is token, repeat, empty or too_long.
bool ParseTrigger(const char* text, std::size_t size, Trigger& out, char* canonical, std::size_t canonical_capacity,
                  const char*& reason) noexcept;

} // namespace mgscoop::world
