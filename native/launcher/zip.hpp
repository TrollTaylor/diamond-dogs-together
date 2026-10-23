// Project-authored zip reader (stored and deflated entries) and zip writer
// (stored entries), written from the PKWARE APPNOTE layout. No zip64, no
// encryption, no multi-disk archives: those are refused.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ddt {

struct ZipLimits {
    std::size_t max_entries = 4096;
    std::uint64_t max_entry_size = 256ull << 20;   // uncompressed bytes per entry
    std::uint64_t max_total_size = 1024ull << 20;  // uncompressed bytes for the whole archive
    std::uint64_t max_archive_size = 1024ull << 20;
    std::size_t max_name_length = 240;              // bytes, whole relative path
};

struct ZipEntry {
    std::string name;  // '/'-separated relative path, checked by IsSafeZipPath
    bool is_directory = false;
    std::uint16_t method = 0;  // 0 stored, 8 deflated
    std::uint16_t flags = 0;
    std::uint32_t crc32 = 0;
    std::uint64_t compressed_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t local_header_offset = 0;
    std::uint16_t dos_time = 0;
    std::uint16_t dos_date = 0;
};

// A relative path that is safe to create under a destination folder on
// Windows: no absolute or drive paths, no backslashes or colons, no '.' or
// '..' segments, no empty segments, no control characters, no reserved device
// names and no segment ending in a dot or space. A trailing '/' marks a folder.
bool IsSafeZipPath(const std::string& name, std::size_t max_length = 240);

class ZipReader {
public:
    // Reads and validates the central directory. Refuses unsafe or duplicate
    // (case-insensitive) names, unsupported methods, encryption and limits.
    bool Open(std::vector<std::uint8_t> archive, const ZipLimits& limits, std::string& error);
    bool OpenFile(const std::wstring& path, const ZipLimits& limits, std::string& error);

    const std::vector<ZipEntry>& entries() const { return entries_; }
    // Case-sensitive exact match; nullptr when absent.
    const ZipEntry* Find(const std::string& name) const;
    // Decompresses one entry and verifies its size and CRC-32.
    bool Extract(const ZipEntry& entry, std::vector<std::uint8_t>& out, std::string& error) const;

private:
    std::vector<std::uint8_t> archive_;
    std::vector<ZipEntry> entries_;
    std::uint64_t central_directory_offset_ = 0;
    ZipLimits limits_{};
};

// Builds a zip in memory with stored (uncompressed) entries.
class ZipWriter {
public:
    // Default stamp is 1980-01-01 00:00:00, which keeps output reproducible.
    void SetTimestamp(std::uint16_t dos_time, std::uint16_t dos_date) {
        dos_time_ = dos_time;
        dos_date_ = dos_date;
    }
    bool AddFile(const std::string& name, const void* data, std::size_t size, std::string& error);
    bool AddFile(const std::string& name, const std::vector<std::uint8_t>& data, std::string& error) {
        return AddFile(name, data.data(), data.size(), error);
    }
    bool AddDirectory(const std::string& name, std::string& error);  // name must end with '/'
    // Appends the central directory; the writer cannot be used afterwards.
    bool Finish(std::vector<std::uint8_t>& out, std::string& error);

private:
    bool Add(const std::string& name, const void* data, std::size_t size, std::string& error);
    std::vector<std::uint8_t> body_;
    std::vector<std::uint8_t> directory_;
    std::vector<std::string> names_;
    std::size_t count_ = 0;
    std::uint16_t dos_time_ = 0;
    std::uint16_t dos_date_ = (0 << 9) | (1 << 5) | 1;
    bool finished_ = false;
};

// Packs a calendar time into the MS-DOS fields zip uses (2-second resolution,
// years 1980..2107). Out-of-range values are clamped.
void ToDosDateTime(int year, int month, int day, int hour, int minute, int second, std::uint16_t& dos_time,
                   std::uint16_t& dos_date);

}  // namespace ddt
