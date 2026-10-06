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
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,1,65535,15,1,4294967295") == 0,
          "CFG first seven fields preserved, unsigned session appended");
    r.link = 0; r.session = 0;
    Check(world::FormatCfgReply(r, out, sizeof(out)) && std::strcmp(out, "return 3,2,1,0,65535,15,1,0") == 0, "disconnected CFG session zero");
    char tiny[16];
    Check(!world::FormatCfgReply(r, tiny, sizeof(tiny)) && tiny[0] == 0, "CFG insufficient buffer produces no partial reply");
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
} // namespace

int main() {
    Classification();
    Cfg();
    TxParse();
    Rx();
    Log();
    Triggers();
    std::printf("world_receipt_tests: %u checks passed\n", checks);
    return 0;
}
