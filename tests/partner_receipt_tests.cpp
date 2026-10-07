// Offline partner spike receipts: classification, strict tick parsing, reply
// and log formatting, trigger grammar, stage policy and pose math. Pure; no
// game or Win32 calls.
#include "mgscoop/partner_receipt.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
using mgscoop::partner::Receipt;
Receipt Kind(const std::string& source) {
    return mgscoop::partner::Classify(source.data(), source.size(), source.c_str());
}
bool TickArgs(const std::string& args, mgscoop::partner::Tick& out) {
    const std::string source = std::string(mgscoop::partner::tick_prefix) + args;
    return mgscoop::partner::ParseTick(source.data(), source.size(), out);
}
bool TickOk(const std::string& args) {
    mgscoop::partner::Tick t{};
    return TickArgs(args, t);
}
std::string ReplyText(const mgscoop::partner::Reply& reply) {
    char out[mgscoop::partner::reply_capacity]{};
    const auto n = mgscoop::partner::FormatReply(reply, out, sizeof(out));
    return std::string(out, n);
}
bool TriggerText(const std::string& text, mgscoop::partner::Trigger& out) {
    return mgscoop::partner::ParseTrigger(text.data(), text.size(), out);
}
bool TriggerOk(const std::string& text) {
    mgscoop::partner::Trigger t{};
    return TriggerText(text, t);
}
std::string LogLine(const std::string& payload) {
    const std::string source = std::string(mgscoop::partner::log_prefix) + payload;
    char out[mgscoop::partner::log_line_capacity]{};
    const auto n = mgscoop::partner::FormatLogLine(source.data(), source.size(), out, sizeof(out));
    return std::string(out, n);
}
} // namespace

int main() {
    using namespace mgscoop;
    using namespace mgscoop::partner;

    // --- Classify: exact names, bytewise name == source, bounds.
    Check(Kind(cfg_source) == Receipt::cfg, "cfg");
    Check(Kind(stop_source) == Receipt::stop, "stop");
    Check(Kind(std::string(tick_prefix) + "1 2 3 4 5 6 0 0 0") == Receipt::tick, "tick");
    Check(Kind(std::string(log_prefix) + "event=started") == Receipt::log, "log");
    Check(Kind(tick_prefix) == Receipt::none && Kind(log_prefix) == Receipt::none, "prefix without payload");
    Check(Kind("return 'MGS_COOP_PARTNER_CFG_V2'") == Receipt::none, "other version");
    Check(Kind("return 'MGS_COOP_PARTNER_CFG_V1' ") == Receipt::none, "trailing space");
    Check(Kind("return 'MGS_COOP_PARTNER_V1'") == Receipt::none, "bare tick tag is not a tick");
    Check(Kind("return 'MGS_COOP_CHARACTER_V1'") == Receipt::none, "foreign receipt");
    {
        const std::string source = cfg_source;
        Check(Classify(source.data(), source.size(), "@TppMain.lua") == Receipt::none, "chunk name must equal source");
        Check(Classify(source.data(), source.size(), "return") == Receipt::none, "short name never over-read");
        const std::string longer = source + "x";
        Check(Classify(source.data(), source.size(), longer.c_str()) == Receipt::none, "longer name");
        Check(Classify(nullptr, 5, "x") == Receipt::none && Classify(source.data(), source.size(), nullptr) == Receipt::none &&
              Classify(source.data(), 0, "") == Receipt::none, "null/empty");
        std::string with_nul = source; with_nul[5] = 0;
        Check(Classify(with_nul.data(), with_nul.size(), with_nul.data()) == Receipt::none, "embedded NUL");
        const std::string at_limit = std::string(log_prefix) + std::string(max_receipt - (sizeof(log_prefix) - 1), 'a');
        Check(at_limit.size() == max_receipt && Kind(at_limit) == Receipt::log, "max_receipt accepted");
        Check(Kind(at_limit + "a") == Receipt::none, "over max_receipt refused");
    }

    // --- ParseTick: strict integers and bounds.
    Tick t{};
    Check(TickArgs("12 -1500 250 999 -17999 4500 2 3 3", t), "valid tick");
    Check(t.frame == 12 && t.x_mm == -1500 && t.y_mm == 250 && t.z_mm == 999 && t.yaw_cdeg == -17999 &&
          t.cam_cdeg == 4500 && t.stance == 2 && t.move == 3 && t.luaflags == 3, "tick fields");
    Check(TickOk("0 0 0 0 0 0 0 0 0"), "all zero");
    Check(t.action_flags == 0 && t.equip_id == 0, "legacy tick clears the optional suffix");
    Tick rich{};
    Check(TickArgs("12 -1500 250 999 -17999 4500 2 3 3 11 65535", rich) &&
          rich.action_flags == 11 && rich.equip_id == 65535 && rich.move == 3, "complete bounded gun suffix");
    Check(!TickOk("0 0 0 0 0 0 0 0 0 1"), "partial gun suffix refused");
    Check(!TickOk("0 0 0 0 0 0 0 0 0 4 1"), "locomotion bit in gun suffix refused");
    Check(!TickOk("0 0 0 0 0 0 0 0 0 12 1") && !TickOk("0 0 0 0 0 0 0 0 0 64 1"), "unknown suffix flags refused");
    Check(!TickOk("0 0 0 0 0 0 0 0 0 1 65536") && !TickOk("0 0 0 0 0 0 0 0 0 1 -1"), "equipment bounds");
    Check(!TickOk("0 0 0 0 0 0 0 0 0 1 1 0") && !TickOk("0 0 0 0 0 0 0 0 0 1 1 "), "suffix count and trailing data strict");
    rich.action_flags = 11; rich.equip_id = 65535;
    Check(TickArgs("0 0 0 0 0 0 0 0 0", rich) && rich.action_flags == 0 && rich.equip_id == 0,
          "legacy receipt after rich receipt resets gun metadata");
    rich.action_flags = 11; rich.equip_id = 65535;
    Check(!TickArgs("0 0 0 0 0 0 0 0 0 1 65536", rich) && rich.action_flags == 11 && rich.equip_id == 65535,
          "invalid optional suffix leaves output untouched");
    Check(TickOk("999999 1000000000 -1000000000 1000000000 3600000 -3600000 0 0 0"), "upper bounds");
    Check(!TickOk("1000000 0 0 0 0 0 0 0 0"), "frame bound");
    Check(!TickOk("-1 0 0 0 0 0 0 0 0"), "negative frame");
    Check(!TickOk("0 1000000001 0 0 0 0 0 0 0"), "position bound");
    Check(!TickOk("0 0 0 0 3600001 0 0 0 0"), "yaw bound");
    Check(!TickOk("0 0 0 0 0 -3600001 0 0 0"), "camera bound");
    Check(!TickOk("0 0 0 0 0 0 3 0 0"), "stance bound");
    Check(!TickOk("0 0 0 0 0 0 0 4 0"), "move bound");
    Check(!TickOk("0 0 0 0 0 0 0 0 4"), "luaflags bound");
    Check(!TickOk("0 0 0 0 0 0 0 0"), "eight values");
    Check(!TickOk("0 0 0 0 0 0 0 0 0 0"), "ten values");
    Check(!TickOk("0 0 0 0 0 0 0 0 0 "), "trailing space");
    Check(!TickOk(" 0 0 0 0 0 0 0 0 0"), "leading space");
    Check(!TickOk("0  0 0 0 0 0 0 0 0"), "double space");
    Check(!TickOk("0\t0 0 0 0 0 0 0 0"), "tab separator");
    Check(!TickOk("+1 0 0 0 0 0 0 0 0"), "plus sign");
    Check(!TickOk("01 0 0 0 0 0 0 0 0"), "leading zero");
    Check(!TickOk("0 -0 0 0 0 0 0 0 0"), "negative zero");
    Check(!TickOk("0 1.5 0 0 0 0 0 0 0"), "decimal point");
    Check(!TickOk("0 1e3 0 0 0 0 0 0 0"), "exponent");
    Check(!TickOk("0 - 0 0 0 0 0 0 0"), "lone minus");
    Check(!TickOk("0 99999999999 0 0 0 0 0 0 0"), "eleven digits");
    Check(!TickOk("0 0 0 0 0 0 0 0 0\r\n"), "trailing newline");
    Check(!TickOk("0 0x10 0 0 0 0 0 0 0"), "hex");
    Check(!TickOk(""), "no values");
    {
        Tick untouched{7, 7, 7, 7, 7, 7, 1, 1, 1};
        Check(!TickArgs("0 0 0 0 0 0 9 0 0", untouched) && untouched.frame == 7 && untouched.stance == 1, "output untouched on failure");
        const std::string wrong = std::string("return 'MGS_COOP_PARTNER_LOG_V1' -- ") + "0 0 0 0 0 0 0 0 0";
        Check(!ParseTick(wrong.data(), wrong.size(), untouched), "wrong prefix");
        Check(!ParseTick(nullptr, 40, untouched), "null");
        // What partner_sync.lua's %d format produces for typical values parses.
        char lua[160]{};
        std::snprintf(lua, sizeof(lua), "%s%d %d %d %d %d %d %d %d %d", tick_prefix, 999999, -12345678, 0, 87654321,
                      -9000, 17999, 1, 2, 1);
        Check(ParseTick(lua, std::strlen(lua), untouched) && untouched.x_mm == -12345678 && untouched.cam_cdeg == 17999,
              "Lua-shaped tick");
    }

    // --- FormatReply.
    Check(ReplyText({7, 1, -1500, 250, 3000, -3141, Status::live, 42}) == "return 7,1,-1500,250,3000,-3141,1,42", "reply text");
    Check(ReplyText({0, 1, 0, 0, 0, 0, Status::synthetic, 0}) == "return 0,1,0,0,0,0,0,0", "zero reply");
    {
        const std::string widest = ReplyText({0xFFFFFFFFu, 0xFFFFFFFFu, -2147483647 - 1, -2147483647 - 1, -2147483647 - 1,
                                          -2147483647 - 1, Status::lost, 0xFFFFFFFFu});
        // op 0xFFFFFFFF includes SET_PARTS (16) and DM_PLAY (128): the 5 look
        // values (here 0) and the gani (0) follow.
        Check(widest == "return 4294967295,4294967295,-2147483648,-2147483648,-2147483648,-2147483648,3,4294967295,0,0,0,0,0,0" &&
              widest.size() < reply_capacity, "widest reply fits reply_capacity");
        char small[16]{};
        Check(FormatReply({1, 1, 0, 0, 0, 0, Status::live, 0}, small, sizeof(small)) == 0 && small[0] == 0, "too small");
        Check(FormatReply({1, 1, 0, 0, 0, 0, Status::live, 0}, nullptr, 10) == 0, "null output");
    }

    // --- LOG payloads.
    Check(LogLine("event=started") == "partner_lua=event=started\r\n", "simple log");
    Check(LogLine("event=refused;reason=bad_index;R=nil") == "partner_lua=event=refused; reason=bad_index; R=nil\r\n", "fields expanded");
    Check(LogLine("event=error;msg=a b\"c'd") == "partner_lua=event=error; msg=a_b_c_d\r\n", "unsafe characters replaced");
    Check(LogLine(std::string(max_log_payload, 'x')).size() == 12 + max_log_payload + 2, "max payload");
    Check(LogLine(std::string(max_log_payload, ';')).size() == 12 + 2 * max_log_payload + 2, "max expansion fits");
    Check(LogLine(std::string(max_log_payload + 1, 'x')).empty(), "oversized payload refused");
    {
        char out[log_line_capacity]{};
        const std::string bare = log_prefix;
        Check(FormatLogLine(bare.data(), bare.size(), out, sizeof(out)) == 0, "empty payload");
        const std::string cfg = cfg_source;
        Check(FormatLogLine(cfg.data(), cfg.size(), out, sizeof(out)) == 0, "not a log receipt");
        char clean[8]{};
        Check(SanitizeLogPayload("a\x01z", 3, clean, sizeof(clean)) == 3 && std::strcmp(clean, "a_z") == 0, "sanitize control");
        Check(SanitizeLogPayload("abcdefgh", 8, clean, sizeof(clean)) == 0, "sanitize capacity");
        Check(SanitizeLogPayload("x", 0, clean, sizeof(clean)) == 0, "sanitize empty");
    }

    // --- Trigger grammar.
    Trigger g{};
    Check(TriggerText("stage=3 source=orbit apply=both", g) && g.has_stage && g.stage == 3 && g.has_source &&
          g.source == PartnerSource::orbit && g.has_apply && g.apply == PartnerApply::both && !g.stop, "full trigger");
    Check(TriggerText("stage=1\r\n", g) && g.has_stage && g.stage == 1 && !g.has_source && !g.has_apply, "stage only, CRLF");
    Check(TriggerText("  source=net  ", g) && g.has_source && g.source == PartnerSource::net && !g.has_stage, "source only, blanks");
    Check(TriggerText("stop", g) && g.stop && !g.has_stage, "stop");
    Check(TriggerText("stop\n", g) && g.stop, "stop newline");
    Check(TriggerText("\xEF\xBB\xBFstage=2", g) && g.stage == 2, "BOM skipped");
    Check(TriggerText("apply=slot\tparts=explicit mirror=on", g) && g.apply == PartnerApply::slot && g.parts_explicit && g.mirror_on,
          "one-shots");
    Check(!TriggerOk(""), "empty");
    Check(!TriggerOk(" \r\n"), "blank");
    Check(!TriggerOk("stage=4"), "stage bound");
    Check(!TriggerOk("stage=03") && !TriggerOk("stage=") && !TriggerOk("stage=-1"), "stage strict");
    Check(!TriggerOk("stage=1 stage=2"), "repeat");
    Check(!TriggerOk("stop stage=1") && !TriggerOk("stop stop"), "stop alone");
    Check(!TriggerOk("source=remote") && !TriggerOk("apply=teleport"), "bad values");
    Check(!TriggerOk("stage=1 bogus=1"), "unknown token");
    Check(!TriggerOk("STAGE=1"), "case sensitive");
    Check(!TriggerOk("stage=1 \xC3\xA9"), "non-ASCII");
    Check(!TriggerOk(std::string(max_trigger + 1, ' ')), "oversized");
    Check(!ParseTrigger(nullptr, 3, g), "null");
    {
        Trigger untouched{};
        untouched.stage = 9;
        Check(!TriggerText("stage=1 junk", untouched) && untouched.stage == 9, "output untouched on refusal");
    }

    // --- Stage policy.
    const auto ops = [](std::uint32_t stage, PartnerApply apply, bool pose, std::uint32_t flags = 0, bool parts = false,
                        bool mirror = false, bool reassert = false, bool mirror_off = false) {
        return OpsFor(OpInput{stage, apply, pose, flags, reassert, parts, mirror, mirror_off});
    };
    Check(ops(0, PartnerApply::warp, true) == 0, "stage 0 probe only");
    Check(ops(1, PartnerApply::warp, true) == op::warp, "stage 1 warp");
    Check(ops(2, PartnerApply::warp, true) == (op::warp | op::set_all_active), "stage 2");
    Check(ops(3, PartnerApply::warp, true) == (op::warp | op::set_all_active | op::copy_parts), "stage 3");
    Check(ops(3, PartnerApply::slot, true) == (op::set_all_active | op::copy_parts), "apply=slot removes warp");
    Check(ops(3, PartnerApply::both, true) == (op::warp | op::set_all_active | op::copy_parts), "apply=both keeps warp");
    Check(ops(3, PartnerApply::warp, false) == (op::set_all_active | op::copy_parts), "no pose, no warp (net before rx)");
    Check(ops(1, PartnerApply::warp, false) == 0, "stage 1 without pose");
    Check(ops(0, PartnerApply::warp, true, luaflag::all_active) == op::clear_all_active, "stop clears all-active");
    Check(ops(1, PartnerApply::warp, true, luaflag::all_active | luaflag::parts_copied) == (op::warp | op::clear_all_active),
          "stage 1 clears all-active");
    Check(ops(2, PartnerApply::warp, true, luaflag::all_active) == op::warp, "latched all-active: no SET, no CLEAR at stage 2");
    Check(ops(3, PartnerApply::warp, true, luaflag::all_active) == (op::warp | op::copy_parts), "latched all-active at stage 3");
    Check(ops(2, PartnerApply::warp, true, luaflag::all_active, false, false, true) == (op::warp | op::set_all_active),
          "lost bit 0x2000 re-asserts SET");
    Check(ops(1, PartnerApply::warp, true, luaflag::all_active, false, false, true) == (op::warp | op::clear_all_active),
          "no re-assert below stage 2");
    Check(ops(3, PartnerApply::warp, true, 0, true, true) ==
          (op::warp | op::set_all_active | op::copy_parts | op::set_parts_explicit | op::mirror_on), "one-shots");
    Check(ops(0, PartnerApply::warp, true, 0, true, true) == 0, "no one-shots at stage 0");
    Check(ops(0, PartnerApply::warp, true, 0, false, false, false, true) == op::mirror_off, "MIRROR_OFF at stage 0 (restore)");
    Check(ops(3, PartnerApply::warp, true, luaflag::all_active, false, false, false, true) ==
          (op::warp | op::copy_parts | op::mirror_off), "MIRROR_OFF alongside stage 3");
    Check(op::mirror_off == 64 && (op::mirror_off & (op::warp | op::set_all_active | op::copy_parts | op::clear_all_active |
          op::set_parts_explicit | op::mirror_on)) == 0, "op 64 is a new bit");
    Check(!WritesSlot(0, PartnerApply::slot, true) && !WritesSlot(3, PartnerApply::warp, true) &&
          WritesSlot(1, PartnerApply::slot, true) && WritesSlot(3, PartnerApply::both, true) &&
          !WritesSlot(3, PartnerApply::both, false), "slot write policy");

    // --- Angles and poses.
    Check(CdegToMrad(0) == 0, "0 deg");
    Check(CdegToMrad(9000) == 1571, "90 deg");
    Check(CdegToMrad(-9000) == -1571, "-90 deg");
    Check(std::abs(CdegToMrad(18000)) == 3142, "180 deg is +-pi");
    Check(CdegToMrad(27000) == -1571, "270 deg wraps to -90");
    Check(CdegToMrad(36000 + 4500) == 785, "405 deg wraps to 45");
    Check(CdegToMrad(-3600000) == 0, "-36000 deg wraps to 0");
    Check(NormalizeMrad(6283) == 0 && NormalizeMrad(-6283) == 0 && NormalizeMrad(3000) == 3000, "normalize");
    Tick local{100, 1000, 2000, 3000, 4500, 0, 0, 0, 0};
    Pose fixed = FixedPose(local, 300);
    Check(fixed.x_mm == 1000 && fixed.y_mm == 2000 && fixed.z_mm == 6000 && fixed.yaw_mrad == 785, "fixed, camera 0");
    local.cam_cdeg = 9000;
    fixed = FixedPose(local, 300);
    Check(fixed.x_mm == 4000 && fixed.z_mm == 3000, "fixed, camera 90");
    local.cam_cdeg = -18000;
    fixed = FixedPose(local, 50);
    Check(fixed.x_mm == 1000 && fixed.z_mm == 2500, "fixed, camera 180, minimum offset");
    Pose orbit = OrbitPose(local, 300, 0);
    Check(orbit.x_mm == 1000 && orbit.z_mm == 6000 && orbit.yaw_mrad == 1571, "orbit t=0");
    orbit = OrbitPose(local, 300, 2000);
    Check(orbit.x_mm == 4000 && orbit.z_mm == 3000 && std::abs(orbit.yaw_mrad) == 3142, "orbit quarter period");
    orbit = OrbitPose(local, 300, 4000 + 8000 * 5);
    Check(orbit.x_mm == 1000 && orbit.z_mm == 0 && orbit.yaw_mrad == -1571, "orbit half period, wrapped time");
    const Pose again = OrbitPose(local, 300, 2000 + 8000);
    Check(again.x_mm == 4000 && again.z_mm == 3000, "orbit period 8 s");
    Tick edge{0, 1000000000, 0, -1000000000, 0, 0, 0, 0, 0};
    const Pose clamped = FixedPose(edge, 1000);
    Check(clamped.x_mm == 1000000000 && clamped.z_mm == -999990000, "fixed clamps at the bound");
    const Pose metres = PoseFromMetres(1.2345f, -0.5f, 1000.0f, 7.0f);
    Check(metres.x_mm == 1235 || metres.x_mm == 1234, "metres x");
    Check(metres.y_mm == -500 && metres.z_mm == 1000000 && metres.yaw_mrad == 717, "metres y/z/yaw wrap");
    const Pose bad = PoseFromMetres(NAN, INFINITY, -1.0e12f, NAN);
    Check(bad.x_mm == 0 && bad.y_mm == 0 && bad.z_mm == -max_pos_mm && bad.yaw_mrad == 0, "non-finite metres");
    Check(DistanceMm(0, 0, 0, 3, 4, 0) == 5 && DistanceMm(0, 0, 0, 0, 0, 0) == 0, "distance");
    Check(DistanceMm(-max_pos_mm, -max_pos_mm, -max_pos_mm, max_pos_mm, max_pos_mm, max_pos_mm) == 3464101615u, "distance large");
    Check(DistanceMm(-2147483647 - 1, -2147483647 - 1, -2147483647 - 1, 2147483647, 2147483647, 2147483647) == 0xFFFFFFFFu,
          "distance saturates");

    // --- Transform for partner_net.
    Tick moving{5, -1500, 250, 3000, 18000, 0, 1, 3, 0};
    const auto transform = MakeTransform(moving, 77, 123456);
    Check(transform.sequence == 77 && transform.tick_ms == 123456 && transform.stance == 1 &&
          transform.action_flags == sync::ActionFlag::Sprinting && transform.equip_id == 0, "transform fields");
    Check(std::fabs(transform.x + 1.5f) < 1e-6f && std::fabs(transform.y - 0.25f) < 1e-6f && std::fabs(transform.z - 3.0f) < 1e-6f,
          "transform metres");
    Check(std::fabs(std::fabs(transform.rot_y) - 3.142f) < 1e-3f, "transform radians normalized");
    moving.move = 2;
    Check(MakeTransform(moving, 1, 0).action_flags == sync::ActionFlag::Running, "explicit running class");
    moving.move = 1; moving.action_flags = sync::ActionFlag::Aiming | sync::ActionFlag::Firing | sync::ActionFlag::Reloading;
    moving.equip_id = 30103;
    const auto gun = MakeTransform(moving, 1, 0);
    Check(gun.action_flags == (sync::ActionFlag::Walking | 11) && gun.equip_id == 30103,
          "walking and gun flags/equipment share unchanged transform");
    moving.move = 0; moving.action_flags = 255;
    Check(MakeTransform(moving, 1, 0).action_flags == 11, "transform drops untrusted suffix movement/unknown bits");
    std::uint8_t frame[sync::transform_frame_size];
    Check(sync::encode_transform_frame(0x4D4753434F4F5001ULL, 30050, transform, frame) == sync::transform_frame_size,
          "transform encodes as a frame");
    const Tick far{0, max_pos_mm, -max_pos_mm, max_pos_mm, max_angle_cdeg, 0, 2, 0, 0};
    Check(sync::encode_transform_frame(1, 1, MakeTransform(far, 1, 1), frame) == sync::transform_frame_size,
          "extreme tick still encodes");

    // --- Replies with SET_PARTS carry the 5 look values (never face_equip).
    {
        Reply with{op::set_parts_explicit | op::warp, 1, 10, 20, 30, 40, Status::live, 5, Look{6, 27, 116, 0, 0, 9}};
        Check(ReplyText(with) == "return 17,1,10,20,30,40,1,5,6,27,116,0,0", "reply with look values");
        Reply without = with;
        without.op = op::warp;
        Check(ReplyText(without) == "return 1,1,10,20,30,40,1,5", "no look values without op 16");
        const std::string widest = ReplyText({0xFFFFFFFFu & ~op::dm_play, 0xFFFFFFFFu, -2147483647 - 1, -2147483647 - 1,
                                              -2147483647 - 1, -2147483647 - 1, Status::lost, 0xFFFFFFFFu, Look{255, 255, 255, 255, 65535, 255}});
        Check(widest.size() == 111 && widest.size() < reply_capacity &&
              widest.compare(widest.size() - 22, 22, ",255,255,255,255,65535") == 0, "widest 13-value reply fits reply_capacity");
        // DM_PLAY (128) appends the gani after the 5 look values (zeros without op 16).
        Reply dmp{op::dm_play | op::warp, 1, 0, 0, 0, 0, Status::live, 0, Look{6, 27, 116, 0, 0, 9}, 3};
        Check(ReplyText(dmp) == "return 129,1,0,0,0,0,1,0,0,0,0,0,0,3", "dm_play: zeros then gani");
        Reply dmp16{op::dm_play | op::set_parts_explicit, 1, 0, 0, 0, 0, Status::live, 0, Look{6, 27, 116, 0, 0, 9}, 5};
        Check(ReplyText(dmp16) == "return 144,1,0,0,0,0,1,0,6,27,116,0,0,5", "dm_play | set_parts: look then gani");
        const std::string dmwidest = ReplyText({0xFFFFFFFFu, 1, 0, 0, 0, 0, Status::live, 0, Look{255, 255, 255, 255, 65535, 255}, 9});
        Check(dmwidest.size() < reply_capacity, "widest 14-value reply fits reply_capacity");
        // dm=map: the stand jog loop (gani 12) is the 14th value; the widest
        // reply with a two-digit gani is 114 characters.
        Reply dm12{op::dm_play, 1, 0, 0, 0, 0, Status::live, 0, Look{}, 12};
        Check(ReplyText(dm12) == "return 128,1,0,0,0,0,1,0,0,0,0,0,0,12", "dm_play: gani 12 as the 14th value");
        const std::string dm12widest = ReplyText({0xFFFFFFFFu, 0xFFFFFFFFu, -2147483647 - 1, -2147483647 - 1, -2147483647 - 1,
                                                  -2147483647 - 1, Status::lost, 0xFFFFFFFFu, Look{255, 255, 255, 255, 65535, 255}, 12});
        Check(dm12widest.size() == 114 && dm12widest.size() < reply_capacity, "widest reply (gani 12) is 114 characters");
        Check(max_gani == 30 && max_gani == mgscoop::kPartnerDmGaniMax, "max_gani follows the dm=map table bound");
    }

    // --- Look normalization (every look that reaches SetPartsInfo).
    {
        Look out{}; bool avatar = true;
        Check(NormalizeLook(Look{0, 1, 0, 1, 2, 3}, out, avatar) == nullptr && SameLook(out, Look{0, 1, 0, 1, 2, 3}) && !avatar,
              "snake look kept as is");
        Check(NormalizeLook(Look{0, 1, 0, 1, 0, 0xEE}, out, avatar) == nullptr && out.face_equip == 0xEE,
              "face_equip passes through (compared only, never an index)");
        Check(NormalizeLook(Look{6, 1, 5, 3, 7, 2}, out, avatar) == nullptr && SameLook(out, quiet_look) && !avatar,
              "quiet forced to parts 27 / camo 116 / hand 0 / face 0");
        Check(NormalizeLook(Look{5, 0, 0, 1, 1, 1}, out, avatar) == nullptr && SameLook(out, Look{5, 26, 115, 0, 0, 0}),
              "ocelot forced to parts 26 / camo 115 / hand 0");
        Check(NormalizeLook(Look{3, 2, 4, 1, 100, 5}, out, avatar) == nullptr && out.type == 0 && out.parts == 2 && out.face == 0 &&
              out.face_equip == 0 && avatar, "avatar mapped to snake with face 0 / face_equip 0 (avatar record not synced)");
        Check(NormalizeLook(Look{1, 5, 9, 1, 350, 0}, out, avatar) == nullptr && out.type == 1 && out.face == 350, "DD female/male kept");
        const Look before = out;
        Check(std::strcmp(NormalizeLook(Look{4, 0, 0, 0, 0, 0}, out, avatar), "type") == 0, "LIQUID refused");
        Check(std::strcmp(NormalizeLook(Look{7, 0, 0, 0, 0, 0}, out, avatar), "type") == 0, "type 7 refused");
        Check(std::strcmp(NormalizeLook(Look{0, 28, 0, 0, 0, 0}, out, avatar), "parts") == 0, "parts 28 refused");
        Check(std::strcmp(NormalizeLook(Look{0, 0, 117, 0, 0, 0}, out, avatar), "camo") == 0, "camo 117 refused");
        Check(std::strcmp(NormalizeLook(Look{0, 0, 0, 8, 0, 0}, out, avatar), "hand") == 0, "hand 8 refused");
        Check(std::strcmp(NormalizeLook(Look{0, 1, 0, 1, 3, 1}, out, avatar), "face") == 0 &&
              std::strcmp(NormalizeLook(Look{0, 1, 0, 1, 6, 0}, out, avatar), "face") == 0 &&
              std::strcmp(NormalizeLook(Look{0, 1, 0, 1, 0xFFFF, 0}, out, avatar), "face") == 0, "snake face index past the table refused");
        Check(SameLook(out, before), "output untouched on refusal");
        Check(NormalizeLook(Look{0, 1, 0, 1, 5, 0}, out, avatar) == nullptr && NormalizeLook(Look{0, 1, 0, 1, 2, 2}, out, avatar) == nullptr &&
              NormalizeLook(Look{0, 1, 0, 1, 5, 3}, out, avatar) == nullptr, "last table entry accepted");
        Check(SnakeFaceIndex(2, 1) == 5 && SnakeFaceIndex(2, 2) == 5 && SnakeFaceIndex(2, 3) == 2 && SnakeFaceIndex(0, 0) == 0,
              "snake face index (+3 with face_equip 1/2)");
        Check(NormalizeLook(Look{1, 5, 9, 1, 0xFFFF, 1}, out, avatar) == nullptr && out.face == 0xFFFF, "DD faces are not Snake table indexes");
        Check(quiet_look.type == 6 && quiet_look.parts == 27 && quiet_look.camo == 116 && quiet_look.hand == 0 &&
              snake_look.type == 0 && snake_look.parts == 1 && snake_look.camo == 0 && snake_look.hand == 1, "fixed test looks");
    }

    // --- Extended trigger grammar (max 255 bytes).
    {
        Trigger tg{};
        Check(max_trigger == 255, "trigger limit raised to 255");
        Check(TriggerText("stage=3 source=net move=hybrid anim=net aset=qs reset=ac look=quiet probe=ps census=1", tg) &&
              tg.has_apply && tg.apply == PartnerApply::hybrid && tg.has_anim && tg.anim == PartnerAnim::net && tg.has_aset &&
              tg.aset == PartnerAset::qs && tg.has_reset && tg.reset == PartnerReset::ac && tg.has_look && tg.look == PartnerLook::quiet &&
              tg.has_probe && tg.probe_ps && tg.census, "every new token");
        Check(TriggerText("apply=xform", tg) && tg.apply == PartnerApply::xform && !tg.has_anim && !tg.census && !tg.has_probe,
              "apply= takes the new drivers");
        Check(TriggerText("move=warp", tg) && tg.has_apply && tg.apply == PartnerApply::warp, "move= is the apply key");
        Check(!TriggerOk("move=warp apply=slot") && !TriggerOk("apply=warp move=warp"), "move and apply are one key");
        Check(TriggerText("probe=off", tg) && tg.has_probe && !tg.probe_ps, "probe=off");
        Check(TriggerText("anim=off aset=r reset=n look=copy", tg) && tg.anim == PartnerAnim::off && tg.aset == PartnerAset::r &&
              tg.reset == PartnerReset::none && tg.look == PartnerLook::copy, "the other values");
        Check(TriggerText("look=snake anim=mirror aset=s reset=a", tg) && tg.look == PartnerLook::snake && tg.anim == PartnerAnim::mirror &&
              tg.aset == PartnerAset::s && tg.reset == PartnerReset::a, "snake, mirror, s, a");
        Check(!TriggerOk("anim=on") && !TriggerOk("aset=x") && !TriggerOk("reset=b") && !TriggerOk("look=ocelot") &&
              !TriggerOk("census=2") && !TriggerOk("census") && !TriggerOk("probe=on") && !TriggerOk("move=teleport"), "bad values");
        Check(!TriggerOk("anim=net anim=off") && !TriggerOk("census=1 census=1") && !TriggerOk("probe=ps probe=off"), "repeats");
        Check(!TriggerOk("stop census=1"), "stop stays alone");
        // Round 3 tokens.
        Check(TriggerText("apply=input input=net stick=180:100 cam=sandbox hold=none yaw=rot words=rp0 bits=0x6 correct=1500 "
                          "isend=1 p218=0 padprobe=1 holdlog=0 dm=probe lookgate=1 lookrecover=1", tg) &&
              tg.apply == PartnerApply::input && tg.has_input && tg.input == PartnerInput::net && tg.has_stick &&
              tg.stick_deg == 180 && tg.stick_pct == 100 && tg.has_cam && tg.cam == PartnerInputCam::sandbox && tg.has_hold &&
              tg.hold == PartnerInputHold::none && tg.has_yaw && tg.yaw == PartnerInputYaw::rot && tg.has_words &&
              tg.words[0] == mgscoop::kPartnerWordRelease && tg.words[1] == mgscoop::kPartnerWordPress &&
              tg.words[2] == mgscoop::kPartnerWordZero && tg.has_bits && tg.bits == 0x6 && tg.has_correct &&
              tg.correct_mm == 1500 && tg.has_isend && tg.isend && tg.has_p218 && !tg.p218 && tg.has_padprobe && tg.padprobe &&
              tg.has_holdlog && !tg.holdlog && tg.has_dm && tg.dm == PartnerDm::probe && tg.has_lookgate && tg.lookgate &&
              tg.has_lookrecover && tg.lookrecover, "every round 3 token");
        Check(TriggerText("tap=0x2", tg) && tg.tap && tg.tap_bits == 0x2 && !tg.press, "tap one-shot");
        Check(TriggerText("press=0x2:1200", tg) && tg.press && tg.press_bits == 0x2 && tg.press_ms == 1200, "press one-shot");
        Check(TriggerText("dm=on dmplay=3", tg) && tg.dm == PartnerDm::on && tg.dmplay && tg.dmplay_gani == 3, "dmplay");
        Check(TriggerText("dmstop=1", tg) && tg.dmstop, "dmstop");
        Check(TriggerText("dmplay=12", tg) && tg.dmplay && tg.dmplay_gani == 12, "resident dmplay 12");
        for (unsigned id = 26; id <= 30; ++id) {
            Check(TriggerText("dmplay=" + std::to_string(id), tg) && tg.dmplay && tg.dmplay_gani == id,
                  "finite receiver probe index accepted");
        }
        // dm=map and its settings.
        Check(TriggerText("dm=map dmmap=0,12,11,5,1,3,4,4,2,6,6,6 dmspeed=300:1500:5600", tg) && tg.has_dm && tg.dm == PartnerDm::map &&
              tg.has_dmmap && tg.dmmap.cells[1] == 12 && tg.dmmap.cells[8] == 2 && tg.dmmap.cells[11] == 6 && tg.has_dmspeed &&
              tg.dmspeed.idle_mmps == 300 && tg.dmspeed.run_mmps == 1500 && tg.dmspeed.dash_mmps == 5600, "dm=map dmmap= dmspeed=");
        Check(TriggerText("dmmap=0,10,11,5,1,3,4,4,7,6,6,6", tg) && tg.has_dmmap && !tg.has_dm && !tg.has_dmspeed,
              "dm= does not swallow dmmap= (the whole prefix with '=' is compared)");
        Check(TriggerText("dmspeed=200:500:800", tg) && tg.has_dmspeed && !tg.has_dm && tg.dmspeed.dash_mmps == 800, "dmspeed alone");
        Check(!TriggerOk("dmmap=0,10,11,5,1,3,4,4,7,6,6") && !TriggerOk("dmmap=0,10,11,5,1,3,4,4,7,6,6,31") &&
              !TriggerOk("dmmap=0,10,11,5,1,3,4,4,7,6,6,06") && !TriggerOk("dmmap=0,10,11,5,1,3,4,4,7,6,6,6,") &&
              !TriggerOk("dmmap=0,10,11,5,1,3,4,4,7,6,6,6,6") && !TriggerOk("dmmap=") &&
              !TriggerOk("dmmap=0,10,11,5,1,3,4,4,7,6,6,6 dmmap=0,10,11,5,1,3,4,4,7,6,6,6"), "bad or repeated dmmap");
        Check(!TriggerOk("dmspeed=300:400:5600") && !TriggerOk("dmspeed=100:2600:5600") && !TriggerOk("dmspeed=0300:2600:5600") &&
              !TriggerOk("dmspeed=300:2600") && !TriggerOk("dmspeed=300:2600:5600:1") && !TriggerOk("dmspeed=300:2600:20001") &&
              !TriggerOk("dmspeed=300:2600:5600 dmspeed=300:2600:5600") && !TriggerOk("dm=mapx"), "bad or repeated dmspeed");
        Check(!TriggerOk("input=walk") && !TriggerOk("cam=on") && !TriggerOk("hold=mine") && !TriggerOk("yaw=left") &&
              !TriggerOk("words=xyz") && !TriggerOk("words=rp") && !TriggerOk("bits=0xZ") && !TriggerOk("bits=0x123456789") &&
              !TriggerOk("correct=10001") && !TriggerOk("stick=360:0") && !TriggerOk("stick=180:101") && !TriggerOk("stick=180") &&
              !TriggerOk("tap=2") && !TriggerOk("press=0x2:49") && !TriggerOk("press=0x2:5001") && !TriggerOk("dmplay=31") &&
              !TriggerOk("dmstop=0") && !TriggerOk("isend=2") && !TriggerOk("dm=maybe"), "bad round 3 values");
        // Round 4 tokens.
        Check(TriggerText("hold=own recon=track align=-16 slot=body runaway=disarm wrap=census place=1", tg) && tg.has_hold &&
              tg.hold == PartnerInputHold::own && tg.has_recon && tg.recon == mgscoop::PartnerRecon::track && tg.has_align &&
              tg.align_ms == -16 && tg.has_slot && tg.slot == mgscoop::PartnerSlot::body && tg.has_runaway &&
              tg.runaway == mgscoop::PartnerRunaway::disarm && tg.has_wrap && tg.wrap == mgscoop::PartnerWrap::census && tg.place,
              "every round 4 token");
        Check(TriggerText("hold=net recon=pin align=150 slot=remote runaway=off wrap=window", tg) && tg.hold == PartnerInputHold::net &&
              tg.recon == mgscoop::PartnerRecon::pin && tg.align_ms == 150 && tg.slot == mgscoop::PartnerSlot::remote &&
              tg.runaway == mgscoop::PartnerRunaway::off && tg.wrap == mgscoop::PartnerWrap::window && !tg.place, "round 4 values");
        Check(TriggerText("recon=off align=0 slot=none", tg) && tg.recon == mgscoop::PartnerRecon::off && tg.align_ms == 0 &&
              tg.slot == mgscoop::PartnerSlot::none, "recon off, align 0, slot none");
        Check(!TriggerOk("recon=on") && !TriggerOk("align=151") && !TriggerOk("align=-51") && !TriggerOk("align=-0") &&
              !TriggerOk("align=016") && !TriggerOk("align=+5") && !TriggerOk("slot=both") && !TriggerOk("runaway=warp") &&
              !TriggerOk("wrap=on") && !TriggerOk("place=0") && !TriggerOk("place=1 place=1") && !TriggerOk("align=1 align=2"),
              "bad round 4 values");
        Check(!TriggerOk("input=net input=off") && !TriggerOk("tap=0x1 tap=0x2") && !TriggerOk("dmplay=1 dmplay=2"), "round 3 repeats");
        // A long trigger within the 255-byte limit (not every token at once).
        const std::string longest =
            "stage=3 source=net apply=input anim=off aset=qs reset=ac look=net input=net stick=359:100 cam=sandbox "
            "hold=none yaw=rotneg words=rhn bits=0xffffffff correct=10000 isend=1 padprobe=1 dm=probe lookgate=1 lookrecover=1";
        Check(longest.size() + 1 <= max_trigger && TriggerOk(longest), "a long trigger fits 255 bytes");
        const std::string longest_map =
            "stage=3 source=net apply=xform reset=n anim=off input=off isend=0 look=net dm=map "
            "dmmap=12,12,12,12,12,12,12,12,12,12,12,12 dmspeed=2000:10000:20000";
        Check(longest_map.size() + 1 <= max_trigger && TriggerOk(longest_map), "a long dm=map trigger fits 255 bytes");
    }

    // --- Driver op policy: xform/hybrid warp only on demand; no copy with an explicit look.
    {
        const auto ops2 = [](std::uint32_t stage, PartnerApply apply, bool no_copy, bool transform_warp, std::uint32_t flags = 0) {
            return OpsFor(OpInput{stage, apply, true, flags, false, false, false, false, no_copy, transform_warp});
        };
        Check(ops2(3, PartnerApply::xform, false, false) == (op::set_all_active | op::copy_parts), "xform: no WARP");
        Check(ops2(3, PartnerApply::xform, false, true) == (op::warp | op::set_all_active | op::copy_parts), "xform fallback: WARP");
        Check(ops2(3, PartnerApply::hybrid, false, true, luaflag::all_active) == (op::warp | op::copy_parts), "hybrid placement WARP");
        Check(ops2(3, PartnerApply::hybrid, true, false, luaflag::all_active) == 0, "explicit look known: no COPY");
        Check(ops2(3, PartnerApply::warp, true, false, luaflag::all_active) == op::warp, "warp ignores transform_warp, keeps WARP");
        Check(ops2(3, PartnerApply::slot, false, true) == (op::set_all_active | op::copy_parts), "slot never warps");
        Check(ops2(0, PartnerApply::xform, false, true) == 0, "stage 0: nothing");
        Check(OpsFor(OpInput{3, PartnerApply::xform, false, 0, false, false, false, false, false, true}) == (op::set_all_active | op::copy_parts),
              "no pose: no WARP even when due");
        Check(WritesSlot(1, PartnerApply::xform, true) && WritesSlot(3, PartnerApply::hybrid, true) && !WritesSlot(3, PartnerApply::hybrid, false),
              "xform/hybrid write the slot like Warp");
        Check(PartnerApplyIsTransform(PartnerApply::xform) && PartnerApplyIsTransform(PartnerApply::hybrid) &&
              !PartnerApplyIsTransform(PartnerApply::both), "transform drivers");
    }

    // --- Facing quaternion (0x9C5F80 build, 0x1F0300 read back).
    {
        float q[4]{};
        YawToQuat(0.0f, q);
        Check(q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && q[3] == 1.0f, "yaw 0 is identity");
        YawToQuat(1.5707963f, q);
        Check(std::fabs(q[1] - 0.7071068f) < 1e-6f && std::fabs(q[3] - 0.7071068f) < 1e-6f && q[0] == 0.0f && q[2] == 0.0f,
              "yaw 90 deg: (0, sin 45, 0, cos 45)");
        for (float yaw : {0.0f, 0.5f, -1.0f, 2.5f, -3.0f, 3.14159f}) {
            YawToQuat(yaw, q);
            Check(std::fabs(QuatToYaw(q) - yaw) < 1e-5f, "yaw round trip");
        }
        YawToQuat(NAN, q);
        Check(q[3] == 1.0f && QuatToYaw(q) == 0.0f, "non-finite yaw: identity");
        const float bad_quat[4] = {NAN, 0, 0, 1};
        Check(QuatToYaw(bad_quat) == 0.0f, "non-finite quaternion reads 0");
    }

    // --- Stance debounce (apply on change after a hold).
    {
        StanceDebounce d{};
        Check(DebounceStance(d, 1, 1000, 150) == 0, "first value held");
        Check(DebounceStance(d, 1, 1100, 150) == 0 && DebounceStance(d, 1, 1150, 150) == 1, "due after 150 ms");
        CommitStance(d, 1);
        Check(d.applied == 1 && DebounceStance(d, 1, 1200, 150) == 0, "committed and unchanged: nothing");
        Check(DebounceStance(d, 2, 1300, 150) == 0 && DebounceStance(d, 1, 1350, 150) == 0 && DebounceStance(d, 2, 1400, 150) == 0,
              "a flicker restarts the hold");
        Check(DebounceStance(d, 2, 1549, 150) == 0 && DebounceStance(d, 2, 1550, 150) == 2, "due once held");
        CommitStance(d, 2);
        Check(DebounceStance(d, 0, 1600, 150) == 0 && DebounceStance(d, 4, 1700, 150) == 0 && d.applied == 2, "0 and 4 never applied");
        Check(DebounceStance(d, 3, 1800, 0) == 3, "hold 0 applies at once");
    }
    {
        // Handed out until committed; a refusal retries after the backoff.
        StanceDebounce d{};
        Check(DebounceStance(d, 2, 1000, 150) == 0 && DebounceStance(d, 2, 1150, 150) == 2 && d.applied == 0, "not committed by the debounce");
        Check(DebounceStance(d, 2, 1160, 150) == 2, "still due while not committed");
        RetryStance(d, 1160, 1000);
        Check(DebounceStance(d, 2, 1200, 150) == 0 && DebounceStance(d, 2, 2159, 150) == 0, "refused: held for the retry backoff");
        Check(DebounceStance(d, 2, 2160, 150) == 2, "due again after the backoff");
        RetryStance(d, 2160, 1000);
        Check(DebounceStance(d, 1, 2170, 150) == 0 && DebounceStance(d, 1, 2320, 150) == 1, "a new candidate is not held by the backoff");
        Check(DebounceStance(d, 2, 2330, 150) == 0 && DebounceStance(d, 2, 2480, 150) == 2, "back to the first candidate: fresh hold");
        CommitStance(d, 2);
        Check(d.applied == 2 && d.candidate == 0 && d.retry_ms == 0 && DebounceStance(d, 2, 2200, 150) == 0, "committed: nothing more");
        Check(DebounceStance(d, 3, 2300, 150) == 0 && DebounceStance(d, 3, 2450, 150) == 3, "a new stance after a commit");
        d = StanceDebounce{};
        Check(DebounceStance(d, 3, 3000, 150) == 0 && DebounceStance(d, 3, 3150, 150) == 3, "a reset debounce re-applies the stance");
    }
    {
        Check(!LocomotionAct(0) && LocomotionAct(1) && LocomotionAct(6) && !LocomotionAct(7) && !LocomotionAct(0x79) &&
              !LocomotionAct(0xFFFF), "locomotion act domain 1..6");
    }

    std::printf("%u partner receipt checks passed: classify, strict ticks, replies (with look values), log payloads, triggers "
                "(extended grammar), stage/driver policy, look normalization, facing quaternion, stance debounce, poses; no game calls\n",
                checks);
    return 0;
}
