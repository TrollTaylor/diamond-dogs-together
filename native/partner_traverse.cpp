#include "mgscoop/partner_traverse.hpp"

#include <cmath>
#include <cstring>

namespace mgscoop::partner_traverse {
namespace {
constexpr double kPi = 3.14159265358979323846;
double Wrap(double r) noexcept {
    if (!std::isfinite(r)) return 0.0;
    r = std::fmod(r + kPi, 2.0 * kPi);
    if (r < 0.0) r += 2.0 * kPi;
    return r - kPi;
}
// After body 1 finished its traversal, a remote that left every traversal act
// this long ago releases the hold (body 1 stuck on a ladder the remote left).
constexpr std::uint32_t kRemoteLeftMs = 3000;
}  // namespace

const char* PhaseName(Phase phase) noexcept {
    switch (phase) {
    case Phase::place: return "place";
    case Phase::tap: return "tap";
    case Phase::native: return "native";
    default: return "idle";
    }
}

const char* EventName(Out::Event event) noexcept {
    switch (event) {
    case Out::Event::start: return "start";
    case Out::Event::placed: return "placed";
    case Out::Event::tapped: return "tap";
    case Out::Event::entered: return "entered";
    case Out::Event::done: return "done";
    case Out::Event::abort: return "abort";
    default: return "none";
    }
}

Out Step(State& s, const In& in, const Params& P) noexcept {
    Out o{};
    o.event = Out::Event::none;
    o.reason = nullptr;
    const std::uint64_t now = in.now_ms;
    const bool remote_edge = in.remote_ok && (!s.have_prev || in.remote_act != s.remote_prev);
    if (in.remote_ok) {
        s.remote_prev = in.remote_act;
        s.have_prev = true;
        if (TraverseAct(in.remote_act)) {
            s.remote_trav_ms = now;
            s.hold_off = false;
        }
    }
    if (in.body_ok && !TraverseAct(in.body_act)) s.hold_off = false;
    const auto go = [&](Phase p) noexcept {
        s.phase = p;
        s.phase_ms = now;
    };
    const auto event = [&](Out::Event e, const char* why) noexcept {
        if (o.event == Out::Event::none) {
            o.event = e;
            o.reason = why;
        }
    };
    const auto stop = [&](bool aborted, const char* why) noexcept {
        if (aborted) ++s.aborted;
        event(aborted ? Out::Event::abort : Out::Event::done, why);
        s.taps = 0;
        s.left_ms = 0;
        s.hold_off = !aborted && why && std::strcmp(why, "remote_left") == 0;
        go(Phase::idle);
    };
    if (!in.enabled || in.blocked) {
        if (s.phase != Phase::idle) stop(true, !in.enabled ? "disabled" : "blocked");
        return o;
    }
    const bool body_traverse = in.body_ok && TraverseAct(in.body_act);
    // A replay starts on the remote's edge into an ACTION-started traversal
    // that body 1 does not show, from idle or from a native hang (climb up).
    const bool start = remote_edge && ReplayAct(in.remote_act) && in.body_ok && SourceAct(in.remote_act, in.body_act) &&
                       in.body_act != in.remote_act && in.target_ok &&
                       (s.phase == Phase::idle || (s.phase == Phase::native && !ReplayAct(in.body_act)));
    if (start) {
        const double dx = in.target[0] - in.body[0], dy = in.target[1] - in.body[1], dz = in.target[2] - in.body[2];
        const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const double yaw = std::fabs(Wrap(in.target_yaw - in.body_yaw));
        if (std::isfinite(d) && d <= P.far_mm) {
            ++s.episodes;
            s.act = in.remote_act;
            s.from = in.body_act;
            s.taps = 0;
            s.left_ms = 0;
            s.saw_placing = false;
            if (d <= P.near_mm && yaw <= P.near_yaw_rad) {
                go(Phase::tap);
                event(Out::Event::start, "near");
            } else {
                go(Phase::place);
                o.request_place = true;
                event(Out::Event::start, "placed");
            }
        }
    } else if (s.phase == Phase::idle && body_traverse && !s.hold_off) {
        // Body 1 runs a traversal natively (hop, ledge grab): hold the placements.
        s.act = in.body_act;
        s.left_ms = 0;
        go(Phase::native);
        event(Out::Event::entered, "native");
    }
    // A hang (54) is already a traversal. The tap has landed only when body 1
    // leaves the act it had when this episode started. Computed after `start`,
    // which records that act.
    const bool episode_entered = body_traverse && in.body_act != s.from;
    switch (s.phase) {
    case Phase::place:
        if (in.placing) s.saw_placing = true;
        if (episode_entered) {
            ++s.entered;
            s.left_ms = 0;
            go(Phase::native);
            event(Out::Event::entered, "place");
        } else if (s.saw_placing && !in.placing && in.armed) {
            go(Phase::tap);
            event(Out::Event::placed, nullptr);
        } else if (now - s.phase_ms >= P.place_ms) {
            stop(true, "place_timeout");
        }
        break;
    case Phase::native:
        if (body_traverse) {
            s.left_ms = 0;
        } else {
            if (!s.left_ms) s.left_ms = now ? now : 1;
            if (now - s.left_ms >= P.grace_ms) stop(false, "left");
        }
        if (s.phase == Phase::native && body_traverse && in.remote_ok && !TraverseAct(in.remote_act)) {
            // Measured from the phase start or the remote's last traversal.
            const std::uint64_t since = s.remote_trav_ms > s.phase_ms ? s.remote_trav_ms : s.phase_ms;
            if (now - since >= kRemoteLeftMs) stop(false, "remote_left");
        }
        if (s.phase == Phase::native && now - s.phase_ms >= P.native_max_ms) stop(false, "max");
        break;
    default:
        break;
    }
    // A tap only from a source act of the episode's own replay act (s.act, set by `start`).
    const bool can_tap = in.armed && !in.placing && in.body_ok && SourceAct(s.act, in.body_act);
    if (s.phase == Phase::tap) {
        if (episode_entered) {
            ++s.entered;
            s.left_ms = 0;
            s.tap_ms = 0;
            go(Phase::native);
            event(Out::Event::entered, "tap");
        } else if (now - s.phase_ms >= P.entry_ms) {
            s.tap_ms = 0;
            stop(true, "no_entry");
        } else if (can_tap && s.taps < P.taps &&
                   (s.taps == 0 || (now - s.tap_ms >= P.retap_ms && in.remote_ok && TraverseAct(in.remote_act)))) {
            o.tap = true;
            ++s.taps;
            s.tap_ms = now ? now : 1;
            event(Out::Event::tapped, nullptr);
        }
    }
    o.hold = s.phase != Phase::idle;
    return o;
}

} // namespace mgscoop::partner_traverse
