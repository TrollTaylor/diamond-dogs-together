// Project-authored reader and writer for Fox Engine FPK / FPKD packages,
// written from the layout recorded in docs/runtime-archive-builder.md:
// a 48-byte header, 48-byte entry records, 16-byte reference records, the
// null-terminated path strings (entries first, then references) padded to
// 16 bytes, then each entry's data padded to 16 bytes. Entry data is kept as
// raw bytes (encrypted Lua inside mission packages is never touched).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ddt {

struct FpkEntry {
    std::string path;
    std::array<std::uint8_t, 16> path_md5{};  // MD5 of `path`
    std::vector<std::uint8_t> data;
};

struct FpkFile {
    bool is_fpkd = false;  // magic "foxfpkdwin" (true) or "foxfpk\0win" (false)
    std::vector<FpkEntry> entries;
    std::vector<std::string> references;
};

bool ParseFpk(const std::uint8_t* data, std::size_t size, FpkFile& out, std::string& error);
bool WriteFpk(const FpkFile& in, std::vector<std::uint8_t>& out, std::string& error);
// Sets `entry.path` and recomputes `entry.path_md5`.
bool SetFpkEntryPath(FpkEntry& entry, const std::string& path);

}  // namespace ddt
