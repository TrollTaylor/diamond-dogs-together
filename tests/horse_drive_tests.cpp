// MGS_COOP_HORSE_DRIVE_V1 (horse plan M1, plan B): the pure part (native/include/mgscoop/horse_drive.hpp), the shared
// D-Horse rider's visibility (partner_ride_wire HorseRider / Observe with In::horse_shown) and the lab key.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/horse_drive.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_ride_wire.hpp"

namespace {
namespace hd = mgscoop::horse_drive;
namespace rw = mgscoop::partner_ride_wire;
unsigned checks = 0;
void Check(bool condition, const char* message) {
    ++checks;
    if (!condition) {
        std::fprintf(stderr, "failed check %u: %s\n", checks, message);
        std::exit(1);
    }
}
bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
bool Parse(const char* s, hd::Request& r) { return hd::Parse(s, std::strlen(s), r); }

void TestMatrix() {
    float m[16]{};
    hd::Matrix(1.f, 2.f, 3.f, 0.f, m);
    const float id[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 2, 3, 1};
    bool same = true;
    for (int i = 0; i < 16; ++i) same = same && Near(m[i], id[i]);
    Check(same, "yaw 0: identity rotation, translation in row 3 (the call-enter's 0x140E6AF3F matrix)");
    const float half = 1.5707963f;
    hd::Matrix(0.f, 0.f, 0.f, half, m);
    // Forward (row 2) is (sin yaw, 0, cos yaw): yaw pi/2 faces +X, as atan2(dx, dz) in the census.
    Check(Near(m[8], 1.f) && Near(m[9], 0.f) && Near(m[10], 0.f) && Near(m[0], 0.f) && Near(m[2], -1.f) && Near(m[5], 1.f),
          "yaw pi/2: z axis (1,0,0), x axis (0,0,-1)");
    Check(Near(m[3], 0.f) && Near(m[7], 0.f) && Near(m[11], 0.f) && Near(m[15], 1.f), "the w column is 0,0,0,1");
}

void TestUnderRider() {
    const float body[3] = {10.f, 5.f, 20.f};
    float h[3]{};
    hd::HorseUnderRider(body, 0.f, h);
    Check(Near(h[0], 10.f) && Near(h[1], 5.f - hd::kSeatUp) && Near(h[2], 20.f - hd::kSeatForward),
          "facing +Z: the root is kSeatForward behind and kSeatUp below the rider");
    hd::HorseUnderRider(body, 1.5707963f, h);
    Check(Near(h[0], 10.f - hd::kSeatForward) && Near(h[2], 20.f), "facing +X: behind along -X");
    Check(Near(hd::kSeatForward, 0.10f) && Near(hd::kSeatUp, 0.49f), "P2 (fa31b4f7): rider 0.10 m forward, 0.49 m up");
}

void TestParse() {
    hd::Request r{};
    Check(Parse("cfg", r) && r.verb == hd::Verb::cfg, "cfg");
    Check(Parse("read 30010 7", r) && r.verb == hd::Verb::read && r.mission == 30010 && r.epoch == 7, "read");
    Check(Parse("place 30020 9 -656010 1200 877730 -3141 1", r) && r.verb == hd::Verb::place && r.mission == 30020 && r.epoch == 9 &&
              r.x == -656010 && r.y == 1200 && r.z == 877730 && r.yaw == -3141 && r.anchor == 1,
          "place with anchor");
    Check(!Parse("read 30050 7", r) && r.verb == hd::Verb::none, "Mother Base has no horse: refused");
    Check(!Parse("read 30010 0", r) && !Parse("read 30010 65536", r), "epoch 1..65535");
    Check(!Parse("place 30010 1 0 0 0 31417 0", r) && !Parse("place 30010 1 0 0 0 0 2", r), "yaw and anchor bounds");
    Check(!Parse("place 30010 1 1000000001 0 0 0 0", r), "coordinate bound");
    Check(!Parse("cfg 1", r) && !Parse("read 30010 7 ", r) && !Parse("readx 30010 7", r) && !Parse("", r), "exact grammar");
}

void TestRefusal() {
    hd::PlaceScope s{};
    Check(std::strcmp(hd::PlaceRefusal(s), "off") == 0, "the key first");
    s.key = true;
    Check(std::strcmp(hd::PlaceRefusal(s), "pins") == 0, "then the contract");
    s.pins = s.field = s.census = s.realized = s.cs = true;
    Check(hd::PlaceRefusal(s) == nullptr, "all good: place");
    s.ridden = true;
    Check(std::strcmp(hd::PlaceRefusal(s), "ridden") == 0, "this copy's player rides it: the rider beats the owner");
    s.ridden = false;
    s.pending = true;
    Check(std::strcmp(hd::PlaceRefusal(s), "pending") == 0, "a call is pending: never fight the AI");
    s.pending = false;
    s.realized = false;
    Check(std::strcmp(hd::PlaceRefusal(s), "unrealized") == 0, "an unrealized horse is never placed");
}

void TestShared() {
    hd::Reset();
    Check(!hd::PlacedFresh(1000), "nothing placed yet");
    hd::NotePlaced(1000);
    Check(hd::PlacedFresh(1000) && hd::PlacedFresh(1000 + hd::kFreshMs) && !hd::PlacedFresh(1001 + hd::kFreshMs),
          "fresh for kFreshMs");
    Check(!hd::PlacedFresh(999), "a clock that went back is not fresh");
    float p[3]{};
    Check(!hd::RiderBody(1000, p), "no rider body yet");
    const float b[3] = {1.f, 2.f, 3.f};
    hd::NoteRiderBody(b, 2000);
    Check(hd::RiderBody(2000 + hd::kAnchorMs, p) && Near(p[1], 2.f) && !hd::RiderBody(2001 + hd::kAnchorMs, p), "anchor for kAnchorMs");
    const float bad[3] = {NAN, 0.f, 0.f};
    hd::NoteRiderBody(bad, 2100);
    Check(hd::RiderBody(2100, p) && Near(p[0], 1.f), "a non-finite sample is ignored");
    Check(hd::Generation(0x1000, 0x2000, 1) != hd::Generation(0x1000, 0x2000, 3) &&
              hd::Generation(0x1000, 0x2000, 1) == hd::Generation(0x1000, 0x2000, 1) && hd::Generation(0, 0, 0) != 0,
          "the census generation follows records, receiver and the realized mask");
    hd::Reset();
    Check(!hd::PlacedFresh(1000) && !hd::RiderBody(2000, p), "reset");
}

rw::In Sample(std::uint16_t act, std::int64_t sender_ms) {
    rw::In in{};
    in.fresh = in.time_valid = in.anim_valid = in.body_anim_valid = in.lease_live = true;
    in.remote_act = act;
    in.sender_ms = sender_ms;
    in.now_ms = static_cast<std::uint64_t>(sender_ms);
    in.body_act = 1;
    in.mount_hide = true;
    return in;
}

// Run 3d1439e6 (lab, attempt 2): the rider showed for one tick, then the remote's act sample went invalid (act 0, body 1
// unrealized by the mount hide) and nothing showed it again for the whole ride: the frames show the horse placed
// under the ride with an empty saddle. The ride wire's ON_HORSE (kind horse, a live reliable state) now shows the
// rider on a fresh placed horse without any act sample, and a missing sample keeps a shown ride.
void TestRideKindShows() {
    Check(rw::HorseRider(true, false, 0, false, true), "ride kind horse on a fresh placed horse: shown without an act sample");
    Check(!rw::HorseRider(false, false, 0, false, true), "ride kind horse but no fresh placement: not shown (today's hide)");
    Check(rw::HorseRider(true, false, 0, true, false), "a missing act sample keeps a shown ride");
    Check(!rw::HorseRider(false, false, 0, true, false), "a stale placement ends it");
    Check(!rw::HorseRider(true, true, 1, true, false), "a valid foot act and the ride wire on foot: off the horse");
    rw::Visibility v{};
    rw::In in = Sample(1, 1000);
    in.anim_valid = false;
    in.remote_act = 0;
    in.horse_shown = true;
    in.ride_have = in.ride_live = true;
    in.kind = rw::Kind::horse;
    rw::Observe(v, in);
    Check(!v.hidden && v.horse, "Observe: ON_HORSE with no act sample on the placed horse is shown");
    // The anchor never drags the horse far from the stream (run 3d1439e6: gap 1.1-1.6 m toward a stale, unrealized body).
    const float stream[3] = {10.f, 2.f, 30.f};
    const float near[3] = {10.5f, 2.2f, 30.5f};
    const float far[3] = {12.f, 2.f, 30.f};
    Check(hd::AnchorUsable(stream, near) && !hd::AnchorUsable(stream, far), "an anchor within 1.5 m of the stream only");
}

void TestVisibility() {
    Check(rw::HorseRider(true, true, 121, false, false) && !rw::HorseRider(false, true, 121, false, false) && !rw::HorseRider(true, false, 121, false, false),
          "act 121 with a fresh placed horse and a valid act");
    Check(!rw::HorseRider(true, true, 122, false, false) && rw::HorseRider(true, true, 122, true, false), "122 (get on / off, turns) only inside a shown ride");
    Check(rw::HorseRider(false, true, 122, true, false) && !rw::HorseRider(false, true, 121, true, false),
          "the get-off (122) of a shown ride stays shown after the final pose; a stale 121 does not");
    Check(!rw::HorseRider(true, true, 123, true, false) && !rw::HorseRider(true, true, 1, true, false), "a D-Walker or foot act is no horse rider");
    // Key off or no placed horse: today's mount hide.
    {
        rw::Visibility v{};
        rw::In in = Sample(121, 1000);
        rw::Observe(v, in);
        Check(v.hidden && v.mount && !v.horse, "no placed horse: the mount hide as before");
    }
    // The shared horse placed: shown at once, seated by the overlay; the ride kind horse does not hide either.
    {
        rw::Visibility v{};
        rw::In in = Sample(122, 1000);
        in.horse_shown = true;
        rw::Observe(v, in);
        Check(v.hidden && !v.horse, "get on (122) before the ride: still the short hide");
        in = Sample(121, 1100);
        in.horse_shown = true;
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::horse;
        rw::Observe(v, in);
        Check(!v.hidden && v.horse && !v.mount, "121 on the placed shared horse: shown");
        in = Sample(122, 1200);
        in.horse_shown = true;
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::horse;
        rw::Observe(v, in);
        Check(!v.hidden && v.horse, "a turn (122) inside the shown ride stays shown");
        in = Sample(121, 1300);
        in.ride_have = in.ride_live = true;
        in.kind = rw::Kind::horse;
        rw::Observe(v, in);
        Check(v.hidden && !v.horse && v.mount, "the placement went stale: hidden again (any doubt keeps the hide)");
    }
}

void TestLabKey() {
    const auto lab = [](const char* text) {
        mgscoop::LabConfig c{};
        (void)mgscoop::ParseLabConfig(text, std::strlen(text), c);
        return c;
    };
    const auto def = lab("enabled=1\nrole=host\n");
    const auto on = lab("enabled=1\nrole=guest\nworld_horse_share=1\n");
    const auto off = lab("enabled=1\nrole=guest\nworld_horse_share=0\n");
    const auto bad = lab("enabled=1\nrole=host\nworld_horse_share=2\n");
    Check(!def.world_horse_share && !def.world_horse_share_given, "world_horse_share: off unless named");
    Check(on.world_horse_share && on.world_horse_share_given && on.unknown_keys == 0 && on.invalid_values == 0, "a named 1 turns it on");
    Check(!off.world_horse_share && off.world_horse_share_given, "a named 0 is the off switch");
    Check(!bad.world_horse_share && bad.invalid_values == 1, "an invalid value stays off");
    // M1 gate passed (hidden frames on both copies, lab and play -WanSim bad): on with the native gun profile.
    const auto gun = lab("enabled=1\nrole=host\npartner_native_profile=gun\n");
    const auto gunOff = lab("enabled=1\nrole=host\npartner_native_profile=gun\nworld_horse_share=0\n");
    const auto play = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                          "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n");
    const auto playOff = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                             "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\nworld=3\n");
    Check(!playOff.world_horse_share, "without the gun profile: off");
    Check(gun.world_horse_share && !gun.world_horse_share_given, "the gun profile turns world_horse_share on in lab");
    Check(play.mode == mgscoop::LabMode::play && play.world_horse_share && play.play_refused == 0, "on in play with the gun profile");
    Check(!gunOff.world_horse_share && gunOff.world_horse_share_given, "a named 0 is the off switch");
}
// Horse plan M2 (a downed partner on the shared horse): world_horse_cargo, off unless named in lab and play until its gate.
void TestCargoKey() {
    const auto lab = [](const char* text) {
        mgscoop::LabConfig c{};
        (void)mgscoop::ParseLabConfig(text, std::strlen(text), c);
        return c;
    };
    const auto def = lab("enabled=1\nrole=host\n");
    const auto on = lab("enabled=1\nrole=guest\nworld_horse_cargo=1\n");
    const auto off = lab("enabled=1\nrole=guest\nworld_horse_cargo=0\n");
    const auto bad = lab("enabled=1\nrole=host\nworld_horse_cargo=2\n");
    Check(!def.world_horse_cargo && !def.world_horse_cargo_given, "world_horse_cargo: off unless named");
    Check(on.world_horse_cargo && on.world_horse_cargo_given && on.unknown_keys == 0 && on.invalid_values == 0, "a named 1 turns it on");
    Check(!off.world_horse_cargo && off.world_horse_cargo_given, "a named 0 is the off switch");
    Check(!bad.world_horse_cargo && bad.invalid_values == 1, "an invalid value stays off");
    const auto gun = lab("enabled=1\nrole=host\npartner_native_profile=gun\n");
    Check(!gun.world_horse_cargo && gun.world_horse_share, "before its gate: off with the gun profile (the M1 key stays on)");
    const auto play = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                          "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n"
                          "world_horse_cargo=1\n");
    Check(play.mode == mgscoop::LabMode::play && play.world_horse_cargo && play.play_refused == 0, "a named 1 runs in play too");
}

} // namespace

int main() {
    TestMatrix();
    TestUnderRider();
    TestParse();
    TestRefusal();
    TestShared();
    TestVisibility();
    TestRideKindShows();
    TestLabKey();
    TestCargoKey();
    std::printf("%u horse drive checks passed: call-enter matrix, saddle anchor, receipt grammar, refusals, shared state, "
                "shared-horse rider visibility, lab key\n",
                checks);
    return 0;
}
