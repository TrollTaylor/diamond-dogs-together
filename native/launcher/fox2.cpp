// Project-authored. See fox2.hpp.
#include "launcher/fox2.hpp"

#include <cstring>
#include <map>
#include <set>

namespace ddt {
namespace {

constexpr std::uint32_t kMagic = 0x786F62F2;
constexpr std::uint32_t kVersion = 0x35;
constexpr std::size_t kMaxEntities = 1u << 16;
constexpr std::size_t kMaxStrings = 1u << 16;
constexpr std::size_t kMinEntityHeader = 0x34;
constexpr std::size_t kPropertyHeader = 16;
constexpr char kEndMarker[5] = {'\0', '\0', 'e', 'n', 'd'};

// FOX2 data types that hold string hashes: String, Path, FilePtr.
bool IsStringType(std::uint8_t type) { return type == 11 || type == 12 || type == 20; }
// Types whose elements are 8 bytes, so string-map entries are a 16-byte (key, value) pair:
// int64, uint64, double, String, Path, EntityPtr, FilePtr, EntityHandle.
bool IsEightByteType(std::uint8_t type) {
    return type == 6 || type == 7 || type == 9 || type == 11 || type == 12 || type == 13 || type == 20 || type == 21;
}
// EntityLink (22) and PropertyInfo (23) may carry strings in a layout this
// code does not decode; anything above WideVector3 (24) is unknown.
bool IsSupportedType(std::uint8_t type) { return type <= 21 || type == 24; }

std::uint16_t Get16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
std::uint32_t Get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint64_t Get64(const std::uint8_t* p) { return static_cast<std::uint64_t>(Get32(p + 4)) << 32 | Get32(p); }
void Append32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}
void Append64(std::vector<std::uint8_t>& out, std::uint64_t v) {
    Append32(out, static_cast<std::uint32_t>(v));
    Append32(out, static_cast<std::uint32_t>(v >> 32));
}
std::size_t Pad16(std::size_t v) { return (v + 15) / 16 * 16; }
bool AllZero(const std::uint8_t* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (p[i]) return false;
    return true;
}

class FirstUse {
public:
    explicit FirstUse(std::vector<std::uint64_t>& order) : order_(order) {}
    void Add(std::uint64_t hash) {
        if (seen_.insert(hash).second) order_.push_back(hash);
    }

private:
    std::vector<std::uint64_t>& order_;
    std::set<std::uint64_t> seen_;
};

bool WalkProperty(const std::uint8_t* p, std::size_t room, FirstUse& use, std::size_t& property_size,
                  std::string& error) {
    if (room < kPropertyHeader) {
        error = "FOX2 property is out of range";
        return false;
    }
    const std::uint64_t name = Get64(p);
    const std::uint8_t type = p[8], container = p[9];
    const std::uint16_t count = Get16(p + 10), value_offset = Get16(p + 12), size = Get16(p + 14);
    if (size < kPropertyHeader || value_offset < kPropertyHeader || value_offset > size || size > room) {
        error = "FOX2 property is out of range";
        return false;
    }
    use.Add(name);
    property_size = size;
    if (count == 0) return true;
    if (container > 3) {
        error = "FOX2 property uses an unsupported container type";
        return false;
    }
    if (!IsSupportedType(type)) {
        error = "FOX2 property uses an unsupported data type";
        return false;
    }
    const std::uint8_t* values = p + value_offset;
    const std::size_t value_bytes = size - value_offset;
    if (container == 2) {  // string map: (u64 key hash, value) pairs
        if (!IsEightByteType(type)) {
            error = "FOX2 string map uses an unsupported value type";
            return false;
        }
        if (static_cast<std::size_t>(count) * 16 > value_bytes) {
            error = "FOX2 string map is out of range";
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) {
            use.Add(Get64(values + 16 * i));
            if (IsStringType(type)) use.Add(Get64(values + 16 * i + 8));
        }
        return true;
    }
    if (IsStringType(type)) {
        if (static_cast<std::size_t>(count) * 8 > value_bytes) {
            error = "FOX2 string values are out of range";
            return false;
        }
        for (std::size_t i = 0; i < count; ++i) use.Add(Get64(values + 8 * i));
    }
    return true;
}

}  // namespace

bool ParseFox2Strings(const std::uint8_t* data, std::size_t size, Fox2Strings& out, std::string& error) {
    out = Fox2Strings{};
    if (size < 0x20 || Get32(data) != kMagic || Get32(data + 4) != kVersion) {
        error = "not a supported FOX2 file";
        return false;
    }
    const std::uint32_t entity_count = Get32(data + 8);
    const std::uint32_t table = Get32(data + 12), first_entity = Get32(data + 16);
    if (entity_count > kMaxEntities || table > size || first_entity < 0x14 || first_entity > table) {
        error = "FOX2 header is out of range";
        return false;
    }
    FirstUse use(out.first_use);
    std::size_t pos = first_entity;
    for (std::uint32_t e = 0; e < entity_count; ++e) {
        if (table - pos < kMinEntityHeader) {
            error = "FOX2 entity is out of range";
            return false;
        }
        const std::uint8_t* entity = data + pos;
        const std::uint16_t header_size = Get16(entity);
        const std::uint16_t static_count = Get16(entity + 0x24), dynamic_count = Get16(entity + 0x26);
        const std::uint32_t property_offset = Get32(entity + 0x28), entity_size = Get32(entity + 0x30);
        if (std::memcmp(entity + 6, "ent", 4) != 0 || header_size < kMinEntityHeader || entity_size < header_size ||
            entity_size > table - pos || property_offset < header_size || property_offset > entity_size) {
            error = "FOX2 entity header is invalid";
            return false;
        }
        use.Add(Get64(entity + 0x1C));
        std::size_t at = property_offset;
        for (unsigned k = 0; k < static_cast<unsigned>(static_count) + dynamic_count; ++k) {
            std::size_t property_size = 0;
            if (!WalkProperty(entity + at, entity_size - at, use, property_size, error)) return false;
            at += property_size;
        }
        pos += entity_size;
    }
    std::size_t p = table;
    for (;;) {
        if (size - p < 8) {
            error = "FOX2 string table is truncated";
            return false;
        }
        const std::uint64_t hash = Get64(data + p);
        p += 8;
        if (hash == 0) break;
        if (size - p < 4) {
            error = "FOX2 string table is truncated";
            return false;
        }
        const std::uint32_t length = Get32(data + p);
        p += 4;
        if (length > size - p || out.table.size() >= kMaxStrings) {
            error = "FOX2 string table is out of range";
            return false;
        }
        out.table.emplace_back(hash, std::string(reinterpret_cast<const char*>(data + p), length));
        p += length;
    }
    const std::size_t marker = Pad16(p);
    if (marker > size || !AllZero(data + p, marker - p) || size - marker < sizeof(kEndMarker) ||
        std::memcmp(data + marker, kEndMarker, sizeof(kEndMarker)) != 0 ||
        Pad16(marker + sizeof(kEndMarker)) != size ||
        !AllZero(data + marker + sizeof(kEndMarker), size - marker - sizeof(kEndMarker))) {
        error = "FOX2 file does not end with the expected end marker";
        return false;
    }
    out.table_offset = table;
    return true;
}

bool RewriteFox2StringTable(const std::uint8_t* data, std::size_t size, std::vector<std::uint8_t>& out,
                            std::string& error) {
    out.clear();
    Fox2Strings strings;
    if (!ParseFox2Strings(data, size, strings, error)) return false;
    std::map<std::uint64_t, const std::string*> known;
    for (const auto& entry : strings.table)
        if (!known.emplace(entry.first, &entry.second).second) {
            error = "FOX2 string table has a duplicate hash";
            return false;
        }
    static const std::string empty;
    known.emplace(kFoxEmptyStringHash, &empty);
    out.assign(data, data + strings.table_offset);
    std::set<std::uint64_t> written;
    for (const std::uint64_t hash : strings.first_use) {
        const auto it = known.find(hash);
        if (it == known.end()) continue;  // a hash with no text (e.g. a path code) has no table entry
        Append64(out, hash);
        Append32(out, static_cast<std::uint32_t>(it->second->size()));
        out.insert(out.end(), it->second->begin(), it->second->end());
        written.insert(hash);
    }
    for (const auto& entry : strings.table)
        if (!written.count(entry.first)) {
            error = "FOX2 string \"" + entry.second + "\" is not referenced by any entity";
            out.clear();
            return false;
        }
    Append64(out, 0);
    out.resize(Pad16(out.size()), 0);
    out.insert(out.end(), kEndMarker, kEndMarker + sizeof(kEndMarker));
    out.resize(Pad16(out.size()), 0);
    return true;
}

}  // namespace ddt
