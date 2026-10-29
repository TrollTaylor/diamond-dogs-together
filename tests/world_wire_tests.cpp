// World sync core (docs/world-sync-protocol.md): the op table, the bundle
// codec, the queues, pairing / epoch / effective level, the host's automatic
// RECEIVED/REJECT, TX gating and the lab probe-fire and goto schedulers.
// Two cores (host and guest) talk through an in-memory pump; no Win32.
#include "mgscoop/world_wire.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {
namespace world = mgscoop::world;
namespace op = mgscoop::world::op;
using mgscoop::WorldProbe;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

// The cores are large (static queues): never on the stack.
world::Core g_host, g_guest;

std::vector<std::string> Lines(world::Core& core) {
    std::vector<std::string> out;
    char line[world::kLogLineSize];
    while (core.TakeLine(line, sizeof(line))) out.emplace_back(line);
    return out;
}
bool HasLine(const std::vector<std::string>& lines, const char* text) {
    for (const auto& l : lines)
        if (l.find(text) != std::string::npos) return true;
    return false;
}
world::TxStatus Tx(world::Core& core, std::uint8_t code, std::initializer_list<std::int64_t> values, std::uint64_t now,
                   world::Datagram* datagram = nullptr) {
    std::vector<std::int64_t> v(values);
    static world::Datagram scratch;
    return core.Tx(code, v.data(), static_cast<unsigned>(v.size()), now, datagram ? datagram : &scratch);
}
// Moves every queued R bundle from `from` to `to`.
unsigned Pump(world::Core& from, world::Core& to, std::uint64_t now) {
    std::uint8_t buffer[world::kMaxReliableBundle];
    unsigned bundles = 0;
    for (;;) {
        std::uint32_t generation = 0;
        const std::size_t n = from.PeekBundle(buffer, sizeof(buffer), generation);
        if (!n) break;
        Check(n <= world::kMaxReliableBundle, "bundle <= 1024");
        from.CommitBundle(generation);
        to.OnReliable(buffer, n, now);
        ++bundles;
    }
    return bundles;
}
void Exchange(std::uint64_t now) {
    for (int i = 0; i < 4; ++i) {
        Pump(g_host, g_guest, now);
        Pump(g_guest, g_host, now);
    }
}
std::vector<world::RxReply> Drain(world::Core& core, std::uint8_t mask, std::uint64_t now) {
    std::vector<world::RxReply> out;
    for (int i = 0; i < 10000; ++i) {
        world::RxReply r{};
        core.Rx(mask, 40, now, r);
        if (r.empty) break;
        out.push_back(r);
    }
    return out;
}
const world::RxReply* Find(const std::vector<world::RxReply>& list, std::uint8_t code) {
    for (const auto& r : list)
        if (r.op == code) return &r;
    return nullptr;
}
unsigned Count(const std::vector<world::RxReply>& list, std::uint8_t code) {
    unsigned n = 0;
    for (const auto& r : list) n += r.op == code ? 1u : 0u;
    return n;
}

void Setup(std::uint8_t host_level = 3, std::uint8_t guest_level = 3, bool play = false) {
    world::Config h{};
    h.level = host_level;
    h.role = world::kRoleHost;
    h.play = play;
    h.lab_triggers = !play;
    h.build = 1100;
    world::Config g = h;
    g.level = guest_level;
    g.role = world::kRoleGuest;
    g_host.Configure(h, 0);
    g_guest.Configure(g, 0);
    Lines(g_host);
    Lines(g_guest);
}
// Both at `mission`, link up, HELLO + READY exchanged; returns the PAIRED values.
void Pair(std::uint32_t mission, std::uint64_t now, std::uint32_t host_roster = 77, std::uint32_t guest_roster = 77) {
    g_host.OnLinkState(true, 1, now);
    g_guest.OnLinkState(true, 1, now);
    g_host.Cfg(mission, true, mission, now);
    g_guest.Cfg(mission, true, mission, now);
    Check(Tx(g_host, op::hello, {mission, 1, host_roster, 10, 3}, now) == world::TxStatus::ok, "host HELLO");
    Check(Tx(g_host, op::ready, {mission, host_roster, 10, 3}, now) == world::TxStatus::ok, "host READY");
    Check(Tx(g_guest, op::hello, {mission, 1, guest_roster, 10, 3}, now) == world::TxStatus::ok, "guest HELLO");
    Check(Tx(g_guest, op::ready, {mission, guest_roster, 10, 3}, now) == world::TxStatus::ok, "guest READY");
    Exchange(now);
}

void Table() {
    std::size_t count = 0;
    const world::OpSpec* ops = world::OpTable(count);
    Check(count == 55, "55 ops (world, rescue, companion, ride body, horse cargo, fast ride, ride tilt, vehicle gone, M1 companion "
                       "and character, and local ops)");
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = i + 1; j < count; ++j) Check(ops[i].code != ops[j].code, "codes unique");
        const unsigned fields = ops[i].lua + ops[i].native + ops[i].rx;
        Check(fields <= world::kMaxValues, "at most 15 values");
        if (ops[i].wire == world::Wire::local) {
            Check(ops[i].code >= op::first_local && ops[i].body == 0, "local ops are 0x70+ and have no body");
            world::Op o{};
            o.code = ops[i].code;
            o.count = static_cast<std::uint8_t>(ops[i].native + ops[i].lua);
            std::uint8_t out[64];
            Check(world::EncodeOp(o, out, sizeof(out)) == 0, "local ops are never encodable");
            continue;
        }
        Check(ops[i].code < op::first_local, "wire ops below 0x70");
        std::size_t body = 0;
        for (unsigned f = 0; f < static_cast<unsigned>(ops[i].lua + ops[i].native); ++f) body += world::FieldSize(ops[i].fields[f].type);
        Check(body == ops[i].body, "body size matches the fields");
    }
    struct Size { std::uint8_t code; std::uint8_t body; };
    const Size sizes[] = {{op::hello, 24}, {op::ready, 14}, {op::received, 3}, {op::reject, 11}, {op::resync, 3}, {op::resync_end, 7},
                          {op::mission_end, 6}, {op::probe_sync, 14}, {op::enemy_outcome, 25}, {op::req_outcome, 28}, {op::phase, 11},
                          {op::req_phase, 24}, {op::player_area, 5}, {op::digest, 22}, {op::guest_digest, 22}, {op::phase_vec, 53},
                          {op::clock, 11}, {op::weather, 7}, {op::checkpoint, 22},
                          {op::enemy_debug, 30}, {op::enemy_debug_ack, 12}};
    for (const auto& s : sizes) Check(world::FindOp(s.code) && world::FindOp(s.code)->body == s.body, "documented body size");
    for (unsigned code = 0x1D; code <= 0x1F; ++code) Check(!world::FindOp(static_cast<std::uint8_t>(code)), "reserved world codes");
    Check(!world::FindOp(0) && !world::FindOp(0x31) && !world::FindOp(0x79) && !world::FindOp(0xFF), "unknown codes");
    Check(world::FindOp(op::ride_fast) && world::FindOp(op::ride_fast)->body == 23 &&
              world::FindOp(op::ride_fast)->wire == world::Wire::datagram, "RIDE_FAST is a 23-byte datagram");
    Check(world::FindOp(op::phase_vec)->lua == 15 && world::FindOp(op::digest)->rx == 1 && world::FindOp(op::clock)->rx == 1,
          "PHASE_VEC 15 fields, DIGEST/CLOCK receiver latency");
    Check(world::FindOp(op::req_outcome)->request && world::FindOp(op::req_phase)->request && world::FindOp(op::resync)->request &&
              !world::FindOp(op::enemy_outcome)->request,
          "requests");
    Check(world::FindOp(op::probe_sync)->lab_only && world::FindOp(op::goto_ack)->lab_only, "lab-only ops");
}

// Encodes `code` with every field at `pick` (0 min, 1 mid, 2 max) and decodes it back.
void RoundTrip(const world::OpSpec& spec, int pick) {
    world::Op o{};
    o.code = spec.code;
    o.epoch = static_cast<std::uint16_t>(pick == 2 ? 65535 : pick * 7);
    o.count = static_cast<std::uint8_t>(spec.lua + spec.native);
    for (unsigned f = 0; f < o.count; ++f) {
        const auto& field = spec.fields[f];
        o.v[f] = pick == 0 ? field.min : pick == 2 ? field.max : field.min + (field.max - field.min) / 2;
    }
    std::uint8_t bundle[world::kMaxReliableBundle];
    const std::size_t n = world::EncodeOp(o, bundle + world::kBundleHeaderSize, sizeof(bundle) - world::kBundleHeaderSize);
    Check(n == world::kOpHeaderSize + spec.body, "encoded size");
    world::WriteBundleHeader(bundle, 1);
    world::BundleReader reader;
    Check(reader.Open(bundle, world::kBundleHeaderSize + n), "bundle opens");
    world::Op back{};
    world::OpStatus status{};
    Check(reader.Next(back, status) && status == world::OpStatus::ok, "op decodes");
    Check(back.code == o.code && back.epoch == o.epoch && back.count == o.count, "header round trip");
    for (unsigned f = 0; f < o.count; ++f) Check(back.v[f] == o.v[f], "value round trip");
    Check(!reader.Next(back, status), "one op only");
}

void Codec() {
    std::size_t count = 0;
    const world::OpSpec* ops = world::OpTable(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (ops[i].wire == world::Wire::local) continue;
        for (int pick = 0; pick < 3; ++pick) RoundTrip(ops[i], pick);
    }
    // Out-of-range values and wrong counts are refused.
    world::Op o{};
    o.code = op::enemy_outcome;
    o.count = 11;
    std::int64_t good[11] = {4294967295LL, 1, 5, 1, 0, 0, 0, 0, -2147483647LL - 1, 2147483647LL, -1};
    std::memcpy(o.v, good, sizeof(good));
    std::uint8_t out[64];
    Check(world::EncodeOp(o, out, sizeof(out)) == 29, "u32 max and i32 extremes encode");
    o.v[2] = 8;
    Check(world::EncodeOp(o, out, sizeof(out)) == 0, "state 8 refused");
    o.v[2] = 5;
    o.v[3] = 0;
    Check(world::EncodeOp(o, out, sizeof(out)) == 0, "ver 0 refused");
    o.v[3] = 1;
    o.v[8] = 2147483648LL;
    Check(world::EncodeOp(o, out, sizeof(out)) == 0, "i32 overflow refused");
    o.v[8] = 0;
    o.count = 10;
    Check(world::EncodeOp(o, out, sizeof(out)) == 0, "wrong count refused");
    o.count = 11;
    Check(world::EncodeOp(o, out, 10) == 0, "short buffer refused");
    world::Op w{};
    w.code = op::weather;
    w.count = 5;
    std::int64_t weather[5] = {255, 20, 65535, 255, 1};
    std::memcpy(w.v, weather, sizeof(weather));
    Check(world::EncodeOp(w, out, sizeof(out)) == 11, "weather 255 (release) is valid");
    w.v[0] = 6;
    Check(world::EncodeOp(w, out, sizeof(out)) == 0, "weather 6 refused");
    w.v[0] = 2;
    w.v[2] = 1000;
    Check(world::EncodeOp(w, out, sizeof(out)) == 0, "fog density is always none in W2");

    // Several ops: order kept; bad magic/version/overrun drop the bundle;
    // unknown ops are skipped; a len mismatch or a bad value drops one op.
    std::uint8_t bundle[256];
    world::Op a{};
    a.code = op::player_area;
    a.count = 2;
    a.v[1] = 30010;
    // header + good + unknown + bad len + bad value + local op + good.
    std::size_t at = world::kBundleHeaderSize;
    a.v[0] = 3;
    at += world::EncodeOp(a, bundle + at, sizeof(bundle) - at);
    const std::uint8_t unknown[] = {0x1D, 2, 0, 0, 0xEE, 0xEE};
    std::memcpy(bundle + at, unknown, sizeof(unknown));
    at += sizeof(unknown);
    const std::uint8_t bad_len[] = {op::player_area, 4, 0, 0, 1, 2, 3, 4};
    std::memcpy(bundle + at, bad_len, sizeof(bad_len));
    at += sizeof(bad_len);
    const std::uint8_t bad_value[] = {op::player_area, 5, 0, 0, 16, 0, 0, 0, 0};  // area 16
    std::memcpy(bundle + at, bad_value, sizeof(bad_value));
    at += sizeof(bad_value);
    const std::uint8_t local_op[] = {op::paired, 0, 0, 0};
    std::memcpy(bundle + at, local_op, sizeof(local_op));
    at += sizeof(local_op);
    a.v[0] = 4;
    at += world::EncodeOp(a, bundle + at, sizeof(bundle) - at);
    world::WriteBundleHeader(bundle, 6);
    world::BundleReader reader;
    Check(reader.Open(bundle, at), "mixed bundle opens");
    world::Op d{};
    world::OpStatus s{};
    std::vector<world::OpStatus> statuses;
    std::vector<std::int64_t> areas;
    while (reader.Next(d, s)) {
        statuses.push_back(s);
        if (s == world::OpStatus::ok) areas.push_back(d.v[0]);
    }
    Check(statuses.size() == 6 && statuses[0] == world::OpStatus::ok && statuses[1] == world::OpStatus::unknown_op &&
              statuses[2] == world::OpStatus::bad_op && statuses[3] == world::OpStatus::bad_op &&
              statuses[4] == world::OpStatus::bad_op && statuses[5] == world::OpStatus::ok,
          "ok, unknown skipped, len mismatch, bad value and local op dropped one by one");
    Check(areas.size() == 2 && areas[0] == 3 && areas[1] == 4, "order kept");
    std::uint8_t copy[256];
    std::memcpy(copy, bundle, at);
    copy[0] = 0x58;
    Check(!reader.Open(copy, at), "bad magic drops the bundle");
    std::memcpy(copy, bundle, at);
    copy[1] = 2;
    Check(!reader.Open(copy, at), "bad version drops the bundle");
    std::memcpy(copy, bundle, at);
    copy[3] = 1;
    Check(!reader.Open(copy, at), "flags (chunking) not supported yet");
    std::memcpy(copy, bundle, at);
    copy[2] = 7;
    Check(!reader.Open(copy, at), "count overrun drops the bundle");
    Check(!reader.Open(bundle, at - 1), "len overrun drops the bundle");
    std::memcpy(copy, bundle, at);
    copy[at] = 0;
    Check(!reader.Open(copy, at + 1), "trailing bytes drop the bundle");
    Check(!reader.Open(bundle, 3), "shorter than the header");
}

void Pairing() {
    // Host first, then guest.
    Setup();
    Pair(30050, 1000);
    Check(g_host.paired() && g_guest.paired(), "both paired");
    Check(g_host.epoch() == 1 && g_guest.epoch() == 1 && g_guest.host_epoch() == 1, "epoch 1 adopted");
    Check(g_host.eff() == 3 && g_guest.eff() == 3, "eff 3");
    auto rx = Drain(g_host, 1, 1000);
    const auto* p = Find(rx, op::paired);
    Check(p && p->queue == 0 && p->v[0] == 1 && p->v[1] == 30050 && p->v[2] == 77 && p->v[3] == 10 && p->v[4] == 1 && p->v[5] == 3,
          "PAIRED{epoch, mission, roster, soldiers, match, eff}");
    Check(Find(rx, op::link) == nullptr, "a CFG start drops the older control entries (LINK came before it)");
    g_host.OnLinkState(false, 1, 1000);
    g_host.OnLinkState(false, 1, 1000);
    g_host.OnLinkState(true, 1, 1000);
    auto links = Drain(g_host, 1, 1000);
    Check(links.size() == 2 && links[0].op == op::link && links[0].v[0] == 0 && links[1].v[0] == 1 && links[1].v[1] == 1,
          "LINK on every state change, deduplicated");
    auto lines = Lines(g_guest);
    Check(HasLine(lines, "world=paired; epoch=1; mission=30050; roster_local=77; roster_remote=77; soldiers_local=10; soldiers_remote=10; "
                         "match=1; level=3; remote_level=3; eff=3"),
          "paired line");
    Check(HasLine(lines, "world=hello_rx; mission=30050; location=1; roster=77; soldiers=10; cps=3; role=host; level=3; epoch=1; saves=1; "
                         "proto=1; build=1100"),
          "hello_rx line");
    Check(HasLine(lines, "world=ready_rx; mission=30050; epoch=1"), "ready_rx line");

    // Guest first (it starts before the host): its READY carries epoch 0; the
    // host echoes its READY and the guest re-sends with the adopted epoch.
    Setup();
    g_host.OnLinkState(true, 1, 0);
    g_guest.OnLinkState(true, 1, 0);
    g_guest.Cfg(30010, true, 30010, 0);
    Tx(g_guest, op::hello, {30010, 1, 5, 10, 3}, 0);
    Tx(g_guest, op::ready, {30010, 5, 10, 3}, 0);
    Exchange(0);
    Check(!g_host.paired() && !g_guest.paired(), "not paired before the host starts");
    g_host.Cfg(30010, true, 30010, 5000);
    Tx(g_host, op::hello, {30010, 1, 9, 10, 3}, 5000);
    Tx(g_host, op::ready, {30010, 9, 10, 3}, 5000);
    Exchange(5000);
    Check(g_host.paired() && g_guest.paired() && g_guest.epoch() == g_host.epoch(), "both orders pair");
    rx = Drain(g_guest, 1, 5000);
    p = Find(rx, op::paired);
    Check(p && p->v[4] == 0 && p->v[2] == 9, "roster mismatch reported (match=0)");

    // Stale-epoch READY from the guest (it saw an old host HELLO) gets the echo.
    Setup();
    Pair(30050, 0);
    Check(g_host.paired(), "paired");
    g_host.Cfg(30010, true, 30010, 100);  // host epoch 2, guest still knows 1
    Check(!g_host.paired() && g_host.epoch() == 2, "a CFG start unpairs and bumps the host epoch");
    Lines(g_host);
    g_guest.Cfg(30010, true, 30010, 100);
    Tx(g_guest, op::hello, {30010, 1, 5, 10, 3}, 100);
    Tx(g_guest, op::ready, {30010, 5, 10, 3}, 100);
    Pump(g_guest, g_host, 100);
    Tx(g_host, op::hello, {30010, 1, 5, 10, 3}, 100);
    Tx(g_host, op::ready, {30010, 5, 10, 3}, 100);
    Exchange(100);
    Check(g_host.paired() && g_guest.paired() && g_guest.epoch() == 2, "stale epoch resolved");

    // A remote READY for another mission unpairs (2) and purges.
    Setup();
    Pair(30050, 0);
    Drain(g_host, 1, 0);
    g_guest.Cfg(30010, true, 30010, 10);
    Tx(g_guest, op::ready, {30010, 5, 10, 3}, 10);
    Pump(g_guest, g_host, 10);
    rx = Drain(g_host, 1, 10);
    p = Find(rx, op::unpaired);
    Check(!g_host.paired() && p && p->v[0] == 2, "UNPAIRED remote_mission");

    // A remote MISSION_END unpairs (4).
    Setup();
    Pair(30050, 0);
    Drain(g_guest, 1, 0);
    Check(Tx(g_host, op::mission_end, {30050}, 10) == world::TxStatus::ok, "MISSION_END TX");
    Check(!g_host.paired(), "local MISSION_END unpairs the host");
    Pump(g_host, g_guest, 10);
    rx = Drain(g_guest, 1, 10);
    p = Find(rx, op::unpaired);
    Check(!g_guest.paired() && p && p->v[0] == 4, "UNPAIRED remote_end");

    // A new session clears every queue, bumps the generation, then LINK + UNPAIRED.
    Setup();
    Pair(30050, 0);
    Drain(g_guest, 1, 0);
    Tx(g_guest, op::resync, {9, 7}, 0);
    Check(g_guest.outbound_size() > 0, "outbound queued");
    const auto generation = g_guest.generation();
    g_guest.OnSession(2, 50);
    Check(g_guest.outbound_size() == 0 && g_guest.world_size() == 0 && g_guest.generation() == generation + 1, "cleared");
    rx = Drain(g_guest, 1, 50);
    Check(rx.size() == 2 && rx[0].op == op::link && rx[0].v[0] == 1 && rx[0].v[1] == 2 && rx[1].op == op::unpaired &&
              rx[1].v[0] == 1,
          "LINK then UNPAIRED{1}");
    Check(g_guest.host_epoch() == 1, "the host epoch survives a new session");

    // Epoch wrap 65535 -> 1.
    Setup();
    for (unsigned i = 0; i < 65535; ++i) g_host.Cfg(30050, true, 30050, i);
    Check(g_host.epoch() == 65535, "epoch 65535");
    g_host.Cfg(30050, true, 30050, 70000);
    Check(g_host.epoch() == 1, "epoch wraps to 1 (never 0)");
    Lines(g_host);

    // eff = min(local, remote): a level-2 guest.
    Setup(3, 2);
    Pair(30010, 0);
    Check(g_host.eff() == 2 && g_guest.eff() == 2, "eff = min");
    Check(Tx(g_host, op::clock, {100, 2000, 1, 5}, 0) == world::TxStatus::disabled, "W2 refused at eff 2");
    Check(Tx(g_host, op::enemy_outcome, {7, 1, 5, 1, 1, 0, 0, 0, 0, 0, 0}, 0) == world::TxStatus::ok, "W1 allowed at eff 2");

    // A remote HELLO with saves=0 is never paired.
    Setup();
    g_host.OnLinkState(true, 1, 0);
    g_host.Cfg(30050, true, 30050, 0);
    Tx(g_host, op::hello, {30050, 1, 5, 10, 3}, 0);
    Tx(g_host, op::ready, {30050, 5, 10, 3}, 0);
    world::Op hello{};
    hello.code = op::hello;
    hello.count = 11;
    const std::int64_t hv[11] = {30050, 1, 5, 10, 3, 3, 2, 1, 0, 0, 1100};
    std::memcpy(hello.v, hv, sizeof(hv));
    world::Op ready{};
    ready.code = op::ready;
    ready.epoch = 1;
    ready.count = 5;
    const std::int64_t rv[5] = {30050, 5, 10, 3, 1};
    std::memcpy(ready.v, rv, sizeof(rv));
    std::uint8_t bundle[128];
    std::size_t at = world::kBundleHeaderSize;
    at += world::EncodeOp(hello, bundle + at, sizeof(bundle) - at);
    at += world::EncodeOp(ready, bundle + at, sizeof(bundle) - at);
    world::WriteBundleHeader(bundle, 2);
    g_host.OnReliable(bundle, at, 0);
    Check(!g_host.paired() && HasLine(Lines(g_host), "world=pair_refused; reason=remote_saves"), "saves=0 never pairs");

    // remote_silent: link up and no HELLO within 30 s.
    Setup();
    g_host.OnLinkState(true, 4, 1000);
    g_host.Tick(30999);
    Check(!HasLine(Lines(g_host), "remote_silent"), "not before 30 s");
    g_host.Tick(31000);
    Check(HasLine(Lines(g_host), "world=remote_silent; since_link_ms=30000"), "remote_silent after 30 s");
    g_host.Tick(90000);
    Check(!HasLine(Lines(g_host), "remote_silent"), "once per link up");

    // RX(3) returns no world op until paired (queued, epoch known).
    Setup();
    Pair(30050, 0);
    g_guest.Cfg(30010, true, 30010, 10);  // guest unpaired, still knows host epoch 1
    world::Op outcome{};
    outcome.code = op::enemy_outcome;
    outcome.epoch = 1;
    outcome.count = 11;
    const std::int64_t ov[11] = {7, 1, 5, 1, 1, 0, 0, 0, 0, 0, 0};
    std::memcpy(outcome.v, ov, sizeof(ov));
    at = world::kBundleHeaderSize;
    at += world::EncodeOp(outcome, bundle + at, sizeof(bundle) - at);
    world::WriteBundleHeader(bundle, 1);
    g_guest.OnReliable(bundle, at, 10);
    Check(g_guest.world_size() == 1, "queued before pairing");
    rx = Drain(g_guest, 3, 10);
    Check(!Find(rx, op::enemy_outcome), "not drained while unpaired");
    g_host.Cfg(30010, true, 30010, 10);  // epoch 2: the queued epoch-1 op is purged on adoption
    Tx(g_host, op::hello, {30010, 1, 5, 10, 3}, 10);
    Tx(g_host, op::ready, {30010, 5, 10, 3}, 10);
    Tx(g_guest, op::hello, {30010, 1, 5, 10, 3}, 10);
    Tx(g_guest, op::ready, {30010, 5, 10, 3}, 10);
    Exchange(10);
    Check(g_guest.paired() && g_guest.world_size() == 0 && g_guest.stats().purged >= 1, "a new host epoch purges older world ops");
    Check(Tx(g_host, op::enemy_outcome, {7, 1, 5, 1, 1, 0, 0, 0, 0, 0, 0}, 20) == world::TxStatus::ok, "host outcome");
    Pump(g_host, g_guest, 20);
    rx = Drain(g_guest, 1, 20);
    Check(!Find(rx, op::enemy_outcome), "mask 1: control only");
    rx = Drain(g_guest, 3, 20);
    p = Find(rx, op::enemy_outcome);
    Check(p && p->queue == 1 && p->v[0] == 7 && p->v[2] == 5, "mask 3: the world op once paired");

    // ---- The remote scene and the pair-wait reason (both roles).
    namespace wait = world::pair_wait;
    Setup();
    Check(g_host.pair_wait() == wait::no_link && g_host.remote_mission() == 0 && g_host.local_mission() == 0 && !g_host.linked() &&
              g_host.remote_location() == 0,
          "configured, link down: no_link and nothing known");
    g_host.OnLinkState(true, 1, 1000);
    g_guest.OnLinkState(true, 1, 1000);
    Check(g_host.pair_wait() == wait::no_hello && g_guest.pair_wait() == wait::no_hello, "link up, no HELLO yet");
    g_host.Cfg(30050, true, 30050, 1000);
    g_guest.Cfg(30010, true, 30010, 1000);
    Check(Tx(g_host, op::hello, {30050, 1, 77, 10, 3}, 1000) == world::TxStatus::ok &&
              Tx(g_host, op::ready, {30050, 77, 10, 3}, 1000) == world::TxStatus::ok &&
              Tx(g_guest, op::hello, {30010, 1, 66, 10, 3}, 1000) == world::TxStatus::ok &&
              Tx(g_guest, op::ready, {30010, 66, 10, 3}, 1000) == world::TxStatus::ok,
          "different missions: HELLO and READY both ways");
    Exchange(1000);
    Check(!g_host.paired() && !g_guest.paired(), "different missions never pair");
    Check(g_host.pair_wait() == wait::different_mission && g_guest.pair_wait() == wait::different_mission,
          "both report different_mission");
    Check(g_host.remote_mission() == 30010 && g_guest.remote_mission() == 30050 && g_host.local_mission() == 30050 &&
              g_guest.local_mission() == 30010,
          "each side knows the other's mission (host from READY, guest from HELLO/READY)");
    Check(g_host.remote_location() == 1 && g_guest.remote_location() == 1, "the HELLO location is kept on both roles");
    Check(g_host.linked() && g_guest.linked() && g_host.remote_level() == 3 && g_guest.remote_level() == 3, "linked at level 3");
    Lines(g_host);
    Lines(g_guest);
    g_host.Tick(1100);
    g_host.Tick(1200);
    g_host.Tick(1300);
    auto waits = Lines(g_host);
    Check(waits.size() == 1 && waits[0] == "world=pair_wait; reason=different_mission; local=30050; remote=30010; remote_level=3",
          "three ticks log the reason once");
    g_guest.Tick(1100);
    Check(HasLine(Lines(g_guest), "world=pair_wait; reason=different_mission; local=30010; remote=30050; remote_level=3"),
          "the guest logs its own view");
    // A remote MISSION_END clears the partner's mission: remote_loading.
    Check(Tx(g_guest, op::mission_end, {30010}, 1400) == world::TxStatus::ok, "guest MISSION_END");
    Pump(g_guest, g_host, 1400);
    Check(g_host.remote_mission() == 0 && g_host.pair_wait() == wait::remote_loading && g_host.linked(), "host: remote_loading");
    Check(g_guest.local_mission() == 0 && g_guest.pair_wait() == wait::local_loading, "guest: local_loading");
    g_host.Tick(1500);
    waits = Lines(g_host);
    Check(waits.size() == 1 && waits[0] == "world=pair_wait; reason=remote_loading; local=30050; remote=0; remote_level=3",
          "a changed reason logs again");
    Check(Tx(g_host, op::mission_end, {30050}, 1600) == world::TxStatus::ok, "host MISSION_END");
    Pump(g_host, g_guest, 1600);
    Check(g_guest.remote_mission() == 0 && g_guest.pair_wait() == wait::local_loading, "guest: the host's mission is gone too");
    // A new session forgets the partner: no_hello.
    g_host.OnSession(2, 1700);
    Check(g_host.remote_mission() == 0 && g_host.pair_wait() == wait::no_hello && g_host.remote_location() == 0 && !g_host.linked() &&
              g_host.remote_level() == 0,
          "after a new session: no_hello, location reset");
    // Link down: no_link, masked values, no pair_wait line while down.
    Tx(g_guest, op::hello, {30010, 2, 66, 10, 3}, 1800);
    Pump(g_guest, g_host, 1800);
    Check(g_host.remote_location() == 2 && g_host.linked() && g_host.remote_level() == 3, "a fresh HELLO on the new session");
    Lines(g_host);
    g_host.OnLinkState(false, 2, 1900);
    Check(g_host.remote_mission() == 0 && g_host.pair_wait() == wait::no_link && !g_host.linked() && g_host.remote_level() == 0,
          "link down: no_link, nothing remote");
    g_host.Tick(2000);
    g_host.Tick(2100);
    Check(!HasLine(Lines(g_host), "world=pair_wait"), "no pair_wait line while the link is down");
    g_host.OnLinkState(true, 2, 2200);
    g_host.Tick(2300);
    Check(HasLine(Lines(g_host), "world=pair_wait; reason=local_loading; local=0; remote=0; remote_level=3"),
          "the link coming back logs again");
    // HELLO refusals: saves 0 (remote_saves), the same role (same_role); the
    // location is kept before either refusal.
    const auto inject_hello = [](world::Core& core, std::int64_t location, std::int64_t role, std::int64_t saves, std::int64_t level) {
        world::Op h{};
        h.code = op::hello;
        h.count = 11;
        const std::int64_t values[11] = {30050, location, 5, 10, 3, level, role, 1, 0, saves, 1100};
        std::memcpy(h.v, values, sizeof(values));
        std::uint8_t b[64];
        const std::size_t n = world::kBundleHeaderSize + world::EncodeOp(h, b + world::kBundleHeaderSize, sizeof(b) - world::kBundleHeaderSize);
        world::WriteBundleHeader(b, 1);
        core.OnReliable(b, n, 2400);
    };
    Setup();
    g_host.OnLinkState(true, 1, 2400);
    g_host.Cfg(30050, true, 30050, 2400);
    inject_hello(g_host, 6, world::kRoleGuest, 1, 0);
    Check(g_host.pair_wait() == wait::remote_off && g_host.remote_location() == 6 && !g_host.linked() && g_host.remote_level() == 0,
          "a level-0 HELLO: remote_off");
    inject_hello(g_host, 7, world::kRoleGuest, 0, 3);
    Check(g_host.pair_wait() == wait::remote_saves && g_host.remote_location() == 7 && !g_host.linked(), "saves=0: remote_saves");
    inject_hello(g_host, 8, world::kRoleHost, 1, 3);
    Check(g_host.pair_wait() == wait::same_role && g_host.remote_location() == 8 && !g_host.linked(), "the same role: same_role");
    inject_hello(g_host, 9, world::kRoleGuest, 1, 3);
    Check(g_host.pair_wait() == wait::local_loading && g_host.linked(), "a good HELLO with no local READY: local_loading");
    Tx(g_host, op::ready, {30050, 5, 10, 3}, 2400);
    Check(g_host.pair_wait() == wait::remote_loading, "a local READY, no remote READY: remote_loading");
    Setup(0, 3);
    Check(g_host.pair_wait() == wait::off, "level 0: off");
    g_host.OnLinkState(true, 1, 2400);
    g_host.Tick(2500);
    Check(!HasLine(Lines(g_host), "world=pair_wait"), "level 0 never logs pair_wait");
    // Paired: 0 and silent; a CFG start (link up) logs again.
    Setup();
    Pair(30050, 3000);
    Check(g_host.pair_wait() == wait::paired && g_guest.pair_wait() == wait::paired && g_host.remote_mission() == 30050 &&
              g_host.local_mission() == 30050,
          "paired: 0");
    Lines(g_host);
    g_host.Tick(3100);
    g_host.Tick(3200);
    Check(!HasLine(Lines(g_host), "world=pair_wait"), "no pair_wait line while paired");
    g_host.Cfg(30010, true, 30010, 3300);
    g_host.Tick(3400);
    Check(HasLine(Lines(g_host), "world=pair_wait; reason=local_loading; local=0; remote=30050; remote_level=3"),
          "a CFG start unpairs and logs the new reason");
    // The last gate (epoch): every scene gate passes but the epochs disagree.
    Setup();
    g_host.OnLinkState(true, 1, 4000);
    g_guest.OnLinkState(true, 1, 4000);
    g_guest.Cfg(30050, true, 30050, 4000);
    Tx(g_guest, op::hello, {30050, 1, 77, 10, 3}, 4000);
    Tx(g_guest, op::ready, {30050, 77, 10, 3}, 4000);
    Tx(g_host, op::hello, {30050, 1, 77, 10, 3}, 4000);  // before its CFG start: epoch 0
    Pump(g_host, g_guest, 4000);
    Check(!g_guest.paired() && g_guest.pair_wait() == wait::epoch && g_guest.remote_mission() == 30050,
          "guest: the same scene, no host epoch yet");
    g_host.Cfg(30050, true, 30050, 4000);
    Tx(g_host, op::ready, {30050, 77, 10, 3}, 4000);
    Pump(g_guest, g_host, 4000);
    Check(!g_host.paired() && g_host.pair_wait() == wait::epoch && g_host.remote_mission() == 30050,
          "host: the guest's READY carries another epoch");
    Exchange(4000);
    Check(g_host.paired() && g_guest.paired() && g_host.pair_wait() == wait::paired && g_guest.pair_wait() == wait::paired,
          "the READY echo pairs both");
    for (std::uint8_t code = 0; code <= 11; ++code)
        Check(std::strcmp(world::PairWaitName(code), "unknown") != 0, "every pair-wait code has a name");
    Check(std::strcmp(world::PairWaitName(0), "paired") == 0 && std::strcmp(world::PairWaitName(1), "off") == 0 &&
              std::strcmp(world::PairWaitName(2), "no_link") == 0 && std::strcmp(world::PairWaitName(3), "no_hello") == 0 &&
              std::strcmp(world::PairWaitName(4), "remote_off") == 0 && std::strcmp(world::PairWaitName(5), "remote_saves") == 0 &&
              std::strcmp(world::PairWaitName(6), "same_role") == 0 && std::strcmp(world::PairWaitName(7), "local_loading") == 0 &&
              std::strcmp(world::PairWaitName(8), "remote_loading") == 0 &&
              std::strcmp(world::PairWaitName(9), "different_mission") == 0 && std::strcmp(world::PairWaitName(10), "epoch") == 0 &&
              std::strcmp(world::PairWaitName(11), "settling") == 0 && std::strcmp(world::PairWaitName(12), "unknown") == 0 &&
              std::strcmp(world::PairWaitName(200), "unknown") == 0,
          "pair-wait names (K2)");
}

// The published pair-wait code: different missions settle for 15 s first.
void Publisher() {
    namespace wait = world::pair_wait;
    world::WaitPublisher w{};
    bool post = true;
    Check(world::kMismatchSettleMs == 15000, "15 s settle");
    Check(w.Update(wait::different_mission, 30050, 30010, 1000, post) == wait::settling && !post, "11 at t");
    Check(w.Update(wait::different_mission, 30050, 30010, 15999, post) == wait::settling && !post, "11 at t+14999");
    Check(w.Update(wait::different_mission, 30050, 30010, 16000, post) == wait::different_mission && post, "9 at t+15000, posted");
    Check(w.Update(wait::different_mission, 30050, 30010, 16001, post) == wait::different_mission && !post, "posted once");
    for (std::uint8_t raw = 0; raw <= wait::epoch; ++raw) {
        if (raw == wait::different_mission) continue;
        Check(w.Update(raw, 30050, 0, 17000, post) == raw && !post, "other codes pass through at once");
    }
    Check(w.Update(wait::different_mission, 30050, 30010, 18000, post) == wait::settling && !post,
          "the same pair after an interruption settles again");
    Check(w.Update(wait::different_mission, 30050, 30010, 33000, post) == wait::different_mission && !post,
          "the same pair never posts twice");
    Check(w.Update(wait::different_mission, 30050, 30020, 34000, post) == wait::settling && !post, "a new remote restarts the timer");
    Check(w.Update(wait::different_mission, 30050, 30020, 48999, post) == wait::settling && !post, "still settling");
    Check(w.Update(wait::different_mission, 30050, 30020, 49000, post) == wait::different_mission && post, "the new pair posts");
    Check(w.Update(wait::different_mission, 40050, 30020, 49001, post) == wait::settling && !post, "a new local restarts the timer");
    // A pair interrupted before it settled starts over when it returns.
    world::WaitPublisher v{};
    Check(v.Update(wait::different_mission, 40050, 30050, 0, post) == wait::settling && !post, "travel: settling");
    Check(v.Update(wait::remote_loading, 40050, 0, 5000, post) == wait::remote_loading, "the partner loads");
    Check(v.Update(wait::different_mission, 40050, 30050, 600000, post) == wait::settling && !post,
          "the same pair much later settles again (no early toast)");
    Check(v.Update(wait::different_mission, 40050, 30050, 614999, post) == wait::settling && !post &&
              v.Update(wait::different_mission, 40050, 30050, 615000, post) == wait::different_mission && post,
          "then posts once after 15 s");
}

// The NOTE rate gate: posts at most once per second, logs once per 10 s.
void Notes() {
    world::NoteGate g{};
    bool post = false, log = false;
    g.Accept(2, 30010, 0, 1000, post, log);
    Check(post && log, "the first note posts and logs");
    g.Accept(2, 30010, 0, 1999, post, log);
    Check(!post && !log, "the same note within 1000 ms: neither");
    g.Accept(2, 30010, 0, 2000, post, log);
    Check(post && !log, "at 1000 ms it posts again; the log waits");
    g.Accept(2, 30010, 0, 10999, post, log);
    Check(post && !log, "logged at most once per 10000 ms");
    g.Accept(2, 30010, 0, 11000, post, log);
    Check(!post && log, "logged again 10000 ms after the last log (the post waits for its own 1000 ms)");
    g.Accept(2, 30010, 1, 11001, post, log);
    Check(post && log, "a different b posts and logs at once");
    g.Accept(3, 30010, 1, 11001, post, log);
    Check(post && log, "each code has its own slot");
    g.Accept(3, 30010, 1, 11500, post, log);
    Check(!post && !log, "that slot gates too");
    g.Accept(32, 1, 1, 11002, post, log);
    Check(!post && !log, "codes above 31 have no slot");
}

void AutoOps() {
    Setup();
    Pair(30010, 0);
    Drain(g_host, 1, 0);
    Drain(g_guest, 1, 0);
    // REQ_OUTCOME -> RECEIVED{rid, reqOp} from the host's native side.
    Check(Tx(g_guest, op::req_outcome, {41, 7, 1, 2, 0, 3, 0, 0, 1, 2, 3}, 0) == world::TxStatus::ok, "REQ_OUTCOME TX");
    Exchange(0);
    auto rx = Drain(g_host, 3, 0);
    const auto* p = Find(rx, op::req_outcome);
    Check(p && p->v[0] == 41 && p->v[1] == 7 && p->v[3] == 2, "the request reaches the host");
    rx = Drain(g_guest, 1, 0);
    p = Find(rx, op::received);
    Check(p && p->v[0] == 41 && p->v[1] == op::req_outcome && g_host.stats().auto_received == 1, "RECEIVED");
    // REQ_PHASE on a wrong epoch -> REJECT 5.
    world::Op req{};
    req.code = op::req_phase;
    req.epoch = 9;
    req.count = 8;
    const std::int64_t qv[8] = {42, 11, 3, 0, 0, 0, 0, 0};
    std::memcpy(req.v, qv, sizeof(qv));
    std::uint8_t bundle[64];
    std::size_t at = world::kBundleHeaderSize + world::EncodeOp(req, bundle + world::kBundleHeaderSize, 60);
    world::WriteBundleHeader(bundle, 1);
    g_host.OnReliable(bundle, at, 0);
    Pump(g_host, g_guest, 0);
    rx = Drain(g_guest, 1, 0);
    p = Find(rx, op::reject);
    Check(p && p->v[0] == 42 && p->v[1] == op::req_phase && p->v[2] == 5 && p->v[3] == 11, "REJECT epoch");
    // A level-1 host answers a W1 request with REJECT 10.
    Setup(1, 3);
    Pair(30010, 0);
    Drain(g_guest, 1, 0);
    req.epoch = g_host.epoch();
    req.v[0] = 43;
    at = world::kBundleHeaderSize + world::EncodeOp(req, bundle + world::kBundleHeaderSize, 60);
    g_host.OnReliable(bundle, at, 0);
    Pump(g_host, g_guest, 0);
    rx = Drain(g_guest, 1, 0);
    p = Find(rx, op::reject);
    Check(p && p->v[0] == 43 && p->v[2] == 10 && g_host.stats().level_drop == 1, "REJECT level");
    // The guest never answers requests: a REQ on the guest is wrong_dir.
    Setup();
    Pair(30010, 0);
    req.epoch = 1;
    at = world::kBundleHeaderSize + world::EncodeOp(req, bundle + world::kBundleHeaderSize, 60);
    g_guest.OnReliable(bundle, at, 0);
    Check(g_guest.stats().wrong_dir == 1 && g_guest.outbound_size() == 0, "no auto ops on the guest");
    // RESYNC unpaired -> REJECT 6; paired -> RECEIVED and the control queue.
    Setup();
    g_host.OnLinkState(true, 1, 0);
    g_host.Cfg(30010, true, 30010, 0);
    world::Op resync{};
    resync.code = op::resync;
    resync.count = 2;
    resync.v[0] = 44;
    resync.v[1] = 7;
    at = world::kBundleHeaderSize + world::EncodeOp(resync, bundle + world::kBundleHeaderSize, 60);
    g_host.OnReliable(bundle, at, 0);
    Check(g_host.stats().auto_reject == 1, "RESYNC unpaired rejected");
}

void Gating() {
    Setup();
    Check(Tx(g_host, op::enemy_outcome, {7, 1, 5, 1, 1, 0, 0, 0, 0, 0, 0}, 0) == world::TxStatus::not_paired, "not paired");
    Check(Tx(g_guest, op::enemy_outcome, {7, 1, 5, 1, 1, 0, 0, 0, 0, 0, 0}, 0) == world::TxStatus::wrong_role, "guest cannot commit");
    Check(Tx(g_host, op::req_outcome, {1, 7, 1, 2, 0, 3, 0, 0, 1, 2, 3}, 0) == world::TxStatus::wrong_role, "host cannot request");
    Check(Tx(g_host, op::received, {}, 0) == world::TxStatus::unknown_op, "RECEIVED is native only");
    Check(Tx(g_host, op::paired, {}, 0) == world::TxStatus::unknown_op, "local ops are not TXable");
    Check(Tx(g_host, 0x1D, {}, 0) == world::TxStatus::unknown_op, "reserved code");
    Check(Tx(g_host, op::hello, {30050, 1, 5, 10}, 0) == world::TxStatus::bad_args, "wrong count");
    Check(Tx(g_host, op::hello, {100000, 1, 5, 10, 3}, 0) == world::TxStatus::bad_args, "mission out of range");
    Check(Tx(g_host, op::probe_sync, {7, 1, 5, 3000}, 0) == world::TxStatus::ok, "PROBE_SYNC in the lab");
    Setup(3, 3, true);
    Check(Tx(g_host, op::probe_sync, {7, 1, 5, 3000}, 0) == world::TxStatus::lab_only, "PROBE_SYNC refused in play");
    Check(Tx(g_host, op::goto_ack, {1, 1}, 0) == world::TxStatus::lab_only, "GOTO_ACK refused in play");
    Setup(0, 3);
    Check(Tx(g_host, op::hello, {30050, 1, 5, 10, 3}, 0) == world::TxStatus::disabled, "level 0 disabled");
    // U ops come back as a one-op datagram bundle, never queued.
    Setup();
    Pair(30010, 0);
    world::Datagram d{};
    Check(Tx(g_host, op::digest, {1, 100, 2000, 255, 0, 0, 0, 3, 1}, 0, &d) == world::TxStatus::ok && d.size == 4 + 4 + 22 &&
              g_host.outbound_size() == 0,
          "DIGEST is a datagram");
    g_guest.OnDatagram(d.bytes, d.size, 0);
    auto rx = Drain(g_guest, 3, 25);
    const auto* p = Find(rx, op::digest);
    Check(p && p->count == 10 && p->v[1] == 100 && p->v[9] == 20 + 25, "DIGEST + latencyMs (rtt/2 + queued)");
}

void Queues() {
    Setup();
    Pair(30010, 0);
    Drain(g_host, 1, 0);
    Drain(g_guest, 1, 0);
    // Control: 256 wire entries, then overflow (local ops keep their 32 reserved slots).
    std::vector<std::uint8_t> bundle(world::kMaxReliableBundle);
    world::Op reject{};
    reject.code = op::reject;
    reject.epoch = 1;
    reject.count = 6;
    const std::int64_t jv[6] = {1, op::req_outcome, 3, 7, 5, 1};
    std::memcpy(reject.v, jv, sizeof(jv));
    unsigned sent = 0;
    while (sent < 300) {
        std::size_t at = world::kBundleHeaderSize;
        unsigned n = 0;
        while (n < 60 && sent < 300) {
            at += world::EncodeOp(reject, bundle.data() + at, bundle.size() - at);
            ++n;
            ++sent;
        }
        world::WriteBundleHeader(bundle.data(), static_cast<std::uint8_t>(n));
        g_guest.OnReliable(bundle.data(), at, 0);
    }
    Check(g_guest.control_size() == 256 + 1, "256 wire control entries plus the RESYNC_NEEDED local op");
    Check(g_guest.resync_needed() && HasLine(Lines(g_guest), "world=overflow; queue=control"), "control overflow");
    auto rx = Drain(g_guest, 1, 0);
    Check(Count(rx, op::resync_needed) == 1 && Find(rx, op::resync_needed)->v[0] == 7, "RESYNC_NEEDED{7} once");
    Check(Tx(g_guest, op::resync, {5, 7}, 0) == world::TxStatus::ok && !g_guest.resync_needed(), "RESYNC clears resync_needed");

    // World ring: 4096, then overflow; a host REQ then gets REJECT busy.
    Setup();
    Pair(30010, 0);
    Drain(g_host, 1, 0);
    Drain(g_guest, 1, 0);
    world::Op req{};
    req.code = op::req_outcome;
    req.epoch = 1;
    req.count = 11;
    const std::int64_t qv[11] = {1, 7, 1, 2, 0, 3, 0, 0, 1, 2, 3};
    std::memcpy(req.v, qv, sizeof(qv));
    unsigned pushed = 0;
    while (pushed < 4100) {
        std::size_t at = world::kBundleHeaderSize;
        unsigned n = 0;
        while (n < 30 && pushed < 4100) {
            req.v[0] = static_cast<std::int64_t>(pushed % 65535 + 1);
            at += world::EncodeOp(req, bundle.data() + at, bundle.size() - at);
            ++n;
            ++pushed;
        }
        world::WriteBundleHeader(bundle.data(), static_cast<std::uint8_t>(n));
        g_host.OnReliable(bundle.data(), at, 0);
        // Drain the host's automatic answers so the outbound queue never fills.
        std::uint8_t sink[world::kMaxReliableBundle];
        std::uint32_t gen = 0;
        while (g_host.PeekBundle(sink, sizeof(sink), gen)) g_host.CommitBundle(gen);
    }
    Check(g_host.world_size() == 4096 && g_host.stats().q_world_max == 4096, "world ring holds 4096");
    Check(g_host.stats().auto_reject == 4 && g_host.stats().auto_received == 4096 && g_host.resync_needed(), "REJECT busy on overflow");
    rx = Drain(g_host, 1, 0);
    Check(Count(rx, op::resync_needed) == 1, "host RESYNC_NEEDED once");
    // Drops after the first line are reported by Tick under their own queue.
    auto lines = Lines(g_host);
    Check(HasLine(lines, "world=overflow; queue=world; dropped=1") && !HasLine(lines, "queue=out"), "first world drop logged at once");
    g_host.Tick(9999);
    Check(!HasLine(Lines(g_host), "world=overflow"), "at most one overflow line per 10 s");
    g_host.Tick(10000);
    lines = Lines(g_host);
    Check(HasLine(lines, "world=overflow; queue=world; dropped=3") && !HasLine(lines, "queue=out"), "later world drops keep their name");

    // A dropped local op (control queue and reserve full) is logged by Tick.
    Setup();
    for (unsigned i = 0; i < world::kControlQueue + world::kLocalReserve + 2; ++i) g_guest.OnLinkState(i % 2 == 0, 1, 0);
    Check(g_guest.control_size() == world::kControlQueue + world::kLocalReserve && g_guest.stats().overflow == 2, "local ops dropped");
    g_guest.Tick(0);
    Check(HasLine(Lines(g_guest), "world=overflow; queue=control; dropped=2"), "a local drop is logged");

    // RESYNC_END rides the world queue behind the OUTCOMEs sent before it.
    Setup();
    Pair(30010, 0);
    Drain(g_host, 1, 0);
    Drain(g_guest, 1, 0);
    Check(Tx(g_host, op::enemy_outcome, {7, 1, 5, 1, 1, 0, 0, 0, 0, 0, 0}, 0) == world::TxStatus::ok, "OUTCOME TX");
    Check(Tx(g_host, op::resync_end, {9, 1, 0, 1}, 0) == world::TxStatus::ok, "RESYNC_END TX");
    Exchange(0);
    rx = Drain(g_guest, 1, 0);
    Check(!Find(rx, op::resync_end) && g_guest.world_size() == 2, "RESYNC_END never overtakes in the control queue");
    rx = Drain(g_guest, 3, 0);
    Check(rx.size() == 2 && rx[0].op == op::enemy_outcome && rx[1].op == op::resync_end && rx[1].queue == 1 && rx[1].v[0] == 9,
          "the OUTCOME, then RESYNC_END (world queue)");
    g_guest.Cfg(30010, true, 30010, 0);
    Check(Tx(g_host, op::resync_end, {10, 1, 0, 1}, 0) == world::TxStatus::ok, "a RESYNC_END for the old pairing");
    Exchange(0);
    Check(!Find(Drain(g_guest, 3, 0), op::resync_end), "a RESYNC_END of another pairing is never delivered");

    // U slots: newer tick replaces, older counted, wrap-safe, epoch-checked.
    Setup();
    Pair(30010, 0);
    Drain(g_guest, 1, 0);
    const auto digest = [&](std::uint32_t tick, std::uint16_t epoch) {
        world::Op o{};
        o.code = op::digest;
        o.epoch = epoch;
        o.count = 9;
        const std::int64_t dv[9] = {tick, 50, 2000, 1, 0, 0, 0, 3, 1};
        std::memcpy(o.v, dv, sizeof(dv));
        std::uint8_t b[64];
        const std::size_t n = world::kBundleHeaderSize + world::EncodeOp(o, b + world::kBundleHeaderSize, 60);
        world::WriteBundleHeader(b, 1);
        g_guest.OnDatagram(b, n, 0);
    };
    digest(0xFFFFFFF0u, 1);
    digest(5, 1);  // newer across the wrap
    digest(0xFFFFFFF5u, 1);  // older
    Check(g_guest.stats().stale_u == 1, "an older digest is stale");
    digest(6, 2);  // another epoch: purged
    rx = Drain(g_guest, 3, 0);
    Check(Count(rx, op::digest) == 1 && Find(rx, op::digest)->v[0] == 5, "the newest digest, once");
    rx = Drain(g_guest, 3, 0);
    Check(!Find(rx, op::digest), "delivered once");

    // Outbound 2048 -> queue_full; peek keeps the ops; commit after a session reset removes nothing.
    Setup();
    Pair(30010, 0);
    Pump(g_host, g_guest, 0);
    unsigned queued = 0;
    while (Tx(g_host, op::player_area, {1, 30010}, 0) == world::TxStatus::ok) ++queued;
    Check(queued == world::kOutboundQueue && g_host.stats().out_full == 1, "outbound 2048 then queue_full");
    Check(Tx(g_host, op::player_area, {1, 30010}, 0) == world::TxStatus::queue_full, "TX 0,6");
    std::uint8_t out[world::kMaxReliableBundle];
    std::uint32_t generation = 0;
    const std::size_t n = g_host.PeekBundle(out, sizeof(out), generation);
    Check(n > 0 && n <= world::kMaxReliableBundle && out[2] > 1, "a bundle of several ops");
    Check(g_host.outbound_size() == world::kOutboundQueue, "peek removes nothing (SendReliable full keeps the ops)");
    g_host.OnSession(3, 0);
    g_host.CommitBundle(generation);
    Check(g_host.outbound_size() == 0 && g_host.generation() == generation + 1, "the session clear won");
    Tx(g_host, op::hello, {30010, 1, 5, 10, 3}, 0);
    g_host.CommitBundle(generation);
    Check(g_host.outbound_size() == 1, "a stale commit removes nothing");
}

void Schedulers() {
    // Probe fire at atMs, wrap-safe; steps >= 2 fire on receipt.
    Setup();
    Pair(30010, 0);
    Drain(g_host, 1, 0);
    Drain(g_guest, 1, 0);
    const std::uint64_t base = 0x1FFFFF000ull;  // low 32 bits wrap during the delay
    Check(Tx(g_host, op::probe_sync, {7, 1, 99, 3000}, base) == world::TxStatus::ok, "PROBE_SYNC step 1");
    Pump(g_host, g_guest, base);
    auto rx = Drain(g_host, 1, base + 2999);
    Check(!Find(rx, op::probe_fire), "not before atMs");
    rx = Drain(g_host, 1, base + 3000);
    const auto* p = Find(rx, op::probe_fire);
    Check(p && p->v[0] == 7 && p->v[1] == 1 && p->v[2] == 99 && p->v[3] == 3000, "host fires at atMs");
    rx = Drain(g_guest, 1, base + 2999);
    Check(!Find(rx, op::probe_fire), "guest waits for atMs");
    rx = Drain(g_guest, 1, base + 3001);
    Check(Find(rx, op::probe_fire) != nullptr, "guest fires at the same atMs (across the u32 wrap)");
    Tx(g_host, op::probe_sync, {8, 2, 99, 4}, base + 4000);
    Pump(g_host, g_guest, base + 4000);
    rx = Drain(g_guest, 1, base + 4000);
    p = Find(rx, op::probe_fire);
    Check(p && p->v[1] == 2 && p->v[3] == 4, "step 2 fires on receipt");
    Check(!Find(Drain(g_host, 1, base + 4000), op::probe_fire), "the sender of step 2 does not fire");

    // Goto refusals.
    const auto trigger = [](std::uint32_t target) {
        world::Trigger t{};
        t.has_goto = true;
        t.goto_target = target;
        return t;
    };
    Setup();
    g_host.Cfg(10033, true, 10033, 0);
    Lines(g_host);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 10033, false, 0);
    Check(HasLine(Lines(g_host), "world_goto=refused; reason=mission"), "goto only from 30050, free roam or an ACC");
    g_host.Cfg(30050, true, 30050, 0);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30050, true, 0);
    Check(HasLine(Lines(g_host), "world_goto=refused; reason=terminating"), "not while terminating");
    g_host.ApplyTrigger(trigger(30030), "goto=30030", 30050, false, 0);
    Check(HasLine(Lines(g_host), "world_goto=refused; reason=target"), "target");
    Setup(3, 3, true);
    g_host.Cfg(30050, true, 30050, 0);
    Lines(g_host);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30050, false, 0);
    Check(HasLine(Lines(g_host), "world_trigger=rejected; reason=not_lab"), "play never honours triggers");
    Setup(0, 3);
    g_host.Cfg(30050, true, 30050, 0);
    Lines(g_host);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30050, false, 0);
    Check(HasLine(Lines(g_host), "world_goto=refused; reason=level"), "level 0");

    // Happy path: 30050 -> 40050 -> 30010.
    Setup();
    g_host.Cfg(30050, true, 30050, 1000);
    Drain(g_host, 1, 1000);
    Lines(g_host);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30050, false, 1000);
    auto lines = Lines(g_host);
    Check(HasLine(lines, "world_trigger=applied; text=goto=30010") && HasLine(lines, "world_goto=armed; target=30010; lz=0"), "armed");
    rx = Drain(g_host, 1, 1000);
    p = Find(rx, op::go_to);
    Check(p && p->v[0] == 1 && p->v[1] == 30010 && p->v[2] == 0, "GOTO{1, 30010, 0}");
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30050, false, 1000);
    Check(HasLine(Lines(g_host), "world_goto=refused; reason=busy"), "busy");
    Tx(g_host, op::goto_ack, {1, 2}, 1100);
    Tx(g_host, op::goto_ack, {1, 2}, 1200);
    lines = Lines(g_host);
    Check(HasLine(lines, "world_goto=step; step=1; result=waiting") && lines.size() == 1, "waiting logged once");
    Tx(g_host, op::goto_ack, {1, 1}, 1300);
    Check(HasLine(Lines(g_host), "world_goto=step; step=1; result=reserved") && g_host.goto_state() == world::GotoState::expect_acc,
          "reserved");
    g_host.LocalMissionEnd(5000);
    g_host.Cfg(40050, true, 40050, 9000);
    rx = Drain(g_host, 1, 9000);
    p = Find(rx, op::go_to);
    Check(p && p->v[0] == 2 && p->v[1] == 30010, "GOTO{2} at the ACC");
    Tx(g_host, op::goto_ack, {2, 1}, 15000);
    Check(g_host.goto_state() == world::GotoState::expect_target, "expect target");
    g_host.Cfg(30010, true, 30010, 61000);
    Check(HasLine(Lines(g_host), "world_goto=arrived; mission=30010; ms=60000") && g_host.goto_state() == world::GotoState::idle,
          "arrived");
    // From free roam: step 1, then the Afghanistan ACC (40010).
    Setup();
    g_host.Cfg(30010, true, 30010, 1000);
    Drain(g_host, 1, 1000);
    Lines(g_host);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30010, false, 1000);
    Check(HasLine(Lines(g_host), "world_goto=armed; target=30010; lz=0") && g_host.goto_state() == world::GotoState::step1,
          "armed in free roam");
    Tx(g_host, op::goto_ack, {1, 1}, 1500);
    g_host.LocalMissionEnd(4000);
    g_host.Cfg(40010, true, 40010, 9000);
    rx = Drain(g_host, 1, 9000);
    p = Find(rx, op::go_to);
    Check(p && p->v[0] == 2 && p->v[1] == 30010 && g_host.goto_state() == world::GotoState::step2, "GOTO{2} at the 40010 ACC");
    // Already at the ACC (a save that continues into 40050): step 2 only.
    Setup();
    g_host.Cfg(40050, true, 40050, 1000);
    Drain(g_host, 1, 1000);
    Lines(g_host);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 40050, false, 1000);
    Check(HasLine(Lines(g_host), "world_goto=armed; target=30010; lz=0; from=acc") &&
              g_host.goto_state() == world::GotoState::step2, "armed at the ACC");
    rx = Drain(g_host, 1, 1000);
    p = Find(rx, op::go_to);
    Check(p && p->v[0] == 2 && p->v[1] == 30010, "GOTO{2} from the ACC");
    Tx(g_host, op::goto_ack, {2, 1}, 5000);
    g_host.Cfg(30010, true, 30010, 30000);
    Check(HasLine(Lines(g_host), "world_goto=arrived; mission=30010; ms=29000"), "arrived from the ACC");
    // Timeouts and an unexpected mission.
    struct Case { std::uint64_t at; const char* reason; };
    Setup();
    g_host.Cfg(30050, true, 30050, 0);
    g_host.ApplyTrigger(trigger(30020), "goto=30020", 30050, false, 0);
    Lines(g_host);
    g_host.Tick(44999);
    Check(g_host.goto_state() == world::GotoState::step1, "step 1 for 45 s");
    g_host.Tick(45000);
    Check(HasLine(Lines(g_host), "world_goto=failed; reason=step1_timeout"), "step1_timeout");
    g_host.ApplyTrigger(trigger(30020), "goto=30020", 30050, false, 50000);
    Tx(g_host, op::goto_ack, {1, 1}, 50000);
    Lines(g_host);
    g_host.Tick(170000);
    Check(HasLine(Lines(g_host), "world_goto=failed; reason=acc_timeout"), "acc_timeout");
    g_host.ApplyTrigger(trigger(30020), "goto=30020", 30050, false, 200000);
    Tx(g_host, op::goto_ack, {1, 1}, 200000);
    g_host.Cfg(40050, true, 40050, 210000);
    Lines(g_host);
    g_host.Tick(270000);
    Check(HasLine(Lines(g_host), "world_goto=failed; reason=acc_timeout"), "step 2 not reserved in 60 s");
    Setup();
    g_host.Cfg(30050, true, 30050, 0);
    g_host.ApplyTrigger(trigger(30020), "goto=30020", 30050, false, 0);
    Tx(g_host, op::goto_ack, {1, 1}, 0);
    g_host.Cfg(40050, true, 40050, 1000);
    Tx(g_host, op::goto_ack, {2, 1}, 2000);
    Lines(g_host);
    g_host.Tick(122001);
    Check(HasLine(Lines(g_host), "world_goto=failed; reason=step_timeout"), "step_timeout");
    Setup();
    g_host.Cfg(30050, true, 30050, 0);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30050, false, 0);
    Tx(g_host, op::goto_ack, {1, 1}, 0);
    Lines(g_host);
    g_host.Cfg(30050, true, 30050, 1000);
    Check(HasLine(Lines(g_host), "world_goto=failed; reason=unexpected_mission; mission=30050"), "unexpected mission");
    Setup();
    g_host.Cfg(30050, true, 30050, 0);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30050, false, 0);
    Tx(g_host, op::goto_ack, {1, 0}, 0);
    Check(HasLine(Lines(g_host), "world_goto=step; step=1; result=refused") && g_host.goto_state() == world::GotoState::idle,
          "refused step");

    // Probe triggers: now while a mission runs, else at the next CFG start; world=0.
    Setup();
    world::Trigger t{};
    t.has_probe = true;
    t.probe = WorldProbe::race_kill;
    g_host.ApplyTrigger(t, "world_probe=race_kill", 0, false, 0);
    Check(HasLine(Lines(g_host), "world_probe=scheduled; id=race_kill; when=start"), "scheduled for the next start");
    auto cfg = g_host.Cfg(30010, true, 30010, 0);
    Check(cfg.probe == 7 && cfg.mode == world::kModeLab && cfg.level == 3 && cfg.role == world::kRoleHost && cfg.epoch == 1,
          "CFG start consumes the probe");
    Check(g_host.Cfg(30010, false, 30010, 0).probe == 0, "a query never carries a probe");
    g_host.ApplyTrigger(t, "world_probe=race_kill", 30010, false, 0);
    rx = Drain(g_host, 1, 0);
    p = Find(rx, op::probe);
    Check(p && p->v[0] == 7, "PROBE now while the mission runs");
    world::Trigger zero{};
    zero.has_world = true;
    zero.world = 0;
    Pair(30010, 0);
    Drain(g_host, 1, 0);
    g_host.ApplyTrigger(zero, "world=0", 30010, false, 0);
    rx = Drain(g_host, 1, 0);
    Check(Find(rx, op::unpaired) && Find(rx, op::unpaired)->v[0] == 5 && Find(rx, op::level) && Find(rx, op::level)->v[0] == 0,
          "world=0: UNPAIRED{5} + LEVEL{0}");
    Check(g_host.level() == 0 && Tx(g_host, op::hello, {30010, 1, 5, 10, 3}, 0) == world::TxStatus::disabled, "disabled");
    g_host.ApplyTrigger(t, "world_probe=race_kill", 30010, false, 0);
    Check(HasLine(Lines(g_host), "world_trigger=rejected; reason=level0"), "no probe at level 0");
    world::Trigger three{};
    three.has_world = true;
    three.world = 3;
    g_host.ApplyTrigger(three, "world=3", 30010, false, 0);
    Check(g_host.level() == 0 && g_host.Cfg(30010, true, 30010, 0).level == 3, "world=N applies at the next CFG start");
    // The lab.ini probe is consumed by the first CFG start only; play never schedules one.
    world::Config c{};
    c.level = 2;
    c.role = world::kRoleGuest;
    c.lab_triggers = true;
    c.probe = WorldProbe::sleep_near;
    g_guest.Configure(c, 0);
    Check(g_guest.Cfg(30050, true, 30050, 0).probe == 10 && g_guest.Cfg(30010, true, 30010, 0).probe == 0, "lab.ini probe once");
    c.play = true;
    g_guest.Configure(c, 0);
    Check(g_guest.Cfg(30050, true, 30050, 0).probe == 0 && g_guest.Cfg(30050, true, 30050, 0).mode == world::kModePlay, "play: none");
    Check(HasLine(Lines(g_guest), "world=configured; level=2; role=guest; mode=play; proto=1; probe=none"), "configured line");
}

void Stats() {
    Setup();
    Pair(30010, 0);
    Lines(g_host);
    g_host.Tick(10000);
    auto lines = Lines(g_host);
    Check(HasLine(lines, "world=stats; level=3; eff=3; role=host; paired=1; epoch=1; tx_r="), "stats line");
    g_host.Tick(20000);
    Check(Lines(g_host).empty(), "no stats line when nothing changed");
    world::RxReply r{};
    g_host.Rx(1, 0, 20000, r);
    g_host.Rx(1, 0, 20500, r);
    g_host.Tick(30000);
    Check(HasLine(Lines(g_host), "rx_gap_ms_max=500"), "rx gap");
    world::RxReply empty{};
    g_host.Rx(3, 0, 30000, empty);
    Check(empty.empty && empty.flags == 3 && empty.epoch == 1, "empty RX flags: link + paired");
}
void Following() {
    Setup();
    Pair(30050, 100);
    Drain(g_host, 3, 100);
    Drain(g_guest, 3, 100);
    Check(Tx(g_host, op::transition, {1, 1, 30050, 40050, 6, 4294967295LL, 0, 2, 0, 0, 2}, 110) == world::TxStatus::ok,
          "host transition accepts unsigned route");
    Check(Tx(g_host, op::seed, {1, 40050, 1, 0, 672983}, 111) == world::TxStatus::ok &&
              Tx(g_host, op::seeds_end, {1, 40050, 1, 673007}, 112) == world::TxStatus::ok,
          "seed stream queued on control");
    Exchange(120);
    g_guest.Cfg(40050, true, 40050, 130);
    auto r = Drain(g_guest, 1, 131);
    Check(r.size() == 3 && r[0].op == op::transition && r[1].op == op::seed && r[2].op == op::seeds_end,
          "ordered travel and seeds survive guest CFG start");
    Check(!g_guest.paired() && g_guest.world_size() == 0, "old world is gone while durable control waits");
    Check(Tx(g_host, op::transition, {2, 1, 40050, 10033, 6, 0, 0, 2, 0, 0, 2}, 140) == world::TxStatus::bad_args,
          "story destination refused before enqueue");
    Check(Tx(g_guest, op::transition, {2, 1, 40050, 30010, 6, 0, 0, 2, 0, 0, 2}, 140) == world::TxStatus::wrong_role,
          "guest cannot commit travel");
    Check(Tx(g_guest, op::follow_ack, {1, 40050, 2}, 141) == world::TxStatus::ok,
          "arrival acknowledgement works while unpaired");
    Exchange(142);
    Check(Find(Drain(g_host, 1, 143), op::follow_ack) != nullptr, "host receives control while mission readiness differs");
    Check(Tx(g_host, op::seed, {3, 40050, 1, 0, 1}, 144) == world::TxStatus::ok, "seed queued before link replacement");
    Exchange(145);
    g_guest.OnSession(2, 146);
    g_guest.Cfg(40050, true, 40050, 147);
    const auto replaced = Drain(g_guest, 1, 148);
    Check(!Find(replaced, op::seed), "durability does not cross authenticated link sessions");
    const auto* reset = Find(replaced, op::unpaired);
    Check(reset && reset->v[0] == 1, "session reset reaches Lua even while unpaired and after CFG clears");
    Setup(3, 2);
    Pair(30050, 200);
    Check(Tx(g_host, op::transition, {1, 1, 30050, 40050, 6, 0, 0, 2, 0, 0, 2}, 210) == world::TxStatus::disabled,
          "mixed levels preserve W0-W2 behavior");
    Setup();
    Pair(30010, 300);
    Drain(g_host, 3, 301);
    Drain(g_guest, 3, 301);
    Check(Tx(g_guest, op::req_mission, {37, 1, 30010, 0}, 302) == world::TxStatus::ok, "guest requests an exit");
    Exchange(303);
    Check(Find(Drain(g_host, 1, 304), op::req_mission) && Find(Drain(g_guest, 1, 304), op::received),
          "exit request is control and receives native receipt");
}
void ConfigSession() {
    Setup();
    auto cfg = g_guest.Cfg(30050, false, 30050, 0);
    Check(cfg.session == 0 && cfg.link == 0, "disconnected CFG has no live transport generation");
    g_guest.OnLinkState(true, 41, 10);
    cfg = g_guest.Cfg(30050, false, 30050, 11);
    Check(cfg.session == 41 && cfg.link == 1 && cfg.level == 3 && cfg.role == world::kRoleGuest &&
              cfg.proto == world::kProto && cfg.epoch == 0 && cfg.probe == 0 && cfg.mode == world::kModeLab,
          "CFG appends the transport generation without changing the first seven fields");
    Check(g_guest.Cfg(30050, false, 30050, 12).session == 41 &&
              g_guest.Cfg(30050, true, 30050, 13).session == 41,
          "query and mission start retain the same connection generation");
    g_guest.OnSession(0xFFFFFFFFu, 20);
    cfg = g_guest.Cfg(30050, false, 30050, 21);
    Check(cfg.session == 0xFFFFFFFFu && cfg.link == 1, "new session exposes its full unsigned generation");
    Check(g_guest.Cfg(40050, true, 40050, 22).session == 0xFFFFFFFFu,
          "loading CFG keeps the replacement session generation");
    g_guest.OnLinkState(false, 0xFFFFFFFFu, 30);
    cfg = g_guest.Cfg(40050, false, 40050, 31);
    Check(cfg.session == 0 && cfg.link == 0 && cfg.level == 3 && cfg.role == world::kRoleGuest,
          "link loss clears only the live generation/link metadata");

    // r[9..11]: the partner's scene and the raw pair-wait code, both phases.
    Setup();
    g_host.OnLinkState(true, 7, 100);
    g_guest.OnLinkState(true, 7, 100);
    g_host.Cfg(30050, true, 30050, 100);
    cfg = g_guest.Cfg(30010, true, 30010, 100);
    Check(cfg.remote_mission == 0 && cfg.remote_level == 0 && cfg.wait == world::pair_wait::no_hello, "start before any HELLO");
    Tx(g_host, op::hello, {30050, 1, 77, 10, 3}, 100);
    Tx(g_host, op::ready, {30050, 77, 10, 3}, 100);
    Tx(g_guest, op::hello, {30010, 1, 66, 10, 3}, 100);
    Tx(g_guest, op::ready, {30010, 66, 10, 3}, 100);
    Exchange(100);
    cfg = g_guest.Cfg(30010, false, 30010, 110);
    Check(cfg.level == 3 && cfg.role == world::kRoleGuest && cfg.proto == world::kProto && cfg.link == 1 &&
              cfg.epoch == g_host.epoch() && cfg.probe == 0 && cfg.mode == world::kModeLab && cfg.session == 7,
          "the first eight fields are unchanged");
    Check(cfg.remote_mission == 30050 && cfg.remote_level == 3 && cfg.wait == world::pair_wait::different_mission,
          "guest CFG: the host's mission and level, different_mission (raw, never settling)");
    cfg = g_host.Cfg(30050, false, 30050, 110);
    Check(cfg.remote_mission == 30010 && cfg.remote_level == 3 && cfg.wait == world::pair_wait::different_mission && cfg.session == 7,
          "host CFG: the guest's mission and level");
    Tx(g_host, op::mission_end, {30050}, 120);
    Pump(g_host, g_guest, 120);
    cfg = g_guest.Cfg(30010, false, 30010, 121);
    Check(cfg.remote_mission == 0 && cfg.remote_level == 3 && cfg.wait == world::pair_wait::remote_loading,
          "after the host's MISSION_END: mission unknown, remote_loading");
    cfg = g_guest.Cfg(40050, true, 40050, 122);
    Check(cfg.remote_mission == 0 && cfg.wait == world::pair_wait::local_loading, "a CFG start fills the fields too");
    g_guest.OnSession(8, 130);
    cfg = g_guest.Cfg(40050, false, 40050, 131);
    Check(cfg.remote_mission == 0 && cfg.remote_level == 0 && cfg.wait == world::pair_wait::no_hello && cfg.session == 8,
          "a new session: no_hello");
    Tx(g_host, op::hello, {40050, 1, 77, 10, 3}, 132);
    Tx(g_host, op::ready, {40050, 77, 10, 3}, 132);
    Exchange(132);
    cfg = g_guest.Cfg(40050, false, 40050, 133);
    Check(cfg.remote_mission == 40050 && cfg.remote_level == 3 && cfg.wait == world::pair_wait::local_loading,
          "the host's READY names its new mission");
    g_guest.OnLinkState(false, 8, 140);
    cfg = g_guest.Cfg(40050, false, 40050, 141);
    Check(cfg.remote_mission == 0 && cfg.remote_level == 0 && cfg.wait == world::pair_wait::no_link && cfg.link == 0,
          "link down: masked, no_link");
}
void WorldDisable() {
    Setup();
    Pair(30050, 100);
    Drain(g_host, 3, 101);
    Drain(g_guest, 3, 101);
    Check(Tx(g_host, op::transition, {1, 1, 30050, 40050, 6, 0, 0, 2, 0, 0, 2}, 110) == world::TxStatus::ok &&
              Tx(g_host, op::seed, {1, 40050, 1, 0, 672983}, 111) == world::TxStatus::ok &&
              Tx(g_host, op::seeds_end, {1, 40050, 1, 674007}, 112) == world::TxStatus::ok,
          "old travel stream is queued before disable");
    Exchange(120);
    g_guest.Cfg(40050, true, 40050, 130);
    Check(!g_guest.paired() && g_guest.control_size() == 3, "loading preserves pending travel while already unpaired");
    world::Trigger go{};
    go.has_goto = true;
    go.goto_target = 30010;
    g_guest.ApplyTrigger(go, "goto=30010", 40050, false, 131);
    Check(Tx(g_guest, op::probe_sync, {7, 1, 123, 3000}, 132) == world::TxStatus::ok &&
              Tx(g_guest, op::follow_ack, {1, 40050, 1}, 133) == world::TxStatus::ok,
          "local scheduled probe and outbound acknowledgement exist before disable");
    std::uint8_t bundle[world::kMaxReliableBundle];
    std::uint32_t old_generation = 0;
    Check(g_guest.PeekBundle(bundle, sizeof(bundle), old_generation) > 0, "worker has peeked an old outgoing bundle");

    world::Trigger zero{};
    zero.has_world = true;
    g_guest.ApplyTrigger(zero, "world=0", 40050, false, 134);
    Check(g_guest.level() == 0 && !g_guest.paired() && g_guest.control_size() == 2 &&
              g_guest.world_size() == 0 && g_guest.outbound_size() == 0 && g_guest.goto_state() == world::GotoState::idle,
          "disable cancels old control, world, outgoing and goto work before reset delivery");
    auto reset = Drain(g_guest, 3, 135);
    Check(reset.size() == 2 && reset[0].op == op::unpaired && reset[0].v[0] == world::unpair::level0 &&
              reset[1].op == op::level && reset[1].v[0] == 0,
          "already-unpaired disable delivers reset before LEVEL0 with no old transaction ahead of it");
    g_guest.Tick(10000);
    Check(Drain(g_guest, 3, 10001).empty(), "disabled scheduler cannot revive a delayed probe or goto");
    Check(Tx(g_host, op::seed, {1, 40050, 1, 0, 672983}, 10002) == world::TxStatus::ok,
          "peer may still have old seed traffic in flight");
    Pump(g_host, g_guest, 10003);
    Check(Drain(g_guest, 3, 10004).empty(), "old incoming traffic cannot restore a disabled transaction");
    g_guest.ApplyTrigger(zero, "world=0", 40050, false, 10005);
    reset = Drain(g_guest, 3, 10006);
    Check(reset.size() == 2 && reset[0].op == op::unpaired && reset[0].v[0] == world::unpair::level0 &&
              reset[1].op == op::level,
          "repeated explicit disable still resets persistent Lua state before cleanup");

    world::Trigger three{};
    three.has_world = true;
    three.world = 3;
    g_guest.ApplyTrigger(three, "world=3", 40050, false, 10007);
    Check(g_guest.Cfg(40050, true, 40050, 10008).level == 3 && Drain(g_guest, 3, 10009).empty(),
          "re-enable starts without old pending travel");
    Check(Tx(g_guest, op::hello, {40050, 50, 77, 10, 3}, 10010) == world::TxStatus::ok, "fresh HELLO after re-enable");
    std::uint32_t new_generation = 0;
    Check(g_guest.PeekBundle(bundle, sizeof(bundle), new_generation) > 0 && new_generation != old_generation,
          "disable invalidates the worker's old outbound generation");
    const unsigned fresh_out = g_guest.outbound_size();
    g_guest.CommitBundle(old_generation);
    Check(g_guest.outbound_size() == fresh_out, "stale worker commit cannot remove newly queued traffic");
    g_guest.CommitBundle(new_generation);
    Check(g_guest.outbound_size() == 0, "current worker commit still drains its own fresh traffic");
}
void Alignment() {
    Setup();
    Pair(30010, 100);
    Drain(g_host, 3, 101);
    Drain(g_guest, 3, 101);
    Check(Tx(g_host, op::align_begin, {5, 30010, 77, 1, 1}, 102) == world::TxStatus::ok, "paired host begins alignment");
    Check(Tx(g_host, op::align_record, {5, 1, 1, 0xF1234567LL, 1, 5, 1}, 103) == world::TxStatus::ok,
          "soldier snapshot with unsigned hash");
    Check(Tx(g_host, op::align_record, {5, 2, 1, 0xF1234568LL, 1, 3, 1}, 104) == world::TxStatus::ok,
          "CP snapshot record");
    Check(Tx(g_host, op::align_end, {5, 1, 1, 0xFFFFFFFFLL, 9}, 105) == world::TxStatus::ok, "snapshot completion queued");
    Exchange(110);
    auto control = Drain(g_guest, 1, 111);
    const auto* begin = Find(control, op::align_begin);
    Check(begin && begin->count == 6 && begin->v[5] == g_host.epoch(), "BEGIN carries established epoch in control");
    auto world_rx = Drain(g_guest, 3, 112);
    Check(world_rx.size() == 3 && world_rx[0].op == op::align_record && world_rx[1].op == op::align_record &&
              world_rx[2].op == op::align_end && world_rx[2].v[4] == 9 && world_rx[2].v[5] == g_host.epoch(),
          "END cannot overtake records and carries sequence plus epoch");
    Check(Tx(g_guest, op::align_ack, {5, 30010, 1, 1}, 113) == world::TxStatus::ok, "guest accepts matching snapshot");
    Exchange(114);
    Check(Find(Drain(g_host, 1, 115), op::align_ack), "host receives acceptance on control");
    Check(Tx(g_guest, op::req_game_over, {51, 1, 1, 1}, 115) == world::TxStatus::ok, "guest requests shared death");
    Exchange(115);
    Check(Find(Drain(g_host, 1, 115), op::req_game_over) && Find(Drain(g_guest, 1, 115), op::received),
          "death request gets native receipt and reaches host control");
    Check(Tx(g_host, op::game_over, {1, 1, 2}, 115) == world::TxStatus::ok &&
              Tx(g_host, op::checkpoint, {0, 1, -748011, 329852, 1668605, -90000}, 115) == world::TxStatus::ok,
          "host broadcasts death and exact signed checkpoint pose");
    Check(Tx(g_guest, op::checkpoint, {0, 1, 0, 0, 0, 0}, 115) == world::TxStatus::wrong_role &&
              Tx(g_host, op::checkpoint, {0, 1, 10000001, 0, 0, 0}, 115) == world::TxStatus::bad_args,
          "checkpoint authority and bounded pose validated");
    Exchange(115);
    const auto recovery = Drain(g_guest, 1, 115);
    const auto* point = Find(recovery, op::checkpoint);
    Check(Find(recovery, op::game_over) && point && point->count == 6 && point->v[2] == -748011 && point->v[5] == -90000,
          "recovery controls carry exact pose on matching epoch");
    g_host.LocalMissionEnd(116);
    Check(Tx(g_host, op::game_over, {1, 0xFFFFFFFFLL, 0}, 117) == world::TxStatus::not_paired,
          "game over cannot escape old mission epoch");
    Check(Tx(g_host, op::align_begin, {6, 30010, 77, 1, 1}, 117) == world::TxStatus::not_paired,
          "no alignment without readiness barrier");
    Setup(2, 3);
    Pair(30010, 200);
    Check(Tx(g_host, op::align_begin, {5, 30010, 77, 1, 1}, 210) == world::TxStatus::disabled,
          "level two remains W0-W2 only");
}

// Spawn sync (docs/debug-menu.md): ENEMY_DEBUG host -> guest on the world queue,
// the guest's placement grant (recorded only when its Lua pops a warp/correct),
// ENEMY_DEBUG_ACK guest -> host, and every grant reset.
void SpawnSync() {
    Setup();
    Pair(30010, 100);
    Drain(g_host, 3, 101);
    Drain(g_guest, 3, 101);
    world::Grant grant{};
    const std::int64_t ent = 0xF5A4F001LL;
    Check(Tx(g_guest, op::enemy_debug, {1, ent, 1, 0, 0, 0, 0, 2, 0, 1116}, 102) == world::TxStatus::wrong_role, "only the host announces");
    Check(Tx(g_host, op::enemy_debug_ack, {1, ent, 0, 0, 0, 0}, 102) == world::TxStatus::wrong_role, "only the guest answers");
    Check(Tx(g_host, op::enemy_debug, {1, ent, 1, -748011, 329852, 1668605, -3141, 2, 0, 1116}, 103) == world::TxStatus::ok,
          "warp announce with signed mm and yaw");
    Check(Tx(g_host, op::enemy_debug, {2, ent, 5, 0, 0, 0, 0, 0, 0, 1116}, 103) == world::TxStatus::ok, "ignore announce");
    Check(Tx(g_host, op::enemy_debug, {3, 9, 9, 0, 0, 0, 0, 0, 0, 0}, 103) == world::TxStatus::bad_args, "kind 9 refused");
    Check(Tx(g_host, op::enemy_debug, {3, 9, 0, 0, 0, 0, 0, 0, 0, 0}, 103) == world::TxStatus::bad_args, "kind 0 refused");
    Check(Tx(g_host, op::enemy_debug, {0, 9, 1, 0, 0, 0, 0, 0, 0, 0}, 103) == world::TxStatus::bad_args, "seq 0 refused");
    Check(Tx(g_host, op::enemy_debug, {3, 9, 1, 100000001, 0, 0, 0, 0, 0, 0}, 103) == world::TxStatus::bad_args, "position bound");
    Check(Tx(g_host, op::enemy_debug, {3, 9, 1, 0, 0, 0, 8001, 0, 0, 0}, 103) == world::TxStatus::bad_args, "yaw bound");
    Check(Tx(g_host, op::enemy_debug, {3, 9, 1, 0, 0, 0, 0, 4, 0, 0}, 103) == world::TxStatus::bad_args, "flags bound");
    Exchange(104);
    Check(!g_guest.FindGrant(1, 104, grant) && g_guest.grant_count() == 0, "no grant before the guest's Lua pops the message");
    Check(Drain(g_guest, 1, 104).empty(), "ENEMY_DEBUG never rides the control queue");
    const auto rx = Drain(g_guest, 3, 105);
    Check(rx.size() == 2 && rx[0].op == op::enemy_debug && rx[0].count == 10 && rx[0].v[1] == ent && rx[0].v[3] == -748011 &&
              rx[0].v[6] == -3141 && rx[0].v[9] == 1116 && rx[1].op == op::enemy_debug && rx[1].v[2] == 5,
          "world queue order and exact fields");
    Check(g_guest.FindGrant(1, 105, grant) && grant.valid && grant.kind == 1 && grant.ent == 0xF5A4F001u && grant.pos_mm[0] == -748011 &&
              grant.pos_mm[1] == 329852 && grant.pos_mm[2] == 1668605 && grant.yaw_mrad == -3141 && grant.flags == 2 &&
              grant.epoch == g_guest.epoch() && grant.rx_ms == 105,
          "the popped warp is the guest's grant");
    const std::uint32_t first_serial = grant.serial;
    const std::uint16_t first_epoch = grant.epoch;
    Check(first_serial >= 1 && first_serial <= 0x7FFFFFFFu, "a grant serial is 1..2^31-1 (the gate id sets bit 31)");
    Check(!g_guest.FindGrant(2, 105, grant) && !grant.valid, "an ignore is not a placement grant");
    Check(!g_host.FindGrant(1, 105, grant), "the host never holds grants");
    Check(g_guest.FindGrant(1, 105 + world::kGrantLifeMs, grant) && !g_guest.FindGrant(1, 106 + world::kGrantLifeMs, grant),
          "a grant lives 15 s from the pop");
    Check(!g_guest.FindGrant(1, 50, grant), "a clock before the pop refuses");
    Check(Tx(g_guest, op::enemy_debug_ack, {1, ent, 0, 0, 12, 34}, 106) == world::TxStatus::ok &&
              Tx(g_guest, op::enemy_debug_ack, {2, ent, 1, 32, 0, 0}, 106) == world::TxStatus::bad_args,
          "guest ACK, reason bound");
    Exchange(107);
    const auto acks = Drain(g_host, 3, 108);
    Check(acks.size() == 1 && acks[0].op == op::enemy_debug_ack && acks[0].v[1] == ent && acks[0].v[4] == 12 && acks[0].v[5] == 34,
          "ACK reaches the host world queue");
    // Twenty corrections: the pending warp grant survives, the oldest corrections go.
    Check(world::kGrants == 16, "16 grant slots");
    for (std::int64_t seq = 10; seq < 30; ++seq)
        Check(Tx(g_host, op::enemy_debug, {seq, 9, 3, 1000, 0, 1000, 0, 0, 0, 7}, 110) == world::TxStatus::ok, "correction");
    Exchange(111);
    std::uint64_t at = 112;
    for (int i = 0; i < 20; ++i) {
        world::RxReply r{};
        g_guest.Rx(3, 40, at++, r);
        Check(!r.empty && r.op == op::enemy_debug && r.v[2] == 3, "correction popped");
    }
    Check(g_guest.grant_count() == world::kGrants && g_guest.FindGrant(1, at, grant) && grant.kind == 1 && grant.serial == first_serial,
          "the warp grant is kept with its serial");
    Check(g_guest.FindGrant(29, at, grant) && grant.kind == 3 && grant.serial == first_serial + 20 && g_guest.FindGrant(15, at, grant) &&
              grant.serial == first_serial + 6 && !g_guest.FindGrant(14, at, grant) && !g_guest.FindGrant(10, at, grant),
          "the oldest corrections are replaced; every recorded grant gets the next serial");
    // Resets: unpair (link session), local mission start/end, a new host epoch.
    g_guest.OnLinkState(true, 2, 200);
    Check(g_guest.grant_count() == 0 && !g_guest.FindGrant(19, 200, grant), "a link session change clears the grants");
    Pair(30010, 300);
    Drain(g_guest, 3, 301);
    Check(Tx(g_host, op::enemy_debug, {1, ent, 3, 0, 0, 0, 0, 0, 0, 0}, 302) == world::TxStatus::ok, "new epoch correction");
    Exchange(303);
    Drain(g_guest, 3, 304);
    Check(g_guest.FindGrant(1, 304, grant) && grant.epoch == g_guest.epoch(), "grant of the new epoch");
    g_guest.LocalMissionEnd(305);
    Check(g_guest.grant_count() == 0, "a local mission end clears the grants");
    Pair(30010, 400);
    Drain(g_guest, 3, 401);
    Check(Tx(g_host, op::enemy_debug, {1, ent, 1, 0, 0, 0, 0, 0, 0, 0}, 402) == world::TxStatus::ok, "warp");
    Exchange(403);
    Drain(g_guest, 3, 404);
    Check(g_guest.grant_count() == 1, "one grant");
    g_guest.Cfg(30020, true, 30020, 405);
    Check(g_guest.grant_count() == 0, "a local mission start clears the grants");
    g_host.LocalMissionEnd(406);
    Check(Tx(g_host, op::enemy_debug, {2, ent, 1, 0, 0, 0, 0, 0, 0, 0}, 407) == world::TxStatus::not_paired, "nothing unpaired");
    // A restarted host (fresh Configure) reuses epoch and seq; the serial never repeats.
    Setup();
    Pair(30010, 450);
    Drain(g_guest, 3, 451);
    Check(Tx(g_host, op::enemy_debug, {1, ent, 1, 0, 0, 0, 0, 0, 0, 0}, 452) == world::TxStatus::ok, "warp after a restart");
    Exchange(453);
    Drain(g_guest, 3, 454);
    Check(g_guest.FindGrant(1, 454, grant) && grant.epoch == first_epoch && grant.seq == 1 && grant.serial > first_serial + 20,
          "same epoch and seq as the first grant, a new serial (Configure never resets it)");
    Setup(1, 3);
    Pair(30010, 500);
    Check(Tx(g_host, op::enemy_debug, {1, ent, 1, 0, 0, 0, 0, 0, 0, 0}, 501) == world::TxStatus::disabled, "level 1 carries no spawn sync");
}
void RescueAndBuddies() {
    const std::uint8_t codes[] = {op::downed_status, op::downed_commit, op::revive_grant, op::buddy_state, op::buddy_ack, op::friendly_hit, op::ride_body};
    const std::int64_t values[][10] = {{1, 1, 1, 2, -100, 50, 20, 0, 0, 1}, {1, 1, 1, 2, 1, 0, 0, 3000, 1},
                                      {2, 1, 1}, {1, 1, 1, -100, 50, 20, -3142, 123}, {1, 1, 0, 2}, {1, 0, 1, 1000000000, 2},
                                      {1, 1, 6656, 802100, 338300, 1642000, -500, 1, 3}};
    for (unsigned i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        Setup(3, 3, true); Pair(30010, 100); Drain(g_host, 3, 100); Drain(g_guest, 3, 100);
        const auto* spec = world::FindOp(codes[i]);
        world::Datagram datagram{};
        Check(spec && spec->wire == world::Wire::reliable && spec->level == 3 && !spec->lab_only && !spec->request,
              "rescue and buddy specs are reliable play controls");
        auto& sender = spec->dir == world::Dir::guest_to_host ? g_guest : g_host;
        auto& receiver = spec->dir == world::Dir::guest_to_host ? g_host : g_guest;
        Check(sender.Tx(codes[i], values[i], spec->lua, 101, &datagram) == world::TxStatus::ok, "new op TX admitted");
        Exchange(102);
        const auto rx = Drain(receiver, 3, 103); const auto* got = Find(rx, codes[i]);
        Check(got && got->count == spec->lua + spec->rx && got->v[0] == values[i][0], "new op delivered on current epoch");
        if (spec->dir != world::Dir::both)
            Check(receiver.Tx(codes[i], values[i], spec->lua, 104, &datagram) == world::TxStatus::wrong_role, "new op wrong sender refused");
        else {
            Check(g_guest.Tx(codes[i], values[i], spec->lua, 104, &datagram) == world::TxStatus::ok, "status is bidirectional");
            Exchange(105); Check(Find(Drain(g_host, 3, 106), codes[i]) != nullptr, "guest status reaches host");
        }
        world::Op old{}; old.code = codes[i]; old.epoch = 9; old.count = spec->lua;
        for (unsigned j = 0; j < spec->lua; ++j) old.v[j] = values[i][j];
        std::uint8_t packet[128]{}; world::WriteBundleHeader(packet, 1);
        const auto size = world::EncodeOp(old, packet + world::kBundleHeaderSize, sizeof(packet) - world::kBundleHeaderSize);
        Check(size != 0, "stale op codec builds");
        receiver.OnReliable(packet, size + world::kBundleHeaderSize, 107);
        Check(!Find(Drain(receiver, 3, 108), codes[i]), "new op of stale epoch discarded");
        receiver.OnReliable(packet, size + world::kBundleHeaderSize, 109);
        Check(!Find(Drain(receiver, 3, 110), codes[i]), "stale duplicate never reaches Lua");
        sender.LocalMissionEnd(111);
        Check(sender.Tx(codes[i], values[i], spec->lua, 112, &datagram) == world::TxStatus::not_paired, "new op after mission end refused");
        Setup(2, 2, true); Pair(30010, 200);
        auto& low = spec->dir == world::Dir::guest_to_host ? g_guest : g_host;
        Check(low.Tx(codes[i], values[i], spec->lua, 201, &datagram) == world::TxStatus::disabled, "new ops require world level 3");
    }
    Setup(); Pair(30010, 300);
    Check(Tx(g_host, op::cargo, {1,2,802100,338300,1642000,1571},301)==world::TxStatus::ok,"cargo offer accepted by reliable codec");
    Check(Tx(g_guest, op::cargo, {2,3,802100,338300,1642000,1571},301)==world::TxStatus::ok,"cargo request accepted by reliable codec");
    Check(Tx(g_host, op::cargo, {3,4,802100,338300,1642000,1571},301)==world::TxStatus::bad_args,"unknown cargo state refused");
    Check(Tx(g_host, op::cargo, {0,0,0,0,0,0},301)==world::TxStatus::bad_args,"cargo zero revision refused");
    Check(Tx(g_host, op::ride_body, {1,3,4000,802100,338300,1642000,1571,1,0},301)==world::TxStatus::ok, "vehicle pose kind admitted");
    Check(Tx(g_host, op::ride_body, {1,4,4000,802100,338300,1642000,1571,1,0},301)==world::TxStatus::bad_args, "unknown ride kind refused");
    Check(Tx(g_host, op::buddy_state, {1,1,1,1000000001,0,0,0,0},301)==world::TxStatus::bad_args, "buddy excessive position refused");
    Check(Tx(g_host, op::buddy_state, {1,1,1,0,0,0,3143,0},301)==world::TxStatus::bad_args, "buddy non-normalized heading refused");
    Check(Tx(g_host, op::revive_grant, {2,0,1},301)==world::TxStatus::bad_args, "revive zero target generation refused");
    Check(Tx(g_host, op::downed_commit, {1,1,1,1,1,0,0,3001,1},301)==world::TxStatus::bad_args, "revive excessive progress refused");
    Check(Tx(g_host, op::friendly_hit, {0,0,1,10,0},301)==world::TxStatus::bad_args, "hit zero shot sequence refused");
    Check(Tx(g_host, op::friendly_hit, {1,0,0,10,0},301)==world::TxStatus::bad_args, "hit zero target status sequence refused");
    Check(Tx(g_host, op::friendly_hit, {1,0,1,1000000001,0},301)==world::TxStatus::bad_args, "hit excessive damage refused");
    Check(Tx(g_host, op::friendly_hit, {1,0,1,10,1},301)==world::TxStatus::bad_args, "hit cause one cannot enter wire");
    Check(Tx(g_host, op::friendly_hit, {1,0,1,10,0},301)==world::TxStatus::ok, "normal hit cause admitted");
    world::Op hit{}; hit.code = op::friendly_hit; hit.epoch = g_host.epoch(); hit.count = 5;
    hit.v[0]=1; hit.v[1]=0; hit.v[2]=1; hit.v[3]=10; hit.v[4]=2;
    std::uint8_t bundle[64]{}; world::WriteBundleHeader(bundle,1);
    const auto hitSize=world::EncodeOp(hit,bundle+world::kBundleHeaderSize,sizeof(bundle)-world::kBundleHeaderSize);
    Check(hitSize==world::kOpHeaderSize+15,"sleep hit alt enum encodes exact body");
    bundle[world::kBundleHeaderSize+hitSize-1]=1;
    world::BundleReader reader; world::Op decoded{}; world::OpStatus status{};
    Check(reader.Open(bundle,world::kBundleHeaderSize+hitSize),"invalid cause bundle opens structurally");
    Check(reader.Next(decoded,status) && status==world::OpStatus::bad_op,"received cause one rejected by codec");
}
// The driver's vehicle pose travels on the datagram channel and only the newest
// sender tick reaches Lua.
void RideFast() {
    Setup(3, 3, true); Pair(30010, 100); Drain(g_host, 3, 100); Drain(g_guest, 3, 100);
    world::Datagram datagram{};
    Check(Tx(g_host, op::ride_fast, {1000, 3, 4000, 802100, 338300, 1642000, 1571}, 101, &datagram) == world::TxStatus::ok &&
              datagram.size == world::kBundleHeaderSize + world::kOpHeaderSize + 23, "fast vehicle pose becomes one datagram");
    world::Datagram older{};
    Check(Tx(g_host, op::ride_fast, {990, 3, 4000, 1, 2, 3, 0}, 101, &older) == world::TxStatus::ok, "older pose encodes");
    g_guest.OnDatagram(datagram.bytes, datagram.size, 102);
    g_guest.OnDatagram(older.bytes, older.size, 102);
    auto rx = Drain(g_guest, 3, 103);
    const auto* got = Find(rx, op::ride_fast);
    Check(got && Count(rx, op::ride_fast) == 1 && got->count == 7 && got->v[0] == 1000 && got->v[2] == 4000 && got->v[3] == 802100,
          "the newest pose is delivered once and a late older pose is dropped");
    Check(!Find(Drain(g_guest, 3, 104), op::ride_fast), "a delivered pose is not repeated");
    world::Datagram guestPose{};
    Check(Tx(g_guest, op::ride_fast, {5, 3, 4001, 0, 0, 0, -31416}, 105, &guestPose) == world::TxStatus::ok, "guest driver sends too");
    g_host.OnDatagram(guestPose.bytes, guestPose.size, 106);
    Check(Find(Drain(g_host, 3, 107), op::ride_fast) != nullptr, "guest pose reaches host");
    Check(Tx(g_host, op::ride_fast, {1001, 1, 4000, 0, 0, 0, 0}, 108) == world::TxStatus::bad_args, "only vehicles use the fast path");
    Check(Tx(g_host, op::ride_fast, {1001, 3, 65535, 0, 0, 0, 0}, 108) == world::TxStatus::bad_args, "null object id refused");
    Check(Tx(g_host, op::ride_fast, {1001, 3, 4000, 0, 0, 0, 31417}, 108) == world::TxStatus::bad_args, "non-normalized heading refused");
    world::Datagram reliableOnly{};
    Check(Tx(g_host, op::ride_body, {1, 3, 4000, 0, 0, 0, 0, 1, 0}, 109, &reliableOnly) == world::TxStatus::ok && reliableOnly.size == 0,
          "RIDE_BODY stays on the reliable queue");
    Setup(2, 2, true); Pair(30010, 200);
    Check(Tx(g_host, op::ride_fast, {1, 3, 4000, 0, 0, 0, 0}, 201) == world::TxStatus::disabled, "fast ride requires world level 3");
}
// WS3 A1 (docs/vehicle-ws3.md): RIDE_TILT is a new datagram next to RIDE_FAST
// (which keeps its exact 7 values); its own latest-value slot, so a RIDE_FAST
// and a RIDE_TILT of the same sender tick both reach Lua. Admitted in play
// (hidden run 0a2314f2); an older peer skips the unknown code.
void RideTilt() {
    const auto* spec = world::FindOp(op::ride_tilt);
    Check(spec && spec->wire == world::Wire::datagram && spec->body == 14 && spec->lua == 4 && spec->native == 0 && spec->rx == 0 &&
              !spec->lab_only && spec->level == 3 && spec->dir == world::Dir::both && !spec->request,
          "RIDE_TILT is a 14-byte datagram both ways, lab and play");
    Check(op::ride_tilt == 0x2D && world::FindOp(op::ride_fast)->lua == 7, "a new code; RIDE_FAST keeps 7 values");
    Setup(3, 3); Pair(30010, 100); Drain(g_host, 3, 100); Drain(g_guest, 3, 100);
    world::Datagram datagram{}, older{};
    Check(Tx(g_host, op::ride_tilt, {1000, 25600, 120, -45}, 101, &datagram) == world::TxStatus::ok &&
              datagram.size == world::kBundleHeaderSize + world::kOpHeaderSize + 14, "the record angles become one datagram");
    Check(Tx(g_host, op::ride_tilt, {990, 25600, 1, 2}, 101, &older) == world::TxStatus::ok, "older angles encode");
    g_guest.OnDatagram(datagram.bytes, datagram.size, 102);
    g_guest.OnDatagram(older.bytes, older.size, 102);
    auto rx = Drain(g_guest, 3, 103);
    const auto* got = Find(rx, op::ride_tilt);
    Check(got && Count(rx, op::ride_tilt) == 1 && got->count == 4 && got->v[0] == 1000 && got->v[1] == 25600 && got->v[2] == 120 &&
              got->v[3] == -45, "the newest angles are delivered once, a late older sample dropped");
    world::Datagram fast{}, tilt{};
    Check(Tx(g_host, op::ride_fast, {1001, 3, 25600, 1, 2, 3, 0}, 104, &fast) == world::TxStatus::ok &&
              Tx(g_host, op::ride_tilt, {1001, 25600, 5, 6}, 104, &tilt) == world::TxStatus::ok, "one tick: pose and angles");
    g_guest.OnDatagram(fast.bytes, fast.size, 104);
    g_guest.OnDatagram(tilt.bytes, tilt.size, 104);
    rx = Drain(g_guest, 3, 105);
    Check(Find(rx, op::ride_fast) && Find(rx, op::ride_tilt) && Find(rx, op::ride_tilt)->v[2] == 5,
          "RIDE_FAST and RIDE_TILT of one sender tick both reach Lua (separate slots)");
    world::Datagram guestTilt{};
    Check(Tx(g_guest, op::ride_tilt, {7, 25601, -1570, 1570}, 106, &guestTilt) == world::TxStatus::ok, "the guest driver sends too");
    g_host.OnDatagram(guestTilt.bytes, guestTilt.size, 106);
    Check(Find(Drain(g_host, 3, 107), op::ride_tilt) != nullptr, "guest angles reach the host");
    Check(Tx(g_host, op::ride_tilt, {1002, 25600, 1571, 0}, 108) == world::TxStatus::bad_args &&
              Tx(g_host, op::ride_tilt, {1002, 25600, 0, -1571}, 108) == world::TxStatus::bad_args,
          "|angle| >= pi/2 refused");
    Check(Tx(g_host, op::ride_tilt, {1002, 65535, 0, 0}, 108) == world::TxStatus::bad_args, "null object id refused");
    Setup(3, 3, true); Pair(30010, 200); Drain(g_host, 3, 200); Drain(g_guest, 3, 200);
    world::Datagram playTilt{};
    Check(Tx(g_host, op::ride_tilt, {1, 25600, 7, -8}, 201, &playTilt) == world::TxStatus::ok && playTilt.size > 0, "play sends RIDE_TILT");
    g_guest.OnDatagram(playTilt.bytes, playTilt.size, 202);
    const auto playRx = Drain(g_guest, 3, 203);
    const auto* playGot = Find(playRx, op::ride_tilt);
    Check(playGot && playGot->v[1] == 25600 && playGot->v[2] == 7 && playGot->v[3] == -8, "play delivers RIDE_TILT to Lua");
}
// 0.13.6 online-vehicle-gone (docs/world-sync-protocol.md section 16): VEHICLE_GONE is a
// host-authoritative world op (H->G, reliable, level 3, lab and play) and REQ_VEHICLE_GONE
// the guest's request (G->H, answered natively with RECEIVED, or REJECT 5 for another
// epoch and 10 below level 3). Both carry a TppVehicle2 id (class 50: 25600..26111).
// Before this branch neither code existed (FindOp(0x2E) was the "unknown code" check).
void VehicleGone() {
    const auto* gone = world::FindOp(op::vehicle_gone);
    const auto* req = world::FindOp(op::req_vehicle_gone);
    Check(op::vehicle_gone == 0x2E && op::req_vehicle_gone == 0x2F && gone && req, "VEHICLE_GONE 0x2E and REQ_VEHICLE_GONE 0x2F exist");
    Check(gone->queue == 1 && gone->wire == world::Wire::reliable && gone->level == 3 && !gone->lab_only && !gone->request &&
              gone->dir == world::Dir::host_to_guest && gone->lua == 9 && gone->native == 0 && gone->rx == 0 && gone->body == 21,
          "VEHICLE_GONE: world queue, reliable, level 3, play too, host to guest, 9 fields, 21 bytes");
    Check(req->queue == 1 && req->wire == world::Wire::reliable && req->level == 3 && !req->lab_only && req->request &&
              req->dir == world::Dir::guest_to_host && req->lua == 7 && req->native == 0 && req->rx == 0 && req->body == 18,
          "REQ_VEHICLE_GONE: a world-queue request, guest to host, 7 fields, 18 bytes");
    Setup(3, 3, true);
    Pair(30050, 100);
    Drain(g_host, 3, 100);
    Drain(g_guest, 3, 100);
    // seq, id, cause, actor, rid, vehicle type, x, y, z
    Check(Tx(g_host, op::vehicle_gone, {1, 25603, 1, 0, 0, 255, -203395, 800, 52899}, 101) == world::TxStatus::ok,
          "the host announces a Fultoned sortie jeep (play)");
    Check(Tx(g_guest, op::vehicle_gone, {1, 25603, 1, 0, 0, 255, 0, 0, 0}, 101) == world::TxStatus::wrong_role,
          "only the host announces a vehicle gone");
    Check(Tx(g_host, op::req_vehicle_gone, {1, 25603, 1, 255, 0, 0, 0}, 101) == world::TxStatus::wrong_role, "only the guest requests");
    Check(Tx(g_host, op::vehicle_gone, {2, 25599, 1, 0, 0, 255, 0, 0, 0}, 101) == world::TxStatus::bad_args &&
              Tx(g_host, op::vehicle_gone, {2, 26112, 1, 0, 0, 255, 0, 0, 0}, 101) == world::TxStatus::bad_args,
          "an id outside the TppVehicle2 class (25600..26111) is refused");
    Check(Tx(g_host, op::vehicle_gone, {2, 25603, 0, 0, 0, 255, 0, 0, 0}, 101) == world::TxStatus::bad_args &&
              Tx(g_host, op::vehicle_gone, {2, 25603, 4, 0, 0, 255, 0, 0, 0}, 101) == world::TxStatus::bad_args &&
              Tx(g_host, op::vehicle_gone, {2, 25603, 1, 3, 0, 255, 0, 0, 0}, 101) == world::TxStatus::bad_args &&
              Tx(g_host, op::vehicle_gone, {0, 25603, 1, 0, 0, 255, 0, 0, 0}, 101) == world::TxStatus::bad_args &&
              Tx(g_host, op::vehicle_gone, {2, 25603, 1, 0, 0, 256, 0, 0, 0}, 101) == world::TxStatus::bad_args &&
              Tx(g_host, op::vehicle_gone, {2, 25603, 1, 0, 0, 255, 1000000001, 0, 0}, 101) == world::TxStatus::bad_args,
          "cause 1..3, actor 0..2, seq 1.., type u8 and the position bound");
    Check(Tx(g_host, op::vehicle_gone, {2, 25600, 3, 2, 0, 0, 0, 0, 0}, 101) == world::TxStatus::ok &&
              Tx(g_host, op::vehicle_gone, {3, 26111, 2, 1, 65535, 7, 1000000000, -1000000000, 0}, 101) == world::TxStatus::ok,
          "the class bounds, the lab cause and a guest rid encode");
    Exchange(102);
    Check(!Find(Drain(g_guest, 1, 103), op::vehicle_gone), "VEHICLE_GONE never rides the control queue");
    auto rx = Drain(g_guest, 3, 104);
    Check(rx.size() == 3 && rx[0].op == op::vehicle_gone && rx[0].count == 9 && rx[0].v[0] == 1 && rx[0].v[1] == 25603 &&
              rx[0].v[2] == 1 && rx[0].v[3] == 0 && rx[0].v[4] == 0 && rx[0].v[5] == 255 && rx[0].v[6] == -203395 && rx[0].v[7] == 800 &&
              rx[0].v[8] == 52899 && rx[1].v[1] == 25600 && rx[2].v[1] == 26111 && rx[2].v[4] == 65535 && rx[2].v[6] == 1000000000,
          "the guest's world queue delivers each VEHICLE_GONE in order with its exact fields");
    // The guest's request: RECEIVED comes back natively, the host's Lua gets the fields.
    Check(Tx(g_guest, op::req_vehicle_gone, {0, 25603, 1, 255, 0, 0, 0}, 105) == world::TxStatus::bad_args, "rid 0 refused");
    Check(Tx(g_guest, op::req_vehicle_gone, {7, 25603, 1, 3, 210903, 693, 290108}, 105) == world::TxStatus::ok, "the guest requests");
    Exchange(106);
    const auto hostRx = Drain(g_host, 3, 107);
    const auto* got = Find(hostRx, op::req_vehicle_gone);
    Check(got && got->count == 7 && got->v[0] == 7 && got->v[1] == 25603 && got->v[2] == 1 && got->v[3] == 3 && got->v[4] == 210903 &&
              got->v[5] == 693 && got->v[6] == 290108,
          "the host's world queue delivers REQ_VEHICLE_GONE with its exact fields");
    Exchange(108);
    const auto ack = Drain(g_guest, 1, 109);
    const auto* received = Find(ack, op::received);
    Check(received && received->v[0] == 7 && received->v[1] == op::req_vehicle_gone, "the host answers the request with RECEIVED");
    // A request of another host epoch gets REJECT 5 naming the vehicle.
    world::Op stale{};
    stale.code = op::req_vehicle_gone;
    stale.epoch = static_cast<std::uint16_t>(g_host.epoch() + 1);
    stale.count = 7;
    const std::int64_t staleValues[7] = {8, 25604, 1, 255, 0, 0, 0};
    for (unsigned i = 0; i < 7; ++i) stale.v[i] = staleValues[i];
    std::uint8_t packet[64]{};
    world::WriteBundleHeader(packet, 1);
    const auto size = world::EncodeOp(stale, packet + world::kBundleHeaderSize, sizeof(packet) - world::kBundleHeaderSize);
    Check(size == world::kOpHeaderSize + 18, "a stale request encodes");
    g_host.OnReliable(packet, size + world::kBundleHeaderSize, 110);
    Check(!Find(Drain(g_host, 3, 111), op::req_vehicle_gone), "a request of another epoch never reaches the host's Lua");
    Exchange(112);
    const auto rejects = Drain(g_guest, 1, 113);
    const auto* rej = Find(rejects, op::reject);
    Check(rej && rej->v[0] == 8 && rej->v[1] == op::req_vehicle_gone && rej->v[2] == world::reject::epoch && rej->v[3] == 25604,
          "a stale request gets REJECT 5 with the vehicle id");
    // A VEHICLE_GONE of another host epoch is purged on the guest.
    world::Op old{};
    old.code = op::vehicle_gone;
    old.epoch = static_cast<std::uint16_t>(g_guest.epoch() + 1);
    old.count = 9;
    const std::int64_t oldValues[9] = {9, 25605, 1, 0, 0, 255, 0, 0, 0};
    for (unsigned i = 0; i < 9; ++i) old.v[i] = oldValues[i];
    world::WriteBundleHeader(packet, 1);
    const auto oldSize = world::EncodeOp(old, packet + world::kBundleHeaderSize, sizeof(packet) - world::kBundleHeaderSize);
    Check(oldSize == world::kOpHeaderSize + 21, "a stale announcement encodes");
    g_guest.OnReliable(packet, oldSize + world::kBundleHeaderSize, 114);
    Check(!Find(Drain(g_guest, 3, 115), op::vehicle_gone), "a VEHICLE_GONE of another host epoch is purged");
    // Unpaired: nothing goes out.
    g_host.LocalMissionEnd(116);
    Check(Tx(g_host, op::vehicle_gone, {4, 25603, 1, 0, 0, 255, 0, 0, 0}, 117) == world::TxStatus::not_paired, "unpaired: not sent");
    // Level 2 carries neither; a level-2 host answers a request with REJECT 10.
    Setup(2, 2, true);
    Pair(30050, 200);
    Check(Tx(g_host, op::vehicle_gone, {1, 25603, 1, 0, 0, 255, 0, 0, 0}, 201) == world::TxStatus::disabled &&
              Tx(g_guest, op::req_vehicle_gone, {1, 25603, 1, 255, 0, 0, 0}, 201) == world::TxStatus::disabled,
          "both ops need world level 3");
    stale.epoch = g_host.epoch();
    stale.v[0] = 9;
    world::WriteBundleHeader(packet, 1);
    const auto lowSize = world::EncodeOp(stale, packet + world::kBundleHeaderSize, sizeof(packet) - world::kBundleHeaderSize);
    g_host.OnReliable(packet, lowSize + world::kBundleHeaderSize, 202);
    Exchange(203);
    const auto low = Drain(g_guest, 1, 204);
    const auto* levelReject = Find(low, op::reject);
    Check(levelReject && levelReject->v[0] == 9 && levelReject->v[2] == world::reject::level && levelReject->v[3] == 25604,
          "a level-2 host rejects a vehicle request with REJECT 10");
}
// M1 host-owned companion (docs/vehicles-companions-design.md section 5): COMPANION 0x30 (both
// directions, world queue, reliable, level 3, play too) and CHARACTER 0x38 (guest to host, control
// queue, so it reaches the host in the helicopter). The seed key index takes 1..127 (the buddy keys).
void CompanionOps() {
    const auto* companion = world::FindOp(op::companion);
    const auto* character = world::FindOp(op::character);
    Check(op::companion == 0x30 && op::character == 0x38 && companion && character, "COMPANION 0x30 and CHARACTER 0x38 exist");
    Check(companion->queue == 1 && companion->wire == world::Wire::reliable && companion->level == 3 && !companion->lab_only &&
              !companion->request && companion->dir == world::Dir::both && companion->lua == 12 && companion->body == 29,
          "COMPANION: world queue, reliable, level 3, both directions, 12 fields, 29 bytes");
    Check(character->queue == 0 && character->wire == world::Wire::reliable && character->level == 3 && !character->lab_only &&
              !character->request && character->dir == world::Dir::guest_to_host && character->lua == 3 && character->body == 4,
          "CHARACTER: control queue, reliable, level 3, guest to host, 3 fields, 4 bytes");
    const auto* seed = world::FindOp(op::seed);
    Check(seed && world::FieldValid(seed->fields[2], 127) && !world::FieldValid(seed->fields[2], 128) &&
              !world::FieldValid(seed->fields[2], 0),
          "the seed key index takes 1..127");
    Setup(3, 3, true);
    Pair(30050, 100);
    Drain(g_host, 3, 100);
    Drain(g_guest, 3, 100);
    Check(Tx(g_host, op::companion, {1, 5000, 1, 0, 1, -203395, 800, 52899, 1571, 0, 0, 0}, 101) == world::TxStatus::ok,
          "the host announces its D-Dog");
    Check(Tx(g_host, op::companion, {2, 5000, 5, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 101) == world::TxStatus::bad_args,
          "kind 5 (battle gear) is refused");
    Check(Tx(g_guest, op::character, {1, 6, 1}, 101) == world::TxStatus::ok, "the guest says it plays Quiet");
    Check(Tx(g_host, op::character, {1, 6, 1}, 101) == world::TxStatus::wrong_role, "only the guest sends CHARACTER");
    Check(Tx(g_guest, op::character, {2, 8, 0}, 101) == world::TxStatus::bad_args, "an unknown character code is refused");
    Exchange(102);
    const auto atGuest = Drain(g_guest, 3, 103);
    const auto* dog = Find(atGuest, op::companion);
    Check(dog && dog->v[2] == 1 && dog->v[4] == 1 && dog->v[5] == -203395, "the guest receives the host's companion");
    const auto atHost = Drain(g_host, 3, 103);
    const auto* quiet = Find(atHost, op::character);
    Check(quiet && quiet->v[1] == 6 && quiet->v[2] == 1, "the host receives the guest's character");
    Setup(2, 2, true);
    Pair(30050, 200);
    Check(Tx(g_host, op::companion, {1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0}, 201) == world::TxStatus::disabled &&
              Tx(g_guest, op::character, {1, 6, 1}, 201) == world::TxStatus::disabled,
          "both ops need world level 3");
}
} // namespace

int main() {
    Table();
    Codec();
    Pairing();
    Publisher();
    Notes();
    AutoOps();
    Gating();
    Queues();
    Schedulers();
    Stats();
    Following();
    ConfigSession();
    WorldDisable();
    Alignment();
    SpawnSync();
    RescueAndBuddies();
    RideFast();
    RideTilt();
    VehicleGone();
    CompanionOps();
    std::printf("world_wire_tests: %u checks passed\n", checks);
    return 0;
}
