// Original MGS Co-op offline partner spike netcode: a second loopback
// UdpTransport carrying fixed-size MGSC transform frames.
#include "mgscoop/partner_net.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include "mgscoop/player_sync.hpp"
#include "mgscoop/session.hpp"
#include "mgscoop/udp_transport.hpp"

namespace mgscoop::partner_net {
namespace {
namespace lab = mgscoop::lab;

// Constant-initialized, trivially destructible process state.
lab::UdpTransport g_transport;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
Config g_config{};
volatile LONG g_running = 0;
volatile LONG g_epoch = 0;
// Receive side, guarded by g_lock (the receive thread writes, the game thread samples).
SRWLOCK g_lock = SRWLOCK_INIT;
pose::SnapshotRing g_ring;
bool g_have_last = false;
std::uint32_t g_last_seq = 0;
std::uint64_t g_last_rx_ms = 0;
bool g_first_logged = false;
volatile LONG g_drop_epoch = 0, g_drop_seq = 0, g_drop_bad = 0;
// Send side: sequence and rate are game-thread only; the last send time is
// shared with the keepalive timer.
std::uint32_t g_seq = 0;
std::uint64_t g_last_frame_us = 0;
volatile LONG64 g_last_send_ms = 0;
// Native tick cost, reset by each stats line.
volatile LONG64 g_us_sum = 0;
volatile LONG g_us_count = 0, g_us_max = 0;
// Receive-thread-only stats bookkeeping.
std::uint64_t g_last_stats_ms = 0;
struct Logged {
    std::uint64_t tx, rx;
    std::uint32_t drop_epoch, drop_seq, drop_bad;
    pose::Status status;
};
Logged g_logged{};
bool g_stats_logged = false;

std::uint64_t NowUs() noexcept {
    LARGE_INTEGER frequency{}, counter{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    if (frequency.QuadPart <= 0) return GetTickCount64() * 1000;
    const auto f = static_cast<std::uint64_t>(frequency.QuadPart);
    const auto c = static_cast<std::uint64_t>(counter.QuadPart);
    return (c / f) * 1000000 + ((c % f) * 1000000) / f;
}

void Log(const char* line) noexcept {
    if (g_config.log) g_config.log(line);
}

const char* TransportErrorName(lab::TransportError error) noexcept {
    switch (error) {
    case lab::TransportError::bad_config: return "bad_config";
    case lab::TransportError::winsock: return "winsock";
    case lab::TransportError::socket: return "socket";
    case lab::TransportError::bind: return "bind";
    case lab::TransportError::thread: return "thread";
    default: return "bad_config";
    }
}

void SendKeepalive() noexcept {
    const LONG epoch = g_epoch;
    std::uint8_t frame[sync::keepalive_frame_size];
    if (!sync::encode_keepalive_frame(kSession, epoch ? static_cast<std::uint32_t>(epoch) : 1u, frame)) return;
    const lab::WireChunk chunk{frame, sizeof(frame)};
    g_transport.transmit(&chunk, 1, true);
}

// Receive thread: decode and enqueue only.
void OnDatagram(void*, const std::uint8_t* data, std::size_t size) noexcept {
    const DWORD error = GetLastError();
    std::uint64_t session = 0;
    if (sync::decode_keepalive_frame(data, size, session)) {
        if (session != kSession) InterlockedIncrement(&g_drop_bad);
        SetLastError(error);
        return;
    }
    sync::PlayerTransformPacket packet{};
    std::uint32_t epoch = 0;
    if (!sync::decode_transform_frame(data, size, session, epoch, packet) || session != kSession) {
        InterlockedIncrement(&g_drop_bad);
        SetLastError(error);
        return;
    }
    const LONG current = g_epoch;
    if (!current || epoch != static_cast<std::uint32_t>(current)) {
        InterlockedIncrement(&g_drop_epoch);
        SetLastError(error);
        return;
    }
    const std::uint64_t now = NowMs();
    const pose::Snapshot snapshot{packet.tick_ms, packet.x, packet.y, packet.z, packet.rot_y, packet.stance, packet.action_flags};
    bool accepted = true, first = false;
    AcquireSRWLockExclusive(&g_lock);
    const bool restart_gap = g_have_last && now - g_last_rx_ms > kRestartGapMs;
    if (g_have_last && !newer_sequence(packet.sequence, g_last_seq)) {
        if (restart_gap) g_ring.Reset();  // peer restarted its sequence
        else accepted = false;
    }
    if (accepted && !g_ring.Push(snapshot, now)) {
        if (restart_gap) { g_ring.Reset(); accepted = g_ring.Push(snapshot, now); }
        else accepted = false;
    }
    if (accepted) {
        g_have_last = true;
        g_last_seq = packet.sequence;
        g_last_rx_ms = now;
        if (!g_first_logged) { g_first_logged = true; first = true; }
    }
    ReleaseSRWLockExclusive(&g_lock);
    if (!accepted) InterlockedIncrement(&g_drop_seq);
    if (first) {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_net=first_rx; seq=%u; epoch=%u\r\n",
                                       packet.sequence, epoch))) Log(line);
    }
    SetLastError(error);
}

// Receive thread, every 250 ms: keepalive after 1 s without a send, and a
// stats line every 10 s when something changed.
void OnIdle(void*) noexcept {
    const DWORD error = GetLastError();
    const std::uint64_t now = NowMs();
    const auto last_send = static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_last_send_ms, 0, 0));
    if (now - last_send >= kKeepaliveAfterMs) {
        InterlockedExchange64(&g_last_send_ms, static_cast<LONG64>(now));
        SendKeepalive();
    }
    if (now - g_last_stats_ms < kStatsIntervalMs) { SetLastError(error); return; }
    g_last_stats_ms = now;
    const lab::TransportStats stats = g_transport.stats();
    pose::Status status = pose::Status::none;
    std::uint64_t age = 0;
    AcquireSRWLockShared(&g_lock);
    if (!g_ring.empty()) {
        age = now >= g_ring.newest_receive_ms() ? now - g_ring.newest_receive_ms() : 0;
        status = pose::StatusForAge(age);
    }
    ReleaseSRWLockShared(&g_lock);
    const Logged current{stats.tx, stats.rx, static_cast<std::uint32_t>(g_drop_epoch), static_cast<std::uint32_t>(g_drop_seq),
                         static_cast<std::uint32_t>(g_drop_bad), status};
    const auto sum = static_cast<std::uint64_t>(InterlockedExchange64(&g_us_sum, 0));
    const auto count = static_cast<std::uint32_t>(InterlockedExchange(&g_us_count, 0));
    const auto max = static_cast<std::uint32_t>(InterlockedExchange(&g_us_max, 0));
    const bool changed = !g_stats_logged || current.tx != g_logged.tx || current.rx != g_logged.rx ||
                         current.drop_epoch != g_logged.drop_epoch || current.drop_seq != g_logged.drop_seq ||
                         current.drop_bad != g_logged.drop_bad || current.status != g_logged.status;
    if (changed) {
        g_logged = current;
        g_stats_logged = true;
        char line[320]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "partner_net=stats; tx=%llu; rx=%llu; drop_epoch=%u; drop_seq=%u; drop_bad=%u; status=%s; last_age_ms=%u; "
                "native_us_avg=%u; native_us_max=%u\r\n",
                static_cast<unsigned long long>(current.tx), static_cast<unsigned long long>(current.rx), current.drop_epoch,
                current.drop_seq, current.drop_bad, pose::StatusName(status),
                static_cast<unsigned>(age > 0xFFFFFFFFull ? 0xFFFFFFFFu : age),
                count ? static_cast<unsigned>(sum / count) : 0u, max))) Log(line);
    }
    SetLastError(error);
}

BOOL CALLBACK StartOnce(PINIT_ONCE, PVOID parameter, PVOID*) noexcept {
    g_config = *static_cast<const Config*>(parameter);
    if (g_config.rate_hz < 1) g_config.rate_hz = 1;
    if (g_config.rate_hz > 60) g_config.rate_hz = 60;
    lab::TransportConfig transport{};
    transport.listen_port = g_config.listen_port;
    transport.peer_port = g_config.peer_port;
    transport.idle_interval_ms = 250;
    transport.latency_ms = g_config.latency_ms;
    transport.loss_permille = g_config.loss_permille;
    const lab::DatagramSink sink{&OnDatagram, &OnIdle, nullptr};
    g_last_stats_ms = NowMs();
    InterlockedExchange64(&g_last_send_ms, static_cast<LONG64>(g_last_stats_ms));
    char line[128]{};
    if (g_transport.start(transport, sink)) {
        InterlockedExchange(&g_running, 1);
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_net=listening; port=%u; peer_port=%u\r\n",
                                       static_cast<unsigned>(transport.listen_port), static_cast<unsigned>(transport.peer_port))))
            Log(line);
    } else if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_net=disabled; reason=transport_%s\r\n",
                                          TransportErrorName(g_transport.error())))) {
        Log(line);
    }
    return TRUE;
}
} // namespace

std::uint64_t NowMs() noexcept { return NowUs() / 1000; }

bool Start(const Config& config) noexcept {
    const DWORD error = GetLastError();
    InitOnceExecuteOnce(&g_once, StartOnce, const_cast<Config*>(&config), nullptr);
    SetLastError(error);
    return g_running != 0;
}

bool Running() noexcept { return g_running != 0; }

void SetEpoch(std::uint32_t epoch) noexcept { InterlockedExchange(&g_epoch, static_cast<LONG>(epoch)); }

bool SubmitLocal(const partner::Tick& tick) noexcept {
    if (!g_running) return false;
    const LONG epoch = g_epoch;
    if (!epoch) return false;
    const DWORD error = GetLastError();
    const std::uint64_t now_us = NowUs();
    const std::uint64_t interval_us = 1000000u / g_config.rate_hz;
    // 2 ms tolerance keeps a 60 Hz caller on an exact 30/60 Hz cadence.
    if (g_last_frame_us && now_us - g_last_frame_us + 2000 < interval_us) { SetLastError(error); return false; }
    const auto packet = partner::MakeTransform(tick, ++g_seq, static_cast<std::uint32_t>(now_us / 1000));
    std::uint8_t frame[sync::transform_frame_size];
    bool sent = false;
    if (sync::encode_transform_frame(kSession, static_cast<std::uint32_t>(epoch), packet, frame)) {
        const lab::WireChunk chunk{frame, sizeof(frame)};
        sent = g_transport.transmit(&chunk, 1, true);
    }
    g_last_frame_us = now_us;
    InterlockedExchange64(&g_last_send_ms, static_cast<LONG64>(now_us / 1000));
    SetLastError(error);
    return sent;
}

pose::Sample SampleRemote(std::uint32_t delay_ms) noexcept {
    const std::uint64_t now = NowMs();
    AcquireSRWLockShared(&g_lock);
    const pose::Sample sample = g_ring.SampleAt(now, delay_ms);
    ReleaseSRWLockShared(&g_lock);
    return sample;
}

void ResetRemote() noexcept {
    AcquireSRWLockExclusive(&g_lock);
    g_ring.Reset();
    g_have_last = false;
    g_last_seq = 0;
    g_last_rx_ms = 0;
    g_first_logged = false;
    ReleaseSRWLockExclusive(&g_lock);
}

void RecordNativeMicros(std::uint32_t micros) noexcept {
    InterlockedAdd64(&g_us_sum, static_cast<LONG64>(micros));
    InterlockedIncrement(&g_us_count);
    LONG seen = g_us_max;
    while (static_cast<LONG>(micros) > seen) {
        const LONG previous = InterlockedCompareExchange(&g_us_max, static_cast<LONG>(micros), seen);
        if (previous == seen) break;
        seen = previous;
    }
}

} // namespace mgscoop::partner_net
