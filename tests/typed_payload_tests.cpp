#include "mgscoop/typed_payload.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace mgscoop;
namespace payload = mgscoop::typed;
using Router = PeerRouter;
constexpr std::uint64_t session_id = 0x0102030405060708ull;
const BuildIdentity build{"fixture-executable", "fixture-content", 1};
unsigned checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)

void set_u32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
    CHECK(offset + 4 <= bytes.size());
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

Router::PeerHandle bind(Router& router, std::uint64_t peer) {
    const auto result = router.bind_authenticated_peer(peer, Router::AuthenticationEvidence::ValidatedByEngine);
    CHECK(result.status == Router::BindStatus::Bound && result.peer.has_value());
    return *result.peer;
}

Router::Packet deliver(Router& router, Router::PeerHandle sender, const wire::Packet& packet) {
    auto result = router.receive(sender, Router::DeliveryKind::LatestState, packet.sequence, wire::encode(packet));
    CHECK(result.status == Router::ReceiveStatus::Delivered && result.delivered.size() == 1);
    return std::move(result.delivered[0]);
}

void begin_play(Session& session) {
    CHECK(session.begin_load(100, 10010, 1) == Result::ok);
    for (const auto& player : session.players())
        if (player.peer) CHECK(session.mark_loaded(player.peer, session.checkpoint().epoch) == Result::ok);
    CHECK(session.start_play(100) == Result::ok);
}

payload::PoseRoster roster_for(const Session& session) {
    payload::PoseRoster roster;
    for (std::size_t slot = 0; slot < max_players; ++slot) {
        const auto& player = session.players()[slot];
        if (!player.peer) continue;
        auto& pose = roster.players[roster.count++];
        pose.token = {static_cast<std::uint8_t>(slot), player.generation};
        pose.position = {static_cast<float>(slot), -2.5f, 0};
    }
    return roster;
}

payload::PoseReceiveContext pose_context(const Session& session, Router::PeerHandle host) {
    payload::PoseReceiveContext context{host, session_id, session.checkpoint().epoch, {}};
    for (std::size_t slot = 0; slot < max_players; ++slot)
        if (session.players()[slot].peer) context.active_generations[slot] = session.players()[slot].generation;
    return context;
}

void input_wire_and_rejections() {
    const InputFrame frame{7, 0, 0.5f, -1.0f, 0x00a5};
    const auto encoded = payload::encode_input(session_id, {3, 0x01020304}, frame);
    CHECK(encoded && encoded->kind == wire::Kind::input && encoded->epoch == 7 && encoded->sequence == 0);
    const std::vector<std::uint8_t> golden{
        1, 1, 3, 4, 3, 2, 1, 0, 0, 0, 0x3f, 0, 0, 0x80, 0xbf, 0xa5, 0};
    CHECK(encoded->payload == golden && golden.size() == payload::input_payload_size);
    auto bytes = wire::encode(*encoded);
    CHECK(bytes.size() == wire::header_size + payload::input_payload_size);
    auto envelope = wire::decode(bytes.data(), bytes.size());
    CHECK(envelope.has_value());
    const auto decoded = payload::decode_input(*envelope);
    CHECK(decoded && decoded->token.slot == 3 && decoded->token.generation == 0x01020304);
    CHECK(decoded->frame.epoch == 7 && decoded->frame.sequence == 0 && decoded->frame.move_x == 0.5f &&
          decoded->frame.move_y == -1.0f && decoded->frame.buttons == 0x00a5);
    CHECK(!payload::decode_pose_roster(*encoded));

    for (std::size_t length = 0; length < golden.size(); ++length) {
        auto packet = *encoded;
        packet.payload.resize(length);
        CHECK(!payload::decode_input(packet));
    }
    for (std::size_t length = 0; length < bytes.size(); ++length) CHECK(!wire::decode(bytes.data(), length));
    CHECK(!wire::decode(nullptr, bytes.size()));
    for (auto offset : {std::size_t{0}, std::size_t{1}, std::size_t{2}}) {
        auto packet = *encoded;
        packet.payload[offset] = 0xff;
        CHECK(!payload::decode_input(packet));
    }
    for (auto kind : {wire::Kind::snapshot, wire::Kind::event, wire::Kind::loaded, static_cast<wire::Kind>(99)}) {
        auto packet = *encoded; packet.kind = kind;
        CHECK(!payload::decode_input(packet));
    }
    auto bad = *encoded; bad.payload.push_back(0);
    CHECK(!payload::decode_input(bad));
    bad = *encoded; bad.session = 0;
    CHECK(!payload::decode_input(bad));
    bad = *encoded; bad.epoch = 0;
    CHECK(!payload::decode_input(bad));
    bad = *encoded; set_u32(bad.payload, 3, 0);
    CHECK(!payload::decode_input(bad));
    for (unsigned bit = 8; bit < 16; ++bit) {
        bad = *encoded;
        bad.payload[16] = static_cast<std::uint8_t>(1u << (bit - 8));
        CHECK(!payload::decode_input(bad));
    }
    // Quiet/signaling NaNs, both infinities, and nearest out-of-range magnitudes.
    for (auto bits : {0x7fc00000u, 0x7f800001u, 0xffc00000u, 0x7f800000u, 0xff800000u,
                      0x3f800001u, 0xbf800001u}) {
        for (auto offset : {std::size_t{7}, std::size_t{11}}) {
            bad = *encoded; set_u32(bad.payload, offset, bits);
            CHECK(!payload::decode_input(bad));
        }
    }
    CHECK(!payload::encode_input(0, {0, 1}, frame));
    CHECK(!payload::encode_input(session_id, {4, 1}, frame));
    CHECK(!payload::encode_input(session_id, {0, 0}, frame));
    auto invalid = frame; invalid.epoch = 0;
    CHECK(!payload::encode_input(session_id, {0, 1}, invalid));
    invalid = frame; invalid.move_x = std::numeric_limits<float>::quiet_NaN();
    CHECK(!payload::encode_input(session_id, {0, 1}, invalid));
    invalid = frame; invalid.move_y = 2;
    CHECK(!payload::encode_input(session_id, {0, 1}, invalid));
    invalid = frame; invalid.buttons = 0x0100;
    CHECK(!payload::encode_input(session_id, {0, 1}, invalid));
    CHECK(payload::encode_input(session_id, {0, 1}, {1, 0xffffffffu, -1, 1, payload::allowed_buttons}));
}

void pose_wire_and_rejections() {
    payload::PoseRoster roster;
    roster.count = 1;
    roster.players[0] = {{0, 0x01020304}, {1, -2.5f, 0}};
    const auto encoded = payload::encode_pose_roster(session_id, 7, 9, roster);
    CHECK(encoded && encoded->kind == wire::Kind::snapshot);
    const std::vector<std::uint8_t> golden{
        2, 1, 1, 0, 4, 3, 2, 1, 0, 0, 0x80, 0x3f, 0, 0, 0x20, 0xc0, 0, 0, 0, 0};
    CHECK(encoded->payload == golden);
    const auto decoded = payload::decode_pose_roster(*encoded);
    CHECK(decoded && decoded->count == 1 && decoded->players[0].token.generation == 0x01020304 &&
          decoded->players[0].position == roster.players[0].position);
    CHECK(!payload::decode_input(*encoded));
    for (std::size_t length = 0; length < golden.size(); ++length) {
        auto packet = *encoded; packet.payload.resize(length);
        CHECK(!payload::decode_pose_roster(packet));
    }
    auto bad = *encoded; bad.payload.push_back(0);
    CHECK(!payload::decode_pose_roster(bad));
    for (auto offset : {std::size_t{0}, std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        bad = *encoded; bad.payload[offset] = 0xff;
        CHECK(!payload::decode_pose_roster(bad));
    }
    bad = *encoded; bad.payload[2] = 0;
    CHECK(!payload::decode_pose_roster(bad));
    bad = *encoded; set_u32(bad.payload, 4, 0);
    CHECK(!payload::decode_pose_roster(bad));
    for (auto bits : {0x7fc00000u, 0x7f800001u, 0x7f800000u, 0xff800000u, 0x49742401u, 0xc9742401u}) {
        for (auto offset : {std::size_t{8}, std::size_t{12}, std::size_t{16}}) {
            bad = *encoded; set_u32(bad.payload, offset, bits);
            CHECK(!payload::decode_pose_roster(bad));
        }
    }
    for (auto kind : {wire::Kind::input, wire::Kind::event, wire::Kind::loaded, static_cast<wire::Kind>(99)}) {
        bad = *encoded; bad.kind = kind;
        CHECK(!payload::decode_pose_roster(bad));
    }
    bad = *encoded; bad.epoch = 0;
    CHECK(!payload::decode_pose_roster(bad));
    bad = *encoded; bad.session = 0;
    CHECK(!payload::decode_pose_roster(bad));
    CHECK(!payload::encode_pose_roster(0, 1, 0, roster));
    CHECK(!payload::encode_pose_roster(session_id, 0, 0, roster));
    auto invalid = roster; invalid.count = 0;
    CHECK(!payload::encode_pose_roster(session_id, 1, 0, invalid));
    invalid.count = 5;
    CHECK(!payload::encode_pose_roster(session_id, 1, 0, invalid));
    invalid = roster; invalid.players[0].token.slot = 4;
    CHECK(!payload::encode_pose_roster(session_id, 1, 0, invalid));
    invalid = roster; invalid.players[0].position[0] = std::numeric_limits<float>::infinity();
    CHECK(!payload::encode_pose_roster(session_id, 1, 0, invalid));

    roster.count = 4;
    for (std::size_t i = 0; i < max_players; ++i)
        roster.players[i] = {{static_cast<std::uint8_t>(i), 1},
                            {payload::max_position_component, -payload::max_position_component, 0}};
    const auto four = payload::encode_pose_roster(session_id, 1, 0, roster);
    CHECK(four && four->payload.size() == payload::max_pose_roster_payload);
    const auto decoded_four = payload::decode_pose_roster(*four);
    CHECK(decoded_four && decoded_four->count == 4);
    for (std::size_t length = 0; length < four->payload.size(); ++length) {
        bad = *four; bad.payload.resize(length);
        CHECK(!payload::decode_pose_roster(bad));
    }
    for (auto slot : {0u, 3u}) {
        bad = *four; bad.payload[payload::pose_roster_prefix_size + payload::pose_entry_size] = static_cast<std::uint8_t>(slot);
        CHECK(!payload::decode_pose_roster(bad)); // Duplicate or nonascending slots.
    }
    roster.players[1].token.slot = 0;
    CHECK(!payload::encode_pose_roster(session_id, 1, 0, roster));
}

void input_identity_boundary() {
    Session session(100, build);
    CHECK(session.join(200, build).result == Result::ok);
    CHECK(session.join(300, build).result == Result::ok);
    begin_play(session);
    Router router;
    bind(router, 100);
    const auto guest = bind(router, 200);
    const auto other = bind(router, 300);
    const auto token = session.token_for(200).value();
    CHECK(guest.generation != token.generation); // Distinct generation domains.
    const payload::InputReceiveContext context{guest, token, session_id, 1};
    auto packet = payload::encode_input(session_id, token, {1, 10, 0.5f, 0, 1}).value();
    const auto routed = deliver(router, guest, packet);
    const auto input = payload::input_from_routed(routed, context);
    CHECK(input.has_value());
    CHECK(session.accept_input(guest.id, input->token, input->frame, 1000) == Result::ok);
    CHECK(session.input_for(200, 1000)->move_x == 0.5f);

    // Authenticated guest 200 can encode guest 300's slot, but cannot apply it.
    packet = payload::encode_input(session_id, session.token_for(300).value(), {1, 11, 1, 1, 0}).value();
    CHECK(payload::decode_input(packet)); // Syntactic validation is not admission.
    CHECK(!payload::input_from_routed(deliver(router, guest, packet), context));
    CHECK(!session.input_for(300, 1000));
    packet = payload::encode_input(session_id, token, {1, 12, 0, 0, 0}).value();
    CHECK(!payload::input_from_routed(deliver(router, other, packet), context));
    auto bad = routed; bad.sequence += 1;
    CHECK(!payload::input_from_routed(bad, context));
    bad = routed; bad.kind = Router::DeliveryKind::ReliableOrderedEvent;
    CHECK(!payload::input_from_routed(bad, context));
    for (auto kind : {wire::Kind::snapshot, wire::Kind::event, wire::Kind::loaded}) {
        auto wrong = payload::encode_input(session_id, token, {1, 13, 0, 0, 0}).value();
        wrong.kind = kind;
        bad = routed; bad.sequence = wrong.sequence; bad.payload = wire::encode(wrong);
        CHECK(!payload::input_from_routed(bad, context));
    }
    auto stale_context = context; stale_context.epoch = 2;
    CHECK(!payload::input_from_routed(routed, stale_context));
    stale_context = context; stale_context.session += 1;
    CHECK(!payload::input_from_routed(routed, stale_context));
    stale_context = context; stale_context.assigned_slot.generation += 1;
    CHECK(!payload::input_from_routed(routed, stale_context));
    stale_context = context; stale_context.authenticated_peer.id = 0;
    CHECK(!payload::input_from_routed(routed, stale_context));
    stale_context = context; stale_context.authenticated_peer.generation = 0;
    CHECK(!payload::input_from_routed(routed, stale_context));
    // A claimed peer ID appended to an otherwise valid body is not a field.
    auto smuggled = payload::encode_input(session_id, token, {1, 14, 0, 0, 0}).value();
    smuggled.payload.insert(smuggled.payload.end(), {100, 0, 0, 0, 0, 0, 0, 0});
    CHECK(!payload::input_from_routed(deliver(router, guest, smuggled), context));
    CHECK(router.disconnect(guest));
    stale_context = context; stale_context.authenticated_peer = bind(router, 200);
    CHECK(!payload::input_from_routed(routed, stale_context)); // Queued old binding.

    Session recycled(100, build);
    const auto old_token = recycled.join(200, build).token.value();
    CHECK(recycled.leave(200) == Result::ok);
    const auto new_token = recycled.join(300, build).token.value();
    begin_play(recycled);
    CHECK(old_token.slot == new_token.slot && old_token.generation != new_token.generation);
    const payload::InputReceiveContext replacement{other, new_token, session_id, 1};
    packet = payload::encode_input(session_id, old_token, {1, 15, 0, 0, 0}).value();
    CHECK(!payload::input_from_routed(deliver(router, other, packet), replacement));
}

void roster_authority_and_complete_updates() {
    Session session(100, build);
    for (auto peer : {200u, 300u, 400u}) CHECK(session.join(peer, build).result == Result::ok);
    begin_play(session);
    Router router;
    const auto host = bind(router, 100);
    const auto guest = bind(router, 200);
    const auto context = pose_context(session, host);
    auto roster = roster_for(session);
    const auto packet = payload::encode_pose_roster(session_id, 1, 100, roster).value();
    const auto routed = deliver(router, host, packet);
    const auto accepted = payload::pose_roster_from_routed(routed, context);
    CHECK(accepted && accepted->count == 4 && accepted->players[3].token.slot == 3);
    CHECK(!payload::pose_roster_from_routed(deliver(router, guest, packet), context));
    auto partial = roster; partial.count = 3;
    auto update = payload::encode_pose_roster(session_id, 1, 101, partial).value();
    CHECK(!payload::pose_roster_from_routed(deliver(router, host, update), context));
    auto wrong_generation = roster; wrong_generation.players[2].token.generation += 1;
    update = payload::encode_pose_roster(session_id, 1, 102, wrong_generation).value();
    CHECK(!payload::pose_roster_from_routed(deliver(router, host, update), context));
    auto inactive = context; inactive.active_generations[3] = 0;
    CHECK(!payload::pose_roster_from_routed(routed, inactive)); // Cannot admit slot 3.
    inactive = context; inactive.active_generations[0] = 0;
    CHECK(!payload::pose_roster_from_routed(routed, inactive));
    auto stale = context; stale.epoch = 2;
    CHECK(!payload::pose_roster_from_routed(routed, stale));
    stale = context; stale.session += 1;
    CHECK(!payload::pose_roster_from_routed(routed, stale));
    auto bad = routed; bad.sequence += 1;
    CHECK(!payload::pose_roster_from_routed(bad, context));
    bad = routed; bad.kind = Router::DeliveryKind::ReliableOrderedEvent;
    CHECK(!payload::pose_roster_from_routed(bad, context));
    bad = routed; bad.payload.resize(wire::header_size - 1);
    CHECK(!payload::pose_roster_from_routed(bad, context));

    // Every accepted latest-state message supplies all active poses, so
    // reordering cannot select the newest update for only one arbitrary slot.
    for (std::size_t i = 0; i < roster.count; ++i) roster.players[i].position[0] = 10;
    update = payload::encode_pose_roster(session_id, 1, 104, roster).value();
    const auto newest = payload::pose_roster_from_routed(deliver(router, host, update), context);
    CHECK(newest && newest->count == 4);
    for (std::size_t i = 0; i < newest->count; ++i) CHECK(newest->players[i].position[0] == 10);
    CHECK(router.receive(host, Router::DeliveryKind::LatestState, packet.sequence, wire::encode(packet)).status ==
          Router::ReceiveStatus::DuplicateOrOld);
    CHECK(router.disconnect(host));
    stale = context; stale.authoritative_host = bind(router, 100);
    CHECK(!payload::pose_roster_from_routed(routed, stale));
}
} // namespace

int main() {
    try {
        input_wire_and_rejections();
        pose_wire_and_rejections();
        input_identity_boundary();
        roster_authority_and_complete_updates();
        std::cout << "typed_payload_tests: " << checks << " codec and trusted-boundary checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "typed_payload_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
