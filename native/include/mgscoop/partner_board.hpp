#pragma once
// F1 vehicles (docs/native-puppet.md "Vehicle boarding"): the two nearest-seat
// call-site redirects. Site A 0x12458AC (the ride pre-query's CALL 0x124B3A0)
// and site B 0x1251479 (phase 1's CALL 0x124B360) are retargeted to SiteA /
// SiteB through 14-byte near stubs (partner_input_hook::detail::RetargetCall,
// the partner_defer H2 pattern); B failing reverts A. Every block both wrappers
// rely on is pinned in tools/generate_partner_contract.py (board_blocks) and
// re-checked here before the patch (VerifyImage, the two rel32 masked).
//
// The wrappers run the pure partner_board_policy decision on the published
// policy word (the driver tick, one atomic u64, valid only inside the ride
// lease): stock (the original call), block (4) or passenger (the original
// 0x124B3A0 over the free back seats only). They read u16 unit+0x78 and, in
// passenger mode, call the two read-only getters GetVehicleType 0x55E7F0 and the
// seat count 0xA27CC0 (their only native calls besides the original search), all
// under SEH: a getter fault blocks; an index-read fault passes through to stock
// (the vehicle is unknown, and the stock search reads the same unit first); both
// are counted. Counters only: the driver logs them from its tick.
//
// Lazy install from the driver after the partner code gate passed (the observer
// glue refuses without it), once per process; never reverted (with the policy
// off both wrappers are the stock calls).
#include <cstddef>
#include <cstdint>
#include "mgscoop/partner_board_policy.hpp"

namespace mgscoop::partner_board {

using LogFn = void (*)(const char* line) noexcept;
using NowFn = std::uint64_t (*)() noexcept;

// nullptr when both sites are live (also on a second call), else a static
// reason: VerifyImage's base / contract / span / read / bytes (nothing patched;
// the partner_board=refused line carries the block rva), a_<RetargetCall reason>, b_failed
// (B refused, A reverted) or b_revert (B refused and A could not be reverted:
// its wrapper stays and passes through, the policy is never published). Logs
// partner_board=installed|refused once.
//
// M6 seat 1 (opts.front / opts.cab, partner_board_policy.hpp): after both sites
// are live, the seat-1 blocks (board_seat1_blocks: the two phase-1 sites and the
// seat matrix's seat-1 remap) are checked and the remap `jne` becomes a `jmp`
// (one locked dword, never reverted; logged partner_board=seat_one). A refusal
// there keeps the back seats (opts stay off). Site B then sets both phase-1
// immediates for each call it makes (1 only for a steered seat 1).
const char* Install(const void* base, std::size_t span, NowFn now, LogFn log, SeatOpts opts = {}) noexcept;
// The seat-1 options that are live (both off until Install armed them).
SeatOpts LiveSeatOpts() noexcept;
// M6 seat races (partner_ride_race; partner_board_policy.hpp "seat races"): on, the pre-query records the
// empty vehicle the local player could drive (want) and a live deny answers 4 for that vehicle in the
// stock verdict at both sites. RaceDeny(vehicle, ms) sets it for ms (at most kRaceDenyMaxMs; 0 or 0xFFFF
// clears). The wrappers only read u16 unit+0x78 for it (SEH).
void SetRace(bool on) noexcept;
bool LiveRace() noexcept;
void RaceDeny(std::uint16_t vehicle, std::uint32_t ms) noexcept;
struct RaceState {
    bool on;
    std::uint16_t want;           // the last vehicle the pre-query offered as the driver's (0xFFFF none)
    std::uint32_t want_age_ms;    // since then (0xFFFFFFFF none)
    std::uint16_t deny;           // the denied vehicle while live (0xFFFF none)
    std::uint32_t deny_left_ms;
    std::uint32_t hits;           // searches answered 4 by the deny (process lifetime)
};
void ReadRace(RaceState& out) noexcept;
bool Installed() noexcept;
const char* InstallRefusal() noexcept;
// Both call sites still call our stubs (the driver checks it every tick).
bool PatchLive(const void* base) noexcept;
// The driver tick's policy word (Pack); 0 = off.
void Publish(std::uint64_t policy) noexcept;
std::uint64_t Published() noexcept;

// Process-lifetime counters (site 0 = A pre-query, 1 = B phase 1).
struct Stats {
    std::uint32_t calls[2];   // every search through the wrapper
    std::uint32_t block[2];   // returned 4 for the peer's vehicle (block mode, or no free back seat)
    std::uint32_t steer[2];   // returned a back seat (passenger mode)
    std::uint8_t last_seat[2];
    std::uint16_t last_vehicle;   // the policy vehicle of the last block or steer
    std::uint8_t last_type;       // GetVehicleType of the last passenger decision (0xFF none)
    std::uint8_t last_count;      // its seat count (0xFF none)
    std::uint32_t faults;         // SEH-caught index reads / getter calls; failed phase-1 immediate writes
    const char* last_reason;      // the last non-stock decision's reason (static)
    std::uint32_t seat_one;       // M6: phase-1 calls steered to seat 1 with both immediates at 1
};
void ReadStats(Stats& out) noexcept;

namespace detail {
// The pinned blocks against an image of at least `span` bytes (the site A/B
// rel32 operands masked): nullptr, or the static reason "base" (no image),
// "contract" (a block missing from code_sites), "span" (a block past `span`),
// "read" (an SEH-caught read) or "bytes" (a block differs), with that block's
// rva in mismatch_rva.
const char* VerifyImage(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept;
// Fixture seams (never called in the game): the originals, the getters and the
// clock bound without patching anything; the wrappers called directly.
using NearestFn = std::uint8_t (*)(void* unit, const float* pos, std::uint8_t first, std::uint8_t last);
using DriverFn = std::uint8_t (*)(void* unit, const float* pos);
using TypeFn = std::uint8_t (*)(void* unit);
using CountFn = int (*)(void* unit);
void BindForTest(NearestFn nearest, DriverFn driver, TypeFn type, CountFn count, NowFn now) noexcept;
// M6: the seat-1 options and the phase-1 immediate writer bound without an install
// (the live writer patches the two image dwords; false: not written).
using LimitFn = bool (*)(std::uint8_t limit) noexcept;
void BindSeatOneForTest(SeatOpts opts, LimitFn limit) noexcept;
std::uint8_t SiteAForTest(void* unit, const float* pos, std::uint8_t first, std::uint8_t last) noexcept;
std::uint8_t SiteBForTest(void* unit, const float* pos) noexcept;
void ResetForTest() noexcept;
} // namespace detail

} // namespace mgscoop::partner_board
