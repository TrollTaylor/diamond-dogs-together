// Project-authored. See qar.hpp and docs/runtime-archive-builder.md for the layout.
#include "launcher/qar.hpp"

#include <algorithm>
#include <cstring>
#include <set>

#include "launcher/file_io.hpp"
#include "launcher/hash.hpp"
#include "launcher/inflate.hpp"

namespace ddt {
namespace {

constexpr std::uint32_t kMagic = 0x52415153;  // "SQAR"
constexpr std::uint32_t kHeaderMask[7] = {0x41441043, 0x11C22050, 0xD05608C3, 0x532C7319,
                                          0x41441043, 0x41441043, 0x11C22050};
constexpr std::uint32_t kSectionMask[4] = {0x41441043, 0x11C22050, 0xD05608C3, 0x532C7319};
constexpr std::uint32_t kHashMask = 0x41441043;
constexpr std::uint32_t kStoredSizeMask = 0x11C22050;
constexpr std::uint32_t kPlainSizeMask = 0xD05608C3;
constexpr std::uint32_t kMd5Mask[4] = {0x532C7319, 0x41441043, 0x41441043, 0x11C22050};
constexpr std::uint32_t kScrambleTable[8] = {0xBB8ADEDB, 0x65229958, 0x08453206, 0x88121302,
                                             0x4C344955, 0x2C02F10C, 0x4887F823, 0xF3818583};
constexpr std::uint32_t kSecondLayerMagic[2] = {0xA0F8EFE6, 0xE3F8EFE6};
constexpr std::uint32_t kLayoutFlag16 = 0x800;  // set: a different (unsupported) alignment
constexpr std::size_t kMaxEntries = 1u << 22;
constexpr std::uint64_t kAlignment = 1024;
constexpr std::uint64_t kKeyMask = 0xFFFFFFFFFFull;  // low 40 bits of a section value

std::uint32_t Get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
void Put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16);
    p[3] = static_cast<std::uint8_t>(v >> 24);
}

// The section table masks each u32 by its position in the table.
std::uint32_t LowMask(std::uint64_t i) { return kSectionMask[(i + (8 * i) / 5) % 4]; }
std::uint32_t HighMask(std::uint64_t i) { return kSectionMask[(i + (8 * i + 4) / 5) % 4]; }

// Bits 0..39 of a section value: the hash's low byte above its high u32.
std::uint64_t SectionKey(std::uint64_t hash) { return (hash & 0xFFu) << 32 | hash >> 32; }

std::uint64_t AlignUp(std::uint64_t v) { return (v + kAlignment - 1) / kAlignment * kAlignment; }

std::string Hex64(std::uint64_t v) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out = "0x";
    for (int shift = 60; shift >= 0; shift -= 4) out.push_back(digits[(v >> shift) & 15]);
    return out;
}

}  // namespace

void QarScramble(std::uint8_t* data, std::size_t size, std::uint32_t hash_low, std::uint64_t position) {
    std::size_t k = 0;
    while (k < size) {
        const std::uint64_t p = position + k;
        const std::uint64_t block = p / 8;
        const std::uint64_t index = 2 * ((hash_low + (8 * block) / 11) % 4);
        std::uint8_t key[8];
        Put32(key, kScrambleTable[index]);
        Put32(key + 4, kScrambleTable[index + 1]);
        const std::size_t within = static_cast<std::size_t>(p % 8);
        const std::size_t n = std::min<std::size_t>(8 - within, size - k);
        for (std::size_t j = 0; j < n; ++j) data[k + j] ^= key[within + j];
        k += n;
    }
}

struct QarReader::Source {
    ReadOnlyFile file;
    std::vector<std::uint8_t> memory;
    bool in_memory = false;
    std::uint64_t size() const { return in_memory ? memory.size() : file.size(); }
    bool ReadAt(std::uint64_t offset, void* buffer, std::size_t count, std::string& error) const {
        if (!in_memory) return file.ReadAt(offset, buffer, count, error);
        if (offset > memory.size() || count > memory.size() - offset) {
            error = "read past the end of the archive";
            return false;
        }
        if (count) std::memcpy(buffer, memory.data() + offset, count);
        return true;
    }
};

QarReader::QarReader() = default;
QarReader::~QarReader() = default;

bool QarReader::OpenFile(const std::wstring& path, std::string& error) {
    source_ = std::make_unique<Source>();
    if (!source_->file.Open(path, error)) {
        source_.reset();
        return false;
    }
    return Load(error);
}

bool QarReader::OpenMemory(std::vector<std::uint8_t> bytes, std::string& error) {
    source_ = std::make_unique<Source>();
    source_->memory = std::move(bytes);
    source_->in_memory = true;
    return Load(error);
}

bool QarReader::Load(std::string& error) {
    sections_.clear();
    const std::uint64_t size = source_->size();
    std::uint8_t header[32];
    if (size < sizeof(header) || !source_->ReadAt(0, header, sizeof(header), error) || Get32(header) != kMagic) {
        error = "not a QAR archive";
        return false;
    }
    std::uint32_t field[7];
    for (int i = 0; i < 7; ++i) field[i] = Get32(header + 4 + 4 * i) ^ kHeaderMask[i];
    flags_ = field[0];
    version_ = field[5];
    const std::uint32_t count = field[1];
    if (version_ != 1) {
        error = "unsupported QAR version " + std::to_string(version_);
        return false;
    }
    if (flags_ & kLayoutFlag16) {
        error = "unsupported QAR layout (flags " + Hex64(flags_) + ")";
        return false;
    }
    if (count > kMaxEntries || 32 + 8ull * count > size) {
        error = "QAR section table is out of range";
        return false;
    }
    std::vector<std::uint8_t> table(8ull * count);
    if (count && !source_->ReadAt(32, table.data(), table.size(), error)) return false;
    sections_.resize(count);
    for (std::uint64_t i = 0; i < count; ++i) {
        const std::uint32_t low = Get32(table.data() + 8 * i) ^ LowMask(i);
        const std::uint32_t high = Get32(table.data() + 8 * i + 4) ^ HighMask(i);
        sections_[i] = static_cast<std::uint64_t>(high) << 32 | low;
    }
    return true;
}

bool QarReader::EntryAt(std::size_t index, QarEntryInfo& info, std::string& error) const {
    if (!source_ || index >= sections_.size()) {
        error = "QAR entry index out of range";
        return false;
    }
    const std::uint64_t offset = (sections_[index] >> 40) << 10;
    const std::uint64_t size = source_->size();
    std::uint8_t header[32];
    if (offset > size || size - offset < sizeof(header)) {
        error = "QAR entry header is out of range";
        return false;
    }
    if (!source_->ReadAt(offset, header, sizeof(header), error)) return false;
    info.offset = offset;
    info.hash = static_cast<std::uint64_t>(Get32(header + 4) ^ kHashMask) << 32 | (Get32(header) ^ kHashMask);
    info.stored_size = Get32(header + 8) ^ kStoredSizeMask;
    info.plain_size = Get32(header + 12) ^ kPlainSizeMask;
    for (int i = 0; i < 4; ++i) Put32(info.md5.data() + 4 * i, Get32(header + 16 + 4 * i) ^ kMd5Mask[i]);
    if (size - offset - sizeof(header) < info.stored_size) {
        error = "QAR entry data is out of range";
        return false;
    }
    return true;
}

QarReader::Lookup QarReader::Find(std::uint64_t hash, QarEntryInfo& info, std::string& error) const {
    const std::uint64_t key = SectionKey(hash);
    for (std::size_t i = 0; i < sections_.size(); ++i) {
        if ((sections_[i] & kKeyMask) != key) continue;
        QarEntryInfo candidate;
        if (!EntryAt(i, candidate, error)) return Lookup::failed;
        if (candidate.hash == hash) {
            info = candidate;
            return Lookup::found;
        }
    }
    return Lookup::not_found;
}

bool QarReader::Read(const QarEntryInfo& info, std::vector<std::uint8_t>& plain, std::uint32_t max_plain_size,
                     std::string& error) const {
    plain.clear();
    if (!source_) {
        error = "QAR archive is not open";
        return false;
    }
    if (info.plain_size > max_plain_size) {
        error = "QAR entry is larger than allowed";
        return false;
    }
    const std::uint64_t stored_limit = static_cast<std::uint64_t>(max_plain_size) + (max_plain_size >> 3) + 1024;
    if (info.stored_size > stored_limit) {
        error = "QAR entry is larger than allowed";
        return false;
    }
    std::vector<std::uint8_t> stored(info.stored_size);
    if (!stored.empty() && !source_->ReadAt(info.offset + 32, stored.data(), stored.size(), error)) return false;
    QarScramble(stored.data(), stored.size(), static_cast<std::uint32_t>(info.hash), 0);
    if (stored.size() >= 4) {
        const std::uint32_t magic = Get32(stored.data());
        if (magic == kSecondLayerMagic[0] || magic == kSecondLayerMagic[1]) {
            error = "QAR entry uses the second encryption layer, which is not supported";
            return false;
        }
    }
    if (info.stored_size != info.plain_size) {
        plain.reserve(info.plain_size);
        if (!ZlibDecompress(stored.data(), stored.size(), plain, info.plain_size, error)) {
            plain.clear();
            return false;
        }
        if (plain.size() != info.plain_size) {
            error = "QAR entry inflated to the wrong size";
            plain.clear();
            return false;
        }
    } else {
        plain = std::move(stored);
    }
    Md5Digest digest{};
    if (!Md5(plain.data(), plain.size(), digest)) {
        error = "MD5 is not available";
        plain.clear();
        return false;
    }
    if (digest != info.md5) {
        error = "QAR entry MD5 mismatch";
        plain.clear();
        return false;
    }
    return true;
}

bool WriteQar(const std::vector<QarWriteEntry>& entries, std::uint32_t flags, std::vector<std::uint8_t>& out,
              std::string& error) {
    out.clear();
    if (entries.size() > kMaxEntries) {
        error = "too many QAR entries";
        return false;
    }
    if (flags & kLayoutFlag16) {
        error = "unsupported QAR layout flags";
        return false;
    }
    std::set<std::uint64_t> seen;
    for (const QarWriteEntry& entry : entries) {
        if (!seen.insert(entry.hash).second) {
            error = "duplicate QAR path hash " + Hex64(entry.hash);
            return false;
        }
        const std::size_t payload = entry.zlib.empty() ? entry.plain.size() : entry.zlib.size();
        if (entry.plain.size() > 0xFFFFFFFFu || payload > 0xFFFFFFFFu) {
            error = "QAR entry is too large";
            return false;
        }
    }
    const std::uint64_t data_start = AlignUp(32 + 8ull * entries.size());
    std::vector<std::uint64_t> positions;
    positions.reserve(entries.size());
    std::uint64_t pos = data_start;
    for (const QarWriteEntry& entry : entries) {
        positions.push_back(pos);
        pos = AlignUp(pos + 32 + (entry.zlib.empty() ? entry.plain.size() : entry.zlib.size()));
        if ((pos >> 10) >= (1ull << 24)) {
            error = "QAR archive would be too large";
            return false;
        }
    }
    const std::uint64_t total = pos;
    if (total > SIZE_MAX) {
        error = "QAR archive would be too large";
        return false;
    }
    out.assign(static_cast<std::size_t>(total), 0);
    std::uint8_t* base = out.data();
    Put32(base, kMagic);
    const std::uint32_t field[7] = {flags,
                                    static_cast<std::uint32_t>(entries.size()),
                                    0,  // second table (unused) entry count
                                    static_cast<std::uint32_t>(total >> 10),
                                    static_cast<std::uint32_t>(data_start),
                                    1,  // version
                                    0};
    for (int i = 0; i < 7; ++i) Put32(base + 4 + 4 * i, field[i] ^ kHeaderMask[i]);
    for (std::uint64_t i = 0; i < entries.size(); ++i) {
        const QarWriteEntry& entry = entries[static_cast<std::size_t>(i)];
        const std::uint64_t at = positions[static_cast<std::size_t>(i)];
        const std::uint64_t section = (at >> 10) << 40 | SectionKey(entry.hash);
        Put32(base + 32 + 8 * i, static_cast<std::uint32_t>(section) ^ LowMask(i));
        Put32(base + 32 + 8 * i + 4, static_cast<std::uint32_t>(section >> 32) ^ HighMask(i));

        Md5Digest digest{};
        if (!Md5(entry.plain.data(), entry.plain.size(), digest)) {
            error = "MD5 is not available";
            out.clear();
            return false;
        }
        const std::vector<std::uint8_t>& payload = entry.zlib.empty() ? entry.plain : entry.zlib;
        std::uint8_t* h = base + at;
        Put32(h, static_cast<std::uint32_t>(entry.hash) ^ kHashMask);
        Put32(h + 4, static_cast<std::uint32_t>(entry.hash >> 32) ^ kHashMask);
        Put32(h + 8, static_cast<std::uint32_t>(payload.size()) ^ kStoredSizeMask);
        Put32(h + 12, static_cast<std::uint32_t>(entry.plain.size()) ^ kPlainSizeMask);
        for (int k = 0; k < 4; ++k) Put32(h + 16 + 4 * k, Get32(digest.data() + 4 * k) ^ kMd5Mask[k]);
        if (!payload.empty()) std::memcpy(h + 32, payload.data(), payload.size());
        QarScramble(h + 32, payload.size(), static_cast<std::uint32_t>(entry.hash), 0);
    }
    return true;
}

}  // namespace ddt
