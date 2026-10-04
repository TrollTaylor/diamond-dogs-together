// Original co-op lab ISteamNetworking005 loopback emulator.
#include "mgscoop/steam_loopback.hpp"

#ifdef _WIN32
#include "mgscoop/steam_callbacks.hpp"

#include <strsafe.h>
#include <cstring>

namespace mgscoop::lab {
namespace {
using steam::P2PSessionState_t;

LoopbackCore* core_of(void* self) noexcept {
    return static_cast<EmulatorObject*>(self)->core;
}

// Emulated vtable entry points. CSteamID by value is a uint64 register argument;
// out-params and buffers are pointers (verified against the game call sites).
bool MGSCOOP_STEAM_CALL Emu_Send(void* self, std::uint64_t remote, const void* data,
                                 std::uint32_t len, std::int32_t mode, std::int32_t channel) noexcept {
    return core_of(self)->send_p2p(remote, data, len, mode, channel);
}
bool MGSCOOP_STEAM_CALL Emu_Available(void* self, std::uint32_t* size, std::int32_t channel) noexcept {
    return core_of(self)->is_available(size, channel);
}
bool MGSCOOP_STEAM_CALL Emu_Read(void* self, void* dest, std::uint32_t cap, std::uint32_t* size_out,
                                 std::uint64_t* remote_out, std::int32_t channel) noexcept {
    return core_of(self)->read_p2p(dest, cap, size_out, remote_out, channel);
}
bool MGSCOOP_STEAM_CALL Emu_Accept(void* self, std::uint64_t remote) noexcept {
    return core_of(self)->accept(remote);
}
bool MGSCOOP_STEAM_CALL Emu_Close(void* self, std::uint64_t remote) noexcept {
    return core_of(self)->close(remote);
}
bool MGSCOOP_STEAM_CALL Emu_CloseChannel(void* self, std::uint64_t remote, std::int32_t /*channel*/) noexcept {
    return core_of(self)->close(remote);
}
bool MGSCOOP_STEAM_CALL Emu_State(void* self, std::uint64_t remote, P2PSessionState_t* out) noexcept {
    return core_of(self)->session_state(remote, out);
}
// Failure stub for the unused slots (7..21). Zero covers bool/int/pointer under x64.
std::uintptr_t MGSCOOP_STEAM_CALL Emu_Zero(void* /*self*/) noexcept { return 0; }

const void* g_vtable[steam::net_slot::kCount] = {
    reinterpret_cast<const void*>(&Emu_Send),          // 0  SendP2PPacket
    reinterpret_cast<const void*>(&Emu_Available),     // 1  IsP2PPacketAvailable
    reinterpret_cast<const void*>(&Emu_Read),          // 2  ReadP2PPacket
    reinterpret_cast<const void*>(&Emu_Accept),        // 3  AcceptP2PSessionWithUser
    reinterpret_cast<const void*>(&Emu_Close),         // 4  CloseP2PSessionWithUser
    reinterpret_cast<const void*>(&Emu_CloseChannel),  // 5  CloseP2PChannelWithUser
    reinterpret_cast<const void*>(&Emu_State),         // 6  GetP2PSessionState
    reinterpret_cast<const void*>(&Emu_Zero),          // 7  AllowP2PPacketRelay
    reinterpret_cast<const void*>(&Emu_Zero),          // 8
    reinterpret_cast<const void*>(&Emu_Zero),          // 9
    reinterpret_cast<const void*>(&Emu_Zero),          // 10
    reinterpret_cast<const void*>(&Emu_Zero),          // 11
    reinterpret_cast<const void*>(&Emu_Zero),          // 12
    reinterpret_cast<const void*>(&Emu_Zero),          // 13
    reinterpret_cast<const void*>(&Emu_Zero),          // 14
    reinterpret_cast<const void*>(&Emu_Zero),          // 15
    reinterpret_cast<const void*>(&Emu_Zero),          // 16
    reinterpret_cast<const void*>(&Emu_Zero),          // 17
    reinterpret_cast<const void*>(&Emu_Zero),          // 18
    reinterpret_cast<const void*>(&Emu_Zero),          // 19
    reinterpret_cast<const void*>(&Emu_Zero),          // 20
    reinterpret_cast<const void*>(&Emu_Zero),          // 21
};
}  // namespace

const void** emulated_networking_vtable() noexcept { return g_vtable; }

std::uint64_t LoopbackCore::now() noexcept {
    return clock_.now_ms ? clock_.now_ms(clock_.ctx) : GetTickCount64();
}
void LoopbackCore::log_line(const char* line) noexcept {
    if (config_.log) config_.log(line);
}

LoopbackCore::Peer* LoopbackCore::find_peer(std::uint64_t id) noexcept {
    for (auto& p : peers_)
        if (p.used && p.id == id) return &p;
    return nullptr;
}
LoopbackCore::Peer* LoopbackCore::ensure_peer(std::uint64_t id, bool accept_now) noexcept {
    if (auto* existing = find_peer(id)) {
        if (accept_now) existing->accepted = true;
        return existing;
    }
    for (auto& p : peers_) {
        if (!p.used) {
            p = Peer{};
            p.used = true;
            p.id = id;
            p.accepted = accept_now;
            return &p;
        }
    }
    return nullptr;
}

void LoopbackCore::enqueue(std::uint64_t sender, const std::uint8_t* data, std::uint32_t size) noexcept {
    if (queue_count_ >= kMaxQueued) return;  // bounded; drop under flood
    auto* bytes = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, size ? size : 1));
    if (!bytes) return;
    if (size) std::memcpy(bytes, data, size);
    queue_[(queue_head_ + queue_count_) % kMaxQueued] = {sender, bytes, size};
    ++queue_count_;
}

void LoopbackCore::deliver_reliable(Peer& peer, const std::uint8_t* payload, std::size_t size) noexcept {
    if (size <= kFragmentHeaderSize) return;
    std::uint32_t msg = 0, total = 0, offset = 0;
    std::memcpy(&msg, payload, 4);
    std::memcpy(&total, payload + 4, 4);
    std::memcpy(&offset, payload + 8, 4);
    const std::uint8_t* chunk = payload + kFragmentHeaderSize;
    const std::uint32_t chunk_size = static_cast<std::uint32_t>(size - kFragmentHeaderSize);
    if (total == 0 || total > steam::kMaxReliablePayload) return;
    if (offset % kFragmentChunk != 0 || offset >= total) return;
    if (offset + chunk_size > total) return;
    const std::uint32_t index = offset / kFragmentChunk;
    const std::uint32_t chunks = (total + kFragmentChunk - 1) / kFragmentChunk;
    if (chunks > kMaxFragChunks || index >= chunks) return;
    // Non-final chunk must be exactly kFragmentChunk; final is the remainder.
    const std::uint32_t expected = (index + 1 == chunks) ? total - offset : kFragmentChunk;
    if (chunk_size != expected) return;
    if (peer.frag_buffer && peer.frag_msg != msg) {
        HeapFree(GetProcessHeap(), 0, peer.frag_buffer);
        peer.frag_buffer = nullptr;
    }
    if (!peer.frag_buffer) {
        peer.frag_buffer = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, total));
        if (!peer.frag_buffer) return;
        peer.frag_msg = msg;
        peer.frag_total = total;
        peer.frag_mask = 0;
    }
    std::memcpy(peer.frag_buffer + offset, chunk, chunk_size);
    peer.frag_mask |= (1u << index);
    if (peer.frag_mask == (chunks == 32 ? 0xffffffffu : ((1u << chunks) - 1))) {
        enqueue(peer.id, peer.frag_buffer, peer.frag_total);
        HeapFree(GetProcessHeap(), 0, peer.frag_buffer);
        peer.frag_buffer = nullptr;
        peer.frag_mask = 0;
    }
}

void LoopbackCore::start(const EmulatorConfig& config, const LoopbackWire& wire, const SyntheticSink& sink,
                         const LoopbackClock& clock) noexcept {
    config_ = config;
    wire_ = wire;
    sink_ = sink;
    clock_ = clock;
    peer_id_ = config.peer_id;
    started_ = true;
    if (config_.log) {
        char line[160]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "loopback=listening; port=%u; peer_port=%u; self=%llu\r\n",
                0u, config_.peer_port, static_cast<unsigned long long>(config_.self_id))))
            log_line(line);
    }
}

void LoopbackCore::shutdown() noexcept {
    AcquireSRWLockExclusive(&lock_);
    while (queue_count_) {
        auto& pkt = queue_[queue_head_];
        if (pkt.bytes) HeapFree(GetProcessHeap(), 0, pkt.bytes);
        pkt = Packet{};
        queue_head_ = (queue_head_ + 1) % kMaxQueued;
        --queue_count_;
    }
    for (auto& p : peers_) {
        if (p.frag_buffer) HeapFree(GetProcessHeap(), 0, p.frag_buffer);
        p = Peer{};
    }
    started_ = false;
    ReleaseSRWLockExclusive(&lock_);
}

bool LoopbackCore::send_p2p(std::uint64_t remote, const void* data, std::uint32_t len,
                            std::int32_t mode, std::int32_t channel) noexcept {
    if (!started_ || !steam::is_valid_mode(mode)) return false;
    const bool reliable = steam::is_reliable_mode(mode);
    const std::uint32_t cap = reliable ? steam::kMaxReliablePayload : steam::kMaxUnreliablePayload;
    if (len > cap) return false;
    if (len && !data) return false;
    AcquireSRWLockExclusive(&lock_);
    Peer* peer = ensure_peer(remote, /*accept_now=*/true);  // Send implicitly accepts
    if (peer) {
        peer->had_activity = true;
        peer->closed = false;
        peer->last_activity_ms = now();
    }
    ReleaseSRWLockExclusive(&lock_);
    if (!peer || !wire_.send) return false;
    if (!reliable || len <= kMaxDataPayload) {
        DatagramHeader header{};
        header.type = DatagramType::Data;
        header.send_mode = static_cast<std::uint8_t>(mode);
        header.channel = channel;
        header.src = config_.self_id;
        header.dst = remote;
        header.length = len;
        return wire_.send(wire_.ctx, header, data, len);
    }
    // Reliable payload larger than one datagram: fragment it.
    static volatile long s_msg_counter = 0;
    const std::uint32_t msg = static_cast<std::uint32_t>(InterlockedIncrement(&s_msg_counter));
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    bool ok = true;
    for (std::uint32_t offset = 0; offset < len && ok; offset += kFragmentChunk) {
        const std::uint32_t chunk = (len - offset < kFragmentChunk) ? len - offset : kFragmentChunk;
        std::uint8_t head[kFragmentHeaderSize];
        std::memcpy(head + 0, &msg, 4);
        std::memcpy(head + 4, &len, 4);
        std::memcpy(head + 8, &offset, 4);
        std::uint8_t frag[kFragmentHeaderSize + kFragmentChunk];
        std::memcpy(frag, head, kFragmentHeaderSize);
        std::memcpy(frag + kFragmentHeaderSize, bytes + offset, chunk);
        DatagramHeader header{};
        header.type = DatagramType::Fragment;
        header.send_mode = static_cast<std::uint8_t>(mode);
        header.channel = channel;
        header.src = config_.self_id;
        header.dst = remote;
        header.length = kFragmentHeaderSize + chunk;
        ok = wire_.send(wire_.ctx, header, frag, header.length);
    }
    return ok;
}

bool LoopbackCore::is_available(std::uint32_t* size, std::int32_t /*channel*/) noexcept {
    bool found = false;
    AcquireSRWLockExclusive(&lock_);
    for (unsigned i = 0; i < queue_count_; ++i) {
        const Packet& pkt = queue_[(queue_head_ + i) % kMaxQueued];
        const Peer* peer = find_peer(pkt.sender);
        if (peer && peer->accepted && !peer->closed) {
            if (size) *size = pkt.size;
            found = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return found;
}

bool LoopbackCore::read_p2p(void* dest, std::uint32_t cap, std::uint32_t* size_out,
                            std::uint64_t* remote_out, std::int32_t /*channel*/) noexcept {
    bool read = false;
    AcquireSRWLockExclusive(&lock_);
    for (unsigned i = 0; i < queue_count_; ++i) {
        const unsigned slot = (queue_head_ + i) % kMaxQueued;
        Packet& pkt = queue_[slot];
        const Peer* peer = find_peer(pkt.sender);
        if (!peer || !peer->accepted || peer->closed) continue;
        const std::uint32_t copy = pkt.size < cap ? pkt.size : cap;  // truncate to caller buffer
        if (dest && copy) std::memcpy(dest, pkt.bytes, copy);
        if (size_out) *size_out = copy;
        if (remote_out) *remote_out = pkt.sender;
        if (pkt.bytes) HeapFree(GetProcessHeap(), 0, pkt.bytes);
        // Remove entry i by shifting the ring forward over the gap.
        for (unsigned j = i; j + 1 < queue_count_; ++j) {
            queue_[(queue_head_ + j) % kMaxQueued] = queue_[(queue_head_ + j + 1) % kMaxQueued];
        }
        queue_[(queue_head_ + queue_count_ - 1) % kMaxQueued] = Packet{};
        --queue_count_;
        read = true;
        break;
    }
    ReleaseSRWLockExclusive(&lock_);
    return read;
}

bool LoopbackCore::accept(std::uint64_t remote) noexcept {
    bool ok = false;
    AcquireSRWLockExclusive(&lock_);
    if (Peer* peer = find_peer(remote)) {
        if (!peer->closed) {
            peer->accepted = true;
            peer->had_activity = true;
            peer->last_activity_ms = now();
            ok = true;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    if (ok && config_.log) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "loopback=accepted; peer=%llu\r\n",
                                       static_cast<unsigned long long>(remote))))
            log_line(line);
    }
    return ok;
}

bool LoopbackCore::close(std::uint64_t remote) noexcept {
    bool existed = false;
    AcquireSRWLockExclusive(&lock_);
    if (Peer* peer = find_peer(remote)) {
        peer->closed = true;
        peer->accepted = false;
        existed = true;
    }
    ReleaseSRWLockExclusive(&lock_);
    if (wire_.send) {
        DatagramHeader header{};
        header.type = DatagramType::Close;
        header.src = config_.self_id;
        header.dst = remote;
        header.length = 0;
        wire_.send(wire_.ctx, header, nullptr, 0);
    }
    return existed;
}

bool LoopbackCore::session_state(std::uint64_t remote, P2PSessionState_t* out) noexcept {
    if (!out) return false;
    AcquireSRWLockExclusive(&lock_);
    const Peer* peer = find_peer(remote);
    const bool active = peer && peer->used && !peer->closed;
    *out = P2PSessionState_t{};
    if (active) {
        out->connection_active = 1;
        out->p2p_session_error = steam::kP2PErrorNone;
        out->remote_ip = 0x7F000001u;
        out->remote_port = config_.peer_port;
    }
    ReleaseSRWLockExclusive(&lock_);
    return active;
}

void LoopbackCore::announce(bool reply) noexcept {
    if (!started_ || !wire_.send) return;
    DatagramHeader header{};
    header.type = DatagramType::Hello;
    header.send_mode = reply ? kHelloReplyRequested : 0;
    header.src = config_.self_id;
    header.dst = peer_id_;
    std::size_t name_len = 0;
    while (name_len < kMaxHelloName && config_.self_name[name_len]) ++name_len;
    header.length = static_cast<std::uint32_t>(name_len);
    wire_.send(wire_.ctx, header, config_.self_name, name_len);
}

void LoopbackCore::on_datagram(const DatagramHeader& header, const std::uint8_t* payload,
                               std::size_t payload_size) noexcept {
    const std::uint64_t sender = header.src;
    if (!steam::is_individual_steamid(sender)) return;
    if (peer_id_ != 0 && sender != peer_id_) return;  // only the configured peer

    bool post_request = false, post_fail = false, reply_hello = false;
    AcquireSRWLockExclusive(&lock_);
    if (peer_id_ == 0) peer_id_ = sender;
    if (header.type == DatagramType::Close) {
        if (Peer* peer = find_peer(sender)) {
            if (!peer->closed) {
                peer->closed = true;
                peer->accepted = false;
                post_fail = true;
            }
        }
    } else {
        Peer* peer = ensure_peer(sender, /*accept_now=*/false);
        if (peer) {
            peer->had_activity = true;
            peer->last_activity_ms = now();
            // First contact from a peer we have not Sent to or Accepted: one 1202.
            if (!peer->accepted && !peer->request_posted) {
                peer->request_posted = true;
                post_request = true;
            }
            if (header.type == DatagramType::Data && payload_size) {
                enqueue(sender, payload, static_cast<std::uint32_t>(payload_size));
            } else if (header.type == DatagramType::Fragment) {
                deliver_reliable(*peer, payload, payload_size);
            } else if (header.type == DatagramType::Hello && header.send_mode == kHelloReplyRequested) {
                reply_hello = true;
            }
        }
    }
    ReleaseSRWLockExclusive(&lock_);

    if (header.type == DatagramType::Hello && config_.log) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "loopback=hello; peer=%llu\r\n",
                                       static_cast<unsigned long long>(sender))))
            log_line(line);
    }
    if (reply_hello) announce(/*reply=*/false);
    if (post_request) {
        if (sink_.post_request) sink_.post_request(sink_.ctx, sender);
        if (config_.log) {
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                    "loopback=session_request_posted; peer=%llu\r\n", static_cast<unsigned long long>(sender))))
                log_line(line);
        }
    }
    if (post_fail) {
        if (sink_.post_fail) sink_.post_fail(sink_.ctx, sender, steam::kP2PErrorTimeout);
        if (config_.log) {
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                    "loopback=connect_fail; peer=%llu; error=%d\r\n",
                    static_cast<unsigned long long>(sender), steam::kP2PErrorTimeout)))
                log_line(line);
        }
    }
}

void LoopbackCore::tick() noexcept {
    std::uint64_t timed_out[kMaxPeers];
    unsigned count = 0;
    const std::uint64_t t = now();
    AcquireSRWLockExclusive(&lock_);
    for (auto& peer : peers_) {
        if (peer.used && !peer.closed && peer.had_activity &&
            config_.timeout_ms && t - peer.last_activity_ms > config_.timeout_ms) {
            peer.closed = true;
            peer.accepted = false;
            timed_out[count++] = peer.id;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    for (unsigned i = 0; i < count; ++i) {
        if (sink_.post_fail) sink_.post_fail(sink_.ctx, timed_out[i], steam::kP2PErrorTimeout);
        if (config_.log) {
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                    "loopback=connect_fail; peer=%llu; error=%d\r\n",
                    static_cast<unsigned long long>(timed_out[i]), steam::kP2PErrorTimeout)))
                log_line(line);
        }
    }
}

void LoopbackCore::drop_front_visible() noexcept {}

// ---------------------------------------------------------------------------
// Process-wide integration: a single emulator wired to a UDP socket, whose
// synthetic events flow into the callback module.
// ---------------------------------------------------------------------------
namespace {
EmulatorConfig g_config{};
std::uint16_t g_listen_port = 0;
bool g_configured = false;
LoopbackCore g_core;
UdpTransport g_transport;
EmulatorObject g_object{};
INIT_ONCE g_start_once = INIT_ONCE_STATIC_INIT;
volatile long g_started = 0;

bool wire_send(void*, const DatagramHeader& header, const void* payload, std::size_t size) noexcept {
    std::uint8_t head[kHeaderSize];
    DatagramHeader local = header;
    local.length = static_cast<std::uint32_t>(size);
    encode_header(local, head);
    WireChunk chunks[2] = {{head, kHeaderSize}, {payload, size}};
    return g_transport.transmit(chunks, size ? 2 : 1);
}
void sink_request(void*, std::uint64_t remote) noexcept { callbacks::post_session_request(remote); }
void sink_fail(void*, std::uint64_t remote, std::uint8_t error) noexcept {
    callbacks::post_connect_fail(remote, error);
}
void transport_datagram(void*, const std::uint8_t* data, std::size_t size) noexcept {
    DatagramHeader header{};
    const std::uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
    if (decode_datagram(data, size, header, payload, payload_size) != DecodeStatus::ok) return;
    g_core.on_datagram(header, payload, payload_size);
}
void transport_idle(void*) noexcept { g_core.tick(); }

BOOL CALLBACK start_once(PINIT_ONCE, PVOID, PVOID*) noexcept {
    LoopbackWire wire{&wire_send, nullptr};
    SyntheticSink sink{&sink_request, &sink_fail, nullptr};
    LoopbackClock clock{};  // real GetTickCount64
    g_core.start(g_config, wire, sink, clock);
    g_object.vtable = emulated_networking_vtable();
    g_object.core = &g_core;
    TransportConfig tc{};
    tc.listen_port = g_listen_port;
    tc.peer_port = g_config.peer_port;
    tc.idle_interval_ms = 250;
    DatagramSink ds{&transport_datagram, &transport_idle, nullptr};
    if (g_transport.start(tc, ds)) {
        InterlockedExchange(&g_started, 1);
        g_core.announce(/*reply=*/true);
    } else if (g_config.log) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "loopback=disabled; reason=transport_error_%d\r\n", static_cast<int>(g_transport.error()))))
            g_config.log(line);
    }
    return TRUE;
}
}  // namespace

void loopback_configure(const EmulatorConfig& config, std::uint16_t listen_port) noexcept {
    g_config = config;
    g_listen_port = listen_port;
    g_configured = listen_port != 0 && config.peer_port != 0 && config.self_id != 0;
}

bool loopback_ensure_started() noexcept {
    if (!g_configured) return false;
    InitOnceExecuteOnce(&g_start_once, start_once, nullptr, nullptr);
    return g_started != 0;
}

void* loopback_interface_object() noexcept {
    if (!g_configured) return nullptr;
    loopback_ensure_started();
    return g_started ? static_cast<void*>(&g_object) : nullptr;
}

void loopback_reset_for_test() noexcept {
    g_core.shutdown();
    g_transport.stop();
    g_configured = false;
    g_started = 0;
    g_config = EmulatorConfig{};
    g_listen_port = 0;
}

}  // namespace mgscoop::lab
#endif  // _WIN32
