#include "mgscoop/protocol.hpp"
#include <stdexcept>
namespace mgscoop::wire {
namespace {
void put(std::vector<std::uint8_t>& out, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
std::uint64_t get(const std::uint8_t* data, unsigned width) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(data[i]) << (i * 8);
    return value;
}
bool valid_kind(std::uint64_t value) { return value >= 1 && value <= 4; }
}
std::vector<std::uint8_t> encode(const Packet& packet) {
    if (!packet.session || !packet.epoch || packet.payload.size() > max_payload ||
        !valid_kind(static_cast<std::uint16_t>(packet.kind))) throw std::invalid_argument("Invalid packet");
    std::vector<std::uint8_t> out{'M', 'G', 'S', 'C'};
    out.reserve(header_size + packet.payload.size());
    put(out, version, 2);
    put(out, static_cast<std::uint16_t>(packet.kind), 2);
    put(out, packet.session, 8);
    put(out, packet.epoch, 4);
    put(out, packet.sequence, 4);
    put(out, packet.payload.size(), 4);
    out.insert(out.end(), packet.payload.begin(), packet.payload.end());
    return out;
}
std::optional<Packet> decode(const std::uint8_t* bytes, std::size_t size) {
    if (!bytes || size < header_size || size > header_size + max_payload) return {};
    if (bytes[0] != 'M' || bytes[1] != 'G' || bytes[2] != 'S' || bytes[3] != 'C') return {};
    if (get(bytes + 4, 2) != version || !valid_kind(get(bytes + 6, 2))) return {};
    if (get(bytes + 24, 4) != size - header_size) return {};
    Packet packet;
    packet.kind = static_cast<Kind>(get(bytes + 6, 2));
    packet.session = get(bytes + 8, 8);
    packet.epoch = static_cast<std::uint32_t>(get(bytes + 16, 4));
    packet.sequence = static_cast<std::uint32_t>(get(bytes + 20, 4));
    if (!packet.session || !packet.epoch) return {};
    packet.payload.assign(bytes + header_size, bytes + size);
    return packet;
}
}
