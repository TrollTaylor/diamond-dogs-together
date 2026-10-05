// Round 4: reconciliation of the input-driven partner body (partner_recon) on
// deterministic timelines. A tiny world model stands in for the game: the
// remote moves along a scripted path, the body moves with its own scripted
// velocity (the game's locomotion), and every action the module returns is
// applied to the body before the next tick, exactly as the driver does.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include "mgscoop/partner_recon.hpp"

namespace {
namespace pr = mgscoop::partner_recon;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
constexpr double kPi = 3.14159265358979323846;

struct World {
    pr::State st{};
    pr::Params params{};
    std::uint64_t now = 10000;
    double remote[3] = {0, 800, 0};
    double remote_yaw = 0;
    double remote_v[3] = {0, 0, 0};  // mm/s
    double r = 0;                    // remote stick magnitude
    double body[3] = {50000, 800, 50000};  // far from the remote before placement
    double body_yaw = 0;
    double body_v[3] = {0, 0, 0};    // the game's own motion (mm/s)
    bool follow = true;              // body velocity = remote velocity (input-driven locomotion)
    bool armable = true;
    bool live = true;
    bool in_valid = true;
    std::uint32_t in_age = 5;
    bool relink = false;
    bool snapped = false;
    bool warp_works = true;
    std::uint16_t body_act = 1, remote_act = 1;
    std::uint8_t body_stance = 1, remote_stance = 1;
    pr::PlaceReason request = pr::PlaceReason::none;
    bool last_ok = false;
    pr::Out out{};
    // Counters over the run.
    unsigned places = 0, snaps = 0, nudges = 0, pins = 0, yaws = 0, ticks = 0;
    std::uint32_t events = 0;
    double max_nudge_step = 0;
    unsigned place_reason[16]{};

    World() { pr::Reset(st); }
    pr::Out Tick(std::uint32_t dt_ms = 16) {
        now += dt_ms;
        ++ticks;
        // The world advances first (remote path and the body's own motion).
        for (int i = 0; i < 3; ++i) remote[i] += remote_v[i] * dt_ms / 1000.0;
        for (int i = 0; i < 3; ++i) body[i] += (follow ? remote_v[i] : body_v[i]) * dt_ms / 1000.0;
        pr::In in{};
        in.now_ms = now;
        in.armable = armable;
        in.place_request = request;
        request = pr::PlaceReason::none;
        in.body_ok = true;
        for (int i = 0; i < 3; ++i) in.body[i] = body[i];
        in.body_yaw = body_yaw;
        in.last_ok = last_ok;
        in.target_ok = true;
        for (int i = 0; i < 3; ++i) in.target[i] = remote[i];
        in.target_yaw = remote_yaw;
        in.target_live = live;
        in.target_snapped = snapped;
        snapped = false;
        in.relink = relink;
        relink = false;
        in.input_net = true;
        in.in_valid = in_valid;
        in.in_age_ms = in_age;
        in.in_r = r;
        in.body_anim_ok = true;
        in.body_act = body_act;
        in.body_stance = body_stance;
        in.remote_anim_valid = true;
        in.remote_act = remote_act;
        in.remote_stance = remote_stance;
        out = pr::Step(st, in, params);
        events |= out.events;
        last_ok = false;
        switch (out.action) {
        case pr::Action::place:
            ++places;
            ++place_reason[static_cast<unsigned>(out.place_reason)];
            if (warp_works) { for (int i = 0; i < 3; ++i) body[i] = out.pos[i]; body_yaw = out.yaw; }
            last_ok = true;
            break;
        case pr::Action::nudge: {
            ++nudges;
            const double dx = out.pos[0] - body[0], dz = out.pos[2] - body[2];
            const double step = std::sqrt(dx * dx + dz * dz);
            if (step > max_nudge_step) max_nudge_step = step;
            body[0] = out.pos[0];
            body[2] = out.pos[2];
            last_ok = true;
            break;
        }
        case pr::Action::snap:
        case pr::Action::pin:
            if (out.action == pr::Action::snap) ++snaps; else ++pins;
            for (int i = 0; i < 3; ++i) body[i] = out.pos[i];
            body_yaw = out.yaw;
            last_ok = true;
            break;
        case pr::Action::yaw:
            ++yaws;
            body_yaw = out.yaw;
            last_ok = true;
            break;
        default:
            break;
        }
        return out;
    }
    void Run(std::uint32_t ms, const std::function<void(World&)>& each = nullptr) {
        const std::uint64_t end = now + ms;
        while (now < end) {
            Tick();
            if (each) each(*this);
        }
    }
    double Err() const {
        const double dx = remote[0] - body[0], dz = remote[2] - body[2];
        return std::sqrt(dx * dx + dz * dz);
    }
    // Placement then settle: placed and tracking.
    void Place() {
        Tick();
        Check(out.action == pr::Action::place && !out.arm, "first tick places (window disarmed)");
        Tick();
        Check(out.mode == pr::Mode::track && out.arm && (out.events & pr::Event::placed_ok), "verified: armed and tracking");
        ResetCounters();
    }
    void ResetCounters() { places = snaps = nudges = pins = yaws = 0; events = 0; max_nudge_step = 0; for (auto& c : place_reason) c = 0; }
};

void TestHelpers() {
    Check(pr::AlignedDelayMs(false, 0, 0) == 1, "no input: delay 1");
    Check(pr::AlignedDelayMs(true, 5, 0) == 1, "fresh input: clamped to 1");
    Check(pr::AlignedDelayMs(true, 50, 0) == 34, "age 50: 34 ms");
    Check(pr::AlignedDelayMs(true, 400, 0) == 150, "clamped to 150");
    Check(pr::AlignedDelayMs(true, 50, -50) == 1 && pr::AlignedDelayMs(true, 16, 100) == 100, "align bias");
    Check(std::fabs(pr::WrapRad(3 * kPi) - kPi) < 1e-9 && std::fabs(pr::WrapRad(-3 * kPi) - kPi) < 1e-9, "wrap to (-pi, pi]");
    Check(pr::Alpha(0, 100) == 0.0 && pr::Alpha(100, 0) == 1.0 && std::fabs(pr::Alpha(150, 150) - (1 - std::exp(-1.0))) < 1e-12, "alpha");
}

// Placement waits for a live target (not for live input), then verifies before arming.
void TestPlacement() {
    World w;
    w.live = false;
    w.Tick();
    Check(w.out.action == pr::Action::none && w.out.mode == pr::Mode::place && !w.out.arm, "no live pose: wait, disarmed");
    w.live = true;
    w.in_valid = false;  // stale input does not delay the placement
    w.Tick();
    Check(w.out.action == pr::Action::place && w.out.place_reason == pr::PlaceReason::first && (w.out.events & pr::Event::place),
          "first placement at the target");
    Check(w.out.pos[0] == w.remote[0] && w.out.pos[2] == w.remote[2], "placed at the aligned target");
    w.Tick();
    Check((w.out.events & pr::Event::placed_ok) && w.out.arm && w.out.report.place_err_mm <= 500, "placed ok -> armed");
    w.in_valid = true;
    // Settle: no corrections for 700 ms.
    w.body[0] += 800;  // a drift right after the placement
    w.Run(600);
    Check(w.nudges == 0 && w.out.report.frozen == pr::Frozen::settle, "settle: frozen, no nudges");
    w.Run(400);
    Check(w.nudges > 0, "after the settle the drift is nudged");
}

// A failed placement retries, then falls back and tries again later.
void TestPlacementFailure() {
    World w;
    w.warp_works = false;
    w.Run(1000);
    Check(w.places == 3 && (w.events & pr::Event::placed_fail) && w.out.mode == pr::Mode::fallback && !w.out.arm,
          "three tries then fallback (the driver's per-tick Warp)");
    w.warp_works = true;
    w.ResetCounters();
    w.Run(5200);
    Check(w.places >= 1 && w.place_reason[static_cast<unsigned>(pr::PlaceReason::retry)] >= 1 && w.out.arm, "retry after 5 s, then armed");
}

// The I6 replay: the remote stands still, the body runs on its own at 3.2 m/s
// with a zero stick. Expect a runaway pin within ~0.6 s, the body pinned, and
// no snap sawtooth.
void TestRunawayReplay() {
    World w;
    w.Place();
    w.follow = false;
    w.body_v[2] = 3200.0;  // the stuck moving state
    double worst_after = 0;
    w.Run(20000, [&](World& v) {
        if (v.events & pr::Event::runaway && v.Err() > worst_after) worst_after = v.Err();
    });
    Check((w.events & pr::Event::runaway) != 0, "runaway detected");
    Check(w.snaps == 0 && w.places == 0, "no snap sawtooth and no warps");
    Check(w.out.mode == pr::Mode::pin && w.out.report.runaway_active, "pinned while the remote is idle");
    Check(worst_after <= 100.0, "pinned error <= 100 mm (one tick of motion)");
    // The remote starts moving: the runaway ends.
    w.ResetCounters();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.follow = true;
    w.Run(200);
    Check((w.events & pr::Event::runaway_end) && w.out.mode != pr::Mode::pin, "runaway ends when the remote moves");
    // runaway=off only logs.
    World o;
    o.params.runaway = mgscoop::PartnerRunaway::off;
    o.Place();
    o.follow = false;
    o.body_v[2] = 3200.0;
    o.Run(2000);
    Check((o.events & pr::Event::runaway) && o.out.mode != pr::Mode::pin, "runaway=off: logged, not pinned");
}

// An aligned run: the body follows the input exactly. No corrections at all.
void TestAlignedRun() {
    World w;
    w.Place();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.Run(10000);
    Check(w.snaps == 0 && w.places == 0 && w.pins == 0 && w.nudges == 0, "aligned run: no actions");
    // A turn by 90 degrees: the body turns with its own input.
    w.remote_v[2] = 0;
    w.remote_v[0] = 3200.0;
    w.remote_yaw = kPi / 2;
    w.Run(5000);
    Check(w.snaps == 0 && w.places == 0 && w.yaws == 0, "turn: no snaps, no yaw actions while moving");
    // Stop: idle, aligned.
    w.remote_v[0] = 0;
    w.r = 0;
    w.Run(3000);
    Check(w.snaps == 0 && w.places == 0 && w.out.report.err_mm < 50, "stop: still aligned");
}

// 1 m lateral drift while moving: nudges at <= 600 mm/s converge in < 3 s.
void TestLateralNudge() {
    World w;
    w.Place();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.Run(1000);
    w.body[0] += 1000.0;  // 1 m to the side
    w.ResetCounters();
    std::uint64_t converged = 0;
    const std::uint64_t start = w.now;
    w.Run(4000, [&](World& v) { if (!converged && v.Err() < 300.0) converged = v.now; });
    Check(w.nudges > 0 && w.snaps == 0 && w.places == 0, "nudges, no snap");
    Check(converged && converged - start < 3000, "converged below 300 mm within 3 s");
    Check(w.max_nudge_step <= 600.0 * 0.2 + 1e-6, "each nudge within the 600 mm/s cap");
    Check(w.Err() < 300.0, "stays converged");
}

// 4 m divergence while moving (a fast sideways drift the nudges cannot hold):
// exactly one hard snap.
void TestMovingSnap() {
    World w;
    w.Place();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.Run(1000);
    w.follow = false;
    w.body_v[2] = 3200.0;
    w.body_v[0] = 2000.0;
    w.ResetCounters();
    w.Run(2700);
    w.follow = true;
    w.Run(3000);
    Check(w.snaps == 1, "exactly one snap");
    Check(w.places == 0 && !(w.events & pr::Event::diverging), "no warp, no diverging");
    Check(w.Err() < 400.0, "converged afterwards");
}

// Repeated snaps (diverging): Warp then pin for a while.
void TestDiverging() {
    World w;
    w.Place();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.Run(1000);
    w.follow = false;
    w.body_v[2] = -3200.0;  // runs the other way: hopeless
    w.ResetCounters();
    w.Run(9000);
    Check((w.events & pr::Event::diverging) != 0, "diverging after repeated snaps");
    Check(w.place_reason[static_cast<unsigned>(pr::PlaceReason::diverging)] >= 1 && w.pins > 0, "Warp, then pinned");
    Check(w.snaps <= 6, "snaps bounded (no 1 Hz sawtooth)");
}

// A teleport of the remote: a new placement.
void TestTeleport() {
    World w;
    w.Place();
    w.remote[0] += 20000.0;
    w.Tick();
    Check(w.out.action == pr::Action::place && w.out.place_reason == pr::PlaceReason::teleport, "teleport -> place");
    w.Tick();
    Check(w.out.arm && (w.out.events & pr::Event::placed_ok), "placed again");
    // A far error without a jump (12 m): far placement.
    World f;
    f.Place();
    f.follow = false;
    f.body_v[0] = 2500.0;  // drifts away slowly while the remote idles... (no runaway: runaway=off)
    f.params.runaway = mgscoop::PartnerRunaway::off;
    f.params.snap_idle_mm = 20000;  // keep snaps out of the way
    f.params.snap_moving_mm = 20000;
    f.Run(6000);
    Check(f.place_reason[static_cast<unsigned>(pr::PlaceReason::far_error)] >= 1, "far error -> placement");
}

// Stance change: corrections are suspended, the dead zone doubles.
void TestStance() {
    World w;
    w.Place();
    w.Run(500);
    w.remote_stance = 2;
    w.body[0] += 600.0;
    w.ResetCounters();
    w.Run(200);
    Check((w.events & pr::Event::stance) != 0, "stance event");
    Check(w.out.report.frozen == pr::Frozen::stance && w.nudges == 0, "suspended after a stance change");
    w.Run(900);  // 150 ms debounce + 700 ms suspend
    Check(w.nudges > 0, "nudges resume after 700 ms");
    // To prone: 1500 ms suspend, prone dead zone and cap.
    World p;
    p.Place();
    p.Run(500);
    p.remote_stance = 3;
    p.body_stance = 3;
    p.ResetCounters();
    p.body[0] += 1000.0;
    p.Run(1400);
    Check(p.nudges == 0, "prone: suspended 1500 ms");
    p.Run(3000);
    Check(p.nudges > 0 && p.max_nudge_step <= 150.0 * 0.2 + 1e-6, "prone nudges within 150 mm/s");
    // Prone and far: a snap, never a Warp.
    p.ResetCounters();
    p.body[0] += 2500.0;
    p.Run(100);
    p.body[0] += 2500.0;
    p.Run(100);
    p.body[0] += 2500.0;
    p.Run(100);
    p.body[0] += 2600.0;
    p.Run(3000);
    Check(p.place_reason[static_cast<unsigned>(pr::PlaceReason::far_error)] == 0, "prone: no far Warp");
    Check(p.snaps >= 1, "prone far: xform snap");
    // A body that does not follow the stance: mismatch logged.
    World m;
    m.Place();
    m.remote_stance = 2;
    m.Run(2000);
    Check((m.events & pr::Event::stance_mismatch) != 0, "stance mismatch after 1.5 s");
}

// Non-locomotion acts freeze the corrections.
void TestFrozenActs() {
    World w;
    w.Place();
    w.Run(800);
    w.body_act = 0x1f;
    w.body[0] += 800.0;
    w.ResetCounters();
    w.Run(1000);
    Check(w.nudges == 0 && w.out.report.frozen == pr::Frozen::act && (w.events & pr::Event::frozen), "transition act: frozen");
    w.body_act = 1;
    w.Run(1000);
    Check((w.events & pr::Event::unfrozen) && w.nudges > 0, "unfrozen: nudges resume");
    w.remote_act = 0x79;
    w.ResetCounters();
    w.body[0] += 800.0;
    w.Run(500);
    Check(w.nudges == 0 && w.out.report.frozen == pr::Frozen::remote_act, "remote act: frozen");
}

// Loss and relink.
void TestLossRelink() {
    World w;
    w.Place();
    w.live = false;
    w.in_valid = false;
    w.body[0] += 900.0;
    w.ResetCounters();
    w.Run(4000);
    Check(w.nudges == 0 && w.snaps == 0 && w.out.report.frozen == pr::Frozen::pose_stale, "pose stale: frozen");
    w.live = true;
    w.Run(200);
    Check(w.out.report.frozen == pr::Frozen::input_stale && w.nudges == 0, "input stale: frozen");
    w.in_valid = true;
    w.relink = true;
    w.Tick();
    Check(w.out.action == pr::Action::place && w.out.place_reason == pr::PlaceReason::relink, "relink -> place");
}

// Standing still with a different facing: one yaw alignment.
void TestIdleYaw() {
    World w;
    w.Place();
    w.Run(500);
    w.remote_yaw = kPi / 2;
    w.ResetCounters();
    w.Run(3000);
    Check(w.yaws == 1 && std::fabs(pr::WrapRad(w.body_yaw - w.remote_yaw)) < 0.01, "one yaw alignment");
    Check(w.snaps == 0 && w.places == 0, "no snaps or warps");
    // Idle with a 2.5 m error that is not shrinking: a snap after 1 s.
    World s;
    s.Place();
    s.params.cap_idle_mmps = 1;  // nudges too weak to matter
    s.Run(800);
    s.body[0] += 2500.0;
    s.ResetCounters();
    s.Run(2500);
    Check(s.snaps == 1, "idle: one snap for a persistent 2.5 m error");
}

// Not armable (any driver refusal): fallback, then a new placement.
void TestRearm() {
    World w;
    w.Place();
    w.armable = false;
    w.Tick();
    Check(!w.out.arm && w.out.mode == pr::Mode::fallback && w.out.action == pr::Action::none, "fallback while not armable");
    w.armable = true;
    w.Tick();
    Check(w.out.action == pr::Action::place && w.out.place_reason == pr::PlaceReason::rearm, "re-armable: placed again (rearm)");
    w.Tick();
    w.request = pr::PlaceReason::manual;
    w.Tick();
    Check(w.out.action == pr::Action::place && w.out.place_reason == pr::PlaceReason::manual, "place=1");
}

// recon=pin pins every tick; recon=off only places.
void TestModes() {
    World p;
    p.params.recon = mgscoop::PartnerRecon::pin;
    p.Place();
    p.Run(1000);
    Check(p.pins >= 60 && p.out.mode == pr::Mode::pin, "recon=pin: pinned every tick");
    World o;
    o.params.recon = mgscoop::PartnerRecon::off;
    o.Place();
    o.body[0] += 5000.0;
    o.Run(3000);
    Check(o.nudges == 0 && o.snaps == 0 && o.places == 0, "recon=off: no corrections");
}

// Stop check (diagnostic): the stick released after a push.
void TestStopCheck() {
    World w;
    w.Place();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.Run(1000);
    w.remote_v[2] = 0;
    w.r = 0;
    w.ResetCounters();
    w.Run(1000);
    Check((w.events & pr::Event::stop_check) && w.out.report.stop_ok == 1, "the body stops: stop_ok=1");
    World s;
    s.Place();
    s.remote_v[2] = 3200.0;
    s.r = 1.0;
    s.Run(1000);
    s.remote_v[2] = 0;
    s.r = 0;
    s.follow = false;
    s.body_v[2] = 3200.0;  // keeps running
    s.Run(1000);
    Check(s.out.report.stop_ok == 0, "the body keeps running: stop_ok=0");
}

} // namespace

int main() {
    TestHelpers();
    TestPlacement();
    TestPlacementFailure();
    TestRunawayReplay();
    TestAlignedRun();
    TestLateralNudge();
    TestMovingSnap();
    TestDiverging();
    TestTeleport();
    TestStance();
    TestFrozenActs();
    TestLossRelink();
    TestIdleYaw();
    TestRearm();
    TestModes();
    TestStopCheck();
    std::printf("%u partner recon checks passed: placement/verify/retry, I6 runaway replay (pin, no sawtooth), aligned run and turn, "
                "lateral nudges, one moving snap, diverging, teleport/far, stance suspend/prone/mismatch, frozen acts, loss/relink, "
                "idle yaw and snap, rearm/manual, recon pin/off, stop check\n", checks);
    return 0;
}
