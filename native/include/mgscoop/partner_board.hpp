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
// off both wrappers are the stock calls). M6 review R1/R2: with partner_ride_race
// a third call site, C 0x1246A4F (RideVehicle CanExec's call of the pre-query
// 0x1245820), is retargeted by InstallRace (its rel32 masked the same way).
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
// seat matrix's seat-1 remap) are checked (logged partner_board=seat_one). A
// refusal there keeps the back seats (opts stay off). Site B then sets both
// phase-1 immediates for each call it makes (1 only for a steered seat 1, and
// only while the remap is skipped: review R4, SetRemapSkip).
const char* Install(const void* base, std::size_t span, NowFn now, LogFn log, SeatOpts opts = {}) noexcept;
// The seat-1 options that are live (both off until Install armed them).
SeatOpts LiveSeatOpts() noexcept;
// Review R4: the seat-1 remap (seat matrix 0xB799C0 maps seat 1 to 0 while rider[1] holds a player id)
// is skipped (jne -> jmp at 0xB79A43, one locked dword) only while the driver tick says seat 1 may be in
// use (partner_board_policy.hpp RemapWanted). True when the dword is in the asked state; false when seat 1
// is not armed, the dword is foreign or the write failed. The driver tick is its only caller (one writer).
bool SetRemapSkip(bool skip) noexcept;
bool RemapSkipped() noexcept;
// M6 seat races (partner_ride_race; partner_board_policy.hpp "seat races", review R1/R2): InstallRace
// retargets site C 0x1246A4F (RideVehicle CanExec's call of the pre-query 0x1245820) after Install
// succeeded, and turns the race on: nullptr, or "board" (not installed), c_<RetargetCall reason> (the
// race stays off, A and B unchanged). Logs partner_board=race once. Site C records the want (the
// pre-query's final answer: true with seat 0/1 for a vehicle without a peer-driver policy) and answers
// false for a live deny or an ungranted hold; site B answers 4 for them (the backstop). RaceDeny(vehicle,
// ms) / RaceGrant(vehicle, ms) set one vehicle for ms (at most kRaceDenyMaxMs; 0 or 0xFFFF clears; a deny
// withdraws a grant of that vehicle and the other way round); RaceHold(ms) is the guest's lease (0 clears).
// The wrappers only read u16 unit+0x78 and the pre-query's seat answer for it (SEH).
const char* InstallRace() noexcept;
void SetRace(bool on) noexcept;  // tests; the game turns it on through InstallRace only
bool LiveRace() noexcept;
void RaceDeny(std::uint16_t vehicle, std::uint32_t ms) noexcept;
void RaceGrant(std::uint16_t vehicle, std::uint32_t ms) noexcept;
void RaceHold(std::uint32_t ms) noexcept;
// E press only (design 8.55; partner_board_policy.hpp PressDecide): the host's arbiter. Under the lock its site B
// takes for its own press: false while the host's press owns the vehicle (kHostCommitMs); else the vehicle is yielded
// (RaceDeny(vehicle, ms): the host's site B and site C refuse it) and true.
bool RaceGrantIf(std::uint16_t vehicle, std::uint32_t ms) noexcept;
struct RaceState {
    bool on;
    std::uint16_t want;           // the last vehicle the pre-query answered as the driver's (0xFFFF none)
    std::uint32_t want_age_ms;    // since then (0xFFFFFFFF none)
    std::uint16_t deny;           // the denied vehicle while live (0xFFFF none)
    std::uint32_t deny_left_ms;
    std::uint32_t hits;           // answers refused by the deny (process lifetime)
    bool hold;                    // the guest's hold lease is live
    std::uint32_t hold_left_ms;
    std::uint16_t grant;          // the granted vehicle while live (0xFFFF none)
    std::uint32_t grant_left_ms;
    std::uint32_t holds;          // answers refused by the hold (process lifetime)
    // Seats on (design 8.21 R6): the Fulton guard's vehicle and how long ago site C last marked it (0xFFFF /
    // 0xFFFFFFFF none): a ride icon it refused (a deny or a hold: only that vehicle's stock Fulton prompt is left on
    // E), or a door of the vehicle the peer drives on this copy (the passenger ride icon shows beside the stock Fulton
    // prompt of the vehicle the partner sits in). While it is fresh Lua withholds the Fulton action (S.guard).
    std::uint16_t guard;
    std::uint32_t guard_age_ms;
    // E press only: site D's last press (its vehicle, 0xFFFF none; how long ago it started), the press count (Lua
    // claims each new one), whether it is still held, and the presses that ran out without an answer.
    std::uint16_t press;
    std::uint32_t press_age_ms;
    std::uint32_t pseq;
    bool pend;
    std::uint32_t timeouts;
    // The host's own press commit (site B) while live (0xFFFF none).
    std::uint16_t commit;
    std::uint32_t commit_left_ms;
    // The last press that ended at site D: its count, vehicle, how (1 grant, 2 deny, 3 timeout, 4 peer: the partner's ride
    // came first and the passenger policy seated it) and the press-to-answer
    // ms; the host's press commits (site B) so far.
    std::uint32_t done_seq;
    std::uint16_t done_vehicle;
    std::uint8_t done_end;
    std::uint32_t done_wait_ms;
    std::uint32_t commits;
};
void ReadRace(RaceState& out) noexcept;
bool Installed() noexcept;
const char* InstallRefusal() noexcept;
// Both call sites still call our stubs (the driver checks it every tick).
bool PatchLive(const void* base) noexcept;
// Review R5: site B owns the two phase-1 immediates only while its call is ours. Once site B is no longer
// live, the driver tick calls this: both immediates go back to the stock 2 (true when one was written).
bool RestorePhaseOneIfBLost(const void* base) noexcept;
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
    std::uint32_t remap_refused;  // review R4: seat-1 steers refused (4) because the remap was stock
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
// Review R4: whether the remap is skipped (the live reader reads the image dword).
using RemapFn = bool (*)() noexcept;
void BindRemapForTest(RemapFn remap) noexcept;
std::uint8_t SiteAForTest(void* unit, const float* pos, std::uint8_t first, std::uint8_t last) noexcept;
std::uint8_t SiteBForTest(void* unit, const float* pos) noexcept;
// Review R1/R2: the pre-query 0x1245820(action, body, unit, id, char* seat) bound without an install.
using PrequeryFn = bool (*)(void* action, std::uint32_t body, void* unit, std::uint16_t id, char* seat);
void BindPrequeryForTest(PrequeryFn prequery) noexcept;
bool SiteCForTest(void* action, std::uint32_t body, void* unit, std::uint16_t id, char* seat) noexcept;
// E press only: the remote gate 0x114DB20(action, index) bound without an install; site D called directly.
using GateFn = bool (*)(void* action, std::uint32_t index);
void BindGateForTest(GateFn gate) noexcept;
bool SiteDForTest(void* action, std::uint32_t index) noexcept;
void ResetForTest() noexcept;
} // namespace detail

} // namespace mgscoop::partner_board
