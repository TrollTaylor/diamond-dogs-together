// Diamond Dogs Together launcher core: the folder layout of one game folder,
// the path rules, the per-folder mutex name and Steam's MGSV folder discovery.
#pragma once
#include <string>
#include <vector>

#include "core_env.hpp"

namespace ddt::core {

// G = the game folder, M = G\mgs-coop (Get-InstancePaths).
struct InstancePaths {
    std::wstring game, exe, steam_api, loader;
    std::wstring mod, record, archive, marker, client_host, lab_ini, coop_ini, lab_status, link_secret, link_status, play_session;
    std::wstring coop_saves, coop_saves_handoff, coop_saves_replaced, link_rejoin, play_coop_log;
    std::wstring launcher_log, launcher_old_log, friend_install, package_json, settings, backups_root;
};
InstancePaths GetInstancePaths(const std::wstring& game_dir);

// native/include/mgscoop/save_redirect.hpp kMaxSaveDirChars (UTF-8 bytes).
inline constexpr std::size_t kMaxSaveDirBytes = 213;
inline constexpr std::size_t kMaxFilePathChars = 259;
inline constexpr std::size_t kMaxDirectoryPathChars = 247;

// "" when the game folder can hold the co-op files, otherwise a sentence:
// a drive root, a link, a path that is not plain ASCII (the loader refuses a
// non-ASCII runtime archive path), a save_dir over 213 bytes, or a created
// path over the classic Windows limits. save_files: the real saves' paths
// relative to userdata\<account> ('287700/local/TPP_GAME_DATA0'), checked as
// backup and archive copies.
std::string GamePathProblem(const std::wstring& game_root, const std::vector<std::string>& save_files = {});

// Local\mgs-coop-play-<first 16 hex of SHA-256(upper-case game root)>, the same
// name the PowerShell tooling used.
std::wstring PlayMutexName(const std::wstring& game_root);

// Steam's MGSV folders (read-only): registry Steam roots, the uninstall key's
// InstallLocation, libraryfolders.vdf libraries and appmanifest_287700.acf
// installdir; folders that contain mgsvtpp.exe, first found first.
std::vector<std::wstring> SteamLibraryPathsFromVdf(const std::string& text);
std::string MgsvInstallDir(const std::string& manifest_text);
std::vector<std::wstring> FindMgsvGameFolders(Env& env);

}  // namespace ddt::core
