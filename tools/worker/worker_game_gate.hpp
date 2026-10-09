#pragma once
#include "worker_protocol.hpp"
#include "mgscoop/lab_config.hpp"

namespace mgsworker {
// Pure admission subset. OS path/handle/actual redirect/mount identity remains
// mandatory in supervisor and in-process bridge, separately from declarations.
// Three admitted partner profiles: the round-4 input drive, the normal animated
// partner of Play-OfflineCoop (xform movement, no input window, dm=map clips),
// and the native puppet (docs/native-puppet.md 13.2; dm=map + xform as its
// fallback, the native drive armed by the arbiter) in exactly three stages.
// The gun stage admits only visual fire (docs/native-puppet.md "Weapons": FIRE
// in the mask only together with partner_native_fire=visual, whose owner
// filter keeps the partner's shots visual, and the shot noise filter:
// partner_native_noise=filter), as the worker profile emits it, with the
// look written after phase 0 (look) or also at its source (looksrc, the
// facing A/B run: -LabSetting partner_native_aim=looksrc). Later stages
// (lookcam, local=all, a gun stage without fire, noise=keep) need an explicit
// gate change. partner_native_face (the facing governor) is free; the
// aim-target mirror partner_native_aimpt (off|look|point; the gun stage emits
// look, -LabSetting selects the A/B) only in the gun stage (off elsewhere).
inline bool LegacyDrive(const mgscoop::LabConfig& lab) {
    return lab.partner_drive == mgscoop::PartnerDrive::legacy && lab.partner_input_defer == mgscoop::PartnerDefer::off;
}
inline bool InputProfile(const mgscoop::LabConfig& lab) {
    return LegacyDrive(lab) && lab.partner_apply == mgscoop::PartnerApply::input && lab.partner_input == mgscoop::PartnerInput::net &&
        lab.partner_input_send;
}
inline bool AnimatedProfile(const mgscoop::LabConfig& lab) {
    return LegacyDrive(lab) && lab.partner_apply == mgscoop::PartnerApply::xform && lab.partner_input == mgscoop::PartnerInput::off &&
        lab.partner_dm == mgscoop::PartnerDm::map;
}
inline bool NativeProfile(const mgscoop::LabConfig& lab) {
    const bool base = lab.partner_apply == mgscoop::PartnerApply::xform && lab.partner_input == mgscoop::PartnerInput::off &&
        lab.partner_dm == mgscoop::PartnerDm::map && lab.partner_drive == mgscoop::PartnerDrive::native &&
        lab.partner_input_send && lab.partner_input_hook && !lab.partner_input_p218_allow &&
        lab.partner_input_hold == mgscoop::PartnerInputHold::own && lab.partner_input_wrap == mgscoop::PartnerWrap::window &&
        lab.partner_input_cam == mgscoop::PartnerInputCam::sandbox && lab.partner_input_local == mgscoop::PartnerLocal::main &&
        lab.partner_input_slot == mgscoop::PartnerSlot::body;
    // Exactly the three stages Get-WorkerPartnerSettings emits
    // (tools/worker/WorkerSessionCommon.psm1): the movement base 0x2240006
    // (STANCE, DASH, EVADE, keyboard dash and walk) in census and loco, plus
    // HOLD + RELOAD + FIRE in the gun stage (0x224005E) with visual fire only;
    // census never writes rates, loco and gun mirror the remote's movement
    // rates into instance 1.
    const bool no_fire = lab.partner_native_fire == mgscoop::PartnerNativeFire::off;
    const bool no_aimpt = lab.partner_native_aimpt == mgscoop::PartnerNativeAimPt::off;
    const bool census = lab.partner_input_defer == mgscoop::PartnerDefer::census && lab.partner_input_mask == 0x2240006u &&
        !lab.partner_native_gun && no_fire && lab.partner_native_aim == mgscoop::PartnerNativeAim::off &&
        lab.partner_native_rates != mgscoop::PartnerNativeRates::mirror && no_aimpt;
    const bool loco = lab.partner_input_defer == mgscoop::PartnerDefer::on && lab.partner_input_mask == 0x2240006u &&
        !lab.partner_native_gun && no_fire && lab.partner_native_aim == mgscoop::PartnerNativeAim::off &&
        lab.partner_native_rates == mgscoop::PartnerNativeRates::mirror && no_aimpt;
    const bool gun = lab.partner_input_defer == mgscoop::PartnerDefer::on && lab.partner_input_mask == 0x224005Eu &&
        lab.partner_native_gun && lab.partner_native_fire == mgscoop::PartnerNativeFire::visual &&
        (lab.partner_native_aim == mgscoop::PartnerNativeAim::look || lab.partner_native_aim == mgscoop::PartnerNativeAim::looksrc) &&
        lab.partner_native_noise == mgscoop::PartnerNativeNoise::filter && lab.partner_native_rates == mgscoop::PartnerNativeRates::mirror;
    return base && (census || loco || gun);
}
inline bool OfflineLab(const mgscoop::LabConfig& lab, const std::string& role, const std::string& save) {
    const auto expected = role == "host" ? mgscoop::LabRole::host : mgscoop::LabRole::guest;
    return (role == "host" || role == "guest") && lab.present && lab.role == expected && lab.mode == mgscoop::LabMode::lab &&
        lab.loopback && lab.xinput == mgscoop::kXInputNone && lab.invalid_values == 0 && lab.mutex_suffix[0] &&
        AbsolutePath(lab.save_dir) && EqualFold(lab.save_dir, save) && lab.partner && lab.net && lab.net_bind_loopback &&
        lab.net_bind_address == 0x7f000001 && lab.net_peer_address == 0x7f000001 &&
        lab.partner_source == mgscoop::PartnerSource::net && (InputProfile(lab) || AnimatedProfile(lab) || NativeProfile(lab));
}
// The one-PC play rehearsal (docs/two-pc-play.md "Hidden play rehearsal"):
// both copies run mode=play exactly as Play-Coop writes it, parsed with the
// worker build's LabParseOptions, which relax only the loopback bind and
// 127/8 peer, the mutex suffix, xinput=none and the impairment keys
// (play_rehearsal=1). Nothing refused or invalid; the real identity (no
// loopback emulator, no fake or pinned SteamID, no persona override, no join
// trigger); our link on UDP 127.0.0.1; the copied save; and either the clip
// partner (no profile) or partner_native_profile, which expanded into exactly
// an admitted native stage (NativeProfile).
inline bool PlayRehearsal(const mgscoop::LabConfig& lab, const std::string& role, const std::string& save) {
    const auto expected = role == "host" ? mgscoop::LabRole::host : mgscoop::LabRole::guest;
    const bool identity = !lab.loopback && lab.self_steamid == 0 && lab.peer_steamid == 0 && !lab.self_name[0] && !lab.join_trigger &&
        lab.auto_join_delay_s == 0;
    const bool link = lab.net && lab.net_transport == mgscoop::NetTransport::udp && lab.net_link == mgscoop::NetLink::v2 &&
        lab.net_bind_loopback && lab.net_bind_address == 0x7f000001 && lab.net_peer_address == 0x7f000001 && lab.net_listen_port >= 1024 &&
        lab.net_peer_port >= 1024 && lab.net_listen_port != lab.net_peer_port;
    const bool partner = lab.partner && lab.partner_source == mgscoop::PartnerSource::net &&
        (lab.partner_native_profile == mgscoop::PartnerNativeProfile::off ? AnimatedProfile(lab) : NativeProfile(lab));
    return (role == "host" || role == "guest") && lab.present && lab.role == expected && lab.mode == mgscoop::LabMode::play &&
        lab.play_rehearsal && lab.play_refused == 0 && lab.invalid_values == 0 && lab.xinput == mgscoop::kXInputNone && lab.mutex_suffix[0] &&
        identity && link && AbsolutePath(lab.save_dir) && EqualFold(lab.save_dir, save) && partner &&
        lab.world_probe == mgscoop::WorldProbe::none && !lab.debug_soldier_warp && lab.steam_selftest == 0;
}
} // namespace mgsworker
