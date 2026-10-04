// Original co-op lab loopback transport: wire codec and Winsock endpoint.
#include "mgscoop/udp_transport.hpp"

#include <cstring>

namespace mgscoop::lab {
namespace {
void put_u16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void put_u32(std::uint8_t* p, std::uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void put_u64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint16_t get_u16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}
std::uint32_t get_u32(const std::uint8_t* p) noexcept {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    return v;
}
std::uint64_t get_u64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    return v;
}
bool individual(std::uint64_t id) noexcept { return (id >> 32) == 0x01100001u && (id & 0xffffffffu) != 0; }
}  // namespace

std::size_t encode_header(const DatagramHeader& header, std::uint8_t* out) noexcept {
    std::memcpy(out, kWireMagic, 4);
    put_u16(out + 4, kWireVersion);
    out[6] = static_cast<std::uint8_t>(header.type);
    out[7] = header.send_mode;
    put_u32(out + 8, static_cast<std::uint32_t>(header.channel));
    put_u64(out + 12, header.src);
    put_u64(out + 20, header.dst);
    put_u32(out + 28, header.length);
    return kHeaderSize;
}

DecodeStatus decode_datagram(const std::uint8_t* data, std::size_t size, DatagramHeader& out,
                             const std::uint8_t*& payload, std::size_t& payload_size) noexcept {
    payload = nullptr;
    payload_size = 0;
    if (!data || size < kHeaderSize) return DecodeStatus::too_short;
    if (std::memcmp(data, kWireMagic, 4) != 0) return DecodeStatus::bad_magic;
    if (get_u16(data + 4) != kWireVersion) return DecodeStatus::bad_version;
    const std::uint8_t type = data[6];
    if (type > static_cast<std::uint8_t>(DatagramType::Fragment)) return DecodeStatus::bad_type;
    DatagramHeader header{};
    header.type = static_cast<DatagramType>(type);
    header.send_mode = data[7];
    header.channel = static_cast<std::int32_t>(get_u32(data + 8));
    header.src = get_u64(data + 12);
    header.dst = get_u64(data + 20);
    header.length = get_u32(data + 28);
    if (header.length != size - kHeaderSize) return DecodeStatus::length_mismatch;
    if (!individual(header.src)) return DecodeStatus::bad_source;
    switch (header.type) {
    case DatagramType::Data:
        if (header.send_mode > 3) return DecodeStatus::bad_mode;
        break;
    case DatagramType::Fragment:
        if (header.send_mode != 2 && header.send_mode != 3) return DecodeStatus::bad_mode;
        if (header.length <= kFragmentHeaderSize) return DecodeStatus::bad_control_length;
        break;
    case DatagramType::Hello:
        if (header.length > kMaxHelloName) return DecodeStatus::bad_control_length;
        break;
    case DatagramType::Ping:
    case DatagramType::Close:
        if (header.length != 0) return DecodeStatus::bad_control_length;
        break;
    }
    out = header;
    payload = data + kHeaderSize;
    payload_size = header.length;
    return DecodeStatus::ok;
}

const char* decode_status_name(DecodeStatus status) noexcept {
    switch (status) {
    case DecodeStatus::ok: return "ok";
    case DecodeStatus::too_short: return "too_short";
    case DecodeStatus::bad_magic: return "bad_magic";
    case DecodeStatus::bad_version: return "bad_version";
    case DecodeStatus::bad_type: return "bad_type";
    case DecodeStatus::length_mismatch: return "length_mismatch";
    case DecodeStatus::bad_mode: return "bad_mode";
    case DecodeStatus::bad_source: return "bad_source";
    case DecodeStatus::bad_control_length: return "bad_control_length";
    }
    return "unknown";
}

bool should_drop(std::uint32_t loss_permille, std::uint64_t counter) noexcept {
    if (loss_permille == 0) return false;
    if (loss_permille >= 1000) return true;
    const std::uint64_t before = (counter * loss_permille) / 1000;
    const std::uint64_t after = ((counter + 1) * loss_permille) / 1000;
    return after != before;
}

bool is_expected_peer(std::uint32_t address, std::uint16_t port, std::uint16_t peer_port) noexcept {
    return address == 0x7F000001u && peer_port != 0 && port == peer_port;
}

}  // namespace mgscoop::lab

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

namespace mgscoop::lab {
namespace {
INIT_ONCE winsock_once = INIT_ONCE_STATIC_INIT;
bool winsock_ready = false;
BOOL CALLBACK start_winsock(PINIT_ONCE, PVOID, PVOID*) noexcept {
    // Reference counted by Winsock; never cleaned up (no unload work).
    WSADATA data{};
    winsock_ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return TRUE;
}
}  // namespace

bool UdpTransport::start(const TransportConfig& config, const DatagramSink& sink) noexcept {
    if (running_) return true;
    config_ = config;
    sink_ = sink;
    error_ = TransportError::none;
    if (!config_.listen_port || !config_.peer_port || config_.listen_port == config_.peer_port) {
        error_ = TransportError::bad_config;
        return false;
    }
    InitOnceExecuteOnce(&winsock_once, start_winsock, nullptr, nullptr);
    if (!winsock_ready) { error_ = TransportError::winsock; return false; }
    const SOCKET s = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) { error_ = TransportError::socket; return false; }
    // An ICMP port-unreachable (peer not started yet) must not surface as a
    // receive error on this datagram socket.
    BOOL report_reset = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &report_reset, sizeof(report_reset), nullptr, 0, &returned, nullptr, nullptr);
    int buffer = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    // Non-blocking: a thread parked inside a blocking recvfrom on this socket
    // would stall a concurrent send from the game's poll thread on the same
    // socket. The receive thread waits for readability with select() instead.
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.listen_port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        closesocket(s);
        error_ = TransportError::bind;
        return false;
    }
    socket_ = static_cast<std::uintptr_t>(s);
    InterlockedExchange(&running_, 1);
    thread_ = CreateThread(nullptr, 0, &UdpTransport::thread_entry, this, 0, nullptr);
    if (!thread_) {
        InterlockedExchange(&running_, 0);
        closesocket(s);
        socket_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
        error_ = TransportError::thread;
        return false;
    }
    return true;
}

bool UdpTransport::transmit(const WireChunk* chunks, unsigned count) noexcept {
    if (!running_ || !chunks || !count || count > 4) return false;
    WSABUF buffers[4]{};
    std::size_t total = 0;
    unsigned used = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!chunks[i].size) continue;
        buffers[used].buf = const_cast<CHAR*>(static_cast<const CHAR*>(chunks[i].data));
        buffers[used].len = static_cast<ULONG>(chunks[i].size);
        total += chunks[i].size;
        ++used;
    }
    if (!used || total > kMaxDatagram) return false;
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(config_.peer_port);
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    DWORD sent = 0;
    const int result = WSASendTo(static_cast<SOCKET>(socket_), buffers, used, &sent, 0,
                                 reinterpret_cast<const sockaddr*>(&peer), sizeof(peer), nullptr, nullptr);
    return result == 0 && sent == total;
}

bool UdpTransport::transmit_raw_to(std::uint16_t port, const void* data, std::size_t size) noexcept {
    if (!running_ || !data || !size || size > kMaxDatagram) return false;
    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return sendto(static_cast<SOCKET>(socket_), static_cast<const char*>(data), static_cast<int>(size), 0,
                  reinterpret_cast<const sockaddr*>(&target), sizeof(target)) == static_cast<int>(size);
}

unsigned long __stdcall UdpTransport::thread_entry(void* self) noexcept {
    static_cast<UdpTransport*>(self)->receive_loop();
    return 0;
}

void UdpTransport::receive_loop() noexcept {
    constexpr std::size_t capacity = kMaxDatagram + 1;
    auto* buffer = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, capacity));
    if (!buffer) return;
    const long interval_us = static_cast<long>((config_.idle_interval_ms ? config_.idle_interval_ms : 250) * 1000);
    ULONGLONG last_idle = GetTickCount64();
    const SOCKET s = static_cast<SOCKET>(socket_);
    while (running_) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(s, &readable);
        timeval tv{0, interval_us};
        const int ready = select(0, &readable, nullptr, nullptr, &tv);
        if (!running_) break;
        if (ready > 0) {
            // Drain everything currently queued before waiting again.
            for (;;) {
                sockaddr_in from{};
                int from_size = sizeof(from);
                const int received = recvfrom(s, reinterpret_cast<char*>(buffer), static_cast<int>(capacity), 0,
                                              reinterpret_cast<sockaddr*>(&from), &from_size);
                if (received <= 0) {
                    if (received == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
                        const int error = WSAGetLastError();
                        if (error == WSAENOTSOCK || error == WSAESHUTDOWN) { running_ = 0; }
                    }
                    break;
                }
                if (!is_expected_peer(ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), config_.peer_port)) {
                    InterlockedIncrement64(&rejected_endpoint_);
                } else if (sink_.on_datagram) {
                    sink_.on_datagram(sink_.ctx, buffer, static_cast<std::size_t>(received));
                }
                if (!running_) break;
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (now - last_idle >= static_cast<ULONGLONG>(interval_us / 1000)) {
            last_idle = now;
            if (sink_.on_idle) sink_.on_idle(sink_.ctx);
        }
    }
    HeapFree(GetProcessHeap(), 0, buffer);
}

void UdpTransport::stop() noexcept {
    if (InterlockedExchange(&running_, 0) == 0) return;
    const SOCKET s = static_cast<SOCKET>(socket_);
    socket_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
    if (s != INVALID_SOCKET) closesocket(s);
    if (thread_) {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
}

}  // namespace mgscoop::lab
#endif  // _WIN32
