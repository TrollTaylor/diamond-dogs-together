// Diamond Dogs Together launcher core: saves (see core_saves.hpp).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core_saves.hpp"

#include <algorithm>
#include <cstring>
#include <map>

#include "core_text.hpp"
#include "launcher/zip.hpp"

namespace ddt::core {
namespace {

const std::vector<std::wstring> kVerifiedFolders = {L"287700/local", L"311340/remote"};
const std::vector<std::wstring> kProgressSubdirs = {L"287700\\local", L"remote"};
constexpr char kImportMarkerName[] = "imported-progress.json";
constexpr char kImportMarkerFormat[] = "mgs-coop-imported-progress";
constexpr char kShareFormat[] = "mgs-coop-save-share";

std::string RelUtf8(const std::wstring& rel) {
    std::string s = Utf8(rel);
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

void CollectFiles(const std::wstring& root, const std::wstring& rel, std::vector<std::wstring>& out) {
    const std::wstring dir = JoinPath2(root, rel);
    for (const DirEntry& e : ListDirectory(dir)) {
        const std::wstring child = rel + L"\\" + e.name;
        if (e.directory) {
            if (!e.reparse) CollectFiles(root, child, out);
        } else {
            out.push_back(child);
        }
    }
}

bool ReadJsonFile(const std::wstring& path, json::Value& out, std::uint64_t max = 1u << 20) {
    std::string text;
    if (!IsFile(path) || !ReadTextShared(path, text, max)) return false;
    return json::Parse(text, out);
}

bool IsTempOfProgress(const std::string& name) {
    // <progress>.mgs-coop-<32 hex>.tmp (the loader's atomic write in a stage).
    const std::size_t at = name.find(".mgs-coop-");
    if (at == std::string::npos) return false;
    const std::string base = name.substr(0, at), rest = name.substr(at + 10);
    return IsProgressFileName(base) && rest.size() == 36 && EndsWith(rest, ".tmp") && IsHexText(rest.substr(0, 32), 32);
}

int SlotIndex(const std::string& entry) {
    const auto& slots = HandoffSlots();
    for (std::size_t i = 0; i < slots.size(); ++i)
        if (slots[i] == entry) return static_cast<int>(i);
    return -1;
}

void SortBySlot(std::vector<StagedFile>& files) {
    std::stable_sort(files.begin(), files.end(), [](const StagedFile& a, const StagedFile& b) {
        const int sa = SlotIndex(a.entry), sb = SlotIndex(b.entry);
        if (sa != sb) return sa < sb;
        return a.entry < b.entry;
    });
}

std::string ImportMarkerText(const std::string& source, const std::string& exported_utc, const std::vector<StagedFile>& files,
                             std::int64_t now_ms) {
    json::Value m = json::Value::Object();
    m["format"] = kImportMarkerFormat;
    m["v"] = 1;
    m["source"] = source;
    m["imported_utc"] = FormatUtcSeconds(now_ms);
    m["exported_utc"] = exported_utc;
    json::Value rows = json::Value::Array();
    for (const StagedFile& f : files) {
        json::Value r = json::Value::Object();
        r["entry"] = f.entry;
        r["bytes"] = static_cast<std::int64_t>(f.bytes);
        r["sha256"] = UpperAscii(f.sha256);
        rows.Push(std::move(r));
    }
    m["files"] = std::move(rows);
    return json::Dump(m) + "\n";
}

}  // namespace

// ---- Hash manifests ------------------------------------------------------------
std::vector<ManifestRow> HashManifest(const std::wstring& root, const std::vector<std::wstring>& folders) {
    std::vector<ManifestRow> rows;
    for (const std::wstring& folder : folders) {
        std::wstring rel = folder;
        std::replace(rel.begin(), rel.end(), L'/', L'\\');
        if (!IsDir(JoinPath2(root, rel))) continue;
        std::vector<std::wstring> files;
        CollectFiles(root, rel, files);
        std::sort(files.begin(), files.end(), [](const std::wstring& a, const std::wstring& b) {
            return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        });
        for (const std::wstring& f : files) {
            ManifestRow row;
            row.path = RelUtf8(f);
            std::uint64_t size = 0;
            if (!Sha256FileHex(JoinPath2(root, f), row.sha256, &size)) continue;
            row.size = size;
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

ManifestComparison CompareManifest(const std::vector<ManifestRow>& expected, const std::vector<ManifestRow>& actual) {
    std::map<std::string, std::string> want, have;
    for (const auto& r : expected) want[r.path] = r.sha256;
    for (const auto& r : actual) have[r.path] = r.sha256;
    ManifestComparison c;
    for (const auto& kv : want) {
        auto it = have.find(kv.first);
        if (it == have.end()) c.missing.push_back(kv.first);
        else if (UpperAscii(it->second) != UpperAscii(kv.second)) c.changed.push_back(kv.first);
    }
    for (const auto& kv : have)
        if (!want.count(kv.first)) c.added.push_back(kv.first);
    c.same = c.changed.empty() && c.missing.empty() && c.added.empty();
    return c;
}

std::vector<ManifestRow> ManifestRowsFromJson(const json::Value& array) {
    std::vector<ManifestRow> rows;
    if (array.is_object()) {   // a single row (PowerShell writes one-element arrays as an object)
        ManifestRow r;
        r.path = array.Str("path");
        r.size = static_cast<std::uint64_t>(array.Int("size"));
        r.sha256 = UpperAscii(array.Str("sha256"));
        if (!r.path.empty()) rows.push_back(r);
        return rows;
    }
    for (const json::Value& v : array.Elements()) {
        ManifestRow r;
        r.path = v.Str("path");
        r.size = static_cast<std::uint64_t>(v.Int("size"));
        r.sha256 = UpperAscii(v.Str("sha256"));
        if (!r.path.empty()) rows.push_back(r);
    }
    return rows;
}

json::Value ManifestRowsToJson(const std::vector<ManifestRow>& rows) {
    json::Value a = json::Value::Array();
    for (const auto& r : rows) {
        json::Value o = json::Value::Object();
        o["path"] = r.path;
        o["size"] = static_cast<std::int64_t>(r.size);
        o["sha256"] = r.sha256;
        a.Push(std::move(o));
    }
    return a;
}

bool BackupRealSaves(Env& env, const std::wstring& userdata, const std::wstring& destination, std::size_t* files_out,
                     std::string& error, const std::function<void(std::size_t, std::size_t)>& progress) {
    const std::wstring root = FullPath(userdata);
    if (PathInside(destination, root)) {
        error = "The backup must be outside Steam's userdata folder.";
        return false;
    }
    if (PathExists(destination)) {
        error = "The backup folder already exists.";
        return false;
    }
    const std::vector<ManifestRow> verified = HashManifest(root, kVerifiedFolders);
    if (verified.empty()) {
        error = "No MGSV save files were found for your Steam account. Play MGSV once normally (so it makes a save), then try again.";
        return false;
    }
    const std::vector<ManifestRow> everything = HashManifest(root, {L"287700", L"311340"});
    if (!CreateDirectories(destination)) {
        error = "Could not create the backup folder: " + WinError(GetLastError());
        return false;
    }
    std::size_t done = 0;
    for (const ManifestRow& row : everything) {
        const std::wstring source = JoinPath2(root, Wide(row.path));
        const std::wstring target = JoinPath2(JoinPath2(destination, L"files"), Wide(row.path));
        if (!CreateDirectories(ParentOf(target))) {
            error = "Could not create a backup folder: " + WinError(GetLastError());
            return false;
        }
        std::string copy_error;
        if (!CopyFileChecked(source, target, false, copy_error)) {
            error = "Could not copy a save file to the backup: " + copy_error;
            return false;
        }
        std::string hex;
        if (!Sha256FileHex(target, hex) || hex != row.sha256) {
            error = "A save file changed while it was copied (is MGSV running?). Close MGSV and try again.";
            return false;
        }
        ++done;
        if (progress) progress(done, everything.size());
    }
    json::Value manifest = json::Value::Object();
    manifest["product"] = "mgs-coop-lab-save-backup";
    manifest["created_utc"] = FormatUtcRoundTrip(env.NowMs());
    manifest["source_root"] = Utf8(root);
    manifest["verified"] = ManifestRowsToJson(verified);
    manifest["all_files"] = ManifestRowsToJson(everything);
    manifest["note"] = "Real files were only read. verified = save files whose hashes are re-checked against the real saves.";
    // manifest.json last: a backup without it never counts (an interrupted run).
    if (!WriteAtomic(JoinPath2(destination, L"manifest.json"), json::Dump(manifest) + "\n", false, error)) {
        error = "Could not write the backup manifest: " + error;
        return false;
    }
    if (files_out) *files_out = everything.size();
    return true;
}

RealSaveCheck CheckRealSavesAgainstBackup(const std::wstring& backup_dir) {
    RealSaveCheck check;
    json::Value manifest;
    if (!ReadJsonFile(JoinPath2(backup_dir, L"manifest.json"), manifest, 64u << 20) || !manifest.is_object()) return check;
    check.source_root = manifest.Str("source_root");
    if (check.source_root.empty()) return check;
    const json::Value* verified = manifest.Find("verified");
    const std::vector<ManifestRow> expected = verified ? ManifestRowsFromJson(*verified) : std::vector<ManifestRow>{};
    const std::vector<ManifestRow> current = HashManifest(Wide(check.source_root), kVerifiedFolders);
    check.comparison = CompareManifest(expected, current);
    check.ok = true;
    return check;
}

Baselines SaveBaselines(const std::vector<std::wstring>& backups) {
    Baselines b;
    std::vector<std::wstring> seen;
    for (const std::wstring& dir : backups) {
        if (dir.empty()) continue;
        json::Value manifest;
        std::string root;
        if (ReadJsonFile(JoinPath2(dir, L"manifest.json"), manifest, 64u << 20)) root = manifest.Str("source_root");
        if (root.empty()) {
            b.missing.push_back(dir);
            continue;
        }
        std::wstring key = UpperInvariant(Wide(root));
        while (!key.empty() && (key.back() == L'\\' || key.back() == L'/')) key.pop_back();
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        b.backups.push_back(dir);
    }
    return b;
}

// ---- The co-op copy ------------------------------------------------------------
bool IsProgressFileName(const std::string& name) {
    auto prefix = [&](const char* p) {
        const std::size_t n = std::strlen(p);
        if (name.compare(0, n, p) != 0) return false;
        if (name.size() == n) return true;
        return name.size() == n + 1 && name[n] >= '0' && name[n] <= '9';
    };
    return prefix("TPP_GAME_DATA") || prefix("PERSONAL_DATA");
}

const std::vector<std::string>& HandoffSlots() {
    static const std::vector<std::string> kSlots = {"287700/local/TPP_GAME_DATA0", "287700/local/TPP_GAME_DATA1",
                                                    "287700/local/PERSONAL_DATA0", "287700/local/PERSONAL_DATA1",
                                                    "remote/TPP_GAME_DATA",        "remote/PERSONAL_DATA"};
    return kSlots;
}

bool CoopCopySeeded(const std::wstring& save_dir) {
    for (const DirEntry& e : ListDirectory(JoinPath2(save_dir, L"287700\\local")))
        if (!e.directory) return true;
    return false;
}

bool SeedCoopCopy(const std::wstring& backup_dir, const std::wstring& save_dir, bool reseed, const std::wstring& archive_dir,
                  SeedResult& result, std::string& error) {
    result = SeedResult{};
    const bool seeded = CoopCopySeeded(save_dir);
    if (seeded && !reseed) {
        result.reason = "already_seeded";
        return true;
    }
    // Build the new copy next to the old one, then swap.
    const std::wstring building = UniquePath(save_dir + L"-seed-" + Wide(RandomHex(4)), L"");
    std::size_t copied = 0;
    const std::pair<const wchar_t*, const wchar_t*> pairs[] = {{L"files\\287700\\local", L"287700\\local"},
                                                               {L"files\\311340\\remote", L"remote"}};
    for (const auto& pr : pairs) {
        const std::wstring target = JoinPath2(building, pr.second);
        if (!CreateDirectories(target)) {
            error = "Could not create the co-op save folder: " + WinError(GetLastError());
            RemoveTree(building);
            return false;
        }
        const std::wstring source = JoinPath2(backup_dir, pr.first);
        for (const DirEntry& e : ListDirectory(source)) {
            if (e.directory) continue;
            std::string copy_error;
            if (!CopyFileChecked(JoinPath2(source, e.name), JoinPath2(target, e.name), true, copy_error)) {
                error = "Could not copy a save into the co-op copy: " + copy_error;
                RemoveTree(building);
                return false;
            }
            ++copied;
        }
    }
    // The old copy (its local and remote folders, and anything else in it) moves aside.
    if (IsDir(save_dir)) {
        const std::wstring local = JoinPath2(save_dir, L"287700\\local"), remote = JoinPath2(save_dir, L"remote");
        if (seeded || IsDir(remote) || IsDir(local)) {
            if (!CreateDirectories(archive_dir)) {
                error = "Could not create " + Utf8(archive_dir);
                RemoveTree(building);
                return false;
            }
        }
        for (const auto& kv : {std::make_pair(local, std::wstring(L"local")), std::make_pair(remote, std::wstring(L"remote"))}) {
            if (!IsDir(kv.first)) continue;
            const std::wstring dest = JoinPath2(archive_dir, kv.second + L"-" + Wide(RandomHex(4)));
            if (!MovePath(kv.first, dest, error)) {
                error = "Could not move the previous co-op copy aside: " + error;
                RemoveTree(building);
                return false;
            }
        }
        // What remains of the old copy (the import marker, settings) goes along.
        const std::wstring marker = JoinPath2(save_dir, Wide(kImportMarkerName));
        if (IsFile(marker)) {
            CreateDirectories(archive_dir);
            std::string move_error;
            MovePath(marker, JoinPath2(archive_dir, Wide(kImportMarkerName)), move_error);
        }
        // Move the new folders into the existing copy (keeps other files there);
        // 287700\local last, so an interrupted run is seen as "not seeded".
        for (const auto& pr : {pairs[1], pairs[0]}) {
            const std::wstring from = JoinPath2(building, pr.second), to = JoinPath2(save_dir, pr.second);
            if (!CreateDirectories(ParentOf(to)) || !MovePath(from, to, error)) {
                error = "Could not put the new co-op copy in place: " + error;
                return false;
            }
        }
        RemoveTree(building);
    } else {
        if (!MovePath(building, save_dir, error)) {
            error = "Could not put the new co-op copy in place: " + error;
            RemoveTree(building);
            return false;
        }
    }
    result.seeded = true;
    result.reason = "copied";
    result.files = copied;
    return true;
}

ImportedInfo GetImportedSaveInfo(const std::wstring& save_dir, const std::wstring& replaced_root) {
    ImportedInfo info;
    json::Value marker;
    if (ReadJsonFile(JoinPath2(save_dir, Wide(kImportMarkerName)), marker) && marker.Str("format") == kImportMarkerFormat) {
        info.present = true;
        info.source = "marker";
        info.kind = marker.Str("source") == "handoff" ? "handoff" : "share-save";
        return info;
    }
    std::string newest;
    for (const DirEntry& e : ListDirectory(replaced_root)) {
        if (!e.directory) continue;
        const std::string name = Utf8(e.name);
        if (name.size() < 15) continue;
        bool stamp = true;
        for (std::size_t i = 0; i < 15; ++i) {
            const char c = name[i];
            if (i == 8 ? c != '-' : !(c >= '0' && c <= '9')) stamp = false;
        }
        if (!stamp) continue;
        if (newest.empty() || name.compare(newest) > 0) newest = name;
    }
    if (newest.empty()) return info;
    if (EndsWith(newest, "-before-import")) info.kind = "share-save";
    else if (EndsWith(newest, "-before-handoff")) info.kind = "handoff";
    else return info;
    info.present = true;
    info.source = "folder";
    return info;
}

StageCheck CheckProgressStage(const std::wstring& stage_root, const json::Value* manifest) {
    StageCheck r;
    if (!IsDir(stage_root)) {
        r.problem = "The save to import is missing.";
        return r;
    }
    const std::wstring root = FullPath(stage_root);
    if (HasReparsePoint(root, ParentOf(root))) {
        r.problem = "Filesystem links are not supported here.";
        return r;
    }
    for (const DirEntry& e : ListDirectory(root)) {
        const std::string name = Utf8(e.name);
        if (e.reparse) {
            r.problem = "Filesystem links are not supported here: " + name + ".";
            return r;
        }
        if (!e.directory && name == "handoff.json") continue;
        if (!e.directory || (name != "287700" && name != "remote")) {
            r.problem = "Unexpected file in the save: " + name + ".";
            return r;
        }
    }
    const std::wstring app = JoinPath2(root, L"287700");
    for (const DirEntry& e : ListDirectory(app)) {
        const std::string name = Utf8(e.name);
        if (e.reparse) {
            r.problem = "Filesystem links are not supported here: 287700/" + name + ".";
            return r;
        }
        if (!e.directory || name != "local") {
            r.problem = "Unexpected file in the save: 287700/" + name + ".";
            return r;
        }
    }
    std::vector<StagedFile> rows;
    for (const std::wstring& sub : kProgressSubdirs) {
        const std::wstring dir = JoinPath2(root, sub);
        if (!IsDir(dir)) continue;
        for (const DirEntry& e : ListDirectory(dir)) {
            const std::string entry = RelUtf8(sub) + "/" + Utf8(e.name);
            if (e.reparse) {
                r.problem = "Filesystem links are not supported here: " + entry + ".";
                return r;
            }
            if (e.directory || !IsProgressFileName(Utf8(e.name))) {
                r.problem = "Unexpected file in the save: " + entry + ".";
                return r;
            }
            if (e.size == 0 || e.size > kMaxProgressFileBytes) {
                r.problem = "Unexpected save file size: " + entry + ".";
                return r;
            }
            StagedFile f;
            f.entry = entry;
            f.path = JoinPath2(dir, e.name);
            if (!Sha256FileHex(f.path, f.sha256, &f.bytes)) {
                r.problem = "Could not read " + entry + ".";
                return r;
            }
            rows.push_back(std::move(f));
        }
    }
    SortBySlot(rows);
    bool has_game_data = false;
    for (const StagedFile& f : rows)
        if (f.entry == "287700/local/TPP_GAME_DATA0" || f.entry == "287700/local/TPP_GAME_DATA1") has_game_data = true;
    if (!has_game_data) {
        r.problem = "The save has no TPP_GAME_DATA file.";
        return r;
    }
    if (manifest) {
        std::map<std::string, const json::Value*> want;
        auto add = [&](const json::Value& m) -> bool {
            const std::string e = m.Str("entry");
            if (e.empty() || want.count(e)) {
                r.problem = "The save's file list is invalid (" + e + ").";
                return false;
            }
            want[e] = &m;
            return true;
        };
        if (manifest->is_object()) {
            if (!add(*manifest)) return r;
        } else {
            for (const json::Value& m : manifest->Elements())
                if (!add(m)) return r;
        }
        for (const StagedFile& f : rows) {
            auto it = want.find(f.entry);
            if (it == want.end()) {
                r.problem = "The save's file list does not name " + f.entry + ".";
                return r;
            }
            const json::Value* bytes = it->second->Find("bytes");
            if (bytes && !bytes->is_null() && static_cast<std::uint64_t>(bytes->AsInt(-1)) != f.bytes) {
                r.problem = f.entry + " has another size than its file list says.";
                return r;
            }
            if (UpperAscii(it->second->Str("sha256")) != f.sha256) {
                r.problem = f.entry + " is damaged (hash mismatch).";
                return r;
            }
        }
        for (const auto& kv : want) {
            bool found = false;
            for (const StagedFile& f : rows)
                if (f.entry == kv.first) found = true;
            if (!found) {
                r.problem = kv.first + " is missing from the save.";
                return r;
            }
        }
    }
    for (const StagedFile& f : rows) r.bytes += f.bytes;
    r.files = std::move(rows);
    r.ok = true;
    return r;
}

bool RemoveProgressStage(const std::wstring& stage_root) {
    if (!IsDir(stage_root)) return true;
    const std::wstring root = FullPath(stage_root);
    if (HasReparsePoint(root, ParentOf(root))) return false;
    for (const std::wstring& sub : kProgressSubdirs) {
        const std::wstring dir = JoinPath2(root, sub);
        if (!IsDir(dir) || HasReparsePoint(dir, root)) continue;
        for (const DirEntry& e : ListDirectory(dir)) {
            if (e.directory || e.reparse) continue;
            const std::string name = Utf8(e.name);
            if (IsProgressFileName(name) || IsTempOfProgress(name)) DeleteFileChecked(JoinPath2(dir, e.name));
        }
    }
    const std::wstring manifest = JoinPath2(root, L"handoff.json");
    if (IsFile(manifest)) DeleteFileChecked(manifest);
    for (const std::wstring& d : {JoinPath2(root, L"287700\\local"), JoinPath2(root, L"287700"), JoinPath2(root, L"remote"), root}) {
        if (!IsDir(d) || IsReparsePoint(d)) continue;
        if (ListDirectory(d).empty()) RemoveDirectoryW(d.c_str());
    }
    return !PathExists(root);
}

std::vector<ProgressBackup> ProgressBackups(const std::wstring& replaced_root) {
    struct Row {
        ProgressBackup b;
        std::string key;
        int n;
    };
    std::vector<Row> rows;
    for (const DirEntry& e : ListDirectory(replaced_root)) {
        if (!e.directory) continue;
        const std::string name = Utf8(e.name);
        // ^([0-9]{8})-([0-9]{6})(?:-([0-9]{1,4}))?-before-(import|handoff)$
        if (name.size() < 15) continue;
        bool ok = true;
        for (std::size_t i = 0; i < 15 && ok; ++i) ok = i == 8 ? name[i] == '-' : (name[i] >= '0' && name[i] <= '9');
        if (!ok) continue;
        std::string rest = name.substr(15);
        int n = 0;
        std::string kind;
        if (rest == "-before-import") kind = "import";
        else if (rest == "-before-handoff") kind = "handoff";
        else {
            if (rest.size() < 2 || rest[0] != '-') continue;
            std::size_t p = 1;
            while (p < rest.size() && p <= 4 && rest[p] >= '0' && rest[p] <= '9') {
                n = n * 10 + (rest[p] - '0');
                ++p;
            }
            if (p == 1) continue;
            const std::string tail = rest.substr(p);
            if (tail == "-before-import") kind = "import";
            else if (tail == "-before-handoff") kind = "handoff";
            else continue;
        }
        rows.push_back(Row{ProgressBackup{JoinPath2(replaced_root, e.name), name, kind}, name.substr(0, 8) + name.substr(9, 6), n});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (a.key != b.key) return a.key > b.key;
        return a.n > b.n;
    });
    std::vector<ProgressBackup> out;
    for (auto& r : rows) out.push_back(std::move(r.b));
    return out;
}

bool ImportProgressFiles(Env& env, const std::wstring& stage_root, const std::wstring& save_dir_in, const std::wstring& replaced_root_in,
                         const std::string& suffix, const std::string& source, const std::string& exported_utc,
                         const json::Value* manifest, bool keep_stage, std::wstring& aside_out, std::string& error) {
    aside_out.clear();
    if (source != "share-save" && source != "handoff") {
        error = "Unknown import source: " + source;
        return false;
    }
    if (suffix != "-before-import" && suffix != "-before-handoff") {
        error = "Unknown import suffix";
        return false;
    }
    const StageCheck check = CheckProgressStage(stage_root, manifest);
    if (!check.ok) {
        error = check.problem;
        return false;
    }
    const std::wstring save_dir = FullPath(save_dir_in), replaced_root = FullPath(replaced_root_in);
    const std::wstring parent = ParentOf(save_dir);
    if (!IsDir(save_dir)) {
        error = "The co-op save copy is missing.";
        return false;
    }
    if (HasReparsePoint(save_dir, parent) || (PathExists(replaced_root) && HasReparsePoint(replaced_root, ParentOf(replaced_root)))) {
        error = "Filesystem links are not supported here.";
        return false;
    }
    {
        std::function<bool(const std::wstring&)> walk = [&](const std::wstring& dir) {
            for (const DirEntry& e : ListDirectory(dir)) {
                if (e.reparse) return false;
                if (e.directory && !walk(JoinPath2(dir, e.name))) return false;
            }
            return true;
        };
        if (!walk(save_dir)) {
            error = "Filesystem links are not supported here.";
            return false;
        }
    }
    const std::string stamp = StampNow(env);
    const std::wstring new_dir = UniquePath(save_dir + L"-new-" + Wide(stamp), L"");
    bool ok = false;
    std::wstring aside;
    do {
        if (!CopyTree(save_dir, new_dir, error)) {
            error = "Could not copy the co-op save: " + error;
            break;
        }
        bool step_ok = true;
        for (const std::wstring& sub : kProgressSubdirs) {
            const std::wstring target = JoinPath2(new_dir, sub);
            if (!CreateDirectories(target)) {
                error = "Could not create " + RelUtf8(sub);
                step_ok = false;
                break;
            }
            // The old progress files go, so a slot the partner does not have cannot mix two players' progress.
            for (const DirEntry& e : ListDirectory(target))
                if (!e.directory && IsProgressFileName(Utf8(e.name))) DeleteFileChecked(JoinPath2(target, e.name));
        }
        if (!step_ok) break;
        for (const StagedFile& f : check.files) {
            const std::wstring dest = JoinPath2(new_dir, Wide(f.entry));
            std::string copy_error, hex;
            if (!CopyFileChecked(f.path, dest, false, copy_error) || !Sha256FileHex(dest, hex) || hex != f.sha256) {
                error = f.entry + " changed while it was copied.";
                step_ok = false;
                break;
            }
        }
        if (!step_ok) break;
        if (!WriteAtomic(JoinPath2(new_dir, Wide(kImportMarkerName)), ImportMarkerText(source, exported_utc, check.files, env.NowMs()),
                         true, error))
            break;
        if (!CreateDirectories(replaced_root)) {
            error = "Could not create the folder for the previous co-op copy.";
            break;
        }
        aside = UniquePath(JoinPath2(replaced_root, Wide(stamp)), Wide(suffix));
        if (!MovePath(save_dir, aside, error)) {
            error = "Could not move the current co-op copy aside (is MGSV running?): " + error;
            aside.clear();
            break;
        }
        std::string move_error;
        if (!MovePath(new_dir, save_dir, move_error)) {
            if (PathExists(save_dir)) {
                error = "The new co-op save copy could not be put in place and the co-op save folder exists again; your previous copy "
                        "is kept in mgs-coop\\coop-saves-replaced\\" + Utf8(LeafOf(aside)) + ".";
                break;
            }
            std::string back_error;
            if (!MovePath(aside, save_dir, back_error)) {
                error = "The new co-op save copy could not be put in place; your previous copy is kept in "
                        "mgs-coop\\coop-saves-replaced\\" + Utf8(LeafOf(aside)) + ".";
                break;
            }
            aside.clear();
            error = "The new co-op save copy could not be put in place: " + move_error;
            break;
        }
        ok = true;
    } while (false);
    if (PathExists(new_dir)) RemoveTree(new_dir);
    if (!ok) return false;
    if (!keep_stage) RemoveProgressStage(stage_root);
    aside_out = aside;
    return true;
}

// ---- Save handoff ----------------------------------------------------------------
bool HandoffSetSha256(const std::vector<StagedFile>& files, std::string& hex, std::string& error) {
    json::Value rows = json::Value::Array();
    for (const StagedFile& f : files) {
        json::Value r = json::Value::Object();
        r["entry"] = f.entry;
        r["bytes"] = static_cast<std::int64_t>(f.bytes);
        r["sha256"] = f.sha256;
        rows.Push(std::move(r));
    }
    return HandoffSetSha256Json(rows, hex, error);
}

bool HandoffSetSha256Json(const json::Value& rows, std::string& hex, std::string& error) {
    const auto& slots = HandoffSlots();
    std::map<int, std::pair<std::uint64_t, std::string>> by_slot;
    std::vector<const json::Value*> list;
    if (rows.is_object()) list.push_back(&rows);
    else
        for (const json::Value& v : rows.Elements()) list.push_back(&v);
    for (const json::Value* e : list) {
        const std::string name = e->Str("entry");
        int slot = -1;
        if (!name.empty()) slot = SlotIndex(name);
        else if (const json::Value* s = e->Find("slot"); s && !s->is_null()) slot = static_cast<int>(s->AsInt(-1));
        if (slot < 0 || slot >= static_cast<int>(slots.size())) {
            error = "Not a save handoff file: " + name;
            return false;
        }
        if (by_slot.count(slot)) {
            error = "Duplicate save handoff file: " + slots[static_cast<std::size_t>(slot)];
            return false;
        }
        const json::Value* size = e->Find("bytes");
        if (!size || size->is_null()) size = e->Find("size");
        const std::int64_t n = size && !size->is_null() ? size->AsInt(-1) : -1;
        if (n <= 0 || static_cast<std::uint64_t>(n) > kMaxProgressFileBytes) {
            error = "Invalid size of " + slots[static_cast<std::size_t>(slot)] + ".";
            return false;
        }
        const std::string sha = e->Str("sha256");
        if (!IsHexText(sha, 64)) {
            error = "Invalid SHA-256 of " + slots[static_cast<std::size_t>(slot)] + ".";
            return false;
        }
        by_slot[slot] = {static_cast<std::uint64_t>(n), sha};
    }
    if (by_slot.empty()) {
        error = "No save handoff file.";
        return false;
    }
    std::vector<std::uint8_t> buffer;
    for (const auto& kv : by_slot) {
        buffer.push_back(static_cast<std::uint8_t>(kv.first));
        for (int i = 0; i < 4; ++i) buffer.push_back(static_cast<std::uint8_t>((kv.second.first >> (8 * i)) & 0xFF));
        const std::string& h = kv.second.second;
        for (int i = 0; i < 32; ++i) buffer.push_back(static_cast<std::uint8_t>(std::stoul(h.substr(static_cast<std::size_t>(i) * 2, 2), nullptr, 16)));
    }
    hex = Sha256Hex(buffer.data(), buffer.size());
    return true;
}

HandoffRequest GetHandoffRequest(const std::wstring& handoff_dir, std::uint32_t expected_pid, std::int64_t expected_start_ms,
                                 std::int64_t session_started_ms, std::int64_t now_ms) {
    HandoffRequest r;
    r.request_path = JoinPath2(handoff_dir, L"request.json");
    if (!PathExists(r.request_path)) return r;
    r.status = "invalid";
    const std::wstring mod = ParentOf(FullPath(handoff_dir));
    if (HasReparsePoint(r.request_path, mod)) {
        r.problem = "Filesystem links are not supported here.";
        return r;
    }
    if (!IsFile(r.request_path) || FileSizeOrZero(r.request_path) > 65536) {
        r.problem = "the request is not a small file";
        return r;
    }
    json::Value req;
    if (!ReadJsonFile(r.request_path, req, 65536) || !req.is_object() || req.Str("format") != "mgs-coop-save-handoff-request" ||
        req.Str("v") != "1") {
        r.problem = "the request has an unknown format";
        return r;
    }
    const std::string xfer = req.Str("xfer"), set = req.Str("set_sha256"), mode = req.Str("mode"), rejoin = req.Str("rejoin", "0");
    if (!IsHexText(xfer, 8) || xfer == "00000000" || !IsHexText(set, 64) || (mode != "scene" && mode != "always") ||
        (rejoin != "0" && rejoin != "1")) {
        r.problem = "the request is malformed";
        return r;
    }
    const std::int64_t req_pid = req.Int("pid", 0);
    if (expected_pid == 0 || req_pid != static_cast<std::int64_t>(expected_pid)) {
        r.problem = "the request belongs to another game start";
        return r;
    }
    std::int64_t req_start = 0;
    if (!ParseUtc(req.Str("process_start_utc"), req_start) || expected_start_ms == 0 ||
        (req_start > expected_start_ms ? req_start - expected_start_ms : expected_start_ms - req_start) >= 2000) {
        r.problem = "the request belongs to another game start";
        return r;
    }
    std::int64_t created = 0;
    if (!ParseUtc(req.Str("created_utc"), created) || now_ms - created >= 30 * 60000ll || created - now_ms > 60000 ||
        (session_started_ms != 0 && created < session_started_ms - 2000)) {
        r.problem = "the request is too old";
        return r;
    }
    const std::wstring stage = JoinPath2(handoff_dir, Wide(xfer));
    if (!IsDir(stage)) {
        r.problem = "the staged save is missing";
        return r;
    }
    if (HasReparsePoint(stage, mod)) {
        r.problem = "Filesystem links are not supported here.";
        return r;
    }
    const std::wstring manifest_path = JoinPath2(stage, L"handoff.json");
    json::Value manifest;
    if (!IsFile(manifest_path) || FileSizeOrZero(manifest_path) > 65536 || !ReadJsonFile(manifest_path, manifest, 65536)) {
        r.problem = "the staged save has no file list";
        return r;
    }
    if (!manifest.is_object() || manifest.Str("format") != "mgs-coop-save-handoff" || manifest.Str("v") != "1" ||
        !IEqualsAscii(manifest.Str("xfer"), xfer) || !IEqualsAscii(manifest.Str("set_sha256"), set)) {
        r.problem = "the staged file list does not match the request";
        return r;
    }
    const json::Value* files = manifest.Find("files");
    const json::Value empty = json::Value::Array();
    const StageCheck check = CheckProgressStage(stage, files ? files : &empty);
    if (!check.ok) {
        r.problem = check.problem;
        return r;
    }
    if (check.bytes > kHandoffMaxTotalBytes) {
        r.problem = "the staged save is too large";
        return r;
    }
    std::string set_hex, set_error;
    if (!HandoffSetSha256(check.files, set_hex, set_error) || set_hex != UpperAscii(set)) {
        r.problem = "the staged files do not match the offered save";
        return r;
    }
    r.status = "ok";
    r.stage_dir = stage;
    r.xfer = UpperAscii(xfer);
    r.set_sha256 = UpperAscii(set);
    r.mode = mode;
    r.rejoin = rejoin == "1";
    r.created_utc = FormatUtcRoundTrip(created);
    r.files = check.files;
    r.bytes = check.bytes;
    r.host_mission = manifest.Int("host_mission", 0);
    return r;
}

// ---- Share save --------------------------------------------------------------------
namespace {
struct ExportFile {
    std::wstring path;
    std::string entry;
    Bytes data;
    std::string sha256;
};

void DosNow(Env& env, std::uint16_t& time, std::uint16_t& date) {
    const Civil c = env.LocalTime(env.NowMs());
    ToDosDateTime(c.year, c.month, c.day, c.hour, c.minute, c.second, time, date);
}
}  // namespace

bool ExportShareSave(Env& env, const std::wstring& save_dir, const std::wstring& out_dir, std::wstring& zip_out, std::size_t& files_out,
                     std::string& error) {
    zip_out.clear();
    files_out = 0;
    std::vector<ExportFile> files;
    for (const std::wstring& sub : kProgressSubdirs) {
        const std::wstring dir = JoinPath2(save_dir, sub);
        for (const DirEntry& e : ListDirectory(dir)) {
            if (e.directory || !IsProgressFileName(Utf8(e.name))) continue;
            if (e.size == 0 || e.size > kMaxProgressFileBytes) {
                error = "A co-op save file has an unexpected size (" + Utf8(e.name) + ").";
                return false;
            }
            ExportFile f;
            f.path = JoinPath2(dir, e.name);
            f.entry = RelUtf8(sub) + "/" + Utf8(e.name);
            std::string read_error;
            if (!ReadFileShared(f.path, f.data, kMaxProgressFileBytes, read_error)) {
                error = "Could not read the co-op save: " + read_error;
                return false;
            }
            f.sha256 = Sha256Hex(f.data.data(), f.data.size());
            files.push_back(std::move(f));
        }
    }
    bool has_game = false;
    for (const ExportFile& f : files)
        if (StartsWith(f.entry, "287700/local/TPP_GAME_DATA")) has_game = true;
    if (!has_game) {
        error = "The co-op save copy has no game progress yet. Play once with Diamond Dogs Together, then export again.";
        return false;
    }
    if (!IsDir(out_dir)) {
        error = "Your Desktop folder was not found.";
        return false;
    }
    json::Value manifest = json::Value::Object();
    manifest["format"] = kShareFormat;
    manifest["v"] = 1;
    manifest["exported_utc"] = FormatUtcRoundTrip(env.NowMs());
    json::Value rows = json::Value::Array();
    ZipWriter writer;
    std::uint16_t dos_time = 0, dos_date = 0;
    DosNow(env, dos_time, dos_date);
    writer.SetTimestamp(dos_time, dos_date);
    for (const ExportFile& f : files) {
        json::Value r = json::Value::Object();
        r["entry"] = f.entry;
        r["bytes"] = static_cast<std::int64_t>(f.data.size());
        r["sha256"] = f.sha256;
        rows.Push(std::move(r));
        if (!writer.AddFile(f.entry, f.data, error)) return false;
    }
    manifest["files"] = std::move(rows);
    const std::string manifest_text = json::Dump(manifest);
    Bytes zip;
    if (!writer.AddFile("share-save.json", manifest_text.data(), manifest_text.size(), error) || !writer.Finish(zip, error)) return false;
    // Verify the bytes before they reach the Desktop.
    ZipReader check;
    ZipLimits limits;
    if (!check.Open(zip, limits, error) || check.entries().size() != files.size() + 1 || !check.Find("share-save.json")) {
        error = "The written zip is incomplete.";
        return false;
    }
    for (const ExportFile& f : files) {
        const ZipEntry* e = check.Find(f.entry);
        Bytes data;
        if (!e || !check.Extract(*e, data, error) || Sha256Hex(data.data(), data.size()) != f.sha256) {
            error = "The written zip is damaged. Export again.";
            return false;
        }
    }
    const std::wstring target = UniquePath(JoinPath2(out_dir, L"mgs-coop-save-" + Wide(StampNow(env))), L".zip");
    if (!WriteAtomic(target, zip.data(), zip.size(), false, error)) {
        error = "Could not save the zip on your Desktop: " + error;
        return false;
    }
    // Re-read from disk: only a verified export stays.
    ZipReader disk;
    bool verified = disk.OpenFile(target, limits, error) && disk.entries().size() == files.size() + 1;
    for (const ExportFile& f : files) {
        if (!verified) break;
        const ZipEntry* e = disk.Find(f.entry);
        Bytes data;
        verified = e && disk.Extract(*e, data, error) && Sha256Hex(data.data(), data.size()) == f.sha256;
    }
    if (!verified) {
        DeleteFileChecked(target);
        error = "The written zip is damaged. Export again.";
        return false;
    }
    zip_out = target;
    files_out = files.size();
    return true;
}

bool ImportShareSave(Env& env, const std::wstring& zip_path, const std::wstring& mod_dir, const std::wstring& save_dir,
                     const std::wstring& replaced_root, std::wstring& aside_out, std::size_t& files_out, std::string& error) {
    aside_out.clear();
    files_out = 0;
    if (!IsFile(zip_path)) {
        error = "That file was not found. Nothing was changed.";
        return false;
    }
    if (FileSizeOrZero(zip_path) > (32ull << 20)) {
        error = "That file is too large to be a co-op save. Nothing was changed.";
        return false;
    }
    ZipLimits limits;
    limits.max_entries = 64;
    limits.max_entry_size = 1ull << 20 << 3;
    limits.max_total_size = 64ull << 20;
    limits.max_archive_size = 32ull << 20;
    ZipReader zip;
    std::string zip_error;
    if (!zip.OpenFile(zip_path, limits, zip_error)) {
        error = "That file is not a co-op save zip (" + zip_error + "). Nothing was changed.";
        return false;
    }
    const ZipEntry* manifest_entry = zip.Find("share-save.json");
    if (!manifest_entry) {
        error = "That zip was not made by Share save (share-save.json is missing). Nothing was changed.";
        return false;
    }
    Bytes manifest_bytes;
    json::Value manifest;
    if (!zip.Extract(*manifest_entry, manifest_bytes, zip_error) ||
        !json::Parse(std::string(manifest_bytes.begin(), manifest_bytes.end()), manifest) || manifest.Str("format") != kShareFormat) {
        error = "That zip is not a Share save export. Nothing was changed.";
        return false;
    }
    std::map<std::string, const json::Value*> want;
    if (const json::Value* files = manifest.Find("files")) {
        if (files->is_object()) want[files->Str("entry")] = files;
        else
            for (const json::Value& m : files->Elements()) want[m.Str("entry")] = &m;
    }
    const std::wstring stage = UniquePath(JoinPath2(mod_dir, L"save-import-" + Wide(StampNow(env))), L"");
    json::Value taken = json::Value::Array();
    bool ok = false;
    do {
        if (!CreateDirectories(stage)) {
            error = "Could not create a folder for the import. Nothing was changed.";
            break;
        }
        bool step_ok = true;
        std::size_t count = 0;
        for (const ZipEntry& e : zip.entries()) {
            if (e.name == "share-save.json" || e.is_directory) continue;
            const std::size_t slash = e.name.rfind('/');
            const std::string sub = slash == std::string::npos ? std::string() : e.name.substr(0, slash);
            const std::string name = slash == std::string::npos ? e.name : e.name.substr(slash + 1);
            if ((sub != "287700/local" && sub != "remote") || !IsProgressFileName(name)) {
                error = "Unexpected file in the zip: " + e.name + ". Nothing was changed.";
                step_ok = false;
                break;
            }
            if (e.uncompressed_size == 0 || e.uncompressed_size > kMaxProgressFileBytes) {
                error = "Unexpected file size in the zip: " + e.name + ". Nothing was changed.";
                step_ok = false;
                break;
            }
            auto it = want.find(e.name);
            if (it == want.end()) {
                error = "The zip's file list does not name " + e.name + ". Nothing was changed.";
                step_ok = false;
                break;
            }
            Bytes data;
            if (!zip.Extract(e, data, zip_error) || Sha256Hex(data.data(), data.size()) != UpperAscii(it->second->Str("sha256"))) {
                error = e.name + " is damaged (hash mismatch). Ask your partner to export again. Nothing was changed.";
                step_ok = false;
                break;
            }
            const std::wstring dest = JoinPath2(stage, Wide(e.name));
            std::string write_error;
            if (!CreateDirectories(ParentOf(dest)) || !WriteAtomic(dest, data.data(), data.size(), false, write_error)) {
                error = "Could not unpack the zip: " + write_error + ". Nothing was changed.";
                step_ok = false;
                break;
            }
            json::Value row = json::Value::Object();
            row["entry"] = e.name;
            row["bytes"] = static_cast<std::int64_t>(data.size());
            row["sha256"] = Sha256Hex(data.data(), data.size());
            taken.Push(std::move(row));
            ++count;
        }
        if (!step_ok) break;
        if (count != want.size()) {
            error = "The zip is incomplete (its file list names files it does not contain). Nothing was changed.";
            break;
        }
        if (!IsFile(JoinPath2(stage, L"287700\\local\\TPP_GAME_DATA0")) && !IsFile(JoinPath2(stage, L"287700\\local\\TPP_GAME_DATA1"))) {
            error = "The zip has no TPP_GAME_DATA file. Nothing was changed.";
            break;
        }
        std::string exported = manifest.Str("exported_utc");
        std::int64_t t = 0;
        if (ParseUtc(exported, t)) exported = FormatUtcRoundTrip(t);
        std::string import_error;
        if (!ImportProgressFiles(env, stage, save_dir, replaced_root, "-before-import", "share-save", exported, &taken, false, aside_out,
                                 import_error)) {
            error = import_error.find("is kept in") != std::string::npos ? import_error : import_error + " Nothing was changed.";
            break;
        }
        files_out = count;
        ok = true;
    } while (false);
    if (PathExists(stage)) RemoveTree(stage);
    return ok;
}

bool RestoreShareSave(Env& env, const std::wstring& save_dir, const std::wstring& replaced_root, std::wstring& restored_from,
                      std::wstring& aside_out, std::string& error) {
    restored_from.clear();
    aside_out.clear();
    const std::vector<ProgressBackup> backups = ProgressBackups(replaced_root);
    if (backups.empty()) {
        error = "There is no earlier co-op save to bring back (none was replaced by an import or a save handoff).";
        return false;
    }
    const ProgressBackup& pick = backups.front();
    if (HasReparsePoint(pick.path, ParentOf(replaced_root))) {
        error = "Filesystem links are not supported here.";
        return false;
    }
    const std::wstring aside = UniquePath(JoinPath2(replaced_root, Wide(StampNow(env))), L"-before-restore");
    if (IsDir(save_dir) && !MovePath(save_dir, aside, error)) {
        error = "Could not move the current co-op copy aside: " + error;
        return false;
    }
    if (!MovePath(pick.path, save_dir, error)) {
        if (!PathExists(save_dir) && PathExists(aside)) {
            std::string back;
            MovePath(aside, save_dir, back);
        }
        error = "Could not bring the earlier co-op save back: " + error;
        return false;
    }
    restored_from = pick.path;
    aside_out = PathExists(aside) ? aside : std::wstring();
    return true;
}

std::string SaveListing(const std::wstring& mod_dir, std::size_t max_entries) {
    std::string out = "# MGS Co-op save folders: names, sizes and times only (no save contents)\r\n# path\tbytes|dir\tlast_write_utc\r\n";
    if (!IsDir(mod_dir)) return out;
    std::vector<std::pair<std::wstring, std::wstring>> roots;   // (full, relative)
    for (const wchar_t* name : {L"coop-saves", L"coop-saves-replaced", L"coop-saves-handoff", L"local-test-saves"}) {
        const std::wstring dir = JoinPath2(mod_dir, name);
        if (IsDir(dir)) roots.emplace_back(dir, name);
    }
    for (const DirEntry& e : ListDirectory(mod_dir)) {
        if (!e.directory) continue;
        const std::string n = Utf8(e.name);
        if (StartsWith(n, "save-import-") || StartsWith(n, "coop-saves-new-")) roots.emplace_back(JoinPath2(mod_dir, e.name), e.name);
    }
    std::size_t rows = 0, more = 0;
    std::function<void(const std::wstring&, const std::wstring&, bool, std::uint64_t, std::int64_t, bool)> visit =
        [&](const std::wstring& full, const std::wstring& rel, bool dir, std::uint64_t size, std::int64_t ms, bool reparse) {
            if (rows >= max_entries) ++more;
            else {
                out += RelUtf8(rel) + "\t" + (dir ? std::string("dir") : Decimal(size)) + "\t" + FormatUtcSeconds(ms) + "\r\n";
                ++rows;
            }
            if (dir && !reparse)
                for (const DirEntry& c : ListDirectory(full))
                    visit(JoinPath2(full, c.name), rel + L"/" + c.name, c.directory, c.size, c.write_ms, c.reparse);
        };
    for (const auto& r : roots) {
        WIN32_FILE_ATTRIBUTE_DATA d{};
        std::int64_t ms = 0;
        if (GetFileAttributesExW(r.first.c_str(), GetFileExInfoStandard, &d)) {
            ULARGE_INTEGER u{};
            u.LowPart = d.ftLastWriteTime.dwLowDateTime;
            u.HighPart = d.ftLastWriteTime.dwHighDateTime;
            ms = static_cast<std::int64_t>(u.QuadPart / 10000ull) - 11644473600000ll;
        }
        visit(r.first, r.second, true, 0, ms, IsReparsePoint(r.first));
    }
    if (more) out += "# truncated: " + Decimal(more) + " more\r\n";
    return out;
}

}  // namespace ddt::core
