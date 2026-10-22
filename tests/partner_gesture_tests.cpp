// Gestures (docs/native-puppet.md "Gestures"), pure part: the upper-act edge
// sender, the channel-2 wire (round trip, strict decode, older peers), the stock
// node tables, the receiver store, the driver's gate and the end decision.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "mgscoop/partner_caps_wire.hpp"
#include "mgscoop/partner_equip_wire.hpp"
#include "mgscoop/partner_gesture_policy.hpp"
#include "mgscoop/partner_ride_wire.hpp"
#include "coop_carry_wire.hpp"

namespace {
namespace gp = mgscoop::partner_gesture_policy;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, what);
        std::exit(1);
    }
}

void TestKinds() {
    Check(gp::KindForUpper(0x15) == gp::Kind::pickup && gp::KindForUpper(0x17) == gp::Kind::knock && gp::KindForUpper(0x18) == gp::Kind::sign_go &&
              gp::KindForUpper(0x19) == gp::Kind::sign_stop,
          "the four gesture upper bytes");
    for (unsigned v : {0u, 5u, 0x16u, 0x1Au, 0x1Bu, 0x11u, 0x12u, 0x13u, 0xFFu})
        Check(gp::KindForUpper(static_cast<std::uint8_t>(v)) == gp::Kind::none, "reload, traversal and other upper bytes are not gestures");
    Check(!std::strcmp(gp::KindName(gp::Kind::pickup), "pickup") && !std::strcmp(gp::KindName(gp::Kind::knock), "knock") &&
              !std::strcmp(gp::KindName(gp::Kind::sign_go), "sign_go") && !std::strcmp(gp::KindName(gp::Kind::sign_stop), "sign_stop") &&
              !std::strcmp(gp::KindName(static_cast<gp::Kind>(9)), "none"),
          "kind names");
    Check(!std::strcmp(gp::MethodName(gp::Method::set_current), "set") && !std::strcmp(gp::MethodName(gp::Method::reserve_reset), "reserve"),
          "method names");
}

void TestNodes() {
    // Pickup (FUN_14120E520): SetCurrentNode, 0.5 blend, stand idle / stand moving / squat; crawl refused.
    auto c = gp::NodeFor(gp::Kind::pickup, 1, false, false);
    Check(!c.refusal && c.node == 0x1E904603EB7DULL && c.method == gp::Method::set_current && c.blend == 0.5f, "pickup stand idle: StandRight");
    c = gp::NodeFor(gp::Kind::pickup, 1, true, true);
    Check(!c.refusal && c.node == 0x82E84B26E4CEULL && c.method == gp::Method::set_current, "pickup stand moving: StandLeft (hand ignored)");
    c = gp::NodeFor(gp::Kind::pickup, 2, false, false);
    Check(!c.refusal && c.node == 0xFF77B695370AULL, "pickup squat idle: SquatRight");
    c = gp::NodeFor(gp::Kind::pickup, 2, true, false);
    Check(!c.refusal && c.node == 0xFF77B695370AULL, "pickup squat moving: SquatRight");
    for (std::uint8_t s : {std::uint8_t{0}, std::uint8_t{3}, std::uint8_t{4}}) {
        c = gp::NodeFor(gp::Kind::pickup, s, false, false);
        Check(c.refusal && !std::strcmp(c.refusal, "stance") && c.method == gp::Method::none && !c.node, "pickup outside stand/squat refused");
    }
    // Hand signs: ReserveToResetNode, 1.0 blend, by stance (other row for 0 / 4).
    const std::uint64_t go[] = {0x2153458C564CULL, 0xE74E11BABA4BULL, 0x8881431FFF42ULL, 0x5FE632112A9AULL, 0x2153458C564CULL};
    const std::uint64_t stop[] = {0x724E8FE0F814ULL, 0xB98C80A7DDAEULL, 0x78CB18630AF1ULL, 0x825F953B5437ULL, 0x724E8FE0F814ULL};
    for (std::uint8_t s = 0; s <= 4; ++s) {
        c = gp::NodeFor(gp::Kind::sign_go, s, s == 2, false);
        Check(!c.refusal && c.node == go[s] && c.method == gp::Method::reserve_reset && c.blend == 1.0f, "hand sign Go node per stance");
        c = gp::NodeFor(gp::Kind::sign_stop, s, false, true);
        Check(!c.refusal && c.node == stop[s] && c.method == gp::Method::reserve_reset && c.blend == 1.0f, "hand sign Stop node per stance");
    }
    // Knock (StateKnock 0x11E7210): the hand selects the second node of each pair.
    const std::uint64_t knock[2][4] = {{0x174A0F341288ULL, 0xD64283446A67ULL, 0x7BC06465D606ULL, 0xC1143CAC83B1ULL},
                                       {0x1EC34B25A344ULL, 0x7B2D4F836A92ULL, 0xD08C8B213CABULL, 0x0430147D9D34ULL}};
    for (int hand = 0; hand < 2; ++hand)
        for (std::uint8_t s = 0; s <= 3; ++s) {
            c = gp::NodeFor(gp::Kind::knock, s, false, hand != 0);
            Check(!c.refusal && c.node == knock[hand][s] && c.method == gp::Method::reserve_reset && c.blend == 1.0f, "knock node per stance and hand");
        }
    c = gp::NodeFor(gp::Kind::none, 1, false, false);
    Check(c.refusal && !std::strcmp(c.refusal, "kind") && c.method == gp::Method::none, "no node without a kind");
    Check(gp::kStateNull == 0xC1A2050D3802ULL && gp::kGraphUpper == 2 && gp::kEndLayer == 3, "stateNull, graph 2, end layer 3");
}

void TestWire() {
    gp::Packet p{};
    p.kind = gp::Kind::sign_stop;
    p.stance = 2;
    p.speed = 3;
    p.flags = gp::Flag::Moving;
    p.seq = 0xBEEF;
    p.sender_ms = 0x12345678u;
    std::uint8_t b[gp::kSize]{};
    Check(gp::Encode(p, b), "encode");
    const std::uint8_t want[gp::kSize] = {0x47, 1, 3, 2, 3, 1, 0xEF, 0xBE, 0x78, 0x56, 0x34, 0x12};
    Check(std::memcmp(b, want, sizeof(want)) == 0, "the exact 12-byte layout");
    gp::Packet q{};
    Check(gp::Decode(b, sizeof(b), q) && q.kind == p.kind && q.stance == 2 && q.speed == 3 && q.flags == gp::Flag::Moving && q.seq == 0xBEEF &&
              q.sender_ms == 0x12345678u,
          "round trip");
    Check(gp::IsGesture(b, sizeof(b)) && !gp::IsGesture(nullptr, 0) && !gp::IsGesture(b, 0), "classifier");
    // Strict: every malformed variant refuses and leaves the output alone.
    const gp::Packet sentinel = q;
    auto refuses = [&](std::uint8_t (&bytes)[gp::kSize], std::size_t size, const char* what) {
        gp::Packet out = sentinel;
        Check(!gp::Decode(bytes, size, out) && out.seq == sentinel.seq && out.kind == sentinel.kind, what);
    };
    std::uint8_t m[gp::kSize]{};
    std::memcpy(m, b, sizeof(m));
    refuses(m, gp::kSize - 1, "short size");
    m[0] = 0x45; refuses(m, gp::kSize, "magic"); std::memcpy(m, b, sizeof(m));
    m[1] = 2; refuses(m, gp::kSize, "version"); std::memcpy(m, b, sizeof(m));
    m[2] = 0; refuses(m, gp::kSize, "kind 0"); std::memcpy(m, b, sizeof(m));
    m[2] = 5; refuses(m, gp::kSize, "kind 5"); std::memcpy(m, b, sizeof(m));
    m[3] = 4; refuses(m, gp::kSize, "stance 4"); std::memcpy(m, b, sizeof(m));
    m[4] = 4; refuses(m, gp::kSize, "speed 4"); std::memcpy(m, b, sizeof(m));
    m[5] = 2; refuses(m, gp::kSize, "unknown flag"); std::memcpy(m, b, sizeof(m));
    m[6] = m[7] = 0; refuses(m, gp::kSize, "seq 0"); std::memcpy(m, b, sizeof(m));
    std::uint8_t longer[gp::kSize + 4]{};
    std::memcpy(longer, b, gp::kSize);
    gp::Packet out{};
    Check(!gp::Decode(longer, sizeof(longer), out), "a longer message refuses");
    // Encode refuses what Decode would.
    gp::Packet bad = p;
    bad.seq = 0;
    Check(!gp::Encode(bad, b) && b[0] == 0, "encode refuses seq 0 (zeroed)");
    bad = p; bad.kind = gp::Kind::none; Check(!gp::Encode(bad, b), "encode refuses kind none");
    bad = p; bad.stance = 7; Check(!gp::Encode(bad, b), "encode refuses stance 7");
    bad = p; bad.flags = 0x80; Check(!gp::Encode(bad, b), "encode refuses unknown flags");
}

// What a 0.13.4 receiver (no gesture classifier) does with the message on
// channel 2 (partner_driver.cpp DrainEquip, in order): the vehicle mirror ('V',
// 32 bytes), the carry event (0x4B), the ride addendum and packet (0x52), the
// capability message (0x43), then the equipment manifest decoder.
enum class OldPath { vehicle, carry, ride_ext, ride, caps, equip };
OldPath OldClassify(const std::uint8_t* b, std::size_t n) {
    mgscoop::coop_carry_wire::Message carry{};
    if (n == 32 && b[0] == 'V') return OldPath::vehicle;
    if (mgscoop::coop_carry_wire::Parse(b, n, carry)) return OldPath::carry;
    if (mgscoop::partner_ride_wire::IsExt(b, n)) return OldPath::ride_ext;
    if (mgscoop::partner_ride_wire::IsRide(b, n)) return OldPath::ride;
    if (mgscoop::partner_caps_wire::IsCaps(b, n)) return OldPath::caps;
    return OldPath::equip;
}

void TestOlderPeers() {
    namespace ew = mgscoop::partner_equip_wire;
    gp::Packet p{};
    p.kind = gp::Kind::pickup;
    p.stance = 1;
    p.seq = 1;
    std::uint8_t b[gp::kSize]{};
    Check(gp::Encode(p, b), "encode a pickup");
    Check(OldClassify(b, sizeof(b)) == OldPath::equip, "an older receiver reaches its manifest decoder");
    ew::Manifest m{};
    Check(ew::Decode(b, sizeof(b), 7, m) == ew::Result::size, "the manifest decoder refuses the size first");
    std::uint8_t sized[ew::kSize]{};
    std::memcpy(sized, b, sizeof(b));
    Check(ew::Decode(sized, sizeof(sized), 7, m) == ew::Result::magic, "even at manifest size the 'G' magic refuses");
    // The store keeps its manifest: the gesture message only bumps `invalid`.
    ew::Store store{};
    store.have = true;
    store.manifest.revision = 4;
    store.manifest.epoch = 7;
    const ew::Store before = store;
    Check(!ew::Accept(store, b, sizeof(b), 7) && store.have && store.manifest.revision == 4 && store.invalid == before.invalid + 1 &&
              store.received == before.received,
          "an older receiver keeps its manifest and only counts the message invalid");
    // The v1 action events are a different frame kind and stay untouched (no new
    // kinds in partner_action_events: a v1 Valid would reject the whole frame).
    // Every gesture kind classifies the same way.
    for (std::uint8_t k = 1; k <= gp::kKindMax; ++k) {
        p.kind = static_cast<gp::Kind>(k);
        Check(gp::Encode(p, b) && OldClassify(b, sizeof(b)) == OldPath::equip, "every kind lands in the older decoder");
    }
    // And the other way: the gesture decoder never takes the other channel-2 messages.
    mgscoop::partner_ride_wire::Packet ride{};
    ride.kind = mgscoop::partner_ride_wire::Kind::vehicle;
    ride.revision = 3;
    std::uint8_t rb[mgscoop::partner_ride_wire::kSize]{};
    Check(mgscoop::partner_ride_wire::Encode(ride, rb), "ride fixture");
    gp::Packet out{};
    Check(!gp::IsGesture(rb, sizeof(rb)) && !gp::Decode(rb, sizeof(rb), out), "a ride packet is not a gesture");
    mgscoop::partner_caps_wire::Caps caps{};
    caps.revision = 1;
    caps.epoch = 7;
    caps.mode = 1;
    std::uint8_t cb[mgscoop::partner_caps_wire::kSize]{};
    Check(mgscoop::partner_caps_wire::Encode(caps, cb) == sizeof(cb), "caps fixture");
    Check(!gp::IsGesture(cb, sizeof(cb)), "a capability message is not a gesture");
    std::uint8_t eb[ew::kSize]{};
    ew::Manifest mf{};
    mf.revision = 1;
    mf.epoch = 7;
    mf.inv_slot = ew::kInvSlotUnknown;
    Check(ew::Encode(mf, eb) && !gp::IsGesture(eb, sizeof(eb)), "a manifest is not a gesture");
    Check(gp::kMagic != ew::kMagic && gp::kMagic != mgscoop::partner_ride_wire::kMagic && gp::kMagic != mgscoop::partner_caps_wire::kMagic &&
              gp::kMagic != 0x4B && gp::kMagic != 'V' && gp::kChannel == ew::kChannel,
          "a distinct first byte on the shared channel 2");
}

gp::SenderIn In(std::uint8_t upper, std::uint64_t now, std::uint8_t stance = 1, std::uint8_t speed = 0, std::uint32_t epoch = 9) {
    return gp::SenderIn{true, true, upper, stance, speed, epoch, now};
}

void TestSender() {
    gp::Sender s{};
    // The first sample is a baseline, even inside a gesture.
    Check(!gp::Observe(s, In(0x15, 100)).send && s.ready, "baseline inside a pickup sends nothing");
    Check(!gp::Observe(s, In(0x15, 133)).send, "a held byte is not an edge");
    Check(!gp::Observe(s, In(0, 166)).send, "back to 0");
    auto o = gp::Observe(s, In(0x15, 200, 1, 2));
    Check(o.send && o.packet.kind == gp::Kind::pickup && o.packet.seq == 1 && o.packet.stance == 1 && o.packet.speed == 2 &&
              o.packet.flags == gp::Flag::Moving && o.packet.sender_ms == 200,
          "a rising edge into 0x15 sends a moving pickup");
    Check(!gp::Observe(s, In(0x15, 233)).send, "no repeat while held");
    o = gp::Observe(s, In(0x18, 266, 2, 0));
    Check(o.send && o.packet.kind == gp::Kind::sign_go && o.packet.seq == 2 && o.packet.flags == 0 && o.packet.stance == 2,
          "pickup straight into a hand sign is an edge");
    o = gp::Observe(s, In(0x19, 300));
    Check(o.send && o.packet.kind == gp::Kind::sign_stop && o.packet.seq == 3, "Go straight into Stop");
    Check(!gp::Observe(s, In(5, 333)).send && !gp::Observe(s, In(0, 366)).send, "reload and idle send nothing");
    o = gp::Observe(s, In(0x17, 400, 3, 1));
    Check(o.send && o.packet.kind == gp::Kind::knock && o.packet.seq == 4 && o.packet.stance == 3, "a knock edge");
    // Invalid reads and a disabled key reset to a new baseline.
    Check(!gp::Observe(s, In(0, 433)).send, "idle");
    gp::SenderIn off = In(0x15, 466);
    off.valid = false;
    Check(!gp::Observe(s, off).send && !s.ready, "an unread tick drops the baseline");
    Check(!gp::Observe(s, In(0x15, 500)).send, "the next read is a baseline again (no edge across the gap)");
    off = In(0, 533);
    off.enabled = false;
    Check(!gp::Observe(s, off).send && !s.ready, "the key or the link off drops the baseline");
    Check(!gp::Observe(s, In(0, 566)).send, "baseline");
    Check(!gp::Observe(s, In(0x15, 600, 1, 0, 10)).send && s.epoch == 10, "a new link session is a baseline");
    Check(!gp::Observe(s, In(0, 633, 1, 0, 10)).send, "idle");
    o = gp::Observe(s, In(0x15, 666, 9, 7, 10));
    Check(o.send && o.packet.stance == 0 && o.packet.speed == 3 && o.packet.flags == gp::Flag::Moving && o.packet.seq == 5 && s.edges == 5,
          "an out-of-range stance is sent as 0 and the speed clamps to 3");
    std::uint8_t b[gp::kSize]{};
    Check(gp::Encode(o.packet, b), "every sender packet encodes");
    // seq never 0 on the wire.
    s.seq = 0xFFFF;
    Check(!gp::Observe(s, In(0, 700, 1, 0, 10)).send, "idle");
    o = gp::Observe(s, In(0x18, 733, 1, 0, 10));
    Check(o.send && o.packet.seq == 1, "seq wraps past 0");
    gp::SenderIn no_epoch = In(0, 766);
    no_epoch.epoch = 0;
    Check(!gp::Observe(s, no_epoch).send && !s.ready, "no link session: nothing");
}

void TestStore() {
    gp::Store st{};
    gp::Packet p{};
    p.kind = gp::Kind::sign_go;
    p.stance = 1;
    p.seq = 10;
    std::uint8_t b[gp::kSize]{};
    Check(gp::Encode(p, b) && gp::Accept(st, b, sizeof(b), 5, 1000) && st.have && st.packet.seq == 10 && st.received == 1 && st.received_ms == 1000,
          "the first packet");
    Check(!gp::Accept(st, b, sizeof(b), 5, 1010) && st.duplicates == 1, "a repeat is a duplicate");
    p.seq = 9;
    Check(gp::Encode(p, b) && !gp::Accept(st, b, sizeof(b), 5, 1020) && st.duplicates == 2 && st.packet.seq == 10, "an older seq is a duplicate");
    p.seq = 11;
    Check(gp::Encode(p, b) && gp::Accept(st, b, sizeof(b), 5, 1030) && st.packet.seq == 11, "a newer seq");
    p.seq = 3;
    Check(gp::Encode(p, b) && gp::Accept(st, b, sizeof(b), 6, 1040) && st.packet.seq == 3 && st.epoch == 6, "a new session accepts any seq");
    b[0] = 0;
    Check(!gp::Accept(st, b, sizeof(b), 6, 1050) && st.invalid == 1 && st.packet.seq == 3, "malformed: invalid, store unchanged");
    Check(gp::Newer(1, 0xFFFF) && gp::Newer(0x8000, 1) && !gp::Newer(1, 0x8001) && !gp::Newer(5, 5) && !gp::Newer(4, 5), "wrap-safe newer");
}

gp::GateIn Ready() {
    gp::GateIn g{};
    g.enabled = g.native = g.realized = g.remote_live = g.body_ok = g.installed = true;
    g.body_act = 1;
    return g;
}

void TestGate() {
    Check(gp::Gate(Ready()) == nullptr, "all clear");
    for (std::uint16_t act = 1; act <= 6; ++act) {
        auto g = Ready();
        g.body_act = act;
        Check(gp::Gate(g) == nullptr && gp::BodyReady(act, 0, 0), "locomotion acts 1..6 take a gesture");
    }
    struct Case { const char* want; void (*mutate)(gp::GateIn&); };
    const Case cases[] = {
        {"key_off", [](gp::GateIn& g) { g.enabled = false; }},
        {"uncertain", [](gp::GateIn& g) { g.uncertain = true; }},
        {"not_native", [](gp::GateIn& g) { g.native = false; }},
        {"unrealized", [](gp::GateIn& g) { g.realized = false; }},
        {"carry", [](gp::GateIn& g) { g.block = "carry"; }},
        {"remote_stale", [](gp::GateIn& g) { g.remote_live = false; }},
        {"not_installed", [](gp::GateIn& g) { g.installed = false; }},
        {"busy", [](gp::GateIn& g) { g.busy = true; }},
        {"body_unread", [](gp::GateIn& g) { g.body_ok = false; }},
        {"box", [](gp::GateIn& g) { g.body_act = 0x4D; }},
        {"body_act", [](gp::GateIn& g) { g.body_act = 0; }},
        {"body_act", [](gp::GateIn& g) { g.body_act = 7; }},
        {"body_act", [](gp::GateIn& g) { g.body_act = 21; }},
        {"body_q28", [](gp::GateIn& g) { g.body_q28 = 1; }},
        {"body_upper", [](gp::GateIn& g) { g.body_upper = 5; }},
    };
    for (const Case& c : cases) {
        auto g = Ready();
        c.mutate(g);
        const char* why = gp::Gate(g);
        Check(why && !std::strcmp(why, c.want), c.want);
    }
    auto g = Ready();
    g.enabled = false;
    g.busy = true;
    Check(!std::strcmp(gp::Gate(g), "key_off"), "the key is reported first");
    Check(!gp::BodyReady(1, 0, 0x15) && !gp::BodyReady(1, 2, 0) && !gp::BodyReady(0x4D, 0, 0), "body readiness");
}

gp::EndIn Running(gp::Kind kind, std::uint64_t elapsed) {
    return gp::EndIn{kind, true, true, true, false, true, 0, elapsed};
}

void TestEnd() {
    auto e = gp::End(Running(gp::Kind::pickup, 500));
    Check(e.via == gp::Via::none, "a running pickup keeps going");
    auto in = Running(gp::Kind::pickup, 100);
    in.clip_end = true;
    Check(gp::End(in).via == gp::Via::none, "no clip end before kMinPlayMs");
    in.elapsed_ms = 400;
    e = gp::End(in);
    Check(e.via == gp::Via::end && !std::strcmp(e.cause, "clip") && e.null_return, "pickup motion end: stateNull");
    in = Running(gp::Kind::pickup, 600);
    in.remote_speed = 3;
    Check(gp::End(in).via == gp::Via::none, "the remote's speed does not end a pickup");
    for (gp::Kind k : {gp::Kind::sign_go, gp::Kind::sign_stop, gp::Kind::knock}) {
        in = Running(k, 300);
        in.remote_speed = 2;
        e = gp::End(in);
        Check(e.via == gp::Via::end && !std::strcmp(e.cause, "speed") && e.null_return, "a running remote ends a sign or knock");
        in.remote_speed = 1;
        Check(gp::End(in).via == gp::Via::none, "walking does not");
        in.remote_speed = 3;
        in.remote_known = false;
        Check(gp::End(in).via == gp::Via::none, "an unknown remote speed does not");
    }
    in = Running(gp::Kind::sign_go, 2000);
    e = gp::End(in);
    Check(e.via == gp::Via::timeout && !std::strcmp(e.cause, "deadline") && e.null_return, "the 2000 ms deadline");
    in = Running(gp::Kind::sign_go, 100);
    in.ours = false;
    Check(gp::End(in).via == gp::Via::none, "graph 2 may still show the previous node within the grace");
    in.elapsed_ms = 400;
    e = gp::End(in);
    Check(e.via == gp::Via::override_ && !std::strcmp(e.cause, "node") && !e.null_return, "another owner of graph 2: no write");
    in = Running(gp::Kind::knock, 50);
    in.lease = false;
    e = gp::End(in);
    Check(e.via == gp::Via::override_ && !std::strcmp(e.cause, "lease") && e.null_return, "a lost lease returns our node to stateNull");
    in.ours = false;
    Check(!gp::End(in).null_return, "but never someone else's");
    in = Running(gp::Kind::pickup, 50);
    in.identity = false;
    e = gp::End(in);
    Check(e.via == gp::Via::override_ && !std::strcmp(e.cause, "identity") && !e.null_return, "another body: never written");
    Check(!std::strcmp(gp::ViaName(gp::Via::end), "end") && !std::strcmp(gp::ViaName(gp::Via::timeout), "timeout") &&
              !std::strcmp(gp::ViaName(gp::Via::override_), "override"),
          "via names");
}
} // namespace

int main() {
    TestKinds();
    TestNodes();
    TestWire();
    TestOlderPeers();
    TestSender();
    TestStore();
    TestGate();
    TestEnd();
    std::printf("partner_gesture_tests: %u checks passed\n", checks);
    return 0;
}
