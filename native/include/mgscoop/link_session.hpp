#pragma once
// Two-PC link v2 session state machine (docs/link-protocol.md "Handshake"):
// the guest initiates (HELLO -> WELCOME -> CONFIRM), the host responds and
// locks onto the guest endpoint only through that authenticated exchange.
// Authenticates, replay-checks and acknowledges DATA, and runs the timers
// (HELLO cadence, CONFIRM resends, keepalives, ack-only packets, lost /
// reconnecting detection).
//
// Pure: the clock, the nonce source and the datagram sink are injected, no
// thread, lock, allocation or logging. State changes are queued as Events
// for the caller to log outside its locks. Not thread safe: partner_net holds
// g_tx_lock around every call. Each response to unauthenticated input is
// smaller than that input (WELCOME 104 and REJECT 88 bytes answer only a
// 1200-byte HELLO).
#include <cstddef>
#include <cstdint>
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/link_protocol.hpp"
#include "mgscoop/udp_transport.hpp"

namespace mgscoop::link {

using lab::Endpoint;

enum class Role : std::uint8_t { host, guest };
// waiting: a Steam guest without a host yet (partner_net only; the session never enters it).
enum class LinkState : std::uint8_t { off, disabled, listening, connecting, handshake, connected, reconnecting, lost, rejected, waiting };
const char* LinkStateName(LinkState state) noexcept;

enum class RejectReason : std::uint8_t {
    none = 0, proto_mismatch = 1, build_mismatch = 2, mode_mismatch = 3, bad_code = 4, busy = 5, role_mismatch = 6
};
const char* RejectReasonName(RejectReason reason) noexcept;

enum class DropReason : std::uint8_t { malformed, wrong_session, wrong_endpoint, replay, bad_mac, rate, unknown_msg };
inline constexpr unsigned kDropReasons = 7;
const char* DropReasonName(DropReason reason) noexcept;

// Timing and limits.
inline constexpr std::uint64_t kHelloFastMs = 500;          // first 10 s of connecting
inline constexpr std::uint64_t kHelloFastPhaseMs = 10000;
inline constexpr std::uint64_t kHelloSlowMs = 2000;
inline constexpr std::uint64_t kHelloBackoffAfterMs = 600000;  // 10 minutes without success
inline constexpr std::uint64_t kHelloBackoffMs = 10000;
inline constexpr std::uint64_t kRejectedRetryMs = 5000;
inline constexpr std::uint64_t kConfirmResendMs = 250;
inline constexpr unsigned kConfirmMaxResends = 10;
inline constexpr std::uint64_t kGuestReconnectMs = 2000;
inline constexpr std::uint64_t kHostLostMs = 3000;
inline constexpr std::uint64_t kCommitMinGapMs = 2000;
// Five keepalives per reconnect timeout: one lost keepalive never reconnects.
inline constexpr std::uint64_t kKeepaliveMs = 400;
// The host answers a CONFIRM of the current session only this long after the
// commit (the guest's resend phase); later it is a replay.
inline constexpr std::uint64_t kConfirmAnswerMs = kConfirmResendMs * (kConfirmMaxResends + 1);
inline constexpr std::uint64_t kAckDelayMs = 50;
inline constexpr std::uint64_t kDupConfirmGapMs = 250;      // <= 4 answers per second
inline constexpr std::uint64_t kUnauthRejectGapMs = 1000;   // <= 1 UNAUTH REJECT per second in total
// Per source address (any port): 8/s, burst 16. A colliding address shares
// the entry until it has been idle kRateIdleMs, and a replacement starts with
// kRateReplaceTokens, so neither collisions nor port cycling mint tokens.
inline constexpr std::uint32_t kHelloRatePerSec = 8, kHelloBurst = 16;
inline constexpr unsigned kRateTable = 64;
inline constexpr std::uint64_t kRateIdleMs = 2000;
inline constexpr std::uint32_t kRateReplaceTokens = 2;
inline constexpr std::uint32_t kMacBudgetPerSec = 500;
// HELLOs from the current (or lost) guest's address use their own MAC budget:
// a flood from other addresses cannot starve its reconnect.
inline constexpr std::uint32_t kGuestMacPerSec = 16;
inline constexpr unsigned kSeenRing = 32;
inline constexpr unsigned kPendingSlots = 2;
// The guest accepts a WELCOME echoing any of its last kGuestNonces HELLO nonces.
inline constexpr unsigned kGuestNonces = 4;

struct SessionConfig {
    Role role = Role::guest;
    bool play = false;
    // Lab loopback: the transport already filters 127.0.0.1:peer (exact).
    bool exact_filter = true;
    // Guest: the host endpoint (sends go only here; WELCOME / REJECT must come
    // from its port). Host: unused.
    Endpoint peer{};
    char build[kBuildSize]{};  // NUL-padded, e.g. "0.11.1"
    HmacKey psk{};             // K_psk
};

struct SessionIo {
    void* context = nullptr;
    bool (*send)(void* context, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept = nullptr;
    bool (*random)(void* context, std::uint8_t* out, std::size_t size) noexcept = nullptr;
};

enum class EventKind : std::uint8_t {
    listening, connecting, handshake, connected, lost, reconnecting, reconnected, rebound, rejected, warn_build, crypto
};
enum class HandshakeStep : std::uint8_t { hello_rx, welcome_rx, confirm_tx, confirm_rx };
const char* HandshakeStepName(HandshakeStep step) noexcept;
struct Event {
    EventKind kind;
    HandshakeStep step;
    RejectReason reason;
    bool verified;
    std::uint16_t remote_proto;
    char remote_build[kBuildSize + 1];
    std::uint32_t session, attempt, rtt_ms, gap_ms, after_ms;
    Endpoint peer;
};

// What OnDatagram hands back: an authenticated DATA body to deliver.
struct Delivery {
    bool data = false;
    const std::uint8_t* body = nullptr;
    std::size_t size = 0;
    std::uint32_t send_ms = 0;
    // A new session was committed (host) or completed (guest) by this
    // datagram: the caller resets its receive state before delivering.
    bool new_session = false;
};

struct SessionStats {
    std::uint64_t drops[kDropReasons];
    std::uint64_t src_changed;
    std::uint64_t reject_ignored;
    std::uint64_t tx_packets, tx_bytes, rx_packets, rx_bytes;
};

class Session {
public:
    void Start(const SessionConfig& config, const SessionIo& io, std::uint64_t now_ms) noexcept;
    Delivery OnDatagram(const std::uint8_t* data, std::size_t size, const Endpoint& from, std::uint64_t now_ms) noexcept;
    // Handshake timers, keepalives, ack-only packets and lost detection.
    void OnTimer(std::uint64_t now_ms) noexcept;
    // One DATA packet on the current session (game data, reliable traffic,
    // ack-only and keepalives). False when not connected or exhausted.
    bool SendData(const std::uint8_t* body, std::size_t size, std::uint64_t now_ms) noexcept;
    bool CanSendData() const noexcept { return state_ == LinkState::connected; }
    // True while a received ack-eliciting packet has been waiting this long.
    bool AckPendingFor(std::uint64_t now_ms, std::uint64_t ms) const noexcept;

    LinkState state() const noexcept { return state_; }
    std::uint32_t session_number() const noexcept { return session_number_; }
    // The endpoint data goes to (host: the locked guest; guest: the host).
    Endpoint peer() const noexcept;
    bool has_peer() const noexcept;
    RejectReason reject_reason() const noexcept { return reject_reason_; }
    bool reject_verified() const noexcept { return reject_verified_; }
    const char* remote_build() const noexcept { return remote_build_; }
    std::uint16_t remote_proto() const noexcept { return remote_proto_; }
    const SessionStats& stats() const noexcept { return stats_; }
    const RttEstimator& rtt() const noexcept { return rtt_; }
    LossMeter& loss() noexcept { return loss_; }
    JitterMeter& jitter() noexcept { return jitter_; }
    std::uint64_t last_tx_ms() const noexcept { return current_.last_tx_ms; }
    bool PopEvent(Event& out) noexcept;
    // Tests only: jump the current session's send sequence (exhaustion).
    void set_tx_seq_for_test(std::uint32_t seq) noexcept { current_.tx_seq = seq; }

private:
    struct Live {
        bool valid;
        std::uint64_t sid;
        HmacKey tx, rx;
        ReplayWindow window;
        SentRing sent;
        std::uint32_t tx_seq;
        std::uint64_t last_rx_ms, last_tx_ms, highest_rx_ms;
        bool ack_pending;
        std::uint64_t ack_since_ms;
        Endpoint endpoint;
        std::uint8_t ng[kNonceSize];
    };
    struct Pending {
        bool used;
        std::uint8_t ng[kNonceSize];
        SessionKeys keys;
        Endpoint endpoint;
        std::uint64_t created_ms;
        char build[kBuildSize + 1];
        std::uint8_t welcome[kLinkOverhead + kWelcomeBodySize];
    };
    struct RateEntry {
        bool used;
        std::uint64_t key;
        std::uint32_t milli_tokens;
        std::uint64_t last_ms;
    };

    void Push(const Event& event) noexcept;
    Event MakeEvent(EventKind kind) const noexcept;
    void Drop(DropReason reason) noexcept { ++stats_.drops[static_cast<unsigned>(reason)]; }
    bool Send(const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept;
    bool Random(std::uint8_t* out, std::size_t size) noexcept;
    bool SendOn(Live& live, PacketType type, const std::uint8_t* body, std::size_t size, std::uint32_t seq,
                std::uint64_t now_ms) noexcept;
    bool SendDataOn(Live& live, const std::uint8_t* body, std::size_t size, std::uint64_t now_ms) noexcept;
    Delivery Accept(Live& live, const Header& h, const std::uint8_t* data, std::uint64_t now_ms) noexcept;
    void SendReject(RejectReason reason, const HmacKey* key, const std::uint8_t* echo, const Endpoint& to) noexcept;
    bool SendUnauthReject(RejectReason reason, const Endpoint& to, std::uint64_t now_ms) noexcept;
    // Host.
    Delivery HostDatagram(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from,
                          std::uint64_t now_ms) noexcept;
    void HostHello(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from, std::uint64_t now_ms) noexcept;
    bool RateAllow(const Endpoint& from, std::uint64_t now_ms) noexcept;
    bool MacBudget(std::uint64_t now_ms) noexcept;
    bool GuestMacBudget(std::uint64_t now_ms) noexcept;
    bool Seen(const std::uint8_t* ng) const noexcept;
    void Commit(Pending& slot, const Endpoint& from, std::uint64_t now_ms) noexcept;
    void HostTimer(std::uint64_t now_ms) noexcept;
    // Guest.
    Delivery GuestDatagram(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from,
                           std::uint64_t now_ms) noexcept;
    void GuestWelcome(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from, std::uint64_t now_ms) noexcept;
    void GuestReject(const Header& h, const std::uint8_t* data, std::size_t size, const Endpoint& from, std::uint64_t now_ms) noexcept;
    int PendingNonce(const std::uint8_t* echo) const noexcept;
    void SendHello(std::uint64_t now_ms) noexcept;
    void SendConfirm(std::uint64_t now_ms) noexcept;
    void BeginConnecting(std::uint64_t now_ms, bool reconnect) noexcept;
    std::uint64_t HelloInterval(std::uint64_t now_ms) const noexcept;
    void GuestTimer(std::uint64_t now_ms) noexcept;
    void Exhausted(std::uint64_t now_ms) noexcept;
    void TimerLive(Live& live, std::uint64_t now_ms) noexcept;

    SessionConfig config_{};
    SessionIo io_{};
    HmacKey hs_{};
    LinkState state_ = LinkState::off;
    std::uint32_t session_number_ = 0;
    Live current_{};
    SessionStats stats_{};
    RttEstimator rtt_{};
    LossMeter loss_{};
    JitterMeter jitter_{};
    RejectReason reject_reason_ = RejectReason::none;
    bool reject_verified_ = false;
    char remote_build_[kBuildSize + 1]{};
    std::uint16_t remote_proto_ = 0;
    char warned_build_[kBuildSize + 1]{};
    // Host.
    Pending pending_[kPendingSlots]{};
    std::uint8_t seen_[kSeenRing][kNonceSize]{};
    unsigned seen_count_ = 0, seen_head_ = 0;
    RateEntry rate_[kRateTable]{};
    std::uint32_t mac_milli_tokens_ = kMacBudgetPerSec * 1000;
    std::uint64_t mac_last_ms_ = 0;
    std::uint32_t guest_mac_milli_tokens_ = kGuestMacPerSec * 1000;
    std::uint64_t guest_mac_last_ms_ = 0;
    bool unauth_sent_ = false;
    std::uint64_t unauth_last_ms_ = 0;
    std::uint64_t dup_confirm_last_ms_ = 0;
    bool committed_ = false;
    std::uint64_t last_commit_ms_ = 0;
    // Guest.
    Live next_{};  // handshake in progress
    std::uint8_t nonces_[kGuestNonces][kNonceSize]{};
    unsigned nonce_count_ = 0, nonce_head_ = 0;
    std::uint64_t connecting_since_ms_ = 0, next_hello_ms_ = 0;
    std::uint32_t attempt_ = 0;
    std::uint64_t confirm_sent_ms_ = 0;
    unsigned confirm_resends_ = 0;
    std::uint64_t hint_logged_ms_ = 0;
    bool hint_logged_ = false;
    // Scratch datagram (callers serialize every call).
    std::uint8_t scratch_[kLinkMaxDatagram]{};
    static constexpr unsigned kEvents = 32;
    Event events_[kEvents]{};
    unsigned event_head_ = 0, event_count_ = 0;
};

} // namespace mgscoop::link
