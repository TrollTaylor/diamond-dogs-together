#pragma once

#include "mgscoop/peer_router.hpp"
#include "mgscoop/protocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mgscoop::sync {

// Development format only: not connected to the native game transport. This
// version is a source constant, not an on-wire discriminator. A future transport
// must select this schema explicitly; it cannot replace the typed pose roster.
constexpr std::uint8_t sync_payload_version = 1;
constexpr std::size_t transform_packet_size = 28;

// Defensive coordinate limit in uncalibrated game units.
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

// Decode a structurally valid transform. Transport identity, mission ownership,
// sequence freshness and actor mapping still require a trusted receiver.
std::optional<PlayerTransformPacket> decode_transform(const wire::Packet& packet);

// Fixed-buffer, non-allocating, non-throwing equivalents for the game thread
// and the UDP receive thread (offline partner spike, partner_net). A frame is
// the 28-byte MGSC header (kind snapshot, payload length 28) followed by the
// transform: byte-identical to wire::encode(*encode_transform(...)).
constexpr std::size_t transform_frame_size = wire::header_size + transform_packet_size;  // 56
// Header-only MGSC frame (kind event, payload length 0) used as a keepalive.
constexpr std::size_t keepalive_frame_size = wire::header_size;  // 28

// Returns transform_frame_size, or 0 (out zeroed) when encode_transform would
// refuse the same input (invalid transform, zero session or epoch).
std::size_t encode_transform_frame(std::uint64_t session, std::uint32_t epoch, const PlayerTransformPacket& transform,
                                   std::uint8_t (&out)[transform_frame_size]) noexcept;
// Strict: exact size, magic, version, kind snapshot, non-zero session and
// epoch, payload length 28, a valid transform and payload sequence equal to
// the header sequence. Outputs are written only on success.
bool decode_transform_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                            PlayerTransformPacket& out) noexcept;
// Keepalive: kind event, sequence 0, no payload. Returns keepalive_frame_size or 0.
std::size_t encode_keepalive_frame(std::uint64_t session, std::uint32_t epoch,
                                   std::uint8_t (&out)[keepalive_frame_size]) noexcept;
bool decode_keepalive_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session) noexcept;

} // namespace mgscoop::sync
