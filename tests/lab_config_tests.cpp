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
              def.partner_input_hold == PartnerInputHold::own && def.partner_input_yaw == PartnerInputYaw::rot &&
              def.partner_input_mask == 0x2200006 && def.partner_input_stick_deg == 180 && def.partner_input_stick_pct == 100 &&
              def.partner_input_correct_mm == 3000 && def.partner_input_stale_ms == 250 && def.partner_input_hook &&
              def.partner_look_gate && def.partner_look_recover && def.partner_dm == PartnerDm::off, "round 3/4 defaults");
        Check(def.partner_input_words[0] == kPartnerWordHeld && def.partner_input_words[1] == kPartnerWordPress &&
              def.partner_input_words[2] == kPartnerWordRelease, "round 4 default words hpr");
        Check(def.partner_recon == PartnerRecon::track && def.partner_recon_align_ms == 0 && def.partner_recon_nudge_mmps == 600 &&
              def.partner_input_slot == PartnerSlot::body && def.partner_recon_runaway == PartnerRunaway::pin &&
              def.partner_input_wrap == PartnerWrap::off, "round 4 defaults");
        const auto r4 = Lab("enabled=1\nrole=host\npartner_input_hold=net\npartner_recon=pin\npartner_recon_align_ms=-20\n"
                            "partner_recon_nudge_mmps=900\npartner_input_slot=none\npartner_recon_runaway=disarm\npartner_input_wrap=census\n");
        Check(r4.partner_input_hold == PartnerInputHold::net && r4.partner_recon == PartnerRecon::pin && r4.partner_recon_align_ms == -20 &&
              r4.partner_recon_nudge_mmps == 900 && r4.partner_input_slot == PartnerSlot::none &&
              r4.partner_recon_runaway == PartnerRunaway::disarm && r4.partner_input_wrap == PartnerWrap::census &&
              r4.invalid_values == 0 && r4.unknown_keys == 0, "round 4 keys");
        const auto bad4 = Lab("enabled=1\nrole=host\npartner_recon=on\npartner_recon_align_ms=151\npartner_recon_align_ms=-0\n"
                              "partner_recon_nudge_mmps=99\npartner_input_slot=both\npartner_recon_runaway=warp\npartner_input_wrap=yes\n");
        Check(bad4.invalid_values == 7 && bad4.partner_recon == PartnerRecon::track && bad4.partner_recon_align_ms == 0 &&
              bad4.partner_recon_nudge_mmps == 600 && bad4.partner_input_slot == PartnerSlot::body, "round 4 bad values counted");
        // Receiver facing rate limit: default on (540 deg/s), 0 disables, 0..1440.
        Check(def.partner_facing_dps == 540, "partner_facing_dps default 540");
        const auto facing_off = Lab("enabled=1\nrole=host\npartner_facing_dps=0\n");
        const auto facing_max = Lab("enabled=1\nrole=host\npartner_facing_dps=1440\n");
        const auto facing_bad = Lab("enabled=1\nrole=host\npartner_facing_dps=1441\n");
        Check(facing_off.partner_facing_dps == 0 && facing_off.invalid_values == 0 && facing_off.unknown_keys == 0 &&
              facing_max.partner_facing_dps == 1440 && facing_bad.partner_facing_dps == 540 && facing_bad.invalid_values == 1,
              "partner_facing_dps parses 0..1440 and rejects out-of-range values");
        std::int32_t sv = 0;
        Check(ParsePartnerSigned("-50", 3, -50, 150, sv) && sv == -50 && ParsePartnerSigned("0", 1, -50, 150, sv) && sv == 0 &&
              !ParsePartnerSigned("-0", 2, -50, 150, sv) && !ParsePartnerSigned("+5", 2, -50, 150, sv) &&
              !ParsePartnerSigned("05", 2, -50, 150, sv) && !ParsePartnerSigned("-", 1, -50, 150, sv), "signed parse");
        Check(std::strcmp(PartnerInputHoldName(PartnerInputHold::own), "own") == 0 &&
              std::strcmp(PartnerReconName(PartnerRecon::track), "track") == 0 && std::strcmp(PartnerSlotName(PartnerSlot::body), "body") == 0 &&
              std::strcmp(PartnerRunawayName(PartnerRunaway::pin), "pin") == 0 &&
              std::strcmp(PartnerWrapName(PartnerWrap::window), "window") == 0, "round 4 names");
        const auto badr3 = Lab("enabled=1\nrole=host\npartner_input=walk\npartner_input_cam=on\npartner_input_mask=0xZ\n"
                             "partner_input_words=xyz\npartner_input_stick=360:0\npartner_input_correct_mm=10001\npartner_dm=maybe\n");
        Check(badr3.invalid_values == 7 && badr3.partner_input == PartnerInput::off && badr3.partner_input_mask == 0x2200006 &&
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
    // dm=map keys: partner_dm=map, the clip table and the speed bands.
    {
        const auto def = Lab("enabled=1\nrole=host\n");
        Check(std::memcmp(def.partner_dm_map.cells, kPartnerDmTableDefault.cells, kPartnerDmCells) == 0 &&
              def.partner_dm_speed.idle_mmps == 300 && def.partner_dm_speed.run_mmps == 2600 && def.partner_dm_speed.dash_mmps == 5600,
              "dm=map defaults");
        const auto map = Lab("enabled=1\nrole=host\npartner_dm=map\npartner_dm_map=0,12,11,5,1,3,4,4,2,6,6,6\n"
                             "partner_dm_speed=300:1500:5600\n");
        Check(map.partner_dm == PartnerDm::map && map.partner_dm_map.cells[1] == 12 && map.partner_dm_map.cells[8] == 2 &&
              map.partner_dm_speed.run_mmps == 1500 && map.invalid_values == 0 && map.unknown_keys == 0, "dm=map keys");
        const auto badmap = Lab("enabled=1\nrole=host\npartner_dm_map=0,10,11\npartner_dm_map=0,10,11,5,1,3,4,4,7,6,6,31\n"
                                "partner_dm_speed=300:400:5600\npartner_dm_speed=0300:2600:5600\n");
        Check(badmap.invalid_values == 4 && std::memcmp(badmap.partner_dm_map.cells, kPartnerDmTableDefault.cells, kPartnerDmCells) == 0 &&
              badmap.partner_dm_speed.run_mmps == 2600, "dm=map bad values counted, defaults kept");
        PartnerDmTable table{};
        Check(ParsePartnerDmMap("0,10,11,5,1,3,4,4,7,6,6,6", 25, table) && table.cells[0] == 0 && table.cells[2] == 11 &&
              table.cells[11] == 6, "dm map parse");
        PartnerDmTable probes{};
        const char* probe_map = "26,27,28,29,30,0,1,2,3,4,5,6";
        Check(ParsePartnerDmMap(probe_map, std::strlen(probe_map), probes) && probes.cells[0] == 26 && probes.cells[4] == 30,
              "receiver probe indices accepted by strict map parser");
        const auto probe_ini = Lab("enabled=1\nrole=host\npartner_dm_map=26,27,28,29,30,0,1,2,3,4,5,6\n");
        Check(probe_ini.invalid_values == 0 && probe_ini.partner_dm_map.cells[4] == 30 && probe_ini.partner_dm == PartnerDm::off,
              "receiver probe table parses without activating DM");
        PartnerDmTable keep = table;
        Check(!ParsePartnerDmMap("0,10,11,5,1,3,4,4,7,6,6", 23, table) && !ParsePartnerDmMap("0,10,11,5,1,3,4,4,7,6,6,6,", 26, table) &&
              !ParsePartnerDmMap("0,10,11,5,1,3,4,4,7,6,6,06", 26, table) && !ParsePartnerDmMap("0,10,11,5,1,3,4,4,7,6,6,31", 26, table) &&
              !ParsePartnerDmMap("0,10,11,5,1,3,4,4,7,6,6, 6", 26, table) && !ParsePartnerDmMap("0,100,11,5,1,3,4,4,7,6,6,6", 26, table) &&
              !ParsePartnerDmMap(",0,10,11,5,1,3,4,4,7,6,6", 24, table) && !ParsePartnerDmMap(nullptr, 1, table) &&
              std::memcmp(table.cells, keep.cells, kPartnerDmCells) == 0, "dm map refusals leave the output untouched");
        char cells[40]{};
        FormatPartnerDmMap(kPartnerDmTableDefault, cells);
        Check(std::strcmp(cells, "0,10,11,5,1,3,4,4,7,6,6,6") == 0, "dm map format");
        PartnerDmTable wide{};
        for (auto& cell : wide.cells) cell = 255;
        FormatPartnerDmMap(wide, cells);
        Check(std::strcmp(cells, "?,?,?,?,?,?,?,?,?,?,?,?") == 0, "dm map format: out of range cells");
        PartnerDmSpeeds speeds{};
        Check(ParsePartnerDmSpeeds("300:2600:5600", 13, speeds) && speeds.idle_mmps == 300 && speeds.run_mmps == 2600 &&
              speeds.dash_mmps == 5600 && ParsePartnerDmSpeeds("2000:10000:20000", 16, speeds) && speeds.dash_mmps == 20000 &&
              ParsePartnerDmSpeeds("200:500:800", 11, speeds) && speeds.run_mmps == 500, "dm speed parse (bounds)");
        Check(!ParsePartnerDmSpeeds("199:2600:5600", 13, speeds) && !ParsePartnerDmSpeeds("2001:2600:5600", 14, speeds) &&
              !ParsePartnerDmSpeeds("300:599:5600", 12, speeds) && !ParsePartnerDmSpeeds("300:10001:20000", 15, speeds) &&
              !ParsePartnerDmSpeeds("300:2600:2899", 13, speeds) && !ParsePartnerDmSpeeds("300:2600:20001", 14, speeds) &&
              !ParsePartnerDmSpeeds("300:2600", 8, speeds) && !ParsePartnerDmSpeeds("300:2600:5600:", 14, speeds) &&
              !ParsePartnerDmSpeeds("0300:2600:5600", 14, speeds) && !ParsePartnerDmSpeeds("300::5600", 9, speeds) &&
              speeds.run_mmps == 500, "dm speed refusals leave the output untouched");
        char st[24]{};
        FormatPartnerDmSpeeds(kPartnerDmSpeedsDefault, st);
        Check(std::strcmp(st, "300:2600:5600") == 0, "dm speed format");
        FormatPartnerDmSpeeds(PartnerDmSpeeds{0, 4000000000u, 20000}, st);
        Check(std::strcmp(st, "0:99999:20000") == 0, "dm speed format clamps");
        Check(std::strcmp(PartnerDmName(PartnerDm::map), "map") == 0, "dm map name");
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

    // --- Two-PC link keys (docs/link-protocol.md).
    // A lab.ini without any new key parses exactly as before; the new fields keep their defaults.
    const auto linkDefaults = Lab("enabled=1\nrole=host\nnet=1\n");
    Check(linkDefaults.mode == LabMode::lab && linkDefaults.play_refused == 0 && linkDefaults.net_link == NetLink::v2 &&
          linkDefaults.net_bind_loopback && linkDefaults.net_bind_address == 0x7F000001u &&
          linkDefaults.net_peer_address == 0x7F000001u && linkDefaults.net_jitter_ms == 0 && linkDefaults.net_dup_permille == 0 &&
          linkDefaults.net_reorder_permille == 0 && linkDefaults.net_reorder_ms == 40 && linkDefaults.net_loss_model == NetLossModel::even &&
          linkDefaults.net_burst == 2 && linkDefaults.net_seed == 1 && linkDefaults.partner_interp == PartnerInterp::fixed &&
          linkDefaults.partner_interp_min_ms == 60 && linkDefaults.partner_interp_max_ms == 350 &&
          linkDefaults.partner_lost == PartnerLost::freeze && linkDefaults.invalid_values == 0, "link defaults");
    // net_listen_port=0 on loopback counts exactly as before (one invalid value, not given).
    const auto zeroLoop = Lab("enabled=1\nrole=host\nnet=1\nnet_listen_port=0\n");
    Check(zeroLoop.net && zeroLoop.net_listen_port == kNetHostPort && zeroLoop.invalid_values == 1, "0 on loopback: invalid, defaults");
    const auto zeroLoopPeer = Lab("enabled=1\nrole=guest\nnet=1\nnet_listen_port=0\nnet_peer_port=47820\n");
    Check(!zeroLoopPeer.net && zeroLoopPeer.invalid_values == 2, "0 on loopback with a peer port: as before");
    Check(Lab("enabled=1\nrole=guest\nnet=0\nnet_listen_port=0\n").invalid_values == 1, "0 is counted with net=0 too");
    const auto keepPrevious = Lab("enabled=1\nrole=host\nnet=1\nnet_listen_port=47830\nnet_listen_port=0\nnet_peer_port=47831\n");
    Check(keepPrevious.net && keepPrevious.net_listen_port == 47830 && keepPrevious.invalid_values == 1, "an invalid 0 keeps the previous port");
    // One-PC LAN rehearsal (lab, net_bind=any).
    const auto lanHost = Lab("enabled=1\nrole=host\nnet=1\nnet_bind=any\nnet_listen_port=47820\nnet_peer_port=47821\n");
    Check(lanHost.net && !lanHost.net_bind_loopback && lanHost.net_bind_address == 0 && lanHost.net_listen_port == 47820 &&
          lanHost.invalid_values == 0, "LAN host");
    const auto lanGuest = Lab("enabled=1\nrole=guest\nnet=1\nnet_bind=any\nnet_listen_port=47821\nnet_peer_addr=192.168.1.20\n"
                              "net_peer_port=47820\n");
    Check(lanGuest.net && lanGuest.net_peer_address == 0xC0A80114u && lanGuest.net_peer_port == 47820 && lanGuest.invalid_values == 0,
          "LAN guest");
    const auto lanNoAddr = Lab("enabled=1\nrole=guest\nnet=1\nnet_bind=any\nnet_listen_port=47821\nnet_peer_port=47820\n");
    Check(!lanNoAddr.net && lanNoAddr.invalid_values == 1, "LAN guest without an address");
    const auto lanEphemeral = Lab("enabled=1\nrole=guest\nnet=1\nnet_bind=any\nnet_listen_port=0\nnet_peer_addr=10.0.0.5\n");
    Check(lanEphemeral.net && lanEphemeral.net_listen_port == 0 && lanEphemeral.net_peer_port == kNetHostPort &&
          lanEphemeral.invalid_values == 0, "a guest off loopback may listen on 0");
    const auto lanHostZero = Lab("enabled=1\nrole=host\nnet=1\nnet_bind=any\nnet_listen_port=0\n");
    Check(lanHostZero.net && lanHostZero.net_listen_port == kNetHostPort && lanHostZero.invalid_values == 1, "a host may not listen on 0");
    const auto bindIp = Lab("enabled=1\nrole=host\nnet=1\nnet_bind=192.168.1.20\n");
    Check(bindIp.net && !bindIp.net_bind_loopback && bindIp.net_bind_address == 0xC0A80114u && bindIp.invalid_values == 0, "bind address");
    const auto bindLoopIp = Lab("enabled=1\nrole=host\nnet_bind=127.0.0.1\n");
    Check(bindLoopIp.net_bind_loopback && bindLoopIp.invalid_values == 0, "127/8 bind is loopback");
    const auto loopPeerFar = Lab("enabled=1\nrole=guest\nnet_peer_addr=192.168.1.20\n");
    Check(loopPeerFar.net_peer_address == 0x7F000001u && loopPeerFar.invalid_values == 1, "a loopback bind talks to 127/8 only");
    Check(Lab("enabled=1\nrole=guest\nnet_peer_addr=127.0.0.2\n").net_peer_address == 0x7F000002u, "127/8 peer in lab");

    // Play: the launcher's host and guest files parse clean.
    const std::string saves = "save_dir=D:\\Games\\MGS_TPP\\mgs-coop\\coop-saves\n";
    const auto playHost = Lab("# Generated by mgs-coop Play-Coop.ps1 (two-PC play); removed when the game exits.\nenabled=1\nmode=play\n"
                              "role=host\n" + saves + "net=1\nnet_bind=any\nnet_listen_port=47830\npartner_interp=auto\npartner=1\n");
    Check(playHost.present && playHost.mode == LabMode::play && playHost.play_refused == 0 && playHost.invalid_values == 0 &&
          playHost.unknown_keys == 0 && playHost.net && !playHost.net_bind_loopback && playHost.net_bind_address == 0 &&
          playHost.net_listen_port == kNetPlayPort && playHost.partner_interp == PartnerInterp::auto_ && !playHost.loopback,
          "play host");
    const auto playHostDefaults = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\n");
    Check(playHostDefaults.net && !playHostDefaults.net_bind_loopback && playHostDefaults.net_listen_port == 47830 &&
          playHostDefaults.net_peer_port == 47830 && playHostDefaults.invalid_values == 0, "play host defaults");
    const auto playGuest = Lab("enabled=1\nmode=play\nrole=guest\n" + saves + "net=1\nnet_bind=any\nnet_listen_port=0\n"
                               "net_peer_addr=203.0.113.7\nnet_peer_port=47830\npartner_interp=auto\n");
    Check(playGuest.present && playGuest.net && playGuest.net_listen_port == 0 && playGuest.net_peer_address == 0xCB007107u &&
          playGuest.net_peer_port == 47830 && playGuest.play_refused == 0 && playGuest.invalid_values == 0, "play guest");
    const auto playGuestDefaults = Lab("enabled=1\nmode=play\nrole=guest\n" + saves + "net=1\nnet_peer_addr=100.101.102.103\n");
    Check(playGuestDefaults.net && playGuestDefaults.net_listen_port == 0 && playGuestDefaults.net_peer_port == 47830 &&
          !playGuestDefaults.net_bind_loopback && playGuestDefaults.invalid_values == 0, "play guest defaults (ephemeral port)");
    // mode is parsed independently of present.
    const auto modeOnly = Lab("mode=play\n");
    Check(!modeOnly.present && modeOnly.mode == LabMode::play, "mode parsed while not present");
    Check(Lab("enabled=0\nrole=host\nmode=play\n").mode == LabMode::play, "mode with enabled=0");
    // Every play refusal: forced off, its bit, one invalid value.
    const std::string playBase = "enabled=1\nmode=play\nrole=host\n" + saves + "net=1\n";
    struct Refusal { const char* line; std::uint32_t bit; };
    const Refusal refusals[] = {
        {"loopback=1\nlisten_port=47810\npeer_port=47811\n", kPlayRefusedLoopback},
        {"self_steamid=76561202255233008\n", kPlayRefusedSelfSteamId},
        {"peer_steamid=76561198057664236\n", kPlayRefusedPeerSteamId},
        {"self_name=Guest\n", kPlayRefusedSelfName},
        {"mutex_suffix=-guest\n", kPlayRefusedMutexSuffix},
        {"xinput=1\n", kPlayRefusedXInput},
        {"join_trigger=1\n", kPlayRefusedJoinTrigger},
        {"auto_join_delay_s=20\n", kPlayRefusedAutoJoin},
        {"net_peer_addr=203.0.113.7\n", kPlayRefusedHostPeerAddr},
        {"net_bind=loopback\n", kPlayRefusedBindLoopback},
        {"net_link=legacy\n", kPlayRefusedLegacyLink},
        {"net_latency_ms=40\n", kPlayRefusedImpairment},
        {"net_loss_permille=5\n", kPlayRefusedImpairment},
        {"net_jitter_ms=5\n", kPlayRefusedImpairment},
        {"net_dup_permille=5\n", kPlayRefusedImpairment},
        {"net_reorder_permille=5\n", kPlayRefusedImpairment},
    };
    for (const auto& r : refusals) {
        const auto c = Lab(playBase + r.line);
        if (c.play_refused != r.bit || c.invalid_values != 1) std::fprintf(stderr, "%s -> 0x%X, %u\n", r.line, c.play_refused, c.invalid_values);
        Check(c.play_refused == r.bit && c.invalid_values == 1 && c.present, "play refusal bit");
        Check(!c.loopback && c.self_steamid == 0 && c.peer_steamid == 0 && !c.self_name[0] && !c.mutex_suffix[0] &&
              c.xinput == kXInputPassthrough && !c.join_trigger && c.auto_join_delay_s == 0 && !c.net_bind_loopback &&
              c.net_link == NetLink::v2 && c.net_latency_ms == 0 && c.net_loss_permille == 0 && c.net_jitter_ms == 0 &&
              c.net_dup_permille == 0 && c.net_reorder_permille == 0 && c.net_peer_address == 0x7F000001u && c.net,
              "refused keys forced off");
    }
    const auto noSaves = Lab("enabled=1\nmode=play\nrole=host\nnet=1\n");
    Check(noSaves.play_refused == kPlayRefusedNoSaveDir && noSaves.invalid_values == 1 && noSaves.present, "play without save_dir");
    const auto relative = Lab("enabled=1\nmode=play\nrole=host\nsave_dir=coop-saves\nnet=1\n");
    Check(relative.play_refused == kPlayRefusedNoSaveDir && relative.invalid_values == 2, "a relative save_dir is empty, then refused");
    const std::string guestBase = "enabled=1\nmode=play\nrole=guest\n" + saves + "net=1\n";
    const auto guestNoPeer = Lab(guestBase);
    Check(guestNoPeer.play_refused == kPlayRefusedGuestNoPeer && !guestNoPeer.net && guestNoPeer.invalid_values == 1, "guest without a peer");
    const auto guestBadPeer = Lab(guestBase + "net_peer_addr=203.000.113.7\n");
    Check(guestBadPeer.play_refused == (kPlayRefusedPeerAddr | kPlayRefusedGuestNoPeer) && !guestBadPeer.net &&
          guestBadPeer.invalid_values == 2, "guest with an invalid peer");
    const auto guestLoopPeer = Lab(guestBase + "net_peer_addr=127.0.0.1\n");
    Check(guestLoopPeer.play_refused == (kPlayRefusedPeerAddr | kPlayRefusedGuestNoPeer) && !guestLoopPeer.net, "no 127/8 in play");
    const auto guestName = Lab(guestBase + "net_peer_addr=my-pc.ts.net\n");
    Check((guestName.play_refused & kPlayRefusedPeerAddr) && !guestName.net, "no host names in lab.ini");
    // The new keys and their ranges (lab).
    const auto linkKeys = Lab("enabled=1\nrole=host\nnet_jitter_ms=1000\nnet_loss_model=burst\nnet_burst=16\nnet_dup_permille=1000\n"
                              "net_reorder_permille=1000\nnet_reorder_ms=500\nnet_seed=4294967295\npartner_interp=auto\n"
                              "partner_interp_min_ms=20\npartner_interp_max_ms=1000\npartner_lost=hide\nnet_link=legacy\nmode=lab\n");
    Check(linkKeys.net_jitter_ms == 1000 && linkKeys.net_loss_model == NetLossModel::burst && linkKeys.net_burst == 16 &&
          linkKeys.net_dup_permille == 1000 && linkKeys.net_reorder_permille == 1000 && linkKeys.net_reorder_ms == 500 &&
          linkKeys.net_seed == 4294967295u && linkKeys.partner_interp == PartnerInterp::auto_ && linkKeys.partner_interp_min_ms == 20 &&
          linkKeys.partner_interp_max_ms == 1000 && linkKeys.partner_lost == PartnerLost::hide && linkKeys.net_link == NetLink::legacy &&
          linkKeys.invalid_values == 0 && linkKeys.unknown_keys == 0, "link keys at their maxima");
    const auto badLink = Lab("enabled=1\nrole=host\nnet_jitter_ms=1001\nnet_loss_model=gilbert\nnet_burst=0\nnet_burst=17\n"
                             "net_dup_permille=1001\nnet_reorder_ms=0\nnet_reorder_ms=501\nnet_seed=0\npartner_interp=adaptive\n"
                             "partner_interp_min_ms=19\npartner_interp_min_ms=501\npartner_interp_max_ms=1001\npartner_lost=drop\n"
                             "mode=lan\nnet_link=v3\nnet_bind=localhost\nnet_peer_addr=host.example\n");
    Check(badLink.invalid_values == 17 && badLink.net_jitter_ms == 0 && badLink.net_burst == 2 && badLink.net_reorder_ms == 40 &&
          badLink.net_seed == 1 && badLink.partner_interp == PartnerInterp::fixed && badLink.partner_interp_min_ms == 60 &&
          badLink.partner_interp_max_ms == 350 && badLink.partner_lost == PartnerLost::freeze && badLink.mode == LabMode::lab &&
          badLink.net_link == NetLink::v2 && badLink.net_bind_loopback, "bad link values keep defaults");
    const auto maxBelowMin = Lab("enabled=1\nrole=host\npartner_interp_min_ms=200\npartner_interp_max_ms=100\n");
    Check(maxBelowMin.partner_interp_min_ms == 200 && maxBelowMin.partner_interp_max_ms == 350 && maxBelowMin.invalid_values == 1,
          "max below min is invalid");
    // The dotted IPv4 grammar.
    std::uint32_t ip = 0;
    Check(ParseIpv4("1.2.3.4", 7, false, ip) && ip == 0x01020304u, "ipv4");
    Check(ParseIpv4("240.0.0.1", 9, false, ip) && ParseIpv4("26.1.2.3", 8, false, ip), "class E / Radmin accepted");
    const char* badIps[] = {"01.2.3.4", "1.2.3", "1.2.3.4.5", "256.1.1.1", "0.0.0.0", "255.255.255.255", "224.0.0.1",
                            "239.255.255.255", "my-pc", "::1", "[::1]", "1.2.3.4:5", " 1.2.3.4", "1..3.4", "", "1.2.3.4 ", "1234.1.1.1",
                            "1.2.3.-4"};
    for (const char* text : badIps) Check(!ParseIpv4(text, std::strlen(text), true, ip), "invalid ipv4 rejected");
    Check(!ParseIpv4("127.0.0.1", 9, false, ip) && ParseIpv4("127.0.0.1", 9, true, ip) && ip == 0x7F000001u, "127/8 only when allowed");
    Check(std::strcmp(LabModeName(LabMode::play), "play") == 0 && std::strcmp(NetLinkName(NetLink::legacy), "legacy") == 0 &&
          std::strcmp(PartnerInterpName(PartnerInterp::auto_), "auto") == 0 && std::strcmp(PartnerLostName(PartnerLost::hide), "hide") == 0 &&
          std::strcmp(NetLossModelName(NetLossModel::burst), "burst") == 0 && std::strcmp(LabModeName(LabMode::lab), "lab") == 0,
          "link key names");

    // World sync (docs/world-sync-protocol.md): world 0..3 (default 0) and the lab-only world_probe.
    const auto worldDefault = Lab("enabled=1\nrole=host\n");
    Check(worldDefault.world == 0 && worldDefault.world_probe == WorldProbe::none && worldDefault.invalid_values == 0, "world defaults");
    for (std::uint32_t level = 0; level <= 3; ++level) {
        const auto c = Lab("enabled=1\nrole=host\nworld=" + std::to_string(level) + "\n");
        Check(c.world == level && c.invalid_values == 0 && c.unknown_keys == 0, "world 0..3");
    }
    for (const char* badWorld : {"4", "x", "-1", "", "3x"}) {
        const auto c = Lab(std::string("enabled=1\nrole=host\nworld=1\nworld=") + badWorld + "\n");
        Check(c.world == 1 && c.invalid_values == 1, "invalid world keeps the earlier value");
    }
    const auto worldSeven = Lab("enabled=1\nrole=host\nworld=7\n");
    Check(worldSeven.world == 0 && worldSeven.invalid_values == 1, "invalid world keeps 0");
    const char* probes[] = {"none", "p1", "p3", "p3b", "p4", "p12", "p17", "race_kill", "remote_area_kill", "kill_near", "sleep_near",
                            "fulton_near", "alert_near", "weather_rain", "clock_jump", "clock_fast",
                            "shared_death", "host_continue", "shared_checkpoint"};
    for (unsigned i = 0; i <= kWorldProbeMax; ++i) {
        const auto c = Lab(std::string("enabled=1\nrole=guest\nworld=2\nworld_probe=") + probes[i] + "\n");
        Check(static_cast<unsigned>(c.world_probe) == i && c.invalid_values == 0 && std::strcmp(WorldProbeName(c.world_probe), probes[i]) == 0,
              "world_probe names round trip");
        WorldProbe parsed = WorldProbe::none;
        Check(ParseWorldProbe(probes[i], std::strlen(probes[i]), parsed) && static_cast<unsigned>(parsed) == i, "ParseWorldProbe");
    }
    WorldProbe unchanged = WorldProbe::p1;
    Check(!ParseWorldProbe("P1", 2, unchanged) && !ParseWorldProbe("p2", 2, unchanged) && !ParseWorldProbe("", 0, unchanged) &&
              !ParseWorldProbe("race_kill ", 10, unchanged) && unchanged == WorldProbe::p1,
          "ParseWorldProbe is exact");
    Check(std::strcmp(WorldProbeName(static_cast<WorldProbe>(kWorldProbeMax + 1)), "?") == 0, "out-of-range probe name");
    const auto badProbe = Lab("enabled=1\nrole=host\nworld_probe=p2\n");
    Check(badProbe.world_probe == WorldProbe::none && badProbe.invalid_values == 1, "unknown probe invalid");
    const auto playWorld = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\nworld=2\n");
    Check(playWorld.world == 2 && playWorld.play_refused == 0 && playWorld.invalid_values == 0, "play keeps world");
    const auto playProbe = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\nworld=2\nworld_probe=race_kill\n");
    Check(playProbe.world == 2 && playProbe.world_probe == WorldProbe::none && playProbe.play_refused == kPlayRefusedWorldProbe &&
              playProbe.invalid_values == 1,
          "play refuses world_probe");
    Check(kPlayRefusedWorldProbe == 0x8000, "world_probe refusal bit");

    std::printf("%u lab/coop configuration checks passed; pure parser, no game calls\n", checks);
}
