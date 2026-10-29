#pragma once
// Diamond Dogs Together: the GUI pages (Play, In game, Tools, Settings; the
// setup runs by itself and shows on Play) drawn with Dear ImGui over a LauncherModel. The Win32/DX11 shell
// (launcher_main.cpp) owns the window and calls Frame() once per redraw.
#include <functional>
#include <string>

#include "imgui.h"
#include "launcher_model.hpp"
#include "launcher_ui_logic.hpp"

namespace ddt {

class MockModel;

// What the window shell does for the pages (Explorer, file dialog).
struct ShellServices {
    std::function<void(const std::string& utf8_folder)> open_folder;
    std::function<void(const std::string& utf8_file)> reveal_file;
    // Shows an "Open" dialog for a .zip; false when cancelled.
    std::function<bool(std::string& utf8_path_out)> pick_save_zip;
    // Shows a folder picker (Settings > Debug: the local test's second game folder); false when cancelled.
    std::function<bool(const std::string& utf8_title, std::string& utf8_folder_out)> pick_folder;
    std::function<void(const std::string& utf8_url)> open_url;
};

struct LogoTexture {
    ImTextureID id = 0;
    float width = 0.0f;   // pixels of the source image
    float height = 0.0f;
};

class LauncherUi {
public:
    LauncherUi(LauncherModel& model, ShellServices shell, LogoTexture logo);

    // Builds one frame (call between ImGui::NewFrame and ImGui::Render).
    // `time` is a monotonic clock in seconds (spinners, "Copied" feedback).
    void Frame(double time);
    // Something on screen is time-driven right now (spinner, progress, feedback).
    bool Animating() const { return animating_; }
    Phase LastPhase() const { return last_phase_; }

    // ---- Screenshot driver (--screenshot-all) ------------------------------
    static int SceneCount();
    static const char* SceneName(int index);
    // Resets the pages and puts `mock` and the pages into scene `index`.
    void PrepareScene(int index, MockModel& mock);

private:
    struct Result {
        bool ok = false;
        std::string text;
        std::string path;   // a file to reveal ("Show")
        bool shown = false;
    };
    enum class Popup { none, uninstall_confirm, uninstall_report, restore_confirm, about };

    void Sidebar(Phase phase);
    void PageHeader(const char* title, const char* subtitle);
    void SetupStatus();
    void WrongFolderPage();
    void PlayPage(Phase phase);
    void HostTab(Phase phase);
    void JoinTab(Phase phase);
    void LocalTab(Phase phase, const LocalTestView& local);
    void CodeCard(bool compact);
    void InGamePage(Phase phase);
    void ToolsPage(Phase phase);
    void SettingsPage(Phase phase);
    void ResultLine(Result& result);
    void Popups();
    void CopyCode();
    void Navigate(ui::Page page);

    LauncherModel& model_;
    ShellServices shell_;
    LogoTexture logo_;
    double time_ = 0.0;
    bool animating_ = false;
    bool first_frame_ = true;

    Phase last_phase_ = Phase::needs_setup;
    ui::Page page_ = ui::Page::play;
    ui::SetupView setup_view_ = ui::SetupView::none;   // this frame's automatic setup view
    ui::PlayTab play_tab_ = ui::PlayTab::host;   // the chosen Play tab
    Role session_role_ = Role::host;

    // Play
    char code_buf_[160] = {};
    std::string checked_text_;
    CodeCheck checked_;
    bool wait_invite_ = false;
    std::string hosted_code_;
    std::string play_error_;
    double copied_until_ = -1.0;

    LinkView link_;   // this frame's link status (sessions only)

    // Tools
    Result tool_result_;   // the last Tools action
    Result logs_result_;   // Collect logs from the setup failure banner / error card
    std::string uninstall_report_;
    Popup popup_request_ = Popup::none;
    bool scroll_settings_down_ = false;   // the screenshot driver shows the Debug section
};

}  // namespace ddt
