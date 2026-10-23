// Diamond Dogs Together launcher core: the real LauncherModel and the
// windowless entry points (--watch, --selftest).
#pragma once
#include <memory>
#include <string>

#include "core_env.hpp"
#include "core_json.hpp"
#include "launcher_model.hpp"

namespace ddt::core {

// MakeRealModel(exe_dir) with injectable seams (tests).
std::unique_ptr<LauncherModel> MakeRealModelWithEnv(const std::wstring& exe_dir, std::shared_ptr<Env> env);

// DiamondDogsTogether.exe --watch <session_id>: the hidden helper for the game
// folder `exe_dir` (the exe's own folder).
int RunWatch(const std::wstring& exe_dir, const std::string& session_id);

// ---- Developer flags (not for players; the release gate's hidden-worker runs) ----
// DiamondDogsTogether.exe --write-session host|join [--code <code>] [--character <c>]
// [--game-dir <dir>]: runs the setup when the folder needs it, then writes the
// session files exactly as Host / Join do (play-session.json, lab.ini,
// link-secret.txt, coop.ini, the marker; same checks, same order), but starts
// neither the background helper nor Steam and leaves the clipboard alone. The
// caller starts the game itself and switches co-op off with --end-session.
struct WriteSessionRequest {
    bool host = true;
    std::string code;        // join: the host's code; "" = wait for a Steam invite
    std::string character;   // join: story | ocelot | dd_male | dd_female | avatar; "" = the saved choice
};
// `out` gets key=value lines: setup=..., role=, session=, code= (host), error=.
// 0 = written, 1 = refused (error= says why), 2 = bad request.
int RunWriteSession(const std::wstring& game_dir, const WriteSessionRequest& request, std::string& out);
int RunWriteSessionWithEnv(const std::wstring& game_dir, std::shared_ptr<Env> env, const WriteSessionRequest& request, std::string& out);
// --end-session [--game-dir <dir>]: switches co-op off in the folder (the same
// hash-checked cleanup as after a game; never while MGSV runs from the folder)
// and removes stale save-handoff leftovers. 0 = off (or nothing was on), 1 = refused.
int RunEndSession(const std::wstring& game_dir, std::string& out);
int RunEndSessionWithEnv(const std::wstring& game_dir, std::shared_ptr<Env> env, std::string& out);

// The core's built-in checks for --selftest: the embedded runtime assets, the
// join code, the session file texts, redaction, and the model on a throwaway
// folder in %TEMP%. The game folder itself is only read (and reported as
// "info:" lines). One "FAIL: ..." line per failure; 0 = passed.
int RunSelfTest(const std::wstring& exe_dir, std::string& report);

// The In game page's plain-language view of a link-status.json document
// (Wording::launcher sentences; host = this PC hosts).
LinkView LinkViewFromStatus(const json::Value& status, bool host, std::int64_t now_ms, const std::string& own_build);

std::string CharacterName(Character c);
bool CharacterFromName(const std::string& name, Character& out);

}  // namespace ddt::core
