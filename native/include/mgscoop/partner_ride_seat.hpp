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
// Seat 1 caveat (0xB79A3F..0xB79A57): 0xB799C0 maps seat 1 to seat 0 only when
// rider[1] (word unit+0x32) holds a player id ((word & 0xFE00) == 0); on an
// empty receiver copy (rider[1] == 0xFFFF) the anchor reads seat 1's own matrix,
// where the player's FrontRight clips (authored on the driver's matrix) drew body 1
// one seat further right, outside the jeep: Arm writes AnchorSeat(seat), 0 for seat
// 1 (design 8.33). Nothing here writes rider[] (re-vehicle.md 3.4).
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
// The seat whose matrix the anchor reads for a seated `seat`: seat 1 -> 0, the others unchanged.
std::uint8_t AnchorSeat(std::uint8_t seat) noexcept;

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

// --- Seat by id (WS3 B1, docs/vehicle-ws3.md): an unnamed vehicle (the
// sortie/deployed 25600..25607) has no locator, so Lua plays the same seat
// chain untargeted (locator "", still CNP_ppos_c / MTP_GLOBAL_C). The binding
// then stores target 0xFFFF and no flag 2 (0x9DAAE1..0x9DAB2A); a named
// locator would have stored the id and flag 2. Once the seat DM is live on
// body 1, the driver writes exactly those two fields into body 1's DM record:
// every chained entry's target id first (rec+0xC0+2i, read by the phase-0
// anchor 0x11987A1), then flag 2 (rec+0xB8+i, read by phase 6 0x1199508), on
// the player-update thread so no frame sees the flag before the id.
inline constexpr std::uint64_t kCnpPposC = 0x2ccada28e251ull;  // CNP_ppos_c, low 48 bits (0x1198821)
inline constexpr std::uint8_t kEntryFlagTarget = 2, kEntryFlagState = 8, kEntryMax = 8;
struct EntryView {
    std::uint8_t flags[kEntryMax];
    std::uint16_t ids[kEntryMax];
    std::uint64_t cnp[kEntryMax];
    std::uint64_t state[kEntryMax];
    std::uint8_t cur;     // rec+0x1A0
    std::uint64_t rec18;  // the playing state (rec+0x18)
};
struct TargetPlan {
    const char* refusal;  // nullptr: the plan holds
    bool write;           // false: every entry is already targeted at `vehicle` (a named locator)
    std::uint8_t entries;
};
// The chain Lua played has `entries` (1..3) entries; every one must carry
// CNP_ppos_c and a named seated state of `seat` (flag 8, rec+0x78+8i), and
// either all are already targeted at `vehicle` with flag 2 (named: nothing to
// write) or all are untargeted (0xFFFF, no flag 2: write). Anything else is
// refused (foreign_target, cnp, entry_state, ...).
TargetPlan PlanSeatTarget(const EntryView& view, std::uint16_t vehicle, std::uint8_t seat, std::uint8_t entries) noexcept;
// The write itself, through `store` (the game: interlocked 16-bit exchanges and
// byte ORs on body 1's live DM record; tests: a recorder): every entry's target
// id first, then every entry's flag 2, so no frame anchors on a flag without
// its id. False at the first failed store (the caller reads back anyway).
inline constexpr std::size_t kEntryState = 0x78, kEntryFlags = 0xB8, kEntryTarget = 0xC0, kEntryCnp = 0xD0, kEntryCur = 0x1A0;
template <class Store>
bool ApplySeatTarget(Store& store, std::uintptr_t rec, std::uint16_t vehicle, std::uint8_t entries) noexcept {
    for (unsigned i = 0; i < entries && i < kEntryMax; ++i)
        if (!store.Word(rec + kEntryTarget + 2u * i, vehicle)) return false;
    for (unsigned i = 0; i < entries && i < kEntryMax; ++i)
        if (!store.OrByte(rec + kEntryFlags + i, kEntryFlagTarget)) return false;
    return true;
}
// The number of chained entries for RIDE_SEAT entry bits (get_on 1, get_off 2).
inline std::uint8_t ChainEntries(std::uint8_t entry_bits) noexcept {
    return static_cast<std::uint8_t>(1u + (entry_bits & 1u) + ((entry_bits >> 1) & 1u));
}

} // namespace mgscoop::partner_ride_seat
