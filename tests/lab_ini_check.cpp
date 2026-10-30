// Cross-check helper for tests/lab_scripts_tests.ps1 and
// tests/friend_package_tests.ps1: parses a lab.ini or coop.ini written by the
// scripts with the native parsers, validates a master URL with the native
// redirect validator, a link-secret.txt with the native secret reader, or a
// join code with the test-only parser, and prints key=value lines.
//   lab_ini_check lab <file> | coop <file> | url <url> | secret <file> | code <text> | steamcode <text>
// The code modes never print the secret.
#include "mgscoop/lab_config.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/link_protocol.hpp"
#include "mgscoop/master_server_patch.hpp"
#include "mgscoop/steam_join_code.hpp"
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
    if (std::strcmp(argv[1], "steamcode") == 0) {
        // The native MGS2 parser (the DLL's own; docs/steam-online.md 9.2).
        mgscoop::steam_code::Code code{};
        const auto status = mgscoop::steam_code::Parse(argv[2], std::strlen(argv[2]), code);
        std::printf("steamcode_status=%s\n", mgscoop::steam_code::StatusName(status));
        if (status == mgscoop::steam_code::Status::ok) std::printf("host=%llu\n", static_cast<unsigned long long>(code.host));
        mgscoop::link::SecureZero(&code, sizeof(code));
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
    // labr: as a background-worker build's loader parses it (the play rehearsal allowed).
    if (std::strcmp(argv[1], "lab") == 0 || std::strcmp(argv[1], "labr") == 0) {
        mgscoop::LabConfig c{};
        mgscoop::LabParseOptions options{};
        options.allow_play_rehearsal = std::strcmp(argv[1], "labr") == 0;
        if (!mgscoop::ParseLabConfig(text.data(), text.size(), c, options)) return 3;
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
        // Steam online keys (docs/steam-online.md 9.1), after the world keys.
        std::printf("net_transport=%s\nnet_steam_self=%llu\nnet_peer_steam=%llu\nnet_steam_wait=%d\nnet_steam_accept=%s\nsteam_selftest=%u\n",
                    mgscoop::NetTransportName(c.net_transport), static_cast<unsigned long long>(c.net_steam_self),
                    static_cast<unsigned long long>(c.net_peer_steam), c.net_steam_wait ? 1 : 0, c.net_steam_accept ? "on" : "off",
                    c.steam_selftest);
        // Online play (docs/native-puppet.md "Online"), after the Steam keys.
        char words[4]{};
        mgscoop::FormatPartnerWords(c.partner_input_words, words);
        std::printf("partner_native_profile=%s\npartner_drive=%s\npartner_input_defer=%s\npartner_input_send=%d\npartner_input_mask=0x%X\n"
                    "partner_native_gun=%d\npartner_native_fire=%s\npartner_native_aim=%s\npartner_native_rates=%s\npartner_input_words=%s\n"
                    "partner_native_net=%s\npartner_native_stall=%s\npartner_native_stall_ms=%u\npartner_native_hold=%s\n"
                    "partner_input_edge_repeats=%u\nplay_rehearsal=%d\n",
                    mgscoop::PartnerNativeProfileName(c.partner_native_profile), mgscoop::PartnerDriveName(c.partner_drive),
                    mgscoop::PartnerDeferName(c.partner_input_defer), c.partner_input_send ? 1 : 0, c.partner_input_mask,
                    c.partner_native_gun ? 1 : 0, mgscoop::PartnerNativeFireName(c.partner_native_fire),
                    mgscoop::PartnerNativeAimName(c.partner_native_aim), mgscoop::PartnerNativeRatesName(c.partner_native_rates), words,
                    mgscoop::PartnerNativeNetName(c.partner_native_net), mgscoop::PartnerNativeStallName(c.partner_native_stall),
                    c.partner_native_stall_ms, mgscoop::PartnerNativeHoldName(c.partner_native_hold), c.partner_input_edge_repeats,
                    c.play_rehearsal ? 1 : 0);
        // Section 20 and the in-game status line.
        std::printf("net_steam_dispatch=%s\nsteam_selftest_fn=%d\ncoop_hud=%s\n", mgscoop::SteamDispatchName(c.net_steam_dispatch),
                    c.steam_selftest_fn ? 1 : 0, mgscoop::CoopHudName(c.coop_hud));
        // Automatic save handoff (join-fix phase 2 P2-11): the policy and the effective travel lock.
        std::printf("save_handoff=%s\ntravel_lock=%d\n", mgscoop::SaveHandoffName(c.save_handoff), mgscoop::EffectiveTravelLock(c) ? 1 : 0);
        // WS3 vehicles (docs/vehicle-ws3.md): the census (lab only), the seat by id and RIDE_TILT (lab and play).
        std::printf("partner_ride_census=%d\npartner_ride_seat_id=%d\npartner_ride_tilt=%d\n", c.partner_ride_census ? 1 : 0,
                    c.partner_ride_seat_id ? 1 : 0, c.partner_ride_tilt ? 1 : 0);
        // F1 vehicles (docs/native-puppet.md "Vehicle boarding"): the board key (passenger with the gun profile) and whether it was named.
        std::printf("partner_ride_board=%s\npartner_ride_board_given=%d\n", mgscoop::PartnerRideBoardName(c.partner_ride_board),
                    c.partner_ride_board_given ? 1 : 0);
        // Native partner throws (docs/anim-round2-throws.md Stage B): the resolved key and whether it was named.
        std::printf("partner_native_support=%d\npartner_native_support_given=%d\n", c.partner_native_support ? 1 : 0,
                    c.partner_native_support_given ? 1 : 0);
        // WS4 v1: native placed C4 and arm holds (on with the native gun profile) and whether each was named.
        std::printf("partner_native_placed=%d\npartner_native_arm=%d\npartner_native_placed_given=%d\npartner_native_arm_given=%d\n",
                    c.partner_native_placed ? 1 : 0, c.partner_native_arm ? 1 : 0, c.partner_native_placed_given ? 1 : 0,
                    c.partner_native_arm_given ? 1 : 0);
        // WS4 v2: the place / detonate mirror (lab only, default 0).
        std::printf("partner_native_place_mirror=%d\npartner_native_place_mirror_given=%d\n", c.partner_native_place_mirror ? 1 : 0,
                    c.partner_native_place_mirror_given ? 1 : 0);
        // Traversal replay (on with the gun profile, lab and play) and whether it was named.
        std::printf("partner_native_traverse=%d\npartner_native_traverse_given=%d\n", c.partner_native_traverse ? 1 : 0,
                    c.partner_native_traverse_given ? 1 : 0);
        // F2: the native cardboard box on body 1 (lab only, default 0) and whether it was named.
        std::printf("partner_native_cbox=%d\npartner_native_cbox_given=%d\n", c.partner_native_cbox ? 1 : 0,
                    c.partner_native_cbox_given ? 1 : 0);
        // Gestures (on with the gun profile, lab and play) and whether the key was named.
        std::printf("partner_native_gesture=%d\npartner_native_gesture_given=%d\n", c.partner_native_gesture ? 1 : 0,
                    c.partner_native_gesture_given ? 1 : 0);
        // Horse / D-Walker riders (hide with the gun profile in lab; play forces off) and whether it was named.
        std::printf("partner_native_mount=%s\npartner_native_mount_given=%d\n", mgscoop::PartnerNativeMountName(c.partner_native_mount),
                    c.partner_native_mount_given ? 1 : 0);
        // G2: the back-seat aim census (lab only, default 0; play forces it off) and whether it was named.
        std::printf("partner_seat_aim=%d\npartner_seat_aim_given=%d\n", c.partner_seat_aim ? 1 : 0, c.partner_seat_aim_given ? 1 : 0);
        return 0;
    }
    if (std::strcmp(argv[1], "coop") == 0) {
        mgscoop::CoopConfig c{};
        if (!mgscoop::ParseCoopConfig(text.data(), text.size(), c)) return 3;
        std::printf("present=%d\nmaster_url=%s\ncharacter=%s\ncharacter_fallback=%s\nunknown_keys=%u\ninvalid_values=%u\nhud=%s\n",
                    c.present ? 1 : 0, c.master_url, CharacterName(c.character), CharacterName(c.character_fallback), c.unknown_keys,
                    c.invalid_values, mgscoop::CoopHudName(c.hud));
        return 0;
    }
    std::fprintf(stderr, "unknown mode %s\n", argv[1]);
    return 2;
}
