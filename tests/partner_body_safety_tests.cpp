// 0.13.6 body safety (partner_body_safety): the stuck re-show, the drop guard and
// the put-down guard, on timelines taken from the 2026-10-22 two-PC session.
// Each case fails without the guard (0.13.5 had none: no re-show after placed
// ok=0, no placement on a lone edge drop, no correction during act 44).
#include "mgscoop/partner_body_safety.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace sb = mgscoop::partner_body_safety;

namespace {
unsigned g_checks = 0;
void Check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        std::fprintf(stderr, "partner_body_safety_tests failed: %s\n", what);
        std::exit(1);
    }
}

void ActClasses() {
    for (unsigned a = 0; a < 300; ++a) {
        const auto act = static_cast<std::uint16_t>(a);
        Check(sb::OnFootAct(act) == (a >= 1 && a <= 20), "on foot 1..20");
        Check(sb::StuckAct(act) == (a == 25 || a == 28 || (a >= 54 && a <= 56)), "stuck 25, 28, 54..56");
        Check(sb::DropAct(act) == (a == 25 || a == 28), "drop 25, 28");
        Check(sb::VerticalAct(act) == ((a >= 21 && a <= 28) || (a >= 54 && a <= 56)), "vertical 21..28, 54..56");
        Check(sb::CarriedOrAsleepAct(act) == ((a >= 42 && a <= 44) || a == 131 || a == 132), "carried/asleep");
        Check(sb::ReleasedAct(act) == (a == 44 || a == 131 || a == 132 || a == 25 || a == 26), "released");
    }
}

sb::StuckIn Stuck(std::uint64_t now, std::uint16_t body, std::uint16_t remote) {
    sb::StuckIn in{};
    in.now_ms = now;
    in.placed_fail = true;
    in.capable = true;
    in.body_anim_ok = true;
    in.body_act = body;
    in.remote_valid = true;
    in.remote_act = remote;
    return in;
}

void StuckCases() {
    // H:11724 placed ok=0 with body 1 at 54 (ledge hang), the remote at 11 (aim stand idle).
    sb::StuckState st{};
    Check(sb::StuckStep(st, Stuck(456317950, 54, 11)) == sb::StuckVerdict::reshow, "ledge hang + on-foot remote: reshow");
    Check(st.count == 1, "one reshow counted");
    // Every stuck act qualifies; on-foot body acts and off-foot remotes never do.
    for (std::uint16_t a : {std::uint16_t(25), std::uint16_t(28), std::uint16_t(55), std::uint16_t(56)}) {
        sb::StuckState s{};
        Check(sb::StuckStep(s, Stuck(1000, a, 2)) == sb::StuckVerdict::reshow, "stuck act reshow");
    }
    for (std::uint16_t a : {std::uint16_t(1), std::uint16_t(2), std::uint16_t(26), std::uint16_t(21), std::uint16_t(44)}) {
        sb::StuckState s{};
        Check(sb::StuckStep(s, Stuck(1000, a, 2)) == sb::StuckVerdict::none, "non-stuck body act: none");
    }
    for (std::uint16_t r : {std::uint16_t(0), std::uint16_t(21), std::uint16_t(25), std::uint16_t(54), std::uint16_t(111), std::uint16_t(132)}) {
        sb::StuckState s{};
        Check(sb::StuckStep(s, Stuck(1000, 54, r)) == sb::StuckVerdict::none, "remote not on foot (it hangs or climbs too): none");
    }
    {
        sb::StuckState s{};
        auto in = Stuck(1000, 54, 2);
        in.placed_fail = false;
        Check(sb::StuckStep(s, in) == sb::StuckVerdict::none && s.count == 0, "no failed placement: none");
        in = Stuck(1000, 54, 2);
        in.remote_valid = false;
        Check(sb::StuckStep(s, in) == sb::StuckVerdict::none, "unknown remote act: none");
        in = Stuck(1000, 54, 2);
        in.body_anim_ok = false;
        Check(sb::StuckStep(s, in) == sb::StuckVerdict::none, "unknown body act: none");
        in = Stuck(1000, 54, 2);
        in.capable = false;
        Check(sb::StuckStep(s, in) == sb::StuckVerdict::not_capable && s.count == 0, "no re-show flow here: not_capable, nothing counted");
    }
    // Rate: at most one per kReshowGapMs, then again.
    sb::StuckState r{};
    Check(sb::StuckStep(r, Stuck(10000, 54, 2)) == sb::StuckVerdict::reshow, "first");
    Check(sb::StuckStep(r, Stuck(10000 + sb::kReshowGapMs - 1, 54, 2)) == sb::StuckVerdict::rate, "inside the gap: rate");
    Check(sb::StuckStep(r, Stuck(10000 + sb::kReshowGapMs, 54, 2)) == sb::StuckVerdict::reshow, "after the gap: reshow");
    // Mission cap.
    sb::StuckState c{};
    for (std::uint32_t i = 0; i < sb::kReshowMissionCap; ++i)
        Check(sb::StuckStep(c, Stuck(1000 + i * 10000ull, 25, 1)) == sb::StuckVerdict::reshow, "under the cap");
    Check(sb::StuckStep(c, Stuck(1000 + sb::kReshowMissionCap * 10000ull, 25, 1)) == sb::StuckVerdict::cap, "the mission cap");
}

sb::DropIn Drop(std::uint64_t now, std::uint16_t body, std::uint16_t remote, double body_y, double target_y) {
    sb::DropIn in{};
    in.now_ms = now;
    in.enabled = true;
    in.body_anim_ok = true;
    in.body_act = body;
    in.body_ok = true;
    in.body_y = body_y;
    in.remote_valid = true;
    in.remote_act = remote;
    in.target_ok = true;
    in.target_y = target_y;
    return in;
}

void DropCases() {
    // H:11285..11309, the logged acts and times (host log, partner_act / partner_body / partner_recon): body
    // 2 -> 28 (cliff_drop, over the railing: y 800 -> 1546) -> 25 -> 26 -> 25 -> 26 (landed on the lip outside
    // the railing at the deck's height, y 800, at +407 ms and still there at +500 ms) -> 25 (+813 ms) -> 54
    // (ledge hang, y 979) while the remote walks on the deck (2, y 800). Body 1's own 28 enters the native
    // traversal hold from the next tick on (partner_traverse=entered; reason=native; act=28). 0.13.5: no
    // guard; the first 0.13.6 guard closed the episode on that hold and would have judged the landing at
    // +500 ms "landed" anyway: no placement before the hang either way.
    {
        sb::DropState st{};
        const auto at = [](std::uint64_t t, std::uint16_t body, double body_y, bool hold) {
            sb::DropIn in = Drop(t, body, 2, body_y, 800);
            in.traverse_hold = hold;
            return in;
        };
        Check(sb::DropStep(st, at(456314890, 2, 800, false)).verdict == sb::DropVerdict::none, "walking");
        Check(sb::DropStep(st, at(456314921, 28, 800, false)).verdict == sb::DropVerdict::opened, "cliff drop edge opens");
        Check(sb::DropStep(st, at(456314937, 28, 1100, true)).verdict == sb::DropVerdict::none && st.drop_ms != 0,
              "the traversal hold body 1's own drop entered keeps the episode open");
        Check(sb::DropStep(st, at(456314984, 25, 1546, true)).verdict == sb::DropVerdict::none, "fall");
        Check(sb::DropStep(st, at(456315000, 26, 1530, true)).verdict == sb::DropVerdict::none, "land");
        Check(sb::DropStep(st, at(456315015, 25, 1507, true)).verdict == sb::DropVerdict::none, "fall");
        Check(sb::DropStep(st, at(456315328, 26, 800, true)).verdict == sb::DropVerdict::none, "landed on the lip at the deck's height");
        Check(sb::DropStep(st, at(456314921 + sb::kDropMatchMs, 26, 800, true)).verdict == sb::DropVerdict::none && st.drop_ms != 0,
              "+500 ms: a landing at the remote's level waits");
        const auto o = sb::DropStep(st, at(456315734, 25, 780, true));
        Check(o.verdict == sb::DropVerdict::place && o.act == 28 && o.ms == 813, "falls again: place, before the ledge hang");
        Check(st.places == 1 && st.drop_ms == 0, "episode closed, counted");
        Check(sb::DropStep(st, at(456315750, 54, 979, true)).verdict == sb::DropVerdict::none, "no second episode from 25 -> 54");
    }
    // A traversal already held before the edge (a replay, a native traversal's grace): no episode opens.
    {
        sb::DropState st{};
        auto in = Drop(1000, 2, 2, 800, 800);
        in.traverse_hold = true;
        sb::DropStep(st, in);
        in = Drop(1016, 28, 2, 800, 800);
        in.traverse_hold = true;
        Check(sb::DropStep(st, in).verdict == sb::DropVerdict::none && st.drop_ms == 0, "held traversal: no episode");
    }
    // A landing at the level, then on its feet there: landed, judged on its feet.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1516, 26, 2, 800, 800)).verdict == sb::DropVerdict::none, "landing at the level: wait");
        Check(sb::DropStep(st, Drop(1700, 2, 2, 800, 800)).verdict == sb::DropVerdict::landed && st.places == 0, "on its feet: landed");
    }
    // The wait for a landing is bounded.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1016 + sb::kDropLandWaitMs - 1, 26, 2, 800, 800)).verdict == sb::DropVerdict::none, "still landing");
        Check(sb::DropStep(st, Drop(1016 + sb::kDropLandWaitMs, 26, 2, 800, 800)).verdict == sb::DropVerdict::landed, "bounded: landed");
    }
    // A landing 1 m under the remote: place at once (no wait).
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1516, 26, 2, -200, 800)).verdict == sb::DropVerdict::place, "landing a level lower: place");
    }
    // The remote dropped first (its act within kDropMatchMs before the body's edge): a real fall.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1100, 2, 28, 800, 800));
        Check(sb::DropStep(st, Drop(1300, 28, 25, 800, 300)).verdict == sb::DropVerdict::matched, "remote fell first: matched");
        Check(st.places == 0 && st.matched == 1, "nothing placed");
    }
    // The remote drops after the body's edge (the body was early): matched.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1200, 25, 28, 500, 700)).verdict == sb::DropVerdict::matched, "remote fell after: matched");
        Check(sb::DropStep(st, Drop(1600, 25, 25, 0, 0)).verdict == sb::DropVerdict::none, "episode closed");
    }
    // A stale remote vertical act (older than the window) does not match.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 24, 800, 800));
        sb::DropStep(st, Drop(1100, 2, 2, 800, 800));
        sb::DropStep(st, Drop(2000, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(2000 + sb::kDropMatchMs, 25, 2, 0, 800)).verdict == sb::DropVerdict::place, "old remote act: place");
    }
    // The remote's height changed (stairs, its own fall without an act sample): level, no placement.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1600, 25, 2, 0, 800 - sb::kDropLevelMm - 1)).verdict == sb::DropVerdict::level, "remote went down: level");
        Check(st.places == 0 && st.refused == 1, "refused counted");
    }
    // A hop: the body is back on its feet at the remote's level: landed, no placement.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1600, 1, 2, 700, 800)).verdict == sb::DropVerdict::landed, "hop at the same level: landed");
    }
    // Landed on a lower level (on its feet, 1 m under the remote): place.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 25, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1600, 1, 2, -200, 800)).verdict == sb::DropVerdict::place, "on its feet a level lower: place");
    }
    // The remote left the on-foot acts (it climbs a ladder now): no placement.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1600, 25, 109, 0, 800)).verdict == sb::DropVerdict::remote_off_foot, "remote off foot");
    }
    // Lost pose at the decision: no placement.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        auto in = Drop(1600, 25, 2, 0, 800);
        in.target_ok = false;
        Check(sb::DropStep(st, in).verdict == sb::DropVerdict::lost, "no live target: lost");
    }
    // The edge needs an on-foot previous act: 26 -> 25 (land, fall again) or 54 -> 25 never opens.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 26, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1016, 25, 2, 800, 800)).verdict == sb::DropVerdict::none, "26 -> 25: no edge");
        sb::DropStep(st, Drop(1032, 54, 2, 800, 800));
        Check(sb::DropStep(st, Drop(1048, 25, 2, 800, 800)).verdict == sb::DropVerdict::none, "54 -> 25: no edge");
    }
    // Disabled (box, carry or the put-down guard): any open episode closes silently.
    {
        sb::DropState st{};
        sb::DropStep(st, Drop(1000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(1016, 28, 2, 800, 800));
        auto in = Drop(1100, 25, 2, 0, 800);
        in.enabled = false;
        Check(sb::DropStep(st, in).verdict == sb::DropVerdict::none && st.drop_ms == 0, "disabled closes the episode");
        Check(sb::DropStep(st, Drop(1600, 25, 2, 0, 800)).verdict == sb::DropVerdict::none, "nothing placed later");
        in = Drop(2000, 2, 2, 800, 800);
        in.enabled = false;
        sb::DropStep(st, in);
        in = Drop(2016, 28, 2, 800, 800);
        in.enabled = false;
        Check(sb::DropStep(st, in).verdict == sb::DropVerdict::none, "disabled: no edge");
    }
    // Rate: at most one prevention placement per kDropPlaceGapMs.
    {
        sb::DropState st{};
        std::uint64_t t = 10000;
        for (int round = 0; round < 2; ++round) {
            sb::DropStep(st, Drop(t, 2, 2, 800, 800));
            sb::DropStep(st, Drop(t + 16, 28, 2, 800, 800));
            const auto v = sb::DropStep(st, Drop(t + 16 + sb::kDropMatchMs, 25, 2, 0, 800)).verdict;
            Check(v == (round == 0 ? sb::DropVerdict::place : sb::DropVerdict::rate), "second drop inside the gap: rate");
            t += 600;
        }
        sb::DropStep(st, Drop(t + 2000, 2, 2, 800, 800));
        sb::DropStep(st, Drop(t + 2016, 28, 2, 800, 800));
        Check(sb::DropStep(st, Drop(t + 2016 + sb::kDropMatchMs, 25, 2, 0, 800)).verdict == sb::DropVerdict::place, "after the gap: place");
    }
}

sb::PutDownIn Put(std::uint64_t now, std::uint16_t body, std::uint16_t remote, double bx, double by, double bz, double tx, double ty, double tz) {
    sb::PutDownIn in{};
    in.now_ms = now;
    in.body_ok = true;
    in.body[0] = bx; in.body[1] = by; in.body[2] = bz;
    in.body_anim_ok = true;
    in.body_act = body;
    in.remote_valid = true;
    in.remote_act = remote;
    in.target_ok = true;
    in.target[0] = tx; in.target[1] = ty; in.target[2] = tz;
    return in;
}

void PutDownCases() {
    // H:8992..9071: the friend's body (act 44 then 132) slides 4 m and falls to y -79 m while the remote
    // (carried 43/44, then asleep) stays on the deck at y 350. 0.13.5: the carry hold kept every correction off.
    {
        sb::PutDownState st{};
        std::uint64_t t = 456216562;
        Check(sb::PutDownStep(st, Put(t, 44, 43, 207099, 1189, 374466, 207153, 350, 374882)).action == sb::PutDownAction::none,
              "a normal put-down (body above the target): nothing");
        Check(sb::PutDownStep(st, Put(t + 400, 44, 43, 203673, 431, 375996, 207153, 350, 374882)).action == sb::PutDownAction::none,
              "4 m sideways on the deck: nothing (horizontal error alone never starts it)");
        const auto start = sb::PutDownStep(st, Put(t + 600, 44, 43, 202942, -2196, 376270, 207153, 350, 374882));
        Check(start.started && start.action == sb::PutDownAction::place && start.dy > sb::kPutDownDropMm, "2.5 m below: start with a Warp");
        auto o = sb::PutDownStep(st, Put(t + 700, 44, 43, 202502, -11269, 376430, 207042, 350, 375126));
        Check(o.action == sb::PutDownAction::pin, "inside the place gap: pin");
        o = sb::PutDownStep(st, Put(t + 1600, 44, 44, 202502, -22486, 376430, 207042, 350, 375126));
        Check(o.action == sb::PutDownAction::place && st.places == 2, "still far below after the gap: Warp again");
        o = sb::PutDownStep(st, Put(t + 1700, 132, 44, 207042, 300, 375100, 207042, 350, 375126));
        Check(o.action == sb::PutDownAction::pin && !o.ended, "held at the target: pin until the hold time");
        o = sb::PutDownStep(st, Put(t + 1700 + sb::kPutDownHoldMs, 132, 132, 207042, 340, 375100, 207042, 350, 375126));
        Check(o.ended && o.end_reason && o.end_reason[0] == 'h' && !st.active && o.action == sb::PutDownAction::none, "held: end");
        Check(st.episodes == 1, "one episode");
    }
    // The horse cargo (coop_carry's cargo hold; Lua warps body 1 onto the horse every tick): never an
    // episode, and an active one ends (reason=cargo). The first 0.13.6 guard fought that warp.
    {
        sb::PutDownState st{};
        auto in = Put(1000, 132, 132, 0, -5000, 0, 0, 800, 0);
        in.cargo = true;
        Check(sb::PutDownStep(st, in).action == sb::PutDownAction::none && !st.active && st.episodes == 0, "cargo: nothing");
        Check(sb::PutDownStep(st, Put(1016, 132, 132, 0, -5000, 0, 0, 800, 0)).started, "no cargo: an episode");
        in.now_ms = 1032;
        const auto o = sb::PutDownStep(st, in);
        Check(o.ended && !st.active && o.end_reason && std::strcmp(o.end_reason, "cargo") == 0 && o.action == sb::PutDownAction::none,
              "the cargo takes over: end, reason=cargo");
    }
    // Attached carry (body 43): never in scope, even with a large vertical error.
    {
        sb::PutDownState st{};
        Check(sb::PutDownStep(st, Put(1000, 43, 43, 0, -5000, 0, 0, 800, 0)).action == sb::PutDownAction::none, "body 43 attached: nothing");
        Check(sb::PutDownStep(st, Put(1000, 42, 42, 0, -5000, 0, 0, 800, 0)).action == sb::PutDownAction::none, "body 42 attached: nothing");
    }
    // The remote is not carried or asleep (it stands up, it carries): never in scope.
    for (std::uint16_t r : {std::uint16_t(1), std::uint16_t(39), std::uint16_t(133), std::uint16_t(25)}) {
        sb::PutDownState st{};
        Check(sb::PutDownStep(st, Put(1000, 44, r, 0, -5000, 0, 0, 800, 0)).action == sb::PutDownAction::none, "remote awake: nothing");
    }
    // A body above the target (on a crate) or within 2 m below: nothing.
    {
        sb::PutDownState st{};
        Check(sb::PutDownStep(st, Put(1000, 132, 132, 0, 5000, 0, 0, 800, 0)).action == sb::PutDownAction::none, "above: nothing");
        Check(sb::PutDownStep(st, Put(1016, 132, 132, 0, 800 - sb::kPutDownDropMm, 0, 0, 800, 0)).action == sb::PutDownAction::none,
              "exactly 2 m below: nothing");
    }
    // Scope end (the remote woke) ends it; the timeout bounds it; bad data keeps it without writes.
    {
        sb::PutDownState st{};
        Check(sb::PutDownStep(st, Put(1000, 132, 132, 0, -5000, 0, 0, 800, 0)).started, "start");
        auto in = Put(1100, 132, 132, 0, -5000, 0, 0, 800, 0);
        in.target_ok = false;
        auto o = sb::PutDownStep(st, in);
        Check(o.action == sb::PutDownAction::none && !o.ended && st.active, "no target: wait");
        in = Put(1200, 132, 132, 0, std::numeric_limits<double>::quiet_NaN(), 0, 0, 800, 0);
        o = sb::PutDownStep(st, in);
        Check(o.action == sb::PutDownAction::none && st.active, "non-finite body: wait");
        o = sb::PutDownStep(st, Put(1300, 133, 133, 0, -5000, 0, 0, 800, 0));
        Check(o.ended && !st.active && o.end_reason && o.end_reason[0] == 's', "the remote woke: scope end");
        Check(sb::PutDownStep(st, Put(2000, 132, 132, 0, -5000, 0, 0, 800, 0)).started, "a new episode");
        std::uint32_t places = 1;
        bool ended = false;
        for (std::uint64_t t = 2016; t < 2000 + sb::kPutDownMaxMs + 100; t += 16) {
            o = sb::PutDownStep(st, Put(t, 132, 132, 0, -5000, 0, 0, 800, 0));
            if (o.action == sb::PutDownAction::place) ++places;
            if (o.ended) { ended = true; Check(o.end_reason && o.end_reason[0] == 't', "timeout"); break; }
        }
        Check(ended && places == sb::kPutDownPlaces, "bounded Warps, then the timeout");
    }
}
} // namespace

int main() {
    ActClasses();
    StuckCases();
    DropCases();
    PutDownCases();
    std::printf("partner_body_safety_tests: %u passed\n", g_checks);
    return 0;
}
