// Portable preparation only: actual codec/mailbox/policy, recording backend.
// No game process, UI, native calls, or claims that a gun backend is verified.
#include "mgscoop/partner_upper_pipeline.hpp"
#include "mgscoop/partner_action_mailbox.hpp"
#include "mgscoop/partner_dmmap.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace a = mgscoop::partner_actions;
namespace u = mgscoop::partner_upper_actions;
namespace p = mgscoop::partner_upper_pipeline;
namespace dm = mgscoop::partner_dmmap;
unsigned checks = 0;
void Check(bool value, const char* reason) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %u: %s\n", checks, reason); std::exit(1); }
}
struct Backend {
    std::vector<u::Command> commands;
    u::Feedback next{};
    u::Feedback Poll() { const auto result = next; next = {}; return result; }
    void Submit(const u::Command& c) {
        if (c.action == u::Action::none) return;
        Check(c.channel == 2 && c.id != 0, "every recording backend command has independent upper identity/channel");
        commands.push_back(c);
    }
    void Reply(u::FeedbackStatus status, std::uint64_t id) { next = {id, status}; }
    std::size_t Plays() const {
        std::size_t n = 0;
        for (const auto& c : commands) if (c.action == u::Action::play) ++n;
        return n;
    }
};
struct Owner {
    a::Producer producer{};
    a::Mailbox mailbox{};
    a::Frame frame{};
    p::State state{};
    p::In input{};
    Backend backend{};
    a::Receipt logged{};
    std::uint64_t now = 1000, peer = 0;
    std::uint32_t mission = 30050;
    bool connected = true;
    unsigned ticks = 0, samples = 0;
    Owner() {
        frame.session = 0x123456789abcdef0ull; frame.epoch = mission;
        Check(a::TrustedPeer(false, false, {0x7f000001, 15101}, {0x7f000001, 15101}, peer), "fixture uses configured raw endpoint key");
    }
    void Enable() {
        // Recording backend proof injected exclusively by the fixture.
        input.enabled = input.play_verified = input.stop_verified = input.body_ready = true;
        input.profile = {873, 0, 21, 24, 25, true, true, true};
    }
    void Send(bool fire = false, bool reload = false, std::uint8_t flags = 0, std::uint16_t equip = 873, std::uint8_t stance = 0,
              std::uint32_t source_tick = 0xffffffffu) {
        a::Sample source{};
        source.enabled = source.eligible = true; source.mission = mission;
        source.tick_ms = source_tick == 0xffffffffu ? static_cast<std::uint32_t>(now) : source_tick;
        source.equip = equip; source.stance = stance; source.flags = flags;
        source.raw_fire_active = fire; source.raw_reload_pressed = reload;
        Check(a::Observe(producer, source), "source sample accepted");
        frame.action = producer.action; ++frame.sequence;
        std::uint8_t bytes[a::kFrameSize]{};
        Check(a::Encode(frame, bytes) == sizeof(bytes), "companion encoded");
        a::Frame decoded{};
        Check(a::Decode(bytes, sizeof(bytes), decoded), "companion decoded");
        Check(a::Store(mailbox, decoded, peer, frame.session, frame.epoch, now), "actual mailbox stores companion");
    }
    u::Command Tick(std::uint64_t dt = 20) {
        now += dt; ++ticks;
        a::Receipt receipt{}; std::uint32_t age = 0;
        ++samples;
        const bool fresh = a::SampleMailbox(mailbox, frame.session, frame.epoch, mission, now, connected, receipt, age);
        logged = receipt; // the logger shares this single sampled result
        input.now_ms = now; input.lease_fresh = fresh; input.sample_age_ms = age; input.sample = receipt;
        input.feedback = backend.Poll();
        const auto command = p::Tick(state, input);
        backend.Submit(command);
        Check(samples == ticks, "owner samples once, sharing receipt with logger and pipeline");
        return command;
    }
    void Heartbeat(std::uint8_t flags = 0) { Send(false, false, flags, producer.action.equip, producer.action.stance); }
};
void DefaultAndCapabilities() {
    Owner off; off.Send(); off.Tick(); off.Send(true, false, 2); off.Tick();
    Check(off.backend.commands.empty() && !off.state.have_latest, "default pipeline cannot submit or retain latent playback");
    for (unsigned guard = 0; guard < 6; ++guard) {
        Owner o; o.Enable();
        if (guard == 0) o.input.enabled = false;
        if (guard == 1) o.input.play_verified = false;
        if (guard == 2) o.input.stop_verified = false;
        if (guard == 3) o.input.body_ready = false;
        if (guard == 4) o.input.reset = true;
        if (guard == 5) o.input.profile = {};
        o.Send(); o.Tick(); o.Send(true, false, 2); o.Tick();
        Check(o.backend.commands.empty(), "runtime/clip capability guard prevents automatic submission");
    }
}
void OneSampleAndHistory() {
    Owner o; o.Enable(); o.Send(); o.Tick(); o.Send(true, false, 2);
    const auto first = o.Tick();
    Check(first.action == u::Action::play && first.gani == 24 && first.event.token == 1
        && first.event.tick_ms == o.producer.action.history[0].tick_ms && a::Same(first.context, o.logged.context)
        && o.logged.count == 1, "same single sampled receipt reaches log and backend with original context/token/tick");
    for (unsigned i = 0; i < 8; ++i) o.Tick(8);
    Check(o.logged.status == a::ReceiptStatus::duplicate && o.logged.count == 0 && o.backend.Plays() == 1
        && o.state.sequencer.count == 0, "empty duplicate samples never double-consume or replay cached event");

    Owner h; h.Enable(); h.Send(); h.Tick();
    for (unsigned i = 0; i < 3; ++i) { h.Send(true, false, 2); h.now += 10; h.Send(); h.now += 10; }
    auto command = h.Tick();
    Check(h.logged.count == 3 && command.event.token == 1 && h.state.sequencer.count == 2,
          "unconsumed mailbox history recovers three original events in one owner sample");
    for (unsigned token = 1; token <= 3; ++token) {
        h.backend.Reply(u::FeedbackStatus::accepted, command.id); h.Tick(8);
        h.backend.Reply(u::FeedbackStatus::completed, command.id);
        command = h.Tick(40);
        if (token < 3) Check(command.action == u::Action::play && command.event.token == token + 1,
                            "natural completion advances original event order without new packets");
    }
    Check(h.backend.Plays() == 3 && h.state.sequencer.count == 0 && h.state.count == 0,
          "three distinct histories submit once each");
}
void ReplacementBetweenPackets() {
    for (const bool equip_change : {false, true}) {
        Owner o; o.Enable(); o.Send(false, false, 1);
        const auto hold = o.Tick();
        o.backend.Reply(u::FeedbackStatus::accepted, hold.id); o.Tick(8);
        if (equip_change) o.input.profile.equip = 874;
        else a::Clear(o.producer); // same equipment, new source generation and released aim
        o.Send(false, false, equip_change ? 1 : 0, equip_change ? 874 : 873);
        const auto stop = o.Tick(8);
        const auto replacement = o.logged.context;
        Check(stop.action == u::Action::stop && a::Same(stop.context, hold.context)
            && !a::Same(replacement, hold.context), "replacement cancels original leased context first");
        o.backend.Reply(u::FeedbackStatus::completed, hold.id); o.Tick(8);
        Check(o.state.sequencer.phase == u::Phase::stopping, "late old play completion cannot acknowledge new stop");
        o.backend.Reply(u::FeedbackStatus::stopped, stop.id);
        const auto next = o.Tick(8);
        Check(o.logged.status == a::ReceiptStatus::duplicate && a::Same(o.state.sequencer.context, replacement),
              "stop ACK between packets commits retained replacement context");
        if (equip_change) Check(next.action == u::Action::play && next.context.equip == 874 && next.id != hold.id,
                               "new upper hold uses only replacement equipment/context");
        else Check(next.action == u::Action::none && o.state.sequencer.flags == 0,
                   "released aim in replacement cannot resurrect old held aim");
    }
    // Updates received during cancellation must not overwrite their baseline
    // or disappear in the policy's early stopping return.
    Owner o; o.Enable(); o.Send(false, false, 1); const auto hold = o.Tick();
    o.backend.Reply(u::FeedbackStatus::accepted, hold.id); o.Tick(8);
    a::Clear(o.producer); o.Send(); const auto stop = o.Tick(8);
    o.Send(true, false, 2); o.Tick(8);
    o.Send(); o.Tick(8); o.Send(false, true, 8); o.Tick(8);
    Check(o.state.count == 2 && o.state.need_baseline, "replacement fire/reload accumulate while stop waits");
    o.backend.Reply(u::FeedbackStatus::stopped, stop.id); const auto ack = o.Tick(8);
    Check(ack.action == u::Action::none && !o.state.need_baseline, "replacement baseline is established before delivering its deltas");
    const auto fire = o.Tick(8);
    Check(fire.action == u::Action::play && fire.event.token == 1 && fire.context.generation == o.producer.action.generation,
          "pending replacement fire retains identity after between-packet stop ACK");
    o.backend.Reply(u::FeedbackStatus::accepted, fire.id); o.Tick(8);
    o.backend.Reply(u::FeedbackStatus::completed, fire.id); const auto reload = o.Tick(40);
    Check(reload.action == u::Action::play && reload.gani == 25 && reload.event.token == 2,
          "pending replacement reload is neither overwritten nor duplicated");

    Owner same; same.Enable(); same.Send(false, false, 1); const auto aim = same.Tick();
    same.backend.Reply(u::FeedbackStatus::accepted, aim.id); same.Tick(8);
    same.Send(true, false, 3); const auto first_stop = same.Tick(8);
    Check(first_stop.action == u::Action::stop && same.state.sequencer.count == 1,
          "same-context fire already queues in policy when it interrupts upper hold");
    same.Send(false, false, 1); same.Tick(8); same.Send(true, false, 3); same.Tick(8);
    same.backend.Reply(u::FeedbackStatus::stopped, first_stop.id); const auto first = same.Tick(8);
    Check(first.action == u::Action::play && first.event.token == 1 && same.state.sequencer.count == 1,
          "events arriving during same-context stop do not duplicate its already consumed first event");
    same.backend.Reply(u::FeedbackStatus::accepted, first.id); same.Tick(8);
    same.backend.Reply(u::FeedbackStatus::completed, first.id); const auto second = same.Tick(8);
    Check(second.action == u::Action::play && second.event.token == 2 && same.state.sequencer.count == 0,
          "second same-context event retains its identity after interrupted hold stops");
}
void CancellationAndDeadlines() {
    for (unsigned cause = 0; cause < 7; ++cause) {
        Owner o; o.Enable(); o.Send(); o.Tick(); o.Send(true, false, 2); const auto fire = o.Tick();
        o.backend.Reply(u::FeedbackStatus::accepted, fire.id); o.Tick(8);
        o.Send(); o.Tick(8); o.Send(true, false, 2); o.Tick(8);
        Check(o.state.sequencer.count == 1, "cancellation starts with another distinct event queued");
        if (cause == 0) o.input.enabled = false;
        if (cause == 1) o.input.play_verified = false;
        if (cause == 2) o.input.reset = true;
        if (cause == 3) o.connected = false;
        if (cause == 4) o.now += a::kNetworkAgeMs + 1;
        if (cause == 5) ++o.mission;
        if (cause == 6) o.now -= 100;
        const auto stop = o.Tick(8);
        Check(stop.action == u::Action::stop && !o.state.count && !o.state.sequencer.count,
              "disable/reset/loss/mission/time changes discard both waiting queues and issue bounded stop");
        o.backend.Reply(u::FeedbackStatus::stopped, stop.id); o.Tick(8);
        Check(o.backend.Plays() == 1, "cancellation ACK cannot launch discarded waiting event");
    }
    Owner unaccepted; unaccepted.Enable(); unaccepted.Send(); unaccepted.Tick();
    unaccepted.Send(true, false, 2); const auto fire = unaccepted.Tick();
    unaccepted.Heartbeat(); unaccepted.Tick(300);
    const auto stop = unaccepted.Tick(50);
    Check(fire.action == u::Action::play && stop.action == u::Action::stop && unaccepted.input.lease_fresh
        && unaccepted.logged.status == a::ReceiptStatus::duplicate,
          "missing acceptance deadline advances on a no-packet owner tick");
    unaccepted.Heartbeat(); unaccepted.Tick(200); unaccepted.Heartbeat(); unaccepted.Tick(150);
    Check(unaccepted.state.sequencer.phase == u::Phase::faulted && !unaccepted.state.have_latest && unaccepted.input.lease_fresh,
          "missing stop ACK faults and clears cached data");

    Owner finite; finite.Enable(); finite.Send(); finite.Tick(); finite.Send(true, false, 2); const auto active = finite.Tick();
    finite.backend.Reply(u::FeedbackStatus::accepted, active.id); finite.Tick(8);
    for (unsigned i = 0; i < 9; ++i) { finite.Heartbeat(); finite.Tick(200); }
    finite.Heartbeat();
    const auto timed = finite.Tick(200);
    Check(timed.action == u::Action::stop && finite.input.lease_fresh,
          "finite completion deadline advances independently of fresh movement/action heartbeats");

    Owner unsafe; unsafe.Enable(); unsafe.Send(); unsafe.Tick(); unsafe.Send(true, false, 2); unsafe.Tick();
    unsafe.input.body_ready = false;
    Check(unsafe.Tick().action == u::Action::none && !unsafe.state.have_latest,
          "invalid body cannot dispatch a stop against unvalidated addresses");
}
void PendingAgeAndGap() {
    Owner o; o.Enable(); o.Send(false, false, 1); const auto hold = o.Tick();
    o.backend.Reply(u::FeedbackStatus::accepted, hold.id); o.Tick(8);
    a::Clear(o.producer); o.Send(); const auto stop = o.Tick(8);
    o.Send(true, false, 2); o.Tick(8);
    o.Send(); o.Tick(200);
    o.backend.Reply(u::FeedbackStatus::stopped, stop.id); o.Tick(8);
    const auto fire = o.Tick(8);
    Check(fire.action == u::Action::play && o.state.sequencer.phase == u::Phase::accepting,
          "still-young pending event survives cancellation wait");
    o.backend.Reply(u::FeedbackStatus::accepted, fire.id); o.Tick(8);
    // A second pending event is injected into the real mailbox, then remains
    // queued behind the active finite long enough to expire.
    o.Send(true, false, 2); o.Tick(8); // prior producer fire was false
    o.Heartbeat(); o.Tick(200); o.Heartbeat(); o.Tick(200);
    o.backend.Reply(u::FeedbackStatus::completed, fire.id); const auto end = o.Tick(150);
    Check(end.action == u::Action::none && o.state.sequencer.dropped == 1 && o.backend.Plays() == 2,
          "queued expiry uses original receive age and never gains a new lease from cache");

    Owner wait; wait.Enable(); wait.Send(false, false, 1); const auto loop = wait.Tick();
    wait.backend.Reply(u::FeedbackStatus::accepted, loop.id); wait.Tick(8);
    a::Clear(wait.producer); wait.Send(false, false, 0, 873, 0, 2000); const auto cancel = wait.Tick(8);
    wait.Send(true, false, 2, 873, 0, 2010); wait.Tick(8);
    // Source clock and owner clock are independent. A fresh heartbeat retains
    // a now nearly old event while the receiver is waiting for cancellation.
    wait.Send(false, false, 0, 873, 0, 2500); wait.Tick(8);
    wait.backend.Reply(u::FeedbackStatus::stopped, cancel.id); wait.Tick(8); wait.Tick(8);
    Check(wait.state.dropped + wait.state.sequencer.dropped == 1 && wait.backend.Plays() == 1,
          "time waiting for ACK counts toward pending expiry before policy delivery");

    Owner gap; gap.Enable(); gap.Send(false, false, 1); const auto ghold = gap.Tick();
    gap.backend.Reply(u::FeedbackStatus::accepted, ghold.id); gap.Tick(8);
    a::Clear(gap.producer); gap.Send(); const auto gstop = gap.Tick(8);
    for (unsigned i = 0; i < 9; ++i) { gap.Send(true, false, 2); gap.Tick(1); gap.Send(); gap.Tick(1); }
    Check(gap.state.pending_gap && gap.state.gaps == 1 && !gap.state.count,
          "bounded pending buffer reports a gap instead of inventing or overwriting events");
    gap.backend.Reply(u::FeedbackStatus::stopped, gstop.id); gap.Tick(8); gap.Tick(8); gap.Tick(8);
    Check(gap.backend.Plays() == 1 && !gap.state.pending_gap && !gap.state.count,
          "acknowledged gap never submits its incomplete event history");

    Owner missing; missing.Enable(); missing.Send(); missing.Tick();
    missing.Send(true, false, 2); const auto current = missing.Tick();
    missing.backend.Reply(u::FeedbackStatus::accepted, current.id); missing.Tick(8);
    for (unsigned i = 0; i < 10; ++i) { missing.Send(); ++missing.now; missing.Send(true, false, 2); ++missing.now; }
    const auto gap_stop = missing.Tick(8);
    Check(missing.logged.status == a::ReceiptStatus::history_gap && gap_stop.action == u::Action::stop
        && missing.state.gaps == 1 && missing.state.sequencer.count == 0,
        "real mailbox history gap cancels active upper ownership without fabricated events");
    missing.backend.Reply(u::FeedbackStatus::stopped, gap_stop.id); missing.Tick(8); missing.Tick(8);
    Check(missing.backend.Plays() == 1 && !missing.state.pending_gap,
          "real mailbox gap acknowledgment cannot replay overwritten history");
}
void LowerIndependence() {
    Owner o; o.Enable(); o.Send(); o.Tick();
    dm::State lower{}, control{}; dm::Rearm(lower); dm::Rearm(control);
    bool in_dm = false; unsigned lower_plays = 0, lower_stops = 0;
    std::uint64_t upper_id = 0;
    for (unsigned i = 0; i < 80; ++i) {
        if (i == 5) o.Send(true, false, 2);
        if (i == 6) o.backend.Reply(u::FeedbackStatus::accepted, upper_id);
        if (i == 20) o.backend.Reply(u::FeedbackStatus::completed, upper_id);
        if (i % 10 == 0) o.Heartbeat();
        const auto upper = o.Tick(20);
        if (upper.action == u::Action::play) upper_id = upper.id;
        dm::In in{};
        in.map = in.target_ok = in.dm_ok = in.patched = in.parts_loaded = in.cam_idle = true;
        in.now_ms = o.now; in.in_dm = in_dm; in.cls.via = dm::Via::flags;
        in.cls.move = i < 30 ? 1 : 0; in.target = i < 30 ? 10 : 0;
        in.responsive = true; in.urgent = in.cls.move != 0;
        const auto actual = dm::Step(lower, in), expected = dm::Step(control, in);
        Check(actual.action == expected.action && actual.gani == expected.gani && actual.event == expected.event,
              "simultaneous upper pipeline leaves actual lower locomotion policy unchanged");
        if (actual.action == dm::Action::play) { in_dm = true; ++lower_plays; }
        if (actual.action == dm::Action::stop) { in_dm = false; ++lower_stops; }
    }
    Check(o.backend.Plays() == 1 && lower_plays == 2 && lower_stops == 1 && lower.cur == 0 && lower.bad == 0,
          "upper finite event coexists with lower walk-to-idle switch without lower faults");
    const auto serial = o.state.sequencer.next_command;
    p::Reset(o.state);
    Check(o.state.sequencer.next_command == serial && !o.state.have_latest && !o.state.count,
          "explicit post-revocation reset preserves unique command serial and clears receipt data");
}
int main() {
    DefaultAndCapabilities(); OneSampleAndHistory(); ReplacementBetweenPackets();
    CancellationAndDeadlines(); PendingAgeAndGap(); LowerIndependence();
    std::printf("%u upper owner pipeline checks passed; actual mailbox/codec/policy, recording backend; no game/native proof\n", checks);
    return 0;
}
