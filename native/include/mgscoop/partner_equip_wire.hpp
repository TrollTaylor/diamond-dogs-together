#pragma once
// Native puppet W2 preparation (docs/native-puppet.md 6.2): the equipment
// manifest one copy sends its partner on reliable channel 2 (latest mode),
// on change and after a new link session. Pure codec (mgs_coop_core); the
// receiver only stores and reports it (binding is W2 research).
//
//   [0] 0x45 'E'  [1] version 1  [2..3] revision u16 (never 0)
//   [4..7] epoch u32 (the partner_net link session; stale epochs dropped)
//   [8] inv_slot (0..15, 0xFF unknown)  [9] slot_count = 3  [10..11] 0
//   then 3 slots x 16 bytes (hip, back, secondary):
//   [0..1] base_id u16 (customizedWeapon[i])  [2..3] alias u16 (weapons[slot] equip id)
//   [4..15] recipe[12] (chimeraParts[i*12..+12]; byte 0 = receiver)
// Total 60 bytes, little-endian.
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_equip_wire {

inline constexpr std::uint8_t kMagic = 0x45;
inline constexpr std::uint8_t kVersion = 1;
inline constexpr std::size_t kSlots = 3;
inline constexpr std::size_t kRecipe = 12;
inline constexpr std::size_t kHeader = 12;
inline constexpr std::size_t kSlotSize = 16;
inline constexpr std::size_t kSize = kHeader + kSlots * kSlotSize;  // 60
inline constexpr std::uint8_t kInvSlotUnknown = 0xFF;
inline constexpr std::uint8_t kInvSlotMax = 15;
inline constexpr std::uint8_t kChannel = 2;  // partner_net reliable channel (1 = world)

struct Slot {
    std::uint16_t base_id;
    std::uint16_t alias;
    std::uint8_t recipe[kRecipe];
};
struct Manifest {
    std::uint16_t revision;
    std::uint32_t epoch;
    std::uint8_t inv_slot;
    Slot slots[kSlots];
};

// kSize, or 0 (out zeroed) for revision 0, epoch 0 or an inv_slot outside 0..15/0xFF.
std::size_t Encode(const Manifest& manifest, std::uint8_t (&out)[kSize]) noexcept;

enum class Result : std::uint8_t { ok, size, magic, version, revision, epoch, stale_epoch, inv_slot, slot_count, reserved };
const char* ResultName(Result result) noexcept;
// Strict decode; current_epoch != 0 drops other epochs (stale_epoch).
Result Decode(const std::uint8_t* data, std::size_t size, std::uint32_t current_epoch, Manifest& out) noexcept;

// Receiver store (driver-owned; one per mission). Counters for telemetry.
struct Store {
    bool have;
    Manifest manifest;
    std::uint32_t received, invalid, stale, duplicates;
};
// Applies one drained message: ok + new revision stores it (true), anything
// else only counts. A revision equal to the stored one is a duplicate.
bool Accept(Store& store, const std::uint8_t* data, std::size_t size, std::uint32_t current_epoch) noexcept;

} // namespace mgscoop::partner_equip_wire
