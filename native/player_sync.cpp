#include "mgscoop/player_sync.hpp"

#include <cstring>
#include <limits>

namespace mgscoop::sync {
namespace {

static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "player_sync requires IEEE-754 binary32 floats");

bool bounded_coord(float value) noexcept {
    std::uint32_t bits = 0, limit_bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const float limit = max_coord_component;
    std::memcpy(&limit_bits, &limit, sizeof(limit_bits));
    // Reject NaN (exponent all 1s + non-zero mantissa) and values exceeding limit
    return (bits & 0x7fffffffu) <= limit_bits;
}

bool bounded_rot(float value) noexcept {
    std::uint32_t bits = 0, limit_bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const float limit = 6.28318530718f * 2.0f; // 4*pi
    std::memcpy(&limit_bits, &limit, sizeof(limit_bits));
    return (bits & 0x7fffffffu) <= limit_bits;
}

bool valid_transform(const PlayerTransformPacket& t) noexcept {
    return bounded_coord(t.x) && bounded_coord(t.y) && bounded_coord(t.z) &&
           bounded_rot(t.rot_y) && t.stance <= static_cast<std::uint8_t>(Stance::Crawl) &&
           (t.action_flags & 0xf0u) == 0;
}

void put(std::uint8_t* out, std::uint32_t value, unsigned width = 4) noexcept {
    for(unsigned i=0;i<width;++i) out[i]=static_cast<std::uint8_t>(value>>(i*8));
}
std::uint32_t get(const std::uint8_t* in, unsigned width = 4) noexcept {
    std::uint32_t value=0;
    for(unsigned i=0;i<width;++i) value|=static_cast<std::uint32_t>(in[i])<<(i*8);
    return value;
}
void put_float(std::uint8_t* out, float value) noexcept {
    std::uint32_t bits=0; std::memcpy(&bits,&value,sizeof(bits)); put(out,bits);
}
float get_float(const std::uint8_t* in) noexcept {
    const auto bits=get(in); float value=0; std::memcpy(&value,&bits,sizeof(value)); return value;
}

} // namespace

std::optional<wire::Packet> encode_transform(std::uint64_t session,
                                            std::uint32_t epoch,
                                            const PlayerTransformPacket& transform) {
    if (!valid_transform(transform) || session == 0 || epoch == 0) return std::nullopt;

    wire::Packet packet;
    packet.kind = wire::Kind::snapshot;
    packet.session = session;
    packet.epoch = epoch;
    packet.sequence = transform.sequence;
    packet.payload.resize(transform_packet_size);
    auto* bytes=packet.payload.data();
    put(bytes,transform.sequence);
    put_float(bytes+4,transform.x); put_float(bytes+8,transform.y); put_float(bytes+12,transform.z);
    put_float(bytes+16,transform.rot_y); put(bytes+20,transform.equip_id,2);
    bytes[22]=transform.stance; bytes[23]=transform.action_flags; put(bytes+24,transform.tick_ms);

    return packet;
}

std::optional<PlayerTransformPacket> decode_transform(const wire::Packet& packet) {
    if (packet.kind != wire::Kind::snapshot || packet.session == 0 || packet.epoch == 0 ||
        packet.payload.size() != transform_packet_size) {
        return std::nullopt;
    }

    PlayerTransformPacket transform{};
    const auto* bytes=packet.payload.data();
    transform.sequence=get(bytes);
    transform.x=get_float(bytes+4); transform.y=get_float(bytes+8); transform.z=get_float(bytes+12);
    transform.rot_y=get_float(bytes+16); transform.equip_id=static_cast<std::uint16_t>(get(bytes+20,2));
    transform.stance=bytes[22]; transform.action_flags=bytes[23]; transform.tick_ms=get(bytes+24);

    if (!valid_transform(transform) || transform.sequence != packet.sequence) return std::nullopt;

    return transform;
}

} // namespace mgscoop::sync
