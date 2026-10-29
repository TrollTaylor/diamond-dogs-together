// Diamond Dogs Together launcher: update checking, download and self-update
// from GitHub Releases.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core_paths.hpp"
#include "launcher_model.hpp"

namespace ddt::core {

using ddt::UpdateStatus;
using ddt::UpdateInfo;

struct UpdateProgress {
    std::uint64_t bytes_downloaded = 0;
    std::uint64_t total_bytes = 0;
    float progress = 0.0f; // 0..1
};

// SemVer parsing and comparison:
// Parses versions in forms like "0.14.1", "v0.14.1", "0.14.2+hash".
bool ParseSemVer(const std::string& str, int& major, int& minor, int& patch);
bool IsVersionNewer(const std::string& candidate, const std::string& current);

// GitHub release feed parser:
// Accepts both an array of releases (GET /repos/.../releases) and a single
// release object (GET /repos/.../releases/latest). The update is the player
// zip (DiamondDogsTogether-<ver>.zip, or mgs-coop-friend-<ver>.zip before
// 0.14.0); an asset whose name contains "source" (the GPL source archive
// DDT-source-<ver>.zip published next to it) is never picked.
bool ParseReleasesFeed(const std::string& json_text, const std::string& current_version, UpdateInfo& out, std::string& error);

// Test seams for network requests and process restart
struct UpdateHooks {
    std::function<bool(const std::string& url, std::string& body_out, int& status_code, std::string& error)> http_get;
    std::function<bool(const std::string& url, const std::wstring& dest_path,
                       const std::function<void(const UpdateProgress&)>& on_progress,
                       const std::atomic<bool>* cancel, std::string& error)> http_download;
    // Replaces StartSuccessor in RelaunchSelf: the new exe and its arguments.
    std::function<bool(const std::wstring& exe, const std::vector<std::wstring>& args, std::string& error)> relaunch;
};
UpdateHooks& GetUpdateHooks();

// WinHttp network operations
bool WinHttpGet(const std::string& url, std::string& body_out, int& status_code, std::string& error);
bool WinHttpDownload(const std::string& url, const std::wstring& dest_path,
                     const std::function<void(const UpdateProgress&)>& on_progress,
                     const std::atomic<bool>* cancel, std::string& error);

// High-level update operations. feed_url "" = the GitHub releases of the
// project; the developer flag --update-feed passes a loopback test feed.
bool CheckForUpdates(const std::string& current_version, UpdateInfo& out, std::string& error,
                     const std::string& feed_url = std::string());
bool DownloadUpdate(const UpdateInfo& info, const std::wstring& dest_zip,
                    const std::function<void(const UpdateProgress&)>& on_progress,
                    const std::atomic<bool>* cancel, std::string& error);

// Installs a downloaded player zip over the game folder, all or nothing:
//  - the zip must be a Diamond Dogs Together package (mgs-coop/package.json,
//    product mgs-coop-local-package) with safe paths only;
//  - every file is first written next to its target (<target>.ddt-new); then
//    each target is moved aside (the running exe to DiamondDogsTogether.exe.old,
//    others to <target>.ddt-old) and the new file moved in. Any failure moves
//    everything back, so the folder keeps the old version complete;
//  - files the previous package.json listed under mgs-coop/ that the new
//    package no longer has (for example mgs-coop/source.zip, shipped up to
//    0.14.3) are deleted, but only while their SHA-256 still matches that
//    listing: nothing that is not ours, and nothing the player changed.
// The zip is deleted afterwards. `report` (optional) lists what was removed.
struct ApplyReport {
    std::size_t files = 0;
    std::vector<std::string> removed;   // stale package files deleted (zip-style names)
};
bool ApplyUpdateZip(const InstancePaths& paths, const std::wstring& zip_path, std::string& error, ApplyReport* report = nullptr);

// ---- Restart after an update ---------------------------------------------------------
// The launcher that installed an update (A) hands its window over to the new
// exe (B); before 0.14.4 A started B and exited at once, so B usually found A
// still holding the folder's single-instance mutex and closed itself:
//  A  creates the event UpdateHandoffEventName(A's pid), starts B with
//     --after-update <A's pid> and waits until B sets the event (B runs) or B
//     exits (B failed: A stays open and says so). Then A closes its window,
//     releases the single-instance mutex and exits normally.
//  B  sets the event, waits up to kPreviousLauncherWaitMs for A to exit (a
//     hung A is ended), then claims the single instance, deletes the .old exe
//     and says "Updated to <version>".
inline constexpr int kSuccessorReadyWaitMs = 20000;
inline constexpr int kPreviousLauncherWaitMs = 15000;
inline constexpr int kStopPreviousWaitMs = 2000;
inline constexpr int kClaimAfterHandoffMs = 5000;
std::wstring UpdateHandoffEventName(std::uint32_t pid);

// A's side: starts `exe` with `args` in `dir` (no handles inherited, same
// desktop) and waits up to ready_wait_ms for it to set this process's handoff
// event. True: B signalled, or B still runs after the wait (it takes over once
// this process exits). False: B could not start or exited early; `error` says
// why in a sentence.
struct SuccessorStart {
    std::uint32_t pid = 0;
    bool signalled = false;
};
bool StartSuccessor(const std::wstring& exe, const std::vector<std::wstring>& args, const std::wstring& dir, int ready_wait_ms,
                    SuccessorStart& out, std::string& error);
// StartSuccessor(<game>\DiamondDogsTogether.exe, args) (or hooks.relaunch).
// It never exits this process: the caller closes its window on success.
bool RelaunchSelf(const InstancePaths& paths, const std::vector<std::wstring>& args, std::string& error);
// One command-line argument, quoted for CommandLineToArgvW when needed.
std::wstring QuoteArgument(const std::wstring& arg);

// B's side, as steps so their order is testable (TakeOverFromPreviousLauncher):
// signal A, wait for A to exit (end it when it hangs), then claim.
struct PreviousLauncherOps {
    std::function<bool()> signal;              // false: no launcher waits for us (A is gone)
    std::function<bool(int ms)> wait_exit;     // true: A has exited
    std::function<bool()> stop;                // ends a hung A
    std::function<bool(int ms)> claim;         // the single instance, waiting up to ms for its release
};
enum class TakeOver { claimed, claimed_after_stop, not_claimed };
TakeOver TakeOverFromPreviousLauncher(const PreviousLauncherOps& ops, int wait_ms);
// signal / wait_exit / stop for the launcher with this pid. Only a process
// that created our handoff event is waited for or ended; claim stays empty.
PreviousLauncherOps PreviousLauncherWin32(std::uint32_t pid);

// Removes DiamondDogsTogether.exe.old left behind after a self-update; true
// when none is left.
bool CleanOldExecutable(const std::wstring& game_dir);

// mgs-coop\source.zip left by a package up to 0.14.3 (the source is now a
// separate release asset). It is deleted only when the current package.json
// does not list it and it verifies as our source bundle (its own
// source-files.json lists exactly its entries with matching SHA-256, and the
// project's CMakeLists.txt, THIRD_PARTY_NOTICES.md and launcher sources are in it).
enum class LeftoverSource { none, removed, kept_listed, kept_unverified, kept_locked };
LeftoverSource RemoveLeftoverSourceZip(const InstancePaths& paths);

}  // namespace ddt::core
