// Project-authored. See crc32.hpp.
#include "launcher/crc32.hpp"

#include <array>

namespace ddt {
namespace {

constexpr std::array<std::uint32_t, 256> MakeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < 256; ++n) {
        std::uint32_t value = n;
        for (int bit = 0; bit < 8; ++bit) value = (value & 1u) ? (value >> 1) ^ 0xEDB88320u : value >> 1;
        table[n] = value;
    }
    return table;
}

constexpr std::array<std::uint32_t, 256> kCrcTable = MakeCrcTable();

}  // namespace

std::uint32_t Crc32(const void* data, std::size_t size, std::uint32_t crc) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t value = ~crc;
    for (std::size_t i = 0; i < size; ++i) value = kCrcTable[(value ^ bytes[i]) & 0xFFu] ^ (value >> 8);
    return ~value;
}

std::uint32_t Adler32(const void* data, std::size_t size, std::uint32_t adler) {
    constexpr std::uint32_t kModulus = 65521;
    // 5552 is the longest run for which the 32-bit sums cannot overflow before reduction.
    constexpr std::size_t kRun = 5552;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t a = adler & 0xFFFFu, b = adler >> 16;
    while (size > 0) {
        const std::size_t run = size < kRun ? size : kRun;
        for (std::size_t i = 0; i < run; ++i) {
            a += bytes[i];
            b += a;
        }
        a %= kModulus;
        b %= kModulus;
        bytes += run;
        size -= run;
    }
    return b << 16 | a;
}

}  // namespace ddt
