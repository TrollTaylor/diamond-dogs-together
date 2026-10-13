// Original MGS Co-op world sync core (docs/world-sync-protocol.md): op table,
// bundle codec, queues, pairing / epoch / effective level and the lab
// schedulers. Pure; world_link serializes every call.
#include "mgscoop/world_wire.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace mgscoop::world {
namespace {
constexpr std::int64_t kU8 = 0xFF, kU16 = 0xFFFF, kU32 = 0xFFFFFFFFLL;
constexpr std::int64_t kI32Min = -2147483647LL - 1, kI32Max = 2147483647LL;
constexpr Field U8(std::int64_t lo = 0, std::int64_t hi = kU8, std::int64_t alt = -1) { return Field{Type::u8, lo, hi, alt}; }
constexpr Field U16(std::int64_t lo = 0, std::int64_t hi = kU16) { return Field{Type::u16, lo, hi, -1}; }
constexpr Field U32(std::int64_t lo = 0, std::int64_t hi = kU32) { return Field{Type::u32, lo, hi, -1}; }
constexpr Field I32() { return Field{Type::i32, kI32Min, kI32Max, -1}; }
constexpr Field I32(std::int64_t lo, std::int64_t hi) { return Field{Type::i32, lo, hi, -1}; }
constexpr Field Mission() { return U32(0, 99999); }
constexpr Field Rid() { return U16(1, kU16); }
constexpr Field Weather() { return U8(0, 5, 255); }

// Body sizes (section 4.2): Lua + native fields only; receiver fields never travel.
constexpr OpSpec kOps[] = {
    {op::hello, "HELLO", 0, Dir::both, Wire::reliable, 1, false, false, 5, 6, 0, 24,
     {Mission(), U16(), U32(), U16(0, 1024), U16(0, 512), U8(0, 3), U8(1, 2), U8(1, 1), U16(), U8(0, 1), U32()}},
    {op::ready, "MISSION_READY", 0, Dir::both, Wire::reliable, 1, false, false, 4, 1, 0, 14,
     {Mission(), U32(), U16(0, 1024), U16(0, 512), U16()}},
    {op::received, "RECEIVED", 0, Dir::host_to_guest, Wire::reliable, 1, false, false, 0, 2, 0, 3, {Rid(), U8()}},
    {op::reject, "REJECT", 0, Dir::host_to_guest, Wire::reliable, 1, false, false, 6, 0, 0, 11,
     {Rid(), U8(), U8(1, 15), U32(), U8(0, 7), U16()}},
    {op::resync, "RESYNC", 0, Dir::guest_to_host, Wire::reliable, 1, true, false, 2, 0, 0, 3, {Rid(), U8(1, 7)}},
    {op::resync_end, "RESYNC_END", 1, Dir::host_to_guest, Wire::reliable, 1, false, false, 4, 0, 0, 7,
     {U16(), U8(1, 7), U16(), U16()}},
    {op::mission_end, "MISSION_END", 0, Dir::both, Wire::reliable, 1, false, false, 1, 1, 0, 6, {Mission(), U16()}},
    {op::probe_sync, "PROBE_SYNC", 0, Dir::both, Wire::reliable, 1, false, true, 4, 1, 0, 14,
     {U8(1, kWorldProbeMax), U8(1, 4), U32(), U32(), U32()}},
    {op::transition, "MISSION_TRANSITION", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 11, 0, 0, 27,
     {U32(1), U8(1, 6), Mission(), Mission(), U8(), U32(), U32(), U8(), U8(), U8(0, 1), U16(1)}},
    {op::seed, "MISSION_SEED", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 5, 0, 0, 15,
     {U32(1), Mission(), U8(1, 63), U16(0, 1023), U32()}},
    {op::seeds_end, "MISSION_SEEDS_END", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 4, 0, 0, 14,
     {U32(1), Mission(), U16(0, 1024), U32()}},
    {op::req_mission, "REQ_MISSION", 0, Dir::guest_to_host, Wire::reliable, 3, true, false, 4, 0, 0, 11,
     {Rid(), U8(1, 8), Mission(), U32()}},
    {op::follow_ack, "FOLLOW_ACK", 0, Dir::guest_to_host, Wire::reliable, 3, false, false, 3, 0, 0, 9,
     {U32(1), Mission(), U8(0, 2)}},
    {op::align_begin, "ALIGN_BEGIN", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 5, 1, 0, 18,
     {U32(1), Mission(), U32(), U16(0, 1024), U16(0, 512), U16(1)}},
    {op::align_end, "ALIGN_END", 1, Dir::host_to_guest, Wire::reliable, 3, false, false, 5, 1, 0, 16,
     {U32(1), U16(0, 1024), U16(0, 512), U32(), U16(), U16(1)}},
    {op::align_record, "ALIGN_RECORD", 1, Dir::host_to_guest, Wire::reliable, 3, false, false, 7, 0, 0, 15,
     {U32(1), U8(1, 2), U16(1, 1024), U32(), U8(0, 1), U8(0, 7), U16(1)}},
    {op::align_ack, "ALIGN_ACK", 0, Dir::guest_to_host, Wire::reliable, 3, false, false, 4, 0, 0, 11,
     {U32(1), Mission(), U16(1), U8(0, 1)}},
    {op::game_over, "GAME_OVER", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 3, 0, 0, 6,
     {U8(), U32(), U8()}},
    {op::req_game_over, "REQ_GAME_OVER", 0, Dir::guest_to_host, Wire::reliable, 3, true, false, 4, 0, 0, 8,
     {Rid(), U8(), U32(), U8()}},
    {op::checkpoint, "CHECKPOINT", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 6, 0, 0, 22,
     {U32(), U16(1), I32(-10000000, 10000000), I32(-10000000, 10000000), I32(-10000000, 10000000), I32(-360000, 360000)}},
    {op::enemy_outcome, "ENEMY_OUTCOME", 1, Dir::host_to_guest, Wire::reliable, 2, false, false, 11, 0, 0, 25,
     {U32(), U8(1, 1), U8(0, 7), U16(1, kU16), U8(0, 15), U8(0, 2), U16(), U8(0, 3), I32(), I32(), I32()}},
    {op::req_outcome, "REQ_OUTCOME", 1, Dir::guest_to_host, Wire::reliable, 2, true, false, 11, 0, 0, 28,
     {Rid(), U32(), U8(1, 1), U8(1, 7), U16(), U8(0, 15), U32(), U8(0, 1), I32(), I32(), I32()}},
    {op::phase, "PHASE", 1, Dir::host_to_guest, Wire::reliable, 2, false, false, 6, 0, 0, 11,
     {U32(), U8(0, 3), U16(1, kU16), U8(0, 0), U16(), U8(0, 1)}},
    {op::req_phase, "REQ_PHASE", 1, Dir::guest_to_host, Wire::reliable, 2, true, false, 8, 0, 0, 24,
     {Rid(), U32(), U8(1, 3), U8(0, 15), U32(), I32(), I32(), I32()}},
    {op::player_area, "PLAYER_AREA", 1, Dir::both, Wire::reliable, 2, false, false, 2, 0, 0, 5, {U8(0, 15), Mission()}},
    {op::digest, "DIGEST", 1, Dir::host_to_guest, Wire::datagram, 2, false, false, 9, 0, 1, 22,
     {U32(), U32(0, 86399), U16(), Weather(), U16(), U32(), U16(), U16(), U8(0, 1), U16()}},
    {op::guest_digest, "GUEST_DIGEST", 1, Dir::guest_to_host, Wire::datagram, 2, false, false, 9, 0, 0, 22,
     {U32(), U16(), U16(), U32(), U16(), U16(), U8(0, 1), U32(0, 86399), Weather()}},
    {op::phase_vec, "PHASE_VEC", 1, Dir::both, Wire::datagram, 2, false, false, 15, 0, 0, 53,
     {U16(0, 192), U16(0, 0), U8(1, 12), U32(), U32(), U32(), U32(), U32(), U32(), U32(), U32(), U32(), U32(), U32(), U32()}},
    {op::clock, "CLOCK", 1, Dir::host_to_guest, Wire::reliable, 3, false, false, 4, 0, 1, 11,
     {U32(0, 86399), U16(), U8(1, 4), U32(), U16()}},
    {op::weather, "WEATHER", 1, Dir::host_to_guest, Wire::reliable, 3, false, false, 5, 0, 0, 7,
     {Weather(), U16(0, 600), U16(kU16, kU16), U8(255, 255), U8(1, 3)}},
    // Spawn sync (docs/debug-menu.md): seq, ent, kind, x/y/z mm, yaw mrad, flags, route hash, host GameObjectId.
    {op::enemy_debug, "ENEMY_DEBUG", 1, Dir::host_to_guest, Wire::reliable, 2, false, false, 10, 0, 0, 30,
     {U16(1, kU16), U32(), U8(1, 8), I32(-100000000, 100000000), I32(-100000000, 100000000), I32(-100000000, 100000000),
      I32(-8000, 8000), U8(0, 3), U32(), U16()}},
    // seq, ent, result (0 applied, 1 refused), reason (world_place.lua WHY), error cm, frames.
    {op::enemy_debug_ack, "ENEMY_DEBUG_ACK", 1, Dir::guest_to_host, Wire::reliable, 2, false, false, 6, 0, 0, 12,
     {U16(1, kU16), U32(), U8(0, 1), U8(0, 31), U16(), U16()}},
    // DOWNED_STATUS: seq, generation, down, cause, xyz mm, revive hold, target generation, ready.
    {op::downed_status, "DOWNED_STATUS", 0, Dir::both, Wire::reliable, 3, false, false, 10, 0, 0, 26,
     {U16(1), U32(), U8(0, 1), U8(0, 2), I32(-1000000000, 1000000000), I32(-1000000000, 1000000000),
      I32(-1000000000, 1000000000), U8(0, 1), U32(), U8(0, 1)}},
    // DOWNED_COMMIT: version, each role's generation/down/cause, progress ms, target role.
    {op::downed_commit, "DOWNED_COMMIT", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 9, 0, 0, 17,
     {U16(1), U32(), U8(0, 1), U8(0, 2), U32(), U8(0, 1), U8(0, 2), U16(0, 3000), U8(0, 2)}},
    {op::revive_grant, "REVIVE_GRANT", 0, Dir::host_to_guest, Wire::reliable, 3, false, false, 3, 0, 0, 9,
     {U8(1, 2), U32(1), U32(1)}},
    // BUDDY_STATE: seq, kind (none/dog/horse/walker/Quiet), present, xyz mm, yaw mrad, sender tick.
    {op::buddy_state, "BUDDY_STATE", 1, Dir::host_to_guest, Wire::reliable, 3, false, false, 8, 0, 1, 24,
     {U16(1), U8(0, 4), U8(0, 1), I32(-1000000000, 1000000000), I32(-1000000000, 1000000000),
      I32(-1000000000, 1000000000), I32(-3142, 3142), U32(), U16()}},
    // Guest readback: seq, kind, result (following/loading/refused/expired/released), error cm.
    {op::buddy_ack, "BUDDY_ACK", 1, Dir::guest_to_host, Wire::reliable, 3, false, false, 4, 0, 0, 6,
     {U16(1), U8(0, 4), U8(0, 4), U16()}},
    // Authenticated local-shooter hit: shot sequence, owner generation/status sequence, damage milli, cause(normal/sleep).
    {op::friendly_hit, "FRIENDLY_HIT", 0, Dir::both, Wire::reliable, 3, false, false, 5, 0, 0, 15,
     {U32(1), U32(), U16(1), U32(0, 1000000000), U8(0, 0, 2)}},
    // RIDE_BODY: seq, kind (1 horse, 2 walker, 3 vehicle), body id, xyz mm, heading mrad, occupied, rider.
    // Horse and walker heading is the fourth float of the verified 16-byte census pose.
    // Vehicle heading is the second return of TppVehicle2 GetPosition.
    {op::ride_body, "RIDE_BODY", 1, Dir::both, Wire::reliable, 3, false, false, 9, 0, 0, 24,
     {U16(1), U8(1, 3), U16(), I32(-1000000000, 1000000000), I32(-1000000000, 1000000000),
      I32(-1000000000, 1000000000), I32(-31416, 31416), U8(0, 1), U16()}},
    {op::link, "LINK", 0, Dir::native_to_lua, Wire::local, 1, false, false, 0, 2, 0, 0, {U8(0, 1), U32()}},
    {op::paired, "PAIRED", 0, Dir::native_to_lua, Wire::local, 1, false, false, 0, 6, 0, 0,
     {U16(), Mission(), U32(), U16(), U8(0, 1), U8(0, 3)}},
    {op::unpaired, "UNPAIRED", 0, Dir::native_to_lua, Wire::local, 1, false, false, 0, 1, 0, 0, {U8(1, 6)}},
    {op::go_to, "GOTO", 0, Dir::native_to_lua, Wire::local, 1, false, true, 0, 3, 0, 0, {U8(1, 2), U32(), U8(0, 1)}},
    {op::probe, "PROBE", 0, Dir::native_to_lua, Wire::local, 1, false, true, 0, 2, 0, 0, {U8(1, kWorldProbeMax), U32()}},
    {op::probe_fire, "PROBE_FIRE", 0, Dir::native_to_lua, Wire::local, 1, false, true, 0, 4, 0, 0,
     {U8(1, kWorldProbeMax), U8(1, 4), U32(), U32()}},
    {op::resync_needed, "RESYNC_NEEDED", 0, Dir::native_to_lua, Wire::local, 1, false, false, 0, 1, 0, 0, {U8(1, 7)}},
    {op::level, "LEVEL", 0, Dir::native_to_lua, Wire::local, 1, false, false, 0, 1, 0, 0, {U8(0, 3)}},
    {op::goto_ack, "GOTO_ACK", 0, Dir::lua_to_native, Wire::local, 1, false, true, 2, 0, 0, 0, {U8(1, 2), U8(0, 2)}},
};

void PutU16(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v);
    p[1] = static_cast<std::uint8_t>(v >> 8);
}
void PutU32(std::uint8_t* p, std::uint32_t v) noexcept {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
std::uint16_t GetU16(const std::uint8_t* p) noexcept { return static_cast<std::uint16_t>(p[0] | (p[1] << 8)); }
std::uint32_t GetU32(const std::uint8_t* p) noexcept {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p[i]) << (8 * i);
    return v;
}
std::int64_t Read(Type type, const std::uint8_t* p) noexcept {
    switch (type) {
    case Type::u8: return p[0];
    case Type::u16: return GetU16(p);
    case Type::u32: return GetU32(p);
    default: return static_cast<std::int32_t>(GetU32(p));
    }
}
void Write(Type type, std::int64_t value, std::uint8_t* p) noexcept {
    const auto u = static_cast<std::uint32_t>(static_cast<std::uint64_t>(value) & 0xFFFFFFFFu);
    switch (type) {
    case Type::u8: p[0] = static_cast<std::uint8_t>(u); break;
    case Type::u16: PutU16(p, u); break;
    default: PutU32(p, u); break;
    }
}
const char* RoleText(std::uint8_t role) noexcept { return role == kRoleGuest ? "guest" : "host"; }
const char* UnpairName(std::uint8_t reason) noexcept {
    switch (reason) {
    case unpair::link_session: return "session";
    case unpair::remote_mission: return "remote_mission";
    case unpair::epoch: return "epoch";
    case unpair::remote_end: return "remote_end";
    case unpair::level0: return "level0";
    default: return "local_end";
    }
}
std::uint32_t U(std::int64_t v) noexcept { return static_cast<std::uint32_t>(v); }
} // namespace

const OpSpec* OpTable(std::size_t& count) noexcept {
    count = sizeof(kOps) / sizeof(kOps[0]);
    return kOps;
}

const OpSpec* FindOp(std::uint8_t code) noexcept {
    for (const OpSpec& spec : kOps)
        if (spec.code == code) return &spec;
    return nullptr;
}

std::size_t FieldSize(Type type) noexcept {
    switch (type) {
    case Type::u8: return 1;
    case Type::u16: return 2;
    default: return 4;
    }
}

bool FieldValid(const Field& field, std::int64_t value) noexcept {
    if (field.alt >= 0 && value == field.alt) return true;
    return value >= field.min && value <= field.max;
}

std::size_t EncodeOp(const Op& op, std::uint8_t* out, std::size_t capacity) noexcept {
    const OpSpec* spec = FindOp(op.code);
    if (!spec || spec->wire == Wire::local || !out) return 0;
    const unsigned fields = static_cast<unsigned>(spec->lua) + spec->native;
    if (op.count != fields || capacity < kOpHeaderSize + spec->body) return 0;
    std::size_t at = kOpHeaderSize;
    for (unsigned i = 0; i < fields; ++i) {
        if (!FieldValid(spec->fields[i], op.v[i])) return 0;
        Write(spec->fields[i].type, op.v[i], out + at);
        at += FieldSize(spec->fields[i].type);
    }
    if (at != kOpHeaderSize + spec->body) return 0;
    out[0] = op.code;
    out[1] = spec->body;
    PutU16(out + 2, op.epoch);
    return at;
}

void WriteBundleHeader(std::uint8_t* out, std::uint8_t count) noexcept {
    out[0] = kBundleMagic;
    out[1] = kBundleVersion;
    out[2] = count;
    out[3] = 0;
}

bool BundleReader::Open(const std::uint8_t* data, std::size_t size) noexcept {
    data_ = nullptr;
    size_ = at_ = 0;
    left_ = 0;
    if (!data || size < kBundleHeaderSize || size > kMaxReliableBundle || data[0] != kBundleMagic || data[1] != kBundleVersion ||
        data[3] != 0)
        return false;
    const unsigned count = data[2];
    std::size_t at = kBundleHeaderSize;
    for (unsigned i = 0; i < count; ++i) {
        if (size - at < kOpHeaderSize) return false;
        const std::size_t length = data[at + 1];
        if (size - at - kOpHeaderSize < length) return false;
        at += kOpHeaderSize + length;
    }
    if (at != size) return false;
    data_ = data;
    size_ = size;
    at_ = kBundleHeaderSize;
    left_ = count;
    return true;
}

bool BundleReader::Next(Op& out, OpStatus& status) noexcept {
    if (!left_ || !data_) return false;
    --left_;
    const std::uint8_t* p = data_ + at_;
    const std::size_t length = p[1];
    at_ += kOpHeaderSize + length;
    out = Op{};
    out.code = p[0];
    out.epoch = GetU16(p + 2);
    const OpSpec* spec = FindOp(out.code);
    if (!spec) { status = OpStatus::unknown_op; return true; }
    if (spec->wire == Wire::local || length != spec->body) { status = OpStatus::bad_op; return true; }
    const unsigned fields = static_cast<unsigned>(spec->lua) + spec->native;
    std::size_t at = kOpHeaderSize;
    for (unsigned i = 0; i < fields; ++i) {
        out.v[i] = Read(spec->fields[i].type, p + at);
        at += FieldSize(spec->fields[i].type);
        if (!FieldValid(spec->fields[i], out.v[i])) { status = OpStatus::bad_op; return true; }
    }
    out.count = static_cast<std::uint8_t>(fields);
    status = OpStatus::ok;
    return true;
}

// ---- Core ---------------------------------------------------------------------

void Core::Configure(const Config& config, std::uint64_t now_ms) noexcept {
    config_ = config;
    if (config_.role != kRoleGuest) config_.role = kRoleHost;
    level_ = config.level > kWorldLevelMax ? static_cast<std::uint8_t>(kWorldLevelMax) : config.level;
    pending_level_ = 0;
    pending_level_set_ = false;
    remote_level_ = remote_saves_ = remote_role_ = eff_ = 0;
    remote_hello_ = false;
    link_up_ = link_known_ = false;
    session_ = 0;
    link_up_ms_ = 0;
    hello_since_up_ = silent_logged_ = false;
    epoch_ = known_host_epoch_ = 0;
    host_mission_ = 0;
    paired_ = false;
    local_ = Ready{};
    remote_ = Ready{};
    sent_epoch_ = 0;
    last_ready_ = Op{};
    have_last_ready_ = false;
    echo_ms_ = 0;
    pair_refused_logged_ = false;
    resync_needed_ = resync_pushed_ = false;
    mission_running_ = false;
    start_mission_ = 0;
    pending_probe_ = config.play ? WorldProbe::none : config.probe;
    pending_probe_logged_ = false;
    for (Fire& f : fires_) f.used = false;
    goto_ = Goto{};
    ctrl_head_ = ctrl_size_ = 0;
    world_head_ = world_size_ = 0;
    digest_.valid = guest_digest_.valid = phase_vec_.valid = false;
    ClearGrants();
    out_head_ = out_size_ = peeked_ = 0;
    generation_ = 0;
    stats_ = Stats{};
    logged_stats_ = Stats{};
    stats_logged_ = logged_paired_ = false;
    logged_level_ = 0;
    logged_epoch_ = 0;
    logged_out_ = logged_ctrl_ = logged_world_ = 0;
    stats_ms_ = now_ms;
    last_rx_ms_ = 0;
    for (std::uint32_t& n : overflow_pending_) n = 0;
    overflow_ms_ = 0;
    overflow_logged_once_ = false;
    line_head_ = line_size_ = 0;
    Line("world=configured; level=%u; role=%s; mode=%s; proto=%u; probe=%s", unsigned(level_), RoleText(config_.role),
         config_.play ? "play" : "lab", unsigned(kProto), WorldProbeName(pending_probe_));
}

void Core::Line(const char* format, ...) noexcept {
    if (line_size_ == kLogLines) {  // keep the newest
        line_head_ = (line_head_ + 1) % kLogLines;
        --line_size_;
    }
    char* out = lines_[(line_head_ + line_size_) % kLogLines];
    va_list arguments;
    va_start(arguments, format);
    const int n = std::vsnprintf(out, kLogLineSize, format, arguments);
    va_end(arguments);
    if (n < 0) out[0] = 0;
    ++line_size_;
}

bool Core::TakeLine(char* out, std::size_t capacity) noexcept {
    if (!line_size_ || !out || !capacity) return false;
    const char* line = lines_[line_head_];
    std::size_t n = 0;
    while (line[n] && n + 1 < capacity) { out[n] = line[n]; ++n; }
    out[n] = 0;
    line_head_ = (line_head_ + 1) % kLogLines;
    --line_size_;
    return true;
}

bool Core::PushControl(const Entry& entry, bool local) noexcept {
    const unsigned limit = local ? kControlQueue + kLocalReserve : kControlQueue;
    if (ctrl_size_ >= limit) return false;
    ctrl_[(ctrl_head_ + ctrl_size_) % (kControlQueue + kLocalReserve)] = entry;
    ++ctrl_size_;
    return true;
}

void Core::PushLocal(std::uint8_t code, std::int64_t a, std::int64_t b, std::int64_t c, std::int64_t d, std::int64_t e,
                     std::int64_t f) noexcept {
    const OpSpec* spec = FindOp(code);
    if (!spec) return;
    Entry entry{};
    entry.op = code;
    entry.local = true;
    entry.count = spec->native;
    const std::int64_t values[6] = {a, b, c, d, e, f};
    for (unsigned i = 0; i < entry.count && i < 6; ++i) entry.v[i] = values[i];
    if (!PushControl(entry, true)) {
        ++stats_.overflow;
        Dropped(kDropControl, 0, false);  // logged by Tick
    }
}

bool Core::PushWorld(const Entry& entry) noexcept {
    if (world_size_ >= kWorldQueue) return false;
    world_[(world_head_ + world_size_) % kWorldQueue] = entry;
    ++world_size_;
    if (world_size_ > stats_.q_world_max) stats_.q_world_max = world_size_;
    return true;
}

bool Core::QueueOut(const Op& op) noexcept {
    if (out_size_ >= kOutboundQueue) {
        ++stats_.out_full;
        return false;
    }
    OutEntry& slot = out_[(out_head_ + out_size_) % kOutboundQueue];
    const std::size_t n = EncodeOp(op, slot.bytes, sizeof(slot.bytes));
    if (!n) return false;
    slot.size = static_cast<std::uint8_t>(n);
    ++out_size_;
    return true;
}

std::uint16_t Core::TxEpoch() const noexcept {
    if (config_.role == kRoleHost) return epoch_;
    return paired_ ? epoch_ : known_host_epoch_;
}

void Core::AutoAnswer(std::uint8_t req_op, std::int64_t rid, std::int64_t ent, std::uint8_t reason) noexcept {
    if (config_.role != kRoleHost || rid < 1 || rid > kU16) return;
    Op out{};
    out.epoch = TxEpoch();
    if (!reason) {
        out.code = op::received;
        out.count = 2;
        out.v[0] = rid;
        out.v[1] = req_op;
        ++stats_.auto_received;
    } else {
        out.code = op::reject;
        out.count = 6;
        out.v[0] = rid;
        out.v[1] = req_op;
        out.v[2] = reason;
        out.v[3] = ent >= 0 && ent <= kU32 ? ent : 0;
        ++stats_.auto_reject;
    }
    if (!QueueOut(out)) Dropped(kDropOut, 0, false);  // logged by Tick
}

void Core::Overflow(DropQueue queue, std::uint64_t now_ms) noexcept {
    ++stats_.overflow;
    resync_needed_ = true;
    if (!resync_pushed_) {
        resync_pushed_ = true;
        PushLocal(op::resync_needed, 7);
    }
    Dropped(queue, now_ms, true);
}

void Core::Dropped(DropQueue queue, std::uint64_t now_ms, bool log_now) noexcept {
    ++overflow_pending_[queue];
    if (log_now && (!overflow_logged_once_ || now_ms - overflow_ms_ >= 10000)) FlushOverflow(now_ms);
}

void Core::FlushOverflow(std::uint64_t now_ms) noexcept {
    static constexpr const char* kNames[kDropQueues] = {"control", "world", "out"};
    for (unsigned q = 0; q < kDropQueues; ++q) {
        if (!overflow_pending_[q]) continue;
        Line("world=overflow; queue=%s; dropped=%u", kNames[q], overflow_pending_[q]);
        overflow_pending_[q] = 0;
    }
    overflow_ms_ = now_ms;
    overflow_logged_once_ = true;
}

void Core::OnReliable(const std::uint8_t* data, std::size_t size, std::uint64_t now_ms) noexcept {
    BundleReader reader;
    if (!reader.Open(data, size)) { ++stats_.bad_bundle; return; }
    Op op{};
    OpStatus status = OpStatus::ok;
    while (reader.Next(op, status)) {
        if (status == OpStatus::unknown_op) { ++stats_.unknown_op; continue; }
        if (status == OpStatus::bad_op) { ++stats_.bad_op; continue; }
        ++stats_.rx_r;
        OnOp(op, false, now_ms);
    }
}

void Core::OnDatagram(const std::uint8_t* data, std::size_t size, std::uint64_t now_ms) noexcept {
    BundleReader reader;
    if (size > kMaxDatagramBundle || !reader.Open(data, size)) { ++stats_.bad_bundle; return; }
    Op op{};
    OpStatus status = OpStatus::ok;
    while (reader.Next(op, status)) {
        if (status == OpStatus::unknown_op) { ++stats_.unknown_op; continue; }
        if (status == OpStatus::bad_op) { ++stats_.bad_op; continue; }
        ++stats_.rx_u;
        OnOp(op, true, now_ms);
    }
}

void Core::OnOp(const Op& op, bool datagram, std::uint64_t now_ms) noexcept {
    const OpSpec* spec = FindOp(op.code);
    if (!spec) return;
    // An R op inside a datagram (or a U op on R) and lab-only ops in play are malformed here.
    if ((spec->wire == Wire::datagram) != datagram || (spec->lab_only && config_.play)) { ++stats_.bad_op; return; }
    const bool host = config_.role == kRoleHost;
    if ((spec->dir == Dir::host_to_guest && host) || (spec->dir == Dir::guest_to_host && !host)) { ++stats_.wrong_dir; return; }
    if (!level_ || spec->level > level_) {
        ++stats_.level_drop;
        if (spec->request && host) AutoAnswer(op.code, op.v[0], op.code == op::resync ? 0 : op.v[1], reject::level);
        return;
    }
    Entry entry{};
    entry.op = op.code;
    entry.epoch = op.epoch;
    entry.count = op.count;
    entry.rx_ms = now_ms;
    for (unsigned i = 0; i < op.count && i < kMaxValues; ++i) entry.v[i] = op.v[i];
    switch (op.code) {
    case op::hello: OnHello(op, now_ms); return;
    case op::ready: OnReady(op, now_ms); return;
    case op::mission_end:
        if (remote_.valid && remote_.mission == U(op.v[0])) remote_.valid = false;
        if (paired_) Unpair(unpair::remote_end);
        return;
    case op::received:
    case op::reject:
        if (!PushControl(entry, false)) Overflow(kDropControl, now_ms);
        return;
    case op::transition:
    case op::seed:
    case op::seeds_end:
    case op::follow_ack:
        // Mission control must work while the peer is loading/unpaired. It
        // still requires the save/role/level HELLO gates of this link session.
        if (!remote_hello_ || remote_saves_ != 1 || remote_level_ < 3 || remote_role_ == config_.role) {
            ++stats_.purged;
            return;
        }
        if ((op.code == op::transition && (!FollowMission(U(op.v[2])) || !FollowMission(U(op.v[3])))) ||
            (op.code != op::transition && !FollowMission(U(op.v[1])))) {
            ++stats_.bad_op;
            return;
        }
        if (!PushControl(entry, false)) Overflow(kDropControl, now_ms);
        return;
    case op::req_mission:
        if (!paired_ || op.epoch != epoch_) { AutoAnswer(op.code, op.v[0], 0, reject::not_paired); return; }
        if (ctrl_size_ >= kControlQueue) { AutoAnswer(op.code, op.v[0], 0, reject::busy); Overflow(kDropControl, now_ms); return; }
        AutoAnswer(op.code, op.v[0], 0, 0);
        PushControl(entry, false);
        return;
    case op::resync:
        if (!paired_) { AutoAnswer(op::resync, op.v[0], 0, reject::not_paired); return; }
        if (ctrl_size_ >= kControlQueue) {
            AutoAnswer(op::resync, op.v[0], 0, reject::busy);
            Overflow(kDropControl, now_ms);
            return;
        }
        AutoAnswer(op::resync, op.v[0], 0, 0);
        PushControl(entry, false);
        return;
    case op::probe_sync:
        if (op.v[1] == 1) {
            for (Fire& f : fires_) {
                if (f.used) continue;
                f = Fire{true, static_cast<std::uint8_t>(op.v[0]), 1, U(op.v[2]), U(op.v[3]), U(op.v[4])};
                return;
            }
            ++stats_.overflow;
        } else {
            PushLocal(op::probe_fire, op.v[0], op.v[1], op.v[2], op.v[3]);
        }
        return;
    default: break;
    }
    // World ops (RESYNC_END too: it stays behind the OUTCOMEs sent before it).
    if (host) {
        if (!paired_ || op.epoch != epoch_) {
            if (spec->request) AutoAnswer(op.code, op.v[0], op.v[1], reject::epoch);
            else ++stats_.purged;
            return;
        }
    } else if (!known_host_epoch_ || op.epoch != known_host_epoch_) {
        ++stats_.purged;
        return;
    }
    if (spec->queue == 0) {
        if (ctrl_size_ >= kControlQueue) {
            if (spec->request && host) AutoAnswer(op.code, op.v[0], 0, reject::busy);
            Overflow(kDropControl, now_ms);
            return;
        }
        if (spec->request && host) AutoAnswer(op.code, op.v[0], 0, 0);
        PushControl(entry, false);
        return;
    }
    if (spec->wire == Wire::datagram) {
        Slot& slot = op.code == op::digest ? digest_ : op.code == op::guest_digest ? guest_digest_ : phase_vec_;
        const bool ticked = op.code != op::phase_vec;
        if (ticked && slot.valid && slot.entry.epoch == op.epoch && !Newer32(U(op.v[0]), U(slot.entry.v[0]))) {
            ++stats_.stale_u;
            return;
        }
        slot.entry = entry;
        slot.valid = true;
        slot.delivered = false;
        return;
    }
    if (world_size_ >= kWorldQueue) {
        if (spec->request && host) AutoAnswer(op.code, op.v[0], op.v[1], reject::busy);
        Overflow(kDropWorld, now_ms);
        return;
    }
    if (spec->request && host) AutoAnswer(op.code, op.v[0], 0, 0);
    PushWorld(entry);
}

void Core::OnHello(const Op& op, std::uint64_t now_ms) noexcept {
    Line("world=hello_rx; mission=%u; location=%u; roster=%u; soldiers=%u; cps=%u; role=%s; level=%u; epoch=%u; saves=%u; "
         "proto=%u; build=%u",
         U(op.v[0]), U(op.v[1]), U(op.v[2]), U(op.v[3]), U(op.v[4]), RoleText(static_cast<std::uint8_t>(op.v[6])), U(op.v[5]),
         U(op.v[8]), U(op.v[9]), U(op.v[7]), U(op.v[10]));
    remote_level_ = static_cast<std::uint8_t>(op.v[5]);
    remote_role_ = static_cast<std::uint8_t>(op.v[6]);
    remote_saves_ = static_cast<std::uint8_t>(op.v[9]);
    remote_hello_ = true;
    hello_since_up_ = true;
    if (remote_saves_ != 1) {
        Line("world=pair_refused; reason=remote_saves");
        if (paired_) Unpair(unpair::link_session);
        return;
    }
    if (remote_role_ == config_.role) {
        Line("world=pair_refused; reason=role");
        return;
    }
    if (config_.role == kRoleGuest) {
        // HELLO also names the host's mission and roster: keep it as the
        // pairing source when no READY of that mission arrived yet.
        if (!remote_.valid || remote_.mission != U(op.v[0]))
            remote_ = Ready{true, U(op.v[0]), U(op.v[2]), static_cast<std::uint16_t>(op.v[3]), static_cast<std::uint16_t>(op.v[4]),
                            static_cast<std::uint16_t>(op.v[8])};
        if (op.v[8]) {
            host_mission_ = U(op.v[0]);
            AdoptHostEpoch(static_cast<std::uint16_t>(op.v[8]));
        }
    }
    TryPair(now_ms);
}

void Core::OnReady(const Op& op, std::uint64_t now_ms) noexcept {
    const auto mission = U(op.v[0]);
    const auto epoch = static_cast<std::uint16_t>(op.v[4]);
    Line("world=ready_rx; mission=%u; epoch=%u; roster=%u; soldiers=%u; cps=%u", mission, unsigned(epoch), U(op.v[1]), U(op.v[2]),
         U(op.v[3]));
    if (paired_ && mission != local_.mission) {
        Unpair(unpair::remote_mission);
        PurgeOtherEpochs(config_.role == kRoleHost ? epoch_ : epoch);
    }
    remote_ = Ready{true, mission, U(op.v[1]), static_cast<std::uint16_t>(op.v[2]), static_cast<std::uint16_t>(op.v[3]), epoch};
    if (config_.role == kRoleHost) {
        if (local_.valid && mission == local_.mission && epoch != epoch_) RequeueReady(now_ms, true);
    } else if (epoch) {
        host_mission_ = mission;
        AdoptHostEpoch(epoch);
    }
    TryPair(now_ms);
}

void Core::AdoptHostEpoch(std::uint16_t epoch) noexcept {
    if (!epoch || epoch == known_host_epoch_) return;
    known_host_epoch_ = epoch;
    ClearGrants();
    PurgeOtherEpochs(epoch);
    if (paired_ && epoch_ != epoch) Unpair(unpair::epoch);
}

void Core::TryPair(std::uint64_t now_ms) noexcept {
    if (paired_ || level_ < 1 || !remote_hello_ || remote_level_ < 1 || remote_saves_ != 1 || remote_role_ == config_.role ||
        !local_.valid || !remote_.valid || remote_.mission != local_.mission)
        return;
    if (config_.role == kRoleHost) {
        if (!epoch_ || remote_.epoch != epoch_) return;
    } else {
        if (!known_host_epoch_ || host_mission_ != local_.mission) return;
        epoch_ = known_host_epoch_;
    }
    paired_ = true;
    eff_ = level_ < remote_level_ ? level_ : remote_level_;
    resync_needed_ = resync_pushed_ = false;
    const bool match = remote_.roster == local_.roster && remote_.soldiers == local_.soldiers;
    PushLocal(op::paired, epoch_, local_.mission, remote_.roster, remote_.soldiers, match ? 1 : 0, eff_);
    Line("world=paired; epoch=%u; mission=%u; roster_local=%u; roster_remote=%u; soldiers_local=%u; soldiers_remote=%u; match=%u; "
         "level=%u; remote_level=%u; eff=%u",
         unsigned(epoch_), local_.mission, local_.roster, remote_.roster, unsigned(local_.soldiers), unsigned(remote_.soldiers),
         match ? 1u : 0u, unsigned(level_), unsigned(remote_level_), unsigned(eff_));
    // The guest re-sends its READY when it carried another epoch.
    if (config_.role == kRoleGuest && sent_epoch_ != epoch_) RequeueReady(now_ms, false);
}

void Core::Unpair(std::uint8_t reason) noexcept {
    ClearGrants();
    if (!paired_) return;
    paired_ = false;
    eff_ = 0;
    PushLocal(op::unpaired, reason);
    Line("world=unpaired; reason=%s", UnpairName(reason));
}

void Core::RequeueReady(std::uint64_t now_ms, bool limited) noexcept {
    if (!have_last_ready_) return;
    if (limited && echo_ms_ && now_ms - echo_ms_ < 2000) return;
    echo_ms_ = now_ms ? now_ms : 1;
    Op ready = last_ready_;
    ready.epoch = TxEpoch();
    ready.v[4] = config_.role == kRoleHost ? epoch_ : (host_mission_ == local_.mission ? known_host_epoch_ : 0);
    if (QueueOut(ready)) {
        sent_epoch_ = static_cast<std::uint16_t>(ready.v[4]);
        last_ready_ = ready;
        ++stats_.tx_r;
    }
}

void Core::PurgeOtherEpochs(std::uint16_t keep) noexcept {
    unsigned kept = 0;
    for (unsigned i = 0; i < world_size_; ++i) {
        const Entry& e = world_[(world_head_ + i) % kWorldQueue];
        if (e.epoch == keep) world_[(world_head_ + kept++) % kWorldQueue] = e;
        else ++stats_.purged;
    }
    world_size_ = kept;
    for (Slot* slot : {&digest_, &guest_digest_, &phase_vec_})
        if (slot->valid && slot->entry.epoch != keep) {
            slot->valid = false;
            ++stats_.purged;
        }
}

void Core::ClearWire() noexcept {
    world_head_ = world_size_ = 0;
    digest_.valid = guest_digest_.valid = phase_vec_.valid = false;
    ClearGrants();
    out_head_ = out_size_ = peeked_ = 0;
    ++generation_;
    // Keep the local ops (GOTO, PROBE, LINK ...); drop what came from the old session.
    unsigned kept = 0;
    constexpr unsigned capacity = kControlQueue + kLocalReserve;
    for (unsigned i = 0; i < ctrl_size_; ++i) {
        const Entry& e = ctrl_[(ctrl_head_ + i) % capacity];
        if (e.local) ctrl_[(ctrl_head_ + kept++) % capacity] = e;
    }
    ctrl_size_ = kept;
}

void Core::OnSession(std::uint32_t session, std::uint64_t now_ms) noexcept {
    ClearWire();
    remote_hello_ = false;
    remote_ = Ready{};
    remote_level_ = remote_saves_ = remote_role_ = 0;
    pair_refused_logged_ = false;
    session_ = session;
    link_known_ = true;
    link_up_ = true;
    link_up_ms_ = now_ms;
    hello_since_up_ = silent_logged_ = false;
    PushLocal(op::link, 1, session);
    if (paired_) Unpair(unpair::link_session);
    else if (level_ >= 3) PushLocal(op::unpaired, unpair::link_session); // Lua transaction history belongs to this session too.
}

void Core::OnLinkState(bool up, std::uint32_t session, std::uint64_t now_ms) noexcept {
    if (up && link_known_ && session != session_) { OnSession(session, now_ms); return; }
    if (link_known_ && up == link_up_) return;
    link_known_ = true;
    if (up) {
        link_up_ms_ = now_ms;
        hello_since_up_ = silent_logged_ = false;
        session_ = session;
    }
    link_up_ = up;
    PushLocal(op::link, up ? 1 : 0, session);
}

CfgReply Core::Cfg(std::uint32_t mission, bool start, std::uint32_t read_mission, std::uint64_t now_ms) noexcept {
    CfgReply reply{};
    if (start) {
        if (pending_level_set_) {
            level_ = pending_level_;
            pending_level_set_ = false;
        }
        Unpair(unpair::local_end);
        // Loading can finish on one PC while the other is still in its old
        // mission. Preserve only W3 control; stale gameplay/local ops are gone.
        unsigned kept = 0;
        constexpr unsigned ctrl_capacity = kControlQueue + kLocalReserve;
        for (unsigned i = 0; i < ctrl_size_; ++i) {
            const Entry& item = ctrl_[(ctrl_head_ + i) % ctrl_capacity];
            if (level_ >= 3 && (item.op == op::transition || item.op == op::seed || item.op == op::seeds_end || item.op == op::follow_ack ||
                               (item.local && item.op == op::unpaired && item.v[0] == unpair::link_session)))
                ctrl_[(ctrl_head_ + kept++) % ctrl_capacity] = item;
        }
        ctrl_size_ = kept;
        world_head_ = world_size_ = 0;
        digest_.valid = guest_digest_.valid = phase_vec_.valid = false;
        local_ = Ready{};
        have_last_ready_ = false;
        sent_epoch_ = 0;
        if (config_.role == kRoleHost) {
            epoch_ = static_cast<std::uint16_t>(epoch_ % 65535 + 1);
            known_host_epoch_ = epoch_;
        } else {
            epoch_ = 0;
        }
        mission_running_ = level_ >= 1;
        start_mission_ = mission;
        switch (goto_.state) {
        case GotoState::step1:
        case GotoState::expect_acc:
            if (IsHeliSpace(mission)) {
                goto_.state = GotoState::step2;
                goto_.deadline_ms = now_ms + 60000;
                goto_.waiting_logged = false;
                PushLocal(op::go_to, 2, goto_.target, goto_.lz);
            } else {
                GotoFail("unexpected_mission", mission);
            }
            break;
        case GotoState::step2: GotoFail("unexpected_mission", mission); break;
        case GotoState::expect_target:
            if (mission == goto_.target) {
                Line("world_goto=arrived; mission=%u; ms=%u", mission, U(static_cast<std::int64_t>(now_ms - goto_.armed_ms)));
                goto_ = Goto{};
            } else {
                GotoFail("unexpected_mission", mission);
            }
            break;
        default: break;
        }
        Line("world=cfg; mission=%u; level=%u; epoch=%u; link=%u; read_mission=%u", mission, unsigned(level_), unsigned(TxEpoch()),
             link_up_ ? 1u : 0u, read_mission);
        if (!config_.play && level_ >= 1 && pending_probe_ != WorldProbe::none) {
            reply.probe = static_cast<std::uint8_t>(pending_probe_);
            Line("world_probe=scheduled; id=%s; when=start", WorldProbeName(pending_probe_));
            pending_probe_ = WorldProbe::none;
        }
    }
    reply.level = level_;
    reply.role = config_.role;
    reply.proto = kProto;
    reply.link = link_up_ ? 1 : 0;
    reply.epoch = TxEpoch();
    reply.mode = config_.play ? kModePlay : kModeLab;
    reply.session = link_up_ ? session_ : 0;
    return reply;
}

TxStatus Core::Tx(std::uint8_t code, const std::int64_t* values, unsigned count, std::uint64_t now_ms, Datagram* datagram) noexcept {
    if (datagram) datagram->size = 0;
    const OpSpec* spec = FindOp(code);
    if (!spec || !spec->lua || (spec->wire == Wire::local && code != op::goto_ack)) return TxStatus::unknown_op;
    if (count != spec->lua || (count && !values)) return TxStatus::bad_args;
    for (unsigned i = 0; i < count; ++i)
        if (!FieldValid(spec->fields[i], values[i])) return TxStatus::bad_args;
    if (!level_ || spec->level > level_) return TxStatus::disabled;
    if (spec->lab_only && config_.play) return TxStatus::lab_only;
    const bool host = config_.role == kRoleHost;
    if ((spec->dir == Dir::host_to_guest && !host) || (spec->dir == Dir::guest_to_host && host)) return TxStatus::wrong_role;
    if (code >= op::transition && code <= op::follow_ack) {
        if (level_ < 3 || !remote_hello_ || remote_level_ < 3 || remote_saves_ != 1 || remote_role_ == config_.role)
            return TxStatus::disabled;
        if (code == op::req_mission && !paired_) return TxStatus::not_paired;
        if ((code == op::transition && (!FollowMission(U(values[2])) || !FollowMission(U(values[3])))) ||
            ((code == op::seed || code == op::seeds_end || code == op::follow_ack) && !FollowMission(U(values[1]))))
            return TxStatus::bad_args;
    }
    if (code == op::goto_ack) {
        GotoTx(values[0], values[1], now_ms);
        return TxStatus::ok;
    }
    if (spec->queue == 1 || (spec->level >= 3 && code > op::follow_ack)) {
        if (!paired_) return TxStatus::not_paired;
        if (eff_ < spec->level) return TxStatus::disabled;
    }
    Op op{};
    op.code = code;
    op.epoch = TxEpoch();
    op.count = static_cast<std::uint8_t>(spec->lua + spec->native);
    for (unsigned i = 0; i < count; ++i) op.v[i] = values[i];
    switch (code) {
    case op::hello:
        op.v[5] = level_;
        op.v[6] = config_.role;
        op.v[7] = kProto;
        op.v[8] = TxEpoch();
        op.v[9] = config_.saves ? 1 : 0;
        op.v[10] = config_.build;
        break;
    case op::ready:
        op.v[4] = host ? epoch_ : (host_mission_ == U(values[0]) ? known_host_epoch_ : 0);
        break;
    case op::mission_end: op.v[1] = TxEpoch(); break;
    case op::align_begin: op.v[5] = TxEpoch(); break;
    case op::align_end: op.v[5] = TxEpoch(); break;
    case op::probe_sync: op.v[4] = values[1] == 1 ? U(static_cast<std::int64_t>((now_ms + U(values[3])) & 0xFFFFFFFFu)) : 0; break;
    default: break;
    }
    if (spec->wire == Wire::datagram) {
        if (!datagram) return TxStatus::disabled;
        const std::size_t n = EncodeOp(op, datagram->bytes + kBundleHeaderSize, sizeof(datagram->bytes) - kBundleHeaderSize);
        if (!n) return TxStatus::bad_args;
        WriteBundleHeader(datagram->bytes, 1);
        datagram->size = kBundleHeaderSize + n;
        ++stats_.tx_u;
        return TxStatus::ok;
    }
    if (!QueueOut(op)) {
        Dropped(kDropOut, now_ms, true);
        return TxStatus::queue_full;
    }
    ++stats_.tx_r;
    switch (code) {
    case op::ready:
        local_ = Ready{true, U(values[0]), U(values[1]), static_cast<std::uint16_t>(values[2]), static_cast<std::uint16_t>(values[3]),
                       static_cast<std::uint16_t>(op.v[4])};
        last_ready_ = op;
        have_last_ready_ = true;
        sent_epoch_ = static_cast<std::uint16_t>(op.v[4]);
        TryPair(now_ms);
        break;
    case op::mission_end:
        mission_running_ = false;
        local_.valid = false;
        Unpair(unpair::local_end);
        break;
    case op::resync:
        resync_needed_ = resync_pushed_ = false;
        break;
    case op::probe_sync:
        if (values[1] == 1) {
            bool placed = false;
            for (Fire& f : fires_) {
                if (f.used) continue;
                f = Fire{true, static_cast<std::uint8_t>(values[0]), 1, U(values[2]), U(values[3]), U(op.v[4])};
                placed = true;
                break;
            }
            if (!placed) ++stats_.overflow;
        }
        break;
    default: break;
    }
    return TxStatus::ok;
}

void Core::Rx(std::uint8_t mask, std::uint32_t rtt_ms, std::uint64_t now_ms, RxReply& out) noexcept {
    out = RxReply{};
    if (last_rx_ms_ && now_ms >= last_rx_ms_) {
        const auto gap = now_ms - last_rx_ms_;
        const std::uint32_t g = gap > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(gap);
        if (g > stats_.rx_gap_ms_max) stats_.rx_gap_ms_max = g;
    }
    last_rx_ms_ = now_ms;
    Tick(now_ms);
    const auto fill = [&](const Entry& e, std::uint8_t queue) {
        out.empty = false;
        out.queue = queue;
        out.op = e.op;
        out.count = e.count;
        for (unsigned i = 0; i < e.count && i < kMaxValues; ++i) out.v[i] = e.v[i];
        const OpSpec* spec = FindOp(e.op);
        if (spec && spec->rx && out.count < kMaxValues) {
            // latencyMs = rtt/2 + time queued here.
            const std::uint64_t queued = now_ms >= e.rx_ms ? now_ms - e.rx_ms : 0;
            const std::uint64_t latency = rtt_ms / 2 + queued;
            out.v[out.count++] = latency > 65535 ? 65535 : static_cast<std::int64_t>(latency);
        }
    };
    if (ctrl_size_) {
        const Entry e = ctrl_[ctrl_head_];
        ctrl_head_ = (ctrl_head_ + 1) % (kControlQueue + kLocalReserve);
        --ctrl_size_;
        fill(e, 0);
        return;
    }
    if ((mask & 2) && paired_ && level_) {
        while (world_size_) {
            const Entry e = world_[world_head_];
            world_head_ = (world_head_ + 1) % kWorldQueue;
            --world_size_;
            if (e.epoch != epoch_) { ++stats_.purged; continue; }
            fill(e, 1);
            if (e.op == op::enemy_debug) RecordGrant(e, now_ms);
            return;
        }
        for (Slot* slot : {&digest_, &guest_digest_, &phase_vec_}) {
            if (!slot->valid || slot->delivered || slot->entry.epoch != epoch_) continue;
            slot->delivered = true;
            fill(slot->entry, 1);
            return;
        }
    }
    out.empty = true;
    out.flags = static_cast<std::uint8_t>((link_up_ ? 1 : 0) | (paired_ ? 2 : 0) | (resync_needed_ ? 4 : 0) | (level_ ? 0 : 8));
    out.qctrl = static_cast<std::uint16_t>(ctrl_size_);
    out.qworld = static_cast<std::uint16_t>(world_size_ > 65535 ? 65535 : world_size_);
    out.epoch = TxEpoch();
}

// Guest only: a warp or correct ENEMY_DEBUG handed to Lua becomes the one
// native placement target for that seq. Slot: the same seq, a free or expired
// one, the oldest correction (a pending warp outlives 1 Hz corrections), else
// the oldest grant.
void Core::RecordGrant(const Entry& e, std::uint64_t now_ms) noexcept {
    if (config_.role != kRoleGuest || e.count < 10 || (e.v[2] != place::warp && e.v[2] != place::correct)) return;
    Grant* slot = nullptr;
    Grant* oldest = nullptr;
    Grant* oldest_correct = nullptr;
    for (Grant& g : grants_) {
        if (g.valid && g.seq == e.v[0] && g.epoch == e.epoch) { slot = &g; break; }
        if (!g.valid || now_ms < g.rx_ms || now_ms - g.rx_ms > kGrantLifeMs) { if (!slot) slot = &g; continue; }
        if (!oldest || g.rx_ms < oldest->rx_ms) oldest = &g;
        if (g.kind == place::correct && (!oldest_correct || g.rx_ms < oldest_correct->rx_ms)) oldest_correct = &g;
    }
    if (!slot) slot = oldest_correct ? oldest_correct : oldest;
    grant_serial_ = grant_serial_ % 0x7FFFFFFFu + 1u;
    *slot = Grant{true, static_cast<std::uint16_t>(e.v[0]), e.epoch, static_cast<std::uint8_t>(e.v[2]), static_cast<std::uint8_t>(e.v[7]),
                  U(e.v[1]), {static_cast<std::int32_t>(e.v[3]), static_cast<std::int32_t>(e.v[4]), static_cast<std::int32_t>(e.v[5])},
                  static_cast<std::int32_t>(e.v[6]), now_ms, grant_serial_};
}

void Core::ClearGrants() noexcept {
    for (Grant& g : grants_) g = Grant{};
}

bool Core::FindGrant(std::uint16_t seq, std::uint64_t now_ms, Grant& out) const noexcept {
    out = Grant{};
    if (config_.role != kRoleGuest || !paired_ || !level_ || !seq) return false;
    for (const Grant& g : grants_) {
        if (!g.valid || g.seq != seq || g.epoch != epoch_ || now_ms < g.rx_ms || now_ms - g.rx_ms > kGrantLifeMs) continue;
        out = g;
        return true;
    }
    return false;
}

unsigned Core::grant_count() const noexcept {
    unsigned n = 0;
    for (const Grant& g : grants_) n += g.valid ? 1u : 0u;
    return n;
}

void Core::RejectTrigger(const char* reason) noexcept { Line("world_trigger=rejected; reason=%s", reason ? reason : "token"); }

void Core::ApplyTrigger(const Trigger& trigger, const char* canonical, std::uint32_t read_mission, bool terminating,
                        std::uint64_t now_ms) noexcept {
    if (!config_.lab_triggers || config_.play) { RejectTrigger("not_lab"); return; }
    if (!level_ && trigger.has_probe && trigger.probe != WorldProbe::none) { RejectTrigger("level0"); return; }
    Line("world_trigger=applied; text=%s", canonical ? canonical : "");
    if (trigger.has_world) {
        if (!trigger.world) {
            level_ = 0;
            pending_level_set_ = false;
            mission_running_ = false;
            // Disable is a Lua transaction reset even during unpaired loading.
            // Old controls must not precede it or survive a later re-enable.
            ClearWire();
            ctrl_head_ = ctrl_size_ = 0;
            pending_probe_ = WorldProbe::none;
            for (Fire& fire : fires_) fire = Fire{};
            goto_ = Goto{};
            resync_needed_ = resync_pushed_ = false;
            if (paired_) Unpair(unpair::level0);
            else PushLocal(op::unpaired, unpair::level0);
            PushLocal(op::level, 0);
        } else {
            pending_level_ = trigger.world;
            pending_level_set_ = true;
        }
    }
    if (trigger.has_probe) {
        if (trigger.probe == WorldProbe::none) {
            pending_probe_ = WorldProbe::none;
        } else if (mission_running_ && level_) {
            PushLocal(op::probe, static_cast<std::uint8_t>(trigger.probe), 0);
            Line("world_probe=scheduled; id=%s; when=trigger", WorldProbeName(trigger.probe));
        } else {
            pending_probe_ = trigger.probe;
            Line("world_probe=scheduled; id=%s; when=start", WorldProbeName(trigger.probe));
        }
    }
    if (trigger.has_goto) {
        const char* refusal = nullptr;
        // From Mother Base or free roam (both hops; free roam rides the helicopter
        // to its ACC, so a continued mission can start fresh) or already at an ACC
        // (step 2 only: a lab save that continues into the helicopter).
        const bool here = read_mission == start_mission_;
        const bool from_mb = here && (read_mission == kMissionMtbsFree || read_mission == kMissionAfghFree ||
                                      read_mission == kMissionMafrFree);
        const bool from_acc = here && IsHeliSpace(read_mission);
        if (!level_) refusal = "level";
        else if (!mission_running_ || (!from_mb && !from_acc)) refusal = "mission";
        else if (trigger.goto_target != 30010 && trigger.goto_target != 30020) refusal = "target";
        else if (goto_.state != GotoState::idle) refusal = "busy";
        else if (terminating) refusal = "terminating";
        if (refusal) {
            Line("world_goto=refused; reason=%s", refusal);
        } else if (from_acc) {
            goto_ = Goto{GotoState::step2, trigger.goto_target, trigger.lz, now_ms, now_ms + 60000, false};
            PushLocal(op::go_to, 2, trigger.goto_target, trigger.lz);
            Line("world_goto=armed; target=%u; lz=%u; from=acc", trigger.goto_target, unsigned(trigger.lz));
        } else {
            goto_ = Goto{GotoState::step1, trigger.goto_target, trigger.lz, now_ms, now_ms + 45000, false};
            PushLocal(op::go_to, 1, trigger.goto_target, trigger.lz);
            Line("world_goto=armed; target=%u; lz=%u", trigger.goto_target, unsigned(trigger.lz));
        }
    }
}

void Core::LocalMissionEnd(std::uint64_t) noexcept {
    mission_running_ = false;
    local_.valid = false;
    Unpair(unpair::local_end);
}

void Core::GotoTx(std::int64_t step, std::int64_t result, std::uint64_t now_ms) noexcept {
    const bool current = (goto_.state == GotoState::step1 && step == 1) || (goto_.state == GotoState::step2 && step == 2);
    if (!current) return;
    if (result == 2) {
        if (!goto_.waiting_logged) Line("world_goto=step; step=%u; result=waiting", U(step));
        goto_.waiting_logged = true;
        return;
    }
    if (result == 1) {
        Line("world_goto=step; step=%u; result=reserved", U(step));
        goto_.state = step == 1 ? GotoState::expect_acc : GotoState::expect_target;
        goto_.deadline_ms = now_ms + 120000;
        goto_.waiting_logged = false;
        return;
    }
    Line("world_goto=step; step=%u; result=refused", U(step));
    goto_ = Goto{};
}

void Core::GotoFail(const char* reason, std::uint32_t mission) noexcept {
    Line("world_goto=failed; reason=%s; mission=%u", reason, mission);
    goto_ = Goto{};
}

void Core::DueFires(std::uint64_t now_ms) noexcept {
    const auto now32 = static_cast<std::uint32_t>(now_ms & 0xFFFFFFFFu);
    for (Fire& f : fires_) {
        if (!f.used || !Due32(now32, f.at)) continue;
        f.used = false;
        PushLocal(op::probe_fire, f.probe, f.step, f.ent, f.arg);
    }
}

void Core::Tick(std::uint64_t now_ms) noexcept {
    DueFires(now_ms);
    if (goto_.state != GotoState::idle && now_ms >= goto_.deadline_ms) {
        const char* reason = goto_.state == GotoState::step1 ? "step1_timeout"
                           : goto_.state == GotoState::expect_target ? "step_timeout" : "acc_timeout";
        GotoFail(reason, start_mission_);
    }
    if (link_up_ && level_ && !hello_since_up_ && !silent_logged_ && now_ms - link_up_ms_ >= 30000) {
        silent_logged_ = true;
        Line("world=remote_silent; since_link_ms=%u", U(static_cast<std::int64_t>(now_ms - link_up_ms_)));
    }
    if ((overflow_pending_[kDropControl] || overflow_pending_[kDropWorld] || overflow_pending_[kDropOut]) &&
        (!overflow_logged_once_ || now_ms - overflow_ms_ >= 10000))
        FlushOverflow(now_ms);
    Stats10s(now_ms);
}

void Core::Stats10s(std::uint64_t now_ms) noexcept {
    if (now_ms - stats_ms_ < 10000) return;
    stats_ms_ = now_ms;
    Stats current = stats_;
    current.q_world_max = stats_.q_world_max;
    const bool changed = !stats_logged_ || std::memcmp(&current, &logged_stats_, sizeof(Stats)) != 0 ||
                         logged_paired_ != paired_ || logged_level_ != level_ || logged_epoch_ != TxEpoch() ||
                         logged_out_ != out_size_ || logged_ctrl_ != ctrl_size_ || logged_world_ != world_size_;
    if (!changed) return;
    stats_logged_ = true;
    logged_stats_ = current;
    logged_paired_ = paired_;
    logged_level_ = level_;
    logged_epoch_ = TxEpoch();
    logged_out_ = out_size_;
    logged_ctrl_ = ctrl_size_;
    logged_world_ = world_size_;
    const Stats& s = current;
    Line("world=stats; level=%u; eff=%u; role=%s; paired=%u; epoch=%u; tx_r=%u; tx_u=%u; rx_r=%u; rx_u=%u; q_ctrl=%u; q_world=%u; "
         "q_world_max=%u; out_q=%u; out_full=%u; overflow=%u; purged=%u; stale_u=%u; bad_bundle=%u; bad_op=%u; wrong_dir=%u; "
         "level_drop=%u; unknown_op=%u; auto_received=%u; auto_reject=%u; rx_gap_ms_max=%u",
         unsigned(level_), unsigned(eff_), RoleText(config_.role), paired_ ? 1u : 0u, unsigned(TxEpoch()), s.tx_r, s.tx_u, s.rx_r,
         s.rx_u, ctrl_size_, world_size_, s.q_world_max, out_size_, s.out_full, s.overflow, s.purged, s.stale_u, s.bad_bundle,
         s.bad_op, s.wrong_dir, s.level_drop, s.unknown_op, s.auto_received, s.auto_reject, s.rx_gap_ms_max);
    // Per-window maxima restart; logged_stats_ keeps the logged values for the comparison.
    stats_.rx_gap_ms_max = 0;
    stats_.q_world_max = world_size_;
    logged_stats_.rx_gap_ms_max = 0;
    logged_stats_.q_world_max = world_size_;
}

std::size_t Core::PeekBundle(std::uint8_t* out, std::size_t capacity, std::uint32_t& generation) noexcept {
    generation = generation_;
    peeked_ = 0;
    if (!out_size_ || !out || capacity < kBundleHeaderSize) return 0;
    const std::size_t limit = capacity < kMaxReliableBundle ? capacity : kMaxReliableBundle;
    std::size_t at = kBundleHeaderSize;
    unsigned n = 0;
    while (n < out_size_ && n < kMaxCount) {
        const OutEntry& e = out_[(out_head_ + n) % kOutboundQueue];
        if (at + e.size > limit) break;
        std::memcpy(out + at, e.bytes, e.size);
        at += e.size;
        ++n;
    }
    if (!n) return 0;
    WriteBundleHeader(out, static_cast<std::uint8_t>(n));
    peeked_ = n;
    return at;
}

void Core::CommitBundle(std::uint32_t generation) noexcept {
    if (generation != generation_ || !peeked_) return;
    const unsigned n = peeked_ < out_size_ ? peeked_ : out_size_;
    out_head_ = (out_head_ + n) % kOutboundQueue;
    out_size_ -= n;
    peeked_ = 0;
}

} // namespace mgscoop::world
