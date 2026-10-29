// Diamond Dogs Together launcher core: session text (see core_text.hpp).
#include "core_text.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core_util.hpp"
#include "mgscoop/coop_reason_text.hpp"

namespace ddt::core {
namespace {

bool InAlphabet(char c) { return c != 0 && std::strchr(kAlphabet, c) != nullptr; }

bool OneOf(const std::string& v, std::initializer_list<const char*> list) {
    for (const char* s : list)
        if (v == s) return true;
    return false;
}

bool IsWordChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
bool IsAlnumAscii(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || IsDigit(c); }

std::string Comparable(const std::string& path) {
    std::string p = Trim(path);
    for (char& c : p)
        if (c == '/') c = '\\';
    if (StartsWith(p, "\\\\?\\")) p = p.substr(4);
    while (!p.empty() && p.back() == '\\') p.pop_back();
    return UpperAscii(p);
}

// "... ; key=<value>" where the value runs to the next blank or ';'.
std::string Field(const std::string& line, const std::string& key) {
    const std::size_t at = line.find(key);
    if (at == std::string::npos) return {};
    std::size_t end = at + key.size();
    while (end < line.size() && !IsBlank(line[end]) && line[end] != ';') ++end;
    return line.substr(at + key.size(), end - at - key.size());
}

// Starts with `prefix` and the next character is end, blank or ';'.
bool LineToken(const std::string& line, const std::string& prefix) {
    if (!StartsWith(line, prefix)) return false;
    if (line.size() == prefix.size()) return true;
    const char c = line[prefix.size()];
    return c == ' ' || c == ';' || c == '\r' || c == '\n';
}

std::string Lower(const std::string& s) { return LowerAscii(s); }

}  // namespace

// ---- Secrets, Steam IDs -------------------------------------------------------
bool IsCanonicalSecret(const std::string& secret) {
    if (secret.size() != 20) return false;
    for (char c : secret)
        if (!InAlphabet(c)) return false;
    return true;
}

std::string NewSecret() {
    std::uint8_t bytes[20]{};
    if (!RandomBytes(bytes, sizeof(bytes))) return {};
    std::string out;
    for (std::uint8_t b : bytes) out.push_back(kAlphabet[b & 31]);
    return out;
}

bool IsIndividualSteamId64(const std::string& text) {
    if (text.size() != 17 || text[0] < '1' || text[0] > '9') return false;
    std::uint64_t id = 0;
    for (char c : text) {
        if (!IsDigit(c)) return false;
        id = id * 10 + static_cast<std::uint64_t>(c - '0');
    }
    return (id >> 32) == 0x01100001ull && (id & 0xFFFFFFFFull) != 0;
}

std::string SteamId64FromAccount(std::uint32_t account) {
    if (account == 0) return {};
    return Decimal(kSteamIdBase + account);
}

std::string SecretFileStatus(const std::string* bytes) {
    if (!bytes) return "missing";
    const std::string& b = *bytes;
    if (!((b.size() == 22 && b[20] == '\r' && b[21] == '\n') || (b.size() == 21 && b[20] == '\n'))) return "bad_length";
    for (std::size_t i = 0; i < 20; ++i)
        if (!InAlphabet(b[i])) return "bad_char";
    return "ok";
}

// ---- lab.ini / coop.ini -------------------------------------------------------
const std::vector<std::pair<const char*, const char*>>& PartnerDefaults() {
    static const std::vector<std::pair<const char*, const char*>> kDefaults = {
        {"partner", "1"},           {"partner_stage", "3"},        {"partner_source", "net"},   {"partner_apply", "xform"},
        {"partner_anim", "off"},    {"partner_aset", "qs"},        {"partner_reset", "n"},      {"partner_look", "net"},
        {"partner_rate_hz", "30"},  {"partner_interp_ms", "100"},  {"partner_offset_cm", "300"}, {"partner_teardown", "clear"},
        {"partner_force_exists", "0"}, {"partner_policy_relax", "0"}, {"partner_anim_probe", "0"}, {"partner_dm", "map"},
        {"crash_probe", "1"}};
    return kDefaults;
}

bool MakeLabIniText(const LabIniParams& p, std::string& text, std::string& error) {
    text.clear();
    const std::string& s = p.save_dir;
    const bool absolute = (s.size() >= 3 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':' &&
                           s[2] == '\\') ||
                          StartsWith(s, "\\\\");
    if (!absolute) {
        error = "save_dir must be an absolute path";
        return false;
    }
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
            error = "Invalid save_dir";
            return false;
        }
    }
    if (s != Trim(s) || s.size() > 259) {
        error = "Invalid save_dir";
        return false;
    }
    if (p.role != "host" && p.role != "guest") {
        error = "Invalid role";
        return false;
    }
    if (!IsIndividualSteamId64(p.steam_self)) {
        error = "Invalid own SteamID64";
        return false;
    }
    if (p.role == "host" && (!p.steam_peer.empty() || p.steam_wait)) {
        error = "The host has no Steam peer.";
        return false;
    }
    if (p.role == "guest") {
        if (p.steam_wait && !p.steam_peer.empty()) {
            error = "A waiting guest has no host yet.";
            return false;
        }
        if (!p.steam_wait && !IsIndividualSteamId64(p.steam_peer)) {
            error = "Invalid host SteamID64";
            return false;
        }
        if (!p.steam_peer.empty() && p.steam_peer == p.steam_self) {
            error = "That is your own code.";
            return false;
        }
    }
    const std::string dispatch = Lower(p.steam_dispatch);
    if (!OneOf(dispatch, {"", "both", "fnptr", "callback", "poll"})) {
        error = "Invalid SteamDispatch";
        return false;
    }
    if (p.partner != "native" && p.partner != "clips") {
        error = "Invalid Partner";
        return false;
    }
    const std::string handoff = Lower(p.save_handoff);
    if (!OneOf(handoff, {"", "off", "stage", "scene", "always"})) {
        error = "Invalid SaveHandoff";
        return false;
    }
    const std::string lock = Lower(p.travel_lock);
    if (!OneOf(lock, {"", "on", "off"})) {
        error = "Invalid TravelLock";
        return false;
    }
    if (p.world < 0 || p.world > 3) {
        error = "Invalid World";
        return false;
    }
    std::string t;
    t += kHeader;
    t += "\nenabled=1\nmode=play\nrole=" + p.role + "\nsave_dir=" + s + "\nnet=1\nnet_transport=steam\nnet_steam_self=" + p.steam_self + "\n";
    if (p.role == "guest") t += p.steam_wait ? std::string("net_steam_wait=1\n") : "net_peer_steam=" + p.steam_peer + "\n";
    if (!dispatch.empty()) t += "net_steam_dispatch=" + dispatch + "\n";
    t += "partner_interp=auto\n";
    for (const auto& kv : PartnerDefaults()) t += std::string(kv.first) + "=" + kv.second + "\n";
    t += "partner_input_send=1\n";
    if (p.partner == "native") t += "partner_native_profile=gun\n";
    if (!handoff.empty()) t += "save_handoff=" + handoff + "\n";
    if (lock == "off") t += "travel_lock=0\n";
    if (p.world > 0) t += "world=" + std::to_string(p.world) + "\n";
    text = std::move(t);
    return true;
}

json::Value LabRecord(const LabIniParams& p) {
    json::Value lab = json::Value::Object();
    lab["role"] = p.role;
    lab["save_dir"] = p.save_dir;
    lab["transport"] = "steam";
    lab["port"] = 47830;
    lab["peer"] = "";
    lab["world"] = p.world;
    lab["partner"] = p.partner;
    lab["steam_self"] = p.steam_self;
    lab["steam_peer"] = p.steam_peer;
    lab["steam_wait"] = p.steam_wait;
    lab["steam_dispatch"] = p.steam_dispatch;
    lab["save_handoff"] = Lower(p.save_handoff);
    lab["travel_lock"] = Lower(p.travel_lock);
    return lab;
}

bool LabParamsFromRecord(const json::Value& lab, LabIniParams& p, std::string& error) {
    if (!lab.is_object()) {
        error = "the session does not record its lab.ini settings";
        return false;
    }
    p = LabIniParams{};
    p.role = lab.Str("role");
    p.save_dir = lab.Str("save_dir");
    if ((p.role != "host" && p.role != "guest") || p.save_dir.empty()) {
        error = "the session does not record its lab.ini settings";
        return false;
    }
    if (lab.Str("transport", "udp") != "steam") {
        error = "the session used the LAN link, which this version does not support";
        return false;
    }
    p.world = static_cast<int>(lab.Int("world", 3));
    p.partner = lab.Str("partner", "native");
    p.save_handoff = lab.Str("save_handoff");
    p.travel_lock = lab.Str("travel_lock");
    p.steam_self = lab.Str("steam_self");
    p.steam_peer = lab.Str("steam_peer");
    p.steam_wait = lab.Bool("steam_wait");
    p.steam_dispatch = lab.Str("steam_dispatch");
    return true;
}

bool MakeCoopIniText(const std::string& character, const std::string& fallback, const std::string& hud, std::string& text,
                     std::string& error) {
    if (!OneOf(character, {"default", "quiet", "dd_male", "dd_female", "snake", "avatar", "ocelot", "story"})) {
        error = "Invalid character: " + character;
        return false;
    }
    if (!OneOf(fallback, {"dd_male", "dd_female", "snake", "avatar"})) {
        error = "Invalid character_fallback: " + fallback;
        return false;
    }
    const std::string h = Lower(hud);
    if (!OneOf(h, {"", "compact", "full", "off"})) {
        error = "Invalid Hud";
        return false;
    }
    text = std::string(kHeader) + "\nmaster_url=" + kMasterUrl + "\ncharacter=" + character + "\ncharacter_fallback=" + fallback + "\n";
    if (!h.empty()) text += "hud=" + h + "\n";
    return true;
}

bool IsGeneratedIniText(const std::string& text) {
    std::string t = text;
    if (t.size() >= 3 && static_cast<unsigned char>(t[0]) == 0xEF && static_cast<unsigned char>(t[1]) == 0xBB &&
        static_cast<unsigned char>(t[2]) == 0xBF)
        t = t.substr(3);
    if (StartsWith(t, kOldPlayHeader)) return true;
    if (!StartsWith(t, kHeader)) return false;
    const std::size_t n = std::strlen(kHeader);
    return t.size() == n || t[n] == '\n' || t[n] == '\r';
}

std::vector<std::pair<std::string, std::string>> ParseIni(const std::string& text) {
    std::vector<std::pair<std::string, std::string>> out;
    for (const std::string& raw : SplitLines(text)) {
        const std::string line = Trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos || eq < 1) continue;
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        bool replaced = false;
        for (auto& kv : out)
            if (kv.first == key) {
                kv.second = value;
                replaced = true;
            }
        if (!replaced) out.emplace_back(key, value);
    }
    return out;
}

// ---- Status text -------------------------------------------------------------
std::string LinkReasonText(const std::string& reason, const std::string& remote_build, const std::string& own_build,
                           bool verified, Wording wording) {
    if (reason.empty()) return {};
    if (wording == Wording::launcher) {
        const std::string remote = remote_build.empty() ? "another version" : remote_build;
        if (reason == "build_mismatch")
            return "Your partner runs Diamond Dogs Together " + remote + "; you run " + own_build + ". Both must use the same version.";
        if (reason == "proto_mismatch")
            return "Your partner's Diamond Dogs Together (" + remote + ") speaks another link version. Both must use the same version.";
        if (reason == "mode_mismatch")
            return "One side is in the single-PC lab mode. Both players must start the game with Diamond Dogs Together.";
        if (reason == "bad_code")
            return "The host did not accept the connection. Possibly a wrong code: copy it again (or the host started a new game: "
                   "ask for the new code).";
        if (StartsWith(reason, "secret_"))
            return "The link secret file is missing or damaged. Quit MGSV and host or join again in Diamond Dogs Together.";
        if (reason == "no_peer_addr") return "The host address is missing. Quit MGSV and join again.";
        if (reason == "no_peer_steam")
            return "The host's Steam account is missing. Quit MGSV and join again (paste the code or wait for an invite).";
        if (reason == "steam_offline")
            return "Steam is offline or not logged in. Quit MGSV, start Steam and log in (online), then host or join again.";
        if (reason == "steam_self_mismatch")
            return "Steam is logged in to another account than when you pressed Host or Join. Quit MGSV and try again.";
        if (reason == "steam_api")
            return "steam_api64.dll is not the expected file. Verify the game files in Steam, then start Diamond Dogs Together again.";
        if (reason == "steam_not_initialized")
            return "Steam did not start for MGSV. Quit the game and host or join again (Diamond Dogs Together starts MGSV through Steam).";
        if (OneOf(reason, {"steam_client_module", "steam_client_iface", "steam_iface_missing", "steam_iface_module", "steam_iface_rtti",
                           "steam_iface_shape"}))
            return "Steam networking changed (probably a Steam update). Use Tools > Collect logs and send the file to the developer.";
        if (reason == "steam_owner_unavailable")
            return "The co-op loader could not attach to Steam. Use Tools > Collect logs and send the file to the developer.";
        if (reason == "steam_fault")
            return "Steam networking failed inside Steam: restart the game. If it happens again, use Tools > Collect logs.";
        if (reason == "steam_acquire_timeout")
            return "Steam networking did not start within two minutes. Check that Steam is online, then restart the game.";
        if (StartsWith(reason, "steam_"))
            return "The Steam link could not start (" + reason + "). Restart the game; if it repeats, use Tools > Collect logs.";
        if (StartsWith(reason, "transport_") && reason != "transport_bind")
            return "The network link could not start (" + reason + "). Restart the PC and try again; if it repeats, use Tools > Collect logs.";
    }
    char buf[512]{};
    if (!mgscoop::coop_text::FormatLinkReason(reason.c_str(), remote_build.c_str(), own_build.c_str(), verified, buf, sizeof(buf)))
        return "The connection was refused (" + reason + ").";
    return buf;
}

std::string SteamEndText(std::int64_t end, Wording wording) {
    if (end == 0) return {};
    if (wording == Wording::launcher) {
        if (end == 5003 || end == 4001)
            return "The host's game did not answer. Is the host's game running (Host in Diamond Dogs Together), with Accept joins on, "
                   "and is your code current?";
        if (end == 5008 || end == 5009) return "Steam could not connect the two PCs. Both must be online in Steam; try again.";
    }
    char buf[256]{};
    if (!mgscoop::coop_text::FormatSteamEnd(static_cast<std::int32_t>(end), buf, sizeof(buf)))
        return "The Steam connection failed (code " + std::to_string(end) + ").";
    return buf;
}

std::string PartnerReasonText(const std::string& r) {
    if (r.empty()) return "no reason reported";
    if (r == "legacy") return "clip partner chosen (Play-Coop -Partner clips)";
    if (r == "no_body_pack")
        return "no partner body in this scene (a Mother Base demo, the battle-gear hangar or a story variant; it returns when the "
               "scene reloads normally, otherwise send Collect-Logs)";
    if (OneOf(r, {"mission_start", "configured", "unlatched", "cooldown", "settling", "ok", "not_installed", "warmup"}))
        return "starting the native partner";
    if (r == "thread") return "this PC runs the game's update threads differently than the native partner needs; please send Collect-Logs";
    if (r == "cost") return "the native partner was too slow on this PC; please send Collect-Logs";
    if (StartsWith(r, "install_"))
        return "the game code differs from the verified build, so the native partner stays off; please send Collect-Logs";
    if (r == "hook_lost") return "another program replaced a native hook (an overlay or mod?)";
    if (r == "faulted" || r == "dm_revert_failed") return "the native partner hit an error; clips for this mission";
    if (OneOf(r, {"net_input", "input_stale", "no_input"})) return "the other player's input stalled (network)";
    if (r == "net_link" || r == "link") return "the connection dropped";
    if (r == "net_quiet" || r == "remote_quiet") return "the other player is paused or loading";
    if (r == "net_quality") return "waiting for a steadier connection";
    if (r == "net_flap") return "the connection keeps dropping; clips until it is steady for 30 s";
    if (r == "remote_caps") return "the other game sends no input frames (different mgs-coop version?)";
    if (OneOf(r, {"range_hidden", "unrealized", "parts", "stage"})) return "the partner is far away or still loading";
    if (r == "terminating") return "the mission is ending";
    if (r == "diverging" || r == "runaway") return "the partner body drifted; back to native shortly";
    if (OneOf(r, {"health", "unwindowed", "lost", "wrap_in_batch", "drain_block", "enter_timeout_dm", "enter_timeout_place",
                  "defer_off", "defer_census"}))
        return "the native partner left (" + r + ")";
    return r;
}

std::string MissionName(std::uint64_t mission) {
    if (mission > 0xFFFFFFFFull) return "mission " + Decimal(mission);
    char buf[64]{};
    if (mgscoop::coop_text::FormatMission(static_cast<std::uint32_t>(mission), buf, sizeof(buf))) return buf;
    return "mission " + Decimal(mission);
}

HandoffText GetHandoffText(const std::string& token, int pct, bool guest, Wording wording) {
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const bool ps = wording == Wording::powershell;
    const std::string restore = ps ? "Share-Save.bat [R]" : "Tools > Share save > Restore previous";
    HandoffText t;
    const std::string p = std::to_string(pct);
    if (token == "wait_host") {
        t.segment = " | waiting for the host's save";
        t.line = "Your save starts somewhere other than the host's. Once the host is in Mother Base or free roam, their save is copied "
                 "to you at the title screen and MGSV restarts once.";
    } else if (token == "receiving") {
        t.segment = " | getting the host's save " + p + "%";
        t.line = "Getting the host's save so you both start in the same place. Your own co-op save is kept (" + restore +
                 " brings it back).";
    } else if (token == "staged" || token == "countdown") {
        t.segment = " | host save ready, MGSV restarts";
        t.line = std::string("Host's save ready: MGSV restarts at the title screen and ") + (ps ? "Play-Coop" : "Diamond Dogs Together") +
                 " starts it again on the host's save. To keep your own save: F9 > Partner / Co-op > Shared save > Keep my own save "
                 "in the game.";
    } else if (token == "exiting") {
        t.segment = " | host save ready, MGSV restarts";
        t.line = ps ? "MGSV is closing to load the host's save; keep this window open."
                    : "MGSV is closing to load the host's save; keep Diamond Dogs Together open.";
    } else if (token == "offered" || token == "sending") {
        t.segment = " | sending your save to your friend " + p + "%";
        t.line = "Sending your save to your friend so you both start in the same place.";
    } else if (token == "partner_staged" || token == "partner_restarting") {
        t.segment = " | your friend is restarting on your save";
        t.line = "Your friend is restarting MGSV on your save (about a minute). Wait here; they arrive where your save continues.";
    } else if (token == "kept") {
        if (guest) {
            t.segment = " | save not shared: you kept your own save";
            t.line = "You kept your own save for this session.";
        } else {
            t.segment = " | save not shared: your friend kept their own save";
            t.line = "Your friend kept their own save.";
        }
    } else if (token == "refused") {
        if (guest) {
            t.segment = " | save not shared: the host's save does not continue into Mother Base or free roam yet";
            t.line = "The host's save cannot be shared yet: the host should Continue into Mother Base or free roam first (a save in the "
                     "story opening is never shared).";
        } else {
            t.segment = " | save not shared: your save does not continue into Mother Base or free roam yet";
            t.line = "Your save is not shared yet: Continue into Mother Base or free roam, or stay there a few seconds (a save in the "
                     "story opening is never shared).";
        }
    } else if (token == "failed") {
        t.segment = " | save not shared: the transfer failed";
        t.line = guest ? "Getting the host's save failed. Nothing was changed."
                       : (ps ? "Sending your save failed. Use Share-Save.bat instead." : "Sending your save failed. Use Tools > Share save instead.");
    } else if (token == "applied") {
        if (guest) {
            t.segment = " | playing on the host's save";
            t.line = "You now play on the host's save. " + restore + " brings your previous co-op save back.";
        }
    }
    return t;
}

bool HandoffBusyToken(const std::string& token) { return OneOf(token, {"wait_host", "receiving", "staged", "countdown", "exiting"}); }

json::Value ParseStatusText(const std::string& text, std::uint32_t expected_pid) {
    json::Value v;
    if (text.empty() || !json::Parse(text, v) || !v.is_object()) return {};
    const json::Value* ver = v.Find("v");
    if (!ver || ver->AsInt(-1) != 1 || !v.Find("state")) return {};
    if (expected_pid) {
        const json::Value* pid = v.Find("pid");
        if (!pid || pid->AsInt(-1) != static_cast<std::int64_t>(expected_pid)) return {};
    }
    return v;
}

std::string FormatStatusLine(const json::Value& status, bool host, std::int64_t now_ms, const std::string& own_build) {
    const std::string tag = host ? "[HOST]" : "[JOIN]";
    if (!status.is_object()) return tag + " waiting for the game's link status...";
    const std::string state = status.Str("state", "off");
    const std::string time_text = status.Str("time_utc");
    std::int64_t t = 0;
    if (state != "disabled" && !time_text.empty() && ParseUtc(time_text, t)) {
        const double age = static_cast<double>(now_ms - t) / 1000.0;
        if (age > 15) return tag + " game not responding (no status for " + std::to_string(static_cast<long long>(std::nearbyint(age))) + " s)";
    }
    std::string text = tag + " link=" + state;
    if (status.Str("transport", "udp") == "steam") {
        const std::string route = status.Str("route", "none");
        text += " via Steam";
        if (route == "relay" || route == "direct") text += " (" + route + ")";
        if (status.Str("steam", "ok") == "acquiring") text += " - connecting to Steam...";
        if (state == "waiting") text += " - waiting for an invite or a code (F9 -> Partner / Co-op -> Online)";
        if (state == "listening" && status.Int("accepting", 1) == 0) text += " - not accepting joins (F9 -> Online -> Accept joins)";
    }
    if (OneOf(state, {"connected", "reconnecting", "lost"})) {
        const std::int64_t lp = status.Int("loss_permille", 0);
        char buf[160];
        std::snprintf(buf, sizeof(buf), " rtt=%lldms jitter=%lldms loss=%lld.%lld%% interp=%lldms", static_cast<long long>(status.Int("rtt_ms", 0)),
                      static_cast<long long>(status.Int("jitter_ms", 0)), static_cast<long long>(lp / 10), static_cast<long long>(lp % 10),
                      static_cast<long long>(status.Int("interp_ms", 0)));
        text += buf;
    }
    const std::string reason = status.Str("reason");
    const bool verified = status.Int("verified", 1) != 0;
    if (!reason.empty())
        text += " (" + reason + ": " + LinkReasonText(reason, status.Str("remote_build"), own_build, verified, Wording::powershell) + ")";
    if (status.Int("left", 0) && state != "connected") {
        if (host) text += " - your friend left; waiting for them to come back";
        else if (reason == "bad_code" && !verified) text += " - the host restarted with a new code: paste it in F9 > Partner / Co-op > Online";
        else text += " - the host left; waiting for them to come back";
    }
    const std::int64_t steam_end = status.Int("steam_end", 0);
    if (steam_end && state != "connected") text += " (Steam: " + SteamEndText(steam_end, Wording::powershell) + ")";
    std::string partner = "partner=" + status.Str("partner", "none");
    if (status.Int("partner_shown", 0)) partner += " shown";
    if (status.Int("look", 0)) partner += " look";
    const std::string drive = status.Str("partner_drive");
    const std::string partner_reason = status.Str("partner_reason");
    const std::string view_key = status.Str("partner_view");
    if (partner_reason == "no_body_pack") {
        partner += " - " + PartnerReasonText(partner_reason);
    } else if (state == "connected" && OneOf(view_key, {"native", "clips", "loading"})) {
        std::string view = status.Str("partner_view", "loading");
        std::string gun = status.Str("partner_gun", "unavailable");
        if (status.Int("partner_view_fresh", 0) != 1) {
            view = "loading";
            gun = "unavailable";
        }
        partner += " " + view;
        std::string gun_text = "unavailable";
        if (gun == "off") gun_text = "off";
        else if (gun == "present") gun_text = "present";
        else if (gun == "match") gun_text = "matched";
        else if (gun == "substitute") gun_text = "substitute";
        else if (gun == "standin") gun_text = "stand-in";
        else if (gun == "switching") gun_text = "switching";
        else if (gun == "mismatch") gun_text = "mismatch";
        else if (gun == "projectile") gun_text = "projectile";
        partner += "; gun " + gun_text;
    } else if (drive == "native") {
        partner += " native";
    } else if (drive == "clips" || drive == "off") {
        partner += " clips (" + PartnerReasonText(partner_reason) + ")";
    }
    const std::string remote_drive = status.Str("remote_drive");
    if (remote_drive == "native") partner += "; you on the other PC: native";
    else if (remote_drive == "clips" || remote_drive == "off")
        partner += "; you on the other PC: clips (" + PartnerReasonText(status.Str("remote_reason")) + ")";
    const std::uint64_t mission = static_cast<std::uint64_t>(status.Int("mission", 0)) & 0xFFFFFFFFull;
    std::string mission_text = "mission " + Decimal(mission);
    const std::uint64_t partner_mission = static_cast<std::uint64_t>(status.Int("partner_mission", 0)) & 0xFFFFFFFFull;
    const std::string wait = status.Str("pair_wait");
    if (state == "connected" && wait == "different_mission" && partner_mission != 0) {
        mission_text += " - your partner is in " + MissionName(partner_mission) +
                        ". Different missions never pair: meet in the same Mother Base, free roam or helicopter.";
    } else if (mission != 0 && mission != 1 && mission != 40010 && mission != 40020 && mission != 40050 && mission != 30010 &&
               mission != 30020 && mission != 30050) {
        mission_text += " - Partner body unavailable in this mission; use Mother Base or free roam.";
    }
    const std::string handoff = status.Str("handoff", "off");
    const HandoffText ht = GetHandoffText(handoff, static_cast<int>(status.Int("handoff_pct", 0)), !host, Wording::powershell);
    if (!HandoffBusyToken(handoff) && status.Str("progress") == "story_start")
        mission_text += " Your co-op save is at the story start: quit, then import your partner's save with Share-Save.bat (option I).";
    return text + " | " + partner + ht.segment + " | " + mission_text;
}

// ---- Launch evidence -----------------------------------------------------------
Evidence GetLaunchEvidence(const std::string& early_log, const std::string& steam_log, const std::string& observer_log,
                           const std::string& save_dir, bool final_check, bool steam) {
    Evidence ev;
    bool unsafe = false;
    const std::string want_dir = Comparable(save_dir);
    std::string saves = "missing";
    const std::vector<std::string> early = SplitLines(early_log);
    for (const std::string& line : early) {
        const bool redirected = LineToken(line, "saves=redirected");
        const bool unchanged = LineToken(line, "saves=unchanged");
        if (!redirected && !unchanged) continue;
        if (unchanged) {
            saves = "unchanged";
            break;
        }
        const std::size_t at = line.find("; dir=");
        if (at != std::string::npos && Comparable(line.substr(at + 6)) == want_dir) {
            saves = "ok";
        } else {
            saves = "wrong_dir";
            break;
        }
    }
    if (saves == "unchanged") {
        unsafe = true;
        ev.problems.push_back("early.log shows saves=unchanged: the save copy is NOT in use.");
    } else if (saves == "wrong_dir") {
        unsafe = true;
        ev.problems.push_back("early.log shows the saves redirected to a different folder.");
    } else if (saves == "missing") {
        ev.missing.push_back("early.log saves=redirected; dir=<coop-saves>");
    }
    std::string play = "missing";
    for (const std::string& line : early) {
        if (!StartsWith(line, "play=1;")) continue;
        const std::string refused = Field(line, "; refused=");
        const bool refused_ok = refused.size() > 2 && (refused[0] == '0') && (refused[1] == 'x' || refused[1] == 'X');
        bool redirected = false;
        for (std::size_t at = line.find("; saves=redirected"); at != std::string::npos; at = line.find("; saves=redirected", at + 1)) {
            const std::size_t end = at + 18;
            if (end == line.size() || line[end] == ' ' || line[end] == ';' || line[end] == '\r') {
                redirected = true;
                break;
            }
        }
        if (refused_ok && IEqualsAscii(refused, "0x0") && redirected) {
            play = "ok";
        } else {
            play = "bad";
            if (!redirected) {
                unsafe = true;
                ev.problems.push_back("early.log play line: saves=unchanged.");
            }
            if (refused_ok && !IEqualsAscii(refused, "0x0"))
                ev.problems.push_back("early.log play line: the loader refused settings (refused=" + refused + ").");
        }
        break;
    }
    if (play == "missing") ev.missing.push_back("early.log play=1 (refused=0x0, saves=redirected)");
    std::string master = "missing";
    for (const std::string& line : SplitLines(steam_log)) {
        std::string value;
        if (LineToken(line, "master_server=redirected")) value = "redirected";
        else if (LineToken(line, "master_server=refused")) value = "refused";
        else if (LineToken(line, "master_server=failed")) value = "failed";
        else continue;
        if (value != "redirected") {
            const std::size_t at = line.find("; reason=already_redirected");
            if (at != std::string::npos) {
                const std::size_t end = at + 27;
                if (end == line.size() || line[end] == ' ' || line[end] == ';') continue;
            }
            master = "bad";
            ev.problems.push_back("steam-observer.log: master_server=" + value + ".");
            break;
        }
        std::string url;
        for (std::size_t i = 0; i + 4 < line.size(); ++i) {
            if ((line[i] == ' ' || line[i] == ';') && line.compare(i + 1, 4, "url=") == 0) {
                std::size_t end = i + 5;
                while (end < line.size() && !IsBlank(line[end])) ++end;
                url = line.substr(i + 5, end - i - 5);
                break;
            }
        }
        if (!url.empty() && url == kMasterUrl) {
            master = "ok";
        } else {
            master = "bad";
            ev.problems.push_back("steam-observer.log: master_server redirected to another URL.");
            break;
        }
    }
    ev.saves = saves;
    ev.master = master;
    if (master == "missing") ev.missing.push_back("steam-observer.log master_server=redirected (closed local port)");
    bool refused_play = false, active = false, link_config = false;
    std::string link_reason;
    for (const std::string& line : SplitLines(observer_log)) {
        if (StartsWith(line, "play=refused; reason=saves_not_redirected")) refused_play = true;
        if (LineToken(line, "play=active")) active = true;
        if (StartsWith(line, "link=config; mode=play; role=host; proto=2; bind=steam; port=0;") ||
            StartsWith(line, "link=config; mode=play; role=guest; proto=2; bind=steam; port=0;"))
            link_config = true;
        if (link_reason.empty() && StartsWith(line, "link=disabled; reason=")) {
            std::size_t end = 22;
            while (end < line.size() && ((line[end] >= 'a' && line[end] <= 'z') || IsDigit(line[end]) || line[end] == '_')) ++end;
            if (end > 22) link_reason = line.substr(22, end - 22);
        }
    }
    if (refused_play) {
        unsafe = true;
        ev.problems.push_back("script-observer.log: play=refused; reason=saves_not_redirected.");
    }
    if (!active && !refused_play) ev.missing.push_back("script-observer.log play=active");
    std::string link = "ok";
    if (steam) {
        link = link_config ? "ok" : "missing";
        if (!link_reason.empty()) {
            link = "disabled";
            ev.link_reason = link_reason;
            ev.problems.push_back("script-observer.log: link=disabled; reason=" + link_reason + " - " +
                                  LinkReasonText(link_reason, "", "", true, Wording::launcher));
        }
        if (link == "missing") ev.missing.push_back("script-observer.log link=config; ...; bind=steam; port=0");
    }
    if (unsafe) ev.state = "unsafe";
    else if (saves == "ok" && play == "ok" && master == "ok" && active && link == "ok") ev.state = "ok";
    else if (final_check) {
        if (saves != "ok") ev.state = "unsafe";
        else if (play == "missing") ev.state = "old_loader";
        else ev.state = "incomplete";
    } else if (play == "bad" || master == "bad" || link == "disabled") ev.state = "incomplete";
    else ev.state = "pending";
    return ev;
}

// ---- Redaction ---------------------------------------------------------------
namespace {
bool CanonicalIPv4(const std::string& text, std::uint32_t& value) {
    std::uint32_t v = 0;
    int parts = 0;
    std::size_t i = 0;
    while (parts < 4) {
        if (i >= text.size() || !IsDigit(text[i])) return false;
        std::size_t start = i;
        unsigned octet = 0;
        while (i < text.size() && IsDigit(text[i])) {
            octet = octet * 10 + static_cast<unsigned>(text[i] - '0');
            if (i - start >= 3) return false;
            ++i;
        }
        if (i - start > 1 && text[start] == '0') return false;
        if (octet > 255) return false;
        v = (v << 8) | octet;
        ++parts;
        if (parts < 4) {
            if (i >= text.size() || text[i] != '.') return false;
            ++i;
        }
    }
    if (i != text.size()) return false;
    value = v;
    return true;
}

bool InRange(std::uint32_t v, std::uint32_t net, int bits) {
    const std::uint32_t mask = bits == 0 ? 0 : 0xFFFFFFFFu << (32 - bits);
    return (v & mask) == (net & mask);
}

bool ShownAddress(const std::string& ip) {
    std::uint32_t v = 0;
    if (!CanonicalIPv4(ip, v)) return false;
    return InRange(v, 0x7F000000u, 8) || InRange(v, 0x0A000000u, 8) || InRange(v, 0xAC100000u, 12) || InRange(v, 0xC0A80000u, 16) ||
           InRange(v, 0x64400000u, 10) || InRange(v, 0x1A000000u, 8);
}

// [0-9]{1,3}(\.[0-9]{1,3}){3} at `i`, not followed by [0-9.]; length in `n`.
bool MatchIp(const std::string& s, std::size_t i, std::size_t& n) {
    std::size_t p = i;
    for (int part = 0; part < 4; ++part) {
        std::size_t start = p;
        while (p < s.size() && IsDigit(s[p]) && p - start < 3) ++p;
        if (p == start) return false;
        if (part < 3) {
            if (p >= s.size() || s[p] != '.') return false;
            ++p;
        }
    }
    if (p < s.size() && (IsDigit(s[p]) || s[p] == '.')) return false;
    n = p - i;
    return true;
}

bool IStartsAt(const std::string& s, std::size_t i, const char* word) {
    const std::size_t n = std::strlen(word);
    if (i + n > s.size()) return false;
    for (std::size_t k = 0; k < n; ++k) {
        char a = s[i + k], b = word[k];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}
}  // namespace

std::string MaskAddress(const std::string& address) {
    if (address.empty()) return address;
    // ^a.b.c.d(:port)?$
    std::size_t colon = address.find(':');
    const std::string ip = colon == std::string::npos ? address : address.substr(0, colon);
    const std::string rest = colon == std::string::npos ? std::string() : address.substr(colon);
    if (!rest.empty()) {
        if (rest.size() < 2) return address;
        for (std::size_t i = 1; i < rest.size(); ++i)
            if (!IsDigit(rest[i])) return address;
    }
    std::size_t dots = 0;
    for (char c : ip) {
        if (c == '.') ++dots;
        else if (!IsDigit(c)) return address;
    }
    if (dots != 3) return address;
    const std::size_t d1 = ip.find('.'), d2 = ip.find('.', d1 + 1);
    if (d1 == 0 || d2 == d1 + 1) return address;
    if (ShownAddress(ip)) return address;
    return ip.substr(0, d2) + ".x.x" + rest;
}

std::string RedactSensitive(const std::string& text) {
    if (text.empty()) return text;
    std::string out;
    out.reserve(text.size());
    // 1. 17-digit runs: SteamIDs keep their first 7 and last 4 digits.
    for (std::size_t i = 0; i < text.size();) {
        if (IsDigit(text[i]) && (i == 0 || !IsDigit(text[i - 1]))) {
            std::size_t j = i;
            while (j < text.size() && IsDigit(text[j])) ++j;
            const std::string run = text.substr(i, j - i);
            if (run.size() == 17 && (StartsWith(run, "7656119") || IsIndividualSteamId64(run)))
                out += run.substr(0, 7) + "XXXXXX" + run.substr(13);
            else
                out += run;
            i = j;
            continue;
        }
        out.push_back(text[i++]);
    }
    // 2. userdata\<account>.
    std::string step2;
    for (std::size_t i = 0; i < out.size();) {
        if (IStartsAt(out, i, "userdata")) {
            std::size_t j = i + 8;
            std::size_t seps = j;
            while (seps < out.size() && (out[seps] == '\\' || out[seps] == '/')) ++seps;
            std::size_t digits = seps;
            while (digits < out.size() && IsDigit(out[digits])) ++digits;
            if (seps > j && digits > seps) {
                step2 += out.substr(i, seps - i) + "<account>";
                i = digits;
                continue;
            }
        }
        step2.push_back(out[i++]);
    }
    // 3. X:\Users\<user>.
    std::string step3;
    for (std::size_t i = 0; i < step2.size();) {
        const char c = step2[i];
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (letter && (i == 0 || !IsWordChar(step2[i - 1])) && i + 1 < step2.size() && step2[i + 1] == ':') {
            std::size_t p = i + 2;
            std::size_t s1 = p;
            while (p < step2.size() && (step2[p] == '\\' || step2[p] == '/')) ++p;
            if (p > s1 && IStartsAt(step2, p, "users")) {
                std::size_t q = p + 5;
                std::size_t s2 = q;
                while (q < step2.size() && (step2[q] == '\\' || step2[q] == '/')) ++q;
                std::size_t name = q;
                while (name < step2.size() && std::strchr("\\/\r\n\"';", step2[name]) == nullptr) ++name;
                if (q > s2 && name > q) {
                    step3 += step2.substr(i, q - i) + "<user>";
                    i = name;
                    continue;
                }
            }
        }
        step3.push_back(step2[i++]);
    }
    return step3;
}

std::string RedactNetwork(const std::string& text) {
    if (text.empty()) return text;
    // Join codes: MGS1/MGS2, blanks or dashes, 22 code characters, optional @host.
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        if (IStartsAt(text, i, "MGS") && i + 3 < text.size() && (text[i + 3] == '1' || text[i + 3] == '2')) {
            std::size_t p = i + 4;
            while (p < text.size() && (IsBlank(text[p]) || text[p] == '-')) ++p;
            int count = 0;
            std::size_t last_end = p;
            while (p < text.size() && count < 22) {
                if (!IsAlnumAscii(text[p])) break;
                ++count;
                ++p;
                last_end = p;
                if (count < 22)
                    while (p < text.size() && (IsBlank(text[p]) || text[p] == '-')) ++p;
            }
            if (count == 22) {
                std::size_t end = last_end;
                std::size_t q = end;
                while (q < text.size() && (IsBlank(text[q]) || text[q] == '-')) ++q;
                if (q + 1 < text.size() && text[q] == '@' && !IsBlank(text[q + 1])) {
                    q += 1;
                    while (q < text.size() && !IsBlank(text[q])) ++q;
                    end = q;
                }
                out += std::string("MGS") + text[i + 3] + "-<redacted>";
                i = end;
                continue;
            }
        }
        out.push_back(text[i++]);
    }
    // Invite strings.
    std::string step2;
    static const char kJoin[] = "+mgscoop_join";
    for (std::size_t i = 0; i < out.size();) {
        if (out.compare(i, sizeof(kJoin) - 1, kJoin) == 0) {
            std::size_t p = i + sizeof(kJoin) - 1;
            std::size_t blanks = p;
            while (p < out.size() && IsBlank(out[p])) ++p;
            std::size_t tok = p;
            while (tok < out.size() && !IsBlank(out[tok])) ++tok;
            if (p > blanks && tok > p) {
                step2 += "+mgscoop_join <redacted>";
                i = tok;
                continue;
            }
        }
        step2.push_back(out[i++]);
    }
    // Public IPv4 addresses after peer=, net_peer_addr=, "peer":" and host=.
    std::string step3;
    for (std::size_t i = 0; i < step2.size();) {
        std::size_t prefix = 0;
        const bool boundary = i == 0 || !IsWordChar(step2[i - 1]);
        if (boundary && step2.compare(i, 5, "peer=") == 0) prefix = 5;
        else if (boundary && step2.compare(i, 14, "net_peer_addr=") == 0) prefix = 14;
        else if (boundary && step2.compare(i, 5, "host=") == 0) prefix = 5;
        else if (step2.compare(i, 6, "\"peer\"") == 0) {
            std::size_t p = i + 6;
            while (p < step2.size() && IsBlank(step2[p])) ++p;
            if (p < step2.size() && step2[p] == ':') {
                ++p;
                while (p < step2.size() && IsBlank(step2[p])) ++p;
                if (p < step2.size() && step2[p] == '"') prefix = p + 1 - i;
            }
        }
        std::size_t n = 0;
        if (prefix && MatchIp(step2, i + prefix, n)) {
            step3 += step2.substr(i, prefix) + MaskAddress(step2.substr(i + prefix, n));
            i += prefix + n;
            continue;
        }
        step3.push_back(step2[i++]);
    }
    return step3;
}

std::string Redact(const std::string& text) { return RedactNetwork(RedactSensitive(text)); }

std::string RedactForLog(const std::string& text) {
    const std::string r = Redact(text);
    std::string out;
    std::size_t i = 0;
    while (i < r.size()) {
        if (IsBlank(r[i])) {
            out.push_back(r[i++]);
            continue;
        }
        std::size_t j = i;
        while (j < r.size() && !IsBlank(r[j])) ++j;
        const std::string token = r.substr(i, j - i);
        out += token.find('\\') != std::string::npos || token.find(":/") != std::string::npos ? "<path>" : token;
        i = j;
    }
    return out;
}

}  // namespace ddt::core
