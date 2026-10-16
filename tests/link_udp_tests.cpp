// Link v2 over real sockets on 127.0.0.1 (docs/link-protocol.md): the
// session on lab::UdpTransport (handshake and data both ways, an ephemeral
// guest port, a wrong secret, a junk flood that must not starve the timers),
// the impairment heap (jitter, FIFO, dup, burst loss, transmit_to), and
// partner_net's link v2 path end to end against a test guest (FRAME
// delivery, reliable channels, the save handoff's channel 4 and hooks,
// link-status.json, freeze on loss).
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
#include "mgscoop/save_handoff_state.hpp"
#include "mgscoop/udp_transport.hpp"

// Background-worker builds (MGS_BACKGROUND_WORKER=1, also on partner_net) admit the
// worker-only input stall; every other build, the friend/Release loader's included, refuses it.
#ifndef MGS_BACKGROUND_WORKER
#define MGS_BACKGROUND_WORKER 0
#endif

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
char g_log[512][256];
volatile LONG g_log_count = 0;
SRWLOCK g_log_lock = SRWLOCK_INIT;
void Capture(const char* line) noexcept {
    AcquireSRWLockExclusive(&g_log_lock);
    if (g_log_count < 512) lstrcpynA(g_log[g_log_count++], line, 256);
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
    // Channel 4 (the save handoff): a real receiver, drained in order into
    // `stream` and acknowledged with its own RELACK; hold4 stops both.
    link::ReliableReceiver rx4;
    std::vector<std::uint8_t> stream;
    volatile LONG hold4 = 0, ch4_messages = 0;
};
TestGuest g_guest;
bool GuestSend(void*, const Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    const lab::WireChunk chunk{data, size};
    return g_guest.transport.transmit_to(to, &chunk, 1, true);
}
void GuestDatagram(void*, const std::uint8_t* data, std::size_t size, const Endpoint& from) noexcept {
    AcquireSRWLockExclusive(&g_guest.lock);
    const std::uint64_t now = NowMs();
    const link::Delivery d = g_guest.session.OnDatagram(data, size, from, now);
    if (d.data) {
        link::MessageReader reader(d.body, d.size);
        link::Message m{};
        bool ack4 = false;
        while (reader.Next(m)) {
            link::ReliableMsg r{};
            if (link::DecodeReliable(m, r) && r.channel == 4) {
                if (g_guest.hold4) continue;
                g_guest.rx4.Accept(r.id, r.payload, r.size, now);
                InterlockedIncrement(&g_guest.ch4_messages);
                std::uint8_t out[link::kMaxReliableMessage];
                std::size_t n = 0;
                while (g_guest.rx4.Drain(out, sizeof(out), n, now)) g_guest.stream.insert(g_guest.stream.end(), out, out + n);
                ack4 = true;
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::frame) && m.size <= sizeof(g_guest.frame)) {
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
        if (ack4) {
            std::uint16_t next = 0;
            std::uint32_t bits = 0;
            std::uint8_t free_slots = 0;
            g_guest.rx4.AckState(next, bits, free_slots);
            g_guest.rx4.ClearDirty();
            std::uint8_t body[16];
            link::BodyWriter w(body, sizeof(body));
            w.AddRelAck(4, free_slots, next, bits);
            g_guest.session.SendData(body, w.size(), now);
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

// Save handoff hooks (partner_net::SetHandoffHooks): call counters.
volatile LONG g_hook_idle = 0, g_hook_session = 0, g_hook_close = 0, g_hook_last_session = 0;
void HookIdle(std::uint64_t) noexcept { InterlockedIncrement(&g_hook_idle); }
void HookSession(std::uint32_t session) noexcept {
    InterlockedExchange(&g_hook_last_session, static_cast<LONG>(session));
    InterlockedIncrement(&g_hook_session);
}
void HookClose() noexcept { InterlockedIncrement(&g_hook_close); }
const mgscoop::partner_net::HandoffHooks kTestHooks{&HookIdle, &HookSession, &HookClose};

// The value token after "key": in a link-status document (a quoted string or
// a bare number), "" when the key is missing.
std::string StatusValue(const std::string& doc, const char* key) {
    const std::string needle = std::string("\"") + key + "\":";
    const std::size_t at = doc.find(needle);
    if (at == std::string::npos) return std::string();
    std::size_t begin = at + needle.size(), end = begin;
    if (end < doc.size() && doc[end] == '"') {
        ++end;
        while (end < doc.size() && doc[end] != '"') end += doc[end] == '\\' ? 2 : 1;
        ++end;
    } else {
        while (end < doc.size() && doc[end] != ',' && doc[end] != '}') ++end;
    }
    return doc.substr(begin, end - begin);
}

// Restarts the test guest's session (a new HELLO from the same endpoint: the
// host commits a new session) and waits for the given host session number.
bool RestartGuest(const link::SessionConfig& sc, std::uint32_t expected_session) {
    AcquireSRWLockExclusive(&g_guest.lock);
    g_guest.session.Start(sc, link::SessionIo{nullptr, &GuestSend, &NodeRandom}, NowMs());
    g_guest.rx4.Reset();
    g_guest.stream.clear();
    ReleaseSRWLockExclusive(&g_guest.lock);
    return WaitFor([&] {
        const auto info = mgscoop::partner_net::GetLinkInfo();
        return info.state == link::LinkState::connected && info.session == expected_session;
    }, 6000);
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
    net::SetHandoffHooks(&kTestHooks);
    Check(net::ReliableQueued(4) == 0, "ReliableQueued is 0 before Start");
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
    Check(std::strcmp(net::GetLinkInfo().remote_build, "0.12.0") == 0, "LinkInfo carries the peer's build");
    Check(WaitFor([] { return g_hook_session >= 1 && g_hook_last_session == 1 && g_hook_idle >= 3; }),
          "HandoffHooks: on_session after the new session, on_idle every link tick");
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
    // Input frames from the guest: the unknown slot/equip debounce in the receive path
    // (partner_input::HoldIdentity, docs/anim-round2-throws.md "Unknown-slot glitch").
    {
        namespace pi = mgscoop::partner_input;
        std::uint32_t in_seq = 0;
        const auto send_input = [&](const pi::InputPacket& p) {
            std::uint8_t f[mgscoop::sync::input_frame_size_max];
            const std::size_t n = mgscoop::sync::encode_input_frame_any(net::kSession, 30050, ++in_seq, p, f);
            std::uint8_t ib[512];
            link::BodyWriter iw(ib, sizeof(ib));
            iw.Add(link::MsgType::frame, f, n);
            return n && GuestSendBody(ib, iw.size());
        };
        const auto latest = [&](pi::Sample& s) { return WaitFor([&] { return net::LatestInput(s) && s.valid && s.seq == in_seq; }); };
        pi::InputPacket good = pi::Quantize(pi::PadSnapshot{}, 0);
        good.version = 5;
        good.held = 0x18;
        good.inv_slot = 3;
        good.xflags = pi::XFlag::Equip;
        good.gun_equip = 354;
        good.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming;
        good.gun_mode = 28;
        good.reload_seq = 4;
        good.switch_seq = 7;
        pi::InputPacket glitch = good;
        glitch.xflags = 0;
        glitch.gun_equip = 0;
        glitch.gun_flags = glitch.gun_mode = glitch.reload_seq = glitch.switch_seq = 0;
        Check(pi::ValidPacket(good) && pi::ValidPacket(glitch), "input frames valid");
        pi::Sample s{};
        net::SetSupportIdentity(354);
        Check(send_input(good) && latest(s) && s.packet.gun_equip == 354, "a complete support frame arrives");
        Check(send_input(glitch) && latest(s), "the 0c9269ce frame arrives");
        Check(s.packet.inv_slot == 3 && (s.packet.xflags & pi::XFlag::Equip) && s.packet.gun_equip == 354 &&
                  s.packet.gun_flags == good.gun_flags && s.packet.reload_seq == 4 && s.packet.switch_seq == 7 && s.packet.held == 0x18,
              "the receiver stores it with the last slot, equip and gun group (input untouched)");
        Check(WaitFor([] { return Logged("partner_support=slot_hold; slot=3; equip=354; reason=held; missing=both; age_ms="); }),
              "slot_hold held line");
        Check(send_input(good) && latest(s), "the next complete frame");
        Check(WaitFor([] { return Logged("partner_support=slot_hold; slot=3; equip=354; reason=resumed; packets=1; held_ms="); }),
              "slot_hold resumed line");
        net::SetSupportIdentity(0);
        Check(send_input(good) && latest(s) && send_input(glitch) && latest(s), "frames without a support verdict");
        Check(!(s.packet.xflags & pi::XFlag::Equip) && !(s.packet.gun_flags & pi::GunFlag::Valid),
              "no support verdict: the unknown frame is stored as it came (gun play unchanged)");
        // The worker-only input stall (docs/anim-round2-throws.md "Stall test step").
        const char* const stall = net::StallInput(320);
        if constexpr (MGS_BACKGROUND_WORKER != 0) {
            Check(stall == nullptr && Logged("partner_net=input_stall; reason=start; ms=320;"), "a worker build starts the stall");
            Check(!net::SubmitInput(good), "nothing goes out during the stall");
            Check(net::StallInput(320) != nullptr && std::strcmp(net::StallInput(320), "busy") == 0, "one stall at a time");
            Sleep(340);
            Check(net::SubmitInput(good), "the first frame after the stall goes out");
            Check(Logged("partner_net=input_stall; reason=end; ms=320; actual_ms="), "input_stall end line");
            Check(std::strcmp(net::StallInput(49), "range") == 0 && std::strcmp(net::StallInput(1001), "range") == 0, "stall range");
        } else {
            Check(stall != nullptr && std::strcmp(stall, "not_worker_build") == 0 && !Logged("partner_net=input_stall"),
                  "a build without the background worker (the friend/Release loader) refuses the input stall");
            Sleep(10);
            Check(net::SubmitInput(good), "sending is unchanged");
        }
    }
    // Reliable channels both ways.
    Check(net::SendReliable(0, "x", 1, link::ReliableMode::append) == link::ReliableResult::bad_channel, "channel 0 reserved");
    Check(net::SendReliable(5, "x", 1, link::ReliableMode::append) == link::ReliableResult::bad_channel, "channel 5 does not exist");
    Check(net::SendDatagram(4, "x", 1) == link::ReliableResult::bad_channel, "datagrams stay on channels 1..3");
    Check(net::ReliableQueued(0) == 0 && net::ReliableQueued(5) == 0, "ReliableQueued of an unknown channel is 0");
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
    // A refused partner pack for the published mission: no body (docs/player-package-selection.md).
    net::PublishPackState(30050);
    Check(WaitFor([&] { const auto t = ReadText(status); return t.find("\"partner_drive\":\"off\",\"partner_reason\":\"no_body_pack\"") != std::string::npos; },
                  3000), "PublishPackState: partner_reason no_body_pack");
    net::PublishPackState(30010);
    Check(WaitFor([&] { const auto t = ReadText(status); return t.find("no_body_pack") == std::string::npos; }, 3000),
          "a refusal for another mission does not apply");
    net::PublishPackState(0);
    // World sync keys (docs/link-protocol.md 7.2): after the transport block,
    // then the save handoff keys (join-fix phase 2), then time_utc.
    Check(WaitFor([&] {
              return ReadText(status).find("\"left\":0,\"world\":0,\"paired\":0,\"partner_mission\":0,\"pair_wait\":\"off\","
                                           "\"progress\":\"unknown\",\"handoff\":\"off\",\"handoff_pct\":0,\"time_utc\"") != std::string::npos;
          }, 3000), "world keys default to off before any publication");
    net::PublishWorldState({3, false, 30010, 9, 2});
    Check(WaitFor([&] {
              return ReadText(status).find("\"world\":3,\"paired\":0,\"partner_mission\":30010,\"pair_wait\":\"different_mission\","
                                           "\"progress\":\"story_start\",\"handoff\":") != std::string::npos;
          }, 3000), "PublishWorldState reaches the status file before the handoff keys");
    {
        const std::string doc = ReadText(status);
        Check(doc.find("\"partner_shown\":1,\"look\":1,\"mission\":30050") != std::string::npos && doc.size() < 1024,
              "the local mission key is unchanged and the document fits 1024 bytes");
    }
    net::PublishWorldState({3, true, 30050, 0, 1});
    Check(WaitFor([&] {
              return ReadText(status).find("\"world\":3,\"paired\":1,\"partner_mission\":30050,\"pair_wait\":\"paired\",\"progress\":\"ok\",") !=
                     std::string::npos;
          }, 3000), "paired world state");
    net::PublishWorldState({0, true, 30010, 9, 1});
    Check(WaitFor([&] {
              return ReadText(status).find("\"world\":0,\"paired\":0,\"partner_mission\":0,\"pair_wait\":\"off\",\"progress\":\"ok\",") !=
                     std::string::npos;
          }, 3000), "world level 0 is gated to off");
    net::PublishWorldState({3, false, 30010, 9, 0});
    // Save handoff link-status keys (P2-13): after the world keys, before time_utc.
    Check(WaitFor([&] { return ReadText(status).find("\"progress\":\"unknown\",\"handoff\":\"off\",\"handoff_pct\":0,\"time_utc\"") != std::string::npos; },
                  3000), "handoff keys default to off");
    net::PublishHandoffState(static_cast<std::uint8_t>(mgscoop::save_handoff_state::Token::receiving), 42);
    Check(WaitFor([&] { return ReadText(status).find("\"handoff\":\"receiving\",\"handoff_pct\":42,\"time_utc\"") != std::string::npos; }, 3000),
          "PublishHandoffState reaches the status file before time_utc");
    Check(ReadText(status).find("\"partner_shown\":1,\"look\":1,\"mission\":30050") != std::string::npos, "the partner keys are unchanged");
    {
        // The worst-case document stays under the 1536-byte buffers: every
        // published value at its longest, the remaining fields bounded by
        // their longest form and the four appended blocks by their buffers.
        net::PublishPartnerState(true, true, 0xFFFFFFFFu);
        net::PublishNativeState(9, 9, 9, 9);
        net::PublishWorldState({3, false, 0xFFFFFFFFu, 11, 2});
        net::PublishHandoffState(static_cast<std::uint8_t>(mgscoop::save_handoff_state::Token::partner_restarting), 100);
        Check(WaitFor([&] { return ReadText(status).find("\"handoff\":\"partner_restarting\",\"handoff_pct\":100,") != std::string::npos; }, 3000),
              "the longest handoff token is published");
        const std::string doc = ReadText(status);
        struct Bound { const char* key; std::size_t longest; };
        static const Bound kBounds[] = {
            {"pid", 10}, {"mode", 6}, {"role", 7}, {"state", 14}, {"reason", 41}, {"verified", 1}, {"session", 10}, {"peer", 25},
            {"local_port", 5}, {"rtt_ms", 10}, {"jitter_ms", 10}, {"loss_permille", 10}, {"resend", 10}, {"tx_Bps", 10},
            {"rx_Bps", 10}, {"interp_ms", 10}, {"remote_build", 35}, {"partner", 18}, {"partner_shown", 1}, {"look", 1},
            {"mission", 10}, {"saves", 12}, {"partner_drive", 9}, {"partner_reason", 24}, {"remote_drive", 9}, {"remote_reason", 24},
            {"time_utc", 22},
        };
        std::size_t worst = doc.size();
        for (const Bound& b : kBounds) {
            const std::string value = StatusValue(doc, b.key);
            Check(!value.empty() && value.size() <= b.longest, "a bounded status field");
            worst += b.longest - value.size();
        }
        // The partner, transport, world and handoff blocks between remote_reason and time_utc.
        const std::string reason_key = "\"remote_reason\":" + StatusValue(doc, "remote_reason") + ",";
        const std::size_t blocks_begin = doc.find(reason_key) + reason_key.size();
        const std::size_t blocks_end = doc.find("\"time_utc\"");
        Check(blocks_begin != std::string::npos && blocks_end != std::string::npos && blocks_end > blocks_begin, "the appended blocks");
        worst = worst - (blocks_end - blocks_begin) + (192 - 1) + (160 - 1) + (128 - 1) + (64 - 1);
        std::printf("link-status worst case: %zu bytes (this document %zu)\n", worst, doc.size());
        Check(worst < 1536, "a worst-case link-status document fits 1536 bytes");
        net::PublishPartnerState(true, true, 30050);
        net::PublishNativeState(0, 0, 0, 0);
        net::PublishWorldState({3, false, 30010, 9, 0});
        net::PublishHandoffState(0, 0);
    }
    // Channel 4 (the save handoff): 608,012 bytes in 1000-byte messages,
    // paced by ReliableQueued(4) < 16, drained in order by the guest.
    {
        std::vector<std::uint8_t> blob(608012);
        for (std::size_t i = 0; i < blob.size(); ++i) blob[i] = static_cast<std::uint8_t>((i * 31u + 7u) ^ (i >> 11));
        std::size_t offset = 0;
        unsigned max_queued = 0;
        const ULONGLONG until = GetTickCount64() + 60000;
        while (offset < blob.size() && GetTickCount64() < until) {
            const unsigned queued = net::ReliableQueued(4);
            if (queued > max_queued) max_queued = queued;
            if (queued >= 16) {
                Sleep(1);
                continue;
            }
            const std::size_t n = blob.size() - offset < 1000 ? blob.size() - offset : 1000;
            const auto result = net::SendReliable(4, blob.data() + offset, n, link::ReliableMode::append);
            if (result == link::ReliableResult::queued) offset += n;
            else if (result == link::ReliableResult::full) Sleep(1);
            else Check(false, "channel 4 accepts while connected");
        }
        Check(offset == blob.size(), "every chunk queued within 60 s");
        Check(max_queued <= 16, "pacing kept at most 16 messages in flight");
        Check(WaitFor([&] {
                  AcquireSRWLockShared(&g_guest.lock);
                  const bool done = g_guest.stream.size() == blob.size();
                  ReleaseSRWLockShared(&g_guest.lock);
                  return done;
              }, 20000), "the guest drained every byte");
        AcquireSRWLockShared(&g_guest.lock);
        const bool same = g_guest.stream == blob;
        ReleaseSRWLockShared(&g_guest.lock);
        Check(same, "channel-4 bytes equal, in order");
        Check(WaitFor([] { return net::ReliableQueued(4) == 0; }), "every channel-4 message acknowledged");
    }
    // A forced new session resets channel 4 (no resume) and calls on_session.
    {
        InterlockedExchange(&g_guest.hold4, 1);
        for (int i = 0; i < 5; ++i)
            Check(net::SendReliable(4, "held", 4, link::ReliableMode::append) == link::ReliableResult::queued, "held channel-4 message");
        Sleep(100);
        Check(net::ReliableQueued(4) == 5, "unacknowledged channel-4 messages stay queued");
        const LONG sessions = g_hook_session;
        InterlockedExchange(&g_guest.hold4, 0);
        Check(RestartGuest(sc, 2), "a new session from the same guest");
        Check(net::ReliableQueued(4) == 0, "the new session reset channel 4");
        Check(WaitFor([&] { return g_hook_session == sessions + 1 && g_hook_last_session == 2; }), "on_session for session 2");
    }
    // The guest's goodbye: on_peer_close after the CLOSE handling, then a rejoin.
    {
        const LONG closes = g_hook_close;
        link::BodyWriter cw(body, sizeof(body));
        Check(cw.Add(link::MsgType::close, nullptr, 0) && GuestSendBody(body, cw.size()), "guest sends CLOSE");
        Check(WaitFor([&] { return g_hook_close == closes + 1; }), "on_peer_close fired");
        Check(WaitFor([] { return Logged("link=peer_left; role=host; next=accepting"); }), "peer_left logged before the hook");
        Check(RestartGuest(sc, 3), "the guest rejoins after the goodbye");
        // The new sessions reset the pose ring: the guest's FRAME again.
        link::BodyWriter fw(body, sizeof(body));
        fw.Add(link::MsgType::frame, frame, sizeof(frame));
        Check(GuestSendBody(body, fw.size()), "guest sends a FRAME again");
        Check(WaitFor([] { return net::SampleRemote(0).valid && net::SampleRemote(0).x == 4.0f; }), "remote pose after the rejoin");
    }
    // The guest goes away: lost at 3 s; a held pose then reads lost (freeze).
    g_guest.transport.stop();
    Check(WaitFor([] { return net::GetLinkInfo().state == link::LinkState::lost; }, 5000), "host lost");
    Check(WaitFor([] { return Logged("link=lost; after_ms="); }), "link=lost line");
    Check(WaitFor([&] {
              return ReadText(status).find("\"world\":3,\"paired\":0,\"partner_mission\":0,\"pair_wait\":\"no_link\",\"progress\":\"unknown\",") !=
                     std::string::npos;
          }, 3000), "a link that is not connected is gated to no_link");
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
