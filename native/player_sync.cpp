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
           bounded_rot(t.rot_y) && t.stance <= static_cast<std::uint8_t>(Stance::Crawl);
}

} // namespace

std::optional<wire::Packet> encode_transform(std::uint64_t session,
                                            std::uint32_t epoch,
                                            const PlayerTransformPacket& transform) {
    if (!valid_transform(transform) || session == 0) return std::nullopt;

    wire::Packet packet;
    packet.kind = wire::Kind::snapshot;
    packet.session = session;
    packet.epoch = epoch;
    packet.sequence = transform.sequence;
    packet.payload.resize(sizeof(PlayerTransformPacket));
    std::memcpy(packet.payload.data(), &transform, sizeof(PlayerTransformPacket));

    return packet;
}

std::optional<PlayerTransformPacket> decode_transform(const wire::Packet& packet) {
    if (packet.kind != wire::Kind::snapshot ||
        packet.payload.size() != sizeof(PlayerTransformPacket)) {
        return std::nullopt;
    }

    PlayerTransformPacket transform{};
    std::memcpy(&transform, packet.payload.data(), sizeof(PlayerTransformPacket));

    if (!valid_transform(transform)) return std::nullopt;

    return transform;
}

} // namespace mgscoop::sync
