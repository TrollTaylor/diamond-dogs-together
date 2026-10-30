// M7: the partner's raised gun on the seated body 1 (partner_seat_gun.hpp), pure part. Synthetic only: no game
// code runs. Covers the driver's gate (every refusal word, in order), the remote's aim from its input sample, the
// hold decision (start, the release debounce, a gate fall, the retry wait after the hook ended a hold), the aim
// rates for body 1 (straight ahead, the seat range and its clamp, a wrapped yaw, non-finite input), the nodes the
// stock gun-hold state uses, and the gesture policy's two local kinds (never on the wire; their body gate and end).
#include "mgscoop/partner_seat_gun.hpp"
#include "mgscoop/partner_gesture_policy.hpp"
#include "mgscoop/partner_aim.hpp"
#include "partner_player_contract.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    std::printf("partner_seat_gun_tests: %u checks passed\n", checks);
    return 0;
}
