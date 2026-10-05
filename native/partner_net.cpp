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
// Received look (guarded by g_lock).
RemoteLook g_look{};
volatile LONG g_drop_epoch = 0, g_drop_seq = 0, g_drop_bad = 0;
// Received input (guarded by g_lock) and its send-side sequence.
partner_input::InputPacket g_input{};
bool g_input_valid = false;
std::uint32_t g_input_seq = 0;
std::uint64_t g_input_rx_ms = 0;
bool g_input_first_logged = false;
std::uint32_t g_input_tx_seq = 0;
std::uint64_t g_input_tx_ms = 0;
partner_input::InputEdges g_edges{};
volatile LONG g_in_tx = 0, g_in_rx = 0, g_in_drop = 0;
// Look send side (game thread only).
partner::Look g_look_sent{};
bool g_look_sent_valid = false;
std::uint16_t g_look_rev = 0;
std::uint64_t g_look_send_ms = 0;
volatile LONG g_look_resend = 1;
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

// Receive thread: a look frame (already decoded). Stores it for the current
// epoch; logs only when the values or the revision change.
void OnLook(const sync::LookPacket& packet, std::uint32_t epoch) noexcept {
    const LONG current = g_epoch;
    if (!current || epoch != static_cast<std::uint32_t>(current)) {
        InterlockedIncrement(&g_drop_epoch);
        return;
    }
    const partner::Look look{packet.type, packet.parts, packet.camo, packet.hand, packet.face, packet.face_equip};
    bool changed = false;
    AcquireSRWLockExclusive(&g_lock);
    changed = !g_look.valid || g_look.revision != packet.revision || !partner::SameLook(g_look.look, look);
    g_look.valid = true;
    g_look.look = look;
    g_look.revision = packet.revision;
    g_look.rx_ms = NowMs();
    ReleaseSRWLockExclusive(&g_lock);
    if (!changed) return;
    char line[160]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "partner_net=look_rx; type=%u; parts=%u; camo=%u; hand=%u; face=%u; fe=%u; rev=%u; loaded=%u\r\n",
            unsigned(look.type), unsigned(look.parts), unsigned(look.camo), unsigned(look.hand), unsigned(look.face),
            unsigned(look.face_equip), unsigned(packet.revision), (packet.flags & sync::LookFlag::PartsLoaded) ? 1u : 0u)))
        Log(line);
}

// Receive thread: decode and enqueue only. Order: keepalive, look, transform.
void OnDatagram(void*, const std::uint8_t* data, std::size_t size) noexcept {
    const DWORD error = GetLastError();
    std::uint64_t session = 0;
    if (sync::decode_keepalive_frame(data, size, session)) {
        if (session != kSession) InterlockedIncrement(&g_drop_bad);
        SetLastError(error);
        return;
    }
    std::uint32_t epoch = 0;
    sync::LookPacket look{};
    if (sync::decode_look_frame(data, size, session, epoch, look)) {
        if (session != kSession) InterlockedIncrement(&g_drop_bad);
        else OnLook(look, epoch);
        SetLastError(error);
        return;
    }
    std::uint32_t in_seq = 0;
    partner_input::InputPacket in{};
    if (sync::decode_input_frame(data, size, session, epoch, in_seq, in)) {
        if (session != kSession) { InterlockedIncrement(&g_drop_bad); SetLastError(error); return; }
        const LONG current = g_epoch;
        if (!current || epoch != static_cast<std::uint32_t>(current)) {
            InterlockedIncrement(&g_drop_epoch);
            SetLastError(error);
            return;
        }
        bool first = false, accepted = false;
        const std::uint64_t now = NowMs();
        AcquireSRWLockExclusive(&g_lock);
        if (AcceptInputSequence(g_input_valid, g_input_seq, g_input_rx_ms, in_seq, now)) {
            accepted = true;
            partner_input::AccumulateEdges(g_edges, in);
            g_input = in;
            g_input_valid = true;
            g_input_seq = in_seq;
            g_input_rx_ms = now;
            if (!g_input_first_logged) { g_input_first_logged = true; first = true; }
        }
        ReleaseSRWLockExclusive(&g_lock);
        InterlockedIncrement(accepted ? &g_in_rx : &g_in_drop);
        if (first) {
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_net=input_rx; first=1; seq=%u\r\n", in_seq))) Log(line);
        }
        SetLastError(error);
        return;
    }
    sync::PlayerTransformPacket packet{};
    sync::AnimState anim{};
    bool has_anim = false;
    if (!sync::decode_partner_transform_frame(data, size, session, epoch, packet, anim, has_anim) || session != kSession) {
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
    const bool anim_valid = has_anim && (anim.flags & sync::AnimFlag::Valid) != 0;
    const pose::Snapshot snapshot{packet.tick_ms, packet.x, packet.y, packet.z, packet.rot_y, packet.stance, packet.action_flags,
                                  anim_valid, anim.act, anim.stance, anim.speed};
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
        char line[384]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                "partner_net=stats; tx=%llu; rx=%llu; drop_epoch=%u; drop_seq=%u; drop_bad=%u; status=%s; last_age_ms=%u; "
                "native_us_avg=%u; native_us_max=%u; in_tx=%u; in_rx=%u; in_drop=%u\r\n",
                static_cast<unsigned long long>(current.tx), static_cast<unsigned long long>(current.rx), current.drop_epoch,
                current.drop_seq, current.drop_bad, pose::StatusName(status),
                static_cast<unsigned>(age > 0xFFFFFFFFull ? 0xFFFFFFFFu : age),
                count ? static_cast<unsigned>(sum / count) : 0u, max, static_cast<unsigned>(g_in_tx),
                static_cast<unsigned>(g_in_rx), static_cast<unsigned>(g_in_drop))) ) Log(line);
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

bool SubmitLocal(const partner::Tick& tick, const sync::AnimState& anim) noexcept {
    if (!g_running) return false;
    const LONG epoch = g_epoch;
    if (!epoch) return false;
    const DWORD error = GetLastError();
    const std::uint64_t now_us = NowUs();
    const std::uint64_t interval_us = 1000000u / g_config.rate_hz;
    // 2 ms tolerance keeps a 60 Hz caller on an exact 30/60 Hz cadence.
    if (g_last_frame_us && now_us - g_last_frame_us + 2000 < interval_us) { SetLastError(error); return false; }
    const auto packet = partner::MakeTransform(tick, ++g_seq, static_cast<std::uint32_t>(now_us / 1000));
    bool sent = false;
    if ((anim.flags & sync::AnimFlag::Valid) && sync::valid_anim(anim)) {
        std::uint8_t frame[sync::transform_anim_frame_size];
        if (sync::encode_transform_anim_frame(kSession, static_cast<std::uint32_t>(epoch), packet, anim, frame)) {
            const lab::WireChunk chunk{frame, sizeof(frame)};
            sent = g_transport.transmit(&chunk, 1, true);
        }
    } else {
        std::uint8_t frame[sync::transform_frame_size];
        if (sync::encode_transform_frame(kSession, static_cast<std::uint32_t>(epoch), packet, frame)) {
            const lab::WireChunk chunk{frame, sizeof(frame)};
            sent = g_transport.transmit(&chunk, 1, true);
        }
    }
    g_last_frame_us = now_us;
    InterlockedExchange64(&g_last_send_ms, static_cast<LONG64>(now_us / 1000));
    SetLastError(error);
    return sent;
}

bool SubmitLook(const partner::Look& look) noexcept {
    if (!g_running) return false;
    const LONG epoch = g_epoch;
    if (!epoch) return false;
    const DWORD error = GetLastError();
    const std::uint64_t now = NowMs();
    const bool changed = !g_look_sent_valid || !partner::SameLook(g_look_sent, look);
    const bool resend = InterlockedExchange(&g_look_resend, 0) != 0;
    if (!changed && !resend && now - g_look_send_ms < kLookHeartbeatMs) { SetLastError(error); return false; }
    if (changed) {
        g_look_rev = static_cast<std::uint16_t>(g_look_rev == 0xFFFF ? 1 : g_look_rev + 1);
        g_look_sent = look;
        g_look_sent_valid = true;
    }
    sync::LookPacket packet{look.type, look.parts, look.camo, look.hand, look.face, look.face_equip, sync::LookFlag::PartsLoaded,
                            g_look_rev};
    std::uint8_t frame[sync::look_frame_size];
    bool sent = false;
    if (sync::encode_look_frame(kSession, static_cast<std::uint32_t>(epoch), packet, frame)) {
        const lab::WireChunk chunk{frame, sizeof(frame)};
        sent = g_transport.transmit(&chunk, 1, true);
    }
    g_look_send_ms = now;
    SetLastError(error);
    return sent;
}

RemoteLook SampleLook() noexcept {
    AcquireSRWLockShared(&g_lock);
    const RemoteLook look = g_look;
    ReleaseSRWLockShared(&g_lock);
    return look;
}

bool SubmitInput(const partner_input::InputPacket& packet) noexcept {
    if (!g_running) return false;
    const LONG epoch = g_epoch;
    if (!epoch) return false;
    const DWORD error = GetLastError();
    const std::uint64_t now = NowMs();
    if (g_input_tx_ms && now - g_input_tx_ms < kInputMinGapMs) { SetLastError(error); return false; }
    const std::uint32_t seq = g_input_tx_seq + 1;
    std::uint8_t frame[sync::input_frame_size];
    bool sent = false;
    if (sync::encode_input_frame(kSession, static_cast<std::uint32_t>(epoch), seq, packet, frame)) {
        const lab::WireChunk chunk{frame, sizeof(frame)};
        sent = g_transport.transmit(&chunk, 1, true);
    }
    if (sent) { g_input_tx_seq = seq; g_input_tx_ms = now; InterlockedIncrement(&g_in_tx); }
    SetLastError(error);
    return sent;
}

bool ConsumeInput(partner_input::Sample& out) noexcept {
    out = partner_input::Sample{};
    if (!TryAcquireSRWLockExclusive(&g_lock)) return false;
    if (g_input_valid) {
        const std::uint64_t now = NowMs();
        out.valid = true;
        out.seq = g_input_seq;
        out.age_ms = now >= g_input_rx_ms ? static_cast<std::uint32_t>(now - g_input_rx_ms) : 0u;
        out.packet = g_input;
    }
    out.edge_press = g_edges.press;
    out.edge_release = g_edges.release;
    out.edge_tap12 = g_edges.tap12;
    g_edges.press = g_edges.release = g_edges.tap12 = 0;
    ReleaseSRWLockExclusive(&g_lock);
    return true;
}

bool LatestInput(partner_input::Sample& out) noexcept {
    out = partner_input::Sample{};
    if (!TryAcquireSRWLockShared(&g_lock)) return false;
    if (g_input_valid) {
        const std::uint64_t now = NowMs();
        out.valid = true;
        out.seq = g_input_seq;
        out.age_ms = now >= g_input_rx_ms ? static_cast<std::uint32_t>(now - g_input_rx_ms) : 0u;
        out.packet = g_input;
    }
    ReleaseSRWLockShared(&g_lock);
    return true;
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
    g_look = RemoteLook{};
    g_input = partner_input::InputPacket{};
    g_input_valid = false;
    g_input_seq = 0;
    g_input_rx_ms = 0;
    g_input_first_logged = false;
    g_edges = partner_input::InputEdges{};
    ReleaseSRWLockExclusive(&g_lock);
    g_input_tx_seq = 0;
    g_input_tx_ms = 0;
    InterlockedExchange(&g_look_resend, 1);
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
