// Original MGS Co-op native puppet: pure arbiter, effective settings,
// steering, HOLD latch rule and mask (docs/native-puppet.md).
#include "mgscoop/partner_native.hpp"
#include <cmath>
#include <algorithm>
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
    if (in.visibility_reason) return in.visibility_reason;
    if (in.suspend_reason) return in.suspend_reason;
    if (!in.defer_on) return "defer_census";
    if (in.install_refusal) return InstallReason(in.install_refusal);
    if (!in.installed_h0 || !in.installed_wrap || !in.installed_h2 || !in.installed_h1 || !in.installed_p1) return "not_installed";
    if (!in.live_h0 || !in.live_h1 || !in.live_h2 || !in.live_p1) return "hook_lost";
    if (!in.thread_ok) return in.thread_warmup ? "warmup" : "thread";
    if (in.faulted) return "faulted";
    if (in.dm_revert_failed) return "dm_revert_failed";
    if (in.terminating) return "terminating";
    if (in.stage < 3) return "stage";
    if (in.range_hidden) return "range_hidden";
    if (!in.realized) return "unrealized";
    if (!in.parts_loaded) return "parts";
    if (!in.link_live) return "link";
    // Online: a paused or loading remote is quiet, not missing; a remote that
    // sends no input frames at all says so in its capabilities.
    if (p.online && in.remote_caps_refused) return "remote_caps";
    if (!in.input_valid) return p.online && in.remote_quiet ? "remote_quiet" : "no_input";
    if (in.input_age_ms > p.input_age_ms) return p.online && in.remote_quiet ? "remote_quiet" : "input_stale";
    return nullptr;
}

// Reasons that leave entering/active immediately.
const char* HardLeave(const In& in) noexcept {
    if (!in.configured) return "configured";
    if (!in.defer_on) return "defer_off";
    if (in.terminating) return "terminating";
    if (in.visibility_reason) return in.visibility_reason;
    if (in.suspend_reason) return in.suspend_reason;
    if (in.faulted) return "faulted";
    if (!in.live_h0 || !in.live_h1 || !in.live_h2 || !in.live_p1) return "hook_lost";
    if (!in.thread_ok) return "thread";
    if (!in.realized) return "unrealized";
    if (in.range_hidden) return "range_hidden";
    if (in.stage < 3) return "stage";
    if (in.lost) return "lost";
    if (in.wrap_in_batch) return "wrap_in_batch";
    if (in.drain_block) return "drain_block";
    return nullptr;
}
// Active only: block 1's job ran without a window under a disabled Arm (it
// read the viewer's pad). Entering may see the arming frame's race.
const char* ActiveLeave(const In& in) noexcept {
    if (in.drain_disarmed) return "unwindowed";
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

bool SameReason(const char* a, const char* b) noexcept { return a && b && (a == b || std::strcmp(a, b) == 0); }

// The online flap guard: network leaves within the window.
std::uint32_t RecentNetLeaves(const State& s, std::uint64_t now, std::uint32_t window_ms) noexcept {
    std::uint32_t n = 0;
    for (std::uint32_t i = 0; i < s.net_count && i < 8; ++i)
        if (now >= s.net_times[i] && now - s.net_times[i] <= window_ms) ++n;
    return n;
}
} // namespace

Params OnlineParams() noexcept {
    Params p{};
    p.online = true;
    p.stale_hold_ms = 1500;
    p.link_hold_ms = 1500;
    return p;
}

bool NetReason(const char* reason) noexcept {
    if (!reason) return false;
    // seat: the seated vehicle rider owns body 1 (partner_native_ride=seat), a visibility leave like ride.
    static const char* const kNet[] = {"net_input", "net_link", "net_quiet", "range_hidden", "unrealized", "ride", "ride_stale", "air", "dead",
                                       "seat"};
    for (const char* n : kNet)
        if (std::strcmp(reason, n) == 0) return true;
    return false;
}

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
        s.left_reason = why;
        s.quiet = false;
        s.ready = false;
        s.enter_step = 0;
        s.resumed = false;
        // A suspend (In::suspend_reason) counts nothing: no fallback, no latch,
        // no network leave; fallback re-enters as soon as it clears.
        if (SameReason(why, in.suspend_reason)) {
            s.suspended = true;
            ++s.suspends;
            s.left_net = false;
            o.suspend_cycle = true;
            return;
        }
        s.suspended = false;
        // Online: a network reason never counts toward the mission latch.
        s.left_net = (p.online || in.visibility_reason != nullptr) && NetReason(why);
        if (s.left_net) {
            ++s.net_leaves;
            s.last_net_reason = why;
            // The flap guard keeps the 8 newest network leaves.
            if (s.net_count < 8) s.net_times[s.net_count++] = now;
            else {
                for (unsigned i = 1; i < 8; ++i) s.net_times[i - 1] = s.net_times[i];
                s.net_times[7] = now;
            }
            if (RecentNetLeaves(s, now, p.flap_window_ms) > p.flap_leaves) s.flap = true;
        } else {
            ++s.fallbacks;
            ++s.hard_leaves;
        }
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
        // Suspended: only the suspend itself keeps the immediate re-entry; any
        // other blocker meanwhile restores the normal cooldown and hold.
        if (s.suspended && blocker && !SameReason(blocker, in.suspend_reason)) s.suspended = false;
        if (s.suspended && blocker) o.suspend_cycle = true;
        const std::uint32_t cooldown = p.online && s.left_net ? p.net_cooldown_ms : p.cooldown_ms;
        if (!blocker && !s.suspended && s.left && now - s.left_ms < cooldown) blocker = "cooldown";
        // Online: after a network leave the link quality must hold first; a
        // flapping link keeps the clips until it held much longer.
        if (!blocker && !s.suspended && p.online && s.flap) {
            if (in.quality_good_ms >= p.flap_good_ms) {
                s.flap = false;
                s.net_count = 0;
            } else {
                blocker = "net_flap";
            }
        }
        if (!blocker && p.online && s.left_net && in.quality_good_ms < p.quality_ms) blocker = "net_quality";
        if (blocker) {
            s.ready = false;
            s.reason = blocker;
            break;
        }
        if (s.suspended) {
            // The suspend cleared: re-enter at once (no cooldown, no settling hold).
            s.suspended = false;
            s.resumed = true;
            go(Mode::entering, "resume");
        } else {
            if (!s.ready) {
                s.ready = true;
                s.ready_since_ms = now;
            }
            if (now - s.ready_since_ms < p.enter_hold_ms) {
                s.reason = "settling";
                break;
            }
            go(Mode::entering, "ok");
            if (s.left_net) ++s.net_reentries;
        }
        s.enter_step = 0;
        s.step_since_ms = now;
        [[fallthrough]];
    }
    case Mode::entering: {
        if (s.resumed) o.suspend_cycle = true;
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
            s.resumed = false;
            break;
        }
        if (step <= 2 && now - s.since_ms >= p.dm_timeout_ms) leave("enter_timeout_dm");
        else if (step == 3 && now - s.step_since_ms >= p.place_timeout_ms) leave("enter_timeout_place");
        // The deferral before the native Arm: the driver publishes the Arm only
        // once the deferral is armed, so block 1's job never runs inline in a
        // batch under a native Arm (the wrap_in_batch race of the arming frame).
        else if (step == 3) o.req_defer_arm = true;
        break;
    }
    case Mode::active: {
        if (const char* why = HardLeave(in)) { leave(why); break; }
        if (const char* why = ActiveLeave(in)) { leave(why); break; }
        if (in.snaps_10s >= p.diverging_snaps) { leave("diverging"); break; }
        if (in.runaway_ms > p.runaway_ms) { leave("runaway"); break; }
        const bool stale = !in.input_valid || in.input_age_ms > p.input_age_ms;
        // Online: a quiet remote (paused, loading) holds the body neutral in
        // place (the window injects a neutral pad) for up to quiet_hold_ms.
        if (p.online && stale && in.remote_quiet && in.link_live) {
            if (!s.quiet) {
                s.quiet = true;
                s.quiet_since_ms = now;
                ++s.quiet_episodes;
            }
            s.reason = "remote_quiet";
            s.stale = s.link_down = false;
            if (now - s.quiet_since_ms >= p.quiet_hold_ms) { leave("net_quiet"); break; }
        } else {
            if (s.quiet) {
                s.quiet = false;
                s.reason = "ok";
            }
            if (stale) {
                if (!s.stale) { s.stale = true; s.stale_since_ms = now; }
                if (now - s.stale_since_ms >= p.stale_hold_ms) { leave(p.online ? "net_input" : "input_stale"); break; }
            } else {
                s.stale = false;
            }
            if (!in.link_live) {
                if (!s.link_down) { s.link_down = true; s.link_since_ms = now; }
                if (now - s.link_since_ms >= p.link_hold_ms) { leave(p.online ? "net_link" : "link"); break; }
            } else {
                s.link_down = false;
            }
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
        if (s.suspended) o.suspend_cycle = true;
        // A suspend never latches (whatever the mission's earlier fallbacks).
        if (s.suspended && in.configured && !s.dm_latch) go(Mode::fallback, s.reason);
        else if (s.dm_latch) go(Mode::latched, "dm_revert_failed");
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
    e.hold = config.hold_net ? PartnerInputHold::net : PartnerInputHold::own;
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

std::uint32_t GunCapacity(const GunFields& g) noexcept {
    std::uint32_t cap = (g.flags27c & 0x200u) ? g.cap_sub : g.cap;
    if (g.equip - 0x42Bu <= 3u && (g.flags27c & 0x100000u)) cap += cap;
    if (g.flags27c & 0x80000u) cap >>= 1;
    return cap;
}

std::uint32_t GunMagazine(const GunFields& g) noexcept {
    if (g.gun3c0 & 0x10000000u) return GunCapacity(g);
    return (g.flags27c & 0x200u) ? g.mag_sub : g.mag;
}

bool GunCanReload(const GunFields& g) noexcept {
    if (g.flags280 & 0x80000u) return false;
    const std::uint32_t cap = GunCapacity(g);
    const std::uint32_t mag = GunMagazine(g);
    std::uint32_t spare = 0;
    if (g.gun3c0 & 0x20000000u) {
        // 0x104F1C0: capacity + (flags27c >> 31) - (rec+0x23A bit 5 with flags bit 9, else bit 4).
        spare = cap + (g.flags27c >> 31) - (((g.flags27c & 0x200u) ? (g.w23a >> 5) : (g.w23a >> 4)) & 1u);
    } else {
        spare = static_cast<std::uint32_t>((g.flags27c & 0x200u) ? g.stock_sub : g.stock) - mag;
    }
    return spare != 0 && mag < cap;
}

bool GunReloadGate(const GunFields& g, std::uint8_t b3d5) noexcept {
    return GunClass(g.flags27c) == kGunClassGun && (g.flags27c & 0x1800u) != 0x800u && g.equip != 0 && b3d5 != 4;
}

std::uint32_t MirrorMagazine(const MagIn& in) noexcept {
    if (!in.cap || !in.remote_cap) return 0;
    std::uint32_t out = 0;
    if (in.remote_mag >= in.remote_cap) {
        out = in.cap;
    } else if (in.remote_mag > 0) {
        const std::uint64_t scaled = (static_cast<std::uint64_t>(in.remote_mag) * in.cap + in.remote_cap - 1) / in.remote_cap;
        out = static_cast<std::uint32_t>(scaled);
        if (out >= in.cap) out = in.cap - 1;  // not full stays reloadable (0 for a capacity of 1)
        if (out == 0 && in.cap > 1) out = 1;
    }
    // The remote fired a round: show it (never on a dry fire of an empty remote).
    if (in.fire_edge && out == 0 && (in.remote_mag > 0 || in.remote_was_loaded)) out = 1;
    return out;
}

bool DashRelatch(DashState& st, const DashIn& in) noexcept {
    const auto now = in.now_ms;
    if (st.last_ms && now < st.last_ms) { st = DashState{}; return false; }
    const bool eligible = in.active && in.input_live && in.mask_dash && !in.hold_stripped &&
        in.body_ok && in.remote_ok && in.body_act >= 1 && in.body_act <= 6 &&
        in.remote_act >= 1 && in.remote_act <= 6 && in.body_stance == 1 && in.remote_stance == 1 &&
        in.body_speed != 3 && (in.remote_speed == 3 || in.remote_sprinting) &&
        std::isfinite(in.in_r) && in.in_r >= 0.6;
    if (!eligible) { st.since_ms = 0; return false; }
    if (!st.since_ms) st.since_ms = now ? now : 1;
    if (!st.window_ms || (now >= st.window_ms && now - st.window_ms >= 10000)) {
        st.window_ms = now ? now : 1;
        st.attempts = 0;
    }
    if (now < st.since_ms || now - st.since_ms < 300 ||
        (st.last_ms && now - st.last_ms < 1500) || st.attempts >= 4) return false;
    st.last_ms = now;
    ++st.attempts;
    return true;
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
    if (!st.valid) st.hold_scale = 1.0;
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
    // HOLD stripped by the existing gate means the body uses free locomotion
    // where the remote has aim-limited speed. Admit only measured along-track
    // overrun, change by 10 scale points/100 ms, recover by 20/100 ms, and
    // keep the injected magnitude above the proven native start threshold.
    const bool parity = in.hold_stripped && (!in.frozen || in.hold_parity_ready) && !in.prone_transition &&
        !in.walk_key && in_r >= 0.6 && speed >= p.min_speed_mmps;
    if (parity) {
        const double floor = Clamp(kTrimFloor / in_r, 0.0, 1.0);
        const bool ahead = std::isfinite(in.body_speed_mmps) &&
            in.body_speed_mmps > 1.25 * speed && o.along_mm < -300.0;
        st.hold_scale = Clamp(st.hold_scale + (ahead ? -dt : 2.0 * dt), floor, 1.0);
        o.mag_scale = std::min(o.mag_scale, st.hold_scale);
    } else {
        st.hold_scale = 1.0;
    }
    return o;
}

} // namespace mgscoop::partner_native
