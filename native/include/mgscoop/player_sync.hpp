#pragma once

#include "mgscoop/peer_router.hpp"
#include "mgscoop/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mgscoop::sync {

constexpr std::uint8_t sync_payload_version = 1;
constexpr std::size_t transform_packet_size = 28;

// Defensive coordinate limit: 1,000,000 meters.
constexpr float max_coord_component = 1000000.0f;

enum class Stance : std::uint8_t {
    Stand = 0,
    Crouch = 1,
    Crawl = 2,
};

namespace ActionFlag {
    constexpr std::uint8_t Aiming    = 1 << 0;
    constexpr std::uint8_t Firing    = 1 << 1;
    constexpr std::uint8_t Sprinting = 1 << 2;
    constexpr std::uint8_t Reloading = 1 << 3;
}

#pragma pack(push, 1)
struct PlayerTransformPacket {
    std::uint32_t sequence = 0;      // 4 bytes: monotonic packet ID
    float x = 0.0f;                  // 4 bytes: world pos X
    float y = 0.0f;                  // 4 bytes: world pos Y
    float z = 0.0f;                  // 4 bytes: world pos Z
    float rot_y = 0.0f;              // 4 bytes: yaw heading angle in radians
    std::uint16_t equip_id = 0;      // 2 bytes: active weapon/item ID
    std::uint8_t stance = 0;         // 1 byte:  Stance enum
    std::uint8_t action_flags = 0;   // 1 byte:  ActionFlag bitmask
    std::uint32_t tick_ms = 0;       // 4 bytes: timestamp (GetTickCount64 lower 32)
};
#pragma pack(pop)

static_assert(sizeof(PlayerTransformPacket) == transform_packet_size,
              "PlayerTransformPacket must be packed exactly to 28 bytes");

// Encode transform into wire packet (little-endian framed).
std::optional<wire::Packet> encode_transform(std::uint64_t session,
                                            std::uint32_t epoch,
                                            const PlayerTransformPacket& transform);

// Decode wire packet into validated PlayerTransformPacket.
std::optional<PlayerTransformPacket> decode_transform(const wire::Packet& packet);

} // namespace mgscoop::sync
