#pragma once
// A minimal link v2 end (one link::Session plus the reliable channels) over a
// caller-supplied datagram sender. The Steam self-test (docs/steam-online.md
// section 14, stages P/L) and the fixture tests run two of these over two
// SteamTransports, so the transport is exercised with the real handshake,
// HMAC, FRAME, RELIABLE/RELACK and DATAGRAM traffic that partner_net sends.
// It mirrors partner_net's packing order and locking (one SRW lock around the
// session and the senders/receivers); it touches no game state and logs
// nothing. Constant-initializable (static instances; about 280 KB each).
#include <cstddef>
#include <cstdint>
#include "mgscoop/link_protocol.hpp"
#include "mgscoop/link_session.hpp"
#include "mgscoop/udp_transport.hpp"

namespace mgscoop::lab {

class LinkHarnessEnd {
public:
    using SendFn = bool (*)(void* ctx, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept;
    struct Counters {
        std::uint64_t frames_rx;      // FRAME messages delivered
        std::uint64_t frames_bad;     // FRAME payload not the expected pattern
        std::uint64_t rel_rx;         // reliable messages drained in order (channel 2)
        std::uint64_t rel_bad;        // out of order or corrupted
        std::uint64_t datagrams_rx;   // DATAGRAM channel 1
        std::uint64_t closes_rx;      // CLOSE messages
        std::uint32_t sessions;       // new sessions committed / completed
        std::uint32_t rejects;        // rejected events
        bool reject_verified;         // the last rejected event was authenticated
    };

    // `secret` is the 20-character link secret (cleared by the caller).
    void Start(link::Role role, bool play, const Endpoint& peer, const char (&secret)[link::kSecretChars], const char* build,
               SendFn send, void* send_ctx, std::uint64_t now_ms) noexcept;
    // Transport thread.
    void OnDatagram(const std::uint8_t* data, std::size_t size, const Endpoint& from, std::uint64_t now_ms) noexcept;
    void OnIdle(std::uint64_t now_ms) noexcept;
    // Any thread. FRAME payloads carry a 32-bit index for the pattern check.
    bool SendFrame(std::uint32_t index, std::uint64_t now_ms) noexcept;
    link::ReliableResult SendReliable(std::uint8_t channel, std::uint32_t index) noexcept;
    bool SendDatagram(std::uint8_t channel, std::uint32_t index, std::uint64_t now_ms) noexcept;
    // One DATA packet with a CLOSE message (the shutdown hand-off).
    bool SendClose(std::uint64_t now_ms) noexcept;
    link::LinkState State() const noexcept;
    bool Connected() const noexcept { return State() == link::LinkState::connected; }
    std::uint32_t RttMs() const noexcept;
    std::uint32_t SessionNumber() const noexcept;
    Endpoint Peer() const noexcept;
    Counters GetCounters() const noexcept;
    link::SessionStats Stats() const noexcept;

    static constexpr std::size_t kFrameSize = 64;
    static constexpr std::size_t kReliableSize = 200;

private:
    static bool Transmit(void* self, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept;
    static bool Random(void* self, std::uint8_t* out, std::size_t size) noexcept;
    void TakeAcks(link::RelAckState (&acks)[link::kReliableChannels], std::uint64_t now_ms, std::uint64_t min_age_ms) noexcept;
    void PopEvents() noexcept;

    void* lock_ = nullptr;  // SRWLOCK storage
    link::Session session_;
    link::ReliableSender tx_[link::kReliableChannels];
    link::ReliableReceiver rx_[link::kReliableChannels];
    std::uint8_t body_[link::kLinkMaxBody]{};
    SendFn send_ = nullptr;
    void* send_ctx_ = nullptr;
    Counters counters_{};
    std::uint32_t next_rel_ = 0;
};

}  // namespace mgscoop::lab
