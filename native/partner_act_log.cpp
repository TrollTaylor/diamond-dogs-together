// Original MGS Co-op act census telemetry (pure, portable, deterministic; log
// only). docs/anim-round1-actions.md "Act census".
#include "mgscoop/partner_act_log.hpp"
#include <cstdarg>
#include <cstdio>

namespace mgscoop::partner_act_log {
namespace {
std::uint32_t Ms(std::uint64_t from, std::uint64_t now) noexcept {
    if (now < from) return 0;
    const std::uint64_t d = now - from;
    return d > 0x7FFFFFFFull ? 0x7FFFFFFFu : static_cast<std::uint32_t>(d);
}
std::size_t Print(char* out, std::size_t capacity, const char* format, ...) noexcept {
    if (!out || !capacity) return 0;
    va_list arguments;
    va_start(arguments, format);
    const int n = std::vsnprintf(out, capacity, format, arguments);
    va_end(arguments);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return 0;
    }
    return static_cast<std::size_t>(n);
}
unsigned PopCount(std::uint32_t v) noexcept {
    unsigned n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}
// PlayerPad (partner_pad_bits.hpp): movement and gun bits are passed by the
// native masks and never counted as "other".
constexpr std::uint32_t kMovement = 0x2u | 0x4u | 0x40000u | 0x200000u | 0x2000000u;
constexpr std::uint32_t kGun = 0x8u | 0x40u;
constexpr std::uint32_t kBits[kGroups] = {0x20u, 0x80u, 0x200u, 0x400u, 0x7800u, 0x8000u, 0x20000u, 0x80000u, 0x400000u, 0x10u, 0u};
constexpr std::uint32_t kNamed = 0x20u | 0x80u | 0x200u | 0x400u | 0x7800u | 0x8000u | 0x20000u | 0x80000u | 0x400000u | 0x10u;
} // namespace

// Names without '?' are confirmed: by play, or by the static writer census of
// Q+0x18[id] on 1.0.15.4 (owners named by Ghidra): 21 LadderActionImpl, 22
// PipeActionPluginImpl, 23 ClimbActionPluginImpl (wall / cliff free-climb), 24
// StepOnActionImpl (low vault, onto a ledge, up out of a hang), 28
// CliffActionImpl (edge drop), 75/76 Cure, 79..83 TrashBoxActionPluginImpl.
const char* ActName(std::uint16_t act) noexcept {
    switch (act) {
    case 0: return "none";
    case 1: return "stand_idle";
    case 2: return "stand_move";
    case 3: return "crouch_idle";
    case 4: return "crouch_move";
    case 5: return "prone_idle";
    case 6: return "prone_crawl";
    case 8: return "stand_run_step?";
    case 9: return "supine_idle?";
    case 11: return "aim_stand_idle";
    case 12: return "aim_stand_turn";
    case 13: return "aim_stand_move";
    case 14: return "aim_crouch_idle";
    case 15: return "aim_crouch_turn";
    case 16: return "aim_crouch_move?";
    case 17: case 19: case 20: return "aim_prone?";
    case 21: return "ladder";
    case 22: return "pipe";
    case 23: return "wall_climb";
    case 24: return "step_on";
    case 25: return "fall";
    case 26: return "land";
    case 27: return "evade";
    case 28: return "cliff_drop";
    case 29: return "hit?";
    case 39: return "carry_body";
    case 40: return "dead";
    case 41: return "dying?";
    case 42: return "carried_start";
    case 43: return "carried";
    case 44: return "carried_putdown";
    case 45: return "damage?";
    case 49: case 50: return "interact?";
    case 51: case 52: case 53: return "fulton_catch?";
    case 54: case 55: case 56: return "ledge_hang?";
    case 57: return "dive";
    case 58: return "evade_quiet";
    case 59: return "interact_held?";
    case 75: case 76: return "cure";
    case 77: case 78: return "cbox?";
    case 97: return "heli_board?";
    case 109: return "vehicle_in";
    case 110: return "vehicle_dismount?";
    case 111: return "vehicle_seated";
    case 112: return "vehicle_out";
    case 121: return "vehicle_passenger?";
    case 122: return "horse?";
    case 131: return "sleep_onset";
    case 132: return "sleep";
    case 133: return "sleep_wake";
    case 139: return "dm";
    case 142: return "dive_variant?";
    default: break;
    }
    if (act >= 67 && act <= 74) return "cover?";
    if (act >= 79 && act <= 83) return "trash_box";
    if (act >= 91 && act <= 96) return "cqc_hold?";
    if (act >= 98 && act <= 108) return "heli";
    if (act >= 123 && act <= 126) return "walker?";
    return "?";
}

const char* EdgeName(Edge edge) noexcept {
    switch (edge) {
    case Edge::remote: return "remote";
    case Edge::body: return "body";
    default: return "miss";
    }
}

void Reset(ActState& s) noexcept { s = ActState{}; }

ActOut Step(ActState& s, const ActIn& in, std::uint32_t cap) noexcept {
    ActOut o{};
    const std::uint64_t now = in.now_ms;
    Line pending_lines[3]{};
    unsigned n = 0;
    const auto line = [&](Edge edge, std::uint16_t remote, std::int32_t match, std::uint32_t held) noexcept {
        Line& l = pending_lines[n++];
        l.edge = edge;
        l.t_ms = in.tick_ms;
        l.remote_valid = edge == Edge::miss ? true : in.remote_valid;
        l.remote = remote;
        l.body_valid = in.body_valid;
        l.body = in.body_act;
        l.match_ms = match;
        l.held_ms = held;
    };
    const auto miss = [&]() noexcept {
        line(Edge::miss, s.remote, kMatchMiss, Ms(s.remote_since, now));
        s.pending = false;
        ++s.missed;
        ++s.w_missed;
    };
    // Remote edge (a valid act that differs from the last one seen).
    if (in.remote_valid && (!s.have_remote || in.remote_act != s.remote)) {
        if (s.have_remote && s.pending && n < 3) miss();
        s.have_remote = true;
        s.remote = in.remote_act;
        s.remote_since = now;
        const bool matched = in.body_valid && in.body_act == in.remote_act;
        s.pending = !matched;
        ++s.remote_edges;
        ++s.w_remote;
        line(Edge::remote, in.remote_act, matched ? 0 : kMatchPending, 0);
    }
    // Body edge.
    if (in.body_valid && (!s.have_body || in.body_act != s.body)) {
        s.have_body = true;
        s.body = in.body_act;
        ++s.body_edges;
        ++s.w_body;
        std::int32_t match = kMatchNone;
        if (s.have_remote && s.pending && in.body_act == s.remote) {
            match = static_cast<std::int32_t>(Ms(s.remote_since, now));
            s.pending = false;
            ++s.matched;
            ++s.w_matched;
        }
        // The very first body observation is a line too (the census starts there).
        if (n < 3) line(Edge::body, s.have_remote ? s.remote : in.remote_act, match, 0);
    }
    // A remote act held unmatched for kMatchTimeoutMs.
    if (s.have_remote && s.pending && Ms(s.remote_since, now) >= kMatchTimeoutMs && n < 3) miss();
    // The cap: lines beyond it are dropped (counted), a summary every
    // kSummaryMs while something happened.
    for (unsigned i = 0; i < n; ++i) {
        if (s.lines < cap) {
            o.lines[o.count++] = pending_lines[i];
            ++s.lines;
        } else {
            if (!s.capped) {
                s.capped = true;
                o.capped_now = true;
                s.window_ms = now;
            }
            ++s.dropped;
            ++s.w_dropped;
        }
    }
    if (!s.capped) {
        s.w_remote = s.w_body = s.w_matched = s.w_missed = s.w_dropped = 0;
    } else if (now - s.window_ms >= kSummaryMs || now < s.window_ms) {
        if (s.w_remote || s.w_body || s.w_matched || s.w_missed || s.w_dropped) {
            o.summary = true;
            o.s_remote = s.w_remote; o.s_body = s.w_body; o.s_matched = s.w_matched; o.s_missed = s.w_missed; o.s_dropped = s.w_dropped;
        }
        s.w_remote = s.w_body = s.w_matched = s.w_missed = s.w_dropped = 0;
        s.window_ms = now;
    }
    return o;
}

std::size_t FormatLine(const Line& l, char* out, std::size_t capacity) noexcept {
    char match[16]{};
    if (l.match_ms >= 0) Print(match, sizeof(match), "%ld", static_cast<long>(l.match_ms));
    else Print(match, sizeof(match), "%s", l.match_ms == kMatchPending ? "pending" : l.match_ms == kMatchMiss ? "miss" : "-");
    char remote[40]{}, body[40]{};
    if (l.remote_valid) Print(remote, sizeof(remote), "%u(%s)", unsigned(l.remote), ActName(l.remote));
    else Print(remote, sizeof(remote), "na");
    if (l.body_valid) Print(body, sizeof(body), "%u(%s)", unsigned(l.body), ActName(l.body));
    else Print(body, sizeof(body), "na");
    if (l.edge == Edge::miss)
        return Print(out, capacity, "partner_act; t=%llu; edge=%s; remote=%s; body=%s; match_ms=%s; held_ms=%u\r\n",
                     static_cast<unsigned long long>(l.t_ms), EdgeName(l.edge), remote, body, match, l.held_ms);
    return Print(out, capacity, "partner_act; t=%llu; edge=%s; remote=%s; body=%s; match_ms=%s\r\n", static_cast<unsigned long long>(l.t_ms),
                 EdgeName(l.edge), remote, body, match);
}

std::size_t FormatSummary(const ActOut& o, char* text, std::size_t capacity) noexcept {
    return Print(text, capacity, "partner_act=summary; remote_edges=%u; body_edges=%u; matched=%u; missed=%u; dropped=%u\r\n", o.s_remote,
                 o.s_body, o.s_matched, o.s_missed, o.s_dropped);
}

const char* GroupName(unsigned group) noexcept {
    static const char* const kNames[kGroups] = {"action", "stock", "call", "subject", "weapon", "cqc", "light", "vfire", "marker", "fire",
                                                "other"};
    return group < kGroups ? kNames[group] : "other";
}

std::uint32_t GroupBits(unsigned group) noexcept { return group < kGroups ? kBits[group] : 0u; }

void Reset(PadState& s) noexcept { s = PadState{}; }

std::uint32_t NewPresses(PadState& s, const PadIn& in) noexcept {
    if (!in.valid) return 0;
    if (s.have && in.seq == s.seq) return 0;
    std::uint32_t presses = 0;
    if (in.version >= 2) {
        if (in.edge_seq && (!s.have || in.edge_seq != s.last_edge_seq)) presses = in.press;
        if (in.edge_seq) s.last_edge_seq = in.edge_seq;
    } else if (s.have) {
        presses = in.held & ~s.prev_held;
    }
    s.have = true;
    s.seq = in.seq;
    s.prev_held = in.held;
    return presses;
}

PadOut PadStep(PadState& s, const PadIn& in, std::uint32_t cap) noexcept {
    PadOut o{};
    const std::uint64_t now = in.now_ms;
    if (!s.window_ms || now < s.window_ms) s.window_ms = now ? now : 1;
    const std::uint32_t presses = NewPresses(s, in);
    if (presses) {
        s.presses += PopCount(presses);
        const std::uint32_t dropped = presses & ~in.mask;
        for (unsigned g = 0; g < kOther; ++g)
            if (dropped & kBits[g]) ++s.counts[g];
        const std::uint32_t other = dropped & ~(kNamed | kMovement | kGun);
        if (other) {
            ++s.counts[kOther];
            s.other_bits |= other;
        }
        s.mask = in.mask;
        s.native = in.native;
    }
    if (now - s.window_ms >= kPadWindowMs) {
        bool any = false;
        for (unsigned g = 0; g < kGroups; ++g) any = any || s.counts[g] != 0;
        if (any) {
            if (s.lines < cap) {
                ++s.lines;
                o.line = true;
                for (unsigned g = 0; g < kGroups; ++g) o.counts[g] = s.counts[g];
                o.other_bits = s.other_bits;
                o.mask = s.mask;
                o.native = s.native;
            } else if (!s.capped) {
                s.capped = true;
                o.capped_now = true;
            }
        }
        for (unsigned g = 0; g < kGroups; ++g) s.counts[g] = 0;
        s.other_bits = 0;
        s.window_ms = now ? now : 1;
    }
    return o;
}

std::size_t FormatPad(const PadOut& o, char* text, std::size_t capacity) noexcept {
    return Print(text, capacity,
                 "partner_pad_dropped; action=%u; stock=%u; call=%u; subject=%u; weapon=%u; cqc=%u; light=%u; vfire=%u; marker=%u; fire=%u; "
                 "other=%u; other_bits=0x%x; mask=0x%x; native=%u\r\n",
                 o.counts[kAction], o.counts[kStock], o.counts[kCall], o.counts[kSubject], o.counts[kWeapon], o.counts[kCqc], o.counts[kLight],
                 o.counts[kVehicleFire], o.counts[kMarker], o.counts[kFire], o.counts[kOther], o.other_bits, o.mask, o.native ? 1u : 0u);
}

} // namespace mgscoop::partner_act_log
