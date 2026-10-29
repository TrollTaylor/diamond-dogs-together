#pragma once
// Diamond Dogs Together: the windowless command-line modes, and the core
// steps the window shell needs before its model exists.
#include <cstdint>
#include <functional>
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

// --after-update <pid> (the restart after a self-update, core_update.hpp):
// tells the old launcher that this window runs, waits for it to close (ends it
// when it hangs), then calls claim(wait_ms) for the folder's single instance.
// True when claimed.
bool TakeOverAfterUpdate(std::uint32_t pid, const std::function<bool(int wait_ms)>& claim);

}  // namespace ddt
