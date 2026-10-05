#pragma once
// Loopback UDP wire format and transport for the co-op lab's ISteamNetworking
// emulator (lab-only; inert unless lab.ini enables loopback).
//
// Every datagram starts with a fixed 32-byte little-endian header:
//   'MGLB' | u16 version | u8 type | u8 send mode | i32 channel | u64 src | u64 dst | u32 length
// followed by exactly `length` payload bytes. The codec is pure so it can be
// unit tested; the socket transport is Windows-only.
#include <cstddef>
#include <cstdint>

namespace mgscoop::lab {

// DATA carries a P2P payload; HELLO announces presence (payload: persona name);
// PING keeps an active session alive; CLOSE ends the session; FRAGMENT carries
// one piece of a reliable message larger than one datagram.
enum class DatagramType : std::uint8_t { Data = 0, Hello = 1, Ping = 2, Close = 3, Fragment = 4 };

constexpr std::uint8_t kWireMagic[4] = {'M', 'G', 'L', 'B'};
constexpr std::uint16_t kWireVersion = 1;
constexpr std::size_t kHeaderSize = 32;
constexpr std::size_t kMaxDatagram = 65507;                       // IPv4 UDP payload limit
constexpr std::size_t kMaxDataPayload = kMaxDatagram - kHeaderSize;  // single DATA datagram
constexpr std::size_t kFragmentHeaderSize = 12;                   // u32 message id | u32 total | u32 offset
constexpr std::size_t kFragmentChunk = 60000;
constexpr std::size_t kMaxHelloName = 63;
// HELLO send-mode byte: 1 asks the receiver to answer with its own HELLO.
constexpr std::uint8_t kHelloReplyRequested = 1;

struct DatagramHeader {
    DatagramType type = DatagramType::Data;
    std::uint8_t send_mode = 0;
    std::int32_t channel = 0;
    std::uint64_t src = 0;
    std::uint64_t dst = 0;
    std::uint32_t length = 0;
};

enum class DecodeStatus : std::uint8_t {
    ok, too_short, bad_magic, bad_version, bad_type, length_mismatch, bad_mode, bad_source, bad_control_length
};

// Serialize `header` into out[0..kHeaderSize). Returns kHeaderSize.
std::size_t encode_header(const DatagramHeader& header, std::uint8_t* out) noexcept;

// Strict parse: magic, version, known type, length equal to the remaining bytes
// (UDP keeps boundaries, so trailing bytes are rejected), DATA send mode 0..3,
// individual-account source id, and control datagrams within their limits
// (PING/CLOSE empty, HELLO <= kMaxHelloName, FRAGMENT > fragment header).
DecodeStatus decode_datagram(const std::uint8_t* data, std::size_t size, DatagramHeader& out,
                             const std::uint8_t*& payload, std::size_t& payload_size) noexcept;
const char* decode_status_name(DecodeStatus status) noexcept;

// Deterministic loss schedule: drop when floor((n+1)*p/1000) != floor(n*p/1000).
// Over any N consecutive counters the drop count is within 1 of N*p/1000.
bool should_drop(std::uint32_t loss_permille, std::uint64_t counter) noexcept;

// Endpoint filter used by the receive thread: only 127.0.0.1:<peer_port>.
// Address and port are in host byte order.
bool is_expected_peer(std::uint32_t address, std::uint16_t port, std::uint16_t peer_port) noexcept;

struct WireChunk {
    const void* data;
    std::size_t size;
};

// An IPv4 endpoint, both fields in host byte order.
struct Endpoint {
    std::uint32_t address;
    std::uint16_t port;
};
inline bool SameEndpoint(const Endpoint& a, const Endpoint& b) noexcept { return a.address == b.address && a.port == b.port; }
inline std::uint64_t PackEndpoint(Endpoint e) noexcept { return (static_cast<std::uint64_t>(e.address) << 16) | e.port; }

// Outbound fault injection (lab only; play refuses every nonzero key).
// latency_ms delays every datagram; jitter_ms adds a uniform [0, jitter_ms]
// delay; reorder_permille adds reorder_ms to that share; dup_permille queues a
// second copy 1 ms later; loss drops droppable datagrams on the deterministic
// should_drop() schedule (burst_model false) or a Gilbert-Elliott chain with
// mean loss loss_permille and mean burst length `burst` (burst_model true).
// Random draws come from xorshift64* seeded with seed ^ 0x9E3779B97F4A7C15.
struct Impairment {
    std::uint32_t latency_ms = 0, jitter_ms = 0, loss_permille = 0, burst = 2, dup_permille = 0, reorder_permille = 0,
                  reorder_ms = 40, seed = 1;
    bool burst_model = false;
};
// True when datagrams go through the delay heap (any delay-type impairment).
inline bool ImpairmentDelays(const Impairment& i) noexcept {
    return i.latency_ms || i.jitter_ms || i.dup_permille || i.reorder_permille;
}
inline bool ImpairmentAllZero(const Impairment& i) noexcept {
    return !i.latency_ms && !i.jitter_ms && !i.loss_permille && !i.dup_permille && !i.reorder_permille && !i.burst_model;
}

// xorshift64* (the impairment RNG).
struct ImpairRng {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;
    void Seed(std::uint32_t seed) noexcept {
        state = static_cast<std::uint64_t>(seed) ^ 0x9E3779B97F4A7C15ull;
        if (!state) state = 1;
    }
    std::uint64_t Next() noexcept {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1Dull;
    }
    // Uniform in [0, 1).
    double Unit() noexcept { return static_cast<double>(Next() >> 11) * (1.0 / 9007199254740992.0); }
};

// Gilbert-Elliott two-state loss chain: good->bad with p = L / (B (1 - L)),
// bad->good with 1 / B; every datagram is dropped while bad; L >= 1 drops
// everything. Returns true when this datagram is dropped.
struct BurstLoss {
    bool bad = false;
    bool Step(std::uint32_t loss_permille, std::uint32_t burst, ImpairRng& rng) noexcept;
};

#ifdef _WIN32
// Receives every datagram that passed the endpoint filter (still undecoded)
// with its source on the transport's own thread. Must only enqueue; never
// call game code.
struct DatagramSink {
    void (*on_datagram)(void* ctx, const std::uint8_t* data, std::size_t size, const Endpoint& from) noexcept = nullptr;
    // Called from the receive thread whenever `idle_interval_ms` has elapsed
    // (deadline-driven: also between drain passes under load).
    void (*on_idle)(void* ctx) noexcept = nullptr;
    void* ctx = nullptr;
};

struct TransportConfig {
    std::uint16_t listen_port = 0;
    std::uint16_t peer_port = 0;
    std::uint32_t idle_interval_ms = 250;
    // lab.ini fault injection, applied on the send path. latency_ms delays every
    // outbound datagram (order preserved); loss_permille drops only datagrams
    // the caller marks droppable, on the deterministic should_drop() schedule.
    // start() copies both into `impair` when `impair` is all zero.
    std::uint32_t latency_ms = 0;
    std::uint32_t loss_permille = 0;
    // Endpoints (host byte order). The defaults reproduce the lab: bind and
    // send to 127.0.0.1, exact source filter.
    std::uint32_t bind_address = 0x7F000001;  // 0 = INADDR_ANY
    std::uint32_t peer_address = 0x7F000001;
    // false: only datagrams from peer_address:peer_port reach the sink;
    // true: every datagram reaches the sink with its source (peer_port may be 0).
    bool any_source = false;
    // listen_port 0: an ephemeral port (lab.ini allows it only for a guest
    // off loopback; the tests use it on 127.0.0.1).
    bool allow_ephemeral = false;
    Impairment impair{};
};

enum class TransportError : std::uint8_t { none, bad_config, winsock, socket, bind, thread };

struct TransportStats {
    std::uint64_t tx = 0;          // datagrams handed to the socket
    std::uint64_t rx = 0;          // datagrams accepted from the peer endpoint
    std::uint64_t dropped = 0;     // loss-injected, delay-queue overflow or send failure
    std::uint64_t rejected = 0;    // datagrams from any other endpoint
    std::uint64_t duplicated = 0;  // extra copies queued by dup_permille
};

// One UDP socket bound to bind_address:listen_port. transmit() sends to
// peer_address:peer_port, transmit_to() anywhere. Constant-initializable and
// trivially destructible (no unload destructor). Without impairment nothing
// allocates per datagram; the delay heap (lab impairment only) copies each
// delayed datagram once.
class UdpTransport {
public:
    static constexpr unsigned kMaxDelayed = 256;
    // Datagrams drained per receive pass before the due timers run again.
    static constexpr unsigned kDrainBatch = 64;
    // Longest receive-thread sleep while the delay heap is in use.
    static constexpr std::uint32_t kDelayPollMs = 2;

    // Starts Winsock lazily (never call from DllMain), binds, disables
    // SIO_UDP_CONNRESET and SIO_UDP_NETRESET, sets 1 MiB buffers and starts
    // the receive thread.
    bool start(const TransportConfig& config, const DatagramSink& sink) noexcept;
    // Scatter-send one datagram (at most 4 chunks) to the peer endpoint.
    // Thread-safe. A loss-injected datagram counts as dropped and returns true
    // (it was "sent" and lost on the wire); a delayed one returns true once queued.
    bool transmit(const WireChunk* chunks, unsigned count, bool droppable = true) noexcept;
    // The same to an explicit endpoint (delayed datagrams keep their endpoint).
    bool transmit_to(Endpoint to, const WireChunk* chunks, unsigned count, bool droppable = true) noexcept;
    // Test helper: send raw bytes from this socket to an arbitrary loopback port.
    bool transmit_raw_to(std::uint16_t port, const void* data, std::size_t size) noexcept;
    void stop() noexcept;
    bool running() const noexcept { return running_ != 0; }
    TransportError error() const noexcept { return error_; }
    std::uint64_t rejected_endpoint() const noexcept { return static_cast<std::uint64_t>(rejected_endpoint_); }
    // The bound port (getsockname; the ephemeral one with listen_port 0).
    std::uint16_t local_port() const noexcept { return local_port_; }
    TransportStats stats() const noexcept;

private:
    struct Delayed {
        std::uint64_t due_ms;
        std::uint64_t order;  // insertion counter: equal due times keep FIFO order
        Endpoint to;
        std::uint8_t* bytes;
        std::uint32_t size;
    };
    static unsigned long __stdcall thread_entry(void* self) noexcept;
    void receive_loop() noexcept;
    bool send_now(Endpoint to, const void* data, std::size_t size) noexcept;
    bool send_now_chunks(Endpoint to, const WireChunk* chunks, unsigned count, std::size_t total) noexcept;
    // Heap push / pop of the delay queue (caller holds delay_lock_).
    bool push_delayed(const Delayed& item) noexcept;
    Delayed pop_delayed() noexcept;
    // Sends every delayed datagram that is due; returns ms until the next one
    // (or `fallback` when none is queued).
    std::uint32_t flush_delayed(std::uint32_t fallback) noexcept;
    void clear_delayed() noexcept;

    TransportConfig config_{};
    DatagramSink sink_{};
    std::uintptr_t socket_ = ~std::uintptr_t{0};
    void* thread_ = nullptr;
    volatile long running_ = 0;
    volatile long long rejected_endpoint_ = 0;
    volatile long long tx_ = 0;
    volatile long long rx_ = 0;
    volatile long long dropped_ = 0;
    volatile long long duplicated_ = 0;
    volatile long long loss_counter_ = 0;
    TransportError error_ = TransportError::none;
    std::uint16_t local_port_ = 0;
    // Delay heap (binary min-heap on (due_ms, order)), the burst-loss chain
    // and the RNG, all under delay_lock_.
    void* delay_lock_ = nullptr;  // SRWLOCK storage (pointer-sized, zero = SRWLOCK_INIT)
    Delayed delayed_[kMaxDelayed]{};
    unsigned delayed_count_ = 0;
    std::uint64_t delayed_order_ = 0;
    ImpairRng rng_{};
    BurstLoss burst_{};
};
#endif

}  // namespace mgscoop::lab
