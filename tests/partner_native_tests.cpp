// Native puppet pure core (docs/native-puppet.md): arbiter transitions and
// holds, effective settings, steering, HOLD latch, effective mask and the
// per-consumer frame injection. Deterministic timelines; no game, no Win32.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "mgscoop/partner_input.hpp"
#include "mgscoop/partner_native.hpp"

namespace {
namespace pn = mgscoop::partner_native;
namespace pi = mgscoop::partner_input;
using mgscoop::PartnerApply;
using mgscoop::PartnerDm;
using mgscoop::PartnerInput;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
bool Is(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }
constexpr double kPi = 3.14159265358979323846;

// Every precondition of fallback -> entering holds.
pn::In Ready(std::uint64_t now) {
    pn::In in{};
    in.now_ms = now;
    in.configured = in.defer_on = true;
    in.installed_h0 = in.installed_h2 = in.installed_h1 = in.installed_wrap = true;
    in.live_h0 = in.live_h1 = in.live_h2 = true;
    in.thread_ok = true;
    in.realized = in.parts_loaded = true;
    in.stage = 3;
    in.link_live = in.input_valid = true;
    in.input_age_ms = 20;
    in.dm_idle = true;
    in.dm_patched = false;
    return in;
}
// Active and healthy (one 16 ms tick at 60 fps: 1 w50, 1 deferred... in 1 s windows).
pn::In Healthy(std::uint64_t now) {
    pn::In in = Ready(now);
    in.window_armed = in.recon_placed = true;
    in.w50_b1_s0 = 2; in.deferred = 2; in.drains = 2; in.windowed = 2; in.frames = 1;
    in.drain_us_max = 500;
    return in;
}
bool NoRequests(const pn::Out& o) {
    return !o.req_defer_arm && !o.req_defer_disarm && !o.req_disarm && !o.req_dmmap_rearm && !o.req_epoch;
}
bool LeaveRequests(const pn::Out& o) {
    return o.mode == pn::Mode::leaving && o.req_defer_disarm && o.req_disarm && o.req_dmmap_rearm && o.req_epoch && !o.req_defer_arm &&
           !o.active;
}

// Drives fallback -> active and returns the time of activation.
std::uint64_t Activate(pn::State& s, std::uint64_t t) {
    pn::Out o{};
    for (; t < 100000; t += 100) {
        pn::In in = Ready(t);
        in.window_armed = in.recon_placed = true;
        o = pn::Step(s, in);
        if (o.mode == pn::Mode::active) return t;
    }
    Check(false, "activation");
    return t;
}

void TestOffAndLegacy() {
    pn::State s{};
    pn::Reset(s, false);
    pn::In in = Ready(0);
    in.configured = false;
    for (std::uint64_t t = 0; t < 10000; t += 100) {
        in.now_ms = t;
        const pn::Out o = pn::Step(s, in);
        Check(o.mode == pn::Mode::off && !o.active && NoRequests(o) && !o.transition, "drive=legacy stays off with no requests");
    }
    Check(Is(pn::ModeName(pn::Mode::off), "off") && Is(pn::ModeName(pn::Mode::latched), "latched"), "mode names");
}

void TestEnterSequence() {
    pn::State s{};
    pn::Reset(s, true);
    Check(s.mode == pn::Mode::fallback, "configured mission starts in fallback");
    // Preconditions must hold 2000 ms continuously.
    pn::Out o = pn::Step(s, Ready(1000));
    Check(o.mode == pn::Mode::fallback && Is(o.reason, "settling"), "settling at first ready tick");
    pn::In broken = Ready(2000);
    broken.input_age_ms = 300;
    o = pn::Step(s, broken);
    Check(o.mode == pn::Mode::fallback && Is(o.reason, "input_stale"), "a stale input resets the hold");
    o = pn::Step(s, Ready(2100));
    o = pn::Step(s, Ready(4099));
    Check(o.mode == pn::Mode::fallback, "1999 ms is not enough");
    // DM still playing: entering with step dm_stop.
    pn::In in = Ready(4100);
    in.dm_idle = false;
    o = pn::Step(s, in);
    Check(o.mode == pn::Mode::entering && o.active && o.transition && Is(o.enter_step, "dm_stop") && NoRequests(o),
          "entering after 2000 ms: dm_stop first, native settings active, no requests");
    in.now_ms = 4200;
    o = pn::Step(s, in);
    Check(o.mode == pn::Mode::entering && !o.enter_step, "step names only on change");
    in.dm_idle = true; in.dm_patched = true; in.now_ms = 4300;
    o = pn::Step(s, in);
    Check(Is(o.enter_step, "dm_revert") && o.mode == pn::Mode::entering, "dm_revert while still patched");
    in.dm_patched = false; in.now_ms = 4400;
    o = pn::Step(s, in);
    Check(Is(o.enter_step, "place") && o.mode == pn::Mode::entering, "place once dm idle and unpatched");
    in.window_armed = true; in.now_ms = 4500;
    o = pn::Step(s, in);
    Check(o.mode == pn::Mode::entering, "armed window without placement is not enough");
    in.recon_placed = true; in.now_ms = 4600;
    o = pn::Step(s, in);
    Check(o.mode == pn::Mode::active && o.active && o.req_defer_arm && !o.req_disarm && !o.req_defer_disarm &&
          Is(o.enter_step, "armed"), "active when armed and placed; the deferral is armed last (identity -> arm -> defer)");
    o = pn::Step(s, Healthy(4700));
    Check(o.mode == pn::Mode::active && NoRequests(o), "steady active");
}

void TestEnterTimeoutsAndRevertLatch() {
    {
        pn::State s{};
        pn::Reset(s, true);
        pn::Step(s, Ready(0));
        pn::In in = Ready(2000);
        in.dm_idle = false;
        pn::Out o = pn::Step(s, in);
        Check(o.mode == pn::Mode::entering, "entering");
        in.now_ms = 3999;
        o = pn::Step(s, in);
        Check(o.mode == pn::Mode::entering, "dm stop within 2000 ms");
        in.now_ms = 4000;
        o = pn::Step(s, in);
        Check(LeaveRequests(o) && Is(o.reason, "enter_timeout_dm"), "DM stop timeout leaves");
        o = pn::Step(s, Ready(4100));
        Check(o.mode == pn::Mode::fallback && s.fallbacks == 1, "leaving lasts one step, then fallback");
    }
    {
        pn::State s{};
        pn::Reset(s, true);
        pn::Step(s, Ready(0));
        pn::Out o = pn::Step(s, Ready(2000));
        Check(o.mode == pn::Mode::entering && Is(o.enter_step, "place"), "straight to place");
        o = pn::Step(s, Ready(4999));
        Check(o.mode == pn::Mode::entering, "placement within 3000 ms");
        o = pn::Step(s, Ready(5000));
        Check(LeaveRequests(o) && Is(o.reason, "enter_timeout_place"), "placement timeout leaves");
    }
    {
        pn::State s{};
        pn::Reset(s, true);
        pn::Step(s, Ready(0));
        pn::In in = Ready(2000);
        in.dm_patched = true;
        pn::Out o = pn::Step(s, in);
        Check(Is(o.enter_step, "dm_revert"), "revert step");
        in.dm_revert_failed = true; in.now_ms = 2100;
        o = pn::Step(s, in);
        Check(LeaveRequests(o) && Is(o.reason, "dm_revert_failed"), "a failed DM revert leaves");
        o = pn::Step(s, Ready(2200));
        Check(o.mode == pn::Mode::latched && Is(o.reason, "dm_revert_failed"), "... and latches for the mission");
        for (std::uint64_t t = 2300; t < 20000; t += 500) Check(pn::Step(s, Ready(t)).mode == pn::Mode::latched, "latched holds");
        pn::Reset(s, true);
        Check(s.mode == pn::Mode::fallback && !s.dm_latch && s.fallbacks == 0, "MissionStart clears the latch");
    }
}

void TestHardReasons() {
    struct Case { const char* reason; void (*apply)(pn::In&); };
    const Case cases[] = {
        {"faulted", [](pn::In& in) { in.faulted = true; }},
        {"hook_lost", [](pn::In& in) { in.live_h1 = false; }},
        {"hook_lost", [](pn::In& in) { in.live_h2 = false; }},
        {"hook_lost", [](pn::In& in) { in.live_h0 = false; }},
        {"thread", [](pn::In& in) { in.thread_ok = false; }},
        {"terminating", [](pn::In& in) { in.terminating = true; }},
        {"unrealized", [](pn::In& in) { in.realized = false; }},
        {"range_hidden", [](pn::In& in) { in.range_hidden = true; }},
        {"configured", [](pn::In& in) { in.configured = false; }},
        {"defer_off", [](pn::In& in) { in.defer_on = false; }},
        {"stage", [](pn::In& in) { in.stage = 2; }},
        {"lost", [](pn::In& in) { in.lost = 1; }},
        {"wrap_in_batch", [](pn::In& in) { in.wrap_in_batch = 1; }},
        {"diverging", [](pn::In& in) { in.snaps_10s = 3; }},
        {"runaway", [](pn::In& in) { in.runaway_ms = 2001; }},
    };
    for (const Case& c : cases) {
        pn::State s{};
        pn::Reset(s, true);
        const std::uint64_t t = Activate(s, 0);
        pn::In in = Healthy(t + 16);
        c.apply(in);
        const pn::Out o = pn::Step(s, in);
        Check(LeaveRequests(o) && Is(o.reason, c.reason), c.reason);
    }
    // Not hard: runaway at exactly 2000 ms, two snaps.
    pn::State s{};
    pn::Reset(s, true);
    const std::uint64_t t = Activate(s, 0);
    pn::In in = Healthy(t + 16);
    in.runaway_ms = 2000;
    in.snaps_10s = 2;
    Check(pn::Step(s, in).mode == pn::Mode::active, "runaway 2000 ms and 2 snaps stay active");
}

void TestSoftReasons() {
    {
        pn::State s{};
        pn::Reset(s, true);
        const std::uint64_t t = Activate(s, 0);
        pn::In in = Healthy(t + 100);
        in.input_age_ms = 400;
        Check(pn::Step(s, in).mode == pn::Mode::active, "stale input starts a hold");
        in.now_ms = t + 1099;
        Check(pn::Step(s, in).mode == pn::Mode::active, "999 ms of stale input stays");
        in.now_ms = t + 1100;
        const pn::Out o = pn::Step(s, in);
        Check(LeaveRequests(o) && Is(o.reason, "input_stale"), "1000 ms of stale input leaves");
    }
    {
        pn::State s{};
        pn::Reset(s, true);
        const std::uint64_t t = Activate(s, 0);
        pn::In in = Healthy(t + 100);
        in.input_age_ms = 400;
        pn::Step(s, in);
        in.now_ms = t + 600;
        in.input_age_ms = 20;
        pn::Step(s, in);  // recovered: the hold restarts
        in.now_ms = t + 700;
        in.input_age_ms = 400;
        pn::Step(s, in);
        in.now_ms = t + 1500;
        Check(pn::Step(s, in).mode == pn::Mode::active, "a recovered input restarts the stale hold");
    }
    {
        pn::State s{};
        pn::Reset(s, true);
        const std::uint64_t t = Activate(s, 0);
        pn::In in = Healthy(t + 100);
        in.link_live = false;
        pn::Step(s, in);
        in.now_ms = t + 599;
        Check(pn::Step(s, in).mode == pn::Mode::active, "link hold 499 ms");
        in.now_ms = t + 600;
        Check(Is(pn::Step(s, in).reason, "link"), "link down 500 ms leaves");
    }
    // Health: the first window is warm-up; two consecutive bad windows leave;
    // a good window resets the count.
    {
        pn::State s{};
        pn::Reset(s, true);
        std::uint64_t t = Activate(s, 0);
        auto window = [&](std::uint32_t w50, std::uint32_t deferred, std::uint32_t drains, std::uint32_t windowed) {
            pn::Out o{};
            for (int i = 0; i < 10; ++i) {
                t += 100;
                pn::In in = Healthy(t);
                in.w50_b1_s0 = w50; in.deferred = deferred; in.drains = drains; in.windowed = windowed;
                o = pn::Step(s, in);
                if (o.mode != pn::Mode::active) break;
            }
            return o;
        };
        Check(window(12, 0, 12, 0).mode == pn::Mode::active, "warm-up window ignored");
        Check(window(12, 10, 12, 12).mode == pn::Mode::active, "one bad window (deferred 83%) is not enough");
        Check(window(12, 12, 12, 12).mode == pn::Mode::active, "a good window resets");
        Check(window(12, 12, 12, 10).mode == pn::Mode::active, "bad windowed ratio, first");
        const pn::Out o = window(12, 12, 12, 10);
        Check(LeaveRequests(o) && Is(o.reason, "health"), "two consecutive bad windows leave (health)");
    }
    {
        pn::State s{};
        pn::Reset(s, true);
        std::uint64_t t = Activate(s, 0);
        // 90% exactly is healthy; sub-0 counting keeps w50 == deferred.
        pn::Out o{};
        for (int i = 0; i < 60; ++i) {
            t += 100;
            pn::In in = Healthy(t);
            in.w50_b1_s0 = 10; in.deferred = 9; in.drains = 10; in.windowed = 9;
            o = pn::Step(s, in);
        }
        Check(o.mode == pn::Mode::active, "90% deferred and windowed is healthy");
    }
    {
        pn::State s{};
        pn::Reset(s, true);
        std::uint64_t t = Activate(s, 0);
        pn::Out o{};
        int windows = 0;
        for (int i = 0; i < 50 && o.mode != pn::Mode::leaving; ++i) {
            t += 100;
            pn::In in = Healthy(t);
            in.drain_us_max = 4500;
            o = pn::Step(s, in);
            if (i % 10 == 9) ++windows;
        }
        Check(LeaveRequests(o) && Is(o.reason, "cost") && windows == 3, "drain cost over 4000 us for 3 windows leaves");
    }
}

void TestCooldownAndLatch() {
    pn::State s{};
    pn::Reset(s, true);
    std::uint64_t t = Activate(s, 0);
    for (int k = 1; k <= 3; ++k) {
        pn::In in = Healthy(t + 16);
        in.faulted = true;
        pn::Out o = pn::Step(s, in);
        Check(LeaveRequests(o), "fault leaves");
        o = pn::Step(s, Ready(t + 32));
        if (k < 3) {
            Check(o.mode == pn::Mode::fallback && s.fallbacks == static_cast<std::uint32_t>(k), "fallback counts");
            // Cooldown: 5000 ms after the leaving, then the 2000 ms hold.
            o = pn::Step(s, Ready(t + 4000));
            Check(o.mode == pn::Mode::fallback && Is(o.reason, "cooldown"), "cooldown after leaving");
            o = pn::Step(s, Ready(t + 5016));
            Check(o.mode == pn::Mode::fallback && Is(o.reason, "settling"), "cooldown over, hold starts");
            pn::In e = Ready(t + 7016);
            e.window_armed = e.recon_placed = true;
            o = pn::Step(s, e);
            Check(o.mode == pn::Mode::active, "re-entry after cooldown + hold");
            t += 7016;
        } else {
            Check(o.mode == pn::Mode::latched && s.fallbacks == 3, "3 fallbacks in one mission latch");
        }
    }
    for (std::uint64_t u = t; u < t + 30000; u += 1000) Check(pn::Step(s, Ready(u)).mode == pn::Mode::latched, "latched holds");
    pn::Unlatch(s, t + 30000);
    Check(s.mode == pn::Mode::fallback && s.fallbacks == 0, "a drive=native trigger unlatches");
    pn::Reset(s, true);
    Check(s.mode == pn::Mode::fallback && s.fallbacks == 0 && !s.left, "MissionStart resets the counters");
}

void TestFallbackReasons() {
    struct Case { const char* reason; void (*apply)(pn::In&); };
    const Case cases[] = {
        {"defer_census", [](pn::In& in) { in.defer_on = false; }},
        {"install_bytes", [](pn::In& in) { in.install_refusal = "bytes"; }},
        {"install_hook_disabled", [](pn::In& in) { in.install_refusal = "hook_disabled"; }},
        {"install_other", [](pn::In& in) { in.install_refusal = "weird"; }},
        {"not_installed", [](pn::In& in) { in.installed_h1 = false; }},
        {"thread", [](pn::In& in) { in.thread_ok = false; }},
        {"faulted", [](pn::In& in) { in.faulted = true; }},
        {"dm_revert_failed", [](pn::In& in) { in.dm_revert_failed = true; }},
        {"stage", [](pn::In& in) { in.stage = 2; }},
        {"unrealized", [](pn::In& in) { in.realized = false; }},
        {"parts", [](pn::In& in) { in.parts_loaded = false; }},
        {"link", [](pn::In& in) { in.link_live = false; }},
        {"no_input", [](pn::In& in) { in.input_valid = false; }},
    };
    for (const Case& c : cases) {
        pn::State s{};
        pn::Reset(s, true);
        pn::Out o{};
        for (std::uint64_t t = 0; t < 20000; t += 500) {
            pn::In in = Ready(t);
            c.apply(in);
            o = pn::Step(s, in);
            Check(o.mode == pn::Mode::fallback && !o.active && NoRequests(o), "stays in fallback");
        }
        Check(Is(o.reason, c.reason), c.reason);
    }
    // Census never activates; legacy goes off.
    pn::State s{};
    pn::Reset(s, true);
    pn::In in = Ready(0);
    in.configured = false;
    Check(pn::Step(s, in).mode == pn::Mode::off, "fallback -> off when drive=legacy");
}

void TestEffective() {
    pn::DriveSettings base{};
    base.apply = PartnerApply::xform;
    base.input = PartnerInput::off;
    base.dm = PartnerDm::map;
    base.anim = mgscoop::PartnerAnim::net;
    base.wrap = mgscoop::PartnerWrap::off;
    base.hold = mgscoop::PartnerInputHold::none;
    base.words[0] = base.words[1] = base.words[2] = mgscoop::kPartnerWordZero;
    base.cam = mgscoop::PartnerInputCam::detect;
    base.recon = mgscoop::PartnerRecon::off;
    base.slot = mgscoop::PartnerSlot::remote;
    base.p218 = true;
    base.mask = 0xFFFFFFFFu;
    const pn::NativeConfig cfg{false, false, mgscoop::PartnerNativeAim::off};
    const pn::DriveSettings same = pn::Effective(base, false, cfg);
    Check(std::memcmp(&same, &base, sizeof(base)) == 0, "inactive: base values unchanged");
    const pn::DriveSettings e = pn::Effective(base, true, cfg);
    Check(e.apply == PartnerApply::input && e.input == PartnerInput::net && e.dm == PartnerDm::off &&
          e.anim == mgscoop::PartnerAnim::off && e.wrap == mgscoop::PartnerWrap::window && e.hold == mgscoop::PartnerInputHold::own &&
          e.words[0] == mgscoop::kPartnerWordHeld && e.words[1] == mgscoop::kPartnerWordPress &&
          e.words[2] == mgscoop::kPartnerWordRelease && e.cam == mgscoop::PartnerInputCam::sandbox &&
          e.recon == mgscoop::PartnerRecon::track && e.slot == mgscoop::PartnerSlot::body && !e.p218 && e.mask == 0x2200006u,
          "active: the exact native settings (p218 never on)");
    Check(pn::Effective(base, true, pn::NativeConfig{true, false, mgscoop::PartnerNativeAim::look}).mask == 0x220004Eu,
          "gun stage mask");
    Check(pn::Effective(base, true, pn::NativeConfig{true, true, mgscoop::PartnerNativeAim::look}).mask == 0x220005Eu,
          "fire stage mask");
    // NativeMask: the configured mask is ANDed with the allowance.
    Check(pn::NativeMask(0x220005Eu, true, false) == 0x220004Eu, "configured 0x220005E with fire=0 yields no FIRE 0x10");
    Check(pn::NativeMask(0x220005Eu, false, true) == 0x2200006u, "fire without gun refused (no 0x10, no 0x48)");
    Check(pn::NativeMask(0x2200006u, true, true) == 0x2200006u, "the configured mask still limits");
    Check(pn::NativeMask(0x2u, false, false) == 0x2u, "subset of the base");
}

void TestLatch() {
    for (std::uint32_t old : {0u, 1u, 0xFFFFFFFEu, 0xFFFFFFFFu, 0x12345678u})
        for (std::uint32_t w4c : {0u, 0x8u, 0xFFFFFFFFu, 0xFFFFFFF7u})
            for (std::uint32_t mask : {0u, 0x8u, 0xFFFFFFF7u}) {
                const std::uint32_t expect = (old & ~1u) | (((w4c & ~mask) >> 3) & 1u);
                Check(pn::Latch27c(old, w4c, mask) == expect, "latch formula");
                Check((pn::Latch27c(old, w4c, mask) & ~1u) == (old & ~1u), "latch touches bit 0 only");
            }
    Check(pn::Latch27c(0, 0x8, 0) == 1 && pn::Latch27c(1, 0, 0) == 0, "HOLD held -> 1, released -> 0");
    Check(pn::Latch27c(0, 0x8, 0x8) == 0, "mask exit: PP+0x28 bit 3 masks HOLD");
    Check(pn::LatchRestore(0xFFFFFFFFu, 0) == 0xFFFFFFFEu && pn::LatchRestore(0x10u, 1u) == 0x11u &&
          pn::LatchRestore(0xABCDEF00u, 0x1u) == 0xABCDEF01u, "restore puts bit 0 back and keeps bits 1..31 as the window left them");
}

void TestSteer() {
    const pn::SteerParams p{};
    // Remote moving +z at 3000 mm/s; target 1000 mm to the left (+x): positive bias.
    pn::SteerState st{};
    pn::SteerIn in{};
    in.v_z = 3000; in.e_x = 1000; in.in_r = 1.0;
    in.now_ms = 1000;
    pn::SteerOut o = pn::Steer(st, in, p);
    Check(o.active && o.bias_rad == 0.0 && std::fabs(o.cross_mm - 1000.0) < 1e-6 && std::fabs(o.along_mm) < 1e-6,
          "first step: active, no time elapsed");
    in.now_ms = 1100;
    o = pn::Steer(st, in, p);
    Check(o.bias_rad > 0 && std::fabs(o.bias_rad - 3.0 * kPi / 180.0) < 1e-9, "target left -> positive bias, 30 deg/s rate limit");
    for (int i = 0; i < 20; ++i) { in.now_ms += 100; o = pn::Steer(st, in, p); }
    const double expect = std::atan2(1000.0, 1500.0);  // 33.7 deg, clamped to 15
    Check(expect > 15.0 * kPi / 180.0 && std::fabs(o.bias_rad - 15.0 * kPi / 180.0) < 1e-9, "clamped to steer_deg");
    // Inactive below 500 mm/s: back to 0 at 60 deg/s.
    in.v_z = 400; in.now_ms += 100;
    o = pn::Steer(st, in, p);
    Check(!o.active && std::fabs(o.bias_rad - 9.0 * kPi / 180.0) < 1e-9, "inactive below 500 mm/s returns at 60 deg/s");
    in.now_ms += 1000;
    o = pn::Steer(st, in, p);
    Check(o.bias_rad == 0.0, "back to 0");
    // Right side: negative; dead zone; in_r.
    pn::SteerState st2{};
    in = pn::SteerIn{};
    in.v_z = 3000; in.e_x = -1000; in.in_r = 1.0; in.now_ms = 0;
    pn::Steer(st2, in, p);
    in.now_ms = 1000;
    Check(pn::Steer(st2, in, p).bias_rad < 0, "target right -> negative bias");
    pn::SteerState st3{};
    in.e_x = 250; in.now_ms = 0;
    Check(!pn::Steer(st3, in, p).active, "within the 300 mm dead zone");
    in.e_x = 1000; in.in_r = 0.4;
    Check(!pn::Steer(st3, in, p).active, "in_r <= 0.4 inactive");
    in.in_r = 1.0; in.frozen = true;
    Check(!pn::Steer(st3, in, p).active, "frozen inactive");
    in.frozen = false; in.prone_transition = true;
    Check(!pn::Steer(st3, in, p).active, "prone transition inactive");
    pn::SteerParams off = p;
    off.max_deg = 0;
    in.prone_transition = false;
    pn::SteerState st4{};
    Check(!pn::Steer(st4, in, off).active && pn::Steer(st4, in, off).bias_rad == 0.0, "steer_deg 0 disables steering");
    // Moving -x: left of that is -z... cross = e.x*u.z - e.z*u.x with u = (-1, 0): cross = e.z.
    pn::SteerState st5{};
    in = pn::SteerIn{};
    in.v_x = -3000; in.e_z = 800; in.in_r = 1.0;
    Check(std::fabs(pn::Steer(st5, in, p).cross_mm - 800.0) < 1e-6, "cross for a remote moving -x");
    // Trim: only at full stick, no walk key, body ahead (along < -300).
    pn::SteerParams tp = p;
    tp.trim_pct = 10;
    pn::SteerState st6{};
    in = pn::SteerIn{};
    in.v_z = 3000; in.e_z = -1000; in.in_r = 1.0;
    o = pn::Steer(st6, in, tp);
    Check(std::fabs(o.along_mm + 1000.0) < 1e-6 && std::fabs(o.mag_scale - 0.9) < 1e-9, "trim: max(1 - 10%, 1 + along/5000) = 0.9");
    in.e_z = -200;
    Check(pn::Steer(st6, in, tp).mag_scale == 1.0, "no trim within 300 mm");
    in.e_z = -1000; in.walk_key = true;
    Check(pn::Steer(st6, in, tp).mag_scale == 1.0, "never with the walk key");
    in.walk_key = false; in.in_r = 0.9;
    Check(pn::Steer(st6, in, tp).mag_scale == 1.0, "never below full stick (in_r < 0.95)");
    in.in_r = 0.95; in.e_z = -40000;
    o = pn::Steer(st6, in, tp);
    Check(o.mag_scale >= 0.9 - 1e-9 && o.mag_scale * 0.95 >= pn::kTrimFloor, "trim floor keeps the magnitude above 0.42");
    Check(pn::Steer(st6, in, p).mag_scale == 1.0, "trim 0 never trims");
}

void TestConsumers() {
    pi::Config c{};
    c.mode = PartnerInput::net;
    c.mask = 0xFFFFFFFFu;
    c.words[0] = pi::Word::held; c.words[1] = pi::Word::press; c.words[2] = pi::Word::release;
    pi::Injection f{};
    f.write = true;
    f.held = 0x6; f.w[0] = 0x6; f.w[1] = 0x2; f.w[2] = 0x8;
    f.edge_press = 0x2; f.edge_release = 0x8; f.tap12 = 0x10;
    f.lx = 0.5f; f.ly = -0.5f; f.mag = 0.7f; f.heading = 1.0f;
    std::uint32_t last[pi::kConsumers]{};
    pi::ConsumerView view{};
    pi::Injection j = pi::ForConsumer(f, 7, last[1], 5, c, &view);
    Check(view == pi::ConsumerView::full && j.edge_press == 0x2 && j.w[1] == 0x2 && j.tap12 == 0x10 && last[1] == 7,
          "first use of a frame: full injection with edges");
    j = pi::ForConsumer(f, 7, last[1], 6, c, &view);
    Check(view == pi::ConsumerView::level && j.edge_press == 0 && j.w[1] == 0 && j.w[2] == 0 && j.w[0] == 0x6 && j.held == 0x6 &&
          j.lx == 0.5f && j.tap12 == 0, "second use: level only");
    j = pi::ForConsumer(f, 7, last[2], 6, c, &view);
    Check(view == pi::ConsumerView::full && j.edge_release == 0x8, "per-consumer independence");
    j = pi::ForConsumer(f, 8, last[1], 1, c, &view);
    Check(view == pi::ConsumerView::full && last[1] == 8, "a new frame resets");
    j = pi::ForConsumer(f, 9, last[3], 101, c, &view);
    Check(view == pi::ConsumerView::neutral && j.write && j.stale && j.held == 0 && j.w[0] == 0 && j.lx == 0.0f && j.mag == 0.0f &&
          j.edge_press == 0 && last[3] == 0, "older than 100 ms: neutral, frame not consumed");
    j = pi::ForConsumer(f, 9, last[3], 100, c, &view);
    Check(view == pi::ConsumerView::full, "exactly 100 ms is still fresh");
}

void TestRing() {
    pn::Ring r{};
    for (std::uint32_t i = 0; i < 50; ++i) {
        pn::RingEntry e{};
        e.ms = i;
        pn::Push(r, e);
    }
    Check(r.count == pn::kRing && pn::At(r, 0).ms == 2 && pn::At(r, pn::kRing - 1).ms == 49, "ring keeps the newest 48, oldest first");
}

} // namespace

int main() {
    TestOffAndLegacy();
    TestEnterSequence();
    TestEnterTimeoutsAndRevertLatch();
    TestHardReasons();
    TestSoftReasons();
    TestCooldownAndLatch();
    TestFallbackReasons();
    TestEffective();
    TestLatch();
    TestSteer();
    TestConsumers();
    TestRing();
    std::printf("%u partner native checks passed: arbiter (off, settling hold, entering steps dm_stop/dm_revert/place/armed, "
                "timeouts, DM revert latch, hard and soft reasons, health warm-up and windows, cost, cooldown, 3-fallback latch, "
                "unlatch, MissionStart reset, fallback reasons, publication order), effective settings and mask, HOLD latch, "
                "steering and trim, per-consumer frame injection, telemetry ring\n", checks);
    return 0;
}
