#include "mgscoop/partner_reload.hpp"
#include <cmath>

namespace mgscoop::partner_reload {

const char* StateName(State state) noexcept {
    switch (state) {
    case State::pending: return "pending";
    case State::entered: return "entered";
    case State::waiting: return "waiting";
    default: return "idle";
    }
}

void Reset(Machine& m) noexcept { m = Machine{}; }

namespace {
Out Fallback(Machine& m, Out o, const char* reason) noexcept {
    m.state = State::idle;
    o.hold = false;
    m.taps_this = 0;
    m.queued = false;
    m.queued_reconcile = false;
    ++m.fallbacks;
    m.last_reason = reason;
    o.event = "fallback";
    o.reason = reason;
    return o;
}
} // namespace

Out Step(Machine& m, const In& in, const Params& p) noexcept {
    Out o{};
    const std::uint64_t now = in.now_ms;
    // Body 1's own reload mode (every setting: telemetry, and the stuck report).
    // An unread tick keeps the last state: only readable ticks enter or exit.
    const bool body_reload = in.body_ok ? ReloadMode(in.body_mode) : m.body_reload;
    bool body_exit = false;
    if (body_reload && !m.body_reload) {
        m.learn_pending = false;  // a body length still waiting for the remote's exit is stale now
        m.body_since_ms = now;
        ++m.body_entries;
        m.stuck_latched = false;
        m.refill_seen = false;
        m.enter_mag_ok = in.body_mag_ok;
        m.enter_mag = in.body_mag;
        o.body_event = "body_enter";
    }
    if (!body_reload && m.body_reload) {
        body_exit = true;
        o.body_event = "body_exit";
        o.len_ms = now - m.body_since_ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(now - m.body_since_ms);
        m.len_ms = o.len_ms;
        if (o.len_ms > m.len_max_ms) m.len_max_ms = o.len_ms;
        // Rate learning: the remote's reload this one followed (its start within
        // pair_ms of body 1's enter); still running: wait for its exit.
        const std::uint64_t enter = m.body_since_ms;
        const auto near = [&](std::uint64_t at) noexcept {
            return at && (at >= enter ? at - enter : enter - at) <= p.pair_ms;
        };
        if (o.len_ms >= p.learn_min_ms && near(m.remote_since_ms)) {
            if (m.remote_in_reload) {
                m.learn_pending = true;
                m.learn_body_ms = o.len_ms;
                m.learn_enter_ms = enter;
            } else if (m.remote_len_ms >= p.learn_min_ms && m.remote_exit_ms >= m.remote_since_ms) {
                o.learn = true;
                o.learn_body_ms = o.len_ms;
                o.learn_remote_ms = m.remote_len_ms;
                ++m.learned;
            }
        }
    }
    // refill_ms: the first magazine change body 1 makes itself inside its reload
    // (the mirror writes nothing while body 1 is in a reload mode).
    if (body_reload && !m.refill_seen && in.body_mag_ok) {
        if (!m.enter_mag_ok) {
            m.enter_mag_ok = true;
            m.enter_mag = in.body_mag;
        } else if (in.body_mag != m.enter_mag) {
            m.refill_seen = true;
            m.refill_ms = now - m.body_since_ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(now - m.body_since_ms);
        }
    }
    if (!body_reload) m.stuck_latched = m.released = false;
    m.body_reload = body_reload;
    const bool freezing = in.setting != PartnerNativeReload::mirror;
    if (freezing && body_reload && !m.stuck_latched && now - m.body_since_ms >= p.stuck_ms) {
        m.stuck_latched = true;
        ++m.stuck;
        o.event = "stuck";
    }
    o.stuck = m.stuck_latched;
    // A lost exit (body 1 unread or wedged in 11/12 past release_ms): released,
    // so neither the hold nor the hook's body-mode freeze outlives it.
    bool release_now = false;
    if (freezing && body_reload && !m.released && now - m.body_since_ms >= p.release_ms) {
        m.released = true;
        release_now = true;
        ++m.releases;
        o.event = "release";
    }
    o.release = m.released;
    // The remote's reload span (its mode 11/12; telemetry).
    const bool remote_in = in.remote_valid && ReloadMode(in.remote_mode);
    if (remote_in && !m.remote_in_reload) m.remote_since_ms = now ? now : 1;
    if (!remote_in && m.remote_in_reload && in.remote_valid) {
        m.remote_len_ms = now - m.remote_since_ms > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(now - m.remote_since_ms);
        m.remote_exit_ms = now;
        // Rate learning: body 1 finished first; this is the reload it followed.
        if (m.learn_pending) {
            const std::uint64_t e = m.learn_enter_ms, r = m.remote_since_ms;
            if ((r >= e ? r - e : e - r) <= p.pair_ms && m.remote_len_ms >= p.learn_min_ms) {
                o.learn = true;
                o.learn_body_ms = m.learn_body_ms;
                o.learn_remote_ms = m.remote_len_ms;
                ++m.learned;
            }
            m.learn_pending = false;
        }
    }
    if (in.remote_valid) m.remote_in_reload = remote_in;
    const bool remote_reloading = in.remote_valid && (in.remote_reloading || ReloadMode(in.remote_mode));
    // The remote's reload start: its counter moved while its mode is a reload mode.
    bool start = false;
    if (in.remote_valid) {
        start = m.have_seq && in.remote_seq != m.last_seq && (ReloadMode(in.remote_mode) || in.remote_reloading);
        m.have_seq = true;
        m.last_seq = in.remote_seq;
    } else {
        m.have_seq = false;
    }
    if (start) {
        ++m.starts;
        if (!o.event) o.event = "start";
    }
    if (in.setting != PartnerNativeReload::event) {
        m.state = State::idle;
        m.taps_this = 0;
        m.queued = false;
        m.queued_reconcile = false;
        return o;
    }
    switch (m.state) {
    case State::idle:
        if (start || in.force_tap) {
            // Anim round 1: body 1 is still switching to the remote's weapon (or the
            // gate's mismatch is a moment old): queue the tap until the gate passes.
            if (in.active && !in.weapon_match && in.weapon_wait && start) {
                m.state = State::waiting;
                m.since_ms = now;
                m.queued = false;
                ++m.waits;
                o.event = "queued_weapon";
                break;
            }
            const char* why = !in.active         ? "inactive"
                              : !in.weapon_match ? "weapon"
                              : !in.body_ok      ? "body"
                              : !in.gun_class    ? "cls"
                              : !in.gate         ? "gate"
                                                 : nullptr;
            if (why) return Fallback(m, o, why);
            m.since_ms = now;
            if (body_reload) {
                // A released (lost) body reload is never entered again.
                if (m.released) return Fallback(m, o, "released");
                // The raw press (a raw remote, or before the strip) already started it.
                m.state = State::entered;
                m.queued = false;
                ++m.entered;
                o.event = "entered";
                break;
            }
            if (!in.can_reload) return Fallback(m, o, "no_reload");
            m.state = State::pending;
            m.tap_ms = now;
            m.taps_this = 1;
            ++m.taps;
            o.tap = true;
            o.event = "tap";
        }
        break;
    case State::pending:
        if (body_reload) {
            if (m.released) return Fallback(m, o, "released");
            m.state = State::entered;
            m.queued = false;
            m.queued_reconcile = false;
            m.since_ms = now;
            ++m.entered;
            o.event = "entered";
            break;
        }
        if (!in.active) return Fallback(m, o, "inactive");
        if (now - m.tap_ms >= p.verify_ms) {
            if (m.taps_this >= p.max_taps) return Fallback(m, o, "timeout");
            if (in.remote_valid && !in.remote_reloading && !ReloadMode(in.remote_mode)) return Fallback(m, o, "remote_done");
            ++m.taps_this;
            ++m.taps;
            m.tap_ms = now;
            o.tap = true;
            o.event = "retry";
        }
        break;
    case State::waiting:
        // The raw press (or an earlier tap) already started body 1's reload.
        if (body_reload) {
            if (m.released) return Fallback(m, o, "released");
            m.state = State::entered;
            m.since_ms = now;
            ++m.entered;
            o.event = "entered";
            break;
        }
        if (!in.active) return Fallback(m, o, "inactive");
        if (in.remote_valid && !in.remote_reloading && !ReloadMode(in.remote_mode)) return Fallback(m, o, "remote_done");
        if (in.weapon_match) {
            const char* why = !in.body_ok ? "body" : !in.gun_class ? "cls" : !in.gate ? "gate" : !in.can_reload ? "no_reload" : nullptr;
            if (why) return Fallback(m, o, why);
            m.state = State::pending;
            m.since_ms = now;
            m.tap_ms = now;
            m.taps_this = 1;
            ++m.taps;
            ++m.replays;
            o.tap = true;
            o.event = "replay";
            break;
        }
        if (!in.weapon_wait) return Fallback(m, o, "weapon");
        if (now - m.since_ms >= p.wait_ms) return Fallback(m, o, "weapon_timeout");
        break;
    case State::entered:
        // The exit never came within release_ms: leave entered (no completion counted).
        if (release_now || m.released) {
            m.state = State::idle;
            m.taps_this = 0;
            m.queued = false;
            break;
        }
        // A remote reload started while body 1 still reloads: queued for its exit.
        if (start) m.queued = true;
        if (body_exit) {
            // Every native exit counts, also after a stuck report (complete_late).
            const bool late = m.len_ms >= p.stuck_ms;
            ++m.completed;
            if (late) ++m.completed_late;
            o.event = late ? "complete_late" : "complete";
            m.state = State::idle;
            m.taps_this = 0;
            // The remote is still in that next reload: one tap now (body 1 can
            // only start it after its own exit). Body 1's own completion just
            // filled its magazine (CanReload needs it below the capacity), so the
            // mirror reconciles to the remote's current magazine first, in the
            // same drain window before the job, although the remote reloads.
            const bool again = m.queued && remote_reloading && in.active && in.weapon_match && in.gun_class && in.gate;
            m.queued = false;
            if (again) {
                m.state = State::pending;
                m.queued_reconcile = true;
                m.since_ms = now;
                m.tap_ms = now;
                m.taps_this = 1;
                ++m.taps;
                ++m.queued_taps;
                o.tap = true;
                o.queued_tap = true;
            }
        }
        // A stuck latch keeps entered (and the hold): the mirror waits for the native exit.
        break;
    }
    o.reconcile = m.state == State::pending && m.queued_reconcile;
    o.hold = (m.state == State::pending && !m.queued_reconcile) || m.state == State::entered || m.state == State::waiting;
    return o;
}

const char* RateVerdictName(RateVerdict verdict) noexcept {
    switch (verdict) {
    case RateVerdict::learned: return "learned";
    case RateVerdict::short_span: return "short";
    case RateVerdict::ratio: return "ratio";
    case RateVerdict::saturated: return "saturated";
    default: return "latched";
    }
}

RateVerdict RateLearn(RateAdapt& a, std::uint32_t body_ms, std::uint32_t remote_ms, const RateParams& p) noexcept {
    if (a.latched) return RateVerdict::latched;
    if (body_ms < p.min_ms || remote_ms < p.min_ms) { ++a.ignored; return RateVerdict::short_span; }
    const float ratio = static_cast<float>(body_ms) / static_cast<float>(remote_ms);
    a.last_ratio = ratio;
    if (!(ratio >= p.ratio_min && ratio <= p.ratio_max)) { ++a.ignored; return RateVerdict::ratio; }
    ++a.samples;
    const float want = a.k * std::pow(ratio, p.gain);
    const float k = want < p.k_min ? p.k_min : want > p.k_max ? p.k_max : want;
    const bool parity = ratio <= p.ratio_ok && ratio >= 1.0f / p.ratio_ok;
    // Already pinned at the bound the ratio still pushes against: the write has
    // no effect (or the pairing is wrong); after saturated_max such reloads stop
    // adapting for the mission (k = 1, the native rate).
    if (!parity && k != want && k == a.k) {
        if (++a.saturated >= p.saturated_max) {
            a.latched = true;
            a.k = 1.0f;
            return RateVerdict::latched;
        }
        return RateVerdict::saturated;
    }
    a.saturated = 0;
    a.k = k;
    return RateVerdict::learned;
}

float RateTarget(float native_rate, float k) noexcept {
    if (!RateNativeOk(native_rate) || !(k == k) || k <= 0.0f) return native_rate;
    const float t = native_rate * k;
    return t < 0.25f ? 0.25f : t > 4.0f ? 4.0f : t;
}

void SenderStep(SenderState& s, bool valid, std::uint8_t mode, std::uint8_t slot) noexcept {
    if (!valid) {
        s.have = false;
        return;
    }
    if (s.have) {
        if (ReloadMode(mode) && !ReloadMode(s.mode)) s.reload_seq = static_cast<std::uint8_t>(s.reload_seq + 1);
        if (slot != s.slot && slot <= 15 && s.slot <= 15) s.switch_seq = static_cast<std::uint8_t>(s.switch_seq + 1);
    }
    s.have = true;
    s.mode = mode;
    s.slot = slot;
}

} // namespace mgscoop::partner_reload
