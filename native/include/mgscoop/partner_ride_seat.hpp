#pragma once
// The seated vehicle rider (partner_native_ride=seat; docs/anim-round1-seat.md,
// critic.md section 5): a stock targeted direct motion on body 1,
//   {{"coopRide",1},{<seated state>,true,<receiver's vehicle locator>,"CNP_ppos_c","MTP_GLOBAL_C",true}},
// anchors the body every frame to the seat matrix of the receiver's own vehicle
// copy (anchor 0x1198770 -> 0xB7AF90(unit,out,seat) -> rec+0x160, applied to
// *(PS+0x270)[1] in phase 6). Four reversible code dwords make that usable:
//
//   seat      0x119883C  30 41 B0 02 -> 30 41 B0 <seat>  the anchor's seat immediate (retail: rear-left 2)
//   cam_play  0x1198D56  C7 82 00 02 -> EB 64 00 02      targeted entry skips CAM+0x200 = 1 / CAM+0x1F0 = 0x88..|id (jmp 0x1198DBC)
//   cam_plain 0x1198D81  48 8B 47 08 -> EB 39 47 08      untargeted entry skips CAM+0x200/+0x1F0 = 0 (jmp 0x1198DBC)
//   cam_end   0x11992E9  48 89 81 F0 -> EB 0C 81 F0      DM End skips CAM+0x1F0/+0x200 = 0 (jmp 0x11992F7; rdx/r14 live)
//
// The camera skips keep the viewer's own vehicle/walker camera connect (the
// same CAM fields) intact across the partner's DM entry and End. critic.md
// proposed a qword at 0x1198D22 for cam_play; that address is 8-byte
// misaligned (near_code::PatchQword refuses it and does not flush the
// instruction cache), so the equivalent dword at 0x1198D56 (the first store)
// is used: the same loads still run, both stores are skipped. Every site and
// both function bodies are pinned in tools/generate_partner_contract.py and
// re-checked here before each write (the patched dword itself is masked, so a
// patched block re-validates for an idempotent revert).
//
// Seat 1 caveat (0xB79A3F..0xB79A57): 0xB799C0 maps seat 1 to seat 0 unless
// word(unit+0x32) & 0xFE00; on an empty receiver copy the front passenger may
// therefore be drawn in the driver's seat (hidden test T4).
//
// Arm only while no DM runs on either body, only in 30010/30020 and only for
// the seat DM's lifetime; revert once DM End ran (body act != 0x8B), and at
// mission end, stop and shutdown (the driver owns that policy).
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_ride_seat {

using LogFn = void (*)(const char* line) noexcept;

enum class SiteId : std::uint8_t { seat, cam_play, cam_plain, cam_end };
inline constexpr unsigned kSites = 4;
struct Site {
    const char* name;
    std::uintptr_t rva;
    std::uint32_t original;  // little-endian dword
    std::uint32_t patched;   // for seat: the seat byte (top) is replaced at Arm
};
const Site& SiteFor(SiteId id) noexcept;
// The seat dword for `seat` (0..3): 0x00B04130 | seat << 24.
std::uint32_t SeatDword(std::uint8_t seat) noexcept;

// Pure verification of an image (the running module or a fixture buffer of at
// least `span` bytes): every pinned block of the contract that covers the four
// sites and the anchor/playback/End bodies, with each site's dword masked.
// nullptr when it matches, else a static reason ("span", "block_<rva>" names
// the first mismatching block's contract rva).
const char* VerifyImage(const unsigned char* base, std::size_t span) noexcept;
// The dword at `id` in `base` is the original, the patched value (for seat:
// any seat 0..3) or something else.
enum class SiteState : std::uint8_t { original, patched, other };
SiteState StateOf(const unsigned char* base, SiteId id) noexcept;

// Win32: Arm writes the four dwords (seat first, End skip last) after
// VerifyImage, each with one locked compare-exchange (near_code::PatchDword);
// a failure part way reverts the ones it wrote. Logs
// partner_ride_seat=patched; seat=..; or partner_ride_seat=refused; reason=...
// Re-arming an armed module with another seat rewrites only the seat dword.
// Revert restores every site it finds patched (logs
// partner_ride_seat=reverted; reason=..). Both are idempotent.
bool Arm(void* base, std::size_t span, std::uint8_t seat, LogFn log) noexcept;
void Revert(void* base, std::size_t span, LogFn log, const char* reason) noexcept;
bool Armed() noexcept;
std::uint8_t ArmedSeat() noexcept;  // 0xFF when not armed

// --- The seated state table (StrCode64 of the MOG state names, verified
// present in TppPlayer2_layers.mog; critic_work/mogcheck.py). class 0 = light
// vehicle (veh_lv_*), 1 = truck (veh_trc_*). Truck seat 1 has no
// stateTruckFrontRightLoop in the MOG: stateVehicleFrontRightLoop is the
// documented guess (hidden test). Truck seats 2/3 and APC/tank are refused.
enum class SeatClass : std::uint8_t { light, truck };
struct SeatStates {
    const char* loop;
    const char* get_on;
    const char* get_off;
    std::uint64_t loop_code, get_on_code, get_off_code;
};
// nullptr when the class has no seated state for `seat`.
const SeatStates* StatesFor(SeatClass cls, std::uint8_t seat) noexcept;
// Whether a DM record's current state code (rec+0x18, low 48 bits) is one of
// the seated loop/get-on/get-off states of `seat` in any class (the driver's
// confirmation: Lua picks the class from the receiver's locator name).
bool IsSeatState(std::uint64_t code, std::uint8_t seat) noexcept;
// Never sent: retail s10150's rear-seat state, whose phase-6 code drives the
// camera (0x11995F1..0x119971C).
inline constexpr std::uint64_t kForbiddenRearSeatState = 0x2278ccf2234dull;

} // namespace mgscoop::partner_ride_seat
