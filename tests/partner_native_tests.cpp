// Native puppet pure core (docs/native-puppet.md): arbiter transitions and
// holds, effective settings, steering, HOLD latch, effective mask and the
// per-consumer frame injection. Deterministic timelines; no game, no Win32.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "mgscoop/partner_caps_wire.hpp"
#include "mgscoop/partner_aim.hpp"
#include "mgscoop/partner_input.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/partner_native_net.hpp"
#include "mgscoop/partner_rates.hpp"
#include "mgscoop/partner_reload.hpp"
#include "mgscoop/partner_slots.hpp"
#include <limits>

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
    in.installed_h0 = in.installed_h2 = in.installed_h1 = in.installed_wrap = in.installed_p1 = true;
    in.live_h0 = in.live_h1 = in.live_h2 = in.live_p1 = true;
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
    Check(!o.req_defer_arm, "no deferral while the DM clip stops or the patch reverts");
    in.dm_patched = false; in.now_ms = 4400;
    o = pn::Step(s, in);
    Check(Is(o.enter_step, "place") && o.mode == pn::Mode::entering && o.req_defer_arm && !o.req_defer_disarm,
          "place once dm idle and unpatched; the deferral is requested before any native Arm (identity -> defer -> Arm)");
    in.window_armed = true; in.now_ms = 4500;
    o = pn::Step(s, in);
    Check(o.mode == pn::Mode::entering && o.req_defer_arm, "armed window without placement is not enough (the deferral stays requested)");
    in.recon_placed = true; in.now_ms = 4600;
    o = pn::Step(s, in);
    Check(o.mode == pn::Mode::active && o.active && o.req_defer_arm && !o.req_disarm && !o.req_defer_disarm &&
          Is(o.enter_step, "armed"), "active when armed and placed (the deferral request repeats; the driver arms once)");
    o = pn::Step(s, Healthy(4700));
    Check(o.mode == pn::Mode::active && NoRequests(o), "steady active");
}

void TestThreadWarmup() {
    pn::State state{}; pn::Reset(state,true);
    auto in=Ready(2000); in.thread_ok=false; in.thread_warmup=true;
    auto out=pn::Step(state,in);
    Check(Is(out.reason,"warmup") && out.mode==pn::Mode::fallback, "thread warmup is an expected entry wait");
    in.thread_warmup=false; out=pn::Step(state,in);
    Check(Is(out.reason,"thread"), "different thread remains a real refusal");
    Check(Is(mgscoop::partner_native_net::ReasonName(mgscoop::partner_native_net::ReasonCode("warmup")),"warmup"),
          "warmup append-only capability reason round trips");
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
        {"hook_lost", [](pn::In& in) { in.live_p1 = false; }},
        {"thread", [](pn::In& in) { in.thread_ok = false; }},
        {"terminating", [](pn::In& in) { in.terminating = true; }},
        {"unrealized", [](pn::In& in) { in.realized = false; }},
        {"range_hidden", [](pn::In& in) { in.range_hidden = true; }},
        {"configured", [](pn::In& in) { in.configured = false; }},
        {"defer_off", [](pn::In& in) { in.defer_on = false; }},
        {"stage", [](pn::In& in) { in.stage = 2; }},
        {"lost", [](pn::In& in) { in.lost = 1; }},
        {"wrap_in_batch", [](pn::In& in) { in.wrap_in_batch = 1; }},
        {"drain_block", [](pn::In& in) { in.drain_block = 1; }},
        {"unwindowed", [](pn::In& in) { in.drain_disarmed = 1; }},
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
    // A disarmed drain (block 1's job without a window under a disabled Arm)
    // leaves active only: while entering it is the arming frame's race.
    pn::State e{};
    pn::Reset(e, true);
    pn::Step(e, Ready(0));
    pn::In entering = Ready(2000);
    entering.drain_disarmed = 2;
    const pn::Out eo = pn::Step(e, entering);
    Check(eo.mode == pn::Mode::entering && Is(eo.enter_step, "place"), "a disarmed drain while entering (place) does not leave");
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

// Run 27c64c8d: every capture in the 5 s after a leaving read "cooldown"
// (the fallback blocker replaces the reason), hiding the runaway that caused
// it. left_reason keeps the cause until the next leaving or MissionStart.
void TestLeftReason() {
    pn::State s{};
    pn::Reset(s, true);
    Check(s.left_reason == nullptr, "no leaving yet");
    const std::uint64_t t = Activate(s, 0);
    pn::In in = Healthy(t + 16);
    in.runaway_ms = 2001;
    pn::Out o = pn::Step(s, in);
    Check(LeaveRequests(o) && Is(o.reason, "runaway") && Is(s.left_reason, "runaway"), "runaway leaves; left_reason runaway");
    o = pn::Step(s, Ready(t + 32));
    Check(o.mode == pn::Mode::fallback && Is(o.reason, "runaway"), "the leaving step hands the reason to fallback");
    o = pn::Step(s, Ready(t + 1000));
    Check(o.mode == pn::Mode::fallback && Is(o.reason, "cooldown") && Is(s.left_reason, "runaway"),
          "cooldown replaces the reason, left_reason keeps the cause");
    o = pn::Step(s, Ready(t + 5100));
    Check(Is(o.reason, "settling") && Is(s.left_reason, "runaway"), "settling: still the cause");
    pn::In e = Ready(t + 7100);
    e.window_armed = e.recon_placed = true;
    o = pn::Step(s, e);
    Check(o.mode == pn::Mode::active && Is(s.left_reason, "runaway"), "re-entered: the last leaving's cause stays visible");
    in = Healthy(t + 7200);
    in.wrap_in_batch = 7;
    o = pn::Step(s, in);
    Check(LeaveRequests(o) && Is(s.left_reason, "wrap_in_batch"), "the next leaving replaces it");
    pn::Reset(s, true);
    Check(s.left_reason == nullptr, "MissionStart clears it");
}

void TestFallbackReasons() {
    struct Case { const char* reason; void (*apply)(pn::In&); };
    const Case cases[] = {
        {"defer_census", [](pn::In& in) { in.defer_on = false; }},
        {"install_bytes", [](pn::In& in) { in.install_refusal = "bytes"; }},
        {"install_hook_disabled", [](pn::In& in) { in.install_refusal = "hook_disabled"; }},
        {"install_other", [](pn::In& in) { in.install_refusal = "weird"; }},
        {"not_installed", [](pn::In& in) { in.installed_h1 = false; }},
        {"not_installed", [](pn::In& in) { in.installed_p1 = false; }},
        {"hook_lost", [](pn::In& in) { in.live_p1 = false; }},
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
          e.recon == mgscoop::PartnerRecon::track && e.slot == mgscoop::PartnerSlot::body && !e.p218 && e.mask == 0x2240006u,
          "active: the exact native settings (p218 never on; EVADE passes)");
    Check(pn::Effective(base, true, pn::NativeConfig{true, false, mgscoop::PartnerNativeAim::look}).mask == 0x224004Eu,
          "gun stage mask");
    Check(pn::Effective(base, true, pn::NativeConfig{true, true, mgscoop::PartnerNativeAim::look}).mask == 0x224005Eu,
          "fire stage mask");
    Check((pn::NativeMask(0xFFFFFFFFu, true, true) & 0x20u) == 0u && (pn::NativeMask(0xFFFFFFFFu, false, false) & 0x40000u) != 0u,
          "ACTION 0x20 never passes, EVADE 0x40000 always may");
    Check(pn::NativeMask(0x224005Eu, true, false) == 0x224004Eu, "configured 0x224005E with fire=0 yields no FIRE 0x10");

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

void TestMovementRecovery() {
    pn::DashIn in{};
    in.now_ms = 1000; in.active = in.input_live = in.mask_dash = in.body_ok = in.remote_ok = true;
    in.body_act = in.remote_act = 2; in.body_stance = in.remote_stance = 1;
    in.body_speed = 2; in.remote_speed = 3; in.in_r = 1;
    pn::DashState dash{};
    Check(!pn::DashRelatch(dash, in), "dash mismatch starts debounce");
    in.now_ms = 1299;
    Check(!pn::DashRelatch(dash, in), "dash not replayed before 300ms");
    in.now_ms = 1300;
    Check(pn::DashRelatch(dash, in) && dash.attempts == 1, "standing sprint mismatch gets one admitted DASH retry");
    in.now_ms = 2799;
    Check(!pn::DashRelatch(dash, in), "dash retry gap is 1500ms");
    for (int n = 2; n <= 4; ++n) {
        in.now_ms = 1300 + (n - 1) * 1500;
        Check(pn::DashRelatch(dash, in) && dash.attempts == static_cast<unsigned>(n), "bounded dash retry admitted");
    }
    in.now_ms = 10000;
    Check(!pn::DashRelatch(dash, in), "no fifth dash retry inside 10s");
    in.now_ms = 11000;
    Check(pn::DashRelatch(dash, in) && dash.attempts == 1, "new bounded retry window");
    const auto refuses = [&](pn::DashIn bad, const char* label) {
        pn::DashState fresh{}; bad.now_ms = 1000; pn::DashRelatch(fresh, bad); bad.now_ms = 2000;
        Check(!pn::DashRelatch(fresh, bad) && fresh.attempts == 0, label);
    };
    auto bad = in; bad.body_stance = 3; refuses(bad, "prone DASH cannot become roll");
    bad = in; bad.remote_stance = 2; refuses(bad, "crouch mismatch refuses DASH relatch");
    bad = in; bad.body_act = 28; refuses(bad, "turn transition holds the sprint request without retrying yet");
    bad = in; bad.remote_act = 99; refuses(bad, "ride/deploy refuses DASH relatch");
    bad = in; bad.mask_dash = false; refuses(bad, "DASH mask remains authoritative");
    bad = in; bad.hold_stripped = true; refuses(bad, "HOLD-stripped parity refuses DASH relatch");
    bad = in; bad.input_live = false; refuses(bad, "stale input refuses DASH relatch");
    bad = in; bad.body_speed = 3; refuses(bad, "already sprinting body is not retriggered");
    bad = in; bad.in_r = 0.59; refuses(bad, "weak stick refuses DASH relatch");
    dash = pn::DashState{}; in.now_ms = 20000; in.body_act = 28;
    pn::DashRelatch(dash, in); in.now_ms += 500; in.body_act = 2;
    Check(!pn::DashRelatch(dash, in), "leaving turn transition starts a fresh dash debounce");
    in.now_ms += 300;
    Check(pn::DashRelatch(dash, in), "dash relatches after stable ordinary locomotion");

    pn::SteerState st{}; pn::SteerParams p{}; pn::SteerIn si{};
    si.now_ms = 1000; si.v_z = 2000; si.body_speed_mmps = 4000; si.in_r = 1;
    si.e_z = -600; si.hold_stripped = true;
    auto o = pn::Steer(st, si, p);
    Check(o.mag_scale == 1, "HOLD-strip parity begins without an instantaneous magnitude jump");
    si.now_ms += 100; o = pn::Steer(st, si, p);
    Check(std::fabs(o.mag_scale - 0.9) < 1e-9, "measured overrun trims ten scale points per 100ms");
    for (int i = 0; i < 10; ++i) { si.now_ms += 100; o = pn::Steer(st, si, p); }
    Check(o.mag_scale >= pn::kTrimFloor && o.mag_scale <= pn::kTrimFloor + 1e-9, "parity never drops below native start margin");
    si.body_speed_mmps = 2000; si.now_ms += 100;
    o = pn::Steer(st, si, p);
    Check(std::fabs(o.mag_scale - 0.62) < 1e-9, "parity recovers twenty scale points per 100ms");
    si.hold_stripped = false; si.now_ms += 100;
    Check(pn::Steer(st, si, p).mag_scale == 1, "admitted combat HOLD receives no parity trimming");
    si.hold_stripped = true; si.frozen = true; si.body_speed_mmps = 4000; si.now_ms += 100;
    Check(pn::Steer(st, si, p).mag_scale == 1, "stale/pin/unqualified frozen state refuses parity");
    si.hold_parity_ready = true; si.now_ms += 100;
    Check(pn::Steer(st, si, p).mag_scale < 1, "qualified stripped-aim frozen state permits magnitude parity only");
    si.hold_parity_ready = false; si.frozen = false; si.prone_transition = true; si.now_ms += 100;
    Check(pn::Steer(st, si, p).mag_scale == 1, "stance transition refuses parity");
    si.prone_transition = false; si.in_r = 0.6;
    for (int i = 0; i < 10; ++i) { si.now_ms += 100; o = pn::Steer(st, si, p); }
    Check(o.mag_scale * si.in_r >= pn::kTrimFloor - 1e-9, "partial stick also retains injected 0.42 floor");
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

// Native rates mirror (partner_rates, pure): records in rec+0x3C..+0x50 order.
void TestRates() {
    namespace pr = mgscoop::partner_rates;
    // The game's record of instance 1: every rate 1.0, the dash factor 0.8 this frame.
    pr::Record rec{{1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.8f}};
    pr::Wire w{};
    Check(pr::ToWire(pr::Record{{1.0f, 1.2f, 1.2f, 1.2f, 1.0f, 1.2f}}, w) && w.dash == 1200 && w.crawl == 1000 && w.crawl_factor == 1200,
          "ToWire: dash +0x48, crawl +0x3C, crawl factor +0x40 x 1000");
    Check(!pr::ToWire(pr::Record{{1.0f, 1.0f, 1.0f, 2.1f, 1.0f, 2.1f}}, w) && w.dash == 0, "ToWire refuses > 2.0");
    Check(!pr::ToWire(pr::Record{{0.4f, 1.0f, 0.4f, 1.0f, 1.0f, 1.0f}}, w), "ToWire refuses < 0.5");
    Check(!pr::ToWire(pr::Record{{1.0f, std::nanf(""), 1.0f, 1.0f, 1.0f, 1.0f}}, w), "ToWire refuses non-finite");
    Check(pr::ValidWire(pr::Wire{500, 2000, 1000}) && !pr::ValidWire(pr::Wire{499, 1000, 1000}) && !pr::ValidWire(pr::Wire{1000, 1000, 2001}),
          "ValidWire bounds 500..2000");
    // First apply: originals saved, the three inputs written, products recomputed with the body's own dash factor.
    pr::MirrorState st{};
    pr::Plan p = pr::Apply(st, rec, pr::Wire{1200, 1000, 1200});
    pr::Record after = pr::After(rec, p);
    Check(st.applied && !st.latched && st.writes == 1 && st.orig[0] == 1.0f && st.orig[1] == 1.0f && st.orig[2] == 1.0f &&
          std::fabs(after.f[pr::kDash] - 1.2f) < 1e-6f && std::fabs(after.f[pr::kDashRate] - 0.8f * 1.2f) < 1e-6f &&
          after.f[pr::kCrawl] == 1.0f && std::fabs(after.f[pr::kCrawlFactor] - 1.2f) < 1e-6f &&
          std::fabs(after.f[pr::kCrawlRate] - 1.2f) < 1e-6f && after.f[pr::kDashFactor] == 0.8f && !(p.mask & (1u << pr::kDashFactor)),
          "apply: +0x48 = 1.2, +0x50 = +0x4C * +0x48, +0x44 = +0x3C * +0x40; +0x4C never written");
    // Steady state: nothing to write.
    p = pr::Apply(st, after, pr::Wire{1200, 1000, 1200});
    Check(p.mask == 0 && st.writes == 1 && !p.reapplied, "steady: no write");
    // The game's per-frame dash factor changes +0x4C and +0x50 (with our +0x48): consistent, nothing to write.
    after.f[pr::kDashFactor] = 1.0f;
    after.f[pr::kDashRate] = 1.0f * after.f[pr::kDash];
    p = pr::Apply(st, after, pr::Wire{1200, 1000, 1200});
    Check(p.mask == 0, "the game's own dash factor update is consistent with the mirror");
    // The instance setup rewrites +0x48 (provider 1.0) and +0x50: reapplied, the new original kept.
    after.f[pr::kDash] = 1.0f;
    after.f[pr::kDashRate] = 1.0f;
    p = pr::Apply(st, after, pr::Wire{1200, 1000, 1200});
    after = pr::After(after, p);
    Check(p.reapplied && st.reapplied == 1 && st.orig[0] == 1.0f && std::fabs(after.f[pr::kDash] - 1.2f) < 1e-6f &&
          std::fabs(after.f[pr::kDashRate] - 1.2f) < 1e-6f, "a game rewrite is reapplied");
    // The crawl factor event resets +0x40 to 1.0 (0x11313F1) with +0x44 = +0x3C.
    after.f[pr::kCrawlFactor] = 1.0f;
    after.f[pr::kCrawlRate] = after.f[pr::kCrawl];
    p = pr::Apply(st, after, pr::Wire{1200, 1000, 1200});
    after = pr::After(after, p);
    Check(p.reapplied && st.orig[2] == 1.0f && std::fabs(after.f[pr::kCrawlFactor] - 1.2f) < 1e-6f &&
          std::fabs(after.f[pr::kCrawlRate] - 1.2f) < 1e-6f, "the crawl factor event is reapplied");
    // Restore: the game's values back, products recomputed.
    p = pr::Restore(st, after);
    const pr::Record restored = pr::After(after, p);
    Check(!st.applied && st.restores == 1 && restored.f[pr::kDash] == 1.0f && restored.f[pr::kCrawlFactor] == 1.0f &&
          restored.f[pr::kCrawl] == 1.0f && restored.f[pr::kDashRate] == restored.f[pr::kDashFactor] * 1.0f &&
          restored.f[pr::kCrawlRate] == 1.0f, "restore: the originals, products recomputed");
    Check(pr::Restore(st, restored).mask == 0 && st.restores == 1, "restore twice: nothing");
    // A field the game rewrote after our last write keeps the game's value on restore.
    pr::MirrorState g{};
    pr::Record r2 = pr::After(rec, pr::Apply(g, rec, pr::Wire{1500, 1000, 1000}));
    r2.f[pr::kDash] = 1.1f;  // the setup ran again (provider 1.1)
    const pr::Record back = pr::After(r2, pr::Restore(g, r2));
    Check(std::fabs(back.f[pr::kDash] - 1.1f) < 1e-6f && std::fabs(back.f[pr::kDashRate] - 1.1f * back.f[pr::kDashFactor]) < 1e-6f,
          "restore keeps a rewrite the mirror has not seen");
    // Refusals latch: out-of-range remote (after restoring), non-finite record.
    pr::MirrorState q{};
    pr::Record r3 = pr::After(rec, pr::Apply(q, rec, pr::Wire{1200, 1000, 1000}));
    p = pr::Apply(q, r3, pr::Wire{2500, 1000, 1000});
    const pr::Record r4 = pr::After(r3, p);
    Check(q.latched && !q.applied && q.reason == pr::Reason::range && r4.f[pr::kDash] == 1.0f, "out-of-range remote: restored, latched");
    Check(pr::Apply(q, r4, pr::Wire{1200, 1000, 1000}).mask == 0 && q.latched, "latched: nothing more");
    pr::MirrorState n{};
    pr::Record bad = rec;
    bad.f[pr::kDashFactor] = std::numeric_limits<float>::infinity();
    Check(pr::Apply(n, bad, pr::Wire{1200, 1000, 1000}).mask == 0 && n.latched && n.reason == pr::Reason::nonfinite, "non-finite record: latched");
    pr::MirrorState l{};
    pr::Latch(l, pr::Reason::rec_vtable);
    Check(l.latched && pr::Apply(l, rec, pr::Wire{1200, 1000, 1000}).mask == 0 && std::strcmp(pr::ReasonName(l.reason), "rec_vtable") == 0 &&
          std::strcmp(pr::MirrorName(pr::Mirror::ok), "ok") == 0, "caller refusals latch; names");
    Check(std::strcmp(pr::ReasonName(pr::Reason::owner), "owner") == 0 && static_cast<unsigned>(pr::Reason::owner) == 9u,
          "owner (not instance 1's own record) keeps its wire value 9");
    pr::Reset(l);
    Check(!l.latched && !l.applied && l.writes == 0, "Reset clears the mission state");
}

} // namespace

// Native weapons (docs/native-puppet.md "Weapons"): the gun record rules of
// the magazine getter 0x10435D0, CanReload 0x1051F30 and the reload gate, and
// the magazine mirror.
void TestWeapons() {
    pn::GunFields g{};
    g.flags27c = 1u << 6;  // class 1
    g.equip = 46;
    g.cap = 5;
    g.cap_sub = 9;
    g.mag = 3;
    g.mag_sub = 1;
    g.stock = 0xFFFF;
    Check(pn::GunClass(g.flags27c) == 1 && pn::GunCapacity(g) == 5 && pn::GunMagazine(g) == 3, "capacity rec+0x210, magazine rec+0x25C");
    Check(pn::GunMagazineOffset(g.flags27c) == 0x25C && pn::GunMagazineOffset(g.flags27c | 0x200u) == 0x25E, "the live magazine field");
    pn::GunFields sub = g;
    sub.flags27c |= 0x200u;
    Check(pn::GunCapacity(sub) == 9 && pn::GunMagazine(sub) == 1, "flags bit 9: the sub magazine and capacity");
    pn::GunFields dbl = g;
    dbl.flags27c |= 0x100000u;
    Check(pn::GunCapacity(dbl) == 5, "bit 20 doubles only equip 0x42B..0x42E");
    dbl.equip = 0x42B;
    Check(pn::GunCapacity(dbl) == 10, "bit 20 doubles equip 0x42B");
    dbl.equip = 0x42E;
    Check(pn::GunCapacity(dbl) == 10, "... and 0x42E");
    dbl.equip = 0x42F;
    Check(pn::GunCapacity(dbl) == 5, "... not 0x42F");
    pn::GunFields half = g;
    half.flags27c |= 0x80000u;
    Check(pn::GunCapacity(half) == 2, "bit 19 halves");
    pn::GunFields full = g;
    full.gun3c0 = 0x10000000u;
    Check(pn::GunMagazine(full) == 5, "rec+0x3C0 bit 28: the getter returns the capacity");
    // CanReload.
    Check(pn::GunCanReload(g), "3/5 with stock: reload allowed");
    pn::GunFields r = g;
    r.mag = 5;
    Check(!pn::GunCanReload(r), "a full magazine refuses");
    r = g; r.flags280 = 0x80000u;
    Check(!pn::GunCanReload(r), "the no-reload bit refuses");
    r = g; r.stock = 3;
    Check(!pn::GunCanReload(r), "stock - magazine == 0 refuses");
    r = g; r.gun3c0 = 0x20000000u;
    Check(pn::GunCanReload(r), "bit 29: the capacity-based stock check");
    r.cap = 0;
    Check(!pn::GunCanReload(r), "bit 29 with capacity 0 refuses");
    Check(!pn::GunCanReload(full), "a full-magazine gun never reloads");
    // The reload gate.
    Check(pn::GunReloadGate(g, 0), "gate: class 1, equip, 0x3D5 != 4");
    Check(!pn::GunReloadGate(g, 4), "gate: rec+0x3D5 == 4 refuses");
    r = g; r.equip = 0;
    Check(!pn::GunReloadGate(r, 0), "gate: no equip refuses");
    r = g; r.flags27c = (2u << 6);
    Check(!pn::GunReloadGate(r, 0), "gate: class 2 refuses");
    r = g; r.flags27c |= 0x800u;
    Check(!pn::GunReloadGate(r, 0), "gate: (flags & 0x1800) == 0x800 refuses");
    r.flags27c |= 0x1000u;
    Check(pn::GunReloadGate(r, 0), "gate: 0x1800 passes");
    // The magazine mirror.
    using M = pn::MagIn;
    Check(pn::MirrorMagazine(M{30, 30, 5, false}) == 5, "a full remote stays full (no reload)");
    Check(pn::MirrorMagazine(M{0, 30, 5, false}) == 0, "an empty remote stays empty");
    Check(pn::MirrorMagazine(M{29, 30, 5, false}) == 4, "almost full maps below the capacity (reloadable)");
    Check(pn::MirrorMagazine(M{1, 30, 5, false}) == 1, "one round maps to at least 1");
    Check(pn::MirrorMagazine(M{15, 30, 5, false}) == 3, "half rounds up");
    Check(pn::MirrorMagazine(M{3, 5, 5, false}) == 3, "same capacity: the same count");
    Check(pn::MirrorMagazine(M{0, 30, 5, true, true}) == 1, "a FIRE edge that spent the remote's last round shows it (1)");
    Check(pn::MirrorMagazine(M{0, 30, 5, true, false}) == 0, "a FIRE edge on an already empty remote (dry fire) stays 0");
    Check(pn::MirrorMagazine(M{0, 30, 5, false, true}) == 0, "no FIRE edge: an empty remote stays empty");
    Check(pn::MirrorMagazine(M{4, 5, 30, false}) == 24, "a larger partner capacity scales up");
    Check(pn::MirrorMagazine(M{1, 2, 1, false}) == 0 && pn::MirrorMagazine(M{2, 2, 1, false}) == 1, "capacity 1: not full is empty");
    Check(pn::MirrorMagazine(M{1, 2, 1, true}) == 1, "capacity 1: a FIRE edge of a loaded remote loads it");
    Check(pn::MirrorMagazine(M{1, 0, 5, true}) == 0 && pn::MirrorMagazine(M{1, 5, 0, true}) == 0, "zero capacities write nothing useful");
    for (std::uint32_t cap = 1; cap <= 50; ++cap)
        for (std::uint16_t rc = 1; rc <= 40; ++rc)
            for (std::uint16_t rm = 0; rm <= rc; ++rm) {
                const std::uint32_t out = pn::MirrorMagazine(M{rm, rc, cap, false});
                const bool ok = out <= cap && ((rm == rc) == (out == cap)) && (rm == 0 ? out == 0 : (cap == 1 || out >= 1));
                if (!ok) Check(false, "mirror invariants: <= cap, full iff remote full, empty iff remote empty");
            }
    Check(true, "mirror invariants hold for capacities 1..50 x 1..40");
}

// A checked store can be unavailable during weapon draw or checkpoint
// recovery. The exact history used by MirrorMag must retain the observation
// until accepted, even without another ammo change, FIRE edge or reload tap.
void TestMagazineHistory() {
    pn::MagazineHistory mirror_history{};
    std::uint16_t fake_store_word=0;
    bool fake_store_ready=false;
    unsigned fake_store_writes=0;
    auto offer=[&](std::uint16_t remote_mag, std::uint8_t body_slot, bool fire_edge=false) {
        const auto attempt=pn::PrepareMagazine(mirror_history,remote_mag,5,body_slot,fire_edge,false);
        if(!attempt.needed || !fake_store_ready) return false;
        const auto target=static_cast<std::uint16_t>(pn::MirrorMagazine(
            pn::MagIn{remote_mag,5,5,fire_edge,attempt.remote_was_loaded}));
        if(fake_store_word!=target) {fake_store_word=target;++fake_store_writes;}
        pn::CommitMagazine(mirror_history,remote_mag,5,body_slot);
        return true;
    };
    Check(!offer(3,0) && !mirror_history.seen && !mirror_history.slot_seen,
          "unavailable initial store does not consume the sample");
    Check(pn::PrepareMagazine(mirror_history,3,5,0,false,false).needed,
          "unchanged initial sample retries without an input edge");
    fake_store_ready=true;
    Check(offer(3,0) && fake_store_word==3 && mirror_history.seen && fake_store_writes==1,
          "ready store accepts the unchanged sample");
    Check(!offer(3,0) && fake_store_writes==1,"accepted stable sample needs no repeat write");

    mirror_history.freeze_pending=true;fake_store_ready=false;
    Check(!offer(3,0) && mirror_history.freeze_pending && mirror_history.remote_mag==3,
          "refused reload reconcile remains pending");
    Check(!offer(3,0) && pn::PrepareMagazine(mirror_history,3,5,0,false,false).reconcile,
          "same reload sample keeps reconciling until acceptance");
    fake_store_ready=true;
    Check(offer(3,0) && !mirror_history.freeze_pending && fake_store_writes==1,
          "validated no-write acceptance consumes reconcile once");
    Check(!offer(3,0),"accepted reconcile is not repeated");

    fake_store_ready=false;
    Check(!offer(3,2) && mirror_history.body_slot==0 &&
          pn::PrepareMagazine(mirror_history,3,5,2,false,false).needed,
          "refused slot change retains prior accepted slot and retries stable ammo");
    fake_store_ready=true;
    Check(offer(3,2) && mirror_history.body_slot==2 && !offer(3,2),
          "checked new-slot acceptance consumes the slot change once");
    fake_store_ready=false;
    Check(!offer(0,2) && mirror_history.remote_mag==3 &&
          pn::PrepareMagazine(mirror_history,0,5,2,true,false).remote_was_loaded,
          "refusal retains the previous loaded sample for a last-round FIRE edge");
    fake_store_ready=true;
    Check(offer(0,2,true) && fake_store_word==1 && mirror_history.remote_mag==0 &&
          !pn::PrepareMagazine(mirror_history,0,5,2,true,false).remote_was_loaded,
          "accepted last-round sample commits; next empty FIRE cannot reuse it");
    mirror_history=pn::MagazineHistory{};
    Check(pn::PrepareMagazine(mirror_history,0,5,2,false,false).needed && !mirror_history.freeze_pending,
          "mission reset forgets accepted history and pending reconcile");
}

// Online play (docs/native-puppet.md "Online"): network leaves never latch,
// quality-gated re-entry, the quiet remote, the flap guard, remote_caps,
// hold=net, the link-quality tracker, reason codes, native.net JSON and the
// capability message.
namespace pnn = mgscoop::partner_native_net;
namespace pcw = mgscoop::partner_caps_wire;
std::uint64_t ActivateOnline(pn::State& s, std::uint64_t t, const pn::Params& p) {
    for (; t < 1000000; t += 100) {
        pn::In in = Ready(t);
        in.window_armed = in.recon_placed = true;
        in.quality_good_ms = 60000;
        if (pn::Step(s, in, p).mode == pn::Mode::active) return t;
    }
    Check(false, "online activation");
    return t;
}
void TestOnline() {
    const pn::Params p = pn::OnlineParams();
    Check(p.online && p.stale_hold_ms == 1500 && p.link_hold_ms == 1500 && p.cooldown_ms == 5000 && p.latch_fallbacks == 3,
          "online params: 1.5 s stale/link holds, the hard latch unchanged");
    Check(pn::NetReason("net_input") && pn::NetReason("net_link") && pn::NetReason("net_quiet") && pn::NetReason("range_hidden") &&
              pn::NetReason("unrealized") && !pn::NetReason("thread") && !pn::NetReason("cost") && !pn::NetReason("input_stale") &&
              !pn::NetReason(nullptr), "network reasons");
    // Network leaves never latch; each re-entry waits the quality gate.
    {
        pn::State s{};
        pn::Reset(s, true);
        std::uint64_t t = ActivateOnline(s, 0, p);
        for (int round = 0; round < 5; ++round) {
            pn::In in = Healthy(t + 100);
            in.input_age_ms = 400;
            in.quality_good_ms = 0;
            pn::Step(s, in, p);
            in.now_ms = t + 1599;
            Check(pn::Step(s, in, p).mode == pn::Mode::active, "online: 1499 ms of stale input stays");
            in.now_ms = t + 1600;
            const pn::Out o = pn::Step(s, in, p);
            Check(LeaveRequests(o) && Is(o.reason, "net_input"), "online: 1500 ms of stale input leaves with net_input");
            Check(s.fallbacks == 0 && s.net_leaves == static_cast<std::uint32_t>(round + 1) && s.left_net, "a network leave never counts");
            in.now_ms = t + 1700;
            Check(pn::Step(s, in, p).mode == pn::Mode::fallback, "leaving -> fallback (never latched)");
            // Cooldown 2 s, then the quality gate, then the settle hold.
            pn::In ready = Ready(t + 3000);
            ready.window_armed = ready.recon_placed = true;
            ready.quality_good_ms = 0;
            Check(Is(pn::Step(s, ready, p).reason, "cooldown"), "network cooldown");
            ready.now_ms = t + 3700;
            Check(Is(pn::Step(s, ready, p).reason, "net_quality"), "the link quality must hold first");
            ready.quality_good_ms = 2000;
            ready.now_ms = t + 3800;
            Check(Is(pn::Step(s, ready, p).reason, "settling"), "quality held 2 s: settling");
            t = ActivateOnline(s, t + 3900, p);
            Check(s.net_reentries == static_cast<std::uint32_t>(round + 1), "re-entries counted");
        }
        Check(s.mode == pn::Mode::active && s.fallbacks == 0 && s.hard_leaves == 0, "five network leaves: still native");
        // A hard reason keeps the latch.
        for (int i = 0; i < 3; ++i) {
            pn::In in = Healthy(t + 100);
            in.thread_ok = false;
            const pn::Out o = pn::Step(s, in, p);
            Check(LeaveRequests(o) && Is(o.reason, "thread") && !s.left_net, "hard reasons still leave at once");
            in.now_ms = t + 200;
            pn::Step(s, in, p);
            t = i < 2 ? ActivateOnline(s, t + 6000, p) : t + 300;
        }
        Check(s.mode == pn::Mode::latched && s.hard_leaves == 3, "three hard leaves latch the mission");
    }
    // Link drops: net_link after 1.5 s.
    {
        pn::State s{};
        pn::Reset(s, true);
        const std::uint64_t t = ActivateOnline(s, 0, p);
        pn::In in = Healthy(t + 100);
        in.link_live = false;
        pn::Step(s, in, p);
        in.now_ms = t + 1599;
        Check(pn::Step(s, in, p).mode == pn::Mode::active, "online: link hold 1499 ms");
        in.now_ms = t + 1600;
        Check(Is(pn::Step(s, in, p).reason, "net_link") && s.fallbacks == 0, "online: link down 1.5 s leaves with net_link, no count");
    }
    // The quiet remote: neutral in place up to 10 s, then net_quiet.
    {
        pn::State s{};
        pn::Reset(s, true);
        const std::uint64_t t = ActivateOnline(s, 0, p);
        pn::In in = Healthy(t + 100);
        in.input_age_ms = 3000;
        in.remote_quiet = true;
        pn::Out o = pn::Step(s, in, p);
        Check(o.mode == pn::Mode::active && Is(o.reason, "remote_quiet") && s.quiet_episodes == 1, "a quiet remote holds the body");
        in.now_ms = t + 9000;
        Check(pn::Step(s, in, p).mode == pn::Mode::active, "quiet 8.9 s: still native (no stale leave)");
        in.now_ms = t + 9500;
        in.input_age_ms = 20;
        in.remote_quiet = false;
        o = pn::Step(s, in, p);
        Check(o.mode == pn::Mode::active && Is(o.reason, "ok"), "the remote resumes: native continues, reason ok");
        in.now_ms = t + 9600;
        in.input_age_ms = 3000;
        in.remote_quiet = true;
        pn::Step(s, in, p);
        Check(s.quiet_episodes == 2, "a second episode");
        in.now_ms = t + 19599;
        Check(pn::Step(s, in, p).mode == pn::Mode::active, "9.999 s quiet stays");
        in.now_ms = t + 19600;
        o = pn::Step(s, in, p);
        Check(LeaveRequests(o) && Is(o.reason, "net_quiet") && s.fallbacks == 0, "10 s quiet: net_quiet, no count");
        in.now_ms = t + 19700;
        pn::Step(s, in, p);
        pn::In fb = Ready(t + 25000);
        fb.input_age_ms = 3000;
        fb.remote_quiet = true;
        fb.quality_good_ms = 60000;
        Check(Is(pn::Step(s, fb, p).reason, "remote_quiet"), "fallback names the quiet remote");
        // The lab policy treats the same as input_stale (unchanged).
        pn::State l{};
        pn::Reset(l, true);
        const std::uint64_t u = Activate(l, 0);
        pn::In li = Healthy(u + 100);
        li.input_age_ms = 3000;
        li.remote_quiet = true;
        pn::Step(l, li);
        li.now_ms = u + 1100;
        Check(Is(pn::Step(l, li).reason, "input_stale") && l.fallbacks == 1, "lab policy: the qualified input_stale leave");
    }
    // range_hidden and unrealized (the partner streamed out) do not count online.
    {
        pn::State s{};
        pn::Reset(s, true);
        std::uint64_t t = ActivateOnline(s, 0, p);
        for (int i = 0; i < 4; ++i) {
            pn::In in = Healthy(t + 100);
            in.range_hidden = true;
            Check(Is(pn::Step(s, in, p).reason, "range_hidden"), "range_hidden leaves at once");
            in.now_ms = t + 200;
            pn::Step(s, in, p);
            t = ActivateOnline(s, t + 5000, p);
        }
        Check(s.fallbacks == 0 && s.mode == pn::Mode::active, "four range leaves online: never latched");
    }
    // The flap guard: more than 6 network leaves in 5 minutes keep the clips until the quality held 30 s.
    {
        pn::State s{};
        pn::Reset(s, true);
        std::uint64_t t = ActivateOnline(s, 0, p);
        for (int i = 0; i < 7; ++i) {
            pn::In in = Healthy(t + 100);
            in.link_live = false;
            pn::Step(s, in, p);
            in.now_ms = t + 1600;
            pn::Step(s, in, p);
            in.now_ms = t + 1700;
            pn::Step(s, in, p);
            if (i < 6) t = ActivateOnline(s, t + 4000, p);
        }
        Check(s.flap && s.net_leaves == 7, "seven network leaves: flapping");
        pn::In ready = Ready(t + 10000);
        ready.window_armed = ready.recon_placed = true;
        ready.quality_good_ms = 20000;
        Check(Is(pn::Step(s, ready, p).reason, "net_flap"), "flapping: clips until the quality holds 30 s");
        ready.quality_good_ms = 30000;
        ready.now_ms = t + 10100;
        Check(Is(pn::Step(s, ready, p).reason, "settling") && !s.flap, "30 s of good quality clears the flap");
    }
    // A remote that sends no input frames: remote_caps (online only).
    {
        pn::State s{};
        pn::Reset(s, true);
        pn::In in = Ready(0);
        in.remote_caps_refused = true;
        Check(Is(pn::Step(s, in, p).reason, "remote_caps"), "online: remote_caps blocks the entry");
        pn::State l{};
        pn::Reset(l, true);
        Check(Is(pn::Step(l, in).reason, "settling"), "lab: the capability flag is ignored");
    }
    // hold=net under the native drive.
    {
        pn::DriveSettings base{};
        base.hold = mgscoop::PartnerInputHold::pass;
        base.mask = 0x224005E;
        Check(pn::Effective(base, true, pn::NativeConfig{true, true, mgscoop::PartnerNativeAim::look, true}).hold ==
                  mgscoop::PartnerInputHold::net &&
                  pn::Effective(base, true, pn::NativeConfig{true, true, mgscoop::PartnerNativeAim::look}).hold == mgscoop::PartnerInputHold::own,
              "partner_native_hold=net: the sender's 12-frame words; default own");
    }
    // Link quality.
    {
        pnn::Quality q{};
        pnn::Reset(q);
        std::uint64_t now = 1000;
        std::uint32_t seq = 1;
        for (int i = 0; i < 200; ++i, now += 16) {
            pnn::Observe(q, pnn::QualityIn{now, true, true, seq++, 3});
        }
        Check(q.good && pnn::GoodMs(q, now) >= 3000 && q.p95_ms <= 17 && q.p50_ms >= 15 && q.gaps_100 == 0,
              "60 Hz frames: good, p95 16 ms");
        // A 400 ms gap: bad while it lasts and when it ends.
        for (int i = 0; i < 25; ++i, now += 16)
            pnn::Observe(q, pnn::QualityIn{now, true, true, seq - 1, static_cast<std::uint32_t>(16 * i + 19)});
        Check(!q.good && pnn::GoodMs(q, now) == 0, "a frame older than 250 ms is bad");
        pnn::Observe(q, pnn::QualityIn{now, true, true, seq++, 3});
        Check(!q.good && q.gaps_250 == 1 && q.max_gap_ms >= 400, "the gap that just ended is bad and counted");
        const std::uint64_t gap_end = now;
        for (int i = 0; i < 200; ++i) {
            now += 16;
            pnn::Observe(q, pnn::QualityIn{now, true, true, seq++, 3});
        }
        Check(q.good && pnn::GoodMs(q, now) < now - gap_end && pnn::GoodMs(q, now) + 2100 >= now - gap_end,
              "good again once the gap left the 2 s window");
        pnn::Observe(q, pnn::QualityIn{now + 16, false, true, seq++, 3});
        Check(!q.good, "link down is bad");
        // Jitter: every 4th frame 130 ms late: p95 above 120 ms is bad.
        pnn::Quality j{};
        now = 0;
        for (int i = 0; i < 200; ++i) {
            now += (i % 4 == 3) ? 130 : 10;
            pnn::Observe(j, pnn::QualityIn{now, true, true, static_cast<std::uint32_t>(i + 1), 0});
        }
        Check(!j.good && j.p95_ms >= 120, "p95 >= 120 ms is bad");
    }
    // Reason codes and the native.net JSON.
    {
        Check(pnn::ReasonCode("ok") == 1 && pnn::ReasonCode(nullptr) == pnn::kReasonNone && pnn::ReasonCode("") == pnn::kReasonNone &&
              pnn::ReasonCode("no_such_reason") == pnn::kReasonUnknown && Is(pnn::ReasonName(pnn::kReasonUnknown), "other") &&
              Is(pnn::ReasonName(pnn::kReasonNone), ""), "reason code edges");
        for (const char* r : {"ok", "thread", "cost", "install_bytes", "net_input", "net_quiet", "remote_quiet", "net_flap", "remote_caps",
                              "settling", "legacy", "range_hidden", "input_stale", "diverging", "unwindowed", "health"})
            Check(Is(pnn::ReasonName(pnn::ReasonCode(r)), r), "reason codes round-trip");
        Check(Is(pnn::DriveName(pnn::Drive::native), "native") && Is(pnn::DriveName(pnn::Drive::clips), "clips") &&
              Is(pnn::DriveName(pnn::Drive::off), "off") && Is(pnn::DriveName(static_cast<pnn::Drive>(9)), "unknown"), "drive names");
        pnn::NetTelemetry n{};
        std::memcpy(n.policy, "online", sizeof("online"));
        std::memcpy(n.local_reason, "q\"uote", sizeof("q\"uote"));
        n.local_drive = pnn::Drive::native;
        char json[1024]{};
        const std::size_t size = pnn::FormatNetJson(n, json, sizeof(json));
        Check(size > 0 && json[0] == '{' && json[size - 1] == '}' && std::strstr(json, "\"policy\":\"online\"") &&
              std::strstr(json, "\"local_reason\":\"q\\\"uote\"") && std::strstr(json, "\"local_drive\":\"native\"") &&
              std::strstr(json, "\"caps\":{"), "native.net JSON");
        char small[32]{};
        Check(pnn::FormatNetJson(n, small, sizeof(small)) == 0 && small[0] == 0, "too small: nothing");
    }
    // The capability message.
    {
        pcw::Caps c{};
        c.revision = 3; c.epoch = 7; c.features = pcw::Feature::InputSender | pcw::Feature::NativeDrive; c.profile = 2;
        c.drive = 3; c.reason = pnn::ReasonCode("ok"); c.mode = 3; c.policy = 1;
        std::uint8_t b[pcw::kSize]{};
        Check(pcw::Encode(c, b) == pcw::kSize && b[0] == 0x43 && pcw::IsCaps(b, sizeof(b)), "caps encode");
        pcw::Caps d{};
        Check(pcw::Decode(b, sizeof(b), 7, d) == pcw::Result::ok && d.profile == 2 && d.drive == 3 && d.policy == 1 && d.revision == 3,
              "caps decode");
        Check(pcw::Decode(b, sizeof(b), 8, d) == pcw::Result::stale_epoch && pcw::Decode(b, 15, 7, d) == pcw::Result::size,
              "stale epoch, size");
        std::uint8_t bad[pcw::kSize]{};
        std::memcpy(bad, b, sizeof(b)); bad[9] = 3;
        Check(pcw::Decode(bad, sizeof(bad), 7, d) == pcw::Result::field, "profile out of range");
        std::memcpy(bad, b, sizeof(b)); bad[15] = 1;
        Check(pcw::Decode(bad, sizeof(bad), 7, d) == pcw::Result::reserved, "reserved bytes");
        std::memcpy(bad, b, sizeof(b)); bad[8] = 0x80;
        Check(pcw::Decode(bad, sizeof(bad), 7, d) == pcw::Result::field, "unknown feature bit");
        c.revision = 0;
        Check(pcw::Encode(c, b) == 0, "revision 0 is never encoded");
        // The store: newer revisions replace, older ones of the same session do not.
        pcw::Store st{};
        c.revision = 5;
        pcw::Encode(c, b);
        Check(pcw::Accept(st, b, sizeof(b), 7) && st.have && st.caps.revision == 5, "store");
        c.revision = 4;
        pcw::Encode(c, b);
        Check(!pcw::Accept(st, b, sizeof(b), 7) && st.caps.revision == 5 && st.stale == 1, "an older revision is stale");
        // Sender pacing: at once per session, on change >= 1 s, heartbeat 5 s.
        pcw::Sender sd{};
        pcw::Caps next{};
        next.drive = 2;
        Check(!pcw::Due(sd, next, 0, 100), "no session: nothing");
        Check(pcw::Due(sd, next, 9, 100) && next.epoch == 9 && next.revision == 1, "first send at once");
        pcw::Sent(sd, next, 100);
        pcw::Caps same = next;
        Check(!pcw::Due(sd, same, 9, 600), "unchanged: wait for the heartbeat");
        same.drive = 3;
        Check(!pcw::Due(sd, same, 9, 600), "a change within 1 s waits");
        Check(pcw::Due(sd, same, 9, 1100) && same.revision == 2, "a change after 1 s goes out");
        pcw::Sent(sd, same, 1100);
        pcw::Caps hb = same;
        Check(!pcw::Due(sd, hb, 9, 6000) && pcw::Due(sd, hb, 9, 6100), "the 5 s heartbeat");
        pcw::Caps fresh = same;
        Check(pcw::Due(sd, fresh, 10, 1200), "a new session sends at once");
    }
}

// Aim parity (docs/native-puppet.md "Facing", the aim-target mirror): the
// look direction convention of 0xFFAC00 / 0x1042F80, the target, the
// aim-turn predicate 0x10A0AB0 and the debug command.
void TestAim() {
    namespace pa = mgscoop::partner_aim;
    using mgscoop::PartnerNativeAimPt;
    // The convention: yaw 0 looks along +z, yaw +90 deg along +x, pitch > 0 looks down (-y).
    double d[3]{};
    pa::LookDir(0.0, 0.0, d);
    Check(std::fabs(d[0]) < 1e-12 && std::fabs(d[1]) < 1e-12 && std::fabs(d[2] - 1.0) < 1e-12, "aim: yaw 0 = +z");
    pa::LookDir(0.0, kPi / 2, d);
    Check(std::fabs(d[0] - 1.0) < 1e-12 && std::fabs(d[2]) < 1e-12, "aim: yaw +90 = +x");
    pa::LookDir(0.3, 0.0, d);
    Check(d[1] < 0.0 && std::fabs(d[1] + std::sin(0.3)) < 1e-12, "aim: pitch > 0 looks down");
    for (double pitch : {-1.5, -0.4, 0.0, 0.25, 1.2})
        for (double yaw : {-3.1, -1.0, 0.0, 0.7, 2.9}) {
            pa::LookDir(pitch, yaw, d);
            double p2 = 0.0, y2 = 0.0;
            if (!pa::DirAngles(d, p2, y2) || std::fabs(p2 - pitch) > 1e-9 || std::fabs(y2 - yaw) > 1e-9) Check(false, "aim: LookDir/DirAngles round trip");
        }
    Check(true, "aim: LookDir and DirAngles invert each other (the 0xFFAC00 atan2 pair)");
    const double zero[3] = {0.0, 0.0, 0.0}, nan3[3] = {std::nan(""), 0.0, 1.0};
    double p0 = 1.0, y0 = 1.0;
    Check(!pa::DirAngles(zero, p0, y0) && !pa::DirAngles(nan3, p0, y0), "aim: no angles for a zero or non-finite direction");
    // The target.
    pa::TargetIn in{};
    in.setting = PartnerNativeAimPt::look;
    in.active = true;
    in.body_ok = true;
    in.body[0] = 100.0f; in.body[1] = 340.0f; in.body[2] = -50.0f;
    in.look_ok = true;
    in.look_pitch = 0.1f; in.look_yaw = -1.0f;
    pa::TargetOut o = pa::Target(in);
    pa::LookDir(0.1f, -1.0f, d);
    Check(o.want && o.valid && o.src == pa::Source::look && o.reason == pa::Reason::none &&
              std::fabs(o.p[0] - (100.0 + 100.0 * d[0])) < 1e-3 && std::fabs(o.p[1] - (340.6 + 100.0 * d[1])) < 1e-3 &&
              std::fabs(o.p[2] - (-50.0 + 100.0 * d[2])) < 1e-3,
          "aim: look target = body + 0.6 m up + 100 m along the remote's look");
    const double dist = std::sqrt(std::pow(o.p[0] - 100.0, 2) + std::pow(o.p[1] - 340.0, 2) + std::pow(o.p[2] + 50.0, 2));
    Check(dist > pa::kMinRangeM && dist < pa::kMaxRangeM && std::fabs(dist - 100.0) < 2.0, "aim: about 100 m from the body");
    in.setting = PartnerNativeAimPt::off;
    o = pa::Target(in);
    Check(!o.want && !o.valid && o.reason == pa::Reason::off, "aim: off wants nothing (the hook restores)");
    in.setting = PartnerNativeAimPt::look;
    in.active = false;
    o = pa::Target(in);
    Check(!o.want && o.reason == pa::Reason::inactive, "aim: inactive (no native drive or no gun) wants nothing");
    in.active = true;
    in.body_ok = false;
    o = pa::Target(in);
    Check(o.want && !o.valid && o.reason == pa::Reason::no_body, "aim: no body: wanted, not valid (the hook holds)");
    in.body_ok = true;
    in.look_ok = false;
    o = pa::Target(in);
    Check(o.want && !o.valid && o.reason == pa::Reason::no_look, "aim: no live look: wanted, not valid");
    in.look_ok = true;
    in.look_yaw = std::nanf("");
    o = pa::Target(in);
    Check(o.want && !o.valid && o.reason == pa::Reason::no_look, "aim: a non-finite look is no look");
    in.look_yaw = -1.0f;
    in.body[0] = 70000.0f;
    o = pa::Target(in);
    Check(o.want && !o.valid && o.reason == pa::Reason::range, "aim: a target beyond the coordinate bound refuses (range)");
    in.body[0] = 100.0f;
    // point: the sender's own aim direction when present, else the look.
    in.setting = PartnerNativeAimPt::point;
    o = pa::Target(in);
    Check(o.valid && o.src == pa::Source::look, "aim: point without a v6 direction falls back to the look");
    in.point_ok = true;
    in.point_pitch = in.point_yaw = 0.0f;
    o = pa::Target(in);
    Check(o.valid && o.src == pa::Source::point && std::fabs(o.p[2] - 50.0f) < 1e-3f && std::fabs(o.p[1] - 340.6f) < 1e-3f,
          "aim: point uses the normalized sender direction");
    in.point_pitch = std::nanf("");
    in.look_ok = false;
    o = pa::Target(in);
    Check(!o.valid && o.reason == pa::Reason::no_look, "aim: invalid point without a look refuses");
    const float big[3] = {65536.0f, 0.0f, 0.0f}, ok3[3] = {-65535.0f, 1.0f, 2.0f}, nanp[3] = {0.0f, std::nanf(""), 0.0f};
    Check(!pa::CoordOk(big) && pa::CoordOk(ok3) && !pa::CoordOk(nanp), "aim: the hook's coordinate bound");
    // The aim-turn predicate: |(0.5 - r60) * pi| > 80 degrees (r60 = 0.5 - yaw / 180 deg).
    Check(!pa::TurnPredicate(0.5f) && !pa::TurnPredicate(0.94f) && pa::TurnPredicate(0.95f) && pa::TurnPredicate(0.05f) &&
              !pa::TurnPredicate(0.06f) && !pa::TurnPredicate(std::nanf("")),
          "aim: the predicate turns beyond 80 degrees of aim (r60 0.944 / 0.056)");
    Check(pa::RelYawCdeg(0.5f) == 0 && pa::RelYawCdeg(0.95f) == -8100 && pa::RelYawCdeg(0.0f) == 9000 && pa::RelYawCdeg(std::nanf("")) == INT32_MIN,
          "aim: the tested yaw in centidegrees");
    // The recorded near-hit inverse, range classes, refusal gates and convention latch.
    pa::GunDirIn gi{};
    gi.gun_ok = gi.rate_ok = gi.body_ok = true; gi.stance = 2;
    gi.body_yaw = static_cast<float>(4.8 * pa::kCdegToRad * 100);
    gi.r5c = .625f; gi.r60 = .517f;
    auto gd = pa::GunDir(gi);
    Check(gd.ok && std::abs(pa::PitchCdeg(gd.pitch) - 1875) <= 1 && std::abs(pa::YawCdeg(gd.yaw) - 174) <= 1,
          "gun direction: crouch near-hit recorded inverse");
    gi.stance = 3; gi.r5c = .770f; gi.r60 = .541f; gd = pa::GunDir(gi);
    Check(!gd.ok && gd.reason == pa::DirReason::prone_frame &&
              std::strcmp(pa::DirReasonName(gd.reason), "prone_frame") == 0,
          "gun direction: captured prone rates refuse the unqualified orientation");
    pa::DirCheck prone_check{};
    const float prone_body[3]{}, prone_aim[3]{0, 0, 100};
    for (unsigned n = 0; n <= pa::kCheckLatch; ++n)
        Check(pa::CheckStep(prone_check, gd, gi, prone_body, prone_aim) == pa::CheckResult::skipped,
              "gun direction: refused prone never qualifies a convention check");
    Check(prone_check.checks == 0 && prone_check.misses == 0 && !prone_check.latched,
          "gun direction: refused prone does not consume the standing convention budget");
    in.setting = PartnerNativeAimPt::point; in.active = in.body_ok = in.look_ok = true;
    in.point_ok = gd.ok; in.stance = 3; in.look_pitch = in.look_yaw = 0;
    o = pa::Target(in);
    Check(o.valid && o.src == pa::Source::look && std::fabs(o.p[1] - in.body[1]) < 1e-3f,
          "aim: unqualified prone direction uses the existing prone-pivot look fallback");
    in.point_ok = true; in.point_pitch = .25f; in.point_yaw = 1.0f;
    o = pa::Target(in);
    Check(o.valid && o.src == pa::Source::look && std::fabs(o.p[1] - in.body[1]) < 1e-3f,
          "aim: receiver prone stance rejects a valid point group from a delayed or older experimental sender");
    gi.stance = 1; gi.gun_class = 2; gi.r5c = 1; gi.r60 = 0; gd = pa::GunDir(gi);
    Check(pa::PitchCdeg(gd.rel_pitch) == 4500 && pa::YawCdeg(gd.rel_yaw) == 4500, "gun direction: class2 ranges");
    gi.gun_class = 6; gd = pa::GunDir(gi);
    Check(pa::PitchCdeg(gd.rel_pitch) == 4000 && pa::YawCdeg(gd.rel_yaw) == 7500, "gun direction: class6 ranges");
    gi.gun_class = 3; Check(pa::GunDir(gi).reason == pa::DirReason::gun_class, "gun direction: other class path refuses");
    gi.gun_class = 0; gi.gun3c0 = 0x2000; Check(pa::GunDir(gi).reason == pa::DirReason::camera, "gun direction: camera relative refuses");
    gi.gun3c0 = 0; gi.b3d5 = 1; Check(pa::GunDir(gi).reason == pa::DirReason::sight, "gun direction: sight mapping refuses");
    gi.b3d5 = 0; gi.stance = 0; Check(pa::GunDir(gi).reason == pa::DirReason::stance, "gun direction: unread stance refuses");
    gi.stance = 1; gi.r5c = std::nanf(""); Check(pa::GunDir(gi).reason == pa::DirReason::range, "gun direction: nonfinite rate refuses");
    gi.r5c = gi.r60 = .5f; gi.body_yaw = 0; gd = pa::GunDir(gi);
    pa::DirCheck dc{}; const float body[3]{}, aim[3]{0, .6f, 100};
    pa::CheckStep(dc, gd, gi, body, aim);
    Check(pa::CheckStep(dc, gd, gi, body, aim) == pa::CheckResult::hit && dc.checks == 1, "gun direction: settled convention hit");
    gd.yaw = 1.0;
    for (unsigned n = 0; n < pa::kCheckLatch; ++n) pa::CheckStep(dc, gd, gi, body, aim);
    Check(dc.latched && dc.streak == pa::kCheckLatch && dc.misses == pa::kCheckLatch, "gun direction: sustained wrong convention latches per mission");
    // The debug command.
    pa::ParsedCommand cmd{};
    Check(pa::ParseCommand("aimpt look", cmd) && cmd.kind == pa::Command::set && cmd.value == PartnerNativeAimPt::look, "aim: aimpt look");
    Check(pa::ParseCommand("aimpt  point", cmd) && cmd.kind == pa::Command::set && cmd.value == PartnerNativeAimPt::point, "aim: aimpt point");
    Check(pa::ParseCommand("aimpt off", cmd) && cmd.value == PartnerNativeAimPt::off, "aim: aimpt off");
    Check(pa::ParseCommand("aimpt lab", cmd) && cmd.kind == pa::Command::clear, "aim: aimpt lab clears the override");
    Check(pa::ParseCommand("aimpt", cmd) && cmd.kind == pa::Command::status && pa::ParseCommand("aimpt status", cmd) && cmd.kind == pa::Command::status,
          "aim: aimpt / aimpt status");
    Check(!pa::ParseCommand("aimpt on", cmd) && pa::IsCommand("aimpt on") && !pa::IsCommand("aimptx") && !pa::IsCommand("nametag on") &&
              !pa::IsCommand(nullptr),
          "aim: refusals");
    Check(std::strcmp(pa::ReasonName(pa::Reason::q_vtable), "q_vtable") == 0 && std::strcmp(pa::ReasonName(pa::Reason::torn), "torn") == 0 &&
              std::strcmp(pa::SourceName(pa::Source::point), "point") == 0,
          "aim: names");
}

// The partner's own reload (docs/native-puppet.md "Weapons", R3/R4): the
// event machine, the freeze rule and the sender counters.
namespace pr = mgscoop::partner_reload;
pr::In ReloadReady(std::uint64_t now, std::uint8_t seq, std::uint8_t remote_mode, std::uint8_t body_mode) {
    pr::In in{};
    in.now_ms = now;
    in.setting = mgscoop::PartnerNativeReload::event;
    in.active = true;
    in.remote_valid = true;
    in.remote_seq = seq;
    in.remote_mode = remote_mode;
    in.remote_reloading = pr::ReloadMode(remote_mode);
    in.body_ok = true;
    in.body_mode = body_mode;
    in.gun_class = in.gate = in.can_reload = in.weapon_match = true;
    in.body_mag_ok = true;
    in.body_mag = 3;
    return in;
}
void TestReload() {
    pr::Machine m{};
    pr::Reset(m);
    // The first valid frame only records the counter (no start without a change).
    pr::Out o = pr::Step(m, ReloadReady(1000, 4, 11, 7));
    Check(!o.tap && !o.hold && !o.event && m.starts == 0 && m.state == pr::State::idle, "first v5 frame: no start");
    // The remote enters its reload (counter +1, mode 11): one RELOAD tap, the mirror held.
    o = pr::Step(m, ReloadReady(1020, 5, 11, 7));
    Check(o.tap && o.hold && Is(o.event, "tap") && m.starts == 1 && m.taps == 1 && m.state == pr::State::pending, "remote start: tap");
    o = pr::Step(m, ReloadReady(1100, 5, 11, 7));
    Check(!o.tap && o.hold && !o.event, "pending: waiting within verify_ms");
    // Body 1 answers with its own reload mode (12 = mode 11 + rec+0x27C bit 28).
    o = pr::Step(m, ReloadReady(1150, 5, 11, 12));
    Check(o.hold && Is(o.event, "entered") && m.entered == 1 && m.body_entries == 1 && m.state == pr::State::entered &&
              Is(o.body_event, "body_enter"), "entered");
    // Body 1's own refill (its magazine changes inside the reload): refill_ms.
    pr::In refill = ReloadReady(1500, 5, 11, 12);
    refill.body_mag = 8;
    o = pr::Step(m, refill);
    Check(o.hold && !o.event && m.refill_ms == 350, "refill_ms: body_enter -> the body's own first magazine change");
    o = pr::Step(m, ReloadReady(1800, 5, 7, 12));
    Check(o.hold && !o.event && m.remote_len_ms == 800, "entered: still reloading after the remote finished (held)");
    o = pr::Step(m, ReloadReady(2100, 5, 7, 7));
    Check(!o.hold && Is(o.event, "complete") && m.completed == 1 && m.state == pr::State::idle && Is(o.body_event, "body_exit") &&
              o.len_ms == 950 && m.len_ms == 950 && m.len_max_ms == 950 && !o.queued_tap && !o.reconcile,
          "complete: the mirror reconciles; len_ms = body_exit - body_enter");
    Check(m.remote_len_ms == 800, "remote_len_ms: the remote's mode-11/12 span (1000 .. 1800)");
    // Retries, then the timeout.
    o = pr::Step(m, ReloadReady(3000, 6, 11, 7));
    Check(o.tap && m.taps == 2, "second reload: tap");
    o = pr::Step(m, ReloadReady(3250, 6, 11, 7));
    Check(o.tap && Is(o.event, "retry") && m.taps == 3, "no reload mode within verify_ms: retry");
    o = pr::Step(m, ReloadReady(3500, 6, 11, 7));
    Check(o.tap && Is(o.event, "retry") && m.taps == 4, "retry 3");
    o = pr::Step(m, ReloadReady(3750, 6, 11, 7));
    Check(!o.tap && !o.hold && Is(o.event, "fallback") && Is(o.reason, "timeout") && m.fallbacks == 1, "max taps: timeout fallback");
    // The remote finished before body 1 entered: no late reload.
    o = pr::Step(m, ReloadReady(4000, 7, 11, 7));
    Check(o.tap, "third reload: tap");
    o = pr::Step(m, ReloadReady(4300, 7, 7, 7));
    Check(Is(o.event, "fallback") && Is(o.reason, "remote_done") && !o.hold, "remote done before the body entered");
    // Refusals before any tap.
    const auto refused = [&](std::uint64_t now, std::uint8_t seq, void (*tweak)(pr::In&), const char* why) {
        pr::In in = ReloadReady(now, seq, 11, 7);
        tweak(in);
        const pr::Out r = pr::Step(m, in);
        return !r.tap && !r.hold && Is(r.event, "fallback") && Is(r.reason, why);
    };
    Check(refused(5000, 8, [](pr::In& in) { in.active = false; }, "inactive"), "inactive");
    Check(refused(5100, 9, [](pr::In& in) { in.weapon_match = false; }, "weapon"), "weapon gate");
    Check(refused(5200, 10, [](pr::In& in) { in.gun_class = false; }, "cls"), "class");
    Check(refused(5300, 11, [](pr::In& in) { in.gate = false; }, "gate"), "reload gate");
    Check(refused(5400, 12, [](pr::In& in) { in.can_reload = false; }, "no_reload"), "can reload");
    Check(refused(5500, 13, [](pr::In& in) { in.body_ok = false; }, "body"), "body unread");
    // A raw press already started body 1's reload: entered without a tap.
    o = pr::Step(m, ReloadReady(6000, 14, 11, 11));
    Check(!o.tap && o.hold && Is(o.event, "entered"), "already reloading: entered, no tap");
    const std::uint32_t completed = m.completed;
    // A native reload of 3.02 s (instance 1's own playback rate, run 53e36944) is no stuck reload.
    o = pr::Step(m, ReloadReady(8600, 14, 7, 11));
    Check(!o.stuck && o.hold && !o.event && m.stuck == 0 && m.state == pr::State::entered, "2.6 s: still entered, held, not stuck");
    // Stuck after 8 s: a report only; entered and the hold stay (the mirror waits for the native exit).
    o = pr::Step(m, ReloadReady(14000, 14, 7, 11));
    Check(o.stuck && o.hold && Is(o.event, "stuck") && m.stuck == 1 && m.state == pr::State::entered, "stuck: reported, still held");
    o = pr::Step(m, ReloadReady(14100, 14, 7, 11));
    Check(o.stuck && o.hold && !o.event, "stuck latched while the body stays (held)");
    // An unread tick neither exits nor re-enters.
    pr::In unread = ReloadReady(14200, 14, 7, 0);
    unread.body_ok = false;
    o = pr::Step(m, unread);
    Check(o.hold && !o.body_event && m.state == pr::State::entered, "an unread tick keeps the state");
    // The native exit after stuck_ms still counts: complete_late.
    o = pr::Step(m, ReloadReady(14300, 14, 7, 7));
    Check(!o.stuck && !o.hold && Is(o.event, "complete_late") && m.completed == completed + 1 && m.completed_late == 1 &&
              o.len_ms == 8300 && m.len_max_ms == 8300 && m.state == pr::State::idle && Is(o.body_event, "body_exit"),
          "the late native exit counts (complete_late) and releases");
    // A queued reload: the remote starts its next reload while body 1 still reloads; at body 1's exit the remote is
    // still reloading: one tap, the reconcile instead of the hold (body 1's magazine is full after its completion).
    o = pr::Step(m, ReloadReady(15000, 20, 11, 7));
    Check(o.tap && Is(o.event, "tap"), "queued: the first reload taps");
    o = pr::Step(m, ReloadReady(15100, 20, 11, 11));
    Check(Is(o.event, "entered") && o.hold, "queued: entered");
    o = pr::Step(m, ReloadReady(16000, 20, 7, 11));
    o = pr::Step(m, ReloadReady(16500, 21, 11, 11));
    Check(o.hold && !o.tap && m.queued && m.starts >= 1, "queued: a remote start while entered waits for the exit");
    const std::uint32_t taps_before = m.taps;
    o = pr::Step(m, ReloadReady(18100, 21, 11, 7));
    Check(Is(o.event, "complete") && o.queued_tap && o.tap && !o.hold && o.reconcile && m.state == pr::State::pending &&
              m.taps == taps_before + 1 && m.queued_taps == 1, "queued: complete, then one tap with the reconcile");
    o = pr::Step(m, ReloadReady(18200, 21, 11, 7));
    Check(!o.hold && o.reconcile && !o.tap, "queued: pending with the reconcile");
    o = pr::Step(m, ReloadReady(18300, 21, 11, 12));
    Check(Is(o.event, "entered") && o.hold && !o.reconcile && !m.queued_reconcile, "queued: body 1 enters its next reload");
    o = pr::Step(m, ReloadReady(21300, 21, 7, 7));
    Check(Is(o.event, "complete") && !o.queued_tap && !o.hold, "queued: its exit completes");
    // A start while entered whose remote reload is over at the exit: no tap.
    o = pr::Step(m, ReloadReady(22000, 22, 11, 7));
    o = pr::Step(m, ReloadReady(22100, 22, 11, 11));
    o = pr::Step(m, ReloadReady(22500, 23, 11, 11));
    o = pr::Step(m, ReloadReady(23000, 23, 7, 11));
    o = pr::Step(m, ReloadReady(25100, 23, 7, 7));
    Check(Is(o.event, "complete") && !o.queued_tap && !o.tap && m.state == pr::State::idle, "queued: the remote finished, no tap");
    // raw/mirror never tap; the counter still counts starts.
    pr::In raw = ReloadReady(29000, 15, 11, 7);
    raw.setting = mgscoop::PartnerNativeReload::raw;
    o = pr::Step(m, raw);
    Check(!o.tap && !o.hold && Is(o.event, "start") && m.starts >= 2, "raw: start counted, no tap");
    // A test tap (rtap=1) without a remote reload.
    pr::In forced = ReloadReady(29500, 15, 7, 7);
    forced.force_tap = true;
    o = pr::Step(m, forced);
    Check(o.tap && o.hold, "force_tap: one tap");
    // Invalid v5 resets the counter memory: a reappearing counter is no start.
    pr::Machine n{};
    pr::Reset(n);
    pr::Step(n, ReloadReady(100, 1, 7, 7));
    pr::In gone = ReloadReady(200, 1, 7, 7);
    gone.remote_valid = false;
    pr::Step(n, gone);
    Check(!pr::Step(n, ReloadReady(300, 9, 11, 7)).tap && n.starts == 0, "after an invalid frame the counter is only recorded");
    // A lost exit never freezes forever: entered, then body 1 unread for good (its last mode stays 11). The
    // stuck report at 8 s keeps the hold; release_ms (20 s) leaves entered without a completion and the hook
    // stops waiting for body 1's mode (Out::release) until a readable tick shows it left the reload mode.
    using mgscoop::PartnerNativeReload;
    {
        pr::Machine r{};
        pr::Reset(r);
        pr::Step(r, ReloadReady(1000, 1, 7, 7));
        o = pr::Step(r, ReloadReady(2000, 2, 11, 7));
        Check(o.tap && r.state == pr::State::pending, "lost exit: tap");
        o = pr::Step(r, ReloadReady(2100, 2, 11, 11));
        Check(Is(o.event, "entered") && o.hold && !o.release, "lost exit: entered");
        pr::In lost = ReloadReady(2200, 2, 7, 0);
        lost.body_ok = false;
        o = pr::Step(r, lost);
        Check(o.hold && !o.release && r.state == pr::State::entered, "lost exit: unread, still held");
        lost.now_ms = 10200;
        o = pr::Step(r, lost);
        Check(o.hold && o.stuck && Is(o.event, "stuck") && !o.release && r.state == pr::State::entered, "lost exit: stuck at 8 s, still held");
        lost.now_ms = 22099;
        o = pr::Step(r, lost);
        Check(o.hold && !o.release, "lost exit: held until release_ms");
        lost.now_ms = 22100;
        o = pr::Step(r, lost);
        Check(!o.hold && o.release && Is(o.event, "release") && r.state == pr::State::idle && r.releases == 1 && r.completed == 0 &&
                  !pr::MirrorFrozen(PartnerNativeReload::event, false, 11, o.hold, o.reconcile, o.release),
              "lost exit: released after 20 s (no hold, the hook no longer freezes on body 1's mode, no completion)");
        lost.now_ms = 30000;
        o = pr::Step(r, lost);
        Check(!o.hold && o.release && !o.event && r.releases == 1, "released stays latched while the mode is unread");
        // A new remote reload while the released body still shows 11/12: no entered, a fallback.
        pr::In again = ReloadReady(30100, 3, 11, 11);
        o = pr::Step(r, again);
        Check(Is(o.event, "fallback") && Is(o.reason, "released") && !o.hold && o.release && r.state == pr::State::idle,
              "released body reload: a new remote reload falls back (the plain mirror)");
        // The body leaves its reload mode: the latch clears, no completion is counted for the lost one.
        o = pr::Step(r, ReloadReady(30200, 3, 7, 7));
        Check(!o.release && !o.hold && Is(o.body_event, "body_exit") && r.completed == 0 && !r.released, "the latch clears at the exit");
        // The next reload enters and completes normally.
        o = pr::Step(r, ReloadReady(31000, 4, 11, 7));
        Check(o.tap, "after a release: the next reload taps");
        o = pr::Step(r, ReloadReady(31100, 4, 11, 11));
        Check(Is(o.event, "entered") && o.hold && !o.release, "after a release: entered");
        o = pr::Step(r, ReloadReady(34100, 4, 7, 7));
        Check(Is(o.event, "complete") && r.completed == 1 && o.len_ms == 3000, "after a release: complete");
        // raw (no machine): a body wedged in 11 is released too (the hook's own body-mode freeze).
        pr::In wedged = ReloadReady(40000, 4, 7, 12);
        wedged.setting = PartnerNativeReload::raw;
        o = pr::Step(r, wedged);
        Check(Is(o.body_event, "body_enter") && !o.release, "raw: body 1 enters 12");
        wedged.now_ms = 60000;
        o = pr::Step(r, wedged);
        Check(o.release && Is(o.event, "release") && r.releases == 2 && !pr::MirrorFrozen(PartnerNativeReload::raw, false, 12, false, false, o.release),
              "raw: a body wedged in 12 past release_ms no longer freezes the mirror");
        // mirror never freezes: no release either.
        pr::Machine q{};
        pr::Reset(q);
        pr::In mir = ReloadReady(1000, 1, 7, 11);
        mir.setting = PartnerNativeReload::mirror;
        pr::Step(q, mir);
        mir.now_ms = 50000;
        o = pr::Step(q, mir);
        Check(!o.release && !o.stuck && q.releases == 0, "mirror: no stuck report, no release");
    }
    // The freeze rule: body 1's reload mode freezes until the exit or the release (the reconcile only after the exit).
    Check(!pr::MirrorFrozen(PartnerNativeReload::mirror, true, 11, true, false, false), "mirror never freezes");
    Check(pr::MirrorFrozen(PartnerNativeReload::raw, true, 7, false, false, false), "raw: the remote reloading freezes");
    Check(pr::MirrorFrozen(PartnerNativeReload::raw, false, 12, false, false, false), "raw: body 1 reloading freezes");
    Check(pr::MirrorFrozen(PartnerNativeReload::raw, false, 12, false, true, false), "raw: body 1 reloading freezes even with the reconcile");
    Check(!pr::MirrorFrozen(PartnerNativeReload::raw, false, 12, false, false, true), "raw: a released body reload no longer freezes");
    Check(pr::MirrorFrozen(PartnerNativeReload::raw, true, 12, false, false, true), "raw: the release never overrides the remote's reload");
    Check(pr::MirrorFrozen(PartnerNativeReload::event, false, 7, true, false, false), "event: the driver's hold freezes");
    Check(!pr::MirrorFrozen(PartnerNativeReload::event, false, 7, false, false, false), "event: nothing reloading, no freeze");
    Check(!pr::MirrorFrozen(PartnerNativeReload::event, true, 7, false, true, false), "event: the queued tap's reconcile despite the remote reload");
    Check(pr::Params{}.stuck_ms == 8000 && pr::Params{}.release_ms == 20000 && pr::Params{}.release_ms > pr::Params{}.stuck_ms,
          "stuck_ms 8000 (a 3.02 s native reload is never stuck), release_ms 20000");
    // Sender counters.
    pr::SenderState s{};
    pr::SenderStep(s, true, 11, 1);
    Check(s.reload_seq == 0 && s.switch_seq == 0, "the first read only records");
    pr::SenderStep(s, true, 7, 1);
    pr::SenderStep(s, true, 11, 1);
    Check(s.reload_seq == 1, "entering 11 counts");
    pr::SenderStep(s, true, 12, 1);
    Check(s.reload_seq == 1, "11 -> 12 is the same reload");
    pr::SenderStep(s, true, 7, 0);
    Check(s.switch_seq == 1, "a slot change counts");
    pr::SenderStep(s, false, 11, 2);
    pr::SenderStep(s, true, 11, 2);
    Check(s.reload_seq == 1 && s.switch_seq == 1, "an unreadable tick never counts");
    for (unsigned i = 0; i < 300; ++i) { pr::SenderStep(s, true, 7, 2); pr::SenderStep(s, true, 11, 2); }
    Check(s.reload_seq == static_cast<std::uint8_t>(1 + 300), "mod 256");
}

// The weapon switch plan and the generalized gate (W4/W5).
namespace ps = mgscoop::partner_slots;
void TestSlots() {
    Check(!ps::Decode(0xFFFF0000u).filled, "handle >> 16 == 0xFFFF: empty");
    // Body 1's never-filled slots hold 0 at runtime (the stock46 preflight's Zero(remote_handles)): empty, as is
    // any equip 0 handle (phase 0 reads both as equip 0).
    Check(!ps::Decode(0u).filled && !ps::Decode(3u << 16).filled && !ps::Decode((3u << 16) | 4u).filled, "handle 0 / equip 0: empty");
    const ps::Handle h46 = ps::Decode((3u << 16) | (46u << 5) | 4u);
    Check(h46.filled && h46.equip == 46 && h46.type == 4, "decode (idx << 16) | (46 << 5) | 4");
    ps::PlanIn in{};
    in.body_ok = true;
    in.body[0] = ps::Decode(0u);  // the runtime shape of a slot body 1 never had
    in.body[1] = h46;  // stock46 in the back slot, nothing else
    in.body[2] = ps::Decode(0u);
    in.remote_base[0] = 343; in.remote_base[1] = 46; in.remote_base[2] = 9;
    in.remote_type[0] = 1; in.remote_type[1] = 4; in.remote_type[2] = 2;
    in.remote_type_ok[0] = in.remote_type_ok[1] = in.remote_type_ok[2] = true;
    ps::Plan p = ps::MakePlan(in);
    Check(p.tier[0] == ps::Tier::empty && p.tier[1] == ps::Tier::same && p.tier[2] == ps::Tier::empty && p.ok_mask == 0x2 &&
              p.equip[1] == 46 && p.equip[0] == 0, "stock46 only: back slot same, the others empty");
    in.remote_base[1] = 47;  // another weapon of type 4 on the remote's back
    p = ps::MakePlan(in);
    Check(p.tier[1] == ps::Tier::sub && p.ok_mask == 0x2, "same type: the substitute is admitted");
    in.remote_type[1] = 3;
    p = ps::MakePlan(in);
    Check(p.tier[1] == ps::Tier::other && p.ok_mask == 0, "a different type: refused");
    in.remote_type_ok[1] = false;
    Check(ps::MakePlan(in).tier[1] == ps::Tier::unknown, "no type for the remote's weapon: unknown");
    in.remote_base[1] = 0;
    Check(ps::MakePlan(in).tier[1] == ps::Tier::unknown && ps::MakePlan(in).ok_mask == 0, "no base and no type: unknown, refused");
    // Version 2: the manifest's own type counts without a base id (the alias's base matched nothing).
    in.remote_type_ok[1] = true;
    in.remote_type[1] = 4;
    p = ps::MakePlan(in);
    Check(p.tier[1] == ps::Tier::sub && p.ok_mask == 0x2 && p.equip[1] == 46, "base 0, type 4: the type-4 body weapon is the substitute");
    // The slot class: a type-6 remote weapon in the back slot, body 1's type-4 gun there (the writer's tier C class).
    in.remote_type[1] = 6;
    Check(ps::MakePlan(in).tier[1] == ps::Tier::sub, "the same slot class (4/6/8): sub");
    in.remote_type[1] = 2;
    Check(ps::MakePlan(in).tier[1] == ps::Tier::other, "a type of another slot class: other");
    in.remote_type[1] = 0;
    Check(ps::MakePlan(in).tier[1] == ps::Tier::unknown, "type 0: unknown");
    // A hip substitute for run 53e36944's slot 0: alias 871 of type 1, no base; body 1 holds 36 (type 1).
    in.body[0] = ps::Decode((5u << 16) | (36u << 5) | 1u);
    in.remote_base[0] = 0; in.remote_type[0] = 1; in.remote_type_ok[0] = true;
    p = ps::MakePlan(in);
    Check(p.tier[0] == ps::Tier::sub && p.equip[0] == 36 && (p.ok_mask & 1u), "slot 0: 36 substitutes the remote's 871 (type 1)");
    for (unsigned t = 0; t < 32; ++t)
        Check(ps::TypeSlot(static_cast<std::uint8_t>(t)) == ((t == 1 || t == 5 || t == 7) ? 0u : (t == 4 || t == 6 || t == 8) ? 1u
                                                              : (t == 2 || t == 3) ? 2u : 0xFFu), "TypeSlot");
    in.body[0] = ps::Decode(0u);
    in.body_ok = false;
    Check(ps::MakePlan(in).ok_mask == 0, "unread handles: nothing admitted");
    Check(Is(ps::TierName(ps::Tier::sub), "sub") && Is(ps::TierName(ps::Tier::empty), "empty"), "tier names");
    // Gate: the remote's raw equip or its base must be body 1's; not mid-switch; FIRE needs a bullet type.
    ps::GateIn g{873, 46, 46, false, true, 4};
    Check(ps::Judge(g) == ps::Gate::match && ps::StripBits(ps::Judge(g)) == 0, "alias 873 of base 46: match");
    g.remote_equip = 875; g.remote_base = 9;
    Check(ps::Judge(g) == ps::Gate::mismatch && ps::StripBits(ps::Gate::mismatch) == 0x50u, "another weapon: FIRE|RELOAD stripped");
    g.remote_equip = 46; g.switching = true;
    Check(ps::Judge(g) == ps::Gate::switching && ps::StripBits(ps::Gate::switching) == 0x50u, "mid-switch: stripped");
    g.switching = false; g.body_type = 9;
    Check(ps::Judge(g) == ps::Gate::projectile && ps::StripBits(ps::Gate::projectile) == 0x10u, "unqualified type: FIRE only stripped");
    g.type_ok = false; g.body_type = 4;
    Check(ps::Judge(g) == ps::Gate::projectile, "an unread type is not qualified");
    Check(ps::BulletType(1) && ps::BulletType(2) && ps::BulletType(3) && ps::BulletType(4) && ps::BulletType(5) &&
              ps::BulletType(6) && !ps::BulletType(0) && !ps::BulletType(7) && !ps::BulletType(8) && !ps::BulletType(9),
          "the bullet allow-list: assault, handgun, SMG, sniper, shotgun, MG; never grenade launcher or missile");
    // W5: the tier C substitute of the remote's in-hand slot counts as the remote's weapon.
    ps::GateIn sub{875, 9, 2, false, true, 2, true};
    Check(ps::Judge(sub) == ps::Gate::match, "a same-type substitute (body 2 for the remote's base 9): match");
    sub.substitute = false;
    Check(ps::Judge(sub) == ps::Gate::mismatch, "without the plan's substitute: mismatch");
    sub.substitute = true; sub.body_type = 7;
    Check(ps::Judge(sub) == ps::Gate::projectile && ps::StripBits(ps::Judge(sub)) == 0x10u, "a projectile substitute: FIRE stripped");
    // Stand-ins: every remote slot and filled pattern, with deterministic
    // primary-pair then lower-slot selection among bullet weapons.
    for(unsigned r=0;r<3;++r) for(unsigned mask=0;mask<8;++mask) {
        ps::PlanIn si{};si.body_ok=true;si.standin=true;
        for(unsigned s=0;s<3;++s) si.body[s]={(mask&(1u<<s))!=0,std::uint16_t(30+s),std::uint8_t(s==1?4:s==2?2:1)};
        unsigned expected=3;
        for(unsigned s=0;s<3;++s) if(s!=r && (mask&(1u<<s)))
            if(expected==3 || (r<2 && s<2 && expected>=2)) expected=s;
        Check(ps::StandIn(si,r)==(expected<3?expected:ps::kNoSlot),"stand-in deterministic filled pattern");
        const auto sp=ps::MakePlan(si);
        Check(sp.map[r]==(expected<3?expected:ps::kNoSlot),"stand-in plan maps the remote slot");
        si.standin=false;Check(ps::MakePlan(si).map[r]==ps::kNoSlot,"stand-in off refuses missing match");
    }
    ps::PlanIn si{};si.body_ok=si.standin=true;si.body[1]={true,51,8};si.body[2]={true,9,2};
    Check(ps::StandIn(si,0)==2,"bullet secondary beats a primary projectile");
    si.body[2]={};Check(ps::StandIn(si,0)==1,"projectile fallback when no bullet exists");
    si.body_ok=false;Check(ps::StandIn(si,0)==ps::kNoSlot,"unread slots never select a stand-in");
    ps::GateIn sg{871,343,46,false,true,4,false,true};
    Check(ps::Judge(sg)==ps::Gate::standin && ps::StripBits(ps::Judge(sg))==0,"qualified stand-in permits HOLD FIRE RELOAD");
    sg.body_type=8;Check(ps::Judge(sg)==ps::Gate::projectile && ps::StripBits(ps::Judge(sg))==0x10,"projectile stand-in masks FIRE");
    sg.switching=true;Check(ps::Judge(sg)==ps::Gate::switching,"switching stand-in masks FIRE RELOAD");
    Check(Is(ps::GateName(ps::Gate::switching), "switching"), "gate names");
}

// The suspend sub-mode (docs/anim-round1-actions.md "DM overlay"): the DM
// overlay suspends the drive without a fallback, a latch or a re-entry hold.
void TestSuspend() {
    pn::State s{};
    pn::Reset(s, true);
    std::uint64_t t = Activate(s, 1000);
    for (unsigned episode = 0; episode < 6; ++episode) {
        pn::In in = Healthy(++t);
        in.suspend_reason = pn::kSuspendOverlay;
        pn::Out o = pn::Step(s, in);
        Check(LeaveRequests(o) && Is(o.reason, "overlay") && o.suspend_cycle && s.fallbacks == 0 && s.hard_leaves == 0 && s.net_leaves == 0,
              "suspend: leaving (defer, window, dm=map rearm, epoch) without a fallback");
        in.now_ms = ++t;
        o = pn::Step(s, in);
        Check(o.mode == pn::Mode::fallback && Is(o.reason, "overlay") && o.suspend_cycle && o.transition, "suspend: fallback, suspend cycle");
        for (int i = 0; i < 30; ++i) {
            in.now_ms = (t += 100);
            o = pn::Step(s, in);
        }
        Check(o.mode == pn::Mode::fallback && Is(o.reason, "overlay") && !o.transition, "suspended: blocked with the overlay reason");
        // The overlay released: re-enter at once (no cooldown, no 2 s settling); the window was disarmed: place step.
        in = Ready(++t);
        o = pn::Step(s, in);
        Check(o.mode == pn::Mode::entering && Is(o.reason, "resume") && o.suspend_cycle && o.transition && Is(o.enter_step, "place") &&
              o.req_defer_arm, "resume: entering at once (place step, deferral first)");
        in.window_armed = in.recon_placed = true;
        in.now_ms = ++t;
        o = pn::Step(s, in);
        Check(o.mode == pn::Mode::active && o.suspend_cycle && !s.resumed, "resume: active, still on the suspend cycle");
        in = Healthy(++t);
        o = pn::Step(s, in);
        Check(o.mode == pn::Mode::active && !o.suspend_cycle, "steady active: not a suspend cycle");
        t += 100;
    }
    Check(s.fallbacks == 0 && s.suspends == 6 && s.mode == pn::Mode::active, "six suspends: never latched, never counted");
    // Another blocker during the suspend restores the normal cooldown and settling hold.
    {
        pn::In in = Healthy(++t);
        in.suspend_reason = pn::kSuspendOverlay;
        pn::Step(s, in);
        in.now_ms = ++t;
        pn::Step(s, in);
        in.link_live = false;
        in.now_ms = ++t;
        pn::Out o = pn::Step(s, in);
        Check(o.mode == pn::Mode::fallback && Is(o.reason, "overlay"), "suspend reason first");
        in.suspend_reason = nullptr;
        in.now_ms = ++t;
        o = pn::Step(s, in);
        Check(o.mode == pn::Mode::fallback && Is(o.reason, "link") && !s.suspended, "another blocker: the suspend shortcut is gone");
        in = Ready(++t);
        in.window_armed = in.recon_placed = true;
        o = pn::Step(s, in);
        Check(o.mode == pn::Mode::fallback && Is(o.reason, "cooldown"), "normal cooldown again");
        bool entered = false;
        std::uint64_t at = 0;
        for (int i = 0; i < 100 && !entered; ++i) {
            in.now_ms = (t += 100);
            o = pn::Step(s, in);
            if (o.mode != pn::Mode::fallback) { entered = true; at = t; }
        }
        Check(entered && at - s.left_ms >= 5000, "re-entry after the cooldown and the settling hold");
    }
    // A suspend from entering leaves without counting either.
    {
        pn::State e{};
        pn::Reset(e, true);
        std::uint64_t now = 1000;
        pn::Out o{};
        for (; now < 10000 && o.mode != pn::Mode::entering; now += 100) o = pn::Step(e, Ready(now));
        Check(o.mode == pn::Mode::entering, "entering");
        pn::In in = Ready(now);
        in.suspend_reason = pn::kSuspendOverlay;
        o = pn::Step(e, in);
        Check(LeaveRequests(o) && e.fallbacks == 0 && e.suspended, "suspend while entering: no fallback counted");
    }
    // A suspend while already in fallback (the drive was not active) keeps the normal hold.
    {
        pn::State f{};
        pn::Reset(f, true);
        pn::In in = Ready(1000);
        in.suspend_reason = pn::kSuspendOverlay;
        pn::Out o = pn::Step(f, in);
        Check(o.mode == pn::Mode::fallback && Is(o.reason, "overlay") && !f.suspended && !o.suspend_cycle, "fallback blocked by the overlay");
        in.suspend_reason = nullptr;
        in.now_ms = 1100;
        o = pn::Step(f, in);
        Check(o.mode == pn::Mode::fallback && Is(o.reason, "settling"), "not suspended by us: the normal settling hold");
    }
}

int main() {
    TestSuspend();
    for (const char* visibilityWhy : {"ride", "ride_stale", "air", "dead"}) {
        pn::State visibilityState{}; pn::Reset(visibilityState, true);
        for (unsigned episode=0; episode<6; ++episode) {
            const auto at=Activate(visibilityState,episode*10000);
            auto input=Healthy(at+1); input.visibility_reason=visibilityWhy;
            auto output=pn::Step(visibilityState,input);
            Check(LeaveRequests(output) && Is(output.reason,visibilityWhy) && visibilityState.fallbacks==0,
                  "visibility leave never spends hard fallback latch, including lab policy");
            input.now_ms+=1; output=pn::Step(visibilityState,input);
            Check(output.mode==pn::Mode::fallback, "visibility leave reaches recoverable fallback");
            input.now_ms+=5000; output=pn::Step(visibilityState,input);
            Check(output.mode==pn::Mode::fallback && Is(output.reason,visibilityWhy), "visibility blocks native re-entry");
        }
        const auto code=mgscoop::partner_native_net::ReasonCode(visibilityWhy);
        Check(code!=mgscoop::partner_native_net::kReasonUnknown &&
              Is(mgscoop::partner_native_net::ReasonName(code),visibilityWhy), "appended stable visibility reason round trip");
    }
    TestOnline();
    TestAim();
    TestWeapons();
    TestMagazineHistory();
    TestReload();
    TestSlots();
    TestOffAndLegacy();
    TestEnterSequence();
    TestEnterTimeoutsAndRevertLatch();
    TestThreadWarmup();
    TestHardReasons();
    TestSoftReasons();
    TestCooldownAndLatch();
    TestLeftReason();
    TestFallbackReasons();
    TestEffective();
    TestLatch();
    TestSteer();
    TestMovementRecovery();
    TestConsumers();
    TestRing();
    TestRates();
    std::printf("%u partner native checks passed: arbiter (off, settling hold, entering steps dm_stop/dm_revert/place/armed, "
                "timeouts, DM revert latch, hard and soft reasons, health warm-up and windows, cost, cooldown, 3-fallback latch, "
                "unlatch, MissionStart reset, fallback reasons, publication order defer before Arm, left_reason through "
                "cooldown, phase-1 detour precondition), effective settings and mask, HOLD latch, "
                "steering and trim, per-consumer frame injection, telemetry ring, rates mirror (apply, reapply, restore, "
                "refusals), weapons (capacity, magazine, CanReload, reload gate, magazine mirror), online play (network leaves "
                "without latch, quality-gated re-entry, quiet remote, flap guard, remote_caps, hold=net, link quality, reason "
                "codes, native.net JSON, capability message), aim parity, reload and switch (event machine, freeze rule, sender "
                "counters, slot plan, weapon gate)\n", checks);
    return 0;
}
