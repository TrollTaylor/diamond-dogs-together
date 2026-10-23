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
