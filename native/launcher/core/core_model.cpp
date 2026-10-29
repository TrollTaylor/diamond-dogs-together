// Diamond Dogs Together launcher core: the real LauncherModel (see core_model.hpp).
//
// The window is the "launcher" of a session (play-session.json launcher_pid);
// the hidden helper started at activation (--watch) is its "watchdog". Both
// follow the game: whoever sees the game exit first applies a save handoff
// under the per-folder mutex (the claim in play-session.json makes exactly one
// of them apply it), and the cleanup is idempotent and hash-checked.
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "core_model.hpp"

#include <atomic>
#include <cmath>
#include <mutex>
#include <thread>

#include "core_install.hpp"
#include "core_log.hpp"
#include "core_saves.hpp"
#include "core_session.hpp"
#include "core_text.hpp"
#include "core_update.hpp"
#include "launcher/runtime_dat.hpp"
#include "launcher_join_code.hpp"

namespace ddt::core {

std::string CharacterName(Character c) {
    switch (c) {
    case Character::story: return "story";
    case Character::ocelot: return "ocelot";
    case Character::dd_male: return "dd_male";
    case Character::dd_female: return "dd_female";
    case Character::avatar: return "avatar";
    }
    return "dd_male";
}

bool CharacterFromName(const std::string& name, Character& out) {
    if (name == "story") out = Character::story;
    else if (name == "ocelot") out = Character::ocelot;
    else if (name == "dd_male") out = Character::dd_male;
    else if (name == "dd_female") out = Character::dd_female;
    else if (name == "avatar") out = Character::avatar;
    else return false;
    return true;
}

// The In game page's view of link-status.json (plain language; the
// PowerShell status line is FormatStatusLine).
LinkView LinkViewFromStatus(const json::Value& status, bool host, std::int64_t now_ms, const std::string& own_build) {
    LinkView v;
    if (!status.is_object()) {
        v.headline = "Starting co-op in the game...";
        v.detail = "MGSV is loading. The connection shows up here once the game reaches its title screen.";
        return v;
    }
    const std::string state = status.Str("state", "off");
    std::int64_t t = 0;
    if (state != "disabled" && ParseUtc(status.Str("time_utc"), t)) {
        const double age = static_cast<double>(now_ms - t) / 1000.0;
        if (age > 15) {
            v.headline = "The game is not responding";
            v.detail = "No status from MGSV for " + std::to_string(static_cast<long long>(std::nearbyint(age))) +
                       " seconds. If the game froze, close it; co-op switches off when it exits.";
            return v;
        }
    }
    std::vector<std::string> detail;
    const bool steam_busy = status.Str("steam", "ok") == "acquiring";
    const std::string reason = status.Str("reason");
    const bool verified = status.Int("verified", 1) != 0;
    if (state == "connected") {
        v.connected = true;
        v.headline = "Connected to your partner";
        const std::string route = status.Str("route");
        if (route == "relay") detail.push_back("Steam connects you through its relay servers.");
    } else if (steam_busy) {
        v.headline = "Connecting to Steam...";
    } else if (state == "listening") {
        v.headline = "Waiting for your partner";
        detail.push_back("Send your partner the join code, or invite them from the Steam overlay (Shift+Tab) in the game.");
        if (status.Int("accepting", 1) == 0)
            detail.push_back("Joins are turned off in the game: F9 > Partner / Co-op > Online > Accept joins.");
    } else if (state == "waiting") {
        v.headline = "Waiting for an invite";
        detail.push_back("Accept your partner's invite in the Steam overlay (Shift+Tab) or the Steam chat. You can also paste a code in "
                         "the game: F9 > Partner / Co-op > Online.");
    } else if (state == "connecting") {
        v.headline = host ? "Waiting for your partner" : "Connecting to your partner";
        if (!host) detail.push_back("Connecting through Steam. This usually takes a few seconds.");
    } else if (state == "reconnecting") {
        v.headline = "Reconnecting...";
    } else if (state == "lost") {
        v.headline = "The connection was lost";
    } else if (state == "rejected") {
        v.headline = "The connection was refused";
    } else if (state == "disabled") {
        v.headline = "Co-op could not start";
    } else {
        v.headline = "Co-op is starting...";
    }
    if (state == "connected" || state == "reconnecting" || state == "lost") {
        const std::int64_t rtt = status.Int("rtt_ms", 0);
        v.ping_ms = rtt > 0 ? static_cast<int>(std::min<std::int64_t>(rtt, 99999)) : 0;
    }
    if (status.Int("left", 0) && state != "connected") {
        if (host) {
            v.headline = "Your partner left";
            detail.insert(detail.begin(), "Waiting for them to come back. They can join again with the same code.");
        } else if (reason == "bad_code" && !verified) {
            v.headline = "The host started a new game";
            detail.insert(detail.begin(), "The old code no longer works: paste the host's new code in the game (F9 > Partner / Co-op > Online).");
        } else {
            v.headline = "The host left";
            detail.insert(detail.begin(), "Waiting for the host to come back.");
        }
    }
    if (!reason.empty()) detail.push_back(LinkReasonText(reason, status.Str("remote_build"), own_build, verified, Wording::launcher));
    const std::int64_t steam_end = status.Int("steam_end", 0);
    if (steam_end && state != "connected") detail.push_back(SteamEndText(steam_end, Wording::launcher));
    v.partner_name = status.Str("partner_name");
    const std::uint64_t mission = static_cast<std::uint64_t>(status.Int("mission", 0)) & 0xFFFFFFFFull;
    const std::uint64_t partner_mission = static_cast<std::uint64_t>(status.Int("partner_mission", 0)) & 0xFFFFFFFFull;
    if (partner_mission) v.partner_mission = MissionName(partner_mission);
    else if (state == "connected" && mission && mission != 1) v.partner_mission = MissionName(mission);
    const std::string token = status.Str("handoff", "off");
    const HandoffText ht = GetHandoffText(token, static_cast<int>(status.Int("handoff_pct", 0)), !host, Wording::launcher);
    const int pct = static_cast<int>(std::max<std::int64_t>(0, std::min<std::int64_t>(100, status.Int("handoff_pct", 0))));
    const bool busy_guest = HandoffBusyToken(token);
    const bool busy_host = token == "offered" || token == "sending" || token == "partner_staged" || token == "partner_restarting";
    if (busy_guest || busy_host) {
        v.handoff_active = true;
        if (token == "receiving" || token == "sending") v.handoff_progress = static_cast<float>(pct) / 100.0f;
        else if (token == "wait_host" || token == "offered") v.handoff_progress = 0.0f;
        else v.handoff_progress = 1.0f;
        if (token == "wait_host") v.headline = "Waiting for the host's save";
        else if (token == "receiving") v.headline = "Getting the host's save";
        else if (token == "staged" || token == "countdown" || token == "exiting") v.headline = "The host's save is ready";
        else if (token == "offered" || token == "sending") v.headline = "Sending your save to your partner";
        else v.headline = "Your partner is restarting on your save";
        detail.insert(detail.begin(), ht.line);
    } else if (!ht.line.empty()) {
        detail.push_back(ht.line);
    }
    const std::string wait = status.Str("pair_wait");
    if (state == "connected" && wait == "different_mission" && partner_mission) {
        detail.push_back("Your partner is in " + MissionName(partner_mission) +
                         ". Different missions never pair: meet in the same Mother Base, free roam or helicopter.");
    } else if (mission != 0 && mission != 1 && mission != 40010 && mission != 40020 && mission != 40050 && mission != 30010 &&
               mission != 30020 && mission != 30050) {
        detail.push_back("The partner body is not available in this mission; play together in Mother Base or free roam.");
    }
    if (!busy_guest && status.Str("progress") == "story_start")
        detail.push_back("Your co-op save is at the story start: quit, then import your partner's save (Tools > Share save > Import).");
    if (v.connected && detail.empty())
        detail.push_back("Meet up in Mother Base or free roam: your partner appears when you are in the same area.");
    for (const std::string& d : detail) {
        if (d.empty()) continue;
        if (!v.detail.empty()) v.detail += " ";
        v.detail += d;
    }
    return v;
}

namespace {

constexpr const char* kStepLabels[4] = {"Check the game", "Back up your real saves", "Make the co-op save copy",
                                        "Build the co-op runtime file"};
const char* const kLogNames[3] = {"early.log", "steam-observer.log", "script-observer.log"};

std::vector<SetupStep> FreshSteps(SetupStep::State state) {
    std::vector<SetupStep> steps;
    for (const char* label : kStepLabels) {
        SetupStep s;
        s.label = label;
        s.state = state;
        s.progress = state == SetupStep::State::done ? 1.0f : 0.0f;
        steps.push_back(s);
    }
    return steps;
}

struct Shared {
    std::mutex m;
    std::vector<SetupStep> steps = FreshSteps(SetupStep::State::pending);
    bool setup_running = false, setup_finished = false, setup_ok = false, setup_failed_once = false;
    std::string setup_error;
    std::atomic<bool> cancel{false};
    bool after_finished = false;
    HandoffOutcome after;
    bool applied = false;
    HandoffOutcome applied_info;
};

enum class Stage { none, launching, in_game, after_game, relaunch_wait };

std::string Join(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) {
        if (l.empty()) continue;
        if (!out.empty()) out += " ";
        out += l;
    }
    return out;
}

class RealModel final : public LauncherModel {
public:
    // no_launch: --write-session (developer): Host / Join write the session
    // files and stop there (no helper, no Steam, no clipboard).
    RealModel(const std::wstring& exe_dir, std::shared_ptr<Env> env, bool exit_if_busy, bool no_launch = false,
              const ModelStart& start = ModelStart{})
        : game_dir_(exe_dir), env_(std::move(env)), exit_if_busy_(exit_if_busy), no_launch_(no_launch), start_(start) {
        paths_ = GetInstancePaths(game_dir_);
        LoadSettings(paths_, settings_);
        // After an update the old launcher has closed (launcher_main waited for
        // it, or for the mutex of an updater before 0.14.4), so its exe can go
        // now; Windows may need a moment to let go of the exiting process's
        // image. Only then: a .old another program holds never slows a normal start.
        bool old_exe_gone = CleanOldExecutable(paths_.game);
        const bool just_updated = start_.after_update != 0 || start_.old_exe_recent;
        for (int i = 0; just_updated && !old_exe_gone && i < 30; ++i) {
            env_->SleepMs(100);
            old_exe_gone = CleanOldExecutable(paths_.game);
        }
        if (start_.after_update) updated_to_ = CoreVersion();
        // An update whose process was ended half way (*.ddt-new / *.ddt-old).
        const std::size_t half_done = IsFile(paths_.exe) ? CleanUpdateLeftovers(paths_) : 0;
        // 0.14.3 and older shipped mgs-coop\source.zip; the source is a release asset now.
        const LeftoverSource leftover = RemoveLeftoverSourceZip(paths_);
        update_info_.current_version = CoreVersion();
        update_info_.status = UpdateStatus::idle;
        Recheck();
        if (phase_ != Phase::not_in_game_folder) {
            Log("window=open; phase=" + PhaseName(phase_) + "; version=" + CoreBuildId());
            if (start_.after_update)
                Log("update=finished; from_pid=" + std::to_string(start_.after_update) + "; version=" + CoreBuildId() +
                    "; old_exe=" + (old_exe_gone ? "removed" : "kept"));
            if (half_done) Log("update=leftovers_cleaned; count=" + std::to_string(half_done));
            if (leftover == LeftoverSource::removed) Log("update=source_zip_removed");
        }
        if (phase_ != Phase::not_in_game_folder && !no_launch_) {
            check_for_updates();
        }
    }

    ~RealModel() override {
        update_cancel_ = true;
        if (update_thread_.joinable()) update_thread_.join();
        shared_->cancel = true;
        if (after_thread_.joinable()) after_thread_.join();
        if (stage_ != Stage::none && !session_id_.empty()) {
            json::Value s;
            if (ReadPlaySession(paths_, s) && SessionIsActive(s) && s.Str("session_id") == session_id_) {
                // The helper follows the game once the window is gone; never two helpers.
                if (!PlaySessionLive(*env_, paths_) && !StartWatchdogFor(*env_, paths_, session_id_) &&
                    !AnyGameRunningIn(*env_, paths_.game))
                    CleanupSession(*env_, paths_, false);
            }
            Log("window=closed; stage=" + StageName(stage_));
        }
        if (setup_thread_.joinable()) {
            bool running = false;
            {
                std::lock_guard<std::mutex> lock(shared_->m);
                running = shared_->setup_running;
            }
            if (running && exit_if_busy_) {
                // Closing the window mid-setup ends the process (and the setup
                // worker with it); the setup is resumable and starts over cleanly.
                Log("window=closed_during_setup");
                ExitProcess(0);
            }
            setup_thread_.join();
        }
    }

    Phase phase() const override { return phase_; }
    std::string session_id() const { return session_id_; }   // --write-session
    std::string version() const override { return CoreBuildId(); }
    std::string game_dir() const override { return Utf8(paths_.game); }

    std::vector<SetupStep> setup_steps() const override {
        std::lock_guard<std::mutex> lock(shared_->m);
        if (phase_ == Phase::ready) return FreshSteps(SetupStep::State::done);
        if (phase_ == Phase::setting_up || shared_->setup_failed_once) return shared_->steps;
        return FreshSteps(SetupStep::State::pending);
    }

    void start_setup() override {
        if (phase_ != Phase::needs_setup) return;
        // Never while an update downloads or installs: it replaces the files the setup uses.
        std::string busy;
        if (UpdateBusy(busy)) return;
        if (setup_thread_.joinable()) setup_thread_.join();
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            shared_->steps = FreshSteps(SetupStep::State::pending);
            shared_->setup_running = true;
            shared_->setup_finished = false;
            shared_->setup_failed_once = false;
            shared_->setup_error.clear();
        }
        last_error_.clear();
        phase_ = Phase::setting_up;
        auto shared = shared_;
        auto env = env_;
        const InstancePaths paths = paths_;
        const std::string character = CharacterName(character_);
        const std::string hud = hud_;
        setup_thread_ = std::thread([shared, env, paths, character, hud]() {
            std::string error;
            const bool ok = RunSetup(*env, paths, character, hud, [&](const StepReport& r) {
                std::lock_guard<std::mutex> lock(shared->m);
                if (r.index < 0 || r.index >= static_cast<int>(shared->steps.size())) return;
                SetupStep& s = shared->steps[static_cast<std::size_t>(r.index)];
                s.state = r.state == StepState::running ? SetupStep::State::running
                          : r.state == StepState::done  ? SetupStep::State::done
                          : r.state == StepState::failed ? SetupStep::State::failed
                                                         : SetupStep::State::pending;
                s.detail = r.detail;
                s.progress = r.progress;
            }, &shared->cancel, error);
            std::lock_guard<std::mutex> lock(shared->m);
            shared->setup_running = false;
            shared->setup_finished = true;
            shared->setup_ok = ok;
            shared->setup_error = error;
            if (!ok) shared->setup_failed_once = true;
        });
    }

    Character character() const override { return character_; }
    void set_character(Character c) override {
        character_ = c;
        PersistCharacter();
    }
    std::string hud() const override { return hud_; }
    void set_hud(const std::string& hud) override {
        if (hud != "compact" && hud != "full" && hud != "off") return;
        hud_ = hud;
        settings_.hud = hud;
        std::string error;
        if (IsDir(paths_.mod) && phase_ != Phase::not_in_game_folder) SaveSettings(paths_, settings_, error);
    }

    bool host(std::string& code_out, std::string& error_out) override {
        code_out.clear();
        if (UpdateBusy(error_out)) return false;
        if (!ReadyFor(error_out)) return false;
        if (!Preflight(error_out)) return false;
        const std::uint64_t self = OwnSteamId(*env_);
        if (!self) {
            error_out = last_error_ = "Steam is not running or not logged in. Start Steam and log in (online), then try again. Nothing was changed.";
            return false;
        }
        const std::string secret = NewSecret();
        const std::string code = FormatJoinCode(secret, self);
        if (secret.empty() || code.empty()) {
            error_out = last_error_ = "Windows could not make a join code (no random numbers). Restart the PC and try again.";
            return false;
        }
        role_ = Role::host;
        // The code's hash is recorded with the session; the cleanup clears the
        // clipboard only while it still holds exactly this code. --write-session
        // never touches the clipboard.
        if (!StartSession(true, secret, 0, false, self, no_launch_ ? std::string() : Sha256Hex(code), error_out)) return false;
        if (!no_launch_) env_->SetClipboardText(code);
        code_ = code;
        code_out = code;
        return true;
    }

    CodeCheck check_code(const std::string& code) const override { return CheckJoinCode(code, OwnSteamId(*env_)); }

    bool join(const std::string& code_or_empty, std::string& error_out) override {
        if (UpdateBusy(error_out)) return false;
        if (!ReadyFor(error_out)) return false;
        const bool wait = Trim(code_or_empty).empty();
        std::string secret;
        std::uint64_t peer = 0;
        const std::uint64_t self = OwnSteamId(*env_);
        if (!wait) {
            const CodeCheck check = CheckJoinCode(code_or_empty, self);
            if (!check.ok) {
                error_out = check.message.empty() ? "Paste the code your partner sent you." : check.message;
                return false;
            }
            if (!ParseJoinCode(code_or_empty, secret, peer)) {
                error_out = "The code has a typo. Copy it again, or ask your partner to send it again.";
                return false;
            }
        }
        if (!Preflight(error_out)) return false;
        if (!self) {
            error_out = last_error_ = "Steam is not running or not logged in. Start Steam and log in (online), then try again. Nothing was changed.";
            return false;
        }
        if (!wait && peer == self) {
            error_out = "That is your own code. Your partner enters it, not you.";
            return false;
        }
        role_ = Role::join;
        const bool ok = StartSession(false, secret, peer, wait, self, std::string(), error_out);
        std::fill(secret.begin(), secret.end(), '\0');
        return ok;
    }

    LinkView link() const override { return link_; }
    std::string last_error() const override { return last_error_; }

    bool export_save(std::string& path_out, std::string& error_out) override {
        path_out.clear();
        if (!ToolsAllowed("share your save", error_out)) return false;
        std::wstring zip;
        std::size_t files = 0;
        if (!ExportShareSave(*env_, paths_.coop_saves, env_->DesktopDir(), zip, files, error_out)) return false;
        path_out = Utf8(zip);
        Log("share=export; files=" + std::to_string(files));
        return true;
    }

    bool import_save(const std::string& zip, std::string& error_out) override {
        if (!ToolsAllowed("load the save", error_out)) return false;
        if (Trim(zip).empty()) {
            error_out = "Choose the save file your partner sent you.";
            return false;
        }
        std::wstring aside;
        std::size_t files = 0;
        if (!ImportShareSave(*env_, Wide(zip), paths_.mod, paths_.coop_saves, paths_.coop_saves_replaced, aside, files, error_out)) {
            Log("share=import_refused");
            return false;
        }
        Log("share=import; files=" + std::to_string(files));
        return true;
    }

    bool restore_save(std::string& error_out) override {
        if (!ToolsAllowed("bring an earlier save back", error_out)) return false;
        std::wstring from, aside;
        if (!RestoreShareSave(*env_, paths_.coop_saves, paths_.coop_saves_replaced, from, aside, error_out)) return false;
        Log("share=restore");
        return true;
    }

    bool collect_logs(std::string& path_out, std::string& error_out) override {
        path_out.clear();
        if (phase_ == Phase::not_in_game_folder) {
            error_out = "Start Diamond Dogs Together from your game folder first.";
            return false;
        }
        std::wstring zip;
        if (!CollectLogs(*env_, paths_, env_->DesktopDir(), zip, error_out)) return false;
        path_out = Utf8(zip);
        return true;
    }

    bool uninstall(std::string& report_out, std::string& error_out) override {
        report_out.clear();
        if (UpdateBusy(error_out)) return false;
        if (stage_ != Stage::none || phase_ == Phase::setting_up) {
            error_out = "Close the game first (co-op is still on), then try again.";
            return false;
        }
        if (phase_ == Phase::not_in_game_folder) {
            error_out = "Start Diamond Dogs Together from your game folder first.";
            return false;
        }
        if (!Uninstall(*env_, paths_, report_out, error_out)) return false;
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            shared_->setup_failed_once = false;
        }
        Recheck();
        return true;
    }

    void tick() override {
        const std::int64_t now = env_->NowMs();
        if (start_.update_now && !update_now_started_ && update_info().status == UpdateStatus::available) {
            // Developer --update-now (with a loopback --update-feed): install at once.
            update_now_started_ = true;
            Log("update=auto; reason=update_now");
            start_update();
        }
        if (phase_ == Phase::setting_up) {
            bool finished = false, ok = false;
            std::string error;
            {
                std::lock_guard<std::mutex> lock(shared_->m);
                finished = shared_->setup_finished;
                ok = shared_->setup_ok;
                error = shared_->setup_error;
                if (finished) shared_->setup_finished = false;
            }
            if (finished) {
                if (setup_thread_.joinable()) setup_thread_.join();
                LoadSettings(paths_, settings_);
                if (ok) {
                    PersistCharacter();
                    Recheck();
                } else {
                    phase_ = Phase::needs_setup;
                    last_error_ = error;
                }
            }
            return;
        }
        if (now < next_poll_) return;
        switch (stage_) {
        case Stage::none: return;
        case Stage::launching: PollLaunching(now); break;
        case Stage::in_game: PollInGame(now); break;
        case Stage::after_game: PollAfterGame(now); break;
        case Stage::relaunch_wait: PollRelaunch(now); break;
        }
        UpdateLink(now);
        next_poll_ = now + 250;
    }

    std::string discovered_game_dir() const override { return discovered_; }

    bool cancel_launch(std::string& error_out) override {
        if (stage_ != Stage::launching && stage_ != Stage::relaunch_wait) {
            error_out = "MGSV is not starting right now.";
            return false;
        }
        if (AnyGameRunningIn(*env_, paths_.game)) {
            error_out = "MGSV is already running. Quit it normally; co-op switches off when it exits.";
            return false;
        }
        const CleanupResult r = CleanupSession(*env_, paths_, true);
        Log("launch=cancelled");
        ResetSession();
        Recheck();
        if (phase_ == Phase::ready && !r.kept.empty()) last_error_ = Join(r.messages);
        return true;
    }

    void recheck() override {
        if (stage_ != Stage::none || phase_ == Phase::setting_up) return;
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            shared_->setup_failed_once = false;
        }
        Recheck();
    }

    Role session_role() const override { return role_; }
    std::string join_code() const override { return stage_ != Stage::none && role_ == Role::host ? code_ : std::string(); }

UpdateInfo update_info() const override {
        std::lock_guard<std::mutex> lock(update_mutex_);
        UpdateInfo info = update_info_;
        info.updated_to = updated_to_;
        return info;
    }

    void check_for_updates() override {
        {
            std::lock_guard<std::mutex> lock(update_mutex_);
            if (update_info_.status == UpdateStatus::checking || update_info_.status == UpdateStatus::downloading ||
                update_info_.status == UpdateStatus::applying || update_info_.status == UpdateStatus::ready_to_restart) {
                return;
            }
            update_info_.status = UpdateStatus::checking;
            update_info_.error.clear();
        }
        if (update_thread_.joinable()) update_thread_.join();
        const std::string cur = CoreVersion();
        const std::string feed = start_.update_feed;
        const InstancePaths paths = paths_;   // the thread never reads members the UI thread changes
        update_thread_ = std::thread([this, cur, feed, paths]() {
            UpdateInfo info;
            std::string error;
            if (CheckForUpdates(cur, info, error, feed)) {
                std::lock_guard<std::mutex> lock(update_mutex_);
                update_info_ = info;
            } else {
                LauncherLog(*env_, paths, "update=check_failed; error=" + error);
                std::lock_guard<std::mutex> lock(update_mutex_);
                update_info_.status = UpdateStatus::error;
                update_info_.download_url.clear();
                update_info_.error = "Could not check for updates. Check your internet connection, then try again.";
            }
        });
    }

    void start_update() override {
        const std::string blocked = UpdateBlocker();
        {
            std::lock_guard<std::mutex> lock(update_mutex_);
            if (update_info_.status != UpdateStatus::available &&
                update_info_.status != UpdateStatus::error) {
                return;
            }
            if (update_info_.download_url.empty()) return;
            if (!blocked.empty()) {
                update_info_.status = UpdateStatus::error;
                update_info_.error = blocked;
                return;
            }
        }
        // available / error are set by the previous check or download as its
        // very last step (a cancelled download stays "downloading" until it has
        // stopped), so this join is immediate and the cancel flag is free again.
        if (update_thread_.joinable()) update_thread_.join();
        {
            std::lock_guard<std::mutex> lock(update_mutex_);
            update_info_.status = UpdateStatus::downloading;
            update_info_.bytes_downloaded = 0;
            update_info_.progress = 0.0f;
            update_info_.error.clear();
            update_info_.cancelling = false;
            update_cancel_ = false;
        }
        const UpdateInfo target = [this]() {
            std::lock_guard<std::mutex> lock(update_mutex_);
            return update_info_;
        }();
        const InstancePaths paths = paths_;
        // The new exe waits for this window to close, and keeps a developer test feed.
        std::vector<std::wstring> relaunch_args = {L"--after-update", std::to_wstring(env_->CurrentPid())};
        if (!start_.update_feed.empty()) {
            relaunch_args.push_back(L"--update-feed");
            relaunch_args.push_back(Wide(start_.update_feed));
        }
        Log("update=start; version=" + target.latest_version + "; asset=" + target.asset_name);
        update_thread_ = std::thread([this, target, paths, relaunch_args]() {
            auto log = [this, &paths](const std::string& text) { LauncherLog(*env_, paths, text); };
            auto fail = [this](const std::string& sentence) {
                std::lock_guard<std::mutex> lock(update_mutex_);
                update_info_.status = UpdateStatus::error;
                update_info_.error = sentence;
                update_info_.cancelling = false;
            };
            const std::wstring dest_zip = JoinPath2(paths.mod, L"update.zip");
            std::string error;
            const bool ok = DownloadUpdate(target, dest_zip, [this](const UpdateProgress& p) {
                std::lock_guard<std::mutex> lock(update_mutex_);
                update_info_.bytes_downloaded = p.bytes_downloaded;
                update_info_.download_size = p.total_bytes;
                update_info_.progress = p.progress;
            }, &update_cancel_, error);
            if (update_cancel_) {   // Cancel: back to "available" once stopped, nothing changed
                DeleteFileChecked(dest_zip);
                std::lock_guard<std::mutex> lock(update_mutex_);
                update_info_.status = UpdateStatus::available;
                update_info_.cancelling = false;
                update_info_.error.clear();
                return;
            }
            if (!ok) {
                log("update=download_failed; error=" + error);
                DeleteFileChecked(dest_zip);
                fail("The update could not be downloaded. Check your internet connection, then try again.");
                return;
            }
            // The folder may have changed while the update downloaded (the setup
            // cannot start meanwhile, but MGSV or co-op can, from outside).
            if (const std::string reason = ApplyBlocker(paths); !reason.empty()) {
                log("update=refused; reason=" + reason);
                DeleteFileChecked(dest_zip);
                fail(reason == "game_running"    ? "MGSV was started while the update downloaded. Quit MGSV, then update again."
                     : reason == "session_live" ? "Co-op was switched on while the update downloaded. Quit MGSV, then update again."
                                                : "Co-op is still getting ready. Update once that has finished.");
                return;
            }
            {
                std::lock_guard<std::mutex> lock(update_mutex_);
                update_info_.status = UpdateStatus::applying;
                update_info_.cancelling = false;   // too late to cancel: it installs
            }
            // The restart is the last step of the install: if the new exe does
            // not start, ApplyUpdateZip puts the running version back.
            ApplyReport report;
            const bool installed = ApplyUpdateZip(paths, dest_zip, error, &report, [&](std::string& start_error) {
                log("update=applied; version=" + target.latest_version + "; files=" + std::to_string(report.files));
                return RelaunchSelf(paths, relaunch_args, start_error);
            });
            if (!installed) {
                DeleteFileChecked(dest_zip);
                while (!error.empty() && (error.back() == '.' || error.back() == ' ')) error.pop_back();
                const std::string restored = report.restored ? "yes" : "no";
                if (report.failure == ApplyFailure::restart_failed)
                    log("update=restart_failed; error=" + error + "; restored=" + restored);
                else
                    log("update=apply_failed; reason=" + ApplyFailureName(report.failure) + "; error=" + error + "; restored=" + restored);
                // The reason stays in the log (Collect logs); the player gets one plain sentence.
                fail(ApplyFailureSentence(report));
                return;
            }
            std::string removed;
            for (const std::string& r : report.removed) removed += (removed.empty() ? "" : ",") + r;
            // The new exe runs and waits for this window: the shell closes it now.
            log("update=handed_over" + (removed.empty() ? std::string() : "; removed=" + removed));
            std::lock_guard<std::mutex> lock(update_mutex_);
            update_info_.status = UpdateStatus::ready_to_restart;
        });
    }

    // The download stops at its next read; the update thread then sets
    // "available" itself, so Update now cannot start before it has stopped.
    void cancel_update() override {
        std::lock_guard<std::mutex> lock(update_mutex_);
        if (update_info_.status != UpdateStatus::downloading) return;
        update_cancel_ = true;
        update_info_.cancelling = true;
    }


private:
    static std::string PhaseName(Phase p) {
        switch (p) {
        case Phase::not_in_game_folder: return "not_in_game_folder";
        case Phase::needs_setup: return "needs_setup";
        case Phase::setting_up: return "setting_up";
        case Phase::ready: return "ready";
        case Phase::launching: return "launching";
        case Phase::in_game: return "in_game";
        case Phase::after_game: return "after_game";
        case Phase::error: return "error";
        }
        return "unknown";
    }
    static std::string StageName(Stage s) {
        switch (s) {
        case Stage::none: return "none";
        case Stage::launching: return "launching";
        case Stage::in_game: return "in_game";
        case Stage::after_game: return "after_game";
        case Stage::relaunch_wait: return "relaunch_wait";
        }
        return "unknown";
    }
    void Log(const std::string& text) { LauncherLog(*env_, paths_, text); }

    static std::string ApplyFailureName(ApplyFailure f) {
        switch (f) {
        case ApplyFailure::none: return "none";
        case ApplyFailure::damaged: return "damaged";
        case ApplyFailure::not_ours: return "not_ours";
        case ApplyFailure::old_exe_in_use: return "old_exe_in_use";
        case ApplyFailure::install_failed: return "install_failed";
        case ApplyFailure::restart_failed: return "restart_failed";
        }
        return "unknown";
    }
    // What the player reads when an update did not go through (the detail is in launcher.log).
    static std::string ApplyFailureSentence(const ApplyReport& r) {
        switch (r.failure) {
        case ApplyFailure::damaged: return "The download was damaged. Try the update again.";
        case ApplyFailure::not_ours: return "The download is not a Diamond Dogs Together update. Try again later.";
        case ApplyFailure::old_exe_in_use:
            return "A file from the last update is still in use. Close every Diamond Dogs Together window, then update again.";
        case ApplyFailure::restart_failed:
            if (r.restored)
                return "The new version would not start, so your current version was kept. Try again later, or get the latest zip "
                       "from the release page.";
            return "The update is installed, but the new version did not start. Close this window, then start Diamond Dogs Together "
                   "again. If it still does not start, extract the latest zip from the release page into the game folder again.";
        case ApplyFailure::none:
        case ApplyFailure::install_failed: break;
        }
        if (r.restored) return "The update could not be installed, so your current version was kept. Try again in a minute.";
        return "The update could not be installed completely. Extract the latest zip from the release page into the game folder, "
               "then start Diamond Dogs Together again.";
    }

    // Why the update cannot install now ("" = it can): never while a game or a
    // co-op session runs from this folder, or while the setup runs.
    std::string UpdateBlocker() {
        if (phase_ == Phase::setting_up) return "Co-op is still getting ready. Update once that has finished.";
        if (stage_ != Stage::none || phase_ == Phase::launching || phase_ == Phase::in_game || phase_ == Phase::after_game)
            return "Quit MGSV first. Diamond Dogs Together updates once the game is closed.";
        if (AnyGameRunningIn(*env_, paths_.game)) return "MGSV is running from this folder. Quit MGSV first, then update.";
        if (PlaySessionLive(*env_, paths_))
            return "Co-op is still on for this folder (another window or its helper). Quit MGSV first, then update.";
        return {};
    }

    // The same, from the update thread just before it installs: only what that
    // thread may read ("" = install; otherwise a token for launcher.log).
    std::string ApplyBlocker(const InstancePaths& paths) {
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            if (shared_->setup_running) return "setup_running";
        }
        if (AnyGameRunningIn(*env_, paths.game)) return "game_running";
        if (PlaySessionLive(*env_, paths)) return "session_live";
        return {};
    }

    bool UpdateBusy(std::string& error) {
        std::lock_guard<std::mutex> lock(update_mutex_);
        if (update_info_.status != UpdateStatus::downloading && update_info_.status != UpdateStatus::applying &&
            update_info_.status != UpdateStatus::ready_to_restart)
            return false;
        error = last_error_ = "Diamond Dogs Together is updating. Wait until it has restarted, then try again.";
        return true;
    }

    bool ReadyFor(std::string& error) {
        if (phase_ == Phase::ready) return true;
        if (stage_ != Stage::none) error = "A game is already running. Close it first.";
        else if (phase_ == Phase::needs_setup || phase_ == Phase::setting_up) error = "Finish the setup first.";
        else if (phase_ == Phase::not_in_game_folder) error = "Start Diamond Dogs Together from your game folder first.";
        else error = last_error_.empty() ? std::string("Co-op is not ready in this folder.") : last_error_;
        return false;
    }

    bool ToolsAllowed(const char* what, std::string& error) {
        if (stage_ != Stage::none) {
            error = std::string("Close the game first, then ") + what + ".";
            return false;
        }
        if (phase_ != Phase::ready && phase_ != Phase::error) {
            error = "Set up co-op first. There is no co-op save yet.";
            return false;
        }
        if (AnyGameRunningIn(*env_, paths_.game)) {
            error = std::string("MGSV is running from this folder. Quit it first, then ") + what + ".";
            return false;
        }
        if (PlaySessionLive(*env_, paths_)) {
            error = "Co-op is still on for this game folder (another window or its helper). Quit MGSV first.";
            return false;
        }
        if (!IsDir(JoinPath2(paths_.coop_saves, L"287700\\local"))) {
            error = "Set up co-op first. There is no co-op save yet.";
            return false;
        }
        return true;
    }

    void PersistCharacter() {
        const std::string name = CharacterName(character_);
        settings_.character = name;
        if (phase_ == Phase::not_in_game_folder || phase_ == Phase::setting_up || !IsDir(paths_.mod)) return;
        std::string error;
        SaveSettings(paths_, settings_, error);
        json::Value state;
        if (ReadInstallState(paths_, state) && state.Str("character") != name) {
            state["character"] = name;
            WriteAtomic(paths_.friend_install, json::Dump(state) + "\n", true, error);
        }
    }

    bool RuntimeArchiveOk() {
        std::string error;
        return RuntimeArchiveValid(paths_, &settings_, error);
    }

    void Recheck() {
        last_error_.clear();
        discovered_.clear();
        paths_ = GetInstancePaths(game_dir_);
        if (!IsFile(paths_.exe)) {
            phase_ = Phase::not_in_game_folder;
            const std::vector<std::wstring> found = FindMgsvGameFolders(*env_);
            if (!found.empty()) discovered_ = Utf8(found.front());
            return;
        }
        // Character and HUD: the settings, else the earlier install's record.
        json::Value state;
        const bool have_state = ReadInstallState(paths_, state);
        Character c = Character::dd_male;
        if (CharacterFromName(settings_.character, c) || (have_state && CharacterFromName(JoinerCharacter(state.Str("character")), c)))
            character_ = c;
        hud_ = settings_.hud.empty() ? std::string("compact") : settings_.hud;
        std::string problem = GamePathProblem(paths_.game);
        if (!problem.empty()) return Fail(problem);
        if (IsDir(paths_.mod) && HasReparsePoint(paths_.mod, paths_.game)) return Fail("The mgs-coop folder is a link; that is not supported.");
        json::Value session;
        const bool active = ReadPlaySession(paths_, session) && SessionIsActive(session);
        const bool game = AnyGameRunningIn(*env_, paths_.game);
        if (active) {
            if (game || HandoffPending(*env_, session)) {
                Attach(session, game);
                return;
            }
            if (PlaySessionLive(*env_, paths_))
                return Fail("Co-op is still switching off in the background (or another Diamond Dogs Together window is open for this "
                            "folder). Press Check again in a moment.");
            const CleanupResult r = CleanupSession(*env_, paths_, false);
            if (r.ran) Log("leftover=cleaned; removed=" + std::to_string(r.removed.size()));
        } else if (game) {
            return Fail("MGSV is running from this folder. Quit it normally, then press Check again.");
        }
        RemoveStaleHandoff(*env_, paths_);
        const PackageInfo package = ReadPackage(paths_);
        problem = GameFileProblem(paths_, settings_, package);
        if (!problem.empty()) return Fail(problem);
        if (have_state && InstallIsCurrent(state) && CoopCopySeeded(paths_.coop_saves) && RuntimeArchiveOk()) {
            phase_ = Phase::ready;
            return;
        }
        phase_ = Phase::needs_setup;
    }

    void Fail(const std::string& sentence) {
        phase_ = Phase::error;
        last_error_ = sentence;
    }

    bool Preflight(std::string& error) {
        auto refuse = [&](const std::string& sentence) {
            error = last_error_ = sentence;
            return false;
        };
        if (AnyGameRunningIn(*env_, paths_.game)) return refuse("MGSV is already running from this folder. Quit it normally, then try again.");
        if (PlaySessionLive(*env_, paths_))
            return refuse("Co-op is still on for this folder (another Diamond Dogs Together window or its helper). Close it or quit MGSV first.");
        if (IsDir(paths_.mod)) {
            const CleanupResult r = CleanupSession(*env_, paths_, false);
            if (r.ran) Log("leftover=cleaned; removed=" + std::to_string(r.removed.size()));
            RemoveStaleHandoff(*env_, paths_);
        }
        if (HasReparsePoint(paths_.mod, paths_.game)) return refuse("The mgs-coop folder is a link; that is not supported.");
        std::string problem = GamePathProblem(paths_.game);
        if (!problem.empty()) return refuse(problem);
        const PackageInfo package = ReadPackage(paths_);
        problem = GameFileProblem(paths_, settings_, package);
        if (!problem.empty()) {
            phase_ = Phase::error;
            return refuse(problem);
        }
        json::Value state;
        if (!ReadInstallState(paths_, state) || !InstallIsCurrent(state) || !CoopCopySeeded(paths_.coop_saves) || !RuntimeArchiveOk()) {
            phase_ = Phase::needs_setup;
            return refuse("The co-op setup is incomplete (the co-op save copy or the runtime file is missing). Run the setup again.");
        }
        for (const std::pair<const std::wstring*, const char*> f : {std::make_pair(&paths_.lab_ini, "lab.ini"), std::make_pair(&paths_.coop_ini, "coop.ini")}) {
            if (!PathExists(*f.first)) continue;
            std::string text;
            ReadTextShared(*f.first, text, 1u << 20);
            const std::string what = IsGeneratedIniText(text) ? "a co-op file that was changed afterwards"
                                                              : "a file Diamond Dogs Together did not write (for example from the single-PC lab)";
            return refuse("mgs-coop\\" + std::string(f.second) + " exists and is " + what +
                          ". Diamond Dogs Together does not overwrite it: move it out of the mgs-coop folder, then try again.");
        }
        for (const std::pair<const std::wstring*, const char*> f :
             {std::make_pair(&paths_.marker, "script-observe.enabled"), std::make_pair(&paths_.link_secret, "link-secret.txt")}) {
            if (PathExists(*f.first))
                return refuse("mgs-coop\\" + std::string(f.second) +
                              " exists but was not written by this session. Move it out of the mgs-coop folder, then try again.");
        }
        return true;
    }

    bool StartSession(bool host, const std::string& secret, std::uint64_t peer, bool wait, std::uint64_t self, const std::string& clip,
                      std::string& error) {
        ActivationRequest req;
        req.lab.role = host ? "host" : "guest";
        req.lab.save_dir = Utf8(paths_.coop_saves);
        req.lab.steam_self = Decimal(self);
        if (!host) {
            req.lab.steam_wait = wait;
            if (!wait) req.lab.steam_peer = Decimal(peer);
        }
        req.lab.save_handoff = "scene";
        req.lab.world = 3;
        req.lab.partner = "native";
        req.secret = wait ? std::string() : secret;
        req.character = host ? "snake" : CharacterName(character_);
        req.fallback = "dd_male";
        req.hud = hud_;
        req.clip_sha256 = clip;
        req.launcher_note = "Diamond Dogs Together " + CoreBuildId();
        std::string id, activation_error;
        auto verify = [&](std::string& e) {
            const PackageInfo p = ReadPackage(paths_);
            if (!p.ok) {
                e = p.problem;
                return false;
            }
            if (!LoaderIsOurs(paths_, p)) {
                e = "dinput8.dll changed while co-op was being switched on.";
                return false;
            }
            std::string dat_error;
            if (!RuntimeArchiveValid(paths_, nullptr, dat_error)) {
                e = "The co-op runtime file changed; run the setup again.";
                return false;
            }
            return true;
        };
        if (!ActivateSession(*env_, paths_, req, verify, id, activation_error)) {
            CleanupSession(*env_, paths_, true);
            error = last_error_ = "Co-op could not be switched on: " + activation_error + " Nothing was left switched on.";
            Log("activate=failed");
            return false;
        }
        session_id_ = id;
        wait_invite_ = !host && wait;
        notice_.clear();
        if (no_launch_) {
            // --write-session (developer): the files are in place exactly as for a
            // launch; the caller starts the game and runs --end-session afterwards.
            Log("play=written; role=" + req.lab.role + "; wait=" + (wait_invite_ ? "1" : "0") + "; launch=0");
            last_error_.clear();
            return true;
        }
        if (!StartWatchdogFor(*env_, paths_, session_id_))
            notice_ = "The background helper could not start: keep Diamond Dogs Together open until you quit MGSV (it switches co-op off then).";
        for (int i = 0; i < 3; ++i) offsets_[i] = static_cast<std::int64_t>(FileSizeOrZero(JoinPath2(paths_.mod, Wide(kLogNames[i]))));
        const std::vector<std::wstring> folders = FindMgsvGameFolders(*env_);
        bool ours = folders.empty();
        for (const std::wstring& f : folders)
            if (IEquals(f, paths_.game)) ours = true;
        if (!ours) {
            const std::string warning = "Steam's MGSV folder seems to be " + Utf8(folders.front()) +
                                        ", not this folder. Steam starts the game from its own folder, where co-op is not switched on.";
            notice_ = notice_.empty() ? warning : notice_ + " " + warning;
            Log("warning=steam_folder_differs");
        }
        Log("play=launch; role=" + req.lab.role + "; wait=" + (wait_invite_ ? "1" : "0") + "; steam=" + GetSteamAppRunning(*env_).text);
        if (!env_->OpenUrl(Wide(kSteamUrl))) {
            CleanupSession(*env_, paths_, true);
            ResetSession();
            error = last_error_ = "Steam could not be asked to start MGSV. Start Steam, then try again. Co-op was switched off again.";
            return false;
        }
        stage_ = Stage::launching;
        phase_ = Phase::launching;
        run_ = 0;
        last_error_.clear();
        next_poll_ = 0;
        UpdateLink(env_->NowMs());
        return true;
    }

    void ResetSession() {
        stage_ = Stage::none;
        session_id_.clear();
        std::fill(code_.begin(), code_.end(), '\0');
        code_.clear();
        notice_.clear();
        hint_.clear();
        game_pid_ = 0;
        game_start_ = 0;
        run_ = 0;
        status_ = json::Value();
        evidence_ = Evidence{};
        link_ = LinkView{};
        last_status_key_.clear();
        std::lock_guard<std::mutex> lock(shared_->m);
        shared_->applied = false;
        shared_->after_finished = false;
    }

    void Attach(const json::Value& session, bool game_running) {
        ResetSession();
        role_ = session.Str("role") == "host" ? Role::host : Role::join;
        session_id_ = session.Str("session_id");
        const json::Value* lab = session.Find("lab");
        wait_invite_ = lab && lab->Bool("steam_wait");
        if (role_ == Role::host && lab) {
            std::string secret_text;
            if (ReadTextShared(paths_.link_secret, secret_text, 64) && secret_text.size() >= 20) {
                std::uint64_t id = 0;
                for (char ch : lab->Str("steam_self"))
                    if (ch >= '0' && ch <= '9') id = id * 10 + static_cast<std::uint64_t>(ch - '0');
                code_ = FormatJoinCode(secret_text.substr(0, 20), id);
            }
            std::fill(secret_text.begin(), secret_text.end(), '\0');
        }
        const std::uint32_t me = env_->CurrentPid();
        const std::int64_t my_start = env_->CurrentStartMs();
        UpdatePlaySession(*env_, paths_, session_id_, [&](json::Value& t) {
            t["launcher_pid"] = static_cast<std::int64_t>(me);
            t["launcher_start_utc"] = my_start ? FormatUtcRoundTrip(my_start) : std::string();
        });
        Log("window=attached; role=" + std::string(role_ == Role::host ? "host" : "guest"));
        if (game_running) {
            for (const ProcessRow& g : GameProcessesIn(*env_, paths_.game)) {
                if (g.unknown) continue;
                game_pid_ = g.pid;
                env_->ProcessStartMs(g.pid, game_start_);
                break;
            }
            evidence_done_ = true;   // its start was not watched from this window
            run_ = HandoffOf(session).Int("count", 0) >= 1 ? 1 : 0;
            stage_ = Stage::in_game;
            phase_ = Phase::in_game;
        } else {
            BeginRelaunchWait(ParseClaimed(session));
        }
        next_poll_ = 0;
        UpdateLink(env_->NowMs());
    }

    static std::int64_t ParseClaimed(const json::Value& session) {
        std::int64_t ms = 0;
        return ParseUtc(HandoffOf(session).Str("claimed_utc"), ms) ? ms : 0;
    }

    void BeginRelaunchWait(std::int64_t claimed_ms) {
        const std::int64_t now = env_->NowMs();
        run_ = 1;
        stage_ = Stage::relaunch_wait;
        phase_ = Phase::launching;
        wait_start_ = now;
        limit_ = now + 300000;
        if (claimed_ms && claimed_ms + 300000 < limit_) limit_ = claimed_ms + 300000;
        next_try_ = now + 20000;
        tries_ = 0;
        relaunch_ok_ = false;
        told_ = false;
        hint_.clear();
    }

    void OnGameSeen(std::uint32_t pid, std::int64_t now) {
        game_pid_ = pid;
        game_start_ = 0;
        env_->ProcessStartMs(pid, game_start_);
        bool relaunched = false;
        SetSessionGame(*env_, paths_, session_id_, pid, game_start_, &relaunched);
        Log("game=seen; run=" + std::to_string(run_) + "; game_pid=" + std::to_string(pid));
        for (auto& t : texts_) t.clear();
        evidence_ = Evidence{};
        evidence_done_ = false;
        evidence_deadline_ = now + 90000;
        next_evidence_ = now;
        status_ = json::Value();
        stage_ = Stage::in_game;
        phase_ = Phase::in_game;
    }

    void PollLaunching(std::int64_t now) {
        for (const ProcessRow& g : GameProcessesIn(*env_, paths_.game)) {
            if (g.unknown) continue;
            OnGameSeen(g.pid, now);
            return;
        }
        // Co-op switched off from elsewhere (the helper, another tool): back to ready.
        json::Value s;
        if (!ReadPlaySession(paths_, s) || !SessionIsActive(s) || s.Str("session_id") != session_id_) {
            Log("launch=session_gone");
            ResetSession();
            Recheck();
            if (phase_ == Phase::ready) last_error_ = "Co-op was switched off before MGSV started. Host or join again.";
        }
    }

    void PollInGame(std::int64_t now) {
        bool alive;
        if (game_pid_) {
            alive = ProcessAliveSince(*env_, game_pid_, game_start_);
        } else {
            alive = AnyGameRunningIn(*env_, paths_.game);
            for (const ProcessRow& g : GameProcessesIn(*env_, paths_.game))
                if (!g.unknown) {
                    game_pid_ = g.pid;
                    env_->ProcessStartMs(g.pid, game_start_);
                    break;
                }
        }
        if (!alive) {
            OnGameExit(now);
            return;
        }
        std::string text;
        ReadTextShared(paths_.link_status, text, 65536);
        status_ = ParseStatusText(text, game_pid_);
        if (!evidence_done_ && now >= next_evidence_) {
            for (int i = 0; i < 3; ++i) {
                std::string delta;
                ReadNewLines(JoinPath2(paths_.mod, Wide(kLogNames[i])), offsets_[i], delta);
                if (texts_[i].size() < (8u << 20)) texts_[i] += delta;
            }
            const bool final_check = now >= evidence_deadline_;
            evidence_ = GetLaunchEvidence(texts_[0], texts_[1], texts_[2], Utf8(paths_.coop_saves), final_check, true);
            if (evidence_.state != "pending") {
                evidence_done_ = true;
                Log("evidence=" + evidence_.state + "; missing=" + std::to_string(evidence_.missing.size()) + "; problems=" +
                    std::to_string(evidence_.problems.size()));
            }
            next_evidence_ = now + 1000;
        }
        const std::string key = status_.is_object() ? status_.Str("state") + "|" + status_.Str("reason") + "|" + status_.Str("handoff") + "|" +
                                                          status_.Str("partner") + "|" + status_.Str("left")
                                                    : std::string("none");
        if (key != last_status_key_) {
            last_status_key_ = key;
            Log("status; " + FormatStatusLine(status_, role_ == Role::host, now, CoreBuildId()));
        }
    }

    void OnGameExit(std::int64_t now) {
        for (int i = 0; i < 3; ++i) offsets_[i] = static_cast<std::int64_t>(FileSizeOrZero(JoinPath2(paths_.mod, Wide(kLogNames[i]))));
        Log("game=exited; run=" + std::to_string(run_) + "; game_pid=" + std::to_string(game_pid_) + "; steam=" + GetSteamAppRunning(*env_).text);
        stage_ = Stage::after_game;
        phase_ = Phase::after_game;
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            shared_->after_finished = false;
            shared_->applied = false;
        }
        auto shared = shared_;
        auto env = env_;
        const InstancePaths paths = paths_;
        const std::uint32_t pid = game_pid_;
        const std::int64_t start = game_start_;
        const bool report_applied = run_ == 0;
        if (after_thread_.joinable()) after_thread_.join();
        after_thread_ = std::thread([shared, env, paths, pid, start, now, report_applied]() {
            HandoffOutcome outcome;
            try {
                outcome = HandoffAfterExit(*env, paths, pid, start, false, false, now,
                    [&](const HandoffOutcome& applied) {
                        if (!report_applied) return;
                        std::lock_guard<std::mutex> lock(shared->m);
                        shared->applied = true;
                        shared->applied_info = applied;
                    },
                    &shared->cancel);
            } catch (...) {
                outcome.action = "failed";
                outcome.messages = {"Could not apply the host's save."};
            }
            std::lock_guard<std::mutex> lock(shared->m);
            shared->after = outcome;
            shared->after_finished = true;
        });
    }

    void PollAfterGame(std::int64_t now) {
        HandoffOutcome outcome;
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            if (!shared_->after_finished) return;
            outcome = shared_->after;
            shared_->after_finished = false;
        }
        if (after_thread_.joinable()) after_thread_.join();
        Log("handoff_after_exit; owner=launcher; action=" + outcome.action);
        if (run_ == 0 && (outcome.action == "relaunched" || outcome.action == "claimed_by_other")) {
            json::Value s;
            BeginRelaunchWait(ReadPlaySession(paths_, s) ? ParseClaimed(s) : now);
            if (outcome.action == "claimed_by_other") hint_ = Join(outcome.messages);
            return;
        }
        FinishSession(outcome.messages);
    }

    void PollRelaunch(std::int64_t now) {
        for (const ProcessRow& g : GameProcessesIn(*env_, paths_.game)) {
            if (g.unknown) continue;
            OnGameSeen(g.pid, now);
            Log("relaunch=arrived");
            return;
        }
        const RelaunchWait pending = GetRelaunchWait(*env_, paths_, session_id_);
        if (pending.stop) {
            Log("relaunch_wait=stopped");
            FinishSession(pending.lines);
            return;
        }
        if (pending.claimed_ms && pending.claimed_ms + 300000 < limit_) limit_ = pending.claimed_ms + 300000;
        const std::int64_t elapsed = now - wait_start_;
        const std::int64_t left = limit_ - now;
        if (now >= next_try_ && left >= 30000) {
            next_try_ = now + 20000;
            const SteamRunning steam = GetSteamAppRunning(*env_);
            if (steam.known && steam.running) {
                Log("relaunch_retry=skipped; reason=steam_running; steam=" + steam.text);
            } else {
                ++tries_;
                Log("relaunch_retry; n=" + std::to_string(tries_) + "; elapsed_s=" + std::to_string(elapsed / 1000));
                const RelaunchResult r = StartRelaunch(*env_, paths_, false);
                if (!r.ok) {
                    FinishSession({"MGSV could not be started again: " + r.problem +
                                   ". Do not start MGSV from Steam now; host or join again in Diamond Dogs Together."});
                    return;
                }
                relaunch_ok_ = true;
            }
        }
        if (elapsed >= 45000 && !told_ && left >= 60000) {
            told_ = true;
            if (!relaunch_ok_) relaunch_ok_ = StartRelaunch(*env_, paths_, true).ok;
            const SteamRunning steam = GetSteamAppRunning(*env_);
            const bool busy = steam.known && steam.running;
            Log("relaunch_hint; shown=" + std::string(relaunch_ok_ ? "1" : "0") + "; left_s=" + std::to_string(left / 1000) + "; steam=" + steam.text);
            if (relaunch_ok_ && busy)
                hint_ = "MGSV is not back yet, and Steam still shows MGSV as running: once Steam shows Play again, press it; co-op stays on for " +
                        std::to_string(left / 1000) + " more seconds.";
            else if (relaunch_ok_)
                hint_ = "MGSV is not back yet: start MGSV from Steam yourself (press Play); co-op stays on for " + std::to_string(left / 1000) +
                        " more seconds.";
        }
        if (now >= limit_) {
            Log("relaunch_wait=limit");
            CleanupSession(*env_, paths_, true);
            FinishSession({"MGSV did not start again in time; co-op is switched off."});
        }
    }

    void FinishSession(const std::vector<std::string>& messages) {
        std::vector<std::string> lines = messages;
        if (AnyGameRunningIn(*env_, paths_.game)) {
            // The game runs again (started by hand?): the helper takes over, the window shows it.
            if (!PlaySessionLive(*env_, paths_)) StartWatchdogFor(*env_, paths_, session_id_);
        } else {
            const CleanupResult r = CleanupSession(*env_, paths_, false);
            for (const std::string& m : r.messages)
                if (m.find("was changed after") != std::string::npos) lines.push_back(m);
        }
        Log("session=finished");
        ResetSession();
        Recheck();
        if (phase_ == Phase::ready) last_error_ = Join(lines);
    }

    void UpdateLink(std::int64_t now) {
        LinkView v;
        switch (stage_) {
        case Stage::none: break;
        case Stage::launching:
            v.headline = "Starting MGSV...";
            v.detail = "Steam is starting the game. Answer any Steam dialog; this can take a minute.";
            break;
        case Stage::relaunch_wait:
            v.headline = "MGSV is restarting with the host's save";
            v.detail = "Steam is starting MGSV again, now on the host's save. Your previous co-op save is kept (Tools > Share save > Restore "
                       "previous brings it back).";
            if (!hint_.empty()) v.detail += " " + hint_;
            v.handoff_active = true;
            v.handoff_progress = 1.0f;
            break;
        case Stage::in_game:
            v = LinkViewFromStatus(status_, role_ == Role::host, now, CoreBuildId());
            if (evidence_.state == "unsafe") {
                v.headline = "Co-op did NOT start: this game uses your REAL saves";
                v.detail = "Save protection is NOT active: this game is normal offline play on your real saves. Quit MGSV now if you do "
                           "not want that.";
                v.connected = false;
            } else if (evidence_.state == "old_loader") {
                v.detail += (v.detail.empty() ? "" : " ") + std::string("The save copy is active, but the co-op loader is too old for "
                                                                         "playing together. Use the same Diamond Dogs Together as your partner.");
            } else if (evidence_.state == "incomplete") {
                std::string why = Join(evidence_.problems);
                if (why.empty() && !evidence_.missing.empty()) why = "Some start-up checks are missing; use Tools > Collect logs if co-op does not work.";
                v.detail += (v.detail.empty() ? "" : " ") + std::string("Co-op did not confirm its start. ") + why;
            }
            break;
        case Stage::after_game: {
            v.headline = "The game has closed";
            bool applied = false;
            HandoffOutcome info;
            {
                std::lock_guard<std::mutex> lock(shared_->m);
                applied = shared_->applied;
                info = shared_->applied_info;
            }
            if (applied) {
                v.handoff_active = true;
                v.handoff_progress = 1.0f;
                v.detail = "Applying the host's save (" + std::to_string(info.files) + " files, " +
                           std::to_string((info.bytes + 1023) / 1024) + " KB). Your previous co-op save is kept (Tools > Share save > "
                           "Restore previous brings it back). Waiting for Steam to let MGSV start again (up to 30 s)...";
            } else {
                v.detail = "Tidying up the co-op files...";
            }
            break;
        }
        }
        if (stage_ != Stage::none && !notice_.empty()) v.detail += (v.detail.empty() ? "" : " ") + notice_;
        link_ = v;
    }

    std::wstring game_dir_;
    std::shared_ptr<Env> env_;
    bool exit_if_busy_ = false;
    bool no_launch_ = false;
    InstancePaths paths_;
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::thread setup_thread_;
    std::thread after_thread_;
    Phase phase_ = Phase::needs_setup;
    std::string last_error_, discovered_;
    Character character_ = Character::dd_male;
    std::string hud_ = "compact";
    Settings settings_;

    Stage stage_ = Stage::none;
    Role role_ = Role::host;
    bool wait_invite_ = false;
    std::string session_id_, code_, notice_, hint_;
    int run_ = 0;
    std::uint32_t game_pid_ = 0;
    std::int64_t game_start_ = 0;
    std::int64_t offsets_[3] = {0, 0, 0};
    std::string texts_[3];
    std::int64_t evidence_deadline_ = 0, next_evidence_ = 0, next_poll_ = 0;
    Evidence evidence_;
    bool evidence_done_ = true;
    json::Value status_;
    std::string last_status_key_;
    LinkView link_;
    std::int64_t wait_start_ = 0, limit_ = 0, next_try_ = 0;
    int tries_ = 0;

    ModelStart start_;
    std::string updated_to_;           // after --after-update: this version
    bool update_now_started_ = false;
    mutable std::mutex update_mutex_;
    UpdateInfo update_info_;
    std::thread update_thread_;
    std::atomic<bool> update_cancel_{false};
    bool relaunch_ok_ = false, told_ = false;
};

}  // namespace

std::unique_ptr<LauncherModel> MakeRealModelWithEnv(const std::wstring& exe_dir, std::shared_ptr<Env> env, const ModelStart& start) {
    return std::make_unique<RealModel>(exe_dir, std::move(env), false, false, start);
}

int RunWatch(const std::wstring& exe_dir, const std::string& session_id) {
    if (!IsHexText(session_id, 32)) return 2;
    auto env = MakeWin32Env();
    return RunWatchdog(*env, exe_dir, LowerAscii(session_id));
}

// ---- Developer flags: --write-session / --end-session ---------------------------------
namespace {
std::string OneLine(std::string text) {
    for (char& c : text)
        if (c == '\r' || c == '\n') c = ' ';
    return text;
}
}  // namespace

int RunWriteSessionWithEnv(const std::wstring& game_dir, std::shared_ptr<Env> env, const WriteSessionRequest& request, std::string& out) {
    if (request.host && (!request.code.empty() || !request.character.empty())) {
        out += "error=the host plays Snake and makes its own code: --code and --character are for join\n";
        return 2;
    }
    Character chosen = Character::dd_male;
    if (!request.character.empty() && !CharacterFromName(request.character, chosen)) {
        out += "error=unknown character (story, ocelot, dd_male, dd_female or avatar)\n";
        return 2;
    }
    RealModel model(game_dir, std::move(env), false, true);
    if (!request.character.empty()) model.set_character(chosen);
    if (model.phase() == Phase::needs_setup) {
        out += "setup=start\n";
        model.start_setup();
        while (model.phase() == Phase::setting_up) {
            Sleep(50);
            model.tick();
        }
        out += model.phase() == Phase::ready ? "setup=done\n" : "setup=failed\n";
    }
    if (model.phase() != Phase::ready) {
        const std::string why = model.last_error().empty() ? std::string("co-op is not ready in this folder") : model.last_error();
        out += "error=" + OneLine(why) + "\n";
        return 1;
    }
    std::string code, error;
    const bool ok = request.host ? model.host(code, error) : model.join(request.code, error);
    if (!ok) {
        out += "error=" + OneLine(error.empty() ? model.last_error() : error) + "\n";
        return 1;
    }
    out += std::string("role=") + (request.host ? "host" : "guest") + "\n";
    out += "session=" + model.session_id() + "\n";
    if (request.host) out += "code=" + code + "\n";
    else out += std::string("wait_invite=") + (Trim(request.code).empty() ? "1" : "0") + "\n";
    out += "launch=0\n";
    std::fill(code.begin(), code.end(), '\0');
    return 0;
}

int RunWriteSession(const std::wstring& game_dir, const WriteSessionRequest& request, std::string& out) {
    return RunWriteSessionWithEnv(game_dir, MakeWin32Env(), request, out);
}

int RunEndSessionWithEnv(const std::wstring& game_dir, std::shared_ptr<Env> env, std::string& out) {
    const InstancePaths paths = GetInstancePaths(game_dir);
    if (!IsFile(paths.exe)) {
        out += "error=no mgsvtpp.exe in this folder\n";
        return 1;
    }
    if (AnyGameRunningIn(*env, paths.game)) {
        out += "error=MGSV is running from this folder; co-op is switched off only after it exits\n";
        return 1;
    }
    if (!IsDir(paths.mod)) {
        out += "session=none\n";
        return 0;
    }
    if (PlaySessionLive(*env, paths)) {
        out += "error=a Diamond Dogs Together window or its helper still owns this session; close it or quit MGSV\n";
        return 1;
    }
    const CleanupResult r = CleanupSession(*env, paths, false);
    const StaleResult stale = RemoveStaleHandoff(*env, paths);
    LauncherLog(*env, paths, "play=ended; removed=" + std::to_string(r.removed.size()) + "; kept=" + std::to_string(r.kept.size()));
    out += std::string("cleanup=") + (r.ran ? "ran" : "skipped") + "\n";
    for (const std::string& name : r.removed) out += "removed=" + name + "\n";
    for (const std::string& name : r.kept) out += "kept=" + name + "\n";
    for (const std::string& name : stale.removed) out += "removed=" + name + "\n";
    for (const std::string& m : r.messages) out += "note=" + OneLine(m) + "\n";
    return r.kept.empty() ? 0 : 1;
}

int RunEndSession(const std::wstring& game_dir, std::string& out) { return RunEndSessionWithEnv(game_dir, MakeWin32Env(), out); }

}  // namespace ddt::core

namespace ddt {
std::unique_ptr<LauncherModel> MakeRealModel(const std::wstring& exe_dir) {
    return std::make_unique<core::RealModel>(exe_dir, core::MakeWin32Env(), true);
}
std::unique_ptr<LauncherModel> MakeRealModel(const std::wstring& exe_dir, const ModelStart& start) {
    return std::make_unique<core::RealModel>(exe_dir, core::MakeWin32Env(), true, false, start);
}
}  // namespace ddt
