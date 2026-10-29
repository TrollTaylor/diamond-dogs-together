#pragma once
// Diamond Dogs Together: the pure part of the GUI shell (no ImGui, no Win32),
// so it can be unit-tested headless (tests/launcher_ui_tests.cpp):
// page navigation, the character picker mapping, the HUD setting, the Join
// field's message/tone, join-code display, setup summary and view, redraw
// pacing, the screenshot scenes and the command line.
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
    std::wstring error;             // non-empty: the command line was refused
};
// args excludes the program name.
Options ParseCommandLine(const std::vector<std::wstring>& args);
const wchar_t* UsageText();

}  // namespace ddt::ui
