#pragma once
// Diamond Dogs Together: the pure part of the GUI shell (no ImGui, no Win32),
// so it can be unit-tested headless (tests/launcher_ui_tests.cpp):
// page navigation, the character picker mapping, the HUD setting, the Join
// field's message/tone, join-code display, setup summary and view, redraw
// pacing, the screenshot scenes, the update notes and the command line.
#include <cstdint>
#include <string>
#include <vector>

#include "launcher_model.hpp"

namespace ddt::ui {

// ---- Pages and navigation --------------------------------------------------
// There is no Setup page (2026-10-29): the setup runs by itself and Play shows
// its progress; "not in the game folder" is a full-window message instead of a page.
enum class Page { play, in_game, tools, settings };
inline constexpr int kPageCount = 4;
const char* PageLabel(Page page);

// A game session exists (the game is starting, running or just closed).
bool SessionActive(Phase phase);
// Share save, Collect logs and Uninstall are allowed (no session, no setup running,
// in the game folder).
bool ToolsAllowed(Phase phase);
// Host and Join can be pressed (co-op is ready and no game runs).
bool CanStartSession(Phase phase);
// The nav shows the item: every page, except on the wrong-folder screen (no navigation).
bool PageVisible(Phase phase, Page page);
// The nav item can be clicked.
bool PageEnabled(Phase phase, Page page);
// The page for a phase when nothing else applies.
Page DefaultPage(Phase phase);
// Automatic navigation when the model's phase changes. It follows the flow
// (play -> in game -> play) but leaves the player on Tools or Settings unless
// that page becomes unusable.
Page PageAfterPhaseChange(Phase from, Phase to, Page current, Role role);

// ---- Play tabs (Settings > Debug mode adds the local test) --------------------
enum class PlayTab { host, join, local_test };
int PlayTabCount(bool debug);           // 2, or 3 with Debug mode on
const char* PlayTabLabel(PlayTab tab);  // "Host" | "Join" | "Local test"
// The tab Play shows: a running local test always; never the local test with Debug mode off.
PlayTab ShownPlayTab(PlayTab chosen, bool debug, bool local_test_active);
// "Start local test" can be pressed.
bool CanStartLocalTest(Phase phase, const LocalTestCheck& check);
// The second folder's check is on screen (the Local test tab, or Settings with
// Debug mode on); the GUI refreshes it when it comes into view.
bool LocalCheckVisible(Page page, PlayTab shown_tab, bool debug);
// How that check shows: ready (a check mark), still checking on its own thread
// (dim, no "Open Settings"), or what is needed first (a warning).
enum class CheckTone { ready, checking, needed };
CheckTone LocalCheckTone(const LocalTestCheck& check);
// "Arrange windows" can be pressed: both games run (not while the test starts,
// not after a safety stop).
bool CanArrangeLocal(const LocalTestView& local);

// ---- Character picker ------------------------------------------------------
enum class CharacterChoice { story, ocelot, dd_soldier, avatar };
inline constexpr int kCharacterChoiceCount = 4;
CharacterChoice ChoiceOf(Character character);
bool IsFemale(Character character);
Character CharacterOf(CharacterChoice choice, bool female);

// ---- HUD setting -----------------------------------------------------------
inline constexpr int kHudCount = 3;
int HudIndex(const std::string& hud);   // compact 0, full 1, off 2; anything else -> compact
const char* HudValue(int index);        // "compact" | "full" | "off"

// ---- Join field ------------------------------------------------------------
enum class Tone { neutral, good, warn, bad };
struct JoinFieldView {
    Tone tone = Tone::neutral;
    std::string message;
    bool can_join = false;
    bool field_enabled = true;
};
// What the Join tab shows under the code field and whether "Join game" is
// enabled. `check` is the model's check_code() of `text`; `busy` = a session
// is already running.
JoinFieldView EvaluateJoinField(const std::string& text, bool wait_for_invite, const CodeCheck& check, bool busy);

// ---- Join code display -----------------------------------------------------
// "MGS2-AAAAA-BBBBB-CCCCC-DDDDD-CC@76561197960265729" -> main
// "MGS2-AAAAA-BBBBB-CCCCC-DDDDD-CC", account "@76561197960265729". A code
// without '@' is all main.
struct CodeDisplay {
    std::string main;
    std::string account;
};
CodeDisplay SplitJoinCode(const std::string& code);

// ---- Link status -----------------------------------------------------------
Tone PingTone(int ms);                  // <= 0 neutral, < 90 good, < 180 warn, else bad
std::string FormatPing(int ms);         // "42 ms", or "-" when unknown
int PercentOf(float fraction);          // clamped 0..100, rounded down

// ---- Setup -----------------------------------------------------------------
struct SetupSummary {
    float overall = 0.0f;   // 0..1 over all steps
    int done = 0;
    int total = 0;
    bool failed = false;
    bool running = false;
    int current = -1;       // index of the running (or failed) step, -1 if none
};
SetupSummary SummarizeSetup(const std::vector<SetupStep>& steps);

// What Play shows about the automatic setup: nothing, a small "Getting co-op
// ready..." line (running, or needed and about to start by itself), or the
// failure banner with "Try again" (the model waits after a failed attempt).
enum class SetupView { none, preparing, failed };
SetupView GetSetupView(Phase phase, const SetupSummary& summary);

// ---- Update ----------------------------------------------------------------
// "Update now" can be pressed: not while co-op gets ready by itself (installing
// the update and restarting would cut the setup off), during a game or a local
// test, or in the wrong folder. A failed setup does not block it.
bool CanStartUpdate(Phase phase, SetupView setup);

// ---- Redraw pacing ---------------------------------------------------------
// How long the message loop may wait for input before the next frame. Input
// always wakes it; this only paces the model's tick() and progress updates.
// `animating`: something on screen moves (recent input, hover, a spinner).
int IdleWaitMs(Phase phase, bool animating);

// ---- Screenshot scenes (--screenshot-all) ------------------------------------
// Every scene the hidden screenshot run renders, in order; the PNG is named
// SceneName(scene) + ".png" ("01-play-preparing.png").
enum class Scene {
    play_preparing,
    play_setup_failed,
    wrong_folder,
    play_host,
    play_host_code,
    play_join,
    play_join_invalid,
    play_join_valid,
    play_join_invite,
    play_error,
    ingame_launching,
    ingame_host_waiting,
    ingame_connected,
    ingame_handoff,
    ingame_after_game,
    tools,
    tools_result,
    tools_uninstall_confirm,
    tools_about,
    settings,
    tools_in_game,
    uninstall_report,
    settings_debug_no_folder,
    settings_debug_folder,
    play_local_no_folder,
    play_local_ready,
    play_local_running,
    ingame_local_test,
    play_update_preparing,   // an update is out while co-op gets ready: Update now waits
    ingame_local_starting,   // Start local test pressed: both folders are made ready (its own thread)
    play_local_checking,     // the second folder is being checked (its own thread)
    play_updated_preparing,  // the new window right after a self-update: "Updated to", co-op gets ready once more
    tools_updated,           // ... Tools once co-op is ready again
    tools_update_refused,    // Update now while MGSV runs from the folder
    play_updated,            // ... Play once co-op is ready again
    play_update_failed,      // Update now on Play did not go through (the longest sentence)
    tools_update_cancelling, // Cancel pressed: the download is stopping
    count
};
int SceneCount();
const char* SceneName(Scene scene);   // "" when out of range

// ---- Command line ----------------------------------------------------------
struct Options {
    enum class Mode { gui, watch, selftest, screenshots, help, write_session, end_session, local_watch } mode = Mode::gui;
    bool mock = false;              // --mock (developer flag; the real model is the default)
    std::wstring watch_session;     // --watch <session>
    std::wstring local_run;         // --local-watch <run>: the local two-window test's helper
    // Developer flags for the release gate's hidden-worker runs (no window, no Steam):
    // --write-session host|join [--code <code>] [--character <c>] [--game-dir <dir>]
    // and --end-session [--game-dir <dir>].
    bool write_host = true;         // --write-session host (else join)
    std::wstring write_code;        // --code <code> (join; none = wait for a Steam invite)
    std::wstring write_character;   // --character <c> (join)
    std::wstring game_dir;          // --game-dir <dir> (default: the exe's folder)
    std::wstring screenshot_dir;    // --screenshot-all <dir>
    std::wstring scenario;          // --mock-scenario <name>
    double mock_speed = 1.0;        // --mock-speed <factor>
    float scale = 0.0f;             // --scale <factor>; 0 = the monitor's DPI
    // The restart after a self-update (the old launcher passes it, core_update.hpp):
    std::uint32_t after_update = 0; // --after-update <pid>: wait for that launcher to close first
    // Developer flags for the update test (the window only, real model):
    std::wstring update_feed;       // --update-feed <url>: a loopback release feed instead of GitHub's
    bool update_now = false;        // --update-now: install a found update at once (needs --update-feed)
    std::wstring error;             // non-empty: the command line was refused
};
// args excludes the program name.
Options ParseCommandLine(const std::vector<std::wstring>& args);
const wchar_t* UsageText();

// ---- Single instance and the restart after an update -------------------------
// How long a starting window waits for the folder's single-instance mutex to
// be released before it gives up and brings the open window forward instead:
// after --after-update (the old launcher closes right after the handoff), and
// when an update just left DiamondDogsTogether.exe.old next to the exe (an
// updater before 0.14.4 started this exe and exits by itself a moment later;
// core_update.hpp OldExecutableFromRecentUpdate). 0 otherwise.
int InstanceClaimWaitMs(std::uint32_t after_update, bool old_exe_from_recent_update);
// The window closes itself: the updated launcher has taken over.
bool CloseForUpdate(const UpdateInfo& info);
// An update is downloading, installing or handing over: the setup cannot start,
// and a closing window releases the folder only after the update has stopped
// (otherwise at once, so a slow update check never blocks the next start).
bool UpdateInProgress(const UpdateInfo& info);
// A closing window keeps the folder until its model has ended its threads: while
// an update is in progress, or while the local test is still starting (that
// thread is switching both folders over). Otherwise the folder is free at once.
bool HoldFolderWhileClosing(const UpdateInfo& info, const LocalTestView& local);

// ---- Update notes on the pages ----------------------------------------------
// The update card on Play: nothing, an update to install, one that did not go
// through (its sentence, with Update now again), the download, the install,
// or the note right after a self-update.
enum class PlayBanner { none, available, failed, downloading, installing, updated };
PlayBanner PlayBannerFor(const UpdateInfo& info);
// "Updated to version X." right after a self-update ("" otherwise). An update
// always needs the setup again (the install record names the version); it runs
// by itself and Play shows it on its own line, so the note does not mention it.
std::string UpdatedNote(const UpdateInfo& info);
// The small line next to Play's "Getting co-op ready...": the first start, right
// after a self-update, or waiting for an update in progress.
std::string PreparingNote(const UpdateInfo& info);

// The GitHub release page of a version ("0.14.4" or "0.14.4+abc1234"): the
// player zip and its source zip (DDT-source-<version>.zip) are both there.
// Anything else falls back to the list of releases.
std::string ReleasePageUrl(const std::string& version);

}  // namespace ddt::ui
