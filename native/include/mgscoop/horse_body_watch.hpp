#pragma once
// BODY0 round (horse plan M2, lab key world_horse_cargo): a hardware write watch on this copy's own body 0 transform T
// (the partner contract's T vtable 0x24c8ef0; hold0 in native/include/mgscoop/horse_drive.hpp) while the downed player is
// held on the horse. hold0's reset bit 2 (kWatchBit) watches, bit 3 (kPinBit) pins.
// DR0 = T+0x24 (the capsule centre's y; T.vt+0x58 0x141AD5660 writes the centre T+0x20) and DR1 = T+0x14 (the foot's y,
// T+0x10), both 4-byte write watches, set on every thread of the process by a watcher thread (no code patch). Each trap
// (EXCEPTION_SINGLE_STEP right after the write) records the writer: its rva, thread, the order of the write since the last
// hold0 (0 = hold0's own T.vt+0x58) and the first image return addresses on its stack. With the pin the handler then puts
// T+0x20 / T+0x10 / T+0x174 back to what hold0 wrote, so no later write of the frame moves body 0 (run cfd2b604: with the
// C0 reset the lying pose held, but something after hold0 dropped the body 1.4-2.9 m to the ground under the horse).
// No hold0 for kIdleMs disarms every thread. Pure part here (horse_body_watch_policy.cpp, tests/horse_drive_tests.cpp);
// live part native/horse_body_watch.cpp (mgs_partner_player).
#include <cstddef>
#include <cstdint>

namespace mgscoop::horse_body_watch {

inline constexpr std::uint8_t kWatchBit = 4, kPinBit = 8;
inline constexpr std::size_t kRips = 16, kRets = 4;
inline constexpr std::uint32_t kIdleMs = 500;    // no hold0 this long: disarm
inline constexpr std::uint32_t kReportMs = 2000; // the receipt logs the writer table this often
// The pin holds only this long after a hold0: longer than world_horse_cargo's 200 ms without a pin after M1's last placement
// (run 911ce293: with 100 ms body 0 dropped to the ground under the horse right after the dismount and the horse fell).
inline constexpr std::uint32_t kPinMs = 300;
// The pin is live at now_ms when the last hold0 (last_ms, 0 none) was at most kPinMs before.
bool PinLive(std::uint64_t last_ms, std::uint64_t now_ms) noexcept;
// DR7 with our two watches set (on) or cleared (off), the other bits kept: L0 / L1, RW 01 (write), LEN 11 (4 bytes).
std::uint64_t Dr7(std::uint64_t dr7, bool on) noexcept;
// A trap's DR6 is ours: B0 or B1.
bool Ours(std::uint64_t dr6) noexcept;
struct Rip {
    std::uint32_t rva{}, n{}, tid{}, ord_min{}, ord_max{};
    std::uint32_t ret[kRets]{};
};
struct Table {
    Rip rip[kRips]{};
    std::uint32_t used{}, traps{}, dropped{}, pinned{};
};
// One trap: the writer's rva (0 outside the image), its thread, its order since the last hold0 and its stack's image
// return rvas (0-terminated). A new writer past kRips counts dropped.
void Note(Table& t, std::uint32_t rva, std::uint32_t tid, std::uint32_t ord, const std::uint32_t (&ret)[kRets]) noexcept;
// Image return rvas in a stack window: values v with text_lo <= v - image < text_hi, in order, up to kRets.
std::size_t StackRets(const std::uint64_t* stack, std::size_t n, std::uintptr_t image, std::uint32_t text_lo, std::uint32_t text_hi,
                      std::uint32_t (&ret)[kRets]) noexcept;
// The fields T.vt+0x58 writes for the centre (x, y, z) and the half height h = T+0x160: T+0x20 = (x, y, z, 0),
// T+0x10 = (x, y - h, z, 0), T+0x174 = y - h.
void Target(float x, float y, float z, float h, float (&centre)[4], float (&foot)[4], float& foot_y) noexcept;

// Live (the hold0 receipt, runtime thread). Before hold0's write: the pin target from (x, y, z) and T+0x160, the order
// base, and the watcher asked to arm T. Returns nullptr or a reason (watch_veh, watch_thread, watch_t).
const char* BeforeHold(std::uintptr_t image, std::size_t span, std::uintptr_t t, float x, float y, float z, bool pin) noexcept;
// free0: the pin and the watch end now (a take-down or lay-down Warp of body 0 follows).
void Release() noexcept;
// Copy and clear the table (false: no trap since the last take); threads = the threads armed now.
bool Take(Table& out, std::uint32_t& threads) noexcept;

} // namespace mgscoop::horse_body_watch
