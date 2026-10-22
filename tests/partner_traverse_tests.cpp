// partner_traverse: the traversal replay machine (ladder/pipe/climb/step-on
// replay with a verified placement, the F3 icon request level with one id per
// episode, and the reconciliation hold).
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <cstring>
#include "mgscoop/partner_traverse.hpp"

namespace {
namespace tv = mgscoop::partner_traverse;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

struct World {
    tv::State st{};
    tv::In in{};
    tv::Out out{};
    unsigned icon_ticks = 0, places = 0, requested_events = 0;
    tv::Icon last_icon = tv::Icon::none;
    std::uint32_t last_seq = 0;
    World() {
        in.now_ms = 1000;
        in.enabled = true;
        in.armed = true;
        in.remote_ok = in.body_ok = in.target_ok = true;
        in.remote_act = in.body_act = 2;
        in.target[0] = 1000.0; in.target[1] = 0.0; in.target[2] = 0.0;
        in.body[0] = 0.0; in.body[1] = 0.0; in.body[2] = 0.0;
        in.target_yaw = 1.0; in.body_yaw = 0.0;
    }
    tv::Out Tick(std::uint32_t dt = 16) {
        in.now_ms += dt;
        out = tv::Step(st, in);
        if (out.icon != tv::Icon::none) {
            ++icon_ticks;
            last_icon = out.icon;
            last_seq = out.icon_seq;
            Check(out.icon_seq != 0, "a raised icon always carries its request id");
        } else {
            Check(out.icon_seq == 0, "no request id without an icon");
        }
        if (out.request_place) ++places;
        if (out.event == tv::Out::Event::requested) ++requested_events;
        return out;
    }
    void Run(std::uint32_t ms) { for (std::uint32_t t = 0; t < ms; t += 16) Tick(); }
};

void TestActSets() {
    // Static writer census (1.0.15.4): 21 ladder, 22 pipe, 23 wall climb, 24 step-on: all icon starts.
    for (std::uint16_t a : {std::uint16_t(21), std::uint16_t(22), std::uint16_t(23), std::uint16_t(24)})
        Check(tv::ReplayAct(a) && tv::TraverseAct(a), "replay acts 21..24 (23 included)");
    Check(tv::IconFor(21) == tv::Icon::ladder && tv::IconFor(22) == tv::Icon::pipe && tv::IconFor(23) == tv::Icon::climb &&
              tv::IconFor(24) == tv::Icon::step_on,
          "IconFor: 21 ladder, 22 pipe, 23 climb, 24 step-on");
    // Automatic acts are never request targets (they arrive natively): falls, landing, evade, drops, hangs.
    for (std::uint16_t a : {std::uint16_t(25), std::uint16_t(26), std::uint16_t(27), std::uint16_t(28), std::uint16_t(54), std::uint16_t(55),
                            std::uint16_t(56)})
        Check(!tv::ReplayAct(a) && tv::IconFor(a) == tv::Icon::none, "automatic act: no request");
    Check(std::strcmp(tv::IconName(tv::Icon::climb), "climb") == 0 && std::strcmp(tv::IconName(tv::Icon::none), "none") == 0, "icon names");
    // 49/50/59 are Fulton plugins, 72/73 doors, 77/78 box: never traversals.
    const std::uint16_t notTrav[] = {49, 50, 59, 72, 73, 77, 78, 1, 2, 25, 26, 27, 57, 112};
    for (std::uint16_t a : notTrav) Check(!tv::TraverseAct(a) && !tv::ReplayAct(a), "not a traversal act");
    const std::uint16_t trav[] = {21, 22, 23, 24, 28, 54, 55, 56};
    for (std::uint16_t a : trav) Check(tv::TraverseAct(a), "traversal act");
    // Ladder sources: the game's own mask 0x18200001E (FUN_1411E9A70) = acts 1..4, 25, 31, 32.
    const std::uint16_t ladderFrom[] = {1, 2, 3, 4, 25, 31, 32};
    for (std::uint16_t a : ladderFrom) Check(tv::LadderSourceAct(a) && tv::SourceAct(21, a), "ladder source act");
    const std::uint16_t ladderNot[] = {0, 5, 6, 7, 8, 20, 21, 24, 26, 30, 33, 54, 55, 56, 63, 64, 139};
    for (std::uint16_t a : ladderNot) Check(!tv::LadderSourceAct(a) && !tv::SourceAct(21, a), "not a ladder source act");
    // Pipe (FUN_141210510) and climb (CheckActionState 0x14114A900): acts 1..20.
    for (std::uint16_t r : {std::uint16_t(22), std::uint16_t(23)}) {
        Check(tv::SourceAct(r, 1) && tv::SourceAct(r, 5) && tv::SourceAct(r, 8) && tv::SourceAct(r, 20), "pipe/climb: acts 1..20");
        for (std::uint16_t a : {std::uint16_t(0), std::uint16_t(21), std::uint16_t(25), std::uint16_t(31), std::uint16_t(32), std::uint16_t(54),
                                std::uint16_t(139)})
            Check(!tv::SourceAct(r, a), "pipe/climb: nothing else");
    }
    // Step-on (FUN_141293790, Exec's act 0x36 path): 1..20, 31, 32 and the hang 54.
    for (std::uint16_t a : {std::uint16_t(1), std::uint16_t(8), std::uint16_t(20), std::uint16_t(31), std::uint16_t(32), std::uint16_t(54)})
        Check(tv::SourceAct(24, a), "step-on source act");
    for (std::uint16_t a : {std::uint16_t(0), std::uint16_t(21), std::uint16_t(25), std::uint16_t(30), std::uint16_t(55), std::uint16_t(56),
                            std::uint16_t(139)})
        Check(!tv::SourceAct(24, a), "not a step-on source act");
    // Non-replay acts never have a source.
    Check(!tv::SourceAct(28, 2) && !tv::SourceAct(54, 2) && !tv::SourceAct(0, 2), "no source for a non-replay act");
}

// Remote 2 -> 21: one placement, then the ladder icon only after the placement
// verified with the window armed, as a level with one request id until body 1
// enters 21; the hold lasts until body 1 left the ladder plus the grace.
void TestLadderReplay() {
    World w;
    w.Tick();
    Check(!w.out.hold && w.st.phase == tv::Phase::idle, "idle while both walk");
    w.in.remote_act = 21;
    w.Tick();
    Check(w.out.request_place && w.out.hold && w.out.event == tv::Out::Event::start && std::strcmp(w.out.reason, "placed") == 0,
          "edge into 21: request a placement, hold");
    Check(w.icon_ticks == 0, "no icon before the placement");
    // The reconciliation places/verifies (window neutral or disarmed).
    w.in.placing = true; w.in.armed = false;
    w.Tick(); w.Tick();
    Check(w.icon_ticks == 0 && w.st.phase == tv::Phase::place, "no icon while placing");
    w.in.placing = false;
    w.Tick();
    Check(w.icon_ticks == 0, "no icon until the window is armed again");
    w.in.armed = true;
    w.Tick();
    Check(w.st.phase == tv::Phase::request && w.icon_ticks == 1 && w.out.icon == tv::Icon::ladder && w.out.icon_seq == w.st.icon_seq &&
              w.out.event == tv::Out::Event::placed && w.st.requests == 1,
          "placed and armed: the ladder icon is requested at once (the placed event goes first)");
    const std::uint32_t seq = w.out.icon_seq;
    w.Tick();
    Check(w.out.event == tv::Out::Event::requested && std::strcmp(w.out.reason, "ladder") == 0, "the request event on the next free tick");
    w.Tick();
    Check(w.icon_ticks == 3 && w.out.icon == tv::Icon::ladder && w.out.icon_seq == seq && w.requested_events == 1,
          "a level: every tick, the same id, one event");
    // A disarmed tick drops the level; it comes back with the same id.
    w.in.armed = false;
    w.Tick();
    Check(w.out.icon == tv::Icon::none && w.st.phase == tv::Phase::request, "not armed: no icon this tick");
    w.in.armed = true;
    w.Tick();
    Check(w.out.icon == tv::Icon::ladder && w.out.icon_seq == seq, "armed again: same request");
    w.in.body_act = 21;
    w.Tick();
    Check(w.st.phase == tv::Phase::native && w.out.hold && w.st.entered == 1 && w.out.icon == tv::Icon::none,
          "body 1 entered the ladder: native, held, no icon");
    const unsigned ticks = w.icon_ticks;
    w.Run(5000);
    Check(w.out.hold && w.icon_ticks == ticks, "held through the climb, no more icon");
    w.in.body_act = 3; w.in.remote_act = 3;
    w.Run(1000);
    Check(w.out.hold, "grace after the ladder");
    w.Run(700);
    Check(!w.out.hold && w.st.phase == tv::Phase::idle, "released after the grace");
    Check(w.places == 1, "exactly one placement");
}

// Close and facing: no placement, the icon at once; no entry aborts after entry_ms.
void TestNearAndTimeout() {
    World w;
    w.in.target[0] = 100.0; w.in.target_yaw = 0.1;
    w.Tick();
    w.in.remote_act = 24;
    w.Tick();
    Check(w.places == 0 && w.icon_ticks == 1 && w.out.icon == tv::Icon::step_on && w.st.phase == tv::Phase::request &&
              w.out.event == tv::Out::Event::start && std::strcmp(w.out.reason, "near") == 0,
          "near: the step-on icon without a placement");
    w.Run(1400);
    Check(w.out.icon == tv::Icon::step_on && w.st.phase == tv::Phase::request, "still requested within entry_ms");
    w.Run(200);
    Check(w.st.phase == tv::Phase::idle && w.st.aborted == 1 && !w.out.hold && w.out.icon == tv::Icon::none, "no entry: abort, no icon");
    const unsigned ticks = w.icon_ticks;
    w.Run(500);
    Check(w.icon_ticks == ticks, "nothing after the abort");
}

// Each episode gets its own request id; the id never repeats within a mission.
void TestSeqPerEpisode() {
    World w;
    w.in.target[0] = 100.0; w.in.target_yaw = 0.1;
    w.Tick();
    w.in.remote_act = 22;
    w.Tick();
    const std::uint32_t first = w.out.icon_seq;
    Check(w.out.icon == tv::Icon::pipe && first != 0, "pipe: first request");
    w.in.body_act = 22;
    w.Tick();
    Check(w.st.phase == tv::Phase::native && w.out.icon == tv::Icon::none, "entered the pipe");
    w.in.body_act = 2; w.in.remote_act = 2;
    w.Run(1600);
    Check(w.st.phase == tv::Phase::idle, "pipe episode over");
    w.in.remote_act = 23;
    w.Tick();
    Check(w.out.icon == tv::Icon::climb && w.out.icon_seq != first && w.out.icon_seq == first + 1, "climb: a new request id");
    Check(w.st.episodes == 2 && w.st.requests == 2, "two episodes, two requests");
}

// The wall climb (23) is replayed now; its sources are acts 1..20.
void TestClimbReplay() {
    World w;
    w.in.target[0] = 100.0; w.in.target_yaw = 0.1;
    w.in.body_act = 5;  // prone: a climb admits 1..20
    w.Tick();
    w.in.remote_act = 23;
    w.Tick();
    Check(w.out.icon == tv::Icon::climb, "climb from prone (5)");
    w.in.body_act = 23;
    w.Tick();
    Check(w.st.phase == tv::Phase::native && w.st.entered == 1, "body 1 climbs");
    World f;
    f.in.body_act = 25;
    f.Tick();
    f.in.remote_act = 23;
    f.Tick();
    Check(f.st.episodes == 0 && f.icon_ticks == 0, "falling (25): no climb request");
}

// Refusals: blocked, disabled, a body that is falling, far away, already there,
// and automatic remote acts.
void TestRefusals() {
    {
        World w; w.Tick(); w.in.blocked = true; w.in.remote_act = 21; w.Tick();
        Check(!w.out.request_place && w.out.icon == tv::Icon::none && !w.out.hold, "blocked: no replay");
    }
    {
        World w; w.Tick(); w.in.enabled = false; w.in.remote_act = 21; w.Tick();
        Check(!w.out.request_place && !w.out.hold, "disabled: no replay");
    }
    {
        World w; w.in.body_act = 25; w.Tick(); w.in.remote_act = 22; w.Tick();
        Check(!w.out.request_place && w.icon_ticks == 0, "body falling (25): no pipe replay");
    }
    {
        // The ladder mask admits a fall (25) but not prone (5) or a running step (8).
        World w; w.in.body_act = 25; w.Tick(); w.in.remote_act = 21; w.Tick();
        Check(w.out.request_place && w.st.episodes == 1, "body falling (25): a ladder replay (game mask)");
        for (std::uint16_t from : {std::uint16_t(5), std::uint16_t(8)}) {
            World p; p.in.body_act = from; p.Tick(); p.in.remote_act = 21; p.Tick();
            Check(!p.out.request_place && p.icon_ticks == 0 && p.st.episodes == 0, "prone or running step: no ladder replay");
        }
    }
    {
        // A pipe from prone is admitted (1..20): the ladder mask is the ladder's only.
        World w; w.in.body_act = 5; w.Tick(); w.in.remote_act = 22; w.Tick();
        Check(w.out.request_place && w.st.episodes == 1, "prone: a pipe replay");
    }
    {
        World w; w.in.target[0] = 20000.0; w.Tick(); w.in.remote_act = 21; w.Tick();
        Check(!w.out.request_place && w.st.episodes == 0, "too far: the reconciliation places normally");
    }
    {
        World w; w.in.body_act = 21; w.Tick();
        Check(w.st.phase == tv::Phase::native && w.icon_ticks == 0, "body already on the ladder: native hold, no icon");
    }
    {
        // Placement never verified: aborted, hold released.
        World w; w.Tick(); w.in.remote_act = 21; w.Tick();
        w.Run(1300);
        Check(w.st.phase == tv::Phase::idle && w.st.aborted == 1 && !w.out.hold && w.icon_ticks == 0, "placement timeout aborts");
    }
    {
        // Abort mid-episode when a carry takes the body.
        World w; w.in.target[0] = 100.0; w.in.target_yaw = 0.1; w.Tick(); w.in.remote_act = 21; w.Tick();
        Check(w.out.icon == tv::Icon::ladder, "requesting");
        w.in.blocked = true; w.Tick();
        Check(w.st.phase == tv::Phase::idle && w.out.event == tv::Out::Event::abort && !w.out.hold && w.out.icon == tv::Icon::none,
              "blocked mid-episode: abort, no icon");
    }
    {
        // The remote's automatic acts are never requested (falls, landing, evade, drop, hang).
        for (std::uint16_t a : {std::uint16_t(25), std::uint16_t(26), std::uint16_t(27), std::uint16_t(28), std::uint16_t(54),
                                std::uint16_t(55), std::uint16_t(56)}) {
            World w; w.in.target[0] = 100.0; w.in.target_yaw = 0.1; w.Tick(); w.in.remote_act = a; w.Run(500);
            Check(w.icon_ticks == 0 && w.st.episodes == 0 && w.places == 0, "automatic remote act: no request, no placement");
        }
    }
    {
        // A body act outside the plugin's sources drops the level (Exec would refuse it anyway).
        World w; w.in.target[0] = 100.0; w.in.target_yaw = 0.1; w.Tick(); w.in.remote_act = 21; w.Tick();
        Check(w.out.icon == tv::Icon::ladder, "requesting from 2");
        w.in.body_act = 5; w.Tick();
        Check(w.out.icon == tv::Icon::none && w.st.phase == tv::Phase::request, "prone while waiting: no ladder icon");
    }
}

// A native ledge grab (54) is held; the remote climbing up (54 -> 24) requests
// the step-on from the hang.
void TestHangClimbUp() {
    World w;
    w.in.body_act = 54; w.in.remote_act = 54;
    w.in.target[0] = 50.0; w.in.target_yaw = 0.0;
    w.Tick();
    Check(w.st.phase == tv::Phase::native && w.out.hold, "native hang held");
    w.Run(500);
    w.in.remote_act = 24;
    w.Tick();
    Check(w.out.icon == tv::Icon::step_on && w.st.phase == tv::Phase::request, "remote climbs up: the step-on icon from the hang");
    w.in.body_act = 24;
    w.Tick();
    Check(w.st.phase == tv::Phase::native && w.out.icon == tv::Icon::none, "body 1 climbs up");
}

// Body 1 left on a ladder the remote left long ago: released.
void TestRemoteLeft() {
    World w;
    w.in.body_act = 21; w.in.remote_act = 21;
    w.Tick();
    w.in.remote_act = 2;
    w.Run(2900);
    Check(w.out.hold, "still held shortly after the remote left");
    w.Run(300);
    Check(!w.out.hold && w.st.phase == tv::Phase::idle, "released 3 s after the remote left");
}

} // namespace

int main() {
    TestActSets();
    TestLadderReplay();
    TestNearAndTimeout();
    TestSeqPerEpisode();
    TestClimbReplay();
    TestRefusals();
    TestHangClimbUp();
    TestRemoteLeft();
    std::printf("%u partner traverse checks passed: act sets (21..24 replayed, IconFor, per-kind source acts, automatic acts never "
                "requested), ladder replay (one placement, icon level after verify with one id, native hold, grace), near request "
                "and entry timeout, one request id per episode, climb replay, refusals, hang climb-up, remote left\n",
                checks);
    return 0;
}
