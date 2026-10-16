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
#include "mgscoop/partner_aim.hpp"
#include "mgscoop/partner_driver.hpp"
#include "mgscoop/partner_net.hpp"
#include "mgscoop/partner_rates.hpp"
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
    bool support_ready=false;
    int support_identity=-1;   // the last publish_support_identity value (-1: never published)
    std::vector<std::string> lines;
    pp::State state{};
    pp::Refusal refusal = pp::Refusal::none;
    std::vector<std::string> writes;
    const char* write_result = nullptr;  // forced refusal for every write
    std::uint8_t policy_byte = 0;
    mgscoop::pose::Sample sample{};
    mgscoop::pose::SnapshotRing raw_ring;
    std::uint64_t raw_now{};
    unsigned submits = 0, samples = 0, slot_attempts = 0;
    std::string trigger;
    bool trigger_present = false, trigger_unreadable = false;
    unsigned trigger_takes = 0;
    bool focused = true;
    bool native_pose_held = false;
    bool anim_ok = false;
    pp::Anim anim{};
    mgscoop::sync::AnimState last_submit_anim{};
    partner::Tick last_submit_tick{};
    // Local instance-0 body transform (sender facing).
    bool local_body_ok = false;
    float local_body_pos[4]{}, local_body_quat[4] = {0, 0, 0, 1};
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
    bool xform_applies = false;  // the body takes the written rotation (facing governor tests)
    float xform_pos[4]{}, xform_quat[4]{};
    bool body_ok = false;
    float body_pos[4]{}, body_quat[4] = {0, 0, 0, 1};
    pp::Census census{};
    const char* census_refusal = nullptr;
    unsigned census_runs = 0;
    unsigned ps_probes = 0;
    // Round 3 input lever.
    const char* input_targets_refusal = nullptr;
    pp::InputTargets input_targets{};
    mgscoop::partner_input::PadRaw pad{};
    bool pad_ok = true;
    unsigned pad_reads = 0, gun_reads[2]{}, slots_reads[2]{}, rates_reads[2]{};
    std::vector<mgscoop::partner_input::InputPacket> inputs;
    bool net_running = true;
    const char* install_result = nullptr;
    unsigned installs = 0;
    std::vector<mgscoop::partner_input::Arm> arms;
    pd::InputStats stats{};
    bool hook_live = true;
    // Round 3 direct motion.
    const char* dm_targets_refusal = nullptr;
    pp::DmTargets dm_targets{};
    pp::DmState dm{};
    bool dm_ok = true;
    unsigned dm_motion_count = 31;
    int dm_patch_calls = 0;
    bool dm_patched = false;
    const char* dm_patch_reason = nullptr;
    bool dm_cleared = false;
    unsigned dm_clear_calls = 0;
    // dm=map: the game reacts to the reply's DM ops (dm_auto): a play starts
    // the clip (q18[1] = 0x8b) or, unconsumed, stays pending (g388 bit 4); a
    // stop ends it and clears g388 0x3C. A failing patch logs like the
    // comparator check.
    bool dm_auto = false, dm_consume = true, dm_stops = true;
    bool dm_patch_fail = false;
    unsigned fault_resets = 0;
    // Round 4.
    const char* position_result = nullptr;
    float position[4]{};
    mgscoop::partner_input::Sample latest{};
    bool latest_ok = true;
    unsigned wrap_installs = 0;
    std::vector<std::uint32_t> sample_delays;
    // Round 4 review: the aligned target (delay <= 50 ms) differs from the
    // 100 ms interpolated pose when sample_by_delay is set.
    bool sample_by_delay = false;
    mgscoop::pose::Sample sample_aligned{};
    bool dm_revert_fail = false;
    // Native puppet fakes (docs/native-puppet.md).
    const char* defer_install_result = nullptr;
    unsigned defer_installs = 0;
    mgscoop::PartnerDefer defer_install_mode = mgscoop::PartnerDefer::off;
    pd::NativeInstall native_install{};
    mgscoop::partner_defer::Stats defer{};
    std::vector<std::string> native_order;   // "identity", "identity_clear", "defer:1", "defer:0", "arm:1", "arm:0"
    bool identity_valid = false;
    mgscoop::partner_defer::Identity identity{};
    unsigned roots_published = 0;
    bool roots_census = false;
    std::uintptr_t roots_last[2] = {0, 0};
    const char* native_targets_refusal = nullptr;
    pp::NativeTargets native_targets{};
    pp::Gun1 gun{};
    bool gun_ok = false;
    pp::LookSource look_source{};
    bool look_source_ok = false;
    std::uint32_t window_seq = 0;
    // Native rates: the records ReadRates returns for instance 0 / 1 and the forget calls.
    pp::RateRead rates[2]{};
    bool rates_ok[2] = {false, false};
    unsigned rates_forgets = 0;
    std::uint32_t rates_forget_epoch = 0xFFFFFFFFu;
    bool link_live = true;
    std::uint32_t link_session = 7;
    // Aim parity: the publications, the hook's statistics and the aim read.
    std::vector<mgscoop::partner_aim_point::Publication> aim_pubs;
    mgscoop::partner_aim_point::Stats aim_stats{};
    pp::AimRead aim_read{};
    bool aim_read_ok = false;
    std::vector<std::vector<std::uint8_t>> channel2;
    unsigned channel2_drains = 0;
    // Native weapons fakes (docs/native-puppet.md "Weapons").
    const char* fire_install_result = nullptr;
    unsigned fire_installs = 0;
    // Native polish fakes: the noise filter, a batch in flight, the look source.
    const char* noise_install_result = nullptr;
    unsigned noise_installs = 0;
    bool in_batch = false;
    const char* look_src_result = nullptr;
    unsigned look_src_installs = 0;
    std::uint16_t fire_owner = 0xFFFF;
    mgscoop::partner_fire_filter::Stats fire{};
    mgscoop::partner_fire_filter::Census fire_census{};
    pp::Gun1 gun0{};
    bool gun0_ok = false;
    pp::Loadout loadout{};
    bool loadout_ok = false;
    unsigned loadout_reads = 0;
    // read_state calls (the early manifest never reads partner state).
    unsigned state_reads = 0;
    std::vector<std::vector<std::uint8_t>> reliable_sent;
    bool reliable_ok = true;
    // The partner's own reload and weapon switch: instance k's slots and the equip types.
    pp::Slots slots[2]{};
    bool slots_ok[2] = {false, false};
    std::uint8_t equip_types[700]{};
    // Input frame v5 negotiation: the peer was seen sending v5 (partner_net::PeerSendsInputV5).
    bool peer_v5 = true;
    bool peer_v6 = false;
    // The seated vehicle rider's dwords (partner_ride_seat).
    unsigned seat_arms = 0, seat_reverts = 0;
    bool seat_arm_ok = true;
    int seat_armed = -1;
    std::string seat_revert_reason;

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
    ++F(context).state_reads;
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
    Fake& f = F(context); ++f.slot_attempts;
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
mgscoop::pose::Sample HookSample(void* context, std::uint32_t delay) noexcept {
    ++F(context).samples;
    F(context).sample_delays.push_back(delay);
    if (F(context).sample_by_delay && delay <= 50) return F(context).sample_aligned;
    return F(context).sample;
}
void HookSubmit(void* context, const partner::Tick& tick, const mgscoop::sync::AnimState& anim) noexcept {
    ++F(context).submits;
    F(context).last_submit_anim = anim;
    F(context).last_submit_tick = tick;
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
const char* HookInputTargets(void* context, const pp::State&, pp::InputTargets& out) noexcept {
    out = F(context).input_targets;
    out.refusal = F(context).input_targets_refusal;
    return out.refusal;
}
std::size_t HookFormatInputCensus(void* context, const pp::InputTargets& t, char* out, std::size_t cap) noexcept {
    (void)context;
    const int n = std::snprintf(out, cap, "partner_input_census; verdict=%s\r\n", t.refusal ? t.refusal : "ok");
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}
bool HookReadPad(void* context, const pp::State&, mgscoop::partner_input::PadRaw& out) noexcept {
    ++F(context).pad_reads;
    out = F(context).pad;
    return F(context).pad_ok;
}
bool HookSubmitInput(void* context, const mgscoop::partner_input::InputPacket& p) noexcept {
    F(context).inputs.push_back(p);
    return true;
}
bool HookNetRunning(void* context) noexcept { return F(context).net_running; }
const char* HookInstallHook(void* context) noexcept { ++F(context).installs; return F(context).install_result; }
void HookPublishArm(void* context, const mgscoop::partner_input::Arm& a) noexcept {
    F(context).arms.push_back(a);
    F(context).native_order.push_back(a.enabled ? "arm:1" : "arm:0");
}
void HookDrainStats(void* context, pd::InputStats& out) noexcept { out = F(context).stats; }
bool HookHookLive(void* context) noexcept { return F(context).hook_live; }
const char* HookDmTargets(void* context, const pp::State&, pp::DmTargets& out) noexcept {
    out = F(context).dm_targets;
    out.refusal = F(context).dm_targets_refusal;
    return out.refusal;
}
bool HookReadDm(void* context, const pp::State&, const pp::DmTargets&, pp::DmState& out) noexcept {
    out = F(context).dm;
    return F(context).dm_ok;
}
std::size_t HookFormatDmCensus(void* context, const pp::DmTargets& t, char* out, std::size_t cap) noexcept {
    (void)context;
    const int n = std::snprintf(out, cap, "partner_dm_census; verdict=%s\r\n", t.refusal ? t.refusal : "ok");
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}
std::size_t HookFormatDmProbe(void* context, const pp::DmState&, bool patched, char* out, std::size_t cap) noexcept {
    (void)context;
    const int n = std::snprintf(out, cap, "partner_dm; patched=%u\r\n", patched ? 1u : 0u);
    return n > 0 ? static_cast<std::size_t>(n) : 0;
}
bool HookDmPatch(void* context, bool on, const char* reason) noexcept {
    Fake& f = F(context);
    ++f.dm_patch_calls;
    f.dm_patch_reason = reason;
    if (!on && f.dm_revert_fail && f.dm_patched) return true;
    // As partner_dm_patch: a refused patch logs its reason, a change logs patched/reverted.
    if (on && f.dm_patch_fail) {
        f.lines.emplace_back("partner_dm=refused; reason=comparator_bytes\r\n");
        return f.dm_patched;
    }
    if (on && !f.dm_patched) f.lines.emplace_back("partner_dm=patched; rva=0x1198e25; old=440f44c7; new=0f1f4000\r\n");
    if (!on && f.dm_patched) f.lines.emplace_back(std::string("partner_dm=reverted; reason=") + (reason ? reason : "unknown") + "\r\n");
    f.dm_patched = on;
    return f.dm_patched;
}
bool HookDmClearPending(void* context) noexcept {
    Fake& f = F(context);
    ++f.dm_clear_calls;
    if (f.dm_cleared) f.dm.g388 = static_cast<std::uint8_t>(f.dm.g388 & ~4u);
    return f.dm_cleared;
}
void HookResetFault(void* context) noexcept {
    ++F(context).fault_resets;
    F(context).stats.window.fault_phase = 0;
    F(context).stats.window.fault_code = 0;
}
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
    if (f.xform_applies) std::memcpy(f.body_quat, quat, sizeof(quat));
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

const char* HookApplyPosition(void* context, bool, const float (&pos)[4]) noexcept {
    Fake& f = F(context);
    if (f.position_result) return f.position_result;
    f.calls.push_back("pos");
    std::memcpy(f.position, pos, sizeof(pos));
    return nullptr;
}
bool HookReadVelocity(void*, const pp::State&, float (&vel)[4]) noexcept {
    vel[0] = 1.0f; vel[1] = 0.0f; vel[2] = 0.0f; vel[3] = 0.0f;
    return true;
}
bool HookLatestInput(void* context, mgscoop::partner_input::Sample& out) noexcept {
    out = F(context).latest;
    return F(context).latest_ok;
}
const char* HookInstallWrap(void* context) noexcept { ++F(context).wrap_installs; return nullptr; }
unsigned HookDmMotionCount(void* context) noexcept { return F(context).dm_motion_count; }

// --- Native puppet hooks ------------------------------------------------------------
const char* HookInstallDefer(void* context, mgscoop::PartnerDefer mode) noexcept {
    Fake& f = F(context);
    ++f.defer_installs;
    f.defer_install_mode = mode;
    return f.defer_install_result;
}
void HookNativeInstall(void* context, pd::NativeInstall& out) noexcept { out = F(context).native_install; }
void HookDrainDefer(void* context, mgscoop::partner_defer::Stats& out) noexcept { out = F(context).defer; }
void HookSetDefer(void* context, bool armed, mgscoop::PartnerDefer) noexcept {
    F(context).native_order.push_back(armed ? "defer:1" : "defer:0");
}
void HookPublishIdentity(void* context, const mgscoop::partner_defer::Identity& id, bool valid) noexcept {
    Fake& f = F(context);
    f.identity = id;
    if (valid != f.identity_valid) f.native_order.push_back(valid ? "identity" : "identity_clear");
    f.identity_valid = valid;
}
void HookPublishRoots(void* context, std::uintptr_t root0, std::uintptr_t root1, bool census) noexcept {
    ++F(context).roots_published;
    F(context).roots_census = census;
    F(context).roots_last[0] = root0;
    F(context).roots_last[1] = root1;
}
const char* HookNativeTargets(void* context, const pp::State&, const pp::InputTargets&, pp::NativeTargets& out) noexcept {
    out = F(context).native_targets;
    out.refusal = F(context).native_targets_refusal;
    return out.refusal;
}
bool HookReadGun1(void* context, const pp::State&, pp::Gun1& out) noexcept {
    out = F(context).gun;
    return F(context).gun_ok;
}
bool HookReadLookSource(void* context, const pp::State&, pp::LookSource& out) noexcept {
    out = F(context).look_source;
    return F(context).look_source_ok;
}
std::uint32_t HookWindowSeq(void* context) noexcept { return F(context).window_seq; }
bool HookReadRates(void* context, const pp::State&, std::uint32_t index, pp::RateRead& out) noexcept {
    if (index < 2) ++F(context).rates_reads[index];
    out = index < 2 ? F(context).rates[index] : pp::RateRead{};
    return index < 2 && F(context).rates_ok[index];
}
void HookForgetRates(void* context, std::uint32_t epoch) noexcept {
    ++F(context).rates_forgets;
    F(context).rates_forget_epoch = epoch;
    F(context).native_order.push_back("rates_forget");
}
bool HookLinkLive(void* context) noexcept { return F(context).link_live; }
std::uint32_t HookLinkSession(void* context) noexcept { return F(context).link_session; }
bool HookDrainReliable(void* context, std::uint8_t channel, std::uint8_t* out, std::size_t capacity, std::size_t& size) noexcept {
    Fake& f = F(context);
    ++f.channel2_drains;
    if (channel != 2 || f.channel2.empty() || f.channel2.front().size() > capacity) return false;
    size = f.channel2.front().size();
    std::memcpy(out, f.channel2.front().data(), size);
    f.channel2.erase(f.channel2.begin());
    return true;
}
const char* HookInstallFire(void* context, std::uint16_t owner) noexcept {
    Fake& f = F(context);
    ++f.fire_installs;
    f.fire_owner = owner;
    if (f.fire_install_result) return f.fire_install_result;
    if (!f.fire.installed) {
        f.fire.installed = f.fire.live = f.fire.armed = true;
        f.fire.owner = owner;
    }
    return nullptr;
}
void HookFireStats(void* context, mgscoop::partner_fire_filter::Stats& out) noexcept { out = F(context).fire; }
const char* HookInstallNoise(void* context) noexcept {
    Fake& f = F(context);
    ++f.noise_installs;
    if (f.noise_install_result) return f.noise_install_result;
    if (!f.fire.noise_installed) f.fire.noise_installed = f.fire.noise_live = true;
    return nullptr;
}
bool HookInBatch(void* context) noexcept { return F(context).in_batch; }
const char* HookInstallLookSource(void* context) noexcept {
    Fake& f = F(context);
    ++f.look_src_installs;
    return f.look_src_result;
}
void HookFireCensus(void* context, mgscoop::partner_fire_filter::Census& out) noexcept { out = F(context).fire_census; }
bool HookReadGun(void* context, const pp::State&, std::uint32_t k, pp::Gun1& out) noexcept {
    Fake& f = F(context); if (k < 2) ++f.gun_reads[k];
    out = k ? f.gun : f.gun0;
    return k ? f.gun_ok : f.gun0_ok;
}
bool HookReadLoadout(void* context, pp::Loadout& out) noexcept {
    ++F(context).loadout_reads;
    out = F(context).loadout;
    return F(context).loadout_ok;
}
bool HookSendReliable(void* context, std::uint8_t channel, const std::uint8_t* bytes, std::size_t size) noexcept {
    Fake& f = F(context);
    if (!f.reliable_ok || channel != 2) return false;
    f.reliable_sent.emplace_back(bytes, bytes + size);
    return true;
}
void HookPublishAim(void* context, const mgscoop::partner_aim_point::Publication& publication) noexcept {
    F(context).aim_pubs.push_back(publication);
}
void HookAimStats(void* context, mgscoop::partner_aim_point::Stats& out) noexcept { out = F(context).aim_stats; }
bool HookReadAim(void* context, const pp::State&, pp::AimRead& out) noexcept {
    out = F(context).aim_read;
    return F(context).aim_read_ok;
}
bool HookReadSlots(void* context, const pp::State&, std::uint32_t k, pp::Slots& out) noexcept {
    Fake& f = F(context); if (k < 2) ++f.slots_reads[k];
    out = k < 2 ? f.slots[k] : pp::Slots{};
    return k < 2 && f.slots_ok[k];
}
bool HookEquipType(void* context, std::uint32_t equip, std::uint8_t& type) noexcept {
    Fake& f = F(context);
    type = equip < 700 ? f.equip_types[equip] : 0;
    return type != 0;
}
bool HookPeerInputV5(void* context) noexcept { return F(context).peer_v5; }
bool HookPeerInputV6(void* context) noexcept { return F(context).peer_v6; }
void AddWeaponHooks(pd::Hooks& h) {
    h.read_slots = HookReadSlots;
    h.equip_type = HookEquipType;
    h.peer_input_v5 = HookPeerInputV5;
    h.install_fire = HookInstallFire;
    h.fire_stats = HookFireStats;
    h.fire_census = HookFireCensus;
    h.read_gun = HookReadGun;
    h.read_loadout = HookReadLoadout;
    h.send_reliable = HookSendReliable;
    h.install_noise = HookInstallNoise;
    h.in_batch = HookInBatch;
    h.install_look_source = HookInstallLookSource;
}
void AddNativeHooks(pd::Hooks& h) {
    h.install_defer = HookInstallDefer;
    h.native_install = HookNativeInstall;
    h.drain_defer_stats = HookDrainDefer;
    h.set_defer = HookSetDefer;
    h.publish_identity = HookPublishIdentity;
    h.publish_wrap_roots = HookPublishRoots;
    h.native_targets = HookNativeTargets;
    h.read_gun1 = HookReadGun1;
    h.read_look_source = HookReadLookSource;
    h.window_seq = HookWindowSeq;
    h.link_live = HookLinkLive;
    h.link_session = HookLinkSession;
    h.drain_reliable = HookDrainReliable;
    h.read_rates = HookReadRates;
    h.forget_rates = HookForgetRates;
}

pd::Hooks HooksFor(Fake& f) {
    return pd::Hooks{&f, HookLog, HookRead, HookOr, HookClear, HookSlot, HookForce, HookPolicy, HookAnim, HookSample,
                     HookSubmit, HookTrigger, HookFocused, HookReadLooks, HookSubmitLook, HookSampleLook, HookWriteAnim,
                     HookWriteFaceEquip, HookSetStance, HookRequestStance, HookApplyTransform, HookReadBody, HookCensus,
                     HookFormatCensus, HookFormatPs, HookInputTargets, HookFormatInputCensus, HookReadPad, HookSubmitInput,
                     HookNetRunning, HookInstallHook, HookPublishArm, HookDrainStats, HookHookLive, HookDmTargets, HookReadDm,
                     HookFormatDmCensus, HookFormatDmProbe, HookDmPatch, HookDmClearPending, HookResetFault,
                     HookApplyPosition, HookReadVelocity, HookLatestInput, HookInstallWrap, nullptr, HookDmMotionCount};
}

std::string TickSource(int frame, int luaflags, int x = 1000, int y = 2000, int z = 3000, int yaw = 4500, int cam = 0, int stance = 0,
                       int move = 0) {
    char text[200]{};
    std::snprintf(text, sizeof(text), "return 'MGS_COOP_PARTNER_V1' -- %d %d %d %d %d %d %d %d %d", frame, x, y, z, yaw, cam, stance,
                  move, luaflags);
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
    // The local PlayerStatus stance/move every tick reports, the gani of each
    // DM_PLAY sent, and replies that carried both DM_PLAY and DM_STOP.
    int stance = 0, move = 0;
    std::vector<int> played;
    unsigned both_dm_ops = 0;

    explicit Session(std::uint32_t stage, mgscoop::PartnerApply apply = mgscoop::PartnerApply::warp,
                     mgscoop::PartnerSource source = mgscoop::PartnerSource::fixed,
                     std::uint32_t mission = 30050) : hooks(HooksFor(fake)) {
        lab.partner = true;
        lab.partner_stage = stage;
        lab.partner_apply = apply;
        lab.partner_source = source;
        pd::Configure(driver, lab);
        pd::MissionStart(driver, hooks, mission);
        fake.state.flags = c::flag_valid | c::flag_all_active;  // as after Lua's SET
        Check(pd::Cfg(driver, hooks, nullptr, now), "cfg ready");
        fake.lines.clear();
    }
    // One frame: advances the clock, returns whether the tick was replied.
    bool Tick(int luaflags, std::uint64_t step_ms = 33, bool terminating = false) {
        now += step_ms;
        const std::string source = TickSource(++frame, luaflags, 1000, 2000, 3000, 4500, 0, stance, move);
        reply = partner::Reply{};
        const bool replied = pd::Tick(driver, hooks, lab, source.data(), source.size(), pd::Clock{now, now + 5000}, terminating, reply);
        if (replied && (reply.op & op::dm_play)) played.push_back(reply.gani);
        if (replied && (reply.op & op::dm_play) && (reply.op & op::dm_stop)) ++both_dm_ops;
        if (replied && fake.dm_auto) {
            if (reply.op & op::dm_play) {
                if (fake.dm_consume) {
                    fake.dm.q18[1] = c::q18_dm_act;
                    fake.dm.g388 = static_cast<std::uint8_t>(fake.dm.g388 & ~4u);
                } else {
                    fake.dm.g388 = static_cast<std::uint8_t>(fake.dm.g388 | 4u);
                }
            }
            if ((reply.op & op::dm_stop) && fake.dm_stops) {
                fake.dm.q18[1] = 3;
                fake.dm.g388 = static_cast<std::uint8_t>(fake.dm.g388 & ~0x3Cu);
            }
        }
        return replied;
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
    pd::MissionEnd(d, hooks);
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
    Check(s.fake.Has("partner_stage=changed; stage=0; source=fixed; apply=warp; anim=off; aset=qs; reset=n; look=copy; probe=off; lookgate=1; lookrecover=1; input=off; cam=sandbox; hold=own; yaw=rot; words=hpr; mask=0x2240006; correct=3000; isend=0; dm=off; recon=track; align=0; slot=body; runaway=pin; wrap=off; via=trigger\r\n") &&
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
    pd::MissionEnd(n.driver, n.hooks);
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

void TestHiddenLocalSender() {
    namespace pi = mgscoop::partner_input;
    Session s(3,mgscoop::PartnerApply::xform,mgscoop::PartnerSource::net,30050);
    AddNativeHooks(s.hooks); AddWeaponHooks(s.hooks);
    s.hooks.peer_input_v6=HookPeerInputV6; s.fake.peer_v6=true;
    s.driver.settings.nride=mgscoop::PartnerNativeRide::hide;
    s.driver.settings.isend=true; s.driver.settings.nrates=mgscoop::PartnerNativeRates::mirror;
    auto& target=s.fake.sample;
    target.valid=target.time_valid=target.anim_valid=true; target.status=mgscoop::pose::Status::live;
    target.act=99; target.x=1; target.y=20; target.z=3; target.sender_ms=1000; target.generation=1;
    s.fake.gun0_ok=true; s.fake.gun0.fields_ok=true; s.fake.gun0.fields.flags27c=1u<<6; s.fake.gun0.fields.cap=30;
    s.fake.slots_ok[0]=true; s.fake.rates_ok[0]=true;
    for(float& value : s.fake.rates[0].f) value=1.0f;
    for(unsigned i=0;i<6;++i) {
        s.fake.pad.src_w[0]=(i%2) ? 0x02000000u : 8u;
        s.fake.gun0.fields.equip=static_cast<std::uint16_t>(46+i); s.fake.gun0.fields.mag=static_cast<std::uint16_t>(5+i);
        s.fake.gun0.gun3c0=c::gun_flag_in_hand | ((i%2) ? 0u : c::gun_flag_hold);
        s.fake.gun0.mode_cur=static_cast<std::uint8_t>(i<3 ? 7 : 11);
        s.fake.slots[0].nibble=static_cast<std::uint8_t>(i%3);
        target.sender_ms+=100;
        Check(s.Tick(1,100) && s.reply.op==0 && s.fake.inputs.size()==i+1 && s.fake.pad_reads==i+1,
              "local on-foot pad continues once per owner tick while remote heli stays hidden");
        const auto& sent=s.fake.inputs.back();
        Check(sent.version==6 && sent.held==s.fake.pad.src_w[0] && sent.gun_equip==46+i && sent.mag==5+i &&
              sent.inv_slot==i%3 && (sent.gun_flags & pi::GunFlag::Valid) && (sent.xflags & pi::XFlag::Rates) && sent.rate_dash==1000,
              "hidden sender preserves v6 and refreshes current local held/equipment/magazine/slot/rates");
        Check(((sent.gun_flags & pi::GunFlag::Reloading)!=0)==(i>=3), "hidden sender updates executed local reload mode");
    }
    Check(s.fake.inputs.back().reload_seq==1 && s.fake.gun_reads[0]==6 && s.fake.slots_reads[0]==6 &&
          s.fake.rates_reads[0]==6 && s.fake.gun_reads[1]==0 && s.fake.slots_reads[1]==0 && s.fake.rates_reads[1]==0,
          "hidden sender refreshes only local instance0; reload counter advances once");
    Check(s.fake.Writes("slot")==0 && s.fake.Writes("or")==0 && s.fake.calls.empty() && s.fake.installs==0 &&
          s.fake.defer_installs==0 && s.fake.wrap_installs==0 && s.fake.fire_installs==0 && s.fake.look_src_installs==0 &&
          !s.driver.mission.input_armed && !s.driver.mission.realized,
          "hidden sender has no remote transforms, realization, native hook installs or enabled Arm");
    // Sender hold (docs/anim-round2-throws.md "Unknown-slot glitch", run 0c9269ce): one
    // failed local gun and slot read 33 ms after the last good one republishes the last
    // good equip, gun group (counters kept) and slot instead of sending them unknown.
    {
        const pi::InputPacket prev=s.fake.inputs.back();
        Check((prev.gun_flags & pi::GunFlag::Valid) && (prev.xflags & pi::XFlag::Equip) && prev.inv_slot<=pi::kInvSlotMax,
              "sender hold: the last good frame carries the gun group, equip and slot");
        const auto before=s.fake.inputs.size();
        s.fake.gun0_ok=false; s.fake.gun0.refusal="gun_node"; s.fake.slots_ok[0]=false; s.fake.slots[0].refusal="rows";
        Check(s.Tick(1) && s.fake.inputs.size()==before+1,"sender hold: a failed local read still sends");
        const pi::InputPacket held=s.fake.inputs.back();
        Check(held.gun_flags==prev.gun_flags && held.gun_mode==prev.gun_mode && held.reload_seq==prev.reload_seq &&
              held.switch_seq==prev.switch_seq && (held.xflags & pi::XFlag::Equip) && held.gun_equip==prev.gun_equip &&
              held.inv_slot==prev.inv_slot && (held.xflags & pi::XFlag::Mag)==(prev.xflags & pi::XFlag::Mag) && held.mag==prev.mag &&
              pi::ValidPacket(held),
              "sender hold: the frame republishes the last good gun group, equip, magazine and slot");
        Check(s.fake.Has("partner_input=sender_hold; field=gun; reason=start; refusal=gun_node; good_age_ms=33\r\n") &&
              s.fake.Has("partner_input=sender_hold; field=slot; reason=start; refusal=rows; good_age_ms=33\r\n"),
              "sender hold: the start lines name the failed read's refusal");
        s.fake.gun0_ok=true; s.fake.gun0.refusal=nullptr; s.fake.slots_ok[0]=true; s.fake.slots[0].refusal=nullptr;
        Check(s.Tick(1) && s.fake.inputs.back().gun_equip==prev.gun_equip && s.fake.inputs.back().reload_seq==prev.reload_seq &&
              s.fake.inputs.back().switch_seq==prev.switch_seq,"sender hold: the next good read goes out (no counter step across the hold)");
        Check(s.fake.Has("partner_input=sender_hold; field=gun; reason=resumed; frames=1; held_ms=33\r\n") &&
              s.fake.Has("partner_input=sender_hold; field=slot; reason=resumed; frames=1; held_ms=33\r\n"),"sender hold: the resumed lines");
    }
    const auto packets=s.fake.inputs.size(); const auto pads=s.fake.pad_reads;
    s.fake.link_live=false; Check(s.Tick(1) && s.fake.inputs.size()==packets && s.fake.pad_reads==pads,"hidden sender requires live link lease");
    s.fake.link_live=true; s.fake.link_session=0;
    Check(s.Tick(1) && s.fake.inputs.size()==packets && s.fake.pad_reads==pads,"hidden sender requires current nonzero session");
    s.fake.link_session=7; s.fake.state.ids[0]=0xFFFF;
    Check(s.Tick(1) && s.fake.inputs.size()==packets && s.fake.pad_reads==pads,"unrealized local0 sends no guessed pad");
    s.fake.state.ids[0]=0; s.fake.state.parts[0]=0;
    Check(s.Tick(1) && s.fake.inputs.size()==packets && s.fake.pad_reads==pads,"unloaded local0 sends no pad");
    s.fake.state.parts[0]=3; s.fake.state.local=1;
    Check(s.Tick(1) && s.fake.inputs.size()==packets && s.fake.pad_reads==pads,"unexpected local index sends no pad");
    s.fake.state.local=0; s.fake.pad_ok=false;
    const auto localReads=s.fake.gun_reads[0];
    Check(s.Tick(1) && s.fake.inputs.size()==packets && s.fake.pad_reads==pads+1 && s.fake.gun_reads[0]==localReads,
          "refused fresh pad read sends no packet and does not reuse cached gun sources");
    s.fake.pad_ok=true; s.fake.gun0_ok=s.fake.slots_ok[0]=s.fake.rates_ok[0]=false;
    Check(s.Tick(1,100) && s.fake.inputs.size()==packets+1,"fresh local pad still sends with unknown optional sources");
    const auto& unreadable=s.fake.inputs.back();
    Check(!(unreadable.gun_flags & pi::GunFlag::Valid) && !(unreadable.xflags & (pi::XFlag::Mag | pi::XFlag::Equip | pi::XFlag::Rates)) &&
          unreadable.inv_slot==pi::kInvSlotUnknown,
          "unreadable local gear/rates beyond the sender hold (298 ms after the last good read) never repeat old valid metadata");
    const auto after=s.fake.inputs.size(); const auto readAfter=s.fake.pad_reads;
    Check(!s.Tick(1,33,true) && s.fake.inputs.size()==after && s.fake.pad_reads==readAfter,"terminating hidden sender does nothing");
    s.driver.mission.stopped=true;
    Check(!s.Tick(1) && s.fake.inputs.size()==after && s.fake.pad_reads==readAfter,"stopped owner has no hidden sender");
    // Normal sender remains at the original TickInput location, one packet
    // per owner tick, with the exact integrated v6 negotiation retained.
    Session normal(1);
    normal.driver.settings.isend=true; normal.hooks.peer_input_v6=HookPeerInputV6; normal.fake.peer_v6=true;
    for(unsigned i=0;i<4;++i) Check(normal.Tick(0) && normal.fake.inputs.size()==i+1 && normal.fake.pad_reads==i+1 &&
                                 normal.fake.inputs.back().version==6,"normal path send cadence and v6 unchanged");
}

void TestRideVisibility() {
    namespace rw = mgscoop::partner_ride_wire;
    for (std::uint32_t mission : {30010u, 30020u, 30050u}) {
        Session s(3, mgscoop::PartnerApply::xform, mgscoop::PartnerSource::net, mission);
        s.lab.partner_native_ride = mgscoop::PartnerNativeRide::hide;
        s.driver.settings.nride = mgscoop::PartnerNativeRide::hide;
        AddNativeHooks(s.hooks); s.hooks.send_reliable_append = HookSendReliable;
        pd::MissionStart(s.driver,s.hooks,mission); Check(pd::Cfg(s.driver,s.hooks,nullptr,s.now), "ride CFG after mission reset");
        auto& sample=s.fake.sample;
        sample.valid=sample.time_valid=sample.anim_valid=true; sample.status=mgscoop::pose::Status::live;
        sample.x=1; sample.y=20; sample.z=3; sample.act=99; sample.sender_ms=1000;
        rw::Packet ride{rw::Kind::heli,1,1000}; std::uint8_t bytes[rw::kSize]{};
        rw::Encode(ride,bytes); s.fake.channel2.emplace_back(bytes,bytes+sizeof(bytes));
        const auto submits=s.fake.submits;
        Check(s.Tick(1) && s.reply.op==0 && s.fake.Writes("slot")==0 && s.fake.Writes("or")==0 &&
              s.driver.mission.visibility.hidden && s.fake.submits==submits+1 && !s.driver.mission.realized,
              "all partner-body missions hide helicopter without movement/realization and still transmit");
        ride.kind=rw::Kind::foot; ride.revision=2; ride.sender_ms=2000;
        rw::Encode(ride,bytes); s.fake.channel2.emplace_back(bytes,bytes+sizeof(bytes));
        sample.act=1; sample.y=2; sample.sender_ms=2000;
        Check(s.Tick(1,100) && s.reply.op==0, "foot state begins landing stability proof");
        for(unsigned i=0;i<8;++i) Check(s.Tick(1,100) && s.fake.Writes("slot")==0, "repeated sender time cannot re-show");
        for(unsigned i=1;i<5;++i) { sample.sender_ms=2000+i*100; Check(s.Tick(1,100) && s.fake.Writes("slot")==0,"stable landing waits500ms"); }
        sample.sender_ms=2500;
        Check(s.Tick(1,100) && s.reply.op==0 && s.fake.Writes("slot")==1 && s.fake.Writes("or")==0,
              "landing first writes checked slot; no realize or Lua effects on same tick");
        sample.sender_ms=2600;
        Check(s.Tick(1,100) && s.fake.Writes("or")==1, "landing realizes on next fresh tick");
        s.fake.Realize(true); s.fake.state.parts[1]=3; s.fake.anim_ok=true; s.fake.anim.act[1]=1;
        sample.sender_ms=2700; Check(s.Tick(1,100), "ride body observed ready");
        s.driver.mission.input_armed=true; s.driver.mission.native_defer_armed=true;
        s.driver.mission.native_identity=s.driver.mission.native_roots=true;
        s.driver.mission.dm_patched=s.fake.dm_patched=true;
        const auto slotBefore=s.fake.Writes("slot"), clearBefore=s.fake.Writes("clear");
        ride.kind=rw::Kind::heli; ride.revision=3; ride.sender_ms=3000;
        rw::Encode(ride,bytes); s.fake.channel2.emplace_back(bytes,bytes+sizeof(bytes)); sample.act=99; sample.y=20; sample.sender_ms=3000;
        Check(s.Tick(1) && s.reply.op==0 && s.fake.Writes("slot")==slotBefore && s.fake.Writes("clear")==clearBefore+1 &&
              !s.driver.mission.input_armed && !s.driver.mission.native_defer_armed && !s.driver.mission.native_identity &&
              !s.driver.mission.native_roots && !s.driver.mission.dm_patched && !s.driver.mission.realized && !s.driver.mission.parts_loaded,
              "ride hide withdraws owner identity, roots, deferral, Arm and DM, and reports readiness conservatively");
        sample.act=1; sample.y=2; sample.sender_ms=3500;
        Check(s.Tick(1,6000) && s.reply.op==0 && s.fake.Writes("or")==1 && s.driver.mission.visibility.hidden,
              "expired heli lease stays hidden even if subsequent pose looks on foot");
        const auto sends=s.fake.reliable_sent.size();
        const auto drains=s.fake.channel2_drains;
        const auto writes=s.fake.writes.size();
        const auto stateReads=s.fake.state_reads;
        Check(!s.Tick(1,33,true) && s.fake.reliable_sent.size()==sends && s.fake.channel2_drains==drains && s.fake.writes.size()==writes && s.fake.state_reads==stateReads,
              "terminating driver does not drain, send, read state or write");
    }
    // Checked restore refusal and refused placement cannot grant a show.
    Session refused(3,mgscoop::PartnerApply::xform,mgscoop::PartnerSource::net,30050);
    AddNativeHooks(refused.hooks); refused.driver.settings.nride=mgscoop::PartnerNativeRide::hide;
    auto& target=refused.fake.sample;
    target.valid=target.time_valid=target.anim_valid=true; target.status=mgscoop::pose::Status::live;
    target.act=99; target.x=1; target.y=2; target.z=3; target.sender_ms=1000;
    refused.driver.mission.bits_set=true; refused.driver.mission.pose_applied=true;
    refused.fake.write_result="guard";
    Check(refused.Tick(1) && refused.driver.mission.visibility.hidden && refused.driver.mission.bits_set,
          "checked clear refusal remains hidden and records ownership for retry");
    target.act=1; target.sender_ms=2000; Check(refused.Tick(1,100),"restore refusal first foot observation");
    for(unsigned i=1;i<=5;++i) { target.sender_ms=2000+i*100; Check(refused.Tick(1,100),"restore refusal foot proof"); }
    Check(refused.reply.op==0 && refused.fake.Writes("slot")==0 && refused.driver.mission.bits_set,
          "stable foot cannot bypass refused clear before placement");
    refused.fake.write_result=nullptr; target.sender_ms=2600;
    Check(refused.Tick(1,100) && !refused.driver.mission.bits_set && refused.fake.Writes("slot")==1,
          "accepted restoration permits one checked placement");
    refused.fake.Realize(false); refused.driver.mission.range_needs_place=true; refused.driver.mission.pose_applied=false;
    refused.driver.mission.visibility_place_try_ms=0; refused.fake.write_result="guard"; target.sender_ms=2700;
    Check(refused.Tick(1,100) && refused.driver.mission.range_needs_place && !refused.driver.mission.pose_applied,
          "checked placement refusal retains unshown state");
    const auto attemptCount=refused.fake.slot_attempts;
    target.sender_ms=2800; Check(refused.Tick(1,100) && refused.fake.slot_attempts==attemptCount,
          "placement retry is bounded to1s rather than every frame");
    refused.fake.write_result=nullptr; target.sender_ms=3800;
    Check(refused.Tick(1,1000) && !refused.driver.mission.range_needs_place && refused.fake.Writes("slot")==2,
          "placement retries after1s through same checked API");
    // Legacy sender (no ride message): the verified helicopter act interval still blocks.
    Session legacy(3,mgscoop::PartnerApply::warp,mgscoop::PartnerSource::net,30050);
    AddNativeHooks(legacy.hooks); legacy.driver.settings.nride=mgscoop::PartnerNativeRide::hide;
    legacy.fake.sample.valid=legacy.fake.sample.time_valid=legacy.fake.sample.anim_valid=true;
    legacy.fake.sample.status=mgscoop::pose::Status::live; legacy.fake.sample.act=102;
    Check(legacy.Tick(1) && legacy.reply.op==0 && legacy.fake.Writes("slot")==0, "legacy helicopter act hides without ride transport");
    legacy.driver.settings.nride=mgscoop::PartnerNativeRide::off;
    Check(legacy.Tick(1) && (legacy.reply.op & op::warp), "ride guard off preserves baseline Mother Base behavior");
    // Producer uses append-mode; retries queued refusal and carries the latest kind.
    Session sender(0);
    AddNativeHooks(sender.hooks); sender.hooks.send_reliable_append=HookSendReliable;
    const auto rideSends=[&]() {
        unsigned n=0;
        for (const auto& item : sender.fake.reliable_sent) if (rw::IsRide(item.data(),item.size())) ++n;
        return n;
    };
    auto send=[&](unsigned kind,std::uint64_t step) {
        sender.now+=step;
        const auto source=TickSource(++sender.frame,0)+" 0 0 ride="+std::to_string(kind);
        return pd::Tick(sender.driver,sender.hooks,sender.lab,source.data(),source.size(),pd::Clock{sender.now,sender.now},false,sender.reply);
    };
    sender.fake.reliable_ok=false;
    Check(send(1,1) && sender.fake.reliable_sent.empty() && sender.driver.ride_tx_dirty,"refused ride send retains newest pending state");
    sender.fake.reliable_ok=true; Check(send(1,1) && rideSends()==1,"ride retry queues once via append mode");
    Check(send(1,1000) && rideSends()==1,"unchanged ride bounded below2s heartbeat");
    Check(send(1,1000) && rideSends()==2,"ride heartbeat at2s");
    Check(send(0,1) && rideSends()==3,"foot transition sends immediately");
    rw::Packet out{};
    for (const auto& item : sender.fake.reliable_sent) if (rw::IsRide(item.data(),item.size())) rw::Decode(item.data(),item.size(),out);
    Check(out.kind==rw::Kind::foot && out.revision==2,"foot packet has next nonzero revision");
}

void TestFreeRoamRange() {
    for (std::uint32_t mission : {30010u, 30020u}) {
        Session s(3, mgscoop::PartnerApply::xform, mgscoop::PartnerSource::net, mission);
        s.fake.sample.valid = true;
        s.fake.sample.status = mgscoop::pose::Status::live;
        s.fake.sample.x = 191.0f; s.fake.sample.y = 2.0f; s.fake.sample.z = 3.0f;
        Check(s.Tick(1) && s.reply.op == 0 && s.fake.Writes("slot") == 0 && s.fake.submits == 1,
              "free roam: initial hysteresis band stays hidden but sends local pose");
        s.fake.sample.x = 101.0f;
        s.fake.sample.status = mgscoop::pose::Status::stale;
        Check(s.Tick(1) && s.reply.op == 0 && s.fake.Writes("slot") == 0, "stale near sample cannot release range latch");
        s.fake.sample.status = mgscoop::pose::Status::live;
        s.fake.write_result = "fault";
        Check(s.Tick(1) && s.reply.op == 0 && s.driver.mission.range_needs_place && !s.driver.mission.pose_applied,
              "refused near placement cannot enable realization");
        s.fake.write_result = nullptr;
        Check(s.Tick(1) && s.reply.op == 0 && s.fake.Writes("slot") == 1 && s.fake.Writes("or") == 0,
              "fresh near sample writes slot first, with no Lua calls or realization");
        s.fake.sample.status = mgscoop::pose::Status::stale;
        Check(s.Tick(1) && s.reply.op == 0 && s.fake.Writes("or") == 0, "stale packet after placement cannot realize");
        s.fake.sample.status = mgscoop::pose::Status::live;
        Check(s.Tick(1) && s.fake.Writes("or") == 1 && !(s.reply.op & op::warp) && s.fake.Calls("xform:") == 0,
              "next fresh tick realizes without Warp into an unrealized body");
        s.fake.Realize(true); s.fake.state.parts[1] = 3;
        s.fake.xform_result = "unrealized";
        Check(s.Tick(1) && !(s.reply.op & op::warp) && s.fake.Has("partner_xform=refused; reason=unrealized; fallback=defer\r\n"),
              "free-roam native realization race does not fall back to Warp");
        s.fake.xform_result = nullptr;
        Check(s.Tick(1) && s.fake.Calls("xform:") == 1, "near validated body resumes transform");
        s.driver.mission.input_armed = true;
        s.driver.mission.dm_patched = s.fake.dm_patched = true;
        s.fake.sample.x = 201.0f; // exactly 200 m from the local receipt
        const auto calls = s.fake.calls.size();
        const auto slots = s.fake.Writes("slot");
        const auto submits = s.fake.submits;
        Check(s.Tick(1) && s.reply.op == 0 && s.fake.Writes("clear") == 1 && s.driver.mission.range_hidden &&
              !s.driver.mission.pose_applied && !s.driver.mission.input_armed && !s.driver.mission.dm_patched &&
              !s.driver.mission.realized && !s.driver.mission.parts_loaded && !s.driver.mission.body_ok &&
              s.fake.calls.size() == calls && s.fake.Writes("slot") == slots && s.fake.submits == submits + 1,
              "far latch clears own bits, disarms/reverts, suppresses movement and keeps sending");
        s.fake.sample.x = 161.0f;
        Check(s.Tick(1) && s.reply.op == 0 && s.fake.Writes("clear") == 1 && s.fake.Writes("or") == 1,
              "return hysteresis band remains hidden and does not thrash bits");
        s.fake.sample.x = 101.0f;
        s.fake.sample.status = mgscoop::pose::Status::lost;
        Check(s.Tick(1) && s.reply.op == 0 && s.driver.mission.range_hidden, "lost near sample cannot re-realize far latch");
        s.fake.sample.status = mgscoop::pose::Status::live;
        s.fake.Realize(false); s.fake.state.parts[1] = 0;
        Check(s.Tick(1) && s.reply.op == 0 && s.fake.Writes("or") == 1 && s.fake.Writes("slot") == slots + 1 &&
              !s.driver.mission.realized && !s.driver.mission.parts_loaded,
              "near return re-places before realizing again");
        Check(s.Tick(1) && s.fake.Writes("or") == 2 && !(s.reply.op & op::warp), "near return next tick realizes safely");
    }
    Session mb(3, mgscoop::PartnerApply::xform, mgscoop::PartnerSource::net);
    mb.fake.sample.valid = true; mb.fake.sample.status = mgscoop::pose::Status::live; mb.fake.sample.x = 500.0f;
    mb.fake.xform_result = "unrealized";
    Check(mb.Tick(1) && (mb.reply.op & op::warp) && mb.fake.Writes("slot") == 1 && mb.fake.Count("partner_range") == 0,
          "Mother Base keeps its proven transform fallback and has no distance latch");
    Session rearm(3, mgscoop::PartnerApply::warp, mgscoop::PartnerSource::net, 30010);
    rearm.fake.sample.valid = true; rearm.fake.sample.status = mgscoop::pose::Status::live;
    rearm.fake.sample.x = 1.0f; rearm.fake.sample.y = 2.0f; rearm.fake.sample.z = 3.0f;
    Check(rearm.Tick(1) && rearm.Tick(1) && rearm.driver.mission.bits_set, "free-roam Warp initially realizes after placement");
    rearm.Trigger("stage=0");
    Check(!rearm.driver.mission.bits_set && rearm.driver.mission.range_needs_place && !rearm.driver.mission.pose_applied,
          "stage decrease requires new free-roam placement");
    rearm.Trigger("stage=3");
    const auto ors = rearm.fake.Writes("or");
    Check(rearm.Tick(1) && rearm.reply.op == 0 && rearm.fake.Writes("or") == ors, "stage restart places without realizing on same tick");
    Check(rearm.Tick(1) && rearm.fake.Writes("or") == ors + 1, "stage restart realizes on next fresh tick");
    rearm.fake.Realize(true); rearm.fake.state.parts[1] = 3;
    Check(rearm.Tick(1) && rearm.driver.mission.realized, "body observed before engine unrealizes it");
    rearm.fake.Realize(false); rearm.fake.state.parts[1] = 0;
    Check(rearm.Tick(1) && rearm.reply.op == 0 && !rearm.driver.mission.realized && !rearm.driver.mission.bits_set,
          "engine unrealization clears old request and re-places safely");
    Check(rearm.Tick(1) && rearm.fake.Writes("or") == ors + 2, "engine-unrealized partner can realize after fresh placement");
}
mgscoop::pose::RemoteFrame HookSampleWithTurns(void* context, std::uint32_t delay,
                                            const mgscoop::pose::HistoryCursor& cursor) noexcept {
    Fake& f = F(context);
    f.sample_delays.push_back(delay);
    return f.raw_ring.ReadFrame(f.raw_now, delay, cursor);
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

std::uint32_t HookInterp(void*, std::uint32_t configured) noexcept { return configured + 23; }

// partner_interp=auto reaches the net sample through the interp_ms hook; a
// null hook keeps the configured delay. partner_lost=hide clears the realize
// bits after 10 s lost and the next live sample re-realizes; freeze keeps them.
void TestInterpHookAndLost() {
    Session s(1, mgscoop::PartnerApply::warp, mgscoop::PartnerSource::net);
    s.lab.partner_interp_ms = 140;
    s.fake.sample = mgscoop::pose::Sample{};
    s.fake.sample.valid = true;
    s.fake.sample.status = mgscoop::pose::Status::live;
    Check(s.hooks.interp_ms == nullptr && s.Tick(0) && s.fake.sample_delays.back() == 140, "null interp hook: the configured delay");
    s.hooks.interp_ms = HookInterp;
    Check(s.Tick(0) && s.fake.sample_delays.back() == 163, "interp hook: the effective delay");

    for (int mode = 0; mode < 2; ++mode) {
        const bool hide = mode == 1;
        Session h(3, mgscoop::PartnerApply::warp, mgscoop::PartnerSource::net);
        h.lab.partner_lost = hide ? mgscoop::PartnerLost::hide : mgscoop::PartnerLost::freeze;
        h.fake.sample = mgscoop::pose::Sample{};
        h.fake.sample.valid = true;
        h.fake.sample.x = 2.0f;
        h.fake.sample.status = mgscoop::pose::Status::live;
        Check(h.Tick(0) && h.Tick(1) && h.fake.Writes("or") == 1, "realize bits set");
        h.fake.sample.status = mgscoop::pose::Status::lost;
        for (int i = 0; i < 300; ++i) h.Tick(1);  // 9.9 s
        Check(h.fake.Writes("clear") == 0, "nothing cleared before 10 s");
        for (int i = 0; i < 10; ++i) h.Tick(1);
        if (hide) {
            Check(h.fake.Writes("clear") == 1 && h.fake.Count("partner_bits=cleared; reason=lost_hide") == 1 &&
                  h.fake.Count("partner_lost=hide; after_ms=") == 1, "hide: bits cleared after 10 s lost");
            for (int i = 0; i < 5; ++i) h.Tick(1);
            Check(h.fake.Writes("or") == 1, "not re-realized while still lost");
            h.fake.sample.status = mgscoop::pose::Status::live;
            Check(h.Tick(1) && h.fake.Count("partner_lost=show") == 1 && h.fake.Writes("or") == 2, "live again: re-realized");
        } else {
            Check(h.fake.Writes("clear") == 0 && h.fake.Count("partner_lost=") == 0 && (h.reply.op & op::warp) != 0 &&
                  h.reply.x_mm == 2000 && h.reply.status == partner::Status::lost,
                  "freeze: stays realized and holds the pose");
        }
    }
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
// Receiver facing on the default animated profile (xform, net): a one-packet
// direction step turns at the bounded eased rate; position stays exact; every
// lifecycle discontinuity snaps; partner_facing_dps=0 restores the exact yaw.
void TestFacingSmoothing() {
    namespace pf = mgscoop::partner_facing;
    Session s(3, mgscoop::PartnerApply::xform, mgscoop::PartnerSource::net);
    Loaded(s);
    auto& sm = s.fake.sample;
    sm.valid = true; sm.status = mgscoop::pose::Status::live;
    sm.x = 1.0f; sm.y = 2.0f; sm.z = 3.0f; sm.yaw = -1.047f;  // -60 degrees
    const auto yaw = [&] { return partner::QuatToYaw(s.fake.xform_quat); };
    const auto exact_position = [&] {
        return s.fake.xform_pos[0] == 1.0f && s.fake.xform_pos[1] == 2.0f && s.fake.xform_pos[2] == 3.0f;
    };
    Check(s.Tick(1, 16) && s.fake.Calls("xform:") == 1 && std::fabs(yaw() + 1.047f) < 2e-3f &&
          s.driver.mission.facing.last_snap == pf::Snap::first, "facing: first applied tick snaps to the remote yaw");
    // W -> W+A: the sender's yaw steps 45 degrees (left) between two packets.
    sm.yaw = -0.262f;
    float previous = yaw();
    unsigned ticks = 0;
    while (std::fabs(yaw() - sm.yaw) > 2e-3f && ticks < 40) {
        Check(s.Tick(1, 16), "facing: tick accepted");
        ++ticks;
        const float moved = yaw() - previous;
        Check(moved >= -1e-3f && moved <= pf::ParamsForDps(pf::kDefaultRateDps).max_rate * 0.016f + 2e-3f,
              "facing: turns left no faster than the 900 deg/s safety net");
        Check(exact_position(), "facing: position stays the exact interpolated remote position");
        Check(std::abs(s.driver.mission.cmd.yaw_mrad - static_cast<std::int32_t>(std::lround(yaw() * 1000.0f))) <= 2,
              "facing: the commanded/telemetry yaw is the applied yaw");
        previous = yaw();
    }
    Check(ticks >= 4 && ticks <= 10, "facing: a 45-degree packet step turns over about 70-160 ms, not one packet");
    Check(s.reply.yaw_mrad == -262, "facing: the reply (Lua Warp fallback) keeps the exact remote yaw");
    const auto& ring = s.driver.mission.facing_ring;
    const auto& last = pf::At(ring, ring.count - 1);
    Check(ring.count == ticks + 1 && std::fabs(last.target_yaw + 15.01f) < 0.2f && std::fabs(last.applied_yaw + 15.01f) < 0.2f &&
          last.snap == pf::Snap::none,
          "facing: the evidence ring records target and applied yaw every tick");
    Check(std::fabs(pf::At(ring, 1).target_yaw - pf::At(ring, 1).applied_yaw) > 5.0f,
          "facing: the ring shows the applied yaw lagging a stepped target");
    // Teleport: a snap-sized position jump snaps the facing exactly.
    sm.x = 60.0f; sm.yaw = 2.0f;
    Check(s.Tick(1, 16) && std::fabs(yaw() - 2.0f) < 2e-3f && s.driver.mission.facing.last_snap == pf::Snap::teleport,
          "facing: teleport snaps exactly");
    // Loss holds; the relink snaps exactly.
    sm.status = mgscoop::pose::Status::lost; sm.yaw = 0.0f;
    Check(s.Tick(1, 16) && s.driver.mission.facing.last_snap == pf::Snap::stale, "facing: a lost pose holds without easing");
    sm.status = mgscoop::pose::Status::live; sm.yaw = -2.0f;
    Check(s.Tick(1, 16) && std::fabs(yaw() + 2.0f) < 2e-3f && s.driver.mission.facing.last_snap == pf::Snap::relink,
          "facing: relink snaps exactly");
    // Unrealize then realize: the first applied tick snaps.
    s.fake.Realize(false);
    s.Tick(1, 16);
    Loaded(s);
    sm.yaw = 1.0f;
    Check(s.Tick(1, 16) && std::fabs(yaw() - 1.0f) < 2e-3f &&
          (s.driver.mission.facing.last_snap == pf::Snap::first || s.driver.mission.facing.last_snap == pf::Snap::warp),
          "facing: realization snaps exactly");
    // partner_facing_dps=0: the exact remote yaw (previous behaviour).
    s.lab.partner_facing_dps = 0;
    sm.yaw = -1.0f;
    Check(s.Tick(1, 16) && std::fabs(yaw() + 1.0f) < 2e-3f && s.driver.mission.facing.last_snap == pf::Snap::off,
          "facing: partner_facing_dps=0 applies the exact yaw");
    // apply=warp never smooths.
    Session w(3, mgscoop::PartnerApply::warp, mgscoop::PartnerSource::net);
    Loaded(w);
    w.fake.sample = sm;
    w.Tick(1, 16);
    Check(!w.driver.mission.facing_active && !w.driver.mission.facing.valid, "facing: inactive outside xform/hybrid");
}
bool HookReadLocalBody(void* context, const pp::State&, float (&pos)[4], float (&quat)[4]) noexcept {
    Fake& f = F(context);
    std::memcpy(pos, f.local_body_pos, sizeof(pos));
    std::memcpy(quat, f.local_body_quat, sizeof(quat));
    return f.local_body_ok;
}
// Sender facing: vars.playerRotY is the control direction, so the local
// body-transform yaw is published when it is a unit quaternion at the
// published position; every other case falls back to vars.playerRotY.
void TestSenderBodyYaw() {
    namespace pf = mgscoop::partner_facing;
    Session s(3, mgscoop::PartnerApply::xform, mgscoop::PartnerSource::net);
    // TickSource publishes vars yaw 4500 cdeg at (1, 2, 3) m.
    Check(s.Tick(1) && s.fake.last_submit_tick.yaw_cdeg == 4500 && s.fake.Has("partner_facing=source; yaw=vars\r\n"),
          "sender: without a body reader the vars yaw is published");
    s.hooks.read_local_body = HookReadLocalBody;
    s.fake.local_body_ok = true;
    s.fake.local_body_pos[0] = 1.2f; s.fake.local_body_pos[1] = 2.0f; s.fake.local_body_pos[2] = 3.1f;
    partner::YawToQuat(-0.5f, s.fake.local_body_quat);  // body still turning: -28.65 degrees
    Check(s.Tick(1) && s.fake.last_submit_tick.yaw_cdeg == -2865 && s.fake.last_submit_tick.x_mm == 1000 &&
          s.fake.last_submit_tick.cam_cdeg == 0 && s.fake.Has("partner_facing=source; yaw=body\r\n"),
          "sender: the body-transform yaw replaces only the yaw field (centidegrees)");
    const auto& ring = s.driver.mission.facing_ring;
    const auto& last = pf::At(ring, ring.count - 1);
    Check(std::fabs(last.source_yaw - 45.0f) < 0.01f && std::fabs(last.source_body_yaw + 28.65f) < 0.01f,
          "sender: the ring keeps both the vars and the body yaw");
    struct Bad { const char* what; float x; float q[4]; bool ok; };
    for (const Bad b : {Bad{"read refused", 1.0f, {0, 0, 0, 1}, false}, Bad{"not a unit quaternion", 1.0f, {0, 0.5f, 0, 0.5f}, true},
                        Bad{"non-finite", 1.0f, {0, NAN, 0, 1}, true}, Bad{"far from the published position", 3.5f, {0, 0, 0, 1}, true}}) {
        s.fake.local_body_ok = b.ok;
        s.fake.local_body_pos[0] = b.x;
        std::memcpy(s.fake.local_body_quat, b.q, sizeof(b.q));
        Check(s.Tick(1) && s.fake.last_submit_tick.yaw_cdeg == 4500, b.what);
    }
    Check(s.fake.Count("partner_facing=source; yaw=vars") == 2 && s.fake.Count("partner_facing=source; yaw=body") == 1,
          "sender: source changes are logged once per change");
    // The guest instance (local != 0) never reads instance 0 as its own body.
    s.fake.local_body_ok = true; s.fake.local_body_pos[0] = 1.0f;
    partner::YawToQuat(-0.5f, s.fake.local_body_quat);
    s.fake.state.local = 1;
    Check(s.Tick(1) && s.fake.last_submit_tick.yaw_cdeg == 4500, "sender: only local instance 0 is read");
}
// Round 4, apply=input: the reconciliation places the body with a disarmed
// Warp to the remote pose, the fake Warp lands it there, the next tick
// verifies and arms. The remote pose is (1, 2, 3) m, live; the body starts far.
void PlaceAndArm(Session& s) {
    s.fake.sample.valid = true; s.fake.sample.x = 1.0f; s.fake.sample.y = 2.0f; s.fake.sample.z = 3.0f;
    s.fake.sample.status = mgscoop::pose::Status::live;
    s.fake.latest.valid = true; s.fake.latest.age_ms = 5;
    s.fake.body_ok = true;
    s.fake.body_pos[0] = 50.0f; s.fake.body_pos[1] = 2.0f; s.fake.body_pos[2] = 50.0f;
    Loaded(s);
    Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Count("partner_recon=place; reason=first; via=warp; target_mm=1000,2000,3000;") == 1 &&
          (s.fake.arms.empty() || !s.fake.arms.back().enabled) && s.reply.x_mm == 1000 && s.reply.z_mm == 3000,
          "apply=input: placement Warp to the remote pose while disarmed");
    s.fake.body_pos[0] = 1.0f; s.fake.body_pos[2] = 3.0f;  // the Lua Warp landed
    Check(s.Tick(1) && s.fake.Count("partner_recon=placed; ok=1;") == 1 && s.fake.arms.back().enabled &&
          s.fake.Count("partner_input=armed;") == 1 && !(s.reply.op & op::warp), "verified, then armed without a Warp");
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
    Check(c.fake.Has("partner_stage=changed; stage=3; source=fixed; apply=warp; anim=mirror; aset=s; reset=n; look=copy; probe=off; lookgate=1; lookrecover=1; input=off; cam=sandbox; hold=own; yaw=rot; words=hpr; mask=0x2240006; correct=3000; isend=0; dm=off; recon=track; align=0; slot=body; runaway=pin; wrap=off; via=trigger\r\n"),
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

    // look=net with lookgate=1 (default): COPY is withheld until a look arrives
    // or 5 s after all-active (Part L withholding, C).
    Session n(3);
    n.driver.settings.look = mgscoop::PartnerLook::net;
    Check(n.Tick(1) && !(n.reply.op & op::copy_parts) && !(n.reply.op & op::set_parts_explicit) &&
          n.fake.Count("partner_look=copy_withheld; R=1; wait_ms=5000") == 1, "COPY withheld before a look");
    Check(n.Tick(3, 4900) && !(n.reply.op & op::copy_parts) && n.fake.Count("partner_look=waiting") == 0, "still withheld before 5 s");
    Check(n.Tick(3, 200) && (n.reply.op & op::copy_parts) && n.fake.Count("partner_look=waiting; R=1; ms_after_allactive=") == 1,
          "after 5 s: COPY fallback and waiting");
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
    // lookgate=0: the old behaviour (COPY on the first stage-3 tick).
    {
        Session g0(3);
        g0.driver.settings.look = mgscoop::PartnerLook::net;
        g0.driver.settings.look_gate = false;
        Check(g0.Tick(1) && (g0.reply.op & op::copy_parts) && g0.fake.Count("partner_look=copy_withheld") == 0,
              "lookgate=0: COPY on the first tick, not withheld");
    }
    // A new mission starts the look from scratch.
    pd::MissionEnd(n.driver, n.hooks);
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
    Check(s.fake.Has("partner_stage=changed; stage=3; source=fixed; apply=warp; anim=off; aset=qs; reset=n; look=copy; probe=off; lookgate=1; lookrecover=1; input=off; cam=sandbox; hold=own; yaw=rot; words=hpr; mask=0x2240006; correct=3000; isend=0; dm=off; recon=track; align=0; slot=body; runaway=pin; wrap=off; via=trigger\r\n"),
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
          s.fake.Has("partner_stage=changed; stage=3; source=fixed; apply=hybrid; anim=net; aset=r; reset=c; look=snake; probe=off; lookgate=1; lookrecover=1; input=off; cam=sandbox; hold=own; yaw=rot; words=hpr; mask=0x2240006; correct=3000; isend=0; dm=off; recon=track; align=0; slot=body; runaway=pin; wrap=off; via=trigger\r\n"),
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
// Part L: the SET_PARTS gate, COPY withholding and stall recovery.
void TestLookFix() {
    // Realized, state 1, a changed look: deferred (no op 16), then op 16 at state 3.
    {
        Session s(3);
        s.driver.settings.look = mgscoop::PartnerLook::snake;
        Loaded(s);
        s.fake.state.parts[1] = 1;  // state 1: builds from the current Q, never completes
        s.fake.looks.look[1] = partner::Look{6, 27, 116, 0, 0, 0};  // not the desired snake look
        Check(s.Tick(1) && !(s.reply.op & op::set_parts_explicit) && s.fake.Count("partner_look=deferred; R=1; state=1;") == 1,
              "state 1: deferred, no op 16");
        s.fake.state.parts[1] = 3;
        Check(s.Tick(1) && (s.reply.op & op::set_parts_explicit), "state 3: op 16");
    }
    // Unrealized state 0 -> immediate; realized state 0 with q48=0xFF -> immediate; q48=8 -> deferred; state 2 -> deferred.
    {
        Session s(3);
        s.driver.settings.look = mgscoop::PartnerLook::snake;
        s.fake.Realize(false);
        s.fake.state.parts[1] = 0;
        Check(s.Tick(1) && (s.reply.op & op::set_parts_explicit), "unrealized state 0: immediate");
    }
    {
        Session s(3);
        s.driver.settings.look = mgscoop::PartnerLook::snake;
        Loaded(s);
        s.fake.state.parts[1] = 0;
        s.fake.looks.look[1].parts = 0xFF;  // no look yet
        Check(s.Tick(1) && (s.reply.op & op::set_parts_explicit), "realized state 0, q48=0xFF: immediate");
        Session s2(3);
        s2.driver.settings.look = mgscoop::PartnerLook::snake;
        Loaded(s2);
        s2.fake.state.parts[1] = 0;
        s2.fake.looks.look[1].parts = 8;  // a look already present
        Check(s2.Tick(1) && !(s2.reply.op & op::set_parts_explicit) && s2.fake.Count("partner_look=deferred") == 1,
              "realized state 0 with a look: deferred");
        Session s3(3);
        s3.driver.settings.look = mgscoop::PartnerLook::snake;
        Loaded(s3);
        s3.fake.state.parts[1] = 2;
        Check(s3.Tick(1) && !(s3.reply.op & op::set_parts_explicit), "state 2: deferred");
    }
    // Stall recovery: the conditions held for < 1 s -> nothing; at 1 s -> op 16
    // with the descriptor look and source=recover, once; a later state 3 ->
    // op 16 with the desired look.
    {
        Session s(3);
        s.driver.settings.look = mgscoop::PartnerLook::net;
        s.fake.remote = pd::RemoteLook{true, partner::Look{0, 1, 0, 1, 0, 0}, 5};  // desired: Snake
        Loaded(s);
        s.fake.state.parts[1] = 1;
        s.fake.state.q180 = 0x2;
        s.fake.state.pb_ok = true;
        s.fake.state.pb_flag[1] = 3;
        s.fake.state.req_ok = true;
        s.fake.state.req_look = partner::Look{6, 27, 116, 0, 0, 0};  // the block actually holds Quiet
        s.fake.looks.look[1] = partner::Look{0, 1, 0, 1, 0, 0};      // Q holds the late Snake look (0x9CB200 fails)
        Check(s.Tick(1, 999) && !(s.reply.op & op::set_parts_explicit) && s.fake.Count("partner_look=stall;") == 0,
              "stall held < 1 s: nothing");
        Check(s.Tick(1, 1000) && (s.reply.op & op::set_parts_explicit) && partner::SameLook(s.reply.look, partner::quiet_look) &&
              s.fake.Count("partner_look=stall;") == 1 && s.fake.Count("partner_look=stall_recover;") == 1,
              "at 1 s: recovery op 16 with the descriptor look");
        Check(s.fake.Has("partner_look=apply; R=1; source=recover; type=6; parts=27; camo=116; hand=0; face=0; fe=0; rev=0; avatar_as_snake=0\r\n"),
              "recovery apply logged with source=recover");
        const std::size_t recoveries = s.fake.Count("partner_look=stall_recover;");
        Check(s.Tick(1, 10) && s.fake.Count("partner_look=stall_recover;") == recoveries, "recovery once per key");
        // The block now holds Snake: state 3 lets A apply the desired look.
        s.fake.state.parts[1] = 3;
        s.fake.looks.look[1] = partner::Look{0, 1, 0, 1, 0, 0};
        Check(s.Tick(1, 2000) && (s.reply.op & op::set_parts_explicit) && partner::SameLook(s.reply.look, partner::snake_look),
              "state 3: desired look applied");
    }
    // Stall with an unreadable descriptor: nothing.
    {
        Session s(3);
        s.driver.settings.look = mgscoop::PartnerLook::net;
        s.fake.remote = pd::RemoteLook{true, partner::Look{0, 1, 0, 1, 0, 0}, 5};
        Loaded(s);
        s.fake.state.parts[1] = 1;
        s.fake.state.q180 = 0x2;
        s.fake.state.pb_ok = true;
        s.fake.state.pb_flag[1] = 3;
        s.fake.state.req_ok = false;
        s.fake.looks.look[1] = partner::Look{6, 27, 116, 0, 0, 0};
        Check(s.Tick(1, 2000) && s.fake.Count("partner_look=stall;") == 0, "req_ok=false: no stall recovery");
    }
}

// Round 3 input arming, placement, sender and one-shots.
void TestInputArming() {
    // Refusal reasons in order (apply, anim, dm, stage, census, net).
    {
        Session s(3, mgscoop::PartnerApply::warp);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Check(s.Tick(1) && s.fake.Has("partner_input=refused; reason=apply\r\n"), "refused: apply");
        s.driver.settings.apply = mgscoop::PartnerApply::xform;
        s.driver.settings.anim = mgscoop::PartnerAnim::net;
        Check(s.Tick(1) && s.fake.Has("partner_input=refused; reason=anim\r\n"), "refused: anim");
        s.driver.settings.anim = mgscoop::PartnerAnim::off;
        s.driver.settings.dm = mgscoop::PartnerDm::on;
        Check(s.Tick(1) && s.fake.Has("partner_input=refused; reason=dm\r\n"), "refused: dm");
        s.driver.settings.dm = mgscoop::PartnerDm::off;
        s.fake.input_targets_refusal = "unrealized";
        Check(s.Tick(1) && s.fake.Has("partner_input=refused; reason=census_unrealized\r\n"), "refused: census");
        s.fake.input_targets_refusal = nullptr;
        s.driver.settings.input = mgscoop::PartnerInput::net;
        s.fake.net_running = false;
        Check(s.Tick(1) && s.fake.Has("partner_input=refused; reason=net\r\n"), "refused: net");
    }
    // Arm with apply=xform input=neutral (no placement), then disarm on stage<3.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Check(s.Tick(1) && s.fake.installs == 1 && !s.fake.arms.empty() && s.fake.arms.back().enabled &&
              s.fake.Count("partner_input=armed; mode=neutral;") == 1, "armed (neutral) publishes an enabled Arm");
        const std::uint32_t gen = s.fake.arms.back().gen;
        Check(s.Tick(1) && s.fake.arms.back().gen == gen, "gen stable when nothing changes");
        s.driver.settings.input_mask = 0x3;  // a config change bumps the gen
        Check(s.Tick(1) && s.fake.arms.back().gen == gen + 1, "gen bumps on a config change");
        s.Trigger("stage=2");
        Check(s.Tick(1) && !s.fake.arms.back().enabled && s.fake.Count("partner_input=disarmed;") >= 1, "stage<3 disarms");
    }
    // apply=input: placement Warp while disarmed, verification, then armed;
    // drift is corrected with position-only nudges, a teleport places again.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        Check(!s.fake.sample_delays.empty() && s.fake.sample_delays.back() == 1, "aligned target delay (fresh input: 1 ms)");
        // A 1 m drift: no snap, nudges through T.vt+0x58 after the 700 ms settle.
        s.fake.body_pos[0] = 2.0f;
        for (int i = 0; i < 30; ++i) Check(s.Tick(1, 33), "drifted ticks");
        Check(s.fake.Calls("pos") >= 1 && s.fake.Calls("xform:") == 0 && s.fake.Count("partner_recon=snap") == 0, "nudges, no snap");
        Check(s.fake.position[0] < 2.0f && s.fake.position[0] > 1.9f && s.fake.position[1] == 2.0f, "a small step towards the target, y kept");
        // The slot carries the body readback (slot=body).
        Check(s.fake.state.slot[1][0] == s.fake.body_pos[0], "S+0x20[1] = body");
        // A teleport of the remote: placed again with a Warp.
        s.fake.sample.x = 40.0f;
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Count("partner_recon=place; reason=teleport;") == 1 &&
              !s.fake.arms.back().enabled && s.reply.x_mm == 40000, "teleport: placement");
        // The 1 Hz partner_recon line.
        for (int i = 0; i < 40; ++i) s.Tick(1, 33);
        Check(s.fake.Count("partner_recon; mode=") >= 1, "1 Hz partner_recon line");
        Check(s.fake.Count("partner_body; R=1;") >= 1 && s.fake.Containing("vel_mmps=1000;") >= 1, "partner_body with the PS velocity");
    }
    // correct=0: placement only (recon off).
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        s.driver.settings.correct_mm = 0;
        PlaceAndArm(s);
        s.fake.body_pos[0] = 3.0f;
        for (int i = 0; i < 60; ++i) s.Tick(1, 33);
        Check(s.fake.Calls("pos") == 0 && s.fake.Calls("xform:") == 0, "correct=0: no corrections");
    }
    // A trigger that changes the input mode places the body again (I6).
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::stick;
        PlaceAndArm(s);
        s.Trigger("input=net");
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Count("partner_recon=place; reason=rearm;") == 1, "mode change: placed again");
        s.Trigger("place=1");
        s.fake.body_pos[0] = 1.0f; s.fake.body_pos[2] = 3.0f;
        s.Tick(1);
        Check(s.Tick(1) && s.fake.Count("partner_recon=place; reason=manual;") == 1, "place=1");
    }
    // The remote stands still while the body runs on its own: pinned, no sawtooth.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        float z = 3.0f;
        for (int i = 0; i < 300; ++i) {
            z += 3.2f * 0.016f;
            s.fake.body_pos[2] = z;
            s.Tick(1, 16);
            if (s.fake.xform_pos[2] != 0.0f) z = s.fake.xform_pos[2];  // the pin landed
        }
        Check(s.fake.Count("partner_recon=runaway;") == 1 && s.fake.Count("partner_recon=snap") == 0 &&
              s.fake.Count("partner_recon=place;") == 1 && s.fake.Calls("xform:") > 100, "runaway: pinned every tick, no snaps");
    }
    // Sender: isend submits an input packet every tick (stage >= 1).
    {
        Session s(1);
        s.driver.settings.isend = true;
        Check(s.Tick(0) && s.fake.inputs.size() == 1, "isend submits one packet");
        Check(s.Tick(0) && s.fake.inputs.size() == 2, "and again next tick");
    }
    // tap refused outside the mask; p218 refused without the lab key, then expires.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        s.driver.settings.input_mask = 0x2;
        Check(s.Tick(1) && s.fake.arms.back().enabled, "armed for one-shots");
        s.Trigger("tap=0x4");  // 0x4 not in mask 0x2
        Check(s.Tick(1) && s.fake.Has("partner_input=tap_refused; reason=mask\r\n"), "tap outside mask refused");
        s.Trigger("tap=0x2");
        Check(s.Tick(1) && s.fake.Has("partner_input=tap; bits=0x2; frames=4\r\n"), "tap within mask accepted");
        s.Trigger("p218=1");
        Check(s.Tick(1) && s.fake.Has("partner_input=p218_refused; reason=not_allowed\r\n"), "p218 refused without the lab key");
        s.lab.partner_input_p218_allow = true;
        s.Trigger("p218=1");
        Check(s.Tick(1) && s.fake.Count("partner_input=p218; on=1;") == 1, "p218 allowed (cam=sandbox, with the hazard note)");
        Check(s.Tick(1, 20000) && s.fake.Has("partner_input=p218_expired\r\n"), "p218 auto-expires after 20 s");
    }
}

// Round 3 direct motion.
void TestDm() {
    // dm=probe: census and the 2 Hz probe line; no patch.
    {
        Session s(3);
        s.driver.settings.dm = mgscoop::PartnerDm::probe;
        s.fake.dm_ok = true;
        Check(s.Tick(1) && s.fake.dm_patch_calls == 0 && s.fake.Count("partner_dm_census;") >= 1 && s.fake.Count("partner_dm; patched=0") >= 1,
              "dm=probe: census + probe, no patch");
    }
    // dm=on: patch when stage 3, parts loaded, input off.
    {
        Session s(3);
        s.driver.settings.dm = mgscoop::PartnerDm::on;
        Loaded(s);
        Check(s.Tick(1) && s.fake.dm_patch_calls == 1 && s.fake.dm_patched && s.driver.mission.dm_patched, "dm=on: patched");
        // dmplay needs patched + not busy + not pending + the camera connect idle.
        s.fake.dm.q18[0] = 1; s.fake.dm.q18[1] = 1; s.fake.dm.g388 = 0; s.fake.dm.cam_ok = true;
        s.Trigger("dmplay=3");
        Check(s.Tick(1) && (s.reply.op & op::dm_play) && s.reply.gani == 3 && s.fake.Has("partner_dm=play; gani=3\r\n"),
              "dmplay: op 128 with the gani");
        // Unconsumed after 2 s -> clear_pending.
        s.fake.dm.g388 = 0x4;  // our request still pending
        Check(s.Tick(1, 2000) && s.fake.Has("partner_dm=unconsumed; ms=2000\r\n") && s.fake.dm_clear_calls >= 1, "unconsumed -> clear");
        // Revert on stop (the driver passes the reason to the patch hook).
        pd::Stop(s.driver, s.hooks, mgscoop::PartnerTeardown::keep, false);
        Check(s.fake.dm_patch_calls >= 2 && !s.fake.dm_patched && !s.driver.mission.dm_patched &&
              s.fake.dm_patch_reason && std::strcmp(s.fake.dm_patch_reason, "stop") == 0, "revert on stop with reason");
    }
    // dmplay refused when not patched.
    {
        Session s(3);
        s.driver.settings.dm = mgscoop::PartnerDm::probe;
        Loaded(s);
        s.Trigger("dmplay=1");
        Check(s.Tick(1) && !(s.reply.op & op::dm_play) && s.fake.Has("partner_dm=refused; reason=not_patched\r\n"), "dmplay refused unpatched");
    }
    // dm=on refused while the input window is armed.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Loaded(s);
        Check(s.Tick(1) && s.driver.mission.input_armed, "armed");
        s.driver.settings.dm = mgscoop::PartnerDm::on;
        Check(s.Tick(1) && !s.driver.mission.dm_patched && s.fake.Has("partner_dm=refused; reason=input_active\r\n"),
              "dm=on refused while armed");
        Check(s.Tick(1) && s.fake.Count("partner_dm=refused; reason=input_active") == 1, "dm=on refusal is change-only");
    }
}


// Round 3 review fixes: relink, fallback, census, stats deltas, stale, the
// TickInput/TickDm order, holdlog, teardown on MissionStart/refusals, the
// per-mission fault, the process-lifetime tap sequence and the DM camera gate.
void TestReviewFixes() {
    namespace pin = mgscoop::partner_input;
    // apply=input relink (lost -> live): disarm, Warp, re-arm.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        s.fake.sample.status = mgscoop::pose::Status::lost;
        Check(s.Tick(1) && s.driver.mission.input_armed && !(s.reply.op & op::warp) &&
              s.fake.Count("partner_recon=frozen; reason=pose_stale") == 1, "relink: lost keeps the window, frozen, no Warp");
        s.fake.sample.status = mgscoop::pose::Status::live;
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Count("partner_recon=place; reason=relink;") == 1 &&
              s.fake.Count("partner_input=disarmed; reason=place;") == 1 && !s.fake.arms.back().enabled,
              "relink: lost -> live disarms and warps");
        Check(s.Tick(1) && s.driver.mission.input_armed && !(s.reply.op & op::warp), "relink: re-armed without Warp");
    }
    // apply=input falls back to Warp every tick while the window is refused.
    {
        Session s(3, mgscoop::PartnerApply::input);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        s.fake.install_result = "allocate_near";
        Loaded(s);
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Has("partner_input=refused; reason=install_allocate_near\r\n") &&
              s.fake.Count("partner_input_place; reason=fallback;") == 1, "fallback: install refused -> Warp");
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Count("partner_input_place; reason=fallback;") == 1,
              "fallback: Warp every tick, logged once");
        s.driver.settings.input = mgscoop::PartnerInput::off;
        Check(s.Tick(1) && (s.reply.op & op::warp), "fallback: input=off with apply=input behaves like warp");
        Session x(3, mgscoop::PartnerApply::xform);
        x.driver.settings.input = mgscoop::PartnerInput::neutral;
        x.fake.install_result = "allocate_near";
        Loaded(x);
        Check(x.Tick(1) && x.fake.Count("partner_input_place; reason=fallback;") == 0, "fallback: only for apply=input");
    }
    // census=1 logs the input census with input=off, failing verdicts included.
    {
        Session s(3);
        s.fake.input_targets_refusal = "unrealized";
        s.Trigger("census=1");
        Check(s.Tick(1) && s.fake.Has("partner_input_census; verdict=unrealized\r\n") && s.fake.Count("partner_census;") == 2,
              "census=1: input census at input=off (and the component census)");
        Check(s.Tick(1) && s.fake.Count("partner_input_census;") == 1, "census=1: once");
    }
    // dm=probe: the DM census is logged once unloaded, once loaded, and on census=1.
    {
        Session s(3);
        s.driver.settings.dm = mgscoop::PartnerDm::probe;
        for (int i = 0; i < 10; ++i) s.Tick(1);
        Check(s.fake.Count("partner_dm_census;") == 1, "dm census: once while instance 1 is not loaded (no per-tick flood)");
        Loaded(s);
        s.Tick(1);
        s.Tick(1);
        Check(s.fake.Count("partner_dm_census;") == 2, "dm census: once more when loaded");
        s.Trigger("census=1");
        s.Tick(1);
        Check(s.fake.Count("partner_dm_census;") == 3, "dm census: census=1");
    }
    // Window statistics are deltas: per_s/root1_calls per interval, windows since the arm.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Loaded(s);
        s.fake.stats.window.windows = 5000;  // process totals from an earlier mission
        s.fake.stats.window.root1_calls = 5000;
        Check(s.Tick(1) && s.driver.mission.input_armed, "stats: armed");
        for (int i = 0; i < 2; ++i) {
            s.fake.stats.window.windows += 60;
            s.fake.stats.window.root1_calls += 60;
            s.Tick(1, 1000);
        }
        Check(s.fake.Containing("windows=60; per_s=60; root1_calls=60; skips=0;") == 1 &&
              s.fake.Containing("windows=120; per_s=60; root1_calls=60; skips=0;") == 1, "stats: per second, not cumulative");
        s.driver.settings.input = mgscoop::PartnerInput::off;
        Check(s.Tick(1) && s.fake.Has("partner_input=disarmed; reason=trigger; gen=1; windows=120\r\n"), "stats: windows since the arm");
    }
    // input=net: a missing sample is stale (age_ms=none), then live.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        Loaded(s);
        Check(s.Tick(1) && s.driver.mission.input_armed, "stale: armed");
        Check(s.Tick(1) && s.fake.Count("partner_input=stale") == 0 && s.fake.Count("partner_input=live") == 0,
              "stale: nothing before the first window");
        s.fake.stats.window.windows += 5;
        s.fake.stats.window.last_stale = 1;
        s.fake.stats.window.last_age_ms = pin::kNoSample;
        Check(s.Tick(1) && s.fake.Has("partner_input=stale; age_ms=none\r\n"), "stale: no sample is stale");
        s.fake.stats.window.windows += 5;
        s.fake.stats.window.last_stale = 0;
        s.fake.stats.window.last_age_ms = 20;
        Check(s.Tick(1) && s.fake.Has("partner_input=live\r\n"), "stale: then live");
    }
    // input=off dm=on dmplay=1 while armed: disarm, patch and play in one tick.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Loaded(s);
        s.fake.dm.q18[0] = 1; s.fake.dm.q18[1] = 1; s.fake.dm.g388 = 0; s.fake.dm.cam_ok = true;
        Check(s.Tick(1) && s.driver.mission.input_armed, "order: armed");
        s.Trigger("input=off dm=on dmplay=1");
        Check(s.Tick(1) && !s.driver.mission.input_armed && s.driver.mission.dm_patched && (s.reply.op & op::dm_play) &&
              s.reply.gani == 1 && s.fake.Count("partner_dm=refused;") == 0, "order: TickInput before TickDm keeps the dmplay");
    }
    // holdlog=1 needs the lab key; it reaches the Arm only then.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Loaded(s);
        Check(s.Tick(1) && !s.fake.arms.back().holdlog, "holdlog: off by default");
        s.Trigger("holdlog=1");
        Check(s.Tick(1) && s.fake.Has("partner_input=holdlog_refused; reason=not_allowed\r\n") && !s.fake.arms.back().holdlog,
              "holdlog: refused without the lab key");
        s.lab.partner_input_holdlog = true;
        s.Trigger("holdlog=1");
        Check(s.Tick(1) && s.fake.Has("partner_input=holdlog; on=1\r\n") && s.fake.arms.back().holdlog, "holdlog: on with the lab key");
        s.Trigger("holdlog=0");
        Check(s.Tick(1) && !s.fake.arms.back().holdlog, "holdlog: off again");
    }
    // MissionStart tears down a window or DM patch a missed MissionEnd left, and clears the hook fault.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Loaded(s);
        Check(s.Tick(1) && s.driver.mission.input_armed, "teardown: armed");
        const unsigned resets = s.fake.fault_resets;
        pd::MissionStart(s.driver, s.hooks);
        Check(!s.fake.arms.back().enabled && s.fake.Count("partner_input=disarmed; reason=mission_start;") == 1 &&
              s.fake.fault_resets == resets + 1 && !s.driver.mission.input_armed, "teardown: MissionStart disarms");
        Session t(3);
        t.driver.settings.dm = mgscoop::PartnerDm::on;
        Loaded(t);
        Check(t.Tick(1) && t.fake.dm_patched, "teardown: patched");
        pd::MissionStart(t.driver, t.hooks);
        Check(!t.fake.dm_patched && t.fake.dm_patch_reason && std::strcmp(t.fake.dm_patch_reason, "mission_start") == 0,
              "teardown: MissionStart reverts the DM patch");
    }
    // A failing state guard (read_state refusal) disarms and reverts on that tick.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Loaded(s);
        Check(s.Tick(1) && s.driver.mission.input_armed, "refusal: armed");
        s.fake.refusal = pp::Refusal::count;
        Check(!s.Tick(1) && !s.fake.arms.back().enabled && s.fake.Count("partner_input=disarmed; reason=cfg_refused;") == 1,
              "refusal: read_state refusal disarms");
        Session t(3);
        t.driver.settings.dm = mgscoop::PartnerDm::on;
        Loaded(t);
        Check(t.Tick(1) && t.fake.dm_patched, "refusal: patched");
        t.fake.refusal = pp::Refusal::local_index;
        Check(!t.Tick(1) && !t.fake.dm_patched && std::strcmp(t.fake.dm_patch_reason, "cfg_refused") == 0, "refusal: DM reverted");
    }
    // A window fault disables the mission; the next mission starts clean (no stale fault).
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Loaded(s);
        Check(s.Tick(1) && s.driver.mission.input_armed, "fault: armed");
        s.fake.stats.window.fault_phase = 1;
        Check(s.Tick(1) && s.fake.Has("partner_call=fault; fn=input_window; native_calls=disabled\r\n") &&
              !s.driver.mission.input_armed && s.fake.Has("partner_input=refused; reason=faulted\r\n"), "fault: disarmed and refused");
        pd::MissionStart(s.driver, s.hooks);
        Check(pd::Cfg(s.driver, s.hooks, nullptr, s.now), "fault: next mission cfg");
        Loaded(s);
        s.fake.lines.clear();
        Check(s.Tick(1) && s.driver.mission.input_armed && s.fake.Count("partner_call=fault") == 0, "fault: next mission arms (fault cleared)");
    }
    // The tap sequence is process-lifetime: it survives MissionStart and gen bumps.
    {
        Session s(3, mgscoop::PartnerApply::xform);
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        s.driver.settings.input_mask = 0x2;
        Loaded(s);
        Check(s.Tick(1) && s.driver.mission.input_armed, "tap: armed");
        s.Trigger("tap=0x2");
        Check(s.Tick(1) && s.fake.arms.back().tap_seq == 1 && s.fake.arms.back().tap_bits == 0x2, "tap: seq 1");
        const std::uint32_t gen = s.fake.arms.back().gen;
        s.Trigger("stick=90:50");
        Check(s.Tick(1) && s.fake.arms.back().gen == gen + 1 && s.fake.arms.back().tap_seq == 1, "tap: a gen bump keeps the seq");
        pd::MissionStart(s.driver, s.hooks);
        Check(pd::Cfg(s.driver, s.hooks, nullptr, s.now), "tap: next mission cfg");
        Loaded(s);
        Check(s.Tick(1) && s.driver.mission.input_armed && s.fake.arms.back().tap_seq == 1, "tap: seq survives MissionStart");
        s.Trigger("tap=0x2");
        Check(s.Tick(1) && s.fake.arms.back().tap_seq == 2, "tap: next tap is a new value");
    }
    // DM: a play is refused while the camera connect is set; conflicts are logged once.
    {
        Session s(3);
        s.driver.settings.dm = mgscoop::PartnerDm::on;
        Loaded(s);
        s.fake.dm.q18[0] = 1; s.fake.dm.q18[1] = 1; s.fake.dm.g388 = 0;
        s.fake.dm.cam_ok = true; s.fake.dm.cam1f0 = 0x8800000000001234ull; s.fake.dm.cam200 = 1;
        Check(s.Tick(1) && s.driver.mission.dm_patched, "dm cam: patched");
        s.Trigger("dmplay=1");
        Check(s.Tick(1) && !(s.reply.op & op::dm_play) && s.fake.Has("partner_dm=refused; reason=cam_connect\r\n"), "dm cam: refused");
        s.fake.dm.cam_ok = false; s.fake.dm.cam1f0 = 0; s.fake.dm.cam200 = 0;
        s.Trigger("dmplay=1");
        Check(s.Tick(1) && !(s.reply.op & op::dm_play) && s.fake.Count("partner_dm=refused; reason=cam_connect") == 2,
              "dm cam: refused when the camera is unreadable");
        s.fake.dm.q18[1] = c::q18_dm_act;
        s.fake.dm.q18[0] = c::q18_dm_act;
        s.Tick(1);
        s.Tick(1);
        Check(s.fake.Count("partner_dm=conflict;") == 1, "dm cam: host DM during ours logged once");
    }
}


// Round 4 review fixes (driver side).
void TestRound4Review() {
    // A source-only trigger re-places the body (compared before the overwrite);
    // the same values do not.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        s.Trigger("source=net apply=input input=net");
        Check(s.Tick(1) && !(s.reply.op & op::warp) && s.fake.Count("partner_recon=place;") == 1, "same values: no new placement");
        s.Trigger("source=fixed");
        Check(s.Tick(1) && s.fake.Count("partner_recon=place; reason=rearm;") == 1, "source-only change: placed again");
    }
    // An apply round trip (input -> xform -> input) places again.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        s.Trigger("apply=xform");
        s.Tick(1);
        s.Trigger("apply=input");
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.fake.Count("partner_recon=place;") == 2, "apply change: placed again");
    }
    // The fallback Warp keeps the proven 100 ms interpolated pose; only the
    // placement uses the aligned target.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        s.fake.sample_by_delay = true;
        s.fake.sample.valid = true; s.fake.sample.x = 1.0f; s.fake.sample.y = 2.0f; s.fake.sample.z = 3.0f;
        s.fake.sample.status = mgscoop::pose::Status::live;
        s.fake.sample_aligned = s.fake.sample;
        s.fake.sample_aligned.x = 1.5f;  // the extrapolated newest pose
        s.fake.latest.valid = true; s.fake.latest.age_ms = 5;
        s.fake.body_ok = true;
        s.fake.install_result = "allocate_near";  // window refused: per-tick fallback Warp
        Loaded(s);
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.reply.x_mm == 1000 && s.fake.Count("partner_input_place; reason=fallback;") == 1,
              "fallback Warp at the 100 ms interpolated pose");
        s.fake.install_result = nullptr;
        s.fake.body_pos[0] = 50.0f; s.fake.body_pos[2] = 50.0f;
        Check(s.Tick(1) && (s.reply.op & op::warp) && s.reply.x_mm == 1500 && s.fake.Count("partner_recon=place;") == 1,
              "placement Warp at the aligned target");
    }
    // correct=0 turns the reconciliation off (logged), the 1 Hz line says so.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        s.Trigger("correct=0");
        for (int i = 0; i < 40; ++i) s.Tick(1, 33);
        Check(s.fake.Count("partner_recon=off; reason=correct0; requested=track;") == 1, "correct=0 with recon=track: logged");
        Check(s.fake.Containing("partner_recon; mode=track; recon=off;") >= 1, "1 Hz line: effective recon=off");
        s.Trigger("correct=3000");
        for (int i = 0; i < 40; ++i) s.Tick(1, 33);
        Check(s.fake.Containing("partner_recon; mode=track; recon=track;") >= 1, "correct=3000: recon=track again");
    }
    // A lasting stance mismatch: one corrective STANCE tap through the overlay.
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        s.fake.sample.anim_valid = true; s.fake.sample.act = 1; s.fake.sample.anim_stance = 2;  // the guest crouches
        s.fake.anim_ok = true; s.fake.anim.act[1] = 1; s.fake.anim.stance[1] = 1;              // the partner still stands
        const std::uint32_t seq = s.fake.arms.back().tap_seq;
        for (int i = 0; i < 80; ++i) s.Tick(1, 33);
        Check(s.fake.Count("partner_recon=stance_fix; remote=2; body=1; via=tap;") == 1 && s.fake.arms.back().tap_seq == seq + 1 &&
              s.fake.arms.back().tap_bits == 0x2, "stance mismatch: one corrective tap (overlay tap_seq + 1, STANCE)");
        // The guest goes prone: a hold through the press overlay.
        s.fake.sample.anim_stance = 3;
        for (int i = 0; i < 80; ++i) s.Tick(1, 33);
        Check(s.fake.Count("partner_recon=stance_fix; remote=3; body=1; via=hold;") >= 1 && s.fake.arms.back().press_bits == 0x2,
              "prone mismatch: a hold");
        // Without STANCE in the mask: refused, logged.
        Session m(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        m.driver.settings.input = mgscoop::PartnerInput::net;
        m.driver.settings.input_mask = 0x4;
        PlaceAndArm(m);
        m.fake.sample.anim_valid = true; m.fake.sample.act = 1; m.fake.sample.anim_stance = 2;
        m.fake.anim_ok = true; m.fake.anim.act[1] = 1; m.fake.anim.stance[1] = 1;
        for (int i = 0; i < 80; ++i) m.Tick(1, 33);
        Check(m.fake.Count("partner_recon=stance_fix; refused=mask;") >= 1, "stance fix outside the mask: refused");
    }
    // partner_body speed at a 60 Hz Lua tick: numeric on every line (the ring
    // spans ~530 ms; no boundary na).
    {
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        PlaceAndArm(s);
        s.fake.latest.packet.ly = -32767;  // the guest runs; body and remote move together
        float z = 3.0f;
        s.fake.lines.clear();
        for (int i = 0; i < 240; ++i) {
            const std::uint64_t step = (i % 3 == 2) ? 16 : 17;
            z += 3.2f * static_cast<float>(step) / 1000.0f;
            s.fake.body_pos[2] = z;
            s.fake.sample.z = z;
            s.Tick(1, step);
        }
        Check(s.fake.Count("partner_body; R=1;") >= 7, "partner_body at 2 Hz");
        Check(s.fake.Containing("speed_mmps=na;") == 0, "partner_body: no na at a steady 60 Hz tick");
        Check(s.fake.Containing("speed_mmps=3199;") + s.fake.Containing("speed_mmps=3200;") + s.fake.Containing("speed_mmps=3201;") +
              s.fake.Containing("speed_mmps=3198;") + s.fake.Containing("speed_mmps=3202;") >= 6, "partner_body: speed ~3200 mm/s");
    }
}

// Input frames: only a newer (wrap-safe) sequence replaces the stored one,
// unless the peer restarted (kRestartGapMs without a stored frame).
void TestInputSequence() {
    namespace pn = mgscoop::partner_net;
    Check(pn::AcceptInputSequence(false, 0, 0, 7, 100), "seq: first frame accepted");
    Check(pn::AcceptInputSequence(true, 7, 100, 8, 110), "seq: newer accepted");
    Check(!pn::AcceptInputSequence(true, 8, 110, 7, 120), "seq: older (reordered) dropped");
    Check(!pn::AcceptInputSequence(true, 8, 110, 8, 120), "seq: duplicate dropped");
    Check(pn::AcceptInputSequence(true, 0xFFFFFFFFu, 110, 1, 120), "seq: wrap accepted");
    Check(!pn::AcceptInputSequence(true, 500, 1000, 1, 1000 + pn::kRestartGapMs), "seq: restart needs the full gap");
    Check(pn::AcceptInputSequence(true, 500, 1000, 1, 1001 + pn::kRestartGapMs), "seq: restart after the gap accepted");
}

// dm=map (partner_dmmap glued into TickDm): a session with xform movement,
// instance 1 loaded, the DM idle and readable, and the fake game reacting to
// the reply's DM ops. Ticks report all-active and parts copied (no COPY or
// SET_ALLACTIVE executes in Lua), so only the gates a test sets apply.
struct MapSession : Session {
    explicit MapSession(mgscoop::PartnerSource source = mgscoop::PartnerSource::fixed)
        : Session(3, mgscoop::PartnerApply::xform, source) {
        driver.settings.dm = mgscoop::PartnerDm::map;
        Loaded(*this);
        fake.dm.q18[0] = 3; fake.dm.q18[1] = 3; fake.dm.g388 = 0; fake.dm.cam_ok = true;
        fake.dm_auto = true;
    }
    bool T(std::uint64_t step = 33) { return Tick(3, step); }
    void Run(std::uint64_t ms, std::uint64_t step = 33) {
        const std::uint64_t end = now + ms;
        while (now < end) T(step);
    }
    // Ticks until a DM_PLAY went out (at most ms): its gani, or -1.
    int UntilPlay(std::uint64_t ms = 3000, std::uint64_t step = 33) {
        const std::uint64_t end = now + ms;
        while (now < end) {
            T(step);
            if (reply.op & op::dm_play) return reply.gani;
        }
        return -1;
    }
    // Ticks until a DM_STOP went out (at most ms).
    bool UntilStop(std::uint64_t ms = 3000, std::uint64_t step = 33) {
        const std::uint64_t end = now + ms;
        while (now < end) {
            T(step);
            if (reply.op & op::dm_stop) return true;
        }
        return false;
    }
    // Playing: the first clip went out and the game confirmed it.
    int Playing() {
        const int g = UntilPlay();
        T();
        return driver.mission.dmmap.phase == mgscoop::partner_dmmap::Phase::playing ? g : -1;
    }
    // Index of the first line starting with `prefix` (-1 if none).
    int Index(const char* prefix) const {
        for (std::size_t i = 0; i < fake.lines.size(); ++i)
            if (fake.lines[i].compare(0, std::strlen(prefix), prefix) == 0) return static_cast<int>(i);
        return -1;
    }
};

void TestDmMapPatchAndTable() {
    namespace pdm = mgscoop::partner_dmmap;
    // Patched like dm=on; the table line once at the first map tick.
    {
        MapSession s;
        Check(s.T() && s.driver.mission.dm_patched && s.fake.Count("partner_dm=patched;") == 1, "map: patched like on");
        Check(s.fake.Has("partner_dmmap=table; cells=0,10,11,5,1,3,4,4,7,6,6,6; speeds=300:2600:5600; reason=start\r\n"), "map: table at start");
        s.Run(500);
        Check(s.fake.Count("partner_dmmap=table;") == 1 && s.played.size() == 1 && s.played[0] == 0 && s.both_dm_ops == 0,
              "map: one table line; stand idle plays after the hold");
        Check(s.fake.Count("partner_dmmap=switch; step=play; from=none; to=0; stance=0; move=0;") == 1 &&
              s.fake.Count("partner_dmmap=playing; gani=0; after_ms=33") == 1 && s.fake.Count("partner_dmmap; phase=") >= 1,
              "map: switch, playing and summary lines");
    }
    // input_active and native_disabled refuse the patch (also for dm=on after a fault).
    {
        MapSession s;
        s.driver.settings.input = mgscoop::PartnerInput::neutral;
        Check(s.T() && !s.driver.mission.dm_patched && s.fake.Has("partner_dm=refused; reason=input_active\r\n") &&
              s.fake.Has("partner_input=refused; reason=dm\r\n") && !s.driver.mission.input_armed, "map: input refuses the patch, dm the input");
        MapSession f;
        f.driver.mission.native_disabled = true;
        Check(f.T() && f.T() && !f.driver.mission.dm_patched && f.fake.Count("partner_dm=refused; reason=native_disabled") == 1 &&
              f.fake.dm_patch_calls == 0, "map: native_disabled refuses the patch");
        Session o(3);
        o.driver.settings.dm = mgscoop::PartnerDm::on;
        Loaded(o);
        o.driver.mission.native_disabled = true;
        Check(o.Tick(1) && !o.driver.mission.dm_patched && o.fake.Has("partner_dm=refused; reason=native_disabled\r\n"),
              "dm=on: no re-patch after a fault (native calls disabled for the mission)");
    }
    // A failed patch is latched: one refusal, one call, until a dm= trigger.
    {
        MapSession s;
        s.fake.dm_patch_fail = true;
        for (int i = 0; i < 100; ++i) s.T();
        Check(s.fake.dm_patch_calls == 1 && s.fake.Count("partner_dm=refused; reason=comparator_bytes") == 1 &&
              s.fake.Count("partner_dm=refused; reason=patch_failed") == 1 && s.played.empty(), "patch failure latched (no per-tick retry)");
        Check(s.fake.Count("partner_dmmap=deferred; reason=not_patched") == 1, "map: not_patched deferred once");
        s.fake.dm_patch_fail = false;
        s.Trigger("dm=map");
        Check(s.T() && s.driver.mission.dm_patched && s.fake.dm_patch_calls == 2, "a dm= trigger retries the patch");
        Check(s.played.size() == 1 && s.played[0] == 0, "then the mapper plays (the hold already ran)");
    }
    // The table line on dmmap=/dmspeed= triggers, in any dm mode.
    {
        Session s(3);
        s.driver.settings.dm = mgscoop::PartnerDm::on;
        s.Trigger("dmspeed=300:1500:5600");
        Check(s.fake.Has("partner_dmmap=table; cells=0,10,11,5,1,3,4,4,7,6,6,6; speeds=300:1500:5600; reason=trigger\r\n") &&
              s.driver.settings.dm_speed.run_mmps == 1500 && s.driver.dmmap_rearm, "dmspeed= logs the table while dm=on");
        s.Trigger("dmmap=0,12,11,5,1,3,4,4,2,6,6,6");
        Check(s.fake.Has("partner_dmmap=table; cells=0,12,11,5,1,3,4,4,2,6,6,6; speeds=300:1500:5600; reason=trigger\r\n") &&
              s.driver.settings.dm_map.cells[1] == 12, "dmmap= logs the table");
        Check(s.Tick(1) && s.fake.Count("partner_dmmap; phase=") == 0 && s.fake.Count("partner_dmmap=switch") == 0, "dm=on: no mapper");
        pd::MissionStart(s.driver, s.hooks);
        Check(!s.driver.dmmap_rearm && s.driver.mission.dmmap.cand == pdm::kNoGani && s.driver.settings.dm_map.cells[1] == 12,
              "MissionStart: the mapper reset, the settings kept");
        // Configure resets the mapper too (Mission{} alone would leave gani 0 as the candidate).
        pd::Driver fresh{};
        pd::Configure(fresh, s.lab);
        Check(fresh.mission.dmmap.cand == pdm::kNoGani && fresh.mission.dmmap.cur == pdm::kNoGani &&
              fresh.settings.dm_map.cells[1] == 10 && fresh.settings.dm_speed.dash_mmps == 5600, "Configure: mapper reset, lab table");
    }
}

void TestDmMapSources() {
    // Orbit (2356 mm/s at offset 300 cm): walk, then run and dash through dmspeed.
    {
        MapSession s(mgscoop::PartnerSource::orbit);
        Check(s.UntilPlay() == 10, "orbit: the stand walk loop (2356 mm/s < 2600)");
        s.Trigger("dmspeed=300:1500:5600");
        Check(s.UntilPlay() == 11 && s.fake.Count("partner_dmmap=switch; step=stop; from=10; to=11;") == 1, "orbit: run after dmspeed");
        s.Trigger("dmspeed=300:1000:2000");
        Check(s.UntilPlay() == 5, "orbit: dash");
        Check(s.fake.Containing("; via=speed; stance_via=flags;") >= 3 && s.both_dm_ops == 0, "orbit: via speed");
        // The local stance decides the row.
        s.stance = 1;
        Check(s.UntilPlay() == 4, "orbit: squat dash falls back to squat run (4)");
        s.stance = 2;
        Check(s.UntilPlay() == 6, "orbit: crawl fast loop");
    }
    // Fixed: the local move flags.
    {
        MapSession s;
        Check(s.UntilPlay() == 0, "fixed: stand idle");
        s.move = 2;
        Check(s.UntilPlay() == 11, "fixed: move 2 -> stand run loop");
        s.move = 3;
        Check(s.UntilPlay() == 5, "fixed: move 3 -> stand dash");
        s.stance = 1; s.move = 1;
        Check(s.UntilPlay() == 3, "fixed: squat walk");
        s.stance = 2; s.move = 0;
        Check(s.UntilPlay() == 7, "fixed: crawl idle (proven p_idl)");
        // Flags win: move 0 with a moving local anim stays idle.
        s.stance = 0;
        s.fake.anim_ok = true;
        s.fake.anim.act[0] = 2; s.fake.anim.stance[0] = 1; s.fake.anim.speed[0] = 3;
        Check(s.UntilPlay() == 0, "fixed: back to stand idle");
        s.Run(1000);
        Check(s.played.back() == 0 && s.driver.mission.dmmap.cur == 0, "fixed: move 0 is a flag (the moving anim does not decide)");
        // A local anim crawl with stance flags stand: the anim stance.
        s.fake.anim.stance[0] = 3; s.fake.anim.act[0] = 5; s.fake.anim.speed[0] = 0;
        Check(s.UntilPlay() == 7 && s.fake.Containing("stance_via=anim") >= 1, "fixed: anim crawl stance");
        Check(s.both_dm_ops == 0, "fixed: never 128 and 256 together");
    }
    // Net: the remote's speed, Sprinting flag, anim stance; stale freezes, lost idles.
    {
        MapSession s(mgscoop::PartnerSource::net);
        auto& sm = s.fake.sample;
        sm.valid = true; sm.status = mgscoop::pose::Status::live; sm.x = 1.0f; sm.y = 2.0f; sm.z = 3.0f;
        const auto move = [&](double mmps, std::uint64_t ms) {
            const std::uint64_t end = s.now + ms;
            while (s.now < end) {
                sm.x = static_cast<float>(sm.x + mmps * 0.033 / 1000.0);
                s.T();
            }
        };
        // An idle-encoded raw action can persist while the real pose moves.
        // It is a stance hint, not authoritative proof that movement stopped.
        sm.stance = 0;
        sm.anim_valid = true; sm.act = 3; sm.anim_stance = 2; sm.anim_speed = 0;
        Check(s.Playing() == 1, "net: odd raw action initially uses crouch idle");
        move(3000, 1500);
        Check(!s.played.empty() && s.played.back() == 4 && s.driver.mission.dmmap.cand_cls.move == 2 &&
              s.driver.mission.dmmap.cand_cls.via == mgscoop::partner_dmmap::Via::speed &&
              s.driver.mission.dmmap.cand_cls.stance_via == mgscoop::partner_dmmap::Via::anim,
              "net: valid odd raw action cannot override moving pose speed or its crouch stance");
        sm.stance = 0;
        sm.anim_valid = false;
        sm.flags = mgscoop::sync::ActionFlag::Sprinting;
        move(6000, 1500);
        Check(s.played.back() == 5, "net: Sprinting -> stand dash");
        sm.flags = 0;
        sm.anim_valid = true; sm.act = 6; sm.anim_stance = 3; sm.anim_speed = 2;
        move(1000, 1500);
        Check(s.played.back() == 6 && s.fake.Containing("stance_via=anim") >= 1, "net: anim crawl -> crawl fast loop");
        sm.anim_valid = false;
        sm.status = mgscoop::pose::Status::stale;
        const std::size_t plays = s.played.size();
        move(0, 1500);
        Check(s.played.size() == plays, "net: stale freezes the clip");
        sm.status = mgscoop::pose::Status::lost;
        s.Run(1500);
        Check(s.played.back() == 7 && s.fake.Containing("via=lost") >= 1, "net: lost -> the idle cell of the last stance (crawl: 7)");
        Check(s.both_dm_ops == 0, "net: never 128 and 256 together");
    }
    // Positive authoritative flags still start a clip before the speed window
    // is populated, even with an idle-encoded raw action and stationary pose.
    for (const auto flag : {mgscoop::sync::ActionFlag::Walking, mgscoop::sync::ActionFlag::Running}) {
        MapSession s(mgscoop::PartnerSource::net);
        auto& sm = s.fake.sample;
        sm.valid = true; sm.status = mgscoop::pose::Status::live;
        sm.x = 1.0f; sm.y = 2.0f; sm.z = 3.0f; sm.flags = flag;
        sm.anim_valid = true; sm.act = 1; sm.anim_stance = 1; sm.anim_speed = 0;
        const int expected = flag == mgscoop::sync::ActionFlag::Walking ? 10 : 11;
        Check(s.UntilPlay(100) == expected && s.driver.mission.dmmap.cand_cls.via == mgscoop::partner_dmmap::Via::flags,
              "net: authoritative walk/run starts immediately before measured speed is available");
        std::uint32_t speed{};
        Check(!mgscoop::partner_dmmap::SpeedMmps(s.driver.mission.dmmap_speed, speed),
              "net: early authoritative clip does not wait for the 100 ms speed window");
    }
}

// Retail stationary body yaw changes while aiming. Feed the transported pose,
// stance, weapon and raw action through the driver, rather than invoking the
// mapper directly. Native completion is modeled by the recording DM reader.
struct AimPivotSession : MapSession {
    explicit AimPivotSession(std::uint64_t step) : MapSession(mgscoop::PartnerSource::net) {
        auto& sm = fake.sample;
        sm.valid = true; sm.status = mgscoop::pose::Status::live;
        sm.x = 1.0f; sm.y = 2.0f; sm.z = 3.0f;
        sm.stance = 1; sm.equip_id = 873; sm.flags = mgscoop::sync::ActionFlag::Aiming;
        sm.anim_valid = true; sm.act = 14; sm.anim_stance = 2; sm.anim_speed = 0;
        fake.dm.q18[0] = 14;
        Check(Playing() == 1, "aim pivot: stationary crouch starts its lower idle loop");
        Run(200, step);
    }
    std::size_t TurnPlays() const {
        std::size_t n = 0;
        for (const int gani : played) if (gani == 17) ++n;
        return n;
    }
    void Pivot(std::uint64_t step) {
        const float start = fake.sample.yaw;
        constexpr std::uint64_t duration = 200;
        std::uint64_t elapsed = 0;
        while (elapsed < duration) {
            elapsed = elapsed + step < duration ? elapsed + step : duration;
            fake.sample.yaw = std::remainder(start + 1.55f * static_cast<float>(elapsed) / duration, 6.28318530718f);  // left
            Check(T(step), "aim pivot: live pose tick accepted");
        }
        Run(70 + step, step);
    }
    void FirstTurn(std::uint64_t step) {
        namespace pdm = mgscoop::partner_dmmap;
        Pivot(step);
        const std::uint64_t end = now + 500;
        while (now < end && !(driver.mission.dmmap.phase == pdm::Phase::playing && driver.mission.dmmap.cur == 17)) T(step);
        Check(driver.mission.dmmap.phase == pdm::Phase::playing && driver.mission.dmmap.cur == 17 && TurnPlays() == 1,
              "aim pivot: stationary aim-only yaw starts finite lower turn 17");
    }
    void TwoTurns(std::uint64_t step) {
        FirstTurn(step);
        const auto first = driver.mission.dmmap.cur_event;
        const auto token = driver.mission.dmmap_turn.event_token;
        Check(first != 0 && token != 0, "aim pivot: first turn has a discrete request identity");
        Pivot(step);
        const auto& dm = driver.mission.dmmap;
        Check(dm.phase == mgscoop::partner_dmmap::Phase::playing && dm.cur == 17 && dm.cur_event == first &&
              dm.finite_count == 2 && dm.finite_events[0].id == first && dm.finite_events[1].id != first &&
              driver.mission.dmmap_turn.event_token != token && TurnPlays() == 1,
              "aim pivot: second identical 90-degree turn queues while first remains active");
    }
};

void TestDmMapRawTurnFeed() {
    namespace pose = mgscoop::pose;
    namespace sync = mgscoop::sync;
    namespace dm = mgscoop::partner_dmmap;
    for (const unsigned interruption : {0u, 2u, 8u, 16u, 32u, 64u, 128u, 256u}) {
        MapSession s(mgscoop::PartnerSource::net);
        s.hooks.sample_remote_with_turns = HookSampleWithTurns;
        s.fake.dm_motion_count = 21;
        const auto base = s.now;
        unsigned source_frame = 0;
        std::uint64_t first = 0;
        bool had_queue = false;
        bool transition_seen = false, transition_canceled = false;
        for (unsigned owner = 1; owner <= 240; ++owner) {
            const auto elapsed = owner * 1000u / 60;
            while (source_frame * 1000u / 30 <= elapsed) {
                const auto t = source_frame * 1000u / 30;
                const float a = t <= 400 ? 0 : t >= 600 ? 1 : static_cast<float>(t - 400) / 200;
                const float b = t <= 850 ? 0 : t >= 1050 ? 1 : static_cast<float>(t - 850) / 200;
                pose::Snapshot p{};
                p.sender_ms = static_cast<std::uint32_t>(base + t);
                p.x = 1; p.y = 2; p.z = 3; p.yaw = pose::WrapAngle(1.55f * (a + b));  // left turns
                p.stance = 1; p.equip_id = 873; p.flags = sync::ActionFlag::Aiming;
                if (interruption && interruption <= 8 && t >= 1250 && t <= 1450)
                    p.flags |= static_cast<std::uint8_t>(interruption);
                Check(s.fake.raw_ring.Push(p, base + t), "raw driver: source30Hz accepted by actual pose ring");
                ++source_frame;
            }
            s.fake.raw_now = base + elapsed;
            const bool transitioning = elapsed >= 1250 && elapsed <= 1450;
            if (interruption == 16) {
                s.driver.mission.look_verify = transitioning;
                s.driver.mission.look_applied = partner::Look{255,255,255,255,255};
                s.driver.mission.look_applied_ms = s.now;
            }
            if (interruption == 32 && transitioning) s.driver.parts_explicit_pending = true;
            if (interruption == 128) s.fake.xform_result = transitioning ? "candidate_near" : nullptr;
            auto& state = s.driver.mission.dmmap;
            if (state.phase == dm::Phase::playing && dm::OneShot(state.cur) && s.now - state.play_ms >= 951)
                s.fake.dm.q18[1] = 3;
            const int flags = transitioning && interruption == 64 ? 1 : transitioning && interruption == 256 ? 2 : 3;
            Check(s.Tick(flags, base + elapsed - s.now), "raw driver: natural owner60Hz tick accepted");
            if (interruption >= 16 && transitioning) {
                transition_seen = transition_seen || (interruption == 16 && s.driver.mission.look_verify) ||
                    (interruption == 32 && (s.reply.op & op::set_parts_explicit)) ||
                    (interruption == 64 && (s.reply.op & op::copy_parts) && !(flags & partner::luaflag::parts_copied)) ||
                    (interruption == 128 && (s.reply.op & op::warp)) ||
                    (interruption == 256 && (s.reply.op & op::set_all_active));
                Check(s.driver.mission.dmmap.finite_count == 0 && s.driver.mission.dmmap_source_turns.count == 0 &&
                      !s.driver.mission.dmmap_turn.collecting && s.driver.mission.dmmap.cur == 17,
                      "raw driver: actual body transition clears waiting pivot context while active17 continues");
                transition_canceled = true;
            }
            if (s.driver.mission.dmmap.cur_event && !first) first = s.driver.mission.dmmap.cur_event;
            had_queue = had_queue || (s.driver.mission.dmmap.finite_count == 2 && s.driver.mission.dmmap.cur_event == first);
            const auto visual = s.fake.raw_ring.SampleAt(s.fake.raw_now, 100);
            // The facing target is the original interpolated yaw; the command is
            // the eased applied yaw toward it (partner_facing).
            const auto& fm = s.driver.mission;
            Check(fm.dmmap_source_frame.visual.yaw == visual.yaw && fm.facing_active &&
                  static_cast<std::int32_t>(std::lround(fm.facing_target_yaw * 1000.0f)) == mgscoop::partner_player::ToMrad(visual.yaw) &&
                  fm.cmd.yaw_mrad == mgscoop::partner_player::ToMrad(fm.facing_applied_yaw),
                  "raw driver: visual command keeps original100ms interpolated yaw as the facing target");
        }
        std::size_t turns = 0;
        for (const int g : s.played) if (g == 17) ++turns;
        Check(had_queue && s.driver.mission.dmmap.bad == 0 && s.driver.mission.dmmap.unconsumed == 0 && s.both_dm_ops == 0,
              "raw driver: two identities queue while first finite clip is active without gate violations");
        Check(turns == (interruption ? 1u : 2u), "raw driver: fire/reload cancels queued pivot, unchanged aim replays both exactly once");
        if (interruption >= 16) Check(transition_seen && transition_canceled && s.fake.Count("partner_dmmap=switch; step=stop; from=17;") == 0,
            "raw driver: look/copy/warp/allactive invalidation cancels waiting identities without forcing an active lower stop");
        Check(s.driver.mission.dmmap.finite_count == 0, "raw driver: natural completion or interruption drains request queue");
        Check(s.fake.sample_delays.back() == 100, "raw driver: paired read retains configured visual delay");
    }
}

void TestDmMapRawVisualLag() {
    namespace pose = mgscoop::pose;
    namespace sync = mgscoop::sync;
    namespace dm = mgscoop::partner_dmmap;
    for (const bool stance_change : {false, true}) {
        MapSession s(mgscoop::PartnerSource::net);
        s.hooks.sample_remote_with_turns = HookSampleWithTurns;
        s.fake.dm_motion_count = 21;
        const auto base = s.now;
        unsigned source_frame = 0, turn_plays = 0;
        bool collected_during_lag = false;
        for (unsigned owner = 1; owner <= 90; ++owner) {
            const auto elapsed = owner * 1000u / 60;
            while (source_frame * 1000u / 30 <= elapsed) {
                const auto t = source_frame * 1000u / 30;
                pose::Snapshot p{};
                p.sender_ms = static_cast<std::uint32_t>(base + t);
                p.x = 1 + (stance_change ? 0 : 4.0f * static_cast<float>(t < 400 ? t : 400) / 1000);
                p.y = 2; p.z = 3; p.yaw = t <= 400 ? 0 : 1.55f;  // left turn
                p.stance = stance_change && t >= 400 ? 1 : 0;
                p.equip_id = 873;
                p.flags = !stance_change && t < 400 ? sync::ActionFlag::Running : sync::ActionFlag::Aiming;
                p.anim_valid = true; p.act = !stance_change && t < 400 ? 2 : 14;
                p.anim_stance = static_cast<std::uint8_t>(p.stance + 1);
                p.anim_speed = !stance_change && t < 400 ? 2 : 0;
                Check(s.fake.raw_ring.Push(p, base + t), "raw visual lag: source30Hz accepted by actual pose ring");
                ++source_frame;
            }
            s.fake.raw_now = base + elapsed;
            Check(s.Tick(3, base + elapsed - s.now), "raw visual lag: actual recording owner60Hz tick accepted");
            const auto& mission = s.driver.mission;
            const auto& raw = mission.dmmap_source_turns;
            collected_during_lag = collected_during_lag ||
                (mission.dmmap_turn.collecting && raw.have_previous && raw.cls.move == 0 &&
                 (mission.dmmap.cand_cls.move != 0 || mission.dmmap.cand_cls.stance != raw.cls.stance));
            if ((s.reply.op & op::dm_play) && dm::OneShot(static_cast<std::uint8_t>(s.reply.gani))) {
                ++turn_plays;
                std::uint32_t visual_speed = 0;
                const auto& visual = mission.dmmap_source_frame.visual;
                Check(dm::SpeedMmps(mission.dmmap_speed, visual_speed) && visual_speed < 300 &&
                      !(visual.flags & (sync::ActionFlag::Running | sync::ActionFlag::Walking | sync::ActionFlag::Sprinting)) &&
                      visual.stance == (stance_change ? 1u : 0u),
                      "raw visual lag: actual interpolated movement and stance are ready before native play");
                Check(s.reply.gani == (stance_change ? 17u : 13u) && mission.dmmap.cand_cls.move == 0 &&
                      mission.dmmap.cand_cls.stance == (stance_change ? 1u : 0u),
                      "raw visual lag: full pivot plays only after visual idle and the matching stance");
            }
        }
        Check(collected_during_lag && turn_plays == 1 && s.both_dm_ops == 0 && s.driver.mission.dmmap.bad == 0,
              "raw visual lag: immediate post-W-release/stance pivot survives delayed visual class exactly once");
        Check(s.fake.sample_delays.back() == 100, "raw visual lag: original visual interpolation delay stays unchanged");
    }
}

void TestDmArchiveBounds() {
    for (const unsigned count : {0u, 13u, 21u, 31u, 32u, 255u}) {
        Session s(3);
        s.fake.dm_motion_count = count;
        s.driver.settings.dm = mgscoop::PartnerDm::on;
        Loaded(s);
        s.fake.dm.q18[0] = 1; s.fake.dm.q18[1] = 1; s.fake.dm.g388 = 0; s.fake.dm.cam_ok = true;
        Check(s.Tick(1), "archive bound: manual owner ready");
        const unsigned bounded = count <= 31 ? count : 0;
        for (const unsigned gani : {0u, 12u, 13u, 20u, 21u, 30u}) {
            char trigger[32]{};
            std::snprintf(trigger, sizeof(trigger), "dmplay=%u", gani);
            s.Trigger(trigger);
            s.fake.dm.q18[1] = 1; s.fake.dm.g388 = 0;
            Check(s.Tick(1) && ((s.reply.op & op::dm_play) != 0) == (gani < bounded),
                  "archive bound: manual requests require a loaded motion index");
        }
        if (!bounded || bounded < 31)
            Check(s.fake.Containing("reason=motion_not_in_archive") != 0, "archive bound: refusal is explicit");
    }
    {
        Session s(3);
        s.hooks.dm_motion_count = nullptr;
        s.driver.settings.dm = mgscoop::PartnerDm::on;
        Loaded(s);
        s.fake.dm.q18[0] = 1; s.fake.dm.q18[1] = 1; s.fake.dm.g388 = 0; s.fake.dm.cam_ok = true;
        s.Trigger("dmplay=13");
        Check(s.Tick(1) && !(s.reply.op & op::dm_play) && s.fake.Containing("reason=motion_not_in_archive") == 1,
              "archive bound: absent capability uses baseline13");
    }
    for (const unsigned count : {0u, 13u, 20u, 32u}) {
        AimPivotSession s(16);
        s.fake.dm_motion_count = count;
        s.Pivot(16);
        s.Run(1000, 16);
        Check(s.TurnPlays() == 0 && !s.driver.mission.dmmap_turn.valid && s.driver.mission.dmmap.finite_count == 0,
              "archive bound: an unavailable turn never enters the finite queue");
    }
    {
        AimPivotSession s(16);
        s.FirstTurn(16);
        s.Pivot(16);
        s.fake.dm_motion_count = 13;
        s.fake.dm.q18[1] = 3;
        s.Run(2200, 16);
        Check(s.TurnPlays() == 1 && s.driver.mission.dmmap.finite_count == 0,
              "archive bound: a revoked motion bound cannot play a queued turn");
    }
    {
        MapSession s;
        s.fake.dm_motion_count = 13;
        s.driver.settings.dm_map.cells[1] = 30;
        s.move = 1;
        s.Run(1000);
        Check(!s.played.empty() && s.played.front() == 0, "archive bound: baseline map still plays its available idle fallback");
        for (const int gani : s.played) Check(gani < 13, "archive bound: custom map uses a loaded fallback");
    }
}

void TestDmMapAimPivots() {
    namespace pdm = mgscoop::partner_dmmap;
    // Owner samples at 20, 60 and 120 Hz, including two identical requested
    // turns before the first modeled 1.5 s native clip naturally finishes.
    for (const std::uint64_t step : {50u, 16u, 8u}) {
        AimPivotSession s(step);
        s.fake.dm_motion_count = 21; // unchanged normal archive + existing resident turns
        s.TwoTurns(step);
        const auto first = s.driver.mission.dmmap.cur_event;
        const auto second = s.driver.mission.dmmap.finite_events[1].id;
        bool second_playing = false;
        const std::uint64_t end = s.now + 4000;
        while (s.now < end && s.fake.Count("partner_dmmap=finished; gani=17;") < 2) {
            const auto& dm = s.driver.mission.dmmap;
            if (dm.phase == pdm::Phase::playing && pdm::OneShot(dm.cur) && s.now + step - dm.play_ms >= 1500)
                s.fake.dm.q18[1] = 3;  // modeled engine natural finite completion
            s.T(step);
            const auto& after = s.driver.mission.dmmap;
            if (after.phase == pdm::Phase::playing && after.cur == 17 && after.cur_event == second) second_playing = true;
        }
        Check(second_playing && first != second && s.TurnPlays() == 2 &&
              s.fake.Count("partner_dmmap=finished; gani=17;") == 2 && s.driver.mission.dmmap.finite_count == 0,
              "aim pivot: both distinct turn requests naturally finish exactly once");
        s.Run(700, step);
        const auto& dm = s.driver.mission.dmmap;
        Check(s.TurnPlays() == 2 && dm.cur == 1 && dm.phase == pdm::Phase::playing && dm.bad == 0 &&
              dm.strikes[17] == 0 && dm.failures == 0 && dm.timeouts == 0 && s.both_dm_ops == 0 &&
              s.fake.Count("partner_dmmap=switch; step=stop; from=17;") == 0,
              "aim pivot: unchanged aim returns to idle without replay, interruption or bad strike");
    }
    // Firing/reloading (alone or while still aiming) must drop the active
    // collector and every waiting finite request. Walking also interrupts.
    for (const unsigned flags : {2u, 8u, 3u, 9u, 17u}) {
        AimPivotSession s(16);
        s.TwoTurns(16);
        s.fake.sample.flags = static_cast<std::uint8_t>(flags);
        s.T(16);
        Check(!s.driver.mission.dmmap_turn.valid && s.driver.mission.dmmap.finite_count == 0,
              "aim pivot: fire, reload or movement immediately clears collector and queued requests");
        s.Run(200, 16);
        Check(s.TurnPlays() == 1 && s.fake.Count("partner_dmmap=switch; step=stop; from=17;") == 1,
              "aim pivot: busy action cancels the active lower turn without starting the queued turn");
        s.fake.sample.flags = mgscoop::sync::ActionFlag::Aiming;
        s.Run(2200, 16);
        Check(s.TurnPlays() == 1 && s.driver.mission.dmmap.finite_count == 0 && s.driver.mission.dmmap.bad == 0,
              "aim pivot: returning to stationary aim cannot resurrect canceled requests");
    }
    {
        AimPivotSession s(16);
        s.TwoTurns(16);
        s.fake.sample.status = mgscoop::pose::Status::lost;
        s.T(16);
        Check(!s.driver.mission.dmmap_turn.valid && s.driver.mission.dmmap.finite_count == 0,
              "aim pivot: network loss clears active collector and queued requests");
        s.Run(200, 16);
        s.fake.sample.status = mgscoop::pose::Status::live;
        s.Run(2200, 16);
        Check(s.TurnPlays() == 1 && s.driver.mission.dmmap.bad == 0,
              "aim pivot: restored network cannot replay a lost request");
    }
    for (const unsigned flags : {2u, 8u}) {
        AimPivotSession s(16);
        s.fake.sample.yaw -= 0.7f;
        s.T(16);
        Check(s.driver.mission.dmmap_turn.collecting, "aim pivot: partial yaw starts collection");
        s.fake.sample.flags = static_cast<std::uint8_t>(flags);
        s.T(16);
        Check(!s.driver.mission.dmmap_turn.valid && s.driver.mission.dmmap.finite_count == 0,
              "aim pivot: fire or reload discards an unfinished angle");
        s.fake.sample.flags = mgscoop::sync::ActionFlag::Aiming;
        s.T(16);  // a new yaw baseline after interruption
        s.fake.sample.yaw -= 0.9f;
        s.Run(300, 16);
        Check(s.TurnPlays() == 0 && s.driver.mission.dmmap.finite_count == 0,
              "aim pivot: two partial angles across a gun interruption cannot combine into a turn");
    }
    {
        AimPivotSession s(16);
        s.TwoTurns(16);
        s.Trigger("source=fixed");
        s.T(16);
        Check(s.driver.mission.dmmap.finite_count == 0 && s.driver.mission.dmmap_turn.event_token == 0,
              "aim pivot: source change resets queued turn identity");
        s.Run(200, 16);
        s.Trigger("source=net");
        s.Run(2200, 16);
        Check(s.TurnPlays() == 1 && s.driver.mission.dmmap.bad == 0,
              "aim pivot: returning to the old source cannot replay its queued request");
    }
    {
        AimPivotSession s(16);
        s.TwoTurns(16);
        pd::MissionEnd(s.driver, s.hooks);
        Check(s.driver.mission.dmmap.finite_count == 0 && !s.driver.mission.dmmap.owned,
              "aim pivot: mission end discards queued requests and DM ownership");
        pd::MissionStart(s.driver, s.hooks, 30050);
        Check(s.driver.mission.dmmap.finite_count == 0 && !s.driver.mission.dmmap_turn.valid,
              "aim pivot: next mission starts without turn identities or yaw collection");
    }
}

void TestDmMapGates() {
    // Host DM, a camera connect and a foreign pending request each defer once.
    const auto once = [](void (*setup)(Fake&), const char* line) {
        MapSession s;
        setup(s.fake);
        for (int i = 0; i < 100; ++i) s.T();
        return s.played.empty() && s.fake.Count(line) == 1;
    };
    Check(once([](Fake& f) { f.dm.q18[0] = c::q18_dm_act; }, "partner_dmmap=deferred; reason=host_dm; phase=wait; want=0; cur=none"),
          "host DM deferred once");
    Check(once([](Fake& f) { f.dm.cam1f0 = 0x1234; }, "partner_dmmap=deferred; reason=cam_connect"), "camera connect deferred once");
    Check(once([](Fake& f) { f.dm.g388 = 0x8; }, "partner_dmmap=deferred; reason=pending"), "foreign pending deferred once");
    // SET_PARTS defers the play; look_verify keeps it deferred until loaded.
    {
        MapSession s;
        s.Trigger("parts=explicit");
        Check(s.T() && (s.reply.op & op::set_parts_explicit) && s.driver.mission.look_verify, "parts=explicit: SET_PARTS");
        s.Run(1000);
        Check(s.played.empty() && s.fake.Count("partner_dmmap=deferred; reason=look") == 1, "SET_PARTS + verify: deferred look");
        s.driver.mission.look_verify = false;
        Check(s.UntilPlay(200) == 0, "look loaded: plays");
    }
    // An executed COPY (Lua has not copied yet) defers the play.
    {
        MapSession s;
        for (int i = 0; i < 30; ++i) s.Tick(1);
        Check(s.played.empty() && (s.reply.op & op::copy_parts) && s.fake.Count("partner_dmmap=deferred; reason=look") == 1,
              "COPY executing: deferred look");
        Check(s.UntilPlay(200) == 0, "parts copied: plays");
    }
    // An xform refusal falls back to Warp: deferred warp; a Warp that ends the clip is a loss, not a strike.
    {
        MapSession s;
        s.fake.xform_result = "body";
        s.Run(1000);
        Check(s.played.empty() && (s.reply.op & op::warp) && s.fake.Count("partner_dmmap=deferred; reason=warp") == 1,
              "xform refused: deferred warp");
        s.fake.xform_result = nullptr;
        Check(s.Playing() == 0, "xform again: plays");
        s.Run(400);
        s.fake.xform_result = "body";
        Check(s.T() && (s.reply.op & op::warp), "one fallback Warp");
        s.fake.xform_result = nullptr;
        s.fake.dm.q18[1] = 3;  // the Warp ended the clip
        Check(s.T() && s.fake.Count("partner_dmmap=lost; gani=0; played_ms=") == 1 &&
              s.fake.Containing("; cause=warp; failures=0") == 1 && s.fake.Count("partner_dmmap=bad_gani") == 0,
              "Warp loss: cause warp, no strike");
        Check(s.UntilPlay(300) == 0, "replayed");
    }
}

void TestDmMapFailures() {
    // Unconsumed plays: backoff, bad_gani on the second strike, failed at five.
    {
        MapSession s;
        s.fake.dm_consume = false;
        s.fake.dm_cleared = true;
        s.move = 1;
        s.Run(60000, 50);
        Check(s.fake.Count("partner_dm=unconsumed;") == 4 && s.fake.Count("partner_dmmap=unconsumed;") == 4,
              "unconsumed: the native watch and the mapper");
        Check(s.fake.Has("partner_dmmap=unconsumed; gani=10; ms=2500; failures=1; backoff_ms=1000\r\n"), "first: backoff 1 s");
        Check(s.fake.Has("partner_dmmap=bad_gani; gani=10; reason=unconsumed; strikes=2\r\n") &&
              s.fake.Has("partner_dmmap=bad_gani; gani=0; reason=unconsumed; strikes=2\r\n"), "walk then stand idle bad");
        Check(s.fake.Count("partner_dmmap=deferred; reason=bad_gani") == 1 && s.played.size() == 4, "stand row exhausted: bad_gani");
        s.stance = 1;
        s.Run(10000, 50);
        Check(s.fake.Has("partner_dmmap=failed; failures=5; last=unconsumed\r\n") && s.played.size() == 5 && s.played[4] == 3 &&
              s.driver.mission.dmmap.phase == mgscoop::partner_dmmap::Phase::failed, "fifth failure: failed");
        s.Run(10000, 50);
        Check(s.played.size() == 5 && s.fake.Containing("; bad=0x401; failures=5; ") >= 1, "failed: nothing more; summary shows bad/failures");
        s.fake.dm_consume = true;
        s.Trigger("dm=map");
        Check(s.UntilPlay() == 3, "dm=map rearms a failed mapper (squat walk again)");
    }
    // A stop that never lands: stop_timeout, then the stop again after the backoff.
    {
        MapSession s;
        Check(s.Playing() == 0, "playing");
        s.fake.dm_stops = false;
        s.move = 3;
        Check(s.UntilStop(), "stop sent");
        s.Run(1100);
        Check(s.fake.Count("partner_dmmap=stop_timeout; gani=0; ms=10") == 1 &&
              s.fake.Containing("; failures=1; backoff_ms=1000; reason=stop_timeout") == 1, "stop_timeout");
        s.fake.dm_stops = true;
        Check(s.UntilPlay() == 5 && s.both_dm_ops == 0, "stop again, then dash");
    }
}

void TestDmMapRelease() {
    namespace pdm = mgscoop::partner_dmmap;
    // dm=off: the clip is stopped first, then the patch reverted.
    {
        MapSession s;
        Check(s.Playing() == 0, "release: playing");
        s.Trigger("dm=off");
        Check(s.T() && (s.reply.op & op::dm_stop) && s.driver.mission.dm_patched, "release: stop first, patch kept");
        Check(s.T() && !s.driver.mission.dm_patched && s.fake.Has("partner_dmmap=released; result=stopped; to=off; ms=33\r\n"),
              "release: released stopped");
        const int released = s.Index("partner_dmmap=released;"), reverted = s.Index("partner_dm=reverted; reason=trigger");
        Check(released >= 0 && reverted > released, "release: released, then reverted");
        s.Run(500);
        Check(s.played.size() == 1 && s.fake.Count("partner_dmmap; phase=") >= 1 && s.both_dm_ops == 0, "off: nothing more");
    }
    // While the host is in DM: no stop, released timeout at 1.5 s.
    {
        MapSession s;
        Check(s.Playing() == 0, "release host: playing");
        s.fake.dm.q18[0] = c::q18_dm_act;
        s.Trigger("dm=off");
        s.Run(1400);
        Check(s.driver.mission.dm_patched && s.fake.Count("partner_dmmap=released") == 0, "release host: waiting");
        s.Run(200);
        Check(!s.driver.mission.dm_patched && s.fake.Count("partner_dmmap=released; result=timeout; to=off; ms=15") == 1,
              "release host: timeout, then reverted");
    }
    // Without dm_targets nothing can be released: abandoned and reverted at once.
    {
        MapSession s;
        Check(s.Playing() == 0, "abandon: playing");
        s.hooks.dm_targets = nullptr;
        s.Trigger("dm=off");
        Check(s.T() && !s.driver.mission.dm_patched && s.fake.Has("partner_dmmap=abandoned; reason=trigger; gani=0\r\n"),
              "no dm_targets: abandoned, reverted");
    }
    // map -> on: released (to=on), the dmplay in the same trigger refused; on -> map adopts a manual clip.
    {
        MapSession s;
        Check(s.Playing() == 0, "map->on: playing");
        s.Trigger("dm=on dmplay=3");
        Check(s.T() && (s.reply.op & op::dm_stop) && !(s.reply.op & op::dm_play) && s.fake.Has("partner_dm=refused; reason=map_release\r\n"),
              "map->on: stop, the manual play refused during the release");
        Check(s.T() && s.fake.Has("partner_dmmap=released; result=stopped; to=on; ms=33\r\n") && s.driver.mission.dm_patched,
              "map->on: released, patch kept");
        s.Trigger("dmplay=3");
        Check(s.T() && (s.reply.op & op::dm_play) && s.reply.gani == 3 && s.fake.Has("partner_dm=play; gani=3\r\n"), "dm=on: manual play");
        s.T();
        s.Trigger("dm=map");
        Check(s.T() && s.fake.Has("partner_dmmap=adopted; reason=active_clip\r\n"), "on->map: the manual clip is adopted");
        Check(s.UntilStop() && s.UntilPlay() == 0 && s.both_dm_ops == 0, "on->map: then stopped and replaced by the target");
        s.Trigger("dmplay=1");
        s.Trigger("dmstop=1");
        Check(s.T() && s.fake.Count("partner_dm=refused; reason=map\r\n") == 2 && !(s.reply.op & (op::dm_play | op::dm_stop)),
              "map: manual dmplay/dmstop refused, one line each");
    }
    // [I]-style dm=off input=neutral during a release: input refused (dm) until released.
    {
        MapSession s;
        Check(s.Playing() == 0, "input race: playing");
        s.fake.dm.q18[0] = c::q18_dm_act;
        s.Trigger("dm=off input=neutral");
        s.Run(1000);
        Check(!s.driver.mission.input_armed && s.fake.Has("partner_input=refused; reason=dm\r\n") && s.driver.mission.dm_patched,
              "input race: refused while the release runs");
        s.fake.dm.q18[0] = 3;
        s.fake.dm.q18[1] = 3;
        s.Run(700);
        Check(s.driver.mission.input_armed && !s.driver.mission.dm_patched &&
              s.driver.mission.dmmap.phase == pdm::Phase::idle, "input race: armed after the release and the revert");
    }
}

void TestDmMapInteractions() {
    namespace pdm = mgscoop::partner_dmmap;
    // anim=mirror while a mapped clip plays: anim writes wait, the clip is stopped, then aset resumes.
    {
        MapSession s;
        s.fake.anim_ok = true;
        s.fake.anim.act[0] = 1; s.fake.anim.stance[0] = 1; s.fake.anim.speed[0] = 0;
        Check(s.Playing() == 0, "anim: playing");
        s.Trigger("anim=mirror aset=q");
        Check(s.T() && (s.reply.op & op::dm_stop) && s.fake.Calls("anim:") == 0 &&
              s.fake.Has("partner_anim=deferred; reason=dm_map\r\n"), "anim: deferred, the clip stopped");
        s.T();
        Check(s.fake.Calls("anim:") == 0 && !s.driver.mission.dmmap.owned, "anim: stop confirmed");
        s.Run(500);
        Check(s.fake.Calls("anim:1,1,0") >= 10 && s.fake.Count("partner_anim=deferred; reason=dm_map") == 1 &&
              s.fake.Count("partner_dmmap=deferred; reason=anim") == 1 && s.played.size() == 1, "anim: aset writes resume, map deferred");
    }
    // Teardown with a clip: abandoned, then reverted; adopted after a re-patch.
    {
        MapSession s;
        Check(s.Playing() == 0, "teardown: playing");
        pd::MissionEnd(s.driver, s.hooks);
        Check(s.fake.Has("partner_dmmap=abandoned; reason=mission_end; gani=0\r\n") &&
              s.Index("partner_dmmap=abandoned") < s.Index("partner_dm=reverted; reason=mission_end"), "MissionEnd: abandoned, reverted");
        MapSession m;
        Check(m.Playing() == 0, "teardown: playing");
        pd::MissionStart(m.driver, m.hooks);
        Check(m.fake.Has("partner_dmmap=abandoned; reason=mission_start; gani=0\r\n") && !m.driver.mission.dmmap.owned &&
              m.driver.mission.dmmap.phase == pdm::Phase::idle, "MissionStart: abandoned, mapper reset");
        MapSession t;
        Check(t.Playing() == 0, "teardown: playing");
        pd::Stop(t.driver, t.hooks, mgscoop::PartnerTeardown::keep, false);
        Check(t.fake.Has("partner_dmmap=abandoned; reason=stop; gani=0\r\n"), "Stop: abandoned");
        MapSession r;
        Check(r.Playing() == 0, "teardown: playing");
        partner::Reply reply{};
        Check(!pd::Tick(r.driver, r.hooks, r.lab, "return 'MGS_COOP_PARTNER_V1' -- x", 33, pd::Clock{r.now, r.now}, false, reply) &&
              r.fake.Has("partner_dmmap=abandoned; reason=tick_rejected; gani=0\r\n") && !r.driver.mission.dm_patched,
              "tick rejected: abandoned, reverted");
        Check(r.T() && r.driver.mission.dm_patched && r.fake.Has("partner_dmmap=adopted; reason=active_clip\r\n"),
              "re-patched: the still looping clip is adopted");
        MapSession q;
        Check(q.Playing() == 0, "teardown: playing");
        q.fake.refusal = pp::Refusal::count;
        Check(!q.T() && q.fake.Has("partner_dmmap=abandoned; reason=cfg_refused; gani=0\r\n"), "cfg refused: abandoned");
    }
    // Unrealize forgets the clip (no lost line); a fresh play once loaded again.
    {
        MapSession s;
        Check(s.Playing() == 0, "unrealize: playing");
        s.fake.Realize(false);
        s.fake.dm.q18[1] = 3;
        s.Run(300);
        Check(!s.driver.mission.dmmap.owned && s.fake.Count("partner_dmmap=lost") == 0 &&
              s.fake.Count("partner_dmmap=deferred; reason=parts") == 1, "unrealize: forgotten, parts deferred");
        Loaded(s);
        Check(s.UntilPlay(300) == 0 && s.played.size() == 2, "realized again: a fresh play");
    }
    // Blocked by the movement driver or the stage: stop, then deferred.
    {
        MapSession s;
        Check(s.Playing() == 0, "blocked: playing");
        s.Trigger("apply=warp");
        Check(s.T() && (s.reply.op & op::dm_stop) && s.fake.Has("partner_dmmap=switch; step=stop; from=0; to=none; stance=0; move=0; "
                                                               "speed_mmps=-1; via=flags; stance_via=flags; wait_ms=0\r\n"),
              "apply=warp: the clip stopped");
        s.Run(500);
        Check(s.fake.Count("partner_dmmap=deferred; reason=apply") == 1 && s.played.size() == 1, "apply=warp: deferred apply");
        s.Trigger("apply=xform");
        Check(s.UntilPlay(300) == 0, "apply=xform: plays again");
        s.T();
        s.Run(400);
        s.Trigger("stage=2");
        Check(s.T() && (s.reply.op & op::dm_stop), "stage=2: the clip stopped");
        s.Run(300);
        Check(s.fake.Count("partner_dmmap=deferred; reason=stage") == 1, "stage=2: deferred stage");
    }
}

void TestDmMapCaps() {
    // Each family has its own cap: 64 deferred lines do not silence abandoned.
    {
        MapSession s;
        Check(s.Playing() == 0, "caps: playing");
        s.move = 3;
        for (int i = 0; i < 100; ++i) {
            if (i % 2) { s.fake.dm.q18[0] = c::q18_dm_act; s.fake.dm.g388 = 0; }
            else { s.fake.dm.q18[0] = 3; s.fake.dm.g388 = 0x8; }
            s.T();
        }
        Check(s.fake.Count("partner_dmmap=deferred;") == 64 && s.fake.Count("partner_dmmap=capped; limit=64; kind=deferred") == 1 &&
              s.driver.mission.dmmap.deferrals > 64, "deferred: capped at 64, the counter continues");
        pd::MissionEnd(s.driver, s.hooks);
        Check(s.fake.Has("partner_dmmap=abandoned; reason=mission_end; gani=0\r\n"), "abandoned still logged");
    }
    // switch/playing lines share 600.
    {
        MapSession s;
        for (int i = 0; i < 260; ++i) {
            s.move = i % 2 ? 3 : 0;
            s.Run(600, 50);
        }
        Check(s.fake.Count("partner_dmmap=switch;") + s.fake.Count("partner_dmmap=playing;") == 600 &&
              s.fake.Count("partner_dmmap=capped; limit=600; kind=switch") == 1 && s.driver.mission.dmmap.plays > 200 && s.both_dm_ops == 0,
              "switch/playing: capped at 600");
    }
    // The summary: every 5 s, 720 lines, then one cap line; counters match.
    {
        MapSession s;
        for (int i = 0; i < 3700; ++i) s.T(1000);
        Check(s.fake.Count("partner_dmmap; phase=") == 720 && s.fake.Count("partner_dmmap=capped; limit=720; kind=summary") == 1,
              "summary: capped at 720");
        char plays[32]{};
        std::snprintf(plays, sizeof(plays), "; plays=%u;", s.driver.mission.dmmap.plays);
        Check(s.fake.Containing(plays) >= 1 && s.fake.Containing("partner_dmmap; phase=playing; cur=0; want=0;") >= 700, "summary counters");
    }
}

} // namespace

// --- Native puppet (docs/native-puppet.md 7) ------------------------------------
// A healthy deferral: installed, live, one batch thread == hook thread, enough batches.
void NativeHealthy(Fake& f) {
    f.native_install = pd::NativeInstall{true, true, true, true, true, true, true, true, true};
    f.defer.h1 = f.defer.h2 = true;
    f.defer.batches = 1000;
    f.defer.batch_threads_seen = 1;
    f.defer.batch_thread = f.defer.hook_thread = 77;
    f.native_targets.go = 0x1000 - 0x20;
    f.native_targets.pool0 = 0;
    f.native_targets.pool1 = 1;
    f.native_targets.ps1 = 0x7000;
    f.native_targets.ps1_idx = 0;
    f.input_targets.root0 = 0x6000;
    f.input_targets.root1 = 0x6100;
}
// Every tick of an active deferral: block 1 deferred and drained in both passes.
void NativeFrame(Fake& f) {
    f.defer.batches += 8;
    f.defer.w50_b1_s0 += 2;
    f.defer.w50_b0_s0 += 2;
    f.defer.deferred += 2;
    f.defer.drains[0] += 1;
    f.defer.drains[1] += 1;
    f.defer.windowed += 2;
}
struct NativeSession : MapSession {
    explicit NativeSession(mgscoop::PartnerDefer defer = mgscoop::PartnerDefer::on) : MapSession(mgscoop::PartnerSource::net) {
        AddNativeHooks(hooks);
        lab.partner_drive = mgscoop::PartnerDrive::native;
        lab.partner_input_defer = defer;
        lab.partner_input_hook = true;
        lab.partner_input_wrap = mgscoop::PartnerWrap::window;
        lab.partner_input_send = true;
        driver.settings.drive = mgscoop::PartnerDrive::native;
        driver.settings.defer = defer;
        driver.settings.isend = true;
        driver.settings.steer_deg = 15;
        mgscoop::partner_native::Reset(driver.mission.native, true);
        driver.mission.mag_scale = 1.0;
        NativeHealthy(fake);
        fake.sample.valid = true; fake.sample.x = 1.0f; fake.sample.y = 2.0f; fake.sample.z = 3.0f;
        fake.sample.status = mgscoop::pose::Status::live;
        fake.latest.valid = true; fake.latest.age_ms = 5; fake.latest.packet.version = 3;
        fake.latest_ok = true;
        fake.body_ok = true;
        fake.body_pos[0] = 1.0f; fake.body_pos[1] = 2.0f; fake.body_pos[2] = 3.0f;
    }
    bool N(std::uint64_t step = 33) {
        NativeFrame(fake);
        return T(step);
    }
    // Runs until the arbiter reaches `mode` (at most ms); true when it did.
    bool Until(mgscoop::partner_native::Mode mode, std::uint64_t ms) {
        const std::uint64_t end = now + ms;
        while (now < end) {
            N();
            if (driver.mission.native_out.mode == mode) return true;
        }
        return false;
    }
    int OrderIndex(const char* what, int from = 0) const {
        for (std::size_t i = static_cast<std::size_t>(from); i < fake.native_order.size(); ++i)
            if (fake.native_order[i] == what) return static_cast<int>(i);
        return -1;
    }
};

// Online play (docs/native-puppet.md "Online"): the online policy, the stall
// hold in the Arm, the sender's edge repeats, the capability exchange both
// ways, remote_caps, a quiet remote, the weapon-absent strip and native.net.
std::vector<std::uint8_t> CapsBytes(std::uint16_t revision, std::uint32_t epoch, std::uint8_t features, mgscoop::partner_native_net::Drive drive,
                                    const char* reason) {
    mgscoop::partner_caps_wire::Caps c{};
    c.revision = revision; c.epoch = epoch; c.features = features; c.drive = static_cast<std::uint8_t>(drive);
    c.reason = mgscoop::partner_native_net::ReasonCode(reason); c.mode = 1; c.policy = 1;
    std::uint8_t b[mgscoop::partner_caps_wire::kSize]{};
    Check(mgscoop::partner_caps_wire::Encode(c, b) == sizeof(b), "caps fixture encodes");
    return std::vector<std::uint8_t>(b, b + sizeof(b));
}
void SetVisualFire(NativeSession& s);
void TestNativeOnline() {
    namespace pn = mgscoop::partner_native;
    namespace pnn = mgscoop::partner_native_net;
    namespace pcw = mgscoop::partner_caps_wire;
    namespace pi = mgscoop::partner_input;
    {
        NativeSession s;
        s.hooks.send_reliable_append = HookSendReliable;
        s.driver.settings.nnet = mgscoop::PartnerNativeNet::online;
        s.driver.settings.nstall = mgscoop::PartnerNativeStall::hold;
        s.driver.settings.stall_ms = 400;
        s.driver.settings.edge_repeats = 5;
        s.driver.settings.nprofile = mgscoop::PartnerNativeProfile::loco;
        Check(s.Until(pn::Mode::active, 9000), "online: active");
        Check(s.fake.arms.back().native && s.fake.arms.back().config.stall_ms == 400, "the stall hold reaches the native Arm");
        Check(pi::SenderRepeats(s.driver.mission.sender) == 5, "the sender repeats each edge set 5 times");
        // The capability message went out (append, channel 2) and says native.
        for (int i = 0; i < 40; ++i) s.N();
        bool native_caps = false;
        for (const auto& m : s.fake.reliable_sent) {
            pcw::Caps c{};
            if (pcw::Decode(m.data(), m.size(), 7, c) == pcw::Result::ok && c.drive == static_cast<std::uint8_t>(pnn::Drive::native) &&
                (c.features & pcw::Feature::InputSender) && (c.features & pcw::Feature::NativeDrive) && c.profile == 1 && c.policy == 1)
                native_caps = true;
        }
        Check(native_caps && s.driver.caps_tx.sent >= 2, "caps sent: native, input sender, loco profile, online");
        // The remote's capabilities: it shows our body as clips because of its thread census.
        s.fake.channel2.push_back(CapsBytes(4, 7, pcw::Feature::InputSender | pcw::Feature::NativeDrive, pnn::Drive::clips, "thread"));
        s.N();
        const auto& net = s.driver.mission.tele.net;
        Check(net.caps_rx && net.remote_drive == pnn::Drive::clips && std::strcmp(net.remote_reason, "thread") == 0 && net.remote_v4 &&
              s.fake.Containing("partner_caps=rx; drive=clips; reason=thread;") == 1, "remote caps stored, logged once");
        Check(net.local_drive == pnn::Drive::native && std::strcmp(net.local_reason, "ok") == 0 && std::strcmp(net.policy, "online") == 0 &&
              net.edge_repeats == 5 && net.quality_good, "native.net: local view, policy, repeats, quality");
        // A quiet remote (link up, input and pose stale): native holds, then a network leave (no latch).
        s.fake.latest.age_ms = 3000;
        s.fake.sample.status = mgscoop::pose::Status::stale;
        for (int i = 0; i < 30; ++i) s.N();
        Check(s.driver.mission.native_out.mode == pn::Mode::active && pn::ModeName(s.driver.mission.native_out.mode) &&
                  std::strcmp(s.driver.mission.native_out.reason, "remote_quiet") == 0 && s.fake.Containing("partner_native_net=quiet") == 1,
              "a quiet remote keeps the native drive (neutral in place)");
        Check(s.Until(pn::Mode::fallback, 12000) && s.driver.mission.native.fallbacks == 0 && s.driver.mission.native.net_leaves == 1 &&
              std::strcmp(s.driver.mission.native.left_reason, "net_quiet") == 0, "10 s quiet: net_quiet, never counted");
    }
    {
        // The remote sends no input frames (capabilities without the sender): remote_caps.
        NativeSession s;
        s.driver.settings.nnet = mgscoop::PartnerNativeNet::online;
        s.fake.channel2.push_back(CapsBytes(1, 7, pcw::Feature::NativeDrive, pnn::Drive::off, "legacy"));
        for (int i = 0; i < 100; ++i) s.N();
        Check(s.driver.mission.native_out.mode == pn::Mode::fallback && std::strcmp(s.driver.mission.native_out.reason, "remote_caps") == 0 &&
              s.driver.mission.tele.net.remote_caps_refused, "remote_caps keeps the clips");
        // Another link session's caps do not count.
        s.fake.link_session = 8;
        for (int i = 0; i < 100; ++i) s.N();
        Check(s.driver.mission.native_out.mode != pn::Mode::fallback || std::strcmp(s.driver.mission.native_out.reason, "remote_caps") != 0,
              "caps of an older session are ignored");
    }
    {
        // The gun profile: body 1 without a gun class strips HOLD, RELOAD and FIRE.
        NativeSession s;
        AddWeaponHooks(s.hooks);
        s.driver.settings.ngun = true;
        s.driver.settings.input_mask = 0x224005E;
        s.fake.gun_ok = false;
        Check(s.Until(pn::Mode::active, 9000), "active (gun profile, no gun on body 1)");
        Check((s.fake.arms.back().strip & 0x58u) == 0x58u && s.driver.mission.weapon_absent &&
              s.fake.Containing("partner_weapon=absent; body_gun=absent;") == 1, "weapon absent: HOLD/RELOAD/FIRE stripped, logged once");
        Check(s.driver.mission.tele.weapon_absent && s.driver.mission.tele.leak_strip == 0, "telemetry: weapon.absent, no leak strip");
    }
    for (const bool play : {true, false}) {
        // Play: a shot or ammo leak into the viewer's vars strips FIRE and RELOAD
        // for the rest of the process (every later mission too); the lab only counts.
        NativeSession s;
        SetVisualFire(s);
        s.driver.settings.play_mode = play;
        Check(s.Until(pn::Mode::active, 9000), "active (gun, leak guard)");
        for (int i = 0; i < 5; ++i) s.N();
        Check((s.fake.arms.back().strip & 0x50u) == 0, "no leak: FIRE and RELOAD pass");
        s.fake.stats.native.shot_leaks = 1;
        for (int i = 0; i < 5; ++i) s.N();
        if (play) {
            Check((s.fake.arms.back().strip & 0x50u) == 0x50u && (s.fake.arms.back().strip & 0x8u) == 0 && s.driver.leak_strip == 0x50u &&
                      s.fake.Containing("partner_weapon=fire_reload_off; reason=shot_leak; shot_leaks=1; ammo_leaks=0;") == 1 &&
                      s.driver.mission.tele.leak_strip == 0x50u,
                  "play: a shot leak strips FIRE and RELOAD (HOLD stays), logged once");
            s.fake.stats.native.shot_leaks = 0;
            pd::MissionEnd(s.driver, s.hooks);
            pd::MissionStart(s.driver, s.hooks);
            Check(pd::Cfg(s.driver, s.hooks, nullptr, s.now), "leak guard: next mission cfg");
            Loaded(s);
            Check(s.Until(pn::Mode::active, 9000) && (s.fake.arms.back().strip & 0x50u) == 0x50u &&
                      s.fake.Containing("fire_reload_off") == 1, "the play leak strip outlives the mission");
        } else {
            Check((s.fake.arms.back().strip & 0x50u) == 0 && s.driver.leak_strip == 0 && !s.fake.Containing("fire_reload_off"),
                  "lab: a leak is telemetry only");
        }
    }
    {
        // An ammo leak without the magazine mirror strips too (play).
        NativeSession s;
        SetVisualFire(s);
        s.driver.settings.play_mode = true;
        Check(s.Until(pn::Mode::active, 9000), "active (gun, ammo leak)");
        s.fake.stats.native.ammo_leaks = 2;
        for (int i = 0; i < 5; ++i) s.N();
        Check((s.fake.arms.back().strip & 0x50u) == 0x50u &&
                  s.fake.Containing("partner_weapon=fire_reload_off; reason=ammo_leak; shot_leaks=0; ammo_leaks=2;") == 1,
              "play: an ammo leak strips FIRE and RELOAD");
    }
    {
        // A copy that shows clips (drive=legacy) still sends its capabilities: off, legacy.
        MapSession s(mgscoop::PartnerSource::net);
        AddNativeHooks(s.hooks);
        s.hooks.send_reliable_append = HookSendReliable;
        s.driver.settings.isend = true;
        s.driver.settings.play_mode = true;
        s.Run(300);
        pcw::Caps c{};
        Check(!s.fake.reliable_sent.empty() && pcw::Decode(s.fake.reliable_sent.back().data(), s.fake.reliable_sent.back().size(), 7, c) ==
                  pcw::Result::ok && c.drive == static_cast<std::uint8_t>(pnn::Drive::off) &&
                  std::strcmp(pnn::ReasonName(c.reason), "legacy") == 0 && (c.features & pcw::Feature::InputSender),
              "clips copy: caps off/legacy with the input sender");
        Check(s.driver.mission.tele.net.local_drive == pnn::Drive::off, "native.net local_drive off");
    }
}

void TestNativeSleepPresentationAdmission() {
    namespace pn = mgscoop::partner_native;
    NativeSession s;
    Check(s.driver.settings.dm == mgscoop::PartnerDm::map && !pd::NativeSleepPresentationReady(s.driver),
          "sleep admission refuses real inactive fallback even before a clip starts");
    Check(s.Playing() >= 0 && s.driver.mission.dm_patched && !pd::NativeSleepPresentationReady(s.driver),
          "sleep admission refuses active fallback clip ownership");
    Check(s.Until(pn::Mode::entering, 3000) && s.Until(pn::Mode::active, 6000),
          "sleep admission fixture performs real stop/revert/place/arm sequence");
    Check(s.driver.mission.native_out.active && s.driver.settings.dm == mgscoop::PartnerDm::map &&
          !s.driver.mission.dm_patched && !s.driver.mission.dmmap.owned && pd::NativeSleepPresentationReady(s.driver),
          "actual effective native dmOFF permits sleep while base dmMAP fallback is preserved");
    auto& m = s.driver.mission;
    m.dm_patched = true; Check(!pd::NativeSleepPresentationReady(s.driver), "sleep refuses actual DM patch ownership"); m.dm_patched = false;
    m.dm_revert_failed = true; Check(!pd::NativeSleepPresentationReady(s.driver), "sleep refuses failed stock DM cleanup"); m.dm_revert_failed = false;
    m.dm_play_pending = true; Check(!pd::NativeSleepPresentationReady(s.driver), "sleep refuses pending submitted clip"); m.dm_play_pending = false;
    s.driver.dmplay_pending = true; Check(!pd::NativeSleepPresentationReady(s.driver), "sleep refuses queued manual clip play"); s.driver.dmplay_pending = false;
    s.driver.dmstop_pending = true; Check(!pd::NativeSleepPresentationReady(s.driver), "sleep refuses queued manual clip stop"); s.driver.dmstop_pending = false;
    m.dmmap.owned = true; Check(!pd::NativeSleepPresentationReady(s.driver), "sleep refuses mapped clip ownership"); m.dmmap.owned = false;
    const auto patches = s.fake.dm_patch_calls; const auto clears = s.fake.dm_clear_calls;
    const auto plays = s.played.size();
    for(auto phase : {mgscoop::partner_dmmap::Phase::wait, mgscoop::partner_dmmap::Phase::play_sent,
                     mgscoop::partner_dmmap::Phase::playing, mgscoop::partner_dmmap::Phase::stop_sent,
                     mgscoop::partner_dmmap::Phase::backoff, mgscoop::partner_dmmap::Phase::failed,
                     mgscoop::partner_dmmap::Phase::release}) {
        m.dmmap.phase = phase;
        Check(!pd::NativeSleepPresentationReady(s.driver), "sleep refuses every nonidle mapper phase without cleanup calls");
    }
    m.dmmap.phase = mgscoop::partner_dmmap::Phase::idle;
    Check(pd::NativeSleepPresentationReady(s.driver) && s.fake.dm_patch_calls == patches && s.fake.dm_clear_calls == clears &&
          s.played.size() == plays && s.driver.settings.dm == mgscoop::PartnerDm::map,
          "admission query is read-only and preserves normal clip fallback configuration");
    s.Trigger("drive=legacy");
    Check(s.N() && !pd::NativeSleepPresentationReady(s.driver), "actual native leave withdraws sleep admission");
}

void TestNativeEnterAndLeave() {
    namespace pn = mgscoop::partner_native;
    NativeSession s;
    // A dm=map clip plays first (the proven fallback).
    Check(s.Playing() >= 0 && s.driver.mission.dm_patched, "fallback: dm=map plays first");
    Check(s.driver.mission.native_out.mode == pn::Mode::fallback && s.fake.defer_installs >= 1 &&
          s.fake.defer_install_mode == mgscoop::PartnerDefer::on, "fallback while settling; install requested with defer=on");
    Check(s.OrderIndex("identity") >= 0 && s.fake.identity.go == 0x1000 - 0x20 && s.fake.identity.pool1 == 1 && s.fake.roots_published >= 1,
          "identity and wrap roots published every resolving tick, before any arming");
    // Entering after 2000 ms: dm stop -> revert -> place -> armed -> active.
    Check(s.Until(pn::Mode::entering, 3000), "entering after the 2000 ms hold");
    Check(s.fake.Count("partner_native=entering; reason=ok;") == 1 && s.fake.Containing("partner_native=enter; step=dm_stop") == 1,
          "entering logged with the dm_stop step");
    const bool active = s.Until(pn::Mode::active, 6000);
    Check(active, "active after the DM release, revert, placement and arming");
    Check(!s.driver.mission.dm_patched && s.driver.mission.input_armed && s.fake.Containing("partner_dm=reverted;") >= 1,
          "the DM patch is reverted and the window armed");
    Check(s.fake.Containing("partner_native=enter; step=place") == 1 && s.fake.Containing("partner_native=enter; step=armed") == 1,
          "every enter step logged once");
    const auto& arm = s.fake.arms.back();
    Check(arm.enabled && arm.native && arm.config.mode == mgscoop::PartnerInput::net && arm.config.mask == 0x2240006u &&
          arm.wrap == mgscoop::PartnerWrap::window && arm.hold == mgscoop::PartnerInputHold::own && arm.go == 0x1000 - 0x20 &&
          !arm.local_all && arm.naim == mgscoop::PartnerNativeAim::off && !arm.p218, "the native Arm (effective settings)");
    // Publication order: identity -> deferral armed -> enabled native Arm (run
    // 27c64c8d: an Arm before the deferral let block 1's job run inline in a
    // batch for one frame, which left with wrap_in_batch).
    const int id = s.OrderIndex("identity"), arm1 = s.OrderIndex("arm:1"), def1 = s.OrderIndex("defer:1");
    Check(id >= 0 && def1 > id && arm1 > def1, "publication order on entering: identity, defer, Arm");
    Check(s.driver.mission.native_defer_armed, "deferral armed");
    // Steady active: no xform (apply=input), no dm play.
    const std::size_t xforms = s.fake.Calls("xform:");
    const std::size_t plays = s.played.size();
    for (int i = 0; i < 60; ++i) s.N();
    Check(s.driver.mission.native_out.mode == pn::Mode::active && s.fake.Calls("xform:") == xforms && s.played.size() == plays,
          "active: the native drive owns the body (no xform, no clips)");
    Check(s.fake.Count("partner_defer; batches=") >= 1 && s.fake.Count("partner_native; mode=active;") >= 1, "1 Hz native lines");
    // Steering never bumps the Arm generation (SameArm ignores it).
    {
        const std::uint32_t gen = s.fake.arms.back().gen;
        s.driver.mission.steer_rad = 0.2;  // as if the remote track drifted left
        s.fake.sample.x = 1.5f;            // the target moves; the body stays
        for (int i = 0; i < 10; ++i) s.N();
        Check(s.fake.arms.back().enabled && s.fake.arms.back().gen == gen, "steering changes never bump the Arm gen");
        s.fake.sample.x = 1.0f;
    }
    // Telemetry for the worker state.json.
    const auto& tele = s.driver.mission.tele;
    Check(tele.configured && tele.mode == pn::Mode::active && tele.armed && tele.h1 && tele.h2 && tele.thread_ok &&
          tele.setting == mgscoop::PartnerDefer::on && tele.per_s_valid && tele.deferred > 0 && tele.ring.count > 0 &&
          std::strcmp(tele.reason, "ok") == 0, "telemetry populated (mode, deferral, rates, ring)");
    // A deferral fault: leave at once (defer off -> window off -> dm=map rearm).
    s.fake.defer.faulted = true;
    s.fake.defer.fault_phase = 1;
    const std::size_t order = s.fake.native_order.size();
    const std::uint32_t epoch = s.driver.mission.body_epoch;
    s.N();
    Check(s.driver.mission.native_out.mode == pn::Mode::leaving && s.fake.Count("partner_native=leaving; reason=faulted;") == 1,
          "a deferral fault leaves immediately");
    const int def0 = s.OrderIndex("defer:0", static_cast<int>(order)), arm0 = s.OrderIndex("arm:0", static_cast<int>(order));
    Check(def0 >= 0 && arm0 > def0, "publication order on leaving: defer disarmed before the window");
    Check(s.driver.mission.body_epoch > epoch && s.fake.Has("partner_defer=fault; phase=lost; code=0x00000000; deferral=disabled\r\n"),
          "epoch bumped; fault line once");
    s.N();
    Check(s.driver.mission.native_out.mode == pn::Mode::fallback && s.driver.mission.native.fallbacks == 1, "then fallback");
    // dm=map resumes (patched again, a clip plays).
    bool replayed = false;
    for (int i = 0; i < 200 && !replayed; ++i) { s.N(); replayed = s.driver.mission.dm_patched && !s.played.empty() && s.played.size() > plays; }
    Check(replayed, "dm=map rearmed after leaving");
    // Faulted stays in fallback (no re-entry).
    for (int i = 0; i < 300; ++i) s.N();
    Check(s.driver.mission.native_out.mode == pn::Mode::fallback && std::strcmp(s.driver.mission.native_out.reason, "faulted") == 0,
          "faulted: stays in fallback");
}

void TestNativeRefusalsAndCensus() {
    namespace pn = mgscoop::partner_native;
    // drive=legacy: today's behaviour, nothing new installed or armed.
    {
        MapSession s;
        AddNativeHooks(s.hooks);
        s.Run(3000);
        bool native_calls = false;
        for (const auto& e : s.fake.native_order) native_calls = native_calls || e.rfind("defer:", 0) == 0 || e.rfind("identity", 0) == 0;
        Check(s.fake.defer_installs == 0 && !native_calls && s.fake.roots_published == 0 && !s.played.empty() &&
              s.driver.mission.native_out.mode == mgscoop::partner_native::Mode::off, "legacy installs, arms and publishes nothing new");
    }
    // census: identity published while disarmed, never active, the partner still animates via dm=map.
    {
        NativeSession s(mgscoop::PartnerDefer::census);
        for (int i = 0; i < 200; ++i) s.N();
        Check(s.driver.mission.native_out.mode == pn::Mode::fallback && std::strcmp(s.driver.mission.native_out.reason, "defer_census") == 0,
              "census never activates");
        Check(s.fake.identity_valid && s.fake.roots_census && s.fake.defer_install_mode == mgscoop::PartnerDefer::census &&
              s.OrderIndex("defer:1") < 0 && !s.played.empty(), "census: identity + census roots, never armed, dm=map plays");
    }
    // partner_input_hook=0 refuses the install (hook_disabled), nothing installed.
    {
        NativeSession s;
        s.lab.partner_input_hook = false;
        for (int i = 0; i < 100; ++i) s.N();
        Check(s.fake.defer_installs == 0 && std::strcmp(s.driver.mission.native_out.reason, "install_hook_disabled") == 0,
              "hook disabled: install refused");
    }
    // An install refusal (bytes) keeps the fallback for the process.
    {
        NativeSession s;
        s.fake.defer_install_result = "bytes";
        for (int i = 0; i < 200; ++i) s.N();
        Check(std::strcmp(s.driver.mission.native_out.reason, "install_bytes") == 0 && !s.played.empty(), "install_bytes keeps dm=map");
    }
    // A thread mismatch never enters.
    {
        NativeSession s;
        s.fake.defer.batch_thread = 78;
        for (int i = 0; i < 200; ++i) s.N();
        Check(std::strcmp(s.driver.mission.native_out.reason, "thread") == 0, "batch thread != hook thread refuses");
    }
    // A failed DM revert while entering: leave and latch for the mission.
    {
        NativeSession s;
        s.Playing();
        s.fake.dm_revert_fail = true;
        bool latched = false;
        for (int i = 0; i < 400 && !latched; ++i) { s.N(); latched = s.driver.mission.native_out.mode == pn::Mode::latched; }
        Check(latched && s.fake.Has("partner_native=latched; reason=dm_revert_failed; fallbacks=1\r\n"), "dm revert failure latches");
        s.fake.dm_revert_fail = false;
        s.Trigger("drive=native");
        Check(s.driver.mission.native.mode == pn::Mode::fallback && s.fake.Count("partner_native=trigger; drive=native;") == 1,
              "a drive=native trigger unlatches");
        pd::MissionStart(s.driver, s.hooks, 30010);
        Check(s.driver.mission.native.mode == pn::Mode::fallback && s.driver.mission.native.fallbacks == 0, "MissionStart resets");
    }
}

void TestNativeChannel2AndSender() {
    // Reliable channel 2 is drained every tick, also in the legacy drive.
    {
        MapSession s;
        AddNativeHooks(s.hooks);
        mgscoop::partner_equip_wire::Manifest m{};
        m.revision = 3; m.epoch = 7; m.inv_slot = 1; m.slots[1].base_id = 46; m.slots[1].alias = 0x2E;
        std::uint8_t bytes[mgscoop::partner_equip_wire::kSize]{};
        Check(mgscoop::partner_equip_wire::Encode(m, bytes) == sizeof(bytes), "manifest encodes");
        s.fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
        s.fake.channel2.emplace_back(std::vector<std::uint8_t>{1, 2, 3});
        s.T();
        Check(s.fake.channel2.empty() && s.driver.mission.equip.have && s.driver.mission.equip.manifest.revision == 3 &&
              s.driver.mission.equip.invalid == 1 &&
              s.fake.Has("partner_equip=manifest; rev=3; inv=1; slots=0:0,46:46,0:0; type=0,0,0; v=2; mission=30050\r\n"),
              "channel 2 drained in legacy: stored, invalid counted, logged");
        const unsigned drains = s.fake.channel2_drains;
        s.T();
        Check(s.fake.channel2_drains > drains, "drained every tick");
    }
    // A native sender writes v3 (look, gated camera/heading); a legacy one v2.
    {
        NativeSession s;
        s.fake.look_source_ok = true;
        s.fake.look_source = pp::LookSource{true, true, true, 0.25f, -1.5f, 2.0f, 3.0f, 0.5f};
        s.fake.pad.src_w[0] = 0x8;  // HOLD held
        s.fake.inputs.clear();
        s.N();
        // partner_native_rates=log (default): v4, no rates while the local record is unread; no gun hook: no magazine.
        Check(!s.fake.inputs.empty() && s.fake.inputs.back().version == 5 && (s.fake.inputs.back().xflags & 0x0F) == 0x0F &&
              !(s.fake.inputs.back().xflags & mgscoop::partner_input::XFlag::Rates) && s.fake.inputs.back().rate_dash == 0 &&
              !(s.fake.inputs.back().xflags & mgscoop::partner_input::XFlag::Mag) && s.fake.inputs.back().mag == 0 &&
              s.fake.inputs.back().look_pitch == 0.25f && s.fake.inputs.back().heading_mrad == 500,
              "native sender: v4 with every v3 group (no rates, no magazine)");
        s.fake.window_seq = 3;  // a window open: camera and heading not quiet
        s.N();
        Check(s.fake.inputs.back().version == 5 && (s.fake.inputs.back().xflags & 0x06) == 0 && (s.fake.inputs.back().xflags & 0x01),
              "odd WindowSeq: camera/heading dropped, look kept");
        s.driver.settings.nrates = mgscoop::PartnerNativeRates::off;
        s.N();
        Check(s.fake.inputs.back().version == 5 && !(s.fake.inputs.back().xflags & mgscoop::partner_input::XFlag::Rates) &&
                  s.fake.inputs.back().rate_dash == 0 && s.fake.inputs.back().rate_crawl == 0,
              "partner_native_rates=off: still v4 (the magazine group), never rates");

        // Online play: the sender no longer depends on its own drive. A copy
        // that shows clips (drive=legacy) still sends v4, so the peer's native
        // drive gets the look, rates and magazine (docs/native-puppet.md "Online").
        Session l(3, mgscoop::PartnerApply::xform, mgscoop::PartnerSource::net);
        l.driver.settings.isend = true;
        AddNativeHooks(l.hooks);
        l.fake.look_source_ok = true;
        l.fake.look_source = pp::LookSource{true, true, true, 0.25f, -1.5f, 2.0f, 3.0f, 0.5f};
        l.Tick(1);
        // No peer_input_v5 hook: v5 always (both copies one build), still the v4 groups.
        Check(!l.fake.inputs.empty() && l.fake.inputs.back().version == 5 && (l.fake.inputs.back().xflags & 0x01) &&
                  l.fake.inputs.back().look_pitch == 0.25f,
              "legacy sender sends v4+ with the look (decoupled from its own drive)");
        // The v5 negotiation runs whatever the sender's drive: an older peer gets v4 frames plus the probe.
        l.hooks.peer_input_v5 = HookPeerInputV5;
        l.fake.peer_v5 = false;
        l.fake.inputs.clear();
        for (int i = 0; i < 61; ++i) l.Tick(1);
        unsigned lv4 = 0, lv5 = 0, lother = 0;
        for (const auto& p : l.fake.inputs) (p.version == 4 ? lv4 : p.version == 5 ? lv5 : lother)++;
        Check(lother == 0 && lv4 > 0 && lv5 >= 1 && lv5 <= 2 && l.driver.mission.v5_probes >= 1,
              "legacy sender: v4 frames plus a v5 probe for an older peer");
        l.fake.peer_v5 = true;
        l.fake.inputs.clear();
        l.Tick(1);
        Check(!l.fake.inputs.empty() && l.fake.inputs.back().version == 5, "legacy sender: v5 once the peer sent v5");
    }
}

// Native rates (docs/native-puppet.md "Rates"): the sender's v4 rates, the
// mirror request in the Arm, telemetry and the forget at mission boundaries.
void TestNativeRates() {
    namespace pn = mgscoop::partner_native;
    namespace pi = mgscoop::partner_input;
    const auto record = [](std::uintptr_t sp, std::uintptr_t rec, std::uint32_t id, float dash, float crawl, float factor) {
        pp::RateRead r{};
        r.sp = sp; r.rec = rec; r.id = id;
        r.f[0] = crawl; r.f[1] = factor; r.f[2] = crawl * factor; r.f[3] = dash; r.f[4] = 1.0f; r.f[5] = dash;
        return r;
    };
    NativeSession s;
    s.driver.settings.nrates = mgscoop::PartnerNativeRates::mirror;
    s.fake.rates[0] = record(0x8000, 0x8100, 0, 1.2f, 1.0f, 1.2f);
    s.fake.rates[1] = record(0x9000, 0x9100, 1, 1.0f, 1.0f, 1.0f);
    s.fake.rates_ok[0] = s.fake.rates_ok[1] = true;
    s.fake.inputs.clear();
    s.N();
    Check(!s.fake.inputs.empty() && s.fake.inputs.back().version == 5 && (s.fake.inputs.back().xflags & pi::XFlag::Rates) &&
          s.fake.inputs.back().rate_dash == 1200 && s.fake.inputs.back().rate_crawl == 1000 && s.fake.inputs.back().rate_crawl_factor == 1200,
          "sender: the local record's dash/crawl/crawl factor in v4");
    // Before the remote's rates arrive: the mirror waits (no_remote), never requested.
    Check(s.Until(pn::Mode::active, 9000), "active");
    Check(!s.fake.arms.back().rates_mirror && s.fake.arms.back().rates_rec == 0x9100 && s.fake.arms.back().rates_sp == 0x9000 &&
          s.fake.arms.back().rates_id == 1 && std::strcmp(s.driver.mission.tele.rates_mirror, "wait") == 0 &&
          std::strcmp(s.driver.mission.tele.rates_reason, "no_remote") == 0, "no remote rates: wait (no_remote), no mirror request");
    // The remote's v4 rates: the Arm requests the mirror with them and the epoch.
    s.fake.latest.packet.version = 4;
    s.fake.latest.packet.xflags = pi::XFlag::Rates;
    s.fake.latest.packet.rate_dash = 1200;
    s.fake.latest.packet.rate_crawl = 1000;
    s.fake.latest.packet.rate_crawl_factor = 1200;
    s.N();
    const pi::Arm& arm = s.fake.arms.back();
    Check(arm.enabled && arm.native && arm.rates_mirror && arm.rates_remote[0] == 1200 && arm.rates_remote[1] == 1000 &&
          arm.rates_remote[2] == 1200 && arm.rates_epoch == s.driver.rates_epoch, "remote v4 rates: the Arm requests the mirror");
    // The hook applied it: telemetry ok with both records and the remote.
    s.fake.stats.native.rates_applied = true;
    s.fake.stats.native.rates_writes = 1;
    s.N();
    const pn::Telemetry& t = s.driver.mission.tele;
    Check(std::strcmp(t.rates_mirror, "ok") == 0 && t.rates_setting == mgscoop::PartnerNativeRates::mirror && t.rates_id0_ok &&
          t.rates_id1_ok && t.rates_id0[3] == 1200 && t.rates_id1[3] == 1000 && t.rates_remote_ok && t.rates_remote[0] == 1200 &&
          t.rates_writes == 1, "telemetry: mirror ok, id0/id1 records, remote");
    Check(s.fake.Containing("partner_native_rates; setting=mirror; mirror=ok;") >= 1 &&
          s.fake.Containing("partner_native_rates; setting=mirror; mirror=wait; reason=no_remote;") >= 1, "rates lines on change");
    // The hook latched (an identity refusal): reported with its reason.
    s.fake.stats.native.rates_applied = false;
    s.fake.stats.native.rates_latched = true;
    s.fake.stats.native.rates_reason = static_cast<std::uint32_t>(mgscoop::partner_rates::Reason::rec_vtable);
    s.N();
    Check(std::strcmp(s.driver.mission.tele.rates_mirror, "latched") == 0 && std::strcmp(s.driver.mission.tele.rates_reason, "rec_vtable") == 0,
          "latched with its reason");
    // An unreadable instance-1 record: never a mirror request.
    s.fake.stats.native.rates_latched = false;
    s.fake.rates_ok[1] = false;
    s.fake.rates[1].refusal = "rec_vtable";
    for (int i = 0; i < 5; ++i) s.N();
    Check(!s.fake.arms.back().rates_mirror && s.fake.arms.back().rates_rec == 0 &&
          std::strcmp(s.driver.mission.tele.rates_id1_refusal, "rec_vtable") == 0, "unreadable id1 record: no mirror request");
    // Stale remote rates (no v4 frame for > 2 s): the mirror request ends (the hook restores).
    s.fake.rates_ok[1] = true;
    s.fake.rates[1].refusal = nullptr;
    s.fake.latest.packet.version = 3;
    s.fake.latest.packet.xflags = 0;
    s.fake.latest.packet.rate_dash = s.fake.latest.packet.rate_crawl = s.fake.latest.packet.rate_crawl_factor = 0;
    for (int i = 0; i < 80; ++i) s.N();
    Check(!s.fake.arms.back().rates_mirror && !s.driver.mission.tele.rates_remote_ok, "remote rates older than 2 s: no mirror request");
    // log: records and remote read, never a mirror request.
    s.driver.settings.nrates = mgscoop::PartnerNativeRates::log;
    s.fake.latest.packet.version = 4;
    s.fake.latest.packet.xflags = pi::XFlag::Rates;
    s.fake.latest.packet.rate_dash = 1200; s.fake.latest.packet.rate_crawl = 1000; s.fake.latest.packet.rate_crawl_factor = 1200;
    s.N();
    Check(!s.fake.arms.back().rates_mirror && std::strcmp(s.driver.mission.tele.rates_mirror, "off") == 0 &&
          std::strcmp(s.driver.mission.tele.rates_reason, "log") == 0 && s.driver.mission.tele.rates_remote_ok,
          "log: telemetry only");
    // Mission boundaries: the hook forgets before the disarmed Arm; a new epoch.
    const std::uint32_t epoch = s.driver.rates_epoch;
    const unsigned forgets = s.fake.rates_forgets;
    s.fake.native_order.clear();
    pd::MissionEnd(s.driver, s.hooks);
    Check(s.fake.rates_forgets == forgets + 1 && s.OrderIndex("rates_forget") >= 0 && s.OrderIndex("arm:0") > s.OrderIndex("rates_forget") &&
              s.fake.rates_forget_epoch == epoch && epoch != 0,
          "MissionEnd: forget (the ending mission's epoch) before the disarmed Arm");
    pd::MissionStart(s.driver, s.hooks, 30010);
    Check(s.fake.rates_forgets == forgets + 2 && s.fake.rates_forget_epoch == epoch && s.driver.rates_epoch == epoch + 1,
          "MissionStart: forget the old epoch, then a new epoch");
}

// Aim parity (docs/native-puppet.md "Facing", the aim-target mirror): the
// driver publishes a target 100 m along the remote's live look from the
// partner's eyes while the native drive is active with a gun; nothing wanted
// while off or inactive; wanted but invalid without a look; the debug
// override wins over lab.ini; the telemetry and the 1 Hz line.
void TestNativeGunDirectionWire() {
    namespace pi = mgscoop::partner_input;
    NativeSession s;
    s.hooks.peer_input_v6 = HookPeerInputV6;
    AddWeaponHooks(s.hooks);
    s.hooks.read_aim = HookReadAim;
    s.driver.settings.ngun = true;
    Check(s.Until(mgscoop::partner_native::Mode::active,9000), "v6 sender: native active");
    s.fake.inputs.clear();
    const auto probes = s.driver.mission.v6_probes;
    for (unsigned n=0;n<30;++n) s.N();
    unsigned v5=0, v6=0;
    for (const auto& in:s.fake.inputs) { if(in.version==5)++v5; if(in.version==6)++v6; }
    Check(v5>0 && v6<=1 && s.driver.mission.v6_probes <= probes+1, "v6 unknown peer: legacy frames with bounded probes");
    s.fake.peer_v6=true; s.fake.inputs.clear();
    auto& r=s.fake.aim_read;
    r.gun_ok[0]=r.rate_ok[0]=r.body_ok[0]=true; r.stance[0]=2; r.gun_class[0]=0;
    r.r5c[0]=.625f; r.r60[0]=.517f; r.body_yaw[0]=static_cast<float>(4.8*3.14159265358979323846/180.0);
    s.fake.aim_read_ok = true; s.fake.gun0_ok = true;
    s.fake.gun0.fields_ok = true; s.fake.gun0.fields.flags27c = 1u<<6; s.fake.gun0.fields.equip = 46;
    s.fake.gun0.gun3c0 = (1u<<18)|4u; s.fake.gun0.mode_cur = 7;
    s.N();
    Check(!s.fake.inputs.empty() && s.fake.inputs.back().version==6 && s.driver.mission.v6_peer_logged,
          "v6 negotiated peer sends v6 and records capability once");
    const auto& sent=s.fake.inputs.back();
    Check((sent.gun_flags & pi::GunFlag::AimDir) && std::abs(sent.aim_pitch_cdeg-1875)<=1 && std::abs(sent.aim_yaw_cdeg-174)<=1,
          "v6 sender puts current gun pose direction on the wire");
    r.stance[0]=3; r.r5c[0]=.770f; r.r60[0]=.541f; s.N();
    Check(!(s.fake.inputs.back().gun_flags & pi::GunFlag::AimDir) &&
              s.driver.mission.dir_reason == mgscoop::partner_aim::DirReason::prone_frame,
          "v6 sender: prone clears the previous direction and reports the missing frame");
    r.stance[0]=2;
    r.gun_flags[0]=0x2000; s.N();
    Check(!(s.fake.inputs.back().gun_flags & pi::GunFlag::AimDir), "v6 sender refuses camera-relative mapping and clears old direction");
    // Receiver point direction chooses the received group, stale input cannot retain it.
    s.driver.settings.naimpt=mgscoop::PartnerNativeAimPt::point;
    s.hooks.publish_aim=HookPublishAim; s.hooks.aim_stats=HookAimStats;
    s.fake.latest.packet.version=6; s.fake.latest.packet.gun_flags=pi::GunFlag::Valid|pi::GunFlag::Aiming|pi::GunFlag::AimDir;
    s.fake.latest.packet.aim_pitch_cdeg=0; s.fake.latest.packet.aim_yaw_cdeg=9000;
    s.N();
    Check(!s.fake.aim_pubs.empty() && s.fake.aim_pubs.back().src==static_cast<std::uint8_t>(mgscoop::partner_aim::Source::point),
          "v6 receiver: received gun direction replaces look");
    s.fake.latest.packet.gun_flags &= ~pi::GunFlag::AimDir;
    s.fake.latest.packet.xflags = pi::XFlag::Look;
    s.fake.latest.packet.look_pitch = s.fake.latest.packet.look_yaw = 0;
    s.N();
    Check(s.fake.aim_pubs.back().valid &&
              s.fake.aim_pubs.back().src == static_cast<std::uint8_t>(mgscoop::partner_aim::Source::look),
          "v6 receiver: direction refusal clears the prior point target and uses look");
}
void TestNativeAim() {
    namespace pn = mgscoop::partner_native;
    namespace pi = mgscoop::partner_input;
    namespace pam = mgscoop::partner_aim;
    NativeSession s;
    s.hooks.publish_aim = HookPublishAim;
    s.hooks.aim_stats = HookAimStats;
    s.hooks.read_aim = HookReadAim;
    s.driver.settings.ngun = true;
    s.driver.settings.naimpt = mgscoop::PartnerNativeAimPt::look;
    s.fake.latest.packet.xflags = pi::XFlag::Look;
    s.fake.latest.packet.look_pitch = 0.0f;
    s.fake.latest.packet.look_yaw = 0.0f;
    s.N();
    Check(!s.fake.aim_pubs.empty() && !s.fake.aim_pubs.back().want &&
              s.fake.aim_pubs.back().reason == static_cast<std::uint8_t>(pam::Reason::inactive),
          "aim: before the native drive is active nothing is wanted (inactive)");
    Check(s.Until(pn::Mode::active, 9000), "aim: active");
    s.N();
    const mgscoop::partner_aim_point::Publication& pub = s.fake.aim_pubs.back();
    Check(pub.want && pub.valid && pub.src == static_cast<std::uint8_t>(pam::Source::look) && pub.epoch == s.driver.rates_epoch &&
              std::fabs(pub.p[0] - 1.0f) < 1e-3f && std::fabs(pub.p[1] - 2.6f) < 1e-3f && std::fabs(pub.p[2] - 103.0f) < 1e-3f &&
              pub.ms == s.now,
          "aim: active with a gun: the target 100 m along the remote's look (yaw 0 = +z) from the body's gun pivot");
    // No live look: wanted, invalid (the hook keeps its last write).
    s.fake.latest.packet.xflags = 0;
    s.N();
    Check(s.fake.aim_pubs.back().want && !s.fake.aim_pubs.back().valid &&
              s.fake.aim_pubs.back().reason == static_cast<std::uint8_t>(pam::Reason::no_look) &&
              std::strcmp(s.driver.mission.tele.aim_reason, "no_look") == 0,
          "aim: no v3 look: wanted, not valid (no_look)");
    s.fake.latest.packet.xflags = pi::XFlag::Look;
    // The debug override wins over lab.ini; -1 returns to it.
    s.fake.aim_stats.override_value = 0;
    s.N();
    Check(!s.fake.aim_pubs.back().want && s.driver.mission.tele.aim_override && std::strcmp(s.driver.mission.tele.aim_setting, "off") == 0,
          "aim: the override off wants nothing (the hook restores)");
    s.fake.aim_stats.override_value = -1;
    s.N();
    Check(s.fake.aim_pubs.back().want && !s.driver.mission.tele.aim_override && std::strcmp(s.driver.mission.tele.aim_setting, "look") == 0,
          "aim: back to lab.ini");
    // Without partner_native_gun the mirror is inactive.
    s.driver.settings.ngun = false;
    s.N();
    Check(!s.fake.aim_pubs.back().want, "aim: no gun stage: nothing wanted");
    s.driver.settings.ngun = true;
    // Telemetry from the read and the hook's statistics.
    s.fake.aim_stats.state = static_cast<std::uint8_t>(mgscoop::partner_aim_point::State::ok);
    s.fake.aim_stats.writes = 42;
    s.fake.aim_stats.overwritten = 1;
    pp::AimRead& r = s.fake.aim_read;
    r.q_ok = true;
    r.q10[1][0] = 1.0f; r.q10[1][1] = 2.6f; r.q10[1][2] = 103.0f;
    for (unsigned k = 0; k < 2; ++k) {
        r.gun_ok[k] = r.rate_ok[k] = r.body_ok[k] = true;
        r.body[k][0] = 1.0f; r.body[k][1] = 2.0f; r.body[k][2] = 3.0f;
        r.gun_state[k] = 7;
    }
    r.aimpt[1][0] = 1.0f; r.aimpt[1][1] = 2.6f; r.aimpt[1][2] = 103.2f;   // straight ahead (+z), 0.2 m beyond the target
    r.aimpt[0][0] = 101.0f; r.aimpt[0][1] = 2.6f; r.aimpt[0][2] = 3.0f;   // instance 0 aims along +x
    r.r60[1] = 0.95f; r.r5c[1] = 0.5f; r.r60[0] = 0.5f;
    r.path_flags[1] = c::gun3d0_bone_path; r.b3d5[1] = 0;  // the bone path
    s.fake.aim_read_ok = true;
    for (int i = 0; i < 35; ++i) s.N();  // past the 1 Hz line's cadence
    const pn::Telemetry& t = s.driver.mission.tele;
    Check(std::strcmp(t.aim_state, "ok") == 0 && t.aim_writes == 42 && t.aim_overwritten == 1 && std::strcmp(t.aim_src, "look") == 0 &&
              t.aim_target_mm[2] == 103000 && t.aim_q10_mm[1][2] == 103000 && t.aim_point_mm[1][2] == 103200 && t.aim_point_err_mm == 200,
          "aim telemetry: state, counters, target, slot, aim point and its error");
    Check(t.aim_yaw_cdeg[1] == 0 && t.aim_pitch_cdeg[1] == 57 && t.aim_yaw_cdeg[0] == 9000 && t.aim_rel_yaw_cdeg[1] == -8100 &&
              t.aim_r60[1] == 950 && t.aim_pred[1] == 1 && t.aim_pred[0] == 0 && t.aim_gun_state[1] == 7 &&
              std::strcmp(t.aim_path[1], "bone") == 0 && std::strcmp(t.aim_path[0], "cam") == 0,
          "aim telemetry: world aim directions, the predicate's yaw, r60, the predicate, the gun state and the path");
    Check(t.ring.count && pn::At(t.ring, t.ring.count - 1).r60_1 == 950 && pn::At(t.ring, t.ring.count - 1).pred1 == 1,
          "aim: the ring carries r60_1 and pred1");
    Check(s.fake.Containing("partner_native_aim; setting=look; override=0; state=ok; reason=none; src=look; writes=42;") >= 1 &&
              s.fake.Containing("pred1=1; gun1=7; path1=bone") >= 1,
          "aim: the 1 Hz partner_native_aim line");
    Check(std::strcmp(t.aim_slot, "na") == 0 && t.aim_written_mm[0] == pn::kNull && s.fake.Containing("; slot=na; ") >= 1,
          "aim: no hook write yet: the slot verdict is na");
    // The hook's last write: the slot holds it (ours) and the aim point error is
    // measured against it (the target leads the write by a tick while moving).
    s.fake.aim_stats.written_ok = true;
    s.fake.aim_stats.written[0] = 1.0f; s.fake.aim_stats.written[1] = 2.6f; s.fake.aim_stats.written[2] = 102.9f;
    r.q10[1][2] = 102.9f;
    for (int i = 0; i < 35; ++i) s.N();
    Check(std::strcmp(t.aim_slot, "ours") == 0 && t.aim_written_mm[2] == 102900 && t.aim_target_mm[2] == 103000 &&
              t.aim_point_err_mm == 300 && s.fake.Containing("; slot=ours; ") >= 1,
          "aim: slot ours (the slot holds the hook's write), the aim point error against the write");
    r.q10[1][2] = 50.0f;  // restored, or something else wrote the slot
    for (int i = 0; i < 35; ++i) s.N();
    Check(std::strcmp(t.aim_slot, "other") == 0 && t.aim_point_err_mm == 200 && s.fake.Containing("; slot=other; ") >= 1,
          "aim: slot other (the error falls back to this tick's target)");
    // Off in lab.ini: nothing wanted, state off.
    s.driver.settings.naimpt = mgscoop::PartnerNativeAimPt::off;
    s.N();
    Check(!s.fake.aim_pubs.back().want && std::strcmp(t.aim_state, "off") == 0 && std::strcmp(t.aim_reason, "off") == 0, "aim: lab.ini off");
}

// Native weapons (docs/native-puppet.md "Weapons"): the fire gate, the
// mirror's Arm fields, the v4 magazine, the loadout manifest and its verdict.
void SetVisualFire(NativeSession& s) {
    AddWeaponHooks(s.hooks);
    s.driver.settings.ngun = true;
    s.driver.settings.nfire = mgscoop::PartnerNativeFire::visual;
    s.driver.settings.input_mask = 0x224005Eu;
    s.fake.Realize(true);
    s.fake.gun_ok = true;
    s.fake.gun.fields_ok = true;
    s.fake.gun.fields.flags27c = 1u << 6;  // class 1 (a gun)
    s.fake.gun.prim = 0x9000;
    s.fake.gun.rec = 0x9100;
}
// Native polish (docs/native-puppet.md "Facing", "Weapons").
void TestMovementRecovery() {
    namespace pn = mgscoop::partner_native;
    namespace pad = mgscoop::partner_pad;
    NativeSession s;
    s.fake.anim_ok = true; s.fake.anim.act[1] = 2; s.fake.anim.stance[1] = 1; s.fake.anim.speed[1] = 2;
    s.fake.sample.anim_valid = true; s.fake.sample.act = 2; s.fake.sample.anim_stance = 1; s.fake.sample.anim_speed = 2;
    s.fake.latest.packet.ly = 32767; s.fake.latest.packet.lmag = 65535;
    Check(s.Until(pn::Mode::active, 9000), "movement recovery fixture active");
    for (int n = 0; n < 50; ++n) s.N();
    s.fake.sample.anim_speed = 3;
    for (int n = 0; n < 13; ++n) s.N();
    Check(s.fake.Containing("partner_recon=dashfix;") == 1 && s.driver.mission.dash.attempts == 1 &&
          (s.fake.arms.back().tap_bits & pad::kDash), "driver wires stable sprint mismatch into one existing DASH overlay");
    s.fake.anim.speed[1] = 3;
    for (int n = 0; n < 60; ++n) s.N();
    Check(s.fake.Containing("partner_recon=dashfix;") == 1, "matching native sprint cancels further relatch attempts");
    s.fake.latest.packet.held = pad::kHold | pad::kDash;
    s.N();
    Check(s.driver.mission.hold_stripped && (s.fake.arms.back().strip & pad::kDash) &&
          !(s.fake.arms.back().config.mask & pad::kHold), "actual stripped HOLD adds DASH to the existing all-word strip");
    s.driver.settings.ngun = true; s.driver.settings.input_mask |= pad::kHold;
    s.fake.gun_ok = s.fake.gun.fields_ok = true; s.fake.gun.fields.flags27c = 1u << 6;
    s.fake.gun.prim = 0x9000; s.fake.gun.rec = 0x9100;
    s.N(); s.N();
    Check(!s.driver.mission.hold_stripped && (s.fake.arms.back().config.mask & pad::kHold) &&
          !(s.fake.arms.back().strip & (pad::kHold | pad::kDash)), "admitted combat HOLD/DASH are untouched by parity policy");
    s.driver.mission.recon_out.report.runaway_idle = true;
    s.driver.mission.input_neutral = false;
    s.driver.mission.runaway_since_ms = s.now - 2000;
    s.N();
    Check(s.driver.mission.runaway_since_ms == 0, "arbiter runaway timer requires an actually neutral runaway window");

    // Actual source standing aim-walk is act 13, not idle aim 11/12. In the
    // loco profile HOLD is stripped, so the body stays act 2 while recon
    // freezes remote_act corrections. Only measured magnitude parity may run.
    NativeSession aimed;
    aimed.fake.anim_ok = true;
    aimed.fake.anim.act[1] = 2; aimed.fake.anim.stance[1] = 1; aimed.fake.anim.speed[1] = 2;
    aimed.fake.sample.anim_valid = true;
    aimed.fake.sample.act = 2; aimed.fake.sample.anim_stance = 1; aimed.fake.sample.anim_speed = 2;
    aimed.fake.latest.packet.ly = 32767; aimed.fake.latest.packet.lmag = 65535;
    aimed.driver.settings.trim_pct = 0;  // isolate HOLD parity from generic drift trim
    Check(aimed.Until(pn::Mode::active, 9000), "stripped aim-walk fixture enters native drive");
    for (int n = 0; n < 50; ++n) aimed.N();
    aimed.fake.sample.act = 13;
    aimed.fake.sample.flags = mgscoop::sync::ActionFlag::Aiming | mgscoop::sync::ActionFlag::Running;
    aimed.fake.sample.time_valid = true; aimed.fake.sample.sender_ms = 10000;
    aimed.fake.sample.generation = 1;
    aimed.fake.latest.packet.held = pad::kHold;
    const auto advance = [&]() {
        aimed.fake.sample.sender_ms += 100;
        aimed.fake.sample.z += 0.2f;       // source: 2 m/s in sender time
        aimed.fake.body_pos[2] += 0.4f;   // body's free locomotion: 4 m/s
        aimed.N(100);
    };
    for (int n = 0; n < 9; ++n) advance();
    const auto& aim_report = aimed.driver.mission.recon_out.report;
    Check(aimed.driver.mission.recon_out.mode == mgscoop::partner_recon::Mode::frozen &&
              aim_report.frozen == mgscoop::partner_recon::Frozen::remote_act &&
              aimed.driver.mission.hold_stripped && aim_report.v_remote_mmps > 1900 &&
              aim_report.v_free_mmps > 1.25 * aim_report.v_remote_mmps && aim_report.along_mm < -300,
          "source act13 with stripped raw HOLD remains frozen despite measured standing overrun");
    Check(aimed.driver.mission.mag_scale < 0.9 && aimed.driver.mission.mag_scale >= pn::kTrimFloor &&
              aimed.fake.arms.back().mag_scale == aimed.driver.mission.mag_scale &&
              (aimed.fake.arms.back().strip & pad::kDash) &&
              aimed.fake.Containing("partner_recon=dashfix;") == 0,
          "driver publishes bounded parity for frozen standing aim-walk without a DASH relatch");
    aimed.fake.latest.packet.held = 0;
    advance(); advance();  // TickNative consumes the previous recon observation
    Check(!aimed.driver.mission.hold_stripped && aimed.driver.mission.mag_scale == 1.0,
          "the same aim-walk overrun without raw HOLD cannot request parity trimming");
    aimed.fake.latest.packet.held = pad::kHold;
    for (int n = 0; n < 3; ++n) advance();
    Check(aimed.driver.mission.hold_stripped && aimed.driver.mission.mag_scale < 1.0,
          "live stripped HOLD re-admits measured standing aim-walk parity");
    aimed.fake.sample.anim_stance = 2;
    advance();
    Check(aimed.driver.mission.hold_stripped && aimed.driver.mission.mag_scale == 1.0 &&
              aimed.fake.anim.stance[1] == 1 && aimed.fake.sample.anim_stance == 2,
          "mismatched source/body stance refuses the aim-walk parity exception");
}

void TestNativePolish() {
    namespace pn = mgscoop::partner_native;
    {
        // Placement keeps an enabled native window with a neutral pad; the
        // arbiter is active only once the body is placed (neutral off).
        NativeSession s;
        Check(s.Until(pn::Mode::active, 9000), "active");
        bool neutral_seen = false;
        for (const auto& a : s.fake.arms) if (a.enabled && a.native && a.neutral) neutral_seen = true;
        Check(neutral_seen && !s.fake.arms.back().neutral && s.fake.arms.back().enabled && !s.driver.mission.input_neutral &&
                  s.fake.Containing("partner_input=neutral; on=1;") == 1 && s.fake.Containing("partner_input=neutral; on=0;") == 1 &&
                  s.fake.Containing("partner_input=disarmed; reason=place") == 0, "placement: a neutral native Arm, never a disarm");
        Check(s.fake.Containing("partner_recon=placed; ok=1;") >= 1 && s.fake.Containing("yaw_err_mdeg=") >= 1, "placed line with the yaw verdict");
        // A drain under a disabled Arm in active: leave (unwindowed).
        s.fake.stats.native.drain_disarmed += 1;
        s.N();
        Check(s.driver.mission.native_out.mode == pn::Mode::leaving && s.fake.Count("partner_native=leaving; reason=unwindowed;") == 1,
              "a disarmed drain while active leaves (unwindowed)");
    }
    {
        // The facing governor turns a quiet body toward the remote's yaw (xform at
        // its own position), backs off on contests and latches; never in a batch.
        NativeSession s;
        s.fake.anim_ok = true;
        s.fake.anim.act[1] = 1;
        s.fake.sample.anim_valid = true;
        s.fake.sample.act = 1;
        s.fake.xform_applies = true;
        Check(s.Until(pn::Mode::active, 9000), "facing: active");
        for (int i = 0; i < 70; ++i) s.N();
        const std::size_t xforms = s.fake.Calls("xform:");
        s.fake.sample.yaw = 0.7f;
        for (int i = 0; i < 50; ++i) s.N();
        const float yaw = mgscoop::partner::QuatToYaw(s.fake.body_quat);
        Check(s.fake.Containing("partner_recon=face_start; mode=idle;") == 1 && s.fake.Containing("partner_recon=face_end; reason=done;") == 1 &&
                  s.fake.Calls("xform:") > xforms + 3 && std::fabs(yaw - 0.7f) < 0.02f && s.driver.mission.tele.face_writes > 3 &&
                  s.driver.mission.native_out.mode == pn::Mode::active, "facing: an idle turn reaches the remote's yaw");
        // The game rotates the body back after every write: contests, then the latch.
        s.fake.xform_applies = false;
        s.fake.sample.yaw = 0.0f;
        for (int i = 0; i < 200; ++i) s.N();
        Check(s.fake.Containing("partner_recon=face_contest;") >= 3 && s.fake.Containing("partner_recon=face_latched;") == 1 &&
                  std::strcmp(s.driver.mission.tele.face_mode, "contested") == 0 && s.driver.mission.native_out.mode == pn::Mode::active,
              "facing: repeated contests latch the governor (still active)");
        Check(s.fake.Containing("partner_native_face; mode=") >= 1, "facing: the 1 Hz line");
    }
    {
        // A batch in flight: the governor's write is refused (no placement follows).
        NativeSession s;
        s.hooks.in_batch = HookInBatch;
        s.fake.anim_ok = true;
        s.fake.anim.act[1] = 1;
        s.fake.sample.anim_valid = true;
        s.fake.sample.act = 1;
        Check(s.Until(pn::Mode::active, 9000), "in batch: active");
        for (int i = 0; i < 70; ++i) s.N();
        s.fake.in_batch = true;
        const std::size_t places = s.fake.Containing("partner_recon=place;");
        s.fake.sample.yaw = 0.7f;
        for (int i = 0; i < 30; ++i) s.N();
        Check(s.fake.Containing("partner_recon=refused; action=face; reason=in_batch; fallback=none") == 1 &&
                  s.fake.Containing("partner_recon=place;") == places, "in batch: the face write refused, no placement");
    }
    {
        // The noise filter: FIRE only while it is live.
        NativeSession s;
        SetVisualFire(s);
        s.fake.noise_install_result = "bytes";
        Check(s.Until(pn::Mode::active, 9000), "noise refused: active");
        Check(!s.driver.settings.fire_live && !(s.fake.arms.back().config.mask & 0x10u) &&
                  s.fake.Containing("partner_fire=masked; reason=noise_bytes;") == 1 &&
                  std::strcmp(s.driver.mission.tele.fire_refused, "noise_bytes") == 0, "noise refused: FIRE masked");
        NativeSession t;
        SetVisualFire(t);
        Check(t.Until(pn::Mode::active, 9000) && t.driver.settings.fire_live && t.fake.noise_installs >= 1 &&
                  t.driver.mission.tele.noise_installed && t.driver.mission.tele.noise_live, "noise installed: FIRE live");
        t.fake.fire.noise_live = false;
        t.N();
        Check(!t.driver.settings.fire_live && t.fake.Containing("partner_fire=masked; reason=noise_lost;") == 1, "noise lost: FIRE masked");
        t.driver.settings.nnoise = mgscoop::PartnerNativeNoise::keep;
        t.N();
        Check(t.driver.settings.fire_live && (t.fake.arms.back().config.mask & 0x10u), "noise=keep: FIRE without the noise filter");
    }
    {
        // The weapon gate: the remote's gun equip (v4 xflags Equip) against body 1's.
        NativeSession s;
        SetVisualFire(s);
        s.fake.gun.fields.equip = 46;
        Check(s.Until(pn::Mode::active, 9000) && s.driver.settings.fire_live, "weapon gate: active, fire live");
        const std::uint32_t gen = s.fake.arms.back().gen;
        s.fake.latest.packet.version = 4;
        s.fake.latest.packet.xflags = static_cast<std::uint8_t>(s.fake.latest.packet.xflags | mgscoop::partner_input::XFlag::Equip);
        s.fake.latest.packet.gun_equip = 46;
        s.N();
        Check(s.fake.arms.back().strip == 0 && s.fake.Containing("partner_weapon=match;") == 1 && s.fake.Containing("weapon_match") == 0,
              "the same gun: no strip, the match logged once (no fire line for a first match)");
        s.fake.latest.packet.gun_equip = 100;
        s.N();
        Check(s.fake.arms.back().strip == 0x50u && s.fake.arms.back().gen == gen && s.driver.settings.fire_live &&
                  s.fake.Containing("partner_fire=masked; reason=weapon_mismatch; remote_equip=100; body_equip=46") == 1 &&
                  s.driver.mission.tele.weapon_mismatch && std::strcmp(s.driver.mission.tele.fire_refused, "weapon_mismatch") == 0,
              "another gun: FIRE and RELOAD stripped in the windows (no gen bump), logged");
        s.fake.latest.packet.gun_equip = 46;
        s.N();
        Check(s.fake.arms.back().strip == 0 && s.fake.Containing("partner_fire=live; reason=weapon_match;") == 1, "back to body 1's gun");
        s.fake.gun_ok = false;
        s.fake.latest.packet.gun_equip = 100;
        s.N();
        Check(s.fake.arms.back().strip == 0, "an unread body gun keeps the verdict");
        // A customized weapon's gun record holds its alias (the lab save's
        // back slot: 873 of base 46): the received manifest maps it to the
        // stock id body 1 carries; without a manifest it is compared raw.
        s.fake.gun_ok = true;
        s.fake.latest.packet.gun_equip = 873;
        s.N();
        Check(s.fake.arms.back().strip == 0x50u && s.driver.mission.remote_gun_base == 873 && s.driver.mission.weapon_mismatch,
              "an alias without a manifest: compared raw (masked)");
        namespace ew = mgscoop::partner_equip_wire;
        ew::Manifest rx{};
        rx.revision = 1; rx.epoch = 7; rx.inv_slot = 0xFF;
        rx.slots[0].base_id = 343; rx.slots[0].alias = 871;
        rx.slots[1].base_id = 46; rx.slots[1].alias = 873;
        std::uint8_t bytes[ew::kSize]{};
        Check(ew::Encode(rx, bytes) == sizeof(bytes), "weapon gate: the remote's manifest encodes");
        s.fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
        s.N();
        Check(s.fake.arms.back().strip == 0 && s.driver.mission.remote_gun_base == 46 && !s.driver.mission.weapon_mismatch &&
                  s.fake.Has("partner_weapon=match; remote_equip=873; remote_base=46; body_equip=46; strip=0x0\r\n") &&
                  s.driver.mission.tele.remote_gun_equip == 873 && s.driver.mission.tele.remote_gun_base == 46,
              "the alias 873 of base 46 is body 1's gun (FIRE and RELOAD pass)");
        s.fake.latest.packet.gun_equip = 871;
        s.N();
        Check(s.fake.arms.back().strip == 0x50u && s.driver.mission.remote_gun_base == 343 &&
                  s.fake.Has("partner_weapon=mismatch; remote_equip=871; remote_base=343; body_equip=46; strip=0x50\r\n"),
              "the alias 871 of base 343 is another gun (masked)");
        s.fake.latest.packet.gun_equip = 46;
        s.N();
        Check(s.fake.arms.back().strip == 0 && s.driver.mission.remote_gun_base == 46, "the stock id itself still matches");
    }
    {
        // The look source (naim=looksrc): installed lazily once, never inside a batch.
        NativeSession s;
        AddWeaponHooks(s.hooks);
        s.driver.settings.naim = mgscoop::PartnerNativeAim::looksrc;
        s.fake.Realize(true);
        s.fake.in_batch = true;
        for (int i = 0; i < 20; ++i) s.N();
        Check(s.fake.look_src_installs == 0, "looksrc: no install while a batch is in flight");
        s.fake.in_batch = false;
        s.N();
        Check(s.fake.look_src_installs >= 1, "looksrc: installed lazily");
        Check(s.Until(pn::Mode::active, 9000) && s.fake.arms.back().naim == mgscoop::PartnerNativeAim::looksrc,
              "looksrc: the Arm carries the aim mode");
        NativeSession o;
        AddWeaponHooks(o.hooks);
        o.fake.Realize(true);
        for (int i = 0; i < 60; ++i) o.N();
        Check(o.fake.look_src_installs == 0 && std::strcmp(o.driver.mission.tele.look_src, "off") == 0, "naim off: never installed");
    }
    {
        // The v4 sender: the pad mode (gamepad unless PP+0x488 bit 6) and the local gun equip.
        NativeSession s;
        AddWeaponHooks(s.hooks);
        s.fake.gun0_ok = true;
        s.fake.gun0.fields_ok = true;
        s.fake.gun0.fields.flags27c = 1u << 6;
        s.fake.gun0.fields.equip = 46;
        s.fake.gun0.fields.cap = 9;
        s.fake.gun0.fields.mag = 5;
        for (int i = 0; i < 5; ++i) s.N();
        Check(!s.fake.inputs.empty(), "v4 sent");
        const auto& p = s.fake.inputs.back();
        Check(p.version == 5 && (p.xflags & mgscoop::partner_input::XFlag::Equip) && p.gun_equip == 46 &&
                  (p.xflags & mgscoop::partner_input::XFlag::Gamepad), "the sender's equip and gamepad mode (bit 6 clear)");
        s.fake.pad.kb_flags = 0x40;
        s.N();
        Check(!(s.fake.inputs.back().xflags & mgscoop::partner_input::XFlag::Gamepad), "keyboard mode: no gamepad flag");
    }
}

void TestNativeWeapons() {
    namespace pn = mgscoop::partner_native;
    namespace ew = mgscoop::partner_equip_wire;
    {
        NativeSession s;
        SetVisualFire(s);
        Check(s.Until(pn::Mode::active, 9000), "active with visual fire configured");
        Check(s.fake.fire_installs >= 1 && s.fake.fire_owner == 1 && s.driver.settings.fire_live,
              "the owner filter installs lazily, armed for instance 1's id");
        Check(s.fake.Has("partner_fire=live; reason=ok; owner=1; installed=1; live=1; armed=1\r\n"), "live line");
        Check(!s.fake.arms.empty() && (s.fake.arms.back().config.mask & 0x10u) && s.fake.arms.back().mag_mirror &&
              s.fake.arms.back().gun_rec == 0x9100 && s.fake.arms.back().gun_prim == 0x9000,
              "FIRE in the native Arm's mask; the magazine mirror on instance 1's gun record");
        Check(s.driver.mission.tele.fire_effective && s.driver.mission.tele.fire_live && std::strcmp(s.driver.mission.tele.fire_mode, "visual") == 0,
              "telemetry: visual, live, effective");
        // A lost filter masks FIRE at once.
        s.fake.fire.live = false;
        s.N();
        Check(!s.driver.settings.fire_live && !(s.fake.arms.back().config.mask & 0x10u) &&
              s.fake.Has("partner_fire=masked; reason=hook_lost; owner=1; installed=1; live=0; armed=1\r\n"), "hook lost: FIRE masked");
        s.fake.fire.live = true;
        s.N();
        Check(s.driver.settings.fire_live && (s.fake.arms.back().config.mask & 0x10u), "live again");
        // Unreadable gun ticks keep the verdict (no mask flapping, no gen bump).
        const std::uint32_t gen = s.fake.arms.back().gen;
        s.fake.gun_ok = false;
        s.N();
        s.N();
        Check(s.driver.settings.fire_live && s.fake.arms.back().gen == gen && s.fake.arms.back().mag_mirror,
              "unreadable gun record: the last verdict, the Arm gen and the mirror stay");
        // Once a gun class was seen, a later other class (holster/draw) keeps the verdict and the record.
        s.fake.gun_ok = true;
        s.fake.gun.fields.flags27c = 2u << 6;
        s.N();
        Check(s.driver.settings.fire_live && (s.fake.arms.back().config.mask & 0x10u) && s.fake.arms.back().gun_rec == 0x9100 &&
              s.fake.Containing("partner_fire=masked; reason=weapon;") == 0, "the gun-class verdict latches for the mission");
    }
    {
        // Before any gun class (no weapon drawn yet): FIRE masked, no mirror.
        NativeSession s;
        SetVisualFire(s);
        s.fake.gun.fields.flags27c = 0;
        Check(s.Until(pn::Mode::active, 9000), "active before body 1 drew a gun");
        Check(!s.driver.settings.fire_live && !(s.fake.arms.back().config.mask & 0x10u) && !s.fake.arms.back().mag_mirror &&
              s.fake.Containing("partner_fire=masked; reason=weapon;") == 1 && s.fake.fire_installs == 0,
              "no gun class yet: FIRE masked, no mirror, the filter not installed");
        s.fake.gun.fields.flags27c = 1u << 6;
        s.N();
        Check(s.driver.settings.fire_live && (s.fake.arms.back().config.mask & 0x10u) && s.fake.arms.back().mag_mirror &&
              s.fake.fire_installs >= 1 && s.fake.fire_owner == 1, "the first gun class: the filter installs, FIRE live and the mirror on");
    }
    {
        // An owner id equal to the local instance's id is refused: the local player's shots are never dropped.
        NativeSession s;
        SetVisualFire(s);
        Check(s.Until(pn::Mode::active, 9000) && s.driver.settings.fire_live, "active with visual fire (owner check)");
        const unsigned installs = s.fake.fire_installs;
        s.fake.state.ids[0] = 1;
        s.N();
        Check(!s.driver.settings.fire_live && s.fake.fire_installs == installs &&
              s.fake.Containing("partner_fire=masked; reason=owner; owner=1;") == 1 && std::strcmp(s.driver.mission.tele.fire_refused, "owner") == 0,
              "owner == the local instance's id: FIRE masked, the filter never armed for it");
    }
    for (const bool native : {true, false}) {
        // With the native drive configured, a legacy input lever (a trigger's input=net while the native
        // drive is not active) never passes FIRE whatever the mask; drive=legacy keeps its research mask.
        Session s(3, mgscoop::PartnerApply::input, mgscoop::PartnerSource::net);
        s.driver.settings.input = mgscoop::PartnerInput::net;
        s.driver.settings.input_mask = 0x224005Eu;
        s.driver.settings.ngun = true;
        s.driver.settings.nfire = mgscoop::PartnerNativeFire::visual;
        if (native) s.driver.settings.drive = mgscoop::PartnerDrive::native;
        PlaceAndArm(s);
        Check(!s.driver.mission.native_out.active && s.fake.arms.back().enabled && !s.fake.arms.back().native &&
              ((s.fake.arms.back().config.mask & 0x10u) != 0) == !native && (s.fake.arms.back().config.mask & 0x48u) == 0x48u,
              native ? "drive=native, legacy lever armed: FIRE stripped (HOLD/RELOAD kept)" : "drive=legacy: the configured mask unchanged");
    }
    {
        // An install refusal keeps FIRE masked for the mission.
        NativeSession s;
        SetVisualFire(s);
        s.fake.fire_install_result = "bytes";
        Check(s.Until(pn::Mode::active, 9000), "active (movement and aim) despite the fire refusal");
        Check(!s.driver.settings.fire_live && !s.fake.arms.empty() && !(s.fake.arms.back().config.mask & 0x10u) &&
              s.fake.Containing("partner_fire=masked; reason=bytes;") == 1 && std::strcmp(s.driver.mission.tele.fire_refused, "bytes") == 0,
              "install refused: FIRE stays masked, logged once");
    }
    {
        // nfire=off never installs the filter and never passes FIRE.
        NativeSession s;
        SetVisualFire(s);
        s.driver.settings.nfire = mgscoop::PartnerNativeFire::off;
        s.driver.settings.input_mask = 0x224004Eu;
        Check(s.Until(pn::Mode::active, 9000), "active without fire");
        Check(s.fake.fire_installs == 0 && !s.driver.settings.fire_live && s.fake.Containing("partner_fire=") == 0 &&
              !(s.fake.arms.back().config.mask & 0x10u) && s.fake.arms.back().mag_mirror,
              "nfire=off: no install, no FIRE, the magazine mirror still on (reload)");
    }
    {
        // The v4 sender: the local gun's magazine (getter rule) and capacity.
        NativeSession s;
        AddWeaponHooks(s.hooks);
        s.fake.gun0_ok = true;
        s.fake.gun0.fields_ok = true;
        s.fake.gun0.fields.flags27c = 1u << 6;
        s.fake.gun0.fields.equip = 46;
        s.fake.gun0.fields.cap = 5;
        s.fake.gun0.fields.mag = 3;
        s.fake.inputs.clear();
        s.N();
        const auto& p = s.fake.inputs.back();
        Check(!s.fake.inputs.empty() && p.version == 5 && (p.xflags & mgscoop::partner_input::XFlag::Mag) && p.mag == 3 && p.mag_cap == 5,
              "v4 carries the local magazine 3/5");
        s.fake.gun0.fields.gun3c0 = 0x10000000u;
        s.N();
        Check(s.fake.inputs.back().mag == 5 && s.fake.inputs.back().mag_cap == 5, "rec+0x3C0 bit 28: the getter's full magazine");
        s.fake.gun0.fields.equip = 0;
        s.N();
        Check(!(s.fake.inputs.back().xflags & mgscoop::partner_input::XFlag::Mag) && s.fake.inputs.back().mag == 0,
              "no equip: no magazine sent");
    }
    {
        // Loadout manifest: sent on change and per link session; the receiver's apply verdict.
        NativeSession s;
        AddWeaponHooks(s.hooks);
        s.fake.loadout_ok = true;
        s.fake.loadout.ok = s.fake.loadout.tables_ok = true;
        s.fake.loadout.weapons[1] = 873;
        s.fake.loadout.base[1] = 46;
        s.fake.loadout.type[1] = 4;
        s.fake.loadout.chimera[12] = 157;
        s.fake.loadout.scan[1] = "gun_types";
        s.N();
        ew::Manifest m{};
        Check(s.fake.reliable_sent.size() == 1 && s.fake.reliable_sent[0].size() == ew::kSize &&
              ew::Decode(s.fake.reliable_sent[0].data(), s.fake.reliable_sent[0].size(), 7, m) == ew::Result::ok && m.revision == 1 &&
              m.epoch == 7 && m.slots[1].base_id == 46 && m.slots[1].alias == 873 && m.slots[1].recipe[0] == 157 && m.inv_slot == 0xFF &&
              m.version == 2 && m.slots[1].type == 4 && m.slots[0].type == 0,
              "manifest rev 1 (version 2, the slot's type) sent on channel 2");
        Check(s.fake.Has("partner_equip=sent; rev=1; epoch=7; base=0,46,0; alias=0,873,0; receiver=0,157,0; type=0,4,0; tables=1; "
                         "mission=30050\r\n") &&
                  s.fake.Has("partner_equip_base; rev=1; slot=1; alias=873; type=4; receiver=157; alias_receiver=0; base=46; scan=gun_types; "
                             "tables=1\r\n"),
              "sent line and the base-id scan line");
        s.N(1100);
        Check(s.fake.reliable_sent.size() == 1, "unchanged loadout: not resent");
        s.fake.link_session = 8;
        s.N(1100);
        Check(s.fake.reliable_sent.size() == 2 &&
              ew::Decode(s.fake.reliable_sent[1].data(), s.fake.reliable_sent[1].size(), 8, m) == ew::Result::ok && m.revision == 1 && m.epoch == 8,
              "a new link session resends the same revision");
        s.fake.loadout.weapons[1] = 875;
        s.N(1100);
        Check(s.fake.reliable_sent.size() == 3 &&
              ew::Decode(s.fake.reliable_sent[2].data(), s.fake.reliable_sent[2].size(), 8, m) == ew::Result::ok && m.revision == 2 &&
              m.slots[1].alias == 875, "a changed loadout gets revision 2");
        Check(s.fake.inputs.back().equip_rev == 2, "the v4 frame names the manifest revision");
        // The receiver's verdict for the back slot.
        s.fake.gun_ok = true;
        s.fake.gun.fields_ok = true;
        s.fake.gun.fields.equip = 46;
        ew::Manifest rx{};
        rx.revision = 4; rx.epoch = 8; rx.inv_slot = 0xFF; rx.slots[1].base_id = 46; rx.slots[1].alias = 873;
        std::uint8_t bytes[ew::kSize]{};
        Check(ew::Encode(rx, bytes) == sizeof(bytes), "rx manifest encodes");
        s.fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
        s.N();
        Check(s.fake.Has("partner_equip=apply; rev=4; slot=1; want=46; loaded=46; result=already; writer=stock46\r\n") &&
              std::strcmp(s.driver.mission.tele.equip_apply, "already") == 0, "apply: already (stock46 loaded)");
        rx.revision = 5; rx.slots[1].base_id = 40;
        Check(ew::Encode(rx, bytes) == sizeof(bytes), "rx manifest 5 encodes");
        s.fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
        s.N();
        Check(s.fake.Has("partner_equip=apply; rev=5; slot=1; want=40; loaded=46; result=unsupported; writer=stock46\r\n"),
              "apply: unsupported (only stock46 is qualified)");
        s.fake.gun.fields.equip = 0;
        s.N();
        Check(s.fake.Has("partner_equip=apply; rev=5; slot=1; want=40; loaded=0; result=no_weapon; writer=stock46\r\n"),
              "apply: re-judged when the loaded equip changes");
    }
}

// The early manifest (docs/native-puppet.md "Loadout mirroring"): the loadout
// sender and the channel 2 drain run outside partner missions (the observer's
// ~100 ms path in the title and the ACC), never read partner state, resend on
// a change or a new link session, and the first mission after connecting
// starts with the remote's manifest (MissionStart seeds the store from the
// session cache; the preload's DrainEquipLink drains only).
void TestEquipEarlyManifest() {
    namespace ew = mgscoop::partner_equip_wire;
    Fake fake;
    pd::Hooks hooks = HooksFor(fake);
    AddWeaponHooks(hooks);
    hooks.link_session = HookLinkSession;
    hooks.drain_reliable = HookDrainReliable;
    pd::Driver d{};
    mgscoop::LabConfig lab{};
    lab.partner = true;
    lab.partner_input_send = true;
    pd::Configure(d, lab);
    pd::MissionStart(d, hooks, 40010);  // the title (heli space): no partner body, no CFG
    fake.lines.clear();
    const unsigned state_reads = fake.state_reads;
    fake.loadout_ok = true;
    fake.loadout.ok = true;
    pd::TickEquipLink(d, hooks, 1000);
    Check(fake.reliable_sent.empty() && fake.loadout_reads == 1, "vars without a weapon (not loaded yet): nothing sent");
    fake.loadout.tables_ok = true;
    fake.loadout.weapons[0] = 871; fake.loadout.type[0] = 1; fake.loadout.chimera[0] = 69; fake.loadout.alias_receiver[0] = 69;
    fake.loadout.scan[0] = "no_match";
    fake.loadout.weapons[1] = 873; fake.loadout.base[1] = 46; fake.loadout.type[1] = 4; fake.loadout.chimera[12] = 157;
    fake.loadout.alias_receiver[1] = 157; fake.loadout.scan[1] = "gun_types";
    fake.loadout.weapons[2] = 9; fake.loadout.base[2] = 9; fake.loadout.type[2] = 2; fake.loadout.scan[2] = "stock";
    pd::TickEquipLink(d, hooks, 1500);
    Check(fake.reliable_sent.empty() && fake.loadout_reads == 1, "the loadout is read at most every kEquipReadMs");
    pd::TickEquipLink(d, hooks, 2100);
    ew::Manifest got{};
    Check(fake.reliable_sent.size() == 1 &&
              ew::Decode(fake.reliable_sent[0].data(), fake.reliable_sent[0].size(), 7, got) == ew::Result::ok && got.version == 2 &&
              got.slots[0].base_id == 0 && got.slots[0].alias == 871 && got.slots[0].type == 1 && got.slots[1].base_id == 46 &&
              got.slots[1].type == 4 && got.slots[2].base_id == 9 && got.slots[2].type == 2,
          "the title sends the manifest: base 0 with the alias's type 1 for 871 (never a non-gun base)");
    Check(fake.Has("partner_equip=sent; rev=1; epoch=7; base=0,46,9; alias=871,873,9; receiver=69,157,0; type=1,4,2; tables=1; "
                   "mission=40010\r\n") &&
              fake.Has("partner_equip_base; rev=1; slot=0; alias=871; type=1; receiver=69; alias_receiver=69; base=0; scan=no_match; "
                       "tables=1\r\n") &&
              fake.Containing("partner_equip_base;") == 3,
          "the sent line names the title; one base line per carried slot");
    pd::TickEquipLink(d, hooks, 3200);
    Check(fake.reliable_sent.size() == 1, "unchanged: not resent");
    fake.link_session = 9;
    pd::TickEquipLink(d, hooks, 3300);
    Check(fake.reliable_sent.size() == 2 &&
              ew::Decode(fake.reliable_sent[1].data(), fake.reliable_sent[1].size(), 9, got) == ew::Result::ok && got.revision == 1,
          "a new link session resends at once (same revision)");
    fake.link_session = 0;
    fake.loadout.weapons[2] = 10;
    pd::TickEquipLink(d, hooks, 4400);
    Check(fake.reliable_sent.size() == 2, "no link session: nothing read or sent");
    fake.link_session = 9;
    pd::TickEquipLink(d, hooks, 4500);
    Check(fake.reliable_sent.size() == 3 &&
              ew::Decode(fake.reliable_sent[2].data(), fake.reliable_sent[2].size(), 9, got) == ew::Result::ok && got.revision == 2 &&
              got.slots[2].alias == 10, "a changed loadout: revision 2");
    // The receiver side in the title: the peer's manifest is drained and cached.
    ew::Manifest rx{};
    rx.revision = 3; rx.epoch = 9; rx.inv_slot = 0xFF;
    rx.slots[0].alias = 871; rx.slots[0].type = 1; rx.slots[0].recipe[0] = 69;
    rx.slots[1].base_id = 46; rx.slots[1].alias = 873; rx.slots[1].type = 4;
    std::uint8_t bytes[ew::kSize]{};
    Check(ew::Encode(rx, bytes) == sizeof(bytes), "rx manifest encodes");
    fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
    pd::DrainEquipLink(d, hooks);
    Check(fake.channel2.empty() && d.equip_cache_have && d.equip_cache_epoch == 9 && d.equip_cache.revision == 3 &&
              d.mission.equip.have && fake.reliable_sent.size() == 3 &&
              fake.Has("partner_equip=manifest; rev=3; inv=255; slots=0:871,46:873,0:0; type=1,4,0; v=2; mission=40010\r\n"),
          "DrainEquipLink: drained in the title, cached, nothing sent");
    Check(fake.state_reads == state_reads, "the early manifest never reads partner state");
    // The first partner mission after connecting starts with it (no resend needed).
    pd::MissionEnd(d, hooks);
    pd::MissionStart(d, hooks, 30010);
    Check(d.mission.equip.have && d.mission.equip.manifest.revision == 3 && d.mission.equip.manifest.slots[0].type == 1 &&
              d.mission.equip_logged_rev == 3, "MissionStart seeds the store from the session cache");
    fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
    pd::TickEquipLink(d, hooks, 5600);
    Check(d.mission.equip.duplicates == 1 && fake.Containing("partner_equip=manifest; rev=3;") == 1, "the same revision again: a duplicate");
    // A 0.12.0 sender restarts at revision 1 every mission (version 1, 60 bytes): the seeded store must take its
    // next mission's loadout under the same revision number (the slot types are recomputed), never drop it.
    {
        ew::Manifest old = rx;
        old.revision = 1;
        old.slots[0].type = old.slots[1].type = 0;
        std::uint8_t v2[ew::kSize]{};
        Check(ew::Encode(old, v2) == sizeof(v2), "old manifest encodes");
        std::vector<std::uint8_t> v1(v2, v2 + ew::kSizeV1);
        v1[1] = 1;
        fake.channel2.push_back(v1);
        pd::TickEquipLink(d, hooks, 5700);
        Check(d.mission.equip.manifest.revision == 1 && d.mission.equip.manifest.version == 1 && d.equip_cache.revision == 1,
              "a 0.12.0 sender's revision 1: stored and cached");
        d.mission.slot_types_rev = 1;  // as if TickSlots had typed revision 1
        v1[12 + 16] = 47;              // its next mission: another back-slot base id, revision 1 again
        fake.channel2.push_back(v1);
        pd::TickEquipLink(d, hooks, 5800);
        Check(d.mission.equip.manifest.slots[1].base_id == 47 && d.equip_cache.slots[1].base_id == 47 && d.mission.slot_types_rev == 0 &&
                  fake.Containing("partner_equip=manifest; rev=1; inv=255; slots=0:871,47:873,0:0; type=0,0,0; v=1;") == 1,
              "revision 1 again with another loadout: replaced, logged, slot types recomputed");
        fake.channel2.push_back(v1);
        pd::TickEquipLink(d, hooks, 5900);
        Check(d.mission.equip.duplicates == 2 && fake.Containing("partner_equip=manifest; rev=1;") == 2, "its exact repeat: a duplicate");
    }
    // Another link session: never seeded.
    fake.link_session = 10;
    pd::MissionStart(d, hooks, 30010);
    Check(!d.mission.equip.have, "another link session's manifest is not seeded");
    // The sender has its own line budget: a title after a long mission (its weapon lines spent) still logs.
    d.mission.weapon_cap.lines = pd::kNativeEventCap;
    fake.loadout.weapons[2] = 11;
    {
        const std::size_t before = fake.reliable_sent.size();
        pd::TickEquipLink(d, hooks, 7000);
        Check(fake.reliable_sent.size() == before + 1 && fake.Containing("partner_equip=sent; rev=3; epoch=10;") == 1 &&
                  fake.Containing("partner_equip_base; rev=3;") == 3 && fake.Containing("partner_weapon=capped") == 0,
              "a spent mission weapon cap never hides the sender's lines");
    }
    // A copy that neither drives natively nor sends input frames sends nothing.
    pd::Driver quiet{};
    mgscoop::LabConfig off{};
    off.partner = true;
    pd::Configure(quiet, off);
    const std::size_t sent = fake.reliable_sent.size();
    pd::TickEquipLink(quiet, hooks, 9000);
    Check(fake.reliable_sent.size() == sent, "no native drive and no input sender: no manifest");
}

// Review fixes: play mode never takes the native drive from the trigger file;
// the GO identity and census wrap roots never outlive a mission or the drive.
// The partner's own reload and weapon switch (docs/native-puppet.md "Weapons"):
// the v5 sender, the event machine's taps and the Arm's freeze inputs, the
// generalized gate (switching, projectile), the slot plan and follower lines.
void TestNativeReloadSwitch() {
    namespace pn = mgscoop::partner_native;
    namespace pi = mgscoop::partner_input;
    namespace ew = mgscoop::partner_equip_wire;
    NativeSession s;
    SetVisualFire(s);
    s.driver.settings.nreload = mgscoop::PartnerNativeReload::event;
    s.driver.settings.nslot = mgscoop::PartnerNativeSlot::write;
    s.fake.gun.fields.equip = 46;
    s.fake.gun.fields.cap = 9;
    s.fake.gun.fields.mag = 3;
    s.fake.gun.fields.stock = 36;
    s.fake.gun.mode_cur = 7;
    s.fake.gun.hand_slot = 1;
    s.fake.gun.slot_src = 7;
    s.fake.equip_types[46] = 4;
    // The local player (the v5 sender): stock46 aimed in the back slot.
    s.fake.gun0_ok = true;
    s.fake.gun0.fields_ok = true;
    s.fake.gun0.fields.flags27c = 1u << 6;
    s.fake.gun0.fields.equip = 46;
    s.fake.gun0.fields.cap = 9;
    s.fake.gun0.fields.mag = 9;
    s.fake.gun0.gun3c0 = 0x40004u;  // in hand, aiming
    s.fake.gun0.mode_cur = 7;
    s.fake.slots_ok[0] = true;
    s.fake.slots[0].nibble = 1;
    // Body 1: stock46 in the back slot, nothing else.
    s.fake.slots_ok[1] = true;
    s.fake.slots[1].nibble = 1;
    s.fake.slots[1].handles[0] = 0xFFFF0000u;
    s.fake.slots[1].handles[1] = (4u << 16) | (46u << 5) | 4u;
    s.fake.slots[1].handles[2] = 0u;  // the runtime shape of a slot body 1 never had (empty, like 0xFFFF0000)
    s.fake.latest.packet.version = 5;
    s.fake.latest.packet.xflags = pi::XFlag::Equip;
    s.fake.latest.packet.gun_equip = 46;
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming;
    s.fake.latest.packet.gun_mode = 7;
    s.fake.latest.packet.reload_seq = 4;
    s.fake.latest.packet.inv_slot = 1;
    Check(s.Until(pn::Mode::active, 9000) && s.driver.settings.fire_live, "reload/switch: active with visual fire");
    s.N();
    // The v5 sender: the gun group and the current slot.
    s.fake.inputs.clear();
    s.N();
    Check(!s.fake.inputs.empty() && s.fake.inputs.back().version == 5 &&
              s.fake.inputs.back().gun_flags == (pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming) &&
              s.fake.inputs.back().gun_mode == 7 && s.fake.inputs.back().reload_seq == 0 && s.fake.inputs.back().inv_slot == 1,
          "v5 sender: in hand, aiming, mode 7, slot 1");
    s.fake.gun0.mode_cur = 11;
    s.N();
    Check(s.fake.inputs.back().reload_seq == 1 && (s.fake.inputs.back().gun_flags & pi::GunFlag::Reloading) && s.fake.inputs.back().gun_mode == 11,
          "v5 sender: entering 11 advances reload_seq");
    s.fake.gun0.mode_cur = 7;
    s.fake.slots[0].nibble = 2;
    s.N();
    Check(s.fake.inputs.back().switch_seq == 1 && s.fake.inputs.back().inv_slot == 2, "v5 sender: a slot change advances switch_seq");
    // The Arm: event mode, the remote's raw RELOAD stripped from its sample (live v5), the plan's slots.
    const pi::Arm& a0 = s.fake.arms.back();
    Check(a0.reload == mgscoop::PartnerNativeReload::event && !a0.reload_hold && a0.config.sample_strip == 0x40u &&
              a0.slot_follow == mgscoop::PartnerNativeSlot::write && a0.slot_ok == 0 && a0.strip == 0,
          "Arm: reload event, sample strip RELOAD, follower write (no manifest yet: no admitted slot)");
    // The receiver: the remote enters its reload (counter 4 -> 5, mode 11): one RELOAD tap.
    const std::uint32_t taps = s.fake.arms.back().tap_seq;
    s.fake.latest.packet.reload_seq = 5;
    s.fake.latest.packet.gun_mode = 11;
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming | pi::GunFlag::Reloading;
    s.N();
    Check(s.fake.arms.back().tap_seq == taps + 1 && s.fake.arms.back().tap_bits == 0x40u && s.fake.arms.back().reload_hold &&
              s.fake.Containing("partner_reload=tap; setting=event; state=pending; body_mode=7; remote_mode=11; seq=5; mag=3;") == 1 &&
              s.driver.mission.tele.reload_taps == 1 && std::strcmp(s.driver.mission.tele.reload_state, "pending") == 0,
          "remote reload start: one tap, the mirror held");
    s.fake.gun.mode_cur = 12;
    s.N();
    Check(s.fake.Containing("partner_reload=body_enter; setting=event; body_mode=12; mag=3; cap=9;") == 1 &&
              s.fake.Containing("partner_reload=entered;") == 1 && s.fake.arms.back().reload_hold && s.fake.arms.back().tap_seq == taps + 1 &&
              s.driver.mission.tele.reload_entered == 1 && s.driver.mission.tele.gun_mode_cur == 12,
          "body 1 enters its own reload mode: no retry");
    s.fake.latest.packet.gun_mode = 7;
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming;
    s.N();
    s.fake.gun.mode_cur = 7;
    s.N();
    Check(s.fake.Containing("partner_reload=complete;") == 1 && !s.fake.arms.back().reload_hold && s.driver.mission.tele.reload_completed == 1,
          "complete: the hold released (the hook reconciles)");
    Check(s.fake.Containing("partner_reload=complete; setting=event; len_ms=") == 1 &&
              s.fake.Containing("; completed=1; entered=1; late=0; queued_tap=0\r\n") == 1 && s.driver.mission.tele.reload_len_ms > 0 &&
              s.driver.mission.tele.reload_len_max_ms == s.driver.mission.tele.reload_len_ms && !s.fake.arms.back().reload_reconcile,
          "complete line: len_ms, remote_len_ms, refill_ms, counters");
    // rtap=1: one test tap; refused when the weapon gate strips RELOAD.
    s.Trigger("rtap=1");
    s.N();
    Check(s.fake.Has("partner_reload=rtap; queued=1\r\n") && s.fake.arms.back().tap_seq == taps + 2, "rtap=1: a test tap");
    for (int i = 0; i < 30; ++i) s.N();  // its retries run out (no reload mode)
    Check(s.fake.Containing("partner_reload=fallback;") >= 1 && !s.fake.arms.back().reload_hold, "rtap without a reload: timeout fallback");
    // F1: a lost exit (body 1 enters 11 and its exit never arrives): the hold lasts through the stuck report until
    // release_ms; then the Arm carries the release (the hook stops waiting for body 1's mode), the machine leaves
    // entered without a completion, and body 1's later exit clears the latch.
    s.fake.latest.packet.reload_seq = 6;
    s.fake.latest.packet.gun_mode = 11;
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming | pi::GunFlag::Reloading;
    s.N();
    s.fake.gun.mode_cur = 11;
    s.N();
    Check(s.fake.arms.back().reload_hold && !s.fake.arms.back().reload_release && std::strcmp(s.driver.mission.tele.reload_state, "entered") == 0,
          "lost exit: entered and held");
    s.fake.latest.packet.gun_mode = 7;
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming;
    {
        const std::uint32_t completed = s.driver.mission.tele.reload_completed;
        int steps = 0;
        for (; steps < 600 && !s.fake.arms.back().reload_release; ++steps) s.N(50);
        Check(steps > 300 && s.fake.arms.back().reload_release && !s.fake.arms.back().reload_hold &&
                  s.fake.Containing("partner_reload=stuck;") == 1 && s.fake.Containing("partner_reload=release; setting=event; state=idle;") == 1 &&
                  s.driver.mission.tele.reload_release && s.driver.mission.tele.reload_releases == 1 &&
                  s.driver.mission.tele.reload_completed == completed,
              "lost exit: held past the stuck report, released after release_ms (no completion counted)");
        s.fake.gun.mode_cur = 7;
        s.N();
        Check(!s.fake.arms.back().reload_release && !s.driver.mission.tele.reload_release && s.driver.mission.tele.reload_completed == completed,
              "body 1's late exit clears the release");
    }
    // The generalized gate: mid-switch strips FIRE and RELOAD (no gate line), an unqualified type FIRE only.
    s.fake.gun.gun3c0 |= 0x80u;
    s.N();
    Check(s.fake.arms.back().strip == 0x50u && std::strcmp(s.driver.mission.tele.weapon_gate, "switching") == 0 &&
              s.driver.mission.tele.body_switching && s.fake.Containing("partner_weapon=switching") == 0,
          "mid-switch: FIRE|RELOAD stripped, not logged");
    s.fake.gun.gun3c0 &= ~0x80u;
    s.fake.gun.fields.equip = 100;
    s.fake.latest.packet.gun_equip = 100;
    s.fake.equip_types[100] = 9;
    s.N();
    Check(s.fake.arms.back().strip == 0x10u && std::strcmp(s.driver.mission.tele.weapon_gate, "projectile") == 0 &&
              s.fake.Has("partner_weapon=projectile; body_equip=100; body_type=9; strip=0x10\r\n") &&
              std::strcmp(s.driver.mission.tele.fire_refused, "weapon_type") == 0,
          "unqualified type: FIRE stripped, RELOAD kept");
    s.fake.gun.fields.equip = 46;
    s.fake.latest.packet.gun_equip = 46;
    s.N();
    Check(s.fake.arms.back().strip == 0 && s.fake.Containing("partner_weapon=type_ok;") == 1, "back to stock46: nothing stripped");
    // The slot plan from the received manifest: back slot same, the others empty.
    ew::Manifest rx{};
    rx.revision = 1; rx.epoch = 7; rx.inv_slot = 0xFF;
    rx.slots[0].base_id = 343; rx.slots[1].base_id = 46; rx.slots[2].base_id = 9;
    std::uint8_t bytes[ew::kSize]{};
    Check(ew::Encode(rx, bytes) == sizeof(bytes), "the remote's manifest encodes");
    s.fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
    s.N();
    s.N();
    Check(s.fake.arms.back().slot_ok == 0x2u && s.fake.arms.back().slot_equip[1] == 46 && s.fake.arms.back().slot_equip[0] == 0 &&
              s.fake.Containing("partner_slot=plan; follow=write; body_slot=1; tiers=empty,same,empty; body=0:0,46:4,0:0; "
                                "remote_base=343,46,9; ok=0x2") == 1 &&
              std::strcmp(s.driver.mission.tele.slot_tier[1], "same") == 0 && s.driver.mission.tele.slot_ok_mask == 2,
          "plan: stock46 in the back slot is admitted, the empty slots are not");
    // The follower's verdicts come from the hook's totals.
    s.fake.stats.native.slot_refused = 1;
    s.fake.stats.native.slot_last = mgscoop::partner_input_hook::SlotRefusal::empty;
    s.fake.stats.native.slot_target = 2;
    s.N();
    Check(s.fake.Containing("partner_slot=refused; reason=empty; target=2;") == 1 &&
              std::strcmp(s.driver.mission.tele.slot_last, "empty") == 0 && s.driver.mission.tele.slot_refused == 1,
          "a follower refusal is logged once");
    s.fake.stats.native.slot_writes = 1;
    s.fake.stats.native.slot_last = mgscoop::partner_input_hook::SlotRefusal::none;
    s.N();
    Check(s.fake.Containing("partner_slot=write; reason=none;") == 1 && s.driver.mission.tele.slot_writes == 1, "a follower write is logged");
    // W5 with a version 2 manifest (run 53e36944's hip slot): the remote holds alias 871 (type 1, no stock base) in its
    // hip slot; body 1's hip slot holds the tier C substitute 36 (type 1), in hand: a substitute in the remote's current
    // slot is a match (the old base_id == remote base rule masked it as a mismatch).
    rx.revision = 2; rx.inv_slot = 0;
    rx.slots[0].base_id = 0; rx.slots[0].alias = 871; rx.slots[0].type = 1;
    rx.slots[1].type = 4;
    Check(ew::Encode(rx, bytes) == sizeof(bytes), "the remote's manifest 2 encodes");
    s.fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
    s.fake.slots[1].handles[0] = (5u << 16) | (36u << 5) | 1u;
    s.fake.equip_types[36] = 1;
    s.fake.gun.fields.equip = 36;
    s.fake.latest.packet.gun_equip = 871;
    s.fake.latest.packet.inv_slot = 0;
    s.N();
    s.N();
    Check(s.driver.mission.slot_plan.tier[0] == mgscoop::partner_slots::Tier::sub && s.driver.mission.slot_plan.equip[0] == 36 &&
              s.driver.mission.slot_types[0] == 1 && std::strcmp(s.driver.mission.tele.weapon_gate, "match") == 0 &&
              (s.fake.arms.back().strip & 0x50u) == 0 && s.fake.arms.back().slot_ok == 0x3u,
          "version 2: base 0 with type 1, the substitute 36 in the remote's current slot: sub and match");
    // The same body weapon while the remote holds its back slot (alias 873): mismatch.
    s.fake.latest.packet.gun_equip = 873;
    s.fake.latest.packet.inv_slot = 1;
    s.N();
    Check(std::strcmp(s.driver.mission.tele.weapon_gate, "mismatch") == 0 && (s.fake.arms.back().strip & 0x50u) == 0x50u,
          "a substitute of another slot is no match");
    s.fake.slots[1].handles[0] = 0xFFFF0000u;
    s.fake.gun.fields.equip = 46;
    s.fake.latest.packet.gun_equip = 46;
    s.N();
    // Weapons4: an empty hip slot follows the admitted back-slot stand-in;
    // exact slot loading is independent, and the magazine mirror stays live.
    s.driver.settings.nstandin=mgscoop::PartnerNativeStandin::on;
    s.fake.latest.packet.gun_equip=871;s.fake.latest.packet.inv_slot=0;
    s.N();s.N();
    Check(s.driver.mission.slot_plan.map[0]==1 && s.driver.mission.tele.weapon_standin &&
              std::strcmp(s.driver.mission.tele.weapon_gate,"standin")==0 && (s.fake.arms.back().strip&0x50u)==0 &&
              (s.fake.arms.back().standin_mask&1u) && s.fake.arms.back().standin_slot[0]==1 && s.fake.arms.back().mag_mirror,
          "stand-in maps remote hip to body back, admits combat and mirrors the body's magazine");
    Check(s.driver.mission.tele.slot_map[0]==1 && s.driver.mission.tele.weapon_standin_ms>0,
          "stand-in mapping and measured duration reach telemetry");
    s.driver.settings.nstandin=mgscoop::PartnerNativeStandin::off;
    s.N();s.N();
    Check(!s.driver.mission.tele.weapon_standin && !(s.fake.arms.back().standin_mask&1u) &&
              (s.fake.arms.back().strip&0x50u)==0x50u,"stand-in off restores mismatch protection");
    s.fake.latest.packet.gun_equip=46;s.fake.latest.packet.inv_slot=1;s.N();
    // Play runs call as write; mirror never holds and never strips the sample.
    s.driver.settings.nslot = mgscoop::PartnerNativeSlot::call;
    s.driver.settings.play_mode = true;
    s.driver.settings.nreload = mgscoop::PartnerNativeReload::mirror;
    s.N();
    Check(s.fake.arms.back().slot_follow == mgscoop::PartnerNativeSlot::write && s.fake.arms.back().reload == mgscoop::PartnerNativeReload::mirror &&
              s.fake.arms.back().config.sample_strip == 0, "play: call runs as write; mirror: no sample strip");
    s.driver.settings.play_mode = false;
    s.N();
    Check(s.fake.arms.back().slot_follow == mgscoop::PartnerNativeSlot::call, "lab: the call variant reaches the Arm");
    // A v4 remote (no gun group): no sample strip even with event.
    s.driver.settings.nreload = mgscoop::PartnerNativeReload::event;
    s.fake.latest.packet.version = 4;
    s.fake.latest.packet.gun_flags = 0;
    s.fake.latest.packet.gun_mode = 0;
    s.fake.latest.packet.reload_seq = 0;
    s.fake.latest.packet.inv_slot = 0xFF;
    s.N();
    Check(s.fake.arms.back().config.sample_strip == 0 && !s.driver.mission.tele.reload_remote_valid,
          "a v4 remote: its raw R still reaches body 1");
    // The ring carries body 1's gun mode and hand slot.
    const auto& e = pn::At(s.driver.mission.tele.ring, s.driver.mission.tele.ring.count - 1);
    Check(e.gmode == 7 && e.hslot == 1 && e.rmode == 0xFF, "ring: gmode, rmode (no v5: unread), hslot");
    // Input frame v5 negotiation: a peer build that never sent v5 (it would refuse every 116-byte frame) gets v4
    // frames and one v5 probe per kInputV5ProbeMs; once its v5 arrives, every frame is v5 again.
    s.fake.peer_v5 = false;
    s.fake.inputs.clear();
    for (int i = 0; i < 61; ++i) s.N();  // ~2 s
    unsigned v4 = 0, v5 = 0, other = 0;
    for (const auto& p : s.fake.inputs) (p.version == 4 ? v4 : p.version == 5 ? v5 : other)++;
    Check(other == 0 && v4 > 50 && v5 >= 1 && v5 <= 2 && s.driver.mission.v5_probes >= 1,
          "an older peer: v4 frames plus a v5 probe every 2 s");
    s.fake.peer_v5 = true;
    s.fake.inputs.clear();
    for (int i = 0; i < 5; ++i) s.N();
    bool all5 = !s.fake.inputs.empty();
    for (const auto& p : s.fake.inputs) all5 = all5 && p.version == 5;
    Check(all5 && s.fake.Containing("partner_input=v5_peer; probes=") == 1, "the peer's v5 seen: v5 frames, logged once");
}

void TestNativePlayAndWithdraw() {
    namespace pn = mgscoop::partner_native;
    {
        pd::Driver d{};
        mgscoop::LabConfig lab{};
        lab.mode = mgscoop::LabMode::play;
        lab.partner_native_slot = mgscoop::PartnerNativeSlot::call;
        pd::Configure(d, lab);
        Check(d.settings.play_mode, "Configure: mode=play is remembered");
        Check(d.settings.nslot == mgscoop::PartnerNativeSlot::write, "Configure: play runs partner_native_slot=call as write");
        lab.mode = mgscoop::LabMode::lab;
        pd::Configure(d, lab);
        Check(!d.settings.play_mode, "Configure: lab mode");
        Check(d.settings.nslot == mgscoop::PartnerNativeSlot::call, "Configure: the lab keeps the call variant");
    }
    {
        MapSession s;
        AddNativeHooks(s.hooks);
        s.driver.settings.play_mode = true;
        s.Trigger("drive=native defer=on steer=10");
        Check(s.driver.settings.drive == mgscoop::PartnerDrive::legacy && s.driver.settings.defer == mgscoop::PartnerDefer::off &&
              s.driver.settings.steer_deg == 10 &&
              s.fake.Has("partner_native=trigger_refused; reason=play_profile; drive=legacy\r\n") &&
              s.fake.Has("partner_native=trigger_refused; reason=play; key=defer\r\n"),
              "play mode without a profile: drive=native / defer=on refused, other keys applied");
        s.Run(3000);
        Check(s.fake.defer_installs == 0 && !s.fake.identity_valid && s.fake.roots_published == 0 &&
              s.driver.mission.native_out.mode == pn::Mode::off, "play mode: nothing native installed or published");
        // Online play: inside the profile lab.ini admitted the trigger toggles the drive (never the deferral).
        s.driver.settings.nprofile = mgscoop::PartnerNativeProfile::gun;
        s.driver.settings.defer = mgscoop::PartnerDefer::on;
        s.fake.lines.clear();
        s.Trigger("drive=native");
        Check(s.driver.settings.drive == mgscoop::PartnerDrive::native && !s.fake.Containing("trigger_refused"),
              "play with a profile: drive=native applies");
        s.Trigger("drive=legacy defer=census");
        Check(s.driver.settings.drive == mgscoop::PartnerDrive::legacy && s.driver.settings.defer == mgscoop::PartnerDefer::on &&
              s.fake.Has("partner_native=trigger_refused; reason=play; key=defer\r\n"), "play: clips by trigger, the deferral untouched");
        // The lab-only native experiments lab.ini refuses in play stay refused by trigger.
        s.driver.settings.local = mgscoop::PartnerLocal::main;
        s.driver.settings.naim = mgscoop::PartnerNativeAim::look;
        s.fake.lines.clear();
        s.Trigger("local=all naim=lookcam");
        Check(s.driver.settings.local == mgscoop::PartnerLocal::main && s.driver.settings.naim == mgscoop::PartnerNativeAim::look &&
                  s.fake.Has("partner_native=trigger_refused; reason=play; key=local\r\n") &&
                  s.fake.Has("partner_native=trigger_refused; reason=play; key=naim\r\n"),
              "play: local=all / naim=lookcam refused by trigger");
        s.Trigger("naim=off nfire=off ngun=0");
        Check(s.driver.settings.naim == mgscoop::PartnerNativeAim::off && s.driver.settings.nfire == mgscoop::PartnerNativeFire::off &&
                  !s.driver.settings.ngun && !s.fake.Containing("key=ngun") && !s.fake.Containing("key=nfire"),
              "play: the trigger may narrow the gun profile");
        // A loco profile: the gun keys never widen it.
        s.driver.settings.nprofile = mgscoop::PartnerNativeProfile::loco;
        s.Trigger("ngun=1 nfire=visual");
        Check(!s.driver.settings.ngun && s.driver.settings.nfire == mgscoop::PartnerNativeFire::off &&
                  s.fake.Has("partner_native=trigger_refused; reason=play_profile; key=ngun\r\n") &&
                  s.fake.Has("partner_native=trigger_refused; reason=play_profile; key=nfire\r\n"),
              "play loco profile: ngun=1 / nfire=visual refused by trigger");
        s.driver.settings.nprofile = mgscoop::PartnerNativeProfile::gun;
        s.driver.settings.play_mode = false;
        s.Trigger("drive=native defer=census");
        Check(s.driver.settings.drive == mgscoop::PartnerDrive::native && s.driver.settings.defer == mgscoop::PartnerDefer::census,
              "lab mode: the same trigger applies");
    }
    {
        // Configure copies the online keys.
        pd::Driver d{};
        mgscoop::LabConfig lab{};
        lab.partner_native_profile = mgscoop::PartnerNativeProfile::gun;
        lab.partner_native_net = mgscoop::PartnerNativeNet::online;
        lab.partner_native_stall = mgscoop::PartnerNativeStall::hold;
        lab.partner_native_stall_ms = 350;
        lab.partner_native_hold = mgscoop::PartnerNativeHold::net;
        lab.partner_input_edge_repeats = 5;
        pd::Configure(d, lab);
        Check(d.settings.nprofile == mgscoop::PartnerNativeProfile::gun && d.settings.nnet == mgscoop::PartnerNativeNet::online &&
              d.settings.nstall == mgscoop::PartnerNativeStall::hold && d.settings.stall_ms == 350 &&
              d.settings.nhold == mgscoop::PartnerNativeHold::net && d.settings.edge_repeats == 5, "Configure: online keys");
    }
    {
        NativeSession s(mgscoop::PartnerDefer::census);
        for (int i = 0; i < 30; ++i) s.N();
        Check(s.fake.identity_valid && s.fake.roots_last[1] == 0x6100 && s.fake.roots_census, "census: identity and roots published");
        pd::MissionEnd(s.driver, s.hooks);
        Check(!s.fake.identity_valid && s.fake.roots_last[0] == 0 && s.fake.roots_last[1] == 0 && !s.fake.roots_census &&
              !s.driver.mission.native_identity && !s.driver.mission.native_roots, "MissionEnd withdraws the identity and the roots");
    }
    {
        NativeSession s;
        for (int i = 0; i < 30; ++i) s.N();
        Check(s.fake.identity_valid, "identity published");
        const std::size_t order = s.fake.native_order.size();
        pd::MissionStart(s.driver, s.hooks, 30010);
        const int def0 = s.OrderIndex("defer:0", static_cast<int>(order));
        const int clear = s.OrderIndex("identity_clear", static_cast<int>(order));
        Check(!s.fake.identity_valid && s.fake.roots_last[1] == 0 && def0 >= 0 && clear > def0,
              "MissionStart: deferral disarmed, then the identity and roots withdrawn");
    }
    {
        NativeSession s(mgscoop::PartnerDefer::census);
        for (int i = 0; i < 30; ++i) s.N();
        s.Trigger("drive=legacy");
        s.N();
        Check(!s.fake.identity_valid && s.fake.roots_last[1] == 0 && !s.fake.roots_census &&
              s.driver.mission.native_out.mode == pn::Mode::off, "drive=legacy: identity and roots withdrawn");
    }
}

// Native actions and weapons together (the integrated gun stage of
// Get-WorkerPartnerSettings: mask 0x224005E, visual fire, rates mirror): one
// v4 carries the local rates and magazine; one remote v4 with both groups
// gives one native Arm with EVADE, HOLD, RELOAD and FIRE, the rates mirror and
// the magazine mirror, and both telemetry groups.
void TestNativeActionsWithWeapons() {
    namespace pn = mgscoop::partner_native;
    namespace pi = mgscoop::partner_input;
    NativeSession s;
    SetVisualFire(s);
    s.driver.settings.nrates = mgscoop::PartnerNativeRates::mirror;
    const auto record = [](std::uintptr_t sp, std::uintptr_t rec, std::uint32_t id, float dash, float crawl, float factor) {
        pp::RateRead r{};
        r.sp = sp; r.rec = rec; r.id = id;
        r.f[0] = crawl; r.f[1] = factor; r.f[2] = crawl * factor; r.f[3] = dash; r.f[4] = 1.0f; r.f[5] = dash;
        return r;
    };
    s.fake.rates[0] = record(0xA000, 0xA100, 0, 1.2f, 1.0f, 1.2f);
    s.fake.rates[1] = record(0xA000, 0xA188, 1, 1.0f, 1.0f, 1.0f);
    s.fake.rates_ok[0] = s.fake.rates_ok[1] = true;
    s.fake.gun0_ok = true;
    s.fake.gun0.fields_ok = true;
    s.fake.gun0.fields.flags27c = 1u << 6;
    s.fake.gun0.fields.equip = 46;
    s.fake.gun0.fields.cap = 5;
    s.fake.gun0.fields.mag = 3;
    s.fake.inputs.clear();
    s.N();
    {
        const pi::InputPacket& p = s.fake.inputs.back();
        Check(!s.fake.inputs.empty() && p.version == 5 && (p.xflags & pi::XFlag::Rates) && (p.xflags & pi::XFlag::Mag) &&
                  p.rate_dash == 1200 && p.rate_crawl == 1000 && p.rate_crawl_factor == 1200 && p.mag == 3 && p.mag_cap == 5 &&
                  pi::ValidPacket(p),
              "together: one v4 carries the local rates and magazine");
    }
    Check(s.Until(pn::Mode::active, 9000) && s.driver.settings.fire_live, "together: active with visual fire live");
    s.fake.latest.packet.version = 4;
    s.fake.latest.packet.xflags = static_cast<std::uint8_t>(pi::XFlag::Rates | pi::XFlag::Mag);
    s.fake.latest.packet.rate_dash = 1100;
    s.fake.latest.packet.rate_crawl = 900;
    s.fake.latest.packet.rate_crawl_factor = 1500;
    s.fake.latest.packet.mag = 20;
    s.fake.latest.packet.mag_cap = 30;
    s.N();
    const pi::Arm& arm = s.fake.arms.back();
    Check(arm.enabled && arm.native && arm.config.mask == 0x224005Eu, "together: the native Arm's mask is EVADE | HOLD | RELOAD | FIRE (0x224005E)");
    Check(arm.rates_mirror && arm.rates_rec == 0xA188 && arm.rates_id == 1 && arm.rates_remote[0] == 1100 && arm.rates_remote[1] == 900 &&
              arm.rates_remote[2] == 1500 && arm.mag_mirror && arm.gun_rec == 0x9100 && arm.gun_prim == 0x9000,
          "together: one Arm requests the rates mirror and the magazine mirror");
    const pn::Telemetry& t = s.driver.mission.tele;
    Check(t.rates_remote_ok && t.rates_remote[0] == 1100 && t.rates_id0_ok && t.rates_id1_ok && t.remote_mag == 20 && t.remote_mag_cap == 30 &&
              t.local_mag == 3 && t.local_mag_cap == 5 && t.fire_effective && std::strcmp(t.fire_mode, "visual") == 0,
          "together: telemetry carries both remote groups, the local magazine and visual fire");
}

void TestNativePoseAnimationSuppression() {
    Session s(3);s.driver.settings.anim=mgscoop::PartnerAnim::mirror;s.driver.settings.aset=mgscoop::PartnerAset::qs;
    s.fake.anim_ok=true;s.fake.anim.act[0]=4;s.fake.anim.stance[0]=2;s.fake.anim.speed[0]=2;Loaded(s);
    s.hooks.native_pose_suppressed=[](void* context) noexcept {return F(context).native_pose_held;};s.fake.native_pose_held=true;
    Check(s.Tick(1)&&s.Tick(1,200)&&s.fake.Calls("anim:")==0&&s.fake.Calls("stance:")==0&&s.fake.Calls("req:")==0,
          "retained native pose prevents normal Q animation and both stance paths");
    Check(s.fake.submits>0,"retained native pose preserves local sender and regular receipt flow");
    s.fake.native_pose_held=false;Check(s.Tick(1)&&s.Tick(1,200)&&s.fake.Calls("anim:")>0&&s.fake.Calls("stance:2")==1,
          "normal animation and stance resume only after native pose holder closes");
    Session dm(3);dm.driver.settings.dm=mgscoop::PartnerDm::on;Loaded(dm);
    dm.hooks.native_pose_suppressed=[](void* context) noexcept {return F(context).native_pose_held;};dm.fake.native_pose_held=true;
    dm.fake.dm.q18[0]=1;dm.fake.dm.q18[1]=1;dm.fake.dm.cam_ok=true;dm.Trigger("dmplay=3");
    Check(dm.Tick(1)&&!(dm.reply.op&(op::dm_play|op::dm_stop))&&dm.fake.dm_patch_calls==0&&dm.fake.dm_clear_calls==0,
          "retained native pose skips every normal DM clip patch/play/stop/clear path");
    dm.fake.native_pose_held=false;Check(dm.Tick(1)&&dm.fake.dm_patch_calls==1&&(dm.reply.op&op::dm_play),
          "ordinary DM path is restored after native holder retirement");
}

// --- The seated vehicle rider (partner_native_ride=seat, docs/anim-round1-seat.md).
bool HookRideSeatArm(void* context, std::uint8_t seat) noexcept {
    Fake& f = F(context);
    ++f.seat_arms;
    if (!f.seat_arm_ok) return false;
    f.seat_armed = seat;
    f.native_order.push_back("seat_arm");
    return true;
}
void HookRideSeatRevert(void* context, const char* reason) noexcept {
    Fake& f = F(context);
    ++f.seat_reverts;
    if (f.seat_armed >= 0) f.seat_revert_reason = reason ? reason : "";
    f.seat_armed = -1;
}
struct SeatSession : Session {
    std::uint16_t ride_rev = 0;
    bool ride_sent = false;
    mgscoop::partner_ride_wire::Kind ride_kind{};
    std::uint8_t ride_seat = 0xFF;
    std::uint16_t ride_vehicle = 0xFFFF;
    explicit SeatSession(mgscoop::PartnerRideSeatExit exit = mgscoop::PartnerRideSeatExit::stop,
                         mgscoop::PartnerRideSeatEnter enter = mgscoop::PartnerRideSeatEnter::direct,
                         mgscoop::PartnerNativeRide ride = mgscoop::PartnerNativeRide::seat, std::uint32_t mission = 30010)
        : Session(3, mgscoop::PartnerApply::xform, mgscoop::PartnerSource::net, mission) {
        AddNativeHooks(hooks);
        hooks.send_reliable_append = HookSendReliable;
        hooks.ride_seat_arm = HookRideSeatArm;
        hooks.ride_seat_revert = HookRideSeatRevert;
        lab.partner_native_ride = ride;
        driver.settings.nride = ride;
        driver.settings.seat_exit = exit;
        driver.settings.seat_enter = enter;
        pd::MissionStart(driver, hooks, mission);
        Check(pd::Cfg(driver, hooks, nullptr, now), "seat CFG after mission reset");
        auto& s = fake.sample;
        s.valid = s.time_valid = s.anim_valid = true; s.status = mgscoop::pose::Status::live;
        s.x = 1.0f; s.y = 2.0f; s.z = 4.0f; s.act = 1; s.sender_ms = 1000; s.generation = 1;
        fake.body_ok = true; fake.body_pos[0] = 1.0f; fake.body_pos[1] = 2.0f; fake.body_pos[2] = 4.0f;
        fake.lines.clear();
    }
    void Ride(mgscoop::partner_ride_wire::Kind kind, std::uint8_t seat = 0xFF, std::uint16_t vehicle = 0xFFFF, bool bump = true) {
        namespace rw = mgscoop::partner_ride_wire;
        if (bump) ++ride_rev;
        ride_sent = true; ride_kind = kind; ride_seat = seat; ride_vehicle = vehicle;
        rw::Packet p{kind, ride_rev, static_cast<std::uint32_t>(now)};
        p.seat = seat; p.vehicle = vehicle;
        std::uint8_t bytes[rw::kSize]{};
        Check(rw::Encode(p, bytes), "ride encodes");
        fake.channel2.emplace_back(bytes, bytes + sizeof(bytes));
    }
    // One remote frame: the sample advances its sender clock.
    // A long step carries the sender's ride heartbeat (same revision) like the 2 s cadence.
    bool Step(std::uint64_t ms = 33) {
        fake.sample.sender_ms += static_cast<std::int64_t>(ms);
        if (ms >= 500 && ride_sent) { now += 1; Ride(ride_kind, ride_seat, ride_vehicle, false); now -= 1; }
        return Tick(1, ms);
    }
    // The body on foot, realized and loaded (the remote on foot next to the viewer).
    void Show() {
        for (unsigned i = 0; i < 60 && !driver.mission.bits_set; ++i) Step(50);
        Check(driver.mission.bits_set, "free roam placement realizes");
        fake.Realize(true); fake.state.parts[1] = 3;
        Check(Step() && driver.mission.realized, "body realized");
    }
    void Playing(std::uint64_t state) {
        fake.dm.q18[1] = c::q18_dm_act; fake.dm.rec_ok = true; fake.dm.rec248 = 0x16; fake.dm.rec18 = state; fake.dm.rec1a0 = 0;
        fake.dm.g388 = 0;
    }
    void Ended() { fake.dm.q18[1] = 1; fake.dm.rec248 = 0; fake.dm.g388 = 0; }
};

void TestRideSeat() {
    namespace rw = mgscoop::partner_ride_wire;
    constexpr std::uint64_t kFrontLeftLoop = 0x489157d7d94full, kBackLeftLoop = 0xc40b38a1a985ull;
    {
        // Enter, confirm, hold, exit on foot (stop), revert after DM End, re-show.
        SeatSession s;
        s.Show();
        s.Ride(rw::Kind::vehicle, 0, 25608);
        s.fake.sample.act = 111;
        const auto slots = s.fake.Writes("slot");
        Check(s.Step() && (s.reply.op & op::ride_seat) && s.reply.seat_vehicle == 25608 && s.reply.seat == 0 &&
                  s.reply.seat_action == partner::seat_action::play && s.reply.seat_entry == 0 && !(s.reply.op & ~op::ride_seat),
              "a vehicle ride in seat 0 plays the seat DM (RIDE_SEAT only)");
        Check(s.fake.seat_arms == 1 && s.fake.seat_armed == 0 && s.fake.dm_patched && s.driver.mission.seat.phase == pd::SeatPhase::seated &&
                  s.fake.Containing("partner_ride_seat=armed; vehicle=25608; seat=0;") == 1 &&
                  s.fake.Containing("partner_ride_seat=play; vehicle=25608; seat=0;") == 1 && s.fake.Containing("partner_ride=seat_show") == 1,
              "comparator and seat dwords armed before the play");
        Check(!s.driver.mission.input_armed && !s.driver.mission.native_defer_armed && s.driver.mission.realized &&
                  s.driver.mission.parts_loaded, "native drive blocked; the realized body stays published (nametag)");
        Check(s.Step() && s.reply.op == 0 && s.driver.mission.seat.plays == 1, "no re-issue before the confirmation window");
        s.Playing(kFrontLeftLoop);
        Check(s.Step() && s.reply.op == 0 && s.driver.mission.seat.confirmed &&
                  s.fake.Containing("partner_ride_seat=confirmed; vehicle=25608; seat=0; state=0x489157d7d94f;") == 1,
              "the record's seated state confirms");
        const auto calls = s.fake.calls.size();
        for (unsigned i = 0; i < 60; ++i) Check(s.Step(), "seated ticks");
        Check(s.fake.calls.size() == calls && s.fake.Writes("slot") == slots && s.reply.op == 0 && s.fake.Writes("clear") == 0 &&
                  s.fake.Containing("partner_ride_seat=status; phase=seated; vehicle=25608; seat=0; confirmed=1;") >= 1,
              "seated: no xform, slot, Warp or bits changes; 1 Hz status");
        // Heartbeats do not spend the rx cap.
        const auto rx = s.fake.Containing("partner_ride=rx;");
        s.Ride(rw::Kind::vehicle, 0, 25608, false); Check(s.Step(), "heartbeat");
        Check(s.fake.Containing("partner_ride=rx;") == rx, "same-revision heartbeat is not logged");
        // The remote leaves: stop, wait for DM End, revert, re-show.
        s.Ride(rw::Kind::foot); s.fake.sample.act = 1;
        Check(s.Step() && s.reply.op == op::dm_stop && s.driver.mission.seat.phase == pd::SeatPhase::exit && s.fake.seat_armed == 0,
              "kind foot stops the DM; the dwords stay until DM End");
        Check(s.Step() && s.reply.op == 0 && s.fake.seat_armed == 0, "no repeated stop inside the stop window");
        s.Ended();
        Check(s.Step() && s.fake.seat_armed == -1 && s.fake.seat_revert_reason == "foot" && s.fake.Writes("clear") == 1 &&
                  !s.driver.mission.bits_set && s.driver.mission.range_needs_place && s.driver.mission.seat.phase == pd::SeatPhase::idle &&
                  s.fake.Containing("partner_ride_seat=exit; vehicle=25608; seat=0; reason=foot; via=stop; confirmed=1;") == 1,
              "after DM End the dwords revert and the body goes through the re-show flow");
        s.fake.Realize(false); s.fake.state.parts[1] = 0;
        Check(s.Step() && s.fake.Writes("slot") == slots + 1, "re-placed on foot");
        Check(s.Step() && s.driver.mission.bits_set, "re-realized");
        pd::MissionEnd(s.driver, s.hooks);
        Check(s.fake.seat_reverts >= 2, "mission end reverts the dwords");
    }
    {
        // Mission start while the remote already drives: the hidden body is placed,
        // realized by the existing flow, then seated (no 500 ms landing proof).
        SeatSession s;
        s.Ride(rw::Kind::vehicle, 0, 25608);
        s.fake.sample.act = 111;
        Check(s.Step(50) && s.driver.mission.visibility.seated && !s.driver.mission.visibility.hidden && s.fake.Writes("slot") == 1 &&
                  s.reply.op == 0, "seated at once: placed at the remote's seated pose");
        Check(s.Step(50) && s.driver.mission.bits_set && s.fake.seat_arms == 0, "realized by the existing flow before the seat starts");
        s.fake.Realize(true); s.fake.state.parts[1] = 3;
        Check(s.Step(50) && (s.reply.op & op::ride_seat) && s.fake.seat_arms == 1, "then seated");
    }
    {
        // Get-off: exit=getoff appends the GetOff entry; act 112 advances to it (GoNext).
        SeatSession s(mgscoop::PartnerRideSeatExit::getoff);
        s.Show();
        s.Ride(rw::Kind::vehicle, 2, 25609);
        Check(s.Step() && (s.reply.op & op::ride_seat) && s.reply.seat == 2 && s.reply.seat_entry == partner::seat_entry::get_off,
              "the chain ends with GetOff");
        s.Playing(kBackLeftLoop);
        Check(s.Step() && s.driver.mission.seat.confirmed, "rear-left confirmed");
        s.fake.sample.act = 112;
        Check(s.Step() && s.reply.op == op::ride_seat && s.reply.seat_action == partner::seat_action::next && s.driver.mission.seat.next_sent,
              "remote act 112 advances to GetOff");
        Check(s.Step() && s.reply.op == 0, "the GetOff plays out");
        s.Ended();
        Check(s.Step() && s.fake.seat_armed == -1 && s.fake.seat_revert_reason == "getoff_act" &&
                  s.fake.Containing("reason=getoff_act; via=getoff;") == 1 && s.driver.mission.seat.blocked,
              "GetOff end reverts; the episode's (vehicle, seat) is latched");
        Check(s.Step() && s.driver.mission.visibility.hidden && !s.driver.mission.visibility.seated,
              "the latched seat hides until the ride changes");
        s.Ride(rw::Kind::foot); s.fake.sample.act = 1;
        Check(s.Step() && !s.driver.mission.seat.blocked, "foot clears the latch");
    }
    {
        // A GetOff advance that is not consumed falls back to stop.
        SeatSession s(mgscoop::PartnerRideSeatExit::getoff);
        s.Show();
        s.Ride(rw::Kind::vehicle, 0, 25608);
        Check(s.Step(), "play");
        s.Playing(kFrontLeftLoop);
        Check(s.Step() && s.driver.mission.seat.confirmed, "confirmed");
        s.fake.sample.act = 112;
        Check(s.Step() && s.reply.seat_action == partner::seat_action::next, "next");
        Check(s.Step(4000) && s.reply.op == op::dm_stop, "unconsumed GetOff advance stops after kSeatNextMs");
    }
    {
        // Get-in: within kSeatGetInMs of the vehicle kind the chain starts with GetOn.
        SeatSession s(mgscoop::PartnerRideSeatExit::stop, mgscoop::PartnerRideSeatEnter::getin);
        s.Show();
        s.Ride(rw::Kind::vehicle, 1, 25608);
        Check(s.Step() && s.reply.seat == 1 && s.reply.seat_entry == partner::seat_entry::get_on && s.driver.mission.seat.loop_entry == 1,
              "get-in prepends GetOn");
        s.Playing(0x00f3b0882fd1ull);  // stateVehicleGetOnFrontRight
        Check(s.Step() && s.driver.mission.seat.confirmed, "the GetOn state confirms seat 1");
    }
    {
        // Unconfirmed: re-issued up to kSeatPlays, then refused and latched (hides).
        SeatSession s;
        s.Show();
        s.Ride(rw::Kind::vehicle, 0, 25608);
        Check(s.Step() && s.driver.mission.seat.plays == 1, "first play");
        Check(s.Step(2000) && s.driver.mission.seat.phase == pd::SeatPhase::arm, "unconfirmed after 2s");
        Check(s.Step() && (s.reply.op & op::ride_seat) && s.driver.mission.seat.plays == 2 && s.fake.seat_arms == 1, "re-issued, dwords kept");
        Check(s.Step(2000) && s.Step() && s.driver.mission.seat.plays == 3, "third play");
        Check(s.Step(2000) && s.fake.Containing("partner_ride_seat=refused; vehicle=25608; seat=0; name=-; state=-; reason=unconfirmed;") == 1,
              "refused after three unconfirmed plays");
        Check(s.Step() && s.fake.seat_armed == -1 && s.driver.mission.seat.blocked && s.driver.mission.seat.phase == pd::SeatPhase::idle,
              "settled and latched");
        Check(s.Step() && s.driver.mission.visibility.hidden, "the refused vehicle hides");
    }
    {
        // A Lua refusal (no locator name) ends the request at once.
        SeatSession s;
        s.Show();
        s.Ride(rw::Kind::vehicle, 0, 25600);
        Check(s.Step() && (s.reply.op & op::ride_seat) && s.reply.seat_vehicle == 25600, "play for an unnamed vehicle");
        const std::string note = std::string(partner::log_prefix) + "event=ride_seat_refused;id=25600;seat=0;reason=no_name";
        pd::LuaLog(s.driver, s.hooks, note.data(), note.size());
        Check(s.driver.mission.seat.lua_refused, "the note marks the request");
        Check(s.Step() && s.fake.Containing("reason=lua_refused") >= 1 && s.fake.seat_armed == -1 && s.driver.mission.seat.blocked,
              "refused and reverted on the next tick");
        const std::string other = std::string(partner::log_prefix) + "event=ride_seat_refused;id=25601;seat=0;reason=no_name";
        pd::LuaLog(s.driver, s.hooks, other.data(), other.size());
        Check(!s.driver.mission.seat.lua_refused, "another vehicle's refusal is ignored");
    }
    {
        // A dm=map clip on body 1 is released first; the viewer's own DM blocks the arm.
        SeatSession s;
        s.Show();
        s.fake.dm.q18[1] = c::q18_dm_act; s.fake.dm.rec18 = 0x81582ffa24b9ull;
        s.Ride(rw::Kind::vehicle, 0, 25608);
        Check(s.Step() && s.reply.op == op::dm_stop && s.driver.mission.seat.phase == pd::SeatPhase::release && s.fake.seat_arms == 0,
              "release stops the clip before arming");
        s.Ended(); s.fake.dm.q18[0] = c::q18_dm_act;
        Check(s.Step() && s.reply.op == 0 && s.fake.seat_arms == 0 && s.driver.mission.seat.phase == pd::SeatPhase::arm,
              "the viewer's own DM holds the arm");
        s.fake.dm.q18[0] = 1;
        Check(s.Step() && (s.reply.op & op::ride_seat) && s.fake.seat_arms == 1, "armed once the viewer's DM ended");
    }
    {
        // Sanity: the anchored body far from the remote's seated pose for 1s falls back to hide.
        SeatSession s;
        s.Show();
        s.Ride(rw::Kind::vehicle, 0, 25608);
        Check(s.Step(), "play");
        s.Playing(kFrontLeftLoop);
        Check(s.Step() && s.driver.mission.seat.confirmed, "confirmed");
        s.fake.body_pos[0] = 9.0f;
        Check(s.Step() && s.driver.mission.seat.far_since_ms != 0, "far observed");
        Check(s.Step(500) && s.driver.mission.seat.phase == pd::SeatPhase::seated, "far for 0.5s: still seated");
        Check(s.Step(600) && s.fake.Containing("partner_ride=seat_far; vehicle=25608; seat=0;") == 1 && s.reply.op == op::dm_stop,
              "far for 1.1s: seat_far, stop");
        s.Ended();
        Check(s.Step() && s.driver.mission.seat.blocked && s.fake.seat_armed == -1, "far latches the seat");
    }
    {
        // Refusals and scope: a patch refusal, Mother Base, nride=hide, a vehicle change.
        SeatSession s;
        s.fake.seat_arm_ok = false;
        s.Show();
        s.Ride(rw::Kind::vehicle, 0, 25608);
        Check(s.Step() && s.fake.Containing("partner_ride_seat=refused; vehicle=25608; seat=0; name=-; state=-; reason=seat_patch;") == 1 &&
                  !(s.reply.op & op::ride_seat) && s.driver.mission.seat.blocked, "a refused seat patch never plays");
        SeatSession mb(mgscoop::PartnerRideSeatExit::stop, mgscoop::PartnerRideSeatEnter::direct, mgscoop::PartnerNativeRide::seat, 30050);
        mb.fake.Realize(true); mb.fake.state.parts[1] = 3; mb.driver.mission.bits_set = true;
        mb.Ride(rw::Kind::vehicle, 0, 25608);
        Check(mb.Step() && !(mb.reply.op & op::ride_seat) && mb.driver.mission.visibility.hidden && mb.fake.seat_arms == 0,
              "Mother Base never seats (vehicle rides hide)");
        SeatSession hide(mgscoop::PartnerRideSeatExit::stop, mgscoop::PartnerRideSeatEnter::direct, mgscoop::PartnerNativeRide::hide);
        hide.Show();
        hide.Ride(rw::Kind::vehicle, 0, 25608);
        Check(hide.Step() && hide.driver.mission.visibility.hidden && hide.fake.seat_arms == 0 && hide.fake.Writes("clear") == 1,
              "nride=hide keeps the proven hide");
        SeatSession change;
        change.Show();
        change.Ride(rw::Kind::vehicle, 0, 25608);
        Check(change.Step(), "play");
        change.Playing(kFrontLeftLoop);
        Check(change.Step() && change.driver.mission.seat.confirmed, "confirmed");
        change.Ride(rw::Kind::vehicle, 1, 25608);
        Check(change.Step() && change.reply.op == op::dm_stop && change.driver.mission.seat.exit_reason &&
                  std::strcmp(change.driver.mission.seat.exit_reason, "vehicle_change") == 0, "a seat change exits");
        change.Ended();
        Check(change.Step() && !change.driver.mission.seat.blocked && change.fake.seat_armed == -1, "a seat change does not latch");
        // An unknown seat or a horse in seat mode hides.
        change.Ride(rw::Kind::vehicle, 0xFF, 25608);
        Check(change.Step() && change.driver.mission.visibility.hidden, "unknown seat hides");
    }
    {
        // The debug seat_dm lever: manual arm with nride=off, confirm, stop.
        SeatSession s(mgscoop::PartnerRideSeatExit::stop, mgscoop::PartnerRideSeatEnter::direct, mgscoop::PartnerNativeRide::off);
        s.Show();
        Check(!pd::SeatManual(s.driver, s.hooks, true, 2, 6656, s.now, false) && s.fake.Containing("reason=vehicle_type") == 1,
              "a horse id is refused");
        s.fake.dm.q18[0] = c::q18_dm_act;
        Check(!pd::SeatManual(s.driver, s.hooks, true, 2, 25608, s.now, false) && s.fake.Containing("reason=busy") == 1, "a running DM refuses");
        s.fake.dm.q18[0] = 1;
        Check(!pd::SeatManual(s.driver, s.hooks, false, 0, 0, s.now, false) && s.fake.Containing("reason=not_manual") == 1, "stop without arm");
        Check(pd::SeatManual(s.driver, s.hooks, true, 2, 25608, s.now, false) && s.fake.seat_armed == 2 && s.driver.mission.seat.manual &&
                  s.fake.Containing("partner_ride_seat=armed; vehicle=25608; seat=2; name=lua; state=lua; reason=manual;") == 1,
              "manual arm");
        s.Playing(kBackLeftLoop);
        Check(s.Step() && s.reply.op == 0 && s.driver.mission.seat.confirmed, "the manual DM confirms (any state)");
        Check(pd::SeatManual(s.driver, s.hooks, false, 0, 0, s.now, false) && s.driver.mission.seat.phase == pd::SeatPhase::exit,
              "manual stop");
        Check(s.Step() && s.reply.op == 0, "Lua stops it first");
        Check(s.Step(1100) && s.reply.op == op::dm_stop, "native backs the stop up");
        s.Ended();
        Check(s.Step() && s.fake.seat_armed == -1 && s.fake.seat_revert_reason == "manual_stop" && !s.driver.mission.seat.blocked,
              "manual exit reverts without a latch");
        SeatSession mb(mgscoop::PartnerRideSeatExit::stop, mgscoop::PartnerRideSeatEnter::direct, mgscoop::PartnerNativeRide::off, 30050);
        Check(!pd::SeatManual(mb.driver, mb.hooks, true, 0, 25608, mb.now, false) && mb.fake.Containing("reason=mission") == 1,
              "the lever is free-roam only");
    }
    {
        // Sender: v2 for a vehicle with a seat, a seat change is a new revision; status census on change.
        Session sender(0);
        AddNativeHooks(sender.hooks); sender.hooks.send_reliable_append = HookSendReliable;
        auto send = [&](const char* suffix, std::uint64_t step) {
            sender.now += step;
            const auto source = TickSource(++sender.frame, 0) + " 0 0" + suffix;
            return pd::Tick(sender.driver, sender.hooks, sender.lab, source.data(), source.size(), pd::Clock{sender.now, sender.now}, false,
                            sender.reply);
        };
        auto last = [&]() {
            rw::Packet out{};
            for (const auto& item : sender.fake.reliable_sent) if (rw::IsRide(item.data(), item.size())) rw::Decode(item.data(), item.size(), out);
            return out;
        };
        Check(send(" ride=2 veh=25608 seat=0 st=0x400", 1) && last().version == 2 && last().seat == 0 && last().vehicle == 25608 &&
                  last().kind == rw::Kind::vehicle, "vehicle ride sends v2");
        Check(sender.fake.Containing("partner_status=local; st=0x400; names=on_vehicle; ride=2; vehicle=25608; seat=0") == 1, "status census");
        const auto revision = last().revision;
        Check(send(" ride=2 veh=25608 seat=1 st=0x400", 1) && last().seat == 1 && last().revision == revision + 1 &&
                  sender.fake.Containing("partner_status=local;") == 1, "seat change: new revision; unchanged status not logged");
        Check(send(" ride=0 st=0x1", 1) && last().version == 1 && last().kind == rw::Kind::foot && last().seat == rw::kSeatUnknown &&
                  sender.fake.Containing("partner_status=local; st=0x1; names=binocle;") == 1, "foot sends v1; binoculars logged");
        Check(send(" ride=2 veh=65535 seat=255", 1) && last().version == 1 && last().kind == rw::Kind::vehicle,
              "an unknown vehicle and seat keep v1");
    }
}

// Anim round 1 weapons (docs/anim-round1-weapons.md): the unsupported gate and
// its lines, the aim intent's Arm fields, the auto-fire stop, the reload rate
// factor in the Arm, partner_weapon_ctx and partner_native_gun past the old cap.
// The support gate has its own owner filter/stock guard. It must not depend
// on the historical weapon_ok latch from a previously observed class-1 gun.
void TestNativeThrownSupport() {
    namespace pn=mgscoop::partner_native;
    namespace pi=mgscoop::partner_input;
    namespace slots=mgscoop::partner_slots;
    NativeSession s;SetVisualFire(s);
    s.driver.settings.nunsupported=true;
    s.driver.settings.naim_intent=mgscoop::PartnerNativeAimIntent::state;
    s.hooks.support_ready=[](void* ctx,const pp::State& st,std::uint16_t equip) noexcept {
        return static_cast<Fake*>(ctx)->support_ready && st.ids[0]==0 && st.ids[1]==1 && equip==354;
    };
    s.hooks.publish_support_identity=[](void* ctx,std::uint16_t equip) noexcept {static_cast<Fake*>(ctx)->support_identity=equip;};
    s.fake.equip_types[46]=4;s.fake.equip_types[354]=12;s.fake.equip_types[355]=13;s.fake.equip_types[516]=14;
    s.fake.latest.packet.version=5;s.fake.latest.packet.xflags=pi::XFlag::Equip;
    s.fake.latest.packet.gun_flags=pi::GunFlag::Valid|pi::GunFlag::InHand|pi::GunFlag::Aiming;
    s.fake.latest.packet.gun_equip=46;s.fake.latest.packet.inv_slot=1;
    s.fake.gun.fields.equip=46;
    Check(s.Until(pn::Mode::active,9000),"support fixture native active");
    s.driver.settings.nsupport=true;s.fake.support_ready=true;
    s.driver.mission.weapon_ok=false;
    s.fake.gun.fields.flags27c=2u<<6;s.fake.gun.fields.equip=354;s.fake.gun.mode_cur=28;
    s.fake.latest.packet.gun_equip=354;s.fake.latest.packet.inv_slot=3;
    s.N();
    Check(s.driver.settings.support_live && !s.driver.settings.fire_live &&
        s.driver.mission.weapon_gate==slots::Gate::support && s.fake.arms.back().strip==0x40u &&
        (s.fake.arms.back().config.mask&0x10u) && !s.fake.arms.back().mag_mirror &&
        s.fake.arms.back().gun_rec==s.fake.gun.rec && s.fake.arms.back().native_support && s.fake.arms.back().aim_intent,
        "qualified support passes HOLD/FIRE without historical gun-class admission");
    Check(s.fake.support_identity==354,"the support verdict's equip is published for partner_net's unknown slot/equip debounce");
    // Critic #4 through the driver (hidden run c9e3156f: a 320 ms host stall never held): the stall
    // hold follows the arbiter's effective state, never the configured copy's native_active.
    {
        const auto stale=s.fake.latest.age_ms;
        s.driver.settings.nstall=mgscoop::PartnerNativeStall::hold;
        s.driver.settings.stale_ms=250;s.driver.settings.stall_ms=400;
        s.fake.latest.age_ms=320;s.N();
        Check(!s.driver.settings.native_active && pd::NativeActive(s.driver) && s.driver.mission.support_stall &&
            s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::support &&
            s.driver.mission.remote_slot==0xFF && s.fake.arms.back().strip==0x40u && s.fake.support_identity==354,
            "a stale v5 slot-3 frame inside stall_ms keeps the support gate (configured native_active stays false)");
        s.fake.latest.age_ms=399;s.N();
        Check(s.driver.mission.support_stall && s.driver.mission.weapon_gate==slots::Gate::support,"still held at 399 ms");
        s.fake.latest.age_ms=stale;s.N();
        Check(!s.driver.mission.support_stall && s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::support,
            "fresh again: live support, no stall");
        s.fake.latest.age_ms=401;s.N();
        Check(!s.driver.mission.support_stall && !s.driver.settings.support_live &&
            s.driver.mission.weapon_gate==slots::Gate::unsupported && (s.fake.arms.back().strip&0x58u)==0x58u,
            "beyond stall_ms the old strip applies");
        s.fake.latest.age_ms=stale;s.N();
        s.driver.settings.nstall=mgscoop::PartnerNativeStall::neutral;s.fake.latest.age_ms=320;s.N();
        Check(!s.driver.mission.support_stall && !s.driver.settings.support_live &&
            s.driver.mission.weapon_gate==slots::Gate::unsupported,"stall=neutral (lab): no hold");
        s.fake.latest.age_ms=stale;s.N();
        Check(s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::support,"fresh: support again");
    }
    s.fake.support_ready=false;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported &&
        (s.fake.arms.back().strip&0x58u)==0x58u && !(s.fake.arms.back().config.mask&0x10u),"lost support safety masks same tick");
    Check(s.fake.support_identity==0,"no support verdict: 0 is published (nothing is held)");
    s.driver.settings.nunsupported=false; // old diagnostic cannot weaken native support
    s.fake.support_ready=true;s.fake.latest.packet.gun_equip=355;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported,"type 13 stays unsupported");
    s.fake.latest.packet.gun_equip=516;s.fake.latest.packet.inv_slot=6;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported,"arms stay unsupported");
    s.driver.settings.nunsupported=true;
    s.fake.latest.packet.gun_equip=354;s.fake.latest.packet.inv_slot=3;s.driver.settings.nsupport=false;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported,"key off refuses qualified support");
    s.fake.latest.packet.gun_equip=46;s.fake.latest.packet.inv_slot=1;
    s.fake.gun.fields.equip=46;s.fake.gun.fields.flags27c=1u<<6;s.fake.gun.mode_cur=7;s.N();
    Check(!s.driver.settings.support_live && s.driver.settings.fire_live &&
        s.driver.mission.weapon_gate==slots::Gate::match && s.fake.arms.back().mag_mirror,"support returns to ordinary gun policy");
}

// WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): the C4 and arm lanes of the driver's support gate. C4 (type 13,
// 391..395) in slot 3 with partner_native_placed gets the support gate (RELOAD stripped, HOLD/FIRE kept); an arm
// (type 14, only 516) in slot 6 with partner_native_arm gets it with FIRE stripped too (HOLD only); every other
// type-13/14 equip, the wrong slot, or a key off stays unsupported (0x58), and the Arm carries both keys.
void TestNativePlacedArmLanes() {
    namespace pn=mgscoop::partner_native;
    namespace pi=mgscoop::partner_input;
    namespace slots=mgscoop::partner_slots;
    NativeSession s;SetVisualFire(s);
    s.driver.settings.nunsupported=true;
    s.driver.settings.naim_intent=mgscoop::PartnerNativeAimIntent::state;
    s.hooks.support_ready=[](void* ctx,const pp::State& st,std::uint16_t equip) noexcept {
        return static_cast<Fake*>(ctx)->support_ready && st.ids[0]==0 && st.ids[1]==1 && (equip==395 || equip==411 || equip==516 || equip==517);
    };
    s.hooks.publish_support_identity=[](void* ctx,std::uint16_t equip) noexcept {static_cast<Fake*>(ctx)->support_identity=equip;};
    s.fake.equip_types[46]=4;s.fake.equip_types[395]=13;s.fake.equip_types[411]=13;s.fake.equip_types[516]=14;s.fake.equip_types[517]=14;
    s.fake.latest.packet.version=5;s.fake.latest.packet.xflags=pi::XFlag::Equip;
    s.fake.latest.packet.gun_flags=pi::GunFlag::Valid|pi::GunFlag::InHand|pi::GunFlag::Aiming;
    s.fake.latest.packet.gun_equip=46;s.fake.latest.packet.inv_slot=1;
    s.fake.gun.fields.equip=46;
    Check(s.Until(pn::Mode::active,9000),"WS4 v1 fixture native active");
    s.driver.settings.nsupport=true;s.fake.support_ready=true;s.driver.mission.weapon_ok=false;
    // C4 395 in hand slot 3 on both sides (body 1's record class 3).
    s.fake.gun.fields.flags27c=3u<<6;s.fake.gun.fields.equip=395;s.fake.gun.mode_cur=43;
    s.fake.latest.packet.gun_equip=395;s.fake.latest.packet.inv_slot=3;
    s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported &&
          (s.fake.arms.back().strip&0x58u)==0x58u && !s.fake.arms.back().native_placed,"C4 with the placed key off: unsupported (Round 1)");
    s.driver.settings.nplaced=true;s.N();
    Check(s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::support && s.fake.arms.back().strip==0x40u &&
          s.fake.arms.back().native_placed && !s.fake.arms.back().native_arm && s.fake.support_identity==395,
          "C4 395 with the placed key: the support gate (HOLD/FIRE kept), the Arm names the lane");
    s.fake.latest.packet.inv_slot=6;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported,"C4 reported in slot 6: unsupported");
    s.fake.latest.packet.inv_slot=3;s.fake.latest.packet.gun_equip=411;s.fake.gun.fields.equip=411;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported,
          "another type-13 item (mine/decoy) with the placed key: unsupported");
    // The arm 516 in hand slot 6 (body 1's record class 6).
    s.fake.gun.fields.flags27c=6u<<6;s.fake.gun.fields.equip=516;s.fake.gun.mode_cur=1;
    s.fake.latest.packet.gun_equip=516;s.fake.latest.packet.inv_slot=6;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported &&
          (s.fake.arms.back().strip&0x58u)==0x58u,"arm with the arm key off: unsupported (Round 1)");
    s.driver.settings.narm=true;s.N();
    Check(s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::support && s.fake.arms.back().strip==0x50u &&
          s.fake.arms.back().native_arm && s.fake.arms.back().gun_rec==s.fake.gun.rec && !s.fake.arms.back().mag_mirror,
          "arm 516 with the arm key: the support gate with FIRE stripped (HOLD only)");
    Check(s.driver.mission.aim_refusal==slots::AimRefusal::none && s.fake.arms.back().aim_intent,"an admitted arm keeps the aim intent (toggle aim)");
    // WS4-1: the rocket arm 517 (class 5; its hold is mode 54, a use mode) is no lane, even with a Ready that
    // answers: unsupported, so body 1 never gets HOLD for it.
    s.fake.gun.fields.flags27c=5u<<6;s.fake.gun.fields.equip=517;s.fake.latest.packet.gun_equip=517;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported && (s.fake.arms.back().strip&0x58u)==0x58u,
          "rocket arm 517 with the arm key: unsupported (HOLD stripped too)");
    s.fake.gun.fields.flags27c=6u<<6;s.fake.gun.fields.equip=516;s.fake.latest.packet.gun_equip=516;s.N();
    Check(s.driver.settings.support_live && s.fake.arms.back().strip==0x50u,"back to 516: the arm lane again");
    s.fake.latest.packet.inv_slot=3;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported,"an arm reported in slot 3: unsupported");
    s.fake.latest.packet.inv_slot=6;s.driver.settings.nsupport=false;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported &&
          !s.fake.arms.back().native_arm && !s.fake.arms.back().native_placed,"the lanes need partner_native_support");
    s.driver.settings.nsupport=true;s.fake.support_ready=false;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::unsupported,"the observer's Ready refuses: unsupported");
    // Back to the gun: ordinary gun policy.
    s.fake.support_ready=true;s.fake.latest.packet.gun_equip=46;s.fake.latest.packet.inv_slot=1;
    s.fake.gun.fields.equip=46;s.fake.gun.fields.flags27c=1u<<6;s.fake.gun.mode_cur=7;s.N();
    Check(!s.driver.settings.support_live && s.driver.mission.weapon_gate==slots::Gate::match,"the arm returns to ordinary gun policy");
}

void TestAnimRound1Weapons() {
    namespace pn = mgscoop::partner_native;
    namespace pi = mgscoop::partner_input;
    NativeSession s;
    SetVisualFire(s);
    s.driver.settings.nunsupported = true;
    s.driver.settings.ndry = true;
    s.driver.settings.naim_intent = mgscoop::PartnerNativeAimIntent::state;
    s.driver.settings.nreload = mgscoop::PartnerNativeReload::event;
    s.fake.gun.fields.equip = 46;
    s.fake.gun.fields.cap = 9;
    s.fake.gun.fields.mag = 3;
    s.fake.gun.mode_cur = 7;
    s.fake.equip_types[46] = 4;
    s.fake.equip_types[354] = 12;  // EQP_SWP_Grenade_G04 (EQP_TYPE_Throwing)
    s.fake.equip_types[516] = 14;  // EQP_HAND_JEHUTY (EQP_TYPE_AttackArm)
    s.fake.latest.packet.version = 5;
    s.fake.latest.packet.xflags = pi::XFlag::Equip | pi::XFlag::Mag;
    s.fake.latest.packet.gun_equip = 46;
    s.fake.latest.packet.mag = 20;
    s.fake.latest.packet.mag_cap = 30;
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Aiming;
    s.fake.latest.packet.gun_mode = 7;
    s.fake.latest.packet.inv_slot = 1;
    s.fake.sample.anim_valid = true;
    s.fake.sample.act = 11;
    Check(s.Until(pn::Mode::active, 9000) && s.driver.settings.fire_live, "anim weapons: active with visual fire");
    s.N();
    Check(s.fake.arms.back().strip == 0 && s.fake.arms.back().aim_intent && s.fake.arms.back().aim_act_until_ms > s.now &&
              s.fake.arms.back().reload_rate_k == 0.0f && s.driver.mission.aim_refusal == mgscoop::partner_slots::AimRefusal::none,
          "match, slot 1, aim act: the Arm carries the aim intent (no rate factor with reload_rate=off)");
    Check(s.fake.Containing("partner_aim_intent=on; source=flag; refusal=none; remote_act=11; act_aim=1;") >= 1,
          "partner_aim_intent: on, the v5 flag");
    // The remote selects a grenade (slot 3, equip 354, type 12): HOLD|FIRE|RELOAD stripped, no aim intent.
    s.fake.latest.packet.inv_slot = 3;
    s.fake.latest.packet.gun_equip = 354;
    s.N();
    Check(s.fake.arms.back().strip == 0x58u && !s.fake.arms.back().aim_intent &&
              s.fake.Has("partner_weapon=unsupported; remote_slot=3; remote_equip=354; type=12; reason=slot; body_equip=46; strip=0x58\r\n") &&
              s.fake.Containing("partner_weapon=mismatch") == 0,
          "a grenade in the remote's hands: unsupported (0x58), logged once, no mismatch line");
    for (int i = 0; i < 5; ++i) s.N();
    Check(s.fake.Containing("partner_weapon=unsupported;") == 1, "the unsupported line only on change");
    // The bionic arm (slot 6, equip 516, type 14): a new key.
    s.fake.latest.packet.inv_slot = 6;
    s.fake.latest.packet.gun_equip = 516;
    s.N();
    Check(s.fake.arms.back().strip == 0x58u &&
              s.fake.Has("partner_weapon=unsupported; remote_slot=6; remote_equip=516; type=14; reason=slot; body_equip=46; strip=0x58\r\n"),
          "the bionic arm: unsupported");
    // A v4-style remote without the slot: the type alone decides.
    s.fake.latest.packet.inv_slot = 0xFF;
    s.N();
    Check(s.fake.arms.back().strip == 0x58u && s.fake.Containing("reason=type;") == 1, "no live slot: the in-hand type decides");
    // Back to the rifle in slot 1.
    s.fake.latest.packet.inv_slot = 1;
    s.fake.latest.packet.gun_equip = 46;
    s.N();
    Check(s.fake.arms.back().strip == 0 && s.fake.arms.back().aim_intent && s.fake.Containing("partner_weapon=unsupported_end;") == 1,
          "back to the rifle: nothing stripped, the intent back");
    // The key off: the old verdicts (mismatch strips FIRE|RELOAD only).
    s.driver.settings.nunsupported = false;
    s.fake.latest.packet.inv_slot = 3;
    s.fake.latest.packet.gun_equip = 354;
    s.N();
    Check(s.fake.arms.back().strip == 0x50u && s.driver.mission.weapon_mismatch, "partner_native_unsupported=0: the old mismatch strip");
    s.driver.settings.nunsupported = true;
    s.fake.latest.packet.inv_slot = 1;
    s.fake.latest.packet.gun_equip = 46;
    s.N();
    // The aim act leaves the aim family and the flag clears: no act deadline.
    s.fake.sample.act = 1;
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand;
    s.N();
    Check(s.fake.arms.back().aim_intent && s.fake.arms.back().aim_act_until_ms == 0, "no aim act: no act deadline in the Arm");
    // A blocked act (being carried, 43): the gate refuses.
    s.fake.sample.act = 43;
    s.N();
    Check(!s.fake.arms.back().aim_intent && s.driver.mission.aim_refusal == mgscoop::partner_slots::AimRefusal::act,
          "a carried remote: no aim intent");
    s.fake.sample.act = 1;
    // pad: never an intent.
    s.driver.settings.naim_intent = mgscoop::PartnerNativeAimIntent::pad;
    s.N();
    Check(!s.fake.arms.back().aim_intent && s.driver.mission.aim_refusal == mgscoop::partner_slots::AimRefusal::off, "aim_intent=pad: off");
    s.driver.settings.naim_intent = mgscoop::PartnerNativeAimIntent::state;
    // The auto-fire stop: the remote's magazine reaches 0 while it holds the trigger.
    s.fake.latest.packet.mag = 0;
    s.N(16);
    Check(!(s.fake.arms.back().strip & 0x10u), "the last round's sample: FIRE still passes");
    s.N(40);
    s.N(40);
    Check((s.fake.arms.back().strip & 0x10u) && s.driver.mission.dry_strip &&
              s.fake.Containing("partner_fire=dry_stop; remote_mag=0; remote_cap=30; reason=remote_empty; stops=1;") == 1,
          "dry after the grace: FIRE stripped, logged");
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand | pi::GunFlag::Reloading;
    s.fake.latest.packet.gun_mode = 11;
    s.N();
    Check(!(s.fake.arms.back().strip & 0x10u) && s.fake.Containing("partner_fire=dry_clear; remote_mag=0; remote_cap=30; reason=reload;") == 1,
          "a reload clears the stop");
    s.fake.latest.packet.gun_flags = pi::GunFlag::Valid | pi::GunFlag::InHand;
    s.fake.latest.packet.gun_mode = 7;
    s.fake.latest.packet.mag = 30;
    s.N();
    Check(!(s.fake.arms.back().strip & 0x10u), "a full magazine: FIRE passes");
    // The reload rate factor reaches the Arm only with adapt.
    s.driver.settings.nreload_rate = mgscoop::PartnerNativeReloadRate::adapt;
    s.N();
    Check(s.fake.arms.back().reload_rate_k == 1.0f, "reload_rate=adapt: k (1 before any pair) in the Arm");
    s.driver.mission.rate_adapt.latched = true;
    s.N();
    Check(s.fake.arms.back().reload_rate_k == 0.0f, "a latched adaptation writes nothing");
    // The mirror's rec+0x3E8 rewrite at a slot change: one partner_reload=shadow line per new write.
    s.fake.stats.native.mag_m3e8_slot = 1;
    s.N();
    Check(s.fake.Containing("partner_reload=shadow; reason=slot; slot_writes=1; cap_writes=0;") == 1, "the shadow rewrite at a slot change is logged");
    s.N();
    Check(s.fake.Containing("partner_reload=shadow;") == 1, "no new rewrite: no new line");
    s.fake.stats.native.mag_m3e8_cap = 1;
    s.N();
    Check(s.fake.Containing("partner_reload=shadow; reason=cap; slot_writes=1; cap_writes=1;") == 1, "a base above the capacity is logged");
    // partner_weapon_ctx: one line per change, at most every kWeaponCtxMs.
    const std::size_t ctx = s.fake.Containing("partner_weapon_ctx;");
    Check(ctx >= 4 && s.fake.Containing("partner_weapon_ctx; remote_slot=3; remote_equip=354; type=12;") >= 1,
          "partner_weapon_ctx lines for the switches");
    for (int i = 0; i < 10; ++i) s.N();
    Check(s.fake.Containing("partner_weapon_ctx;") == ctx, "no change: no ctx line");
    // partner_native_gun: change lines keep coming past the old 64-line cap.
    for (int i = 0; i < 200; ++i) {
        s.fake.gun.gun3c0 ^= 0x40000u;
        s.N(120);
    }
    Check(s.fake.Containing("partner_native_gun;") > 200 && s.fake.Containing("partner_native_gun=capped") == 0,
          "partner_native_gun: no blind spot after 64 changes");
    s.fake.gun.gun3c0 ^= 0x40000u; s.N(20);
    const std::size_t before = s.fake.Containing("partner_native_gun;");
    s.fake.gun.gun3c0 ^= 0x40000u; s.N(20);
    Check(s.fake.Containing("partner_native_gun;") == before, "a change within kNativeGunChangeMs coalesces");
}

// Round-1 actions track (docs/anim-round1-actions.md): the native stance
// window and its gun gate, the act census lines, the dropped pad presses and
// the DM overlay lever (suspend without a fallback, play, stop, re-entry).
bool CameraSkipArmed(void*) noexcept { return true; }
void TestRound1Actions() {
    namespace pn = mgscoop::partner_native;
    namespace pdo = mgscoop::partner_dm_overlay;
    const auto idle = [](NativeSession& s) {
        s.fake.sample.anim_valid = true; s.fake.sample.act = 1; s.fake.sample.anim_stance = 1;
        s.fake.anim_ok = true; s.fake.anim.act[1] = 1; s.fake.anim.stance[1] = 1;
    };
    {   // The native stance window: a lost crouch edge is fixed within ~0.65 s (the gun stage).
        NativeSession s;
        idle(s);
        s.driver.settings.ngun = true;
        Check(s.Until(pn::Mode::active, 9000), "stance: active");
        for (int i = 0; i < 40; ++i) s.N();
        s.fake.lines.clear();
        s.fake.sample.anim_stance = 2;  // the guest crouched; body 1 missed the edge
        for (int i = 0; i < 15; ++i) s.N();
        Check(s.fake.Count("partner_recon=stance_fix;") == 0, "native stance: no fix before the window");
        for (int i = 0; i < 10; ++i) s.N();
        Check(s.fake.Count("partner_recon=stance_fix; remote=2; body=1; via=tap;") == 1 && s.fake.Containing("; after_ms=") == 1,
              "native stance: one tap within ~650 ms (not 2 s)");
        // The remote aims (the gun stage): the next mismatch waits with reason=gun.
        s.fake.anim.stance[1] = 2;
        for (int i = 0; i < 40; ++i) s.N();
        s.fake.lines.clear();
        s.fake.sample.flags = mgscoop::sync::ActionFlag::Aiming;
        s.fake.sample.anim_stance = 1;
        for (int i = 0; i < 40; ++i) s.N();
        Check(s.fake.Count("partner_recon=stance_fix;") == 0 && s.fake.Count("partner_recon=stance_wait; reason=gun; remote=1; body=2;") == 1,
              "native stance: the remote's aim holds the fix (stance_wait reason=gun, once)");
        s.fake.sample.flags = 0;
        for (int i = 0; i < 5; ++i) s.N();
        Check(s.fake.Count("partner_recon=stance_fix; remote=1; body=2; via=tap;") == 1, "native stance: fixed once the aim ended");
    }
    {   // partner_native_stance_fast=0: the previous 2 s rule.
        NativeSession s;
        idle(s);
        s.driver.settings.stance_fast = false;
        Check(s.Until(pn::Mode::active, 9000), "stance legacy: active");
        for (int i = 0; i < 40; ++i) s.N();
        s.fake.lines.clear();
        s.fake.sample.anim_stance = 2;
        for (int i = 0; i < 50; ++i) s.N();
        Check(s.fake.Count("partner_recon=stance_fix;") == 0, "stance legacy: no fix before 2 s");
        for (int i = 0; i < 25; ++i) s.N();
        Check(s.fake.Count("partner_recon=stance_fix; remote=2; body=1; via=tap;") == 1, "stance legacy: the fix after 2 s");
    }
    {   // The act census: edge lines with family names and the match latency.
        NativeSession s;
        idle(s);
        Check(s.Until(pn::Mode::active, 9000), "census: active");
        for (int i = 0; i < 5; ++i) s.N();
        s.fake.lines.clear();
        s.fake.sample.act = 57;
        s.N();
        Check(s.fake.Count("partner_act; t=") == 1 &&
              s.fake.Containing("; edge=remote; remote=57(dive); body=1(stand_idle); match_ms=pending\r\n") == 1, "census: remote edge");
        s.N();
        s.fake.anim.act[1] = 57;
        s.N();
        Check(s.fake.Containing("; edge=body; remote=57(dive); body=57(dive); match_ms=66\r\n") == 1, "census: body matched after 66 ms");
        s.fake.sample.act = 6;
        s.N();
        s.fake.sample.act = 1;
        s.N();
        Check(s.fake.Containing("; edge=miss; remote=6(prone_crawl); body=57(dive); match_ms=miss; held_ms=33\r\n") == 1, "census: a missed act");
        // The remote presses ACTION and CQC (masked): one partner_pad_dropped line within a second.
        s.fake.lines.clear();
        s.fake.latest.seq = 500; s.fake.latest.packet.version = 6; s.fake.latest.packet.edge_seq = 9; s.fake.latest.packet.w50 = 0x20;
        s.N();
        s.fake.latest.seq = 501; s.fake.latest.packet.edge_seq = 10; s.fake.latest.packet.w50 = 0x8000;
        s.N();
        s.fake.latest.packet.edge_seq = 0; s.fake.latest.packet.w50 = 0;
        for (int i = 0; i < 35; ++i) s.N();
        Check(s.fake.Count("partner_pad_dropped; action=1; stock=0; call=0; subject=0; weapon=0; cqc=1;") == 1 &&
              s.fake.Containing("; native=1\r\n") == 1, "census: dropped ACTION and CQC presses, native window");
        // partner_act_log=0: no census lines.
        s.driver.settings.act_log = false;
        s.fake.lines.clear();
        s.fake.sample.act = 2;
        for (int i = 0; i < 5; ++i) s.N();
        Check(s.fake.Count("partner_act") == 0, "census off: nothing");
    }
    {   // The DM overlay (debug): suspend native without a fallback, play the named state, stop, re-enter at once.
        NativeSession s;
        idle(s);
        s.driver.settings.noverlay = mgscoop::PartnerNativeOverlay::debug;
        Check(s.Until(pn::Mode::active, 9000), "overlay: active first");
        const std::uint32_t fallbacks = s.driver.mission.native.fallbacks;
        const std::size_t clips = s.played.size();
        s.fake.lines.clear();
        s.Trigger("overlay=stateEludeIdle oloop=1 oms=1500");
        Check(s.fake.Has("partner_overlay=queued; state=stateEludeIdle; loop=1; ms=1500; setting=debug\r\n"), "overlay: queued");
        int gani = -1;
        for (int i = 0; i < 30 && gani < 0; ++i) {
            s.N();
            if (s.reply.op & op::dm_play) gani = s.reply.gani;
        }
        Check(gani == 64 + 32 + pdo::Find("stateEludeIdle", 14), "overlay: DM_PLAY with the overlay reply code (64 + 32 + index)");
        Check(s.fake.Containing("partner_overlay=request; state=stateEludeIdle; loop=1; source=debug;") == 1 &&
                  s.fake.Containing("partner_overlay=native; mode=leaving; reason=overlay") == 1 &&
                  s.fake.Count("partner_native=leaving") == 0 && s.driver.mission.native.fallbacks == fallbacks &&
                  s.driver.mission.dm_patched && !s.driver.mission.input_armed && !s.driver.mission.native_defer_armed,
              "overlay: native suspended on the overlay cap (no fallback), patched, window and deferral disarmed");
        s.N();
        Check(s.fake.Containing("partner_overlay=playing; state=stateEludeIdle;") == 1, "overlay: playing confirmed");
        const std::size_t xforms = s.fake.Calls("xform:");
        for (int i = 0; i < 10; ++i) s.N();
        Check(s.fake.Calls("xform:") >= xforms + 10 && s.played.size() == clips + 1, "overlay: placed by xform every tick; no dm=map clip");
        Check(s.UntilStop(2000), "overlay: DM_STOP at the timeout");
        Check(s.Until(pn::Mode::active, 3000), "overlay: native active again");
        Check(s.fake.Containing("partner_overlay=released; state=stateEludeIdle;") == 1 &&
                  s.fake.Containing("partner_overlay=native; mode=entering; reason=resume") == 1 &&
                  s.fake.Count("partner_native=entering") == 0 && s.driver.mission.native.fallbacks == fallbacks &&
                  s.driver.mission.native.suspends == 1 && !s.driver.mission.dm_patched && s.driver.mission.input_armed,
              "overlay: released; native re-entered at once (resume), no fallback, patch reverted, window armed");
        // The camera connect refusal, and the camera-skip hook point.
        s.fake.lines.clear();
        s.fake.dm.cam1f0 = 0x8800000000006400ull;
        s.fake.dm.cam200 = 1;
        s.Trigger("overlay=stateDamageBigFront oms=3000");
        const std::size_t played = s.played.size();
        for (int i = 0; i < 30; ++i) s.N();
        Check(s.played.size() == played && s.fake.Containing("partner_overlay=deferred; state=stateDamageBigFront;") == 1 &&
                  s.fake.Containing("reason=cam_connect") >= 1, "overlay: cam_connect refused (deferred) without a camera-skip module");
        s.hooks.dm_camera_skip_armed = CameraSkipArmed;
        for (int i = 0; i < 3; ++i) s.N();
        Check(s.played.size() == played + 1 && s.played.back() == 64 + pdo::Find("stateDamageBigFront", 19),
              "overlay: plays once a camera-skip module is armed (hook point)");
        s.hooks.dm_camera_skip_armed = nullptr;
        s.fake.dm.cam1f0 = 0;
        s.fake.dm.cam200 = 0;
        s.Trigger("overlay=stop");
        for (int i = 0; i < 60; ++i) s.N();
        Check(s.fake.Containing("partner_overlay=released; state=stateDamageBigFront;") == 1 && s.Until(pn::Mode::active, 3000),
              "overlay_stop: released, native again");
    }
    {   // partner_native_overlay=off (the default): the debug command is refused, native untouched.
        NativeSession s;
        idle(s);
        Check(s.Until(pn::Mode::active, 9000), "overlay off: active");
        s.Trigger("overlay=stateDead");
        for (int i = 0; i < 10; ++i) s.N();
        Check(s.fake.Containing("partner_overlay=refused; state=stateDead;") == 1 && s.fake.Containing("reason=setting_off") == 1 &&
                  s.driver.mission.native_out.mode == pn::Mode::active && !s.driver.mission.overlay_out.suspend,
              "overlay off: refused, native stays active");
    }
}

int main() {
    TestRound1Actions();
    TestCfgAndMissionBoundaries();
    TestStageThreeTimeline();
    TestTriggerStopAndTeardown();
    TestTeardownKeepAndRefusals();
    TestStateRefusalFlips();
    TestRealizeThrashCap();
    TestAllActiveAndFlags();
    TestMirrorAndOneShots();
    TestForceExistsAndPolicy();
    TestFreeRoamRange();
    TestRideVisibility();
    TestRideSeat();
    TestHiddenLocalSender();
    TestNetSourceAndCadence();
    TestInterpHookAndLost();
    TestLuaLogAndQuiet();
    TestAnimationApply();
    TestNativePoseAnimationSuppression();
    TestAnimationGuards();
    TestTransformDrivers();
    TestFacingSmoothing();
    TestSenderBodyYaw();
    TestLooks();
    TestCensusAndProbes();
    TestLookFix();
    TestInputArming();
    TestDm();
    TestReviewFixes();
    TestRound4Review();
    TestInputSequence();
    TestDmMapPatchAndTable();
    TestDmMapSources();
    TestDmMapAimPivots();
    TestDmMapRawTurnFeed();
    TestDmMapRawVisualLag();
    TestDmArchiveBounds();
    TestDmMapGates();
    TestDmMapFailures();
    TestDmMapRelease();
    TestDmMapInteractions();
    TestDmMapCaps();
    TestNativeSleepPresentationAdmission();
    TestNativeEnterAndLeave();
    TestNativeOnline();
    TestNativeRefusalsAndCensus();
    TestNativeChannel2AndSender();
    TestEquipEarlyManifest();
    TestNativeRates();
    TestNativeAim();
    TestNativeGunDirectionWire();
    TestNativePlayAndWithdraw();
    TestNativeWeapons();
    TestNativeActionsWithWeapons();
    TestNativePolish();
    TestMovementRecovery();
    TestNativeReloadSwitch();
    TestAnimRound1Weapons();
    TestNativeThrownSupport();
    TestNativePlacedArmLanes();
    std::printf("%u partner driver checks passed: CFG/boundaries, stage-3 timeline, trigger stop/stage, STOP while terminating, "
                "teardown keep, refusal caps, realize thrash cap, all-active re-assert, flags, mirror lifecycle, one-shots, "
                "force_exists, policy, net source, interp hook, lost freeze/hide, cadence, Lua notes, quiet pairing, probes, anim mirror/net (q/s/r, debounce, "
                "revert, fault), xform/hybrid drivers, looks (quiet/snake/net/copy, verify, timeout, waiting, refusals), census, "
                "ps probe, dm=map (patch, table, sources, aim pivots, gates, failures, release, teardown, caps), native puppet "
                "(enter sequence, publication order, leave on fault, dm=map rearm, census, install refusals, thread, revert latch, "
                "unlatch, channel 2 in legacy, v3 sender, play-mode trigger refusal, identity/roots withdrawal, rates: v4 sender, "
                "mirror request, telemetry, forget/epoch; weapons: fire gate, magazine mirror, loadout; both packages in one v4 and one Arm; "
                "polish: neutral placement, unwindowed leave, facing governor (turn, contest/latch, in-batch), noise gate, weapon gate, "
                "look source install, v4 pad mode and equip; aim parity: target publication, override, telemetry, ring, 1 Hz line; "
                "reload and switch: v5 negotiation (also from a clip sender), reload event machine, sample strip, slot follower, "
                "generalized gate); anim round 1 weapons (unsupported gate, aim intent Arm, auto-fire stop, rate factor, "
                "weapon ctx, native gun lines); "
                "round-1 actions (native stance window and its gun gate, act census edges/match/miss, dropped pad "
                "presses, DM overlay suspend without a fallback, play, xform placement, timeout stop, resume, cam_connect refusal and "
                "the camera-skip hook, overlay_stop, setting off); "
                "no game\n", checks);
    return 0;
}
