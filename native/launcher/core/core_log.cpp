// Diamond Dogs Together launcher core: launcher.log (see core_log.hpp).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core_log.hpp"

#include <mutex>

#include "core_text.hpp"

namespace ddt::core {

void LauncherLog(Env& env, const InstancePaths& paths, const std::string& text) {
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    if (!IsDir(paths.mod)) return;
    std::string clean = text;
    for (char& c : clean)
        if (c == '\r' || c == '\n') c = ' ';
    clean = RedactForLog(clean);
    const std::string line = FormatUtcSeconds(env.NowMs()) + " pid=" + std::to_string(env.CurrentPid()) + " " + clean + "\r\n";
    if (FileSizeOrZero(paths.launcher_log) > kLauncherLogMaxBytes)
        MoveFileExW(paths.launcher_log.c_str(), paths.launcher_old_log.c_str(), MOVEFILE_REPLACE_EXISTING);
    for (int attempt = 1; attempt <= 5; ++attempt) {
        HANDLE h = CreateFileW(paths.launcher_log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            WriteFile(h, line.data(), static_cast<DWORD>(line.size()), &wrote, nullptr);
            CloseHandle(h);
            return;
        }
        Sleep(static_cast<DWORD>(20 * attempt));
    }
}

}  // namespace ddt::core
