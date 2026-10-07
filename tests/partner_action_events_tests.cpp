#include "mgscoop/partner_action_events.hpp"
#include "mgscoop/partner_upper_actions.hpp"
#include "mgscoop/partner_action_mailbox.hpp"
#include "mgscoop/player_sync.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace a = mgscoop::partner_actions;
namespace u = mgscoop::partner_upper_actions;
unsigned checks = 0;
void Check(bool ok, const char* why) { ++checks; if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n", checks, why); std::exit(1); } }
a::Sample Sample(std::uint32_t t, bool fire = false, bool reload = false) {
    a::Sample s{}; s.enabled = s.eligible = true; s.mission = 30050; s.equip = 873; s.tick_ms = t;
    s.raw_fire_active = fire; s.raw_reload_pressed = reload; s.flags = std::uint8_t((fire ? 2 : 0) | (reload ? 8 : 0)); return s;
}
struct Stream {
    a::Producer producer{}; a::Receiver receiver{}; a::Frame f{}; a::Expected expected{77, 0x1122334455667788ull, 4, 30050};
    std::uint64_t now = 1000;
    Stream() { f.session = expected.session; f.epoch = expected.epoch; }
    a::Receipt Send(a::Sample s, std::uint64_t dt = 20) {
        now += dt; Check(a::Observe(producer, s), "valid sample"); f.action = producer.action; ++f.sequence;
        std::uint8_t bytes[a::kFrameSize]; Check(a::Encode(f, bytes) == a::kFrameSize, "encode companion");
        a::Frame decoded{}; Check(a::Decode(bytes, sizeof(bytes), decoded), "decode companion");
        return a::Receive(receiver, decoded, expected.peer, expected, now);
    }
};
void ProducerAndTransport() {
    a::Producer p{}; auto s = Sample(100); s.enabled = false;
    Check(!a::Observe(p, s) && !p.ready, "producer default off");
    s = Sample(100, true, true); Check(a::Observe(p, s) && p.action.count == 0, "first held sample baselines");
    for (unsigned t = 110; t < 210; t += 10) Check(a::Observe(p, Sample(t, true, true)), "hold sample");
    Check(p.action.cursor == 0, "held fire and reload never count repeated events");
    a::Observe(p, Sample(220)); a::Observe(p, Sample(230, true)); a::Observe(p, Sample(240)); a::Observe(p, Sample(250, true));
    a::Observe(p, Sample(260, true, true));
    Check(p.action.fire_count == 2 && p.action.reload_count == 1 && p.action.cursor == 3, "raw edges faster than old latch remain distinct");
    Check(p.action.history[0].tick_ms == 230 && p.action.history[1].tick_ms == 250
        && p.action.history[2].kind == a::EventKind::reload, "event order and original times retained");
    const auto generation = p.action.generation;
    s = Sample(270, true, true); s.equip = 874; a::Observe(p, s);
    Check(p.action.generation == generation + 1 && !p.action.count, "equipment cancels and baselines held state");
    s.stance = 1; ++s.tick_ms; a::Observe(p, s); Check(p.action.generation == generation + 2, "stance generation");
    s.mission = 30020; ++s.tick_ms; a::Observe(p, s); Check(p.action.generation == generation + 3, "mission generation");
    --s.tick_ms; a::Observe(p, s); Check(p.action.generation == generation + 4, "backwards clock resets");
    s.eligible = false; Check(!a::Observe(p, s), "non gameplay cancels");
    s.eligible = true; a::Observe(p, s); Check(p.action.generation == generation + 5 && !p.action.count, "reset preserves generation progression");
    p.generation = 0xffffffffu; a::Clear(p); a::Observe(p, s); Check(p.action.generation == 1, "generation zero skipped");
    a::Clear(p); a::Observe(p, Sample(0xfffffff0u)); a::Observe(p, Sample(10, true));
    Check(p.action.count == 1 && p.action.history[0].tick_ms == 10, "clock rollover is forward");

    Stream x; auto r = x.Send(Sample(100)); Check(r.status == a::ReceiptStatus::baseline, "first wire sample baselines");
    a::Observe(x.producer, Sample(110, true)); a::Observe(x.producer, Sample(120));
    a::Observe(x.producer, Sample(130, false, true)); a::Observe(x.producer, Sample(140));
    r = x.Send(Sample(150, true));
    Check(r.status == a::ReceiptStatus::update && r.count == 3, "loss retains three distinct events");
    Check(r.events[0].kind == a::EventKind::fire && r.events[1].kind == a::EventKind::reload
        && r.events[2].kind == a::EventKind::fire, "mixed ordering retained through loss");
    const auto before = x.receiver;
    Check(a::Receive(x.receiver, x.f, 77, x.expected, x.now + 5).status == a::ReceiptStatus::duplicate
        && x.receiver.received_ms == before.received_ms, "duplicate does not refresh lease or replay");
    auto old = x.f; --old.sequence;
    Check(a::Receive(x.receiver, old, 77, x.expected, x.now + 5).status == a::ReceiptStatus::duplicate, "reordered packet drops");
    auto bad = x.f; ++bad.sequence; ++bad.action.fire_count; ++bad.action.cursor;
    Check(a::Receive(x.receiver, bad, 77, x.expected, x.now).status == a::ReceiptStatus::rejected
        && x.receiver.sequence == before.sequence, "inconsistent history atomic rejection");
    bad = x.f; ++bad.sequence; ++bad.action.equip;
    Check(a::Receive(x.receiver, bad, 77, x.expected, x.now).status == a::ReceiptStatus::rejected, "equip cannot change without generation");
    bad = x.f; ++bad.sequence; --bad.action.generation;
    Check(a::Receive(x.receiver, bad, 77, x.expected, x.now).status == a::ReceiptStatus::rejected, "old generation rejects");
    Check(a::Receive(x.receiver, bad, 77, x.expected, x.now + a::kNetworkAgeMs + 1).status == a::ReceiptStatus::rejected, "expired lease cannot authorize old generation");
    for (unsigned which = 0; which < 4; ++which) {
        bad = x.f; ++bad.sequence; auto peer = x.expected.peer;
        if (which == 0) ++peer; if (which == 1) ++bad.session; if (which == 2) ++bad.epoch; if (which == 3) ++bad.action.mission;
        Check(a::Receive(x.receiver, bad, peer, x.expected, x.now).status == a::ReceiptStatus::rejected, "trusted envelope mismatch rejects");
    }
    r = x.Send(Sample(160), a::kNetworkAgeMs + 1);
    Check(r.status == a::ReceiptStatus::baseline && r.count == 0, "return after loss never replays retained events");
    Check(a::Expired(x.receiver, x.now + a::kNetworkAgeMs + 1) && a::Expired(x.receiver, x.now - 1), "loss/backwards local clock expiry");
    for (unsigned i = 0; i < 10; ++i) { a::Observe(x.producer, Sample(170 + i * 20, true)); a::Observe(x.producer, Sample(180 + i * 20)); }
    r = x.Send(Sample(380)); Check(r.status == a::ReceiptStatus::history_gap && !r.count, "history exceeded baselines instead of fabricating events");
    a::Observe(x.producer, Sample(390, true));
    r = x.Send(Sample(1000)); Check(r.status == a::ReceiptStatus::update && r.expired == 1 && !r.count, "old event expires independently of fresh packet");
    // u32 sequence, event cursor and counts all wrap without aliasing zero.
    x.receiver.sequence = 0xffffffffu; x.f.sequence = 0;
    x.receiver.cursor = 0xffffffffu; x.receiver.fire_count = 0xffffffffu; x.receiver.reload_count = 0;
    x.f.action.cursor = x.f.action.fire_count = x.f.action.reload_count = 0; x.f.action.count = 1;
    x.f.action.history[0] = {0, x.f.action.tick_ms, a::EventKind::fire};
    r = a::Receive(x.receiver, x.f, 77, x.expected, x.now + 1);
    Check(r.status == a::ReceiptStatus::update && r.count == 1 && r.events[0].token == 0, "event counter rollover zero remains an identity");
}
void CodecAndLegacy() {
    Stream x; x.Send(Sample(10)); x.Send(Sample(20, true, true));
    std::uint8_t original[a::kFrameSize]; Check(a::Encode(x.f, original) == a::kFrameSize, "codec valid");
    a::Frame sentinel{}; sentinel.session = 123;
    auto fails = [&](const std::uint8_t* bytes, std::size_t n) {
        auto out = sentinel; Check(!a::Decode(bytes, n, out) && out.session == sentinel.session, "malformed codec atomic failure");
    };
    fails(nullptr, a::kFrameSize); for (std::size_t n = 0; n < a::kFrameSize; ++n) fails(original, n);
    fails(original, a::kFrameSize + 1);
    // Reserved fields, sources, stance, unknown flags, event kind and unused slots.
    for (unsigned off : {0u, 4u, 6u, 24u, 28u, 29u, 61u, 62u, 63u, 73u, 74u, 75u, 88u}) {
        std::uint8_t b[a::kFrameSize]; std::memcpy(b, original, sizeof(b)); b[off] ^= 0x80; fails(b, sizeof(b));
    }
    for (auto spec : {std::array<unsigned,2>{30,3}, {31,3}, {32,3}, {33,4}, {60,9}, {72,3}}) {
        std::uint8_t b[a::kFrameSize]; std::memcpy(b, original, sizeof(b)); b[spec[0]] = std::uint8_t(spec[1]); fails(b, sizeof(b));
    }
    for (unsigned off : {8u,16u,34u,36u,40u}) {
        std::uint8_t b[a::kFrameSize]; std::memcpy(b, original, sizeof(b));
        const auto width = off == 8 ? 8 : off == 34 ? 2 : 4; std::memset(b + off, 0, width); fails(b, sizeof(b));
    }
    auto f = x.f; f.action.flags = 0x80; std::uint8_t out[a::kFrameSize]; std::memset(out, 0xaa, sizeof(out));
    Check(a::Encode(f, out) == 0, "bad encode fails"); for (auto b : out) Check(b == 0, "bad encode zeroes buffer");
    namespace sync = mgscoop::sync;
    sync::PlayerTransformPacket pose{}; pose.sequence = 1; pose.equip_id = 873; pose.action_flags = 0x3f;
    sync::AnimState anim{14,1,2,sync::AnimFlag::Valid};
    std::uint8_t legacy[sync::transform_frame_size], animated[sync::transform_anim_frame_size];
    Check(sync::encode_transform_frame(x.f.session, x.f.epoch, pose, legacy) == sizeof(legacy), "legacy 56 encode unchanged");
    Check(sync::encode_transform_anim_frame(x.f.session, x.f.epoch, pose, anim, animated) == sizeof(animated), "legacy 64 encode unchanged");
    std::uint64_t session = 0; std::uint32_t epoch = 0; sync::PlayerTransformPacket decoded{}; sync::AnimState da{}; bool has = false;
    Check(sync::decode_partner_transform_frame(legacy, sizeof(legacy), session, epoch, decoded, da, has) && !has, "legacy 56 still consumed");
    Check(sync::decode_partner_transform_frame(animated, sizeof(animated), session, epoch, decoded, da, has) && has && decoded.action_flags == 0x3f, "legacy 64 all action flags consumed");
    Check(!sync::decode_partner_transform_frame(original, sizeof(original), session, epoch, decoded, da, has), "movement decoder rejects distinct action companion");
    fails(legacy, sizeof(legacy)); fails(animated, sizeof(animated));
    const char* receipt = "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 11 1 1 2 2 1 20 1 2 20 2";
    a::Payload parsed{}; Check(a::DecodeReceipt(receipt, std::strlen(receipt), parsed) && parsed.cursor == 2
        && parsed.fire_source == a::FireSource::observed_fire_state_rise, "strict natural Lua receipt labels provenance");
    for (const char* malformed : {
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 0 0 0 0 0 1",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 0 0 0 0 1",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 4294967296 0 0 0 0 0",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 -1 0 0 0 0 0",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 1e3 0 0 0 0 0",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 0 0 0 0 0 ",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 0 0 0 0 0 -- extra",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 0 0 0 0 0\n",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 4 0 0 0 0",
        "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 1 30050 873 0 20 0 1 0 1 1 1 20 3"
    }) {
        parsed.generation = 99;
        Check(!a::DecodeReceipt(malformed, std::strlen(malformed), parsed) && parsed.generation == 99, "malformed Lua receipt atomically rejected");
    }
    const char* wrapped = "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- 4294967295 30050 873 0 4294967295 0 0 0 0 1 0 4294967295 1";
    Check(a::DecodeReceipt(wrapped, std::strlen(wrapped), parsed), "full unsigned receipt supports wrapped event token zero");
}
void Mailbox() {
    const mgscoop::lab::Endpoint expected{0x7f000001,47821}; std::uint64_t key = 0;
    Check(a::TrustedPeer(false,false,expected,expected,key) && key == mgscoop::lab::PackEndpoint(expected), "actual configured callback endpoint derives raw identity");
    const auto before_key = key;
    Check(!a::TrustedPeer(false,false,{0x7f000001,47822},expected,key) && key == before_key, "foreign callback port cannot derive trusted raw identity");
    Check(!a::TrustedPeer(false,false,{0x7f000002,47821},expected,key), "foreign callback address cannot derive trusted raw identity");
    Check(!a::TrustedPeer(true,false,expected,{},key), "unauthenticated link callback cannot derive identity");
    Check(a::TrustedPeer(true,true,{0x0a000002,50000},{},key), "accepted link delivery can carry its actual authenticated endpoint");
    Stream x; a::Mailbox inbox{}; x.Send(Sample(10));
    Check(a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1000), "first bounded history stored");
    a::Receipt r{}; std::uint32_t age = 0;
    Check(a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30050,1000,true,r,age) && r.status == a::ReceiptStatus::baseline, "owner mailbox establishes first baseline");
    x.Send(Sample(20,true)); a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1010);
    x.Send(Sample(30)); a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1020);
    x.Send(Sample(40,false,true)); a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1030);
    x.Send(Sample(50)); a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1040);
    x.Send(Sample(60,true)); a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1050);
    Check(a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30050,1060,true,r,age) && r.count == 3
        && r.events[0].kind == a::EventKind::fire && r.events[1].kind == a::EventKind::reload && r.events[2].kind == a::EventKind::fire,
        "multiple receive callbacks cannot overwrite unconsumed ordered owner events");
    Check(age == 10 && inbox.receiver.received_ms == 1050, "lease uses receive time rather than owner sample time");
    Check(a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30050,1070,true,r,age) && r.status == a::ReceiptStatus::duplicate && !r.count, "repeated owner sample does not replay history");
    Check(!a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30020,1070,true,r,age), "owner verified mission mismatch fails");
    Check(!a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30050,1401,true,r,age), "stale callback cannot refresh lease by sampling");
    Check(!a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30050,1070,false,r,age), "lost authenticated link refuses fresh cached actions");
    auto bad = x.f; ++bad.sequence; bad.action.tick_ms = 1;
    Check(!a::Store(inbox,bad,key,x.expected.session,x.expected.epoch,1060), "backwards sender time rejected before overwriting pending history");
    bad = x.f; ++bad.sequence; --bad.action.generation;
    Check(!a::Store(inbox,bad,key,x.expected.session,x.expected.epoch,1060), "old source generation rejects before owner sample");
    bad = x.f; ++bad.sequence;
    Check(!a::Store(inbox,bad,0,x.expected.session,x.expected.epoch,1060), "mailbox cannot accept identity from payload");
    Check(!a::Store(inbox,bad,key,x.expected.session,x.expected.epoch+1,1060), "epoch guard preserves pending history");
    for (unsigned i=0; i<10; ++i) { x.Send(Sample(70+i*20)); a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1080+i*20);
        x.Send(Sample(80+i*20,true)); a::Store(inbox,x.f,key,x.expected.session,x.expected.epoch,1090+i*20); }
    Check(a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30050,1290,true,r,age) && r.status == a::ReceiptStatus::history_gap && !r.count,
        "more than eight unconsumed events reports a gap instead of fabricating history");
    a::Reset(inbox); Check(!a::SampleMailbox(inbox,x.expected.session,x.expected.epoch,30050,1300,true,r,age), "new link/mission/reset clears pending remote data");
}
struct Backend {
    u::State state{}; u::In in{}; Stream stream{}; unsigned plays = 0, stops = 0;
    Backend() { in.enabled = in.play_verified = in.stop_verified = in.body_ready = in.fresh = true; in.now_ms = 1000;
        in.profile = {873, 0, 21, 24, 25, true, true, true}; }
    u::Command Tick(a::Sample sample, u::FeedbackStatus status = u::FeedbackStatus::none, std::uint64_t command = 0, std::uint64_t dt = 20) {
        in.now_ms += dt; in.receipt = stream.Send(sample, dt); in.feedback = {command, status}; return Step();
    }
    u::Command Step() { const auto out = u::Step(state, in); if (out.action == u::Action::play) ++plays; if (out.action == u::Action::stop) ++stops;
        Check(out.channel == 2, "policy never selects lower locomotion channel"); return out; }
};
void Sequencer() {
    Backend b; auto out = b.Tick(Sample(100)); Check(out.action == u::Action::none, "baseline idle");
    out = b.Tick(Sample(120, true)); const auto first = out.id;
    Check(out.action == u::Action::play && out.gani == 24 && !out.loop, "observed burst finite fire");
    b.Tick(Sample(140), u::FeedbackStatus::accepted, first);
    out = b.Tick(Sample(160, true)); Check(out.action == u::Action::none && b.state.count == 1, "identical second fire queues while first owns upper");
    out = b.Tick(Sample(180), u::FeedbackStatus::completed, first);
    Check(out.action == u::Action::play && out.gani == 24 && out.id != first && out.event.token == 2, "natural completion immediately admits distinct identical request");
    const auto second = out.id;
    b.Tick(Sample(200), u::FeedbackStatus::accepted, second);
    out = b.Tick(Sample(220, false, true), u::FeedbackStatus::completed, second);
    Check(out.action == u::Action::play && out.gani == 25 && !out.loop, "reload requested exactly once even on completion tick");
    const auto reload = out.id;
    b.Tick(Sample(240, false, true), u::FeedbackStatus::accepted, reload);
    out = b.Tick(Sample(260, false, true), u::FeedbackStatus::completed, reload);
    Check(out.action == u::Action::none && b.plays == 3 && b.stops == 0, "held reload and latched flags never replay finite action");
    // Native pending state never serves as proof of acceptance or completion.
    b.in.feedback = {first, u::FeedbackStatus::completed}; b.Step(); Check(b.state.phase == u::Phase::idle, "late previous completion cannot affect ownership");
    auto aim = Sample(280); aim.flags = 1; out = b.Tick(aim); const auto hold = out.id;
    Check(out.gani == 21 && out.loop, "aim enters upper hold loop");
    aim.tick_ms = 300; b.Tick(aim, u::FeedbackStatus::accepted, hold);
    aim.tick_ms = 320; aim.raw_fire_active = true; aim.flags = 3; out = b.Tick(aim); const auto stop = out.id;
    Check(out.action == u::Action::stop && b.state.count == 1, "finite arrival waits for upper hold stop");
    out = b.Tick(Sample(340), u::FeedbackStatus::stopped, stop);
    Check(out.action == u::Action::play && out.gani == 24, "verified stop admits fire without touching lower");

    for (unsigned gate = 0; gate < 5; ++gate) {
        Backend off; off.Tick(Sample(10));
        if (gate == 0) off.in.enabled = false; if (gate == 1) off.in.play_verified = false; if (gate == 2) off.in.stop_verified = false;
        if (gate == 3) off.in.body_ready = false; if (gate == 4) off.in.fresh = false;
        Check(off.Tick(Sample(20, true)).action == u::Action::none && !off.state.count, "capability/runtime gate prevents auto playback and latent queue");
    }
    for (unsigned cancel = 0; cancel < 7; ++cancel) {
        Backend c; c.Tick(Sample(10)); const auto cmd = c.Tick(Sample(20, true));
        auto s = Sample(30);
        if (cancel == 0) s.mission = c.stream.expected.mission = 30020;
        if (cancel == 1) ++s.equip; if (cancel == 2) s.stance = 1;
        if (cancel == 3) c.in.fresh = false; if (cancel == 4) c.in.reset = true; if (cancel == 5) c.in.enabled = false;
        if (cancel == 6) c.in.now_ms -= 100;
        out = c.Tick(s, u::FeedbackStatus::accepted, cmd.id);
        Check(out.action == u::Action::stop && !c.state.count, "mission/equip/stance/loss/reset/disable/time reset cancels upper");
        const auto cancel_id = out.id;
        out = c.Tick(s, u::FeedbackStatus::stopped, cancel_id);
        Check(c.state.phase != u::Phase::stopping && out.action != u::Action::stop, "cancellation ack consumed during disable or next context");
    }
    Backend stalled; stalled.Tick(Sample(10)); out = stalled.Tick(Sample(20, true));
    stalled.in.receipt = {}; stalled.in.feedback = {}; stalled.in.now_ms += u::kAcceptDeadlineMs;
    out = stalled.Step(); Check(out.action == u::Action::stop, "unaccepted play bounded by deadline");
    stalled.in.now_ms += u::kAcceptDeadlineMs; stalled.Step(); Check(stalled.state.phase == u::Phase::faulted, "unverified stop result faults rather than overlapping another play");
    stalled.in.now_ms += 1; Check(stalled.Step().action == u::Action::none, "fault stays silent");
    Backend finite; finite.Tick(Sample(10)); out = finite.Tick(Sample(20, true));
    finite.Tick(Sample(30), u::FeedbackStatus::accepted, out.id);
    finite.in.now_ms += u::kEndDeadlineMs; finite.in.receipt = {}; finite.in.feedback = {};
    Check(finite.Step().action == u::Action::stop, "finite missing natural completion deadline bounded");
    Backend expire; expire.Tick(Sample(10)); out = expire.Tick(Sample(20, true)); const auto active = out.id;
    expire.Tick(Sample(30), u::FeedbackStatus::accepted, active); expire.Tick(Sample(40, true));
    expire.in.now_ms += a::kEventAgeMs + 1; expire.in.receipt = {}; expire.in.feedback = {active,u::FeedbackStatus::completed};
    Check(expire.Step().action == u::Action::none && expire.state.dropped == 1, "queued old event expires instead of delayed replay");
    Backend duplicate; duplicate.Tick(Sample(10)); out = duplicate.Tick(Sample(20, true));
    duplicate.Step(); duplicate.Step(); Check(duplicate.plays == 1 && duplicate.state.count == 0, "repeated receipt cannot duplicate queue");
    Backend overflow; overflow.Tick(Sample(10)); out = overflow.Tick(Sample(20,true));
    overflow.Tick(Sample(30),u::FeedbackStatus::accepted,out.id);
    for (unsigned i = 0; i < 10; ++i) { overflow.Tick(Sample(40+i*20,true)); overflow.Tick(Sample(50+i*20)); }
    Check(overflow.state.count == a::kHistory && overflow.state.dropped == 2, "fixed queue bounds burst flood without stopping active finite");
    Backend reconnect; reconnect.Tick(Sample(10)); out = reconnect.Tick(Sample(20,true));
    reconnect.Tick(Sample(30),u::FeedbackStatus::accepted,out.id);
    out = reconnect.Tick(Sample(400),u::FeedbackStatus::none,0,a::kNetworkAgeMs+1);
    Check(out.action == u::Action::stop && !reconnect.state.count, "fresh baseline after lease gap cancels previous upper ownership");
    for (unsigned guard = 0; guard < 4; ++guard) {
        Backend proof; proof.Tick(Sample(10));
        if (guard == 0) proof.in.profile = {}; if (guard == 1) ++proof.in.profile.equip;
        if (guard == 2) proof.in.profile.fire_verified = false; if (guard == 3) ++proof.in.profile.stance;
        Check(proof.Tick(Sample(20,true)).action == u::Action::none, "per-clip equipment/stance proof required");
    }
    Backend sniper; sniper.in.profile = {873,0,255,29,255,false,true,false};
    sniper.Tick(Sample(10)); auto requested_aim = Sample(20); requested_aim.flags = 1;
    Check(sniper.Tick(requested_aim).action == u::Action::none, "single finite proof never authorizes unknown hold");
    out = sniper.Tick(Sample(30,true)); Check(out.action == u::Action::play && out.gani == 29 && !out.loop, "explicit verified finite profile selects proven clip");
    const auto sniper_id = out.id; sniper.Tick(Sample(40),u::FeedbackStatus::accepted,sniper_id);
    Check(sniper.Tick(Sample(50,false,true),u::FeedbackStatus::completed,sniper_id).action == u::Action::none, "single finite proof never authorizes unknown reload");
    u::State default_state{}; u::In default_in{};
    Check(u::Step(default_state, default_in).action == u::Action::none, "sequencer default off");
}
int main(int argc, char** argv) {
    ProducerAndTransport(); CodecAndLegacy(); Mailbox(); Sequencer();
    if (argc == 2) {
        std::FILE* file = nullptr;
#ifdef _MSC_VER
        const auto opened = fopen_s(&file, argv[1], "rb"); Check(opened == 0 && file != nullptr, "open real Lua receipt fixture");
#else
        file = std::fopen(argv[1], "rb"); Check(file != nullptr, "open real Lua receipt fixture");
#endif
        char source[512]; const auto n = std::fread(source,1,sizeof(source),file); const bool complete = std::feof(file) != 0; std::fclose(file);
        a::Payload p{}; Check(complete && a::DecodeReceipt(source,n,p), "actual Lua unsigned receipt parsed by native policy");
        Check(p.fire_count == 2 && p.reload_count == 1 && p.cursor == 3 && p.count == 3 && p.history[0].tick_ms == 1010
            && p.history[1].kind == a::EventKind::reload && p.history[2].tick_ms == 1030, "cross-language event counts/order/timestamps agree");
    }
    std::printf("PASS %u action event/upper policy checks (no game/backend integration)\n", checks);
}
