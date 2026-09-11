#include "mgscoop/peer_router.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using Router = mgscoop::PeerRouter;
using Kind = Router::DeliveryKind;
using Status = Router::ReceiveStatus;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

Router::PeerHandle bind(Router& router, Router::PeerId id, std::uint32_t initial_event = 0) {
    const auto result = router.bind_authenticated_peer(
        id, Router::AuthenticationEvidence::ValidatedByEngine, initial_event);
    require(result.status == Router::BindStatus::Bound && result.peer.has_value(), "validated peer binds");
    return *result.peer;
}

void authentication_and_capacity() {
    Router router;
    require(router.receive({7, 1}, Kind::LatestState, 0, {1}).status == Status::UnboundSender,
            "a sender ID in a packet is not an authenticated binding");
    for (auto evidence : {Router::AuthenticationEvidence::Unverified, Router::AuthenticationEvidence::Pending}) {
        require(router.bind_authenticated_peer(7, evidence).status == Router::BindStatus::Unverified,
                "pending and unverified identities cannot bind");
    }
    require(router.bind_authenticated_peer(0, Router::AuthenticationEvidence::ValidatedByEngine).status ==
                Router::BindStatus::InvalidId,
            "invalid zero identity is rejected");
    const auto first = bind(router, 100);
    bind(router, 200);
    bind(router, 300);
    bind(router, 400);
    require(router.authenticated_peers().size() == 4, "four bound peers enumerated");
    require(router.bind_authenticated_peer(500, Router::AuthenticationEvidence::ValidatedByEngine).status ==
                Router::BindStatus::Full,
            "fifth peer is rejected");
    require(router.bind_authenticated_peer(100, Router::AuthenticationEvidence::ValidatedByEngine).status ==
                Router::BindStatus::AlreadyBound,
            "duplicate bind cannot reset sequence state");
    require(router.receive({100, first.generation + 1}, Kind::LatestState, 0, {}).status == Status::UnboundSender,
            "identity with incorrect binding generation is rejected");
}

void independent_state_and_event_sequences() {
    Router router;
    const auto a = bind(router, 11);
    const auto b = bind(router, 22);
    require(router.receive(a, Kind::LatestState, 99, {9}).status == Status::Delivered, "first snapshot accepted");
    require(router.receive(b, Kind::LatestState, 1, {1}).status == Status::Delivered,
            "one peer's high sequence cannot suppress another peer");
    require(router.receive(a, Kind::LatestState, 99, {9}).status == Status::DuplicateOrOld, "snapshot deduplication");
    require(router.receive(a, Kind::LatestState, 98, {8}).status == Status::DuplicateOrOld, "old snapshot ignored");
    require(router.receive(a, Kind::ReliableOrderedEvent, 0, {4}).status == Status::Delivered,
            "snapshot sequence cannot suppress mission event");
    require(router.receive(b, Kind::ReliableOrderedEvent, 0, {5}).status == Status::Delivered,
            "reliable sequence is independent per peer");
    require(router.receive(a, Kind::ReliableOrderedEvent, 0, {4}).status == Status::DuplicateOrOld,
            "delivered reliable event cannot execute twice");
}

void reliable_reordering_keeps_events() {
    Router router;
    const auto peer = bind(router, 44);
    Router::Bytes second_payload{2, 8};
    auto result = router.receive(peer, Kind::ReliableOrderedEvent, 2, second_payload);
    require(result.status == Status::Buffered && result.delivered.empty(), "future event waits for the gap");
    second_payload[0] = 99;
    require(router.receive(peer, Kind::ReliableOrderedEvent, 2, {2, 8}).status == Status::DuplicateOrOld,
            "buffered duplicate is not enqueued twice");
    require(router.receive(peer, Kind::ReliableOrderedEvent, 2, {9}).status == Status::ConflictingReplay,
            "same buffered sequence with different payload is reported");
    result = router.receive(peer, Kind::ReliableOrderedEvent, 0, {0});
    require(result.delivered.size() == 1 && result.delivered[0].sequence == 0, "only contiguous events are delivered");
    result = router.receive(peer, Kind::ReliableOrderedEvent, 1, {1});
    require(result.status == Status::Delivered && result.delivered.size() == 2, "filling gap drains queued event");
    require(result.delivered[0].sequence == 1 && result.delivered[1].sequence == 2, "events delivered in order");
    require(result.delivered[1].payload == Router::Bytes({2, 8}), "buffer owns payload rather than caller memory");
    require(result.delivered[1].source == peer, "delivered event retains authenticated binding");
    require(router.buffered_event_count(peer) == 0, "drained event storage is released");
}

void wrap_and_ambiguous_sequences() {
    const auto maximum = std::numeric_limits<std::uint32_t>::max();
    Router router;
    const auto peer = bind(router, 55, maximum - 1);
    require(router.receive(peer, Kind::LatestState, maximum, {1}).status == Status::Delivered, "snapshot near wrap");
    require(router.receive(peer, Kind::LatestState, 0, {2}).status == Status::Delivered, "snapshot wraps to zero");
    require(router.receive(peer, Kind::LatestState, maximum, {1}).status == Status::DuplicateOrOld,
            "pre-wrap snapshot is old after wrap");
    require(router.receive(peer, Kind::LatestState, 0x80000000u, {}).status == Status::AmbiguousSequence,
            "half-range snapshot has no defined temporal order");
    require(router.receive(peer, Kind::ReliableOrderedEvent, 0, {0}).status == Status::Buffered,
            "post-wrap event can wait for pre-wrap events");
    require(router.receive(peer, Kind::ReliableOrderedEvent, maximum, {1}).status == Status::Buffered,
            "pre-wrap future event buffered");
    const auto result = router.receive(peer, Kind::ReliableOrderedEvent, maximum - 1, {2});
    require(result.delivered.size() == 3, "contiguous reliable drain crosses wrap");
    require(result.delivered[0].sequence == maximum - 1 && result.delivered[1].sequence == maximum &&
                result.delivered[2].sequence == 0,
            "wrapped reliable order preserved");
    require(router.receive(peer, Kind::ReliableOrderedEvent, maximum, {1}).status == Status::DuplicateOrOld,
            "wrapped reliable duplicate rejected");
    require(router.receive(peer, Kind::ReliableOrderedEvent, 0x80000001u, {}).status == Status::AmbiguousSequence,
            "half-range reliable sequence rejected without state change");
    require(router.receive(peer, Kind::ReliableOrderedEvent, 1, {3}).status == Status::Delivered,
            "ambiguous sequence cannot move reliable cursor");
}

void queue_limits_and_disconnect() {
    Router router;
    const auto a = bind(router, 66);
    const auto b = bind(router, 77);
    for (std::uint32_t sequence = 1; sequence < Router::kReliableWindow; ++sequence) {
        require(router.receive(a, Kind::ReliableOrderedEvent, sequence, {1}).status == Status::Buffered,
                "bounded future window accepts in-range events");
    }
    require(router.receive(a, Kind::ReliableOrderedEvent, 32, {1}).status == Status::SequenceGapTooLarge,
            "overflow reports explicit gap instead of silently losing a mission event");
    require(router.buffered_event_count(a) == Router::kReliableWindow - 1, "queue remains bounded");
    const auto drain = router.receive(a, Kind::ReliableOrderedEvent, 0, {0});
    require(drain.delivered.size() == Router::kReliableWindow, "full window drains in order");
    require(router.receive(a, Kind::ReliableOrderedEvent, 32, {1}).status == Status::Delivered,
            "previously rejected far event succeeds after retry");
    require(router.receive(a, Kind::ReliableOrderedEvent, 34, {1}).status == Status::Buffered, "prepare pending disconnect");
    require(router.receive(b, Kind::ReliableOrderedEvent, 1, {2}).status == Status::Buffered, "other peer has own pending event");
    require(router.disconnect(a), "disconnect removes exact binding");
    require(!router.disconnect(a) && !router.buffered_event_count(a).has_value(), "disconnect cleans queued state");
    const auto replacement = bind(router, 66);
    require(replacement.generation != a.generation, "reconnect gets new generation");
    require(router.receive(a, Kind::ReliableOrderedEvent, 33, {4}).status == Status::UnboundSender,
            "late work from previous binding cannot affect reconnect");
    require(router.receive(replacement, Kind::ReliableOrderedEvent, 0, {0}).status == Status::Delivered,
            "reconnected peer starts negotiated sequence");
    const auto other = router.receive(b, Kind::ReliableOrderedEvent, 0, {0});
    require(other.delivered.size() == 2, "disconnecting one peer preserves another's queue");
    router.clear();
    require(router.authenticated_peers().empty(), "clear removes every binding");
    const auto after_clear = bind(router, 66);
    require(after_clear.generation != replacement.generation, "clear never recycles stale generations");
}

void payload_and_kind_validation() {
    Router router;
    const auto peer = bind(router, 88);
    require(router.receive(peer, Kind::ReliableOrderedEvent, 0, Router::Bytes(Router::kMaxPayloadBytes + 1)).status ==
                Status::PayloadTooLarge,
            "oversized payload cannot enter queue");
    require(router.receive(peer, static_cast<Kind>(99), 0, {}).status == Status::InvalidDeliveryKind,
            "unknown delivery policy rejected");
    const auto exact = router.receive(peer, Kind::ReliableOrderedEvent, 0, Router::Bytes(Router::kMaxPayloadBytes));
    require(exact.status == Status::Delivered && exact.delivered[0].payload.size() == Router::kMaxPayloadBytes,
            "rejected payload and kind cannot advance event cursor");
}
} // namespace

int main() {
    try {
        authentication_and_capacity();
        independent_state_and_event_sequences();
        reliable_reordering_keeps_events();
        wrap_and_ambiguous_sequences();
        queue_limits_and_disconnect();
        payload_and_kind_validation();
        std::cout << "peer_router_tests: all routing and admission checks passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "peer_router_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
