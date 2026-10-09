#pragma once
// lab::SteamTransport (docs/steam-online.md section 7): link v2 datagrams over
// ISteamNetworkingMessages002 ("SteamNetworkingMessages002") on channel
// 0x4D47, or (self-test only) over a Sockets012 connection of an in-process
// socket pair. Steam signals, traverses NAT and falls back to Valve's relay
// network; link v2 above it is unchanged (handshake, HMAC, sessions, reliable
// channels, world datagrams).
//
// Split (C5): a core (bounds, Release exactly once, delivery, the send ring,
// idle cadence, shutdown, fault latch) and two legs:
//   messages:   SendMessageToUser(identity, data, n, 0x21, 0x4D47) /
//               ReceiveMessagesOnChannel(0x4D47, msgs, 32); the identity gate
//               maps an individual SteamID to Endpoint{account, 0x5354}. The
//               guest delivers only its configured host (C8); the host runs
//               the accept policy (6.3).
//   connection: SendMessageToConnection(h, data, n, 1, null) /
//               ReceiveMessagesOnConnection(h, msgs, 32); the identity must
//               be IP-localhost (type 1, size 18); the handle maps to a fixed
//               synthetic endpoint.
//
// Threads: transmit_to runs on any thread under the owner's tx lock (single
// producer) and only copies into the 64 x 1200-byte send ring; the
// transport's own thread is the only one that calls into steamclient (C9).
// Every interface call goes through a C-style __try/__except thunk: a fault
// latches the transport (no further Steam call) and reports reason=fault.
// Function-pointer dispatch (docs/steam-online.md section 20): with
// config.dispatch set, the thread sets config 204/205 once ready, calls
// Sockets012::RunCallbacks at least every kPumpMs between receive and accept
// work (a code join is accepted in the same pass) and resets them before it
// stops (stop() and the shutdown flag). A refused install leaves the link on
// the 1251 callback and polling.
// Constant-initializable and trivially destructible; nothing allocates after
// start(). The game thread never enters steamclient for networking.
#include <cstddef>
#include <cstdint>
#include "mgscoop/steam_net_abi.hpp"
#include "mgscoop/udp_transport.hpp"

namespace mgscoop::lab {

using SteamLogFn = void (*)(const char* line) noexcept;

// Interface pointers acquired (once, cached) in the owner context.
struct SteamNetApi {
    steam_net::ISteamNetworkingMessages002Abi* messages = nullptr;
    steam_net::ISteamNetworkingSockets012Abi* sockets = nullptr;
    steam_net::ISteamNetworkingUtils004Abi* utils = nullptr;
    // Fault latch (steam=disabled; reason=fault); called once from the thread
    // that caught the exception, after which that thread makes no Steam call.
    void (*on_fault)(void* ctx, const char* where) noexcept = nullptr;
    void* fault_ctx = nullptr;
    // The process-wide fault latch (another of our threads faulted inside
    // steamclient): when set, the transport parks without another call.
    const volatile long* halt = nullptr;
    // Section 20: the owner context's dispatch gate (Sockets012 slot 41 and
    // Utils004 slot 16 forwarders down to the inner RTTI, one per-user
    // object): nullptr = checked; otherwise why the function pointer is refused.
    const char* dispatch_refused = "not_checked";
};

// The API is acquired in the owner context, possibly after start(): the
// thread polls this until it is ready (1) or failed (-1); 0 = pending.
using SteamResolveFn = int (*)(void* ctx, SteamNetApi& out, const char*& reason) noexcept;

enum class SteamLeg : std::uint8_t { messages, connection };
enum class SteamRole : std::uint8_t { host, guest };

// Fixed synthetic endpoint of the connection leg (both ends see it).
inline constexpr Endpoint kConnectionEndpoint{0x7F535400u, steam_net::kSteamPortTag};
inline Endpoint SteamEndpoint(std::uint64_t id) noexcept { return Endpoint{steam_net::AccountOf(id), steam_net::kSteamPortTag}; }
inline std::uint64_t SteamIdOf(const Endpoint& e) noexcept { return steam_net::SteamIdFromAccount(e.address); }

// ---- Send ring (C9): 64 slots x 1200 bytes, single producer (serialized by
// the caller's tx lock), single consumer (the transport thread).
class SteamSendRing {
public:
    static constexpr unsigned kSlots = 64;
    // Copies the chunks; false (counted full) when the ring is full. Size 0 or
    // above kMaxMessage is refused by the caller before this.
    bool Push(const Endpoint& to, const WireChunk* chunks, unsigned count, std::size_t total) noexcept;
    // Consumer: the oldest datagram, or false. Release() after use.
    bool Peek(Endpoint& to, const std::uint8_t*& data, std::size_t& size) const noexcept;
    void Pop() noexcept;
    unsigned Depth() const noexcept;
    void Clear() noexcept;  // consumer side only (stop)

private:
    struct Slot {
        Endpoint to;
        std::uint32_t size;
        std::uint8_t bytes[steam_net::kMaxMessage];
    };
    Slot slots_[kSlots]{};
    volatile long head_ = 0;  // consumer
    volatile long tail_ = 0;  // producer
};

// ---- Host accept policy (6.3). Pure; the transport calls it on its thread.
enum class AcceptVia : std::uint8_t { callback, poll, beacon, fnptr };
const char* AcceptViaName(AcceptVia via) noexcept;
enum class KnownKind : std::uint8_t { guest, invited, beacon };
class SteamAcceptPolicy {
public:
    static constexpr unsigned kMaxNonPeer = 8;
    static constexpr unsigned kRatePerSec = 4;
    static constexpr unsigned kDenyEntries = 64;
    static constexpr unsigned kKnownEntries = 24;
    static constexpr unsigned kMaxInvited = 16;
    static constexpr std::uint64_t kNoHeaderCloseMs = 15000;
    static constexpr std::uint64_t kDenyMs = 60000;
    static constexpr std::uint64_t kFormerPeerIdleMs = 60000;
    static constexpr std::uint64_t kInvitedKeepMs = 600000;
    static constexpr std::uint64_t kBeaconKeepMs = 120000;
    static constexpr std::uint64_t kPollIntervalMs = 250;  // 4 Hz

    enum class Decision : std::uint8_t { accept, not_accepting, self, denied, rate, cap, known_peer };
    static const char* DecisionName(Decision d) noexcept;

    void Reset(std::uint64_t self) noexcept;
    // A 1251 request: may it be accepted now? (`known_peer`: the current or
    // last peer, always accepted, never counted in the cap.)
    Decision OnRequest(std::uint64_t id, bool accepting, std::uint64_t now_ms) noexcept;
    // A poll candidate the host chose itself (DuePolls: the peer, an invited
    // friend, a beacon hit): the refusals of OnRequest except the rate, which
    // is for unsolicited 1251 requests. Consumes no rate token, so a 4 Hz poll
    // of an invited friend who is not joining cannot use up the tokens and
    // starve the 1251 accept of a player joining with a code.
    Decision CheckKnown(std::uint64_t id, bool accepting, std::uint64_t now_ms) const noexcept;
    // AcceptSessionWithUser returned true for `id` (callback or poll).
    void OnAccepted(std::uint64_t id, std::uint64_t now_ms) noexcept;
    // A delivered message; `link` = it decodes as a link v2 header.
    void OnMessage(std::uint64_t id, bool link, std::uint64_t now_ms) noexcept;
    // A link session committed with `id` (it leaves the non-peer table).
    void SetPeer(std::uint64_t id, std::uint64_t now_ms) noexcept;
    std::uint64_t peer() const noexcept { return peer_; }
    std::uint64_t last_peer() const noexcept { return last_peer_; }
    bool Denied(std::uint64_t id, std::uint64_t now_ms) const noexcept;
    bool Accepted(std::uint64_t id) const noexcept;
    void AddKnown(std::uint64_t id, KnownKind kind, std::uint64_t now_ms) noexcept;
    // The ids to poll-accept now (4 Hz; the peer/last peer always, invited and
    // beacon ids only while accepting); returns the count.
    unsigned DuePolls(std::uint64_t now_ms, bool accepting, std::uint64_t* out, KnownKind* kinds, unsigned max) noexcept;
    // One identity to close now (non-peer without a link header for 15 s:
    // also denied for 60 s; a former peer idle 60 s); false when none.
    bool NextExpired(std::uint64_t now_ms, std::uint64_t& id) noexcept;
    unsigned non_peer() const noexcept;

private:
    struct Entry { bool used; std::uint64_t id, accepted_ms, last_rx_ms; bool link; };
    struct Deny { std::uint64_t id, until_ms; };
    struct Known { std::uint64_t id, expires_ms; KnownKind kind; };
    Entry* Find(std::uint64_t id) noexcept;
    const Entry* Find(std::uint64_t id) const noexcept;
    void DenyFor(std::uint64_t id, std::uint64_t now_ms) noexcept;
    std::uint64_t self_ = 0, peer_ = 0, last_peer_ = 0, peer_rx_ms_ = 0, last_peer_rx_ms_ = 0;
    Entry entries_[kMaxNonPeer]{};
    Deny deny_[kDenyEntries]{};
    unsigned deny_next_ = 0;
    Known known_[kKnownEntries]{};
    std::uint64_t rate_window_ms_ = 0;
    unsigned rate_count_ = 0;
    std::uint64_t next_poll_ms_ = 0;
};

// ---- Guest foreign filter (C8): only the configured host is delivered;
// another identity's third message within 10 s closes its session.
class SteamGuestFilter {
public:
    static constexpr unsigned kEntries = 8;
    static constexpr unsigned kCloseAfter = 3;
    static constexpr std::uint64_t kWindowMs = 10000;
    // True when `id` should get CloseSessionWithUser now.
    bool OnForeign(std::uint64_t id, std::uint64_t now_ms) noexcept;
    void Reset() noexcept;

private:
    struct Entry { std::uint64_t id, first_ms; unsigned count; };
    Entry entries_[kEntries]{};
    unsigned next_ = 0;
};

struct SteamTransportConfig {
    SteamRole role = SteamRole::host;
    SteamLeg leg = SteamLeg::messages;
    std::uint64_t self_id = 0;   // messages leg: never accepted or delivered as a peer
    std::uint64_t host_id = 0;   // guest: the configured host (0 = waiting, nothing delivered)
    std::uint32_t conn = 0;      // connection leg: this end's connection handle
    std::uint32_t idle_interval_ms = 20;
    std::uint32_t resolve_timeout_ms = 120000;
    bool accepting = true;
    bool high_res_timer = true;
    // Play: CheckPingDataUpToDate(1e10f) once on this thread when ready (the
    // SDK's InitRelayNetworkAccess; shortens the first connect). Skipped
    // without a Utils004 interface.
    bool warm_relay = false;
    SteamResolveFn resolve = nullptr;
    void* resolve_ctx = nullptr;
    // Host: the session-request queue (steam_cb::PopSessionRequest: 1251 from
    // the Init wrapper and/or the function pointer, with the source mask);
    // null = poll only.
    bool (*pop_request)(std::uint64_t& id, std::uint8_t& sources) noexcept = nullptr;
    // Messages leg: steam_dispatch install bits (1 = 204 session request, 2 =
    // 205 session failed); 0 = no function pointer (section 20).
    std::uint8_t dispatch = 0;
    SteamLogFn log = nullptr;    // steam= lines (transport thread, no lock held)
};

struct SteamSink {
    void (*on_datagram)(void* ctx, const std::uint8_t* data, std::size_t size, const Endpoint& from) noexcept = nullptr;
    void (*on_idle)(void* ctx) noexcept = nullptr;
    void (*on_ready)(void* ctx) noexcept = nullptr;
    void (*on_failed)(void* ctx, const char* reason) noexcept = nullptr;
    // Shutdown flag seen: the owner may transmit one last datagram (link CLOSE).
    void (*on_shutdown)(void* ctx) noexcept = nullptr;
    void* ctx = nullptr;
};

struct SteamTransportStats {
    std::uint64_t tx, rx, ring_full, send_fail, send_invalid, too_large, foreign, bad_identity, bad_size, denied, accepted,
        accept_refused, closed_idle, closed_foreign, release, received, faults;
    std::uint32_t non_peer, ring_depth;
    // Section 20: the second report of an accepted request (the other source
    // within kDupWindowMs), and the dispatch state.
    std::uint64_t accept_dup, pumps;
    std::uint8_t dispatch;        // DispatchState
};
// The function-pointer dispatch as the transport reports it.
enum class DispatchState : std::uint8_t { off, on, refused };
const char* DispatchStateName(DispatchState state) noexcept;

class SteamTransport {
public:
    static constexpr std::int32_t kReceiveBatch = 32;
    static constexpr unsigned kReceivePasses = 4;
    static constexpr std::uint32_t kWaitMs = 2;
    static constexpr std::uint64_t kRouteMs = 1000;
    static constexpr std::uint64_t kStatsMs = 10000;
    static constexpr std::uint64_t kSendFailLogMs = 10000;
    static constexpr std::uint32_t kResolvePollMs = 50;
    static constexpr std::uint32_t kCloseGraceMs = 30;  // shutdown: the CLOSE goes out before CloseSessionWithUser
    static constexpr std::uint64_t kPumpMs = 10;        // RunCallbacks cadence while a pointer is set (<= 50 ms required)
    static constexpr std::uint64_t kDupWindowMs = 5000; // a second source's report of an accept is not a new accept

    bool start(const SteamTransportConfig& config, const SteamSink& sink) noexcept;
    // Copies one datagram (<= 4 chunks, 1..1200 bytes) into the send ring.
    // True when queued or lost on a full ring (counted ring_full; link v2
    // recovers); false for size 0 / > 1200, a stopped transport or (messages
    // leg) an endpoint that is not a Steam endpoint.
    bool transmit_to(Endpoint to, const WireChunk* chunks, unsigned count, bool droppable = true) noexcept;
    bool transmit(const WireChunk* chunks, unsigned count, bool droppable = true) noexcept;
    // Stops and joins the thread (tests, self-test). Never called at game exit.
    void stop() noexcept;
    // Window-close shutdown flag (any thread, flag only): the thread sends the
    // owner's last datagram, closes the peer session and parks forever.
    void request_shutdown() noexcept;
    bool running() const noexcept { return running_ != 0; }
    bool ready() const noexcept { return ready_ != 0; }
    bool faulted() const noexcept { return faulted_ != 0; }
    TransportError error() const noexcept { return error_; }
    std::uint16_t local_port() const noexcept { return 0; }
    TransportStats stats() const noexcept;
    SteamTransportStats steam_stats() const noexcept;

    // Transport thread only (from the sink callbacks).
    void set_host(std::uint64_t id) noexcept;   // guest arm (waiting -> configured)
    // Host: the committed session peer (any thread; the accept policy takes it
    // on the transport thread).
    void set_peer(std::uint64_t id) noexcept;
    // Any thread.
    void set_accepting(bool accepting) noexcept { accepting_ = accepting ? 1 : 0; }
    bool accepting() const noexcept { return accepting_ != 0; }
    void add_known(std::uint64_t id, KnownKind kind) noexcept;
    steam_net::Route route() const noexcept { return static_cast<steam_net::Route>(route_); }
    std::int32_t steam_state() const noexcept { return steam_state_; }
    std::uint64_t peer_id() const noexcept { return static_cast<std::uint64_t>(peer_id_); }
    DispatchState dispatch_state() const noexcept { return static_cast<DispatchState>(dispatch_state_); }
    const char* dispatch_reason() const noexcept { return dispatch_reason_; }

private:
    static unsigned long __stdcall thread_entry(void* self) noexcept;
    void run() noexcept;
    bool resolve() noexcept;
    void drain_ring() noexcept;
    unsigned receive() noexcept;
    void deliver(void* message) noexcept;
    void accept_work(std::uint64_t now) noexcept;
    void route_poll(std::uint64_t now) noexcept;
    void hygiene(std::uint64_t now) noexcept;
    void shutdown_sequence() noexcept;
    void install_dispatch() noexcept;
    void pump_dispatch(std::uint64_t now) noexcept;
    void uninstall_dispatch() noexcept;
    void park() noexcept;
    void wait_idle() noexcept;
    void fault(const char* where) noexcept;
    void log_line(const char* text) noexcept;
    void log_stats(std::uint64_t now, bool force) noexcept;
    bool close_session(std::uint64_t id) noexcept;
    bool accept_session(std::uint64_t id) noexcept;
    // AcceptSessionWithUser returns true for ANY existing session, not only a
    // pending one (steamclient CSteamNetworkingMessages::AcceptSessionWithUser
    // returns 1 whenever the session has a connection and accepts only a
    // connecting one), so the 4 Hz poll of an established peer answers true
    // every 250 ms. Only a new session (no live mark yet) is counted and
    // logged as an accept. Returns the previous mark; transport thread only.
    bool mark_live(std::uint64_t id, bool live) noexcept;

    SteamTransportConfig config_{};
    SteamSink sink_{};
    SteamNetApi api_{};
    SteamSendRing ring_{};
    SteamAcceptPolicy policy_{};
    SteamGuestFilter filter_{};
    void* thread_ = nullptr;
    void* stop_event_ = nullptr;
    void* timer_ = nullptr;
    void* known_lock_ = nullptr;  // SRWLOCK storage (pointer-sized, zero = SRWLOCK_INIT)
    struct PendingKnown { std::uint64_t id; KnownKind kind; };
    PendingKnown known_pending_[16]{};
    unsigned known_count_ = 0;
    std::uint64_t pending_peer_ = 0;  // under known_lock_
    // Ids whose last accept (callback or poll) returned true (mark_live).
    static constexpr unsigned kLiveEntries = SteamAcceptPolicy::kKnownEntries + SteamAcceptPolicy::kMaxNonPeer + 2;
    std::uint64_t live_[kLiveEntries]{};
    unsigned live_next_ = 0;
    volatile long running_ = 0, ready_ = 0, faulted_ = 0, stop_ = 0, shutdown_ = 0, accepting_ = 1, parked_ = 0;
    volatile long route_ = 0, steam_state_ = 0, non_peer_ = 0, dispatch_state_ = 0;
    const char* dispatch_reason_ = "";  // static strings only
    std::uint64_t next_pump_ms_ = 0;
    std::uint64_t last_accept_id_ = 0, last_accept_ms_ = 0;
    volatile long long peer_id_ = 0;
    std::uint64_t host_id_ = 0;
    TransportError error_ = TransportError::none;
    // Counters: written by the transport thread (ring_full also by producers).
    volatile long long tx_ = 0, rx_ = 0, ring_full_ = 0, send_fail_ = 0, send_invalid_ = 0, too_large_ = 0, foreign_ = 0,
                       bad_identity_ = 0, bad_size_ = 0, denied_ = 0, accepted_ = 0, accept_refused_ = 0, closed_idle_ = 0,
                       closed_foreign_ = 0, release_ = 0, received_ = 0, faults_ = 0, accept_dup_ = 0, pumps_ = 0;
    std::uint64_t next_idle_ms_ = 0, next_route_ms_ = 0, next_stats_ms_ = 0, send_fail_log_ms_ = 0;
    std::uint64_t logged_[8]{};
    bool stats_logged_ = false;
    std::int32_t logged_state_ = -1;
    std::uint8_t logged_route_ = 0;
    std::uint8_t info_[steam_net::kConnInfoSize]{};
};

}  // namespace mgscoop::lab
