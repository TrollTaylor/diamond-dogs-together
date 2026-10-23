// Diamond Dogs Together launcher core: a co-op game session in one game folder
// (ports of tools/friend/FriendCommon.psm1 and Play-Coop.ps1, Steam only).
//
//  - play-session.json (v2) records the session; every file's SHA-256 is
//    recorded BEFORE the file is written, in the order lab.ini,
//    link-secret.txt, coop.ini, script-observe.enabled (the marker last).
//  - Everything that changes the session runs under the per-folder mutex
//    Local\mgs-coop-play-<16 hex>, the same name the PowerShell tools used.
//  - Cleanup deletes only files whose hash still matches the record (or its
//    prev_sha256), the marker first, never while a game runs from the folder.
//  - The automatic save handoff after the guest's game exits: validate, claim,
//    import, rewrite lab.ini, wait for Steam, re-verify, relaunch once.
//  - The background helper (--watch) waits for the game and cleans up after it.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core_env.hpp"
#include "core_json.hpp"
#include "core_paths.hpp"
#include "core_text.hpp"

namespace ddt::core {

class PlayMutex {
public:
    explicit PlayMutex(const std::wstring& game_root);
    ~PlayMutex();
    PlayMutex(const PlayMutex&) = delete;
    PlayMutex& operator=(const PlayMutex&) = delete;
    bool Lock(unsigned long timeout_ms = 30000);
    bool locked() const { return locked_; }

private:
    void* handle_ = nullptr;
    bool locked_ = false;
};

// ---- play-session.json ------------------------------------------------------------
bool ReadPlaySession(const InstancePaths& paths, json::Value& out);   // v == 2 only
bool SavePlaySession(const InstancePaths& paths, const json::Value& session, std::string& error);
bool SessionIsActive(const json::Value& session);
// The "handoff" table with every field (state, claimed_by_pid, claimed_start_utc,
// claimed_utc, xfer, set_sha256, count, aside, message).
json::Value HandoffOf(const json::Value& session);
// The launcher window or the helper of an active session still runs (the
// calling process never counts).
bool PlaySessionLive(Env& env, const InstancePaths& paths);
bool HandoffClaimLive(Env& env, const json::Value& session);
bool HandoffPending(Env& env, const json::Value& session);
// Read-modify-write under the mutex; only an active session (with that id
// when session_id is not empty). False when there is none.
bool UpdatePlaySession(Env& env, const InstancePaths& paths, const std::string& session_id,
                       const std::function<void(json::Value&)>& update, json::Value* out = nullptr);
// Records the running game; a relaunching handoff becomes relaunched.
bool SetSessionGame(Env& env, const InstancePaths& paths, const std::string& session_id, std::uint32_t pid, std::int64_t start_ms,
                    bool* relaunched = nullptr);

// ---- Activation -----------------------------------------------------------------------
struct ActivationRequest {
    LabIniParams lab;          // role, save_dir, steam keys, save_handoff...
    std::string secret;        // "" = a guest waiting for an invite (no link-secret.txt)
    std::string character;     // coop.ini character
    std::string fallback = "dd_male";
    std::string hud;           // "" | compact | full | off
    std::string clip_sha256;   // the join code copied to the clipboard (upper-case SHA-256)
    std::string launcher_note;
};
// verify_before_marker re-checks the install right before the marker is
// written (loader, package, runtime archive). On failure nothing more is
// written; the caller runs CleanupSession(force) to undo the written files.
bool ActivateSession(Env& env, const InstancePaths& paths, const ActivationRequest& request,
                     const std::function<bool(std::string&)>& verify_before_marker, std::string& session_id_out, std::string& error);
// The texts ActivateSession writes, in order (name, bytes).
bool SessionFileTexts(const ActivationRequest& request, std::vector<std::pair<std::string, std::string>>& files, std::string& error);

// ---- Cleanup ----------------------------------------------------------------------------
struct CleanupResult {
    bool ran = false;
    std::vector<std::string> removed, kept, messages;
};
// Invoke-CoopPlayCleanup (+ never while a game runs from the folder). force
// skips the live-handoff-claim guard.
CleanupResult CleanupSession(Env& env, const InstancePaths& paths, bool force);
struct StaleResult {
    std::vector<std::string> removed, messages;
};
// Remove-CoopStaleHandoff: request.json, staged and .partial folders,
// link-rejoin.txt of an earlier session (not while a session is live, a claim
// is live or a game runs).
StaleResult RemoveStaleHandoff(Env& env, const InstancePaths& paths);

// ---- Steam's running flag and the relaunch ------------------------------------------------
struct SteamRunning {
    bool known = false, running = false;
    std::string text = "unknown";
};
SteamRunning GetSteamAppRunning(Env& env);
struct SteamWait {
    bool stopped = true, known = false;
    std::int64_t waited_ms = 0;
    std::string last;
};
SteamWait WaitSteamAppStopped(Env& env, int timeout_sec, const std::atomic<bool>* cancel = nullptr);
struct RelaunchResult {
    bool ok = false, started = false;
    std::string problem;
};
// Start-CoopRelaunch: only while the session is active and relaunching and
// every recorded file is in place with its hash (the marker exactly
// offline-v1). check_only checks and starts nothing.
RelaunchResult StartRelaunch(Env& env, const InstancePaths& paths, bool check_only);

// ---- The save handoff after the guest's game exited --------------------------------------
struct HandoffOutcome {
    std::string action = "none";   // none | deferred | relaunched | claimed_by_other | limit | stale | failed | cancelled
    std::vector<std::string> messages;
    std::string aside;             // the folder name below coop-saves-replaced
    std::size_t files = 0;
    std::uint64_t bytes = 0;
};
HandoffOutcome HandoffAfterExit(Env& env, const InstancePaths& paths, std::uint32_t exited_pid, std::int64_t exited_start_ms,
                                bool owner_watchdog, bool launcher_alive, std::int64_t exit_seen_ms,
                                const std::function<void(const HandoffOutcome&)>& on_applied, const std::atomic<bool>* cancel);
struct RelaunchWait {
    bool stop = false, off = false;
    std::vector<std::string> lines;
    std::int64_t claimed_ms = 0;
};
RelaunchWait GetRelaunchWait(Env& env, const InstancePaths& paths, const std::string& session_id);

// ---- The background helper ---------------------------------------------------------------------
// DiamondDogsTogether.exe --watch <session_id>: claims the helper role under
// the mutex (a second helper exits), waits for the game, applies a save
// handoff when the window is gone, cleans up after the game exits. Returns
// the process exit code.
int RunWatchdog(Env& env, const std::wstring& game_dir, const std::string& session_id);
// Starts the helper and records it in the session. False when it could not start.
bool StartWatchdogFor(Env& env, const InstancePaths& paths, const std::string& session_id);

}  // namespace ddt::core
