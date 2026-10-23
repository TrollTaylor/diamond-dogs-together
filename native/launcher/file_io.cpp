// Project-authored. See file_io.hpp.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "launcher/file_io.hpp"

#include <utility>

namespace ddt {
namespace {
HANDLE AsHandle(void* h) { return static_cast<HANDLE>(h); }
}  // namespace

ReadOnlyFile::~ReadOnlyFile() { Close(); }

ReadOnlyFile::ReadOnlyFile(ReadOnlyFile&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)), size_(std::exchange(other.size_, 0)) {}

ReadOnlyFile& ReadOnlyFile::operator=(ReadOnlyFile&& other) noexcept {
    if (this != &other) {
        Close();
        handle_ = std::exchange(other.handle_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

bool ReadOnlyFile::Open(const std::wstring& path, std::string& error) {
    Close();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot open for reading (" + WindowsErrorText(GetLastError()) + ")";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0) {
        error = "cannot get the file size (" + WindowsErrorText(GetLastError()) + ")";
        CloseHandle(h);
        return false;
    }
    handle_ = h;
    size_ = static_cast<std::uint64_t>(size.QuadPart);
    return true;
}

void ReadOnlyFile::Close() {
    if (handle_) CloseHandle(AsHandle(handle_));
    handle_ = nullptr;
    size_ = 0;
}

bool ReadOnlyFile::ReadAt(std::uint64_t offset, void* buffer, std::size_t size, std::string& error) const {
    if (!handle_) {
        error = "file is not open";
        return false;
    }
    if (offset > size_ || size > size_ - offset) {
        error = "read past the end of the file";
        return false;
    }
    auto* out = static_cast<unsigned char*>(buffer);
    while (size > 0) {
        const DWORD piece = size > (1u << 26) ? (1u << 26) : static_cast<DWORD>(size);
        OVERLAPPED at{};
        at.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFu);
        at.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD got = 0;
        if (!ReadFile(AsHandle(handle_), out, piece, &got, &at)) {
            error = "read failed (" + WindowsErrorText(GetLastError()) + ")";
            return false;
        }
        if (got != piece) {
            error = "short read";
            return false;
        }
        out += got;
        offset += got;
        size -= got;
    }
    return true;
}

bool FileExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirectoryExists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

bool ReadWholeFile(const std::wstring& path, std::vector<std::uint8_t>& out, std::uint64_t max_size, std::string& error) {
    ReadOnlyFile file;
    if (!file.Open(path, error)) return false;
    if (file.size() > max_size) {
        error = "file is larger than the allowed " + std::to_string(max_size) + " bytes";
        return false;
    }
    out.assign(static_cast<std::size_t>(file.size()), 0);
    return out.empty() || file.ReadAt(0, out.data(), out.size(), error);
}

bool WriteWholeFile(const std::wstring& path, const void* data, std::size_t size, std::string& error) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot create the file (" + WindowsErrorText(GetLastError()) + ")";
        return false;
    }
    const auto* in = static_cast<const unsigned char*>(data);
    bool ok = true;
    while (ok && size > 0) {
        const DWORD piece = size > (1u << 26) ? (1u << 26) : static_cast<DWORD>(size);
        DWORD wrote = 0;
        ok = WriteFile(h, in, piece, &wrote, nullptr) && wrote == piece;
        if (!ok) error = "write failed (" + WindowsErrorText(GetLastError()) + ")";
        in += piece;
        size -= piece;
    }
    if (ok && !FlushFileBuffers(h)) {
        ok = false;
        error = "flush failed (" + WindowsErrorText(GetLastError()) + ")";
    }
    if (!CloseHandle(h) && ok) {
        ok = false;
        error = "close failed (" + WindowsErrorText(GetLastError()) + ")";
    }
    return ok;
}

std::string WindowsErrorText(unsigned long code) {
    std::string text = "Windows error " + std::to_string(code);
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
        MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT), reinterpret_cast<wchar_t*>(&message), 0, nullptr);
    if (length && message) {
        std::wstring wide(message, length);
        while (!wide.empty() && (wide.back() == L'\r' || wide.back() == L'\n' || wide.back() == L' ' || wide.back() == L'.'))
            wide.pop_back();
        if (!wide.empty()) text += ": " + Utf8FromWide(wide);
    }
    if (message) LocalFree(message);
    return text;
}

std::string Utf8FromWide(const std::wstring& text) {
    if (text.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string out(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr, nullptr);
    return out;
}

std::wstring WideFromUtf8(const std::string& text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    std::wstring out = a;
    while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
    std::size_t start = 0;
    while (start < b.size() && (b[start] == L'\\' || b[start] == L'/')) ++start;
    out += L'\\';
    out.append(b, start, std::wstring::npos);
    return out;
}

std::wstring ParentDirectory(const std::wstring& path) {
    std::wstring trimmed = path;
    while (trimmed.size() > 1 && (trimmed.back() == L'\\' || trimmed.back() == L'/')) trimmed.pop_back();
    const auto slash = trimmed.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    if (slash == 0) return trimmed.substr(0, 1);
    if (slash == 2 && trimmed[1] == L':') return trimmed.substr(0, 3);
    return trimmed.substr(0, slash);
}

}  // namespace ddt
