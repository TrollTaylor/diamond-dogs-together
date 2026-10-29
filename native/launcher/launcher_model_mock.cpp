// Diamond Dogs Together: the simulated LauncherModel (see launcher_model_mock.hpp).
#include "launcher_model_mock.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <random>

#include "ddt_launcher_version.h"
#include "launcher_join_code.hpp"
#include "launcher_ui_logic.hpp"

namespace ddt {
namespace {
constexpr int kSetupSteps = 4;
constexpr double kSetupDuration[kSetupSteps] = {1.2, 1.8, 1.0, 6.0};
constexpr int kFailStep = 3;
constexpr float kFailProgress = 0.62f;
constexpr double kLaunch = 4.0;
constexpr double kConnectAt = 5.0;
constexpr double kHandoffStart = 14.0;
constexpr double kHandoffEnd = 26.0;
constexpr double kDropAt = 34.0;
constexpr double kGameLength = 90.0;
constexpr double kAfterGame = 5.0;
constexpr double kLocalPrepare = 2.0;   // "Starting the local test..." (both folders made ready, games started)
constexpr double kLocalStartup = 8.0;
constexpr double kLocalLength = 120.0;

const char* const kStepLabel[kSetupSteps] = {
    "Check the game",
    "Back up your saves",
    "Make the co-op save",
    "Build the co-op game files",
};
const char* const kStepRunning[kSetupSteps] = {
    "Checking MGSV and Steam...",
    "Copying your Steam saves into the mgs-coop folder...",
    "Making a separate copy of your save for co-op...",
    "Reading the game's own files. This takes about a minute.",
};
const char* const kStepDone[kSetupSteps] = {
    "MGSV and Steam are ready.",
    "Your own saves are backed up. They are never changed.",
    "Co-op plays on its own copy of your save.",
    "Built from your game files and checked.",
};
const char kStepFailed[] =
    "One of the game's files is not the version co-op expects. In Steam, right-click MGSV > Properties > "
    "Installed Files > Verify integrity, then try again.";
const char kSteamOffline[] = "Steam isn't running, or you're not logged in. Start Steam, log in, then try again.";
const char kGameUpdated[] =
    "MGSV was updated by Steam, so co-op can't start safely. Diamond Dogs Together needs an update for the new "
    "game version.";
const char kSessionBusy[] = "Close the game first.";
const char kNotReady[] = "Co-op is still getting ready. Try again in a moment.";
const char kLoaderGone[] =
    "dinput8.dll is missing from the game folder. Extract the whole Diamond Dogs Together zip into the game folder again.";
const char kSteamDir[] = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\MGS_TPP";
const char kDownloadsDir[] = "C:\\Users\\Player\\Downloads\\DiamondDogsTogether";

std::string Lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string DesktopFile(const std::string& name) {
    char* profile = nullptr;
    std::size_t size = 0;
    std::string dir = "C:\\Users\\Player";
    if (_dupenv_s(&profile, &size, "USERPROFILE") == 0 && profile && *profile) dir = profile;
    std::free(profile);
    return dir + "\\Desktop\\" + name;
}
}  // namespace

bool ParseMockScenario(const std::string& name, MockScenario& out) {
    static const struct {
        const char* name;
        MockScenario scenario;
    } kNames[] = {
        {"first-run", MockScenario::first_run},     {"ready", MockScenario::ready},
        {"wrong-folder", MockScenario::wrong_folder}, {"setup-fails", MockScenario::setup_fails},
        {"error", MockScenario::error},             {"steam-offline", MockScenario::steam_offline},
        {"partner-drops", MockScenario::partner_drops},
    };
    for (const auto& n : kNames)
        if (name == n.name) {
            out = n.scenario;
            return true;
        }
    return false;
}

MockModel::MockModel(const MockOptions& options) : options_(options) {
    if (!(options_.speed > 0.0)) options_.speed = 1.0;
    epoch_ = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    switch (options_.scenario) {
    case MockScenario::first_run:
    case MockScenario::setup_fails: phase_ = Phase::needs_setup; break;
    case MockScenario::wrong_folder: phase_ = Phase::not_in_game_folder; break;
    case MockScenario::error:
        phase_ = Phase::error;
        last_error_ = kGameUpdated;
        break;
    case MockScenario::ready:
    case MockScenario::steam_offline:
    case MockScenario::partner_drops: phase_ = Phase::ready; break;
    }
}

std::string MockModel::PartnerCode() {
    return FormatJoinCode("7QK2MX9RT4PB8WZ3HCNA", kPartnerSteamId);
}

double MockModel::now() const {
    if (manual_clock_) return manual_now_;
    const double real = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return (real - epoch_) * options_.speed;
}

void MockModel::use_manual_clock(double seconds) {
    manual_clock_ = true;
    manual_now_ = seconds;
}

void MockModel::advance(double seconds) {
    if (manual_clock_) {
        manual_now_ += seconds;
    } else {
        epoch_ -= seconds / options_.speed;
    }
}

void MockModel::SetPhase(Phase phase) { phase_ = phase; }

Phase MockModel::phase() const { return phase_; }

std::string MockModel::version() const { return DDT_VERSION_STRING; }

std::string MockModel::game_dir() const {
    return phase_ == Phase::not_in_game_folder ? kDownloadsDir : kSteamDir;
}

std::string MockModel::discovered_game_dir() const { return kSteamDir; }

std::vector<SetupStep> MockModel::setup_steps() const {
    std::vector<SetupStep> steps(kSetupSteps);
    for (int i = 0; i < kSetupSteps; ++i) steps[static_cast<std::size_t>(i)].label = kStepLabel[i];
    if (phase_ == Phase::setting_up) {
        double t = now() - setup_start_;
        for (int i = 0; i < kSetupSteps; ++i) {
            SetupStep& s = steps[static_cast<std::size_t>(i)];
            if (t >= kSetupDuration[i]) {
                s.state = SetupStep::State::done;
                s.progress = 1.0f;
                s.detail = kStepDone[i];
            } else if (t >= 0.0) {
                s.state = SetupStep::State::running;
                s.progress = static_cast<float>(t / kSetupDuration[i]);
                s.detail = kStepRunning[i];
            }
            t -= kSetupDuration[i];
        }
        return steps;
    }
    if (setup_failed_ && !last_setup_.empty()) return last_setup_;
    if (phase_ == Phase::needs_setup || phase_ == Phase::not_in_game_folder) return steps;
    for (int i = 0; i < kSetupSteps; ++i) {
        SetupStep& s = steps[static_cast<std::size_t>(i)];
        s.state = SetupStep::State::done;
        s.progress = 1.0f;
        s.detail = kStepDone[i];
    }
    return steps;
}

void MockModel::start_setup() {
    if (phase_ != Phase::needs_setup) return;
    ++setup_attempts_;
    setup_failed_ = false;
    last_setup_.clear();
    last_error_.clear();
    setup_start_ = now();
    SetPhase(Phase::setting_up);
}

Character MockModel::character() const { return character_; }
void MockModel::set_character(Character character) { character_ = character; }
std::string MockModel::hud() const { return hud_; }
void MockModel::set_hud(const std::string& hud) { hud_ = ui::HudValue(ui::HudIndex(hud)); }

bool MockModel::host(std::string& code_out, std::string& error_out) {
    code_out.clear();
    if (phase_ != Phase::ready) {
        error_out = ui::SessionActive(phase_) ? "A game is already running." : kNotReady;
        return false;
    }
    if (options_.scenario == MockScenario::steam_offline) {
        error_out = last_error_ = kSteamOffline;
        return false;
    }
    std::random_device rd;
    std::string secret;
    static const char kAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";
    for (int i = 0; i < 20; ++i) secret.push_back(kAlphabet[rd() & 31u]);
    code_out = FormatJoinCode(secret, kOwnSteamId);
    session_ = Session{Role::host, false, now(), code_out};
    last_error_.clear();
    SetPhase(Phase::launching);
    return true;
}

CodeCheck MockModel::check_code(const std::string& code) const { return CheckJoinCode(code, kOwnSteamId); }

bool MockModel::join(const std::string& code_or_empty_for_invite, std::string& error_out) {
    if (phase_ != Phase::ready) {
        error_out = ui::SessionActive(phase_) ? "A game is already running." : kNotReady;
        return false;
    }
    const bool invite = code_or_empty_for_invite.empty();
    if (!invite) {
        const CodeCheck check = check_code(code_or_empty_for_invite);
        if (!check.ok) {
            error_out = check.message.empty() ? "Paste the code your partner sent you." : check.message;
            return false;
        }
    }
    if (options_.scenario == MockScenario::steam_offline) {
        error_out = last_error_ = kSteamOffline;
        return false;
    }
    session_ = Session{Role::join, invite, now()};
    last_error_.clear();
    SetPhase(Phase::launching);
    return true;
}

double MockModel::GameSeconds() const { return now() - session_.start - kLaunch; }

LinkView MockModel::link() const {
    LinkView v;
    if (!ui::SessionActive(phase_)) return v;
    if (local_active_) {
        const double g = now() - local_start_;
        v.headline = g < kLocalStartup ? "Local test: Starting co-op in the game..." : "Local test: Connected to your partner";
        if (g < kLocalPrepare) v.headline = "Starting the local test...";
        v.connected = g >= kLocalStartup;
        v.partner_name = g >= kLocalStartup ? "Guest" : "";
        v.partner_mission = g >= kLocalStartup ? "Mother Base" : "";
        v.ping_ms = g >= kLocalStartup ? 3 : 0;
        v.detail = local_test().detail;
        return v;
    }
    const bool host = session_.role == Role::host;
    const std::string partner = host ? "Kaz" : "Venom";
    if (phase_ == Phase::launching) {
        v.headline = "Starting MGSV...";
        v.detail = "Steam is starting the game. This can take a minute.";
        return v;
    }
    const double g = GameSeconds();
    if (phase_ == Phase::after_game) {
        const double a = g - kGameLength;
        v.headline = "The game has closed";
        v.detail = a < 3.0 ? "Tidying up the co-op files..." : "All tidied up. Your own saves were not touched.";
        v.partner_name = partner;
        return v;
    }
    if (g < kConnectAt) {
        if (host) {
            v.headline = "Waiting for your partner";
            v.detail = "Send your partner the join code, or invite them from the Steam overlay (Shift+Tab) in the game.";
        } else if (session_.invite) {
            v.headline = "Waiting for an invite";
            v.detail = "Accept your partner's invite from the Steam overlay (Shift+Tab) in the game.";
        } else {
            v.headline = "Connecting to your partner";
            v.detail = "Connecting through Steam. This usually takes a few seconds.";
        }
        return v;
    }
    v.partner_name = partner;
    v.partner_mission = g < 40.0 ? "Mother Base" : "Afghanistan free roam";
    if (options_.scenario == MockScenario::partner_drops && g >= kDropAt) {
        v.headline = partner + " left the game";
        v.detail = host ? "Waiting for them to come back. They can join again with the same code."
                        : "Waiting for the host to come back. If they start a new game, ask them for the new code.";
        v.partner_mission.clear();
        return v;
    }
    v.connected = true;
    const int spike = (static_cast<int>(g) % 11 == 0) ? 31 : 0;
    v.ping_ms = 44 + static_cast<int>(std::lround(9.0 * std::sin(g * 0.7))) + spike;
    if (g >= kHandoffStart && g < kHandoffEnd) {
        v.handoff_active = true;
        v.handoff_progress = static_cast<float>((g - kHandoffStart) / (kHandoffEnd - kHandoffStart));
        if (host) {
            v.headline = "Sending your save to " + partner;
            v.detail = "So you both continue from the same point in the story. This takes a few seconds.";
        } else {
            v.headline = "Getting the host's save";
            v.detail = "So you both continue from the same point in the story. Your own co-op save is kept: "
                       "Tools > Restore previous brings it back.";
        }
        return v;
    }
    v.headline = "Connected to " + partner;
    if (g < kHandoffStart)
        v.detail = "You're both in Mother Base. Your partner appears when you're in the same area.";
    else if (host)
        v.detail = partner + " now plays on your save. Meet up in Mother Base or in free roam.";
    else
        v.detail = "You now play on the host's save. Meet up in Mother Base or in free roam.";
    return v;
}

std::string MockModel::last_error() const { return last_error_; }

namespace {
std::string Stamp(bool fixed) {
    if (fixed) return "20261023-153000";
    const std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &t);
    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &local);
    return buf;
}
}  // namespace

bool MockModel::export_save(std::string& path_out, std::string& error_out) {
    path_out.clear();
    if (ui::SessionActive(phase_)) {
        error_out = "Close the game first, then share your save.";
        return false;
    }
    if (phase_ != Phase::ready && phase_ != Phase::error) {
        error_out = "There is no co-op save yet. Wait until co-op is ready, then try again.";
        return false;
    }
    path_out = DesktopFile("mgs-coop-save-" + Stamp(manual_clock_) + ".zip");
    return true;
}

bool MockModel::import_save(const std::string& zip, std::string& error_out) {
    if (ui::SessionActive(phase_)) {
        error_out = "Close the game first, then load the save.";
        return false;
    }
    if (zip.empty()) {
        error_out = "Choose the save file your partner sent you.";
        return false;
    }
    if (Lower(zip).find("bad") != std::string::npos) {
        error_out = "That file is not a co-op save from Diamond Dogs Together. Nothing was changed.";
        return false;
    }
    has_previous_save_ = true;
    return true;
}

bool MockModel::restore_save(std::string& error_out) {
    if (ui::SessionActive(phase_)) {
        error_out = kSessionBusy;
        return false;
    }
    if (!has_previous_save_) {
        error_out = "There is no earlier co-op save to bring back.";
        return false;
    }
    has_previous_save_ = false;
    return true;
}

bool MockModel::collect_logs(std::string& path_out, std::string& error_out) {
    path_out.clear();
    if (phase_ == Phase::not_in_game_folder) {
        error_out = "Start Diamond Dogs Together from your game folder first.";
        return false;
    }
    const char* role = session_.role == Role::host ? "host" : "join";
    path_out = DesktopFile(std::string("mgs-coop-logs-") + role + "-" + Stamp(manual_clock_) + ".zip");
    return true;
}

bool MockModel::uninstall(std::string& report_out, std::string& error_out) {
    report_out.clear();
    if (ui::SessionActive(phase_) || phase_ == Phase::setting_up) {
        error_out = kSessionBusy;
        return false;
    }
    report_out =
        "Co-op is turned off and the mod's loader is removed from your game folder.\n"
        "Your own saves match the backup made when co-op was set up.\n"
        "Kept: your co-op save and the backups in the mgs-coop folder.\n"
        "To remove everything, delete DiamondDogsTogether.exe and the mgs-coop folder.";
    // Like the real model: the loader is gone, so co-op can't start (and is not set up again by itself).
    setup_failed_ = false;
    last_setup_.clear();
    last_error_ = kLoaderGone;
    SetPhase(Phase::error);
    return true;
}

void MockModel::tick() {
    const double t = now();
    // The setup runs by itself (2026-10-29); after a failed attempt it waits for Try again.
    if (phase_ == Phase::needs_setup && !setup_failed_) {
        start_setup();
        return;
    }
    if (local_active_) {
        if (t - local_start_ >= kLocalLength) {
            local_active_ = false;
            local_last_ = LocalTestView{};
            local_last_.headline = "Local test ended";
            local_last_.detail =
                "Local test ended. The test settings in both folders were put back; the second folder keeps this version's co-op files.";
            SetPhase(Phase::ready);
        }
        return;
    }
    if (phase_ == Phase::setting_up) {
        const double elapsed = t - setup_start_;
        if (options_.scenario == MockScenario::setup_fails && setup_attempts_ == 1) {
            double fail_at = kFailProgress * kSetupDuration[kFailStep];
            for (int i = 0; i < kFailStep; ++i) fail_at += kSetupDuration[i];
            if (elapsed >= fail_at) {
                std::vector<SetupStep> steps = setup_steps();
                SetupStep& s = steps[kFailStep];
                s.state = SetupStep::State::failed;
                s.progress = kFailProgress;
                s.detail = kStepFailed;
                last_setup_ = steps;
                setup_failed_ = true;
                last_error_ = kStepFailed;
                SetPhase(Phase::needs_setup);
            }
            return;
        }
        double total = 0.0;
        for (double d : kSetupDuration) total += d;
        if (elapsed >= total) SetPhase(Phase::ready);
        return;
    }
    if (ui::SessionActive(phase_)) {
        const double s = t - session_.start;
        if (s < kLaunch)
            SetPhase(Phase::launching);
        else if (s < kLaunch + kGameLength)
            SetPhase(Phase::in_game);
        else if (s < kLaunch + kGameLength + kAfterGame)
            SetPhase(Phase::after_game);
        else
            SetPhase(Phase::ready);
    }
}

bool MockModel::cancel_launch(std::string& error_out) {
    if (phase_ != Phase::launching) {
        error_out = "The game is already running. Quit it from the game's menu.";
        return false;
    }
    SetPhase(Phase::ready);
    return true;
}

void MockModel::recheck() {
    if (phase_ == Phase::error) {
        last_error_.clear();
        SetPhase(Phase::ready);
    } else if (phase_ == Phase::not_in_game_folder) {
        SetPhase(Phase::needs_setup);
    }
}

void MockModel::jump_to_setup(int step, float progress, bool failed) {
    local_active_ = false;
    if (step < 0) step = 0;
    if (step >= kSetupSteps) step = kSetupSteps - 1;
    double elapsed = progress * kSetupDuration[step];
    for (int i = 0; i < step; ++i) elapsed += kSetupDuration[i];
    setup_attempts_ = failed ? 1 : 2;
    setup_failed_ = false;
    last_setup_.clear();
    last_error_.clear();
    setup_start_ = now() - elapsed;
    SetPhase(Phase::setting_up);
    if (failed) {
        std::vector<SetupStep> steps = setup_steps();
        SetupStep& s = steps[static_cast<std::size_t>(step)];
        s.state = SetupStep::State::failed;
        s.detail = kStepFailed;
        last_setup_ = steps;
        setup_failed_ = true;
        last_error_ = kStepFailed;
        SetPhase(Phase::needs_setup);
    }
}

void MockModel::jump_to_phase(Phase phase) {
    local_active_ = false;
    local_last_ = LocalTestView{};
    setup_failed_ = false;
    last_setup_.clear();
    last_error_ = phase == Phase::error ? kGameUpdated : "";
    SetPhase(phase);
}

void MockModel::jump_to_session(Role role, bool invite, Phase phase, double seconds) {
    local_active_ = false;
    double offset = seconds;
    if (phase == Phase::in_game) offset += kLaunch;
    if (phase == Phase::after_game) offset += kLaunch + kGameLength;
    session_ = Session{role, invite, now() - offset};
    setup_failed_ = false;
    last_error_.clear();
    SetPhase(Phase::launching);
    tick();
}

Role MockModel::session_role() const { return session_.role; }

std::string MockModel::join_code() const {
    return ui::SessionActive(phase_) && session_.role == Role::host ? session_.code : std::string();
}


UpdateInfo MockModel::update_info() const {
    return update_info_;
}

void MockModel::check_for_updates() {
    update_info_.status = UpdateStatus::up_to_date;
}

void MockModel::start_update() {
    // Like the real model: not while co-op gets ready, a game runs or a local test runs.
    if (ui::SessionActive(phase_) || phase_ == Phase::setting_up || phase_ == Phase::not_in_game_folder) return;
    update_info_.status = UpdateStatus::downloading;
}

void MockModel::cancel_update() {
    update_info_.status = UpdateStatus::idle;
}

void MockModel::set_mock_update_info(const UpdateInfo& info) {
    update_info_ = info;
}

bool MockModel::debug() const { return debug_; }
void MockModel::set_debug(bool on) { debug_ = on; }
std::string MockModel::local_test_folder() const { return local_folder_; }
void MockModel::set_local_test_folder(const std::string& folder) { local_folder_ = folder; }

LocalTestCheck MockModel::check_local_test() const {
    if (!debug_) return {false, "Turn on Debug mode in Settings first."};
    if (local_active_) return {false, "A local test is running."};
    if (ui::SessionActive(phase_)) return {false, "Close the game first."};
    if (phase_ == Phase::needs_setup || phase_ == Phase::setting_up) return {false, "Co-op is still getting ready."};
    if (phase_ != Phase::ready) return {false, last_error_.empty() ? std::string("Co-op can't start in this folder right now.") : last_error_};
    if (local_folder_.empty()) return {false, "Choose the second MGSV game folder in Settings > Debug."};
    if (mock_checking_) return {false, "Checking the second folder...", true};
    if (Lower(local_folder_) == Lower(game_dir()) || Lower(local_folder_).rfind(Lower(game_dir()) + "\\", 0) == 0)
        return {false, "Choose a second, separate MGSV folder: not this game folder, and neither folder inside the other."};
    if (Lower(local_folder_).find("mgs_tpp") == std::string::npos)
        return {false, "There is no mgsvtpp.exe in that folder. Choose a separate MGSV game folder (a full copy of the game)."};
    return {true, "Ready. This version's co-op loader and runtime file are copied into it when the test starts."};
}

void MockModel::refresh_local_test() {}

bool MockModel::start_local_test(std::string& error_out) {
    const LocalTestCheck check = check_local_test();
    if (!check.ok) {
        error_out = check.message;
        return false;
    }
    local_active_ = true;
    local_start_ = now();
    local_last_ = LocalTestView{};
    session_ = Session{Role::host, false, now(), std::string()};
    last_error_.clear();
    SetPhase(Phase::in_game);
    return true;
}

LocalTestView MockModel::local_test() const {
    if (!local_active_) return local_last_;
    const double g = now() - local_start_;
    LocalTestView v;
    v.active = true;
    if (g < kLocalPrepare) {
        v.starting = true;
        v.headline = "Starting the local test...";
        v.detail = "Getting both game folders ready, then starting both games. This can take a minute.";
    } else if (g < kLocalStartup) {
        v.headline = "Starting both games...";
        v.detail = "Host: waiting for the game. Guest: waiting for the game.";
    } else {
        v.headline = "Local test running";
        v.detail = "Host: connected | ping 3 ms | partner Guest | Mother Base. Guest: connected | ping 3 ms | partner Venom | Mother Base.";
    }
    return v;
}

void MockModel::arrange_local_test() {}

void MockModel::set_mock_checking(bool on) { mock_checking_ = on; }

void MockModel::jump_to_local_test(double seconds) {
    jump_to_phase(Phase::ready);
    debug_ = true;
    if (local_folder_.empty()) local_folder_ = kGuestDir;
    std::string error;
    start_local_test(error);
    local_start_ = now() - seconds;
}

std::unique_ptr<LauncherModel> MakeMockModel(const MockOptions& options) {
    return std::make_unique<MockModel>(options);
}

std::unique_ptr<LauncherModel> MakeMockModel() { return MakeMockModel(MockOptions{}); }

}  // namespace ddt
