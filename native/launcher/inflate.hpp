// Project-authored DEFLATE decoder written from RFC 1951 (raw deflate) and
// RFC 1950 (zlib wrapper). No third-party code.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ddt {

// Decodes a raw DEFLATE stream and appends the result to `out`. Refuses to
// produce more than `max_output` bytes (bomb guard). `consumed`, when given,
// receives the number of input bytes used up to the end of the final block.
bool InflateRaw(const std::uint8_t* src, std::size_t size, std::vector<std::uint8_t>& out, std::size_t max_output,
                std::string& error, std::size_t* consumed = nullptr);

// Decodes a zlib stream (RFC 1950): checks the header, refuses a preset
// dictionary and verifies the Adler-32 trailer.
bool ZlibDecompress(const std::uint8_t* src, std::size_t size, std::vector<std::uint8_t>& out, std::size_t max_output,
                    std::string& error);

}  // namespace ddt
