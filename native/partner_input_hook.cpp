// Original MGS Co-op round 3 input lever: the block-1 context window hook.
// It wraps the single per-block phase-8 dispatch call at 0x9C2B51 (into
// 0xBF61D0). Around block 1's call only, with instance 1 realized and every
// identity re-validated, it swaps in instance 1's own virtual pad and sets the
// base plugin's per-id isLocal bit, then restores everything. Never blocks.
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
#include "partner_player_contract.hpp"

namespace mgscoop::partner_input_hook {
namespace {
namespace c = mgscoop::partner_player_contract;
namespace pi = mgscoop::partner_input;

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
bool g_vt_ready = false;

// Seqlock-published Arm.
volatile LONG g_seq = 0;
pi::Arm g_arm{};
volatile LONG g_arm_enabled = 0;
volatile LONG g_holdlog = 0;
// Odd while the hook has anything swapped into PP/src/rec/CAM/P (the Lua
// thread's pad reads retry or refuse while it is odd or changes).
volatile LONG g_window_seq = 0;

// Hook-thread-only edge/tap/sample state (survives gen bumps; see the Arm
// comment in partner_input.hpp) and instance 1's private masks (reset per gen).
pi::EdgeState g_edge{};
pi::TapState g_tap{};
pi::Sample g_prev{};
std::uint32_t g_gen = 0;
bool g_priv_valid = false;
unsigned char g_priv_masks[c::pp_masks_size + c::pp_mask_tables_size]{};

// Statistics (atomics).
volatile LONG g_faulted = 0;
volatile LONG g_windows = 0, g_root1 = 0, g_skips = 0, g_stale = 0, g_faults = 0, g_hold_calls = 0, g_cam_restored = 0;
volatile LONG g_cam_dirty = 0, g_us_max = 0, g_hook_thread = 0, g_gen_seen = 0;
volatile LONG g_last_skip = 0, g_last_held = 0, g_last_lx = 0, g_last_ly = 0, g_last_dir = 0, g_last_mag = 0;
volatile LONG g_last_age = static_cast<LONG>(pi::kNoSample), g_last_stale = 0;
volatile LONG g_fault_phase = 0, g_fault_code = 0;
// Hold log ring (block 0 only).
pi::HoldRecord g_hold_ring[64]{};
volatile LONG g_hold_head = 0;

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
    unsigned char cam[pi::kCamSaveBytes];
    bool src_saved, derived_saved, masks_saved, kb_saved, kb_flags_saved, vt_saved, rec_saved, p218_saved, cam_saved;
};

void ApplyPre(const pi::Arm& a, const pi::Injection& j, Save& save) noexcept {
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
    Store<float>(a.pp + 0x10, j.mag); Store<float>(a.pp + 0x14, j.force); Store<float>(a.pp + 0x18, j.dir);
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
    if (a.hold == PartnerInputHold::none && g_vt_ready) {
        save.vt = Load<std::uintptr_t>(a.pp);
        save.vt_saved = true;
        Store<std::uintptr_t>(a.pp, reinterpret_cast<std::uintptr_t>(&g_vt_none[1]));
    }
    save.rec_2ac = Load<std::uint32_t>(a.rec1 + c::rec_local);
    save.rec_saved = true;
    Store<std::uint32_t>(a.rec1 + c::rec_local, save.rec_2ac | 1u);
    if (a.cam != PartnerInputCam::off) {
        std::size_t at = 0;
        for (const pi::CamRange& r : pi::kCamRanges) {
            const std::size_t n = static_cast<std::size_t>(r.end - r.begin);
            std::memcpy(save.cam + at, reinterpret_cast<void*>(a.cam_object + r.begin), n);
            at += n;
        }
        save.cam_saved = true;
    }
    if (a.p218) {
        save.p218 = Load<std::uint32_t>(a.P + c::p_local_id);
        save.p218_saved = true;
        Store<std::uint32_t>(a.P + c::p_local_id, 1u);
    }
}

// Exact reverse of ApplyPre for the saved fields. capture_private keeps the
// masks instance 1's update left as its private copy (only after a real call).
void ApplyPost(const pi::Arm& a, const Save& save, bool capture_private) noexcept {
    if (save.p218_saved) Store<std::uint32_t>(a.P + c::p_local_id, save.p218);
    if (save.vt_saved) Store<std::uintptr_t>(a.pp, save.vt);
    if (save.rec_saved) {
        const std::uint32_t now = Load<std::uint32_t>(a.rec1 + c::rec_local);
        Store<std::uint32_t>(a.rec1 + c::rec_local, (now & ~1u) | (save.rec_2ac & 1u));
    }
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
            const bool changed = std::memcmp(save.cam + at, reinterpret_cast<void*>(a.cam_object + r.begin), n) != 0;
            if (changed) {
                dirty |= (1u << i);
                if (a.cam == PartnerInputCam::sandbox) {
                    std::memcpy(reinterpret_cast<void*>(a.cam_object + r.begin), save.cam + at, n);
                    ++restored;
                }
            }
            at += n;
        }
        if (dirty) InterlockedOr(&g_cam_dirty, static_cast<LONG>(dirty));
        if (restored) InterlockedAdd(&g_cam_restored, static_cast<LONG>(restored));
    }
}

// POST under SEH (phase 3 on an access fault).
void SafePost(const pi::Arm& a, const Save& save, bool capture_private) noexcept {
    __try { ApplyPost(a, save, capture_private); }
    __except (OwnFilter(3, GetExceptionCode())) {}
}

// PRE under SEH; on an access fault the saved prefix is restored and false
// returned (the window is then disabled by Fault).
bool SafePre(const pi::Arm& a, const pi::Injection& j, Save& save) noexcept {
    bool done = false;
    __try { ApplyPre(a, j, save); done = true; }
    __except (OwnFilter(1, GetExceptionCode())) {}
    if (!done) SafePost(a, save, false);
    return done;
}

// The original dispatch with the window's fields swapped in. A fatal game
// exception is recorded and passed on; if an outer handler unwinds through
// here, the swapped fields are restored and the window closed first.
void GuardedCall(const pi::Arm& a, const Save& save, void* root, std::uint32_t phase) noexcept {
    __try {
        __try { g_original(root, phase); }
        __finally {
            if (AbnormalTermination()) {
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
    GuardedCall(a, save, root, phase);
    SafePost(a, save, false);
    EndWindow();
}

void RunWindow(const pi::Arm& a, void* root, std::uint32_t phase) noexcept {
    InterlockedIncrement(&g_root1);
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
    if (a.config.mode == PartnerInput::pass) { g_original(root, phase); return; }
    const std::uint64_t now_ms = NowMs();
    pi::Sample s{};
    if (g_sample && g_sample(s)) g_prev = s; else s = g_prev;
    const std::uint32_t overlay_bits = pi::StepOverlay(g_tap, a.tap_seq, a.tap_bits, a.press_bits, a.press_until_ms, now_ms);
    const pi::Injection j = pi::Compose(a.config, s, a.host_cam_cdeg, pi::Overlay{overlay_bits}, g_edge);
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
    if (!SafePre(a, j, save)) {
        // The prefix was restored; block 1 still updates once this frame.
        EndWindow();
        g_original(root, phase);
        return;
    }
    GuardedCall(a, save, root, phase);
    SafePost(a, save, true);
    EndWindow();
    Max(g_us_max, static_cast<LONG>(NowUs() - t0));
}

void __stdcall InputDispatchHook(void* root, std::uint32_t phase) noexcept {
    if (InterlockedCompareExchange(&g_hook_thread, static_cast<LONG>(GetCurrentThreadId()), 0) == 0) {}
    if (!g_arm_enabled && !g_holdlog) { g_original(root, phase); return; }
    pi::Arm a{};
    if (!ReadArm(a)) { g_original(root, phase); return; }
    InterlockedExchange(&g_gen_seen, static_cast<LONG>(a.gen));
    if (a.gen != g_gen) {
        // Only instance 1's private masks start afresh with a new gen.
        g_gen = a.gen;
        std::memset(g_priv_masks, 0, sizeof(g_priv_masks));
        g_priv_valid = false;
    }
    if (phase != 8) { g_original(root, phase); return; }
    if (a.holdlog && reinterpret_cast<std::uintptr_t>(root) == a.root0) { RunHoldLog(a, root, phase); return; }
    if (!a.enabled || reinterpret_cast<std::uintptr_t>(root) != a.root1 || g_faulted) { g_original(root, phase); return; }
    RunWindow(a, root, phase);
}

std::uint32_t __stdcall HoldNone(void*, std::uint32_t, std::uint32_t) noexcept { return 0; }
std::uint32_t __stdcall HoldLog(void* pp, std::uint32_t btn, std::uint32_t frames) noexcept {
    const std::uint32_t result = g_hold_orig ? g_hold_orig(pp, btn, frames) : 0;
    InterlockedIncrement(&g_hold_calls);
    const LONG at = InterlockedIncrement(&g_hold_head) - 1;
    g_hold_ring[at & 63] = pi::HoldRecord{btn, frames, result};
    return result;
}

bool CopyVtable(std::uintptr_t table[43], std::uintptr_t new_slot90) noexcept {
    const std::uintptr_t vt = g_base + c::pp_vtable_rva;
    // Verify the live slots we rely on equal the image targets before copying.
    if (Load<std::uintptr_t>(vt + c::pp_vt_hold) != g_base + c::pp_hold_rva) return false;
    std::memcpy(table, reinterpret_cast<const void*>(vt - 8), sizeof(std::uintptr_t) * 43);
    table[1 + c::pp_vt_hold / 8] = new_slot90;
    return true;
}
bool BuildCopies() noexcept {
    g_vt_ready = false;
    if (!CopyVtable(g_vt_none, reinterpret_cast<std::uintptr_t>(&HoldNone)) ||
        !CopyVtable(g_vt_log, reinterpret_cast<std::uintptr_t>(&HoldLog))) return false;
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
                              "partner_input_hook=installed; site=0x9c2b51; stub=0x%llx; rel32=0x%08x; target=0xbf61d0\r\n",
                              static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(g_stub) - g_base), new_rel);
        g_log(line);
    }
    SetLastError(error);
    return result;
}

bool Installed() noexcept { return g_ready; }

void VtableCopies(std::uintptr_t out[2]) noexcept {
    out[0] = g_ready && g_vt_ready ? reinterpret_cast<std::uintptr_t>(&g_vt_none[1]) : 0;
    out[1] = g_ready && g_vt_ready ? reinterpret_cast<std::uintptr_t>(&g_vt_log[1]) : 0;
}

std::uint32_t WindowSeq() noexcept { return static_cast<std::uint32_t>(g_window_seq); }

void ResetFault() noexcept {
    InterlockedExchange(&g_fault_phase, 0);
    InterlockedExchange(&g_fault_code, 0);
    InterlockedExchange(&g_faulted, 0);
}

void PublishArm(const pi::Arm& arm) noexcept {
    InterlockedIncrement(&g_seq);  // odd
    MemoryBarrier();
    std::memcpy(const_cast<pi::Arm*>(&g_arm), &arm, sizeof(arm));
    MemoryBarrier();
    InterlockedIncrement(&g_seq);  // even
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
    // The ring is written lock-free by the game thread; a record copied while
    // it is being written may be torn (diagnostic only).
    const LONG head = g_hold_head;
    const unsigned count = head > 8 ? 8 : static_cast<unsigned>(head);
    for (unsigned i = 0; i < count; ++i) out.holds[i] = g_hold_ring[(head - count + i) & 63];
    out.hold_count = count;
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
