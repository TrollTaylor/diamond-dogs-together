// Diamond Dogs Together launcher core: the Win32 implementation of Env.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include "core_env.hpp"

#include <cstdio>

namespace ddt::core {
namespace {

std::int64_t FileTimeMs(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>(u.QuadPart / 10000ull) - 11644473600000ll;
}

FILETIME MsFileTime(std::int64_t ms) {
    ULARGE_INTEGER u{};
    u.QuadPart = static_cast<std::uint64_t>(ms + 11644473600000ll) * 10000ull;
    FILETIME ft{};
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    return ft;
}

std::wstring ExePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

class ClipboardLock {
public:
    ClipboardLock() {
        for (int i = 0; i < 10 && !open_; ++i) {
            open_ = OpenClipboard(nullptr) != 0;
            if (!open_) Sleep(20);
        }
    }
    ~ClipboardLock() {
        if (open_) CloseClipboard();
    }
    bool open() const { return open_; }

private:
    bool open_ = false;
};

class Win32Env final : public Env {
public:
    std::int64_t NowMs() override {
        FILETIME ft{};
        GetSystemTimePreciseAsFileTime(&ft);
        return FileTimeMs(ft);
    }
    void SleepMs(std::uint32_t ms) override { Sleep(ms); }
    Civil LocalTime(std::int64_t utc_ms) override {
        const FILETIME ft = MsFileTime(utc_ms);
        SYSTEMTIME utc{}, local{};
        Civil c;
        if (FileTimeToSystemTime(&ft, &utc) && SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local)) {
            c.year = local.wYear;
            c.month = local.wMonth;
            c.day = local.wDay;
            c.hour = local.wHour;
            c.minute = local.wMinute;
            c.second = local.wSecond;
            c.millisecond = local.wMilliseconds;
            return c;
        }
        return CivilFromMs(utc_ms);
    }

    bool RegDword(bool hklm, const std::wstring& key, const std::wstring& name, std::uint32_t& out) override {
        DWORD value = 0, size = sizeof(value);
        if (RegGetValueW(hklm ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, key.c_str(), name.c_str(), RRF_RT_REG_DWORD, nullptr,
                         &value, &size) != ERROR_SUCCESS)
            return false;
        out = value;
        return true;
    }
    bool RegString(bool hklm, const std::wstring& key, const std::wstring& name, std::wstring& out) override {
        DWORD size = 0;
        HKEY root = hklm ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        if (RegGetValueW(root, key.c_str(), name.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &size) !=
                ERROR_SUCCESS ||
            size == 0)
            return false;
        std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
        size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
        if (RegGetValueW(root, key.c_str(), name.c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, value.data(), &size) !=
            ERROR_SUCCESS)
            return false;
        value.resize(wcsnlen(value.c_str(), value.size()));
        out = value;
        return true;
    }

    std::vector<ProcessRow> GameProcesses() override {
        std::vector<ProcessRow> rows;
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap == INVALID_HANDLE_VALUE) return rows;
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
            if (_wcsicmp(pe.szExeFile, L"mgsvtpp.exe") != 0 && _wcsicmp(pe.szExeFile, L"mgsvmgo.exe") != 0) continue;
            ProcessRow row;
            row.pid = pe.th32ProcessID;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (h) {
                wchar_t buffer[1024];
                DWORD n = 1024;
                if (QueryFullProcessImageNameW(h, 0, buffer, &n)) row.path.assign(buffer, n);
                CloseHandle(h);
            }
            row.unknown = row.path.empty();
            rows.push_back(row);
        }
        CloseHandle(snap);
        return rows;
    }
    bool ProcessStartMs(std::uint32_t pid, std::int64_t& start_ms) override {
        if (pid == 0) return false;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return false;
        FILETIME created{}, exited{}, kernel{}, user{};
        DWORD code = 0;
        const bool ok = GetProcessTimes(h, &created, &exited, &kernel, &user) && GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        if (ok) start_ms = FileTimeMs(created);
        return ok;
    }
    bool ProcessAlive(std::uint32_t pid) override {
        if (pid == 0) return false;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return GetLastError() == ERROR_ACCESS_DENIED;   // exists, but not ours to open
        DWORD code = 0;
        const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        return alive;
    }
    std::uint32_t CurrentPid() override { return GetCurrentProcessId(); }
    std::int64_t CurrentStartMs() override {
        FILETIME created{}, exited{}, kernel{}, user{};
        return GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) ? FileTimeMs(created) : 0;
    }

    bool OpenUrl(const std::wstring& url) override {
        const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const HINSTANCE r = ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (SUCCEEDED(init)) CoUninitialize();
        return reinterpret_cast<INT_PTR>(r) > 32;
    }
    bool StartWatchdog(const std::wstring& game_dir, const std::string& session_id, std::uint32_t& pid_out) override {
        pid_out = 0;
        const std::wstring exe = ExePath();
        if (exe.empty()) return false;
        // The session id is 32 hex digits (no secret): the only argument.
        for (char c : session_id)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        std::wstring cmd = L"\"" + exe + L"\" --watch " + Wide(session_id);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        const DWORD base = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW;
        BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, base | CREATE_BREAKAWAY_FROM_JOB, nullptr,
                                 game_dir.c_str(), &si, &pi);
        if (!ok) ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, base, nullptr, game_dir.c_str(), &si, &pi);
        if (!ok) return false;
        pid_out = pi.dwProcessId;
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    bool SetClipboardText(const std::string& text) override {
        const std::wstring w = Wide(text);
        ClipboardLock lock;
        if (!lock.open() || !EmptyClipboard()) return false;
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
        if (!mem) return false;
        void* p = GlobalLock(mem);
        if (!p) {
            GlobalFree(mem);
            return false;
        }
        memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        if (!SetClipboardData(CF_UNICODETEXT, mem)) {
            GlobalFree(mem);
            return false;
        }
        return true;
    }
    bool GetClipboardText(std::string& text) override {
        text.clear();
        if (!IsClipboardFormatAvailable(CF_UNICODETEXT)) return false;
        ClipboardLock lock;
        if (!lock.open()) return false;
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (!h) return false;
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (!p) return false;
        const SIZE_T bytes = GlobalSize(h);
        const std::wstring w(p, wcsnlen(p, bytes / sizeof(wchar_t)));
        GlobalUnlock(h);
        text = Utf8(w);
        return true;
    }
    bool ClearClipboard() override {
        ClipboardLock lock;
        return lock.open() && EmptyClipboard();
    }

    std::wstring DesktopDir() override {
        PWSTR path = nullptr;
        std::wstring out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &path)) && path) out = path;
        if (path) CoTaskMemFree(path);
        return out;
    }
    std::wstring TempDir() override {
        wchar_t buffer[MAX_PATH + 2]{};
        const DWORD n = GetTempPathW(MAX_PATH + 1, buffer);
        std::wstring out(buffer, n);
        while (!out.empty() && (out.back() == L'\\' || out.back() == L'/')) out.pop_back();
        return out;
    }
    std::string OsVersion() override {
        // RtlGetVersion reports the real version (GetVersionEx is shimmed).
        using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto fn = ntdll ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))) : nullptr;
        if (!fn || fn(&v) != 0) return "Windows";
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Windows %lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
        return buf;
    }
};

}  // namespace

std::shared_ptr<Env> MakeWin32Env() { return std::make_shared<Win32Env>(); }

std::vector<ProcessRow> GameProcessesIn(Env& env, const std::wstring& game_dir) {
    std::vector<ProcessRow> out;
    const std::wstring root = FullPath(game_dir);
    for (const ProcessRow& p : env.GameProcesses()) {
        if (p.unknown || p.path.empty()) {
            ProcessRow r = p;
            r.unknown = true;
            out.push_back(r);
            continue;
        }
        if (IEquals(ParentOf(FullPath(p.path)), root)) out.push_back(p);
    }
    return out;
}

bool AnyGameRunningIn(Env& env, const std::wstring& game_dir) { return !GameProcessesIn(env, game_dir).empty(); }

bool ProcessAliveSince(Env& env, std::uint32_t pid, std::int64_t start_ms) {
    if (pid == 0) return false;
    if (start_ms == 0) return env.ProcessAlive(pid);
    std::int64_t actual = 0;
    if (!env.ProcessStartMs(pid, actual)) return false;
    const std::int64_t d = actual > start_ms ? actual - start_ms : start_ms - actual;
    return d < 1000;
}

std::string StampNow(Env& env) { return FormatStamp(env.LocalTime(env.NowMs())); }

}  // namespace ddt::core
