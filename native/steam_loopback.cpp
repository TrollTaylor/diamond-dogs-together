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
// out-params and buffers are pointers (verified against the game call sites:
// Send 0x1A5FB60 passes mode/channel at [rsp+20h]/[rsp+28h], Read 0x1A5FAC0
// passes remote_out/channel at [rsp+20h]/[rsp+28h], the poll at 0x1A5FBF0
// passes channel in r8d).
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
bool MGSCOOP_STEAM_CALL Emu_CloseChannel(void* self, std::uint64_t remote, std::int32_t channel) noexcept {
    return core_of(self)->close_channel(remote, channel);
}
bool MGSCOOP_STEAM_CALL Emu_State(void* self, std::uint64_t remote, P2PSessionState_t* out) noexcept {
    return core_of(self)->session_state(remote, out);
}
// Failure stub for the unused slots (7..21) and for every slot of the dead
// object. Zero covers bool/int/pointer returns under x64.
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

#define MGSCOOP_ZERO4 reinterpret_cast<const void*>(&Emu_Zero), reinterpret_cast<const void*>(&Emu_Zero), \
                      reinterpret_cast<const void*>(&Emu_Zero), reinterpret_cast<const void*>(&Emu_Zero)
const void* g_dead_vtable[steam::net_slot::kCount] = {
    MGSCOOP_ZERO4, MGSCOOP_ZERO4, MGSCOOP_ZERO4, MGSCOOP_ZERO4, MGSCOOP_ZERO4,
    reinterpret_cast<const void*>(&Emu_Zero), reinterpret_cast<const void*>(&Emu_Zero),
};
#undef MGSCOOP_ZERO4
static_assert(sizeof(g_dead_vtable) / sizeof(g_dead_vtable[0]) == steam::net_slot::kCount, "dead vtable size");
EmulatorObject g_dead_object{g_dead_vtable, nullptr};

bool printable(char c) noexcept { return c >= 0x20 && c <= 0x7e; }
}  // namespace

const void** emulated_networking_vtable() noexcept { return g_vtable; }
void* dead_networking_object() noexcept { return &g_dead_object; }

std::uint64_t LoopbackCore::now() noexcept {
    return clock_.now_ms ? clock_.now_ms(clock_.ctx) : GetTickCount64();
}
void LoopbackCore::log_line(const char* line) noexcept {
    if (config_.log) config_.log(line);
}
void LoopbackCore::log_peer(const char* event, std::uint64_t peer) noexcept {
    if (!config_.log) return;
    char line[128]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "loopback=%s; peer=%llu\r\n", event,
                                   static_cast<unsigned long long>(peer))))
        log_line(line);
}

LoopbackCore::Peer* LoopbackCore::find_peer(std::uint64_t id) noexcept {
    for (auto& p : peers_)
        if (p.used && p.id == id) return &p;
    return nullptr;
}
LoopbackCore::Peer* LoopbackCore::ensure_peer(std::uint64_t id) noexcept {
    if (auto* existing = find_peer(id)) return existing;
    for (auto& p : peers_) {
        if (!p.used) {
            p = Peer{};
            p.used = true;
            p.id = id;
            return &p;
        }
    }
    return nullptr;
}

void LoopbackCore::drop_reassembly(Peer& peer) noexcept {
    if (peer.frag_buffer) HeapFree(GetProcessHeap(), 0, peer.frag_buffer);
    peer.frag_buffer = nullptr;
    peer.frag_msg = 0;
    peer.frag_total = 0;
    peer.frag_mask = 0;
    peer.frag_channel = 0;
}

void LoopbackCore::reset_peer(Peer& peer) noexcept {
    drop_reassembly(peer);
    purge(peer.id, /*any_channel=*/true, 0);
    peer = Peer{};
}

void LoopbackCore::note_channel(Peer& peer, std::int32_t channel) noexcept {
    for (unsigned i = 0; i < peer.channel_count; ++i)
        if (peer.channels[i] == channel) return;
    if (peer.channel_count < kMaxChannels)
        peer.channels[peer.channel_count++] = channel;
    else
        peer.channel_overflow = true;  // untracked channels: never auto-close on CloseChannel
}

bool LoopbackCore::forget_channel(Peer& peer, std::int32_t channel) noexcept {
    for (unsigned i = 0; i < peer.channel_count; ++i) {
        if (peer.channels[i] == channel) {
            peer.channels[i] = peer.channels[--peer.channel_count];
            peer.channels[peer.channel_count] = 0;
            break;
        }
    }
    return peer.channel_count == 0 && !peer.channel_overflow;
}

void LoopbackCore::remove_packet(unsigned index) noexcept {
    Packet& pkt = queue_[(queue_head_ + index) % kMaxQueued];
    if (pkt.bytes) HeapFree(GetProcessHeap(), 0, pkt.bytes);
    for (unsigned j = index; j + 1 < queue_count_; ++j)
        queue_[(queue_head_ + j) % kMaxQueued] = queue_[(queue_head_ + j + 1) % kMaxQueued];
    queue_[(queue_head_ + queue_count_ - 1) % kMaxQueued] = Packet{};
    --queue_count_;
}

void LoopbackCore::purge(std::uint64_t sender, bool any_channel, std::int32_t channel) noexcept {
    for (unsigned i = 0; i < queue_count_;) {
        const Packet& pkt = queue_[(queue_head_ + i) % kMaxQueued];
        if (pkt.sender == sender && (any_channel || pkt.channel == channel))
            remove_packet(i);
        else
            ++i;
    }
}

bool LoopbackCore::enqueue(std::uint64_t sender, const std::uint8_t* data, std::uint32_t size, std::int32_t channel,
                           std::uint64_t at) noexcept {
    if (queue_count_ >= kMaxQueued) {
        // Make room by evicting the oldest packet of a session the game has not
        // accepted; packets of accepted sessions are never evicted.
        bool evicted = false;
        for (unsigned i = 0; i < queue_count_; ++i) {
            const Packet& pkt = queue_[(queue_head_ + i) % kMaxQueued];
            const Peer* owner = find_peer(pkt.sender);
            if (!owner || !owner->accepted) {
                remove_packet(i);
                evicted = true;
                break;
            }
        }
        if (!evicted) return false;  // bounded; drop under flood
    }
    auto* bytes = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, size ? size : 1));
    if (!bytes) return false;
    if (size) std::memcpy(bytes, data, size);
    queue_[(queue_head_ + queue_count_) % kMaxQueued] = {sender, bytes, size, channel, at};
    ++queue_count_;
    return true;
}

void LoopbackCore::deliver_reliable(Peer& peer, const std::uint8_t* payload, std::size_t size, std::int32_t channel,
                                    std::uint64_t at) noexcept {
    if (size <= kFragmentHeaderSize) return;
    std::uint32_t msg = 0, total = 0, offset = 0;
    std::memcpy(&msg, payload, 4);
    std::memcpy(&total, payload + 4, 4);
    std::memcpy(&offset, payload + 8, 4);
    const std::uint8_t* chunk = payload + kFragmentHeaderSize;
    const std::uint32_t chunk_size = static_cast<std::uint32_t>(size - kFragmentHeaderSize);
    if (total == 0 || total > steam::kMaxReliablePayload) return;
    if (offset % kFragmentChunk != 0 || offset >= total) return;
    if (chunk_size > total - offset) return;
    const std::uint32_t index = offset / kFragmentChunk;
    const std::uint32_t chunks = (total + kFragmentChunk - 1) / kFragmentChunk;
    if (chunks > kMaxFragChunks || index >= chunks) return;
    // Non-final chunk must be exactly kFragmentChunk; final is the remainder.
    const std::uint32_t expected = (index + 1 == chunks) ? total - offset : kFragmentChunk;
    if (chunk_size != expected) return;
    // A fragment that does not belong to the message being reassembled (other
    // id, other total, other channel) abandons it. The buffer is always sized
    // from this very fragment's total, so the copy below stays in bounds.
    if (peer.frag_buffer && (peer.frag_msg != msg || peer.frag_total != total || peer.frag_channel != channel))
        drop_reassembly(peer);
    if (!peer.frag_buffer) {
        peer.frag_buffer = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, total));
        if (!peer.frag_buffer) return;
        peer.frag_msg = msg;
        peer.frag_total = total;
        peer.frag_channel = channel;
        peer.frag_mask = 0;
    }
    if (offset > peer.frag_total || chunk_size > peer.frag_total - offset) {  // invariant guard
        drop_reassembly(peer);
        return;
    }
    std::memcpy(peer.frag_buffer + offset, chunk, chunk_size);
    peer.frag_mask |= (1u << index);
    if (peer.frag_mask == (chunks == 32 ? 0xffffffffu : ((1u << chunks) - 1))) {
        enqueue(peer.id, peer.frag_buffer, peer.frag_total, peer.frag_channel, at);
        drop_reassembly(peer);
    }
}

void LoopbackCore::start(const EmulatorConfig& config, const LoopbackWire& wire, const SyntheticSink& sink,
                         const LoopbackClock& clock) noexcept {
    AcquireSRWLockExclusive(&lock_);
    config_ = config;
    wire_ = wire;
    sink_ = sink;
    clock_ = clock;
    peer_id_ = steam::is_individual_steamid(config.peer_id) ? config.peer_id : 0;
    rejected_logged_ = 0;
    // Random per-process start for reliable message ids, so a restarted peer
    // does not reuse the ids of a half-delivered message.
    LARGE_INTEGER qpc{};
    QueryPerformanceCounter(&qpc);
    const auto mixed = static_cast<std::uint32_t>(qpc.QuadPart) ^ static_cast<std::uint32_t>(qpc.QuadPart >> 32) ^
                       (static_cast<std::uint32_t>(GetCurrentProcessId()) << 16) ^
                       static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(this) >> 4);
    msg_seq_ = static_cast<long>(mixed);
    ReleaseSRWLockExclusive(&lock_);
    InterlockedExchange(&started_, 1);
}

void LoopbackCore::shutdown() noexcept {
    InterlockedExchange(&started_, 0);
    AcquireSRWLockExclusive(&lock_);
    while (queue_count_) remove_packet(0);
    queue_head_ = 0;
    for (auto& p : peers_) {
        drop_reassembly(p);
        p = Peer{};
    }
    ReleaseSRWLockExclusive(&lock_);
}

std::uint64_t LoopbackCore::learned_peer() noexcept {
    AcquireSRWLockShared(&lock_);
    const std::uint64_t id = peer_id_;
    ReleaseSRWLockShared(&lock_);
    return id;
}
unsigned LoopbackCore::peer_count() noexcept {
    AcquireSRWLockShared(&lock_);
    unsigned n = 0;
    for (const auto& p : peers_)
        if (p.used) ++n;
    ReleaseSRWLockShared(&lock_);
    return n;
}
unsigned LoopbackCore::queued_count() noexcept {
    AcquireSRWLockShared(&lock_);
    const unsigned n = queue_count_;
    ReleaseSRWLockShared(&lock_);
    return n;
}

bool LoopbackCore::send_control(DatagramType type, std::uint64_t remote, bool droppable) noexcept {
    if (!wire_.send) return false;
    DatagramHeader header{};
    header.type = type;
    header.src = config_.self_id;
    header.dst = remote;
    header.length = 0;
    return wire_.send(wire_.ctx, header, nullptr, 0, droppable);
}

void LoopbackCore::post_request(std::uint64_t remote) noexcept {
    const bool ok = sink_.post_request && sink_.post_request(sink_.ctx, remote);
    if (!ok) {
        // Not queued: let the next inbound packet or tick ask again.
        AcquireSRWLockExclusive(&lock_);
        if (Peer* peer = find_peer(remote))
            if (!peer->accepted) peer->request_posted = false;
        ReleaseSRWLockExclusive(&lock_);
        return;
    }
    log_peer("session_request_posted", remote);
}

void LoopbackCore::post_fail(std::uint64_t remote) noexcept {
    if (sink_.post_fail) sink_.post_fail(sink_.ctx, remote, steam::kP2PErrorTimeout);
    if (config_.log) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "loopback=connect_fail; peer=%llu; error=%d\r\n",
                                       static_cast<unsigned long long>(remote), steam::kP2PErrorTimeout)))
            log_line(line);
    }
}

bool LoopbackCore::send_p2p(std::uint64_t remote, const void* data, std::uint32_t len,
                            std::int32_t mode, std::int32_t channel) noexcept {
    if (!started_ || !steam::is_valid_mode(mode) || !steam::is_individual_steamid(remote)) return false;
    const bool reliable = steam::is_reliable_mode(mode);
    const std::uint32_t cap = reliable ? steam::kMaxReliablePayload : steam::kMaxUnreliablePayload;
    if (len > cap) return false;
    if (len && !data) return false;
    const std::uint64_t t = now();
    AcquireSRWLockExclusive(&lock_);
    Peer* peer = (peer_id_ == 0 || remote == peer_id_) ? ensure_peer(remote) : nullptr;
    if (peer) {
        if (!peer->accepted) {  // Send implicitly opens/accepts the session
            peer->accepted = true;
            peer->request_posted = false;
            peer->open_ms = t;
            peer->last_ping_ms = t;
        }
        note_channel(*peer, channel);
    }
    ReleaseSRWLockExclusive(&lock_);
    if (!peer || !wire_.send) return false;
    if (reliable && len > kMaxDataPayload)
        return send_fragmented(remote, static_cast<const std::uint8_t*>(data), len, mode, channel);
    DatagramHeader header{};
    header.type = DatagramType::Data;
    header.send_mode = static_cast<std::uint8_t>(mode);
    header.channel = channel;
    header.src = config_.self_id;
    header.dst = remote;
    header.length = len;
    const WireChunk chunk{data, len};
    return wire_.send(wire_.ctx, header, &chunk, len ? 1u : 0u, true);
}

bool LoopbackCore::send_fragmented(std::uint64_t remote, const std::uint8_t* bytes, std::uint32_t len,
                                   std::int32_t mode, std::int32_t channel) noexcept {
    // Scatter-send header | 12-byte fragment header | payload slice: nothing
    // large is copied or placed on the caller's (game thread's) stack.
    const auto msg = static_cast<std::uint32_t>(InterlockedIncrement(&msg_seq_));
    bool ok = true;
    for (std::uint32_t offset = 0; offset < len && ok; offset += kFragmentChunk) {
        const std::uint32_t chunk = (len - offset < kFragmentChunk) ? len - offset : kFragmentChunk;
        std::uint8_t head[kFragmentHeaderSize];
        std::memcpy(head + 0, &msg, 4);
        std::memcpy(head + 4, &len, 4);
        std::memcpy(head + 8, &offset, 4);
        DatagramHeader header{};
        header.type = DatagramType::Fragment;
        header.send_mode = static_cast<std::uint8_t>(mode);
        header.channel = channel;
        header.src = config_.self_id;
        header.dst = remote;
        header.length = static_cast<std::uint32_t>(kFragmentHeaderSize + chunk);
        const WireChunk chunks[2] = {{head, kFragmentHeaderSize}, {bytes + offset, chunk}};
        ok = wire_.send(wire_.ctx, header, chunks, 2, true);
    }
    return ok;
}

bool LoopbackCore::is_available(std::uint32_t* size, std::int32_t channel) noexcept {
    bool found = false;
    AcquireSRWLockExclusive(&lock_);
    for (unsigned i = 0; i < queue_count_; ++i) {
        const Packet& pkt = queue_[(queue_head_ + i) % kMaxQueued];
        if (pkt.channel != channel) continue;
        const Peer* peer = find_peer(pkt.sender);
        if (peer && peer->accepted) {
            if (size) *size = pkt.size;
            found = true;
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    return found;
}

bool LoopbackCore::read_p2p(void* dest, std::uint32_t cap, std::uint32_t* size_out,
                            std::uint64_t* remote_out, std::int32_t channel) noexcept {
    bool read = false;
    AcquireSRWLockExclusive(&lock_);
    for (unsigned i = 0; i < queue_count_; ++i) {
        const Packet& pkt = queue_[(queue_head_ + i) % kMaxQueued];
        if (pkt.channel != channel) continue;
        const Peer* peer = find_peer(pkt.sender);
        if (!peer || !peer->accepted) continue;
        const std::uint32_t copy = pkt.size < cap ? pkt.size : cap;  // truncate to caller buffer
        if (dest && copy) std::memcpy(dest, pkt.bytes, copy);
        if (size_out) *size_out = copy;
        if (remote_out) *remote_out = pkt.sender;
        remove_packet(i);
        read = true;
        break;
    }
    ReleaseSRWLockExclusive(&lock_);
    return read;
}

bool LoopbackCore::accept(std::uint64_t remote) noexcept {
    if (!started_ || !steam::is_individual_steamid(remote)) return false;
    const std::uint64_t t = now();
    bool ok = false;
    AcquireSRWLockExclusive(&lock_);
    if (peer_id_ == 0 || remote == peer_id_) {
        if (Peer* peer = ensure_peer(remote)) {
            if (!peer->accepted) {
                peer->accepted = true;
                peer->request_posted = false;
                peer->open_ms = t;
                peer->last_ping_ms = t;
            }
            ok = true;
        }
    }
    ReleaseSRWLockExclusive(&lock_);
    if (ok) log_peer("accepted", remote);
    return ok;
}

bool LoopbackCore::close(std::uint64_t remote) noexcept {
    if (!started_) return false;
    bool existed = false;
    AcquireSRWLockExclusive(&lock_);
    if (Peer* peer = find_peer(remote)) {
        reset_peer(*peer);
        existed = true;
    }
    ReleaseSRWLockExclusive(&lock_);
    if (existed) send_control(DatagramType::Close, remote, /*droppable=*/false);
    return existed;
}

bool LoopbackCore::close_channel(std::uint64_t remote, std::int32_t channel) noexcept {
    if (!started_) return false;
    bool existed = false, last = false;
    AcquireSRWLockExclusive(&lock_);
    if (Peer* peer = find_peer(remote)) {
        existed = true;
        purge(remote, /*any_channel=*/false, channel);
        last = forget_channel(*peer, channel);
        if (last) reset_peer(*peer);
    }
    ReleaseSRWLockExclusive(&lock_);
    if (last) send_control(DatagramType::Close, remote, /*droppable=*/false);
    return existed;
}

bool LoopbackCore::session_state(std::uint64_t remote, P2PSessionState_t* out) noexcept {
    if (!out) return false;
    *out = P2PSessionState_t{};
    AcquireSRWLockExclusive(&lock_);
    const Peer* peer = find_peer(remote);
    if (peer) {
        out->connection_active = peer->accepted ? 1 : 0;
        out->connecting = peer->accepted ? 0 : 1;
        out->p2p_session_error = steam::kP2PErrorNone;
        out->remote_ip = 0x7F000001u;
        out->remote_port = config_.peer_port;
    }
    ReleaseSRWLockExclusive(&lock_);
    return peer != nullptr;
}

void LoopbackCore::announce(bool reply) noexcept {
    if (!started_ || !wire_.send) return;
    DatagramHeader header{};
    header.type = DatagramType::Hello;
    header.send_mode = reply ? kHelloReplyRequested : 0;
    header.src = config_.self_id;
    header.dst = learned_peer();
    std::size_t name_len = 0;
    while (name_len < kMaxHelloName && config_.self_name[name_len]) ++name_len;
    header.length = static_cast<std::uint32_t>(name_len);
    const WireChunk chunk{config_.self_name, name_len};
    wire_.send(wire_.ctx, header, &chunk, name_len ? 1u : 0u, /*droppable=*/false);
}

void LoopbackCore::on_datagram(const DatagramHeader& header, const std::uint8_t* payload,
                               std::size_t payload_size) noexcept {
    if (!started_) return;
    const std::uint64_t sender = header.src;
    if (!steam::is_individual_steamid(sender)) return;
    const std::uint64_t t = now();

    bool reject = false, learned = false, hello = false, reply_hello = false;
    bool want_request = false, want_fail = false, log_reject = false;
    char name[kMaxHelloName + 1]{};
    AcquireSRWLockExclusive(&lock_);
    if (peer_id_ != 0 && sender != peer_id_) reject = true;                     // only the configured peer
    if (header.dst != 0 && header.dst != config_.self_id) reject = true;        // addressed to someone else
    if (reject) {
        log_reject = rejected_logged_ < 8;
        if (log_reject) ++rejected_logged_;
    } else {
        if (peer_id_ == 0) {
            peer_id_ = sender;
            learned = true;
        }
        Peer* peer = find_peer(sender);
        switch (header.type) {
        case DatagramType::Hello:
            hello = true;
            for (std::size_t i = 0; i < payload_size && i < kMaxHelloName; ++i)
                name[i] = printable(static_cast<char>(payload[i])) ? static_cast<char>(payload[i]) : '?';
            reply_hello = header.send_mode == kHelloReplyRequested;
            if (peer) {
                peer->last_inbound_ms = t;
                drop_reassembly(*peer);  // a (re)started peer abandons any half-sent message
            }
            break;
        case DatagramType::Ping:
            if (peer) peer->last_inbound_ms = t;  // liveness only; never creates a session
            break;
        case DatagramType::Close:
            if (peer) {
                want_fail = peer->accepted;  // the game knew this session
                reset_peer(*peer);
            }
            break;
        case DatagramType::Data:
        case DatagramType::Fragment:
            if (!peer) peer = ensure_peer(sender);
            if (!peer) break;  // all slots in use
            peer->last_inbound_ms = t;
            note_channel(*peer, header.channel);
            if (header.type == DatagramType::Data)
                enqueue(sender, payload, static_cast<std::uint32_t>(payload_size), header.channel, t);
            else
                deliver_reliable(*peer, payload, payload_size, header.channel, t);
            if (!peer->accepted &&
                (!peer->request_posted || t - peer->request_ms >= config_.request_repost_ms)) {
                peer->request_posted = true;
                peer->request_ms = t;
                want_request = true;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&lock_);

    if (log_reject && config_.log) {
        char line[160]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "loopback=rejected; reason=peer_mismatch; src=%llu; dst=%llu\r\n",
                                       static_cast<unsigned long long>(sender),
                                       static_cast<unsigned long long>(header.dst))))
            log_line(line);
    }
    if (reject) return;
    if (config_.on_peer && (hello || learned)) config_.on_peer(sender, name);
    if (hello) log_peer("hello", sender);
    if (reply_hello) announce(/*reply=*/false);
    if (want_request) post_request(sender);
    if (want_fail) post_fail(sender);
}

void LoopbackCore::tick() noexcept {
    if (!started_) return;
    std::uint64_t pings[kMaxPeers], requests[kMaxPeers], fails[kMaxPeers], expired[kMaxPeers];
    unsigned np = 0, nr = 0, nf = 0, ne = 0;
    const std::uint64_t t = now();
    const std::uint32_t timeout = config_.timeout_ms;
    AcquireSRWLockExclusive(&lock_);
    for (auto& peer : peers_) {
        if (!peer.used) continue;
        if (peer.accepted) {
            // Liveness counts inbound datagrams only (local sends do not prove
            // the peer is alive); the session open time is the grace start.
            const std::uint64_t ref = peer.last_inbound_ms > peer.open_ms ? peer.last_inbound_ms : peer.open_ms;
            if (timeout && t > ref && t - ref > timeout) {
                fails[nf++] = peer.id;
                reset_peer(peer);
                continue;
            }
            if (config_.ping_interval_ms && t - peer.last_ping_ms >= config_.ping_interval_ms) {
                peer.last_ping_ms = t;
                pings[np++] = peer.id;
            }
        } else {
            if (timeout && t > peer.last_inbound_ms && t - peer.last_inbound_ms > timeout) {
                expired[ne++] = peer.id;  // the remote stopped asking; forget it
                reset_peer(peer);
                continue;
            }
            if (!peer.request_posted ||
                (config_.request_repost_ms && t - peer.request_ms >= config_.request_repost_ms)) {
                peer.request_posted = true;
                peer.request_ms = t;
                requests[nr++] = peer.id;
            }
        }
    }
    // Packets that stay unaccepted for longer than the timeout are dropped.
    for (unsigned i = 0; i < queue_count_;) {
        const Packet& pkt = queue_[(queue_head_ + i) % kMaxQueued];
        const Peer* owner = find_peer(pkt.sender);
        if ((!owner || !owner->accepted) && timeout && t > pkt.queued_ms && t - pkt.queued_ms > timeout)
            remove_packet(i);
        else
            ++i;
    }
    ReleaseSRWLockExclusive(&lock_);
    for (unsigned i = 0; i < np; ++i) send_control(DatagramType::Ping, pings[i], /*droppable=*/true);
    for (unsigned i = 0; i < nr; ++i) post_request(requests[i]);
    for (unsigned i = 0; i < nf; ++i) post_fail(fails[i]);
    for (unsigned i = 0; i < ne; ++i) log_peer("request_expired", expired[i]);
}

// ---------------------------------------------------------------------------
// Process-wide integration: a single emulator wired to a UDP socket, whose
// synthetic events flow into the callback module.
// ---------------------------------------------------------------------------
namespace {
constexpr long kTraceBudget = 512;  // bounded packet trace (both directions)
constexpr std::uint64_t kStatsIntervalMs = 10000;

EmulatorConfig g_config{};
LoopbackOptions g_options{};
volatile long g_configured = 0;
LoopbackCore g_core;
UdpTransport g_transport;
EmulatorObject g_object{};
INIT_ONCE g_start_once = INIT_ONCE_STATIC_INIT;
volatile long g_started = 0;
volatile long g_trace_budget = 0;
// Receive-thread-only stats bookkeeping.
std::uint64_t g_last_stats_ms = 0;
TransportStats g_last_stats{};

const char* type_name(DatagramType type) noexcept {
    switch (type) {
    case DatagramType::Data: return "data";
    case DatagramType::Hello: return "hello";
    case DatagramType::Ping: return "ping";
    case DatagramType::Close: return "close";
    case DatagramType::Fragment: return "fragment";
    }
    return "unknown";
}
const char* transport_error_name(TransportError error) noexcept {
    switch (error) {
    case TransportError::none: return "none";
    case TransportError::bad_config: return "bad_config";
    case TransportError::winsock: return "winsock";
    case TransportError::socket: return "socket";
    case TransportError::bind: return "bind";
    case TransportError::thread: return "thread";
    }
    return "unknown";
}

void global_log(const char* line) noexcept {
    if (g_config.log) g_config.log(line);
}

void trace(const char* direction, const DatagramHeader& header, std::size_t size) noexcept {
    if (!g_options.trace_packets || !g_config.log) return;
    if (InterlockedDecrement(&g_trace_budget) < 0) return;
    char line[224]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "loopback=trace; dir=%s; type=%s; mode=%u; channel=%d; len=%llu; src=%llu; dst=%llu\r\n",
            direction, type_name(header.type), static_cast<unsigned>(header.send_mode), header.channel,
            static_cast<unsigned long long>(size), static_cast<unsigned long long>(header.src),
            static_cast<unsigned long long>(header.dst))))
        global_log(line);
}

bool wire_send(void*, const DatagramHeader& header, const WireChunk* chunks, unsigned count, bool droppable) noexcept {
    if (count > 3 || (count && !chunks)) return false;
    WireChunk all[4]{};
    std::uint8_t head[kHeaderSize];
    std::size_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        all[i + 1] = chunks[i];
        total += chunks[i].size;
    }
    DatagramHeader local = header;
    local.length = static_cast<std::uint32_t>(total);
    encode_header(local, head);
    all[0] = {head, kHeaderSize};
    trace("tx", local, total);
    return g_transport.transmit(all, count + 1, droppable);
}
bool sink_request(void*, std::uint64_t remote) noexcept { return callbacks::post_session_request(remote); }
bool sink_fail(void*, std::uint64_t remote, std::uint8_t error) noexcept {
    return callbacks::post_connect_fail(remote, error);
}
void transport_datagram(void*, const std::uint8_t* data, std::size_t size, const Endpoint&) noexcept {
    DatagramHeader header{};
    const std::uint8_t* payload = nullptr;
    std::size_t payload_size = 0;
    if (decode_datagram(data, size, header, payload, payload_size) != DecodeStatus::ok) return;
    trace("rx", header, payload_size);
    g_core.on_datagram(header, payload, payload_size);
}
void transport_idle(void*) noexcept {
    g_core.tick();
    const std::uint64_t now = GetTickCount64();
    if (now - g_last_stats_ms < kStatsIntervalMs) return;
    g_last_stats_ms = now;
    const TransportStats stats = g_transport.stats();
    if (stats.tx == g_last_stats.tx && stats.rx == g_last_stats.rx && stats.dropped == g_last_stats.dropped) return;
    g_last_stats = stats;
    char line[160]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "loopback=stats; tx=%llu; rx=%llu; dropped=%llu\r\n",
                                   static_cast<unsigned long long>(stats.tx), static_cast<unsigned long long>(stats.rx),
                                   static_cast<unsigned long long>(stats.dropped))))
        global_log(line);
}

BOOL CALLBACK start_once(PINIT_ONCE, PVOID, PVOID*) noexcept {
    LoopbackWire wire{&wire_send, nullptr};
    SyntheticSink sink{&sink_request, &sink_fail, nullptr};
    g_core.start(g_config, wire, sink, LoopbackClock{});  // real GetTickCount64
    g_object.vtable = emulated_networking_vtable();
    g_object.core = &g_core;
    TransportConfig tc{};
    tc.listen_port = g_options.listen_port;
    tc.peer_port = g_config.peer_port;
    tc.idle_interval_ms = 250;
    tc.latency_ms = g_options.latency_ms;
    tc.loss_permille = g_options.loss_permille;
    DatagramSink ds{&transport_datagram, &transport_idle, nullptr};
    g_last_stats_ms = GetTickCount64();
    g_last_stats = TransportStats{};
    char line[192]{};
    if (g_transport.start(tc, ds)) {
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "loopback=listening; port=%u; peer_port=%u; self=%llu; latency_ms=%u; loss_permille=%u\r\n",
                static_cast<unsigned>(tc.listen_port), static_cast<unsigned>(tc.peer_port),
                static_cast<unsigned long long>(g_config.self_id), tc.latency_ms, tc.loss_permille)))
            global_log(line);
        InterlockedExchange(&g_started, 1);
        g_core.announce(/*reply=*/true);
    } else {
        g_core.shutdown();
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "loopback=disabled; reason=transport_%s; port=%u; networking=dead_object\r\n",
                transport_error_name(g_transport.error()), static_cast<unsigned>(tc.listen_port))))
            global_log(line);
    }
    return TRUE;
}
}  // namespace

void loopback_configure(const EmulatorConfig& config, const LoopbackOptions& options) noexcept {
    g_config = config;
    g_options = options;
    InterlockedExchange(&g_trace_budget, options.trace_packets ? kTraceBudget : 0);
    const bool ok = options.listen_port != 0 && config.peer_port != 0 && options.listen_port != config.peer_port &&
                    steam::is_individual_steamid(config.self_id);
    InterlockedExchange(&g_configured, ok ? 1 : 0);
}

bool loopback_ensure_started() noexcept {
    if (!g_configured) return false;
    InitOnceExecuteOnce(&g_start_once, start_once, nullptr, nullptr);
    return g_started != 0;
}

void* loopback_networking_object() noexcept {
    return loopback_ensure_started() ? static_cast<void*>(&g_object) : dead_networking_object();
}

void* MGSCOOP_STEAM_CALL loopback_networking_gate() noexcept {
    const DWORD error = GetLastError();
    void* object = loopback_networking_object();
    SetLastError(error);
    return object;
}

void loopback_reset_for_test() noexcept {
    g_transport.stop();
    g_core.shutdown();
    InterlockedExchange(&g_configured, 0);
    InterlockedExchange(&g_started, 0);
    g_config = EmulatorConfig{};
    g_options = LoopbackOptions{};
    InitOnceInitialize(&g_start_once);
}

}  // namespace mgscoop::lab
#endif  // _WIN32
