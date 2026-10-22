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

    // Native support follows the native gun profile in lab AND play (Stage B, docs/anim-round2-throws.md: lab hidden
    // runs aaa2a0e0, ea93a2e3; play rehearsals 117620b9, e9244c56, 733e023b, 33c11da9); off without a profile and for
    // loco/off in either mode, and a named key always wins (partner_native_support=0 opts out of the gun default).
    Check(!LabConfig{}.partner_native_support && !Lab("").partner_native_support, "native support default off");
    for (const char* mode : {"lab", "play"}) {
        // Play profiles require the real clip-partner fallback configuration.
        const std::string prefix = std::string("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nmode=") + mode +
                                  "\nnet_bind=any\npartner=1\npartner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\n";
        for (const char* profile : {"off", "loco", "gun"}) {
            const std::string key = std::string("partner_native_profile=") + profile + "\n";
            const bool gun = std::strcmp(profile, "gun") == 0;
            const auto resolved = Lab(prefix + key);
            Check(resolved.partner_native_support == gun && !resolved.partner_native_support_given,
                  "support defaults on for the gun profile in lab and play, off for off/loco");
            Check(resolved.play_refused == 0 && resolved.invalid_values == 0 && resolved.unknown_keys == 0,
                  "support matrix configs are clean in lab and play");
            for (const bool keyFirst : {false, true}) {
                for (const char* value : {"0", "1"}) {
                    const std::string support = std::string("partner_native_support=") + value + "\n";
                    const auto named = Lab(prefix + (keyFirst ? support + key : key + support));
                    Check(named.partner_native_support == (value[0] == '1') && named.invalid_values == 0 && named.unknown_keys == 0,
                          "explicit support 0/1 survives mode/profile resolution in either key order");
                }
            }
        }
    }
    for (const char* value : {"2", "-1", "", "maybe"}) {
        const auto invalid = Lab(std::string("enabled=1\nrole=host\npartner_native_support=") + value + "\n");
        Check(!invalid.partner_native_support && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
              "invalid native support value refuses without arming");
    }
    Check(!Lab("enabled=1\nrole=host\npartner_native_support=1\npartner_native_support=0\n").partner_native_support,
          "explicit support 0 disarms a previous 1");
    // WS4 step 0: the read-only placed / arm census is its own lab key, default off, no profile turns it on.
    Check(!LabConfig{}.partner_placed_census && !Lab("").partner_placed_census, "placed census default off");
    {
        const auto on = Lab("enabled=1\nrole=host\npartner_placed_census=1\n");
        Check(on.partner_placed_census && on.invalid_values == 0 && on.unknown_keys == 0, "placed census 1 parses");
        Check(!Lab("enabled=1\nrole=host\npartner_placed_census=1\npartner_placed_census=0\n").partner_placed_census,
              "placed census 0 after 1 turns it off");
        const auto gunLab = Lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nmode=lab\nnet_bind=any\npartner=1\n"
                                "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\n");
        Check(!gunLab.partner_placed_census && gunLab.partner_native_support, "the lab gun profile leaves the placed census off");
        for (const char* value : {"2", "-1", "", "maybe"}) {
            const auto invalid = Lab(std::string("enabled=1\nrole=host\npartner_placed_census=") + value + "\n");
            Check(!invalid.partner_placed_census && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
                  "an invalid placed census value refuses");
        }
    }
    // WS4 v2: the C4 place / detonate mirror (partner_native_place_mirror). Not named: on with the gun profile in lab
    // (after its hidden runs), off for off/loco and without a profile; a named 0/1 wins in lab; play forces it off.
    {
        Check(!LabConfig{}.partner_native_place_mirror && !Lab("").partner_native_place_mirror, "WS4 v2 key defaults off");
        const std::string gun = "enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\n"
                                "partner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\n";
        for (const char* mode : {"mode=lab\n", "mode=play\n"}) {
            const bool play = std::strcmp(mode, "mode=play\n") == 0;
            const auto profile = Lab(gun + mode);
            Check(profile.partner_native_place_mirror == !play && !profile.partner_native_place_mirror_given && profile.partner_native_placed,
                  "the gun profile turns the WS4 v2 mirror on in lab; play forces it off");
            const auto named = Lab(gun + mode + "partner_native_place_mirror=1\n");
            Check(named.partner_native_place_mirror == !play && named.partner_native_place_mirror_given && named.partner_native_placed &&
                      named.invalid_values == 0 && named.unknown_keys == 0 && named.play_refused == 0,
                  "a named WS4 v2 mirror runs in lab; play forces it off");
            const auto off = Lab(gun + mode + "partner_native_place_mirror=0\n");
            Check(!off.partner_native_place_mirror && off.partner_native_place_mirror_given && off.partner_native_placed,
                  "a named 0 opts the gun profile out of the WS4 v2 mirror");
            for (const char* other : {"off", "loco"}) {
                const std::string base = "enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\n"
                                         "partner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=";
                Check(!Lab(base + other + "\n" + mode).partner_native_place_mirror, "off/loco profiles leave the WS4 v2 mirror off");
            }
        }
        const auto alone = Lab("enabled=1\nrole=host\npartner_native_place_mirror=1\n");
        Check(alone.partner_native_place_mirror && alone.invalid_values == 0 && alone.unknown_keys == 0, "partner_native_place_mirror=1 parses");
        const auto later = Lab("enabled=1\nrole=host\npartner_native_place_mirror=1\npartner_native_place_mirror=0\n");
        Check(!later.partner_native_place_mirror, "a later 0 turns the mirror off");
        for (const char* value : {"2", "-1", "", "maybe"}) {
            const auto invalid = Lab(std::string("enabled=1\nrole=host\npartner_native_place_mirror=") + value + "\n");
            Check(!invalid.partner_native_place_mirror && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
                  "an invalid WS4 v2 mirror value refuses");
        }
    }
    // Traversal replay (partner_native_traverse, docs/native-puppet.md "Traversal"): default 0 in lab and play, also
    // with the native gun profile (no hidden-worker FRAME of body 1 on a ladder yet); a named 1 runs in lab only, play
    // forces it off; a named value records _given.
    {
        Check(!LabConfig{}.partner_native_traverse && !LabConfig{}.partner_native_traverse_given && !Lab("").partner_native_traverse,
              "traverse key defaults off");
        const std::string gun = "enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\n"
                                "partner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\n";
        for (const char* mode : {"mode=lab\n", "mode=play\n"}) {
            const bool play = std::strcmp(mode, "mode=play\n") == 0;
            const auto profile = Lab(gun + mode);
            Check(!profile.partner_native_traverse && !profile.partner_native_traverse_given && profile.invalid_values == 0,
                  "the gun profile leaves traverse off in lab and play");
            const auto named = Lab(gun + mode + "partner_native_traverse=1\n");
            Check(named.partner_native_traverse == !play && named.partner_native_traverse_given && named.invalid_values == 0 &&
                      named.unknown_keys == 0 && named.play_refused == 0,
                  "a named traverse=1 runs in lab; play forces it off");
            const auto off = Lab(gun + mode + "partner_native_traverse=0\n");
            Check(!off.partner_native_traverse && off.partner_native_traverse_given, "a named traverse=0 stays off");
        }
        const auto alone = Lab("enabled=1\nrole=host\npartner_native_traverse=1\n");
        Check(alone.partner_native_traverse && alone.partner_native_traverse_given && alone.invalid_values == 0 && alone.unknown_keys == 0,
              "partner_native_traverse=1 parses");
        const auto later = Lab("enabled=1\nrole=host\npartner_native_traverse=1\npartner_native_traverse=0\n");
        Check(!later.partner_native_traverse, "a later 0 turns traverse off");
        for (const char* value : {"2", "-1", "", "maybe"}) {
            const auto invalid = Lab(std::string("enabled=1\nrole=host\npartner_native_traverse=") + value + "\n");
            Check(!invalid.partner_native_traverse && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
                  "an invalid traverse value refuses");
        }
    }
    // F1 vehicles (partner_ride_board, docs/native-puppet.md "Vehicle boarding"): off|block|passenger, default off in lab
    // and play, also with the native gun profile (no hidden-worker FRAME yet); a named value runs in lab only, play
    // forces it off; a named value records _given.
    {
        using mgscoop::PartnerRideBoard;
        Check(LabConfig{}.partner_ride_board == PartnerRideBoard::off && !LabConfig{}.partner_ride_board_given &&
                  Lab("").partner_ride_board == PartnerRideBoard::off,
              "board key defaults off");
        const std::string gun = "enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\n"
                                "partner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\n";
        for (const char* mode : {"mode=lab\n", "mode=play\n"}) {
            const bool play = std::strcmp(mode, "mode=play\n") == 0;
            const auto profile = Lab(gun + mode);
            Check(profile.partner_ride_board == PartnerRideBoard::off && !profile.partner_ride_board_given && profile.invalid_values == 0,
                  "the gun profile leaves the board key off in lab and play");
            for (const char* value : {"block", "passenger"}) {
                const auto named = Lab(gun + mode + "partner_ride_board=" + value + "\n");
                const PartnerRideBoard want = std::strcmp(value, "block") == 0 ? PartnerRideBoard::block : PartnerRideBoard::passenger;
                Check(named.partner_ride_board == (play ? PartnerRideBoard::off : want) && named.partner_ride_board_given &&
                          named.invalid_values == 0 && named.unknown_keys == 0 && named.play_refused == 0,
                      "a named board mode runs in lab; play forces it off");
            }
            const auto off = Lab(gun + mode + "partner_ride_board=off\n");
            Check(off.partner_ride_board == PartnerRideBoard::off && off.partner_ride_board_given, "a named off stays off");
        }
        const auto alone = Lab("enabled=1\nrole=host\npartner_ride_board=passenger\n");
        Check(alone.partner_ride_board == PartnerRideBoard::passenger && alone.partner_ride_board_given && alone.invalid_values == 0 &&
                  alone.unknown_keys == 0,
              "partner_ride_board=passenger parses");
        const auto later = Lab("enabled=1\nrole=host\npartner_ride_board=passenger\npartner_ride_board=block\n");
        Check(later.partner_ride_board == PartnerRideBoard::block, "a later value wins");
        for (const char* value : {"1", "on", "", "Block", "passengers", "drive"}) {
            const auto invalid = Lab(std::string("enabled=1\nrole=host\npartner_ride_board=") + value + "\n");
            Check(invalid.partner_ride_board == PartnerRideBoard::off && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
                  "an invalid board value refuses");
        }
        Check(std::strcmp(mgscoop::PartnerRideBoardName(PartnerRideBoard::off), "off") == 0 &&
                  std::strcmp(mgscoop::PartnerRideBoardName(PartnerRideBoard::block), "block") == 0 &&
                  std::strcmp(mgscoop::PartnerRideBoardName(PartnerRideBoard::passenger), "passenger") == 0,
              "board names");
    }
    // F2 cardboard box on body 1 (partner_native_cbox, docs/native-puppet.md "Cardboard box"): default 0 in lab and play,
    // also with the native gun profile (no hidden-worker FRAME of body 1 boxed yet); a named 1 runs in lab only, play
    // forces it off; a named value records _given.
    {
        Check(!LabConfig{}.partner_native_cbox && !LabConfig{}.partner_native_cbox_given && !Lab("").partner_native_cbox,
              "cbox key defaults off");
        const std::string gun = "enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\n"
                                "partner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\n";
        for (const char* mode : {"mode=lab\n", "mode=play\n"}) {
            const bool play = std::strcmp(mode, "mode=play\n") == 0;
            const auto profile = Lab(gun + mode);
            Check(!profile.partner_native_cbox && !profile.partner_native_cbox_given && profile.invalid_values == 0,
                  "the gun profile leaves the box off in lab and play");
            const auto named = Lab(gun + mode + "partner_native_cbox=1\n");
            Check(named.partner_native_cbox == !play && named.partner_native_cbox_given && named.invalid_values == 0 &&
                      named.unknown_keys == 0 && named.play_refused == 0,
                  "a named cbox=1 runs in lab; play forces it off");
            const auto off = Lab(gun + mode + "partner_native_cbox=0\n");
            Check(!off.partner_native_cbox && off.partner_native_cbox_given, "a named cbox=0 stays off");
        }
        const auto alone = Lab("enabled=1\nrole=host\npartner_native_cbox=1\n");
        Check(alone.partner_native_cbox && alone.partner_native_cbox_given && alone.invalid_values == 0 && alone.unknown_keys == 0,
              "partner_native_cbox=1 parses");
        const auto later = Lab("enabled=1\nrole=host\npartner_native_cbox=1\npartner_native_cbox=0\n");
        Check(!later.partner_native_cbox, "a later 0 turns the box off");
        for (const char* value : {"2", "-1", "", "maybe"}) {
            const auto invalid = Lab(std::string("enabled=1\nrole=host\npartner_native_cbox=") + value + "\n");
            Check(!invalid.partner_native_cbox && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
                  "an invalid cbox value refuses");
        }
    }
    // Gestures (partner_native_gesture, docs/native-puppet.md "Gestures"): default 0 in lab and play, also with the
    // native gun profile (no hidden-worker FRAME of a graph-2 clip on body 1 yet); a named 1 runs in lab only, play
    // forces it off; a named value records _given; independent of the traversal key.
    {
        Check(!LabConfig{}.partner_native_gesture && !LabConfig{}.partner_native_gesture_given && !Lab("").partner_native_gesture,
              "gesture key defaults off");
        const std::string gun = "enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\n"
                                "partner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\n";
        for (const char* mode : {"mode=lab\n", "mode=play\n"}) {
            const bool play = std::strcmp(mode, "mode=play\n") == 0;
            const auto profile = Lab(gun + mode);
            Check(!profile.partner_native_gesture && !profile.partner_native_gesture_given && profile.invalid_values == 0,
                  "the gun profile leaves gestures off in lab and play");
            const auto named = Lab(gun + mode + "partner_native_gesture=1\n");
            Check(named.partner_native_gesture == !play && named.partner_native_gesture_given && named.invalid_values == 0 &&
                      named.unknown_keys == 0 && named.play_refused == 0 && !named.partner_native_traverse,
                  "a named gesture=1 runs in lab; play forces it off");
            const auto off = Lab(gun + mode + "partner_native_gesture=0\n");
            Check(!off.partner_native_gesture && off.partner_native_gesture_given, "a named gesture=0 stays off");
        }
        const auto alone = Lab("enabled=1\nrole=host\npartner_native_gesture=1\n");
        Check(alone.partner_native_gesture && alone.partner_native_gesture_given && alone.invalid_values == 0 && alone.unknown_keys == 0,
              "partner_native_gesture=1 parses");
        const auto later = Lab("enabled=1\nrole=host\npartner_native_gesture=1\npartner_native_gesture=0\n");
        Check(!later.partner_native_gesture, "a later 0 turns gestures off");
        for (const char* value : {"2", "-1", "", "maybe"}) {
            const auto invalid = Lab(std::string("enabled=1\nrole=host\npartner_native_gesture=") + value + "\n");
            Check(!invalid.partner_native_gesture && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
                  "an invalid gesture value refuses");
        }
    }
    // WS4 v1: native placed C4 and arm holds, two lab keys. Not named, they follow the native gun profile in lab AND play
    // (after the lab hidden runs 7f8d643d / d05a1267), off without a profile and for off/loco; a named 0/1 always wins
    // in either key order; play keeps them (ModeRefusal gates them there, link v2 connected).
    Check(!LabConfig{}.partner_native_placed && !LabConfig{}.partner_native_arm && !Lab("").partner_native_placed &&
          !Lab("").partner_native_arm, "WS4 v1 keys default off without a profile");
    for (const char* mode : {"lab", "play"}) {
        const std::string prefix = std::string("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nmode=") + mode +
                                  "\nnet_bind=any\npartner=1\npartner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\n";
        for (const char* profile : {"off", "loco", "gun"}) {
            const std::string key = std::string("partner_native_profile=") + profile + "\n";
            const bool gun = std::strcmp(profile, "gun") == 0;
            const auto resolved = Lab(prefix + key);
            Check(resolved.partner_native_placed == gun && resolved.partner_native_arm == gun &&
                      !resolved.partner_native_placed_given && !resolved.partner_native_arm_given,
                  "C4 and arm holds default on for the gun profile in lab and play, off for off/loco");
            Check(resolved.play_refused == 0 && resolved.invalid_values == 0 && resolved.unknown_keys == 0,
                  "WS4 v1 matrix configs are clean in lab and play");
            for (const bool keyFirst : {false, true}) {
                for (const char* value : {"0", "1"}) {
                    const std::string named = std::string("partner_native_placed=") + value + "\npartner_native_arm=" + value + "\n";
                    const auto n = Lab(prefix + (keyFirst ? named + key : key + named));
                    Check(n.partner_native_placed == (value[0] == '1') && n.partner_native_arm == (value[0] == '1') &&
                              n.partner_native_placed_given && n.partner_native_arm_given && n.invalid_values == 0 &&
                              n.unknown_keys == 0,
                          "explicit C4 / arm 0/1 survive mode/profile resolution in either key order");
                }
                const std::string one = "partner_native_arm=0\n";
                const auto armOff = Lab(prefix + (keyFirst ? one + key : key + one));
                Check(armOff.partner_native_placed == gun && !armOff.partner_native_arm && !armOff.partner_native_placed_given &&
                          armOff.partner_native_arm_given,
                      "a named arm 0 opts out of the arm alone; C4 keeps the profile's default");
            }
        }
    }
    {
        const std::string gun = "enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\n"
                                "partner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_native_profile=gun\n";
        for (const char* mode : {"mode=lab\n", "mode=play\n"}) {
            const auto profile = Lab(gun + mode);
            Check(profile.partner_native_placed && profile.partner_native_arm && profile.partner_native_support,
                  "the gun profile turns C4 and arm holds on (lab and play)");
            // integ7 (WS2 Stage B + WS3 + WS4 v1 together): the gun profile runs throws, C4 and arm holds, the seat by
            // id and RIDE_TILT in lab and play; both read-only censuses stay off.
            Check(profile.partner_native_support && profile.partner_native_placed && profile.partner_native_arm &&
                      !profile.partner_ride_census && profile.partner_ride_seat_id && profile.partner_ride_tilt &&
                      !profile.partner_placed_census && profile.play_refused == 0 && profile.invalid_values == 0,
                  "the gun profile: throws, C4, arms, seat by id and tilt on; censuses off (lab and play)");
            const auto off = Lab(gun + mode + "partner_native_placed=0\npartner_native_arm=0\n");
            Check(!off.partner_native_placed && !off.partner_native_arm && off.partner_native_support && off.play_refused == 0,
                  "named 0s opt the gun profile out of C4 and arm holds, throws stay on");
        }
        const auto placed = Lab("enabled=1\nrole=host\npartner_native_placed=1\n");
        Check(placed.partner_native_placed && !placed.partner_native_arm && placed.invalid_values == 0 && placed.unknown_keys == 0,
              "partner_native_placed=1 parses alone");
        const auto arm = Lab("enabled=1\nrole=host\npartner_native_arm=1\n");
        Check(arm.partner_native_arm && !arm.partner_native_placed && arm.invalid_values == 0 && arm.unknown_keys == 0,
              "partner_native_arm=1 parses alone");
        const auto both = Lab("enabled=1\nrole=host\npartner_native_placed=1\npartner_native_arm=1\npartner_native_placed=0\n");
        Check(!both.partner_native_placed && both.partner_native_arm, "a later 0 turns a key off, the other stays");
        for (const char* key : {"partner_native_placed=", "partner_native_arm="})
            for (const char* value : {"2", "-1", "", "maybe"}) {
                const auto invalid = Lab(std::string("enabled=1\nrole=host\n") + key + value + "\n");
                Check(!invalid.partner_native_placed && !invalid.partner_native_arm && invalid.invalid_values == 1 && invalid.unknown_keys == 0,
                      "an invalid WS4 v1 key value refuses");
            }
        const auto playNamed = Lab(gun + "mode=play\npartner_native_placed=1\npartner_native_arm=1\n");
        Check(playNamed.partner_native_placed && playNamed.partner_native_arm && playNamed.invalid_values == 0,
              "play keeps the named WS4 v1 keys (admission is ModeRefusal's, link v2 connected)");
    }

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
              def.partner_input_mask == 0x2240006 && def.partner_input_stick_deg == 180 && def.partner_input_stick_pct == 100 &&
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
        // Receiver facing rate limit: default on (900 deg/s), 0 disables, 0..1440.
        Check(def.partner_facing_dps == 900, "partner_facing_dps default 900");
        const auto facing_off = Lab("enabled=1\nrole=host\npartner_facing_dps=0\n");
        const auto facing_max = Lab("enabled=1\nrole=host\npartner_facing_dps=1440\n");
        const auto facing_bad = Lab("enabled=1\nrole=host\npartner_facing_dps=1441\n");
        Check(facing_off.partner_facing_dps == 0 && facing_off.invalid_values == 0 && facing_off.unknown_keys == 0 &&
              facing_max.partner_facing_dps == 1440 && facing_bad.partner_facing_dps == 900 && facing_bad.invalid_values == 1,
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
        Check(badr3.invalid_values == 7 && badr3.partner_input == PartnerInput::off && badr3.partner_input_mask == 0x2240006 &&
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
        {"debug_soldier_warp=1\n", kPlayRefusedDebugSoldierWarp},
    };
    for (const auto& r : refusals) {
        const auto c = Lab(playBase + r.line);
        if (c.play_refused != r.bit || c.invalid_values != 1) std::fprintf(stderr, "%s -> 0x%X, %u\n", r.line, c.play_refused, c.invalid_values);
        Check(c.play_refused == r.bit && c.invalid_values == 1 && c.present, "play refusal bit");
        Check(!c.loopback && c.self_steamid == 0 && c.peer_steamid == 0 && !c.self_name[0] && !c.mutex_suffix[0] &&
              c.xinput == kXInputPassthrough && !c.join_trigger && c.auto_join_delay_s == 0 && !c.net_bind_loopback &&
              c.net_link == NetLink::v2 && c.net_latency_ms == 0 && c.net_loss_permille == 0 && c.net_jitter_ms == 0 &&
              c.net_dup_permille == 0 && c.net_reorder_permille == 0 && c.net_peer_address == 0x7F000001u && c.net &&
              !c.debug_soldier_warp,
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
    // Steam online (docs/steam-online.md 9.1).
    {
        const std::string steamHost = "enabled=1\nmode=play\nrole=host\n" + saves +
                                      "net=1\nnet_transport=steam\nnet_steam_self=76561198058195100\npartner_interp=auto\npartner=1\n";
        const auto sHost = Lab(steamHost);
        Check(sHost.present && sHost.net && sHost.net_transport == NetTransport::steam && sHost.net_steam_self == 76561198058195100ull &&
              sHost.play_refused == 0 && sHost.invalid_values == 0 && sHost.unknown_keys == 0 && sHost.net_steam_accept &&
              sHost.net_listen_port == 0 && sHost.net_peer_port == 0 && !sHost.net_bind_loopback,
              "steam host: no UDP defaults applied");
        Check(!Lab(steamHost + "net_steam_accept=off\n").net_steam_accept, "net_steam_accept=off");
        Check(Lab(steamHost + "net_steam_accept=maybe\n").invalid_values == 1, "net_steam_accept invalid");
        const auto hostPeer = Lab(steamHost + "net_peer_steam=76561198057664236\n");
        Check(hostPeer.play_refused == kPlayRefusedHostPeerSteam && hostPeer.net_peer_steam == 0 && hostPeer.invalid_values == 1 && hostPeer.net,
              "net_peer_steam on a host: ignored, 0x80000");
        const auto udpKeys = Lab(steamHost + "net_listen_port=47830\nnet_bind=any\n");
        Check(udpKeys.play_refused == kPlayRefusedUdpKeysWithSteam && udpKeys.net && udpKeys.net_listen_port == 0 && udpKeys.invalid_values == 1,
              "UDP keys with steam: ignored, 0x200000");
        const auto hostAddr = Lab(steamHost + "net_peer_addr=203.0.113.7\n");
        Check(hostAddr.play_refused == kPlayRefusedUdpKeysWithSteam && hostAddr.net_peer_address == 0x7F000001u, "net_peer_addr with steam");
        const std::string steamGuest = "enabled=1\nmode=play\nrole=guest\n" + saves + "net=1\nnet_transport=steam\nnet_steam_self=76561198057664236\n";
        const auto sGuest = Lab(steamGuest + "net_peer_steam=76561198058195100\n");
        Check(sGuest.net && sGuest.net_peer_steam == 76561198058195100ull && sGuest.play_refused == 0 && sGuest.invalid_values == 0 && !sGuest.net_steam_wait,
              "steam guest with a host (no net_peer_addr needed)");
        const auto waiting = Lab(steamGuest + "net_steam_wait=1\n");
        Check(waiting.net && waiting.net_steam_wait && waiting.net_peer_steam == 0 && waiting.play_refused == 0, "waiting guest (S2)");
        const auto both = Lab(steamGuest + "net_steam_wait=1\nnet_peer_steam=76561198058195100\n");
        Check(both.net && !both.net_steam_wait && both.net_peer_steam, "a configured host wins over waiting");
        const auto noPeer = Lab(steamGuest);
        Check(!noPeer.net && noPeer.play_refused == kPlayRefusedNoPeerSteam && noPeer.invalid_values == 1, "steam guest without a host: 0x100000");
        const auto self = Lab(steamGuest + "net_peer_steam=76561198057664236\n");
        Check(self.net_peer_steam == 0 && !self.net && (self.play_refused & kPlayRefusedNoPeerSteam) && self.invalid_values == 2,
              "a guest's host is never itself");
        Check(Lab(steamGuest + "net_peer_steam=103582791429521408\n").invalid_values >= 1, "a group id is not a peer");
        Check(Lab(steamGuest + "net_peer_steam=76561198058195100\nnet_transport=bogus\n").invalid_values == 1, "net_transport invalid");
        const auto selftest = Lab(steamHost + "steam_selftest=1\n");
        Check(selftest.steam_selftest == 0 && selftest.play_refused == kPlayRefusedSteamSelftest, "steam_selftest in play: 0x40000");
        // The lab refuses Steam networking (its only Steam exercise is the self-test).
        const auto lab = Lab("enabled=1\nrole=host\nnet=1\nnet_transport=steam\nsteam_selftest=2\nnet_peer_steam=76561198058195100\n");
        Check(lab.net_transport == NetTransport::udp && lab.net_transport_refused && lab.invalid_values == 1 && lab.steam_selftest == 2 &&
              lab.net && lab.net_listen_port == kNetHostPort && lab.net_peer_steam == 0 && lab.play_refused == 0,
              "lab: steam forced back to udp (+1 invalid), the self-test allowed");
        Check(Lab("enabled=1\nrole=host\nsteam_selftest=3\n").invalid_values == 1, "steam_selftest 0..2");
        // UDP play unchanged by the Steam keys.
        const auto udpPlay = Lab("enabled=1\nmode=play\nrole=guest\n" + saves + "net=1\nnet_peer_addr=203.0.113.7\nnet_steam_wait=1\n");
        Check(udpPlay.net_transport == NetTransport::udp && udpPlay.net && !udpPlay.net_steam_wait && udpPlay.net_peer_port == 47830,
              "UDP play ignores the Steam keys");
        Check(std::strcmp(NetTransportName(NetTransport::steam), "steam") == 0 && std::strcmp(NetTransportName(NetTransport::udp), "udp") == 0,
              "transport names");
        // Section 20: the code-join sources (default both), the self-test's stage F switch.
        Check(sHost.net_steam_dispatch == SteamDispatch::both && sHost.steam_selftest_fn, "net_steam_dispatch default both; steam_selftest_fn 1");
        for (const char* v : {"both", "fnptr", "callback", "poll"}) {
            const auto d = Lab(steamHost + "net_steam_dispatch=" + v + "\n");
            Check(std::strcmp(SteamDispatchName(d.net_steam_dispatch), v) == 0 && d.invalid_values == 0 && d.play_refused == 0, "net_steam_dispatch values");
        }
        const auto badDispatch = Lab(steamHost + "net_steam_dispatch=fn\n");
        Check(badDispatch.net_steam_dispatch == SteamDispatch::both && badDispatch.invalid_values == 1, "net_steam_dispatch invalid keeps both");
        const auto noFn = Lab("enabled=1\nrole=host\nsteam_selftest=1\nsteam_selftest_fn=0\n");
        Check(noFn.steam_selftest == 1 && !noFn.steam_selftest_fn && noFn.invalid_values == 0, "steam_selftest_fn=0");
        Check(Lab("enabled=1\nrole=host\nsteam_selftest_fn=2\n").invalid_values == 1, "steam_selftest_fn 0|1");
        // The in-game status line: lab.ini coop_hud, coop.ini hud (allowed in play).
        const auto hudLab = Lab(steamHost + "coop_hud=full\n");
        Check(hudLab.coop_hud == CoopHud::full && hudLab.invalid_values == 0 && hudLab.play_refused == 0, "coop_hud=full in play");
        Check(Lab("enabled=1\nrole=host\n").coop_hud == CoopHud::unset && Lab("enabled=1\nrole=host\ncoop_hud=off\n").coop_hud == CoopHud::off,
              "coop_hud default unset; off");
        Check(Lab("enabled=1\nrole=host\ncoop_hud=big\n").invalid_values == 1, "coop_hud invalid");
        Check(Coop("hud=compact\n").hud == CoopHud::compact && Coop("").hud == CoopHud::unset && Coop("hud=x\n").invalid_values == 1, "coop.ini hud");
        Check(std::strcmp(CoopHudName(CoopHud::full), "full") == 0 && CoopHudName(CoopHud::unset)[0] == 0, "hud names");
    }
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
    // Automatic save handoff and the guest travel lock (join-fix phase 2 P2-11):
    // save_handoff absent = off, travel_lock absent = 1 in play and 0 in lab;
    // neither is a play refusal.
    {
        const auto absent = Lab("enabled=1\nrole=guest\nworld=3\n");
        Check(absent.save_handoff == SaveHandoff::off && !absent.save_handoff_given && absent.travel_lock == kTravelLockAbsent &&
                  absent.invalid_values == 0 && absent.unknown_keys == 0,
              "save_handoff absent is off, travel_lock absent");
        Check(!EffectiveTravelLock(absent), "travel lock defaults off in lab");
        const auto playAbsent = Lab("enabled=1\nmode=play\nrole=guest\n" + saves + "net=1\nnet_peer_addr=192.168.1.20\nworld=3\n");
        Check(playAbsent.mode == LabMode::play && playAbsent.travel_lock == kTravelLockAbsent && EffectiveTravelLock(playAbsent) &&
                  playAbsent.save_handoff == SaveHandoff::off,
              "travel lock defaults on in play");
        const char* modes[] = {"off", "stage", "scene", "always"};
        for (unsigned i = 0; i < 4; ++i) {
            const auto c = Lab(std::string("enabled=1\nrole=guest\nsave_handoff=") + modes[i] + "\n");
            Check(static_cast<unsigned>(c.save_handoff) == i && c.save_handoff_given && c.invalid_values == 0 &&
                      std::strcmp(SaveHandoffName(c.save_handoff), modes[i]) == 0,
                  "save_handoff values round trip");
        }
        const auto badHandoff = Lab("enabled=1\nrole=guest\nsave_handoff=auto\ntravel_lock=2\n");
        Check(badHandoff.save_handoff == SaveHandoff::off && !badHandoff.save_handoff_given && badHandoff.travel_lock == kTravelLockAbsent &&
                  badHandoff.invalid_values == 2,
              "save_handoff=auto and travel_lock=2 are invalid values");
        const auto lockOn = Lab("enabled=1\nrole=guest\ntravel_lock=1\n");
        Check(lockOn.travel_lock == 1 && EffectiveTravelLock(lockOn) && lockOn.invalid_values == 0, "travel_lock=1 in lab");
        const auto playOff = Lab("enabled=1\nmode=play\nrole=guest\n" + saves +
                                 "net=1\nnet_peer_addr=192.168.1.20\nworld=3\nsave_handoff=scene\ntravel_lock=0\n");
        Check(playOff.travel_lock == 0 && !EffectiveTravelLock(playOff) && playOff.save_handoff == SaveHandoff::scene &&
                  playOff.play_refused == 0 && playOff.invalid_values == 0 && playOff.unknown_keys == 0,
              "play keeps save_handoff and travel_lock=0 without a refusal bit");
        const auto playAlways = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\nsave_handoff=always\ntravel_lock=1\n");
        Check(playAlways.save_handoff == SaveHandoff::always && playAlways.travel_lock == 1 && playAlways.play_refused == 0 &&
                  playAlways.invalid_values == 0,
              "play host with save_handoff=always");
        SaveHandoff parsed = SaveHandoff::scene;
        Check(ParseSaveHandoff("stage", 5, parsed) && parsed == SaveHandoff::stage && !ParseSaveHandoff("Stage", 5, parsed) &&
                  !ParseSaveHandoff("", 0, parsed) && !ParseSaveHandoff(nullptr, 3, parsed) && parsed == SaveHandoff::stage,
              "save_handoff spellings are exact");
        Check(static_cast<unsigned>(SaveHandoff::always) == 3, "SaveHandoff matches save_handoff_wire::Mode");
    }
    // Debug "Spawn enemy here" native placement: lab-only, default off.
    Check(!Lab("enabled=1\nrole=host\n").debug_soldier_warp, "debug_soldier_warp defaults off");
    const auto warpOn = Lab("enabled=1\nrole=host\ndebug_soldier_warp=1\n");
    Check(warpOn.debug_soldier_warp && warpOn.invalid_values == 0 && warpOn.unknown_keys == 0, "debug_soldier_warp=1 parsed");
    const auto warpBad = Lab("enabled=1\nrole=host\ndebug_soldier_warp=2\n");
    Check(!warpBad.debug_soldier_warp && warpBad.invalid_values == 1, "debug_soldier_warp is boolean");
    Check(kPlayRefusedDebugSoldierWarp == 0x10000, "debug_soldier_warp refusal bit");

    // Native puppet keys (docs/native-puppet.md): defaults keep today's behaviour.
    {
        const auto def = Lab("enabled=1\nrole=host\n");
        Check(def.partner_drive == PartnerDrive::legacy && def.partner_input_defer == PartnerDefer::off &&
              def.partner_input_local == PartnerLocal::main && def.partner_native_aim == PartnerNativeAim::off &&
              !def.partner_native_gun && def.partner_native_fire == mgscoop::PartnerNativeFire::off && def.partner_native_steer_deg == 15 &&
              def.partner_native_trim_pct == 0 && def.partner_native_rates == PartnerNativeRates::log,
              "native defaults (legacy, defer off, local main, aim off, fire off, steer 15, trim 0, rates log)");
        const auto nat = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_input_defer=census\npartner_input_local=all\n"
                             "partner_native_aim=lookcam\npartner_native_gun=1\npartner_native_fire=1\npartner_native_steer_deg=30\n"
                             "partner_native_trim_pct=10\npartner_native_rates=mirror\n");
        Check(nat.partner_drive == PartnerDrive::native && nat.partner_input_defer == PartnerDefer::census &&
              nat.partner_input_local == PartnerLocal::all && nat.partner_native_aim == PartnerNativeAim::lookcam &&
              nat.partner_native_gun && nat.partner_native_fire == mgscoop::PartnerNativeFire::visual && nat.partner_native_steer_deg == 30 &&
              nat.partner_native_trim_pct == 10 && nat.partner_native_rates == PartnerNativeRates::mirror && nat.invalid_values == 0 &&
              nat.unknown_keys == 0, "native keys parse");
        const auto ratesOff = Lab("enabled=1\nrole=host\npartner_native_rates=off\n");
        const auto ratesBad = Lab("enabled=1\nrole=host\npartner_native_rates=copy\n");
        Check(ratesOff.partner_native_rates == PartnerNativeRates::off && ratesOff.invalid_values == 0 &&
              ratesBad.partner_native_rates == PartnerNativeRates::log && ratesBad.invalid_values == 1 &&
              std::strcmp(PartnerNativeRatesName(PartnerNativeRates::mirror), "mirror") == 0 &&
              std::strcmp(PartnerNativeRatesName(PartnerNativeRates::log), "log") == 0 &&
              std::strcmp(PartnerNativeRatesName(PartnerNativeRates::off), "off") == 0, "partner_native_rates off|log|mirror");
        const auto on = Lab("enabled=1\nrole=guest\npartner_drive=native\npartner_input_defer=on\npartner_native_aim=look\n"
                            "partner_native_steer_deg=0\n");
        Check(on.partner_input_defer == PartnerDefer::on && on.partner_native_aim == PartnerNativeAim::look &&
              on.partner_native_steer_deg == 0 && on.invalid_values == 0, "defer=on, aim=look, steering off");
        const auto badNative = Lab("enabled=1\nrole=host\npartner_drive=puppet\npartner_input_defer=yes\npartner_input_local=none\n"
                             "partner_native_aim=cam\npartner_native_gun=2\npartner_native_steer_deg=31\npartner_native_trim_pct=11\n");
        Check(badNative.invalid_values == 7 && badNative.partner_drive == PartnerDrive::legacy && badNative.partner_input_defer == PartnerDefer::off &&
              badNative.partner_input_local == PartnerLocal::main && badNative.partner_native_aim == PartnerNativeAim::off &&
              !badNative.partner_native_gun && badNative.partner_native_steer_deg == 15 && badNative.partner_native_trim_pct == 0,
              "native range/enum refusals keep the defaults");
        // Native polish keys: the facing governor (idle) and the noise filter (filter) by default; looksrc.
        Check(def.partner_native_face == mgscoop::PartnerNativeFace::idle && def.partner_native_noise == mgscoop::PartnerNativeNoise::filter,
              "polish defaults: face idle, noise filter");
        const auto polish = Lab("enabled=1\nrole=host\npartner_native_aim=looksrc\npartner_native_face=aim\npartner_native_noise=keep\n");
        Check(polish.partner_native_aim == PartnerNativeAim::looksrc && polish.partner_native_face == mgscoop::PartnerNativeFace::aim &&
              polish.partner_native_noise == mgscoop::PartnerNativeNoise::keep && polish.invalid_values == 0 && polish.unknown_keys == 0,
              "looksrc, face=aim, noise=keep parse");
        const auto polishOff = Lab("enabled=1\nrole=host\npartner_native_face=off\npartner_native_noise=filter\n");
        const auto polishBad = Lab("enabled=1\nrole=host\npartner_native_face=always\npartner_native_noise=loud\npartner_native_aim=src\n");
        // The aim-target mirror (docs/native-puppet.md "Facing"): off by default; off|look|point.
        Check(def.partner_native_aimpt == mgscoop::PartnerNativeAimPt::off, "aimpt default off");
        const auto aimpt = Lab("enabled=1\nrole=host\npartner_native_aimpt=look\n");
        const auto aimptPoint = Lab("enabled=1\nrole=host\npartner_native_aimpt=point\n");
        const auto aimptBad = Lab("enabled=1\nrole=host\npartner_native_aimpt=on\n");
        Check(aimpt.partner_native_aimpt == mgscoop::PartnerNativeAimPt::look && aimpt.invalid_values == 0 && aimpt.unknown_keys == 0 &&
                  aimptPoint.partner_native_aimpt == mgscoop::PartnerNativeAimPt::point && aimptBad.invalid_values == 1 &&
                  aimptBad.partner_native_aimpt == mgscoop::PartnerNativeAimPt::off &&
                  std::strcmp(mgscoop::PartnerNativeAimPtName(mgscoop::PartnerNativeAimPt::look), "look") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeAimPtName(mgscoop::PartnerNativeAimPt::point), "point") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeAimPtName(mgscoop::PartnerNativeAimPt::off), "off") == 0,
              "partner_native_aimpt parses off|look|point");
        // Defaults agree for emitted raw gun settings and native profiles; named
        // values retain their meaning irrespective of the profile key's order.
        const auto rawPoint = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=1\npartner_input_defer=on\n");
        const auto censusPoint = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=1\npartner_input_defer=census\n");
        const auto undrivenPoint = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=1\n");
        Check(rawPoint.partner_native_aimpt == PartnerNativeAimPt::point && !rawPoint.partner_native_aimpt_given &&
              censusPoint.partner_native_aimpt == PartnerNativeAimPt::off && undrivenPoint.partner_native_aimpt == PartnerNativeAimPt::off,
              "raw driven gun defaults point; census and undriven gun stay off");
        for (const char* mode : {"lab", "play"}) {
            const std::string prefix = std::string("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=") + mode + "\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\n";
            Check(Lab(prefix + "partner_native_profile=gun\n").partner_native_aimpt == PartnerNativeAimPt::point &&
                  Lab(prefix + "partner_native_profile=loco\n").partner_native_aimpt == PartnerNativeAimPt::off &&
                  Lab(prefix).partner_native_aimpt == PartnerNativeAimPt::off, "lab/play default point is confined to the gun stage");
            for (const char* named : {"off", "look", "point"}) {
                const std::string key = std::string("partner_native_aimpt=") + named + "\n";
                for (const bool keyFirst : {false, true}) {
                    const auto explicitPoint = Lab(prefix + (keyFirst ? key + "partner_native_profile=gun\n" : "partner_native_profile=gun\n" + key));
                    Check(std::strcmp(PartnerNativeAimPtName(explicitPoint.partner_native_aimpt), named) == 0 &&
                          explicitPoint.partner_native_aimpt_given, "explicit aimpt off/look/point wins before/after gun profile in lab/play");
                }
            }
        }
        const auto badPoint = Lab("enabled=1\nrole=host\npartner_native_profile=gun\npartner_native_aimpt=on\n");
        const auto namedBadPoint = Lab("enabled=1\nrole=host\npartner_native_profile=gun\npartner_native_aimpt=off\npartner_native_aimpt=on\n");
        Check(badPoint.invalid_values == 1 && !badPoint.partner_native_aimpt_given && badPoint.partner_native_aimpt == PartnerNativeAimPt::point &&
              namedBadPoint.invalid_values == 1 && namedBadPoint.partner_native_aimpt_given && namedBadPoint.partner_native_aimpt == PartnerNativeAimPt::off,
              "invalid aimpt inherits gun default but cannot erase a valid explicit off");
        Check(polishOff.partner_native_face == mgscoop::PartnerNativeFace::off && polishOff.invalid_values == 0 &&
              polishBad.invalid_values == 3 && polishBad.partner_native_face == mgscoop::PartnerNativeFace::idle &&
              polishBad.partner_native_noise == mgscoop::PartnerNativeNoise::filter && polishBad.partner_native_aim == PartnerNativeAim::off,
              "polish enum refusals keep the defaults");
        Check(std::strcmp(PartnerNativeAimName(PartnerNativeAim::looksrc), "looksrc") == 0 &&
              std::strcmp(mgscoop::PartnerNativeFaceName(mgscoop::PartnerNativeFace::aim), "aim") == 0 &&
              std::strcmp(mgscoop::PartnerNativeFaceName(mgscoop::PartnerNativeFace::idle), "idle") == 0 &&
              std::strcmp(mgscoop::PartnerNativeFaceName(mgscoop::PartnerNativeFace::off), "off") == 0 &&
              std::strcmp(mgscoop::PartnerNativeNoiseName(mgscoop::PartnerNativeNoise::keep), "keep") == 0 &&
              std::strcmp(mgscoop::PartnerNativeNoiseName(mgscoop::PartnerNativeNoise::filter), "filter") == 0, "polish names");
        // The partner's own reload and weapon switch (docs/native-puppet.md "Weapons"): mirror / off by default
        // (today's behaviour); the native gun stage and Play-OfflineCoop set event / write.
        Check(def.partner_native_reload == mgscoop::PartnerNativeReload::mirror && def.partner_native_slot == mgscoop::PartnerNativeSlot::off,
              "weapons defaults: reload mirror, slot off");
        const auto weapons = Lab("enabled=1\nrole=host\npartner_native_reload=event\npartner_native_slot=write\n");
        const auto weaponsRaw = Lab("enabled=1\nrole=host\npartner_native_reload=raw\npartner_native_slot=call\n");
        const auto weaponsBad = Lab("enabled=1\nrole=host\npartner_native_reload=tap\npartner_native_slot=on\n");
        Check(weapons.partner_native_reload == mgscoop::PartnerNativeReload::event &&
                  weapons.partner_native_slot == mgscoop::PartnerNativeSlot::write && weapons.invalid_values == 0 && weapons.unknown_keys == 0 &&
                  weaponsRaw.partner_native_reload == mgscoop::PartnerNativeReload::raw &&
                  weaponsRaw.partner_native_slot == mgscoop::PartnerNativeSlot::call && weaponsRaw.invalid_values == 0,
              "reload event|raw, slot write|call parse");
        Check(weaponsBad.invalid_values == 2 && weaponsBad.partner_native_reload == mgscoop::PartnerNativeReload::mirror &&
                  weaponsBad.partner_native_slot == mgscoop::PartnerNativeSlot::off,
              "weapons enum refusals keep the defaults");
        Check(std::strcmp(mgscoop::PartnerNativeReloadName(mgscoop::PartnerNativeReload::mirror), "mirror") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeReloadName(mgscoop::PartnerNativeReload::raw), "raw") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeReloadName(mgscoop::PartnerNativeReload::event), "event") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeSlotName(mgscoop::PartnerNativeSlot::off), "off") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeSlotName(mgscoop::PartnerNativeSlot::write), "write") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeSlotName(mgscoop::PartnerNativeSlot::call), "call") == 0,
              "weapons names");
        // What body 1 carries (W2/W3): stock46 by default; the native gun profile defaults the three
        // weapons keys to event / write / substitute unless lab.ini names them.
        Check(def.partner_native_equip == mgscoop::PartnerNativeEquip::stock46 && !def.partner_native_equip_given,
              "equip default: stock46");
        const auto gunProfile = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=1\n");
        Check(gunProfile.partner_native_reload == mgscoop::PartnerNativeReload::event &&
                  gunProfile.partner_native_slot == mgscoop::PartnerNativeSlot::write &&
                  gunProfile.partner_native_equip == mgscoop::PartnerNativeEquip::substitute && gunProfile.invalid_values == 0,
              "the native gun profile defaults: event / write / substitute");
        Check(gunProfile.partner_native_equip_shared==mgscoop::PartnerNativeEquipShared::on &&
              gunProfile.partner_native_standin==mgscoop::PartnerNativeStandin::on && !gunProfile.partner_native_equip_measured &&
              gunProfile.partner_native_ride==mgscoop::PartnerNativeRide::off,"weapons4 defaults; undriven gun config keeps ride off and measured opt-in");
        const auto weaponsOff=Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=1\n"
                                  "partner_native_equip_shared=off\npartner_native_standin=off\n");
        Check(weaponsOff.partner_native_equip_shared==mgscoop::PartnerNativeEquipShared::off &&
              weaponsOff.partner_native_standin==mgscoop::PartnerNativeStandin::off,"named weapons4 off keys are preserved");
        const auto weaponsLab=Lab("enabled=1\nrole=host\npartner_native_equip_shared=prefer\npartner_native_standin=on\n"
                                  "partner_native_equip_measured=1\npartner_native_ride=hide\n");
        Check(weaponsLab.invalid_values==0 && weaponsLab.partner_native_equip_shared==mgscoop::PartnerNativeEquipShared::prefer &&
              weaponsLab.partner_native_equip_measured && weaponsLab.partner_native_ride==mgscoop::PartnerNativeRide::hide,
              "lab-only measured and shared preference; explicit ride hide parses");
        const auto weapons4Bad=Lab("enabled=1\nrole=host\npartner_native_equip_shared=all\npartner_native_standin=yes\n"
                                  "partner_native_equip_measured=all\npartner_native_ride=on\n");
        Check(weapons4Bad.invalid_values==4,"invalid weapons4 and ride values refuse");
        // Anim round 1 weapons (docs/anim-round1-weapons.md): the unsupported gate, the auto-fire stop
        // and the state aim intent follow the gun profile; the reload rate and the census stay as named.
        Check(gunProfile.partner_native_unsupported && gunProfile.partner_native_dry_stop &&
                  gunProfile.partner_native_aim_intent == mgscoop::PartnerNativeAimIntent::state &&
                  gunProfile.partner_native_reload_rate == mgscoop::PartnerNativeReloadRate::adapt && !gunProfile.partner_fire_census,
              "anim weapons: gun profile defaults unsupported/dry on, aim intent state, rate adapt, census off");
        Check(!def.partner_native_unsupported && !def.partner_native_dry_stop &&
                  def.partner_native_aim_intent == mgscoop::PartnerNativeAimIntent::pad &&
                  def.partner_native_reload_rate == mgscoop::PartnerNativeReloadRate::off && !def.partner_fire_census,
              "anim weapons: off outside the gun profile");
        const auto animNamed = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=1\npartner_native_unsupported=0\n"
                                   "partner_native_dry_stop=0\npartner_native_aim_intent=pad\npartner_native_reload_rate=adapt\npartner_fire_census=1\n");
        Check(animNamed.invalid_values == 0 && !animNamed.partner_native_unsupported && !animNamed.partner_native_dry_stop &&
                  animNamed.partner_native_aim_intent == mgscoop::PartnerNativeAimIntent::pad &&
                  animNamed.partner_native_reload_rate == mgscoop::PartnerNativeReloadRate::adapt && animNamed.partner_fire_census,
              "anim weapons: named keys are preserved over the gun profile");
        const auto animLoco = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=0\npartner_native_aim_intent=state\n");
        Check(animLoco.partner_native_aim_intent == mgscoop::PartnerNativeAimIntent::state && !animLoco.partner_native_unsupported,
              "anim weapons: a named aim intent outside the gun profile");
        const auto animBad = Lab("enabled=1\nrole=host\npartner_native_unsupported=2\npartner_native_dry_stop=maybe\n"
                                 "partner_native_aim_intent=on\npartner_native_reload_rate=fast\npartner_fire_census=2\n");
        Check(animBad.invalid_values == 5 && animBad.unknown_keys == 0, "anim weapons: invalid values refuse (known keys)");
        Check(std::strcmp(mgscoop::PartnerNativeAimIntentName(mgscoop::PartnerNativeAimIntent::state), "state") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeReloadRateName(mgscoop::PartnerNativeReloadRate::adapt), "adapt") == 0,
              "anim weapons: names");
        // A visibility default follows actual native driving, not just a gun
        // toggle. The parser's final resolver preserves explicit keys in any order.
        Check(def.partner_native_ride == PartnerNativeRide::off && !def.partner_native_ride_given,
              "legacy/default config does not enable ride hiding");
        for (const char* mode : {"lab", "play"}) {
            const std::string prefix = std::string("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=") + mode + "\nnet=1\nnet_bind=any\npartner=1\npartner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\n";
            for (const char* profile : {"loco", "gun"}) {
                const std::string profileKey = std::string("partner_native_profile=") + profile + "\n";
                const auto autoRide = Lab(prefix + profileKey);
                Check(autoRide.partner_native_ride == mgscoop::kPartnerNativeRideDriven && !autoRide.partner_native_ride_given &&
                      autoRide.partner_input_defer == PartnerDefer::on, "driven native profile defaults ride hide in lab/play");
                Check(autoRide.partner_ride_seat_enter == mgscoop::PartnerRideSeatEnter::getin &&
                      autoRide.partner_ride_seat_exit == mgscoop::PartnerRideSeatExit::getoff, "seat enter/exit defaults getin/getoff");
                for (const bool keyFirst : {false, true}) {
                    const auto off = Lab(prefix + (keyFirst ? "partner_native_ride=off\n" + profileKey : profileKey + "partner_native_ride=off\n"));
                    Check(off.partner_native_ride == PartnerNativeRide::off && off.partner_native_ride_given,
                          "explicit ride off wins before/after either native profile in lab/play");
                    const auto hide = Lab(prefix + (keyFirst ? "partner_native_ride=hide\n" + profileKey : profileKey + "partner_native_ride=hide\n"));
                    Check(hide.partner_native_ride == PartnerNativeRide::hide && hide.partner_native_ride_given,
                          "explicit ride hide is admitted before/after either native profile in lab/play");
                    const auto seat = Lab(prefix + (keyFirst ? "partner_native_ride=seat\npartner_ride_seat_enter=direct\n" + profileKey
                                                             : profileKey + "partner_native_ride=seat\npartner_ride_seat_exit=stop\n"));
                    Check(seat.invalid_values == 0 && seat.partner_native_ride == PartnerNativeRide::seat && seat.partner_native_ride_given &&
                          seat.partner_ride_seat_enter == (keyFirst ? mgscoop::PartnerRideSeatEnter::direct : mgscoop::PartnerRideSeatEnter::getin) &&
                          seat.partner_ride_seat_exit == (keyFirst ? mgscoop::PartnerRideSeatExit::getoff : mgscoop::PartnerRideSeatExit::stop),
                          "explicit ride seat and the seat enter/exit keys are admitted in lab/play");
                }
            }
        }
        const auto rawRide = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_input_defer=on\n");
        const auto censusRide = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_input_defer=census\npartner_native_gun=1\n");
        Check(rawRide.partner_native_ride == mgscoop::kPartnerNativeRideDriven && censusRide.partner_native_ride == PartnerNativeRide::off &&
              !censusRide.partner_native_ride_given, "raw driven offline/loco stage takes the driven ride default; census never inherits it");
        const auto badRide = Lab("enabled=1\nrole=host\npartner_native_profile=gun\npartner_native_ride=on\n");
        const auto namedBadRide = Lab("enabled=1\nrole=host\npartner_native_profile=gun\npartner_native_ride=off\npartner_native_ride=on\n");
        Check(badRide.invalid_values == 1 && !badRide.partner_native_ride_given && badRide.partner_native_ride == mgscoop::kPartnerNativeRideDriven &&
              namedBadRide.invalid_values == 1 && namedBadRide.partner_native_ride_given && namedBadRide.partner_native_ride == PartnerNativeRide::off,
              "invalid ride value inherits default but cannot erase a valid explicit off");
        const auto badSeat = Lab("enabled=1\nrole=host\npartner_ride_seat_enter=walk\npartner_ride_seat_exit=jump\npartner_native_ride=Seat\n");
        Check(badSeat.invalid_values == 3 && badSeat.partner_ride_seat_enter == mgscoop::PartnerRideSeatEnter::getin &&
              badSeat.partner_ride_seat_exit == mgscoop::PartnerRideSeatExit::getoff && badSeat.partner_native_ride == PartnerNativeRide::off,
              "invalid seat enter/exit/ride values refuse and keep the defaults");
        Check(mgscoop::kPartnerNativeRideDriven == PartnerNativeRide::seat, "the driven default draws the remote seated (hidden runs 8b8e5bbc, 77d24c5e)");
        Check(std::strcmp(mgscoop::PartnerNativeRideName(PartnerNativeRide::seat),"seat")==0 &&
              std::strcmp(mgscoop::PartnerNativeRideName(PartnerNativeRide::off),"off")==0 &&
              std::strcmp(mgscoop::PartnerRideSeatEnterName(mgscoop::PartnerRideSeatEnter::getin),"getin")==0 &&
              std::strcmp(mgscoop::PartnerRideSeatEnterName(mgscoop::PartnerRideSeatEnter::direct),"direct")==0 &&
              std::strcmp(mgscoop::PartnerRideSeatExitName(mgscoop::PartnerRideSeatExit::getoff),"getoff")==0 &&
              std::strcmp(mgscoop::PartnerRideSeatExitName(mgscoop::PartnerRideSeatExit::stop),"stop")==0, "seat names");
        Check(std::strcmp(mgscoop::PartnerNativeRideName(mgscoop::PartnerNativeRide::hide),"hide")==0 &&
              std::strcmp(mgscoop::PartnerNativeEquipSharedName(mgscoop::PartnerNativeEquipShared::prefer),"prefer")==0,
              "weapons4 and ride names");
        // WS3 vehicles (docs/vehicle-ws3.md): the ride census is a lab measurement, default off in every
        // profile, explicit only, and always off in play. The seat by id (hidden run abb0b7cf: the remote
        // driver seated in the moving Mother Base jeep) and RIDE_TILT (hidden run 0a2314f2: the viewer's
        // jeep pitched with the seated driver) are on by default, in play too; an explicit 0 wins.
        Check(!def.partner_ride_census && def.partner_ride_seat_id && def.partner_ride_tilt,
              "WS3 census default off, seat id and tilt default on");
        const auto vehicleGun = Lab("enabled=1\nrole=host\npartner_native_profile=gun\n");
        Check(!vehicleGun.partner_ride_census && vehicleGun.partner_ride_seat_id && vehicleGun.partner_ride_tilt,
              "the gun profile keeps the WS3 defaults (census off, seat id and tilt on)");
        const auto vehicleKeys = Lab("enabled=1\nrole=host\npartner_ride_census=1\npartner_ride_seat_id=0\npartner_ride_tilt=0\n");
        Check(vehicleKeys.invalid_values == 0 && vehicleKeys.unknown_keys == 0 && vehicleKeys.partner_ride_census &&
                  !vehicleKeys.partner_ride_seat_id && !vehicleKeys.partner_ride_tilt,
              "WS3 vehicle keys parse in lab (seat_id=0 and tilt=0 turn them off)");
        const auto vehicleOn = Lab("enabled=1\nrole=host\npartner_ride_seat_id=1\n");
        Check(vehicleOn.invalid_values == 0 && vehicleOn.partner_ride_seat_id, "an explicit seat_id=1 is accepted");
        const auto vehicleBad = Lab("enabled=1\nrole=host\npartner_ride_census=2\npartner_ride_seat_id=maybe\npartner_ride_tilt=\n");
        Check(vehicleBad.invalid_values == 3 && vehicleBad.unknown_keys == 0 && !vehicleBad.partner_ride_census &&
                  vehicleBad.partner_ride_seat_id && vehicleBad.partner_ride_tilt, "invalid WS3 vehicle values refuse and keep the defaults");
        const auto vehiclePlay = Lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                                     "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_ride_census=1\n"
                                     "partner_ride_seat_id=1\npartner_ride_tilt=1\n");
        Check(vehiclePlay.mode == mgscoop::LabMode::play && !vehiclePlay.partner_ride_census && vehiclePlay.partner_ride_seat_id &&
                  vehiclePlay.partner_ride_tilt && vehiclePlay.play_refused == 0,
              "play turns the census off and admits the seat id and RIDE_TILT (no refusal bit)");
        const auto vehiclePlayDefault = Lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                                            "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\n");
        const auto vehiclePlayOff = Lab("enabled=1\nrole=host\nsave_dir=C:\\coop\\saves\nmode=play\nnet=1\nnet_bind=any\npartner=1\n"
                                        "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_dm=map\npartner_ride_seat_id=0\n"
                                        "partner_ride_tilt=0\n");
        Check(vehiclePlayDefault.mode == mgscoop::LabMode::play && vehiclePlayDefault.partner_ride_tilt &&
                  !vehiclePlayDefault.partner_ride_census && vehiclePlayDefault.partner_ride_seat_id &&
                  vehiclePlayOff.mode == mgscoop::LabMode::play && !vehiclePlayOff.partner_ride_tilt &&
                  !vehiclePlayOff.partner_ride_seat_id && vehiclePlayOff.invalid_values == 0 && vehiclePlayOff.play_refused == 0,
              "play: seat id and tilt on by default, an explicit 0 keeps each off");
        // Round-1 actions track (docs/anim-round1-actions.md): the DM overlay lever is off and the act census on by
        // default, in every profile; both keys are explicit only.
        {
            const auto gunDefault = Lab("enabled=1\nrole=host\npartner_native_profile=gun\n");
            Check(def.partner_native_overlay == mgscoop::PartnerNativeOverlay::off && def.partner_act_log &&
                  gunDefault.partner_native_overlay == mgscoop::PartnerNativeOverlay::off && gunDefault.partner_act_log,
                  "overlay off and act census on by default (gun profile too)");
            const auto debugOverlay = Lab("enabled=1\nrole=host\npartner_native_profile=gun\npartner_native_overlay=debug\npartner_act_log=0\n");
            const auto mapOverlay = Lab("enabled=1\nrole=host\npartner_native_overlay=map\n");
            Check(def.partner_native_stance_fast && gunDefault.partner_native_stance_fast &&
                  !Lab("enabled=1\nrole=host\npartner_native_profile=gun\npartner_native_stance_fast=0\n").partner_native_stance_fast,
                  "the native stance window is on by default; partner_native_stance_fast=0 restores the 2 s rule");
            Check(debugOverlay.partner_native_overlay == mgscoop::PartnerNativeOverlay::debug && !debugOverlay.partner_act_log &&
                  mapOverlay.partner_native_overlay == mgscoop::PartnerNativeOverlay::map && debugOverlay.invalid_values == 0,
                  "overlay debug/map and act census off parse");
            const auto badOverlay = Lab("enabled=1\nrole=host\npartner_native_overlay=on\npartner_act_log=maybe\n");
            Check(badOverlay.invalid_values == 2 && badOverlay.partner_native_overlay == mgscoop::PartnerNativeOverlay::off &&
                  badOverlay.partner_act_log, "invalid overlay/act census values refused, defaults kept");
            Check(std::strcmp(mgscoop::PartnerNativeOverlayName(mgscoop::PartnerNativeOverlay::map),"map")==0 &&
                  std::strcmp(mgscoop::PartnerNativeOverlayName(mgscoop::PartnerNativeOverlay::debug),"debug")==0 &&
                  std::strcmp(mgscoop::PartnerNativeOverlayName(mgscoop::PartnerNativeOverlay::off),"off")==0, "overlay names");
        }
        const auto gunNamed = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=1\npartner_native_reload=mirror\n"
                                  "partner_native_slot=off\npartner_native_equip=stock46\n");
        Check(gunNamed.partner_native_reload == mgscoop::PartnerNativeReload::mirror &&
                  gunNamed.partner_native_slot == mgscoop::PartnerNativeSlot::off &&
                  gunNamed.partner_native_equip == mgscoop::PartnerNativeEquip::stock46 && gunNamed.partner_native_equip_given,
              "named weapons keys keep their values under the gun profile");
        const auto locoProfile = Lab("enabled=1\nrole=host\npartner_drive=native\npartner_native_gun=0\n");
        Check(locoProfile.partner_native_reload == mgscoop::PartnerNativeReload::mirror &&
                  locoProfile.partner_native_slot == mgscoop::PartnerNativeSlot::off &&
                  locoProfile.partner_native_equip == mgscoop::PartnerNativeEquip::stock46,
              "without the gun the weapons keys stay off");
        const auto equipManifest = Lab("enabled=1\nrole=host\npartner_native_equip=manifest\n");
        const auto equipBad = Lab("enabled=1\nrole=host\npartner_native_equip=all\n");
        Check(equipManifest.partner_native_equip == mgscoop::PartnerNativeEquip::manifest && equipManifest.invalid_values == 0 &&
                  equipBad.invalid_values == 1 && equipBad.partner_native_equip == mgscoop::PartnerNativeEquip::stock46,
              "equip manifest parses; an unknown value refuses and keeps stock46");
        Check(std::strcmp(mgscoop::PartnerNativeEquipName(mgscoop::PartnerNativeEquip::stock46), "stock46") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeEquipName(mgscoop::PartnerNativeEquip::manifest), "manifest") == 0 &&
                  std::strcmp(mgscoop::PartnerNativeEquipName(mgscoop::PartnerNativeEquip::substitute), "substitute") == 0,
              "equip names");
        // defer takes effect only with drive=native.
        const auto deferOnly = Lab("enabled=1\nrole=host\npartner_input_defer=on\n");
        Check(deferOnly.partner_input_defer == PartnerDefer::on &&
              EffectivePartnerDefer(deferOnly.partner_drive, deferOnly.partner_input_defer) == PartnerDefer::off &&
              EffectivePartnerDefer(PartnerDrive::native, PartnerDefer::census) == PartnerDefer::census,
              "defer is ignored without drive=native");
        // The effective-mask allowance: FIRE never without gun.
        Check(PartnerNativeAllowed(false, false) == 0x2240006u && PartnerNativeAllowed(true, false) == 0x224004Eu &&
              PartnerNativeAllowed(true, true) == 0x224005Eu && PartnerNativeAllowed(false, true) == 0x2240006u,
              "native allowance: base (with EVADE), +HOLD/RELOAD, +FIRE only with gun");
        Check((PartnerNativeAllowed(true, true) & (partner_pad::kAction | partner_pad::kCqc | partner_pad::kCall | partner_pad::kSubject)) == 0u &&
              (kPartnerNativeBaseMask & partner_pad::kEvade) != 0u, "ACTION/CQC/CALL/SUBJECT never allowed; EVADE in the base");

        Check(std::strcmp(PartnerDriveName(PartnerDrive::native), "native") == 0 && std::strcmp(PartnerDeferName(PartnerDefer::census), "census") == 0 &&
              std::strcmp(PartnerLocalName(PartnerLocal::all), "all") == 0 &&
              std::strcmp(PartnerNativeAimName(PartnerNativeAim::lookcam), "lookcam") == 0 &&
              std::strcmp(PartnerDeferName(PartnerDefer::off), "off") == 0, "native names");
        // Play mode forces the legacy drive and sets kPlayRefusedNativeDrive.
        const auto playNative = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\npartner_drive=native\npartner_input_defer=on\n");
        Check(playNative.partner_drive == PartnerDrive::legacy && playNative.partner_input_defer == PartnerDefer::off &&
              playNative.play_refused == kPlayRefusedNativeDrive && playNative.invalid_values == 1, "play refuses the native drive");
        const auto playDefer = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\npartner_input_defer=census\n");
        Check(playDefer.partner_input_defer == PartnerDefer::off && playDefer.play_refused == kPlayRefusedNativeDrive,
              "play refuses a lone defer key too");
        const auto playLegacy = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\npartner_drive=legacy\npartner_native_gun=1\n");
        Check(playLegacy.play_refused == 0 && playLegacy.invalid_values == 0, "play keeps an explicit legacy drive");
        const auto playCensus = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\npartner_fire_census=1\npartner_native_reload_rate=adapt\n");
        Check(!playCensus.partner_fire_census && playCensus.partner_native_reload_rate == mgscoop::PartnerNativeReloadRate::adapt,
              "play: the fire census is lab only; the reload rate key is kept");
        const auto playPlaced = Lab("enabled=1\nmode=play\nrole=host\n" + saves + "net=1\npartner_placed_census=1\n");
        Check(!playPlaced.partner_placed_census && playPlaced.invalid_values == 0 && playPlaced.play_refused == 0,
              "play: the placed census is forced off (lab only), not a play refusal");
        Check(kPlayRefusedNativeDrive == 0x20000 && kPlayRefusedNativeDrive == (kPlayRefusedDebugSoldierWarp << 1), "native refusal bit");
        // Every play refusal is its own bit (no two refusals share one).
        const std::uint32_t play_refusal_bits[] = {kPlayRefusedLoopback, kPlayRefusedSelfSteamId, kPlayRefusedPeerSteamId, kPlayRefusedSelfName,
                                          kPlayRefusedMutexSuffix, kPlayRefusedXInput, kPlayRefusedJoinTrigger, kPlayRefusedAutoJoin,
                                          kPlayRefusedHostPeerAddr, kPlayRefusedBindLoopback, kPlayRefusedLegacyLink, kPlayRefusedPeerAddr,
                                          kPlayRefusedNoSaveDir, kPlayRefusedGuestNoPeer, kPlayRefusedImpairment, kPlayRefusedWorldProbe,
                                          kPlayRefusedDebugSoldierWarp, kPlayRefusedNativeDrive, kPlayRefusedSteamSelftest,
                                          kPlayRefusedHostPeerSteam, kPlayRefusedNoPeerSteam, kPlayRefusedUdpKeysWithSteam,
                                          kPlayRefusedRehearsal, kPlayRefusedNativeProfile};
        std::uint32_t refusal_union = 0;
        bool refusal_distinct = true;
        for (const std::uint32_t bit : play_refusal_bits) {
            refusal_distinct = refusal_distinct && bit != 0 && (bit & (bit - 1)) == 0 && (refusal_union & bit) == 0;
            refusal_union |= bit;
        }
        Check(refusal_distinct && refusal_union == 0xFFFFFFu, "kPlayRefused* are 24 distinct single bits");
    }
    // Online play (docs/native-puppet.md "Online"): partner_native_profile, the
    // play defaults of the network policy, the raw native keys still refused,
    // and the worker-only play rehearsal.
    {
        const std::string play_saves = "save_dir=C:\\coop\\play_saves\n";
        // Play-Coop's dm=map partner keys (tools/CoopPlay.psm1 Get-PlayPartnerDefaults) plus the send key.
        const std::string clips = "partner=1\npartner_stage=3\npartner_source=net\npartner_apply=xform\npartner_anim=off\npartner_aset=qs\n"
                                  "partner_reset=n\npartner_look=net\npartner_dm=map\npartner_input_send=1\n";
        const std::string play = "enabled=1\nmode=play\nrole=host\n" + play_saves + "net=1\nnet_bind=any\nnet_listen_port=47830\n" + clips;
        // The worker's gun stage, raw (tools/worker/WorkerSessionCommon.psm1 Get-WorkerPartnerSettings native gun).
        const std::string gun_raw =
            "partner_drive=native\npartner_input_send=1\npartner_input_hook=1\npartner_input_hold=own\npartner_input_words=hpr\n"
            "partner_input_wrap=window\npartner_input_cam=sandbox\npartner_input_local=main\npartner_recon=track\npartner_input_slot=body\n"
            "partner_input_defer=on\npartner_input_mask=0x224005E\npartner_native_gun=1\npartner_native_fire=visual\npartner_native_aim=look\n"
            "partner_input_triggers=1\npartner_input_rstick=1\npartner_native_rates=mirror\n";
        const std::string loco_raw =
            "partner_drive=native\npartner_input_send=1\npartner_input_hook=1\npartner_input_hold=own\npartner_input_words=hpr\n"
            "partner_input_wrap=window\npartner_input_cam=sandbox\npartner_input_local=main\npartner_recon=track\npartner_input_slot=body\n"
            "partner_input_defer=on\npartner_input_mask=0x2240006\npartner_native_aim=off\npartner_native_rates=mirror\n";
        const auto same_stage = [](const LabConfig& a, const LabConfig& b) {
            return a.partner_drive == b.partner_drive && a.partner_input_defer == b.partner_input_defer &&
                   a.partner_input_send == b.partner_input_send && a.partner_input_hook == b.partner_input_hook &&
                   a.partner_input_hold == b.partner_input_hold && std::memcmp(a.partner_input_words, b.partner_input_words, 3) == 0 &&
                   a.partner_input_wrap == b.partner_input_wrap && a.partner_input_cam == b.partner_input_cam &&
                   a.partner_input_local == b.partner_input_local && a.partner_recon == b.partner_recon &&
                   a.partner_input_slot == b.partner_input_slot && a.partner_input_mask == b.partner_input_mask &&
                   a.partner_native_gun == b.partner_native_gun && a.partner_native_fire == b.partner_native_fire &&
                   a.partner_native_aim == b.partner_native_aim && a.partner_input_triggers == b.partner_input_triggers &&
                   a.partner_input_rstick == b.partner_input_rstick && a.partner_native_rates == b.partner_native_rates &&
                   a.partner_native_noise == b.partner_native_noise && a.partner_input_p218_allow == b.partner_input_p218_allow &&
                   a.partner_native_aimpt == b.partner_native_aimpt && a.partner_native_ride == b.partner_native_ride;
        };
        // The profile is exactly the stage the worker qualified (lab and play).
        const auto labGunRaw = Lab("enabled=1\nrole=host\n" + clips + gun_raw);
        const auto labGun = Lab("enabled=1\nrole=host\n" + clips + "partner_native_profile=gun\n");
        Check(labGun.partner_native_profile == PartnerNativeProfile::gun && labGun.invalid_values == 0 && same_stage(labGun, labGunRaw),
              "profile=gun equals the worker's raw gun stage");
        const auto labLocoRaw = Lab("enabled=1\nrole=host\n" + clips + loco_raw);
        const auto labLoco = Lab("enabled=1\nrole=host\n" + clips + "partner_native_profile=loco\n");
        Check(labLoco.partner_native_profile == PartnerNativeProfile::loco && same_stage(labLoco, labLocoRaw) &&
              labLoco.partner_input_mask == 0x2240006u && !labLoco.partner_native_gun, "profile=loco equals the worker's raw loco stage");
        Check(labGun.partner_native_net == PartnerNativeNet::lab && labGun.partner_native_stall == PartnerNativeStall::neutral &&
              labGun.partner_input_edge_repeats == 2 && labGun.partner_native_hold == PartnerNativeHold::own,
              "the lab keeps the qualified network behaviour");
        // A key named in the same lab.ini keeps its value (the looksrc A/B run).
        const auto labSrc = Lab("enabled=1\nrole=host\n" + clips + "partner_native_aim=looksrc\npartner_native_profile=gun\n");
        Check(labSrc.partner_native_aim == PartnerNativeAim::looksrc && labSrc.partner_drive == PartnerDrive::native, "named keys win");
        Check(Lab("enabled=1\nrole=host\npartner_native_profile=off\n").partner_drive == PartnerDrive::legacy &&
              Lab("enabled=1\nrole=host\npartner_native_profile=native\n").invalid_values == 1, "profile off / badOnline value");
        // Play: the profile is the way to the native drive, with the online defaults.
        const auto playGun = Lab(play + "partner_native_profile=gun\nworld=3\n");
        Check(playGun.mode == LabMode::play && playGun.play_refused == 0 && playGun.invalid_values == 0 && same_stage(playGun, labGunRaw) &&
              playGun.partner_drive == PartnerDrive::native && playGun.partner_input_defer == PartnerDefer::on,
              "play: profile=gun gives the qualified gun stage, nothing refused");
        Check(playGun.partner_native_net == PartnerNativeNet::online && playGun.partner_native_stall == PartnerNativeStall::hold &&
              playGun.partner_native_stall_ms == 400 && playGun.partner_input_edge_repeats == kPlayEdgeRepeats &&
              playGun.partner_native_hold == PartnerNativeHold::own, "play defaults: online, stall hold 400 ms, 5 edge repeats, hold own");
        // The weapons keys resolve after the profile expansion (ResolveWeaponDefaults, last).
        Check(playGun.partner_native_reload == PartnerNativeReload::event && playGun.partner_native_slot == PartnerNativeSlot::write &&
              playGun.partner_native_equip == PartnerNativeEquip::substitute && playGun.partner_native_aimpt == PartnerNativeAimPt::point,
              "play: profile=gun runs the partner's own reload, the slot follower, the generalized writer and the aim mirror");
        const auto weaponsPlay=Lab(play+"partner_native_profile=gun\npartner_native_equip_shared=prefer\n"
                                   "partner_native_equip_measured=1\npartner_native_ride=hide\n");
        Check(weaponsPlay.partner_native_equip_shared==PartnerNativeEquipShared::on && !weaponsPlay.partner_native_equip_measured &&
              weaponsPlay.partner_native_ride==PartnerNativeRide::hide && weaponsPlay.partner_native_standin==PartnerNativeStandin::on,
              "play uses shared on and explicit ride hide; measured remains lab-only");
        const auto playNamed = Lab(play + "partner_native_profile=gun\npartner_native_slot=call\npartner_native_equip=stock46\n");
        Check(playNamed.play_refused == 0 && playNamed.partner_native_slot == PartnerNativeSlot::call &&
              playNamed.partner_native_equip == PartnerNativeEquip::stock46 && playNamed.partner_native_reload == PartnerNativeReload::event,
              "play: named weapons keys keep their values (the driver runs call as write)");
        const auto playClips = Lab(play);
        Check(playClips.partner_native_reload == PartnerNativeReload::mirror && playClips.partner_native_slot == PartnerNativeSlot::off &&
              playClips.partner_native_equip == PartnerNativeEquip::stock46, "play without a profile: the weapons defaults");
        Check(playClips.play_refused == 0 && playClips.partner_drive == PartnerDrive::legacy && playClips.partner_input_send &&
              playClips.partner_native_profile == PartnerNativeProfile::off, "play without a profile: the clip partner, sending input");
        const auto playVariants = Lab(play + "partner_native_profile=gun\npartner_native_net=lab\npartner_native_stall=neutral\n"
                                             "partner_native_stall_ms=250\npartner_native_hold=net\npartner_input_edge_repeats=8\n"
                                             "partner_native_aim=looksrc\npartner_native_face=aim\n");
        Check(playVariants.play_refused == 0 && playVariants.invalid_values == 0 && playVariants.partner_native_net == PartnerNativeNet::lab &&
              playVariants.partner_native_stall == PartnerNativeStall::neutral && playVariants.partner_native_stall_ms == 250 &&
              playVariants.partner_native_hold == PartnerNativeHold::net && playVariants.partner_input_edge_repeats == 8 &&
              playVariants.partner_native_aim == PartnerNativeAim::looksrc && playVariants.partner_native_face == PartnerNativeFace::aim,
              "play keeps the variant keys");
        // The raw native keys stay refused in play; the profile still applies.
        const auto playRaw = Lab(play + "partner_native_profile=gun\npartner_drive=native\npartner_input_defer=census\n");
        Check(playRaw.play_refused == kPlayRefusedNativeDrive && playRaw.partner_drive == PartnerDrive::native &&
              playRaw.partner_input_defer == PartnerDefer::on, "raw drive/defer refused, the profile sets them");
        const auto playExp = Lab(play + "partner_native_profile=gun\npartner_native_aim=lookcam\npartner_input_local=all\n"
                                        "partner_input_p218_allow=1\npartner_native_noise=keep\n");
        Check(playExp.play_refused == kPlayRefusedNativeDrive && playExp.invalid_values == 1 &&
              playExp.partner_native_aim == PartnerNativeAim::look && playExp.partner_input_local == PartnerLocal::main &&
              !playExp.partner_input_p218_allow && playExp.partner_native_noise == PartnerNativeNoise::filter,
              "lab-only native experiments refused in play, the profile's values instead");
        const auto playNoBase = Lab("enabled=1\nmode=play\nrole=host\n" + play_saves + "net=1\npartner=1\npartner_source=net\npartner_dm=off\n"
                                    "partner_native_profile=gun\n");
        Check(playNoBase.play_refused == kPlayRefusedNativeProfile && playNoBase.partner_native_profile == PartnerNativeProfile::off &&
              playNoBase.partner_drive == PartnerDrive::legacy, "play: the profile needs the dm=map + xform fallback");
        // Ranges.
        const auto badOnline = Lab("enabled=1\nrole=host\npartner_native_stall_ms=99\npartner_native_stall_ms=1001\npartner_input_edge_repeats=1\n"
                             "partner_input_edge_repeats=9\npartner_native_net=wan\npartner_native_stall=keep\npartner_native_hold=both\n");
        Check(badOnline.invalid_values == 7 && badOnline.partner_native_stall_ms == 400 && badOnline.partner_input_edge_repeats == 2 &&
              badOnline.partner_native_net == PartnerNativeNet::lab, "online keys: invalid values keep the defaults");
        Check(std::strcmp(PartnerNativeProfileName(PartnerNativeProfile::gun), "gun") == 0 &&
              std::strcmp(PartnerNativeNetName(PartnerNativeNet::online), "online") == 0 &&
              std::strcmp(PartnerNativeStallName(PartnerNativeStall::hold), "hold") == 0 &&
              std::strcmp(PartnerNativeHoldName(PartnerNativeHold::net), "net") == 0, "online names");
        // The play rehearsal (the hidden worker's one-PC play): only with the worker build's option.
        const std::string rehearsal = "enabled=1\nmode=play\nrole=guest\n" + play_saves + "net=1\nnet_bind=loopback\nnet_listen_port=47831\n"
                                      "net_peer_addr=127.0.0.1\nnet_peer_port=47830\nmutex_suffix=worker_guest_0123abcd\nxinput=none\n"
                                      "net_latency_ms=40\nplay_rehearsal=1\n" + clips + "partner_native_profile=gun\n";
        LabConfig friendView{};
        Check(ParseLabConfig(rehearsal.data(), rehearsal.size(), friendView), "rehearsal parse");
        Check((friendView.play_refused & kPlayRefusedRehearsal) && (friendView.play_refused & kPlayRefusedMutexSuffix) &&
              (friendView.play_refused & kPlayRefusedXInput) && (friendView.play_refused & kPlayRefusedBindLoopback) &&
              (friendView.play_refused & kPlayRefusedImpairment) && (friendView.play_refused & kPlayRefusedPeerAddr) &&
              !friendView.play_rehearsal && !friendView.mutex_suffix[0] && friendView.net_latency_ms == 0,
              "without the worker option the rehearsal keys are play refusals");
        LabConfig workerView{};
        LabParseOptions options{};
        options.allow_play_rehearsal = true;
        Check(ParseLabConfig(rehearsal.data(), rehearsal.size(), workerView, options), "rehearsal parse (worker)");
        Check(workerView.play_refused == 0 && workerView.invalid_values == 0 && workerView.play_rehearsal && workerView.net &&
              workerView.net_bind_loopback && workerView.net_peer_address == 0x7F000001u && workerView.net_listen_port == 47831 &&
              workerView.net_peer_port == 47830 && std::strcmp(workerView.mutex_suffix, "worker_guest_0123abcd") == 0 &&
              workerView.xinput == kXInputNone && workerView.net_latency_ms == 40 && workerView.partner_drive == PartnerDrive::native,
              "the worker build relaxes exactly loopback, mutex, xinput=none and impairment");
        const std::string identity =
            rehearsal + "loopback=1\nlisten_port=47810\npeer_port=47811\nself_steamid=76561202255233008\nxinput=0\njoin_trigger=1\n";
        LabConfig stillRefused{};
        Check(ParseLabConfig(identity.data(), identity.size(), stillRefused, options), "rehearsal identity parse");
        Check((stillRefused.play_refused & kPlayRefusedLoopback) && (stillRefused.play_refused & kPlayRefusedSelfSteamId) &&
              (stillRefused.play_refused & kPlayRefusedXInput) && (stillRefused.play_refused & kPlayRefusedJoinTrigger),
              "the rehearsal never relaxes the identity hacks or a real pad index");
        Check(Lab("enabled=1\nrole=host\nplay_rehearsal=1\n").invalid_values == 1 && !Lab("enabled=1\nrole=host\nplay_rehearsal=1\n").play_rehearsal,
              "play_rehearsal is meaningless in the lab");
    }
    // Partner name tag (docs/partner-nametag.md): lab.ini and coop.ini keys.
    {
        const auto none = Lab("enabled=1\nrole=host\n");
        Check(none.nametag == NametagSetting::unset && none.nametag_crosshair == NametagCrosshair::unset && !none.nametag_name[0] &&
              none.nametag_max_m == 300, "name tag defaults: unset (coop.ini, then on/aim), 300 m");
        const auto all = Lab("enabled=1\nrole=guest\nnametag=0\nnametag_crosshair=always\nnametag_name=Guest Tag_1.x-y\nnametag_max_m=1000\n");
        Check(all.nametag == NametagSetting::off && all.nametag_crosshair == NametagCrosshair::always &&
              std::strcmp(all.nametag_name, "Guest Tag_1.x-y") == 0 && all.nametag_max_m == 1000 && all.invalid_values == 0 && all.unknown_keys == 0,
              "name tag keys parsed");
        Check(Lab("enabled=1\nrole=host\nnametag=on\nnametag_crosshair=off\n").nametag == NametagSetting::on &&
              Lab("enabled=1\nrole=host\nnametag_crosshair=off\n").nametag_crosshair == NametagCrosshair::off &&
              Lab("enabled=1\nrole=host\nnametag_crosshair=aim\n").nametag_crosshair == NametagCrosshair::aim, "boolean and enum spellings");
        const auto tagBad = Lab("enabled=1\nrole=host\nnametag=2\nnametag_crosshair=sometimes\nnametag_crosshair=unset\nnametag_name=a;b\n"
                             "nametag_max_m=49\nnametag_max_m=1001\n");
        Check(tagBad.nametag == NametagSetting::unset && tagBad.nametag_crosshair == NametagCrosshair::unset && !tagBad.nametag_name[0] &&
              tagBad.nametag_max_m == 300 && tagBad.invalid_values == 6, "invalid name tag values keep the defaults");
        Check(!Lab("enabled=1\nrole=host\nnametag_name=ABCDEFGHIJKLMNOPQRSTUVWXY\n").nametag_name[0] &&
              std::strlen(Lab("enabled=1\nrole=host\nnametag_name=ABCDEFGHIJKLMNOPQRSTUVWX\n").nametag_name) == 24 &&
              !Lab("enabled=1\nrole=host\nnametag_name=\xC3\xA9t\xC3\xA9\n").nametag_name[0] &&
              !Lab("enabled=1\nrole=host\nnametag_name=\n").nametag_name[0], "nametag_name: 1..24 of [A-Za-z0-9 _.-]");
        Check(Lab("enabled=1\nrole=host\nnametag_name= Pad \n").nametag_name[0] == 'P', "surrounding blanks are trimmed like every key");
        // Play mode allows the read-only overlay keys (no refusal bit).
        const auto play = Lab("enabled=1\nmode=play\nrole=host\nsave_dir=C:\\coop\\saves\nnet=1\nnametag=1\nnametag_crosshair=always\nnametag_name=Snake\n");
        Check(play.mode == LabMode::play && play.play_refused == 0 && play.nametag == NametagSetting::on &&
              play.nametag_crosshair == NametagCrosshair::always && std::strcmp(play.nametag_name, "Snake") == 0, "play keeps the name tag keys");
        const auto tagCoop = Coop("name=Big Boss \xC3\xA9\nnametag=off\nfriendly_crosshair=always\n");
        Check(std::strcmp(tagCoop.name, "Big Boss \xC3\xA9") == 0 && tagCoop.nametag == NametagSetting::off &&
              tagCoop.friendly_crosshair == NametagCrosshair::always && tagCoop.invalid_values == 0 && tagCoop.unknown_keys == 0, "coop.ini name tag keys");
        const auto coopBad = Coop(std::string("name=a\x01") + "b\nnametag=maybe\nfriendly_crosshair=never\nname=" + std::string(65, 'x') + "\n");
        Check(!coopBad.name[0] && coopBad.nametag == NametagSetting::unset && coopBad.friendly_crosshair == NametagCrosshair::unset &&
              coopBad.invalid_values == 4, "coop.ini refuses controls, bad values and names over 64 bytes");
        Check(std::strlen(Coop("name=" + std::string(64, 'x') + "\n").name) == 64, "a 64-byte coop.ini name");
        NametagCrosshair parsed{};
        Check(ParseNametagCrosshair("always", 6, parsed) && parsed == NametagCrosshair::always && ParseNametagCrosshair("0", 1, parsed) &&
              parsed == NametagCrosshair::off && !ParseNametagCrosshair("", 0, parsed) && !ParseNametagCrosshair(nullptr, 3, parsed) &&
              std::strcmp(NametagCrosshairName(NametagCrosshair::aim), "aim") == 0 && !NametagCrosshairName(NametagCrosshair::unset)[0],
              "crosshair spellings");
    }

    std::printf("%u lab/coop configuration checks passed; pure parser, no game calls\n", checks);
}
