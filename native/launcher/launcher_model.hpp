#pragma once
// Diamond Dogs Together launcher: the model behind the GUI shell
// (launcher spec 2026-10-23, "LauncherModel interface").
//
// C0 (wp/launcher-gui) implements the shell and a MOCK model
// (launcher_model_mock.cpp); B (wp/launcher-core) implements the real model.
// Every string is UTF-8 and player-facing: no file names of the mod's
// internals, no codes, IDs or keys in log-like form.
//
// Threading: the GUI calls every member from its one UI thread. tick() runs
// every frame (at least once a second, about 4 Hz while a game runs); long work
// (setup) runs on the model's own worker thread and is observed by polling.
//
// C0 additions to the spec interface (all documented below, marked "C0"; the
// B additions session_role() / join_code() follow them):
//  - default member initializers on the plain structs (no layout change);
//  - discovered_game_dir(): the not_in_game_folder screen shows where Steam
//    keeps MGSV and offers "Open my game folder" (Explorer is opened by the GUI);
//  - cancel_launch(): the "Cancel" button while the game is starting;
//  - recheck(): the "Check again" button on the error / wrong-folder screens.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ddt {

enum class Role { host, join };
enum class Character { story, ocelot, dd_male, dd_female, avatar };   // the host is always snake
enum class Phase { not_in_game_folder, needs_setup, setting_up, ready, launching, in_game, after_game, error };

struct SetupStep {
    std::string label;
    enum class State { pending, running, done, failed } state = State::pending;
    std::string detail;
    float progress = 0.0f;   // 0..1 within this step
};

struct LinkView {
    bool connected = false;
    int ping_ms = 0;                 // <= 0: unknown
    std::string partner_name;        // "" until known
    std::string partner_mission;     // where the partner is, e.g. "Mother Base"; "" until known
    std::string headline;            // one short line, e.g. "Connected to Kaz"
    std::string detail;              // plain-language reason sentence(s)
    float handoff_progress = 0.0f;   // 0..1, meaningful while handoff_active
    bool handoff_active = false;     // a save handoff is running
};

struct CodeCheck {
    bool ok = false;
    std::string message;             // "" for an empty field; otherwise a sentence for the player
};

enum class UpdateStatus {
    idle,
    checking,
    up_to_date,
    available,
    downloading,
    applying,
    ready_to_restart,
    error
};

// The developer's local two-window test (Settings > Debug mode, Play > Local test).
struct LocalTestCheck {
    bool ok = false;        // Start local test can be pressed
    std::string message;    // what starting does, or what is needed first
    bool checking = false;  // the second folder is being checked (on its own thread)
};
struct LocalTestView {
    bool active = false;    // a local test owns both games (until they exit and the folders are restored)
    bool failed = false;    // stopped by a safety check
    bool starting = false;  // Start local test is preparing both folders and starting both games (its own thread)
    std::string headline;   // "" when there was no local test in this window
    std::string detail;     // the status of both games; after a test, how it ended
};

struct UpdateInfo {
    UpdateStatus status = UpdateStatus::idle;
    std::string current_version;
    std::string latest_version;
    std::string release_title;
    std::string release_notes;
    std::string download_url;
    std::string html_url;
    std::string asset_name;
    std::uint64_t download_size = 0;
    std::uint64_t bytes_downloaded = 0;
    float progress = 0.0f;
    std::string error;
};

class LauncherModel {
public:
    virtual ~LauncherModel() = default;
    virtual Phase phase() const = 0;
    virtual std::string version() const = 0;
    virtual std::string game_dir() const = 0;
    virtual std::vector<SetupStep> setup_steps() const = 0;
    virtual void start_setup() = 0;                          // runs on a worker thread; poll setup_steps()
    virtual Character character() const = 0;
    virtual void set_character(Character) = 0;               // persisted
    virtual std::string hud() const = 0;                     // "compact" | "full" | "off"
    virtual void set_hud(const std::string&) = 0;            // persisted; applies to the next game
    virtual bool host(std::string& code_out, std::string& error_out) = 0;         // activate + launch
    virtual CodeCheck check_code(const std::string& code) const = 0;
    virtual bool join(const std::string& code_or_empty_for_invite, std::string& error_out) = 0;
    virtual LinkView link() const = 0;                       // poll ~4 Hz while in_game
    virtual std::string last_error() const = 0;
    virtual bool export_save(std::string& path_out, std::string& error_out) = 0;
    virtual bool import_save(const std::string& zip, std::string& error_out) = 0;
    virtual bool restore_save(std::string& error_out) = 0;
    virtual bool collect_logs(std::string& path_out, std::string& error_out) = 0;
    virtual bool uninstall(std::string& report_out, std::string& error_out) = 0;
    virtual void tick() = 0;                                 // called every frame (game exit detection, cleanup, handoff)

    // ---- C0 additions -------------------------------------------------------
    // not_in_game_folder: the MGSV folder Steam knows about (registry
    // discovery), "" when unknown. The GUI shows it and opens it in Explorer.
    virtual std::string discovered_game_dir() const = 0;
    // launching, before the game process exists: stop waiting for the game,
    // undo the session files and go back to ready. False (with a sentence)
    // once the game is already running.
    virtual bool cancel_launch(std::string& error_out) = 0;
    // error / not_in_game_folder: run the start-up checks again (the phase
    // changes on success).
    virtual void recheck() = 0;

    // ---- B additions (wp/launcher-core) ---------------------------------------
    // While a session runs (launching, in_game, after_game): whether this PC
    // hosts or joins. A window opened while the game already runs (the first
    // window was closed) learns it here.
    virtual Role session_role() const = 0;
    // The host's join code while hosting ("" otherwise), also after the window
    // was reopened. Never logged.
    virtual std::string join_code() const = 0;

    // ---- Update checking & auto-download ------------------------------------
    virtual UpdateInfo update_info() const = 0;
    virtual void check_for_updates() = 0;
    virtual void start_update() = 0;
    virtual void cancel_update() = 0;

    // ---- Debug mode and the local two-window test (2026-10-29) ---------------
    // Settings > Debug mode shows developer tests (off by default, persisted
    // with the launcher settings). With it on, Play offers the local test: two
    // MGSV copies on this PC (this folder hosts as Snake, the second folder
    // joins as Quiet), the PowerShell tools/dev/Play-LocalCoop.ps1 test natively.
    virtual bool debug() const = 0;
    virtual void set_debug(bool on) = 0;
    // The second game folder ("" = none chosen), persisted.
    virtual std::string local_test_folder() const = 0;
    virtual void set_local_test_folder(const std::string& folder) = 0;
    // The last check of the second folder (kept: it reads that copy's 166 MB game
    // exe); refresh_local_test() checks again (the Local test tab or Settings >
    // Debug coming into view, Check again on the tab). recheck() refreshes it too.
    // The check runs on its own thread: `checking` until tick() has its result.
    virtual LocalTestCheck check_local_test() const = 0;
    virtual void refresh_local_test() = 0;
    // Starts the test on its own thread (it hashes and copies files, takes the
    // play locks and waits for both games): true once it is under way, false
    // with `error_out` when the check refuses it. The phase is in_game
    // (session_role() host) from then until both games have exited and the test
    // files in both folders are put back. While it starts, local_test() is
    // active and starting. A refusal found then ends it: local_test() is not
    // active, failed, and its detail says why. A failed start that leaves a game
    // running keeps the stopped run (local_test() active and failed).
    virtual bool start_local_test(std::string& error_out) = 0;
    virtual LocalTestView local_test() const = 0;
    virtual void arrange_local_test() = 0;   // places both game windows again
};

std::unique_ptr<LauncherModel> MakeMockModel();
std::unique_ptr<LauncherModel> MakeRealModel(const std::wstring& exe_dir);   // B

}  // namespace ddt
