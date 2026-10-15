#pragma once
// Round 3/4 input lever, Win32 part (docs/partner-anim-appearance.md "Round 3"
// and "Round 4"): a near stub and rel32 patch at the block-1 dispatch call
// 0x9C2B51, the hook itself, the seqlock the driver publishes the Arm through,
// and the drained statistics. The portable pad logic is partner_input; the
// pure reads that build the Arm are partner_player. Lock order and threading:
// the driver runs on the Lua tick thread (publishes the Arm, drains stats); the
// hook runs on whatever thread runs the player update 0x9C1F00 and never
// blocks or takes a lock other than the non-blocking SRW the sample uses.
//
// Round 4 additions:
// - Every window installs a per-window PP vtable copy whose key-manager slots
//   (+0xE8..+0x138) answer from the injected words instead of the host
//   keyboard, and whose hold classifiers (+0x90, +0x140) follow hold=none|
//   pass|own|net (own: partner_input::ClassifyHold over a history of what
//   instance 1 was fed, one entry per main window).
// - PP+0x278 (mouse-look gate) and PP+0x4B0 (rebind request) are shadowed:
//   instance 1 sees and writes its own copy, the host keeps its value.
// - wrap=census|window: pass-through wrappers on the other walks of the block
//   trees (UpdateTree service slots +0x28/+0x30/+0x40, calls 0x9C1DA3 and
//   0x9C1AFF) that count block-1 calls per phase/thread/nesting, and with
//   wrap=window run the same window (the main window's injection of this
//   frame) around block 1's calls on the hook thread.
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_input.hpp"
#include "mgscoop/native_pose_window.hpp"

namespace mgscoop::nametag_game { struct HudJobProbe; }
namespace mgscoop::partner_input_hook {

// Optional read-only HUD census owned by the probe DLL. Callbacks avoid a
// static partner library dependency on name-tag/game observer code.
using HudBeforeFn = nametag_game::HudJobProbe (*)(std::uintptr_t p) noexcept;
using HudAfterFn = void (*)(const nametag_game::HudJobProbe& before) noexcept;
void RegisterHudProbe(HudBeforeFn before, HudAfterFn after) noexcept;
using LogFn = void (*)(const char* line) noexcept;
// partner_net::ConsumeInput: false when the lock is busy (reuse the previous
// level fields, no edges).
using SampleFn = bool (*)(partner_input::Sample& out) noexcept;
// 0xBF61D0(root, phase) and the PP hold classifier 0x1205C30(PP, btn, frames).
using DispatchFn = void (*)(void* root, std::uint32_t phase) noexcept;
using HoldFn = std::uint32_t (*)(void* pp, std::uint32_t btn, std::uint32_t frames) noexcept;

// Installs once per process (idempotent; the patch and stub are never removed,
// the proxy DLL never unloads). `base` is the image base. Returns nullptr on
// success, otherwise a static reason (also logged):
//   bytes | allocate_near | range | protect | exchange | vt_copy | disabled.
const char* Install(const void* base, LogFn log, SampleFn sample) noexcept;
bool Installed() noexcept;
// The wrap entries (wrap=census|window), installed lazily once per process
// after Install; never removed (with wrap=off the wrappers only forward).
// nullptr on success, otherwise a static reason (also logged).
const char* InstallWrap() noexcept;
bool WrapInstalled() noexcept;
// The per-window vtable copies (hold none / hold log / own+net / pass); 0
// before install. They are only ever live inside a window: the Lua-thread
// reads refuse them.
inline constexpr std::size_t kVtableCopies = 4;
void VtableCopies(std::uintptr_t out[kVtableCopies]) noexcept;
// Odd while a window (or the block-0 hold log) has fields swapped in; it
// changes on every window. Lua-thread readers of PP/src retry or refuse
// unless it is even and unchanged across their read.
std::uint32_t WindowSeq() noexcept;
// True on the hook thread while instance 1's deferred job runs inside its drain window.
bool InDrainJob() noexcept;
// Clears a recorded fault so the window may run again (MissionStart only;
// the driver keeps its own per-mission input_faulted) and the hold history.
void ResetFault() noexcept;
bool NativeSupportLatchedOff() noexcept;

// The driver publishes this each tick (seqlock: odd, copy, even). Copied by
// value; pointers are read inside the hook under SEH and re-validated.
void PublishArm(const partner_input::Arm& arm) noexcept;
// Process shutdown (the game window is closing), from any thread: every wrapper
// forwards from now on (no windows, no rate/magazine restore writes).
void Shutdown() noexcept;

// Wrap census counters (process-lifetime totals; the driver logs deltas).
// Entry e: 0 svc+0x28 (active walk), 1 svc+0x30 (all ids), 2 svc+0x40 (per
// id), 3 call 0x9C1DA3 (phase 7), 4 call 0x9C1AFF (phase 11).
inline constexpr std::size_t kWrapEntries = 5;
struct WrapStats {
    bool installed;
    std::uint32_t calls0[kWrapEntries];       // root == block 0's root
    std::uint32_t calls1[kWrapEntries];       // root == block 1's root
    std::uint32_t same_thread[kWrapEntries];  // block-1 calls on the hook thread
    std::uint32_t nested[kWrapEntries];       // block-1 calls inside an open window
    std::uint32_t windows[kWrapEntries];      // wrap=window windows run
    std::uint32_t phase_mask[kWrapEntries];   // phases seen for block 1 (bit = phase & 31)
    std::uint32_t other_calls;                // calls for any other root (all entries)
    // Native puppet: windows refused because a batch was in flight (a walk or
    // a legacy svc window inline during the join; main windows and drains
    // count here too), and svc entries of block 1 on the hook thread outside
    // a window under a native Arm (block 1's job runs only inside drains: one
    // arriving here ran outside a drain, in a batch or not; never a refusal).
    std::uint32_t wrap_in_batch;
    std::uint32_t svc_native_skips;
};

// Native puppet window statistics (process-lifetime totals).
enum class LookRefusal : std::uint8_t { none, off, no_v3, stale, ps, rec_read, fault };
const char* LookRefusalName(LookRefusal refusal) noexcept;
// Magazine mirror verdicts: off (Arm without mirror or gun record), no_v4
// (the live sample carries no magazine), prim (the gun prim chain changed),
// full (rec+0x3C0 bit 28: the getter ignores the field), cls (not a gun
// class), cap (capacity outside 1..999), fault (a read/write faulted: the
// mirror is off until MissionStart), leak (a drain's job changed the viewer's
// weapon/ammo vars: the mirror is off until MissionStart).
// weapon: the weapon gate strips FIRE/RELOAD (the remote's gun is not body
// 1's): the mirror is frozen. store: the store row refused (StoreRefusal).
// reload: frozen while the remote or body 1 reloads (partner_native_reload
// raw|event, partner_reload::MirrorFrozen); the next write reconciles.
enum class MagRefusal : std::uint8_t { none, off, no_v4, prim, full, cls, cap, fault, leak, weapon, store, reload };
const char* MagRefusalName(MagRefusal refusal) noexcept;
// The magazine's per-instance store row (docs/native-puppet.md "Weapons" F2;
// gun phase 0 copies it into rec+0x25C/+0x25E every frame): y_vt (the store
// iface is not the pinned class or its +0x20 is not the setter), slot (stride
// outside 1..16, the slot beyond it, or id 1's row at or past the store's row
// count Y+0x10, which sized its arrays), type (the row's type outside 1..9, or
// type 9 with a sub magazine), owner (instance 1 is the local id), online
// (S_IS_ONLINE or P+0x204 bit 18), tamper (rec+0x3D0 bit 27), cap (the value
// above the capacity).
enum class StoreRefusal : std::uint8_t { none, y_vt, slot, type, owner, online, tamper, cap };
const char* StoreRefusalName(StoreRefusal refusal) noexcept;
// The pad's sample rings in drain windows (F1): src (no src), cap (src+0x40
// outside 2..kRingEntries), fault.
enum class RingRefusal : std::uint8_t { none, src, cap, fault };
const char* RingRefusalName(RingRefusal refusal) noexcept;
// The slot follower (W4) verdicts: off (the Arm has no follower or no gun
// record), no_v5 (the live sample carries no slot), other (the remote's slot
// is not 0..2: support, items, hand), same (body 1 is already there), wait
// (body 1 is mid-switch, or this episode's writes are spent), plan (the
// driver's plan does not admit the slot), chain (P/Q/ctl/inventory identity),
// online (S_IS_ONLINE or P+0x204 bit 18), owner (instance 1 is local), mode
// (rec+0x3DE != 7: phase 0 does not follow the equip controller), empty (no
// weapon in that slot), mismatch (the handle's equip is not the row's or the
// plan's), readback (the row did not keep the write), fault (a read, write or
// the call faulted: the follower is off until MissionStart).
enum class SlotRefusal : std::uint8_t { none, off, no_v5, other, same, wait, plan, chain, online, owner, mode, empty, mismatch, readback, fault };
const char* SlotRefusalName(SlotRefusal refusal) noexcept;
inline constexpr std::uint32_t kRingEntries = 128;
struct NativeStats {
    std::uint32_t frames;          // frame injections composed (main windows under a native Arm)
    std::uint32_t views[3];        // consumer views: full, level, neutral
    std::uint32_t drain_windows;   // RunJobWindow windows
    std::uint32_t drain_refused;   // RunJobWindow ran the job without a window
    std::uint32_t walk_windows;    // native walk7/walk11 windows
    std::uint32_t latch_other;     // windows that left PP+0x27C bits 1..31 changed
    std::uint32_t latch_last;      // the last latch value written (bit 0)
    std::uint32_t look_writes, look_skipped_rec, look_refused;
    LookRefusal look_last;
    bool look_faulted;
    // CamSub ranges changed (bit 0: +0x3C..+0x48, bit 1: +0xB8); bit 2: the
    // sub-controller pointer *(CAM+0x360) itself changed inside the window
    // (then nothing of the old object is read or restored).
    std::uint32_t camsub_dirty_or;
    std::uint32_t lookcam_writes;
    // RunJobWindow refusals: the job's block is not instance 1's (the Arm's
    // pool1, never the live pool of instance 0), or a batch was in flight.
    std::uint32_t drain_block_refused, drain_in_batch;
    // Phase-1 detour (partner_local_detour, process totals): phase-1 calls
    // switched to the local branch, id matches refused (P or thread), drain
    // windows that armed it.
    bool p1_installed;
    std::uint32_t p1_forced, p1_refused, p1_windows;
    // The last windowed drain: the injection it got (held word, stick
    // magnitude x1000, consumer view 0 full / 1 level / 2 neutral), rec+0x2AC
    // before the window set isLocal, instance 1's private PP masks after it
    // (+0x28 buttons, +0x2C sticks: bit 0 zeroes the sampled move magnitude,
    // +0x30 triggers) and instance 1's base action record after its update
    // (rec_ok false when the read faulted).
    std::uint32_t drain_held, drain_view, drain_local_pre, drain_mask28, drain_mask2c, drain_mask30;
    std::int32_t drain_mag;
    bool rec_ok;
    std::uint32_t rec_act, rec_next;       // rec+0x2A0 current act, rec+0x2A1 requested next act
    std::int32_t rec_mag, rec_heading_mrad;  // rec+0x2C0 move magnitude x1000, rec+0x2C4 heading
    // Native rates mirror (docs/native-puppet.md "Rates"): whether instance
    // 1's record holds the mirrored rates (applied) or the mirror latched for
    // the epoch (reason: partner_rates::Reason), process totals of plans that
    // wrote (writes), of game rewrites taken over (reapplied), of restores, of
    // states dropped without a write (forget, record or epoch change), and
    // the last written {dash, crawl, crawl factor} x 1000.
    bool rates_applied, rates_latched;
    std::uint32_t rates_reason;
    std::uint32_t rates_writes, rates_reapplied, rates_restores, rates_drops;
    std::uint32_t rates_written[3];
    // Native weapons (docs/native-puppet.md "Weapons"). The magazine mirror
    // (drain windows, before block 1's job): writes, refusals and the last
    // refusal reason, the last values (remote mag/cap, partner capacity and
    // the value written, 0xFFFF before any), writes caused by a FIRE or a
    // RELOAD press edge, and whether a fault disabled it for the mission.
    std::uint32_t mag_writes, mag_refused, mag_fire_edges, mag_reload_edges;
    MagRefusal mag_last;
    std::uint32_t mag_remote, mag_remote_cap, mag_cap, mag_written;
    bool mag_faulted, mag_leak_off;  // mag_leak_off: an ammo leak turned the mirror off until MissionStart
    // Leak check: drain windows whose job changed the viewer's
    // vars.playerShootCountInMission (vars+0x144; never written by us), its
    // value after the last drain (0xFFFFFFFF unread), and drains after which
    // instance 1's reload request (rec+0x3C0 bit 20) was set.
    std::uint32_t shot_leaks, shot_count, reload_drains;
    // The same for the viewer's weapon/ammo vars block (vars+0x548..+0x670):
    // drain windows whose job changed it, and its FNV-1a digest after the
    // last drain (0 unread).
    std::uint32_t ammo_leaks, ammo_digest;
    // F2: store row writes and refusals (the last reason), and the last
    // drain's injected remote magazine (0xFFFF without a v4 magazine).
    std::uint32_t store_writes, store_refused;
    StoreRefusal store_last;
    std::uint32_t mag_inj, mag_inj_cap;
    // F1: drain windows that isolated the sample rings, refusals (the last
    // reason; ring_on false after a refusal until the next isolated window),
    // and windows whose job left foreign ring pointers (restored anyway).
    bool ring_on;
    std::uint32_t ring_windows, ring_refused, ring_changed;
    RingRefusal ring_last;
    // Placement and the weapon gate: native windows that injected Neutral()
    // (Arm.neutral) and windows that stripped bits (Arm.strip, last bits).
    std::uint32_t neutral_windows, strip_windows, strip_bits;
    // Drains whose job ran without a window because the published Arm was
    // not an enabled native Arm (a disarm under the native drive).
    std::uint32_t drain_disarmed;
    // The partner's trigger after each windowed drain: shots requested
    // (rec+0x3C0 bit 24, summed over rec+0x3DF), drains with a shot, drains
    // that left the gun in a fire mode (rec+0x2C0 9 or 10).
    std::uint32_t fire_requested, fire_shot_drains, fire_modes;
    // The partner's own reload (R1/R4): RELOAD press edges the mirror saw
    // (written or not), drain windows the freeze held the mirror, reconcile
    // writes after a freeze and the rec+0x3E8 writes among them; the post-
    // phase-1 probe (inside the drain, right after the gun update: the reload
    // request bit 20 and the trigger's bit 24 live only there, phase 6 clears
    // them): phase-1 calls that left bit 20 / bit 24 set, the shots (sum of
    // rec+0x3DF), rec+0x2C0 changes across phase 1 and the last mode seen.
    std::uint32_t mag_reload_seen, mag_frozen, mag_reconciles, mag_m3e8_writes;
    std::uint32_t p1_b20, p1_b24, p1_shots, p1_mode_changes, p1_mode;
    // The slot follower (W4): writes (each switched instance 1's current
    // slot), refusals (once per episode and reason), the last verdict, the
    // last remote slot it acted on and whether a fault turned it off.
    std::uint32_t slot_writes, slot_refused, slot_target;
    SlotRefusal slot_last;
    bool slot_faulted;
    // D3: the look of the last windowed drain (the remote's, injected) and
    // instance 1's slot read back after its job (centidegrees {pitch, yaw};
    // look_ok false when either was unread), and the look source hook.
    bool look_ok;
    std::int32_t look_remote_cdeg[2], look_slot_cdeg[2];
    bool look_src_installed, look_src_faulted;
    std::uint32_t look_src_hits, look_src_skipped, look_src_refused, look_src_calls;
    // Anim round 1 (docs/anim-round1-weapons.md): rec+0x3E8 rewrites for a slot
    // change and for a base above the capacity (also in mag_m3e8_writes); the
    // aim intent (main windows whose live sample got HOLD from the v5 flag / the
    // act, windows the remote's own pad held it, the last source: AimSource);
    // the reload rate write (reloads seen with a factor, writes, refusals, the
    // last refusal RateRefusal, the native and written rate x1000 of the last
    // reload, a fault turned it off until MissionStart).
    std::uint32_t mag_m3e8_slot, mag_m3e8_cap;
    std::uint32_t aim_added_flag, aim_added_act, aim_pad_windows;
    std::uint8_t aim_last_source;
    std::uint32_t rate_reloads, rate_writes, rate_refused;
    std::uint8_t rate_last;
    std::int32_t rate_native_milli, rate_written_milli;
    bool rate_faulted;
};
// The reload rate write's refusals (NativeStats::rate_last).
enum class RateRefusal : std::uint8_t { none, off, prim, cls, native, fault };
const char* RateRefusalName(RateRefusal refusal) noexcept;

// Drained by the driver. Counters are process-lifetime totals (the driver
// logs deltas); us_max is cleared by each drain.
struct Stats {
    partner_input::WindowStats window;
    partner_input::HoldRecord holds[8];
    unsigned hold_count;
    WrapStats wrap;
    NativeStats native;
};
void DrainStats(Stats& out) noexcept;

// --- Native puppet (docs/native-puppet.md 5) ------------------------------------
// The thread that runs the main window (0x9C2B51, inside the ticker): 0 before
// the first call.
std::uint32_t HookThread() noexcept;
// The wrap roots for counting, independent of an enabled Arm (census needs
// them without input arming); census also turns the counting wrappers on.
void PublishWrapRoots(std::uintptr_t root0, std::uintptr_t root1, bool census) noexcept;
// Runs block 1's deferred job once on the hook thread inside a native window
// (Validate with root1, GO == P - 0x20 checks, `block` == the Arm's pool1 and
// never instance 0's live pool, no batch in flight anywhere, the frame
// injection's view for `consumer`, PRE incl. HOLD latch and isLocal, the
// phase-1 detour armed for (P, id 1, this thread), the job, POST).
// Returns true when the job ran windowed; when the Arm, validation, the block,
// a batch in flight or PRE refuses, the job still runs exactly once without a
// window (false): instance 0's job never sees instance 1's pad.
using JobCallFn = void (*)(void* go, const void* param) noexcept;
bool RunJobWindow(void* go, const void* param, std::uint32_t block, JobCallFn job, partner_input::Consumer consumer) noexcept;
// True while the 5 bytes at 0x9C2B51 still call our stub (1 Hz check).
bool PatchLive(const void* base) noexcept;
// Native rates: drop the mirror state without writing anything (MissionEnd /
// MissionStart: the record dies with the mission). Any thread; the hook
// thread applies it before its next rates step. `epoch` is the forgotten
// mission's rates epoch: an Arm still carrying it (published before the
// driver's disarm lands) never applies the mirror again.
void ForgetRates(std::uint32_t epoch) noexcept;


namespace detail {
// Test seams (also used by Install). BlockMatches ignores the retargetable
// rel32 operand; BuildStub writes the 14-byte absolute jmp.
bool BlockMatches(const unsigned char* block) noexcept;
void BuildStub(unsigned char out[14], const void* target) noexcept;
std::size_t BlockSize() noexcept;
// A 14-byte near stub (BuildStub) within rel32 reach of next_ip, then the
// rel32 of the E8 call at `site` (expected orig_rel) retargeted to it with a
// locked dword exchange (near_code::PatchDword). nullptr on success, else
// bytes | allocate_near | range | protect | exchange | cache_line |
// protect_restore; on protect_restore the patch IS live and stub_out is set.
const char* RetargetCall(std::uintptr_t site, std::uintptr_t next_ip, std::uint32_t orig_rel, const void* target,
                         void*& stub_out) noexcept;
// Which injected word a PP key-manager slot answers from (pure): +0xE8/+0x110/
// +0x118 pressed, +0xF0/+0x120/+0x128 released, +0x100/+0x108/+0x130/+0x138
// held, +0xF8 (km vt+0x148, not decoded) nothing.
enum class KmWord : std::uint8_t { held, press, release, zero, none };
KmWord KmSlotWord(std::size_t slot_offset) noexcept;
// Whether the slot takes a binding index (true) or a key handle (false).
bool KmSlotByIndex(std::size_t slot_offset) noexcept;
// --- Fixture seams (never called in the game) ---------------------------------
// The wrap originals and the hook thread, bound without patching anything.
using SvcFn = void (*)(void* ctx, void* root, std::uint32_t phase) noexcept;
using SvcIdFn = void (*)(void* ctx, void* root, std::uint32_t phase, std::uint32_t id) noexcept;
using WalkFn = void (*)(void* root, std::uint32_t phase) noexcept;
using WalkIdFn = void (*)(void* root, std::uint32_t phase, std::uint32_t id) noexcept;
void BindWrapForTest(SvcFn active, SvcFn all, SvcIdFn per_id, WalkFn walk7, WalkIdFn walk11, std::uint32_t hook_thread) noexcept;
// Calls the wrapper of entry e (0 svc28, 1 svc30, 2 svc40, 3 walk7, 4 walk11).
void CallWrapForTest(std::uint8_t entry, void* ctx, void* root, std::uint32_t phase, std::uint32_t id) noexcept;
// Simulates being inside a drain window (for the post-phase-0 look write).
void SetDrainForTest(const partner_input::Arm& arm, const partner_input::Injection& injection, bool in_drain) noexcept;
// After Install on a synthetic image: the original the main window calls
// (0xBF61D0) and the near stub at 0x9C2B51 (InputDispatchHook's entry).
void BindDispatchForTest(DispatchFn original) noexcept;
void* DispatchStubForTest() noexcept;
}

} // namespace mgscoop::partner_input_hook
