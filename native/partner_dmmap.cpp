// Original MGS Co-op dm=map: the partner's direct-motion clip follows the
// remote stance and speed (pure, portable, deterministic).
// docs/partner-anim-appearance.md "D-map".
#include "mgscoop/partner_dmmap.hpp"
#include <cmath>
#include <cstring>

namespace mgscoop::partner_dmmap {
namespace {
constexpr const char* kUnconsumed = "unconsumed";
constexpr const char* kStopTimeout = "stop_timeout";
constexpr const char* kDmRead = "dm_read";

std::uint32_t Ms(std::uint64_t from, std::uint64_t now) noexcept {
    if (now < from) return 0;
    const std::uint64_t d = now - from;
    return d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(d);
}
bool Same(const char* a, const char* b) noexcept { return a && b && std::strcmp(a, b) == 0; }
std::uint8_t Band(std::uint64_t mmps, const PartnerDmSpeeds& s) noexcept {
    return mmps < s.idle_mmps ? 0 : mmps < s.run_mmps ? 1 : mmps < s.dash_mmps ? 2 : 3;
}
bool Usable(std::uint8_t gani, std::uint16_t bad) noexcept { return gani <= kPartnerDmGaniMax && !(bad & (1u << gani)); }

// One strike against a gani; the kStrikes-th makes it bad (reported once).
void Strike(State& s, Out& o, std::uint8_t gani) noexcept {
    if (gani > kPartnerDmGaniMax) return;
    if (s.strikes[gani] < 0xFF) ++s.strikes[gani];
    if (s.strikes[gani] >= kStrikes && !(s.bad & (1u << gani))) {
        s.bad = static_cast<std::uint16_t>(s.bad | (1u << gani));
        ++s.bad_ganis;
        o.bad_gani = gani;
    }
}
// A failed request (unconsumed play, stop not confirmed, DM state unreadable):
// a doubling backoff, failed after kMaxFailures in a row.
void Failure(State& s, Out& o, const char* kind, std::uint64_t now) noexcept {
    ++s.failures;
    s.last_failure = kind;
    const unsigned shift = s.failures - 1 < 3 ? s.failures - 1 : 3;
    const std::uint32_t backoff = kBackoffMs << shift;
    s.backoff_ms = backoff < kBackoffMaxMs ? backoff : kBackoffMaxMs;
    if (Same(kind, kUnconsumed)) ++s.unconsumed;
    else ++s.timeouts;
    o.reason = kind;
    if (s.failures >= kMaxFailures) {
        s.phase = Phase::failed;
        o.event = Event::failed;
        return;
    }
    s.phase = Phase::backoff;
    s.retry_ms = now + s.backoff_ms;
    o.event = Same(kind, kUnconsumed) ? Event::unconsumed : Same(kind, kStopTimeout) ? Event::stop_timeout : Event::dm_read;
}
// Change-only deferral (no action either way; a success clears it).
void Defer(State& s, Out& o, const char* reason) noexcept {
    if (Same(s.deferred, reason)) return;
    s.deferred = reason;
    ++s.deferrals;
    o.event = Event::deferred;
    o.reason = reason;
}
// A clip is already active on instance 1 (a manual dmplay, a late start):
// it becomes ours, unknown, and is replaced after the hold.
void Adopt(State& s, Out& o) noexcept {
    s.phase = Phase::playing;
    s.owned = true;
    s.cur = kNoGani;
    s.play_ms = 0;
    s.deferred = nullptr;
    ++s.adopted;
    o.event = Event::adopted;
    o.reason = "active_clip";
}
// DM End clears g+0x388 0x3C globally and writes the camera fields for any
// id: a stop waits for the host's DM, any pending request and a camera connect.
const char* StopGate(const In& in) noexcept {
    if (!in.patched) return "not_patched";
    if (!in.dm_ok) return "dm_read";
    if (in.host_dm) return "host_dm";
    if (in.pending) return "pending";
    if (!in.cam_idle) return "cam_connect";
    return nullptr;
}
// The play went on: q18[1] is in the DM act (R1, and map on again during a
// release that began in play_sent).
void Confirm(State& s, Out& o, std::uint64_t now) noexcept {
    s.phase = Phase::playing;
    s.cur = s.sent;
    s.play_ms = now;
    s.failures = 0;
    s.backoff_ms = 0;
    o.event = Event::playing;
    o.gani = s.cur;
    o.ms = Ms(s.sent_ms, now);
}
void SendStop(State& s, Out& o, std::uint8_t to, std::uint32_t ms, std::uint64_t now) noexcept {
    o.action = Action::stop;
    o.event = Event::step;
    o.step_stop = true;
    o.from = s.cur;
    o.gani = to;
    o.ms = ms;
    s.phase = Phase::stop_sent;
    s.from = s.cur;
    s.stop_ms = now;
    ++s.stops;
    ++s.switches;
    s.deferred = nullptr;
}
} // namespace

const char* ViaName(Via via) noexcept {
    switch (via) {
    case Via::flags: return "flags";
    case Via::speed: return "speed";
    case Via::anim: return "anim";
    case Via::lost: return "lost";
    case Via::stale: return "stale";
    default: return "none";
    }
}

const char* PhaseName(Phase phase) noexcept {
    switch (phase) {
    case Phase::wait: return "wait";
    case Phase::play_sent: return "play_sent";
    case Phase::playing: return "playing";
    case Phase::stop_sent: return "stop_sent";
    case Phase::backoff: return "backoff";
    case Phase::failed: return "failed";
    case Phase::release: return "release";
    default: return "idle";
    }
}

const char* BlockName(Block block) noexcept {
    switch (block) {
    case Block::terminating: return "terminating";
    case Block::fault: return "fault";
    case Block::stage: return "stage";
    case Block::apply: return "apply";
    case Block::anim: return "anim";
    case Block::input: return "input";
    default: return "none";
    }
}

const char* CauseName(Cause cause) noexcept {
    switch (cause) {
    case Cause::ended: return "ended";
    case Cause::warp: return "warp";
    case Cause::look: return "look";
    case Cause::parts: return "parts";
    case Cause::host: return "host";
    default: return "none";
    }
}

std::uint8_t MoveForSpeed(std::uint32_t speed_mmps, const PartnerDmSpeeds& speeds, std::uint8_t prev_move) noexcept {
    const std::uint8_t base = Band(speed_mmps, speeds);
    if (prev_move > 3) return base;
    const std::uint8_t hi = Band(speed_mmps > kSpeedHystMmps ? speed_mmps - kSpeedHystMmps : 0, speeds);
    const std::uint8_t lo = Band(static_cast<std::uint64_t>(speed_mmps) + kSpeedHystMmps, speeds);
    if (hi > prev_move) return hi;
    if (lo < prev_move) return lo;
    return prev_move;
}

Class Classify(const Observation& o, const PartnerDmSpeeds& speeds, std::uint8_t prev_move, std::uint8_t prev_stance) noexcept {
    Class c{};
    if (o.lost) {
        c.stance = prev_stance <= 2 ? prev_stance : 0;
        c.move = 0;
        c.via = Via::lost;
        c.stance_via = Via::flags;
        return c;
    }
    c.stance = o.stance <= 2 ? o.stance : 0;
    c.stance_via = Via::flags;
    // The anim stance is 1 stand, 2 squat, 3 crawl (SetStance's encoding).
    if (c.stance == 0 && o.anim_valid && (o.anim_stance == 2 || o.anim_stance == 3)) {
        c.stance = static_cast<std::uint8_t>(o.anim_stance - 1);
        c.stance_via = Via::anim;
    }
    if (o.sprint) {
        c.move = 3;
        c.via = Via::flags;
    } else if (o.flag_move <= 2) {
        c.move = o.flag_move;
        c.via = Via::flags;
    } else if (o.speed_ok) {
        c.move = MoveForSpeed(o.speed_mmps, speeds, prev_move);
        c.via = Via::speed;
    } else if (o.anim_move && o.anim_valid && o.act >= 1 && o.act <= 6) {
        // Locomotion acts: odd = idle (stand 1, squat 3, prone 5), even = moving.
        c.move = (o.act & 1u) ? 0 : (o.anim_speed == 3 ? 2 : 1);
        c.via = Via::anim;
    } else {
        c.move = 0;
        c.via = Via::none;
    }
    return c;
}

std::uint8_t ResolveTarget(const PartnerDmTable& table, const Class& c, std::uint16_t bad) noexcept {
    if (c.stance > 2 || c.move > 3) return kNoGani;
    const std::uint8_t gani = table.cells[Cell(c)];
    if (Usable(gani, bad)) return gani;
    const std::uint8_t idle = table.cells[c.stance * 4u];
    if (Usable(idle, bad)) return idle;
    return kNoGani;
}

void SpeedReset(SpeedRing& ring) noexcept {
    ring.head = 0;
    ring.count = 0;
}

void SpeedPush(SpeedRing& ring, std::uint64_t t_ms, std::int32_t x_mm, std::int32_t z_mm) noexcept {
    if (ring.count) {
        SpeedRing::Entry& newest = ring.entries[ring.head];
        if (t_ms < newest.t_ms) {
            SpeedReset(ring);
        } else if (t_ms == newest.t_ms) {
            newest.x_mm = x_mm;
            newest.z_mm = z_mm;
            return;
        } else {
            const double dx = static_cast<double>(x_mm) - newest.x_mm, dz = static_cast<double>(z_mm) - newest.z_mm;
            const double mmps = std::sqrt(dx * dx + dz * dz) * 1000.0 / static_cast<double>(t_ms - newest.t_ms);
            if (mmps > kSpeedJumpMmps) SpeedReset(ring);
        }
    }
    ring.head = ring.count ? static_cast<std::uint8_t>((ring.head + 1) % kSpeedRing) : 0;
    ring.entries[ring.head] = SpeedRing::Entry{t_ms, x_mm, z_mm};
    if (ring.count < kSpeedRing) ++ring.count;
}

bool SpeedMmps(const SpeedRing& ring, std::uint32_t& out) noexcept {
    if (ring.count < 2) return false;
    const SpeedRing::Entry& newest = ring.entries[ring.head];
    const SpeedRing::Entry* oldest = nullptr;
    for (std::size_t i = 1; i < ring.count; ++i) {
        const SpeedRing::Entry& e = ring.entries[(ring.head + kSpeedRing - i) % kSpeedRing];
        if (newest.t_ms - e.t_ms > kSpeedSpanMaxMs) break;
        oldest = &e;
    }
    if (!oldest || newest.t_ms - oldest->t_ms < kSpeedSpanMinMs) return false;
    const double dx = static_cast<double>(newest.x_mm) - oldest->x_mm, dz = static_cast<double>(newest.z_mm) - oldest->z_mm;
    const double mmps = std::sqrt(dx * dx + dz * dz) * 1000.0 / static_cast<double>(newest.t_ms - oldest->t_ms);
    out = mmps >= 4294967295.0 ? 0xFFFFFFFFu : static_cast<std::uint32_t>(std::llround(mmps));
    return true;
}

void Reset(State& s) noexcept {
    s = State{};
    s.phase = Phase::idle;
    s.cur = s.sent = s.from = s.cand = kNoGani;
}

void Rearm(State& s) noexcept {
    s.failures = 0;
    s.backoff_ms = 0;
    s.deferred = nullptr;
    s.bad = 0;
    std::memset(s.strikes, 0, sizeof(s.strikes));
    if (s.phase == Phase::failed) {
        if (s.owned) {
            s.phase = Phase::playing;
            s.play_ms = 0;
        } else {
            s.phase = Phase::wait;
        }
    } else if (s.phase == Phase::backoff) {
        s.retry_ms = 0;
    }
}

void Forget(State& s) noexcept {
    s.owned = false;
    s.cur = kNoGani;
    s.stop_done = false;
    s.release_stop_sent = false;
    // failed stays latched until Rearm and a backoff runs out (R6): a realize
    // cycle or a patch flicker skips neither.
    if (s.phase != Phase::idle && s.phase != Phase::failed && s.phase != Phase::backoff) s.phase = Phase::wait;
}

void Idle(State& s) noexcept {
    Forget(s);
    s.phase = Phase::idle;
    s.cand = s.sent = s.from = kNoGani;
    s.cand_ms = 0;
    s.deferred = nullptr;
}

Out Step(State& s, const In& in) noexcept {
    Out o{};
    o.gani = kNoGani;
    o.from = kNoGani;
    o.bad_gani = kNoGani;
    const std::uint64_t now = in.now_ms;
    // R: reconcile against q18[1].
    switch (s.phase) {
    case Phase::play_sent:
        if (in.dm_ok && in.in_dm) {  // R1
            Confirm(s, o, now);
            return o;
        }
        if (Ms(s.sent_ms, now) >= kConfirmMs && (!in.dm_ok || !in.our_pending)) {  // R2
            s.owned = false;
            o.gani = s.sent;
            o.ms = Ms(s.sent_ms, now);
            if (in.dm_ok) Strike(s, o, s.sent);
            Failure(s, o, in.dm_ok ? kUnconsumed : kDmRead, now);
            return o;
        }
        break;
    case Phase::stop_sent:
        if (in.dm_ok && !in.in_dm && !in.pending) {  // R3 (no event; the play may follow below)
            s.cur = kNoGani;
            s.owned = false;
            s.stop_done = true;
            s.phase = Phase::wait;
        } else if (Ms(s.stop_ms, now) >= kStopWaitMs) {  // R4 (the clip stays ours)
            o.gani = s.cur;
            o.ms = Ms(s.stop_ms, now);
            Failure(s, o, in.dm_ok ? kStopTimeout : kDmRead, now);
            return o;
        }
        break;
    case Phase::playing:
        if (in.dm_ok && !in.in_dm) {  // R5
            const std::uint8_t old = s.cur;
            const std::uint32_t played = s.play_ms ? Ms(s.play_ms, now) : 0;
            o.cause = !in.parts_loaded ? Cause::parts
                    : in.prev_look     ? Cause::look
                    : in.prev_warp     ? Cause::warp
                    : in.host_dm       ? Cause::host
                                       : Cause::ended;
            const bool short_loop = o.cause == Cause::ended && old != kNoGani && s.play_ms && played < kLoopMinMs;
            s.owned = false;
            s.cur = kNoGani;
            s.phase = Phase::wait;
            ++s.lost;
            if (short_loop) Strike(s, o, old);
            o.event = Event::lost;
            o.gani = old;
            o.ms = played;
            return o;
        }
        break;
    case Phase::backoff:
        if (now >= s.retry_ms) {  // R6 (no event; adopted only while patched, as in D3)
            if (in.dm_ok && in.in_dm && in.patched) {
                s.phase = Phase::playing;
                s.owned = true;
                s.play_ms = 0;
                // A play that was declared unconsumed and started late is that clip.
                if (Same(s.last_failure, kUnconsumed)) s.cur = s.sent;
            } else {
                s.phase = Phase::wait;
                s.owned = false;
                s.cur = kNoGani;
            }
        }
        break;
    case Phase::failed:  // R7
        if (in.dm_ok && !in.in_dm) s.owned = false;
        if (in.map) return o;
        break;
    default:
        break;
    }
    // L: leave (map off): release our clip.
    if (!in.map) {
        if (s.phase == Phase::idle) return o;  // L0
        if (!s.owned) {                         // L1
            Idle(s);
            return o;
        }
        if (s.phase != Phase::release) {  // L2
            s.release_from = s.phase;
            if (s.phase == Phase::stop_sent) {
                s.release_stop_sent = true;
                s.release_ms = s.stop_ms;
            } else {
                s.release_stop_sent = false;
                s.release_ms = now;
            }
            s.phase = Phase::release;
        }
        // L3
        if (in.dm_ok && !in.in_dm && !in.pending && !in.our_pending) {
            o.event = Event::released;
            o.reason = s.release_stop_sent ? "stopped" : "ended";
            o.gani = s.cur;
            o.ms = Ms(s.release_ms, now);
            Idle(s);
            return o;
        }
        if (!s.release_stop_sent && in.in_dm && !StopGate(in)) {
            o.action = Action::stop;
            s.release_stop_sent = true;
            s.stop_ms = now;
            ++s.stops;
            return o;
        }
        // A stop the gate held back late in the release still gets kStopWaitMs:
        // the patch is never reverted while our stop may still be pending.
        std::uint64_t deadline = s.release_ms + kReleaseMs;
        if (s.release_stop_sent && s.stop_ms + kStopWaitMs > deadline) deadline = s.stop_ms + kStopWaitMs;
        if (now >= deadline) {
            o.event = Event::released;
            o.reason = "timeout";
            o.gani = s.cur;
            o.ms = Ms(s.release_ms, now);
            Idle(s);
            return o;
        }
        return o;
    }
    // map is on again during a release: the clip is still ours. A release
    // that began in play_sent keeps that play (R1/R2 still apply to it).
    if (s.phase == Phase::release) {
        const bool from_play = s.release_from == Phase::play_sent;
        const bool stop_sent = s.release_stop_sent;
        s.release_stop_sent = false;
        if (stop_sent) {
            if (from_play) s.cur = s.sent;  // the stop only went out once the play had started
            s.phase = Phase::stop_sent;
            s.from = s.cur;
        } else if (from_play && in.dm_ok && in.in_dm) {
            Confirm(s, o, now);
            return o;
        } else if (from_play) {
            s.phase = Phase::play_sent;
        } else {
            s.phase = Phase::playing;
            s.play_ms = 0;
        }
    }
    // B: blocked.
    if (in.blocked != Block::none) {
        if (in.blocked == Block::terminating || in.blocked == Block::fault) {
            Defer(s, o, BlockName(in.blocked));
            return o;
        }
        if (s.phase == Phase::playing) {
            if (const char* why = StopGate(in)) {
                Defer(s, o, why);
                return o;
            }
            SendStop(s, o, kNoGani, 0, now);
            return o;
        }
        Defer(s, o, BlockName(in.blocked));
        return o;
    }
    // D: decide. D1 the candidate and its hold.
    if (in.target_ok && in.target != s.cand) {
        s.cand = in.target;
        s.cand_cls = in.cls;
        s.cand_ms = now;
    } else if (in.target_ok) {
        s.cand_cls = in.cls;
    }
    const bool stable = in.target_ok && Ms(s.cand_ms, now) >= kHoldMs;
    if (s.phase == Phase::idle) {  // D2
        if (in.dm_ok && in.in_dm) {
            Adopt(s, o);
            return o;
        }
        s.phase = Phase::wait;
    }
    if (s.phase == Phase::wait) {  // D3
        if (!stable) return o;
        if (s.cand == kNoGani) {
            Defer(s, o, "bad_gani");
            return o;
        }
        const char* why = !in.patched ? "not_patched" : !in.parts_loaded ? "parts" : !in.dm_ok ? "dm_read" : in.host_dm ? "host_dm" : nullptr;
        if (!why && in.in_dm) {
            Adopt(s, o);
            return o;
        }
        if (!why) {
            why = (in.pending || in.our_pending) ? "pending"
                : !in.cam_idle                  ? "cam_connect"
                : in.look_busy                  ? "look"
                : in.allactive                  ? "allactive"
                : in.warp                       ? "warp"
                                                : nullptr;
        }
        if (why) {
            Defer(s, o, why);
            return o;
        }
        o.action = Action::play;
        o.event = Event::step;
        o.step_stop = false;
        o.gani = s.cand;
        s.phase = Phase::play_sent;
        s.sent = s.cand;
        s.sent_ms = now;
        s.owned = true;
        ++s.plays;
        if (!s.stop_done) {
            ++s.switches;
            s.from = s.cur;
        }
        o.from = s.stop_done ? s.from : kNoGani;
        o.ms = s.stop_done ? Ms(s.stop_ms, now) : 0;
        s.stop_done = false;
        s.deferred = nullptr;
        return o;
    }
    if (s.phase == Phase::playing) {  // D4
        if (!in.target_ok || s.cand == s.cur) {
            s.deferred = nullptr;
            return o;
        }
        if (!stable || (s.play_ms && Ms(s.play_ms, now) < kDwellMs)) return o;
        if (const char* why = StopGate(in)) {
            Defer(s, o, why);
            return o;
        }
        SendStop(s, o, s.cand, Ms(s.cand_ms, now), now);
        return o;
    }
    return o;  // D5: play_sent, stop_sent, backoff wait for R
}

} // namespace mgscoop::partner_dmmap
