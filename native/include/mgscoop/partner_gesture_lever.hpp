#pragma once
// Lab lever for the gesture proof (docs/native-puppet.md "Gestures", "Lab lever:
// knock and hand signs"): this copy's OWN player knocks or makes the hand sign
// Go / Stop through the stock request entry points the game's call menu uses,
// so a hidden run can film the guest's body 1 replaying the host's edge.
// Lab key partner_gesture_lever (default 0, play forces it off); debug command
// "gesture_lever knock|sign_go|sign_stop" (native, never reaches Lua).
//
// Stock (mgsvtpp.exe 1.0.15.4, RVAs; read-only research, docs "Gestures"):
// - The block's *(blk+0xF8) is its KnockActionPluginImpl (primary vtable
//   0x23C5A20, set by the constructor 0x11E3750; *(plugin+8) == blk). Its request
//   slots: vt+0x28 RequestKnock 0x11E5100, vt+0x30 RequestHandSignGo 0x11E5060,
//   vt+0x38 RequestHandSignStop 0x11E50B0, each (plugin, id): row =
//   *(plugin+0x68) + (id - *(*(plugin+0x38)+0x24))*0xE0; row+0xD0 |= 1,
//   row+0x24 = 1|2|3, row+0x18 = the frame (QST+0x40 -> +0x38), row+0x20 = 2.
// - The polling vt+0x58 0x11E5160 takes the request through FUN_11E3A20 while
//   frame <= row+0x18 + row+0x20 (knock: always; signs: speed Q+0x38 <= 1, act
//   not 0x79/0x7A, PS rec+0xC bit 28 clear) and starts state 1 StateKnock
//   0x11E7210 (Q+0x20 = 0x17), 2 StateHandSignGo 0x11E6A40 (0x18) or 3
//   StateHandSignStop 0x11E6C30 (0x19; signs not while GetStance 0x1002350
//   is 4: the raw Q+0x30 is 1..3, GetStance returns 4 for 3 (crawl) with PS
//   rec+0xC bit 20 set).
// - Stock callers (the [blk+0xF8] loads that feed these slots in a .text scan;
//   the request functions have no direct caller): the call menu
//   (hold CALL = Q, PP vt+0x90(9, 12) == 3) command set 0 "knock" 0x110CCE0
//   (enabled while act 1..20 or 31..37, Q+0x20 == 0, Q+0x28 == 0, speed not
//   2..3: UpdateCommandSetKnock 0x110F8D0); the buddy commands 0x110CF10 (Quiet
//   "Cover me" 4 and D-Horse "Do it" 0x13 -> Go, Quiet "Cease cover" 5 -> Stop;
//   no D-Dog command signs); the child-soldier orders 0x110CD30 (Go) and
//   0x110CE20 (Stop).
//
// Safety: lab only (the key, mode=lab, lab active, saves redirected, the
// offline branch, the partner code gate); this copy's own player (P+0x214 = 0,
// id 0) is the only target; the identity (P, Q, both blocks, both plugins,
// their back pointers and work rows) and the slot are re-checked on every
// command; the call runs under SEH (a fault latches the lever off for the
// process); instance 1's request fields and upper act byte are compared before
// and after (any change latches the lever off: other_changed).
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_gesture_lever {

// --- Pure part (mgs_coop_core) ----------------------------------------------
enum class Request : std::uint8_t { none = 0, knock = 1, sign_go = 2, sign_stop = 3 };
const char* RequestName(Request request) noexcept;
inline constexpr char kCommand[] = "gesture_lever";
// True for "gesture_lever" and "gesture_lever <anything>" (the command is ours
// even when its argument is wrong).
bool IsCommand(const char* text) noexcept;
// "gesture_lever knock|sign_go|sign_stop" (one space, nothing else); none otherwise.
Request Parse(const char* text) noexcept;

inline constexpr std::uintptr_t kPluginVtableRva = 0x23C5A20;  // KnockActionPluginImpl (primary)
inline constexpr std::uintptr_t kPollingRva = 0x11E5160;        // vt+0x58
inline constexpr std::uintptr_t kBlockPlugin = 0xF8;            // *(blk+0xF8)
inline constexpr std::uintptr_t kRowStride = 0xE0;
struct Slot {
    std::uintptr_t offset;      // vtable slot
    std::uintptr_t target_rva;  // the stock request function
    std::uint8_t row_kind;      // row+0x24 after the call
    std::uint8_t upper;         // Q+0x20 the state writes
};
// offset 0 for Request::none.
Slot SlotFor(Request request) noexcept;

// Admission, in this order: key_off, mode, lab_inactive, saves_not_redirected,
// offline_branch, partner_off, code_gate, gameplay, shutting_down, latched;
// nullptr admitted.
struct Gate {
    bool key, lab_mode, lab_active, saves_redirected, offline, partner_on, code_gate, gameplay, shutting_down, latched;
};
const char* Admit(const Gate& gate) noexcept;

// This copy's own arrays at its id (Q+0x18 act, +0x20 upper, +0x28, +0x30
// stance, +0x38 speed). The stock call menu's knock entry is enabled only
// while act 1..20 or 31..37, upper 0, Q+0x28 0 and speed not 2..3; the sign
// request needs speed <= 1, act not 0x79/0x7A, PS rec+0xC (`recc`) bit 28
// clear and GetStance != 4 (StanceOf below). The lever also needs upper 0
// for the signs (the sender reports a rising edge).
// nullptr when met, else act | upper | q28 | speed | rec_c28 | stance.
struct Local {
    std::uint16_t act;
    std::uint8_t upper, q28, stance, speed;
};
inline constexpr std::uint32_t kRecCCrawlBit = 0x00100000;  // PS rec+0xC bit 20 (GetStance)
inline constexpr std::uint32_t kRecCSignBit = 0x10000000;   // PS rec+0xC bit 28 (FUN_11E3A20)
// GetStance 0x1002350: the raw Q+0x30 byte, but 4 when it is 3 (crawl) and
// PS rec+0xC bit 20 is set.
std::uint8_t StanceOf(std::uint8_t stance, std::uint32_t recc) noexcept;
const char* Precondition(Request request, const Local& local, std::uint32_t recc = 0) noexcept;

// --- The lever's outcome (Fire, mgs_partner_player) and its formatting (pure) --
struct Outcome {
    const char* refusal;      // nullptr: the request was made
    Request request;
    std::uint32_t id;         // this copy's id (0)
    Local local;              // before the call
    std::uint8_t row_kind, row_d0, row_life;  // the row after the call (+0x24, +0xD0, +0x20)
    std::uint32_t row_frame;  // row+0x18 after the call (the request frame)
    std::uint32_t rec4, rec8, recc;  // PS rec +4 / +8 / +0xC of this id (the sign gates read +0xC bits 20 / 28)
    bool called;              // the stock request function ran (requested, other_changed)
    bool other_same;          // instance 1's request fields and upper byte unchanged
};
// Native (mgs_partner_player). Game (runtime) thread. `image` is the module base; P is re-read from the
// player slot. Refusals: latched | player | online | count | q | ids | pool |
// blocks | block | ps | plugin | slot | row | arrays | act | upper | q28 |
// speed | rec_c28 | stance | fault | other_changed (the last two latch the
// lever off).
Outcome Fire(std::uintptr_t image, Request request) noexcept;
bool Latched() noexcept;
// The debug command's result (at most 160 characters):
// "gesture_lever=requested;kind=..;id=..;act=..;..." or
// "gesture_lever=refused;reason=..;kind=..;...": ASCII [A-Za-z0-9_=;,.] only.
std::size_t FormatResult(const Outcome& outcome, char* out, std::size_t capacity) noexcept;
// The observer log line, one per command: "partner_gesture=lever; result=
// requested|refused; reason=..; kind=..; id=..; act=..; upper=0x..; q28=..;
// stance=..; speed=..; called=0|1; row_kind=..; row_d0=0x..; row_life=..;
// frame=..; rec4=0x..; rec8=0x..; recc=0x..; other_same=0|1\r\n".
std::size_t FormatLog(const Outcome& outcome, char* out, std::size_t capacity) noexcept;

namespace detail {
// Tests: replace the called function (the slot is still checked against the
// stock target); nullptr restores the slot's function. Clears the latch.
using RequestFn = void (*)(void* plugin, std::uint32_t id);
void SetCallForTest(RequestFn fn) noexcept;
}  // namespace detail

}  // namespace mgscoop::partner_gesture_lever
