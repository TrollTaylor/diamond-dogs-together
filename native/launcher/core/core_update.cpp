// Diamond Dogs Together launcher: update checking, download and self-update.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include "core_update.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <vector>

#include "core_json.hpp"
#include "core_paths.hpp"
#include "core_util.hpp"
#include "launcher/zip.hpp"

namespace ddt::core {

namespace {

constexpr char kDefaultReleasesUrl[] = "https://api.github.com/repos/TrollTaylor/diamond-dogs-together/releases?per_page=5";

std::string ToLowerStr(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The player zip of a release: DiamondDogsTogether-<version>.zip (or
// mgs-coop-friend-<version>.zip before 0.14.0), <version> being exactly the tag's.
bool IsPlayerZipFor(const std::string& asset, const std::string& version) {
    for (const char* prefix : {"DiamondDogsTogether-", "mgs-coop-friend-"}) {
        const std::string p = prefix;
        if (asset.size() > p.size() + 4 && asset.compare(0, p.size(), p) == 0 && ToLowerStr(asset.substr(asset.size() - 4)) == ".zip")
            return asset.substr(p.size(), asset.size() - p.size() - 4) == version;
    }
    return false;
}

}  // namespace

UpdateHooks& GetUpdateHooks() {
    static UpdateHooks hooks;
    return hooks;
}

bool ParseSemVer(const std::string& str, int& major, int& minor, int& patch) {
    major = minor = patch = 0;
    std::size_t i = 0;
    while (i < str.size() && (str[i] == 'v' || str[i] == 'V' || str[i] == ' ')) ++i;
    if (i >= str.size()) return false;

    // Parse major
    if (!std::isdigit(static_cast<unsigned char>(str[i]))) return false;
    while (i < str.size() && std::isdigit(static_cast<unsigned char>(str[i]))) {
        major = major * 10 + (str[i] - '0');
        ++i;
    }
    if (i >= str.size() || str[i] != '.') return false;
    ++i;

    // Parse minor
    if (i >= str.size() || !std::isdigit(static_cast<unsigned char>(str[i]))) return false;
    while (i < str.size() && std::isdigit(static_cast<unsigned char>(str[i]))) {
        minor = minor * 10 + (str[i] - '0');
        ++i;
    }
    if (i >= str.size() || str[i] != '.') return false;
    ++i;

    // Parse patch
    if (i >= str.size() || !std::isdigit(static_cast<unsigned char>(str[i]))) return false;
    while (i < str.size() && std::isdigit(static_cast<unsigned char>(str[i]))) {
        patch = patch * 10 + (str[i] - '0');
        ++i;
    }
    return true;
}

bool IsVersionNewer(const std::string& candidate, const std::string& current) {
    int c_maj = 0, c_min = 0, c_pat = 0;
    int cur_maj = 0, cur_min = 0, cur_pat = 0;
    if (!ParseSemVer(candidate, c_maj, c_min, c_pat)) return false;
    if (!ParseSemVer(current, cur_maj, cur_min, cur_pat)) return false;
    if (c_maj != cur_maj) return c_maj > cur_maj;
    if (c_min != cur_min) return c_min > cur_min;
    return c_pat > cur_pat;
}

bool ParseReleasesFeed(const std::string& json_text, const std::string& current_version, UpdateInfo& out, std::string& error) {
    json::Value root;
    if (!json::Parse(json_text, root, &error)) {
        return false;
    }

    std::vector<const json::Value*> releases;
    if (root.is_array()) {
        for (const auto& el : root.Elements()) {
            if (el.is_object()) releases.push_back(&el);
        }
    } else if (root.is_object()) {
        releases.push_back(&root);
    } else {
        error = "Expected JSON array or object in release feed";
        return false;
    }

    out.current_version = current_version;
    if (releases.empty()) {
        out.status = UpdateStatus::up_to_date;
        return true;
    }

    int best_maj = 0, best_min = 0, best_pat = 0;
    const json::Value* best_release = nullptr;
    const json::Value* best_asset = nullptr;

    for (const auto* rel : releases) {
        if (rel->Bool("draft", false)) continue;
        std::string tag = rel->Str("tag_name");
        int maj = 0, min = 0, pat = 0;
        if (!ParseSemVer(tag, maj, min, pat)) continue;

        // The player zip of exactly this version, and its GPL source zip
        // (DDT-source-<version>.zip) published in the same release: a release
        // without its source is never offered (GPLv3 section 6(d)).
        const json::Value* assets = rel->Find("assets");
        if (!assets || !assets->is_array()) continue;
        const std::string version = std::to_string(maj) + "." + std::to_string(min) + "." + std::to_string(pat);
        const std::string source_name = ToLowerStr("DDT-source-" + version + ".zip");

        const json::Value* matching_asset = nullptr;
        bool has_source = false;
        for (const auto& a : assets->Elements()) {
            const std::string aname = a.Str("name");
            if (ToLowerStr(aname) == source_name) has_source = true;
            else if (!matching_asset && IsPlayerZipFor(aname, version)) matching_asset = &a;
        }
        if (!matching_asset || !has_source) continue;

        bool is_better = false;
        if (!best_release) {
            is_better = true;
        } else {
            if (maj != best_maj) is_better = (maj > best_maj);
            else if (min != best_min) is_better = (min > best_min);
            else is_better = (pat > best_pat);
        }

        if (is_better) {
            best_maj = maj;
            best_min = min;
            best_pat = pat;
            best_release = rel;
            best_asset = matching_asset;
        }
    }

    if (!best_release || !best_asset) {
        out.status = UpdateStatus::up_to_date;
        return true;
    }

    std::string best_ver_str = std::to_string(best_maj) + "." + std::to_string(best_min) + "." + std::to_string(best_pat);
    out.latest_version = best_release->Str("tag_name");
    if (out.latest_version.empty()) out.latest_version = best_ver_str;
    out.release_title = best_release->Str("name", "Diamond Dogs Together " + best_ver_str);
    out.release_notes = best_release->Str("body");
    out.html_url = best_release->Str("html_url");
    out.asset_name = best_asset->Str("name");
    out.download_url = best_asset->Str("browser_download_url");
    out.download_size = static_cast<std::uint64_t>(best_asset->Int("size", 0));

    if (IsVersionNewer(best_ver_str, current_version)) {
        out.status = UpdateStatus::available;
    } else {
        out.status = UpdateStatus::up_to_date;
    }
    return true;
}

namespace {

bool WinHttpExecuteGet(const std::string& url, int redirect_depth,
                       std::function<bool(HINTERNET hRequest, DWORD status_code, std::string& err)> on_response,
                       std::string& error) {
    if (redirect_depth > 5) {
        error = "Too many HTTP redirects";
        return false;
    }

    std::wstring wurl = Wide(url);
    URL_COMPONENTS urlComp{};
    urlComp.dwStructSize = sizeof(urlComp);
    urlComp.dwHostNameLength = static_cast<DWORD>(-1);
    urlComp.dwUrlPathLength = static_cast<DWORD>(-1);
    urlComp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &urlComp)) {
        error = "Failed to parse URL: " + url;
        return false;
    }

    std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
    std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
    if (urlComp.dwExtraInfoLength > 0) {
        path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
    }

    HINTERNET hSession = WinHttpOpen(L"DiamondDogsTogether-Launcher/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        error = "WinHttpOpen failed: " + std::to_string(GetLastError());
        return false;
    }

    WinHttpSetTimeouts(hSession, 10000, 10000, 15000, 30000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
    if (!hConnect) {
        error = "WinHttpConnect failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD req_flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags);
    if (!hRequest) {
        error = "WinHttpOpenRequest failed: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));

    std::wstring headers = L"User-Agent: DiamondDogsTogether-Launcher\r\nAccept: application/vnd.github.v3+json, application/octet-stream, */*\r\n";
    if (!WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(headers.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        error = "Failed to send HTTP request: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        error = "Failed to receive HTTP response: " + std::to_string(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    DWORD status_code = 0;
    DWORD status_size = sizeof(status_code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

    // Follow manual redirect if returned
    if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
        DWORD loc_size = 0;
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &loc_size, WINHTTP_NO_HEADER_INDEX);
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            std::wstring loc(loc_size / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc.data(), &loc_size, WINHTTP_NO_HEADER_INDEX)) {
                while (!loc.empty() && loc.back() == L'\0') loc.pop_back();
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                return WinHttpExecuteGet(Utf8(loc), redirect_depth + 1, on_response, error);
            }
        }
    }

    bool result = on_response(hRequest, status_code, error);

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

}  // namespace

bool WinHttpGet(const std::string& url, std::string& body_out, int& status_code_out, std::string& error) {
    if (GetUpdateHooks().http_get) {
        return GetUpdateHooks().http_get(url, body_out, status_code_out, error);
    }

    body_out.clear();
    status_code_out = 0;

    return WinHttpExecuteGet(url, 0, [&](HINTERNET hRequest, DWORD status_code, std::string& err) {
        status_code_out = static_cast<int>(status_code);
        if (status_code != 200) {
            err = "Server returned HTTP status " + std::to_string(status_code);
            return false;
        }

        std::vector<char> buffer(8192);
        DWORD bytes_read = 0;
        while (WinHttpReadData(hRequest, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read) && bytes_read > 0) {
            body_out.append(buffer.data(), bytes_read);
        }
        return true;
    }, error);
}

bool WinHttpDownload(const std::string& url, const std::wstring& dest_path,
                     const std::function<void(const UpdateProgress&)>& on_progress,
                     const std::atomic<bool>* cancel, std::string& error) {
    if (GetUpdateHooks().http_download) {
        return GetUpdateHooks().http_download(url, dest_path, on_progress, cancel, error);
    }

    CreateDirectories(ParentOf(dest_path));

    return WinHttpExecuteGet(url, 0, [&](HINTERNET hRequest, DWORD status_code, std::string& err) {
        if (status_code != 200) {
            err = "Server returned HTTP " + std::to_string(status_code) + " when downloading package";
            return false;
        }

        wchar_t cl_buf[64] = {};
        DWORD cl_len = sizeof(cl_buf);
        std::uint64_t total_bytes = 0;
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                                cl_buf, &cl_len, WINHTTP_NO_HEADER_INDEX)) {
            total_bytes = _wcstoui64(cl_buf, nullptr, 10);
        }

        HANDLE hFile = CreateFileW(dest_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            err = "Could not create destination file: " + Utf8(dest_path);
            return false;
        }

        std::vector<char> buffer(65536);
        DWORD bytes_read = 0;
        std::uint64_t bytes_downloaded = 0;
        bool ok = true;

        while (WinHttpReadData(hRequest, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read) && bytes_read > 0) {
            if (cancel && cancel->load()) {
                ok = false;
                err = "Download cancelled";
                break;
            }
            DWORD written = 0;
            if (!WriteFile(hFile, buffer.data(), bytes_read, &written, nullptr) || written != bytes_read) {
                ok = false;
                err = "Failed writing to destination file: " + Utf8(dest_path);
                break;
            }
            bytes_downloaded += bytes_read;
            if (on_progress) {
                float progress = total_bytes > 0 ? static_cast<float>(bytes_downloaded) / static_cast<float>(total_bytes) : 0.0f;
                on_progress({bytes_downloaded, total_bytes, progress});
            }
        }

        CloseHandle(hFile);
        if (!ok) {
            DeleteFileW(dest_path.c_str());
            return false;
        }
        return true;
    }, error);
}

bool CheckForUpdates(const std::string& current_version, UpdateInfo& out, std::string& error, const std::string& feed_url) {
    std::string json_body;
    int status = 0;
    if (!WinHttpGet(feed_url.empty() ? std::string(kDefaultReleasesUrl) : feed_url, json_body, status, error)) {
        return false;
    }
    return ParseReleasesFeed(json_body, current_version, out, error);
}

bool DownloadUpdate(const UpdateInfo& info, const std::wstring& dest_zip,
                    const std::function<void(const UpdateProgress&)>& on_progress,
                    const std::atomic<bool>* cancel, std::string& error) {
    if (info.download_url.empty()) {
        error = "No download URL available";
        return false;
    }
    return WinHttpDownload(info.download_url, dest_zip, on_progress, cancel, error);
}

namespace {

constexpr char kOurProduct[] = "mgs-coop-local-package";   // core_install.hpp kPackageProduct
constexpr wchar_t kLauncherExe[] = L"DiamondDogsTogether.exe";

std::wstring TargetFor(const std::wstring& game, const std::string& zip_name) {
    std::wstring rel = Wide(zip_name);
    for (auto& c : rel)
        if (c == L'/') c = L'\\';
    return JoinPath2(game, rel);
}

bool WriteWholeFile(const std::wstring& path, const std::vector<std::uint8_t>& data, std::string& error) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = WinError(GetLastError());
        return false;
    }
    DWORD written = 0;
    const BOOL ok = data.empty() || WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    const DWORD write_error = ok ? 0 : GetLastError();
    const BOOL flushed = FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok || (!data.empty() && written != data.size()) || !flushed) {
        error = write_error ? WinError(write_error) : std::string("the disk did not take all of it");
        DeleteFileW(path.c_str());
        return false;
    }
    return true;
}

// package.json of the folder: its files[] (target, upper-case SHA-256) when it is ours.
bool PackageRows(const std::wstring& package_json, std::vector<std::pair<std::string, std::string>>& rows) {
    rows.clear();
    std::string text;
    json::Value v;
    if (!IsFile(package_json) || !ReadTextShared(package_json, text, 1u << 20) || !json::Parse(text, v) || !v.is_object() ||
        v.Str("product") != kOurProduct)
        return false;
    const json::Value* files = v.Find("files");
    if (files && files->is_array())
        for (const json::Value& row : files->Elements())
            if (row.is_object()) rows.emplace_back(row.Str("target"), UpperAscii(row.Str("sha256")));
    return true;
}

// A link (junction, symlink) between `top` (excluded) and `path` (included).
bool LinkBelow(const std::wstring& path, const std::wstring& top) {
    const std::wstring stop = FullPath(top);
    for (std::wstring cursor = FullPath(path); !cursor.empty() && !IEquals(cursor, stop) && PathInside(cursor, stop); cursor = ParentOf(cursor)) {
        if (IsReparsePoint(cursor)) return true;
        if (ParentOf(cursor) == cursor) break;
    }
    return false;
}

struct StagedFile {
    std::string name;   // the zip entry
    std::wstring target, staged, backup;
    bool moved_aside = false;   // the old target is at `backup`
    bool committed = false;     // the new file is at `target`
};

// An antivirus or the search indexer may hold a freshly written file for a
// moment: such a move or delete is retried for about two seconds.
bool Transient(DWORD code) { return code == ERROR_SHARING_VIOLATION || code == ERROR_ACCESS_DENIED || code == ERROR_LOCK_VIOLATION; }
constexpr int kBusyRetries = 20;
constexpr DWORD kBusyRetryMs = 100;

bool MoveRetrying(const std::wstring& from, const std::wstring& to, DWORD* code_out = nullptr) {
    for (int attempt = 0;; ++attempt) {
        if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        const DWORD code = GetLastError();
        if (code_out) *code_out = code;
        if (!Transient(code) || attempt >= kBusyRetries) return false;
        Sleep(kBusyRetryMs);
    }
}

bool DeleteRetrying(const std::wstring& path) {
    for (int attempt = 0;; ++attempt) {
        if (DeleteFileChecked(path) || !PathExists(path)) return true;
        if (attempt >= kBusyRetries) return false;
        Sleep(kBusyRetryMs);
    }
}

}  // namespace

std::size_t CleanUpdateLeftovers(const InstancePaths& paths) {
    std::size_t handled = 0;
    for (const std::wstring& dir : {paths.game, paths.mod}) {
        if (!IsDir(dir) || LinkBelow(dir, paths.game)) continue;
        for (const DirEntry& e : ListDirectory(dir)) {
            if (e.directory) continue;
            const std::wstring path = JoinPath2(dir, e.name);
            const std::string lower = LowerAscii(Utf8(e.name));
            const bool is_new = EndsWith(lower, ".ddt-new"), is_old = EndsWith(lower, ".ddt-old");
            if (!is_new && !is_old) continue;
            const std::wstring target = path.substr(0, path.size() - 8);
            if (is_new || PathExists(target)) {
                if (DeleteFileChecked(path)) ++handled;
            } else if (MoveFileExW(path.c_str(), target.c_str(), 0)) {
                ++handled;   // the old file was moved aside and its new one never came: back it goes
            }
        }
    }
    return handled;
}

bool ApplyUpdateZip(const InstancePaths& paths, const std::wstring& zip_path, std::string& error, ApplyReport* report,
                    const std::function<bool(std::string& error)>& start_new) {
    ApplyReport local;
    ApplyReport& out = report ? *report : local;
    out = ApplyReport{};
    auto failed = [&](ApplyFailure why, const std::string& detail) {
        out.failure = why;
        error = detail;
        return false;
    };
    ZipReader reader;
    ZipLimits limits;
    limits.max_total_size = 512ull << 20;
    limits.max_entry_size = 256ull << 20;
    std::string zip_error;
    if (!reader.OpenFile(zip_path, limits, zip_error)) return failed(ApplyFailure::damaged, "The downloaded file is damaged (" + zip_error + ")");
    for (const auto& entry : reader.entries()) {
        if (!IsSafeZipPath(entry.name))
            return failed(ApplyFailure::damaged, "The downloaded file holds an unsafe path (" + entry.name + ")");
    }
    // Only a Diamond Dogs Together player package is installed: never the
    // source archive or any other zip.
    bool ours = false;
    if (const ZipEntry* pkg = reader.Find("mgs-coop/package.json"); pkg && !pkg->is_directory) {
        std::vector<std::uint8_t> bytes;
        json::Value v;
        ours = reader.Extract(*pkg, bytes, zip_error) &&
               json::Parse(std::string(bytes.begin(), bytes.end()), v) && v.is_object() && v.Str("product") == kOurProduct;
    }
    if (!ours) return failed(ApplyFailure::not_ours, "The downloaded file is not a Diamond Dogs Together package");
    // What the previous package shipped (its files[]), read before it is replaced.
    std::vector<std::pair<std::string, std::string>> previous;
    PackageRows(paths.package_json, previous);

    // 1. Stage: every new file next to its target. Nothing is replaced yet.
    std::vector<StagedFile> files;
    auto discard_staged = [&]() {
        for (const StagedFile& f : files)
            if (!f.committed) DeleteFileW(f.staged.c_str());
    };
    for (const auto& entry : reader.entries()) {
        const std::wstring target = TargetFor(paths.game, entry.name);
        if (LinkBelow(target, paths.game)) {
            discard_staged();
            return failed(ApplyFailure::install_failed, entry.name + " in the game folder is a link; that is not supported");
        }
        if (entry.is_directory) {
            CreateDirectories(target);
            continue;
        }
        CreateDirectories(ParentOf(target));
        std::vector<std::uint8_t> data;
        if (!reader.Extract(entry, data, zip_error)) {
            discard_staged();
            return failed(ApplyFailure::damaged, "The downloaded file is damaged (" + zip_error + ")");
        }
        StagedFile f;
        f.name = entry.name;
        f.target = target;
        f.staged = target + L".ddt-new";
        std::string write_error;
        if (!WriteWholeFile(f.staged, data, write_error)) {
            discard_staged();
            return failed(ApplyFailure::install_failed, "Could not write " + entry.name + " (" + write_error + ")");
        }
        files.push_back(f);
    }

    // Files the previous package shipped under mgs-coop/ that this one does
    // not, removed only while they are exactly as shipped.
    std::vector<StagedFile> stale;
    {
        std::vector<std::string> now;
        for (const auto& entry : reader.entries()) now.push_back(LowerAscii(entry.name));
        for (const auto& row : previous) {
            const std::string lower = LowerAscii(row.first);
            if (!StartsWith(lower, "mgs-coop/") || !IsSafeZipPath(row.first) || lower == "mgs-coop/package.json") continue;
            if (std::find(now.begin(), now.end(), lower) != now.end()) continue;
            const std::wstring path = TargetFor(paths.game, row.first);
            std::string hex;
            if (!IsHexText(row.second, 64) || !IsFile(path) || LinkBelow(path, paths.game) || !Sha256FileHex(path, hex) || hex != row.second)
                continue;
            StagedFile s;
            s.name = row.first;
            s.target = path;
            s.backup = path + L".ddt-old";
            stale.push_back(s);
        }
    }

    // 2. Commit: move each old file aside and the new one in; undo all on any failure.
    const std::wstring current_exe = JoinPath2(paths.game, kLauncherExe);
    const std::wstring old_exe = current_exe + L".old";
    // Moves every old file back (the running exe from .old too); false when one stays out.
    auto roll_back = [&]() {
        bool all = true;
        for (auto it = stale.rbegin(); it != stale.rend(); ++it) {
            if (it->moved_aside && !MoveRetrying(it->backup, it->target)) all = false;
            it->moved_aside = false;
        }
        for (auto it = files.rbegin(); it != files.rend(); ++it) {
            if (it->moved_aside) {
                if (!MoveRetrying(it->backup, it->target)) all = false;   // replaces the new file
            } else if (it->committed && !DeleteRetrying(it->target)) {
                all = false;
            }
            it->committed = it->moved_aside = false;
        }
        discard_staged();
        return all;
    };
    if (PathExists(old_exe) && !DeleteRetrying(old_exe)) {
        discard_staged();
        return failed(ApplyFailure::old_exe_in_use, "DiamondDogsTogether.exe.old from an earlier update is still in use");
    }
    for (StagedFile& f : files) {
        f.backup = IEquals(f.target, current_exe) ? old_exe : f.target + L".ddt-old";
        DWORD code = 0;
        if (PathExists(f.target)) {
            if (!IEquals(f.backup, old_exe) && PathExists(f.backup)) DeleteFileChecked(f.backup);
            if (!MoveRetrying(f.target, f.backup, &code)) {
                out.restored = roll_back();
                return failed(ApplyFailure::install_failed, "Could not replace " + f.name + " (" + WinError(code) + ")");
            }
            f.moved_aside = true;
        }
        if (!MoveRetrying(f.staged, f.target, &code)) {
            out.restored = roll_back();
            return failed(ApplyFailure::install_failed, "Could not put " + f.name + " in place (" + WinError(code) + ")");
        }
        f.committed = true;
    }
    for (StagedFile& s : stale) {
        if (PathExists(s.backup)) DeleteFileChecked(s.backup);
        s.moved_aside = MoveRetrying(s.target, s.backup);   // one that stays is only left over, never a failure
    }
    out.files = files.size();

    // 3. The restart into the new launcher, while the old files can still come back.
    if (start_new) {
        std::string start_error;
        if (!start_new(start_error)) {
            out.restored = roll_back();
            return failed(ApplyFailure::restart_failed, start_error);
        }
    }

    // 4. The old files are no longer needed; the running exe's .old goes when the new exe starts.
    for (const StagedFile& f : files)
        if (f.moved_aside && !IEquals(f.backup, old_exe)) DeleteFileChecked(f.backup);
    for (const StagedFile& s : stale)
        if (s.moved_aside && DeleteFileChecked(s.backup)) out.removed.push_back(s.name);
    DeleteFileChecked(zip_path);
    return true;
}

// ---- Restart after an update -----------------------------------------------------------
std::wstring UpdateHandoffEventName(std::uint32_t pid) {
    return L"Local\\DiamondDogsTogether-update-" + std::to_wstring(pid);
}

std::wstring QuoteArgument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out = L"\"";
    for (auto it = arg.begin();; ++it) {
        std::size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') {
            ++it;
            ++backslashes;
        }
        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        }
        if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
        } else {
            out.append(backslashes, L'\\');
        }
        out.push_back(*it);
    }
    out.push_back(L'"');
    return out;
}

bool StartSuccessor(const std::wstring& exe, const std::vector<std::wstring>& args, const std::wstring& dir, int ready_wait_ms,
                    SuccessorStart& out, std::string& error) {
    out = SuccessorStart{};
    // Kept for the life of this process: the new exe opens it by name. Auto-reset.
    static const HANDLE ready = CreateEventW(nullptr, FALSE, FALSE, UpdateHandoffEventName(GetCurrentProcessId()).c_str());
    if (!ready) {
        error = "Windows could not prepare the restart: " + WinError(GetLastError());
        return false;
    }
    ResetEvent(ready);
    std::wstring command = QuoteArgument(exe);
    for (const std::wstring& a : args) command += L" " + QuoteArgument(a);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.empty() ? nullptr : dir.c_str(), &si, &pi)) {
        error = "Windows could not start it: " + WinError(GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    out.pid = pi.dwProcessId;
    const HANDLE waits[2] = {ready, pi.hProcess};
    const DWORD r = WaitForMultipleObjects(2, waits, FALSE, static_cast<DWORD>(ready_wait_ms < 0 ? 0 : ready_wait_ms));
    bool ok = true;
    if (r == WAIT_OBJECT_0) {
        out.signalled = true;
    } else if (r == WAIT_OBJECT_0 + 1) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        error = "it closed right after it started (exit code " + std::to_string(code) + ")";
        ok = false;
    }   // WAIT_TIMEOUT: it still runs and takes over once this process has exited.
    CloseHandle(pi.hProcess);
    return ok;
}

bool RelaunchSelf(const InstancePaths& paths, const std::vector<std::wstring>& args, std::string& error) {
    const std::wstring exe = JoinPath2(paths.game, kLauncherExe);
    if (GetUpdateHooks().relaunch) return GetUpdateHooks().relaunch(exe, args, error);
    SuccessorStart started;
    return StartSuccessor(exe, args, paths.game, kSuccessorReadyWaitMs, started, error);
}

TakeOver TakeOverFromPreviousLauncher(const PreviousLauncherOps& ops, int wait_ms) {
    bool stopped = false;
    if (ops.signal && ops.signal() && ops.wait_exit && !ops.wait_exit(wait_ms)) {
        // The old launcher handed over but does not close: end it, it has nothing left to do.
        if (ops.stop) ops.stop();
        stopped = true;
        ops.wait_exit(kStopPreviousWaitMs);
    }
    if (!ops.claim || !ops.claim(kClaimAfterHandoffMs)) return TakeOver::not_claimed;
    return stopped ? TakeOver::claimed_after_stop : TakeOver::claimed;
}

PreviousLauncherOps PreviousLauncherWin32(std::uint32_t pid) {
    struct Handles {
        HANDLE event = nullptr, process = nullptr;
        ~Handles() {
            if (event) CloseHandle(event);
            if (process) CloseHandle(process);
        }
    };
    auto h = std::make_shared<Handles>();
    PreviousLauncherOps ops;
    ops.signal = [h, pid]() {
        if (!pid || pid == GetCurrentProcessId()) return false;
        // Only a launcher that is waiting for us created this event.
        h->event = OpenEventW(EVENT_MODIFY_STATE, FALSE, UpdateHandoffEventName(pid).c_str());
        if (!h->event) return false;
        // Opened before the signal, so the id cannot be reused while we wait.
        h->process = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
        SetEvent(h->event);
        return h->process != nullptr;
    };
    ops.wait_exit = [h](int ms) {
        return !h->process || WaitForSingleObject(h->process, static_cast<DWORD>(ms < 0 ? 0 : ms)) == WAIT_OBJECT_0;
    };
    ops.stop = [h]() { return h->process && TerminateProcess(h->process, 1) != FALSE; };
    return ops;
}

bool CleanOldExecutable(const std::wstring& game_dir) {
    const std::wstring old_exe = JoinPath2(game_dir, L"DiamondDogsTogether.exe.old");
    if (PathExists(old_exe)) DeleteFileChecked(old_exe);
    return !PathExists(old_exe);
}

bool RecentUpdateTimes(std::int64_t now_ms, std::int64_t exe_written_ms, std::int64_t old_changed_ms) {
    // A clock that is a minute off (or a file time from another PC's zip) still counts.
    auto recent = [now_ms](std::int64_t t) { return t > 0 && now_ms - t < kRecentUpdateMs && t - now_ms < 60000; };
    return recent(exe_written_ms) || recent(old_changed_ms);
}

namespace {
// ms since 1970 (UTC) of a FILETIME-style 100 ns count since 1601; 0 for none.
std::int64_t UnixMsOf(std::int64_t filetime) { return filetime > 0 ? filetime / 10000 - 11644473600000ll : 0; }

// LastWriteTime and ChangeTime (NTFS sets it on a rename) of a file, 0 when unknown.
bool FileTimes(const std::wstring& path, std::int64_t& written_ms, std::int64_t& changed_ms) {
    written_ms = changed_ms = 0;
    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    FILE_BASIC_INFO info{};
    const bool ok = GetFileInformationByHandleEx(h, FileBasicInfo, &info, sizeof(info)) != FALSE;
    CloseHandle(h);
    if (!ok) return false;
    written_ms = UnixMsOf(info.LastWriteTime.QuadPart);
    changed_ms = UnixMsOf(info.ChangeTime.QuadPart);
    return true;
}
}  // namespace

bool OldExecutableFromRecentUpdate(const std::wstring& game_dir) {
    const std::wstring exe = JoinPath2(game_dir, kLauncherExe);
    const std::wstring old_exe = exe + L".old";
    if (!IsFile(old_exe)) return false;
    std::int64_t exe_written = 0, exe_changed = 0, old_written = 0, old_changed = 0;
    FileTimes(exe, exe_written, exe_changed);
    FileTimes(old_exe, old_written, old_changed);
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER now{};
    now.LowPart = ft.dwLowDateTime;
    now.HighPart = ft.dwHighDateTime;
    // Every updater writes the new exe (LastWriteTime) and renames the running one to .old (ChangeTime).
    return RecentUpdateTimes(UnixMsOf(static_cast<std::int64_t>(now.QuadPart)), exe_written, old_changed);
}

LeftoverSource RemoveLeftoverSourceZip(const InstancePaths& paths) {
    const std::wstring zip = JoinPath2(paths.mod, L"source.zip");
    if (!IsFile(zip)) return LeftoverSource::none;
    if (LinkBelow(zip, paths.game)) return LeftoverSource::kept_unverified;
    std::vector<std::pair<std::string, std::string>> rows;
    if (!PackageRows(paths.package_json, rows)) return LeftoverSource::kept_unverified;   // cannot tell: keep
    for (const auto& row : rows)
        if (LowerAscii(row.first) == "mgs-coop/source.zip") return LeftoverSource::kept_listed;
    // Our source bundle: source-files.json lists exactly its other entries, each with its SHA-256.
    ZipReader reader;
    ZipLimits limits;
    limits.max_entries = 65536;
    limits.max_archive_size = 512ull << 20;
    std::string error;
    if (!reader.OpenFile(zip, limits, error)) return LeftoverSource::kept_unverified;
    const ZipEntry* list = reader.Find("source-files.json");
    std::vector<std::uint8_t> bytes;
    json::Value listed;
    if (!list || !reader.Extract(*list, bytes, error) || !json::Parse(std::string(bytes.begin(), bytes.end()), listed) || !listed.is_array())
        return LeftoverSource::kept_unverified;
    std::vector<std::pair<std::string, std::string>> want;
    for (const json::Value& row : listed.Elements()) {
        if (!row.is_object()) return LeftoverSource::kept_unverified;
        want.emplace_back(row.Str("path"), UpperAscii(row.Str("sha256")));
    }
    std::size_t files = 0;
    bool cmake = false, notices = false, launcher = false;
    for (const ZipEntry& entry : reader.entries()) {
        if (entry.is_directory) return LeftoverSource::kept_unverified;
        if (entry.name == "source-files.json") continue;
        ++files;
        auto it = std::find_if(want.begin(), want.end(), [&](const auto& w) { return w.first == entry.name; });
        if (it == want.end() || !reader.Extract(entry, bytes, error) || Sha256Hex(bytes.data(), bytes.size()) != it->second)
            return LeftoverSource::kept_unverified;
        cmake = cmake || entry.name == "CMakeLists.txt";
        notices = notices || entry.name == "THIRD_PARTY_NOTICES.md";
        launcher = launcher || StartsWith(entry.name, "native/launcher/");
    }
    if (files != want.size() || !cmake || !notices || !launcher) return LeftoverSource::kept_unverified;
    return DeleteFileChecked(zip) ? LeftoverSource::removed : LeftoverSource::kept_locked;
}

}  // namespace ddt::core
