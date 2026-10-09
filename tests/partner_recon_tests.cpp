// Round 4: reconciliation of the input-driven partner body (partner_recon) on
// deterministic timelines. A tiny world model stands in for the game: the
// remote moves along a scripted path, the body moves with its own scripted
// velocity (the game's locomotion), and every action the module returns is
// applied to the body before the next tick, exactly as the driver does.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <deque>
#include <functional>
#include <string>
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
    // Lag model: the remote's actual velocity approaches remote_v with
    // remote_accel (mm/s^2, 0 = instant: its own locomotion turns and
    // accelerates); the body (follow) takes the remote's actual velocity of
    // lag_ms ago with its own acceleration limit accel (0 = instant).
    double remote_accel = 0;
    double rv[3] = {0, 0, 0};
    std::uint32_t lag_ms = 0;
    double accel = 0;
    double lag_v[3] = {0, 0, 0};
    std::deque<std::array<double, 3>> v_hist;
    bool nudge_sticks = true;        // false: position writes of nudges are ignored by the game
    pr::StanceFix last_fix = pr::StanceFix::none;
    bool apply_fix = false;          // the corrective stance input changes body_stance
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
    unsigned places = 0, snaps = 0, nudges = 0, pins = 0, yaws = 0, ticks = 0, runaways = 0, fixes = 0, input_pins = 0, faces = 0;
    bool face_sticks = true;         // false: the game rotates the body back after a governor write (contest)
    double face_back_yaw = 0;        // where the game puts it back
    std::uint32_t events = 0;
    double max_nudge_step = 0;
    unsigned place_reason[16]{};

    World() { pr::Reset(st); }
    pr::Out Tick(std::uint32_t dt_ms = 16) {
        now += dt_ms;
        ++ticks;
        // The world advances first (remote path and the body's own motion).
        if (remote_accel > 0) {
            const double rstep = remote_accel * dt_ms / 1000.0;
            double ax = remote_v[0] - rv[0], az = remote_v[2] - rv[2];
            const double alen = std::sqrt(ax * ax + az * az);
            if (alen > rstep) { ax *= rstep / alen; az *= rstep / alen; }
            rv[0] += ax;
            rv[2] += az;
        } else {
            for (int i = 0; i < 3; ++i) rv[i] = remote_v[i];
        }
        for (int i = 0; i < 3; ++i) remote[i] += rv[i] * dt_ms / 1000.0;
        if (follow && (lag_ms || accel > 0)) {
            v_hist.push_back({rv[0], rv[1], rv[2]});
            const std::size_t lag_ticks = lag_ms / dt_ms;
            while (v_hist.size() > lag_ticks + 1) v_hist.pop_front();
            const std::array<double, 3>& want = v_hist.front();
            const double step = accel > 0 ? accel * dt_ms / 1000.0 : 1e18;
            double dvx = want[0] - lag_v[0], dvz = want[2] - lag_v[2];
            const double len = std::sqrt(dvx * dvx + dvz * dvz);
            if (len > step) { dvx *= step / len; dvz *= step / len; }
            lag_v[0] += dvx;
            lag_v[2] += dvz;
            for (int i = 0; i < 3; ++i) body[i] += lag_v[i] * dt_ms / 1000.0;
            if (std::sqrt(lag_v[0] * lag_v[0] + lag_v[2] * lag_v[2]) > 100.0) body_yaw = std::atan2(lag_v[0], lag_v[2]);
        } else {
            for (int i = 0; i < 3; ++i) body[i] += (follow ? rv[i] : body_v[i]) * dt_ms / 1000.0;
        }
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
        if (out.events & pr::Event::runaway) ++runaways;
        if (out.events & pr::Event::input_pin) ++input_pins;
        if (out.stance_fix != pr::StanceFix::none) {
            ++fixes;
            last_fix = out.stance_fix;
            if (apply_fix) body_stance = out.stance_fix == pr::StanceFix::hold ? 3 : (body_stance == 2 ? 1 : 2);
        }
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
            if (nudge_sticks) {
                body[0] = out.pos[0];
                body[2] = out.pos[2];
            }
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
        case pr::Action::face:
            ++faces;
            for (int i = 0; i < 3; ++i) body[i] = out.pos[i];
            body_yaw = face_sticks ? out.yaw : face_back_yaw;
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
        Check(out.action == pr::Action::place && out.arm == params.neutral_place && out.neutral == params.neutral_place,
              "first tick places (window disarmed, or neutral with native placements)");
        Tick();
        Check(out.mode == pr::Mode::track && out.arm && !out.neutral && (out.events & pr::Event::placed_ok), "verified: armed and tracking");
        ResetCounters();
    }
    void ResetCounters() {
        places = snaps = nudges = pins = yaws = runaways = fixes = input_pins = faces = 0;
        events = 0;
        max_nudge_step = 0;
        for (auto& c : place_reason) c = 0;
    }
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
    // The remote starts moving and the body's own motion follows it again:
    // the runaway ends once both velocities match (not merely because the
    // remote moves), within ~1 s.
    w.ResetCounters();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.follow = true;
    w.Run(200);
    Check(w.out.mode == pr::Mode::pin && !(w.events & pr::Event::runaway_end), "a moving remote alone does not end the runaway");
    w.Run(1300);
    Check((w.events & pr::Event::runaway_end) && w.out.mode != pr::Mode::pin, "runaway ends when the body's motion matches the remote's");
    Check(w.snaps == 0 && w.places == 0, "no snaps or warps through the runaway");
    // runaway=off only logs, once per episode.
    World o;
    o.params.runaway = mgscoop::PartnerRunaway::off;
    o.Place();
    o.follow = false;
    o.body_v[2] = 3200.0;
    o.Run(5000);
    Check((o.events & pr::Event::runaway) && o.out.mode != pr::Mode::pin && !o.out.report.runaway_active, "runaway=off: logged, not pinned");
    Check(o.runaways == 1, "runaway=off: one event per episode (latched)");
}

// The stuck body with a moving remote (the case the idle-only rule missed):
// the body's own velocity differs from the remote's while the error grows.
// Expect a runaway pin (remote=moving), no snap cycle and no diverging Warp.
void TestMovingRunaway() {
    World w;
    w.Place();
    w.remote_v[0] = -3200.0;  // the guest runs -x
    w.remote_yaw = -kPi / 2;
    w.r = 1.0;
    w.follow = false;
    w.body_v[2] = 3200.0;     // the body keeps running +z on its own
    double worst = 0;
    bool pinned = false;
    w.Run(10000, [&](World& v) {
        if (v.out.report.runaway_active) pinned = true;
        if (pinned && v.Err() > worst) worst = v.Err();
    });
    Check(w.runaways == 1 && w.out.report.runaway_moving && w.out.mode == pr::Mode::pin, "moving runaway: detected once, pinned");
    Check(w.snaps == 0 && w.places == 0 && !(w.events & pr::Event::diverging), "no snap cycle, no diverging Warp");
    Check(worst <= 150.0, "pinned error <= 150 mm (one tick of the relative motion)");
    // The remote turns into the body's own direction: both motions match, the runaway ends.
    w.ResetCounters();
    w.remote_v[0] = 0;
    w.remote_v[2] = 3200.0;
    w.remote_yaw = 0;
    w.Run(2000);
    Check((w.events & pr::Event::runaway_end) && w.out.mode == pr::Mode::track, "runaway ends when the motions match");
    // A body drifting sideways at 2 m/s while running: a runaway too (not a snap).
    World s;
    s.Place();
    s.remote_v[2] = 3200.0;
    s.r = 1.0;
    s.Run(1000);
    s.follow = false;
    s.body_v[2] = 3200.0;
    s.body_v[0] = 2000.0;
    s.ResetCounters();
    s.Run(3000);
    Check(s.runaways == 1 && s.snaps == 0 && s.places == 0, "sideways drift: runaway pin, no snap");
}

// The lag model: the remote's own locomotion accelerates and turns at
// 10 m/s^2; the body takes the remote's velocity of 120 ms ago (the input
// latency) and accelerates at 10 m/s^2 (the same locomotion) or only at
// 8 m/s^2 (a pessimistic mismatch). Starts, a 90-degree turn, a sprint and
// stops never trip the runaway or a snap.
void RunLagModel(double body_accel, std::uint32_t settle_ms, const char* what_moving, const char* what_stopped) {
    World w;
    w.remote_accel = 10000.0;
    w.lag_ms = 120;
    w.accel = body_accel;
    w.Place();
    double worst_moving = 0, worst_stopped = 0;
    const auto leg = [&](double vx, double vz, double r, std::uint32_t ms, bool moving) {
        w.remote_v[0] = vx;
        w.remote_v[2] = vz;
        if (vx != 0.0 || vz != 0.0) w.remote_yaw = std::atan2(vx, vz);
        w.r = r;
        const std::uint64_t start = w.now;
        w.Run(ms, [&](World& v) {
            if (moving && v.now - start > 1500 && v.Err() > worst_moving) worst_moving = v.Err();
            if (!moving && v.now - start > settle_ms && v.Err() > worst_stopped) worst_stopped = v.Err();
        });
    };
    leg(0, 1600, 0.5, 5000, true);    // walk
    leg(0, 3200, 1.0, 5000, true);    // run
    leg(3200, 0, 1.0, 3000, true);    // a 90-degree turn
    leg(6000, 0, 1.0, 3000, true);    // sprint
    leg(0, 0, 0, 5000, false);        // stop
    leg(-3200, 0, 1.0, 3000, true);   // start the other way
    leg(0, 0, 0, 5000, false);        // stop
    Check(w.runaways == 0 && w.snaps == 0 && w.places == 0 && w.pins == 0, "lag model: no runaway, snap, warp or pin");
    Check(worst_moving <= 600.0, what_moving);
    Check(worst_stopped <= 300.0, what_stopped);
}
void TestLagModel() {
    RunLagModel(10000.0, 2000, "same locomotion, 120 ms latency: error <= 600 mm while moving",
                "same locomotion: error <= 300 mm 2 s after stopping");
    RunLagModel(8000.0, 3000, "20 % slower acceleration: error <= 600 mm while moving",
                "20 % slower acceleration: error <= 300 mm 3 s after a sprint stop");
}

// Nudges the game ignores (position writes that do not stick): no fake
// own-motion runaway and no snap for a 1 m idle error.
void TestNonStickingNudge() {
    World w;
    w.Place();
    w.Run(800);
    w.nudge_sticks = false;
    w.body[0] += 1000.0;
    w.ResetCounters();
    w.Run(5000);
    Check(w.nudges > 0 && w.runaways == 0 && w.pins == 0 && w.snaps == 0 && w.places == 0, "non-sticking nudges: no runaway, pin or snap");
}

// Net input stale while the pose stays live: pinned to the pose (no 10 m Warp
// cycle with a neutral pad), then tracking resumes once the input is live.
void TestInputPin() {
    World w;
    w.Place();
    w.remote_v[2] = 6000.0;   // the guest sprints
    w.r = 1.0;
    w.Run(1000);
    w.in_valid = false;       // input frames stop, pose frames continue
    w.follow = false;         // the window injects a neutral pad: the body stands
    w.body_v[2] = 0;
    w.ResetCounters();
    double worst = 0;
    w.Run(8000, [&](World& v) { if (v.out.report.input_pin && v.Err() > worst) worst = v.Err(); });
    Check(w.input_pins == 1 && w.out.mode == pr::Mode::pin && w.out.report.input_pin, "input stale: pinned to the live pose");
    Check(w.places == 0 && w.place_reason[static_cast<unsigned>(pr::PlaceReason::far_error)] == 0, "no far Warp cycle");
    Check(worst <= 150.0, "pinned error <= 150 mm");
    w.in_valid = true;
    w.follow = true;
    w.ResetCounters();
    w.Run(2000);
    Check((w.events & pr::Event::input_pin_end) && w.out.mode == pr::Mode::track && w.places == 0, "input live: tracking resumes, no Warp");
    Check(w.Err() < 300.0, "still on the pose");
}

// The stance level: a lasting mismatch asks for one corrective tap toward
// the remote's stance (a hold for prone), then waits for the result.
void TestStanceFix() {
    World w;
    w.Place();
    w.Run(500);
    w.remote_stance = 2;      // the edge was lost: the body stays standing
    w.ResetCounters();
    w.Run(1900);
    Check(w.fixes == 0, "no fix before 2 s of mismatch");
    w.Run(600);
    Check(w.fixes == 1 && w.last_fix == pr::StanceFix::tap && (w.events & pr::Event::stance_fix), "one corrective tap");
    w.Run(1500);
    Check(w.fixes == 1, "waits for the result (gap)");
    w.Run(1000);
    Check(w.fixes == 2, "retries every 2 s while the body never follows");
    // A body that follows the fix: exactly one.
    World f;
    f.apply_fix = true;
    f.Place();
    f.Run(500);
    f.remote_stance = 2;
    f.ResetCounters();
    f.Run(6000);
    Check(f.fixes == 1 && f.body_stance == 2, "fixed once, stance matches");
    // Prone: a hold.
    f.remote_stance = 3;
    f.ResetCounters();
    f.Run(6000);
    Check(f.fixes == 1 && f.last_fix == pr::StanceFix::hold && f.body_stance == 3, "prone: one hold");
    // Stale input or a transition act: no fix.
    World s;
    s.Place();
    s.Run(500);
    s.remote_stance = 2;
    s.in_valid = false;
    s.ResetCounters();
    s.Run(4000);
    Check(s.fixes == 0, "stale input: no stance fix");
    World a;
    a.Place();
    a.Run(500);
    a.remote_stance = 2;
    a.body_act = 0x1f;
    a.ResetCounters();
    a.Run(4000);
    Check(a.fixes == 0, "transition act: no stance fix");
    World o;
    o.params.stance_fix = false;
    o.Place();
    o.Run(500);
    o.remote_stance = 2;
    o.Run(4000);
    Check(o.fixes == 0, "stance_fix=false: none");
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

// 4 m divergence while moving without a velocity difference (the body was
// pushed or held back by collision in two steps; its own motion still follows
// the input): exactly one hard snap.
void TestMovingSnap() {
    World w;
    w.Place();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.Run(1000);
    w.ResetCounters();
    w.body[0] += 2000.0;
    w.Run(100);
    w.body[0] += 2000.0;
    w.Run(3000);
    Check(w.snaps == 1, "exactly one snap");
    Check(w.places == 0 && w.runaways == 0 && !(w.events & pr::Event::diverging), "no warp, no runaway, no diverging");
    Check(w.Err() < 400.0, "converged afterwards");
}

// Repeated snaps (diverging: the body keeps being pushed 4 m off): Warp then
// pin for a while. The snaps that caused the Warp stay in the counters.
void TestDiverging() {
    World w;
    w.Place();
    w.remote_v[2] = 3200.0;
    w.r = 1.0;
    w.Run(1000);
    w.ResetCounters();
    for (int i = 0; i < 5; ++i) {
        w.body[0] += 2000.0;
        w.Run(100);
        w.body[0] += 2000.0;
        w.Run(1700);
    }
    Check((w.events & pr::Event::diverging) != 0, "diverging after repeated snaps");
    Check(w.place_reason[static_cast<unsigned>(pr::PlaceReason::diverging)] >= 1 && w.pins > 0, "Warp, then pinned");
    Check(w.snaps <= 6, "snaps bounded (no 1 Hz sawtooth)");
    Check(w.out.report.snaps >= 3 && w.out.report.places >= 2, "cumulative counters: the snaps and the re-placement stay counted");
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

// Native actions (NativeParams): the split moving runaway, lag, the
// along-track nudge cap and the stop grace (hidden runs a7823/63199: the body
// sprinted at 0.835 of the remote's speed, the old moving runaway pinned it
// and the arbiter left with "runaway").
void TestNativeActions() {
    const pr::Params native = pr::NativeParams(pr::Params{});
    Check(native.runaway_split && native.runaway_rel_pct == 20 && native.cap_along_pct == 15 && native.cap_along_min_mmps == 300 &&
          native.cap_along_max_mmps == 1500 && native.cap_along_prone_min_mmps == 150 && native.cap_along_prone_max_mmps == 400 &&
          native.stop_grace_ms == 1200, "native preset: split runaway, along cap, stop grace");
    Check(!pr::Params{}.runaway_split && pr::Params{}.cap_along_pct == 0 && pr::Params{}.stop_grace_ms == 0, "round-4 defaults unchanged");
    // Sprint at 0.835 of the remote's 10 m/s: the old rule pins (runaway),
    // the split rule only nudges along the track.
    const auto sprint = [&](bool split) {
        World w;
        w.params = native;
        if (!split) { w.params.runaway_split = false; w.params.cap_along_pct = 0; }
        w.Place();
        w.remote_v[2] = 10000.0;
        w.r = 1.0;
        w.follow = false;
        w.body_v[2] = 8350.0;
        double worst = 0;
        w.Run(5000, [&](World& v) { if (v.Err() > worst) worst = v.Err(); });
        return std::make_pair(w, worst);
    };
    {
        auto [w, worst] = sprint(false);
        Check(w.runaways == 1 && w.out.report.runaway_moving, "old rule: a slower sprinting body is a moving runaway (the run's leave)");
    }
    {
        auto [w, worst] = sprint(true);
        Check(w.runaways == 0 && w.pins == 0 && w.snaps == 0 && w.places == 0, "split: a slower body never pins or snaps");
        Check(worst < 3000.0 && w.nudges > 0, "split: along-track nudges keep the error under the snap threshold");
        Check(w.max_nudge_step > 300.0 * 0.066 && w.max_nudge_step <= 1500.0 * 0.2 + 1.0, "along nudges exceed the 300 mm/s moving cap, <= 1500 mm/s");
        Check(w.out.report.dv_along_mmps < -1000.0 && w.out.report.dv_cross_mmps < 200.0 && !w.out.report.lag,
              "dv split: along -1.65 m/s (inside thr 2 m/s: no lag), across ~0");
    }
    // A much slower body: lag (no pin), lag_end once the speeds match.
    {
        World w;
        w.params = native;
        w.Place();
        w.remote_v[2] = 10000.0;
        w.r = 1.0;
        w.follow = false;
        w.body_v[2] = 7000.0;
        w.Run(1500);
        Check((w.events & pr::Event::lag) && w.out.report.lag && w.runaways == 0 && w.pins == 0, "lag: raised, never pinned");
        w.ResetCounters();
        w.body_v[2] = 10000.0;
        w.Run(1500);
        Check((w.events & pr::Event::lag_end) && !w.out.report.lag, "lag ends when the body keeps up");
    }
    // An overrun (faster than the remote along its motion) still pins, as a moving runaway.
    {
        World w;
        w.params = native;
        w.Place();
        w.remote_v[2] = 3200.0;
        w.r = 1.0;
        w.follow = false;
        w.body_v[2] = 6400.0;
        w.Run(4000);
        Check(w.runaways == 1 && w.out.report.runaway_active && w.out.report.runaway_moving && !w.out.report.runaway_idle,
              "overrun: moving runaway pin, not the idle kind the arbiter times");
    }
    // Stop grace: the remote releases the stick after a sprint; the body's
    // dash-stop slide decelerates over 1.8 s. Without the grace the idle
    // runaway fires (and the arbiter would time it); with it, nothing.
    const auto slide = [&](std::uint32_t grace_ms) {
        World w;
        w.params = native;
        w.params.stop_grace_ms = grace_ms;
        w.Place();
        w.remote_v[2] = 6000.0;
        w.r = 1.0;
        w.Run(1500);
        w.ResetCounters();
        w.remote_v[2] = 0.0;
        w.r = 0.0;
        w.follow = false;
        w.body_v[2] = 6000.0;
        const std::uint64_t start = w.now;
        bool idle_pin = false;
        w.Run(3000, [&](World& v) {
            const double t = static_cast<double>(v.now - start);
            v.body_v[2] = t >= 1800.0 ? 0.0 : 6000.0 * (1.0 - t / 1800.0);
            if (v.out.report.runaway_idle) idle_pin = true;
        });
        return std::make_pair(w.runaways, idle_pin);
    };
    const auto no_grace = slide(0);
    const auto with_grace = slide(1200);
    Check(no_grace.first == 1 && no_grace.second, "no grace: the slide is an idle runaway (runaway_idle)");
    Check(with_grace.first == 0 && !with_grace.second, "stop grace: the decelerating slide is not a runaway");
    // The grace never hides a body that speeds up again after the release.
    {
        World w;
        w.params = native;
        w.Place();
        w.remote_v[2] = 6000.0;
        w.r = 1.0;
        w.Run(1500);
        w.remote_v[2] = 0.0;
        w.r = 0.0;
        w.follow = false;
        w.body_v[2] = 3000.0;
        w.Run(200);
        w.ResetCounters();
        w.body_v[2] = 6000.0;  // accelerates on its own
        w.Run(2000);
        Check(w.runaways == 1 && w.out.report.runaway_idle, "a body accelerating after the release is still an idle runaway");
    }
}

// Facing governor (docs/native-puppet.md "Facing"; hidden runs a7823, 63199,
// bec857fb, d91fc, b9528): idle turns of a quiet body at a bounded eased
// rate, hysteresis, the stick abort, the contest backoff and latch, the
// settle snap, aim turns (FaceMode::aim only), the placement yaw check and
// the windowed teleport.
double Deg(double rad) { return rad * 180.0 / kPi; }
void TestFacingGovernor() {
    const pr::Params native = pr::NativeParams(pr::Params{});
    Check(native.face == pr::FaceMode::idle && native.neutral_place && native.place_yaw_cdeg == 1000 &&
              native.teleport_window_mm == 3000 && native.teleport_window_ms == 200,
          "native preset: governor idle, neutral placements, 10 degree placement yaw, 3 m / 200 ms teleport");
    Check(pr::Params{}.face == pr::FaceMode::legacy && !pr::Params{}.neutral_place && pr::Params{}.place_yaw_cdeg == 0 &&
              pr::Params{}.teleport_window_mm == 0, "round-4 defaults unchanged (legacy yaw rule)");
    Check(pr::NativeParams(pr::Params{}, pr::FaceMode::aim).face == pr::FaceMode::aim, "native preset takes the configured face mode");
    // Idle turn: the remote turned 40 degrees in place (a debug placement).
    {
        World w;
        w.params = native;
        w.Place();
        w.Run(2000);  // past the placement's settle window
        w.ResetCounters();
        w.remote_yaw = 40.0 * kPi / 180.0;
        double prev = w.body_yaw, max_step = 0;
        unsigned epochs = 0, writes = 0;
        w.Run(1500, [&](World& v) {
            if (v.out.action == pr::Action::face) {
                ++writes;
                if (v.out.face_epoch) ++epochs;
                const double step = std::fabs(pr::WrapRad(v.body_yaw - prev));
                if (step > max_step) max_step = step;
            }
            prev = v.body_yaw;
        });
        Check((w.events & pr::Event::face_start) && (w.events & pr::Event::face_end), "idle: one turn started and ended");
        Check(std::fabs(Deg(pr::WrapRad(w.remote_yaw - w.body_yaw))) < 1.0, "idle: the body ends within 1 degree of the remote's yaw");
        Check(Deg(max_step) <= 180.0 * 0.016 + 0.01, "idle: no step faster than 180 deg/s at 16 ms ticks");
        Check(writes >= 10 && epochs == 2, "idle: a gradual turn whose start and end alone bump the body epoch");
        Check(w.snaps == 0 && w.places == 0 && w.yaws == 0, "idle: no snaps, placements or legacy yaw writes");
        Check(w.out.report.face_turns == 1 && w.out.report.face_contests == 0, "idle: report counts one turn, no contest");
    }
    // Hysteresis: 4 degrees never starts a turn; 6 degrees held 100 ms neither.
    {
        World w;
        w.params = native;
        w.Place();
        w.Run(2000);  // past the placement's settle window
        w.ResetCounters();
        w.remote_yaw = 4.0 * kPi / 180.0;
        w.Run(2000);
        Check(w.faces == 0 && std::string(w.out.report.face_reason) == "small", "4 degrees: below the 5 degree entry");
        w.remote_yaw = 6.0 * kPi / 180.0;
        w.Run(100);
        w.remote_yaw = 0.0;
        w.Run(1000);
        Check(w.faces == 0, "6 degrees for 100 ms: under the 200 ms hold");
    }
    // Any stick input aborts in the same tick; locomotion is never fought.
    {
        World w;
        w.params = native;
        w.Place();
        w.Run(2000);  // past the placement's settle window
        w.ResetCounters();
        w.remote_yaw = 90.0 * kPi / 180.0;
        w.Run(260);
        Check((w.events & pr::Event::face_start) && w.faces > 0, "a 90 degree turn is under way");
        const unsigned faces = w.faces;
        w.r = 0.5;
        w.Tick();
        Check(w.out.action != pr::Action::face && (w.out.events & pr::Event::face_end) &&
                  std::string(w.out.report.face_reason) == "stick", "the stick aborts the turn in the same tick");
        w.remote_v[2] = 3000.0;
        w.Run(1000);
        Check(w.faces == faces, "no governor write while the remote moves");
    }
    // Contest: the game rotates the body back after each write.
    {
        World w;
        w.params = native;
        w.Place();
        w.Run(2000);  // past the placement's settle window
        w.ResetCounters();
        w.face_sticks = false;
        w.face_back_yaw = 0.0;
        w.remote_yaw = 30.0 * kPi / 180.0;
        std::uint64_t first_contest = 0, latched_at = 0;
        w.Run(6000, [&](World& v) {
            if ((v.out.events & pr::Event::face_contest) && !first_contest) first_contest = v.now;
            if ((v.out.events & pr::Event::face_latched) && !latched_at) latched_at = v.now;
        });
        Check(first_contest != 0 && w.out.report.face_contests == 3 && latched_at != 0, "three contests latch the governor");
        Check(w.out.report.face_state == pr::FaceState::contested, "latched: face=contested");
        const unsigned faces = w.faces;
        w.Run(5000);
        Check(w.faces == faces && w.out.report.face_state == pr::FaceState::contested, "no writes while latched (10 s)");
        w.face_sticks = true;
        w.Run(2500);
        Check(w.faces > faces && std::fabs(Deg(pr::WrapRad(w.remote_yaw - w.body_yaw))) < 1.0, "after the latch the turn completes");
    }
    // Settle: a body in fall/land (act 25/26) with a large error gets one snap.
    {
        World w;
        w.params = native;
        w.Place();
        w.Run(2000);
        w.ResetCounters();
        w.body_act = 26;
        w.remote_yaw = 60.0 * kPi / 180.0;
        w.Run(1500);
        Check(w.faces == 1 && (w.events & pr::Event::face_snap) && std::fabs(Deg(pr::WrapRad(w.remote_yaw - w.body_yaw))) < 0.01,
              "fall/land: exactly one settle snap onto the target");
        w.remote_yaw = 100.0 * kPi / 180.0;
        w.Run(1500);
        Check(w.faces == 1, "fall/land: one snap per settle window");
        w.body_act = 1;
        w.Run(1500);
        Check(w.faces > 1 && std::fabs(Deg(pr::WrapRad(w.remote_yaw - w.body_yaw))) < 1.0, "back in an idle act: an idle turn finishes it");
    }
    // Aim: only with FaceMode::aim, and only while the body's own turning is slow.
    {
        for (const pr::FaceMode mode : {pr::FaceMode::idle, pr::FaceMode::aim}) {
            World w;
            w.params = pr::NativeParams(pr::Params{}, mode);
            w.Place();
            w.Run(2000);
            w.ResetCounters();
            w.body_act = 12;
            w.remote_act = 11;
            w.remote_yaw = 30.0 * kPi / 180.0;
            w.Run(3000);
            if (mode == pr::FaceMode::idle) Check(w.faces == 0, "face=idle: no aim turn");
            else Check(w.faces > 5 && (w.events & pr::Event::face_start) && std::fabs(Deg(pr::WrapRad(w.remote_yaw - w.body_yaw))) < 1.0,
                       "face=aim: a quiet aim turn reaches the remote's yaw");
        }
    }
    // Placement yaw: a Warp whose yaw does not hold is fixed by one xform, then verified.
    {
        World w;
        w.params = native;
        w.remote_yaw = -60.0 * kPi / 180.0;
        w.Tick();
        Check(w.out.action == pr::Action::place && w.out.neutral && w.out.arm, "native placement keeps a neutral window");
        w.body_yaw = -97.0 * kPi / 180.0;  // the held aim kept turning the body
        w.Tick();
        Check(w.out.action == pr::Action::face && w.out.face_epoch && (w.out.events & pr::Event::place_yaw_fix) &&
                  w.out.mode == pr::Mode::verify && w.out.neutral, "verify: one xform at the placed yaw, still neutral");
        w.Tick();
        Check(w.out.mode == pr::Mode::track && (w.out.events & pr::Event::placed_ok) && w.out.arm && !w.out.neutral &&
                  std::fabs(w.out.report.place_yaw_err_mrad) < 1.0, "verified with the yaw: armed and tracking");
    }
    // Windowed teleport: 8 m over four ticks (each below the 5 m jump) is placed at once.
    {
        World w;
        w.params = native;
        w.Place();
        w.Run(1000);
        w.ResetCounters();
        for (int i = 0; i < 4; ++i) {
            w.remote[0] += 2000.0;
            w.Tick(33);
            if (w.places) break;
        }
        Check(w.places == 1 && w.place_reason[static_cast<unsigned>(pr::PlaceReason::teleport)] == 1,
              "3 m within 200 ms without locomotion: place(teleport)");
        World r;
        r.params = pr::Params{};
        r.Place();
        r.Run(1000);
        r.ResetCounters();
        for (int i = 0; i < 4; ++i) { r.remote[0] += 2000.0; r.Tick(33); }
        Check(r.places == 0, "round-4 params: the windowed teleport is off");
        // A sprint (10 m/s) never trips it.
        World s;
        s.params = native;
        s.Place();
        s.remote_v[2] = 10000.0;
        s.r = 1.0;
        s.Run(3000);
        Check(s.place_reason[static_cast<unsigned>(pr::PlaceReason::teleport)] == 0, "a sprint is not a teleport");
    }
    // FaceMode::off: no facing correction at all (and no legacy rule).
    {
        World w;
        w.params = pr::NativeParams(pr::Params{}, pr::FaceMode::off);
        w.Place();
        w.Run(1000);
        w.ResetCounters();
        w.remote_yaw = 60.0 * kPi / 180.0;
        w.Run(4000);
        Check(w.faces == 0 && w.yaws == 0, "face=off: no correction");
    }
}

} // namespace

int main() {
    TestHelpers();
    TestPlacement();
    TestPlacementFailure();
    TestRunawayReplay();
    TestMovingRunaway();
    TestLagModel();
    TestNonStickingNudge();
    TestInputPin();
    TestStanceFix();
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
    TestNativeActions();
    TestFacingGovernor();
    std::printf("%u partner recon checks passed: placement/verify/retry, I6 runaway replay (pin, no sawtooth), moving runaway, "
                "lag model (walk/run/turn/sprint/stops), non-sticking nudges, input pin, stance fix, aligned run and turn, "
                "lateral nudges, one moving snap, diverging, teleport/far, stance suspend/prone/mismatch, frozen acts, loss/relink, "
                "idle yaw and snap, rearm/manual, recon pin/off, stop check, native actions (split runaway, lag, along cap, "
                "stop grace), facing governor (idle turn, hysteresis, stick abort, contest/latch, settle, aim, placement yaw, windowed "
                "teleport, off)\n", checks);
    return 0;
}
