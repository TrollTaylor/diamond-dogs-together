#pragma once
// World sync W0-W2 core (docs/world-sync-protocol.md, docs/world-sync-design.md):
// the op table, the bundle codec carried by the link's reliable channel 1 and
// DATAGRAM channel 1, the inbound/outbound queues, pairing / epoch / effective
// level, the host's automatic RECEIVED/REJECT, and the lab probe-fire and goto
// schedulers. Pure and portable: no Win32, no game memory, every time passed
// in. Nothing here is thread safe; world_link serializes every call under one
// leaf lock and writes the log lines this core formats after releasing it.
//
// Bundle (little-endian), at most 1024 bytes on R and 512 on U:
//   u8 0x57 'W' | u8 version 1 | u8 count | u8 flags 0 | op x count
//   op: u8 op | u8 len | u16 epoch | body[len]
// Lua sends integers only (the TX receipt); native appends its own fields
// (HELLO level/role/proto/epoch/saves/build, READY/MISSION_END epoch,
// PROBE_SYNC atMs); the receiver appends latencyMs to DIGEST and CLOCK.
#include <cstddef>
#include <cstdint>
#include "mgscoop/lab_config.hpp"

namespace mgscoop::world {

inline constexpr std::uint8_t kProto = 1;
inline constexpr std::uint8_t kBundleMagic = 0x57, kBundleVersion = 1;
inline constexpr std::size_t kBundleHeaderSize = 4, kOpHeaderSize = 4;
inline constexpr std::size_t kMaxReliableBundle = 1024, kMaxDatagramBundle = 512;
inline constexpr unsigned kMaxValues = 15;  // Lua + native + receiver fields of one op
inline constexpr unsigned kMaxCount = 255;  // ops per bundle
inline constexpr std::uint8_t kRoleHost = 1, kRoleGuest = 2;
inline constexpr std::uint8_t kModeLab = 1, kModePlay = 2;
inline constexpr std::uint32_t kMissionMtbsFree = 30050, kMissionMtbsHeli = 40050;
inline constexpr std::uint32_t kMissionAfghFree = 30010, kMissionMafrFree = 30020;
inline constexpr bool FollowMission(std::uint32_t mission) noexcept {
    return mission == 30010 || mission == 30020 || mission == 30050 ||
           mission == 40010 || mission == 40020 || mission == 40050;
}
// TppMission.IsHelicopterSpace: an ACC (40010/40020/40050).
inline constexpr bool IsHeliSpace(std::uint32_t mission) noexcept { return mission / 10000 == 4; }

// Queue sizes (static POD; nothing allocates after Configure).
inline constexpr unsigned kControlQueue = 256, kLocalReserve = 32, kWorldQueue = 4096, kOutboundQueue = 2048;
inline constexpr unsigned kProbeFires = 4;
inline constexpr unsigned kLogLines = 64;
inline constexpr std::size_t kLogLineSize = 320;

namespace op {
// Control (queue 0).
inline constexpr std::uint8_t hello = 0x01, ready = 0x02, received = 0x03, reject = 0x04, resync = 0x05, resync_end = 0x06,
                              mission_end = 0x07, probe_sync = 0x08;
// W3 control survives a local mission start; it never enters the world queue.
inline constexpr std::uint8_t transition = 0x09, seed = 0x0A, seeds_end = 0x0B,
                              req_mission = 0x0C, follow_ack = 0x0D;
inline constexpr std::uint8_t align_begin = 0x0E, align_end = 0x0F, align_record = 0x18,
                              align_ack = 0x19, game_over = 0x1A, req_game_over = 0x1B, checkpoint = 0x1C;
// World (queue 1).
inline constexpr std::uint8_t enemy_outcome = 0x10, req_outcome = 0x11, phase = 0x12, req_phase = 0x13, player_area = 0x14,
                              digest = 0x15, guest_digest = 0x16, phase_vec = 0x17, clock = 0x20, weather = 0x21;
// Debug enemy placement and per-soldier debug actions (docs/debug-menu.md
// "Spawn sync"): the host announces, the guest applies and answers.
inline constexpr std::uint8_t enemy_debug = 0x22, enemy_debug_ack = 0x23;
// Local (never on the wire).
inline constexpr std::uint8_t link = 0x70, paired = 0x71, unpaired = 0x72, go_to = 0x73, probe = 0x74, probe_fire = 0x75,
                              resync_needed = 0x76, level = 0x77, goto_ack = 0x78;
inline constexpr std::uint8_t first_local = 0x70;
}

// REJECT reasons (4 is reserved).
namespace reject {
inline constexpr std::uint8_t unknown_entity = 1, terminal = 2, not_forward = 3, epoch = 5, not_paired = 6, roster_mismatch = 7,
                              bad_state = 8, busy = 9, level = 10, inactive = 11;
}
// ENEMY_DEBUG kinds and flags (lua/coop/world_place.lua).
namespace place {
inline constexpr std::uint8_t warp = 1, route = 2, correct = 3, come = 4, ignore = 5, notice = 6, disable = 7, enable = 8;
inline constexpr std::uint8_t flag_come = 1, flag_restrict_notice = 2;
}
// Guest warp grants: a warp/correct ENEMY_DEBUG popped by the guest's Lua is
// the only source of a guest native placement target (never a Lua value).
inline constexpr unsigned kGrants = 8;
inline constexpr std::uint64_t kGrantLifeMs = 15000;
struct Grant {
    bool valid;
    std::uint16_t seq, epoch;
    std::uint8_t kind, flags;
    std::uint32_t ent;
    std::int32_t pos_mm[3];
    std::int32_t yaw_mrad;
    std::uint64_t rx_ms;
};
// UNPAIRED reasons.
namespace unpair {
inline constexpr std::uint8_t link_session = 1, remote_mission = 2, epoch = 3, remote_end = 4, level0 = 5, local_end = 6;
}

enum class Type : std::uint8_t { u8, u16, u32, i32 };
// A value is valid in [min, max], or equal to alt when alt >= 0 (weather 255 = release).
struct Field {
    Type type;
    std::int64_t min, max, alt;
};
enum class Dir : std::uint8_t { both, host_to_guest, guest_to_host, native_to_lua, lua_to_native };
enum class Wire : std::uint8_t { reliable, datagram, local };
struct OpSpec {
    std::uint8_t code;
    const char* name;
    std::uint8_t queue;   // 0 control, 1 world
    Dir dir;
    Wire wire;
    std::uint8_t level;   // effective level that carries it: 1 W0, 2 W1, 3 W2
    bool request;         // the host answers with RECEIVED or REJECT
    bool lab_only;
    std::uint8_t lua, native, rx;  // field counts, in this order
    std::uint8_t body;    // wire body bytes (Lua + native fields)
    Field fields[kMaxValues];
};
const OpSpec* FindOp(std::uint8_t code) noexcept;
const OpSpec* OpTable(std::size_t& count) noexcept;
std::size_t FieldSize(Type type) noexcept;
bool FieldValid(const Field& field, std::int64_t value) noexcept;

// One decoded op: values are the Lua then native fields (receiver fields are
// appended by Core when it is popped).
struct Op {
    std::uint8_t code;
    std::uint16_t epoch;
    std::uint8_t count;
    std::int64_t v[kMaxValues];
};
// Encodes one wire op (count = lua + native, every value valid). Returns the
// bytes written (kOpHeaderSize + body) or 0.
std::size_t EncodeOp(const Op& op, std::uint8_t* out, std::size_t capacity) noexcept;
// Wraps already-encoded ops: writes the 4-byte header in front of them.
void WriteBundleHeader(std::uint8_t* out, std::uint8_t count) noexcept;

enum class OpStatus : std::uint8_t { ok, unknown_op, bad_op };
class BundleReader {
public:
    // Validates the header and every op header/length (false: bad_bundle).
    bool Open(const std::uint8_t* data, std::size_t size) noexcept;
    // The next op: ok (decoded), unknown_op (skipped by len), bad_op (len or
    // a field out of range, or a local op). False after the last op.
    bool Next(Op& out, OpStatus& status) noexcept;

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0, at_ = 0;
    unsigned left_ = 0;
};

// world.trigger (lab only): world=0..3 world_probe=<name>|off goto=30010|30020 goto_lz=0|1.
struct Trigger {
    bool has_world = false, has_probe = false, has_goto = false, has_lz = false;
    std::uint8_t world = 0;
    WorldProbe probe = WorldProbe::none;  // none = off
    std::uint32_t goto_target = 0;
    std::uint8_t lz = 0;
};

enum class TxStatus : std::uint8_t {
    ok = 0, syntax = 1, unknown_op = 2, bad_args = 3, wrong_role = 4, not_paired = 5, queue_full = 6, disabled = 7, lab_only = 8
};

struct Config {
    std::uint8_t level = 0;          // lab.ini world (0..3)
    std::uint8_t role = kRoleHost;
    bool play = false;               // mode=play: no probes, no PROBE_SYNC, no goto
    bool lab_triggers = false;       // world.trigger honoured
    std::uint8_t saves = 1;          // the local save gate (always 1 when configured)
    std::uint32_t build = 0;         // major*10000 + minor*100 + patch
    WorldProbe probe = WorldProbe::none;  // lab.ini world_probe (consumed by the first CFG start)
};

struct CfgReply {
    std::uint8_t level, role, proto, link;
    std::uint16_t epoch;
    std::uint8_t probe, mode;
    std::uint32_t session; // Local transport generation; zero while disconnected.
};

struct RxReply {
    bool empty;
    std::uint8_t queue, op, count;
    std::int64_t v[kMaxValues];
    // Empty reply: flags 1 link up, 2 paired, 4 resync_needed, 8 level0.
    std::uint8_t flags;
    std::uint16_t qctrl, qworld, epoch;
};

// A U op TXed by Lua: one-op bundle for partner_net::SendDatagram(1, ...).
struct Datagram {
    std::uint8_t bytes[kMaxDatagramBundle];
    std::size_t size;
};

struct Stats {
    std::uint32_t tx_r, tx_u, rx_r, rx_u, out_full, overflow, purged, stale_u, bad_bundle, bad_op, wrong_dir, level_drop,
        unknown_op, auto_received, auto_reject, rx_gap_ms_max, q_world_max;
};

enum class GotoState : std::uint8_t { idle, step1, expect_acc, step2, expect_target };

class Core {
public:
    void Configure(const Config& config, std::uint64_t now_ms) noexcept;

    // Receive thread (world_link hooks).
    void OnSession(std::uint32_t session, std::uint64_t now_ms) noexcept;
    void OnLinkState(bool up, std::uint32_t session, std::uint64_t now_ms) noexcept;
    void OnReliable(const std::uint8_t* data, std::size_t size, std::uint64_t now_ms) noexcept;
    void OnDatagram(const std::uint8_t* data, std::size_t size, std::uint64_t now_ms) noexcept;

    // Game thread (receipts). Cfg start (start=true) has the side effects.
    CfgReply Cfg(std::uint32_t mission, bool start, std::uint32_t read_mission, std::uint64_t now_ms) noexcept;
    // `values` are the Lua fields. A U op fills `datagram` (size > 0) for the
    // caller to send; R ops go to the outbound queue.
    TxStatus Tx(std::uint8_t code, const std::int64_t* values, unsigned count, std::uint64_t now_ms, Datagram* datagram) noexcept;
    void Rx(std::uint8_t mask, std::uint32_t rtt_ms, std::uint64_t now_ms, RxReply& out) noexcept;
    void ApplyTrigger(const Trigger& trigger, const char* canonical, std::uint32_t read_mission, bool terminating,
                      std::uint64_t now_ms) noexcept;
    void RejectTrigger(const char* reason) noexcept;
    void LocalMissionEnd(std::uint64_t now_ms) noexcept;

    // Outbound pump: a bundle of the oldest queued R ops (nothing removed),
    // then CommitBundle(generation) removes them unless a clear intervened.
    std::size_t PeekBundle(std::uint8_t* out, std::size_t capacity, std::uint32_t& generation) noexcept;
    void CommitBundle(std::uint32_t generation) noexcept;
    bool OutboundPending() const noexcept { return out_size_ != 0; }

    // Timers: probe fires, goto timeouts, remote_silent, stats.
    void Tick(std::uint64_t now_ms) noexcept;
    // Formatted log lines without the "; t_ms=..." suffix and line end.
    bool TakeLine(char* out, std::size_t capacity) noexcept;
    // Guest: the warp/correct grant recorded when Rx popped ENEMY_DEBUG `seq`
    // in the current paired epoch, at most kGrantLifeMs ago. Cleared at unpair,
    // local mission start/end, link session and host epoch changes.
    bool FindGrant(std::uint16_t seq, std::uint64_t now_ms, Grant& out) const noexcept;
    unsigned grant_count() const noexcept;

    // Observers (tests and status).
    bool paired() const noexcept { return paired_; }
    std::uint16_t epoch() const noexcept { return epoch_; }
    std::uint16_t host_epoch() const noexcept { return known_host_epoch_; }
    std::uint8_t level() const noexcept { return level_; }
    std::uint8_t eff() const noexcept { return eff_; }
    bool link_up() const noexcept { return link_up_; }
    bool resync_needed() const noexcept { return resync_needed_; }
    unsigned control_size() const noexcept { return ctrl_size_; }
    unsigned world_size() const noexcept { return world_size_; }
    unsigned outbound_size() const noexcept { return out_size_; }
    std::uint32_t generation() const noexcept { return generation_; }
    GotoState goto_state() const noexcept { return goto_.state; }
    const Stats& stats() const noexcept { return stats_; }

private:
    struct Entry {
        std::uint8_t op, count;
        bool local;
        std::uint16_t epoch;
        std::int64_t v[kMaxValues];
        std::uint64_t rx_ms;
    };
    struct OutEntry {
        std::uint8_t size;
        std::uint8_t bytes[kOpHeaderSize + 32];
    };
    struct Ready {
        bool valid;
        std::uint32_t mission, roster;
        std::uint16_t soldiers, cps, epoch;
    };
    struct Slot {
        bool valid, delivered;
        Entry entry;
    };
    struct Fire {
        bool used;
        std::uint8_t probe, step;
        std::uint32_t ent, arg, at;
    };
    struct Goto {
        GotoState state;
        std::uint32_t target;
        std::uint8_t lz;
        std::uint64_t armed_ms, deadline_ms;
        bool waiting_logged;
    };

    void Line(const char* format, ...) noexcept;
    bool PushControl(const Entry& entry, bool local) noexcept;
    void PushLocal(std::uint8_t code, std::int64_t a = 0, std::int64_t b = 0, std::int64_t c = 0, std::int64_t d = 0,
                   std::int64_t e = 0, std::int64_t f = 0) noexcept;
    bool PushWorld(const Entry& entry) noexcept;
    bool QueueOut(const Op& op) noexcept;
    void AutoAnswer(std::uint8_t req_op, std::int64_t rid, std::int64_t ent, std::uint8_t reason) noexcept;
    // Drops are counted per queue and logged as world=overflow; queue=<name>
    // at once (at most every 10 s) or by Tick.
    enum DropQueue : unsigned { kDropControl, kDropWorld, kDropOut, kDropQueues };
    void Overflow(DropQueue queue, std::uint64_t now_ms) noexcept;
    void Dropped(DropQueue queue, std::uint64_t now_ms, bool log_now) noexcept;
    void FlushOverflow(std::uint64_t now_ms) noexcept;
    void OnOp(const Op& op, bool datagram, std::uint64_t now_ms) noexcept;
    void OnHello(const Op& op, std::uint64_t now_ms) noexcept;
    void OnReady(const Op& op, std::uint64_t now_ms) noexcept;
    void AdoptHostEpoch(std::uint16_t epoch) noexcept;
    void TryPair(std::uint64_t now_ms) noexcept;
    void Unpair(std::uint8_t reason) noexcept;
    void PurgeOtherEpochs(std::uint16_t keep) noexcept;
    void ClearWire() noexcept;
    void RequeueReady(std::uint64_t now_ms, bool limited) noexcept;
    void DueFires(std::uint64_t now_ms) noexcept;
    void GotoFail(const char* reason, std::uint32_t mission) noexcept;
    void GotoTx(std::int64_t step, std::int64_t result, std::uint64_t now_ms) noexcept;
    void Stats10s(std::uint64_t now_ms) noexcept;
    std::uint16_t TxEpoch() const noexcept;
    void RecordGrant(const Entry& entry, std::uint64_t now_ms) noexcept;
    void ClearGrants() noexcept;

    Config config_{};
    std::uint8_t level_ = 0, pending_level_ = 0, remote_level_ = 0, remote_saves_ = 0, remote_role_ = 0, eff_ = 0;
    bool pending_level_set_ = false;
    bool remote_hello_ = false;
    bool link_up_ = false, link_known_ = false;
    std::uint32_t session_ = 0;
    std::uint64_t link_up_ms_ = 0;
    bool hello_since_up_ = false, silent_logged_ = false;
    std::uint16_t epoch_ = 0;             // host: its own counter; guest: the adopted epoch while paired
    std::uint16_t known_host_epoch_ = 0;  // guest: the latest host READY/HELLO epoch; host: epoch_
    std::uint32_t host_mission_ = 0;      // guest: the mission of that latest host READY/HELLO
    bool paired_ = false;
    Ready local_{}, remote_{};
    std::uint16_t sent_epoch_ = 0;        // epoch of our last READY
    Op last_ready_{};
    bool have_last_ready_ = false;
    std::uint64_t echo_ms_ = 0;
    bool pair_refused_logged_ = false;
    bool resync_needed_ = false, resync_pushed_ = false;
    bool mission_running_ = false;        // a CFG start with level >= 1 and no mission end since
    std::uint32_t start_mission_ = 0;
    WorldProbe pending_probe_ = WorldProbe::none;
    bool pending_probe_logged_ = false;
    Fire fires_[kProbeFires]{};
    Goto goto_{};

    Entry ctrl_[kControlQueue + kLocalReserve]{};
    unsigned ctrl_head_ = 0, ctrl_size_ = 0;
    Entry world_[kWorldQueue]{};
    unsigned world_head_ = 0, world_size_ = 0;
    Slot digest_{}, guest_digest_{}, phase_vec_{};
    Grant grants_[kGrants]{};
    OutEntry out_[kOutboundQueue]{};
    unsigned out_head_ = 0, out_size_ = 0, peeked_ = 0;
    std::uint32_t generation_ = 0;

    Stats stats_{}, logged_stats_{};
    bool stats_logged_ = false, logged_paired_ = false;
    std::uint8_t logged_level_ = 0;
    std::uint16_t logged_epoch_ = 0;
    unsigned logged_out_ = 0, logged_ctrl_ = 0, logged_world_ = 0;
    std::uint64_t stats_ms_ = 0, last_rx_ms_ = 0;
    std::uint32_t overflow_pending_[kDropQueues]{};
    std::uint64_t overflow_ms_ = 0;
    bool overflow_logged_once_ = false;

    char lines_[kLogLines][kLogLineSize]{};
    unsigned line_head_ = 0, line_size_ = 0;
};

// Wrap-safe: true when `at` (low 32 bits of QPC ms) is due at `now32`.
inline bool Due32(std::uint32_t now32, std::uint32_t at) noexcept { return static_cast<std::int32_t>(now32 - at) >= 0; }
// Wrap-safe u32 tick order: true when `a` is newer than `b`.
inline bool Newer32(std::uint32_t a, std::uint32_t b) noexcept {
    const std::uint32_t d = a - b;
    return d != 0 && d < 0x80000000u;
}

} // namespace mgscoop::world
