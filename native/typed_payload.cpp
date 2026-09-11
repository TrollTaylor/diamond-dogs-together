#include "mgscoop/typed_payload.hpp"

#include <cstring>
#include <limits>

namespace mgscoop::typed {
namespace {
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Typed payloads require IEEE-754 binary32 floats");
static_assert(max_players == 4 && PeerRouter::kMaxPeers == max_players,
              "Typed slot bounds must match session and router");
static_assert(max_pose_roster_payload <= wire::max_payload &&
              wire::header_size + max_pose_roster_payload <= PeerRouter::kMaxPayloadBytes,
              "Typed frames must fit both transport bounds");

void put(std::vector<std::uint8_t>& bytes, std::uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

std::uint32_t get(const std::uint8_t* bytes, unsigned width) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < width; ++i) value |= std::uint32_t(bytes[i]) << (8 * i);
    return value;
}

void put_float(std::vector<std::uint8_t>& bytes, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put(bytes, bits, 4);
}

float get_float(const std::uint8_t* bytes) {
    const auto bits = get(bytes, 4);
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool valid_slot(SlotToken token) {
    return token.slot < max_players && token.generation != 0;
}

bool same_slot(SlotToken left, SlotToken right) {
    return left.slot == right.slot && left.generation == right.generation;
}

bool bounded(float value, float limit) {
    // IEEE binary32 magnitudes sort as unsigned integers. Reject infinities
    // and both NaN forms without arithmetic on untrusted signaling NaN bits.
    std::uint32_t bits = 0, limit_bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    std::memcpy(&limit_bits, &limit, sizeof(limit_bits));
    return (bits & 0x7fffffffu) <= limit_bits;
}

bool valid_input(SlotToken token, const InputFrame& frame) {
    return valid_slot(token) && frame.epoch != 0 &&
           bounded(frame.move_x, 1.0f) && bounded(frame.move_y, 1.0f) &&
           (frame.buttons & static_cast<std::uint16_t>(~allowed_buttons)) == 0;
}

bool valid_pose(const PlayerPose& pose) {
    if (!valid_slot(pose.token)) return false;
    for (float value : pose.position) if (!bounded(value, max_position_component)) return false;
    return true;
}

bool valid_roster(const PoseRoster& roster) {
    if (roster.count == 0 || roster.count > max_players) return false;
    for (std::size_t i = 0; i < roster.count; ++i) {
        if (!valid_pose(roster.players[i])) return false;
        if (i && roster.players[i - 1].token.slot >= roster.players[i].token.slot) return false;
    }
    return true;
}

bool valid_header(const wire::Packet& packet, wire::Kind kind) {
    return packet.kind == kind && packet.session != 0 && packet.epoch != 0;
}

bool schema_matches(const std::vector<std::uint8_t>& bytes, Schema schema) {
    return bytes.size() >= 2 && bytes[0] == static_cast<std::uint8_t>(schema) && bytes[1] == payload_version;
}

bool valid_binding(PeerRouter::PeerHandle peer) {
    return peer.id != 0 && peer.generation != 0;
}

std::optional<wire::Packet> routed_frame(const PeerRouter::Packet& routed,
                                        PeerRouter::PeerHandle expected_peer,
                                        std::uint64_t session, std::uint32_t epoch) {
    if (!valid_binding(expected_peer) || !(routed.source == expected_peer) || !session || !epoch ||
        routed.kind != PeerRouter::DeliveryKind::LatestState) return {};
    auto packet = wire::decode(routed.payload.data(), routed.payload.size());
    if (!packet || packet->session != session || packet->epoch != epoch || packet->sequence != routed.sequence)
        return {};
    return packet;
}
} // namespace

std::optional<wire::Packet> encode_input(std::uint64_t session, SlotToken token, const InputFrame& frame) {
    if (!session || !valid_input(token, frame)) return {};
    wire::Packet packet{wire::Kind::input, session, frame.epoch, frame.sequence, {}};
    auto& bytes = packet.payload;
    bytes.reserve(input_payload_size);
    bytes.push_back(static_cast<std::uint8_t>(Schema::player_input));
    bytes.push_back(payload_version);
    bytes.push_back(token.slot);
    put(bytes, token.generation, 4);
    put_float(bytes, frame.move_x);
    put_float(bytes, frame.move_y);
    put(bytes, frame.buttons, 2);
    return packet;
}

std::optional<PlayerInput> decode_input(const wire::Packet& packet) {
    if (!valid_header(packet, wire::Kind::input) || packet.payload.size() != input_payload_size ||
        !schema_matches(packet.payload, Schema::player_input)) return {};
    const auto* bytes = packet.payload.data();
    PlayerInput input;
    input.token = {bytes[2], get(bytes + 3, 4)};
    input.frame = {packet.epoch, packet.sequence, get_float(bytes + 7), get_float(bytes + 11),
                   static_cast<std::uint16_t>(get(bytes + 15, 2))};
    if (!valid_input(input.token, input.frame)) return {};
    return input;
}

std::optional<wire::Packet> encode_pose_roster(std::uint64_t session, std::uint32_t epoch,
                                             std::uint32_t sequence, const PoseRoster& roster) {
    if (!session || !epoch || !valid_roster(roster)) return {};
    wire::Packet packet{wire::Kind::snapshot, session, epoch, sequence, {}};
    auto& bytes = packet.payload;
    bytes.reserve(pose_roster_prefix_size + roster.count * pose_entry_size);
    bytes.push_back(static_cast<std::uint8_t>(Schema::player_pose_roster));
    bytes.push_back(payload_version);
    bytes.push_back(roster.count);
    for (std::size_t i = 0; i < roster.count; ++i) {
        const auto& pose = roster.players[i];
        bytes.push_back(pose.token.slot);
        put(bytes, pose.token.generation, 4);
        for (float coordinate : pose.position) put_float(bytes, coordinate);
    }
    return packet;
}

std::optional<PoseRoster> decode_pose_roster(const wire::Packet& packet) {
    if (!valid_header(packet, wire::Kind::snapshot) || packet.payload.size() < pose_roster_prefix_size ||
        !schema_matches(packet.payload, Schema::player_pose_roster)) return {};
    const auto count = packet.payload[2];
    if (!count || count > max_players ||
        packet.payload.size() != pose_roster_prefix_size + count * pose_entry_size) return {};
    PoseRoster roster;
    roster.count = count;
    for (std::size_t i = 0; i < count; ++i) {
        const auto* bytes = packet.payload.data() + pose_roster_prefix_size + i * pose_entry_size;
        auto& pose = roster.players[i];
        pose.token = {bytes[0], get(bytes + 1, 4)};
        for (std::size_t axis = 0; axis < pose.position.size(); ++axis)
            pose.position[axis] = get_float(bytes + 5 + axis * 4);
    }
    if (!valid_roster(roster)) return {};
    return roster;
}

std::optional<PlayerInput> input_from_routed(const PeerRouter::Packet& routed,
                                            const InputReceiveContext& context) {
    if (!valid_slot(context.assigned_slot)) return {};
    const auto packet = routed_frame(routed, context.authenticated_peer, context.session, context.epoch);
    if (!packet) return {};
    auto input = decode_input(*packet);
    if (!input || !same_slot(input->token, context.assigned_slot)) return {};
    return input;
}

std::optional<PoseRoster> pose_roster_from_routed(const PeerRouter::Packet& routed,
                                                const PoseReceiveContext& context) {
    if (context.active_generations[0] == 0) return {};
    const auto packet = routed_frame(routed, context.authoritative_host, context.session, context.epoch);
    if (!packet) return {};
    auto roster = decode_pose_roster(*packet);
    if (!roster) return {};
    std::size_t active_count = 0;
    for (auto generation : context.active_generations) if (generation != 0) ++active_count;
    if (roster->count != active_count) return {};
    for (std::size_t i = 0; i < roster->count; ++i) {
        const auto token = roster->players[i].token;
        if (context.active_generations[token.slot] != token.generation) return {};
    }
    return roster;
}

} // namespace mgscoop::typed
