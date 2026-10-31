// Melee on the partner (partner_melee): the attacker-side reach test of a local CQC strike on body 1
// (jab and finisher acts, edges, reach / height / cone, the body facing convention, the repeat window, the
// partner's own state, the verdict line). Pure fixtures; no game, no Win32.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "mgscoop/partner_melee.hpp"

namespace {
namespace pm = mgscoop::partner_melee;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

struct Fixture {
    pm::State s{};
    pm::In in{};
    Fixture() {
        pm::Reset(s);
        in.enabled = true;
        in.local_valid = true;
        in.body_valid = true;
        in.pos0[0] = 10.f; in.pos0[1] = 2.f; in.pos0[2] = -5.f;
        in.yaw0 = 0.f;  // facing +z
        Partner(0.f, 0.f, 1.f);
        in.now_ms = 100000;
    }
    void Partner(float dx, float dy, float dz) {
        in.pos1[0] = in.pos0[0] + dx; in.pos1[1] = in.pos0[1] + dy; in.pos1[2] = in.pos0[2] + dz;
    }
    pm::Out Step(std::uint16_t act, std::uint64_t dt = 16) {
        in.now_ms += dt;
        in.act = act;
        return pm::Step(s, in);
    }
    // One press is a 113 then its 114 (run 1526e58a: one 120 ms LMB press with the partner 1.1 m in front gave 113 for
    // 219 ms, then 114 for 549 ms). Two presses: returns the second press's 113 verdict (the one that lands in reach).
    pm::Out Combo() {
        Step(1);
        Step(113);
        Step(114, 200);
        Step(1, 300);
        return Step(113, 200);
    }
};

void TestActs() {
    Check(pm::StrikeAct(113) && pm::StrikeAct(114), "113 (jab) and 114 (finisher) are the strike acts");
    Check(!pm::StrikeAct(0) && !pm::StrikeAct(1) && !pm::StrikeAct(2) && !pm::StrikeAct(91) && !pm::StrikeAct(94) &&
          !pm::StrikeAct(111) && !pm::StrikeAct(112) && !pm::StrikeAct(131), "idle, move, CQC hold, vehicle and sleep acts are not strikes");
    Check(std::strcmp(pm::VerdictName(pm::Verdict::land), "land") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::range), "range") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::angle), "angle") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::repeat), "repeat") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::no_body), "no_body") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::height), "height") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::off), "off") == 0, "verdict names");
    Check(std::strcmp(pm::VerdictName(pm::Verdict::jab), "jab") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::takedown), "takedown") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::busy), "busy") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::prone), "prone") == 0,
          "review verdict names: jab, takedown, busy, prone");
    for (int a : {21, 22, 24, 54, 56, 79, 83, 89, 91, 94, 96}) Check(pm::PartnerBusyAct(static_cast<std::uint16_t>(a)), "ladder, pipe, ledge, box, toilet, CQC hold acts are busy");
    for (int a : {0, 1, 2, 5, 20, 25, 53, 57, 78, 90, 97, 113, 114}) Check(!pm::PartnerBusyAct(static_cast<std::uint16_t>(a)), "stand, move, crawl, fall, strike acts are not busy");
}

void TestJabAndFinisher() {
    Fixture f;
    Check(f.Step(1).verdict == pm::Verdict::none && f.Step(2).verdict == pm::Verdict::none, "no strike, no verdict");
    const auto jab = f.Step(113);
    Check(jab.verdict == pm::Verdict::jab && !jab.queue, "one jab with the partner 1 m in front does not queue (vanilla CQC feel)");
    Check(jab.dist_cm == 100 && jab.dy_cm == 0 && jab.angle_deg == 0, "the jab's distance, height and angle");
    for (int i = 0; i < 30; ++i) Check(f.Step(113).verdict == pm::Verdict::none, "a held jab is one edge");
    Check(f.Step(1).verdict == pm::Verdict::none, "back to stand between jabs");
    // Run af0b88b6: four scripted presses gave two 113 edges 1.6 s apart and no 114 on either copy, so two jabs in reach
    // within kPairMs land too (the coordinator's fallback); one jab alone never does.
    const auto jab2 = f.Step(113, 300);
    Check(jab2.verdict == pm::Verdict::land && jab2.queue && jab2.dist_cm == 100, "a second jab in reach within 2 s lands");
    const auto fin = f.Step(114, 200);
    Check(fin.verdict == pm::Verdict::repeat && !fin.queue, "the finisher inside 1.5 s of that land does not queue again");
    Check(f.s.strikes == 3 && f.s.lands == 1, "counters: two jabs, a finisher and one land");
    f.Step(1, pm::kRepeatMs);
    f.Step(1);
    Check(f.Step(113).verdict == pm::Verdict::jab, "after a land the pair starts over: one jab");
    const auto same = f.Step(114, 219);
    Check(same.verdict == pm::Verdict::jab && !same.queue, "one press (113 then its 114) never lands (run 1526e58a)");
    f.Step(1, 549);
    const auto next = f.Step(113, 200);
    Check(next.verdict == pm::Verdict::land && next.queue, "the next press in reach within 2 s lands");
    Check(f.Step(114, 219).verdict == pm::Verdict::repeat, "that press's 114 is inside the repeat window");
    f.Step(1, pm::kPairMs + 100);
    Check(f.Step(113).verdict == pm::Verdict::jab, "a second jab more than 2 s later is one jab again");
}

void TestJabPair() {
    {
        Fixture f; f.Partner(0.f, 0.f, 2.f);
        Check(f.Step(113).verdict == pm::Verdict::range, "a jab out of reach");
        f.Step(1, 300);
        f.Partner(0.f, 0.f, 1.f);
        Check(f.Step(113, 300).verdict == pm::Verdict::jab, "a jab out of reach does not count toward the pair");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 93;
        Check(f.Step(113).verdict == pm::Verdict::busy, "a jab at a busy partner");
        f.Step(1, 300);
        f.in.remote_act = 1;
        Check(f.Step(113, 300).verdict == pm::Verdict::jab, "a jab at a busy partner does not count toward the pair");
    }
    {
        Fixture f;
        Check(f.Step(113).verdict == pm::Verdict::jab, "a jab");
        f.in.local_valid = false;
        f.Step(1, 300);
        f.in.local_valid = true;
        Check(f.Step(113, 300).verdict == pm::Verdict::land, "a missed local read between two jabs keeps the pair");
    }
}

void TestTakedown() {
    {
        Fixture f;
        f.Step(2);
        const auto o = f.Step(114);
        Check(o.verdict == pm::Verdict::takedown && !o.queue, "running straight into 114 (a soldier takedown) is not a strike on the partner");
        for (int i = 0; i < 100; ++i) Check(f.Step(114, 30).verdict == pm::Verdict::none, "a held takedown is one edge");
    }
    {
        Fixture f;
        f.Step(1);
        f.Step(113);
        f.Step(1, 100);
        const auto o = f.Step(114, pm::kComboMs + 100);
        Check(o.verdict == pm::Verdict::takedown && !o.queue, "a 114 long after the last jab is not the finisher");
    }
    {
        Fixture f;
        f.Step(1);
        f.Step(113);
        f.Step(1, 100);
        const auto o = f.Step(114, 300);
        Check(o.verdict == pm::Verdict::jab && !o.queue, "a 114 within the combo window of the last jab is that press's strike: no land alone");
    }
    {
        Fixture f;
        f.Step(113);
        f.in.local_valid = false;
        f.Step(1, 30);
        f.in.local_valid = true;
        const auto o = f.Step(114, 30);
        Check(o.verdict == pm::Verdict::jab, "a missed local read between the jab and its 114 keeps the press (no takedown)");
    }
}

void TestPartnerState() {
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 93;
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::busy && !o.queue, "the partner holding a soldier in CQC is not struck");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 80;
        Check(f.Combo().verdict == pm::Verdict::busy, "the partner in a trash box is not struck");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 22;
        Check(f.Combo().verdict == pm::Verdict::busy, "the partner on a ladder or pipe is not struck");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 5; f.in.remote_stance = 3;
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::prone && !o.queue, "a prone partner is not struck (a stock strike passes over)");
    }
    {
        Fixture f; f.in.remote_valid = false; f.in.remote_act = 93; f.in.remote_stance = 3;
        Check(f.Combo().verdict == pm::Verdict::land, "no valid remote anim sample: the stale act and stance are not used");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 2; f.in.remote_stance = 1;
        Check(f.Combo().verdict == pm::Verdict::land, "a standing, moving partner is struck");
    }
}

void TestMisses() {
    {
        Fixture f; f.Partner(0.f, 0.f, 2.0f);
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::range && !o.queue && o.dist_cm == 200, "2 m away: out of reach");
    }
    {
        Fixture f; f.Partner(0.f, 1.5f, 0.5f);
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::height && !o.queue && o.dy_cm == 150, "1.5 m above: out of reach in height");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, -1.f);
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::angle && !o.queue && o.angle_deg == 180, "behind the attacker: outside the cone");
    }
    {
        Fixture f; f.Partner(1.f, 0.f, 0.f);
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::angle && !o.queue && o.angle_deg == 90, "beside the attacker facing +z: outside the cone");
    }
    {
        Fixture f; f.in.yaw0 = 1.5707963f; f.Partner(1.f, 0.f, 0.f);  // facing +x: forward = (sin, cos)
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::land && o.queue && o.angle_deg == 0, "facing +x, the partner at +x lands");
    }
    {
        Fixture f; f.Partner(0.6f, 0.f, 0.8f);  // 1 m, about 37 degrees off the facing
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::land && o.queue && o.dist_cm == 100 && o.angle_deg == 37, "inside the 60 degree cone");
    }
    {
        Fixture f; f.in.body_valid = false;
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::no_body && !o.queue, "no realized, on-foot partner body: no hit");
    }
    {
        Fixture f; f.in.enabled = false;
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::off && !o.queue, "the key off: no hit");
        f.in.enabled = true;
        Check(f.Step(113).verdict == pm::Verdict::none, "a strike already running when the key turns on is not a new strike");
    }
    {
        Fixture f; f.in.local_valid = false;
        Check(f.Step(113).verdict == pm::Verdict::none && f.s.strikes == 0, "no local read: no strike");
        f.in.local_valid = true;
        Check(f.Step(113).verdict == pm::Verdict::jab, "the first readable strike tick is the edge");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, 2.f);
        Check(f.Combo().verdict == pm::Verdict::range, "a miss");
        f.Partner(0.f, 0.f, 1.f);
        f.Step(1, 100);
        const auto o = f.Combo();
        Check(o.verdict == pm::Verdict::land && o.queue, "a miss does not open the repeat window: the next combo lands");
    }
}

void TestFormat() {
    Fixture f;
    const auto o = f.Combo();
    char line[256]{};
    Check(pm::FormatStrike(f.s, 113, o, true, line, sizeof(line)) > 0, "the strike line formats");
    Check(std::string(line) == "partner_melee=strike; act=113; verdict=land; queued=1; dist_cm=100; dy_cm=0; angle_deg=0; strikes=3; lands=1\r\n",
          "the strike line");
    char tiny[8]{};
    Check(pm::FormatStrike(f.s, 113, o, true, tiny, sizeof(tiny)) == 0 && tiny[0] == 0, "a short buffer formats nothing");
}

} // namespace

int main() {
    TestActs();
    TestJabAndFinisher();
    TestJabPair();
    TestTakedown();
    TestPartnerState();
    TestMisses();
    TestFormat();
    std::printf("%u partner melee checks passed: one press (113 and its 114) logs and never queues, the next press "
                "in reach within 2 s lands, a 114 from any other act is a soldier takedown, a partner in a CQC hold / box / ladder / ledge or prone "
                "is not struck, reach 1.6 m, height 1 m, the 60 degree cone on the body facing, the 1.5 s repeat window, no body "
                "/ key off / no local read, the strike line\n",
                checks);
    return 0;
}
