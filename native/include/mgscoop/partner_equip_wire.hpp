#pragma once
// Native puppet W2 preparation (docs/native-puppet.md 6.2): the equipment
// manifest one copy sends its partner on reliable channel 2, on change and
// after a new link session, from any mission (the title and the ACC too: the
// first mission after connecting preloads the remote's weapons). Pure codec
// (mgs_coop_core); the receiver stores it for the generalized preload, the
// writer and the slot plan.
//
//   [0] 0x45 'E'  [1] version (2; 1 still decodes)  [2..3] revision u16 (never 0)
//   [4..7] epoch u32 (the SENDER's partner_net link-session number: identity
//          only. Each process counts its own sessions, so after a relaunch of
//          one copy the two counters differ for good (0.13.5 online session
//          2026-10-22: host 3, guest 1). The receiver never compares it with its
//          own counter; it binds what it stores to its OWN link session, the
//          one the message was drained in (Store::link_epoch). The transport
//          guarantees freshness: every new link session resets the reliable
//          windows and the drain yields only the current session's messages.)
//   [8] inv_slot (0..15, 0xFF unknown)  [9] slot_count = 3
//   [10..11] active_item_id u16 (version 2; 0 none). Version 1 keeps these 0.
//   then 3 slots x 16 bytes (hip, back, secondary):
//   [0..1] base_id u16 (a stock gun id, 0 unknown: never a non-gun id)
//   [2..3] alias u16 (weapons[slot] equip id)
//   [4..15] recipe[12] (chimeraParts[i*12..+12]; byte 0 = receiver)
//   version 2 only: [60..62] type[3] (the slot's own gun type 1..8 from the
//   game's tables, 0 unknown; also known when no base id matched),
//   [63] bit 0 = cbox_active (the remote is in a cardboard box). Other bits 0.
//   EQP_IT_CBox is 0x1C8. IsVarsCurrentItemCBox (0x9D68E0) treats
//   id - 0x1C8 < 0x1B as a box (0x1C8..0x1E2), from RegisterConstantEquipId.
// Version 1 is 60 bytes (types unknown), version 2 64 bytes; little-endian.
// One message type on the channel (the capability message, first byte 0x43,
// is told apart by its magic).
#include <cstddef>
#include <cstdint>

namespace mgscoop::partner_equip_wire {

inline constexpr std::uint8_t kMagic = 0x45;
inline constexpr std::uint8_t kVersion1 = 1;
inline constexpr std::uint8_t kVersion = 2;
inline constexpr std::size_t kSlots = 3;
inline constexpr std::size_t kRecipe = 12;
inline constexpr std::size_t kHeader = 12;
inline constexpr std::size_t kSlotSize = 16;
inline constexpr std::size_t kSizeV1 = kHeader + kSlots * kSlotSize;  // 60
inline constexpr std::size_t kTypes = kSizeV1;                         // type[3] at 60..62, [63] reserved
inline constexpr std::size_t kSize = kSizeV1 + 4;                      // 64
inline constexpr std::uint8_t kInvSlotUnknown = 0xFF;
inline constexpr std::uint8_t kInvSlotMax = 15;
inline constexpr std::uint8_t kChannel = 2;  // partner_net reliable channel (1 = world)
inline constexpr std::uint8_t kMaxGunType = 8;  // gun types 1..8 (1 AR .. 8 missile)
inline constexpr bool GunType(std::uint32_t type) noexcept { return type >= 1 && type <= kMaxGunType; }
// Cardboard boxes (1.0.15.4): EQP_IT_CBox = 0x1C8, and every id in
// [0x1C8, 0x1E2] (IsVarsCurrentItemCBox: id - 0x1C8 < 0x1B).
inline constexpr std::uint16_t kCboxBase = 0x1C8;
inline constexpr std::uint16_t kCboxSpan = 0x1B;
inline constexpr bool CboxEquip(std::uint16_t id) noexcept {
    return static_cast<std::uint16_t>(id - kCboxBase) < kCboxSpan;
}

struct Slot {
    std::uint16_t base_id;
    std::uint16_t alias;
    std::uint8_t recipe[kRecipe];
    std::uint8_t type;  // 0 unknown (a version 1 sender), else 1..8
};
struct Manifest {
    std::uint16_t revision;
    std::uint32_t epoch;
    std::uint8_t inv_slot;
    std::uint8_t version;  // the decoded version (Encode always writes kVersion)
    bool cbox_active;             // version 2 byte 63 bit 0 (version 1: false)
    std::uint16_t active_item_id; // version 2 bytes 10..11 (version 1: 0)
    Slot slots[kSlots];
};
// The three gun slots, field-wise (no padding compared). The box fields are
// not compared: a box toggle must not re-drive the receiver's gun writer.
bool SameSlots(const Manifest& a, const Manifest& b) noexcept;
// The cardboard-box fields (cbox_active, active_item_id).
inline bool SameBox(const Manifest& a, const Manifest& b) noexcept {
    return a.cbox_active == b.cbox_active && a.active_item_id == b.active_item_id;
}

// kSize (version 2), or 0 (out zeroed) for revision 0, epoch 0, an inv_slot
// outside 0..15/0xFF or a type outside 0..8.
std::size_t Encode(const Manifest& manifest, std::uint8_t (&out)[kSize]) noexcept;

// no_link: the receiver had no link session to bind the message to (Accept).
enum class Result : std::uint8_t { ok, size, magic, version, revision, epoch, stale_epoch, inv_slot, slot_count, reserved, type, no_link };
const char* ResultName(Result result) noexcept;
// Strict decode of version 1 (60 bytes) or 2 (64 bytes); current_epoch != 0
// drops other epochs (stale_epoch). A strict helper only: Accept passes 0
// (the sender's epoch is never compared with the receiver's own counter).
Result Decode(const std::uint8_t* data, std::size_t size, std::uint32_t current_epoch, Manifest& out) noexcept;
// The revision and sender epoch fields of any message of at least 8 bytes
// (for the refusal log; 0 when shorter). No validation.
void PeekHeader(const std::uint8_t* data, std::size_t size, std::uint16_t& revision, std::uint32_t& epoch) noexcept;

// Receiver store (driver-owned; one per mission). Counters for telemetry.
// manifest.epoch is the sender's session number (identity only); link_epoch
// is the RECEIVER's own link session the stored manifest was drained in.
// last: the result of the last refused message (ok for a duplicate).
struct Store {
    bool have;
    Manifest manifest;
    std::uint32_t link_epoch;
    std::uint32_t received, invalid, stale, duplicates;
    Result last;
};
// Applies one message drained in the receiver's own link session `link_epoch`:
// ok + a new revision or new content stores it, stamped with link_epoch
// (true); anything else only counts (link_epoch 0: no_link). A duplicate
// repeats the stored revision, sender epoch, version, inv_slot, slots and box
// fields IN THE SAME link session: a new link session is never a duplicate
// (a guest relaunched on the same save sends the identical revision 1 again).
// The same revision with other content (a 0.12.0 sender restarts at revision
// 1 every mission) replaces it.
bool Accept(Store& store, const std::uint8_t* data, std::size_t size, std::uint32_t link_epoch) noexcept;

// --- The sender's base ids (docs/native-puppet.md "Loadout mirroring"), pure.
// The equip type table holds one packed u16 per table index (type packed &
// 0x3F, GunBasic row packed >> 6); A2A3E0 maps equip ids to indices: ids
// below 0x230 index themselves, ids 0x400..0x44F index 0x230..0x27F and ids
// 0x600.. index 0x280.. (0xA2A48F sub 0x1D0, 0xA2A478 sub 0x380).
inline constexpr std::uint16_t TableIndexToId(std::uint32_t index) noexcept {
    return static_cast<std::uint16_t>(index < 0x230 ? index : index < 0x280 ? index + 0x1D0 : index + 0x380);
}
// A customized slot's stock base: the first table index whose packed type is
// the gun type `type` (1..8, the alias's own type) and whose GunBasic row
// (1..rows) has `receiver` as byte 0 (receivers[row - 1]), as an equip id;
// 0 when nothing matches, the type is not a gun type or the receiver is 0.
std::uint16_t BaseFromTables(const std::uint16_t* packed, std::size_t count, const std::uint8_t* receivers, std::size_t rows,
                             std::uint8_t type, std::uint8_t receiver) noexcept;

} // namespace mgscoop::partner_equip_wire
