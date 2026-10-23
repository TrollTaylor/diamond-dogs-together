#pragma once
// Diamond Dogs Together: the windowless command-line modes.
#include <string>

namespace ddt {

// --watch <session>: the hidden helper that keeps watching the game after the
// window closes (cleanup and save handoff after the game exits). C0 STUB: it
// returns 0 at once; B (wp/launcher-core) implements it on the real model.
int RunWatch(const std::wstring& session);

// Developer only (the release gate's hidden-worker runs): --write-session
// host|join writes the session files exactly as Host / Join (after the setup
// when it is needed) without the helper, Steam or the clipboard; --end-session
// switches co-op off again. game_dir "" = the exe's own folder. `report` gets
// key=value lines (core_model.hpp RunWriteSession / RunEndSession).
int RunWriteSession(const std::wstring& game_dir, bool host, const std::wstring& code, const std::wstring& character, std::string& report);
int RunEndSession(const std::wstring& game_dir, std::string& report);

// --selftest: the built-in checks; 0 = passed. C0 runs the mock model through
// setup, hosting, joining, a session with a save handoff and the tools, plus
// the join-code check. B adds the core's own checks. `report` gets one line
// per failure (and a summary line).
int RunSelfTest(std::string& report);

}  // namespace ddt
