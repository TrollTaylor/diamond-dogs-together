#pragma once
// Diamond Dogs Together: the MOCK LauncherModel. It simulates every phase on
// a clock so the GUI shell runs without the core (wp/launcher-core):
// first-run setup with progress, host codes, join-code checks (the real MGS2
// parser), the game starting, the link status with a save handoff, the game
// closing, the Tools results and errors. It never touches the disk, Steam,
// the registry or the game.
//
// Timeline at speed 1 (seconds): setup 1.2 + 1.8 + 1.0 + 6.0 (it starts by
// itself on the first tick while needed, like the real model); the game starts
// 4 s after Host/Join; the partner connects 5 s into the game; a save handoff
// runs from 14 s to 26 s; the game closes by itself after 90 s; tidying up
// takes 5 s, then the launcher is ready again.
#include <cstdint>
#include <string>
#include <vector>

#include "launcher_model.hpp"

namespace ddt {

enum class MockScenario {
    first_run,       // needs_setup (the default); the first tick starts the setup
    ready,           // already set up
    wrong_folder,    // not_in_game_folder; "Check again" moves on to needs_setup
    setup_fails,     // the last setup step fails once (it waits for Try again); the second try works
    error,           // error phase; "Check again" fixes it
    steam_offline,   // Host and Join fail with the Steam sentence
    partner_drops,   // the partner leaves 34 s into the game
};
bool ParseMockScenario(const std::string& name, MockScenario& out);

struct MockOptions {
    MockScenario scenario = MockScenario::first_run;
    double speed = 1.0;   // > 1 runs the simulation faster
};

class MockModel final : public LauncherModel {
public:
    explicit MockModel(const MockOptions& options);

    Phase phase() const override;
    std::string version() const override;
    std::string game_dir() const override;
    std::vector<SetupStep> setup_steps() const override;
    void start_setup() override;
    Character character() const override;
    void set_character(Character character) override;
    std::string hud() const override;
    void set_hud(const std::string& hud) override;
    bool host(std::string& code_out, std::string& error_out) override;
    CodeCheck check_code(const std::string& code) const override;
    bool join(const std::string& code_or_empty_for_invite, std::string& error_out) override;
    LinkView link() const override;
    std::string last_error() const override;
    bool export_save(std::string& path_out, std::string& error_out) override;
    bool import_save(const std::string& zip, std::string& error_out) override;
    bool restore_save(std::string& error_out) override;
    bool collect_logs(std::string& path_out, std::string& error_out) override;
    bool uninstall(std::string& report_out, std::string& error_out) override;
    void tick() override;
    std::string discovered_game_dir() const override;
    bool cancel_launch(std::string& error_out) override;
    void recheck() override;
    Role session_role() const override;
    std::string join_code() const override;

    // ---- Update checking & auto-download ------------------------------------
    UpdateInfo update_info() const override;
    void check_for_updates() override;
    void start_update() override;
    void cancel_update() override;
    void set_mock_update_info(const UpdateInfo& info);

    // ---- Debug mode and the local two-window test ----------------------------
    // The second folder is accepted when it is kGuestDir (or another folder
    // named like an MGSV copy); a run lasts 120 s of the simulated clock.
    bool debug() const override;
    void set_debug(bool on) override;
    std::string local_test_folder() const override;
    void set_local_test_folder(const std::string& folder) override;
    LocalTestCheck check_local_test() const override;
    bool start_local_test(std::string& error_out) override;
    LocalTestView local_test() const override;
    void arrange_local_test() override;
    static constexpr const char kGuestDir[] = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\MGS_TPP_guest";
    // A local test `seconds` after Start local test.
    void jump_to_local_test(double seconds);

    // ---- Hooks for tests and the screenshot driver -------------------------
    // The player's own (synthetic) SteamID64 and a valid partner code.
    static constexpr std::uint64_t kOwnSteamId = 76561197960265728ull + 104729ull;
    static constexpr std::uint64_t kPartnerSteamId = 76561197960265728ull + 7919ull;
    static std::string PartnerCode();
    // Freezes the clock at `seconds` (it then moves only with advance()).
    void use_manual_clock(double seconds);
    void advance(double seconds);
    double now() const;
    // Jumps: setup at `step` (0..3) with `progress`, optionally failed there.
    void jump_to_setup(int step, float progress, bool failed);
    void jump_to_phase(Phase phase);   // ready, needs_setup, not_in_game_folder, error
    // A session as `role` (invite = joined by waiting for an invite), `seconds`
    // into `phase` (launching, in_game or after_game).
    void jump_to_session(Role role, bool invite, Phase phase, double seconds);

private:
    struct Session {
        Role role = Role::host;
        bool invite = false;
        double start = 0.0;   // when Host/Join was pressed
        std::string code;     // the host's code (host() only)
    };
    double GameSeconds() const;   // since the game started (negative while launching)
    void SetPhase(Phase phase);

    MockOptions options_;
    bool manual_clock_ = false;
    double manual_now_ = 0.0;
    double epoch_ = 0.0;

    Phase phase_ = Phase::needs_setup;
    double setup_start_ = -1.0;
    int setup_attempts_ = 0;
    std::vector<SetupStep> last_setup_;   // the steps of a failed attempt
    bool setup_failed_ = false;
    Session session_;
    Character character_ = Character::story;
    std::string hud_ = "compact";
    std::string last_error_;
    bool has_previous_save_ = true;
    UpdateInfo update_info_;
    bool debug_ = false;
    std::string local_folder_;
    bool local_active_ = false;
    double local_start_ = 0.0;
    LocalTestView local_last_;
};

std::unique_ptr<LauncherModel> MakeMockModel(const MockOptions& options);

}  // namespace ddt
