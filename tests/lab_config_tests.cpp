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
mgscoop::CoopConfig Coop(const std::string& text) {
    mgscoop::CoopConfig config{};
    Check(mgscoop::ParseCoopConfig(text.data(), text.size(), config), "coop parse usable");
    return config;
}
} // namespace

int main() {
    using namespace mgscoop;
    // Absent / disabled configurations are inert.
    Check(!Lab("").present, "empty lab is not present");
    Check(!Lab("role=guest\n").present, "role without enabled is not present");
    Check(!Lab("enabled=1\n").present, "enabled without role is not present");

    const auto guest = Lab(
        "\xEF\xBB\xBF# lab guest\r\n"
        "enabled = 1\r\n"
        "role=guest\r\n"
        "loopback=1\n"
        "self_steamid=76561202255233008\n"
        "self_name=Quiet Guest\n"
        "peer_steamid=76561198057664236\n"
        "listen_port=47811\n"
        "peer_port=47810\n"
        "mutex_suffix=-guest\n"
        "save_dir=D:\\Games\\steamapps\\common\\MGS_TPP_guest\\mgs-coop\\lab\\saves\n"
        "xinput=1\n"
        "join_trigger=yes\n"
        "auto_join_delay_s=20\n"
        "net_latency_ms=40\n"
        "net_loss_permille=5\n"
        "trace_packets=on\n"
        "future_key=1\n");
    Check(guest.present, "guest present");
    Check(guest.role == LabRole::guest, "guest role");
    Check(guest.loopback, "guest loopback");
    Check(guest.self_steamid == 76561202255233008ull, "guest fake id");
    Check(std::strcmp(guest.self_name, "Quiet Guest") == 0, "guest name");
    Check(guest.peer_steamid == 76561198057664236ull, "peer id");
    Check(guest.listen_port == 47811 && guest.peer_port == 47810, "ports");
    Check(std::strcmp(guest.mutex_suffix, "-guest") == 0, "mutex suffix");
    Check(std::strcmp(guest.save_dir, "D:\\Games\\steamapps\\common\\MGS_TPP_guest\\mgs-coop\\lab\\saves") == 0, "save dir");
    Check(guest.xinput == 1, "xinput pad");
    Check(guest.join_trigger, "join trigger");
    Check(guest.auto_join_delay_s == 20, "auto join");
    Check(guest.net_latency_ms == 40 && guest.net_loss_permille == 5, "fault injection");
    Check(guest.trace_packets, "trace");
    Check(guest.unknown_keys == 1 && guest.invalid_values == 0, "counters");

    const auto host = Lab("enabled=1\nrole=host\nself_steamid=real\npeer_steamid=learn\nxinput=none\n");
    Check(host.present && host.role == LabRole::host, "host present");
    Check(host.self_steamid == 0 && host.peer_steamid == 0, "host real identity, learned peer");
    Check(host.xinput == kXInputNone, "xinput none");
    Check(!host.loopback, "loopback off by default");

    // Invalid values are rejected individually and counted.
    const auto bad = Lab(
        "enabled=1\nrole=guest\n"
        "self_steamid=12345\n"            // not an individual SteamID64
        "peer_steamid=76561197960265728\n" // account 0
        "listen_port=80\n"                // privileged
        "peer_port=70000\n"               // out of range
        "mutex_suffix=bad suffix\n"       // space
        "xinput=7\n"
        "auto_join_delay_s=-1\n"
        "net_loss_permille=1001\n"
        "save_dir=relative\\path\n"
        "no_equals_line\n"
        "role=spectator\n");
    Check(bad.present, "still present (role kept from first valid line)");
    Check(bad.self_steamid == 0 && bad.peer_steamid == 0, "bad ids rejected");
    Check(bad.listen_port == 0 && bad.peer_port == 0, "bad ports rejected");
    Check(bad.mutex_suffix[0] == 0 && bad.save_dir[0] == 0, "bad text rejected");
    Check(bad.xinput == kXInputPassthrough, "bad xinput rejected");
    Check(bad.invalid_values == 11, "invalid counter");

    // Loopback requires two distinct ports.
    Check(!Lab("enabled=1\nrole=host\nloopback=1\nlisten_port=47810\n").loopback, "loopback needs peer port");
    Check(!Lab("enabled=1\nrole=host\nloopback=1\nlisten_port=47810\npeer_port=47810\n").loopback, "loopback needs distinct ports");
    Check(Lab("enabled=1\nrole=host\nloopback=1\nlisten_port=47810\npeer_port=47811\n").loopback, "loopback ok");

    // Oversized names are rejected rather than truncated.
    Check(Lab("enabled=1\nrole=guest\nself_name=" + std::string(64, 'x') + "\n").self_name[0] == 0, "name too long");
    Check(std::strlen(Lab("enabled=1\nrole=guest\nself_name=" + std::string(63, 'x') + "\n").self_name) == 63, "name max");

    // Unusable inputs.
    LabConfig sink{};
    Check(!ParseLabConfig(nullptr, 5, sink), "null with length");
    Check(ParseLabConfig(nullptr, 0, sink) && !sink.present, "null empty");
    const std::string huge(64 * 1024 + 1, '#');
    Check(!ParseLabConfig(huge.data(), huge.size(), sink), "too large");

    // Coop options.
    const auto coop = Coop("master_url=http://127.0.0.1:6667/tppstm/gate\ncharacter=quiet\ncharacter_fallback=dd_female\n");
    Check(coop.present, "coop present");
    Check(std::strcmp(coop.master_url, "http://127.0.0.1:6667/tppstm/gate") == 0, "master url");
    Check(coop.character == PlayerCharacter::quiet, "character quiet");
    Check(coop.character_fallback == PlayerCharacter::dd_female, "fallback");
    Check(Coop("").present && Coop("").character == PlayerCharacter::game_default, "coop defaults");
    Check(Coop("").character_fallback == PlayerCharacter::dd_male, "fallback default");
    Check(Coop("master_url=\n").master_url[0] == 0, "empty url keeps game url");
    const auto badCoop = Coop(
        "master_url=ftp://x/tppstm/gate\n"
        "master_url=http://127.0.0.1:6667/other\n"
        "master_url=http://" + std::string(60, 'a') + "/tppstm/gate\n"
        "character=d_dog\n"
        "character_fallback=quiet\n");
    Check(badCoop.master_url[0] == 0 && badCoop.invalid_values == 5, "bad coop rejected");
    Check(badCoop.character == PlayerCharacter::game_default && badCoop.character_fallback == PlayerCharacter::dd_male, "bad coop defaults");
    const std::string url69 = "http://" + std::string(69 - 7 - 12, 'a') + "/tppstm/gate";
    Check(url69.size() == 69 && std::strcmp(Coop("master_url=" + url69 + "\n").master_url, url69.c_str()) == 0, "69-char url accepted");

    // Enum mapping used by the Lua bridge.
    Check(PlayerCharacterType(PlayerCharacter::quiet) == 6 && PlayerCharacterType(PlayerCharacter::ocelot) == 5, "types");
    Check(PlayerCharacterType(PlayerCharacter::dd_female) == 2 && PlayerCharacterType(PlayerCharacter::game_default) == -1, "types 2");
    Check(std::strcmp(PlayerCharacterName(PlayerCharacter::dd_male), "dd_male") == 0, "names");
    Check(PlayerCharacterName(PlayerCharacter::game_default)[0] == 0, "default name empty");

    std::printf("%u lab/coop configuration checks passed; pure parser, no game calls\n", checks);
}
