#pragma once

#include "mgscoop/peer_router.hpp"
#include "mgscoop/protocol.hpp"
#include "mgscoop/session.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mgscoop::typed {

constexpr std::uint8_t payload_version = 1;
enum class Schema : std::uint8_t { player_input = 1, player_pose_roster = 2 };
constexpr std::uint16_t allowed_buttons = 0x00ff;
constexpr std::size_t input_payload_size = 17;
constexpr std::size_t pose_entry_size = 17;
constexpr std::size_t pose_roster_prefix_size = 3;
constexpr std::size_t max_pose_roster_payload = pose_roster_prefix_size + max_players * pose_entry_size;
// Defensive codec limit only. No FOX units, coordinate space, or actor validity
// have been established by this codec.
constexpr float max_position_component = 1000000.0f;

struct PlayerInput {
    SlotToken token{};
    InputFrame frame{};
};

struct PlayerPose {
    SlotToken token{};
    std::array<float, 3> position{};
};

// A complete, ordered pose roster for the trusted active slots. This is not a
// FOX world/checkpoint snapshot and cannot admit players or create actors.
// Only the first count entries are serialized; later array entries are unused.
struct PoseRoster {
    std::uint8_t count = 0;
    std::array<PlayerPose, max_players> players{};
};

// Payload v1, explicit little endian (IEEE-754 binary32 floats):
// input: schema:u8, version:u8, slot:u8, generation:u32, x:f32, y:f32, buttons:u16.
// pose:  schema:u8, version:u8, count:u8, then count times:
//        slot:u8, generation:u32, x:f32, y:f32, z:f32.
// Slots are 0..3; generations are nonzero. Pose slots are strictly ascending.
// Session/epoch/sequence occur only in the existing wire::Packet header.
// No peer identity, pointer, receive timestamp, or authentication claim appears
// in these payloads. Invalid values/kinds/versions/lengths return nullopt.
std::optional<wire::Packet> encode_input(std::uint64_t session, SlotToken token, const InputFrame& frame);
std::optional<PlayerInput> decode_input(const wire::Packet& packet);
std::optional<wire::Packet> encode_pose_roster(std::uint64_t session, std::uint32_t epoch,
                                             std::uint32_t sequence, const PoseRoster& roster);
std::optional<PoseRoster> decode_pose_roster(const wire::Packet& packet);

// These contexts MUST be supplied from current trusted admission state, never
// populated from a decoded payload. PeerRouter binding generation (u64) and
// Session slot generation (u32) are independent and are not compared together.
struct InputReceiveContext {
    PeerRouter::PeerHandle authenticated_peer{};
    SlotToken assigned_slot{};
    std::uint64_t session = 0;
    std::uint32_t epoch = 0;
};

struct PoseReceiveContext {
    PeerRouter::PeerHandle authoritative_host{};
    std::uint64_t session = 0;
    std::uint32_t epoch = 0;
    // Zero means inactive. Slot 0 is the current Session's host slot.
    std::array<std::uint32_t, max_players> active_generations{};
};

// Call only for PeerRouter-delivered LatestState packets whose payload holds a
// complete wire frame. These checks add typed validation; they do not prove
// authentication, routing delivery, engine ownership, or mission readiness.
// Both envelope sequence numbers must agree. Continue the router's per-binding
// LatestState sequence across message kinds/epochs; this codec does not reset it.
// Pass accepted input to Session::accept_input with the trusted caller and host
// clock so that phase, freshness, and replay checks are still enforced there.
std::optional<PlayerInput> input_from_routed(const PeerRouter::Packet& routed,
                                            const InputReceiveContext& context);
std::optional<PoseRoster> pose_roster_from_routed(const PeerRouter::Packet& routed,
                                                const PoseReceiveContext& context);

} // namespace mgscoop::typed
