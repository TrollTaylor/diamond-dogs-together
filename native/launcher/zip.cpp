// Project-authored. See zip.hpp. Record layouts follow the PKWARE APPNOTE:
// local file header (30 bytes + name + extra), central directory header
// (46 bytes + name + extra + comment), end of central directory (22 bytes + comment).
#include "launcher/zip.hpp"

#include <algorithm>
#include <set>

#include "launcher/crc32.hpp"
#include "launcher/file_io.hpp"
#include "launcher/inflate.hpp"

namespace ddt {
namespace {

constexpr std::uint32_t kLocalSignature = 0x04034b50;
constexpr std::uint32_t kCentralSignature = 0x02014b50;
constexpr std::uint32_t kEndSignature = 0x06054b50;

std::uint16_t Get16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
std::uint32_t Get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
void Put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v));
    out.push_back(static_cast<std::uint8_t>(v >> 8));
}
void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    Put16(out, v & 0xFFFFu);
    Put16(out, v >> 16);
}

std::string Fold(std::string name) {
    if (!name.empty() && name.back() == '/') name.pop_back();
    for (char& c : name)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return name;
}

bool IsReservedDeviceName(const std::string& segment) {
    std::string base = segment.substr(0, segment.find('.'));
    for (char& c : base)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") return true;
    if (base.size() == 4 && (base.compare(0, 3, "COM") == 0 || base.compare(0, 3, "LPT") == 0) && base[3] >= '0' &&
        base[3] <= '9')
        return true;
    return false;
}

// Names (folded) that are files and folders at the same time, or repeated.
bool NamesConflict(const std::vector<std::string>& names, std::string& clash) {
    std::set<std::string> files, folders;
    for (const std::string& raw : names) {
        const bool folder = !raw.empty() && raw.back() == '/';
        const std::string name = Fold(raw);
        for (std::size_t slash = name.find('/'); slash != std::string::npos; slash = name.find('/', slash + 1))
            folders.insert(name.substr(0, slash));
        if (folder) {
            folders.insert(name);
        } else if (!files.insert(name).second) {
            clash = raw;
            return true;
        }
    }
    for (const std::string& name : files)
        if (folders.count(name)) {
            clash = name;
            return true;
        }
    return false;
}

}  // namespace

bool IsSafeZipPath(const std::string& name, std::size_t max_length) {
    if (name.empty() || name.size() > max_length || name.front() == '/') return false;
    for (const char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7E) return false;
        if (std::string("\\:*?\"<>|").find(ch) != std::string::npos) return false;
    }
    std::string body = name;
    if (body.back() == '/') body.pop_back();
    if (body.empty()) return false;
    std::size_t start = 0;
    while (start <= body.size()) {
        const std::size_t end = std::min(body.find('/', start), body.size());
        const std::string segment = body.substr(start, end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        if (segment.back() == '.' || segment.back() == ' ') return false;
        if (IsReservedDeviceName(segment)) return false;
        start = end + 1;
    }
    return true;
}

bool ZipReader::OpenFile(const std::wstring& path, const ZipLimits& limits, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!ReadWholeFile(path, bytes, limits.max_archive_size, error)) {
        error = "zip: " + error;
        return false;
    }
    return Open(std::move(bytes), limits, error);
}

bool ZipReader::Open(std::vector<std::uint8_t> archive, const ZipLimits& limits, std::string& error) {
    archive_.clear();
    entries_.clear();
    limits_ = limits;
    const std::size_t size = archive.size();
    if (size > limits.max_archive_size) {
        error = "zip: archive is larger than allowed";
        return false;
    }
    if (size < 22) {
        error = "zip: not a zip archive (too small)";
        return false;
    }
    // The end record is the last thing in the file; its comment must reach EOF exactly.
    std::size_t end_pos = SIZE_MAX;
    const std::size_t lowest = size - 22 > 0xFFFF ? size - 22 - 0xFFFF : 0;
    for (std::size_t pos = size - 22 + 1; pos-- > lowest;) {
        const std::uint8_t* p = archive.data() + pos;
        if (Get32(p) == kEndSignature && pos + 22 + Get16(p + 20) == size) {
            end_pos = pos;
            break;
        }
    }
    if (end_pos == SIZE_MAX) {
        error = "zip: end of central directory not found";
        return false;
    }
    const std::uint8_t* e = archive.data() + end_pos;
    const std::uint16_t disk = Get16(e + 4), directory_disk = Get16(e + 6);
    const std::uint16_t disk_entries = Get16(e + 8), total_entries = Get16(e + 10);
    const std::uint32_t directory_size = Get32(e + 12), directory_offset = Get32(e + 16);
    if (total_entries == 0xFFFF || directory_size == 0xFFFFFFFFu || directory_offset == 0xFFFFFFFFu) {
        error = "zip: zip64 archives are not supported";
        return false;
    }
    if (disk != 0 || directory_disk != 0 || disk_entries != total_entries) {
        error = "zip: multi-disk archives are not supported";
        return false;
    }
    if (directory_offset > end_pos || directory_size > end_pos - directory_offset) {
        error = "zip: central directory is out of range";
        return false;
    }
    if (total_entries > limits.max_entries) {
        error = "zip: too many entries";
        return false;
    }
    const std::size_t directory_end = directory_offset + directory_size;
    std::size_t pos = directory_offset;
    std::uint64_t total_size = 0;
    std::vector<ZipEntry> entries;
    std::vector<std::string> names;
    for (unsigned i = 0; i < total_entries; ++i) {
        if (directory_end - pos < 46 || Get32(archive.data() + pos) != kCentralSignature) {
            error = "zip: damaged central directory";
            return false;
        }
        const std::uint8_t* c = archive.data() + pos;
        ZipEntry entry;
        entry.flags = Get16(c + 8);
        entry.method = Get16(c + 10);
        entry.dos_time = Get16(c + 12);
        entry.dos_date = Get16(c + 14);
        entry.crc32 = Get32(c + 16);
        const std::uint32_t compressed = Get32(c + 20), uncompressed = Get32(c + 24);
        const std::uint16_t name_length = Get16(c + 28), extra_length = Get16(c + 30), comment_length = Get16(c + 32);
        const std::uint16_t start_disk = Get16(c + 34);
        const std::uint32_t local_offset = Get32(c + 42);
        const std::size_t record = 46u + name_length + extra_length + comment_length;
        if (directory_end - pos < record) {
            error = "zip: damaged central directory";
            return false;
        }
        entry.name.assign(reinterpret_cast<const char*>(c + 46), name_length);
        pos += record;
        if (compressed == 0xFFFFFFFFu || uncompressed == 0xFFFFFFFFu || local_offset == 0xFFFFFFFFu) {
            error = "zip: zip64 entries are not supported";
            return false;
        }
        if (start_disk != 0) {
            error = "zip: multi-disk archives are not supported";
            return false;
        }
        if (entry.flags & (0x0001u | 0x0040u | 0x2000u)) {
            error = "zip: encrypted entries are not supported";
            return false;
        }
        if (entry.method != 0 && entry.method != 8) {
            error = "zip: entry uses an unsupported compression method";
            return false;
        }
        if (!IsSafeZipPath(entry.name, limits.max_name_length)) {
            error = "zip: unsafe entry name";
            return false;
        }
        entry.is_directory = entry.name.back() == '/';
        entry.compressed_size = compressed;
        entry.uncompressed_size = uncompressed;
        entry.local_header_offset = local_offset;
        if (entry.is_directory && uncompressed != 0) {
            error = "zip: folder entry has data";
            return false;
        }
        if (entry.method == 0 && compressed != uncompressed) {
            error = "zip: stored entry sizes disagree";
            return false;
        }
        if (uncompressed > limits.max_entry_size) {
            error = "zip: entry is larger than allowed";
            return false;
        }
        total_size += uncompressed;
        if (total_size > limits.max_total_size) {
            error = "zip: archive content is larger than allowed";
            return false;
        }
        if (local_offset > directory_offset || directory_offset - local_offset < 30) {
            error = "zip: entry data is out of range";
            return false;
        }
        names.push_back(entry.name);
        entries.push_back(std::move(entry));
    }
    if (pos != directory_end) {
        error = "zip: central directory size mismatch";
        return false;
    }
    std::string clash;
    if (NamesConflict(names, clash)) {
        error = "zip: duplicate or conflicting entry name";
        return false;
    }
    archive_ = std::move(archive);
    entries_ = std::move(entries);
    central_directory_offset_ = directory_offset;
    return true;
}

const ZipEntry* ZipReader::Find(const std::string& name) const {
    for (const ZipEntry& entry : entries_)
        if (entry.name == name) return &entry;
    return nullptr;
}

bool ZipReader::Extract(const ZipEntry& entry, std::vector<std::uint8_t>& out, std::string& error) const {
    out.clear();
    const std::uint64_t offset = entry.local_header_offset;
    if (offset + 30 > central_directory_offset_) {
        error = "zip: entry data is out of range";
        return false;
    }
    const std::uint8_t* l = archive_.data() + offset;
    if (Get32(l) != kLocalSignature) {
        error = "zip: damaged local header";
        return false;
    }
    const std::uint16_t method = Get16(l + 8);
    const std::uint16_t name_length = Get16(l + 26), extra_length = Get16(l + 28);
    const std::uint64_t data_start = offset + 30 + name_length + extra_length;
    if (data_start > central_directory_offset_ || entry.compressed_size > central_directory_offset_ - data_start) {
        error = "zip: entry data is out of range";
        return false;
    }
    if (method != entry.method || name_length != entry.name.size() ||
        entry.name.compare(0, entry.name.size(), reinterpret_cast<const char*>(l + 30), name_length) != 0) {
        error = "zip: local header disagrees with the central directory";
        return false;
    }
    const std::uint8_t* data = archive_.data() + data_start;
    const std::size_t compressed = static_cast<std::size_t>(entry.compressed_size);
    const std::size_t expected = static_cast<std::size_t>(entry.uncompressed_size);
    if (entry.method == 0) {
        out.assign(data, data + compressed);
    } else {
        out.reserve(expected);
        std::string inflate_error;
        if (!InflateRaw(data, compressed, out, expected, inflate_error)) {
            error = "zip: " + inflate_error;
            out.clear();
            return false;
        }
    }
    if (out.size() != expected) {
        error = "zip: entry size mismatch";
        out.clear();
        return false;
    }
    if (Crc32(out.data(), out.size()) != entry.crc32) {
        error = "zip: CRC-32 mismatch";
        out.clear();
        return false;
    }
    return true;
}

bool ZipWriter::AddFile(const std::string& name, const void* data, std::size_t size, std::string& error) {
    if (!name.empty() && name.back() == '/') {
        error = "zip: file names must not end with '/'";
        return false;
    }
    return Add(name, data, size, error);
}

bool ZipWriter::AddDirectory(const std::string& name, std::string& error) {
    if (name.empty() || name.back() != '/') {
        error = "zip: folder names must end with '/'";
        return false;
    }
    return Add(name, nullptr, 0, error);
}

bool ZipWriter::Add(const std::string& name, const void* data, std::size_t size, std::string& error) {
    if (finished_) {
        error = "zip: writer is already finished";
        return false;
    }
    if (!IsSafeZipPath(name)) {
        error = "zip: unsafe entry name";
        return false;
    }
    std::vector<std::string> names = names_;
    names.push_back(name);
    std::string clash;
    if (NamesConflict(names, clash)) {
        error = "zip: duplicate or conflicting entry name";
        return false;
    }
    if (count_ >= 0xFFFF) {
        error = "zip: too many entries";
        return false;
    }
    const std::uint64_t end = static_cast<std::uint64_t>(body_.size()) + 30 + name.size() + size;
    if (size >= 0xFFFFFFFFu || end >= 0xFFFFFFFFu) {
        error = "zip: archive would need zip64";
        return false;
    }
    const std::uint32_t crc = Crc32(data, size);
    const std::uint32_t offset = static_cast<std::uint32_t>(body_.size());
    const bool folder = name.back() == '/';
    Put32(body_, kLocalSignature);
    Put16(body_, 10);  // version needed: 1.0, stored
    Put16(body_, 0);   // flags
    Put16(body_, 0);   // method: stored
    Put16(body_, dos_time_);
    Put16(body_, dos_date_);
    Put32(body_, crc);
    Put32(body_, static_cast<std::uint32_t>(size));
    Put32(body_, static_cast<std::uint32_t>(size));
    Put16(body_, static_cast<std::uint32_t>(name.size()));
    Put16(body_, 0);  // extra length
    body_.insert(body_.end(), name.begin(), name.end());
    if (size) body_.insert(body_.end(), static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + size);

    Put32(directory_, kCentralSignature);
    Put16(directory_, 20);  // made by: MS-DOS, spec 2.0
    Put16(directory_, 10);
    Put16(directory_, 0);
    Put16(directory_, 0);
    Put16(directory_, dos_time_);
    Put16(directory_, dos_date_);
    Put32(directory_, crc);
    Put32(directory_, static_cast<std::uint32_t>(size));
    Put32(directory_, static_cast<std::uint32_t>(size));
    Put16(directory_, static_cast<std::uint32_t>(name.size()));
    Put16(directory_, 0);  // extra
    Put16(directory_, 0);  // comment
    Put16(directory_, 0);  // disk
    Put16(directory_, 0);  // internal attributes
    Put32(directory_, folder ? 0x10u : 0u);  // MS-DOS folder attribute
    Put32(directory_, offset);
    directory_.insert(directory_.end(), name.begin(), name.end());
    names_.push_back(name);
    ++count_;
    return true;
}

bool ZipWriter::Finish(std::vector<std::uint8_t>& out, std::string& error) {
    if (finished_) {
        error = "zip: writer is already finished";
        return false;
    }
    const std::uint64_t total = static_cast<std::uint64_t>(body_.size()) + directory_.size() + 22;
    if (total >= 0xFFFFFFFFu) {
        error = "zip: archive would need zip64";
        return false;
    }
    out = std::move(body_);
    const std::uint32_t directory_offset = static_cast<std::uint32_t>(out.size());
    out.insert(out.end(), directory_.begin(), directory_.end());
    Put32(out, kEndSignature);
    Put16(out, 0);
    Put16(out, 0);
    Put16(out, static_cast<std::uint32_t>(count_));
    Put16(out, static_cast<std::uint32_t>(count_));
    Put32(out, static_cast<std::uint32_t>(directory_.size()));
    Put32(out, directory_offset);
    Put16(out, 0);  // comment length
    body_.clear();
    directory_.clear();
    finished_ = true;
    return true;
}

void ToDosDateTime(int year, int month, int day, int hour, int minute, int second, std::uint16_t& dos_time,
                   std::uint16_t& dos_date) {
    auto clamp = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
    year = clamp(year, 1980, 2107);
    month = clamp(month, 1, 12);
    day = clamp(day, 1, 31);
    hour = clamp(hour, 0, 23);
    minute = clamp(minute, 0, 59);
    second = clamp(second, 0, 59);
    dos_date = static_cast<std::uint16_t>((year - 1980) << 9 | month << 5 | day);
    dos_time = static_cast<std::uint16_t>(hour << 11 | minute << 5 | second / 2);
}

}  // namespace ddt
