// Unit tests for the co-op lab ISteamNetworking005 loopback emulator, its
// synthetic 1202/1203 callback dispatch, the UDP transport fault injection and
// the forwarding-proxy thunks.
//
// The emulator core is wired to an injected wire + clock so the P2P session
// state machine is exercised deterministically with no sockets; further groups
// drive the emulated vtable with the game's exact call shapes, two cores over
// real 127.0.0.1 UDP sockets, the transport's loss/latency path and the
// process-wide integration (fail-closed getter).
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#include "mgscoop/steam_abi.hpp"
#include "mgscoop/steam_callbacks.hpp"
#include "mgscoop/steam_identity.hpp"
#include "mgscoop/steam_loopback.hpp"
#include "mgscoop/udp_transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace mgscoop::lab;
namespace steam = mgscoop::steam;

namespace {
unsigned g_checks = 0;

// Bounded copy (avoids the CRT-deprecated strcpy/strncpy under /W4 /WX).
void CopyText(char* dst, std::size_t size, const char* src) {
    std::size_t i = 0;
    for (; src && src[i] && i + 1 < size; ++i) dst[i] = src[i];
    if (size) dst[i] = 0;
}
void Check(bool value, const char* what) {
    ++g_checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", g_checks, what);
        std::exit(1);
    }
}

constexpr std::uint64_t kHostId = 0x0110000100000001ull;
constexpr std::uint64_t kGuestId = 0x0110000100000002ull;
constexpr std::uint64_t kOtherId = 0x0110000100000099ull;

struct Recorder {
    std::uint64_t requests[64]{};
    unsigned request_count = 0;
    std::uint64_t fail_remote[64]{};
    std::uint8_t fail_error[64]{};
    unsigned fail_count = 0;
    bool refuse_requests = false;
    unsigned refused = 0;
};
bool rec_request(void* ctx, std::uint64_t remote) noexcept {
    auto* r = static_cast<Recorder*>(ctx);
    if (r->refuse_requests) {
        ++r->refused;
        return false;
    }
    if (r->request_count < 64) r->requests[r->request_count++] = remote;
    return true;
}
bool rec_fail(void* ctx, std::uint64_t remote, std::uint8_t error) noexcept {
    auto* r = static_cast<Recorder*>(ctx);
    if (r->fail_count < 64) {
        r->fail_remote[r->fail_count] = remote;
        r->fail_error[r->fail_count] = error;
        ++r->fail_count;
    }
    return true;
}

// In-memory wire: encode -> (optional cut/drop) -> decode -> deliver to the peer.
struct Link {
    LoopbackCore* dst = nullptr;
    std::uint32_t loss_permille = 0;
    std::uint64_t counter = 0;
    bool cut = false;  // peer crashed / unreachable
    unsigned sent[5]{};
};
bool wire_deliver(void* ctx, const DatagramHeader& header, const WireChunk* chunks, unsigned count,
                  bool droppable) noexcept {
    auto* link = static_cast<Link*>(ctx);
    ++link->sent[static_cast<unsigned>(header.type)];
    if (link->cut) return true;
    if (droppable && should_drop(link->loss_permille, link->counter++)) return true;  // lost on the wire
    static std::uint8_t buf[kMaxDatagram];
    std::size_t total = 0;
    for (unsigned i = 0; i < count; ++i) total += chunks[i].size;
    if (kHeaderSize + total > kMaxDatagram) return false;
    DatagramHeader local = header;
    local.length = static_cast<std::uint32_t>(total);
    encode_header(local, buf);
    std::size_t at = kHeaderSize;
    for (unsigned i = 0; i < count; ++i) {
        if (chunks[i].size) std::memcpy(buf + at, chunks[i].data, chunks[i].size);
        at += chunks[i].size;
    }
    DatagramHeader out{};
    const std::uint8_t* p = nullptr;
    std::size_t ps = 0;
    if (decode_datagram(buf, at, out, p, ps) != DecodeStatus::ok) return false;
    link->dst->on_datagram(out, p, ps);
    return true;
}
std::uint64_t manual_now(void* ctx) noexcept { return *static_cast<std::uint64_t*>(ctx); }

// on_peer has no context argument; record into globals.
std::uint64_t g_peer_seen[8]{};
char g_peer_names[8][64]{};
unsigned g_peer_calls = 0;
void record_peer(std::uint64_t id, const char* name) noexcept {
    if (g_peer_calls < 8) {
        g_peer_seen[g_peer_calls] = id;
        CopyText(g_peer_names[g_peer_calls], sizeof(g_peer_names[g_peer_calls]), name ? name : "");
        ++g_peer_calls;
    }
}

struct Pair {
    LoopbackCore host, guest;
    Recorder host_rec, guest_rec;
    Link host_to_guest, guest_to_host;
    std::uint64_t host_now = 1000, guest_now = 1000;

    void start(std::uint32_t timeout_ms = 10000) {
        host_to_guest.dst = &guest;
        guest_to_host.dst = &host;
        EmulatorConfig hc{};
        hc.self_id = kHostId;
        hc.peer_id = kGuestId;
        hc.peer_port = 47811;
        hc.timeout_ms = timeout_ms;
        CopyText(hc.self_name, sizeof(hc.self_name), "HostName");
        hc.on_peer = &record_peer;
        EmulatorConfig gc{};
        gc.self_id = kGuestId;
        gc.peer_id = kHostId;
        gc.peer_port = 47810;
        gc.timeout_ms = timeout_ms;
        CopyText(gc.self_name, sizeof(gc.self_name), "GuestName");
        gc.on_peer = &record_peer;
        host.start(hc, {&wire_deliver, &host_to_guest}, {&rec_request, &rec_fail, &host_rec}, {&manual_now, &host_now});
        guest.start(gc, {&wire_deliver, &guest_to_host}, {&rec_request, &rec_fail, &guest_rec}, {&manual_now, &guest_now});
    }
    // Advance both clocks in 250 ms steps (the receive thread's idle period),
    // ticking the selected cores.
    void run_for(std::uint64_t ms, bool tick_host = true, bool tick_guest = true) {
        for (std::uint64_t t = 0; t < ms; t += 250) {
            host_now += 250;
            guest_now += 250;
            if (tick_host) host.tick();
            if (tick_guest) guest.tick();
        }
    }
    void stop() {
        host.shutdown();
        guest.shutdown();
    }
};

bool read_one(LoopbackCore& core, const char* expect, std::int32_t channel = 0) {
    std::uint8_t dest[64]{};
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    if (!core.read_p2p(dest, sizeof(dest), &got, &from, channel)) return false;
    const std::size_t n = std::strlen(expect);
    return got == n && std::memcmp(dest, expect, n) == 0;
}

void TestRequestOnceAndInvisibleUntilAccept() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "hi", 2, steam::kP2PSendUnreliable, 0), "guest send ok");
    Check(p.host_rec.request_count == 1 && p.host_rec.requests[0] == kGuestId, "one 1202 on first contact");
    // Invisible until the host accepts.
    std::uint32_t size = 0;
    Check(!p.host.is_available(&size, 0), "packet invisible before accept");
    std::uint8_t dest[16];
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    Check(!p.host.read_p2p(dest, sizeof(dest), &got, &from, 0), "read fails before accept");
    steam::P2PSessionState_t pending{};
    Check(p.host.session_state(kGuestId, &pending) && pending.connecting == 1 && pending.connection_active == 0,
          "pending session reports connecting");
    // A second inbound packet within the re-post interval does not post another 1202.
    Check(p.guest.send_p2p(kHostId, "yo", 2, steam::kP2PSendUnreliable, 0), "guest send 2 ok");
    Check(p.host_rec.request_count == 1, "1202 not repeated within the re-post interval");
    // After accept, both queued packets are readable in order with the sender id.
    Check(p.host.accept(kGuestId), "host accept");
    Check(p.host.is_available(&size, 0) && size == 2, "visible after accept");
    Check(p.host.read_p2p(dest, sizeof(dest), &got, &from, 0), "read 1");
    Check(got == 2 && from == kGuestId && std::memcmp(dest, "hi", 2) == 0, "read 1 payload+sender");
    Check(p.host.read_p2p(dest, sizeof(dest), &got, &from, 0) && std::memcmp(dest, "yo", 2) == 0, "read 2");
    Check(!p.host.read_p2p(dest, sizeof(dest), &got, &from, 0), "queue empty");
    // The guest's own Send opened its side: no 1202 on the guest for host data.
    Check(p.host.send_p2p(kGuestId, "back", 4, 0, 0), "host replies");
    Check(p.guest_rec.request_count == 0, "no 1202 toward the side that sent first");
    Check(read_one(p.guest, "back"), "guest reads reply");
    p.stop();
}

void TestTruncation() {
    Pair p;
    p.start();
    Check(p.host.send_p2p(kGuestId, "x", 1, 0, 0), "prime host->guest accept");  // host accepts guest implicitly
    Check(p.guest.send_p2p(kHostId, "abcdef", 6, 0, 0), "guest send 6");
    Check(p.host.accept(kGuestId), "accept");
    std::uint8_t dest[3];
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    Check(p.host.read_p2p(dest, 3, &got, &from, 0), "read truncated");
    Check(got == 3 && std::memcmp(dest, "abc", 3) == 0, "truncated to caller buffer");
    p.stop();
}

void TestSizeCaps() {
    Pair p;
    p.start();
    static std::uint8_t big[steam::kMaxReliablePayload + 2];
    for (std::size_t i = 0; i < sizeof(big); ++i) big[i] = static_cast<std::uint8_t>(i * 131 + 7);
    Check(!p.guest.send_p2p(kHostId, big, 1201, steam::kP2PSendUnreliable, 0), "unreliable 1201 rejected");
    Check(p.guest.send_p2p(kHostId, big, 1200, steam::kP2PSendUnreliable, 0), "unreliable 1200 ok");
    Check(!p.guest.send_p2p(kHostId, big, steam::kMaxReliablePayload + 1, steam::kP2PSendReliable, 0),
          "reliable over 1 MiB rejected");
    p.stop();

    // Reliable fragmentation round-trip of a multi-datagram payload.
    Pair q;
    q.start();
    const std::uint32_t len = 150000;
    Check(q.guest.send_p2p(kHostId, big, len, steam::kP2PSendReliable, 0), "reliable 150000 sent");
    Check(q.guest_to_host.sent[static_cast<unsigned>(DatagramType::Fragment)] == 3, "150000 B -> 3 fragments");
    Check(q.host.accept(kGuestId), "accept reliable peer");
    static std::uint8_t out[200000];
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    Check(q.host.is_available(&got, 0) && got == len, "reassembled size visible");
    Check(q.host.read_p2p(out, sizeof(out), &got, &from, 0), "read reassembled");
    Check(got == len && from == kGuestId, "reassembled length+sender");
    Check(std::memcmp(out, big, len) == 0, "reassembled content matches");
    // The full 1 MiB reliable maximum also round-trips.
    Check(q.guest.send_p2p(kHostId, big, steam::kMaxReliablePayload, steam::kP2PSendReliable, 0), "1 MiB sent");
    static std::uint8_t out2[steam::kMaxReliablePayload];
    Check(q.host.read_p2p(out2, sizeof(out2), &got, &from, 0) && got == steam::kMaxReliablePayload &&
              std::memcmp(out2, big, got) == 0,
          "1 MiB reassembled");
    q.stop();
}

void TestSessionStateAndClose() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "guest send");
    Check(p.host.accept(kGuestId), "accept");
    steam::P2PSessionState_t state{};
    Check(p.host.session_state(kGuestId, &state), "state active");
    Check(state.connection_active == 1 && state.remote_ip == 0x7F000001u && state.remote_port == 47811,
          "state fields: active, 127.0.0.1, peer port");
    // Host closes -> guest gets a synthetic 1203 error 4, and the host session
    // reports inactive with its packets purged.
    Check(p.host.close(kGuestId), "host close");
    Check(p.guest_rec.fail_count == 1 && p.guest_rec.fail_error[0] == steam::kP2PErrorTimeout, "remote close -> 1203 err 4");
    Check(p.guest_rec.fail_remote[0] == kHostId, "1203 names the host");
    steam::P2PSessionState_t after{};
    Check(!p.host.session_state(kGuestId, &after) && after.connection_active == 0, "closed session inactive");
    Check(p.host.peer_count() == 0 && p.host.queued_count() == 0, "close frees the slot and purges packets");
    Check(!p.host.close(kGuestId), "closing an unknown session returns false");
    p.stop();
}

void TestTimeout() {
    Pair p;
    p.start(/*timeout_ms=*/1000);
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "guest send");
    Check(p.host.accept(kGuestId), "accept");
    // No inbound for > timeout -> synthetic 1203 on tick.
    p.host_now += 1500;
    p.host.tick();
    Check(p.host_rec.fail_count == 1 && p.host_rec.fail_error[0] == steam::kP2PErrorTimeout, "idle timeout -> 1203");
    Check(p.host_rec.fail_remote[0] == kGuestId, "timeout names the guest");
    // A second tick does not re-post for the same (reset) peer.
    p.host_now += 1500;
    p.host.tick();
    Check(p.host_rec.fail_count == 1, "timeout posts once");
    p.stop();
}

void TestHelloOnlyCreatesNoSession() {
    g_peer_calls = 0;
    Pair p;
    p.start();
    p.host.announce(/*reply=*/true);  // guest answers with its own HELLO
    Check(p.host_to_guest.sent[static_cast<unsigned>(DatagramType::Hello)] == 1, "host HELLO sent");
    Check(p.guest_to_host.sent[static_cast<unsigned>(DatagramType::Hello)] == 1, "guest HELLO reply sent");
    Check(p.host_rec.request_count == 0 && p.guest_rec.request_count == 0, "HELLO never posts 1202");
    Check(p.host.peer_count() == 0 && p.guest.peer_count() == 0, "HELLO creates no session");
    Check(g_peer_calls == 2, "persona reported for each HELLO");
    Check(g_peer_seen[0] == kHostId && std::strcmp(g_peer_names[0], "HostName") == 0, "guest learned host persona");
    Check(g_peer_seen[1] == kGuestId && std::strcmp(g_peer_names[1], "GuestName") == 0, "host learned guest persona");
    // No idle timer was armed: 20 s later there is still no 1203 and no PING.
    p.run_for(20000);
    Check(p.host_rec.fail_count == 0 && p.guest_rec.fail_count == 0, "HELLO arms no idle timer");
    Check(p.host_to_guest.sent[static_cast<unsigned>(DatagramType::Ping)] == 0 &&
              p.guest_to_host.sent[static_cast<unsigned>(DatagramType::Ping)] == 0,
          "no PING without a session");
    // The real join after the long idle works normally.
    Check(p.guest.send_p2p(kHostId, "join", 4, 0, 0), "guest joins late");
    Check(p.host_rec.request_count == 1, "late join gets its 1202");
    Check(p.host.accept(kGuestId) && read_one(p.host, "join"), "late join accepted and readable");
    p.stop();
}

void TestRejoinAfterLocalClose() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "one", 3, 0, 0), "first join");
    Check(p.host.accept(kGuestId) && read_one(p.host, "one"), "first session readable");
    Check(p.guest.close(kHostId), "guest closes");
    Check(p.host_rec.fail_count == 1 && p.host_rec.fail_remote[0] == kGuestId, "host told via 1203");
    Check(p.host.peer_count() == 0, "host slot freed");
    Check(p.guest.send_p2p(kHostId, "two", 3, 0, 0), "guest rejoins");
    Check(p.host_rec.request_count == 2, "rejoin posts a fresh 1202");
    std::uint32_t size = 0;
    Check(!p.host.is_available(&size, 0), "rejoin invisible before accept");
    Check(p.host.accept(kGuestId), "host accepts the rejoin");
    Check(read_one(p.host, "two"), "rejoin readable");
    p.stop();
}

void TestRejoinAfterRemoteClose() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "one", 3, 0, 0), "first join");
    Check(p.host.accept(kGuestId), "accepted");
    Check(p.host.close(kGuestId), "host ends the session");
    Check(p.guest_rec.fail_count == 1, "guest told via 1203");
    Check(p.guest.peer_count() == 0, "guest slot freed by remote CLOSE");
    Check(p.guest.send_p2p(kHostId, "two", 3, 0, 0), "guest asks again");
    Check(p.host_rec.request_count == 2, "host re-requests after its own close");
    Check(p.host.accept(kGuestId) && read_one(p.host, "two"), "accepted again");
    p.stop();
}

void TestRejoinAfterTimeout() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "one", 3, 0, 0), "first join");
    Check(p.host.accept(kGuestId), "accepted");
    p.guest_to_host.cut = true;  // guest goes quiet (crash/stall)
    p.run_for(11000, /*tick_host=*/true, /*tick_guest=*/false);
    Check(p.host_rec.fail_count == 1, "host 1203 after 10 s without inbound");
    Check(p.host.peer_count() == 0, "timed-out session reset");
    p.guest_to_host.cut = false;
    Check(p.guest.send_p2p(kHostId, "two", 3, 0, 0), "guest back");
    Check(p.host_rec.request_count == 2, "fresh 1202 after timeout");
    Check(p.host.accept(kGuestId) && read_one(p.host, "two"), "accepted after timeout");
    p.stop();
}

void TestDeadPeerWhileSending() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "join");
    Check(p.host.accept(kGuestId), "accepted");
    p.guest_to_host.cut = true;  // the guest process died
    unsigned sends = 0;
    for (int step = 0; step < 48 && p.host_rec.fail_count == 0; ++step) {  // up to 12 s
        p.host_now += 250;
        if (step % 4 == 0) {
            p.host.send_p2p(kGuestId, "tick", 4, 0, 0);  // the host keeps sending
            ++sends;
        }
        p.host.tick();
    }
    Check(sends >= 10, "host kept sending (one packet per second)");
    Check(p.host_rec.fail_count == 1 && p.host_rec.fail_remote[0] == kGuestId,
          "dead peer detected although the local side kept sending");
    p.stop();
}

void TestPingKeepalive() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "join");
    Check(p.host.accept(kGuestId), "accepted");
    Check(read_one(p.host, "hi"), "drained");
    p.run_for(30000);  // 30 s of application silence; PINGs flow both ways
    Check(p.host_rec.fail_count == 0 && p.guest_rec.fail_count == 0, "no 1203 while PINGs flow");
    Check(p.host_to_guest.sent[static_cast<unsigned>(DatagramType::Ping)] >= 25 &&
              p.guest_to_host.sent[static_cast<unsigned>(DatagramType::Ping)] >= 25,
          "about one PING per second each way");
    std::uint32_t size = 0;
    Check(!p.host.is_available(&size, 0) && !p.guest.is_available(&size, 0), "PINGs are not packets");
    Check(p.guest.send_p2p(kHostId, "still", 5, 0, 0) && read_one(p.host, "still"), "session still usable");
    p.stop();
}

void TestPendingRepostAndExpiry() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "knock", 5, 0, 0), "guest knocks");
    Check(p.host_rec.request_count == 1, "first 1202");
    p.run_for(3250, /*tick_host=*/true, /*tick_guest=*/false);
    Check(p.host_rec.request_count == 2, "1202 re-posted while still pending");
    Check(p.host.queued_count() == 1, "packet held while pending");
    p.run_for(8000, /*tick_host=*/true, /*tick_guest=*/false);
    Check(p.host.peer_count() == 0 && p.host.queued_count() == 0, "pending session expires and drops its packets");
    const unsigned before = p.host_rec.request_count;
    p.run_for(5000, /*tick_host=*/true, /*tick_guest=*/false);
    Check(p.host_rec.request_count == before, "no re-post after expiry");
    // A stale Accept still succeeds but exposes nothing.
    Check(p.host.accept(kGuestId), "late accept creates the session");
    std::uint32_t size = 0;
    Check(!p.host.is_available(&size, 0), "expired packet is gone");
    // A later knock is delivered normally on the accepted session.
    Check(p.guest.send_p2p(kHostId, "again", 5, 0, 0) && read_one(p.host, "again"), "later packet readable");
    p.stop();
}

void TestRequestRetryWhenSinkRefuses() {
    Pair p;
    p.start();
    p.host_rec.refuse_requests = true;  // callback queue full
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "guest send");
    Check(p.host_rec.refused == 1 && p.host_rec.request_count == 0, "refused 1202 not latched");
    p.host_rec.refuse_requests = false;
    p.host_now += 250;
    p.host.tick();
    Check(p.host_rec.request_count == 1, "next tick retries the 1202");
    p.stop();
}

void SendFragment(LoopbackCore& core, std::uint32_t msg, std::uint32_t total, std::uint32_t offset,
                  std::uint32_t chunk, std::int32_t channel, std::uint8_t fill) {
    static std::uint8_t payload[kFragmentHeaderSize + kFragmentChunk];
    std::memcpy(payload + 0, &msg, 4);
    std::memcpy(payload + 4, &total, 4);
    std::memcpy(payload + 8, &offset, 4);
    std::memset(payload + kFragmentHeaderSize, fill, chunk);
    DatagramHeader h{};
    h.type = DatagramType::Fragment;
    h.send_mode = static_cast<std::uint8_t>(steam::kP2PSendReliable);
    h.channel = channel;
    h.src = kGuestId;
    h.dst = kHostId;
    h.length = static_cast<std::uint32_t>(kFragmentHeaderSize + chunk);
    core.on_datagram(h, payload, h.length);
}

void TestFragmentMismatch() {
    Pair p;
    p.start();
    Check(p.host.accept(kGuestId), "host pre-accepts");
    // The reviewer's overflow case: a half-built 100000-byte message, then the
    // same id claiming total 1000000 at offset 120000 (restarted peer).
    SendFragment(p.host, 1, 100000, 0, kFragmentChunk, 0, 0xAA);
    SendFragment(p.host, 1, 1000000, 120000, kFragmentChunk, 0, 0xB2);
    Check(HeapValidate(GetProcessHeap(), 0, nullptr) != FALSE, "heap intact after total mismatch");
    std::uint32_t size = 0;
    Check(!p.host.is_available(&size, 0), "no message from mismatched fragments");
    // The rest of the 1000000-byte message arrives: it must reassemble into a
    // buffer of its own size (reusing the 100000-byte buffer would overflow it).
    constexpr std::uint32_t kBig = 1000000;
    const std::uint32_t big_chunks = (kBig + kFragmentChunk - 1) / kFragmentChunk;
    for (std::uint32_t i = 0; i < big_chunks; ++i) {
        if (i == 2) continue;  // offset 120000 already delivered
        const std::uint32_t offset = i * static_cast<std::uint32_t>(kFragmentChunk);
        const std::uint32_t chunk = (kBig - offset < kFragmentChunk) ? kBig - offset : static_cast<std::uint32_t>(kFragmentChunk);
        SendFragment(p.host, 1, kBig, offset, chunk, 0, static_cast<std::uint8_t>(0xB0 + i));
    }
    static std::uint8_t big_out[kBig];
    std::uint32_t big_got = 0;
    std::uint64_t big_from = 0;
    Check(p.host.read_p2p(big_out, kBig, &big_got, &big_from, 0) && big_got == kBig && big_from == kGuestId,
          "restarted peer's message reassembled at its own size");
    bool content_ok = true;
    for (std::uint32_t i = 0; i < big_chunks; ++i) {
        const std::uint32_t offset = i * static_cast<std::uint32_t>(kFragmentChunk);
        content_ok = content_ok && big_out[offset] == static_cast<std::uint8_t>(0xB0 + i);
    }
    Check(content_ok && big_out[kBig - 1] == static_cast<std::uint8_t>(0xB0 + big_chunks - 1), "fragment contents");
    Check(HeapValidate(GetProcessHeap(), 0, nullptr) != FALSE, "heap intact after the large message");
    // A fragment on another channel with the same id abandons the reassembly.
    SendFragment(p.host, 2, 70000, 0, kFragmentChunk, 0, 0x11);
    SendFragment(p.host, 2, 70000, 60000, 10000, 3, 0x22);
    Check(!p.host.is_available(&size, 0) && !p.host.is_available(&size, 3), "channel mismatch abandons");
    // A complete, consistent message still assembles.
    SendFragment(p.host, 3, 70000, 0, kFragmentChunk, 0, 0x33);
    SendFragment(p.host, 3, 70000, 60000, 10000, 0, 0x44);
    static std::uint8_t out[70000];
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    Check(p.host.read_p2p(out, sizeof(out), &got, &from, 0) && got == 70000, "consistent message assembled");
    Check(out[0] == 0x33 && out[59999] == 0x33 && out[60000] == 0x44 && out[69999] == 0x44, "fragment order");
    // Malformed fragments are ignored: wrong chunk size, offset not aligned.
    SendFragment(p.host, 4, 70000, 0, 100, 0, 0x55);
    SendFragment(p.host, 4, 70000, 30000, kFragmentChunk, 0, 0x55);
    Check(!p.host.is_available(&size, 0), "malformed fragments ignored");
    Check(HeapValidate(GetProcessHeap(), 0, nullptr) != FALSE, "heap intact at end");
    p.stop();
}

void TestChannels() {
    Pair p;
    p.start();
    Check(p.guest.send_p2p(kHostId, "zero", 4, 0, 0), "channel 0 send");
    Check(p.guest.send_p2p(kHostId, "five", 4, 0, 5), "channel 5 send");
    Check(p.host.accept(kGuestId), "accept");
    std::uint32_t size = 0;
    Check(p.host.is_available(&size, 5) && size == 4, "channel 5 visible on channel 5");
    Check(read_one(p.host, "zero", 0), "channel 0 read returns the channel 0 packet");
    Check(!p.host.read_p2p(nullptr, 0, &size, nullptr, 0), "channel 0 poll does not consume channel 5");
    Check(p.host.close_channel(kGuestId, 5), "close channel 5");
    Check(!p.host.is_available(&size, 5), "channel 5 packets purged");
    steam::P2PSessionState_t state{};
    Check(p.host.session_state(kGuestId, &state) && state.connection_active == 1, "session survives with channel 0");
    Check(p.guest_rec.fail_count == 0, "no CLOSE while a channel remains");
    Check(p.host.close_channel(kGuestId, 0), "close last channel");
    Check(!p.host.session_state(kGuestId, &state), "last channel closes the session");
    Check(p.guest_rec.fail_count == 1, "guest told when the last channel closes");
    p.stop();
}

// ----- The emulated vtable, called with the game's exact shapes -----
using SendFn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t, const void*, std::uint32_t, std::int32_t, std::int32_t);
using AvailFn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint32_t*, std::int32_t);
using ReadFn = bool(MGSCOOP_STEAM_CALL*)(void*, void*, std::uint32_t, std::uint32_t*, std::uint64_t*, std::int32_t);
using IdFn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t);
using CloseChanFn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t, std::int32_t);
using StateFn = bool(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t, steam::P2PSessionState_t*);
using ZeroFn = std::uintptr_t(MGSCOOP_STEAM_CALL*)(void*);

template <typename Fn>
Fn slot(void* object, unsigned index) {
    return reinterpret_cast<Fn>((*static_cast<void* const* const*>(object))[index]);
}

// Mirrors the game's poll at 0x1A5FBF0: while IsP2PPacketAvailable(&size, 0)
// read into a buffer of that size. Returns packets read; bounded so a spin is
// detected as a failure rather than a hang.
unsigned GamePoll(void* object, unsigned limit) {
    unsigned reads = 0, iterations = 0;
    std::uint32_t size = 0;
    while (slot<AvailFn>(object, steam::net_slot::IsP2PPacketAvailable)(object, &size, 0)) {
        if (++iterations > limit) return ~0u;
        std::uint8_t buf[1200];
        std::uint32_t got = 0;
        std::uint64_t from = 0;
        if (slot<ReadFn>(object, steam::net_slot::ReadP2PPacket)(object, buf, size < sizeof(buf) ? size : sizeof(buf),
                                                                  &got, &from, 0))
            ++reads;
    }
    return reads;
}

void TestEmulatedVtableCallShapes() {
    Pair p;
    p.start();
    EmulatorObject host_obj{emulated_networking_vtable(), &p.host};
    EmulatorObject guest_obj{emulated_networking_vtable(), &p.guest};
    void* host = &host_obj;
    void* guest = &guest_obj;
    // 0x1A5FB60: Send(remote, data, len, mode=0, channel=0).
    Check(slot<SendFn>(guest, steam::net_slot::SendP2PPacket)(guest, kHostId, "abc", 3, 0, 0), "vtable Send");
    Check(p.host_rec.request_count == 1, "vtable Send reaches the host as 1202");
    // The poll before Accept terminates immediately (never spins).
    Check(GamePoll(host, 8) == 0, "poll before accept reads nothing and returns");
    Check(slot<IdFn>(host, steam::net_slot::AcceptP2PSessionWithUser)(host, kGuestId), "vtable Accept");
    Check(slot<SendFn>(guest, steam::net_slot::SendP2PPacket)(guest, kHostId, "defg", 4, 0, 0), "vtable Send 2");
    Check(GamePoll(host, 8) == 2, "poll after accept reads both packets and stops");
    // 0x1A5FAC0: Read(dest, cap, &size, &remote, channel=0) returns the sender.
    Check(slot<SendFn>(guest, steam::net_slot::SendP2PPacket)(guest, kHostId, "xyz", 3, 0, 0), "vtable Send 3");
    std::uint8_t buf[8]{};
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    Check(slot<ReadFn>(host, steam::net_slot::ReadP2PPacket)(host, buf, sizeof(buf), &got, &from, 0) && got == 3 &&
              from == kGuestId && std::memcmp(buf, "xyz", 3) == 0,
          "vtable Read size+sender+bytes");
    // 0x1A5F8C0: GetP2PSessionState(remote, &state).
    steam::P2PSessionState_t state{};
    Check(slot<StateFn>(host, steam::net_slot::GetP2PSessionState)(host, kGuestId, &state) &&
              state.remote_ip == 0x7F000001u && state.remote_port == 47811,
          "vtable GetP2PSessionState");
    // Unused slots 7..21 return 0/false.
    for (unsigned i = 7; i < steam::net_slot::kCount; ++i)
        Check(slot<ZeroFn>(host, i)(host) == 0, "unused slot returns 0");
    // 0x1A5F780: Close(remote) -> the guest sees 1203.
    Check(slot<IdFn>(host, steam::net_slot::CloseP2PSessionWithUser)(host, kGuestId), "vtable Close");
    Check(p.guest_rec.fail_count == 1, "vtable Close -> remote 1203");
    Check(!slot<CloseChanFn>(host, steam::net_slot::CloseP2PChannelWithUser)(host, kGuestId, 0),
          "vtable CloseChannel on a closed session");

    // The dead object answers every slot with 0/false.
    void* dead = dead_networking_object();
    Check(dead != nullptr, "dead object is not null");
    Check(!slot<SendFn>(dead, steam::net_slot::SendP2PPacket)(dead, kHostId, "a", 1, 0, 0), "dead Send false");
    Check(GamePoll(dead, 2) == 0, "dead poll returns immediately");
    Check(!slot<IdFn>(dead, steam::net_slot::AcceptP2PSessionWithUser)(dead, kGuestId), "dead Accept false");
    Check(!slot<StateFn>(dead, steam::net_slot::GetP2PSessionState)(dead, kGuestId, &state), "dead state false");
    for (unsigned i = 0; i < steam::net_slot::kCount; ++i)
        Check(slot<ZeroFn>(dead, i)(dead) == 0, "every dead slot returns 0");
    p.stop();
}

void TestLossInjectionInMemory() {
    Pair p;
    p.start();
    p.guest_to_host.loss_permille = 1000;  // drop everything guest->host
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "send returns true even when lost");
    Check(p.host_rec.request_count == 0, "fully-lossy link delivers nothing");
    p.guest_to_host.loss_permille = 0;
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "send after loss cleared");
    Check(p.host_rec.request_count == 1, "delivery resumes");
    p.stop();
}

// ----- Synthetic callback dispatch through a fake CCallbackBase -----
struct FakeCallback {
    const void** vtable;  // +0
    std::uint8_t flags;   // +8
    std::uint8_t pad[3];
    std::int32_t id;      // +0xC
    int calls;
    std::uint64_t last_remote;
    std::uint8_t last_error;
    FakeCallback* unregister_on_run;
};
static_assert(offsetof(FakeCallback, flags) == steam::kCallbackFlagsOffset, "flags offset");
static_assert(offsetof(FakeCallback, id) == steam::kCallbackIdOffset, "id offset");

void MGSCOOP_STEAM_CALL fake_run(void* self, const void* payload) noexcept {
    auto* cb = static_cast<FakeCallback*>(self);
    ++cb->calls;
    if (cb->id == steam::kCallbackP2PSessionRequest) {
        cb->last_remote = static_cast<const steam::P2PSessionRequest_t*>(payload)->steam_id_remote;
    } else {
        const auto* f = static_cast<const steam::P2PSessionConnectFail_t*>(payload);
        cb->last_remote = f->steam_id_remote;
        cb->last_error = f->p2p_session_error;
    }
    if (cb->unregister_on_run) callbacks::on_unregister(cb->unregister_on_run);  // reentrant
}
std::uintptr_t MGSCOOP_STEAM_CALL fake_stub(void*) noexcept { return 0; }
std::uint64_t g_cb_now = 0;
std::uint64_t cb_clock() noexcept { return g_cb_now; }

void TestCallbackDispatch() {
    callbacks::reset_for_test();
    callbacks::set_clock_for_test(&cb_clock);
    g_cb_now = 1000;
    const void* vt1202[3] = {reinterpret_cast<const void*>(&fake_stub), reinterpret_cast<const void*>(&fake_run),
                             reinterpret_cast<const void*>(&fake_stub)};
    const void* vt1203[3] = {reinterpret_cast<const void*>(&fake_stub), reinterpret_cast<const void*>(&fake_run),
                             reinterpret_cast<const void*>(&fake_stub)};
    FakeCallback cb1202{vt1202, steam::kCallbackFlagRegistered, {}, steam::kCallbackP2PSessionRequest, 0, 0, 0, nullptr};
    FakeCallback cb1203{vt1203, steam::kCallbackFlagRegistered, {}, steam::kCallbackP2PSessionConnectFail, 0, 0, 0, nullptr};
    callbacks::configure(vt1202, vt1203);

    // Before the game registers: a 1202 waits, a 1203 is dropped.
    Check(callbacks::post_session_request(kGuestId), "queue early 1202");
    Check(callbacks::post_connect_fail(kOtherId, steam::kP2PErrorTimeout), "queue early 1203");
    Check(callbacks::drain_at(g_cb_now) == 0, "nothing to dispatch to");
    Check(callbacks::pending() == 1, "early 1202 kept, listener-less 1203 dropped");
    callbacks::on_register(&cb1202, steam::kCallbackP2PSessionRequest);
    callbacks::on_register(&cb1203, steam::kCallbackP2PSessionConnectFail);
    Check(callbacks::tracked(steam::kCallbackP2PSessionRequest) == 1, "1202 tracked");
    Check(callbacks::tracked(steam::kCallbackP2PSessionConnectFail) == 1, "1203 tracked");
    Check(callbacks::drain_at(g_cb_now) == 1 && cb1202.calls == 1 && cb1202.last_remote == kGuestId,
          "kept 1202 delivered once the session registers");

    Check(callbacks::post_session_request(kGuestId), "queue 1202");
    Check(callbacks::post_session_request(kGuestId), "duplicate 1202 coalesced");
    Check(callbacks::post_connect_fail(kGuestId, steam::kP2PErrorTimeout), "queue 1203");
    Check(callbacks::pending() == 2, "two pending after coalescing");
    Check(callbacks::drain_at(g_cb_now) == 2, "two dispatched");
    Check(cb1202.calls == 2 && cb1202.last_remote == kGuestId, "1202 delivered");
    Check(cb1203.calls == 1 && cb1203.last_remote == kGuestId && cb1203.last_error == steam::kP2PErrorTimeout,
          "1203 delivered");
    Check(callbacks::pending() == 0, "queue drained");

    // Stale events (older than kMaxEventAgeMs) are discarded, not delivered.
    callbacks::on_unregister(&cb1202);
    Check(callbacks::post_session_request(kGuestId), "queue 1202 with no listener");
    g_cb_now += callbacks::kMaxEventAgeMs + 1;
    callbacks::on_register(&cb1202, steam::kCallbackP2PSessionRequest);
    Check(callbacks::drain_at(g_cb_now) == 0 && callbacks::pending() == 0 && cb1202.calls == 2, "stale 1202 discarded");

    // Wrong vptr is refused.
    const void* vt_other[3] = {nullptr, reinterpret_cast<const void*>(&fake_run), nullptr};
    FakeCallback impostor{vt_other, steam::kCallbackFlagRegistered, {}, steam::kCallbackP2PSessionRequest, 0, 0, 0, nullptr};
    callbacks::on_register(&impostor, steam::kCallbackP2PSessionRequest);
    callbacks::post_session_request(kGuestId);
    callbacks::drain_at(g_cb_now);
    Check(impostor.calls == 0, "impostor vptr refused");
    Check(cb1202.calls == 3, "genuine still dispatched");

    // Cleared registered flag is refused.
    cb1202.flags = 0;
    callbacks::post_session_request(kHostId);
    callbacks::drain_at(g_cb_now);
    Check(cb1202.calls == 3, "unregistered-flag refused");
    cb1202.flags = steam::kCallbackFlagRegistered;

    // A Run handler that unregisters another tracked object (reentrantly, on
    // the dispatch thread) prevents that object's invocation.
    FakeCallback second{vt1202, steam::kCallbackFlagRegistered, {}, steam::kCallbackP2PSessionRequest, 0, 0, 0, nullptr};
    callbacks::on_unregister(&impostor);
    callbacks::on_register(&second, steam::kCallbackP2PSessionRequest);
    cb1202.unregister_on_run = &second;
    callbacks::post_session_request(kOtherId);
    callbacks::drain_at(g_cb_now);
    Check(cb1202.calls == 4 && second.calls == 0, "reentrant unregister honoured before the next invoke");
    cb1202.unregister_on_run = nullptr;

    // Unregister stops dispatch.
    callbacks::on_unregister(&cb1202);
    Check(callbacks::tracked(steam::kCallbackP2PSessionRequest) == 0, "nothing tracked");
    callbacks::reset_for_test();
}

// ----- Forwarding proxy / thunk generation -----
struct FakeIface {
    const void** vtable;
    std::uint64_t value;
};
int MGSCOOP_STEAM_CALL iface_add(void* self, int a, int b) noexcept {
    return a + b + static_cast<int>(static_cast<FakeIface*>(self)->value);
}
std::uint64_t* MGSCOOP_STEAM_CALL iface_getid(void* self, std::uint64_t* out) noexcept {
    if (out) *out = static_cast<FakeIface*>(self)->value;
    return out;
}
const char* MGSCOOP_STEAM_CALL iface_name(void*) noexcept { return "real"; }
const char* MGSCOOP_STEAM_CALL override_name(void*) noexcept { return "override"; }
std::uint64_t* MGSCOOP_STEAM_CALL override_getid(void*, std::uint64_t* out) noexcept {
    if (out) *out = 0xABCDull;
    return out;
}
// Six arguments: four in registers, two on the stack (the shape of Send/Read).
std::uint64_t MGSCOOP_STEAM_CALL iface_six(void* self, std::uint64_t a, std::uint64_t b, std::uint64_t c,
                                           std::uint64_t d, std::uint64_t e) noexcept {
    return static_cast<FakeIface*>(self)->value + a + 2 * b + 3 * c + 4 * d + 5 * e;
}

void TestForwardingProxy() {
    const void* vt[5] = {reinterpret_cast<const void*>(&iface_add), reinterpret_cast<const void*>(&iface_getid),
                         reinterpret_cast<const void*>(&iface_name), reinterpret_cast<const void*>(&iface_getid),
                         reinterpret_cast<const void*>(&iface_six)};
    FakeIface iface{vt, 100};
    ProxyOverride overrides[2] = {{2, reinterpret_cast<const void*>(&override_name)},
                                  {3, reinterpret_cast<const void*>(&override_getid)}};
    void* proxy = build_forwarding_proxy(&iface, 5, overrides, 2, nullptr);
    Check(proxy != nullptr, "proxy built");
    auto* const* pv = *static_cast<void* const* const*>(proxy);

    // Forwarded slot 0: this is rewritten to the real object, args preserved.
    auto add = reinterpret_cast<int(MGSCOOP_STEAM_CALL*)(void*, int, int)>(pv[0]);
    Check(add(proxy, 2, 3) == 105, "forwarded call reaches real this");
    // Forwarded hidden-return slot 1.
    auto getid = reinterpret_cast<std::uint64_t*(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t*)>(pv[1]);
    std::uint64_t out = 0;
    Check(getid(proxy, &out) == &out && out == 100, "forwarded hidden-return");
    // Overridden slot 2.
    auto name = reinterpret_cast<const char*(MGSCOOP_STEAM_CALL*)(void*)>(pv[2]);
    Check(std::strcmp(name(proxy), "override") == 0, "override slot");
    // Overridden hidden-return slot 3.
    auto oid = reinterpret_cast<std::uint64_t*(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t*)>(pv[3]);
    out = 0;
    oid(proxy, &out);
    Check(out == 0xABCDull, "override hidden-return");
    // Forwarded slot with stack arguments.
    auto six = reinterpret_cast<std::uint64_t(MGSCOOP_STEAM_CALL*)(void*, std::uint64_t, std::uint64_t, std::uint64_t,
                                                                   std::uint64_t, std::uint64_t)>(pv[4]);
    Check(six(proxy, 1, 2, 3, 4, 5) == 100 + 1 + 4 + 9 + 16 + 25, "forwarded stack arguments preserved");

    // An override slot past the vtable is rejected.
    ProxyOverride bad[1] = {{9, reinterpret_cast<const void*>(&override_name)}};
    Check(build_forwarding_proxy(&iface, 5, bad, 1, nullptr) == nullptr, "out-of-range override rejected");
}

// ----- Two cores over real 127.0.0.1 UDP sockets on different ports -----
struct SocketSide {
    LoopbackCore core;
    UdpTransport transport;
    Recorder rec;
};
bool side_wire(void* ctx, const DatagramHeader& header, const WireChunk* chunks, unsigned count,
               bool droppable) noexcept {
    auto* side = static_cast<SocketSide*>(ctx);
    std::uint8_t head[kHeaderSize];
    WireChunk all[4]{};
    std::size_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        all[i + 1] = chunks[i];
        total += chunks[i].size;
    }
    DatagramHeader local = header;
    local.length = static_cast<std::uint32_t>(total);
    encode_header(local, head);
    all[0] = {head, kHeaderSize};
    return side->transport.transmit(all, count + 1, droppable);
}
void side_datagram(void* ctx, const std::uint8_t* data, std::size_t size, const Endpoint&) noexcept {
    auto* side = static_cast<SocketSide*>(ctx);
    DatagramHeader header{};
    const std::uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
    if (decode_datagram(data, size, header, payload, payload_size) != DecodeStatus::ok) return;
    side->core.on_datagram(header, payload, payload_size);
}

bool wait_until(volatile unsigned* counter, unsigned target) noexcept {
    for (int i = 0; i < 400; ++i) {  // up to ~4 s
        if (*counter >= target) return true;
        Sleep(10);
    }
    return *counter >= target;
}

void TestRealSockets() {
    static SocketSide host, guest;
    const std::uint16_t host_port = 48120, guest_port = 48121;
    EmulatorConfig hc{};
    hc.self_id = kHostId;
    hc.peer_id = kGuestId;
    hc.peer_port = guest_port;
    EmulatorConfig gc{};
    gc.self_id = kGuestId;
    gc.peer_id = kHostId;
    gc.peer_port = host_port;
    host.core.start(hc, {&side_wire, &host}, {&rec_request, &rec_fail, &host.rec}, {});
    guest.core.start(gc, {&side_wire, &guest}, {&rec_request, &rec_fail, &guest.rec}, {});
    TransportConfig htc{};
    htc.listen_port = host_port;
    htc.peer_port = guest_port;
    htc.idle_interval_ms = 100;
    TransportConfig gtc{};
    gtc.listen_port = guest_port;
    gtc.peer_port = host_port;
    gtc.idle_interval_ms = 100;
    Check(host.transport.start(htc, {&side_datagram, nullptr, &host}), "host transport start");
    Check(guest.transport.start(gtc, {&side_datagram, nullptr, &guest}), "guest transport start");

    // Guest contacts host -> host receives one synthetic 1202.
    Check(guest.core.send_p2p(kHostId, "ping", 4, 0, 0), "guest send over socket");
    Check(wait_until(&host.rec.request_count, 1), "host got 1202 over socket");
    Check(host.rec.requests[0] == kGuestId, "1202 names the guest");
    Check(host.core.accept(kGuestId), "host accept");

    // Guest sends more data; host drains its queue (the first "ping" that
    // triggered the request is also delivered post-accept) until it sees it.
    Check(guest.core.send_p2p(kHostId, "data!", 5, 0, 0), "guest data over socket");
    std::uint8_t dest[32];
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    bool saw_data = false;
    for (int i = 0; i < 400 && !saw_data; ++i) {
        while (host.core.read_p2p(dest, sizeof(dest), &got, &from, 0)) {
            if (got == 5 && from == kGuestId && std::memcmp(dest, "data!", 5) == 0) {
                saw_data = true;
                break;
            }
        }
        if (!saw_data) Sleep(10);
    }
    Check(saw_data, "host read socket data with correct sender");

    // A datagram from an unexpected endpoint is rejected by the receive filter.
    static SocketSide intruder;
    TransportConfig itc{};
    itc.listen_port = 48130;
    itc.peer_port = host_port;
    itc.idle_interval_ms = 100;
    Check(intruder.transport.start(itc, {&side_datagram, nullptr, &intruder}), "intruder start");
    std::uint8_t junk[kHeaderSize + 2];
    DatagramHeader h{};
    h.type = DatagramType::Data;
    h.src = kGuestId;
    h.length = 2;
    encode_header(h, junk);
    junk[kHeaderSize] = 'x';
    junk[kHeaderSize + 1] = 'y';
    Check(intruder.transport.transmit_raw_to(host_port, junk, sizeof(junk)), "intruder sent to host");
    for (int i = 0; i < 50 && host.transport.rejected_endpoint() == 0; ++i) Sleep(10);
    Check(host.transport.rejected_endpoint() >= 1, "host rejected the foreign endpoint");
    Check(host.transport.stats().rejected >= 1 && host.transport.stats().rx >= 2, "transport stats count rx/rejected");

    intruder.transport.stop();
    host.transport.stop();
    guest.transport.stop();
    host.core.shutdown();
    guest.core.shutdown();
}

// ----- Transport fault injection (the production send path) -----
volatile unsigned g_rx_count = 0;
LARGE_INTEGER g_rx_time{};
void count_datagram(void*, const std::uint8_t*, std::size_t, const Endpoint&) noexcept {
    QueryPerformanceCounter(&g_rx_time);
    g_rx_count = g_rx_count + 1;
}

void TestTransportLossAndLatency() {
    static UdpTransport a, b;
    TransportConfig ac{};
    ac.listen_port = 48150;
    ac.peer_port = 48151;
    ac.loss_permille = 500;
    TransportConfig bc{};
    bc.listen_port = 48151;
    bc.peer_port = 48150;
    g_rx_count = 0;
    Check(a.start(ac, {}), "lossy sender start");
    Check(b.start(bc, {&count_datagram, nullptr, nullptr}), "receiver start");
    std::uint8_t datagram[48]{};
    const WireChunk chunk{datagram, sizeof(datagram)};
    for (int i = 0; i < 100; ++i) Check(a.transmit(&chunk, 1, /*droppable=*/true), "lossy transmit reports sent");
    Check(a.stats().dropped == 50 && a.stats().tx == 50, "deterministic 50% loss on the send path");
    Check(wait_until(&g_rx_count, 50), "receiver got the surviving half");
    for (int i = 0; i < 10; ++i) Check(a.transmit(&chunk, 1, /*droppable=*/false), "control datagram");
    Check(a.stats().dropped == 50 && a.stats().tx == 60, "non-droppable datagrams are never lost");
    Check(wait_until(&g_rx_count, 60), "control datagrams arrive");
    Sleep(50);
    Check(g_rx_count == 60 && b.stats().rx == 60, "exactly the surviving datagrams arrived");
    a.stop();
    b.stop();

    static UdpTransport c, d;
    TransportConfig cc{};
    cc.listen_port = 48152;
    cc.peer_port = 48153;
    cc.latency_ms = 150;
    TransportConfig dc{};
    dc.listen_port = 48153;
    dc.peer_port = 48152;
    g_rx_count = 0;
    Check(c.start(cc, {}), "delayed sender start");
    Check(d.start(dc, {&count_datagram, nullptr, nullptr}), "delay receiver start");
    LARGE_INTEGER freq{}, sent{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&sent);
    Check(c.transmit(&chunk, 1, true), "delayed transmit queued");
    Check(c.stats().tx == 0, "not on the wire yet");
    Check(wait_until(&g_rx_count, 1), "delayed datagram arrives");
    const double elapsed_ms = static_cast<double>(g_rx_time.QuadPart - sent.QuadPart) * 1000.0 /
                              static_cast<double>(freq.QuadPart);
    Check(elapsed_ms >= 145.0, "latency applied (>= configured delay)");
    Check(c.stats().tx == 1, "delayed datagram counted once sent");
    c.stop();
    d.stop();
}

// A send from the game thread must never wait for the receive thread's
// select() poll on the same socket (a synchronous socket handle serializes
// them: 50 sends then take seconds instead of well under a millisecond).
void TestSendNotSerializedWithReceivePoll() {
    static UdpTransport sender;
    TransportConfig cfg{};
    cfg.listen_port = 48160;
    cfg.peer_port = 48161;  // nobody listens; UDP sends still complete
    cfg.idle_interval_ms = 250;
    Check(sender.start(cfg, {}), "sender start");
    Sleep(50);  // the receive thread is now parked in select()
    std::uint8_t datagram[48]{};
    const WireChunk chunk{datagram, sizeof(datagram)};
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < 50; ++i) Check(sender.transmit(&chunk, 1, true), "transmit");
    QueryPerformanceCounter(&t1);
    const double elapsed_ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 /
                              static_cast<double>(freq.QuadPart);
    Check(elapsed_ms < 100.0, "50 sends finish without waiting on the receive poll");
    Check(sender.stats().tx == 50, "all 50 sent");
    sender.stop();
}

// ----- Process-wide integration: fail closed, never real, never null -----
char g_log_text[4096]{};
void capture_log(const char* line) noexcept {
    const std::size_t have = std::strlen(g_log_text);
    CopyText(g_log_text + have, sizeof(g_log_text) - have, line);
}

void TestIntegrationFailClosed() {
    loopback_reset_for_test();
    Check(loopback_networking_object() == dead_networking_object(), "unconfigured -> dead object");

    // Occupy the listen port so the transport cannot bind.
    WSADATA wsa{};
    Check(WSAStartup(MAKEWORD(2, 2), &wsa) == 0, "test winsock");
    SOCKET blocker = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(48140);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Check(blocker != INVALID_SOCKET &&
              bind(blocker, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0,
          "blocker bound");
    g_log_text[0] = 0;
    EmulatorConfig cfg{};
    cfg.self_id = kHostId;
    cfg.peer_id = kGuestId;
    cfg.peer_port = 48141;
    cfg.log = &capture_log;
    LoopbackOptions options{};
    options.listen_port = 48140;
    loopback_configure(cfg, options);
    void* object = loopback_networking_object();
    Check(object != nullptr && object == dead_networking_object(), "bind failure -> dead object, not null/real");
    Check(std::strstr(g_log_text, "loopback=disabled; reason=transport_bind; port=48140") != nullptr,
          "bind failure logged with its port");
    Check(std::strstr(g_log_text, "loopback=listening") == nullptr, "no listening line on failure");
    SetLastError(1234);
    Check(loopback_networking_gate() == dead_networking_object() && GetLastError() == 1234,
          "gate preserves last error and stays closed");
    closesocket(blocker);
    loopback_reset_for_test();

    // A free port starts the emulator; the listening line carries the real port.
    g_log_text[0] = 0;
    options.listen_port = 48142;
    cfg.peer_port = 48143;
    loopback_configure(cfg, options);
    object = loopback_networking_gate();
    Check(object != dead_networking_object() &&
              *static_cast<const void* const* const*>(object) == emulated_networking_vtable(),
          "started -> emulator object");
    Check(std::strstr(g_log_text, "loopback=listening; port=48142; peer_port=48143") != nullptr,
          "listening line logs the bound port");
    loopback_reset_for_test();
    WSACleanup();
}
}  // namespace

// Runs one test and reports its wall time (slow socket tests stand out).
void Run(const char* name, void (*test)()) {
    const ULONGLONG start = GetTickCount64();
    test();
    std::printf("  %-40s %llu ms\n", name, static_cast<unsigned long long>(GetTickCount64() - start));
}

int main() {
    Run("TestRequestOnceAndInvisibleUntilAccept", &TestRequestOnceAndInvisibleUntilAccept);
    Run("TestTruncation", &TestTruncation);
    Run("TestSizeCaps", &TestSizeCaps);
    Run("TestSessionStateAndClose", &TestSessionStateAndClose);
    Run("TestTimeout", &TestTimeout);
    Run("TestHelloOnlyCreatesNoSession", &TestHelloOnlyCreatesNoSession);
    Run("TestRejoinAfterLocalClose", &TestRejoinAfterLocalClose);
    Run("TestRejoinAfterRemoteClose", &TestRejoinAfterRemoteClose);
    Run("TestRejoinAfterTimeout", &TestRejoinAfterTimeout);
    Run("TestDeadPeerWhileSending", &TestDeadPeerWhileSending);
    Run("TestPingKeepalive", &TestPingKeepalive);
    Run("TestPendingRepostAndExpiry", &TestPendingRepostAndExpiry);
    Run("TestRequestRetryWhenSinkRefuses", &TestRequestRetryWhenSinkRefuses);
    Run("TestFragmentMismatch", &TestFragmentMismatch);
    Run("TestChannels", &TestChannels);
    Run("TestEmulatedVtableCallShapes", &TestEmulatedVtableCallShapes);
    Run("TestLossInjectionInMemory", &TestLossInjectionInMemory);
    Run("TestCallbackDispatch", &TestCallbackDispatch);
    Run("TestForwardingProxy", &TestForwardingProxy);
    Run("TestRealSockets", &TestRealSockets);
    Run("TestSendNotSerializedWithReceivePoll", &TestSendNotSerializedWithReceivePoll);
    Run("TestTransportLossAndLatency", &TestTransportLossAndLatency);
    Run("TestIntegrationFailClosed", &TestIntegrationFailClosed);
    std::printf("steam_loopback_tests: %u checks passed\n", g_checks);
    return 0;
}
