#pragma once
// Portable, default-off policy for a separately verified upper channel backend.
// It cannot dispatch native requests. Lower locomotion/turn channel 0 is never
// read, stopped or changed. Completion must come from the upper backend itself.
#include "mgscoop/partner_action_events.hpp"
#include <array>
#include <cstdint>

namespace mgscoop::partner_upper_actions {
namespace events = partner_actions;
constexpr std::uint8_t kUpperChannel = 2, kNoGani = 255;
constexpr std::uint64_t kAcceptDeadlineMs = 350, kEndDeadlineMs = 2000;
enum class Action : std::uint8_t { none, play, stop };
enum class FeedbackStatus : std::uint8_t { none, accepted, rejected, completed, stopped };
enum class Phase : std::uint8_t { idle, accepting, loop, finite, stopping, faulted };
struct Feedback { std::uint64_t command = 0; FeedbackStatus status = FeedbackStatus::none; };
struct Command {
    Action action = Action::none; std::uint8_t channel = kUpperChannel, gani = kNoGani; bool loop = false;
    std::uint64_t id = 0; events::Context context{}; events::Event event{};
};
// Proof is per clip, equipment and stance. One verified finite sniper clip
// cannot authorize unverified resident hold/reload states or other guns.
struct Profile {
    std::uint16_t equip = 0; std::uint8_t stance = 0;
    std::uint8_t hold = kNoGani, fire = kNoGani, reload = kNoGani;
    bool hold_verified = false, fire_verified = false, reload_verified = false;
};
struct In {
    bool enabled = false, play_verified = false, stop_verified = false, body_ready = false, fresh = false, reset = false;
    std::uint64_t now_ms = 0; events::Receipt receipt{}; Feedback feedback{}; Profile profile{};
};
struct Queued { events::Event event{}; std::uint64_t received_ms = 0; std::uint32_t initial_age = 0; };
struct State {
    Phase phase = Phase::idle; bool context_ready = false, have_sequence = false;
    events::Context context{}; std::uint32_t sequence = 0; std::uint8_t flags = 0, count = 0;
    std::array<Queued, events::kHistory> queue{};
    std::uint64_t next_command = 0, current_command = 0, began_ms = 0, last_ms = 0;
    std::uint32_t dropped = 0; bool current_loop = false;
};
inline void Reset(State& s) noexcept { const auto serial = s.next_command; s = {}; s.next_command = serial; }
inline void Pop(State& s) noexcept { if (s.count) { for (unsigned i = 1; i < s.count; ++i) s.queue[i - 1] = s.queue[i]; --s.count; } }
inline bool Owns(const State& s) noexcept { return s.phase == Phase::accepting || s.phase == Phase::loop || s.phase == Phase::finite; }
inline std::uint64_t Next(State& s) noexcept { if (!++s.next_command) ++s.next_command; return s.next_command; }
inline Command Stop(State& s, std::uint64_t now) noexcept {
    Command c{}; c.action = Action::stop; c.context = s.context; c.id = Next(s);
    s.current_command = c.id; s.phase = Phase::stopping; s.began_ms = now; return c;
}
inline Command Step(State& s, const In& in) noexcept {
    Command out{};
    const bool clock_reset = s.last_ms && in.now_ms < s.last_ms; s.last_ms = in.now_ms;
    if (s.phase == Phase::faulted) { if (in.reset || !in.body_ready) Reset(s); return out; }
    // Cancellation still consumes its acknowledgement while disabled or while
    // the next context waits. Otherwise a repeated context reset could starve
    // the stop completion forever.
    if (s.phase == Phase::stopping) {
        if (in.feedback.command == s.current_command
            && (in.feedback.status == FeedbackStatus::stopped || in.feedback.status == FeedbackStatus::completed)) {
            s.phase = Phase::idle; s.current_command = 0;
        } else if ((in.feedback.command == s.current_command && in.feedback.status == FeedbackStatus::rejected)
            || clock_reset || in.now_ms - s.began_ms >= kAcceptDeadlineMs) { s.count = 0; s.phase = Phase::faulted; }
        if (s.phase == Phase::stopping || s.phase == Phase::faulted) return out;
    }
    const bool gate = in.enabled && in.play_verified && in.stop_verified && in.body_ready && in.fresh && !in.reset && !clock_reset;
    const auto& r = in.receipt;
    const bool accepted_receipt = r.status == events::ReceiptStatus::baseline || r.status == events::ReceiptStatus::update
        || r.status == events::ReceiptStatus::history_gap;
    const bool context_change = accepted_receipt && (!s.context_ready || !events::Same(s.context, r.context));
    const bool reconnect = accepted_receipt && r.status == events::ReceiptStatus::baseline && s.have_sequence && events::Newer(r.sequence, s.sequence);
    if (!gate || context_change || reconnect || r.status == events::ReceiptStatus::history_gap) {
        s.count = 0;
        if (Owns(s) && in.stop_verified && in.body_ready) return Stop(s, in.now_ms);
        if (s.phase != Phase::faulted || context_change || in.reset) { s.phase = Phase::idle; s.current_command = 0; }
        if ((context_change || reconnect) && s.phase == Phase::idle) {
            s.context = r.context; s.context_ready = true; s.have_sequence = true; s.sequence = r.sequence; s.flags = r.flags;
        }
        if (!gate) { s.flags = 0; s.context_ready = false; s.have_sequence = false; return out; }
        if (r.status == events::ReceiptStatus::history_gap) { s.flags = 0; return out; }
    }
    if (accepted_receipt && s.context_ready && events::Same(s.context, r.context)
        && (!s.have_sequence || events::Newer(r.sequence, s.sequence))) {
        s.have_sequence = true; s.sequence = r.sequence; s.flags = r.flags;
        if (r.status == events::ReceiptStatus::update) for (unsigned i = 0; i < r.count && i < events::kHistory; ++i) {
            const auto age = r.tick_ms - r.events[i].tick_ms;
            if (age > events::kEventAgeMs) { ++s.dropped; continue; }
            if (s.count == events::kHistory) { ++s.dropped; continue; }
            s.queue[s.count++] = {r.events[i], in.now_ms, age};
        }
    }
    if (in.feedback.command == s.current_command && in.feedback.status != FeedbackStatus::none) {
        if (in.feedback.status == FeedbackStatus::rejected) { s.count = 0; s.phase = Phase::faulted; return out; }
        if (s.phase == Phase::accepting && in.feedback.status == FeedbackStatus::accepted)
            s.phase = s.current_loop ? Phase::loop : Phase::finite;
        else if ((Owns(s) && in.feedback.status == FeedbackStatus::completed)
            || (s.phase == Phase::stopping && (in.feedback.status == FeedbackStatus::stopped || in.feedback.status == FeedbackStatus::completed))) {
            s.phase = Phase::idle; s.current_command = 0;
        }
    }
    if (s.phase == Phase::faulted) return out;
    if (s.phase == Phase::stopping) {
        if (in.now_ms - s.began_ms >= kAcceptDeadlineMs) { s.count = 0; s.phase = Phase::faulted; }
        return out;
    }
    if (s.phase == Phase::accepting && in.now_ms - s.began_ms >= kAcceptDeadlineMs) { s.count = 0; return Stop(s, in.now_ms); }
    if (s.phase == Phase::finite && in.now_ms - s.began_ms >= kEndDeadlineMs) { s.count = 0; return Stop(s, in.now_ms); }
    while (s.count && (in.now_ms < s.queue[0].received_ms
        || in.now_ms - s.queue[0].received_ms + s.queue[0].initial_age > events::kEventAgeMs)) { Pop(s); ++s.dropped; }
    const auto& profile = in.profile;
    const bool profile_match = profile.equip != 0 && profile.equip == s.context.equip && profile.stance == s.context.stance;
    const bool hold_ok = profile_match && profile.hold_verified && profile.hold == 21;
    const bool fire_ok = profile_match && profile.fire_verified && (profile.fire == 24 || (profile.fire >= 26 && profile.fire <= 29));
    const bool reload_ok = profile_match && profile.reload_verified && (profile.reload == 25 || profile.reload == 30);
    while (s.count && !(s.queue[0].event.kind == events::EventKind::fire ? fire_ok : reload_ok)) { Pop(s); ++s.dropped; }
    if (s.phase == Phase::loop && (s.count || !(s.flags & 1) || !hold_ok)) return Stop(s, in.now_ms);
    if (s.phase != Phase::idle || !s.context_ready) return out;
    if (!s.count && (!(s.flags & 1) || !hold_ok)) return out;
    out.action = Action::play; out.context = s.context; out.id = Next(s);
    if (s.count) { out.event = s.queue[0].event; out.gani = out.event.kind == events::EventKind::fire ? profile.fire : profile.reload; Pop(s); }
    else { out.gani = profile.hold; out.loop = true; }
    s.current_command = out.id; s.current_loop = out.loop; s.phase = Phase::accepting; s.began_ms = in.now_ms;
    return out;
}
} // namespace mgscoop::partner_upper_actions
