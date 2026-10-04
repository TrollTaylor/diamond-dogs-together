// Offline partner spike: the receipt state machine (partner_driver) on
// deterministic timelines with fake effects. No game, no files, no network:
// every read, guarded write, trigger, pose sample and log line goes through a
// recording Hooks implementation (the observer wires the real ones).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "mgscoop/partner_driver.hpp"
#include "partner_player_contract.hpp"

namespace {
namespace pd = mgscoop::partner_driver;
namespace pp = mgscoop::partner_player;
namespace partner = mgscoop::partner;
namespace op = mgscoop::partner::op;
namespace c = mgscoop::partner_player_contract;

unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}

// Recording fake of every effect. Writes emulate partner_player's rule that
// set-type writes are refused while terminating and restorations are not.
struct Fake {
    std::vector<std::string> lines;
    pp::State state{};
    pp::Refusal refusal = pp::Refusal::none;
    std::vector<std::string> writes;
    const char* write_result = nullptr;  // forced refusal for every write
    std::uint8_t policy_byte = 0;
    mgscoop::pose::Sample sample{};
    unsigned submits = 0, samples = 0;
    std::string trigger;
    bool trigger_present = false, trigger_unreadable = false;
    unsigned trigger_takes = 0;
    bool focused = true;
    bool anim_ok = false;
    pp::Anim anim{};
    mgscoop::sync::AnimState last_submit_anim{};
    // Animation / movement / appearance fakes.
    bool looks_ok = true;
    pp::Looks looks{};
    std::vector<mgscoop::partner::Look> sent_looks;
    pd::RemoteLook remote{};
    const char* anim_write_result = nullptr;
    std::vector<std::string> calls;          // "anim:a,s,v", "fe:n", "stance:s", "req:s", "xform:reset"
    const char* stance_result = nullptr;
    std::uint8_t stance_psw = 0;             // psw reported back by set_stance (0 = s|s<<4)
    const char* request_result = nullptr;
    const char* xform_result = nullptr;
    float xform_pos[4]{}, xform_quat[4]{};
    bool body_ok = false;
    float body_pos[4]{}, body_quat[4] = {0, 0, 0, 1};
    pp::Census census{};
    const char* census_refusal = nullptr;
    unsigned census_runs = 0;
    unsigned ps_probes = 0;

    Fake() {
        Ready();
        looks.look[0] = mgscoop::partner::Look{0, 1, 0, 1, 2, 2};
        looks.look[1] = mgscoop::partner::Look{0xFF, 0xFF, 0xFF, 0xFF, 0x7FFF, 0};
    }
    // Lines containing `part` anywhere.
    std::size_t Containing(const char* part) const {
        std::size_t n = 0;
        for (const auto& line : lines) if (line.find(part) != std::string::npos) ++n;
        return n;
    }
    std::size_t Calls(const char* prefix) const {
        std::size_t n = 0;
        for (const auto& call : calls) if (call.compare(0, std::strlen(prefix), prefix) == 0) ++n;
        return n;
    }
    void Ready() {
        state = pp::State{};
        state.P = 0x1000; state.Q = 0x2000; state.S = 0x3000; state.exists = 0x4000; state.slots = 0x5000;
        state.flags = c::flag_valid;
        state.count = 2; state.realized_count = 2; state.local = 0;
        state.ids[0] = 0x0000; state.ids[1] = 0xFFFF;
        state.pool[0] = 0x00; state.pool[1] = 0xFF;
        state.parts[0] = 3; state.parts[1] = 0;
        state.arrays_ok = state.bits_ok = state.slots_ok = true;
        state.s0 = 0x01; state.q170 = 0x1; state.q194 = 0x1; state.q198 = 0x1;
        state.vars_ok = true; state.vars_pos[0] = 1.0f; state.vars_pos[1] = 2.0f; state.vars_pos[2] = 3.0f;
        state.mirror_object = 0x9000;
    }
    void Realize(bool on) {
        state.ids[1] = on ? 0x0001 : 0xFFFF;
        state.pool[1] = on ? 0x01 : 0xFF;
    }
    std::size_t Count(const char* prefix) const {
        std::size_t n = 0;
        for (const auto& line : lines) if (line.compare(0, std::strlen(prefix), prefix) == 0) ++n;
        return n;
    }
    std::size_t Writes(const char* name) const {
        std::size_t n = 0;
        for (const auto& w : writes) if (w == name) ++n;
        return n;
    }
    bool Has(const char* exact) const {
        for (const auto& line : lines) if (line == exact) return true;
        return false;
    }
    std::string Last() const { return lines.empty() ? std::string() : lines.back(); }
};
Fake& F(void* context) { return *static_cast<Fake*>(context); }

void HookLog(void* context, const char* line) noexcept { F(context).lines.emplace_back(line); }
pp::Refusal HookRead(void* context, pp::State& out) noexcept {
    out = F(context).state;
    return F(context).refusal;
}
const char* WriteResult(Fake& f, const char* name, bool terminating, bool set) noexcept {
    if (f.write_result) return f.write_result;
    if (terminating && set) return "terminating";
    f.writes.emplace_back(name);
    return nullptr;
}
const char* HookOr(void* context, bool terminating, std::uint32_t& q194, std::uint32_t& q198) noexcept {
    Fake& f = F(context);
    if (const char* refused = WriteResult(f, "or", terminating, true)) return refused;
    f.state.q194 |= 2u; f.state.q198 |= 2u;
    q194 = f.state.q194; q198 = f.state.q198;
    return nullptr;
}
const char* HookClear(void* context, bool terminating, std::uint32_t& q194, std::uint32_t& q198) noexcept {
    Fake& f = F(context);
    if (const char* refused = WriteResult(f, "clear", terminating, false)) return refused;
    f.state.q194 &= ~2u; f.state.q198 &= ~2u;
    q194 = f.state.q194; q198 = f.state.q198;
    return nullptr;
}
const char* HookSlot(void* context, bool terminating, const float (&xyzw)[4]) noexcept {
    Fake& f = F(context);
    if (const char* refused = WriteResult(f, "slot", terminating, true)) return refused;
    std::memcpy(f.state.slot[1], xyzw, sizeof(xyzw));
    return nullptr;
}
const char* HookForce(void* context, bool terminating) noexcept {
    Fake& f = F(context);
    if (const char* refused = WriteResult(f, "force", terminating, true)) return refused;
    return nullptr;  // the game keeps rewriting S0 in these timelines
}
const char* HookPolicy(void* context, bool relax, std::uint8_t restore, bool terminating, std::uint8_t& previous) noexcept {
    Fake& f = F(context);
    if (const char* refused = WriteResult(f, relax ? "relax" : "restore", terminating, relax)) return refused;
    previous = f.policy_byte;
    f.policy_byte = relax ? 1 : restore;
    return nullptr;
}
bool HookAnim(void* context, const pp::State&, pp::Anim& out) noexcept {
    out = F(context).anim;
    return F(context).anim_ok;
}
mgscoop::pose::Sample HookSample(void* context, std::uint32_t) noexcept {
    ++F(context).samples;
    return F(context).sample;
}
void HookSubmit(void* context, const partner::Tick&, const mgscoop::sync::AnimState& anim) noexcept {
    ++F(context).submits;
    F(context).last_submit_anim = anim;
}
bool HookReadLooks(void* context, const pp::State&, pp::Looks& out) noexcept {
    out = F(context).looks;
    return F(context).looks_ok;
}
bool HookSubmitLook(void* context, const partner::Look& look) noexcept {
    F(context).sent_looks.push_back(look);
    return true;
}
pd::RemoteLook HookSampleLook(void* context) noexcept { return F(context).remote; }
std::string Text(const char* format, unsigned a, unsigned b = 0, unsigned c2 = 0) {
    char text[64]{};
    std::snprintf(text, sizeof(text), format, a, b, c2);
    return text;
}
const char* HookWriteAnim(void* context, bool terminating, std::uint16_t act, std::uint8_t stance, std::uint8_t speed) noexcept {
    Fake& f = F(context);
    if (terminating) return "terminating";
    if (f.anim_write_result) return f.anim_write_result;
    f.calls.push_back(Text("anim:%u,%u,%u", act, stance, speed));
    return nullptr;
}
const char* HookWriteFaceEquip(void* context, bool, std::uint8_t value) noexcept {
    F(context).calls.push_back(Text("fe:%u", value));
    return nullptr;
}
const char* HookSetStance(void* context, bool, std::uint8_t stance, std::uint8_t& psw, std::uint8_t& q30) noexcept {
    Fake& f = F(context);
    if (f.stance_result) return f.stance_result;
    f.calls.push_back(Text("stance:%u", stance));
    psw = f.stance_psw ? f.stance_psw : static_cast<std::uint8_t>(stance | (stance << 4));
    q30 = stance;
    f.census.k[1].psw = psw;
    return nullptr;
}
const char* HookRequestStance(void* context, bool, std::uint8_t stance) noexcept {
    Fake& f = F(context);
    if (f.request_result) return f.request_result;
    f.calls.push_back(Text("req:%u", stance));
    return nullptr;
}
const char* HookApplyTransform(void* context, bool, const float (&pos)[4], const float (&quat)[4], mgscoop::PartnerReset reset) noexcept {
    Fake& f = F(context);
    if (f.xform_result) return f.xform_result;
    f.calls.push_back(std::string("xform:") + mgscoop::PartnerResetName(reset));
    std::memcpy(f.xform_pos, pos, sizeof(pos));
    std::memcpy(f.xform_quat, quat, sizeof(quat));
    return nullptr;
}
bool HookReadBody(void* context, const pp::State&, float (&pos)[4], float (&quat)[4]) noexcept {
    Fake& f = F(context);
    std::memcpy(pos, f.body_pos, sizeof(pos));
    std::memcpy(quat, f.body_quat, sizeof(quat));
    return f.body_ok;
}
const char* HookCensus(void* context, const pp::State&, pp::Census& out) noexcept {
    Fake& f = F(context);
    ++f.census_runs;
    out = f.census;
    out.refusal = f.census_refusal;
    return f.census_refusal;
}
std::size_t HookFormatCensus(void*, const pp::Census& census, std::uint32_t k, char* out, std::size_t capacity) noexcept {
    const int n = std::snprintf(out, capacity, "partner_census; k=%u; verdict=%s\r\n", k, census.refusal ? census.refusal : "ok");
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}
std::size_t HookFormatPs(void* context, const pp::State&, std::uint32_t k, char* out, std::size_t capacity) noexcept {
    ++F(context).ps_probes;
    const int n = std::snprintf(out, capacity, "partner_ps; k=%u\r\n", k);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}
pd::TriggerRead HookTrigger(void* context, char* text, std::size_t capacity, std::size_t& length) noexcept {
    Fake& f = F(context);
    ++f.trigger_takes;
    length = 0;
    if (!f.trigger_present) return pd::TriggerRead::none;
    f.trigger_present = false;  // consumed and deleted
    if (f.trigger_unreadable || f.trigger.size() > capacity) return pd::TriggerRead::unreadable;
    std::memcpy(text, f.trigger.data(), f.trigger.size());
    length = f.trigger.size();
    return pd::TriggerRead::read;
}
bool HookFocused(void* context) noexcept { return F(context).focused; }

pd::Hooks HooksFor(Fake& f) {
    return pd::Hooks{&f, HookLog, HookRead, HookOr, HookClear, HookSlot, HookForce, HookPolicy, HookAnim, HookSample,
                     HookSubmit, HookTrigger, HookFocused, HookReadLooks, HookSubmitLook, HookSampleLook, HookWriteAnim,
                     HookWriteFaceEquip, HookSetStance, HookRequestStance, HookApplyTransform, HookReadBody, HookCensus,
                     HookFormatCensus, HookFormatPs};
}

std::string TickSource(int frame, int luaflags, int x = 1000, int y = 2000, int z = 3000, int yaw = 4500, int cam = 0) {
    char text[200]{};
    std::snprintf(text, sizeof(text), "return 'MGS_COOP_PARTNER_V1' -- %d %d %d %d %d %d 0 0 %d", frame, x, y, z, yaw, cam,
                  luaflags);
    return text;
}

// One mission: configured, started, CFG accepted at t=1000 ms.
struct Session {
    Fake fake;
    pd::Hooks hooks;
    pd::Driver driver{};
    mgscoop::LabConfig lab{};
    std::uint64_t now = 1000;
    int frame = 0;
    partner::Reply reply{};

    explicit Session(std::uint32_t stage, mgscoop::PartnerApply apply = mgscoop::PartnerApply::warp,
                     mgscoop::PartnerSource source = mgscoop::PartnerSource::fixed) : hooks(HooksFor(fake)) {
        lab.partner = true;
        lab.partner_stage = stage;
        lab.partner_apply = apply;
        lab.partner_source = source;
        pd::Configure(driver, lab);
        pd::MissionStart(driver, hooks);
        fake.state.flags = c::flag_valid | c::flag_all_active;  // as after Lua's SET
        Check(pd::Cfg(driver, hooks, nullptr, now), "cfg ready");
        fake.lines.clear();
    }
    // One frame: advances the clock, returns whether the tick was replied.
    bool Tick(int luaflags, std::uint64_t step_ms = 33, bool terminating = false) {
        now += step_ms;
        const std::string source = TickSource(++frame, luaflags);
        reply = partner::Reply{};
        return pd::Tick(driver, hooks, lab, source.data(), source.size(), pd::Clock{now, now + 5000}, terminating, reply);
    }
    void Trigger(const char* text) {
        fake.trigger = text;
        fake.trigger_present = true;
        pd::PollTrigger(driver, hooks, false);
    }
};

void TestCfgAndMissionBoundaries() {
    Fake f;
    const pd::Hooks hooks = HooksFor(f);
    pd::Driver d{};
    mgscoop::LabConfig lab{};
    lab.partner_stage = 2;
    lab.partner_source = mgscoop::PartnerSource::orbit;
    lab.partner_apply = mgscoop::PartnerApply::both;
    pd::Configure(d, lab);
    Check(d.settings.stage == 2 && d.settings.source == mgscoop::PartnerSource::orbit &&
          d.settings.apply == mgscoop::PartnerApply::both && !d.mission.cfg_ready, "configure from lab.ini");
    // A trigger left over from a previous mission (or the heli) is discarded unapplied.
    f.trigger = "stage=3";
    f.trigger_present = true;
    d.parts_explicit_pending = d.mirror_pending = true;
    pd::MissionStart(d, hooks);
    Check(f.lines.size() == 1 && f.Last() == "partner_trigger=discarded; reason=stale\r\n" && !f.trigger_present,
          "stale trigger discarded at mission start");
    Check(d.settings.stage == 2 && !d.parts_explicit_pending && !d.mirror_pending, "stage kept, one-shots dropped");
    pd::MissionStart(d, hooks);
    Check(f.lines.size() == 1, "no trigger: nothing logged");
    // No tick before CFG.
    partner::Reply reply{};
    const std::string tick = TickSource(1, 0);
    Check(!pd::Tick(d, hooks, lab, tick.data(), tick.size(), pd::Clock{2000, 2000}, false, reply) && f.lines.size() == 1,
          "no tick before CFG");
    Check(!pd::Cfg(d, hooks, "mission", 1000) && f.Last() == "partner_cfg=refused; reason=mission\r\n", "precheck refusal");
    f.refusal = pp::Refusal::online_bit;
    Check(!pd::Cfg(d, hooks, nullptr, 1000) && f.Last() == "partner_cfg=refused; reason=online_bit\r\n", "state refusal");
    f.refusal = pp::Refusal::sync_present;
    Check(!pd::Cfg(d, hooks, nullptr, 1000) && f.Last() == "partner_cfg=refused; reason=sync_present\r\n", "synchronizer refusal");
    f.refusal = pp::Refusal::none;
    Check(pd::Cfg(d, hooks, nullptr, 1000) && d.mission.cfg_ready &&
          f.Last() == "partner_cfg=ready; flags=0x00000001; count=2; realized_count=2; local=0; R=1; online_bit=0; sync=null; code_gate=ok\r\n",
          "ready line");
    Check(pd::Tick(d, hooks, lab, tick.data(), tick.size(), pd::Clock{2000, 2000}, false, reply), "ticks after CFG");
    Check(!pd::Tick(d, hooks, lab, tick.data(), tick.size(), pd::Clock{2033, 2033}, true, reply), "no tick while terminating");
    // Mission end: no more ticks; one-shots dropped.
    d.parts_explicit_pending = d.mirror_pending = true;
    pd::MissionEnd(d);
    Check(!d.mission.cfg_ready && !d.parts_explicit_pending && !d.mirror_pending, "mission end resets");
    Check(!pd::Tick(d, hooks, lab, tick.data(), tick.size(), pd::Clock{2066, 2066}, false, reply), "no tick after mission end");
}

void TestStageThreeTimeline() {
    Session s(3);
    // Tick 1: the first pose is applied (WARP), no realize bits yet.
    Check(s.Tick(0), "tick 1 replied");
    Check(s.reply.op == (op::warp | op::set_all_active | op::copy_parts) && s.reply.index == 1, "tick 1 ops");
    Check(s.reply.x_mm == 1000 && s.reply.y_mm == 2000 && s.reply.z_mm == 6000 && s.reply.yaw_mrad == 785 &&
          s.reply.status == partner::Status::synthetic && s.reply.age_ms == 0, "fixed pose: 3 m along camera 0");
    Check(s.fake.Writes("or") == 0 && s.fake.Count("partner_bits=set") == 0, "no bits before the first applied pose");
    Check(s.fake.Count("partner_units; lua_pos_mm=1000,2000,3000; mem_pos_mm=1000,2000,3000; yaw_cdeg=4500;") == 1 &&
          s.fake.Count("partner_probe=state; t_ms=6033; stage=3; ids=0000,ffff; pool=00,ff;") == 1, "units and first probe");
    Check(s.fake.submits == 1, "local pose submitted");
    // Tick 2: bits ORed once, logged with the values read back.
    Check(s.Tick(1), "tick 2");
    Check(s.fake.Writes("or") == 1 && s.fake.Has("partner_bits=set; R=1; q194=0x3; q198=0x3\r\n"), "bits set on tick 2");
    Check(s.reply.op == (op::warp | op::copy_parts), "Lua latched all-active: SET not resent");
    Check(s.Tick(1) && s.fake.Writes("or") == 1, "bits not re-ORed while present");
    // warp_check compares the previous command with the slot (1 Hz).
    s.fake.state.slot[1][0] = 1.0f; s.fake.state.slot[1][1] = 2.0f; s.fake.state.slot[1][2] = 6.004f;
    Check(s.Tick(1, 1000), "tick after 1 s");
    Check(s.fake.Has("partner_warp_check; R=1; apply=warp; cmd_mm=1000,2000,6000; slot_mm=1000,2000,6004; err_mm=4\r\n"),
          "warp check against the previous command");
    // Realization and parts are logged immediately.
    s.fake.Realize(true);
    Check(s.Tick(1), "realized tick");
    Check(s.fake.Count("partner_realized=1; R=1; id=0001; pool=01; ms_after_bits=") == 1, "realized line");
    s.fake.state.parts[1] = 3;
    Check(s.Tick(1) && s.fake.Count("partner_parts=loaded; R=1; ms_after_realize=33") == 1, "parts loaded line");
    // A lost bit is re-ORed every tick; its log line is capped at 8.
    for (int i = 0; i < 12; ++i) {
        s.fake.state.q194 &= ~2u;
        Check(s.Tick(1), "lost tick");
    }
    Check(s.fake.Writes("or") == 13 && s.fake.Count("partner_bits=lost; R=1; q194=0x1; q198=0x3") == 8, "re-OR each tick, log capped");
    Check(s.fake.Count("partner_bits=set") == 1, "set logged once");
    // Unrealize (thrash): logged.
    s.fake.Realize(false);
    Check(s.Tick(1) && s.fake.Count("partner_realized=0; R=1; ms_after_realize=") == 1, "unrealized line");
}

void TestTriggerStopAndTeardown() {
    Session s(3);
    s.lab.partner_policy_relax = true;
    Check(s.Tick(0) && s.fake.Writes("relax") == 1 && s.fake.Has("partner_policy=relaxed\r\n") && s.fake.policy_byte == 1,
          "policy relaxed at stage 3 (last resort)");
    Check(s.Tick(1) && s.fake.Writes("or") == 1, "bits set");
    // Polling happens every 30 ticks.
    s.fake.trigger = "stop";
    s.fake.trigger_present = true;
    while (s.driver.mission.ticks < 29) Check(s.Tick(1), "ticks before poll");
    Check(s.fake.trigger_present && s.driver.settings.stage == 3, "not polled before tick 30");
    Check(s.Tick(1), "tick 30");
    Check(!s.fake.trigger_present && s.driver.settings.stage == 0, "stop applied at tick 30");
    Check(s.fake.Has("partner_stage=changed; stage=0; source=fixed; apply=warp; anim=off; aset=qs; reset=n; look=copy; probe=off; via=trigger\r\n") &&
          s.fake.Has("partner_bits=cleared; reason=trigger\r\n") && s.fake.Has("partner_policy=restored\r\n") &&
          s.fake.Writes("clear") == 1 && s.fake.Writes("restore") == 1 && s.fake.policy_byte == 0, "stop clears bits, restores policy");
    Check(s.reply.op == op::clear_all_active, "stage 0 with Lua all-active: CLEAR only");
    Check(s.Tick(0) && s.reply.op == 0, "stage 0 probe only");
    // stage=3 again: bits from the tick after the first applied pose.
    s.Trigger("stage=3");
    Check(s.Tick(0) && s.fake.Writes("or") == 1, "no bits on the first stage-3 tick after re-entry");
    Check(s.Tick(1) && s.fake.Writes("or") == 2, "bits set again");
    // A lower stage also clears.
    s.Trigger("stage=2 apply=slot");
    Check(s.fake.Has("partner_bits=cleared; reason=trigger\r\n") && s.fake.Writes("clear") == 2 &&
          s.driver.settings.apply == mgscoop::PartnerApply::slot, "stage 2 clears bits");
    Check(s.Tick(1) && s.fake.Writes("slot") == 1 && s.reply.op == 0, "apply=slot: native slot write, no WARP");
    Check(s.fake.state.slot[1][0] == 1.0f && s.fake.state.slot[1][2] == 6.0f, "slot written in metres");
    // Bad trigger text.
    s.Trigger("stage=9");
    Check(s.fake.Last() == "partner_trigger=rejected; reason=parse\r\n" && s.driver.settings.stage == 2, "rejected trigger");
    s.fake.trigger_unreadable = true;
    s.Trigger("stage=1");
    Check(s.fake.Last() == "partner_trigger=rejected; reason=parse\r\n" && s.driver.settings.stage == 2, "unreadable trigger");
    s.fake.trigger_unreadable = false;
    // STOP while terminating: restorations still allowed.
    s.Trigger("stage=3");
    Check(s.Tick(1) && s.Tick(1) && s.driver.mission.bits_set, "bits set before STOP");
    pd::Stop(s.driver, s.hooks, mgscoop::PartnerTeardown::clear, true);
    Check(s.fake.Has("partner_bits=cleared; reason=stop\r\n") && s.fake.Last() == "partner_stop; cleared=1; teardown=clear\r\n",
          "STOP while terminating clears");
    Check(!s.Tick(1) && !s.driver.mission.cfg_ready, "no tick after STOP");
}

void TestTeardownKeepAndRefusals() {
    Session s(3);
    Check(s.Tick(0) && s.Tick(1) && s.driver.mission.bits_set, "bits set");
    s.driver.mirror_pending = s.driver.parts_explicit_pending = true;
    pd::Stop(s.driver, s.hooks, mgscoop::PartnerTeardown::keep, true);
    Check(s.fake.Writes("clear") == 0 && s.fake.Last() == "partner_stop; cleared=0; teardown=keep\r\n", "teardown=keep");
    Check(!s.driver.mirror_pending && !s.driver.parts_explicit_pending, "STOP drops one-shots");
    // A clear refused by the guards is reported.
    Session r(3);
    Check(r.Tick(0) && r.Tick(1) && r.driver.mission.bits_set, "bits set");
    r.fake.write_result = "online_bit";
    pd::Stop(r.driver, r.hooks, mgscoop::PartnerTeardown::clear, true);
    Check(r.fake.Has("partner_write=refused; op=clear_bits; reason=online_bit\r\n") &&
          r.fake.Last() == "partner_stop; cleared=0; teardown=clear\r\n", "refused clear reported");
    // Set-type writes refused by the guards: capped at 8.
    Session w(3, mgscoop::PartnerApply::both);
    w.fake.write_result = "sync_present";
    for (int i = 0; i < 20; ++i) Check(w.Tick(i ? 1 : 0), "refused tick");
    Check(w.fake.Count("partner_write=refused;") == 8 && w.fake.Count("partner_bits=set") == 0, "write refusals capped at 8");
}

void TestStateRefusalFlips() {
    Session s(1);
    Check(s.Tick(0), "ready");
    s.fake.refusal = pp::Refusal::s_null;
    Check(!s.Tick(0) && s.fake.Last() == "partner_cfg=refused; reason=s_null\r\n", "refusal leaves the literal");
    const unsigned submits = s.fake.submits;
    Check(!s.Tick(0) && s.fake.Count("partner_cfg=refused") == 1 && s.fake.submits == submits, "change-only, no submit");
    s.fake.refusal = pp::Refusal::none;
    Check(s.Tick(0) && s.fake.Count("partner_cfg=ready") == 1, "ready again");
    for (int i = 0; i < 80; ++i) {
        s.fake.refusal = i % 2 ? pp::Refusal::none : pp::Refusal::q_vtable;
        s.Tick(0);
    }
    Check(s.fake.Count("partner_cfg=") == 65 && s.fake.Has("partner_cfg=capped; limit=64\r\n") &&
          s.fake.Count("partner_cfg=capped") == 1, "refused/ready flips capped at 64");
}

void TestRealizeThrashCap() {
    Session s(3);
    s.fake.state.parts[1] = 3;
    for (int i = 0; i < 100; ++i) {
        s.fake.Realize(i % 2 == 0);
        Check(s.Tick(1), "thrash tick");
    }
    Check(s.fake.Count("partner_realized=") + s.fake.Count("partner_parts=") == 65 &&
          s.fake.Count("partner_realized=capped; limit=64") == 1, "realize/parts transitions capped at 64");
    Check(s.fake.Count("partner_realized=1") >= 1 && s.fake.Count("partner_realized=0") >= 1 && s.fake.Count("partner_parts=loaded") >= 1,
          "first transitions of each kind logged");
}

void TestAllActiveAndFlags() {
    Session s(2);
    s.fake.state.flags = c::flag_valid;
    Check(s.Tick(0) && s.reply.op == (op::warp | op::set_all_active), "SET until Lua latches");
    s.fake.state.flags = c::flag_valid | c::flag_all_active;
    Check(s.Tick(1) && s.reply.op == op::warp, "latched and set: no SET");
    Check(s.fake.Has("partner_flags=0x00002001\r\n"), "flags change logged (E2: 0x2000)");
    Check(s.Tick(1) && s.fake.Count("partner_flags=") == 2, "flags change-only");
    // The game clears bit 0x2000: SET re-asserted at most 1 Hz, 8 times.
    s.fake.state.flags = c::flag_valid;
    Check(s.Tick(1) && (s.reply.op & op::set_all_active) && s.fake.Has("partner_allactive=lost; flags=0x00000001; reassert=1\r\n"),
          "lost all-active re-asserted");
    Check(s.Tick(1, 100) && !(s.reply.op & op::set_all_active), "not again within 1 s");
    unsigned sent = 1;
    for (int i = 0; i < 20; ++i) {
        Check(s.Tick(1, 1000), "resend tick");
        if (s.reply.op & op::set_all_active) ++sent;
    }
    Check(sent == 8 && s.fake.Count("partner_allactive=lost") == 8, "re-assert capped at 8");
    // Stage 1 with Lua latched: CLEAR, never a re-assert.
    Session t(1);
    t.fake.state.flags = c::flag_valid;
    Check(t.Tick(1) && t.reply.op == (op::warp | op::clear_all_active) && t.fake.Count("partner_allactive") == 0, "CLEAR below stage 2");
    // Flags lines capped.
    Session u(0);
    for (std::uint32_t i = 0; i < 70; ++i) {
        u.fake.state.flags = c::flag_valid | (i << 16);
        u.Tick(0);
    }
    Check(u.fake.Count("partner_flags=0x") == 64 && u.fake.Count("partner_flags=capped; limit=64") == 1, "flags lines capped");
}

void TestMirrorAndOneShots() {
    Session s(0);
    // One-shots at stage 0 are refused (they would fire later, maybe in another mission).
    s.Trigger("mirror=on parts=explicit");
    Check(s.fake.Has("partner_trigger=ignored; token=parts=explicit; reason=stage\r\n") &&
          s.fake.Has("partner_trigger=ignored; token=mirror=on; reason=stage\r\n") && !s.driver.mirror_pending &&
          !s.driver.parts_explicit_pending, "one-shots refused at stage 0");
    s.Trigger("stage=1 mirror=on parts=explicit");
    Check(s.driver.mirror_pending && s.driver.parts_explicit_pending, "one-shots pending at stage 1");
    Check(s.Tick(0) && s.reply.op == (op::warp | op::set_parts_explicit | op::mirror_on), "one-shots sent once");
    Check(!s.driver.mirror_pending && !s.driver.parts_explicit_pending && s.driver.mission.mirror_requested, "consumed");
    s.fake.state.flags = c::flag_valid | c::flag_mirror;
    Check(s.Tick(0) && s.reply.op == op::warp && s.fake.Has("partner_mirror=on; flags=0x00004001\r\n"), "mirror seen");
    Check(s.Tick(0, 4000) && !(s.reply.op & op::mirror_off), "mirror kept before 10 s");
    Check(s.Tick(0, 6000) && (s.reply.op & op::mirror_off), "MIRROR_OFF at 10 s");
    Check(s.Tick(0, 500) && !(s.reply.op & op::mirror_off), "not resent within 1 s");
    Check(s.Tick(0, 600) && (s.reply.op & op::mirror_off), "resent after 1 s while the bit stays");
    s.fake.state.flags = c::flag_valid;
    Check(s.Tick(0) && !(s.reply.op & op::mirror_off) && s.fake.Count("partner_mirror=off; ms_on=") == 1 &&
          !s.driver.mission.mirror_requested, "mirror off observed");
    // stop: mirror off immediately; refused while the engine object is null.
    s.Trigger("mirror=on");
    Check(s.Tick(0) && (s.reply.op & op::mirror_on), "mirror on again");
    s.fake.state.flags = c::flag_valid | c::flag_mirror;
    s.fake.state.mirror_object = 0;
    s.Trigger("stop");
    Check(s.Tick(0) && !(s.reply.op & op::mirror_off) && s.fake.Has("partner_mirror=refused; reason=engine_object\r\n"),
          "no MIRROR_OFF without the engine object");
    Check(s.Tick(0, 2000) && s.fake.Count("partner_mirror=refused") == 1, "refusal logged once");
    s.fake.state.mirror_object = 0x9000;
    Check(s.Tick(0, 2000) && s.reply.op == op::mirror_off, "stage 0: MIRROR_OFF as soon as allowed");
    // A new request while the window has expired restarts it (no ON+OFF pair).
    Session q(1);
    q.Trigger("mirror=on");
    Check(q.Tick(0) && (q.reply.op & op::mirror_on), "first request");
    q.fake.state.flags = c::flag_valid | c::flag_mirror;
    Check(q.Tick(0, 10000) && (q.reply.op & op::mirror_off), "expired: off");
    q.Trigger("mirror=on");
    Check(q.Tick(0, 1100) && (q.reply.op & op::mirror_on) && !(q.reply.op & op::mirror_off), "re-request: ON only");
    Check(q.Tick(0, 1100) && !(q.reply.op & op::mirror_off), "new 10 s window");
    // A request whose bit never appears (binding missing) expires quietly.
    Session m(1);
    m.Trigger("mirror=on");
    Check(m.Tick(0) && (m.reply.op & op::mirror_on), "requested");
    Check(m.Tick(0, 10000) && !(m.reply.op & op::mirror_off) && !m.driver.mission.mirror_requested, "never applied: expires");
    // One-shots never leak into another mission.
    Session n(1);
    n.Trigger("mirror=on");
    pd::MissionEnd(n.driver);
    pd::MissionStart(n.driver, n.hooks);
    Check(pd::Cfg(n.driver, n.hooks, nullptr, n.now) && n.Tick(0) && !(n.reply.op & op::mirror_on), "pending one-shot dropped");
    n.Trigger("mirror=on");
    n.Trigger("stop");
    n.Trigger("stage=1");
    Check(n.Tick(0) && !(n.reply.op & op::mirror_on), "stop drops the pending one-shot");
}

void TestForceExistsAndPolicy() {
    Session s(2);
    s.lab.partner_force_exists = true;
    for (int i = 0; i < 10; ++i) Check(s.Tick(1), "s0 missing");
    Check(s.fake.Writes("force") == 0, "not before 11 ticks");
    Check(s.Tick(1) && s.fake.Writes("force") == 1 && s.fake.Has("partner_s0=forced; R=1\r\n"), "forced on tick 11");
    for (int i = 0; i < 11 * 10; ++i) Check(s.Tick(1), "s0 still missing");
    Check(s.fake.Writes("force") == 11 && s.fake.Count("partner_s0=forced") == 8, "every 11 ticks, log capped");
    s.fake.state.s0 = 0x03;
    for (int i = 0; i < 30; ++i) Check(s.Tick(1), "s0 present");
    Check(s.fake.Writes("force") == 11, "not while the exists bit is set");
    // Policy relax refused: logged once, never retried.
    Session p(3);
    p.lab.partner_policy_relax = true;
    p.fake.write_result = "policy_vtable";
    for (int i = 0; i < 5; ++i) Check(p.Tick(1), "policy tick");
    Check(p.fake.Count("partner_policy=refused; reason=policy_vtable") == 1 && !p.driver.mission.policy_relaxed, "relax refused once");
}

void TestNetSourceAndCadence() {
    Session s(1, mgscoop::PartnerApply::warp, mgscoop::PartnerSource::net);
    s.fake.sample = mgscoop::pose::Sample{};
    Check(s.Tick(0) && s.reply.op == 0 && s.reply.status == partner::Status::lost, "net: no WARP before the first packet");
    s.fake.sample.valid = true;
    s.fake.sample.x = 4.0f; s.fake.sample.y = 0.5f; s.fake.sample.z = -2.0f; s.fake.sample.yaw = 1.0f;
    s.fake.sample.status = mgscoop::pose::Status::live;
    s.fake.sample.age_ms = 42;
    Check(s.Tick(0) && s.reply.op == op::warp && s.reply.x_mm == 4000 && s.reply.y_mm == 500 && s.reply.z_mm == -2000 &&
          s.reply.yaw_mrad == 1000 && s.reply.status == partner::Status::live && s.reply.age_ms == 42, "net: interpolated pose");
    s.fake.sample.status = mgscoop::pose::Status::stale;
    Check(s.Tick(0) && s.reply.status == partner::Status::stale && s.reply.op == op::warp, "stale holds the pose");
    // Cadence line every 10 s, focus from the hook.
    s.fake.focused = false;
    for (int i = 0; i < 400; ++i) s.Tick(0);
    Check(s.fake.Count("partner_tick=rate; hz=30; focused=0") >= 1, "rate line");
    // Parse rejections capped.
    for (int i = 0; i < 12; ++i) {
        const char bad[] = "return 'MGS_COOP_PARTNER_V1' -- 1 2 3";
        Check(!pd::Tick(s.driver, s.hooks, s.lab, bad, sizeof(bad) - 1, pd::Clock{s.now, s.now}, false, s.reply), "bad tick");
    }
    Check(s.fake.Count("partner_tick=rejected; reason=parse") == 8, "parse rejections capped at 8");
}

void TestLuaLogAndQuiet() {
    Session s(0);
    const char note[] = "return 'MGS_COOP_PARTNER_LOG_V1' -- event=started;x=1";
    pd::LuaLog(s.driver, s.hooks, note, sizeof(note) - 1);
    Check(s.fake.Last() == "partner_lua=event=started; x=1\r\n", "Lua note line");
    for (int i = 0; i < 80; ++i) pd::LuaLog(s.driver, s.hooks, note, sizeof(note) - 1);
    Check(s.fake.Count("partner_lua=event=started") == 64 && s.fake.Count("partner_lua=capped; limit=64") == 1, "Lua notes capped");
    // Quiet pairing: only a replaced tick skips its entry and return lines.
    pd::Quiet quiet{};
    Check(!pd::LogsEntry(partner::Receipt::tick) && pd::LogsEntry(partner::Receipt::cfg) && pd::LogsEntry(partner::Receipt::stop) &&
          pd::LogsEntry(partner::Receipt::log), "only the tick is quiet");
    pd::Arm(quiet, partner::Receipt::tick, true);
    Check(pd::ConsumeReturn(quiet) && !pd::ConsumeReturn(quiet), "replaced tick: one quiet return");
    pd::Arm(quiet, partner::Receipt::tick, false);
    Check(!pd::ConsumeReturn(quiet), "unreplaced tick (literal): ordinary return line");
    pd::Arm(quiet, partner::Receipt::tick, true);
    pd::Arm(quiet, partner::Receipt::cfg, true);
    Check(!pd::ConsumeReturn(quiet), "a later receipt re-arms");
    // Probe: change-only (1 s), forced every 10 s, capped.
    Session p(0);
    Check(p.Tick(0) && p.fake.Count("partner_probe=state") == 1, "first probe");
    Check(p.Tick(0, 500) && p.fake.Count("partner_probe=state") == 1, "unchanged: no probe");
    p.fake.state.s0 = 0x03;
    Check(p.Tick(0, 400) && p.fake.Count("partner_probe=state") == 1, "changed within 1 s: waits");
    Check(p.Tick(0, 200) && p.fake.Count("partner_probe=state") == 2, "changed after 1 s: logged");
    Check(p.Tick(0, 10000) && p.fake.Count("partner_probe=state") == 3, "forced after 10 s");
    // Anim survey (per slot), change-only at most 10 Hz.
    p.lab.partner_anim_probe = true;
    p.fake.anim_ok = true;
    p.fake.anim.act[0] = 0x12;
    p.fake.census.k[1].psw = 0x22;
    p.fake.state.rec_ok = true;
    p.fake.state.rec1_status = 0x00100020;
    Check(p.Tick(0) && p.fake.Has("partner_anim; act=0012,0000; s=0,0; v=0,0; q28=0,0; qd8=0000,0000; psw=00,22; rec1=0x00100020\r\n"),
          "anim line (per slot, stance words, record flags)");
    p.fake.anim.act[0] = 0x13;
    Check(p.Tick(0, 50) && p.fake.Count("partner_anim;") == 1 && p.Tick(0, 60) && p.fake.Count("partner_anim; act=0013,0000;") == 1,
          "anim at most 10 Hz");
    Check(p.Tick(0, 200) && p.fake.Count("partner_anim;") == 2, "anim unchanged: no line");
    p.fake.census.k[1].psw = 0x33;
    Check(p.Tick(0, 200) && p.fake.Count("partner_anim;") == 3, "a stance word change is a change");
}

// Instance 1 realized with its parts loaded.
void Loaded(Session& s) {
    s.fake.Realize(true);
    s.fake.state.parts[1] = 3;
}

void TestAnimationApply() {
    // anim=mirror aset=qs: the local slot 0 drives instance 1 (no network).
    Session s(3);
    s.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    s.driver.settings.aset = mgscoop::PartnerAset::qs;
    s.fake.anim_ok = true;
    s.fake.anim.act[0] = 4; s.fake.anim.stance[0] = 2; s.fake.anim.speed[0] = 2;
    Check(s.Tick(1) && s.fake.calls.empty() && s.fake.Count("partner_census") == 0, "nothing before instance 1 is loaded");
    Check(s.fake.Has("partner_anim=source; anim=mirror; valid=1; act=4; s=2; v=2\r\n"), "anim source logged");
    Check(s.fake.last_submit_anim.act == 4 && s.fake.last_submit_anim.stance == 2 &&
          s.fake.last_submit_anim.flags == mgscoop::sync::AnimFlag::Valid, "the local slot-0 anim goes out with the pose");
    Loaded(s);
    Check(s.Tick(1) && s.fake.Calls("anim:4,2,2") == 1 && s.fake.Calls("stance:") == 0, "arrays every tick; stance held 150 ms");
    Check(s.fake.Has("partner_aset=q; state=applied; act=4; s=2; v=2\r\n") && s.fake.Count("partner_census; k=") == 2,
          "aset=q logged once; census logged once at the first loaded tick");
    Check(s.Tick(1, 100) && s.fake.Calls("stance:") == 0, "still holding");
    Check(s.Tick(1, 60) && s.fake.Calls("stance:2") == 1 && s.fake.Has("partner_stance=set; R=1; id=1; from=0; to=2; psw=0x22; q30=2\r\n"),
          "SetStance after the hold");
    Check(s.Tick(1) && s.Tick(1) && s.fake.Calls("stance:") == 1 && s.fake.Calls("anim:4,2,2") == 5 && s.fake.Count("partner_census; k=") == 2,
          "stance on change only; arrays every tick; census once");
    Check(s.fake.Count("partner_aset=q") == 1 && s.fake.Count("partner_stance=reverted") == 0, "change-only lines");
    // The game's action plugin reverts the stance word within 500 ms.
    s.fake.anim.stance[0] = 3; s.fake.anim.act[0] = 6;
    Check(s.Tick(1) && s.Tick(1, 200) && s.fake.Calls("stance:3") == 1, "crawl applied");
    s.fake.census.k[1].psw = 0x11;
    Check(s.Tick(1) && s.fake.Has("partner_stance=reverted; R=1; psw=0x11; expected=0x33; ms_after_set=33\r\n"), "revert detected");
    Check(s.Tick(1) && s.fake.Count("partner_stance=reverted") == 1, "revert logged once per set");
    // anim=net: the received anim; frames without the extension apply nothing.
    Session n(3, mgscoop::PartnerApply::warp, mgscoop::PartnerSource::net);
    n.driver.settings.anim = mgscoop::PartnerAnim::net;
    n.driver.settings.aset = mgscoop::PartnerAset::s;
    Loaded(n);
    n.fake.sample.valid = true;
    n.fake.sample.status = mgscoop::pose::Status::live;
    Check(n.Tick(1) && n.fake.Has("partner_anim=source; anim=net; valid=0; act=0; s=0; v=0\r\n") && n.fake.calls.empty(),
          "net without anim: nothing applied");
    n.fake.sample.anim_valid = true; n.fake.sample.act = 3; n.fake.sample.anim_stance = 2; n.fake.sample.anim_speed = 0;
    Check(n.Tick(1) && n.Tick(1, 200) && n.fake.Calls("stance:2") == 1 && n.fake.Calls("anim:") == 0, "aset=s: SetStance only");
    Check(n.fake.Has("partner_anim=source; anim=net; valid=1; act=3; s=2; v=0\r\n"), "source validity change logged");
    // aset=r: the stance request component.
    Session r(3);
    r.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    r.driver.settings.aset = mgscoop::PartnerAset::r;
    r.fake.anim_ok = true;
    r.fake.anim.stance[0] = 3;
    Loaded(r);
    Check(r.Tick(1) && r.Tick(1, 200) && r.fake.Calls("req:3") == 1 && r.fake.Calls("stance:") == 0 &&
          r.fake.Has("partner_stance=req; R=1; from=0; to=3; result=ok\r\n"), "aset=r requests the stance");
    // A faulting SetStance disables every native call for the mission.
    Session f(3);
    f.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    f.driver.settings.aset = mgscoop::PartnerAset::s;
    f.fake.anim_ok = true;
    f.fake.anim.stance[0] = 2;
    f.fake.stance_result = "fault";
    Loaded(f);
    Check(f.Tick(1) && f.Tick(1, 200) && f.fake.Has("partner_call=fault; fn=set_stance; native_calls=disabled\r\n") &&
          f.fake.Has("partner_stance=refused; R=1; to=2; reason=fault\r\n"), "fault: disabled");
    f.fake.stance_result = nullptr;
    f.fake.anim.stance[0] = 3;
    Check(f.Tick(1) && f.Tick(1, 200) && f.fake.Calls("stance:") == 0 && f.fake.Has("partner_stance=refused; R=1; to=3; reason=native_disabled\r\n"),
          "no native call after a fault");
    pd::MissionStart(f.driver, f.hooks);
    Check(pd::Cfg(f.driver, f.hooks, nullptr, f.now) && !f.driver.mission.native_disabled, "a new mission re-enables native calls");
    // A refused array write is logged (change-only), and nothing runs at stage < 3.
    Session q(3);
    q.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    q.driver.settings.aset = mgscoop::PartnerAset::q;
    q.fake.anim_ok = true;
    q.fake.anim.stance[0] = 1; q.fake.anim.act[0] = 1;
    q.fake.anim_write_result = "count";
    Loaded(q);
    Check(q.Tick(1) && q.Tick(1) && q.fake.Count("partner_aset=q; state=refused; reason=count") == 1 && q.fake.Count("partner_census") == 0,
          "refused array write logged once; aset=q needs no census");
    Session low(2);
    low.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    low.fake.anim_ok = true;
    low.fake.anim.stance[0] = 2;
    Loaded(low);
    Check(low.Tick(1) && low.Tick(1, 200) && low.fake.calls.empty() && low.fake.Count("partner_anim=source") == 0, "stage 2: no anim");
}

void TestAnimationGuards() {
    // aset=q copies only the locomotion ids 1..6.
    Session q(3);
    q.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    q.driver.settings.aset = mgscoop::PartnerAset::q;
    q.fake.anim_ok = true;
    q.fake.anim.act[0] = 0x14; q.fake.anim.stance[0] = 1; q.fake.anim.speed[0] = 0;
    Loaded(q);
    Check(q.Tick(1) && q.Tick(1) && q.fake.Calls("anim:") == 0 &&
          q.fake.Count("partner_aset=q; state=refused; reason=act_domain; act=20\r\n") == 1, "a non-locomotion act is not written (logged once)");
    q.fake.anim.act[0] = 2; q.fake.anim.speed[0] = 3;
    Check(q.Tick(1) && q.fake.Calls("anim:2,1,3") == 1 && q.fake.Has("partner_aset=q; state=applied; act=2; s=1; v=3\r\n"),
          "back to a locomotion act: written");
    q.fake.anim.act[0] = 0;
    Check(q.Tick(1) && q.fake.Calls("anim:") == 1 && q.fake.Count("partner_aset=q; state=refused; reason=act_domain; act=0") == 1,
          "act 0 is not written");
    // anim=net with a received out-of-domain act: nothing written.
    Session n(3, mgscoop::PartnerApply::warp, mgscoop::PartnerSource::net);
    n.driver.settings.anim = mgscoop::PartnerAnim::net;
    n.driver.settings.aset = mgscoop::PartnerAset::q;
    Loaded(n);
    n.fake.sample.valid = true;
    n.fake.sample.status = mgscoop::pose::Status::live;
    n.fake.sample.anim_valid = true; n.fake.sample.act = 0xFFFF; n.fake.sample.anim_stance = 2; n.fake.sample.anim_speed = 0;
    Check(n.Tick(1) && n.fake.Calls("anim:") == 0 && n.fake.Count("partner_aset=q; state=refused; reason=act_domain; act=65535") == 1,
          "received act 0xFFFF refused");

    // A refused SetStance is not committed: retried after kStanceRetryMs.
    Session r(3);
    r.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    r.driver.settings.aset = mgscoop::PartnerAset::s;
    r.fake.anim_ok = true;
    r.fake.anim.act[0] = 5; r.fake.anim.stance[0] = 3;
    r.fake.stance_result = "ps_sub";
    Loaded(r);
    Check(r.Tick(1) && r.Tick(1, 200) && r.fake.Has("partner_stance=refused; R=1; to=3; reason=ps_sub\r\n") && r.fake.Calls("stance:") == 0,
          "first attempt refused");
    Check(r.Tick(1, 500) && r.Tick(1, 400) && r.fake.Count("partner_stance=refused") == 1, "no retry inside the backoff, refusal logged once");
    r.fake.stance_result = nullptr;
    Check(r.Tick(1, 200) && r.fake.Calls("stance:3") == 1 && r.fake.Has("partner_stance=set; R=1; id=1; from=0; to=3; psw=0x33; q30=3\r\n"),
          "retried after the backoff and applied");
    Check(r.Tick(1, 2000) && r.fake.Calls("stance:") == 1, "committed: no further call");
    Check(r.fake.Count("partner_stance=summary;") == 1 && r.fake.Has("partner_stance=summary; last=0; sets=0; reqs=0; refused=1; reverts=0\r\n"),
          "the first stance event is summarized at once, later ones at most every 10 s");
    // A parts reload (look apply) resets the debounce: the stance is re-applied.
    r.fake.state.parts[1] = 4;
    Check(r.Tick(1) && r.Tick(1, 200) && r.fake.Calls("stance:") == 1, "nothing while the parts reload");
    r.fake.state.parts[1] = 3;
    Check(r.Tick(1) && r.Tick(1, 200) && r.fake.Calls("stance:3") == 2 && r.fake.Has("partner_stance=set; R=1; id=1; from=0; to=3; psw=0x33; q30=3\r\n"),
          "re-applied once the parts are loaded again");
    // The revert watch uses the stance verdict, not the transform verdict.
    r.fake.anim.stance[0] = 2; r.fake.anim.act[0] = 3;
    Check(r.Tick(1) && r.Tick(1, 200) && r.fake.Calls("stance:2") == 1, "crouch applied");
    r.fake.census.k[1].refusal = "t_null";
    r.fake.census.k[1].psw = 0x11;
    Check(r.Tick(1) && r.fake.Count("partner_stance=reverted") == 1, "revert detected while the transform chain is refused");
    r.fake.census.k[1].refusal = nullptr;

    // Separate caps: 64 set lines, then kind=set capped; refused, reverted and
    // the summary keep going.
    Session c(3);
    c.driver.settings.anim = mgscoop::PartnerAnim::mirror;
    c.driver.settings.aset = mgscoop::PartnerAset::s;
    c.fake.anim_ok = true;
    Loaded(c);
    for (unsigned i = 0; i < 70; ++i) {
        c.fake.anim.stance[0] = static_cast<std::uint8_t>(1 + (i % 2));
        c.fake.anim.act[0] = static_cast<std::uint8_t>(1 + 2 * (i % 2));
        Check(c.Tick(1) && c.Tick(1, 200), "cap timeline");
    }
    Check(c.fake.Calls("stance:") == 70 && c.fake.Count("partner_stance=set;") == 64 &&
          c.fake.Count("partner_stance=capped; limit=64; kind=set\r\n") == 1, "set lines capped at 64 (kind=set)");
    c.fake.anim.stance[0] = 1; c.fake.anim.act[0] = 1;
    Check(c.Tick(1) && c.Tick(1, 200) && c.fake.Calls("stance:1") == 36, "set 71 (stand)");
    c.fake.census.k[1].psw = 0x33;
    Check(c.Tick(1) && c.fake.Count("partner_stance=reverted") == 1, "reverts still logged after the set cap");
    c.fake.stance_result = "ps_words";
    c.fake.anim.stance[0] = 3; c.fake.anim.act[0] = 5;
    Check(c.Tick(1) && c.Tick(1, 200) && c.fake.Count("partner_stance=refused; R=1; to=3; reason=ps_words") == 1,
          "refusals still logged after the set cap");
    c.fake.stance_result = nullptr;
    const auto last_summary = [&c]() {
        for (auto it = c.fake.lines.rbegin(); it != c.fake.lines.rend(); ++it)
            if (it->compare(0, 23, "partner_stance=summary;") == 0) return *it;
        return std::string();
    };
    const std::size_t summaries = c.fake.Count("partner_stance=summary;");
    Check(c.Tick(1, 11000) && c.fake.Count("partner_stance=summary;") == summaries + 1, "summary keeps counting after the caps");
    Check(last_summary() == "partner_stance=summary; last=3; sets=72; reqs=0; refused=1; reverts=1\r\n",
          "summary counters (the refused crawl was retried and applied)");
    // A trigger that changes anim/aset re-arms the change-only lines.
    c.Trigger("aset=s");
    Check(c.fake.Has("partner_stage=changed; stage=3; source=fixed; apply=warp; anim=mirror; aset=s; reset=n; look=copy; probe=off; via=trigger\r\n"),
          "trigger accepted");
}

void TestTransformDrivers() {
    // xform: refused before realization (Warp fallback), then applied every tick.
    Session s(3, mgscoop::PartnerApply::xform);
    s.fake.xform_result = "unrealized";
    Check(s.Tick(0) && (s.reply.op & op::warp) && s.fake.Writes("slot") == 1, "unrealized: slot write + WARP fallback");
    Check(s.fake.Has("partner_xform=refused; reason=unrealized; fallback=warp\r\n"), "refusal logged");
    Check(s.Tick(1) && s.fake.Count("partner_xform=refused") == 1, "refusal change-only");
    Loaded(s);
    s.fake.xform_result = nullptr;
    Check(s.Tick(1) && !(s.reply.op & op::warp) && s.fake.Calls("xform:n") == 1, "applied: no WARP");
    Check(s.fake.Has("partner_xform=applied; R=1; apply=xform; reset=n\r\n") && s.fake.Count("partner_census; k=") == 2,
          "applied logged; census once per realize");
    Check(s.fake.xform_pos[0] == 1.0f && s.fake.xform_pos[1] == 2.0f && s.fake.xform_pos[2] == 6.0f && s.fake.xform_pos[3] == 0.0f,
          "position in metres, w = 0");
    Check(std::fabs(s.fake.xform_quat[1] - std::sin(0.3925f)) < 1e-4f && std::fabs(s.fake.xform_quat[3] - std::cos(0.3925f)) < 1e-4f &&
          s.fake.xform_quat[0] == 0.0f && s.fake.xform_quat[2] == 0.0f, "quaternion (0, sin(yaw/2), 0, cos(yaw/2))");
    Check(s.Tick(1) && s.fake.Calls("xform:") == 2 && s.fake.Count("partner_xform=applied") == 1, "every tick, logged once");
    // Readback at 1 Hz: commanded vs body.
    s.fake.body_ok = true;
    s.fake.body_pos[0] = 1.0f; s.fake.body_pos[1] = 2.0f; s.fake.body_pos[2] = 6.03f;
    partner::YawToQuat(0.785f, s.fake.body_quat);
    Check(s.Tick(1, 1000) && s.fake.Has("partner_xform; R=1; apply=xform; cmd_mm=1000,2000,6000; body_mm=1000,2000,6030; err_mm=30; "
                                       "yaw_err_mrad=0; reset=n\r\n"), "readback line");
    // reset token reaches the call.
    s.Trigger("reset=ac");
    Check(s.Tick(1) && s.fake.Calls("xform:ac") == 1, "reset=ac passed through");
    // A faulting call disables native calls; Warp keeps the partner moving.
    s.fake.xform_result = "fault";
    Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Has("partner_call=fault; fn=xform; native_calls=disabled\r\n"), "fault");
    s.fake.xform_result = nullptr;
    const std::size_t xforms = s.fake.Calls("xform:");
    Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Calls("xform:") == xforms &&
          s.fake.Has("partner_xform=refused; reason=native_disabled; fallback=warp\r\n"), "no call after a fault");
    // hybrid: a full Warp on first placement, then xform; Warp again on a large error.
    Session h(3, mgscoop::PartnerApply::hybrid);
    Loaded(h);
    Check(h.Tick(1) && (h.reply.op & op::warp) && h.fake.Calls("xform:") == 0 && h.fake.Has("partner_hybrid=warp; reason=first\r\n"),
          "first: Warp");
    Check(h.Tick(1) && !(h.reply.op & op::warp) && h.fake.Calls("xform:") == 1, "then xform");
    h.fake.body_ok = true;
    h.fake.body_pos[0] = 1.0f; h.fake.body_pos[1] = 2.0f; h.fake.body_pos[2] = 7.4f;  // 1400 mm: within
    Check(h.Tick(1) && !(h.reply.op & op::warp) && h.fake.Calls("xform:") == 2, "1.4 m: still xform");
    h.fake.body_pos[2] = 8.0f;  // 2000 mm
    Check(h.Tick(1) && (h.reply.op & op::warp) && h.fake.Has("partner_hybrid=warp; reason=err\r\n"), "2 m: Warp");
    h.fake.body_pos[2] = 6.0f;
    Check(h.Tick(1) && !(h.reply.op & op::warp), "back to xform");
    // Unrealize resets the placement.
    h.fake.Realize(false);
    h.fake.xform_result = "unrealized";
    Check(h.Tick(1) && (h.reply.op & op::warp), "unrealized: fallback");
    Loaded(h);
    h.fake.xform_result = nullptr;
    Check(h.Tick(1) && h.fake.Count("partner_hybrid=warp; reason=first") == 2, "placement again after re-realize");
    // hybrid with source=net: a lost -> live relink re-places the body.
    Session nl(3, mgscoop::PartnerApply::hybrid, mgscoop::PartnerSource::net);
    Loaded(nl);
    nl.fake.sample.valid = true;
    nl.fake.sample.status = mgscoop::pose::Status::live;
    Check(nl.Tick(1) && nl.Tick(1) && nl.fake.Calls("xform:") == 1, "net hybrid placed");
    nl.fake.sample.status = mgscoop::pose::Status::lost;
    Check(nl.Tick(1) && nl.fake.Calls("xform:") == 2, "lost: holds with xform");
    nl.fake.sample.status = mgscoop::pose::Status::live;
    Check(nl.Tick(1) && (nl.reply.op & op::warp) && nl.fake.Has("partner_hybrid=warp; reason=relink\r\n"), "relink: Warp");
    // The default driver never calls a native transform.
    Session w(3);
    Loaded(w);
    Check(w.Tick(1) && w.fake.Calls("xform:") == 0 && w.fake.Count("partner_xform") == 0 && w.fake.Count("partner_census") == 0,
          "apply=warp: no native call, no census");
}

void TestLooks() {
    // look=quiet: a fixed look, applied once all-active is latched; no COPY.
    Session s(3);
    s.driver.settings.look = mgscoop::PartnerLook::quiet;
    Check(s.Tick(0) && s.reply.op == (op::warp | op::set_all_active), "before all-active: no SET_PARTS, no COPY");
    Check(s.Tick(1) && s.reply.op == (op::warp | op::set_parts_explicit) && partner::SameLook(s.reply.look, partner::quiet_look),
          "SET_PARTS with the Quiet values");
    Check(s.fake.Has("partner_look=apply; R=1; source=quiet; type=6; parts=27; camo=116; hand=0; face=0; fe=0; rev=0; avatar_as_snake=0\r\n") &&
          s.fake.Calls("fe:0") == 1, "apply logged; face_equip written natively");
    Check(s.Tick(1) && !(s.reply.op & op::set_parts_explicit), "Lua has not confirmed yet, but within 2 s: not resent");
    Check(s.Tick(3) && !(s.reply.op & op::set_parts_explicit), "confirmed (parts_copied): never resent");
    Check(s.fake.Count("partner_looks; look0=0,1,0,1,2,2; look1=255,255,255,255,32767,0") == 1, "both looks logged");
    // Verification: the parts state cycles, then the slot-1 arrays match.
    Loaded(s);
    s.fake.state.parts[1] = 4;
    Check(s.Tick(3), "unloading");
    s.fake.state.parts[1] = 3;
    s.fake.looks.look[1] = partner::quiet_look;
    Check(s.Tick(3) && s.fake.Count("partner_look=loaded; R=1; q40=6; q48=27; q50=116; q58=0; q60=0; q68=0; ms_after_apply=") == 1 &&
          s.fake.Containing("; cycle=1") == 1, "loaded after the cycle");
    // look=snake via trigger: re-applied (2 s gap) with the Snake values.
    s.Trigger("look=snake");
    Check(s.Tick(3, 2000) && (s.reply.op & op::set_parts_explicit) && partner::SameLook(s.reply.look, partner::snake_look),
          "look=snake applied");
    // Stuck load: timeout after 15 s.
    Check(s.Tick(3, 16000) && s.fake.Count("partner_look=timeout; R=1; state=3; q40=6") == 1, "timeout logged");
    // parts=explicit outside look=copy re-applies the desired look once.
    s.Trigger("stage=3 parts=explicit");
    Check(s.Tick(3, 2000) && (s.reply.op & op::set_parts_explicit) && partner::SameLook(s.reply.look, partner::snake_look), "re-armed");
    Check(s.Tick(3, 2000) && !(s.reply.op & op::set_parts_explicit), "once");
    // mirror=on is refused while an explicit look is used (mirror copies the local look).
    s.Trigger("mirror=on");
    Check(s.fake.Has("partner_trigger=ignored; token=mirror=on; reason=look\r\n") && !s.driver.mirror_pending, "mirror refused");

    // look=net: COPY until the first look arrives, then the partner's own look.
    Session n(3);
    n.driver.settings.look = mgscoop::PartnerLook::net;
    Check(n.Tick(1) && (n.reply.op & op::copy_parts) && !(n.reply.op & op::set_parts_explicit), "no look yet: COPY fallback");
    Check(n.Tick(3, 4900) && n.fake.Count("partner_look=waiting") == 0, "not waiting before 5 s");
    Check(n.Tick(3, 200) && n.fake.Count("partner_look=waiting; R=1; ms_after_allactive=") == 1, "waiting logged once");
    n.fake.remote = pd::RemoteLook{true, partner::Look{6, 1, 2, 3, 4, 5}, 7};
    Check(n.Tick(3) && (n.reply.op & op::set_parts_explicit) && !(n.reply.op & op::copy_parts) &&
          partner::SameLook(n.reply.look, partner::quiet_look), "remote Quiet normalized and applied, COPY stopped");
    Check(n.fake.Has("partner_look=apply; R=1; source=net; type=6; parts=27; camo=116; hand=0; face=0; fe=0; rev=7; avatar_as_snake=0\r\n"),
          "net apply logged");
    Check(n.Tick(3) && !(n.reply.op & op::set_parts_explicit), "same revision: not resent");
    n.fake.remote = pd::RemoteLook{true, partner::Look{3, 2, 4, 1, 100, 1}, 8};
    Check(n.Tick(3) && !(n.reply.op & op::set_parts_explicit), "new revision within 2 s: waits");
    Check(n.Tick(3, 2000) && (n.reply.op & op::set_parts_explicit) && n.reply.look.type == 0 &&
          n.fake.Containing("face=0; fe=0; rev=8; avatar_as_snake=1") == 1 && !n.fake.calls.empty() && n.fake.calls.back() == "fe:0" && n.reply.look.face == 0,
          "avatar applied as Snake with face 0 / face_equip 0");
    n.fake.remote = pd::RemoteLook{true, partner::Look{4, 0, 0, 0, 0, 0}, 9};
    Check(n.Tick(3, 2000) && !(n.reply.op & op::set_parts_explicit) && (n.reply.op & op::copy_parts) &&
          n.fake.Has("partner_look=refused; reason=type; source=net; type=4; parts=0; camo=0; hand=0; rev=9\r\n"),
          "LIQUID refused, logged; COPY op again (Lua keeps its latch)");
    Check(n.Tick(3) && n.fake.Count("partner_look=refused") == 1, "refusal change-only");
    // The sender always sends the local look once its parts are loaded.
    Check(!n.fake.sent_looks.empty() && partner::SameLook(n.fake.sent_looks.back(), partner::Look{0, 1, 0, 1, 2, 2}), "local look sent");
    const std::size_t sent = n.fake.sent_looks.size();
    n.fake.state.parts[0] = 1;
    Check(n.Tick(3) && n.fake.sent_looks.size() == sent, "not while the local parts reload");
    // A new mission starts the look from scratch.
    pd::MissionStart(n.driver, n.hooks);
    Check(pd::Cfg(n.driver, n.hooks, nullptr, n.now) && !n.driver.mission.look_applied_any, "mission reset");

    // look=copy: COPY as before; parts=explicit sends the local look once (F3b).
    Session c(1);
    c.Trigger("parts=explicit");
    Check(c.Tick(0) && c.reply.op == (op::warp | op::set_parts_explicit) && partner::SameLook(c.reply.look, partner::Look{0, 1, 0, 1, 2, 2}) &&
          c.fake.Calls("fe:2") == 1 && c.fake.Has("partner_look=apply; R=1; source=copy; type=0; parts=1; camo=0; hand=1; face=2; fe=2; rev=0; avatar_as_snake=0\r\n"),
          "copy + parts=explicit: the local look at stage 1");
    Check(c.Tick(0) && !(c.reply.op & op::set_parts_explicit) && !c.driver.parts_explicit_pending, "one-shot");
    Check(c.fake.Count("partner_looks") == 0, "no looks line in copy mode");
}

void TestCensusAndProbes() {
    Session s(3);
    s.Trigger("census=1");
    Check(s.fake.Has("partner_stage=changed; stage=3; source=fixed; apply=warp; anim=off; aset=qs; reset=n; look=copy; probe=off; via=trigger\r\n"),
          "trigger line with the new fields");
    Check(s.Tick(0) && s.fake.Count("partner_census; k=0; verdict=ok") == 1 && s.fake.Count("partner_census; k=1; verdict=ok") == 1,
          "census=1 logs both blocks");
    Check(s.Tick(0) && s.fake.Count("partner_census") == 2, "one-shot");
    s.fake.census_refusal = "t_class";
    s.Trigger("census=1");
    Check(s.Tick(0) && s.fake.Count("partner_census; k=1; verdict=t_class") == 1, "refusal verdict");
    // probe=ps: both instances at 4 Hz until probe=off.
    s.Trigger("probe=ps");
    Check(s.driver.settings.probe_ps && s.Tick(0) && s.fake.ps_probes == 2 && s.fake.Count("partner_ps; k=1") == 1, "ps probe");
    Check(s.Tick(0, 100) && s.fake.ps_probes == 2 && s.Tick(0, 200) && s.fake.ps_probes == 4, "4 Hz");
    s.Trigger("probe=off");
    Check(!s.driver.settings.probe_ps && s.Tick(0, 300) && s.fake.ps_probes == 4, "probe=off");
    // move= alias and every setting through the trigger.
    s.Trigger("move=hybrid anim=net aset=r reset=c look=snake");
    Check(s.driver.settings.apply == mgscoop::PartnerApply::hybrid && s.driver.settings.anim == mgscoop::PartnerAnim::net &&
          s.driver.settings.aset == mgscoop::PartnerAset::r && s.driver.settings.reset == mgscoop::PartnerReset::c &&
          s.driver.settings.look == mgscoop::PartnerLook::snake &&
          s.fake.Has("partner_stage=changed; stage=3; source=fixed; apply=hybrid; anim=net; aset=r; reset=c; look=snake; probe=off; via=trigger\r\n"),
          "settings via trigger");
    // Configure takes the lab.ini values.
    mgscoop::LabConfig lab{};
    lab.partner_apply = mgscoop::PartnerApply::xform;
    lab.partner_anim = mgscoop::PartnerAnim::mirror;
    lab.partner_aset = mgscoop::PartnerAset::s;
    lab.partner_reset = mgscoop::PartnerReset::a;
    lab.partner_look = mgscoop::PartnerLook::net;
    pd::Driver d{};
    pd::Configure(d, lab);
    Check(d.settings.apply == mgscoop::PartnerApply::xform && d.settings.anim == mgscoop::PartnerAnim::mirror &&
          d.settings.aset == mgscoop::PartnerAset::s && d.settings.reset == mgscoop::PartnerReset::a &&
          d.settings.look == mgscoop::PartnerLook::net && !d.settings.probe_ps && d.look_forced_rev == 1, "configure");
}
} // namespace

int main() {
    TestCfgAndMissionBoundaries();
    TestStageThreeTimeline();
    TestTriggerStopAndTeardown();
    TestTeardownKeepAndRefusals();
    TestStateRefusalFlips();
    TestRealizeThrashCap();
    TestAllActiveAndFlags();
    TestMirrorAndOneShots();
    TestForceExistsAndPolicy();
    TestNetSourceAndCadence();
    TestLuaLogAndQuiet();
    TestAnimationApply();
    TestAnimationGuards();
    TestTransformDrivers();
    TestLooks();
    TestCensusAndProbes();
    std::printf("%u partner driver checks passed: CFG/boundaries, stage-3 timeline, trigger stop/stage, STOP while terminating, "
                "teardown keep, refusal caps, realize thrash cap, all-active re-assert, flags, mirror lifecycle, one-shots, "
                "force_exists, policy, net source, cadence, Lua notes, quiet pairing, probes, anim mirror/net (q/s/r, debounce, "
                "revert, fault), xform/hybrid drivers, looks (quiet/snake/net/copy, verify, timeout, waiting, refusals), census, "
                "ps probe; no game\n", checks);
    return 0;
}
