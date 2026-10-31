// Melee on the partner (partner_melee): the attacker-side reach test of a local CQC strike on body 1
// (strike acts, edges, reach / height / cone, the body facing convention, the repeat window, the
// verdict line). Pure fixtures; no game, no Win32.
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
};

void TestActs() {
    Check(pm::StrikeAct(113) && pm::StrikeAct(114), "113 and 114 are the strike acts");
    Check(!pm::StrikeAct(0) && !pm::StrikeAct(1) && !pm::StrikeAct(2) && !pm::StrikeAct(91) && !pm::StrikeAct(94) &&
          !pm::StrikeAct(111) && !pm::StrikeAct(112) && !pm::StrikeAct(131), "idle, move, CQC hold, vehicle and sleep acts are not strikes");
    Check(std::strcmp(pm::VerdictName(pm::Verdict::land), "land") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::range), "range") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::angle), "angle") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::repeat), "repeat") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::no_body), "no_body") == 0 && std::strcmp(pm::VerdictName(pm::Verdict::height), "height") == 0 &&
          std::strcmp(pm::VerdictName(pm::Verdict::off), "off") == 0, "verdict names");
}

void TestLand() {
    Fixture f;
    Check(f.Step(1).verdict == pm::Verdict::none && f.Step(2).verdict == pm::Verdict::none, "no strike, no verdict");
    const auto o = f.Step(113);
    Check(o.verdict == pm::Verdict::land && o.queue, "a strike edge with the partner 1 m in front lands");
    Check(o.dist_cm == 100 && o.dy_cm == 0 && o.angle_deg == 0, "distance, height and angle of the landed strike");
    Check(f.s.strikes == 1 && f.s.lands == 1, "counters after one land");
    for (int i = 0; i < 30; ++i) Check(f.Step(113).verdict == pm::Verdict::none, "a held strike act is one strike");
    const auto combo = f.Step(114);
    Check(combo.verdict == pm::Verdict::repeat && !combo.queue, "the combo's next strike inside 1.5 s does not queue again");
    Check(f.Step(1).verdict == pm::Verdict::none, "back to idle");
    const auto later = f.Step(113, pm::kRepeatMs);
    Check(later.verdict == pm::Verdict::land && later.queue, "a new strike after the repeat window lands again");
    Check(f.s.strikes == 3 && f.s.lands == 2, "counters after a repeat and a second land");
}

void TestMisses() {
    {
        Fixture f; f.Partner(0.f, 0.f, 2.0f);
        const auto o = f.Step(113);
        Check(o.verdict == pm::Verdict::range && !o.queue && o.dist_cm == 200, "2 m away: out of reach");
    }
    {
        Fixture f; f.Partner(0.f, 1.5f, 0.5f);
        const auto o = f.Step(113);
        Check(o.verdict == pm::Verdict::height && !o.queue && o.dy_cm == 150, "1.5 m above: out of reach in height");
    }
    {
        Fixture f; f.Partner(0.f, 0.f, -1.f);
        const auto o = f.Step(113);
        Check(o.verdict == pm::Verdict::angle && !o.queue && o.angle_deg == 180, "behind the attacker: outside the cone");
    }
    {
        Fixture f; f.Partner(1.f, 0.f, 0.f);
        const auto o = f.Step(113);
        Check(o.verdict == pm::Verdict::angle && !o.queue && o.angle_deg == 90, "beside the attacker facing +z: outside the cone");
    }
    {
        Fixture f; f.in.yaw0 = 1.5707963f; f.Partner(1.f, 0.f, 0.f);  // facing +x: forward = (sin, cos)
        const auto o = f.Step(113);
        Check(o.verdict == pm::Verdict::land && o.queue && o.angle_deg == 0, "facing +x, the partner at +x lands");
    }
    {
        Fixture f; f.Partner(0.6f, 0.f, 0.8f);  // 1 m, about 37 degrees off the facing
        const auto o = f.Step(113);
        Check(o.verdict == pm::Verdict::land && o.queue && o.dist_cm == 100 && o.angle_deg == 37, "inside the 60 degree cone");
    }
    {
        Fixture f; f.in.body_valid = false;
        const auto o = f.Step(113);
        Check(o.verdict == pm::Verdict::no_body && !o.queue, "no realized partner body: no hit");
    }
    {
        Fixture f; f.in.enabled = false;
        const auto o = f.Step(113);
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
        Check(f.Step(113).verdict == pm::Verdict::range, "a miss");
        f.Partner(0.f, 0.f, 1.f);
        const auto o = f.Step(114, 100);
        Check(o.verdict == pm::Verdict::land && o.queue, "a miss does not open the repeat window: the combo's next strike lands");
    }
}

void TestFormat() {
    Fixture f;
    const auto o = f.Step(113);
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
    TestLand();
    TestMisses();
    TestFormat();
    std::printf("%u partner melee checks passed: strike acts 113/114, edges (held act, combo), reach 1.6 m, height 1 m, the 60 degree "
                "cone on the body facing (forward = sin, cos), the 1.5 s repeat window (misses do not open it), no body / key off / "
                "no local read, the strike line\n",
                checks);
    return 0;
}
