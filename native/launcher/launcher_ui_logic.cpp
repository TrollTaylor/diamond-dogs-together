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
        } else if (Is(a, L"--mock-speed")) {
            if (!has_value || !ParseNumber(args[i + 1], 0.1, 100.0, o.mock_speed)) {
                o.error = L"--mock-speed needs a number from 0.1 to 100.";
                return o;
            }
            ++i;
        } else if (Is(a, L"--scale")) {
            double v = 0.0;
            if (!has_value || !ParseNumber(args[i + 1], 0.5, 4.0, v)) { o.error = L"--scale needs a number from 0.5 to 4."; return o; }
            o.scale = static_cast<float>(v);
            ++i;
        } else if (Is(a, L"--help") || Is(a, L"-h") || Is(a, L"/?")) {
            o.mode = Options::Mode::help;
            ++modes;
        } else {
            o.error = L"Unknown option: " + a;
            return o;
        }
    }
    if (modes > 1) o.error = L"Use only one of --watch, --selftest, --screenshot-all and --help.";
    return o;
}

const wchar_t* UsageText() {
    return L"DiamondDogsTogether.exe [options]\n"
           L"  (no options)              open the launcher window\n"
           L"  --mock                    use the simulated model (the default for now)\n"
           L"  --mock-scenario <name>    first-run | ready | wrong-folder | setup-fails | error |\n"
           L"                            steam-offline | partner-drops\n"
           L"  --mock-speed <factor>     run the simulation faster or slower\n"
           L"  --scale <factor>          force a UI scale instead of the monitor's DPI\n"
           L"  --screenshot-all <dir>    render every page once (hidden) and save PNGs\n"
           L"  --selftest                run the built-in checks; exit code 0 = passed\n"
           L"  --watch <session>         background helper while the game runs (no window)\n";
}

}  // namespace ddt::ui
