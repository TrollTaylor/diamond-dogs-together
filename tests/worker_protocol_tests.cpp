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
    const auto probe_context = "MGSWORKERPROBE1 " + run + " probe 123 456 MGSWorker_" + run + " admitted\n";
    Check(Admission(probe_context, "MGSWORKERPROBE1", run, "probe", 123, 456, "MGSWorker_" + run), "live_probe_admission");
    const auto probe_ack = "MGSWORKERPROBEACK1 " + run + " probe 123 456 release\n";
    Check(ProbeRelease(probe_ack, run, 123, 456), "admitted_probe_release");
    Check(!ProbeRelease(probe_ack, other, 123, 456), "probe_release_run_splice");
    Check(!ProbeRelease(probe_ack, run, 124, 456), "probe_release_pid_reuse");
    Check(!ProbeRelease(probe_ack, run, 123, 457), "probe_release_creation_reuse");
    Check(!ProbeRelease(Replace(probe_ack, " probe ", " guest "), run, 123, 456), "probe_release_role_splice");
    Check(!ProbeRelease(Replace(probe_ack, " release", " admitted"), run, 123, 456), "probe_admission_not_release");
    Check(!ProbeRelease(Replace(probe_ack, " 123 ", " 0123 "), run, 123, 456), "probe_release_pid_alias");
    Check(!ProbeRelease(Replace(probe_ack, " 123 ", "  123 "), run, 123, 456), "probe_release_extra_spacing");
    Check(!ProbeRelease(probe_ack + "extra\n", run, 123, 456), "probe_release_trailing_content");
    Check(!ProbeRelease(std::string(257, 'x'), run, 123, 456), "probe_release_size_bound");
    Check(!ProbeRelease("MGSWORKERPROBEACK1 " + run + " probe 0 0 release\n", run, 0, 0), "probe_release_missing_lease");
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
    // The normal animated profile (Play-OfflineCoop default): xform, input off, dm=map.
    auto animated = lc; animated.partner_apply = mgscoop::PartnerApply::xform; animated.partner_input = mgscoop::PartnerInput::off;
    animated.partner_input_send = false; animated.partner_dm = mgscoop::PartnerDm::map;
    Check(OfflineLab(animated, "host", save), "animated_partner_profile");
    changed = animated; changed.partner_dm = mgscoop::PartnerDm::off; Check(!OfflineLab(changed, "host", save), "animated_profile_needs_dm_map");
    changed = animated; changed.partner_input = mgscoop::PartnerInput::net; Check(!OfflineLab(changed, "host", save), "animated_profile_without_input_window");
    changed = animated; changed.partner_apply = mgscoop::PartnerApply::warp; Check(!OfflineLab(changed, "host", save), "animated_profile_needs_xform");
    // Native puppet (docs/native-puppet.md 13.2): exactly the census, loco and gun stages.
    changed = animated; changed.partner_drive = mgscoop::PartnerDrive::native; Check(!OfflineLab(changed, "host", save), "native_drive_not_admitted_as_animated");
    changed = lc; changed.partner_input_defer = mgscoop::PartnerDefer::on; Check(!OfflineLab(changed, "host", save), "defer_not_admitted_as_input_profile");
    const std::string native_lab = lab + "partner_stage=3\npartner_apply=xform\npartner_input=off\npartner_dm=map\npartner_drive=native\n"
        "partner_input_hook=1\npartner_input_hold=own\npartner_input_words=hpr\npartner_input_wrap=window\npartner_input_cam=sandbox\n"
        "partner_input_local=main\npartner_recon=track\npartner_input_slot=body\n";
    mgscoop::LabConfig census{}, loco{}, gun{};
    const std::string census_text = native_lab + "partner_input_defer=census\npartner_input_mask=0x2240006\npartner_native_aim=off\n";
    const std::string loco_text = native_lab + "partner_input_defer=on\npartner_input_mask=0x2240006\npartner_native_aim=off\n"
        "partner_native_rates=mirror\n";
    // The stage settings of Get-WorkerPartnerSettings (WorkerSessionCommon.psm1), in its order.
    const std::string gun_text = native_lab + "partner_input_defer=on\npartner_input_mask=0x224005E\npartner_native_gun=1\n"
        "partner_native_fire=visual\npartner_native_aim=look\npartner_input_triggers=1\npartner_input_rstick=1\npartner_native_rates=mirror\n"
        "partner_native_aimpt=point\npartner_native_reload=event\npartner_native_slot=write\npartner_native_equip=substitute\n";
    Check(mgscoop::ParseLabConfig(census_text.data(), census_text.size(), census) && OfflineLab(census, "host", save) && NativeProfile(census),
          "native_census_stage");
    Check(mgscoop::ParseLabConfig(loco_text.data(), loco_text.size(), loco) && OfflineLab(loco, "host", save) && NativeProfile(loco),
          "native_loco_stage");
    Check(mgscoop::ParseLabConfig(gun_text.data(), gun_text.size(), gun) && OfflineLab(gun, "host", save) && NativeProfile(gun) &&
          gun.partner_native_fire == mgscoop::PartnerNativeFire::visual && gun.partner_input_mask == 0x224005Eu,
          "native_gun_stage (visual fire, EVADE and FIRE in the mask)");
    Check(census.partner_native_ride == mgscoop::PartnerNativeRide::off &&
          loco.partner_native_ride == mgscoop::PartnerNativeRide::hide && gun.partner_native_ride == mgscoop::PartnerNativeRide::hide,
          "worker native visibility defaults: census off; driven loco/gun hide");
    mgscoop::LabConfig rideOff{}; const std::string rideOffText = gun_text + "partner_native_ride=off\n";
    Check(mgscoop::ParseLabConfig(rideOffText.data(), rideOffText.size(), rideOff) && NativeProfile(rideOff) &&
          rideOff.partner_native_ride == mgscoop::PartnerNativeRide::off && rideOff.partner_native_ride_given,
          "worker explicit ride off is preserved without changing stage admission");
    Check(!InputProfile(loco) && !AnimatedProfile(loco), "native is not a legacy profile");
    changed = loco; changed.partner_input_defer = mgscoop::PartnerDefer::off; Check(!OfflineLab(changed, "host", save), "native_needs_defer");
    changed = loco; changed.partner_input_p218_allow = true; Check(!OfflineLab(changed, "host", save), "native_refuses_p218");
    changed = loco; changed.partner_input_mask = 0x2240007u; Check(!OfflineLab(changed, "host", save), "native_loco_exact_mask");
    changed = loco; changed.partner_input_mask = 0x2200006u; Check(!OfflineLab(changed, "host", save), "native_loco_mask_with_evade");
    changed = gun; changed.partner_input_mask = 0x220005Eu; Check(!OfflineLab(changed, "host", save), "native_gun_mask_with_evade");
    // Native weapons: the gun stage is visual fire with FIRE in the mask, exactly as the profile emits it.
    changed = gun; changed.partner_input_mask = 0x224004Eu; Check(!OfflineLab(changed, "host", save), "native_visual_fire_needs_the_fire_mask");
    changed = gun; changed.partner_native_fire = mgscoop::PartnerNativeFire::off;
    Check(!OfflineLab(changed, "host", save), "native_fire_mask_needs_visual_fire");
    changed.partner_input_mask = 0x224004Eu; Check(!OfflineLab(changed, "host", save), "native_gun_without_fire_needs_a_gate_change");
    changed = loco; changed.partner_native_fire = mgscoop::PartnerNativeFire::visual;
    Check(!OfflineLab(changed, "host", save), "native_loco_without_fire");
    changed = census; changed.partner_native_fire = mgscoop::PartnerNativeFire::visual;
    Check(!OfflineLab(changed, "host", save), "native_census_without_fire");
    // Native rates: census never mirrors, loco and gun always do.
    changed = census; changed.partner_native_rates = mgscoop::PartnerNativeRates::mirror;
    Check(!OfflineLab(changed, "host", save), "native_census_never_mirrors");
    changed = loco; changed.partner_native_rates = mgscoop::PartnerNativeRates::log; Check(!OfflineLab(changed, "host", save), "native_loco_mirrors_rates");
    changed = gun; changed.partner_native_rates = mgscoop::PartnerNativeRates::off; Check(!OfflineLab(changed, "host", save), "native_gun_mirrors_rates");
    changed = gun; changed.partner_native_aim = mgscoop::PartnerNativeAim::lookcam; Check(!OfflineLab(changed, "host", save), "native_lookcam_needs_a_gate_change");
    changed = gun; changed.partner_native_aim = mgscoop::PartnerNativeAim::off; Check(!OfflineLab(changed, "host", save), "native_gun_needs_look");
    // Native polish: the look source A/B run (looksrc) is a gun stage; the noise filter is not optional there.
    changed = gun; changed.partner_native_aim = mgscoop::PartnerNativeAim::looksrc; Check(OfflineLab(changed, "host", save), "native_gun_looksrc_admitted");
    changed = loco; changed.partner_native_aim = mgscoop::PartnerNativeAim::looksrc; Check(!OfflineLab(changed, "host", save), "native_loco_without_looksrc");
    changed = gun; changed.partner_native_noise = mgscoop::PartnerNativeNoise::keep; Check(!OfflineLab(changed, "host", save), "native_gun_keeps_the_noise_filter");
    changed = gun; changed.partner_native_face = mgscoop::PartnerNativeFace::aim; Check(OfflineLab(changed, "host", save), "native_face_is_free");
    changed = loco; changed.partner_native_face = mgscoop::PartnerNativeFace::off; Check(OfflineLab(changed, "host", save), "native_face_off_admitted");
    changed = census; changed.partner_native_aim = mgscoop::PartnerNativeAim::look; Check(!OfflineLab(changed, "host", save), "native_census_without_aim");
    // The aim-target mirror: the gun stage emits point; off and look are its A/B (-LabSetting); never outside the gun stage.
    Check(gun.partner_native_aimpt == mgscoop::PartnerNativeAimPt::point, "native_gun_stage_aimpt_point");
    changed = gun; changed.partner_native_aimpt = mgscoop::PartnerNativeAimPt::off; Check(OfflineLab(changed, "host", save), "native_gun_aimpt_off_admitted");
    changed = gun; changed.partner_native_aimpt = mgscoop::PartnerNativeAimPt::point; Check(OfflineLab(changed, "host", save), "native_gun_aimpt_point_admitted");
    changed = loco; changed.partner_native_aimpt = mgscoop::PartnerNativeAimPt::look; Check(!OfflineLab(changed, "host", save), "native_loco_without_aimpt");
    changed = census; changed.partner_native_aimpt = mgscoop::PartnerNativeAimPt::look; Check(!OfflineLab(changed, "host", save), "native_census_without_aimpt");
    changed = loco; changed.partner_native_gun = true; Check(!OfflineLab(changed, "host", save), "native_loco_without_gun");
    changed = loco; changed.partner_input_local = mgscoop::PartnerLocal::all; Check(!OfflineLab(changed, "host", save), "native_local_all_needs_a_gate_change");
    changed = loco; changed.partner_input_hook = false; Check(!OfflineLab(changed, "host", save), "native_needs_the_hook");
    changed = loco; changed.partner_input_wrap = mgscoop::PartnerWrap::census; Check(!OfflineLab(changed, "host", save), "native_needs_wrap_window");
    changed = loco; changed.partner_dm = mgscoop::PartnerDm::off; Check(!OfflineLab(changed, "host", save), "native_needs_dm_map_fallback");
    changed = loco; changed.partner_input_send = false; Check(!OfflineLab(changed, "host", save), "native_needs_the_sender");
    changed = loco; changed.partner_input_slot = mgscoop::PartnerSlot::remote; Check(!OfflineLab(changed, "host", save), "native_slot_body");
    // The partner's own reload and the slot follower: free in the gun stage (the A/B variants), defaults elsewhere.
    Check(gun.partner_native_reload == mgscoop::PartnerNativeReload::event && gun.partner_native_slot == mgscoop::PartnerNativeSlot::write,
          "native_gun_stage_reload_event_slot_write");
    for (const auto r : {mgscoop::PartnerNativeReload::mirror, mgscoop::PartnerNativeReload::raw, mgscoop::PartnerNativeReload::event})
        for (const auto sl : {mgscoop::PartnerNativeSlot::off, mgscoop::PartnerNativeSlot::write, mgscoop::PartnerNativeSlot::call}) {
            changed = gun; changed.partner_native_reload = r; changed.partner_native_slot = sl;
            Check(OfflineLab(changed, "host", save), "native_gun_reload_slot_variants_admitted");
        }
    changed = loco; changed.partner_native_reload = mgscoop::PartnerNativeReload::event; Check(!OfflineLab(changed, "host", save), "native_loco_reload_default");
    changed = loco; changed.partner_native_slot = mgscoop::PartnerNativeSlot::write; Check(!OfflineLab(changed, "host", save), "native_loco_slot_default");
    changed = census; changed.partner_native_slot = mgscoop::PartnerNativeSlot::call; Check(!OfflineLab(changed, "host", save), "native_census_slot_default");
    // The generalized writer: free in the gun stage, stock46 elsewhere.
    Check(gun.partner_native_equip == mgscoop::PartnerNativeEquip::substitute, "native_gun_stage_equip_substitute");
    for (const auto eq : {mgscoop::PartnerNativeEquip::stock46, mgscoop::PartnerNativeEquip::manifest, mgscoop::PartnerNativeEquip::substitute}) {
        changed = gun; changed.partner_native_equip = eq;
        Check(OfflineLab(changed, "host", save), "native_gun_equip_variants_admitted");
    }
    changed = loco; changed.partner_native_equip = mgscoop::PartnerNativeEquip::manifest; Check(!OfflineLab(changed, "host", save), "native_loco_equip_default");
    // The play rehearsal (docs/two-pc-play.md "Hidden play rehearsal"): Play-Coop's
    // play lab.ini plus the worker-only rehearsal keys, parsed with the worker option.
    const std::string play_clips = "# Generated by mgs-coop Play-Coop.ps1 (two-PC play); removed when the game exits.\nenabled=1\nmode=play\n"
        "role=host\nsave_dir=" + save + "\nnet=1\nnet_bind=any\nnet_listen_port=47830\npartner_interp=auto\npartner=1\npartner_stage=3\n"
        "partner_source=net\npartner_apply=xform\npartner_anim=off\npartner_aset=qs\npartner_reset=n\npartner_look=net\npartner_dm=map\n"
        "partner_input_send=1\nworld=3\n";
    const std::string rehearse = "play_rehearsal=1\nnet_bind=loopback\nnet_peer_port=47831\nmutex_suffix=worker_host_0123abcd\nxinput=none\n";
    mgscoop::LabParseOptions worker{}; worker.allow_play_rehearsal = true;
    mgscoop::LabConfig play_gun{}, play_loco{}, play_clip{}, friend_view{};
    const std::string gun_play = play_clips + "partner_native_profile=gun\n" + rehearse;
    Check(mgscoop::ParseLabConfig(gun_play.data(), gun_play.size(), play_gun, worker) && PlayRehearsal(play_gun, "host", save) &&
          NativeProfile(play_gun) && !OfflineLab(play_gun, "host", save), "play_rehearsal_gun_profile");
    const std::string loco_play = play_clips + "partner_native_profile=loco\n" + rehearse;
    Check(mgscoop::ParseLabConfig(loco_play.data(), loco_play.size(), play_loco, worker) && PlayRehearsal(play_loco, "host", save),
          "play_rehearsal_loco_profile");
    // The play gun profile runs the partner's own reload, the slot follower and the generalized writer (ResolveWeaponDefaults).
    Check(play_gun.partner_native_reload == mgscoop::PartnerNativeReload::event && play_gun.partner_native_slot == mgscoop::PartnerNativeSlot::write &&
              play_gun.partner_native_equip == mgscoop::PartnerNativeEquip::substitute && play_loco.partner_native_reload == mgscoop::PartnerNativeReload::mirror &&
              play_loco.partner_native_slot == mgscoop::PartnerNativeSlot::off && play_loco.partner_native_equip == mgscoop::PartnerNativeEquip::stock46,
          "play_rehearsal_gun_profile_weapons_defaults");
    const std::string clip_play = play_clips + rehearse;
    Check(mgscoop::ParseLabConfig(clip_play.data(), clip_play.size(), play_clip, worker) && PlayRehearsal(play_clip, "host", save) &&
          AnimatedProfile(play_clip), "play_rehearsal_clip_partner");
    Check(mgscoop::ParseLabConfig(gun_play.data(), gun_play.size(), friend_view) && !PlayRehearsal(friend_view, "host", save) &&
          friend_view.play_refused != 0, "play_rehearsal_needs_the_worker_parser");
    Check(!PlayRehearsal(play_gun, "guest", save) && !PlayRehearsal(play_gun, "host", save + "-other"), "play_rehearsal_role_and_save");
    changed = play_gun; changed.mode = mgscoop::LabMode::lab; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_needs_play");
    changed = play_gun; changed.play_rehearsal = false; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_flag");
    changed = play_gun; changed.play_refused = mgscoop::kPlayRefusedImpairment; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_no_refusal");
    changed = play_gun; changed.loopback = true; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_no_loopback_emulator");
    changed = play_gun; changed.self_steamid = 76561202255233008ull; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_real_identity");
    changed = play_gun; changed.xinput = 0; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_no_physical_pad");
    changed = play_gun; changed.mutex_suffix[0] = 0; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_second_instance_mutex");
    changed = play_gun; changed.net_bind_loopback = false; changed.net_bind_address = 0; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_loopback_bind");
    changed = play_gun; changed.net_peer_address = 0xc0a80101; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_loopback_peer");
    changed = play_gun; changed.net_transport = mgscoop::NetTransport::steam; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_udp_only");
    changed = play_gun; changed.partner_native_aim = mgscoop::PartnerNativeAim::lookcam; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_admitted_stage_only");
    changed = play_clip; changed.partner_drive = mgscoop::PartnerDrive::native; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_native_only_by_profile");
    changed = play_gun; changed.invalid_values = 1; Check(!PlayRehearsal(changed, "host", save), "play_rehearsal_invalid");
    // The manifest's lab_mode.
    Manifest gm; std::string why;
    Check(ParseManifest(Game(), gm, why) && gm.lab_mode == "lab", "manifest_lab_mode_default_lab");
    Check(ParseManifest(Game() + "lab_mode=play\n", gm, why) && gm.lab_mode == "play", "manifest_lab_mode_play");
    Check(!Parsed(Game() + "lab_mode=online\n"), "manifest_lab_mode_values");
    Check(!Parsed(Probe() + "lab_mode=play\n"), "manifest_lab_mode_mgsv_only");
    std::cout << "worker_protocol_tests: " << checks << " checks passed; OS calls=0, processes=0, windows=0\n";
    return 0;
}
