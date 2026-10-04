#pragma once
// ISteamNetworking005 loopback emulator for the co-op lab (lab-only; inert
// unless lab.ini enables loopback). Emulates the P2P slots the game uses
// (Send[0], IsP2PPacketAvailable[1], Read[2], Accept[3], Close[4],
// CloseChannel[5], GetP2PSessionState[6]); slots 7..21 return 0/false.
//
// Steam P2P semantics reproduced (isteamnetworking.h, SteamNetworking005):
//   - Send implicitly opens/accepts the session toward that peer.
//   - Only DATA/FRAGMENT from a peer without an accepted session creates a
//     pending session: the packet is queued and a synthetic 1202 posted. While
//     the session stays pending, 1202 is re-posted every `request_repost_ms`
//     ("another P2PSessionRequest_t will be posted periodically"). A pending
//     session with no inbound traffic for `timeout_ms` expires silently and its
//     packets are dropped, so a later packet asks again.
//   - HELLO and PING never create a session, post 1202 or arm a timer; they
//     only refresh the liveness of an existing session (and HELLO carries the
//     sender's persona name).
//   - Inbound packets stay invisible to IsP2PPacketAvailable/Read until the
//     session is accepted, so the poll loop at 0x1A5FC50 can never spin on an
//     unreadable packet. Packets are filtered by channel.
//   - Read truncates to the caller buffer and returns the sender id.
//   - Unreliable sends cap at 1200 B, reliable at 1 MiB (fragmented on the wire
//     and scatter-sent, with no large stack buffers).
//   - GetP2PSessionState reports active (or connecting for a pending session),
//     IP 127.0.0.1 and the peer port.
//   - Liveness: every accepted session sends PING every `ping_interval_ms`; an
//     accepted session with no inbound datagram of any kind for `timeout_ms`
//     (local sends do not count) is reset and a synthetic 1203 (error 4) posted.
//     A remote CLOSE also resets the session and posts 1203. Close/CloseChannel
//     reset the session locally and send CLOSE. Every reset frees the peer slot,
//     drops reassembly state and purges that peer's queued packets, so the
//     session can be re-established by a later Send or inbound DATA.
//
// The core is injected with a wire, a clock and a synthetic-event sink, so two
// emulators can be wired together in one test process with no sockets. The
// sink is always called without the core lock held.
#include <cstddef>
#include <cstdint>

#include "mgscoop/steam_abi.hpp"
#include "mgscoop/udp_transport.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace mgscoop::lab {

// Sends one datagram (header + up to 3 payload chunks) on behalf of the
// emulator. `droppable` is false for HELLO/CLOSE (loss injection skips them).
// Returns false if the datagram could not be sent.
struct LoopbackWire {
    bool (*send)(void* ctx, const DatagramHeader& header, const WireChunk* chunks, unsigned count,
                 bool droppable) noexcept = nullptr;
    void* ctx = nullptr;
};

// Where synthetic 1202/1203 events go (enqueue only; never calls game code).
// Returning false means the event was not queued; a 1202 is then retried.
struct SyntheticSink {
    bool (*post_request)(void* ctx, std::uint64_t remote) noexcept = nullptr;
    bool (*post_fail)(void* ctx, std::uint64_t remote, std::uint8_t error) noexcept = nullptr;
    void* ctx = nullptr;
};

// Monotonic millisecond clock (injectable so tests are deterministic).
struct LoopbackClock {
    std::uint64_t (*now_ms)(void* ctx) noexcept = nullptr;
    void* ctx = nullptr;
};

struct EmulatorConfig {
    std::uint64_t self_id = 0;
    std::uint64_t peer_id = 0;  // 0 = learn from the first valid inbound datagram
    std::uint16_t peer_port = 0;
    std::uint32_t timeout_ms = 10000;
    std::uint32_t ping_interval_ms = 1000;
    std::uint32_t request_repost_ms = 3000;
    char self_name[64]{};
    void (*log)(const char* line) noexcept = nullptr;
    // Called (without the core lock) when the peer id is learned and on every
    // HELLO with its persona name (empty when not carried).
    void (*on_peer)(std::uint64_t id, const char* name) noexcept = nullptr;
};

#ifdef _WIN32
class LoopbackCore {
public:
    void start(const EmulatorConfig& config, const LoopbackWire& wire, const SyntheticSink& sink,
               const LoopbackClock& clock) noexcept;
    void shutdown() noexcept;

    // Emulated ISteamNetworking005 operations (game-ABI argument shapes).
    bool send_p2p(std::uint64_t remote, const void* data, std::uint32_t len,
                  std::int32_t mode, std::int32_t channel) noexcept;
    bool is_available(std::uint32_t* size, std::int32_t channel) noexcept;
    bool read_p2p(void* dest, std::uint32_t cap, std::uint32_t* size_out,
                  std::uint64_t* remote_out, std::int32_t channel) noexcept;
    bool accept(std::uint64_t remote) noexcept;
    bool close(std::uint64_t remote) noexcept;
    bool close_channel(std::uint64_t remote, std::int32_t channel) noexcept;
    bool session_state(std::uint64_t remote, steam::P2PSessionState_t* out) noexcept;

    // Send a HELLO announcing presence; `reply` asks the peer to answer.
    void announce(bool reply) noexcept;

    // Feed one already-decoded, endpoint-filtered datagram (receive thread).
    void on_datagram(const DatagramHeader& header, const std::uint8_t* payload,
                     std::size_t payload_size) noexcept;

    // Advance timers: keepalive PINGs, 1202 re-posts, pending-session expiry and
    // the accepted-session liveness timeout (1203).
    void tick() noexcept;

    std::uint64_t learned_peer() noexcept;
    // Diagnostics for tests: number of used peer slots / queued packets.
    unsigned peer_count() noexcept;
    unsigned queued_count() noexcept;

private:
    static constexpr unsigned kMaxPeers = 4;
    static constexpr unsigned kMaxQueued = 256;
    static constexpr unsigned kMaxFragChunks = 32;
    static constexpr unsigned kMaxChannels = 8;

    struct Peer {
        bool used = false;
        bool accepted = false;        // visible to the game (Send or Accept)
        bool request_posted = false;  // a 1202 was queued at request_ms
        bool channel_overflow = false;
        std::uint64_t id = 0;
        std::uint64_t open_ms = 0;          // when accepted locally
        std::uint64_t last_inbound_ms = 0;  // any datagram from the peer
        std::uint64_t request_ms = 0;
        std::uint64_t last_ping_ms = 0;
        std::int32_t channels[kMaxChannels]{};
        unsigned channel_count = 0;
        // One in-flight reliable reassembly.
        std::uint32_t frag_msg = 0;
        std::uint32_t frag_total = 0;
        std::uint32_t frag_mask = 0;
        std::int32_t frag_channel = 0;
        std::uint8_t* frag_buffer = nullptr;
    };
    struct Packet {
        std::uint64_t sender = 0;
        std::uint8_t* bytes = nullptr;
        std::uint32_t size = 0;
        std::int32_t channel = 0;
        std::uint64_t queued_ms = 0;
    };
    enum class Event : std::uint8_t { none, request, fail };

    Peer* find_peer(std::uint64_t id) noexcept;
    Peer* ensure_peer(std::uint64_t id) noexcept;
    void reset_peer(Peer& peer) noexcept;
    void drop_reassembly(Peer& peer) noexcept;
    void note_channel(Peer& peer, std::int32_t channel) noexcept;
    bool forget_channel(Peer& peer, std::int32_t channel) noexcept;
    void remove_packet(unsigned index) noexcept;
    void purge(std::uint64_t sender, bool any_channel, std::int32_t channel) noexcept;
    bool enqueue(std::uint64_t sender, const std::uint8_t* data, std::uint32_t size, std::int32_t channel,
                 std::uint64_t now) noexcept;
    void deliver_reliable(Peer& peer, const std::uint8_t* payload, std::size_t size, std::int32_t channel,
                          std::uint64_t now) noexcept;
    bool send_fragmented(std::uint64_t remote, const std::uint8_t* bytes, std::uint32_t len, std::int32_t mode,
                         std::int32_t channel) noexcept;
    bool send_control(DatagramType type, std::uint64_t remote, bool droppable) noexcept;
    void post_request(std::uint64_t remote) noexcept;
    void post_fail(std::uint64_t remote) noexcept;
    std::uint64_t now() noexcept;
    void log_line(const char* line) noexcept;
    void log_peer(const char* event, std::uint64_t peer) noexcept;

    EmulatorConfig config_{};
    LoopbackWire wire_{};
    SyntheticSink sink_{};
    LoopbackClock clock_{};
    std::uint64_t peer_id_ = 0;
    volatile long msg_seq_ = 0;  // reliable message ids (random per-process start)

    SRWLOCK lock_ = SRWLOCK_INIT;
    Peer peers_[kMaxPeers]{};
    Packet queue_[kMaxQueued]{};
    unsigned queue_head_ = 0;
    unsigned queue_count_ = 0;
    unsigned rejected_logged_ = 0;
    volatile long started_ = 0;
};

// Interface object the SteamNetworking getter hands to the game. Its first qword
// is the emulated 22-slot vtable; the second qword recovers the core.
struct EmulatorObject {
    const void** vtable;
    LoopbackCore* core;
};

// Returns the shared 22-slot emulated vtable (process-lifetime static).
const void** emulated_networking_vtable() noexcept;
// A process-lifetime "no networking" object: every slot returns 0/false. It is
// what the lab hands the game whenever the emulator is not running, instead of
// null (Send 0x1A5FB60 and Read 0x1A5FAC0 dereference the getter result without
// a null check) and instead of the real interface (fail closed).
void* dead_networking_object() noexcept;

// ---- Process-wide loopback integration (wraps the SteamNetworking getter) ----
struct LoopbackOptions {
    std::uint16_t listen_port = 0;
    std::uint32_t latency_ms = 0;
    std::uint32_t loss_permille = 0;
    bool trace_packets = false;
};
// Configure once from steam_observer; inert until started.
void loopback_configure(const EmulatorConfig& config, const LoopbackOptions& options) noexcept;
// Lazily start the socket transport and emulator (outside DllMain). Idempotent.
bool loopback_ensure_started() noexcept;
// What the lab's SteamNetworking getter returns: the emulator object once the
// transport is running, otherwise dead_networking_object(). Never null and
// never the real interface.
void* loopback_networking_object() noexcept;
// The function the lab installs in the SteamNetworking IAT slot (0x208E138):
// preserves the caller's last error and returns loopback_networking_object().
void* MGSCOOP_STEAM_CALL loopback_networking_gate() noexcept;
// Stops the transport and core and forgets the configuration (tests only).
void loopback_reset_for_test() noexcept;
#endif  // _WIN32

}  // namespace mgscoop::lab
