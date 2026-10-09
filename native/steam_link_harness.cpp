// Original MGS Co-op link v2 test end used by the Steam self-test and fixtures.
#include "mgscoop/steam_link_harness.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include "mgscoop/link_crypto.hpp"

namespace mgscoop::lab {
namespace {
SRWLOCK* Lock(void*& storage) noexcept { return reinterpret_cast<SRWLOCK*>(&storage); }
std::uint8_t Pattern(std::uint32_t index, std::size_t i) noexcept { return static_cast<std::uint8_t>(index * 7u + i * 13u + 1u); }
void Fill(std::uint32_t index, std::uint8_t* out, std::size_t size) noexcept {
    for (unsigned i = 0; i < 4; ++i) out[i] = static_cast<std::uint8_t>(index >> (8 * i));
    for (std::size_t i = 4; i < size; ++i) out[i] = Pattern(index, i);
}
bool Matches(const std::uint8_t* data, std::size_t size, std::size_t expected, std::uint32_t& index) noexcept {
    if (size != expected) return false;
    index = static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) | (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
    for (std::size_t i = 4; i < size; ++i)
        if (data[i] != Pattern(index, i)) return false;
    return true;
}
}  // namespace

bool LinkHarnessEnd::Transmit(void* self, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    auto* end = static_cast<LinkHarnessEnd*>(self);
    return end->send_ && end->send_(end->send_ctx_, to, data, size);
}

bool LinkHarnessEnd::Random(void*, std::uint8_t* out, std::size_t size) noexcept {
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

void LinkHarnessEnd::Start(link::Role role, bool play, const Endpoint& peer, const char (&secret)[link::kSecretChars], const char* build,
                           SendFn send, void* send_ctx, std::uint64_t now_ms) noexcept {
    AcquireSRWLockExclusive(Lock(lock_));
    send_ = send;
    send_ctx_ = send_ctx;
    counters_ = Counters{};
    next_rel_ = 0;
    for (auto& t : tx_) t.Reset();
    for (auto& r : rx_) r.Reset();
    link::SessionConfig config{};
    config.role = role;
    config.play = play;
    config.exact_filter = true;
    config.peer = peer;
    for (std::size_t i = 0; build && i < link::kBuildSize && build[i]; ++i) config.build[i] = build[i];
    link::DerivePsk(secret, config.psk);
    session_.Start(config, link::SessionIo{this, &LinkHarnessEnd::Transmit, &LinkHarnessEnd::Random}, now_ms);
    link::SecureZero(&config.psk, sizeof(config.psk));
    ReleaseSRWLockExclusive(Lock(lock_));
}

void LinkHarnessEnd::TakeAcks(link::RelAckState (&acks)[link::kReliableChannels], std::uint64_t now_ms, std::uint64_t min_age_ms) noexcept {
    for (auto& a : acks) a = link::RelAckState{};
    for (unsigned ch = 1; ch < link::kReliableChannels; ++ch) {
        link::ReliableReceiver& rx = rx_[ch];
        if (!rx.dirty() || now_ms - rx.dirty_since() < min_age_ms) continue;
        acks[ch].due = true;
        rx.AckState(acks[ch].next_expected, acks[ch].bits, acks[ch].free_slots);
        rx.ClearDirty();
    }
}

void LinkHarnessEnd::PopEvents() noexcept {
    link::Event e{};
    while (session_.PopEvent(e)) {
        if (e.kind == link::EventKind::rejected) {
            ++counters_.rejects;
            counters_.reject_verified = e.verified;
        }
    }
}

void LinkHarnessEnd::OnDatagram(const std::uint8_t* data, std::size_t size, const Endpoint& from, std::uint64_t now_ms) noexcept {
    AcquireSRWLockExclusive(Lock(lock_));
    const link::Delivery delivery = session_.OnDatagram(data, size, from, now_ms);
    if (delivery.new_session) {
        for (auto& t : tx_) t.Reset();
        for (auto& r : rx_) r.Reset();
        next_rel_ = 0;
        ++counters_.sessions;
    }
    unsigned messages = 0;
    if (delivery.data && link::ValidateBody(delivery.body, delivery.size, messages)) {
        link::MessageReader reader(delivery.body, delivery.size);
        link::Message m{};
        while (reader.Next(m)) {
            std::uint32_t index = 0;
            if (m.type == static_cast<std::uint8_t>(link::MsgType::frame)) {
                if (Matches(m.data, m.size, kFrameSize, index)) ++counters_.frames_rx;
                else ++counters_.frames_bad;
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::reliable)) {
                link::ReliableMsg r{};
                if (link::DecodeReliable(m, r) && r.channel >= 1 && r.channel < link::kReliableChannels)
                    rx_[r.channel].Accept(r.id, r.payload, r.size, now_ms);
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::relack)) {
                link::RelAckMsg a{};
                if (link::DecodeRelAck(m, a) && a.channel >= 1 && a.channel < link::kReliableChannels)
                    tx_[a.channel].OnRelAck(a.next_expected, a.bits, a.free_slots, now_ms);
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::close)) {
                ++counters_.closes_rx;
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::datagram)) {
                link::DatagramMsg d{};
                if (link::DecodeDatagram(m, d) && d.channel == 1) ++counters_.datagrams_rx;
            }
        }
    }
    std::uint8_t out[link::kMaxReliableMessage];
    std::size_t got = 0;
    while (rx_[2].Drain(out, sizeof(out), got, now_ms)) {
        std::uint32_t index = 0;
        if (Matches(out, got, kReliableSize, index) && index == next_rel_) {
            ++counters_.rel_rx;
            ++next_rel_;
        } else {
            ++counters_.rel_bad;
        }
    }
    PopEvents();
    ReleaseSRWLockExclusive(Lock(lock_));
}

void LinkHarnessEnd::OnIdle(std::uint64_t now_ms) noexcept {
    AcquireSRWLockExclusive(Lock(lock_));
    if (session_.CanSendData()) {
        std::size_t budget = link::kRetransmitBudgetBytes;
        const std::uint32_t rto = session_.rtt().rto_ms();
        for (unsigned packets = 0; packets < 4; ++packets) {
            const bool due = link::ReliableDue(tx_, now_ms, rto, budget);
            link::RelAckState acks[link::kReliableChannels];
            TakeAcks(acks, now_ms, due ? 0 : link::kAckDelayMs);
            link::BodyWriter writer(body_, sizeof(body_));
            link::PackBody(writer, nullptr, 0, acks, tx_, now_ms, rto, budget);
            if (!writer.size()) break;
            if (!session_.SendData(body_, writer.size(), now_ms)) break;
        }
    }
    session_.OnTimer(now_ms);
    PopEvents();
    ReleaseSRWLockExclusive(Lock(lock_));
}

bool LinkHarnessEnd::SendFrame(std::uint32_t index, std::uint64_t now_ms) noexcept {
    std::uint8_t frame[kFrameSize];
    Fill(index, frame, sizeof(frame));
    bool sent = false;
    AcquireSRWLockExclusive(Lock(lock_));
    if (session_.CanSendData()) {
        link::RelAckState acks[link::kReliableChannels];
        TakeAcks(acks, now_ms, 0);
        link::BodyWriter writer(body_, sizeof(body_));
        std::size_t no_retransmits = 0;
        if (link::PackBody(writer, frame, sizeof(frame), acks, tx_, now_ms, session_.rtt().rto_ms(), no_retransmits))
            sent = session_.SendData(body_, writer.size(), now_ms);
    }
    ReleaseSRWLockExclusive(Lock(lock_));
    return sent;
}

link::ReliableResult LinkHarnessEnd::SendReliable(std::uint8_t channel, std::uint32_t index) noexcept {
    std::uint8_t payload[kReliableSize];
    Fill(index, payload, sizeof(payload));
    AcquireSRWLockExclusive(Lock(lock_));
    const link::ReliableResult result = session_.CanSendData() && channel >= 1 && channel < link::kReliableChannels
                                            ? tx_[channel].Queue(payload, sizeof(payload), link::ReliableMode::append)
                                            : link::ReliableResult::not_connected;
    ReleaseSRWLockExclusive(Lock(lock_));
    return result;
}

bool LinkHarnessEnd::SendDatagram(std::uint8_t channel, std::uint32_t index, std::uint64_t now_ms) noexcept {
    std::uint8_t payload[32];
    Fill(index, payload, sizeof(payload));
    std::uint8_t message[link::kDatagramHeaderSize + sizeof(payload)];
    const std::size_t length = link::EncodeDatagram(channel, payload, sizeof(payload), message, sizeof(message));
    if (!length) return false;
    bool sent = false;
    AcquireSRWLockExclusive(Lock(lock_));
    if (session_.CanSendData()) {
        link::RelAckState acks[link::kReliableChannels];
        TakeAcks(acks, now_ms, 0);
        link::BodyWriter writer(body_, sizeof(body_));
        std::size_t no_retransmits = 0;
        if (link::PackBodyWith(writer, link::MsgType::datagram, message, length, acks, tx_, now_ms, session_.rtt().rto_ms(),
                               no_retransmits))
            sent = session_.SendData(body_, writer.size(), now_ms);
    }
    ReleaseSRWLockExclusive(Lock(lock_));
    return sent;
}

bool LinkHarnessEnd::SendClose(std::uint64_t now_ms) noexcept {
    bool sent = false;
    AcquireSRWLockExclusive(Lock(lock_));
    if (session_.CanSendData()) {
        link::BodyWriter writer(body_, sizeof(body_));
        if (writer.Add(link::MsgType::close, nullptr, 0)) sent = session_.SendData(body_, writer.size(), now_ms);
    }
    ReleaseSRWLockExclusive(Lock(lock_));
    return sent;
}

link::LinkState LinkHarnessEnd::State() const noexcept {
    auto& self = const_cast<LinkHarnessEnd&>(*this);
    AcquireSRWLockShared(Lock(self.lock_));
    const link::LinkState state = session_.state();
    ReleaseSRWLockShared(Lock(self.lock_));
    return state;
}

std::uint32_t LinkHarnessEnd::RttMs() const noexcept {
    auto& self = const_cast<LinkHarnessEnd&>(*this);
    AcquireSRWLockShared(Lock(self.lock_));
    const std::uint32_t rtt = session_.rtt().have() ? session_.rtt().srtt_ms() : 0;
    ReleaseSRWLockShared(Lock(self.lock_));
    return rtt;
}

std::uint32_t LinkHarnessEnd::SessionNumber() const noexcept {
    auto& self = const_cast<LinkHarnessEnd&>(*this);
    AcquireSRWLockShared(Lock(self.lock_));
    const std::uint32_t n = session_.session_number();
    ReleaseSRWLockShared(Lock(self.lock_));
    return n;
}

Endpoint LinkHarnessEnd::Peer() const noexcept {
    auto& self = const_cast<LinkHarnessEnd&>(*this);
    AcquireSRWLockShared(Lock(self.lock_));
    const Endpoint peer = session_.peer();
    ReleaseSRWLockShared(Lock(self.lock_));
    return peer;
}

LinkHarnessEnd::Counters LinkHarnessEnd::GetCounters() const noexcept {
    auto& self = const_cast<LinkHarnessEnd&>(*this);
    AcquireSRWLockShared(Lock(self.lock_));
    const Counters c = counters_;
    ReleaseSRWLockShared(Lock(self.lock_));
    return c;
}

link::SessionStats LinkHarnessEnd::Stats() const noexcept {
    auto& self = const_cast<LinkHarnessEnd&>(*this);
    AcquireSRWLockShared(Lock(self.lock_));
    const link::SessionStats s = session_.stats();
    ReleaseSRWLockShared(Lock(self.lock_));
    return s;
}

}  // namespace mgscoop::lab
