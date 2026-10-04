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

#ifdef _WIN32
// Receives every datagram that passed the endpoint filter (still undecoded) on
// the transport's own thread. Must only enqueue; never call game code.
struct DatagramSink {
    void (*on_datagram)(void* ctx, const std::uint8_t* data, std::size_t size) noexcept = nullptr;
    // Called roughly every `idle_interval_ms` from the receive thread.
    void (*on_idle)(void* ctx) noexcept = nullptr;
    void* ctx = nullptr;
};

struct TransportConfig {
    std::uint16_t listen_port = 0;
    std::uint16_t peer_port = 0;
    std::uint32_t idle_interval_ms = 250;
};

enum class TransportError : std::uint8_t { none, bad_config, winsock, socket, bind, thread };

// One UDP socket bound to 127.0.0.1:listen_port sending to 127.0.0.1:peer_port.
// Constant-initializable and trivially destructible (no unload destructor).
class UdpTransport {
public:
    // Starts Winsock lazily (never call from DllMain), binds, disables
    // SIO_UDP_CONNRESET, sets 1 MiB buffers and starts the receive thread.
    bool start(const TransportConfig& config, const DatagramSink& sink) noexcept;
    // Scatter-send one datagram to the peer endpoint. Thread-safe.
    bool transmit(const WireChunk* chunks, unsigned count) noexcept;
    // Test helper: send raw bytes from this socket to an arbitrary loopback port.
    bool transmit_raw_to(std::uint16_t port, const void* data, std::size_t size) noexcept;
    void stop() noexcept;
    bool running() const noexcept { return running_ != 0; }
    TransportError error() const noexcept { return error_; }
    std::uint64_t rejected_endpoint() const noexcept { return static_cast<std::uint64_t>(rejected_endpoint_); }

private:
    static unsigned long __stdcall thread_entry(void* self) noexcept;
    void receive_loop() noexcept;

    TransportConfig config_{};
    DatagramSink sink_{};
    std::uintptr_t socket_ = ~std::uintptr_t{0};
    void* thread_ = nullptr;
    volatile long running_ = 0;
    volatile long long rejected_endpoint_ = 0;
    TransportError error_ = TransportError::none;
};
#endif

}  // namespace mgscoop::lab
