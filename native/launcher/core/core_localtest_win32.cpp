// Diamond Dogs Together launcher core: the Win32 seams of the developer's local
// two-window test (core_localtest.hpp LocalHost). Window placement uses the
// per-monitor DPI-aware coordinates of LocalCoopWindows.psm1 and never
// activates a window or sends input; closing is only ever asked for the exact
// game processes the run started.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>

#include "core_localtest.hpp"
#include "core_util.hpp"

namespace ddt::core {
namespace {

std::int64_t FileTimeToMs(const FILETIME& ft) {
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<std::int64_t>(u.QuadPart / 10000ULL) - 11644473600000LL;
}

bool StartOf(HANDLE process, std::int64_t& ms) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return false;
    ms = FileTimeToMs(created);
    return true;
}

std::wstring ThisExe() {
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

// Per-monitor DPI awareness for this thread while it reads or places windows.
class DpiScope {
public:
    DpiScope() {
        using Fn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        set_ = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetThreadDpiAwarenessContext")));
        if (set_) {
            old_ = set_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            if (!old_) old_ = set_(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        }
    }
    ~DpiScope() {
        if (set_ && old_) set_(old_);
    }
    DpiScope(const DpiScope&) = delete;
    DpiScope& operator=(const DpiScope&) = delete;

private:
    DPI_AWARENESS_CONTEXT(WINAPI* set_)(DPI_AWARENESS_CONTEXT) = nullptr;
    DPI_AWARENESS_CONTEXT old_ = nullptr;
};

struct EnumState {
    DWORD pid = 0;
    std::vector<LocalWindow>* out = nullptr;
};

BOOL CALLBACK CollectWindow(HWND hwnd, LPARAM data) {
    EnumState& s = *reinterpret_cast<EnumState*>(data);
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    if (pid != s.pid || !IsWindowVisible(hwnd) || IsIconic(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
    RECT r{}, c{};
    if (!GetWindowRect(hwnd, &r) || !GetClientRect(hwnd, &c) || c.right - c.left < 200 || c.bottom - c.top < 100) return TRUE;
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    LocalWindow w;
    w.handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hwnd));
    w.rect = LocalRect{r.left, r.top, r.right - r.left, r.bottom - r.top};
    w.moving = GetGUIThreadInfo(tid, &gui) && (gui.flags & GUI_INMOVESIZE);
    s.out->push_back(w);
    return TRUE;
}

BOOL CALLBACK CloseWindowOf(HWND hwnd, LPARAM data) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == static_cast<DWORD>(data) && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    return TRUE;
}

// ShellExecuteEx may hand a launch to shell extensions, which need COM on the calling thread (the local test starts
// on its own thread). Balanced: an already initialised thread (S_FALSE) is undone too; another mode is left alone.
class ComScope {
public:
    ComScope() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

private:
    HRESULT hr_;
};

class Win32LocalHost final : public LocalHost {
public:
    bool StartGame(const std::wstring& exe, const std::wstring& dir, std::uint32_t& pid, std::int64_t& start_ms, std::string& error) override {
        pid = 0;
        start_ms = 0;
        const ComScope com;
        // The shell's normal GUI launch (the PowerShell test's UseShellExecute), nothing inherited.
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"open";
        info.lpFile = exe.c_str();
        info.lpDirectory = dir.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (!ShellExecuteExW(&info) || !info.hProcess) {
            error = WinError(GetLastError());
            return false;
        }
        pid = GetProcessId(info.hProcess);
        const bool ok = pid != 0 && StartOf(info.hProcess, start_ms);
        CloseHandle(info.hProcess);
        if (!ok) error = "the new process could not be read";
        return ok;
    }

    std::wstring ProcessImage(std::uint32_t pid) override {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return {};
        std::wstring name(32768, L'\0');
        DWORD size = static_cast<DWORD>(name.size());
        const BOOL ok = QueryFullProcessImageNameW(h, 0, name.data(), &size);
        CloseHandle(h);
        if (!ok || !size) return {};
        name.resize(size);
        std::wstring longer(32768, L'\0');
        const DWORD n = GetLongPathNameW(name.c_str(), longer.data(), static_cast<DWORD>(longer.size()));
        if (n > 0 && n < longer.size()) {
            longer.resize(n);
            return longer;
        }
        return name;
    }

    bool StartHelper(const std::wstring& game_dir, const std::string& run_id, std::uint32_t& pid, std::int64_t& start_ms) override {
        pid = 0;
        start_ms = 0;
        const std::wstring exe = ThisExe();
        if (exe.empty()) return false;
        for (char c : run_id)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        std::wstring cmd = L"\"" + exe + L"\" --local-watch " + Wide(run_id);
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        const DWORD base = CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW;
        BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, base | CREATE_BREAKAWAY_FROM_JOB, nullptr, game_dir.c_str(), &si,
                                 &pi);
        if (!ok) ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, base, nullptr, game_dir.c_str(), &si, &pi);
        if (!ok) return false;
        pid = pi.dwProcessId;
        StartOf(pi.hProcess, start_ms);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return true;
    }

    bool WorkArea(LocalRect& area, int& dpi) override {
        DpiScope scope;
        HWND reference = GetForegroundWindow();
        HMONITOR monitor = MonitorFromWindow(reference, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info{};
        info.cbSize = sizeof(info);
        if (!monitor || !GetMonitorInfoW(monitor, &info)) return false;
        area = LocalRect{info.rcWork.left, info.rcWork.top, info.rcWork.right - info.rcWork.left, info.rcWork.bottom - info.rcWork.top};
        dpi = 96;
        using Fn = UINT(WINAPI*)(HWND);
        if (auto get = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"))); get && reference)
            if (const UINT d = get(reference)) dpi = static_cast<int>(d);
        return true;
    }

    std::vector<LocalWindow> Windows(std::uint32_t pid) override {
        DpiScope scope;
        std::vector<LocalWindow> out;
        EnumState s;
        s.pid = pid;
        s.out = &out;
        EnumWindows(CollectWindow, reinterpret_cast<LPARAM>(&s));
        std::sort(out.begin(), out.end(), [](const LocalWindow& a, const LocalWindow& b) {
            return static_cast<long long>(a.rect.width) * a.rect.height > static_cast<long long>(b.rect.width) * b.rect.height;
        });
        return out;
    }

    bool PlaceWindow(std::uint64_t handle, const LocalRect& rect) override {
        DpiScope scope;
        HWND hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(handle));
        if (!IsWindow(hwnd)) return false;
        // No activation, no z-order change, no input.
        return SetWindowPos(hwnd, nullptr, rect.x, rect.y, rect.width, rect.height, SWP_NOZORDER | SWP_NOACTIVATE) != 0;
    }

    void CloseGame(std::uint32_t pid, unsigned wait_ms) override {
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) return;
        EnumWindows(CloseWindowOf, static_cast<LPARAM>(pid));
        if (WaitForSingleObject(h, wait_ms) == WAIT_TIMEOUT) TerminateProcess(h, 1);
        CloseHandle(h);
    }

    bool PortFree(int port, bool tcp) override {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        bool available = false;
        SOCKET s = socket(AF_INET, tcp ? SOCK_STREAM : SOCK_DGRAM, tcp ? IPPROTO_TCP : IPPROTO_UDP);
        if (s != INVALID_SOCKET) {
            BOOL exclusive = TRUE;
            setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(static_cast<u_short>(port));
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            available = bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0;
            closesocket(s);
        }
        WSACleanup();
        return available;
    }
};

}  // namespace

std::shared_ptr<LocalHost> MakeWin32LocalHost() { return std::make_shared<Win32LocalHost>(); }

}  // namespace ddt::core
