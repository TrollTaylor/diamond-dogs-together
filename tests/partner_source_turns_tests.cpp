// Actual SnapshotRing -> raw collector -> finite policy, with recorded effects.
// Pure timelines only; neither game processes nor native routines are used.
#include "mgscoop/partner_source_turns.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
namespace pose = mgscoop::pose;
namespace raw = mgscoop::partner_source_turns;
namespace dm = mgscoop::partner_dmmap;
namespace sync = mgscoop::sync;
unsigned checks{};
void Check(bool ok, const char* reason) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "failed check %u: %s\n", checks, reason); std::exit(1); }
}
constexpr float pi = 3.14159265358979f;
constexpr mgscoop::PartnerDmSpeeds speeds{300, 2600, 5600};
pose::Snapshot At(std::uint64_t t, float yaw, std::uint8_t stance = 0, std::uint8_t flags = sync::ActionFlag::Aiming, float x = 0) {
    pose::Snapshot p{};
    p.sender_ms = static_cast<std::uint32_t>(t);
    p.x = x; p.y = 2; p.z = 0; p.yaw = pose::WrapAngle(yaw);
    p.stance = stance; p.flags = flags; p.equip_id = 873;
    return p;
}
struct Session {
    pose::SnapshotRing ring;
    raw::State source{};
    dm::TurnState turn{};
    dm::State mapper{};
    bool active{};
    bool driver_urgency{};
    std::uint64_t finish_ms{};
    std::vector<raw::Event> emitted;
    std::vector<std::uint8_t> played;
    std::vector<std::uint64_t> played_at;
    std::vector<std::uint64_t> identities;
    std::vector<std::uint64_t> stopped_at;
    std::vector<std::uint8_t> stopped_from;
    unsigned finished{};
    unsigned confirmed{};
    Session() { dm::Reset(mapper); }
    raw::Result Tick(std::uint64_t t, std::uint8_t stance = 0, bool eligible = true, std::uint32_t delay = 100,
                     bool delivery_ready = true) {
        const auto frame = ring.ReadFrame(t, delay, source.cursor);
        const auto result = raw::Step(source, turn, frame, t, eligible, speeds, stance, delivery_ready);
        if (result.cancel_queue) dm::CancelFiniteEvents(mapper);
        if (result.event.token) emitted.push_back(result.event);
        if (active && t >= finish_ms) active = false;
        dm::In in{};
        in.map = eligible; in.target_ok = frame.visual.valid && frame.visual.status == pose::Status::live;
        in.target = stance; in.cls = dm::Class{stance, 0, dm::Via::speed, dm::Via::flags};
        in.dm_ok = in.patched = in.parts_loaded = in.cam_idle = true;
        in.in_dm = active; in.now_ms = t; in.available_ganis = 21;
        in.stance_only_urgent = driver_urgency && dm::StanceUrgency(mapper, in.cls, false);
        in.urgent = result.busy || (driver_urgency && (in.cls.move != 0 || in.stance_only_urgent));
        if (result.busy) in.stance_only_urgent = false;
        if (result.event.token) { in.target = result.event.gani; in.finite_token = result.event.token; in.responsive = true; }
        const auto out = dm::Step(mapper, in);
        if (out.action == dm::Action::stop) {
            active = false;
            stopped_at.push_back(t);
            stopped_from.push_back(out.from);
        }
        if (out.action == dm::Action::play) {
            active = true;
            finish_ms = dm::OneShot(out.gani) ? t + 951 : UINT64_MAX;
            if (dm::OneShot(out.gani)) {
                played.push_back(out.gani); played_at.push_back(t); identities.push_back(mapper.sent_event);
            }
        }
        if (out.event == dm::Event::finished) ++finished;
        if (out.event == dm::Event::playing && dm::OneShot(out.gani)) ++confirmed;
        return result;
    }
    void Push(std::uint64_t t, float yaw, std::uint8_t stance = 0, std::uint8_t flags = sync::ActionFlag::Aiming, float x = 0) {
        Check(ring.Push(At(t, yaw, stance, flags, x), t), "source packet accepted");
    }
};

void History() {
    pose::SnapshotRing ring;
    auto frame = ring.ReadFrame(1000, 100, {});
    Check(frame.reset && frame.count == 0 && !frame.visual.valid, "empty new cursor has explicit generation");
    Check(ring.Push(At(1000, 0), 1000), "history baseline");
    frame = ring.ReadFrame(1000, 100, {});
    Check(frame.reset && frame.count == 1 && frame.records[0].sender_ms == 1000, "new owner receives latest baseline only");
    auto cursor = frame.next;
    Check(!ring.Push(At(1000, 2), 1000) && !ring.Push(At(999, 2), 1001), "duplicate and reordered timestamps refused");
    Check(ring.ReadFrame(1001, 100, cursor).count == 0, "duplicate owner read has no new source record");
    for (unsigned i = 1; i <= 16; ++i) Check(ring.Push(At(1000 + i * 50, i * 0.1f), 1000 + i * 50), "bounded history push");
    frame = ring.ReadFrame(1800, 100, cursor);
    Check(frame.count == 16 && !frame.reset && !frame.overflow, "exactly16 new accepted records are complete, not overflow");
    Check(frame.records[0].sender_ms == 1050 && frame.records[15].sender_ms == 1800, "history remains source ordered");
    Check(std::fabs(frame.visual.yaw - 1.4f) < 0.001f && std::fabs(frame.records[15].snapshot.yaw - 1.6f) < 0.001f,
          "paired visual stays100ms delayed while raw heading is latest");
    Check(ring.Push(At(1850, 1.7f), 1850), "overflow push");
    frame = ring.ReadFrame(1850, 100, cursor);
    Check(frame.overflow && frame.count == 1 && frame.records[0].sender_ms == 1850, "overrun only publishes baseline, never incomplete pivot");
    cursor = frame.next;
    ring.Reset();
    Check(ring.Push(At(10, -1), 1900), "reset accepts replacement source clock");
    frame = ring.ReadFrame(1900, 100, cursor);
    Check(frame.reset && frame.next.generation != cursor.generation && frame.count == 1, "same mission peer restart changes private receive generation");
    pose::SnapshotRing wrap;
    Check(wrap.Push(At(0xFFFFFFF0ull, 0), 2000), "wrap baseline");
    auto c = wrap.ReadFrame(2000, 0, {}).next;
    Check(wrap.Push(At(0x100000020ull, 1), 2048), "wrapped source clock accepted");
    const auto w = wrap.ReadFrame(2048, 0, c);
    Check(w.count == 1 && w.records[0].sender_ms == 0x100000020ll && !w.reset, "source history cursor follows unwrapped clock");
}

float Heading(unsigned scenario, std::uint64_t t) {
    const float fraction = t <= 200 ? 0 : t >= 600 ? 1 : static_cast<float>(t - 200) / 400;
    switch (scenario) {
    case 0: return pi * 0.5f * fraction;
    case 1: return -pi * fraction;
    case 2: return 3.05f + pi * 0.5f * fraction;
    case 3: return 3.14f + (t % 3 == 0 ? -0.002f : 0.002f);
    case 4: // crosses180 classification threshold, then reverses before settling to90
        return t <= 200 ? 0 : t <= 400 ? 2.7f * static_cast<float>(t - 200) / 200 :
               t <= 600 ? 2.7f + (pi * 0.5f - 2.7f) * static_cast<float>(t - 400) / 200 : pi * 0.5f;
    default: return 0;
    }
}
void Timelines() {
    for (const unsigned hz : {20u, 30u, 60u}) for (const unsigned owner : {20u, 60u, 120u}) {
        for (unsigned scenario = 0; scenario <= 4; ++scenario) {
            Session s;
            dm::TurnState legacy{};
            std::uint64_t raw_commit = 0, old_commit = 0;
            unsigned src_frame = 0;
            for (unsigned tick = 0; tick * 1000u / owner <= 1700; ++tick) {
                const std::uint64_t elapsed = tick * 1000u / owner;
                while (src_frame * 1000u / hz <= elapsed) {
                    const auto source_elapsed = src_frame * 1000u / hz;
                    s.Push(10000 + source_elapsed, Heading(scenario, source_elapsed));
                    ++src_frame;
                }
                const auto visual = s.ring.SampleAt(10000 + elapsed, 100);
                const auto old = legacy.event_token;
                dm::ObserveTurn(legacy, 10000 + elapsed, true, visual.yaw, 0, false);
                if (legacy.event_token != old && !old_commit) old_commit = elapsed;
                const auto out = s.Tick(10000 + elapsed);
                if (out.event.token && !raw_commit) raw_commit = elapsed;
            }
            if (scenario == 3) Check(s.emitted.empty(), "wrapped stationary noise never emits a pivot");
            else {
                const std::uint8_t wanted = scenario == 1 ? 15 : 14;
                Check(s.emitted.size() == 1 && s.emitted[0].gani == wanted, "full raw90/180/wrap/reversal angle classified once");
                Check(raw_commit >= 670 && raw_commit <= 670 + 1000u / hz + 1000u / owner + 2,
                      "new source samples span70ms settling and bounded packet/owner quantization");
                Check(old_commit >= raw_commit + 49 && old_commit <= raw_commit + 151,
                      "accepted raw source removes interpolation lag without predicting final angle");
            }
        }
        Session two;
        unsigned src_frame = 0;
        for (unsigned tick = 0; tick * 1000u / owner <= 3300; ++tick) {
            const auto elapsed = tick * 1000u / owner;
            while (src_frame * 1000u / hz <= elapsed) {
                const auto t = src_frame * 1000u / hz;
                const float first = t <= 200 ? 0 : t >= 400 ? 1 : static_cast<float>(t - 200) / 200;
                const float second = t <= 650 ? 0 : t >= 850 ? 1 : static_cast<float>(t - 650) / 200;
                two.Push(20000 + t, -pi * 0.5f * (first + second), 1);
                ++src_frame;
            }
            two.Tick(20000 + elapsed, 1);
        }
        Check(two.emitted.size() == 2 && two.emitted[0].gani == 17 && two.emitted[1].gani == 17 &&
              two.emitted[0].token != two.emitted[1].token, "two physical settled pivots retain distinct identical-clip tokens");
        Check(two.played.size() == 2 && two.identities[0] != two.identities[1] && two.finished == 2 &&
              two.mapper.bad == 0 && two.mapper.timeouts == 0 && two.mapper.unconsumed == 0,
              "actual finite policy plays both once and finishes naturally without strikes");
    }
}

void Interruptions() {
    for (const std::uint8_t flags : {sync::ActionFlag::Firing, sync::ActionFlag::Reloading, sync::ActionFlag::Walking}) {
        Session s;
        s.Push(1000, 0); s.Tick(1000);
        s.Push(1050, 1.0f); s.Tick(1050);
        s.Push(1100, 1.6f, 0, flags);
        Check(s.Tick(1100).cancel_queue && !s.turn.valid && s.source.count == 0, "move/fire/reload cancels raw pivot and waiting deliveries");
        s.Push(1150, 1.6f); s.Tick(1150);
        s.Push(1200, 1.6f); s.Tick(1200);
        s.Push(1250, 1.6f); s.Tick(1250);
        Check(s.emitted.empty(), "busy interruption cannot combine old partial angles into a pivot");
    }
    for (const unsigned reason : {0u, 1u, 2u, 3u, 4u}) {
        Session s;
        s.Push(2000, 0); s.Tick(2000);
        s.Push(2050, 1.6f); s.Tick(2050);
        raw::Result out{};
        if (reason == 0) { s.Push(2200, 1.6f); out = s.Tick(2200); }
        if (reason == 1) { s.ring.Reset(); s.Push(2100, 1.6f); out = s.Tick(2100); }
        if (reason == 2) { s.Push(2100, 1.6f, 1); out = s.Tick(2100, 1); }
        if (reason == 3) { s.Push(2100, 1.6f, 0, sync::ActionFlag::Aiming, 10); out = s.Tick(2100); }
        if (reason == 4) { s.Push(2100, 1.6f); out = s.Tick(2100, 0, false); }
        Check(out.cancel_queue && s.source.count == 0 && s.emitted.empty(), "gap/restart/stance/teleport/disabled context cancels partial collection");
    }
    Session duplicate;
    duplicate.Push(3000, 0); duplicate.Tick(3000);
    duplicate.Push(3050, 1.6f); duplicate.Tick(3050);
    duplicate.Tick(3120); // repeated owner ticks are not evidence that the source settled
    Check(duplicate.emitted.empty() && duplicate.turn.collecting, "duplicate owner sample cannot certify70ms source settling");
    duplicate.Push(3100, 1.6f); duplicate.Tick(3120);
    Check(duplicate.emitted.empty(), "a single new50ms source span cannot certify settling");
    duplicate.Push(3150, 1.6f); duplicate.Tick(3150);
    Check(duplicate.emitted.size() == 1, "two new accepted50ms source intervals confirm one settled pivot");
    Session loss;
    loss.Push(4000, 0); loss.Tick(4000);
    loss.Push(4050, 1.6f); loss.Tick(4050);
    Check(loss.Tick(4160).cancel_queue && !loss.turn.valid, "receive gap clears collector while general pose is still live");
    Session stall;
    stall.Push(5000, 0); stall.Tick(5000);
    stall.Push(5050, 1.6f); stall.Push(5100, 1.6f); stall.Push(5150, 1.6f);
    stall.Push(5600, 3.1f); // latest fresh does not renew old unconsumed history
    Check(stall.Tick(5600).cancel_queue && stall.emitted.empty() && !stall.turn.collecting,
          "owner stall>500ms with fresh latest packet baselines instead of replaying stale pivots");
    Session age;
    age.Push(6000, 0); age.Tick(6000);
    age.Push(6050, 1.6f); age.Push(6100, 1.6f); age.Push(6150, 1.6f);
    age.Push(6800, 3.1f);
    age.source.have_owner = false; // exercise per-record age independently of owner-stall detection
    Check(age.Tick(6800).cancel_queue && age.emitted.empty(), "each historical record retains age even with fresh newest record");
    Session pending;
    pending.Push(7000, 0); pending.Tick(7000);
    pending.source.pending[0] = raw::Event{1, 14, 0, 6400, 6400, pending.source.cursor.generation};
    pending.source.count = 1;
    Check(pending.Tick(7000).cancel_queue && pending.emitted.empty(), "old outbox receive age is not overwritten by current owner time");
}

void VisualReadiness() {
    for (const bool stance_change : {false, true}) {
        Session s;
        s.Push(1000, 0); s.Tick(1000);
        s.Push(1050, 0, 0, sync::ActionFlag::Running);
        s.Tick(1050);
        const std::uint8_t source_stance = stance_change ? 1 : 0;
        // W releases (or the source changes stance) and establishes the new
        // stationary heading while the visual lower class still lags behind.
        s.Push(1100, 0, source_stance);
        s.Tick(1100, 0, true, 100, false);
        s.Push(1150, -1.55f, source_stance);
        s.Tick(1150, 0, true, 100, false);
        s.Push(1200, -1.55f, source_stance);
        s.Tick(1200, 0, true, 100, false);
        s.Push(1250, -1.55f, source_stance);
        s.Tick(1250, 0, true, 100, false);
        Check(s.source.count == 1 && s.emitted.empty() && s.played.empty(),
              "visual move/stance lag retains the complete fresh raw pivot without playing it");
        const auto pending = s.source.pending[0];
        Check(pending.gani == (stance_change ? 17 : 13) && pending.stance == source_stance,
              "retained pivot keeps its raw stance and full signed90degree clip");
        if (stance_change) {
            s.Push(1300, -1.55f, source_stance);
            s.Tick(1300, 0); // Idle alone cannot deliver the wrong stance.
            Check(s.emitted.empty() && s.source.count == 1,
                  "visual idle in the old stance cannot deliver a new-stance pivot");
        }
        const auto ready = stance_change ? 1350u : 1300u;
        s.Push(ready, -1.55f, source_stance);
        const auto out = s.Tick(ready, source_stance);
        Check(out.event.token == pending.token && out.event.receive_ms == pending.receive_ms &&
              out.event.sender_ms == pending.sender_ms && s.source.count == 0,
              "visual readiness delivers the original pivot identity and age exactly once");
        s.Tick(ready + 1, source_stance);
        Check(s.emitted.size() == 1, "duplicate owner ticks cannot replay a retained pivot");
    }
    for (const bool interrupted : {false, true}) {
        Session s;
        for (unsigned t = 2000; t <= 2700; t += 50) {
            const auto flags = interrupted && t >= 2250 ? sync::ActionFlag::Firing : sync::ActionFlag::Aiming;
            s.Push(t, t == 2000 ? 0 : -1.55f, 0, flags);
            s.Tick(t, 0, true, 100, false);
        }
        Check(s.source.count == 0 && s.emitted.empty(),
              "waiting visual readiness does not extend raw-event freshness or survive an actual gun action");
        s.Push(2750, -1.55f); s.Tick(2750);
        Check(s.emitted.empty(), "expired or interrupted retained pivot cannot resurrect at visual readiness");
    }
}

void BoundedHistoryAndDeadline() {
    Session history;
    history.Push(10000, 0); history.Tick(10000);
    for (unsigned i = 1; i <= 15; ++i) {
        const auto t = i * 1000u / 60;
        history.Push(10000 + t, i < 7 ? 1.55f : 3.1f);
    }
    const auto first = history.Tick(10250);
    Check(first.event.gani == 14 && first.event.token != 0 && history.source.count == 1,
          "two settled pivots in one complete history retain a bounded ordered outbox");
    const auto second = history.Tick(10251);
    Check(second.event.gani == 14 && second.event.token != first.event.token && history.source.count == 0 &&
          first.event.receive_ms < second.event.receive_ms && first.event.sender_ms < second.event.sender_ms,
          "one delivery per owner tick preserves both source identities and original ages");
    Check(history.mapper.finite_count == 2 && history.mapper.finite_events[0].id != history.mapper.finite_events[1].id,
          "stop/candidate reconciliation cannot overwrite history event delivery");
    const auto repeated = history.Tick(10252);
    Check(!repeated.event.token && history.mapper.finite_count == 2, "duplicate owner ticks never double-consume bounded history");

    Session deadline;
    std::uint64_t committed = 0;
    for (unsigned i = 0; i <= 15; ++i) {
        const auto t = i * 50u;
        deadline.Push(20000 + t, 3.2f * static_cast<float>(t) / 750);
        const auto out = deadline.Tick(20000 + t);
        if (out.event.token && !committed) committed = t;
    }
    Check(committed == 650 && deadline.emitted.size() == 1 && deadline.emitted[0].gani == 16,
          "unsettled raw pivot retains exact600ms collection bound and180 classification");
    auto frame = deadline.ring.ReadFrame(20750, 100, deadline.source.cursor);
    frame.visual.status = pose::Status::lost;
    const auto lost = raw::Step(deadline.source, deadline.turn, frame, 20750, true, speeds, 0);
    Check(lost.cancel_queue && deadline.source.count == 0 && !deadline.turn.valid,
          "current trusted-link loss cancels even a valid held pose and retained event");

    Session overrun;
    overrun.Push(30000, 0); overrun.Tick(30000);
    for (unsigned i = 1; i <= 18; ++i) overrun.Push(30000 + i * 16, i < 10 ? 1.55f : 3.1f);
    Check(overrun.Tick(30288).cancel_queue && overrun.emitted.empty() && !overrun.turn.collecting,
          "history truncation cannot manufacture the middle of an unobserved pivot");
    Session backwards;
    backwards.Push(40000, 0); backwards.Tick(40000);
    backwards.Push(40050, 1.55f); backwards.Tick(40050);
    Check(backwards.Tick(40049).cancel_queue && backwards.emitted.empty(), "backwards owner clock invalidates source collection");
}

void DriverStanceConfirmation() {
    for (const unsigned case_id : {0u, 1u, 2u}) {
        const bool completing = case_id == 1;
        const bool confirming_turn = case_id == 2;
        Session s;
        s.driver_urgency = true;
        // A native request issued in the old stance reconciles precisely when
        // the delayed visual stance first becomes crouched. No timing fiction:
        // accepted source packets arrive at30Hz and the owner reads at60Hz.
        s.mapper.phase = completing ? dm::Phase::playing : dm::Phase::play_sent;
        s.mapper.cand = 0;
        s.mapper.cand_cls = dm::Class{0, 0, dm::Via::speed, dm::Via::flags};
        s.mapper.sent = confirming_turn ? 14 : 0;
        s.mapper.sent_ms = 1000;
        s.mapper.owned = true;
        s.mapper.cur = completing ? 14 : dm::kNoGani;
        s.mapper.play_ms = completing ? 900 : 0;
        s.active = completing;
        s.finish_ms = UINT64_MAX;
        bool visual_changed = false, delivered = false, checked_next = false;
        std::uint64_t delivered_at = 0;
        unsigned source_frame = 0;
        for (unsigned owner_tick = 0; owner_tick * 1000u / 60 <= 2200; ++owner_tick) {
            const auto elapsed = owner_tick * 1000u / 60;
            while (source_frame * 1000u / 30 <= elapsed) {
                const auto source_elapsed = source_frame * 1000u / 30;
                s.Push(1000 + source_elapsed, source_elapsed >= 66 ? -1.55f : 0,
                       source_elapsed >= 33 ? 1 : 0);
                ++source_frame;
            }
            const auto visual = s.ring.SampleAt(1000 + elapsed, 200);
            if (!visual_changed && visual.stance == 1) {
                visual_changed = true;
                s.active = true;
                s.finish_ms = completing ? 1000 + elapsed : UINT64_MAX;
            }
            const bool was_delivered = delivered;
            const auto result = s.Tick(1000 + elapsed, visual.stance, true, 200);
            if (result.event.token) {
                delivered = true;
                delivered_at = 1000 + elapsed;
                Check(visual_changed && result.event.gani == 17 && s.mapper.finite_count == 1,
                      "delayed crouch pivot queues on old-stance native reconciliation tick");
            }
            if (was_delivered && !checked_next) {
                checked_next = true;
                Check(s.mapper.finite_count == 1,
                      "driver stance urgency cannot cancel retained pivot after an early reconciliation return");
                if (confirming_turn) Check(!s.stopped_at.empty() && s.stopped_at.back() == 1000 + elapsed &&
                    s.stopped_from.back() == 14,
                    "incompatible standing turn stops on the next owner tick without awaiting its natural end");
            }
        }
        Check(delivered && checked_next && s.emitted.size() == 1,
              "stand-to-crouch source pivot is delivered once at delayed visual readiness");
        Check(s.played.size() == 1 && s.played[0] == 17 && s.confirmed == (confirming_turn ? 2u : 1u) &&
              s.finished == (completing ? 2u : 1u),
              "retained crouch turn plays once, confirms and finishes naturally");
        Check(!confirming_turn || (!s.stopped_at.empty() && s.stopped_at[0] <= delivered_at + 17),
              "old-stance turn urgency remains actionable across native confirmation");
        Check(!confirming_turn || (!s.played_at.empty() && s.played_at[0] >= s.stopped_at[0] + 16),
              "stance interruption cannot play the new turn until the stop confirms and its candidate hold passes");
    }
}

void AnimationStanceReseeding() {
    Session s;
    const auto push = [&](std::uint64_t t, float yaw, std::uint8_t anim_stance) {
        auto p = At(t, yaw); // Posture flags remain stand throughout.
        p.anim_valid = true;
        p.anim_stance = anim_stance;
        p.act = anim_stance == 2 ? 3 : 1;
        p.anim_speed = 1;
        Check(s.ring.Push(p, t), "animation-only stance source packet accepted");
    };
    push(1000, 0, 2); s.Tick(1000, 0, true, 100, false);
    for (const unsigned t : {1050u, 1100u, 1150u}) {
        push(t, -1.55f, 2); s.Tick(t, 0, true, 100, false);
    }
    Check(s.source.count == 1 && s.source.pending[0].stance == 1 && s.emitted.empty(),
          "animation-only crouch pivot waits for delayed visual stance");
    push(1200, -1.55f, 1);
    const auto changed = s.Tick(1200, 1);
    Check(changed.cancel_queue && s.source.count == 0 && !changed.event.token &&
          s.turn.valid && !s.turn.collecting && s.turn.stance == 0,
          "effective animation stance change clears old pending crouch pivot and reseeds stand");
    push(1250, -1.55f, 1); s.Tick(1250, 1);
    Check(s.emitted.empty() && s.played.empty(),
          "delayed crouch visual readiness cannot resurrect a canceled animation-stance pivot");
    for (const unsigned t : {1300u, 1350u, 1400u}) {
        push(t, 0, 1); s.Tick(t, 0);
    }
    Check(s.emitted.size() == 1 && s.emitted[0].gani == 14 && s.emitted[0].stance == 0,
          "a fresh stand pivot after animation-stance reseeding retains its own full angle");
}
} // namespace
int main() {
    History(); Timelines(); Interruptions(); VisualReadiness(); BoundedHistoryAndDeadline(); DriverStanceConfirmation(); AnimationStanceReseeding();
    std::printf("partner_source_turns_tests: %u checks passed\n", checks);
}
