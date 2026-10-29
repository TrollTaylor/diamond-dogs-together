// Diamond Dogs Together: --watch and --selftest (see launcher_entry.hpp).
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "launcher_entry.hpp"

#include "core/core_model.hpp"
#include "core/core_update.hpp"
#include "core/core_util.hpp"
#include "launcher_join_code.hpp"
#include "launcher_model_mock.hpp"
#include "launcher_ui_logic.hpp"

namespace ddt {

namespace {
std::wstring ExeFolder() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n == 0) return {};
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}
}  // namespace

int RunWatch(const std::wstring& session) {
    // The hidden helper (wp/launcher-core): follows the game started from the
    // exe's own folder, applies a save handoff once the window is gone and
    // switches co-op off after the game exits.
    return core::RunWatch(ExeFolder(), core::Utf8(session));
}

namespace {
std::wstring SessionFolder(const std::wstring& game_dir) {
    if (game_dir.empty()) return ExeFolder();
    std::wstring full(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetFullPathNameW(game_dir.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
        if (n == 0) return game_dir;
        if (n < full.size()) {
            full.resize(n);
            break;
        }
        full.resize(n + 1);
    }
    while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
    return full;
}
}  // namespace

int RunWriteSession(const std::wstring& game_dir, bool host, const std::wstring& code, const std::wstring& character, std::string& report) {
    core::WriteSessionRequest request;
    request.host = host;
    request.code = core::Utf8(code);
    request.character = core::Utf8(character);
    return core::RunWriteSession(SessionFolder(game_dir), request, report);
}

int RunEndSession(const std::wstring& game_dir, std::string& report) { return core::RunEndSession(SessionFolder(game_dir), report); }

bool TakeOverAfterUpdate(std::uint32_t pid, const std::function<bool(int)>& claim) {
    core::PreviousLauncherOps ops = core::PreviousLauncherWin32(pid);
    ops.claim = claim;
    return core::TakeOverFromPreviousLauncher(ops, core::kPreviousLauncherWaitMs) != core::TakeOver::not_claimed;
}

bool OldLauncherFromRecentUpdate(const std::wstring& game_dir) { return core::OldExecutableFromRecentUpdate(game_dir); }

int RunSelfTest(std::string& report) {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* what) {
        ++checks;
        if (!ok) {
            ++failures;
            report += "FAIL: ";
            report += what;
            report += '\n';
        }
    };

    MockModel m(MockOptions{});
    m.use_manual_clock(0.0);
    check(m.phase() == Phase::needs_setup, "mock starts in needs_setup");
    m.start_setup();
    check(m.phase() == Phase::setting_up, "setup starts");
    m.advance(5.0);
    m.tick();
    const ui::SetupSummary mid = ui::SummarizeSetup(m.setup_steps());
    check(mid.running && mid.done >= 2 && !mid.failed, "setup progresses");
    m.advance(10.0);
    m.tick();
    check(m.phase() == Phase::ready, "setup finishes");

    std::string code, error;
    check(m.host(code, error) && m.phase() == Phase::launching, "host launches");
    check(CheckJoinCode(code, 0).ok, "the host code is a valid join code");
    check(!m.check_code(code).ok, "the player's own code is refused");
    m.advance(5.0 + 16.0);
    m.tick();
    const LinkView handoff = m.link();
    check(m.phase() == Phase::in_game && handoff.connected && handoff.handoff_active, "a session with a handoff");
    m.advance(100.0);
    m.tick();
    check(m.phase() == Phase::ready, "the session ends");

    check(!m.join("MGS2-NOT-A-CODE", error) && !error.empty(), "a bad code is refused");
    check(m.join(MockModel::PartnerCode(), error) && m.phase() == Phase::launching, "join with a code");
    check(m.cancel_launch(error) && m.phase() == Phase::ready, "cancel while launching");

    std::string path;
    check(m.export_save(path, error) && !path.empty(), "export");
    check(m.collect_logs(path, error) && !path.empty(), "collect logs");
    check(m.restore_save(error), "restore");
    std::string uninstall;
    check(m.uninstall(uninstall, error) && m.phase() == Phase::needs_setup, "uninstall");

    report += "selftest: " + std::to_string(checks - failures) + "/" + std::to_string(checks) + " checks passed\n";
    // The real model's checks (wp/launcher-core): read-only for the game folder.
    const int core_result = core::RunSelfTest(ExeFolder(), report);
    return failures == 0 && core_result == 0 ? 0 : 1;
}

}  // namespace ddt
