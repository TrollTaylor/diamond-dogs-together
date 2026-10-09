#pragma once
// The partner's weapon switch and the generalized weapon gate
// (docs/native-puppet.md "Weapons", W4/W5), pure: no Win32 calls and no game
// memory (mgs_coop_core).
//
// Switching needs no pad bits. Each player instance has an equipment row
// *(Q+0xC0) + id*0x3A whose low nibble of +0x36 is its current slot; gun phase
// 0 (0x103AED0, every frame) compares it with the slot in hand rec+0x3D9 and
// sets rec+0x3C0 bit 7 when they differ; with no gun mode active the same
// phase activates the slot's weapon at once (0x103E049 -> 0x105FDB0), while
// aiming the aim decider goes to mode 8, the native weapon-change sequence.
// The slot follower (partner_input_hook) writes instance 1's nibble the way
// SetCurrentSlot 0x11AFA80 does for a non-local id, only to a slot this plan
// admits: the body's slot must hold the remote's weapon of that slot (same) or
// a weapon of the same type (sub, the tier C substitute). Body 1 carries what
// the writers gave it: the stock46 writer only the back slot, the generalized
// writer (partner_native_equip=manifest|substitute) each slot a tier A1 or C
// target; an empty slot refuses (slot_empty).
#include <cstdint>

namespace mgscoop::partner_slots {
inline constexpr unsigned kSlots = 3;  // 0 hip primary, 1 back primary, 2 secondary (the remote's inv_slot 0..2)

// An inventory handle u32 [Y+0x40][id*stride + slot]: equip (handle >> 5) &
// 0x7FF, type handle & 0x1F; phase 0 (0x103AF7B) reads a handle with
// handle >> 16 == 0xFFFF as equip 0. A slot body 1 never had holds 0 (the
// stock46 writer's preflight requires all seven zero, and it bound at runtime),
// which decodes to equip 0 too: both are empty (equip 0 is no weapon).
struct Handle {
    bool filled;
    std::uint16_t equip;
    std::uint8_t type;
};
inline constexpr Handle Decode(std::uint32_t handle) noexcept {
    return ((handle >> 16) == 0xFFFFu || ((handle >> 5) & 0x7FFu) == 0) ? Handle{false, 0, 0}
                                     : Handle{true, static_cast<std::uint16_t>((handle >> 5) & 0x7FFu), static_cast<std::uint8_t>(handle & 0x1Fu)};
}

// same: the body's slot holds the remote's base id of that slot (the tier A1
// shape); sub: another weapon of the same type, or of the same slot class when
// both types belong to this slot (the writer's tier C); empty: nothing in the
// body's slot; other: a different type; unknown: no manifest or no type for
// the remote's weapon (the manifest's own type, version 2, counts also when
// no base id matched).
enum class Tier : std::uint8_t { unknown, same, sub, empty, other };
// ChangeEquip's slot of a gun type (equipment_stock_writer_contract::TypeSlot,
// same mapping): 1/5/7 hip (0), 4/6/8 back (1), 2/3 secondary (2), else 0xFF.
constexpr std::uint8_t TypeSlot(std::uint8_t type) noexcept {
    return (type == 1 || type == 5 || type == 7) ? 0u : (type == 4 || type == 6 || type == 8) ? 1u : (type == 2 || type == 3) ? 2u : 0xFFu;
}
const char* TierName(Tier tier) noexcept;
struct PlanIn {
    Handle body[kSlots];             // body 1's inventory handles (filled false when unread)
    bool body_ok;                    // the handles were read
    std::uint16_t remote_base[kSlots];  // the received manifest's base ids (0 unknown)
    std::uint8_t remote_type[kSlots];   // their type (the equip type table: packed & 0x3F)
    bool remote_type_ok[kSlots];
};
struct Plan {
    Tier tier[kSlots];
    std::uint16_t equip[kSlots];  // the body's equip the follower may switch to (0: refused)
    std::uint8_t ok_mask;         // bit s: the follower may switch to slot s
};
Plan MakePlan(const PlanIn& in) noexcept;

// The bullet weapon types (the RequestBullet owner filter suppresses their
// shots): 1 Assault, 2 Handgun, 3 Submachinegun, 4 Sniper, 5 Shotgun,
// 6 Machinegun. Projectiles (7 GrenadeLauncher, 8 Missile: shot class 3,
// gm+0x210) are not filtered, so FIRE stays masked for them and anything else.
bool BulletType(std::uint8_t type) noexcept;

// The generalized weapon gate (W5): FIRE and RELOAD reach body 1 only while
// its in-hand weapon is the remote's (raw equip or the manifest base of the
// remote's alias) and it is not mid-switch; FIRE additionally needs a bullet
// type. judged false: either side unread (the previous verdict stays).
enum class Gate : std::uint8_t { match, mismatch, switching, projectile };
const char* GateName(Gate gate) noexcept;
struct GateIn {
    std::uint32_t remote_equip, remote_base, body_equip;
    bool switching;    // body 1: rec+0x3C0 bit 7 or rec+0x2C0 == 8
    bool type_ok;      // body_type was read
    std::uint8_t body_type;
    // The body holds the substitute the plan admitted (tier sub) for the
    // remote's current slot (its live v5 slot, else the manifest slot carrying
    // its in-hand equip): a substitute in that slot is a match.
    bool substitute;
};
Gate Judge(const GateIn& in) noexcept;
// FIRE | RELOAD for mismatch and switching, FIRE for projectile, 0 for match.
std::uint32_t StripBits(Gate gate) noexcept;
} // namespace mgscoop::partner_slots
