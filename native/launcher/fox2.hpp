// Project-authored. The one FOX2 transform the runtime archive needs: rebuild
// the string table of a compiled FOX2 (entity data kept byte for byte) in
// first-use order instead of the game's alphabetical order.
//
// FOX2 layout used here (all little-endian):
//   header   u32 magic 0x786F62F2, u32 version 0x35, u32 entity count,
//            u32 string-table offset, u32 first-entity offset, zero padding
//   entity   u16 header size, ..., "ent\0" at +6, u64 class-name hash at +0x1C,
//            u16 static and u16 dynamic property counts at +0x24,
//            u32 property offset at +0x28, u32 entity size at +0x30
//   property u64 name hash, u8 data type, u8 container type, u16 element
//            count, u16 value offset, u16 property size
//   strings  { u64 hash, u32 length, bytes } ... u64 0, zero pad to 16,
//            "\0\0end", zero pad to 16
//
// First-use order: for each entity its class name; then for each property its
// name followed by its string values (String, Path and FilePtr types) and, for
// string maps, each key followed by its value. Duplicates are dropped and only
// strings whose text is known are written: the original table plus the empty
// string, which the game's table leaves out.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ddt {

constexpr std::uint64_t kFoxEmptyStringHash = 0xB8A0BF169F98ull;

struct Fox2Strings {
    std::size_t table_offset = 0;
    std::vector<std::pair<std::uint64_t, std::string>> table;  // as stored
    std::vector<std::uint64_t> first_use;                        // every referenced hash, in first-use order
};

bool ParseFox2Strings(const std::uint8_t* data, std::size_t size, Fox2Strings& out, std::string& error);
bool RewriteFox2StringTable(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                            std::string& error);

}  // namespace ddt
