// Link v2 session state machine (docs/link-protocol.md "Handshake") over an
// in-memory pipe with an injected clock and nonce source: the handshake, the
// authenticated rejections and their replay protection (CONFIRM included),
// endpoint locking, takeover and NAT rebinding, DoS budgets, timers (one lost
// keepalive), sequence exhaustion, and the rule that no response to
// unauthenticated input is larger than that input.
#include "mgscoop/link_session.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

namespace {
namespace link = mgscoop::link;
using link::Endpoint;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

struct Datagram {
    Endpoint from, to;
    std::vector<std::uint8_t> bytes;
};

struct Node {
    link::Session session;
    Endpoint self{};
    std::vector<Datagram> outbox;
    std::uint64_t rng = 0;
    std::vector<link::Event> events;
    void Collect() {
        link::Event e{};
        while (session.PopEvent(e)) events.push_back(e);
    }
    bool Saw(link::EventKind kind) const {
        for (const auto& e : events) if (e.kind == kind) return true;
        return false;
    }
    unsigned Count(link::EventKind kind) const {
        unsigned n = 0;
        for (const auto& e : events) n += e.kind == kind ? 1u : 0u;
        return n;
    }
    std::uint64_t Drops(link::DropReason reason) const { return session.stats().drops[static_cast<unsigned>(reason)]; }
};

bool NodeSend(void* context, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    auto* node = static_cast<Node*>(context);
    node->outbox.push_back(Datagram{node->self, to, std::vector<std::uint8_t>(data, data + size)});
    return true;
}
bool NodeRandom(void* context, std::uint8_t* out, std::size_t size) noexcept {
    auto* node = static_cast<Node*>(context);
    for (std::size_t i = 0; i < size; ++i) {
        node->rng = node->rng * 6364136223846793005ull + 1442695040888963407ull;
        out[i] = static_cast<std::uint8_t>(node->rng >> 56);
    }
    return true;
}

const Endpoint kHostEp{0x0A000001u, 47830};
const Endpoint kGuestEp{0x0A000002u, 50000};
const char kSecret[] = "7K3QZ9M2XWD4HT8RVN6B";
const char kOther[] = "C00PC00PC00PC00PC00P";

void CopyBuild(char (&out)[link::kBuildSize], const char* build) {
    for (std::size_t i = 0; i < link::kBuildSize; ++i) out[i] = 0;
    for (std::size_t i = 0; i < link::kBuildSize && build[i]; ++i) out[i] = build[i];
}

link::HmacKey Psk(const char* secret) {
    char s[link::kSecretChars];
    std::memcpy(s, secret, link::kSecretChars);
    link::HmacKey key{};
    link::DerivePsk(s, key);
    return key;
}

std::unique_ptr<Node> MakeNode(link::Role role, Endpoint self, Endpoint peer, const char* secret, bool play, const char* build,
                               std::uint64_t now, std::uint64_t seed) {
    auto node = std::make_unique<Node>();
    node->self = self;
    node->rng = seed;
    link::SessionConfig config{};
    config.role = role;
    config.play = play;
    config.exact_filter = false;
    config.peer = peer;
    CopyBuild(config.build, build);
    config.psk = Psk(secret);
    node->session.Start(config, link::SessionIo{node.get(), &NodeSend, &NodeRandom}, now);
    node->Collect();
    return node;
}

// Delivers every queued datagram to the node owning its destination,
// repeatedly, until nothing more is sent.
void Pump(std::vector<Node*> nodes, std::uint64_t now) {
    for (int round = 0; round < 50; ++round) {
        std::vector<Datagram> wire;
        for (Node* n : nodes) {
            for (auto& d : n->outbox) wire.push_back(d);
            n->outbox.clear();
        }
        if (wire.empty()) break;
        for (auto& d : wire) {
            for (Node* n : nodes) {
                if (!mgscoop::lab::SameEndpoint(n->self, d.to)) continue;
                n->session.OnDatagram(d.bytes.data(), d.bytes.size(), d.from, now);
                n->Collect();
            }
        }
    }
}

void Timers(std::vector<Node*> nodes, std::uint64_t now) {
    for (Node* n : nodes) {
        n->session.OnTimer(now);
        n->Collect();
    }
}

// Runs a host/guest pair until the guest is connected; returns that time.
std::uint64_t Connect(Node& host, Node& guest, std::uint64_t now) {
    for (int i = 0; i < 40 && guest.session.state() != link::LinkState::connected; ++i) {
        Timers({&host, &guest}, now);
        Pump({&host, &guest}, now);
        if (guest.session.state() != link::LinkState::connected) now += 20;
    }
    return now;
}

// Feeds unauthenticated bytes to `node` and asserts every response is no
// larger than the input.
void FeedUnauth(Node& node, const std::vector<std::uint8_t>& bytes, const Endpoint& from, std::uint64_t now, const char* what) {
    const std::size_t before = node.outbox.size();
    node.session.OnDatagram(bytes.data(), bytes.size(), from, now);
    node.Collect();
    for (std::size_t i = before; i < node.outbox.size(); ++i) Check(node.outbox[i].bytes.size() <= bytes.size(), what);
}

std::vector<std::uint8_t> MakeHello(const link::HmacKey& hs, std::uint16_t proto, std::uint8_t mode, std::uint8_t role, std::uint8_t nonce,
                                    const char* build) {
    std::vector<std::uint8_t> d(link::kLinkMaxDatagram, 0);
    link::Header h{};
    h.type = link::PacketType::hello;
    h.body_len = static_cast<std::uint16_t>(link::kHelloBodySize);
    link::EncodeHeader(h, d.data());
    link::Hello hello{};
    hello.proto = proto;
    hello.min_proto = proto;
    hello.mode = mode;
    hello.role = role;
    std::memset(hello.nonce_g, nonce, sizeof(hello.nonce_g));
    CopyBuild(hello.build, build);
    link::EncodeHelloBody(hello, d.data() + link::kLinkHeaderSize);
    link::WriteTag(hs, d.data(), d.size());
    return d;
}

void HandshakeAndData() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 1000, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 1000, 2);
    Check(host->session.state() == link::LinkState::listening && host->Saw(link::EventKind::listening), "host listening");
    Check(guest->session.state() == link::LinkState::connecting, "guest connecting");
    guest->session.OnTimer(1000);
    guest->Collect();
    Check(guest->outbox.size() == 1 && guest->outbox[0].bytes.size() == 1200, "HELLO is 1200 bytes");
    Check(guest->Saw(link::EventKind::connecting), "connecting logged");
    Pump({host.get(), guest.get()}, 1010);
    Check(host->session.state() == link::LinkState::connected && guest->session.state() == link::LinkState::connected, "both connected");
    Check(host->session.session_number() == 1 && guest->session.session_number() == 1, "session 1");
    Check(host->Saw(link::EventKind::connected) && guest->Saw(link::EventKind::connected), "connected events");
    Check(std::strcmp(host->session.remote_build(), "0.12.0") == 0 && std::strcmp(guest->session.remote_build(), "0.12.0") == 0,
          "remote builds");
    Check(mgscoop::lab::SameEndpoint(host->session.peer(), kGuestEp), "host locked onto the guest endpoint");
    Check(guest->session.rtt().have(), "guest RTT sampled from the CONFIRM ack");
    // Data both ways, delivered exactly once.
    const std::uint8_t body[] = {1, 0, 2, 0, 0xAA, 0xBB};
    Check(guest->session.SendData(body, sizeof(body), 1020), "guest sends");
    Check(guest->outbox.back().bytes.size() == link::kLinkOverhead + sizeof(body), "DATA size");
    const auto data = guest->outbox.back();
    guest->outbox.clear();
    const link::Delivery d = host->session.OnDatagram(data.bytes.data(), data.bytes.size(), kGuestEp, 1030);
    Check(d.data && d.size == sizeof(body) && std::memcmp(d.body, body, sizeof(body)) == 0 && !d.new_session, "host receives");
    const link::Delivery again = host->session.OnDatagram(data.bytes.data(), data.bytes.size(), kGuestEp, 1031);
    Check(!again.data && host->Drops(link::DropReason::replay) == 1, "replayed DATA dropped");
    Check(host->session.SendData(body, sizeof(body), 1040), "host sends");
    const auto back = host->outbox.back();
    host->outbox.clear();
    const link::Delivery g = guest->session.OnDatagram(back.bytes.data(), back.bytes.size(), kHostEp, 1050);
    Check(g.data && g.size == sizeof(body), "guest receives");
    // A flipped bit fails the tag.
    auto flipped = back.bytes;
    flipped[link::kLinkHeaderSize + 1] ^= 0x10;
    flipped[16] = static_cast<std::uint8_t>(flipped[16] + 1);  // a fresh seq, so it is not a replay
    guest->session.OnDatagram(flipped.data(), flipped.size(), kHostEp, 1060);
    Check(guest->Drops(link::DropReason::bad_mac) == 1, "flipped bit: bad_mac");
    // DATA of the current session from another endpoint is dropped (never re-targeted).
    Check(guest->session.SendData(body, sizeof(body), 1070), "guest sends again");
    const auto moved = guest->outbox.back();
    guest->outbox.clear();
    host->session.OnDatagram(moved.bytes.data(), moved.bytes.size(), Endpoint{0x0A000003u, 50000}, 1080);
    Check(host->Drops(link::DropReason::wrong_endpoint) == 1 && mgscoop::lab::SameEndpoint(host->session.peer(), kGuestEp),
          "wrong_endpoint");
    // An ack-only packet 50 ms after an ack-eliciting packet, a keepalive after 400 ms without a send.
    Check(guest->session.SendData(body, sizeof(body), 1085), "guest sends a third time");
    const auto third = guest->outbox.back();
    guest->outbox.clear();
    Check(host->session.OnDatagram(third.bytes.data(), third.bytes.size(), kGuestEp, 1090).data, "accepted");
    host->session.OnTimer(1139);
    Check(host->outbox.empty(), "no ack-only before 50 ms");
    host->session.OnTimer(1140);
    Check(host->outbox.size() == 1 && host->outbox[0].bytes.size() == link::kLinkOverhead, "ack-only after 50 ms");
    host->outbox.clear();
    host->session.OnTimer(1539);
    Check(host->outbox.empty(), "nothing due");
    host->session.OnTimer(1540);
    Check(host->outbox.size() == 1 && host->outbox[0].bytes.size() == link::kLinkOverhead, "keepalive after 400 ms");
}

// The guest's first steps by hand: HELLO -> WELCOME -> CONFIRM. Returns the CONFIRM.
Datagram HandshakeToConfirm(Node& host, Node& guest, std::uint64_t now) {
    guest.session.OnTimer(now);
    for (auto& d : guest.outbox) host.session.OnDatagram(d.bytes.data(), d.bytes.size(), guest.self, now);
    guest.outbox.clear();
    for (auto& d : host.outbox) guest.session.OnDatagram(d.bytes.data(), d.bytes.size(), host.self, now);
    host.outbox.clear();
    Check(guest.session.state() == link::LinkState::handshake && guest.outbox.size() == 1, "CONFIRM sent");
    const Datagram confirm = guest.outbox[0];
    guest.outbox.clear();
    return confirm;
}

void ConfirmReplay() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    const Datagram confirm = HandshakeToConfirm(*host, *guest, 0);
    host->session.OnDatagram(confirm.bytes.data(), confirm.bytes.size(), kGuestEp, 10);
    host->Collect();
    Check(host->session.state() == link::LinkState::connected && host->outbox.size() == 1, "commit and the first DATA");
    host->outbox.clear();  // that DATA is lost
    // The guest's resend is answered once more (the resend phase).
    host->session.OnDatagram(confirm.bytes.data(), confirm.bytes.size(), kGuestEp, 300);
    Check(host->outbox.size() == 1 && host->Drops(link::DropReason::replay) == 0, "a resent CONFIRM is answered");
    for (auto& d : host->outbox) guest->session.OnDatagram(d.bytes.data(), d.bytes.size(), kHostEp, 310);
    host->outbox.clear();
    Check(guest->session.state() == link::LinkState::connected, "the guest completes");
    // (a) After guest DATA, the CONFIRM is a replay: dropped, nothing sent.
    Check(guest->session.SendData(nullptr, 0, 320), "guest DATA");
    for (auto& d : guest->outbox) host->session.OnDatagram(d.bytes.data(), d.bytes.size(), kGuestEp, 330);
    guest->outbox.clear();
    host->Collect();
    host->session.OnDatagram(confirm.bytes.data(), confirm.bytes.size(), kGuestEp, 700);
    Check(host->outbox.empty() && host->Drops(link::DropReason::replay) == 1, "CONFIRM after guest DATA: replay, unanswered");
    // (b) The guest is gone: the host goes lost; the replayed CONFIRM changes nothing.
    std::uint64_t now = 700;
    for (; now < 4000; now += 20) host->session.OnTimer(now);
    host->Collect();
    host->outbox.clear();
    Check(host->session.state() == link::LinkState::lost, "host lost");
    const unsigned connected_before = host->Count(link::EventKind::connected);
    for (int i = 0; i < 5; ++i) {
        now += 2900;
        host->session.OnDatagram(confirm.bytes.data(), confirm.bytes.size(), kGuestEp, now);
        host->session.OnTimer(now);
        host->Collect();
    }
    Check(host->session.state() == link::LinkState::lost && host->outbox.empty() &&
          host->Count(link::EventKind::connected) == connected_before && host->Drops(link::DropReason::replay) == 6,
          "CONFIRM replayed while lost: dropped, no state change, nothing sent");
    // (c) Without any guest DATA, a CONFIRM after the resend phase is a replay too.
    auto host2 = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest2 = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    const Datagram confirm2 = HandshakeToConfirm(*host2, *guest2, 0);
    host2->session.OnDatagram(confirm2.bytes.data(), confirm2.bytes.size(), kGuestEp, 10);
    host2->outbox.clear();
    host2->session.OnDatagram(confirm2.bytes.data(), confirm2.bytes.size(), kGuestEp, 10 + link::kConfirmAnswerMs + 1);
    Check(host2->outbox.empty() && host2->Drops(link::DropReason::replay) == 1, "CONFIRM after the resend phase: replay");
}

void KeepaliveLoss() {
    // Idle connected session (no game frames): exactly one host keepalive is lost.
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    std::uint64_t now = Connect(*host, *guest, 0);
    bool dropped = false, stayed = true;
    for (const std::uint64_t end = now + 10000; now < end; now += 20) {
        Timers({host.get(), guest.get()}, now);
        if (!dropped && now > 3000 && !host->outbox.empty()) {
            host->outbox.erase(host->outbox.begin());
            dropped = true;
        }
        Pump({host.get(), guest.get()}, now + 15);  // 15 ms one way
        stayed = stayed && guest->session.state() == link::LinkState::connected && host->session.state() == link::LinkState::connected;
    }
    Check(dropped && stayed && !guest->Saw(link::EventKind::reconnecting) && guest->session.session_number() == 1,
          "one lost keepalive: the guest stays connected");
}

void RebindBusy() {
    // The guest's NAT mapping moves to another port mid-session.
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    std::uint64_t now = Connect(*host, *guest, 0);
    for (const std::uint64_t end = now + 1000; now < end; now += 20) {
        Timers({host.get(), guest.get()}, now);
        Pump({host.get(), guest.get()}, now);
    }
    const std::uint64_t t0 = now;
    guest->self = Endpoint{kGuestEp.address, 50777};  // the host's packets to 50000 now go nowhere
    bool rejected = false;
    for (; now < t0 + 6000 && guest->session.session_number() < 2; now += 20) {
        Timers({host.get(), guest.get()}, now);
        Pump({host.get(), guest.get()}, now);
        rejected = rejected || guest->session.state() == link::LinkState::rejected;
    }
    Check(!rejected && guest->session.reject_reason() == link::RejectReason::none, "rebinding: no rejected state");
    Check(guest->session.state() == link::LinkState::connected && host->Saw(link::EventKind::rebound) &&
          mgscoop::lab::SameEndpoint(host->session.peer(), guest->self), "rebinding: rebound");
    Check(now - t0 <= 3600, "rebound within about 3.5 s");
    Check(!guest->Saw(link::EventKind::rejected) && guest->session.stats().reject_ignored >= 1, "busy seen, no rejected event");
}

void ReplayedHello() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    guest->session.OnTimer(0);
    const auto hello = guest->outbox[0];
    const std::uint64_t now = Connect(*host, *guest, 0);
    Check(guest->session.state() == link::LinkState::connected, "connected");
    FeedUnauth(*host, hello.bytes, kGuestEp, now + 1, "replayed HELLO answer size");
    Check(host->Drops(link::DropReason::replay) == 1 && host->outbox.empty(), "replayed HELLO dropped silently");
}

void WrongCode() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kOther, true, "0.12.0", 0, 2);
    guest->session.OnTimer(0);
    auto hello = guest->outbox[0];
    guest->outbox.clear();
    FeedUnauth(*host, hello.bytes, kGuestEp, 0, "UNAUTH REJECT smaller than the HELLO");
    Check(host->Drops(link::DropReason::bad_mac) == 1 && host->outbox.size() == 1 && host->outbox[0].bytes.size() == 88,
          "bad code: bad_mac and one 88-byte UNAUTH REJECT");
    FeedUnauth(*host, hello.bytes, kGuestEp, 500, "second bad HELLO");
    Check(host->outbox.size() == 1, "at most one UNAUTH REJECT per second");
    Pump({host.get(), guest.get()}, 10);
    Check(guest->session.state() == link::LinkState::connecting && guest->session.reject_reason() == link::RejectReason::bad_code &&
          !guest->session.reject_verified(), "guest: a hint only, state unchanged");
    bool hint = false;
    for (const auto& e : guest->events)
        if (e.kind == link::EventKind::rejected && e.reason == link::RejectReason::bad_code && !e.verified) hint = true;
    Check(hint, "rejected; reason=bad_code; verified=0");
    // The cadence is unchanged: the next HELLO 500 ms after the first.
    guest->session.OnTimer(499);
    Check(guest->outbox.empty(), "no HELLO before 500 ms");
    guest->session.OnTimer(500);
    Check(guest->outbox.size() == 1, "HELLO at 500 ms");
}

void UnauthRejectWhileConnected() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    const std::uint64_t now = Connect(*host, *guest, 0);
    std::vector<std::uint8_t> reject(88, 0);
    link::Header h{};
    h.type = link::PacketType::reject;
    h.flags = link::HeaderFlag::Unauth;
    h.body_len = static_cast<std::uint16_t>(link::kRejectBodySize);
    link::EncodeHeader(h, reject.data());
    link::Reject r{};
    r.reason = static_cast<std::uint8_t>(link::RejectReason::bad_code);
    link::EncodeRejectBody(r, reject.data() + link::kLinkHeaderSize);
    FeedUnauth(*guest, reject, kHostEp, now + 1, "no answer to an UNAUTH REJECT");
    Check(guest->session.state() == link::LinkState::connected && guest->session.stats().reject_ignored == 1 &&
          guest->session.reject_reason() == link::RejectReason::none, "UNAUTH REJECT while connected is ignored");
}

void RejectReasons() {
    link::HmacKey hs{};
    link::DeriveHandshakeKey(Psk(kSecret), hs);
    struct Case { const char* what; std::uint16_t proto; std::uint8_t mode; std::uint8_t role; const char* build; link::RejectReason reason; };
    const Case cases[] = {
        {"proto", 3, link::kModePlay, link::kRoleGuest, "0.12.0", link::RejectReason::proto_mismatch},
        {"mode", 2, link::kModeLab, link::kRoleGuest, "0.12.0", link::RejectReason::mode_mismatch},
        {"role", 2, link::kModePlay, link::kRoleHost, "0.12.0", link::RejectReason::role_mismatch},
        {"build", 2, link::kModePlay, link::kRoleGuest, "0.13.0", link::RejectReason::build_mismatch},
    };
    std::uint8_t nonce = 1;
    for (const auto& c : cases) {
        auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
        const auto hello = MakeHello(hs, c.proto, c.mode, c.role, nonce++, c.build);
        FeedUnauth(*host, hello, kGuestEp, 0, "REJECT smaller than the HELLO");
        Check(host->outbox.size() == 1 && host->outbox[0].bytes.size() == 88, c.what);
        link::Header h{};
        Check(link::DecodeHeader(host->outbox[0].bytes.data(), 88, h) == link::DecodeResult::ok && h.flags == 0 &&
              link::VerifyTag(hs, host->outbox[0].bytes.data(), 88), "K_hs REJECT");
        link::Reject r{};
        Check(link::DecodeRejectBody(host->outbox[0].bytes.data() + link::kLinkHeaderSize, h.body_len, r) &&
              r.reason == static_cast<std::uint8_t>(c.reason) && r.echo_nonce_g[0] == nonce - 1, "reason and echo");
        Check(host->session.state() == link::LinkState::listening, "no state from a rejected HELLO");
    }
    // The guest side: another build in play is rejected (verified).
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.13.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    Timers({host.get(), guest.get()}, 0);
    Pump({host.get(), guest.get()}, 0);
    Check(guest->session.state() == link::LinkState::rejected && guest->session.reject_reason() == link::RejectReason::build_mismatch &&
          guest->session.reject_verified() && std::strcmp(guest->session.remote_build(), "0.13.0") == 0,
          "guest rejected: build_mismatch, verified");
    // Retry with a fresh nonce every 5 s.
    guest->session.OnTimer(4999);
    Check(guest->outbox.empty(), "no retry before 5 s");
    guest->session.OnTimer(5000);
    Check(guest->outbox.size() == 1, "retry at 5 s");
    // Lab: a build mismatch only warns.
    auto lab_host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, false, "0.13.0", 0, 1);
    auto lab_guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, false, "0.12.0", 0, 2);
    Connect(*lab_host, *lab_guest, 0);
    Check(lab_guest->session.state() == link::LinkState::connected && lab_host->Saw(link::EventKind::warn_build) &&
          lab_guest->Saw(link::EventKind::warn_build), "lab build mismatch: warn and connect");
    // Lab against play: mode_mismatch at the guest.
    auto play_host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto lab_guest2 = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, false, "0.12.0", 0, 2);
    Timers({play_host.get(), lab_guest2.get()}, 0);
    Pump({play_host.get(), lab_guest2.get()}, 0);
    Check(lab_guest2->session.reject_reason() == link::RejectReason::mode_mismatch && lab_guest2->session.reject_verified(),
          "mode_mismatch");
}

void StaleReject() {
    // A K_hs REJECT counts only while it echoes a pending nonce.
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.13.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    guest->session.OnTimer(0);
    host->session.OnDatagram(guest->outbox[0].bytes.data(), guest->outbox[0].bytes.size(), kGuestEp, 0);
    guest->outbox.clear();
    Check(host->outbox.size() == 1, "REJECT captured");
    const auto stale = host->outbox[0];
    host->outbox.clear();
    for (std::uint64_t t = 500; t <= 2500; t += 500) guest->session.OnTimer(t);  // 5 more HELLOs: that nonce is gone
    guest->outbox.clear();
    guest->session.OnDatagram(stale.bytes.data(), stale.bytes.size(), kHostEp, 2600);
    Check(guest->session.state() == link::LinkState::connecting && guest->session.stats().reject_ignored == 1,
          "replayed K_hs REJECT (stale echo) ignored");
    // A K_hs REJECT does nothing to a connected guest either.
    auto host2 = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest2 = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    const std::uint64_t now = Connect(*host2, *guest2, 0);
    guest2->session.OnDatagram(stale.bytes.data(), stale.bytes.size(), kHostEp, now);
    Check(guest2->session.state() == link::LinkState::connected, "K_hs REJECT ignored while connected");
}

void BusyAndTakeover() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    std::uint64_t now = Connect(*host, *guest, 0);
    const Endpoint otherEp{0x0A000009u, 50001};
    auto other = MakeNode(link::Role::guest, otherEp, kHostEp, kSecret, true, "0.12.0", now, 3);
    // While the first guest is live, the second is told busy (verified).
    for (std::uint64_t t = now; t < now + 2000; t += 20) {
        Timers({host.get(), guest.get(), other.get()}, t);
        Pump({host.get(), guest.get(), other.get()}, t);
    }
    now += 2000;
    Check(other->session.state() == link::LinkState::rejected && other->session.reject_reason() == link::RejectReason::busy &&
          other->session.reject_verified(), "busy while the first guest is live");
    Check(mgscoop::lab::SameEndpoint(host->session.peer(), kGuestEp) && host->session.session_number() == 1, "no takeover while live");
    // The first guest disappears: the host goes lost after 3 s and stops sending.
    for (std::uint64_t t = now; t < now + 3100; t += 20) {
        Timers({host.get()}, t);
        host->outbox.clear();
    }
    now += 3100;
    Check(host->session.state() == link::LinkState::lost && host->Saw(link::EventKind::lost), "host lost after 3 s");
    bool silent = true;
    for (std::uint64_t t = now; t < now + 5000; t += 20) {
        host->session.OnTimer(t);
        silent = silent && host->outbox.empty();
    }
    Check(silent, "the host sends nothing while lost");
    now += 5000;
    // The second guest's retry (every 5 s) now takes over: rebound.
    for (std::uint64_t t = now; t < now + 6000 && other->session.state() != link::LinkState::connected; t += 20) {
        Timers({host.get(), other.get()}, t);
        Pump({host.get(), other.get()}, t);
    }
    Check(other->session.state() == link::LinkState::connected && host->Saw(link::EventKind::rebound) &&
          mgscoop::lab::SameEndpoint(host->session.peer(), otherEp) && host->session.session_number() == 2, "takeover after lost: rebound");
}

void ReconnectSameEndpoint() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    const std::uint64_t now = Connect(*host, *guest, 0);
    // The guest restarts at once from the same endpoint: the commit waits for the 2 s limit.
    auto restarted = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", now, 7);
    bool committed_early = false;
    for (std::uint64_t t = now; t < now + 4000 && restarted->session.state() != link::LinkState::connected; t += 20) {
        Timers({host.get(), restarted.get()}, t);
        Pump({host.get(), restarted.get()}, t);
        if (host->session.session_number() == 2 && t < link::kCommitMinGapMs - 10) committed_early = true;
    }
    Check(!committed_early, "at most one commit per 2000 ms");
    Check(restarted->session.state() == link::LinkState::connected && host->Saw(link::EventKind::reconnected) &&
          host->session.session_number() == 2, "the same endpoint may reconnect: reconnected");
}

// A CLOSE (the partner quit the game): the host is free for a new join at
// once (lost, no 3 s timeout) and the guest starts reconnecting at once. A
// late, reordered DATA of the closed session revives neither side, the guest
// sends nothing more into the closed session, and a guest on another endpoint
// (busy while the first one was live) is accepted at once.
link::PacketType TypeOf(const Datagram& d) {
    link::Header h{};
    return link::DecodeHeader(d.bytes.data(), d.bytes.size(), h) == link::DecodeResult::ok ? h.type : link::PacketType::reject;
}
void PeerClose() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    std::uint64_t now = Connect(*host, *guest, 0);
    // One DATA each way, sent before the CLOSE and delivered after it.
    const std::uint8_t body[4] = {1, 2, 3, 4};
    Check(guest->session.SendData(body, sizeof(body), now + 1) && host->session.SendData(body, sizeof(body), now + 1), "late DATA queued");
    std::vector<Datagram> late_to_host = guest->outbox, late_to_guest = host->outbox;
    guest->outbox.clear();
    host->outbox.clear();
    Check(host->session.OnPeerClose(now + 5) && host->session.state() == link::LinkState::lost, "host: CLOSE -> lost at once");
    host->Collect();
    Check(host->Saw(link::EventKind::lost) && !host->session.OnPeerClose(now + 6), "host: one lost event; a second CLOSE changes nothing");
    const unsigned connected_before = host->Count(link::EventKind::connected);
    for (const auto& d : late_to_host) host->session.OnDatagram(d.bytes.data(), d.bytes.size(), d.from, now + 7);
    host->Collect();
    Check(host->session.state() == link::LinkState::lost && host->Count(link::EventKind::connected) == connected_before && !late_to_host.empty(),
          "host: a late DATA of the closed session does not revive it");
    Check(guest->session.OnPeerClose(now + 5) && guest->session.state() == link::LinkState::reconnecting, "guest: CLOSE -> reconnecting at once");
    guest->Collect();
    Check(guest->Saw(link::EventKind::reconnecting), "guest: reconnecting event");
    const auto wrong_before = guest->Drops(link::DropReason::wrong_session);
    for (const auto& d : late_to_guest) guest->session.OnDatagram(d.bytes.data(), d.bytes.size(), d.from, now + 7);
    guest->Collect();
    Check(guest->session.state() == link::LinkState::reconnecting && guest->Drops(link::DropReason::wrong_session) == wrong_before + 1,
          "guest: a late DATA of the closed session is wrong_session (no revival)");
    // The guest keeps trying HELLOs only: no keepalive goes into the closed session.
    bool hello_only = true;
    unsigned hellos = 0;
    for (std::uint64_t t = now + 10; t < now + 2000; t += 20) {
        guest->session.OnTimer(t);
        guest->Collect();
        for (const auto& d : guest->outbox) {
            hello_only = hello_only && TypeOf(d) == link::PacketType::hello;
            hellos += TypeOf(d) == link::PacketType::hello ? 1u : 0u;
        }
        guest->outbox.clear();
    }
    Check(hello_only && hellos >= 1, "guest: only HELLOs after the CLOSE");
    // A guest on another endpoint (told busy while the first was live) is accepted at once.
    const Endpoint otherEp{0x0A000009u, 50001};
    auto other = MakeNode(link::Role::guest, otherEp, kHostEp, kSecret, true, "0.12.0", now + 2100, 9);
    for (std::uint64_t t = now + 2100; t < now + 2600 && other->session.state() != link::LinkState::connected; t += 20) {
        Timers({host.get(), other.get()}, t);
        Pump({host.get(), other.get()}, t);
    }
    Check(other->session.state() == link::LinkState::connected && host->session.state() == link::LinkState::connected &&
              host->session.session_number() == 2 && mgscoop::lab::SameEndpoint(host->session.peer(), otherEp),
          "after a CLOSE the host accepts the next join (another endpoint) within 0.5 s");
    auto idle = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 3);
    Check(!idle->session.OnPeerClose(10) && idle->session.state() == link::LinkState::listening, "no session: ignored");
    // Without a CLOSE the same second guest is busy while the first is live (the CLOSE is what freed the host).
    auto host2 = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 11);
    auto guest2 = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 12);
    std::uint64_t t2 = Connect(*host2, *guest2, 0);
    auto other2 = MakeNode(link::Role::guest, otherEp, kHostEp, kSecret, true, "0.12.0", t2, 13);
    for (std::uint64_t t = t2; t < t2 + 500; t += 20) {
        Timers({host2.get(), guest2.get(), other2.get()}, t);
        Pump({host2.get(), guest2.get(), other2.get()}, t);
    }
    Check(other2->session.state() == link::LinkState::rejected && other2->session.reject_reason() == link::RejectReason::busy,
          "control: busy without the CLOSE");
}

void GuestReconnecting() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    std::uint64_t now = Connect(*host, *guest, 0);
    // Nothing from the host for 2 s: reconnecting (the old session is kept).
    for (std::uint64_t t = now; t <= now + 2000; t += 20) {
        guest->session.OnTimer(t);
        guest->Collect();
        guest->outbox.clear();
    }
    now += 2000;
    Check(guest->session.state() == link::LinkState::reconnecting && guest->Saw(link::EventKind::reconnecting), "reconnecting at 2 s");
    // The path comes back: DATA of the old session resumes it.
    host->outbox.clear();
    Check(host->session.SendData(nullptr, 0, now), "the host is still connected (3 s not reached)");
    Pump({host.get(), guest.get()}, now + 10);
    Check(guest->session.state() == link::LinkState::connected && guest->session.session_number() == 1, "old session resumed");
}

void RateLimits() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    link::HmacKey wrong{};
    link::DeriveHandshakeKey(Psk(kOther), wrong);
    // A spoofed flood from a few sources: each source gets its burst of 16, then 8/s.
    const auto junk = MakeHello(wrong, 2, link::kModePlay, link::kRoleGuest, 9, "0.12.0");
    for (int i = 0; i < 4000; ++i) {
        const Endpoint spoof{0xC6336400u + static_cast<std::uint32_t>(i % 4), static_cast<std::uint16_t>(40000 + i % 4)};
        FeedUnauth(*host, junk, spoof, static_cast<std::uint64_t>(i / 20), "flood answers are small");
    }
    Check(host->Drops(link::DropReason::rate) > 3000 && host->Drops(link::DropReason::bad_mac) <= 4 * 16 + 4 * 2 + 8,
          "per-source buckets limit each spoofed source");
    host->outbox.clear();
    // The real guest is still admitted.
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 200, 2);
    Connect(*host, *guest, 200);
    Check(guest->session.state() == link::LinkState::connected, "the real guest connects during the flood");
    // The global MAC budget: 500 per second across sources.
    auto host2 = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    for (int i = 0; i < 2000; ++i) {
        const Endpoint spoof{0xC6336400u + static_cast<std::uint32_t>(i), 40000};
        host2->session.OnDatagram(junk.data(), junk.size(), spoof, 0);
    }
    Check(host2->Drops(link::DropReason::bad_mac) == 500 && host2->Drops(link::DropReason::rate) == 1500, "global MAC budget 500/s");
    // One machine cycling 200 source ports at 1000/s shares one bucket (keyed on
    // the address): the MAC budget stays available to the real guest.
    auto host3 = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    const std::uint32_t attacker = 0xC6336401u;
    for (int i = 0; i < 1000; ++i) {
        const Endpoint port{attacker, static_cast<std::uint16_t>(40000 + i % 200)};
        FeedUnauth(*host3, junk, port, static_cast<std::uint64_t>(i), "port-cycling flood answers are small");
    }
    Check(host3->Drops(link::DropReason::bad_mac) <= 16 + 8 + 1 && host3->Drops(link::DropReason::rate) >= 970,
          "port cycling: one bucket per address");
    host3->outbox.clear();
    auto guest3 = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 1000, 2);
    std::uint64_t t = 1000;
    for (int i = 0; i < 40 && guest3->session.state() != link::LinkState::connected; ++i, t += 20) {
        for (int k = 0; k < 20; ++k) {  // the flood goes on at 1000/s
            const Endpoint port{attacker, static_cast<std::uint16_t>(40000 + (i * 20 + k) % 200)};
            host3->session.OnDatagram(junk.data(), junk.size(), port, t);
        }
        host3->outbox.clear();
        Timers({host3.get(), guest3.get()}, t);
        Pump({host3.get(), guest3.get()}, t);
    }
    Check(guest3->session.state() == link::LinkState::connected, "the real guest gets a WELCOME during a port-cycling flood");
    // A spoofed flood from many addresses drains the global budget, but the
    // lost guest's address has its own: its reconnect still gets through.
    auto host4 = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest4 = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    t = Connect(*host4, *guest4, 0);
    for (const std::uint64_t end = t + 3100; t < end; t += 20) {
        host4->session.OnTimer(t);
        host4->outbox.clear();
    }
    host4->Collect();
    Check(host4->session.state() == link::LinkState::lost, "host4 lost");
    std::uint32_t spoofed = 0;
    const auto flood = [&](std::uint64_t at) {
        for (int k = 0; k < 40; ++k)  // 2000/s from ever new addresses
            host4->session.OnDatagram(junk.data(), junk.size(), Endpoint{0x0B000000u + spoofed++, 40000}, at);
        host4->outbox.clear();
    };
    for (const std::uint64_t end = t + 1000; t < end; t += 20) flood(t);
    const std::uint64_t rate_before = host4->Drops(link::DropReason::rate);
    Check(rate_before > 0, "the spoofed flood drains the global budget");
    auto back = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", t, 9);
    for (const std::uint64_t end = t + 3000; t < end && back->session.state() != link::LinkState::connected; t += 20) {
        flood(t);
        Timers({host4.get(), back.get()}, t);
        Pump({host4.get(), back.get()}, t);
    }
    Check(host4->Drops(link::DropReason::rate) > rate_before, "the flood still runs");
    Check(back->session.state() == link::LinkState::connected && host4->Saw(link::EventKind::reconnected),
          "the lost guest's address reconnects during a spoofed flood");
}

void Malformed() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    std::vector<std::uint8_t> junk(60, 0x41);
    FeedUnauth(*host, junk, kGuestEp, 0, "junk");
    Check(host->Drops(link::DropReason::malformed) == 1 && host->outbox.empty(), "junk is malformed, unanswered");
    // A short datagram of another version is never answered.
    std::vector<std::uint8_t> v3(100, 0);
    link::Header h{};
    h.type = link::PacketType::hello;
    h.body_len = 48;
    link::EncodeHeader(h, v3.data());
    v3[4] = 3;
    FeedUnauth(*host, v3, kGuestEp, 0, "short v3");
    Check(host->outbox.empty() && host->Drops(link::DropReason::malformed) == 2, "short v3: malformed, unanswered");
    // A 1200-byte one of another version gets one UNAUTH REJECT(proto_mismatch).
    std::vector<std::uint8_t> v3big(1200, 0);
    h.body_len = static_cast<std::uint16_t>(link::kHelloBodySize);
    link::EncodeHeader(h, v3big.data());
    v3big[4] = 3;
    FeedUnauth(*host, v3big, kGuestEp, 0, "v3 answer");
    Check(host->outbox.size() == 1 && host->outbox[0].bytes.size() == 88, "1200-byte v3: UNAUTH REJECT");
    link::Reject r{};
    Check(link::DecodeRejectBody(host->outbox[0].bytes.data() + link::kLinkHeaderSize, link::kRejectBodySize, r) &&
          r.reason == static_cast<std::uint8_t>(link::RejectReason::proto_mismatch), "proto_mismatch");
    // Types not allowed in the role.
    std::vector<std::uint8_t> welcome(104, 0);
    h.type = link::PacketType::welcome;
    h.body_len = static_cast<std::uint16_t>(link::kWelcomeBodySize);
    link::EncodeHeader(h, welcome.data());
    FeedUnauth(*host, welcome, kGuestEp, 0, "welcome at the host");
    Check(host->Drops(link::DropReason::unknown_msg) == 1, "WELCOME at the host: unknown_msg");
    std::vector<std::uint8_t> data(52, 0);
    h.type = link::PacketType::data;
    h.body_len = 0;
    h.session_id = 77;
    h.seq = 1;
    link::EncodeHeader(h, data.data());
    FeedUnauth(*host, data, kGuestEp, 0, "unknown session");
    Check(host->Drops(link::DropReason::wrong_session) == 1, "DATA of no session: wrong_session");
    FeedUnauth(*guest, data, kHostEp, 0, "guest unknown session");
    Check(guest->Drops(link::DropReason::wrong_session) == 1, "guest wrong_session");
    // The guest checks the source port of WELCOME and REJECT.
    FeedUnauth(*guest, welcome, Endpoint{kHostEp.address, 47831}, 0, "welcome from the wrong port");
    Check(guest->Drops(link::DropReason::wrong_endpoint) == 1, "WELCOME from another port: wrong_endpoint");
}

void SourceChanged() {
    // A multi-homed host answering from another address: accepted, counted, never re-targeted.
    auto host = MakeNode(link::Role::host, Endpoint{0x0A000005u, 47830}, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    guest->session.OnTimer(0);
    for (auto& d : guest->outbox) host->session.OnDatagram(d.bytes.data(), d.bytes.size(), kGuestEp, 0);
    guest->outbox.clear();
    for (int round = 0; round < 4; ++round) {
        for (auto& d : host->outbox) guest->session.OnDatagram(d.bytes.data(), d.bytes.size(), host->self, 10);
        host->outbox.clear();
        for (auto& d : guest->outbox) {
            Check(mgscoop::lab::SameEndpoint(d.to, kHostEp), "the guest sends only to its configured endpoint");
            host->session.OnDatagram(d.bytes.data(), d.bytes.size(), kGuestEp, 10);
        }
        guest->outbox.clear();
    }
    Check(guest->session.state() == link::LinkState::connected && guest->session.stats().src_changed >= 2, "src_changed counted");
}

// A slow relay delivers the guest's queued HELLOs to the host in one burst
// (kGuestNonces of them) before the guest sees any WELCOME. The guest commits
// on the first matching WELCOME (its oldest HELLO), so the host must still
// hold that HELLO's pending slot when the CONFIRM arrives (kPendingSlots).
void PendingBurst() {
    static_assert(link::kPendingSlots >= link::kGuestNonces, "one pending slot per nonce the guest still accepts");
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    std::vector<Datagram> hellos;
    for (std::uint64_t t = 0; hellos.size() < link::kGuestNonces && t < 5000; t += 100) {
        guest->session.OnTimer(t);
        guest->Collect();
        for (auto& d : guest->outbox) hellos.push_back(d);
        guest->outbox.clear();
    }
    Check(hellos.size() == link::kGuestNonces && guest->session.state() == link::LinkState::connecting, "4 HELLOs queued");
    for (const auto& d : hellos) host->session.OnDatagram(d.bytes.data(), d.bytes.size(), kGuestEp, 1600);
    host->Collect();
    Check(host->outbox.size() == link::kGuestNonces, "one WELCOME per queued HELLO");
    std::vector<Datagram> welcomes = host->outbox;
    host->outbox.clear();
    for (const auto& d : welcomes) {
        guest->session.OnDatagram(d.bytes.data(), d.bytes.size(), kHostEp, 1700);
        guest->Collect();
    }
    Check(guest->session.state() == link::LinkState::handshake && guest->outbox.size() == 1 &&
              guest->Drops(link::DropReason::unknown_msg) == link::kGuestNonces - 1,
          "the guest commits on the first WELCOME (the later ones are dropped)");
    Pump({host.get(), guest.get()}, 1710);
    Check(guest->session.state() == link::LinkState::connected && host->session.state() == link::LinkState::connected &&
              host->Saw(link::EventKind::connected) && host->session.session_number() == 1,
          "the host still holds the oldest HELLO's slot and accepts its CONFIRM");
    Check(host->Drops(link::DropReason::wrong_session) == 0 && guest->Drops(link::DropReason::wrong_session) == 0,
          "no wrong_session drops after a HELLO burst");
}

void GuestBackoff() {
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    unsigned first10 = 0, until10min = 0, after = 0;
    for (std::uint64_t t = 0; t < 1200000; t += 100) {
        guest->session.OnTimer(t);
        const auto n = static_cast<unsigned>(guest->outbox.size());
        guest->outbox.clear();
        if (t < 10000) first10 += n;
        else if (t < 600000) until10min += n;
        else after += n;
    }
    Check(first10 == 20, "every 500 ms for 10 s");
    Check(until10min >= 293 && until10min <= 296, "every 2 s until 10 minutes");
    Check(after >= 59 && after <= 61, "every 10 s after 10 minutes");
    guest->Collect();
    Check(guest->Count(link::EventKind::connecting) < 100, "connecting lines are sparse (first, then every 10th)");
}

void SeqExhaustion() {
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, true, "0.12.0", 0, 1);
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.12.0", 0, 2);
    const std::uint64_t now = Connect(*host, *guest, 0);
    guest->session.set_tx_seq_for_test(link::kSeqLimit - 2);
    Check(guest->session.SendData(nullptr, 0, now), "the last sequence below the limit");
    Check(!guest->session.SendData(nullptr, 0, now) && guest->session.state() == link::LinkState::reconnecting,
          "guest: exhaustion stops DATA and reconnects");
    host->session.set_tx_seq_for_test(link::kSeqLimit - 1);
    Check(!host->session.SendData(nullptr, 0, now) && host->session.state() == link::LinkState::lost, "host: exhaustion stops at lost");
    host->outbox.clear();
    guest->outbox.clear();
    for (std::uint64_t t = now; t < now + 5000 && guest->session.state() != link::LinkState::connected; t += 20) {
        Timers({host.get(), guest.get()}, t);
        Pump({host.get(), guest.get()}, t);
    }
    Check(guest->session.state() == link::LinkState::connected && guest->session.session_number() == 2, "a new session after exhaustion");
}

void ExactFilter() {
    // Lab loopback: the transport filters 127.0.0.1:peer; the session does not check ports.
    auto host = MakeNode(link::Role::host, kHostEp, Endpoint{}, kSecret, false, "0.12.0", 0, 1);
    auto guest = std::make_unique<Node>();
    guest->self = kGuestEp;
    guest->rng = 5;
    link::SessionConfig config{};
    config.role = link::Role::guest;
    config.exact_filter = true;
    config.peer = Endpoint{kHostEp.address, 1};  // another configured port
    CopyBuild(config.build, "0.12.0");
    config.psk = Psk(kSecret);
    guest->session.Start(config, link::SessionIo{guest.get(), &NodeSend, &NodeRandom}, 0);
    guest->session.OnTimer(0);
    for (auto& d : guest->outbox) host->session.OnDatagram(d.bytes.data(), d.bytes.size(), kGuestEp, 0);
    guest->outbox.clear();
    for (auto& d : host->outbox) guest->session.OnDatagram(d.bytes.data(), d.bytes.size(), kHostEp, 0);
    Check(guest->session.state() == link::LinkState::handshake && guest->Drops(link::DropReason::wrong_endpoint) == 0,
          "exact filter: no port check in the session");
}

void Names() {
    Check(std::strcmp(link::LinkStateName(link::LinkState::reconnecting), "reconnecting") == 0 &&
          std::strcmp(link::LinkStateName(link::LinkState::off), "off") == 0, "state names");
    Check(std::strcmp(link::RejectReasonName(link::RejectReason::busy), "busy") == 0 &&
          std::strcmp(link::RejectReasonName(link::RejectReason::role_mismatch), "role_mismatch") == 0, "reason names");
    Check(std::strcmp(link::DropReasonName(link::DropReason::unknown_msg), "unknown_msg") == 0 &&
          std::strcmp(link::DropReasonName(link::DropReason::malformed), "malformed") == 0, "drop names");
    Check(std::strcmp(link::HandshakeStepName(link::HandshakeStep::confirm_rx), "confirm_rx") == 0, "step names");
}

// 0.13.6 online-net-epoch: lab.ini link_session_skew (the lab and the worker's play rehearsal only) starts the
// host's counter at 2, so its first committed session is 3 while the fresh guest's is 1: the counters of the
// 0.13.5 online session after a guest relaunch, inside one hidden session. The numbers are local only (the
// handshake never carries them), so the link connects as usual; a restart never lowers the counter.
void SessionSkew() {
    auto host = std::make_unique<Node>();
    host->self = kHostEp;
    host->rng = 11;
    link::SessionConfig config{};
    config.role = link::Role::host;
    config.play = true;
    config.exact_filter = false;
    CopyBuild(config.build, "0.13.6");
    config.psk = Psk(kSecret);
    config.session_skew = 2;
    host->session.Start(config, link::SessionIo{host.get(), &NodeSend, &NodeRandom}, 1000);
    host->Collect();
    auto guest = MakeNode(link::Role::guest, kGuestEp, kHostEp, kSecret, true, "0.13.6", 1000, 12);
    Connect(*host, *guest, 1000);
    Check(host->session.state() == link::LinkState::connected && guest->session.state() == link::LinkState::connected,
          "skewed host: both connected");
    Check(host->session.session_number() == 3 && guest->session.session_number() == 1, "skew 2: host session 3, guest session 1");
    bool host_event = false;
    for (const auto& e : host->events) host_event = host_event || (e.kind == link::EventKind::connected && e.session == 3);
    Check(host_event, "the host's connected event names session 3 (link=connected; session=3)");
    // A second Start (another session object configuration) never lowers the counter.
    config.session_skew = 1;
    host->session.Start(config, link::SessionIo{host.get(), &NodeSend, &NodeRandom}, 5000);
    Check(host->session.session_number() == 3, "a smaller skew never lowers the counter");
    link::Session plain{};
    link::SessionConfig none{};
    none.role = link::Role::host;
    none.psk = Psk(kSecret);
    plain.Start(none, link::SessionIo{host.get(), &NodeSend, &NodeRandom}, 1000);
    Check(plain.session_number() == 0, "no skew: the counter starts at 0 (first session 1)");
}
} // namespace

int main() {
    Names();
    HandshakeAndData();
    ConfirmReplay();
    KeepaliveLoss();
    RebindBusy();
    ReplayedHello();
    WrongCode();
    UnauthRejectWhileConnected();
    RejectReasons();
    StaleReject();
    BusyAndTakeover();
    ReconnectSameEndpoint();
    PeerClose();
    GuestReconnecting();
    RateLimits();
    Malformed();
    SourceChanged();
    PendingBurst();
    GuestBackoff();
    SeqExhaustion();
    ExactFilter();
    SessionSkew();
    std::printf("link_session_tests: %u checks passed\n", checks);
    return 0;
}
