// Diamond Dogs Together: the GUI pages (see launcher_app.hpp).
#include "launcher_app.hpp"

#include <cmath>
#include <cstring>

#include "imgui_internal.h"   // ClosePopupToLevel (the screenshot driver resets popups between scenes)
#include "launcher_join_code.hpp"
#include "launcher_model_mock.hpp"
#include "launcher_theme.hpp"

namespace ddt {
namespace {
using namespace theme;
using ui::Page;

constexpr float kSidebarWidth = 200.0f;
constexpr const char* kPopupUninstall = "##uninstall_confirm";
constexpr const char* kPopupReport = "##uninstall_report";
constexpr const char* kPopupRestore = "##restore_confirm";
constexpr const char* kPopupAbout = "##about";

ImU32 ToneColor(ui::Tone tone) {
    switch (tone) {
    case ui::Tone::good: return col::kGood;
    case ui::Tone::warn: return col::kWarn;
    case ui::Tone::bad: return col::kBad;
    case ui::Tone::neutral: break;
    }
    return col::kTextDim;
}

Icon PageIcon(Page page) {
    switch (page) {
    case Page::setup: return Icon::setup;
    case Page::play: return Icon::play;
    case Page::in_game: return Icon::signal;
    case Page::tools: return Icon::tools;
    case Page::settings: return Icon::sliders;
    }
    return Icon::dot;
}

std::string FileName(const std::string& path) {
    const std::size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

void Gap(float logical) { ImGui::Dummy(ImVec2(0.0f, Px(logical))); }

// An icon on a soft round badge, then the cursor moves right of it.
void IconBadge(Icon icon, ImU32 color, ImU32 background, float logical_size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = Px(logical_size);
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 c(p.x + s * 0.5f, p.y + s * 0.5f);
    d->AddCircleFilled(c, s * 0.5f, background);
    DrawIcon(d, icon, c, s * 0.48f, color);
    ImGui::Dummy(ImVec2(s, s));
}

// A message with a leading icon in a tone colour, wrapped to `wrap_width`
// (from the current cursor; 0 = the rest of the region).
void Message(Icon icon, ImU32 color, const std::string& text, float size = kSmall, float wrap_width = 0.0f) {
    if (text.empty()) return;
    const Font f(GetFonts().regular, size);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float start_x = ImGui::GetCursorPosX();
    const float line = ImGui::GetTextLineHeight();
    DrawIcon(ImGui::GetWindowDrawList(), icon, ImVec2(p.x + Px(7.0f), p.y + line * 0.5f), Px(14.0f), color);
    ImGui::SetCursorScreenPos(ImVec2(p.x + Px(20.0f), p.y));
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::PushTextWrapPos(wrap_width > 0.0f ? start_x + wrap_width : 0.0f);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

const char* PhaseStatus(Phase phase, const LinkView& link, ImU32& color) {
    switch (phase) {
    case Phase::not_in_game_folder: color = col::kBad; return "Wrong folder";
    case Phase::needs_setup: color = col::kWarn; return "Setup needed";
    case Phase::setting_up: color = col::kAccent; return "Setting up";
    case Phase::ready: color = col::kGood; return "Ready to play";
    case Phase::launching: color = col::kAccent; return "Starting the game";
    case Phase::in_game:
        color = link.connected ? col::kGood : col::kAccent;
        return link.connected ? "Connected" : "In game";
    case Phase::after_game: color = col::kAccent; return "Game closed";
    case Phase::error: color = col::kBad; return "Can't start";
    }
    color = col::kTextDim;
    return "";
}

struct CharacterText {
    const char* title;
    const char* description;
};
constexpr CharacterText kCharacters[ui::kCharacterChoiceCount] = {
    {"Story: Quiet or Ocelot", "Quiet while she is on your team, Ocelot otherwise."},
    {"Ocelot", "Revolver Ocelot, with a single revolver."},
    {"Diamond Dogs soldier", nullptr},
    {"Your custom character", "Your own avatar. Your partner may see you as Snake."},
};

struct HudText {
    const char* title;
    const char* description;
};
constexpr HudText kHud[ui::kHudCount] = {
    {"Compact", "A small status line in a corner of the screen."},
    {"Full", "Connection, ping and your partner's status."},
    {"Off", "Nothing on screen. Check this window instead."},
};
}  // namespace

LauncherUi::LauncherUi(LauncherModel& model, ShellServices shell, LogoTexture logo)
    : model_(model), shell_(std::move(shell)), logo_(logo) {}

void LauncherUi::Navigate(Page page) {
    if (ui::PageEnabled(model_.phase(), page)) page_ = page;
}

void LauncherUi::CopyCode() {
    if (hosted_code_.empty()) return;
    ImGui::SetClipboardText(hosted_code_.c_str());
    copied_until_ = time_ + 2.5;
}

void LauncherUi::Frame(double time) {
    time_ = time;
    animating_ = copied_until_ > time_;
    const Phase phase = model_.phase();
    if (first_frame_) {
        first_frame_ = false;
        page_ = ui::DefaultPage(phase);
        last_phase_ = phase;
    } else if (phase != last_phase_) {
        page_ = ui::PageAfterPhaseChange(last_phase_, phase, page_, session_role_);
        if (!ui::SessionActive(phase)) {
            hosted_code_.clear();
            copied_until_ = -1.0;
        }
        last_phase_ = phase;
    }
    if (ui::SessionActive(phase)) {
        // A window opened while the game already runs learns the session here.
        session_role_ = model_.session_role();
        if (session_role_ == Role::host && hosted_code_.empty()) hosted_code_ = model_.join_code();
    }
    if (!ui::PageEnabled(phase, page_)) page_ = ui::DefaultPage(phase);
    link_ = ui::SessionActive(phase) ? model_.link() : LinkView{};
    if (phase == Phase::setting_up || phase == Phase::launching || phase == Phase::after_game || link_.handoff_active)
        animating_ = true;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);   // the sidebar and page panes are square
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    Sidebar(phase);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(28.0f, 22.0f));
    ImGui::BeginChild("##page", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PopStyleVar(2);
    switch (page_) {
    case Page::setup: SetupPage(phase); break;
    case Page::play: PlayPage(phase); break;
    case Page::in_game: InGamePage(phase); break;
    case Page::tools: ToolsPage(phase); break;
    case Page::settings: SettingsPage(phase); break;
    }
    ImGui::EndChild();
    Popups();
    ImGui::End();
}

// ---- Sidebar ---------------------------------------------------------------
void LauncherUi::Sidebar(Phase phase) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::kSidebar);
    ImGui::BeginChild("##nav", ImVec2(Px(kSidebarWidth), 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float w = Px(kSidebarWidth);
    const float h = ImGui::GetWindowHeight();
    d->AddLine(ImVec2(origin.x + w - 0.5f, origin.y), ImVec2(origin.x + w - 0.5f, origin.y + h), col::kBorder);

    // The emblem.
    const float logo_w = Px(124.0f);
    const float logo_h = logo_.width > 0 ? logo_w * logo_.height / logo_.width : Px(108.0f);
    ImGui::SetCursorScreenPos(ImVec2(origin.x + (w - logo_w) * 0.5f, origin.y + Px(22.0f)));
    if (logo_.id) {
        ImGui::Image(ImTextureRef(logo_.id), ImVec2(logo_w, logo_h));
    } else {
        ImGui::Dummy(ImVec2(logo_w, logo_h));
        const Font f(GetFonts().semibold, kHeading);
        d->AddText(ImVec2(origin.x + Px(20.0f), origin.y + Px(60.0f)), col::kText, "Diamond Dogs\nTogether");
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + Px(22.0f) + logo_h + Px(20.0f)));

    // Navigation.
    static const Page kPages[ui::kPageCount] = {Page::setup, Page::play, Page::in_game, Page::tools, Page::settings};
    for (Page page : kPages) {
        if (!ui::PageVisible(phase, page, page_)) continue;
        const bool enabled = ui::PageEnabled(phase, page);
        const bool active = page == page_;
        const ImVec2 p(origin.x + Px(12.0f), ImGui::GetCursorScreenPos().y);
        const ImVec2 size(w - Px(24.0f), Px(40.0f));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(static_cast<int>(page));
        if (!enabled) ImGui::BeginDisabled();
        if (ImGui::InvisibleButton("##navitem", size) && enabled) page_ = page;
        const bool hovered = enabled && ImGui::IsItemHovered();
        if (!enabled) ImGui::EndDisabled();
        ImGui::PopID();
        if (hovered && !active) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 q(p.x + size.x, p.y + size.y);
        if (active) {
            d->AddRectFilled(p, q, col::kCard, Px(8.0f));
            d->AddRectFilled(ImVec2(p.x, p.y + Px(9.0f)), ImVec2(p.x + Px(3.0f), q.y - Px(9.0f)), col::kAccent,
                             Px(2.0f));
        } else if (hovered) {
            d->AddRectFilled(p, q, col::Rgb(0x1A, 0x1C, 0x20), Px(8.0f));
        }
        const ImU32 fg = !enabled ? col::kTextFaint : active ? col::kText : col::kTextDim;
        DrawIcon(d, PageIcon(page), ImVec2(p.x + Px(24.0f), p.y + size.y * 0.5f), Px(17.0f),
                 active ? col::kAccent : fg);
        {
            const Font f(active ? GetFonts().semibold : GetFonts().regular, kBody);
            const float th = ImGui::GetTextLineHeight();
            d->AddText(ImVec2(p.x + Px(44.0f), p.y + (size.y - th) * 0.5f), fg, ui::PageLabel(page));
        }
        if (page == Page::in_game && ui::SessionActive(phase) && !active)
            StatusDot(ImVec2(q.x - Px(14.0f), p.y + size.y * 0.5f), 3.5f, link_.connected ? col::kGood : col::kAccent);
        ImGui::SetCursorScreenPos(ImVec2(origin.x, q.y + Px(4.0f)));
    }
    ImGui::Dummy(ImVec2(0.0f, 0.0f));

    // Status and version at the bottom.
    ImU32 status_color = col::kTextDim;
    const char* status = PhaseStatus(phase, link_, status_color);
    const float bottom = origin.y + h - Px(58.0f);
    d->AddLine(ImVec2(origin.x + Px(16.0f), bottom - Px(12.0f)), ImVec2(origin.x + w - Px(16.0f), bottom - Px(12.0f)),
               col::kBorder);
    StatusDot(ImVec2(origin.x + Px(26.0f), bottom + Px(9.0f)), 4.0f, status_color);
    {
        const Font f(GetFonts().semibold, kSmall);
        d->AddText(ImVec2(origin.x + Px(40.0f), bottom), col::kText, status);
    }
    {
        const Font f(GetFonts().regular, kLabel);
        const std::string version = "Version " + model_.version();
        d->AddText(ImVec2(origin.x + Px(40.0f), bottom + Px(20.0f)), col::kTextFaint, version.c_str());
    }
    ImGui::EndChild();
}

void LauncherUi::PageHeader(const char* title, const char* subtitle) {
    Text(title, col::kText, kTitle, GetFonts().semibold, -1.0f);
    if (subtitle && *subtitle) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(6.0f));
        Text(subtitle, col::kTextDim, kBody, GetFonts().regular, 0.0f);
    }
    Gap(6.0f);
}

void LauncherUi::ResultLine(Result& result) {
    if (result.text.empty()) return;
    if (result.ok && !result.path.empty()) {
        const float button_w = 128.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        const ImVec2 start = ImGui::GetCursorPos();
        Message(Icon::check, col::kGood, result.text, kSmall, avail - Px(button_w + 12.0f));
        const ImVec2 after = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(start.x + avail - Px(button_w), start.y - Px(8.0f)));
        if (Button("Show in folder##result", ButtonKind::quiet, ImVec2(button_w, 32.0f), true, Icon::folder, true))
            if (shell_.reveal_file) shell_.reveal_file(result.path);
        ImGui::SetCursorPos(ImVec2(start.x, std::fmax(after.y, start.y + Px(26.0f))));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
        return;
    }
    Message(result.ok ? Icon::check : Icon::warning, result.ok ? col::kGood : col::kBad, result.text);
}

// ---- Setup -----------------------------------------------------------------
void LauncherUi::WrongFolderPage() {
    PageHeader("Move me to your game folder",
               "Diamond Dogs Together has to be in the same folder as MGSV (the folder with mgsvtpp.exe).");
    const std::string here = model_.game_dir();
    const std::string found = model_.discovered_game_dir();
    BeginCard("##where", 0.0f, col::kCard, col::Rgb(0xEB, 0x5D, 0x55, 0x90));
    SectionLabel("THIS COPY IS IN");
    Text(here.c_str(), col::kTextDim, kSmall, GetFonts().mono, 0.0f);
    EndCard();
    Gap(4.0f);
    BeginCard("##how");
    SectionLabel("HOW TO FIX IT");
    Text("1.  Open your MGSV folder.", col::kText, kBody, nullptr, 0.0f);
    Text("2.  Extract everything from the Diamond Dogs Together zip into it.", col::kText, kBody, nullptr, 0.0f);
    Text("3.  Start DiamondDogsTogether.exe from there.", col::kText, kBody, nullptr, 0.0f);
    Gap(4.0f);
    if (!found.empty()) {
        SectionLabel("STEAM HAS MGSV HERE");
        Text(found.c_str(), col::kTextDim, kSmall, GetFonts().mono, 0.0f);
    } else {
        Text("In Steam, right-click Metal Gear Solid V: The Phantom Pain, then Manage > Browse local files.",
             col::kTextDim, kSmall, nullptr, 0.0f);
    }
    EndCard();
    Gap(6.0f);
    if (!found.empty()) {
        if (Button("Open my game folder", ButtonKind::primary, ImVec2(0, 40.0f), true, Icon::folder, true))
            if (shell_.open_folder) shell_.open_folder(found);
        ImGui::SameLine();
    }
    if (Button("Check again", found.empty() ? ButtonKind::primary : ButtonKind::secondary, ImVec2(0, 40.0f)))
        model_.recheck();
}

void LauncherUi::SetupPage(Phase phase) {
    if (phase == Phase::not_in_game_folder) {
        WrongFolderPage();
        return;
    }
    const std::vector<SetupStep> steps = model_.setup_steps();
    const ui::SetupSummary sum = ui::SummarizeSetup(steps);
    const bool done = phase == Phase::ready;
    if (done)
        PageHeader("All set", "Co-op is ready. You only had to do this once.");
    else
        PageHeader("Set up co-op", "A one-time setup. It takes about a minute and never changes your own saves.");

    BeginCard("##steps");
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const SetupStep& step = steps[i];
        ImGui::PushID(static_cast<int>(i));
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float badge = Px(26.0f);
        ImDrawList* d = ImGui::GetWindowDrawList();
        const ImVec2 c(p.x + badge * 0.5f, p.y + badge * 0.5f);
        switch (step.state) {
        case SetupStep::State::pending: {
            d->AddCircle(c, badge * 0.5f - Px(1.0f), col::kBorderStrong, 0, std::fmax(1.0f, Px(1.5f)));
            const Font f(GetFonts().semibold, kSmall);
            const std::string n = std::to_string(i + 1);
            const ImVec2 ts = ImGui::CalcTextSize(n.c_str());
            d->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col::kTextFaint, n.c_str());
            break;
        }
        case SetupStep::State::running: Spinner(c, 9.0f, col::kAccent, time_); break;
        case SetupStep::State::done:
            d->AddCircleFilled(c, badge * 0.5f, col::kGoodSoft);
            DrawIcon(d, Icon::check, c, badge * 0.62f, col::kGood);
            break;
        case SetupStep::State::failed:
            d->AddCircleFilled(c, badge * 0.5f, col::kBadSoft);
            DrawIcon(d, Icon::cross, c, badge * 0.62f, col::kBad);
            break;
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x + badge + Px(14.0f), p.y + Px(2.0f)));
        ImGui::BeginGroup();
        const bool pending = step.state == SetupStep::State::pending;
        const float text_w = ImGui::GetContentRegionAvail().x;
        {
            const Font f(GetFonts().semibold, kBody);
            ImGui::PushStyleColor(ImGuiCol_Text, pending ? col::kTextDim : col::kText);
            ImGui::TextUnformatted(step.label.c_str());
            ImGui::PopStyleColor();
            if (step.state == SetupStep::State::running) {
                const std::string pct = std::to_string(ui::PercentOf(step.progress)) + "%";
                const float pw = ImGui::CalcTextSize(pct.c_str()).x;
                ImGui::SameLine(text_w - pw);
                ImGui::PushStyleColor(ImGuiCol_Text, col::kAccent);
                ImGui::TextUnformatted(pct.c_str());
                ImGui::PopStyleColor();
            }
        }
        if (!step.detail.empty()) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(6.0f));
            Text(step.detail.c_str(), step.state == SetupStep::State::failed ? col::kBad : col::kTextDim, kSmall,
                 nullptr, text_w);
        }
        if (step.state == SetupStep::State::running) {
            Gap(0.0f);
            ProgressBar(step.progress, text_w / Scale(), 4.0f, col::kAccent);
        }
        ImGui::EndGroup();
        ImGui::PopID();
        if (i + 1 < steps.size()) {
            Gap(2.0f);
            const ImVec2 lp = ImGui::GetCursorScreenPos();
            d->AddLine(ImVec2(lp.x + badge + Px(14.0f), lp.y), ImVec2(lp.x + ImGui::GetContentRegionAvail().x, lp.y),
                       col::kBorder);
            Gap(8.0f);
        }
    }
    EndCard();
    Gap(6.0f);

    if (phase == Phase::setting_up) {
        // Steps differ a lot in length, so the page names the step instead of an overall percentage.
        const std::string text = "Setting up: step " + std::to_string(sum.current >= 0 ? sum.current + 1 : sum.done) +
                                 " of " + std::to_string(sum.total);
        Message(Icon::info, col::kTextDim, text, kBody);
        Gap(0.0f);
        Text("Keep this window open until it's done. Your own saves are only read, never changed.", col::kTextFaint,
             kSmall, nullptr, 0.0f);
    } else if (done) {
        if (Button("Continue to Play", ButtonKind::primary, ImVec2(0, 42.0f), true, Icon::play, true))
            Navigate(Page::play);
    } else if (sum.failed) {
        if (Button("Try again", ButtonKind::primary, ImVec2(0, 42.0f))) {
            logs_result_ = Result{};
            model_.start_setup();
        }
        ImGui::SameLine();
        if (Button("Collect logs", ButtonKind::secondary, ImVec2(0, 42.0f), true, Icon::logs, true)) {
            std::string path, error;
            const bool ok = model_.collect_logs(path, error);
            logs_result_ = Result{ok, ok ? "Saved to your Desktop: " + FileName(path) : error, path};
        }
        Gap(2.0f);
        ResultLine(logs_result_);
    } else {
        if (Button("Set up co-op", ButtonKind::primary, ImVec2(0, 42.0f), phase == Phase::needs_setup))
            model_.start_setup();
    }
}

// ---- Play ------------------------------------------------------------------
void LauncherUi::PlayPage(Phase phase) {
    if (phase == Phase::error) {
        PageHeader("Play together", nullptr);
        BeginCard("##error", 0.0f, col::kCard, col::Rgb(0xEB, 0x5D, 0x55, 0x90));
        IconBadge(Icon::warning, col::kBad, col::kBadSoft, 34.0f);
        ImGui::SameLine(0.0f, Px(12.0f));
        ImGui::BeginGroup();
        Text("Co-op can't start right now", col::kText, kHeading, GetFonts().semibold, -1.0f);
        Text(model_.last_error().c_str(), col::kTextDim, kBody, nullptr, 0.0f);
        ImGui::EndGroup();
        Gap(4.0f);
        if (Button("Check again", ButtonKind::primary, ImVec2(0, 40.0f))) model_.recheck();
        ImGui::SameLine();
        if (Button("Collect logs", ButtonKind::secondary, ImVec2(0, 40.0f), true, Icon::logs, true)) {
            std::string path, error;
            const bool ok = model_.collect_logs(path, error);
            logs_result_ = Result{ok, ok ? "Saved to your Desktop: " + FileName(path) : error, path};
        }
        ResultLine(logs_result_);
        EndCard();
        return;
    }
    PageHeader("Play together", "Host a game for your partner, or join theirs. You both need Steam running.");
    const bool session = ui::SessionActive(phase);
    static const char* const kTabs[2] = {"Host", "Join"};
    int index = (session ? session_role_ : role_) == Role::host ? 0 : 1;
    if (Segmented("##role", kTabs, 2, &index, 230.0f, !session)) {
        role_ = index == 0 ? Role::host : Role::join;
        play_error_.clear();
    }
    Gap(4.0f);
    if (index == 0)
        HostTab(phase);
    else
        JoinTab(phase);
}

void LauncherUi::CodeCard(bool compact) {
    const ui::CodeDisplay code = ui::SplitJoinCode(hosted_code_);
    BeginCard(compact ? "##codecompact" : "##code", 0.0f, col::kCard, col::Rgb(0xE8, 0x86, 0x2E, 0x8C));
    SectionLabel("YOUR JOIN CODE");
    const bool copied = copied_until_ > time_;
    if (compact) {
        const float button_w = 112.0f;
        const float avail = ImGui::GetContentRegionAvail().x;
        ImGui::BeginGroup();
        Text(code.main.c_str(), col::kText, 17.0f, GetFonts().mono, -1.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(6.0f));
        Text(code.account.c_str(), col::kTextDim, kSmall, GetFonts().mono, -1.0f);
        ImGui::EndGroup();
        ImGui::SameLine(avail - Px(button_w) + ImGui::GetStyle().WindowPadding.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(4.0f));
        if (Button(copied ? "Copied##copy" : "Copy##copy", ButtonKind::secondary, ImVec2(button_w, 36.0f), true,
                   copied ? Icon::check : Icon::copy, true))
            CopyCode();
    } else {
        Text(code.main.c_str(), col::kText, kCode, GetFonts().mono, -1.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(6.0f));
        Text(code.account.c_str(), col::kTextDim, kBody, GetFonts().mono, -1.0f);
        Gap(4.0f);
        if (Button(copied ? "Copied##copy" : "Copy code##copy", ButtonKind::primary, ImVec2(132.0f, 38.0f), true,
                   copied ? Icon::check : Icon::copy, true))
            CopyCode();
        ImGui::SameLine(0.0f, Px(14.0f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(2.0f));
        Text(copied ? "It's on your clipboard. Paste it to your partner in Steam chat or Discord."
                    : "Send it to your partner in Steam chat or Discord.",
             col::kTextDim, kSmall, nullptr, 0.0f);
    }
    EndCard();
}

void LauncherUi::HostTab(Phase phase) {
    const bool session = ui::SessionActive(phase);
    BeginCard("##snake");
    IconBadge(Icon::person, col::kAccent, col::kAccentSoft, 36.0f);
    ImGui::SameLine(0.0f, Px(12.0f));
    ImGui::BeginGroup();
    Text("You play as Snake", col::kText, kBody, GetFonts().semibold, -1.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(8.0f));
    Text("The host is always Snake, because the story follows the host's save.", col::kTextDim, kSmall, nullptr, 0.0f);
    ImGui::EndGroup();
    EndCard();
    Gap(2.0f);

    if (session && session_role_ == Role::host && !hosted_code_.empty()) {
        CodeCard(false);
        Gap(2.0f);
        if (phase == Phase::launching) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            Spinner(ImVec2(p.x + Px(10.0f), p.y + Px(17.0f)), 7.0f, col::kAccent, time_);
            ImGui::SetCursorScreenPos(ImVec2(p.x + Px(28.0f), p.y + Px(8.0f)));
            Text("Starting MGSV through Steam...", col::kTextDim, kBody, nullptr, -1.0f);
            ImGui::SameLine(0.0f, Px(14.0f));
            ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, p.y));
            if (Button("Cancel##launch", ButtonKind::quiet, ImVec2(0, 34.0f))) {
                std::string error;
                if (!model_.cancel_launch(error)) play_error_ = error;
            }
        } else {
            Text("Your partner pastes the code on their Join tab. You can also invite them from the Steam overlay "
                 "(Shift+Tab) in the game.",
                 col::kTextDim, kSmall, nullptr, 0.0f);
            if (Button("See the connection", ButtonKind::quiet, ImVec2(0, 34.0f), true, Icon::signal, true))
                Navigate(Page::in_game);
        }
        Message(Icon::warning, col::kBad, play_error_);
        return;
    }
    if (session) {
        Message(Icon::warning, col::kWarn, "You joined a game. Close it before you host one.", kBody);
        return;
    }
    if (Button("Host game", ButtonKind::primary, ImVec2(190.0f, 46.0f), phase == Phase::ready, Icon::play, true)) {
        std::string code, error;
        if (model_.host(code, error)) {
            hosted_code_ = code;
            session_role_ = Role::host;
            play_error_.clear();
            CopyCode();
        } else {
            play_error_ = error;
        }
    }
    Gap(2.0f);
    Text("MGSV starts through Steam and you get a join code to send to your partner. You can also invite them "
         "from the Steam overlay (Shift+Tab) once you're in the game.",
         col::kTextDim, kSmall, nullptr, 0.0f);
    Gap(2.0f);
    Message(Icon::warning, col::kBad, play_error_);
}

void LauncherUi::JoinTab(Phase phase) {
    const bool session = ui::SessionActive(phase);
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = Px(18.0f);
    const float col_w = std::floor((avail - gap) * 0.5f);
    const float top = ImGui::GetCursorPosY();
    const float left_x = ImGui::GetCursorPosX();

    // Left: who the player is.
    ImGui::BeginGroup();
    SectionLabel("PLAY AS");
    const Character current = model_.character();
    const ui::CharacterChoice choice = ui::ChoiceOf(current);
    const bool female = ui::IsFemale(current);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, Px(7.0f)));
    for (int i = 0; i < ui::kCharacterChoiceCount; ++i) {
        const auto c = static_cast<ui::CharacterChoice>(i);
        const bool selected = c == choice;
        const bool dd = c == ui::CharacterChoice::dd_soldier;
        ImVec2 extra{};
        ImGui::PushID(i);
        if (RadioRow("##char", selected, kCharacters[i].title, kCharacters[i].description, col_w, !session,
                     dd ? 30.0f : 0.0f, dd ? &extra : nullptr))
            model_.set_character(ui::CharacterOf(c, female));
        if (dd) {
            const ImVec2 back = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(extra);
            static const char* const kSex[2] = {"Male", "Female"};
            int sex = female ? 1 : 0;
            if (Segmented("##sex", kSex, 2, &sex, 150.0f, !session, selected))
                model_.set_character(ui::CharacterOf(ui::CharacterChoice::dd_soldier, sex == 1));
            ImGui::SetCursorScreenPos(back);
        }
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    // Right: the code.
    ImGui::SetCursorPos(ImVec2(left_x + col_w + gap, top));
    ImGui::BeginGroup();
    SectionLabel("JOIN CODE");
    const std::string text(code_buf_);
    if (text != checked_text_) {
        checked_text_ = text;
        checked_ = model_.check_code(text);
    }
    const ui::JoinFieldView view = ui::EvaluateJoinField(text, wait_invite_, checked_, session);
    // "Paste" sits at the right end of the label row, so the field can use the
    // whole column (a code is 49 characters: two lines here).
    const float paste_w = 82.0f;
    ImGui::SameLine(col_w - Px(paste_w));   // (group-relative)
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(9.0f));
    const bool paste = Button("Paste##code", ButtonKind::quiet, ImVec2(paste_w, 28.0f), view.field_enabled,
                              Icon::paste, true);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4.0f));
    {
        const Font f(GetFonts().mono, kSmall);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, Px(11.0f, 10.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, view.tone == ui::Tone::bad    ? col::Rgb(0xEB, 0x5D, 0x55, 0xA0)
                                               : view.tone == ui::Tone::good ? col::Rgb(0x5C, 0xC0, 0x7A, 0xA0)
                                                                             : col::kBorder);
        if (!view.field_enabled) ImGui::BeginDisabled();
        const bool enter = ImGui::InputTextMultiline(
            "##code", code_buf_, sizeof(code_buf_), ImVec2(col_w, ImGui::GetTextLineHeight() * 2.0f + Px(22.0f)),
            ImGuiInputTextFlags_WordWrap | ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_CtrlEnterForNewLine | ImGuiInputTextFlags_AutoSelectAll);
        const bool active = ImGui::IsItemActive();
        const ImVec2 field_min = ImGui::GetItemRectMin();
        if (!view.field_enabled) ImGui::EndDisabled();
        if (code_buf_[0] == 0 && !active) {
            ImGui::GetWindowDrawList()->AddText(GetFonts().mono, ImGui::GetFontSize(),
                                                ImVec2(field_min.x + Px(11.0f), field_min.y + Px(10.0f)),
                                                col::kTextFaint, "MGS2-XXXXX-XXXXX-XXXXX-XXXXX-XX @7656...", nullptr,
                                                col_w - Px(22.0f));
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (enter && view.can_join && !wait_invite_) {
            std::string error;
            session_role_ = Role::join;
            if (!model_.join(text, error)) play_error_ = error; else play_error_.clear();
        }
    }
    if (paste) {
        const char* clip = ImGui::GetClipboardText();
        if (clip) {
            std::string trimmed(clip);
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' '))
                trimmed.pop_back();
            strncpy_s(code_buf_, sizeof(code_buf_), trimmed.c_str(), _TRUNCATE);
        }
    }
    {
        const Icon icon = view.tone == ui::Tone::good  ? Icon::check
                          : view.tone == ui::Tone::bad ? Icon::warning
                                                       : Icon::info;
        Message(icon, ToneColor(view.tone), view.message, kSmall, col_w);
    }
    Gap(4.0f);
    if (Checkbox("Wait for a Steam invite instead", &wait_invite_, !session)) play_error_.clear();
    Gap(10.0f);
    if (session && session_role_ == Role::join) {
        Message(Icon::signal, col::kAccent,
                phase == Phase::launching ? "Starting MGSV through Steam..." : "You're in the game.", kBody);
        if (Button("See the connection", ButtonKind::quiet, ImVec2(0, 34.0f), true, Icon::signal, true))
            Navigate(Page::in_game);
    } else {
        if (Button("Join game", ButtonKind::primary, ImVec2(col_w / Scale(), 46.0f), view.can_join, Icon::play, true)) {
            std::string error;
            session_role_ = Role::join;
            if (!model_.join(wait_invite_ ? std::string() : text, error)) play_error_ = error;
            else play_error_.clear();
        }
    }
    Gap(2.0f);
    Message(Icon::warning, col::kBad, play_error_, kSmall, col_w);
    ImGui::EndGroup();
}

// ---- In game ---------------------------------------------------------------
void LauncherUi::InGamePage(Phase phase) {
    if (!ui::SessionActive(phase)) {
        PageHeader("In game", "No game is running.");
        Text("Host or join a game on the Play page. Its connection shows up here.", col::kTextDim, kBody, nullptr, 0.0f);
        Gap(4.0f);
        if (Button("Go to Play", ButtonKind::primary, ImVec2(0, 40.0f), true, Icon::play, true)) Navigate(Page::play);
        return;
    }
    const bool host = session_role_ == Role::host;
    PageHeader("In game", host ? "You're hosting. Your partner joins you." : "You're joining your partner's game.");
    const LinkView& l = link_;

    // Status.
    const bool busy = phase == Phase::launching || (phase == Phase::after_game && l.detail.find("...") != std::string::npos);
    ImU32 tone = l.connected ? col::kGood : col::kWarn;
    if (phase == Phase::after_game) tone = col::kTextDim;
    BeginCard("##status", 0.0f, col::kCard,
              l.connected ? col::Rgb(0x5C, 0xC0, 0x7A, 0x70) : col::kBorder);
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 c(p.x + Px(17.0f), p.y + Px(17.0f));
        if (busy || l.handoff_active)
            Spinner(c, 10.0f, col::kAccent, time_);
        else if (phase == Phase::after_game)
            DrawIcon(ImGui::GetWindowDrawList(), Icon::check, c, Px(26.0f), col::kGood);
        else
            StatusDot(c, 6.5f, tone);
        ImGui::Dummy(Px(34.0f, 34.0f));
    }
    ImGui::SameLine(0.0f, Px(12.0f));
    ImGui::BeginGroup();
    Text(l.headline.empty() ? "Waiting for the game..." : l.headline.c_str(), col::kText, kHeading,
         GetFonts().semibold, -1.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4.0f));
    Text(l.detail.c_str(), col::kTextDim, kBody, nullptr, 0.0f);
    ImGui::EndGroup();
    if (phase == Phase::launching) {
        Gap(2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Px(46.0f));
        if (Button("Cancel##launch", ButtonKind::secondary, ImVec2(0, 34.0f))) {
            std::string error;
            if (!model_.cancel_launch(error)) play_error_ = error;
        }
    }
    EndCard();

    // Tiles.
    if (phase == Phase::in_game) {
        Gap(2.0f);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float gap = Px(10.0f);
        const float tile_w = (avail - 2 * gap) / 3.0f;
        const std::string ping = ui::FormatPing(l.ping_ms);
        const struct {
            const char* label;
            std::string value;
            ImU32 color;
        } tiles[3] = {
            {"PARTNER", l.partner_name.empty() ? std::string("Not here yet") : l.partner_name,
             l.partner_name.empty() ? col::kTextFaint : col::kText},
            {"PING", ping, l.ping_ms > 0 ? ToneColor(ui::PingTone(l.ping_ms)) : col::kTextFaint},
            {"PARTNER IS IN", l.partner_mission.empty() ? std::string("-") : l.partner_mission,
             l.partner_mission.empty() ? col::kTextFaint : col::kText},
        };
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine(0.0f, gap);
            ImGui::PushID(i);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, col::kCard);
            ImGui::PushStyleColor(ImGuiCol_Border, col::kBorder);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Px(10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(14.0f, 11.0f));
            ImGui::BeginChild("##tile", ImVec2(tile_w, Px(66.0f)), ImGuiChildFlags_Borders,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
            SectionLabel(tiles[i].label);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(4.0f));
            Text(tiles[i].value.c_str(), tiles[i].color, 17.0f, GetFonts().semibold, -1.0f);
            ImGui::EndChild();
            ImGui::PopID();
        }
    }

    // Save handoff.
    if (l.handoff_active) {
        Gap(2.0f);
        BeginCard("##handoff", 0.0f, col::kCard, col::Rgb(0xE8, 0x86, 0x2E, 0x70));
        SectionLabel("SAVE HANDOFF");
        const std::string pct = std::to_string(ui::PercentOf(l.handoff_progress)) + "%";
        {
            const Font f(GetFonts().semibold, kSmall);
            const float pw = ImGui::CalcTextSize(pct.c_str()).x;
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetStyle().WindowPadding.x - pw);
            ImGui::PushStyleColor(ImGuiCol_Text, col::kAccent);
            ImGui::TextUnformatted(pct.c_str());
            ImGui::PopStyleColor();
        }
        ProgressBar(l.handoff_progress, 0.0f, 8.0f, col::kAccent);
        Text("Keep the game open. It may restart by itself once to load the save.", col::kTextDim, kSmall, nullptr,
             0.0f);
        EndCard();
    }

    // The code matters until the partner is in (and again if they leave).
    if (host && !hosted_code_.empty() && phase == Phase::in_game && !l.connected) {
        Gap(2.0f);
        CodeCard(true);
    }
    Message(Icon::warning, col::kBad, play_error_);
    Gap(4.0f);
    Message(Icon::info, col::kTextFaint,
            phase == Phase::after_game
                ? "You can close this window. The tidy-up finishes on its own."
                : "You can close this window. Co-op keeps running and tidies up after the game closes.");
}

// ---- Tools -----------------------------------------------------------------
void LauncherUi::ToolsPage(Phase phase) {
    PageHeader("Tools", nullptr);
    const bool allowed = ui::ToolsAllowed(phase);
    if (ui::SessionActive(phase)) {
        Message(Icon::warning, col::kWarn, "Close the game to share saves or uninstall.", kBody);
        Gap(2.0f);
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = Px(12.0f);
    const float card_w = std::floor((avail - gap) * 0.5f);

    auto card = [&](const char* id, Icon icon, ImU32 icon_color, ImU32 icon_bg, const char* title,
                    const char* description, float logical_height, auto&& body) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, col::kCard);
        ImGui::PushStyleColor(ImGuiCol_Border, col::kBorder);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Px(10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(16.0f, 14.0f));
        ImGui::BeginChild(id, ImVec2(card_w, Px(logical_height)), ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, Px(8.0f, 8.0f));
        IconBadge(icon, icon_color, icon_bg, 30.0f);
        ImGui::SameLine(0.0f, Px(10.0f));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(4.0f));
        Text(title, col::kText, kBody + 1.0f, GetFonts().semibold, -1.0f);
        Text(description, col::kTextDim, kSmall, nullptr, 0.0f);
        // Buttons sit at the bottom of the card so the rows line up.
        body();
        ImGui::PopStyleVar();
        ImGui::EndChild();
    };
    auto bottom = [&](int rows) {   // (the card's own padding is 14 at the bottom)
        const float button_h = Px(34.0f);
        const float y = ImGui::GetWindowHeight() - Px(14.0f) - rows * button_h - (rows - 1) * Px(8.0f);
        if (ImGui::GetCursorPosY() < y) ImGui::SetCursorPosY(y);
    };
    auto result_of = [](bool ok, const std::string& good, const std::string& error, const std::string& path) {
        return Result{ok, ok ? good : error, path};
    };

    card("##share", Icon::share, col::kAccent, col::kAccentSoft, "Share save",
         "Send your co-op progress to your partner, or load theirs.", 180.0f,
         [&] {
             bottom(2);
             const float half = (ImGui::GetContentRegionAvail().x - Px(8.0f)) * 0.5f / Scale();
             if (Button("Export##share", ButtonKind::secondary, ImVec2(half, 34.0f), allowed)) {
                 std::string path, error;
                 const bool ok = model_.export_save(path, error);
                 tool_result_ = result_of(ok, "Saved to your Desktop: " + FileName(path) + ". Send it to your partner.",
                                          error, path);
             }
             ImGui::SameLine(0.0f, Px(8.0f));
             if (Button("Import...##share", ButtonKind::secondary, ImVec2(half, 34.0f), allowed)) {
                 std::string zip;
                 if (shell_.pick_save_zip && shell_.pick_save_zip(zip)) {
                     std::string error;
                     const bool ok = model_.import_save(zip, error);
                     tool_result_ = result_of(ok,
                                              "Loaded your partner's save. Your previous co-op save is kept "
                                              "(Restore previous brings it back), and your Steam saves are untouched.",
                                              error, std::string());
                 }
             }
             if (Button("Restore previous...##share", ButtonKind::secondary, ImVec2(-1.0f, 34.0f), allowed))
                 popup_request_ = Popup::restore_confirm;
         });
    ImGui::SameLine(0.0f, gap);
    card("##logs", Icon::logs, col::kTextDim, col::Rgb(0xA2, 0xA7, 0xAD, 0x22), "Collect logs",
         "Makes a zip on your Desktop to attach to a bug report. It holds no saves, join codes or Steam IDs.", 180.0f,
         [&] {
             bottom(1);
             if (Button("Collect logs##logs", ButtonKind::secondary, ImVec2(-1.0f, 34.0f),
                        phase != Phase::not_in_game_folder)) {
                 std::string path, error;
                 const bool ok = model_.collect_logs(path, error);
                 tool_result_ = result_of(ok, "Saved to your Desktop: " + FileName(path) + ".", error, path);
             }
         });
    Gap(2.0f);
    card("##uninstall", Icon::trash, col::kBad, col::kBadSoft, "Uninstall",
         "Turns co-op off and removes the mod. Your saves and backups stay.", 136.0f, [&] {
             bottom(1);
             if (Button("Uninstall...##uninstall", ButtonKind::secondary, ImVec2(-1.0f, 34.0f), allowed))
                 popup_request_ = Popup::uninstall_confirm;
         });
    ImGui::SameLine(0.0f, gap);
    card("##about", Icon::info, col::kTextDim, col::Rgb(0xA2, 0xA7, 0xAD, 0x22), "About",
         "Version, credits, licences and the source code.", 136.0f, [&] {
             bottom(1);
             if (Button("About and licences##about", ButtonKind::secondary, ImVec2(-1.0f, 34.0f)))
                 popup_request_ = Popup::about;
         });
    Gap(4.0f);
    ResultLine(tool_result_);
}

// ---- Settings --------------------------------------------------------------
void LauncherUi::SettingsPage(Phase phase) {
    PageHeader("Settings", nullptr);
    SectionLabel("STATUS DISPLAY IN THE GAME");
    const int hud = ui::HudIndex(model_.hud());
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, Px(7.0f)));
    for (int i = 0; i < ui::kHudCount; ++i) {
        ImGui::PushID(i);
        if (RadioRow("##hud", hud == i, kHud[i].title, kHud[i].description, w, true)) model_.set_hud(ui::HudValue(i));
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    Text("Changes apply the next time you start a game.", col::kTextFaint, kSmall, nullptr, 0.0f);
    Gap(10.0f);
    SectionLabel("GAME FOLDER");
    BeginCard("##folder");
    const std::string dir = model_.game_dir();
    const float button_w = 96.0f;
    const float text_w = ImGui::GetContentRegionAvail().x - Px(button_w + 12.0f);
    const float y = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(y + Px(8.0f));
    Text(dir.c_str(), col::kTextDim, kSmall, GetFonts().mono, text_w);
    ImGui::SameLine(ImGui::GetStyle().WindowPadding.x + text_w + Px(12.0f));
    ImGui::SetCursorPosY(y);
    if (Button("Open##folder", ButtonKind::secondary, ImVec2(button_w, 34.0f), phase != Phase::not_in_game_folder,
               Icon::folder, true))
        if (shell_.open_folder) shell_.open_folder(dir);
    EndCard();
}

// ---- Popups ----------------------------------------------------------------
void LauncherUi::Popups() {
    switch (popup_request_) {
    case Popup::uninstall_confirm: ImGui::OpenPopup(kPopupUninstall); break;
    case Popup::uninstall_report: ImGui::OpenPopup(kPopupReport); break;
    case Popup::restore_confirm: ImGui::OpenPopup(kPopupRestore); break;
    case Popup::about: ImGui::OpenPopup(kPopupAbout); break;
    case Popup::none: break;
    }
    popup_request_ = Popup::none;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    auto begin = [&](const char* id, float logical_width) {
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(Px(logical_width), 0.0f), ImVec2(Px(logical_width), Px(600.0f)));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, Px(24.0f, 22.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, Px(12.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, std::fmax(1.0f, Px(1.0f)));
        ImGui::PushStyleColor(ImGuiCol_Border, col::kBorderStrong);
        const bool open = ImGui::BeginPopupModal(id, nullptr, flags);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        return open;
    };
    auto right_buttons = [&](float total_logical_width) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - Px(total_logical_width));
    };

    if (begin(kPopupUninstall, 430.0f)) {
        Text("Uninstall co-op?", col::kText, kHeading, GetFonts().semibold, -1.0f);
        Text("This turns co-op off and removes the mod's loader from your game folder. MGSV goes back to normal.",
             col::kTextDim, kBody, nullptr, 0.0f);
        Message(Icon::check, col::kGood, "Kept: your own saves, your co-op save and the backups.", kBody);
        Gap(8.0f);
        right_buttons(110.0f + 130.0f + 10.0f);
        if (Button("Cancel##un", ButtonKind::secondary, ImVec2(110.0f, 38.0f))) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0.0f, Px(10.0f));
        if (Button("Uninstall##un", ButtonKind::danger, ImVec2(130.0f, 38.0f))) {
            std::string report, error;
            if (model_.uninstall(report, error)) {
                uninstall_report_ = report;
                tool_result_ = Result{true, "Co-op is uninstalled.", std::string()};
                popup_request_ = Popup::uninstall_report;
            } else {
                tool_result_ = Result{false, error, std::string()};
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (begin(kPopupReport, 470.0f)) {
        Text("Co-op is uninstalled", col::kText, kHeading, GetFonts().semibold, -1.0f);
        Gap(2.0f);
        std::size_t start = 0;
        while (start < uninstall_report_.size()) {
            std::size_t end = uninstall_report_.find('\n', start);
            if (end == std::string::npos) end = uninstall_report_.size();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            {
                const Font f(GetFonts().regular, kBody);
                DrawIcon(ImGui::GetWindowDrawList(), Icon::dot,
                         ImVec2(p.x + Px(6.0f), p.y + ImGui::GetTextLineHeight() * 0.5f), Px(16.0f), col::kAccent);
            }
            ImGui::SetCursorScreenPos(ImVec2(p.x + Px(20.0f), p.y));
            const std::string line = uninstall_report_.substr(start, end - start);
            Text(line.c_str(), col::kText, kBody, nullptr, 0.0f);
            start = end + 1;
        }
        Gap(8.0f);
        right_buttons(110.0f);
        if (Button("Close##report", ButtonKind::primary, ImVec2(110.0f, 38.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (begin(kPopupRestore, 430.0f)) {
        Text("Bring back your previous co-op save?", col::kText, kHeading, GetFonts().semibold, 0.0f);
        Text("The save from before your last import or save handoff comes back. Your current co-op save is kept as "
             "a backup.",
             col::kTextDim, kBody, nullptr, 0.0f);
        Gap(8.0f);
        right_buttons(110.0f + 120.0f + 10.0f);
        if (Button("Cancel##re", ButtonKind::secondary, ImVec2(110.0f, 38.0f))) ImGui::CloseCurrentPopup();
        ImGui::SameLine(0.0f, Px(10.0f));
        if (Button("Restore##re", ButtonKind::primary, ImVec2(120.0f, 38.0f))) {
            std::string error;
            const bool ok = model_.restore_save(error);
            tool_result_ = Result{ok, ok ? "Your previous co-op save is back." : error, std::string()};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (begin(kPopupAbout, 480.0f)) {
        if (logo_.id) {
            const float lw = Px(72.0f);
            ImGui::Image(ImTextureRef(logo_.id), ImVec2(lw, lw * logo_.height / logo_.width));
            ImGui::SameLine(0.0f, Px(16.0f));
        }
        ImGui::BeginGroup();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Px(8.0f));
        Text("Diamond Dogs Together", col::kText, kHeading, GetFonts().semibold, -1.0f);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - Px(6.0f));
        const std::string version = "Version " + model_.version();
        Text(version.c_str(), col::kTextDim, kSmall, nullptr, -1.0f);
        ImGui::EndGroup();
        Gap(2.0f);
        Text("Two-player co-op for Metal Gear Solid V: The Phantom Pain. A fan project, not affiliated with Konami.",
             col::kTextDim, kBody, nullptr, 0.0f);
        Gap(4.0f);
        SectionLabel("BUILT WITH");
        static const char* const kCredits[][2] = {
            {"Dynamite", "unknown321, GPLv3 with the Commons Clause"},
            {"tpp-mod", "alicealys and contributors, GPLv3"},
            {"MinHook", "Tsuda Kageyu, BSD 2-clause"},
            {"Dear ImGui", "Omar Cornut, MIT"},
        };
        for (const auto& credit : kCredits) {
            Text(credit[0], col::kText, kSmall, GetFonts().semibold, -1.0f);
            ImGui::SameLine(Px(130.0f));
            Text(credit[1], col::kTextDim, kSmall, nullptr, -1.0f);
        }
        Gap(4.0f);
        Text("The full licence texts and the source code (source.zip) are in the mgs-coop folder next to this "
             "program.",
             col::kTextDim, kSmall, nullptr, 0.0f);
        Gap(8.0f);
        if (Button("Open the mgs-coop folder##about", ButtonKind::secondary, ImVec2(0, 38.0f), true, Icon::folder, true))
            if (shell_.open_folder) shell_.open_folder(model_.game_dir() + "\\mgs-coop");
        ImGui::SameLine();
        right_buttons(100.0f);
        if (Button("Close##about", ButtonKind::primary, ImVec2(100.0f, 38.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---- Screenshot scenes -----------------------------------------------------
namespace {
const char* const kScenes[] = {
    "01-setup-start",       "02-setup-progress",      "03-setup-failed",       "04-setup-done",
    "05-wrong-folder",      "06-play-host",           "07-play-host-code",     "08-play-join",
    "09-play-join-invalid", "10-play-join-valid",     "11-play-join-invite",   "12-play-error",
    "13-ingame-launching",  "14-ingame-host-waiting", "15-ingame-connected",   "16-ingame-handoff",
    "17-ingame-after-game", "18-tools",               "19-tools-result",       "20-tools-uninstall-confirm",
    "21-tools-about",       "22-settings",            "23-tools-in-game",      "24-uninstall-report",
};
constexpr int kSceneCount = static_cast<int>(sizeof(kScenes) / sizeof(kScenes[0]));
constexpr char kFixedSecret[] = "K7RD3WQ9MP2XVB6TZHN4";
}  // namespace

int LauncherUi::SceneCount() { return kSceneCount; }
const char* LauncherUi::SceneName(int index) { return index >= 0 && index < kSceneCount ? kScenes[index] : ""; }

void LauncherUi::PrepareScene(int index, MockModel& mock) {
    if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->OpenPopupStack.Size > 0) ImGui::ClosePopupToLevel(0, false);
    // Reset the pages.
    role_ = Role::host;
    session_role_ = Role::host;
    code_buf_[0] = '\0';
    checked_text_.assign("\x01");
    wait_invite_ = false;
    hosted_code_.clear();
    play_error_.clear();
    copied_until_ = -1.0;
    logs_result_ = tool_result_ = Result{};
    uninstall_report_.clear();
    popup_request_ = Popup::none;
    mock.use_manual_clock(1000.0);
    mock.set_character(Character::story);
    mock.set_hud("compact");
    const std::string own_code = FormatJoinCode(kFixedSecret, MockModel::kOwnSteamId);
    auto copy_into = [&](const std::string& s) {
        strncpy_s(code_buf_, sizeof(code_buf_), s.c_str(), _TRUNCATE);
    };
    Page page = Page::play;
    switch (index) {
    case 0: mock.jump_to_phase(Phase::needs_setup); page = Page::setup; break;
    case 1: mock.jump_to_setup(3, 0.45f, false); page = Page::setup; break;
    case 2: mock.jump_to_setup(3, 0.62f, true); page = Page::setup; break;
    case 3: mock.jump_to_phase(Phase::ready); page = Page::setup; break;
    case 4: mock.jump_to_phase(Phase::not_in_game_folder); page = Page::setup; break;
    case 5: mock.jump_to_phase(Phase::ready); break;
    case 6:
        mock.jump_to_session(Role::host, false, Phase::launching, 1.0);
        hosted_code_ = own_code;
        copied_until_ = 1e9;
        break;
    case 7:
        mock.jump_to_phase(Phase::ready);
        role_ = Role::join;
        break;
    case 8: {
        mock.jump_to_phase(Phase::ready);
        role_ = Role::join;
        mock.set_character(Character::ocelot);
        std::string bad = MockModel::PartnerCode();
        bad[bad.find('@') - 1] = bad[bad.find('@') - 1] == 'A' ? 'B' : 'A';
        copy_into(bad);
        break;
    }
    case 9:
        mock.jump_to_phase(Phase::ready);
        role_ = Role::join;
        mock.set_character(Character::dd_female);
        copy_into(MockModel::PartnerCode());
        break;
    case 10:
        mock.jump_to_phase(Phase::ready);
        role_ = Role::join;
        mock.set_character(Character::avatar);
        wait_invite_ = true;
        break;
    case 11: mock.jump_to_phase(Phase::error); break;
    case 12:
        mock.jump_to_session(Role::join, false, Phase::launching, 1.5);
        session_role_ = Role::join;
        page = Page::in_game;
        break;
    case 13:
        mock.jump_to_session(Role::host, false, Phase::in_game, 2.0);
        hosted_code_ = own_code;
        page = Page::in_game;
        break;
    case 14:
        mock.jump_to_session(Role::join, false, Phase::in_game, 9.0);
        session_role_ = Role::join;
        page = Page::in_game;
        break;
    case 15:
        mock.jump_to_session(Role::host, false, Phase::in_game, 21.0);
        hosted_code_ = own_code;
        page = Page::in_game;
        break;
    case 16:
        mock.jump_to_session(Role::join, false, Phase::after_game, 1.0);
        session_role_ = Role::join;
        page = Page::in_game;
        break;
    case 17: mock.jump_to_phase(Phase::ready); page = Page::tools; break;
    case 18: {
        mock.jump_to_phase(Phase::ready);
        page = Page::tools;
        std::string path, error;
        const bool ok = mock.export_save(path, error);
        tool_result_ = Result{ok, ok ? "Saved to your Desktop: " + FileName(path) + ". Send it to your partner." : error,
                              path};
        break;
    }
    case 19:
        mock.jump_to_phase(Phase::ready);
        page = Page::tools;
        popup_request_ = Popup::uninstall_confirm;
        break;
    case 20:
        mock.jump_to_phase(Phase::ready);
        page = Page::tools;
        popup_request_ = Popup::about;
        break;
    case 21: mock.jump_to_phase(Phase::ready); page = Page::settings; break;
    case 22:
        mock.jump_to_session(Role::host, false, Phase::in_game, 9.0);
        hosted_code_ = own_code;
        page = Page::tools;
        break;
    case 23: {
        mock.jump_to_phase(Phase::ready);
        page = Page::tools;
        std::string report, error;
        if (mock.uninstall(report, error)) {
            uninstall_report_ = report;
            tool_result_ = Result{true, "Co-op is uninstalled.", std::string()};
        }
        popup_request_ = Popup::uninstall_report;
        break;
    }
    default: break;
    }
    first_frame_ = false;
    last_phase_ = mock.phase();
    page_ = page;
}

}  // namespace ddt
