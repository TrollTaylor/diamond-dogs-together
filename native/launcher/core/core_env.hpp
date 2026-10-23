// Diamond Dogs Together launcher core: the seams to the outside world
// (registry, processes, clock, Steam/shell launch, clipboard, special folders).
// The real implementation is Win32; the unit tests pass a fake. The file
// system is used directly (tests run in temp folders).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core_util.hpp"

namespace ddt::core {

struct ProcessRow {
    std::uint32_t pid = 0;
    std::wstring path;      // full executable path; "" when it cannot be read
    bool unknown = false;   // the path could not be read (counts as "maybe this folder")
};

class Env {
public:
    virtual ~Env() = default;

    // Clock: UTC milliseconds since 1970; a monotonic sleep.
    virtual std::int64_t NowMs() = 0;
    virtual void SleepMs(std::uint32_t ms) = 0;
    virtual Civil LocalTime(std::int64_t utc_ms) = 0;

    // Registry, read-only. hklm = HKEY_LOCAL_MACHINE (else HKEY_CURRENT_USER).
    virtual bool RegDword(bool hklm, const std::wstring& key, const std::wstring& name, std::uint32_t& out) = 0;
    virtual bool RegString(bool hklm, const std::wstring& key, const std::wstring& name, std::wstring& out) = 0;

    // Processes. GameProcesses lists every mgsvtpp.exe / mgsvmgo.exe.
    virtual std::vector<ProcessRow> GameProcesses() = 0;
    // The start time of a running process (UTC ms); false when it is gone or
    // cannot be opened.
    virtual bool ProcessStartMs(std::uint32_t pid, std::int64_t& start_ms) = 0;
    virtual bool ProcessAlive(std::uint32_t pid) = 0;
    virtual std::uint32_t CurrentPid() = 0;
    virtual std::int64_t CurrentStartMs() = 0;

    // Opens a URL with the shell (steam://rungameid/287700).
    virtual bool OpenUrl(const std::wstring& url) = 0;
    // Starts "<this exe> --watch <session_id>" hidden and detached, working
    // folder `game_dir`. pid_out = the new process.
    virtual bool StartWatchdog(const std::wstring& game_dir, const std::string& session_id, std::uint32_t& pid_out) = 0;

    // Clipboard (UTF-8 text).
    virtual bool SetClipboardText(const std::string& text) = 0;
    virtual bool GetClipboardText(std::string& text) = 0;
    virtual bool ClearClipboard() = 0;

    // Special folders.
    virtual std::wstring DesktopDir() = 0;
    virtual std::wstring TempDir() = 0;
    virtual std::string OsVersion() = 0;
};

std::shared_ptr<Env> MakeWin32Env();

// Helpers built on Env.
// The game processes whose executable lives directly in `game_dir` (unknown
// paths included, flagged).
std::vector<ProcessRow> GameProcessesIn(Env& env, const std::wstring& game_dir);
bool AnyGameRunningIn(Env& env, const std::wstring& game_dir);   // unknown counts as running
// Alive and (start_ms != 0) started within 1 s of start_ms.
bool ProcessAliveSince(Env& env, std::uint32_t pid, std::int64_t start_ms);
std::string StampNow(Env& env);   // local yyyyMMdd-HHmmss

}  // namespace ddt::core
