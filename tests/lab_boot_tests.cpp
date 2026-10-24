// The developer test boot (docs/test-boot.md): lab_fast_boot / lab_boot keys,
// their play refusal and the loader's Lua receipts (mgscoop/lab_boot.hpp).
#include "mgscoop/lab_boot.hpp"
#include "mgscoop/lab_config.hpp"
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
mgscoop::LabConfig Lab(const std::string& text) {
    mgscoop::LabConfig config{};
    Check(mgscoop::ParseLabConfig(text.data(), text.size(), config), "lab parse usable");
    return config;
}
bool Boot(const char* text, mgscoop::LabBoot& out) { return mgscoop::ParseLabBoot(text, std::strlen(text), out); }
std::string Reply(bool admitted, bool fast, const mgscoop::LabBoot& boot, bool host) {
    char out[mgscoop::lab_boot::kReplyCapacity]{};
    const auto n = mgscoop::lab_boot::FormatCfgReply(admitted, fast, boot, host, out, sizeof(out));
    return n ? std::string(out, n) : std::string("<none>");
}
std::string Line(const std::string& source) {
    char out[mgscoop::lab_boot::kLogLineCapacity]{};
    const auto n = mgscoop::lab_boot::FormatLogLine(source.data(), source.size(), out, sizeof(out));
    return n ? std::string(out, n) : std::string("<none>");
}
} // namespace

int main() {
    using namespace mgscoop;
    // Defaults: nothing named, nothing happens.
    Check(!LabConfig{}.lab_fast_boot && LabConfig{}.lab_boot.mission == 0 && !Lab("").lab_fast_boot, "test boot defaults off");

    // The boot spec.
    LabBoot b{};
    Check(Boot("30020", b) && b.mission == 30020 && !b.has_pos && b.clock_s == -1 && b.weather == 0, "mission only");
    Check(Boot("30010 -510.73 5.09 1183.02 90 time=12:30 weather=sunny", b) && b.mission == 30010 && b.has_pos &&
              b.x_mm == -510730 && b.y_mm == 5090 && b.z_mm == 1183020 && b.yaw_mdeg == 90000 && b.clock_s == 12 * 3600 + 30 * 60 &&
              b.weather == 1,
          "position, time and weather");
    Check(Boot("30050 weather=rainy time=06:05", b) && b.mission == 30050 && !b.has_pos && b.clock_s == 6 * 3600 + 5 * 60 && b.weather == 3,
          "time and weather in either order");
    Check(Boot("  40050  ", b) && b.mission == 40050, "the ACC, blanks trimmed");
    Check(Boot("30020 1 2 3 -360", b) && b.x_mm == 1000 && b.yaw_mdeg == -360000, "integers and the yaw bound");
    Check(Boot("30020 100000 -100000 0.5 0.001", b) && b.x_mm == 100000000 && b.y_mm == -100000000 && b.z_mm == 500 && b.yaw_mdeg == 1,
          "coordinate bounds and decimals");
    Check(Boot("off", b) && b.mission == 0, "off clears");
    {
        LabBoot keep{};
        Check(Boot("30020", keep), "seed");
        for (const char* bad : {"", "0", "10010", "30020 1 2 3", "30020 1 2 3 4 5", "40050 1 2 3 4", "30020 100001 0 0 0", "30020 0 0 0 361",
                                "30020 1.2345 0 0 0", "30020 .5 0 0 0", "30020 1e3 0 0 0", "30020 time=24:00", "30020 time=12:60",
                                "30020 time=1:00", "30020 weather=foggy", "30020 weather=snow", "30020 time=12:00 time=13:00",
                                "30020 weather=sunny weather=rainy", "30020 extra", "30020 0 0 0 0 time=12:00 weather=sunny extra x"}) {
            Check(!ParseLabBoot(bad, std::strlen(bad), keep) && keep.mission == 30020, "an invalid spec refuses and keeps the old one");
        }
        Check(Boot("30010 weather=foggy", b) && b.weather == 5, "fog outside Africa");
        Check(std::strcmp(LabBootWeatherName(5), "foggy") == 0 && std::strcmp(LabBootWeatherName(0), "keep") == 0 &&
                  std::strcmp(LabBootWeatherName(9), "keep") == 0,
              "weather names");
    }

    // lab.ini keys in lab mode.
    const auto lab = Lab("enabled=1\nrole=host\nlab_fast_boot=1\nlab_boot=30020 -510.7 5.1 1183.0 90 time=12:00 weather=sunny\n");
    Check(lab.lab_fast_boot && lab.lab_boot.mission == 30020 && lab.lab_boot.has_pos && lab.lab_boot.clock_s == 43200 &&
              lab.invalid_values == 0 && lab.unknown_keys == 0 && lab.play_refused == 0,
          "lab mode keeps both keys");
    const auto later = Lab("enabled=1\nrole=host\nlab_fast_boot=1\nlab_fast_boot=0\nlab_boot=30020\nlab_boot=off\n");
    Check(!later.lab_fast_boot && later.lab_boot.mission == 0 && later.invalid_values == 0, "a later value wins");
    for (const char* bad : {"lab_fast_boot=2", "lab_fast_boot=", "lab_boot=31000", "lab_boot=30020 time=25:00"}) {
        const auto c = Lab(std::string("enabled=1\nrole=host\n") + bad + "\n");
        Check(!c.lab_fast_boot && c.lab_boot.mission == 0 && c.invalid_values == 1 && c.unknown_keys == 0, "an invalid key value counts");
    }
    // Play forces both off with their bit (a friend's play and the worker's play rehearsal alike).
    const std::string play = "enabled=1\nmode=play\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\n";
    const auto refused = Lab(play + "lab_fast_boot=1\nlab_boot=30020\n");
    Check(refused.mode == LabMode::play && !refused.lab_fast_boot && refused.lab_boot.mission == 0 &&
              (refused.play_refused & kPlayRefusedLabBoot) != 0 && refused.invalid_values == 1,
          "play refuses the test boot");
    const auto fastOnly = Lab(play + "lab_fast_boot=1\n");
    Check(!fastOnly.lab_fast_boot && (fastOnly.play_refused & kPlayRefusedLabBoot) != 0, "play refuses lab_fast_boot alone");
    const auto plain = Lab(play);
    Check((plain.play_refused & kPlayRefusedLabBoot) == 0 && !plain.lab_fast_boot, "a plain play lab.ini has no refusal");
    Check(kPlayRefusedLabBoot == 0x1000000u && (kPlayRefusedLabBoot & kPlayRefusedNativeProfile) == 0, "refusal bit");

    // Receipts.
    namespace lb = lab_boot;
    Check(lb::Classify(lb::kCfgSource, sizeof(lb::kCfgSource) - 1, lb::kCfgSource) == lb::Receipt::cfg, "cfg receipt");
    {
        const std::string log = std::string(lb::kLogPrefix) + "fast;step=logos;ms=1234";
        Check(lb::Classify(log.data(), log.size(), log.c_str()) == lb::Receipt::log, "log receipt");
        Check(lb::Classify(log.data(), log.size(), "other") == lb::Receipt::none, "the chunk name must equal the source");
        const std::string nul = std::string(lb::kCfgSource) + std::string(1, '\0');
        Check(lb::Classify(nul.data(), nul.size(), nullptr) == lb::Receipt::none, "no name, no receipt");
        Check(lb::Classify("return 1", 8, "return 1") == lb::Receipt::none, "other chunks pass");
    }
    lb::Gate g{true, true, true, true, true, true};
    Check(lb::Admitted(g), "the full gate admits");
    for (int i = 0; i < 6; ++i) {
        lb::Gate h = g;
        bool* fields[] = {&h.worker_compiled, &h.worker_active, &h.lab_mode, &h.lab_active, &h.offline, &h.copied_saves};
        *fields[i] = false;
        Check(!lb::Admitted(h), "every gate field is required");
    }
    Check(Reply(false, true, lab.lab_boot, true) == "return 0", "a refused gate is inert");
    Check(Reply(true, false, LabBoot{}, true) == "return 0", "nothing named is inert");
    Check(Reply(true, true, LabBoot{}, false) == "return 1,1,0,0,0,0,0,0,-1,0,2", "fast boot alone, guest");
    Check(Reply(true, true, lab.lab_boot, true) == "return 1,1,30020,1,-510700,5100,1183000,90000,43200,1,1", "the full spec, host");
    {
        char tiny[8]{};
        Check(lb::FormatCfgReply(true, true, lab.lab_boot, true, tiny, sizeof(tiny)) == 0 && !tiny[0], "a short buffer refuses");
    }
    Check(Line(std::string(lb::kLogPrefix) + "fast;step=logos;ms=1234") == "lab_boot=fast; step=logos; ms=1234\r\n", "log line");
    Check(Line(std::string(lb::kLogPrefix) + "x y<z>") == "lab_boot=x_y_z_\r\n", "the log sanitizer");
    Check(Line(std::string(lb::kLogPrefix)) == "<none>" && Line(std::string(lb::kLogPrefix) + std::string(161, 'a')) == "<none>" &&
              Line(std::string(lb::kLogPrefix) + std::string(160, ';')).size() == 9 + 320 + 2,
          "log payload bounds");
    Check(Line("return 'MGS_COOP_WORLD_LOG_V1' -- x") == "<none>", "only the boot log");

    std::printf("%u lab boot checks passed; pure parser and receipts, no game calls\n", checks);
}
