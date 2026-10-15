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
bool NametagNameChar(char c) noexcept {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_' || c == '.' || c == '-';
}
// coop.ini name=: any UTF-8 byte except controls (sanitized again before display).
bool NameByte(char c) noexcept { return static_cast<unsigned char>(c) >= 0x20 && c != 0x7f; }
bool ParseNametagSetting(Slice s, NametagSetting& out) noexcept {
    bool value = false;
    if (!ParseBool(s, value)) return false;
    out = value ? NametagSetting::on : NametagSetting::off;
    return true;
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

// The lab.ini keys partner_native_profile writes (ApplyNativeProfile), as
// their kProfileKey* bit: a key named explicitly keeps its own value.
std::uint32_t ProfileKeyBit(Slice key) noexcept {
    struct Entry { const char* name; std::uint32_t bit; };
    static constexpr Entry entries[] = {
        {"partner_drive", kProfileKeyDrive}, {"partner_input_defer", kProfileKeyDefer}, {"partner_input_send", kProfileKeySend},
        {"partner_input_hook", kProfileKeyHook}, {"partner_input_hold", kProfileKeyHold}, {"partner_input_words", kProfileKeyWords},
        {"partner_input_wrap", kProfileKeyWrap}, {"partner_input_cam", kProfileKeyCam}, {"partner_input_local", kProfileKeyLocal},
        {"partner_recon", kProfileKeyRecon}, {"partner_input_slot", kProfileKeySlot}, {"partner_native_rates", kProfileKeyRates},
        {"partner_native_noise", kProfileKeyNoise}, {"partner_input_p218_allow", kProfileKeyP218}, {"partner_input_mask", kProfileKeyMask},
        {"partner_native_aim", kProfileKeyAim}, {"partner_native_gun", kProfileKeyGun}, {"partner_native_fire", kProfileKeyFire},
        {"partner_input_triggers", kProfileKeyTriggers}, {"partner_input_rstick", kProfileKeyRstick},
        {"partner_native_aimpt", kProfileKeyAimPt}};
    for (const auto& entry : entries) if (Equals(key, entry.name)) return entry.bit;
    return 0;
}

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

// The round-1 actions track's keys (docs/anim-round1-actions.md): true when
// `key` is one of them (`ok` then says whether its value parsed).
bool ParseRound1ActionsKey(Slice key, Slice value, LabConfig& result, bool& ok) noexcept {
    if (Equals(key, "partner_native_overlay")) ok = ParsePartnerNativeOverlay(value.data, value.size, result.partner_native_overlay);
    else if (Equals(key, "partner_act_log")) ok = ParseBool(value, result.partner_act_log);
    else if (Equals(key, "partner_native_stance_fast")) ok = ParseBool(value, result.partner_native_stance_fast);
    else return false;
    return true;
}
// Automatic save handoff and the guest travel lock (join-fix phase 2 P2-11):
// save_handoff=off|stage|scene|always and travel_lock=0|1. Neither is a play
// refusal; the effective travel lock is EffectiveTravelLock.
bool ParseSaveHandoffKey(Slice key, Slice value, LabConfig& result, bool& ok) noexcept {
    if (Equals(key, "save_handoff")) {
        ok = ParseSaveHandoff(value.data, value.size, result.save_handoff);
        if (ok) result.save_handoff_given = true;
    } else if (Equals(key, "travel_lock")) {
        bool lock = false;
        ok = ParseBool(value, lock);
        if (ok) result.travel_lock = lock ? 1 : 0;
    } else {
        return false;
    }
    return true;
}
} // namespace

bool ParseLabConfig(const char* text, std::size_t length, LabConfig& out) noexcept {
    return ParseLabConfig(text, length, out, LabParseOptions{});
}

namespace {
// Anim round 1 weapons (docs/anim-round1-weapons.md): true when key is one of
// them (ok: the value parsed).
bool ParseAnimWeaponsKey(Slice key, Slice value, LabConfig& result, bool& ok) noexcept {
    if (Equals(key, "partner_native_unsupported")) {
        ok = ParseBool(value, result.partner_native_unsupported);
        if (ok) result.partner_native_unsupported_given = true;
    } else if (Equals(key, "partner_native_dry_stop")) {
        ok = ParseBool(value, result.partner_native_dry_stop);
        if (ok) result.partner_native_dry_stop_given = true;
    } else if (Equals(key, "partner_native_aim_intent")) {
        ok = ParsePartnerNativeAimIntent(value.data, value.size, result.partner_native_aim_intent);
        if (ok) result.partner_native_aim_intent_given = true;
    } else if (Equals(key, "partner_native_reload_rate")) {
        ok = ParsePartnerNativeReloadRate(value.data, value.size, result.partner_native_reload_rate);
        if (ok) result.partner_native_reload_rate_given = true;
    } else if (Equals(key, "partner_fire_census")) {
        ok = ParseBool(value, result.partner_fire_census);
    } else {
        return false;
    }
    return true;
}
} // namespace
bool ParseLabConfig(const char* text, std::size_t length, LabConfig& out, const LabParseOptions& options) noexcept {
    LabConfig result{};
    bool enabled = false;
    // Online play: the profile expands after the loop (keys named explicitly
    // keep their value: `given`); the play defaults apply to keys not named.
    PartnerNativeProfile profile = PartnerNativeProfile::off;
    std::uint32_t given = 0;
    bool native_net_given = false, stall_given = false, repeats_given = false;
    // Keys whose validity depends on mode or role are resolved after the loop.
    bool bind_given = false, listen_given = false, listen_zero = false, peer_port_given = false;
    Slice peer_addr{nullptr, 0};
    bool peer_addr_given = false;
    bool peer_steam_given = false;
    std::uint32_t interp_max = 0;
    bool interp_max_given = false;
    const bool usable = ForEachPair(text, length, [&](Slice key, Slice value, bool has_value) {
        if (!has_value) { ++result.invalid_values; return; }
        bool ok = true;
        // Anim round 1 and save handoff keys: separate chains (the one below is
        // at MSVC's block nesting limit).
        if (ParseAnimWeaponsKey(key, value, result, ok) || ParseRound1ActionsKey(key, value, result, ok) ||
            ParseSaveHandoffKey(key, value, result, ok)) {
            if (!ok) ++result.invalid_values;
            return;
        }
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
        else if (Equals(key, "partner_native_face")) ok = ParsePartnerNativeFace(value.data, value.size, result.partner_native_face);
        else if (Equals(key, "partner_native_noise")) ok = ParsePartnerNativeNoise(value.data, value.size, result.partner_native_noise);
        else if (Equals(key, "partner_native_aimpt")) {
            ok = ParsePartnerNativeAimPt(value.data, value.size, result.partner_native_aimpt);
            if (ok) result.partner_native_aimpt_given = true;
        }
        else if (Equals(key, "partner_native_reload")) {
            ok = ParsePartnerNativeReload(value.data, value.size, result.partner_native_reload);
            if (ok) result.partner_native_reload_given = true;
        } else if (Equals(key, "partner_native_slot")) {
            ok = ParsePartnerNativeSlot(value.data, value.size, result.partner_native_slot);
            if (ok) result.partner_native_slot_given = true;
        } else if (Equals(key, "partner_native_equip")) {
            ok = ParsePartnerNativeEquip(value.data, value.size, result.partner_native_equip);
            if (ok) result.partner_native_equip_given = true;
        } else if (Equals(key, "partner_native_equip_shared")) {
            ok = ParsePartnerNativeEquipShared(value.data, value.size, result.partner_native_equip_shared);
            if (ok) result.partner_native_equip_shared_given = true;
        } else if (Equals(key, "partner_native_standin")) {
            ok = ParsePartnerNativeStandin(value.data, value.size, result.partner_native_standin);
            if (ok) result.partner_native_standin_given = true;
        }
        else if (Equals(key, "partner_native_equip_measured")) ok = ParseBool(value, result.partner_native_equip_measured);
        else if (Equals(key, "partner_native_ride")) {
            ok = ParsePartnerNativeRide(value.data, value.size, result.partner_native_ride);
            if (ok) result.partner_native_ride_given = true;
        }
        else if (Equals(key, "partner_ride_seat_enter")) ok = ParsePartnerRideSeatEnter(value.data, value.size, result.partner_ride_seat_enter);
        else if (Equals(key, "partner_ride_seat_exit")) ok = ParsePartnerRideSeatExit(value.data, value.size, result.partner_ride_seat_exit);
        else if (Equals(key, "partner_native_steer_deg"))
            ok = ParseRange(value, 0, kPartnerNativeSteerMaxDeg, result.partner_native_steer_deg);
        else if (Equals(key, "partner_native_trim_pct"))
            ok = ParseRange(value, 0, kPartnerNativeTrimMaxPct, result.partner_native_trim_pct);
        else if (Equals(key, "crash_probe")) ok = ParseBool(value, result.crash_probe);
        else if (Equals(key, "world")) ok = ParseRange(value, 0, kWorldLevelMax, result.world);
        else if (Equals(key, "world_probe")) ok = ParseWorldProbe(value.data, value.size, result.world_probe);
        else if (Equals(key, "debug_soldier_warp")) ok = ParseBool(value, result.debug_soldier_warp);
        else if (Equals(key, "nametag")) ok = ParseNametagSetting(value, result.nametag);
        else if (Equals(key, "nametag_crosshair")) ok = ParseNametagCrosshair(value.data, value.size, result.nametag_crosshair);
        else if (Equals(key, "nametag_name"))
            ok = value.size && value.data[0] != ' ' && value.data[value.size - 1] != ' ' && CopyText(value, result.nametag_name, NametagNameChar);
        else if (Equals(key, "nametag_max_m")) ok = ParseRange(value, kNametagMaxMMin, kNametagMaxMMax, result.nametag_max_m);
        else if (Equals(key, "net_transport")) {
            if (Equals(value, "udp")) result.net_transport = NetTransport::udp;
            else if (Equals(value, "steam")) result.net_transport = NetTransport::steam;
            else ok = false;
        } else if (Equals(key, "net_steam_self")) {
            std::uint64_t id = 0;
            ok = ParseU64(value, id) && IsIndividualSteamId(id);
            if (ok) result.net_steam_self = id;
        } else if (Equals(key, "net_peer_steam")) {
            std::uint64_t id = 0;
            ok = ParseU64(value, id) && IsIndividualSteamId(id);
            if (ok) { result.net_peer_steam = id; peer_steam_given = true; }
        } else if (Equals(key, "net_steam_wait")) ok = ParseBool(value, result.net_steam_wait);
        else if (Equals(key, "net_steam_accept")) {
            if (Equals(value, "on")) result.net_steam_accept = true;
            else if (Equals(value, "off")) result.net_steam_accept = false;
            else ok = false;
        } else if (Equals(key, "steam_selftest")) ok = ParseRange(value, 0, 2, result.steam_selftest);
        else if (Equals(key, "partner_native_profile")) ok = ParsePartnerNativeProfile(value.data, value.size, profile);
        else if (Equals(key, "partner_native_net")) {
            ok = ParsePartnerNativeNet(value.data, value.size, result.partner_native_net);
            if (ok) native_net_given = true;
        } else if (Equals(key, "partner_native_stall")) {
            ok = ParsePartnerNativeStall(value.data, value.size, result.partner_native_stall);
            if (ok) stall_given = true;
        } else if (Equals(key, "partner_native_stall_ms"))
            ok = ParseRange(value, kPartnerStallMsMin, kPartnerStallMsMax, result.partner_native_stall_ms);
        else if (Equals(key, "partner_native_hold")) ok = ParsePartnerNativeHold(value.data, value.size, result.partner_native_hold);
        else if (Equals(key, "partner_input_edge_repeats")) {
            ok = ParseRange(value, kPartnerEdgeRepeatsMin, kPartnerEdgeRepeatsMax, result.partner_input_edge_repeats);
            if (ok) repeats_given = true;
        } else if (Equals(key, "play_rehearsal")) ok = ParseBool(value, result.play_rehearsal);
        else if (Equals(key, "net_steam_dispatch")) ok = ParseSteamDispatch(value.data, value.size, result.net_steam_dispatch);
        else if (Equals(key, "steam_selftest_fn")) ok = ParseBool(value, result.steam_selftest_fn);
        else if (Equals(key, "coop_hud")) ok = ParseCoopHud(value.data, value.size, result.coop_hud);
        else { ++result.unknown_keys; return; }
        if (!ok) ++result.invalid_values;
        else given |= ProfileKeyBit(key);
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
    // Steam is play only: the lab never uses Steam networking (its Steam
    // exercise is the self-test).
    if (!play && result.net_transport == NetTransport::steam) {
        result.net_transport = NetTransport::udp;
        result.net_transport_refused = true;
        ++result.invalid_values;
    }
    const bool steam = result.net_transport == NetTransport::steam;
    // The hidden worker's one-PC play rehearsal (UDP on 127.0.0.1): only with
    // the worker build's option; otherwise a play refusal (below) and never in the lab.
    const bool rehearsal = play && !steam && result.play_rehearsal && options.allow_play_rehearsal;
    if (!play && result.play_rehearsal) {
        result.play_rehearsal = false;
        ++result.invalid_values;
    }
    // A guest's host is never this account.
    if (result.net_peer_steam && result.net_peer_steam == result.net_steam_self) {
        result.net_peer_steam = 0;
        peer_steam_given = false;
        ++result.invalid_values;
    }
    // Steam ignores the UDP endpoint keys (play: refused with a bit below).
    const bool udp_keys_with_steam = steam && (peer_addr_given || bind_given || listen_given || listen_zero);
    if (steam) {
        peer_addr_given = false;
        peer_addr = Slice{nullptr, 0};
        bind_given = listen_given = listen_zero = peer_port_given = false;
        result.net_listen_port = result.net_peer_port = 0;
        result.net_bind_loopback = false;
        result.net_bind_address = 0;
        result.net_peer_address = 0x7F000001;
    }
    // net_peer_addr: a dotted IPv4 only (127/8 only in lab mode and the rehearsal).
    bool peer_addr_valid = false;
    if (peer_addr_given) {
        std::uint32_t address = 0;
        if (ParseIpv4(peer_addr.data, peer_addr.size, !play || rehearsal, address)) {
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
    // With steam no UDP default is applied.
    if (play && !steam) {
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
        // The rehearsal keeps its second instance's mutex suffix and hides the pads (xinput=none only).
        if (refuse(result.play_rehearsal && !rehearsal, kPlayRefusedRehearsal)) result.play_rehearsal = false;
        if (refuse(result.mutex_suffix[0] != 0 && !rehearsal, kPlayRefusedMutexSuffix)) result.mutex_suffix[0] = 0;
        if (refuse(result.xinput != kXInputPassthrough && !(rehearsal && result.xinput == kXInputNone), kPlayRefusedXInput))
            result.xinput = kXInputPassthrough;
        if (refuse(result.join_trigger, kPlayRefusedJoinTrigger)) result.join_trigger = false;
        if (refuse(result.auto_join_delay_s != 0, kPlayRefusedAutoJoin)) result.auto_join_delay_s = 0;
        if (refuse(result.role == LabRole::host && peer_addr_given, kPlayRefusedHostPeerAddr)) {
            peer_addr_valid = false;
            result.net_peer_address = 0x7F000001;
        }
        if (refuse(result.net_bind_loopback && !rehearsal, kPlayRefusedBindLoopback)) {
            result.net_bind_loopback = false;
            result.net_bind_address = 0;
        }
        if (refuse(result.net_link == NetLink::legacy, kPlayRefusedLegacyLink)) result.net_link = NetLink::v2;
        refuse(result.save_dir[0] == 0, kPlayRefusedNoSaveDir);
        if (refuse(!steam && result.role == LabRole::guest && !peer_addr_valid, kPlayRefusedGuestNoPeer)) result.net = false;
        std::uint32_t* const impairments[] = {&result.net_latency_ms, &result.net_loss_permille, &result.net_jitter_ms,
                                              &result.net_dup_permille, &result.net_reorder_permille};
        for (std::uint32_t* value : impairments)
            if (refuse(*value != 0 && !rehearsal, kPlayRefusedImpairment)) *value = 0;
        // World-sync probes are lab experiments; world itself is allowed in play.
        if (refuse(result.world_probe != WorldProbe::none, kPlayRefusedWorldProbe)) result.world_probe = WorldProbe::none;
        // The native debug soldier placement stays a lab experiment until qualified.
        if (refuse(result.debug_soldier_warp, kPlayRefusedDebugSoldierWarp)) result.debug_soldier_warp = false;
        // The native puppet: only through partner_native_profile (the qualified
        // stage). The raw drive and deferral keys and the lab-only native
        // experiments (lookcam, local=all, the P+0x218 window, noise=keep) stay refused.
        if (refuse(result.partner_drive != PartnerDrive::legacy || result.partner_input_defer != PartnerDefer::off ||
                       result.partner_native_aim == PartnerNativeAim::lookcam || result.partner_input_local == PartnerLocal::all ||
                       result.partner_input_p218_allow || result.partner_native_noise == PartnerNativeNoise::keep,
                   kPlayRefusedNativeDrive)) {
            result.partner_drive = PartnerDrive::legacy;
            result.partner_input_defer = PartnerDefer::off;
            if (result.partner_native_aim == PartnerNativeAim::lookcam) result.partner_native_aim = PartnerNativeAim::off;
            result.partner_input_local = PartnerLocal::main;
            result.partner_input_p218_allow = false;
            result.partner_native_noise = PartnerNativeNoise::filter;
            given &= ~(kProfileKeyDrive | kProfileKeyDefer | kProfileKeyLocal | kProfileKeyP218 | kProfileKeyNoise);
            if (result.partner_native_aim == PartnerNativeAim::off) given &= ~kProfileKeyAim;
        }
        // The profile needs the clip partner it falls back to (Play-Coop's play defaults).
        if (refuse(profile != PartnerNativeProfile::off &&
                       !(result.partner && result.partner_source == PartnerSource::net && result.partner_apply == PartnerApply::xform &&
                         result.partner_input == PartnerInput::off && result.partner_dm == PartnerDm::map &&
                         result.partner_anim == PartnerAnim::off),
                   kPlayRefusedNativeProfile))
            profile = PartnerNativeProfile::off;
        // Online defaults (keys not named keep them).
        if (!native_net_given) result.partner_native_net = PartnerNativeNet::online;
        if (!stall_given) result.partner_native_stall = PartnerNativeStall::hold;
        if (!repeats_given) result.partner_input_edge_repeats = kPlayEdgeRepeats;
        // Steam online.
        if (refuse(result.steam_selftest != 0, kPlayRefusedSteamSelftest)) result.steam_selftest = 0;
        if (refuse(steam && result.role == LabRole::host && peer_steam_given, kPlayRefusedHostPeerSteam)) result.net_peer_steam = 0;
        if (refuse(steam && result.role == LabRole::guest && !result.net_peer_steam && !result.net_steam_wait, kPlayRefusedNoPeerSteam))
            result.net = false;
        refuse(udp_keys_with_steam, kPlayRefusedUdpKeysWithSteam);
    }
    // The native profile (lab and play): the qualified stage's settings.
    ApplyNativeProfile(result, profile, given);
    result.partner_native_profile = profile;
    if (!steam) {
        // The Steam keys mean nothing on the UDP link.
        result.net_peer_steam = 0;
        result.net_steam_wait = false;
    } else if (result.net_peer_steam) {
        result.net_steam_wait = false;  // a configured host wins over waiting
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
    // 3. partner_net ports (none with steam: the transport has no port).
    if (result.net && !steam) {
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
    // 5. The weapons keys follow the native gun profile unless named (last:
    // after any expansion that sets partner_drive / partner_native_gun).
    ResolveWeaponDefaults(result);
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
        } else if (Equals(key, "name")) ok = CopyText(value, result.name, NameByte);
        else if (Equals(key, "nametag")) ok = ParseNametagSetting(value, result.nametag);
        else if (Equals(key, "friendly_crosshair")) ok = ParseNametagCrosshair(value.data, value.size, result.friendly_crosshair);
        else if (Equals(key, "hud")) ok = ParseCoopHud(value.data, value.size, result.hud);
        else { ++result.unknown_keys; return; }
        if (!ok) ++result.invalid_values;
    });
    if (!usable) return false;
    result.present = true;
    out = result;
    return true;
}

const char* NametagCrosshairName(NametagCrosshair value) noexcept {
    switch (value) {
    case NametagCrosshair::aim: return "aim";
    case NametagCrosshair::always: return "always";
    case NametagCrosshair::off: return "off";
    default: return "";
    }
}
bool ParseNametagCrosshair(const char* text, std::size_t size, NametagCrosshair& out) noexcept {
    const Slice s{text, text ? size : 0};
    if (Equals(s, "aim")) out = NametagCrosshair::aim;
    else if (Equals(s, "always")) out = NametagCrosshair::always;
    else if (Equals(s, "off") || Equals(s, "0")) out = NametagCrosshair::off;
    else return false;
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
const char* NetTransportName(NetTransport value) noexcept { return value == NetTransport::steam ? "steam" : "udp"; }

const char* SteamDispatchName(SteamDispatch value) noexcept {
    switch (value) {
    case SteamDispatch::fnptr: return "fnptr";
    case SteamDispatch::callback: return "callback";
    case SteamDispatch::poll: return "poll";
    default: return "both";
    }
}
bool ParseSteamDispatch(const char* text, std::size_t size, SteamDispatch& out) noexcept {
    const Slice s{text, text ? size : 0};
    if (Equals(s, "both")) out = SteamDispatch::both;
    else if (Equals(s, "fnptr")) out = SteamDispatch::fnptr;
    else if (Equals(s, "callback")) out = SteamDispatch::callback;
    else if (Equals(s, "poll")) out = SteamDispatch::poll;
    else return false;
    return true;
}
const char* CoopHudName(CoopHud value) noexcept {
    switch (value) {
    case CoopHud::compact: return "compact";
    case CoopHud::full: return "full";
    case CoopHud::off: return "off";
    default: return "";
    }
}
bool ParseCoopHud(const char* text, std::size_t size, CoopHud& out) noexcept {
    const Slice s{text, text ? size : 0};
    if (Equals(s, "compact")) out = CoopHud::compact;
    else if (Equals(s, "full")) out = CoopHud::full;
    else if (Equals(s, "off") || Equals(s, "0")) out = CoopHud::off;
    else return false;
    return true;
}
const char* SaveHandoffName(SaveHandoff value) noexcept {
    switch (value) {
    case SaveHandoff::stage: return "stage";
    case SaveHandoff::scene: return "scene";
    case SaveHandoff::always: return "always";
    default: return "off";
    }
}
bool ParseSaveHandoff(const char* text, std::size_t size, SaveHandoff& out) noexcept {
    const Slice s{text, text ? size : 0};
    if (Equals(s, "off")) out = SaveHandoff::off;
    else if (Equals(s, "stage")) out = SaveHandoff::stage;
    else if (Equals(s, "scene")) out = SaveHandoff::scene;
    else if (Equals(s, "always")) out = SaveHandoff::always;
    else return false;
    return true;
}
bool EffectiveTravelLock(const LabConfig& config) noexcept {
    if (config.travel_lock != kTravelLockAbsent) return config.travel_lock != 0;
    return config.mode == LabMode::play;
}
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
    case PartnerNativeAim::looksrc: return "looksrc";
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
    else if (Equals(s, "looksrc")) out = PartnerNativeAim::looksrc;
    else return false;
    return true;
}

const char* PartnerNativeFaceName(PartnerNativeFace value) noexcept {
    switch (value) {
    case PartnerNativeFace::off: return "off";
    case PartnerNativeFace::aim: return "aim";
    default: return "idle";
    }
}

bool ParsePartnerNativeFace(const char* text, std::size_t size, PartnerNativeFace& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeFace::off;
    else if (Equals(s, "idle")) out = PartnerNativeFace::idle;
    else if (Equals(s, "aim")) out = PartnerNativeFace::aim;
    else return false;
    return true;
}

const char* PartnerNativeNoiseName(PartnerNativeNoise value) noexcept {
    return value == PartnerNativeNoise::keep ? "keep" : "filter";
}

bool ParsePartnerNativeNoise(const char* text, std::size_t size, PartnerNativeNoise& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "filter")) out = PartnerNativeNoise::filter;
    else if (Equals(s, "keep")) out = PartnerNativeNoise::keep;
    else return false;
    return true;
}

const char* PartnerNativeProfileName(PartnerNativeProfile value) noexcept {
    switch (value) {
    case PartnerNativeProfile::loco: return "loco";
    case PartnerNativeProfile::gun: return "gun";
    default: return "off";
    }
}
const char* PartnerNativeNetName(PartnerNativeNet value) noexcept { return value == PartnerNativeNet::online ? "online" : "lab"; }
const char* PartnerNativeStallName(PartnerNativeStall value) noexcept { return value == PartnerNativeStall::hold ? "hold" : "neutral"; }
const char* PartnerNativeHoldName(PartnerNativeHold value) noexcept { return value == PartnerNativeHold::net ? "net" : "own"; }

bool ParsePartnerNativeProfile(const char* text, std::size_t size, PartnerNativeProfile& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeProfile::off;
    else if (Equals(s, "loco")) out = PartnerNativeProfile::loco;
    else if (Equals(s, "gun")) out = PartnerNativeProfile::gun;
    else return false;
    return true;
}
bool ParsePartnerNativeNet(const char* text, std::size_t size, PartnerNativeNet& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "lab")) out = PartnerNativeNet::lab;
    else if (Equals(s, "online")) out = PartnerNativeNet::online;
    else return false;
    return true;
}
bool ParsePartnerNativeStall(const char* text, std::size_t size, PartnerNativeStall& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "neutral")) out = PartnerNativeStall::neutral;
    else if (Equals(s, "hold")) out = PartnerNativeStall::hold;
    else return false;
    return true;
}
bool ParsePartnerNativeHold(const char* text, std::size_t size, PartnerNativeHold& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "own")) out = PartnerNativeHold::own;
    else if (Equals(s, "net")) out = PartnerNativeHold::net;
    else return false;
    return true;
}

const char* PartnerNativeAimPtName(PartnerNativeAimPt value) noexcept {
    switch (value) {
    case PartnerNativeAimPt::look: return "look";
    case PartnerNativeAimPt::point: return "point";
    default: return "off";
    }
}

bool ParsePartnerNativeAimPt(const char* text, std::size_t size, PartnerNativeAimPt& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeAimPt::off;
    else if (Equals(s, "look")) out = PartnerNativeAimPt::look;
    else if (Equals(s, "point")) out = PartnerNativeAimPt::point;
    else return false;
    return true;
}

// The worker's admitted native stages (tools/worker/WorkerSessionCommon.psm1
// Get-WorkerPartnerSettings; tools/worker/worker_game_gate.hpp NativeProfile).
void ApplyNativeProfile(LabConfig& c, PartnerNativeProfile profile, std::uint32_t keep) noexcept {
    if (profile == PartnerNativeProfile::off) return;
    const bool gun = profile == PartnerNativeProfile::gun;
    const auto set = [keep](std::uint32_t bit) noexcept { return (keep & bit) == 0; };
    if (set(kProfileKeyDrive)) c.partner_drive = PartnerDrive::native;
    if (set(kProfileKeyDefer)) c.partner_input_defer = PartnerDefer::on;
    if (set(kProfileKeySend)) c.partner_input_send = true;
    if (set(kProfileKeyHook)) c.partner_input_hook = true;
    if (set(kProfileKeyHold)) c.partner_input_hold = PartnerInputHold::own;
    if (set(kProfileKeyWords)) {
        c.partner_input_words[0] = kPartnerWordHeld;
        c.partner_input_words[1] = kPartnerWordPress;
        c.partner_input_words[2] = kPartnerWordRelease;
    }
    if (set(kProfileKeyWrap)) c.partner_input_wrap = PartnerWrap::window;
    if (set(kProfileKeyCam)) c.partner_input_cam = PartnerInputCam::sandbox;
    if (set(kProfileKeyLocal)) c.partner_input_local = PartnerLocal::main;
    if (set(kProfileKeyRecon)) c.partner_recon = PartnerRecon::track;
    if (set(kProfileKeySlot)) c.partner_input_slot = PartnerSlot::body;
    if (set(kProfileKeyRates)) c.partner_native_rates = PartnerNativeRates::mirror;
    if (set(kProfileKeyNoise)) c.partner_native_noise = PartnerNativeNoise::filter;
    if (set(kProfileKeyP218)) c.partner_input_p218_allow = false;
    if (set(kProfileKeyMask)) c.partner_input_mask = gun ? (kPartnerNativeBaseMask | kPartnerNativeGunBits | kPartnerNativeFireBit) : kPartnerNativeBaseMask;
    if (set(kProfileKeyAim)) c.partner_native_aim = gun ? PartnerNativeAim::look : PartnerNativeAim::off;
    if (set(kProfileKeyGun)) c.partner_native_gun = gun;
    if (set(kProfileKeyFire)) c.partner_native_fire = gun ? PartnerNativeFire::visual : PartnerNativeFire::off;
    if (set(kProfileKeyTriggers)) c.partner_input_triggers = gun;
    if (set(kProfileKeyRstick)) c.partner_input_rstick = gun;
    if (set(kProfileKeyAimPt)) c.partner_native_aimpt = gun ? PartnerNativeAimPt::point : PartnerNativeAimPt::off;
}

const char* PartnerNativeReloadName(PartnerNativeReload value) noexcept {
    switch (value) {
    case PartnerNativeReload::raw: return "raw";
    case PartnerNativeReload::event: return "event";
    default: return "mirror";
    }
}

bool ParsePartnerNativeReload(const char* text, std::size_t size, PartnerNativeReload& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "mirror")) out = PartnerNativeReload::mirror;
    else if (Equals(s, "raw")) out = PartnerNativeReload::raw;
    else if (Equals(s, "event")) out = PartnerNativeReload::event;
    else return false;
    return true;
}

const char* PartnerNativeSlotName(PartnerNativeSlot value) noexcept {
    switch (value) {
    case PartnerNativeSlot::write: return "write";
    case PartnerNativeSlot::call: return "call";
    default: return "off";
    }
}

bool ParsePartnerNativeSlot(const char* text, std::size_t size, PartnerNativeSlot& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeSlot::off;
    else if (Equals(s, "write")) out = PartnerNativeSlot::write;
    else if (Equals(s, "call")) out = PartnerNativeSlot::call;
    else return false;
    return true;
}

const char* PartnerNativeEquipName(PartnerNativeEquip value) noexcept {
    switch (value) {
    case PartnerNativeEquip::manifest: return "manifest";
    case PartnerNativeEquip::substitute: return "substitute";
    default: return "stock46";
    }
}

bool ParsePartnerNativeEquip(const char* text, std::size_t size, PartnerNativeEquip& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "stock46")) out = PartnerNativeEquip::stock46;
    else if (Equals(s, "manifest")) out = PartnerNativeEquip::manifest;
    else if (Equals(s, "substitute")) out = PartnerNativeEquip::substitute;
    else return false;
    return true;
}

void ResolveWeaponDefaults(LabConfig& c) noexcept {
    // The native gun profile (the worker gun stage, Play-OfflineCoop, play's
    // partner_native_profile=gun) runs the partner's own reload, the slot
    // follower and the generalized writer; anything else keeps the old defaults.
    const bool gun = c.partner_drive == PartnerDrive::native && c.partner_native_gun;
    // Raw offline/worker gun settings and profile expansion resolve identically.
    // Census/undriven and legacy configs remain off unless a key was named.
    if (!c.partner_native_aimpt_given)
        c.partner_native_aimpt = gun && c.partner_input_defer == PartnerDefer::on ? PartnerNativeAimPt::point : PartnerNativeAimPt::off;
    if (!c.partner_native_reload_given) c.partner_native_reload = gun ? PartnerNativeReload::event : PartnerNativeReload::mirror;
    if (!c.partner_native_slot_given) c.partner_native_slot = gun ? PartnerNativeSlot::write : PartnerNativeSlot::off;
    if (!c.partner_native_equip_given) c.partner_native_equip = gun ? PartnerNativeEquip::substitute : PartnerNativeEquip::stock46;
    // Weapons4: the shared tiers and the stand-in follow the gun profile; the
    // lab-only variants never run in play (prefer runs as on, measured as 0).
    if (!c.partner_native_equip_shared_given)
        c.partner_native_equip_shared = gun ? PartnerNativeEquipShared::on : PartnerNativeEquipShared::off;
    if (!c.partner_native_standin_given) c.partner_native_standin = gun ? PartnerNativeStandin::on : PartnerNativeStandin::off;
    // Anim round 1: the unsupported gate, the auto-fire stop, the state-driven
    // aim intent and the adaptive reload rate follow the gun profile (the rate
    // after hidden run 8ca5da4a converged 3018 -> 2035 ms against the remote's
    // 2020 ms); the census stays as named.
    if (!c.partner_native_unsupported_given) c.partner_native_unsupported = gun;
    if (!c.partner_native_reload_rate_given)
        c.partner_native_reload_rate = gun ? PartnerNativeReloadRate::adapt : PartnerNativeReloadRate::off;
    if (!c.partner_native_dry_stop_given) c.partner_native_dry_stop = gun;
    if (!c.partner_native_aim_intent_given)
        c.partner_native_aim_intent = gun ? PartnerNativeAimIntent::state : PartnerNativeAimIntent::pad;
    // Only a driven native stage gets the default. Census/off never owns the
    // body; explicit off/hide remains authoritative in either lab or play.
    if (!c.partner_native_ride_given)
        c.partner_native_ride = c.partner_drive == PartnerDrive::native && c.partner_input_defer == PartnerDefer::on
                                    ? kPartnerNativeRideDriven : PartnerNativeRide::off;
    if (c.mode == LabMode::play) {
        if (c.partner_native_equip_shared == PartnerNativeEquipShared::prefer) c.partner_native_equip_shared = PartnerNativeEquipShared::on;
        c.partner_native_equip_measured = false;
        // The fire census is a lab measurement (it swaps a forwarding census slot).
        c.partner_fire_census = false;
    }
}

const char* PartnerNativeEquipSharedName(PartnerNativeEquipShared value) noexcept {
    switch (value) {
    case PartnerNativeEquipShared::on: return "on";
    case PartnerNativeEquipShared::prefer: return "prefer";
    default: return "off";
    }
}

bool ParsePartnerNativeEquipShared(const char* text, std::size_t size, PartnerNativeEquipShared& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeEquipShared::off;
    else if (Equals(s, "on")) out = PartnerNativeEquipShared::on;
    else if (Equals(s, "prefer")) out = PartnerNativeEquipShared::prefer;
    else return false;
    return true;
}

const char* PartnerNativeStandinName(PartnerNativeStandin value) noexcept {
    return value == PartnerNativeStandin::on ? "on" : "off";
}

bool ParsePartnerNativeStandin(const char* text, std::size_t size, PartnerNativeStandin& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeStandin::off;
    else if (Equals(s, "on")) out = PartnerNativeStandin::on;
    else return false;
    return true;
}

const char* PartnerNativeAimIntentName(PartnerNativeAimIntent value) noexcept {
    return value == PartnerNativeAimIntent::state ? "state" : "pad";
}

bool ParsePartnerNativeAimIntent(const char* text, std::size_t size, PartnerNativeAimIntent& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "pad")) out = PartnerNativeAimIntent::pad;
    else if (Equals(s, "state")) out = PartnerNativeAimIntent::state;
    else return false;
    return true;
}

const char* PartnerNativeReloadRateName(PartnerNativeReloadRate value) noexcept {
    return value == PartnerNativeReloadRate::adapt ? "adapt" : "off";
}

bool ParsePartnerNativeReloadRate(const char* text, std::size_t size, PartnerNativeReloadRate& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeReloadRate::off;
    else if (Equals(s, "adapt")) out = PartnerNativeReloadRate::adapt;
    else return false;
    return true;
}

const char* PartnerNativeRideName(PartnerNativeRide value) noexcept {
    return value == PartnerNativeRide::hide ? "hide" : value == PartnerNativeRide::seat ? "seat" : "off";
}

bool ParsePartnerNativeRide(const char* text, std::size_t size, PartnerNativeRide& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeRide::off;
    else if (Equals(s, "hide")) out = PartnerNativeRide::hide;
    else if (Equals(s, "seat")) out = PartnerNativeRide::seat;
    else return false;
    return true;
}

const char* PartnerRideSeatEnterName(PartnerRideSeatEnter value) noexcept {
    return value == PartnerRideSeatEnter::getin ? "getin" : "direct";
}

bool ParsePartnerRideSeatEnter(const char* text, std::size_t size, PartnerRideSeatEnter& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "direct")) out = PartnerRideSeatEnter::direct;
    else if (Equals(s, "getin")) out = PartnerRideSeatEnter::getin;
    else return false;
    return true;
}

const char* PartnerRideSeatExitName(PartnerRideSeatExit value) noexcept {
    return value == PartnerRideSeatExit::getoff ? "getoff" : "stop";
}

bool ParsePartnerRideSeatExit(const char* text, std::size_t size, PartnerRideSeatExit& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "stop")) out = PartnerRideSeatExit::stop;
    else if (Equals(s, "getoff")) out = PartnerRideSeatExit::getoff;
    else return false;
    return true;
}

const char* PartnerNativeOverlayName(PartnerNativeOverlay value) noexcept {
    switch (value) {
    case PartnerNativeOverlay::debug: return "debug";
    case PartnerNativeOverlay::map: return "map";
    default: return "off";
    }
}

bool ParsePartnerNativeOverlay(const char* text, std::size_t size, PartnerNativeOverlay& out) noexcept {
    if (!text && size) return false;
    const Slice s{text, size};
    if (Equals(s, "off")) out = PartnerNativeOverlay::off;
    else if (Equals(s, "debug")) out = PartnerNativeOverlay::debug;
    else if (Equals(s, "map")) out = PartnerNativeOverlay::map;
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
    LabParseOptions options{};
#if defined(MGS_PLAY_REHEARSAL) && MGS_PLAY_REHEARSAL
    // Background-worker builds only (CMake MGS_BACKGROUND_WORKER): the hidden
    // one-PC play rehearsal. Friend and release builds never define it.
    options.allow_play_rehearsal = true;
#endif
    if (size >= 0) ParseLabConfig(file_buffer, static_cast<std::size_t>(size), lab_config, options);
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
