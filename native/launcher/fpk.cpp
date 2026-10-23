// Project-authored. See fpk.hpp.
#include "launcher/fpk.hpp"

#include <cstring>

#include "launcher/hash.hpp"

namespace ddt {
namespace {

constexpr char kMagicFpk[10] = {'f', 'o', 'x', 'f', 'p', 'k', '\0', 'w', 'i', 'n'};
constexpr char kMagicFpkd[10] = {'f', 'o', 'x', 'f', 'p', 'k', 'd', 'w', 'i', 'n'};
constexpr std::size_t kHeaderSize = 48;
constexpr std::size_t kEntryRecordSize = 48;
constexpr std::size_t kReferenceRecordSize = 16;
constexpr std::uint32_t kMaxRecords = 1u << 16;
constexpr std::size_t kMaxPathLength = 1024;

std::uint32_t Get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint64_t Get64(const std::uint8_t* p) { return static_cast<std::uint64_t>(Get32(p + 4)) << 32 | Get32(p); }
void Put32(std::vector<std::uint8_t>& out, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out[at + i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void Put64(std::vector<std::uint8_t>& out, std::size_t at, std::uint64_t v) {
    Put32(out, at, static_cast<std::uint32_t>(v));
    Put32(out, at + 4, static_cast<std::uint32_t>(v >> 32));
}
std::size_t Pad16(std::size_t v) { return (v + 15) / 16 * 16; }

// A path string at [offset, offset+length) followed by a NUL, printable ASCII.
bool ReadString(const std::uint8_t* data, std::size_t size, std::uint64_t offset, std::uint64_t length,
                std::string& out) {
    if (length == 0 || length > kMaxPathLength || offset > size || length >= size - offset) return false;
    if (data[offset + length] != 0) return false;
    for (std::uint64_t i = 0; i < length; ++i) {
        const std::uint8_t c = data[offset + i];
        if (c < 0x20 || c > 0x7E) return false;
    }
    out.assign(reinterpret_cast<const char*>(data + offset), static_cast<std::size_t>(length));
    return true;
}

bool ValidPath(const std::string& path) {
    if (path.empty() || path.size() > kMaxPathLength) return false;
    for (const char ch : path) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

}  // namespace

bool ParseFpk(const std::uint8_t* data, std::size_t size, FpkFile& out, std::string& error) {
    out = FpkFile{};
    if (size < kHeaderSize) {
        error = "FPK is too small";
        return false;
    }
    if (std::memcmp(data, kMagicFpkd, 10) == 0) {
        out.is_fpkd = true;
    } else if (std::memcmp(data, kMagicFpk, 10) != 0) {
        error = "not an FPK/FPKD package";
        return false;
    }
    if (Get32(data + 10) != size) {
        error = "FPK size field does not match the data";
        return false;
    }
    for (std::size_t i = 14; i < 32; ++i)
        if (data[i] != 0) {
            error = "FPK header has unexpected data";
            return false;
        }
    const std::uint32_t version = Get32(data + 32), entry_count = Get32(data + 36);
    const std::uint32_t reference_count = Get32(data + 40), tail = Get32(data + 44);
    if (version != 2 || tail != 0) {
        error = "unsupported FPK header";
        return false;
    }
    if (entry_count > kMaxRecords || reference_count > kMaxRecords ||
        kHeaderSize + kEntryRecordSize * entry_count + kReferenceRecordSize * reference_count > size) {
        error = "FPK record tables are out of range";
        return false;
    }
    std::size_t pos = kHeaderSize;
    out.entries.resize(entry_count);
    for (FpkEntry& entry : out.entries) {
        const std::uint8_t* r = data + pos;
        const std::uint64_t data_offset = Get64(r), data_size = Get64(r + 8);
        const std::uint64_t string_offset = Get64(r + 16), string_length = Get64(r + 24);
        if (!ReadString(data, size, string_offset, string_length, entry.path)) {
            error = "FPK entry path is invalid";
            return false;
        }
        if (data_offset > size || data_size > size - data_offset) {
            error = "FPK entry data is out of range";
            return false;
        }
        std::memcpy(entry.path_md5.data(), r + 32, 16);
        entry.data.assign(data + data_offset, data + data_offset + data_size);
        pos += kEntryRecordSize;
    }
    out.references.resize(reference_count);
    for (std::string& reference : out.references) {
        const std::uint8_t* r = data + pos;
        if (!ReadString(data, size, Get64(r), Get64(r + 8), reference)) {
            error = "FPK reference path is invalid";
            return false;
        }
        pos += kReferenceRecordSize;
    }
    return true;
}

bool WriteFpk(const FpkFile& in, std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    if (in.entries.size() > kMaxRecords || in.references.size() > kMaxRecords) {
        error = "too many FPK records";
        return false;
    }
    for (const FpkEntry& entry : in.entries)
        if (!ValidPath(entry.path)) {
            error = "FPK entry path is invalid";
            return false;
        }
    for (const std::string& reference : in.references)
        if (!ValidPath(reference)) {
            error = "FPK reference path is invalid";
            return false;
        }
    const std::size_t strings_start =
        kHeaderSize + kEntryRecordSize * in.entries.size() + kReferenceRecordSize * in.references.size();
    std::size_t pos = strings_start;
    std::vector<std::size_t> entry_strings, reference_strings, data_offsets;
    for (const FpkEntry& entry : in.entries) {
        entry_strings.push_back(pos);
        pos += entry.path.size() + 1;
    }
    for (const std::string& reference : in.references) {
        reference_strings.push_back(pos);
        pos += reference.size() + 1;
    }
    pos = Pad16(pos);
    for (const FpkEntry& entry : in.entries) {
        data_offsets.push_back(pos);
        pos = Pad16(pos + entry.data.size());
    }
    if (pos > 0xFFFFFFFFu) {
        error = "FPK would be too large";
        return false;
    }
    out.assign(pos, 0);
    std::memcpy(out.data(), in.is_fpkd ? kMagicFpkd : kMagicFpk, 10);
    Put32(out, 10, static_cast<std::uint32_t>(pos));
    Put32(out, 32, 2);
    Put32(out, 36, static_cast<std::uint32_t>(in.entries.size()));
    Put32(out, 40, static_cast<std::uint32_t>(in.references.size()));
    std::size_t record = kHeaderSize;
    for (std::size_t i = 0; i < in.entries.size(); ++i) {
        const FpkEntry& entry = in.entries[i];
        Put64(out, record, data_offsets[i]);
        Put64(out, record + 8, entry.data.size());
        Put64(out, record + 16, entry_strings[i]);
        Put64(out, record + 24, entry.path.size());
        std::memcpy(out.data() + record + 32, entry.path_md5.data(), 16);
        std::memcpy(out.data() + entry_strings[i], entry.path.data(), entry.path.size());
        if (!entry.data.empty()) std::memcpy(out.data() + data_offsets[i], entry.data.data(), entry.data.size());
        record += kEntryRecordSize;
    }
    for (std::size_t i = 0; i < in.references.size(); ++i) {
        Put64(out, record, reference_strings[i]);
        Put64(out, record + 8, in.references[i].size());
        std::memcpy(out.data() + reference_strings[i], in.references[i].data(), in.references[i].size());
        record += kReferenceRecordSize;
    }
    return true;
}

bool SetFpkEntryPath(FpkEntry& entry, const std::string& path) {
    Md5Digest digest{};
    if (!ValidPath(path) || !Md5(path.data(), path.size(), digest)) return false;
    entry.path = path;
    entry.path_md5 = digest;
    return true;
}

}  // namespace ddt
