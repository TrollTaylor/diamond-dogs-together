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

// --local-watch <run>: the hidden helper of the developer's local two-window test
// (Settings > Debug): restores both game folders after both games have exited.
int RunLocalWatch(const std::wstring& run);

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

// An updater before 0.14.4 has just started this exe (DiamondDogsTogether.exe.old
// next to it from an update a few minutes old, core_update.hpp
// OldExecutableFromRecentUpdate): the window waits for that old launcher.
bool OldLauncherFromRecentUpdate(const std::wstring& game_dir);

// One launcher window per game folder (launcher_main.cpp ClaimSingleInstance):
// claims the folder's single-instance mutex `name` and returns it owned (a
// HANDLE), or nullptr when another window keeps the folder (the caller exits).
// When another process holds it: waits up to wait_ms for it (the restart after
// an update), then calls bring_forward(), which puts the window that holds the
// folder in front and says whether it found one. When it found none, the
// folder's last window is closing (0.14.4 review, round 2: a window closed
// during setup is gone at once, but its process holds the folder until it has
// ended its setup, about 50 ms) or the next one is still opening: it waits up
// to no_window_wait_ms for the folder, then looks for that window once more.
// A mutex abandoned by a process that ended counts as released.
constexpr int kInstanceNoWindowWaitMs = 2000;
void* ClaimFolderInstance(const std::wstring& name, int wait_ms, int no_window_wait_ms, const std::function<bool()>& bring_forward);

}  // namespace ddt
