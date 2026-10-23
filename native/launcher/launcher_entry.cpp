// Diamond Dogs Together: --watch (stub) and --selftest (see launcher_entry.hpp).
#include "launcher_entry.hpp"

#include "launcher_join_code.hpp"
#include "launcher_model_mock.hpp"
#include "launcher_ui_logic.hpp"

namespace ddt {

int RunWatch(const std::wstring& session) {
    // C0 stub: the real watchdog (B) polls the session, cleans up after the
    // game exits and runs the save handoff. Nothing to do without the core.
    (void)session;
    return 0;
}

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
    return failures == 0 ? 0 : 1;
}

}  // namespace ddt
