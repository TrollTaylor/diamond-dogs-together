#pragma once
// Offline partner spike netcode (docs/offline-partner-spike-plan.md 2.3 item 7
// and 2.6): our own second lab::UdpTransport on 127.0.0.1 (lab.ini
// net_listen_port / net_peer_port, default 47820/47821), carrying 56-byte
// MGSC transform frames (player_sync fixed-buffer codec) and 28-byte
// keepalives. Completely separate from the Steam loopback emulator and from
// every game session API.
//
// Threads: Start/SubmitLocal/SampleRemote/ResetRemote/SetEpoch run on the
// game's Lua runtime thread inside the observer's receipt lock (lock order:
// receipt lock, then the net lock). The transport's receive thread only
// decodes into the snapshot ring under the exclusive net lock and never
// touches game memory. Nothing here allocates after Start (except the
// transport's own delay queue when net_latency_ms is set); all state is
// constant-initialized POD (no destructor runs at unload).
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_input.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/pose_interp.hpp"

namespace mgscoop::partner_net {

// Lab-only session token in every frame ("MGSCOOP" + 0x01).
inline constexpr std::uint64_t kSession = 0x4D4753434F4F5001ULL;
// A sequence that is not newer is accepted as a peer restart after this gap.
inline constexpr std::uint32_t kRestartGapMs = 2000;
inline constexpr std::uint32_t kKeepaliveAfterMs = 1000;
inline constexpr std::uint32_t kStatsIntervalMs = 10000;

using LogFn = void (*)(const char* line) noexcept;

struct Config {
    std::uint16_t listen_port;
    std::uint16_t peer_port;
    std::uint32_t rate_hz;       // 1..60
    std::uint32_t latency_ms;    // lab.ini net_latency_ms (fault injection)
    std::uint32_t loss_permille; // lab.ini net_loss_permille
    LogFn log;
};

// Starts once (INIT_ONCE); never call from DllMain. Logs
// "partner_net=listening; port=%u; peer_port=%u" or
// "partner_net=disabled; reason=transport_<bind|winsock|socket|thread|bad_config>".
// Returns true while the transport runs.
bool Start(const Config& config) noexcept;
bool Running() noexcept;
// Current mission (frame epoch). 0 drops every received transform.
void SetEpoch(std::uint32_t epoch) noexcept;
// Sends the local pose when the rate interval has elapsed (non-blocking):
// a 64-byte v2 frame with the anim extension when anim.flags has
// sync::AnimFlag::Valid, else a 56-byte v1 frame. Returns true when a frame
// was handed to the transport.
bool SubmitLocal(const partner::Tick& tick, const sync::AnimState& anim) noexcept;
// Interpolated remote pose at now (QPC milliseconds) with the given delay.
pose::Sample SampleRemote(std::uint32_t delay_ms) noexcept;

// Appearance frames (40 bytes, kind event). The local look is sent at once
// when it differs from the last one sent (the revision bumps; never 0), then
// repeated every kLookHeartbeatMs (UDP loss, late peers). Only call it with a
// look read while the local parts state is loaded (3).
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

// Input frames (round 3, 68 bytes, kind event): SubmitInput sends at once
// (min gap 8 ms) with a running sequence >= 1. The receive path decodes the
// frame in OnDatagram (between look and transform), stores it for the current
// epoch, and logs partner_net=input_rx; first=1 once per epoch.
inline constexpr std::uint32_t kInputMinGapMs = 8;
bool SubmitInput(const partner_input::InputPacket& packet) noexcept;
// The newest received input (non-blocking): false when the lock is busy (the
// caller keeps its previous sample). age_ms = NowMs() - rx_ms.
bool LatestInput(partner_input::Sample& out) noexcept;
// Clears the ring, the sequence state and the received look, and makes the
// next SubmitLook send at once (mission boundaries).
void ResetRemote() noexcept;
// Per-tick native cost of the observer's tick handler, for the stats line.
void RecordNativeMicros(std::uint32_t micros) noexcept;
// QPC milliseconds (the clock used for tick_ms and receive times).
std::uint64_t NowMs() noexcept;

} // namespace mgscoop::partner_net
