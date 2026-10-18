#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/partner_ride_wire.hpp"

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
} // namespace

int main() {
    namespace rw = mgscoop::partner_ride_wire;

    // 1. Seat helpers & packing
    Check(rw::SeatIndex(0) == 0, "SeatIndex 0");
    Check(rw::SeatIndex(1) == 1, "SeatIndex 1");
    Check(rw::SeatIndex(3) == 3, "SeatIndex 3");
    Check(rw::SeatIndex(rw::kSeatUnknown) == rw::kSeatUnknown, "SeatIndex unknown");
    Check(!rw::SeatRolePassenger(0), "Seat 0 is not passenger role");
    Check(!rw::SeatRolePassenger(1), "Raw seat 1 without bit is not passenger role flag");

    const std::uint8_t packed_driver = rw::PackSeat(0, false);
    Check(packed_driver == 0, "PackSeat driver is 0");
    Check(!rw::SeatRolePassenger(packed_driver), "Driver not passenger role");

    const std::uint8_t packed_passenger = rw::PackSeat(1, true);
    Check(rw::SeatIndex(packed_passenger) == 1, "PackSeat passenger index is 1");
    Check(rw::SeatRolePassenger(packed_passenger), "PackSeat passenger role bit is set");
    Check((packed_passenger & rw::kSeatRolePassenger) != 0, "kSeatRolePassenger bit active");

    // 2. SeatUsable & HorseUsable
    Check(rw::SeatUsable(0, 25608), "seat 0 is usable for vehicle 25608");
    Check(rw::SeatUsable(1, 25608), "seat 1 is usable for vehicle 25608");
    Check(rw::SeatUsable(packed_passenger, 25608), "packed passenger seat 1 is usable for vehicle");
    Check(!rw::SeatUsable(4, 25608), "seat 4 is not usable");
    Check(!rw::SeatUsable(0, rw::kVehicleUnknown), "unknown vehicle seat not usable");
    Check(!rw::SeatUsable(0, 6656), "horse id 6656 not usable as vehicle seat");

    Check(rw::HorseUsable(0, 6656), "seat 0 usable for horse 6656");
    Check(rw::HorseUsable(0, 100), "seat 0 usable for horse 100");
    Check(!rw::HorseUsable(1, 6656), "seat 1 not usable for horse");
    Check(!rw::HorseUsable(0, rw::kVehicleUnknown), "unknown horse not usable");

    // 3. Packet round-trip with multi-seat
    rw::Packet p{};
    p.kind = rw::Kind::vehicle;
    p.seat = rw::PackSeat(2, true);
    p.vehicle = 25608;
    p.revision = 5;
    p.sender_ms = 123456;

    std::uint8_t b[rw::kSize]{};
    Check(rw::VersionFor(p) == rw::kVersion, "VersionFor returns v2 for vehicle with seat and vehicle id");
    Check(rw::Encode(p, b), "Encode multi-seat passenger packet succeeds");

    rw::Packet out{};
    Check(rw::Decode(b, sizeof(b), out), "Decode multi-seat passenger packet succeeds");
    Check(out.version == rw::kVersion, "Decoded version is 2");
    Check(out.kind == rw::Kind::vehicle, "Decoded kind is vehicle");
    Check(rw::SeatIndex(out.seat) == 2, "Decoded seat index is 2");
    Check(rw::SeatRolePassenger(out.seat), "Decoded seat role is passenger");
    Check(out.vehicle == 25608, "Decoded vehicle is 25608");
    Check(out.revision == 5, "Decoded revision is 5");
    Check(out.sender_ms == 123456, "Decoded sender_ms matches");

    // 4. Horse Packet encoding/decoding
    rw::Packet horse_pkt{};
    horse_pkt.kind = rw::Kind::horse;
    horse_pkt.seat = 0;
    horse_pkt.vehicle = 6656;
    horse_pkt.revision = 3;
    horse_pkt.sender_ms = 9999;

    Check(rw::VersionFor(horse_pkt) == rw::kVersion, "VersionFor returns v2 for horse with valid vehicle id");
    Check(rw::Encode(horse_pkt, b), "Encode horse packet succeeds");
    Check(rw::Decode(b, sizeof(b), out), "Decode horse packet succeeds");
    Check(out.version == rw::kVersion, "Decoded horse version is 2");
    Check(out.kind == rw::Kind::horse, "Decoded horse kind is horse");
    Check(rw::SeatIndex(out.seat) == 0, "Decoded horse seat is 0");
    Check(out.vehicle == 6656, "Decoded horse vehicle is 6656");

    // 5. Visibility / Unhide mounted partner:
    // Only set s.hidden = true for ON_HELICOPTER (heli acts). Keep instance 1 visible when
    // in.kind == Kind::vehicle or Kind::horse with a valid vehicle_id.
    rw::Visibility v{};
    v.hidden = true;
    v.reason = "ride_stale";

    rw::In in{};
    in.fresh = in.time_valid = in.anim_valid = in.body_anim_valid = in.lease_live = true;
    in.ride_have = in.ride_live = true;
    in.now_ms = 1000;
    in.sender_ms = 1000;

    // Vehicle with seat_mode: seated and visible
    in.kind = rw::Kind::vehicle;
    in.seat_mode = true;
    in.seat = 1;
    in.vehicle = 25608;
    in.remote_act = 111;
    rw::Observe(v, in);
    Check(v.seated && !v.hidden && v.reason == nullptr, "vehicle in seat_mode is seated and visible");

    // Vehicle without seat_mode hides
    in.seat_mode = false;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "vehicle without seat_mode hides");

    // Horse with valid vehicle and seat_mode: NOT seated, but VISIBLE (not hidden)
    in.kind = rw::Kind::horse;
    in.seat_mode = true;
    in.seat = 0;
    in.vehicle = 6656;
    rw::Observe(v, in);
    Check(!v.seated && !v.hidden && v.reason == nullptr, "horse with valid vehicle stays visible");

    // Helicopter boarding act (97): hides
    in.remote_act = 97;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "helicopter boarding hides");

    // Helicopter airborne act (99): hides
    in.remote_act = 99;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "helicopter airborne hides");

    // Unknown vehicle: hides
    in.remote_act = 111;
    in.vehicle = rw::kVehicleUnknown;
    rw::Observe(v, in);
    Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "unknown vehicle hides");

    std::printf("partner_ride_wire_tests: ALL PASS\n");
    return 0;
}
