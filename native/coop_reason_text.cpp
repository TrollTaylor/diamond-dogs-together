// Original MGS Co-op plain-language link reasons, in the words of the Diamond Dogs
// Together launcher (Steam online play; shared with tools/CoopPlay.psm1 and the
// launcher core through tests/fixtures/link-reasons.json).
#include "mgscoop/coop_reason_text.hpp"

#include <cstdio>
#include <cstring>

namespace mgscoop::coop_text {
namespace {
bool Is(const char* a, const char* b) noexcept { return std::strcmp(a, b) == 0; }
bool Starts(const char* a, const char* prefix) noexcept { return std::strncmp(a, prefix, std::strlen(prefix)) == 0; }

bool Put(char* out, std::size_t capacity, const char* text) noexcept {
    if (!out || !capacity) return false;
    const std::size_t n = std::strlen(text);
    if (n >= capacity) {
        out[0] = 0;
        return false;
    }
    std::memcpy(out, text, n + 1);
    return true;
}

template <typename... Args>
bool PutF(char* out, std::size_t capacity, const char* format, Args... args) noexcept {
    if (!out || !capacity) return false;
    const int n = std::snprintf(out, capacity, format, args...);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) {
        out[0] = 0;
        return false;
    }
    return true;
}

// Steam gates that mean "Steam networking changed" (a Steam update).
const char* const kSteamChanged[] = {"steam_client_module", "steam_client_iface", "steam_iface_missing",
                                     "steam_iface_module",  "steam_iface_rtti",   "steam_iface_shape"};
}  // namespace

bool FormatLinkReason(const char* reason, const char* remote_build, const char* own_build, bool verified, char* out,
                      std::size_t capacity) noexcept {
    const char* r = reason ? reason : "";
    const char* remote = remote_build && remote_build[0] ? remote_build : "another version";
    const char* own = own_build ? own_build : "";
    if (!r[0]) return Put(out, capacity, "");
    if (Is(r, "build_mismatch"))
        return PutF(out, capacity, "Your partner runs Diamond Dogs Together %s; you run %s. Both must use the same version.", remote, own);
    if (Is(r, "proto_mismatch"))
        return PutF(out, capacity, "Your partner's Diamond Dogs Together (%s) speaks another link version. Both must use the same version.", remote);
    if (Is(r, "mode_mismatch"))
        return Put(out, capacity, "One side is in the single-PC lab mode. Both players must start the game with Diamond Dogs Together.");
    if (Is(r, "role_mismatch")) return Put(out, capacity, "Both players chose the same role: one must Host, the other Join.");
    if (Is(r, "busy")) return Put(out, capacity, "The host is already connected to someone.");
    if (Is(r, "bad_code"))
        return Put(out, capacity,
                   "The host did not accept the connection. Possibly a wrong code: copy it again (or the host started a new game: ask for "
                   "the new code).");
    if (Starts(r, "secret_")) return Put(out, capacity, "The link secret file is missing or damaged. Quit MGSV and host or join again in Diamond Dogs Together.");
    if (Is(r, "crypto")) return Put(out, capacity, "Windows could not provide random numbers for the link. Restart the PC and try again.");
    if (Is(r, "no_peer_addr")) return Put(out, capacity, "The host address is missing. Quit MGSV and join again.");
    if (Is(r, "no_peer_steam"))
        return Put(out, capacity,
                   "The host's Steam account is missing. Quit MGSV and join again (paste the code or wait for an invite).");
    if (Is(r, "steam_offline"))
        return Put(out, capacity, "Steam is offline or not logged in. Quit MGSV, start Steam and log in (online), then host or join again.");
    if (Is(r, "steam_self_mismatch"))
        return Put(out, capacity, "Steam is logged in to another account than when you pressed Host or Join. Quit MGSV and try again.");
    if (Is(r, "steam_peer_is_self")) return Put(out, capacity, "That join code is your own. Ask your friend for theirs.");
    if (Is(r, "steam_api"))
        return Put(out, capacity, "steam_api64.dll is not the expected file. Verify the game files in Steam, then start Diamond Dogs Together again.");
    if (Is(r, "steam_not_initialized"))
        return Put(out, capacity, "Steam did not start for MGSV. Quit the game and host or join again (Diamond Dogs Together starts MGSV through Steam).");
    for (const char* changed : kSteamChanged)
        if (Is(r, changed))
            return Put(out, capacity,
                       "Steam networking changed (probably a Steam update). Use Tools > Collect logs and send the file to the developer.");
    if (Is(r, "steam_owner_unavailable"))
        return Put(out, capacity, "The co-op loader could not attach to Steam. Use Tools > Collect logs and send the file to the developer.");
    if (Is(r, "steam_fault"))
        return Put(out, capacity,
                   "Steam networking failed inside Steam: restart the game. If it happens again, use Tools > Collect logs.");
    if (Is(r, "steam_acquire_timeout"))
        return Put(out, capacity, "Steam networking did not start within two minutes. Check that Steam is online, then restart the game.");
    if (Starts(r, "steam_"))
        return PutF(out, capacity,
                    "The Steam link could not start (%s). Restart the game; if it repeats, use Tools > Collect logs.", r);
    if (Is(r, "transport_bind"))
        return Put(out, capacity,
                   "The UDP port is already in use by another program on this PC (or a second MGSV). Close it, or use another port.");
    if (Starts(r, "transport_"))
        return PutF(out, capacity, "The network link could not start (%s). Restart the PC and try again; if it repeats, use Tools > Collect logs.", r);
    if (!verified) return PutF(out, capacity, "The connection was refused (%s). Possibly a wrong code.", r);
    return PutF(out, capacity, "The connection was refused (%s).", r);
}

bool FormatSteamEnd(std::int32_t end, char* out, std::size_t capacity) noexcept {
    if (!end) return Put(out, capacity, "");
    switch (end) {
    case 5003:  // Misc_Timeout
    case 4001:  // Remote_Timeout
        return Put(out, capacity,
                   "The host's game did not answer. Is the host's game running (Host in Diamond Dogs Together), with Accept joins on, and is "
                   "your code current?");
    case 5008:  // Misc_P2P_Rendezvous
    case 5009:  // Misc_P2P_NAT_Firewall
        return Put(out, capacity, "Steam could not connect the two PCs. Both must be online in Steam; try again.");
    case 5005:  // Misc_SteamConnectivity
    case 3001:  // Local_OfflineMode
    case 3002:  // Local_ManyRelayConnectivity
        return Put(out, capacity, "This PC cannot reach Steam's network. Check the internet connection and that Steam is not in Offline Mode.");
    case 3004:  // Local_NetworkConfig
    case 3005:  // Local_Rights
        return Put(out, capacity, "Steam networking is not available here right now; restart Steam.");
    case 4002:  // Remote_BadCrypt
    case 4003:  // Remote_BadCert
        return Put(out, capacity, "Steam's secure handshake failed; restart Steam on both PCs.");
    case 4006:  // Remote_BadProtocolVersion
        return Put(out, capacity, "Your friend's Steam is a different version; update Steam on both PCs.");
    default:
        break;
    }
    if ((end >= 1000 && end <= 1999) || end == 5010)  // App_*, Misc_PeerSentNoConnection
        return Put(out, capacity, "The host closed the connection (left, restarted, or refused the join).");
    return PutF(out, capacity, "The Steam connection failed (code %d).", static_cast<int>(end));
}

const char* ShortReason(const char* reason) noexcept {
    const char* r = reason ? reason : "";
    if (!r[0]) return "";
    if (Is(r, "bad_code")) return "wrong code?";
    if (Is(r, "busy")) return "host busy";
    if (Is(r, "build_mismatch") || Is(r, "proto_mismatch")) return "different mod version";
    if (Is(r, "role_mismatch")) return "both chose the same role";
    if (Is(r, "mode_mismatch")) return "one side is in the lab mode";
    if (Starts(r, "secret_")) return "code file missing";
    if (Is(r, "steam_offline")) return "Steam offline";
    if (Is(r, "steam_peer_is_self")) return "that is your own code";
    if (Is(r, "no_peer_steam") || Is(r, "no_peer_addr")) return "no host given";
    if (Starts(r, "steam_")) return "Steam unavailable";
    if (Starts(r, "transport_")) return "network unavailable";
    return "refused";
}

const char* ShortSteamEnd(std::int32_t end) noexcept {
    switch (end) {
    case 0: return "";
    case 5003:
    case 4001: return "host not answering";
    case 5008:
    case 5009: return "Steam could not connect";
    case 5005:
    case 3001:
    case 3002: return "no Steam network";
    default: break;
    }
    if ((end >= 1000 && end <= 1999) || end == 5010) return "host closed";
    return "Steam failed";
}

bool FormatNoBody(const char* scene, char* out, std::size_t capacity) noexcept {
    const char* s = scene ? scene : "";
    const char* what = Is(s, "demo_stage") ? "Mother Base demo stage"
                     : Is(s, "hangar")     ? "battle-gear hangar"
                     : Is(s, "story")      ? "story variant"
                     : Is(s, "layout")     ? "unknown Mother Base layout"
                                           : "unexpected pack list";
    const bool reloads = Is(s, "demo_stage") || Is(s, "hangar") || Is(s, "story");
    return PutF(out, capacity, "No partner body in this scene (%s). %s", what,
                reloads ? "It returns when the scene reloads normally." : "Please send your logs (Tools > Collect logs).");
}

namespace {
// The fixed mission names (long, short); story and other missions are numbered.
struct MissionText {
    std::uint32_t mission;
    const char* long_name;
    const char* short_name;
};
const MissionText kMissions[] = {
    {0, "a loading screen", "loading"},
    {1, "the start screen", "start screen"},
    {30010, "Afghanistan free roam", "Afghanistan"},
    {30020, "Africa free roam", "Africa"},
    {30050, "Mother Base", "Mother Base"},
    {40010, "the Afghanistan helicopter", "Afghanistan heli"},
    {40020, "the Africa helicopter", "Africa heli"},
    {40050, "the Mother Base helicopter", "Mother Base heli"},
};
bool NameMission(std::uint32_t mission, bool long_name, char* out, std::size_t capacity) noexcept {
    for (const auto& m : kMissions)
        if (m.mission == mission) return Put(out, capacity, long_name ? m.long_name : m.short_name);
    const unsigned value = static_cast<unsigned>(mission);
    if (mission >= 10000 && mission <= 11999) return PutF(out, capacity, "story mission %u", value);
    return PutF(out, capacity, "mission %u", value);
}
// world_follow's SUPPORTED scenes (co-op travel follows there).
bool CoopScene(std::uint32_t mission) noexcept {
    return mission == 30010 || mission == 30020 || mission == 30050 || mission == 40010 || mission == 40020 || mission == 40050;
}
}  // namespace

bool FormatMission(std::uint32_t mission, char* out, std::size_t capacity) noexcept {
    return NameMission(mission, true, out, capacity);
}

bool FormatMissionShort(std::uint32_t mission, char* out, std::size_t capacity) noexcept {
    return NameMission(mission, false, out, capacity);
}

const char* PairWaitName(std::uint8_t code) noexcept {
    static const char* const kNames[] = {"paired",     "off",           "no_link",        "no_hello",          "remote_off", "remote_saves",
                                         "same_role",  "local_loading", "remote_loading", "different_mission", "epoch",      "settling"};
    return code < sizeof(kNames) / sizeof(kNames[0]) ? kNames[code] : "unknown";
}

bool FormatWorldNote(std::uint8_t code, std::uint32_t a, std::uint32_t b, std::uint32_t partner, std::uint32_t local, char* out,
                     std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    char first[32]{}, second[32]{};
    switch (code) {
    case 16:  // MISMATCH{a=local mission, b=partner mission}
        if (!FormatMission(b, first, sizeof(first)) || !FormatMission(a, second, sizeof(second))) return false;
        return PutF(out, capacity,
                    "Your partner is in %s; you are in %s. Co-op only pairs when you are both in the same Mother Base, free roam or "
                    "helicopter.",
                    first, second);
    case 1:  // PROGRESS{a=title mission, b=1 story start}
        if (b != 1) return false;
        return Put(out, capacity,
                   "Your co-op save is at the story start, so Continue goes to a story mission that co-op cannot join. Quit and import "
                   "your partner's save (Tools > Share save > Import).");
    case 2:  // HELI_WAIT{a=target}
        if (partner != 0 && partner != local) {
            if (!FormatMission(partner, first, sizeof(first))) return false;
            return PutF(out, capacity,
                        "Waiting up to 2 min for your partner (now in %s) to reach this helicopter. Choose the deployment again to fly "
                        "alone.",
                        first);
        }
        return Put(out, capacity,
                   "Waiting up to 2 min for your partner to reach this helicopter. Choose the deployment again to fly alone.");
    case 3:  // HELI_ALONE{a=target, b=reason}
        switch (b) {
        case 1: return Put(out, capacity, "No co-op partner can come on this flight: flying alone.");
        case 2:
            if (partner == 0)
                return Put(out, capacity,
                           "Flying alone: your partner is in another mission. Meet in the same Mother Base or free roam to play together.");
            if (!FormatMission(partner, first, sizeof(first))) return false;
            return PutF(out, capacity, "Flying alone: your partner is in %s. Meet in the same Mother Base or free roam to play together.",
                        first);
        case 3: return Put(out, capacity, "Flying alone, as you chose the deployment again.");
        case 4: return Put(out, capacity, "Your partner did not reach this helicopter within 2 min: flying alone.");
        case 5:  // join_partner (online 2026-10-22 P7): the target is the partner's own mission
            if (!FormatMission(a, first, sizeof(first))) return false;
            if (CoopScene(a)) return PutF(out, capacity, "Flying to your partner in %s.", first);
            return PutF(out, capacity, "Joining your partner in %s; co-op travel does not follow in story missions.", first);
        default: return Put(out, capacity, "Flying alone.");
        }
    case 4:  // HELI_REFUSED{a=target}
        return Put(out, capacity,
                   "Your partner cannot follow you there: co-op travel only goes to Mother Base and free roam. Choose the same "
                   "destination again within 30 s to go alone.");
    case 5:  // TRAVEL_LOCKED{a=host mission, b=target}
        if (!FormatMission(a, first, sizeof(first))) return false;
        return PutF(out, capacity,
                    "The host is in %s. Go there or to a helicopter, or do the same again within 30 s to travel alone.", first);
    case 6:  // HOST_PARTNER_CANNOT_JOIN{a=target, b=partner mission}
        if (!FormatMission(a, first, sizeof(first))) return false;
        return PutF(out, capacity,
                    "Your partner cannot join %s (no co-op there). Meet in Mother Base, free roam or the helicopter.", first);
    case 7:  // TITLE_CONTINUE_LOCKED{a=host mission, b=1 location_differs | 2 story_start | 3 waiting_for_host_save |
             //                       4 host_save_checked (the host is in game in 30010/30020/30050)}
        // Online 2026-10-22 P2: the title repeat needs 3 s (a double press is
        // no escape), and a pending handoff has no Continue escape for 60 s:
        // F9 Keep my own save is the way out.
        switch (b) {
        case 1:
            if (!FormatMission(a, first, sizeof(first))) return false;
            return PutF(out, capacity,
                        "The host is in %s, but your save continues elsewhere. Press Continue again 3 to 10 s later to play alone.", first);
        case 2:
            return Put(out, capacity,
                       "Your save is at the story start; the host cannot join it. Press Continue again 3 to 10 s later to play it anyway.");
        case 3:
            return Put(out, capacity,
                       "The host's save is copied to you once they are in Mother Base or free roam. Stay here; F9 > Partner / Co-op > "
                       "Shared save > Keep my own save plays yours.");
        case 4:
            if (!FormatMission(a, first, sizeof(first))) return false;
            return PutF(out, capacity,
                        // (No duration: the host's settle window depends on its build.)
                        "The host is in %s; their save is checked there and then copied to you. Stay here; F9 > Partner / "
                        "Co-op > Shared save > Keep my own save plays yours.",
                        first);
        default: return false;
        }
    case 8:  // TRAVEL_ALONE{a=target, b=1 repeat | 2 f9_unlock | 3 timeout}
        return Put(out, capacity, "Travelling alone. The host leads co-op travel again once you are in the same place.");
    case 9:  // CONTINUE_DURING_HANDOFF{a=pct 0..100}
        if (a > 100) return false;
        return PutF(out, capacity,
                    "Getting the host's save (%u%%); MGSV restarts on it. Stay here; F9 > Partner / Co-op > Shared save > Keep my own save "
                    "plays yours.",
                    static_cast<unsigned>(a));
    case 10:  // HANDOFF_KEPT_OWN
        return Put(out, capacity, "Keeping your own save for this session.");
    case 11:  // GUEST_LANDING_ELSEWHERE{a=target, b=host mission}
        if (!FormatMission(a, first, sizeof(first)) || !FormatMission(b, second, sizeof(second))) return false;
        return PutF(out, capacity, "You are loading %s; the host is in %s. Meet in a helicopter, Mother Base or free roam.", first,
                    second);
    case 12:  // SORTIE_HELD{a=target, b=host mission}
        if (!FormatMission(b, first, sizeof(first))) return false;
        return PutF(out, capacity,
                    "The host is in %s. Choose that destination, or choose this one again to fly alone (it leaves alone after 2 min).",
                    first);
    default:  // 13..15 reserved or unknown: no toast
        return false;
    }
}

// ---- Automatic save handoff (docs/save-handoff.md; join-fix phase 2 P2-15).
// The numbers are save_handoff_state::Toast / Token and save_handoff_wire::Fail /
// Elig; they are spelled out here so coop_text stays a leaf of the core.
const char* SaveHandoffFailText(std::uint8_t fail) noexcept {
    switch (fail) {
    case 1: return "the copy did not match the host's files";  // hash_mismatch
    case 2: return "the files could not be written";           // disk
    case 3: return "not enough disk space";                    // disk_space
    case 4: return "the transfer stalled";                     // stall
    case 5: return "the connection dropped";                   // link_lost
    case 6: return "an unexpected message";                    // protocol
    case 7: return "the save changed while it was read";       // snapshot_changed
    case 8: return "the title screen was left";                // not_title
    case 9: return "no answer in time";                        // timeout
    case 10: return "the game is closing";                     // shutdown
    case 11: return "the host's file list was not valid";      // bad_offer
    case 12: return "your partner could not store it";         // partner
    default: return "unknown error";                           // none or a newer code
    }
}

const char* SaveHandoffEligText(std::uint8_t elig) noexcept {
    switch (elig) {
    case 1: return "it is ready";                                                            // eligible
    case 2: return "it is still in the story opening";                                       // story_opening
    case 3: return "its story progress is not known yet";                                    // story_unknown
    case 4: return "it is not yet known to continue into Mother Base or free roam";          // continue_unknown
    case 5: return "it continues into a mission co-op cannot join";                          // continue_unsupported
    case 6: return "the game is saving";                                                     // saving
    case 7: return "the save folder holds extra save files";                                 // unexpected_files
    case 8: return "the save files are too large";                                           // too_large
    case 9: return "save sharing is off";                                                    // off
    default: return "it is not checked yet";                                                 // unknown
    }
}

namespace {
// The partner's name for a sentence: at most 24 printable ASCII characters
// ('?' for anything else); "" when absent.
void PartnerName(const char* partner, char (&out)[25]) noexcept {
    std::size_t i = 0;
    for (; partner && partner[i] && i + 1 < sizeof(out); ++i) {
        const auto c = static_cast<unsigned char>(partner[i]);
        out[i] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    out[i] = 0;
}
// FormatMission, or `fallback` for an unknown (0) mission.
bool MissionOr(std::uint32_t mission, const char* fallback, char* out, std::size_t capacity) noexcept {
    return mission ? FormatMission(mission, out, capacity) : Put(out, capacity, fallback);
}
}  // namespace

bool FormatSaveHandoff(std::uint8_t code, std::uint32_t a, std::uint32_t b, const char* partner, char* out,
                       std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    char name[25]{};
    PartnerName(partner, name);
    const char* who = name[0] ? name : "your partner";
    const char* Who = name[0] ? name : "Your partner";
    const auto value = static_cast<unsigned>(a);
    char mission[32]{};
    switch (code) {
    case 1:  // G1 receiving {a=kb}
        return PutF(out, capacity,
                    "Getting the host's save (%u KB) so you both start in the same place. Your own co-op save is kept (Tools > Share save "
                    "> Restore previous brings it back).",
                    value);
    case 2:  // G2 countdown {a=seconds left}
        // Online 2026-10-22: no "press Continue twice" (a natural double press,
        // P2) and a hint for a Steam relaunch that does not come (P5).
        return PutF(out, capacity,
                    "Host's save ready: MGSV restarts in %u s and the launcher starts it again (not back within a minute? Press Play in "
                    "Steam). Keep yours: F9 > Partner / Co-op > Shared save > Keep my own save.",
                    value);
    case 3:  // G3 stage only
        return Put(out, capacity, "Host's save stored for a test; it is not applied.");
    case 4:  // G4 wait for the host
        return Put(out, capacity,
                   "Your save starts somewhere other than the host's. Once the host is in Mother Base or free roam, their save is copied "
                   "to you at the title screen and MGSV restarts once.");
    case 5:  // G5 the host's save is in the story opening
        return Put(out, capacity,
                   "The host's save is still in the story opening, so it is not shared. The player whose save reaches Mother Base should "
                   "host.");
    case 6:  // G6 {a=the host's Continue mission}
        if (!MissionOr(a, "a mission", mission, sizeof(mission))) return false;
        return PutF(out, capacity,
                    "The host's save continues into %s, which co-op cannot join. The host should go to Mother Base or free roam first.",
                    mission);
    case 7:  // G7 {a=Fail}
        return PutF(out, capacity, "Getting the host's save failed (%s). Nothing was changed.",
                    SaveHandoffFailText(static_cast<std::uint8_t>(a > 255 ? 0 : a)));
    case 8:  // G8 kept
        return Put(out, capacity, "You kept your own save for this session.");
    case 9:  // G9 quit manually
        return Put(out, capacity,
                   "Host's save ready: quit MGSV (title menu > Exit) and Diamond Dogs Together starts it again on the host's save.");
    case 10:  // G10 applied
        return Put(out, capacity,
                   "You now play on the host's save. Tools > Share save > Restore previous brings your previous co-op save back.");
    case 11:  // G11 {a=the host's mission, b=the settle seconds} (online 2026-10-22 P3)
        if (!MissionOr(a, "Mother Base or free roam", mission, sizeof(mission))) return false;
        if (!b || b > 600)
            return PutF(out, capacity, "The host is in %s: their save is checked there, then copied to you at the title screen and MGSV "
                                       "restarts once.",
                        mission);
        return PutF(out, capacity,
                    "The host is in %s: their save is checked there (about %u s), then copied to you at the title screen and MGSV "
                    "restarts once.",
                    mission, static_cast<unsigned>(b));
    case 12:  // G12 the host's save is ready, the guest is in game (P6)
        return Put(out, capacity,
                   "The host's save is ready for you: go back to the title screen (or quit MGSV and join again in Diamond Dogs Together) "
                   "to get it. MGSV then restarts once on it.");
    case 21:  // H1 sending
        return PutF(out, capacity, "Sending your save to %s so you both start in the same place.", who);
    case 22:  // H2 {a=the mission the guest arrives in}
        if (!a)
            return PutF(out, capacity, "%s is restarting MGSV on your save (about a minute). Wait here for them.", Who);
        if (!FormatMission(a, mission, sizeof(mission))) return false;
        return PutF(out, capacity, "%s is restarting MGSV on your save (about a minute). Wait here; they arrive in %s.", Who, mission);
    case 23:  // H3 the partner's save starts elsewhere
        return PutF(out, capacity,
                    "%s's save starts somewhere else. Go to Mother Base or free roam: your save is then copied to them.", Who);
    case 24:  // H4 own save in the story opening
        return PutF(out, capacity,
                    "Your save is still in the story opening, so it is not shared. Let %s host if their save reaches Mother Base.", who);
    case 25:  // H5 the partner kept their save
        return PutF(out, capacity, "%s kept their own save.", Who);
    case 26:  // H6 {a=Fail}
        return PutF(out, capacity, "Sending your save failed (%s). Use Tools > Share save instead.",
                    SaveHandoffFailText(static_cast<std::uint8_t>(a > 255 ? 0 : a)));
    case 27:  // H7 {a=the unsupported mission}
        if (!MissionOr(a, "a mission", mission, sizeof(mission))) return false;
        return PutF(out, capacity, "Your save continues into %s, which co-op cannot join. Go to Mother Base or free roam first.",
                    mission);
    case 28:  // H8 {a=the settle seconds} (online 2026-10-22 P3)
        if (!a || a > 600) return PutF(out, capacity, "Checking your save here (stay in this mission), then it is copied to %s.", who);
        return PutF(out, capacity, "Checking your save here (about %u s; stay in this mission), then it is copied to %s.", value, who);
    default:
        return false;
    }
}

bool FormatSaveHandoffStatus(std::uint8_t role, std::uint8_t token, std::uint8_t pct, std::uint8_t why, std::uint32_t bytes,
                             std::uint32_t seconds_left, char* out, std::size_t capacity) noexcept {
    if (!out || !capacity) return false;
    out[0] = 0;
    const bool host = role == 1;
    const unsigned percent = pct > 100 ? 100u : pct;
    const auto kb = static_cast<unsigned>((static_cast<std::uint64_t>(bytes) + 1023u) / 1024u);
    switch (token) {
    case 1:  // idle (online 2026-10-22 P1: not the "matches" sentence, nothing is decided yet)
        return Put(out, capacity,
                   host ? "Comparing your save with your partner's; nothing to share yet." : "Comparing your save with the host's; nothing to share yet.");
    case 2:  // same
        return Put(out, capacity,
                   host ? "Your save matches your partner's place; nothing to share." : "Your save matches the host's place; nothing to share.");
    case 3:  // wait_host {why=save_handoff_state::WaitWhy}
        if (why == 1)  // title (P6)
            return Put(out, capacity,
                       "The host's save is ready: go back to the title screen (or quit MGSV and join again in Diamond Dogs Together) to get "
                       "it.");
        if (why == 2)  // settling (P3)
            return Put(out, capacity, "The host's save is checked in their mission, then copied to you at the title screen.");
        return Put(out, capacity, "Waiting for the host to reach Mother Base or free roam; their save is then copied to you.");
    case 4:  // offered
        return Put(out, capacity, "Offering your save to your partner.");
    case 5:  // sending
        return PutF(out, capacity, "Sending your save: %u%%", percent);
    case 6:  // partner_staged
        return Put(out, capacity, "Your partner received your save.");
    case 7:  // partner_restarting
        return Put(out, capacity, "Your partner is restarting on your save.");
    case 8:  // receiving
        return PutF(out, capacity, "Receiving the host's save: %u%% of %u KB.", percent, kb);
    case 9:  // staged
        return PutF(out, capacity, "Host's save received and checked (%u KB).", kb);
    case 10:  // countdown
        if (!seconds_left)
            return Put(out, capacity, "Host's save ready: MGSV restarts now. Your current co-op save is kept aside (Tools > Share save > Restore previous).");
        return PutF(out, capacity,
                    "Host's save ready: MGSV restarts in %u s. Your current co-op save is kept aside (Tools > Share save > Restore "
                    "previous).",
                    static_cast<unsigned>(seconds_left));
    case 11:  // exiting (P5: Steam may not act on the relaunch)
        return Put(out, capacity,
                   "MGSV is closing to load the host's save; Diamond Dogs Together starts it again (not back within a minute? Press Play "
                   "in Steam).");
    case 12:  // kept
        return Put(out, capacity, host ? "Your partner kept their own save." : "You kept your own save for this session.");
    case 13:  // refused
        // continue_unknown (P3): not final; a Continue or a short stay in a co-op scene binds it.
        if (host && why == 4)
            return Put(out, capacity,
                       "Your save is shared once it is known to continue into Mother Base or free roam: Continue there, or stay there a "
                       "few seconds.");
        return PutF(out, capacity, host ? "Your save cannot be shared: %s." : "The host's save cannot be shared: %s.",
                    SaveHandoffEligText(why));
    case 14:  // failed
        if (host) return PutF(out, capacity, "Sending your save failed: %s.", SaveHandoffFailText(why));
        return PutF(out, capacity, "Getting the host's save failed: %s. Nothing was changed.", SaveHandoffFailText(why));
    case 15:  // applied
        return Put(out, capacity,
                   "You now play on the host's save. Tools > Share save > Restore previous brings your previous co-op save back.");
    default:  // off (0) or an unknown token
        return Put(out, capacity, "Automatic save sharing is off for this session.");
    }
}

}  // namespace mgscoop::coop_text
