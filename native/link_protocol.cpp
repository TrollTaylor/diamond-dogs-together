// Original MGS Co-op link v2 wire format, estimators and reliable channel
// (docs/link-protocol.md).
#include "mgscoop/link_protocol.hpp"

#include <cstring>

namespace mgscoop::link {
namespace {
void PutU16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void PutU32(std::uint8_t* p, std::uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void PutU64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint16_t GetU16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}
std::uint32_t GetU32(const std::uint8_t* p) noexcept {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    return v;
}
std::uint64_t GetU64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}
void CopyBuild(char* out, const std::uint8_t* in) noexcept {
    std::memcpy(out, in, kBuildSize);
    // Received builds are logged: keep printable ASCII only, NUL-padded.
    bool ended = false;
    for (std::size_t i = 0; i < kBuildSize; ++i) {
        if (!out[i]) ended = true;
        if (ended || static_cast<unsigned char>(out[i]) < 0x20 || static_cast<unsigned char>(out[i]) > 0x7e || out[i] == '"' ||
            out[i] == '\\' || out[i] == ';')
            out[i] = ended ? 0 : '?';
    }
}
} // namespace

DecodeResult DecodeHeader(const std::uint8_t* data, std::size_t size, Header& out) noexcept {
    if (!data || size < kLinkOverhead) return DecodeResult::malformed;
    if (std::memcmp(data, kLinkMagic, 4) != 0) return DecodeResult::malformed;
    if (data[4] != kLinkVersion) return size >= kLinkMaxDatagram ? DecodeResult::version_mismatch : DecodeResult::malformed;
    if (size > kLinkMaxDatagram) return DecodeResult::malformed;
    const std::uint8_t type = data[5];
    if (type < static_cast<std::uint8_t>(PacketType::hello) || type > static_cast<std::uint8_t>(PacketType::reject))
        return DecodeResult::malformed;
    Header h{};
    h.type = static_cast<PacketType>(type);
    h.flags = GetU16(data + 6);
    if (h.flags & ~(HeaderFlag::HasAck | HeaderFlag::Unauth)) return DecodeResult::malformed;
    if ((h.flags & HeaderFlag::Unauth) && h.type != PacketType::reject) return DecodeResult::malformed;
    h.session_id = GetU64(data + 8);
    h.seq = GetU32(data + 16);
    h.ack = GetU32(data + 20);
    h.ack_bits = GetU32(data + 24);
    h.send_ms = GetU32(data + 28);
    h.ack_delay_ms = GetU16(data + 32);
    h.body_len = GetU16(data + 34);
    if (size != kLinkOverhead + h.body_len) return DecodeResult::malformed;
    out = h;
    return DecodeResult::ok;
}

void EncodeHeader(const Header& header, std::uint8_t* out) noexcept {
    std::memcpy(out, kLinkMagic, 4);
    out[4] = kLinkVersion;
    out[5] = static_cast<std::uint8_t>(header.type);
    PutU16(out + 6, header.flags);
    PutU64(out + 8, header.session_id);
    PutU32(out + 16, header.seq);
    PutU32(out + 20, header.ack);
    PutU32(out + 24, header.ack_bits);
    PutU32(out + 28, header.send_ms);
    PutU16(out + 32, header.ack_delay_ms);
    PutU16(out + 34, header.body_len);
}

void WriteTag(const HmacKey& key, std::uint8_t* datagram, std::size_t size) noexcept {
    std::uint8_t mac[kSha256Size];
    Hmac(key, datagram, size - kLinkTagSize, mac);
    std::memcpy(datagram + size - kLinkTagSize, mac, kLinkTagSize);
    SecureZero(mac, sizeof(mac));
}

bool VerifyTag(const HmacKey& key, const std::uint8_t* datagram, std::size_t size) noexcept {
    if (!key.valid || size < kLinkOverhead) return false;
    std::uint8_t mac[kSha256Size];
    Hmac(key, datagram, size - kLinkTagSize, mac);
    return ConstantTimeEqual(mac, datagram + size - kLinkTagSize, kLinkTagSize);
}

void EncodeHelloBody(const Hello& hello, std::uint8_t* body) noexcept {
    std::memset(body, 0, kHelloBodySize);
    PutU16(body, hello.proto);
    PutU16(body + 2, hello.min_proto);
    body[4] = hello.mode;
    body[5] = hello.role;
    std::memcpy(body + 8, hello.nonce_g, kNonceSize);
    std::memcpy(body + 24, hello.build, kBuildSize);
}

bool DecodeHelloBody(const std::uint8_t* body, std::size_t size, Hello& out) noexcept {
    if (!body || size != kHelloBodySize) return false;
    out.proto = GetU16(body);
    out.min_proto = GetU16(body + 2);
    out.mode = body[4];
    out.role = body[5];
    std::memcpy(out.nonce_g, body + 8, kNonceSize);
    CopyBuild(out.build, body + 24);
    return true;
}

void EncodeWelcomeBody(const Welcome& welcome, std::uint8_t* body) noexcept {
    PutU16(body, welcome.proto);
    body[2] = welcome.mode;
    body[3] = welcome.role;
    std::memcpy(body + 4, welcome.nonce_h, kNonceSize);
    std::memcpy(body + 20, welcome.echo_nonce_g, kNonceSize);
    std::memcpy(body + 36, welcome.build, kBuildSize);
}

bool DecodeWelcomeBody(const std::uint8_t* body, std::size_t size, Welcome& out) noexcept {
    if (!body || size != kWelcomeBodySize) return false;
    out.proto = GetU16(body);
    out.mode = body[2];
    out.role = body[3];
    std::memcpy(out.nonce_h, body + 4, kNonceSize);
    std::memcpy(out.echo_nonce_g, body + 20, kNonceSize);
    CopyBuild(out.build, body + 36);
    return true;
}

void EncodeRejectBody(const Reject& reject, std::uint8_t* body) noexcept {
    body[0] = reject.reason;
    body[1] = 0;
    PutU16(body + 2, reject.proto);
    std::memcpy(body + 4, reject.build, kBuildSize);
    std::memcpy(body + 20, reject.echo_nonce_g, kNonceSize);
}

bool DecodeRejectBody(const std::uint8_t* body, std::size_t size, Reject& out) noexcept {
    if (!body || size != kRejectBodySize) return false;
    out.reason = body[0];
    out.proto = GetU16(body + 2);
    CopyBuild(out.build, body + 4);
    std::memcpy(out.echo_nonce_g, body + 20, kNonceSize);
    return true;
}

void FormatAddress(std::uint32_t address, char (&out)[16]) noexcept {
    std::size_t at = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const unsigned octet = (address >> shift) & 0xFFu;
        if (octet >= 100) out[at++] = static_cast<char>('0' + octet / 100);
        if (octet >= 10) out[at++] = static_cast<char>('0' + (octet / 10) % 10);
        out[at++] = static_cast<char>('0' + octet % 10);
        if (shift) out[at++] = '.';
    }
    out[at] = 0;
}

void MaskAddress(std::uint32_t address, char (&out)[16]) noexcept {
    const unsigned a = address >> 24, b = (address >> 16) & 0xFFu;
    const bool shown = a == 127 || a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
                       (a == 100 && b >= 64 && b <= 127) || a == 26;
    FormatAddress(address, out);
    if (shown) return;
    // Keep the first two octets: "a.b.x.x".
    std::size_t at = 0, dots = 0;
    while (out[at] && dots < 2) {
        if (out[at] == '.') ++dots;
        ++at;
    }
    out[at++] = 'x';
    out[at++] = '.';
    out[at++] = 'x';
    out[at] = 0;
}

bool ValidateBody(const std::uint8_t* body, std::size_t size, unsigned& messages) noexcept {
    messages = 0;
    if (size && !body) return false;
    std::size_t at = 0;
    while (at < size) {
        if (size - at < kMsgHeaderSize) return false;
        if (body[at + 1] != 0) return false;
        const std::size_t length = GetU16(body + at + 2);
        if (size - at - kMsgHeaderSize < length) return false;
        at += kMsgHeaderSize + length;
        ++messages;
    }
    return true;
}

bool MessageReader::Next(Message& out) noexcept {
    if (at_ + kMsgHeaderSize > size_) return false;
    const std::uint16_t length = GetU16(body_ + at_ + 2);
    if (size_ - at_ - kMsgHeaderSize < length) return false;
    out.type = body_[at_];
    out.data = body_ + at_ + kMsgHeaderSize;
    out.size = length;
    at_ += kMsgHeaderSize + length;
    return true;
}

bool BodyWriter::Add(MsgType type, const void* data, std::size_t size) noexcept {
    if (size > 0xFFFF || kMsgHeaderSize + size > remaining() || (size && !data)) return false;
    std::uint8_t* p = body_ + used_;
    p[0] = static_cast<std::uint8_t>(type);
    p[1] = 0;
    PutU16(p + 2, static_cast<std::uint16_t>(size));
    if (size) std::memcpy(p + kMsgHeaderSize, data, size);
    used_ += kMsgHeaderSize + size;
    return true;
}

bool BodyWriter::AddReliable(std::uint8_t channel, std::uint16_t id, const void* payload, std::size_t size) noexcept {
    const std::size_t total = kReliableHeaderSize + size;
    if (size > kMaxReliableMessage || kMsgHeaderSize + total > remaining() || (size && !payload)) return false;
    std::uint8_t* p = body_ + used_;
    p[0] = static_cast<std::uint8_t>(MsgType::reliable);
    p[1] = 0;
    PutU16(p + 2, static_cast<std::uint16_t>(total));
    p[4] = channel;
    p[5] = 0;
    PutU16(p + 6, id);
    if (size) std::memcpy(p + 8, payload, size);
    used_ += kMsgHeaderSize + total;
    return true;
}

bool BodyWriter::AddRelAck(std::uint8_t channel, std::uint8_t free_slots, std::uint16_t next_expected, std::uint32_t bits) noexcept {
    std::uint8_t m[kRelAckSize];
    m[0] = channel;
    m[1] = free_slots;
    PutU16(m + 2, next_expected);
    PutU32(m + 4, bits);
    return Add(MsgType::relack, m, sizeof(m));
}

bool DecodeReliable(const Message& message, ReliableMsg& out) noexcept {
    if (message.type != static_cast<std::uint8_t>(MsgType::reliable) || message.size < kReliableHeaderSize ||
        message.size - kReliableHeaderSize > kMaxReliableMessage || message.data[1] != 0)
        return false;
    out.channel = message.data[0];
    out.id = GetU16(message.data + 2);
    out.payload = message.data + kReliableHeaderSize;
    out.size = message.size - kReliableHeaderSize;
    return true;
}

bool DecodeRelAck(const Message& message, RelAckMsg& out) noexcept {
    if (message.type != static_cast<std::uint8_t>(MsgType::relack) || message.size != kRelAckSize) return false;
    out.channel = message.data[0];
    out.free_slots = message.data[1];
    out.next_expected = GetU16(message.data + 2);
    out.bits = GetU32(message.data + 4);
    return out.free_slots <= kReliableWindow;
}

// --- ReplayWindow
bool ReplayWindow::Fresh(std::uint32_t seq) const noexcept {
    if (!seq) return false;
    if (!any_ || seq > highest_) return true;
    if (highest_ - seq >= kBits) return false;
    return !Test(seq);
}

void ReplayWindow::Mark(std::uint32_t seq) noexcept {
    if (!seq) return;
    if (!any_) {
        for (auto& word : bits_) word = 0;
        highest_ = seq;
        any_ = true;
    } else if (seq > highest_) {
        const std::uint32_t advance = seq - highest_;
        if (advance >= kBits) {
            for (auto& word : bits_) word = 0;
        } else {
            for (std::uint32_t s = highest_ + 1; s != seq + 1; ++s) bits_[(s % kBits) / 32] &= ~(1u << (s % 32));
        }
        highest_ = seq;
    } else if (highest_ - seq >= kBits) {
        return;
    }
    bits_[(seq % kBits) / 32] |= 1u << (seq % 32);
}

void ReplayWindow::Reset() noexcept { *this = ReplayWindow{}; }

std::uint32_t ReplayWindow::AckBits() const noexcept {
    if (!any_) return 0;
    std::uint32_t out = 0;
    for (std::uint32_t i = 0; i < 32; ++i) {
        if (highest_ < i + 2) break;
        if (Test(highest_ - 1 - i)) out |= 1u << i;
    }
    return out;
}

// --- SentRing
void SentRing::Record(std::uint32_t seq, std::uint32_t send_ms) noexcept {
    entries_[seq % kSize] = Entry{seq, send_ms, true};
}

bool SentRing::Take(std::uint32_t seq, std::uint32_t& send_ms) noexcept {
    Entry& e = entries_[seq % kSize];
    if (!e.valid || e.seq != seq) return false;
    e.valid = false;
    send_ms = e.send_ms;
    return true;
}

void SentRing::Reset() noexcept {
    for (auto& e : entries_) e = Entry{};
}

// --- RttEstimator
void RttEstimator::Sample(std::uint32_t rtt_ms) noexcept {
    const std::int64_t s = rtt_ms > 60000 ? 60000 : rtt_ms;
    if (!have_) {
        have_ = true;
        srtt8_ = s * 8;
        rttvar4_ = s * 2;  // rttvar = s/2
        return;
    }
    // RFC 6298 order: RTTVAR with the old SRTT, then SRTT.
    const std::int64_t srtt = srtt8_ / 8;
    const std::int64_t delta = s > srtt ? s - srtt : srtt - s;
    rttvar4_ += delta - rttvar4_ / 4;
    srtt8_ += s - srtt8_ / 8;
}

std::uint32_t RttEstimator::rto_ms() const noexcept {
    if (!have_) return 250;
    const std::int64_t rto = srtt8_ / 8 + rttvar4_;  // srtt + 4 * rttvar
    return static_cast<std::uint32_t>(rto < 100 ? 100 : rto > kMaxRtoMs ? kMaxRtoMs : rto);
}

// --- LossMeter
void LossMeter::Rotate(std::uint64_t now_ms) noexcept {
    if (!started_) {
        started_ = true;
        current_ = Bucket{now_ms, 0, 0};
        previous_ = Bucket{};
        return;
    }
    if (now_ms - current_.start_ms < kBucketMs) return;
    if (now_ms - current_.start_ms >= 2 * kBucketMs) previous_ = Bucket{};
    else previous_ = current_;
    current_ = Bucket{now_ms, 0, 0};
}

void LossMeter::OnAccepted(std::uint32_t seq, std::uint32_t highest_before, bool had_any, std::uint64_t now_ms) noexcept {
    Rotate(now_ms);
    ++current_.received;
    if (!had_any) return;
    if (seq > highest_before) {
        const std::uint32_t gap = seq - highest_before - 1;
        current_.lost += gap > 1000 ? 1000 : gap;
    } else if (current_.lost) {
        --current_.lost;  // a late (reordered) packet that was counted as lost
    } else if (previous_.lost) {
        --previous_.lost;
    }
}

std::uint32_t LossMeter::Permille(std::uint64_t now_ms) noexcept {
    Rotate(now_ms);
    const std::uint64_t received = static_cast<std::uint64_t>(current_.received) + previous_.received;
    const std::uint64_t lost = static_cast<std::uint64_t>(current_.lost) + previous_.lost;
    if (!received && !lost) return 0;
    return static_cast<std::uint32_t>(lost * 1000 / (received + lost));
}

// --- JitterMeter
void JitterMeter::OnPacket(std::uint32_t rx_ms, std::uint32_t send_ms, std::uint64_t now_ms) noexcept {
    const auto transit = static_cast<std::int32_t>(rx_ms - send_ms);
    if (have_) {
        const std::int64_t d = static_cast<std::int64_t>(transit) - last_transit_;
        const std::uint32_t magnitude = static_cast<std::uint32_t>(d < 0 ? (-d > 100000 ? 100000 : -d) : (d > 100000 ? 100000 : d));
        jitter16_ = jitter16_ + magnitude - ((jitter16_ + 8) >> 4);
    }
    have_ = true;
    last_transit_ = transit;
    if (!have_current_ && !have_previous_) bucket_start_ = now_ms;
    if (now_ms - bucket_start_ >= LossMeter::kBucketMs) {
        have_previous_ = have_current_ && now_ms - bucket_start_ < 2 * LossMeter::kBucketMs;
        min_previous_ = min_current_;
        have_current_ = false;
        bucket_start_ = now_ms;
    }
    if (!have_current_ || transit < min_current_) min_current_ = transit;
    have_current_ = true;
}

std::int32_t JitterMeter::offset_ms() const noexcept {
    if (have_current_ && have_previous_) return min_current_ < min_previous_ ? min_current_ : min_previous_;
    return have_current_ ? min_current_ : have_previous_ ? min_previous_ : 0;
}

// --- InterpController
void InterpController::Init(std::uint32_t min_ms, std::uint32_t max_ms) noexcept {
    *this = InterpController{};
    min_ = min_ms;
    max_ = max_ms < min_ms ? min_ms : max_ms;
    const std::uint32_t initial = 100 < min_ ? min_ : 100 > max_ ? max_ : 100;
    goal_ = applied_ = target_ = initial;
}

void InterpController::Update(std::uint64_t now_ms, std::uint32_t rate_hz, std::uint32_t jitter_ms,
                              std::uint32_t loss_permille) noexcept {
    const std::uint32_t frame_ms = 1000 / (rate_hz ? rate_hz : 1);
    if (loss_level_ == 0 && loss_permille >= 20) loss_level_ = 1;
    if (loss_level_ == 1 && loss_permille >= 60) loss_level_ = 2;
    if (loss_level_ == 2 && loss_permille < 40) loss_level_ = 1;
    if (loss_level_ == 1 && loss_permille < 10) loss_level_ = 0;
    const std::uint64_t raw = static_cast<std::uint64_t>(frame_ms) + 2ull * (jitter_ms > 10000 ? 10000 : jitter_ms) +
                              static_cast<std::uint64_t>(loss_level_) * frame_ms + 10;
    target_ = raw < min_ ? min_ : raw > max_ ? max_ : static_cast<std::uint32_t>(raw);
    if (target_ > goal_) {
        goal_ = target_;
        lowering_ = false;
    } else if (target_ + kDownMarginMs < goal_) {
        if (!lowering_) {
            lowering_ = true;
            lowering_since_ = now_ms;
        } else if (now_ms - lowering_since_ >= kHoldDownMs) {
            goal_ = target_;
            lowering_ = false;
        }
    } else {
        lowering_ = false;
    }
}

std::uint32_t InterpController::Tick() noexcept {
    if (applied_ < goal_) ++applied_;
    else if (applied_ > goal_) --applied_;
    return applied_;
}

// --- Reliable channel
const char* ReliableResultName(ReliableResult result) noexcept {
    switch (result) {
    case ReliableResult::replaced: return "replaced";
    case ReliableResult::full: return "full";
    case ReliableResult::not_connected: return "not_connected";
    case ReliableResult::too_large: return "too_large";
    case ReliableResult::bad_channel: return "bad_channel";
    default: return "queued";
    }
}

ReliableSender::Slot* ReliableSender::Find(std::uint16_t id) noexcept {
    Slot& slot = slots_[id % kReliableWindow];
    return slot.used && slot.id == id ? &slot : nullptr;
}

const ReliableSender::Slot* ReliableSender::Find(std::uint16_t id) const noexcept {
    const Slot& slot = slots_[id % kReliableWindow];
    return slot.used && slot.id == id ? &slot : nullptr;
}

ReliableResult ReliableSender::Queue(const void* bytes, std::size_t size, ReliableMode mode) noexcept {
    if (size > kMaxReliableMessage) return ReliableResult::too_large;
    if (size && !bytes) return ReliableResult::too_large;
    if (mode == ReliableMode::latest && count_) {
        Slot* newest = Find(static_cast<std::uint16_t>(next_id_ - 1));
        if (newest && newest->sends == 0) {
            if (size) std::memcpy(newest->bytes, bytes, size);
            newest->size = static_cast<std::uint16_t>(size);
            return ReliableResult::replaced;
        }
    }
    if (IdDistance(base_, next_id_) >= kReliableWindow) return ReliableResult::full;
    Slot& slot = slots_[next_id_ % kReliableWindow];
    if (slot.used) return ReliableResult::full;
    slot.used = true;
    slot.id = next_id_;
    slot.size = static_cast<std::uint16_t>(size);
    slot.sends = 0;
    slot.last_sent_ms = 0;
    if (size) std::memcpy(slot.bytes, bytes, size);
    if (!count_) base_ = next_id_;
    ++count_;
    next_id_ = static_cast<std::uint16_t>(next_id_ + 1);
    return ReliableResult::queued;
}

unsigned ReliableSender::OnRelAck(std::uint16_t next_expected, std::uint32_t bits, std::uint8_t free_slots,
                                  std::uint64_t) noexcept {
    peer_known_ = true;
    peer_free_ = free_slots > kReliableWindow ? static_cast<std::uint8_t>(kReliableWindow) : free_slots;
    unsigned freed = 0;
    for (auto& slot : slots_) {
        if (!slot.used || slot.sends == 0) continue;  // never acknowledge what was never sent
        const std::uint16_t below = IdDistance(slot.id, next_expected);
        bool acked = below != 0 && below < 0x8000;
        if (!acked) {
            const std::uint16_t above = IdDistance(static_cast<std::uint16_t>(next_expected + 1), slot.id);
            acked = above < 32 && ((bits >> above) & 1u);
        }
        if (acked) {
            slot.used = false;
            --count_;
            ++freed;
        }
    }
    if (freed) {
        base_ = next_id_;
        for (std::uint16_t id = static_cast<std::uint16_t>(next_id_ - kReliableWindow); id != next_id_; ++id)
            if (Find(id)) { base_ = id; break; }
    }
    return freed;
}

bool ReliableSender::NextDue(std::uint64_t now_ms, std::uint32_t rto_ms, std::size_t budget, Pick& out) const noexcept {
    if (!count_) return false;
    const bool zero_window = peer_known_ && peer_free_ == 0;
    if (zero_window) {
        // One probe per second: the oldest message, counted as a retransmit.
        if (last_probe_ms_ && now_ms - last_probe_ms_ < kZeroWindowProbeMs) return false;
        const Slot* oldest = Find(base_);
        if (!oldest || oldest->size > budget) return false;
        out = Pick{oldest->id, oldest->bytes, oldest->size, oldest->sends != 0};
        return true;
    }
    // First transmissions, in id order.
    for (std::uint16_t id = base_; id != next_id_; ++id) {
        const Slot* slot = Find(id);
        if (slot && slot->sends == 0) {
            out = Pick{slot->id, slot->bytes, slot->size, false};
            return true;
        }
    }
    // Retransmissions: each at most once per (backed-off) RTO, within budget.
    for (std::uint16_t id = base_; id != next_id_; ++id) {
        const Slot* slot = Find(id);
        if (!slot) continue;
        std::uint64_t timeout = rto_ms ? rto_ms : 250;
        for (unsigned i = 1; i < slot->sends && timeout < kMaxRtoMs; ++i) timeout *= 2;
        if (timeout > kMaxRtoMs) timeout = kMaxRtoMs;
        if (now_ms - slot->last_sent_ms < timeout) continue;
        if (slot->size > budget) return false;
        out = Pick{slot->id, slot->bytes, slot->size, true};
        return true;
    }
    return false;
}

void ReliableSender::MarkSent(std::uint16_t id, std::uint64_t now_ms) noexcept {
    Slot* slot = Find(id);
    if (!slot) return;
    if (slot->sends) ++retransmits_;
    if (slot->sends < 0xFF) ++slot->sends;
    slot->last_sent_ms = now_ms;
    if (peer_known_ && peer_free_ == 0) last_probe_ms_ = now_ms;
}

void ReliableSender::Reset() noexcept {
    for (auto& slot : slots_) {
        slot.used = false;
        slot.sends = 0;
        slot.size = 0;
    }
    base_ = next_id_ = 0;
    count_ = 0;
    peer_known_ = false;
    peer_free_ = kReliableWindow;
    last_probe_ms_ = 0;
}

bool ReliableReceiver::Accept(std::uint16_t id, const std::uint8_t* payload, std::size_t size, std::uint64_t now_ms) noexcept {
    SetDirty(now_ms);
    if (size > kMaxReliableMessage || (size && !payload)) return false;
    if (IdDistance(drain_head_, id) >= kReliableWindow) return false;  // already drained, or beyond the window
    Slot& slot = slots_[id % kReliableWindow];
    if (slot.stored) return false;  // duplicate
    slot.stored = true;
    slot.size = static_cast<std::uint16_t>(size);
    if (size) std::memcpy(slot.bytes, payload, size);
    ++stored_;
    return true;
}

bool ReliableReceiver::Drain(std::uint8_t* out, std::size_t capacity, std::size_t& size, std::uint64_t now_ms) noexcept {
    size = 0;
    Slot& slot = slots_[drain_head_ % kReliableWindow];
    if (!slot.stored || slot.size > capacity || (slot.size && !out)) return false;
    if (slot.size) std::memcpy(out, slot.bytes, slot.size);
    size = slot.size;
    slot.stored = false;
    slot.size = 0;
    --stored_;
    drain_head_ = static_cast<std::uint16_t>(drain_head_ + 1);
    SetDirty(now_ms);  // the free-slot count changed
    return true;
}

void ReliableReceiver::AckState(std::uint16_t& next_expected, std::uint32_t& bits, std::uint8_t& free_slots) const noexcept {
    std::uint16_t next = drain_head_;
    while (IdDistance(drain_head_, next) < kReliableWindow && slots_[next % kReliableWindow].stored)
        next = static_cast<std::uint16_t>(next + 1);
    std::uint32_t out = 0;
    for (std::uint32_t i = 0; i < 32; ++i) {
        const auto id = static_cast<std::uint16_t>(next + 1 + i);
        if (IdDistance(drain_head_, id) >= kReliableWindow) break;
        if (slots_[id % kReliableWindow].stored) out |= 1u << i;
    }
    next_expected = next;
    bits = out;
    free_slots = static_cast<std::uint8_t>(kReliableWindow - stored_);
}

void ReliableReceiver::SetDirty(std::uint64_t now_ms) noexcept {
    if (!dirty_) dirty_since_ = now_ms;
    dirty_ = true;
}

void ReliableReceiver::Reset() noexcept {
    for (auto& slot : slots_) {
        slot.stored = false;
        slot.size = 0;
    }
    drain_head_ = 0;
    stored_ = 0;
    dirty_ = false;
    dirty_since_ = 0;
}

bool PackBody(BodyWriter& writer, const std::uint8_t* frame, std::size_t frame_size, const RelAckState* acks,
              ReliableSender* senders, std::uint64_t now_ms, std::uint32_t rto_ms, std::size_t& retransmit_budget) noexcept {
    if (frame && !writer.Add(MsgType::frame, frame, frame_size)) return false;
    for (unsigned ch = 1; acks && ch < kReliableChannels; ++ch) {
        const RelAckState& a = acks[ch];
        if (a.due) writer.AddRelAck(static_cast<std::uint8_t>(ch), a.free_slots, a.next_expected, a.bits);
    }
    for (unsigned ch = 1; senders && ch < kReliableChannels; ++ch) {
        ReliableSender& tx = senders[ch];
        for (unsigned guard = 0; guard < kReliableWindow; ++guard) {
            const std::size_t overhead = kMsgHeaderSize + kReliableHeaderSize;
            const std::size_t room = writer.remaining() > overhead ? writer.remaining() - overhead : 0;
            const std::size_t budget = room < retransmit_budget ? room : retransmit_budget;
            ReliableSender::Pick pick{};
            // A first transmission is offered whatever the budget; it must still fit.
            if (!tx.NextDue(now_ms, rto_ms, budget, pick)) break;
            if (pick.size > room || !writer.AddReliable(static_cast<std::uint8_t>(ch), pick.id, pick.payload, pick.size)) break;
            tx.MarkSent(pick.id, now_ms);
            if (pick.retransmit) retransmit_budget -= pick.size;
        }
    }
    return true;
}

bool ReliableDue(const ReliableSender* senders, std::uint64_t now_ms, std::uint32_t rto_ms, std::size_t retransmit_budget) noexcept {
    for (unsigned ch = 1; senders && ch < kReliableChannels; ++ch) {
        ReliableSender::Pick pick{};
        if (senders[ch].NextDue(now_ms, rto_ms, retransmit_budget, pick)) return true;
    }
    return false;
}

} // namespace mgscoop::link
