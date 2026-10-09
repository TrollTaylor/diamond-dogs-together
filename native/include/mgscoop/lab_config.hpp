#pragma once
// Plain-text configuration shared by the loader components.
//
// <exeDir>/mgs-coop/lab.ini  - development harness (single-PC host+guest), or
//                              mode=play for two-PC play (Play-Coop writes it;
//                              docs/link-protocol.md). Ignored unless it exists
//                              and contains enabled=1.
// <exeDir>/mgs-coop/coop.ini - player-facing options (master server, character).
//
// Format: one key=value per line, '#' or ';' comments, surrounding blanks
// trimmed, unknown keys ignored (counted), invalid values rejected (counted)
// and left at their defaults. Parsers are pure and portable so they can be
// unit tested; the loaders read the files once and are safe to call from
// DllMain (kernel32 file I/O only, no heap, no LoadLibrary).
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_pad_bits.hpp"

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
// input: instance 1 is driven by its own virtual pad inside the block-1
// context window (docs/partner-anim-appearance.md "Round 3"): one Lua Warp to
// place it (and on relink), a rare xform snap when the body drifts further
// than partner_input_correct_mm, otherwise the game's own locomotion moves it.
enum class PartnerApply : std::uint8_t { warp, slot, both, xform, hybrid, input };
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
// Round 3 input lever (block-1 context window at 0x9C2B51): what instance 1's
// virtual pad carries. off: no window at all; pass: the hook runs and counts
// windows but swaps nothing; neutral: an idle pad plus the isLocal bit;
// stick: a constant stick (partner_input_stick); net: the partner's own pad
// from its input frames.
enum class PartnerInput : std::uint8_t { off, pass, neutral, stick, net };
// Camera POD fields inside the window: untouched, compared (dirty bits
// logged) or compared and restored.
enum class PartnerInputCam : std::uint8_t { off, detect, sandbox };
// Hold classifiers PP vt+0x90 (0x1205C30) and vt+0x140 (0x1205DF0) inside the
// window (round 4): none (both return 0, no prone), pass (vt+0x90 stays the
// game's: diagnostic only, it reads the HOST's src history at *(src+8)), own
// (an exact replica of src->vt+0x10 = 0x434ED0 over a history of what
// instance 1 was fed, one entry per window; the default) or net (the sender's
// 12-frame hold/tap words for frames == 12, own otherwise).
enum class PartnerInputHold : std::uint8_t { none, pass, own, net };
// Camera-relative stick rotation sign: +1, -1, or no rotation.
enum class PartnerInputYaw : std::uint8_t { rot, rotneg, none };
// Direct-motion lever: off, probe (census and the 2 Hz line), on (also the
// 4-byte comparator patch at 0x1198E25), map (on, plus the clip follows the
// remote stance/speed through the 3x4 table below; manual dmplay/dmstop refused).
enum class PartnerDm : std::uint8_t { off, probe, on, map };
// dm=map: highest gani index of partner_sync.lua GANI, the 3x4 [stance][move]
// table (stand none/walk/run/dash, squat ..., crawl ...) and the speed bands.
inline constexpr std::uint8_t kPartnerDmGaniMax = 30;
inline constexpr std::size_t kPartnerDmCells = 12;
struct PartnerDmTable { std::uint8_t cells[kPartnerDmCells]; };
inline constexpr PartnerDmTable kPartnerDmTableDefault{{0, 10, 11, 5, 1, 3, 4, 4, 7, 6, 6, 6}};
// Horizontal speed bands (mm/s): < idle none, < run walk, < dash run, else dash.
struct PartnerDmSpeeds { std::uint32_t idle_mmps, run_mmps, dash_mmps; };
inline constexpr PartnerDmSpeeds kPartnerDmSpeedsDefault{300, 2600, 5600};
// Round 4 reconciliation of an input-driven body (apply=input,
// docs/partner-anim-appearance.md "Round 4"): off (placement only, then pure
// locomotion: a diagnostic), track (placement, gentle position nudges, rare
// snaps; the default) or pin (the body is xform-pinned to the remote pose every
// tick while the window still drives its animation).
enum class PartnerRecon : std::uint8_t { off, track, pin };
// What the S+0x20[1] slot carries with apply=input: the remote pose (the old
// behaviour), the body readback (what the game publishes for a local player;
// the default) or nothing.
enum class PartnerSlot : std::uint8_t { remote, body, none };
// A body that keeps moving while the remote stands still (the stuck-moving
// state seen live): pin it to the remote (default), disarm the window, or only
// log it (off, diagnostic).
enum class PartnerRunaway : std::uint8_t { pin, disarm, off };
// Other walks of block 1's update tree (UpdateTree service slots +0x28/+0x30/
// +0x40, the phase-7 call 0x9C1DA3, the phase-11 call 0x9C1AFF): off (not
// hooked), census (pass-through wrappers that count calls per phase, thread
// and window nesting) or window (the same instance-1 pad window around block
// 1's calls on the hook thread; needs the census first).
enum class PartnerWrap : std::uint8_t { off, census, window };
// Native puppet (docs/native-puppet.md). partner_drive=native enables the
// arbiter that switches between the game's own locomotion/action/gun systems
// driven by the remote pad (block 1's per-block job deferred onto the main
// thread inside the input window) and the proven dm=map + xform fallback.
// legacy (default, with partner_input_defer=off) installs nothing new.
enum class PartnerDrive : std::uint8_t { legacy, native };
// The job deferral hooks H1/H2: off (not installed), census (installed as
// pass-through counters; the arbiter cannot activate) or on (deferral allowed;
// the arbiter still decides). Ignored unless partner_drive=native.
enum class PartnerDefer : std::uint8_t { off, census, on };
// isLocal (base plugin rec+0x2AC bit 0) in native windows: the main window
// only (round-3 behaviour) or also the drains and walk windows.
enum class PartnerLocal : std::uint8_t { main, all };
// Native aim: off; look (the remote look angles written into instance 1's PS
// slot after phase 0 inside a drain); lookcam (also CAM+0x270/+0x274 inside
// block-1 windows, sandboxed).
enum class PartnerNativeAim : std::uint8_t { off, look, lookcam };
// Instance 1's per-id movement rates (base action record of *(PS1+0x260):
// dash +0x48/+0x50, crawl +0x3C/+0x40/+0x44; docs/native-puppet.md "Rates"):
// off (not read), log (read both instances and the remote's, telemetry only)
// or mirror (also write the remote's rates into instance 1's own record inside
// the drain window, restored on leave). Inert unless partner_drive=native.
enum class PartnerNativeRates : std::uint8_t { off, log, mirror };
// Native fire (docs/native-puppet.md "Weapons"): off (FIRE masked) or visual
// (FIRE reaches instance 1 only while the RequestBullet owner filter is
// installed, live and armed: the partner's shots are animation, sound and
// effects on this machine, never bullets). lab.ini/trigger spellings: 0|off,
// 1|visual. There is no mode with real partner bullets.
enum class PartnerNativeFire : std::uint8_t { off, visual };
// Native puppet ranges and the effective-mask base (partner_pad_bits.hpp:
// STANCE 2, DASH 4, EVADE 0x40000, keyboard dash bit 21, keyboard walk bit
// 25); HOLD 0x8 + RELOAD 0x40 with partner_native_gun, FIRE 0x10 with
// partner_native_gun and partner_native_fire=visual (and only while the fire
// filter is live). ACTION 0x20 (ride, knife kill, pickup) never.
inline constexpr std::uint32_t kPartnerNativeSteerMaxDeg = 30;
inline constexpr std::uint32_t kPartnerNativeTrimMaxPct = 10;
inline constexpr std::uint32_t kPartnerNativeBaseMask = partner_pad::kNativeBase;
inline constexpr std::uint32_t kPartnerNativeGunBits = partner_pad::kNativeGun;
inline constexpr std::uint32_t kPartnerNativeFireBit = partner_pad::kFire;
static_assert(kPartnerNativeBaseMask == 0x2240006u && kPartnerNativeGunBits == 0x48u && kPartnerNativeFireBit == 0x10u, "native masks");
static_assert((kPartnerNativeBaseMask & ~partner_pad::kMovementOnlyBits) == 0u, "the base passes movement-only bits");
static_assert(((kPartnerNativeGunBits | kPartnerNativeFireBit) & partner_pad::kMovementOnlyBits) == 0u, "gun/fire bits disjoint from movement");
// Sources of the three pad edge words src+0x4C/+0x50/+0x54 inside the window
// (partner_input::Word): '0' zero, 'p' press, 'r' release, 'h' held, 'n' the
// partner's own word (once per received frame).
inline constexpr std::uint8_t kPartnerWordZero = 0, kPartnerWordPress = 1, kPartnerWordRelease = 2, kPartnerWordHeld = 3,
                              kPartnerWordNet = 4;
// Default partner_net ports when net=1 names neither port: host 47820/47821,
// guest 47821/47820 (never the loopback emulator's 47810/47811).
constexpr std::uint16_t kNetHostPort = 47820;
constexpr std::uint16_t kNetGuestPort = 47821;
// Two-PC play: the host's UDP port (the guest binds an ephemeral port).
inline constexpr std::uint16_t kNetPlayPort = 47830;

// lab.ini mode: lab (the one-PC harness, default) or play (two PCs, real
// identity). Play refuses every lab identity hack and impairment key; each
// refused key is forced off, sets its bit in play_refused and counts one
// invalid value.
enum class LabMode : std::uint8_t { lab, play };
// Partner link wire: v2 (authenticated link protocol, default) or legacy (the
// raw lab frames; lab only, the bisect escape hatch).
enum class NetLink : std::uint8_t { v2, legacy };
enum class PartnerInterp : std::uint8_t { fixed, auto_ };
enum class PartnerLost : std::uint8_t { freeze, hide };
enum class NetLossModel : std::uint8_t { even, burst };
inline constexpr std::uint32_t kPlayRefusedLoopback = 0x01;
inline constexpr std::uint32_t kPlayRefusedSelfSteamId = 0x02;
inline constexpr std::uint32_t kPlayRefusedPeerSteamId = 0x04;
inline constexpr std::uint32_t kPlayRefusedSelfName = 0x08;
inline constexpr std::uint32_t kPlayRefusedMutexSuffix = 0x10;
inline constexpr std::uint32_t kPlayRefusedXInput = 0x20;
inline constexpr std::uint32_t kPlayRefusedJoinTrigger = 0x40;
inline constexpr std::uint32_t kPlayRefusedAutoJoin = 0x80;
inline constexpr std::uint32_t kPlayRefusedHostPeerAddr = 0x100;
inline constexpr std::uint32_t kPlayRefusedBindLoopback = 0x200;
inline constexpr std::uint32_t kPlayRefusedLegacyLink = 0x400;
inline constexpr std::uint32_t kPlayRefusedPeerAddr = 0x800;
inline constexpr std::uint32_t kPlayRefusedNoSaveDir = 0x1000;
inline constexpr std::uint32_t kPlayRefusedGuestNoPeer = 0x2000;
inline constexpr std::uint32_t kPlayRefusedImpairment = 0x4000;
inline constexpr std::uint32_t kPlayRefusedWorldProbe = 0x8000;
inline constexpr std::uint32_t kPlayRefusedDebugSoldierWarp = 0x10000;
// partner_drive=native / partner_input_defer != off are lab-only until qualified.
inline constexpr std::uint32_t kPlayRefusedNativeDrive = 0x20000;
// Steam online (docs/steam-online.md 9.1): steam_selftest is lab only;
// net_peer_steam on a host is ignored; a steam guest needs net_peer_steam or
// net_steam_wait=1 (else net=0, link=disabled; reason=no_peer_steam); the UDP
// endpoint keys (net_peer_addr, net_listen_port, net_bind) are ignored with steam.
inline constexpr std::uint32_t kPlayRefusedSteamSelftest = 0x40000;
inline constexpr std::uint32_t kPlayRefusedHostPeerSteam = 0x80000;
inline constexpr std::uint32_t kPlayRefusedNoPeerSteam = 0x100000;
inline constexpr std::uint32_t kPlayRefusedUdpKeysWithSteam = 0x200000;
// Partner link transport: udp (our own UDP socket; the one-PC lab and the
// LAN/VPN code) or steam (ISteamNetworkingMessages002 over Steam's relays;
// play only, the lab refuses it and keeps udp).
enum class NetTransport : std::uint8_t { udp, steam };
// World sync lab probes (docs/world-sync-protocol.md "Probes"): lab.ini
// world_probe and the world.trigger key. The ids are the wire probeId of
// PROBE_SYNC and the Lua probe number, in this order.
enum class WorldProbe : std::uint8_t {
    none, p1, p3, p3b, p4, p12, p17, race_kill, remote_area_kill, kill_near, sleep_near, fulton_near, alert_near,
    weather_rain, clock_jump, clock_fast, shared_death, host_continue, shared_checkpoint
};
inline constexpr std::uint8_t kWorldProbeMax = 18;
inline constexpr std::uint32_t kWorldLevelMax = 3;

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
    // Round 3 (docs/partner-anim-appearance.md "Round 3"; all inert unless
    // offline-v1 and partner=1). Defaults keep the proven behaviour, except
    // the look fix (look gate and stall recovery), which is on.
    PartnerInput partner_input = PartnerInput::off;
    PartnerInputCam partner_input_cam = PartnerInputCam::sandbox;
    PartnerInputHold partner_input_hold = PartnerInputHold::own;
    PartnerInputYaw partner_input_yaw = PartnerInputYaw::rot;
    // Round 4: "hpr" (w4c held, w50 press edge, w54 release edge; the stance
    // toggle fires on w50 or w54 bit 1, 0x10CE317..0x10CE344).
    std::uint8_t partner_input_words[3] = {kPartnerWordHeld, kPartnerWordPress, kPartnerWordRelease};
    // STANCE 0x2 | DASH 0x4 | EVADE 0x40000 | keyboard dash 0x200000 | keyboard walk 0x2000000 (binding 0x19).
    std::uint32_t partner_input_mask = 0x2240006;
    bool partner_input_rstick = false;
    bool partner_input_triggers = false;
    std::uint32_t partner_input_stick_deg = 180;  // 0..359
    std::uint32_t partner_input_stick_pct = 100;  // 0..100
    // Round 4: the hard xform snap threshold while moving (0 = recon off).
    std::uint32_t partner_input_correct_mm = 3000;  // 0..10000
    std::uint32_t partner_input_stale_ms = 250;     // 50..2000
    bool partner_input_send = false;
    bool partner_input_hook = true;               // 0 forbids installing the 0x9C2B51 hook
    bool partner_input_p218_allow = false;        // the diagnostic P+0x218 window (I3x)
    bool partner_input_holdlog = false;
    bool partner_pad_probe = false;
    bool partner_look_gate = true;
    bool partner_look_recover = true;
    PartnerDm partner_dm = PartnerDm::off;
    PartnerDmTable partner_dm_map = kPartnerDmTableDefault;
    PartnerDmSpeeds partner_dm_speed = kPartnerDmSpeedsDefault;
    // Round 4 (docs/partner-anim-appearance.md "Round 4").
    PartnerRecon partner_recon = PartnerRecon::track;
    std::int32_t partner_recon_align_ms = 0;        // -50..150: target delay bias
    std::uint32_t partner_recon_nudge_mmps = 600;   // 100..3000: nudge cap while moving
    PartnerSlot partner_input_slot = PartnerSlot::body;
    PartnerRunaway partner_recon_runaway = PartnerRunaway::pin;
    PartnerWrap partner_input_wrap = PartnerWrap::off;
    // Receiver facing rate limit for apply=xform/hybrid on the net source
    // (partner_facing.hpp): degrees per second, 0 = exact remote yaw.
    std::uint32_t partner_facing_dps = 900;         // 0..1440
    // Native puppet (docs/native-puppet.md); every default keeps today's behaviour.
    PartnerDrive partner_drive = PartnerDrive::legacy;
    PartnerDefer partner_input_defer = PartnerDefer::off;
    PartnerLocal partner_input_local = PartnerLocal::main;
    PartnerNativeAim partner_native_aim = PartnerNativeAim::off;
    bool partner_native_gun = false;                // HOLD 0x8 + RELOAD 0x40 allowed through the mask
    PartnerNativeFire partner_native_fire = PartnerNativeFire::off;  // FIRE 0x10 (only with partner_native_gun, visual only)
    std::uint32_t partner_native_steer_deg = 15;    // 0..30 (0 disables steering)
    std::uint32_t partner_native_trim_pct = 0;      // 0..10
    PartnerNativeRates partner_native_rates = PartnerNativeRates::log;
    bool crash_probe = true;
    // Two-PC link (docs/link-protocol.md). `mode` is parsed independently of
    // `present`; the play save gate is lab::SavesRedirected() in the observer.
    LabMode mode = LabMode::lab;
    std::uint32_t play_refused = 0;                 // kPlayRefused* bits
    NetLink net_link = NetLink::v2;
    bool net_bind_loopback = true;                  // net_bind=loopback (lab default)
    std::uint32_t net_bind_address = 0x7F000001;    // host byte order; 0 = any
    std::uint32_t net_peer_address = 0x7F000001;    // host byte order (net_peer_addr, dotted IPv4)
    std::uint32_t net_jitter_ms = 0, net_dup_permille = 0, net_reorder_permille = 0, net_reorder_ms = 40;
    NetLossModel net_loss_model = NetLossModel::even;
    std::uint32_t net_burst = 2, net_seed = 1;
    PartnerInterp partner_interp = PartnerInterp::fixed;
    std::uint32_t partner_interp_min_ms = 60, partner_interp_max_ms = 350;
    PartnerLost partner_lost = PartnerLost::freeze;
    // World sync (docs/world-sync-protocol.md): 0 = inert (today's behaviour),
    // 1 plumbing, 2 enemies and alert, 3 clock and weather. world_probe is lab
    // only (play forces none and sets kPlayRefusedWorldProbe).
    std::uint32_t world = 0;
    WorldProbe world_probe = WorldProbe::none;
    // Debug "Spawn enemy here" native placement (native/soldier_warp.cpp): the
    // engine set-transform message to one soldier. Lab qualification only; play
    // forces it off (kPlayRefusedDebugSoldierWarp).
    bool debug_soldier_warp = false;
    // Steam online (docs/steam-online.md 9.1). net_transport=steam is play
    // only: in the lab it is forced back to udp (+1 invalid,
    // net_transport_refused). net_steam_self is the account Play-Coop saw
    // (checked against GetSteamID at gate 5); net_peer_steam the guest's host;
    // net_steam_wait=1 a guest waiting for an invite or a pasted code.
    NetTransport net_transport = NetTransport::udp;
    bool net_transport_refused = false;
    std::uint64_t net_steam_self = 0;
    std::uint64_t net_peer_steam = 0;
    bool net_steam_wait = false;
    bool net_steam_accept = true;
    // Lab only: the hidden-worker Steam self-test (1 = in-process pair, 2 adds
    // the network-loopback pair). Play forces 0 (kPlayRefusedSteamSelftest).
    std::uint32_t steam_selftest = 0;
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
const char* PartnerInputName(PartnerInput value) noexcept;
const char* PartnerInputCamName(PartnerInputCam value) noexcept;
const char* PartnerInputHoldName(PartnerInputHold value) noexcept;
const char* PartnerInputYawName(PartnerInputYaw value) noexcept;
const char* PartnerDmName(PartnerDm value) noexcept;
const char* PartnerReconName(PartnerRecon value) noexcept;
const char* PartnerSlotName(PartnerSlot value) noexcept;
const char* PartnerRunawayName(PartnerRunaway value) noexcept;
const char* PartnerWrapName(PartnerWrap value) noexcept;
const char* PartnerDriveName(PartnerDrive value) noexcept;
const char* PartnerDeferName(PartnerDefer value) noexcept;
const char* PartnerLocalName(PartnerLocal value) noexcept;
const char* PartnerNativeAimName(PartnerNativeAim value) noexcept;
const char* PartnerNativeRatesName(PartnerNativeRates value) noexcept;
const char* PartnerNativeFireName(PartnerNativeFire value) noexcept;
const char* LabModeName(LabMode value) noexcept;
const char* NetLinkName(NetLink value) noexcept;
const char* NetTransportName(NetTransport value) noexcept;
const char* PartnerInterpName(PartnerInterp value) noexcept;
const char* PartnerLostName(PartnerLost value) noexcept;
const char* NetLossModelName(NetLossModel value) noexcept;
// The lab.ini / trigger spelling; "?" when out of range.
const char* WorldProbeName(WorldProbe value) noexcept;
bool ParseWorldProbe(const char* text, std::size_t size, WorldProbe& out) noexcept;
// Dotted IPv4 "a.b.c.d": four decimal octets 0..255 without leading zeros
// (host byte order out). Rejects 0.0.0.0, 255.255.255.255 and 224.0.0.0/4,
// and 127/8 unless allow_loopback. No names, no IPv6.
bool ParseIpv4(const char* text, std::size_t size, bool allow_loopback, std::uint32_t& out) noexcept;
bool ParsePartnerRecon(const char* text, std::size_t size, PartnerRecon& out) noexcept;
bool ParsePartnerSlot(const char* text, std::size_t size, PartnerSlot& out) noexcept;
bool ParsePartnerRunaway(const char* text, std::size_t size, PartnerRunaway& out) noexcept;
bool ParsePartnerWrap(const char* text, std::size_t size, PartnerWrap& out) noexcept;
bool ParsePartnerDrive(const char* text, std::size_t size, PartnerDrive& out) noexcept;
bool ParsePartnerDefer(const char* text, std::size_t size, PartnerDefer& out) noexcept;
bool ParsePartnerLocal(const char* text, std::size_t size, PartnerLocal& out) noexcept;
bool ParsePartnerNativeAim(const char* text, std::size_t size, PartnerNativeAim& out) noexcept;
bool ParsePartnerNativeRates(const char* text, std::size_t size, PartnerNativeRates& out) noexcept;
bool ParsePartnerNativeFire(const char* text, std::size_t size, PartnerNativeFire& out) noexcept;
// partner_input_defer as it takes effect: off unless partner_drive=native.
inline PartnerDefer EffectivePartnerDefer(PartnerDrive drive, PartnerDefer defer) noexcept {
    return drive == PartnerDrive::native ? defer : PartnerDefer::off;
}
// The bits the native drive may pass to instance 1 (ANDed with partner_input_mask).
inline std::uint32_t PartnerNativeAllowed(bool gun, bool fire) noexcept {
    return kPartnerNativeBaseMask | (gun ? kPartnerNativeGunBits : 0u) | (gun && fire ? kPartnerNativeFireBit : 0u);
}
// A signed decimal in [min, max]: optional '-', no '+', no leading zeros, no "-0".
bool ParsePartnerSigned(const char* text, std::size_t size, std::int32_t min, std::int32_t max, std::int32_t& out) noexcept;
bool ParsePartnerInput(const char* text, std::size_t size, PartnerInput& out) noexcept;
bool ParsePartnerInputCam(const char* text, std::size_t size, PartnerInputCam& out) noexcept;
bool ParsePartnerInputHold(const char* text, std::size_t size, PartnerInputHold& out) noexcept;
bool ParsePartnerInputYaw(const char* text, std::size_t size, PartnerInputYaw& out) noexcept;
bool ParsePartnerDm(const char* text, std::size_t size, PartnerDm& out) noexcept;
// Exactly 3 characters of 0prhn (one per word src+0x4C/+0x50/+0x54).
bool ParsePartnerWords(const char* text, std::size_t size, std::uint8_t (&out)[3]) noexcept;
// "0p0" (NUL-terminated); '?' for an out-of-range code.
void FormatPartnerWords(const std::uint8_t (&words)[3], char (&out)[4]) noexcept;
// Exactly 12 decimal gani indices 0..kPartnerDmGaniMax, comma-separated, no
// blanks, no leading zeros, nothing trailing. `out` is written only on success.
bool ParsePartnerDmMap(const char* text, std::size_t size, PartnerDmTable& out) noexcept;
// "0,10,11,5,1,3,4,4,7,6,6,6" (NUL-terminated; at most 35 chars; '?' for a cell above 99).
void FormatPartnerDmMap(const PartnerDmTable& table, char (&out)[40]) noexcept;
// "<idle>:<run>:<dash>" decimal mm/s, no leading zeros; idle 200..2000,
// run idle+300..10000, dash run+300..20000. `out` is written only on success.
bool ParsePartnerDmSpeeds(const char* text, std::size_t size, PartnerDmSpeeds& out) noexcept;
// "300:2600:5600" (NUL-terminated; at most 17 chars; values clamped to 99999).
void FormatPartnerDmSpeeds(const PartnerDmSpeeds& speeds, char (&out)[24]) noexcept;
// A u32 in decimal or 0x hex (1..8 hex digits, either case).
bool ParsePartnerMask(const char* text, std::size_t size, std::uint32_t& out) noexcept;
// "<deg>:<pct>" with deg 0..359 and pct 0..100 (no leading zeros except "0").
bool ParsePartnerStick(const char* text, std::size_t size, std::uint32_t& deg, std::uint32_t& pct) noexcept;
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
