// Original MGS Co-op configuration parsers and once-only loaders.
#include "mgscoop/lab_config.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace mgscoop {
namespace {
constexpr std::size_t kMaxText = 64 * 1024;

struct Slice {
    const char* data;
    std::size_t size;
};

bool IsBlank(char c) noexcept { return c == ' ' || c == '\t' || c == '\r'; }

Slice Trim(Slice s) noexcept {
    while (s.size && IsBlank(s.data[0])) { ++s.data; --s.size; }
    while (s.size && IsBlank(s.data[s.size - 1])) --s.size;
    return s;
}

bool Equals(Slice s, const char* literal) noexcept {
    std::size_t i = 0;
    for (; literal[i]; ++i) if (i >= s.size || s.data[i] != literal[i]) return false;
    return i == s.size;
}

bool ParseU64(Slice s, std::uint64_t& out) noexcept {
    if (!s.size || s.size > 20) return false;
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < s.size; ++i) {
        const char c = s.data[i];
        if (c < '0' || c > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

bool ParseU32(Slice s, std::uint32_t max, std::uint32_t& out) noexcept {
    std::uint64_t value = 0;
    if (!ParseU64(s, value) || value > max) return false;
    out = static_cast<std::uint32_t>(value);
    return true;
}

bool ParseBool(Slice s, bool& out) noexcept {
    if (Equals(s, "1") || Equals(s, "true") || Equals(s, "yes") || Equals(s, "on")) { out = true; return true; }
    if (Equals(s, "0") || Equals(s, "false") || Equals(s, "no") || Equals(s, "off")) { out = false; return true; }
    return false;
}

bool ParseRange(Slice s, std::uint32_t min, std::uint32_t max, std::uint32_t& out) noexcept {
    std::uint32_t value = 0;
    if (!ParseU32(s, max, value) || value < min) return false;
    out = value;
    return true;
}

bool ParsePort(Slice s, std::uint16_t& out) noexcept {
    std::uint32_t value = 0;
    if (!ParseU32(s, 65535, value) || value < 1024) return false;
    out = static_cast<std::uint16_t>(value);
    return true;
}

// Individual-account SteamID64 in the public universe: 0x0110000100000000 | account.
bool IsIndividualSteamId(std::uint64_t id) noexcept {
    return (id >> 32) == 0x01100001u && (id & 0xffffffffu) != 0;
}

template <std::size_t N>
bool CopyText(Slice s, char (&out)[N], bool (*allowed)(char)) noexcept {
    if (s.size >= N) return false;
    for (std::size_t i = 0; i < s.size; ++i) if (!allowed(s.data[i])) return false;
    for (std::size_t i = 0; i < s.size; ++i) out[i] = s.data[i];
    out[s.size] = 0;
    return true;
}

bool PrintableNoControl(char c) noexcept { return static_cast<unsigned char>(c) >= 0x20 && c != 0x7f; }
bool SuffixChar(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
}
bool UrlChar(char c) noexcept {
    return static_cast<unsigned char>(c) > 0x20 && static_cast<unsigned char>(c) < 0x7f && c != '"' && c != '\\';
}

bool StartsWith(Slice s, const char* literal) noexcept {
    std::size_t i = 0;
    for (; literal[i]; ++i) if (i >= s.size || s.data[i] != literal[i]) return false;
    return true;
}
bool EndsWith(Slice s, const char* literal) noexcept {
    std::size_t n = 0;
    while (literal[n]) ++n;
    if (n > s.size) return false;
    for (std::size_t i = 0; i < n; ++i) if (s.data[s.size - n + i] != literal[i]) return false;
    return true;
}

bool IsLoopbackAddress(std::uint32_t address) noexcept { return (address >> 24) == 127; }

bool ParseCharacter(Slice s, PlayerCharacter& out) noexcept {
    struct Entry { const char* name; PlayerCharacter value; };
    static constexpr Entry entries[] = {
        {"default", PlayerCharacter::game_default}, {"snake", PlayerCharacter::snake},
        {"dd_male", PlayerCharacter::dd_male}, {"dd_female", PlayerCharacter::dd_female},
        {"avatar", PlayerCharacter::avatar}, {"ocelot", PlayerCharacter::ocelot},
        {"quiet", PlayerCharacter::quiet}};
    for (const auto& entry : entries) if (Equals(s, entry.name)) { out = entry.value; return true; }
    return false;
}

// Calls visit(key, value) for each key=value line; returns false if unusable.
template <typename Visit>
bool ForEachPair(const char* text, std::size_t length, Visit visit) noexcept {
    if ((!text && length) || length > kMaxText) return false;
    std::size_t start = 0;
    // Skip a UTF-8 byte-order mark written by some editors.
    if (length >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb && static_cast<unsigned char>(text[2]) == 0xbf) start = 3;
    while (start < length) {
        std::size_t end = start;
        while (end < length && text[end] != '\n') ++end;
        Slice line = Trim({text + start, end - start});
        start = end + 1;
        if (!line.size || line.data[0] == '#' || line.data[0] == ';') continue;
        std::size_t equals = 0;
        while (equals < line.size && line.data[equals] != '=') ++equals;
        if (equals == line.size) { visit(Slice{line.data, line.size}, Slice{nullptr, 0}, false); continue; }
        visit(Trim({line.data, equals}), Trim({line.data + equals + 1, line.size - equals - 1}), true);
    }
    return true;
}
} // namespace

bool ParseLabConfig(const char* text, std::size_t length, LabConfig& out) noexcept {
    LabConfig result{};
    bool enabled = false;
    // Keys whose validity depends on mode or role are resolved after the loop.
    bool bind_given = false, listen_given = false, listen_zero = false, peer_port_given = false;
    Slice peer_addr{nullptr, 0};
    bool peer_addr_given = false;
    std::uint32_t interp_max = 0;
    bool interp_max_given = false;
    const bool usable = ForEachPair(text, length, [&](Slice key, Slice value, bool has_value) {
        if (!has_value) { ++result.invalid_values; return; }
        bool ok = true;
        if (Equals(key, "enabled")) ok = ParseBool(value, enabled);
        else if (Equals(key, "role")) {
            if (Equals(value, "host")) result.role = LabRole::host;
            else if (Equals(value, "guest")) result.role = LabRole::guest;
            else ok = false;
        } else if (Equals(key, "loopback")) ok = ParseBool(value, result.loopback);
        else if (Equals(key, "self_steamid")) {
            std::uint64_t id = 0;
            if (Equals(value, "real")) result.self_steamid = 0;
            else if (ParseU64(value, id) && IsIndividualSteamId(id)) result.self_steamid = id;
            else ok = false;
        } else if (Equals(key, "self_name")) ok = CopyText(value, result.self_name, PrintableNoControl);
        else if (Equals(key, "peer_steamid")) {
            std::uint64_t id = 0;
            if (Equals(value, "learn")) result.peer_steamid = 0;
            else if (ParseU64(value, id) && IsIndividualSteamId(id)) result.peer_steamid = id;
            else ok = false;
        } else if (Equals(key, "listen_port")) ok = ParsePort(value, result.listen_port);
        else if (Equals(key, "peer_port")) ok = ParsePort(value, result.peer_port);
        else if (Equals(key, "mutex_suffix")) ok = CopyText(value, result.mutex_suffix, SuffixChar);
        else if (Equals(key, "save_dir")) ok = CopyText(value, result.save_dir, PrintableNoControl);
        else if (Equals(key, "xinput")) {
            std::uint32_t pad = 0;
            if (Equals(value, "passthrough")) result.xinput = kXInputPassthrough;
            else if (Equals(value, "none")) result.xinput = kXInputNone;
            else if (ParseU32(value, 3, pad)) result.xinput = static_cast<int>(pad);
            else ok = false;
        } else if (Equals(key, "join_trigger")) ok = ParseBool(value, result.join_trigger);
        else if (Equals(key, "auto_join_delay_s")) ok = ParseU32(value, 3600, result.auto_join_delay_s);
        else if (Equals(key, "net_latency_ms")) ok = ParseU32(value, 2000, result.net_latency_ms);
        else if (Equals(key, "net_loss_permille")) ok = ParseU32(value, 1000, result.net_loss_permille);
        else if (Equals(key, "trace_packets")) ok = ParseBool(value, result.trace_packets);
        else if (Equals(key, "net")) ok = ParseBool(value, result.net);
        else if (Equals(key, "net_listen_port")) {
            // 0 (an ephemeral port) is valid only for a guest off loopback (resolved below).
            if (Equals(value, "0")) listen_zero = true;
            else {
                ok = ParsePort(value, result.net_listen_port);
                if (ok) { listen_zero = false; listen_given = true; }
            }
        } else if (Equals(key, "net_peer_port")) {
            ok = ParsePort(value, result.net_peer_port);
            if (ok) peer_port_given = true;
        } else if (Equals(key, "mode")) {
            if (Equals(value, "lab")) result.mode = LabMode::lab;
            else if (Equals(value, "play")) result.mode = LabMode::play;
            else ok = false;
        } else if (Equals(key, "net_link")) {
            if (Equals(value, "v2")) result.net_link = NetLink::v2;
            else if (Equals(value, "legacy")) result.net_link = NetLink::legacy;
            else ok = false;
        } else if (Equals(key, "net_bind")) {
            std::uint32_t address = 0;
            if (Equals(value, "loopback")) { result.net_bind_loopback = true; result.net_bind_address = 0x7F000001; }
            else if (Equals(value, "any")) { result.net_bind_loopback = false; result.net_bind_address = 0; }
            else if (ParseIpv4(value.data, value.size, true, address)) {
                result.net_bind_loopback = IsLoopbackAddress(address);
                result.net_bind_address = address;
            } else ok = false;
            if (ok) bind_given = true;
        } else if (Equals(key, "net_peer_addr")) {
            peer_addr = value;
            peer_addr_given = true;
        } else if (Equals(key, "net_jitter_ms")) ok = ParseU32(value, 1000, result.net_jitter_ms);
        else if (Equals(key, "net_loss_model")) {
            if (Equals(value, "even")) result.net_loss_model = NetLossModel::even;
            else if (Equals(value, "burst")) result.net_loss_model = NetLossModel::burst;
            else ok = false;
        } else if (Equals(key, "net_burst")) ok = ParseRange(value, 1, 16, result.net_burst);
        else if (Equals(key, "net_dup_permille")) ok = ParseU32(value, 1000, result.net_dup_permille);
        else if (Equals(key, "net_reorder_permille")) ok = ParseU32(value, 1000, result.net_reorder_permille);
        else if (Equals(key, "net_reorder_ms")) ok = ParseRange(value, 1, 500, result.net_reorder_ms);
        else if (Equals(key, "net_seed")) ok = ParseRange(value, 1, 0xFFFFFFFFu, result.net_seed);
        else if (Equals(key, "partner_interp")) {
            if (Equals(value, "fixed")) result.partner_interp = PartnerInterp::fixed;
            else if (Equals(value, "auto")) result.partner_interp = PartnerInterp::auto_;
            else ok = false;
        } else if (Equals(key, "partner_interp_min_ms")) ok = ParseRange(value, 20, 500, result.partner_interp_min_ms);
        else if (Equals(key, "partner_interp_max_ms")) {
            ok = ParseU32(value, 1000, interp_max);
            if (ok) interp_max_given = true;
        } else if (Equals(key, "partner_lost")) {
            if (Equals(value, "freeze")) result.partner_lost = PartnerLost::freeze;
            else if (Equals(value, "hide")) result.partner_lost = PartnerLost::hide;
            else ok = false;
        }
        else if (Equals(key, "partner")) ok = ParseBool(value, result.partner);
        else if (Equals(key, "partner_stage")) ok = ParseRange(value, 0, 3, result.partner_stage);
        else if (Equals(key, "partner_source")) ok = ParsePartnerSource(value.data, value.size, result.partner_source);
        else if (Equals(key, "partner_apply") || Equals(key, "partner_move"))
            ok = ParsePartnerApply(value.data, value.size, result.partner_apply);
        else if (Equals(key, "partner_anim")) ok = ParsePartnerAnim(value.data, value.size, result.partner_anim);
        else if (Equals(key, "partner_aset")) ok = ParsePartnerAset(value.data, value.size, result.partner_aset);
        else if (Equals(key, "partner_reset")) ok = ParsePartnerReset(value.data, value.size, result.partner_reset);
        else if (Equals(key, "partner_look")) ok = ParsePartnerLook(value.data, value.size, result.partner_look);
        else if (Equals(key, "partner_rate_hz")) ok = ParseRange(value, 1, 60, result.partner_rate_hz);
        else if (Equals(key, "partner_interp_ms")) ok = ParseRange(value, 0, 500, result.partner_interp_ms);
        else if (Equals(key, "partner_offset_cm")) ok = ParseRange(value, 50, 1000, result.partner_offset_cm);
        else if (Equals(key, "partner_teardown")) {
            if (Equals(value, "clear")) result.partner_teardown = PartnerTeardown::clear;
            else if (Equals(value, "keep")) result.partner_teardown = PartnerTeardown::keep;
            else ok = false;
        } else if (Equals(key, "partner_force_exists")) ok = ParseBool(value, result.partner_force_exists);
        else if (Equals(key, "partner_policy_relax")) ok = ParseBool(value, result.partner_policy_relax);
        else if (Equals(key, "partner_anim_probe")) ok = ParseBool(value, result.partner_anim_probe);
        else if (Equals(key, "partner_input")) ok = ParsePartnerInput(value.data, value.size, result.partner_input);
        else if (Equals(key, "partner_input_cam")) ok = ParsePartnerInputCam(value.data, value.size, result.partner_input_cam);
        else if (Equals(key, "partner_input_hold")) ok = ParsePartnerInputHold(value.data, value.size, result.partner_input_hold);
        else if (Equals(key, "partner_input_yaw")) ok = ParsePartnerInputYaw(value.data, value.size, result.partner_input_yaw);
        else if (Equals(key, "partner_input_words")) ok = ParsePartnerWords(value.data, value.size, result.partner_input_words);
        else if (Equals(key, "partner_input_mask")) ok = ParsePartnerMask(value.data, value.size, result.partner_input_mask);
        else if (Equals(key, "partner_input_rstick")) ok = ParseBool(value, result.partner_input_rstick);
        else if (Equals(key, "partner_input_triggers")) ok = ParseBool(value, result.partner_input_triggers);
        else if (Equals(key, "partner_input_stick"))
            ok = ParsePartnerStick(value.data, value.size, result.partner_input_stick_deg, result.partner_input_stick_pct);
        else if (Equals(key, "partner_input_correct_mm")) ok = ParseRange(value, 0, 10000, result.partner_input_correct_mm);
        else if (Equals(key, "partner_input_stale_ms")) ok = ParseRange(value, 50, 2000, result.partner_input_stale_ms);
        else if (Equals(key, "partner_input_send")) ok = ParseBool(value, result.partner_input_send);
        else if (Equals(key, "partner_input_hook")) ok = ParseBool(value, result.partner_input_hook);
        else if (Equals(key, "partner_input_p218_allow")) ok = ParseBool(value, result.partner_input_p218_allow);
        else if (Equals(key, "partner_input_holdlog")) ok = ParseBool(value, result.partner_input_holdlog);
        else if (Equals(key, "partner_pad_probe")) ok = ParseBool(value, result.partner_pad_probe);
        else if (Equals(key, "partner_look_gate")) ok = ParseBool(value, result.partner_look_gate);
        else if (Equals(key, "partner_look_recover")) ok = ParseBool(value, result.partner_look_recover);
        else if (Equals(key, "partner_dm")) ok = ParsePartnerDm(value.data, value.size, result.partner_dm);
        else if (Equals(key, "partner_dm_map")) ok = ParsePartnerDmMap(value.data, value.size, result.partner_dm_map);
        else if (Equals(key, "partner_dm_speed")) ok = ParsePartnerDmSpeeds(value.data, value.size, result.partner_dm_speed);
        else if (Equals(key, "partner_recon")) ok = ParsePartnerRecon(value.data, value.size, result.partner_recon);
        else if (Equals(key, "partner_recon_align_ms"))
            ok = ParsePartnerSigned(value.data, value.size, -50, 150, result.partner_recon_align_ms);
        else if (Equals(key, "partner_recon_nudge_mmps")) ok = ParseRange(value, 100, 3000, result.partner_recon_nudge_mmps);
        else if (Equals(key, "partner_input_slot")) ok = ParsePartnerSlot(value.data, value.size, result.partner_input_slot);
        else if (Equals(key, "partner_recon_runaway")) ok = ParsePartnerRunaway(value.data, value.size, result.partner_recon_runaway);
        else if (Equals(key, "partner_input_wrap")) ok = ParsePartnerWrap(value.data, value.size, result.partner_input_wrap);
        else if (Equals(key, "partner_facing_dps")) ok = ParseRange(value, 0, 1440, result.partner_facing_dps);
        else if (Equals(key, "partner_drive")) ok = ParsePartnerDrive(value.data, value.size, result.partner_drive);
        else if (Equals(key, "partner_input_defer")) ok = ParsePartnerDefer(value.data, value.size, result.partner_input_defer);
        else if (Equals(key, "partner_input_local")) ok = ParsePartnerLocal(value.data, value.size, result.partner_input_local);
        else if (Equals(key, "partner_native_aim")) ok = ParsePartnerNativeAim(value.data, value.size, result.partner_native_aim);
        else if (Equals(key, "partner_native_rates")) ok = ParsePartnerNativeRates(value.data, value.size, result.partner_native_rates);
        else if (Equals(key, "partner_native_gun")) ok = ParseBool(value, result.partner_native_gun);
        else if (Equals(key, "partner_native_fire")) ok = ParsePartnerNativeFire(value.data, value.size, result.partner_native_fire);
        else if (Equals(key, "partner_native_steer_deg"))
            ok = ParseRange(value, 0, kPartnerNativeSteerMaxDeg, result.partner_native_steer_deg);
        else if (Equals(key, "partner_native_trim_pct"))
            ok = ParseRange(value, 0, kPartnerNativeTrimMaxPct, result.partner_native_trim_pct);
        else if (Equals(key, "crash_probe")) ok = ParseBool(value, result.crash_probe);
        else if (Equals(key, "world")) ok = ParseRange(value, 0, kWorldLevelMax, result.world);
        else if (Equals(key, "world_probe")) ok = ParseWorldProbe(value.data, value.size, result.world_probe);
        else if (Equals(key, "debug_soldier_warp")) ok = ParseBool(value, result.debug_soldier_warp);
        else { ++result.unknown_keys; return; }
        if (!ok) ++result.invalid_values;
    });
    if (!usable) return false;
    // A loopback instance needs both ports and a distinct identity source.
    if (result.loopback && (!result.listen_port || !result.peer_port || result.listen_port == result.peer_port)) {
        result.loopback = false;
        ++result.invalid_values;
    }
    // save_dir must be absolute (drive letter or UNC) to avoid working-directory surprises.
    if (result.save_dir[0]) {
        const char* p = result.save_dir;
        const bool drive = ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' && (p[2] == '\\' || p[2] == '/');
        const bool unc = p[0] == '\\' && p[1] == '\\';
        if (!drive && !unc) { result.save_dir[0] = 0; ++result.invalid_values; }
    }
    const bool play = result.mode == LabMode::play;
    // net_peer_addr: a dotted IPv4 only (127/8 only in lab mode).
    bool peer_addr_valid = false;
    if (peer_addr_given) {
        std::uint32_t address = 0;
        if (ParseIpv4(peer_addr.data, peer_addr.size, !play, address)) {
            result.net_peer_address = address;
            peer_addr_valid = true;
        } else {
            ++result.invalid_values;
            if (play) result.play_refused |= kPlayRefusedPeerAddr;
        }
    }
    if (interp_max_given) {
        if (interp_max < result.partner_interp_min_ms) ++result.invalid_values;
        else result.partner_interp_max_ms = interp_max;
    }
    // 1. Mode-dependent defaults: play binds every address, the host listens
    // on kNetPlayPort, the guest on an ephemeral port, and both target kNetPlayPort.
    if (play) {
        if (!bind_given) { result.net_bind_loopback = false; result.net_bind_address = 0; }
        if (!listen_given && !listen_zero) {
            if (result.role == LabRole::host) { result.net_listen_port = kNetPlayPort; listen_given = true; }
            else if (result.role == LabRole::guest) listen_zero = true;
        }
        if (!peer_port_given) { result.net_peer_port = kNetPlayPort; peer_port_given = true; }
    }
    // 2. Play refusals: each refused key is forced off, sets its bit and counts.
    if (play) {
        const auto refuse = [&](bool condition, std::uint32_t bit) noexcept {
            if (!condition) return false;
            result.play_refused |= bit;
            ++result.invalid_values;
            return true;
        };
        if (refuse(result.loopback, kPlayRefusedLoopback)) result.loopback = false;
        if (refuse(result.self_steamid != 0, kPlayRefusedSelfSteamId)) result.self_steamid = 0;
        if (refuse(result.peer_steamid != 0, kPlayRefusedPeerSteamId)) result.peer_steamid = 0;
        if (refuse(result.self_name[0] != 0, kPlayRefusedSelfName)) result.self_name[0] = 0;
        if (refuse(result.mutex_suffix[0] != 0, kPlayRefusedMutexSuffix)) result.mutex_suffix[0] = 0;
        if (refuse(result.xinput != kXInputPassthrough, kPlayRefusedXInput)) result.xinput = kXInputPassthrough;
        if (refuse(result.join_trigger, kPlayRefusedJoinTrigger)) result.join_trigger = false;
        if (refuse(result.auto_join_delay_s != 0, kPlayRefusedAutoJoin)) result.auto_join_delay_s = 0;
        if (refuse(result.role == LabRole::host && peer_addr_given, kPlayRefusedHostPeerAddr)) {
            peer_addr_valid = false;
            result.net_peer_address = 0x7F000001;
        }
        if (refuse(result.net_bind_loopback, kPlayRefusedBindLoopback)) {
            result.net_bind_loopback = false;
            result.net_bind_address = 0;
        }
        if (refuse(result.net_link == NetLink::legacy, kPlayRefusedLegacyLink)) result.net_link = NetLink::v2;
        refuse(result.save_dir[0] == 0, kPlayRefusedNoSaveDir);
        if (refuse(result.role == LabRole::guest && !peer_addr_valid, kPlayRefusedGuestNoPeer)) result.net = false;
        std::uint32_t* const impairments[] = {&result.net_latency_ms, &result.net_loss_permille, &result.net_jitter_ms,
                                              &result.net_dup_permille, &result.net_reorder_permille};
        for (std::uint32_t* value : impairments)
            if (refuse(*value != 0, kPlayRefusedImpairment)) *value = 0;
        // World-sync probes are lab experiments; world itself is allowed in play.
        if (refuse(result.world_probe != WorldProbe::none, kPlayRefusedWorldProbe)) result.world_probe = WorldProbe::none;
        // The native debug soldier placement stays a lab experiment until qualified.
        if (refuse(result.debug_soldier_warp, kPlayRefusedDebugSoldierWarp)) result.debug_soldier_warp = false;
        // The native puppet stays a lab experiment until its runtime qualification.
        if (refuse(result.partner_drive != PartnerDrive::legacy || result.partner_input_defer != PartnerDefer::off,
                   kPlayRefusedNativeDrive)) {
            result.partner_drive = PartnerDrive::legacy;
            result.partner_input_defer = PartnerDefer::off;
        }
    }
    // A loopback bind talks to 127.0.0.1 only.
    if (result.net_bind_loopback && peer_addr_valid && !IsLoopbackAddress(result.net_peer_address)) {
        ++result.invalid_values;
        result.net_peer_address = 0x7F000001;
        peer_addr_valid = false;
    }
    // net_listen_port=0 is an ephemeral port: only a guest off loopback may
    // ask for it (otherwise invalid and not given, exactly as before).
    if (listen_zero && !(result.role == LabRole::guest && !result.net_bind_loopback)) {
        ++result.invalid_values;
        listen_zero = false;
    }
    // 3. partner_net ports.
    if (result.net) {
        const auto collides = [&](std::uint16_t port) noexcept {
            return port && (port == result.listen_port || port == result.peer_port);
        };
        if (result.net_bind_loopback) {
            // Loopback (the lab): both ports default by role when neither is
            // named; the pair must be distinct and must not reuse the loopback
            // emulator's ports.
            if (!result.net_listen_port && !result.net_peer_port) {
                if (result.role == LabRole::host) { result.net_listen_port = kNetHostPort; result.net_peer_port = kNetGuestPort; }
                else if (result.role == LabRole::guest) { result.net_listen_port = kNetGuestPort; result.net_peer_port = kNetHostPort; }
            }
            const std::uint16_t a = result.net_listen_port, b = result.net_peer_port;
            if (!a || !b || a == b || collides(a) || collides(b)) {
                result.net = false;
                ++result.invalid_values;
            }
        } else if (result.role == LabRole::host) {
            // Any address: the host listens; its peer is learned (peer port ignored).
            if (!result.net_listen_port) result.net_listen_port = kNetHostPort;
            if (!result.net_peer_port) result.net_peer_port = kNetGuestPort;
            if (collides(result.net_listen_port)) {
                result.net = false;
                ++result.invalid_values;
            }
        } else if (result.role == LabRole::guest) {
            if (listen_zero) result.net_listen_port = 0;
            else if (!result.net_listen_port && !peer_port_given) result.net_listen_port = kNetGuestPort;
            if (!result.net_peer_port) result.net_peer_port = kNetHostPort;
            const bool listen_ok = listen_zero || result.net_listen_port >= 1024;
            if (!listen_ok || result.net_peer_port < 1024 || !peer_addr_valid || collides(result.net_listen_port)) {
                result.net = false;
                ++result.invalid_values;
            }
        } else {
            result.net = false;
            ++result.invalid_values;
        }
    }
    // 4. present is unchanged: the play save gate is SavesRedirected() at the observer.
    result.present = enabled && result.role != LabRole::none;
    out = result;
    return true;
}

bool ParseCoopConfig(const char* text, std::size_t length, CoopConfig& out) noexcept {
    CoopConfig result{};
    const bool usable = ForEachPair(text, length, [&](Slice key, Slice value, bool has_value) {
        if (!has_value) { ++result.invalid_values; return; }
        bool ok = true;
        if (Equals(key, "master_url")) {
            if (!value.size) result.master_url[0] = 0;
            else ok = (StartsWith(value, "http://") || StartsWith(value, "https://")) &&
                      EndsWith(value, "/tppstm/gate") && CopyText(value, result.master_url, UrlChar);
        } else if (Equals(key, "character")) ok = ParseCharacter(value, result.character);
        else if (Equals(key, "character_fallback")) {
            PlayerCharacter fallback{};
            ok = ParseCharacter(value, fallback) &&
                 (fallback == PlayerCharacter::dd_male || fallback == PlayerCharacter::dd_female ||
                  fallback == PlayerCharacter::snake || fallback == PlayerCharacter::avatar);
            if (ok) result.character_fallback = fallback;
        } else { ++result.unknown_keys; return; }
        if (!ok) ++result.invalid_values;
    });
    if (!usable) return false;
    result.present = true;
    out = result;
    return true;
}

const char* PartnerSourceName(PartnerSource value) noexcept {
    switch (value) {
    case PartnerSource::orbit: return "orbit";
    case PartnerSource::net: return "net";
    default: return "fixed";
    }
}

const char* PartnerApplyName(PartnerApply value) noexcept {
    switch (value) {
    case PartnerApply::slot: return "slot";
    case PartnerApply::both: return "both";
    case PartnerApply::xform: return "xform";
    case PartnerApply::hybrid: return "hybrid";
    case PartnerApply::input: return "input";
    default: return "warp";
    }
}

const char* PartnerAnimName(PartnerAnim value) noexcept {
    switch (value) {
    case PartnerAnim::mirror: return "mirror";
    case PartnerAnim::net: return "net";
    default: return "off";
    }
}

const char* PartnerAsetName(PartnerAset value) noexcept {
    switch (value) {
    case PartnerAset::q: return "q";
    case PartnerAset::s: return "s";
    case PartnerAset::r: return "r";
    default: return "qs";
    }
}

const char* PartnerResetName(PartnerReset value) noexcept {
    switch (value) {
    case PartnerReset::a: return "a";
    case PartnerReset::c: return "c";
    case PartnerReset::ac: return "ac";
    default: return "n";
    }
}

const char* PartnerLookName(PartnerLook value) noexcept {
    switch (value) {
    case PartnerLook::net: return "net";
    case PartnerLook::quiet: return "quiet";
    case PartnerLook::snake: return "snake";
    default: return "copy";
    }
}

const char* PartnerTeardownName(PartnerTeardown value) noexcept {
    return value == PartnerTeardown::keep ? "keep" : "clear";
}

const char* LabModeName(LabMode value) noexcept { return value == LabMode::play ? "play" : "lab"; }
const char* NetLinkName(NetLink value) noexcept { return value == NetLink::legacy ? "legacy" : "v2"; }
const char* PartnerInterpName(PartnerInterp value) noexcept { return value == PartnerInterp::auto_ ? "auto" : "fixed"; }
const char* PartnerLostName(PartnerLost value) noexcept { return value == PartnerLost::hide ? "hide" : "freeze"; }
const char* NetLossModelName(NetLossModel value) noexcept { return value == NetLossModel::burst ? "burst" : "even"; }

namespace {
constexpr const char* kWorldProbeNames[] = {"none", "p1", "p3", "p3b", "p4", "p12", "p17", "race_kill", "remote_area_kill",
    "kill_near", "sleep_near", "fulton_near", "alert_near", "weather_rain", "clock_jump", "clock_fast",
    "shared_death", "host_continue", "shared_checkpoint"};
static_assert(sizeof(kWorldProbeNames) / sizeof(kWorldProbeNames[0]) == kWorldProbeMax + 1u);
}

const char* WorldProbeName(WorldProbe value) noexcept {
    const auto index = static_cast<unsigned>(value);
    return index <= kWorldProbeMax ? kWorldProbeNames[index] : "?";
}

bool ParseWorldProbe(const char* text, std::size_t size, WorldProbe& out) noexcept {
    if (!text) return false;
    for (unsigned i = 0; i <= kWorldProbeMax; ++i) {
        const char* name = kWorldProbeNames[i];
        std::size_t n = 0;
        while (name[n]) ++n;
        bool same = n == size;
        for (std::size_t k = 0; same && k < n; ++k) same = text[k] == name[k];
        if (same) { out = static_cast<WorldProbe>(i); return true; }
    }
    return false;
}

bool ParseIpv4(const char* text, std::size_t size, bool allow_loopback, std::uint32_t& out) noexcept {
    if (!text || !size || size > 15) return false;
    std::uint32_t address = 0;
    std::size_t at = 0;
    for (int octet = 0; octet < 4; ++octet) {
        if (octet) {
            if (at >= size || text[at] != '.') return false;
            ++at;
        }
        const std::size_t start = at;
        std::uint32_t value = 0;
        while (at < size && text[at] >= '0' && text[at] <= '9' && at - start < 3) value = value * 10 + static_cast<std::uint32_t>(text[at++] - '0');
        const std::size_t digits = at - start;
        if (!digits || (digits > 1 && text[start] == '0') || value > 255) return false;
        address = (address << 8) | value;
    }
    if (at != size) return false;
    if (address == 0 || address == 0xFFFFFFFFu || (address >> 28) == 0xE) return false;
    if (!allow_loopback && IsLoopbackAddress(address)) return false;
    out = address;
    return true;
}

bool ParsePartnerSource(const char* text, std::size_t size, PartnerSource& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "fixed")) out = PartnerSource::fixed;
    else if (Equals(s, "orbit")) out = PartnerSource::orbit;
    else if (Equals(s, "net")) out = PartnerSource::net;
    else return false;
    return true;
}

bool ParsePartnerApply(const char* text, std::size_t size, PartnerApply& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "warp")) out = PartnerApply::warp;
    else if (Equals(s, "slot")) out = PartnerApply::slot;
    else if (Equals(s, "both")) out = PartnerApply::both;
    else if (Equals(s, "xform")) out = PartnerApply::xform;
    else if (Equals(s, "hybrid")) out = PartnerApply::hybrid;
    else if (Equals(s, "input")) out = PartnerApply::input;
    else return false;
    return true;
}

bool ParsePartnerAnim(const char* text, std::size_t size, PartnerAnim& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerAnim::off;
    else if (Equals(s, "mirror")) out = PartnerAnim::mirror;
    else if (Equals(s, "net")) out = PartnerAnim::net;
    else return false;
    return true;
}

bool ParsePartnerAset(const char* text, std::size_t size, PartnerAset& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "q")) out = PartnerAset::q;
    else if (Equals(s, "s")) out = PartnerAset::s;
    else if (Equals(s, "qs")) out = PartnerAset::qs;
    else if (Equals(s, "r")) out = PartnerAset::r;
    else return false;
    return true;
}

bool ParsePartnerReset(const char* text, std::size_t size, PartnerReset& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "n")) out = PartnerReset::none;
    else if (Equals(s, "a")) out = PartnerReset::a;
    else if (Equals(s, "c")) out = PartnerReset::c;
    else if (Equals(s, "ac")) out = PartnerReset::ac;
    else return false;
    return true;
}

bool ParsePartnerLook(const char* text, std::size_t size, PartnerLook& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "copy")) out = PartnerLook::copy;
    else if (Equals(s, "net")) out = PartnerLook::net;
    else if (Equals(s, "quiet")) out = PartnerLook::quiet;
    else if (Equals(s, "snake")) out = PartnerLook::snake;
    else return false;
    return true;
}

const char* PartnerInputName(PartnerInput value) noexcept {
    switch (value) {
    case PartnerInput::pass: return "pass";
    case PartnerInput::neutral: return "neutral";
    case PartnerInput::stick: return "stick";
    case PartnerInput::net: return "net";
    default: return "off";
    }
}

const char* PartnerInputCamName(PartnerInputCam value) noexcept {
    switch (value) {
    case PartnerInputCam::off: return "off";
    case PartnerInputCam::detect: return "detect";
    default: return "sandbox";
    }
}

const char* PartnerInputHoldName(PartnerInputHold value) noexcept {
    switch (value) {
    case PartnerInputHold::pass: return "pass";
    case PartnerInputHold::own: return "own";
    case PartnerInputHold::net: return "net";
    default: return "none";
    }
}

const char* PartnerReconName(PartnerRecon value) noexcept {
    switch (value) {
    case PartnerRecon::off: return "off";
    case PartnerRecon::pin: return "pin";
    default: return "track";
    }
}

const char* PartnerSlotName(PartnerSlot value) noexcept {
    switch (value) {
    case PartnerSlot::remote: return "remote";
    case PartnerSlot::none: return "none";
    default: return "body";
    }
}

const char* PartnerRunawayName(PartnerRunaway value) noexcept {
    switch (value) {
    case PartnerRunaway::disarm: return "disarm";
    case PartnerRunaway::off: return "off";
    default: return "pin";
    }
}

const char* PartnerWrapName(PartnerWrap value) noexcept {
    switch (value) {
    case PartnerWrap::census: return "census";
    case PartnerWrap::window: return "window";
    default: return "off";
    }
}

bool ParsePartnerRecon(const char* text, std::size_t size, PartnerRecon& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerRecon::off;
    else if (Equals(s, "track")) out = PartnerRecon::track;
    else if (Equals(s, "pin")) out = PartnerRecon::pin;
    else return false;
    return true;
}

bool ParsePartnerSlot(const char* text, std::size_t size, PartnerSlot& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "remote")) out = PartnerSlot::remote;
    else if (Equals(s, "body")) out = PartnerSlot::body;
    else if (Equals(s, "none")) out = PartnerSlot::none;
    else return false;
    return true;
}

bool ParsePartnerRunaway(const char* text, std::size_t size, PartnerRunaway& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "pin")) out = PartnerRunaway::pin;
    else if (Equals(s, "disarm")) out = PartnerRunaway::disarm;
    else if (Equals(s, "off")) out = PartnerRunaway::off;
    else return false;
    return true;
}

bool ParsePartnerWrap(const char* text, std::size_t size, PartnerWrap& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerWrap::off;
    else if (Equals(s, "census")) out = PartnerWrap::census;
    else if (Equals(s, "window")) out = PartnerWrap::window;
    else return false;
    return true;
}

const char* PartnerDriveName(PartnerDrive value) noexcept { return value == PartnerDrive::native ? "native" : "legacy"; }

const char* PartnerDeferName(PartnerDefer value) noexcept {
    switch (value) {
    case PartnerDefer::census: return "census";
    case PartnerDefer::on: return "on";
    default: return "off";
    }
}

const char* PartnerLocalName(PartnerLocal value) noexcept { return value == PartnerLocal::all ? "all" : "main"; }

const char* PartnerNativeAimName(PartnerNativeAim value) noexcept {
    switch (value) {
    case PartnerNativeAim::look: return "look";
    case PartnerNativeAim::lookcam: return "lookcam";
    default: return "off";
    }
}

bool ParsePartnerDrive(const char* text, std::size_t size, PartnerDrive& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "legacy")) out = PartnerDrive::legacy;
    else if (Equals(s, "native")) out = PartnerDrive::native;
    else return false;
    return true;
}

bool ParsePartnerDefer(const char* text, std::size_t size, PartnerDefer& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerDefer::off;
    else if (Equals(s, "census")) out = PartnerDefer::census;
    else if (Equals(s, "on")) out = PartnerDefer::on;
    else return false;
    return true;
}

bool ParsePartnerLocal(const char* text, std::size_t size, PartnerLocal& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "main")) out = PartnerLocal::main;
    else if (Equals(s, "all")) out = PartnerLocal::all;
    else return false;
    return true;
}

const char* PartnerNativeFireName(PartnerNativeFire value) noexcept {
    return value == PartnerNativeFire::visual ? "visual" : "off";
}

bool ParsePartnerNativeFire(const char* text, std::size_t size, PartnerNativeFire& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "0") || Equals(s, "off")) out = PartnerNativeFire::off;
    else if (Equals(s, "1") || Equals(s, "visual")) out = PartnerNativeFire::visual;
    else return false;
    return true;
}

bool ParsePartnerNativeAim(const char* text, std::size_t size, PartnerNativeAim& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeAim::off;
    else if (Equals(s, "look")) out = PartnerNativeAim::look;
    else if (Equals(s, "lookcam")) out = PartnerNativeAim::lookcam;
    else return false;
    return true;
}

const char* PartnerNativeRatesName(PartnerNativeRates value) noexcept {
    switch (value) {
    case PartnerNativeRates::off: return "off";
    case PartnerNativeRates::mirror: return "mirror";
    default: return "log";
    }
}

bool ParsePartnerNativeRates(const char* text, std::size_t size, PartnerNativeRates& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeRates::off;
    else if (Equals(s, "log")) out = PartnerNativeRates::log;
    else if (Equals(s, "mirror")) out = PartnerNativeRates::mirror;
    else return false;
    return true;
}

bool ParsePartnerSigned(const char* text, std::size_t size, std::int32_t min, std::int32_t max, std::int32_t& out) noexcept {
    if (!text || !size || size > 11) return false;
    std::size_t i = 0;
    bool negative = false;
    if (text[0] == '-') { negative = true; i = 1; }
    if (i >= size) return false;
    if (text[i] == '0' && (size - i > 1 || negative)) return false;  // no leading zeros, no "-0"
    std::int64_t value = 0;
    for (; i < size; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        value = value * 10 + (text[i] - '0');
        if (value > 0x7FFFFFFFll) return false;
    }
    if (negative) value = -value;
    if (value < min || value > max) return false;
    out = static_cast<std::int32_t>(value);
    return true;
}

const char* PartnerInputYawName(PartnerInputYaw value) noexcept {
    switch (value) {
    case PartnerInputYaw::rotneg: return "rotneg";
    case PartnerInputYaw::none: return "none";
    default: return "rot";
    }
}

const char* PartnerDmName(PartnerDm value) noexcept {
    switch (value) {
    case PartnerDm::probe: return "probe";
    case PartnerDm::on: return "on";
    case PartnerDm::map: return "map";
    default: return "off";
    }
}

bool ParsePartnerInput(const char* text, std::size_t size, PartnerInput& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerInput::off;
    else if (Equals(s, "pass")) out = PartnerInput::pass;
    else if (Equals(s, "neutral")) out = PartnerInput::neutral;
    else if (Equals(s, "stick")) out = PartnerInput::stick;
    else if (Equals(s, "net")) out = PartnerInput::net;
    else return false;
    return true;
}

bool ParsePartnerInputCam(const char* text, std::size_t size, PartnerInputCam& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerInputCam::off;
    else if (Equals(s, "detect")) out = PartnerInputCam::detect;
    else if (Equals(s, "sandbox")) out = PartnerInputCam::sandbox;
    else return false;
    return true;
}

bool ParsePartnerInputHold(const char* text, std::size_t size, PartnerInputHold& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "none")) out = PartnerInputHold::none;
    else if (Equals(s, "pass")) out = PartnerInputHold::pass;
    else if (Equals(s, "own")) out = PartnerInputHold::own;
    else if (Equals(s, "net")) out = PartnerInputHold::net;
    else return false;
    return true;
}

bool ParsePartnerInputYaw(const char* text, std::size_t size, PartnerInputYaw& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "rot")) out = PartnerInputYaw::rot;
    else if (Equals(s, "rotneg")) out = PartnerInputYaw::rotneg;
    else if (Equals(s, "none")) out = PartnerInputYaw::none;
    else return false;
    return true;
}

bool ParsePartnerDm(const char* text, std::size_t size, PartnerDm& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerDm::off;
    else if (Equals(s, "probe")) out = PartnerDm::probe;
    else if (Equals(s, "on")) out = PartnerDm::on;
    else if (Equals(s, "map")) out = PartnerDm::map;
    else return false;
    return true;
}

bool ParsePartnerWords(const char* text, std::size_t size, std::uint8_t (&out)[3]) noexcept {
    if (!text || size != 3) return false;
    std::uint8_t words[3]{};
    for (std::size_t i = 0; i < 3; ++i) {
        switch (text[i]) {
        case '0': words[i] = kPartnerWordZero; break;
        case 'p': words[i] = kPartnerWordPress; break;
        case 'r': words[i] = kPartnerWordRelease; break;
        case 'h': words[i] = kPartnerWordHeld; break;
        case 'n': words[i] = kPartnerWordNet; break;
        default: return false;
        }
    }
    for (std::size_t i = 0; i < 3; ++i) out[i] = words[i];
    return true;
}

void FormatPartnerWords(const std::uint8_t (&words)[3], char (&out)[4]) noexcept {
    static constexpr char names[] = "0prhn";
    for (std::size_t i = 0; i < 3; ++i) out[i] = words[i] <= kPartnerWordNet ? names[words[i]] : '?';
    out[3] = 0;
}

bool ParsePartnerDmMap(const char* text, std::size_t size, PartnerDmTable& out) noexcept {
    if (!text || !size) return false;
    PartnerDmTable table{};
    std::size_t at = 0, cells = 0;
    for (;;) {
        if (cells >= kPartnerDmCells) return false;
        const std::size_t start = at;
        std::uint32_t value = 0;
        while (at < size && text[at] >= '0' && text[at] <= '9') {
            if (at - start >= 2) return false;
            value = value * 10 + static_cast<std::uint32_t>(text[at] - '0');
            ++at;
        }
        const std::size_t digits = at - start;
        if (!digits || (digits > 1 && text[start] == '0') || value > kPartnerDmGaniMax) return false;
        table.cells[cells++] = static_cast<std::uint8_t>(value);
        if (at == size) break;
        if (text[at] != ',') return false;
        ++at;
    }
    if (cells != kPartnerDmCells) return false;
    out = table;
    return true;
}

void FormatPartnerDmMap(const PartnerDmTable& table, char (&out)[40]) noexcept {
    std::size_t at = 0;
    for (std::size_t i = 0; i < kPartnerDmCells; ++i) {
        if (i) out[at++] = ',';
        const unsigned value = table.cells[i];
        if (value > 99) out[at++] = '?';
        else {
            if (value >= 10) out[at++] = static_cast<char>('0' + value / 10);
            out[at++] = static_cast<char>('0' + value % 10);
        }
    }
    out[at] = 0;
}

bool ParsePartnerDmSpeeds(const char* text, std::size_t size, PartnerDmSpeeds& out) noexcept {
    if (!text || !size) return false;
    std::uint32_t values[3]{};
    std::size_t at = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t start = at;
        while (at < size && text[at] != ':') ++at;
        const Slice s{text + start, at - start};
        if (!s.size || s.size > 5 || (s.size > 1 && s.data[0] == '0') || !ParseU32(s, 99999, values[i])) return false;
        if (i < 2) {
            if (at >= size) return false;
            ++at;
        }
    }
    if (at != size) return false;
    if (values[0] < 200 || values[0] > 2000 || values[1] < values[0] + 300 || values[1] > 10000 || values[2] < values[1] + 300 ||
        values[2] > 20000) return false;
    out = PartnerDmSpeeds{values[0], values[1], values[2]};
    return true;
}

void FormatPartnerDmSpeeds(const PartnerDmSpeeds& speeds, char (&out)[24]) noexcept {
    const std::uint32_t values[3] = {speeds.idle_mmps, speeds.run_mmps, speeds.dash_mmps};
    std::size_t at = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        if (i) out[at++] = ':';
        const std::uint32_t value = values[i] > 99999 ? 99999 : values[i];
        char digits[5]{};
        std::size_t n = 0;
        std::uint32_t v = value;
        do { digits[n++] = static_cast<char>('0' + v % 10); v /= 10; } while (v && n < sizeof(digits));
        while (n) out[at++] = digits[--n];
    }
    out[at] = 0;
}

bool ParsePartnerMask(const char* text, std::size_t size, std::uint32_t& out) noexcept {
    if (!text || !size) return false;
    if (size > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        if (size - 2 > 8) return false;
        std::uint32_t value = 0;
        for (std::size_t i = 2; i < size; ++i) {
            const char c = text[i];
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') digit = static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') digit = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digit = static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
            value = (value << 4) | digit;
        }
        out = value;
        return true;
    }
    return ParseU32(Slice{text, size}, 0xFFFFFFFFu, out);
}

bool ParsePartnerStick(const char* text, std::size_t size, std::uint32_t& deg, std::uint32_t& pct) noexcept {
    if (!text || !size) return false;
    std::size_t colon = 0;
    while (colon < size && text[colon] != ':') ++colon;
    if (colon == 0 || colon + 1 >= size) return false;
    const Slice a{text, colon}, b{text + colon + 1, size - colon - 1};
    const auto canonical = [](Slice s) noexcept { return s.size == 1 || s.data[0] != '0'; };
    std::uint32_t d = 0, p = 0;
    if (!canonical(a) || !canonical(b) || !ParseU32(a, 359, d) || !ParseU32(b, 100, p)) return false;
    deg = d;
    pct = p;
    return true;
}

const char* PlayerCharacterName(PlayerCharacter value) noexcept {
    switch (value) {
    case PlayerCharacter::snake: return "snake";
    case PlayerCharacter::dd_male: return "dd_male";
    case PlayerCharacter::dd_female: return "dd_female";
    case PlayerCharacter::avatar: return "avatar";
    case PlayerCharacter::ocelot: return "ocelot";
    case PlayerCharacter::quiet: return "quiet";
    default: return "";
    }
}

int PlayerCharacterType(PlayerCharacter value) noexcept {
    switch (value) {
    case PlayerCharacter::snake: return 0;
    case PlayerCharacter::dd_male: return 1;
    case PlayerCharacter::dd_female: return 2;
    case PlayerCharacter::avatar: return 3;
    case PlayerCharacter::ocelot: return 5;
    case PlayerCharacter::quiet: return 6;
    default: return -1;
    }
}

#ifdef _WIN32
namespace {
INIT_ONCE lab_once = INIT_ONCE_STATIC_INIT;
INIT_ONCE coop_once = INIT_ONCE_STATIC_INIT;
LabConfig lab_config{};
CoopConfig coop_config{};
char file_buffer[kMaxText + 1];  // guarded by the INIT_ONCE callbacks (serialized by InitOnce)
SRWLOCK buffer_lock = SRWLOCK_INIT;

// Reads <exeDir>\mgs-coop\<name> into file_buffer; returns byte count or -1.
long ReadConfigFile(const wchar_t* name) noexcept {
    wchar_t path[MAX_PATH * 2]{};
    const DWORD size = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
    if (!size || size >= ARRAYSIZE(path)) return -1;
    DWORD separator = 0;
    for (DWORD i = 0; i < size; ++i) if (path[i] == L'\\') separator = i;
    if (!separator) return -1;
    const wchar_t* folder = L"\\mgs-coop\\";
    DWORD at = separator;
    for (const wchar_t* p = folder; *p; ++p) { if (at + 1 >= ARRAYSIZE(path)) return -1; path[at++] = *p; }
    for (const wchar_t* p = name; *p; ++p) { if (at + 1 >= ARRAYSIZE(path)) return -1; path[at++] = *p; }
    path[at] = 0;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return -1;
    LARGE_INTEGER file_size{};
    long result = -1;
    if (GetFileSizeEx(file, &file_size) && file_size.QuadPart >= 0 && file_size.QuadPart <= static_cast<LONGLONG>(kMaxText)) {
        DWORD read = 0;
        if (ReadFile(file, file_buffer, static_cast<DWORD>(file_size.QuadPart), &read, nullptr) &&
            read == static_cast<DWORD>(file_size.QuadPart)) result = static_cast<long>(read);
    }
    CloseHandle(file);
    return result;
}

BOOL CALLBACK LoadLab(PINIT_ONCE, PVOID, PVOID*) noexcept {
    AcquireSRWLockExclusive(&buffer_lock);
    const long size = ReadConfigFile(L"lab.ini");
    if (size >= 0) ParseLabConfig(file_buffer, static_cast<std::size_t>(size), lab_config);
    ReleaseSRWLockExclusive(&buffer_lock);
    return TRUE;
}

BOOL CALLBACK LoadCoop(PINIT_ONCE, PVOID, PVOID*) noexcept {
    AcquireSRWLockExclusive(&buffer_lock);
    const long size = ReadConfigFile(L"coop.ini");
    if (size >= 0) ParseCoopConfig(file_buffer, static_cast<std::size_t>(size), coop_config);
    ReleaseSRWLockExclusive(&buffer_lock);
    return TRUE;
}
} // namespace

const LabConfig& GetLabConfig() noexcept {
    const DWORD error = GetLastError();
    InitOnceExecuteOnce(&lab_once, LoadLab, nullptr, nullptr);
    SetLastError(error);
    return lab_config;
}

const CoopConfig& GetCoopConfig() noexcept {
    const DWORD error = GetLastError();
    InitOnceExecuteOnce(&coop_once, LoadCoop, nullptr, nullptr);
    SetLastError(error);
    return coop_config;
}
#endif

} // namespace mgscoop
