#pragma once
// Two-PC link v2 wire format and the pure helpers around it
// (docs/link-protocol.md): the 36-byte authenticated header, the message
// framing inside DATA, the 1024-bit anti-replay window, the RTT / jitter /
// inbound-loss estimators, the adaptive interpolation controller and the
// reliable ordered channel. Portable and allocation-free; every time is passed
// in. Nothing here is thread safe: partner_net serializes (g_tx_lock for send
// state, g_lock for receive state).
//
// Datagram (little-endian):
//   'MGSL' | u8 version 2 | u8 type | u16 flags | u64 session_id | u32 seq |
//   u32 ack | u32 ack_bits | u32 send_ms | u16 ack_delay_ms | u16 body_len |
//   body[body_len] | tag[16] = HMAC-SHA256(key, everything before the tag)[0..16)
#include <cstddef>
#include <cstdint>
#include "mgscoop/link_crypto.hpp"

namespace mgscoop::link {

inline constexpr std::uint8_t kLinkMagic[4] = {'M', 'G', 'S', 'L'};
inline constexpr std::uint8_t kLinkVersion = 2;
inline constexpr std::uint16_t kLinkProto = 2;
inline constexpr std::size_t kLinkHeaderSize = 36;
inline constexpr std::size_t kLinkTagSize = 16;
inline constexpr std::size_t kLinkOverhead = kLinkHeaderSize + kLinkTagSize;  // 52
inline constexpr std::size_t kLinkMaxDatagram = 1200;
inline constexpr std::size_t kLinkMaxBody = kLinkMaxDatagram - kLinkOverhead;  // 1148
// Neither side sends DATA at or above this sequence (no wrap; a new
// handshake starts a new session).
inline constexpr std::uint32_t kSeqLimit = 0xFFFFFF00u;

enum class PacketType : std::uint8_t { hello = 1, welcome = 2, confirm = 3, data = 4, reject = 5 };
namespace HeaderFlag {
inline constexpr std::uint16_t HasAck = 0x1;
inline constexpr std::uint16_t Unauth = 0x2;  // REJECT only; the tag is 16 zero bytes
}

struct Header {
    PacketType type = PacketType::data;
    std::uint16_t flags = 0;
    std::uint64_t session_id = 0;
    std::uint32_t seq = 0;
    std::uint32_t ack = 0;
    std::uint32_t ack_bits = 0;
    std::uint32_t send_ms = 0;
    std::uint16_t ack_delay_ms = 0;
    std::uint16_t body_len = 0;
};

// Strict decode: size in [52, 1200], size == 52 + body_len, magic, version 2,
// a known type, reserved flag bits zero, UNAUTH only on REJECT. A datagram
// with the magic and another version is version_mismatch only when it is at
// least kLinkMaxDatagram bytes (answering anything smaller could amplify);
// everything else is malformed.
enum class DecodeResult : std::uint8_t { ok, malformed, version_mismatch };
DecodeResult DecodeHeader(const std::uint8_t* data, std::size_t size, Header& out) noexcept;
// Writes the 36 header bytes (out must hold kLinkHeaderSize).
void EncodeHeader(const Header& header, std::uint8_t* out) noexcept;
// Tag over datagram[0 .. size-16) written into / compared with the last 16 bytes.
void WriteTag(const HmacKey& key, std::uint8_t* datagram, std::size_t size) noexcept;
bool VerifyTag(const HmacKey& key, const std::uint8_t* datagram, std::size_t size) noexcept;

// Handshake bodies. Roles: 1 host, 2 guest. Modes: 1 lab, 2 play.
inline constexpr std::uint8_t kRoleHost = 1, kRoleGuest = 2;
inline constexpr std::uint8_t kModeLab = 1, kModePlay = 2;
inline constexpr std::size_t kBuildSize = 16;
// HELLO: u16 proto | u16 min_proto | u8 mode | u8 role | u16 0 | nonce_g[16] |
// build[16] | zero pad; the datagram is always exactly kLinkMaxDatagram bytes.
inline constexpr std::size_t kHelloBodySize = kLinkMaxBody;
struct Hello {
    std::uint16_t proto, min_proto;
    std::uint8_t mode, role;
    std::uint8_t nonce_g[kNonceSize];
    char build[kBuildSize];
};
// WELCOME (104 bytes): u16 proto | u8 mode | u8 role | nonce_h[16] | echo_nonce_g[16] | build[16].
inline constexpr std::size_t kWelcomeBodySize = 52;
struct Welcome {
    std::uint16_t proto;
    std::uint8_t mode, role;
    std::uint8_t nonce_h[kNonceSize];
    std::uint8_t echo_nonce_g[kNonceSize];
    char build[kBuildSize];
};
// REJECT (88 bytes): u8 reason | u8 0 | u16 proto | build[16] | echo_nonce_g[16].
inline constexpr std::size_t kRejectBodySize = 36;
struct Reject {
    std::uint8_t reason;
    std::uint16_t proto;
    char build[kBuildSize];
    std::uint8_t echo_nonce_g[kNonceSize];
};
void EncodeHelloBody(const Hello& hello, std::uint8_t* body) noexcept;      // kHelloBodySize bytes
bool DecodeHelloBody(const std::uint8_t* body, std::size_t size, Hello& out) noexcept;
void EncodeWelcomeBody(const Welcome& welcome, std::uint8_t* body) noexcept;  // kWelcomeBodySize bytes
bool DecodeWelcomeBody(const std::uint8_t* body, std::size_t size, Welcome& out) noexcept;
void EncodeRejectBody(const Reject& reject, std::uint8_t* body) noexcept;    // kRejectBodySize bytes
bool DecodeRejectBody(const std::uint8_t* body, std::size_t size, Reject& out) noexcept;

// Log/status masking of an IPv4 address (host byte order): the full dotted
// address for 127/8, 10/8, 172.16/12, 192.168/16, 100.64/10 (CGNAT, Tailscale)
// and 26/8 (Radmin), "a.b.x.x" for anything else.
void MaskAddress(std::uint32_t address, char (&out)[16]) noexcept;
// "a.b.c.d" (no masking).
void FormatAddress(std::uint32_t address, char (&out)[16]) noexcept;

// --- Messages inside a DATA body: u8 type | u8 flags (0) | u16 len | len bytes.
enum class MsgType : std::uint8_t { frame = 1, reliable = 2, close = 3, relack = 4 };
inline constexpr std::size_t kMsgHeaderSize = 4;
struct Message {
    std::uint8_t type;
    const std::uint8_t* data;
    std::uint16_t size;
};
// Whole-body validation: a length overrun or nonzero message flags make the
// packet malformed (dropped whole). Returns the number of messages.
bool ValidateBody(const std::uint8_t* body, std::size_t size, unsigned& messages) noexcept;
// Iterates a validated body.
class MessageReader {
public:
    MessageReader(const std::uint8_t* body, std::size_t size) noexcept : body_(body), size_(size) {}
    bool Next(Message& out) noexcept;

private:
    const std::uint8_t* body_;
    std::size_t size_;
    std::size_t at_ = 0;
};
// Appends messages to a caller buffer of at most kLinkMaxBody bytes.
class BodyWriter {
public:
    BodyWriter(std::uint8_t* body, std::size_t capacity) noexcept : body_(body), capacity_(capacity) {}
    bool Add(MsgType type, const void* data, std::size_t size) noexcept;
    // RELIABLE: u8 channel | u8 0 | u16 msg_id | payload.
    bool AddReliable(std::uint8_t channel, std::uint16_t id, const void* payload, std::size_t size) noexcept;
    bool AddRelAck(std::uint8_t channel, std::uint8_t free_slots, std::uint16_t next_expected, std::uint32_t bits) noexcept;
    std::size_t size() const noexcept { return used_; }
    std::size_t remaining() const noexcept { return capacity_ - used_; }

private:
    std::uint8_t* body_;
    std::size_t capacity_;
    std::size_t used_ = 0;
};
inline constexpr std::size_t kReliableHeaderSize = 4;
inline constexpr std::size_t kRelAckSize = 8;
struct ReliableMsg {
    std::uint8_t channel;
    std::uint16_t id;
    const std::uint8_t* payload;
    std::size_t size;
};
struct RelAckMsg {
    std::uint8_t channel, free_slots;
    std::uint16_t next_expected;
    std::uint32_t bits;
};
bool DecodeReliable(const Message& message, ReliableMsg& out) noexcept;
bool DecodeRelAck(const Message& message, RelAckMsg& out) noexcept;

// --- Anti-replay: 1024 bits over the u32 sequence (no wrap; seq 0 invalid).
class ReplayWindow {
public:
    static constexpr std::uint32_t kBits = 1024;
    // True when seq is new and not older than highest - 1023.
    bool Fresh(std::uint32_t seq) const noexcept;
    void Mark(std::uint32_t seq) noexcept;
    void Reset() noexcept;
    bool any() const noexcept { return any_; }
    std::uint32_t highest() const noexcept { return highest_; }
    // bit i set => seq highest-1-i received (packet ack_bits).
    std::uint32_t AckBits() const noexcept;

private:
    bool Test(std::uint32_t seq) const noexcept { return (bits_[(seq % kBits) / 32] >> (seq % 32)) & 1u; }
    std::uint32_t bits_[kBits / 32]{};
    std::uint32_t highest_ = 0;
    bool any_ = false;
};

// --- Timing (docs/link-protocol.md "Timing").
// Send times of our own sequences, for RTT samples from the peer's ack.
class SentRing {
public:
    static constexpr unsigned kSize = 256;
    void Record(std::uint32_t seq, std::uint32_t send_ms) noexcept;
    // The send time of `seq` if still recorded and not sampled yet (marks it sampled).
    bool Take(std::uint32_t seq, std::uint32_t& send_ms) noexcept;
    void Reset() noexcept;

private:
    struct Entry { std::uint32_t seq, send_ms; bool valid; };
    Entry entries_[kSize]{};
};

// RFC 6298 smoothing; RTO = clamp(srtt + 4 rttvar, 100, 2000), 250 before the first sample.
class RttEstimator {
public:
    void Sample(std::uint32_t rtt_ms) noexcept;
    bool have() const noexcept { return have_; }
    std::uint32_t srtt_ms() const noexcept { return static_cast<std::uint32_t>(srtt8_ / 8); }
    std::uint32_t rttvar_ms() const noexcept { return static_cast<std::uint32_t>(rttvar4_ / 4); }
    std::uint32_t rto_ms() const noexcept;
    void Reset() noexcept { *this = RttEstimator{}; }

private:
    bool have_ = false;
    std::int64_t srtt8_ = 0;    // srtt x 8
    std::int64_t rttvar4_ = 0;  // rttvar x 4
};

// Inbound loss from DATA sequence gaps over two 5-s buckets.
class LossMeter {
public:
    static constexpr std::uint64_t kBucketMs = 5000;
    // seq accepted (fresh); `highest_before` is the window's highest before the mark (0 = none).
    void OnAccepted(std::uint32_t seq, std::uint32_t highest_before, bool had_any, std::uint64_t now_ms) noexcept;
    std::uint32_t Permille(std::uint64_t now_ms) noexcept;
    void Reset() noexcept { *this = LossMeter{}; }

private:
    struct Bucket { std::uint64_t start_ms; std::uint32_t received, lost; };
    void Rotate(std::uint64_t now_ms) noexcept;
    Bucket current_{}, previous_{};
    bool started_ = false;
};

// RFC 3550 interarrival jitter (x16 fixed point) and the minimum transit
// (clock offset, diagnostic) over two 5-s buckets. Fed with transform-carrying
// DATA only.
class JitterMeter {
public:
    void OnPacket(std::uint32_t rx_ms, std::uint32_t send_ms, std::uint64_t now_ms) noexcept;
    std::uint32_t jitter_ms() const noexcept { return jitter16_ >> 4; }
    bool have_offset() const noexcept { return have_current_ || have_previous_; }
    std::int32_t offset_ms() const noexcept;
    void Reset() noexcept { *this = JitterMeter{}; }

private:
    bool have_ = false;
    std::int32_t last_transit_ = 0;
    std::uint32_t jitter16_ = 0;
    std::uint64_t bucket_start_ = 0;
    bool have_current_ = false, have_previous_ = false;
    std::int32_t min_current_ = 0, min_previous_ = 0;
};

// Adaptive interpolation delay (partner_interp=auto): Update once per second,
// Tick every 20 ms (the applied delay moves at most 1 ms per tick).
class InterpController {
public:
    static constexpr std::uint64_t kHoldDownMs = 5000;
    static constexpr std::uint32_t kDownMarginMs = 15;
    void Init(std::uint32_t min_ms, std::uint32_t max_ms) noexcept;
    void Update(std::uint64_t now_ms, std::uint32_t rate_hz, std::uint32_t jitter_ms, std::uint32_t loss_permille) noexcept;
    std::uint32_t Tick() noexcept;
    std::uint32_t goal() const noexcept { return goal_; }
    std::uint32_t applied() const noexcept { return applied_; }
    std::uint32_t target() const noexcept { return target_; }
    unsigned loss_level() const noexcept { return loss_level_; }

private:
    std::uint32_t min_ = 60, max_ = 350;
    std::uint32_t goal_ = 100, applied_ = 100, target_ = 100;
    unsigned loss_level_ = 0;  // 0 none, 1 one frame, 2 two frames
    bool lowering_ = false;
    std::uint64_t lowering_since_ = 0;
};

// --- Reliable ordered channel (docs/link-protocol.md "Reliable channel").
inline constexpr unsigned kReliableChannels = 4;  // 0 reserved (link), 1 world (reserved), 2..3 game
inline constexpr std::size_t kMaxReliableMessage = 1024;
inline constexpr unsigned kReliableWindow = 32;
inline constexpr std::size_t kRetransmitBudgetBytes = 2400;  // retransmitted payload per 20 ms tick
inline constexpr std::uint64_t kZeroWindowProbeMs = 1000;
inline constexpr std::uint32_t kMaxRtoMs = 2000;

enum class ReliableResult : std::uint8_t { queued, replaced, full, not_connected, too_large, bad_channel };
enum class ReliableMode : std::uint8_t { append, latest };  // latest: replace the newest still-unsent message
const char* ReliableResultName(ReliableResult result) noexcept;

// Wrap-safe u16 message-id distance (b - a).
inline std::uint16_t IdDistance(std::uint16_t a, std::uint16_t b) noexcept { return static_cast<std::uint16_t>(b - a); }

class ReliableSender {
public:
    ReliableResult Queue(const void* bytes, std::size_t size, ReliableMode mode) noexcept;
    // Applies the peer's RELACK; returns the number of messages freed.
    unsigned OnRelAck(std::uint16_t next_expected, std::uint32_t bits, std::uint8_t free_slots, std::uint64_t now_ms) noexcept;
    // The next message to put on the wire at `now`: first transmissions
    // (always, in id order), then retransmissions due by `rto_ms` (x2 per
    // retry, capped) within `budget` bytes. Returns false when nothing is due.
    // `retransmit` tells whether the pick counts against the budget.
    struct Pick { std::uint16_t id; const std::uint8_t* payload; std::size_t size; bool retransmit; };
    bool NextDue(std::uint64_t now_ms, std::uint32_t rto_ms, std::size_t budget, Pick& out) const noexcept;
    // Marks a picked message as sent at `now`.
    void MarkSent(std::uint16_t id, std::uint64_t now_ms) noexcept;
    unsigned queued() const noexcept { return count_; }
    bool idle() const noexcept { return count_ == 0; }
    std::uint64_t retransmits() const noexcept { return retransmits_; }
    void Reset() noexcept;

private:
    struct Slot {
        bool used;
        std::uint16_t id;
        std::uint16_t size;
        std::uint8_t sends;
        std::uint64_t last_sent_ms;
        std::uint8_t bytes[kMaxReliableMessage];
    };
    Slot* Find(std::uint16_t id) noexcept;
    const Slot* Find(std::uint16_t id) const noexcept;
    Slot slots_[kReliableWindow]{};
    std::uint16_t base_ = 0;     // oldest unacknowledged id
    std::uint16_t next_id_ = 0;  // id of the next queued message
    unsigned count_ = 0;
    bool peer_known_ = false;    // a RELACK was seen
    std::uint8_t peer_free_ = kReliableWindow;
    std::uint64_t last_probe_ms_ = 0;
    std::uint64_t retransmits_ = 0;
};

class ReliableReceiver {
public:
    // Stores `id` if it lies in [drain_head, drain_head + 32) and its slot is
    // free (accepted means stored). Returns true when it was stored. Every
    // call marks a RELACK due (duplicates repeat the state).
    bool Accept(std::uint16_t id, const std::uint8_t* payload, std::size_t size, std::uint64_t now_ms) noexcept;
    // Copies the next in-order message out; false when none is ready (or it
    // does not fit `capacity`, in which case it stays).
    bool Drain(std::uint8_t* out, std::size_t capacity, std::size_t& size, std::uint64_t now_ms) noexcept;
    // RELACK fields: every id before next_expected is stored; bit i => id
    // next_expected+1+i stored; free_slots = window slots not holding a message.
    void AckState(std::uint16_t& next_expected, std::uint32_t& bits, std::uint8_t& free_slots) const noexcept;
    bool dirty() const noexcept { return dirty_; }
    std::uint64_t dirty_since() const noexcept { return dirty_since_; }
    void SetDirty(std::uint64_t now_ms) noexcept;
    void ClearDirty() noexcept { dirty_ = false; }
    unsigned stored() const noexcept { return stored_; }
    void Reset() noexcept;

private:
    struct Slot {
        bool stored;
        std::uint16_t size;
        std::uint8_t bytes[kMaxReliableMessage];
    };
    Slot slots_[kReliableWindow]{};
    std::uint16_t drain_head_ = 0;
    unsigned stored_ = 0;
    bool dirty_ = false;
    std::uint64_t dirty_since_ = 0;
};

// The RELACK a receiver wants to send (taken under g_lock by partner_net).
struct RelAckState {
    bool due;
    std::uint8_t free_slots;
    std::uint16_t next_expected;
    std::uint32_t bits;
};
// Fills one DATA body in the fixed order: the FRAME (when given; it must fit
// or nothing is written), the due RELACKs of channels 1..kReliableChannels-1,
// then reliable messages: first transmissions always, retransmissions within
// `retransmit_budget` (decremented). Marks what it packed as sent. Never
// exceeds the writer's capacity (kLinkMaxBody). Returns false when the frame
// does not fit.
bool PackBody(BodyWriter& writer, const std::uint8_t* frame, std::size_t frame_size, const RelAckState* acks,
              ReliableSender* senders, std::uint64_t now_ms, std::uint32_t rto_ms, std::size_t& retransmit_budget) noexcept;
// True when a sender has a message due at `now` (within the budget).
bool ReliableDue(const ReliableSender* senders, std::uint64_t now_ms, std::uint32_t rto_ms, std::size_t retransmit_budget) noexcept;

} // namespace mgscoop::link
