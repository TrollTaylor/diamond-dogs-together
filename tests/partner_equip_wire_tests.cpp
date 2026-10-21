#include <initializer_list>
// Native puppet equipment manifest codec (docs/native-puppet.md 6.2): bounds,
// revision 0 refused, epoch check, receiver store, version 2 (the slots' own
// gun types) next to version 1, and the sender's base-id scan. Pure; no game,
// no network.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/partner_equip_wire.hpp"
#include "mgscoop/partner_ride_wire.hpp"

namespace {
namespace ew = mgscoop::partner_equip_wire;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

ew::Manifest Sample() {
    ew::Manifest m{};
    m.revision = 7;
    m.epoch = 0x11223344u;
    m.inv_slot = 1;
    for (std::size_t i = 0; i < ew::kSlots; ++i) {
        m.slots[i].base_id = static_cast<std::uint16_t>(100 + i);
        m.slots[i].alias = static_cast<std::uint16_t>(0x2E + i);
        for (std::size_t k = 0; k < ew::kRecipe; ++k) m.slots[i].recipe[k] = static_cast<std::uint8_t>(i * 16 + k);
        m.slots[i].type = static_cast<std::uint8_t>(i == 0 ? 1 : i == 1 ? 4 : 2);
    }
    return m;
}
} // namespace

int main() {
    {
        namespace rw = mgscoop::partner_ride_wire;
        rw::Packet packet{rw::Kind::heli, 1, 1234}, decoded{};
        std::uint8_t ride[rw::kSize]{};
        Check(rw::Encode(packet, ride) && rw::Decode(ride, sizeof(ride), decoded) && decoded.kind==rw::Kind::heli &&
              decoded.revision==1 && decoded.sender_ms==1234, "ride 12-byte little-endian round trip");
        ew::Store legacy{};
        Check(!ew::Accept(legacy, ride, sizeof(ride), 7) && legacy.invalid==1 && !legacy.have,
              "old equipment receiver refuses ride without mutating its manifest");
        for (unsigned at : {0u,1u,2u,3u,4u,6u,7u}) {
            const auto prior=ride[at]; ride[at]=at==4 ? 0 : 255;
            Check(!rw::Decode(ride,sizeof(ride),decoded), "ride magic/version/kind/flags/revision/reserved refusals"); ride[at]=prior;
        }
        Check(!rw::Decode(ride,11,decoded) && !rw::Decode(nullptr,12,decoded), "ride size and null refusals");
        rw::Store store{};
        Check(!rw::Accept(store,ride,sizeof(ride),0,1000), "ride requires authenticated active link session");
        Check(rw::Accept(store,ride,sizeof(ride),7,1000) && rw::Live(store,7,6000) && !rw::Live(store,7,6001) && !rw::Live(store,8,1000),
              "ride heartbeat lease and session-scoped read");
        Check(!rw::Accept(store,ride,sizeof(ride),7,5900) && !rw::Live(store,7,6001), "duplicate cannot renew lease");
        packet.sender_ms=3234; rw::Encode(packet,ride);
        Check(rw::Accept(store,ride,sizeof(ride),7,6100) && rw::Live(store,7,6100), "same revision heartbeat renews lease");
        packet.kind=rw::Kind::foot; rw::Encode(packet,ride);
        Check(!rw::Accept(store,ride,sizeof(ride),7,6200), "same revision different state refused");
        packet.revision=2; packet.sender_ms=4000; rw::Encode(packet,ride);
        Check(rw::Accept(store,ride,sizeof(ride),7,6200), "fresh foot transition accepted");
        packet.revision=1; packet.sender_ms=4100; rw::Encode(packet,ride);
        Check(!rw::Accept(store,ride,sizeof(ride),7,6300), "stale revision refused");
        Check(rw::Accept(store,ride,sizeof(ride),8,6400), "new authenticated session starts its own revision sequence");
        rw::Visibility vis{}; rw::In in{};
        in.fresh=in.time_valid=in.anim_valid=in.body_anim_valid=in.lease_live=true;
        in.ride_have=in.ride_live=true; in.kind=rw::Kind::heli; in.remote_act=99; in.now_ms=1000;
        rw::Observe(vis,in); Check(vis.hidden && std::strcmp(vis.reason,"ride")==0, "heli ride hides without placement");
        in.kind=rw::Kind::foot; in.remote_act=1; in.sender_ms=1000;
        rw::Observe(vis,in);
        for(unsigned i=0;i<20;++i) { in.now_ms+=50; rw::Observe(vis,in); }
        Check(vis.hidden, "held sender sample cannot fabricate 500ms landing proof");
        for(unsigned i=1;i<=5;++i) { in.sender_ms=1000+i*100; rw::Observe(vis,in); }
        Check(!vis.hidden, "fresh foot pose stable for 500ms releases hide");
        // A true ring reset can preserve sender time and position exactly.
        vis={}; vis.hidden=true; in.generation=1; in.sender_ms=1000;
        rw::Observe(vis,in);
        for(unsigned i=1;i<=4;++i) { in.sender_ms=1000+i*100; rw::Observe(vis,in); }
        Check(vis.hidden && vis.stable, "landing has400ms proof before ring reset");
        in.generation=2; rw::Observe(vis,in);
        Check(vis.hidden && !vis.stable && vis.generation==2, "known ring identity change clears equal-clock equal-position proof");
        in.sender_ms=1500; rw::Observe(vis,in);
        Check(vis.hidden, "ring reset prevents old400ms joining new100ms to fake landing");
        for(unsigned i=1;i<=4;++i) { in.sender_ms=1500+i*100; rw::Observe(vis,in); }
        Check(!vis.hidden, "new ring supplies its own500ms landing proof");
        vis={}; vis.hidden=true; in.generation=0; in.sender_ms=3000;
        rw::Observe(vis,in);
        for(unsigned i=1;i<=5;++i) { in.sender_ms=3000+i*100; rw::Observe(vis,in); }
        Check(!vis.hidden && vis.generation==0, "legacy zero generation retains existing sender-time proof behavior");
        in.generation=0;
        in.kind=rw::Kind::heli; rw::Observe(vis,in); in.kind=rw::Kind::foot; in.sender_ms=2000; rw::Observe(vis,in);
        in.sender_ms=2100; in.y=1; rw::Observe(vis,in);
        Check(vis.hidden && !vis.stable, "vertical landing speed does not release");
        in.ride_live=false; rw::Observe(vis,in); Check(vis.hidden && std::strcmp(vis.reason,"ride_stale")==0, "expired ride lease stays hidden");
        in.ride_live=true; in.lease_live=false; rw::Observe(vis,in); Check(vis.hidden && !vis.have_sample, "lost link clears landing proof");
        in.lease_live=true; in.jump=true; rw::Observe(vis,in); Check(!vis.have_sample, "teleport discontinuity clears proof"); in.jump=false;
        vis={}; in.kind=rw::Kind::foot; in.body_act=25; in.remote_act=1; in.body_valid=true; in.y=5; in.body_y=1;
        in.now_ms=4000; rw::Placed(vis,3900); rw::Observe(vis,in);
        Check(!vis.hidden && !vis.air && vis.air_pending, "one post-placement fall frame (a teleport Warp) is only an air candidate");
        in.now_ms=4249; rw::Observe(vis,in); Check(!vis.air && !vis.hidden, "the candidate must hold kAirHoldMs");
        in.now_ms=4250; rw::Observe(vis,in);
        Check(vis.hidden && vis.air && std::strcmp(vis.reason,"air")==0, "post-placement fall mismatch over2m held 250ms hides");
        in.body_act=25; in.now_ms=8000; rw::Observe(vis,in); Check(vis.air && vis.hidden, "air act stays unplaceable after window");
        in.body_act=1; rw::Observe(vis,in); Check(!vis.air && vis.hidden, "changed body act still waits fresh stability");
        vis={}; in.body_act=40; in.sender_ms=9000; in.now_ms=9000; in.rearmed=false;
        rw::Observe(vis,in); Check(vis.hidden && std::strcmp(vis.reason,"dead")==0, "dead mirror body hides");
        for(unsigned i=1;i<=5;++i) { in.sender_ms=9000+i*100; rw::Observe(vis,in); }
        Check(!vis.hidden && vis.dead_retry, "dead mirror permits one stable-foot re-show attempt");
        rw::Placed(vis,9500); in.sender_ms=9600; rw::Observe(vis,in);
        Check(!vis.hidden, "dead re-show allows placement then next-tick realization");
        in.rearmed=true; in.sender_ms=9700; rw::Observe(vis,in);
        Check(vis.hidden && vis.dead_locked, "persistent dead act after rearm locks rest of mission");
    }
    {
        // The air candidate that ends before kAirHoldMs (the Warp's few frames) never hides.
        namespace rw = mgscoop::partner_ride_wire;
        rw::Visibility v{}; rw::In in{};
        in.fresh = in.time_valid = in.anim_valid = in.body_anim_valid = in.body_valid = in.lease_live = true;
        in.remote_act = 1; in.y = 5; in.body_y = 1; in.body_act = 25; in.now_ms = 1000;
        rw::Placed(v, 950); rw::Observe(v, in);
        in.now_ms = 1100; in.body_act = 1; rw::Observe(v, in);
        Check(!v.air_pending && !v.air && !v.hidden, "a broken candidate resets");
        in.now_ms = 1150; in.body_act = 25; rw::Observe(v, in); in.now_ms = 1350; rw::Observe(v, in);
        Check(!v.air && !v.hidden, "the hold restarts after a reset");
        in.now_ms = 1400; rw::Observe(v, in); Check(v.air && v.hidden, "a restarted hold of 250ms hides");
        // The remote's own fall or landing is never air; outside the 1.5s window neither.
        v = {}; in.remote_act = 25; in.now_ms = 1000; rw::Placed(v, 950); rw::Observe(v, in); in.now_ms = 1400; rw::Observe(v, in);
        Check(!v.air && !v.air_pending, "remote falling too");
        v = {}; in.remote_act = 1; in.now_ms = 3000; rw::Placed(v, 1000); rw::Observe(v, in); in.now_ms = 3400; rw::Observe(v, in);
        Check(!v.air, "outside the placement window");
    }
    {
        // Ride wire v2 (docs/anim-round1-seat.md): seat and vehicle for a vehicle only.
        namespace rw = mgscoop::partner_ride_wire;
        rw::Packet p{rw::Kind::vehicle, 7, 0x01020304u}, out{};
        p.seat = 1; p.vehicle = 25608;
        std::uint8_t b[rw::kSize]{};
        Check(rw::VersionFor(p) == 2 && rw::Encode(p, b) && b[0] == 0x52 && b[1] == 2 && b[2] == 2 && b[3] == 1 && b[4] == 7 && b[5] == 0 &&
                  b[6] == 0x08 && b[7] == 0x64 && b[8] == 4 && b[11] == 1, "v2 layout: seat byte 3, vehicle LE16 bytes 6..7");
        Check(rw::Decode(b, sizeof(b), out) && out.version == 2 && out.kind == rw::Kind::vehicle && out.seat == 1 && out.vehicle == 25608 &&
                  out.revision == 7 && out.sender_ms == 0x01020304u, "v2 round trip");
        p.seat = rw::kSeatUnknown;
        Check(rw::Encode(p, b) && b[1] == 2 && b[3] == 0xFF && rw::Decode(b, sizeof(b), out) && out.seat == rw::kSeatUnknown &&
                  out.vehicle == 25608, "v2 unknown seat, known vehicle");
        p.vehicle = rw::kVehicleUnknown;
        Check(rw::VersionFor(p) == 1 && rw::Encode(p, b) && b[1] == 1 && b[3] == 0 && b[6] == 0 && b[7] == 0 && rw::Decode(b, sizeof(b), out) &&
                  out.version == 1 && out.seat == rw::kSeatUnknown && out.vehicle == rw::kVehicleUnknown,
              "a vehicle without seat or id stays v1 (an old receiver reads it)");
        p.seat = 2; p.kind = rw::Kind::horse;
        Check(rw::VersionFor(p) == 1 && rw::Encode(p, b) && b[1] == 1 && b[3] == 0, "only a vehicle carries a seat");
        p.kind = rw::Kind::vehicle; p.seat = 9; p.vehicle = 25608;
        Check(!rw::Encode(p, b), "seat 9 is not encodable");
        // v2 refusals.
        p.seat = 3; rw::Encode(p, b);
        std::uint8_t bad[rw::kSize]{};
        std::memcpy(bad, b, sizeof(b)); bad[3] = 4; Check(!rw::Decode(bad, sizeof(bad), out), "v2 seat 4 refused");
        std::memcpy(bad, b, sizeof(b)); bad[3] = 0xFE; Check(!rw::Decode(bad, sizeof(bad), out), "v2 seat 0xFE refused");
        std::memcpy(bad, b, sizeof(b)); bad[2] = 0; Check(!rw::Decode(bad, sizeof(bad), out), "v2 foot with a seat refused");
        std::memcpy(bad, b, sizeof(b)); bad[2] = 0; bad[3] = 0xFF; Check(!rw::Decode(bad, sizeof(bad), out), "v2 foot with a vehicle refused");
        std::memcpy(bad, b, sizeof(b)); bad[2] = 0; bad[3] = 0xFF; bad[6] = bad[7] = 0xFF;
        Check(rw::Decode(bad, sizeof(bad), out) && out.kind == rw::Kind::foot && out.version == 2, "v2 foot without seat or vehicle decodes");
        std::memcpy(bad, b, sizeof(b)); bad[1] = 3; Check(!rw::Decode(bad, sizeof(bad), out), "version 3 refused");
        std::memcpy(bad, b, sizeof(b)); bad[1] = 0; Check(!rw::Decode(bad, sizeof(bad), out), "version 0 refused");
        std::memcpy(bad, b, sizeof(b)); bad[4] = bad[5] = 0; Check(!rw::Decode(bad, sizeof(bad), out), "v2 revision 0 refused");
        // Store: a same-revision seat or vehicle change is stale; a new revision is accepted.
        rw::Store s{};
        p.seat = 0; p.vehicle = 25608; p.revision = 10; p.sender_ms = 100; rw::Encode(p, b);
        Check(rw::Accept(s, b, sizeof(b), 3, 1000) && s.packet.seat == 0, "v2 accepted");
        p.seat = 1; p.sender_ms = 200; rw::Encode(p, b);
        Check(!rw::Accept(s, b, sizeof(b), 3, 1100) && s.stale == 1, "same revision, other seat: stale");
        p.seat = 0; p.vehicle = 25609; rw::Encode(p, b);
        Check(!rw::Accept(s, b, sizeof(b), 3, 1100) && s.stale == 2, "same revision, other vehicle: stale");
        p.vehicle = 25608; rw::Encode(p, b);
        Check(rw::Accept(s, b, sizeof(b), 3, 1200), "same revision heartbeat accepted");
        p.revision = 11; p.seat = 1; p.sender_ms = 300; rw::Encode(p, b);
        Check(rw::Accept(s, b, sizeof(b), 3, 1300) && s.packet.seat == 1, "seat change with a new revision");
        Check(rw::SeatUsable(0, 25608) && rw::SeatUsable(3, 25600) && !rw::SeatUsable(4, 25608) && !rw::SeatUsable(0, 0xFFFF) &&
                  !rw::SeatUsable(0, 6656) && !rw::SeatUsable(0, 22529) && !rw::SeatUsable(rw::kSeatUnknown, 25608),
              "a usable seat is 0..3 in a vehicle-type id (never a horse 6656 or walker 22529)");
    }
    {
        // Observe in seat mode: a live vehicle ride in a usable seat is seated, not hidden.
        namespace rw = mgscoop::partner_ride_wire;
        rw::Visibility v{}; v.hidden = true; v.reason = "ride_stale";
        rw::In in{};
        in.fresh = in.time_valid = in.anim_valid = in.body_anim_valid = in.lease_live = true;
        in.ride_have = in.ride_live = true; in.kind = rw::Kind::vehicle; in.remote_act = 111; in.now_ms = 1000; in.sender_ms = 1000;
        in.seat_mode = true; in.seat = 0; in.vehicle = 25608;
        rw::Observe(v, in);
        Check(v.seated && !v.hidden && !v.reason, "seated is shown at once (no landing proof)");
        in.seat_mode = false; rw::Observe(v, in);
        Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "seat mode off: the vehicle ride hides");
        in.seat_mode = true; in.seat = rw::kSeatUnknown; rw::Observe(v, in);
        Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "an unknown seat hides");
        in.seat = 1; in.vehicle = 6656; rw::Observe(v, in);
        Check(!v.seated && v.hidden, "a non-vehicle id hides");
        in.vehicle = 25608; in.kind = rw::Kind::horse; rw::Observe(v, in);
        Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "a horse still hides in seat mode");
        in.kind = rw::Kind::vehicle; rw::Observe(v, in); Check(v.seated && !v.hidden, "back to seated");
        in.remote_act = 97; rw::Observe(v, in);
        Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride") == 0, "act 97 (heli boarding) hides, even seated");
        in.remote_act = 111; rw::Observe(v, in); Check(v.seated, "seated again");
        in.ride_live = false; rw::Observe(v, in);
        Check(!v.seated && v.hidden && std::strcmp(v.reason, "ride_stale") == 0, "a stale ride lease hides");
        in.ride_live = true; in.lease_live = false; rw::Observe(v, in);
        Check(!v.seated && v.hidden, "a dead link hides");
        in.lease_live = true; rw::Observe(v, in); Check(v.seated && !v.hidden, "seated after the link returns");
        in.body_act = 40; in.rearmed = true; v.dead_retry = true; rw::Observe(v, in);
        Check(!v.seated && v.hidden && std::strcmp(v.reason, "dead") == 0, "a locked dead body never seats");
        // Foot after seated: not hidden (the seat flow's exit owns the body until DM End).
        v = {}; in.body_act = 139; in.rearmed = false; in.kind = rw::Kind::vehicle; rw::Observe(v, in); Check(v.seated, "seated with the DM act");
        in.kind = rw::Kind::foot; in.remote_act = 1; rw::Observe(v, in);
        Check(!v.seated && !v.hidden, "a remote back on foot is neither seated nor hidden");
        Check(rw::kHeliActFirst == 97 && rw::kHeliActLast == 108, "heli acts 97..108");
    }
    {
        namespace rw = mgscoop::partner_ride_wire;
        // No ride heartbeat ever received in this epoch is deliberately distinct
        // from an expired known heartbeat; neither state bypasses the link lease.
        rw::Visibility v{}; v.hidden = true;
        rw::In sample{};
        sample.fresh = sample.time_valid = sample.anim_valid = sample.lease_live = true;
        sample.remote_act = 1; sample.generation = 1;
        for (unsigned i = 0; i <= 5; ++i) { sample.sender_ms = 1000 + i * 100; sample.now_ms = sample.sender_ms; rw::Observe(v, sample); }
        Check(!v.hidden, "never-received ride heartbeat permits stable foot poses on a live link");
        sample.ride_have = true; sample.ride_live = false;
        for (unsigned i = 0; i <= 6; ++i) { sample.sender_ms = 2000 + i * 100; sample.now_ms = sample.sender_ms; rw::Observe(v, sample); }
        Check(v.hidden && !v.have_sample && std::strcmp(v.reason, "ride_stale") == 0,
              "expired known heartbeat cannot reuse foot poses to prove landing");
        sample.ride_live = true; sample.kind = rw::Kind::foot;
        for (unsigned i = 0; i <= 4; ++i) { sample.sender_ms = 3000 + i * 100; sample.now_ms = sample.sender_ms; rw::Observe(v, sample); }
        Check(v.hidden, "renewed foot heartbeat still needs its own 500ms stability");
        sample.sender_ms = sample.now_ms = 3500; rw::Observe(v, sample);
        Check(!v.hidden, "renewed foot heartbeat plus fresh 500ms proof releases hiding");
        v = {}; sample.ride_have = sample.ride_live = false; sample.remote_act = 100;
        rw::Observe(v, sample);
        Check(v.hidden && std::strcmp(v.reason, "ride") == 0, "fresh heli act hides even without a heartbeat");
        v = {}; sample.remote_act = 1; sample.lease_live = false;
        for (unsigned i = 0; i <= 6; ++i) { sample.sender_ms = 4000 + i * 100; sample.now_ms = sample.sender_ms; rw::Observe(v, sample); }
        Check(v.hidden && !v.have_sample, "never-received heartbeat cannot make a disconnected link valid");
    }
    std::uint8_t buf[ew::kSize]{};
    const ew::Manifest m = Sample();
    Check(ew::kSizeV1 == 60 && ew::kSize == 64 && ew::Encode(m, buf) == 64, "encode 64 bytes (version 2)");
    Check(buf[0] == 0x45 && buf[1] == 2 && buf[2] == 7 && buf[3] == 0 && buf[4] == 0x44 && buf[7] == 0x11 && buf[8] == 1 && buf[9] == 3 &&
          buf[10] == 0 && buf[11] == 0, "header layout");
    Check(buf[12] == 100 && buf[14] == 0x2E && buf[16] == 0 && buf[12 + 16] == 101 && buf[59] == 0x2B, "slot layout");
    Check(buf[60] == 1 && buf[61] == 4 && buf[62] == 2 && buf[63] == 0, "type[3] at 60..62, box bit clear");
    Check(ew::CboxEquip(0x1C8) && ew::CboxEquip(0x1E2) && !ew::CboxEquip(0x1E3) && !ew::CboxEquip(0x1C7),
          "box ids are 0x1C8..0x1E2");
    ew::Manifest got{};
    Check(ew::Decode(buf, sizeof(buf), m.epoch, got) == ew::Result::ok && got.revision == 7 && got.epoch == m.epoch && got.inv_slot == 1 &&
          got.version == 2 && ew::SameSlots(got, m) && got.slots[0].type == 1 && got.slots[1].type == 4 && got.slots[2].type == 2,
          "round trip");
    Check(ew::Decode(buf, sizeof(buf), 0, got) == ew::Result::ok, "epoch 0 accepts any epoch");
    Check(ew::Decode(buf, sizeof(buf), m.epoch + 1, got) == ew::Result::stale_epoch, "another epoch is stale");
    Check(ew::Decode(buf, sizeof(buf) - 1, 0, got) == ew::Result::size && ew::Decode(nullptr, 64, 0, got) == ew::Result::size, "size");
    // Version 1 (the 0.12.0 layout, 60 bytes): still accepted, types unknown.
    {
        std::uint8_t v1[ew::kSizeV1]{};
        std::memcpy(v1, buf, sizeof(v1));
        v1[1] = 1;
        ew::Manifest old{};
        Check(ew::Decode(v1, sizeof(v1), m.epoch, old) == ew::Result::ok && old.version == 1 && old.slots[1].base_id == 101 &&
                  old.slots[0].type == 0 && old.slots[1].type == 0 && old.slots[2].type == 0, "version 1 decodes (types 0)");
        Check(ew::Decode(buf, sizeof(buf), 0, old) == ew::Result::ok, "version 2 decodes");
        std::uint8_t mixed[ew::kSize]{};
        std::memcpy(mixed, buf, sizeof(mixed));
        mixed[1] = 1;
        Check(ew::Decode(mixed, sizeof(mixed), 0, old) == ew::Result::size, "version 1 with 64 bytes: size");
        v1[1] = 2;
        Check(ew::Decode(v1, sizeof(v1), 0, old) == ew::Result::size, "version 2 with 60 bytes: size");
        ew::Store st{};
        v1[1] = 1;
        Check(ew::Accept(st, v1, sizeof(v1), m.epoch) && st.have && st.manifest.version == 1, "the store accepts version 1");
    }
    ew::Manifest bad = m;
    bad.revision = 0;
    Check(ew::Encode(bad, buf) == 0 && buf[0] == 0, "revision 0 refused by encode (zeroed)");
    bad = m;
    bad.epoch = 0;
    Check(ew::Encode(bad, buf) == 0, "epoch 0 refused");
    bad = m;
    bad.inv_slot = 16;
    Check(ew::Encode(bad, buf) == 0, "inv_slot 16 refused");
    bad.inv_slot = 0xFF;
    Check(ew::Encode(bad, buf) == ew::kSize, "inv_slot unknown accepted");
    bad = m;
    bad.slots[2].type = 9;
    Check(ew::Encode(bad, buf) == 0, "a type above 8 refused by encode");
    struct Tweak { std::size_t at; std::uint8_t value; ew::Result expect; };
    const Tweak tweaks[] = {{0, 0x46, ew::Result::magic}, {1, 3, ew::Result::version}, {8, 16, ew::Result::inv_slot},
                            {9, 2, ew::Result::slot_count}, {63, 2, ew::Result::reserved}, {60, 9, ew::Result::type},
                            {62, 0x20, ew::Result::type}};
    for (const Tweak& t : tweaks) {
        ew::Encode(m, buf);
        buf[t.at] = t.value;
        Check(ew::Decode(buf, sizeof(buf), 0, got) == t.expect, ew::ResultName(t.expect));
    }
    ew::Encode(m, buf);
    buf[2] = buf[3] = 0;
    Check(ew::Decode(buf, sizeof(buf), 0, got) == ew::Result::revision, "revision 0 refused on decode");
    ew::Encode(m, buf);
    buf[4] = buf[5] = buf[6] = buf[7] = 0;
    Check(ew::Decode(buf, sizeof(buf), 0, got) == ew::Result::epoch, "epoch 0 refused on decode");
    // Version 2 reserved bytes: the active item id and the cardboard-box bit.
    {
        ew::Manifest box = m;
        box.cbox_active = true;
        box.active_item_id = 0x1C8;
        Check(ew::Encode(box, buf) == ew::kSize && buf[10] == 0xC8 && buf[11] == 0x01 && buf[63] == 1, "box fields pack");
        ew::Manifest out{};
        Check(ew::Decode(buf, sizeof(buf), box.epoch, out) == ew::Result::ok && out.cbox_active && out.active_item_id == 0x1C8 &&
                  !ew::SameBox(out, m), "box fields round-trip and change the manifest");
        // A box toggle is not a slot change: the receiver's gun writer is keyed on the slots only.
        Check(ew::SameSlots(out, m) && ew::SameBox(m, m), "box fields are outside SameSlots");
        // A box-only change under the stored revision is new content, not a duplicate.
        ew::Store boxed{};
        Check(ew::Encode(m, buf) == ew::kSize && ew::Accept(boxed, buf, sizeof(buf), m.epoch), "store the plain manifest");
        box.revision = m.revision;
        Check(ew::Encode(box, buf) == ew::kSize && ew::Accept(boxed, buf, sizeof(buf), m.epoch) && boxed.manifest.cbox_active &&
                  boxed.duplicates == 0,
              "a box-only change is stored, not a duplicate");
        ew::Encode(box, buf);
        std::uint8_t v1[ew::kSizeV1]{};
        std::memcpy(v1, buf, sizeof(v1));
        v1[1] = 1;
        Check(ew::Decode(v1, sizeof(v1), box.epoch, out) == ew::Result::reserved, "version 1 still refuses a nonzero item id");
    }
    // Store: new revisions stored, duplicates/invalid/stale counted only.
    ew::Store st{};
    ew::Encode(m, buf);
    Check(ew::Accept(st, buf, sizeof(buf), m.epoch) && st.have && st.received == 1 && st.manifest.revision == 7, "store accepts");
    Check(!ew::Accept(st, buf, sizeof(buf), m.epoch) && st.duplicates == 1 && st.received == 1, "duplicate counted");
    Check(!ew::Accept(st, buf, sizeof(buf), m.epoch + 1) && st.stale == 1, "stale epoch counted");
    Check(!ew::Accept(st, buf, 3, m.epoch) && st.invalid == 1 && st.manifest.revision == 7, "invalid counted, store kept");
    ew::Manifest next = m;
    next.revision = 8;
    ew::Encode(next, buf);
    Check(ew::Accept(st, buf, sizeof(buf), m.epoch) && st.manifest.revision == 8 && st.received == 2, "next revision stored");
    // The same revision and epoch with other content replaces it (a 0.12.0 sender restarts at revision 1 every
    // mission while the receiver keeps the session's manifest); only an exact repeat is a duplicate.
    {
        ew::Store old{};
        ew::Manifest first = m;
        first.revision = 1;
        std::uint8_t v1[ew::kSizeV1]{};
        ew::Encode(first, buf);
        std::memcpy(v1, buf, sizeof(v1));
        v1[1] = 1;
        Check(ew::Accept(old, v1, sizeof(v1), m.epoch) && old.manifest.revision == 1 && old.manifest.slots[2].alias == 0x30,
              "a 0.12.0 sender's mission 1 manifest (version 1, revision 1)");
        v1[12 + 2 * 16 + 2] = 0x31;  // its next mission: another secondary alias, revision 1 again
        Check(ew::Accept(old, v1, sizeof(v1), m.epoch) && old.manifest.slots[2].alias == 0x31 && old.received == 2 && old.duplicates == 0,
              "revision 1 again with a new loadout: replaced, not a duplicate");
        Check(!ew::Accept(old, v1, sizeof(v1), m.epoch) && old.duplicates == 1 && old.received == 2, "an exact repeat: duplicate");
        v1[8] = 2;  // only the inventory slot moved
        Check(ew::Accept(old, v1, sizeof(v1), m.epoch) && old.manifest.inv_slot == 2 && old.received == 3, "inv_slot change: replaced");
        Check(ew::Accept(old, buf, sizeof(buf), m.epoch) && old.manifest.version == 2 && old.manifest.slots[2].type == 2 && old.received == 4,
              "the same revision as version 2 (types): replaced");
        Check(!ew::Accept(old, buf, sizeof(buf), m.epoch) && old.duplicates == 2, "and its repeat is a duplicate");
    }
    Check(std::strcmp(ew::ResultName(ew::Result::stale_epoch), "stale_epoch") == 0 && std::strcmp(ew::ResultName(ew::Result::type), "type") == 0,
          "result names");
    // SameSlots compares every slot field (the type too), never the box fields.
    ew::Manifest other = m;
    Check(ew::SameSlots(other, m), "same slots");
    other.slots[1].type = 6;
    Check(!ew::SameSlots(other, m), "a type change is a change");
    other = m;
    other.slots[2].recipe[11] ^= 1;
    Check(!ew::SameSlots(other, m), "a recipe change is a change");
    // The type table index -> equip id (A2A3E0): ids below 0x230 themselves, 0x230.. -> 0x400.., 0x280.. -> 0x600...
    Check(ew::TableIndexToId(46) == 46 && ew::TableIndexToId(0x22F) == 0x22F && ew::TableIndexToId(0x230) == 0x400 &&
              ew::TableIndexToId(0x27F) == 0x44F && ew::TableIndexToId(0x280) == 0x600 && ew::TableIndexToId(0x288) == 0x608,
          "table index -> id");
    Check(ew::GunType(1) && ew::GunType(8) && !ew::GunType(0) && !ew::GunType(9) && !ew::GunType(11), "gun types 1..8");
    // The base-id scan of run 53e36944: alias 871 (type 1, receiver 69). Index 343 is type 11 whose row 105 holds receiver
    // 69: never a base. The first type-1 index with receiver 69 is 0x232 (id 0x402); type-1 rows with other receivers lose.
    {
        static std::uint16_t packed[649]{};
        static std::uint8_t receivers[514]{};
        packed[36] = static_cast<std::uint16_t>((20 << 6) | 1);    // type 1, row 20: receiver 70
        receivers[19] = 70;
        packed[343] = static_cast<std::uint16_t>((105 << 6) | 11); // type 11, row 105: receiver 69 (not a gun)
        receivers[104] = 69;
        packed[0x232] = static_cast<std::uint16_t>((300 << 6) | 1); // type 1, row 300: receiver 69
        receivers[299] = 69;
        packed[0x234] = static_cast<std::uint16_t>((301 << 6) | 4); // type 4, row 301: receiver 69 (another type)
        receivers[300] = 69;
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 69) == 0x402, "alias 871: the type-1 index 0x232 -> id 0x402");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 4, 69) == 0x404, "a type-4 alias: index 0x234 -> id 0x404");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 11, 69) == 0, "type 11 is no gun type: base 0");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 2, 69) == 0, "no type-2 match: base 0");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 0) == 0 && ew::BaseFromTables(nullptr, 649, receivers, 514, 1, 69) == 0,
              "no receiver or no table: base 0");
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 70) == 36, "receiver 70: stock id 36");
        packed[0x232] = static_cast<std::uint16_t>((600 << 6) | 1);  // a row beyond GunBasic: skipped
        Check(ew::BaseFromTables(packed, 649, receivers, 514, 1, 69) == 0, "row out of range: skipped");
    }
    {
        namespace rw = mgscoop::partner_ride_wire;
        const rw::StreamPose rider{true, true, 4};
        const rw::StreamPose mountedAct{true, true, 40};
        const auto horse = rw::FollowStream(rw::Kind::horse, rider);
        Check(horse.rider_anchor && !horse.horse_entity && !horse.mounted_animation && !horse.vehicle_pose && !horse.walker_arm,
              "a horse rider keeps the streamed player anchor and does not gain a horse body or mounted write");
        const auto otherAct = rw::FollowStream(rw::Kind::horse, mountedAct);
        Check(otherAct.rider_anchor && !otherAct.mounted_animation && !otherAct.horse_entity,
              "an unverified mounted act is not written");
        const rw::StreamPose missing{};
        const auto absent = rw::FollowStream(rw::Kind::horse, missing);
        Check(!absent.rider_anchor && !absent.horse_entity && !absent.mounted_animation, "no transform means no horse follow");
        for (const auto kind : {rw::Kind::vehicle, rw::Kind::walker, rw::Kind::heli, rw::Kind::foot, rw::Kind::other}) {
            const auto follow = rw::FollowStream(kind, rider);
            Check(!follow.rider_anchor && !follow.horse_entity && !follow.mounted_animation && !follow.vehicle_pose && !follow.walker_arm,
                  "vehicle, walker, helicopter and on-foot streams do not invent a mount pose");
        }
    }
    std::printf("%u equipment manifest checks passed: version 2 layout (types), version 1 decode, round trip, "
                "revision/epoch/inv_slot/slot_count/reserved/type refusals, stale epoch, receiver store, field-wise change, "
                "table index mapping, gun-type base scan\n", checks);
    return 0;
}
