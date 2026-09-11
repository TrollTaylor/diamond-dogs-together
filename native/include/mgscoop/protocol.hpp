#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace mgscoop::wire {
constexpr std::uint16_t version = 1;
constexpr std::size_t header_size = 28;
constexpr std::size_t max_payload = 1024;
enum class Kind : std::uint16_t { input = 1, snapshot = 2, event = 3, loaded = 4 };
struct Packet {
    Kind kind = Kind::input;
    std::uint64_t session = 0;
    std::uint32_t epoch = 0;
    std::uint32_t sequence = 0;
    std::vector<std::uint8_t> payload;
};
// Explicit little-endian framing. Sender identity comes from the transport.
// This framing does not provide authentication, delivery, or payload validation.
std::vector<std::uint8_t> encode(const Packet& packet);
std::optional<Packet> decode(const std::uint8_t* bytes, std::size_t size);
}
