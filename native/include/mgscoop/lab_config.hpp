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
// block-1 windows, sandboxed); looksrc (look plus the look source hook
// partner_look_source: PS phase 0's own look call 0xFFAC00 is followed by the
// remote's look for instance 1, so every phase-0 consumer sees it; the post
// phase-0 write stays the fallback; docs/native-puppet.md "Facing").
enum class PartnerNativeAim : std::uint8_t { off, look, lookcam, looksrc };
// The native facing governor (partner_recon FaceMode; docs/native-puppet.md
// "Facing"): off, idle (quiet idle turns and the settle snap; default) or aim
// (also quiet aim turns). Inert unless the native drive is active.
enum class PartnerNativeFace : std::uint8_t { off, idle, aim };
// The partner's shot noise on this machine (docs/native-puppet.md "Weapons"):
// filter (default: the noise vtable filter drops instance 1's records; FIRE
// is masked unless it is live) or keep (lab experiment: the receiver's guards
// hear the partner's visual shots).
enum class PartnerNativeNoise : std::uint8_t { filter, keep };
// The aim-target mirror (docs/native-puppet.md "Facing", aim parity): instance
// 1's network aim target Q+0x10[1] (never fed offline: its gun aimed at the
// world origin and the aim-turn predicate 0x10A0AB0 cycled act 12/15) gets a
// point along the remote's aim inside block 1's drain window. off outside a
// driven native gun stage, look (camera direction), or point (the sender's
// qualified gun direction in input frame v6; falls back to look when refused,
// stale or unavailable). Driven native gun profiles default to point; explicit
// off/look/point remains authoritative. No target writes outside native gun ownership.
enum class PartnerNativeAimPt : std::uint8_t { off, look, point };
// The partner's own reload (docs/native-puppet.md "Weapons", R1..R6): mirror
// (the magazine mirror writes the remote's magazine whenever it changes, also
// during body 1's reload: today's behaviour), raw (the mirror freezes while
// either side reloads and reconciles once afterwards; the remote's raw R still
// reaches body 1) or event (raw plus the explicit reload event: input frame v5
// tells when the remote enters its reload mode, the driver taps RELOAD through
// the overlay until body 1 enters its own, and the raw R/CARRY bit is stripped
// from the remote's sample). Inert unless the native drive runs the gun stage.
enum class PartnerNativeReload : std::uint8_t { mirror, raw, event };
// The partner's weapon switch (W4): off, write (the slot follower writes the
// non-local data effect of SetCurrentSlot 0x11AFA80 into instance 1's
// equipment row inside the drain window; the game then switches natively) or
// call (calls 0x11AFA80 itself, lab only: play uses write). Only to a filled
// slot whose weapon matches the plan; inert unless the native gun stage runs.
enum class PartnerNativeSlot : std::uint8_t { off, write, call };
// What body 1 carries (W2/W3; docs/native-puppet.md "Generalized writer"):
// stock46 (the qualified stock46 preload and writer: Afghanistan free roam
// only, the back slot), manifest (the generalized preload and writer with
// the received manifest's own base ids only: tier A1) or substitute (also a
// resident protected stock gun of the same type when the base id is not
// resident: tier C). Every partner-body mission; never a filled slot, never
// a cache eviction. Inert unless the native gun profile runs and the build
// carries the stock46 gun pieces (MGS_LOCAL_STOCK46_PRELOAD_TEST/_WRITER_TEST).
enum class PartnerNativeEquip : std::uint8_t { stock46, manifest, substitute };
// Weapons4 (docs/native-puppet.md "Generalized writer"): the generalized
// writer's shared tiers A1s/Cs, a stock cache entry the soldiers already
// reference (never the viewer): off, on (after the unreferenced candidates of
// each pass; the gun profile's default) or prefer (lab only: shared first;
// play runs it as on).
enum class PartnerNativeEquipShared : std::uint8_t { off, on, prefer };
// Weapons4: when the remote's current slot is empty or another class on body
// 1, the slot follower switches to a filled bullet slot (the stand-in) and the
// gate lets HOLD, FIRE and RELOAD through for it (partner_slots MakePlan,
// Gate::standin): off or on (the gun profile's default).
enum class PartnerNativeStandin : std::uint8_t { off, on };
// Anim round 1 (docs/anim-round1-weapons.md). The partner's aim intent:
// pad (HOLD only from the remote's pad, the old behaviour) or state (also
// while the remote's v5 gun group says Aiming or its act is an aim act, with
// the weapon gate match/standin, the remote in slot 0..2 and neither asleep,
// carried, hidden nor riding: partner_slots AimGate/AimFrame). The gun
// profile's default is state.
enum class PartnerNativeAimIntent : std::uint8_t { pad, state };
// Reload duration parity: off (body 1 reloads at its own provider rate) or
// adapt (the hook writes rate * k into instance 1's own rec+0x310 while it
// reloads; k learns from body 1's and the remote's measured reload lengths,
// partner_reload RateLearn). Lab key, default off until a hidden test.
enum class PartnerNativeReloadRate : std::uint8_t { off, adapt };
// Remote riding/deployment body visibility: driven native loco/gun stages
// default to hide in lab and play. Explicit off restores the prior visibility;
// hiding and stable on-foot recovery do not render a mounted partner body.
// seat (docs/anim-round1-seat.md): a remote driving or riding a light vehicle
// or truck in 30010/30020 is drawn seated in the receiver's copy of that
// vehicle (a targeted direct motion on body 1, partner_ride_seat); everything
// else (heli, horse, walker, APC/tank, unnamed vehicles, stale) still hides.
enum class PartnerNativeRide : std::uint8_t { off, hide, seat };
// The driven native stages' default (ResolveWeaponDefaults): the seated
// driver, flipped from hide after hidden runs 8b8e5bbc (the guest seated on the
// host screen) and 77d24c5e (the host seated on the guest screen) captured the
// remote body in the jeep's driver seat while it drove and parked.
inline constexpr PartnerNativeRide kPartnerNativeRideDriven = PartnerNativeRide::seat;
// partner_native_ride=seat: how the seat direct motion starts (direct: the
// seated loop at once; getin: the seat's finite get-on state, then the loop,
// only while the remote is entering) and ends (stop: RequestToStopDirectMotion;
// getoff: the chain carries the seat's finite get-off state and the exit
// advances to it with RequestToGoNextDirectMotion, falling back to stop).
enum class PartnerRideSeatEnter : std::uint8_t { direct, getin };
enum class PartnerRideSeatExit : std::uint8_t { stop, getoff };
// F1 vehicles (docs/native-puppet.md "Vehicle boarding"; partner_board): what the
// viewer's own ride action does at a vehicle the partner drives. off: stock (the
// viewer can become a second driver of that copy); block: no ride icon and no
// second driver; passenger: the free back seats of a light vehicle (the stock
// passenger phase, with the passenger camera), trucks and the rest block. Default
// passenger with the native gun profile in lab and play (ResolveWeaponDefaults),
// off without it.
enum class PartnerRideBoard : std::uint8_t { off, block, passenger };
// D-Horse and D-Walker riders (docs/mounts.md, partner_mount_policy): off (the
// ride wire alone decides: a horse rider hides, a D-Walker rider is reported on
// foot), hide (the viewer also hides body 1 while the remote's act is a horse
// or D-Walker act 121..126, and unrealizes it through Lua's all-active mode).
// The viewer's copy holds no GameObject for the partner's mount, so there is no
// seated value. Default hide with the native gun profile in lab; play forces off.
enum class PartnerNativeMount : std::uint8_t { off, hide };
// Round-1 actions track (docs/anim-round1-actions.md "DM overlay"): the
// DirectMotion overlay lever for graph-0 action states on body 1: off (the
// default), debug (only the debug commands overlay_state / overlay_stop) or
// map (also the remote-act map: damage, death, box, ledge hang, ladder).
enum class PartnerNativeOverlay : std::uint8_t { off, debug, map };
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
// play_rehearsal=1 outside a background-worker build (LabParseOptions):
// forced off, so a friend DLL can never be relaxed.
inline constexpr std::uint32_t kPlayRefusedRehearsal = 0x400000;
// partner_native_profile without the dm=map + xform fallback base (Play-Coop
// always writes it; a hand-edited lab.ini): the profile is forced off.
inline constexpr std::uint32_t kPlayRefusedNativeProfile = 0x800000;
// lab_fast_boot / lab_boot (the developer test boot, docs/test-boot.md): lab
// only, play forces both off.
inline constexpr std::uint32_t kPlayRefusedLabBoot = 0x1000000;
// The developer test boot (docs/test-boot.md): lab.ini
//   lab_boot=<mission> [<x> <y> <z> <yaw>] [time=HH:MM] [weather=<name>]
// (or lab_boot=off). mission is 30010, 30020, 30050 or an ACC 40010, 40020,
// 40050; the position (metres, at most 3 decimals, |x|,|y|,|z| <= 100000;
// yaw in degrees, |yaw| <= 360) only for a free-roam mission; time and weather
// at most once each, in either order; weather sunny|cloudy|rainy|sandstorm|
// foggy (no fog in 30020, which the game does not support). Blank-separated.
// Pure; the effect is lua/coop/debug_boot.lua, behind the loader's boot
// receipt (mgscoop/lab_boot.hpp), which answers only a background-worker
// build's owned private worker in lab mode.
struct LabBoot {
    std::uint32_t mission = 0;                 // 0 = none
    bool has_pos = false;
    std::int32_t x_mm = 0, y_mm = 0, z_mm = 0;  // metres * 1000
    std::int32_t yaw_mdeg = 0;                 // degrees * 1000
    std::int32_t clock_s = -1;                 // seconds after midnight; -1 = keep
    std::uint8_t weather = 0;                  // 0 keep, 1 sunny, 2 cloudy, 3 rainy, 4 sandstorm, 5 foggy
};
inline constexpr std::size_t kLabBootMaxText = 120;
// Outputs are written only on success ("off" clears the spec).
bool ParseLabBoot(const char* text, std::size_t size, LabBoot& out) noexcept;
const char* LabBootWeatherName(std::uint8_t weather) noexcept;
// Partner link transport: udp (our own UDP socket; the one-PC lab and the
// LAN/VPN code) or steam (ISteamNetworkingMessages002 over Steam's relays;
// play only, the lab refuses it and keeps udp).
enum class NetTransport : std::uint8_t { udp, steam };
// Steam code joins (docs/steam-online.md section 20): lab.ini
// net_steam_dispatch=both|fnptr|callback|poll (play; default both): the
// function pointer (config 204/205 + Sockets012::RunCallbacks) and/or the Init
// wrapper's 1251, polling always. Any other value is invalid (+1) and keeps both.
enum class SteamDispatch : std::uint8_t { both, fnptr, callback, poll };
const char* SteamDispatchName(SteamDispatch value) noexcept;
bool ParseSteamDispatch(const char* text, std::size_t size, SteamDispatch& out) noexcept;
// The in-game connection line and toasts (docs/two-pc-play.md "In game"):
// lab.ini coop_hud / coop.ini hud = compact|full|off; unset = compact.
enum class CoopHud : std::uint8_t { unset, compact, full, off };
const char* CoopHudName(CoopHud value) noexcept;
bool ParseCoopHud(const char* text, std::size_t size, CoopHud& out) noexcept;
// Automatic save handoff (docs/save-handoff.md; join-fix phase 2 P2-11):
// lab.ini save_handoff=off|stage|scene|always (absent = off; Play-Coop writes
// it explicitly; stage transfers and stages but never requests a restart) and
// travel_lock=0|1 (absent: 1 in play, 0 in lab). Neither is a play refusal.
// The order matches save_handoff_wire::Mode.
enum class SaveHandoff : std::uint8_t { off, stage, scene, always };
const char* SaveHandoffName(SaveHandoff value) noexcept;
bool ParseSaveHandoff(const char* text, std::size_t size, SaveHandoff& out) noexcept;
inline constexpr std::uint8_t kTravelLockAbsent = 0xFF;
// World sync lab probes (docs/world-sync-protocol.md "Probes"): lab.ini
// world_probe and the world.trigger key. The ids are the wire probeId of
// PROBE_SYNC and the Lua probe number, in this order.
enum class WorldProbe : std::uint8_t {
    none, p1, p3, p3b, p4, p12, p17, race_kill, remote_area_kill, kill_near, sleep_near, fulton_near, alert_near,
    weather_rain, clock_jump, clock_fast, shared_death, host_continue, shared_checkpoint
};
inline constexpr std::uint8_t kWorldProbeMax = 18;
inline constexpr std::uint32_t kWorldLevelMax = 3;

// Two-PC play's native partner (docs/two-pc-play.md "Native partner",
// docs/native-puppet.md "Configuration"): one key the loader expands into
// exactly the native stage the hidden worker qualified (loco or gun; the
// fields Get-WorkerPartnerSettings writes) on top of the dm=map + xform
// partner, which stays the automatic fallback. off keeps the clip partner.
// Allowed in lab and play; in play it is the only way to the native drive
// (the raw native keys stay refused, kPlayRefusedNativeDrive). A key named
// explicitly in the same lab.ini keeps its value (the variants below).
enum class PartnerNativeProfile : std::uint8_t { off, loco, gun };
// The native arbiter's network policy (partner_native Params;
// docs/native-puppet.md "Online"): lab (the thresholds the hidden runs
// qualified; every fallback counts toward the per-mission latch) or online
// (input stalls, link drops, a paused remote and range hiding fall back
// without counting, re-entry waits for link quality; the play default).
enum class PartnerNativeNet : std::uint8_t { lab, online };
// A gap in the remote's input stream: neutral (sticks zero once older than
// partner_input_stale_ms; lab default) or hold (the last live stick for up to
// partner_native_stall_ms; play default).
enum class PartnerNativeStall : std::uint8_t { neutral, hold };
// The 12-frame hold classifier under the native drive: own (the receiver's
// frames; lab default) or net (the sender's 12-frame words, frame-rate free).
enum class PartnerNativeHold : std::uint8_t { own, net };
inline constexpr std::uint32_t kPartnerStallMsMin = 100, kPartnerStallMsMax = 1000;
inline constexpr std::uint32_t kPartnerEdgeRepeatsMin = 2, kPartnerEdgeRepeatsMax = 8;
// Play defaults applied when the key is not named (lab keeps the qualified ones).
inline constexpr std::uint32_t kPlayEdgeRepeats = 5;

// Partner name tag and friendly crosshair (docs/partner-nametag.md): a
// read-only overlay, allowed in play. unset leaves the decision to coop.ini
// (nametag=, friendly_crosshair=) and then to the defaults (on, aim).
enum class NametagSetting : std::uint8_t { unset, off, on };
enum class NametagCrosshair : std::uint8_t { unset, aim, always, off };
inline constexpr std::uint32_t kNametagMaxMMin = 50, kNametagMaxMMax = 1000;
inline constexpr std::size_t kNametagNameChars = 24;

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
    PartnerNativeFace partner_native_face = PartnerNativeFace::idle;
    PartnerNativeNoise partner_native_noise = PartnerNativeNoise::filter;
    // Online play (docs/native-puppet.md "Online"; play defaults online / hold / 5).
    PartnerNativeProfile partner_native_profile = PartnerNativeProfile::off;
    PartnerNativeNet partner_native_net = PartnerNativeNet::lab;
    PartnerNativeStall partner_native_stall = PartnerNativeStall::neutral;
    std::uint32_t partner_native_stall_ms = 400;      // kPartnerStallMsMin..kPartnerStallMsMax
    PartnerNativeHold partner_native_hold = PartnerNativeHold::own;
    std::uint32_t partner_input_edge_repeats = 2;     // kPartnerEdgeRepeatsMin..kPartnerEdgeRepeatsMax
    // The hidden worker's one-PC play rehearsal (mode=play, worker builds only):
    // relaxes exactly the loopback bind and 127/8 peer, mutex_suffix,
    // xinput=none and the impairment keys.
    bool play_rehearsal = false;
    PartnerNativeAimPt partner_native_aimpt = PartnerNativeAimPt::off;
    bool partner_native_aimpt_given = false;
    PartnerNativeReload partner_native_reload = PartnerNativeReload::mirror;
    PartnerNativeSlot partner_native_slot = PartnerNativeSlot::off;
    PartnerNativeEquip partner_native_equip = PartnerNativeEquip::stock46;
    // Weapons4: the shared tiers, the lab-only measured optional render size
    // (partner_native_equip_measured=1: an optional header shape the size model
    // cannot qualify takes a native render's size; play forces 0) and the
    // stand-in follower. Not named in lab.ini: on / 0 / on with the native gun
    // profile, else off / 0 / off (ResolveWeaponDefaults).
    PartnerNativeEquipShared partner_native_equip_shared = PartnerNativeEquipShared::off;
    bool partner_native_equip_measured = false;
    PartnerNativeStandin partner_native_standin = PartnerNativeStandin::off;
    // Anim round 1 weapons (docs/anim-round1-weapons.md): the unsupported-
    // equipment gate (HOLD|FIRE|RELOAD stripped while the remote holds a
    // support weapon, an item or the bionic arm; on with the gun profile), the
    // auto-fire stop (FIRE stripped once the remote's magazine is dry; on with
    // the gun profile), the aim intent (state with the gun profile, else pad),
    // the reload rate (off unless named) and the fire/projectile census (lab
    // key partner_fire_census=1, read-only: logs every RequestBullet, AddNoise
    // and class-3 projectile request with its owner; default 0).
    bool partner_native_unsupported = false, partner_native_unsupported_given = false;
    bool partner_native_dry_stop = false, partner_native_dry_stop_given = false;
    PartnerNativeAimIntent partner_native_aim_intent = PartnerNativeAimIntent::pad;
    bool partner_native_aim_intent_given = false;
    PartnerNativeReloadRate partner_native_reload_rate = PartnerNativeReloadRate::off;
    bool partner_native_reload_rate_given = false;
    bool partner_fire_census = false;
    // Native type-12 support throws (docs/anim-round2-throws.md): not named in
    // lab.ini, on with the native gun profile in lab and play (Stage B), else
    // off (ResolveWeaponDefaults); a named 0/1 always wins.
    bool partner_native_support = false;
    bool partner_native_support_given = false;
    // WS4 step 0 (docs/anim-round2-throws.md "WS4 step 0"): the read-only C4 /
    // bionic-arm census (partner_placed_census lines, the input hook's pad
    // sample and the Lua event=obs_att lines through world CFG r[14]). No
    // write, no game call; default 0; play forces it off.
    bool partner_placed_census = false;
    // WS4 v1 (docs/anim-round2-throws.md "WS4 v1"): body 1's native placed C4
    // (type 13, only 391..395, slot 3: place-hold, ready, place) and arm holds
    // (type 14, only 516 for now, slot 6: HOLD only, FIRE always stripped). Both ride on
    // partner_native_support (its guard, filter, writer and gates). Not named in
    // lab.ini: on with the native gun profile in lab and play (after hidden runs
    // 7f8d643d / d05a1267), else off (ResolveWeaponDefaults); a named 0/1 always
    // wins; play admits them only while link v2 is connected (ModeRefusal).
    bool partner_native_placed = false;
    bool partner_native_placed_given = false;
    bool partner_native_arm = false;
    bool partner_native_arm_given = false;
    // WS4 v2 (docs/anim-round2-throws.md "WS4 v2"): the remote's C4 place (gun
    // mode 46) and detonate (47) mirrored into body 1's rec+0x3CC before its job,
    // so body 1 kneels to place and plays the detonator motion (no charge is
    // placed or set off in the viewer's world). Rides on partner_native_placed.
    // Not named in lab.ini: on with the native gun profile in lab (after hidden
    // runs 12320c2f / 0d8190ea / the reference run), else off; a named 0/1 wins.
    // Play forces it off (no play-mode C4 run yet).
    bool partner_native_place_mirror = false;
    bool partner_native_place_mirror_given = false;
    PartnerNativeRide partner_native_ride = PartnerNativeRide::off;
    bool partner_native_ride_given = false;
    PartnerRideSeatEnter partner_ride_seat_enter = PartnerRideSeatEnter::getin;
    PartnerRideSeatExit partner_ride_seat_exit = PartnerRideSeatExit::getoff;
    // Mount riders (docs/mounts.md): hide with the native gun profile in lab
    // (hidden run bfd6fcc4: no partner body on the viewer's screen while the
    // partner rode, the body back after the dismount), off without it; a named
    // value records _given (a named off opts out). Play forces it off (its
    // rehearsal fbde3dcc covered the horse only). Acts only where
    // partner_native_ride hides.
    PartnerNativeMount partner_native_mount = PartnerNativeMount::off;
    bool partner_native_mount_given = false;
    // G5 (docs/native-puppet.md "Ride hides"): the other ride hides (the heli acts
    // 97..108 or a heli ride, a vehicle ride drawn hidden: partner_native_ride=hide
    // or a refused / unavailable seat, a horse rider by its ride kind 3 when
    // partner_native_mount is off) unrealize body 1 through Lua's all-active mode
    // like the mount hide, and logs the link-quality rebase after a ride
    // (partner_ride=net_rebased; the rebase itself is always on). Default 0 in lab
    // and play until a hidden-worker FRAME proves it; a named value records _given
    // and is lab only (play forces it off). Acts only where partner_native_ride hides.
    bool partner_native_unrealize_all = false;
    bool partner_native_unrealize_all_given = false;
    // WS3 vehicles (docs/vehicle-ws3.md). partner_ride_census: the read-only
    // vehicle unit/body census lines, lab only (play forces it off), default
    // off. partner_ride_seat_id: the remote driver seated in an unnamed vehicle
    // by id (B1); default on in lab and play after hidden run abb0b7cf (Mother
    // Base, the guest saw the remote driver seated in the moving sortie jeep
    // 25603). partner_ride_tilt: RIDE_TILT, the driver's record angles written
    // into the viewer's own vehicle copy (A1); default on in lab and play after
    // hidden run 0a2314f2 (the viewer's jeep pitched with the seated driver).
    // Both are capability-gated on the wire (the ride addendum's features).
    bool partner_ride_census = false;
    bool partner_ride_seat_id = true;
    bool partner_ride_tilt = true;
    // F1 vehicles (partner_board, see PartnerRideBoard): off here; the native gun
    // profile resolves it to passenger in lab and play; a named value records _given.
    PartnerRideBoard partner_ride_board = PartnerRideBoard::off;
    bool partner_ride_board_given = false;
    // M6 seat 1 (docs/vehicles-companions-design.md 6.6, E6; partner_board): with
    // partner_ride_board=passenger, partner_ride_front=1 also offers the light
    // vehicle's front seat 1 and partner_ride_cab=1 the truck cab seat 1 of a vehicle
    // the partner drives (the two phase-1 immediates per steered call, the seat-1
    // remap skipped for the process). Off unless named, in lab and play, until a FRAME
    // proof on both copies and a play rehearsal; an invalid value refuses (named, off).
    bool partner_ride_front = false;
    bool partner_ride_front_given = false;
    bool partner_ride_cab = false;
    bool partner_ride_cab_given = false;
    // M6 seat races (docs/vehicles-companions-design.md 4 rule 3, 5 SEAT; partner_board "seat races",
    // lua/coop/world_ride_pose.lua SEAT op 54): the host decides who takes the driver's seat of an empty
    // vehicle both players reach; the loser gets no ride icon and a toast. Off unless named, in lab and play.
    bool partner_ride_race = false;
    bool partner_ride_race_given = false;
    // G4 truck test levers (vehicle_door, docs/native-puppet.md "Truck test levers"): the native
    // door-spot receipt behind debug goto_vehicle truck / goto_partner_vehicle / vehicle_door
    // (three read-only game calls under SEH). Default 0 in lab and play with a _given flag; play
    // forces it off; the receipt also refuses outside mode=lab with offline and redirected saves.
    bool debug_vehicle_door = false;
    bool debug_vehicle_door_given = false;
    // Round-1 actions track: the DM overlay lever (off) and the act census
    // telemetry (partner_act / partner_pad_dropped lines, log only; on).
    PartnerNativeOverlay partner_native_overlay = PartnerNativeOverlay::off;
    bool partner_act_log = true;
    // The native stance level fix: 1 = the round-1 window (500 ms behind the
    // guards, partner_recon NativeParams), 0 = the previous 2 s rule.
    bool partner_native_stance_fast = true;
    // Traversal replay (partner_traverse, docs/native-puppet.md "Traversal" and
    // "Traversal icon"): ladders, drain pipes, wall climbs and step-ons the
    // remote starts through its action icon are replayed on body 1 as a
    // placement and (0.13.5, partner_icon) an icon request that the padop
    // vtable copy answers inside instance 1's drain windows with the plugin's
    // own Exec for idx 1. Nothing presses ACTION on body 1. Default on with the
    // native gun profile in lab and play (user decision 2026-10-22; hidden run
    // 104e9a47: body 1's step-on in FRAMES, the viewer's own player unchanged;
    // play rehearsal 04d3511f; the Mother Base ladder in 8f27806d lab and
    // 2596f6ae play; pipe and wall climb unproven); a named 0 opts out.
    bool partner_native_traverse = false;
    bool partner_native_traverse_given = false;
    // F2 (partner_cbox_equip, docs/native-puppet.md "Cardboard box"): the
    // remote's cardboard box on body 1 through two native calls on the
    // equipment observer tick (ChangeEquip(1, box, flags 4) on, then
    // SetCurrentItemIndex(1, 0) off). Default on with the native gun profile in
    // lab and play (hidden run 05cffdd1: the box model on body 1, both
    // directions, the viewer's own state unchanged); play admits it only with
    // link v2 connected; a named 0 opts out.
    bool partner_native_cbox = false;
    bool partner_native_cbox_given = false;
    // Gestures (partner_gesture, docs/native-puppet.md "Gestures"): the remote's
    // pickup, hand sign Go / Stop and wall knock (its upper act Q+0x20 edges, a
    // new reliable channel-2 message) played on body 1's graph 2 through
    // SetCurrentNode / ReserveToResetNode inside body 1's own motion update. The
    // same key gates the sender. Default on with the native gun profile in lab
    // and play (hidden run 0e343f72: the pickup clip on body 1, the viewer's own
    // player unchanged); a named 0 opts out.
    bool partner_native_gesture = false;
    bool partner_native_gesture_given = false;
    // G1 partner HUD hints (partner_ui_filter, docs/native-puppet.md "Partner HUD
    // hints"): body 1's HUD hint requests (the UiControllerImpl hint queues
    // vt+0x240..+0x260 its ladder / climb / pipe / hang states post every frame,
    // e.g. the "C + ladder" slide hint) are swallowed inside its own drain job, so
    // they never show beside the viewer's player; the viewer's own hints pass.
    // Default 0 in lab and play: a named 1 is lab only (play forces it off) until
    // a hidden-worker FRAME shows the viewer's HUD clean while body 1 climbs.
    bool partner_native_ui_filter = false;
    bool partner_native_ui_filter_given = false;
    // The gesture lab lever (partner_gesture_lever, docs/native-puppet.md
    // "Gestures", "Lab lever: knock and hand signs"): the native debug command
    // "gesture_lever knock|sign_go|sign_stop" makes this copy's OWN player knock
    // or sign through the stock KnockActionPluginImpl request slots the call
    // menu uses (vt+0x28 / +0x30 / +0x38), so a hidden run can film the peer's
    // body 1 replaying the edge. Lab only: default 0, play forces it off; a
    // named value records partner_gesture_lever_given. Never on by default
    // until a hidden-run FRAME proves the replay.
    bool partner_gesture_lever = false;
    bool partner_gesture_lever_given = false;
    bool partner_native_equip_shared_given = false, partner_native_standin_given = false;
    // partner_native_reload / _slot / _equip not named in lab.ini follow the
    // native gun profile (ResolveWeaponDefaults): event / write / substitute
    // with partner_drive=native and partner_native_gun=1, else mirror / off /
    // stock46. These record which of the three were named explicitly.
    bool partner_native_reload_given = false, partner_native_slot_given = false, partner_native_equip_given = false;
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
    // link_session_skew=<0..999> (0.13.6 online-net-epoch; a lab / hidden-worker
    // reproduction knob): this copy's link-session counter starts at the value,
    // so its first committed session is skew+1 while a fresh peer's is 1 (the
    // counters of the 0.13.5 online session after a guest relaunch). An
    // impairment key: the lab and the worker's play rehearsal only; play
    // forces 0 (kPlayRefusedImpairment).
    std::uint32_t link_session_skew = 0;
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
    // world_vehicle_gone=0|1 (0.13.6 online-vehicle-gone, docs/world-sync-protocol.md
    // section 16): the host-authoritative VEHICLE_GONE world op for Fultoned and
    // destroyed vehicles, CFG r[15] bit 1 for Lua. Lab and play (not a play refusal).
    // M4: off in the lab unless named (scenarios name it), on in play unless named
    // (lab FRAMEs 92720b75 / 0edadbe2; play rehearsal e44516d7, -WanSim bad: each
    // player's own Fulton removed the jeep on both copies); a named value records _given.
    bool world_vehicle_gone = false;
    bool world_vehicle_gone_given = false;
    // world_vehicle_state=0|1 (M4, docs/vehicles-companions-design.md 6.4): the
    // host-authoritative VEHICLE_STATE world op for parked / pushed / left vehicles
    // and their body life, CFG r[15] bit 2 for Lua. Lab and play (not a play
    // refusal); off in the lab unless named, on in play unless named (lab FRAMEs
    // d407f115 / 0edadbe2; play rehearsals e235bda6 and d9b48fcb, -WanSim bad).
    bool world_vehicle_state = false;
    bool world_vehicle_state_given = false;
    // world_vehicle_wreck=0|1 (M4 review): VEHICLE_GONE's wreck parity, CFG r[15] bit 4
    // for Lua: a destroyed vehicle (cause 2) becomes a wreck on the receiver through
    // SetBodyLife{life=0} (the engine's own destruction, blast included) instead of a
    // Despawn. Lab and play (not a play refusal), off in both unless named: its only
    // play rehearsal (e235bda6) never destroyed the jeep, and the receiver's blast was
    // never measured with live guards. Lab FRAME 0edadbe2.
    bool world_vehicle_wreck = false;
    // The user's vehicle decisions of 2026-10-29 (docs/vehicles-companions-design.md section 10;
    // lua/coop/world_vehicle_rules.lua), lab and play (no refusal bit), CFG r[15] bits 8 / 16 / 32
    // for Lua; off in the lab unless named, on in play unless named (lab FRAME c0203fdd, play
    // rehearsal 70146170 with -WanSim bad; after the review FRAME f729d7f4 / 33a7fa6e and the play
    // rehearsal 33a7fa6e); a named value (an invalid one too, which counts as
    // off) records _given.
    // world_vehicle_break=0|1 (D12, bit 8): a vehicle that breaks on the guest's copy without
    // attribution is requested as a destruction (REQ_VEHICLE_GONE cause 4); the host applies it (its
    // copy wrecked) only when plausible (the guest within 300 m, or its copy heavily damaged; never
    // under its own player, held while another seat is filled; 4 per 20 s, more wait), as a Despawn
    // instead of the wreck while a player stands within 15 m of its copy (the blast rule, run
    // 7762536a). A host without the key answers cause 4 with REJECT 11.
    bool world_vehicle_break = false;
    bool world_vehicle_break_given = false;
    // world_vehicle_credit=0|1 (D14, bit 16): the receiver of a Fultoned world vehicle removes its
    // copy with the stock Seize Instant, so its own engine credits the vehicle once (never index 0).
    bool world_vehicle_credit = false;
    bool world_vehicle_credit_given = false;
    // world_loadout_block=0|1 (D11, bit 32): the guest's iDroid LOADOUT and VEHICLES supply drops
    // are inactive in co-op (a toast on selecting one), whatever world_relief says; the host's are
    // untouched.
    bool world_loadout_block = false;
    bool world_loadout_block_given = false;
    // The vehicle fixes of 2026-10-29 (docs/vehicles-companions-design.md sections 8.14 and 8.15), lab and
    // play (no refusal bit), off in the lab unless named; in play world_vehicle_crew is on unless named (lab
    // FRAME c0a385a5, play rehearsal c6db67b8) and world_vehicle_damage off unless named (the review of
    // 2026-10-29 turned it off again):
    // world_vehicle_crew=0|1 (D12, bit 64; lua/coop/world_vehicle_rules.lua): a guest destruction the
    // host judged plausible whose only hold is an NPC crew (soldiers, not a player, D-Dog or a buddy)
    // is applied at once (the host's copy wrecked: the engine's blast decides the crew), held only
    // while a player stands within the blast distance (15 m for a light vehicle, 50 m for any other type).
    bool world_vehicle_crew = false;
    bool world_vehicle_crew_given = false;
    // world_vehicle_damage=0|1 (bit 128; lua/coop/world_vehicle_damage.lua): the guest reports the
    // body-life drops of its own copy (VEHICLE_DAMAGE, op 0x3A) and the host applies them (the lower
    // life, at most the guest's drop), so VEHICLE_STATE keeps the guest's damage on both copies.
    bool world_vehicle_damage = false;
    bool world_vehicle_damage_given = false;
    // companion_host_owned=0|1 (docs/vehicles-companions-design.md 6.1, M1): the host's
    // buddy is the session's one companion. The follow seeds carry the host's buddy vars
    // and the guest loads them (its own are kept for its saves); guest buddy calls, the
    // call menu and the F9 summon are refused; a guest playing Quiet locks the host's
    // buddy Quiet. CFG r[16] for Lua. Default off in lab and play until a hidden-run
    // FRAME and a play rehearsal prove it (project rule); a named value wins.
    bool companion_host_owned = false;
    bool companion_host_owned_given = false;
    // world_relief=0|1 (docs/vehicles-companions-design.md 6.3, M3 route A): the host's sortie
    // (relief) vehicle on both copies. The follow seeds carry the host's relief carry vars and
    // paint presets and the guest applies them (its own are kept for its saves); VEHICLE_SPAWN
    // gives a late or continuing guest the host's vehicle; the guest's mission-end seize is
    // skipped; VEHICLE_GONE removes index 0 on the receiver without a credit; a guest-side loss of
    // index 0 is a request the host answers with its own (credited) Fulton; the guest's iDroid
    // vehicle drop is refused. CFG r[17] for Lua. Off in lab and play unless named (on from 5a6bac8
    // until the review of 2026-10-29; ResolveWeaponDefaults); a named value wins.
    bool world_relief = false;
    bool world_relief_given = false;
    // The developer test boot (docs/test-boot.md): lab_fast_boot=0|1 skips the
    // startup logos and copyright screen and takes the title itself (no press
    // start, no menu, the offline notice closed); lab_boot (LabBoot above) is
    // where this copy lands after the title. Lab only: play forces both off
    // (kPlayRefusedLabBoot); outside a background-worker build the boot receipt
    // stays inert, so a friend/Release loader never acts on them.
    bool lab_fast_boot = false;
    LabBoot lab_boot{};
    // Automatic save handoff and the guest travel lock (P2-11): save_handoff
    // absent = off; travel_lock 0|1, kTravelLockAbsent when not named (the
    // effective value is then 1 in play and 0 in lab).
    SaveHandoff save_handoff = SaveHandoff::off;
    bool save_handoff_given = false;
    std::uint8_t travel_lock = kTravelLockAbsent;
    // Debug "Spawn enemy here" native placement (native/soldier_warp.cpp): the
    // engine set-transform message to one soldier. Lab qualification only; play
    // forces it off (kPlayRefusedDebugSoldierWarp).
    bool debug_soldier_warp = false;
    // Partner name tag: nametag=0|1, nametag_crosshair=aim|always|off,
    // nametag_name (1..24 of [A-Za-z0-9 _.-], this PC's label on the other
    // screen), nametag_max_m (50..1000, hidden farther away).
    NametagSetting nametag = NametagSetting::unset;
    NametagCrosshair nametag_crosshair = NametagCrosshair::unset;
    char nametag_name[kNametagNameChars + 1]{};
    std::uint32_t nametag_max_m = 300;
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
    // Section 20: the code-join sources (play) and the self-test's stage F/D
    // (steam_selftest_fn=0 skips it: fn=skip).
    SteamDispatch net_steam_dispatch = SteamDispatch::both;
    bool steam_selftest_fn = true;
    // In-game connection line and toasts (allowed in play; wins over coop.ini hud).
    CoopHud coop_hud = CoopHud::unset;
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
const char* PartnerNativeFaceName(PartnerNativeFace value) noexcept;
const char* PartnerNativeNoiseName(PartnerNativeNoise value) noexcept;
const char* PartnerNativeAimPtName(PartnerNativeAimPt value) noexcept;
const char* PartnerNativeReloadName(PartnerNativeReload value) noexcept;
const char* PartnerNativeSlotName(PartnerNativeSlot value) noexcept;
const char* PartnerNativeEquipName(PartnerNativeEquip value) noexcept;
const char* PartnerNativeEquipSharedName(PartnerNativeEquipShared value) noexcept;
const char* PartnerNativeStandinName(PartnerNativeStandin value) noexcept;
const char* PartnerNativeAimIntentName(PartnerNativeAimIntent value) noexcept;
const char* PartnerNativeReloadRateName(PartnerNativeReloadRate value) noexcept;
const char* PartnerNativeRideName(PartnerNativeRide value) noexcept;
const char* PartnerNativeMountName(PartnerNativeMount value) noexcept;
const char* PartnerRideSeatEnterName(PartnerRideSeatEnter value) noexcept;
const char* PartnerRideSeatExitName(PartnerRideSeatExit value) noexcept;
const char* PartnerRideBoardName(PartnerRideBoard value) noexcept;
const char* PartnerNativeOverlayName(PartnerNativeOverlay value) noexcept;
const char* LabModeName(LabMode value) noexcept;
// The effective travel lock (P2-11): the lab.ini travel_lock when given,
// otherwise on in play and off in lab.
bool EffectiveTravelLock(const LabConfig& config) noexcept;
// aim|always|off ("" for unset); parse refuses "unset".
const char* NametagCrosshairName(NametagCrosshair value) noexcept;
bool ParseNametagCrosshair(const char* text, std::size_t size, NametagCrosshair& out) noexcept;
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
bool ParsePartnerNativeFace(const char* text, std::size_t size, PartnerNativeFace& out) noexcept;
bool ParsePartnerNativeNoise(const char* text, std::size_t size, PartnerNativeNoise& out) noexcept;
const char* PartnerNativeProfileName(PartnerNativeProfile value) noexcept;
const char* PartnerNativeNetName(PartnerNativeNet value) noexcept;
const char* PartnerNativeStallName(PartnerNativeStall value) noexcept;
const char* PartnerNativeHoldName(PartnerNativeHold value) noexcept;
bool ParsePartnerNativeProfile(const char* text, std::size_t size, PartnerNativeProfile& out) noexcept;
bool ParsePartnerNativeNet(const char* text, std::size_t size, PartnerNativeNet& out) noexcept;
bool ParsePartnerNativeStall(const char* text, std::size_t size, PartnerNativeStall& out) noexcept;
bool ParsePartnerNativeHold(const char* text, std::size_t size, PartnerNativeHold& out) noexcept;
// The profile's stage settings (docs/native-puppet.md "Configuration"; equal
// to Get-WorkerPartnerSettings -NativeStage loco|gun): written into `config`
// except the keys whose bit is set in `keep` (kProfileKey* below: named
// explicitly in the same lab.ini). off writes nothing.
inline constexpr std::uint32_t kProfileKeyDrive = 1u << 0, kProfileKeyDefer = 1u << 1, kProfileKeySend = 1u << 2,
                               kProfileKeyHook = 1u << 3, kProfileKeyHold = 1u << 4, kProfileKeyWords = 1u << 5,
                               kProfileKeyWrap = 1u << 6, kProfileKeyCam = 1u << 7, kProfileKeyLocal = 1u << 8,
                               kProfileKeyRecon = 1u << 9, kProfileKeySlot = 1u << 10, kProfileKeyRates = 1u << 11,
                               kProfileKeyNoise = 1u << 12, kProfileKeyP218 = 1u << 13, kProfileKeyMask = 1u << 14,
                               kProfileKeyAim = 1u << 15, kProfileKeyGun = 1u << 16, kProfileKeyFire = 1u << 17,
                               kProfileKeyTriggers = 1u << 18, kProfileKeyRstick = 1u << 19,
                               kProfileKeyAimPt = 1u << 20;
void ApplyNativeProfile(LabConfig& config, PartnerNativeProfile profile, std::uint32_t keep = 0) noexcept;
bool ParsePartnerNativeAimPt(const char* text, std::size_t size, PartnerNativeAimPt& out) noexcept;
bool ParsePartnerNativeReload(const char* text, std::size_t size, PartnerNativeReload& out) noexcept;
bool ParsePartnerNativeSlot(const char* text, std::size_t size, PartnerNativeSlot& out) noexcept;
bool ParsePartnerNativeEquip(const char* text, std::size_t size, PartnerNativeEquip& out) noexcept;
bool ParsePartnerNativeEquipShared(const char* text, std::size_t size, PartnerNativeEquipShared& out) noexcept;
bool ParsePartnerNativeStandin(const char* text, std::size_t size, PartnerNativeStandin& out) noexcept;
bool ParsePartnerNativeAimIntent(const char* text, std::size_t size, PartnerNativeAimIntent& out) noexcept;
bool ParsePartnerNativeReloadRate(const char* text, std::size_t size, PartnerNativeReloadRate& out) noexcept;
bool ParsePartnerNativeRide(const char* text, std::size_t size, PartnerNativeRide& out) noexcept;
bool ParsePartnerNativeMount(const char* text, std::size_t size, PartnerNativeMount& out) noexcept;
bool ParsePartnerRideSeatEnter(const char* text, std::size_t size, PartnerRideSeatEnter& out) noexcept;
bool ParsePartnerRideSeatExit(const char* text, std::size_t size, PartnerRideSeatExit& out) noexcept;
bool ParsePartnerRideBoard(const char* text, std::size_t size, PartnerRideBoard& out) noexcept;
bool ParsePartnerNativeOverlay(const char* text, std::size_t size, PartnerNativeOverlay& out) noexcept;
// The weapons keys' profile defaults (see partner_native_equip): runs at the
// end of ParseLabConfig, after every key and any profile expansion.
void ResolveWeaponDefaults(LabConfig& config) noexcept;
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
    snake, dd_male, dd_female, avatar, ocelot, quiet,
    // Resolved by lua/coop/character.lua at each allocation from the player's
    // own save: Quiet while she is a deployable buddy on Diamond Dogs' team,
    // otherwise Ocelot (docs/characters.md). Never a fallback.
    story
};

struct CoopConfig {
    bool present = false;
    char master_url[70]{};  // empty = leave the game's URL; else http(s)://.../tppstm/gate, <= 69 chars
    PlayerCharacter character = PlayerCharacter::game_default;
    // Used where the chosen character conflicts with the mission (e.g. story Quiet).
    PlayerCharacter character_fallback = PlayerCharacter::dd_male;
    // Partner name tag (docs/partner-nametag.md): name= is this player's label
    // on the partner's screen (UTF-8, at most 64 bytes, sanitized before use);
    // nametag= and friendly_crosshair= apply unless lab.ini sets its own keys.
    char name[65]{};
    NametagSetting nametag = NametagSetting::unset;
    NametagCrosshair friendly_crosshair = NametagCrosshair::unset;
    // hud=compact|full|off: the in-game connection line and toasts.
    CoopHud hud = CoopHud::unset;
    unsigned unknown_keys = 0;
    unsigned invalid_values = 0;
};

// Pure parsers. `text` need not be NUL-terminated. Return false only when the
// input is unusable (null with nonzero length, or longer than 64 KiB).
// allow_play_rehearsal: only the background-worker build's loader and its
// supervisor gate pass true (GetLabConfig with MGS_PLAY_REHEARSAL); everywhere
// else play_rehearsal=1 is a play refusal (kPlayRefusedRehearsal).
struct LabParseOptions {
    bool allow_play_rehearsal = false;
};
bool ParseLabConfig(const char* text, std::size_t length, LabConfig& out) noexcept;
bool ParseLabConfig(const char* text, std::size_t length, LabConfig& out, const LabParseOptions& options) noexcept;
bool ParseCoopConfig(const char* text, std::size_t length, CoopConfig& out) noexcept;

// Lua/log spelling of a character ("quiet", "dd_male", "story", ...; "" for game_default).
const char* PlayerCharacterName(PlayerCharacter value) noexcept;
// PlayerType enum value for the 1.0.15.4 binding (SNAKE 0, DD_MALE 1, DD_FEMALE 2,
// AVATAR 3, OCELOT 5, QUIET 6); -1 for game_default and story (resolved in Lua).
int PlayerCharacterType(PlayerCharacter value) noexcept;

#ifdef _WIN32
// Read once from <directory of the process executable>\mgs-coop\{lab,coop}.ini.
// A missing file yields a default (present=false) configuration.
const LabConfig& GetLabConfig() noexcept;
const CoopConfig& GetCoopConfig() noexcept;
#endif

} // namespace mgscoop
