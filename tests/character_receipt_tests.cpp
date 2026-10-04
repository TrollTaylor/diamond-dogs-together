// Character-selection receipt helpers: exact classification, query formatting
// from coop.ini, bypass eligibility and bounded log lines. Pure logic only; no
// game, Lua state or Win32 call is involved.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include "mgscoop/character_receipt.hpp"
#include "mgscoop/lab_config.hpp"

namespace {
unsigned checks = 0;
void Check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
using mgscoop::character::Receipt;
using mgscoop::character::Classify;
using mgscoop::character::FormatQuery;
using mgscoop::character::FormatLogLine;
using mgscoop::character::NeedsResetBypass;
using mgscoop::character::FormatPlayersReply;

Receipt Exact(const std::string& source) { return Classify(source.data(), source.size(), source.c_str()); }

mgscoop::CoopConfig Coop(const char* text) {
    mgscoop::CoopConfig config{};
    Check(mgscoop::ParseCoopConfig(text, std::strlen(text), config), "coop.ini parses");
    return config;
}

std::string Query(const mgscoop::CoopConfig& config, bool readback) {
    char out[mgscoop::character::max_line]{};
    const auto length = FormatQuery(config, readback, out, sizeof(out));
    Check(length == std::strlen(out), "query length matches text");
    return out;
}

std::string Line(Receipt kind, const std::string& source, std::size_t capacity = mgscoop::character::max_line) {
    char out[mgscoop::character::max_line]{};
    if (!FormatLogLine(kind, source.data(), source.size(), out, capacity)) {
        Check(out[0] == 0, "refused line leaves an empty buffer");
        return "<refused>";
    }
    return out;
}

void Classification() {
    Check(Exact("return 'MGS_COOP_CHARACTER_V1'") == Receipt::query, "query receipt");
    Check(Exact("return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'") == Receipt::bypass_on, "bypass on receipt");
    Check(Exact("return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'") == Receipt::bypass_off, "bypass off receipt");
    Check(Exact("return 'MGS_COOP_CHARACTER_REPORT_V1' -- applied;mission=10036") == Receipt::report, "report receipt");
    Check(Exact("return 'MGS_COOP_CHARACTER_READBACK_V1' -- start;i0=0") == Receipt::readback, "readback receipt");
    Check(Exact("return 'MGS_COOP_CHARACTER_PLAYERS_V1'") == Receipt::players, "players guard receipt");
    Check(Exact("return 'MGS_COOP_CHARACTER_PLAYERS_V1' -- x") == Receipt::none, "players guard takes no payload");
    Check(Exact("return 'MGS_COOP_CHARACTER_PLAYERS_V2'") == Receipt::none, "players guard other version");
    const std::string players = mgscoop::character::players_source;
    Check(Classify(players.data(), players.size(), "=character.lua") == Receipt::none, "players guard other chunk name");

    const std::string query = "return 'MGS_COOP_CHARACTER_V1'";
    Check(Classify(query.data(), query.size(), "@/Assets/tpp/script/lib/TppMain.lua") == Receipt::none, "other chunk name");
    Check(Classify(query.data(), query.size(), "return 'MGS_COOP_CHARACTER_V1' ") == Receipt::none, "longer chunk name");
    Check(Classify(query.data(), query.size(), "return 'MGS_COOP") == Receipt::none, "shorter chunk name");
    Check(Classify(nullptr, query.size(), query.c_str()) == Receipt::none, "null source");
    Check(Classify(query.data(), query.size(), nullptr) == Receipt::none, "null name");
    Check(Classify(query.data(), 0, "") == Receipt::none, "empty source");
    Check(Classify(query.data(), query.size() - 1, query.substr(0, query.size() - 1).c_str()) == Receipt::none, "truncated");
    Check(Exact("return 'MGS_COOP_CHARACTER_V2'") == Receipt::none, "other version");
    Check(Exact("return 'MGS_COOP_CHARACTER_V1' -- quiet") == Receipt::none, "query takes no payload");
    Check(Exact("return 'MGS_COOP_CHARACTER_REPORT_V1' -- ") == Receipt::none, "empty report payload");
    Check(Exact("return 'MGS_COOP_CHARACTER_REPORT_V1'") == Receipt::none, "report without separator");
    Check(Exact("return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'\n") == Receipt::none, "trailing newline");
    std::string embedded = query; embedded[8] = '\0';
    Check(Classify(embedded.data(), embedded.size(), embedded.data()) == Receipt::none, "embedded terminator");

    const std::string prefix = mgscoop::character::report_prefix;
    Check(Exact(prefix + std::string(mgscoop::character::max_payload, 'a')) == Receipt::report, "maximum payload");
    Check(Exact(prefix + std::string(mgscoop::character::max_payload + 1, 'a')) == Receipt::none, "oversized payload");
}

void Queries() {
    Check(Query(mgscoop::CoopConfig{}, false) == "return '','dd_male',false", "defaults without coop.ini");
    Check(Query(Coop(""), true) == "return '','dd_male',true", "empty coop.ini with lab readback");
    Check(Query(Coop("character=default\n"), false) == "return '','dd_male',false", "explicit default");
    Check(Query(Coop("character=quiet\n"), false) == "return 'quiet','dd_male',false", "quiet with default fallback");
    Check(Query(Coop("character=quiet\ncharacter_fallback=dd_female\n"), true) == "return 'quiet','dd_female',true",
          "quiet with female fallback");
    Check(Query(Coop("character=ocelot\ncharacter_fallback=snake\n"), false) == "return 'ocelot','snake',false", "ocelot");
    Check(Query(Coop("character=avatar\n"), false) == "return 'avatar','dd_male',false", "avatar");
    Check(Query(Coop("character=dd_female\n"), false) == "return 'dd_female','dd_male',false", "dd female");
    Check(Query(Coop("character=snake\n"), false) == "return 'snake','dd_male',false", "snake");
    // coop.ini rejects Ocelot/Quiet as a fallback, so the fallback never needs the bypass.
    Check(Query(Coop("character=quiet\ncharacter_fallback=quiet\n"), false) == "return 'quiet','dd_male',false",
          "invalid fallback keeps the default");
    Check(Query(Coop("character=wolf\n"), false) == "return '','dd_male',false", "unknown character ignored");

    const auto config = Coop("character=quiet\ncharacter_fallback=dd_female\n");
    const std::string expected = "return 'quiet','dd_female',true";
    char exact[64]{};
    Check(FormatQuery(config, true, exact, expected.size() + 1) == expected.size() && expected == exact, "exact fit");
    char small[64] = "sentinel";
    Check(FormatQuery(config, true, small, expected.size()) == 0 && small[0] == 0, "one byte short refused");
    Check(FormatQuery(config, true, nullptr, 64) == 0, "null output refused");
    Check(FormatQuery(config, true, small, 0) == 0, "zero capacity refused");
}

void Bypass() {
    Check(NeedsResetBypass(Coop("character=quiet\n")), "quiet needs bypass");
    Check(NeedsResetBypass(Coop("character=ocelot\n")), "ocelot needs bypass");
    for (const char* text : {"", "character=default\n", "character=snake\n", "character=dd_male\n",
                             "character=dd_female\n", "character=avatar\n", "character=liquid\n"})
        Check(!NeedsResetBypass(Coop(text)), "other characters never need bypass");
    Check(!NeedsResetBypass(mgscoop::CoopConfig{}), "absent coop.ini never needs bypass");
}

void LogLines() {
    const std::string report = mgscoop::character::report_prefix;
    const std::string readback = mgscoop::character::readback_prefix;
    Check(Line(Receipt::report, report + "applied;mission=10036;requested=quiet;type=6") ==
          "character=applied; mission=10036; requested=quiet; type=6\r\n", "report line");
    Check(Line(Receipt::readback, readback + "change;local=1;vars=6;i0=1;i1=0;quiet_ocelot_read_as=0") ==
          "character_readback=change; local=1; vars=6; i0=1; i1=0; quiet_ocelot_read_as=0\r\n", "readback line");
    Check(Line(Receipt::report, report + "a,b:c.d-e_f") == "character=a,b:c.d-e_f\r\n", "allowed punctuation");
    // Longest guarded readback body (32-byte stage, three-digit values).
    const std::string longest = "change;local=15;vars=255;players=abcdefghijklmnopqrstuvwxyz_abcde;limit=4;i0=err;i1=err;"
                                "raw0=255;raw1=255;quiet_ocelot_read_as=0";
    Check(longest.size() <= mgscoop::character::max_payload && Line(Receipt::readback, readback + longest) != "<refused>",
          "longest readback payload fits");
    for (const char* bad : {"has space", "quote'", "dq\"", "new\nline", "cr\r", "tab\t", "pct%", "back\\slash", "\xc3\xa9"})
        Check(Line(Receipt::report, report + bad) == "<refused>", "unsafe payload refused");
    Check(Line(Receipt::readback, report + "applied") == "<refused>", "kind and prefix must agree");
    Check(Line(Receipt::query, report + "applied") == "<refused>", "non-log kind refused");
    Check(Line(Receipt::report, report) == "<refused>", "empty payload refused");
    Check(Line(Receipt::report, report + std::string(mgscoop::character::max_payload, ';')) != "<refused>",
          "maximum payload of separators fits max_line");
    Check(Line(Receipt::report, report + std::string(mgscoop::character::max_payload + 1, 'a')) == "<refused>",
          "oversized payload refused");
    Check(Line(Receipt::report, report + "applied", 12) == "<refused>", "small capacity refused");
    Check(Line(Receipt::report, report + "ab", std::strlen("character=ab\r\n") + 1) == "character=ab\r\n", "exact line fit");
    Check(!FormatLogLine(Receipt::report, nullptr, 10, nullptr, 0), "null buffers refused");
}
std::string Players(bool ready, const char* stage, unsigned limit, std::initializer_list<unsigned char> types,
                    std::size_t capacity = 64) {
    char out[64] = "sentinel";
    const auto length = FormatPlayersReply(ready, stage, limit, types.size() ? types.begin() : nullptr, types.size(),
                                           out, capacity);
    Check(length == std::strlen(out), "players reply length matches text");
    return length ? std::string(out) : "<refused>";
}

void PlayersReplies() {
    Check(Players(true, "ready", 2, {6, 1}) == "return true,2,6,1", "two instances, raw Quiet kept");
    Check(Players(true, "ready", 4, {5, 0}) == "return true,4,5,0", "four-slot limit, two sampled");
    Check(Players(true, "ready", 1, {3}) == "return true,1,3", "one instance");
    Check(Players(true, "ready", 2, {255, 254}) == "return true,2,255,254", "byte range");
    Check(Players(false, "player_absent", 0, {}) == "return false,'player_absent'", "absent player");
    Check(Players(false, "code_gate", 0, {}) == "return false,'code_gate'", "code gate");
    Check(Players(false, "lab_disabled", 0, {}) == "return false,'lab_disabled'", "lab disabled");
    for (const char* bad : {"", "Player", "a b", "x'", "quote\"", "new\nline", "abcdefghijklmnopqrstuvwxyz_abcdefg"})
        Check(Players(false, bad, 0, {}) == "return false,'unknown'", "unsafe stage replaced");
    Check(Players(false, nullptr, 0, {}) == "return false,'unknown'", "null stage replaced");
    Check(Players(false, "abcdefghijklmnopqrstuvwxyz_abcde", 0, {}) == "return false,'abcdefghijklmnopqrstuvwxyz_abcde'",
          "32-byte stage kept");
    // Inconsistent ready samples never claim readiness.
    Check(Players(true, "ready", 0, {}) == "return false,'invalid_sample'", "ready without entries");
    Check(Players(true, "ready", 5, {0, 0}) == "return false,'invalid_sample'", "limit above four");
    Check(Players(true, "ready", 0, {0}) == "return false,'invalid_sample'", "zero limit");
    Check(Players(true, "ready", 1, {0, 0}) == "return false,'invalid_sample'", "more entries than the limit");
    Check(Players(true, "ready", 4, {0, 0, 0}) == "return false,'invalid_sample'", "more than two entries");
    char out[32]{};
    Check(FormatPlayersReply(true, "ready", 2, nullptr, 2, out, sizeof(out)) > 0 &&
          std::string(out) == "return false,'invalid_sample'", "null types never ready");
    const std::string ready = "return true,2,6,1";
    Check(Players(true, "ready", 2, {6, 1}, ready.size() + 1) == ready, "exact fit");
    Check(Players(true, "ready", 2, {6, 1}, ready.size()) == "<refused>", "one byte short refused");
    Check(Players(false, "player_absent", 0, {}, 10) == "<refused>", "small capacity refused");
    Check(FormatPlayersReply(true, "ready", 2, nullptr, 0, nullptr, 64) == 0, "null output refused");
}
} // namespace

int main() {
    Classification();
    Queries();
    Bypass();
    LogLines();
    PlayersReplies();
    std::printf("%u character receipt checks passed; classification, coop.ini queries, bypass eligibility, log lines, "
                "players guard replies\n", checks);
    return 0;
}
