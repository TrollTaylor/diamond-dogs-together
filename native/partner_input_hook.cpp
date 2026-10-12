// Original MGS Co-op round 3/4 input lever: the block-1 context window hook.
// It wraps the single per-block phase-8 dispatch call at 0x9C2B51 (into
// 0xBF61D0). Around block 1's call only, with instance 1 realized and every
// identity re-validated, it swaps in instance 1's own virtual pad and sets the
// base plugin's per-id isLocal bit, then restores everything. Never blocks.
//
// Round 4 (docs/partner-anim-appearance.md "Round 4"): every window installs
// a PP vtable copy whose key-manager slots answer from the injected words and
// whose hold classifiers follow hold=none|pass|own|net; PP+0x14 carries the
// world heading; PP+0x278/PP+0x4B0 are shadowed; the camera sandbox covers the
// request block; wrap=census|window wraps the other walks of block 1's tree.
//
// Native puppet (docs/native-puppet.md 5): under a native Arm the main window
// composes one frame injection (with steering) that every consumer of the
// frame gets once with its edges and then as level only (drains of the two
// batch passes, walk7/walk11); every native window swaps the HOLD latch
// PP+0x27C bit 0 to the remote's; svc entries never open windows (block 1's
// job runs only inside drains) and no window opens while a hook-thread batch
// is in flight (a job run inline during the join would race block 0 on a
// worker); after phase 0 inside a drain the remote look angles are written
// into instance 1's own PS slot (naim=look|lookcam). RunJobWindow is the
// drain window partner_defer runs the deferred job in: it always sets
// isLocal (block 1's update phases 0/5/6 read the pad only then) and arms the
// phase-1 detour (partner_local_detour: phase 1 picks the pad-sampling local
// branch by id == P+0x218, not by isLocal) for exactly that job.
//
// Exceptions. Our own reads and writes (validation, PRE, POST) handle an
// access fault themselves: what PRE had saved is restored field by field and
// the window is disabled until the next MissionStart (ResetFault). An
// exception raised by the game's own update inside the window is recorded
// (phase=call) and passed on unchanged (EXCEPTION_CONTINUE_SEARCH): the game
// and crash_probe see it exactly as without the hook, and if an outer handler
// unwinds through the window, a __finally restores the swapped fields first.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include "mgscoop/nametag_game.hpp"
#include "mgscoop/teammate_targeting.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_aim_point.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_local_detour.hpp"
#include "mgscoop/partner_look_source.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/partner_rates.hpp"
#include "mgscoop/partner_reload.hpp"
#include "partner_player_contract.hpp"
#include <MinHook.h>
#include <intrin.h>
#include "mgscoop/native_pose_window.hpp"
#include "native_pose_exclusion.hpp"
#include "camera_mode_window.hpp"
#include "carry_model_lifetime_contract.hpp"
#include "carry_model_retirement_contract.hpp"
#include "native_pose_window_contract.hpp"
#include "native_pose_birth.hpp"
#include "native_pose_birth_contract.hpp"
#include "whole_player_lifetime_contract.hpp"
#include "whole_player_lifetime_checks.hpp"
#include "native_pose_selected_export.hpp"
#include "native_pose_camera_reader.hpp"
#include "carry_vars_pair_word_window.hpp"
#include "owned_flag_map.hpp"
#include "native_pose_message_lifetime.hpp"
#include "mgscoop/native_pose_local_carry_slice.hpp"
#include "native_pose_vars_bulk_reset_contract.hpp"
#include <new>
#include "native_pose_reject_diagnostic.hpp"
#include "native_pose_teardown_diagnostic.hpp"
#include "native_pose_parts_diagnostic.hpp"
#include "mgscoop/native_pose_sleep_parts.hpp"
#include "sleep_parts_policy.hpp"
#include "sleep_parts_contract.hpp"
#include "mgscoop/native_pose_dual_parts.hpp"
#include "dual_parts_core_policy.hpp"
#include "dual_parts_local_profile.hpp"
#include "dual_parts_cold_profile.hpp"
#include "dual_parts_local_profile_contract.hpp"
#include "native_pose_hold_owners.hpp"
#include "native_pose_carry_participants.hpp"
#include "carry_plugin_contract.hpp"
#include "carry_owned_handshake_contract.hpp"
#include "carry_dispatch_lifetime.hpp"
#include "native_pose_hook_groups.hpp"
#include "mgscoop/native_pose_carry_cold.hpp"
#include "mgscoop/native_pose_dispatch_lifetime.hpp"
#include "carry_request_retirement_contract.hpp"

namespace mgscoop::partner_input_hook {
namespace {
namespace c = mgscoop::partner_player_contract;
namespace pi = mgscoop::partner_input;
namespace pad = mgscoop::partner_pad;
// The named pad bits (partner_pad_bits.hpp, the masks) are the pinned PlayerPad enum values.
static_assert(c::pad_stance == pad::kStance && c::pad_dash == pad::kDash && c::pad_evade == pad::kEvade &&
                  c::pad_vehicle_dash == pad::kKbDash && c::pad_hold == pad::kHold && c::pad_fire == pad::kFire &&
                  c::pad_action == pad::kAction && c::pad_reload == pad::kReload && c::pad_side_roll == pad::kSideRoll,
              "partner_pad_bits.hpp matches the pinned PlayerPad enum");
// The magazine mirror's edge bits and limits are the same pad bits and wire limits.
static_assert(mgscoop::partner_native::kPadFire == pad::kFire && mgscoop::partner_native::kPadReload == pad::kReload &&
                  mgscoop::partner_native::kMaxMagCap == pi::kMaxMagCap,
              "weapons pad bits and magazine limit match partner_pad_bits.hpp and the input frame");

// Process-level state (the DLL is pinned; nothing here is ever torn down).
volatile LONG g_installed = 0;
bool g_ready = false;
std::uintptr_t g_base = 0;
LogFn g_log = nullptr;
SampleFn g_sample = nullptr;
HudBeforeFn g_hud_before = nullptr;
HudAfterFn g_hud_after = nullptr;
DispatchFn g_original = nullptr;
HoldFn g_hold_orig = nullptr;
void* g_stub = nullptr;
// 43 slots (slot -1 included); the new vtable pointer is &table[1]. Slot -1
// of the PP vtable (RVA 0x23C8D38) is 0 in the image (no RTTI locator), so
// table[0] cannot tell whether a copy exists: g_vt_ready does.
std::uintptr_t g_vt_none[43]{};
std::uintptr_t g_vt_log[43]{};
std::uintptr_t g_vt_own[43]{};
std::uintptr_t g_vt_pass[43]{};
bool g_vt_ready = false;

// Seqlock-published Arm.
volatile LONG g_seq = 0;
pi::Arm g_arm{};
volatile LONG g_arm_enabled = 0;
volatile LONG g_holdlog = 0;
// Fast filters for the wrap entries (written by PublishArm).
volatile LONG g_wrap_mode = 0;  // PartnerWrap
volatile LONG64 g_wrap_root0 = 0, g_wrap_root1 = 0;
// Odd while the hook has anything swapped into PP/src/rec/CAM/P (the Lua
// thread's pad reads retry or refuse while it is odd or changes).
volatile LONG g_window_seq = 0;

// Hook-thread-only edge/tap/sample state (survives gen bumps; see the Arm
// comment in partner_input.hpp) and instance 1's private masks and shadows
// (reset per gen).
pi::EdgeState g_edge{};
pi::TapState g_tap{};
pi::Sample g_prev{};
std::uint32_t g_gen = 0;
bool g_priv_valid = false;
unsigned char g_priv_masks[c::pp_masks_size + c::pp_mask_tables_size]{};
bool g_shadow_valid = false;
std::uint32_t g_shadow_gate = 0;   // PP+0x278 as instance 1 sees it
std::uint8_t g_shadow_rebind = 0;  // PP+0x4B0 as instance 1 sees it
pi::HoldHistory g_hist{};
volatile LONG g_reset_hist = 0;  // set by ResetFault (Lua thread), consumed by the hook thread
// Hook thread: no block-1 window ran since the last disarm (or ever). The
// next window resyncs the level and drops the edge sets accumulated meanwhile.
bool g_need_resync = true;
std::uint64_t g_last_compose_ms = 0;  // the last composed main window (gap -> resync)
// The main window's injection of the current frame (reused by wrap windows).
pi::Injection g_last_inj{};
bool g_inj_valid = false;
// Native puppet (hook thread only): the frame injection's id and time, each
// consumer's last frame id, the drain window and the look write's inputs.
std::uint32_t g_frame_id = 0;
std::uint64_t g_frame_ms = 0;
std::uint32_t g_consumer_last[pi::kConsumers]{};
bool g_in_drain = false;
pi::Injection g_drain_inj{};
pi::Arm g_drain_arm{};
volatile LONG g_wrap_floor = 0;   // PartnerWrap counting without an enabled Arm (census)
volatile LONG g_wrap_native = 0;  // the enabled Arm is native
volatile LONG g_n_frames = 0, g_n_views[3] = {0, 0, 0}, g_n_drain_windows = 0, g_n_drain_refused = 0, g_n_walk_windows = 0;
volatile LONG g_n_latch_other = 0, g_n_latch_last = 0, g_n_look_writes = 0, g_n_look_skipped = 0, g_n_look_refused = 0;
volatile LONG g_n_look_last = 0, g_n_look_faulted = 0, g_n_camsub_dirty = 0, g_n_lookcam = 0;
volatile LONG g_n_drain_block = 0, g_n_drain_in_batch = 0;
volatile LONG g_w_in_batch = 0, g_w_svc_native = 0;
// The last windowed drain (diagnostics): the injection it got, instance 1's
// private PP masks after it, isLocal before the window set it, and instance
// 1's base action record after its update (act, next act, sampled move
// magnitude x1000 and heading in mrad; rec_ok 0 when the read faulted).
volatile LONG g_d_held = 0, g_d_mag = 0, g_d_view = 0, g_d_mask28 = 0, g_d_mask2c = 0, g_d_mask30 = 0, g_d_local_pre = 0;
volatile LONG g_d_rec_ok = 0, g_d_rec_act = 0, g_d_rec_next = 0, g_d_rec_mag = 0, g_d_rec_heading = 0;
// Native rates mirror (hook thread only, docs/native-puppet.md "Rates"): the
// state of instance 1's record, the record it applies to (with the chain it
// was validated through: P, instance 1's block and pool) and its epoch; the
// forget request and the epoch it forgets (any thread), the epoch the hook
// thread blocked (a forgotten mission's Arm never applies again) and the
// published statistics.
namespace prt = mgscoop::partner_rates;
prt::MirrorState g_rates{};
std::uintptr_t g_rates_sp = 0, g_rates_rec = 0, g_rates_p = 0, g_rates_blk = 0, g_rates_p_vt = 0;
std::uint32_t g_rates_id = 0, g_rates_epoch = 0, g_rates_blocked = 0;
std::uint8_t g_rates_pool = 0;
volatile LONG g_rates_forget = 0, g_rates_forget_epoch = 0;
volatile LONG g_n_rates_applied = 0, g_n_rates_latched = 0, g_n_rates_reason = 0, g_n_rates_writes = 0, g_n_rates_reapplied = 0;
volatile LONG g_n_rates_restores = 0, g_n_rates_drops = 0, g_n_rates_w[3] = {0, 0, 0};
// Native weapons: the magazine mirror (hook thread writes; atomics for the
// driver) and the shoot-count leak check of drain windows.
volatile LONG g_m_writes = 0, g_m_refused = 0, g_m_fire = 0, g_m_reload = 0, g_m_last = 0, g_m_faulted = 0;
volatile LONG g_m_remote = 0xFFFF, g_m_remote_cap = 0xFFFF, g_m_cap = 0xFFFF, g_m_written = 0xFFFF;
volatile LONG g_shot_leaks = 0, g_shot_count = -1, g_reload_drains = 0;
// The same for the viewer's weapon/ammo vars block (FNV-1a digest; 0 unread).
// An ammo leak under the magazine mirror turns the mirror off until
// MissionStart (it is what lets instance 1 reload): g_m_leak.
volatile LONG g_ammo_leaks = 0, g_ammo_digest = 0, g_m_leak = 0;
partner_native::MagazineHistory g_mag_history{};  // hook thread only: checked, accepted samples
// F2: the store row (atomics for the driver) and the last drain's injected magazine.
volatile LONG g_s_writes = 0, g_s_refused = 0, g_s_last = 0, g_m_inj = 0xFFFF, g_m_inj_cap = 0xFFFF;
// F1: the private sample rings of drain windows (hook thread only) and the
// previous held word each drain consumer injected (the semi-auto trigger's
// older entry of a gamepad remote).
std::uint32_t g_ring_held[kRingEntries]{};
float g_ring_f[6][kRingEntries]{};
std::uint32_t g_ring_prev[pi::kConsumers]{};
volatile LONG g_r_on = 0, g_r_windows = 0, g_r_refused = 0, g_r_changed = 0, g_r_last = 0;
// Placement and the weapon gate.
volatile LONG g_n_neutral = 0, g_n_strip = 0, g_n_strip_bits = 0, g_n_drain_disarmed = 0;
// The partner's own reload (R1/R4): RELOAD edges seen, frozen drains,
// reconciles and rec+0x3E8 writes (atomics for the driver); hook thread only:
// the pending reconcile and last accepted slot live in g_mag_history.
volatile LONG g_m_reload_seen = 0, g_m_frozen = 0, g_m_reconciles = 0, g_m_3e8 = 0;
// The post-phase-1 probe (R1).
volatile LONG g_p1_b20 = 0, g_p1_b24 = 0, g_p1_shots = 0, g_p1_modes = 0, g_p1_mode = 0;
// The slot follower (W4): statistics (atomics) and the episode it acts in
// (hook thread only: the remote's slot and switch counter, the last counted
// reason and the writes spent in it).
volatile LONG g_sl_writes = 0, g_sl_refused = 0, g_sl_last = 0, g_sl_target = 0xFF, g_sl_faulted = 0;
std::uint32_t g_sl_ep_target = 0xFFFFFFFFu, g_sl_ep_seq = 0xFFFFFFFFu, g_sl_ep_writes = 0;
LONG g_sl_ep_reason = -1;
// The partner's trigger after each windowed drain (shots requested, drains with one, drains in a fire mode).
volatile LONG g_f_requested = 0, g_f_shot_drains = 0, g_f_modes = 0;
// D3: the last windowed drain's look (centidegrees; INT32_MIN unread).
volatile LONG g_l_ok = 0, g_l_remote[2] = {INT32_MIN, INT32_MIN}, g_l_slot[2] = {INT32_MIN, INT32_MIN};
// What the stubs answer during the current window (set before each call).
struct Current {
    std::uint32_t held, press, release, w54, long12, tap12;
    bool live;
    PartnerInputHold hold;
};
Current g_cur{};

// Statistics (atomics).
volatile LONG g_faulted = 0;
volatile LONG g_windows = 0, g_root1 = 0, g_skips = 0, g_stale = 0, g_faults = 0, g_hold_calls = 0, g_cam_restored = 0;
volatile LONG g_cam_dirty = 0, g_us_max = 0, g_hook_thread = 0, g_gen_seen = 0;
volatile LONG g_last_skip = 0, g_last_held = 0, g_last_lx = 0, g_last_ly = 0, g_last_dir = 0, g_last_mag = 0;
volatile LONG g_last_age = static_cast<LONG>(pi::kNoSample), g_last_stale = 0;
volatile LONG g_fault_phase = 0, g_fault_code = 0;
volatile LONG g_hold_own[4] = {0, 0, 0, 0};
volatile LONG g_km_calls = 0, g_synth = 0, g_press_n = 0, g_release_n = 0, g_shadow_changes = 0;
// Hold log ring (block 0 only).
pi::HoldRecord g_hold_ring[64]{};
volatile LONG g_hold_head = 0;
// Wrap entries.
volatile LONG g_wrap_installed = 0;
bool g_wrap_ready = false;
volatile LONG g_w_calls0[kWrapEntries]{}, g_w_calls1[kWrapEntries]{}, g_w_same[kWrapEntries]{}, g_w_nested[kWrapEntries]{};
volatile LONG g_w_windows[kWrapEntries]{}, g_w_phases[kWrapEntries]{};
volatile LONG g_w_other = 0;
using detail::SvcFn;
using detail::SvcIdFn;
using detail::WalkFn;
using detail::WalkIdFn;
SvcFn g_svc_active = nullptr, g_svc_all = nullptr;
SvcIdFn g_svc_id = nullptr;
WalkFn g_walk7 = nullptr;
WalkIdFn g_walk11 = nullptr;
void* g_walk7_stub = nullptr;
void* g_walk11_stub = nullptr;

std::uint64_t NowMs() noexcept {
    LARGE_INTEGER f{}, cc{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0 || !QueryPerformanceCounter(&cc)) return GetTickCount64();
    return static_cast<std::uint64_t>(cc.QuadPart) / static_cast<std::uint64_t>(f.QuadPart) * 1000 +
           (static_cast<std::uint64_t>(cc.QuadPart) % static_cast<std::uint64_t>(f.QuadPart)) * 1000 /
               static_cast<std::uint64_t>(f.QuadPart);
}
std::uint64_t NowUs() noexcept {
    LARGE_INTEGER f{}, cc{};
    if (!QueryPerformanceFrequency(&f) || f.QuadPart <= 0 || !QueryPerformanceCounter(&cc)) return GetTickCount64() * 1000;
    return static_cast<std::uint64_t>(cc.QuadPart) / static_cast<std::uint64_t>(f.QuadPart) * 1000000 +
           (static_cast<std::uint64_t>(cc.QuadPart) % static_cast<std::uint64_t>(f.QuadPart)) * 1000000 /
               static_cast<std::uint64_t>(f.QuadPart);
}

void Max(volatile LONG& slot, LONG value) noexcept {
    LONG seen = slot;
    while (value > seen) {
        const LONG prev = InterlockedCompareExchange(&slot, value, seen);
        if (prev == seen) break;
        seen = prev;
    }
}

volatile LONG g_shutdown = 0;  // set once at window close; never cleared
bool ReadArm(pi::Arm& out) noexcept {
    if (g_shutdown) return false;
    for (int tries = 0; tries < 2; ++tries) {
        const LONG s1 = g_seq;
        if (s1 & 1) continue;
        MemoryBarrier();
        std::memcpy(&out, const_cast<const pi::Arm*>(&g_arm), sizeof(out));
        MemoryBarrier();
        if (g_seq == s1) return true;
    }
    return false;
}

bool BeginWindow(const pi::Arm* arm=nullptr) noexcept;
void EndWindow() noexcept;
bool PoseOwned() noexcept;
bool PoseRequired() noexcept;
bool PoseHeld() noexcept;
void PoseDispatchOriginal(void* root,std::uint32_t phase) noexcept;
void __stdcall InputDispatchHook(void* root,std::uint32_t phase) noexcept;
void WrapSvcActive(void*,void*,std::uint32_t) noexcept;
void WrapSvcAll(void*,void*,std::uint32_t) noexcept;
void WrapSvcId(void*,void*,std::uint32_t,std::uint32_t) noexcept;
void WrapWalk7(void*,std::uint32_t) noexcept;
void WrapWalk11(void*,std::uint32_t,std::uint32_t) noexcept;

template <class T>
T Load(std::uintptr_t a) noexcept { T v{}; std::memcpy(&v, reinterpret_cast<const void*>(a), sizeof(v)); return v; }
template <class T>
void Store(std::uintptr_t a, T v) noexcept { std::memcpy(reinterpret_cast<void*>(a), &v, sizeof(v)); }

void Fault(unsigned phase, std::uint32_t code) noexcept {
    if (InterlockedExchange(&g_faulted, 1) == 0) {
        InterlockedExchange(&g_fault_phase, static_cast<LONG>(phase));
        InterlockedExchange(&g_fault_code, static_cast<LONG>(code));
        InterlockedIncrement(&g_faults);
        if (g_log) {
            char line[128]{};
            if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_input=fault; phase=%s; code=0x%08x; native_input=disabled\r\n",
                                           pi::FaultPhaseName(static_cast<std::uint8_t>(phase)), code)))
                g_log(line);
        }
    }
}

bool IsAccessFault(DWORD code) noexcept { return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR; }
// The codes that mean "the game's update broke" (never a C++ or debugger exception).
bool IsFatalCode(DWORD code) noexcept {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_IN_PAGE_ERROR: case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_INT_OVERFLOW:
    case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: case EXCEPTION_DATATYPE_MISALIGNMENT:
        return true;
    default:
        return false;
    }
}
// Our own reads/writes: an access fault is ours to handle; anything else is
// not (passed on).
int OwnFilter(unsigned phase, DWORD code) noexcept {
    if (!IsAccessFault(code)) return EXCEPTION_CONTINUE_SEARCH;
    Fault(phase, code);
    return EXCEPTION_EXECUTE_HANDLER;
}
// The game's own update: record a fatal code, never swallow anything.
int GameFilter(DWORD code) noexcept {
    if (IsFatalCode(code)) Fault(2, code);
    return EXCEPTION_CONTINUE_SEARCH;
}

// The identities every swap relies on (direct reads inside the caller's
// __try): the player slot, P's vtable, flags (online and alternate dispatch
// clear), no synchronizer, count 2 / local index 0 / Q vtable, PP and src.
pi::Skip ValidatePad(const pi::Arm& a) noexcept {
    if (Load<std::uintptr_t>(g_base + c::player_slot_rva) != a.P) return pi::Skip::player;
    if (Load<std::uintptr_t>(a.P) != a.p_vtable) return pi::Skip::p_vtable;
    const std::uint32_t flags = Load<std::uint32_t>(a.P + c::p_flags);
    if (flags & (c::flag_online | c::flag_alt_dispatch)) return pi::Skip::flags;
    if (Load<std::uintptr_t>(a.P + c::p_sync) != 0) return pi::Skip::sync;
    if (Load<std::uint32_t>(a.P + c::p_count) != 2 || Load<std::uint32_t>(a.P + c::p_local) != 0) return pi::Skip::state;
    const std::uintptr_t q = Load<std::uintptr_t>(a.P + c::p_q);
    if (!q || Load<std::uintptr_t>(q) != g_base + c::q_vtable_rva) return pi::Skip::state;
    if (Load<std::uintptr_t>(a.P + c::p_pad) != a.pp || Load<std::uintptr_t>(a.pp) != a.pp_vtable) return pi::Skip::pp;
    if (Load<std::uintptr_t>(a.pp + c::pp_src) != a.src) return pi::Skip::src;
    return pi::Skip::none;
}

// Full block-1 validation (inside the caller's __try).
pi::Skip Validate(const pi::Arm& a, void* root) noexcept {
    if (const pi::Skip skip = ValidatePad(a); skip != pi::Skip::none) return skip;
    if (Load<std::uintptr_t>(a.P + c::p_cam) != a.cam_object || Load<std::uintptr_t>(a.cam_object) != a.cam_vtable)
        return pi::Skip::cam;
    const std::uintptr_t ids = Load<std::uintptr_t>(a.P + c::p_ids);
    const std::uintptr_t pool = Load<std::uintptr_t>(a.P + c::p_pool);
    const std::uintptr_t parts = Load<std::uintptr_t>(a.P + c::p_parts);
    if (Load<std::uint16_t>(ids + 2) != 1 || Load<std::uint8_t>(pool + 1) != a.pool1 ||
        Load<std::uint8_t>(parts + 1) != c::parts_loaded_state) return pi::Skip::ids;
    const std::uintptr_t blocks = Load<std::uintptr_t>(a.P + c::p_blocks);
    const std::uintptr_t blk = blocks + static_cast<std::uintptr_t>(a.pool1) * c::block_stride;
    if (blk != a.blk1 || Load<std::uintptr_t>(blk + c::blk_root) != reinterpret_cast<std::uintptr_t>(root)) return pi::Skip::root;
    static constexpr std::size_t off[4] = {0x0, 0x10, 0x30, 0x48};
    for (unsigned i = 0; i < 4; ++i)
        if (Load<std::uintptr_t>(a.plugin1 + off[i]) != a.plugin_vt[i]) return pi::Skip::plugin;
    const std::uintptr_t node = Load<std::uintptr_t>(a.plugin1 + c::plugin_node);
    const std::uint32_t base_id = Load<std::uint32_t>(node + c::node_base);
    const std::uintptr_t rec = Load<std::uintptr_t>(a.plugin1 + c::plugin_recs) + static_cast<std::uintptr_t>(1 - base_id) * c::plugin_rec_stride;
    if (rec != a.rec1) return pi::Skip::rec;
    return pi::Skip::none;
}

// Everything the window overwrites, each with its own saved flag: a restore
// writes back exactly the fields PRE had saved (a PRE fault midway restores
// the prefix, never zeros).
struct Save {
    unsigned char src[c::src_size];
    unsigned char derived[c::pp_derived_size];
    unsigned char host_masks[c::pp_masks_size + c::pp_mask_tables_size];
    unsigned char kb[c::pp_kb_size];
    std::uint8_t kb_flags;
    std::uintptr_t vt;
    std::uint32_t rec_2ac;
    std::uint32_t p218;
    std::uint32_t gate;
    std::uint8_t rebind;
    unsigned char cam[pi::kCamSaveBytes];
    bool src_saved, derived_saved, masks_saved, kb_saved, kb_flags_saved, vt_saved, rec_saved, p218_saved, cam_saved, shadow_saved;
    // Native windows: the HOLD latch PP+0x27C, the aim sub-controller ranges
    // (CamSub: [sub+0x3C, +0x48) and [sub+0xB8, +0xBC) of *(CAM+0x360)) and
    // range 6 as it was after our lookcam write.
    std::uint32_t latch, latch_written;
    bool latch_saved;
    std::uintptr_t camsub_ptr;
    unsigned char camsub[0x10];
    bool camsub_saved;
    unsigned char cam6_after[0x20];
    bool lookcam_written;
    // F1 (drain windows): the src ring pointers src+0x08..+0x3F and the count src+0x44.
    std::uintptr_t ring_ptr[7];
    std::uint32_t ring_count;
    bool rings_saved;
};
// What a window does beyond the round-3/4 swap.
struct WindowOpts {
    bool set_local;  // isLocal (rec+0x2AC bit 0)
    bool native;     // HOLD latch, CamSub (aim stage), lookcam
    bool rings;      // F1: the private sample rings (drain windows)
    std::uint8_t consumer;
};
// What a native window injects: the composed view, or Neutral() while the
// body is placed (Arm.neutral), without the weapon gate's bits (Arm.strip).
pi::Injection NativeView(const pi::Arm& a, const pi::Injection& j) noexcept {
    if (PoseHeld()) return pi::Neutral(j);
    if (!a.native) return j;
    if (a.neutral) {
        InterlockedIncrement(&g_n_neutral);
        return pi::Neutral(j);
    }
    if (a.strip) {
        InterlockedIncrement(&g_n_strip);
        InterlockedExchange(&g_n_strip_bits, static_cast<LONG>(a.strip));
        return pi::Strip(j, a.strip);
    }
    return j;
}
float FloatOr0(float v) noexcept { return v == v ? v : 0.0f; }
inline constexpr std::size_t kCam6 = 6;  // kCamRanges[6] = {0x270, 0x290}

// The vtable copy a window installs for a hold mode.
std::uintptr_t WindowVtable(PartnerInputHold hold) noexcept {
    switch (hold) {
    case PartnerInputHold::none: return reinterpret_cast<std::uintptr_t>(&g_vt_none[1]);
    case PartnerInputHold::pass: return reinterpret_cast<std::uintptr_t>(&g_vt_pass[1]);
    default: return reinterpret_cast<std::uintptr_t>(&g_vt_own[1]);
    }
}

void ApplyPre(const pi::Arm& a, const pi::Injection& j, Save& save, const WindowOpts& o) noexcept {
    std::memcpy(save.src, reinterpret_cast<void*>(a.src + c::src_words), c::src_size);
    save.src_saved = true;
    Store<std::uint32_t>(a.src + 0x48, j.held);
    Store<std::uint32_t>(a.src + 0x4C, j.w[0]);
    Store<std::uint32_t>(a.src + 0x50, j.w[1]);
    Store<std::uint32_t>(a.src + 0x54, j.w[2]);
    Store<float>(a.src + 0x58, j.lx); Store<float>(a.src + 0x5C, j.ly);
    Store<float>(a.src + 0x60, j.rx); Store<float>(a.src + 0x64, j.ry);
    Store<float>(a.src + 0x68, j.lt); Store<float>(a.src + 0x6C, j.rt);
    std::memcpy(save.derived, reinterpret_cast<void*>(a.pp + c::pp_derived), c::pp_derived_size);
    save.derived_saved = true;
    // PP+0x14 is the world move heading (0x12070F9), not a 0..1 value.
    Store<float>(a.pp + 0x10, j.mag); Store<float>(a.pp + 0x14, j.heading); Store<float>(a.pp + 0x18, j.dir);
    Store<float>(a.pp + 0x1C, j.rmag); Store<float>(a.pp + 0x20, j.rforce); Store<float>(a.pp + 0x24, j.rdir);
    // Masks: save the host's, write instance 1's private copy (zero on a new gen).
    std::memcpy(save.host_masks, reinterpret_cast<void*>(a.pp + c::pp_masks), c::pp_masks_size);
    std::memcpy(save.host_masks + c::pp_masks_size, reinterpret_cast<void*>(a.pp + c::pp_mask_tables), c::pp_mask_tables_size);
    save.masks_saved = true;
    std::memcpy(reinterpret_cast<void*>(a.pp + c::pp_masks), g_priv_masks, c::pp_masks_size);
    std::memcpy(reinterpret_cast<void*>(a.pp + c::pp_mask_tables), g_priv_masks + c::pp_masks_size, c::pp_mask_tables_size);
    std::memcpy(save.kb, reinterpret_cast<void*>(a.pp + c::pp_kb), c::pp_kb_size);
    save.kb_saved = true;
    std::memset(reinterpret_cast<void*>(a.pp + c::pp_kb), 0, c::pp_kb_size);
    save.kb_flags = Load<std::uint8_t>(a.pp + c::pp_kb_flags);
    save.kb_flags_saved = true;
    Store<std::uint8_t>(a.pp + c::pp_kb_flags, static_cast<std::uint8_t>(save.kb_flags & ~c::kb_flags_clear));
    // Round 4: the vtable copy is installed in every window (key-manager
    // slots answer from the injected words; hold classifiers per hold mode).
    if (g_vt_ready) {
        save.vt = Load<std::uintptr_t>(a.pp);
        save.vt_saved = true;
        Store<std::uintptr_t>(a.pp, WindowVtable(a.hold));
    }
    // Shadowed PP fields: instance 1's own copy (initialized from the host's
    // value on the first window of a gen).
    save.gate = Load<std::uint32_t>(a.pp + c::pp_mouse_gate);
    save.rebind = Load<std::uint8_t>(a.pp + c::pp_rebind);
    save.shadow_saved = true;
    if (!g_shadow_valid) {
        g_shadow_gate = save.gate;
        g_shadow_rebind = save.rebind;
        g_shadow_valid = true;
    }
    Store<std::uint32_t>(a.pp + c::pp_mouse_gate, g_shadow_gate);
    Store<std::uint8_t>(a.pp + c::pp_rebind, g_shadow_rebind);
    if (o.set_local) {
        save.rec_2ac = Load<std::uint32_t>(a.rec1 + c::rec_local);
        save.rec_saved = true;
        Store<std::uint32_t>(a.rec1 + c::rec_local, save.rec_2ac | 1u);
    }
    if (o.native) {
        // Gun eligibility (0x1054210) reads PP+0x27C bit 0: the latch of the
        // injected "now" word under instance 1's private PP+0x28.
        std::uint32_t mask28 = 0;
        std::memcpy(&mask28, g_priv_masks, sizeof(mask28));
        save.latch = Load<std::uint32_t>(a.pp + c::pp_hold_latch);
        save.latch_saved = true;
        save.latch_written = partner_native::Latch27c(save.latch, j.w[0], mask28);
        Store<std::uint32_t>(a.pp + c::pp_hold_latch, save.latch_written);
        InterlockedExchange(&g_n_latch_last, static_cast<LONG>(save.latch_written & 1u));
    }
    if (a.cam != PartnerInputCam::off) {
        std::size_t at = 0;
        for (const pi::CamRange& r : pi::kCamRanges) {
            const std::size_t n = static_cast<std::size_t>(r.end - r.begin);
            std::memcpy(save.cam + at, reinterpret_cast<void*>(a.cam_object + r.begin), n);
            at += n;
        }
        save.cam_saved = true;
        if (o.native && a.naim != PartnerNativeAim::off) {
            // Aim init's writes through *(CAM+0x360) (sub+0x3C..+0x48, sub+0xB8).
            const std::uintptr_t sub = Load<std::uintptr_t>(a.cam_object + c::cam_aim_sub);
            if (sub) {
                std::memcpy(save.camsub, reinterpret_cast<void*>(sub + c::cam_sub_begin), c::cam_sub_end - c::cam_sub_begin);
                std::memcpy(save.camsub + 0xC, reinterpret_cast<void*>(sub + c::cam_sub_b8), c::cam_sub_bc - c::cam_sub_b8);
                save.camsub_ptr = sub;
                save.camsub_saved = true;
            }
        }
        if (o.native && a.naim == PartnerNativeAim::lookcam && j.cam_valid && a.cam == PartnerInputCam::sandbox) {
            Store<float>(a.cam_object + c::cam270, j.cam270);
            Store<float>(a.cam_object + c::cam274, j.cam274);
            std::memcpy(save.cam6_after, reinterpret_cast<void*>(a.cam_object + pi::kCamRanges[kCam6].begin), sizeof(save.cam6_after));
            save.lookcam_written = true;
            InterlockedIncrement(&g_n_lookcam);
        }
    }
    if (a.p218 || (o.native && PoseRequired() && PoseOwned())) {
        save.p218 = Load<std::uint32_t>(a.P + c::p_local_id);
        save.p218_saved = true;
        Store<std::uint32_t>(a.P + c::p_local_id, 1u);
    }
    // F1: the trigger's history path (0x1096171) and the right-stick history
    // (0x112344F) read the src sample rings and the samples-of-this-update
    // count, not the injected words: point them at instance 1's own rings
    // (count 1: entry 0 the injected level, the older entries the previous
    // drain's held word of a gamepad remote, else 0 = keyboard semantics).
    if (o.rings) {
        const std::uint32_t cap = Load<std::uint32_t>(a.src + c::src_ring_cap);
        if (cap < 2 || cap > kRingEntries) {
            InterlockedIncrement(&g_r_refused);
            InterlockedExchange(&g_r_last, static_cast<LONG>(RingRefusal::cap));
            InterlockedExchange(&g_r_on, 0);
        } else {
            std::memcpy(save.ring_ptr, reinterpret_cast<const void*>(a.src + c::src_rings), sizeof(save.ring_ptr));
            save.ring_count = Load<std::uint32_t>(a.src + c::src_ring_new);
            save.rings_saved = true;
            const std::uint32_t older = j.gamepad && o.consumer < pi::kConsumers ? g_ring_prev[o.consumer] : 0u;
            g_ring_held[0] = j.held;
            const float level[6] = {FloatOr0(j.lx), FloatOr0(j.rx), FloatOr0(j.ly), FloatOr0(j.ry), FloatOr0(j.lt), FloatOr0(j.rt)};
            for (std::uint32_t i = 1; i < kRingEntries; ++i) g_ring_held[i] = older;
            for (unsigned r = 0; r < 6; ++r)
                for (std::uint32_t i = 0; i < kRingEntries; ++i) g_ring_f[r][i] = level[r];
            std::uintptr_t ptr[7] = {reinterpret_cast<std::uintptr_t>(g_ring_held)};
            for (unsigned r = 0; r < 6; ++r) ptr[1 + r] = reinterpret_cast<std::uintptr_t>(g_ring_f[r]);
            std::memcpy(reinterpret_cast<void*>(a.src + c::src_rings), ptr, sizeof(ptr));
            Store<std::uint32_t>(a.src + c::src_ring_new, 1u);
            InterlockedIncrement(&g_r_windows);
            InterlockedExchange(&g_r_on, 1);
            InterlockedExchange(&g_r_last, static_cast<LONG>(RingRefusal::none));
        }
    }
}

// Exact reverse of ApplyPre for the saved fields. capture_private keeps the
// masks and shadows instance 1's update left as its private copy (only after
// a real call).
void ApplyPost(const pi::Arm& a, const Save& save, bool capture_private) noexcept {
    if (save.rings_saved) {
        std::uintptr_t live[7]{};
        std::memcpy(live, reinterpret_cast<const void*>(a.src + c::src_rings), sizeof(live));
        if (live[0] != reinterpret_cast<std::uintptr_t>(g_ring_held)) InterlockedIncrement(&g_r_changed);
        std::memcpy(reinterpret_cast<void*>(a.src + c::src_rings), save.ring_ptr, sizeof(save.ring_ptr));
        Store<std::uint32_t>(a.src + c::src_ring_new, save.ring_count);
    }
    if (save.p218_saved) Store<std::uint32_t>(a.P + c::p_local_id, save.p218);
    if (save.rec_saved) {
        const std::uint32_t now = Load<std::uint32_t>(a.rec1 + c::rec_local);
        Store<std::uint32_t>(a.rec1 + c::rec_local, (now & ~1u) | (save.rec_2ac & 1u));
    }
    if (save.shadow_saved) {
        if (capture_private) {
            const std::uint32_t gate = Load<std::uint32_t>(a.pp + c::pp_mouse_gate);
            const std::uint8_t rebind = Load<std::uint8_t>(a.pp + c::pp_rebind);
            if (gate != g_shadow_gate || rebind != g_shadow_rebind) InterlockedIncrement(&g_shadow_changes);
            g_shadow_gate = gate;
            g_shadow_rebind = rebind;
        }
        Store<std::uint32_t>(a.pp + c::pp_mouse_gate, save.gate);
        Store<std::uint8_t>(a.pp + c::pp_rebind, save.rebind);
    }
    if (save.latch_saved) {
        const std::uint32_t now = Load<std::uint32_t>(a.pp + c::pp_hold_latch);
        if ((now ^ save.latch_written) & ~1u) InterlockedIncrement(&g_n_latch_other);
        Store<std::uint32_t>(a.pp + c::pp_hold_latch, partner_native::LatchRestore(now, save.latch));
    }
    if (save.vt_saved) Store<std::uintptr_t>(a.pp, save.vt);
    if (save.masks_saved) {
        if (capture_private) {
            std::memcpy(g_priv_masks, reinterpret_cast<void*>(a.pp + c::pp_masks), c::pp_masks_size);
            std::memcpy(g_priv_masks + c::pp_masks_size, reinterpret_cast<void*>(a.pp + c::pp_mask_tables), c::pp_mask_tables_size);
            g_priv_valid = true;
        }
        std::memcpy(reinterpret_cast<void*>(a.pp + c::pp_masks), save.host_masks, c::pp_masks_size);
        std::memcpy(reinterpret_cast<void*>(a.pp + c::pp_mask_tables), save.host_masks + c::pp_masks_size, c::pp_mask_tables_size);
    }
    if (save.kb_flags_saved) Store<std::uint8_t>(a.pp + c::pp_kb_flags, save.kb_flags);
    if (save.kb_saved) std::memcpy(reinterpret_cast<void*>(a.pp + c::pp_kb), save.kb, c::pp_kb_size);
    if (save.derived_saved) std::memcpy(reinterpret_cast<void*>(a.pp + c::pp_derived), save.derived, c::pp_derived_size);
    if (save.src_saved) std::memcpy(reinterpret_cast<void*>(a.src + c::src_words), save.src, c::src_size);
    if (save.cam_saved) {
        std::size_t at = 0;
        std::uint32_t dirty = 0;
        unsigned restored = 0;
        for (std::size_t i = 0; i < pi::kCamRangeCount; ++i) {
            const pi::CamRange& r = pi::kCamRanges[i];
            const std::size_t n = static_cast<std::size_t>(r.end - r.begin);
            // Range 6 after a lookcam write: only changes beyond ours are dirty,
            // and it is always restored.
            const bool ours = i == kCam6 && save.lookcam_written;
            const bool changed = std::memcmp(ours ? save.cam6_after : save.cam + at, reinterpret_cast<void*>(a.cam_object + r.begin), n) != 0;
            if (changed) dirty |= (1u << i);
            if ((changed || ours) && a.cam == PartnerInputCam::sandbox) {
                std::memcpy(reinterpret_cast<void*>(a.cam_object + r.begin), save.cam + at, n);
                ++restored;
            }
            at += n;
        }
        if (dirty) InterlockedOr(&g_cam_dirty, static_cast<LONG>(dirty));
        if (restored) InterlockedAdd(&g_cam_restored, static_cast<LONG>(restored));
    }
    if (save.camsub_saved) {
        std::uint32_t dirty = 0;
        const auto sub = save.camsub_ptr;
        // Aim init may replace the sub-controller inside the window: the old
        // object may be gone, so it is neither compared nor restored (bit 2).
        if (Load<std::uintptr_t>(a.cam_object + c::cam_aim_sub) != sub) {
            dirty = 4u;
        } else {
            if (std::memcmp(save.camsub, reinterpret_cast<void*>(sub + c::cam_sub_begin), c::cam_sub_end - c::cam_sub_begin) != 0) dirty |= 1u;
            if (std::memcmp(save.camsub + 0xC, reinterpret_cast<void*>(sub + c::cam_sub_b8), c::cam_sub_bc - c::cam_sub_b8) != 0) dirty |= 2u;
            if (dirty && a.cam == PartnerInputCam::sandbox) {
                std::memcpy(reinterpret_cast<void*>(sub + c::cam_sub_begin), save.camsub, c::cam_sub_end - c::cam_sub_begin);
                std::memcpy(reinterpret_cast<void*>(sub + c::cam_sub_b8), save.camsub + 0xC, c::cam_sub_bc - c::cam_sub_b8);
            }
        }
        if (dirty) InterlockedOr(&g_n_camsub_dirty, static_cast<LONG>(dirty));
    }
}

// POST under SEH (phase 3 on an access fault).
void SafePost(const pi::Arm& a, const Save& save, bool capture_private) noexcept {
    __try { ApplyPost(a, save, capture_private); }
    __except (OwnFilter(3, GetExceptionCode())) {}
}

// PRE under SEH; on an access fault the saved prefix is restored and false
// returned (the window is then disabled by Fault).
bool SafePre(const pi::Arm& a, const pi::Injection& j, Save& save, const WindowOpts& o) noexcept {
    bool done = false;
    __try { ApplyPre(a, j, save, o); done = true; }
    __except (OwnFilter(1, GetExceptionCode())) {}
    if (!done) SafePost(a, save, false);
    return done;
}

// One original call through any of the wrapped entries (entry 255 = the main
// 0x9C2B51 dispatch).
struct Call {
    std::uint8_t entry;
    void* ctx;
    void* root;
    std::uint32_t a, b;
    JobCallFn job = nullptr;
};
inline constexpr std::uint8_t kMainEntry = 255;
inline constexpr std::uint8_t kJobEntry = 5;  // partner_defer's drain: this call's job(go = ctx, param = root)
bool PoseAllowCall(const Call& k) noexcept;
bool PoseEnterShared(PoseSharedLease&) noexcept;
void PoseExitShared(PoseSharedLease&) noexcept;
void CallOriginal(const Call& k) noexcept {
    if (!PoseAllowCall(k)) return;
    PoseSharedLease shared{};
    if (!PoseOwned() && !PoseEnterShared(shared)) return;
    __try {
    switch (k.entry) {
    case kJobEntry: k.job(k.ctx, k.root); break;
    case 0: g_svc_active(k.ctx, k.root, k.a); break;
    case 1: g_svc_all(k.ctx, k.root, k.a); break;
    case 2: g_svc_id(k.ctx, k.root, k.a, k.b); break;
    case 3: g_walk7(k.root, k.a); break;
    case 4: g_walk11(k.root, k.a, k.b); break;
    default: g_original(k.root, k.a); break;
    }
    } __finally { PoseExitShared(shared); }
}

// The original call with the window's fields swapped in. A fatal game
// exception is recorded and passed on; if an outer handler unwinds through
// here, the swapped fields are restored and the window closed first. A drain
// (`detour`) releases the phase-1 detour on every exit, unwinding included.
void GuardedCall(const pi::Arm& a, const Save& save, const Call& k, bool detour = false) noexcept {
    __try {
        __try { CallOriginal(k); }
        __finally {
            if (detour) {
                // The UI scope must end even when the native job unwinds.
                teammate_targeting::EndQuietJob();
                partner_local_detour::Release();
                partner_look_source::Withdraw();
            }
            if (AbnormalTermination() && !PoseOwned()) {
                g_in_drain = false;
                SafePost(a, save, false);
                EndWindow();
            }
        }
    } __except (GameFilter(GetExceptionCode())) {}
}

#include "native_pose_window_impl.inc"
bool PoseHeld() noexcept { return PoseRequired() && g_pose_held != 0; }
bool BeginWindow(const pi::Arm* arm) noexcept { return PoseBegin(arm); }
void EndWindow() noexcept { PoseEnd(g_faulted == 0); }

// Block 0 only (I5): the hold classifier of the local player's update is
// logged through the log vtable copy. Validated like the window (PP identity),
// honours g_faulted, never calls the original twice.
void RunHoldLog(const pi::Arm& a, void* root, std::uint32_t phase) noexcept {
    if (g_faulted || !g_vt_ready) { PoseDispatchOriginal(root, phase); return; }
    pi::Skip skip = pi::Skip::player;
    __try { skip = ValidatePad(a); }
    __except (OwnFilter(1, GetExceptionCode())) {}
    if (skip != pi::Skip::none) { PoseDispatchOriginal(root, phase); return; }
    Save save{};
    if (!BeginWindow()) { PoseDispatchOriginal(root, phase); return; }
    bool swapped = false;
    __try {
        save.vt = Load<std::uintptr_t>(a.pp);
        save.vt_saved = true;
        Store<std::uintptr_t>(a.pp, reinterpret_cast<std::uintptr_t>(&g_vt_log[1]));
        swapped = true;
    } __except (OwnFilter(1, GetExceptionCode())) {}
    if (!swapped) {
        SafePost(a, save, false);
        EndWindow();
        PoseDispatchOriginal(root, phase);
        return;
    }
    GuardedCall(a, save, Call{kMainEntry, nullptr, root, phase, 0});
    SafePost(a, save, false);
    EndWindow();
}

// --- Native rates mirror ------------------------------------------------------
// Runs on the hook thread only with no batch in flight (the drain window, or
// the main dispatch once no native Arm is published): block 1's job, the only
// reader of the rates besides the instance setup, never runs concurrently.
int RatesFilter(DWORD code) noexcept { return IsAccessFault(code) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH; }
void RatesPublish() noexcept {
    InterlockedExchange(&g_n_rates_applied, g_rates.applied ? 1 : 0);
    InterlockedExchange(&g_n_rates_latched, g_rates.latched ? 1 : 0);
    InterlockedExchange(&g_n_rates_reason, static_cast<LONG>(g_rates.reason));
}
void RatesDrop() noexcept {
    if (g_rates.applied) InterlockedIncrement(&g_n_rates_drops);
    g_rates.applied = false;
}
void RatesForgetPending() noexcept {
    if (!InterlockedExchange(&g_rates_forget, 0)) return;
    RatesDrop();
    prt::Reset(g_rates);
    g_rates_sp = g_rates_rec = g_rates_p = g_rates_blk = g_rates_p_vt = 0;
    g_rates_id = g_rates_epoch = 0;
    g_rates_pool = 0;
    // The forgotten mission's Arm (still published until the driver's disarm
    // lands, a drain may run with it meanwhile) never applies again; only a
    // later epoch does.
    g_rates_blocked = static_cast<std::uint32_t>(InterlockedCompareExchange(&g_rates_forget_epoch, 0, 0));
}
// The identity of a rate record (the contract's chain, read under the caller's SEH).
prt::Reason RatesIdentity(std::uintptr_t sp, std::uintptr_t rec, std::uint32_t id) noexcept {
    if (Load<std::uintptr_t>(sp) != g_base + c::rate_sp_vtable_rva) return prt::Reason::sp_vtable;
    const std::uint32_t base = Load<std::uint32_t>(sp + c::sp_base_id), count = Load<std::uint32_t>(sp + c::sp_count);
    const std::uintptr_t recs = Load<std::uintptr_t>(sp + c::sp_records);
    if (id < base || id - base >= count || count > 64 || !recs || recs + static_cast<std::uintptr_t>(id - base) * c::rate_rec_stride != rec)
        return prt::Reason::bounds;
    if (Load<std::uintptr_t>(rec) != g_base + c::rate_rec_vtable_rva) return prt::Reason::rec_vtable;
    return prt::Reason::none;
}
// The record is instance 1's own: id 1 and the rate service of instance 1's
// block PS (PS1 = *(blk1+0x78) with a player PS vtable and PS1+0x408 == blk1,
// sp == *(PS1+0x260)), never a pointer only the driver tick vouched for.
bool RatesOwner(std::uintptr_t blk1, std::uintptr_t sp, std::uint32_t id) noexcept {
    if (id != 1 || !blk1 || !sp) return false;
    const std::uintptr_t ps = Load<std::uintptr_t>(blk1 + c::blk_ps);
    if (!ps) return false;
    const std::uintptr_t ps_vt = Load<std::uintptr_t>(ps);
    if (ps_vt != g_base + c::ps_vtable_rva && ps_vt != g_base + c::ps_vtable_alt_rva) return false;
    return Load<std::uintptr_t>(ps + c::ps_blk) == blk1 && Load<std::uintptr_t>(ps + c::ps_rates) == sp;
}
// Outside the drain (no Validate for this call): the chain the applied record
// was validated through still holds. The player slot still holds P with its
// vtable, ids[1] == 1, pool[1] is still instance 1's pool (never instance 0's)
// and its block is still blk1; then RatesOwner.
bool RatesChain() noexcept {
    const std::uintptr_t P = g_rates_p;
    if (!P || Load<std::uintptr_t>(g_base + c::player_slot_rva) != P || Load<std::uintptr_t>(P) != g_rates_p_vt) return false;
    const std::uintptr_t ids = Load<std::uintptr_t>(P + c::p_ids), pool = Load<std::uintptr_t>(P + c::p_pool);
    if (!ids || !pool || Load<std::uint16_t>(ids + 2) != 1 || Load<std::uint8_t>(pool + 1) != g_rates_pool ||
        Load<std::uint8_t>(pool) == g_rates_pool)
        return false;
    const std::uintptr_t blocks = Load<std::uintptr_t>(P + c::p_blocks);
    if (!blocks || blocks + static_cast<std::uintptr_t>(g_rates_pool) * c::block_stride != g_rates_blk) return false;
    return RatesOwner(g_rates_blk, g_rates_sp, g_rates_id);
}
void RatesWrite(std::uintptr_t rec, const prt::Plan& plan) noexcept {
    for (unsigned i = 0; i < prt::kFields; ++i)
        if (plan.mask & (1u << i)) Store<float>(rec + c::rate_first + 4 * i, plan.value[i]);
}
// Restore what the mirror changed on its own record (chain and identity
// re-checked; a record that no longer validates is dropped, never written).
void RatesRestore() noexcept {
    RatesForgetPending();
    if (!g_rates.applied) { RatesPublish(); return; }
    __try {
        if (!RatesChain() || RatesIdentity(g_rates_sp, g_rates_rec, g_rates_id) != prt::Reason::none) {
            RatesDrop();
        } else {
            prt::Record cur{};
            std::memcpy(cur.f, reinterpret_cast<const void*>(g_rates_rec + c::rate_first), sizeof(cur.f));
            const prt::Plan plan = prt::Restore(g_rates, cur);
            RatesWrite(g_rates_rec, plan);
            InterlockedIncrement(&g_n_rates_restores);
        }
    } __except (RatesFilter(GetExceptionCode())) {
        RatesDrop();
        prt::Latch(g_rates, prt::Reason::fault);
    }
    RatesPublish();
}
// The drain window's step under a native Arm (Validate held for `a`: P, ids[1]
// == 1, blk1 is pool1's live block, never instance 0's): mirror the remote's
// rates into instance 1's record (a.rates_mirror) or restore them.
void RatesStep(const pi::Arm& a) noexcept {
    RatesForgetPending();
    if (a.rates_epoch != g_rates_epoch) {
        // A new epoch: whatever the old state referred to is not touched.
        RatesDrop();
        prt::Reset(g_rates);
        g_rates_epoch = a.rates_epoch;
        g_rates_sp = g_rates_rec = g_rates_p = g_rates_blk = g_rates_p_vt = 0;
        g_rates_id = 0;
        g_rates_pool = 0;
    }
    if (!a.rates_mirror || (g_rates_blocked && a.rates_epoch == g_rates_blocked)) { RatesRestore(); return; }
    if (g_rates.latched || !a.rates_sp || !a.rates_rec) { RatesPublish(); return; }
    if (g_rates.applied && (a.rates_rec != g_rates_rec || a.rates_sp != g_rates_sp || a.rates_id != g_rates_id ||
                            a.blk1 != g_rates_blk || a.P != g_rates_p || a.pool1 != g_rates_pool))
        RatesDrop();
    __try {
        prt::Reason why = RatesOwner(a.blk1, a.rates_sp, a.rates_id) ? prt::Reason::none : prt::Reason::owner;
        if (why == prt::Reason::none) why = RatesIdentity(a.rates_sp, a.rates_rec, a.rates_id);
        if (why != prt::Reason::none) {
            RatesDrop();
            prt::Latch(g_rates, why);
        } else {
            prt::Record cur{};
            std::memcpy(cur.f, reinterpret_cast<const void*>(a.rates_rec + c::rate_first), sizeof(cur.f));
            const std::uint32_t reapplied = g_rates.reapplied;
            const prt::Plan plan = prt::Apply(g_rates, cur, prt::Wire{a.rates_remote[0], a.rates_remote[1], a.rates_remote[2]});
            RatesWrite(a.rates_rec, plan);  // a latching plan restores first
            if (g_rates.latched && plan.mask) InterlockedIncrement(&g_n_rates_restores);
            if (!g_rates.latched) {
                g_rates_sp = a.rates_sp;
                g_rates_rec = a.rates_rec;
                g_rates_id = a.rates_id;
                g_rates_p = a.P;
                g_rates_p_vt = a.p_vtable;
                g_rates_blk = a.blk1;
                g_rates_pool = a.pool1;
                if (plan.mask) InterlockedIncrement(&g_n_rates_writes);
                if (g_rates.reapplied != reapplied) InterlockedIncrement(&g_n_rates_reapplied);
                for (unsigned i = 0; i < 3; ++i) InterlockedExchange(&g_n_rates_w[i], static_cast<LONG>(a.rates_remote[i]));
            }
        }
    } __except (RatesFilter(GetExceptionCode())) {
        RatesDrop();
        prt::Latch(g_rates, prt::Reason::fault);
    }
    RatesPublish();
}
// Main dispatch (hook thread): restore once no drain will run any more (no
// enabled native Arm: disarmed, or a legacy Arm after leaving the native
// drive), never while a batch is in flight (workers reach the dispatch only
// inside a batch, so this never runs off the hook thread).
void RatesIdle() noexcept {
    if (partner_defer::InBatch()) return;
    RatesRestore();
}

void SetCurrent(const pi::Injection& j, PartnerInputHold hold) noexcept {
    g_cur = Current{j.held, j.edge_press, j.edge_release, j.w[2], j.long12, j.tap12, j.live, hold};
}

void RunWindow(const pi::Arm& a, void* root, std::uint32_t phase) noexcept {
    InterlockedIncrement(&g_root1);
    // Native: never swap the shared pad while any batch is in flight (block 0's
    // job may be running on a worker); the arbiter leaves on wrap_in_batch.
    if (a.native && partner_defer::InBatch()) {
        InterlockedIncrement(&g_w_in_batch);
        PoseDispatchOriginal(root, phase);
        return;
    }
    pi::Skip skip = pi::Skip::player;
    bool validated = false;
    __try { skip = Validate(a, root); validated = true; }
    __except (OwnFilter(1, GetExceptionCode())) {}
    if (!validated) { PoseDispatchOriginal(root, phase); return; }
    if (skip != pi::Skip::none) {
        InterlockedIncrement(&g_skips);
        InterlockedExchange(&g_last_skip, static_cast<LONG>(skip));
        PoseDispatchOriginal(root, phase);
        return;
    }
    InterlockedIncrement(&g_windows);
    if (a.config.mode == PartnerInput::pass) { g_inj_valid = false; PoseDispatchOriginal(root, phase); return; }
    const std::uint64_t now_ms = NowMs();
    // One fresh injection per main window (once per frame); wrap windows
    // reuse its level only (LevelOnly), so every edge is delivered once.
    pi::Sample s{};
    if (g_sample && g_sample(s)) {
        g_prev = s;
    } else {
        s = g_prev;
        s.edge_press = s.edge_release = s.edge_tap12 = 0;  // consumed edges never replay
    }
    g_prev.edge_press = g_prev.edge_release = g_prev.edge_tap12 = 0;
    // First window after a disarm: the edge sets that piled up while disarmed
    // were consumed above and are dropped by Compose (level only).
    // So is the first window after a gap (skips, pass mode) of kResyncGapMs.
    if (g_need_resync || !g_last_compose_ms || now_ms - g_last_compose_ms > pi::kResyncGapMs) {
        g_need_resync = false;
        g_edge.resync = true;
    }
    g_last_compose_ms = now_ms;
    const std::uint32_t overlay_bits = pi::StepOverlay(g_tap, a.tap_seq, a.tap_bits, a.press_bits, a.press_until_ms, now_ms);
    const pi::Injection j = pi::Compose(a.config, s, a.host_cam_cdeg, pi::Overlay{overlay_bits}, g_edge, a.native ? a.steer_rad : 0.0,
                                        a.native && a.mag_scale > 0.0 ? a.mag_scale : 1.0);
    if (j.hist_reset) g_hist = pi::HoldHistory{};
    if (!j.hist_freeze) pi::HoldPush(g_hist, j.held);
    g_last_inj = j;
    g_inj_valid = true;
    // The frame injection: the main window is its first consumer.
    ++g_frame_id;
    g_frame_ms = now_ms;
    g_consumer_last[static_cast<std::size_t>(pi::Consumer::main)] = g_frame_id;
    if (a.native) InterlockedIncrement(&g_n_frames);
    if (j.synth) InterlockedIncrement(&g_synth);
    if (j.edge_press) InterlockedIncrement(&g_press_n);
    if (j.edge_release) InterlockedIncrement(&g_release_n);
    if (j.stale) InterlockedIncrement(&g_stale);
    InterlockedExchange(&g_last_stale, j.stale ? 1 : 0);
    InterlockedExchange(&g_last_held, static_cast<LONG>(j.held));
    InterlockedExchange(&g_last_lx, pi::Milli(j.lx));
    InterlockedExchange(&g_last_ly, pi::Milli(j.ly));
    InterlockedExchange(&g_last_dir, pi::Milli(j.dir));
    InterlockedExchange(&g_last_mag, pi::Milli(j.mag));
    InterlockedExchange(&g_last_age, static_cast<LONG>(s.valid ? s.age_ms : pi::kNoSample));
    const std::uint64_t t0 = NowUs();
    const pi::Injection jw = NativeView(a, j);
    Save save{};
    if (!BeginWindow(&a)) return;
    __try {
    if (!SafePre(a, jw, save, WindowOpts{true, a.native, false, 0})) {
        // The prefix was restored; block 1 still updates once this frame.
        EndWindow();
        PoseDispatchOriginal(root, phase);
        return;
    }
    SetCurrent(jw, a.hold);
    GuardedCall(a, save, Call{kMainEntry, nullptr, root, phase, 0});
    SafePost(a, save, true);
    EndWindow();
    Max(g_us_max, static_cast<LONG>(NowUs() - t0));
    } __finally {
        if (PoseOwned()) {
            g_in_drain = false;
            SafePost(a, save, false);
            EndWindow();
        }
    }
}

void __stdcall InputDispatchHook(void* root, std::uint32_t phase) noexcept {
    if (InterlockedCompareExchange(&g_hook_thread, static_cast<LONG>(GetCurrentThreadId()), 0) == 0) {}
    if (g_shutdown) { PoseDispatchOriginal(root, phase); return; }
    if ((g_rates.applied && !g_wrap_native) || g_rates_forget) RatesIdle();
    // The aim-target mirror (partner_aim_point): restored like the rates once
    // no enabled native Arm (or no wanted target) is published.
    if (partner_aim_point::IdleDue(g_wrap_native != 0) && !partner_defer::InBatch()) partner_aim_point::Idle();
    if (!g_arm_enabled && !g_holdlog) { g_need_resync = true; PoseDispatchOriginal(root, phase); return; }
    pi::Arm a{};
    if (!ReadArm(a)) { PoseDispatchOriginal(root, phase); return; }
    InterlockedExchange(&g_gen_seen, static_cast<LONG>(a.gen));
    if (InterlockedExchange(&g_reset_hist, 0)) {
        g_hist = pi::HoldHistory{};
        g_inj_valid = false;
        g_mag_history = partner_native::MagazineHistory{};
        g_sl_ep_target = g_sl_ep_seq = 0xFFFFFFFFu;
        g_sl_ep_writes = 0;
        g_sl_ep_reason = -1;
        // F1: no drain consumer's previous held word carries into a new
        // mission (a stale FIRE would hide the first press edge).
        std::memset(g_ring_prev, 0, sizeof(g_ring_prev));
    }
    if (a.gen != g_gen) {
        // Only instance 1's private masks and shadows start afresh with a new gen.
        g_gen = a.gen;
        std::memset(g_priv_masks, 0, sizeof(g_priv_masks));
        g_priv_valid = false;
        g_shadow_valid = false;
        g_inj_valid = false;
    }
    if (phase != 8) { PoseDispatchOriginal(root, phase); return; }
    if (a.holdlog && reinterpret_cast<std::uintptr_t>(root) == a.root0) { RunHoldLog(a, root, phase); return; }
    if (!a.enabled || g_faulted) { g_need_resync = true; PoseDispatchOriginal(root, phase); return; }
    if (reinterpret_cast<std::uintptr_t>(root) != a.root1) { PoseDispatchOriginal(root, phase); return; }
    RunWindow(a, root, phase);
}

// --- Wrap entries --------------------------------------------------------------
// A window around one of block 1's other tree walks, on the hook thread, not
// nested, with this frame's injection (wrap=window).
void RunWrapWindow(const Call& k) noexcept {
    pi::Arm a{};
    if (!ReadArm(a) || !a.enabled || a.wrap != PartnerWrap::window || a.gen != g_gen || !g_inj_valid || !g_last_inj.write ||
        reinterpret_cast<std::uintptr_t>(k.root) != a.root1 || g_faulted) {
        CallOriginal(k);
        return;
    }
    pi::Skip skip = pi::Skip::player;
    bool validated = false;
    __try { skip = Validate(a, k.root); validated = true; }
    __except (OwnFilter(1, GetExceptionCode())) {}
    if (!validated || skip != pi::Skip::none) { CallOriginal(k); return; }
    // Legacy: level only (the main window delivered this frame's edges).
    // Native: this consumer's view of the frame injection (edges once).
    pi::Injection j{};
    if (a.native) {
        const auto consumer = k.entry == 3 ? pi::Consumer::walk7 : pi::Consumer::walk11;
        pi::ConsumerView view{};
        j = pi::ForConsumer(g_last_inj, g_frame_id, g_consumer_last[static_cast<std::size_t>(consumer)], NowMs() - g_frame_ms,
                            a.config, &view);
        InterlockedIncrement(&g_n_views[static_cast<std::size_t>(view)]);
    } else {
        j = pi::LevelOnly(g_last_inj, a.config);
    }
    j = NativeView(a, j);
    Save save{};
    if (!BeginWindow(&a)) return;
    __try {
    if (!SafePre(a, j, save, WindowOpts{a.native ? a.local_all : true, a.native, false, 0})) {
        EndWindow();
        CallOriginal(k);
        return;
    }
    SetCurrent(j, a.hold);
    GuardedCall(a, save, k);
    SafePost(a, save, true);
    EndWindow();
    if (k.entry < kWrapEntries) InterlockedIncrement(&g_w_windows[k.entry]);
    if (a.native) InterlockedIncrement(&g_n_walk_windows);
    } __finally {
        if (PoseOwned()) {
            g_in_drain = false;
            SafePost(a, save, false);
            EndWindow();
        }
    }
}

// Native look write (aim stage): instance 1's own per-id look slot after the
// game computed it in PS phase 0 (svc28 phase 0 returns before activation),
// skipped when the record bit 1 says the game skipped it. Never restored: the
// game recomputes it every phase 0. A fault disables the look for the mission.
int LookFilter(DWORD code) noexcept {
    if (!IsAccessFault(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&g_n_look_faulted, 1) == 0 && g_log) {
        char line[128]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_input=fault; phase=look; code=0x%08x; aim=disabled\r\n", code)))
            g_log(line);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
void LookRefused(LookRefusal why) noexcept {
    InterlockedIncrement(&g_n_look_refused);
    InterlockedExchange(&g_n_look_last, static_cast<LONG>(why));
}
void LookWrite() noexcept {
    const pi::Arm& a = g_drain_arm;
    if (a.naim == PartnerNativeAim::off) { InterlockedExchange(&g_n_look_last, static_cast<LONG>(LookRefusal::off)); return; }
    if (g_n_look_faulted) { LookRefused(LookRefusal::fault); return; }
    const pi::Injection& j = g_drain_inj;
    if (!j.look_valid) { LookRefused(j.stale ? LookRefusal::stale : LookRefusal::no_v3); return; }
    LookRefusal why = LookRefusal::none;
    __try {
        const std::uintptr_t ps = a.ps1;
        const std::uintptr_t vt = ps ? Load<std::uintptr_t>(ps) : 0;
        const std::uint32_t base_id = ps ? Load<std::uint32_t>(ps + c::ps_base_id) : 0xFFFFFFFFu;
        if (!ps || (vt != g_base + c::ps_vtable_rva && vt != g_base + c::ps_vtable_alt_rva) ||
            Load<std::uintptr_t>(ps + c::ps_phase_comp) != vt + c::ps_phase_comp_vt_delta || base_id > 1 || 1 - base_id != a.ps1_idx) {
            why = LookRefusal::ps;
        } else {
            const std::uintptr_t rec = Load<std::uintptr_t>(ps + c::ps_rec);
            const std::uintptr_t look = Load<std::uintptr_t>(ps + c::ps_look);
            if (!rec || !look) why = LookRefusal::ps;
            else if (Load<std::uint32_t>(rec + static_cast<std::uintptr_t>(a.ps1_idx) * c::ps_rec_stride) & c::ps_rec_skip_bit) {
                InterlockedIncrement(&g_n_look_skipped);
            } else {
                const float pair[2] = {j.look_pitch, j.look_yaw};
                std::memcpy(reinterpret_cast<void*>(look + static_cast<std::uintptr_t>(a.ps1_idx) * c::ps_look_stride), pair, sizeof(pair));
                InterlockedIncrement(&g_n_look_writes);
                InterlockedExchange(&g_n_look_last, static_cast<LONG>(LookRefusal::none));
            }
        }
    } __except (LookFilter(GetExceptionCode())) {
        why = LookRefusal::fault;
    }
    if (why != LookRefusal::none) LookRefused(why);
}

// R1: the post-phase-1 probe (read-only; a fault only skips the sample).
std::int32_t PhaseOneMode() noexcept {
    std::int32_t mode = -1;
    __try {
        if (Load<std::uintptr_t>(g_drain_arm.gun_prim + c::prim_gun_rec) == g_drain_arm.gun_rec)
            mode = Load<std::uint8_t>(g_drain_arm.gun_rec + c::gun_mode_cur);
    } __except (IsAccessFault(GetExceptionCode()) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    return mode;
}
void PhaseOneProbe(std::int32_t before) noexcept {
    std::uint32_t f = 0;
    std::uint8_t shots = 0, mode = 0;
    bool ok = false;
    __try {
        if (Load<std::uintptr_t>(g_drain_arm.gun_prim + c::prim_gun_rec) == g_drain_arm.gun_rec) {
            f = Load<std::uint32_t>(g_drain_arm.gun_rec + c::gun_flags);
            shots = Load<std::uint8_t>(g_drain_arm.gun_rec + c::gun_shots);
            mode = Load<std::uint8_t>(g_drain_arm.gun_rec + c::gun_mode_cur);
            ok = true;
        }
    } __except (IsAccessFault(GetExceptionCode()) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {}
    if (!ok) return;
    if (f & c::gun3c0_reload) InterlockedIncrement(&g_p1_b20);
    if (f & c::gun3c0_shot) {
        InterlockedIncrement(&g_p1_b24);
        InterlockedAdd(&g_p1_shots, static_cast<LONG>(shots ? shots : 1u));
    }
    if (before >= 0 && static_cast<std::int32_t>(mode) != before) InterlockedIncrement(&g_p1_modes);
    InterlockedExchange(&g_p1_mode, static_cast<LONG>(mode));
}

void WrapEnter(const Call& k) noexcept {
    const LONG mode = g_wrap_mode;
    const auto root = static_cast<LONG64>(reinterpret_cast<std::uintptr_t>(k.root));
    if (mode == static_cast<LONG>(PartnerWrap::off) || !root) { CallOriginal(k); return; }
    if (root == g_wrap_root0) {
        InterlockedIncrement(&g_w_calls0[k.entry]);
        CallOriginal(k);
        return;
    }
    if (root != g_wrap_root1) {
        InterlockedIncrement(&g_w_other);
        CallOriginal(k);
        return;
    }
    InterlockedIncrement(&g_w_calls1[k.entry]);
    InterlockedOr(&g_w_phases[k.entry], static_cast<LONG>(1u << (k.a & 31u)));
    const bool same = static_cast<LONG>(GetCurrentThreadId()) == g_hook_thread;
    const bool nested = (g_window_seq & 1) != 0;
    if (same) InterlockedIncrement(&g_w_same[k.entry]);
    if (nested) InterlockedIncrement(&g_w_nested[k.entry]);
    if (mode == static_cast<LONG>(PartnerWrap::window) && same && !nested) {
        // Native: svc entries never open a window (block 1's job runs only in
        // drains). One arriving here is block 1's job outside a drain (the
        // frame before the deferral is armed, or a job the deferral passed;
        // the arbiter's health counts that). Nothing is swapped, so it is
        // never an in-batch refusal: checked before InBatch.
        if (g_wrap_native && k.entry <= 2) {
            InterlockedIncrement(&g_w_svc_native);
            CallOriginal(k);
            return;
        }
        // A walk run inline during a hook-thread batch's join (or a legacy svc
        // window): block 0's job may be running on a worker, the shared PP
        // must not be swapped now.
        if (partner_defer::InBatch()) {
            InterlockedIncrement(&g_w_in_batch);
            CallOriginal(k);
            return;
        }
        RunWrapWindow(k);
        return;
    }
    // The post-phase-0 look write, nested in a drain window.
    if (k.entry == 0 && k.a == 0 && same && g_in_drain) {
        CallOriginal(k);
        LookWrite();
        return;
    }
    // R1: instance 1's gun record right after phase 1 (the gun update), nested
    // in a drain window: the reload request bit 20 and the trigger's bit 24 /
    // shot count exist only inside the drain (phase 6 clears them). Read-only.
    if ((k.entry == 0 || k.entry == 1) && k.a == 1 && same && g_in_drain && g_drain_arm.gun_rec) {
        const std::int32_t before = PhaseOneMode();
        CallOriginal(k);
        PhaseOneProbe(before);
        return;
    }
    CallOriginal(k);
}

// Diagnostics after a drain's job (inside the window, before POST): instance
// 1's base action record (current act, next act, the move magnitude and
// heading the act handlers sampled from the pad). Read-only; a fault only
// marks the probe unread (never the window's fault state).
int ProbeFilter(DWORD code) noexcept { return IsAccessFault(code) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH; }
void ProbeRecord(std::uintptr_t rec) noexcept {
    std::uint8_t act = 0, next = 0;
    float mag = 0.0f, heading = 0.0f;
    bool ok = false;
    __try {
        act = Load<std::uint8_t>(rec + c::rec_act);
        next = Load<std::uint8_t>(rec + c::rec_act_next);
        mag = Load<float>(rec + c::rec_move_mag);
        heading = Load<float>(rec + c::rec_move_heading);
        ok = true;
    } __except (ProbeFilter(GetExceptionCode())) {}
    InterlockedExchange(&g_d_rec_ok, ok ? 1 : 0);
    InterlockedExchange(&g_d_rec_act, static_cast<LONG>(act));
    InterlockedExchange(&g_d_rec_next, static_cast<LONG>(next));
    InterlockedExchange(&g_d_rec_mag, ok ? pi::Milli(mag) : 0);
    InterlockedExchange(&g_d_rec_heading, ok ? pi::Milli(heading) : 0);
}

// D3 telemetry: the remote's look the drain injected and instance 1's slot
// after its job (centidegrees), read-only.
std::int32_t Cdeg(float rad) noexcept {
    if (!(rad == rad) || rad > 10.0f || rad < -10.0f) return INT32_MIN;
    return static_cast<std::int32_t>(rad * 5729.5779513f + (rad >= 0.0f ? 0.5f : -0.5f));
}
void LookProbe(const pi::Arm& a, const pi::Injection& j) noexcept {
    if (a.naim == PartnerNativeAim::off || !a.ps1 || !j.look_valid) {
        InterlockedExchange(&g_l_ok, 0);
        return;
    }
    float pair[2] = {0.0f, 0.0f};
    bool ok = false;
    __try {
        const std::uintptr_t look = Load<std::uintptr_t>(a.ps1 + c::ps_look);
        if (look) {
            std::memcpy(pair, reinterpret_cast<const void*>(look + static_cast<std::uintptr_t>(a.ps1_idx) * c::ps_look_stride), sizeof(pair));
            ok = true;
        }
    } __except (ProbeFilter(GetExceptionCode())) {}
    InterlockedExchange(&g_l_remote[0], Cdeg(j.look_pitch));
    InterlockedExchange(&g_l_remote[1], Cdeg(j.look_yaw));
    InterlockedExchange(&g_l_slot[0], ok ? Cdeg(pair[0]) : INT32_MIN);
    InterlockedExchange(&g_l_slot[1], ok ? Cdeg(pair[1]) : INT32_MIN);
    InterlockedExchange(&g_l_ok, ok ? 1 : 0);
}

// --- Native weapons (docs/native-puppet.md "Weapons") ---------------------------
static_assert(c::gun_mag == 0x25C && c::gun_mag_sub == 0x25E && c::gun27c_sub == 0x200 && c::gun3c0_full_mag == 0x10000000u &&
                  c::gun_cap == 0x210 && c::gun_cap_sub == 0x212 && c::gun27c_double == 0x100000u && c::gun27c_half == 0x80000u &&
                  c::gun_double_equip_lo == 0x42B && c::gun_class_shift == 6 && c::gun_class_mask == 7 && c::gun_class_gun == 1,
              "the pure gun rules of partner_native match the contract");
int MagFilter(DWORD code) noexcept {
    if (!IsAccessFault(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&g_m_faulted, 1) == 0 && g_log) {
        char line[128]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_input=fault; phase=mag; code=0x%08x; mag_mirror=disabled\r\n", code)))
            g_log(line);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
void MagRefused(MagRefusal why, bool count) noexcept {
    if (count) InterlockedIncrement(&g_m_refused);
    InterlockedExchange(&g_m_last, static_cast<LONG>(why));
}
// F2: the magazine's per-instance store row, which gun phase 0 (0x108C8C0)
// copies into rec+0x25C/+0x25E every frame before the trigger and the reload
// gate read them (a write to the record alone is overwritten). Written
// directly, never through the setter 0xEE96C0 (its notifier [Y+0x58] vt+0x100
// takes no id). Inside the caller's __try.
StoreRefusal StoreRow(const pi::Arm& a, std::uint32_t flags27c, std::uint32_t target, std::uint32_t cap, bool& wrote) noexcept {
    if (Load<std::uint32_t>(a.P + c::p_flags) & (c::flag_online | c::flag_alt_dispatch)) return StoreRefusal::online;
    if (Load<std::uint32_t>(a.gun_rec + c::gun_tamper) & c::gun3d0_tamper_check) return StoreRefusal::tamper;
    if (Load<std::uint32_t>(a.P + c::p_local_id) == 1u) return StoreRefusal::owner;
    const std::uintptr_t x = Load<std::uintptr_t>(a.P + c::p_equip);
    const std::uintptr_t y = x ? Load<std::uintptr_t>(x + c::store_y) : 0;
    const std::uintptr_t vt = y ? Load<std::uintptr_t>(y) : 0;
    if (!vt || (vt != g_base + c::store_vt_rva && vt != g_base + c::store_vt_derived_rva) ||
        Load<std::uintptr_t>(vt + c::store_vt_set) != g_base + c::store_set_rva)
        return StoreRefusal::y_vt;
    const std::uint32_t stride = Load<std::uint32_t>(y + c::store_stride);
    const std::uint32_t slot = Load<std::uint8_t>(a.gun_rec + c::gun_slot);
    if (stride < 1 || stride > 16 || slot >= stride) return StoreRefusal::slot;
    const std::uintptr_t types = Load<std::uintptr_t>(y + c::store_types), rows = Load<std::uintptr_t>(y + c::store_rows);
    if (!types || !rows) return StoreRefusal::y_vt;
    const std::uintptr_t idx = static_cast<std::uintptr_t>(stride) + slot;  // id 1 (Validate: ids[1] == 1)
    // The store init (0xEE92F0) sized the types and rows by Y+0x10 = ids *
    // stride: a store without a row for id 1 is never written past its end.
    if (idx >= Load<std::uint32_t>(y + c::store_total)) return StoreRefusal::slot;
    const std::uint32_t type = Load<std::uint32_t>(types + idx * 4) & c::store_type_mask;
    const bool sub = (flags27c & c::gun27c_sub) != 0;
    if (type < 1 || type > c::store_type_word0_only || (sub && type == c::store_type_word0_only)) return StoreRefusal::type;
    if (target > cap) return StoreRefusal::cap;
    const std::uintptr_t word = rows + idx * c::store_row_size + (sub ? 2u : 0u);
    if (Load<std::uint16_t>(word) != target) {
        Store<std::uint16_t>(word, static_cast<std::uint16_t>(target));
        wrote = true;
    }
    return StoreRefusal::none;
}
// The magazine mirror (drain window, before block 1's job, on the thread that
// runs instance 1's gun update): on a FIRE press edge of this consumer's view
// (and a RELOAD edge with partner_native_reload=mirror), when the remote's
// (mag, cap) changed, after a freeze or a slot change of body 1 (rec+0x3DA),
// instance 1's magazine store row (StoreRow) and live magazine field become
// partner_native::MirrorMagazine (only when they differ). Its non-local shots
// never spend it (0x1041650 is local-only). Frozen by the weapon gate (RELOAD
// stripped) and, with reload=raw|event, while the remote (v5) or body 1
// reloads (partner_reload::MirrorFrozen): body 1's own completion 0x1054900
// then fills its magazine from rec+0x3E8; the first write after the freeze
// (the reconcile) also writes rec+0x3E8, the completion's base.
void MirrorMag(const pi::Arm& a, const pi::Injection& j) noexcept {
    if (g_m_faulted) { MagRefused(MagRefusal::fault, false); return; }
    if (!a.mag_mirror || !a.gun_prim || !a.gun_rec) { MagRefused(MagRefusal::off, false); return; }
    if (g_m_leak) { MagRefused(MagRefusal::leak, false); return; }
    // The weapon gate: the remote's gun is not body 1's (or body 1 is
    // mid-switch); the mirror is frozen. FIRE alone stripped (an unqualified
    // weapon type) keeps the mirror.
    if (a.strip & partner_native::kPadReload) { MagRefused(MagRefusal::weapon, false); return; }
    InterlockedExchange(&g_m_inj, j.ammo_valid ? static_cast<LONG>(j.ammo) : 0xFFFF);
    InterlockedExchange(&g_m_inj_cap, j.ammo_valid ? static_cast<LONG>(j.ammo_cap) : 0xFFFF);
    if (!j.ammo_valid) { MagRefused(MagRefusal::no_v4, false); return; }
    const bool fire = (j.edge_press & partner_native::kPadFire) != 0;
    const bool reload = (j.edge_press & partner_native::kPadReload) != 0;
    if (reload) InterlockedIncrement(&g_m_reload_seen);
    // R4: body 1's current mode and slot (read under the mirror's fault rule).
    std::uint8_t body_mode = 0, body_slot = 0;
    bool body_read = false;
    __try {
        body_mode = Load<std::uint8_t>(a.gun_rec + c::gun_mode_cur);
        body_slot = Load<std::uint8_t>(a.gun_rec + c::gun_slot);
        body_read = true;
    } __except (MagFilter(GetExceptionCode())) {}
    if (!body_read) { MagRefused(MagRefusal::fault, true); return; }
    const bool remote_reloading = j.gun_valid && (j.gun_flags & pi::GunFlag::Reloading) != 0;
    if (partner_reload::MirrorFrozen(a.reload, remote_reloading, body_mode, a.reload_hold, a.reload_reconcile, a.reload_release)) {
        g_mag_history.freeze_pending = true;
        InterlockedIncrement(&g_m_frozen);
        MagRefused(MagRefusal::reload, false);
        return;
    }
    // raw|event: the freeze owns the reload; an R edge no longer re-asserts the remote's magazine.
    const bool reload_edge = reload && a.reload == PartnerNativeReload::mirror;
    const auto attempt = partner_native::PrepareMagazine(g_mag_history, j.ammo, j.ammo_cap, body_slot, fire, reload_edge);
    if (!attempt.needed) return;
    const bool reconcile = attempt.reconcile;
    // A FIRE edge shows a round only if the last accepted remote sample had one.
    const bool was_loaded = attempt.remote_was_loaded;
    InterlockedExchange(&g_m_remote, j.ammo);
    InterlockedExchange(&g_m_remote_cap, j.ammo_cap);
    MagRefusal why = MagRefusal::none;
    StoreRefusal store = StoreRefusal::none;
    std::uint32_t cap = 0, target = 0;
    bool wrote = false;
    __try {
        if (Load<std::uintptr_t>(a.gun_prim) != g_base + c::gun_prim_vt_rva || Load<std::uintptr_t>(a.gun_prim + c::prim_blk) != a.blk1 ||
            Load<std::uintptr_t>(a.gun_prim + c::prim_gun_rec) != a.gun_rec) {
            why = MagRefusal::prim;
        } else {
            partner_native::GunFields g{};
            g.flags27c = Load<std::uint32_t>(a.gun_rec + c::gun_flags27c);
            g.gun3c0 = Load<std::uint32_t>(a.gun_rec + c::gun_flags);
            g.equip = Load<std::uint32_t>(a.gun_rec + c::gun_equip);
            g.cap = Load<std::uint16_t>(a.gun_rec + c::gun_cap);
            g.cap_sub = Load<std::uint16_t>(a.gun_rec + c::gun_cap_sub);
            cap = partner_native::GunCapacity(g);
            if (g.gun3c0 & c::gun3c0_full_mag) why = MagRefusal::full;
            else if (partner_native::GunClass(g.flags27c) != c::gun_class_gun) why = MagRefusal::cls;
            else if (cap == 0 || cap > partner_native::kMaxMagCap) why = MagRefusal::cap;
            else {
                target = partner_native::MirrorMagazine(partner_native::MagIn{j.ammo, j.ammo_cap, cap, fire, was_loaded});
                store = StoreRow(a, g.flags27c, target, cap, wrote);
                if (store != StoreRefusal::none) {
                    why = MagRefusal::store;
                } else {
                    // The record too, so this frame's readers before the copy see it.
                    const std::uintptr_t field = a.gun_rec + partner_native::GunMagazineOffset(g.flags27c);
                    if (Load<std::uint16_t>(field) != target) {
                        Store<std::uint16_t>(field, static_cast<std::uint16_t>(target));
                        wrote = true;
                    }
                    // R4: the reconcile also sets the completion's base (0x1054AC1 /
                    // 0x107833C: rec+0x3E8, the non-local magazine shadow), so a
                    // later completion starts from the mirrored magazine.
                    if (reconcile && Load<std::uint16_t>(a.gun_rec + c::gun_m3e8) != target) {
                        Store<std::uint16_t>(a.gun_rec + c::gun_m3e8, static_cast<std::uint16_t>(target));
                        InterlockedIncrement(&g_m_3e8);
                        wrote = true;
                    }
                }
            }
        }
    } __except (MagFilter(GetExceptionCode())) {
        why = MagRefusal::fault;
    }
    InterlockedExchange(&g_m_cap, static_cast<LONG>(cap));
    if (why == MagRefusal::store) {
        InterlockedIncrement(&g_s_refused);
        InterlockedExchange(&g_s_last, static_cast<LONG>(store));
    }
    if (why != MagRefusal::none) { MagRefused(why, true); return; }
    // All chain, class, capacity, store and record checks accepted the sample.
    // A validated no-write result also commits; refusals leave it pending.
    partner_native::CommitMagazine(g_mag_history, j.ammo, j.ammo_cap, body_slot);
    InterlockedExchange(&g_m_last, static_cast<LONG>(MagRefusal::none));
    InterlockedExchange(&g_s_last, static_cast<LONG>(StoreRefusal::none));
    InterlockedExchange(&g_m_written, static_cast<LONG>(target));
    if (!wrote) return;
    InterlockedIncrement(&g_m_writes);
    InterlockedIncrement(&g_s_writes);
    if (reconcile) InterlockedIncrement(&g_m_reconciles);
    if (fire) InterlockedIncrement(&g_m_fire);
    if (reload) InterlockedIncrement(&g_m_reload);
}

// --- W4: the slot follower --------------------------------------------------------
// Drain window, before block 1's job, on the thread of its gun update (no batch
// in flight: RunJobWindow refused otherwise). Instance 1's equipment row gets
// the remote's current slot (input frame v5 inv_slot 0..2) the way
// SetCurrentSlot 0x11AFA80 writes it for a non-local id and slot types 0/1/2
// (contract: Q+0x17C |= 1 << id, the low nibble of row+0x36, row+0 = (row+2 +
// 2*slot) & 0x7FF, then the exit 0x11AFE4B before any call), only to a slot the
// driver's plan admits whose handle holds the planned equip: the body slot
// the plan maps the remote's slot to (itself, or weapons4's stand-in when
// body 1 cannot hold the remote's own weapon there). Gun phase 0
// (0x103AED0) then sees the slot differ from rec+0x3D9 and switches natively:
// an immediate activation (0x103E049 -> 0x105FDB0) or, while aiming, mode 8
// (the weapon-change sequence 0x1074070). slot_follow=call runs 0x11AFA80
// itself (lab only). One episode per (remote slot, switch counter): refusals
// count once per reason, at most kSlotEpisodeWrites writes.
constexpr std::uint32_t kSlotEpisodeWrites = 2;
int SlotFilter(DWORD code) noexcept {
    if (!IsAccessFault(code)) return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedExchange(&g_sl_faulted, 1) == 0 && g_log) {
        char line[128]{};
        if (SUCCEEDED(StringCchPrintfA(line, ARRAYSIZE(line), "partner_input=fault; phase=slot; code=0x%08x; slot_follow=disabled\r\n", code)))
            g_log(line);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
void SlotVerdict(SlotRefusal why) noexcept {
    InterlockedExchange(&g_sl_last, static_cast<LONG>(why));
    if (why == SlotRefusal::none || why == SlotRefusal::same || why == SlotRefusal::wait || why == SlotRefusal::off ||
        why == SlotRefusal::no_v5)
        return;
    if (g_sl_ep_reason == static_cast<LONG>(why)) return;  // counted once per episode and reason
    g_sl_ep_reason = static_cast<LONG>(why);
    InterlockedIncrement(&g_sl_refused);
}
using SetSlotFn = void (*)(void* ctl, std::uint32_t id, std::int32_t type, std::uint8_t sub, char a5, char a6, int a7);
void SlotFollow(const pi::Arm& a, const pi::Injection& j) noexcept {
    if (a.slot_follow == PartnerNativeSlot::off || !a.gun_prim || !a.gun_rec) { SlotVerdict(SlotRefusal::off); return; }
    if (g_sl_faulted) { InterlockedExchange(&g_sl_last, static_cast<LONG>(SlotRefusal::fault)); return; }
    if (!j.slot_valid || !j.gun_valid) { SlotVerdict(SlotRefusal::no_v5); return; }
    const std::uint32_t target = j.inv_slot;
    if (target != g_sl_ep_target || j.switch_seq != g_sl_ep_seq) {
        g_sl_ep_target = target;
        g_sl_ep_seq = j.switch_seq;
        g_sl_ep_writes = 0;
        g_sl_ep_reason = -1;
    }
    InterlockedExchange(&g_sl_target, static_cast<LONG>(target));
    if (target > 2) { SlotVerdict(SlotRefusal::other); return; }
    // The body slot for the remote's slot: itself, or the plan's stand-in (weapons4).
    const std::uint32_t body_target = (a.standin_mask & (1u << target)) ? a.standin_slot[target] : target;
    if (body_target > 2) { SlotVerdict(SlotRefusal::plan); return; }
    SlotRefusal why = SlotRefusal::none;
    bool wrote = false;
    __try {
        const std::uintptr_t P = a.P, rec = a.gun_rec;
        if (Load<std::uintptr_t>(a.gun_prim) != g_base + c::gun_prim_vt_rva || Load<std::uintptr_t>(a.gun_prim + c::prim_blk) != a.blk1 ||
            Load<std::uintptr_t>(a.gun_prim + c::prim_gun_rec) != rec) {
            why = SlotRefusal::chain;
        } else if (Load<std::uint32_t>(P + c::p_flags) & (c::flag_online | c::flag_alt_dispatch)) {
            why = SlotRefusal::online;
        } else if (Load<std::uint32_t>(P + c::p_local) == 1u || Load<std::uint32_t>(P + c::p_local_id) == 1u) {
            why = SlotRefusal::owner;
        } else {
            const std::uintptr_t Q = Load<std::uintptr_t>(P + c::p_q);
            const std::uintptr_t ctl = Load<std::uintptr_t>(P + c::p_equip);
            const std::uintptr_t inv = ctl ? Load<std::uintptr_t>(ctl + c::ctl_inventory) : 0;
            const std::uintptr_t inv_vt = inv ? Load<std::uintptr_t>(inv) : 0;
            const std::uint32_t stride = inv ? Load<std::uint32_t>(inv + c::store_stride) : 0;
            const std::uintptr_t idx = static_cast<std::uintptr_t>(stride) + body_target;  // id 1 (Validate: ids[1] == 1)
            if (!Q || Load<std::uintptr_t>(Q) != g_base + c::q_vtable_rva || !ctl || Load<std::uintptr_t>(ctl) != g_base + c::equip_ctl_vt_rva ||
                Load<std::uintptr_t>(ctl + c::ctl_player) != P || (inv_vt != g_base + c::store_vt_rva && inv_vt != g_base + c::store_vt_derived_rva) ||
                Load<std::uintptr_t>(inv_vt + c::store_vt_set) != g_base + c::store_set_rva || stride < 3 || stride > 16 ||
                idx >= Load<std::uint32_t>(inv + c::store_total) || !Load<std::uintptr_t>(inv + c::inv_handles) ||
                !Load<std::uintptr_t>(Q + c::q_equip_rows)) {
                why = SlotRefusal::chain;
            } else {
                const std::uintptr_t row = Load<std::uintptr_t>(Q + c::q_equip_rows) + c::equip_row_stride;  // id 1
                const std::uint8_t nibble = Load<std::uint8_t>(row + c::equip_row_nibble);
                const std::uint32_t f = Load<std::uint32_t>(rec + c::gun_flags);
                const std::uint8_t mode = Load<std::uint8_t>(rec + c::gun_mode_cur);
                if ((nibble & 0xFu) == body_target) why = SlotRefusal::same;
                else if (Load<std::uint8_t>(rec + c::gun_slot_src) != c::gun_slot_follow) why = SlotRefusal::mode;
                else if ((f & c::gun3c0_switch) || mode == c::gun_mode_change || g_sl_ep_writes >= kSlotEpisodeWrites) why = SlotRefusal::wait;
                else {
                    // The live handle first (read-only): an empty slot (handle >> 16 == 0xFFFF, or equip 0:
                    // a slot body 1 never had holds 0) reports empty, then the plan, then the identity.
                    const std::uint32_t handle = Load<std::uint32_t>(Load<std::uintptr_t>(inv + c::inv_handles) + idx * 4);
                    const std::uint16_t row_equip =
                        static_cast<std::uint16_t>(Load<std::uint16_t>(row + c::equip_row_slots + 2 * body_target) & c::equip_id_mask);
                    if ((handle >> 16) == c::handle_empty_hi || ((handle >> c::handle_equip_shift) & c::equip_id_mask) == 0)
                        why = SlotRefusal::empty;
                    else if (!(a.slot_ok & (1u << body_target))) why = SlotRefusal::plan;
                    else if (((handle >> c::handle_equip_shift) & c::equip_id_mask) != row_equip || row_equip != a.slot_equip[body_target])
                        why = SlotRefusal::mismatch;
                    else {
                        if (a.slot_follow == PartnerNativeSlot::call) {
                            // The call variant: only through the pinned slot (ctl vt+0x10 is
                            // 0x11AFA80, code-gated at startup); id 1 is non-local (checked
                            // above), so it takes the data-only path to 0x11AFE4B.
                            if (Load<std::uintptr_t>(Load<std::uintptr_t>(ctl) + 0x10) != g_base + c::set_current_slot_rva) {
                                why = SlotRefusal::chain;
                            } else {
                                ++g_sl_ep_writes;
                                reinterpret_cast<SetSlotFn>(g_base + c::set_current_slot_rva)(reinterpret_cast<void*>(ctl), 1u,
                                                                                             static_cast<std::int32_t>(body_target), 0, 0, 0, 0);
                                wrote = true;
                            }
                        } else {
                            ++g_sl_ep_writes;
                            Store<std::uint32_t>(Q + c::q_equip_dirty, Load<std::uint32_t>(Q + c::q_equip_dirty) | 2u);
                            Store<std::uint8_t>(row + c::equip_row_nibble, static_cast<std::uint8_t>((nibble & 0xF0u) | body_target));
                            Store<std::uint16_t>(row + c::equip_row_current, row_equip);
                            wrote = true;
                        }
                        if (wrote && ((Load<std::uint8_t>(row + c::equip_row_nibble) & 0xFu) != body_target ||
                                      Load<std::uint16_t>(row + c::equip_row_current) != row_equip))
                            why = SlotRefusal::readback;
                    }
                }
            }
        }
    } __except (SlotFilter(GetExceptionCode())) {
        why = SlotRefusal::fault;
    }
    if (wrote) InterlockedIncrement(&g_sl_writes);
    SlotVerdict(why);
}
// vars.playerShootCountInMission (read-only leak evidence; 0xFFFFFFFF unread).
std::uint32_t ReadShotCount() noexcept {
    std::uint32_t value = 0xFFFFFFFFu;
    __try {
        const std::uintptr_t vars = Load<std::uintptr_t>(g_base + c::vars_slot_rva);
        if (vars) value = Load<std::uint32_t>(vars + c::vars_shoot_count);
    } __except (ProbeFilter(GetExceptionCode())) {}
    return value;
}
// The viewer's weapon/ammo vars block vars+0x548..+0x670 (the contiguous
// registrations weapons .. u16[46] at +0x614, contract-pinned): an FNV-1a
// digest, 0 when unread (read-only leak evidence: instance 1's job, its
// reload completion included, must never change the viewer's inventory).
std::uint32_t ReadAmmoDigest() noexcept {
    static_assert(c::vars_ammo_end > c::vars_ammo_begin && c::vars_ammo_end - c::vars_ammo_begin <= 0x200, "bounded vars block");
    unsigned char block[c::vars_ammo_end - c::vars_ammo_begin];
    bool ok = false;
    __try {
        const std::uintptr_t vars = Load<std::uintptr_t>(g_base + c::vars_slot_rva);
        if (vars) {
            std::memcpy(block, reinterpret_cast<const void*>(vars + c::vars_ammo_begin), sizeof(block));
            ok = true;
        }
    } __except (ProbeFilter(GetExceptionCode())) {}
    if (!ok) return 0;
    std::uint32_t h = 2166136261u;
    for (const unsigned char b : block) h = (h ^ b) * 16777619u;
    return h ? h : 1u;
}
// After the job: the leak verdicts and whether instance 1's reload request is set.
void AfterJob(const pi::Arm& a, std::uint32_t shots_before, std::uint32_t ammo_before) noexcept {
    const std::uint32_t shots = ReadShotCount();
    if (shots_before != 0xFFFFFFFFu && shots != 0xFFFFFFFFu && shots != shots_before) InterlockedIncrement(&g_shot_leaks);
    InterlockedExchange(&g_shot_count, static_cast<LONG>(shots));
    const std::uint32_t ammo = ReadAmmoDigest();
    if (ammo_before && ammo && ammo != ammo_before) {
        InterlockedIncrement(&g_ammo_leaks);
        // Instance 1's job changed the viewer's inventory: no more mirrored
        // magazines (no reloads they enable) until MissionStart.
        if (a.mag_mirror && InterlockedExchange(&g_m_leak, 1) == 0 && g_log) g_log("partner_input=ammo_leak; mag_mirror=disabled\r\n");
    }
    InterlockedExchange(&g_ammo_digest, static_cast<LONG>(ammo));
    if (!a.gun_rec || !a.gun_prim) return;
    bool reload = false, shot = false, fire_mode = false;
    std::uint32_t requested = 0;
    __try {
        if (Load<std::uintptr_t>(a.gun_prim + c::prim_gun_rec) == a.gun_rec) {
            const std::uint32_t f = Load<std::uint32_t>(a.gun_rec + c::gun_flags);
            const std::uint8_t mode = Load<std::uint8_t>(a.gun_rec + c::gun_mode_cur);
            reload = (f & c::gun3c0_reload) != 0;
            // The trigger 0x1095DF0 sets bit 24 and the shot count for this update (0x10964F7).
            shot = (f & c::gun3c0_shot) != 0;
            requested = Load<std::uint8_t>(a.gun_rec + c::gun_shots);
            fire_mode = mode == c::gun_mode_fire || mode == c::gun_mode_fire2;
        }
    } __except (ProbeFilter(GetExceptionCode())) {}
    if (reload) InterlockedIncrement(&g_reload_drains);
    if (shot) {
        InterlockedIncrement(&g_f_shot_drains);
        InterlockedAdd(&g_f_requested, static_cast<LONG>(requested ? requested : 1u));
    }
    if (fire_mode) InterlockedIncrement(&g_f_modes);
}

// A deferred job without a window: a fatal game code is recorded and passed on.
bool GuardedJobCall(JobCallFn job, void* go, const void* param) noexcept {
    if (PoseOwned()) { PoseFail("pose_nested_refused_job"); return false; }
    PoseSharedLease shared{}; if (!PoseEnterShared(shared)) return false;
    __try {
        __try { job(go, param); }
        __except (GameFilter(GetExceptionCode())) {}
    } __finally { PoseExitShared(shared); }
    return true;
}
void WrapSvcActive(void* ctx, void* root, std::uint32_t phase) noexcept { WrapEnter(Call{0, ctx, root, phase, 0}); }
void WrapSvcAll(void* ctx, void* root, std::uint32_t phase) noexcept { WrapEnter(Call{1, ctx, root, phase, 0}); }
void WrapSvcId(void* ctx, void* root, std::uint32_t phase, std::uint32_t id) noexcept { WrapEnter(Call{2, ctx, root, phase, id}); }
void WrapWalk7(void* root, std::uint32_t phase) noexcept { WrapEnter(Call{3, nullptr, root, phase, 0}); }
void WrapWalk11(void* root, std::uint32_t phase, std::uint32_t id) noexcept { WrapEnter(Call{4, nullptr, root, phase, id}); }

// --- PP vtable copy slots --------------------------------------------------------
std::uint32_t WordFor(detail::KmWord w) noexcept {
    switch (w) {
    case detail::KmWord::held: return g_cur.held;
    case detail::KmWord::press: return g_cur.press;
    case detail::KmWord::release: return g_cur.release;
    default: return 0;
    }
}
bool BitAt(std::uint32_t word, std::uint32_t idx) noexcept { return idx < 32 && ((word >> idx) & 1u) != 0; }
// The binding index of a key handle (PP+0x390[i], i < 26), or -1.
int HandleIndex(void* pp, std::uint64_t handle) noexcept {
    std::uint64_t handles[pi::kBindingCount]{};
    std::memcpy(handles, reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(pp) + c::pp_bindings), sizeof(handles));
    return pi::BindingIndex(handles, handle);
}
bool __stdcall KmPressIdx(void*, std::uint32_t idx) noexcept { InterlockedIncrement(&g_km_calls); return BitAt(g_cur.press, idx); }
bool __stdcall KmReleaseIdx(void*, std::uint32_t idx) noexcept { InterlockedIncrement(&g_km_calls); return BitAt(g_cur.release, idx); }
bool __stdcall KmHeldIdx(void*, std::uint32_t idx) noexcept { InterlockedIncrement(&g_km_calls); return BitAt(g_cur.held, idx); }
bool __stdcall KmZeroIdx(void*, std::uint32_t) noexcept { InterlockedIncrement(&g_km_calls); return false; }
bool __stdcall KmHeldHandle(void* pp, std::uint64_t h) noexcept {
    InterlockedIncrement(&g_km_calls);
    const int i = HandleIndex(pp, h);
    return i >= 0 && BitAt(g_cur.held, static_cast<std::uint32_t>(i));
}
bool __stdcall KmPressHandle(void* pp, std::uint64_t h) noexcept {
    InterlockedIncrement(&g_km_calls);
    const int i = HandleIndex(pp, h);
    return i >= 0 && BitAt(g_cur.press, static_cast<std::uint32_t>(i));
}
bool __stdcall KmReleaseHandle(void* pp, std::uint64_t h) noexcept {
    InterlockedIncrement(&g_km_calls);
    const int i = HandleIndex(pp, h);
    return i >= 0 && BitAt(g_cur.release, static_cast<std::uint32_t>(i));
}

std::uint32_t OwnCode(std::uint32_t btn, std::uint32_t frames) noexcept {
    std::uint32_t code = pi::kHoldUseOwn;
    if (g_cur.hold == PartnerInputHold::net) code = pi::NetHoldCode(g_cur.live, g_cur.held, g_cur.long12, g_cur.tap12, btn, frames);
    if (code == pi::kHoldUseOwn) code = pi::ClassifyHold(g_hist, g_cur.held, g_cur.w54, btn, frames);
    if ((btn & 31u) == 1u && code < 4) InterlockedIncrement(&g_hold_own[code]);
    return code;
}
std::uint32_t __stdcall HoldNone(void*, std::uint32_t, std::uint32_t) noexcept { return 0; }
// PP vt+0x90 replica: the mask exit of 0x1205C30 (bt [PP+0x28], btn), then
// the classifier over instance 1's own history.
std::uint32_t __stdcall HoldOwn(void* pp, std::uint32_t btn, std::uint32_t frames) noexcept {
    InterlockedIncrement(&g_hold_calls);
    const std::uint32_t mask = Load<std::uint32_t>(reinterpret_cast<std::uintptr_t>(pp) + c::pp_masks);
    if ((mask >> (btn & 31u)) & 1u) return 0;
    return OwnCode(btn, frames);
}
// PP vt+0x140 (the key-manager classifier, no mask gate in 0x1205DF0).
std::uint32_t __stdcall KmHoldOwn(void*, std::uint32_t idx, std::uint32_t frames) noexcept {
    InterlockedIncrement(&g_hold_calls);
    return OwnCode(idx, frames);
}
std::uint32_t __stdcall HoldLog(void* pp, std::uint32_t btn, std::uint32_t frames) noexcept {
    const std::uint32_t result = g_hold_orig ? g_hold_orig(pp, btn, frames) : 0;
    InterlockedIncrement(&g_hold_calls);
    const LONG at = InterlockedIncrement(&g_hold_head) - 1;
    g_hold_ring[at & 63] = pi::HoldRecord{btn, frames, result};
    return result;
}

std::uintptr_t Fn(const void* f) noexcept { return reinterpret_cast<std::uintptr_t>(f); }
// The key-manager slot stubs of a window copy.
void InstallKmStubs(std::uintptr_t table[43]) noexcept {
    const auto set = [&](std::size_t offset, std::uintptr_t value) noexcept { table[1 + offset / 8] = value; };
    set(c::pp_vt_km_press_idx, Fn(reinterpret_cast<const void*>(&KmPressIdx)));
    set(c::pp_vt_km_release_idx, Fn(reinterpret_cast<const void*>(&KmReleaseIdx)));
    set(c::pp_vt_km_148_idx, Fn(reinterpret_cast<const void*>(&KmZeroIdx)));
    set(c::pp_vt_km_held_idx, Fn(reinterpret_cast<const void*>(&KmHeldIdx)));
    set(c::pp_vt_km_held_h, Fn(reinterpret_cast<const void*>(&KmHeldHandle)));
    set(c::pp_vt_km_press2_idx, Fn(reinterpret_cast<const void*>(&KmPressIdx)));
    set(c::pp_vt_km_press2_h, Fn(reinterpret_cast<const void*>(&KmPressHandle)));
    set(c::pp_vt_km_release2_idx, Fn(reinterpret_cast<const void*>(&KmReleaseIdx)));
    set(c::pp_vt_km_release2_h, Fn(reinterpret_cast<const void*>(&KmReleaseHandle)));
    set(c::pp_vt_km_held2_idx, Fn(reinterpret_cast<const void*>(&KmHeldIdx)));
    set(c::pp_vt_km_held2_h, Fn(reinterpret_cast<const void*>(&KmHeldHandle)));
}

// The live slots we replace must equal the image targets before copying.
bool LiveSlotsOk() noexcept {
    const std::uintptr_t vt = g_base + c::pp_vtable_rva;
    static constexpr struct { std::size_t offset; std::uintptr_t target; } kSlots[] = {
        {c::pp_vt_hold, c::pp_hold_rva}, {c::pp_vt_km_hold, c::pp_km_hold_rva},
        {c::pp_vt_km_press_idx, 0x1206290}, {c::pp_vt_km_release_idx, 0x1206300}, {c::pp_vt_km_148_idx, 0x1206260},
        {c::pp_vt_km_held_idx, 0x12062c0}, {c::pp_vt_km_held_h, 0x12062e0}, {c::pp_vt_km_press2_idx, 0x1206330},
        {c::pp_vt_km_press2_h, 0x1206350}, {c::pp_vt_km_release2_idx, 0x12063b0}, {c::pp_vt_km_release2_h, 0x12063d0},
        {c::pp_vt_km_held2_idx, 0x1206370}, {c::pp_vt_km_held2_h, 0x1206390}};
    for (const auto& s : kSlots)
        if (Load<std::uintptr_t>(vt + s.offset) != g_base + s.target) return false;
    return true;
}
void CopyVtable(std::uintptr_t table[43]) noexcept {
    std::memcpy(table, reinterpret_cast<const void*>(g_base + c::pp_vtable_rva - 8), sizeof(std::uintptr_t) * 43);
}
bool BuildCopies() noexcept {
    g_vt_ready = false;
    if (!LiveSlotsOk()) return false;
    CopyVtable(g_vt_none);
    CopyVtable(g_vt_log);
    CopyVtable(g_vt_own);
    CopyVtable(g_vt_pass);
    // Block 0 hold log: only +0x90 replaced (the host's own update).
    g_vt_log[1 + c::pp_vt_hold / 8] = Fn(reinterpret_cast<const void*>(&HoldLog));
    // Window copies: key-manager stubs everywhere; hold classifiers per mode.
    InstallKmStubs(g_vt_none);
    InstallKmStubs(g_vt_own);
    InstallKmStubs(g_vt_pass);
    g_vt_none[1 + c::pp_vt_hold / 8] = Fn(reinterpret_cast<const void*>(&HoldNone));
    g_vt_none[1 + c::pp_vt_km_hold / 8] = Fn(reinterpret_cast<const void*>(&HoldNone));
    g_vt_own[1 + c::pp_vt_hold / 8] = Fn(reinterpret_cast<const void*>(&HoldOwn));
    g_vt_own[1 + c::pp_vt_km_hold / 8] = Fn(reinterpret_cast<const void*>(&KmHoldOwn));
    // pass keeps the game's +0x90 (diagnostic: it reads the host's history)
    // and answers +0x140 with 0 (its key manager is the host keyboard).
    g_vt_pass[1 + c::pp_vt_km_hold / 8] = Fn(reinterpret_cast<const void*>(&HoldNone));
    g_vt_ready = true;
    return true;
}

} // namespace

namespace detail {
// The 27-byte dispatch block at contract site 0x9C2B40, with the call rel32
// operand at offset 18 (0x9C2B52 - 0x9C2B40) excluded from the comparison.
inline constexpr unsigned char kBlock[] = {0x84, 0xd2, 0x75, 0x17, 0xba, 0x08, 0x00, 0x00, 0x00, 0x49, 0x8b, 0x8c, 0x00,
                                           0x90, 0x01, 0x00, 0x00, 0xe8, 0x7a, 0x36, 0x23, 0x00, 0xe9, 0xcc, 0x00, 0x00, 0x00};
inline constexpr std::size_t kBlockSize = sizeof(kBlock);
inline constexpr std::size_t kRelOffset = 18;  // the 0xE8 operand
bool BlockMatches(const unsigned char* block) noexcept {
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        if (i >= kRelOffset && i < kRelOffset + 4) continue;  // the retargetable rel32
        if (block[i] != kBlock[i]) return false;
    }
    return block[kRelOffset - 1] == 0xE8;
}
void BuildStub(unsigned char out[14], const void* target) noexcept {
    out[0] = 0xFF; out[1] = 0x25;
    std::memset(out + 2, 0, 4);
    const auto t = reinterpret_cast<std::uintptr_t>(target);
    std::memcpy(out + 6, &t, 8);
}
std::size_t BlockSize() noexcept { return kBlockSize; }
// A near stub at a rel32 call site retargeted to `target` (Install's pattern;
// also used by partner_defer for H2). nullptr on success, else a reason; on
// "protect_restore" the patch IS live (stub_out set).
const char* RetargetCall(std::uintptr_t site, std::uintptr_t next_ip, std::uint32_t orig_rel, const void* target, void*& stub_out) noexcept {
    const unsigned char* code = reinterpret_cast<const unsigned char*>(site);
    std::uint32_t live = 0;
    std::memcpy(&live, code + 1, 4);
    if (code[0] != 0xE8 || live != orig_rel) return "bytes";
    void* stub = near_code::AllocateNear(next_ip, 0x20);
    if (!stub) return "allocate_near";
    std::int32_t rel = 0;
    if (!near_code::Rel32(next_ip, reinterpret_cast<std::uintptr_t>(stub), rel)) { VirtualFree(stub, 0, MEM_RELEASE); return "range"; }
    unsigned char s[14]{};
    BuildStub(s, target);
    std::memcpy(stub, s, sizeof(s));
    DWORD old = 0;
    if (!VirtualProtect(stub, 0x20, PAGE_EXECUTE_READ, &old)) { VirtualFree(stub, 0, MEM_RELEASE); return "protect"; }
    FlushInstructionCache(GetCurrentProcess(), stub, 0x20);
    stub_out = stub;
    std::uint32_t new_rel = 0;
    std::memcpy(&new_rel, &rel, 4);
    if (const char* why = near_code::PatchDword(reinterpret_cast<void*>(site + 1), orig_rel, new_rel))
        return std::strcmp(why, "changed") == 0 ? "exchange" : why;
    return nullptr;
}
KmWord KmSlotWord(std::size_t slot) noexcept {
    switch (slot) {
    case c::pp_vt_km_press_idx: case c::pp_vt_km_press2_idx: case c::pp_vt_km_press2_h: return KmWord::press;
    case c::pp_vt_km_release_idx: case c::pp_vt_km_release2_idx: case c::pp_vt_km_release2_h: return KmWord::release;
    case c::pp_vt_km_held_idx: case c::pp_vt_km_held_h: case c::pp_vt_km_held2_idx: case c::pp_vt_km_held2_h: return KmWord::held;
    case c::pp_vt_km_148_idx: return KmWord::zero;
    default: return KmWord::none;
    }
}
void BindWrapForTest(SvcFn active, SvcFn all, SvcIdFn per_id, WalkFn walk7, WalkIdFn walk11, std::uint32_t hook_thread) noexcept {
    g_svc_active = active;
    g_svc_all = all;
    g_svc_id = per_id;
    g_walk7 = walk7;
    g_walk11 = walk11;
    InterlockedExchange(&g_hook_thread, static_cast<LONG>(hook_thread));
}
void CallWrapForTest(std::uint8_t entry, void* ctx, void* root, std::uint32_t phase, std::uint32_t id) noexcept {
    switch (entry) {
    case 0: WrapSvcActive(ctx, root, phase); break;
    case 1: WrapSvcAll(ctx, root, phase); break;
    case 2: WrapSvcId(ctx, root, phase, id); break;
    case 3: WrapWalk7(root, phase); break;
    default: WrapWalk11(root, phase, id); break;
    }
}
void SetDrainForTest(const pi::Arm& arm, const pi::Injection& injection, bool in_drain) noexcept {
    g_drain_arm = arm;
    g_drain_inj = injection;
    g_in_drain = in_drain;
    // A drain is a window: odd while inside.
    if (((g_window_seq & 1) != 0) != in_drain) InterlockedIncrement(&g_window_seq);
}
void BindDispatchForTest(DispatchFn original) noexcept { g_original = original; }
void* DispatchStubForTest() noexcept { return g_stub; }
bool KmSlotByIndex(std::size_t slot) noexcept {
    return slot == c::pp_vt_km_press_idx || slot == c::pp_vt_km_release_idx || slot == c::pp_vt_km_148_idx ||
           slot == c::pp_vt_km_held_idx || slot == c::pp_vt_km_press2_idx || slot == c::pp_vt_km_release2_idx ||
           slot == c::pp_vt_km_held2_idx;
}

} // namespace detail

const char* Install(const void* base, LogFn log, SampleFn sample) noexcept {
    if (InterlockedCompareExchange(&g_installed, 1, 0) != 0) return g_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    g_base = reinterpret_cast<std::uintptr_t>(base);
    g_log = log;
    g_sample = sample;
    g_original = reinterpret_cast<DispatchFn>(g_base + c::dispatch_rva);
    g_hold_orig = reinterpret_cast<HoldFn>(g_base + c::pp_hold_rva);
    const char* result = nullptr;
    std::uint32_t new_rel = 0;
    const std::uintptr_t site = g_base + c::hook_site;
    const std::uintptr_t next_ip = g_base + c::hook_next_ip;
    unsigned char block[detail::kBlockSize]{};
    std::memcpy(block, reinterpret_cast<const void*>(g_base + c::hook_block_rva), sizeof(block));
    if (!detail::BlockMatches(block)) { result = "bytes"; }
    else if (!BuildCopies()) {
        result = "vt_copy";
    } else {
        void* stub = near_code::AllocateNear(next_ip, 0x20);
        std::int32_t rel = 0;
        if (!stub) result = "allocate_near";
        else if (!near_code::Rel32(next_ip, reinterpret_cast<std::uintptr_t>(stub), rel)) { VirtualFree(stub, 0, MEM_RELEASE); result = "range"; }
        else {
            unsigned char s[14]{};
            detail::BuildStub(s, reinterpret_cast<const void*>(&InputDispatchHook));
            std::memcpy(stub, s, sizeof(s));
            DWORD old = 0;
            if (!VirtualProtect(stub, 0x20, PAGE_EXECUTE_READ, &old)) { VirtualFree(stub, 0, MEM_RELEASE); result = "protect"; }
            else {
                FlushInstructionCache(GetCurrentProcess(), stub, 0x20);
                g_stub = stub;
                std::memcpy(&new_rel, &rel, 4);
                if (const char* why = near_code::PatchDword(reinterpret_cast<void*>(site + 1), c::hook_rel32_orig, new_rel))
                    result = std::strcmp(why, "changed") == 0 ? "exchange" : why;
            }
        }
    }
    if (!result) {
        // The aim-target mirror's base and log before g_ready: no drain
        // window ever runs its Step unbound.
        partner_aim_point::Bind(base, log);
        g_ready = true;
    }
    if (g_log) {
        char line[160]{};
        if (result) StringCchPrintfA(line, ARRAYSIZE(line), "partner_input_hook=refused; reason=%s\r\n", result);
        else StringCchPrintfA(line, ARRAYSIZE(line),
                              "partner_input_hook=installed; site=0x9c2b51; stub=0x%llx; rel32=0x%08x; target=0xbf61d0; vt_copies=4\r\n",
                              static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_stub) - g_base), new_rel);
        g_log(line);
    }
    SetLastError(error);
    return result;
}

bool Installed() noexcept { return g_ready; }

std::uint32_t HookThread() noexcept { return static_cast<std::uint32_t>(g_hook_thread); }

const char* MagRefusalName(MagRefusal refusal) noexcept {
    switch (refusal) {
    case MagRefusal::off: return "off";
    case MagRefusal::no_v4: return "no_v4";
    case MagRefusal::prim: return "prim";
    case MagRefusal::full: return "full";
    case MagRefusal::cls: return "cls";
    case MagRefusal::cap: return "cap";
    case MagRefusal::fault: return "fault";
    case MagRefusal::leak: return "leak";
    case MagRefusal::weapon: return "weapon";
    case MagRefusal::store: return "store";
    case MagRefusal::reload: return "reload";
    default: return "none";
    }
}

const char* SlotRefusalName(SlotRefusal refusal) noexcept {
    switch (refusal) {
    case SlotRefusal::off: return "off";
    case SlotRefusal::no_v5: return "no_v5";
    case SlotRefusal::other: return "other";
    case SlotRefusal::same: return "same";
    case SlotRefusal::wait: return "wait";
    case SlotRefusal::plan: return "plan";
    case SlotRefusal::chain: return "chain";
    case SlotRefusal::online: return "online";
    case SlotRefusal::owner: return "owner";
    case SlotRefusal::mode: return "mode";
    case SlotRefusal::empty: return "empty";
    case SlotRefusal::mismatch: return "mismatch";
    case SlotRefusal::readback: return "readback";
    case SlotRefusal::fault: return "fault";
    default: return "none";
    }
}

const char* StoreRefusalName(StoreRefusal refusal) noexcept {
    switch (refusal) {
    case StoreRefusal::y_vt: return "y_vt";
    case StoreRefusal::slot: return "slot";
    case StoreRefusal::type: return "type";
    case StoreRefusal::owner: return "owner";
    case StoreRefusal::online: return "online";
    case StoreRefusal::tamper: return "tamper";
    case StoreRefusal::cap: return "cap";
    default: return "none";
    }
}

const char* RingRefusalName(RingRefusal refusal) noexcept {
    switch (refusal) {
    case RingRefusal::src: return "src";
    case RingRefusal::cap: return "cap";
    case RingRefusal::fault: return "fault";
    default: return "none";
    }
}

const char* LookRefusalName(LookRefusal refusal) noexcept {
    switch (refusal) {
    case LookRefusal::off: return "off";
    case LookRefusal::no_v3: return "no_v3";
    case LookRefusal::stale: return "stale";
    case LookRefusal::ps: return "ps";
    case LookRefusal::rec_read: return "rec_read";
    case LookRefusal::fault: return "fault";
    default: return "none";
    }
}

void RegisterHudProbe(HudBeforeFn before, HudAfterFn after) noexcept { g_hud_before = before; g_hud_after = after; }

void RegisterNativePoseJobHooks(NativePoseBeforeFn before,NativePoseAfterFn after,NativePoseClosedFn closed) noexcept {
 if(PoseRequired()||PoseOwned())return;g_pose_before=before;g_pose_after=after;g_pose_closed=closed;
}
bool RegisterNativePosePartsPollValidator(NativePosePartsPollValidator callback) noexcept {
 if(!callback||PoseRequired()||PoseOwned()||g_pose_held||g_pose_fault)return false;
 if(g_pose_parts_validator&&g_pose_parts_validator!=callback)return false;g_pose_parts_validator=callback;return true;
}
bool ReadNativePoseSleepPartsPoll(NativePoseSleepPartsContext& out) noexcept {return PosePartsContext(out);}
bool ReadNativePoseDualPartsPoll(NativePoseDualPartsContext& out) noexcept {return PoseDualPartsContext(out);}
bool BindNativePoseWorker(const NativePoseWorkerBindings& bindings) noexcept {
 if(g_pose_installed||PoseRequired()||PoseOwned())return false;
 if(!bindings.admitted||!bindings.refuse)return false;
 g_pose_worker=bindings;return true;
}
const char* InstallNativePoseWindow(const void* image,std::size_t span,bool fingerprint) noexcept {
 return PoseInstall(image,span,fingerprint)?nullptr:g_pose_reason;
}
bool SetNativePoseWindowTest(bool enabled) noexcept {
 if(!enabled)return !PoseRequired(); // never discard a retained test/feature requirement
 if(PoseRequired())return !g_pose_fault;
 pi::Arm a{};
 if(!g_pose_installed||!PoseWorker()||!PoseParticipantsLive()||!g_ready||g_faulted||g_shutdown||!ReadArm(a)||
    !a.enabled||!a.native||a.cam!=PartnerInputCam::sandbox||a.holdlog||
    a.wrap!=PartnerWrap::window||a.config.mode!=PartnerInput::net||!a.gen||!a.rates_epoch||
    static_cast<std::uint32_t>(g_hook_thread)!=GetCurrentThreadId()||partner_defer::InBatch()||
    (g_window_seq&1)||!g_pose_gate.Idle())return false;
 pi::Skip skip=pi::Skip::player;
 __try {skip=Validate(a,reinterpret_cast<void*>(a.root1));}__except(OwnFilter(1,GetExceptionCode())){}
 if(skip!=pi::Skip::none)return false;
 NativePoseJobContext born{};if(!PoseModel(a,born)||!PoseBirthCurrent(a,born,true)){g_pose_reason="pose_actor_birth_unobserved";return false;}
 g_pose_player=a.P;g_pose_go=a.go;g_pose_root=a.root1;g_pose_block=a.blk1;
 g_pose_generation=a.gen;g_pose_epoch=a.rates_epoch;g_pose_thread=GetCurrentThreadId();g_pose_pool=a.pool1;
 g_pose_test=true;MemoryBarrier();InterlockedExchange(&g_pose_required,1);g_pose_reason="pose_window_required";return true;
}
bool ReadNativePoseOriginalCode(std::uintptr_t at,void* out,std::size_t size) noexcept {return PoseOriginalCode(at,out,size);}
bool NativePoseRequiredForJob(void* go,std::uint32_t block) noexcept {return PoseJobRequired(go,block);}
bool NativePoseOwnedWindow() noexcept {return PoseOwned();}
bool NativePoseWholePlayerLifetimeLive() noexcept {
 // Installation freezes the complete exact PE methods/slots above. This live
 // capability additionally revalidates both actual owned entry jumps/relays.
 // Synthetic fixtures do not impersonate installed retail lifecycle hooks.
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
    !g_pose_delete_player||!g_pose_reset_player)return false;
 return PosePatchOwned(g_pose_patches[9])&&PosePatchOwned(g_pose_patches[10])&&
  g_pose_patches[9].rva==0x9bb7a0&&g_pose_patches[10].rva==0x9c2f70;
}

bool NativePoseLocalCacheIsolationLive() noexcept {
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire)||
    !g_pose_selected_export||g_pose_patches[11].rva!=0xff5510)return false;
 return PosePatchOwned(g_pose_patches[11]);
}

bool ReadNativePoseOwnedJobContext(NativePoseJobContext& out) noexcept {
 out={};const auto& c0=t_pose.context;
 if(g_pose_bindings.enabled||!PoseOwned()||!t_pose.job||g_pose_fault||g_shutdown||!g_pose_test||!PoseWorker()||
  partner_defer::InBatch()||GetCurrentThreadId()!=g_pose_thread||!PoseParticipantsLive()||!NativePoseWholePlayerLifetimeLive()||
  !c0.modelReady||!c0.selectedBodyOwned||c0.selectedBody!=1||c0.generation!=g_pose_generation||c0.sceneEpoch!=g_pose_epoch||
  c0.thread!=g_pose_thread||c0.sequence!=static_cast<std::uint32_t>(g_window_seq)||(c0.sequence&1u)==0||!PoseContextBirthCurrent(c0))return false;
 pi::Arm a{},after{};NativePoseJobContext model=c0;std::uintptr_t pp=0,source=0,vt=0;std::uint32_t real=0,selected=0;
 if(!ReadArm(a)||a.P!=c0.player||a.go!=c0.go||a.blk1!=c0.block||a.ps1!=c0.state||a.gen!=c0.generation||a.rates_epoch!=c0.sceneEpoch||
  !a.enabled||!a.native||a.cam!=PartnerInputCam::sandbox||a.pp!=c0.pp||a.src!=c0.source||
  !PoseRead(a.P+0x214,real)||real||!PoseRead(a.P+0x218,selected)||selected!=1||
  !PoseRead(a.P+c::p_pad,pp)||pp!=c0.pp||!PoseRead(pp+c::pp_src,source)||source!=c0.source||
  !PoseRead(pp,vt)||vt!=c0.windowPPVT||vt!=WindowVtable(a.hold)||!PoseModel(a,model)||!model.modelReady||
  model.modelCS!=c0.modelCS||model.modelEntry!=c0.modelEntry||!PoseContextBirthCurrent(c0)||!PoseCarryPairCurrent()||
  !ReadArm(after)||after.P!=a.P||after.go!=a.go||after.blk1!=a.blk1||after.ps1!=a.ps1||after.gen!=a.gen||after.rates_epoch!=a.rates_epoch||
  after.pp!=a.pp||after.src!=a.src||after.hold!=a.hold||!after.enabled||!after.native||!PoseOwned()||g_pose_fault||
  c0.sequence!=static_cast<std::uint32_t>(g_window_seq))return false;
 out=c0;return true;
}
bool RequireNativePoseOwnedJob(void* go,std::uint32_t block) noexcept {
 if(!PoseOwned())return false;
 if(g_pose_fault||reinterpret_cast<std::uintptr_t>(go)!=g_pose_go||block!=g_pose_pool){
  PoseFail("pose_nested_foreign_job");return false;
 }
 return true;
}
bool SetNativePoseLeaseHeld() noexcept {
 if(!PoseOwned()||!t_pose.job||!t_pose.context.modelReady||g_pose_fault)return false;
 if(!PoseContextBirthCurrent(t_pose.context)){PoseFail("pose_actor_birth_changed");return false;}
 if(g_pose_held&&(g_pose_cs!=t_pose.context.modelCS||g_pose_entry!=t_pose.context.modelEntry)){
  PoseFail("pose_held_model_changed");return false;
 }
 g_pose_cs=t_pose.context.modelCS;g_pose_entry=t_pose.context.modelEntry;
 if(!holds::AddSleep(g_pose_owners))return false;InterlockedExchange(&g_pose_held,1);return true;
}
bool RequestNativePoseLeaseClear() noexcept {
 if(!PoseOwned()||!t_pose.job||!g_pose_held||g_pose_fault)return false;
 if(!holds::ClearSleep(g_pose_owners))return false;InterlockedExchange(&g_pose_clear,1);return true;
}
bool NativePoseVarsPairWordOwnedWindow() noexcept {
 // Faulted cleanup keeps the exact writer and the admission established by
 // private opt-in. It cannot promote a native callback or a replacement owner.
 return PoseOwned()&&g_pose_test&&PoseRequired()&&GetCurrentThreadId()==g_pose_thread&&PoseVarsPatchesLive();
}
bool NativePoseVarsPairWordCleanupWindow(const NativePoseJobContext& c0) noexcept {
 return NativePoseVarsPairWordOwnedWindow()&&t_pose.job&&c0.player==t_pose.context.player&&c0.go==t_pose.context.go&&
  c0.block==t_pose.context.block&&c0.state==t_pose.context.state&&c0.modelCS==t_pose.context.modelCS&&c0.modelEntry==t_pose.context.modelEntry&&
  c0.generation==g_pose_generation&&c0.sceneEpoch==g_pose_epoch&&c0.thread==g_pose_thread&&c0.sequence==t_pose.context.sequence&&
  c0.birthSerial==t_pose.context.birthSerial&&PoseContextBirthCurrent(c0);
}
bool ReadNativePoseLocalCarrySlice(NativePoseLocalCarryContext& out) noexcept {return PoseLocalCarryQuery(out);}
bool RunNativePoseLocalCarrySlice(const NativePoseCarryLease& lease,NativePoseLocalCarryFn fn,void* argument) noexcept {
 if(g_pose_bindings.enabled||!fn||!lease.serial||g_pose_fault||g_shutdown||!g_pose_test||!PoseRequired()||!PoseWorker()||
  GetCurrentThreadId()!=g_pose_thread||partner_defer::InBatch()||t_pose.opened||t_local_carry.opened||!t_pose.job||!t_pose.cleanup||
  (g_window_seq&1)||static_cast<std::uint32_t>(g_window_seq)!=t_pose.context.sequence+1u||t_pose_pair_word_token.active()||
  lease.serial!=g_pose_owners.carrySerial||lease.generation!=g_pose_generation||lease.sceneEpoch!=g_pose_epoch||
  !(g_pose_owners.held&holds::carry)||(g_pose_owners.pending&holds::carry)||!g_pose_held||
  !NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||!PoseMessagePatchesLive()||!PoseVarsPatchesLive()||!PoseParticipantsLive())return false;
 px::Admission admission{true,true,true,true,true,true,g_pose_generation,g_pose_epoch,g_pose_thread};
 if(g_pose_gate.TryWrite(admission,t_local_carry.ticket)!=px::Status::entered)return false;
 InterlockedIncrement(&g_window_seq);t_local_carry.opened=true;t_local_carry.context={};
 ++g_local_carry_nonce;if(!g_local_carry_nonce)++g_local_carry_nonce;
 t_local_carry.context.carryLease=lease;t_local_carry.context.sliceNonce=g_local_carry_nonce;
 t_local_carry.context.sequence=static_cast<std::uint32_t>(g_window_seq);
 NativePoseLocalCarryContext context{};bool normal=false,accepted=false,checked=false,released=false;
 __try {
  checked=PoseLocalCarryQuery(context);if(checked)accepted=fn(context,argument);normal=true;
 } __finally {
  NativePoseLocalCarryContext after{};const bool current=checked&&PoseLocalCarryQuery(after)&&SameNativePoseLocalCarrySlice(context,after);
  const bool clean=normal&&accepted&&current&&!g_pose_fault;
  if(!clean)PoseFail("pose_local_carry_slice_changed");
  InterlockedIncrement(&g_window_seq);
  released=g_pose_gate.EndWrite(g_pose_thread,g_pose_generation,g_pose_epoch,clean,true,t_local_carry.ticket);
  if(released)t_local_carry.opened=false;else PoseFail("pose_local_carry_slice_cleanup");
 }
 return normal&&accepted&&released&&!g_pose_fault;
}
bool NativePoseCarryRecipientHeld() noexcept {return PoseOwned()&&(g_pose_owners.held&holds::carry)!=0;}
bool NativePoseCarryRecipientLeaseCurrent(const NativePoseCarryLease& lease) noexcept {
 if(!PoseOwned()||!t_pose.job)return false;
 if(!lease.serial||lease.serial!=g_pose_owners.carrySerial||lease.generation!=g_pose_generation||lease.sceneEpoch!=g_pose_epoch||
    !t_pose.context.selectedBodyOwned||t_pose.context.selectedBody!=1||
    !(g_pose_owners.held&holds::carry)||!NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||!NativePoseVarsPairWordOwnedWindow()||!PoseParticipantsLive()||!PoseMessageCurrent())return false;
 return PoseCarryPairCurrent()&&PoseRouteCurrent();
}
bool ReadNativePoseCarryColdStatus(NativePoseCarryColdStatus& out) noexcept {
 out={};const DWORD incoming=GetLastError();
 if(g_pose_bindings.enabled||!g_pose_test||!PoseRequired()||!PoseWorker()||g_pose_fault||g_shutdown||
  GetCurrentThreadId()!=g_pose_thread||t_pose.opened||t_local_carry.opened||partner_defer::InBatch()||
  (g_window_seq&1)||g_pose_held||g_pose_clear||g_pose_owners.held||g_pose_owners.pending||
  !PoseParticipantsLive()||!NativePoseWholePlayerLifetimeLive()){SetLastError(incoming);return false;}
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return false;}bool accepted=false;
 __try {
  const bool owned=read.tracked&&read.thread==GetCurrentThreadId()&&read.slot<32&&t_readers.serial[read.slot]==read.serial&&
   g_pose_gate.OwnsRead(read.thread,t_readers.tickets[read.slot]);
  std::uint64_t epoch=0;AcquireSRWLockShared(&g_pose_birth_lock);epoch=g_pose_birth_epoch;ReleaseSRWLockShared(&g_pose_birth_lock);
  pi::Arm a{},after{};route::Observation routeBefore{},routeAfter{};msg::Observation messageBefore{},messageAfter{};
  std::uintptr_t vars=0,q=0;std::uint16_t word=0xffff,wordAfter=0xffff;std::uint32_t local=0,selected=0;
  dual_parts_cold_profile::Stamp profileBefore{},profileAfter{};std::uint32_t profileReasonAfter=0;
  NativePoseCarryColdStatus value{};value.sequence=static_cast<std::uint32_t>(g_window_seq);
  const bool root=owned&&ReadArm(a)&&a.P==g_pose_player&&a.go==g_pose_go&&a.blk1==g_pose_block&&a.gen==g_pose_generation&&a.rates_epoch==g_pose_epoch&&
   a.enabled&&a.native&&PoseRead(a.P+0x214,local)&&!local&&PoseRead(a.P+0x218,selected)&&!selected;
  if(root){
   value.local=local;value.selected=selected;value.routeReady=route::ReadPair(PoseReader,g_base,a.P,routeBefore);
   value.routeNoWork=value.routeReady&&route::NoClassWork(routeBefore);
   if(value.routeReady){value.routeType=0x23063f8;value.registryType=0x23073c0;value.routeMode=routeBefore.mode;value.routeFlags=routeBefore.metadataFlags;value.routeCount=routeBefore.count;value.routePending=routeBefore.pendingCount;}
   value.messageReady=msg::ReadPair(PoseReader,g_base,a.P,messageBefore);
   if(value.messageReady){value.messageType=0x20e9b60;value.messageKey=messageBefore.key;value.messageEnabled=messageBefore.enabled;}
   value.varsReady=PoseRead(a.P+0x140,vars)&&vars==g_base+0x2c01360&&PoseRead(a.P+0x60,q)&&q&&PoseRead(vars+0x10e,word);
   value.word=word;value.varsDomain=PoseVarsPatchesLive();value.messageDomain=PoseMessagePatchesLive();value.routeDomain=PoseRoutePatchesLive();
   value.localSliceActive=false; // cold shared query is NEVER an exclusive slice
   value.profileReady=dual_parts_cold_profile::Read(PoseReader,g_base,a.P,profileBefore,value.profileReason);
   value.profileCurrent=value.profileReady&&value.profileReason==0;
   if(value.profileReady){const auto& p=profileBefore.profile;value.profileType=p.type;value.profileParts=p.parts;value.profileCamo=p.camo;
    value.profileAux=p.auxiliary;value.profileFace=p.face;value.profileRefresh=p.refresh;value.currentType=p.currentType;value.currentParts=p.currentParts;
    value.currentCamo=p.currentCamo;value.currentEncoded=p.currentEncoded;value.currentAux=p.currentAux;value.currentFace=p.currentFace;value.profileSelector=p.selector;}
   const bool profileSame=!value.profileReady||(dual_parts_cold_profile::Read(PoseReader,g_base,a.P,profileAfter,profileReasonAfter)&&
    profileReasonAfter==value.profileReason&&dual_parts_cold_profile::Same(profileBefore,profileAfter));
   const bool routeSame=!value.routeReady||(route::ReadPair(PoseReader,g_base,a.P,routeAfter)&&route::Same(routeBefore,routeAfter));
   const bool messageSame=!value.messageReady||(msg::ReadPair(PoseReader,g_base,a.P,messageAfter)&&msg::SameLease(messageBefore,messageAfter));
   const bool varsSame=!value.varsReady||(PoseRead(vars+0x10e,wordAfter)&&word==wordAfter);
   AcquireSRWLockShared(&g_pose_birth_lock);const bool birthSame=epoch==g_pose_birth_epoch;ReleaseSRWLockShared(&g_pose_birth_lock);
   if(routeSame&&messageSame&&varsSame&&profileSame&&birthSame&&ReadArm(after)&&after.P==a.P&&after.go==a.go&&after.blk1==a.blk1&&
    after.gen==a.gen&&after.rates_epoch==a.rates_epoch&&value.sequence==static_cast<std::uint32_t>(g_window_seq)&&
    g_pose_gate.OwnsRead(read.thread,t_readers.tickets[read.slot])&&!g_pose_fault){out=value;accepted=true;}
  }
 } __finally {PoseExitShared(read);SetLastError(incoming);}
 return accepted&&!g_pose_fault;
}
bool NativePoseDispatchInterfaceLifetimeLive() noexcept {return PoseRoutePatchesLive()&&PoseParticipantsLive()&&NativePoseWholePlayerLifetimeLive();}
bool AcquireNativePoseCarryRecipientLease(NativePoseCarryLease& out) noexcept {
 out={};if(g_pose_bindings.enabled||!PoseOwned()||!t_pose.job||g_pose_fault||!PoseWorker()||
   !t_pose.context.modelReady||!t_pose.context.selectedBodyOwned||t_pose.context.selectedBody!=1||
   !NativePoseWholePlayerLifetimeLive()||!NativePoseLocalCacheIsolationLive()||!NativePoseVarsPairWordOwnedWindow()||!PoseParticipantsLive()||!PoseContextBirthCurrent(t_pose.context))return false;
 if(g_pose_owners.held&holds::carry){
  out={g_pose_owners.carrySerial,g_pose_generation,g_pose_epoch};if(NativePoseCarryRecipientLeaseCurrent(out))return true;out={};return false;
 }
 cp::Pair pair{};route::Observation dispatch{};auto read=PoseReader;
 if(!cp::ReadPair(read,g_base,g_pose_player,pair)||pair.go!=g_pose_go||pair.body[1].block!=t_pose.context.block||
    pair.body[1].state!=t_pose.context.state||pair.body[1].cs!=t_pose.context.modelCS||pair.body[1].entry!=t_pose.context.modelEntry||
    !PoseCarryBirth0Current(pair,true)||pair.body[0].freeCallbacks<2||!PoseRouteCapture(pair,dispatch)||!PoseMessageCapture(pair))return false;
 ++g_pose_carry_serial;if(!g_pose_carry_serial)++g_pose_carry_serial;
 if(!holds::AddCarry(g_pose_owners,g_pose_carry_serial))return false;
 g_pose_carry_pair=pair;g_pose_route_hold=dispatch;g_pose_route_bound=true;g_pose_cs=t_pose.context.modelCS;g_pose_entry=t_pose.context.modelEntry;
 InterlockedExchange(&g_pose_held,1);out={g_pose_carry_serial,g_pose_generation,g_pose_epoch};return true;
}
bool RequestNativePoseCarryRecipientLeaseClear(const NativePoseCarryLease& lease) noexcept {
 if(!NativePoseCarryRecipientLeaseCurrent(lease))return false;auto read=PoseReader;
 // Both typed participant phases, callback owners and Q allocation must retire.
 // No producer/end/native call is introduced by this release check.
 if(!cp::Retired(read,g_base,g_pose_carry_pair)||!PoseCarryPairCurrent()||!holds::ClearCarry(g_pose_owners,lease.serial))return false;
 InterlockedExchange(&g_pose_clear,1);return true;
}
bool EnterNativePoseShared(PoseSharedLease& out) noexcept {return PoseEnterShared(out);}
void ExitNativePoseShared(PoseSharedLease& out) noexcept {PoseExitShared(out);}
NativePoseStatus NativePoseWindowStatus() noexcept {
 AcquireSRWLockShared(&g_pose_birth_lock);const auto born=g_pose_birth;ReleaseSRWLockShared(&g_pose_birth_lock);
 return {g_pose_installed,PoseRequired(),g_pose_held!=0,g_pose_fault!=0,PoseOwned(),
  static_cast<std::uint32_t>(g_pose_windows),static_cast<std::uint32_t>(g_pose_suppressed),
  static_cast<std::uint32_t>(g_pose_hints),g_pose_reason,born.ready,born.serial,
  static_cast<std::uint32_t>(g_birth_enter),static_cast<std::uint32_t>(g_birth_ready),static_cast<std::uint32_t>(g_birth_exact),
  static_cast<std::uint32_t>(g_birth_body1),static_cast<std::uint32_t>(g_birth_model),static_cast<std::uint32_t>(g_birth_done),
  static_cast<std::uint32_t>(g_birth_overlap),static_cast<std::uint32_t>(g_birth_count),static_cast<std::uint32_t>(g_birth_base),
  static_cast<std::uint32_t>(g_birth_caller),g_pose_birth_reason,static_cast<std::uint32_t>(g_pose_local_exports),
  static_cast<std::uint32_t>(g_pose_cam_owned),static_cast<std::uint32_t>(g_pose_cam_shared),
  static_cast<std::uint32_t>(g_pose_cam_caller),static_cast<std::uint32_t>(g_pose_cam_owner),static_cast<std::uint32_t>(g_pose_cam_body),
  static_cast<std::uint32_t>(g_pose_cam_sender),static_cast<std::uint32_t>(g_pose_cam_filtered)};
}
namespace detail {
void BindNativePoseForTest(const NativePoseTestBindings& bindings) noexcept {
 if(PoseRequired()||PoseOwned())return;g_pose_bindings=bindings;g_pose_installed=bindings.enabled;
}
bool CompleteNativePoseBirthForTest(const partner_input::Arm& a) noexcept {
 if(!g_pose_bindings.enabled||PoseRequired()||PoseOwned())return false;
 NativePoseJobContext c0{};if(!PoseModel(a,c0))return false;
 native_pose_birth::Completion c{};c.identity={a.P,a.blk1,a.ps1,c0.modelCS,c0.modelEntry};
 c.installedAtEntry=c.installedAtExit=c.workerAtEntry=c.workerAtExit=c.exactCaller=c.normalReturn=c.typedPair=true;
 c.entryThread=c.exitThread=GetCurrentThreadId();c.lifecycleEpoch=PoseBirthEpoch();return PoseRecordBirth(c);
}
bool ResetNativePoseForTest() noexcept {
 if(!g_pose_bindings.enabled||PoseOwned()||g_pose_held||!g_pose_gate.Idle())return false;
 InterlockedExchange(&g_pose_required,0);g_pose_test=false;g_pose_before=nullptr;g_pose_after=nullptr;g_pose_closed=nullptr;
 return true;
}
}
JobWindowOutcome RunJobWindowOutcome(void* go, const void* param, std::uint32_t block, JobCallFn job, pi::Consumer consumer) noexcept {
    const bool required=PoseJobRequired(go,block);
    pi::Arm a{};
    const auto refusal=[&](reject::Stage stage,const char* detail,std::uint32_t skip=UINT32_MAX) noexcept {
        if(required){PoseRejectDiagnostic(stage,detail,&a,static_cast<std::uint32_t>(consumer),skip);InterlockedIncrement(&g_pose_suppressed);return JobWindowOutcome::suppressed;}
        return GuardedJobCall(job,go,param)?JobWindowOutcome::legacy:JobWindowOutcome::suppressed;
    };
    const bool have = g_ready && !g_faulted && ReadArm(a);
    const bool armed = have && g_inj_valid && a.enabled && a.native && a.config.mode == PartnerInput::net && a.gen == g_gen;
    if (!armed) {
        // No enabled native Arm at all: block 1's job reads the viewer's pad
        // (the arbiter leaves active on this; a gen change in flight is not it).
        if (have && (!a.enabled || !a.native)) InterlockedIncrement(&g_n_drain_disarmed);
        InterlockedIncrement(&g_n_drain_refused);
        return refusal(reject::Stage::arm,"not_armed");
    }
    // A batch in flight anywhere (nested on this thread or on another thread):
    // block 0's job may be running on a worker, the shared pad must not be
    // swapped. The arbiter leaves on wrap_in_batch.
    if (partner_defer::InBatch()) {
        InterlockedIncrement(&g_n_drain_in_batch);
        InterlockedIncrement(&g_w_in_batch);
        InterlockedIncrement(&g_n_drain_refused);
        return refusal(reject::Stage::batch,"batch_live");
    }
    pi::Skip skip = pi::Skip::player;
    bool validated = false, block_ok = false;
    const auto go_addr = reinterpret_cast<std::uintptr_t>(go);
    __try {
        skip = Validate(a, reinterpret_cast<void*>(a.root1));
        if (skip == pi::Skip::none && (go_addr != a.go || Load<std::uintptr_t>(go_addr) != g_base + c::go_vtable_rva ||
                                       go_addr + c::go_to_p != a.P))
            skip = pi::Skip::p_vtable;
        // Only instance 1's block ever runs inside the window: the job's block
        // is the Arm's pool1 (Validate checked the live pool[1]) and never
        // instance 0's live pool.
        if (skip == pi::Skip::none)
            block_ok = block == a.pool1 && Load<std::uint8_t>(Load<std::uintptr_t>(a.P + c::p_pool)) != block;
        validated = true;
    } __except (OwnFilter(1, GetExceptionCode())) {}
    if (!validated || skip != pi::Skip::none) {
        if (validated) {
            InterlockedIncrement(&g_skips);
            InterlockedExchange(&g_last_skip, static_cast<LONG>(skip));
        }
        InterlockedIncrement(&g_n_drain_refused);
        return refusal(reject::Stage::validate,validated?pi::SkipName(skip):"validation_fault",static_cast<std::uint32_t>(skip));
    }
    if (!block_ok) {
        InterlockedIncrement(&g_n_drain_block);
        InterlockedIncrement(&g_n_drain_refused);
        return refusal(reject::Stage::pool,"wrong_job_pool");
    }
    pi::ConsumerView view{};
    const pi::Injection j = NativeView(a, pi::ForConsumer(g_last_inj, g_frame_id, g_consumer_last[static_cast<std::size_t>(consumer)],
                                                          NowMs() - g_frame_ms, a.config, &view));
    InterlockedIncrement(&g_n_views[static_cast<std::size_t>(view)]);
    const std::uint64_t t0 = NowUs();
    Save save{};
    if (!BeginWindow(&a)) {
        if(required){PoseRejectDiagnostic(reject::Stage::begin,g_pose_reason,&a,static_cast<std::uint32_t>(consumer));return JobWindowOutcome::suppressed;}
        return refusal(reject::Stage::begin,g_pose_reason);
    }
    __try {
    // isLocal always: block 1's whole update runs here (phases 0/5/6 read the
    // pad only for a local record; local=all only adds the walk windows).
    if (!a.src) {
        InterlockedIncrement(&g_r_refused);
        InterlockedExchange(&g_r_last, static_cast<LONG>(RingRefusal::src));
        InterlockedExchange(&g_r_on, 0);
    }
    if (!SafePre(a, j, save, WindowOpts{true, true, a.src != 0, static_cast<std::uint8_t>(consumer)})) {
        EndWindow();
        InterlockedIncrement(&g_n_drain_refused);
        return refusal(reject::Stage::pre,"safe_pre_refused");
    }
    SetCurrent(j, a.hold);
    g_drain_arm = a;
    g_drain_inj = j;
    g_in_drain = true;
    InterlockedExchange(&g_d_held, static_cast<LONG>(j.held));
    InterlockedExchange(&g_d_mag, pi::Milli(j.mag));
    InterlockedExchange(&g_d_view, static_cast<LONG>(view));
    InterlockedExchange(&g_d_local_pre, static_cast<LONG>(save.rec_2ac));
    // Native rates: instance 1's record gets the remote's movement rates
    // before its job reads them (or what the mirror changed goes back).
    RatesStep(a);
    // Native weapons: the magazine mirror on this thread, before the gun
    // update reads it, and the viewer's shoot count around the job.
    MirrorMag(a, j);
    SlotFollow(a, j);
    // Aim parity: instance 1's network aim slot Q+0x10[1] gets the remote's
    // aim target before its gun update builds the aim point from it.
    partner_aim_point::Step(a.P, NowMs());
    const std::uint32_t shots_before = ReadShotCount();
    const std::uint32_t ammo_before = ReadAmmoDigest();
    // Phase 1 picks its pad-sampling branch by id == P+0x218: instance 1 (id 1,
    // validated: ids[1] == 1) takes it for this job only, on this thread.
    const bool detour = partner_local_detour::Installed();
    if (detour) partner_local_detour::Force(a.P, 1);
    // D3 (naim=looksrc): the remote's look follows the game's own look call
    // for instance 1's PS on this thread during this job only (withdrawn by
    // GuardedCall on every exit).
    if (a.naim == PartnerNativeAim::looksrc && j.look_valid && a.ps1 && partner_look_source::Installed())
        partner_look_source::Publish(a.ps1, 1, a.ps1_idx, j.look_pitch, j.look_yaw);
    // Always released (the detour and the published look) on every exit.
    const auto hud_probe = g_hud_before ? g_hud_before(a.P) : nametag_game::HudJobProbe{};
    if (!PoseBefore(a,save,j,consumer)) {
        PoseRejectDiagnostic(reject::Stage::before,g_pose_reason,&a,static_cast<std::uint32_t>(consumer));
        InterlockedIncrement(&g_pose_suppressed);g_pose_reason="pose_before_refused";return JobWindowOutcome::suppressed;
    }
    GuardedCall(a, save, Call{kJobEntry, go, const_cast<void*>(param), 0, 0, job}, true);
    if (!PoseAfter()) PoseFail("pose_after_refused");
    if (g_hud_after) g_hud_after(hud_probe);
    g_in_drain = false;
    if (save.rings_saved) g_ring_prev[static_cast<std::size_t>(consumer)] = j.held;
    ProbeRecord(a.rec1);
    LookProbe(a, j);
    AfterJob(a, shots_before, ammo_before);
    SafePost(a, save, true);
    EndWindow();
    std::uint32_t masks[3]{};
    std::memcpy(masks, g_priv_masks, sizeof(masks));
    InterlockedExchange(&g_d_mask28, static_cast<LONG>(masks[0]));
    InterlockedExchange(&g_d_mask2c, static_cast<LONG>(masks[1]));
    InterlockedExchange(&g_d_mask30, static_cast<LONG>(masks[2]));
    InterlockedIncrement(&g_n_drain_windows);
    Max(g_us_max, static_cast<LONG>(NowUs() - t0));
    return JobWindowOutcome::windowed;
    } __finally {
        if (PoseOwned()) {
            teammate_targeting::EndQuietJob();
            partner_local_detour::Release();
            partner_look_source::Withdraw();
            g_in_drain=false;
            SafePost(a,save,false);
            EndWindow();
        }
    }
}
bool RunJobWindow(void* go,const void* param,std::uint32_t block,JobCallFn job,pi::Consumer consumer) noexcept {
 return RunJobWindowOutcome(go,param,block,job,consumer)==JobWindowOutcome::windowed;
}

void PublishWrapRoots(std::uintptr_t root0, std::uintptr_t root1, bool census) noexcept {
    const LONG floor = census ? static_cast<LONG>(PartnerWrap::census) : static_cast<LONG>(PartnerWrap::off);
    InterlockedExchange(&g_wrap_floor, floor);
    // An enabled Arm owns the roots and the mode (PublishArm, same thread).
    if (g_arm_enabled) return;
    InterlockedExchange64(&g_wrap_root0, static_cast<LONG64>(root0));
    InterlockedExchange64(&g_wrap_root1, static_cast<LONG64>(root1));
    InterlockedExchange(&g_wrap_mode, floor);
}

const char* InstallWrap() noexcept {
    if (!g_ready) return "hook";
    if (InterlockedCompareExchange(&g_wrap_installed, 1, 0) != 0) return g_wrap_ready ? nullptr : "disabled";
    const DWORD error = GetLastError();
    const char* result = nullptr;
    const char* where = "";
    // The original targets are called by the wrappers; set before any patch.
    g_svc_active = reinterpret_cast<SvcFn>(g_base + c::svc_active_rva);
    g_svc_all = reinterpret_cast<SvcFn>(g_base + c::svc_all_rva);
    g_svc_id = reinterpret_cast<SvcIdFn>(g_base + c::svc_id_rva);
    g_walk7 = reinterpret_cast<WalkFn>(g_base + c::walk7_target);
    g_walk11 = reinterpret_cast<WalkIdFn>(g_base + c::walk11_target);
    const struct { std::size_t offset; std::uintptr_t target; const void* wrapper; const char* name; } slots[] = {
        {c::svc_vt_active, c::svc_active_rva, reinterpret_cast<const void*>(&WrapSvcActive), "svc28"},
        {c::svc_vt_all, c::svc_all_rva, reinterpret_cast<const void*>(&WrapSvcAll), "svc30"},
        {c::svc_vt_id, c::svc_id_rva, reinterpret_cast<const void*>(&WrapSvcId), "svc40"}};
    for (const auto& s : slots) {
        if (result) break;
        void* at = reinterpret_cast<void*>(g_base + c::svc_vtable_rva + s.offset);
        if (const char* why = near_code::PatchQword(at, g_base + s.target, reinterpret_cast<std::uintptr_t>(s.wrapper))) {
            result = why;
            where = s.name;
        }
    }
    if (!result) {
        if (const char* why = detail::RetargetCall(g_base + c::walk7_site, g_base + c::walk7_next_ip, c::walk7_rel32_orig,
                                           reinterpret_cast<const void*>(&WrapWalk7), g_walk7_stub)) { result = why; where = "walk7"; }
    }
    if (!result) {
        if (const char* why = detail::RetargetCall(g_base + c::walk11_site, g_base + c::walk11_next_ip, c::walk11_rel32_orig,
                                           reinterpret_cast<const void*>(&WrapWalk11), g_walk11_stub)) { result = why; where = "walk11"; }
    }
    if (!result) g_wrap_ready = true;
    if (g_log) {
        char line[192]{};
        if (result) StringCchPrintfA(line, ARRAYSIZE(line), "partner_input_wrap=refused; reason=%s; at=%s; installed_before=pass_through\r\n",
                                     result, where);
        else StringCchCopyA(line, ARRAYSIZE(line),
                            "partner_input_wrap=installed; entries=svc28,svc30,svc40,walk7@0x9c1da3,walk11@0x9c1aff\r\n");
        g_log(line);
    }
    SetLastError(error);
    return result;
}

bool WrapInstalled() noexcept { return g_wrap_ready; }

void VtableCopies(std::uintptr_t out[kVtableCopies]) noexcept {
    const bool ok = g_ready && g_vt_ready;
    out[0] = ok ? reinterpret_cast<std::uintptr_t>(&g_vt_none[1]) : 0;
    out[1] = ok ? reinterpret_cast<std::uintptr_t>(&g_vt_log[1]) : 0;
    out[2] = ok ? reinterpret_cast<std::uintptr_t>(&g_vt_own[1]) : 0;
    out[3] = ok ? reinterpret_cast<std::uintptr_t>(&g_vt_pass[1]) : 0;
}

std::uint32_t WindowSeq() noexcept { return static_cast<std::uint32_t>(g_window_seq); }

void ResetFault() noexcept {
    InterlockedExchange(&g_fault_phase, 0);
    InterlockedExchange(&g_fault_code, 0);
    InterlockedExchange(&g_faulted, 0);
    // The hold history and the frame injection never carry into a new
    // mission: the hook thread clears them before its next window.
    InterlockedExchange(&g_reset_hist, 1);
    InterlockedExchange(&g_n_look_faulted, 0);
    InterlockedExchange(&g_m_faulted, 0);
    InterlockedExchange(&g_m_leak, 0);
    InterlockedExchange(&g_sl_faulted, 0);
}

void Shutdown() noexcept { g_pose_gate.StopNewWriters(); InterlockedExchange(&g_shutdown, 1); }

void PublishArm(const pi::Arm& arm) noexcept {
    if (PoseRequired() && (arm.P != g_pose_player || arm.go != g_pose_go ||
        arm.root1 != g_pose_root || arm.blk1 != g_pose_block ||
        (arm.gen != g_pose_generation && g_pose_held) || arm.rates_epoch != g_pose_epoch || !arm.enabled || !arm.native))
        PoseFail("pose_arm_lifetime_changed");
    InterlockedIncrement(&g_seq);  // odd
    MemoryBarrier();
    std::memcpy(const_cast<pi::Arm*>(&g_arm), &arm, sizeof(arm));
    MemoryBarrier();
    InterlockedIncrement(&g_seq);  // even
    // A disarmed Arm keeps the last roots (PublishWrapRoots may count with
    // them); without an enabled Arm the wrappers only count (census) or forward.
    if (arm.enabled) {
        InterlockedExchange64(&g_wrap_root0, static_cast<LONG64>(arm.root0));
        InterlockedExchange64(&g_wrap_root1, static_cast<LONG64>(arm.root1));
    }
    InterlockedExchange(&g_wrap_native, arm.enabled && arm.native ? 1 : 0);
    InterlockedExchange(&g_wrap_mode, arm.enabled ? static_cast<LONG>(arm.wrap) : g_wrap_floor);
    InterlockedExchange(&g_arm_enabled, arm.enabled ? 1 : 0);
    InterlockedExchange(&g_holdlog, arm.holdlog ? 1 : 0);
}

void DrainStats(Stats& out) noexcept {
    out = Stats{};
    pi::WindowStats& w = out.window;
    w.windows = static_cast<std::uint32_t>(g_windows);
    w.root1_calls = static_cast<std::uint32_t>(g_root1);
    w.skips = static_cast<std::uint32_t>(g_skips);
    w.stale = static_cast<std::uint32_t>(g_stale);
    w.faults = static_cast<std::uint32_t>(g_faults);
    w.hold_calls = static_cast<std::uint32_t>(g_hold_calls);
    w.cam_restored = static_cast<std::uint32_t>(g_cam_restored);
    w.cam_dirty_or = static_cast<std::uint32_t>(g_cam_dirty);
    w.us_max = static_cast<std::uint32_t>(InterlockedExchange(&g_us_max, 0));
    w.hook_thread = static_cast<std::uint32_t>(g_hook_thread);
    w.gen_seen = static_cast<std::uint32_t>(g_gen_seen);
    w.last_skip = static_cast<pi::Skip>(g_last_skip);
    w.last_held = static_cast<std::uint32_t>(g_last_held);
    w.last_lx = g_last_lx; w.last_ly = g_last_ly; w.last_dir = g_last_dir; w.last_mag = g_last_mag;
    w.last_age_ms = static_cast<std::uint32_t>(g_last_age);
    w.last_stale = static_cast<std::uint8_t>(g_last_stale ? 1 : 0);
    w.fault_phase = static_cast<std::uint8_t>(g_fault_phase);
    w.fault_code = static_cast<std::uint32_t>(g_fault_code);
    for (unsigned i = 0; i < 4; ++i) w.hold_own[i] = static_cast<std::uint32_t>(g_hold_own[i]);
    w.km_calls = static_cast<std::uint32_t>(g_km_calls);
    w.synth_taps = static_cast<std::uint32_t>(g_synth);
    w.press_n = static_cast<std::uint32_t>(g_press_n);
    w.release_n = static_cast<std::uint32_t>(g_release_n);
    w.shadow_changes = static_cast<std::uint32_t>(g_shadow_changes);
    // The ring is written lock-free by the game thread; a record copied while
    // it is being written may be torn (diagnostic only).
    const LONG head = g_hold_head;
    const unsigned count = head > 8 ? 8 : static_cast<unsigned>(head);
    for (unsigned i = 0; i < count; ++i) out.holds[i] = g_hold_ring[(head - count + i) & 63];
    out.hold_count = count;
    WrapStats& ws = out.wrap;
    ws.installed = g_wrap_ready;
    for (std::size_t e = 0; e < kWrapEntries; ++e) {
        ws.calls0[e] = static_cast<std::uint32_t>(g_w_calls0[e]);
        ws.calls1[e] = static_cast<std::uint32_t>(g_w_calls1[e]);
        ws.same_thread[e] = static_cast<std::uint32_t>(g_w_same[e]);
        ws.nested[e] = static_cast<std::uint32_t>(g_w_nested[e]);
        ws.windows[e] = static_cast<std::uint32_t>(g_w_windows[e]);
        ws.phase_mask[e] = static_cast<std::uint32_t>(g_w_phases[e]);
    }
    ws.other_calls = static_cast<std::uint32_t>(g_w_other);
    ws.wrap_in_batch = static_cast<std::uint32_t>(g_w_in_batch);
    ws.svc_native_skips = static_cast<std::uint32_t>(g_w_svc_native);
    NativeStats& n = out.native;
    n.frames = static_cast<std::uint32_t>(g_n_frames);
    for (unsigned i = 0; i < 3; ++i) n.views[i] = static_cast<std::uint32_t>(g_n_views[i]);
    n.drain_windows = static_cast<std::uint32_t>(g_n_drain_windows);
    n.drain_refused = static_cast<std::uint32_t>(g_n_drain_refused);
    n.walk_windows = static_cast<std::uint32_t>(g_n_walk_windows);
    n.latch_other = static_cast<std::uint32_t>(g_n_latch_other);
    n.latch_last = static_cast<std::uint32_t>(g_n_latch_last);
    n.look_writes = static_cast<std::uint32_t>(g_n_look_writes);
    n.look_skipped_rec = static_cast<std::uint32_t>(g_n_look_skipped);
    n.look_refused = static_cast<std::uint32_t>(g_n_look_refused);
    n.look_last = static_cast<LookRefusal>(g_n_look_last);
    n.look_faulted = g_n_look_faulted != 0;
    n.camsub_dirty_or = static_cast<std::uint32_t>(g_n_camsub_dirty);
    n.lookcam_writes = static_cast<std::uint32_t>(g_n_lookcam);
    n.drain_block_refused = static_cast<std::uint32_t>(g_n_drain_block);
    n.drain_in_batch = static_cast<std::uint32_t>(g_n_drain_in_batch);
    partner_local_detour::Stats p1{};
    partner_local_detour::ReadStats(p1);
    n.p1_installed = p1.installed;
    n.p1_forced = p1.forced;
    n.p1_refused = p1.refused;
    n.p1_windows = p1.windows;
    n.drain_held = static_cast<std::uint32_t>(g_d_held);
    n.drain_mag = g_d_mag;
    n.drain_view = static_cast<std::uint32_t>(g_d_view);
    n.drain_local_pre = static_cast<std::uint32_t>(g_d_local_pre);
    n.drain_mask28 = static_cast<std::uint32_t>(g_d_mask28);
    n.drain_mask2c = static_cast<std::uint32_t>(g_d_mask2c);
    n.drain_mask30 = static_cast<std::uint32_t>(g_d_mask30);
    n.rec_ok = g_d_rec_ok != 0;
    n.rec_act = static_cast<std::uint32_t>(g_d_rec_act);
    n.rec_next = static_cast<std::uint32_t>(g_d_rec_next);
    n.rec_mag = g_d_rec_mag;
    n.rec_heading_mrad = g_d_rec_heading;
    n.mag_writes = static_cast<std::uint32_t>(g_m_writes);
    n.mag_refused = static_cast<std::uint32_t>(g_m_refused);
    n.mag_fire_edges = static_cast<std::uint32_t>(g_m_fire);
    n.mag_reload_edges = static_cast<std::uint32_t>(g_m_reload);
    n.mag_last = static_cast<MagRefusal>(g_m_last);
    n.mag_remote = static_cast<std::uint32_t>(g_m_remote);
    n.mag_remote_cap = static_cast<std::uint32_t>(g_m_remote_cap);
    n.mag_cap = static_cast<std::uint32_t>(g_m_cap);
    n.mag_written = static_cast<std::uint32_t>(g_m_written);
    n.mag_faulted = g_m_faulted != 0;
    n.mag_leak_off = g_m_leak != 0;
    n.shot_leaks = static_cast<std::uint32_t>(g_shot_leaks);
    n.shot_count = static_cast<std::uint32_t>(g_shot_count);
    n.reload_drains = static_cast<std::uint32_t>(g_reload_drains);
    n.ammo_leaks = static_cast<std::uint32_t>(g_ammo_leaks);
    n.ammo_digest = static_cast<std::uint32_t>(g_ammo_digest);
    n.rates_applied = g_n_rates_applied != 0;
    n.rates_latched = g_n_rates_latched != 0;
    n.rates_reason = static_cast<std::uint32_t>(g_n_rates_reason);
    n.rates_writes = static_cast<std::uint32_t>(g_n_rates_writes);
    n.rates_reapplied = static_cast<std::uint32_t>(g_n_rates_reapplied);
    n.rates_restores = static_cast<std::uint32_t>(g_n_rates_restores);
    n.rates_drops = static_cast<std::uint32_t>(g_n_rates_drops);
    for (unsigned i = 0; i < 3; ++i) n.rates_written[i] = static_cast<std::uint32_t>(g_n_rates_w[i]);
    n.store_writes = static_cast<std::uint32_t>(g_s_writes);
    n.store_refused = static_cast<std::uint32_t>(g_s_refused);
    n.store_last = static_cast<StoreRefusal>(g_s_last);
    n.mag_inj = static_cast<std::uint32_t>(g_m_inj);
    n.mag_inj_cap = static_cast<std::uint32_t>(g_m_inj_cap);
    n.ring_on = g_r_on != 0;
    n.ring_windows = static_cast<std::uint32_t>(g_r_windows);
    n.ring_refused = static_cast<std::uint32_t>(g_r_refused);
    n.ring_changed = static_cast<std::uint32_t>(g_r_changed);
    n.ring_last = static_cast<RingRefusal>(g_r_last);
    n.neutral_windows = static_cast<std::uint32_t>(g_n_neutral);
    n.strip_windows = static_cast<std::uint32_t>(g_n_strip);
    n.strip_bits = static_cast<std::uint32_t>(g_n_strip_bits);
    n.drain_disarmed = static_cast<std::uint32_t>(g_n_drain_disarmed);
    n.fire_requested = static_cast<std::uint32_t>(g_f_requested);
    n.fire_shot_drains = static_cast<std::uint32_t>(g_f_shot_drains);
    n.fire_modes = static_cast<std::uint32_t>(g_f_modes);
    n.mag_reload_seen = static_cast<std::uint32_t>(g_m_reload_seen);
    n.mag_frozen = static_cast<std::uint32_t>(g_m_frozen);
    n.mag_reconciles = static_cast<std::uint32_t>(g_m_reconciles);
    n.mag_m3e8_writes = static_cast<std::uint32_t>(g_m_3e8);
    n.p1_b20 = static_cast<std::uint32_t>(g_p1_b20);
    n.p1_b24 = static_cast<std::uint32_t>(g_p1_b24);
    n.p1_shots = static_cast<std::uint32_t>(g_p1_shots);
    n.p1_mode_changes = static_cast<std::uint32_t>(g_p1_modes);
    n.p1_mode = static_cast<std::uint32_t>(g_p1_mode);
    n.slot_writes = static_cast<std::uint32_t>(g_sl_writes);
    n.slot_refused = static_cast<std::uint32_t>(g_sl_refused);
    n.slot_target = static_cast<std::uint32_t>(g_sl_target);
    n.slot_last = static_cast<SlotRefusal>(g_sl_last);
    n.slot_faulted = g_sl_faulted != 0;
    n.look_ok = g_l_ok != 0;
    for (unsigned i = 0; i < 2; ++i) {
        n.look_remote_cdeg[i] = static_cast<std::int32_t>(g_l_remote[i]);
        n.look_slot_cdeg[i] = static_cast<std::int32_t>(g_l_slot[i]);
    }
    partner_look_source::Stats ls{};
    partner_look_source::ReadStats(ls);
    n.look_src_installed = ls.installed;
    n.look_src_faulted = ls.faulted;
    n.look_src_hits = ls.hits;
    n.look_src_skipped = ls.skipped;
    n.look_src_refused = ls.refused;
    n.look_src_calls = ls.calls;
}

void ForgetRates(std::uint32_t epoch) noexcept {
    InterlockedExchange(&g_rates_forget_epoch, static_cast<LONG>(epoch));
    InterlockedExchange(&g_rates_forget, 1);
}

bool PatchLive(const void* base) noexcept {
    const auto b = reinterpret_cast<std::uintptr_t>(base);
    if (!g_stub) return false;
    std::int32_t rel = 0;
    if (!near_code::Rel32(b + c::hook_next_ip, reinterpret_cast<std::uintptr_t>(g_stub), rel)) return false;
    const unsigned char* site = reinterpret_cast<const unsigned char*>(b + c::hook_site);
    std::int32_t live = 0;
    std::memcpy(&live, site + 1, 4);
    return site[0] == 0xE8 && live == rel;
}

} // namespace mgscoop::partner_input_hook
