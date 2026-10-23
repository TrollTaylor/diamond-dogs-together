// Diamond Dogs Together launcher core: folder layout and path rules.
#include "core_paths.hpp"

#include <algorithm>

namespace ddt::core {

InstancePaths GetInstancePaths(const std::wstring& game_dir) {
    InstancePaths p;
    p.game = FullPath(game_dir);
    p.exe = JoinPath2(p.game, L"mgsvtpp.exe");
    p.steam_api = JoinPath2(p.game, L"steam_api64.dll");
    p.loader = JoinPath2(p.game, L"dinput8.dll");
    p.mod = JoinPath2(p.game, L"mgs-coop");
    auto m = [&](const wchar_t* name) { return JoinPath2(p.mod, name); };
    p.record = m(L"probe-install.json");
    p.archive = m(L"mgs-coop-player-runtime.dat");
    p.marker = m(L"script-observe.enabled");
    p.client_host = m(L"client-host.txt");
    p.lab_ini = m(L"lab.ini");
    p.coop_ini = m(L"coop.ini");
    p.lab_status = m(L"lab-status.json");
    p.link_secret = m(L"link-secret.txt");
    p.link_status = m(L"link-status.json");
    p.play_session = m(L"play-session.json");
    p.coop_saves = m(L"coop-saves");
    p.coop_saves_handoff = m(L"coop-saves-handoff");
    p.coop_saves_replaced = m(L"coop-saves-replaced");
    p.link_rejoin = m(L"link-rejoin.txt");
    p.play_coop_log = m(L"play-coop.log");
    p.launcher_log = m(L"launcher.log");
    p.launcher_old_log = m(L"launcher.old.log");
    p.friend_install = m(L"friend-install.json");
    p.package_json = m(L"package.json");
    p.settings = m(L"launcher-settings.json");
    p.backups_root = m(L"real-save-backups");
    return p;
}

std::string GamePathProblem(const std::wstring& game_root, const std::vector<std::string>& save_files) {
    const std::wstring root = FullPath(game_root);
    if (IsDriveRoot(root))
        return "Diamond Dogs Together must be in the MGSV game folder (the one with mgsvtpp.exe), not at the top of a drive.";
    if (IsReparsePoint(root))
        return "This game folder is a link (junction or symbolic link). Put Diamond Dogs Together in the real MGSV folder.";
    if (!IsAsciiPrintable(root))
        return "The game folder's path has characters outside plain English letters and digits, which the co-op loader cannot use. "
               "Move the game to a plain folder such as C:\\Games (Steam > Settings > Storage), then put Diamond Dogs Together there again.";
    const std::string save_dir = Utf8(root) + "\\mgs-coop\\coop-saves";
    if (save_dir.size() > kMaxSaveDirBytes)
        return "The game folder path is too long for the co-op save copy (" + std::to_string(save_dir.size()) +
               " characters; the limit is " + std::to_string(kMaxSaveDirBytes) + "). Move the game to a shorter folder path.";
    const std::string zeros32(32, '0');
    std::vector<std::string> candidates = {
        "\\mgs-coop\\mgs-coop-player-runtime.dat.tmp-4294967295-18446744073709551615",
        "\\mgs-coop\\friend-install.json." + zeros32 + ".tmp",
        "\\mgs-coop\\launcher-settings.json." + zeros32 + ".tmp",
        "\\mgs-coop\\script-observe.enabled.disabled-00000000-000000-99",
        "\\mgs-coop\\script-observe.enabled.before-friend-00000000-000000",
        "\\mgs-coop\\friend-install.json.uninstalled-00000000-000000-99",
        "\\mgs-coop\\coop-saves-handoff\\00000000.partial\\287700\\local\\TPP_GAME_DATA0.mgs-coop-" + zeros32 + ".tmp",
        "\\mgs-coop\\coop-saves-replaced\\00000000-000000-99-before-handoff\\287700\\local\\TPP_GRAPHICS_CONFIG",
        "\\mgs-coop\\coop-saves-new-00000000-000000-99\\imported-progress.json." + zeros32 + ".tmp",
        "\\mgs-coop\\coop-saves-new-00000000-000000-99\\287700\\local\\TPP_GRAPHICS_CONFIG",
        "\\mgs-coop\\coop-saves-seed-00000000-99\\287700\\local\\TPP_GRAPHICS_CONFIG",
        "\\mgs-coop\\save-import-00000000-000000-99\\287700\\local\\TPP_GAME_DATA0." + zeros32 + ".tmp",
    };
    for (const std::string& f : save_files) {
        if (f.empty()) continue;
        std::string rel = f;
        std::replace(rel.begin(), rel.end(), '/', '\\');
        while (!rel.empty() && rel.front() == '\\') rel.erase(rel.begin());
        const std::size_t slash = rel.rfind('\\');
        const std::string leaf = slash == std::string::npos ? rel : rel.substr(slash + 1);
        candidates.push_back("\\mgs-coop\\real-save-backups\\00000000-000000-99\\files\\" + rel);
        candidates.push_back("\\mgs-coop\\coop-saves-replaced\\00000000-000000\\remote-00000000\\" + leaf);
    }
    const std::string base = Utf8(root);
    std::size_t longest_file = 0, longest_dir = 0;
    for (const std::string& c : candidates) {
        const std::string full = base + c;
        longest_file = std::max(longest_file, full.size());
        longest_dir = std::max(longest_dir, full.substr(0, full.rfind('\\')).size());
    }
    if (longest_file > kMaxFilePathChars || longest_dir > kMaxDirectoryPathChars)
        return "The game folder path is too long (" + std::to_string(base.size()) + " characters): the co-op files would need paths of " +
               std::to_string(longest_file) + " characters, and Windows limits paths to " + std::to_string(kMaxFilePathChars) +
               ". Move the game to a shorter folder path (Steam > Settings > Storage).";
    return {};
}

std::wstring PlayMutexName(const std::wstring& game_root) {
    std::wstring root = FullPath(game_root);
    while (!root.empty() && (root.back() == L'\\' || root.back() == L'/') && !IsDriveRoot(root)) root.pop_back();
    const std::string hash = Sha256Hex(Utf8(UpperInvariant(root)));
    return L"Local\\mgs-coop-play-" + Wide(hash.substr(0, 16));
}

std::vector<std::wstring> SteamLibraryPathsFromVdf(const std::string& text) {
    std::vector<std::wstring> out;
    for (const std::string& raw : SplitLines(text)) {
        std::size_t p = 0;
        while (p < raw.size() && (raw[p] == ' ' || raw[p] == '\t')) ++p;
        if (p >= raw.size() || raw[p] != '"') continue;
        const std::size_t key_end = raw.find('"', p + 1);
        if (key_end == std::string::npos) continue;
        const std::string key = raw.substr(p + 1, key_end - p - 1);
        bool numeric = !key.empty();
        for (char c : key)
            if (c < '0' || c > '9') numeric = false;
        if (key != "path" && !numeric) continue;
        std::size_t q = key_end + 1;
        while (q < raw.size() && (raw[q] == ' ' || raw[q] == '\t')) ++q;
        if (q >= raw.size() || raw[q] != '"') continue;
        std::string value;
        bool closed = false;
        for (std::size_t i = q + 1; i < raw.size(); ++i) {
            if (raw[i] == '\\' && i + 1 < raw.size()) {
                value.push_back(raw[i + 1]);
                ++i;
                continue;
            }
            if (raw[i] == '"') {
                closed = true;
                break;
            }
            value.push_back(raw[i]);
        }
        if (!closed) continue;
        const bool absolute = (value.size() >= 3 && ((value[0] >= 'A' && value[0] <= 'Z') || (value[0] >= 'a' && value[0] <= 'z')) &&
                               value[1] == ':' && value[2] == '\\') ||
                              StartsWith(value, "\\\\");
        if (!absolute) continue;
        while (!value.empty() && (value.back() == '\\' || value.back() == '/')) value.pop_back();
        const std::wstring w = Wide(value);
        bool dup = false;
        for (const std::wstring& e : out)
            if (IEquals(e, w)) dup = true;
        if (!dup) out.push_back(w);
    }
    return out;
}

std::string MgsvInstallDir(const std::string& manifest_text) {
    for (const std::string& raw : SplitLines(manifest_text)) {
        const std::string line = Trim(raw);
        if (!StartsWith(line, "\"installdir\"")) continue;
        const std::size_t q = line.find('"', 12);
        if (q == std::string::npos) continue;
        const std::size_t e = line.find('"', q + 1);
        if (e == std::string::npos || e == q + 1) continue;
        const std::string v = line.substr(q + 1, e - q - 1);
        if (v.find_first_of("\\/:*?<>|") != std::string::npos) continue;
        return v;
    }
    return "MGS_TPP";
}

std::vector<std::wstring> FindMgsvGameFolders(Env& env) {
    std::vector<std::wstring> roots, candidates, found;
    const wchar_t* steam_keys[][2] = {{L"0", L"Software\\Valve\\Steam"},
                                      {L"1", L"SOFTWARE\\WOW6432Node\\Valve\\Steam"},
                                      {L"1", L"SOFTWARE\\Valve\\Steam"}};
    for (const auto& k : steam_keys) {
        const bool hklm = k[0][0] == L'1';
        for (const wchar_t* name : {L"SteamPath", L"InstallPath"}) {
            std::wstring v;
            if (env.RegString(hklm, k[1], name, v) && !v.empty()) {
                std::replace(v.begin(), v.end(), L'/', L'\\');
                roots.push_back(v);
            }
        }
    }
    for (const wchar_t* key : {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 287700",
                               L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 287700"}) {
        std::wstring v;
        if (env.RegString(true, key, L"InstallLocation", v) && !v.empty()) candidates.push_back(v);
    }
    auto add = [&](const std::wstring& folder) {
        if (folder.empty()) return;
        const std::wstring full = FullPath(folder);
        if (!IsFile(JoinPath2(full, L"mgsvtpp.exe"))) return;
        for (const std::wstring& e : found)
            if (IEquals(e, full)) return;
        found.push_back(full);
    };
    for (const std::wstring& c : candidates) add(c);
    for (const std::wstring& root : roots) {
        std::vector<std::wstring> libraries = {FullPath(root)};
        for (const wchar_t* vdf : {L"steamapps\\libraryfolders.vdf", L"config\\libraryfolders.vdf"}) {
            std::string text;
            if (ReadTextShared(JoinPath2(root, vdf), text, 4u << 20))
                for (const std::wstring& lib : SteamLibraryPathsFromVdf(text)) libraries.push_back(lib);
        }
        for (const std::wstring& lib : libraries) {
            std::string manifest, dir = "MGS_TPP";
            if (ReadTextShared(JoinPath2(lib, L"steamapps\\appmanifest_287700.acf"), manifest, 1u << 20)) dir = MgsvInstallDir(manifest);
            add(JoinPath2(JoinPath2(lib, L"steamapps\\common"), Wide(dir)));
        }
    }
    return found;
}

}  // namespace ddt::core
