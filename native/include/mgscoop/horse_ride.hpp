#pragma once
// Step S3 (horse plan M5, docs/mounts.md section 19): the RideHorse action's per-player ride record, read for the logs and,
// with the lab key world_horse_down_off, the forced get-off of this copy's own rider when it is downed while riding.
// Static [I] (Ghidra comments at 0x14122EB30 / 0x14122F3F0 / 0x14122DA20): a player block keeps its actions in two 0x45-slot
// arrays (+0x190, +0x3B8); the RideHorse action (vtable 0x1423CB600, the factory returns obj+0x10) has +8 its block
// (block+0x138 = P), +0x38 an id node (+0x24 the base id), +0x68 the records (0x2B0 each). A record: +0x10 the state (an
// index into the state table 0x14122EB30: 3 the get-off, 5 the ride, ...), +0x11 the next state (the update 0x14122DDA0
// enters it when non-zero and it differs from +0x10 or +0x235 is set), +0x18 the horse-side sub record (+0x7C the rider id,
// +0x82 the get-off selector, +0x16E a forced get-off flag whose writer is unknown). The stock get-off (E, or +0x16E) in
// the ride states sets sub+0x82 = 6 when sub+0x7C names the rider, then +0x11 = 3 and +0x235 = 0; getoff writes the same.
// A downed rider never gets off by itself (step S: the downed cycle waits for a prone stance a mounted body never takes,
// and its ACTION mask hides E). Contract: native/horse_ride_contract.hpp (tools/generate_horse_ride_contract.py).
#include <cstddef>
#include <cstdint>

namespace mgscoop::horse_ride {

inline constexpr std::uint8_t kGetOffState = 3, kRideState = 5;

struct Rec {
    bool ok{};
    std::uintptr_t plugin{}, rec{}, sub{};
    std::uint8_t state{}, next{}, again{}, select{}, forced{};
    std::uint16_t rider{};
};

// The states whose update runs the stock get-off check 0x14122F3F0 (its xrefs: 5, 6 / 7, 8, 9, 10..19, 29).
bool GetOffState(std::uint8_t state) noexcept;
// A getoff's refusal in order (nullptr: write): unread, idle (state 0), state (not a riding state), pending (a next state
// already set).
const char* GetOffRefusal(const Rec& r) noexcept;
// The rrec log: a record worth a line (first read, or any field changed).
bool Changed(const Rec& was, const Rec& now) noexcept;
// Lua's getoff trigger (lua/coop/world_horse_share.lua): this copy's player rides the shared horse and is downed, the key
// named, at most kGetOffTries requests kGetOffGapMs apart per downed episode.
inline constexpr unsigned kGetOffTries = 6;
inline constexpr std::uint32_t kGetOffGapMs = 500;

// Live (native/horse_ride.cpp): every site and slot of the contract compared in memory (nullptr: they hold).
const char* PinsReason(std::uintptr_t image, std::size_t span) noexcept;
// The RideHorse action in `block` (its two action arrays; the object's vtable, its block and block+0x138 == player
// checked); 0 when none.
std::uintptr_t FindPlugin(std::uintptr_t image, std::uintptr_t block, std::uintptr_t player) noexcept;
// Player `id`'s record of `plugin` (id - the node's base id < id_count); nullptr or a refusal (node, index, record).
const char* Read(std::uintptr_t plugin, std::uint32_t id, std::uint32_t id_count, Rec& out) noexcept;
// The stock get-off's writes on `r` (re-read first: nullptr or a refusal from GetOffRefusal / Read / "write").
const char* ForceGetOff(std::uintptr_t plugin, std::uint32_t id, std::uint32_t id_count, Rec& r) noexcept;

} // namespace mgscoop::horse_ride
