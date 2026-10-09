#pragma once

#include "mgscoop/partner_input.hpp"
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
    constexpr std::uint8_t Walking   = 1 << 4;
    constexpr std::uint8_t Running   = 1 << 5;
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

// --- Offline partner frame extensions (partner wire format v2) ---------------
// Animation extension: 8 bytes appended after the 28-byte transform inside the
// same snapshot frame (header payload length 36, frame 64 bytes):
//   [0] 0x41 ("A")  [1] version 1  [2..3] act (u16 LE)  [4] stance  [5] speed
//   [6] flags (bit 0 valid; other bits zero)  [7] reserved, zero
// A v1 receiver (exact 56-byte frames) drops a v2 frame as malformed; a v2
// receiver accepts both sizes, so a v1 sender still drives the pose (no anim).
constexpr std::size_t anim_extension_size = 8;
constexpr std::size_t transform_anim_frame_size = transform_frame_size + anim_extension_size;  // 64
constexpr std::uint8_t anim_extension_magic = 0x41;
constexpr std::uint8_t anim_extension_version = 1;
namespace AnimFlag {
    constexpr std::uint8_t Valid = 1 << 0;
}
// The sender's slot-0 animation fields (native/partner_player ReadAnim):
// act = *(u16*)(*(Q+0x18))[local], stance = *(u8*)(*(Q+0x30))[local] (1 stand,
// 2 squat, 3 crawl), speed = *(u8*)(*(Q+0x38))[local] (0 idle, 2 walk, 3 run).
struct AnimState {
    std::uint16_t act = 0;
    std::uint8_t stance = 0;  // 0..3 (0 = unknown)
    std::uint8_t speed = 0;   // 0..3
    std::uint8_t flags = 0;   // AnimFlag bits
};
// Only known flag bits and speed <= 3; with AnimFlag::Valid the stance must be
// 1..3, without it stance and speed must be 0. The act id is not checked here:
// partner::LocomotionAct decides what may be written into the game arrays.
bool valid_anim(const AnimState& anim) noexcept;
// Returns transform_anim_frame_size, or 0 (out zeroed) when the transform or
// the anim state is invalid, or session/epoch is zero.
std::size_t encode_transform_anim_frame(std::uint64_t session, std::uint32_t epoch, const PlayerTransformPacket& transform,
                                        const AnimState& anim, std::uint8_t (&out)[transform_anim_frame_size]) noexcept;
// Accepts a v1 frame (56 bytes, has_anim = false, anim zeroed) or a v2 frame
// (64 bytes, valid extension, has_anim = true). Same transform rules as
// decode_transform_frame. Outputs are written only on success.
bool decode_partner_transform_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                                    PlayerTransformPacket& out, AnimState& anim, bool& has_anim) noexcept;

// Appearance ("look") frame: header kind event, sequence = revision (never 0),
// payload length 12 (a keepalive is kind event with length 0):
//   [0] 0x4C ("L")  [1] version 1  [2] type  [3] parts  [4] camo  [5] hand
//   [6..7] face (u16 LE)  [8] face_equip  [9] flags  [10..11] revision (u16 LE)
// Structural checks only; partner::NormalizeLook validates the values.
constexpr std::size_t look_packet_size = 12;
constexpr std::size_t look_frame_size = wire::header_size + look_packet_size;  // 40
constexpr std::uint8_t look_magic = 0x4C;
constexpr std::uint8_t look_version = 1;
namespace LookFlag {
    constexpr std::uint8_t PartsLoaded = 1 << 0;  // sender's own parts state was 3 at capture
}
struct LookPacket {
    std::uint8_t type = 0;        // Q+0x40[local] PlayerType
    std::uint8_t parts = 0;       // Q+0x48[local] PlayerPartsType
    std::uint8_t camo = 0;        // Q+0x50[local] PlayerCamoType
    std::uint8_t hand = 0;        // Q+0x58[local] PlayerHandType (computed by the game)
    std::uint16_t face = 0;       // Q+0x60[local] (u16)
    std::uint8_t face_equip = 0;  // Q+0x68[local]
    std::uint8_t flags = 0;       // LookFlag bits
    std::uint16_t revision = 0;   // sender bumps on change; 0 is never sent
};
// Returns look_frame_size, or 0 (out zeroed) for a zero session/epoch/revision
// or unknown flag bits.
std::size_t encode_look_frame(std::uint64_t session, std::uint32_t epoch, const LookPacket& look,
                              std::uint8_t (&out)[look_frame_size]) noexcept;
// Strict: exact size, magic/version, kind event, payload length 12, non-zero
// session/epoch/revision, header sequence == revision, known flag bits.
bool decode_look_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                       LookPacket& out) noexcept;

// Input frame (round 3, docs/partner-anim-appearance.md "Round 3"): header
// kind event, sequence = running u32 >= 1.
// Version 1, payload length 40, frame 68 bytes (decoded only):
//   [0] 0x49 ("I")  [1] version 1  [2] flags (partner_input::Flag, subset of 0x07)  [3] 0
//   [4..19] held, w4c, w50, w54 (u32 LE)  [20..27] lx, ly, rx, ry (i16 LE, never -32768)
//   [28] lt  [29] rt  [30..31] lmag  [32..33] lforce (u16 LE)  [34..35] ldir (i16, |x| <= 31416)
//   [36..37] cam_cdeg (i16, -18000..17999)  [38..39] 0
// Version 2 (round 4, "Round 4"), payload length 48, frame 76 bytes (encoded):
//   [0] 0x49  [1] version 2  [2] flags  [3] edge_seq (0 = no edge set)
//   [4..7] held  [8..11] now (w4c)  [12..15] press  [16..19] release (the edge set)
//   [20..37] as version 1  [38] edge_rep (0 new, 1..2 repeats; 0 without a set)  [39] 0
//   [40..43] long12 (level)  [44..47] tap12 (part of the edge set)
// With edge_seq 0 the press, release and tap12 words must be 0.
// A build without v2 drops the frame as malformed (both copies must run it).
// Version 3 (native puppet, docs/native-puppet.md 6.1), payload 72, frame 100
// (encoded only when the sender runs partner_drive=native):
//   [0..47] as version 2 (version byte 3)  [48..51] look_pitch f32  [52..55] look_yaw f32
//   [56..59] cam270 f32  [60..63] cam274 f32  [64..65] heading_mrad i16  [66] xflags
//   [67] inv_slot (0..15, 0xFF)  [68..69] equip_rev u16  [70..71] 0
// Version 4 (native rates and weapons, docs/native-puppet.md "Rates" and
// "Weapons"), payload 84, frame 112 (encoded whenever the sender runs
// partner_drive=native):
//   [0..71] as version 3 (version byte 4)  [72..73] rate_dash u16  [74..75] rate_crawl u16
//   [76..77] rate_crawl_factor u16 (milli, 500..2000 each with xflags Rates 0x10, else all 0;
//   sent unless partner_native_rates=off)  [78..79] mag u16  [80..81] mag_cap u16 (both 0
//   unless xflags Mag 0x20: 1 <= mag_cap <= 999, mag <= mag_cap)  [82..83] gun_equip u16
//   (the sender's gun record equip, 1..65535 with xflags Equip 0x80, else 0). xflags
//   Gamepad 0x40: the sender's PlayerPad is in gamepad mode (PP+0x488 bit 6 clear).
// Non-finite or out-of-range floats, unknown xflags, a non-zero field whose
// xflag is clear and non-zero reserved bytes are refused. The earlier
// single-feature v4 drafts (104 bytes: magazine only; 108 bytes: rates only)
// never shipped together; their sizes are refused as malformed.
constexpr std::size_t input_packet_size_v1 = 40;
constexpr std::size_t input_frame_size_v1 = wire::header_size + input_packet_size_v1;  // 68
constexpr std::size_t input_packet_size = 48;
constexpr std::size_t input_frame_size = wire::header_size + input_packet_size;  // 76
constexpr std::size_t input_packet_size_v3 = 72;
constexpr std::size_t input_frame_size_v3 = wire::header_size + input_packet_size_v3;  // 100
constexpr std::size_t input_packet_size_v4 = 84;
constexpr std::size_t input_frame_size_v4 = wire::header_size + input_packet_size_v4;  // 112
// v5 (docs/native-puppet.md "Weapons", R2/W1): v4 plus [84] gun flags
// (partner_input::GunFlag: valid, in hand, aiming, reloading, changing), [85] the
// sender's gun mode rec+0x2C0, [86] reload_seq, [87] switch_seq; without the
// valid flag all four are 0. A v5 sender also carries its current slot in the
// v3 inv_slot byte [67] (0..15, 0xFF unknown).
constexpr std::size_t input_packet_size_v5 = 88;
constexpr std::size_t input_frame_size_v5 = wire::header_size + input_packet_size_v5;  // 116
constexpr std::uint8_t input_magic = 0x49;
constexpr std::uint8_t input_version = 2;
constexpr std::uint8_t input_version_v3 = 3;
constexpr std::uint8_t input_version_v4 = 4;
constexpr std::uint8_t input_version_v5 = 5;
// Returns input_frame_size (a v2 frame), or 0 (out zeroed) for a zero
// session/epoch/sequence or a packet outside the wire ranges
// (partner_input::ValidPacket; packet.version must be 2).
std::size_t encode_input_frame(std::uint64_t session, std::uint32_t epoch, std::uint32_t sequence,
                               const partner_input::InputPacket& packet, std::uint8_t (&out)[input_frame_size]) noexcept;
// packet.version 2 -> a 76-byte v2 frame, 3 -> a 100-byte v3 frame, 4 -> a
// 112-byte v4 frame, 5 -> a 116-byte v5 frame (returned size), 0 (out zeroed)
// otherwise or for anything encode_input_frame refuses.
std::size_t encode_input_frame_any(std::uint64_t session, std::uint32_t epoch, std::uint32_t sequence,
                                   const partner_input::InputPacket& packet, std::uint8_t (&out)[input_frame_size_v5]) noexcept;
// Strict: a 68-byte v1, a 76-byte v2, a 100-byte v3, a 112-byte v4 or a 116-byte v5 frame; magic/version matching the size,
// kind event, the payload length, non-zero session/epoch/sequence, known flag
// bits, reserved bytes zero, the ranges above. out.version tells which.
bool decode_input_frame(const std::uint8_t* data, std::size_t size, std::uint64_t& session, std::uint32_t& epoch,
                        std::uint32_t& sequence, partner_input::InputPacket& out) noexcept;

} // namespace mgscoop::sync
