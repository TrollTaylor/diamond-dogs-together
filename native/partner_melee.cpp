// Melee on the partner: the attacker-side reach test (partner_melee.hpp). Pure: no game access.
#include "mgscoop/partner_melee.hpp"
#include <cmath>
#include <cstdio>

namespace mgscoop::partner_melee {

bool StrikeAct(std::uint16_t act) noexcept { return act == kStrikeAct || act == kFinisherAct; }
// Ladder / pipe (21..24), ledge (54..56), trash box and toilet (79..89), CQC hold (91..96): a strike in reach is aimed
// at what the partner holds or hides in, or cannot reach them (fb1031 review, melee-soldier-adjacent-false-positive).
bool PartnerBusyAct(std::uint16_t a) noexcept {
    return (a >= 21 && a <= 24) || (a >= 54 && a <= 56) || (a >= 79 && a <= 89) || (a >= 91 && a <= 96);
}

const char* VerdictName(Verdict v) noexcept {
    switch (v) {
    case Verdict::none: return "none";
    case Verdict::land: return "land";
    case Verdict::off: return "off";
    case Verdict::no_local: return "no_local";
    case Verdict::no_body: return "no_body";
    case Verdict::range: return "range";
    case Verdict::height: return "height";
    case Verdict::angle: return "angle";
    case Verdict::repeat: return "repeat";
    case Verdict::takedown: return "takedown";
    case Verdict::busy: return "busy";
    case Verdict::prone: return "prone";
    case Verdict::hold: return "hold";
    case Verdict::target: return "target";
    }
    return "none";
}

void Reset(State& s) noexcept { s = State{}; }


namespace {
std::int32_t Cm(double metres) noexcept {
    if (!std::isfinite(metres)) return 0;
    const double cm = metres * 100.0;
    if (cm > 2000000000.0) return 2000000000;
    if (cm < -2000000000.0) return -2000000000;
    return static_cast<std::int32_t>(std::lround(cm));
}
bool Finite(const float (&v)[3]) noexcept { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
std::int32_t Ms(std::uint64_t now, std::uint64_t then) noexcept {
    if (now < then) return 0;
    const std::uint64_t d = now - then;
    return d > 2000000000u ? 2000000000 : static_cast<std::int32_t>(d);
}
// The held strike's line fields (its edge's act and geometry, the decision timings).
Out HeldOut(const State& s, std::uint64_t now) noexcept {
    Out o{};
    o.act = s.held_act;
    o.dist_cm = s.held_dist_cm;
    o.dy_cm = s.held_dy_cm;
    o.angle_deg = s.held_angle_deg;
    o.hold_ms = Ms(now, s.held_ms);
    o.cqc_ms = s.cqc_seen ? Ms(s.cqc_ms, s.press_ms) : -1;
    o.end_ms = s.held_ended ? Ms(now, s.held_end_ms) : -1;
    return o;
}
void Block(State& s, std::uint32_t press) noexcept {
    s.blocked = true;
    s.blocked_press = press;
    ++s.targets;
}
} // namespace

// melee-target: decides the held strike. The stock CQC acting on a target (CQC_CONTINUOUS) since the press's 113 edge is
// verdict=target; otherwise the strike lands kTargetGraceMs after the press's 113 ended (its 114 edge), at kHoldCapMs,
// or when a new press starts (the held press's strike is over).
Out Resolve(State& s, const In& in) noexcept {
    const bool new_press = in.local_valid && in.act == kStrikeAct && s.last_act != kStrikeAct;
    // The current press's stock target signal (Step resets it at the next 113 edge).
    if (in.local_valid && !new_press && s.presses && in.status_valid && in.cqc && !s.cqc_seen) {
        s.cqc_seen = true;
        s.cqc_ms = in.now_ms;
    }
    if (!s.held) return Out{};
    if (in.local_valid && !new_press && !s.held_ended && in.act != kStrikeAct) {
        s.held_ended = true;
        s.held_end_ms = in.now_ms;
    }
    Out o = HeldOut(s, in.now_ms);
    if (!in.local_valid) { s.held = false; o.verdict = Verdict::no_local; return o; }
    if (!in.enabled) { s.held = false; o.verdict = Verdict::off; return o; }
    if (s.cqc_seen) {
        s.held = false;
        Block(s, s.held_press);
        o.verdict = Verdict::target;
        return o;
    }
    const bool graced = s.held_ended && in.now_ms >= s.held_end_ms && in.now_ms - s.held_end_ms >= kTargetGraceMs;
    const bool capped = in.now_ms >= s.held_ms && in.now_ms - s.held_ms >= kHoldCapMs;
    if (!new_press && !graced && !capped) return Out{};
    s.held = false;
    s.landed = true;
    s.last_land_ms = in.now_ms;
    s.landed_press = s.held_press;
    ++s.lands;
    o.verdict = Verdict::land;
    o.queue = true;
    return o;
}

Out Step(State& s, const In& in) noexcept {
    Out o{};
    o.act = in.act;
    if (!in.local_valid) { s.last_act = 0; return o; }
    const bool edge = StrikeAct(in.act) && in.act != s.last_act;
    s.last_act = in.act;
    if (in.act == kStrikeAct) { s.jab_seen = true; s.last_jab_ms = in.now_ms; }
    if (!edge) return o;
    if (in.act == kStrikeAct) {
        ++s.presses;  // each press starts with its own 113 edge
        // melee-target: the press's stock target signal starts with its 113 edge (a bit still on is counted).
        s.press_ms = in.now_ms;
        s.cqc_seen = in.status_valid && in.cqc;
        s.cqc_ms = in.now_ms;
    }
    if (!in.enabled) { o.verdict = Verdict::off; return o; }
    // The press's 114 comes straight out of its 113; a 114 from any other act starts a soldier takedown.
    const bool finisher = in.act == kFinisherAct;
    if (finisher && !(s.jab_seen && in.now_ms >= s.last_jab_ms && in.now_ms - s.last_jab_ms <= kComboMs)) {
        o.verdict = Verdict::takedown;
        return o;
    }
    ++s.strikes;
    if (!in.body_valid || !Finite(in.pos1)) { o.verdict = Verdict::no_body; return o; }
    if (!Finite(in.pos0) || !std::isfinite(in.yaw0)) { o.verdict = Verdict::no_local; return o; }
    const double dx = static_cast<double>(in.pos1[0]) - in.pos0[0];
    const double dy = static_cast<double>(in.pos1[1]) - in.pos0[1];
    const double dz = static_cast<double>(in.pos1[2]) - in.pos0[2];
    const double dist = std::sqrt(dx * dx + dz * dz);
    o.dist_cm = Cm(dist);
    o.dy_cm = Cm(dy);
    // Body facing: forward = (sin yaw, cos yaw) in (x, z), as partner_aim / the driver's heading (atan2(dx, dz)).
    const double fx = std::sin(static_cast<double>(in.yaw0)), fz = std::cos(static_cast<double>(in.yaw0));
    const double cosine = dist > 1e-6 ? (dx * fx + dz * fz) / dist : 1.0;
    const double clamped = cosine > 1.0 ? 1.0 : (cosine < -1.0 ? -1.0 : cosine);
    o.angle_deg = static_cast<std::int32_t>(std::lround(std::acos(clamped) * 57.29577951308232));
    if (dist > kReachM) { o.verdict = Verdict::range; return o; }
    if (std::fabs(dy) > kHeightM) { o.verdict = Verdict::height; return o; }
    if (dist > 1e-6 && cosine < kConeCos) { o.verdict = Verdict::angle; return o; }
    if (in.remote_valid && PartnerBusyAct(in.remote_act)) { o.verdict = Verdict::busy; return o; }
    if (in.remote_valid && in.remote_stance == 3) { o.verdict = Verdict::prone; return o; }
    // One connecting press knocks the partner down (the user's decision, as it does a soldier). Run 1526e58a: ONE press
    // with the partner 1.1 m in front gave 113 (219 ms) then 114 (549 ms), so the press's 113 and the 114 out of it are
    // one strike: whichever reaches first lands, the other is no second hit. At most one hit per kRepeatMs.
    if (s.landed && s.landed_press == s.presses) { o.verdict = Verdict::repeat; return o; }
    if (s.landed && in.now_ms >= s.last_land_ms && in.now_ms - s.last_land_ms < kRepeatMs) { o.verdict = Verdict::repeat; return o; }
    // melee-target: what the strike is aimed at. A press the stock CQC aimed at another target stays no hit; a press in
    // reach waits for that signal (Resolve) when the st= census is known.
    if (s.blocked && s.blocked_press == s.presses) { o.verdict = Verdict::target; return o; }
    if (s.held && s.held_press == s.presses) { o.verdict = Verdict::hold; return o; }
    if (in.status_valid) {
        o.cqc_ms = s.cqc_seen ? Ms(s.cqc_ms, s.press_ms) : -1;
        if (s.cqc_seen) {
            Block(s, s.presses);
            o.verdict = Verdict::target;
            return o;
        }
        s.held = true;
        s.held_press = s.presses;
        s.held_act = in.act;
        s.held_ms = in.now_ms;
        s.held_ended = in.act != kStrikeAct;
        s.held_end_ms = in.now_ms;
        s.held_dist_cm = o.dist_cm;
        s.held_dy_cm = o.dy_cm;
        s.held_angle_deg = o.angle_deg;
        o.verdict = Verdict::hold;
        return o;
    }
    s.landed = true;
    s.last_land_ms = in.now_ms;
    s.landed_press = s.presses;
    ++s.lands;
    o.verdict = Verdict::land;
    o.queue = true;
    return o;
}

std::size_t FormatStrike(const State& s, std::uint16_t act, const Out& o, bool queued, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return 0;
    const int n = std::snprintf(out, capacity,
                                "partner_melee=strike; act=%u; verdict=%s; queued=%u; dist_cm=%d; dy_cm=%d; angle_deg=%d; strikes=%u; lands=%u; "
                                "hold_ms=%d; cqc_ms=%d; end_ms=%d; targets=%u\r\n",
                                unsigned(act), VerdictName(o.verdict), queued ? 1u : 0u, o.dist_cm, o.dy_cm, o.angle_deg, s.strikes, s.lands,
                                o.hold_ms, o.cqc_ms, o.end_ms, s.targets);
    if (n <= 0 || static_cast<std::size_t>(n) >= capacity) { out[0] = 0; return 0; }
    return static_cast<std::size_t>(n);
}

} // namespace mgscoop::partner_melee
