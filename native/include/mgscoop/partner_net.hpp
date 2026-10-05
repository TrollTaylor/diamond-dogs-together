#pragma once
// Offline partner spike netcode (docs/offline-partner-spike-plan.md 2.3 item 7
// and 2.6): our own second lab::UdpTransport (lab.ini net_listen_port /
// net_peer_port, default 47820/47821 on 127.0.0.1), carrying 56-byte MGSC
// transform frames (player_sync fixed-buffer codec) and the look and input
// frames. Completely separate from the Steam loopback emulator and from every
// game session API.
//
// Two wire paths (lab.ini net_link):
// - v2 (default; docs/link-protocol.md): every MGSC frame travels unchanged as
//   a FRAME message inside an authenticated link v2 DATA packet after a
//   pre-shared-key handshake (link_session), plus the reliable ordered
//   channels. The same code runs the one-PC lab (127.0.0.1, the lab secret)
//   and two-PC play (0.0.0.0, link-secret.txt).
// - legacy (lab only): the raw frames and 28-byte keepalives.
//
// Threads: Start/SubmitLocal/SampleRemote/ResetRemote/SetEpoch run on the
// game's Lua runtime thread inside the observer's receipt lock. Lock order:
// receipt lock, then g_tx_lock (link session and send state), then g_lock
// (receive state); the receive thread never takes g_tx_lock while it holds
// g_lock, and nothing logs or touches files under either lock. The
// transport's receive thread only decodes into the snapshot ring and never
// touches game memory. Nothing here allocates after Start (except the
// transport's own delay heap under lab impairment); all state is
// constant-initialized POD (no destructor runs at unload).
#include <cstddef>
#include <cstdint>
#include "mgscoop/link_protocol.hpp"
#include "mgscoop/link_session.hpp"
#include "mgscoop/partner_input.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/pose_interp.hpp"
#include "mgscoop/udp_transport.hpp"

namespace mgscoop::partner_net {

// Lab-only session token in every frame ("MGSCOOP" + 0x01).
inline constexpr std::uint64_t kSession = 0x4D4753434F4F5001ULL;
// A sequence that is not newer is accepted as a peer restart after this gap.
inline constexpr std::uint32_t kRestartGapMs = 2000;
// Input frames are stored only when newer (wrap-safe, as newer_sequence) than
// the stored one, or after kRestartGapMs without a stored frame (the peer
// restarted its counter): an out-of-order older frame never replaces a newer
// one or re-delivers its edge words (counted as in_drop).
inline bool AcceptInputSequence(bool have, std::uint32_t last_seq, std::uint64_t last_rx_ms, std::uint32_t seq,
                                std::uint64_t now_ms) noexcept {
    if (!have) return true;
    const std::uint32_t delta = seq - last_seq;
    if (delta != 0 && delta < 0x80000000u) return true;
    return now_ms >= last_rx_ms && now_ms - last_rx_ms > kRestartGapMs;
}
inline constexpr std::uint32_t kKeepaliveAfterMs = 1000;
inline constexpr std::uint32_t kStatsIntervalMs = 10000;
// Link v2 receive-thread cadence and the status file.
inline constexpr std::uint32_t kLinkIdleMs = 20;
inline constexpr std::uint32_t kStatusCheckMs = 1000;
inline constexpr std::uint32_t kStatusRefreshMs = 5000;

using LogFn = void (*)(const char* line) noexcept;

struct Config {
    std::uint16_t listen_port;
    std::uint16_t peer_port;
    std::uint32_t rate_hz;       // 1..60
    std::uint32_t latency_ms;    // lab.ini net_latency_ms (fault injection)
    std::uint32_t loss_permille; // lab.ini net_loss_permille
    LogFn log;
    // Link v2. A caller that names only the fields above keeps the raw-frame
    // (legacy) path; the observer fills these from lab.ini.
    bool link_v2 = false;
    bool play = false;
    bool host = true;
    bool bind_loopback = true;
    std::uint32_t bind_address = 0x7F000001;  // host byte order; 0 = any
    std::uint32_t peer_address = 0x7F000001;  // guest target
    lab::Impairment impair{};                 // jitter / dup / reorder / burst / seed (latency and loss above)
    bool interp_auto = false;
    std::uint32_t interp_ms = 100;            // partner_interp_ms (fixed mode; reported in status)
    std::uint32_t interp_min_ms = 60;
    std::uint32_t interp_max_ms = 350;
    bool saves_redirected = false;            // lab::SavesRedirected(), for link-status.json
    const wchar_t* directory = nullptr;       // game folder (link-secret.txt, link-status.json)
    const char* build = nullptr;              // this loader's version (HELLO/WELCOME build field)
};

// Starts once (INIT_ONCE); never call from DllMain. Logs
// "partner_net=listening; port=%u; peer_port=%u" or
// "partner_net=disabled; reason=transport_<bind|winsock|socket|thread|bad_config>",
// and with link v2 the link= lines (docs/link-protocol.md "Log lines").
// Returns true while the transport runs.
bool Start(const Config& config) noexcept;
bool Running() noexcept;
// Current mission (frame epoch). 0 drops every received transform.
void SetEpoch(std::uint32_t epoch) noexcept;
// Sends the local pose when the rate interval has elapsed (non-blocking):
// a 64-byte v2 frame with the anim extension when anim.flags has
// sync::AnimFlag::Valid, else a 56-byte v1 frame. Returns true when a frame
// was handed to the transport (link v2: false while not connected).
bool SubmitLocal(const partner::Tick& tick, const sync::AnimState& anim) noexcept;
// Interpolated remote pose at now (QPC milliseconds) with the given delay.
// Link v2: while the link is not connected a held pose reports status lost
// (partner_lost=freeze holds it, forces dm speed 0 and stays realized).
pose::Sample SampleRemote(std::uint32_t delay_ms) noexcept;

// Appearance frames (40 bytes, kind event). The local look is sent at once
// when it differs from the last one sent (the revision bumps; never 0), then
// repeated every kLookHeartbeatMs (UDP loss, late peers). Only call it with a
// look read while the local parts state is loaded (3). Link v2 keeps this a
// FRAME with the same heartbeat (never a reliable message).
inline constexpr std::uint32_t kLookHeartbeatMs = 2000;
bool SubmitLook(const partner::Look& look) noexcept;
// The newest look received in the current epoch (raw values; the driver
// normalizes them). Logged on change only from the receive thread:
// "partner_net=look_rx; type=..; parts=..; camo=..; hand=..; face=..; fe=..; rev=..".
struct RemoteLook {
    bool valid;
    partner::Look look;
    std::uint16_t revision;
    std::uint64_t rx_ms;  // QPC milliseconds of the newest look frame
};
RemoteLook SampleLook() noexcept;

// Input frames (round 3/4, kind event; v2 76 bytes sent, v1 68 bytes still
// accepted): SubmitInput sends at once (min gap 8 ms) with a running sequence
// >= 1. The receive path decodes the frame (between look and transform),
// stores its level fields for the current epoch, accumulates its edge set
// (InputEdges), and logs partner_net=input_rx; first=1 once per epoch.
inline constexpr std::uint32_t kInputMinGapMs = 8;
bool SubmitInput(const partner_input::InputPacket& packet) noexcept;
// Edge sets of accepted frames are accumulated (partner_input::AccumulateEdges)
// until ConsumeInput takes them.
// The newest received input (non-blocking): false when the lock is busy (the
// caller keeps its previous sample). age_ms = NowMs() - rx_ms. Read-only: the
// edge fields stay 0 (Lua-side stats and probes).
bool LatestInput(partner_input::Sample& out) noexcept;
// LatestInput plus the edges accumulated since the previous call, which it
// clears (the input window's sample; false when the lock is busy, and then
// nothing is consumed).
bool ConsumeInput(partner_input::Sample& out) noexcept;
// Clears the ring, the sequence state and the received look, and makes the
// next SubmitLook send at once (mission boundaries). A new link session does
// the receive half of this on the receive thread and asks the game thread
// (through an atomic flag) to restart its input sequence.
void ResetRemote() noexcept;
// Per-tick native cost of the observer's tick handler, for the stats line.
void RecordNativeMicros(std::uint32_t micros) noexcept;
// QPC milliseconds (the clock used for tick_ms and receive times).
std::uint64_t NowMs() noexcept;

// --- Link v2 state (docs/link-protocol.md).
using LinkState = link::LinkState;
const char* LinkStateName(LinkState state) noexcept;
struct LinkInfo {
    LinkState state;
    std::uint32_t session, rtt_ms, jitter_ms, interp_ms, loss_permille;
};
// Game-thread snapshot (seqlock read; never blocks). state off without v2.
LinkInfo GetLinkInfo() noexcept;
// partner_interp=auto: the applied adaptive delay; otherwise `configured`.
std::uint32_t EffectiveInterpMs(std::uint32_t configured) noexcept;
// Game thread, atomics only: what link-status.json reports as partner_shown,
// look and mission.
void PublishPartnerState(bool shown, bool look_loaded, std::uint32_t mission) noexcept;
// Reliable ordered channels 1..3 (1 = world, reserved; 2..3 = game). Session
// scoped: a new handshake clears them on both sides (LinkInfo.session bumps)
// and callers send their state again. On `full` the caller keeps its dirty
// flag and retries next tick. Never blocks on I/O, never allocates.
link::ReliableResult SendReliable(std::uint8_t channel, const void* bytes, std::size_t size, link::ReliableMode mode) noexcept;
bool DrainReliable(std::uint8_t channel, std::uint8_t* out, std::size_t capacity, std::size_t& size) noexcept;

} // namespace mgscoop::partner_net
