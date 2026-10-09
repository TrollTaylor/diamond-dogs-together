// Original MGS Co-op plain-language link reasons (shared with CoopPlay.psm1).
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
        return PutF(out, capacity, "Your friend runs mgs-coop %s; you run %s. Both must install the same package.", remote, own);
    if (Is(r, "proto_mismatch"))
        return PutF(out, capacity, "Your friend's mgs-coop (%s) speaks another link version. Both must install the same package.", remote);
    if (Is(r, "mode_mismatch"))
        return Put(out, capacity, "One side is in the single-PC lab mode. Both players must start the game with Play-Coop.bat.");
    if (Is(r, "role_mismatch")) return Put(out, capacity, "Both players chose the same role: one must Host, the other Join.");
    if (Is(r, "busy")) return Put(out, capacity, "The host is already connected to someone.");
    if (Is(r, "bad_code"))
        return Put(out, capacity,
                   "The host did not accept the connection. Possibly a wrong code: copy it again (or the host restarted Play-Coop: ask for the "
                   "new code).");
    if (Starts(r, "secret_")) return Put(out, capacity, "The link secret file is missing or damaged. Quit MGSV and start Play-Coop again.");
    if (Is(r, "crypto")) return Put(out, capacity, "Windows could not provide random numbers for the link. Restart the PC and try again.");
    if (Is(r, "no_peer_addr")) return Put(out, capacity, "The host address is missing. Quit MGSV and run Join again.");
    if (Is(r, "no_peer_steam"))
        return Put(out, capacity,
                   "The host's Steam account is missing. Quit MGSV and run Join online again (paste the code or wait for an invite).");
    if (Is(r, "steam_offline"))
        return Put(out, capacity, "Steam is offline or not logged in. Quit MGSV, start Steam and log in (online), then start Play-Coop again.");
    if (Is(r, "steam_self_mismatch"))
        return Put(out, capacity, "Steam is logged in to another account than when Play-Coop started. Quit MGSV and start Play-Coop again.");
    if (Is(r, "steam_peer_is_self")) return Put(out, capacity, "That join code is your own. Ask your friend for theirs.");
    if (Is(r, "steam_api"))
        return Put(out, capacity, "steam_api64.dll is not the expected file. Verify the game files in Steam, then run Install-Coop.bat again.");
    if (Is(r, "steam_not_initialized"))
        return Put(out, capacity, "Steam did not start for MGSV. Quit the game and start Play-Coop again (it starts MGSV through Steam).");
    for (const char* changed : kSteamChanged)
        if (Is(r, changed))
            return Put(out, capacity,
                       "Steam networking changed (probably a Steam update). Use a LAN/VPN code ([L] in Play-Coop) and run Collect-Logs.bat for "
                       "the developer.");
    if (Is(r, "steam_owner_unavailable"))
        return Put(out, capacity, "The co-op loader could not attach to Steam. Use a LAN/VPN code ([L] in Play-Coop) and run Collect-Logs.bat.");
    if (Is(r, "steam_fault"))
        return Put(out, capacity,
                   "Steam networking failed inside Steam: restart the game. If it happens again, use a LAN/VPN code ([L]) and run "
                   "Collect-Logs.bat.");
    if (Is(r, "steam_acquire_timeout"))
        return Put(out, capacity, "Steam networking did not start within two minutes. Check that Steam is online, then restart the game.");
    if (Starts(r, "steam_"))
        return PutF(out, capacity,
                    "The Steam link could not start (%s). Restart the game; if it repeats, use a LAN/VPN code ([L]) and run Collect-Logs.bat.", r);
    if (Is(r, "transport_bind"))
        return Put(out, capacity,
                   "The UDP port is already in use by another program on this PC (or a second MGSV). Close it, or use another port.");
    if (Starts(r, "transport_"))
        return PutF(out, capacity, "The network link could not start (%s). Restart the PC and try again; if it repeats, run Collect-Logs.bat.", r);
    if (!verified) return PutF(out, capacity, "The connection was refused (%s). Possibly a wrong code.", r);
    return PutF(out, capacity, "The connection was refused (%s).", r);
}

bool FormatSteamEnd(std::int32_t end, char* out, std::size_t capacity) noexcept {
    if (!end) return Put(out, capacity, "");
    switch (end) {
    case 5003:  // Misc_Timeout
    case 4001:  // Remote_Timeout
        return Put(out, capacity,
                   "The host's game did not answer. Is it running from Play-Coop (Host online), with Accept joins on, and is your code current?");
    case 5008:  // Misc_P2P_Rendezvous
    case 5009:  // Misc_P2P_NAT_Firewall
        return Put(out, capacity, "Steam could not connect the two PCs. Both must be online in Steam; try again, or use a LAN/VPN code [L].");
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
                reloads ? "It returns when the scene reloads normally." : "Please send Collect-Logs.");
}

}  // namespace mgscoop::coop_text
