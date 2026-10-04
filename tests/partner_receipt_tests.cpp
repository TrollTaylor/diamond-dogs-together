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
        Check(widest == "return 4294967295,4294967295,-2147483648,-2147483648,-2147483648,-2147483648,3,4294967295" &&
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
    Check(MakeTransform(moving, 1, 0).action_flags == 0, "running is not sprinting");
    std::uint8_t frame[sync::transform_frame_size];
    Check(sync::encode_transform_frame(0x4D4753434F4F5001ULL, 30050, transform, frame) == sync::transform_frame_size,
          "transform encodes as a frame");
    const Tick far{0, max_pos_mm, -max_pos_mm, max_pos_mm, max_angle_cdeg, 0, 2, 0, 0};
    Check(sync::encode_transform_frame(1, 1, MakeTransform(far, 1, 1), frame) == sync::transform_frame_size,
          "extreme tick still encodes");

    std::printf("%u partner receipt checks passed: classify, strict ticks, replies, log payloads, triggers, stage policy, poses; no game calls\n",
                checks);
    return 0;
}
