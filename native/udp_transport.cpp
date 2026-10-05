// Original co-op lab loopback transport and the link v2 UDP endpoint: wire
// codec, impairment and Winsock endpoint.
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

bool BurstLoss::Step(std::uint32_t loss_permille, std::uint32_t burst, ImpairRng& rng) noexcept {
    if (loss_permille == 0) {
        bad = false;
        return false;
    }
    if (loss_permille >= 1000) return true;
    const double loss = loss_permille / 1000.0;
    const double length = burst ? static_cast<double>(burst) : 1.0;
    if (bad) {
        if (rng.Unit() < 1.0 / length) bad = false;
    } else if (rng.Unit() < loss / (length * (1.0 - loss))) {
        bad = true;
    }
    return bad;
}

}  // namespace mgscoop::lab

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#ifndef SIO_UDP_NETRESET
#define SIO_UDP_NETRESET _WSAIOW(IOC_VENDOR, 15)
#endif

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

namespace {
PSRWLOCK delay_lock_of(void** storage) noexcept { return reinterpret_cast<PSRWLOCK>(storage); }
static_assert(sizeof(SRWLOCK) == sizeof(void*), "SRWLOCK storage is one pointer");
// Millisecond clock for the delay line and the idle deadline.
// QueryPerformanceCounter (not the 15.6 ms GetTickCount64 tick) so a datagram
// is never released early and a 20 ms idle cadence stays 20 ms.
std::uint64_t precise_ms() noexcept {
    LARGE_INTEGER frequency{}, counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart <= 0) return GetTickCount64();
    const auto f = static_cast<std::uint64_t>(frequency.QuadPart);
    const auto c = static_cast<std::uint64_t>(counter.QuadPart);
    return (c / f) * 1000 + ((c % f) * 1000) / f;
}
bool loopback_address(std::uint32_t address) noexcept { return (address >> 24) == 127; }
bool earlier(const std::uint64_t due_a, std::uint64_t order_a, std::uint64_t due_b, std::uint64_t order_b) noexcept {
    return due_a < due_b || (due_a == due_b && order_a < order_b);
}
}  // namespace

bool UdpTransport::start(const TransportConfig& config, const DatagramSink& sink) noexcept {
    if (running_) return true;
    config_ = config;
    sink_ = sink;
    error_ = TransportError::none;
    // The Steam emulator and older callers set only the two legacy fields.
    if (ImpairmentAllZero(config_.impair)) {
        config_.impair.latency_ms = config_.latency_ms;
        config_.impair.loss_permille = config_.loss_permille;
    }
    const bool loopback_bind = loopback_address(config_.bind_address);
    const bool listen_ok = config_.listen_port != 0 || config_.allow_ephemeral;
    const bool peer_ok = config_.peer_port != 0 || config_.any_source;
    const bool distinct = !(loopback_bind && loopback_address(config_.peer_address)) || config_.listen_port != config_.peer_port;
    if (!listen_ok || !peer_ok || !distinct) {
        error_ = TransportError::bad_config;
        return false;
    }
    rng_.Seed(config_.impair.seed);
    burst_ = BurstLoss{};
    delayed_count_ = 0;
    delayed_order_ = 0;
    InitOnceExecuteOnce(&winsock_once, start_winsock, nullptr, nullptr);
    if (!winsock_ready) { error_ = TransportError::winsock; return false; }
    // WSA_FLAG_OVERLAPPED is required even though no overlapped I/O is issued:
    // without it the socket is a synchronous file object, the I/O manager
    // serializes every request on it, and a game-thread send waits for the
    // receive thread's select() poll to time out (measured: 20 sends took
    // 200 ms instead of 0.2 ms; see the steam_loopback_tests regression check).
    const SOCKET s = WSASocketW(AF_INET, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0,
                                WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    if (s == INVALID_SOCKET) { error_ = TransportError::socket; return false; }
    // An ICMP port-unreachable (peer not started yet) or TTL-expired must not
    // surface as a receive error on this datagram socket.
    BOOL report = FALSE;
    DWORD returned = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);
    WSAIoctl(s, SIO_UDP_NETRESET, &report, sizeof(report), nullptr, 0, &returned, nullptr, nullptr);
    int buffer = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&buffer), sizeof(buffer));
    // Non-blocking: the receive thread waits for readability with a bounded
    // select() (so it also wakes for the idle timer and the delay line) and
    // then drains with recvfrom, at most kDrainBatch datagrams per pass.
    u_long nonblocking = 1;
    ioctlsocket(s, FIONBIO, &nonblocking);
    BOOL exclusive = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.listen_port);
    address.sin_addr.s_addr = htonl(config_.bind_address);
    if (bind(s, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        closesocket(s);
        error_ = TransportError::bind;
        return false;
    }
    sockaddr_in bound{};
    int bound_size = sizeof(bound);
    local_port_ = getsockname(s, reinterpret_cast<sockaddr*>(&bound), &bound_size) == 0 ? ntohs(bound.sin_port) : config_.listen_port;
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

bool UdpTransport::send_now_chunks(Endpoint to, const WireChunk* chunks, unsigned count, std::size_t total) noexcept {
    WSABUF buffers[4]{};
    unsigned used = 0;
    for (unsigned i = 0; i < count && used < 4; ++i) {
        if (!chunks[i].size) continue;
        buffers[used].buf = const_cast<CHAR*>(static_cast<const CHAR*>(chunks[i].data));
        buffers[used].len = static_cast<ULONG>(chunks[i].size);
        ++used;
    }
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_port = htons(to.port);
    peer.sin_addr.s_addr = htonl(to.address);
    DWORD sent = 0;
    const int result = WSASendTo(static_cast<SOCKET>(socket_), buffers, used, &sent, 0,
                                 reinterpret_cast<const sockaddr*>(&peer), sizeof(peer), nullptr, nullptr);
    const bool ok = result == 0 && sent == total;
    InterlockedIncrement64(ok ? &tx_ : &dropped_);
    return ok;
}

bool UdpTransport::send_now(Endpoint to, const void* data, std::size_t size) noexcept {
    const WireChunk chunk{data, size};
    return send_now_chunks(to, &chunk, 1, size);
}

bool UdpTransport::transmit(const WireChunk* chunks, unsigned count, bool droppable) noexcept {
    return transmit_to(Endpoint{config_.peer_address, config_.peer_port}, chunks, count, droppable);
}

bool UdpTransport::push_delayed(const Delayed& item) noexcept {
    if (delayed_count_ >= kMaxDelayed) return false;
    unsigned at = delayed_count_++;
    while (at) {
        const unsigned parent = (at - 1) / 2;
        if (!earlier(item.due_ms, item.order, delayed_[parent].due_ms, delayed_[parent].order)) break;
        delayed_[at] = delayed_[parent];
        at = parent;
    }
    delayed_[at] = item;
    return true;
}

UdpTransport::Delayed UdpTransport::pop_delayed() noexcept {
    const Delayed top = delayed_[0];
    const Delayed last = delayed_[--delayed_count_];
    delayed_[delayed_count_] = Delayed{};
    if (delayed_count_) {
        unsigned at = 0;
        for (;;) {
            const unsigned left = 2 * at + 1, right = left + 1;
            unsigned best = at;
            const Delayed* best_item = &last;
            if (left < delayed_count_ && earlier(delayed_[left].due_ms, delayed_[left].order, best_item->due_ms, best_item->order)) {
                best = left;
                best_item = &delayed_[left];
            }
            if (right < delayed_count_ && earlier(delayed_[right].due_ms, delayed_[right].order, best_item->due_ms, best_item->order)) {
                best = right;
                best_item = &delayed_[right];
            }
            if (best == at) break;
            delayed_[at] = delayed_[best];
            at = best;
        }
        delayed_[at] = last;
    }
    return top;
}

bool UdpTransport::transmit_to(Endpoint to, const WireChunk* chunks, unsigned count, bool droppable) noexcept {
    if (!running_ || !chunks || !count || count > 4 || !to.port) return false;
    std::size_t total = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (chunks[i].size && !chunks[i].data) return false;
        total += chunks[i].size;
    }
    if (!total || total > kMaxDatagram) return false;
    const Impairment& impair = config_.impair;
    if (droppable && impair.loss_permille) {
        bool drop = false;
        if (impair.burst_model) {
            AcquireSRWLockExclusive(delay_lock_of(&delay_lock_));
            drop = burst_.Step(impair.loss_permille, impair.burst, rng_);
            ReleaseSRWLockExclusive(delay_lock_of(&delay_lock_));
        } else {
            const auto n = static_cast<std::uint64_t>(InterlockedIncrement64(&loss_counter_) - 1);
            drop = should_drop(impair.loss_permille, n);
        }
        if (drop) {
            InterlockedIncrement64(&dropped_);
            return true;  // "sent" and lost on the wire
        }
    }
    if (!ImpairmentDelays(impair)) return send_now_chunks(to, chunks, count, total);
    // Delay heap (lab impairment only): copy the datagram; the receive thread
    // sends it when due.
    auto* bytes = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, total));
    if (!bytes) {
        InterlockedIncrement64(&dropped_);
        return false;
    }
    std::size_t at = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!chunks[i].size) continue;
        std::memcpy(bytes + at, chunks[i].data, chunks[i].size);
        at += chunks[i].size;
    }
    std::uint8_t* copy = nullptr;
    bool queued = false, duplicated = false;
    AcquireSRWLockExclusive(delay_lock_of(&delay_lock_));
    std::uint64_t delay = impair.latency_ms;
    if (impair.jitter_ms) delay += rng_.Next() % (static_cast<std::uint64_t>(impair.jitter_ms) + 1);
    if (impair.reorder_permille && rng_.Next() % 1000 < impair.reorder_permille) delay += impair.reorder_ms;
    const std::uint64_t due = precise_ms() + delay;
    queued = push_delayed(Delayed{due, delayed_order_++, to, bytes, static_cast<std::uint32_t>(total)});
    if (queued && impair.dup_permille && rng_.Next() % 1000 < impair.dup_permille) {
        copy = static_cast<std::uint8_t*>(HeapAlloc(GetProcessHeap(), 0, total));
        if (copy) {
            std::memcpy(copy, bytes, total);
            duplicated = push_delayed(Delayed{due + 1, delayed_order_++, to, copy, static_cast<std::uint32_t>(total)});
        }
    }
    ReleaseSRWLockExclusive(delay_lock_of(&delay_lock_));
    if (!queued) {
        HeapFree(GetProcessHeap(), 0, bytes);
        InterlockedIncrement64(&dropped_);
    }
    if (copy && !duplicated) HeapFree(GetProcessHeap(), 0, copy);
    if (duplicated) InterlockedIncrement64(&duplicated_);
    return queued;
}

std::uint32_t UdpTransport::flush_delayed(std::uint32_t fallback) noexcept {
    for (;;) {
        Delayed item{};
        std::uint32_t wait = fallback;
        AcquireSRWLockExclusive(delay_lock_of(&delay_lock_));
        if (delayed_count_) {
            const std::uint64_t now = precise_ms();
            if (delayed_[0].due_ms <= now) {
                item = pop_delayed();
            } else {
                const std::uint64_t remaining = delayed_[0].due_ms - now;
                wait = remaining < fallback ? static_cast<std::uint32_t>(remaining) : fallback;
            }
        }
        ReleaseSRWLockExclusive(delay_lock_of(&delay_lock_));
        if (!item.bytes) return wait ? wait : 1;
        send_now(item.to, item.bytes, item.size);
        HeapFree(GetProcessHeap(), 0, item.bytes);
    }
}

void UdpTransport::clear_delayed() noexcept {
    AcquireSRWLockExclusive(delay_lock_of(&delay_lock_));
    for (unsigned i = 0; i < delayed_count_; ++i) {
        if (delayed_[i].bytes) HeapFree(GetProcessHeap(), 0, delayed_[i].bytes);
        delayed_[i] = Delayed{};
    }
    delayed_count_ = 0;
    ReleaseSRWLockExclusive(delay_lock_of(&delay_lock_));
}

TransportStats UdpTransport::stats() const noexcept {
    TransportStats out{};
    out.tx = static_cast<std::uint64_t>(tx_);
    out.rx = static_cast<std::uint64_t>(rx_);
    out.dropped = static_cast<std::uint64_t>(dropped_);
    out.rejected = static_cast<std::uint64_t>(rejected_endpoint_);
    out.duplicated = static_cast<std::uint64_t>(duplicated_);
    return out;
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
    const std::uint32_t interval_ms = config_.idle_interval_ms ? config_.idle_interval_ms : 250;
    const bool delays = ImpairmentDelays(config_.impair);
    std::uint64_t next_idle = precise_ms() + interval_ms;
    const SOCKET s = static_cast<SOCKET>(socket_);
    while (running_) {
        // Send due delayed datagrams first; wake up no later than the next one
        // or the idle deadline.
        // With a delay-type impairment a newly queued datagram must not wait
        // for a whole idle interval: poll at most every kDelayPollMs.
        std::uint32_t wait_ms = delays ? flush_delayed(interval_ms < kDelayPollMs ? interval_ms : kDelayPollMs) : interval_ms;
        const std::uint64_t before = precise_ms();
        const std::uint64_t idle_wait = next_idle > before ? next_idle - before : 0;
        if (idle_wait < wait_ms) wait_ms = static_cast<std::uint32_t>(idle_wait);
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(s, &readable);
        timeval tv{static_cast<long>(wait_ms / 1000), static_cast<long>((wait_ms % 1000) * 1000)};
        const int ready = select(0, &readable, nullptr, nullptr, &tv);
        if (!running_) break;
        if (ready > 0) {
            // Drain at most kDrainBatch datagrams, then let the timers run: a
            // flood can never starve the keepalives, handshake timers and the
            // delay line.
            for (unsigned n = 0; n < kDrainBatch; ++n) {
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
                const Endpoint source{ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)};
                if (!config_.any_source && (source.address != config_.peer_address || !config_.peer_port ||
                                            source.port != config_.peer_port)) {
                    InterlockedIncrement64(&rejected_endpoint_);
                } else {
                    InterlockedIncrement64(&rx_);
                    if (sink_.on_datagram) sink_.on_datagram(sink_.ctx, buffer, static_cast<std::size_t>(received), source);
                }
                if (!running_) break;
            }
        }
        if (delays) flush_delayed(interval_ms);
        const std::uint64_t now = precise_ms();
        if (now >= next_idle) {
            next_idle = now + interval_ms;
            if (sink_.on_idle) sink_.on_idle(sink_.ctx);
        }
    }
    HeapFree(GetProcessHeap(), 0, buffer);
}

void UdpTransport::stop() noexcept {
    if (InterlockedExchange(&running_, 0) == 0) return;
    // The receive thread polls with a bounded select() timeout and exits on the
    // cleared flag; close the socket only after it is gone.
    if (thread_) {
        WaitForSingleObject(thread_, 5000);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    const SOCKET s = static_cast<SOCKET>(socket_);
    socket_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
    if (s != INVALID_SOCKET) closesocket(s);
    clear_delayed();
}

}  // namespace mgscoop::lab
#endif  // _WIN32
