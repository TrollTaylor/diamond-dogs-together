// Diamond Dogs Together launcher core: text, time, hashing and file helpers.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

#include "core_util.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "launcher/hash.hpp"

namespace ddt::core {

// ---- Text ------------------------------------------------------------------
std::string Utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string UpperAscii(std::string text) {
    for (char& c : text)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return text;
}

std::string LowerAscii(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::wstring UpperInvariant(const std::wstring& text) {
    if (text.empty()) return text;
    const int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.c_str(), static_cast<int>(text.size()), nullptr,
                                0, nullptr, nullptr, 0);
    if (n <= 0) return text;
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, text.c_str(), static_cast<int>(text.size()), out.data(), n, nullptr,
                  nullptr, 0);
    return out;
}

bool IEquals(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool IEqualsAscii(const std::string& a, const std::string& b) { return LowerAscii(a) == LowerAscii(b); }

bool StartsWith(const std::string& text, const std::string& prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string Trim(const std::string& text) {
    std::size_t b = 0, e = text.size();
    auto blank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    while (b < e && blank(text[b])) ++b;
    while (e > b && blank(text[e - 1])) --e;
    return text.substr(b, e - b);
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t nl = text.find('\n', start);
        std::size_t end = nl == std::string::npos ? text.size() : nl;
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(std::move(line));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

bool IsHexText(const std::string& text, std::size_t digits) {
    if (text.size() != digits) return false;
    for (char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    return true;
}

bool IsAsciiPrintable(const std::wstring& text) {
    for (wchar_t c : text)
        if (c < 0x20 || c > 0x7E) return false;
    return true;
}

std::string Decimal(std::uint64_t value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    return buf;
}

// ---- Hashes ----------------------------------------------------------------
std::string Sha256Hex(const void* data, std::size_t size) {
    Sha256Digest d{};
    if (!Sha256(data, size, d)) return {};
    return UpperAscii(ToHex(d));
}

std::string Sha256Hex(const std::string& data) { return Sha256Hex(data.data(), data.size()); }

bool Sha256FileHex(const std::wstring& path, std::string& hex, std::uint64_t* size) {
    hex.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    Sha256Stream stream;
    std::vector<std::uint8_t> buffer(1u << 20);
    std::uint64_t total = 0;
    bool ok = stream.ok();
    while (ok) {
        DWORD got = 0;
        if (!ReadFile(h, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) {
            ok = false;
            break;
        }
        if (got == 0) break;
        total += got;
        ok = stream.Update(buffer.data(), got);
    }
    CloseHandle(h);
    Sha256Digest d{};
    if (!ok || !stream.Finish(d)) return false;
    hex = UpperAscii(ToHex(d));
    if (size) *size = total;
    return true;
}

bool RandomBytes(void* out, std::size_t size) {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, static_cast<PUCHAR>(out), static_cast<ULONG>(size),
                                          BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

std::string RandomHex(std::size_t bytes) {
    std::vector<std::uint8_t> b(bytes);
    if (!RandomBytes(b.data(), b.size())) {
        // Fall back to a time-based value: names only, never secrets.
        LARGE_INTEGER t{};
        QueryPerformanceCounter(&t);
        for (std::size_t i = 0; i < b.size(); ++i) b[i] = static_cast<std::uint8_t>((t.QuadPart >> ((i % 8) * 8)) ^ (i * 131));
    }
    return ToHex(b.data(), b.size());
}

// ---- Time ------------------------------------------------------------------
namespace {
// Howard Hinnant's days_from_civil / civil_from_days.
std::int64_t DaysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void CivilFromDays(std::int64_t z, int& y, int& m, int& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yy + (m <= 2));
}

bool Digits(const std::string& s, std::size_t pos, std::size_t n, int& out) {
    if (pos + n > s.size()) return false;
    int v = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = s[pos + i];
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    out = v;
    return true;
}
}  // namespace

Civil CivilFromMs(std::int64_t ms) {
    std::int64_t days = ms / 86400000;
    std::int64_t rem = ms % 86400000;
    if (rem < 0) {
        rem += 86400000;
        --days;
    }
    Civil c;
    CivilFromDays(days, c.year, c.month, c.day);
    c.hour = static_cast<int>(rem / 3600000);
    c.minute = static_cast<int>(rem / 60000 % 60);
    c.second = static_cast<int>(rem / 1000 % 60);
    c.millisecond = static_cast<int>(rem % 1000);
    return c;
}

std::int64_t MsFromCivil(const Civil& c) {
    return DaysFromCivil(c.year, c.month, c.day) * 86400000 + c.hour * 3600000ll + c.minute * 60000ll + c.second * 1000ll +
           c.millisecond;
}

std::string FormatUtcRoundTrip(std::int64_t ms) {
    const Civil c = CivilFromMs(ms);
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d0000Z", c.year, c.month, c.day, c.hour, c.minute, c.second,
                  c.millisecond);
    return buf;
}

std::string FormatUtcSeconds(std::int64_t ms) {
    const Civil c = CivilFromMs(ms);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", c.year, c.month, c.day, c.hour, c.minute, c.second);
    return buf;
}

bool ParseUtc(const std::string& raw, std::int64_t& ms) {
    const std::string s = Trim(raw);
    Civil c;
    if (!Digits(s, 0, 4, c.year) || s.size() < 19 || s[4] != '-' || !Digits(s, 5, 2, c.month) || s[7] != '-' ||
        !Digits(s, 8, 2, c.day) || (s[10] != 'T' && s[10] != ' ') || !Digits(s, 11, 2, c.hour) || s[13] != ':' ||
        !Digits(s, 14, 2, c.minute) || s[16] != ':' || !Digits(s, 17, 2, c.second))
        return false;
    if (c.month < 1 || c.month > 12 || c.day < 1 || c.day > 31 || c.hour > 23 || c.minute > 59 || c.second > 60) return false;
    std::size_t pos = 19;
    if (pos < s.size() && s[pos] == '.') {
        ++pos;
        int digits = 0, value = 0;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9') {
            if (digits < 3) value = value * 10 + (s[pos] - '0');
            ++digits;
            ++pos;
        }
        if (digits == 0) return false;
        for (int d = digits; d < 3; ++d) value *= 10;
        c.millisecond = value;
    }
    std::int64_t offset_ms = 0;
    if (pos < s.size()) {
        if (s[pos] == 'Z' || s[pos] == 'z') {
            ++pos;
        } else if (s[pos] == '+' || s[pos] == '-') {
            int oh = 0, om = 0;
            const bool neg = s[pos] == '-';
            if (!Digits(s, pos + 1, 2, oh)) return false;
            std::size_t p = pos + 3;
            if (p < s.size() && s[p] == ':') ++p;
            if (!Digits(s, p, 2, om)) return false;
            offset_ms = (oh * 60 + om) * 60000ll * (neg ? -1 : 1);
            pos = p + 2;
        }
    }
    if (pos != s.size()) return false;
    ms = MsFromCivil(c) - offset_ms;
    return true;
}

std::string FormatStamp(const Civil& c) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", c.year, c.month, c.day, c.hour, c.minute, c.second);
    return buf;
}

// ---- Paths -----------------------------------------------------------------
std::wstring JoinPath2(const std::wstring& a, const std::wstring& b) {
    std::wstring tail = b;
    std::replace(tail.begin(), tail.end(), L'/', L'\\');
    while (!tail.empty() && tail.front() == L'\\') tail.erase(tail.begin());
    if (a.empty()) return tail;
    if (a.back() == L'\\' || a.back() == L'/') return a + tail;
    return a + L"\\" + tail;
}

bool IsDriveRoot(const std::wstring& full) {
    return (full.size() == 2 && full[1] == L':') || (full.size() == 3 && full[1] == L':' && (full[2] == L'\\' || full[2] == L'/'));
}

std::wstring FullPath(const std::wstring& path) {
    if (path.empty()) return path;
    DWORD n = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    std::wstring out;
    if (n == 0) {
        out = path;
    } else {
        out.resize(n);
        n = GetFullPathNameW(path.c_str(), n, out.data(), nullptr);
        out.resize(n);
    }
    std::replace(out.begin(), out.end(), L'/', L'\\');
    while (out.size() > 3 && out.back() == L'\\') out.pop_back();
    if (out.size() == 3 && out[1] == L':' && out[2] == L'\\') return out;
    return out;
}

std::wstring ParentOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    if (slash == 2 && path.size() > 1 && path[1] == L':') return path.substr(0, 3);
    return path.substr(0, slash);
}

std::wstring LeafOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

bool PathInside(const std::wstring& child, const std::wstring& parent) {
    const std::wstring c = FullPath(child), p = FullPath(parent);
    if (IEquals(c, p)) return true;
    std::wstring prefix = p;
    if (prefix.empty()) return false;
    if (prefix.back() != L'\\') prefix += L'\\';
    return c.size() > prefix.size() && IEquals(c.substr(0, prefix.size()), prefix);
}

// ---- Files -----------------------------------------------------------------
namespace {
DWORD Attributes(const std::wstring& path) { return GetFileAttributesW(path.c_str()); }

std::int64_t FileTimeToUnixMs(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>(u.QuadPart / 10000ull) - 11644473600000ll;
}
}  // namespace

bool PathExists(const std::wstring& path) { return Attributes(path) != INVALID_FILE_ATTRIBUTES; }
bool IsFile(const std::wstring& path) {
    const DWORD a = Attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool IsDir(const std::wstring& path) {
    const DWORD a = Attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool IsReparsePoint(const std::wstring& path) {
    const DWORD a = Attributes(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool HasReparsePoint(const std::wstring& path, const std::wstring& stop_at) {
    std::wstring cursor = FullPath(path);
    const std::wstring stop = stop_at.empty() ? std::wstring() : FullPath(stop_at);
    while (!cursor.empty()) {
        if (IsReparsePoint(cursor)) return true;
        if (!stop.empty() && IEquals(cursor, stop)) break;
        const std::wstring parent = ParentOf(cursor);
        if (parent.empty() || parent == cursor) break;
        cursor = parent;
    }
    return false;
}

std::uint64_t FileSizeOrZero(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 0;
    return (static_cast<std::uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
}

bool ReadFileShared(const std::wstring& path, Bytes& out, std::uint64_t max_size, std::string& error) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = WinError(GetLastError());
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || static_cast<std::uint64_t>(size.QuadPart) > max_size) {
        CloseHandle(h);
        error = "the file is too large";
        return false;
    }
    out.resize(static_cast<std::size_t>(size.QuadPart));
    std::size_t done = 0;
    while (done < out.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<std::size_t>(out.size() - done, 1u << 24));
        if (!ReadFile(h, out.data() + done, want, &got, nullptr) || got == 0) break;
        done += got;
    }
    CloseHandle(h);
    out.resize(done);
    return true;
}

bool ReadTextShared(const std::wstring& path, std::string& out, std::uint64_t max_size) {
    Bytes b;
    std::string error;
    out.clear();
    if (!ReadFileShared(path, b, max_size, error)) return false;
    out.assign(b.begin(), b.end());
    return true;
}

std::string ReadTail(const std::wstring& path, std::uint64_t max_bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return {};
    }
    const std::uint64_t length = static_cast<std::uint64_t>(size.QuadPart);
    const std::uint64_t count = std::min(length, max_bytes);
    const std::uint64_t offset = length - count;
    std::string text(static_cast<std::size_t>(count), '\0');
    LARGE_INTEGER pos{};
    pos.QuadPart = static_cast<LONGLONG>(offset);
    std::size_t done = 0;
    if (SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
        while (done < text.size()) {
            DWORD got = 0;
            if (!ReadFile(h, text.data() + done, static_cast<DWORD>(text.size() - done), &got, nullptr) || got == 0) break;
            done += got;
        }
    }
    CloseHandle(h);
    text.resize(done);
    if (offset > 0) {
        const std::size_t nl = text.find('\n');
        if (nl != std::string::npos) text = text.substr(nl + 1);
        text = "[... " + Decimal(offset) + " earlier bytes omitted ...]\n" + text;
    }
    return text;
}

void ReadNewLines(const std::wstring& path, std::int64_t& offset, std::string& text_out, std::size_t max_bytes) {
    text_out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return;
    }
    std::int64_t start = offset;
    if (size.QuadPart < start) start = 0;
    const std::int64_t available = size.QuadPart - start;
    const std::size_t count = static_cast<std::size_t>(std::min<std::int64_t>(available, static_cast<std::int64_t>(max_bytes)));
    if (count == 0) {
        offset = start;
        CloseHandle(h);
        return;
    }
    std::string buffer(count, '\0');
    LARGE_INTEGER pos{};
    pos.QuadPart = start;
    std::size_t done = 0;
    if (SetFilePointerEx(h, pos, nullptr, FILE_BEGIN)) {
        while (done < count) {
            DWORD got = 0;
            if (!ReadFile(h, buffer.data() + done, static_cast<DWORD>(count - done), &got, nullptr) || got == 0) break;
            done += got;
        }
    }
    CloseHandle(h);
    buffer.resize(done);
    const std::size_t last = buffer.rfind('\n');
    if (last == std::string::npos) {
        offset = start;
        return;
    }
    text_out = buffer.substr(0, last + 1);
    offset = start + static_cast<std::int64_t>(last + 1);
}

bool WriteAtomic(const std::wstring& path, const void* data, std::size_t size, bool replace, std::string& error) {
    const std::wstring temp = path + L"." + Wide(RandomHex(16)) + L".tmp";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot create a temporary file: " + WinError(GetLastError());
        return false;
    }
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::size_t done = 0;
    bool ok = true;
    while (done < size) {
        DWORD wrote = 0;
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(size - done, 1u << 24));
        if (!WriteFile(h, p + done, chunk, &wrote, nullptr) || wrote == 0) {
            ok = false;
            error = "cannot write: " + WinError(GetLastError());
            break;
        }
        done += wrote;
    }
    if (ok && !FlushFileBuffers(h)) {
        ok = false;
        error = "cannot flush: " + WinError(GetLastError());
    }
    CloseHandle(h);
    if (ok) {
        Bytes check;
        std::string read_error;
        if (!ReadFileShared(temp, check, static_cast<std::uint64_t>(size) + 1, read_error) || check.size() != size ||
            (size && std::memcmp(check.data(), data, size) != 0)) {
            ok = false;
            error = "the written file did not read back the same";
        }
    }
    if (ok) {
        ok = false;
        for (int attempt = 1; attempt <= 5 && !ok; ++attempt) {
            const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
            if (MoveFileExW(temp.c_str(), path.c_str(), flags)) {
                ok = true;
                break;
            }
            const DWORD e = GetLastError();
            error = WinError(e);
            if (!replace && (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS)) {
                error = "the file already exists";
                break;
            }
            Sleep(50 * attempt);
        }
    }
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

bool CreateDirectories(const std::wstring& path) {
    if (path.empty()) return false;
    if (IsDir(path)) return true;
    const std::wstring parent = ParentOf(path);
    if (!parent.empty() && parent != path && !IsDir(parent) && !CreateDirectories(parent)) return false;
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<DirEntry> ListDirectory(const std::wstring& path) {
    std::vector<DirEntry> out;
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW(JoinPath2(path, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        DirEntry e;
        e.name = name;
        e.directory = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.reparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
        e.size = (static_cast<std::uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        e.write_ms = FileTimeToUnixMs(fd.ftLastWriteTime);
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
        return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return out;
}

bool MovePath(const std::wstring& from, const std::wstring& to, std::string& error) {
    for (int attempt = 1; attempt <= 5; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
        const DWORD e = GetLastError();
        error = WinError(e);
        if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS || e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            return false;
        Sleep(100 * attempt);
    }
    return false;
}

bool CopyFileChecked(const std::wstring& from, const std::wstring& to, bool overwrite, std::string& error) {
    if (!CopyFileW(from.c_str(), to.c_str(), overwrite ? FALSE : TRUE)) {
        error = WinError(GetLastError());
        return false;
    }
    // Copies keep the read-only attribute of a source; the copy is ours to manage.
    const DWORD a = GetFileAttributesW(to.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(to.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
    return true;
}

bool DeleteFileChecked(const std::wstring& path) {
    if (DeleteFileW(path.c_str())) return true;
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesW(path.c_str(), a & ~FILE_ATTRIBUTE_READONLY);
        return DeleteFileW(path.c_str()) != 0;
    }
    return !PathExists(path);
}

bool CopyTree(const std::wstring& from, const std::wstring& to, std::string& error) {
    if (IsReparsePoint(from)) {
        error = "filesystem links are not supported here";
        return false;
    }
    if (!CreateDirectoryW(to.c_str(), nullptr)) {
        error = WinError(GetLastError());
        return false;
    }
    for (const DirEntry& e : ListDirectory(from)) {
        if (e.reparse) {
            error = "filesystem links are not supported here";
            return false;
        }
        const std::wstring a = JoinPath2(from, e.name), b = JoinPath2(to, e.name);
        if (e.directory) {
            if (!CopyTree(a, b, error)) return false;
        } else if (!CopyFileChecked(a, b, false, error)) {
            return false;
        }
    }
    return true;
}

bool RemoveTree(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    if (a == INVALID_FILE_ATTRIBUTES) return true;
    if (!(a & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileChecked(path);
    if (a & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(path.c_str()) != 0;   // the link only
    bool ok = true;
    for (const DirEntry& e : ListDirectory(path)) {
        const std::wstring child = JoinPath2(path, e.name);
        if (e.directory && !e.reparse) ok = RemoveTree(child) && ok;
        else if (e.directory) ok = (RemoveDirectoryW(child.c_str()) != 0) && ok;
        else ok = DeleteFileChecked(child) && ok;
    }
    return RemoveDirectoryW(path.c_str()) != 0 && ok;
}

std::wstring UniquePath(const std::wstring& base, const std::wstring& suffix) {
    std::wstring candidate = base + suffix;
    for (int n = 1; PathExists(candidate) && n < 100000; ++n) candidate = base + L"-" + std::to_wstring(n) + suffix;
    return candidate;
}

std::string WinError(unsigned long code) {
    wchar_t* text = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    std::string out = "Windows error " + Decimal(code);
    if (n && text) {
        std::string msg = Trim(Utf8(std::wstring(text, n)));
        if (!msg.empty()) out += ": " + msg;
    }
    if (text) LocalFree(text);
    return out;
}

}  // namespace ddt::core
