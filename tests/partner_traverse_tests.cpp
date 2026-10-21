// partner_traverse: the traversal replay machine (ladder/pipe/step-on ACTION
// replay with a verified placement, bounded taps and the reconciliation hold).
#include <cstdio>
#include <cstdlib>
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
    unsigned taps = 0, places = 0;
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
        if (out.tap) ++taps;
        if (out.request_place) ++places;
        return out;
    }
    void Run(std::uint32_t ms) { for (std::uint32_t t = 0; t < ms; t += 16) Tick(); }
};

void TestActSets() {
    // Static writer census (1.0.15.4): 21 ladder, 22 pipe, 24 step-on (ACTION starts).
    Check(tv::ReplayAct(21) && tv::ReplayAct(22) && tv::ReplayAct(24), "replay acts 21/22/24");
    Check(!tv::ReplayAct(28) && !tv::ReplayAct(54) && !tv::ReplayAct(23), "hop/hang/climb are native (no replay)");
    // 49/50/59 are Fulton plugins, 72/73 doors, 77/78 box: never traversals.
    const std::uint16_t notTrav[] = {49, 50, 59, 72, 73, 77, 78, 1, 2, 25, 26, 27, 57, 112};
    for (std::uint16_t a : notTrav) Check(!tv::TraverseAct(a) && !tv::ReplayAct(a), "not a traversal act");
    const std::uint16_t trav[] = {21, 22, 23, 24, 28, 54, 55, 56};
    for (std::uint16_t a : trav) Check(tv::TraverseAct(a), "traversal act");
    Check(tv::SourceAct(1) && tv::SourceAct(8) && tv::SourceAct(54) && !tv::SourceAct(25) && !tv::SourceAct(139), "source acts");
}

// Remote 2 -> 21: one placement, then one tap only after the placement
// verified with the window armed; body 1 enters 21; the hold lasts until body
// 1 left the ladder plus the grace.
void TestLadderReplay() {
    World w;
    w.Tick();
    Check(!w.out.hold && w.st.phase == tv::Phase::idle, "idle while both walk");
    w.in.remote_act = 21;
    w.Tick();
    Check(w.out.request_place && w.out.hold && w.out.event == tv::Out::Event::start && std::strcmp(w.out.reason, "placed") == 0,
          "edge into 21: request a placement, hold");
    Check(w.taps == 0, "no tap before the placement");
    // The reconciliation places/verifies (window neutral or disarmed).
    w.in.placing = true; w.in.armed = false;
    w.Tick(); w.Tick();
    Check(w.taps == 0 && w.st.phase == tv::Phase::place, "no tap while placing");
    w.in.placing = false;
    w.Tick();
    Check(w.taps == 0, "no tap until the window is armed again");
    w.in.armed = true;
    w.Tick();
    Check(w.st.phase == tv::Phase::tap && w.taps == 1, "placed and armed: one tap");
    w.Tick();
    Check(w.taps == 1, "no repeat before retap_ms");
    w.in.body_act = 21;
    w.Tick();
    Check(w.st.phase == tv::Phase::native && w.out.hold && w.st.entered == 1, "body 1 entered the ladder: native, held");
    w.Run(5000);
    Check(w.out.hold && w.taps == 1, "held through the climb, no more taps");
    w.in.body_act = 3; w.in.remote_act = 3;
    w.Run(1000);
    Check(w.out.hold, "grace after the ladder");
    w.Run(700);
    Check(!w.out.hold && w.st.phase == tv::Phase::idle, "released after the grace");
    Check(w.places == 1, "exactly one placement");
}

// Close and facing: no placement, tap at once; bounded retaps; no entry aborts.
void TestNearAndRetaps() {
    World w;
    w.in.target[0] = 100.0; w.in.target_yaw = 0.1;
    w.Tick();
    w.in.remote_act = 24;
    w.Tick();
    Check(w.places == 0 && w.taps == 1 && std::strcmp(w.st.phase == tv::Phase::tap ? "tap" : "x", "tap") == 0, "near: tap without placement");
    w.Run(400);
    Check(w.taps == 2, "a retap after retap_ms while the remote stays in 24");
    w.Run(400);
    Check(w.taps == 3, "third tap");
    w.Run(600);
    Check(w.taps == 3, "taps bounded at 3");
    w.Run(400);
    Check(w.st.phase == tv::Phase::idle && w.st.aborted == 1 && !w.out.hold, "no entry: abort, hold released");
}

// Refusals: blocked, disabled, a body that is falling, far away, already there.
void TestRefusals() {
    {
        World w; w.Tick(); w.in.blocked = true; w.in.remote_act = 21; w.Tick();
        Check(!w.out.request_place && !w.out.tap && !w.out.hold, "blocked: no replay");
    }
    {
        World w; w.Tick(); w.in.enabled = false; w.in.remote_act = 21; w.Tick();
        Check(!w.out.request_place && !w.out.hold, "disabled: no replay");
    }
    {
        World w; w.in.body_act = 25; w.Tick(); w.in.remote_act = 21; w.Tick();
        Check(!w.out.request_place && !w.out.tap, "body falling (25): no replay");
    }
    {
        World w; w.in.target[0] = 20000.0; w.Tick(); w.in.remote_act = 21; w.Tick();
        Check(!w.out.request_place && w.st.episodes == 0, "too far: the reconciliation places normally");
    }
    {
        World w; w.in.body_act = 21; w.Tick();
        Check(w.st.phase == tv::Phase::native && w.taps == 0, "body already on the ladder: native hold, no tap");
    }
    {
        // Placement never verified: aborted, hold released.
        World w; w.Tick(); w.in.remote_act = 21; w.Tick();
        w.Run(1300);
        Check(w.st.phase == tv::Phase::idle && w.st.aborted == 1 && !w.out.hold && w.taps == 0, "placement timeout aborts");
    }
    {
        // Abort mid-episode when a carry takes the body.
        World w; w.Tick(); w.in.remote_act = 21; w.Tick();
        w.in.blocked = true; w.Tick();
        Check(w.st.phase == tv::Phase::idle && w.out.event == tv::Out::Event::abort && !w.out.hold, "blocked mid-episode: abort");
    }
}

// A native ledge grab (54) is held; the remote climbing up (54 -> 24) replays
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
    Check(w.taps == 1 && w.st.phase == tv::Phase::tap, "remote climbs up: tap from the hang");
    w.in.body_act = 24;
    w.Tick();
    Check(w.st.phase == tv::Phase::native, "body 1 climbs up");
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
    TestNearAndRetaps();
    TestRefusals();
    TestHangClimbUp();
    TestRemoteLeft();
    std::printf("%u partner traverse checks passed: act sets (static writer census), ladder replay (one placement, tap after "
                "verify, native hold, grace), near tap and bounded retaps, refusals, hang climb-up, remote left\n", checks);
    return 0;
}
