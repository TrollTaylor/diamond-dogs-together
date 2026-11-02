// Diamond Dogs Together launcher core: the install in one game folder. The
// hash gates, package.json, friend-install.json (the 0.13.x schema, so an
// earlier install is adopted), the launcher settings, first-run setup, the
// uninstall and Collect logs.
//
// The expected dinput8.dll hash: read from mgs-coop\package.json, which the
// release zip ships next to the exe (files[] row with target "dinput8.dll").
// The release packaging writes that file from the same build as the loader,
// so the exe does not need to be rebuilt when the loader changes; the file's
// product, version (the exe's own X.Y.Z) and the pinned game hashes are
// checked before its loader hash is trusted. The game hashes (mgsvtpp.exe,
// steam_api64.dll) and the runtime archive hash are compiled in.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core_env.hpp"
#include "core_json.hpp"
#include "core_paths.hpp"

namespace ddt::core {

inline constexpr char kExeSha256[] = "085C2F82D1C963C40B3D2D55786661DFEE2B18CBBF388A710C00FA76C5E9BB45";
inline constexpr char kSteamApiSha256[] = "08BD0152356E5619EE56A6714AC951B6800DBB21821693A4EFAF85278442C60A";
inline constexpr char kPackageProduct[] = "mgs-coop-local-package";
inline constexpr char kInstallProduct[] = "mgs-coop-friend-install";

struct Settings;

// Test seams (process-wide; production code never changes them): the pinned
// game hashes and the runtime archive check/build (the real builder needs the
// real game archives).
struct InstallHooks {
    std::string exe_sha256 = kExeSha256;
    std::string steam_api_sha256 = kSteamApiSha256;
    std::function<bool(const std::wstring& path, std::string& error)> verify_runtime;   // empty = VerifyRuntimeDat
    std::function<bool(const std::wstring& game, const std::wstring& out, const std::function<void(float, const std::string&)>& progress,
                       std::string& error)>
        build_runtime;   // empty = BuildRuntimeDat
};
InstallHooks& Hooks();
// The runtime archive in the game folder has the pinned size and SHA-256
// (cached by size and time when `settings` is given).
bool RuntimeArchiveValid(const InstancePaths& paths, Settings* settings, std::string& error);

std::string CoreVersion();   // X.Y.Z (CMake project version)
std::string CoreBuildId();   // X.Y.Z+<git hash> when known

// ---- Launcher settings (mgs-coop\launcher-settings.json) --------------------------
struct Settings {
    std::string character;   // story | ocelot | dd_male | dd_female | avatar ("" = not chosen)
    std::string hud;         // compact | full | off ("" = not chosen)
    bool debug = false;      // Settings > Debug mode (developer tests)
    std::string local_guest; // the local two-window test's second game folder ("" = none)
    json::Value raw = json::Value::Object();
};
bool LoadSettings(const InstancePaths& paths, Settings& out);
bool SaveSettings(const InstancePaths& paths, Settings& settings, std::string& error);
// SHA-256 of a large file, cached in the settings by size and last-write time.
bool CachedFileSha256(const InstancePaths& paths, Settings& settings, const std::wstring& file, std::string& hex);

// The joiner's character for an install record value: quiet -> story,
// story/ocelot/dd_male/dd_female/avatar kept, anything else -> dd_male.
std::string JoinerCharacter(const std::string& value);
bool IsJoinerCharacter(const std::string& value);

// ---- package.json and the hash gates -------------------------------------------------
struct PackageInfo {
    bool ok = false;
    std::string problem;
    std::string version;
    std::string loader_sha256;   // upper case
};
PackageInfo ReadPackage(const InstancePaths& paths);
// The exe, steam_api64.dll and dinput8.dll gates; "" when all pass.
std::string GameFileProblem(const InstancePaths& paths, Settings& settings, const PackageInfo& package);
// The loader in the folder is this package's (hash equal to package.json).
bool LoaderIsOurs(const InstancePaths& paths, const PackageInfo& package);

// ---- Steam account ---------------------------------------------------------------------
struct SteamAccount {
    bool ok = false;
    std::string problem;
    std::wstring steam_root, userdata;
    std::uint32_t account = 0;
    bool active = false;   // from Steam's ActiveUser (else the only account with MGSV saves)
};
SteamAccount ResolveSteamAccount(Env& env);
std::uint32_t SteamActiveUser(Env& env);
// The player's own SteamID64 (ActiveUser), 0 when Steam is not running / logged in.
std::uint64_t OwnSteamId(Env& env);

// ---- friend-install.json --------------------------------------------------------------
bool ReadInstallState(const InstancePaths& paths, json::Value& out);   // product mgs-coop-friend-install
// Status installed, this version, written by Diamond Dogs Together (an earlier
// PowerShell install is adopted by running the setup once).
bool InstallIsCurrent(const json::Value& state);

// ---- Setup -------------------------------------------------------------------------------
enum class StepState { pending, running, done, failed };
struct StepReport {
    int index = 0;   // 0 game check, 1 save backup, 2 co-op save, 3 runtime build
    StepState state = StepState::pending;
    std::string detail;
    float progress = 0.0f;
};
using SetupCallback = std::function<void(const StepReport&)>;
// Runs the four steps; resumable and idempotent (a run killed half way leaves
// friend-install.json "pending", and the next run finishes the job).
bool RunSetup(Env& env, const InstancePaths& paths, const std::string& character, const std::string& hud, const SetupCallback& report,
              const std::atomic<bool>* cancel, std::string& error);
// One setup at a time per game folder (0.14.4 review): RunSetup holds the
// folder's setup lock (SetupMutexName) while it runs. True while another thread
// or process holds it: a window then waits instead of starting its own setup.
bool SetupRunningElsewhere(const std::wstring& game_root);
// What keeps a setup from running in this folder right now, as a log token (fb1031: on a friend's PC the first setup
// after an update failed at step 0 four times in 22 s, until Steam had logged in): "other_setup" (another setup holds
// the folder's lock), "game_running" (MGSV runs from this folder), "session_live" (co-op is on for a game another
// window or its helper started), "steam_login" (Steam is installed but nobody is logged in, and the saves alone do not
// name the account), or empty. The window waits for these, then sets co-op up by itself.
std::string SetupBusyReason(Env& env, const InstancePaths& paths);
// The player's sentence while the window waits for a SetupBusyReason token (empty for none or another token).
std::string SetupBusyText(const std::string& reason);

// ---- Uninstall and Collect logs -------------------------------------------------------
bool Uninstall(Env& env, const InstancePaths& paths, std::string& report, std::string& error);
bool CollectLogs(Env& env, const InstancePaths& paths, const std::wstring& out_dir, std::wstring& zip_out, std::string& error);

// Collect-logs text helpers (ports of Get-TextFromLastMarkers / Limit-TextTail).
std::string TextFromLastMarkers(const std::string& text, const std::string& marker, int count);
std::string LimitTextTail(const std::string& text, std::size_t max_chars);
// The last `count` game launches of a log whose launches start at a line holding `marker` (text before the first
// marker counts as one more). A launch over `per_launch` keeps its first quarter and its end around an omission
// line; the oldest launches are dropped until the whole is at most `total` (the newest always stays, cut).
std::string LastLaunches(const std::string& text, const std::string& marker, int count, std::size_t per_launch, std::size_t total);
// The same with its own budget for the newest launch (fb1101 X1: the session a report is about; at least per_launch).
std::string LastLaunches(const std::string& text, const std::string& marker, int count, std::size_t per_launch, std::size_t total,
                         std::size_t newest_budget);
// " (t_ms A..B)": the first and the last game-clock value (t_ms= or tick_ms=) inside text[begin, end), or "" for none.
std::string CutSpan(const std::string& text, std::size_t begin, std::size_t end);
// The newest launch of a log (from the line holding the last `marker`; the whole text without one).
std::string NewestLaunch(const std::string& text, const std::string& marker);
// The lines holding any of `needles`, at most max_chars (the newest kept, the cut marked as in LimitTextTail).
std::string FilterLines(const std::string& text, const std::vector<std::string>& needles, std::size_t max_chars);

}  // namespace ddt::core
