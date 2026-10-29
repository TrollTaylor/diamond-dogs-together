// World sync receipts and the lab world.trigger grammar
// (docs/world-sync-protocol.md "Receipts", "world.trigger"): exact
// classification, strict integers, reply widths, the LOG sanitizer.
#include "mgscoop/world_receipt.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
namespace world = mgscoop::world;
unsigned checks = 0;
void Check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "failed check %u: %s\n", checks, what); std::exit(1); }
}
world::Receipt Kind(const std::string& s) { return world::Classify(s.data(), s.size(), s.c_str()); }
world::TxStatus Tx(const std::string& args, std::uint8_t& code, std::int64_t (&v)[world::kMaxValues], unsigned& n) {
    const std::string s = std::string(world::tx_prefix) + args;
    return world::ParseTx(s.data(), s.size(), code, v, n);
}
world::TxStatus Tx(const std::string& args) {
    std::uint8_t code = 0;
    std::int64_t v[world::kMaxValues]{};
    unsigned n = 0;
    return Tx(args, code, v, n);
}
bool Trigger(const std::string& text, world::Trigger& out, std::string& canonical, std::string& reason) {
    char buffer[300]{};
    const char* why = nullptr;
    const bool ok = world::ParseTrigger(text.data(), text.size(), out, buffer, sizeof(buffer), why);
    canonical = buffer;
    reason = why ? why : "";
    return ok;
}

void Classification() {
    Check(Kind("return 'MGS_COOP_WORLD_CFG_V1' -- 30050 0") == world::Receipt::cfg, "cfg");
    Check(Kind("return 'MGS_COOP_WORLD_TX_V1' -- 1 30050 1 2 3 4") == world::Receipt::tx, "tx");
    Check(Kind("return 'MGS_COOP_WORLD_RX_V1' -- 3") == world::Receipt::rx, "rx");
    Check(Kind("return 'MGS_COOP_WORLD_LOG_V1' -- event=started") == world::Receipt::log, "log");
    Check(Kind("return 'MGS_COOP_WORLD_WARP_V1' -- 1 1116") == world::Receipt::warp, "warp");
    Check(Kind("return 'MGS_COOP_DEBUG_WARP_V1' -- 1 1116 0 0 0 0") == world::Receipt::none, "the host menu warp is not a world receipt");
    Check(Kind("return 'MGS_COOP_WORLD_CFG_V1'") == world::Receipt::none, "a prefix without args");
    Check(Kind("return 'MGS_COOP_WORLD_XX_V1' -- 1") == world::Receipt::none, "unknown receipt");
    Check(Kind("return 'MGS_COOP_PARTNER_V1' -- 1") == world::Receipt::none, "partner receipts are not world");
    const std::string ok = "return 'MGS_COOP_WORLD_RX_V1' -- 3";
    Check(world::Classify(ok.data(), ok.size(), "return 'MGS_COOP_WORLD_RX_V1' -- 1") == world::Receipt::none, "name != source");
    Check(world::Classify(ok.data(), ok.size(), (ok + "x").c_str()) == world::Receipt::none, "a longer name");
    std::string nul = ok;
    nul[10] = '\0';
    Check(world::Classify(nul.data(), nul.size(), nul.c_str()) == world::Receipt::none, "NUL inside");
    std::string big = "return 'MGS_COOP_WORLD_LOG_V1' -- " + std::string(240 - 34, 'a');
    Check(big.size() == 240 && Kind(big) == world::Receipt::log, "240 bytes");
    big += "a";
    Check(Kind(big) == world::Receipt::none, "241 bytes refused");
    Check(world::Classify(nullptr, 3, "abc") == world::Receipt::none && world::Classify("abc", 3, nullptr) == world::Receipt::none,
          "null inputs");
}

void Cfg() {
    std::uint32_t mission = 0;
    std::uint8_t phase = 9;
    const auto parse = [&](const std::string& args) {
        const std::string s = std::string(world::cfg_prefix) + args;
        return world::ParseCfg(s.data(), s.size(), mission, phase);
    };
    Check(parse("30050 0") && mission == 30050 && phase == 0, "query");
    Check(parse("30010 1") && mission == 30010 && phase == 1, "start");
    Check(parse("0 1") && mission == 0, "mission 0");
    Check(!parse("30050 2") && !parse("30050") && !parse("30050  1") && !parse("030050 1") && !parse("100000 1") && !parse("30050 1 ") &&
              !parse("-1 1") && !parse("30050 -0") && !parse("3005.0 1") && !parse("+30050 1"),
          "strict CFG");
    world::CfgReply r{3, 2, 1, 1, 65535, 15, 1, 4294967295u};
    char out[world::reply_capacity];
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,4294967295,0,0,0,0,0,0,0,0,0") == 0,
          "CFG first seven fields preserved, unsigned session appended, then remote mission, remote level, wait, handoff, lock and census");
    r.link = 0; r.session = 0;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,0,65535,15,1,0,0,0,0,0,0,0,0,0,0") == 0, "disconnected CFG session zero");
    r.link = 1; r.session = 7; r.remote_mission = 30010; r.remote_level = 3; r.wait = 9;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,0,0,0,0,0,0") == 0,
          "the partner's mission, its world level and the raw pair-wait code are r[9..11]");
    // Save handoff (join-fix phase 2 P2-1): r[12] the handoff code, r[13] the travel lock.
    r.handoff = 3; r.lock = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,0,0,0,0") == 0,
          "the handoff code and the travel lock are r[12..13]");
    // WS4 step 0 (lab key partner_placed_census): r[14] the attacker census flag.
    r.census = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,0,0,0") == 0,
          "the WS4 attacker census flag is r[14]");
    // 0.13.6 online-vehicle-gone (lab key world_vehicle_gone, lab and play): r[15] the VEHICLE_GONE module.
    r.vehicle_gone = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,1,0,0") == 0,
          "the VEHICLE_GONE module flag is r[15]");
    // M4 (lab key world_vehicle_state): r[15] is the vehicle modules bitmask, bit 2 VEHICLE_STATE.
    r.vehicle_gone = 2;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,2,0,0") == 0,
          "VEHICLE_STATE alone is r[15] = 2");
    r.vehicle_gone = 3;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,3,0,0") == 0,
          "both vehicle modules are r[15] = 3");
    // M4 review: bit 4 the wreck parity key (world_vehicle_wreck); world_link::HandleCfg composes r[15] with
    // world::VehicleModules (gone, state, wreck).
    Check(world::VehicleModules(false, false, false) == 0 && world::VehicleModules(true, false, false) == 1 &&
              world::VehicleModules(false, true, false) == 2 && world::VehicleModules(true, true, false) == 3 &&
              world::VehicleModules(false, false, true) == 4 && world::VehicleModules(true, true, true) == 7,
          "r[15] bits: 1 VEHICLE_GONE, 2 VEHICLE_STATE, 4 the wreck parity");
    r.vehicle_gone = world::VehicleModules(true, true, true);
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,0,0") == 0,
          "both vehicle modules and the wreck key are r[15] = 7");
    // M1 host-owned companion (lab key companion_host_owned, lab and play): r[16] the companion module.
    r.companion = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,0") == 0,
          "the host-owned companion module flag is r[16]");
    // M3 sortie vehicle (lab key world_relief, lab and play): r[17] the relief module.
    r.relief = 1;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,30010,3,9,3,1,1,7,1,1") == 0,
          "the sortie vehicle module flag is r[17]");
    r.remote_mission = 4294967295u; r.remote_level = 255; r.wait = 255; r.handoff = 255; r.lock = 255; r.census = 255; r.vehicle_gone = 255; r.companion = 255;
    r.relief = 255;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,7,4294967295,255,255,255,255,255,255,255,255") == 0 &&
              std::strlen(out) < world::reply_capacity,
          "the widest CFG reply fits");
    r.handoff = 0; r.lock = 0; r.census = 0; r.vehicle_gone = 0; r.companion = 0; r.relief = 0;
    char tiny[16];
    Check(!world::FormatCfgReply(r, tiny, sizeof(tiny)) && tiny[0] == 0, "CFG insufficient buffer produces no partial reply");
}

// On-screen world notes: "<code> <a> <b>", code 1..15, a and b u32, no sign.
void Note() {
    Check(Kind("return 'MGS_COOP_WORLD_NOTE_V1' -- 1 40010 1") == world::Receipt::note, "note");
    const std::string ok = "return 'MGS_COOP_WORLD_NOTE_V1' -- 2 30010 0";
    Check(world::Classify(ok.data(), ok.size(), "return 'MGS_COOP_WORLD_NOTE_V1' -- 2 30010 1") == world::Receipt::none,
          "note name != source");
    Check(Kind("return 'MGS_COOP_WORLD_NOTE_V1' -- ") == world::Receipt::none, "a note prefix without args");
    std::uint8_t code = 0;
    std::uint32_t a = 0, b = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::note_prefix) + tail;
        return world::ParseNote(s.data(), s.size(), code, a, b);
    };
    Check(parse("1 40010 1") && code == 1 && a == 40010 && b == 1, "PROGRESS story start");
    Check(parse("4 0 0") && code == 4 && a == 0 && b == 0, "zeros");
    Check(parse("15 4294967295 4294967295") && code == 15 && a == 4294967295u && b == 4294967295u, "bounds");
    code = 9; a = 9; b = 9;
    Check(!parse("0 1 1") && !parse("16 1 1") && !parse("-1 1 1") && !parse("01 1 1") && !parse("1  1 1") && !parse("1 1") &&
              !parse("1 1 1 ") && !parse("1 4294967296 0") && !parse("1 0 4294967296") && !parse("1 -1 0") && !parse("1 0 -1") &&
              !parse("1 1 1 1") && !parse("+1 1 1") && !parse("1 1.0 1") && !parse(" 1 1 1") && !parse(""),
          "strict NOTE grammar");
    Check(code == 9 && a == 9 && b == 9, "outputs untouched on failure");
    const std::string other = "return 'MGS_COOP_WORLD_WARP_V1' -- 1 2 3";
    Check(!world::ParseNote(other.data(), other.size(), code, a, b) && !world::ParseNote(nullptr, 3, code, a, b), "other receipts");
}

// The save handoff report (join-fix phase 2 P2-2): exactly 6 integers with
// per-op ranges.
void Save() {
    Check(Kind("return 'MGS_COOP_WORLD_SAVE_V1' -- 1 40050 1 2 0 0") == world::Receipt::save, "SAVE_V1 classifies");
    const std::string ok = "return 'MGS_COOP_WORLD_SAVE_V1' -- 1 40050 1 2 0 0";
    Check(world::Classify(ok.data(), ok.size(), "return 'MGS_COOP_WORLD_SAVE_V1' -- 1 40050 1 2 0 1") == world::Receipt::none,
          "save name != source");
    Check(Kind("return 'MGS_COOP_WORLD_SAVE_V1' -- ") == world::Receipt::none, "a save prefix without args");
    std::uint8_t op = 0;
    std::uint32_t a = 0, b = 0, c = 0, d = 0, e = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::save_prefix) + tail;
        return world::ParseSave(s.data(), s.size(), op, a, b, c, d, e);
    };
    Check(parse("1 40050 1 2 0 0") && op == 1 && a == 40050 && b == 1 && c == 2 && d == 0 && e == 0, "REPORT at the title");
    Check(parse("2 30010 1 2 0 0") && op == 2 && a == 30010 && b == 1 && c == 2, "CONTINUE into 30010");
    Check(parse("3 1 0 0 0 0") && op == 3 && a == 1 && b == 0, "DECLINE second continue");
    Check(parse("3 2 0 0 0 0") && op == 3 && a == 2, "DECLINE f9");
    Check(parse("1 0 0 0 1 1") && op == 1 && a == 0 && d == 1 && e == 1, "REPORT mission 0, saving, in game");
    Check(parse("1 99999 0 1 0 1") && a == 99999 && c == 1, "REPORT bounds");
    op = 9; a = b = c = d = e = 9;
    Check(!parse("1 40050 1 2 0") && !parse("1 40050 1 2 0 0 0"), "5 or 7 tokens");
    Check(!parse("01 40050 1 2 0 0") && !parse("1 040050 1 2 0 0") && !parse("1 40050 1 2 0 00"), "leading zeros");
    Check(!parse("-1 40050 1 2 0 0") && !parse("1 -1 1 2 0 0") && !parse("1 40050 -0 2 0 0") && !parse("+1 40050 1 2 0 0"), "signs");
    Check(!parse("4 0 0 0 0 0") && !parse("0 0 0 0 0 0"), "op 4 and op 0");
    Check(!parse("1 40050 1 3 0 0") && !parse("2 30010 1 3 0 0"), "story 3");
    Check(!parse("1 100000 1 2 0 0") && !parse("2 100000 1 2 0 0") && !parse("2 0 1 2 0 0"), "mission range");
    Check(!parse("1 40050  1 2 0 0") && !parse("1 40050 1 2 0 0 ") && !parse(" 1 40050 1 2 0 0") && !parse(""), "spacing");
    Check(!parse("1 40050 2 2 0 0") && !parse("1 40050 1 2 2 0") && !parse("1 40050 1 2 0 2"), "flags 0|1");
    Check(!parse("2 30010 1 2 1 0") && !parse("2 30010 1 2 0 1") && !parse("2 30010 2 2 0 0"), "CONTINUE d=e=0, title 0|1");
    Check(!parse("3 0 0 0 0 0") && !parse("3 3 0 0 0 0") && !parse("3 1 1 0 0 0") && !parse("3 1 0 0 0 1"), "DECLINE reason 1|2, zeros");
    Check(!parse("1 4294967296 1 2 0 0") && !parse("1 40050 1 2 0 0x"), "overflow and junk");
    Check(op == 9 && a == 9 && b == 9 && c == 9 && d == 9 && e == 9, "outputs untouched on failure");
    const std::string other = "return 'MGS_COOP_WORLD_NOTE_V1' -- 1 2 3";
    Check(!world::ParseSave(other.data(), other.size(), op, a, b, c, d, e) && !world::ParseSave(nullptr, 3, op, a, b, c, d, e),
          "other receipts");
}

void TxParse() {
    std::uint8_t code = 0;
    std::int64_t v[world::kMaxValues]{};
    unsigned n = 0;
    Check(Tx("1 30050 1 4294967295 10 3", code, v, n) == world::TxStatus::ok && code == 1 && n == 5 && v[2] == 4294967295LL,
          "HELLO with a u32 max hash");
    Check(Tx("16 7 1 5 1 1 0 0 0 -2147483648 2147483647 0", code, v, n) == world::TxStatus::ok && n == 11 && v[8] == -2147483648LL &&
              v[9] == 2147483647LL,
          "i32 extremes");
    Check(Tx("16 7 1 5 1 1 0 0 0 -2147483649 0 0") == world::TxStatus::bad_args, "i32 underflow");
    Check(Tx("1 30050 1 4294967296 10 3") == world::TxStatus::bad_args, "u32 overflow");
    Check(Tx("1 30050 1 -5 10 3") == world::TxStatus::bad_args, "negative u32");
    Check(Tx("1 30050 1 5 10") == world::TxStatus::bad_args && Tx("1 30050 1 5 10 3 0") == world::TxStatus::bad_args, "count per op");
    Check(Tx("1 30050 1 +5 10 3") == world::TxStatus::syntax, "'+'");
    Check(Tx("1 30050 1 05 10 3") == world::TxStatus::syntax, "leading zero");
    Check(Tx("1 30050 1 -0 10 3") == world::TxStatus::syntax, "-0");
    Check(Tx("1 30050 1 5.0 10 3") == world::TxStatus::syntax, "'.'");
    Check(Tx("1 30050 1 5  10 3") == world::TxStatus::syntax && Tx("1 30050 1 5 10 3 ") == world::TxStatus::syntax &&
              Tx(" 1 30050 1 5 10 3") == world::TxStatus::syntax,
          "single spaces, nothing trailing");
    Check(Tx("1 30050 1 12345678901 10 3") == world::TxStatus::syntax, "more than 10 digits");
    Check(Tx("") == world::TxStatus::syntax && Tx("x") == world::TxStatus::syntax, "empty / not a number");
    Check(Tx("29 1") == world::TxStatus::unknown_op && Tx("3 1 3") == world::TxStatus::unknown_op && Tx("113") == world::TxStatus::unknown_op &&
              Tx("0") == world::TxStatus::unknown_op && Tx("256") == world::TxStatus::unknown_op,
          "unknown, native-only and local ops");
    Check(Tx("120 1 2", code, v, n) == world::TxStatus::ok && code == 120 && n == 2, "GOTO_ACK is the one Lua local op");
    Check(Tx("120 3 2") == world::TxStatus::bad_args, "GOTO_ACK step range");
    Check(Tx("23 77 0 5 1 2 3 4 5 0 0 0 0 0 0 0", code, v, n) == world::TxStatus::ok && n == 15, "PHASE_VEC: 15 fields");
    Check(Tx("21 1 86400 2000 1 0 0 0 3 1") == world::TxStatus::bad_args, "clock 86400 refused");
    Check(Tx("21 1 86399 2000 255 0 0 0 3 1") == world::TxStatus::ok, "weather 255 (release)");
    Check(Tx("28 4294967295 65535 -10000000 10000000 0 -360000", code, v, n) == world::TxStatus::ok && n == 6,
          "checkpoint hash, version and bounded signed pose");
    Check(Tx("28 0 1 0 0 0 360001") == world::TxStatus::bad_args && Tx("28 0 1 0 -10000001 0 0") == world::TxStatus::bad_args,
          "checkpoint angle and position bounds");
    char out[world::reply_capacity];
    Check(world::FormatTxReply(world::TxStatus::ok, out, sizeof(out)) && std::strcmp(out, "return 1") == 0, "TX ok reply");
    Check(world::FormatTxReply(world::TxStatus::queue_full, out, sizeof(out)) && std::strcmp(out, "return 0,6") == 0, "TX refusal");
    // The longest TX source (PHASE_VEC with 15 u32 maxima) stays below 256 bytes.
    std::string longest = std::string(world::tx_prefix) + "23 192 0 12";
    for (int i = 0; i < 12; ++i) longest += " 4294967295";
    Check(longest.size() < 256 && longest.size() <= world::max_receipt, "longest TX under 240");
}

void Rx() {
    std::uint8_t mask = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::rx_prefix) + tail;
        return world::ParseRx(s.data(), s.size(), mask);
    };
    Check(parse("1") && mask == 1 && parse("3") && mask == 3, "masks 1 and 3");
    Check(!parse("2") && !parse("0") && !parse("03") && !parse("3 ") && !parse("13"), "other masks refused");
    world::RxReply r{};
    r.empty = false;
    r.queue = 1;
    r.op = 23;
    r.count = 15;
    for (unsigned i = 0; i < 15; ++i) r.v[i] = -2147483647LL - 1;
    char out[world::reply_capacity];
    const std::size_t n = world::FormatRxReply(r, out, sizeof(out));
    Check(n > 0 && n < world::reply_capacity && std::strncmp(out, "return 1,23,-2147483648,", 24) == 0, "17-value reply fits 224");
    world::RxReply empty{};
    empty.empty = true;
    empty.flags = 7;
    empty.qctrl = 2;
    empty.qworld = 300;
    empty.epoch = 9;
    Check(world::FormatRxReply(empty, out, sizeof(out)) && std::strcmp(out, "return 0,0,7,2,300,9") == 0, "empty reply");
    char tiny[16];
    Check(world::FormatRxReply(r, tiny, sizeof(tiny)) == 0 && tiny[0] == 0, "too small: nothing");
}

void Log() {
    char out[world::log_line_capacity];
    const auto line = [&](const std::string& payload) {
        const std::string s = std::string(world::log_prefix) + payload;
        return world::FormatLogLine(s.data(), s.size(), 1234, out, sizeof(out));
    };
    Check(line("event=commit;ent=4294967295;state=5") && std::strcmp(out, "world_lua=event=commit; ent=4294967295; state=5; t_ms=1234\r\n") == 0,
          "';' expanded, t_ms appended");
    Check(line("a b\"c'd") && std::strcmp(out, "world_lua=a_b_c_d; t_ms=1234\r\n") == 0, "sanitized");
    Check(line(std::string(160, 'x')) && std::strlen(out) == 10 + 160 + 13, "160 bytes");
    Check(!line(std::string(161, 'x')) && !line(""), "empty or above 160 refused");
}

void Triggers() {
    world::Trigger t{};
    std::string canonical, reason;
    Check(Trigger("goto_lz=1 goto=30020 world_probe=race_kill world=3\r\n", t, canonical, reason) && t.has_world && t.world == 3 &&
              t.has_probe && t.probe == mgscoop::WorldProbe::race_kill && t.has_goto && t.goto_target == 30020 && t.has_lz && t.lz == 1,
          "every key");
    Check(canonical == "world=3 world_probe=race_kill goto=30020 goto_lz=1", "canonical order");
    Check(Trigger("\xEF\xBB\xBF  world=0  \n", t, canonical, reason) && t.world == 0 && !t.has_goto && canonical == "world=0",
          "BOM and blanks");
    Check(Trigger("world_probe=off", t, canonical, reason) && t.has_probe && t.probe == mgscoop::WorldProbe::none &&
              canonical == "world_probe=off",
          "off cancels");
    Check(Trigger("goto=30010", t, canonical, reason) && t.goto_target == 30010 && !t.has_lz && canonical == "goto=30010", "goto");
    Check(!Trigger("world=1 world=2", t, canonical, reason) && reason == "repeat", "repeat");
    Check(!Trigger("goto_lz=1", t, canonical, reason) && reason == "token", "goto_lz needs goto");
    Check(!Trigger("", t, canonical, reason) && reason == "empty", "empty");
    Check(!Trigger(" \r\n", t, canonical, reason) && reason == "empty", "blank");
    Check(!Trigger("world=4", t, canonical, reason) && reason == "token", "world 0..3");
    Check(!Trigger("goto=30050", t, canonical, reason) && reason == "token", "goto targets");
    Check(!Trigger("world_probe=none", t, canonical, reason) && reason == "token", "none is spelled off");
    Check(!Trigger("world_probe=p2", t, canonical, reason) && reason == "token", "unknown probe");
    Check(!Trigger("stage=3", t, canonical, reason) && reason == "token", "partner keys are not world keys");
    Check(!Trigger("world", t, canonical, reason) && reason == "token", "a key without a value");
    std::string pad = "world=1" + std::string(247, ' ') + "\n";
    Check(pad.size() == 255 && Trigger(pad, t, canonical, reason), "255 bytes");
    pad.insert(0, " ");
    Check(!Trigger(pad, t, canonical, reason) && reason == "too_long", "256 bytes refused");
    const char* names[] = {"p1", "p3", "p3b", "p4", "p12", "p17", "race_kill", "remote_area_kill", "kill_near", "sleep_near",
                           "fulton_near", "alert_near", "weather_rain", "clock_jump", "clock_fast",
                           "shared_death", "host_continue", "shared_checkpoint"};
    for (unsigned i = 0; i < 15; ++i) {
        Check(Trigger(std::string("world_probe=") + names[i], t, canonical, reason) && static_cast<unsigned>(t.probe) == i + 1 &&
                  canonical == std::string("world_probe=") + names[i],
              "probe names round trip");
    }
}
// Guest spawn sync: "<seq> <goId>", strict integers, nothing else.
void Warp() {
    std::uint16_t seq = 0;
    std::uint32_t go = 0;
    const auto parse = [&](const char* tail) {
        const std::string s = std::string(world::warp_prefix) + tail;
        return world::ParseWarp(s.data(), s.size(), seq, go);
    };
    Check(parse("1 1116") && seq == 1 && go == 1116, "seq and GameObjectId");
    Check(parse("65535 0") && seq == 65535 && go == 0 && parse("7 65535") && go == 65535, "bounds");
    seq = 9; go = 9;
    Check(!parse("0 1116") && !parse("65536 1") && !parse("1 65536") && !parse("-1 1") && !parse("1 -1"), "out of range");
    Check(!parse("1") && !parse("1 2 3") && !parse("1  2") && !parse("01 2") && !parse("1 02") && !parse("1 2 ") && !parse("1.0 2") &&
              !parse("+1 2") && !parse(""),
          "strict grammar");
    Check(seq == 9 && go == 9, "outputs untouched on failure");
    const std::string other = "return 'MGS_COOP_WORLD_TX_V1' -- 1 2";
    Check(!world::ParseWarp(other.data(), other.size(), seq, go) && !world::ParseWarp(nullptr, 3, seq, go), "other receipts");
}
} // namespace

int main() {
    Classification();
    Cfg();
    TxParse();
    Rx();
    Log();
    Triggers();
    Warp();
    Note();
    Save();
    std::printf("world_receipt_tests: %u checks passed\n", checks);
    return 0;
}
