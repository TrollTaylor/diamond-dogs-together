// dm=map: the direct-motion clip mapper (partner_dmmap) on deterministic
// timelines. A tiny model stands in for the game: a play starts the clip on
// the next tick (q18[1] == 0x8b) unless `consume` is off, a stop ends it on
// the next tick unless `stops` is off, and an unconsumed play stays pending
// for 2 s (the native watch). Every action the module returns is applied
// before the next tick, exactly as the driver does.
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "mgscoop/partner_dmmap.hpp"
#include "mgscoop/partner_facing.hpp"
#include "mgscoop/pose_interp.hpp"

namespace {
namespace pdm = mgscoop::partner_dmmap;
using mgscoop::kPartnerDmTableDefault;
using mgscoop::kPartnerDmSpeedsDefault;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

pdm::Class Cls(std::uint8_t stance, std::uint8_t move) { return pdm::Class{stance, move, pdm::Via::flags, pdm::Via::flags}; }

struct Game {
    pdm::State s{};
    pdm::In in{};
    std::uint64_t now = 10000;
    bool consume = true;          // a play starts the clip on the next tick
    bool stops = true;            // a stop ends the clip on the next tick
    std::uint64_t pending_ms = 0; // an unconsumed play is pending (native watch: 2 s)
    pdm::Class cls = Cls(0, 0);
    std::vector<pdm::Out> outs;
    unsigned plays = 0, stop_actions = 0;
    std::uint8_t last_play = pdm::kNoGani;

    Game() {
        pdm::Reset(s);
        Defaults();
    }
    void Defaults() {
        in = pdm::In{};
        in.map = true;
        in.blocked = pdm::Block::none;
        in.target_ok = true;
        in.dm_ok = true;
        in.patched = true;
        in.parts_loaded = true;
        in.cam_idle = true;
    }
    // One tick with the class `cls` resolved through the default table (bad mask applied).
    pdm::Out Tick(std::uint64_t dt = 33) {
        now += dt;
        if (pending_ms && now - pending_ms >= 2000) { pending_ms = 0; in.our_pending = false; }
        in.now_ms = now;
        in.cls = cls;
        in.target = pdm::ResolveTarget(kPartnerDmTableDefault, cls, s.bad);
        const pdm::Out o = pdm::Step(s, in);
        if (o.action == pdm::Action::play) {
            ++plays;
            last_play = o.gani;
            if (consume) {
                in.in_dm = true;
            } else {
                in.our_pending = true;
                pending_ms = now;
            }
        } else if (o.action == pdm::Action::stop) {
            ++stop_actions;
            if (stops) in.in_dm = false;
        }
        outs.push_back(o);
        return o;
    }
    // Ticks for `ms`; returns the number of outs with `event` (0 for none).
    unsigned Run(std::uint64_t ms, pdm::Event event = pdm::Event::none) {
        unsigned n = 0;
        const std::uint64_t end = now + ms;
        while (now < end) if (Tick().event == event && event != pdm::Event::none) ++n;
        return n;
    }
    // Ticks until a play of `gani` went out (at most `ms`).
    bool RunUntilPlay(std::uint8_t gani, std::uint64_t ms = 3000) {
        const std::uint64_t end = now + ms;
        while (now < end) {
            const pdm::Out o = Tick();
            if (o.action == pdm::Action::play && o.gani == gani) return true;
        }
        return false;
    }
    std::size_t Events(pdm::Event event) const {
        std::size_t n = 0;
        for (const auto& o : outs) if (o.event == event) ++n;
        return n;
    }
    // Playing `gani` (confirmed).
    void Playing(std::uint8_t gani) {
        Check(RunUntilPlay(gani), "harness: play went out");
        Check(Tick().event == pdm::Event::playing && s.phase == pdm::Phase::playing && s.cur == gani, "harness: playing confirmed");
    }
};

void TestNamesAndReset() {
    Check(std::strcmp(pdm::PhaseName(pdm::Phase::stop_sent), "stop_sent") == 0 && std::strcmp(pdm::PhaseName(pdm::Phase::idle), "idle") == 0 &&
          std::strcmp(pdm::BlockName(pdm::Block::anim), "anim") == 0 && std::strcmp(pdm::CauseName(pdm::Cause::warp), "warp") == 0 &&
          std::strcmp(pdm::ViaName(pdm::Via::speed), "speed") == 0 && std::strcmp(pdm::ViaName(pdm::Via::stale), "stale") == 0, "names");
    pdm::State s{};
    pdm::Reset(s);
    Check(s.phase == pdm::Phase::idle && s.cur == pdm::kNoGani && s.sent == pdm::kNoGani && s.from == pdm::kNoGani &&
          s.cand == pdm::kNoGani && !s.owned && s.bad == 0, "Reset: idle, no gani");
    // Value-initialized (Mission{}) the candidate is gani 0 held since t=0:
    // stand idle would play on the very first tick. After Reset it waits the hold.
    {
        Game g;
        g.s = pdm::State{};
        Check(g.Tick().action == pdm::Action::play, "value-initialized state skips the hold (why the driver calls Reset)");
    }
    {
        Game g;
        Check(g.Tick().action == pdm::Action::none && g.s.cand == 0, "Reset: the first target only starts the hold");
        unsigned before = 0;
        while (g.now < 10000 + 33 + 150) { if (g.Tick().action == pdm::Action::play) ++before; }
        Check(before == 0, "Reset: no play within the hold");
        Check(g.RunUntilPlay(0, 200), "Reset: stand idle plays after the hold");
    }
}

void TestSpeedBands() {
    const auto& sp = kPartnerDmSpeedsDefault;
    const std::uint8_t u = pdm::kMoveUnknown;
    Check(pdm::MoveForSpeed(0, sp, u) == 0 && pdm::MoveForSpeed(299, sp, u) == 0 && pdm::MoveForSpeed(300, sp, u) == 1 &&
          pdm::MoveForSpeed(2599, sp, u) == 1 && pdm::MoveForSpeed(2600, sp, u) == 2 && pdm::MoveForSpeed(5599, sp, u) == 2 &&
          pdm::MoveForSpeed(5600, sp, u) == 3, "bands without hysteresis");
    Check(pdm::MoveForSpeed(2700, sp, 0) == 1, "0 -> 2700 reads walk (dead band above 2600)");
    Check(pdm::MoveForSpeed(2700, sp, 1) == 1 && pdm::MoveForSpeed(2751, sp, 1) == 2, "walk -> run needs 2600 + 150");
    Check(pdm::MoveForSpeed(2500, sp, 2) == 2 && pdm::MoveForSpeed(2449, sp, 2) == 1, "run -> walk needs 2600 - 150");
    Check(pdm::MoveForSpeed(400, sp, 0) == 0 && pdm::MoveForSpeed(451, sp, 0) == 1 && pdm::MoveForSpeed(200, sp, 1) == 1 &&
          pdm::MoveForSpeed(149, sp, 1) == 0, "idle <-> walk hysteresis");
    Check(pdm::MoveForSpeed(5500, sp, 3) == 3 && pdm::MoveForSpeed(5449, sp, 3) == 2 && pdm::MoveForSpeed(9000, sp, 0) == 3, "dash");
    // Orbit (offset 300 cm: 2356 mm/s) reads walk by default, run / dash with the D4 bands.
    Check(pdm::MoveForSpeed(2356, sp, u) == 1 && pdm::MoveForSpeed(2356, mgscoop::PartnerDmSpeeds{300, 1500, 5600}, u) == 2 &&
          pdm::MoveForSpeed(2356, mgscoop::PartnerDmSpeeds{300, 1000, 2000}, u) == 3, "orbit speed against the D4 bands");
}

void TestClassify() {
    const auto& sp = kPartnerDmSpeedsDefault;
    pdm::Observation o{};
    o.fresh = true;
    o.flag_move = pdm::kMoveUnknown;
    pdm::Class c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.stance == 0 && c.move == 0 && c.via == pdm::Via::none && c.stance_via == pdm::Via::flags, "nothing known: stand idle");
    o.stance = 1;
    o.sprint = true;
    c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.stance == 1 && c.move == 3 && c.via == pdm::Via::flags, "sprint flag wins");
    o.sprint = false;
    o.flag_move = 2;
    o.speed_ok = true;
    o.speed_mmps = 100;
    c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.move == 2 && c.via == pdm::Via::flags, "fixed: the local move flag before the speed");
    o.flag_move = 0;
    o.speed_mmps = 4000;
    c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.move == 0 && c.via == pdm::Via::flags, "fixed: move 0 is a flag too (not the anim or speed)");
    o.flag_move = pdm::kMoveUnknown;
    c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.move == 2 && c.via == pdm::Via::speed, "speed band");
    o.speed_ok = false;
    o.anim_move = true;
    o.anim_valid = true;
    o.act = 4;
    o.anim_speed = 2;
    c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.move == 1 && c.via == pdm::Via::anim, "anim: crouch moving at speed 2 is walk");
    o.anim_speed = 3;
    Check(pdm::Classify(o, sp, pdm::kMoveUnknown, 0).move == 2, "anim: speed 3 is run");
    o.act = 3;
    Check(pdm::Classify(o, sp, pdm::kMoveUnknown, 0).move == 0, "anim: odd acts are idle");
    o.act = 0x79;
    Check(pdm::Classify(o, sp, pdm::kMoveUnknown, 0).via == pdm::Via::none, "anim: non-locomotion act ignored");
    o.act = 4;
    o.anim_move = false;
    Check(pdm::Classify(o, sp, pdm::kMoveUnknown, 0).via == pdm::Via::none, "orbit/fixed: the local anim never decides move");
    // Stance: flags, the anim stance only when the flags say stand.
    o.stance = 0;
    o.anim_stance = 3;
    c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.stance == 2 && c.stance_via == pdm::Via::anim, "anim crawl when the flags say stand");
    o.stance = 1;
    c = pdm::Classify(o, sp, pdm::kMoveUnknown, 0);
    Check(c.stance == 1 && c.stance_via == pdm::Via::flags, "flags squat kept");
    o.stance = 7;
    o.anim_valid = false;
    Check(pdm::Classify(o, sp, pdm::kMoveUnknown, 0).stance == 0, "bad stance reads stand");
    o.lost = true;
    c = pdm::Classify(o, sp, 2, 1);
    Check(c.stance == 1 && c.move == 0 && c.via == pdm::Via::lost, "lost: previous stance, idle");
}

void TestResolveTarget() {
    const auto& t = kPartnerDmTableDefault;
    Check(pdm::ResolveTarget(t, Cls(0, 0), 0) == 0 && pdm::ResolveTarget(t, Cls(0, 1), 0) == 10 && pdm::ResolveTarget(t, Cls(0, 2), 0) == 11 &&
          pdm::ResolveTarget(t, Cls(0, 3), 0) == 5, "stand row");
    Check(pdm::ResolveTarget(t, Cls(1, 0), 0) == 1 && pdm::ResolveTarget(t, Cls(1, 1), 0) == 3 && pdm::ResolveTarget(t, Cls(1, 2), 0) == 4 &&
          pdm::ResolveTarget(t, Cls(1, 3), 0) == 4, "squat row");
    Check(pdm::ResolveTarget(t, Cls(2, 0), 0) == 7 && pdm::ResolveTarget(t, Cls(2, 1), 0) == 6 && pdm::ResolveTarget(t, Cls(2, 3), 0) == 6,
          "crawl row (idle = proven p_idl)");
    Check(pdm::ResolveTarget(t, Cls(0, 1), 1u << 10) == 0, "bad cell falls back to the stance's idle");
    Check(pdm::ResolveTarget(t, Cls(0, 1), (1u << 10) | 1u) == pdm::kNoGani, "bad idle too: no clip");
    Check(pdm::ResolveTarget(t, Cls(3, 0), 0) == pdm::kNoGani && pdm::ResolveTarget(t, Cls(0, 4), 0) == pdm::kNoGani, "out of range");
    mgscoop::PartnerDmTable odd = t;
    odd.cells[1] = mgscoop::kPartnerDmGaniMax + 1;
    Check(pdm::ResolveTarget(odd, Cls(0, 1), 0) == 0, "a cell above the GANI bound falls back");
}

void TestSpeedRing() {
    pdm::SpeedRing r{};
    pdm::SpeedReset(r);
    std::uint32_t v = 0;
    Check(!pdm::SpeedMmps(r, v), "empty ring");
    for (int i = 0; i <= 3; ++i) pdm::SpeedPush(r, 1000 + 33u * i, 2356 * 33 * i / 1000, 0);
    Check(!pdm::SpeedMmps(r, v), "span below 100 ms");
    pdm::SpeedPush(r, 1132, 2356 * 132 / 1000, 0);
    Check(pdm::SpeedMmps(r, v) && v >= 2300 && v <= 2400, "four 30 Hz intervals provide the first reliable speed estimate");
    for (int i = 5; i <= 20; ++i) pdm::SpeedPush(r, 1000 + 33u * i, 2356 * 33 * i / 1000, 0);
    Check(pdm::SpeedMmps(r, v) && v >= 2300 && v <= 2400, "steady 2356 mm/s");
    pdm::SpeedPush(r, 1000 + 33u * 20, 2356 * 33 * 20 / 1000 + 5000, 0);
    Check(r.count == 21 && pdm::SpeedMmps(r, v) && v > 10000, "equal timestamp overwrites the newest");
    pdm::SpeedPush(r, 900, 0, 0);
    Check(r.count == 1 && !pdm::SpeedMmps(r, v), "time backwards restarts");
    for (int i = 1; i <= 10; ++i) pdm::SpeedPush(r, 900 + 33u * i, 0, 0);
    Check(pdm::SpeedMmps(r, v) && v == 0, "standing still");
    pdm::SpeedPush(r, 900 + 33u * 11, 0, 50000);  // 50 m in 33 ms: a teleport
    Check(r.count == 1, "a teleport restarts");
    for (int i = 0; i < 100; ++i) pdm::SpeedPush(r, 5000 + 33u * i, 0, 1000 * 33 * i / 1000);
    Check(r.count == pdm::kSpeedRing && pdm::SpeedMmps(r, v) && v >= 980 && v <= 1020, "the ring wraps; only the newest 200 ms count");
    for (const unsigned hz : {20u, 60u, 120u, 240u, 500u}) {
        pdm::SpeedReset(r);
        for (unsigned frame = 0; frame <= hz * 2; ++frame) {
            const std::uint64_t elapsed = frame * 1000u / hz;
            const auto travel = elapsed <= 1000 ? elapsed : 1000;
            pdm::SpeedPush(r, 10000 + elapsed, static_cast<std::int32_t>(travel * 3), 0);
            Check(r.count <= pdm::kSpeedRing && r.entries[r.head].t_ms == 10000 + elapsed,
                  "time-spaced history stays bounded and retains the latest pose at every owner cadence");
            if (elapsed >= 200 && elapsed <= 1000)
                Check(pdm::SpeedMmps(r, v) && v >= 2990 && v <= 3010,
                      "100..200ms measured speed survives20/60/120/240/500Hz owner cadence");
            if (elapsed >= 1200)
                Check(pdm::SpeedMmps(r, v) && v == 0, "all owner cadences observe a complete stop within the speed window");
        }
    }
    pdm::SpeedReset(r);
    Check(!pdm::SpeedPush(r, 1000, 0, 0) && !pdm::SpeedPush(r, 1002, 6, 0) && r.count == 1,
          "a high-cadence sample updates the latest pose without adding history");
    Check(pdm::SpeedPush(r, 1004, 10000, 0) && r.count == 1 && r.entries[r.head].x_mm == 10000,
          "a teleport resets even on a tick that would have skipped a history insertion");
    Check(pdm::SpeedPush(r, 1003, 10000, 0) && r.count == 1 && r.anchor_ms == 1003,
          "a backwards tick resets the history cadence even between spaced insertions");
}

void TestMeasuredPoseTimeline() {
    // Real interpolation and speed measurement: 30 Hz sender snapshots, a
    // 20..500 Hz owner, 100 ms pose delay, no WALK/RUN flags, and idle raw acts.
    // This tests observed start/stop latency rather than timing constants.
    const auto timeline = [](bool jitter, unsigned profile, unsigned owner_hz) {
        Game g;
        mgscoop::pose::SnapshotRing poses;
        pdm::SpeedRing speeds{};
        const std::uint64_t base = g.now, start = base + 1000, stop = start + 1000;
        std::uint64_t first_move = 0, first_idle_after_stop = 0;
        unsigned sender = 0, moving_plays = 0;
        bool steady_walk = true;
        for (unsigned owner = 1; owner <= owner_hz * 4; ++owner) {
            const std::uint64_t now = base + owner * 1000u / owner_hz;
            for (;;) {
                const std::uint64_t sent = base + sender * 1000u / 30u;
                const unsigned transit[] = {0, 15, 5, 20, 0};
                const std::uint64_t received = sent + 20 + (jitter ? transit[sender % 5] : 0);
                if (received > now) break;
                const auto elapsed = sent <= start ? 0 : (sent >= stop ? 1000 : sent - start);
                const float rate = profile == 2 ? 2.6f : 3.0f;
                const float noise = jitter ? static_cast<float>(static_cast<int>(sender % 5) - 2) * 0.004f : 0.0f;
                const float x = (profile == 1 ? 0.0f : static_cast<float>(elapsed) * rate / 1000.0f) + noise;
                const mgscoop::pose::Snapshot snapshot{static_cast<std::uint32_t>(sent), x, 0, 0, 0, 0, 0, true, 1, 1, 0, 0};
                Check(poses.Push(snapshot, received), "30 Hz source accepts monotonic snapshots with bounded transit jitter");
                ++sender;
            }
            const auto sample = poses.SampleAt(now, 100);
            pdm::Observation observed{};
            observed.flag_move = pdm::kMoveUnknown;
            observed.fresh = sample.valid && sample.status == mgscoop::pose::Status::live;
            observed.lost = !sample.valid || sample.status == mgscoop::pose::Status::lost;
            observed.anim_move = true;
            if (sample.valid) {
                pdm::SpeedPush(speeds, now, static_cast<std::int32_t>(std::lround(sample.x * 1000)), 0);
                observed.speed_ok = pdm::SpeedMmps(speeds, observed.speed_mmps);
                observed.stance = sample.stance;
                observed.anim_valid = sample.anim_valid;
                observed.act = sample.act;
                observed.anim_stance = sample.anim_stance;
                observed.anim_speed = sample.anim_speed;
            }
            g.cls = pdm::Classify(observed, kPartnerDmSpeedsDefault,
                                 g.s.cand == pdm::kNoGani ? pdm::kMoveUnknown : g.s.cand_cls.move, g.s.cand_cls.stance);
            g.in.measured = observed.fresh && observed.speed_ok && g.cls.via == pdm::Via::speed;
            g.in.urgent = g.cls.move != 0;
            g.in.target_ok = observed.fresh || observed.lost;
            const auto out = g.Tick(now - g.now);
            if (out.action == pdm::Action::play) {
                if (out.gani != 0) {
                    ++moving_plays;
                    if (!first_move) first_move = now;
                } else if (now >= stop && !first_idle_after_stop) first_idle_after_stop = now;
            }
            if (profile == 2 && now >= start + 400 && now <= stop - 100) steady_walk = steady_walk && g.cls.move == 1;
        }
        if (profile == 1) {
            Check(moving_plays == 0 && g.last_play == 0, "centimetre-scale stationary jitter never starts a walk/run clip");
        } else {
            const std::uint64_t tick_budget = (1000u + owner_hz - 1) / owner_hz;
            Check(first_move >= start + 100 && first_move <= start + 350 + tick_budget,
                  "flagless real interpolated movement starts within350ms plus one owner tick, including100ms pose delay");
            Check(first_idle_after_stop >= stop + 100 && first_idle_after_stop <= stop + 480 + tick_budget && g.last_play == 0,
                  "flagless real interpolated stop returns to idle within480ms plus one owner tick, including100ms pose delay");
            if (profile == 2) Check(steady_walk, "150mmps hysteresis keeps near-boundary measured walking stable under snapshot noise");
            std::printf("measured pose timeline:30Hz/%uHz jitter=%u profile=%u start=%llu ms stop=%llu ms\n",
                        owner_hz, unsigned(jitter), profile, static_cast<unsigned long long>(first_move - start),
                        static_cast<unsigned long long>(first_idle_after_stop - stop));
        }
    };
    for (const unsigned hz : {20u, 60u, 120u, 240u, 500u}) {
        timeline(false, 0, hz);
        timeline(true, 0, hz);
        timeline(true, 1, hz);
        timeline(true, 2, hz);
    }
}

void TestSwitchFlow() {
    Game g;
    g.Playing(0);
    const pdm::Out first = g.outs[g.outs.size() - 2];
    Check(first.event == pdm::Event::step && !first.step_stop && first.from == pdm::kNoGani && first.gani == 0, "first play: step from none");
    g.Run(500);
    // Walk: stop after the hold, the play on the tick the stop is confirmed.
    g.cls = Cls(0, 1);
    unsigned ticks = 0;
    pdm::Out o{};
    do { o = g.Tick(); ++ticks; } while (o.action == pdm::Action::none && ticks < 100);
    Check(o.action == pdm::Action::stop && o.event == pdm::Event::step && o.step_stop && o.from == 0 && o.gani == 10 &&
          g.s.phase == pdm::Phase::stop_sent && ticks >= 7 && ticks <= 9 && o.ms >= 200, "switch: stop after the hold (200 ms)");
    o = g.Tick();
    Check(o.action == pdm::Action::play && o.gani == 10 && o.event == pdm::Event::step && !o.step_stop && o.from == 0 && o.ms == 33 &&
          g.s.phase == pdm::Phase::play_sent, "switch: play 10 once the stop is confirmed (wait_ms)");
    Check(g.Tick().event == pdm::Event::playing && g.s.cur == 10, "switch: 10 playing");
    Check(g.s.switches == 2 && g.s.plays == 2 && g.s.stops == 1, "counters: first play + one switch");
    // Dwell: a change right after the play waits 300 ms of play.
    g.cls = Cls(0, 2);
    unsigned none = 0;
    while (g.Tick().action == pdm::Action::none) ++none;
    Check(none >= 7 && g.outs.back().action == pdm::Action::stop && g.outs.back().gani == 11, "switch: dwell 300 ms before the stop");
    // A flicker shorter than the hold never switches.
    Check(g.RunUntilPlay(11), "run plays");
    g.Tick();
    g.Run(400);
    g.cls = Cls(0, 3);
    g.Run(100);
    g.cls = Cls(0, 2);
    g.Run(1000);
    Check(g.stop_actions == 2 && g.s.cur == 11, "a 100 ms flicker never switches");
}

void TestNaturalEnd() {
    Game g;
    g.cls = Cls(0, 1);
    g.Playing(10);
    g.Run(1000);
    g.in.in_dm = false;  // the clip ends on its own after 1 s: not a loop
    pdm::Out o = g.Tick();
    Check(o.event == pdm::Event::lost && o.cause == pdm::Cause::ended && o.gani == 10 && g.s.strikes[10] == 1 &&
          o.bad_gani == pdm::kNoGani && g.s.phase == pdm::Phase::wait && !g.s.owned, "lost after 1 s: ended, strike 1");
    g.Playing(10);
    g.Run(1000);
    g.in.in_dm = false;
    o = g.Tick();
    Check(o.event == pdm::Event::lost && o.bad_gani == 10 && (g.s.bad & (1u << 10)) && g.s.bad_ganis == 1, "second strike: bad_gani");
    Check(g.RunUntilPlay(0), "the bad walk loop falls back to stand idle");
    g.Tick();
    const unsigned plays = g.plays;
    g.Run(5000);
    Check(g.plays == plays && g.s.cur == 0, "no replay loop: idle keeps looping");
    // A loss after 3 s is no strike, and the replay works.
    Game h;
    h.Playing(0);
    h.Run(3500);
    h.in.in_dm = false;
    o = h.Tick();
    Check(o.event == pdm::Event::lost && o.cause == pdm::Cause::ended && h.s.strikes[0] == 0, "a loss after 3 s is no strike");
    Check(h.RunUntilPlay(0, 100), "late loss: replayed at once (the hold already ran)");
}

void TestLossAttribution() {
    const auto lose = [](void (*setup)(pdm::In&), pdm::Cause cause) {
        Game g;
        g.Playing(0);
        g.Run(500);
        setup(g.in);
        g.in.in_dm = false;
        const pdm::Out o = g.Tick();
        return o.event == pdm::Event::lost && o.cause == cause && g.s.strikes[0] == 0 && o.bad_gani == pdm::kNoGani;
    };
    Check(lose([](pdm::In& in) { in.prev_warp = true; }, pdm::Cause::warp), "prev Warp: cause warp, no strike");
    Check(lose([](pdm::In& in) { in.prev_look = true; }, pdm::Cause::look), "prev look op: cause look, no strike");
    Check(lose([](pdm::In& in) { in.parts_loaded = false; }, pdm::Cause::parts), "parts reloading: cause parts, no strike");
    Check(lose([](pdm::In& in) { in.host_dm = true; }, pdm::Cause::host), "host DM: cause host, no strike");
    Check(lose([](pdm::In& in) { in.prev_look = true; in.parts_loaded = false; }, pdm::Cause::parts), "parts before look");
}

void TestUnconsumed() {
    Game g;
    g.consume = false;
    g.cls = Cls(0, 1);
    Check(g.RunUntilPlay(10), "play 10 (never consumed)");
    const std::uint64_t sent = g.now;
    pdm::Out o{};
    while ((o = g.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::unconsumed && g.now - sent >= 2500 && g.now - sent < 2540 && o.gani == 10 && g.s.failures == 1 &&
          g.s.backoff_ms == 1000 && g.s.strikes[10] == 1 && g.s.phase == pdm::Phase::backoff && !g.s.owned,
          "unconsumed at 2.5 s: strike + failure, backoff 1 s");
    Check(!g.RunUntilPlay(10, 900) && g.RunUntilPlay(10, 300), "retry after the backoff");
    while ((o = g.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::unconsumed && o.bad_gani == 10 && g.s.failures == 2 && g.s.backoff_ms == 2000, "second: bad, backoff 2 s");
    Check(g.RunUntilPlay(0, 2500), "fallback to stand idle");
    while ((o = g.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::unconsumed && g.s.failures == 3 && g.s.backoff_ms == 4000, "third: backoff 4 s");
    Check(g.RunUntilPlay(0, 5000), "idle again");
    while ((o = g.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::unconsumed && o.bad_gani == 0 && g.s.failures == 4 && g.s.backoff_ms == 8000, "idle bad, backoff 8 s");
    // Nothing usable in the stand row: deferred bad_gani, no further plays.
    const unsigned plays = g.plays;
    g.Run(9000);
    Check(g.plays == plays && g.s.deferred && std::strcmp(g.s.deferred, "bad_gani") == 0, "stand row bad: deferred bad_gani");
    g.cls = Cls(1, 1);
    Check(g.RunUntilPlay(3), "squat walk still plays");
    while ((o = g.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::failed && o.reason && std::strcmp(o.reason, "unconsumed") == 0 && g.s.failures == 5 &&
          g.s.phase == pdm::Phase::failed, "five failures across gani: failed");
    g.Run(20000);
    Check(g.plays == plays + 1, "failed: nothing more while map stays on");
    pdm::Rearm(g.s);
    Check(g.s.bad == 0 && g.s.failures == 0 && g.s.strikes[10] == 0 && g.s.phase == pdm::Phase::wait, "Rearm clears bad, failures");
    g.consume = true;
    g.cls = Cls(0, 1);
    Check(g.RunUntilPlay(10), "after Rearm the walk loop plays again");
}

void TestLateClip() {
    Game g;
    g.consume = false;
    Check(g.RunUntilPlay(0), "play");
    pdm::Out o{};
    while ((o = g.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::unconsumed && g.s.phase == pdm::Phase::backoff, "unconsumed");
    g.in.in_dm = true;  // it started late after all
    g.Run(1100);
    Check(g.s.phase == pdm::Phase::playing && g.s.owned && g.s.cur == 0 && g.plays == 1, "R6: the late clip is the one sent");
}

void TestDmReadTimeouts() {
    Game g;
    g.consume = false;
    Check(g.RunUntilPlay(0), "play");
    const std::uint64_t sent = g.now;
    g.in.dm_ok = false;
    pdm::Out o{};
    while ((o = g.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::dm_read && o.reason && std::strcmp(o.reason, "dm_read") == 0 && g.now - sent >= 2500 &&
          g.s.strikes[0] == 0 && g.s.timeouts == 1, "play_sent with the DM unreadable: dm_read failure at 2.5 s, no strike");
    Game h;
    h.Playing(0);
    h.cls = Cls(0, 1);
    while (h.Tick().action != pdm::Action::stop) {}
    const std::uint64_t stop = h.now;
    h.in.dm_ok = false;
    while ((o = h.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::dm_read && h.now - stop >= 1000 && h.now - stop < 1040 && h.s.owned, "stop_sent unreadable: dm_read at 1 s");
    Game k;
    k.Playing(0);
    k.stops = false;
    k.cls = Cls(0, 1);
    while (k.Tick().action != pdm::Action::stop) {}
    while ((o = k.Tick()).event == pdm::Event::none) {}
    Check(o.event == pdm::Event::stop_timeout && o.gani == 0 && k.s.owned && k.s.phase == pdm::Phase::backoff, "stop timeout keeps the clip");
    k.stops = true;
    while (k.Tick().action != pdm::Action::stop) {}
    Check(k.s.phase == pdm::Phase::stop_sent && k.Tick().action == pdm::Action::play && k.last_play == 10, "R6 back to playing, stop again");
}

void TestGates() {
    // A foreign pending request blocks the stop (D, B and L).
    {
        Game g;
        g.Playing(0);
        g.in.pending = true;
        g.cls = Cls(0, 1);
        pdm::Out o{};
        while ((o = g.Tick()).event == pdm::Event::none) {}
        Check(o.event == pdm::Event::deferred && std::strcmp(o.reason, "pending") == 0 && g.Run(1000, pdm::Event::deferred) == 0 &&
              g.stop_actions == 0, "pending: the switch stop waits, deferred once");
        g.in.blocked = pdm::Block::anim;
        g.Run(500);
        Check(g.stop_actions == 0, "pending: no blocked stop either");
        g.in.map = false;
        g.in.blocked = pdm::Block::none;
        g.Run(1400);
        Check(g.stop_actions == 0 && g.s.phase == pdm::Phase::release, "pending: no release stop");
        o = g.Tick(200);
        Check(o.event == pdm::Event::released && std::strcmp(o.reason, "timeout") == 0 && o.ms >= 1500 && g.s.phase == pdm::Phase::idle,
              "pending: release times out");
    }
    // The play gates, each deferred once while it holds.
    const auto gate = [](void (*setup)(pdm::In&), const char* reason) {
        Game g;
        g.Run(150);
        setup(g.in);
        g.Run(1000);
        const bool ok = g.plays == 0 && g.Events(pdm::Event::deferred) == 1 && g.s.deferred && std::strcmp(g.s.deferred, reason) == 0;
        g.Defaults();
        return ok && g.RunUntilPlay(0, 100) && g.s.deferred == nullptr;
    };
    Check(gate([](pdm::In& in) { in.patched = false; }, "not_patched"), "gate not_patched");
    Check(gate([](pdm::In& in) { in.parts_loaded = false; }, "parts"), "gate parts");
    Check(gate([](pdm::In& in) { in.dm_ok = false; }, "dm_read"), "gate dm_read");
    Check(gate([](pdm::In& in) { in.host_dm = true; }, "host_dm"), "gate host_dm");
    Check(gate([](pdm::In& in) { in.pending = true; }, "pending"), "gate pending");
    Check(gate([](pdm::In& in) { in.our_pending = true; }, "pending"), "gate our pending");
    Check(gate([](pdm::In& in) { in.cam_idle = false; }, "cam_connect"), "gate cam_connect");
    Check(gate([](pdm::In& in) { in.look_busy = true; }, "look"), "gate look");
    Check(gate([](pdm::In& in) { in.allactive = true; }, "allactive"), "gate allactive");
    Check(gate([](pdm::In& in) { in.warp = true; }, "warp"), "gate warp");
    // A stale remote never switches.
    Game g;
    g.Playing(0);
    g.in.target_ok = false;
    g.cls = Cls(0, 3);
    g.Run(2000);
    Check(g.stop_actions == 0 && g.s.cand == 0, "stale: frozen, no switch");
    g.in.target_ok = true;
    Check(g.RunUntilPlay(5, 1000), "live again: dash");
}

void TestAdopt() {
    Game g;
    g.in.in_dm = true;  // a manual dmplay still loops
    pdm::Out o = g.Tick();
    Check(o.event == pdm::Event::adopted && g.s.phase == pdm::Phase::playing && g.s.owned && g.s.cur == pdm::kNoGani && g.s.adopted == 1,
          "adopted at once");
    g.cls = Cls(1, 0);
    unsigned ticks = 0;
    while ((o = g.Tick()).action == pdm::Action::none) ++ticks;
    Check(o.action == pdm::Action::stop && o.from == pdm::kNoGani && o.gani == 1 && ticks >= 5 && ticks <= 7, "adopted: stop after the hold");
    Check(g.Tick().action == pdm::Action::play && g.last_play == 1, "adopted: then squat idle");
    // Adopted in wait too (a clip started outside the mapper).
    Game h;
    h.in.patched = false;
    h.Run(300);
    h.in.patched = true;
    h.in.in_dm = true;
    Check(h.Tick().event == pdm::Event::adopted && h.plays == 0, "adopted from wait");
}

void TestBlocked() {
    Game g;
    g.cls = Cls(0, 3);
    g.Playing(5);
    g.in.blocked = pdm::Block::fault;
    Check(g.Tick().event == pdm::Event::deferred, "fault: deferred");
    g.Run(2000);
    Check(g.stop_actions == 0 && g.Events(pdm::Event::deferred) == 1, "fault: no action, deferred once");
    g.in.blocked = pdm::Block::terminating;
    Check(g.Tick().event == pdm::Event::deferred && g.stop_actions == 0, "terminating: no action");
    g.in.blocked = pdm::Block::stage;
    pdm::Out o = g.Tick();
    Check(o.action == pdm::Action::stop && o.step_stop && o.from == 5 && o.gani == pdm::kNoGani, "stage block: the clip is stopped");
    o = g.Tick();
    Check(o.event == pdm::Event::deferred && std::strcmp(o.reason, "stage") == 0 && !g.s.owned && g.s.phase == pdm::Phase::wait,
          "then deferred stage");
    g.Run(1000);
    Check(g.plays == 1, "blocked: no play");
    g.in.blocked = pdm::Block::none;
    Check(g.RunUntilPlay(5, 100), "unblocked: plays the target");
}

void TestRelease() {
    // map -> off while playing: one stop, released stopped.
    {
        Game g;
        g.cls = Cls(0, 3);
        g.Playing(5);
        g.in.map = false;
        pdm::Out o = g.Tick();
        Check(o.action == pdm::Action::stop && o.event == pdm::Event::none && g.s.phase == pdm::Phase::release, "release: one stop");
        o = g.Tick();
        Check(o.event == pdm::Event::released && std::strcmp(o.reason, "stopped") == 0 && o.ms == 33 && g.s.phase == pdm::Phase::idle &&
              !g.s.owned && g.stop_actions == 1, "release: stopped");
        g.Run(500);
        Check(g.stop_actions == 1 && g.plays == 1, "idle: nothing more");
    }
    // While the host is in DM no stop goes out; timeout at 1.5 s.
    {
        Game g;
        g.Playing(0);
        g.in.map = false;
        g.in.host_dm = true;
        pdm::Out o{};
        while ((o = g.Tick()).event == pdm::Event::none) {}
        Check(o.event == pdm::Event::released && std::strcmp(o.reason, "timeout") == 0 && o.ms >= 1500 && o.ms < 1540 && g.stop_actions == 0,
              "release: host DM -> timeout");
    }
    // map -> off during stop_sent: no second stop.
    {
        Game g;
        g.Playing(0);
        g.stops = false;
        g.cls = Cls(0, 1);
        while (g.Tick().action != pdm::Action::stop) {}
        g.in.map = false;
        g.Run(300);
        Check(g.stop_actions == 1 && g.s.phase == pdm::Phase::release && g.s.release_stop_sent, "release from stop_sent: no second stop");
        g.in.in_dm = false;
        const pdm::Out o = g.Tick();
        Check(o.event == pdm::Event::released && std::strcmp(o.reason, "stopped") == 0 && o.ms >= 300, "then released stopped");
    }
    // Not owned: idle at once, no event.
    {
        Game g;
        g.Run(100);
        g.in.map = false;
        Check(g.Tick().event == pdm::Event::none && g.s.phase == pdm::Phase::idle, "release: nothing owned");
    }
    // A clip that ends during the release: ended.
    {
        Game g;
        g.Playing(0);
        g.in.map = false;
        g.in.cam_idle = false;
        Check(g.Tick().action == pdm::Action::none, "release: camera connect holds the stop");
        g.in.in_dm = false;
        const pdm::Out o = g.Tick();
        Check(o.event == pdm::Event::released && std::strcmp(o.reason, "ended") == 0, "release: ended on its own");
    }
    // map on again during the release: the clip is still ours.
    {
        Game g;
        g.Playing(0);
        g.in.map = false;
        g.in.host_dm = true;
        g.Run(300);
        g.in.map = true;
        g.in.host_dm = false;
        g.Tick();
        Check(g.s.phase == pdm::Phase::playing && g.s.owned, "map again: playing (owned)");
        g.cls = Cls(0, 3);
        Check(g.RunUntilPlay(5, 1000), "map again: switches normally");
    }
    // map on again before a play sent just before map off started: that play
    // is kept (R1 confirms it), no lost/adopted, no stop/replay cycle.
    {
        Game g;
        g.consume = false;
        g.cls = Cls(0, 3);
        Check(g.RunUntilPlay(5), "release from play_sent: play 5 (pending)");
        g.in.map = false;
        pdm::Out o = g.Tick();
        Check(o.action == pdm::Action::none && g.s.phase == pdm::Phase::release && g.s.release_from == pdm::Phase::play_sent,
              "release from play_sent: no stop while the play is pending");
        g.in.map = true;
        o = g.Tick();
        Check(o.action == pdm::Action::none && o.event == pdm::Event::none && g.s.phase == pdm::Phase::play_sent && g.s.sent == 5,
              "map again, play still pending: back to play_sent");
        g.in.in_dm = true;  // consumed late
        g.in.our_pending = false;
        g.pending_ms = 0;
        o = g.Tick();
        Check(o.event == pdm::Event::playing && o.gani == 5 && g.s.cur == 5 && g.s.owned && g.s.play_ms == g.now,
              "map again: the late play is confirmed as 5");
        g.Run(1000);
        Check(g.Events(pdm::Event::lost) == 0 && g.Events(pdm::Event::adopted) == 0 && g.plays == 1 && g.stop_actions == 0,
              "map again from play_sent: no lost/adopted, no extra stop/play");
    }
    // The same, consumed during the release while the stop was gated: playing at once.
    {
        Game g;
        g.consume = false;
        g.cls = Cls(1, 0);
        Check(g.RunUntilPlay(1), "release from play_sent (gated stop): play 1");
        g.in.map = false;
        g.Tick();
        g.in.host_dm = true;
        g.in.in_dm = true;
        g.in.our_pending = false;
        g.pending_ms = 0;
        Check(g.Tick().action == pdm::Action::none && g.s.phase == pdm::Phase::release, "consumed during the release, stop gated");
        g.in.map = true;
        g.in.host_dm = false;
        const pdm::Out o = g.Tick();
        Check(o.event == pdm::Event::playing && o.gani == 1 && g.s.phase == pdm::Phase::playing && g.s.cur == 1 && g.s.play_ms == g.now,
              "map again: playing 1 (not adopted as unknown)");
        g.Run(1000);
        Check(g.Events(pdm::Event::lost) == 0 && g.Events(pdm::Event::adopted) == 0 && g.plays == 1 && g.stop_actions == 0,
              "consumed during the release: no stop/replay");
    }
    // The same with the release stop already out: wait for that stop, from = the play.
    {
        Game g;
        g.consume = false;
        g.stops = false;
        g.cls = Cls(1, 0);
        Check(g.RunUntilPlay(1), "release from play_sent (stop out): play 1");
        g.in.map = false;
        g.Tick();
        g.in.in_dm = true;
        g.in.our_pending = false;
        g.pending_ms = 0;
        Check(g.Tick().action == pdm::Action::stop && g.s.release_stop_sent, "consumed during the release: the release stop goes out");
        g.in.map = true;
        g.Tick();
        Check(g.s.phase == pdm::Phase::stop_sent && g.s.from == 1 && g.s.cur == 1 && g.stop_actions == 1, "map again: stop_sent, from 1");
    }
    // A stop held back by the gate until late in the release still gets the
    // stop wait: no timeout (and so no revert) the tick after it went out.
    {
        Game g;
        g.Playing(0);
        g.stops = false;
        g.in.map = false;
        g.in.host_dm = true;
        g.Tick();
        const std::uint64_t start = g.s.release_ms;
        while (g.now - start < 1450) Check(g.Tick().event == pdm::Event::none, "late stop: nothing while the gate holds");
        g.in.host_dm = false;
        pdm::Out o = g.Tick();
        const std::uint64_t stop = g.now;
        Check(o.action == pdm::Action::stop && stop - start < pdm::kReleaseMs, "late stop: sent just before 1.5 s");
        o = g.Tick();
        Check(o.event == pdm::Event::none && g.s.phase == pdm::Phase::release && g.now - start >= pdm::kReleaseMs,
              "late stop: no timeout the tick after (stop still pending)");
        g.Run(300);
        Check(g.s.phase == pdm::Phase::release && g.Events(pdm::Event::released) == 0, "late stop: still waiting for it");
        g.in.in_dm = false;
        o = g.Tick();
        Check(o.event == pdm::Event::released && std::strcmp(o.reason, "stopped") == 0, "late stop: released stopped once consumed");
        Game k;
        k.Playing(0);
        k.stops = false;
        k.in.map = false;
        k.in.host_dm = true;
        k.Tick();
        const std::uint64_t kstart = k.s.release_ms;
        while (k.now - kstart < 1450) k.Tick();
        k.in.host_dm = false;
        Check(k.Tick().action == pdm::Action::stop, "late stop never consumed: sent");
        const std::uint64_t kstop = k.now;
        while ((o = k.Tick()).event == pdm::Event::none) {}
        Check(o.event == pdm::Event::released && std::strcmp(o.reason, "timeout") == 0 && k.now - kstop >= pdm::kStopWaitMs &&
              k.now - kstop < pdm::kStopWaitMs + 40, "late stop never consumed: timeout after the stop wait");
    }
    // Failed with a clip, then map off: released.
    {
        Game g;
        g.Playing(0);
        g.s.phase = pdm::Phase::failed;
        g.Run(300);
        Check(g.stop_actions == 0, "failed: nothing while map stays on");
        g.in.map = false;
        const pdm::Out o = g.Tick();
        Check(o.action == pdm::Action::stop && g.Tick().event == pdm::Event::released, "failed: released on map off");
    }
}

void TestRearmAndForget() {
    Game g;
    g.Playing(0);
    g.s.phase = pdm::Phase::failed;
    g.s.failures = 5;
    pdm::Rearm(g.s);
    Check(g.s.phase == pdm::Phase::playing && g.s.play_ms == 0 && g.s.failures == 0, "Rearm: failed with a clip -> playing");
    g.s.phase = pdm::Phase::backoff;
    g.s.retry_ms = g.now + 5000;
    pdm::Rearm(g.s);
    Check(g.s.retry_ms == 0, "Rearm: backoff retries at once");
    g.s.phase = pdm::Phase::playing;
    pdm::Forget(g.s);
    Check(!g.s.owned && g.s.cur == pdm::kNoGani && g.s.phase == pdm::Phase::wait, "Forget: wait, nothing owned");
    pdm::Idle(g.s);
    Check(g.s.phase == pdm::Phase::idle && g.s.cand == pdm::kNoGani && g.s.plays == 1, "Idle keeps the statistics");
    pdm::State s{};
    pdm::Reset(s);
    pdm::Forget(s);
    Check(s.phase == pdm::Phase::idle, "Forget keeps idle");
    // An unrealize or a patch flicker (Forget) neither leaves failed nor skips a backoff.
    {
        Game f;
        f.Playing(0);
        f.s.phase = pdm::Phase::failed;
        f.s.failures = pdm::kMaxFailures;
        pdm::Forget(f.s);
        Check(f.s.phase == pdm::Phase::failed && !f.s.owned && f.s.cur == pdm::kNoGani, "Forget keeps failed");
        f.in.in_dm = false;
        f.Run(20000);
        Check(f.plays == 1 && f.s.phase == pdm::Phase::failed && f.Events(pdm::Event::failed) == 0, "failed after Forget: no play");
    }
    {
        Game b;
        b.Playing(0);
        b.stops = false;
        b.cls = Cls(0, 1);
        while (b.Tick().action != pdm::Action::stop) {}
        pdm::Out o{};
        while ((o = b.Tick()).event == pdm::Event::none) {}
        Check(o.event == pdm::Event::stop_timeout && b.s.phase == pdm::Phase::backoff && b.s.owned, "backoff after a stop timeout");
        const std::uint64_t retry = b.s.retry_ms;
        pdm::Forget(b.s);
        Check(b.s.phase == pdm::Phase::backoff && !b.s.owned && b.s.cur == pdm::kNoGani && b.s.retry_ms == retry, "Forget keeps the backoff");
        b.in.in_dm = false;  // the body went away with the clip
        b.stops = true;
        const unsigned plays = b.plays;
        while (b.now + 33 < retry) Check(b.Tick().action == pdm::Action::none, "Forget: nothing before retry_ms");
        Check(b.plays == plays && b.RunUntilPlay(10, 300) && b.now >= retry, "Forget: the play after the backoff");
    }
    // R6 adopts an active clip only while patched (as D3 does).
    {
        Game r;
        r.Playing(0);
        r.stops = false;
        r.cls = Cls(0, 1);
        while (r.Tick().action != pdm::Action::stop) {}
        while (r.Tick().event == pdm::Event::none) {}
        pdm::Forget(r.s);
        r.in.patched = false;  // RevertDm: the clip loops on, unpatched
        r.Run(1100);
        Check(r.s.phase == pdm::Phase::wait && !r.s.owned, "R6 unpatched: wait, not adopted");
        r.in.patched = true;
        Check(r.Tick().event == pdm::Event::adopted && r.s.owned, "patched again: adopted");
    }
}

void TestPivotTimelines() {
    constexpr float pi = 3.14159265358979323846f;
    for (const unsigned hz : {20u, 60u, 120u}) {
        for (const std::uint8_t stance : {std::uint8_t{0}, std::uint8_t{1}}) {
            for (const bool large : {false, true}) {
                for (const bool right : {false, true}) {
                    pdm::TurnState state{};
                    // Right turns decrease yaw (right-handed Fox); both cross the +/-pi boundary.
                    const float initial = right ? -3.0f : 3.0f;
                    const float angle = (right ? -1.0f : 1.0f) * (large ? pi : pi / 2);
                    const auto expected = static_cast<std::uint8_t>(13 + stance * 4 + (large ? 2 : 0) + (right ? 1 : 0));
                    const std::uint64_t base = 10000, end = base + 500;
                    Check(pdm::ObserveTurn(state, base, true, initial, stance, false) == pdm::kNoGani,
                          "smooth pivot establishes its starting heading");
                    std::uint64_t selected_ms = 0;
                    for (unsigned frame = 1; frame <= hz; ++frame) {
                        const std::uint64_t elapsed = frame * 1000u / hz;
                        const float progress = elapsed < 500 ? static_cast<float>(elapsed) / 500 : 1.0f;
                        const float noise = elapsed >= 500 ? ((frame & 1u) ? 0.003f : -0.003f) : 0;
                        const float yaw = mgscoop::pose::WrapAngle(initial + angle * progress + noise);
                        const auto clip = pdm::ObserveTurn(state, base + elapsed, true, yaw, stance, false);
                        if (clip != pdm::kNoGani) {
                            Check(clip == expected, "smooth90/180 pivot selects the full signed angle after wrap and settling noise");
                            if (!selected_ms) selected_ms = base + elapsed;
                        }
                    }
                    const auto tick = (1000u + hz - 1) / hz;
                    Check(selected_ms >= end + pdm::kTurnSettleMs && selected_ms <= end + pdm::kTurnSettleMs + tick,
                          "smooth pivot waits70ms for settling and selects within one20/60/120Hz owner tick");
                }
            }
        }
        pdm::TurnState noise{};
        pdm::ObserveTurn(noise, 1000, true, 3.14f, 0, false);
        for (unsigned frame = 1; frame <= hz * 2; ++frame) {
            const float yaw = mgscoop::pose::WrapAngle(3.14f + ((frame & 1u) ? 0.004f : -0.004f));
            Check(pdm::ObserveTurn(noise, 1000 + frame * 1000u / hz, true, yaw, 0, false) == pdm::kNoGani,
                  "wrapped stationary heading noise never requests a turn");
        }
        pdm::TurnState interrupted{};
        pdm::ObserveTurn(interrupted, 1000, true, 0, 0, false);
        for (unsigned frame = 1; frame * 1000u / hz <= 350; ++frame)
            Check(pdm::ObserveTurn(interrupted, 1000 + frame * 1000u / hz, true, pi * (frame * 1000u / hz) / 500, 0, false) == pdm::kNoGani,
                  "a moving smooth pivot does not commit its partial90degree angle");
        Check(pdm::ObserveTurn(interrupted, 1351, true, 2.2f, 0, true) == pdm::kNoGani && !interrupted.valid,
              "movement cancels a collected pivot before it is committed");
        Check(pdm::ObserveTurn(interrupted, 1400, true, pi, 0, false) == pdm::kNoGani &&
              pdm::ObserveTurn(interrupted, 1600, true, pi, 0, false) == pdm::kNoGani,
              "a canceled pivot cannot replay when movement ends");
        pdm::TurnState bounded{};
        pdm::ObserveTurn(bounded, 1000, true, 0, 0, false);
        std::uint64_t selected = 0;
        for (unsigned frame = 1; frame <= hz; ++frame) {
            const auto elapsed = frame * 1000u / hz;
            const auto clip = pdm::ObserveTurn(bounded, 1000 + elapsed, true,
                                              mgscoop::pose::WrapAngle(3.0f * elapsed / 1000), 0, false);
            if (clip != pdm::kNoGani && !selected) selected = 1000 + elapsed;
        }
        const auto tick = (1000u + hz - 1) / hz;
        Check(selected >= 1000 + pdm::kTurnCollectMs && selected <= 1000 + pdm::kTurnCollectMs + 2 * tick,
              "an ongoing pivot selects at the600ms collection deadline without requiring settling");
    }
    pdm::TurnState queued{};
    pdm::ObserveTurn(queued, 1000, true, 0, 0, false);
    Check(pdm::ObserveTurn(queued, 1100, true, 1.3f, 0, false) == pdm::kNoGani &&
          pdm::ObserveTurn(queued, 1170, true, 1.3f, 0, false) == 13, "queued fixture starts a settled90degree pivot");
    const auto first_token = queued.event_token;
    Check(pdm::ObserveTurn(queued, 1300, true, -1.2f, 0, false) == 13 &&
          pdm::ObserveTurn(queued, 1400, true, -1.2f, 0, false) == 16 && queued.event_token != first_token,
          "a settled new pivot emits a distinct request before the active turn hold ends");
    const auto second_token = queued.event_token;
    Check(pdm::ObserveTurn(queued, 2070, true, -1.2f, 0, false) == 16 && queued.event_token == second_token &&
          pdm::ObserveTurn(queued, 2600, true, -1.2f, 0, false) == pdm::kNoGani,
          "a held pivot keeps its event identity until its request expires");
    Check(pdm::ObserveTurn(queued, 2599, true, -1.2f, 0, false) == pdm::kNoGani && queued.valid && queued.gani == pdm::kNoGani,
          "a backwards clock resets a held pivot even when it stays after the collection start");
}

void TestFiniteRequestIdentity() {
    Game defaults;
    const auto start = [&](pdm::State& state, pdm::In& in) {
        pdm::Reset(state);
        in = defaults.in;
        in.cls = Cls(0, 0);
        in.target = 14;
        in.finite_token = 1;
        in.responsive = true;
        in.now_ms = 1000;
        Check(pdm::Step(state, in).action == pdm::Action::none, "finite event observes the candidate hold");
        in.now_ms = 1017;
        Check(pdm::Step(state, in).action == pdm::Action::play, "first finite event starts");
        in.in_dm = true; in.now_ms = 1034;
        Check(pdm::Step(state, in).event == pdm::Event::playing && state.cur_event != 0,
              "finite event confirms with a discrete identity");
    };
    pdm::State state{};
    pdm::In in{};
    start(state, in);
    const auto first = state.cur_event;
    for (unsigned i = 0; i < 10; ++i) {
        in.now_ms += 17;
        Check(pdm::Step(state, in).action == pdm::Action::none && state.finite_count == 1,
              "a held finite token cannot generate duplicate requests");
    }
    in.finite_token = 2; in.now_ms = 1300;
    Check(pdm::Step(state, in).action == pdm::Action::none && state.finite_count == 2 && state.cur_event == first,
          "second identical turn queues before first completes without interrupting it");
    in.in_dm = false; in.now_ms = 1800;
    Check(pdm::Step(state, in).event == pdm::Event::finished && state.finite_count == 1 && state.strikes[14] == 0,
          "first natural completion consumes only its own event identity");
    in.now_ms = 1817;
    Check(pdm::Step(state, in).action == pdm::Action::none, "queued identical turn observes its new candidate hold");
    in.now_ms = 1834;
    Check(pdm::Step(state, in).action == pdm::Action::play && state.sent == 14 && state.sent_event != first,
          "second identical turn survives clip-only completion suppression");
    in.in_dm = true; in.now_ms = 1851;
    Check(pdm::Step(state, in).event == pdm::Event::playing, "second identical turn confirms");
    in.in_dm = false; in.now_ms = 2600;
    Check(pdm::Step(state, in).event == pdm::Event::finished && state.finite_count == 0,
          "second natural completion leaves no queued event");
    for (unsigned i = 0; i < 20; ++i) {
        in.now_ms += 17;
        Check(pdm::Step(state, in).action == pdm::Action::none && state.finite_count == 0,
              "completed held event does not fall through to an anonymous third play");
    }

    // Requests can arrive on the exact confirmation/end ticks, before Step's
    // early reconciliation return. Each must still retain its identity.
    pdm::Reset(state); in = defaults.in; in.target = 14; in.cls = Cls(0, 0); in.finite_token = 1; in.responsive = true;
    in.now_ms = 1000; pdm::Step(state, in);
    in.now_ms = 1017; pdm::Step(state, in);
    in.in_dm = true; in.finite_token = 2; in.now_ms = 1034;
    Check(pdm::Step(state, in).event == pdm::Event::playing && state.finite_count == 2,
          "a second event arriving on confirmation is retained");
    in.in_dm = false; in.finite_token = 3; in.now_ms = 1800;
    Check(pdm::Step(state, in).event == pdm::Event::finished && state.finite_count == 2,
          "a third event arriving on completion is retained while only the first is consumed");

    start(state, in);
    const auto retained_active = state.cur_event;
    for (std::uint32_t token = 2; token <= 9; ++token) {
        in.finite_token = token; in.now_ms += 17;
        pdm::Step(state, in);
        Check(state.finite_count <= pdm::State::finite_capacity && state.finite_events[0].id == retained_active,
              "event bursts stay bounded and cannot overwrite the active turn");
    }
    Check(state.finite_count == 4 && state.finite_dropped == 5 && state.finite_events[1].id == 7 &&
          state.finite_events[2].id == 8 && state.finite_events[3].id == 9,
          "bounded queue retains the active and three most recent waiting events");
    for (unsigned canceled = 0; canceled < 7; ++canceled) {
        auto copy = state;
        auto changed = in;
        changed.now_ms += 17;
        switch (canceled) {
        case 0: changed.map = false; break;
        case 1: changed.blocked = pdm::Block::fault; break;
        case 2: changed.parts_loaded = false; break;
        case 3: changed.cls.via = pdm::Via::lost; changed.finite_token = 0; changed.target = 0; break;
        case 4: changed.urgent = true; changed.cls.move = 1; changed.finite_token = 0; changed.target = 10; break;
        case 5: pdm::Forget(copy); break;
        case 6: pdm::Rearm(copy); break;
        }
        if (canceled < 5) pdm::Step(copy, changed);
        Check(copy.finite_count == 0, "map/fault/parts/loss/movement/unrealize/rearm cancels queued turns");
    }
    auto stale = in; stale.target_ok = false; stale.finite_token = 10; stale.now_ms += 17;
    pdm::Step(state, stale);
    Check(state.finite_count == 4 && state.next_finite_id == 9, "stale input cannot produce a new finite event");
    const auto phase_before_reset = state.phase;
    pdm::CancelFiniteEvents(state);
    Check(state.finite_count == 0 && state.phase == phase_before_reset && state.cur_event == retained_active,
          "explicit source/teleport reset drops events without bypassing native ownership gates");
    start(state, in);
    state.next_finite_id = 0xFFFFFFFFFFFFFFFFull;
    in.finite_token = 2; in.now_ms += 17;
    pdm::Step(state, in);
    Check(state.finite_count == 2 && state.finite_events[1].id != 0 && state.finite_events[1].id != state.cur_event,
          "finite ID wrap skips zero and every active identity without collision");

    // Exercise the actual collector -> mapper contract at realistic owner
    // rates. The second token expires before the first 1500ms clip ends.
    for (const unsigned hz : {20u, 60u, 120u}) {
        pdm::Reset(state); in = defaults.in; in.cls = Cls(0, 0);
        pdm::TurnState turn{};
        bool have_request = false, native_dm = false;
        std::uint64_t natural_end = 0;
        unsigned finite_plays = 0, finite_ends = 0, stops = 0;
        pdm::ObserveTurn(turn, 10000, true, 0, 0, false);
        for (unsigned frame = 1; frame <= hz * 5; ++frame) {
            const auto elapsed = frame * 1000u / hz;
            const float first_progress = elapsed <= 200 ? static_cast<float>(elapsed) / 200 : 1.0f;
            const float second_progress = elapsed <= 500 ? 0 : elapsed < 700 ? static_cast<float>(elapsed - 500) / 200 : 1.0f;
            const auto clip = pdm::ObserveTurn(turn, 10000 + elapsed, true,
                                              mgscoop::pose::WrapAngle(1.5707963f * (first_progress + second_progress)), 0, false);
            if (clip != pdm::kNoGani) have_request = true;
            if (natural_end && 10000 + elapsed >= natural_end) { native_dm = false; natural_end = 0; }
            in.now_ms = 10000 + elapsed;
            in.target_ok = have_request;
            in.target = clip == pdm::kNoGani ? 0 : clip;
            in.finite_token = clip == pdm::kNoGani ? 0 : turn.event_token;
            in.responsive = clip != pdm::kNoGani;
            in.in_dm = native_dm;
            const auto out = pdm::Step(state, in);
            if (out.action == pdm::Action::play) {
                native_dm = true;
                if (pdm::OneShot(out.gani)) {
                    Check(out.gani == 13, "real collector repeats the identical signed90degree turn");
                    ++finite_plays;
                    natural_end = in.now_ms + 1500;
                }
            } else if (out.action == pdm::Action::stop) { native_dm = false; natural_end = 0; ++stops; }
            if (out.event == pdm::Event::finished) ++finite_ends;
            Check(state.finite_count <= pdm::State::finite_capacity, "collector replay queue stays bounded throughout native timing");
        }
        Check(finite_plays == 2 && finite_ends == 2 && stops == 0 && state.strikes[13] == 0,
              "two identical turns replay exactly twice after natural completion without idle hacks or strikes");
    }
    pdm::TurnState wrap{};
    pdm::ObserveTurn(wrap, 1000, true, 0, 0, false);
    wrap.event_token = 0xFFFFFFFFu;
    pdm::ObserveTurn(wrap, 1100, true, 1.3f, 0, false);
    Check(pdm::ObserveTurn(wrap, 1170, true, 1.3f, 0, false) == 13 && wrap.event_token == 1,
          "collector token wrap skips the reserved zero identity");
}

void TestTransitionsAndResponsiveness() {
    pdm::TurnState turn{};
    Check(pdm::ObserveTurn(turn, 1000, true, 3.10f, 0, false) == pdm::kNoGani, "turn first sample establishes heading");
    Check(pdm::ObserveTurn(turn, 1100, true, -3.10f, 0, false) == pdm::kNoGani, "wrap across pi is a small turn");
    Check(pdm::ObserveTurn(turn, 1200, true, -1.85f, 0, false) == pdm::kNoGani, "positive idle pivot first collects its angle");
    Check(pdm::ObserveTurn(turn, 1250, true, -1.80f, 0, false) == pdm::kNoGani &&
          pdm::ObserveTurn(turn, 1320, true, -1.80f, 0, false) == 13, "settled positive (left) idle turn selects resident stand left90");
    Check(pdm::ObserveTurn(turn, 1330, true, -1.70f, 0, true) == pdm::kNoGani, "movement interrupts turn");
    Check(pdm::ObserveTurn(turn, 1400, true, 0, 1, false) == pdm::kNoGani &&
          pdm::ObserveTurn(turn, 1500, true, -2.5f, 1, false) == pdm::kNoGani &&
          pdm::ObserveTurn(turn, 1570, true, -2.5f, 1, false) == 20, "settled large negative (right) squat turn selects right180");
    Check(pdm::ObserveTurn(turn, 1580, false, -2.5f, 1, false) == pdm::kNoGani && !turn.valid, "lost peer clears turn intent");
    pdm::ObserveTurn(turn, 1600, true, 0, 0, false);
    Check(pdm::ObserveTurn(turn, 1700, true, 1.3f, 0, false) == pdm::kNoGani &&
          pdm::ObserveTurn(turn, 1770, true, 1.3f, 0, false) == 13 &&
          pdm::ObserveTurn(turn, 1780, true, 1.3f, 1, false) == pdm::kNoGani, "stance change cancels stand turn");
    Check(pdm::OneShot(13) && pdm::OneShot(20) && !pdm::OneShot(12), "transition metadata separates one-shots and loops");

    Game g;
    auto in = g.in;
    pdm::State state{}; pdm::Reset(state);
    in.target = 13; in.responsive = true; in.now_ms = 1000;
    Check(pdm::Step(state, in).action == pdm::Action::none, "responsive first observation is held one frame");
    in.now_ms = 1017;
    Check(pdm::Step(state, in).action == pdm::Action::play, "responsive transition plays after17ms");
    in.in_dm = true; in.now_ms = 1034;
    Check(pdm::Step(state, in).event == pdm::Event::playing, "one-shot consumed");
    in.in_dm = false; in.now_ms = 1400;
    Check(pdm::Step(state, in).event == pdm::Event::finished && state.strikes[13] == 0, "short one-shot completion never strikes");
    in.now_ms = 1417;
    Check(pdm::Step(state, in).action == pdm::Action::none, "completed held one-shot does not replay");
    in.target = 10; in.urgent = true; in.now_ms = 1434;
    Check(pdm::Step(state, in).action == pdm::Action::none, "next movement observes one frame hold");
    in.now_ms = 1451;
    Check(pdm::Step(state, in).action == pdm::Action::play && state.sent == 10, "movement resumes promptly after transition");
    in.in_dm = true; in.now_ms = 1468; pdm::Step(state, in);
    in.target = 11; in.now_ms = 1501; pdm::Step(state, in);
    in.now_ms = 1549;
    Check(pdm::Step(state, in).action == pdm::Action::stop, "authoritative movement uses80ms dwell instead300ms");

    // Measured movement and observed gun intent can interrupt a finite turn;
    // merely becoming idle keeps its protection against premature truncation.
    for (const bool gun : {false, true}) {
        pdm::Reset(state);
        in = g.in; in.target = 13; in.responsive = true; in.now_ms = 2000;
        pdm::Step(state, in);
        in.now_ms = 2017;
        Check(pdm::Step(state, in).action == pdm::Action::play, "interrupt fixture starts a finite turn");
        in.in_dm = true; in.now_ms = 2034;
        Check(pdm::Step(state, in).event == pdm::Event::playing, "interrupt fixture confirms the turn");
        in.target = gun ? 0 : 10;
        in.responsive = gun; in.measured = !gun; in.urgent = true; in.now_ms = 2051;
        Check(pdm::Step(state, in).action == pdm::Action::none, "new interrupt candidate still observes its hold");
        in.now_ms = gun ? 2113 : 2133;
        Check(pdm::Step(state, in).action == pdm::Action::none, "gun/measured interrupts preserve80/100ms minimum dwell");
        ++in.now_ms;
        Check(pdm::Step(state, in).action == pdm::Action::stop,
              "gun/measured intent interrupts finite turn promptly without waiting1800ms");
    }
    pdm::Reset(state);
    in = g.in; in.target = 13; in.responsive = true; in.now_ms = 3000; pdm::Step(state, in);
    in.now_ms = 3017; pdm::Step(state, in);
    in.in_dm = true; in.now_ms = 3034; pdm::Step(state, in);
    in.target = 0; in.responsive = false; in.measured = true; in.urgent = false; in.now_ms = 3051; pdm::Step(state, in);
    in.now_ms = 3134;
    Check(pdm::Step(state, in).action == pdm::Action::none, "measured idle does not truncate a finite turn merely to shorten dwell");
}

// Turn clip direction: increasing yaw is a left (counter-clockwise from
// above) turn with forward = (sin, cos) on (x, z) in right-handed Fox space.
void TestTurnDirection() {
    constexpr float pi = 3.14159265358979323846f;
    // Geometry: facing +z (yaw 0), +x lies to the LEFT in a right-handed y-up
    // frame (x = y cross z), so a yaw increase toward +x turns left.
    const float up[3] = {0, 1, 0}, forward[3] = {0, 0, 1};
    const float left[3] = {up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2],
                           up[0] * forward[1] - up[1] * forward[0]};
    Check(left[0] == 1.0f && left[2] == 0.0f, "right-handed: up x forward (the left hand) is +x");
    Check(std::sin(pi / 2) > 0.99f, "yaw +90 degrees faces +x, the left side");
    for (const std::uint8_t stance : {std::uint8_t{0}, std::uint8_t{1}}) {
        const std::uint8_t base = static_cast<std::uint8_t>(13 + stance * 4);
        Check(pdm::TurnGani(stance, false, 1.4f) == base && pdm::TurnGani(stance, false, -1.4f) == base + 1 &&
              pdm::TurnGani(stance, true, 3.0f) == base + 2 && pdm::TurnGani(stance, true, -3.0f) == base + 3,
              "positive yaw selects tn_l90/tn_l180, negative tn_r90/tn_r180");
        pdm::TurnState right{};
        pdm::ObserveTurn(right, 1000, true, -60.0f / 57.2957795f, stance, false);
        pdm::ObserveTurn(right, 1100, true, -147.12f / 57.2957795f, stance, false);
        Check(pdm::ObserveTurn(right, 1170, true, -147.12f / 57.2957795f, stance, false) == base + 1,
              "recorded mouse-right aimed pivot (-60 -> -147 degrees) selects the right90 clip");
        pdm::TurnState left_turn{};
        pdm::ObserveTurn(left_turn, 1000, true, 0, stance, false);
        pdm::ObserveTurn(left_turn, 1100, true, 75.2f / 57.2957795f, stance, false);
        Check(pdm::ObserveTurn(left_turn, 1170, true, 75.2f / 57.2957795f, stance, false) == base,
              "recorded mouse-left aimed pivot (+75 degrees) selects the left90 clip");
    }
}

namespace pf = mgscoop::partner_facing;
float Deg(float radians) { return radians * 57.2957795f; }
float Rad(float degrees) { return degrees / 57.2957795f; }

// Runs a fixed-cadence timeline toward `target` and returns the ms until the
// applied yaw lands on it (or 0), checking rate/acceleration limits each tick.
std::uint64_t Converge(pf::State& s, float target, std::uint64_t& now, unsigned step_ms, const pf::Params& p, float& peak_dps) {
    peak_dps = 0;
    float previous_rate = s.rate;
    const std::uint64_t start = now;
    for (unsigned tick = 0; tick < 200; ++tick) {
        now += step_ms;
        const float before = s.yaw;
        const float applied = pf::Step(s, target, now, p, pf::Snap::none);
        const float moved = std::fabs(pf::Wrap(applied - before));
        const float dps = Deg(moved) * 1000.0f / static_cast<float>(step_ms);
        if (dps > peak_dps) peak_dps = dps;
        Check(Deg(moved) <= Deg(p.max_rate) * static_cast<float>(step_ms) / 1000.0f + 0.01f, "facing never exceeds the rate limit");
        Check(std::fabs(s.rate - previous_rate) <= p.accel * static_cast<float>(step_ms) / 1000.0f + 1e-3f ||
              std::fabs(s.rate) < std::fabs(previous_rate), "facing eases in: acceleration bounded");
        previous_rate = s.rate;
        if (std::fabs(pf::Wrap(applied - pf::Wrap(target))) < 1e-5f) return now - start;
    }
    return 0;
}

void TestFacing() {
    constexpr float pi = 3.14159265358979323846f;
    const pf::Params p = pf::ParamsForDps(pf::kDefaultRateDps);
    Check(std::fabs(Deg(p.max_rate) - 900.0f) < 0.01f && std::fabs(Deg(p.accel) - 18000.0f) < 0.5f,
          "default safety net: 900 deg/s, full rate after 50 ms");
    const float rate_dps = Deg(p.max_rate), accel_dps2 = Deg(p.accel);
    // Ideal continuous time for a stepped target of `degrees` (trapezoid or triangle).
    const auto ideal_ms = [&](float degrees) {
        const float ramp = rate_dps * rate_dps / accel_dps2;
        const float seconds = degrees >= ramp ? degrees / rate_dps + rate_dps / accel_dps2 : 2.0f * std::sqrt(degrees / accel_dps2);
        return static_cast<std::uint64_t>(seconds * 1000.0f);
    };
    Check(pf::ParamsForDps(5000).max_rate == pf::ParamsForDps(pf::kMaxRateDps).max_rate, "rate clamps to 1440 deg/s");
    for (const unsigned step : {8u, 16u, 33u}) {
        // W -> W+A (45), W -> A (90), W -> S reversal (180, crossing +-pi).
        struct Case { float from, to, degrees; };
        // Ideal 900 deg/s profile: 100 / 150 / 250 / 117 ms; coarse ticks land a little sooner.
        for (const Case c : {Case{-60.0f, -15.0f, 45.0f}, Case{0.0f, 90.0f, 90.0f}, Case{120.0f, -60.0f, 180.0f},
                             Case{-150.0f, 150.0f, 60.0f}}) {
            const std::uint64_t ideal = ideal_ms(c.degrees);
            pf::State s{};
            std::uint64_t now = 5000;
            Check(pf::Step(s, Rad(c.from), now, p, pf::Snap::none) == pf::Wrap(Rad(c.from)) && s.last_snap == pf::Snap::first,
                  "first sample snaps exactly");
            float peak = 0;
            const auto took = Converge(s, Rad(c.to), now, step, p, peak);
            Check(took >= ideal / 2 && took <= ideal + 2 * step, "a direction step turns over a bounded, eased interval");
            Check(peak > rate_dps * 0.5f && peak <= rate_dps + 0.5f, "the stepped turn reaches the safety-net rate but never exceeds it");
            Check(s.rate == 0.0f || std::fabs(Deg(s.rate)) < rate_dps + 0.5f, "landing keeps a bounded rate");
            // Held target: stays exactly on it without drift.
            for (unsigned i = 0; i < 5; ++i) { now += step; pf::Step(s, Rad(c.to), now, p, pf::Snap::none); }
            Check(s.yaw == pf::Wrap(Rad(c.to)) && s.rate == 0.0f, "settled facing holds the target exactly");
        }
        // The shortest arc: -150 -> 150 turns through +-180 (60 degrees), never through 0.
        pf::State arc{};
        std::uint64_t now = 1000;
        pf::Step(arc, Rad(-150.0f), now, p, pf::Snap::none);
        now += step;
        const float first = Deg(pf::Step(arc, Rad(150.0f), now, p, pf::Snap::none));
        Check(first < -150.0f || first > 150.0f, "facing turns along the shortest arc across +-pi");
        // A smooth native turn (stationary pivot, 180 deg/s) is followed with
        // at most a few degrees of lag and lands exactly.
        pf::State smooth{};
        now = 1000;
        pf::Step(smooth, 0, now, p, pf::Snap::none);
        float worst = 0;
        for (unsigned t = step; t <= 500; t += step) {
            now += step;
            const float target = Rad(180.0f * static_cast<float>(t) / 1000.0f);
            const float applied = pf::Step(smooth, target, now, p, pf::Snap::none);
            const float lag = std::fabs(Deg(pf::Wrap(target - applied)));
            if (lag > worst) worst = lag;
        }
        Check(worst <= 2.0f, "a smooth 180 deg/s source turn is followed within 2 degrees");
        // The recorded host body curve for W -> W+A (run 442f6081, about 42 ms
        // apart) must pass through the safety net essentially unchanged.
        const float body[] = {-59.9f, -50.0f, -40.6f, -30.6f, -24.7f, -18.6f, -16.4f, -15.4f, -15.4f, -15.4f};
        pf::State recorded{};
        now = 1000;
        pf::Step(recorded, Rad(body[0]), now, p, pf::Snap::none);
        float recorded_lag = 0;
        for (unsigned t = step; t < 42u * 9u; t += step) {
            now += step;
            const unsigned i = t / 42u;
            const float f = static_cast<float>(t - i * 42u) / 42.0f;
            const float target = body[i] + (body[i + 1] - body[i]) * f;
            const float lag = std::fabs(Deg(pf::Wrap(Rad(target) - pf::Step(recorded, Rad(target), now, p, pf::Snap::none))));
            if (lag > recorded_lag) recorded_lag = lag;
        }
        Check(recorded_lag <= 2.0f && std::fabs(Deg(recorded.yaw) + 15.4f) < 0.01f,
              "a genuine body turn (about 240 deg/s peak) is not clipped by the safety net");
        for (unsigned i = 0; i < 6; ++i) { now += step; pf::Step(smooth, Rad(90.0f), now, p, pf::Snap::none); }
        Check(smooth.yaw == pf::Wrap(Rad(90.0f)), "a smooth pivot settles exactly on the source heading");
    }
    // Lifecycle snaps are exact, counted and named.
    pf::State s{};
    std::uint64_t now = 1000;
    pf::Step(s, 0, now, p, pf::Snap::none);
    for (const pf::Snap snap : {pf::Snap::teleport, pf::Snap::relink, pf::Snap::warp, pf::Snap::unrealized, pf::Snap::stale}) {
        const auto snaps = s.snaps;
        now += 16;
        const float target = s.yaw + pi * 0.75f;
        Check(pf::Step(s, target, now, p, snap) == pf::Wrap(target) && s.rate == 0.0f && s.snaps == snaps + 1 && s.last_snap == snap,
              "teleport/relink/warp/unrealized/stale snap to the exact target");
    }
    Check(std::strcmp(pf::SnapName(pf::Snap::teleport), "teleport") == 0 && std::strcmp(pf::SnapName(pf::Snap::none), "none") == 0,
          "snap names");
    now += pf::kMaxStepMs + 1;
    Check(pf::Step(s, 1.0f, now, p, pf::Snap::none) == 1.0f && s.last_snap == pf::Snap::gap, "an owner gap snaps");
    now -= 5;
    Check(pf::Step(s, -1.0f, now, p, pf::Snap::none) == -1.0f && s.last_snap == pf::Snap::clock, "a backwards clock snaps");
    const float held = s.yaw;
    Check(pf::Step(s, 2.0f, now, p, pf::Snap::none) == held, "a repeated tick time holds the applied yaw");
    // 0 = off: exact remote yaw every tick (the previous behaviour).
    const pf::Params off = pf::ParamsForDps(0);
    pf::State exact{};
    now = 1000;
    pf::Step(exact, 0, now, off, pf::Snap::none);
    const auto snaps = exact.snaps;
    for (unsigned i = 1; i <= 4; ++i) {
        now += 16;
        Check(pf::Step(exact, Rad(45.0f * static_cast<float>(i)), now, off, pf::Snap::none) == pf::Wrap(Rad(45.0f * static_cast<float>(i))),
              "partner_facing_dps=0 applies the exact remote yaw");
    }
    Check(exact.snaps == snaps + 1 && exact.last_snap == pf::Snap::off, "off is one counted state, not a snap per tick");
    // A reversal mid-turn decelerates through zero rather than jumping.
    pf::State back{};
    now = 1000;
    pf::Step(back, 0, now, p, pf::Snap::none);
    for (unsigned i = 0; i < 6; ++i) { now += 16; pf::Step(back, Rad(90.0f), now, p, pf::Snap::none); }
    const float mid = back.yaw, rate = back.rate;
    now += 16;
    pf::Step(back, Rad(-90.0f), now, p, pf::Snap::none);
    Check(rate > 0 && back.rate < rate && back.rate >= rate - p.accel * 0.016f - 1e-3f && back.yaw > mid,
          "a reversed target brakes at the acceleration bound, never an instant flip");
    // Evidence ring: bounded, oldest first.
    pf::Ring ring{};
    for (std::uint32_t i = 0; i < pf::kRing + 5; ++i) {
        pf::Sample sample{};
        sample.ms = i;
        pf::Push(ring, sample);
    }
    Check(ring.count == pf::kRing && pf::At(ring, 0).ms == 5 && pf::At(ring, pf::kRing - 1).ms == pf::kRing + 4,
          "facing ring keeps the newest 48 samples in order");
}

} // namespace

int main() {
    TestNamesAndReset();
    TestSpeedBands();
    TestClassify();
    TestResolveTarget();
    TestSpeedRing();
    TestMeasuredPoseTimeline();
    TestSwitchFlow();
    TestNaturalEnd();
    TestLossAttribution();
    TestUnconsumed();
    TestLateClip();
    TestDmReadTimeouts();
    TestGates();
    TestAdopt();
    TestBlocked();
    TestRelease();
    TestRearmAndForget();
    TestPivotTimelines();
    TestFiniteRequestIdentity();
    TestTransitionsAndResponsiveness();
    TestTurnDirection();
    TestFacing();
    std::printf("%u partner dmmap checks passed: reset/hold, speed bands and hysteresis, classify (flags/speed/anim/lost), table and "
                "bad fallback, speed ring, stop-then-play switch, hold/dwell, natural end strikes, loss attribution, unconsumed backoff/"
                "failed/rearm, late clip, dm_read and stop timeouts, play/stop gates, stale freeze, adopt, blocks, release (map again "
                "from play_sent, late stop), Forget keeps failed/backoff, turn clip direction, eased rate-limited facing with lifecycle snaps\n",
                checks);
    return 0;
}
