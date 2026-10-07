// Pure worker admission/protocol checks: no Win32, windows, processes or games.
#include "../tools/worker/worker_game_gate.hpp"
#include <cstdlib>
#include <iostream>

namespace {
using namespace mgsworker;
unsigned checks = 0;
void Check(bool ok, const char* name) { ++checks; if (!ok) { std::cerr << "FAIL " << name << '\n'; std::exit(1); } }
const std::string run = "0123456789abcdef0123456789abcdef";
const std::string other = "abcdef0123456789abcdef0123456789";
const std::string hash(64, 'A');
std::string Common(const char* mode) {
    return "schema=1\nmode=" + std::string(mode) + "\nrun_id=" + run + "\nrun_dir=C:\\worker\\runs\\" + run +
           "\ndesktop=MGSWorker_" + run + "\ntimeout_ms=30000\nready_timeout_ms=10000\n";
}
std::string Probe() { return Common("probe") + "probe_exe=C:\\worker\\bin\\worker-probe.exe\nprobe_sha256=" + hash + "\n"; }
std::string Game() {
    const std::string dir = "C:\\worker\\runs\\" + run;
    return Common("mgsv") + "exe_sha256=" + kRetailExeSha + "\nsteam_sha256=" + kRetailSteamSha + "\nloader_sha256=" + hash +
        "\nruntime_archive_sha256=" + kBaselineArchiveSha + "\nruntime_motion_count=21" +
        "\nprobe_receipt=C:\\worker\\runs\\" + other + "\\qualification.txt\nprobe_receipt_sha256=" + hash +
        "\noffline_master_port=47899\nhost_exe=D:\\copies\\host\\mgsvtpp.exe\nguest_exe=D:\\copies\\guest\\mgsvtpp.exe\n" +
        "host_save_dir=" + dir + "\\nested\\host-saves\nguest_save_dir=" + dir + "\\nested\\guest-saves\n" +
        "host_lab_sha256=" + hash + "\nguest_lab_sha256=" + hash + "\nhost_coop_sha256=" + hash + "\nguest_coop_sha256=" + hash +
        "\nhost_graphics_sha256=" + hash + "\nguest_graphics_sha256=" + hash + "\n";
}
std::string Replace(std::string s, const std::string& from, const std::string& to) { const auto i = s.find(from); Check(i != std::string::npos, "test_replace_source"); s.replace(i, from.size(), to); return s; }
bool Parsed(const std::string& text) { Manifest m; std::string why; return ParseManifest(text, m, why); }
}
int main() {
    using namespace mgsworker;
    Check(Parsed(Probe()), "valid_probe_manifest"); Check(Parsed(Game()), "valid_nested_copied_save_manifest");
    Check(!Parsed(Probe() + "timeout_ms=30000\n"), "duplicate_timeout");
    Check(!Parsed(Probe() + "shell_command=calc.exe\n"), "no_arbitrary_command");
    Check(!Parsed(Replace(Probe(), "MGSWorker_" + run, "Default")), "default_desktop_refused");
    Check(!Parsed(Replace(Probe(), "MGSWorker_" + run, "WinSta0\\Default")), "desktop_path_injection");
    Check(!Parsed(Replace(Probe(), "run_id=" + run, "run_id=abc")), "short_run_id");
    Check(!Parsed(Replace(Probe(), "timeout_ms=30000", "timeout_ms=600001")), "timeout_bounded");
    Check(!Parsed(Replace(Probe(), "ready_timeout_ms=10000", "ready_timeout_ms=30001")), "readiness_cannot_exceed_lifetime");
    Check(!Parsed(Replace(Probe(), "probe_sha256=" + hash, "probe_sha256=bad")), "probe_pin_required");
    Check(!Parsed(Replace(Game(), kRetailExeSha, hash)), "wrong_retail_exe");
    Check(!Parsed(Replace(Game(), kRetailSteamSha, hash)), "wrong_retail_steam");
    Check(!Parsed(Replace(Game(), kBaselineArchiveSha, hash)), "wrong_runtime_archive");
    Check(!Parsed(Replace(Game(), "runtime_motion_count=21", "runtime_motion_count=31")), "research_catalog_not_baseline");
    Check(!Parsed(Replace(Game(), "guest\\mgsvtpp.exe", "host\\mgsvtpp.exe")), "same_game_roots");
    Check(!Parsed(Replace(Game(), "\\nested\\guest-saves", "\\nested\\host-saves")), "same_save_roots");
    Check(!Parsed(Replace(Game(), "\\nested\\guest-saves", "\\nested\\host-saves\\child")), "save_descendant_alias");
    Check(!Parsed(Replace(Game(), "host_save_dir=C:\\worker\\runs\\" + run + "\\nested\\host-saves", "host_save_dir=C:\\Steam\\userdata\\123")), "real_userdata_saves_refused");
    Check(!Parsed(Replace(Game(), "\\" + other + "\\qualification.txt", "\\" + run + "\\qualification.txt")), "same_run_qualification_refused");
    Check(!Parsed(Replace(Game(), "offline_master_port=47899", "offline_master_port=80")), "privileged_offline_port");
    Check(!Parsed(Replace(Game(), "mgsvtpp.exe", "other.exe")), "game_basename_pin");
    Check(!Parsed(Probe().substr(0, Probe().size() - 1)), "manifest_final_newline_required");
    Check(!Parsed("\xef\xbb\xbf" + Probe()), "manifest_bom_refused");
    Check(!Parsed(std::string(kMaxManifest + 1, 'x')), "manifest_capacity");
    Check(AbsolutePath("C:\\safe folder\\run"), "spaces_in_absolute_path");
    Check(!AbsolutePath("C:\\safe\\..\\outside"), "dotdot_refused");
    Check(!AbsolutePath("C:\\safe\\.\\outside"), "dot_refused");
    Check(!AbsolutePath("C:\\safe\\name:stream"), "ads_refused");
    Check(!AbsolutePath("\\\\server\\share\\game"), "unc_refused");
    Check(!AbsolutePath("\\\\?\\C:\\safe"), "device_path_refused");
    Check(!AbsolutePath("C:\\safe.\\run"), "trailing_dot_alias");
    Check(!AbsolutePath("C:\\safe \\run"), "trailing_space_alias");
    Check(!AbsolutePath("C:\\safe\\\\run"), "empty_component");
    Check(!AbsolutePath("C:/safe/run"), "canonical_separator_required");
    Check(!AbsolutePath("C:safe\\run"), "drive_relative_refused");
    Check(!Inside("C:\\runs-other\\x", "C:\\runs"), "sibling_prefix_not_descendant");
    Check(Inside("C:\\RUNS\\x", "c:\\runs"), "case_insensitive_descendant");
    Check(Utf8("C:\\caf\xc3\xa9\\run"), "unicode_utf8_path");
    Check(!Utf8("\xc0\x80"), "overlong_utf8"); Check(!Utf8("\xed\xa0\x80"), "surrogate_utf8"); Check(!Utf8("\xf4\x90\x80\x80"), "utf8_codepoint_limit");
    std::uint64_t n = 17;
    Check(Decimal("18446744073709551615", UINT64_MAX, n) && n == UINT64_MAX, "u64_max");
    Check(!Decimal("18446744073709551616", UINT64_MAX, n), "u64_overflow"); Check(!Decimal("00", UINT64_MAX, n), "no_leading_zero");
    Check(!Decimal("+1", UINT64_MAX, n), "no_plus"); Check(!Decimal("-1", UINT64_MAX, n), "no_negative");
    Control control;
    const auto cmd = "MGSWORKERCTL1 " + run + " 1 shutdown\n";
    Check(ParseControl(cmd, run, 0, control) && control.sequence == 1, "owned_shutdown");
    Check(!ParseControl(cmd, other, 0, control), "foreign_run_control"); Check(!ParseControl(cmd, run, 1, control), "control_replay");
    Check(!ParseControl("MGSWORKERCTL1 " + run + " 0 shutdown\n", run, 0, control), "zero_control_sequence");
    Check(!ParseControl("MGSWORKERCTL1 " + run + " 2 launch\n", run, 0, control), "no_exec_control");
    Check(!ParseControl(cmd + "extra\n", run, 0, control), "extra_control_line");
    Check(!ParseControl(Replace(cmd, " 1 ", "  1 "), run, 0, control), "noncanonical_spacing");
    const auto context = "MGSWORKERCTX1 " + run + " host 123 456 MGSWorker_" + run + " admitted\n";
    Check(Admission(context, "MGSWORKERCTX1", run, "host", 123, 456, "MGSWorker_" + run), "actual_context_identity");
    Check(!Admission(context, "MGSWORKERCTX1", run, "guest", 123, 456, "MGSWorker_" + run), "context_role_splice");
    Check(!Admission(context, "MGSWORKERCTX1", run, "host", 124, 456, "MGSWorker_" + run), "context_pid_splice");
    Check(!Admission(context, "MGSWORKERCTX1", run, "host", 123, 457, "MGSWorker_" + run), "context_creation_splice");
    Check(!Admission(context, "MGSWORKERCTX1", run, "host", 123, 456, "Default"), "context_desktop_splice");
    Check(!Admission(Replace(context, " admitted", " declared"), "MGSWORKERCTX1", run, "host", 123, 456, "MGSWorker_" + run), "env_declaration_not_admission");
    Qualification q;
    const auto seal = "MGSWORKERQUAL1 " + other + " " + hash + " " + hash + " " + hash + " 1000 qualified\n";
    Check(ParseQualification(seal, hash, 1000, q), "matching_probe_qualification");
    Check(ParseQualification(seal, hash, 1000 + kQualificationAgeMs, q), "qualification_age_boundary");
    Check(!ParseQualification(seal, hash, 1001 + kQualificationAgeMs, q), "expired_qualification");
    Check(!ParseQualification(seal, hash, 999, q), "future_tick_or_previous_boot");
    Check(!ParseQualification(seal, std::string(64, 'B'), 1000, q), "changed_supervisor_binary");
    Check(!ParseQualification(Replace(seal, " qualified", " skipped"), hash, 1000, q), "unqualified_probe");
    Check(Json("C:\\run\n\"") == "\"C:\\\\run\\u000a\\\"\"", "receipt_json_escaping");
    const std::string save = "C:\\worker\\runs\\" + run + "\\host-saves";
    const std::string lab = "enabled=1\nrole=host\nloopback=1\nxinput=none\nmutex_suffix=worker_host\nsave_dir=" + save +
        "\nlisten_port=47810\npeer_port=47811\npartner=1\nnet=1\nnet_listen_port=47820\nnet_peer_port=47821\n" +
        "partner_source=net\npartner_apply=input\npartner_input=net\npartner_input_send=1\nnet_bind=loopback\nnet_peer_addr=127.0.0.1\n";
    mgscoop::LabConfig lc{};
    Check(mgscoop::ParseLabConfig(lab.data(), lab.size(), lc) && OfflineLab(lc, "host", save), "actual_lab_parser_valid_worker_subset");
    Check(!OfflineLab(lc, "guest", save), "wrong_lab_role");
    Check(!OfflineLab(lc, "host", save + "-other"), "wrong_copy_save");
    auto changed = lc; changed.xinput = mgscoop::kXInputPassthrough; Check(!OfflineLab(changed, "host", save), "physical_controller_refused");
    changed = lc; changed.mode = mgscoop::LabMode::play; Check(!OfflineLab(changed, "host", save), "online_mode_refused");
    changed = lc; changed.partner = false; Check(!OfflineLab(changed, "host", save), "missing_partner");
    changed = lc; changed.net = false; Check(!OfflineLab(changed, "host", save), "missing_partner_network");
    changed = lc; changed.net_bind_loopback = false; Check(!OfflineLab(changed, "host", save), "nonloopback_bind");
    changed = lc; changed.net_peer_address = 0xc0a80101; Check(!OfflineLab(changed, "host", save), "external_peer");
    changed = lc; changed.partner_source = mgscoop::PartnerSource::fixed; Check(!OfflineLab(changed, "host", save), "fixed_partner_source");
    changed = lc; changed.partner_apply = mgscoop::PartnerApply::warp; Check(!OfflineLab(changed, "host", save), "unreviewed_partner_apply");
    changed = lc; changed.partner_input = mgscoop::PartnerInput::off; Check(!OfflineLab(changed, "host", save), "missing_virtual_partner_input");
    changed = lc; changed.partner_input_send = false; Check(!OfflineLab(changed, "host", save), "missing_local_input_transport");
    changed = lc; changed.invalid_values = 1; Check(!OfflineLab(changed, "host", save), "invalid_lab_config");
    std::cout << "worker_protocol_tests: " << checks << " checks passed; OS calls=0, processes=0, windows=0\n";
    return 0;
}
