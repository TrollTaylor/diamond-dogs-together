#include "mgscoop/partner_reload.hpp"

namespace mgscoop::partner_reload {

const char* StateName(State state) noexcept {
    switch (state) {
    case State::pending: return "pending";
    case State::entered: return "entered";
    default: return "idle";
    }
}

void Reset(Machine& m) noexcept { m = Machine{}; }

namespace {
Out Fallback(Machine& m, Out o, const char* reason) noexcept {
    m.state = State::idle;
    m.taps_this = 0;
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
    const bool freezing = in.setting != PartnerNativeReload::mirror;
    // Body 1's own reload mode (every setting: telemetry, and the stuck rule).
    const bool body_reload = in.body_ok && ReloadMode(in.body_mode);
    if (body_reload && !m.body_reload) {
        m.body_since_ms = now;
        ++m.body_entries;
        m.stuck_latched = false;
        o.body_event = "body_enter";
    }
    if (!body_reload && m.body_reload && in.body_ok) o.body_event = "body_exit";
    if (!body_reload) m.stuck_latched = false;
    m.body_reload = body_reload;
    if (freezing && body_reload && !m.stuck_latched && now - m.body_since_ms >= p.stuck_ms) {
        m.stuck_latched = true;
        ++m.stuck;
        o.event = "stuck";
    }
    o.stuck = m.stuck_latched;
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
        return o;
    }
    switch (m.state) {
    case State::idle:
        if (start || in.force_tap) {
            const char* why = !in.active         ? "inactive"
                              : !in.weapon_match ? "weapon"
                              : !in.body_ok      ? "body"
                              : !in.gun_class    ? "cls"
                              : !in.gate         ? "gate"
                                                 : nullptr;
            if (why) return Fallback(m, o, why);
            m.since_ms = now;
            if (body_reload) {
                // The raw press (a raw remote, or before the strip) already started it.
                m.state = State::entered;
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
            m.state = State::entered;
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
    case State::entered:
        if (!body_reload) {
            m.state = State::idle;
            m.taps_this = 0;
            ++m.completed;
            o.event = "complete";
        } else if (m.stuck_latched) {
            // Stuck: stop holding; the mirror reconciles (o.stuck).
            m.state = State::idle;
            m.taps_this = 0;
        }
        break;
    }
    o.hold = m.state == State::pending || m.state == State::entered;
    return o;
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
