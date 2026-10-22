#pragma once
// F3 (0.13.5, docs/native-puppet.md "Traversal icon"): the traversal start of
// body 1 (instance 1) through the stock action icon. Lab key
// partner_native_traverse, default off in lab and play (play forces it off).
//
// The icon controller "padop" (Player2PadOperatorControllerImpl, vtable
// 0x22E8F00, 96 slots, one object per P at P+0xD0) is shared by both bodies.
// Its builder (vt+0x18) runs only for the local id (0x141155B87) and fills the
// key-type slots with block 0's plugin pointers; the action plugins are per
// block, and body 1's polls (ladder 0x1411EB00D, pipe 0x141213DBC, climb
// 0x14114DEC8, step-on 0x14129BF86) pass block 1's own plugin objects to
// IsCompletedActionIcon (vt+0x28, 0x140AFD870), whose compare
// [padop+0x980+kt*0xE0] == plugin therefore never holds for body 1.
//
// The module keeps a COPY of the padop vtable whose +0x28 is
// IconCompletedBody1 and whose +0x50 (0x140B02480, the step-on poll's
// force-complete) is IconForceBody1. partner_input_hook swaps the copy into
// P+0xD0's padop only inside instance 1's drain windows that carry a pending
// request (Arm.icon_kind/icon_seq/icon_until_ms) and restores the image
// vtable in ApplyPost; no code byte and no image vtable slot is written (the
// carry contract pins 0xAFD870 and the slot {0x22E8F00, +0x28}). Begin/End
// scope the answer to (thread, P, blk1) around block 1's job only; every call
// through the copy outside that scope goes to the original.
//
// In scope, a query is body 1's when *(plugin+8) == blk1 (ActionPlugin+8 is
// the block). Body 1's query of the requested kind (its vt[1] is that kind's
// Exec), key type 0, not yet granted for this request: priority vt[0](plugin,
// 1, 0), then Exec(plugin, 1, 0, res) (fills body 1's own work and geo path;
// the stock poll then wakes the plugin, which writes act 21..24), with the
// shared ray-cache entry padop+0x7F0..+0x840 saved and restored around it and
// the viewer's icon slots, icon input states and act words digested before and
// after (a change latches the mission-scoped `uncertain` flag: no more grants
// until MissionStart). Every other body-1 query returns false (what stock
// returns for body 1); queries of other P/blocks/threads go to the original.
// Body 1's force-complete in scope is swallowed (it would set the viewer's
// input-state bit 7 at padop+0x1260; the poll discards its result).
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_icon {

// The requested plugin (partner_traverse::Icon has the same values).
enum class Kind : std::uint8_t { none = 0, ladder = 1, pipe = 2, climb = 3, step_on = 4 };
inline constexpr unsigned kKinds = 5;
const char* KindName(Kind kind) noexcept;

// --- 1.0.15.4 RVAs and offsets (tools/generate_partner_contract.py pins the
// slots, the call sites and the bodies; partner_player_contract.hpp icon_*).
inline constexpr std::uintptr_t kPadopVtableRva = 0x22E8F00;
inline constexpr std::size_t kPadopSlots = 96;                 // +0x000..+0x2F8; +0x300 is data
inline constexpr std::size_t kSlotCompleted = 0x28, kSlotForce = 0x50;
inline constexpr std::uintptr_t kCompletedRva = 0xAFD870;      // IsCompletedActionIcon(padop, plugin, kt, res) -> al
inline constexpr std::uintptr_t kForceRva = 0xB02480;          // (padop, kt, flag, res): input bit 7, copies the slot
inline constexpr std::uintptr_t kPriorityRva = 0x12128C0;      // vt[0] of all four: kt == 0 ? 6 : 0
// ActionPlugin vt[1] ExecActionButtonFunction(plugin, idx, kt, res) -> al, by kind.
inline constexpr std::uintptr_t kExecRva[kKinds] = {0, 0x11EA140, 0x1211820, 0x114BEC0, 0x1293F60};
inline constexpr std::size_t kPPadop = 0xD0;      // P+0xD0 = padop (GO+0xF0)
inline constexpr std::size_t kPadopOwner = 0x10;  // padop+0x10 == P
inline constexpr std::size_t kPFlags = 0x204;     // P flags; bit 10 gates stock completion (0x140AFD90C)
inline constexpr std::uint32_t kPFlagIcons = 1u << 10;
inline constexpr std::size_t kRayCache = 0x7F0, kRayCacheSize = 0x50;  // padop ray cache entry 0x19 (vt+0x188)
inline constexpr std::size_t kSlots = 0x980, kSlotStride = 0xE0, kInputs = 0x1240, kInputStride = 0x28;
inline constexpr unsigned kKeyTypes = 6;
inline constexpr std::size_t kPluginBlock = 8;    // ActionPlugin+8 = its block
inline constexpr std::size_t kBlockPs = 0x78, kPsPlugins = 0x10;  // block -> PS; PS+0x10 = ActionPlugin*[0x45]
inline constexpr unsigned kPlugins = 0x45;
inline constexpr std::uint64_t kHoldMs = 250;     // Arm.icon_until_ms = now + this (a stale Arm drops the request)

// --- Pure decisions (tests/partner_icon_tests.cpp) ---------------------------
// Whether a call through the copy is body 1's (scope active on this thread
// and the plugin's block is the Arm's blk1) or goes to the original.
enum class Route : std::uint8_t { original, body1 };
constexpr Route RouteQuery(bool active, bool same_thread, std::uintptr_t plugin_block, std::uintptr_t blk1) noexcept {
    return active && same_thread && blk1 != 0 && plugin_block == blk1 ? Route::body1 : Route::original;
}
// Why a body-1 query is answered false (none: run priority + Exec). Checked
// in this order: key type (only kt 0), a request at all, the mission latch,
// already granted for this seq, the plugin is the requested kind's (vt[1]
// equals its Exec), padop+0x10 is the scoped P, the stock P+0x204 bit 10, res
// 16-byte aligned (Exec and operator= use movaps).
enum class Deny : std::uint8_t { none, kt, no_request, latched, granted, kind, owner, p_flag, align, priority, exec, fault, count };
const char* DenyName(Deny deny) noexcept;
struct GrantIn {
    std::int32_t kt;
    Kind kind;
    std::uint32_t seq, granted_seq;
    bool latched;     // uncertain or faulted this mission
    bool exec_match;
    bool owner_ok;
    bool p_flag;
    bool res_aligned;
};
constexpr Deny GrantCheck(const GrantIn& g) noexcept {
    if (g.kt != 0) return Deny::kt;
    if (g.kind == Kind::none || static_cast<unsigned>(g.kind) >= kKinds || g.seq == 0) return Deny::no_request;
    if (g.latched) return Deny::latched;
    if (g.granted_seq == g.seq) return Deny::granted;
    if (!g.exec_match) return Deny::kind;
    if (!g.owner_ok) return Deny::owner;
    if (!g.p_flag) return Deny::p_flag;
    if (!g.res_aligned) return Deny::align;
    return Deny::none;
}
// The force-complete (+0x50) is swallowed exactly in scope (body 1's job on
// the drain thread); everything else goes to the original.
constexpr bool ForceSwallowed(bool active, bool same_thread) noexcept { return active && same_thread; }
// A drain window installs the copy only for a native Arm with a pending,
// unexpired request of a known kind and a request id.
constexpr bool SwapWanted(bool native, std::uint8_t kind, std::uint32_t seq, std::uint64_t now_ms, std::uint64_t until_ms) noexcept {
    return native && kind != 0 && kind < kKinds && seq != 0 && until_ms != 0 && now_ms < until_ms;
}

// Read-only census (once per mission with the key on): block 1's PS+0x10
// plugin list against block 0's, and *(plugin+8) == its block for the four
// traversal plugins (found by their vt[1]). Bits are 1 << Kind.
struct Census {
    bool read;              // every read succeeded
    unsigned plugins;       // list entries non-null in both lists
    unsigned distinct;      // ... that differ between the lists
    unsigned null0, null1;  // null entries per list
    std::uint32_t found0, found1;    // traversal kinds found in block 0's / block 1's list
    std::uint32_t blk_ok0, blk_ok1;  // ... whose *(plugin+8) is that block
};
inline constexpr std::uint32_t kAllKinds = 0x1Eu;  // ladder | pipe | climb | step_on
// "ok" when the lists are fully distinct and both carry all four kinds with
// their own block; otherwise the first reason (unread, empty, shared,
// missing0, missing1, blk0, blk1).
const char* CensusVerdict(const Census& census) noexcept;
// One step of the census over one list entry pair (pure; the reader feeds it).
// vt1_exec0/1 are the entries' vt[1] as RVAs (0 when null), block0/1 their +8.
void CensusAdd(Census& census, std::uintptr_t p0, std::uintptr_t p1, std::uintptr_t exec0_rva, std::uintptr_t exec1_rva,
               std::uintptr_t block_of_p0, std::uintptr_t block_of_p1, std::uintptr_t blk0, std::uintptr_t blk1) noexcept;
// The kind whose Exec RVA this is (none otherwise).
constexpr Kind KindOfExec(std::uintptr_t exec_rva) noexcept {
    for (unsigned k = 1; k < kKinds; ++k)
        if (exec_rva == kExecRva[k]) return static_cast<Kind>(k);
    return Kind::none;
}

// --- Runtime (native/partner_icon.cpp; game memory, SEH everywhere) ----------
using LogFn = void (*)(const char* line) noexcept;
// Builds the vtable copy once per process (after the code gate, lab key on):
// the live image slots +0x28/+0x50 must be the originals and the complete
// bodies of 0xAFD870, 0xB02480 and 0x12128C0 must match their SHA-256 pins.
// nullptr (also when already built) or a static reason, logged once:
// slots | pins | fault | disabled (an earlier build failed).
const char* Build(const void* base, LogFn log) noexcept;
bool Ready() noexcept;
// The vptr value of the copy (&table[1]), 0 before Build succeeded.
std::uintptr_t CopyTable() noexcept;
// The swap bookkeeping (partner_input_hook ApplyPre): a window that installed
// the copy, or one that wanted to but found the padop identity wrong.
void NoteSwap(bool installed) noexcept;
// Drain scope: Begin right before block 1's job (only when the window
// installed the copy), End on every exit (idempotent).
void Begin(std::uintptr_t P, std::uintptr_t blk1, std::uint8_t kind, std::uint32_t seq) noexcept;
void End() noexcept;
// MissionStart: clears the uncertain and fault latches (granted ids stay:
// the driver's request ids are process-lifetime).
void ResetMission() noexcept;
// Process-lifetime totals (the driver logs deltas from its tick).
struct Stats {
    bool ready;
    std::uint32_t swaps, swap_refused, windows;   // copy installed, identity refused, Begin calls
    std::uint32_t queries;                         // body-1 queries through the copy in scope
    std::uint32_t passed;                          // queries of other blocks / threads / outside scope (original)
    std::uint32_t denied;                          // body-1 queries answered false without Exec
    std::uint32_t exec_ok, exec_fail, granted;     // Exec true / false; grants (== exec_ok)
    std::uint32_t force_swallowed, force_passed;
    std::uint32_t viewer_changed, faults;          // digest surprises and SEH faults (both latch)
    std::uint8_t last_kind, last_deny;             // Kind of the last Exec, Deny of the last denied query
    std::uint32_t granted_seq;                     // the last granted request id
    bool latched;                                  // uncertain or faulted this mission
};
void ReadStats(Stats& out) noexcept;
// Read-only census of block 0's and block 1's plugin lists (SEH; read=false on a fault).
void ReadCensus(const void* base, std::uintptr_t blk0, std::uintptr_t blk1, Census& out) noexcept;

namespace detail {
using IsCompletedFn = bool (*)(void* padop, void* plugin, std::int32_t kt, void* res);
using ForceFn = void (*)(void* padop, std::int32_t kt, bool flag, void* res);
// Test only: the originals and the Exec addresses (absolute) a fixture binds
// instead of the image's, and a copy built from `base` without the body pins.
void BindForTest(const void* base, IsCompletedFn completed, ForceFn force, const std::uintptr_t (&exec)[kKinds]) noexcept;
void ResetForTest() noexcept;
// The copy's slots (absolute function addresses) for the fixture.
std::uintptr_t CompletedSlot() noexcept;
std::uintptr_t ForceSlot() noexcept;
}

} // namespace mgscoop::partner_icon
