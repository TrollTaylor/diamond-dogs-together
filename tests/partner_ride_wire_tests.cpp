// partner_ride_wire: the channel 2 ride packet as shipped in 0.13.1 (version 1
// and the vehicle-only version 2 with the native seat 0..3 and the vehicle id),
// its store, the WS3 addendum (version 3) and the seated/hidden visibility.
// 0.13.2's seat_role bit, PackSeat and horse seats were reverted in 0.13.3;
// these checks pin the wire both copies of 0.13.4 speak.
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <cstdlib>
#include <cstring>
#include "mgscoop/partner_ride_wire.hpp"

namespace {
namespace rw = mgscoop::partner_ride_wire;
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, message);
        std::exit(1);
    }
}
bool Named(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

void TestVersionsAndCodec() {
    std::uint8_t b[rw::kSize]{};
    rw::Packet out{};
    // Foot, heli, horse, walker and other: always version 1 (no seat, no vehicle bytes).
    for (rw::Kind kind : {rw::Kind::foot, rw::Kind::heli, rw::Kind::horse, rw::Kind::walker, rw::Kind::other}) {
        rw::Packet p{};
        p.kind = kind; p.revision = 7; p.sender_ms = 0x01020304u; p.seat = 0; p.vehicle = 25603;
        Check(rw::VersionFor(p) == rw::kVersion1, "a non-vehicle kind is version 1");
        Check(rw::Encode(p, b) && b[0] == rw::kMagic && b[1] == rw::kVersion1 && b[2] == static_cast<std::uint8_t>(kind) && b[3] == 0 &&
                  b[4] == 7 && b[5] == 0 && b[6] == 0 && b[7] == 0 && b[8] == 4 && b[11] == 1,
              "version 1 layout: no seat, no vehicle");
        Check(rw::Decode(b, sizeof(b), out) && out.version == rw::kVersion1 && out.kind == kind && out.revision == 7 &&
                  out.sender_ms == 0x01020304u && out.seat == rw::kSeatUnknown && out.vehicle == rw::kVehicleUnknown,
              "version 1 round trip (seat and vehicle unknown)");
    }
    // A vehicle without a seat or vehicle id stays version 1.
    rw::Packet bare{};
    bare.kind = rw::Kind::vehicle; bare.revision = 1;
    Check(rw::VersionFor(bare) == rw::kVersion1, "a vehicle without seat or id is version 1");
    // A vehicle with its native seat and id: version 2, every seat 0..3.
    for (std::uint8_t seat = 0; seat <= rw::kSeatMax; ++seat) {
        rw::Packet p{};
        p.kind = rw::Kind::vehicle; p.revision = 0x1234; p.sender_ms = 99; p.seat = seat; p.vehicle = 25603;
        Check(rw::VersionFor(p) == rw::kVersion, "a seated vehicle is version 2");
        Check(rw::Encode(p, b) && b[1] == rw::kVersion && b[3] == seat && b[4] == 0x34 && b[5] == 0x12 && b[6] == (25603 & 0xFF) &&
                  b[7] == (25603 >> 8),
              "version 2 layout: seat byte 3, vehicle bytes 6..7");
        Check(rw::Decode(b, sizeof(b), out) && out.version == rw::kVersion && out.kind == rw::Kind::vehicle && out.seat == seat &&
                  out.vehicle == 25603 && out.revision == 0x1234 && out.sender_ms == 99,
              "version 2 round trip");
    }
    // The 0.13.2 passenger-role bit (0x80) is not part of this wire: refused both ways.
    rw::Packet role{};
    role.kind = rw::Kind::vehicle; role.revision = 3; role.seat = 0x81; role.vehicle = 25603;
    Check(!rw::Encode(role, b), "a seat with the 0.13.2 role bit does not encode");
    role.seat = 1;
    Check(rw::Encode(role, b), "seat 1 encodes");
    b[3] = 0x81;
    Check(!rw::Decode(b, sizeof(b), out), "a seat with the role bit does not decode");
    b[3] = 4;
    Check(!rw::Decode(b, sizeof(b), out), "seat 4 does not decode");
    b[3] = rw::kSeatUnknown;
    Check(rw::Decode(b, sizeof(b), out) && out.seat == rw::kSeatUnknown, "the unknown seat decodes");
    // Encode refusals: revision 0, a kind outside 0..5, a seat above 3.
    rw::Packet bad{};
    bad.kind = rw::Kind::foot; bad.revision = 0;
    Check(!rw::Encode(bad, b), "revision 0 does not encode");
    bad.revision = 1; bad.kind = static_cast<rw::Kind>(6);
    Check(!rw::Encode(bad, b), "kind 6 does not encode");
    bad.kind = rw::Kind::vehicle; bad.seat = 4; bad.vehicle = 25603;
    Check(!rw::Encode(bad, b), "seat 4 does not encode");
    // Decode refusals: size, magic, kind, version, v1 reserved bytes, a v2 non-vehicle with a seat, revision 0.
    rw::Packet foot{};
    foot.kind = rw::Kind::foot; foot.revision = 2;
    Check(rw::Encode(foot, b), "foot encodes");
    Check(!rw::Decode(b, sizeof(b) - 1, out) && !rw::Decode(nullptr, sizeof(b), out), "size and null refused");
    std::uint8_t t[rw::kSize]{};
    std::memcpy(t, b, sizeof(t)); t[0] = 0x51;
    Check(!rw::Decode(t, sizeof(t), out) && !rw::IsRide(t, sizeof(t)) && rw::IsRide(b, sizeof(b)), "magic refused");
    std::memcpy(t, b, sizeof(t)); t[2] = 6;
    Check(!rw::Decode(t, sizeof(t), out), "kind 6 refused");
    std::memcpy(t, b, sizeof(t)); t[1] = 4;
    Check(!rw::Decode(t, sizeof(t), out), "version 4 refused");
    for (unsigned at : {3u, 6u, 7u}) {
        std::memcpy(t, b, sizeof(t)); t[at] = 1;
        Check(!rw::Decode(t, sizeof(t), out), "version 1 reserved byte refused");
    }
    std::memcpy(t, b, sizeof(t)); t[1] = rw::kVersion; t[3] = 0; t[6] = t[7] = 0xFF;
    Check(!rw::Decode(t, sizeof(t), out), "a version 2 foot packet with a seat refused");
    t[3] = rw::kSeatUnknown;
    Check(rw::Decode(t, sizeof(t), out) && out.kind == rw::Kind::foot, "a version 2 foot packet without seat or vehicle decodes");
    std::memcpy(t, b, sizeof(t)); t[4] = t[5] = 0;
    Check(!rw::Decode(t, sizeof(t), out), "revision 0 refused");
}

void TestStore() {
    rw::Store s{};
    std::uint8_t b[rw::kSize]{};
    rw::Packet p{};
    p.kind = rw::Kind::vehicle; p.revision = 5; p.sender_ms = 1000; p.seat = 0; p.vehicle = 25603;
    Check(rw::Encode(p, b), "store packet encodes");
    Check(!rw::Accept(s, b, sizeof(b), 0, 100) && s.invalid == 1 && !s.have, "epoch 0 refused");
    Check(rw::Accept(s, b, sizeof(b), 7, 100) && s.have && s.received == 1 && s.packet.seat == 0, "first packet stored");
    Check(!rw::Accept(s, b, sizeof(b), 7, 150) && s.duplicates == 1, "an exact repeat is a duplicate");
    p.sender_ms = 3000;  // a heartbeat: the same revision and state, later
    Check(rw::Encode(p, b) && rw::Accept(s, b, sizeof(b), 7, 2100) && s.received == 2, "a heartbeat refreshes");
    p.seat = 1;          // another state under the same revision is stale
    Check(rw::Encode(p, b) && !rw::Accept(s, b, sizeof(b), 7, 2200) && s.stale == 1, "a new state needs a new revision");
    p.revision = 4; p.seat = 0; p.sender_ms = 4000;
    Check(rw::Encode(p, b) && !rw::Accept(s, b, sizeof(b), 7, 2300) && s.stale == 2, "an older revision is stale");
    p.revision = 6; p.seat = 2; p.sender_ms = 5000;
    Check(rw::Encode(p, b) && rw::Accept(s, b, sizeof(b), 7, 2400) && s.packet.seat == 2 && s.packet.revision == 6, "a newer revision");
    Check(rw::Live(s, 7, 2400 + rw::kLeaseMs) && !rw::Live(s, 7, 2401 + rw::kLeaseMs) && !rw::Live(s, 8, 2400),
          "the lease is kLeaseMs and per epoch");
    p.revision = 1; p.sender_ms = 10;
    Check(rw::Encode(p, b) && rw::Accept(s, b, sizeof(b), 8, 2500) && s.epoch == 8 && s.packet.revision == 1, "a new epoch restarts");
    Check(rw::SeatUsable(0, 25603) && rw::SeatUsable(3, 25603) && !rw::SeatUsable(4, 25603) && !rw::SeatUsable(0, rw::kVehicleUnknown) &&
              !rw::SeatUsable(0, 6656) && !rw::SeatUsable(rw::kSeatUnknown, 25603),
          "SeatUsable: seats 0..3 of a type-50 vehicle id only (no horse id)");
}

void TestExt() {
    rw::Ext e{};
    e.features = rw::ExtFeature::SeatId | rw::ExtFeature::Tilt; e.category = 2; e.type = 0x21; e.revision = 6;
    e.vehicle = 25603; e.seat = 0; e.sender_ms = 5000;
    std::uint8_t b[rw::kExtSize]{};
    Check(rw::EncodeExt(e, b) && rw::IsExt(b, sizeof(b)) && b[1] == rw::kVersionExt && b[10] == 0 && b[11] == 0, "addendum encodes");
    rw::Ext out{};
    Check(rw::DecodeExt(b, sizeof(b), out) && rw::SameExt(out, e) && out.sender_ms == 5000, "addendum round trip");
    rw::Packet probe{};
    Check(!rw::Decode(b, sizeof(b), probe), "the addendum is not a v1/v2 ride packet");
    rw::Ext bad = e;
    bad.type = 0x31;  // category must be type >> 4
    Check(!rw::EncodeExt(bad, b), "a category that is not type >> 4 refused");
    bad = e; bad.vehicle = 6656;
    Check(!rw::EncodeExt(bad, b), "a non-vehicle id refused");
    bad = e; bad.features = 0x04;
    Check(!rw::EncodeExt(bad, b), "an unknown feature refused");
    rw::ExtStore s{};
    Check(rw::EncodeExt(e, b) && rw::AcceptExt(s, b, sizeof(b), 7, 2400), "addendum stored");
    rw::Packet ride{};
    ride.kind = rw::Kind::vehicle; ride.revision = 6; ride.seat = 0; ride.vehicle = 25603;
    Check(rw::RemoteCategory(s, ride, 7, 2500) == 2, "the category for the described revision, vehicle and seat");
    ride.revision = 7;
    Check(rw::RemoteCategory(s, ride, 7, 2500) == rw::kCategoryUnknown, "another revision: unknown");
    ride.revision = 6;
    Check(rw::RemoteCategory(s, ride, 7, 2401 + rw::kLeaseMs) == rw::kCategoryUnknown, "an expired addendum: unknown");
}

// M6 review R3: the v4 seat addendum (the driver's copy accepts a seat-1 passenger; its seat 1 is taken).
void TestSeatExt() {
    rw::SeatExt e{};
    e.flags = rw::SeatFlag::Front | rw::SeatFlag::OneTaken; e.revision = 9; e.vehicle = 25608; e.sender_ms = 0x01020304;
    std::uint8_t b[rw::kSeatExtSize]{};
    Check(rw::kSeatExtSize == 12 && rw::kVersionSeat == 4, "12 bytes, version 4");
    Check(rw::EncodeSeatExt(e, b) && rw::IsSeatExt(b, sizeof(b)) && b[0] == rw::kMagic && b[1] == 4 && b[2] == 5 && b[3] == 0 &&
              b[4] == 9 && b[5] == 0 && b[6] == (25608 & 0xFF) && b[7] == (25608 >> 8) && b[8] == 4 && b[11] == 1,
          "seat addendum layout: magic, 4, flags, 0, revision, vehicle, sender_ms");
    rw::SeatExt out{};
    Check(rw::DecodeSeatExt(b, sizeof(b), out) && out.flags == e.flags && out.revision == 9 && out.vehicle == 25608 &&
              out.sender_ms == 0x01020304u, "seat addendum round trip");
    rw::Packet probe{};
    Check(!rw::Decode(b, sizeof(b), probe), "an older receiver's v1/v2 decoder refuses it (version 4): dropped as an invalid ride packet");
    Check(!rw::IsExt(b, sizeof(b)), "and it is not the v3 addendum");
    std::uint8_t c[rw::kSeatExtSize]{};
    std::memcpy(c, b, sizeof(c));
    c[3] = 1;
    Check(!rw::DecodeSeatExt(c, sizeof(c), out), "a non-zero reserved byte refused");
    std::memcpy(c, b, sizeof(c));
    c[2] = 0x08;
    Check(!rw::DecodeSeatExt(c, sizeof(c), out), "an unknown flag refused");
    Check(!rw::DecodeSeatExt(b, sizeof(b) - 1, out) && !rw::IsSeatExt(b, 16), "the size is exact");
    rw::SeatExt bad = e;
    bad.revision = 0;
    Check(!rw::EncodeSeatExt(bad, b), "revision 0 refused");
    bad = e; bad.vehicle = 6656;
    Check(!rw::EncodeSeatExt(bad, b), "a non-vehicle id refused");
    bad = e; bad.vehicle = rw::kVehicleUnknown; bad.flags = rw::SeatFlag::Cab;
    Check(rw::EncodeSeatExt(bad, b), "no driven vehicle: the capability alone");
    bad.flags = rw::SeatFlag::OneTaken;
    Check(!rw::EncodeSeatExt(bad, b), "seat 1 taken needs a vehicle");
    // The store and the flags for the peer's stored ride.
    rw::SeatExtStore s{};
    Check(rw::EncodeSeatExt(e, b) && rw::AcceptSeatExt(s, b, sizeof(b), 7, 2400) && s.have, "stored");
    rw::Packet ride{};
    ride.kind = rw::Kind::vehicle; ride.revision = 9; ride.seat = 0; ride.vehicle = 25608;
    Check(rw::SeatFlagsFor(s, ride, 7, 2500) == (rw::SeatFlag::Front | rw::SeatFlag::OneTaken), "the flags for the described ride");
    ride.revision = 10;
    Check(rw::SeatFlagsFor(s, ride, 7, 2500) == 0, "another revision: none (the driver's newer ride is not described yet)");
    ride.revision = 9; ride.vehicle = 25609;
    Check(rw::SeatFlagsFor(s, ride, 7, 2500) == 0, "another vehicle: none");
    ride.vehicle = 25608;
    Check(rw::SeatFlagsFor(s, ride, 8, 2500) == 0, "another link epoch: none");
    Check(rw::SeatFlagsFor(s, ride, 7, 2401 + rw::kLeaseMs) == 0, "an expired addendum: none");
    ride.kind = rw::Kind::foot;
    Check(rw::SeatFlagsFor(s, ride, 7, 2500) == 0, "a ride that is no vehicle: none");
    ride.kind = rw::Kind::vehicle;
    rw::SeatExt older = e;
    older.sender_ms = 0x01020300;
    older.flags = rw::SeatFlag::Front;
    Check(rw::EncodeSeatExt(older, b) && !rw::AcceptSeatExt(s, b, sizeof(b), 7, 2600) && s.stale == 1, "an older sender_ms is stale");
    rw::SeatExt newer = e;
    newer.sender_ms = 0x01020400;
    newer.flags = rw::SeatFlag::Front;
    Check(rw::EncodeSeatExt(newer, b) && rw::AcceptSeatExt(s, b, sizeof(b), 7, 2700) &&
              rw::SeatFlagsFor(s, ride, 7, 2800) == rw::SeatFlag::Front, "a newer one replaces it (the dog left the seat)");
    Check(!rw::AcceptSeatExt(s, b, sizeof(b), 0, 2700) && s.invalid == 1, "no link epoch: refused");
}

void TestVisibility() {
    rw::In in{};
    in.fresh = in.time_valid = in.anim_valid = in.body_anim_valid = in.lease_live = true;
    in.ride_have = in.ride_live = true;
    in.now_ms = 1000; in.sender_ms = 1000;
    in.kind = rw::Kind::vehicle; in.seat_mode = true; in.seat = 0; in.vehicle = 25603; in.remote_act = 111;
    rw::Visibility v{};
    v.hidden = true; v.reason = "ride_stale";
    rw::Observe(v, in);
    Check(v.seated && !v.hidden && v.reason == nullptr, "a usable seat in seat mode: drawn seated");
    in.seat = 2;
    rw::Observe(v, in);
    Check(v.seated && !v.hidden, "a passenger seat of the same vehicle: seated");
    in.seat_mode = false;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && Named(v.reason, "ride"), "without seat mode a vehicle ride hides");
    in.seat_mode = true; in.seat = rw::kSeatUnknown;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && Named(v.reason, "ride"), "an unknown seat hides");
    in.seat = 0; in.vehicle = rw::kVehicleUnknown;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && Named(v.reason, "ride"), "an unknown vehicle hides");
    // 0.13.1: a horse rider hides (no seated horse motion); 0.13.2's visible horse was reverted.
    in.kind = rw::Kind::horse; in.vehicle = 6656; in.seat = 0;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && Named(v.reason, "ride"), "a horse rider hides");
    in.kind = rw::Kind::walker;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && Named(v.reason, "ride"), "a walker rider hides");
    // The heli acts 97..108 hide even in a usable vehicle seat.
    in.kind = rw::Kind::vehicle; in.vehicle = 25603;
    for (std::uint16_t act : {std::uint16_t(97), std::uint16_t(99), std::uint16_t(108)}) {
        in.remote_act = act;
        rw::Observe(v, in);
        Check(!v.seated && v.hidden && Named(v.reason, "ride"), "heli acts hide");
    }
    // A stale ride (stored, lease expired) hides as ride_stale.
    in.remote_act = 111; in.ride_live = false;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && Named(v.reason, "ride_stale"), "a stale ride hides");
    // Back on foot: shown again after 500 ms of calm samples.
    rw::Visibility f{};
    f.hidden = true; f.reason = "ride";
    rw::In walk{};
    walk.fresh = walk.time_valid = walk.anim_valid = walk.body_anim_valid = walk.lease_live = true;
    walk.ride_have = walk.ride_live = true; walk.kind = rw::Kind::foot; walk.remote_act = 2;
    for (int i = 0; i < 8; ++i) {
        walk.sender_ms = 1000 + i * 100; walk.now_ms = walk.sender_ms; walk.x = i * 0.1;
        rw::Observe(f, walk);
    }
    Check(!f.hidden && f.reason == nullptr && !f.seated, "on foot: shown again after 500 ms of calm samples");
}

} // namespace

int main() {
    TestVersionsAndCodec();
    TestStore();
    TestExt();
    TestSeatExt();
    TestVisibility();
    std::printf("%u partner ride wire checks passed: versions 1/2 (seat 0..3, no role bit), store, WS3 addendum, seated and hidden "
                "visibility (horse and walker hide)\n",
                checks);
    return 0;
}
