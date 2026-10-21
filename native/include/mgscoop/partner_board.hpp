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
// 0x124B3A0 over the free back seats only). Their only native calls besides the
// original search are the two read-only getters GetVehicleType 0x55E7F0 and the
// seat count 0xA27CC0, under SEH (a fault blocks and is counted). Counters only:
// the driver logs them from its tick.
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
// reason: bytes (a pinned block differs), a_<RetargetCall reason>, b_failed
// (B refused, A reverted) or b_revert (B refused and A could not be reverted:
// its wrapper stays and passes through, the policy is never published). Logs
// partner_board=installed|refused once.
const char* Install(const void* base, std::size_t span, NowFn now, LogFn log) noexcept;
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
    std::uint32_t faults;         // SEH-caught index reads / getter calls
    const char* last_reason;      // the last non-stock decision's reason (static)
};
void ReadStats(Stats& out) noexcept;

namespace detail {
// The pinned blocks against an image of at least `span` bytes (the site A/B
// rel32 operands masked): nullptr or "span" / "contract" / "block_0x<rva>"-style
// static reasons ("bytes").
const char* VerifyImage(const void* base, std::size_t span, std::uintptr_t& mismatch_rva) noexcept;
// Fixture seams (never called in the game): the originals, the getters and the
// clock bound without patching anything; the wrappers called directly.
using NearestFn = std::uint8_t (*)(void* unit, const float* pos, std::uint8_t first, std::uint8_t last);
using DriverFn = std::uint8_t (*)(void* unit, const float* pos);
using TypeFn = std::uint8_t (*)(void* unit);
using CountFn = int (*)(void* unit);
void BindForTest(NearestFn nearest, DriverFn driver, TypeFn type, CountFn count, NowFn now) noexcept;
std::uint8_t SiteAForTest(void* unit, const float* pos, std::uint8_t first, std::uint8_t last) noexcept;
std::uint8_t SiteBForTest(void* unit, const float* pos) noexcept;
void ResetForTest() noexcept;
} // namespace detail

} // namespace mgscoop::partner_board
