#pragma once
// Portable owner-side plumbing only. The caller samples the trusted mailbox
// once, shares that result with logging, and supplies current lease freshness.
// This file neither samples transport nor calls/enables a native backend.
#include "mgscoop/partner_upper_actions.hpp"
#include <array>
#include <cstdint>

namespace mgscoop::partner_upper_pipeline {
namespace events = partner_actions;
namespace upper = partner_upper_actions;
struct In {
    bool enabled = false, play_verified = false, stop_verified = false, body_ready = false, reset = false;
    bool lease_fresh = false; // this tick's trusted sample result; never inferred from the cached receipt
    std::uint32_t sample_age_ms = 0; // current sample's actual receive age, for event expiry only
    std::uint64_t now_ms = 0;
    events::Receipt sample{};
    upper::Profile profile{};
    upper::Feedback feedback{};
};
struct Pending {
    events::Event event{};
    std::uint64_t received_ms = 0;
    std::uint32_t initial_age = 0;
};
struct State {
    upper::State sequencer{};
    events::Receipt latest{}, baseline{};
    bool have_latest = false, need_baseline = false, pending_gap = false;
    std::uint64_t latest_ms = 0;
    std::uint32_t latest_age = 0;
    std::array<Pending, events::kHistory> pending{};
    std::uint8_t count = 0;
    std::uint32_t dropped = 0, gaps = 0;
};
inline bool Accepted(events::ReceiptStatus status) noexcept {
    return status == events::ReceiptStatus::baseline || status == events::ReceiptStatus::update
        || status == events::ReceiptStatus::history_gap;
}
inline void DiscardReceived(State& s) noexcept {
    s.have_latest = s.need_baseline = s.pending_gap = false;
    s.latest = {}; s.baseline = {}; s.count = 0; s.latest_ms = 0; s.latest_age = 0;
}
// Use only after the native lease has been revoked, or before any submission.
// For ordinary cancellation, tick with reset=true so the policy issues a stop.
inline void Reset(State& s) noexcept {
    const auto serial = s.sequencer.next_command;
    s = {}; s.sequencer.next_command = serial;
}
inline void Erase(State& s, unsigned at) noexcept {
    for (unsigned i = at + 1; i < s.count; ++i) s.pending[i - 1] = s.pending[i];
    --s.count;
}
inline upper::Command Tick(State& s, const In& in) noexcept {
    const bool gate = in.enabled && in.play_verified && in.stop_verified && in.body_ready && in.lease_fresh && !in.reset;
    if (!gate || (s.sequencer.last_ms && in.now_ms < s.sequencer.last_ms)) {
        DiscardReceived(s);
        // Step waits for a stop ACK before its ordinary cancellation branch.
        // Drop waiting events immediately without abandoning that stop owner.
        s.sequencer.count = 0; s.sequencer.flags = 0;
    }
    else if (Accepted(in.sample.status) && (!s.have_latest || !events::Same(s.latest.context, in.sample.context)
        || events::Newer(in.sample.sequence, s.latest.sequence))) {
        const bool changed = !s.have_latest || !events::Same(s.latest.context, in.sample.context);
        if (changed || in.sample.status == events::ReceiptStatus::baseline) {
            s.count = 0; s.pending_gap = false; s.need_baseline = true;
            s.baseline = in.sample;
            s.baseline.status = events::ReceiptStatus::baseline;
            s.baseline.count = 0; s.baseline.events = {};
        }
        s.latest = in.sample; s.have_latest = true; s.latest_ms = in.now_ms; s.latest_age = in.sample_age_ms;
        if (in.sample.status == events::ReceiptStatus::history_gap) {
            s.count = 0; s.pending_gap = true; ++s.gaps;
        } else if (in.sample.status == events::ReceiptStatus::update && !changed && !s.pending_gap) {
            for (unsigned i = 0; i < in.sample.count && i < events::kHistory; ++i) {
                const std::uint64_t age = std::uint64_t(in.sample.tick_ms - in.sample.events[i].tick_ms) + in.sample_age_ms;
                if (age > events::kEventAgeMs) { ++s.dropped; continue; }
                if (s.count == events::kHistory) {
                    s.dropped += s.count + in.sample.count - i; s.count = 0; s.pending_gap = true; ++s.gaps; break;
                }
                s.pending[s.count++] = {in.sample.events[i], in.now_ms, static_cast<std::uint32_t>(age)};
            }
        }
    }
    for (unsigned i = 0; i < s.count;) {
        const auto& p = s.pending[i];
        if (in.now_ms < p.received_ms || in.now_ms - p.received_ms + p.initial_age > events::kEventAgeMs) {
            Erase(s, i); ++s.dropped;
        } else ++i;
    }
    upper::In input{};
    input.enabled = in.enabled; input.play_verified = in.play_verified; input.stop_verified = in.stop_verified;
    input.body_ready = in.body_ready; input.fresh = in.lease_fresh && s.have_latest; input.reset = in.reset;
    input.now_ms = in.now_ms; input.profile = in.profile; input.feedback = in.feedback;
    const bool baseline = s.have_latest && s.need_baseline;
    const bool gap = s.have_latest && !baseline && s.pending_gap;
    if (s.have_latest) {
        input.receipt = baseline ? s.baseline : s.latest;
        input.receipt.count = 0; input.receipt.events = {};
        if (gap) input.receipt.status = events::ReceiptStatus::history_gap;
        else if (!baseline) {
            input.receipt.status = events::ReceiptStatus::update;
            // The internal delivery clock includes time spent waiting for a
            // stop ACK. Original event tokens/ticks remain byte-for-byte intact.
            // This conservative age accounting never supplies lease freshness.
            input.receipt.tick_ms += s.latest_age + static_cast<std::uint32_t>(in.now_ms - s.latest_ms);
            for (unsigned i = 0; i < s.count; ++i) {
                const auto& p = s.pending[i];
                const auto tick = p.event.tick_ms + p.initial_age + static_cast<std::uint32_t>(in.now_ms - p.received_ms);
                if (events::Newer(tick, input.receipt.tick_ms)) input.receipt.tick_ms = tick;
                input.receipt.events[input.receipt.count++] = p.event;
            }
        }
    }
    const auto out = upper::Step(s.sequencer, input); // always advances feedback/deadlines, even with no packets
    const auto& policy = s.sequencer;
    const bool received = gate && policy.context_ready && events::Same(policy.context, input.receipt.context)
        && (policy.sequence == input.receipt.sequence || events::Newer(policy.sequence, input.receipt.sequence));
    if (baseline && received && policy.phase != upper::Phase::stopping) s.need_baseline = false;
    if (gap && policy.phase == upper::Phase::idle && out.action != upper::Action::stop) s.pending_gap = false;
    // A hold-to-finite stop can occur after Step already consumed this exact
    // sequence. Retire those delivered deltas even while the stop is pending.
    if (!baseline && !gap && received && policy.phase != upper::Phase::faulted) s.count = 0;
    if (policy.phase == upper::Phase::faulted) DiscardReceived(s);
    return out;
}
} // namespace mgscoop::partner_upper_pipeline
