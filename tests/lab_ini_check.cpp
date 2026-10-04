// Cross-check helper for tests/lab_scripts_tests.ps1: parses a lab.ini or
// coop.ini written by the lab scripts with the native parsers, or validates a
// master URL with the native redirect validator, and prints key=value lines.
//   lab_ini_check lab <file> | coop <file> | url <url>
#include "mgscoop/lab_config.hpp"
#include "mgscoop/master_server_patch.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
bool ReadFile(const char* path, std::vector<char>& out) {
    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) return false;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0) out.insert(out.end(), buffer, buffer + n);
    std::fclose(file);
    return true;
}

const char* RoleName(mgscoop::LabRole role) {
    switch (role) {
    case mgscoop::LabRole::host: return "host";
    case mgscoop::LabRole::guest: return "guest";
    default: return "none";
    }
}

const char* CharacterName(mgscoop::PlayerCharacter value) {
    const char* name = mgscoop::PlayerCharacterName(value);
    return *name ? name : "default";
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: lab_ini_check lab|coop <file> | url <url>\n");
        return 2;
    }
    if (std::strcmp(argv[1], "url") == 0) {
        std::size_t length = 0;
        const auto status = mgscoop::master_server::ValidateUrl(argv[2], length);
        std::printf("url_status=%s\nlength=%zu\n", mgscoop::master_server::UrlStatusName(status), length);
        return 0;
    }
    std::vector<char> text;
    if (!ReadFile(argv[2], text)) {
        std::fprintf(stderr, "cannot read %s\n", argv[2]);
        return 2;
    }
    if (std::strcmp(argv[1], "lab") == 0) {
        mgscoop::LabConfig c{};
        if (!mgscoop::ParseLabConfig(text.data(), text.size(), c)) return 3;
        std::printf("present=%d\nrole=%s\nloopback=%d\nself_steamid=%llu\nself_name=%s\npeer_steamid=%llu\n", c.present ? 1 : 0,
                    RoleName(c.role), c.loopback ? 1 : 0, static_cast<unsigned long long>(c.self_steamid), c.self_name,
                    static_cast<unsigned long long>(c.peer_steamid));
        std::printf("listen_port=%u\npeer_port=%u\nmutex_suffix=%s\nsave_dir=%s\nxinput=%d\njoin_trigger=%d\n",
                    static_cast<unsigned>(c.listen_port), static_cast<unsigned>(c.peer_port), c.mutex_suffix, c.save_dir, c.xinput,
                    c.join_trigger ? 1 : 0);
        std::printf("auto_join_delay_s=%u\nunknown_keys=%u\ninvalid_values=%u\n", c.auto_join_delay_s, c.unknown_keys, c.invalid_values);
        return 0;
    }
    if (std::strcmp(argv[1], "coop") == 0) {
        mgscoop::CoopConfig c{};
        if (!mgscoop::ParseCoopConfig(text.data(), text.size(), c)) return 3;
        std::printf("present=%d\nmaster_url=%s\ncharacter=%s\ncharacter_fallback=%s\nunknown_keys=%u\ninvalid_values=%u\n",
                    c.present ? 1 : 0, c.master_url, CharacterName(c.character), CharacterName(c.character_fallback), c.unknown_keys,
                    c.invalid_values);
        return 0;
    }
    std::fprintf(stderr, "unknown mode %s\n", argv[1]);
    return 2;
}
