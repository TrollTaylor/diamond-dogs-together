// Original MGS Co-op native puppet: pure arbiter, effective settings,
// steering, HOLD latch rule and mask (docs/native-puppet.md).
#include "mgscoop/partner_native.hpp"
#include <cmath>
#include <cstring>

namespace mgscoop::partner_native {
namespace {
constexpr double kPi = 3.14159265358979323846;

const char* const kStepNames[] = {nullptr, "dm_stop", "dm_revert", "place", "armed"};

// The static "install_<why>" reason for a lazy-install refusal.
const char* InstallReason(const char* why) noexcept {
    static const char* const kNames[][2] = {
        {"bytes", "install_bytes"}, {"slot", "install_slot"}, {"changed", "install_changed"}, {"protect", "install_protect"},
        {"allocate_near", "install_allocate_near"}, {"range", "install_range"}, {"alignment", "install_alignment"},
        {"cache_line", "install_cache_line"}, {"hook", "install_hook"}, {"hook_disabled", "install_hook_disabled"},
        {"scope", "install_scope"}, {"exchange", "install_exchange"}, {"unavailable", "install_unavailable"},
        {"disabled", "install_disabled"}, {"vt_copy", "install_vt_copy"}, {"wrap", "install_wrap"}, {"gate", "install_gate"}};
    for (const auto& n : kNames)
        if (std::strcmp(why, n[0]) == 0) return n[1];
    return "install_other";
}

// Every fallback -> entering precondition; the first failing one names the reason.
const char* EnterBlocker(const State& s, const In& in, const Params& p) noexcept {
    if (s.dm_latch) return "dm_revert_failed";
    if (!in.defer_on) return "defer_census";
    if (in.install_refusal) return InstallReason(in.install_refusal);
    if (!in.installed_h0 || !in.installed_wrap || !in.installed_h2 || !in.installed_h1) return "not_installed";
    if (!in.live_h0 || !in.live_h1 || !in.live_h2) return "hook_lost";
    if (!in.thread_ok) return "thread";
    if (in.faulted) return "faulted";
    if (in.dm_revert_failed) return "dm_revert_failed";
    if (in.terminating) return "terminating";
    if (in.stage < 3) return "stage";
    if (in.range_hidden) return "range_hidden";
    if (!in.realized) return "unrealized";
    if (!in.parts_loaded) return "parts";
    if (!in.link_live) return "link";
    if (!in.input_valid) return "no_input";
    if (in.input_age_ms > p.input_age_ms) return "input_stale";
    return nullptr;
}

// Reasons that leave entering/active immediately.
const char* HardLeave(const In& in) noexcept {
    if (!in.configured) return "configured";
    if (!in.defer_on) return "defer_off";
    if (in.terminating) return "terminating";
    if (in.faulted) return "faulted";
    if (!in.live_h0 || !in.live_h1 || !in.live_h2) return "hook_lost";
    if (!in.thread_ok) return "thread";
    if (!in.realized) return "unrealized";
    if (in.range_hidden) return "range_hidden";
    if (in.stage < 3) return "stage";
    if (in.lost) return "lost";
    if (in.wrap_in_batch) return "wrap_in_batch";
    return nullptr;
}

void ResetWindows(State& s, std::uint64_t now) noexcept {
    s.window_start_ms = now;
    s.win_w50 = s.win_deferred = s.win_drains = s.win_windowed = s.win_us_max = 0;
    s.windows_done = s.health_bad = s.cost_bad = 0;
    s.stale = s.link_down = false;
}

double Clamp(double v, double lo, double hi) noexcept {
    if (!(v >= lo)) return lo;
    if (v > hi) return hi;
    return v;
}
} // namespace

const char* ModeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::fallback: return "fallback";
    case Mode::entering: return "entering";
    case Mode::active: return "active";
    case Mode::leaving: return "leaving";
    case Mode::latched: return "latched";
    default: return "off";
    }
}

void Reset(State& s, bool configured) noexcept {
    s = State{};
    s.mode = configured ? Mode::fallback : Mode::off;
    s.reason = configured ? "mission_start" : "legacy";
}

void Unlatch(State& s, std::uint64_t now_ms) noexcept {
    s.dm_latch = false;
    s.fallbacks = 0;
    if (s.mode == Mode::latched) {
        s.mode = Mode::fallback;
        s.reason = "unlatched";
        s.since_ms = now_ms;
        s.ready = false;
    }
}

Out Step(State& s, const In& in, const Params& p) noexcept {
    Out o{};
    const Mode before = s.mode;
    const std::uint64_t now = in.now_ms;
    const auto go = [&](Mode mode, const char* why) noexcept {
        s.mode = mode;
        s.reason = why;
        s.since_ms = now;
    };
    const auto leave = [&](const char* why) noexcept {
        go(Mode::leaving, why);
        o.req_defer_disarm = o.req_disarm = o.req_dmmap_rearm = o.req_epoch = true;
        s.left = true;
        s.left_ms = now;
        ++s.fallbacks;
        s.ready = false;
        s.enter_step = 0;
    };
    switch (s.mode) {
    case Mode::off:
        if (!in.configured) {
            s.reason = "legacy";
            break;
        }
        go(Mode::fallback, "configured");
        [[fallthrough]];
    case Mode::fallback: {
        if (!in.configured) {
            go(Mode::off, "legacy");
            break;
        }
        const char* blocker = EnterBlocker(s, in, p);
        if (!blocker && s.left && now - s.left_ms < p.cooldown_ms) blocker = "cooldown";
        if (blocker) {
            s.ready = false;
            s.reason = blocker;
            break;
        }
        if (!s.ready) {
            s.ready = true;
            s.ready_since_ms = now;
        }
        if (now - s.ready_since_ms < p.enter_hold_ms) {
            s.reason = "settling";
            break;
        }
        go(Mode::entering, "ok");
        s.enter_step = 0;
        s.step_since_ms = now;
        [[fallthrough]];
    }
    case Mode::entering: {
        if (const char* why = HardLeave(in)) {
            leave(why);
            break;
        }
        if (in.dm_revert_failed) {
            s.dm_latch = true;
            leave("dm_revert_failed");
            break;
        }
        const std::uint8_t step = !in.dm_idle ? 1 : in.dm_patched ? 2 : (in.window_armed && in.recon_placed) ? 4 : 3;
        if (step != s.enter_step) {
            s.enter_step = step;
            s.step_since_ms = now;
            o.enter_step = kStepNames[step];
        }
        if (step == 4) {
            go(Mode::active, "ok");
            o.req_defer_arm = true;
            ResetWindows(s, now);
            break;
        }
        if (step <= 2 && now - s.since_ms >= p.dm_timeout_ms) leave("enter_timeout_dm");
        else if (step == 3 && now - s.step_since_ms >= p.place_timeout_ms) leave("enter_timeout_place");
        break;
    }
    case Mode::active: {
        if (const char* why = HardLeave(in)) { leave(why); break; }
        if (in.snaps_10s >= p.diverging_snaps) { leave("diverging"); break; }
        if (in.runaway_ms > p.runaway_ms) { leave("runaway"); break; }
        const bool stale = !in.input_valid || in.input_age_ms > p.input_age_ms;
        if (stale) {
            if (!s.stale) { s.stale = true; s.stale_since_ms = now; }
            if (now - s.stale_since_ms >= p.stale_hold_ms) { leave("input_stale"); break; }
        } else {
            s.stale = false;
        }
        if (!in.link_live) {
            if (!s.link_down) { s.link_down = true; s.link_since_ms = now; }
            if (now - s.link_since_ms >= p.link_hold_ms) { leave("link"); break; }
        } else {
            s.link_down = false;
        }
        s.win_w50 += in.w50_b1_s0;
        s.win_deferred += in.deferred;
        s.win_drains += in.drains;
        s.win_windowed += in.windowed;
        if (in.drain_us_max > s.win_us_max) s.win_us_max = in.drain_us_max;
        if (now - s.window_start_ms >= p.health_window_ms) {
            ++s.windows_done;
            // The first window after activation is a warm-up (arming raced it).
            if (s.windows_done > 1) {
                const bool bad = (s.win_w50 && static_cast<std::uint64_t>(s.win_deferred) * 100u <
                                                   static_cast<std::uint64_t>(p.health_pct) * s.win_w50) ||
                                 (s.win_drains && static_cast<std::uint64_t>(s.win_windowed) * 100u <
                                                      static_cast<std::uint64_t>(p.health_pct) * s.win_drains);
                s.health_bad = bad ? s.health_bad + 1 : 0;
            }
            s.cost_bad = s.win_us_max > p.cost_us ? s.cost_bad + 1 : 0;
            s.window_start_ms = now;
            s.win_w50 = s.win_deferred = s.win_drains = s.win_windowed = s.win_us_max = 0;
            if (s.health_bad >= p.health_windows) { leave("health"); break; }
            if (s.cost_bad >= p.cost_windows) { leave("cost"); break; }
        }
        break;
    }
    case Mode::leaving:
        if (s.dm_latch) go(Mode::latched, "dm_revert_failed");
        else if (s.fallbacks >= p.latch_fallbacks) go(Mode::latched, s.reason);
        else if (!in.configured) go(Mode::off, "legacy");
        else go(Mode::fallback, s.reason);
        s.ready = false;
        break;
    case Mode::latched:
        if (!in.configured) go(Mode::off, "legacy");
        break;
    }
    o.mode = s.mode;
    o.reason = s.reason;
    o.active = s.mode == Mode::entering || s.mode == Mode::active;
    o.transition = s.mode != before;
    return o;
}

std::uint32_t NativeMask(std::uint32_t configured, bool gun, bool fire) noexcept {
    return configured & PartnerNativeAllowed(gun, fire);
}

DriveSettings Effective(const DriveSettings& base, bool active, const NativeConfig& config) noexcept {
    if (!active) return base;
    DriveSettings e = base;
    e.apply = PartnerApply::input;
    e.input = PartnerInput::net;
    e.dm = PartnerDm::off;
    e.anim = PartnerAnim::off;
    e.wrap = PartnerWrap::window;
    e.hold = PartnerInputHold::own;
    e.words[0] = kPartnerWordHeld;
    e.words[1] = kPartnerWordPress;
    e.words[2] = kPartnerWordRelease;
    e.cam = PartnerInputCam::sandbox;
    e.recon = PartnerRecon::track;
    e.slot = PartnerSlot::body;
    e.p218 = false;
    e.mask = NativeMask(base.mask, config.gun, config.fire);
    return e;
}

std::uint32_t Latch27c(std::uint32_t old, std::uint32_t w4c, std::uint32_t mask28) noexcept {
    return (old & ~1u) | (((w4c & ~mask28) >> 3) & 1u);
}

SteerOut Steer(SteerState& st, const SteerIn& in, const SteerParams& p) noexcept {
    SteerOut o{};
    o.mag_scale = 1.0;
    const double vx = std::isfinite(in.v_x) ? in.v_x : 0.0, vz = std::isfinite(in.v_z) ? in.v_z : 0.0;
    const double ex = std::isfinite(in.e_x) ? in.e_x : 0.0, ez = std::isfinite(in.e_z) ? in.e_z : 0.0;
    const double speed = std::sqrt(vx * vx + vz * vz);
    if (speed > 1e-6) {
        const double ux = vx / speed, uz = vz / speed;
        o.along_mm = ex * ux + ez * uz;
        // left = (u.z, -u.x): +yaw (and +x when facing +z) is left in the Fox frame.
        o.cross_mm = ex * uz - ez * ux;
    }
    const double max_rad = Clamp(p.max_deg, 0.0, static_cast<double>(kPartnerNativeSteerMaxDeg)) * kPi / 180.0;
    const double in_r = std::isfinite(in.in_r) ? in.in_r : 0.0;
    double target = 0.0;
    if (max_rad > 0.0 && speed >= p.min_speed_mmps && in_r > p.min_in_r && !in.frozen && !in.prone_transition &&
        std::fabs(o.cross_mm) > p.cross_dead_mm) {
        o.active = true;
        const double look = speed * 0.5 > p.lookahead_min_mm ? speed * 0.5 : p.lookahead_min_mm;
        target = Clamp(std::atan2(o.cross_mm, look), -max_rad, max_rad);
    }
    double dt = 0.0;
    if (st.valid && in.now_ms >= st.last_ms) dt = Clamp(static_cast<double>(in.now_ms - st.last_ms) / 1000.0, 0.0, 0.25);
    const double rate = (o.active ? p.rate_dps : p.return_dps) * kPi / 180.0 * dt;
    st.bias_rad += Clamp(target - st.bias_rad, -rate, rate);
    st.bias_rad = Clamp(st.bias_rad, -max_rad, max_rad);
    st.last_ms = in.now_ms;
    st.valid = true;
    o.bias_rad = st.bias_rad;
    const double trim = Clamp(p.trim_pct, 0.0, static_cast<double>(kPartnerNativeTrimMaxPct)) / 100.0;
    if (trim > 0.0 && in_r >= 0.95 && !in.walk_key && speed >= p.min_speed_mmps && o.along_mm < -300.0) {
        double scale = 1.0 + o.along_mm / 5000.0;
        if (scale < 1.0 - trim) scale = 1.0 - trim;
        const double floor = kTrimFloor / in_r;
        if (scale < floor) scale = floor;
        o.mag_scale = Clamp(scale, 0.0, 1.0);
    }
    return o;
}

} // namespace mgscoop::partner_native
