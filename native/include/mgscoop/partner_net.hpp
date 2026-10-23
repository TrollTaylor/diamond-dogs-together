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
#include "mgscoop/partner_action_events.hpp"
#include "mgscoop/partner_receipt.hpp"
#include "mgscoop/pose_interp.hpp"
#include "mgscoop/coop_hud.hpp"
#include "mgscoop/steam_transport.hpp"
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

// The link's transport (docs/steam-online.md 8.1): our UDP socket (the
// one-PC lab and the LAN/VPN code) or lab::SteamTransport (Steam online,
// play only). Nothing switches transports automatically.
enum class Transport : std::uint8_t { udp, steam };

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
    // lab.ini link_session_skew (the lab and the worker's play rehearsal only;
    // play forces 0): the link-session counter starts there (SessionConfig).
    std::uint32_t session_skew = 0;
    bool interp_auto = false;
    std::uint32_t interp_ms = 100;            // partner_interp_ms (fixed mode; reported in status)
    std::uint32_t interp_min_ms = 60;
    std::uint32_t interp_max_ms = 350;
    bool saves_redirected = false;            // lab::SavesRedirected(), for link-status.json
    const wchar_t* directory = nullptr;       // game folder (link-secret.txt, link-status.json)
    const char* build = nullptr;              // this loader's version (HELLO/WELCOME build field)
    // Steam online (play only; docs/steam-online.md 8). The session runs with
    // exact_filter=true; the guest's host is Endpoint{account, 0x5354}.
    Transport transport = Transport::udp;
    std::uint64_t steam_self = 0;             // net_steam_self (gate 5 checks it against GetSteamID)
    std::uint64_t steam_peer = 0;             // guest: the host's SteamID64
    bool steam_wait = false;                  // guest: no host yet (invite / F9 paste arms it)
    bool accepting = true;                    // host: net_steam_accept (F9 toggles it)
    lab::SteamResolveFn steam_resolve = nullptr;              // steam_online::Resolve
    void* steam_resolve_ctx = nullptr;
    // steam_cb::PopSessionRequest (host): 1251 through the Init wrapper and/or
    // the function pointer, with the source mask (docs/steam-online.md 20).
    bool (*steam_requests)(std::uint64_t& id, std::uint8_t& sources) noexcept = nullptr;
    // lab.ini net_steam_dispatch (SteamDispatch: 0 both, 1 fnptr, 2 callback,
    // 3 poll): the host installs 204+205, the guest 205 when it uses fnptr.
    std::uint8_t steam_dispatch = 0;
    // The in-game status line and toasts (coop_hud; lab.ini coop_hud, coop.ini
    // hud). The one-PC lab keeps it off unless coop_hud is set.
    coop_hud::Mode hud = coop_hud::Mode::off;
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
pose::RemoteFrame SampleRemoteWithTurns(std::uint32_t delay_ms, const pose::HistoryCursor& cursor) noexcept;

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
// The worker-only input stall (docs/anim-round2-throws.md "Stall test step"):
// SubmitInput sends nothing for `ms` (kInputStallMinMs..kInputStallMaxMs) from
// now, so the peer's newest input frame ages through stale_ms into its stall
// hold exactly as a real gap would; the sender's edge sets keep accumulating
// (SenderSent is not called) and go out with the first frame after the stall.
// Only the private worker's native debug command (input_stall <ms>) calls it,
// and only when MGS_BACKGROUND_WORKER is compiled in and the worker scenario is
// active; every other build refuses (the friend/Release loader never has the
// background worker). Lines: partner_net=input_stall; reason=start|end.
inline constexpr std::uint32_t kInputStallMinMs = 50, kInputStallMaxMs = 1000;
const char* StallInput(std::uint32_t ms) noexcept;   // nullptr: started; else the refusal
// The unknown remote slot/equip debounce (partner_input::HoldIdentity, applied
// to each accepted input frame before it is stored): the equip of the driver's
// last support verdict (0 = none; the partner driver publishes it every tick).
void SetSupportIdentity(std::uint16_t equip) noexcept;
// Default-off data transport only (MGS_LOCAL_ACTION_EVENTS). The observer
// authorizes a natural owner receipt before caching it. SubmitLocal sends this
// separate companion at the same rate gate as movement, never enables gun DM.
bool ActionEventsCompiled() noexcept;
bool SetLocalActions(const partner_actions::Payload& payload) noexcept;
void ClearLocalActions() noexcept;
struct ActionSample { partner_actions::Receipt receipt{}; std::uint32_t age_ms = 0; bool fresh = false; };
// Current mission comes from the owner's verified game state, not the packet.
// Samples consume history once; returns false when disabled/busy/stale/lost.
bool SampleActions(std::uint32_t expected_mission, ActionSample& out) noexcept;
// Edge sets of accepted frames are accumulated (partner_input::AccumulateEdges)
// until ConsumeInput takes them.
// The newest received input; always returns true. It waits for the receive
// thread's short exclusive section (like SampleRemote; the waits are counted in
// the stats line's latest_waits): its callers (the driver, the observer) took a
// busy lock for "no input" (out.valid false). age_ms = NowMs() - rx_ms.
// Read-only: the edge fields stay 0 (Lua-side stats and probes).
bool LatestInput(partner_input::Sample& out) noexcept;
// LatestInput plus the edges accumulated since the previous call, which it
// clears (the input window's sample; non-blocking: false when the lock is busy,
// and then nothing is consumed and the input hook keeps its previous sample).
bool ConsumeInput(partner_input::Sample& out) noexcept;
// True once the peer sent an accepted input frame v5 since the last receive
// reset (a new link session, ResetRemote): its build decodes v5. The native
// sender sends v5 only then (plus probes); an older build refuses 116 bytes.
bool PeerSendsInputV5() noexcept;
bool PeerSendsInputV6() noexcept;
// The same for input frame v7 (WS4 v2: the sender's C4 place word).
bool PeerSendsInputV7() noexcept;
// Clears the ring, the sequence state and the received look, and makes the
// next SubmitLook send at once (mission boundaries). A new link session does
// the receive half of this on the receive thread, in the same g_tx_lock
// section as the commit; the send sequences keep counting (the peer's reset
// receivers accept any first sequence).
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
    // The peer's HELLO/WELCOME build ("0.13.0+c07d0a2", NUL-terminated; ""
    // before a handshake). The save handoff gates channel 4 on it (P2-4).
    char remote_build[link::kBuildSize + 1];
};
// Game-thread snapshot (seqlock read; never blocks). state off without v2.
LinkInfo GetLinkInfo() noexcept;
// partner_interp=auto: the applied adaptive delay; otherwise `configured`.
std::uint32_t EffectiveInterpMs(std::uint32_t configured) noexcept;
// Game thread, atomics only: what link-status.json reports as partner_shown,
// look and mission.
void PublishPartnerState(bool shown, bool look_loaded, std::uint32_t mission) noexcept;
// Game thread, atomics only (online play, docs/native-puppet.md "Online"):
// link-status.json partner_drive / partner_reason (how this copy shows the
// partner body) and remote_drive / remote_reason (how the other PC shows
// ours, from the capability exchange), as partner_native_net::Drive values
// and partner_native_net reason codes.
void PublishNativeState(std::uint8_t drive, std::uint8_t reason, std::uint8_t remote_drive, std::uint8_t remote_reason) noexcept;
// Game thread, atomics only: the partner mission whose pack list the player
// package selection refused (0 none; docs/player-package-selection.md). While
// it equals the published partner mission, link-status.json reports
// partner_drive "off" and partner_reason "no_body_pack" (no partner body).
void PublishPackState(std::uint32_t refused_mission) noexcept;
// World sync as link-status.json, the HUD and F9 report it (world_link
// publishes it; docs/link-protocol.md 7.2): the local world level 0..3,
// paired, the partner's mission (0 unknown), the published pair-wait code
// (world_wire pair_wait; 11 settling) and the title progress (0 unknown,
// 1 ok, 2 story_start). Any thread, atomics only, never blocks.
struct WorldView {
    std::uint8_t level;
    bool paired;
    std::uint32_t partner_mission;
    std::uint8_t pair_wait;
    std::uint8_t progress;
};
void PublishWorldState(const WorldView& view) noexcept;
// Reliable ordered channels 1..4 (1 = world; 2..3 = game; 4 = save handoff,
// sent and drained only by save_handoff.cpp through HandoffHooks). Session
// scoped: a new handshake clears them on both sides (LinkInfo.session bumps)
// and callers send their state again. On `full` the caller keeps its dirty
// flag and retries next tick. Never blocks on I/O, never allocates.
link::ReliableResult SendReliable(std::uint8_t channel, const void* bytes, std::size_t size, link::ReliableMode mode) noexcept;
bool DrainReliable(std::uint8_t channel, std::uint8_t* out, std::size_t capacity, std::size_t& size) noexcept;
// Actual authenticated queue provenance, captured with its payload under
// tx -> rx locks. `incarnation` never reuses Session::Start's local counter.
// `peer` is the endpoint at the authenticated commit, retained across valid
// same-key address rebinding. It is not reconstructed from GetLinkInfo.
// acceptedTick is GetTickCount64 at actual stored insertion; NowMs is QPC
// and is never converted or used to freshen queued vehicle messages.
struct ReliableOrigin {
    std::uint64_t incarnation=0, acceptedTick=0;
    std::uint32_t session=0;
    lab::Endpoint peer{};
    std::uint16_t message=0;
    std::uint8_t localRole=0, peerRole=1;
};
bool CurrentReliableOrigin(ReliableOrigin&) noexcept; // real current connected session + queue
// Compare the native producer's expected incarnation under the same actual
// tx -> rx locks used for enqueue; no read/send race can transplant an old
// mount into a newly committed channel. Existing SendReliable is unchanged.
link::ReliableResult SendReliableForOrigin(const ReliableOrigin&,std::uint8_t channel,const void* bytes,
                                        std::size_t size,link::ReliableMode) noexcept;
bool DrainReliableAuthenticated(std::uint8_t channel,std::uint8_t* out,std::size_t capacity,
                                std::size_t& size,ReliableOrigin&) noexcept;


// World sync (docs/world-sync-protocol.md). All callbacks run on the transport's
// receive thread with no partner_net lock held; null hooks = today's behaviour.
struct WorldHooks {
    void (*on_datagram)(const std::uint8_t* payload, std::size_t size) noexcept; // DATAGRAM channel 1
    void (*on_idle)(std::uint64_t now_ms) noexcept;      // end of every LinkIdle (~20 ms)
    void (*on_session)(std::uint32_t session) noexcept;  // after a new session committed
};
void SetWorldHooks(const WorldHooks* hooks) noexcept;     // install time, before Start
// One DATA packet: the datagram first, then due RELACKs and new reliable data.
// Game or receive thread; not_connected / too_large / bad_channel as SendReliable.
link::ReliableResult SendDatagram(std::uint8_t channel, const void* bytes, std::size_t size) noexcept;

// Automatic save handoff (docs/save-handoff.md; join-fix phase 2 P2-12). Only
// save_handoff.cpp sends on reliable channel 4 or drains it, from these hooks.
// All callbacks run on the receive or transport thread with no partner_net
// lock held; null hooks = today's behaviour.
struct HandoffHooks {
    void (*on_idle)(std::uint64_t now_ms) noexcept;      // after g_world.on_idle at the end of LinkIdle
    void (*on_session)(std::uint32_t session) noexcept;  // after each g_world.on_session (new session, ArmSteamGuest)
    void (*on_peer_close)() noexcept;                    // after the CLOSE handling, outside the locks
};
void SetHandoffHooks(const HandoffHooks* hooks) noexcept;  // install time, before Start
// Messages queued and not yet acknowledged on a reliable channel (g_tx_lock);
// 0 for an unknown channel or before Start.
unsigned ReliableQueued(std::uint8_t channel) noexcept;
// Guest: the MGS2 join code it was armed with (secret-bearing: the caller
// clears its buffer). True only for a Steam guest that started waiting
// (net_steam_wait) and was then armed by an invite or an F9 paste; zeroed at
// shutdown.
bool BuildGuestJoinCode(char* out, std::size_t capacity) noexcept;
// link-status.json "handoff" (save_handoff_state::Token) and "handoff_pct"
// (0..100), also copied into the HUD LinkView. Any thread, atomics only.
void PublishHandoffState(std::uint8_t token, std::uint8_t pct) noexcept;

// --- Steam online (docs/steam-online.md 7.6, 8.2, 8.3, 10). Inert on UDP.
// Window-close shutdown flag (any thread, flags only): the Steam transport
// sends one link CLOSE, closes the peer session and makes no further Steam
// call; the host's retained secret is zeroed.
void Shutdown() noexcept;
struct SteamInfo {
    bool active;            // transport=steam and started
    bool ready;             // the Steam API resolved; the transport thread runs
    bool host;
    bool waiting;           // guest without a host
    bool accepting;
    bool beacon_wanted;     // guest: connecting > 10 s without WELCOME (rich-presence beacon)
    bool code_ready;        // host: a join code can be built
    steam_net::Route route;
    std::int32_t steam_state;
    std::uint64_t peer;     // the session peer (host) or the configured host (guest); 0 none
    std::uint64_t self;
    char reason[32];        // "" or the steam=disabled reason
    // Section 20: the function pointer (off|on|refused) and its reason, the
    // guest's last 1252 end reason (0 none) and the partner's goodbye.
    lab::DispatchState dispatch;
    char dispatch_reason[24];
    std::int32_t steam_end;
    bool left;
    // Guest: ArmSteamGuest bumps join_seq on every pasted-code or invite
    // decision; join_result is that decision (0 none, 1 joined, 2 ignored
    // because already connected or connecting, 3 the own code). Copied even
    // when Steam is not active (steam_online's F9 Online result).
    std::uint32_t join_seq{};
    std::uint8_t join_result{};
};
SteamInfo GetSteamInfo() noexcept;
// Host: the F9 Accept joins toggle (atomic; the transport reads it).
void SetSteamAccepting(bool accepting) noexcept;
// Host: an id to poll-accept (invited friend, beacon hit).
void AddSteamKnown(std::uint64_t id, lab::KnownKind kind) noexcept;
// Host: the MGS2 join code / the "+mgscoop_join <code>" invite string from
// the retained secret (secret-bearing: the caller clears its buffer). False
// when not hosting on Steam, before the transport is ready or after Shutdown.
bool BuildSteamJoinCode(char* out, std::size_t capacity) noexcept;
bool BuildSteamInvite(char* out, std::size_t capacity) noexcept;
// Guest: a pasted MGS2 code (F9 Join); parsed and armed on the transport
// thread (ArmSteamGuest) only while waiting or rejected.
bool PostSteamJoin(const char* text) noexcept;

} // namespace mgscoop::partner_net
