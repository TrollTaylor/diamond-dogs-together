// Diamond Dogs Together: pure GUI logic (see launcher_ui_logic.hpp).
#include "launcher_ui_logic.hpp"

#include <cwchar>

namespace ddt::ui {

const char* PageLabel(Page page) {
    switch (page) {
    case Page::setup: return "Setup";
    case Page::play: return "Play";
    case Page::in_game: return "In game";
    case Page::tools: return "Tools";
    case Page::settings: return "Settings";
    }
    return "";
}

bool SessionActive(Phase phase) {
    return phase == Phase::launching || phase == Phase::in_game || phase == Phase::after_game;
}

bool ToolsAllowed(Phase phase) {
    return phase == Phase::ready || phase == Phase::needs_setup || phase == Phase::error;
}

namespace {
bool SetupPhase(Phase phase) {
    return phase == Phase::not_in_game_folder || phase == Phase::needs_setup || phase == Phase::setting_up;
}
}  // namespace

bool PageVisible(Phase phase, Page page, Page current) {
    if (page == Page::setup) return SetupPhase(phase) || current == Page::setup;
    return true;
}

bool PageEnabled(Phase phase, Page page) {
    switch (page) {
    case Page::setup: return SetupPhase(phase) || phase == Phase::ready;
    case Page::play:
        return phase == Phase::ready || phase == Phase::error || SessionActive(phase);
    case Page::in_game: return SessionActive(phase);
    case Page::tools: return phase != Phase::not_in_game_folder && phase != Phase::setting_up;
    case Page::settings: return phase != Phase::not_in_game_folder;
    }
    return false;
}

Page DefaultPage(Phase phase) {
    if (SetupPhase(phase)) return Page::setup;
    if (SessionActive(phase)) return Page::in_game;
    return Page::play;
}

Page PageAfterPhaseChange(Phase from, Phase to, Page current, Role role) {
    Page next = current;
    const bool side_page = current == Page::tools || current == Page::settings;
    if (from != to) {
        switch (to) {
        case Phase::not_in_game_folder:
        case Phase::setting_up: next = Page::setup; break;
        case Phase::needs_setup:
            // After an uninstall the player stays on Tools to read the report.
            if (!side_page) next = Page::setup;
            break;
        case Phase::launching:
            if (!side_page) next = role == Role::host ? Page::play : Page::in_game;
            break;
        case Phase::in_game:
            if (current == Page::play || current == Page::setup) next = Page::in_game;
            break;
        case Phase::after_game:
            if (current == Page::play) next = Page::in_game;
            break;
        case Phase::ready:
            // setting_up -> ready keeps the "All set" screen; a finished session returns to Play.
            if (current == Page::in_game) next = Page::play;
            break;
        case Phase::error:
            if (!side_page) next = Page::play;
            break;
        }
    }
    if (!PageEnabled(to, next)) next = DefaultPage(to);
    return next;
}

CharacterChoice ChoiceOf(Character character) {
    switch (character) {
    case Character::story: return CharacterChoice::story;
    case Character::ocelot: return CharacterChoice::ocelot;
    case Character::dd_male:
    case Character::dd_female: return CharacterChoice::dd_soldier;
    case Character::avatar: return CharacterChoice::avatar;
    }
    return CharacterChoice::dd_soldier;
}

bool IsFemale(Character character) { return character == Character::dd_female; }

Character CharacterOf(CharacterChoice choice, bool female) {
    switch (choice) {
    case CharacterChoice::story: return Character::story;
    case CharacterChoice::ocelot: return Character::ocelot;
    case CharacterChoice::dd_soldier: return female ? Character::dd_female : Character::dd_male;
    case CharacterChoice::avatar: return Character::avatar;
    }
    return Character::dd_male;
}

int HudIndex(const std::string& hud) {
    if (hud == "full") return 1;
    if (hud == "off") return 2;
    return 0;
}

const char* HudValue(int index) {
    switch (index) {
    case 1: return "full";
    case 2: return "off";
    default: return "compact";
    }
}

JoinFieldView EvaluateJoinField(const std::string& text, bool wait_for_invite, const CodeCheck& check, bool busy) {
    JoinFieldView view;
    if (busy) {
        view.field_enabled = false;
        view.message = "A game is already running. Close it to join someone else.";
        return view;
    }
    if (wait_for_invite) {
        view.field_enabled = false;
        view.can_join = true;
        view.message = "The game starts and waits. Accept your partner's invite from the Steam overlay (Shift+Tab).";
        return view;
    }
    bool blank = true;
    for (char c : text)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') blank = false;
    if (blank) {
        view.message = "Paste the code your partner sent you.";
        return view;
    }
    view.can_join = check.ok;
    view.tone = check.ok ? Tone::good : Tone::bad;
    view.message = check.message.empty() ? (check.ok ? "The code looks right." : "That code doesn't work.") : check.message;
    return view;
}

CodeDisplay SplitJoinCode(const std::string& code) {
    CodeDisplay out;
    const std::size_t at = code.rfind('@');
    if (at == std::string::npos) {
        out.main = code;
    } else {
        out.main = code.substr(0, at);
        out.account = code.substr(at);
    }
    return out;
}

Tone PingTone(int ms) {
    if (ms <= 0) return Tone::neutral;
    if (ms < 90) return Tone::good;
    if (ms < 180) return Tone::warn;
    return Tone::bad;
}

std::string FormatPing(int ms) {
    if (ms <= 0) return "-";
    return std::to_string(ms) + " ms";
}

int PercentOf(float fraction) {
    if (!(fraction > 0.0f)) return 0;   // also NaN
    if (fraction >= 1.0f) return 100;
    return static_cast<int>(fraction * 100.0f);
}

SetupSummary SummarizeSetup(const std::vector<SetupStep>& steps) {
    SetupSummary s;
    s.total = static_cast<int>(steps.size());
    if (steps.empty()) return s;
    float sum = 0.0f;
    for (int i = 0; i < s.total; ++i) {
        const SetupStep& step = steps[static_cast<std::size_t>(i)];
        switch (step.state) {
        case SetupStep::State::done:
            ++s.done;
            sum += 1.0f;
            break;
        case SetupStep::State::running: {
            s.running = true;
            if (s.current < 0) s.current = i;
            float p = step.progress;
            if (!(p > 0.0f)) p = 0.0f;
            if (p > 1.0f) p = 1.0f;
            sum += p;
            break;
        }
        case SetupStep::State::failed:
            s.failed = true;
            if (s.current < 0 || !s.running) s.current = i;
            break;
        case SetupStep::State::pending: break;
        }
    }
    s.overall = sum / static_cast<float>(s.total);
    return s;
}

int IdleWaitMs(Phase phase, bool animating) {
    if (animating) return 16;
    switch (phase) {
    case Phase::setting_up:
    case Phase::launching:
    case Phase::after_game: return 100;
    case Phase::in_game: return 250;
    default: return 1000;
    }
}

namespace {
bool Is(const std::wstring& a, const wchar_t* b) { return a == b; }

bool ParseNumber(const std::wstring& text, double lo, double hi, double& out) {
    if (text.empty() || text.size() > 16) return false;
    wchar_t* end = nullptr;
    const double v = std::wcstod(text.c_str(), &end);
    if (!end || *end != L'\0' || !(v >= lo && v <= hi)) return false;
    out = v;
    return true;
}

// A process id: 1 to 4294967295, decimal digits only.
bool ParsePid(const std::wstring& text, std::uint32_t& out) {
    if (text.empty() || text.size() > 10) return false;
    unsigned long long v = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') return false;
        v = v * 10 + static_cast<unsigned long long>(c - L'0');
    }
    if (v == 0 || v > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(v);
    return true;
}

// http://127.0.0.1[:port]/... or http://localhost[:port]/... and nothing else
// (no user info, no other host): the developer update test feed.
bool IsLoopbackFeed(const std::wstring& url) {
    const std::wstring scheme = L"http://";
    if (url.compare(0, scheme.size(), scheme) != 0) return false;
    const std::size_t slash = url.find(L'/', scheme.size());
    if (slash == std::wstring::npos) return false;
    std::wstring authority = url.substr(scheme.size(), slash - scheme.size());
    const std::size_t colon = authority.find(L':');
    if (colon != std::wstring::npos) {
        const std::wstring port = authority.substr(colon + 1);
        if (port.empty() || port.size() > 5) return false;
        for (wchar_t c : port)
            if (c < L'0' || c > L'9') return false;
        authority.resize(colon);
    }
    return authority == L"127.0.0.1" || authority == L"localhost";
}

bool ValidSession(const std::wstring& s) {
    if (s.empty() || s.size() > 64) return false;
    for (wchar_t c : s)
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'-' || c == L'_'))
            return false;
    return true;
}
}  // namespace

Options ParseCommandLine(const std::vector<std::wstring>& args) {
    Options o;
    int modes = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::wstring& a = args[i];
        const bool has_value = i + 1 < args.size();
        if (Is(a, L"--mock")) {
            o.mock = true;
        } else if (Is(a, L"--watch")) {
            if (!has_value || !ValidSession(args[i + 1])) { o.error = L"--watch needs a session id."; return o; }
            o.mode = Options::Mode::watch;
            o.watch_session = args[++i];
            ++modes;
        } else if (Is(a, L"--selftest")) {
            o.mode = Options::Mode::selftest;
            ++modes;
        } else if (Is(a, L"--screenshot-all")) {
            if (!has_value || args[i + 1].empty()) { o.error = L"--screenshot-all needs a folder."; return o; }
            o.mode = Options::Mode::screenshots;
            o.screenshot_dir = args[++i];
            ++modes;
        } else if (Is(a, L"--mock-scenario")) {
            if (!has_value || args[i + 1].empty()) { o.error = L"--mock-scenario needs a name."; return o; }
            o.scenario = args[++i];
            o.mock = true;
        } else if (Is(a, L"--mock-speed")) {
            if (!has_value || !ParseNumber(args[i + 1], 0.1, 100.0, o.mock_speed)) {
                o.error = L"--mock-speed needs a number from 0.1 to 100.";
                return o;
            }
            o.mock = true;
            ++i;
        } else if (Is(a, L"--scale")) {
            double v = 0.0;
            if (!has_value || !ParseNumber(args[i + 1], 0.5, 4.0, v)) { o.error = L"--scale needs a number from 0.5 to 4."; return o; }
            o.scale = static_cast<float>(v);
            ++i;
        } else if (Is(a, L"--help") || Is(a, L"-h") || Is(a, L"/?")) {
            o.mode = Options::Mode::help;
            ++modes;
        } else if (Is(a, L"--write-session")) {
            if (!has_value || (!Is(args[i + 1], L"host") && !Is(args[i + 1], L"join"))) {
                o.error = L"--write-session needs host or join.";
                return o;
            }
            o.mode = Options::Mode::write_session;
            o.write_host = Is(args[++i], L"host");
            ++modes;
        } else if (Is(a, L"--end-session")) {
            o.mode = Options::Mode::end_session;
            ++modes;
        } else if (Is(a, L"--code")) {
            if (!has_value || args[i + 1].empty()) { o.error = L"--code needs the host's join code."; return o; }
            o.write_code = args[++i];
        } else if (Is(a, L"--character")) {
            if (!has_value || args[i + 1].empty()) { o.error = L"--character needs a name."; return o; }
            o.write_character = args[++i];
        } else if (Is(a, L"--game-dir")) {
            if (!has_value || args[i + 1].empty()) { o.error = L"--game-dir needs a folder."; return o; }
            o.game_dir = args[++i];
        } else if (Is(a, L"--after-update")) {
            if (!has_value || !ParsePid(args[i + 1], o.after_update)) { o.error = L"--after-update needs a process id."; return o; }
            ++i;
        } else if (Is(a, L"--update-feed")) {
            if (!has_value || !IsLoopbackFeed(args[i + 1])) {
                o.error = L"--update-feed needs a test feed on this PC: http://127.0.0.1:<port>/... or http://localhost:<port>/...";
                return o;
            }
            o.update_feed = args[++i];
        } else if (Is(a, L"--update-now")) {
            o.update_now = true;
        } else {
            o.error = L"Unknown option: " + a;
            return o;
        }
    }
    if (modes > 1) {
        o.error = L"Use only one of --watch, --selftest, --screenshot-all, --write-session, --end-session and --help.";
        return o;
    }
    const bool session_flag = o.mode == Options::Mode::write_session || o.mode == Options::Mode::end_session;
    if (!o.game_dir.empty() && !session_flag) o.error = L"--game-dir goes with --write-session or --end-session.";
    else if ((!o.write_code.empty() || !o.write_character.empty()) && !(o.mode == Options::Mode::write_session && !o.write_host))
        o.error = L"--code and --character go with --write-session join.";
    else if (session_flag && o.mock) o.error = L"--mock does not go with --write-session or --end-session.";
    else if ((o.after_update || !o.update_feed.empty() || o.update_now) && (o.mode != Options::Mode::gui || o.mock))
        o.error = L"--after-update, --update-feed and --update-now go with the window on the real model (no --mock).";
    else if (o.update_now && o.update_feed.empty())
        o.error = L"--update-now needs --update-feed: it installs from a test feed only.";
    return o;
}

const wchar_t* UsageText() {
    return L"DiamondDogsTogether.exe [options]\n"
           L"  (no options)              open the launcher window\n"
           L"  --mock                    use the simulated model (developer check, no game)\n"
           L"  --mock-scenario <name>    first-run | ready | wrong-folder | setup-fails | error |\n"
           L"                            steam-offline | partner-drops\n"
           L"  --mock-speed <factor>     run the simulation faster or slower\n"
           L"  --scale <factor>          force a UI scale instead of the monitor's DPI\n"
           L"  --screenshot-all <dir>    render every page once (hidden) and save PNGs\n"
           L"  --selftest                run the built-in checks; exit code 0 = passed\n"
           L"  --watch <session>         background helper while the game runs (no window)\n"
           L"Developer only (release-gate checks; no window, no Steam, no clipboard):\n"
           L"  --write-session host|join run the setup if needed and write the session files\n"
           L"                            exactly as Host / Join do, without starting Steam\n"
           L"    --code <code>           join: the host's code (none = wait for a Steam invite)\n"
           L"    --character <name>      join: story | ocelot | dd_male | dd_female | avatar\n"
           L"    --game-dir <dir>        the game folder (default: this program's folder)\n"
           L"  --end-session [--game-dir <dir>]  switch co-op off again (not while MGSV runs)\n"
           L"  --update-feed <url>       read releases from a test feed on this PC\n"
           L"                            (http://127.0.0.1:<port>/... or http://localhost:<port>/...)\n"
           L"  --update-now              install an update from that feed at once\n"
           L"Used by the updater:\n"
           L"  --after-update <pid>      the updated window waits for the old one to close\n";
}

int InstanceClaimWaitMs(std::uint32_t after_update, bool old_exe_from_recent_update) {
    if (old_exe_from_recent_update) return 10000;   // an updater before 0.14.4 exits right after starting us
    if (after_update) return 5000;      // the old window releases the folder as it closes
    return 0;
}

bool CloseForUpdate(const UpdateInfo& info) { return info.status == UpdateStatus::ready_to_restart; }

std::string ReleasePageUrl(const std::string& version) {
    const std::string releases = "https://github.com/TrollTaylor/diamond-dogs-together/releases";
    const std::string plain = version.substr(0, version.find('+'));
    int dots = 0;
    bool digit_before = false;
    for (char c : plain) {
        if (c >= '0' && c <= '9') {
            digit_before = true;
        } else if (c == '.' && digit_before) {
            ++dots;
            digit_before = false;
        } else {
            return releases;
        }
    }
    if (dots != 2 || !digit_before) return releases;
    return releases + "/tag/v" + plain;
}

}  // namespace ddt::ui
