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

    // Offline partner spike keys: defaults, values and validation.
    const auto spikeDefaults = Lab("enabled=1\nrole=host\n");
    Check(!spikeDefaults.net && !spikeDefaults.partner && spikeDefaults.net_listen_port == 0, "spike off by default");
    Check(spikeDefaults.partner_stage == 0 && spikeDefaults.partner_source == PartnerSource::fixed &&
          spikeDefaults.partner_apply == PartnerApply::warp && spikeDefaults.partner_rate_hz == 30 &&
          spikeDefaults.partner_interp_ms == 100 && spikeDefaults.partner_offset_cm == 300 &&
          spikeDefaults.partner_teardown == PartnerTeardown::clear && !spikeDefaults.partner_force_exists &&
          !spikeDefaults.partner_policy_relax && !spikeDefaults.partner_anim_probe && spikeDefaults.crash_probe,
          "spike defaults");
    const auto spike = Lab(
        "enabled=1\nrole=guest\nloopback=1\nlisten_port=47811\npeer_port=47810\n"
        "net=1\nnet_listen_port=47821\nnet_peer_port=47820\npartner=1\npartner_stage=3\npartner_source=orbit\n"
        "partner_apply=both\npartner_rate_hz=60\npartner_interp_ms=0\npartner_offset_cm=1000\npartner_teardown=keep\n"
        "partner_force_exists=1\npartner_policy_relax=yes\npartner_anim_probe=on\ncrash_probe=0\n");
    Check(spike.net && spike.net_listen_port == 47821 && spike.net_peer_port == 47820, "net ports");
    Check(spike.partner && spike.partner_stage == 3 && spike.partner_source == PartnerSource::orbit &&
          spike.partner_apply == PartnerApply::both && spike.partner_rate_hz == 60 && spike.partner_interp_ms == 0 &&
          spike.partner_offset_cm == 1000 && spike.partner_teardown == PartnerTeardown::keep, "partner values");
    Check(spike.partner_force_exists && spike.partner_policy_relax && spike.partner_anim_probe && !spike.crash_probe, "partner flags");
    Check(spike.unknown_keys == 0 && spike.invalid_values == 0, "spike keys are known and valid");
    // Role defaults for the net ports.
    const auto netHost = Lab("enabled=1\nrole=host\nnet=1\n");
    Check(netHost.net && netHost.net_listen_port == kNetHostPort && netHost.net_peer_port == kNetGuestPort, "host net defaults");
    const auto netGuest = Lab("net=1\nrole=guest\nenabled=1\n");
    Check(netGuest.net && netGuest.net_listen_port == 47821 && netGuest.net_peer_port == 47820, "guest net defaults (order independent)");
    Check(netGuest.invalid_values == 0, "defaults are not invalid");
    // Port collisions and incomplete pairs force net=0 and count one invalid value.
    const auto sameNet = Lab("enabled=1\nrole=host\nnet=1\nnet_listen_port=47820\nnet_peer_port=47820\n");
    Check(!sameNet.net && sameNet.invalid_values == 1, "net ports must differ");
    const auto onlyOne = Lab("enabled=1\nrole=host\nnet=1\nnet_listen_port=47820\n");
    Check(!onlyOne.net && onlyOne.invalid_values == 1, "net needs both ports");
    const auto clashListen = Lab("enabled=1\nrole=host\nlisten_port=47820\npeer_port=47811\nnet=1\n");
    Check(!clashListen.net && clashListen.invalid_values == 1, "net default collides with listen_port");
    const auto clashPeer = Lab("enabled=1\nrole=host\nlisten_port=47810\npeer_port=47811\nnet=1\nnet_listen_port=47830\nnet_peer_port=47811\n");
    Check(!clashPeer.net && clashPeer.invalid_values == 1, "net peer collides with peer_port");
    const auto clashCross = Lab("enabled=1\nrole=host\nlisten_port=47810\npeer_port=47811\nnet=1\nnet_listen_port=47811\nnet_peer_port=47830\n");
    Check(!clashCross.net && clashCross.invalid_values == 1, "net listen collides with peer_port");
    const auto noRole = Lab("enabled=1\nnet=1\n");
    Check(!noRole.present && !noRole.net, "net without role has no default ports");
    const auto netOffPorts = Lab("enabled=1\nrole=host\nnet=0\nnet_listen_port=47810\nnet_peer_port=47810\nlisten_port=47810\n");
    Check(!netOffPorts.net && netOffPorts.invalid_values == 0, "ports are not cross-checked while net=0");
    // Out-of-range and misspelled values are rejected individually.
    const auto badSpike = Lab(
        "enabled=1\nrole=host\npartner=2\npartner_stage=4\npartner_source=remote\npartner_apply=teleport\n"
        "partner_rate_hz=0\npartner_rate_hz=61\npartner_interp_ms=501\npartner_offset_cm=49\npartner_offset_cm=1001\n"
        "partner_teardown=drop\npartner_force_exists=maybe\ncrash_probe=-1\nnet_listen_port=80\n");
    Check(badSpike.invalid_values == 13, "bad spike values counted");
    Check(!badSpike.partner && badSpike.partner_stage == 0 && badSpike.partner_source == PartnerSource::fixed &&
          badSpike.partner_apply == PartnerApply::warp && badSpike.partner_rate_hz == 30 && badSpike.partner_interp_ms == 100 &&
          badSpike.partner_offset_cm == 300 && badSpike.partner_teardown == PartnerTeardown::clear && badSpike.crash_probe &&
          badSpike.net_listen_port == 0, "bad spike values keep defaults");
    const auto bounds = Lab("enabled=1\nrole=host\npartner_rate_hz=1\npartner_offset_cm=50\npartner_interp_ms=500\n");
    Check(bounds.partner_rate_hz == 1 && bounds.partner_offset_cm == 50 && bounds.partner_interp_ms == 500 &&
          bounds.invalid_values == 0, "inclusive bounds");
    // Animation / movement / appearance keys (inert defaults keep the proven behaviour).
    Check(spikeDefaults.partner_anim == PartnerAnim::off && spikeDefaults.partner_aset == PartnerAset::qs &&
          spikeDefaults.partner_reset == PartnerReset::none && spikeDefaults.partner_look == PartnerLook::copy,
          "anim off, aset qs, reset n, look copy by default");
    const auto animKeys = Lab("enabled=1\nrole=guest\npartner_apply=hybrid\npartner_anim=net\npartner_aset=s\npartner_reset=ac\n"
                              "partner_look=net\n");
    Check(animKeys.partner_apply == PartnerApply::hybrid && animKeys.partner_anim == PartnerAnim::net &&
          animKeys.partner_aset == PartnerAset::s && animKeys.partner_reset == PartnerReset::ac &&
          animKeys.partner_look == PartnerLook::net && animKeys.unknown_keys == 0 && animKeys.invalid_values == 0, "new keys parsed");
    const auto moveAlias = Lab("enabled=1\nrole=host\npartner_move=xform\npartner_anim=mirror\npartner_aset=r\npartner_look=quiet\n");
    Check(moveAlias.partner_apply == PartnerApply::xform && moveAlias.partner_anim == PartnerAnim::mirror &&
          moveAlias.partner_aset == PartnerAset::r && moveAlias.partner_look == PartnerLook::quiet && moveAlias.unknown_keys == 0,
          "partner_move is an alias of partner_apply");
    Check(Lab("enabled=1\nrole=host\npartner_apply=slot\npartner_move=hybrid\n").partner_apply == PartnerApply::hybrid, "the later key wins");
    const auto badAnim = Lab("enabled=1\nrole=host\npartner_anim=on\npartner_aset=x\npartner_reset=b\npartner_look=ocelot\npartner_move=fly\n");
    Check(badAnim.invalid_values == 5 && badAnim.partner_anim == PartnerAnim::off && badAnim.partner_aset == PartnerAset::qs &&
          badAnim.partner_reset == PartnerReset::none && badAnim.partner_look == PartnerLook::copy &&
          badAnim.partner_apply == PartnerApply::warp, "bad new values counted, defaults kept");
    Check(std::strcmp(PartnerApplyName(PartnerApply::xform), "xform") == 0 && std::strcmp(PartnerApplyName(PartnerApply::hybrid), "hybrid") == 0 &&
          std::strcmp(PartnerAnimName(PartnerAnim::net), "net") == 0 && std::strcmp(PartnerAsetName(PartnerAset::qs), "qs") == 0 &&
          std::strcmp(PartnerResetName(PartnerReset::none), "n") == 0 && std::strcmp(PartnerResetName(PartnerReset::ac), "ac") == 0 &&
          std::strcmp(PartnerLookName(PartnerLook::snake), "snake") == 0 && std::strcmp(PartnerLookName(PartnerLook::copy), "copy") == 0,
          "new spellings");
    {
        PartnerAnim anim{}; PartnerAset aset{}; PartnerReset reset{}; PartnerLook look{};
        Check(ParsePartnerAnim("mirror", 6, anim) && anim == PartnerAnim::mirror && !ParsePartnerAnim("mirro", 5, anim) &&
              !ParsePartnerAnim(nullptr, 1, anim), "anim parse");
        Check(ParsePartnerAset("r", 1, aset) && aset == PartnerAset::r && !ParsePartnerAset("qq", 2, aset), "aset parse");
        Check(ParsePartnerReset("c", 1, reset) && reset == PartnerReset::c && !ParsePartnerReset("none", 4, reset), "reset parse");
        Check(ParsePartnerLook("quiet", 5, look) && look == PartnerLook::quiet && !ParsePartnerLook("Quiet", 5, look), "look parse");
    }
    // Round 3 lab.ini keys.
    {
        const auto r3 = Lab("enabled=1\nrole=host\npartner_apply=input\npartner_input=net\npartner_input_cam=detect\n"
                            "partner_input_hold=pass\npartner_input_yaw=rotneg\npartner_input_words=rp0\npartner_input_mask=0x1F\n"
                            "partner_input_rstick=1\npartner_input_triggers=1\npartner_input_stick=90:30\npartner_input_correct_mm=800\n"
                            "partner_input_stale_ms=120\npartner_input_send=1\npartner_input_hook=0\npartner_input_p218_allow=1\n"
                            "partner_input_holdlog=1\npartner_pad_probe=1\npartner_look_gate=0\npartner_look_recover=0\npartner_dm=on\n");
        Check(r3.partner_apply == PartnerApply::input && r3.partner_input == PartnerInput::net &&
              r3.partner_input_cam == PartnerInputCam::detect && r3.partner_input_hold == PartnerInputHold::pass &&
              r3.partner_input_yaw == PartnerInputYaw::rotneg, "round 3 enums");
        Check(r3.partner_input_words[0] == kPartnerWordRelease && r3.partner_input_words[1] == kPartnerWordPress &&
              r3.partner_input_words[2] == kPartnerWordZero && r3.partner_input_mask == 0x1F && r3.partner_input_rstick &&
              r3.partner_input_triggers && r3.partner_input_stick_deg == 90 && r3.partner_input_stick_pct == 30, "round 3 words/mask/stick");
        Check(r3.partner_input_correct_mm == 800 && r3.partner_input_stale_ms == 120 && r3.partner_input_send &&
              !r3.partner_input_hook && r3.partner_input_p218_allow && r3.partner_input_holdlog && r3.partner_pad_probe &&
              !r3.partner_look_gate && !r3.partner_look_recover && r3.partner_dm == PartnerDm::on && r3.invalid_values == 0 &&
              r3.unknown_keys == 0, "round 3 scalars/bools");
        const auto def = Lab("enabled=1\nrole=host\n");
        Check(def.partner_input == PartnerInput::off && def.partner_input_cam == PartnerInputCam::sandbox &&
              def.partner_input_hold == PartnerInputHold::none && def.partner_input_yaw == PartnerInputYaw::rot &&
              def.partner_input_mask == 0x6 && def.partner_input_stick_deg == 180 && def.partner_input_stick_pct == 100 &&
              def.partner_input_correct_mm == 1500 && def.partner_input_stale_ms == 250 && def.partner_input_hook &&
              def.partner_look_gate && def.partner_look_recover && def.partner_dm == PartnerDm::off, "round 3 defaults");
        const auto badr3 = Lab("enabled=1\nrole=host\npartner_input=walk\npartner_input_cam=on\npartner_input_mask=0xZ\n"
                             "partner_input_words=xyz\npartner_input_stick=360:0\npartner_input_correct_mm=10001\npartner_dm=maybe\n");
        Check(badr3.invalid_values == 7 && badr3.partner_input == PartnerInput::off && badr3.partner_input_mask == 0x6 &&
              badr3.partner_dm == PartnerDm::off, "round 3 bad values counted, defaults kept");
        std::uint32_t mask = 0; std::uint8_t words[3]{}; std::uint32_t deg = 0, pct = 0;
        Check(ParsePartnerMask("6", 1, mask) && mask == 6 && ParsePartnerMask("0x6", 3, mask) && mask == 6 &&
              ParsePartnerMask("0xFFFFFFFF", 10, mask) && mask == 0xFFFFFFFFu && !ParsePartnerMask("0x", 2, mask) &&
              !ParsePartnerMask("0x1g", 4, mask), "mask parse");
        Check(ParsePartnerWords("0p0", 3, words) && words[0] == kPartnerWordZero && words[1] == kPartnerWordPress &&
              !ParsePartnerWords("0p", 2, words) && !ParsePartnerWords("0px", 3, words), "words parse");
        char wt[4]{};
        FormatPartnerWords(words, wt);
        Check(std::strcmp(wt, "0p0") == 0, "words format");
        Check(ParsePartnerStick("180:100", 7, deg, pct) && deg == 180 && pct == 100 && !ParsePartnerStick("180", 3, deg, pct) &&
              !ParsePartnerStick("360:0", 5, deg, pct) && !ParsePartnerStick("18:101", 6, deg, pct) &&
              !ParsePartnerStick("01:5", 4, deg, pct), "stick parse");
    }
    // Shared spellings.
    PartnerSource source{};
    PartnerApply apply{};
    Check(ParsePartnerSource("net", 3, source) && source == PartnerSource::net && !ParsePartnerSource("ne", 2, source) &&
          !ParsePartnerSource("nets", 4, source) && !ParsePartnerSource(nullptr, 1, source), "source parse");
    Check(ParsePartnerApply("slot", 4, apply) && apply == PartnerApply::slot && !ParsePartnerApply("", 0, apply), "apply parse");
    Check(std::strcmp(PartnerSourceName(PartnerSource::orbit), "orbit") == 0 &&
          std::strcmp(PartnerApplyName(PartnerApply::both), "both") == 0 &&
          std::strcmp(PartnerTeardownName(PartnerTeardown::keep), "keep") == 0, "names");

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
