// Original MGS Co-op offline partner spike netcode: a second UdpTransport
// carrying fixed-size MGSC frames, raw (legacy) or inside link v2 packets.
#include "mgscoop/partner_net.hpp"
#include "mgscoop/partner_action_mailbox.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/early_patches.hpp"
#include "mgscoop/lab_status.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/player_sync.hpp"
#include "mgscoop/session.hpp"
#include "mgscoop/steam_join_code.hpp"
#include "mgscoop/steam_net_callbacks.hpp"
#include "mgscoop/steam_net_dispatch.hpp"
#include "mgscoop/steam_transport.hpp"
#include "mgscoop/udp_transport.hpp"

namespace mgscoop::partner_net {
namespace {
namespace lab = mgscoop::lab;

// Constant-initialized, trivially destructible process state.
lab::UdpTransport g_transport;
// Steam online (docs/steam-online.md 7, 8): chosen once in Start, before any
// thread runs; the UDP path is unchanged when it is not selected.
lab::SteamTransport g_steam;
bool g_use_steam = false;
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
partner_actions::Payload g_local_actions{}; // owner-thread cache only
bool g_local_actions_valid = false;
std::uint64_t g_local_actions_ms = 0, g_action_tx_log_ms = 0, g_action_rx_log_ms = 0;
std::uint32_t g_action_tx_seq = 0;
partner_actions::Mailbox g_action_mailbox{}; // g_lock
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

// ---- Link v2 (docs/link-protocol.md). Lock order: receipt lock, g_tx_lock, g_lock.
bool g_v2 = false;
SRWLOCK g_tx_lock = SRWLOCK_INIT;
// Under g_tx_lock: the session, the reliable senders and the packer buffer.
link::Session g_session;
link::ReliableSender g_rel_tx[link::kReliableChannels];
std::uint8_t g_body[link::kLinkMaxBody];
// Under g_lock: the reliable receivers.
link::ReliableReceiver g_rel_rx[link::kReliableChannels];
// A receiver has a RELACK to send (avoids taking g_lock on every frame).
volatile LONG g_relack_dirty = 0;
volatile LONG g_tx_not_connected = 0;
volatile LONG g_link_state = static_cast<LONG>(link::LinkState::off);
volatile LONG g_applied_interp = 100;
volatile LONG g_partner_shown = 0, g_partner_look = 0, g_partner_mission = 0;
// LinkInfo seqlock (single writer: the receive thread, or Start before it runs).
volatile LONG g_info_seq = 0;
volatile LONG g_info_state = 0, g_info_session = 0, g_info_rtt = 0, g_info_jitter = 0, g_info_interp = 0, g_info_loss = 0;
// Receive-thread-only link bookkeeping.
link::InterpController g_interp;
std::uint64_t g_interp_update_ms = 0;
const char* g_disabled_reason = nullptr;  // link=disabled reason (state disabled)
wchar_t g_status_path[lab::kMaxAtomicPath]{};
std::uint64_t g_status_check_ms = 0, g_status_write_ms = 0;
char g_status_last[768]{};
std::uint32_t g_status_fail = 0, g_status_write_ms_max = 0;
std::uint64_t g_drop_logged[link::kDropReasons]{};
std::uint64_t g_drop_logged_ms = 0;
struct LinkLogged {
    bool valid;
    link::LinkState state;
    std::uint32_t session, rtt, rttvar, jitter, loss, resend, tx_bps, rx_bps, interp, rel, dup, src, status_fail, status_ms;
    std::int32_t offset;
};
LinkLogged g_link_logged{};
std::uint64_t g_link_stats_ms = 0, g_link_tx_bytes = 0, g_link_rx_bytes = 0;
std::uint32_t g_tx_bps = 0, g_rx_bps = 0;
link::HandshakeStep g_handshake_logged_step = link::HandshakeStep::hello_rx;
bool g_handshake_logged = false;
std::uint64_t g_handshake_logged_ms = 0;
char g_build[link::kBuildSize]{};
// World sync hooks (set once before Start; read by the receive thread only).
WorldHooks g_world{};
volatile LONG g_world_unknown_channel = 0;
// Status-file snapshot taken under g_tx_lock by the receive thread.
struct LinkSnapshot {
    link::LinkState state;
    std::uint32_t session, rtt_ms, rttvar_ms, jitter_ms, loss_permille, resend, rel_queued;
    std::int32_t offset_ms;
    bool have_offset;
    link::RejectReason reason;
    bool verified;
    bool has_peer;
    lab::Endpoint peer;
    char remote_build[link::kBuildSize + 1];
    std::uint64_t tx_bytes, rx_bytes, replay, src_changed;
    std::uint64_t drops[link::kDropReasons];
};

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

std::uint32_t Clamp32(std::uint64_t value) noexcept { return value > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(value); }

// ---- The transport ops table (UDP or Steam), chosen once in StartLink.
struct TransportOps {
    bool (*transmit_to)(lab::Endpoint to, const lab::WireChunk* chunks, unsigned count, bool droppable) noexcept;
    lab::TransportStats (*stats)() noexcept;
    std::uint16_t (*local_port)() noexcept;
};
bool UdpTransmitTo(lab::Endpoint to, const lab::WireChunk* chunks, unsigned count, bool droppable) noexcept {
    return g_transport.transmit_to(to, chunks, count, droppable);
}
lab::TransportStats UdpStats() noexcept { return g_transport.stats(); }
std::uint16_t UdpLocalPort() noexcept { return g_transport.local_port(); }
bool SteamTransmitTo(lab::Endpoint to, const lab::WireChunk* chunks, unsigned count, bool droppable) noexcept {
    return g_steam.transmit_to(to, chunks, count, droppable);
}
lab::TransportStats SteamStats() noexcept { return g_steam.stats(); }
std::uint16_t SteamLocalPort() noexcept { return 0; }
const TransportOps kUdpOps{&UdpTransmitTo, &UdpStats, &UdpLocalPort};
const TransportOps kSteamOps{&SteamTransmitTo, &SteamStats, &SteamLocalPort};
const TransportOps* g_ops = &kUdpOps;

// ---- Steam online state.
volatile LONG g_shutdown = 0;
volatile LONG g_steam_waiting = 0;      // guest without a host (link-status "waiting")
volatile LONG g_steam_ready = 0;
volatile LONG g_beacon_wanted = 0;
volatile LONG64 g_steam_host = 0;       // guest: the armed host
char g_steam_reason[32]{};              // "" or the steam=disabled reason (set once)
char g_steam_disabled[40]{};            // "steam_<reason>" for link=disabled
SRWLOCK g_secret_lock = SRWLOCK_INIT;   // the host's retained secret (code / invites)
char g_host_secret[link::kSecretChars]{};
bool g_host_secret_valid = false;
// The F9 paste mailbox (consumed on the transport thread).
SRWLOCK g_paste_lock = SRWLOCK_INIT;
char g_paste[96]{};
bool g_paste_pending = false;
std::uint64_t g_connecting_since_ms = 0;  // transport thread: guest connecting without WELCOME
volatile LONG g_steam_failed = 0;       // g_steam_reason is complete
void SteamGuestIdle(std::uint64_t now) noexcept;
void SteamFailedIdle() noexcept;
// Section 20 / UX: the guest's last Steam end reason (1252, 0 none; cleared
// when connected), the partner's goodbye (CLOSE) and the in-game status line.
volatile LONG g_steam_end = 0;
volatile LONG g_peer_left = 0;
std::uint64_t g_down_since_ms = 0;        // transport thread: the link left connected
link::LinkState g_hud_state = link::LinkState::off;
std::uint64_t g_hud_publish_ms = 0;

// ---- Link v2 send path (g_tx_lock held by every caller).
bool LinkTransmit(void*, const lab::Endpoint& to, const std::uint8_t* data, std::size_t size) noexcept {
    const lab::WireChunk chunk{data, size};
    return g_ops->transmit_to(to, &chunk, 1, true);
}

bool LinkRandom(void*, std::uint8_t* out, std::size_t size) noexcept {
    // Transport thread only (the session draws nonces in OnDatagram / OnTimer).
    return BCRYPT_SUCCESS(BCryptGenRandom(nullptr, out, static_cast<ULONG>(size), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
}

// The RELACKs due (dirty for at least min_age_ms) of every receiver
// (g_tx_lock held; takes g_lock: tx -> rx order). Each one taken is marked
// clean: PackBody always has room for them after at most one FRAME.
void TakeRelAcks(link::RelAckState (&acks)[link::kReliableChannels], std::uint64_t now_ms, std::uint64_t min_age_ms) noexcept {
    for (auto& a : acks) a = link::RelAckState{};
    if (!g_relack_dirty) return;
    bool still_dirty = false;
    AcquireSRWLockExclusive(&g_lock);
    for (unsigned ch = 1; ch < link::kReliableChannels; ++ch) {
        link::ReliableReceiver& rx = g_rel_rx[ch];
        if (!rx.dirty()) continue;
        if (now_ms - rx.dirty_since() < min_age_ms) { still_dirty = true; continue; }
        acks[ch].due = true;
        rx.AckState(acks[ch].next_expected, acks[ch].bits, acks[ch].free_slots);
        rx.ClearDirty();
    }
    // The flag changes only under g_lock, so a store racing with this pass is never lost.
    if (!still_dirty) InterlockedExchange(&g_relack_dirty, 0);
    ReleaseSRWLockExclusive(&g_lock);
}

// Game thread: one DATA packet with the FRAME first, then the RELACKs and
// any new reliable messages (retransmissions are the receive thread's).
bool LinkSendFrame(const std::uint8_t* frame, std::size_t size) noexcept {
    const std::uint64_t now = NowMs();
    bool sent = false, connected = false;
    AcquireSRWLockExclusive(&g_tx_lock);
    if (g_session.CanSendData()) {
        connected = true;
        link::RelAckState acks[link::kReliableChannels];
        TakeRelAcks(acks, now, 0);
        link::BodyWriter writer(g_body, sizeof(g_body));
        std::size_t no_retransmits = 0;
        if (link::PackBody(writer, frame, size, acks, g_rel_tx, now, g_session.rtt().rto_ms(), no_retransmits))
            sent = g_session.SendData(g_body, writer.size(), now);
    }
    ReleaseSRWLockExclusive(&g_tx_lock);
    if (!connected) InterlockedIncrement(&g_tx_not_connected);
    return sent;
}

bool Transmit(const std::uint8_t* frame, std::size_t size) noexcept {
    if (g_v2) return LinkSendFrame(frame, size);
    const lab::WireChunk chunk{frame, size};
    return g_transport.transmit(&chunk, 1, true);
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

// Receive thread: one MGSC frame (a raw datagram or a link v2 FRAME
// message). Decode and enqueue only. Order: keepalive, look, input, transform.
void OnFrame(const std::uint8_t* data, std::size_t size, std::uint64_t trusted_action_peer) noexcept {
    std::uint64_t session = 0;
    if (sync::decode_keepalive_frame(data, size, session)) {
        if (session != kSession) InterlockedIncrement(&g_drop_bad);
        return;
    }
    std::uint32_t epoch = 0;
    partner_actions::Frame actions{};
    if (partner_actions::Decode(data,size,actions)) {
        if (!ActionEventsCompiled()) return;
        const auto current = static_cast<std::uint32_t>(g_epoch);
        if (!trusted_action_peer || actions.session != kSession || !current || actions.epoch != current) { InterlockedIncrement(&g_drop_bad); return; }
        const auto now = NowMs();
        AcquireSRWLockExclusive(&g_lock);
        const bool stored = partner_actions::Store(g_action_mailbox,actions,trusted_action_peer,kSession,current,now,kRestartGapMs);
        ReleaseSRWLockExclusive(&g_lock);
        if (!stored) { InterlockedIncrement(&g_drop_seq); return; }
        if (!g_action_rx_log_ms || now - g_action_rx_log_ms >= 1000) {
            g_action_rx_log_ms = now; char line[240]{};
            if (SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"partner_actions=rx; mission=%u; generation=%u; burst_onsets=%u; reload_requests=%u; retained=%u; seq=%u; playback=off\r\n",
                actions.action.mission,actions.action.generation,actions.action.fire_count,actions.action.reload_count,unsigned(actions.action.count),actions.sequence))) Log(line);
        }
        return;
    }
    sync::LookPacket look{};
    if (sync::decode_look_frame(data, size, session, epoch, look)) {
        if (session != kSession) InterlockedIncrement(&g_drop_bad);
        else OnLook(look, epoch);
        return;
    }
    std::uint32_t in_seq = 0;
    partner_input::InputPacket in{};
    if (sync::decode_input_frame(data, size, session, epoch, in_seq, in)) {
        if (session != kSession) { InterlockedIncrement(&g_drop_bad); return; }
        const LONG current = g_epoch;
        if (!current || epoch != static_cast<std::uint32_t>(current)) {
            InterlockedIncrement(&g_drop_epoch);
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
        return;
    }
    sync::PlayerTransformPacket packet{};
    sync::AnimState anim{};
    bool has_anim = false;
    if (!sync::decode_partner_transform_frame(data, size, session, epoch, packet, anim, has_anim) || session != kSession) {
        InterlockedIncrement(&g_drop_bad);
        return;
    }
    const LONG current = g_epoch;
    if (!current || epoch != static_cast<std::uint32_t>(current)) {
        InterlockedIncrement(&g_drop_epoch);
        return;
    }
    const std::uint64_t now = NowMs();
    const bool anim_valid = has_anim && (anim.flags & sync::AnimFlag::Valid) != 0;
    const pose::Snapshot snapshot{packet.tick_ms, packet.x, packet.y, packet.z, packet.rot_y, packet.stance, packet.action_flags,
                                  anim_valid, anim.act, anim.stance, anim.speed, packet.equip_id};
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
}

// The receive half of ResetRemote (g_lock held).
void ResetReceiveLocked() noexcept {
    partner_actions::Reset(g_action_mailbox);
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
}

// ---- Link v2 logging (receive thread, outside both locks).
void FormatPeer(const lab::Endpoint& peer, char (&out)[24]) noexcept {
    if (g_use_steam) {
        // Steam endpoints are {account, 0x5354}: a masked SteamID, never an IPv4.
        steam_code::FormatPeer(peer.address ? lab::SteamIdOf(peer) : 0, out);
        return;
    }
    char address[16]{};
    link::MaskAddress(peer.address, address);
    if (FAILED(StringCchPrintfA(out, ARRAYSIZE(out), "%s:%u", address, static_cast<unsigned>(peer.port)))) out[0] = 0;
}

void LogEvent(const link::Event& e, std::uint64_t now_ms) noexcept {
    char line[256]{}, peer[24]{};
    FormatPeer(e.peer, peer);
    HRESULT hr = E_FAIL;
    switch (e.kind) {
    case link::EventKind::listening:
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=listening; port=%u\r\n", static_cast<unsigned>(g_ops->local_port()));
        break;
    case link::EventKind::connecting:
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=connecting; peer=%s; attempt=%u\r\n", peer, e.attempt);
        break;
    case link::EventKind::handshake:
        // At most one line per step change or per 10 s (HELLO floods stay quiet).
        if (g_handshake_logged && g_handshake_logged_step == e.step && now_ms - g_handshake_logged_ms < 10000) return;
        g_handshake_logged = true;
        g_handshake_logged_step = e.step;
        g_handshake_logged_ms = now_ms;
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=handshake; step=%s; peer=%s\r\n", link::HandshakeStepName(e.step), peer);
        break;
    case link::EventKind::connected:
        g_handshake_logged = false;
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=connected; session=%u; peer=%s; rtt_ms=%u; remote_build=%s\r\n", e.session,
                              peer, e.rtt_ms, e.remote_build);
        break;
    case link::EventKind::lost:
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=lost; after_ms=%u\r\n", e.after_ms);
        break;
    case link::EventKind::reconnecting:
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=reconnecting; attempt=%u\r\n", e.attempt);
        break;
    case link::EventKind::reconnected:
        g_handshake_logged = false;
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=reconnected; session=%u; peer=%s; gap_ms=%u\r\n", e.session, peer, e.gap_ms);
        break;
    case link::EventKind::rebound:
        g_handshake_logged = false;
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=rebound; session=%u; peer=%s\r\n", e.session, peer);
        break;
    case link::EventKind::rejected:
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=rejected; reason=%s; remote_proto=%u; remote_build=%s; verified=%u\r\n",
                              link::RejectReasonName(e.reason), static_cast<unsigned>(e.remote_proto), e.remote_build,
                              e.verified ? 1u : 0u);
        break;
    case link::EventKind::warn_build:
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=warn; reason=build_mismatch; remote_build=%s\r\n", e.remote_build);
        break;
    case link::EventKind::crypto:
        g_disabled_reason = "crypto";
        hr = StringCchPrintfA(line, ARRAYSIZE(line), "link=disabled; reason=crypto\r\n");
        break;
    }
    if (SUCCEEDED(hr)) Log(line);
    // The in-game toasts (coop_hud; transport thread, no lock held).
    switch (e.kind) {
    case link::EventKind::connected:
        InterlockedExchange(&g_peer_left, 0);
        InterlockedExchange(&g_steam_end, 0);
        coop_hud::Post(coop_hud::EventKind::connected, static_cast<std::int32_t>(e.session), peer);
        break;
    case link::EventKind::reconnected:
        InterlockedExchange(&g_peer_left, 0);
        InterlockedExchange(&g_steam_end, 0);
        coop_hud::Post(coop_hud::EventKind::reconnected, static_cast<std::int32_t>(e.gap_ms), peer);
        break;
    case link::EventKind::lost:
        if (!g_peer_left) coop_hud::Post(coop_hud::EventKind::lost, static_cast<std::int32_t>(e.after_ms), "");
        break;
    case link::EventKind::reconnecting:
        if (!g_peer_left) coop_hud::Post(coop_hud::EventKind::lost, 0, "");
        break;
    case link::EventKind::rejected:
        if (e.reason == link::RejectReason::build_mismatch) coop_hud::Post(coop_hud::EventKind::version, 0, e.remote_build);
        else coop_hud::Post(coop_hud::EventKind::refused, e.verified ? 1 : 0, link::RejectReasonName(e.reason));
        break;
    case link::EventKind::warn_build:
        coop_hud::Post(coop_hud::EventKind::version, 0, e.remote_build);
        break;
    default:
        break;
    }
}

// The in-game status line's view of the link (transport thread; at most
// every 100 ms or on a state change).
void PublishHud(const LinkSnapshot& s, link::LinkState state, std::uint64_t now) noexcept {
    if (!coop_hud::Enabled()) return;
    if (state != link::LinkState::connected && g_hud_state == link::LinkState::connected) g_down_since_ms = now;
    if (state == link::LinkState::connected) g_down_since_ms = 0;
    if (state == g_hud_state && now - g_hud_publish_ms < 100) return;
    if (state != link::LinkState::connected && !g_down_since_ms) g_down_since_ms = now;
    g_hud_state = state;
    g_hud_publish_ms = now;
    coop_hud::LinkView v{};
    v.state = static_cast<std::uint8_t>(state);
    v.host = g_config.host;
    v.steam = g_use_steam;
    v.play = g_config.play;
    v.accepting = g_use_steam && g_config.host && g_steam.accepting();
    v.waiting = g_steam_waiting != 0;
    v.left = g_peer_left != 0;
    v.verified = s.verified;
    v.route = static_cast<std::uint8_t>(g_use_steam ? g_steam.route() : steam_net::Route::none);
    v.rtt_ms = s.rtt_ms;
    v.jitter_ms = s.jitter_ms;
    v.loss_permille = s.loss_permille;
    v.down_ms = g_down_since_ms && now >= g_down_since_ms ? static_cast<std::uint32_t>(now - g_down_since_ms) : 0;
    v.steam_end = g_steam_end;
    if (state == link::LinkState::disabled && g_disabled_reason) StringCchCopyA(v.reason, ARRAYSIZE(v.reason), g_disabled_reason);
    else if (s.reason != link::RejectReason::none) StringCchCopyA(v.reason, ARRAYSIZE(v.reason), link::RejectReasonName(s.reason));
    if (s.has_peer && s.peer.port) FormatPeer(s.peer, v.peer);
    else if (g_use_steam && !g_config.host && g_steam_host) steam_code::FormatPeer(static_cast<std::uint64_t>(g_steam_host), v.peer);
    StringCchCopyA(v.remote_build, ARRAYSIZE(v.remote_build), s.remote_build);
    StringCchCopyNA(v.own_build, ARRAYSIZE(v.own_build), g_build, link::kBuildSize);
    if (g_use_steam && g_config.host) {
        AcquireSRWLockShared(&g_secret_lock);
        v.code_ready = g_host_secret_valid && g_steam.ready() && !g_shutdown;
        ReleaseSRWLockShared(&g_secret_lock);
    }
    v.publish_ms = GetTickCount64();
    coop_hud::PublishLink(v);
}

void TakeSnapshot(LinkSnapshot& s) noexcept {  // g_tx_lock held
    s.state = g_session.state();
    s.session = g_session.session_number();
    const link::RttEstimator& rtt = g_session.rtt();
    s.rtt_ms = rtt.have() ? rtt.srtt_ms() : 0;
    s.rttvar_ms = rtt.have() ? rtt.rttvar_ms() : 0;
    s.jitter_ms = g_session.jitter().jitter_ms();
    s.have_offset = g_session.jitter().have_offset();
    s.offset_ms = g_session.jitter().offset_ms();
    s.loss_permille = g_session.loss().Permille(NowMs());
    std::uint64_t resend = 0;
    unsigned queued = 0;
    for (const auto& tx : g_rel_tx) {
        resend += tx.retransmits();
        queued += tx.queued();
    }
    s.resend = Clamp32(resend);
    s.rel_queued = queued;
    s.reason = g_session.reject_reason();
    s.verified = g_session.reject_verified();
    s.has_peer = g_session.has_peer();
    s.peer = g_session.peer();
    StringCchCopyA(s.remote_build, ARRAYSIZE(s.remote_build), g_session.remote_build());
    const link::SessionStats& st = g_session.stats();
    s.tx_bytes = st.tx_bytes;
    s.rx_bytes = st.rx_bytes;
    s.replay = st.drops[static_cast<unsigned>(link::DropReason::replay)];
    s.src_changed = st.src_changed;
    for (unsigned i = 0; i < link::kDropReasons; ++i) s.drops[i] = st.drops[i];
}

void PublishInfo(const LinkSnapshot& s, std::uint32_t interp_ms) noexcept {
    InterlockedIncrement(&g_info_seq);  // odd: writing
    InterlockedExchange(&g_info_state, static_cast<LONG>(s.state));
    InterlockedExchange(&g_info_session, static_cast<LONG>(s.session));
    InterlockedExchange(&g_info_rtt, static_cast<LONG>(s.rtt_ms));
    InterlockedExchange(&g_info_jitter, static_cast<LONG>(s.jitter_ms));
    InterlockedExchange(&g_info_interp, static_cast<LONG>(interp_ms));
    InterlockedExchange(&g_info_loss, static_cast<LONG>(s.loss_permille));
    InterlockedIncrement(&g_info_seq);  // even: stable
    InterlockedExchange(&g_link_state, static_cast<LONG>(s.state));
}

const char* PartnerStatusName() noexcept {
    pose::Status status = pose::Status::none;
    AcquireSRWLockShared(&g_lock);
    if (!g_ring.empty()) {
        const std::uint64_t now = NowMs();
        status = pose::StatusForAge(now >= g_ring.newest_receive_ms() ? now - g_ring.newest_receive_ms() : 0);
    }
    ReleaseSRWLockShared(&g_lock);
    return status == pose::Status::none ? "none" : pose::StatusName(status);
}

// link-status.json: one ASCII line, keys in the documented order.
bool FormatStatus(const LinkSnapshot& s, link::LinkState state, char* out, std::size_t capacity, bool with_time) noexcept {
    char peer[24]{};
    if (s.has_peer && s.peer.port) FormatPeer(s.peer, peer);
    const char* reason = state == link::LinkState::disabled ? (g_disabled_reason ? g_disabled_reason : "")
                       : s.reason != link::RejectReason::none ? link::RejectReasonName(s.reason) : "";
    const bool verified = !(s.reason != link::RejectReason::none && !s.verified);
    char build[2 * link::kBuildSize + 4]{};
    std::size_t build_length = 0;
    if (!lab::AppendJsonString(build, sizeof(build), build_length, s.remote_build)) StringCchCopyA(build, ARRAYSIZE(build), "\"\"");
    char time_utc[32] = "\"\"";
    if (with_time) {
        FILETIME ft{};
        SYSTEMTIME st{};
        GetSystemTimeAsFileTime(&ft);
        if (FileTimeToSystemTime(&ft, &st))
            StringCchPrintfA(time_utc, ARRAYSIZE(time_utc), "\"%04u-%02u-%02uT%02u:%02u:%02uZ\"", st.wYear, st.wMonth, st.wDay,
                             st.wHour, st.wMinute, st.wSecond);
    }
    // Transport keys (docs/steam-online.md 11), appended before time_utc.
    char transport[160]{};
    std::size_t transport_length = 0;
    const char* steam_status = !g_use_steam ? "off" : g_steam_failed ? g_steam_reason : g_steam_ready ? "ok" : "acquiring";
    const char* route = g_use_steam ? steam_net::RouteName(g_steam.route()) : "none";
    if (!lab::AppendLinkTransportJson(transport, sizeof(transport), transport_length, g_use_steam ? "steam" : "udp", route,
                                      g_use_steam && g_config.host && g_steam.accepting(), steam_status))
        transport[0] = 0;
    // Section 20 / UX: the guest's Steam end reason (1252) and the goodbye.
    else if (FAILED(StringCchPrintfA(transport + transport_length, ARRAYSIZE(transport) - transport_length, "\"steam_end\":%ld,\"left\":%u,",
                                     static_cast<long>(g_steam_end), g_peer_left ? 1u : 0u)))
        transport[0] = 0;
    return SUCCEEDED(StringCchPrintfA(out, capacity,
        "{\"v\":1,\"pid\":%lu,\"mode\":\"%s\",\"role\":\"%s\",\"state\":\"%s\",\"reason\":\"%s\",\"verified\":%u,\"session\":%u,"
        "\"peer\":\"%s\",\"local_port\":%u,\"rtt_ms\":%u,\"jitter_ms\":%u,\"loss_permille\":%u,\"resend\":%u,"
        "\"tx_Bps\":%u,\"rx_Bps\":%u,\"interp_ms\":%u,\"remote_build\":%s,\"partner\":\"%s\",\"partner_shown\":%u,"
        "\"look\":%u,\"mission\":%u,\"saves\":\"%s\",%s\"time_utc\":%s}\n",
        GetCurrentProcessId(), g_config.play ? "play" : "lab", g_config.host ? "host" : "guest", link::LinkStateName(state), reason,
        verified ? 1u : 0u, s.session, peer, static_cast<unsigned>(g_ops->local_port()), s.rtt_ms, s.jitter_ms,
        s.loss_permille, s.resend, g_tx_bps, g_rx_bps, static_cast<unsigned>(EffectiveInterpMs(g_config.interp_ms)), build, PartnerStatusName(),
        g_partner_shown ? 1u : 0u, g_partner_look ? 1u : 0u, static_cast<unsigned>(g_partner_mission),
        g_config.saves_redirected ? "redirected" : "unchanged", transport, time_utc));
}

void WriteStatus(const LinkSnapshot& s, link::LinkState state, std::uint64_t now_ms) noexcept {
    if (!g_status_path[0]) return;
    if (now_ms - g_status_check_ms < kStatusCheckMs) return;
    g_status_check_ms = now_ms;
    char content[768]{};
    if (!FormatStatus(s, state, content, sizeof(content), false)) return;
    const bool changed = lstrcmpA(content, g_status_last) != 0;
    if (!changed && now_ms - g_status_write_ms < kStatusRefreshMs) return;
    char document[768]{};
    if (!FormatStatus(s, state, document, sizeof(document), true)) return;
    std::size_t length = 0;
    while (document[length]) ++length;
    const std::uint64_t started = NowMs();
    if (lab::WriteFileAtomic(g_status_path, document, length)) {
        StringCchCopyA(g_status_last, ARRAYSIZE(g_status_last), content);
        g_status_write_ms = now_ms;
    } else {
        ++g_status_fail;  // retried on the next check (a reader may hold the file)
    }
    const std::uint32_t took = Clamp32(NowMs() - started);
    if (took > g_status_write_ms_max) g_status_write_ms_max = took;
}

void LinkStats(const LinkSnapshot& s, link::LinkState state, std::uint64_t now_ms) noexcept {
    // Byte rates over the last stats window.
    if (!g_link_stats_ms) {
        g_link_stats_ms = now_ms;
        g_link_tx_bytes = s.tx_bytes;
        g_link_rx_bytes = s.rx_bytes;
        return;
    }
    if (now_ms - g_link_stats_ms < kStatsIntervalMs) return;
    const std::uint64_t span = now_ms - g_link_stats_ms;
    g_tx_bps = Clamp32((s.tx_bytes - g_link_tx_bytes) * 1000 / span);
    g_rx_bps = Clamp32((s.rx_bytes - g_link_rx_bytes) * 1000 / span);
    g_link_stats_ms = now_ms;
    g_link_tx_bytes = s.tx_bytes;
    g_link_rx_bytes = s.rx_bytes;
    // Drops: one line per reason per window, only when the count changed.
    for (unsigned i = 0; i < link::kDropReasons; ++i) {
        if (s.drops[i] == g_drop_logged[i]) continue;
        g_drop_logged[i] = s.drops[i];
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "link=drop; reason=%s; count=%llu\r\n",
                                       link::DropReasonName(static_cast<link::DropReason>(i)),
                                       static_cast<unsigned long long>(s.drops[i]))))
            Log(line);
    }
    if (state == link::LinkState::off) return;
    const LinkLogged current{true, state, s.session, s.rtt_ms, s.rttvar_ms, s.jitter_ms, s.loss_permille, s.resend, g_tx_bps,
                             g_rx_bps, EffectiveInterpMs(g_config.interp_ms), s.rel_queued, Clamp32(s.replay), Clamp32(s.src_changed),
                             g_status_fail, g_status_write_ms_max, s.have_offset ? s.offset_ms : 0};
    const LinkLogged& l = g_link_logged;
    const bool changed = !l.valid || l.state != current.state || l.session != current.session || l.rtt != current.rtt ||
                         l.rttvar != current.rttvar || l.jitter != current.jitter || l.loss != current.loss ||
                         l.resend != current.resend || l.tx_bps != current.tx_bps || l.rx_bps != current.rx_bps ||
                         l.interp != current.interp || l.rel != current.rel || l.dup != current.dup || l.src != current.src ||
                         l.status_fail != current.status_fail || l.status_ms != current.status_ms || l.offset != current.offset;
    if (!changed) return;
    g_link_logged = current;
    char line[448]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "link=stats; state=%s; session=%u; rtt_ms=%u; rttvar_ms=%u; jitter_ms=%u; loss_pct=%u.%u; resend=%u; tx_Bps=%u; "
            "rx_Bps=%u; interp_ms=%u; offset_ms=%d; rel_queued=%u; dup=%u; src_changed=%u; status_fail=%u; status_write_ms_max=%u\r\n",
            link::LinkStateName(state), current.session, current.rtt, current.rttvar, current.jitter, current.loss / 10,
            current.loss % 10, current.resend, current.tx_bps, current.rx_bps, current.interp, current.offset, current.rel,
            current.dup, current.src, current.status_fail, current.status_ms)))
        Log(line);
}

// Receive thread: one link v2 datagram.
void LinkDatagram(const std::uint8_t* data, std::size_t size, const lab::Endpoint& from) noexcept {
    constexpr unsigned kMaxFrames = 64, kMaxReliable = 32, kMaxEvents = 16, kMaxDatagrams = 8;
    struct Piece { const std::uint8_t* data; std::size_t size; };
    struct Rel { std::uint8_t channel; std::uint16_t id; const std::uint8_t* payload; std::size_t size; };
    Piece frames[kMaxFrames];
    Piece datagrams[kMaxDatagrams];
    Rel rels[kMaxReliable];
    link::Event events[kMaxEvents];
    unsigned frame_count = 0, rel_count = 0, event_count = 0, datagram_count = 0;
    bool close_seen = false;
    const std::uint64_t now = NowMs();
    AcquireSRWLockExclusive(&g_tx_lock);
    const link::Delivery delivery = g_session.OnDatagram(data, size, from, now);
    std::uint64_t action_peer = 0;
    partner_actions::TrustedPeer(true, delivery.data, from, {}, action_peer);
    if (delivery.new_session) {
        // Both halves in the commit's g_tx_lock section (tx -> rx order): no
        // packet of the new session carries a RELACK of the old receivers.
        for (auto& tx : g_rel_tx) tx.Reset();
        AcquireSRWLockExclusive(&g_lock);
        ResetReceiveLocked();
        for (auto& rx : g_rel_rx) rx.Reset();
        InterlockedExchange(&g_relack_dirty, 0);
        ReleaseSRWLockExclusive(&g_lock);
        InterlockedExchange(&g_look_resend, 1);
    }
    unsigned messages = 0;
    if (delivery.data && link::ValidateBody(delivery.body, delivery.size, messages)) {
        link::MessageReader reader(delivery.body, delivery.size);
        link::Message m{};
        bool transform = false;
        while (reader.Next(m)) {
            if (m.type == static_cast<std::uint8_t>(link::MsgType::frame)) {
                if (frame_count < kMaxFrames) frames[frame_count++] = Piece{m.data, m.size};
                transform = transform || m.size == sync::transform_frame_size || m.size == sync::transform_anim_frame_size;
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::reliable)) {
                link::ReliableMsg r{};
                if (link::DecodeReliable(m, r) && r.channel >= 1 && r.channel < link::kReliableChannels && rel_count < kMaxReliable)
                    rels[rel_count++] = Rel{r.channel, r.id, r.payload, r.size};
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::relack)) {
                link::RelAckMsg a{};
                if (link::DecodeRelAck(m, a) && a.channel >= 1 && a.channel < link::kReliableChannels)
                    g_rel_tx[a.channel].OnRelAck(a.next_expected, a.bits, a.free_slots, now);
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::close)) {
                close_seen = true;
                // An authenticated goodbye: the host is free for a new join at
                // once, the guest starts reconnecting (no 3 s / 10 s timeout).
                g_session.OnPeerClose(now);
            } else if (m.type == static_cast<std::uint8_t>(link::MsgType::datagram)) {
                link::DatagramMsg d{};
                if (!link::DecodeDatagram(m, d)) continue;
                if (d.channel != 1) InterlockedIncrement(&g_world_unknown_channel);
                else if (datagram_count < kMaxDatagrams) datagrams[datagram_count++] = Piece{d.payload, d.size};
            }
        }
        if (transform) g_session.jitter().OnPacket(static_cast<std::uint32_t>(now), delivery.send_ms, now);
    }
    link::Event e{};
    while (event_count < kMaxEvents && g_session.PopEvent(e)) events[event_count++] = e;
    InterlockedExchange(&g_link_state, static_cast<LONG>(g_session.state()));
    const std::uint32_t new_session = delivery.new_session ? g_session.session_number() : 0;
    const lab::Endpoint committed = g_session.peer();
    ReleaseSRWLockExclusive(&g_tx_lock);
    // Steam host: the committed guest is the session peer (accept policy, route).
    if (g_use_steam && delivery.new_session && g_config.host && committed.address) g_steam.set_peer(lab::SteamIdOf(committed));
    // World sync: the session first (it clears the world queues), then the
    // datagrams of this packet; both outside every partner_net lock.
    if (delivery.new_session && g_world.on_session) g_world.on_session(new_session);
    for (unsigned i = 0; g_world.on_datagram && i < datagram_count; ++i) g_world.on_datagram(datagrams[i].data, datagrams[i].size);
    if (rel_count) {
        AcquireSRWLockExclusive(&g_lock);
        for (unsigned i = 0; i < rel_count; ++i) g_rel_rx[rels[i].channel].Accept(rels[i].id, rels[i].payload, rels[i].size, now);
        InterlockedExchange(&g_relack_dirty, 1);
        ReleaseSRWLockExclusive(&g_lock);
    }
    for (unsigned i = 0; i < frame_count; ++i) OnFrame(frames[i].data, frames[i].size, action_peer);
    for (unsigned i = 0; i < event_count; ++i) LogEvent(events[i], now);
    if (close_seen) {
        InterlockedExchange(&g_peer_left, 1);
        Log(g_config.host ? "link=peer_left; role=host; next=accepting\r\n" : "link=peer_left; role=guest; next=reconnecting\r\n");
        coop_hud::Post(coop_hud::EventKind::peer_left, 0, "");
    }
}

void OnDatagram(void*, const std::uint8_t* data, std::size_t size, const lab::Endpoint& from) noexcept {
    const DWORD error = GetLastError();
    if (g_v2) LinkDatagram(data, size, from);
    else {
        std::uint64_t action_peer = 0;
        partner_actions::TrustedPeer(false,false,from,lab::Endpoint{g_config.peer_address,g_config.peer_port},action_peer);
        OnFrame(data,size,action_peer);
    }
    SetLastError(error);
}

// Receive thread, every kLinkIdleMs: reliable retransmits and due RELACKs,
// the session timers, the adaptive delay, stats and the status file.
void LinkIdle() noexcept {
    constexpr unsigned kMaxEvents = 16;
    link::Event events[kMaxEvents];
    unsigned event_count = 0;
    LinkSnapshot snapshot{};
    const std::uint64_t now = NowMs();
    if (g_use_steam) SteamFailedIdle();
    if (g_use_steam && !g_config.host) SteamGuestIdle(now);
    AcquireSRWLockExclusive(&g_tx_lock);
    if (g_session.CanSendData()) {
        // Paced reliable traffic (<= kRetransmitBudgetBytes retransmitted per
        // tick); RELACKs ride along with data, or go alone once due for
        // kAckDelayMs.
        std::size_t budget = link::kRetransmitBudgetBytes;
        const std::uint32_t rto = g_session.rtt().rto_ms();
        for (unsigned packets = 0; packets < 4; ++packets) {
            const bool data = link::ReliableDue(g_rel_tx, now, rto, budget);
            link::RelAckState acks[link::kReliableChannels];
            TakeRelAcks(acks, now, data ? 0 : link::kAckDelayMs);
            link::BodyWriter writer(g_body, sizeof(g_body));
            link::PackBody(writer, nullptr, 0, acks, g_rel_tx, now, rto, budget);
            if (!writer.size()) break;
            if (!g_session.SendData(g_body, writer.size(), now)) break;
        }
    }
    g_session.OnTimer(now);
    link::Event e{};
    while (event_count < kMaxEvents && g_session.PopEvent(e)) events[event_count++] = e;
    TakeSnapshot(snapshot);
    ReleaseSRWLockExclusive(&g_tx_lock);
    for (unsigned i = 0; i < event_count; ++i) LogEvent(events[i], now);
    // Adaptive interpolation: the target once per second, the slew every tick.
    if (g_config.interp_auto) {
        if (now - g_interp_update_ms >= 1000) {
            g_interp_update_ms = now;
            g_interp.Update(now, g_config.rate_hz, snapshot.jitter_ms, snapshot.loss_permille);
        }
        InterlockedExchange(&g_applied_interp, static_cast<LONG>(g_interp.Tick()));
    }
    const link::LinkState state = g_disabled_reason ? link::LinkState::disabled
                                : g_steam_waiting ? link::LinkState::waiting
                                                  : snapshot.state;
    snapshot.state = state;
    if (g_use_steam && !g_config.host) {
        // Guest beacon (10.4): connecting for more than 10 s without a WELCOME.
        const bool connecting = state == link::LinkState::connecting;
        if (!connecting) g_connecting_since_ms = 0;
        else if (!g_connecting_since_ms) g_connecting_since_ms = now;
        InterlockedExchange(&g_beacon_wanted, connecting && now - g_connecting_since_ms > 10000 ? 1 : 0);
    }
    PublishInfo(snapshot, EffectiveInterpMs(g_config.interp_ms));
    LinkStats(snapshot, state, now);
    WriteStatus(snapshot, state, now);
    PublishHud(snapshot, state, now);
    if (g_world.on_idle) g_world.on_idle(now);
}

// Receive thread: legacy keepalive after 1 s without a send (raw path), and a
// partner_net=stats line every 10 s when something changed (both paths).
void OnIdle(void*) noexcept {
    const DWORD error = GetLastError();
    const std::uint64_t now = NowMs();
    if (g_v2) {
        LinkIdle();
    } else {
        const auto last_send = static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_last_send_ms, 0, 0));
        if (now - last_send >= kKeepaliveAfterMs) {
            InterlockedExchange64(&g_last_send_ms, static_cast<LONG64>(now));
            SendKeepalive();
        }
    }
    if (now - g_last_stats_ms < kStatsIntervalMs) { SetLastError(error); return; }
    g_last_stats_ms = now;
    const lab::TransportStats stats = g_ops->stats();
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

// link-secret.txt (kernel32 file I/O; Start runs at observer install, never
// in DllMain). Every buffer that held the secret is cleared by the caller.
link::SecretStatus ReadSecret(const wchar_t* directory, char (&out)[link::kSecretChars]) noexcept {
    wchar_t path[lab::kMaxAtomicPath]{};
    if (!directory || FAILED(StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\mgs-coop\\link-secret.txt", directory)))
        return link::SecretStatus::missing;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return link::SecretStatus::missing;
    char text[64]{};
    DWORD read = 0;
    LARGE_INTEGER size{};
    const bool ok = GetFileSizeEx(file, &size) && size.QuadPart <= static_cast<LONGLONG>(sizeof(text)) &&
                    ReadFile(file, text, sizeof(text), &read, nullptr);
    CloseHandle(file);
    const link::SecretStatus status = ok ? link::ParseSecretText(text, read, out) : link::SecretStatus::bad_length;
    link::SecureZero(text, sizeof(text));
    return status;
}

// No receive thread will run: publish the disabled state and write
// link-status.json once from the starting thread.
void LogDisabled(const char* reason) noexcept {
    g_disabled_reason = reason;
    char line[96]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "link=disabled; reason=%s\r\n", reason))) Log(line);
    LinkSnapshot snapshot{};
    snapshot.state = link::LinkState::disabled;
    PublishInfo(snapshot, EffectiveInterpMs(g_config.interp_ms));
    WriteStatus(snapshot, link::LinkState::disabled, kStatusCheckMs + NowMs());
    PublishHud(snapshot, link::LinkState::disabled, NowMs());
}

void StartSteamLink() noexcept;

// Link v2 Start: the secret, then the transport, then the session.
void StartLink() noexcept {
    if (g_config.transport == Transport::steam) {
        StartSteamLink();
        return;
    }
    char secret[link::kSecretChars]{};
    const link::SecretStatus secret_status = ReadSecret(g_config.directory, secret);
    bool lab_secret = false;
    if (secret_status != link::SecretStatus::ok) {
        if (!g_config.play && secret_status == link::SecretStatus::missing && g_config.bind_loopback) {
            for (std::size_t i = 0; i < link::kSecretChars; ++i) secret[i] = link::kLabSecret[i];
            lab_secret = true;
        } else {
            switch (secret_status) {
            case link::SecretStatus::bad_length: LogDisabled("secret_bad_length"); break;
            case link::SecretStatus::bad_char: LogDisabled("secret_bad_char"); break;
            default: LogDisabled("secret_missing"); break;
            }
            link::SecureZero(secret, sizeof(secret));
            return;
        }
    }
    link::SessionConfig session{};
    session.role = g_config.host ? link::Role::host : link::Role::guest;
    session.play = g_config.play;
    session.exact_filter = g_config.bind_loopback;
    session.peer = lab::Endpoint{g_config.peer_address, g_config.peer_port};
    for (std::size_t i = 0; i < link::kBuildSize && g_config.build && g_config.build[i]; ++i) g_build[i] = g_config.build[i];
    for (std::size_t i = 0; i < link::kBuildSize; ++i) session.build[i] = g_build[i];
    link::DerivePsk(secret, session.psk);
    link::SecureZero(secret, sizeof(secret));

    lab::TransportConfig transport{};
    transport.listen_port = g_config.listen_port;
    transport.peer_port = g_config.bind_loopback || !g_config.host ? g_config.peer_port : 0;
    transport.idle_interval_ms = kLinkIdleMs;
    transport.bind_address = g_config.bind_loopback ? 0x7F000001u : g_config.bind_address;
    transport.peer_address = g_config.bind_loopback ? 0x7F000001u : g_config.peer_address;
    transport.any_source = !g_config.bind_loopback;
    transport.allow_ephemeral = !g_config.bind_loopback && !g_config.host;
    transport.impair = g_config.impair;
    transport.impair.latency_ms = g_config.latency_ms;
    transport.impair.loss_permille = g_config.loss_permille;
    if (g_config.interp_auto) {
        g_interp.Init(g_config.interp_min_ms, g_config.interp_max_ms);
        InterlockedExchange(&g_applied_interp, static_cast<LONG>(g_interp.applied()));
    }
    char line[256]{}, bind[16]{}, peer[24]{};
    if (g_config.bind_loopback) StringCchCopyA(bind, ARRAYSIZE(bind), "loopback");
    else if (!g_config.bind_address) StringCchCopyA(bind, ARRAYSIZE(bind), "any");
    else link::FormatAddress(g_config.bind_address, bind);
    if (g_config.host) StringCchCopyA(peer, ARRAYSIZE(peer), "learned");
    else FormatPeer(session.peer, peer);
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "link=config; mode=%s; role=%s; proto=%u; bind=%s; port=%u; peer=%s; auth=%s; interp=%s\r\n",
            g_config.play ? "play" : "lab", g_config.host ? "host" : "guest", static_cast<unsigned>(link::kLinkProto), bind,
            static_cast<unsigned>(g_config.listen_port), peer, lab_secret ? "lab" : "code", g_config.interp_auto ? "auto" : "fixed")))
        Log(line);
    const lab::DatagramSink sink{&OnDatagram, &OnIdle, nullptr};
    const std::uint64_t now = NowMs();
    g_last_stats_ms = now;
    InterlockedExchange64(&g_last_send_ms, static_cast<LONG64>(now));
    // The session exists before the receive thread starts (it may deliver at once).
    AcquireSRWLockExclusive(&g_tx_lock);
    g_session.Start(session, link::SessionIo{nullptr, &LinkTransmit, &LinkRandom}, now);
    ReleaseSRWLockExclusive(&g_tx_lock);
    link::SecureZero(&session.psk, sizeof(session.psk));
    if (!g_transport.start(transport, sink)) {
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_net=disabled; reason=transport_%s\r\n",
                                       TransportErrorName(g_transport.error()))))
            Log(line);
        char reason[32]{};
        StringCchPrintfA(reason, ARRAYSIZE(reason), "transport_%s", TransportErrorName(g_transport.error()));
        static char disabled_reason[32];
        StringCchCopyA(disabled_reason, ARRAYSIZE(disabled_reason), reason);
        LogDisabled(disabled_reason);
        return;
    }
    InterlockedExchange(&g_running, 1);
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_net=listening; port=%u; peer_port=%u\r\n",
                                   static_cast<unsigned>(g_transport.local_port()), static_cast<unsigned>(transport.peer_port))))
        Log(line);
    // Queued events (listening / connecting) are logged by the receive thread.
}

// ------------------------------------------------------------ Steam online
// The Steam guest's arm (8.2): on the transport thread, only while waiting or
// rejected. Derives K_psk, restarts the session for the new host under
// g_tx_lock with the same receive/reliable reset as a new session, then
// clears the world queues through on_session.
void ArmSteamGuest(std::uint64_t host, const char (&secret)[link::kSecretChars], const char* via, std::uint64_t now) noexcept {
    char line[160]{}, peer[24]{};
    const link::LinkState current = static_cast<link::LinkState>(g_link_state);
    AcquireSRWLockShared(&g_tx_lock);
    const bool bad_code_hint = g_session.reject_reason() == link::RejectReason::bad_code;
    ReleaseSRWLockShared(&g_tx_lock);
    // Waiting or rejected; a pasted code may also replace one the host refused
    // as bad_code (an unauthenticated hint that leaves the guest connecting).
    const bool armable = g_steam_waiting || current == link::LinkState::rejected ||
                         (lstrcmpA(via, "code") == 0 && bad_code_hint &&
                          (current == link::LinkState::connecting || current == link::LinkState::reconnecting));
    if (!armable) {
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=join_ignored; reason=busy; via=%s\r\n", via))) Log(line);
        return;
    }
    if (host == g_config.steam_self) {
        Log("steam=join_refused; reason=peer_is_self\r\n");
        return;
    }
    link::SessionConfig session{};
    session.role = link::Role::guest;
    session.play = g_config.play;
    session.exact_filter = true;  // C8: the Messages leg delivers only this host
    session.peer = lab::SteamEndpoint(host);
    for (std::size_t i = 0; i < link::kBuildSize; ++i) session.build[i] = g_build[i];
    link::DerivePsk(secret, session.psk);
    AcquireSRWLockExclusive(&g_tx_lock);
    g_session.Start(session, link::SessionIo{nullptr, &LinkTransmit, &LinkRandom}, now);
    for (auto& tx : g_rel_tx) tx.Reset();
    AcquireSRWLockExclusive(&g_lock);
    ResetReceiveLocked();
    for (auto& rx : g_rel_rx) rx.Reset();
    InterlockedExchange(&g_relack_dirty, 0);
    ReleaseSRWLockExclusive(&g_lock);
    const std::uint32_t session_number = g_session.session_number();
    InterlockedExchange(&g_link_state, static_cast<LONG>(g_session.state()));
    ReleaseSRWLockExclusive(&g_tx_lock);
    link::SecureZero(&session.psk, sizeof(session.psk));
    g_steam.set_host(host);
    InterlockedExchange64(&g_steam_host, static_cast<LONG64>(host));
    InterlockedExchange(&g_steam_waiting, 0);
    InterlockedExchange(&g_look_resend, 1);
    InterlockedExchange(&g_steam_end, 0);
    InterlockedExchange(&g_peer_left, 0);
    g_connecting_since_ms = 0;
    if (g_world.on_session) g_world.on_session(session_number);
    steam_code::FormatPeer(host, peer);
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=join; via=%s; host=%s\r\n", via, peer))) Log(line);
    coop_hud::Post(coop_hud::EventKind::joining, lstrcmpA(via, "invite") == 0 ? 1 : 0, peer);
}

// Transport thread, every LinkIdle (both roles): a 1252 the function pointer
// stored (docs/steam-online.md 20). Logged; for the guest, a failure of the
// session with its configured host becomes link-status "steam_end" and the
// in-game sentence until the link connects.
void SteamFailedIdle() noexcept {
    steam_dispatch::SessionFailed f{};
    if (!steam_dispatch::TakeSessionFailed(f)) return;
    char line[224]{}, peer[24]{};
    steam_code::FormatPeer(f.peer, peer);
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=session_failed; peer=%s; end=%ld; state=%s; debug=\"%s\"\r\n", peer,
                                   static_cast<long>(f.end_reason), steam_net::ConnectionStateName(f.state), f.debug)))
        Log(line);
    const auto host = static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_steam_host, 0, 0));
    if (g_config.host || !f.peer || f.peer != host) return;
    if (static_cast<link::LinkState>(g_link_state) == link::LinkState::connected) return;
    if (InterlockedExchange(&g_steam_end, f.end_reason) != f.end_reason) coop_hud::Post(coop_hud::EventKind::steam_end, f.end_reason, peer);
}

// Transport thread, every LinkIdle (guest): the 337 mailbox and the F9 paste.
void SteamGuestIdle(std::uint64_t now) noexcept {
    steam_net::JoinRequested request{};
    if (steam_cb::TakeJoinRequest(request)) {
        steam_code::Code code{};
        const steam_code::JoinResult result =
            steam_code::ParseJoinRequest(request.friend_id, request.connect, sizeof(request.connect), code);
        link::SecureZero(&request, sizeof(request));
        if (result.status == steam_code::JoinStatus::ok) {
            char secret[link::kSecretChars]{};
            for (std::size_t i = 0; i < link::kSecretChars; ++i) secret[i] = code.secret[i];
            ArmSteamGuest(code.host, secret, "invite", now);
            link::SecureZero(secret, sizeof(secret));
        } else {
            char line[96]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=join_refused; reason=%s; via=invite\r\n",
                                           steam_code::JoinReason(result))))
                Log(line);
            coop_hud::Post(coop_hud::EventKind::join_refused, 1, steam_code::JoinReason(result));
        }
        link::SecureZero(&code, sizeof(code));
    }
    char text[sizeof(g_paste)]{};
    bool pasted = false;
    AcquireSRWLockExclusive(&g_paste_lock);
    if (g_paste_pending) {
        for (std::size_t i = 0; i < sizeof(text); ++i) text[i] = g_paste[i];
        link::SecureZero(g_paste, sizeof(g_paste));
        g_paste_pending = false;
        pasted = true;
    }
    ReleaseSRWLockExclusive(&g_paste_lock);
    if (!pasted) return;
    steam_code::Code code{};
    std::size_t length = 0;
    while (length < sizeof(text) && text[length]) ++length;
    const steam_code::Status status = steam_code::Parse(text, length, code);
    link::SecureZero(text, sizeof(text));
    if (status == steam_code::Status::ok) {
        char secret[link::kSecretChars]{};
        for (std::size_t i = 0; i < link::kSecretChars; ++i) secret[i] = code.secret[i];
        ArmSteamGuest(code.host, secret, "code", now);
        link::SecureZero(secret, sizeof(secret));
    } else {
        char line[96]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "steam=join_refused; reason=%s; via=code\r\n", steam_code::StatusName(status))))
            Log(line);
        coop_hud::Post(coop_hud::EventKind::join_refused, 0, steam_code::StatusName(status));
    }
    link::SecureZero(&code, sizeof(code));
}

void SteamOnDatagram(void* ctx, const std::uint8_t* data, std::size_t size, const lab::Endpoint& from) noexcept { OnDatagram(ctx, data, size, from); }
void SteamOnIdle(void* ctx) noexcept { OnIdle(ctx); }
void SteamOnReady(void*) noexcept {
    InterlockedExchange(&g_steam_ready, 1);
    // Without the SteamAPI_Init wrapper (refused in DllMain) no 1251/337 is
    // registered: the host accepts only by polling known ids (6.1).
    const char* callbacks = lab::InitWrapperInstalled() ? (g_config.host ? "callback" : "invite") : "poll_only";
    char line[192]{};
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
                                   "steam=transport; leg=messages; role=%s; channel=0x%X; accepting=%u; callbacks=%s; dispatch=%s\r\n",
                                   g_config.host ? "host" : "guest", static_cast<unsigned>(steam_net::kLinkChannel),
                                   g_config.host && g_steam.accepting() ? 1u : 0u, callbacks,
                                   SteamDispatchName(static_cast<SteamDispatch>(g_config.steam_dispatch)))))
        Log(line);
}
void SteamOnFailed(void*, const char* reason) noexcept {
    if (g_steam_failed) return;
    StringCchCopyA(g_steam_reason, ARRAYSIZE(g_steam_reason), reason ? reason : "unknown");
    StringCchPrintfA(g_steam_disabled, ARRAYSIZE(g_steam_disabled), "steam_%s", g_steam_reason);
    InterlockedExchange(&g_steam_failed, 1);  // full barrier: the strings are complete
    LogDisabled(g_steam_disabled);
    coop_hud::Post(coop_hud::EventKind::steam_off, 0, g_steam_disabled);
}
// Shutdown hand-off (transport thread): one link CLOSE while connected.
void SteamOnShutdown(void*) noexcept {
    const std::uint64_t now = NowMs();
    AcquireSRWLockExclusive(&g_tx_lock);
    if (g_session.CanSendData()) {
        link::BodyWriter writer(g_body, sizeof(g_body));
        if (writer.Add(link::MsgType::close, nullptr, 0)) g_session.SendData(g_body, writer.size(), now);
    }
    ReleaseSRWLockExclusive(&g_tx_lock);
    AcquireSRWLockExclusive(&g_secret_lock);
    link::SecureZero(g_host_secret, sizeof(g_host_secret));
    g_host_secret_valid = false;
    ReleaseSRWLockExclusive(&g_secret_lock);
}

// Steam Start: the secret (unless a waiting guest), the session, then the
// transport (its thread waits for the owner context's acquisition).
void StartSteamLink() noexcept {
    g_use_steam = true;
    g_ops = &kSteamOps;
    const bool waiting = !g_config.host && (g_config.steam_wait || !g_config.steam_peer);
    char secret[link::kSecretChars]{};
    if (!waiting) {
        const link::SecretStatus secret_status = ReadSecret(g_config.directory, secret);
        if (secret_status != link::SecretStatus::ok) {
            switch (secret_status) {
            case link::SecretStatus::bad_length: LogDisabled("secret_bad_length"); break;
            case link::SecretStatus::bad_char: LogDisabled("secret_bad_char"); break;
            default: LogDisabled("secret_missing"); break;
            }
            link::SecureZero(secret, sizeof(secret));
            return;
        }
    }
    for (std::size_t i = 0; i < link::kBuildSize && g_config.build && g_config.build[i]; ++i) g_build[i] = g_config.build[i];
    if (g_config.host) {
        // 8.3: kept only to build the code for F9 Copy and invites; zeroed on Shutdown.
        AcquireSRWLockExclusive(&g_secret_lock);
        for (std::size_t i = 0; i < link::kSecretChars; ++i) g_host_secret[i] = secret[i];
        g_host_secret_valid = true;
        ReleaseSRWLockExclusive(&g_secret_lock);
    }
    if (g_config.interp_auto) {
        g_interp.Init(g_config.interp_min_ms, g_config.interp_max_ms);
        InterlockedExchange(&g_applied_interp, static_cast<LONG>(g_interp.applied()));
    }
    char line[256]{}, peer[24]{};
    if (g_config.host) StringCchCopyA(peer, ARRAYSIZE(peer), "learned");
    else if (waiting) StringCchCopyA(peer, ARRAYSIZE(peer), "waiting");
    else steam_code::FormatPeer(g_config.steam_peer, peer);
    if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line),
            "link=config; mode=%s; role=%s; proto=%u; bind=steam; port=0; peer=%s; auth=%s; interp=%s\r\n",
            g_config.play ? "play" : "lab", g_config.host ? "host" : "guest", static_cast<unsigned>(link::kLinkProto), peer,
            waiting ? "pending" : "code", g_config.interp_auto ? "auto" : "fixed")))
        Log(line);
    const std::uint64_t now = NowMs();
    g_last_stats_ms = now;
    InterlockedExchange64(&g_last_send_ms, static_cast<LONG64>(now));
    if (waiting) {
        InterlockedExchange(&g_steam_waiting, 1);
    } else {
        link::SessionConfig session{};
        session.role = g_config.host ? link::Role::host : link::Role::guest;
        session.play = g_config.play;
        session.exact_filter = true;  // C8 (the guest's leg delivers only its host)
        session.peer = g_config.host ? lab::Endpoint{} : lab::SteamEndpoint(g_config.steam_peer);
        for (std::size_t i = 0; i < link::kBuildSize; ++i) session.build[i] = g_build[i];
        link::DerivePsk(secret, session.psk);
        AcquireSRWLockExclusive(&g_tx_lock);
        g_session.Start(session, link::SessionIo{nullptr, &LinkTransmit, &LinkRandom}, now);
        ReleaseSRWLockExclusive(&g_tx_lock);
        link::SecureZero(&session.psk, sizeof(session.psk));
        if (!g_config.host) InterlockedExchange64(&g_steam_host, static_cast<LONG64>(g_config.steam_peer));
    }
    link::SecureZero(secret, sizeof(secret));
    lab::SteamTransportConfig transport{};
    transport.role = g_config.host ? lab::SteamRole::host : lab::SteamRole::guest;
    transport.leg = lab::SteamLeg::messages;
    transport.self_id = g_config.steam_self;
    transport.host_id = waiting ? 0 : g_config.steam_peer;
    transport.idle_interval_ms = kLinkIdleMs;
    transport.accepting = g_config.accepting;
    transport.warm_relay = true;
    transport.resolve = g_config.steam_resolve;
    transport.resolve_ctx = g_config.steam_resolve_ctx;
    transport.pop_request = g_config.host ? g_config.steam_requests : nullptr;
    transport.dispatch = steam_dispatch::InstallBits(static_cast<SteamDispatch>(g_config.steam_dispatch), g_config.host);
    transport.log = g_config.log;
    lab::SteamSink sink{};
    sink.on_datagram = &SteamOnDatagram;
    sink.on_idle = &SteamOnIdle;
    sink.on_ready = &SteamOnReady;
    sink.on_failed = &SteamOnFailed;
    sink.on_shutdown = &SteamOnShutdown;
    if (!transport.resolve || !g_steam.start(transport, sink)) {
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_net=disabled; reason=transport_%s\r\n",
                                       transport.resolve ? TransportErrorName(g_steam.error()) : "bad_config")))
            Log(line);
        StringCchCopyA(g_steam_reason, ARRAYSIZE(g_steam_reason), "transport");
        InterlockedExchange(&g_steam_failed, 1);
        LogDisabled("steam_transport");
        return;
    }
    InterlockedExchange(&g_running, 1);
    Log("partner_net=listening; port=0; peer_port=0\r\n");
    // Until the owner context resolves the API: the status file says so.
    LinkSnapshot snapshot{};
    AcquireSRWLockExclusive(&g_tx_lock);
    TakeSnapshot(snapshot);
    ReleaseSRWLockExclusive(&g_tx_lock);
    snapshot.state = waiting ? link::LinkState::waiting : snapshot.state;
    PublishInfo(snapshot, EffectiveInterpMs(g_config.interp_ms));
    WriteStatus(snapshot, snapshot.state, kStatusCheckMs + NowMs());
}

BOOL CALLBACK StartOnce(PINIT_ONCE, PVOID parameter, PVOID*) noexcept {
    g_config = *static_cast<const Config*>(parameter);
    // The in-game status line exists with a link v2 session only.
    coop_hud::Configure(g_config.hud, g_config.link_v2);
    if (g_config.rate_hz < 1) g_config.rate_hz = 1;
    if (g_config.rate_hz > 60) g_config.rate_hz = 60;
    if (g_config.directory &&
        FAILED(StringCchPrintfW(g_status_path, ARRAYSIZE(g_status_path), L"%s\\mgs-coop\\link-status.json", g_config.directory)))
        g_status_path[0] = 0;
    if (g_config.link_v2) {
        g_v2 = true;
        StartLink();
        g_config.directory = nullptr;  // the caller's buffer is not kept
        return TRUE;
    }
    g_config.directory = nullptr;
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

bool ActionEventsCompiled() noexcept {
#if defined(MGS_LOCAL_ACTION_EVENTS) && MGS_LOCAL_ACTION_EVENTS
    return true;
#else
    return false;
#endif
}
bool SetLocalActions(const partner_actions::Payload& payload) noexcept {
    if (!ActionEventsCompiled() || !g_running || !partner_actions::Valid(payload)) return false;
    g_local_actions = payload; g_local_actions_valid = true; g_local_actions_ms = NowMs(); return true;
}
void ClearLocalActions() noexcept { g_local_actions = {}; g_local_actions_valid = false; g_local_actions_ms = 0; }
bool SampleActions(std::uint32_t mission, ActionSample& out) noexcept {
    out = {};
    if (!ActionEventsCompiled() || !g_running || !mission || !TryAcquireSRWLockExclusive(&g_lock)) return false;
    const bool connected = !g_v2 || static_cast<link::LinkState>(g_link_state) == link::LinkState::connected;
    out.fresh = partner_actions::SampleMailbox(g_action_mailbox,kSession,static_cast<std::uint32_t>(g_epoch),mission,NowMs(),connected,out.receipt,out.age_ms);
    ReleaseSRWLockExclusive(&g_lock);
    return out.fresh;
}

void SetEpoch(std::uint32_t epoch) noexcept {
    const auto old = InterlockedExchange(&g_epoch, static_cast<LONG>(epoch));
    if (static_cast<std::uint32_t>(old) != epoch) {
        ClearLocalActions();
        AcquireSRWLockExclusive(&g_lock);
        partner_actions::Reset(g_action_mailbox);
        ReleaseSRWLockExclusive(&g_lock);
    }
}

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
        if (sync::encode_transform_anim_frame(kSession, static_cast<std::uint32_t>(epoch), packet, anim, frame))
            sent = Transmit(frame, sizeof(frame));
    } else {
        std::uint8_t frame[sync::transform_frame_size];
        if (sync::encode_transform_frame(kSession, static_cast<std::uint32_t>(epoch), packet, frame))
            sent = Transmit(frame, sizeof(frame));
    }
    const auto now_ms = now_us / 1000;
    if (ActionEventsCompiled() && g_local_actions_valid && now_ms >= g_local_actions_ms
        && now_ms - g_local_actions_ms <= partner_actions::kNetworkAgeMs) {
        partner_actions::Frame actions{kSession,static_cast<std::uint32_t>(epoch),++g_action_tx_seq,g_local_actions};
        std::uint8_t frame[partner_actions::kFrameSize];
        if (partner_actions::Encode(actions,frame) && Transmit(frame,sizeof(frame))
            && (!g_action_tx_log_ms || now_ms - g_action_tx_log_ms >= 1000)) {
            g_action_tx_log_ms = now_ms; char line[240]{};
            if (SUCCEEDED(StringCchPrintfA(line,ARRAYSIZE(line),"partner_actions=tx; mission=%u; generation=%u; burst_onsets=%u; reload_requests=%u; retained=%u; seq=%u; playback=off\r\n",
                actions.action.mission,actions.action.generation,actions.action.fire_count,actions.action.reload_count,unsigned(actions.action.count),actions.sequence))) Log(line);
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
    if (sync::encode_look_frame(kSession, static_cast<std::uint32_t>(epoch), packet, frame)) sent = Transmit(frame, sizeof(frame));
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
    // The sequence keeps counting across link sessions: the peer's receiver,
    // reset at the new session, accepts any first sequence.
    const LONG epoch = g_epoch;
    if (!epoch) return false;
    const DWORD error = GetLastError();
    const std::uint64_t now = NowMs();
    if (g_input_tx_ms && now - g_input_tx_ms < kInputMinGapMs) { SetLastError(error); return false; }
    const std::uint32_t seq = g_input_tx_seq + 1;
    // v2, or v4 when the sender runs the native drive (packet.version 4: rates and magazine).
    std::uint8_t frame[sync::input_frame_size_v4];
    bool sent = false;
    if (const std::size_t n = sync::encode_input_frame_any(kSession, static_cast<std::uint32_t>(epoch), seq, packet, frame))
        sent = Transmit(frame, n);
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
    pose::Sample sample = g_ring.SampleAt(now, delay_ms);
    ReleaseSRWLockShared(&g_lock);
    // Link v2 freeze: a link that is not connected holds the last pose as lost.
    if (g_v2 && sample.valid && static_cast<link::LinkState>(g_link_state) != link::LinkState::connected)
        sample.status = pose::Status::lost;
    return sample;
}

pose::RemoteFrame SampleRemoteWithTurns(std::uint32_t delay_ms, const pose::HistoryCursor& cursor) noexcept {
    AcquireSRWLockShared(&g_lock);
    const std::uint64_t now = NowMs();
    pose::RemoteFrame frame = g_ring.ReadFrame(now, delay_ms, cursor);
    ReleaseSRWLockShared(&g_lock);
    if (g_v2 && frame.visual.valid && static_cast<link::LinkState>(g_link_state) != link::LinkState::connected)
        frame.visual.status = pose::Status::lost;
    return frame;
}

void ResetRemote() noexcept {
    ClearLocalActions();
    AcquireSRWLockExclusive(&g_lock);
    ResetReceiveLocked();
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

const char* LinkStateName(LinkState state) noexcept { return link::LinkStateName(state); }

LinkInfo GetLinkInfo() noexcept {
    LinkInfo info{};
    for (int attempt = 0; attempt < 8; ++attempt) {
        const LONG before = g_info_seq;
        if (before & 1) { YieldProcessor(); continue; }
        info.state = static_cast<LinkState>(g_info_state);
        info.session = static_cast<std::uint32_t>(g_info_session);
        info.rtt_ms = static_cast<std::uint32_t>(g_info_rtt);
        info.jitter_ms = static_cast<std::uint32_t>(g_info_jitter);
        info.interp_ms = static_cast<std::uint32_t>(g_info_interp);
        info.loss_permille = static_cast<std::uint32_t>(g_info_loss);
        MemoryBarrier();
        if (g_info_seq == before) return info;
    }
    info.state = static_cast<LinkState>(g_link_state);  // writer busy: the state alone
    return info;
}

std::uint32_t EffectiveInterpMs(std::uint32_t configured) noexcept {
    return g_v2 && g_config.interp_auto ? static_cast<std::uint32_t>(g_applied_interp) : configured;
}

void PublishPartnerState(bool shown, bool look_loaded, std::uint32_t mission) noexcept {
    InterlockedExchange(&g_partner_shown, shown ? 1 : 0);
    InterlockedExchange(&g_partner_look, look_loaded ? 1 : 0);
    InterlockedExchange(&g_partner_mission, static_cast<LONG>(mission));
}

link::ReliableResult SendReliable(std::uint8_t channel, const void* bytes, std::size_t size, link::ReliableMode mode) noexcept {
    if (channel < 1 || channel >= link::kReliableChannels) return link::ReliableResult::bad_channel;
    if (size > link::kMaxReliableMessage) return link::ReliableResult::too_large;
    if (!g_v2 || !g_running) return link::ReliableResult::not_connected;
    AcquireSRWLockExclusive(&g_tx_lock);
    const link::ReliableResult result = g_session.CanSendData() ? g_rel_tx[channel].Queue(bytes, size, mode)
                                                                : link::ReliableResult::not_connected;
    ReleaseSRWLockExclusive(&g_tx_lock);
    return result;
}

void SetWorldHooks(const WorldHooks* hooks) noexcept { g_world = hooks ? *hooks : WorldHooks{}; }

link::ReliableResult SendDatagram(std::uint8_t channel, const void* bytes, std::size_t size) noexcept {
    if (channel < 1 || channel >= link::kReliableChannels) return link::ReliableResult::bad_channel;
    if (size > link::kMaxDatagramPayload) return link::ReliableResult::too_large;
    if (!g_v2 || !g_running) return link::ReliableResult::not_connected;
    std::uint8_t message[link::kDatagramHeaderSize + link::kMaxDatagramPayload];
    const std::size_t length = link::EncodeDatagram(channel, bytes, size, message, sizeof(message));
    if (!length) return link::ReliableResult::too_large;
    const DWORD error = GetLastError();
    const std::uint64_t now = NowMs();
    bool sent = false;
    AcquireSRWLockExclusive(&g_tx_lock);
    if (g_session.CanSendData()) {
        link::RelAckState acks[link::kReliableChannels];
        TakeRelAcks(acks, now, 0);
        link::BodyWriter writer(g_body, sizeof(g_body));
        std::size_t no_retransmits = 0;
        if (link::PackBodyWith(writer, link::MsgType::datagram, message, length, acks, g_rel_tx, now, g_session.rtt().rto_ms(),
                               no_retransmits))
            sent = g_session.SendData(g_body, writer.size(), now);
    }
    ReleaseSRWLockExclusive(&g_tx_lock);
    SetLastError(error);
    return sent ? link::ReliableResult::queued : link::ReliableResult::not_connected;
}

bool DrainReliable(std::uint8_t channel, std::uint8_t* out, std::size_t capacity, std::size_t& size) noexcept {
    size = 0;
    if (channel < 1 || channel >= link::kReliableChannels || !g_v2) return false;
    AcquireSRWLockExclusive(&g_lock);
    const bool drained = g_rel_rx[channel].Drain(out, capacity, size, NowMs());
    if (drained) InterlockedExchange(&g_relack_dirty, 1);
    ReleaseSRWLockExclusive(&g_lock);
    return drained;
}

void Shutdown() noexcept {
    if (InterlockedExchange(&g_shutdown, 1)) return;
    if (!g_use_steam) return;  // UDP: unchanged
    g_steam.request_shutdown();
    AcquireSRWLockExclusive(&g_secret_lock);
    link::SecureZero(g_host_secret, sizeof(g_host_secret));
    g_host_secret_valid = false;
    ReleaseSRWLockExclusive(&g_secret_lock);
}

SteamInfo GetSteamInfo() noexcept {
    SteamInfo info{};
    info.active = g_use_steam && g_running;
    if (!info.active) return info;
    info.ready = g_steam_ready != 0 && g_steam.ready() && !g_steam.faulted();
    info.host = g_config.host;
    info.waiting = g_steam_waiting != 0;
    info.accepting = g_config.host && g_steam.accepting();
    info.beacon_wanted = g_beacon_wanted != 0;
    info.route = g_steam.route();
    info.steam_state = g_steam.steam_state();
    info.peer = g_config.host ? g_steam.peer_id() : static_cast<std::uint64_t>(InterlockedCompareExchange64(&g_steam_host, 0, 0));
    info.self = g_config.steam_self;
    if (g_steam_failed) StringCchCopyA(info.reason, ARRAYSIZE(info.reason), g_steam_reason);
    else if (g_steam.faulted()) StringCchCopyA(info.reason, ARRAYSIZE(info.reason), "fault");
    info.dispatch = g_steam.dispatch_state();
    StringCchCopyA(info.dispatch_reason, ARRAYSIZE(info.dispatch_reason), g_steam.dispatch_reason());
    info.steam_end = g_steam_end;
    info.left = g_peer_left != 0;
    AcquireSRWLockShared(&g_secret_lock);
    info.code_ready = g_config.host && g_host_secret_valid && info.ready && !g_shutdown;
    ReleaseSRWLockShared(&g_secret_lock);
    return info;
}

void SetSteamAccepting(bool accepting) noexcept {
    if (g_use_steam && g_config.host) g_steam.set_accepting(accepting);
}

void AddSteamKnown(std::uint64_t id, lab::KnownKind kind) noexcept {
    if (g_use_steam && g_config.host && g_running) g_steam.add_known(id, kind);
}

namespace {
bool BuildCode(char* out, std::size_t capacity, bool invite) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    if (!g_use_steam || !g_config.host || g_shutdown || !g_steam.ready()) return false;
    bool ok = false;
    AcquireSRWLockShared(&g_secret_lock);
    if (g_host_secret_valid)
        ok = invite ? steam_code::FormatInvite(g_host_secret, g_config.steam_self, out, capacity)
                    : steam_code::Format(g_host_secret, g_config.steam_self, out, capacity);
    ReleaseSRWLockShared(&g_secret_lock);
    return ok;
}
} // namespace

bool BuildSteamJoinCode(char* out, std::size_t capacity) noexcept { return BuildCode(out, capacity, false); }
bool BuildSteamInvite(char* out, std::size_t capacity) noexcept { return BuildCode(out, capacity, true); }

bool PostSteamJoin(const char* text) noexcept {
    if (!g_use_steam || g_config.host || !g_running || !text) return false;
    std::size_t n = 0;
    while (text[n] && n < sizeof(g_paste)) ++n;
    if (!n || n >= sizeof(g_paste)) return false;
    AcquireSRWLockExclusive(&g_paste_lock);
    for (std::size_t i = 0; i < sizeof(g_paste); ++i) g_paste[i] = i < n ? text[i] : 0;
    g_paste_pending = true;
    ReleaseSRWLockExclusive(&g_paste_lock);
    return true;
}

} // namespace mgscoop::partner_net
