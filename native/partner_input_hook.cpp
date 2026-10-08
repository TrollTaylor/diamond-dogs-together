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
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/partner_defer.hpp"
#include "mgscoop/partner_local_detour.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/partner_rates.hpp"
#include "partner_player_contract.hpp"

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
JobCallFn g_job_fn = nullptr;
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
bool g_m_seen = false;  // hook thread only: the last remote (mag, cap) acted on
std::uint16_t g_m_seen_mag = 0, g_m_seen_cap = 0;
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

bool ReadArm(pi::Arm& out) noexcept {
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

void BeginWindow() noexcept { InterlockedIncrement(&g_window_seq); }  // odd
void EndWindow() noexcept { InterlockedIncrement(&g_window_seq); }    // even

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
};
// What a window does beyond the round-3/4 swap.
struct WindowOpts {
    bool set_local;  // isLocal (rec+0x2AC bit 0)
    bool native;     // HOLD latch, CamSub (aim stage), lookcam
};
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
    if (a.p218) {
        save.p218 = Load<std::uint32_t>(a.P + c::p_local_id);
        save.p218_saved = true;
        Store<std::uint32_t>(a.P + c::p_local_id, 1u);
    }
}

// Exact reverse of ApplyPre for the saved fields. capture_private keeps the
// masks and shadows instance 1's update left as its private copy (only after
// a real call).
void ApplyPost(const pi::Arm& a, const Save& save, bool capture_private) noexcept {
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
};
inline constexpr std::uint8_t kMainEntry = 255;
inline constexpr std::uint8_t kJobEntry = 5;  // partner_defer's drain: g_job_fn(go = ctx, param = root)
void CallOriginal(const Call& k) noexcept {
    switch (k.entry) {
    case kJobEntry: g_job_fn(k.ctx, k.root); break;
    case 0: g_svc_active(k.ctx, k.root, k.a); break;
    case 1: g_svc_all(k.ctx, k.root, k.a); break;
    case 2: g_svc_id(k.ctx, k.root, k.a, k.b); break;
    case 3: g_walk7(k.root, k.a); break;
    case 4: g_walk11(k.root, k.a, k.b); break;
    default: g_original(k.root, k.a); break;
    }
}

// The original call with the window's fields swapped in. A fatal game
// exception is recorded and passed on; if an outer handler unwinds through
// here, the swapped fields are restored and the window closed first. A drain
// (`detour`) releases the phase-1 detour on every exit, unwinding included.
void GuardedCall(const pi::Arm& a, const Save& save, const Call& k, bool detour = false) noexcept {
    __try {
        __try { CallOriginal(k); }
        __finally {
            if (detour) partner_local_detour::Release();
            if (AbnormalTermination()) {
                g_in_drain = false;
                SafePost(a, save, false);
                EndWindow();
            }
        }
    } __except (GameFilter(GetExceptionCode())) {}
}

// Block 0 only (I5): the hold classifier of the local player's update is
// logged through the log vtable copy. Validated like the window (PP identity),
// honours g_faulted, never calls the original twice.
void RunHoldLog(const pi::Arm& a, void* root, std::uint32_t phase) noexcept {
    if (g_faulted || !g_vt_ready) { g_original(root, phase); return; }
    pi::Skip skip = pi::Skip::player;
    __try { skip = ValidatePad(a); }
    __except (OwnFilter(1, GetExceptionCode())) {}
    if (skip != pi::Skip::none) { g_original(root, phase); return; }
    Save save{};
    BeginWindow();
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
        g_original(root, phase);
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
        g_original(root, phase);
        return;
    }
    pi::Skip skip = pi::Skip::player;
    bool validated = false;
    __try { skip = Validate(a, root); validated = true; }
    __except (OwnFilter(1, GetExceptionCode())) {}
    if (!validated) { g_original(root, phase); return; }
    if (skip != pi::Skip::none) {
        InterlockedIncrement(&g_skips);
        InterlockedExchange(&g_last_skip, static_cast<LONG>(skip));
        g_original(root, phase);
        return;
    }
    InterlockedIncrement(&g_windows);
    if (a.config.mode == PartnerInput::pass) { g_inj_valid = false; g_original(root, phase); return; }
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
    Save save{};
    BeginWindow();
    if (!SafePre(a, j, save, WindowOpts{true, a.native})) {
        // The prefix was restored; block 1 still updates once this frame.
        EndWindow();
        g_original(root, phase);
        return;
    }
    SetCurrent(j, a.hold);
    GuardedCall(a, save, Call{kMainEntry, nullptr, root, phase, 0});
    SafePost(a, save, true);
    EndWindow();
    Max(g_us_max, static_cast<LONG>(NowUs() - t0));
}

void __stdcall InputDispatchHook(void* root, std::uint32_t phase) noexcept {
    if (InterlockedCompareExchange(&g_hook_thread, static_cast<LONG>(GetCurrentThreadId()), 0) == 0) {}
    if ((g_rates.applied && !g_wrap_native) || g_rates_forget) RatesIdle();
    if (!g_arm_enabled && !g_holdlog) { g_need_resync = true; g_original(root, phase); return; }
    pi::Arm a{};
    if (!ReadArm(a)) { g_original(root, phase); return; }
    InterlockedExchange(&g_gen_seen, static_cast<LONG>(a.gen));
    if (InterlockedExchange(&g_reset_hist, 0)) {
        g_hist = pi::HoldHistory{};
        g_inj_valid = false;
        g_m_seen = false;
    }
    if (a.gen != g_gen) {
        // Only instance 1's private masks and shadows start afresh with a new gen.
        g_gen = a.gen;
        std::memset(g_priv_masks, 0, sizeof(g_priv_masks));
        g_priv_valid = false;
        g_shadow_valid = false;
        g_inj_valid = false;
    }
    if (phase != 8) { g_original(root, phase); return; }
    if (a.holdlog && reinterpret_cast<std::uintptr_t>(root) == a.root0) { RunHoldLog(a, root, phase); return; }
    if (!a.enabled || g_faulted) { g_need_resync = true; g_original(root, phase); return; }
    if (reinterpret_cast<std::uintptr_t>(root) != a.root1) { g_original(root, phase); return; }
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
    Save save{};
    BeginWindow();
    if (!SafePre(a, j, save, WindowOpts{a.native ? a.local_all : true, a.native})) {
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
// The magazine mirror (drain window, before block 1's job, on the thread that
// runs instance 1's gun update): on a FIRE or RELOAD press edge of this
// consumer's view, or when the remote's (mag, cap) changed, instance 1's live
// magazine field becomes partner_native::MirrorMagazine (only when it
// differs). Its non-local shots never spend it (0x1041650 is local-only).
void MirrorMag(const pi::Arm& a, const pi::Injection& j) noexcept {
    if (g_m_faulted) { MagRefused(MagRefusal::fault, false); return; }
    if (!a.mag_mirror || !a.gun_prim || !a.gun_rec) { MagRefused(MagRefusal::off, false); return; }
    if (g_m_leak) { MagRefused(MagRefusal::leak, false); return; }
    if (!j.ammo_valid) { MagRefused(MagRefusal::no_v4, false); return; }
    const bool fire = (j.edge_press & partner_native::kPadFire) != 0;
    const bool reload = (j.edge_press & partner_native::kPadReload) != 0;
    const bool changed = !g_m_seen || j.ammo != g_m_seen_mag || j.ammo_cap != g_m_seen_cap;
    if (!fire && !reload && !changed) return;
    // A FIRE edge shows a round only if the remote had one (MirrorMagazine).
    const bool was_loaded = g_m_seen && g_m_seen_mag > 0;
    g_m_seen = true;
    g_m_seen_mag = j.ammo;
    g_m_seen_cap = j.ammo_cap;
    InterlockedExchange(&g_m_remote, j.ammo);
    InterlockedExchange(&g_m_remote_cap, j.ammo_cap);
    MagRefusal why = MagRefusal::none;
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
                const std::uintptr_t field = a.gun_rec + partner_native::GunMagazineOffset(g.flags27c);
                if (Load<std::uint16_t>(field) != target) {
                    Store<std::uint16_t>(field, static_cast<std::uint16_t>(target));
                    wrote = true;
                }
            }
        }
    } __except (MagFilter(GetExceptionCode())) {
        why = MagRefusal::fault;
    }
    InterlockedExchange(&g_m_cap, static_cast<LONG>(cap));
    if (why != MagRefusal::none) { MagRefused(why, true); return; }
    InterlockedExchange(&g_m_last, static_cast<LONG>(MagRefusal::none));
    InterlockedExchange(&g_m_written, static_cast<LONG>(target));
    if (!wrote) return;
    InterlockedIncrement(&g_m_writes);
    if (fire) InterlockedIncrement(&g_m_fire);
    if (reload) InterlockedIncrement(&g_m_reload);
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
    bool reload = false;
    __try {
        reload = Load<std::uintptr_t>(a.gun_prim + c::prim_gun_rec) == a.gun_rec &&
                 (Load<std::uint32_t>(a.gun_rec + c::gun_flags) & c::gun3c0_reload) != 0;
    } __except (ProbeFilter(GetExceptionCode())) {}
    if (reload) InterlockedIncrement(&g_reload_drains);
}

// A deferred job without a window: a fatal game code is recorded and passed on.
void GuardedJobCall(JobCallFn job, void* go, const void* param) noexcept {
    __try { job(go, param); }
    __except (GameFilter(GetExceptionCode())) {}
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
    if (!result) g_ready = true;
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

bool RunJobWindow(void* go, const void* param, std::uint32_t block, JobCallFn job, pi::Consumer consumer) noexcept {
    g_job_fn = job;
    pi::Arm a{};
    const bool armed = g_ready && !g_faulted && g_inj_valid && ReadArm(a) && a.enabled && a.native &&
                       a.config.mode == PartnerInput::net && a.gen == g_gen;
    if (!armed) {
        InterlockedIncrement(&g_n_drain_refused);
        GuardedJobCall(job, go, param);
        return false;
    }
    // A batch in flight anywhere (nested on this thread or on another thread):
    // block 0's job may be running on a worker, the shared pad must not be
    // swapped. The arbiter leaves on wrap_in_batch.
    if (partner_defer::InBatch()) {
        InterlockedIncrement(&g_n_drain_in_batch);
        InterlockedIncrement(&g_w_in_batch);
        InterlockedIncrement(&g_n_drain_refused);
        GuardedJobCall(job, go, param);
        return false;
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
        GuardedJobCall(job, go, param);
        return false;
    }
    if (!block_ok) {
        InterlockedIncrement(&g_n_drain_block);
        InterlockedIncrement(&g_n_drain_refused);
        GuardedJobCall(job, go, param);
        return false;
    }
    pi::ConsumerView view{};
    const pi::Injection j = pi::ForConsumer(g_last_inj, g_frame_id, g_consumer_last[static_cast<std::size_t>(consumer)],
                                            NowMs() - g_frame_ms, a.config, &view);
    InterlockedIncrement(&g_n_views[static_cast<std::size_t>(view)]);
    const std::uint64_t t0 = NowUs();
    Save save{};
    BeginWindow();
    // isLocal always: block 1's whole update runs here (phases 0/5/6 read the
    // pad only for a local record; local=all only adds the walk windows).
    if (!SafePre(a, j, save, WindowOpts{true, true})) {
        EndWindow();
        InterlockedIncrement(&g_n_drain_refused);
        GuardedJobCall(job, go, param);
        return false;
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
    const std::uint32_t shots_before = ReadShotCount();
    const std::uint32_t ammo_before = ReadAmmoDigest();
    // Phase 1 picks its pad-sampling branch by id == P+0x218: instance 1 (id 1,
    // validated: ids[1] == 1) takes it for this job only, on this thread.
    const bool detour = partner_local_detour::Installed();
    if (detour) partner_local_detour::Force(a.P, 1);
    GuardedCall(a, save, Call{kJobEntry, go, const_cast<void*>(param), 0, 0}, detour);
    g_in_drain = false;
    ProbeRecord(a.rec1);
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
    return true;
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
}

void PublishArm(const pi::Arm& arm) noexcept {
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
