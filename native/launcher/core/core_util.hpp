// Diamond Dogs Together launcher core: text, time, hashing and file helpers.
// Project-authored. Every write that matters goes through WriteAtomic (temp
// file next to the target, flushed, re-read and compared, then moved in).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ddt::core {

using Bytes = std::vector<std::uint8_t>;

// ---- Text ------------------------------------------------------------------
std::string Utf8(const std::wstring& text);
std::wstring Wide(const std::string& text);
std::string UpperAscii(std::string text);
std::string LowerAscii(std::string text);
std::wstring UpperInvariant(const std::wstring& text);   // like .NET ToUpperInvariant
bool IEquals(const std::wstring& a, const std::wstring& b);   // ordinal, ignoring case
bool IEqualsAscii(const std::string& a, const std::string& b);
bool StartsWith(const std::string& text, const std::string& prefix);
bool EndsWith(const std::string& text, const std::string& suffix);
std::string Trim(const std::string& text);
// Lines split at '\n' with a trailing '\r' removed; a final line without '\n' is kept.
std::vector<std::string> SplitLines(const std::string& text);
bool IsHexText(const std::string& text, std::size_t digits);
bool IsAsciiPrintable(const std::wstring& text);   // 0x20..0x7E only
std::string Decimal(std::uint64_t value);

// ---- Hashes and randomness ---------------------------------------------------
std::string Sha256Hex(const void* data, std::size_t size);   // upper case, like Get-FileHash
std::string Sha256Hex(const std::string& data);
// Upper-case SHA-256 of a whole file (shared read); false when it cannot be read.
bool Sha256FileHex(const std::wstring& path, std::string& hex, std::uint64_t* size = nullptr);
bool RandomBytes(void* out, std::size_t size);
std::string RandomHex(std::size_t bytes);   // lower case (a GUID's "N" form for 16 bytes)

// ---- Time: milliseconds since 1970-01-01 UTC ---------------------------------
struct Civil {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, millisecond = 0;
};
Civil CivilFromMs(std::int64_t ms);
std::int64_t MsFromCivil(const Civil& c);
std::string FormatUtcRoundTrip(std::int64_t ms);   // 2026-10-23T18:14:03.1900000Z (.NET "o")
std::string FormatUtcSeconds(std::int64_t ms);     // 2026-10-23T18:14:03Z
// ISO 8601 date and time, 'T' or ' ', optional fraction, 'Z' / +hh:mm / -hh:mm
// (no zone = UTC, like the PowerShell tools' AssumeUniversal).
bool ParseUtc(const std::string& text, std::int64_t& ms);
std::string FormatStamp(const Civil& local);       // yyyyMMdd-HHmmss

// ---- Paths -----------------------------------------------------------------
std::wstring JoinPath2(const std::wstring& a, const std::wstring& b);   // b may hold '/' separators
std::wstring FullPath(const std::wstring& path);   // GetFullPathNameW, trailing separators trimmed (not "C:\")
std::wstring ParentOf(const std::wstring& path);
std::wstring LeafOf(const std::wstring& path);
bool IsDriveRoot(const std::wstring& full);
bool PathInside(const std::wstring& child, const std::wstring& parent);   // equal or below, ignoring case

// ---- Files and folders ---------------------------------------------------------
bool PathExists(const std::wstring& path);
bool IsFile(const std::wstring& path);
bool IsDir(const std::wstring& path);
bool IsReparsePoint(const std::wstring& path);
// True when `path` or an existing ancestor down to and including `stop_at`
// is a reparse point (junction, symbolic link).
bool HasReparsePoint(const std::wstring& path, const std::wstring& stop_at);
std::uint64_t FileSizeOrZero(const std::wstring& path);

// Reads with FILE_SHARE_READ | WRITE | DELETE (the game may be writing).
bool ReadFileShared(const std::wstring& path, Bytes& out, std::uint64_t max_size, std::string& error);
bool ReadTextShared(const std::wstring& path, std::string& out, std::uint64_t max_size = 1u << 20);
// The last `max_bytes` of a file, starting at a line boundary, with a note
// when something was dropped (Read-FriendFileTail). "" when absent.
std::string ReadTail(const std::wstring& path, std::uint64_t max_bytes);
// Complete lines appended since `offset` (Read-LogFromOffset): a file shorter
// than `offset` was replaced, so reading restarts at 0.
void ReadNewLines(const std::wstring& path, std::int64_t& offset, std::string& text_out, std::size_t max_bytes = 8u << 20);

// Temp file "<path>.<32 hex>.tmp" next to the target, written, flushed,
// re-read and compared, then moved in. replace=false fails when `path`
// exists. The temp file never stays behind.
bool WriteAtomic(const std::wstring& path, const void* data, std::size_t size, bool replace, std::string& error);
inline bool WriteAtomic(const std::wstring& path, const std::string& text, bool replace, std::string& error) {
    return WriteAtomic(path, text.data(), text.size(), replace, error);
}
bool CreateDirectories(const std::wstring& path);

struct DirEntry {
    std::wstring name;
    bool directory = false;
    bool reparse = false;
    std::uint64_t size = 0;
    std::int64_t write_ms = 0;   // last write, UTC ms
};
std::vector<DirEntry> ListDirectory(const std::wstring& path);   // sorted by name (ordinal, ignoring case)

// MoveFileExW without replacing, retried 5 times (an antivirus or indexer
// may hold a handle for a moment).
bool MovePath(const std::wstring& from, const std::wstring& to, std::string& error);
bool CopyFileChecked(const std::wstring& from, const std::wstring& to, bool overwrite, std::string& error);
bool DeleteFileChecked(const std::wstring& path);
// Copies a folder tree (refuses reparse points inside it).
bool CopyTree(const std::wstring& from, const std::wstring& to, std::string& error);
// Deletes a folder tree this program created (never follows reparse points).
bool RemoveTree(const std::wstring& path);
// `base` + `suffix`, or base + "-1" + suffix, "-2"... when taken.
std::wstring UniquePath(const std::wstring& base, const std::wstring& suffix);
std::string WinError(unsigned long code);

}  // namespace ddt::core
