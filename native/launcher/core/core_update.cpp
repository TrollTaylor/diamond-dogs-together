// Diamond Dogs Together launcher: update checking, download and self-update.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include "core_update.hpp"

#include <cctype>
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

        // Find matching zip asset
        const json::Value* assets = rel->Find("assets");
        if (!assets || !assets->is_array()) continue;

        const json::Value* matching_asset = nullptr;
        for (const auto& a : assets->Elements()) {
            std::string aname = a.Str("name");
            if (aname.size() >= 4 && aname.substr(aname.size() - 4) == ".zip") {
                if (aname.find("DiamondDogsTogether-") == 0 || aname.find("mgs-coop-friend-") == 0) {
                    matching_asset = &a;
                    break;
                }
            }
        }
        if (!matching_asset) continue;

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

bool CheckForUpdates(const std::string& current_version, UpdateInfo& out, std::string& error) {
    std::string json_body;
    int status = 0;
    if (!WinHttpGet(kDefaultReleasesUrl, json_body, status, error)) {
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

bool ApplyUpdateZip(const InstancePaths& paths, const std::wstring& zip_path, std::string& error) {
    ZipReader reader;
    ZipLimits limits;
    limits.max_total_size = 512ull << 20;
    limits.max_entry_size = 256ull << 20;
    if (!reader.OpenFile(zip_path, limits, error)) {
        return false;
    }

    const std::wstring current_exe = JoinPath2(paths.game, L"DiamondDogsTogether.exe");
    const std::wstring old_exe = JoinPath2(paths.game, L"DiamondDogsTogether.exe.old");

    // Pre-flight check: ensure every path in the archive is safe to unpack
    for (const auto& entry : reader.entries()) {
        if (!IsSafeZipPath(entry.name)) {
            error = "Archive contains unsafe path: " + entry.name;
            return false;
        }
    }

    for (const auto& entry : reader.entries()) {
        std::wstring rel = Wide(entry.name);
        for (auto& c : rel) {
            if (c == L'/') c = L'\\';
        }
        const std::wstring target = JoinPath2(paths.game, rel);

        if (entry.is_directory) {
            CreateDirectories(target);
            continue;
        }

        CreateDirectories(ParentOf(target));

        std::vector<std::uint8_t> data;
        if (!reader.Extract(entry, data, error)) {
            return false;
        }

        // Special handling for the running launcher executable:
        // Rename current DiamondDogsTogether.exe to .old so Windows allows writing the new exe.
        const std::string lower_name = ToLowerStr(entry.name);
        if (lower_name == "diamonddogstogether.exe") {
            if (PathExists(old_exe)) {
                DeleteFileChecked(old_exe);
            }
            if (PathExists(current_exe)) {
                if (!MoveFileExW(current_exe.c_str(), old_exe.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                    error = "Could not move current executable to .old: " + std::to_string(GetLastError());
                    return false;
                }
            }
        }

        HANDLE hFile = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            error = "Could not create file: " + Utf8(target) + " (" + std::to_string(GetLastError()) + ")";
            return false;
        }

        DWORD written = 0;
        BOOL ok = WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
        CloseHandle(hFile);
        if (!ok || written != data.size()) {
            error = "Failed to write all data to: " + Utf8(target);
            return false;
        }
    }

    // Clean up temporary download archive
    DeleteFileChecked(zip_path);
    return true;
}

bool RelaunchSelf(const InstancePaths& paths, std::string& error) {
    if (GetUpdateHooks().relaunch) {
        return GetUpdateHooks().relaunch(paths, error);
    }

    const std::wstring exe = JoinPath2(paths.game, L"DiamondDogsTogether.exe");
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(exe.c_str(), nullptr, nullptr, nullptr, FALSE, 0, nullptr, paths.game.c_str(), &si, &pi)) {
        error = "Could not launch updated executable: " + std::to_string(GetLastError());
        return false;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    ExitProcess(0);
    return true;
}

void CleanOldExecutable(const std::wstring& game_dir) {
    const std::wstring old_exe = JoinPath2(game_dir, L"DiamondDogsTogether.exe.old");
    if (PathExists(old_exe)) {
        DeleteFileChecked(old_exe);
    }
}

}  // namespace ddt::core
