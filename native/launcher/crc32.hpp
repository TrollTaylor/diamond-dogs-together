// Project-authored. CRC-32 (ISO-HDLC / zip: reflected polynomial 0xEDB88320,
// initial value and final XOR 0xFFFFFFFF) and Adler-32 (RFC 1950).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ddt {

// Pass the previous result as `crc` to continue a running checksum.
std::uint32_t Crc32(const void* data, std::size_t size, std::uint32_t crc = 0);
std::uint32_t Adler32(const void* data, std::size_t size, std::uint32_t adler = 1);

}  // namespace ddt
