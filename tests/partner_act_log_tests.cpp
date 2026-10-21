// Act census telemetry (partner_act_log): edge lines, match latency, misses,
// the cap and its summary, and the dropped pad presses per second. Pure
// fixtures; no game, no Win32.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "mgscoop/partner_act_log.hpp"

namespace {
namespace pal = mgscoop::partner_act_log;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
bool Is(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
std::string Text(const pal::Line& l) {
    char buffer[256]{};
    Check(pal::FormatLine(l, buffer, sizeof(buffer)) > 0, "line formats");
    return buffer;
}

struct Act {
    pal::ActState s{};
    std::uint64_t now = 10000;
    pal::ActOut out{};
    Act() { pal::Reset(s); }
    pal::ActOut Step(bool rv, std::uint16_t r, bool bv, std::uint16_t b, std::uint64_t dt = 16, std::uint32_t cap = pal::kActLineCap) {
        now += dt;
        pal::ActIn in{};
        in.now_ms = now;
        in.tick_ms = now + 1000000;
        in.remote_valid = rv;
        in.remote_act = r;
        in.body_valid = bv;
        in.body_act = b;
        out = pal::Step(s, in, cap);
        return out;
    }
};

void TestNames() {
    // Critic 3.6: 3 crouch idle, 4 crouch move, 5 prone idle, 6 prone crawl; 57 dive, 58 Quiet's evade.
    Check(Is(pal::ActName(1), "stand_idle") && Is(pal::ActName(2), "stand_move") && Is(pal::ActName(3), "crouch_idle") &&
              Is(pal::ActName(4), "crouch_move") && Is(pal::ActName(5), "prone_idle") && Is(pal::ActName(6), "prone_crawl"),
          "locomotion names (critic 3.6)");
    Check(Is(pal::ActName(57), "dive") && Is(pal::ActName(58), "evade_quiet") && Is(pal::ActName(27), "evade") && Is(pal::ActName(139), "dm") &&
              Is(pal::ActName(111), "vehicle_seated") && Is(pal::ActName(132), "sleep") && Is(pal::ActName(43), "carried"),
          "verified names have no '?'");
    // The static writer census (1.0.15.4): 21 ladder, 22 pipe, 23 wall climb, 24 step-on, 28 cliff drop, 75/76 cure,
    // 79..83 trash box (not heli, ladder climb, fall entry, hop, knockdown or stun).
    Check(Is(pal::ActName(21), "ladder") && Is(pal::ActName(22), "pipe") && Is(pal::ActName(23), "wall_climb") &&
              Is(pal::ActName(24), "step_on") && Is(pal::ActName(28), "cliff_drop") && Is(pal::ActName(75), "cure") &&
              Is(pal::ActName(76), "cure") && Is(pal::ActName(79), "trash_box") && Is(pal::ActName(83), "trash_box"),
          "writer-census names have no '?'");
    Check(Is(pal::ActName(55), "ledge_hang?") && Is(pal::ActName(77), "cbox?") &&
              Is(pal::ActName(70), "cover?") && Is(pal::ActName(93), "cqc_hold?") && Is(pal::ActName(104), "heli") &&
              Is(pal::ActName(124), "walker?") && Is(pal::ActName(29), "hit?"),
          "uncertain families end in '?'");
    Check(Is(pal::ActName(10), "?") && Is(pal::ActName(145), "?") && Is(pal::ActName(0xFFFF), "?") && Is(pal::ActName(0), "none"), "unknown acts");
    for (unsigned a = 0; a < 0x10000; ++a) Check(pal::ActName(static_cast<std::uint16_t>(a)) != nullptr, "every act has a name");
}

void TestEdges() {
    Act a;
    a.Step(true, 1, true, 1);
    Check(a.out.count == 2 && a.out.lines[0].edge == pal::Edge::remote && a.out.lines[0].match_ms == 0 &&
              a.out.lines[1].edge == pal::Edge::body, "first observation: one remote and one body line");
    a.Step(true, 1, true, 1);
    Check(a.out.count == 0, "no change: no line");
    // The remote dives; the body follows 120 ms later.
    a.Step(true, 57, true, 1);
    Check(a.out.count == 1 && a.out.lines[0].edge == pal::Edge::remote && a.out.lines[0].match_ms == pal::kMatchPending,
          "remote edge: pending");
    Check(Text(a.out.lines[0]) == "partner_act; t=" + std::to_string(a.now + 1000000) +
                                      "; edge=remote; remote=57(dive); body=1(stand_idle); match_ms=pending\r\n",
          "remote line format");
    for (int i = 0; i < 7; ++i) a.Step(true, 57, true, 1);
    a.Step(true, 57, true, 57, 8);
    Check(a.out.count == 1 && a.out.lines[0].edge == pal::Edge::body && a.out.lines[0].match_ms == 120 && a.s.matched == 1,
          "body matched after 120 ms");
    Check(Text(a.out.lines[0]).find("edge=body; remote=57(dive); body=57(dive); match_ms=120") != std::string::npos, "body line format");
    // The remote leaves to 6 while the body stays in 57, then 1: 6 is missed.
    a.Step(true, 6, true, 57);
    Check(a.out.count == 1 && a.out.lines[0].match_ms == pal::kMatchPending, "6 pending");
    a.Step(true, 1, true, 57, 400);
    Check(a.out.count == 2 && a.out.lines[0].edge == pal::Edge::miss && a.out.lines[0].remote == 6 && a.out.lines[0].held_ms == 400 &&
              a.out.lines[1].edge == pal::Edge::remote && a.out.lines[1].remote == 1 && a.s.missed == 1,
          "remote left an unmatched act: miss line first");
    Check(Text(a.out.lines[0]).find("edge=miss; remote=6(prone_crawl); body=57(dive); match_ms=miss; held_ms=400") != std::string::npos,
          "miss line format");
    a.Step(true, 1, true, 1);
    Check(a.out.count == 1 && a.out.lines[0].match_ms >= 0, "body back to 1: matched");
    // A remote act held unmatched for the timeout.
    a.Step(true, 77, true, 1);
    a.Step(true, 77, true, 1, pal::kMatchTimeoutMs - 17);
    Check(a.out.count == 0, "before the timeout");
    a.Step(true, 77, true, 1, 20);
    Check(a.out.count == 1 && a.out.lines[0].edge == pal::Edge::miss && a.out.lines[0].remote == 77, "timeout miss");
    a.Step(true, 77, true, 1, 5000);
    Check(a.out.count == 0, "one miss per remote act");
    // The body led: the remote edge reports 0.
    a.Step(true, 77, true, 3);
    a.Step(true, 3, true, 3);
    Check(a.out.count == 1 && a.out.lines[0].match_ms == 0, "body already there: match_ms=0");
    // Invalid samples never make edges; a hidden body (invalid) does not match.
    a.Step(false, 0, false, 0);
    Check(a.out.count == 0, "invalid: no lines");
    a.Step(true, 109, false, 0);
    Check(a.out.count == 1 && Text(a.out.lines[0]).find("remote=109(vehicle_in); body=na; match_ms=pending") != std::string::npos,
          "remote edge while the body is hidden");
    a.Step(true, 109, true, 3);
    Check(a.out.count == 0, "same body act after re-show: no body edge");
}

void TestCap() {
    Act a;
    unsigned lines = 0;
    bool capped = false;
    unsigned summaries = 0;
    for (int i = 0; i < 200; ++i) {
        a.Step(true, static_cast<std::uint16_t>(1 + (i % 2)), true, 1, 100, 50);
        lines += a.out.count;
        capped = capped || a.out.capped_now;
        if (a.out.summary) {
            ++summaries;
            char text[200]{};
            Check(pal::FormatSummary(a.out, text, sizeof(text)) > 0 && std::strstr(text, "partner_act=summary; remote_edges=") == text,
                  "summary format");
        }
    }
    Check(lines == 50 && capped && a.s.dropped > 0, "own cap: 50 lines, then dropped and counted");
    Check(summaries >= 12 && summaries <= 20, "a summary at most every second while capped");
}

void TestPad() {
    pal::PadState s{};
    pal::Reset(s);
    std::uint64_t now = 5000;
    std::uint32_t seq = 0;
    const auto frame = [&](std::uint32_t held, std::uint32_t press, std::uint8_t edge, std::uint32_t mask, bool native = true,
                           std::uint8_t version = 6, std::uint64_t dt = 33) {
        now += dt;
        pal::PadIn in{};
        in.now_ms = now;
        in.valid = true;
        in.seq = ++seq;
        in.version = version;
        in.held = held;
        in.press = press;
        in.edge_seq = edge;
        in.mask = mask;
        in.native = native;
        return pal::PadStep(s, in);
    };
    const std::uint32_t mask = 0x224005Eu;  // the gun stage
    Check(pal::GroupBits(pal::kWeapon) == 0x7800u && pal::GroupBits(pal::kCqc) == 0x8000u && Is(pal::GroupName(pal::kVehicleFire), "vfire"),
          "groups");
    auto o = frame(0x20, 0x20, 1, mask);  // ACTION pressed (edge set 1)
    o = frame(0x20, 0x20, 1, mask);       // the edge set repeated
    o = frame(0, 0, 0, mask);
    o = frame(0x8000, 0x8000, 2, mask);   // CQC
    o = frame(0x2, 0x2, 3, mask);         // STANCE: passed, not counted
    o = frame(0x10, 0x10, 4, mask & ~0x10u);  // FIRE while the gate strips it
    o = frame(0x800, 0x800, 5, mask);     // weapon select
    o = frame(0x100001, 0x100001, 6, mask);  // VEHICLE_CALL + DECIDE: other
    pal::PadOut line{};
    for (int i = 0; i < 40 && !line.line; ++i) {
        o = frame(0, 0, 0, mask);
        if (o.line) line = o;
    }
    Check(line.line && line.counts[pal::kAction] == 1 && line.counts[pal::kCqc] == 1 && line.counts[pal::kFire] == 1 &&
              line.counts[pal::kWeapon] == 1 && line.counts[pal::kOther] == 1 && line.other_bits == 0x100001u && line.counts[pal::kStock] == 0 &&
              line.mask == mask && line.native,
          "one line per second: each press once, repeats not recounted, passed bits not counted");
    char text[300]{};
    Check(pal::FormatPad(line, text, sizeof(text)) > 0 &&
              std::string(text) == "partner_pad_dropped; action=1; stock=0; call=0; subject=0; weapon=1; cqc=1; light=0; vfire=0; marker=0; "
                                   "fire=1; other=1; other_bits=0x100001; mask=0x224005e; native=1\r\n",
          "pad line format");
    // A quiet second logs nothing.
    bool any = false;
    for (int i = 0; i < 60; ++i) any = any || frame(0, 0, 0, mask).line;
    Check(!any, "nothing dropped: no line");
    // v1 frames: rising held bits are the presses; the same frame (seq) twice counts once.
    pal::Reset(s);
    seq = 0;
    frame(0, 0, 0, mask, false, 1);
    frame(0x400, 0, 0, mask, false, 1);  // SUBJECT rises
    pal::PadIn same{};
    same.now_ms = now; same.valid = true; same.seq = seq; same.version = 1; same.held = 0x400; same.mask = mask;
    Check(pal::NewPresses(s, same) == 0, "the same frame twice adds nothing");
    line = pal::PadOut{};
    for (int i = 0; i < 40 && !line.line; ++i) {
        o = frame(0x400, 0, 0, mask, false, 1);
        if (o.line) line = o;
    }
    Check(line.line && line.counts[pal::kSubject] == 1 && !line.native, "v1: one SUBJECT press, native=0");
    // The cap.
    pal::Reset(s);
    seq = 0;
    unsigned lines = 0;
    bool capped = false;
    for (int i = 0; i < 40; ++i) {
        now += 1000;
        pal::PadIn in{};
        in.now_ms = now; in.valid = true; in.seq = ++seq; in.version = 6; in.held = 0x20; in.press = 0x20;
        in.edge_seq = static_cast<std::uint8_t>(1 + i % 200); in.mask = mask; in.native = true;
        const auto p = pal::PadStep(s, in, 5);
        lines += p.line ? 1u : 0u;
        capped = capped || p.capped_now;
    }
    Check(lines == 5 && capped, "pad line cap");
}

} // namespace

int main() {
    TestNames();
    TestEdges();
    TestCap();
    TestPad();
    std::printf("%u partner act log checks passed: act family names (critic 3.6), remote/body edges, match latency, misses (replaced, "
                "timeout), hidden body, own cap with per-second summaries, dropped pad presses by group (edge sets, v1 rising bits, FIRE "
                "outside the gate, other bits), formats, pad cap\n",
                checks);
    return 0;
}
