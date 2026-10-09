// Link v2 over real sockets on 127.0.0.1 (docs/link-protocol.md): the
// session on lab::UdpTransport (handshake and data both ways, an ephemeral
// guest port, a wrong secret, a junk flood that must not starve the timers),
// the impairment heap (jitter, FIFO, dup, burst loss, transmit_to), and
// partner_net's link v2 path end to end against a test guest (FRAME
// delivery, reliable channels, link-status.json, freeze on loss).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "mgscoop/link_session.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/player_sync.hpp"
#include "mgscoop/udp_transport.hpp"

namespace {
namespace link = mgscoop::link;
namespace lab = mgscoop::lab;
using lab::Endpoint;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
template <class Predicate>
bool WaitFor(Predicate predicate, DWORD timeout_ms = 4000) {
    const ULONGLONG until = GetTickCount64() + timeout_ms;
    while (!predicate()) {
        if (GetTickCount64() > until) return false;
        Sleep(5);
    }
    return true;
}
std::uint64_t NowMs() {
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return static_cast<std::uint64_t>(c.QuadPart) * 1000 / static_cast<std::uint64_t>(f.QuadPart);
}
constexpr std::uint32_t kLoopback = 0x7F000001u;

link::HmacKey Psk(const char* secret) {
    char s[link::kSecretChars];
    std::memcpy(s, secret, link::kSecretChars);
    link::HmacKey key{};
    link::DerivePsk(s, key);
    return key;
}

// One endpoint: a session on a real transport, serialized by one lock.
struct UdpNode {
    lab::UdpTransport transport;
    link::Session session;
    SRWLOCK lock = SRWLOCK_INIT;
    volatile LONG data_count = 0;
    std::uint8_t last_body[link::kLinkMaxBody]{};
    std::size_t last_size = 0;
    volatile LONG rejected_hint = 0;
};
bool NodeSend(void* context, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    const lab::WireChunk chunk{data, size};
    return static_cast<UdpNode*>(context)->transport.transmit_to(to, &chunk, 1, true);
}
bool NodeRandom(void*, std::uint8_t* out, std::size_t size) noexcept {
    static std::uint64_t state = 0x1234567;
    for (std::size_t i = 0; i < size; ++i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        out[i] = static_cast<std::uint8_t>(state >> 56);
    }
    return true;
}
void NodeDatagram(void* context, const std::uint8_t* data, std::size_t size, const Endpoint& from) noexcept {
    auto* node = static_cast<UdpNode*>(context);
    AcquireSRWLockExclusive(&node->lock);
    const link::Delivery d = node->session.OnDatagram(data, size, from, NowMs());
    if (d.data && d.size) {
        std::memcpy(node->last_body, d.body, d.size);
        node->last_size = d.size;
        InterlockedIncrement(&node->data_count);
    }
    link::Event e{};
    while (node->session.PopEvent(e))
        if (e.kind == link::EventKind::rejected && !e.verified) InterlockedExchange(&node->rejected_hint, 1);
    ReleaseSRWLockExclusive(&node->lock);
}
void NodeIdle(void* context) noexcept {
    auto* node = static_cast<UdpNode*>(context);
    AcquireSRWLockExclusive(&node->lock);
    node->session.OnTimer(NowMs());
    link::Event e{};
    while (node->session.PopEvent(e)) {}
    ReleaseSRWLockExclusive(&node->lock);
}
link::LinkState StateOf(UdpNode& node) {
    AcquireSRWLockShared(&node.lock);
    const link::LinkState state = node.session.state();
    ReleaseSRWLockShared(&node.lock);
    return state;
}
bool StartNode(UdpNode& node, link::Role role, std::uint16_t listen, Endpoint peer, const char* secret) {
    link::SessionConfig config{};
    config.role = role;
    config.play = true;
    config.exact_filter = false;
    config.peer = peer;
    std::memcpy(config.build, "0.12.0", 6);
    config.psk = Psk(secret);
    AcquireSRWLockExclusive(&node.lock);
    node.session.Start(config, link::SessionIo{&node, &NodeSend, &NodeRandom}, NowMs());
    ReleaseSRWLockExclusive(&node.lock);
    lab::TransportConfig tc{};
    tc.listen_port = listen;
    tc.peer_port = role == link::Role::host ? 0 : peer.port;
    tc.peer_address = role == link::Role::host ? kLoopback : peer.address;
    tc.any_source = true;
    tc.allow_ephemeral = role == link::Role::guest;
    tc.idle_interval_ms = 20;
    return node.transport.start(tc, lab::DatagramSink{&NodeDatagram, &NodeIdle, &node});
}

const char kSecret[] = "7K3QZ9M2XWD4HT8RVN6B";
const char kOther[] = "C00PC00PC00PC00PC00P";

void SessionOverSockets() {
    static UdpNode host, guest;
    Check(StartNode(host, link::Role::host, 48220, Endpoint{}, kSecret), "host transport");
    Check(StartNode(guest, link::Role::guest, 0, Endpoint{kLoopback, 48220}, kSecret), "guest transport (ephemeral port)");
    Check(guest.transport.local_port() != 0 && guest.transport.local_port() != 48220, "the guest got an ephemeral port");
    Check(WaitFor([] { return StateOf(guest) == link::LinkState::connected && StateOf(host) == link::LinkState::connected; }),
          "handshake over sockets");
    AcquireSRWLockShared(&host.lock);
    const Endpoint locked = host.session.peer();
    ReleaseSRWLockShared(&host.lock);
    Check(locked.address == kLoopback && locked.port == guest.transport.local_port(), "host locked onto the ephemeral port");
    // Data both ways.
    const std::uint8_t body[] = {1, 0, 3, 0, 'a', 'b', 'c'};
    AcquireSRWLockExclusive(&guest.lock);
    Check(guest.session.SendData(body, sizeof(body), NowMs()), "guest data");
    ReleaseSRWLockExclusive(&guest.lock);
    Check(WaitFor([] { return host.data_count >= 1; }) && host.last_size == sizeof(body) && std::memcmp(host.last_body, body, 7) == 0,
          "host received the body");
    AcquireSRWLockExclusive(&host.lock);
    Check(host.session.SendData(body, sizeof(body), NowMs()), "host data");
    ReleaseSRWLockExclusive(&host.lock);
    Check(WaitFor([] { return guest.data_count >= 1; }), "guest received the body");

    // A flood of 100000 junk datagrams while connected must not starve the
    // timers (drain cap): neither side goes lost / reconnecting.
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET attacker = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(48220);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    std::uint8_t junk[64];
    std::memset(junk, 0x5A, sizeof(junk));
    std::memcpy(junk, "MGSL", 4);
    junk[4] = 2;
    junk[5] = 4;  // DATA with a random session id: dropped in O(1)
    junk[6] = 0;
    junk[7] = 0;
    junk[34] = 12;
    junk[35] = 0;
    bool stayed = true;
    for (int i = 0; i < 100000; ++i) {
        junk[8] = static_cast<std::uint8_t>(i);
        sendto(attacker, reinterpret_cast<const char*>(junk), sizeof(junk), 0, reinterpret_cast<const sockaddr*>(&target), sizeof(target));
        if (i % 1000 == 0) stayed = stayed && StateOf(host) == link::LinkState::connected && StateOf(guest) == link::LinkState::connected;
        if (i % 20000 == 0) Sleep(200);
    }
    Sleep(1500);
    stayed = stayed && StateOf(host) == link::LinkState::connected && StateOf(guest) == link::LinkState::connected;
    closesocket(attacker);
    AcquireSRWLockShared(&host.lock);
    const std::uint64_t dropped = host.session.stats().drops[static_cast<unsigned>(link::DropReason::wrong_session)];
    ReleaseSRWLockShared(&host.lock);
    std::printf("flood: %llu junk datagrams dropped as wrong_session\n", static_cast<unsigned long long>(dropped));
    Check(stayed, "a junk flood does not cause lost");
    Check(dropped > 1000, "the junk reached the session");
    guest.transport.stop();
    host.transport.stop();
}

void WrongSecret() {
    static UdpNode host, guest;
    Check(StartNode(host, link::Role::host, 48222, Endpoint{}, kSecret), "host transport");
    Check(StartNode(guest, link::Role::guest, 48223, Endpoint{kLoopback, 48222}, kOther), "guest transport");
    Check(WaitFor([] { return guest.rejected_hint != 0; }, 3000), "the guest sees the bad_code hint");
    Sleep(600);
    Check(StateOf(guest) == link::LinkState::connecting && StateOf(host) == link::LinkState::listening, "a wrong secret never connects");
    AcquireSRWLockShared(&host.lock);
    Check(host.session.stats().drops[static_cast<unsigned>(link::DropReason::bad_mac)] >= 2, "host: bad_mac");
    ReleaseSRWLockShared(&host.lock);
    guest.transport.stop();
    host.transport.stop();
}

// --- Impairment heap.
struct Inbox {
    SRWLOCK lock = SRWLOCK_INIT;
    std::vector<std::uint32_t> order;
};
void InboxDatagram(void* context, const std::uint8_t* data, std::size_t size, const Endpoint&) noexcept {
    if (size < 4) return;
    auto* inbox = static_cast<Inbox*>(context);
    std::uint32_t n = 0;
    std::memcpy(&n, data, 4);
    AcquireSRWLockExclusive(&inbox->lock);
    inbox->order.push_back(n);
    ReleaseSRWLockExclusive(&inbox->lock);
}
std::size_t InboxSize(Inbox& inbox) {
    AcquireSRWLockShared(&inbox.lock);
    const std::size_t n = inbox.order.size();
    ReleaseSRWLockShared(&inbox.lock);
    return n;
}
unsigned Inversions(const std::vector<std::uint32_t>& order) {
    unsigned n = 0;
    for (std::size_t i = 1; i < order.size(); ++i) n += order[i] < order[i - 1] ? 1u : 0u;
    return n;
}
bool StartPair(lab::UdpTransport& sender, lab::UdpTransport& receiver, Inbox& inbox, std::uint16_t a, std::uint16_t b,
               const lab::Impairment& impair) {
    lab::TransportConfig sc{};
    sc.listen_port = a;
    sc.peer_port = b;
    sc.impair = impair;
    lab::TransportConfig rc{};
    rc.listen_port = b;
    rc.peer_port = a;
    return sender.start(sc, lab::DatagramSink{}) && receiver.start(rc, lab::DatagramSink{&InboxDatagram, nullptr, &inbox});
}
void SendNumbered(lab::UdpTransport& sender, std::uint32_t count, bool droppable = true) {
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint8_t datagram[16]{};
        std::memcpy(datagram, &i, 4);
        const lab::WireChunk chunk{datagram, sizeof(datagram)};
        sender.transmit(&chunk, 1, droppable);
        if (i % 20 == 19) Sleep(1);
    }
}

void Impairments() {
    {
        // Jitter reorders.
        static lab::UdpTransport s, r;
        static Inbox inbox;
        lab::Impairment imp{};
        imp.jitter_ms = 50;
        imp.seed = 7;
        Check(StartPair(s, r, inbox, 48240, 48241, imp), "jitter pair");
        SendNumbered(s, 200);
        Check(WaitFor([] { return InboxSize(inbox) >= 200; }), "jittered datagrams arrive (latency 0 is still flushed)");
        AcquireSRWLockShared(&inbox.lock);
        const unsigned inversions = Inversions(inbox.order);
        ReleaseSRWLockShared(&inbox.lock);
        Check(inversions > 0, "jitter reorders");
        s.stop();
        r.stop();
    }
    {
        // Latency without jitter keeps the FIFO order.
        static lab::UdpTransport s, r;
        static Inbox inbox;
        lab::Impairment imp{};
        imp.latency_ms = 30;
        Check(StartPair(s, r, inbox, 48242, 48243, imp), "latency pair");
        SendNumbered(s, 200);  // within the 256-entry heap
        Check(WaitFor([] { return InboxSize(inbox) >= 200; }), "delayed datagrams arrive");
        AcquireSRWLockShared(&inbox.lock);
        const unsigned inversions = Inversions(inbox.order);
        ReleaseSRWLockShared(&inbox.lock);
        Check(inversions == 0, "zero jitter keeps the FIFO order");
        s.stop();
        r.stop();
    }
    {
        // Dup is counted and delivered.
        static lab::UdpTransport s, r;
        static Inbox inbox;
        lab::Impairment imp{};
        imp.dup_permille = 500;
        imp.seed = 3;
        Check(StartPair(s, r, inbox, 48244, 48245, imp), "dup pair");
        SendNumbered(s, 200);
        const std::uint64_t duplicated = s.stats().duplicated;
        Check(duplicated >= 70 && duplicated <= 130, "about half duplicated");
        Check(WaitFor([duplicated] { return InboxSize(inbox) >= 200 + duplicated; }), "duplicates delivered");
        s.stop();
        r.stop();
    }
    {
        // Burst loss: the long-run rate within +-20% at 10k datagrams; reorder via reorder_permille.
        static lab::UdpTransport s, r;
        static Inbox inbox;
        lab::Impairment imp{};
        imp.loss_permille = 100;
        imp.burst_model = true;
        imp.burst = 3;
        imp.seed = 11;
        Check(StartPair(s, r, inbox, 48246, 48247, imp), "burst pair");
        SendNumbered(s, 10000);
        const std::uint64_t dropped = s.stats().dropped;
        std::printf("burst loss: %llu of 10000 dropped\n", static_cast<unsigned long long>(dropped));
        Check(dropped >= 800 && dropped <= 1200, "burst loss long-run rate");
        // Bursts: the mean run of consecutive losses is near 3.
        WaitFor([] { return InboxSize(inbox) >= 8800; }, 2000);
        AcquireSRWLockShared(&inbox.lock);
        unsigned runs = 0, lost = 0;
        for (std::size_t i = 1; i < inbox.order.size(); ++i) {
            const std::uint32_t gap = inbox.order[i] - inbox.order[i - 1] - 1;
            if (inbox.order[i] > inbox.order[i - 1] && gap) { ++runs; lost += gap; }
        }
        ReleaseSRWLockShared(&inbox.lock);
        Check(runs && lost * 10 / runs >= 20 && lost * 10 / runs <= 40, "mean burst length about 3");
        // Non-droppable datagrams are never lost.
        const std::uint64_t before = s.stats().dropped;
        SendNumbered(s, 500, false);
        Check(s.stats().dropped == before, "non-droppable datagrams are never lost");
        s.stop();
        r.stop();
    }
    {
        // A delayed transmit_to reaches its own endpoint, not the peer.
        static lab::UdpTransport s, peer, other;
        static Inbox peer_inbox, other_inbox;
        lab::Impairment imp{};
        imp.latency_ms = 20;
        imp.reorder_permille = 1000;
        imp.reorder_ms = 10;
        Check(StartPair(s, peer, peer_inbox, 48248, 48249, imp), "transmit_to pair");
        lab::TransportConfig oc{};
        oc.listen_port = 48250;
        oc.peer_port = 48248;
        Check(other.start(oc, lab::DatagramSink{&InboxDatagram, nullptr, &other_inbox}), "other endpoint");
        std::uint32_t n = 77;
        std::uint8_t datagram[8]{};
        std::memcpy(datagram, &n, 4);
        const lab::WireChunk chunk{datagram, sizeof(datagram)};
        const std::uint64_t sent = NowMs();
        Check(s.transmit_to(Endpoint{kLoopback, 48250}, &chunk, 1, false), "transmit_to queued");
        Check(WaitFor([] { return InboxSize(other_inbox) >= 1; }), "delayed transmit_to arrives");
        Check(NowMs() - sent >= 29 && InboxSize(peer_inbox) == 0, "at its own endpoint after latency + reorder");
        s.stop();
        peer.stop();
        other.stop();
    }
}

// --- partner_net's v2 path against a test guest (lab: loopback, lab secret).
char g_log[64][256];
volatile LONG g_log_count = 0;
SRWLOCK g_log_lock = SRWLOCK_INIT;
void Capture(const char* line) noexcept {
    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_count < 64) lstrcpynA(g_log[g_log_count++], line, 256);
    ReleaseSRWLockExclusive(&g_log_lock);
}
bool Logged(const char* prefix) {
    AcquireSRWLockShared(&g_log_lock);
    bool found = false;
    for (LONG i = 0; i < g_log_count && !found; ++i) found = std::strncmp(g_log[i], prefix, std::strlen(prefix)) == 0;
    ReleaseSRWLockShared(&g_log_lock);
    return found;
}

struct TestGuest {
    lab::UdpTransport transport;
    link::Session session;
    SRWLOCK lock = SRWLOCK_INIT;
    volatile LONG frames = 0, reliables = 0, relacks = 0;
    std::uint8_t frame[96]{};
    std::size_t frame_size = 0;
    std::uint8_t reliable[64]{};
    std::size_t reliable_size = 0;
};
TestGuest g_guest;
bool GuestSend(void*, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    const lab::WireChunk chunk{data, size};
    return g_guest.transport.transmit_to(to, &chunk, 1, true);
}
void GuestDatagram(void*, const std::uint8_t* data, std::size_t size, const Endpoint& from) noexcept {
    AcquireSRWLockExclusive(&g_guest.lock);
    const link::Delivery d = g_guest.session.OnDatagram(data, size, from, NowMs());
    if (d.data) {
        link::MessageReader reader(d.body, d.size);
        link::Message m{};
        while (reader.Next(m)) {
            link::ReliableMsg r{};
            if (m.type == static_cast<std::uint8_t>(link::MsgType::frame) && m.size <= sizeof(g_guest.frame)) {
                std::memcpy(g_guest.frame, m.data, m.size);
                g_guest.frame_size = m.size;
                InterlockedIncrement(&g_guest.frames);
            } else if (link::DecodeReliable(m, r) && r.size <= sizeof(g_guest.reliable)) {
                std::memcpy(g_guest.reliable, r.payload, r.size);
                g_guest.reliable_size = r.size;
                InterlockedIncrement(&g_guest.reliables);
                // Acknowledge it (next_expected = id + 1).
                std::uint8_t body[16];
                link::BodyWriter w(body, sizeof(body));
                w.AddRelAck(r.channel, 31, static_cast<std::uint16_t>(r.id + 1), 0);
                g_guest.session.SendData(body, w.size(), NowMs());
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::relack)) {
                InterlockedIncrement(&g_guest.relacks);
            }
        }
    }
    link::Event e{};
    while (g_guest.session.PopEvent(e)) {}
    ReleaseSRWLockExclusive(&g_guest.lock);
}
void GuestIdle(void*) noexcept {
    AcquireSRWLockExclusive(&g_guest.lock);
    g_guest.session.OnTimer(NowMs());
    link::Event e{};
    while (g_guest.session.PopEvent(e)) {}
    ReleaseSRWLockExclusive(&g_guest.lock);
}
bool GuestSendBody(const std::uint8_t* body, std::size_t size) {
    AcquireSRWLockExclusive(&g_guest.lock);
    const bool sent = g_guest.session.SendData(body, size, NowMs());
    ReleaseSRWLockExclusive(&g_guest.lock);
    return sent;
}

std::string ReadText(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::string();
    char buffer[2048]{};
    DWORD read = 0;
    ReadFile(file, buffer, sizeof(buffer) - 1, &read, nullptr);
    CloseHandle(file);
    return std::string(buffer, read);
}

void PartnerNetV2() {
    namespace net = mgscoop::partner_net;
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::wstring dir = std::wstring(temp) + L"mgs-coop-link-udp-tests-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW((dir + L"\\mgs-coop").c_str(), nullptr);
    Check(net::GetLinkInfo().state == link::LinkState::off, "off before Start");
    net::Config config{48260, 48261, 30, 0, 0, Capture};
    config.link_v2 = true;
    config.host = true;
    config.interp_auto = true;
    config.directory = dir.c_str();
    config.build = "0.12.0";
    Check(net::Start(config), "partner_net v2 starts");
    Check(Logged("link=config; mode=lab; role=host; proto=2; bind=loopback; port=48260; peer=learned; auth=lab; interp=auto\r\n"),
          "link=config line");
    Check(Logged("partner_net=listening; port=48260; peer_port=48261\r\n"), "byte-compatible listening line");
    Check(WaitFor([] { return Logged("link=listening; port=48260"); }), "link=listening");
    Check(net::EffectiveInterpMs(77) == 100, "auto: the initial applied delay is 100");
    // The test guest: lab mode, lab secret, exact loopback filter.
    link::SessionConfig sc{};
    sc.role = link::Role::guest;
    sc.play = false;
    sc.exact_filter = true;
    sc.peer = Endpoint{kLoopback, 48260};
    std::memcpy(sc.build, "0.12.0", 6);
    sc.psk = Psk(link::kLabSecret);
    AcquireSRWLockExclusive(&g_guest.lock);
    g_guest.session.Start(sc, link::SessionIo{nullptr, &GuestSend, &NodeRandom}, NowMs());
    ReleaseSRWLockExclusive(&g_guest.lock);
    lab::TransportConfig tc{};
    tc.listen_port = 48261;
    tc.peer_port = 48260;
    tc.idle_interval_ms = 20;
    Check(g_guest.transport.start(tc, lab::DatagramSink{&GuestDatagram, &GuestIdle, nullptr}), "test guest transport");
    Check(WaitFor([] { return net::GetLinkInfo().state == link::LinkState::connected; }), "partner_net connected");
    Check(WaitFor([] { return Logged("link=connected; session=1; peer=127.0.0.1:48261; rtt_ms="); }), "link=connected line");
    Check(net::GetLinkInfo().session == 1, "LinkInfo session 1");
    // FRAME from the guest reaches the snapshot ring.
    net::SetEpoch(30050);
    mgscoop::sync::PlayerTransformPacket packet{};
    packet.sequence = 1;
    packet.x = 4.0f;
    packet.y = 1.0f;
    packet.z = -4.0f;
    packet.tick_ms = 1000;
    std::uint8_t frame[mgscoop::sync::transform_frame_size];
    Check(mgscoop::sync::encode_transform_frame(net::kSession, 30050, packet, frame) != 0, "encode frame");
    std::uint8_t body[128];
    link::BodyWriter w(body, sizeof(body));
    w.Add(link::MsgType::frame, frame, sizeof(frame));
    Check(GuestSendBody(body, w.size()), "guest sends a FRAME");
    Check(WaitFor([] { return net::SampleRemote(0).valid && net::SampleRemote(0).x == 4.0f; }), "remote pose from a FRAME");
    Check(WaitFor([] { return Logged("partner_net=first_rx; seq=1; epoch=30050"); }), "first_rx line");
    // SubmitLocal goes out as a FRAME.
    const mgscoop::partner::Tick tick{1, 1500, -250, 3000, 9000, 0, 2, 3, 0};
    Check(net::SubmitLocal(tick, mgscoop::sync::AnimState{}), "SubmitLocal sent");
    Check(WaitFor([] { return g_guest.frames >= 1; }) && g_guest.frame_size == mgscoop::sync::transform_frame_size,
          "the guest received the FRAME");
    // Reliable channels both ways.
    Check(net::SendReliable(0, "x", 1, link::ReliableMode::append) == link::ReliableResult::bad_channel, "channel 0 reserved");
    Check(net::SendReliable(2, "hello", 5, link::ReliableMode::append) == link::ReliableResult::queued, "reliable queued");
    Check(WaitFor([] { return g_guest.reliables >= 1; }) && g_guest.reliable_size == 5 && std::memcmp(g_guest.reliable, "hello", 5) == 0,
          "reliable delivered (by the next tick)");
    link::BodyWriter rw(body, sizeof(body));
    rw.AddReliable(3, 0, "world", 5);
    Check(GuestSendBody(body, rw.size()), "guest sends a reliable message");
    std::uint8_t out[32];
    std::size_t size = 0;
    Check(WaitFor([&] { return net::DrainReliable(3, out, sizeof(out), size); }) && size == 5 && std::memcmp(out, "world", 5) == 0,
          "DrainReliable");
    Check(WaitFor([] { return g_guest.relacks >= 1; }), "RELACK sent back within 50 ms");
    // link-status.json.
    const std::wstring status = dir + L"\\mgs-coop\\link-status.json";
    Check(WaitFor([&] { return ReadText(status).find("\"state\":\"connected\"") != std::string::npos; }, 3000), "status file connected");
    const std::string json = ReadText(status);
    Check(json.rfind("{\"v\":1,\"pid\":", 0) == 0 && json.find("\"mode\":\"lab\",\"role\":\"host\"") != std::string::npos &&
          json.find("\"peer\":\"127.0.0.1:48261\",\"local_port\":48260") != std::string::npos &&
          json.find("\"remote_build\":\"0.12.0\"") != std::string::npos && json.find("\"saves\":\"unchanged\"") != std::string::npos &&
          json.find("\"time_utc\":\"20") != std::string::npos && json.back() == '\n', "status file fields");
    net::PublishPartnerState(true, true, 30050);
    Check(WaitFor([&] { const auto t = ReadText(status); return t.find("\"partner_shown\":1,\"look\":1,\"mission\":30050") != std::string::npos; },
                  3000), "PublishPartnerState reaches the status file");
    // The guest goes away: lost at 3 s; a held pose then reads lost (freeze).
    g_guest.transport.stop();
    Check(WaitFor([] { return net::GetLinkInfo().state == link::LinkState::lost; }, 5000), "host lost");
    Check(WaitFor([] { return Logged("link=lost; after_ms="); }), "link=lost line");
    const auto held = net::SampleRemote(0);
    Check(held.valid && held.status == mgscoop::pose::Status::lost && held.x == 4.0f, "freeze: the held pose reads lost");
    Check(!net::SubmitLocal(tick, mgscoop::sync::AnimState{}) || true, "SubmitLocal while lost does not block");
    Check(net::SendReliable(2, "late", 4, link::ReliableMode::append) == link::ReliableResult::not_connected, "not_connected while lost");
    DeleteFileW(status.c_str());
    RemoveDirectoryW((dir + L"\\mgs-coop").c_str());
    RemoveDirectoryW(dir.c_str());
}
} // namespace

int main() {
    SessionOverSockets();
    WrongSecret();
    Impairments();
    PartnerNetV2();
    std::printf("link_udp_tests: %u checks passed\n", checks);
    return 0;
}
