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
    Check(count == 27, "27 ops (W0-W2 wire ops and the local ops)");
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
                          {op::clock, 11}, {op::weather, 7}};
    for (const auto& s : sizes) Check(world::FindOp(s.code) && world::FindOp(s.code)->body == s.body, "documented body size");
    for (unsigned code = 0x09; code <= 0x0F; ++code) Check(!world::FindOp(static_cast<std::uint8_t>(code)), "reserved control codes");
    for (unsigned code = 0x18; code <= 0x1F; ++code) Check(!world::FindOp(static_cast<std::uint8_t>(code)), "reserved world codes");
    Check(!world::FindOp(0) && !world::FindOp(0x22) && !world::FindOp(0x79) && !world::FindOp(0xFF), "unknown codes");
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
    const std::uint8_t unknown[] = {0x19, 2, 0, 0, 0xEE, 0xEE};
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
    Check(Tx(g_host, 0x19, {}, 0) == world::TxStatus::unknown_op, "reserved code");
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
    g_host.Cfg(30010, true, 30010, 0);
    Lines(g_host);
    g_host.ApplyTrigger(trigger(30010), "goto=30010", 30010, false, 0);
    Check(HasLine(Lines(g_host), "world_goto=refused; reason=mission"), "goto only from 30050");
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
} // namespace

int main() {
    Table();
    Codec();
    Pairing();
    AutoOps();
    Gating();
    Queues();
    Schedulers();
    Stats();
    std::printf("world_wire_tests: %u checks passed\n", checks);
    return 0;
}
