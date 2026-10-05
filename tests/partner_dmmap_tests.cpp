// dm=map: the direct-motion clip mapper (partner_dmmap) on deterministic
// timelines. A tiny model stands in for the game: a play starts the clip on
// the next tick (q18[1] == 0x8b) unless `consume` is off, a stop ends it on
// the next tick unless `stops` is off, and an unconsumed play stays pending
// for 2 s (the native watch). Every action the module returns is applied
// before the next tick, exactly as the driver does.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "mgscoop/partner_dmmap.hpp"

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
    odd.cells[1] = 13;
    Check(pdm::ResolveTarget(odd, Cls(0, 1), 0) == 0, "a cell above the GANI bound falls back");
}

void TestSpeedRing() {
    pdm::SpeedRing r{};
    pdm::SpeedReset(r);
    std::uint32_t v = 0;
    Check(!pdm::SpeedMmps(r, v), "empty ring");
    for (int i = 0; i <= 4; ++i) pdm::SpeedPush(r, 1000 + 33u * i, 2356 * 33 * i / 1000, 0);
    Check(!pdm::SpeedMmps(r, v), "span below 150 ms");
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
    Check(r.count == pdm::kSpeedRing && pdm::SpeedMmps(r, v) && v >= 980 && v <= 1020, "the ring wraps; only the newest 300 ms count");
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

} // namespace

int main() {
    TestNamesAndReset();
    TestSpeedBands();
    TestClassify();
    TestResolveTarget();
    TestSpeedRing();
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
    std::printf("%u partner dmmap checks passed: reset/hold, speed bands and hysteresis, classify (flags/speed/anim/lost), table and "
                "bad fallback, speed ring, stop-then-play switch, hold/dwell, natural end strikes, loss attribution, unconsumed backoff/"
                "failed/rearm, late clip, dm_read and stop timeouts, play/stop gates, stale freeze, adopt, blocks, release (map again "
                "from play_sent, late stop), Forget keeps failed/backoff\n",
                checks);
    return 0;
}
