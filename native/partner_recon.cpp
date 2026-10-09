// Original MGS Co-op round 4: reconciliation of the input-driven partner body
// (pure, portable, deterministic). docs/partner-anim-appearance.md "Round 4".
#include "mgscoop/partner_recon.hpp"
#include <cmath>

namespace mgscoop::partner_recon {
namespace {
constexpr double kPi = 3.14159265358979323846;

double Len2(double x, double z) noexcept { return std::sqrt(x * x + z * z); }
double Len3(const double (&a)[3], const double (&b)[3]) noexcept {
    const double dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}
bool Loco(std::uint16_t act) noexcept { return act >= 1 && act <= 6; }
// Facing governor act classes (hidden runs a7823/63199/bec857fb/b9528: idle
// loco 1/3/5, aim idle 11/12 standing and 14/15 crouched, fall/land 25/26).
bool IdleAct(std::uint16_t act) noexcept { return act == 1 || act == 3 || act == 5; }
bool AimAct(std::uint16_t act) noexcept { return act == 11 || act == 12 || act == 14 || act == 15; }
bool FallAct(std::uint16_t act) noexcept { return act == 25 || act == 26; }
constexpr double kRadPerCdeg = 3.14159265358979323846 / 18000.0;
bool Held(std::uint64_t since, std::uint64_t now, std::uint32_t ms) noexcept { return since && now >= since && now - since >= ms; }
std::uint32_t Ms(std::uint64_t from, std::uint64_t now) noexcept {
    if (!from || now < from) return 0;
    const std::uint64_t d = now - from;
    return d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(d);
}
void Copy3(double (&to)[3], const double (&from)[3]) noexcept { to[0] = from[0]; to[1] = from[1]; to[2] = from[2]; }

// The filtered error d about 300 ms ago (the oldest sample at least 300 ms
// old, else the oldest one).
double DistanceAgo(const State& st, std::uint64_t now, std::uint32_t ago_ms, double fallback) noexcept {
    double best = fallback;
    bool found = false;
    for (std::uint32_t i = 0; i < st.d_count; ++i) {
        const std::uint32_t at = (st.d_head + 32 - i) % 32;  // newest first
        const std::uint64_t t = st.d_hist_ms[at];
        best = st.d_hist[at];
        found = true;
        if (now >= t && now - t >= ago_ms) return best;
    }
    return found ? best : fallback;
}
void PushDistance(State& st, std::uint64_t now, double d) noexcept {
    st.d_head = (st.d_head + 1) % 32;
    st.d_hist[st.d_head] = d;
    st.d_hist_ms[st.d_head] = now;
    if (st.d_count < 32) ++st.d_count;
}
// Per-placement counters; snaps and places stay cumulative (since Reset), so a
// diverging re-placement never hides the snaps that caused it.
void ResetArmCounters(State& st) noexcept {
    st.nudges = st.pins = st.yaws = 0;
    st.nudge_mm = 0.0;
}
void ResetMotionFilters(State& st) noexcept {
    st.tp_count = 0;
    st.v_free[0] = st.v_free[1] = 0.0;
    st.v_remote[0] = st.v_remote[1] = 0.0;
    st.have_ef = false;
    st.e_f[0] = st.e_f[1] = 0.0;
    st.d_count = 0;
    st.over_since = st.vertical_since = st.yaw_since = 0;
    st.runaway_since = st.runaway_exit_since = 0;
    st.runaway_latched = false;
    st.still_since = 0;
    st.lag_since = 0;
    st.lag = false;
    st.grace_since = 0;
    st.grace_speed = 0.0;
}
double Clamp(double v, double lo, double hi) noexcept { return v < lo ? lo : v > hi ? hi : v; }
} // namespace

const char* ActionName(Action action) noexcept {
    switch (action) {
    case Action::place: return "place";
    case Action::nudge: return "nudge";
    case Action::snap: return "snap";
    case Action::pin: return "pin";
    case Action::yaw: return "yaw";
    case Action::face: return "face";
    default: return "none";
    }
}

const char* FaceModeName(FaceMode mode) noexcept {
    switch (mode) {
    case FaceMode::off: return "off";
    case FaceMode::idle: return "idle";
    case FaceMode::aim: return "aim";
    default: return "legacy";
    }
}

const char* FaceStateName(FaceState state) noexcept {
    switch (state) {
    case FaceState::idle: return "idle";
    case FaceState::settle: return "settle";
    case FaceState::aim: return "aim";
    case FaceState::contested: return "contested";
    default: return "off";
    }
}

const char* StanceFixName(StanceFix fix) noexcept {
    switch (fix) {
    case StanceFix::tap: return "tap";
    case StanceFix::hold: return "hold";
    default: return "none";
    }
}

const char* ModeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::place: return "place";
    case Mode::verify: return "verify";
    case Mode::track: return "track";
    case Mode::pin: return "pin";
    case Mode::frozen: return "frozen";
    case Mode::fallback: return "fallback";
    default: return "idle";
    }
}

const char* PlaceReasonName(PlaceReason reason) noexcept {
    switch (reason) {
    case PlaceReason::first: return "first";
    case PlaceReason::rearm: return "rearm";
    case PlaceReason::relink: return "relink";
    case PlaceReason::teleport: return "teleport";
    case PlaceReason::far_error: return "far";
    case PlaceReason::vertical: return "vertical";
    case PlaceReason::diverging: return "diverging";
    case PlaceReason::manual: return "manual";
    case PlaceReason::retry: return "retry";
    default: return "none";
    }
}

const char* FrozenName(Frozen reason) noexcept {
    switch (reason) {
    case Frozen::pose_stale: return "pose_stale";
    case Frozen::input_stale: return "input_stale";
    case Frozen::act: return "act";
    case Frozen::remote_act: return "remote_act";
    case Frozen::stance: return "stance";
    case Frozen::settle: return "settle";
    case Frozen::body: return "body";
    default: return "none";
    }
}

double WrapRad(double radians) noexcept {
    if (!std::isfinite(radians)) return 0.0;
    double r = std::fmod(radians, 2.0 * kPi);
    if (r <= -kPi) r += 2.0 * kPi;
    else if (r > kPi) r -= 2.0 * kPi;
    return r;
}

double Alpha(double dt_ms, double tau_ms) noexcept {
    if (!(dt_ms > 0.0)) return 0.0;
    if (!(tau_ms > 0.0)) return 1.0;
    return 1.0 - std::exp(-dt_ms / tau_ms);
}

std::uint32_t AlignedDelayMs(bool in_valid, std::uint32_t in_age_ms, std::int32_t align_ms) noexcept {
    if (!in_valid) return 1;
    const std::int64_t d = static_cast<std::int64_t>(in_age_ms > 100000u ? 100000u : in_age_ms) - 16 + align_ms;
    if (d < 1) return 1;
    if (d > 150) return 150;
    return static_cast<std::uint32_t>(d);
}

void Reset(State& st) noexcept {
    st = State{};
    st.mode = Mode::idle;
    st.need_place = true;
    st.place_reason = PlaceReason::first;
    st.stop_ok = -1;
    st.face_reason = "none";
}

Params NativeParams(const Params& base, FaceMode face) noexcept {
    Params p = base;
    p.face = face;
    p.neutral_place = true;
    p.place_yaw_cdeg = 1000;
    p.teleport_window_mm = 3000;
    p.teleport_window_ms = 200;
    p.cap_moving_mmps = 300;
    p.cap_idle_mmps = 300;
    p.cap_prone_mmps = 150;
    p.dead_moving_mm = 300;
    p.dead_idle_mm = 200;
    p.dead_prone_mm = 400;
    p.snap_moving_mm = 3000;
    p.snap_moving_hold_ms = 300;
    p.snap_idle_mm = 2000;
    p.snap_idle_hold_ms = 1000;
    p.warp_mm = 10000;
    p.runaway_mmps = 1500;
    p.runaway_moving_mm = 2000;
    p.input_pin_ms = 60000;
    p.diverge_snaps = 3;
    p.diverge_window_ms = 10000;
    p.runaway_split = true;
    p.runaway_rel_pct = 20;
    p.cap_along_pct = 15;
    p.cap_along_min_mmps = 300;
    p.cap_along_max_mmps = 1500;
    p.cap_along_prone_min_mmps = 150;
    p.cap_along_prone_max_mmps = 400;
    p.stop_grace_ms = 1200;
    return p;
}

Out Step(State& st, const In& in, const Params& P) noexcept {
    Out o{};
    const std::uint64_t now = in.now_ms;
    const double dt = (st.started && now > st.prev_ms) ? static_cast<double>(now - st.prev_ms > 250 ? 250 : now - st.prev_ms) : 0.0;

    // ---- Observations: the remote's velocity and teleports ------------------
    bool teleport = false;
    if (in.target_ok) {
        if (st.have_target && dt > 0.0) {
            const double jump = Len3(in.target, st.prev_target);
            const double jump_speed = Len2(in.target[0] - st.prev_target[0], in.target[2] - st.prev_target[2]) * 1000.0 / dt;
            if (in.target_snapped || jump > P.teleport_jump_mm) {
                teleport = true;
                st.v_remote[0] = st.v_remote[1] = 0.0;
            } else if (jump_speed > P.max_speed_mmps) {
                // A snapshot correction step, not the remote's velocity.
            } else {
                const double a = Alpha(dt, P.ema_ms);
                const double vx = (in.target[0] - st.prev_target[0]) * 1000.0 / dt, vz = (in.target[2] - st.prev_target[2]) * 1000.0 / dt;
                st.v_remote[0] += a * (vx - st.v_remote[0]);
                st.v_remote[1] += a * (vz - st.v_remote[1]);
            }
        }
        // Windowed teleport: the interpolated pose spreads a teleport over a
        // few ticks (each below teleport_jump_mm); no locomotion covers
        // teleport_window_mm horizontally within teleport_window_ms.
        if (P.teleport_window_mm && !teleport) {
            for (std::uint32_t i = 0; i < st.tp_count; ++i) {
                const std::uint32_t at = (st.tp_head + 16 - i) % 16;
                if (now < st.tp_ms[at] || now - st.tp_ms[at] > P.teleport_window_ms) break;
                if (Len2(in.target[0] - st.tp_pos[at][0], in.target[2] - st.tp_pos[at][2]) > P.teleport_window_mm) {
                    teleport = true;
                    st.v_remote[0] = st.v_remote[1] = 0.0;
                    break;
                }
            }
        }
        if (teleport) st.tp_count = 0;
        st.tp_head = (st.tp_head + 1) % 16;
        Copy3(st.tp_pos[st.tp_head], in.target);
        st.tp_ms[st.tp_head] = now;
        if (st.tp_count < 16) ++st.tp_count;
        Copy3(st.prev_target, in.target);
        st.have_target = true;
    }
    // ---- The body's own motion (readback minus what we wrote) ---------------
    bool body_jump = false;
    if (in.body_ok) {
        if (st.have_body && dt > 0.0) {
            double expected[3];
            if (st.wrote && in.last_ok) {
                expected[0] = st.last_write[0];
                // A nudge leaves y to the game: compare y with the previous readback.
                expected[1] = st.last_action == Action::nudge ? st.prev_body[1] : st.last_write[1];
                expected[2] = st.last_write[2];
            } else {
                Copy3(expected, st.prev_body);
            }
            const double moved = Len3(in.body, expected);
            const double moved_speed = Len2(in.body[0] - expected[0], in.body[2] - expected[2]) * 1000.0 / dt;
            if (moved > P.body_jump_mm && (st.mode == Mode::track || st.mode == Mode::pin || st.mode == Mode::frozen)) {
                body_jump = true;
            } else if (moved_speed > P.max_speed_mmps) {
                // A discontinuity (pushed, a missed write): not locomotion.
            } else {
                const double a = Alpha(dt, P.ema_ms);
                const double vx = (in.body[0] - expected[0]) * 1000.0 / dt, vz = (in.body[2] - expected[2]) * 1000.0 / dt;
                st.v_free[0] += a * (vx - st.v_free[0]);
                st.v_free[1] += a * (vz - st.v_free[1]);
            }
        }
        Copy3(st.prev_body, in.body);
        st.have_body = true;
    } else {
        st.have_body = false;
    }
    // The previous step's writes (the body's own yaw rate, the contest guard).
    const bool prev_face = st.face_wrote && in.last_ok;
    const bool prev_yaw_write = st.wrote_yaw && in.last_ok;
    const double prev_write_yaw = st.last_write_yaw;
    st.wrote = false;
    st.last_action = Action::none;
    st.wrote_yaw = false;
    st.face_wrote = false;
    if (in.body_ok && std::isfinite(in.body_yaw)) {
        if (st.face_have_prev && dt > 0.0) {
            const double from = prev_yaw_write ? prev_write_yaw : st.face_prev_yaw;
            const double rate = WrapRad(in.body_yaw - from) * 1000.0 / dt;
            st.face_own_rate += Alpha(dt, P.ema_ms) * (rate - st.face_own_rate);
        }
        st.face_prev_yaw = in.body_yaw;
        st.face_have_prev = true;
    } else {
        st.face_have_prev = false;
        st.face_own_rate = 0.0;
    }
    // ---- Errors --------------------------------------------------------------
    double ex = 0.0, ez = 0.0, err = 0.0, dy = 0.0;
    const bool have_err = in.body_ok && in.target_ok;
    if (have_err) {
        ex = in.target[0] - in.body[0];
        ez = in.target[2] - in.body[2];
        err = Len2(ex, ez);
        dy = in.target[1] - in.body[1];
        if (!st.have_ef || teleport) {
            st.e_f[0] = ex;
            st.e_f[1] = ez;
            st.have_ef = true;
        } else {
            const double a = Alpha(dt, P.ema_ms);
            st.e_f[0] += a * (ex - st.e_f[0]);
            st.e_f[1] += a * (ez - st.e_f[1]);
        }
    }
    double d_f = have_err ? Len2(st.e_f[0], st.e_f[1]) : 0.0;
    const double speed_remote = Len2(st.v_remote[0], st.v_remote[1]);
    const double speed_free = Len2(st.v_free[0], st.v_free[1]);
    const double r = std::isfinite(in.in_r) ? in.in_r : 0.0;
    const bool idle_now = speed_remote < P.idle_mmps && r < P.idle_r;
    if (idle_now) { if (!st.idle_since) st.idle_since = now ? now : 1; } else { st.idle_since = 0; }
    const bool remote_idle = Held(st.idle_since, now, P.idle_hold_ms);
    const bool remote_moving = speed_remote >= P.moving_mmps || r >= P.moving_r;
    if (speed_free < P.yaw_still_mmps) { if (!st.still_since) st.still_since = now ? now : 1; } else { st.still_since = 0; }
    // The body's own velocity against the remote's (runaway with a moving remote),
    // also split along and across the remote's direction of motion.
    const double dv = Len2(st.v_free[0] - st.v_remote[0], st.v_free[1] - st.v_remote[1]);
    double dv_along = 0.0, dv_cross = dv;
    if (speed_remote >= P.moving_mmps) {
        const double ux = st.v_remote[0] / speed_remote, uz = st.v_remote[1] / speed_remote;
        const double dvx = st.v_free[0] - st.v_remote[0], dvz = st.v_free[1] - st.v_remote[1];
        dv_along = dvx * ux + dvz * uz;
        dv_cross = std::fabs(dvx * uz - dvz * ux);
    }
    // Net input staleness (the window injects a neutral pad while stale).
    const bool input_stale = in.input_net && (!in.in_valid || in.in_age_ms > P.input_stale_ms);
    if (input_stale) { if (!st.input_stale_since) st.input_stale_since = now ? now : 1; } else { st.input_stale_since = 0; }

    // ---- Stance (debounced remote stance) ------------------------------------
    std::uint8_t stance_from = 0, stance_to = 0;
    std::uint32_t suspend_ms = 0;
    if (in.remote_anim_valid && in.remote_stance >= 1 && in.remote_stance <= 3) {
        if (in.remote_stance != st.stance_applied) {
            if (in.remote_stance != st.stance_candidate) {
                st.stance_candidate = in.remote_stance;
                st.stance_since = now;
            }
            if (now - st.stance_since >= P.stance_debounce_ms) {
                stance_from = st.stance_applied;
                stance_to = st.stance_candidate;
                if (stance_from) {
                    suspend_ms = (stance_from == 3 || stance_to == 3) ? P.stance_suspend_prone_ms : P.stance_suspend_ms;
                    st.suspend_until = now + suspend_ms;
                    st.dead_until = st.suspend_until + P.stance_dead_ms;
                    o.events |= Event::stance;
                }
                st.stance_applied = stance_to;
                st.stance_candidate = 0;
                st.mismatch_since = now;
                st.mismatch_logged = false;
            }
        } else {
            st.stance_candidate = 0;
        }
    }
    std::uint32_t mismatch_ms = 0;
    if (st.stance_applied && in.body_anim_ok && in.body_stance >= 1 && in.body_stance <= 3 && in.body_stance != st.stance_applied) {
        if (!st.mismatch_since) st.mismatch_since = now;
        mismatch_ms = Ms(st.mismatch_since, now);
        if (!st.mismatch_logged && mismatch_ms >= P.stance_mismatch_ms) {
            st.mismatch_logged = true;
            o.events |= Event::stance_mismatch;
        }
    } else {
        st.mismatch_since = now;
    }
    const bool prone = st.stance_applied ? st.stance_applied == 3 : (in.body_anim_ok && in.body_stance == 3);

    // ---- Stop check (diagnostic) ----------------------------------------------
    std::uint32_t stop_ms = 0;
    if (st.started && st.prev_r >= P.stop_from_r && r < P.stop_to_r) {
        st.stop_pending = true;
        st.stop_since = now;
        if (P.stop_grace_ms) {
            st.grace_since = now ? now : 1;
            st.grace_speed = speed_free;
        }
    }
    // Stop grace: the release's slide may keep the body moving for a while; it
    // ends at the deadline, when the body speeds up again or the stick moves.
    bool grace = false;
    if (st.grace_since) {
        if (now - st.grace_since < P.stop_grace_ms && speed_free <= st.grace_speed + 100.0 && r < P.moving_r) grace = true;
        else st.grace_since = 0;
    }
    if (st.stop_pending && st.mode != Mode::verify && st.mode != Mode::place) {
        stop_ms = Ms(st.stop_since, now);
        if (speed_free < P.stop_mmps && stop_ms > 0) {
            st.stop_pending = false;
            st.stop_ok = 1;
            o.events |= Event::stop_check;
        } else if (stop_ms >= P.stop_window_ms) {
            st.stop_pending = false;
            st.stop_ok = 0;
            o.events |= Event::stop_check;
        }
    }
    st.prev_r = r;

    const auto finish = [&](Mode mode) noexcept {
        st.mode = mode;
        o.mode = mode;
        st.prev_ms = now;
        st.started = true;
        Report& rp = o.report;
        rp.err_mm = err;
        rp.dy_mm = dy;
        const double vx = st.v_remote[0], vz = st.v_remote[1];
        double ux = 0.0, uz = 0.0;
        if (speed_remote >= P.moving_mmps) { ux = vx / speed_remote; uz = vz / speed_remote; }
        else if (in.target_ok) { ux = std::sin(in.target_yaw); uz = std::cos(in.target_yaw); }
        rp.along_mm = ex * ux + ez * uz;
        rp.cross_mm = ex * uz - ez * ux;
        rp.v_free_mmps = speed_free;
        rp.v_remote_mmps = speed_remote;
        rp.yaw_err_mrad = (in.target_ok && in.body_ok) ? WrapRad(in.target_yaw - in.body_yaw) * 1000.0 : 0.0;
        rp.d_filtered_mm = d_f;
        rp.remote_idle = remote_idle;
        rp.remote_moving = remote_moving;
        rp.prone = prone;
        rp.frozen = st.frozen;
        rp.stop_ok = st.stop_ok;
        rp.nudges = st.nudges; rp.snaps = st.snaps; rp.places = st.places; rp.pins = st.pins; rp.yaws = st.yaws;
        rp.nudge_mm = st.nudge_mm;
        rp.snaps_window = st.snap_count;
        rp.place_tries = st.tries;
        rp.idle_ms = Ms(st.idle_since, now);
        rp.stance_from = stance_from;
        rp.stance_to = stance_to;
        rp.suspend_ms = suspend_ms;
        rp.mismatch_ms = mismatch_ms;
        rp.stop_ms = stop_ms;
        rp.runaway_active = st.runaway_active;
        rp.runaway_moving = st.runaway_moving;
        rp.runaway_idle = st.runaway_active && remote_idle;
        rp.lag = st.lag;
        rp.stop_grace = grace;
        rp.dv_mmps = dv;
        rp.dv_along_mmps = dv_along;
        rp.dv_cross_mmps = dv_cross;
        rp.input_pin = st.input_pin;
        if (rp.place_reason == PlaceReason::none) rp.place_reason = st.place_reason;
        rp.face_state = st.face_state;
        rp.face_reason = st.face_reason ? st.face_reason : "none";
        rp.face_target_mrad = in.target_ok ? WrapRad(in.target_yaw) * 1000.0 : 0.0;
        rp.face_body_mrad = in.body_ok ? WrapRad(in.body_yaw) * 1000.0 : 0.0;
        rp.face_err_mrad = (in.target_ok && in.body_ok) ? WrapRad(in.target_yaw - in.body_yaw) * 1000.0 : 0.0;
        rp.face_rate_dps = st.face_own_rate * 57.29577951308232;
        rp.face_writes = st.face_writes;
        rp.face_snaps = st.face_snaps;
        rp.face_contests = st.face_contests;
        rp.face_turns = st.face_turns;
        return o;
    };
    const auto write = [&](Action action, const double (&pos)[3], double yaw) noexcept {
        o.action = action;
        Copy3(o.pos, pos);
        o.yaw = yaw;
        st.wrote = true;
        st.last_action = action;
        Copy3(st.last_write, pos);
        if (action != Action::nudge) {
            st.wrote_yaw = true;
            st.last_write_yaw = yaw;
        }
    };
    // A turn ends (on target or aborted): the event once, with its reason.
    const auto face_stop = [&](const char* why) noexcept {
        if (st.face_turning) {
            st.face_turning = false;
            o.events |= Event::face_end;
        }
        if (st.face_state != FaceState::contested) st.face_state = FaceState::off;
        st.face_reason = why;
    };
    const auto face_write = [&](double yaw, bool epoch) noexcept {
        write(Action::face, in.body, yaw);
        o.face_epoch = epoch;
        st.face_wrote = true;
        st.face_last_write = yaw;
        ++st.face_writes;
    };
    const bool governor = P.face == FaceMode::idle || P.face == FaceMode::aim;
    // Contest guard: the previous governor write did not hold.
    if (governor && prev_face && in.body_ok && std::fabs(WrapRad(in.body_yaw - st.face_last_write)) > P.face_contest_cdeg * kRadPerCdeg) {
        o.events |= Event::face_contest;
        ++st.face_contests;
        face_stop("contest");
        st.face_backoff_until = now + P.face_backoff_ms;
        std::uint32_t kept = 0;
        for (std::uint32_t i = 0; i < st.face_contest_count && i < 4; ++i)
            if (now - st.face_contest_ms[i] < P.face_contest_window_ms) st.face_contest_ms[kept++] = st.face_contest_ms[i];
        if (kept < 4) st.face_contest_ms[kept++] = now;
        st.face_contest_count = kept;
        if (kept >= P.face_contests) {
            st.face_latch_until = now + P.face_latch_ms;
            st.face_contest_count = 0;
            st.face_state = FaceState::contested;
            st.face_reason = "latched";
            o.events |= Event::face_latched;
        }
    }
    const auto request_place = [&](PlaceReason reason) noexcept {
        if (!st.need_place) { st.need_place = true; st.place_reason = reason; }
    };

    // ---- Not armable: the driver keeps the proven fallback --------------------
    if (!in.armable) {
        if (st.armable_prev || !st.started) {
            st.need_place = true;
            st.place_reason = st.placed_once ? PlaceReason::rearm : PlaceReason::first;
        }
        st.armable_prev = false;
        if (st.runaway_active) { st.runaway_active = false; }
        st.lag = false;
        st.lag_since = 0;
        st.input_pin = false;
        st.frozen = Frozen::none;
        o.arm = false;
        if (governor) face_stop("unarmed");
        return finish(Mode::fallback);
    }
    if (!st.armable_prev && st.started && !st.need_place) request_place(st.placed_once ? PlaceReason::rearm : PlaceReason::first);
    st.armable_prev = true;
    if (in.place_request != PlaceReason::none) { st.need_place = true; st.place_reason = in.place_request; }
    if (in.relink) { st.need_place = true; st.place_reason = PlaceReason::relink; }
    const bool armed_mode = st.mode == Mode::track || st.mode == Mode::pin || st.mode == Mode::frozen;
    const bool gap_ok = !st.last_hard_ms || now - st.last_hard_ms >= P.hard_gap_ms;
    bool far_snap = false;
    if (armed_mode && P.recon != PartnerRecon::off) {
        // Far and vertical errors only with live input: with a neutral pad the
        // body stands, and the input pin follows the pose instead of a 10 m
        // Warp cycle.
        if (teleport || body_jump) {
            st.need_place = true;
            st.place_reason = PlaceReason::teleport;
        } else if (have_err && err >= P.warp_mm && gap_ok && !input_stale) {
            if (prone) far_snap = true;
            else request_place(PlaceReason::far_error);
        }
        if (have_err && std::fabs(dy) >= P.vertical_mm && !input_stale) {
            if (!st.vertical_since) st.vertical_since = now ? now : 1;
            if (Held(st.vertical_since, now, P.vertical_hold_ms) && gap_ok) {
                if (prone) far_snap = true;
                else request_place(PlaceReason::vertical);
            }
        } else {
            st.vertical_since = 0;
        }
    }

    // ---- Placement: disarmed Warp to the aligned target, then verify ----------
    if (st.need_place) {
        o.arm = o.neutral = P.neutral_place;
        st.frozen = Frozen::none;
        if (governor) face_stop("place");
        // A live target is enough: stale input only freezes the corrections.
        if (in.target_ok && in.target_live) {
            const PlaceReason reason = st.place_reason == PlaceReason::none ? PlaceReason::manual : st.place_reason;
            const bool retry_same = st.mode == Mode::verify;
            write(Action::place, in.target, in.target_yaw);
            o.place_reason = reason;
            o.report.place_reason = reason;
            o.events |= Event::place;
            st.need_place = false;
            st.place_reason = reason;
            Copy3(st.placed_target, in.target);
            st.placed_yaw = in.target_yaw;
            st.place_yaw_fixed = false;
            st.face_settle_until = now + P.face_settle_ms;
            st.face_settle_done = false;
            st.place_ms = now;
            st.tries = retry_same ? st.tries + 1 : 1;
            st.last_hard_ms = now;
            st.runaway_active = false;
            st.input_pin = false;
            ResetMotionFilters(st);
            ResetArmCounters(st);
            ++st.places;
            if (reason == PlaceReason::diverging) {
                const std::uint32_t pin = st.pin_ms_next ? st.pin_ms_next : P.diverge_pin_ms;
                st.pin_until = now + pin;
                st.pin_ms_next = pin * 2 > P.diverge_pin_max_ms ? P.diverge_pin_max_ms : pin * 2;
                st.last_diverge_ms = now;
            }
            return finish(Mode::verify);
        }
        return finish(Mode::place);
    }
    if (st.mode == Mode::verify) {
        o.arm = o.neutral = P.neutral_place;
        const double place_err = in.body_ok ? Len3(in.body, st.placed_target) : -1.0;
        o.report.place_err_mm = place_err;
        o.report.place_ms = Ms(st.place_ms, now);
        const double place_yaw_err = in.body_ok ? WrapRad(st.placed_yaw - in.body_yaw) : 0.0;
        o.report.place_yaw_err_mrad = place_yaw_err * 1000.0;
        // The placed body's facing: one xform at the placed yaw when it is
        // off (a Warp under a held aim kept turning toward the viewer's look),
        // then verify again; a second miss is accepted (logged).
        if (in.body_ok && place_err <= P.place_ok_mm && P.place_yaw_cdeg && !st.place_yaw_fixed &&
            std::fabs(place_yaw_err) > P.place_yaw_cdeg * kRadPerCdeg) {
            st.place_yaw_fixed = true;
            write(Action::face, in.body, st.placed_yaw);
            o.face_epoch = true;
            o.events |= Event::place_yaw_fix;
            return finish(Mode::verify);
        }
        if (in.body_ok && place_err <= P.place_ok_mm) {
            o.events |= Event::placed_ok;
            st.placed_once = true;
            st.settle_until = now + P.settle_ms;
            st.snap_count = 0;
            o.arm = true;
            o.neutral = false;
            return finish(Mode::track);
        }
        if (now - st.place_ms >= P.place_retry_ms) {
            if (st.tries < P.place_tries) {
                st.need_place = true;  // next tick (counted as a retry of the same placement)
                // keep Mode::verify so the next placement counts tries.
                return finish(Mode::verify);
            }
            o.events |= Event::placed_fail;
            st.retry_at = now + P.place_fail_retry_ms;
            return finish(Mode::fallback);
        }
        return finish(Mode::verify);
    }
    if (st.mode == Mode::fallback || st.mode == Mode::idle || st.mode == Mode::place) {
        // A failed placement: the driver's per-tick Warp fallback keeps the
        // partner following; try again later.
        o.arm = o.neutral = P.neutral_place;
        if (now >= st.retry_at) request_place(PlaceReason::retry);
        return finish(Mode::fallback);
    }

    // ---- Armed ------------------------------------------------------------------
    o.arm = true;
    PushDistance(st, now, d_f);
    if (st.last_diverge_ms && now - st.last_diverge_ms >= P.diverge_reset_ms) {
        st.pin_ms_next = 0;
        st.last_diverge_ms = 0;
    }
    if (P.recon == PartnerRecon::off) {
        st.frozen = Frozen::none;
        return finish(Mode::track);
    }
    // Frozen reason (corrections only; placement and pins are decided above/below).
    Frozen frozen = Frozen::none;
    if (!in.body_ok) frozen = Frozen::body;
    else if (!in.target_ok || !in.target_live) frozen = Frozen::pose_stale;
    else if (in.input_net && (!in.in_valid || in.in_age_ms > P.input_stale_ms)) frozen = Frozen::input_stale;
    else if (in.body_anim_ok && !Loco(in.body_act)) frozen = Frozen::act;
    else if (in.remote_anim_valid && !Loco(in.remote_act)) frozen = Frozen::remote_act;
    else if (now < st.suspend_until) frozen = Frozen::stance;
    else if (now < st.settle_until) frozen = Frozen::settle;
    if (frozen != st.frozen) {
        if (frozen != Frozen::none) o.events |= Event::frozen;
        else o.events |= Event::unfrozen;
        st.frozen = frozen;
    }
    // Stance level (net input): the input carries stance as edges only, so one
    // lost or extra edge inverts crouch/stand until the remote toggles again.
    // A mismatch held stance_fix_ms asks for one corrective tap toward the
    // remote's stance (a hold for prone), at most every stance_fix_gap_ms.
    if (P.stance_fix && in.input_net && !input_stale && in.target_live && st.stance_applied && in.body_anim_ok &&
        in.body_stance >= 1 && in.body_stance <= 3 && in.body_stance != st.stance_applied && Loco(in.body_act) &&
        (!in.remote_anim_valid || Loco(in.remote_act)) && mismatch_ms >= P.stance_fix_ms &&
        (!st.last_stance_fix_ms || now - st.last_stance_fix_ms >= P.stance_fix_gap_ms)) {
        o.stance_fix = st.stance_applied == 3 ? StanceFix::hold : StanceFix::tap;
        o.report.fix_body = in.body_stance;
        o.report.fix_remote = st.stance_applied;
        o.events |= Event::stance_fix;
        st.last_stance_fix_ms = now;
        st.mismatch_since = now;  // wait for the result before the next one
    }
    // Runaway: the body moves on its own. Idle remote: the body moves. Moving
    // remote: its own velocity differs from the remote's while the error is
    // large and growing (a turn or a start lags but does not keep diverging).
    // runaway_split: an overrun or a sideways drift only; a slower body is lag.
    std::uint32_t runaway_ms = 0;
    const double thr = P.runaway_split ? (P.runaway_rel_pct * speed_remote / 100.0 > P.runaway_mmps
                                              ? P.runaway_rel_pct * speed_remote / 100.0 : static_cast<double>(P.runaway_mmps))
                                       : static_cast<double>(P.runaway_mmps);
    if (P.runaway_split) {
        const bool lag_now = in.body_ok && speed_remote >= P.moving_mmps && dv_along < -thr && d_f >= P.dead_moving_mm;
        if (lag_now) {
            if (!st.lag_since) st.lag_since = now ? now : 1;
            if (!st.lag && Held(st.lag_since, now, P.runaway_hold_ms)) {
                st.lag = true;
                o.events |= Event::lag;
            }
        } else {
            st.lag_since = 0;
            if (st.lag) {
                st.lag = false;
                o.events |= Event::lag_end;
            }
        }
    }
    if (!st.runaway_active) {
        const bool idle_case = remote_idle && speed_free > P.runaway_mmps && !grace;
        const bool moving_case = !idle_case && remote_moving &&
                                 (P.runaway_split ? (dv_along > thr || dv_cross > thr) : dv > P.runaway_mmps);
        if ((idle_case || moving_case) && in.body_ok) {
            if (!st.runaway_since) st.runaway_since = now ? now : 1;
            bool fire = Held(st.runaway_since, now, P.runaway_hold_ms);
            if (fire && moving_case) {
                const double d_ago = DistanceAgo(st, now, 300, d_f);
                fire = d_f >= P.runaway_moving_mm && d_f >= d_ago + P.runaway_grow_mm;
            }
            if (fire && !st.runaway_latched) {
                o.events |= Event::runaway;
                st.runaway_exit_since = 0;
                st.runaway_moving = moving_case;
                if (P.runaway != PartnerRunaway::off) {
                    st.runaway_active = true;
                    st.runaway_start = now;
                    st.runaway_since = 0;
                } else {
                    st.runaway_latched = true;  // runaway=off: one event per episode
                }
            }
        } else {
            st.runaway_since = 0;
            st.runaway_latched = false;
        }
    } else {
        // Ends only when the body's own motion matches the remote's again (a
        // stuck body never "ends" because the remote starts moving).
        const double exit = remote_moving ? P.runaway_exit_moving_mmps : P.runaway_exit_mmps;
        bool end = false;
        if (dv < exit) {
            if (!st.runaway_exit_since) st.runaway_exit_since = now ? now : 1;
            if (Held(st.runaway_exit_since, now, P.runaway_exit_ms)) end = true;
        } else {
            st.runaway_exit_since = 0;
        }
        if (end) {
            runaway_ms = Ms(st.runaway_start, now);
            st.runaway_active = false;
            st.runaway_exit_since = 0;
            o.events |= Event::runaway_end;
        }
    }
    o.report.runaway_ms = runaway_ms;
    // Input pin: the net input stayed stale while the pose is live. The window
    // injects a neutral pad, so the body would stand while the remote runs;
    // follow the pose instead, then resume tracking (settle) once it is live.
    const bool pose_live = in.target_ok && in.target_live;
    if (!st.input_pin) {
        if (Held(st.input_stale_since, now, P.input_pin_ms) && pose_live) {
            st.input_pin = true;
            st.input_pin_start = now;
            o.events |= Event::input_pin;
        }
    } else if (!input_stale || !pose_live) {
        o.report.input_pin_ms = Ms(st.input_pin_start, now);
        st.input_pin = false;
        o.events |= Event::input_pin_end;
        if (!input_stale) {
            ResetMotionFilters(st);
            st.settle_until = now + P.settle_ms;
        }
    }
    // Pinned: recon=pin, an active runaway, a diverging pin or the input pin.
    const bool diverge_pin = st.pin_until && now < st.pin_until;
    if (st.pin_until && now >= st.pin_until) {
        st.pin_until = 0;
        o.events |= Event::pin_end;
    }
    if (P.recon == PartnerRecon::pin || st.runaway_active || diverge_pin || st.input_pin) {
        if (governor) face_stop("pin");
        if (st.runaway_active && P.runaway == PartnerRunaway::disarm) o.arm = false;
        if (in.target_ok) {
            write(Action::pin, in.target, in.target_yaw);
            ++st.pins;
            st.have_ef = false;
        }
        return finish(Mode::pin);
    }
    if (far_snap && in.target_ok) {
        write(Action::snap, in.target, in.target_yaw);
        st.last_hard_ms = now;
        ++st.snaps;
        st.have_ef = false;
        o.events |= Event::snap;
        if (governor) {
            face_stop("snap");
            st.face_settle_until = now + P.face_settle_ms;
            st.face_settle_done = false;
        }
        return finish(Mode::track);
    }
    // ---- Facing governor (FaceMode idle|aim) ---------------------------------
    // Decided here (settle and aim act while the corrections are frozen by a
    // non-locomotion act); an idle step waits for the hard snap below.
    bool face_idle_step = false;
    double face_idle_yaw = 0.0;
    bool face_idle_epoch = false;
    if (governor) {
        const double r_now = std::isfinite(in.in_r) ? in.in_r : 0.0;
        const bool stick_quiet = r_now < P.face_stick_r;
        const bool quiet_now = stick_quiet && speed_remote < P.face_remote_mmps && speed_free < P.face_body_mmps;
        if (quiet_now) { if (!st.face_quiet_since) st.face_quiet_since = now ? now : 1; } else { st.face_quiet_since = 0; }
        const bool quiet = Held(st.face_quiet_since, now, P.face_quiet_ms);
        const bool data = in.body_ok && in.target_ok && in.target_live && std::isfinite(in.body_yaw) && std::isfinite(in.target_yaw) &&
                          frozen != Frozen::pose_stale && frozen != Frozen::input_stale && frozen != Frozen::body;
        const double ferr = data ? WrapRad(in.target_yaw - in.body_yaw) : 0.0;
        const double aerr = std::fabs(ferr);
        const bool body_act = in.body_anim_ok;
        const bool settle_window = now < st.face_settle_until || (body_act && FallAct(in.body_act));
        const bool idle_class = body_act && IdleAct(in.body_act) &&
                                (!in.remote_anim_valid || in.remote_act == in.body_act);
        const bool aim_class = P.face == FaceMode::aim && body_act && AimAct(in.body_act) && in.remote_anim_valid && AimAct(in.remote_act);
        const auto turn_params = [&](FaceState kind) noexcept {
            const double dps = kind == FaceState::aim ? P.face_aim_dps : P.face_dps;
            const double acc = kind == FaceState::aim ? P.face_aim_accel_dps2 : P.face_accel_dps2;
            return partner_facing::Params{static_cast<float>(dps * kRadPerCdeg * 100.0), static_cast<float>(acc * kRadPerCdeg * 100.0)};
        };
        if (!settle_window) st.face_settle_done = false;
        if (st.face_state == FaceState::contested && now >= st.face_latch_until) {
            st.face_state = FaceState::off;
            st.face_reason = "unlatched";
        }
        if (now < st.face_latch_until) {
            if (st.face_turning) face_stop("latched");
            st.face_state = FaceState::contested;
            st.face_reason = "latched";
        } else if (!data) {
            face_stop("no_data");
        } else if (!stick_quiet) {
            face_stop("stick");  // any stick input aborts in the same tick
        } else if (now < st.face_backoff_until) {
            face_stop("backoff");
        } else if (!quiet) {
            face_stop("moving");
        } else if (settle_window && !st.face_settle_done && aerr > P.face_settle_cdeg * kRadPerCdeg) {
            // One snap per settle window (a placement, snap or teleport is a
            // discontinuity already; fall/land acts are not locomotion).
            if (st.face_turning) face_stop("settle");
            st.face_settle_done = true;
            st.face_state = FaceState::settle;
            st.face_reason = "snap";
            ++st.face_snaps;
            o.events |= Event::face_snap;
            face_write(in.target_yaw, true);
            return finish(frozen != Frozen::none ? Mode::frozen : Mode::track);
        } else {
            const FaceState kind = aim_class ? FaceState::aim : idle_class ? FaceState::idle : FaceState::off;
            if (st.face_turning && kind != st.face_turn_kind) face_stop("act");
            if (kind == FaceState::off) {
                face_stop(settle_window ? "settle" : "act");
                if (settle_window) st.face_state = FaceState::settle;
            } else if (!st.face_turning) {
                // Enter: the error held (aim: and the body's own turning is slow).
                const std::uint32_t enter = kind == FaceState::aim ? P.face_aim_cdeg : P.face_enter_cdeg;
                const std::uint32_t hold = kind == FaceState::aim ? P.face_aim_hold_ms : P.face_enter_ms;
                if (aerr > enter * kRadPerCdeg) { if (!st.face_err_since) st.face_err_since = now ? now : 1; } else { st.face_err_since = 0; }
                const bool slow = kind != FaceState::aim || std::fabs(st.face_own_rate) * 57.29577951308232 < P.face_aim_rate_dps;
                if (Held(st.face_err_since, now, hold) && slow) {
                    st.face_turning = true;
                    st.face_turn_kind = kind;
                    st.face_turn = partner_facing::State{};
                    st.face_turn.valid = true;
                    st.face_turn.yaw = static_cast<float>(WrapRad(in.body_yaw));
                    st.face_turn.ms = now;
                    st.face_err_since = 0;
                    st.face_state = kind;
                    st.face_reason = "turn";
                    ++st.face_turns;
                    o.events |= Event::face_start;
                } else {
                    st.face_state = FaceState::off;
                    st.face_reason = aerr > enter * kRadPerCdeg ? (slow ? "hold" : "rate") : "small";
                }
            }
            if (st.face_turning) {
                // Below the exit error the last write lands exactly on the
                // target (the turn's end bumps the epoch like its start).
                double applied = WrapRad(in.target_yaw);
                bool landed = aerr < P.face_exit_cdeg * kRadPerCdeg;
                if (!landed) {
                    applied = static_cast<double>(partner_facing::Step(st.face_turn, static_cast<float>(applied), now,
                                                                       turn_params(st.face_turn_kind), partner_facing::Snap::none));
                    landed = std::fabs(WrapRad(in.target_yaw - applied)) < 1e-4;
                    if (landed) applied = WrapRad(in.target_yaw);
                }
                const bool first = (o.events & Event::face_start) != 0;
                if (st.face_turn_kind == FaceState::aim || frozen != Frozen::none) {
                    // Aim acts are frozen for the corrections: write now.
                    face_write(applied, first || landed);
                    if (landed) face_stop("done");
                    return finish(frozen != Frozen::none ? Mode::frozen : Mode::track);
                }
                face_idle_step = true;
                face_idle_yaw = applied;
                face_idle_epoch = first || landed;
            }
        }
    }
    if (frozen != Frozen::none) {
        if (face_idle_step) face_stop("frozen");
        return finish(Mode::frozen);
    }

    // Hard snap: a large sustained error.
    const std::uint32_t thresh = remote_moving ? P.snap_moving_mm : P.snap_idle_mm;
    const std::uint32_t hold = remote_moving ? P.snap_moving_hold_ms : P.snap_idle_hold_ms;
    if (d_f >= thresh) {
        if (!st.over_since) st.over_since = now ? now : 1;
        const double d_ago = DistanceAgo(st, now, 300, d_f);
        const bool shrinking = d_f < d_ago - 100.0;
        if (Held(st.over_since, now, hold) && gap_ok && (remote_moving || !shrinking)) {
            write(Action::snap, in.target, in.target_yaw);
            st.last_hard_ms = now;
            st.over_since = 0;
            st.have_ef = false;
            ++st.snaps;
            o.events |= Event::snap;
            if (governor) {
                face_stop("snap");
                st.face_settle_until = now + P.face_settle_ms;
                st.face_settle_done = false;
            }
            // Diverging: diverge_snaps hard snaps within the window.
            std::uint32_t kept = 0;
            for (std::uint32_t i = 0; i < st.snap_count && i < 8; ++i)
                if (now - st.snap_times[i] < P.diverge_window_ms) st.snap_times[kept++] = st.snap_times[i];
            if (kept < 8) st.snap_times[kept++] = now;
            st.snap_count = kept;
            if (st.snap_count >= P.diverge_snaps) {
                o.events |= Event::diverging;
                st.snap_count = 0;
                request_place(PlaceReason::diverging);
            }
            return finish(Mode::track);
        }
    } else {
        st.over_since = 0;
    }
    // The governor's idle turn step (one xform at the body's own position).
    if (face_idle_step) {
        face_write(face_idle_yaw, face_idle_epoch);
        if (face_idle_epoch && std::fabs(WrapRad(in.target_yaw - face_idle_yaw)) < 1e-4) face_stop("done");
        return finish(Mode::track);
    }
    // Idle facing (legacy): both still, the facing differs for a while.
    const double yaw_err = WrapRad(in.target_yaw - in.body_yaw);
    if (P.face == FaceMode::legacy && remote_idle && Held(st.still_since, now, P.yaw_still_ms) && std::fabs(yaw_err) * 1000.0 > P.yaw_mrad) {
        if (!st.yaw_since) st.yaw_since = now ? now : 1;
        if (Held(st.yaw_since, now, P.yaw_hold_ms) && (!st.last_yaw_ms || now - st.last_yaw_ms >= P.yaw_gap_ms)) {
            write(Action::yaw, in.body, in.target_yaw);
            st.last_yaw_ms = now;
            st.yaw_since = 0;
            ++st.yaws;
            o.events |= Event::yaw;
            return finish(Mode::track);
        }
    } else {
        st.yaw_since = 0;
    }
    // Nudge: position only, at most every nudge_period_ms.
    std::uint32_t dead = prone ? P.dead_prone_mm : remote_moving ? P.dead_moving_mm : P.dead_idle_mm;
    if (now < st.dead_until) dead *= 2;
    if (d_f >= dead && (!st.last_nudge_ms || now - st.last_nudge_ms >= P.nudge_period_ms)) {
        const double dtn = !st.last_nudge_ms ? static_cast<double>(P.nudge_period_ms)
                                              : static_cast<double>(now - st.last_nudge_ms > 200 ? 200 : now - st.last_nudge_ms);
        const double tau = remote_moving ? P.tau_moving_ms : P.tau_idle_ms;
        const double cap = (prone ? P.cap_prone_mmps : remote_moving ? P.cap_moving_mmps : P.cap_idle_mmps) * dtn / 1000.0;
        const double k = Alpha(dtn, tau);
        double dx = st.e_f[0] * k, dz = st.e_f[1] * k;
        if (P.cap_along_pct && remote_moving && speed_remote >= P.moving_mmps) {
            // Along the remote's motion up to clamp(pct |v_remote|, min, max);
            // across it the moving cap as before.
            const double ux = st.v_remote[0] / speed_remote, uz = st.v_remote[1] / speed_remote;
            const double lo = prone ? P.cap_along_prone_min_mmps : P.cap_along_min_mmps;
            const double hi = prone ? P.cap_along_prone_max_mmps : P.cap_along_max_mmps;
            const double cap_along = Clamp(P.cap_along_pct * speed_remote / 100.0, lo, hi) * dtn / 1000.0;
            const double along = Clamp(dx * ux + dz * uz, -cap_along, cap_along);
            double cx = dx - (dx * ux + dz * uz) * ux, cz = dz - (dx * ux + dz * uz) * uz;
            const double clen = Len2(cx, cz);
            if (clen > cap && clen > 0.0) { cx *= cap / clen; cz *= cap / clen; }
            dx = along * ux + cx;
            dz = along * uz + cz;
        } else {
            const double len = Len2(dx, dz);
            if (len > cap && len > 0.0) { dx *= cap / len; dz *= cap / len; }
        }

        const double pos[3] = {in.body[0] + dx, in.body[1], in.body[2] + dz};
        write(Action::nudge, pos, in.body_yaw);
        st.last_nudge_ms = now;
        st.e_f[0] -= dx;
        st.e_f[1] -= dz;
        ++st.nudges;
        st.nudge_mm += Len2(dx, dz);
        d_f = Len2(st.e_f[0], st.e_f[1]);
    }
    return finish(Mode::track);
}

} // namespace mgscoop::partner_recon
