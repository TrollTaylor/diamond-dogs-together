// Unit tests for the co-op lab ISteamNetworking005 loopback emulator, its
// synthetic 1202/1203 callback dispatch, and the interface forwarding proxy.
//
// The emulator core is wired to an injected wire + clock so the P2P semantics
// are exercised deterministically with no sockets, then a second group drives
// two cores over real 127.0.0.1 UDP sockets on different ports.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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
void Check(bool value, const char* what) {
    ++g_checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", g_checks, what);
        std::exit(1);
    }
}

constexpr std::uint64_t kHostId = 0x0110000100000001ull;
constexpr std::uint64_t kGuestId = 0x0110000100000002ull;

struct Recorder {
    std::uint64_t requests[32]{};
    unsigned request_count = 0;
    std::uint64_t fail_remote[32]{};
    std::uint8_t fail_error[32]{};
    unsigned fail_count = 0;
};
void rec_request(void* ctx, std::uint64_t remote) noexcept {
    auto* r = static_cast<Recorder*>(ctx);
    if (r->request_count < 32) r->requests[r->request_count++] = remote;
}
void rec_fail(void* ctx, std::uint64_t remote, std::uint8_t error) noexcept {
    auto* r = static_cast<Recorder*>(ctx);
    if (r->fail_count < 32) {
        r->fail_remote[r->fail_count] = remote;
        r->fail_error[r->fail_count] = error;
        ++r->fail_count;
    }
}

// In-memory wire: encode -> (optional drop) -> decode -> deliver to the peer.
struct Link {
    LoopbackCore* dst = nullptr;
    std::uint32_t loss_permille = 0;
    std::uint64_t counter = 0;
};
bool wire_deliver(void* ctx, const DatagramHeader& header, const void* payload, std::size_t size) noexcept {
    auto* link = static_cast<Link*>(ctx);
    if (should_drop(link->loss_permille, link->counter++)) return true;  // sent, lost on the wire
    static thread_local std::uint8_t buf[kMaxDatagram];
    DatagramHeader local = header;
    local.length = static_cast<std::uint32_t>(size);
    encode_header(local, buf);
    if (size) std::memcpy(buf + kHeaderSize, payload, size);
    DatagramHeader out{};
    const std::uint8_t* p = nullptr;
    std::size_t ps = 0;
    if (decode_datagram(buf, kHeaderSize + size, out, p, ps) != DecodeStatus::ok) return false;
    link->dst->on_datagram(out, p, ps);
    return true;
}
std::uint64_t manual_now(void* ctx) noexcept { return *static_cast<std::uint64_t*>(ctx); }

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
        EmulatorConfig gc{};
        gc.self_id = kGuestId;
        gc.peer_id = kHostId;
        gc.peer_port = 47810;
        gc.timeout_ms = timeout_ms;
        host.start(hc, {&wire_deliver, &host_to_guest}, {&rec_request, &rec_fail, &host_rec}, {&manual_now, &host_now});
        guest.start(gc, {&wire_deliver, &guest_to_host}, {&rec_request, &rec_fail, &guest_rec}, {&manual_now, &guest_now});
    }
    void stop() {
        host.shutdown();
        guest.shutdown();
    }
};

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
    // A second inbound packet does not post another 1202.
    Check(p.guest.send_p2p(kHostId, "yo", 2, steam::kP2PSendUnreliable, 0), "guest send 2 ok");
    Check(p.host_rec.request_count == 1, "1202 posted once per peer");
    // After accept, both queued packets are readable in order with the sender id.
    Check(p.host.accept(kGuestId), "host accept");
    Check(p.host.is_available(&size, 0) && size == 2, "visible after accept");
    Check(p.host.read_p2p(dest, sizeof(dest), &got, &from, 0), "read 1");
    Check(got == 2 && from == kGuestId && std::memcmp(dest, "hi", 2) == 0, "read 1 payload+sender");
    Check(p.host.read_p2p(dest, sizeof(dest), &got, &from, 0) && std::memcmp(dest, "yo", 2) == 0, "read 2");
    Check(!p.host.read_p2p(dest, sizeof(dest), &got, &from, 0), "queue empty");
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
    Check(q.host.accept(kGuestId), "accept reliable peer");
    static std::uint8_t out[200000];
    std::uint32_t got = 0;
    std::uint64_t from = 0;
    Check(q.host.is_available(&got, 0) && got == len, "reassembled size visible");
    Check(q.host.read_p2p(out, sizeof(out), &got, &from, 0), "read reassembled");
    Check(got == len && from == kGuestId, "reassembled length+sender");
    Check(std::memcmp(out, big, len) == 0, "reassembled content matches");
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
    // reports inactive.
    Check(p.host.close(kGuestId), "host close");
    Check(p.guest_rec.fail_count == 1 && p.guest_rec.fail_error[0] == steam::kP2PErrorTimeout, "remote close -> 1203 err 4");
    Check(p.guest_rec.fail_remote[0] == kHostId, "1203 names the host");
    steam::P2PSessionState_t after{};
    Check(!p.host.session_state(kGuestId, &after) && after.connection_active == 0, "closed session inactive");
    p.stop();
}

void TestTimeout() {
    Pair p;
    p.start(/*timeout_ms=*/1000);
    Check(p.guest.send_p2p(kHostId, "hi", 2, 0, 0), "guest send");
    Check(p.host.accept(kGuestId), "accept");
    // No activity for > timeout -> synthetic 1203 on tick.
    p.host_now += 1500;
    p.host.tick();
    Check(p.host_rec.fail_count == 1 && p.host_rec.fail_error[0] == steam::kP2PErrorTimeout, "idle timeout -> 1203");
    Check(p.host_rec.fail_remote[0] == kGuestId, "timeout names the guest");
    // A second tick does not re-post for the same closed peer.
    p.host_now += 1500;
    p.host.tick();
    Check(p.host_rec.fail_count == 1, "timeout posts once");
    p.stop();
}

void TestLossInjection() {
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
}
std::uintptr_t MGSCOOP_STEAM_CALL fake_stub(void*) noexcept { return 0; }

void TestCallbackDispatch() {
    callbacks::reset_for_test();
    const void* vt1202[3] = {reinterpret_cast<const void*>(&fake_stub), reinterpret_cast<const void*>(&fake_run),
                             reinterpret_cast<const void*>(&fake_stub)};
    const void* vt1203[3] = {reinterpret_cast<const void*>(&fake_stub), reinterpret_cast<const void*>(&fake_run),
                             reinterpret_cast<const void*>(&fake_stub)};
    FakeCallback cb1202{vt1202, steam::kCallbackFlagRegistered, {}, steam::kCallbackP2PSessionRequest, 0, 0, 0};
    FakeCallback cb1203{vt1203, steam::kCallbackFlagRegistered, {}, steam::kCallbackP2PSessionConnectFail, 0, 0, 0};
    callbacks::configure(vt1202, vt1203);
    callbacks::on_register(&cb1202, steam::kCallbackP2PSessionRequest);
    callbacks::on_register(&cb1203, steam::kCallbackP2PSessionConnectFail);
    Check(callbacks::tracked(steam::kCallbackP2PSessionRequest) == 1, "1202 tracked");
    Check(callbacks::tracked(steam::kCallbackP2PSessionConnectFail) == 1, "1203 tracked");

    Check(callbacks::post_session_request(kGuestId), "queue 1202");
    Check(callbacks::post_connect_fail(kGuestId, steam::kP2PErrorTimeout), "queue 1203");
    Check(callbacks::pending() == 2, "two pending");
    Check(callbacks::drain() == 2, "two dispatched");
    Check(cb1202.calls == 1 && cb1202.last_remote == kGuestId, "1202 delivered");
    Check(cb1203.calls == 1 && cb1203.last_remote == kGuestId && cb1203.last_error == steam::kP2PErrorTimeout,
          "1203 delivered");
    Check(callbacks::pending() == 0, "queue drained");

    // Wrong vptr is refused.
    const void* vt_other[3] = {nullptr, reinterpret_cast<const void*>(&fake_run), nullptr};
    FakeCallback impostor{vt_other, steam::kCallbackFlagRegistered, {}, steam::kCallbackP2PSessionRequest, 0, 0, 0};
    callbacks::on_register(&impostor, steam::kCallbackP2PSessionRequest);
    callbacks::post_session_request(kGuestId);
    callbacks::drain();
    Check(impostor.calls == 0, "impostor vptr refused");
    Check(cb1202.calls == 2, "genuine still dispatched");

    // Cleared registered flag is refused.
    cb1202.flags = 0;
    callbacks::post_session_request(kGuestId);
    callbacks::drain();
    Check(cb1202.calls == 2, "unregistered-flag refused");
    cb1202.flags = steam::kCallbackFlagRegistered;

    // Unregister stops dispatch.
    callbacks::on_unregister(&cb1202);
    Check(callbacks::tracked(steam::kCallbackP2PSessionRequest) == 1, "only impostor remains tracked");
    callbacks::post_session_request(kGuestId);
    callbacks::drain();
    Check(cb1202.calls == 2, "unregistered object not dispatched");
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

void TestForwardingProxy() {
    const void* vt[4] = {reinterpret_cast<const void*>(&iface_add), reinterpret_cast<const void*>(&iface_getid),
                         reinterpret_cast<const void*>(&iface_name), reinterpret_cast<const void*>(&iface_getid)};
    FakeIface iface{vt, 100};
    ProxyOverride overrides[2] = {{2, reinterpret_cast<const void*>(&override_name)},
                                  {3, reinterpret_cast<const void*>(&override_getid)}};
    void* proxy = build_forwarding_proxy(&iface, 4, overrides, 2, nullptr);
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

    // An override slot past the vtable is rejected.
    ProxyOverride bad[1] = {{9, reinterpret_cast<const void*>(&override_name)}};
    Check(build_forwarding_proxy(&iface, 4, bad, 1, nullptr) == nullptr, "out-of-range override rejected");
}

// ----- Two cores over real 127.0.0.1 UDP sockets on different ports -----
struct SocketSide {
    LoopbackCore core;
    UdpTransport transport;
    Recorder rec;
};
bool side_wire(void* ctx, const DatagramHeader& header, const void* payload, std::size_t size) noexcept {
    auto* side = static_cast<SocketSide*>(ctx);
    std::uint8_t head[kHeaderSize];
    DatagramHeader local = header;
    local.length = static_cast<std::uint32_t>(size);
    encode_header(local, head);
    WireChunk chunks[2] = {{head, kHeaderSize}, {payload, size}};
    return side->transport.transmit(chunks, size ? 2 : 1);
}
void side_datagram(void* ctx, const std::uint8_t* data, std::size_t size) noexcept {
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
    Check(host.transport.start({host_port, guest_port, 100}, {&side_datagram, nullptr, &host}), "host transport start");
    Check(guest.transport.start({guest_port, host_port, 100}, {&side_datagram, nullptr, &guest}), "guest transport start");

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
    Check(intruder.transport.start({48130, host_port, 100}, {&side_datagram, nullptr, &intruder}), "intruder start");
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

    intruder.transport.stop();
    host.transport.stop();
    guest.transport.stop();
    host.core.shutdown();
    guest.core.shutdown();
}
}  // namespace

int main() {
    TestRequestOnceAndInvisibleUntilAccept();
    TestTruncation();
    TestSizeCaps();
    TestSessionStateAndClose();
    TestTimeout();
    TestLossInjection();
    TestCallbackDispatch();
    TestForwardingProxy();
    TestRealSockets();
    std::printf("steam_loopback_tests: %u checks passed\n", g_checks);
    return 0;
}
