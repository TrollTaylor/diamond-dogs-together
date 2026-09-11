#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace mgscoop {

// Portable routing policy only. The engine adapter owns authentication and transport.
// Binding evidence and PeerHandle values must come from that trusted adapter, never
// from the packet payload. This class does not authenticate Steam users by itself.
class PeerRouter {
public:
    using PeerId = std::uint64_t;
    using Bytes = std::vector<std::uint8_t>;

    static constexpr std::size_t kMaxPeers = 4;
    static constexpr std::size_t kMaxPayloadBytes = 1200;
    static constexpr std::size_t kReliableWindow = 32;

    enum class AuthenticationEvidence { Unverified, Pending, ValidatedByEngine };
    enum class BindStatus { Bound, Unverified, InvalidId, AlreadyBound, Full, GenerationExhausted };
    enum class DeliveryKind { LatestState, ReliableOrderedEvent };
    enum class ReceiveStatus {
        Delivered,
        Buffered,
        UnboundSender,
        PayloadTooLarge,
        DuplicateOrOld,
        AmbiguousSequence,
        SequenceGapTooLarge,
        ConflictingReplay,
        InvalidDeliveryKind
    };

    struct PeerHandle {
        PeerId id = 0;
        std::uint64_t generation = 0;

        bool operator==(const PeerHandle& other) const noexcept {
            return id == other.id && generation == other.generation;
        }
    };

    struct BindResult {
        BindStatus status;
        std::optional<PeerHandle> peer;
    };

    struct Packet {
        PeerHandle source;
        DeliveryKind kind;
        std::uint32_t sequence;
        Bytes payload;
    };

    struct ReceiveResult {
        ReceiveStatus status;
        // Apply only these packets, in order. Buffered packets are owned by the router.
        std::vector<Packet> delivered;
    };

    // initial_event_sequence is the first expected reliable event from this binding.
    // Negotiate it during the authenticated handshake; never infer it from whichever
    // reliable event happens to arrive first, since that event may have been reordered.
    BindResult bind_authenticated_peer(PeerId id, AuthenticationEvidence evidence,
                                       std::uint32_t initial_event_sequence = 0) {
        if (evidence != AuthenticationEvidence::ValidatedByEngine) {
            return {BindStatus::Unverified, std::nullopt};
        }
        if (id == 0) {
            return {BindStatus::InvalidId, std::nullopt};
        }
        for (const auto& peer : peers_) {
            if (peer && peer->handle.id == id) {
                return {BindStatus::AlreadyBound, std::nullopt};
            }
        }
        if (next_generation_ == 0) {
            return {BindStatus::GenerationExhausted, std::nullopt};
        }
        for (auto& slot : peers_) {
            if (!slot) {
                Peer peer;
                peer.handle = {id, next_generation_++};
                peer.next_event_sequence = initial_event_sequence;
                slot = std::move(peer);
                return {BindStatus::Bound, slot->handle};
            }
        }
        return {BindStatus::Full, std::nullopt};
    }

    bool disconnect(PeerHandle handle) {
        for (auto& peer : peers_) {
            if (peer && peer->handle == handle) {
                peer.reset();
                return true;
            }
        }
        return false;
    }

    void clear() {
        for (auto& peer : peers_) {
            peer.reset();
        }
        // Do not recycle generations: queued work from an old binding stays invalid.
    }

    std::vector<PeerHandle> authenticated_peers() const {
        std::vector<PeerHandle> result;
        for (const auto& peer : peers_) {
            if (peer) {
                result.push_back(peer->handle);
            }
        }
        return result;
    }

    std::optional<std::size_t> buffered_event_count(PeerHandle handle) const {
        for (const auto& peer : peers_) {
            if (peer && peer->handle == handle) {
                std::size_t count = 0;
                for (const auto& event : peer->events) {
                    count += event.has_value() ? 1u : 0u;
                }
                return count;
            }
        }
        return std::nullopt;
    }

    ReceiveResult receive(PeerHandle transport_source, DeliveryKind kind,
                          std::uint32_t sequence, const Bytes& payload) {
        Peer* peer = find(transport_source);
        if (!peer) {
            return {ReceiveStatus::UnboundSender, {}};
        }
        if (payload.size() > kMaxPayloadBytes) {
            return {ReceiveStatus::PayloadTooLarge, {}};
        }
        if (kind == DeliveryKind::LatestState) {
            return receive_state(*peer, sequence, payload);
        }
        if (kind == DeliveryKind::ReliableOrderedEvent) {
            return receive_event(*peer, sequence, payload);
        }
        return {ReceiveStatus::InvalidDeliveryKind, {}};
    }

private:
    static constexpr std::uint32_t kHalfSequenceSpace = 0x80000000u;

    struct Peer {
        PeerHandle handle;
        std::optional<std::uint32_t> latest_state_sequence;
        std::uint32_t next_event_sequence = 0;
        std::array<std::optional<Packet>, kReliableWindow> events;
    };

    Peer* find(PeerHandle handle) {
        for (auto& peer : peers_) {
            if (peer && peer->handle == handle) {
                return &*peer;
            }
        }
        return nullptr;
    }

    static ReceiveResult receive_state(Peer& peer, std::uint32_t sequence, const Bytes& payload) {
        if (peer.latest_state_sequence) {
            const auto distance = static_cast<std::uint32_t>(sequence - *peer.latest_state_sequence);
            if (distance == kHalfSequenceSpace) {
                return {ReceiveStatus::AmbiguousSequence, {}};
            }
            if (distance == 0 || distance > kHalfSequenceSpace) {
                return {ReceiveStatus::DuplicateOrOld, {}};
            }
        }
        peer.latest_state_sequence = sequence;
        ReceiveResult result{ReceiveStatus::Delivered, {}};
        result.delivered.push_back({peer.handle, DeliveryKind::LatestState, sequence, payload});
        return result;
    }

    static ReceiveResult receive_event(Peer& peer, std::uint32_t sequence, const Bytes& payload) {
        const auto distance = static_cast<std::uint32_t>(sequence - peer.next_event_sequence);
        if (distance == kHalfSequenceSpace) {
            return {ReceiveStatus::AmbiguousSequence, {}};
        }
        if (distance > kHalfSequenceSpace) {
            return {ReceiveStatus::DuplicateOrOld, {}};
        }
        if (distance >= kReliableWindow) {
            // No acknowledgement: the adapter must retry after the gap is filled or
            // explicitly resynchronize/disconnect. This is not snapshot-style loss.
            return {ReceiveStatus::SequenceGapTooLarge, {}};
        }
        if (distance != 0) {
            auto& pending = peer.events[sequence % kReliableWindow];
            if (pending) {
                if (pending->sequence == sequence && pending->payload == payload) {
                    return {ReceiveStatus::DuplicateOrOld, {}};
                }
                return {ReceiveStatus::ConflictingReplay, {}};
            }
            pending = Packet{peer.handle, DeliveryKind::ReliableOrderedEvent, sequence, payload};
            return {ReceiveStatus::Buffered, {}};
        }

        ReceiveResult result{ReceiveStatus::Delivered, {}};
        result.delivered.push_back({peer.handle, DeliveryKind::ReliableOrderedEvent, sequence, payload});
        ++peer.next_event_sequence;
        while (true) {
            auto& pending = peer.events[peer.next_event_sequence % kReliableWindow];
            if (!pending || pending->sequence != peer.next_event_sequence) {
                break;
            }
            result.delivered.push_back(std::move(*pending));
            pending.reset();
            ++peer.next_event_sequence;
        }
        return result;
    }

    std::array<std::optional<Peer>, kMaxPeers> peers_;
    std::uint64_t next_generation_ = 1;
};

} // namespace mgscoop
