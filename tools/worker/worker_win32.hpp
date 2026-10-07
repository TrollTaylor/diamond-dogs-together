#pragma once
// Original Win32 support for the worker. No input injection or desktop switching.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "worker_protocol.hpp"

namespace mgsworker {
class Failure : public std::runtime_error {
public:
    DWORD code;
    explicit Failure(const char* reason, DWORD error = GetLastError()) : std::runtime_error(reason), code(error) {}
};
inline void Require(bool ok, const char* reason) { if (!ok) throw Failure(reason); }
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { Reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& h) noexcept : value(h.value) { h.value = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& h) noexcept { if (this != &h) { Reset(); value = h.value; h.value = INVALID_HANDLE_VALUE; } return *this; }
    void Reset(HANDLE h = INVALID_HANDLE_VALUE) { if (Valid()) CloseHandle(value); value = h; }
    bool Valid() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct OwnedDesktop {
    HDESK value = nullptr;
    OwnedDesktop() = default;
    explicit OwnedDesktop(HDESK h) : value(h) {}
    ~OwnedDesktop() { if (value) CloseDesktop(value); }
    OwnedDesktop(const OwnedDesktop&) = delete;
    OwnedDesktop& operator=(const OwnedDesktop&) = delete;
};
inline std::wstring Wide(const std::string& s) {
    Require(s.size() < 32768 && Utf8(s), "utf8_conversion");
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    Require(n > 0, "utf8_conversion"); std::wstring out(static_cast<std::size_t>(n), L'\0');
    Require(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n) == n, "utf8_conversion"); return out;
}
inline std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    Require(s.size() < 32768, "utf16_conversion");
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    Require(n > 0, "utf16_conversion"); std::string out(static_cast<std::size_t>(n), '\0');
    Require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr) == n, "utf16_conversion"); return out;
}
inline bool SameWide(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}
inline std::string ObjectName(HANDLE h) {
    DWORD bytes = 0; GetUserObjectInformationW(h, UOI_NAME, nullptr, 0, &bytes);
    Require(bytes >= sizeof(wchar_t) && bytes <= 1024 && bytes % sizeof(wchar_t) == 0, "user_object_name_size");
    std::vector<wchar_t> text(bytes / sizeof(wchar_t), L'\0');
    Require(GetUserObjectInformationW(h, UOI_NAME, text.data(), bytes, &bytes) != FALSE, "user_object_name");
    Require(text.back() == 0, "user_object_name_termination"); return Narrow(text.data());
}
inline std::string InputDesktop() {
    OwnedDesktop desktop(OpenInputDesktop(0, FALSE, DESKTOP_READOBJECTS));
    Require(desktop.value != nullptr, "input_desktop_unavailable"); return ObjectName(desktop.value);
}
inline void PrivateThread(const std::string& expected) {
    Require(expected.rfind("MGSWorker_", 0) == 0 && RunId(expected.substr(10)), "private_desktop_name");
    const HDESK desktop = GetThreadDesktop(GetCurrentThreadId()); // Borrowed: never close.
    Require(desktop != nullptr && ObjectName(desktop) == expected, "thread_desktop_mismatch");
    BOOL receives = TRUE; DWORD returned = 0;
    Require(GetUserObjectInformationW(desktop, UOI_IO, &receives, sizeof(receives), &returned) != FALSE && !receives, "private_desktop_receives_input");
    Require(InputDesktop() != expected, "private_desktop_is_visible");
}
inline std::uint64_t CreationTime(HANDLE process) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    Require(GetProcessTimes(process, &creation, &exit, &kernel, &user) != FALSE, "process_creation_time");
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
}
inline std::wstring ExePath(HANDLE process) {
    std::vector<wchar_t> path(32768); DWORD length = static_cast<DWORD>(path.size());
    Require(QueryFullProcessImageNameW(process, 0, path.data(), &length) != FALSE && length > 0 && length < path.size(), "process_image_path");
    return {path.data(), length};
}
inline std::wstring OwnExe() { return ExePath(GetCurrentProcess()); }
inline std::wstring FinalPath(HANDLE file) {
    std::vector<wchar_t> path(32768);
    const DWORD n = GetFinalPathNameByHandleW(file, path.data(), static_cast<DWORD>(path.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    Require(n > 0 && n < path.size(), "final_file_path"); std::wstring out(path.data(), n);
    Require(out.rfind(L"\\\\?\\", 0) == 0 && out.rfind(L"\\\\?\\UNC\\", 0) != 0, "final_path_not_drive_absolute");
    return out.substr(4);
}
inline Handle OpenRead(const std::wstring& path, bool directory = false, bool mutable_file = false) {
    Handle out(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | (mutable_file ? FILE_SHARE_WRITE : 0), nullptr,
                           OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN), nullptr));
    Require(out.Valid(), "file_open"); FILE_ATTRIBUTE_TAG_INFO tag{};
    Require(GetFileInformationByHandleEx(out.value, FileAttributeTagInfo, &tag, sizeof(tag)) != FALSE, "file_attributes");
    Require(!(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "file_reparse_point");
    Require(((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) == directory, "file_kind");
    Require(SameWide(FinalPath(out.value), path), "file_resolved_path_mismatch"); return out;
}
// Keep ancestors open without delete-sharing until all children are closed.
// This prevents path rebinding after a successful no-reparse observation.
inline std::vector<Handle> LockAncestors(const std::string& path, bool include_leaf) {
    Require(AbsolutePath(path), "absolute_path_required"); const auto wide = Wide(path);
    std::vector<Handle> held;
    for (std::size_t end = 3; end <= wide.size(); ++end) {
        if (end != 3 && end != wide.size() && wide[end] != L'\\') continue;
        if (end == wide.size() && !include_leaf) break;
        const auto prefix = wide.substr(0, end);
        Handle h(CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        Require(h.Valid(), "ancestor_open"); FILE_ATTRIBUTE_TAG_INFO tag{};
        Require(GetFileInformationByHandleEx(h.value, FileAttributeTagInfo, &tag, sizeof(tag)) != FALSE, "ancestor_attributes");
        Require((tag.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "ancestor_not_plain_directory");
        // A drive-root final path may omit its trailing slash.
        const auto final = FinalPath(h.value);
        Require(SameWide(final, prefix) || (end == 3 && SameWide(final + L"\\", prefix)), "ancestor_resolved_path_mismatch");
        held.push_back(std::move(h));
    }
    return held;
}
inline std::uint64_t Size(HANDLE file, std::uint64_t maximum) {
    LARGE_INTEGER n{}; Require(GetFileSizeEx(file, &n) != FALSE && n.QuadPart >= 0 && static_cast<std::uint64_t>(n.QuadPart) <= maximum, "file_size");
    return static_cast<std::uint64_t>(n.QuadPart);
}
inline void Rewind(HANDLE file) { LARGE_INTEGER zero{}; Require(SetFilePointerEx(file, zero, nullptr, FILE_BEGIN) != FALSE, "file_rewind"); }
inline std::string Read(HANDLE file, std::size_t maximum) {
    const auto n = Size(file, maximum); Rewind(file); std::string out(static_cast<std::size_t>(n), '\0');
    std::size_t at = 0;
    while (at < out.size()) { DWORD done = 0; Require(ReadFile(file, out.data() + at, static_cast<DWORD>(out.size() - at), &done, nullptr) != FALSE && done, "file_read"); at += done; }
    return out;
}
inline std::string ReadText(const std::wstring& path, std::size_t maximum) { auto h = OpenRead(path); return Read(h.value, maximum); }
inline bool TryText(const std::wstring& path, std::size_t maximum, std::string& out) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) { const DWORD error = GetLastError(); if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false; throw Failure("receipt_attributes", error); }
    out = ReadText(path, maximum); return true;
}
inline std::string Hash(HANDLE file) {
    BCRYPT_ALG_HANDLE provider = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    const auto status = BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    Require(status >= 0, "sha256_provider");
    struct Cleanup { BCRYPT_ALG_HANDLE p; BCRYPT_HASH_HANDLE* h; ~Cleanup() { if (*h) BCryptDestroyHash(*h); BCryptCloseAlgorithmProvider(p, 0); } } cleanup{provider, &hash};
    DWORD bytes = 0, returned = 0;
    Require(BCryptGetProperty(provider, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&bytes), sizeof(bytes), &returned, 0) >= 0 && bytes <= 65536, "sha256_object");
    std::vector<unsigned char> object(bytes), chunk(65536); unsigned char digest[32]{};
    Require(BCryptCreateHash(provider, &hash, object.data(), bytes, nullptr, 0, 0) >= 0, "sha256_create");
    const auto size = Size(file, UINT64_C(16) * 1024 * 1024 * 1024); Rewind(file); std::uint64_t read = 0;
    while (read < size) { DWORD done = 0; const DWORD ask = static_cast<DWORD>(std::min<std::uint64_t>(chunk.size(), size - read));
        Require(ReadFile(file, chunk.data(), ask, &done, nullptr) != FALSE && done > 0 && done <= ask, "sha256_read");
        Require(BCryptHashData(hash, chunk.data(), done, 0) >= 0, "sha256_update"); read += done; }
    Require(Size(file, UINT64_C(16) * 1024 * 1024 * 1024) == size && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0, "sha256_finish");
    constexpr char hex[] = "0123456789ABCDEF"; std::string out;
    for (auto byte : digest) { out += hex[byte >> 4]; out += hex[byte & 15]; } return out;
}
struct Pin { std::wstring path; Handle file; std::string digest; };
inline Pin Pinned(const std::string& path, const std::string& expected) {
    Require(AbsolutePath(path) && Sha256(expected), "file_pin_arguments");
    Pin out; out.path = Wide(path); out.file = OpenRead(out.path); out.digest = Hash(out.file.value);
    Require(EqualFold(out.digest, expected), "file_hash_mismatch"); return out;
}
inline void WriteAll(HANDLE file, const std::string& bytes) {
    Require(bytes.size() <= 2 * 1024 * 1024, "write_size"); std::size_t at = 0;
    while (at < bytes.size()) { DWORD done = 0; Require(WriteFile(file, bytes.data() + at, static_cast<DWORD>(bytes.size() - at), &done, nullptr) != FALSE && done, "file_write"); at += done; }
    Require(FlushFileBuffers(file) != FALSE, "file_flush");
}
inline void Atomic(const std::wstring& path, const std::string& bytes, bool replace = true) {
    const auto tmp = path + L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
    Handle file(CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Require(file.Valid(), "atomic_create");
    try { WriteAll(file.value, bytes); file.Reset(); Require(MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)) != FALSE, "atomic_publish"); }
    catch (...) { file.Reset(); DeleteFileW(tmp.c_str()); throw; }
}
inline std::wstring Quote(const std::wstring& s) {
    std::wstring out = L"\""; std::size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') out.append(slashes * 2 + 1, L'\\'); else out.append(slashes, L'\\');
        slashes = 0; out += c;
    }
    out.append(slashes * 2, L'\\'); return out + L'\"';
}
inline std::vector<wchar_t> Environment(const Manifest& manifest, const std::string& role, const std::string& game_dir) {
    std::map<std::wstring, std::wstring> entries;
    struct Free { LPWCH p; ~Free() { FreeEnvironmentStringsW(p); } } inherited{GetEnvironmentStringsW()};
    Require(inherited.p != nullptr, "environment_read");
    for (const wchar_t* at = inherited.p; *at; at += wcslen(at) + 1) {
        std::wstring item(at); const auto equals = item.find(L'=', item[0] == L'=' ? 1 : 0);
        Require(equals != std::wstring::npos, "environment_entry"); auto key = item.substr(0, equals);
        std::transform(key.begin(), key.end(), key.begin(), [](wchar_t c) -> wchar_t { return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - (L'a' - L'A')) : c; });
        if (key.rfind(L"MGS_WORKER_", 0) != 0) entries[key] = item.substr(equals + 1);
    }
    entries[L"MGS_WORKER_RUN_DIR"] = Wide(manifest.directory); entries[L"MGS_WORKER_RUN_ID"] = Wide(manifest.run);
    entries[L"MGS_WORKER_ROLE"] = Wide(role); entries[L"MGS_WORKER_DESKTOP"] = Wide(manifest.desktop); entries[L"MGS_WORKER_GAME_DIR"] = Wide(game_dir);
    if (manifest.mode == "mgsv") { entries[L"MGS_WORKER_MOTION_COUNT"] = Wide(manifest.At("runtime_motion_count")); entries[L"MGS_WORKER_ARCHIVE_SHA256"] = Wide(manifest.At("runtime_archive_sha256")); }
    std::vector<wchar_t> out;
    for (const auto& e : entries) { out.insert(out.end(), e.first.begin(), e.first.end()); out.push_back(L'='); out.insert(out.end(), e.second.begin(), e.second.end()); out.push_back(0); }
    out.push_back(0); Require(out.size() < 32767, "environment_size"); return out;
}
inline std::string Env(const wchar_t* key, DWORD cap = 1024) {
    std::vector<wchar_t> text(cap); const DWORD n = GetEnvironmentVariableW(key, text.data(), cap);
    Require(n > 0 && n < cap, "worker_environment"); return Narrow({text.data(), n});
}
inline std::string HResult(HRESULT result) {
    constexpr char digits[] = "0123456789ABCDEF"; std::string out = "0x00000000";
    auto value = static_cast<std::uint32_t>(result); for (int i = 9; i >= 2; --i) { out[i] = digits[value & 15]; value >>= 4; } return out;
}
} // namespace mgsworker
