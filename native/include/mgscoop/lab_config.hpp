#pragma once
// Plain-text configuration shared by the loader components.
//
// <exeDir>/mgs-coop/lab.ini  - development harness only (single-PC host+guest).
//                              Ignored unless it exists and contains enabled=1.
// <exeDir>/mgs-coop/coop.ini - player-facing options (master server, character).
//
// Format: one key=value per line, '#' or ';' comments, surrounding blanks
// trimmed, unknown keys ignored (counted), invalid values rejected (counted)
// and left at their defaults. Parsers are pure and portable so they can be
// unit tested; the loaders read the files once and are safe to call from
// DllMain (kernel32 file I/O only, no heap, no LoadLibrary).
#include <cstddef>
#include <cstdint>

namespace mgscoop {

enum class LabRole : std::uint8_t { none, host, guest };

// XInput routing for the lab: pass through unchanged, hide all pads, or expose
// only one physical pad index (0..3) as pad 0 to this instance.
constexpr int kXInputPassthrough = -2;
constexpr int kXInputNone = -1;

// Offline partner spike (docs/offline-partner-spike-plan.md 2.1). Every key
// below is inert unless the offline-v1 marker is active and partner=1.
enum class PartnerSource : std::uint8_t { fixed, orbit, net };
// Movement driver (lab.ini partner_apply or its alias partner_move; trigger
// apply= or move=). warp: Lua TppPlayer2 Warp every tick (proven, resets the
// body each call). slot: native S+0x20 write only. both: slot + Warp. xform:
// native per-tick body transform through Warp's own T.vt+0x70/+0x68 calls
// without Warp's resets (0x9CEBD4..0x9CEC32), Lua Warp only as a fallback.
// hybrid: xform, plus a full Lua Warp on first apply, large error or relink.
enum class PartnerApply : std::uint8_t { warp, slot, both, xform, hybrid };
// Animation source for instance 1: off, mirror (the local slot 0, read
// natively; no network) or net (the anim extension of received frames).
enum class PartnerAnim : std::uint8_t { off, mirror, net };
// How the animation fields are applied: q = Q arrays slot 1 every tick,
// s = PS.vt+0 SetStance (0xFF5B10) on change, qs = both, r = the stance
// request component (PS.vt+0xC8, X.vt+0x10) on change.
enum class PartnerAset : std::uint8_t { q, s, qs, r };
// Warp's resets replayed after an xform: none, A0 (*(blk+0xA0).vt+0x80),
// C0 (*(blk+0xC0).vt+0x90) or both (C0 first, as Warp does).
enum class PartnerReset : std::uint8_t { none, a, c, ac };
// Instance 1 appearance: copy (CopyPartsInfoFromLocalPlayer, the proven
// default), net (the partner's own look from its look frames; copy until the
// first one arrives), quiet / snake (fixed local test looks).
enum class PartnerLook : std::uint8_t { copy, net, quiet, snake };
enum class PartnerTeardown : std::uint8_t { clear, keep };
// Default partner_net ports when net=1 names neither port: host 47820/47821,
// guest 47821/47820 (never the loopback emulator's 47810/47811).
constexpr std::uint16_t kNetHostPort = 47820;
constexpr std::uint16_t kNetGuestPort = 47821;

struct LabConfig {
    bool present = false;            // file read and enabled=1
    LabRole role = LabRole::none;
    bool loopback = false;           // emulate ISteamNetworking P2P over 127.0.0.1 UDP
    std::uint64_t self_steamid = 0;  // 0 = keep the real Steam identity
    char self_name[64]{};            // persona name override; empty = real
    std::uint64_t peer_steamid = 0;  // 0 = learn from the first HELLO
    std::uint16_t listen_port = 0;   // loopback UDP port of this instance
    std::uint16_t peer_port = 0;     // loopback UDP port of the other instance
    char mutex_suffix[32]{};         // appended to the single-instance mutex name
    char save_dir[260]{};            // absolute UTF-8 path; empty = no redirect
    int xinput = kXInputPassthrough;
    bool join_trigger = false;       // accept <exeDir>/mgs-coop/join.trigger as the join key
    std::uint32_t auto_join_delay_s = 0;  // guest: trigger join N s after armed (0 = off)
    std::uint32_t net_latency_ms = 0;     // loopback fault injection
    std::uint32_t net_loss_permille = 0;  // 0..1000
    bool trace_packets = false;
    // Offline partner spike. net: our own second UDP transport (partner_net).
    // Its two ports must differ from each other and from listen_port/peer_port;
    // otherwise net is forced to false and invalid_values is incremented.
    bool net = false;
    std::uint16_t net_listen_port = 0;
    std::uint16_t net_peer_port = 0;
    bool partner = false;
    std::uint32_t partner_stage = 0;        // 0..3
    PartnerSource partner_source = PartnerSource::fixed;
    PartnerApply partner_apply = PartnerApply::warp;  // partner_apply or partner_move
    PartnerAnim partner_anim = PartnerAnim::off;
    PartnerAset partner_aset = PartnerAset::qs;
    PartnerReset partner_reset = PartnerReset::none;  // n|a|c|ac
    PartnerLook partner_look = PartnerLook::copy;
    std::uint32_t partner_rate_hz = 30;     // 1..60
    std::uint32_t partner_interp_ms = 100;  // 0..500 (0 = latest snapshot only)
    std::uint32_t partner_offset_cm = 300;  // 50..1000
    PartnerTeardown partner_teardown = PartnerTeardown::clear;
    bool partner_force_exists = false;
    bool partner_policy_relax = false;      // last resort
    bool partner_anim_probe = false;
    bool crash_probe = true;
    unsigned unknown_keys = 0;
    unsigned invalid_values = 0;
};

// Spellings shared by lab.ini, partner.trigger and the log lines.
const char* PartnerSourceName(PartnerSource value) noexcept;
const char* PartnerApplyName(PartnerApply value) noexcept;
const char* PartnerTeardownName(PartnerTeardown value) noexcept;
const char* PartnerAnimName(PartnerAnim value) noexcept;
const char* PartnerAsetName(PartnerAset value) noexcept;
const char* PartnerResetName(PartnerReset value) noexcept;  // n|a|c|ac
const char* PartnerLookName(PartnerLook value) noexcept;
bool ParsePartnerSource(const char* text, std::size_t size, PartnerSource& out) noexcept;
bool ParsePartnerApply(const char* text, std::size_t size, PartnerApply& out) noexcept;
bool ParsePartnerAnim(const char* text, std::size_t size, PartnerAnim& out) noexcept;
bool ParsePartnerAset(const char* text, std::size_t size, PartnerAset& out) noexcept;
bool ParsePartnerReset(const char* text, std::size_t size, PartnerReset& out) noexcept;
bool ParsePartnerLook(const char* text, std::size_t size, PartnerLook& out) noexcept;
// True for xform and hybrid (the native body-transform drivers).
inline bool PartnerApplyIsTransform(PartnerApply value) noexcept {
    return value == PartnerApply::xform || value == PartnerApply::hybrid;
}

enum class PlayerCharacter : std::uint8_t {
    game_default,  // leave the sortie/save selection untouched
    snake, dd_male, dd_female, avatar, ocelot, quiet
};

struct CoopConfig {
    bool present = false;
    char master_url[70]{};  // empty = leave the game's URL; else http(s)://.../tppstm/gate, <= 69 chars
    PlayerCharacter character = PlayerCharacter::game_default;
    // Used where the chosen character conflicts with the mission (e.g. story Quiet).
    PlayerCharacter character_fallback = PlayerCharacter::dd_male;
    unsigned unknown_keys = 0;
    unsigned invalid_values = 0;
};

// Pure parsers. `text` need not be NUL-terminated. Return false only when the
// input is unusable (null with nonzero length, or longer than 64 KiB).
bool ParseLabConfig(const char* text, std::size_t length, LabConfig& out) noexcept;
bool ParseCoopConfig(const char* text, std::size_t length, CoopConfig& out) noexcept;

// Lua/log spelling of a character ("quiet", "dd_male", ...; "" for game_default).
const char* PlayerCharacterName(PlayerCharacter value) noexcept;
// PlayerType enum value for the 1.0.15.4 binding (SNAKE 0, DD_MALE 1, DD_FEMALE 2,
// AVATAR 3, OCELOT 5, QUIET 6); -1 for game_default.
int PlayerCharacterType(PlayerCharacter value) noexcept;

#ifdef _WIN32
// Read once from <directory of the process executable>\mgs-coop\{lab,coop}.ini.
// A missing file yields a default (present=false) configuration.
const LabConfig& GetLabConfig() noexcept;
const CoopConfig& GetCoopConfig() noexcept;
#endif

} // namespace mgscoop
