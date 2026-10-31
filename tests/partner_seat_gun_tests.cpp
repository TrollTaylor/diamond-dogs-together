// M7: the partner's raised gun on the seated body 1 (partner_seat_gun.hpp) and the seated shots' S1 (partner_seat_shot.hpp), pure part. Synthetic only: no game
// code runs. Covers the driver's gate (every refusal word, in order), the remote's aim from its input sample, the
// hold decision (start, the release debounce, a gate fall, the retry wait after the hook ended a hold), the aim
// rates for body 1 (straight ahead, the seat range and its clamp, a wrapped yaw, non-finite input), the nodes the
// stock gun-hold state uses, and the gesture policy's two local kinds (never on the wire; their body gate and end).
#include "mgscoop/partner_seat_gun.hpp"
#include "mgscoop/partner_seat_shot.hpp"
#include "mgscoop/partner_gesture_policy.hpp"
#include "mgscoop/partner_aim.hpp"
#include "partner_player_contract.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace sg = mgscoop::partner_seat_gun;
namespace gp = mgscoop::partner_gesture_policy;
namespace c = mgscoop::partner_player_contract;

namespace {
unsigned checks = 0;
void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) {
        std::fprintf(stderr, "failed: %s\n", label);
        std::exit(1);
    }
}
bool Is(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) <= eps; }
constexpr double kPi = 3.14159265358979323846;

sg::GateIn Ready() {
    sg::GateIn g{};
    g.enabled = g.installed = g.seated = g.passenger = g.realized = true;
    return g;
}
} // namespace

int main() {
    // --- The stock nodes and blends (StateGunHoldStart 0x1075A50, the GunHold imm64s at 0x1070B4C / 0x10721CD).
    Check(sg::kGunHoldStart == 0x50130AE2E584ULL && sg::kGunHold == 0xAA0EA22A5E0EULL, "the stock gun-hold nodes");
    Check(sg::kStartBlend == 0.0f, "the stock raise passes 0.0f to SetCurrentNode");
    // The generated contract pins the same nodes at their stock sites (seat_gun_assertions) and the rate fields the
    // hook writes (0x108E7D0's stores, the record chain the rates mirror uses).
    Check(sg::kGunHoldStart == c::seat_gun_node_start && sg::kGunHold == c::seat_gun_node_hold, "the contract's seat gun nodes");
    Check(c::rate_aim_pitch == 0x5c && c::rate_aim_yaw == 0x60 && c::rate_rec_stride == 0x88 && c::ps_rates == 0x260 &&
              c::rate_sp_vtable_rva == 0x23ad380 && c::rate_rec_vtable_rva == 0x23ad2b0 && c::rate_hold_type == 0x58,
          "the contract's rate record fields and vtables (the hook's constants)");
    Check(sizeof(c::seat_gun_blocks) / sizeof(c::seat_gun_blocks[0]) == 2 && c::seat_gun_blocks[0] == 0x10721a9, "the seat gun blocks");
    Check(sg::kPitchRangeDeg == c::aim_range_deg[0][0] && sg::kYawRangeDeg == c::aim_range_deg[0][1],
          "the default class's seat range is the contract's aim range row 0");

    // --- The gate, in order.
    Check(sg::Gate(Ready()) == nullptr, "gate: ready");
    {
        sg::GateIn g = Ready(); g.enabled = false; Check(Is(sg::Gate(g), "key_off"), "gate: key off");
        g = Ready(); g.installed = false; Check(Is(sg::Gate(g), "not_installed"), "gate: no hook");
        g = Ready(); g.uncertain = true; Check(Is(sg::Gate(g), "uncertain"), "gate: the mission latch");
        g = Ready(); g.seated = false; Check(Is(sg::Gate(g), "not_seated"), "gate: not seated");
        g = Ready(); g.passenger = false; Check(Is(sg::Gate(g), "driver"), "gate: the remote drives (seat 0)");
        g = Ready(); g.realized = false; Check(Is(sg::Gate(g), "unrealized"), "gate: unrealized");
        g = Ready(); g.block = "carry"; Check(Is(sg::Gate(g), "carry"), "gate: a block names itself");
        g = Ready(); g.other_gesture = true; Check(Is(sg::Gate(g), "busy"), "gate: another gesture runs");
        g = Ready(); g.enabled = false; g.seated = false; Check(Is(sg::Gate(g), "key_off"), "gate: the key first");
    }
    // The remote's seat the gate takes as a passenger (review of wp/seat-shoot): 1..3 of a live vehicle ride; never
    // the driver's seat 0, a seat the wire leaves unknown (255) or one past a light vehicle's four.
    Check(sg::PassengerSeat(true, 1) && sg::PassengerSeat(true, 2) && sg::PassengerSeat(true, 3), "passenger seat: 1..3");
    Check(!sg::PassengerSeat(true, 0), "passenger seat: the driver's seat 0 is not one");
    Check(!sg::PassengerSeat(true, 4) && !sg::PassengerSeat(true, 255), "passenger seat: 4 and the unknown seat 255 are not");
    Check(!sg::PassengerSeat(false, 2), "passenger seat: no live vehicle ride, no seat");

    // --- The remote's aim: its live pose flags, or its newest input frame.
    Check(sg::RemoteAiming({true, true, false, 0, false}), "aim: the live pose sample's Aiming flag");
    Check(!sg::RemoteAiming({false, true, false, 0, false}), "aim: a stale pose sample's flag does not count");
    Check(!sg::RemoteAiming({true, false, false, 0, false}), "aim: the pose says not aiming, no input");
    Check(sg::RemoteAiming({true, false, true, 40, true}), "aim: a live v5 Aiming input frame");
    Check(!sg::RemoteAiming({false, false, true, sg::kSampleStaleMs + 1, true}), "aim: a stale input frame");
    Check(sg::RemoteAiming({false, false, true, sg::kSampleStaleMs, true}), "aim: the input stale edge is still live");
    Check(!sg::RemoteAiming({false, false, false, 0, true}), "aim: no input sample");
    Check(!sg::RemoteAiming({false, false, true, 40, false}), "aim: the input frame says not aiming");

    // --- The hold decision.
    {
        sg::State s{};
        sg::StepOut o = sg::Step(s, nullptr, false, 1000);
        Check(o.edge == sg::Edge::none && !s.want, "step: idle without aim");
        o = sg::Step(s, nullptr, true, 1100);
        Check(o.edge == sg::Edge::start && s.want && s.starts == 1, "step: Aiming starts the hold at once");
        o = sg::Step(s, nullptr, true, 1200);
        Check(o.edge == sg::Edge::none && s.want, "step: still aiming");
        o = sg::Step(s, nullptr, false, 1200 + sg::kReleaseMs - 1);
        Check(o.edge == sg::Edge::none && s.want, "step: the release debounce keeps the hold");
        o = sg::Step(s, nullptr, true, 1200 + sg::kReleaseMs);
        Check(o.edge == sg::Edge::none && s.want, "step: a renewed aim inside the debounce keeps the hold");
        o = sg::Step(s, nullptr, false, 1200 + 2 * sg::kReleaseMs);
        Check(o.edge == sg::Edge::release && !s.want && Is(o.cause, "aim") && s.releases == 1, "step: the debounce ends the hold");
        o = sg::Step(s, nullptr, true, 1200 + 2 * sg::kReleaseMs + 10);
        Check(o.edge == sg::Edge::start && s.want && s.starts == 2, "step: a driver release needs no retry wait");
        // A gate fall ends it at once and names the gate.
        o = sg::Step(s, "not_seated", true, 5000);
        Check(o.edge == sg::Edge::release && !s.want && Is(o.cause, "gate") && Is(s.refusal, "not_seated"), "step: the gate falls");
        o = sg::Step(s, "not_seated", true, 5100);
        Check(o.edge == sg::Edge::none && !s.want, "step: no start while the gate refuses");
        o = sg::Step(s, nullptr, true, 5200);
        Check(o.edge == sg::Edge::start && s.want && s.refusal == nullptr, "step: the gate back, aiming: start");
        // The hook ended it (node override, refusal): a retry wait, then a new start.
        sg::HookEnded(s, 6000);
        Check(!s.want, "hook ended: the driver drops its hold");
        o = sg::Step(s, nullptr, true, 6000 + sg::kRetryMs - 1);
        Check(o.edge == sg::Edge::none && !s.want, "hook ended: no start inside the retry wait");
        o = sg::Step(s, nullptr, true, 6000 + sg::kRetryMs);
        Check(o.edge == sg::Edge::start && s.want, "hook ended: a start after the retry wait");
        // A clock going backwards never releases or starts by itself.
        sg::State b{};
        sg::Step(b, nullptr, true, 9000);
        o = sg::Step(b, nullptr, false, 8000);
        Check(o.edge == sg::Edge::none && b.want, "step: a backwards clock keeps the hold");
    }

    // --- The aim rates for body 1.
    {
        sg::RatesOut r = sg::Rates({true, 0.0, 1.0, true, 1.0});
        Check(r.ok && Near(r.r5c, 0.5) && Near(r.r60, 0.5) && !r.clamped, "rates: straight ahead of the seat");
        // 45 deg to the body's left (yaw + pi/4): r60 = 0.5 - 45/(2*90) = 0.25 (0x108EC55: rel_yaw = (0.5 - r60) * 2 * 90).
        r = sg::Rates({true, 0.0, 1.0 + kPi / 4, true, 1.0});
        Check(r.ok && Near(r.r60, 0.25) && Near(r.r5c, 0.5), "rates: 45 deg of yaw");
        // 30 deg up: rel_pitch = (r5c - 0.5) * 2 * 75 -> r5c = 0.5 + 30/150 = 0.7.
        r = sg::Rates({true, 30.0 * kPi / 180.0, 1.0, true, 1.0});
        Check(r.ok && Near(r.r5c, 0.7) && Near(r.r60, 0.5), "rates: 30 deg of pitch");
        // Outside the seat range: clamped and flagged.
        r = sg::Rates({true, 0.0, 1.0 + kPi * 0.75, true, 1.0});
        Check(r.ok && r.clamped && Near(r.r60, 0.0), "rates: 135 deg is clamped to the range edge");
        r = sg::Rates({true, -kPi / 2, 1.0, true, 1.0});
        Check(r.ok && r.clamped && Near(r.r5c, 0.0), "rates: straight down is clamped");
        // The yaw difference wraps.
        r = sg::Rates({true, 0.0, -kPi + 0.1, true, kPi - 0.1});
        Check(r.ok && Near(r.r60, 0.5 - (0.2 * 180.0 / kPi) / 180.0), "rates: the yaw difference wraps across pi");
        // Refusals.
        Check(!sg::Rates({false, 0.0, 0.0, true, 0.0}).ok, "rates: no direction");
        Check(!sg::Rates({true, 0.0, 0.0, false, 0.0}).ok, "rates: no body yaw");
        Check(!sg::Rates({true, std::nan(""), 0.0, true, 0.0}).ok, "rates: non-finite pitch");
        Check(!sg::Rates({true, 0.0, 0.0, true, INFINITY}).ok, "rates: non-finite body yaw");
        Check(sg::RateOk(0.0f) && sg::RateOk(1.0f) && !sg::RateOk(1.01f) && !sg::RateOk(-0.01f) && !sg::RateOk(std::nanf("")),
              "rates: the write bounds");
    }

    // --- The gesture policy's local kinds.
    Check(gp::GunKind(gp::Kind::gun_start) && gp::GunKind(gp::Kind::gun_hold) && !gp::GunKind(gp::Kind::pickup) &&
              !gp::GunKind(gp::Kind::none),
          "gesture: the gun kinds");
    Check(Is(gp::KindName(gp::Kind::gun_start), "gun_start") && Is(gp::KindName(gp::Kind::gun_hold), "gun_hold"), "gesture: kind names");
    {
        // Never on the wire: Encode refuses them, Decode refuses kind 5 / 6.
        gp::Packet p{};
        p.kind = gp::Kind::gun_start; p.seq = 1; p.stance = 1;
        std::uint8_t out[gp::kSize]{};
        Check(!gp::Encode(p, out), "gesture: a gun kind is never encoded");
        p.kind = gp::Kind::pickup;
        Check(gp::Encode(p, out), "gesture: a pickup still encodes");
        out[2] = 5;
        gp::Packet back{};
        Check(!gp::Decode(out, sizeof(out), back), "gesture: kind 5 never decodes");
        Check(gp::KindForUpper(2) == gp::Kind::none, "gesture: the aim upper act is not a sent gesture");
    }
    // The seated body gate: body 1 in the DM seat act with no upper action of its own.
    Check(gp::SeatBodyReady(c::q18_dm_act, 0, 0), "seat body: the DM act, idle upper");
    Check(!gp::SeatBodyReady(1, 0, 0), "seat body: plain locomotion is not seated");
    Check(!gp::SeatBodyReady(c::q18_dm_act, 0, 0x15), "seat body: another upper action runs");
    Check(!gp::SeatBodyReady(c::q18_dm_act, 1, 0), "seat body: Q+0x28 set");
    Check(gp::SeatBodyReady(c::q18_dm_act, 0, 2), "seat body: our own raised gun keeps the gate (upper act 2 is the aim)");
    // The end of a gun kind: no deadline, the driver's release, the raise chaining to the held loop.
    {
        gp::EndIn e{};
        e.kind = gp::Kind::gun_hold; e.lease = e.identity = e.ours = e.held = true; e.elapsed_ms = 60000;
        gp::EndOut o = gp::End(e);
        Check(o.via == gp::Via::none && !o.chain, "end: a held gun has no deadline");
        e.held = false;
        o = gp::End(e);
        Check(o.via == gp::Via::end && Is(o.cause, "release") && o.null_return, "end: the driver's release returns stateNull");
        e.held = true; e.lease = false;
        o = gp::End(e);
        Check(o.via == gp::Via::override_ && Is(o.cause, "lease") && o.null_return, "end: a lost lease returns stateNull");
        e.lease = true; e.pending = true;
        o = gp::End(e);
        Check(o.via == gp::Via::override_ && Is(o.cause, "pending") && !o.null_return, "end: another owner's request, no write");
        e.pending = false; e.ours = false; e.elapsed_ms = 100;
        o = gp::End(e);
        Check(o.via == gp::Via::none, "end: the node grace");
        e.elapsed_ms = 400;
        o = gp::End(e);
        Check(o.via == gp::Via::override_ && Is(o.cause, "node") && !o.null_return, "end: another owner of graph 2");
        e.ours = true; e.identity = false;
        o = gp::End(e);
        Check(o.via == gp::Via::override_ && Is(o.cause, "identity") && !o.null_return, "end: identity first");
        // The raise: its clip end chains to GunHold (after the minimum play time), never ends the hold.
        gp::EndIn s{};
        s.kind = gp::Kind::gun_start; s.lease = s.identity = s.ours = s.held = true; s.clip_end = true; s.elapsed_ms = gp::kMinPlayMs;
        o = gp::End(s);
        Check(o.via == gp::Via::none && o.chain, "end: the raise clip's end chains to the held loop");
        s.elapsed_ms = gp::kMinPlayMs - 1;
        o = gp::End(s);
        Check(o.via == gp::Via::none && !o.chain, "end: no chain before the minimum play time");
        s.elapsed_ms = 5000; s.clip_end = false;
        o = gp::End(s);
        Check(o.via == gp::Via::none && !o.chain, "end: a long raise without a clip end keeps waiting");
        s.held = false; s.clip_end = true;
        o = gp::End(s);
        Check(o.via == gp::Via::end && Is(o.cause, "release") && !o.chain, "end: a release during the raise ends it");
        // The old kinds keep their rules (deadline, clip).
        gp::EndIn p{};
        p.kind = gp::Kind::pickup; p.lease = p.identity = p.ours = true; p.elapsed_ms = gp::kEndDeadlineMs;
        o = gp::End(p);
        Check(o.via == gp::Via::timeout && !o.chain, "end: a pickup still has its deadline (held is ignored)");
    }
    // --- The passenger's seated aim direction (partner_aim::GunDirSeated): GunDir with standing in place of the seated
    // stance byte, never the self-check (RideGate); a refusal keeps its own reason.
    {
        namespace pa = mgscoop::partner_aim;
        pa::GunDirIn in{};
        in.gun_ok = in.rate_ok = in.body_ok = true;
        in.gun_class = 0; in.stance = 0; in.b3d5 = 0; in.gun3c0 = 0;
        in.r5c = 0.5f; in.r60 = 0.25f; in.body_yaw = 1.0f;
        Check(pa::GunDir(in).reason == pa::DirReason::stance, "seated dir: the on-foot GunDir refuses the seated stance byte");
        pa::GunDirOut d = pa::GunDirSeated(in);
        Check(d.ok && d.reason == pa::DirReason::seat && Near(d.yaw, 1.0 + kPi / 4) && Near(d.pitch, 0.0), "seated dir: 45 deg left of the seat");
        // Round trip through the receiver's rates.
        const sg::RatesOut back = sg::Rates({true, d.pitch, d.yaw, true, 1.0});
        Check(back.ok && Near(back.r5c, 0.5) && Near(back.r60, 0.25), "seated dir: the receiver's rates invert it");
        in.stance = 3;
        Check(pa::GunDirSeated(in).ok, "seated dir: a prone-looking stance byte is still the standing set");
        in.gun3c0 = c::gun3c0_camera_rel;
        Check(!pa::GunDirSeated(in).ok && pa::GunDirSeated(in).reason == pa::DirReason::camera, "seated dir: camera-relative refuses");
        in.gun3c0 = 0; in.r60 = 1.5f;
        Check(pa::GunDirSeated(in).reason == pa::DirReason::range, "seated dir: out of range refuses");
        in.r60 = 0.5f; in.body_ok = false;
        Check(pa::GunDirSeated(in).reason == pa::DirReason::body, "seated dir: no body yaw refuses");
        Check(std::strcmp(pa::DirReasonName(pa::DirReason::seat), "seat") == 0, "seated dir: the reason name");
    }
    // --- M7 seated shots, S1 (partner_seat_shot.hpp): the box, the plan, the lever ----------------------------------
    {
        namespace ss = mgscoop::partner_seat_shot;
        // The classes: light vehicles (category 1, 2) in seats 1..3, a truck (3, 4) only in its cab seat 1.
        Check(ss::ClassFor(1, 2) == ss::Class::light && ss::ClassFor(2, 3) == ss::Class::light && ss::ClassFor(2, 1) == ss::Class::light,
              "S1 class: light vehicles in seats 1..3");
        Check(ss::ClassFor(4, 1) == ss::Class::truck && ss::ClassFor(3, 1) == ss::Class::truck && ss::ClassFor(4, 2) == ss::Class::none,
              "S1 class: a truck only in its cab seat");
        Check(ss::ClassFor(2, 0) == ss::Class::none && ss::ClassFor(2, 4) == ss::Class::none && ss::ClassFor(5, 1) == ss::Class::none &&
                  ss::ClassFor(0, 2) == ss::Class::none,
              "S1 class: the driver, seat 4, armoured and unknown categories: none");
        Check(Is(ss::ClassName(ss::Class::light), "light") && Is(ss::ClassName(ss::Class::truck), "truck") &&
                  Is(ss::ClassName(ss::Class::none), "none"),
              "S1 class names");
        // The vehicle frame: yaw 0 faces +z, its right is -x (lua goto_vehicle's passenger side, yaw - pi/2).
        {
            const float w[3] = {0.0f, 2.0f, 1.0f}, r[3] = {-1.0f, 0.0f, 0.0f};
            float a[3], b[3];
            ss::ToLocal(0.0f, w, a);
            ss::ToLocal(0.0f, r, b);
            Check(Near(a[0], 1.0) && Near(a[1], 0.0) && Near(a[2], 2.0) && Near(b[0], 0.0) && Near(b[1], 1.0) && Near(b[2], 0.0),
                  "S1 frame: forward +z, right -x, up +y at yaw 0");
            const float f90[3] = {1.0f, 0.0f, 0.0f};
            ss::ToLocal(static_cast<float>(kPi / 2), f90, a);
            Check(Near(a[0], 1.0) && Near(a[1], 0.0), "S1 frame: yaw +90 deg faces +x");
        }
        // The slab test: inside, outside crossing, grazing a face, missing, behind.
        {
            const float h[3] = {2.0f, 1.0f, 1.0f};
            float tin = 0, tout = 0;
            const float o0[3] = {0, 0, 0}, dx[3] = {0, 1, 0};
            Check(ss::RayBox(o0, dx, h, tin, tout) && tin < 0 && Near(tout, 1.0), "S1 box: from the centre out of a side: exit 1 m");
            const float o1[3] = {0, -3, 0};
            Check(ss::RayBox(o1, dx, h, tin, tout) && Near(tin, 2.0) && Near(tout, 4.0), "S1 box: from outside through it: enter 2, exit 4");
            const float o2[3] = {0, -3, 1.0f};
            Check(ss::RayBox(o2, dx, h, tin, tout) && Near(tout, 4.0), "S1 box: grazing the top face counts");
            const float o3[3] = {0, -3, 1.5f};
            Check(!ss::RayBox(o3, dx, h, tin, tout), "S1 box: above the box: no crossing");
            const float o4[3] = {0, 3, 0};
            Check(!ss::RayBox(o4, dx, h, tin, tout), "S1 box: the box behind the start: no crossing");
        }
        // The light box is the jeep (~4.2 x 1.9 m) plus about 5 cm a side, not more: a start moved past a bigger box can
        // skip a wall or rock the jeep stands against (review of 8.30). The publication is fresh for 50 ms at most: a pose
        // older than that is metres off on a moving jeep.
        Check(ss::kLightBox.half_side == 1.0f && ss::kLightBox.half_fwd == 2.2f && ss::kFreshMs <= 50,
              "S1 box: the light box hugs the jeep (4.4 x 2.0 m); fresh for 50 ms");
        // The freshness window follows the vehicle's speed (review of 8.37): a parked jeep's pose stays good for 250 ms, so a
        // 25-30 fps copy (33-40 ms ticks on a 15.6 ms clock) never judges a parked shot stale; at 20 m/s and over it is
        // 50 ms (1 m of drift); unknown speed is 50 ms.
        Check(ss::MaxAgeMs(-1.0f) == 50 && ss::MaxAgeMs(std::numeric_limits<float>::quiet_NaN()) == 50 && ss::MaxAgeMs(0.0f) == 250 &&
                  ss::MaxAgeMs(2.0f) == 250 && ss::MaxAgeMs(10.0f) == 100 && ss::MaxAgeMs(20.0f) == 50 && ss::MaxAgeMs(40.0f) == 50,
              "S1 fresh: the window by speed (unknown 50, parked 250, 10 m/s 100, 20+ m/s 50 ms)");
        Check(ss::Fresh(1120, 1000, 0.0f) && !ss::Fresh(1060, 1000, 20.0f) && ss::Fresh(1050, 1000, -1.0f) && !ss::Fresh(1251, 1000, 0.0f) &&
                  !ss::Fresh(999, 1000, 0.0f),
              "S1 fresh: parked 120 ms fresh; 20 m/s 60 ms stale; 251 ms stale; a future stamp stale");
        // A seated shot across a jeep at yaw 0: the start 0.3 m right of the centre line and 1.1 m up (in the box), aiming
        // left and slightly down at a guard 15 m out: the start moves to the box's left side (-1.0 m) + 5 cm.
        ss::Pub p{};
        p.owner = 0;
        p.vehicle = 25608;
        p.seat = 3;
        p.cls = ss::Class::light;
        p.apply = true;
        p.target_ok = true;
        p.pos[0] = 100.0f; p.pos[1] = 10.0f; p.pos[2] = 200.0f;
        p.yaw = 0.0f;
        const float start[3] = {100.0f - 0.3f, 11.1f, 200.0f - 1.0f};  // right 0.3 (x -0.3), 1 m behind the record
        const float aim[3] = {100.0f + 15.0f, 10.2f, 200.0f - 1.0f};
        p.target[0] = aim[0]; p.target[1] = aim[1]; p.target[2] = aim[2];
        const float dir[3] = {aim[0] - start[0], aim[1] - start[1], aim[2] - start[2]};  // not unit: the plan normalizes
        ss::Plan q = ss::PlanShot(p, start, dir);
        Check(Is(q.result, "shift") && q.move && q.inside, "S1 plan: inside, moved");
        Check(Near(q.o_local[0], -1.0, 1e-3) && Near(q.o_local[1], 0.3, 1e-3) && Near(q.o_local[2], 1.1, 1e-3),
              "S1 plan: the start in the vehicle frame (1 m back, 0.3 m right, 1.1 m up)");
        Check(q.d_local[1] < -0.99f, "S1 plan: the direction points left (across the jeep)");
        // Exit through the left side at local right = -1.0: the line runs 1.3 m sideways (plus the dip), then +5 cm.
        Check(q.exit > 1.29f && q.exit < 1.32f && Near(q.t, q.exit + ss::kExitPadM, 1e-4), "S1 plan: the exit and the pad");
        Check(Near(q.dist, std::sqrt(15.3 * 15.3 + 0.9 * 0.9), 1e-3), "S1 plan: the target distance");
        Check(q.pos[0] > 100.0f + 1.0f && q.pos[0] < 100.0f + 1.1f && std::fabs(q.pos[2] - (200.0f - 1.0f)) < 1e-3f,
              "S1 plan: the moved start just past the left side, on the line");
        // The same with apply off: planned, not moved (the lab census).
        p.apply = false;
        q = ss::PlanShot(p, start, dir);
        Check(Is(q.result, "census") && !q.move && q.t > 1.3f, "S1 plan: apply off plans without a move");
        p.apply = true;
        // The target 1.2 m away, inside the box: the start stops 0.3 m before it.
        {
            ss::Pub n = p;
            n.target[0] = start[0] + 1.2f; n.target[1] = start[1]; n.target[2] = start[2];
            const float d2[3] = {1, 0, 0};
            q = ss::PlanShot(n, start, d2);
            Check(Is(q.result, "shift") && Near(q.t, 0.9, 1e-4), "S1 plan: never closer than 0.3 m to the target");
            n.target[0] = start[0] + 0.25f;
            q = ss::PlanShot(n, start, d2);
            Check(Is(q.result, "near") && !q.move, "S1 plan: a target within 0.3 m: nothing to move");
        }
        // A start outside the box: a seated passenger's muzzle is always inside it, so the pose is stale or wrong (a moving
        // jeep's box ahead along the line): nothing moves, whether the target is before or beyond the box.
        {
            ss::Pub n = p;
            const float s2[3] = {100.0f - 5.0f, 11.0f, 199.0f};  // 5 m right of the jeep
            n.target[0] = 100.0f - 3.0f; n.target[1] = 11.0f; n.target[2] = 199.0f;  // a guard between the start and the jeep
            const float d2[3] = {1, 0, 0};
            q = ss::PlanShot(n, s2, d2);
            Check(Is(q.result, "outside") && !q.move && !q.inside, "S1 plan: a start outside the box (target before it): outside");
            n.target[0] = 100.0f + 15.0f;
            q = ss::PlanShot(n, s2, d2);
            Check(Is(q.result, "outside") && !q.move && !q.inside, "S1 plan: a start outside the box (target beyond it): outside");
            // The jeep moved 4 m on since the publication (200 ms at 20 m/s; the old box 4 m behind the real one) and the
            // passenger shoots backwards: the old box lies ahead along the line (it would have moved the start 5.5 m back,
            // through whatever stands there). The muzzle (0.8 m behind the box centre) is outside the old box: no move.
            // Up to 3 m it stays inside: kFreshMs keeps a pose within ~1 m.
            n = p;
            n.pos[2] = 200.0f - 4.0f;
            n.target[0] = start[0]; n.target[1] = start[1]; n.target[2] = start[2] - 15.0f;
            const float back[3] = {0, 0, -1};
            q = ss::PlanShot(n, start, back);
            Check(Is(q.result, "outside") && !q.move && q.exit > 5.0f, "S1 plan: an old box ahead along the line (a stale pose): outside");
        }
        // Refusals: no class, no target, a zero or non-finite direction, a line that misses the box.
        {
            ss::Pub n = p;
            n.cls = ss::Class::none;
            Check(Is(ss::PlanShot(n, start, dir).result, "class"), "S1 plan: no class");
            n = p;
            n.target_ok = false;
            Check(Is(ss::PlanShot(n, start, dir).result, "target") && !ss::PlanShot(n, start, dir).move, "S1 plan: no target read: no move");
            const float zero[3] = {0, 0, 0};
            Check(Is(ss::PlanShot(p, start, zero).result, "dir"), "S1 plan: a zero direction");
            const float bad[3] = {std::numeric_limits<float>::quiet_NaN(), 0, 1};
            Check(Is(ss::PlanShot(p, start, bad).result, "nonfinite"), "S1 plan: a non-finite direction");
            const float far_start[3] = {100.0f, 30.0f, 200.0f}, up[3] = {0, 1, 0};
            Check(Is(ss::PlanShot(p, far_start, up).result, "miss"), "S1 plan: the line does not cross the box");
        }
        // A truck cab: the bigger box (half side 1.45 m).
        {
            ss::Pub n = p;
            n.cls = ss::Class::truck;
            n.seat = 1;
            const float s2[3] = {100.0f - 0.4f, 11.6f, 200.9f};
            n.target[0] = 100.0f - 20.0f; n.target[1] = 11.0f; n.target[2] = 200.9f;
            const float d2[3] = {-1, 0, 0};
            q = ss::PlanShot(n, s2, d2);
            Check(Is(q.result, "shift") && Near(q.exit, 1.05, 1e-3), "S1 plan: the truck cab out of the right window");
        }
        // The lever (lab only): on / off / lab (none) / status; other text is not ours.
        {
            char r[160]{};
            bool ok = false;
            int next = 7;
            Check(!ss::HandleCommand("seatshots on", true, -1, next, r, sizeof(r), ok) && next == -1, "S1 lever: a longer word is not ours");
            Check(ss::HandleCommand("seatshot on", false, -1, next, r, sizeof(r), ok) && !ok && next == -1 && std::strstr(r, "reason=lab_only"),
                  "S1 lever: lab only");
            Check(ss::HandleCommand("seatshot on", true, -1, next, r, sizeof(r), ok) && ok && next == 1 && Is(r, "seatshot=on; override=set"),
                  "S1 lever: on");
            Check(ss::HandleCommand("seatshot off", true, 1, next, r, sizeof(r), ok) && ok && next == 0, "S1 lever: off");
            Check(ss::HandleCommand("seatshot lab", true, 0, next, r, sizeof(r), ok) && ok && next == -1 && Is(r, "seatshot=lab; override=none"),
                  "S1 lever: lab clears");
            Check(ss::HandleCommand("seatshot status", true, 1, next, r, sizeof(r), ok) && ok && next == 1 && std::strstr(r, "seatshot=on"),
                  "S1 lever: status keeps");
            Check(ss::HandleCommand("seatshot maybe", true, 1, next, r, sizeof(r), ok) && !ok && next == 1 && std::strstr(r, "reason=args"),
                  "S1 lever: bad argument refused");
            Check(ss::HandleCommand("seatshot", true, 0, next, r, sizeof(r), ok) && !ok && next == 0, "S1 lever: no argument refused");
        }
        // The record layout the hook relies on (RequestBullet's slot copy 0xD32690; the pellet loop 0x130342E).
        Check(ss::kPos == 0x00 && ss::kDir == 0x10 && ss::kRecordBytes == 0x34 && c::shot_owner == 0x2a,
              "S1 record: start +0x00, direction +0x10, owner +0x2A, 0x34 bytes copied");
        // The partner's code gate (all-or-nothing) holds only what S1 relies on: the copy and the pellet store
        // (SEAT_SHOT_SITES). The static facts behind S1 (the hit loop's owner skip, the shot target, the camera's writer,
        // the origin call) are generator checks only (SEAT_SHOT_FACTS), so they never switch the partner off.
        {
            bool facts_in_gate = false;
            for (const auto& s : c::code_sites)
                if (s.rva == 0xd2dc60 || s.rva == 0x108b0fc || s.rva == 0x11192c5 || s.rva == 0x1119314 || s.rva == 0x1041214)
                    facts_in_gate = true;
            Check(sizeof(c::seat_shot_blocks) / sizeof(c::seat_shot_blocks[0]) == 2 && c::seat_shot_blocks[0] == 0xd32690 &&
                      c::seat_shot_blocks[1] == 0x130342e && !facts_in_gate,
                  "S1 gate: only the copy and the pellet store join the partner code gate");
        }
    }
    std::printf("partner_seat_gun_tests: %u checks passed\n", checks);
    return 0;
}
