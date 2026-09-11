#include "mgscoop/import_table.hpp"
#include <cstring>
#include <limits>
namespace mgscoop {
namespace {
bool within(std::size_t offset, std::size_t count, std::size_t size) noexcept {
    return offset <= size && count <= size - offset;
}
template<class T> T read(const std::uint8_t* p) noexcept { T result; std::memcpy(&result, p, sizeof(T)); return result; }
bool name_equal(const std::uint8_t* image, std::size_t span, std::size_t offset,
                const char* expected, bool fold, bool& valid, ReadableRange readable, const void* context) noexcept {
    valid = false;
    if (offset >= span) return false;
    const auto limit = (span - offset < 4096) ? span - offset : 4096;
    const auto expected_length = std::strlen(expected);
    bool same = true;
    for (std::size_t i = 0; i < limit; ++i) {
        if (readable && !readable(context, offset + i, 1)) return false;
        unsigned char a = image[offset + i];
        if (!a) { valid = true; return same && i == expected_length; }
        if (i >= expected_length) { same = false; continue; }
        unsigned char b = static_cast<unsigned char>(expected[i]);
        if (fold) { if (a >= 'A' && a <= 'Z') a += 'a' - 'A'; if (b >= 'A' && b <= 'Z') b += 'a' - 'A'; }
        if (a != b) same = false;
    }
    return false;
}
}
ImportSlot find_import_slot(const std::uint8_t* image, std::size_t span, const char* dll, const char* symbol, ReadableRange readable, const void* context) noexcept {
    const ImportSlot invalid{ImportStatus::invalid_image, 0};
    if (!image || !dll || !symbol || !*dll || !*symbol) return {ImportStatus::invalid_argument, 0};
    const auto accessible = [&](std::size_t offset, std::size_t count) { return within(offset, count, span) && (!readable || readable(context, offset, count)); };
    if (!accessible(0, 64) || read<std::uint16_t>(image) != 0x5a4d) return invalid;
    const auto pe = static_cast<std::size_t>(read<std::uint32_t>(image + 0x3c));
    if (!accessible(pe, 24) || read<std::uint32_t>(image + pe) != 0x4550 || read<std::uint16_t>(image + pe + 4) != 0x8664) return invalid;
    const auto optional_size = read<std::uint16_t>(image + pe + 20);
    const auto optional = pe + 24;
    if (optional_size < 128 || !accessible(optional, optional_size) || read<std::uint16_t>(image + optional) != 0x20b) return invalid;
    const auto image_size = static_cast<std::size_t>(read<std::uint32_t>(image + optional + 56));
    if (image_size > span || image_size < optional + optional_size) return invalid;
    span = image_size;
    if (read<std::uint32_t>(image + optional + 108) < 2) return {ImportStatus::not_found, 0};
    const auto directory = static_cast<std::size_t>(read<std::uint32_t>(image + optional + 120));
    const auto directory_size = static_cast<std::size_t>(read<std::uint32_t>(image + optional + 124));
    if (!directory && !directory_size) return {ImportStatus::not_found, 0};
    if (!directory || directory_size < 20 || !accessible(directory, directory_size)) return invalid;
    ImportSlot found{ImportStatus::not_found, 0};
    bool terminated = false;
    for (std::size_t index = 0; index < directory_size / 20; ++index) {
        const auto d = directory + index * 20;
        const auto lookup = read<std::uint32_t>(image + d);
        const auto name = read<std::uint32_t>(image + d + 12);
        const auto address = read<std::uint32_t>(image + d + 16);
        bool empty = true;
        for (unsigned k = 0; k < 20; ++k) empty = empty && image[d + k] == 0;
        if (empty) { terminated = true; break; }
        bool valid = false;
        const bool matches = name_equal(image, span, name, dll, true, valid, readable, context);
        if (!valid) return invalid;
        if (!matches) continue;
        // Without an original lookup table, resolved addresses cannot be decoded as names.
        if (!lookup || !address || (address % 8) != 0) return invalid;
        bool thunk_end = false;
        for (std::size_t j = 0; j < 65536; ++j) {
            const auto l = std::size_t(lookup) + j * 8;
            const auto a = std::size_t(address) + j * 8;
            if (!accessible(l, 8) || !accessible(a, 8)) return invalid;
            const auto entry = read<std::uint64_t>(image + l);
            if (!entry) { thunk_end = true; break; }
            if (entry & (std::uint64_t(1) << 63)) continue;
            if (entry > std::numeric_limits<std::uint32_t>::max() || !accessible(static_cast<std::size_t>(entry), 3)) return invalid;
            const bool same = name_equal(image, span, static_cast<std::size_t>(entry) + 2, symbol, false, valid, readable, context);
            if (!valid) return invalid;
            if (same) {
                if (found.status == ImportStatus::found) return {ImportStatus::ambiguous, 0};
                found = {ImportStatus::found, a};
            }
        }
        if (!thunk_end) return invalid;
    }
    return terminated ? found : invalid;
}
}
