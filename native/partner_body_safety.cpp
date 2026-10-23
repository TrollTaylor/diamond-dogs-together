// Partner body safety (0.13.6): pure decisions, see partner_body_safety.hpp.
#include "mgscoop/partner_body_safety.hpp"
#include <cmath>

namespace mgscoop::partner_body_safety {
namespace {
bool Finite(double v) noexcept { return std::isfinite(v); }
std::uint32_t Ms(std::uint64_t from, std::uint64_t now) noexcept {
    if (!from || now < from) return 0;
    const std::uint64_t d = now - from;
    return d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(d);
}
} // namespace

bool OnFootAct(std::uint16_t act) noexcept { return act >= 1 && act <= 20; }
bool StuckAct(std::uint16_t act) noexcept { return act == 25 || act == 28 || (act >= 54 && act <= 56); }
bool DropAct(std::uint16_t act) noexcept { return act == 25 || act == 28; }
bool VerticalAct(std::uint16_t act) noexcept { return (act >= 21 && act <= 28) || (act >= 54 && act <= 56); }
bool CarriedOrAsleepAct(std::uint16_t act) noexcept { return (act >= 42 && act <= 44) || act == 131 || act == 132; }
bool ReleasedAct(std::uint16_t act) noexcept { return act == 44 || act == 131 || act == 132 || act == 25 || act == 26; }

const char* StuckVerdictName(StuckVerdict verdict) noexcept {
    switch (verdict) {
    case StuckVerdict::reshow: return "reshow";
    case StuckVerdict::not_capable: return "not_capable";
    case StuckVerdict::rate: return "rate";
    case StuckVerdict::cap: return "cap";
    default: return "none";
    }
}

StuckVerdict StuckStep(StuckState& st, const StuckIn& in) noexcept {
    if (!in.placed_fail || !in.body_anim_ok || !StuckAct(in.body_act) || !in.remote_valid || !OnFootAct(in.remote_act))
        return StuckVerdict::none;
    if (!in.capable) return StuckVerdict::not_capable;
    if (st.count >= kReshowMissionCap) return StuckVerdict::cap;
    if (st.last_ms && in.now_ms >= st.last_ms && in.now_ms - st.last_ms < kReshowGapMs) return StuckVerdict::rate;
    st.last_ms = in.now_ms ? in.now_ms : 1;
    ++st.count;
    return StuckVerdict::reshow;
}

const char* DropVerdictName(DropVerdict verdict) noexcept {
    switch (verdict) {
    case DropVerdict::opened: return "opened";
    case DropVerdict::matched: return "matched";
    case DropVerdict::place: return "place";
    case DropVerdict::level: return "level";
    case DropVerdict::remote_off_foot: return "remote_off_foot";
    case DropVerdict::landed: return "landed";
    case DropVerdict::lost: return "lost";
    case DropVerdict::rate: return "rate";
    default: return "none";
    }
}

DropOut DropStep(DropState& st, const DropIn& in) noexcept {
    DropOut o{};
    const std::uint64_t now = in.now_ms ? in.now_ms : 1;
    if (in.remote_valid && VerticalAct(in.remote_act)) st.remote_vertical_ms = now;
    const bool target = in.target_ok && Finite(in.target_y);
    const auto remember = [&]() noexcept {
        st.have_prev = in.body_anim_ok;
        st.prev_body_act = in.body_anim_ok ? in.body_act : 0;
    };
    if (!in.enabled) {
        st.drop_ms = 0;  // closed silently: nothing could place the body anyway
        remember();
        return o;
    }
    const bool edge = in.body_anim_ok && DropAct(in.body_act) && st.have_prev && OnFootAct(st.prev_body_act);
    if (edge && !st.drop_ms) {
        remember();
        o.act = in.body_act;
        // The remote dropped first (its act arrives with the aligned pose): a real fall.
        if (st.remote_vertical_ms && now - st.remote_vertical_ms <= kDropMatchMs) {
            ++st.matched;
            o.verdict = DropVerdict::matched;
            return o;
        }
        if (!target) {
            o.verdict = DropVerdict::lost;
            return o;
        }
        st.drop_ms = now;
        st.drop_act = in.body_act;
        st.drop_remote_y = in.target_y;
        o.verdict = DropVerdict::opened;
        return o;
    }
    remember();
    if (!st.drop_ms) return o;
    o.act = st.drop_act;
    o.ms = Ms(st.drop_ms, now);
    o.remote_dy = target ? in.target_y - st.drop_remote_y : 0.0;
    // The remote's own vertical act after the body's edge: the body was early.
    if (st.remote_vertical_ms && st.remote_vertical_ms >= st.drop_ms) {
        st.drop_ms = 0;
        ++st.matched;
        o.verdict = DropVerdict::matched;
        return o;
    }
    if (o.ms < kDropMatchMs) return o;  // still waiting for the remote
    st.drop_ms = 0;
    if (!target) o.verdict = DropVerdict::lost;
    else if (!in.remote_valid || !OnFootAct(in.remote_act)) o.verdict = DropVerdict::remote_off_foot;
    else if (std::fabs(o.remote_dy) > kDropLevelMm) o.verdict = DropVerdict::level;
    // Back on its feet at the remote's level (a hop, a short step): nothing to undo.
    else if (!(in.body_anim_ok && StuckAct(in.body_act)) &&
             !(in.body_ok && Finite(in.body_y) && in.target_y - in.body_y > kDropLevelMm)) o.verdict = DropVerdict::landed;
    else if (st.last_place_ms && now - st.last_place_ms < kDropPlaceGapMs) o.verdict = DropVerdict::rate;
    else {
        st.last_place_ms = now;
        ++st.places;
        o.verdict = DropVerdict::place;
        return o;
    }
    ++st.refused;
    return o;
}

PutDownOut PutDownStep(PutDownState& st, const PutDownIn& in) noexcept {
    PutDownOut o{};
    const std::uint64_t now = in.now_ms ? in.now_ms : 1;
    const bool scope = in.remote_valid && CarriedOrAsleepAct(in.remote_act) && in.body_anim_ok && ReleasedAct(in.body_act);
    const bool data = in.body_ok && in.target_ok && Finite(in.body[0]) && Finite(in.body[1]) && Finite(in.body[2]) &&
                      Finite(in.target[0]) && Finite(in.target[1]) && Finite(in.target[2]);
    if (data) {
        o.dy = in.target[1] - in.body[1];
        const double dx = in.target[0] - in.body[0], dz = in.target[2] - in.body[2];
        o.err_xz = std::sqrt(dx * dx + dz * dz);
    }
    const auto end = [&](const char* why) noexcept {
        st.active = false;
        o.ended = true;
        o.end_reason = why;
        o.action = PutDownAction::none;
        return o;
    };
    if (!st.active) {
        if (!scope || !data || o.dy <= kPutDownDropMm) return o;
        st.active = true;
        st.start_ms = now;
        st.hold_since = 0;
        st.places = 1;
        st.last_place_ms = now;
        ++st.episodes;
        o.started = true;
        o.action = PutDownAction::place;
        return o;
    }
    o.ms = Ms(st.start_ms, now);
    if (!scope) return end("scope");
    if (o.ms >= kPutDownMaxMs) return end("timeout");
    if (!data) return o;  // keep the episode; nothing to write without both poses
    if (std::fabs(o.dy) <= kPutDownHoldMm && o.err_xz <= kPutDownHoldXzMm) {
        if (!st.hold_since) st.hold_since = now;
        if (now - st.hold_since >= kPutDownHoldMs) return end("held");
    } else {
        st.hold_since = 0;
        if (o.dy > kPutDownDropMm && st.places < kPutDownPlaces && now - st.last_place_ms >= kPutDownPlaceGapMs) {
            ++st.places;
            st.last_place_ms = now;
            o.action = PutDownAction::place;
            return o;
        }
    }
    ++st.pins;
    o.action = PutDownAction::pin;
    return o;
}

} // namespace mgscoop::partner_body_safety
