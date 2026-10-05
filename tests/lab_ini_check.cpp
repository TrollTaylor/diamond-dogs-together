// Cross-check helper for tests/lab_scripts_tests.ps1 and
// tests/friend_package_tests.ps1: parses a lab.ini or coop.ini written by the
// scripts with the native parsers, validates a master URL with the native
// redirect validator, a link-secret.txt with the native secret reader, or a
// join code with the test-only parser, and prints key=value lines.
//   lab_ini_check lab <file> | coop <file> | url <url> | secret <file> | code <text>
// The code mode never prints the secret.
#include "mgscoop/lab_config.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/link_protocol.hpp"
#include "mgscoop/master_server_patch.hpp"
#include "link_join_code.hpp"
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

void Dotted(std::uint32_t address, char (&out)[16]) { mgscoop::link::FormatAddress(address, out); }
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: lab_ini_check lab|coop|secret <file> | url <url> | code <text>\n");
        return 2;
    }
    if (std::strcmp(argv[1], "url") == 0) {
        std::size_t length = 0;
        const auto status = mgscoop::master_server::ValidateUrl(argv[2], length);
        std::printf("url_status=%s\nlength=%zu\n", mgscoop::master_server::UrlStatusName(status), length);
        return 0;
    }
    if (std::strcmp(argv[1], "code") == 0) {
        mgscoop::test::JoinCode code{};
        const auto status = mgscoop::test::ParseJoinCode(argv[2], code);
        std::printf("code_status=%s\n", mgscoop::test::CodeStatusName(status));
        if (status == mgscoop::test::CodeStatus::ok) std::printf("host=%s\nport=%u\n", code.host, code.port);
        mgscoop::link::SecureZero(&code, sizeof(code));
        return 0;
    }
    std::vector<char> text;
    if (std::strcmp(argv[1], "secret") == 0) {
        char secret[mgscoop::link::kSecretChars]{};
        const auto status = !ReadFile(argv[2], text) ? mgscoop::link::SecretStatus::missing
                                                     : mgscoop::link::ParseSecretText(text.data(), text.size(), secret);
        std::printf("secret_status=%s\n", mgscoop::link::SecretStatusName(status));
        mgscoop::link::SecureZero(secret, sizeof(secret));
        return 0;
    }
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
        // Two-PC link keys (docs/link-protocol.md); link_keys=1 marks a loader
        // with them (the PowerShell cross-checks key off it).
        char bind[16]{}, peer[16]{};
        if (c.net_bind_loopback) std::snprintf(bind, sizeof(bind), "loopback");
        else if (!c.net_bind_address) std::snprintf(bind, sizeof(bind), "any");
        else Dotted(c.net_bind_address, bind);
        Dotted(c.net_peer_address, peer);
        std::printf("mode=%s\nplay_refused=0x%X\nnet=%d\nnet_link=%s\nnet_bind=%s\nnet_listen_port=%u\nnet_peer_addr=%s\n"
                    "net_peer_port=%u\n",
                    mgscoop::LabModeName(c.mode), c.play_refused, c.net ? 1 : 0, mgscoop::NetLinkName(c.net_link), bind,
                    static_cast<unsigned>(c.net_listen_port), peer, static_cast<unsigned>(c.net_peer_port));
        std::printf("net_latency_ms=%u\nnet_jitter_ms=%u\nnet_loss_permille=%u\nnet_loss_model=%s\nnet_burst=%u\n"
                    "net_dup_permille=%u\nnet_reorder_permille=%u\nnet_reorder_ms=%u\nnet_seed=%u\n",
                    c.net_latency_ms, c.net_jitter_ms, c.net_loss_permille, mgscoop::NetLossModelName(c.net_loss_model), c.net_burst,
                    c.net_dup_permille, c.net_reorder_permille, c.net_reorder_ms, c.net_seed);
        std::printf("partner=%d\npartner_interp=%s\npartner_interp_ms=%u\npartner_interp_min_ms=%u\npartner_interp_max_ms=%u\n"
                    "partner_lost=%s\nlink_keys=1\n",
                    c.partner ? 1 : 0, mgscoop::PartnerInterpName(c.partner_interp), c.partner_interp_ms, c.partner_interp_min_ms,
                    c.partner_interp_max_ms, mgscoop::PartnerLostName(c.partner_lost));
        // World sync keys (docs/world-sync-protocol.md), after link_keys=1.
        std::printf("world=%u\nworld_probe=%s\n", c.world, mgscoop::WorldProbeName(c.world_probe));
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
