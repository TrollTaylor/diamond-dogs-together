// Original MGS Co-op DirectMotion overlay lever (pure, portable,
// deterministic). docs/anim-round1-actions.md "DM overlay".
#include "mgscoop/partner_dm_overlay.hpp"
#include <cstring>

namespace mgscoop::partner_dm_overlay {
namespace {
constexpr std::uint16_t kDmAct = 0x8B;  // q18 while a DM owns the body (139)
constexpr char kHostDm[] = "host_dm";
constexpr char kDebugStop[] = "debug";
constexpr std::uint64_t kStateDirectPlay = 0x81582ffa24b9ull;
#define MGS_SNAP "/Assets/tpp/motion/SI_game/fani/bodies/snap/"
// Graph-0 states: StrCode64 present once in TppPlayer2_layers.mog and absent
// from the SQ-4 graph 2/3 node list (scratchpad overlay_mogcheck.py, recorded
// in docs/anim-round1-actions.md). Clip paths: player2_resident.mtar
// PathCode64 (verified-clips.tsv). The order is the reply's index and
// partner_sync.lua OVERLAY's: append only.
constexpr Entry kEntries[] = {
    {"stateDamageBigFront", "stateDamageBigFront", 0x3db7bdedaea7ull, false, false},
    {"stateDamageBigBack", "stateDamageBigBack", 0xad926744b0e3ull, false, false},
    {"stateDamageBigLeft", "stateDamageBigLeft", 0x1888402f2d17ull, false, false},
    {"stateDamageBigRight", "stateDamageBigRight", 0x074254c360e9ull, false, false},
    {"stateDead", "stateDead", 0xdad5759b0203ull, false, false},
    {"stateCBoxIdle", "stateCBoxIdle", 0x039c85aa4598ull, false, true},
    {"stateCBoxMove", "stateCBoxMove", 0x6e06f122e3b3ull, false, true},
    {"stateEludeIdle", "stateEludeIdle", 0x1cf4b93bd4fcull, false, true},
    {"stateClimbIdle", "stateClimbIdle", 0x2928469590b4ull, false, true},
    {"stateClimbMoveFront", "stateClimbMoveFront", 0x489a8f233d63ull, false, true},
    {"stateClimbMoveBack", "stateClimbMoveBack", 0x66d0af83390eull, false, true},
    {"stateClimbIdleR", "stateClimbIdleR", 0x04cf99dc27d5ull, false, true},
    // DM-proven controls (partner_sync.lua GANI 9 / 8).
    {"stateStandIdleLeft", "stateStandIdleLeft", 0x904c34eb558aull, false, true},
    {"stateSquatIdleLeft", "stateSquatIdleLeft", 0xf0cde06b01dbull, false, true},
    {"stateCqcDeath", "stateCqcDeath", 0xe7e552499013ull, false, false},
    {"statePutDown", "statePutDown", 0x20b90885c4c1ull, false, false},
    {"stateFultonStart", "stateFultonStart", 0x75d780810388ull, false, false},
    {"stateLedgeDrop", "stateLedgeDrop", 0x6f4869f627ceull, false, false},
    {"stateCrawlSupineIdle", "stateCrawlSupineIdle", 0x61ffe4f8b02bull, false, true},
    // Resident clips (stateDirectPlay).
    {"snapdam_s_fre", MGS_SNAP "snapdam/snapdam_s_fre.gani", 0xfc53062c44565522ull, true, false},
    {"snapdam_p_die_idl", MGS_SNAP "snapdam/snapdam_p_die_idl.gani", 0xfc5046c89dad210cull, true, true},
    {"snapnon_s_fal_lp_l", MGS_SNAP "snapnon/snapnon_s_fal_lp_l.gani", 0xfc50fb7461e1772cull, true, true},
    // Mount research probes (docs/mounts.md: does a rider state play on body 1
    // without a mount?). Each StrCode64 is present once in the retail
    // TppPlayer2_layers.mog node pool. stateRideOnIdle and stateGallop are the
    // rider's graph-0 nodes (RideHorse plugin 0x141236940 plays them through
    // 0x141230B30(ctl, idx, 0, node)); stateRideWalkerWalk has no code
    // reference (the walker drive node 0x23B9A991BFB has no known name).
    // No map entry uses them: debug overlay_state only.
    {"stateRideOnIdle", "stateRideOnIdle", 0xf241f3bcf60bull, false, true},
    {"stateGallop", "stateGallop", 0xce7e8409eb61ull, false, true},
    {"stateRideWalkerWalk", "stateRideWalkerWalk", 0x1b8b642289d5ull, false, true},
};
#undef MGS_SNAP
constexpr std::uint8_t kCount = static_cast<std::uint8_t>(sizeof(kEntries) / sizeof(kEntries[0]));
static_assert(kCount <= kMaxEntries, "the reply value carries a 5-bit index");
constexpr std::uint8_t kFront = 0, kDead = 4, kBoxIdle = 5, kBoxMove = 6, kElude = 7, kClimbIdle = 8, kClimbUp = 9, kClimbDown = 10;

std::uint32_t Ms(std::uint64_t from, std::uint64_t now) noexcept {
    if (now < from) return 0;
    const std::uint64_t d = now - from;
    return d > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<std::uint32_t>(d);
}
bool CamIdle(const In& in) noexcept { return in.dm_ok && in.cam_ok && in.cam1f0 == 0 && in.cam200 == 0; }
} // namespace

std::uint8_t Count() noexcept { return kCount; }

const Entry* At(std::uint8_t index) noexcept { return index < kCount ? &kEntries[index] : nullptr; }

std::uint8_t Find(const char* name, std::size_t size) noexcept {
    if (!name || !size) return kNone;
    for (std::uint8_t i = 0; i < kCount; ++i)
        if (std::strlen(kEntries[i].name) == size && std::memcmp(kEntries[i].name, name, size) == 0) return i;
    return kNone;
}

std::uint8_t ReplyCode(std::uint8_t index, bool loop) noexcept {
    if (index >= kCount) return 0;
    return static_cast<std::uint8_t>(kReplyBase | (loop ? 32u : 0u) | index);
}

bool DecodeReply(std::uint8_t code, std::uint8_t& index, bool& loop) noexcept {
    if (code < kReplyBase || code >= kReplyBase + 64u) return false;
    const std::uint8_t i = static_cast<std::uint8_t>(code & 31u);
    if (i >= kCount) return false;
    index = i;
    loop = (code & 32u) != 0;
    return true;
}

std::uint64_t ExpectedRec18(std::uint8_t index) noexcept {
    if (index >= kCount) return 0;
    return kEntries[index].path ? kStateDirectPlay : kEntries[index].code;
}

std::uint8_t MapAct(std::uint16_t act, std::int32_t vy_mmps) noexcept {
    switch (act) {
    case 29: case 75: case 76: return kFront;
    case 40: case 41: return kDead;
    case 77: return kBoxIdle;
    case 78: return kBoxMove;
    case 54: case 55: case 56: return kElude;
    case 21: case 23: return vy_mmps > kClimbMmps ? kClimbUp : vy_mmps < -kClimbMmps ? kClimbDown : kClimbIdle;
    default: return kNone;
    }
}

const char* PhaseName(Phase phase) noexcept {
    switch (phase) {
    case Phase::suspend: return "suspend";
    case Phase::patch: return "patch";
    case Phase::play_sent: return "play_sent";
    case Phase::playing: return "playing";
    case Phase::stop_sent: return "stop_sent";
    default: return "idle";
    }
}

const char* SourceName(Source source) noexcept {
    switch (source) {
    case Source::debug: return "debug";
    case Source::map: return "map";
    default: return "none";
    }
}

void Reset(State& s) noexcept {
    s = State{};
    s.index = kNone;
    s.map_cand = kNone;
    s.done_index = kNone;
    s.next_index = kNone;
}

Out Step(State& s, const In& in, const Params& p) noexcept {
    Out o{};
    const std::uint64_t now = in.now_ms;
    const auto event = [&](const char* what, const char* why, std::uint32_t ms) noexcept {
        if (o.event) return;  // one telemetry event per step
        o.event = what;
        o.reason = why;
        o.ms = ms;
    };
    // The remote's vertical speed (ladder): an EMA over the sampled y.
    if (in.remote_valid) {
        if (s.have_y && now > s.prev_y_ms && now - s.prev_y_ms <= 500) {
            const double dt = static_cast<double>(now - s.prev_y_ms);
            const double v = static_cast<double>(in.remote_y_mm - s.prev_y) * 1000.0 / dt;
            s.vy += (v - s.vy) * (dt >= 150.0 ? 1.0 : dt / 150.0);
        } else {
            s.vy = 0.0;
        }
        s.have_y = true;
        s.prev_y = in.remote_y_mm;
        s.prev_y_ms = now;
    } else {
        s.have_y = false;
        s.vy = 0.0;
    }
    // The map candidate (mode map) and its holds; a new remote episode clears
    // the finished one-shot entry.
    const bool map_mode = in.mode == PartnerNativeOverlay::map;
    const std::uint8_t want = map_mode && in.remote_valid ? MapAct(in.remote_act, static_cast<std::int32_t>(s.vy)) : kNone;
    if (want != s.map_cand) {
        s.map_cand = want;
        s.map_cand_ms = now;
    }
    if (want == kNone) {
        if (!s.unmapped_ms) s.unmapped_ms = now ? now : 1;
    } else {
        s.unmapped_ms = 0;
    }
    const bool want_held = want != kNone && now - s.map_cand_ms >= p.map_hold_ms;
    const bool unmapped_long = s.unmapped_ms && now - s.unmapped_ms >= p.map_exit_ms;
    if (unmapped_long || (want != kNone && want != s.done_index)) s.done_index = kNone;

    const auto begin = [&](Source source, std::uint8_t index, bool loop, std::uint32_t ms) noexcept {
        s.phase = Phase::suspend;
        s.source = source;
        s.index = index;
        s.loop = loop;
        s.since_ms = s.start_ms = now;
        s.deadline_ms = now + ms;
        s.stop_tries = 0;
        s.replays = 0;
        s.stop_reason = nullptr;
        s.deferred = nullptr;
        s.conflict_logged = false;
        s.switching = false;
        s.next_index = kNone;
        ++s.requests;
    };
    // Back to idle: the arbiter re-enters on the next tick. This tick the DM
    // stays the overlay's (no dm=map play before the native drive is back).
    const auto finish = [&](const char* what, const char* why) noexcept {
        event(what, why, Ms(s.start_ms, now));
        // A map overlay never restarts for the same remote episode (no
        // suspend/resume thrash): the remote must leave or change the act.
        if (s.source == Source::map) s.done_index = s.index;
        s.phase = Phase::idle;
        s.cooldown_until = now + p.cooldown_ms;
        s.stop_reason = nullptr;
        s.switching = false;
        s.next_index = kNone;
        o.suspend = o.release_map = false;
        o.owns_dm = true;
    };
    const auto map_entry = [&](std::uint8_t index) noexcept {
        const Entry* entry = At(index);
        return entry && entry->loop;
    };
    const auto map_ms = [&](std::uint8_t index) noexcept { return map_entry(index) ? p.map_max_ms : p.once_max_ms; };

    // Debug commands.
    if (in.debug.stop) {
        if (s.phase == Phase::idle) {
            ++s.refusals;
            event("refused", "not_active", 0);
        } else if (!s.stop_reason) {
            s.stop_reason = kDebugStop;
            s.switching = false;
        }
    }
    if (in.debug.start) {
        const char* refusal = in.mode == PartnerNativeOverlay::off ? "setting_off"
                            : !At(in.debug.index)                  ? "unknown_state"
                            : s.phase != Phase::idle               ? "busy"
                            : in.blocked                            ? in.blocked
                                                                    : nullptr;
        if (refusal) {
            ++s.refusals;
            event("refused", refusal, 0);
            o.index = in.debug.index;
            o.loop = in.debug.loop;
            o.source = Source::debug;
            return o;
        }
        std::uint32_t ms = in.debug.ms ? in.debug.ms : p.debug_ms;
        if (ms > p.max_ms) ms = p.max_ms;
        begin(Source::debug, in.debug.index, in.debug.loop, ms);
        event("request", "debug", ms);
    }
    // A map start: idle, map mode, a held candidate the body does not show natively.
    if (s.phase == Phase::idle && map_mode && want_held && want != s.done_index && now >= s.cooldown_until && !in.blocked &&
        !(in.body_valid && in.body_act == in.remote_act)) {
        begin(Source::map, want, map_entry(want), map_ms(want));
        event("request", "map", map_ms(want));
    }
    if (in.mode == PartnerNativeOverlay::off && s.phase != Phase::idle && !s.stop_reason) s.stop_reason = "setting_off";
    // The map source: before playing, a new candidate retargets; the remote
    // leaving the mapped acts cancels (a one-shot already playing runs out).
    if (s.source == Source::map && s.phase != Phase::idle && !s.stop_reason) {
        const bool before_play = s.phase == Phase::suspend || s.phase == Phase::patch;
        if (want != kNone && want != s.index && before_play) {
            s.index = want;
            s.loop = map_entry(want);
            s.deadline_ms = s.start_ms + map_ms(want);
        } else if (want == kNone && unmapped_long && (before_play || s.loop)) {
            s.stop_reason = "map_exit";
        } else if (want != kNone && want != s.index && s.phase == Phase::playing && Ms(s.playing_ms, now) >= p.map_dwell_ms) {
            s.stop_reason = "switch";
            s.switching = true;
            s.next_index = want;
        }
    }

    switch (s.phase) {
    case Phase::idle:
        break;
    case Phase::suspend:
    case Phase::patch: {
        o.suspend = o.owns_dm = true;
        if (const char* cancel = in.blocked ? in.blocked : s.stop_reason) {
            ++s.aborts;
            finish("abort", cancel);
            break;
        }
        if (s.phase == Phase::suspend) {
            if (!in.map_idle) o.release_map = true;
            if (!in.native_clear || !in.map_idle) {
                if (Ms(s.since_ms, now) >= p.suspend_ms) {
                    ++s.aborts;
                    finish("abort", !in.native_clear ? "native_busy" : "dm_map_busy");
                }
                break;
            }
            s.phase = Phase::patch;
            s.since_ms = now;
        }
        if (!in.patched) {
            o.patch = true;
            break;
        }
        const char* wait = !in.dm_ok                   ? "dm_read"
                         : in.q18[0] == kDmAct          ? kHostDm
                         : in.q18[1] == kDmAct          ? "busy"
                         : (in.g388 & 0x3Cu) != 0       ? "pending"
                         : !CamIdle(in) && !in.cam_skip ? "cam_connect"
                         : in.reply_busy                ? "reply"
                                                        : nullptr;
        if (wait) {
            if (now >= s.deadline_ms) {
                ++s.aborts;
                finish("abort", wait);
            } else if (wait != s.deferred) {
                s.deferred = wait;
                event("deferred", wait, Ms(s.since_ms, now));
            }
            break;
        }
        o.play = true;
        o.reply_code = ReplyCode(s.index, s.loop);
        s.phase = Phase::play_sent;
        s.play_ms = now;
        s.deferred = nullptr;
        ++s.plays;
        event("play", s.replays ? "replay" : nullptr, Ms(s.start_ms, now));
        break;
    }
    case Phase::play_sent:
        o.suspend = o.owns_dm = true;
        if (in.dm_ok && in.q18[1] == kDmAct) {
            s.phase = Phase::playing;
            s.playing_ms = now;
            ++s.confirms;
            event("playing", nullptr, Ms(s.play_ms, now));
            o.code_match = (in.rec18 & 0xFFFFFFFFFFFFull) == (ExpectedRec18(s.index) & 0xFFFFFFFFFFFFull);
        } else if (Ms(s.play_ms, now) >= p.confirm_ms) {
            o.clear_pending = true;
            ++s.unconsumed;
            finish("unconsumed", nullptr);
        } else if (in.blocked) {
            // The body went away (a ride or range hide): nothing of ours plays.
            o.clear_pending = true;
            ++s.aborts;
            finish("abort", in.blocked);
        }
        break;
    case Phase::playing: {
        o.suspend = o.owns_dm = true;
        if (in.blocked && !in.body_present) {
            // The body went away (ride/range hide, unloaded): no global Stop
            // that could outlive our DM and end the next one.
            ++s.aborts;
            finish("abort", in.blocked);
            break;
        }
        if (in.blocked && !s.stop_reason) s.stop_reason = in.blocked;
        if (!s.stop_reason && now >= s.deadline_ms) s.stop_reason = "timeout";
        // The game ended the DM: a one-shot finished; a loop is replayed once.
        if (in.dm_ok && in.q18[1] != kDmAct) {
            if (s.loop && !s.stop_reason && s.replays < p.replays) {
                ++s.replays;
                ++s.losts;
                s.phase = Phase::patch;
                s.since_ms = now;
                event("lost", "replay", Ms(s.playing_ms, now));
                break;
            }
            if (s.loop) ++s.losts; else ++s.finishes;
            finish(s.loop ? "lost" : "finished", s.stop_reason ? s.stop_reason : "ended");
            break;
        }
        if (in.dm_ok && in.cam_ok && (in.cam1f0 || in.cam200) && !in.cam_skip && !s.conflict_logged) {
            s.conflict_logged = true;
            event("conflict", "cam_connect", Ms(s.playing_ms, now));
        }
        if (!s.stop_reason) break;
        if (in.dm_ok && in.q18[0] == kDmAct) {
            // Stop is global: it would end the local player's DM.
            if (s.deferred != kHostDm) {
                s.deferred = kHostDm;
                event("deferred", kHostDm, Ms(s.playing_ms, now));
            }
            if (now < s.deadline_ms + p.stop_wait_ms) break;
            ++s.aborts;
            finish("abort", kHostDm);  // gives the DM up (it ends with the local DM's End)
            break;
        }
        o.stop = true;
        s.phase = Phase::stop_sent;
        s.stop_ms = now;
        s.stop_tries = 1;
        ++s.stops;
        event("stop", s.stop_reason, Ms(s.playing_ms, now));
        break;
    }
    case Phase::stop_sent:
        o.suspend = o.owns_dm = true;
        if (in.blocked && !in.body_present) {
            ++s.aborts;
            finish("abort", in.blocked);
            break;
        }
        if (in.dm_ok && in.q18[1] != kDmAct) {
            if (s.switching && s.next_index != kNone && !in.blocked && map_mode) {
                // Switch: the next entry plays without returning to native.
                const std::uint8_t next = s.next_index;
                event("released", "switch", Ms(s.start_ms, now));
                o.index = s.index;  // the line names the released entry
                o.loop = s.loop;
                o.source = s.source;
                begin(Source::map, next, map_entry(next), map_ms(next));
                s.phase = Phase::patch;
                o.suspend = o.owns_dm = true;
                return o;
            }
            finish("released", s.stop_reason ? s.stop_reason : "stopped");
        } else if (Ms(s.stop_ms, now) >= p.stop_wait_ms) {
            if (s.stop_tries < 2 && !(in.dm_ok && in.q18[0] == kDmAct)) {
                o.stop = true;
                s.stop_ms = now;
                ++s.stop_tries;
                event("stop_retry", s.stop_reason, Ms(s.playing_ms, now));
            } else {
                finish("stop_timeout", s.stop_reason);
            }
        }
        break;
    }
    o.index = s.index;
    o.loop = s.loop;
    o.source = s.source;
    return o;
}

} // namespace mgscoop::partner_dm_overlay
