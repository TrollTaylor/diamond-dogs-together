// Round 3 input lever: the portable pad logic (partner_input). No game.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "mgscoop/partner_input.hpp"

namespace {
namespace pi = mgscoop::partner_input;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
bool Near(double a, double b, double eps = 1e-3) { return std::fabs(a - b) < eps; }
constexpr double kPi = 3.14159265358979323846;

void TestEffective() {
    pi::PadRaw raw{};
    // Stick pad: src words and floats, no masks, no keyboard override.
    raw.src_w[0] = 0x12; raw.src_w[1] = 0x34; raw.src_w[2] = 0x50; raw.src_w[3] = 0x6;
    raw.src_f[0] = 0.5f; raw.src_f[1] = -0.5f; raw.src_f[2] = 0.25f; raw.src_f[3] = 0.1f; raw.src_f[4] = 0.7f; raw.src_f[5] = 0.3f;
    raw.pp_derived[0] = 0.7f; raw.pp_derived[1] = 0.6f; raw.pp_derived[2] = 1.5f;
    pi::PadSnapshot s = pi::Effective(raw);
    Check(s.held == 0x12 && s.w4c == 0x34 && s.w50 == 0x50 && s.w54 == 0x6, "src words pass through");
    Check(Near(s.lx, 0.5) && Near(s.ly, -0.5) && Near(s.lt, 0.7), "src floats pass through");
    Check(s.flags == pi::Flag::Valid, "no mask/keyboard: only Valid");
    // Keyboard fallback: src word 0 -> PP+0x460; keyboard flags set.
    raw.src_w[0] = 0; raw.kb_w[0] = 0x80;
    raw.kb_flags = 0x01; raw.kb_f[0] = -1.0f; raw.kb_f[2] = 1.0f;  // LX override, LY override
    s = pi::Effective(raw);
    Check(s.held == 0x80 && (s.flags & pi::Flag::SenderKeyboard), "keyboard word fallback and flag");
    Check(Near(s.lx, -1.0) && Near(s.ly, 1.0), "PP+0x488&1: kb LX/LY overrides");
    // Button mask clears held/word bits; the masked flag is set.
    raw = pi::PadRaw{};
    raw.src_w[0] = 0xF; raw.src_w[1] = 0xF; raw.pp_mask[0] = 0x3;
    s = pi::Effective(raw);
    Check(s.held == 0xC && s.w4c == 0xC && (s.flags & pi::Flag::SenderMasked), "button mask clears bits");
    // Stick mask zeroes the left stick; trigger mask zeroes triggers.
    raw = pi::PadRaw{};
    raw.src_f[0] = 0.4f; raw.src_f[4] = 0.9f; raw.pp_mask[1] = 0x1; raw.pp_mask[2] = 0x1;
    s = pi::Effective(raw);
    Check(s.lx == 0.0f && s.lt == 0.0f && (s.flags & pi::Flag::SenderMasked), "stick/trigger masks zero axes");
}

void TestDirCheck() {
    pi::PadSnapshot s{};
    s.mag = 0.5f; s.lx = 0.5f; s.ly = 0.5f; s.dir = static_cast<float>(std::atan2(0.5, 0.5));
    Check(pi::CheckDir(s, 0) == pi::DirState::ok, "dir matches atan2(lx,ly)");
    s.dir = 0.0f;
    Check(pi::CheckDir(s, 0) == pi::DirState::bad, "wrong dir");
    s.mag = 0.0f;
    Check(pi::CheckDir(s, 0) == pi::DirState::na, "no magnitude: na");
    s.mag = 0.5f;
    Check(pi::CheckDir(s, 0x1) == pi::DirState::na, "left stick masked: na");
}

void TestQuantize() {
    pi::PadSnapshot s{};
    s.flags = pi::Flag::Valid;
    s.lx = 1.5f; s.ly = -2.0f; s.lt = 2.0f; s.mag = 0.5f; s.force = 0.25f; s.dir = static_cast<float>(kPi);
    pi::InputPacket p = pi::Quantize(s, 40000);
    Check(p.lx == 32767 && p.ly == -32767 && p.lt == 255, "axis/trigger clamps, no -32768");
    // float pi slightly exceeds double pi, so it normalizes to -pi; either end clamps to kMaxDir.
    Check(std::abs(p.ldir) == pi::kMaxDir, "ldir clamps at +-pi");
    Check(p.cam_cdeg == pi::NormalizeCdeg(40000) && p.cam_cdeg >= pi::kCamMin && p.cam_cdeg <= pi::kCamMax, "cam normalized");
    Check(pi::ValidPacket(p), "quantized packet is valid");
    // Round trip within 1/32767.
    s.lx = 0.321f; s.ly = -0.654f;
    p = pi::Quantize(s, 0);
    Check(Near(p.lx / 32767.0, 0.321, 1.0 / 32767) && Near(p.ly / 32767.0, -0.654, 1.0 / 32767), "axis round trip");
    // Non-finite -> 0.
    s.lx = std::nanf(""); s.dir = INFINITY;
    p = pi::Quantize(s, 0);
    Check(p.lx == 0 && p.ldir == 0, "non-finite becomes 0");
    // Invalid packets rejected.
    pi::InputPacket bad = pi::Quantize(pi::PadSnapshot{}, 0);
    bad.flags = 0x08;
    Check(!pi::ValidPacket(bad), "unknown flag bit rejected");
    bad = pi::Quantize(pi::PadSnapshot{}, 0); bad.ldir = 31417;
    Check(!pi::ValidPacket(bad), "ldir out of range rejected");
    bad = pi::Quantize(pi::PadSnapshot{}, 0); bad.cam_cdeg = 18000;
    Check(!pi::ValidPacket(bad), "cam out of range rejected");
}

pi::Config Cfg(pi::Word w0 = pi::Word::zero, pi::Word w1 = pi::Word::zero, pi::Word w2 = pi::Word::zero) {
    pi::Config c{};
    c.mode = mgscoop::PartnerInput::net;
    c.mask = 0xFFFFFFFFu;
    c.words[0] = w0; c.words[1] = w1; c.words[2] = w2;
    c.yaw_sign = 1;
    c.stale_ms = 250;
    return c;
}

void TestCompose() {
    pi::EdgeState edge{};
    // pass / off: no write.
    pi::Config c = Cfg();
    c.mode = mgscoop::PartnerInput::pass;
    Check(!pi::Compose(c, pi::Sample{}, 0, pi::Overlay{}, edge).write, "pass: no write");
    c.mode = mgscoop::PartnerInput::off;
    Check(!pi::Compose(c, pi::Sample{}, 0, pi::Overlay{}, edge).write, "off: no write");
    // neutral: zeros.
    c.mode = mgscoop::PartnerInput::neutral;
    pi::Injection j = pi::Compose(c, pi::Sample{}, 0, pi::Overlay{}, edge);
    Check(j.write && j.held == 0 && j.lx == 0.0f && j.ly == 0.0f && !j.stale, "neutral: zeros");
    // stick 180:100 -> (0, -1), dir pi.
    c.mode = mgscoop::PartnerInput::stick; c.stick_deg = 180; c.stick_pct = 100;
    j = pi::Compose(c, pi::Sample{}, 0, pi::Overlay{}, edge);
    Check(Near(j.lx, 0.0) && Near(j.ly, -1.0) && Near(std::fabs(j.dir), kPi) && Near(j.mag, 1.0), "stick 180:100");
    c.stick_deg = 90; c.stick_pct = 100;
    j = pi::Compose(c, pi::Sample{}, 0, pi::Overlay{}, edge);
    Check(Near(j.lx, 1.0) && Near(j.ly, 0.0, 1e-3) && Near(j.dir, kPi / 2), "stick 90:100");
    // net stale -> zeros with stale.
    c = Cfg();
    pi::Sample stale{}; stale.valid = true; stale.age_ms = 500;
    j = pi::Compose(c, stale, 0, pi::Overlay{}, edge);
    Check(j.write && j.stale && j.held == 0, "net stale: zeros + stale flag");
    pi::Sample none{};
    j = pi::Compose(c, none, 0, pi::Overlay{}, edge);
    Check(j.stale, "net no sample: stale");
    // net fresh with a forward stick and a 90 deg camera delta.
    edge = pi::EdgeState{};
    pi::Sample live{};
    live.valid = true; live.age_ms = 10; live.seq = 1;
    live.packet.flags = pi::Flag::Valid;
    live.packet.ly = -32767;  // forward in the sender frame
    live.packet.lmag = 65535; live.packet.ldir = static_cast<std::int16_t>(std::lround(kPi * 10000));
    live.packet.cam_cdeg = 9000;  // sender camera 90 deg
    c.yaw_sign = 1;
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);  // host cam 0: theta' = pi + 90deg
    const double expect = pi::NormalizeRadians(kPi + kPi / 2);
    Check(Near(j.dir, expect, 1e-2), "net yaw rotation by the camera delta");
    c.yaw_sign = 0;
    edge = pi::EdgeState{};
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    Check(Near(std::fabs(j.dir), kPi, 1e-2), "yaw none: no rotation");
    // Edges: press then release across frames (held source).
    edge = pi::EdgeState{};
    c = Cfg(pi::Word::press, pi::Word::release, pi::Word::held);
    live.packet.held = 0x2; live.seq = 2;
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    Check(j.w[0] == 0x2 && j.w[1] == 0 && j.w[2] == 0x2 && j.held == 0x2, "press edge on first hold");
    live.seq = 3;
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    Check(j.w[0] == 0 && j.w[2] == 0x2, "held stays, no new press");
    live.packet.held = 0x0; live.seq = 4;
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    Check(j.w[1] == 0x2 && j.held == 0, "release edge when let go");
    // word 'n' consumed once per seq.
    edge = pi::EdgeState{};
    c = Cfg(pi::Word::net);
    live.packet.held = 0; live.packet.w4c = 0x55; live.seq = 10;
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    Check(j.w[0] == 0x55, "net word on a new seq");
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    Check(j.w[0] == 0, "net word not repeated for the same seq");
    // mask filtering.
    edge = pi::EdgeState{};
    c = Cfg(pi::Word::held); c.mask = 0x2;
    live.packet.held = 0x6; live.seq = 20;
    j = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    Check(j.held == 0x2 && j.w[0] == 0x2, "mask keeps only allowed bits");
    // overlay bits add to held (within the mask).
    edge = pi::EdgeState{};
    c = Cfg(); c.mask = 0x6; c.mode = mgscoop::PartnerInput::neutral;
    j = pi::Compose(c, pi::Sample{}, 0, pi::Overlay{0x2}, edge);
    Check(j.held == 0x2, "overlay bit in neutral");
}

void TestOverlay() {
    pi::TapState tap{};
    // A tap gives its bits for kTapFrames windows, then 0.
    std::uint32_t bits = 0;
    for (unsigned i = 0; i < pi::kTapFrames; ++i) bits = pi::StepOverlay(tap, 1, 0x2, 0, 0, 0);
    Check(pi::StepOverlay(tap, 1, 0x2, 0, 0, 0) == 0, "tap expires after kTapFrames");
    (void)bits;
    tap = pi::TapState{};
    Check(pi::StepOverlay(tap, 1, 0x2, 0, 0, 0) == 0x2, "tap frame 1");
    // Press holds until ms.
    tap = pi::TapState{};
    Check(pi::StepOverlay(tap, 0, 0, 0x4, 1000, 500) == 0x4, "press active before ms");
    Check(pi::StepOverlay(tap, 0, 0, 0x4, 1000, 1000) == 0, "press expired at ms");
    // The hook keeps the tap and edge state across an Arm gen bump: the same
    // (cumulative) tap_seq is never delivered twice, a new one is.
    tap = pi::TapState{};
    for (unsigned i = 0; i < pi::kTapFrames; ++i) pi::StepOverlay(tap, 5, 0x2, 0, 0, 0);
    for (unsigned i = 0; i < 8; ++i) Check(pi::StepOverlay(tap, 5, 0x2, 0, 0, 0) == 0, "kept tap state: no replay of seq 5");
    Check(pi::StepOverlay(tap, 6, 0x2, 0, 0, 0) == 0x2, "kept tap state: seq 6 delivered");
    // A held bit across a gen bump with a kept edge state: no synthetic press.
    pi::Config cfg{};
    cfg.mode = mgscoop::PartnerInput::neutral;
    cfg.mask = 0x6;
    cfg.words[0] = pi::Word::zero; cfg.words[1] = pi::Word::press; cfg.words[2] = pi::Word::zero;
    pi::EdgeState edge{};
    const pi::Sample none{};
    pi::Injection j = pi::Compose(cfg, none, 0, pi::Overlay{0x4}, edge);
    Check(j.held == 0x4 && j.w[1] == 0x4, "edge: first frame press");
    j = pi::Compose(cfg, none, 0, pi::Overlay{0x4}, edge);
    Check(j.held == 0x4 && j.w[1] == 0, "edge: held, no second press (state kept across a gen bump)");
}

// Round 4: PP+0x14 is the world heading wrap(cam + atan2(lx, ly) - pi)
// (live: W/D/A/S/idle at cam 30 mrad gave 30/-1541/1600/-3112/-3112 mrad).
void TestHeading() {
    const double cam = 0.030;
    Check(Near(pi::Heading(cam, 0.0f, -1.0f), 0.030, 2e-3), "W: heading = cam");
    Check(Near(pi::Heading(cam, 1.0f, 0.0f), -1.541, 2e-3), "D: -1541 mrad");
    Check(Near(pi::Heading(cam, -1.0f, 0.0f), 1.600, 2e-3), "A: 1600 mrad");
    Check(Near(pi::Heading(cam, 0.0f, 1.0f), -3.1116, 2e-3), "S: -3112 mrad");
    Check(Near(pi::Heading(cam, 0.0f, 0.0f), -3.1116, 2e-3), "idle: atan2(0,0) = 0, as S");
    // Compose writes it (and dir = atan2(lx, ly)) from the injected stick.
    pi::Config c{};
    c.mode = mgscoop::PartnerInput::stick;
    c.stick_deg = 180; c.stick_pct = 100; c.mask = 0x2; c.stale_ms = 250;
    pi::EdgeState edge{};
    pi::Injection j = pi::Compose(c, pi::Sample{}, 173, pi::Overlay{}, edge);  // host cam 1.73 deg = 30 mrad
    Check(Near(j.heading, 0.030, 2e-3) && Near(std::fabs(j.dir), kPi, 1e-3), "stick 180:100 at cam 30 mrad: heading 30 mrad");
    c.stick_pct = 0;
    j = pi::Compose(c, pi::Sample{}, 173, pi::Overlay{}, edge);
    Check(j.dir == 0.0f && Near(j.heading, -3.1116, 2e-3), "zero stick: dir 0, heading cam - pi");
}

pi::Sample Live(std::uint32_t seq, std::uint32_t held) {
    pi::Sample s{};
    s.valid = true; s.age_ms = 5; s.seq = seq;
    s.packet.version = 2; s.packet.flags = pi::Flag::Valid; s.packet.held = held;
    return s;
}

// words=hpr: press on w50, release on w54, each for exactly one window, and
// a tap the window never saw as held is synthesized for one window.
void TestEdgesAndTaps() {
    pi::Config c = Cfg(pi::Word::held, pi::Word::press, pi::Word::release);
    c.mask = 0x2200006;
    pi::EdgeState edge{};
    pi::Injection j = pi::Compose(c, Live(1, 0x2), 0, pi::Overlay{}, edge);
    Check(j.w[0] == 0x2 && j.w[1] == 0x2 && j.w[2] == 0 && j.edge_press == 0x2, "hpr: press window");
    j = pi::Compose(c, Live(2, 0x2), 0, pi::Overlay{}, edge);
    Check(j.w[0] == 0x2 && j.w[1] == 0 && j.w[2] == 0, "hpr: held window");
    j = pi::Compose(c, Live(3, 0), 0, pi::Overlay{}, edge);
    Check(j.w[0] == 0 && j.w[1] == 0 && j.w[2] == 0x2 && j.edge_release == 0x2, "hpr: release window (the toggle edge)");
    j = pi::Compose(c, Live(4, 0), 0, pi::Overlay{}, edge);
    Check(j.w[2] == 0, "release lasts one window");
    // A tap pressed and released between two samples (both edges pending).
    pi::Sample tap = Live(5, 0);
    tap.edge_press = 0x2; tap.edge_release = 0x2;
    j = pi::Compose(c, tap, 0, pi::Overlay{}, edge);
    Check(j.synth == 0x2 && j.held == 0x2 && j.w[1] == 0x2, "sub-sample tap: one held window with its press edge");
    j = pi::Compose(c, Live(6, 0), 0, pi::Overlay{}, edge);
    Check(j.synth == 0 && j.held == 0 && j.w[2] == 0x2, "then its release edge");
    // tap12 alone also synthesizes; a tap the window already saw does not.
    pi::Sample t12 = Live(7, 0);
    t12.edge_tap12 = 0x2;
    j = pi::Compose(c, t12, 0, pi::Overlay{}, edge);
    Check(j.synth == 0x2 && j.tap12 == 0x2, "tap12 synthesizes once");
    j = pi::Compose(c, Live(8, 0), 0, pi::Overlay{}, edge);
    pi::Compose(c, Live(9, 0x2), 0, pi::Overlay{}, edge);
    pi::Sample seen = Live(10, 0);
    seen.edge_press = 0x2; seen.edge_release = 0x2; seen.edge_tap12 = 0x2;
    j = pi::Compose(c, seen, 0, pi::Overlay{}, edge);
    Check(j.synth == 0 && j.w[2] == 0x2, "a tap already seen as held: only its release");
    // Masked bits never synthesize; keyboard dash and walk are in the default mask.
    pi::Sample other = Live(11, 0);
    other.edge_press = other.edge_release = 0x10;
    j = pi::Compose(c, other, 0, pi::Overlay{}, edge);
    Check(j.synth == 0, "outside the mask: nothing");
    j = pi::Compose(c, Live(12, 0x2200000), 0, pi::Overlay{}, edge);
    Check(j.held == 0x2200000, "keyboard dash 0x200000 and walk 0x2000000 pass the default mask");
    // Stale input within the grace: the last live level, no synthesis, no edges.
    pi::Sample stale = Live(13, 0x2);
    stale.age_ms = 999; stale.edge_press = stale.edge_release = 0x2;
    j = pi::Compose(c, stale, 0, pi::Overlay{}, edge);
    Check(j.stale && j.held == 0x2200000 && j.synth == 0 && j.w[1] == 0 && j.w[2] == 0 && j.hist_freeze,
          "stale (grace): last live level kept, nothing synthesized, no edges, history frozen");
    // Beyond the grace: released silently (no release edge), the history restarts.
    stale.age_ms = 1500;
    j = pi::Compose(c, stale, 0, pi::Overlay{}, edge);
    Check(j.stale && j.held == 0 && j.w[2] == 0 && j.edge_release == 0 && j.hist_reset, "stale beyond the grace: silent release");
}

// Round 4 review: a stall during a held stance never toggles; the first
// window after a disarm replays no stale edge set; wrap windows get the level only.
void TestStallAndResync() {
    pi::Config c = Cfg(pi::Word::held, pi::Word::press, pi::Word::release);
    c.mask = 0x2200006;
    c.stale_ms = 250;
    pi::EdgeState edge{};
    // The guest holds stance; a 400 ms stall (age 400) keeps the level, no w54.
    pi::Injection j = pi::Compose(c, Live(1, 0x2), 0, pi::Overlay{}, edge);
    Check(j.w[1] == 0x2, "stall: press window");
    pi::Sample s = Live(1, 0x2);
    s.age_ms = 400;
    j = pi::Compose(c, s, 0, pi::Overlay{}, edge);
    Check(j.held == 0x2 && j.w[2] == 0 && j.edge_release == 0 && j.hist_freeze && j.lx == 0.0f && j.ly == 0.0f,
          "stall: held level kept, no release edge, history frozen, stick neutral");
    // Live again, still held: no second press edge.
    j = pi::Compose(c, Live(2, 0x2), 0, pi::Overlay{}, edge);
    Check(j.held == 0x2 && j.w[1] == 0 && j.w[2] == 0 && !j.hist_freeze, "recovered, still held: no edges");
    // Live again, released during the stall: exactly one release edge.
    j = pi::Compose(c, Live(3, 0), 0, pi::Overlay{}, edge);
    Check(j.w[2] == 0x2 && j.edge_release == 0x2, "released: one release edge");
    // After a silent release, a press on recovery is a fresh press.
    j = pi::Compose(c, Live(4, 0x4), 0, pi::Overlay{}, edge);
    s = Live(4, 0x4);
    s.age_ms = 2000;
    j = pi::Compose(c, s, 0, pi::Overlay{}, edge);
    Check(j.held == 0 && j.w[2] == 0 && j.hist_reset, "dash: silently released after the grace");
    j = pi::Compose(c, Live(5, 0x4), 0, pi::Overlay{}, edge);
    Check(j.held == 0x4 && j.w[1] == 0x4, "dash: pressed again on recovery");
    // Resync (first window after a disarm): edge sets accumulated while
    // disarmed are dropped, the current level is injected without edges.
    pi::EdgeState r{};
    r.prev_held = 0x2;  // the level before the disarm (stance was held)
    r.resync = true;
    pi::Sample stale_sets = Live(9, 0);
    stale_sets.edge_press = stale_sets.edge_release = 0x2;  // a crouch tap while disarmed
    stale_sets.edge_tap12 = 0x2;
    j = pi::Compose(c, stale_sets, 0, pi::Overlay{}, r);
    Check(j.resynced && j.synth == 0 && j.held == 0 && j.w[1] == 0 && j.w[2] == 0 && j.edge_release == 0 && j.tap12 == 0 &&
          j.hist_reset && !r.resync, "resync: no replayed tap, no release edge for the old level, history restarts");
    // A bit held across the resync is level only, and its later release is an edge.
    r.resync = true;
    j = pi::Compose(c, Live(10, 0x4), 0, pi::Overlay{}, r);
    Check(j.held == 0x4 && j.w[1] == 0, "resync with dash held: level, no press edge");
    j = pi::Compose(c, Live(11, 0x4), 0, pi::Overlay{}, r);
    Check(j.w[1] == 0 && j.w[2] == 0, "then held: no edges");
    j = pi::Compose(c, Live(12, 0), 0, pi::Overlay{}, r);
    Check(j.w[2] == 0x4, "then its release edge");
    // A resync during a stall drops the stalled level (no stale level across a disarm).
    pi::EdgeState g{};
    pi::Compose(c, Live(20, 0x4), 0, pi::Overlay{}, g);
    g.resync = true;
    pi::Sample stalled = Live(20, 0x4);
    stalled.age_ms = 500;
    j = pi::Compose(c, stalled, 0, pi::Overlay{}, g);
    Check(j.held == 0 && j.w[2] == 0 && g.net_held == 0, "resync during a stall: level dropped silently");
    // Fresh edge sets after the resync work again.
    pi::Sample tap = Live(13, 0);
    tap.edge_press = tap.edge_release = 0x2;
    j = pi::Compose(c, tap, 0, pi::Overlay{}, r);
    Check(j.synth == 0x2 && j.w[1] == 0x2, "after the resync: sub-sample taps synthesize again");
    // LevelOnly: wrap windows keep held words and sticks, drop every edge.
    pi::Injection full = j;
    full.w[2] = 0x2;
    full.edge_release = 0x2;
    full.tap12 = 0x2;
    full.lx = 0.5f;
    const pi::Injection lvl = pi::LevelOnly(full, c);
    Check(lvl.w[0] == (full.held & c.mask) && lvl.w[1] == 0 && lvl.w[2] == 0 && lvl.edge_press == 0 && lvl.edge_release == 0 &&
          lvl.tap12 == 0 && lvl.synth == 0 && lvl.held == full.held && lvl.lx == 0.5f, "LevelOnly: level kept, edges dropped");
    pi::Config n = Cfg(pi::Word::net, pi::Word::net, pi::Word::held);
    n.mask = 0x6;
    const pi::Injection lvl2 = pi::LevelOnly(full, n);
    Check(lvl2.w[0] == 0 && lvl2.w[1] == 0 && lvl2.w[2] == (full.held & 0x6), "LevelOnly: net words dropped, held word kept");
}

// hold=own: the exact replica of 0x434ED0 over the fed history.
void TestHoldReplica() {
    pi::HoldHistory h{};
    const std::uint32_t frames = 12;
    // A tap of 5 windows: 1 x5, then 2 on the release window, then 0.
    for (int i = 0; i < 5; ++i) {
        pi::HoldPush(h, 0x2);
        Check(pi::ClassifyHold(h, 0x2, 0, 1, frames) == 1, "tap: held < 12 -> 1");
    }
    pi::HoldPush(h, 0);
    Check(pi::ClassifyHold(h, 0, 0x2, 1, frames) == 2, "tap: released after a short run -> 2");
    pi::HoldPush(h, 0);
    Check(pi::ClassifyHold(h, 0, 0, 1, frames) == 0, "idle -> 0");
    // A hold of 15 windows: 1 for windows 1..11, 3 from window 12.
    h = pi::HoldHistory{};
    for (int i = 1; i <= 15; ++i) {
        pi::HoldPush(h, 0x2);
        const std::uint32_t code = pi::ClassifyHold(h, 0x2, 0, 1, frames);
        Check(i < 12 ? code == 1 : code == 3, "hold: 1 until 12 windows, then 3 (prone)");
    }
    pi::HoldPush(h, 0);
    Check(pi::ClassifyHold(h, 0, 0x2, 1, frames) == 0, "released after a long hold -> 0");
    // A sub-sample tap: released with an empty history -> 2.
    pi::HoldHistory empty{};
    Check(pi::ClassifyHold(empty, 0, 0x2, 1, frames) == 2, "release with an empty history -> 2");
    // A gap run (3 held, 1 free, 2 held): only the newest run counts.
    h = pi::HoldHistory{};
    pi::HoldPush(h, 0x2); pi::HoldPush(h, 0x2); pi::HoldPush(h, 0x2); pi::HoldPush(h, 0); pi::HoldPush(h, 0x2); pi::HoldPush(h, 0x2);
    Check(pi::ClassifyHold(h, 0x2, 0, 1, 3) == 1, "gap: newest run of 2 < 3 -> 1");
    Check(pi::ClassifyHold(h, 0x2, 0, 1, 2) == 3, "gap: newest run of 2 >= 2 -> 3");
    // Leading zeros are skipped; newest is only entry 0.
    pi::HoldPush(h, 0);
    Check(pi::ClassifyHold(h, 0x2, 0, 1, 2) == 0, "held word but entry 0 clear: not newest -> 0");
    // btn & 31 wraps.
    h = pi::HoldHistory{};
    pi::HoldPush(h, 0x2);
    Check(pi::ClassifyHold(h, 0x2, 0, 33, frames) == 1, "btn 33 == btn 1");
    // The ring keeps 128 entries; older ones read as 0.
    for (int i = 0; i < 200; ++i) pi::HoldPush(h, 0x4);
    Check(h.count == pi::kHoldHistory && pi::HoldAt(h, 0) == 0x4 && pi::HoldAt(h, pi::kHoldHistory) == 0, "ring bounds");
    // hold=net: the sender's words for frames == 12.
    Check(pi::NetHoldCode(true, 0x2, 0x2, 0, 1, 12) == 3 && pi::NetHoldCode(true, 0x2, 0, 0, 1, 12) == 1 &&
          pi::NetHoldCode(true, 0, 0, 0x2, 1, 12) == 2 && pi::NetHoldCode(true, 0, 0, 0, 1, 12) == 0, "net codes");
    Check(pi::NetHoldCode(false, 0x2, 0x2, 0, 1, 12) == pi::kHoldUseOwn && pi::NetHoldCode(true, 0x2, 0x2, 0, 1, 8) == pi::kHoldUseOwn,
          "net: not live or other frames -> own");
}

void TestBindings() {
    std::uint64_t handles[pi::kBindingCount]{};
    for (std::uint32_t i = 0; i < pi::kBindingCount; ++i) handles[i] = 0xAB0000001000ull + i;
    handles[3] = pi::kUnboundHandle;
    Check(pi::BindingIndex(handles, 0xAB0000001000ull + 25) == 25, "walk binding 0x19");
    Check(pi::BindingIndex(handles, 0xFFFFAB0000001000ull + 1) == 1, "48-bit compare");
    Check(pi::BindingIndex(handles, pi::kUnboundHandle) == -1, "unbound handle -> -1");
    Check(pi::BindingIndex(handles, 0x1234ull) == -1, "unknown handle -> -1");
}

// Sender edge sets: new set, two repeats, newer edges start the next set,
// tap12 and long12, and a refused submit sends the same content again.
void TestSenderEdges() {
    pi::SenderEdges st{};
    pi::PadSnapshot snap{};
    pi::InputPacket pkt{};
    snap.held = 0x2;
    pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.edge_seq == 1 && pkt.edge_rep == 0 && pkt.w50 == 0x2 && pkt.w54 == 0, "press opens set 1");
    pi::SenderFill(st, pkt);
    Check(pkt.edge_seq == 1 && pkt.w50 == 0x2, "a refused submit refills the same set");
    pi::SenderSent(st);
    pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.edge_seq == 1 && pkt.edge_rep == 1 && pkt.w50 == 0x2, "repeat 1");
    pi::SenderSent(st);
    pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.edge_seq == 1 && pkt.edge_rep == 2, "repeat 2");
    pi::SenderSent(st);
    pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.edge_seq == 0 && pkt.w50 == 0 && pkt.w54 == 0 && pkt.tap12 == 0, "no set after the repeats");
    pi::SenderSent(st);
    snap.held = 0;
    pi::SenderObserve(st, snap);  // released after 4 ticks: tap12
    pi::SenderFill(st, pkt);
    Check(pkt.edge_seq == 2 && pkt.w54 == 0x2 && pkt.tap12 == 0x2, "release opens set 2 with tap12");
    pi::SenderSent(st);
    snap.held = 0x4;
    pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.edge_seq == 3 && pkt.edge_rep == 0 && pkt.w50 == 0x4 && pkt.w54 == 0, "newer edges start set 3 (no stale bits)");
    pi::SenderSent(st);
    for (int i = 0; i < 12; ++i) pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.long12 == 0x4, "long12 after 12 ticks held");
    snap.held = 0;
    pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.tap12 == 0 && pkt.w54 == 0x4, "a long hold released: not a tap");
    // The game's own w50/w54 of one snapshot join the set (sub-tick taps).
    pi::SenderSent(st);
    snap.held = 0; snap.w50 = 0x2; snap.w54 = 0x2;
    pi::SenderObserve(st, snap);
    pi::SenderFill(st, pkt);
    Check(pkt.w50 == 0x2 && pkt.w54 == 0x2, "snapshot edge words latched");
    // Wire validity of filled packets.
    pkt.version = 2;
    Check(pi::ValidPacket(pkt), "a filled packet is valid");
    // Sequence wraps 255 -> 1.
    pi::SenderEdges w{};
    w.seq = 255; w.press = 0x2;
    pi::SenderFill(w, pkt);
    Check(pkt.edge_seq == 1, "edge_seq skips 0");
}

void TestAccumulate() {
    pi::InputEdges e{};
    pi::InputPacket p{};
    p.version = 2; p.edge_seq = 4; p.w50 = 0x2; p.tap12 = 0x2;
    pi::AccumulateEdges(e, p);
    pi::AccumulateEdges(e, p);  // a repeat of seq 4
    Check(e.press == 0x2 && e.tap12 == 0x2 && e.sets == 1, "a repeat adds nothing");
    p.edge_seq = 5; p.w50 = 0; p.w54 = 0x2; p.tap12 = 0;
    pi::AccumulateEdges(e, p);
    Check(e.press == 0x2 && e.release == 0x2 && e.sets == 2, "a new seq accumulates");
    p.edge_seq = 0; p.w54 = 0;
    pi::AccumulateEdges(e, p);
    Check(e.sets == 2, "no set: nothing");
    pi::InputPacket v1{};
    v1.version = 1; v1.w50 = 0x4;
    pi::AccumulateEdges(e, v1);
    Check(e.press == 0x6 && e.sets == 3, "v1 words every frame");
    // Packet validity rules.
    pi::InputPacket bad{};
    bad.version = 2; bad.edge_seq = 0; bad.w50 = 1;
    Check(!pi::ValidPacket(bad), "edge words without a set refused");
    bad.w50 = 0; bad.edge_rep = 1;
    Check(!pi::ValidPacket(bad), "edge_rep without a set refused");
    bad.edge_rep = 0; bad.version = 3;
    Check(pi::ValidPacket(bad), "version 3 (native) with no v3 fields is valid");
    bad.version = 4;
    Check(!pi::ValidPacket(bad), "unknown version refused");
    bad.version = 1; bad.long12 = 1;
    Check(!pi::ValidPacket(bad), "v1 with v2 fields refused");
}

// Native puppet: Compose's steer_rad (added to the stick direction, positive =
// left = world heading up) and mag_scale (radius and PP+0x10 scaled, clamped).
void TestComposeSteer() {
    pi::Config c = Cfg();
    c.yaw_sign = 0;
    pi::Sample live{};
    live.valid = true; live.age_ms = 10; live.seq = 1;
    live.packet.version = 2; live.packet.flags = pi::Flag::Valid;
    live.packet.ly = -32767; live.packet.lmag = 65535;
    live.packet.ldir = static_cast<std::int16_t>(std::lround(kPi * 10000));
    pi::EdgeState edge{};
    const pi::Injection base = pi::Compose(c, live, 0, pi::Overlay{}, edge);
    edge = pi::EdgeState{};
    const pi::Injection same = pi::Compose(c, live, 0, pi::Overlay{}, edge, 0.0, 1.0);
    Check(same.lx == base.lx && same.ly == base.ly && same.mag == base.mag && same.heading == base.heading,
          "defaults reproduce the legacy injection");
    edge = pi::EdgeState{};
    const double steer = 10.0 * kPi / 180.0;
    const pi::Injection left = pi::Compose(c, live, 0, pi::Overlay{}, edge, steer, 1.0);
    Check(Near(pi::NormalizeRadians(left.dir - base.dir), steer, 1e-3), "steer adds to the stick direction");
    Check(Near(pi::NormalizeRadians(left.heading - base.heading), steer, 1e-3), "a positive steer increases the world heading (left)");
    edge = pi::EdgeState{};
    const pi::Injection slow = pi::Compose(c, live, 0, pi::Overlay{}, edge, 0.0, 0.9);
    Check(Near(slow.mag, 0.9, 1e-3) && Near(std::sqrt(slow.lx * slow.lx + slow.ly * slow.ly), 0.9, 1e-3), "mag_scale scales radius and PP+0x10");
    edge = pi::EdgeState{};
    const pi::Injection big = pi::Compose(c, live, 0, pi::Overlay{}, edge, 0.0, 3.0);
    Check(big.mag <= 1.0f && std::sqrt(big.lx * big.lx + big.ly * big.ly) <= 1.0001f, "mag_scale clamps to 1");
    edge = pi::EdgeState{};
    const pi::Injection nanv = pi::Compose(c, live, 0, pi::Overlay{}, edge, std::nan(""), std::nan(""));
    Check(nanv.lx == base.lx && nanv.mag == base.mag, "non-finite steer/scale ignored");
    // Not live: the stick is neutral whatever the steer.
    pi::Sample stale = live;
    stale.age_ms = 5000;
    edge = pi::EdgeState{};
    const pi::Injection none = pi::Compose(c, stale, 0, pi::Overlay{}, edge, steer, 0.5);
    Check(none.lx == 0.0f && none.ly == 0.0f && none.mag == 0.0f, "stale input: no stick despite steering");
    Check(!none.look_valid, "stale input: no look");
    // v3: the live sample's look and camera fields reach the frame injection.
    pi::Sample v3 = live;
    pi::V3Source src{};
    src.look_valid = true; src.look_pitch = 0.2f; src.look_yaw = -1.0f;
    src.cam_valid = true; src.cam270 = 3.0f; src.cam274 = 4.0f; src.aiming = true;
    pi::Fill3(v3.packet, src);
    edge = pi::EdgeState{};
    const pi::Injection look = pi::Compose(c, v3, 0, pi::Overlay{}, edge);
    Check(look.look_valid && look.cam_valid && look.aiming && look.look_pitch == 0.2f && look.look_yaw == -1.0f && look.cam270 == 3.0f &&
          look.cam274 == 4.0f, "v3 look fields copied into the injection");
    Check(pi::LevelOnly(look, c).look_valid && !pi::Neutral(look).look_valid, "level-only keeps the look; neutral drops it");
    Check(!base.look_valid && !base.cam_valid, "a v2 sample carries no look");
}

} // namespace

int main() {
    TestEffective();
    TestDirCheck();
    TestQuantize();
    TestCompose();
    TestOverlay();
    TestHeading();
    TestEdgesAndTaps();
    TestStallAndResync();
    TestHoldReplica();
    TestBindings();
    TestSenderEdges();
    TestAccumulate();
    TestComposeSteer();
    std::printf("%u partner input checks passed: effective pad, dir check, quantize round trip/clamps, compose "
                "(pass/neutral/stick/net, yaw, stale, edges, net word, mask, overlay), tap/press timers, heading, hpr edges and "
                "sub-sample taps, stall grace/silent release, resync after a disarm, level-only wrap windows, hold classifier replica and net codes, key bindings, sender edge sets, receiver accumulation\n", checks);
    return 0;
}
