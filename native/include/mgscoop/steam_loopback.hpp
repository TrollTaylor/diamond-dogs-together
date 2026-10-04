#pragma once
// ISteamNetworking005 loopback emulator for the co-op lab (lab-only; inert
// unless lab.ini enables loopback). Emulates the six P2P slots the game uses
// (Send[0], IsP2PPacketAvailable[1], Read[2], Accept[3], Close[4],
// GetP2PSessionState[6]); the other slots return 0/false.
//
// Steam P2P semantics reproduced:
//   - Send implicitly opens/accepts the session toward that peer.
//   - The first inbound datagram from a new, not-yet-accepted peer queues the
//     packet (if it carries data) and posts exactly one synthetic 1202.
//   - Inbound packets stay invisible to IsP2PPacketAvailable/Read until the
//     game Accepts, so the poll loop at 0x1A5FC50 can never spin on an
//     unreadable packet.
//   - Read truncates to the caller buffer and returns the sender id.
//   - Unreliable sends cap at 1200 B, reliable at 1 MiB.
//   - GetP2PSessionState reports active, IP 127.0.0.1 and the peer port.
//   - Close sends CLOSE; a remote CLOSE or `timeout_ms` of silence after
//     activity posts a synthetic 1203 with error 4.
//
// The core is injected with a wire, a clock and a synthetic-event sink, so two
// emulators can be wired together in one test process with no sockets.
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

// Sends one datagram on behalf of the emulator. Returns false if dropped/failed.
struct LoopbackWire {
    bool (*send)(void* ctx, const DatagramHeader& header, const void* payload, std::size_t size) noexcept = nullptr;
    void* ctx = nullptr;
};

// Where synthetic 1202/1203 events go (enqueue only; never calls game code).
struct SyntheticSink {
    void (*post_request)(void* ctx, std::uint64_t remote) noexcept = nullptr;
    void (*post_fail)(void* ctx, std::uint64_t remote, std::uint8_t error) noexcept = nullptr;
    void* ctx = nullptr;
};

// Monotonic millisecond clock (injectable so tests are deterministic).
struct LoopbackClock {
    std::uint64_t (*now_ms)(void* ctx) noexcept = nullptr;
    void* ctx = nullptr;
};

struct EmulatorConfig {
    std::uint64_t self_id = 0;
    std::uint64_t peer_id = 0;  // 0 = learn from the first inbound datagram
    std::uint16_t peer_port = 0;
    std::uint32_t timeout_ms = 10000;
    char self_name[64]{};
    void (*log)(const char* line) noexcept = nullptr;
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
    bool session_state(std::uint64_t remote, steam::P2PSessionState_t* out) noexcept;

    // Send a HELLO announcing presence; `reply` asks the peer to answer.
    void announce(bool reply) noexcept;

    // Feed one already-decoded, endpoint-filtered datagram (receive thread).
    void on_datagram(const DatagramHeader& header, const std::uint8_t* payload,
                     std::size_t payload_size) noexcept;

    // Advance timers; posts 1203 for any session idle past timeout_ms.
    void tick() noexcept;

    std::uint64_t learned_peer() const noexcept { return peer_id_; }

private:
    static constexpr unsigned kMaxPeers = 4;
    static constexpr unsigned kMaxQueued = 256;
    static constexpr unsigned kMaxFragChunks = 32;

    struct Peer {
        bool used = false;
        bool accepted = false;       // visible to the game (Send or Accept)
        bool request_posted = false;
        bool closed = false;
        bool had_activity = false;
        std::uint64_t id = 0;
        std::uint64_t last_activity_ms = 0;
        // One in-flight reliable reassembly.
        std::uint32_t frag_msg = 0;
        std::uint32_t frag_total = 0;
        std::uint32_t frag_mask = 0;
        std::uint8_t* frag_buffer = nullptr;
    };
    struct Packet {
        std::uint64_t sender = 0;
        std::uint8_t* bytes = nullptr;
        std::uint32_t size = 0;
    };

    Peer* find_peer(std::uint64_t id) noexcept;
    Peer* ensure_peer(std::uint64_t id, bool accept_now) noexcept;
    void enqueue(std::uint64_t sender, const std::uint8_t* data, std::uint32_t size) noexcept;
    void deliver_reliable(Peer& peer, const std::uint8_t* payload, std::size_t size) noexcept;
    void drop_front_visible() noexcept;
    std::uint64_t now() noexcept;
    void log_line(const char* line) noexcept;

    EmulatorConfig config_{};
    LoopbackWire wire_{};
    SyntheticSink sink_{};
    LoopbackClock clock_{};
    std::uint64_t peer_id_ = 0;

    SRWLOCK lock_ = SRWLOCK_INIT;
    Peer peers_[kMaxPeers]{};
    Packet queue_[kMaxQueued]{};
    unsigned queue_head_ = 0;
    unsigned queue_count_ = 0;
    bool started_ = false;
};

// Interface object the SteamNetworking getter hands to the game. Its first qword
// is the emulated 22-slot vtable; the second qword recovers the core.
struct EmulatorObject {
    const void** vtable;
    LoopbackCore* core;
};

// Returns the shared 22-slot emulated vtable (process-lifetime static).
const void** emulated_networking_vtable() noexcept;

// ---- Process-wide loopback integration (wraps the SteamNetworking getter) ----
// Configure once from early_patches/steam_observer; inert until started.
void loopback_configure(const EmulatorConfig& config, std::uint16_t listen_port) noexcept;
// Lazily start the socket transport and emulator (outside DllMain). Idempotent.
bool loopback_ensure_started() noexcept;
// The object pointer the getter wrapper returns, or null when not configured.
void* loopback_interface_object() noexcept;
void loopback_reset_for_test() noexcept;
#endif  // _WIN32

}  // namespace mgscoop::lab
