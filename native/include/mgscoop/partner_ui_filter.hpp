#pragma once
// G1 (0.13.6, docs/native-puppet.md "Partner HUD hints"): the partner body's
// HUD hint requests never reach the viewer's HUD. Lab key
// partner_native_ui_filter, default 0 in lab and play (play forces it off)
// until a hidden-worker FRAME shows the viewer's HUD clean while body 1 climbs.
//
// The defect (0.13.5 runs 8f27806d / 2596f6ae): while body 1 is on a ladder
// the VIEWER's HUD shows the stock "C + ladder" slide hint beside its own
// player. The player's UI controller P+0xD8 (UiControllerImpl, the secondary
// interface: vtable 0x23AF700, primary 0x23AF6D0 at ui-0x20, ui+0x28 == P) is
// one object for every body. Its hint queues are ten identical 0x70-byte
// leaves (vt+0x240..+0x288), each a request record ui+0x1E0+k*0x50 (id +0,
// flags +4, active +0x30, priority +0x33; a request only replaces a record
// whose priority is numerically higher) that the HUD shows next to the LOCAL
// player. Nothing in a leaf checks the body. The traversal plugins post into
// vt+0x260 (record 0x320) from their per-frame state update (event 4), which
// phase 1 (ladder 0x1411EA220) selects for every idx while P+0x204 bit 8
// (S_IS_ONLINE) is clear, i.e. always in this mod: ladder states 5..11 (hint
// 3, priority 8; call sites 0x1411EB5A0 / EBFEE / EC606 / EDB11), climb (0x61,
// 0x14114FC48), pipe (0x60, 0x141216893) and the ledge hang (2, 0x1411A20BD /
// A2BC5 / A791C / A85D2 / A9205). Their only guard, IsNeedToSync, is
// S_IS_ONLINE-gated (false offline). Body 1's update runs inside instance 1's
// drain job (partner_input_hook RunJobWindow), so these requests are made on
// the hook thread with the window open.
//
// The queues vt+0x268/0x270/0x278/0x288 (+0x280, the Quiet hint) already have
// a body-1 filter (teammate_targeting / the private pose window, MinHooks on
// their leaves gated by the HUD probe's job scope). This module covers the
// other five: a MinHook on each leaf 0xFE84F0 (+0x240), 0xFE8970 (+0x248),
// 0xFE8560 (+0x250), 0xFE87E0 (+0x258), 0xFE8480 (+0x260), installed lazily
// (after the code gate, key on, defer on) once the complete bodies match their
// SHA-256 pins and the five vtable slots still point at them. A request is
// swallowed only inside the scope Begin/End opens around block 1's drain job:
// the hook thread, the job's own P, and the request's receiver equal to the
// UiControllerImpl Begin validated at P+0xD8. Everything else (the viewer's
// own update, other threads, another receiver, outside the job) goes to the
// original unchanged, so the viewer's own HUD hints are never touched.
//
// Why not a vtable copy (the padop pattern of partner_icon): swapping the UI
// object's vptr for the window breaks every identity check that reads it
// during body 1's job (teammate_targeting's partner prompt and Quiet hint
// filters, the pose window's sleep-hint owner check, nametag's HUD probe, the
// sleep / vehicle readers), and other threads use the same UI object
// concurrently. The leaf hooks leave the object untouched.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_ui_filter {

// --- 1.0.15.4 RVAs and offsets (tools/generate_partner_contract.py pins the
// slots, the leaf prologues and complete bodies, the identity sites and the
// census call sites; partner_player_contract.hpp ui_filter_*).
inline constexpr std::uintptr_t kUiVtableRva = 0x23AF700;         // UiControllerImpl (P+0xD8)
inline constexpr std::uintptr_t kUiPrimaryVtableRva = 0x23AF6D0;  // its primary at ui-0x20
inline constexpr std::size_t kPUi = 0xD8, kUiOwner = 0x28, kUiPrimary = 0x20;
inline constexpr unsigned kQueues = 5;
struct Queue {
    std::size_t slot;          // UiControllerImpl vt offset
    std::uintptr_t leaf_rva;   // the queue leaf (0x70 bytes)
    std::size_t record;        // ui + record = the request record (0x50 bytes)
};
inline constexpr Queue kQueue[kQueues] = {
    {0x240, 0xFE84F0, 0x1E0}, {0x248, 0xFE8970, 0x230}, {0x250, 0xFE8560, 0x280}, {0x258, 0xFE87E0, 0x2D0}, {0x260, 0xFE8480, 0x320},
};
inline constexpr std::size_t kLeafSize = 0x70;
inline constexpr std::size_t kRecordsBegin = 0x1E0, kRecordsEnd = 0x370;  // the five records, digested around the job
inline constexpr unsigned kKeys = 16;  // distinct swallowed (queue, id, priority, caller) keys kept per process

// --- Pure decisions (tests/partner_ui_filter_tests.cpp) ----------------------
// One hint request: swallowed only in scope (Begin armed it on this thread) for
// the scoped UiControllerImpl; everything else goes to the original leaf.
enum class Route : std::uint8_t { original, swallow };
constexpr Route RouteHint(bool active, bool same_thread, bool shutdown, std::uintptr_t ui, std::uintptr_t scoped_ui) noexcept {
    return active && same_thread && !shutdown && scoped_ui != 0 && ui == scoped_ui ? Route::swallow : Route::original;
}
// Begin's verdict on the window's P: the scope opens only for the stock
// UiControllerImpl owned by that P.
enum class Refusal : std::uint8_t { none, not_installed, shutdown, latched, player, ui, align, vtable, primary, owner, fault, count };
const char* RefusalName(Refusal why) noexcept;
struct Identity {
    std::uintptr_t P;         // the window's player
    std::uintptr_t ui;        // *(P+0xD8)
    std::uintptr_t ui_vt;     // *(ui)
    std::uintptr_t primary_vt;// *(ui-0x20)
    std::uintptr_t owner;     // *(ui+0x28)
};
constexpr Refusal CheckIdentity(const Identity& id, std::uintptr_t image) noexcept {
    if (!id.P) return Refusal::player;
    if (id.ui < kUiPrimary) return Refusal::ui;
    if ((id.ui & 7u) != 0) return Refusal::align;
    if (id.ui_vt != image + kUiVtableRva) return Refusal::vtable;
    if (id.primary_vt != image + kUiPrimaryVtableRva) return Refusal::primary;
    if (id.owner != id.P) return Refusal::owner;
    return Refusal::none;
}
// A swallowed request's census key: queue index (4 bits), hint id (16),
// priority (8, saturated) and the caller's return-address RVA (32).
constexpr std::uint64_t Key(unsigned queue, std::uint16_t id, std::uint32_t priority, std::uint32_t caller_rva) noexcept {
    return (static_cast<std::uint64_t>(queue & 0xFu) << 60) | (static_cast<std::uint64_t>(id) << 40) |
           (static_cast<std::uint64_t>(priority > 0xFFu ? 0xFFu : priority) << 32) | caller_rva;
}
constexpr unsigned KeyQueue(std::uint64_t key) noexcept { return static_cast<unsigned>(key >> 60) & 0xFu; }
constexpr std::uint16_t KeyId(std::uint64_t key) noexcept { return static_cast<std::uint16_t>(key >> 40); }
constexpr std::uint32_t KeyPriority(std::uint64_t key) noexcept { return static_cast<std::uint32_t>(key >> 32) & 0xFFu; }
constexpr std::uint32_t KeyCaller(std::uint64_t key) noexcept { return static_cast<std::uint32_t>(key); }
// The census's name for a caller (the return address of a known call site;
// "other" for any caller outside the static census).
const char* CallerName(std::uint32_t caller_rva) noexcept;
// The driver's log decision: a swallowed line when any total moved and at
// least kLogGapMs passed since the previous line (the first one at once).
inline constexpr std::uint64_t kLogGapMs = 1000;
constexpr bool LogDue(bool moved, std::uint64_t now_ms, std::uint64_t last_ms) noexcept {
    return moved && (last_ms == 0 || now_ms - last_ms >= kLogGapMs);
}

// --- Runtime (native/partner_ui_filter.cpp; game memory, SEH everywhere) -----
using LogFn = void (*)(const char* line) noexcept;
// Once per process (key on, drain mode on, after the code gate): the five
// vtable slots must point at the leaves and the leaves' complete bodies must
// match their pins; then one MinHook per leaf. nullptr (also when already
// installed) or a static reason, logged once:
// image | slots | pins | minhook | module | hook | enable | disabled.
const char* Install(const void* base, std::size_t span, LogFn log) noexcept;
bool Installed() noexcept;
// Every leaf still starts with the MinHook jump (the driver's tick).
bool PatchLive() noexcept;
// Teardown: the hooks pass every request to the original; Begin refuses.
void Shutdown() noexcept;
// Drain scope: Begin right before block 1's job (the hook thread), End on
// every exit (idempotent). Begin validates P+0xD8 (CheckIdentity) under SEH
// and digests the five request records; End compares the digest.
void Begin(std::uintptr_t P) noexcept;
void End() noexcept;
// MissionStart: clears the fault latch.
void ResetMission() noexcept;
// Process-lifetime totals (the driver logs deltas from its tick).
struct Stats {
    bool installed, live, latched;
    std::uint32_t windows;              // Begin calls that opened the scope
    std::uint32_t refused;              // Begin calls refused (last_refusal says why)
    std::uint8_t last_refusal;          // Refusal
    std::uint32_t swallowed[kQueues];   // body-1 requests swallowed, per queue (kQueue order)
    std::uint32_t foreign;              // in scope on the hook thread but another receiver (passed)
    std::uint32_t passed;               // requests that went to the original (everything else)
    std::uint32_t changed;              // windows whose request records changed across the job (diagnostic)
    std::uint32_t faults;               // SEH faults in Begin / End (latch the mission)
    std::uint32_t keys;                 // distinct swallowed keys recorded (<= kKeys)
    std::uint64_t key[kKeys];
};
void ReadStats(Stats& out) noexcept;

namespace detail {
using LeafFn = void (*)(void* ui, std::uint16_t id, std::uint32_t priority, std::uint8_t flag, std::uint8_t a5, std::uint8_t a6,
                        std::uint8_t a7);
// Test only: originals bound instead of MinHook trampolines and an image base
// for CheckIdentity / the caller RVAs; the detours are callable directly.
void BindForTest(const void* base, const LeafFn (&originals)[kQueues]) noexcept;
void ResetForTest() noexcept;
LeafFn Detour(unsigned queue) noexcept;
}

} // namespace mgscoop::partner_ui_filter
