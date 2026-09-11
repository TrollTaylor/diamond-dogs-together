#include "mgscoop/input_receiver.hpp"
#include "mgscoop/typed_payload.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace mgscoop;
using Router = PeerRouter;
using Status = InputReceiveStatus;
using Bytes = Router::Bytes;
constexpr std::uint64_t session_id = 0x0102030405060708ull;
constexpr PeerId host_id = 100, guest_id = 200, other_id = 300, fourth_id = 400;
const BuildIdentity build{"receiver-fixture-executable", "receiver-fixture-content", 1};
unsigned checks = 0;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) throw std::runtime_error("Line " + std::to_string(__LINE__) + ": " #__VA_ARGS__); } while (false)

void set_u32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    CHECK(offset + 4 <= bytes.size());
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

Router::PeerHandle bind(Router& router, PeerId peer) {
    const auto result = router.bind_authenticated_peer(peer, Router::AuthenticationEvidence::ValidatedByEngine);
    CHECK(result.status == Router::BindStatus::Bound && result.peer);
    return *result.peer;
}

Bytes bytes_for(SlotToken token, std::uint32_t sequence, std::uint32_t epoch = 1,
                std::uint64_t transport_session = session_id) {
    const auto packet = typed::encode_input(transport_session, token, {epoch, sequence, 0.5f, -1.0f, 0x0081});
    CHECK(packet.has_value());
    return wire::encode(*packet);
}

struct Fixture {
    Session session{host_id, build};
    Router router;
    Router::PeerHandle host, guest, other, fourth;

    explicit Fixture(bool playing = true) {
        for (const auto peer : {guest_id, other_id, fourth_id})
            CHECK(session.join(peer, build).result == Result::ok);
        host = bind(router, host_id);
        guest = bind(router, guest_id);
        other = bind(router, other_id);
        fourth = bind(router, fourth_id);
        CHECK(session.player_count() == max_players);
        if (playing) {
            CHECK(session.begin_load(host_id, 10010, 1) == Result::ok);
            finish_load();
        }
    }

    void finish_load() {
        for (const auto& player : session.players())
            if (player.peer) CHECK(session.mark_loaded(player.peer, session.checkpoint().epoch) == Result::ok);
        CHECK(session.start_play(host_id) == Result::ok);
    }

    Bytes input(Router::PeerHandle source, std::uint32_t sequence) const {
        return bytes_for(session.token_for(source.id).value(), sequence, session.checkpoint().epoch);
    }

    InputReceiveResult send(Router::PeerHandle source, const Bytes& bytes, std::uint64_t time = 1000) {
        return receive_host_input(session, router, {source, session_id, time}, bytes);
    }

    void accept(Router::PeerHandle source, std::uint32_t sequence, std::uint64_t time = 1000) {
        const auto result = send(source, input(source, sequence), time);
        CHECK(result.status == Status::accepted);
        CHECK(result.router_status == Router::ReceiveStatus::Delivered && result.session_result == Result::ok);
        const auto applied = session.input_for(source.id, time);
        CHECK(applied && applied->sequence == sequence && applied->move_x == 0.5f &&
              applied->move_y == -1.0f && applied->buttons == 0x0081);
    }
};

void seed_unchanged(const Fixture& fixture) {
    const auto input = fixture.session.input_for(guest_id, 1000);
    CHECK(input && input->sequence == 10 && input->move_x == 0.5f && input->buttons == 0x0081);
    CHECK(fixture.session.players()[fixture.session.token_for(guest_id)->slot].input_received_ms == 1000);
    CHECK(!fixture.session.input_for(host_id, 1000) && !fixture.session.input_for(other_id, 1000) &&
          !fixture.session.input_for(fourth_id, 1000));
    auto inspect = fixture.router;
    CHECK(inspect.receive(fixture.guest, Router::DeliveryKind::LatestState, 11, {0}).status ==
          Router::ReceiveStatus::Delivered);
}

void reject_high(Fixture& fixture, const Bytes& bad, Status expected) {
    const auto result = fixture.send(fixture.guest, bad, 2000);
    CHECK(result.status == expected);
    CHECK(!result.router_status && !result.session_result);
    seed_unchanged(fixture);
}

void malformed_high_sequence_does_not_poison_cursor() {
    Fixture fixture;
    fixture.accept(fixture.guest, 10);
    const auto high = fixture.input(fixture.guest, 900);

    for (std::size_t size = 0; size < high.size(); ++size) {
        auto truncated = high;
        truncated.resize(size);
        reject_high(fixture, truncated, Status::invalid_frame);
    }
    auto bad = high;
    bad.push_back(0);
    reject_high(fixture, bad, Status::invalid_frame);
    bad.resize(Router::kMaxPayloadBytes + 1);
    reject_high(fixture, bad, Status::invalid_frame);

    // A claimed peer ID is not a field, even with the wire length adjusted.
    bad = high;
    bad.insert(bad.end(), {100, 0, 0, 0, 0, 0, 0, 0});
    set_u32(bad, 24, static_cast<std::uint32_t>(typed::input_payload_size + 8));
    reject_high(fixture, bad, Status::invalid_frame);

    for (const auto offset : {std::size_t{0}, std::size_t{4}, std::size_t{6}, std::size_t{24}}) {
        bad = high;
        bad[offset] = 0xff;
        reject_high(fixture, bad, Status::invalid_frame);
    }
    bad = high;
    set_u32(bad, 16, 0);
    reject_high(fixture, bad, Status::invalid_frame);
    bad = high;
    set_u32(bad, 8, 0);
    set_u32(bad, 12, 0);
    reject_high(fixture, bad, Status::invalid_frame);

    for (const auto kind : {wire::Kind::snapshot, wire::Kind::event, wire::Kind::loaded}) {
        bad = high;
        bad[6] = static_cast<std::uint8_t>(kind);
        reject_high(fixture, bad, Status::invalid_payload);
    }
    for (const auto offset : {std::size_t{0}, std::size_t{1}, std::size_t{2}}) {
        bad = high;
        bad[wire::header_size + offset] = 0xff;
        reject_high(fixture, bad, Status::invalid_payload);
    }
    bad = high;
    set_u32(bad, wire::header_size + 3, 0);
    reject_high(fixture, bad, Status::invalid_payload);
    for (const auto bits : {0x7fc00000u, 0x7f800001u, 0x7f800000u, 0xff800000u, 0x3f800001u}) {
        for (const auto offset : {std::size_t{7}, std::size_t{11}}) {
            bad = high;
            set_u32(bad, wire::header_size + offset, bits);
            reject_high(fixture, bad, Status::invalid_payload);
        }
    }
    bad = high;
    bad[wire::header_size + 16] = 1;
    reject_high(fixture, bad, Status::invalid_payload);
    fixture.accept(fixture.guest, 11, 1001);
}

void trusted_identity_epoch_and_slot_boundaries() {
    Fixture fixture;
    fixture.accept(fixture.guest, 10);
    const auto token = fixture.session.token_for(guest_id).value();
    reject_high(fixture, bytes_for(token, 900, 1, session_id + 1), Status::wrong_session);
    reject_high(fixture, bytes_for(token, 900, 2), Status::wrong_epoch);
    reject_high(fixture, bytes_for(fixture.session.token_for(other_id).value(), 900), Status::wrong_slot);
    reject_high(fixture, bytes_for(fixture.session.token_for(host_id).value(), 900), Status::wrong_slot);
    reject_high(fixture, bytes_for({token.slot, token.generation + 1}, 900), Status::wrong_slot);

    const auto high = fixture.input(fixture.guest, 900);
    for (const auto source : {Router::PeerHandle{0, 1}, Router::PeerHandle{guest_id, 0}}) {
        CHECK(fixture.send(source, high).status == Status::invalid_context);
        seed_unchanged(fixture);
    }
    for (const auto source : {Router::PeerHandle{999, 1},
                              Router::PeerHandle{guest_id, fixture.guest.generation + 1}}) {
        CHECK(fixture.send(source, high).status == Status::unbound_source);
        seed_unchanged(fixture);
    }
    CHECK(receive_host_input(fixture.session, fixture.router, {fixture.guest, 0, 1000}, high).status ==
          Status::invalid_context);
    CHECK(fixture.send(fixture.other, high).status == Status::wrong_slot);
    seed_unchanged(fixture);
    fixture.accept(fixture.other, 1); // Rejected guest token did not consume other peer's sequence.
    fixture.accept(fixture.fourth, 1);
    fixture.accept(fixture.host, 1);
    fixture.accept(fixture.guest, 11);
}

void recycled_connections_and_slots() {
    Fixture fixture;
    fixture.accept(fixture.guest, 10);
    const auto old_connection = fixture.guest;
    CHECK(fixture.router.disconnect(old_connection));
    fixture.guest = bind(fixture.router, guest_id);
    CHECK(!(fixture.guest == old_connection));
    CHECK(fixture.send(old_connection, fixture.input(fixture.guest, 900)).status == Status::unbound_source);
    fixture.accept(fixture.guest, 11);

    Fixture recycled(false);
    const auto old_token = recycled.session.token_for(guest_id).value();
    CHECK(recycled.session.leave(guest_id) == Result::ok);
    const auto admitted = recycled.session.join(500, build);
    CHECK(admitted.result == Result::ok && admitted.token);
    CHECK(admitted.token->slot == old_token.slot && admitted.token->generation != old_token.generation);
    CHECK(recycled.send(recycled.guest, bytes_for(old_token, 900)).status == Status::unassigned_source);
    CHECK(recycled.router.disconnect(recycled.guest));
    const auto replacement = bind(recycled.router, 500);
    CHECK(recycled.session.begin_load(host_id, 10010, 1) == Result::ok);
    recycled.finish_load();
    CHECK(recycled.send(replacement, bytes_for(old_token, 900)).status == Status::wrong_slot);
    CHECK(!recycled.session.input_for(replacement.id, 1000));
    recycled.accept(replacement, 1);
}

void session_rejection_does_not_commit_staged_routing() {
    Fixture fixture;
    fixture.accept(fixture.guest, 10);
    auto result = fixture.send(fixture.guest, fixture.input(fixture.guest, 900), 999);
    CHECK(result.status == Status::session_rejected && result.session_result == Result::invalid);
    CHECK(result.router_status == Router::ReceiveStatus::Delivered); // Staged only.
    seed_unchanged(fixture);
    fixture.accept(fixture.guest, 11, 1001);

    CHECK(fixture.session.begin_load(host_id, 10020, 2) == Result::ok);
    result = fixture.send(fixture.guest, fixture.input(fixture.guest, 900), 1002);
    CHECK(result.status == Status::session_rejected && result.session_result == Result::wrong_phase);
    CHECK(!fixture.session.input_for(guest_id, 1002));
    fixture.finish_load();
    // The router survives load epochs, even though Session clears held input.
    result = fixture.send(fixture.guest, fixture.input(fixture.guest, 1), 1003);
    CHECK(result.status == Status::router_rejected &&
          result.router_status == Router::ReceiveStatus::DuplicateOrOld && !result.session_result);
    fixture.accept(fixture.guest, 12, 1003);
    const auto old_epoch = bytes_for(fixture.session.token_for(guest_id).value(), 900, 1);
    CHECK(fixture.send(fixture.guest, old_epoch, 1004).status == Status::wrong_epoch);
    fixture.accept(fixture.guest, 13, 1004);

    CHECK(fixture.session.leave(host_id) == Result::ok);
    result = fixture.send(fixture.guest, fixture.input(fixture.guest, 900), 1005);
    CHECK(result.status == Status::session_rejected && result.session_result == Result::wrong_phase);
    CHECK(!fixture.session.input_for(guest_id, 1005));
    auto inspect = fixture.router;
    CHECK(inspect.receive(fixture.guest, Router::DeliveryKind::LatestState, 14, {0}).status ==
          Router::ReceiveStatus::Delivered);

    // Session can also have a later cursor than a newly bound router (for
    // example, after a connection is replaced). Session remains authoritative.
    Fixture ahead;
    CHECK(ahead.session.accept_input(guest_id, ahead.session.token_for(guest_id).value(),
                                    {1, 100, 0, 0, 0}, 1000) == Result::ok);
    result = ahead.send(ahead.guest, ahead.input(ahead.guest, 99), 1001);
    CHECK(result.status == Status::session_rejected && result.session_result == Result::stale);
    inspect = ahead.router;
    CHECK(inspect.receive(ahead.guest, Router::DeliveryKind::LatestState, 1, {0}).status ==
          Router::ReceiveStatus::Delivered); // No initial routing cursor was committed.
    ahead.accept(ahead.guest, 101, 1001);
}

void replay_wrap_and_held_input_expiry() {
    Fixture fixture;
    fixture.accept(fixture.guest, 0xfffffffeu, 1000);
    fixture.accept(fixture.guest, 0xffffffffu, 1001);
    fixture.accept(fixture.guest, 0, 1002);
    for (const auto sequence : {0u, 0xffffffffu, 0x80000000u}) {
        const auto result = fixture.send(fixture.guest, fixture.input(fixture.guest, sequence), 1100);
        CHECK(result.status == Status::router_rejected && !result.session_result);
        CHECK(result.router_status == (sequence == 0x80000000u ? Router::ReceiveStatus::AmbiguousSequence :
                                                               Router::ReceiveStatus::DuplicateOrOld));
        CHECK(fixture.session.input_for(guest_id, 1002)->sequence == 0);
    }
    CHECK(!fixture.session.input_for(guest_id, 1001)); // Host clock before receipt.
    CHECK(fixture.session.input_for(guest_id, 1252));
    CHECK(!fixture.session.input_for(guest_id, 1253));
    fixture.accept(fixture.guest, 1, 2000);
    fixture.accept(fixture.other, 0, 2000); // Sequence namespaces remain independent.
}

void unrelated_reliable_buffers_survive_commit_and_rejection() {
    Fixture fixture;
    const Bytes guest_event{1, 2, 3}, other_event{4, 5, 6}, initial_event{7};
    CHECK(fixture.router.receive(fixture.guest, Router::DeliveryKind::ReliableOrderedEvent, 1, guest_event).status ==
          Router::ReceiveStatus::Buffered);
    CHECK(fixture.router.receive(fixture.other, Router::DeliveryKind::ReliableOrderedEvent, 1, other_event).status ==
          Router::ReceiveStatus::Buffered);
    fixture.accept(fixture.guest, 10);
    CHECK(fixture.send(fixture.guest, fixture.input(fixture.guest, 900), 999).status == Status::session_rejected);
    CHECK(fixture.router.buffered_event_count(fixture.guest) == 1);
    CHECK(fixture.router.buffered_event_count(fixture.other) == 1);
    for (const auto source : {fixture.guest, fixture.other}) {
        const auto result = fixture.router.receive(source, Router::DeliveryKind::ReliableOrderedEvent, 0, initial_event);
        CHECK(result.status == Router::ReceiveStatus::Delivered && result.delivered.size() == 2);
        CHECK(result.delivered[0].sequence == 0 && result.delivered[0].payload == initial_event);
        CHECK(result.delivered[1].sequence == 1 && result.delivered[1].source == source &&
              result.delivered[1].payload == (source == fixture.guest ? guest_event : other_event));
        CHECK(fixture.router.buffered_event_count(source) == 0);
    }
    fixture.accept(fixture.guest, 11, 1001);
}
} // namespace

int main() {
    try {
        malformed_high_sequence_does_not_poison_cursor();
        trusted_identity_epoch_and_slot_boundaries();
        recycled_connections_and_slots();
        session_rejection_does_not_commit_staged_routing();
        replay_wrap_and_held_input_expiry();
        unrelated_reliable_buffers_survive_commit_and_rejection();
        std::cout << "input_receiver_tests: " << checks << " host admission and rollback checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "input_receiver_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
