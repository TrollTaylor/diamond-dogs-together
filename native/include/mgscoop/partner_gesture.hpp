#pragma once
// Gestures on body 1 (docs/native-puppet.md "Gestures"): the native side.
//
// The driver (game thread) turns a received gesture edge into a Request with
// the stock node and method (partner_gesture_policy) and publishes it with
// Step every tick. The request is submitted only inside body 1's own motion
// update: a MinHook on the per-entry update 0xB6C410, filtered to the call
// from ActMotionStart 0xAF0340 (return address image+0xAF0381) for body 1's
// entry, exactly the window partner_upper_manual_idle uses. Right before the
// original update runs:
//   1. IsGraphExistNode(cs, 1, 2, node, {0,0})            cs vt+0x210 0xAF1F10
//      (the tag list is a zero-terminated array: graph vt+0x40 0x1C6FEA0
//      walks it, so null is never passed)
//   2. pickup:     SetCurrentNode(cs, 1, 2, node, 0.5f, 0)  cs vt+0x170 0xAF2BA0
//      sign/knock: ReserveToResetNode(cs, 1, 2, node, 1.0f, 0) vt+0x160 0xAF2710
// and the same update consumes the request (0xB6E9F0 / 0xB6E6E0 only write
// body 1's request buffer). Every later body-1 update tests the end: pickup
// vt+0x228(cs, 1, 2) 0xAF1F70, sign/knock vt+0x2B8(cs, 1, 3) 0xAF2110 or the
// remote's speed > 1, or the 2000 ms deadline; then, only while graph 2 still
// shows our node, SetCurrentNode(cs, 1, 2, stateNull 0xC1A2050D3802, 0.0f, 0)
// (the validated manual_idle return). Another owner of graph 2 ends the
// gesture without a write (via=override; cause=node).
//
// Safety: body 1's identity (P, Q, block 1 with id 1 only, PS, CS with base 1
// and count 1, entry, graph and buffer layout) is re-resolved on every body-1
// update; the five cs vtable slots are checked before every call; every call
// runs under SEH (a fault disables the hook's work for the process). Around
// every submission the viewer's own body 0 (request masks and graph-2 request
// record) is digested before and after, and body 1 may change only graph-2
// mask bits and keep its graph-0 record: any surprise latches `uncertain` for
// the mission epoch (no further submissions until the next epoch).
//
// Threads: Step on the game thread; the hook on whatever thread runs body 1's
// motion update. The command and the receipts are behind one SRW lock (no game
// call under it); the hook's own state is single-owner (one body-1 update at a
// time, guarded by a try-flag).
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_gesture_policy.hpp"

namespace mgscoop::partner_gesture {
namespace pol = partner_gesture_policy;
using LogFn = void (*)(const char* line) noexcept;

// SHA-256 pins checked immediately before the hook is created (the same
// values as partner_upper_manual_idle / end_observer where they overlap).
struct Pin { std::uintptr_t rva; std::size_t size; const char* sha256; };
inline constexpr Pin kPins[] = {
    {0xb6c410, 0x1f2, "6ffea8164e703db1f24e1e9f0e3f53e0a0a2083d2fdc52cacb1361969e3adf6e"},  // the per-entry update (hooked)
    {0xaf0340, 0x5d, "2ceda96c5086a8a142d8460b117c92e78bd2b55ef74db4c35ee934b4ae9b90c4"},   // ActMotionStart (the caller)
    {0xaf2ba0, 0x8e, "bf4f8d6085f686bad8a6eecd5863a6a8b0b39c0f0df19e3803e6e7f648a709ff"},   // SetCurrentNode
    {0xaf2710, 0x83, "cced851de795e6a9854db07f233822cb36ae5643f21aa057b2f3a36c3be60bf1"},   // ReserveToResetNode
    {0xaf1f10, 0x56, "97aa07f4ccd5b4369c9611ce3face36f26d5191a31095e541c9cf8f95c0bfda9"},   // IsGraphExistNode
    {0xaf1f70, 0x28, "b0c67c63652884c8cdd03531b6a1ea084d4654ba1ce7b2c878348f7e05430f1e"},   // vt+0x228 (graph vt+0xD8)
    {0xaf2110, 0x41, "a760923737d054b4df3021eb95b196bbfad1e4d588cceb9cd6b2adadebe8704d"},   // vt+0x2B8 (IsPlayEnd)
    {0xb6e9f0, 0xa5, "0cbabd97f75b22722107aa37ff97343684ece2e4c76938b4a2448a1dd8b504d1"},   // SetRequest
    {0xb6e6e0, 0x88, "46f645c4efc930d692aa562b5fea06dee8babd03e199ada7ee8bb04bb3670c47"}};  // ReserveToReset
struct Slot { std::uintptr_t vtable_rva, offset, target_rva; };
inline constexpr std::uintptr_t kCsVtableRva = 0x22e6070, kGraphVtableRva = 0x250ba50;
inline constexpr Slot kSlots[] = {{kCsVtableRva, 0x170, 0xaf2ba0}, {kCsVtableRva, 0x160, 0xaf2710}, {kCsVtableRva, 0x210, 0xaf1f10},
                                  {kCsVtableRva, 0x228, 0xaf1f70}, {kCsVtableRva, 0x2b8, 0xaf2110}};
inline constexpr std::uintptr_t kUpdateRva = 0xb6c410, kCallerRva = 0xaf0381;

struct Request {
    std::uint32_t seq;        // the driver's serial (never reused in a process), 0 none
    pol::Kind kind;
    std::uint64_t node;
    pol::Method method;
    float blend;
    std::uint64_t issued_ms;  // GetTickCount64 when the driver published it
};
struct Command {
    bool enabled;             // the key, the native drive, a realized body and no block (the driver's gate)
    std::uint32_t epoch;      // mission serial: a new epoch drops running state and clears the uncertain latch
    std::uint64_t now_ms;     // GetTickCount64 at publication (the hook's lease is kLeaseMs)
    Request request;
    bool remote_known;
    std::uint8_t remote_speed;
};
enum class Stage : std::uint8_t { submit, refused, applied, ended, uncertain, fault };
const char* StageName(Stage stage) noexcept;
struct Receipt {
    Stage stage;
    std::uint32_t seq;
    pol::Kind kind;
    pol::Method method;
    std::uint64_t node;
    bool exist;
    bool ok;                  // applied: graph 2 shows the node after the update
    const char* reason;       // refused / uncertain / fault
    pol::Via via;             // ended
    const char* cause;
    bool null_sent;           // ended: stateNull was submitted
    std::uint64_t current, queued;  // graph 2's node codes (applied / ended / refused)
    std::uint32_t ms;         // ended: since the submission; refused: the request's age
    std::uint16_t body_act;
    std::uint8_t body_upper, body_q28;
};
inline constexpr unsigned kReceipts = 16;
struct Status {
    bool installed, live, faulted, active, uncertain;
    // Body 1's identity on the game thread this step (Step's Resolve): nullptr
    // when resolved (the update filter then has body 1's entry), else the
    // refusal (player | online | count | q | ids | pool | blocks | block | ps |
    // cs | entry | buffer | graph | shared) or not_installed / faulted.
    const char* resolve;
    std::uint32_t active_seq, epoch;
    std::uint32_t body_calls, submits, ends, refusals, dropped;
    unsigned count;
    Receipt receipts[kReceipts];
};

// Lazy, once per process (after the startup code gates): the pins, the cs
// slots, the caller's call bytes, then MinHook on 0xB6C410. nullptr when
// installed, else a static reason (also logged): phase_observer | pins |
// slots | caller | minhook | module | hook | enable.
const char* Install(const void* base, std::size_t span, LogFn log) noexcept;
bool Installed() noexcept;
// True while the hooked prologue still starts with the hook's jump.
bool PatchLive() noexcept;
// Game thread, every driver tick: publishes `command`, resolves body 1's entry
// for the update filter, and drains the receipts and counters into `out`.
void Step(const Command& command, Status& out) noexcept;

// The forwarding body of the detour (fixture tests call it with a fake original).
using OriginalFn = std::uintptr_t (*)(void* entry);
std::uintptr_t ForwardUpdate(OriginalFn original, void* entry, std::uintptr_t caller, std::uint64_t now_ms) noexcept;

namespace detail {
// The five methods (production: image + RVA). Tests bind fakes without a hook.
struct Calls {
    bool (*exist)(void* cs, std::uint32_t body, std::uint32_t graph, std::uint64_t node, const std::uint64_t* tags);
    void (*set)(void* cs, std::uint32_t body, std::uint32_t graph, std::uint64_t node, float blend, std::uint32_t option);
    void (*reserve)(void* cs, std::uint32_t body, std::uint32_t graph, std::uint64_t node, float blend, std::uint32_t option);
    bool (*motion_end)(void* cs, std::uint32_t body, std::uint32_t graph);  // vt+0x228
    bool (*play_end)(void* cs, std::uint32_t body, std::uint32_t layer);    // vt+0x2B8
};
void BindForTest(std::uintptr_t image, const Calls& calls) noexcept;
void ResetForTest() noexcept;
// Resolves body 1's identity like the hook does (nullptr ok, else the refusal).
const char* ResolveForTest(std::uintptr_t image, std::uintptr_t& entry1) noexcept;
}

} // namespace mgscoop::partner_gesture
