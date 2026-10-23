// Diamond Dogs Together launcher core: saves. The real-save backup (read-only
// from Steam\userdata), the co-op copy and its seeding, progress imports
// (Share save and the automatic save handoff), the handoff request check and
// the Share save zip. Ports of tools/CoopPlay.psm1 and
// tools/friend/FriendCommon.psm1 / Share-Save.ps1. Nothing here writes below
// Steam\userdata.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core_env.hpp"
#include "core_json.hpp"

namespace ddt::core {

// ---- Hash manifests ------------------------------------------------------------
struct ManifestRow {
    std::string path;     // relative, '/' separators
    std::uint64_t size = 0;
    std::string sha256;   // upper case
};
// Every file below root\<folder> for each folder ('/' relative), read-only.
std::vector<ManifestRow> HashManifest(const std::wstring& root, const std::vector<std::wstring>& folders);
struct ManifestComparison {
    bool same = true;
    std::vector<std::string> changed, missing, added;
};
ManifestComparison CompareManifest(const std::vector<ManifestRow>& expected, const std::vector<ManifestRow>& actual);
std::vector<ManifestRow> ManifestRowsFromJson(const json::Value& array);
json::Value ManifestRowsToJson(const std::vector<ManifestRow>& rows);

// Backup-RealSaves: copies userdata\287700 and \311340 to destination\files\...,
// checks each copy's hash and writes manifest.json (product
// mgs-coop-lab-save-backup, verified = 287700/local and 311340/remote).
// progress(done, total) may be empty. The destination must not exist and must
// be outside userdata.
bool BackupRealSaves(Env& env, const std::wstring& userdata, const std::wstring& destination, std::size_t* files_out,
                     std::string& error, const std::function<void(std::size_t, std::size_t)>& progress = {});
struct RealSaveCheck {
    bool ok = false;           // the backup could be read
    std::string source_root;
    ManifestComparison comparison;
};
RealSaveCheck CheckRealSavesAgainstBackup(const std::wstring& backup_dir);
// The first backup of each Steam account among `backups` (install order).
struct Baselines {
    std::vector<std::wstring> backups;
    std::vector<std::wstring> missing;
};
Baselines SaveBaselines(const std::vector<std::wstring>& backups);

// ---- The co-op copy ------------------------------------------------------------
inline constexpr std::uint64_t kMaxProgressFileBytes = 4ull << 20;
inline constexpr std::uint64_t kHandoffMaxTotalBytes = 8ull << 20;
bool IsProgressFileName(const std::string& name);   // TPP_GAME_DATA[0-9]? | PERSONAL_DATA[0-9]?
const std::vector<std::string>& HandoffSlots();
// The copy holds at least one file in 287700\local.
bool CoopCopySeeded(const std::wstring& save_dir);
// Copy-SaveSeed, made idempotent: the new copy is built in a sibling folder
// and renamed in, so an interrupted seed never leaves a half copy. An existing
// copy is moved to archive_dir (local-<8hex>, remote-<8hex>) when reseeding.
struct SeedResult {
    bool seeded = false;
    std::string reason;   // copied | already_seeded
    std::size_t files = 0;
};
bool SeedCoopCopy(const std::wstring& backup_dir, const std::wstring& save_dir, bool reseed, const std::wstring& archive_dir,
                  SeedResult& result, std::string& error);
// Get-FriendImportedSaveInfo: the copy holds a partner's progress (a valid
// imported-progress.json, or the newest coop-saves-replaced folder ends with
// -before-import / -before-handoff).
struct ImportedInfo {
    bool present = false;
    std::string kind;   // share-save | handoff
    std::string source;   // marker | folder
};
ImportedInfo GetImportedSaveInfo(const std::wstring& save_dir, const std::wstring& replaced_root);

struct StagedFile {
    std::string entry;   // 287700/local/TPP_GAME_DATA0 ...
    std::uint64_t bytes = 0;
    std::string sha256;  // upper case
    std::wstring path;
};
struct StageCheck {
    bool ok = false;
    std::string problem;
    std::vector<StagedFile> files;   // slot order
    std::uint64_t bytes = 0;
};
// Test-CoopProgressStage. manifest: rows with entry, bytes, sha256 (or null).
StageCheck CheckProgressStage(const std::wstring& stage_root, const json::Value* manifest);
// Remove-CoopProgressStage: deletes known names only; true when the folder is gone.
bool RemoveProgressStage(const std::wstring& stage_root);
// Get-CoopProgressBackups: <replaced>\<yyyyMMdd-HHmmss>[-n]-before-import|handoff, newest first.
struct ProgressBackup {
    std::wstring path;
    std::string name;
    std::string kind;   // import | handoff
};
std::vector<ProgressBackup> ProgressBackups(const std::wstring& replaced_root);
// Import-CoopProgressFiles. suffix: -before-import | -before-handoff; source:
// share-save | handoff. aside_out: the folder the old copy moved to.
bool ImportProgressFiles(Env& env, const std::wstring& stage_root, const std::wstring& save_dir, const std::wstring& replaced_root,
                         const std::string& suffix, const std::string& source, const std::string& exported_utc,
                         const json::Value* manifest, bool keep_stage, std::wstring& aside_out, std::string& error);

// ---- Save handoff ----------------------------------------------------------------
// Get-CoopHandoffSetSha256 over rows with entry (or slot), bytes (or size) and sha256.
bool HandoffSetSha256(const std::vector<StagedFile>& files, std::string& hex, std::string& error);
bool HandoffSetSha256Json(const json::Value& rows, std::string& hex, std::string& error);
struct HandoffRequest {
    std::string status = "none";   // none | ok | invalid
    std::string problem;
    std::wstring request_path, stage_dir;
    std::string xfer, set_sha256, mode, created_utc;
    bool rejoin = false;
    std::vector<StagedFile> files;
    std::uint64_t bytes = 0;
    std::int64_t host_mission = 0;
};
// Get-CoopHandoffRequest: request.json for the game that exited (pid and
// start within 2 s), created after the session start (2 s slack) and under
// 30 minutes old; the stage must match handoff.json and the set hash.
HandoffRequest GetHandoffRequest(const std::wstring& handoff_dir, std::uint32_t expected_pid, std::int64_t expected_start_ms,
                                 std::int64_t session_started_ms, std::int64_t now_ms);

// ---- Share save --------------------------------------------------------------------
bool ExportShareSave(Env& env, const std::wstring& save_dir, const std::wstring& out_dir, std::wstring& zip_out,
                     std::size_t& files_out, std::string& error);
bool ImportShareSave(Env& env, const std::wstring& zip_path, const std::wstring& mod_dir, const std::wstring& save_dir,
                     const std::wstring& replaced_root, std::wstring& aside_out, std::size_t& files_out, std::string& error);
bool RestoreShareSave(Env& env, const std::wstring& save_dir, const std::wstring& replaced_root, std::wstring& restored_from,
                      std::wstring& aside_out, std::string& error);

// Get-FriendSaveListing for Collect logs (names, sizes, times only).
std::string SaveListing(const std::wstring& mod_dir, std::size_t max_entries = 500);

}  // namespace ddt::core
