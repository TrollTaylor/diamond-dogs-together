// Diamond Dogs Together launcher (C0): headless tests of the pure GUI logic
// (navigation, character picker, HUD, Join field, code display, setup summary,
// redraw pacing, command line), the join-code check and the mock model.
#include <cstdio>
#include <string>
#include <vector>

#include "launcher_join_code.hpp"
#include "launcher_model_mock.hpp"
#include "launcher_ui_logic.hpp"

namespace {
unsigned checks{}, failures{};
void Check(bool value, const char* label) {
    ++checks;
    if (!value) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}

using namespace ddt;
using ui::Page;

void TestNavigation() {
    // There is no Setup page any more (2026-10-29): the setup runs by itself and
    // Play shows its progress. The pages are Play, In game, Tools and Settings.
    Check(ui::kPageCount == 4, "four pages");
    const Page all[] = {Page::play, Page::in_game, Page::tools, Page::settings};
    for (Page p : all)
        Check(std::string(ui::PageLabel(p)) != "" && std::string(ui::PageLabel(p)) != "Setup", "no Setup page in the navigation");

    Check(ui::DefaultPage(Phase::not_in_game_folder) == Page::play, "wrong folder -> play (the wrong-folder screen covers it)");
    Check(ui::DefaultPage(Phase::needs_setup) == Page::play, "needs setup -> play");
    Check(ui::DefaultPage(Phase::setting_up) == Page::play, "setting up -> play");
    Check(ui::DefaultPage(Phase::ready) == Page::play, "ready -> play");
    Check(ui::DefaultPage(Phase::error) == Page::play, "error -> play");
    Check(ui::DefaultPage(Phase::in_game) == Page::in_game, "in game -> in game");

    Check(ui::PageEnabled(Phase::needs_setup, Page::play) && ui::PageEnabled(Phase::setting_up, Page::play),
          "Play shows the automatic setup");
    Check(!ui::PageEnabled(Phase::ready, Page::in_game), "In game only during a session");
    Check(ui::PageEnabled(Phase::launching, Page::in_game), "In game while launching");
    for (Page p : all) Check(!ui::PageEnabled(Phase::not_in_game_folder, p), "nothing to open in the wrong folder");
    Check(ui::PageEnabled(Phase::setting_up, Page::tools) && ui::PageEnabled(Phase::setting_up, Page::settings),
          "Tools and Settings stay open while co-op gets ready");
    Check(ui::PageEnabled(Phase::in_game, Page::tools), "Tools page visible in game (actions disabled)");

    for (Page p : all) {
        Check(!ui::PageVisible(Phase::not_in_game_folder, p), "the wrong-folder screen has no navigation");
        Check(ui::PageVisible(Phase::ready, p) && ui::PageVisible(Phase::setting_up, p), "every page is listed otherwise");
    }

    Check(ui::ToolsAllowed(Phase::ready) && ui::ToolsAllowed(Phase::error) && ui::ToolsAllowed(Phase::needs_setup),
          "tools allowed outside a session");
    Check(!ui::ToolsAllowed(Phase::in_game) && !ui::ToolsAllowed(Phase::launching) &&
              !ui::ToolsAllowed(Phase::after_game) && !ui::ToolsAllowed(Phase::setting_up),
          "tools refused during a session or setup");
    Check(ui::CanStartSession(Phase::ready), "Host and Join once ready");
    Check(!ui::CanStartSession(Phase::needs_setup) && !ui::CanStartSession(Phase::setting_up) &&
              !ui::CanStartSession(Phase::error) && !ui::CanStartSession(Phase::in_game),
          "Host and Join wait until co-op is ready");

    // Setup flow: it all happens on Play.
    Check(ui::PageAfterPhaseChange(Phase::needs_setup, Phase::setting_up, Page::play, Role::host) == Page::play,
          "setting up stays on Play");
    Check(ui::PageAfterPhaseChange(Phase::setting_up, Phase::ready, Page::play, Role::host) == Page::play,
          "ready stays on Play");
    Check(ui::PageAfterPhaseChange(Phase::setting_up, Phase::needs_setup, Page::play, Role::host) == Page::play,
          "a failed setup stays on Play");
    Check(ui::PageAfterPhaseChange(Phase::setting_up, Phase::ready, Page::settings, Role::host) == Page::settings,
          "the setup finishing does not pull the player off Settings");
    Check(ui::PageAfterPhaseChange(Phase::not_in_game_folder, Phase::needs_setup, Page::play, Role::host) == Page::play,
          "Check again from the wrong folder -> Play");
    // Host flow.
    Check(ui::PageAfterPhaseChange(Phase::ready, Phase::launching, Page::play, Role::host) == Page::play,
          "host keeps the code on Play while launching");
    Check(ui::PageAfterPhaseChange(Phase::launching, Phase::in_game, Page::play, Role::host) == Page::in_game,
          "the game started -> In game");
    // Join flow.
    Check(ui::PageAfterPhaseChange(Phase::ready, Phase::launching, Page::play, Role::join) == Page::in_game,
          "join goes to In game while launching");
    Check(ui::PageAfterPhaseChange(Phase::in_game, Phase::after_game, Page::in_game, Role::join) == Page::in_game,
          "after the game stays on In game");
    Check(ui::PageAfterPhaseChange(Phase::after_game, Phase::in_game, Page::in_game, Role::join) == Page::in_game,
          "a handoff relaunch stays on In game");
    Check(ui::PageAfterPhaseChange(Phase::after_game, Phase::ready, Page::in_game, Role::join) == Page::play,
          "session over -> Play");
    Check(ui::PageAfterPhaseChange(Phase::launching, Phase::ready, Page::play, Role::host) == Page::play,
          "cancelled launch stays on Play");
    // Side pages are not yanked.
    Check(ui::PageAfterPhaseChange(Phase::launching, Phase::in_game, Page::tools, Role::host) == Page::tools,
          "Tools stays when the game starts");
    Check(ui::PageAfterPhaseChange(Phase::after_game, Phase::ready, Page::settings, Role::host) == Page::settings,
          "Settings stays when the session ends");
    Check(ui::PageAfterPhaseChange(Phase::ready, Phase::error, Page::tools, Role::host) == Page::tools,
          "uninstall report stays on Tools");
    Check(ui::PageAfterPhaseChange(Phase::ready, Phase::needs_setup, Page::tools, Role::host) == Page::tools,
          "a new setup does not pull the player off Tools");
    Check(ui::PageAfterPhaseChange(Phase::ready, Phase::needs_setup, Page::play, Role::host) == Page::play,
          "needs setup from Play stays on Play");
    Check(ui::PageAfterPhaseChange(Phase::in_game, Phase::error, Page::in_game, Role::host) == Page::play,
          "error -> Play");
    Check(ui::PageAfterPhaseChange(Phase::ready, Phase::not_in_game_folder, Page::tools, Role::host) == Page::play,
          "wrong folder leaves the side pages");
    Check(ui::PageAfterPhaseChange(Phase::in_game, Phase::ready, Page::in_game, Role::host) == Page::play,
          "never left on a disabled page");
    Check(ui::PageAfterPhaseChange(Phase::ready, Phase::ready, Page::in_game, Role::host) == Page::play,
          "a disabled page falls back to the default");
}

void TestSetupView() {
    // What Play shows about the automatic setup.
    std::vector<SetupStep> steps(4);
    ui::SetupSummary running_sum;
    running_sum.running = true;
    Check(ui::GetSetupView(Phase::setting_up, running_sum) == ui::SetupView::preparing, "setting up -> Getting co-op ready");
    Check(ui::GetSetupView(Phase::needs_setup, ui::SummarizeSetup(steps)) == ui::SetupView::preparing,
          "needed, about to start by itself -> Getting co-op ready");
    steps[3].state = SetupStep::State::failed;
    Check(ui::GetSetupView(Phase::needs_setup, ui::SummarizeSetup(steps)) == ui::SetupView::failed,
          "a failed attempt -> the Try again banner");
    Check(ui::GetSetupView(Phase::ready, ui::SummarizeSetup(steps)) == ui::SetupView::none, "ready -> nothing");
    Check(ui::GetSetupView(Phase::error, ui::SummarizeSetup(steps)) == ui::SetupView::none, "error has its own card");
    Check(ui::GetSetupView(Phase::in_game, ui::SummarizeSetup({})) == ui::SetupView::none, "in game -> nothing");
    Check(ui::GetSetupView(Phase::not_in_game_folder, ui::SummarizeSetup({})) == ui::SetupView::none, "wrong folder -> nothing");
}

void TestDebugGating() {
    // Settings > Debug mode adds "Local test (two windows)" to Play; nothing of it shows otherwise.
    Check(ui::PlayTabCount(false) == 2 && ui::PlayTabCount(true) == 3, "the Local test tab only in Debug mode");
    Check(std::string(ui::PlayTabLabel(ui::PlayTab::host)) == "Host" && std::string(ui::PlayTabLabel(ui::PlayTab::join)) == "Join" &&
              std::string(ui::PlayTabLabel(ui::PlayTab::local_test)) == "Local test",
          "tab labels");
    Check(ui::ShownPlayTab(ui::PlayTab::local_test, false, false) == ui::PlayTab::host, "Debug off: never the Local test tab");
    Check(ui::ShownPlayTab(ui::PlayTab::join, true, false) == ui::PlayTab::join, "Debug on: the chosen tab");
    Check(ui::ShownPlayTab(ui::PlayTab::host, true, true) == ui::PlayTab::local_test &&
              ui::ShownPlayTab(ui::PlayTab::join, false, true) == ui::PlayTab::local_test,
          "a running local test is always shown");
    const LocalTestCheck ok{true, "Ready."}, missing{false, "Choose the second game folder in Settings > Debug."};
    Check(ui::CanStartLocalTest(Phase::ready, ok), "start when ready and the folder is fine");
    Check(!ui::CanStartLocalTest(Phase::ready, missing) && !ui::CanStartLocalTest(Phase::setting_up, ok) &&
              !ui::CanStartLocalTest(Phase::in_game, ok),
          "no start without a folder, while getting ready or during a game");

    // The mock model: Debug mode off by default; the folder is checked; a run ends by itself.
    MockOptions o;
    o.scenario = MockScenario::ready;
    MockModel m(o);
    m.use_manual_clock(0.0);
    Check(!m.debug() && m.local_test_folder().empty() && !m.check_local_test().ok, "Debug mode is off by default");
    std::string error;
    Check(!m.start_local_test(error) && !error.empty(), "no local test with Debug mode off");
    m.set_debug(true);
    Check(m.debug() && m.check_local_test().message.find("Settings") != std::string::npos, "Debug on: choose the second folder");
    m.set_local_test_folder(m.game_dir());
    Check(!m.check_local_test().ok && m.check_local_test().message.find("separate") != std::string::npos, "not the same folder");
    m.set_local_test_folder(MockModel::kGuestDir);
    Check(m.check_local_test().ok, "a separate MGSV folder");
    Check(m.start_local_test(error) && m.phase() == Phase::in_game && m.local_test().active && m.session_role() == Role::host,
          "the local test runs");
    Check(!m.check_local_test().ok, "not twice");
    m.advance(5.0);
    m.tick();
    Check(m.local_test().active && !m.link().headline.empty(), "both games running");
    m.advance(200.0);
    m.tick();
    Check(m.phase() == Phase::ready && !m.local_test().active && m.local_test().detail.find("were put back") != std::string::npos &&
              m.local_test().detail.find("previous setup") == std::string::npos,
          "both games closed: the test settings are put back (the copied co-op files stay)");
    m.set_debug(false);
    Check(!m.check_local_test().ok, "Debug off again");

    // The second folder's check is refreshed when the Local test tab or Settings > Debug comes into view.
    Check(ui::LocalCheckVisible(Page::play, ui::PlayTab::local_test, true) && ui::LocalCheckVisible(Page::settings, ui::PlayTab::host, true),
          "the Local test tab and Settings with Debug on show the check");
    Check(!ui::LocalCheckVisible(Page::settings, ui::PlayTab::local_test, false) && !ui::LocalCheckVisible(Page::play, ui::PlayTab::host, true) &&
              !ui::LocalCheckVisible(Page::tools, ui::PlayTab::local_test, true) && !ui::LocalCheckVisible(Page::in_game, ui::PlayTab::local_test, true),
          "nowhere else");
}

void TestUpdateGate() {
    // Update now waits while co-op gets ready by itself (the restart would cut the setup off) and during a game.
    Check(ui::CanStartUpdate(Phase::ready, ui::SetupView::none) && ui::CanStartUpdate(Phase::error, ui::SetupView::none),
          "update when ready, or when co-op can't start");
    Check(ui::CanStartUpdate(Phase::needs_setup, ui::SetupView::failed), "update after a failed setup (it may fix it)");
    Check(!ui::CanStartUpdate(Phase::setting_up, ui::SetupView::preparing) && !ui::CanStartUpdate(Phase::needs_setup, ui::SetupView::preparing),
          "no update while co-op gets ready");
    Check(!ui::CanStartUpdate(Phase::launching, ui::SetupView::none) && !ui::CanStartUpdate(Phase::in_game, ui::SetupView::none) &&
              !ui::CanStartUpdate(Phase::after_game, ui::SetupView::none),
          "no update during a game or a local test");
    Check(!ui::CanStartUpdate(Phase::not_in_game_folder, ui::SetupView::none), "no update in the wrong folder");
    // The mock model refuses it the same way.
    MockOptions o;
    o.scenario = MockScenario::first_run;
    MockModel m(o);
    m.use_manual_clock(0.0);
    UpdateInfo available;
    available.status = UpdateStatus::available;
    available.latest_version = "99.0.0";
    m.set_mock_update_info(available);
    m.tick();
    Check(m.phase() == Phase::setting_up, "the mock's setup runs by itself");
    m.start_update();
    Check(m.update_info().status == UpdateStatus::available, "mock: no update while co-op gets ready");
}

void TestScenes() {
    // --screenshot-all renders every scene; the Setup page scenes are gone.
    Check(ui::SceneCount() >= 20, "scene list");
    bool preparing = false, failed = false, wrong = false;
    for (int i = 0; i < ui::SceneCount(); ++i) {
        const std::string name = ui::SceneName(static_cast<ui::Scene>(i));
        Check(name.size() > 3 && name.compare(3, 6, "setup-") != 0, "no Setup page scene");
        Check(name.size() > 3 && name[0] >= '0' && name[0] <= '9' && name[1] >= '0' && name[1] <= '9' && name[2] == '-' &&
                  std::stoi(name.substr(0, 2)) == i + 1,
              "scenes are numbered in order");
        if (name.find("play-preparing") != std::string::npos) preparing = true;
        if (name.find("play-setup-failed") != std::string::npos) failed = true;
        if (name.find("wrong-folder") != std::string::npos) wrong = true;
    }
    Check(preparing && failed && wrong, "Play while preparing, the failure banner and the wrong folder are rendered");
    int debug_scenes = 0;
    for (int i = 0; i < ui::SceneCount(); ++i) {
        const std::string name = ui::SceneName(static_cast<ui::Scene>(i));
        if (name.find("settings-debug") != std::string::npos || name.find("play-local") != std::string::npos) ++debug_scenes;
    }
    Check(debug_scenes >= 4, "Settings with Debug on and Play's Local test are rendered");
    Check(std::string(ui::SceneName(static_cast<ui::Scene>(ui::SceneCount()))).empty(), "out of range scene");
}

void TestCharactersAndHud() {
    const Character all[] = {Character::story, Character::ocelot, Character::dd_male, Character::dd_female,
                             Character::avatar};
    for (Character c : all)
        Check(ui::CharacterOf(ui::ChoiceOf(c), ui::IsFemale(c)) == c, "character round trip");
    Check(ui::ChoiceOf(Character::dd_female) == ui::CharacterChoice::dd_soldier, "dd_female is the DD option");
    Check(ui::CharacterOf(ui::CharacterChoice::dd_soldier, false) == Character::dd_male, "DD male");
    Check(ui::CharacterOf(ui::CharacterChoice::dd_soldier, true) == Character::dd_female, "DD female");
    Check(ui::CharacterOf(ui::CharacterChoice::ocelot, true) == Character::ocelot, "the toggle only affects DD");

    Check(ui::HudIndex("compact") == 0 && ui::HudIndex("full") == 1 && ui::HudIndex("off") == 2, "hud index");
    Check(ui::HudIndex("") == 0 && ui::HudIndex("FULL") == 0, "unknown hud -> compact");
    for (int i = 0; i < ui::kHudCount; ++i) Check(ui::HudIndex(ui::HudValue(i)) == i, "hud round trip");
    Check(std::string(ui::HudValue(9)) == "compact", "out of range hud -> compact");
}

void TestJoinCode() {
    const std::string partner = MockModel::PartnerCode();
    Check(partner.size() == 49 && partner.rfind("MGS2-", 0) == 0, "partner code format");
    Check(CheckJoinCode(partner, 0).ok, "valid code");
    Check(CheckJoinCode(partner, MockModel::kOwnSteamId).ok, "valid code (someone else's)");
    Check(!CheckJoinCode(partner, MockModel::kPartnerSteamId).ok, "own code refused");
    Check(CheckJoinCode(partner, MockModel::kPartnerSteamId).message.find("your own") != std::string::npos,
          "own code sentence");

    std::string lower = partner;
    for (char& c : lower)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    Check(CheckJoinCode("  " + lower + "\r\n", 0).ok, "tolerant: case and blanks");
    Check(CheckJoinCode("+mgscoop_join " + partner, 0).ok, "a pasted invite string");

    const CodeCheck empty = CheckJoinCode("   ", 0);
    Check(!empty.ok && empty.message.empty(), "empty -> nothing to say");
    std::string typo = partner;
    const std::size_t at = typo.find('@');
    typo[at - 1] = typo[at - 1] == 'A' ? 'B' : 'A';
    Check(CheckJoinCode(typo, 0).message.find("typo") != std::string::npos, "check characters catch a typo");
    Check(CheckJoinCode(partner.substr(0, at), 0).message.find("after @") != std::string::npos, "missing @ part");
    Check(CheckJoinCode("MGS1-AAAAA-AAAAA-AAAAA-AAAAA-AA@192.168.1.2:47830", 0).message.find("LAN") !=
              std::string::npos,
          "old LAN code");
    Check(CheckJoinCode("hello@there", 0).message.find("MGS2-") != std::string::npos, "not a code");
    Check(CheckJoinCode("MGS2-AAAAA-AAAAA@76561197960273647", 0).message.find("missing") != std::string::npos,
          "short code");
    Check(CheckJoinCode(partner.substr(0, at) + "@12345", 0).message.find("Steam account") != std::string::npos,
          "bad Steam account");
    Check(CheckJoinCode(std::string(300, 'A'), 0).message.find("too long") != std::string::npos, "too long");
    Check(FormatJoinCode("short", MockModel::kOwnSteamId).empty(), "format refuses a bad secret");
    Check(FormatJoinCode("K7RD3WQ9MP2XVB6TZHN4", 12345).empty(), "format refuses a bad SteamID");
}

void TestJoinField() {
    const CodeCheck ok{true, "The code looks right."};
    const CodeCheck bad{false, "The code has a typo."};
    ui::JoinFieldView v = ui::EvaluateJoinField("", false, CodeCheck{}, false);
    Check(v.tone == ui::Tone::neutral && !v.can_join && v.field_enabled && !v.message.empty(), "empty field");
    v = ui::EvaluateJoinField("MGS2-x", false, bad, false);
    Check(v.tone == ui::Tone::bad && !v.can_join && v.message == bad.message, "bad code");
    v = ui::EvaluateJoinField("MGS2-ok", false, ok, false);
    Check(v.tone == ui::Tone::good && v.can_join, "good code");
    v = ui::EvaluateJoinField("MGS2-x", true, bad, false);
    Check(v.can_join && !v.field_enabled && v.message.find("invite") != std::string::npos, "wait for an invite");
    v = ui::EvaluateJoinField("MGS2-ok", false, ok, true);
    Check(!v.can_join && !v.field_enabled, "busy");
    v = ui::EvaluateJoinField("x", false, CodeCheck{false, ""}, false);
    Check(v.tone == ui::Tone::bad && !v.message.empty(), "a refusal always says something");

    const ui::CodeDisplay d = ui::SplitJoinCode("MGS2-AAAAA-BBBBB-CCCCC-DDDDD-EE@76561197960265729");
    Check(d.main == "MGS2-AAAAA-BBBBB-CCCCC-DDDDD-EE" && d.account == "@76561197960265729", "split code");
    const ui::CodeDisplay n = ui::SplitJoinCode("plain");
    Check(n.main == "plain" && n.account.empty(), "split without @");
}

void TestStatusHelpers() {
    Check(ui::PingTone(0) == ui::Tone::neutral && ui::PingTone(42) == ui::Tone::good &&
              ui::PingTone(120) == ui::Tone::warn && ui::PingTone(400) == ui::Tone::bad,
          "ping tones");
    Check(ui::FormatPing(42) == "42 ms" && ui::FormatPing(-1) == "-", "ping text");
    Check(ui::PercentOf(-1.0f) == 0 && ui::PercentOf(0.456f) == 45 && ui::PercentOf(2.0f) == 100, "percent");

    std::vector<SetupStep> steps(4);
    steps[0].state = SetupStep::State::done;
    steps[1].state = SetupStep::State::done;
    steps[2].state = SetupStep::State::running;
    steps[2].progress = 0.5f;
    ui::SetupSummary s = ui::SummarizeSetup(steps);
    Check(s.done == 2 && s.total == 4 && s.running && !s.failed && s.current == 2, "setup summary");
    Check(s.overall > 0.62f && s.overall < 0.63f, "setup overall");
    steps[2].state = SetupStep::State::failed;
    s = ui::SummarizeSetup(steps);
    Check(s.failed && !s.running && s.current == 2, "setup failure");
    Check(ui::SummarizeSetup({}).total == 0, "no steps");

    Check(ui::IdleWaitMs(Phase::ready, false) == 1000, "idle pacing");
    Check(ui::IdleWaitMs(Phase::in_game, false) == 250, "in-game pacing (4 Hz)");
    Check(ui::IdleWaitMs(Phase::setting_up, false) == 100, "setup pacing");
    Check(ui::IdleWaitMs(Phase::ready, true) == 16, "input pacing");
}

void TestCommandLine() {
    ui::Options o = ui::ParseCommandLine({});
    Check(o.mode == ui::Options::Mode::gui && !o.mock && o.error.empty(), "no arguments: the real model");
    o = ui::ParseCommandLine({L"--mock"});
    Check(o.mode == ui::Options::Mode::gui && o.mock, "--mock");
    o = ui::ParseCommandLine({L"--watch", L"0123456789abcdef0123456789ABCDEF"});
    Check(o.mode == ui::Options::Mode::watch && o.watch_session == L"0123456789abcdef0123456789ABCDEF", "--watch");
    Check(!ui::ParseCommandLine({L"--watch"}).error.empty(), "--watch needs a session");
    Check(!ui::ParseCommandLine({L"--watch", L"a b"}).error.empty(), "--watch refuses odd ids");
    Check(ui::ParseCommandLine({L"--selftest"}).mode == ui::Options::Mode::selftest, "--selftest");
    o = ui::ParseCommandLine({L"--screenshot-all", L"C:\\shots", L"--scale", L"1.5", L"--mock-scenario", L"ready"});
    Check(o.mode == ui::Options::Mode::screenshots && o.screenshot_dir == L"C:\\shots" && o.scale == 1.5f &&
              o.scenario == L"ready",
          "--screenshot-all with options");
    Check(!ui::ParseCommandLine({L"--scale", L"9"}).error.empty(), "--scale range");
    Check(!ui::ParseCommandLine({L"--mock-speed", L"x"}).error.empty(), "--mock-speed number");
    Check(ui::ParseCommandLine({L"--mock-speed", L"4"}).mock_speed == 4.0, "--mock-speed");
    Check(ui::ParseCommandLine({L"--mock-scenario", L"ready"}).mock, "--mock-scenario implies --mock");
    Check(!ui::ParseCommandLine({L"--selftest", L"--watch", L"abc"}).error.empty(), "one mode only");
    Check(!ui::ParseCommandLine({L"--frobnicate"}).error.empty(), "unknown option");
    Check(ui::ParseCommandLine({L"/?"}).mode == ui::Options::Mode::help, "help");
    // Developer flags for the release gate (no window, no Steam).
    o = ui::ParseCommandLine({L"--write-session", L"join", L"--code", L"MGS2-X", L"--character", L"story", L"--game-dir", L"D:\\G"});
    Check(o.error.empty() && o.mode == ui::Options::Mode::write_session && !o.write_host && o.write_code == L"MGS2-X" &&
              o.write_character == L"story" && o.game_dir == L"D:\\G",
          "--write-session join with a code, a character and a folder");
    o = ui::ParseCommandLine({L"--write-session", L"host"});
    Check(o.error.empty() && o.mode == ui::Options::Mode::write_session && o.write_host && o.game_dir.empty(), "--write-session host");
    Check(!ui::ParseCommandLine({L"--write-session"}).error.empty(), "--write-session needs host or join");
    Check(!ui::ParseCommandLine({L"--write-session", L"guest"}).error.empty(), "--write-session takes host or join only");
    Check(!ui::ParseCommandLine({L"--write-session", L"host", L"--code", L"MGS2-X"}).error.empty(), "the host makes its own code");
    Check(!ui::ParseCommandLine({L"--code", L"MGS2-X"}).error.empty(), "--code only with --write-session join");
    Check(!ui::ParseCommandLine({L"--game-dir", L"D:\\G"}).error.empty(), "--game-dir only with the session flags");
    o = ui::ParseCommandLine({L"--end-session", L"--game-dir", L"D:\\G"});
    Check(o.error.empty() && o.mode == ui::Options::Mode::end_session && o.game_dir == L"D:\\G", "--end-session");
    Check(!ui::ParseCommandLine({L"--end-session", L"--write-session", L"host"}).error.empty(), "one session flag at a time");
    o = ui::ParseCommandLine({L"--local-watch", L"0123456789abcdef0123456789abcdef"});
    Check(o.error.empty() && o.mode == ui::Options::Mode::local_watch && o.local_run == L"0123456789abcdef0123456789abcdef",
          "--local-watch (the local test's helper)");
    Check(!ui::ParseCommandLine({L"--local-watch", L"../x"}).error.empty() && !ui::ParseCommandLine({L"--local-watch"}).error.empty(),
          "--local-watch needs a run id");
    Check(!ui::ParseCommandLine({L"--write-session", L"host", L"--mock"}).error.empty(), "no simulated model for the session flags");
    Check(std::wstring(ui::UsageText()).find(L"Developer only") != std::wstring::npos &&
              std::wstring(ui::UsageText()).find(L"--end-session") != std::wstring::npos,
          "the usage text marks the session flags developer-only");
}

void TestMock() {
    MockScenario scenario{};
    Check(ParseMockScenario("partner-drops", scenario) && scenario == MockScenario::partner_drops, "scenario name");
    Check(!ParseMockScenario("nope", scenario), "unknown scenario");

    {   // First run, setup, host session with a handoff.
        MockModel m(MockOptions{});
        m.use_manual_clock(0.0);
        Check(m.phase() == Phase::needs_setup, "first run");
        std::string code, error;
        Check(!m.host(code, error) && !error.empty(), "no host before setup");
        m.tick();   // nobody presses a button: the setup starts by itself
        Check(m.phase() == Phase::setting_up, "the first tick starts the setup");
        m.advance(2.0);
        m.tick();
        std::vector<SetupStep> steps = m.setup_steps();
        Check(steps.size() == 4 && steps[0].state == SetupStep::State::done &&
                  steps[1].state == SetupStep::State::running && steps[3].state == SetupStep::State::pending,
              "setup steps progress");
        m.advance(20.0);
        m.tick();
        Check(m.phase() == Phase::ready, "setup done");
        Check(ui::SummarizeSetup(m.setup_steps()).done == 4, "all steps done");
        Check(m.host(code, error) && m.phase() == Phase::launching && CheckJoinCode(code, 0).ok, "host");
        Check(!m.check_code(code).ok, "the mock refuses its own code");
        LinkView l = m.link();
        Check(!l.connected && !l.headline.empty(), "launching status");
        m.advance(5.0);
        m.tick();
        Check(m.phase() == Phase::in_game && !m.link().connected, "waiting for the partner");
        std::string path;
        Check(!m.export_save(path, error) && !error.empty(), "no export during a session");
        Check(!m.uninstall(path, error), "no uninstall during a session");
        Check(!m.cancel_launch(error), "no cancel once the game runs");
        m.advance(6.0);
        m.tick();
        l = m.link();
        Check(l.connected && l.ping_ms > 0 && !l.partner_name.empty() && !l.partner_mission.empty(), "connected");
        m.advance(13.0);   // 20 s into the game
        m.tick();
        l = m.link();
        Check(l.handoff_active && l.handoff_progress > 0.4f && l.handoff_progress < 0.6f, "handoff progress");
        m.advance(10.0);
        m.tick();
        Check(!m.link().handoff_active && m.link().connected, "handoff done");
        m.advance(62.0);   // 92 s: the game closes at 90 s
        m.tick();
        Check(m.phase() == Phase::after_game, "the game closed");
        m.advance(10.0);
        m.tick();
        Check(m.phase() == Phase::ready, "tidied up");
    }
    {   // Join, cancel, tools, uninstall.
        MockOptions o;
        o.scenario = MockScenario::ready;
        MockModel m(o);
        m.use_manual_clock(0.0);
        std::string error, path;
        Check(!m.join("MGS2-bad", error) && !error.empty(), "bad join code");
        Check(m.join("", error) && m.phase() == Phase::launching, "wait for an invite");
        Check(m.link().headline.find("Starting") != std::string::npos, "launching headline");
        Check(m.cancel_launch(error) && m.phase() == Phase::ready, "cancel");
        Check(m.export_save(path, error) && path.find("mgs-coop-save-") != std::string::npos, "export path");
        Check(!m.import_save("C:\\bad.zip", error) && !error.empty(), "import refuses a bad file");
        Check(m.import_save("C:\\partner.zip", error), "import");
        Check(m.restore_save(error), "restore");
        Check(!m.restore_save(error) && !error.empty(), "nothing left to restore");
        Check(m.collect_logs(path, error) && path.find("mgs-coop-logs-") != std::string::npos, "logs");
        m.set_hud("full");
        Check(m.hud() == "full", "hud stored");
        m.set_hud("bogus");
        Check(m.hud() == "compact", "hud normalized");
        m.set_character(Character::dd_female);
        Check(m.character() == Character::dd_female, "character stored");
        std::string report;
        // Like the real model: the loader is gone afterwards, so co-op can't start (and is not set up again behind the player's back).
        Check(m.uninstall(report, error) && !report.empty() && m.phase() == Phase::error &&
                  m.last_error().find("dinput8.dll") != std::string::npos,
              "uninstall");
        m.advance(5.0);
        m.tick();
        Check(m.phase() == Phase::error, "no automatic setup after an uninstall");
    }
    {   // Scenarios.
        MockOptions o;
        o.scenario = MockScenario::wrong_folder;
        MockModel m(o);
        Check(m.phase() == Phase::not_in_game_folder && !m.discovered_game_dir().empty() &&
                  m.game_dir() != m.discovered_game_dir(),
              "wrong folder");
        m.recheck();
        Check(m.phase() == Phase::needs_setup, "recheck moves on");
        m.tick();
        Check(m.phase() == Phase::setting_up, "and then sets up by itself");

        o.scenario = MockScenario::error;
        MockModel e(o);
        Check(e.phase() == Phase::error && !e.last_error().empty(), "error scenario");
        e.recheck();
        Check(e.phase() == Phase::ready && e.last_error().empty(), "error fixed");

        o.scenario = MockScenario::setup_fails;
        MockModel f(o);
        f.use_manual_clock(0.0);
        f.tick();
        Check(f.phase() == Phase::setting_up, "the setup starts by itself");
        f.advance(30.0);
        f.tick();
        const ui::SetupSummary s = ui::SummarizeSetup(f.setup_steps());
        Check(f.phase() == Phase::needs_setup && s.failed && s.current == 3 && !f.last_error().empty(),
              "setup fails once");
        Check(ui::GetSetupView(f.phase(), s) == ui::SetupView::failed, "the failure banner");
        for (int i = 0; i < 5; ++i) {
            f.advance(10.0);
            f.tick();
        }
        Check(f.phase() == Phase::needs_setup && ui::SummarizeSetup(f.setup_steps()).failed,
              "a failed setup waits for Try again (no retry loop)");
        f.start_setup();   // Try again
        f.advance(30.0);
        f.tick();
        Check(f.phase() == Phase::ready, "second try works");

        o.scenario = MockScenario::steam_offline;
        MockModel so(o);
        std::string code, error;
        Check(!so.host(code, error) && error.find("Steam") != std::string::npos, "Steam offline host");
        Check(!so.join(MockModel::PartnerCode(), error) && error.find("Steam") != std::string::npos,
              "Steam offline join");

        o.scenario = MockScenario::partner_drops;
        MockModel pd(o);
        pd.use_manual_clock(0.0);
        pd.jump_to_session(Role::host, false, Phase::in_game, 40.0);
        Check(pd.phase() == Phase::in_game && !pd.link().connected &&
                  pd.link().headline.find("left") != std::string::npos,
              "partner left");
    }
    {   // Jumps used by the screenshot driver.
        MockModel m(MockOptions{});
        m.use_manual_clock(100.0);
        m.jump_to_setup(3, 0.45f, false);
        const ui::SetupSummary s = ui::SummarizeSetup(m.setup_steps());
        Check(m.phase() == Phase::setting_up && s.current == 3 && s.done == 3, "jump to setup");
        m.jump_to_setup(2, 0.5f, true);
        Check(m.phase() == Phase::needs_setup && ui::SummarizeSetup(m.setup_steps()).failed, "jump to failed setup");
        m.jump_to_session(Role::join, false, Phase::after_game, 1.0);
        Check(m.phase() == Phase::after_game, "jump to after game");
        m.jump_to_phase(Phase::ready);
        Check(m.phase() == Phase::ready && ui::SummarizeSetup(m.setup_steps()).done == 4, "jump to ready");
    }
}
}  // namespace

int main() {
    TestNavigation();
    TestSetupView();
    TestScenes();
    TestDebugGating();
    TestUpdateGate();
    TestCharactersAndHud();
    TestJoinCode();
    TestJoinField();
    TestStatusHelpers();
    TestCommandLine();
    TestMock();
    std::printf("launcher_ui_tests: %u checks, %u failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
