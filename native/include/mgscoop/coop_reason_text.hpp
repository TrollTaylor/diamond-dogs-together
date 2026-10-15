#pragma once
// Plain-language sentences for why a co-op link is not connected
// (docs/two-pc-play.md "When it does not connect"). The same sentences as the
// launcher: tools/CoopPlay.psm1 Get-CoopLinkReasonText and
// Get-CoopSteamEndText; both test suites read tests/fixtures/link-reasons.json.
// Also the world sync note, mission name and save handoff sentences of the
// in-game toasts and the F9 menu.
// Pure: no allocation, no OS call; ASCII only.
#include <cstddef>
#include <cstdint>

namespace mgscoop::coop_text {

// A link-status.json "reason" token (bad_code, busy, build_mismatch,
// steam_offline, transport_bind, ...). `remote_build` / `own_build` fill the
// version sentences ("another version" when the remote build is empty);
// `verified` = 0 adds "Possibly a wrong code." to an unknown refusal. An empty
// token gives "". False only when `out` is too small.
bool FormatLinkReason(const char* reason, const char* remote_build, const char* own_build, bool verified, char* out,
                      std::size_t capacity) noexcept;

// A Steam session failure (callback 1252 SteamNetworkingMessagesSessionFailed_t,
// info.m_eEndReason: ESteamNetConnectionEnd, public SDK values; the names are
// not in the binary, so this table is SDK-derived). 0 gives "".
bool FormatSteamEnd(std::int32_t end_reason, char* out, std::size_t capacity) noexcept;

// A few words for the in-game status line ("wrong code?", "host busy",
// "different mod version", "Steam unavailable", ...).
const char* ShortReason(const char* reason) noexcept;
const char* ShortSteamEnd(std::int32_t end_reason) noexcept;

// The no_body toast: a partner mission started on a pack list the player
// package selection refused (docs/player-package-selection.md). `scene` is a
// partner::PackSceneName (demo_stage, hangar, story, layout, other); the demo
// stage, the hangar and a story variant end with a normal reload, a refused
// layout or another list asks for Collect-Logs. False only when `out` is too small.
bool FormatNoBody(const char* scene, char* out, std::size_t capacity) noexcept;

// Mission names, shared with tools/CoopPlay.psm1 Get-CoopMissionName.
// Long: "a loading screen" (0), "the start screen" (1), "Mother Base",
// "Afghanistan free roam", "the Africa helicopter", "story mission N" (10000..
// 11999), otherwise "mission N". Short (HUD line, F9 header; <= 22 chars):
// "loading", "start screen", "Afghanistan", "Africa heli", "story mission N",
// "mission N". False only when `out` is too small.
bool FormatMission(std::uint32_t mission, char* out, std::size_t capacity) noexcept;
bool FormatMissionShort(std::uint32_t mission, char* out, std::size_t capacity) noexcept;

// A world_wire pair_wait code as its name (paired, off, no_link, no_hello,
// remote_off, remote_saves, same_role, local_loading, remote_loading,
// different_mission, epoch, settling); any other value gives "unknown".
const char* PairWaitName(std::uint8_t code) noexcept;

// The toast sentence for a world sync note (docs/world-sync-protocol.md NOTE
// codes; `partner` / `local` are the partner's and this game's missions, 0
// unknown). Codes 1..4 and 16 (phase 1), 5..12 (save handoff and travel lock,
// join-fix phase 2 P2-3). False when the note has no toast (code 1 with b=0,
// code 7 with b outside 1..3, code 9 with a > 100, the reserved 13..15 and
// unknown codes) or `out` is too small.
bool FormatWorldNote(std::uint8_t code, std::uint32_t a, std::uint32_t b, std::uint32_t partner, std::uint32_t local,
                     char* out, std::size_t capacity) noexcept;

// The toast sentence for an automatic save handoff event (docs/save-handoff.md;
// `code` is a save_handoff_state::Toast, G1..G10 = 1..10 and H1..H7 = 21..27,
// with `a` / `b` as listed there: kb, seconds, a mission or a
// save_handoff_wire::Fail). `partner` is the partner's display name ("" gives
// "your partner"; at most 24 characters are used, anything outside printable
// ASCII becomes '?'); mission names come from FormatMission. ASCII, at most 199
// characters. False for an unknown code or when `out` is too small.
bool FormatSaveHandoff(std::uint8_t code, std::uint32_t a, std::uint32_t b, const char* partner, char* out,
                       std::size_t capacity) noexcept;

// A few words for why a save handoff failed (a save_handoff_wire::Fail:
// "the connection dropped", "not enough disk space", ...) or why a save cannot
// be shared (a save_handoff_wire::Elig: "it is still in the story opening",
// ...). Used by FormatSaveHandoff (G7, H6) and the F9 Shared save line; an
// unknown value gives "unknown error" / "it is not checked yet".
const char* SaveHandoffFailText(std::uint8_t fail) noexcept;
const char* SaveHandoffEligText(std::uint8_t elig) noexcept;

// The F9 Partner / Co-op > Shared save status line (docs/debug-menu.md) for a
// save_handoff_state::View: `role` 1 host / 2 guest, `token` a
// save_handoff_state::Token, `pct` 0..100 (receiving / sending), `why` the
// token's reason (refused: Elig, failed: Fail), `bytes` the offered total
// (shown in KB) and `seconds_left` the restart countdown (countdown only).
// ASCII, at most 199 characters. False only when `out` is too small.
bool FormatSaveHandoffStatus(std::uint8_t role, std::uint8_t token, std::uint8_t pct, std::uint8_t why, std::uint32_t bytes,
                             std::uint32_t seconds_left, char* out, std::size_t capacity) noexcept;

}  // namespace mgscoop::coop_text
