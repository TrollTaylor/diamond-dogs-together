// Melee on the partner (partner_melee): the attacker-side reach test of a local CQC strike on body 1
// (one press = its 113 edge and the 114 out of it, edges, reach / height / cone, the body facing convention, one hit per
// press and the repeat window, the partner's own state, the verdict line). Pure fixtures; no game, no Win32.
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
    unsigned queued = 0;  // every Step's queue, summed: one press must hand at most one hit
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
        const pm::Out o = pm::Step(s, in);
        if (o.queue) ++queued;
        return o;
    }
    // One press (the user's decision: one connecting CQC press knocks the partner down, as it does a soldier). Run
    // 1526e58a: one 120 ms LMB press with the partner 1.1 m in front gave 113 for 219 ms, then 114 for 549 ms. Returns the
    // press's 113 edge verdict.
    pm::Out Press() {
        Step(1);
        return Step(113);
    }
};

void TestActs() {
    Check(pm::StrikeAct(113) && pm::StrikeAct(114), "113 (the press's strike) and 114 (the strike out of it) are the strike acts");
    Check(!pm::StrikeAct(0) && !pm::StrikeAct(1) && !pm::StrikeAct(2) && !pm::StrikeAct(91) && !pm::StrikeAct(94) &&
          !pm::StrikeAct(111) && !pm::StrikeAct(112) && !pm::StrikeAct(131), "idle, move, CQC hold, vehicle and sleep acts are not strikes");
    Check(std::strcmp(pm::VerdictName(pm::Verdict::land), "land") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::range), "range") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::angle), "angle") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::repeat), "repeat") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::no_body), "no_body") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::height), "height") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::off), "off") == 0, "verdict names");
    Check(std::strcmp(pm::VerdictName(pm::Verdict::takedown), "takedown") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::busy), "busy") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::prone), "prone") == 0, "review verdict names: takedown, busy, prone");
    for (int a : {21, 22, 24, 54, 56, 79, 83, 89, 91, 94, 96}) Check(pm::PartnerBusyAct(static_cast<std::uint16_t>(a)), "ladder, pipe, ledge, box, toilet, CQC hold acts are busy");
    for (int a : {0, 1, 2, 5, 20, 25, 53, 57, 78, 90, 97, 113, 114}) Check(!pm::PartnerBusyAct(static_cast<std::uint16_t>(a)), "stand, move, crawl, fall, strike acts are not busy");
}

void TestOnePress() {
    Fixture f;
    Check(f.Step(1).verdict == pm::Verdict::none && f.Step(2).verdict == pm::Verdict::none, "no strike, no verdict");
    const auto hit = f.Step(113);
    Check(hit.verdict == pm::Verdict::land && hit.queue, "ONE press with the partner 1 m in front lands (the user's decision)");
    Check(hit.dist_cm == 100 && hit.dy_cm == 0 && hit.angle_deg == 0, "the press's distance, height and angle");
    for (int i = 0; i < 13; ++i) Check(f.Step(113).verdict == pm::Verdict::none, "a held 113 is one edge");
    const auto same = f.Step(114, 16);
    Check(same.verdict == pm::Verdict::repeat && !same.queue, "the 114 out of that press's 113 is no second hit (run 1526e58a)");
    for (int i = 0; i < 30; ++i) Check(f.Step(114, 18).verdict == pm::Verdict::none, "a held 114 is one edge");
    Check(f.queued == 1 && f.s.strikes == 2 && f.s.lands == 1, "one press: two strike edges, exactly one hit");
    // A press inside the repeat window of that land does not queue again; a press after it lands again.
    Check(f.Step(1, 100).verdict == pm::Verdict::none, "back to stand");
    const auto quick = f.Step(113, 100);
    Check(quick.verdict == pm::Verdict::repeat && !quick.queue, "a second press inside 1.5 s of the land is no second hit");
    f.Step(1, pm::kRepeatMs);
    const auto next = f.Step(113);
    Check(next.verdict == pm::Verdict::land && next.queue, "a press after the repeat window lands again");
    Check(f.Step(114, 219).verdict == pm::Verdict::repeat, "that press's 114 is still one hit");
    Check(f.queued == 2 && f.s.lands == 2, "two landed presses, two hits");
}

void TestStrikeOutOfThePress() {
    {
        // Run 9018ae47: the 113 at 142 cm, the 114 out of it at 84 cm (the strike steps forward). The 114 is the press's
        // strike too, so a 113 just out of reach followed by its 114 in reach is one hit.
        Fixture f; f.Partner(0.f, 0.f, 1.7f);
        Check(f.Press().verdict == pm::Verdict::range, "the press's 113 just out of reach");
        f.Partner(0.f, 0.f, 0.9f);
        const auto o = f.Step(114, 219);
        Check(o.verdict == pm::Verdict::land && o.queue && o.dist_cm == 90, "the 114 out of that 113 reaches: one hit");
        Check(f.queued == 1 && f.s.lands == 1, "exactly one hit for the press");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, 1.7f);
        Check(f.Press().verdict == pm::Verdict::range, "a 113 out of reach");
        Check(f.Step(114, 219).verdict == pm::Verdict::range && f.queued == 0, "its 114 still out of reach: no hit");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 93;
        Check(f.Press().verdict == pm::Verdict::busy, "a press at a busy partner");
        Check(f.Step(114, 219).verdict == pm::Verdict::busy && f.queued == 0, "its 114 at the still busy partner: no hit");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, 1.7f);
        Check(f.Press().verdict == pm::Verdict::range, "a 113 out of reach");
        f.in.local_valid = false;
        f.Step(1, 30);
        f.in.local_valid = true;
        f.Partner(0.f, 0.f, 1.f);
        Check(f.Step(114, 30).verdict == pm::Verdict::land && f.queued == 1, "a missed local read between the 113 and its 114 keeps the press");
    }
}

void TestTakedown() {
    {
        Fixture f;
        f.Step(2);
        const auto o = f.Step(114);
        Check(o.verdict == pm::Verdict::takedown && !o.queue, "running straight into 114 (a soldier takedown) is not a strike on the partner");
        for (int i = 0; i < 100; ++i) Check(f.Step(114, 30).verdict == pm::Verdict::none, "a held takedown is one edge");
        Check(f.queued == 0, "a takedown hands no hit");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, 2.f);
        f.Press();
        f.Step(1, 100);
        f.Partner(0.f, 0.f, 1.f);
        const auto o = f.Step(114, pm::kComboMs + 100);
        Check(o.verdict == pm::Verdict::takedown && !o.queue, "a 114 long after the last 113 is not the press's strike");
    }
    {
        Fixture f;
        Check(f.Press().verdict == pm::Verdict::land, "a press lands");
        f.in.local_valid = false;
        f.Step(1, 30);
        f.in.local_valid = true;
        const auto o = f.Step(114, 30);
        Check(o.verdict == pm::Verdict::repeat && !o.queue && f.queued == 1, "a missed local read between the 113 and its 114: same press, no takedown, no second hit");
    }
    {
        // A press that landed, then a soldier takedown (2 -> 114) after the repeat window: the takedown is not a hit.
        Fixture f;
        Check(f.Press().verdict == pm::Verdict::land, "a press lands");
        f.Step(2, pm::kRepeatMs + 100);
        Check(f.Step(114).verdict == pm::Verdict::takedown && f.queued == 1, "a later takedown hands nothing");
    }
}

void TestPartnerState() {
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 93;
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::busy && !o.queue, "the partner holding a soldier in CQC is not struck");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 80;
        Check(f.Press().verdict == pm::Verdict::busy, "the partner in a trash box is not struck");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 22;
        Check(f.Press().verdict == pm::Verdict::busy, "the partner on a ladder or pipe is not struck");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 5; f.in.remote_stance = 3;
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::prone && !o.queue, "a prone (or asleep) partner is not struck (a stock strike passes over)");
        Check(f.Step(114, 219).verdict == pm::Verdict::prone && f.queued == 0, "nor by the press's 114");
    }
    {
        Fixture f; f.in.remote_valid = false; f.in.remote_act = 93; f.in.remote_stance = 3;
        Check(f.Press().verdict == pm::Verdict::land, "no valid remote anim sample: the stale act and stance are not used");
    }
    {
        Fixture f; f.in.remote_valid = true; f.in.remote_act = 2; f.in.remote_stance = 1;
        Check(f.Press().verdict == pm::Verdict::land, "a standing, moving partner is struck");
    }
}

void TestMisses() {
    {
        Fixture f; f.Partner(0.f, 0.f, 2.0f);
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::range && !o.queue && o.dist_cm == 200, "2 m away: out of reach");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, 4.5f);
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::range && !o.queue && o.dist_cm == 450, "4.5 m away: out of reach");
        Check(f.Step(114, 219).verdict == pm::Verdict::range && f.queued == 0, "nor its 114");
    }
    {
        Fixture f; f.Partner(0.f, 1.5f, 0.5f);
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::height && !o.queue && o.dy_cm == 150, "1.5 m above: out of reach in height");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, -1.f);
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::angle && !o.queue && o.angle_deg == 180, "behind the attacker: outside the cone");
        Check(f.Step(114, 219).verdict == pm::Verdict::angle && f.queued == 0, "nor its 114");
    }
    {
        Fixture f; f.Partner(1.f, 0.f, 0.f);
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::angle && !o.queue && o.angle_deg == 90, "beside the attacker facing +z: outside the cone");
    }
    {
        Fixture f; f.in.yaw0 = 1.5707963f; f.Partner(1.f, 0.f, 0.f);  // facing +x: forward = (sin, cos)
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::land && o.queue && o.angle_deg == 0, "facing +x, the partner at +x lands");
    }
    {
        Fixture f; f.Partner(0.6f, 0.f, 0.8f);  // 1 m, about 37 degrees off the facing
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::land && o.queue && o.dist_cm == 100 && o.angle_deg == 37, "inside the 60 degree cone");
    }
    {
        Fixture f; f.in.body_valid = false;
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::no_body && !o.queue, "no realized, on-foot partner body: no hit");
    }
    {
        Fixture f; f.in.enabled = false;
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::off && !o.queue, "the key off: no hit");
        f.in.enabled = true;
        Check(f.Step(113).verdict == pm::Verdict::none, "a strike already running when the key turns on is not a new strike");
    }
    {
        Fixture f; f.in.local_valid = false;
        Check(f.Step(113).verdict == pm::Verdict::none && f.s.strikes == 0, "no local read: no strike");
        f.in.local_valid = true;
        Check(f.Step(113).verdict == pm::Verdict::land, "the first readable strike tick is the edge");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, 2.f);
        Check(f.Press().verdict == pm::Verdict::range, "a miss");
        f.Partner(0.f, 0.f, 1.f);
        f.Step(1, 100);
        const auto o = f.Press();
        Check(o.verdict == pm::Verdict::land && o.queue, "a miss does not open the repeat window: the next press lands");
    }
}

void TestFormat() {
    Fixture f;
    const auto o = f.Press();
    char line[256]{};
    Check(pm::FormatStrike(f.s, 113, o, true, line, sizeof(line)) > 0, "the strike line formats");
    Check(std::string(line) == "partner_melee=strike; act=113; verdict=land; queued=1; dist_cm=100; dy_cm=0; angle_deg=0; strikes=1; lands=1\r\n",
          "the strike line");
    char tiny[8]{};
    Check(pm::FormatStrike(f.s, 113, o, true, tiny, sizeof(tiny)) == 0 && tiny[0] == 0, "a short buffer formats nothing");
}

} // namespace

int main() {
    TestActs();
    TestOnePress();
    TestStrikeOutOfThePress();
    TestTakedown();
    TestPartnerState();
    TestMisses();
    TestFormat();
    std::printf("%u partner melee checks passed: one press (its 113 or the 114 out of it) in reach lands exactly one hit, "
                "a press inside 1.5 s of a land is no second hit, a 114 from any other act is a soldier takedown, a partner in a CQC "
                "hold / box / ladder / ledge or prone is not struck, reach 1.6 m, height 1 m, the 60 degree cone on the body facing, "
                "no body / key off / no local read, the strike line\n",
                checks);
    return 0;
}
