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
// release object (GET /repos/.../releases/latest).
bool ParseReleasesFeed(const std::string& json_text, const std::string& current_version, UpdateInfo& out, std::string& error);

// Test seams for network requests and process restart
struct UpdateHooks {
    std::function<bool(const std::string& url, std::string& body_out, int& status_code, std::string& error)> http_get;
    std::function<bool(const std::string& url, const std::wstring& dest_path,
                       const std::function<void(const UpdateProgress&)>& on_progress,
                       const std::atomic<bool>* cancel, std::string& error)> http_download;
    std::function<bool(const InstancePaths& paths, std::string& error)> relaunch;
};
UpdateHooks& GetUpdateHooks();

// WinHttp network operations
bool WinHttpGet(const std::string& url, std::string& body_out, int& status_code, std::string& error);
bool WinHttpDownload(const std::string& url, const std::wstring& dest_path,
                     const std::function<void(const UpdateProgress&)>& on_progress,
                     const std::atomic<bool>* cancel, std::string& error);

// High-level update operations
bool CheckForUpdates(const std::string& current_version, UpdateInfo& out, std::string& error);
bool DownloadUpdate(const UpdateInfo& info, const std::wstring& dest_zip,
                    const std::function<void(const UpdateProgress&)>& on_progress,
                    const std::atomic<bool>* cancel, std::string& error);
bool ApplyUpdateZip(const InstancePaths& paths, const std::wstring& zip_path, std::string& error);
bool RelaunchSelf(const InstancePaths& paths, std::string& error);

// Removes DiamondDogsTogether.exe.old left behind after a self-update
void CleanOldExecutable(const std::wstring& game_dir);

}  // namespace ddt::core
