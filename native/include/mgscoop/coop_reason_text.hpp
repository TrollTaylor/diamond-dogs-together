#pragma once
// Plain-language sentences for why a co-op link is not connected
// (docs/two-pc-play.md "When it does not connect"). The same sentences as the
// launcher: tools/CoopPlay.psm1 Get-CoopLinkReasonText and
// Get-CoopSteamEndText; both test suites read tests/fixtures/link-reasons.json.
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

}  // namespace mgscoop::coop_text
