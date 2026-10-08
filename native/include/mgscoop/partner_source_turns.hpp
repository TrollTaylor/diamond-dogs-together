#pragma once
// Raw accepted-source turn policy. No wire format, native calls or camera
// inference. Visual interpolation is deliberately outside this collector.
#include <cmath>
#include "mgscoop/partner_dmmap.hpp"
#include "mgscoop/pose_interp.hpp"
#include "mgscoop/player_sync.hpp"

namespace mgscoop::partner_source_turns {
namespace dm = partner_dmmap;
inline constexpr std::uint64_t kSourceGapMs = 100;
inline constexpr std::uint64_t kSourceFreshMs = pose::stale_ms;
inline constexpr std::size_t kCapacity = 16;
struct Event {
    std::uint32_t token{};
    std::uint8_t gani{dm::kNoGani}, stance{};
    std::uint64_t receive_ms{};
    std::int64_t sender_ms{};
    std::uint32_t generation{};
};
struct State {
    pose::HistoryCursor cursor{};
    dm::SpeedRing speed{};
    pose::SourceRecord previous{};
    dm::Class cls{};
    Event pending[kCapacity]{};
    std::uint8_t count{};
    bool have_previous{}, have_owner{};
    std::uint64_t owner_ms{};
};
struct Result { bool cancel_queue{}, busy{}; Event event{}; };

inline bool Fresh(std::uint64_t receive, std::uint64_t now) noexcept {
    return now >= receive && now - receive <= kSourceFreshMs;
}
inline void Clear(State& s, dm::TurnState& turn) noexcept {
    s.have_previous = false;
    s.count = 0;
    s.cls = {};
    dm::SpeedReset(s.speed);
    turn = {};
}
inline std::int32_t Mm(float value) noexcept { return static_cast<std::int32_t>(std::lround(value * 1000.0f)); }
inline bool Valid(const pose::SourceRecord& r) noexcept {
    const auto& p = r.snapshot;
    return r.sender_ms >= 0 && p.stance <= 2 && std::isfinite(p.yaw) &&
        std::isfinite(p.x) && std::fabs(p.x) <= 1000000.0f &&
        std::isfinite(p.z) && std::fabs(p.z) <= 1000000.0f;
}
inline dm::Class Classify(State& s, const pose::SourceRecord& r, const PartnerDmSpeeds& speeds) noexcept {
    const auto& p = r.snapshot;
    dm::Observation o{};
    o.fresh = true;
    o.stance = p.stance;
    o.sprint = (p.flags & sync::ActionFlag::Sprinting) != 0;
    o.flag_move = (p.flags & sync::ActionFlag::Running) ? 2 :
                  (p.flags & sync::ActionFlag::Walking) ? 1 : dm::kMoveUnknown;
    o.anim_valid = p.anim_valid;
    o.anim_move = true;
    o.act = p.act; o.anim_stance = p.anim_stance; o.anim_speed = p.anim_speed;
    o.speed_ok = dm::SpeedMmps(s.speed, o.speed_mmps);
    return dm::Classify(o, speeds, s.have_previous ? s.cls.move : dm::kMoveUnknown, s.cls.stance);
}
// Feed a paired visual/history read once per owner tick. Only new accepted
// source records advance the70ms settle/600ms deadline. Every queued delivery
// retains its original context and receive age, even if the latest packet is
// fresh after an owner stall. One delivery per owner tick preserves Step's
// existing request queue and at-most-one native action contract. Collection
// eligibility is a lifecycle gate; delayed visual movement/stance only gates
// delivery so it cannot discard the start of a new stationary source pivot.
inline Result Step(State& s, dm::TurnState& turn, const pose::RemoteFrame& frame, std::uint64_t now,
                   bool eligible, const PartnerDmSpeeds& speeds, std::uint8_t expected_stance,
                   bool delivery_ready = true) noexcept {
    Result out{};
    const bool owner_stall = s.have_owner && (now < s.owner_ms || now - s.owner_ms > kSourceFreshMs);
    s.have_owner = true; s.owner_ms = now;
    if (!eligible || !frame.visual.valid || frame.visual.status != pose::Status::live || frame.count > kCapacity || !frame.next.generation) {
        Clear(s, turn); s.cursor = frame.next; out.cancel_queue = true; return out;
    }
    bool baseline = frame.reset || frame.overflow || owner_stall || s.cursor.generation != frame.next.generation;
    for (std::size_t i = 0; i < frame.count; ++i)
        baseline = baseline || !Valid(frame.records[i]) || !Fresh(frame.records[i].receive_ms, now);
    if (baseline) { Clear(s, turn); out.cancel_queue = true; }
    std::size_t begin = baseline && frame.count ? frame.count - 1 : 0;
    for (std::size_t i = begin; i < frame.count; ++i) {
        const auto& r = frame.records[i];
        if (!Valid(r) || !Fresh(r.receive_ms, now)) { Clear(s, turn); out.cancel_queue = true; continue; }
        if (s.have_previous && r.sender_ms <= s.previous.sender_ms) continue; // defensive duplicate/reorder
        const bool gap = s.have_previous &&
            (r.sender_ms - s.previous.sender_ms > static_cast<std::int64_t>(kSourceGapMs) ||
             r.receive_ms < s.previous.receive_ms || r.receive_ms - s.previous.receive_ms > kSourceGapMs);
        const bool stance_change = s.have_previous && r.snapshot.stance != s.previous.snapshot.stance;
        if (gap || stance_change) { Clear(s, turn); out.cancel_queue = true; }
        const bool jump = dm::SpeedPush(s.speed, static_cast<std::uint64_t>(r.sender_ms), Mm(r.snapshot.x), Mm(r.snapshot.z));
        if (jump) { Clear(s, turn); out.cancel_queue = true;
            dm::SpeedPush(s.speed, static_cast<std::uint64_t>(r.sender_ms), Mm(r.snapshot.x), Mm(r.snapshot.z)); }
        const auto cls = Classify(s, r, speeds);
        // Animation readback can reveal a stance transition before posture
        // flags change. Keep speed/history, but never deliver its old pivot.
        if (s.have_previous && cls.stance != s.cls.stance) {
            s.count = 0;
            turn = {};
            out.cancel_queue = true;
        }
        const bool gun_busy = r.snapshot.equip_id &&
            (r.snapshot.flags & (sync::ActionFlag::Firing | sync::ActionFlag::Reloading)) != 0;
        const bool busy = cls.move != 0 || gun_busy || cls.stance > 1;
        if (busy) { Clear(s, turn); out.cancel_queue = true; }
        const auto old_token = turn.event_token;
        const auto gani = dm::ObserveTurn(turn, static_cast<std::uint64_t>(r.sender_ms), true,
                                          r.snapshot.yaw, cls.stance, busy);
        if (!busy && gani != dm::kNoGani && turn.event_token != old_token) {
            if (s.count == kCapacity) { // bounded policy: discard waiting oldest, never native in-flight state
                for (std::size_t j = 1; j < s.count; ++j) s.pending[j - 1] = s.pending[j];
                --s.count;
            }
            s.pending[s.count++] = Event{turn.event_token, gani, cls.stance, r.receive_ms, r.sender_ms, frame.next.generation};
        }
        s.previous = r; s.have_previous = true; s.cls = cls;
        out.busy = busy;
    }
    s.cursor = frame.next;
    if (s.have_previous && (now < s.previous.receive_ms || now - s.previous.receive_ms > kSourceGapMs)) {
        Clear(s, turn); out.cancel_queue = true;
    }
    while (s.count && (!Fresh(s.pending[0].receive_ms, now) || s.pending[0].generation != frame.next.generation)) {
        Clear(s, turn); out.cancel_queue = true;
    }
    if (s.count && delivery_ready && s.pending[0].stance == expected_stance) {
        out.event = s.pending[0];
        for (std::size_t i = 1; i < s.count; ++i) s.pending[i - 1] = s.pending[i];
        --s.count;
    }
    return out;
}
} // namespace mgscoop::partner_source_turns
