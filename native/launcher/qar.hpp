// Project-authored reader and writer for Fox Engine QAR ("SQAR") archives,
// version 1, written from the layout recorded in docs/runtime-archive-builder.md.
//
// Reading: header and section table are XOR-masked; each entry has a 32-byte
// masked header (path hash, stored and plain sizes, MD5 of the plain bytes)
// followed by data scrambled with the per-entry "decrypt1" stream and, when
// the sizes differ, zlib-compressed. The second encryption layer ("decrypt2")
// is refused. Lookup goes through the section table, so a multi-gigabyte
// game archive is never read in full.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ddt {

constexpr std::uint32_t kQarRuntimeFlags = 0x3011E0;

struct QarEntryInfo {
    std::uint64_t hash = 0;
    std::uint64_t offset = 0;  // of the 32-byte entry header
    std::uint32_t stored_size = 0;
    std::uint32_t plain_size = 0;
    std::array<std::uint8_t, 16> md5{};  // of the plain bytes
};

// XOR scramble used for entry data (its own inverse). `hash_low` is the low
// 32 bits of the entry's path hash; `position` is the offset of `data[0]`
// from the start of the entry data.
void QarScramble(std::uint8_t* data, std::size_t size, std::uint32_t hash_low, std::uint64_t position = 0);

class QarReader {
public:
    QarReader();
    ~QarReader();
    QarReader(const QarReader&) = delete;
    QarReader& operator=(const QarReader&) = delete;

    bool OpenFile(const std::wstring& path, std::string& error);
    bool OpenMemory(std::vector<std::uint8_t> bytes, std::string& error);

    std::uint32_t flags() const { return flags_; }
    std::uint32_t version() const { return version_; }
    std::size_t entry_count() const { return sections_.size(); }

    enum class Lookup { found, not_found, failed };
    // Finds an entry by its 64-bit path hash. Only entries whose section key
    // matches are read.
    Lookup Find(std::uint64_t hash, QarEntryInfo& info, std::string& error) const;
    // Reads the header of the entry in section slot `index`.
    bool EntryAt(std::size_t index, QarEntryInfo& info, std::string& error) const;
    // Unscrambles, inflates when compressed, and checks size and MD5.
    bool Read(const QarEntryInfo& info, std::vector<std::uint8_t>& plain, std::uint32_t max_plain_size,
              std::string& error) const;

private:
    struct Source;
    bool Load(std::string& error);
    std::unique_ptr<Source> source_;
    std::vector<std::uint64_t> sections_;
    std::uint32_t flags_ = 0;
    std::uint32_t version_ = 0;
};

struct QarWriteEntry {
    std::uint64_t hash = 0;
    std::vector<std::uint8_t> plain;  // the entry's plain bytes
    std::vector<std::uint8_t> zlib;   // optional zlib stream of `plain`; empty = stored as is
};

// Writes entries in the given order at 1024-byte-aligned positions, with
// zero padding, MD5 of each plain entry, and the given header flags.
bool WriteQar(const std::vector<QarWriteEntry>& entries, std::uint32_t flags, std::vector<std::uint8_t>& out,
              std::string& error);

}  // namespace ddt
