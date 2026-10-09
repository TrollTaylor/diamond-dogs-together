// Link v2 wire format and pure helpers (docs/link-protocol.md): header
// strictness, message framing, the replay window, RTT / jitter / loss
// estimators, the adaptive interpolation controller, the reliable channel
// over a seeded lossy pipe, the DATA body packer and the world-sync DATAGRAM
// message (docs/world-sync-protocol.md).
#include "mgscoop/link_protocol.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

namespace {
namespace link = mgscoop::link;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

std::vector<std::uint8_t> Datagram(link::PacketType type, std::size_t body_len, std::uint16_t flags = 0) {
    std::vector<std::uint8_t> d(link::kLinkOverhead + body_len, 0);
    link::Header h{};
    h.type = type;
    h.flags = flags;
    h.body_len = static_cast<std::uint16_t>(body_len);
    link::EncodeHeader(h, d.data());
    return d;
}

void HeaderStrictness() {
    link::Header h{};
    auto d = Datagram(link::PacketType::data, 0);
    Check(d.size() == 52 && link::DecodeHeader(d.data(), d.size(), h) == link::DecodeResult::ok, "52-byte empty DATA");
    Check(link::DecodeHeader(d.data(), 51, h) == link::DecodeResult::malformed, "51 bytes");
    auto max = Datagram(link::PacketType::data, link::kLinkMaxBody);
    Check(max.size() == 1200 && link::DecodeHeader(max.data(), max.size(), h) == link::DecodeResult::ok, "1200 bytes");
    std::vector<std::uint8_t> big(1201, 0);
    std::memcpy(big.data(), max.data(), 36);
    big[34] = static_cast<std::uint8_t>((1201 - 52) & 0xFF);
    big[35] = static_cast<std::uint8_t>((1201 - 52) >> 8);
    Check(link::DecodeHeader(big.data(), big.size(), h) == link::DecodeResult::malformed, "1201 bytes");
    auto mismatch = Datagram(link::PacketType::data, 10);
    Check(link::DecodeHeader(mismatch.data(), mismatch.size() - 1, h) == link::DecodeResult::malformed, "body_len mismatch");
    auto reserved = Datagram(link::PacketType::data, 0, 0x4);
    Check(link::DecodeHeader(reserved.data(), reserved.size(), h) == link::DecodeResult::malformed, "reserved flag");
    auto unauth = Datagram(link::PacketType::data, 0, link::HeaderFlag::Unauth);
    Check(link::DecodeHeader(unauth.data(), unauth.size(), h) == link::DecodeResult::malformed, "UNAUTH on DATA");
    auto unauth_reject = Datagram(link::PacketType::reject, link::kRejectBodySize, link::HeaderFlag::Unauth);
    Check(link::DecodeHeader(unauth_reject.data(), unauth_reject.size(), h) == link::DecodeResult::ok && h.type == link::PacketType::reject,
          "UNAUTH on REJECT");
    auto bad_type = Datagram(link::PacketType::data, 0);
    bad_type[5] = 6;
    Check(link::DecodeHeader(bad_type.data(), bad_type.size(), h) == link::DecodeResult::malformed, "unknown type");
    bad_type[5] = 0;
    Check(link::DecodeHeader(bad_type.data(), bad_type.size(), h) == link::DecodeResult::malformed, "type 0");
    auto magic = Datagram(link::PacketType::data, 0);
    magic[0] = 'X';
    Check(link::DecodeHeader(magic.data(), magic.size(), h) == link::DecodeResult::malformed, "magic");
    // Another version: answerable only at >= 1200 bytes (no amplification).
    auto v3 = Datagram(link::PacketType::hello, 100);
    v3[4] = 3;
    Check(link::DecodeHeader(v3.data(), v3.size(), h) == link::DecodeResult::malformed, "version 3 below 1200 is malformed");
    auto v3big = Datagram(link::PacketType::hello, link::kLinkMaxBody);
    v3big[4] = 3;
    Check(link::DecodeHeader(v3big.data(), v3big.size(), h) == link::DecodeResult::version_mismatch, "version 3 at 1200");
    // Field round trip.
    link::Header in{};
    in.type = link::PacketType::confirm;
    in.flags = link::HeaderFlag::HasAck;
    in.session_id = 0x1122334455667788ull;
    in.seq = 7;
    in.ack = 9;
    in.ack_bits = 0x80000001u;
    in.send_ms = 123456;
    in.ack_delay_ms = 33;
    in.body_len = 0;
    std::uint8_t raw[52]{};
    link::EncodeHeader(in, raw);
    Check(std::memcmp(raw, "MGSL", 4) == 0 && raw[4] == 2 && raw[5] == 3, "magic, version, type bytes");
    link::Header out{};
    Check(link::DecodeHeader(raw, sizeof(raw), out) == link::DecodeResult::ok && out.session_id == in.session_id && out.seq == 7 &&
          out.ack == 9 && out.ack_bits == 0x80000001u && out.send_ms == 123456 && out.ack_delay_ms == 33 &&
          out.flags == link::HeaderFlag::HasAck, "header round trip");
    // Tags.
    link::HmacKey key{}, other{};
    link::HmacKeyInit(key, "k", 1);
    link::HmacKeyInit(other, "j", 1);
    link::WriteTag(key, raw, sizeof(raw));
    Check(link::VerifyTag(key, raw, sizeof(raw)) && !link::VerifyTag(other, raw, sizeof(raw)), "tag verifies with its key only");
    raw[10] ^= 1;
    Check(!link::VerifyTag(key, raw, sizeof(raw)), "a flipped bit fails the tag");
    Check(!link::VerifyTag(link::HmacKey{}, raw, sizeof(raw)), "an invalid key never verifies");
}

void Bodies() {
    link::Hello hello{};
    hello.proto = 2;
    hello.min_proto = 2;
    hello.mode = link::kModePlay;
    hello.role = link::kRoleGuest;
    for (int i = 0; i < 16; ++i) hello.nonce_g[i] = static_cast<std::uint8_t>(i);
    std::memcpy(hello.build, "0.12.0", 6);
    std::uint8_t body[link::kHelloBodySize];
    link::EncodeHelloBody(hello, body);
    link::Hello back{};
    Check(link::DecodeHelloBody(body, sizeof(body), back) && back.proto == 2 && back.mode == 2 && back.role == 2 &&
          back.nonce_g[15] == 15 && std::strcmp(back.build, "0.12.0") == 0, "HELLO body");
    Check(!link::DecodeHelloBody(body, sizeof(body) - 1, back), "HELLO body size");
    Check(link::kLinkOverhead + link::kWelcomeBodySize == 104 && link::kLinkOverhead + link::kRejectBodySize == 88 &&
          link::kLinkOverhead + link::kHelloBodySize == 1200, "handshake sizes 1200/104/88");
    // Received build strings are sanitized for the logs.
    std::memcpy(body + 24, "1.0;\"\x01x", 7);
    Check(link::DecodeHelloBody(body, sizeof(body), back) && std::strcmp(back.build, "1.0???x") == 0, "build sanitized");
    link::Reject reject{};
    reject.reason = 5;
    reject.proto = 2;
    std::memcpy(reject.build, "0.13.0", 6);
    reject.echo_nonce_g[0] = 0xAB;
    std::uint8_t rb[link::kRejectBodySize];
    link::EncodeRejectBody(reject, rb);
    link::Reject rback{};
    Check(link::DecodeRejectBody(rb, sizeof(rb), rback) && rback.reason == 5 && rback.echo_nonce_g[0] == 0xAB &&
          std::strcmp(rback.build, "0.13.0") == 0, "REJECT body");
    link::Welcome w{};
    w.proto = 2;
    w.role = link::kRoleHost;
    w.nonce_h[3] = 7;
    w.echo_nonce_g[4] = 9;
    std::uint8_t wb[link::kWelcomeBodySize];
    link::EncodeWelcomeBody(w, wb);
    link::Welcome wback{};
    Check(link::DecodeWelcomeBody(wb, sizeof(wb), wback) && wback.nonce_h[3] == 7 && wback.echo_nonce_g[4] == 9 && wback.role == 1,
          "WELCOME body");
}

void Messages() {
    std::uint8_t body[link::kLinkMaxBody];
    link::BodyWriter w(body, sizeof(body));
    const std::uint8_t frame[56] = {1};
    Check(w.Add(link::MsgType::frame, frame, sizeof(frame)), "frame added");
    Check(w.AddRelAck(2, 31, 1000, 0x5), "relack added");
    const std::uint8_t payload[10] = {9, 8, 7};
    Check(w.AddReliable(3, 65535, payload, sizeof(payload)), "reliable added");
    const std::uint8_t unknown[3] = {1, 2, 3};
    Check(w.Add(static_cast<link::MsgType>(0x7F), unknown, sizeof(unknown)), "unknown type added");
    unsigned count = 0;
    Check(link::ValidateBody(body, w.size(), count) && count == 4, "body validates");
    link::MessageReader reader(body, w.size());
    link::Message m{};
    Check(reader.Next(m) && m.type == 1 && m.size == 56, "frame read");
    Check(reader.Next(m) && m.type == 4, "relack read");
    link::RelAckMsg a{};
    Check(link::DecodeRelAck(m, a) && a.channel == 2 && a.free_slots == 31 && a.next_expected == 1000 && a.bits == 5, "relack fields");
    Check(reader.Next(m) && m.type == 2, "reliable read");
    link::ReliableMsg r{};
    Check(link::DecodeReliable(m, r) && r.channel == 3 && r.id == 65535 && r.size == 10 && r.payload[0] == 9, "reliable fields");
    Check(reader.Next(m) && m.type == 0x7F && m.size == 3, "unknown type skipped by length");
    Check(!reader.Next(m), "end of body");
    // A length overrun makes the whole packet malformed.
    body[w.size() - 3 - 2] = 50;
    Check(!link::ValidateBody(body, w.size(), count), "overrun");
    std::uint8_t flagged[8] = {1, 1, 0, 0};
    Check(!link::ValidateBody(flagged, 4, count), "nonzero message flags");
    Check(!link::ValidateBody(flagged, 3, count), "truncated message header");
    Check(link::ValidateBody(nullptr, 0, count) && count == 0, "empty body");
    // The writer never exceeds its capacity.
    std::uint8_t small[20];
    link::BodyWriter s(small, sizeof(small));
    Check(s.Add(link::MsgType::frame, frame, 16) && !s.Add(link::MsgType::frame, frame, 1) && s.size() == 20, "capacity respected");
    std::uint8_t big[1100]{};
    link::BodyWriter b(body, sizeof(body));
    Check(!b.AddReliable(2, 0, big, 1025), "reliable payload above 1024");
    Check(b.AddReliable(2, 0, big, 1024), "1024-byte reliable payload");
    // Masking (logs and link-status.json).
    char out[16];
    link::MaskAddress(0xCB007107u, out);
    Check(std::strcmp(out, "203.0.x.x") == 0, "public address masked");
    link::MaskAddress(0xC0A80114u, out);
    Check(std::strcmp(out, "192.168.1.20") == 0, "192.168/16 shown");
    link::MaskAddress(0x64656667u, out);
    Check(std::strcmp(out, "100.101.102.103") == 0, "100.64/10 shown");
    link::MaskAddress(0x64C00001u, out);
    Check(std::strcmp(out, "100.192.x.x") == 0, "100.192 is outside 100.64/10");
    link::MaskAddress(0x1A000001u, out);
    Check(std::strcmp(out, "26.0.0.1") == 0, "26/8 shown");
    link::MaskAddress(0xAC1F0001u, out);
    Check(std::strcmp(out, "172.31.0.1") == 0, "172.16/12 shown");
    link::MaskAddress(0xAC200001u, out);
    Check(std::strcmp(out, "172.32.x.x") == 0, "172.32 masked");
    link::MaskAddress(0x7F000001u, out);
    Check(std::strcmp(out, "127.0.0.1") == 0, "loopback shown");
    link::FormatAddress(0xFFFFFFFFu, out);
    Check(std::strcmp(out, "255.255.255.255") == 0, "longest dotted form fits");
}

void Replay() {
    link::ReplayWindow w;
    Check(!w.Fresh(0), "seq 0 never fresh");
    Check(w.Fresh(1) && !w.any(), "first");
    w.Mark(1);
    Check(!w.Fresh(1), "duplicate");
    w.Mark(5);
    Check(w.Fresh(3) && w.Fresh(4) && w.Fresh(2), "out of order still fresh");
    w.Mark(3);
    Check(!w.Fresh(3) && w.highest() == 5, "out of order marked");
    // ack_bits: bit i => highest-1-i received. 4 no, 3 yes, 2 no, 1 yes.
    Check(w.AckBits() == 0xAu, "ack bits");
    w.Mark(2000);
    Check(!w.Fresh(2000 - 1024) && w.Fresh(2000 - 1023) && !w.Fresh(5), "older than 1024 rejected");
    Check(w.AckBits() == 0, "ack bits after a jump");
    w.Mark(1999);
    Check(w.AckBits() == 1u && !w.Fresh(1999), "neighbour bit");
    w.Mark(100000);
    Check(!w.Fresh(1999) && w.Fresh(99999), "a big jump clears the window");
    w.Reset();
    Check(!w.any() && w.Fresh(1), "reset");
}

void Timing() {
    link::SentRing sent;
    sent.Record(10, 1000);
    std::uint32_t ms = 0;
    Check(sent.Take(10, ms) && ms == 1000 && !sent.Take(10, ms), "sent ring samples once");
    sent.Record(266, 2000);
    Check(!sent.Take(10, ms), "overwritten entry");
    link::RttEstimator rtt;
    Check(rtt.rto_ms() == 250, "initial RTO 250");
    rtt.Sample(100);
    Check(rtt.srtt_ms() == 100 && rtt.rttvar_ms() == 50 && rtt.rto_ms() == 300, "first sample: srtt=s, rttvar=s/2");
    rtt.Sample(200);
    // rttvar = 50 + (|200-100| - 50)/4 = 62.5; srtt = 100 + 100/8 = 112.5.
    Check(rtt.srtt_ms() == 112 && rtt.rttvar_ms() == 62, "RFC 6298 smoothing");
    link::RttEstimator lo;
    lo.Sample(1);
    Check(lo.rto_ms() == 100, "RTO floor 100");
    link::RttEstimator hi;
    hi.Sample(5000);
    Check(hi.rto_ms() == 2000, "RTO cap 2000");
    // Inbound loss from sequence gaps over two 5-s buckets.
    link::LossMeter loss;
    loss.OnAccepted(1, 0, false, 0);
    for (std::uint32_t s = 2; s <= 100; ++s) loss.OnAccepted(s, s - 1, true, s);
    Check(loss.Permille(200) == 0, "no loss");
    loss.OnAccepted(110, 100, true, 300);  // 9 lost
    Check(loss.Permille(300) == 9 * 1000 / 110, "gap counted");
    loss.OnAccepted(105, 110, true, 310);  // a late one comes back
    Check(loss.Permille(310) == 8 * 1000 / 111, "late packet uncounted");
    Check(loss.Permille(5300) == 8 * 1000 / 111, "previous bucket still counts");
    Check(loss.Permille(10400) == 0, "two buckets later it is gone");
    // Jitter: constant transit gives 0; alternating +-10 ms converges to ~10.
    link::JitterMeter jitter;
    for (std::uint32_t i = 0; i < 100; ++i) jitter.OnPacket(1000 + i * 33 + 40, i * 33, i * 33);
    Check(jitter.jitter_ms() == 0 && jitter.have_offset() && jitter.offset_ms() == 1040, "constant transit");
    for (std::uint32_t i = 100; i < 400; ++i) jitter.OnPacket(1000 + i * 33 + 40 + ((i & 1) ? 10 : 0), i * 33, i * 33);
    Check(jitter.jitter_ms() >= 8 && jitter.jitter_ms() <= 10, "jitter converges");
    link::JitterMeter wrap;
    wrap.OnPacket(5, 0xFFFFFFF0u, 0);
    Check(wrap.offset_ms() == 21, "transit is wrap-safe");
}

void Interp() {
    link::InterpController c;
    c.Init(60, 350);
    Check(c.goal() == 100 && c.applied() == 100, "initial 100");
    // 30 Hz, no jitter, no loss: target = 33 + 0 + 0 + 10 = 43 -> min 60.
    c.Update(0, 30, 0, 0);
    Check(c.target() == 60 && c.goal() == 100, "down is held");
    c.Update(4000, 30, 0, 0);
    Check(c.goal() == 100, "held before 5 s");
    c.Update(5000, 30, 0, 0);
    Check(c.goal() == 60, "down after 5 s continuously");
    for (int i = 0; i < 10; ++i) c.Tick();
    Check(c.applied() == 90, "slews 1 ms per tick");
    for (int i = 0; i < 100; ++i) c.Tick();
    Check(c.applied() == 60, "reaches the goal");
    // Jitter up: immediate goal raise, slewed application.
    c.Update(6000, 30, 40, 0);
    Check(c.goal() == 33 + 80 + 10 && c.applied() == 60, "up at once (goal), applied slews");
    c.Tick();
    Check(c.applied() == 61, "one step");
    // Loss hysteresis: enter at 2% (20 permille), leave below 1%.
    link::InterpController h;
    h.Init(20, 1000);
    h.Update(0, 30, 0, 20);
    Check(h.loss_level() == 1 && h.target() == 33 + 33 + 10, "loss pad one frame at 2%");
    h.Update(1000, 30, 0, 15);
    Check(h.loss_level() == 1, "kept at 1.5%");
    h.Update(2000, 30, 0, 9);
    Check(h.loss_level() == 0, "left below 1%");
    h.Update(3000, 30, 0, 60);
    Check(h.loss_level() == 2 && h.target() == 33 + 66 + 10, "two frames at 6%");
    h.Update(4000, 30, 0, 45);
    Check(h.loss_level() == 2, "kept at 4.5%");
    h.Update(5000, 30, 0, 39);
    Check(h.loss_level() == 1, "back to one frame below 4%");
    // Bounds.
    link::InterpController b;
    b.Init(60, 350);
    b.Update(0, 1, 1000, 1000);
    Check(b.target() == 350 && b.goal() == 350, "max bound");
    link::InterpController narrow;
    narrow.Init(200, 150);
    Check(narrow.applied() == 200, "max below min clamps to min");
    // A goal that keeps bouncing within 15 ms never moves down.
    link::InterpController steady;
    steady.Init(60, 350);
    for (std::uint64_t t = 0; t < 20000; t += 1000) steady.Update(t, 30, 30, 0);  // target 103
    Check(steady.goal() == 103, "raised to 103");
    for (std::uint64_t t = 20000; t < 40000; t += 1000) steady.Update(t, 30, 25, 0);  // target 93 (+15 > 103)
    Check(steady.goal() == 103, "within the 15 ms margin no change");
}

// --- Reliable channel through a seeded lossy / duplicating / reordering pipe.
struct Rng {
    std::uint64_t s = 0x9E3779B97F4A7C15ull;
    std::uint32_t Next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<std::uint32_t>((s * 0x2545F4914F6CDD1Dull) >> 32);
    }
};
struct Packet {
    std::uint64_t due;
    std::vector<std::uint8_t> body;
};
struct Pipe {
    Rng rng;
    unsigned loss = 200, dup = 50, reorder = 100;  // permille
    std::deque<Packet> queue;
    void Send(const std::uint8_t* body, std::size_t size, std::uint64_t now) {
        if (rng.Next() % 1000 < loss) return;
        std::uint64_t delay = 20 + (rng.Next() % 1000 < reorder ? 40 : 0);
        Packet p{now + delay, std::vector<std::uint8_t>(body, body + size)};
        queue.push_back(p);
        if (rng.Next() % 1000 < dup) queue.push_back(Packet{now + delay + 1, p.body});
    }
    bool Receive(std::uint64_t now, std::vector<std::uint8_t>& out) {
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->due <= now) {
                out = it->body;
                queue.erase(it);
                return true;
            }
        }
        return false;
    }
};

struct Side {
    link::ReliableSender tx[link::kReliableChannels];
    link::ReliableReceiver rx[link::kReliableChannels];
};

void Deliver(Side& side, const std::vector<std::uint8_t>& body, std::uint64_t now) {
    unsigned count = 0;
    Check(link::ValidateBody(body.data(), body.size(), count), "pipe body valid");
    link::MessageReader reader(body.data(), body.size());
    link::Message m{};
    while (reader.Next(m)) {
        link::ReliableMsg r{};
        link::RelAckMsg a{};
        if (link::DecodeReliable(m, r)) side.rx[r.channel].Accept(r.id, r.payload, r.size, now);
        else if (link::DecodeRelAck(m, a)) side.tx[a.channel].OnRelAck(a.next_expected, a.bits, a.free_slots, now);
    }
}

// One 20 ms tick of one side: pack due RELACKs and reliable data.
std::size_t Pump(Side& side, Pipe& pipe, std::uint64_t now, std::uint32_t rto, std::size_t& retransmitted) {
    std::size_t budget = link::kRetransmitBudgetBytes;
    std::size_t sent = 0;
    for (int packets = 0; packets < 8; ++packets) {
        link::RelAckState acks[link::kReliableChannels]{};
        for (unsigned ch = 1; ch < link::kReliableChannels; ++ch) {
            if (!side.rx[ch].dirty()) continue;
            acks[ch].due = true;
            side.rx[ch].AckState(acks[ch].next_expected, acks[ch].bits, acks[ch].free_slots);
            side.rx[ch].ClearDirty();
        }
        std::uint8_t body[link::kLinkMaxBody];
        link::BodyWriter writer(body, sizeof(body));
        const std::size_t before = budget;
        link::PackBody(writer, nullptr, 0, acks, side.tx, now, rto, budget);
        retransmitted += before - budget;
        if (!writer.size()) break;
        Check(writer.size() <= link::kLinkMaxBody, "packed body within 1148");
        pipe.Send(body, writer.size(), now);
        ++sent;
    }
    Check(link::kRetransmitBudgetBytes >= budget, "budget never grows");
    return sent;
}

void ReliableStress() {
    static Side a, b;  // large: static storage
    Pipe ab, ba;
    ab.rng.s ^= 1;
    ba.rng.s ^= 2;
    const unsigned total = 10000;
    unsigned queued = 0, received = 0;
    std::uint32_t expected_next[link::kReliableChannels] = {0, 0, 0, 0};
    std::uint32_t queued_per_channel[link::kReliableChannels] = {0, 0, 0, 0};
    std::size_t max_retransmit_tick = 0;
    std::uint64_t now = 0;
    for (; now < 3600000 && received < total; now += 20) {
        // The producer queues while the window has room (back-pressure on full).
        while (queued < total) {
            const std::uint8_t channel = static_cast<std::uint8_t>(1 + queued % 3);
            std::uint8_t payload[64];
            const std::size_t size = 4 + queued % 60;
            const std::uint32_t index = queued_per_channel[channel];
            std::memcpy(payload, &index, 4);
            std::memset(payload + 4, static_cast<int>(queued & 0xFF), size - 4);
            const auto result = a.tx[channel].Queue(payload, size, link::ReliableMode::append);
            if (result == link::ReliableResult::full) break;
            Check(result == link::ReliableResult::queued, "queued");
            ++queued_per_channel[channel];
            ++queued;
        }
        std::size_t retransmitted = 0;
        Pump(a, ab, now, 250, retransmitted);
        if (retransmitted > max_retransmit_tick) max_retransmit_tick = retransmitted;
        std::size_t ignore = 0;
        Pump(b, ba, now, 250, ignore);
        std::vector<std::uint8_t> body;
        while (ab.Receive(now, body)) Deliver(b, body, now);
        while (ba.Receive(now, body)) Deliver(a, body, now);
        // The consumer drains in order, exactly once.
        for (unsigned ch = 1; ch < link::kReliableChannels; ++ch) {
            std::uint8_t out[link::kMaxReliableMessage];
            std::size_t size = 0;
            while (b.rx[ch].Drain(out, sizeof(out), size, now)) {
                std::uint32_t index = 0;
                std::memcpy(&index, out, 4);
                Check(index == expected_next[ch], "in order, exactly once");
                ++expected_next[ch];
                ++received;
            }
        }
    }
    Check(received == total && queued == total, "10000 messages delivered");
    Check(max_retransmit_tick <= link::kRetransmitBudgetBytes, "retransmit pacing <= 2400 B per tick");
    std::printf("reliable: 10000 messages in %llu simulated ms\n", static_cast<unsigned long long>(now));
}

void ReliableUnits() {
    static link::ReliableSender tx;
    static link::ReliableReceiver rx;
    tx.Reset();
    rx.Reset();
    std::uint8_t m[8] = {1};
    // latest: replaces only the newest unsent message.
    Check(tx.Queue(m, 1, link::ReliableMode::latest) == link::ReliableResult::queued, "latest on empty queues");
    m[0] = 2;
    Check(tx.Queue(m, 1, link::ReliableMode::latest) == link::ReliableResult::replaced && tx.queued() == 1, "latest replaces");
    link::ReliableSender::Pick pick{};
    Check(tx.NextDue(0, 250, 0, pick) && !pick.retransmit && pick.payload[0] == 2 && pick.id == 0, "first transmission");
    tx.MarkSent(pick.id, 0);
    m[0] = 3;
    Check(tx.Queue(m, 1, link::ReliableMode::latest) == link::ReliableResult::queued && tx.queued() == 2, "sent ones are not replaced");
    Check(tx.Queue(m, 1025, link::ReliableMode::append) == link::ReliableResult::too_large, "too large");
    // RELACK never acknowledges what the receiver did not store.
    Check(!rx.Accept(40, m, 1, 0), "beyond the window is not stored");
    std::uint16_t next = 0;
    std::uint32_t bits = 0;
    std::uint8_t free_slots = 0;
    rx.AckState(next, bits, free_slots);
    Check(next == 0 && bits == 0 && free_slots == 32, "nothing stored, nothing acknowledged");
    Check(rx.Accept(2, m, 1, 0) && rx.dirty(), "out of order stored");
    rx.AckState(next, bits, free_slots);
    Check(next == 0 && bits == 0x2u && free_slots == 31, "bit for id 2 (next_expected+1+1)");
    Check(!rx.Accept(2, m, 1, 0), "duplicate");
    // Window full: the sender stalls, then resumes when the receiver drains.
    tx.Reset();
    rx.Reset();
    for (unsigned i = 0; i < 32; ++i) Check(tx.Queue(m, 1, link::ReliableMode::append) == link::ReliableResult::queued, "fill");
    Check(tx.Queue(m, 1, link::ReliableMode::append) == link::ReliableResult::full, "sender window full");
    for (unsigned i = 0; i < 32; ++i) {
        Check(tx.NextDue(0, 250, 0, pick) && !pick.retransmit, "first sends");
        tx.MarkSent(pick.id, 0);
        Check(rx.Accept(pick.id, pick.payload, pick.size, 0), "stored");
    }
    rx.AckState(next, bits, free_slots);
    Check(next == 32 && free_slots == 0, "all stored, no free slot");
    Check(tx.OnRelAck(next, bits, free_slots, 10) == 32 && tx.queued() == 0, "all acknowledged");
    Check(tx.Queue(m, 1, link::ReliableMode::append) == link::ReliableResult::queued, "queue again");
    Check(tx.NextDue(20, 250, 2400, pick) && pick.id == 32, "a probe goes out at once");
    tx.MarkSent(pick.id, 20);
    Check(!rx.Accept(pick.id, pick.payload, pick.size, 20), "receiver full: not stored");
    Check(!tx.NextDue(500, 250, 2400, pick), "zero window: no retransmit before the 1 s probe");
    Check(tx.NextDue(1020, 250, 2400, pick) && pick.retransmit, "1 s probe");
    std::uint8_t out[8];
    std::size_t size = 0;
    Check(rx.Drain(out, sizeof(out), size, 1100) && size == 1 && rx.dirty(), "drain frees a slot (RELACK due)");
    rx.AckState(next, bits, free_slots);
    Check(free_slots == 1, "one free slot");
    tx.OnRelAck(next, bits, free_slots, 1100);
    Check(tx.NextDue(1100 + 2000, 250, 2400, pick) && pick.id == 32 && pick.retransmit, "resumes after the window opens");
    Check(rx.Accept(pick.id, pick.payload, pick.size, 3100), "stored after the drain");
    // Retransmit backoff: x2 per retry, capped at 2000.
    link::ReliableSender back;
    back.Queue(m, 1, link::ReliableMode::append);
    Check(back.NextDue(0, 250, 0, pick), "first");
    back.MarkSent(pick.id, 0);
    Check(!back.NextDue(249, 250, 2400, pick) && back.NextDue(250, 250, 2400, pick), "RTO 250");
    back.MarkSent(pick.id, 250);
    Check(!back.NextDue(749, 250, 2400, pick) && back.NextDue(750, 250, 2400, pick), "backoff 500");
    back.MarkSent(pick.id, 750);
    Check(!back.NextDue(1749, 250, 2400, pick) && back.NextDue(1750, 250, 2400, pick), "backoff 1000");
    back.MarkSent(pick.id, 1750);
    back.MarkSent(pick.id, 1750);
    back.MarkSent(pick.id, 1750);
    Check(!back.NextDue(3749, 250, 2400, pick) && back.NextDue(3750, 250, 2400, pick), "capped at 2000");
    Check(!back.NextDue(3750, 250, 0, pick), "no budget, no retransmit");
    Check(back.retransmits() == 5, "retransmits counted");
    // Session reset.
    back.Reset();
    rx.Reset();
    Check(back.idle() && rx.stored() == 0, "reset clears both sides");
}

void Packer() {
    // The maximum mix: a 76-byte input FRAME, three RELACKs and full reliable windows.
    static link::ReliableSender senders[link::kReliableChannels];
    for (auto& s : senders) s.Reset();
    std::uint8_t payload[link::kMaxReliableMessage]{};
    for (unsigned ch = 1; ch < link::kReliableChannels; ++ch)
        for (unsigned i = 0; i < link::kReliableWindow; ++i)
            Check(senders[ch].Queue(payload, link::kMaxReliableMessage - i, link::ReliableMode::append) == link::ReliableResult::queued,
                  "queue max");
    link::RelAckState acks[link::kReliableChannels]{};
    for (unsigned ch = 1; ch < link::kReliableChannels; ++ch) acks[ch] = link::RelAckState{true, 32, 1, 0xFFFFFFFFu};
    const std::uint8_t frame[76] = {0x4D};
    for (int round = 0; round < 200; ++round) {
        std::uint8_t body[link::kLinkMaxBody];
        link::BodyWriter writer(body, sizeof(body));
        std::size_t budget = link::kRetransmitBudgetBytes;
        Check(link::PackBody(writer, frame, sizeof(frame), acks, senders, static_cast<std::uint64_t>(round) * 1000, 250, budget),
              "packed");
        Check(link::kLinkOverhead + writer.size() <= link::kLinkMaxDatagram, "never above 1200 bytes");
        link::MessageReader reader(body, writer.size());
        link::Message m{};
        Check(reader.Next(m) && m.type == static_cast<std::uint8_t>(link::MsgType::frame) && m.size == 76, "the FRAME is first");
        unsigned relacks = 0;
        while (reader.Next(m)) relacks += m.type == static_cast<std::uint8_t>(link::MsgType::relack) ? 1u : 0u;
        Check(relacks == 3, "all RELACKs fit after a FRAME");
    }
    // A frame that does not fit writes nothing.
    std::uint8_t tiny[8];
    link::BodyWriter writer(tiny, sizeof(tiny));
    std::size_t budget = 0;
    Check(!link::PackBody(writer, frame, sizeof(frame), nullptr, nullptr, 0, 250, budget) && writer.size() == 0, "frame too big");
}

void Datagrams() {
    std::uint8_t data[link::kDatagramHeaderSize + link::kMaxDatagramPayload + 1]{};
    const std::uint8_t payload[link::kMaxDatagramPayload + 1] = {0x57, 1};
    Check(link::EncodeDatagram(1, payload, 2, data, sizeof(data)) == 6 && data[0] == 1 && data[1] == 0 && data[2] == 0 &&
              data[3] == 0 && data[4] == 0x57,
          "encode channel 1");
    Check(link::EncodeDatagram(0, payload, 2, data, sizeof(data)) == 0 && link::EncodeDatagram(4, payload, 2, data, sizeof(data)) == 0,
          "encode refuses channels 0 and 4");
    Check(link::EncodeDatagram(3, payload, link::kMaxDatagramPayload, data, sizeof(data)) == 4 + link::kMaxDatagramPayload,
          "encode 512");
    Check(link::EncodeDatagram(3, payload, link::kMaxDatagramPayload + 1, data, sizeof(data)) == 0, "encode refuses 513");
    Check(link::EncodeDatagram(1, payload, 10, data, 13) == 0, "encode refuses a short buffer");
    const auto decode = [&](std::size_t size, link::DatagramMsg& out) {
        const link::Message m{static_cast<std::uint8_t>(link::MsgType::datagram), data, static_cast<std::uint16_t>(size)};
        return link::DecodeDatagram(m, out);
    };
    link::DatagramMsg d{};
    Check(link::EncodeDatagram(2, payload, 2, data, sizeof(data)) == 6 && decode(6, d) && d.channel == 2 && d.size == 2 &&
              d.payload == data + 4,
          "decode channel 2");
    Check(decode(4, d) && d.size == 0, "an empty payload decodes");
    Check(!decode(3, d), "shorter than the header");
    data[0] = 0;
    Check(!decode(6, d), "channel 0 refused");
    data[0] = 4;
    Check(!decode(6, d), "channel 4 refused");
    data[0] = 1;
    data[1] = 1;
    Check(!decode(6, d), "reserved byte 1");
    data[1] = 0;
    data[3] = 1;
    Check(!decode(6, d), "reserved u16");
    data[3] = 0;
    Check(link::EncodeDatagram(1, payload, link::kMaxDatagramPayload, data, sizeof(data)) && decode(4 + 512, d) &&
              !decode(4 + 513, d),
          "payload above 512 refused");
    const link::Message frame_type{static_cast<std::uint8_t>(link::MsgType::frame), data, 6};
    Check(!link::DecodeDatagram(frame_type, d), "another message type is not a datagram");

    // PackBodyWith: the DATAGRAM first, then RELACKs and reliable data in PackBody order.
    static link::ReliableSender senders[link::kReliableChannels];
    for (auto& s : senders) s.Reset();
    std::uint8_t big[link::kMaxReliableMessage]{};
    for (unsigned ch = 1; ch < link::kReliableChannels; ++ch)
        for (unsigned i = 0; i < 4; ++i)
            Check(senders[ch].Queue(big, link::kMaxReliableMessage - i, link::ReliableMode::append) == link::ReliableResult::queued,
                  "queue");
    link::RelAckState acks[link::kReliableChannels]{};
    for (unsigned ch = 1; ch < link::kReliableChannels; ++ch) acks[ch] = link::RelAckState{true, 32, 1, 0};
    const std::size_t first = link::EncodeDatagram(1, payload, link::kMaxDatagramPayload, data, sizeof(data));
    for (int round = 0; round < 50; ++round) {
        std::uint8_t body[link::kLinkMaxBody];
        link::BodyWriter writer(body, sizeof(body));
        std::size_t budget = link::kRetransmitBudgetBytes;
        Check(link::PackBodyWith(writer, link::MsgType::datagram, data, first, round ? nullptr : acks, senders,
                                 static_cast<std::uint64_t>(round) * 1000, 250, budget),
              "packed with a datagram");
        Check(writer.size() <= link::kLinkMaxBody && link::kLinkOverhead + writer.size() <= link::kLinkMaxDatagram, "1148 bound");
        unsigned messages = 0;
        Check(link::ValidateBody(body, writer.size(), messages) && messages >= 1, "valid body");
        link::MessageReader reader(body, writer.size());
        link::Message m{};
        Check(reader.Next(m) && m.type == static_cast<std::uint8_t>(link::MsgType::datagram) && m.size == first &&
                  link::DecodeDatagram(m, d) && d.size == link::kMaxDatagramPayload,
              "the DATAGRAM is first");
        unsigned relacks = 0, reliable = 0;
        while (reader.Next(m)) {
            relacks += m.type == static_cast<std::uint8_t>(link::MsgType::relack) ? 1u : 0u;
            reliable += m.type == static_cast<std::uint8_t>(link::MsgType::reliable) ? 1u : 0u;
        }
        if (!round) Check(relacks == 3, "all RELACKs after the DATAGRAM");
    }
    std::uint8_t tiny[16];
    link::BodyWriter small(tiny, sizeof(tiny));
    std::size_t budget = 0;
    Check(!link::PackBodyWith(small, link::MsgType::datagram, data, first, nullptr, nullptr, 0, 250, budget) && small.size() == 0,
          "a datagram that does not fit writes nothing");
    link::BodyWriter other(tiny, sizeof(tiny));
    Check(!link::PackBodyWith(other, link::MsgType::reliable, data, 4, nullptr, nullptr, 0, 250, budget) && other.size() == 0,
          "only FRAME or DATAGRAM may come first");
    // PackBody is PackBodyWith(frame): byte-identical bodies.
    std::uint8_t a[link::kLinkMaxBody], b[link::kLinkMaxBody];
    static link::ReliableSender sa[link::kReliableChannels], sb[link::kReliableChannels];
    for (unsigned ch = 1; ch < link::kReliableChannels; ++ch) {
        sa[ch].Reset();
        sb[ch].Reset();
        sa[ch].Queue(big, 300, link::ReliableMode::append);
        sb[ch].Queue(big, 300, link::ReliableMode::append);
    }
    const std::uint8_t frame[56] = {0x4D};
    link::BodyWriter wa(a, sizeof(a)), wb(b, sizeof(b));
    std::size_t ba = 1000, bb = 1000;
    Check(link::PackBody(wa, frame, sizeof(frame), acks, sa, 0, 250, ba) &&
              link::PackBodyWith(wb, link::MsgType::frame, frame, sizeof(frame), acks, sb, 0, 250, bb) && wa.size() == wb.size() &&
              std::memcmp(a, b, wa.size()) == 0 && ba == bb,
          "PackBody delegates unchanged");
    // A reader that predates type 5 skips it by its length: the FRAME behind it survives.
    std::uint8_t mixed[64];
    link::BodyWriter mw(mixed, sizeof(mixed));
    Check(mw.Add(link::MsgType::datagram, data, 6) && mw.Add(link::MsgType::frame, frame, 20), "mixed body");
    unsigned count = 0;
    Check(link::ValidateBody(mixed, mw.size(), count) && count == 2, "type 5 validates as a message");
    link::MessageReader old(mixed, mw.size());
    link::Message m{};
    unsigned frames = 0;
    while (old.Next(m)) frames += m.type == static_cast<std::uint8_t>(link::MsgType::frame) ? 1u : 0u;
    Check(frames == 1, "the old dispatcher still finds the FRAME after an unknown type");
}
} // namespace

int main() {
    HeaderStrictness();
    Bodies();
    Messages();
    Replay();
    Timing();
    Interp();
    ReliableUnits();
    ReliableStress();
    Packer();
    Datagrams();
    std::printf("link_protocol_tests: %u checks passed\n", checks);
    return 0;
}
