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
}

} // namespace

int main() {
    TestEffective();
    TestDirCheck();
    TestQuantize();
    TestCompose();
    TestOverlay();
    std::printf("%u partner input checks passed: effective pad, dir check, quantize round trip/clamps, compose "
                "(pass/neutral/stick/net, yaw, stale, edges, net word, mask, overlay), tap/press timers\n", checks);
    return 0;
}
