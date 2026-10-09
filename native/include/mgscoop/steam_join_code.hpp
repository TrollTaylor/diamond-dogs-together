#pragma once
// Steam online join codes and invite strings (docs/steam-online.md 9.2-9.4).
// Pure and portable: built on link::Sha256, no allocation, no logging. The
// same vectors are checked against tools/CoopPlay.psm1
// (ConvertFrom-CoopSteamJoinCode / New-CoopSteamJoinCode) by the tests.
//
//   MGS2-SSSSS-SSSSS-SSSSS-SSSSS-CC@<SteamID64>
//   S  = the 20-character link secret (canonical Crockford base32)
//   CC = c1 c2 over d = SHA256("MGS2|" + S + "|" + <id64 decimal>):
//        c1 = alphabet[d[0] >> 3], c2 = alphabet[((d[0] & 7) << 2) | (d[1] >> 6)]
//   id64 = 17 decimal digits, no leading zero, an individual account
//
// Parse tolerance (as MGS1): blanks anywhere, case, dashes, O->0 and I/L->1.
// The invite / rich-presence connect string is "+mgscoop_join " + the code.
// Neither the secret nor the code is ever logged.
#include <cstddef>
#include <cstdint>
#include "mgscoop/link_crypto.hpp"

namespace mgscoop::steam_code {

inline constexpr char kPrefix[] = "MGS2";
inline constexpr char kInvitePrefix[] = "+mgscoop_join ";
inline constexpr std::size_t kInvitePrefixSize = sizeof(kInvitePrefix) - 1;
// "MGS2" + 4 x "-SSSSS" + "-CC" + "@" + 17 digits = 49 characters.
inline constexpr std::size_t kCodeChars = 49;
inline constexpr std::size_t kCodeBuffer = 64;
// "+mgscoop_join " + code: 63 characters (Steam's limit is 256).
inline constexpr std::size_t kInviteBuffer = 80;

enum class Status : std::uint8_t { ok, empty, bad_prefix, bad_char, bad_length, bad_check, bad_steamid };
const char* StatusName(Status status) noexcept;

struct Code {
    char secret[link::kSecretChars + 1];  // NUL-terminated canonical secret
    std::uint64_t host;                   // the host's SteamID64
};

// The two check characters (NUL-terminated).
void Check(const char (&secret)[link::kSecretChars], std::uint64_t host, char (&out)[3]) noexcept;
// Formats the canonical code; false for a non-canonical secret, a host that is
// not an individual SteamID or a short buffer (out[0] = 0).
bool Format(const char (&secret)[link::kSecretChars], std::uint64_t host, char* out, std::size_t capacity) noexcept;
// "+mgscoop_join " + Format(...).
bool FormatInvite(const char (&secret)[link::kSecretChars], std::uint64_t host, char* out, std::size_t capacity) noexcept;
// Parses a pasted code (tolerant, see above). On anything but ok `out` is zeroed.
Status Parse(const char* text, std::size_t size, Code& out) noexcept;

// GameRichPresenceJoinRequested_t (callback 337): {u64 friend @0; char connect[256] @8}.
enum class JoinStatus : std::uint8_t { ok, not_terminated, not_printable, not_mgscoop, extra_text, code, host_mismatch };
const char* JoinStatusName(JoinStatus status) noexcept;
struct JoinResult {
    JoinStatus status;
    Status code;  // the code parse status when status == code
};
// Strict: NUL within 256 bytes, printable ASCII, the exact prefix
// "+mgscoop_join ", then exactly one token that parses as an MGS2 code whose
// host equals `friend_id`. The reason logged for a refusal: the code status
// name for JoinStatus::code, otherwise JoinStatusName.
JoinResult ParseJoinRequest(std::uint64_t friend_id, const char* connect, std::size_t capacity, Code& out) noexcept;
const char* JoinReason(const JoinResult& result) noexcept;

// S3 (optional, needs a user decision; docs/steam-online.md 9.4): exactly one
// "+mgscoop_join <MGS2>" on a command line (UTF-16, whitespace-separated
// tokens). Not wired into the loader until S3 is approved.
enum class CommandLineStatus : std::uint8_t { ok, none, multiple, missing_code, bad_code };
const char* CommandLineStatusName(CommandLineStatus status) noexcept;
CommandLineStatus ExtractCommandLineJoin(const wchar_t* command_line, Code& out, Status& code_status) noexcept;

// Masked SteamID for logs and status files: "steam:...1234" (ASCII; the last
// four decimal digits of the SteamID64). "steam:none" for 0.
void FormatPeer(std::uint64_t id, char (&out)[24]) noexcept;
// Decimal SteamID64 (no masking), e.g. for lab.ini; false if it does not fit.
bool FormatId64(std::uint64_t id, char* out, std::size_t capacity) noexcept;
// Strict decimal SteamID64: 17 digits, no leading zero, individual.
bool ParseId64(const char* text, std::size_t size, std::uint64_t& out) noexcept;

}  // namespace mgscoop::steam_code
