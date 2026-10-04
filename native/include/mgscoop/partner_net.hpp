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
// Sends the local pose when the rate interval has elapsed (non-blocking).
// Returns true when a frame was handed to the transport.
bool SubmitLocal(const partner::Tick& tick) noexcept;
// Interpolated remote pose at now (QPC milliseconds) with the given delay.
pose::Sample SampleRemote(std::uint32_t delay_ms) noexcept;
// Clears the ring and the sequence state (mission boundaries).
void ResetRemote() noexcept;
// Per-tick native cost of the observer's tick handler, for the stats line.
void RecordNativeMicros(std::uint32_t micros) noexcept;
// QPC milliseconds (the clock used for tick_ms and receive times).
std::uint64_t NowMs() noexcept;

} // namespace mgscoop::partner_net
