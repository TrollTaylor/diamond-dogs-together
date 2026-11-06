// MGS_COOP_HORSE_DRIVE_V1 (horse plan M1, plan B): the pure part (native/include/mgscoop/horse_drive.hpp), the shared
// D-Horse rider's visibility (partner_ride_wire HorseRider / Observe with In::horse_shown) and the lab key.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "mgscoop/horse_body_watch.hpp"
#include "mgscoop/horse_drive.hpp"
#include "mgscoop/horse_ride.hpp"
#include "../native/horse_drive_contract.hpp"
#include "../native/horse_ride_contract.hpp"
#include "mgscoop/lab_config.hpp"
#include "mgscoop/partner_ride_wire.hpp"
#include "mgscoop/partner_seat_shot.hpp"

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
    // M2 round 2 (runs a49c4d76 / 20aef564 / d2a5cafa: body 0 under the moving horse on the victim's screen with a
    // per-tick Lua Warp): hold0 moves this copy's own downed body 0 as the partner driver's xform mover moves body 1
    // (T.vt+0x70 then T.vt+0x68, no C0 / A0 reset), mm / mrad.
    // Run a242d867: with no reset body 0 stayed at its target but played the airborne pose; the last number picks
    // Warp's resets after the move (bit 0 A0.vt+0x80, bit 1 C0.vt+0x90, C0 first as Warp).
    Check(Parse("hold0 30010 7 -656010 1200 877730 -3141 1", r) && r.verb == hd::Verb::hold0 && r.mission == 30010 && r.epoch == 7 &&
              r.x == -656010 && r.y == 1200 && r.z == 877730 && r.yaw == -3141 && r.reset == 1,
          "hold0 with the A0 reset");
    Check(Parse("hold0 30010 7 0 0 0 0 3", r) && r.reset == 3 && Parse("hold0 30010 7 0 0 0 0 0", r) && r.reset == 0, "hold0 resets 0..3");
    // BODY0 round: bit 2 (4) the write watch on body 0's transform, bit 3 (8) the pin (horse_body_watch).
    Check(Parse("hold0 30010 7 0 0 0 0 14", r) && r.reset == 14 && Parse("hold0 30010 7 0 0 0 0 4", r) && r.reset == 4,
          "hold0 resets 0..15 (C0 + watch + pin = 14)");
    // hold1: the carrier's copy holds body 1 (the downed partner) the same way (cfd2b604: body 1 low on the carrier's screen).
    Check(Parse("hold1 30010 7 1 2 3 4 14", r) && r.verb == hd::Verb::hold1 && r.x == 1 && r.reset == 14 && !Parse("hold1 30010 7 1 2 3 4 16", r),
          "hold1: hold0's numbers for body 1");
    // free0: the take-down / lay-down ends the pin before its Warp (else the pin would put the body back on the horse).
    Check(Parse("free0 30010 7", r) && r.verb == hd::Verb::free0 && r.mission == 30010 && r.epoch == 7 && !Parse("free0 30010", r) &&
              !Parse("free0 30010 7 1", r),
          "free0 <mission> <epoch>");
    Check(!Parse("hold0 30010 7 0 0 0 0 16", r) && !Parse("hold0 30010 7 0 0 0 0", r) && !Parse("hold0 30050 7 0 0 0 0 1", r) &&
              !Parse("hold0 30010 7 0 0 0 31417 1", r),
          "hold0: seven numbers, free roam, yaw and reset bounds");
}

// M2g (run f7e09c55: at the partner's mount the victim's copy unrealized body 1 for a frame, the census refused, hold0
// refused "body", the fall check ran on body 0 and it squatted on the horse): hold0 of body 0 alone may use body 1's class
// from the last full census, only while body 0's own T class equals it.
void TestHoldAlone() {
    hd::BodyClass b0{}, cached{};
    b0.t_vt = 0x10; b0.t68 = 0x20; b0.t70 = 0x30; b0.t58 = 0x40; b0.c0_vt = 0x50; b0.c090 = 0x60; b0.a0_vt = 0x70; b0.a080 = 0x80;
    Check(!hd::CachedBody1Usable(b0, cached), "hold alone: no full census yet, no cached class");
    cached = b0;
    Check(hd::CachedBody1Usable(b0, cached), "hold alone: body 0's T class equals the cached body 1 class");
    cached.t68 = 0x21;
    Check(!hd::CachedBody1Usable(b0, cached), "hold alone: another T set-pose slot refuses");
    cached = b0; cached.t_vt = 0x11;
    Check(!hd::CachedBody1Usable(b0, cached), "hold alone: another T vtable refuses");
    cached = b0; b0.t58 = 0;
    Check(!hd::CachedBody1Usable(b0, cached), "hold alone: an unread slot refuses");
}

// M2i (frames 2a41c416 / c62e2ce6 h2-loaded: body 1 along the horse with its upper body raised at the load, across the
// back only after the ride, act 5 and the readback yaw unchanged): with world_horse_cargo a hold1 run (no gap over 1 s)
// adds Warp's A0 reset once, on the first hold1 1 s or more into the run (the stock put-down settled), only while body
// 0's A0 class and slot equal body 1's.
void TestHoldRunA0() {
    hd::HoldRun r{};
    Check(!hd::HoldRunA0(r, 5000), "hold run A0: the run's first hold1 (the put-down still settling) adds no A0");
    Check(!hd::HoldRunA0(r, 5033), "hold run A0: the next tick neither");
    Check(hd::HoldRunA0(r, 5000 + hd::kA0DelayMs), "hold run A0: 1 s into the run, A0 once");
    Check(!hd::HoldRunA0(r, 5033 + hd::kA0DelayMs), "hold run A0: once per run");
    Check(!hd::HoldRunA0(r, 5033 + hd::kA0DelayMs + hd::kHoldRunGapMs), "hold run A0: a gap of exactly 1 s keeps the run");
    const std::uint64_t next = 5034 + hd::kA0DelayMs + 2 * hd::kHoldRunGapMs;
    Check(!hd::HoldRunA0(r, next), "hold run A0: a longer gap opens a new run (the next load), no A0 at its start");
    Check(hd::HoldRunA0(r, next + hd::kA0DelayMs), "hold run A0: the new run's A0, 1 s in");
    Check(!hd::HoldRunA0(r, 100), "hold run A0: a clock behind the last hold opens a run");
    hd::BodyClass b0{}, b1{};
    b0.a0_vt = b1.a0_vt = 0x70; b0.a080 = b1.a080 = 0x80;
    Check(hd::A0Usable(b0, b1), "A0 once: body 0's A0 class and slot equal body 1's");
    b1.a080 = 0x81;
    Check(!hd::A0Usable(b0, b1), "A0 once: another A0 slot skips the reset");
    b1 = b0; b1.a0_vt = 0;
    Check(!hd::A0Usable(b0, b1), "A0 once: an unread A0 skips the reset");
}

void TestHoldRefusal() {
    hd::HoldScope s{};
    Check(std::strcmp(hd::HoldRefusal(s), "off") == 0, "hold0: the keys first");
    s.share = true;
    Check(std::strcmp(hd::HoldRefusal(s), "off") == 0, "hold0: world_horse_share alone is not enough (world_horse_cargo)");
    s.cargo = true;
    Check(std::strcmp(hd::HoldRefusal(s), "pins") == 0, "hold0: then the contracts");
    s.pins = true;
    Check(std::strcmp(hd::HoldRefusal(s), "scope") == 0, "hold0: then the scope");
    s.field = true;
    Check(std::strcmp(hd::HoldRefusal(s), "body") == 0, "hold0: then body 0's transform chain");
    s.body = true;
    Check(hd::HoldRefusal(s) == nullptr, "hold0: all good");
    s.mounted = true;
    Check(std::strcmp(hd::HoldRefusal(s), "mounted") == 0, "hold0: never while this copy's player rides");
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

// M2j (lab 2a41c416 guest, the M2i round: one victim_pin window with body 0 2.03 m under its target just after the
// carrier got off; body 1 on the victim's copy played the get-off act 122 until about 1 s after the final, and 122 holds
// a shown ride, so each anchored place put the horse under the body getting off, up to kAnchorMaxM from the stream):
// with world_horse_cargo the rider body anchors nothing during act 122 (the stream pose places the horse); every other
// act, an invalid sample and the defaults anchor as before.
void TestAnchorHold() {
    Check(hd::kGetOffAct == 122, "the get-off act is 122");
    Check(!hd::AnchorNote(true, true, 122), "cargo key, act 122: no anchor");
    Check(hd::AnchorNote(false, true, 122), "defaults, act 122: anchors as before");
    Check(hd::AnchorNote(true, true, 121) && hd::AnchorNote(true, false, 122) && hd::AnchorNote(true, true, 1),
          "cargo key, the seated act, an invalid sample or a foot act: anchors as before");
}

// fixland4 (review of S3: the motion-record log partner_pillion=mg ran at the defaults with no run naming no horse key):
// the log comes only with world_horse_down_off (Settings.horse_down_off) and a seat-error window, as the S3 lab runs did.
void TestMotionLogKey() {
    Check(!hd::MotionLog(false, 3), "defaults, a seat-error window: no motion-record log");
    Check(hd::MotionLog(true, 3), "world_horse_down_off, a seat-error window: logged");
    Check(!hd::MotionLog(true, 0) && !hd::MotionLog(false, 0), "no seat-error window: nothing");
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
    // M2f round: its three proof attempts (5a92c73f / d4aec0a2, 131f88c6 / 05758736, 770368d0 and its play run) never got
    // the lab session's (a) and (b) through (a missed pickup, worker capture and landing refusals): off unless named again.
    Check(!gun.world_horse_cargo && !gun.world_horse_cargo_given && gun.world_horse_share,
          "after the M2f attempts: off with the gun profile (the M1 key stays on)");
    const auto gunOff = lab("enabled=1\nrole=host\npartner_native_profile=gun\nworld_horse_cargo=0\n");
    Check(!gunOff.world_horse_cargo && gunOff.world_horse_cargo_given, "with the gun profile a named 0 is the off switch");
    const auto playGun = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                             "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n");
    Check(playGun.mode == mgscoop::LabMode::play && !playGun.world_horse_cargo && playGun.play_refused == 0, "off in play unless named");
    const auto play = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                          "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n"
                          "world_horse_cargo=1\n");
    Check(play.mode == mgscoop::LabMode::play && play.world_horse_cargo && play.play_refused == 0, "a named 1 runs in play too");
}

// Horse plan M3 (ride behind your partner, lab key world_horse_pillion): the raw E read, the seat1 lease and the rear seat.
void TestPillion() {
    hd::Request r{};
    Check(Parse("pad 30010 7", r) && r.verb == hd::Verb::pad && r.mission == 30010 && r.epoch == 7, "pad");
    Check(!Parse("pad 30050 7", r) && !Parse("pad 30010 0", r) && !Parse("pad 30010 7 1", r), "pad: free roam, epoch, exact grammar");
    Check(Parse("seat1 30020 9 450 0 1", r) && r.verb == hd::Verb::seat1 && r.mission == 30020 && r.epoch == 9 && r.back == 450 &&
              r.up == 0 && r.seat_pose == 1,
          "seat1 with the default rear seat, the overlay seat on");
    Check(Parse("seat1 30020 9 450 0 0", r) && r.seat_pose == 0 && !Parse("seat1 30020 9 450 0 2", r), "seat1: the pose flag 0 or 1");
    Check(Parse("seat1 30010 1 0 -500 1", r) && r.back == 0 && r.up == -500 && Parse("seat1 30010 1 1500 1500 1", r) && r.back == 1500 &&
              r.up == 1500,
          "seat1 bounds: back 0..1500, up -500..1500");
    Check(!Parse("seat1 30010 1 1501 0 1", r) && !Parse("seat1 30010 1 -1 0 1", r) && !Parse("seat1 30010 1 450 -501 1", r) &&
              !Parse("seat1 30010 1 450 0", r) && !Parse("seat1 30050 1 450 0 1", r),
          "seat1: out of bounds, short, Mother Base");
    Check(hd::kRearBackMm == 450 && hd::kRearUpMm == 0 && hd::kSeatMs == 300, "rear seat 0.45 m behind the rider, a 300 ms lease");
    // The PlayerPad held getter's word choice without the mask (probe P5: the ACTION mask hides E from the downed poll).
    Check(hd::RawAction(0x20, 0) && hd::RawAction(0, 0x20) && hd::RawAction(0x21, 0x0), "E on the device or the keyboard word");
    Check(!hd::RawAction(0x1, 0x20) && !hd::RawAction(0, 0) && !hd::RawAction(0x10, 0), "the device word wins when non-zero, as the getter");
    hd::SeatScope s{};
    Check(std::strcmp(hd::SeatRefusal(s), "off") == 0, "seat1: the key first");
    s.pillion = true;
    Check(std::strcmp(hd::SeatRefusal(s), "pins") == 0, "seat1: then the contracts");
    s.pins = true;
    Check(std::strcmp(hd::SeatRefusal(s), "scope") == 0, "seat1: then the scope");
    s.field = true;
    Check(std::strcmp(hd::SeatRefusal(s), "not_riding") == 0, "seat1: only while this copy's player rides the shared horse");
    s.riding = true;
    Check(hd::SeatRefusal(s) == nullptr, "seat1: lease");
    hd::Reset();
    float back = -1.f, up = -1.f;
    bool pose = false;
    Check(!hd::SeatLease(1000, back, up, pose), "no lease yet");
    hd::NoteSeat(450, 100, true, 1000);
    Check(hd::SeatLease(1000, back, up, pose) && Near(back, 0.45f) && Near(up, 0.1f) && pose, "a lease carries the offsets in metres");
    Check(hd::SeatLease(1000 + hd::kSeatMs, back, up, pose) && !hd::SeatLease(1001 + hd::kSeatMs, back, up, pose), "live for kSeatMs");
    Check(!hd::SeatLease(999, back, up, pose), "a clock that went back has no lease");
    // Runs bdd49a57 / a295d660: the host's horse stood still under the ride keys while the overlay seated body 1 behind
    // it; a lease without the overlay seat (a lab test) keeps body 1 at the rear seat in its own pose.
    hd::NoteSeat(450, 300, false, 1500);
    Check(hd::SeatLease(1500, back, up, pose) && !pose && Near(up, 0.3f), "a lease without the overlay seat");
    hd::NoteSeat(450, 0, true, 2000);
    hd::Reset();
    Check(!hd::SeatLease(2000, back, up, pose), "reset ends the lease");
    // M4 (run d935beae): a capture stalled the driver's game past kSeatMs, the lease lapsed for one tick and the overlay
    // seat ended (body 1 then stood 1.9 m under the rear seat for the rest of the ride). A lease live on the partner
    // driver's previous tick is kept kSeatGraceTicks more ticks with its last offsets; a lease gone that long ends.
    // The review of M4: the grace is part of world_horse_shot (off unless named); without it a lapsed tick ends the seat.
    hd::SeatHold off{};
    float ob = 0.45f, ou = 0.3f;
    bool op = true;
    Check(!off.grace && hd::HoldSeat(off, true, ob, ou, op), "grace off: a live lease seats");
    ob = ou = 0.f;
    op = false;
    Check(!hd::HoldSeat(off, false, ob, ou, op) && !off.live, "grace off (the defaults): one lapsed tick ends the seat");
    hd::SeatHold sh{};
    sh.grace = true;
    float hb = 0.f, hu = 0.f;
    bool hp = false;
    Check(!hd::HoldSeat(sh, false, hb, hu, hp), "no lease yet: no seat");
    hb = 0.45f; hu = 0.3f; hp = true;
    Check(hd::HoldSeat(sh, true, hb, hu, hp) && sh.live, "a live lease seats");
    hb = hu = 0.f; hp = false;
    Check(hd::kSeatGraceTicks == 2 && hd::HoldSeat(sh, false, hb, hu, hp) && Near(hb, 0.45f) && Near(hu, 0.3f) && hp,
          "one lapsed tick keeps the seat with the last offsets");
    hb = 0.45f; hu = 0.3f; hp = true;
    Check(hd::HoldSeat(sh, true, hb, hu, hp) && sh.miss == 0, "the lease back: the miss count restarts");
    hb = hu = 0.f;
    Check(hd::HoldSeat(sh, false, hb, hu, hp) && hd::HoldSeat(sh, false, hb, hu, hp) && !hd::HoldSeat(sh, false, hb, hu, hp) && !sh.live,
          "kSeatGraceTicks lapsed ticks are kept, the next one ends the seat");
    Check(!hd::HoldSeat(sh, false, hb, hu, hp), "ended: stays off until a live lease");
    const float rider[3] = {10.f, 5.f, 20.f};
    float seat[3]{};
    hd::RearSeat(rider, 0.f, 0.45f, 0.1f, seat);
    Check(Near(seat[0], 10.f) && Near(seat[1], 5.1f) && Near(seat[2], 19.55f), "facing +Z: 0.45 m behind along -Z, 0.1 m up");
    hd::RearSeat(rider, 1.5707963f, 0.45f, 0.f, seat);
    Check(Near(seat[0], 9.55f) && Near(seat[1], 5.f) && Near(seat[2], 20.f), "facing +X: behind along -X");
    // hold0 for the passenger's body 0: world_horse_pillion admits it without world_horse_cargo.
    hd::HoldScope h{};
    h.share = h.pillion = true;
    Check(std::strcmp(hd::HoldRefusal(h), "pins") == 0, "hold0: the pillion key passes the key step");
    h.share = false;
    Check(std::strcmp(hd::HoldRefusal(h), "off") == 0, "hold0: never without world_horse_share");
}
void TestPillionKey() {
    const auto lab = [](const char* text) {
        mgscoop::LabConfig c{};
        (void)mgscoop::ParseLabConfig(text, std::strlen(text), c);
        return c;
    };
    const auto def = lab("enabled=1\nrole=host\n");
    const auto on = lab("enabled=1\nrole=guest\nworld_horse_pillion=1\n");
    const auto off = lab("enabled=1\nrole=guest\nworld_horse_pillion=0\n");
    const auto bad = lab("enabled=1\nrole=host\nworld_horse_pillion=2\n");
    Check(!def.world_horse_pillion && !def.world_horse_pillion_given, "world_horse_pillion: off unless named");
    Check(on.world_horse_pillion && on.world_horse_pillion_given && on.unknown_keys == 0 && on.invalid_values == 0, "a named 1 turns it on");
    Check(!off.world_horse_pillion && off.world_horse_pillion_given, "a named 0 is the off switch");
    Check(!bad.world_horse_pillion && bad.invalid_values == 1, "an invalid value stays off");
    // M3c: on with the gun profile in lab and play (with its 2.5 / 2.8 m ride hold); a named 0 is the off switch.
    const auto gun = lab("enabled=1\nrole=host\npartner_native_profile=gun\n");
    Check(gun.world_horse_pillion && !gun.world_horse_pillion_given && gun.world_horse_share, "M3c: on with the gun profile");
    const auto gunOff = lab("enabled=1\nrole=host\npartner_native_profile=gun\nworld_horse_pillion=0\n");
    Check(!gunOff.world_horse_pillion && gunOff.world_horse_pillion_given, "with the gun profile a named 0 is the off switch");
    const auto playGun = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                             "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n");
    Check(playGun.mode == mgscoop::LabMode::play && playGun.world_horse_pillion && playGun.play_refused == 0, "on in play with the gun profile");
    const auto play = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                          "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n"
                          "world_horse_pillion=1\n");
    Check(play.mode == mgscoop::LabMode::play && play.world_horse_pillion && play.play_refused == 0, "a named 1 runs in play too");
}

// M3c: body 1's readback against the rear seat it was given on the driver's copy (partner_pillion=stats), and the pinned
// life table read of the passenger's copy's horse (receiver+0x30 -> +8, 0x28 per index: life u16, max u16; SetMaxLife).
void TestSeatErr() {
    hd::SeatErr e{};
    const float seat[3] = {10.f, 5.f, 20.f};
    const float b1[3] = {10.3f, 4.6f, 20.4f};
    hd::NoteSeatErr(e, seat, b1);
    Check(e.n == 1 && Near(e.xz_max, 0.5f) && Near(e.dy_max, -0.4f), "xz distance 0.5 m, 0.4 m under the seat");
    const float b2[3] = {10.1f, 5.7f, 20.f};
    hd::NoteSeatErr(e, seat, b2);
    Check(e.n == 2 && Near(e.xz_max, 0.5f) && Near(e.dy_max, 0.7f), "the worst of the window: the larger |dy| wins with its sign");
    const float bad[3] = {NAN, 5.f, 20.f};
    hd::NoteSeatErr(e, seat, bad);
    Check(e.n == 2, "a non-finite readback is not counted");
    Check(hd::LifeEntry(0x1000, 0) == 0x1000 && hd::LifeEntry(0x1000, 1) == 0x1028, "life entry stride 0x28");
    bool pinned = false;
    for (const auto& s : mgscoop::horse_drive_contract::sites) if (s.rva == hd::kLifeSiteRva && s.size == 0x30) pinned = true;
    Check(pinned && hd::kLifeSiteRva == 0xe4d340, "the life table's reader (SetMaxLife's loop 0x140E4D340) is a pinned site");
}

// Horse plan M4 (shoot from the back of the horse, lab key world_horse_shot): the passenger's copy leases its placed horse's
// box (pbox: the census root and yaw) to the seated-shot hook (partner_seat_shot Class::horse), which moves a bullet's start
// past the horse and the driver in front of the passenger (design 8.37's self-hit exclusion for the rear seat).
void TestShot() {
    hd::Request r{};
    Check(Parse("pbox 30010 7 1000 -2000 3000 1571", r) && r.verb == hd::Verb::pbox && r.mission == 30010 && r.epoch == 7 && r.x == 1000 &&
              r.y == -2000 && r.z == 3000 && r.yaw == 1571,
          "pbox: the placed census root (mm) and yaw (mrad)");
    Check(!Parse("pbox 30050 7 0 0 0 0", r) && !Parse("pbox 30010 7 0 0 0", r) && !Parse("pbox 30010 7 0 0 0 0 1", r) &&
              !Parse("pbox 30010 7 0 0 0 40000", r) && !Parse("pbox 30010 0 0 0 0 0", r),
          "pbox: free roam, epoch, exact grammar, yaw bounds");
    hd::Reset();
    float p[3]{}, yaw = 0.f;
    Check(!hd::ShotBox(1000, p, yaw), "no box yet");
    hd::NoteShotBox(1000, -2000, 3000, 1571, 1000);
    Check(hd::ShotBox(1000, p, yaw) && Near(p[0], 1.f) && Near(p[1], -2.f) && Near(p[2], 3.f) && Near(yaw, 1.571f), "a box in metres / radians");
    Check(hd::kBoxMs == 300 && hd::ShotBox(1000 + hd::kBoxMs, p, yaw) && !hd::ShotBox(1001 + hd::kBoxMs, p, yaw) && !hd::ShotBox(999, p, yaw),
          "live for kBoxMs (300 ms) after the last pbox; a clock that went back has none");
    hd::NoteShotBox(1000, -2000, 3000, 1571, 2000);
    hd::Reset();
    Check(!hd::ShotBox(2000, p, yaw), "reset ends the box");
    namespace pss = mgscoop::partner_seat_shot;
    Check(pss::SpecFor(pss::Class::horse) == &pss::kHorseBox && std::strcmp(pss::ClassName(pss::Class::horse), "horse") == 0 &&
              pss::ClassFor(1, 1) == pss::Class::light,
          "the horse class has its own box (the vehicle classes unchanged)");
    pss::Pub pub{};
    pub.cls = pss::Class::horse;
    pub.apply = true;
    pub.target_ok = true;
    pub.target[0] = 0.f; pub.target[1] = 1.2f; pub.target[2] = 20.f;
    // The passenger's muzzle 0.25 m behind and 1.3 m over the census root (body 0 is held 0.35 m behind, 0.8 m up), the
    // horse facing +Z: a shot ahead across the driver and the neck starts past the horse's head.
    const float start[3] = {0.f, 1.3f, -0.25f}, ahead[3] = {0.f, 0.f, 1.f};
    pss::Plan pl = pss::PlanShot(pub, start, ahead);
    Check(pl.move && std::strcmp(pl.result, "shift") == 0 && pl.inside && pl.pos[2] > 1.6f && pl.pos[2] < 2.0f,
          "ahead: the start moves past the box's front (the driver, the neck and the head)");
    const float right[3] = {-1.f, 0.f, 0.f};  // the horse's right at yaw 0 (partner_seat_shot Axes)
    pl = pss::PlanShot(pub, start, right);
    Check(pl.move && pl.t > 0.5f && pl.t < 0.75f, "to the side: past the horse's flank within 0.75 m");
    const float behind[3] = {0.f, 1.3f, -2.0f};  // behind the tail, the line crossing the horse
    pl = pss::PlanShot(pub, behind, ahead);
    Check(!pl.move && std::strcmp(pl.result, "outside") == 0, "a start outside the box is never moved");
    const float driver_head[3] = {0.f, 0.49f + 1.0f, 0.1f};
    pl = pss::PlanShot(pub, driver_head, ahead);
    Check(pl.inside, "the driver's head (0.1 m ahead, ~1 m over the rider root) is inside the box");
}
void TestShotKey() {
    const auto lab = [](const char* text) {
        mgscoop::LabConfig c{};
        (void)mgscoop::ParseLabConfig(text, std::strlen(text), c);
        return c;
    };
    const auto def = lab("enabled=1\nrole=host\n");
    const auto on = lab("enabled=1\nrole=guest\nworld_horse_shot=1\n");
    const auto off = lab("enabled=1\nrole=guest\nworld_horse_shot=0\n");
    const auto bad = lab("enabled=1\nrole=host\nworld_horse_shot=2\n");
    Check(!def.world_horse_shot && !def.world_horse_shot_given, "world_horse_shot: off unless named without the gun profile");
    Check(on.world_horse_shot && on.world_horse_shot_given && on.unknown_keys == 0 && on.invalid_values == 0, "a named 1 turns it on");
    Check(!off.world_horse_shot && off.world_horse_shot_given, "a named 0 is the off switch");
    Check(!bad.world_horse_shot && bad.invalid_values == 1, "an invalid value stays off");
    // M4 gate not passed (runs d935beae / 28015a91 / 48d31500 / 1a7c5d0e: no shot from the rear seat put a guard down, and the
    // driver's screen shows no raised gun, the M7 seat gun's known limit): off unless named with the gun profile too; a
    // named 1 runs in play as well.
    const auto gun = lab("enabled=1\nrole=host\npartner_native_profile=gun\n");
    Check(!gun.world_horse_shot && !gun.world_horse_shot_given && gun.world_horse_pillion, "M4: off unless named with the gun profile");
    const auto gunOn = lab("enabled=1\nrole=host\npartner_native_profile=gun\nworld_horse_shot=1\n");
    Check(gunOn.world_horse_shot && gunOn.world_horse_shot_given, "with the gun profile a named 1 turns it on");
    const auto playGun = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                             "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n"
                             "world_horse_shot=1\n");
    Check(playGun.mode == mgscoop::LabMode::play && playGun.world_horse_shot && playGun.play_refused == 0, "a named 1 runs in play too");
}
// M4b (world_horse_shot): the driver sees the passenger's raised gun. While the remote aims, the DM overlay leaves body 1 to
// its native squat-aim at the rear seat (kAimLingerMs past the last aiming tick, so the gaps between shots keep it); the
// partner driver notes that seat and the seat1 receipt hands it to Lua, whose hold1 pins body 1 there (admitted by the
// pillion and shot keys together).
void TestAimSeat() {
    hd::SeatAim a{};
    Check(!hd::AimSeat(a, false, true, 1000), "aim: no shot key or no live seat: never");
    Check(!hd::AimSeat(a, true, false, 1000), "aim: the remote not aiming: no");
    Check(hd::AimSeat(a, true, true, 1000), "aim: the remote aims: the overlay leaves body 1 to the native aim");
    Check(hd::kAimLingerMs == 1500 && hd::AimSeat(a, true, false, 2500) && !hd::AimSeat(a, true, false, 2501),
          "aim: kAimLingerMs (1.5 s) past the last aiming tick");
    Check(hd::AimSeat(a, true, true, 3000) && !hd::AimSeat(a, false, true, 3001) && !hd::AimSeat(a, true, false, 3002),
          "aim: the seat's end (or the key's) clears it");
    Check(!hd::AimSeat(a, true, false, 1), "aim: a clock that went back is no aim");
    hd::Reset();
    std::int32_t mm[3]{}, yaw = 0;
    Check(!hd::SeatAimTarget(1000, mm, yaw), "aim seat: none noted");
    const float seat[3] = {1.f, 2.5f, -3.25f};
    hd::NoteSeatAim(true, seat, 1.5708f, 1000);
    Check(hd::SeatAimTarget(1000 + hd::kSeatMs, mm, yaw) && mm[0] == 1000 && mm[1] == 2500 && mm[2] == -3250 && yaw == 1571,
          "aim seat: the noted seat in mm / mrad for kSeatMs");
    Check(!hd::SeatAimTarget(1001 + hd::kSeatMs, mm, yaw) && !hd::SeatAimTarget(999, mm, yaw), "aim seat: stale or a clock gone back: none");
    hd::NoteSeatAim(false, seat, 1.5708f, 2000);
    Check(!hd::SeatAimTarget(2000, mm, yaw), "aim seat: not aiming: none");
    const float far[3] = {2e6f, 0.f, 0.f};
    hd::NoteSeatAim(true, far, 0.f, 3000);
    Check(!hd::SeatAimTarget(3000, mm, yaw), "aim seat: out of the receipt's bounds: none");
    hd::NoteSeatAim(true, seat, 1.5708f, 4000);
    hd::Reset();
    Check(!hd::SeatAimTarget(4000, mm, yaw), "aim seat: reset ends it");
    char reply[160]{};
    hd::NoteSeatAim(true, seat, -1.5708f, 5000);
    Check(hd::SeatReply(5000, reply, sizeof(reply)) &&
              std::strcmp(reply, "return {ready=true,seat=1,aim=1,sx=1000,sy=2500,sz=-3250,syaw=-1571}") == 0,
          "seat1's reply carries the aim seat");
    Check(hd::SeatReply(5000 + hd::kSeatMs + 1, reply, sizeof(reply)) && std::strcmp(reply, "return {ready=true,seat=1,aim=0}") == 0,
          "seat1's reply without an aim");
    Check(!hd::SeatReply(5000, reply, 8), "seat1's reply: a short buffer fails");
    // hold1 (body 1) on the driver's copy: the pillion key admits it only with the shot key (world_horse_cargo as before).
    hd::HoldScope h{};
    h.share = h.pillion = h.body1 = true;
    Check(std::strcmp(hd::HoldRefusal(h), "off") == 0, "hold1: the pillion key alone does not admit body 1");
    h.shot = true;
    Check(std::strcmp(hd::HoldRefusal(h), "pins") == 0, "hold1: pillion + shot pass the key step");
    h.pillion = false;
    Check(std::strcmp(hd::HoldRefusal(h), "off") == 0, "hold1: the shot key alone does not");
    h.cargo = true;
    Check(std::strcmp(hd::HoldRefusal(h), "pins") == 0, "hold1: world_horse_cargo as before");
    hd::HoldScope h0{};
    h0.share = h0.pillion = true;
    Check(std::strcmp(hd::HoldRefusal(h0), "pins") == 0, "hold0: the pillion key as before");
}

// BODY0 round: the pure part of the hardware write watch on body 0's transform (native/include/mgscoop/horse_body_watch.hpp).
void TestBodyWatch() {
    namespace w = mgscoop::horse_body_watch;
    // DR7: L0 (bit 0) and L1 (bit 2), RW0 = RW1 = 01 (write), LEN0 = LEN1 = 11 (4 bytes); other bits kept.
    const std::uint64_t on = w::Dr7(0x400, true);
    Check(on == (0x400ull | 1ull | 4ull | (1ull << 16) | (3ull << 18) | (1ull << 20) | (3ull << 22)), "Dr7: two 4-byte write watches");
    Check(w::Dr7(on, false) == 0x400ull, "Dr7 off: only our bits cleared");
    Check(w::Dr7(0x10 | (3ull << 24), true) == (0x10ull | (3ull << 24) | 5ull | (1ull << 16) | (3ull << 18) | (1ull << 20) | (3ull << 22)),
          "Dr7: DR2's bits untouched");
    Check(w::Ours(1) && w::Ours(2) && w::Ours(3) && !w::Ours(4) && !w::Ours(0x4000), "Ours: B0 / B1 only");
    // The pin holds only within kPinMs of the last hold0 (a later Warp of body 0 is not undone).
    Check(w::PinLive(1000, 1000 + w::kPinMs) && !w::PinLive(1000, 1001 + w::kPinMs) && !w::PinLive(0, 10), "PinLive");
    // Run 911ce293: world_horse_cargo pins nothing for TRACK_PIN_MS (200 ms) after M1's last placement; a 100 ms pin let body 0
    // drop to the ground under the horse right after the dismount (drop_cm 246) and the victim's copy's horse fell (Unconscious).
    Check(w::kPinMs >= 300, "the pin outlasts world_horse_cargo's 200 ms track-pin gap");
    w::Table t{};
    const std::uint32_t ret[w::kRets] = {0x9c1b90, 0x123, 0, 0};
    w::Note(t, 0x1ad566c, 7, 0, ret);
    w::Note(t, 0x1ad566c, 7, 0, ret);
    w::Note(t, 0x1ad1f82, 9, 3, ret);
    w::Note(t, 0x1ad1f82, 9, 1, ret);
    Check(t.used == 2 && t.traps == 4 && t.rip[0].rva == 0x1ad566c && t.rip[0].n == 2 && t.rip[1].n == 2 && t.rip[1].ord_min == 1 &&
              t.rip[1].ord_max == 3 && t.rip[1].tid == 9 && t.rip[1].ret[0] == 0x9c1b90,
          "Note: one row per writer, its count, thread, order range and return rvas");
    for (std::uint32_t i = 0; i < 20; ++i) w::Note(t, 0x100 + i, 1, 0, ret);
    Check(t.used == w::kRips && t.dropped == 20 - (w::kRips - 2), "Note: a full table counts dropped");
    const std::uint64_t stack[6] = {0x5, 0x140001234, 0x7ff000000000, 0x1409c1b95, 0x142100000, 0x140abc000};
    std::uint32_t r[w::kRets]{};
    Check(w::StackRets(stack, 6, 0x140000000, 0x1000, 0x2000000, r) == 3 && r[0] == 0x1234 && r[1] == 0x9c1b95 && r[2] == 0xabc000 &&
              r[3] == 0,
          "StackRets: image text values in order (outside the text range skipped)");
    // The pin target: T+0x20 = the centre, T+0x10 = the centre minus (0, h, 0), T+0x174 the foot y (T.vt+0x58 0x141AD5660).
    float c[4]{}, f[4]{}, fy = 0.f;
    w::Target(1.f, 5.f, 3.f, 0.9f, c, f, fy);
    Check(Near(c[0], 1.f) && Near(c[1], 5.f) && Near(c[2], 3.f) && Near(c[3], 0.f) && Near(f[0], 1.f) && Near(f[1], 4.1f) && Near(f[2], 3.f) &&
              Near(fy, 4.1f),
          "Target: centre, foot and foot y as T.vt+0x58 writes them");
}

// Step S3: the RideHorse ride record (rrec, logs) and the forced get-off of this copy's downed rider (getoff, lab key
// world_horse_down_off: off unless named until its gate).
void TestRideRecord() {
    namespace hr = mgscoop::horse_ride;
    namespace rc = mgscoop::horse_ride_contract;
    hd::Request q{};
    Check(hd::Parse("rrec 30010 7", 12, q) && q.verb == hd::Verb::rrec && q.mission == 30010 && q.epoch == 7, "S3: rrec <mission> <epoch>");
    Check(hd::Parse("getoff 30020 9", 14, q) && q.verb == hd::Verb::getoff && q.mission == 30020 && q.epoch == 9,
          "S3: getoff <mission> <epoch>");
    Check(!hd::Parse("getoff 30010", 12, q) && !hd::Parse("getoff 30010 0", 14, q) && !hd::Parse("rrec 30050 1", 12, q) &&
              !hd::Parse("getoff 30010 7 1", 16, q) && !hd::Parse("getoffx 30010 7", 15, q),
          "S3: rrec / getoff take exactly a free-roam mission and an epoch");
    hr::Rec r{};
    Check(std::strcmp(hr::GetOffRefusal(r), "unread") == 0, "S3: an unread record is never written");
    r.ok = true;
    Check(std::strcmp(hr::GetOffRefusal(r), "idle") == 0, "S3: state 0 (not riding) is refused");
    for (unsigned s : {1u, 2u, 3u, 4u, 20u, 21u, 28u, 30u}) {
        r.state = static_cast<std::uint8_t>(s);
        Check(std::strcmp(hr::GetOffRefusal(r), "state") == 0, "S3: get on (1, 2), get off (3), 4 and 20..28 have no get-off check");
    }
    for (unsigned s : {5u, 6u, 7u, 8u, 9u, 10u, 15u, 19u, 29u}) {
        r.state = static_cast<std::uint8_t>(s);
        Check(hr::GetOffRefusal(r) == nullptr, "S3: the states whose update runs the stock get-off check 0x14122F3F0 are written");
    }
    r.state = hr::kRideState;
    r.next = hr::kGetOffState;
    Check(std::strcmp(hr::GetOffRefusal(r), "pending") == 0, "S3: a next state already set (the stock get-off) is left alone");
    hr::Rec a{}, b{};
    Check(!hr::Changed(a, b), "S3: equal records log nothing");
    b.ok = true;
    Check(hr::Changed(a, b), "S3: the first read logs");
    a = b;
    b.state = 5;
    Check(hr::Changed(a, b), "S3: a state change logs");
    a = b;
    b.forced = 1;
    Check(hr::Changed(a, b), "S3: the forced get-off flag (+0x16E) logs");
    a = b;
    b.rec = 0x1234;
    Check(!hr::Changed(a, b), "S3: the record address alone does not log");
    Check(rc::record_size == 0x2b0 && rc::rec_next == 0x11 && rc::rec_again == 0x235 && rc::getoff_state == hr::kGetOffState &&
              rc::getoff_select == 6 && rc::ride_vtable == 0x23cb600 && rc::block_actions == 0x190 && rc::block_action_slots == 0x8a,
          "S3: the contract's record layout and the stock get-off's values (0x141236BA3..0x141236BB8)");
    Check(hr::kGetOffTries == 6 && hr::kGetOffGapMs == 500, "S3: Lua's getoff retries");
    const auto lab = [](const char* text) {
        mgscoop::LabConfig c{};
        (void)mgscoop::ParseLabConfig(text, std::strlen(text), c);
        return c;
    };
    const auto def = lab("enabled=1\nrole=host\npartner_native_profile=gun\n");
    const auto on = lab("enabled=1\nrole=guest\nworld_horse_down_off=1\n");
    const auto off = lab("enabled=1\nrole=guest\npartner_native_profile=gun\nworld_horse_down_off=0\n");
    const auto bad = lab("enabled=1\nrole=host\nworld_horse_down_off=2\n");
    Check(!def.world_horse_down_off && !def.world_horse_down_off_given && def.world_horse_pillion,
          "S3: world_horse_down_off is off unless named, also with the gun profile (gate not passed)");
    Check(on.world_horse_down_off && on.world_horse_down_off_given && on.unknown_keys == 0 && on.invalid_values == 0, "S3: a named 1");
    Check(!off.world_horse_down_off && off.world_horse_down_off_given, "S3: a named 0");
    Check(!bad.world_horse_down_off && bad.invalid_values == 1, "S3: an invalid value stays off");
    const auto playOn = lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                            "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\nworld=3\n"
                            "world_horse_down_off=1\n");
    Check(playOn.mode == mgscoop::LabMode::play && playOn.world_horse_down_off && playOn.play_refused == 0, "S3: a named 1 runs in play");
}

} // namespace

int main() {
    TestBodyWatch();
    TestMatrix();
    TestUnderRider();
    TestParse();
    TestRefusal();
    TestHoldAlone();
    TestHoldRunA0();
    TestHoldRefusal();
    TestShared();
    TestVisibility();
    TestRideKindShows();
    TestAnchorHold();
    TestMotionLogKey();
    TestLabKey();
    TestCargoKey();
    TestPillion();
    TestPillionKey();
    TestShot();
    TestShotKey();
    TestAimSeat();
    TestSeatErr();
    TestRideRecord();
    std::printf("%u horse drive checks passed: call-enter matrix, saddle anchor, receipt grammar, refusals, shared state, "
                "shared-horse rider visibility, lab key\n",
                checks);
    return 0;
}
