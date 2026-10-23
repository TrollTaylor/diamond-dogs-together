// Project-authored. Small Win32 file helpers: positioned reads with 64-bit
// offsets (game archives are larger than 2 GB) and whole-file read/write.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ddt {

// Read-only, share-read handle. Never opens for writing.
class ReadOnlyFile {
public:
    ReadOnlyFile() = default;
    ~ReadOnlyFile();
    ReadOnlyFile(const ReadOnlyFile&) = delete;
    ReadOnlyFile& operator=(const ReadOnlyFile&) = delete;
    ReadOnlyFile(ReadOnlyFile&& other) noexcept;
    ReadOnlyFile& operator=(ReadOnlyFile&& other) noexcept;

    bool Open(const std::wstring& path, std::string& error);
    void Close();
    bool is_open() const { return handle_ != nullptr; }
    std::uint64_t size() const { return size_; }
    // Reads exactly `size` bytes at `offset`; fails on a short read.
    bool ReadAt(std::uint64_t offset, void* buffer, std::size_t size, std::string& error) const;

private:
    void* handle_ = nullptr;
    std::uint64_t size_ = 0;
};

bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
bool ReadWholeFile(const std::wstring& path, std::vector<std::uint8_t>& out, std::uint64_t max_size, std::string& error);
// CREATE_ALWAYS, write, flush to disk, close.
bool WriteWholeFile(const std::wstring& path, const void* data, std::size_t size, std::string& error);
// "Windows error N: <system text>" for messages.
std::string WindowsErrorText(unsigned long code);
std::string Utf8FromWide(const std::wstring& text);
std::wstring WideFromUtf8(const std::string& text);
// Joins with a single backslash.
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring ParentDirectory(const std::wstring& path);

}  // namespace ddt
