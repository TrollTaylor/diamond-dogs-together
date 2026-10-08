#pragma once
// PlayerPad button words (PP vt+0x20 held, +0x30 now, +0x40 press, +0x58
// release; docs/native-puppet.md "Pad bits"). Values from the Lua PlayerPad
// enum registration 0x9D3A78..0x9D3EE5 (0x1A156E0(table, name, value) per
// entry; tools/generate_partner_contract.py pins every pair used here, string
// RVA and value). Keyboard bit i is key binding i: the PP update ORs 1 << i
// into PP+0x460..0x46C for the handle PP+0x390[i] (0x1206C21..0x1206CF2).
// Pure constants, no game access.
#include <cstdint>

namespace mgscoop::partner_pad {

// Movement (the native drive's base mask).
inline constexpr std::uint32_t kStance = 0x2u;         // STANCE (C; hold = prone)
inline constexpr std::uint32_t kDash = 0x4u;           // DASH (Shift; while prone: roll)
inline constexpr std::uint32_t kEvade = 0x40000u;      // EVADE (Space): press -> act 27 (dive / evade / roll)
inline constexpr std::uint32_t kKbDash = 0x200000u;    // VEHICLE_DASH = keyboard binding 21 (W): vehicles only
inline constexpr std::uint32_t kKbWalk = 0x2000000u;   // keyboard binding 25 (Ctrl, walk): no enum name
// Gun (partner_native_gun / _fire).
inline constexpr std::uint32_t kHold = 0x8u;           // HOLD (RMB, aim)
inline constexpr std::uint32_t kFire = 0x10u;          // FIRE (LMB while aiming)
inline constexpr std::uint32_t kReload = 0x40u;        // RELOAD = CARRY (R): also carries a body
// Never passed: aliases that act on the receiver's world or UI.
inline constexpr std::uint32_t kDecide = 0x1u;
inline constexpr std::uint32_t kAction = 0x20u;        // ACTION = MOVE_ACTION = JUMP = RIDE_ON/OFF = CQC_KNIFE_KILL (E)
inline constexpr std::uint32_t kStock = 0x80u;         // STOCK = ZOOM_CHANGE (V)
inline constexpr std::uint32_t kMbDevice = 0x100u;
inline constexpr std::uint32_t kCall = 0x200u;         // CALL = INTERROGATE (Q)
inline constexpr std::uint32_t kSubject = 0x400u;      // SUBJECT (F): binoculars, marking
inline constexpr std::uint32_t kWeaponBits = 0x7800u;  // UP/PRIMARY, DOWN/SECONDARY, LEFT, RIGHT
inline constexpr std::uint32_t kCqc = 0x8000u;
inline constexpr std::uint32_t kSideRoll = 0x10000u;   // no consumer in the player code (inert)
inline constexpr std::uint32_t kLightSwitch = 0x20000u;
inline constexpr std::uint32_t kVehicleFire = 0x80000u;
inline constexpr std::uint32_t kVehicleCall = 0x100000u;
inline constexpr std::uint32_t kPlaceMarker = 0x400000u;

// Bits whose every consumer only moves or poses the pad owner's own body.
inline constexpr std::uint32_t kMovementOnlyBits = kStance | kDash | kEvade | kKbDash | kKbWalk;
// The native drive's movement base (lab_config kPartnerNativeBaseMask).
inline constexpr std::uint32_t kNativeBase = kStance | kDash | kEvade | kKbDash | kKbWalk;
inline constexpr std::uint32_t kNativeGun = kHold | kReload;
inline constexpr std::uint32_t kWorldBits = kDecide | kFire | kAction | kStock | kMbDevice | kCall | kSubject | kWeaponBits | kCqc |
                                            kLightSwitch | kVehicleFire | kVehicleCall | kPlaceMarker;

static_assert(kNativeBase == 0x2240006u, "native movement base");
static_assert((kNativeBase & ~kMovementOnlyBits) == 0u, "the native base passes movement-only bits");
static_assert((kNativeGun & kMovementOnlyBits) == 0u && (kFire & kMovementOnlyBits) == 0u, "gun/fire bits stay out of the movement set");
static_assert((kMovementOnlyBits & kWorldBits) == 0u, "no world/UI alias in the movement set");
static_assert((kAction & kNativeBase) == 0u, "ACTION (ride/knife kill/pickup) is never in the base");

} // namespace mgscoop::partner_pad
