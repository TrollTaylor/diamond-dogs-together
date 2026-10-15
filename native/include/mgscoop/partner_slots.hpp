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
    bool standin;                    // partner_native_standin=on (weapons4)
};
inline constexpr std::uint8_t kNoSlot = 0xFF;
struct Plan {
    Tier tier[kSlots];
    std::uint16_t equip[kSlots];  // the body's equip the follower may switch to (0: refused)
    std::uint8_t ok_mask;         // bit s: the follower may switch to slot s
    // Weapons4: the body slot the follower switches to while the remote holds
    // its slot r: r itself for same/sub, else (standin) the stand-in s* the
    // rule below picks, kNoSlot when none (the follower refuses: plan).
    std::uint8_t map[kSlots];
};
// The stand-in for the remote's slot r (weapons4): among body 1's other
// filled slots (s != r) holding a gun type, a bullet type (1..6) first, then
// both r and s primary ({0, 1}), then the lower s; a projectile slot (7/8:
// HOLD allowed, FIRE masked) only when no bullet slot is filled. kNoSlot
// when none. The body's handles must have been read.
std::uint8_t StandIn(const PlanIn& in, unsigned r) noexcept;
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
// standin (weapons4): body 1 holds the plan's stand-in for the remote's
// current slot (a bullet type: nothing stripped; a projectile stand-in is
// projectile: FIRE stripped, HOLD allowed).
// unsupported (anim round 1, A1; JudgeUnsupported below): the remote holds
// equipment body 1 cannot show (a support weapon, an item, the bionic arm, a
// shield): HOLD, FIRE and RELOAD are stripped so the partner no longer raises
// its old gun while the remote aims a grenade. Computed by the driver before
// Judge; it wins over every other verdict.
enum class Gate : std::uint8_t { match, mismatch, switching, projectile, standin, unsupported, support };
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
    // Weapons4: the body holds the plan's stand-in for that slot (its tier
    // neither same nor sub, map[r] != r, the body's equip == the plan's equip
    // of map[r]; partner_native_standin=on).
    bool standin;
};
Gate Judge(const GateIn& in) noexcept;
// FIRE | RELOAD for mismatch and switching, FIRE for projectile, HOLD | FIRE |
// RELOAD (0x58) for unsupported, 0 for match and standin.
std::uint32_t StripBits(Gate gate) noexcept;

// --- Anim round 1 (docs/anim-round1-weapons.md) ----------------------------------
// A1, the unsupported-equipment gate: the remote's live v5 slot (the sender's
// equip-row nibble) above the gun slots (ChangeEquip 0x11AC4AA..0x11AC5AF:
// 12 Throwing / 13 Placed -> 3, 17 Item -> 4, 14 AttackArm -> 6), or its
// in-hand equip (input frame v4, rec+0x24C of the sender's gun record; an
// alias as the manifest base) of a type outside the gun and shield types 1..9
// (12 Throwing, 13 Placed, 14 AttackArm, 17 Item, any type above 9; a shield
// keeps the projectile verdict: FIRE stripped). An unread type and an unknown
// slot never judge unsupported (the old verdicts stay).
enum class Unsupported : std::uint8_t { no, slot, type };
const char* UnsupportedName(Unsupported value) noexcept;
struct UnsupportedIn {
    bool slot_valid;      // the remote's live v5 slot (0..15)
    std::uint8_t slot;
    bool type_ok;         // the remote's in-hand equip type was read (equip type table)
    std::uint8_t type;
};
Unsupported JudgeUnsupported(const UnsupportedIn& in) noexcept;

// The remote's aim-family acts (Q+0x18; action-anims act table): 11/12 stand
// aim idle/turn, 13 stand aim move, 14/15 crouch aim idle/turn, 16 crouch aim
// move, 17/19/20 prone and supine aim. Act 18 is not an aim state.
inline constexpr bool AimAct(std::uint16_t act) noexcept { return (act >= 11 && act <= 17) || act == 19 || act == 20; }
// Remote acts in which a synthesized HOLD is refused: dead/dying 40/41,
// carried 42..44, the life/down family 61/63, knockdown and recovery 75/76,
// stun/faint 79..83, down 84..89, helicopter 97..108, vehicle 109..112 and
// 121, horse 122, D-Walker 123..126, sleep 131..133, direct motion 139.
bool AimBlockedAct(std::uint16_t act) noexcept;

// State-driven aim intent (critic P0.5/4.6; partner_native_aim_intent). The
// remote's pad HOLD reaches body 1 as before; with state the native window
// also injects HOLD (into the sample's held word, so the edge and the PP+0x27C
// latch follow the level exactly like a held RMB) while the remote's v5 gun
// group says Aiming or its act is an aim act, independent of the remote's
// toggle/hold aim option. The driver's gate (AimGate, per tick) and the hook's
// frame rule (AimFrame, per window) both must pass; the frame rule reads the
// sample of that window, so HOLD drops in the first window after the pad bit,
// the flag and the act all cleared (the act through the next Arm).
enum class AimRefusal : std::uint8_t { none, off, gate, slot, sleep, carried, hidden, ride, act };
const char* AimRefusalName(AimRefusal value) noexcept;
struct AimGateIn {
    bool enabled;                // partner_native_aim_intent=state with the native gun stage active
    bool judged;                 // the weapon gate judged this tick (weapon_gate is current)
    Gate gate;                   // match or standin admit
    bool slot_valid;             // the remote's live v5 slot
    std::uint8_t slot;           // <= 2
    bool sleeping;               // the partner sleep owner holds body 1 (coop_sleep)
    bool carried;                // instance 1 is carried (coop_carry)
    bool hidden;                 // the ride guard hides body 1 (ride, air, dead, stale)
    bool riding;                 // the remote's ride state is not on foot (ride wire)
    bool act_valid;              // the remote's live act
    std::uint16_t act;
};
AimRefusal AimGate(const AimGateIn& in) noexcept;
enum class AimSource : std::uint8_t { none, pad, flag, act };
const char* AimSourceName(AimSource value) noexcept;
struct AimFrameIn {
    bool gate_ok;      // the driver's AimGate passed (Arm)
    bool live;         // the window's sample is live (age <= stale_ms)
    bool pad_hold;     // its held word carries HOLD (the remote's own press: nothing to add)
    bool v5_valid;     // its v5 gun group is valid
    bool v5_aiming;    // GunFlag::Aiming
    bool act_aim;      // the driver's remote act is an aim act (Arm, expires)
};
// pad first (the raw HOLD, never added), then the v5 flag, then the act;
// none without the gate or a live sample.
AimSource AimFrame(const AimFrameIn& in) noexcept;
inline constexpr bool AimAdds(AimSource source) noexcept { return source == AimSource::flag || source == AimSource::act; }

// Auto-fire stop (critic P0.4, weapon-anims A3): instance 1's non-local shots
// never spend its magazine and the mirror shows the remote's last round as 1,
// so a held automatic trigger kept firing visual rounds after the remote ran
// dry. Dry: the remote's v4 magazine has read 0 for grace_ms (the sample that
// spent the last round still delivers its FIRE edge) and the remote is not
// reloading; FIRE is stripped until a reload starts or the magazine reads
// above 0 (a new magazine value: a reload's refill, a switch, a pickup).
struct DryIn {
    std::uint64_t now_ms;
    bool mag_valid;            // the live sample carries v4 Mag
    std::uint16_t mag;
    bool remote_reloading;     // v5 Reloading or mode 11/12
};
struct DryState {
    bool dry;
    std::uint64_t zero_ms;     // when the magazine first read 0 (0: not zero)
    std::uint32_t stops, clears;
};
inline constexpr std::uint32_t kDryGraceMs = 50;
// True while FIRE must be stripped.
bool DryStep(DryState& state, const DryIn& in, std::uint32_t grace_ms = kDryGraceMs) noexcept;
} // namespace mgscoop::partner_slots
