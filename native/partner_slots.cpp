#include "mgscoop/partner_slots.hpp"
#include "mgscoop/partner_native.hpp"
#include "mgscoop/partner_pad_bits.hpp"

namespace mgscoop::partner_slots {

const char* TierName(Tier tier) noexcept {
    switch (tier) {
    case Tier::same: return "same";
    case Tier::sub: return "sub";
    case Tier::empty: return "empty";
    case Tier::other: return "other";
    default: return "unknown";
    }
}

std::uint8_t StandIn(const PlanIn& in, unsigned r) noexcept {
    if (!in.body_ok || r >= kSlots) return kNoSlot;
    std::uint8_t best = kNoSlot;
    unsigned best_rank = ~0u;
    for (unsigned s = 0; s < kSlots; ++s) {
        const Handle& h = in.body[s];
        if (s == r || !h.filled || h.type < 1 || h.type > 8) continue;
        // Lower is better: a projectile last, then a primary pair, then the slot.
        const bool primary_pair = r <= 1 && s <= 1;
        const unsigned rank = (BulletType(h.type) ? 0u : 8u) | (primary_pair ? 0u : 4u) | s;
        if (rank < best_rank) {
            best_rank = rank;
            best = static_cast<std::uint8_t>(s);
        }
    }
    return best;
}

Plan MakePlan(const PlanIn& in) noexcept {
    Plan p{};
    for (unsigned s = 0; s < kSlots; ++s) p.map[s] = kNoSlot;
    for (unsigned s = 0; s < kSlots; ++s) {
        const Handle& h = in.body[s];
        Tier t = Tier::unknown;
        if (!in.body_ok) t = Tier::unknown;
        else if (!h.filled) t = Tier::empty;
        else if (in.remote_base[s] != 0 && h.equip == in.remote_base[s]) t = Tier::same;
        else if (!in.remote_type_ok[s] || in.remote_type[s] == 0) t = Tier::unknown;
        else if (h.type == in.remote_type[s]) t = Tier::sub;
        // The writer's slot class: both types belong to this slot.
        else if (TypeSlot(h.type) == s && TypeSlot(in.remote_type[s]) == s) t = Tier::sub;
        else t = Tier::other;
        p.tier[s] = t;
        if (t == Tier::same || t == Tier::sub) {
            p.equip[s] = h.equip;
            p.ok_mask = static_cast<std::uint8_t>(p.ok_mask | (1u << s));
            p.map[s] = static_cast<std::uint8_t>(s);
        }
    }
    // Weapons4: a remote slot body 1 cannot follow (empty, other, unknown)
    // maps to the stand-in; the follower may switch to it (ok_mask) with the
    // body's own equip of that slot.
    if (in.standin) {
        for (unsigned r = 0; r < kSlots; ++r) {
            if (p.map[r] != kNoSlot) continue;
            const std::uint8_t s = StandIn(in, r);
            if (s >= kSlots) continue;
            p.map[r] = s;
            p.ok_mask = static_cast<std::uint8_t>(p.ok_mask | (1u << s));
            if (!p.equip[s]) p.equip[s] = in.body[s].equip;
        }
    }
    return p;
}

// The bullet categories (EquipIdTable EQP_TYPE_* joined with the runtime type
// table of the H2 capture: 1 Assault, 2 Handgun, 3 Submachinegun, 4 Sniper,
// 5 Shotgun, 6 Machinegun); 7 GrenadeLauncher and 8 Missile fire projectiles
// (shot class 3, gm+0x210, not filtered) and stay masked, as does any type
// above 8. Their shots go through RequestBullet, which the owner filter drops.
bool BulletType(std::uint8_t type) noexcept { return type >= 1 && type <= 6; }

const char* GateName(Gate gate) noexcept {
    switch (gate) {
    case Gate::mismatch: return "mismatch";
    case Gate::switching: return "switching";
    case Gate::projectile: return "projectile";
    case Gate::standin: return "standin";
    case Gate::unsupported: return "unsupported";
    default: return "match";
    }
}

Gate Judge(const GateIn& in) noexcept {
    const bool own = in.remote_equip == in.body_equip || in.remote_base == in.body_equip || in.substitute;
    if (!own && !in.standin) return Gate::mismatch;
    if (in.switching) return Gate::switching;
    if (!in.type_ok || !BulletType(in.body_type)) return Gate::projectile;
    return own ? Gate::match : Gate::standin;
}

std::uint32_t StripBits(Gate gate) noexcept {
    switch (gate) {
    case Gate::mismatch:
    case Gate::switching: return partner_native::kPadFire | partner_native::kPadReload;
    case Gate::projectile: return partner_native::kPadFire;
    case Gate::unsupported: return partner_pad::kHold | partner_native::kPadFire | partner_native::kPadReload;
    default: return 0u;
    }
}

const char* UnsupportedName(Unsupported value) noexcept {
    switch (value) {
    case Unsupported::slot: return "slot";
    case Unsupported::type: return "type";
    default: return "no";
    }
}

Unsupported JudgeUnsupported(const UnsupportedIn& in) noexcept {
    // The live slot first: 3 support, 4 item, 6 hand (any nibble above the gun slots).
    if (in.slot_valid && in.slot > 2 && in.slot <= 15) return Unsupported::slot;
    // A type ChangeEquip puts outside the gun slots (12 Throwing, 13 Placed ->
    // 3, 14 AttackArm -> 6, 17 Item -> 4, any other type above 9). 9 Shield sits
    // in the back slot with the secondary gun in hand: the projectile verdict
    // (FIRE stripped) keeps covering it.
    if (in.type_ok && in.type > 9) return Unsupported::type;
    return Unsupported::no;
}

bool AimBlockedAct(std::uint16_t act) noexcept {
    return act == 40 || act == 41 || (act >= 42 && act <= 44) || act == 61 || act == 63 || act == 75 || act == 76 ||
           (act >= 79 && act <= 89) || (act >= 97 && act <= 112) || act == 121 || (act >= 122 && act <= 126) ||
           (act >= 131 && act <= 133) || act == 139;
}

const char* AimRefusalName(AimRefusal value) noexcept {
    switch (value) {
    case AimRefusal::none: return "none";
    case AimRefusal::gate: return "gate";
    case AimRefusal::slot: return "slot";
    case AimRefusal::sleep: return "sleep";
    case AimRefusal::carried: return "carried";
    case AimRefusal::hidden: return "hidden";
    case AimRefusal::ride: return "ride";
    case AimRefusal::act: return "act";
    default: return "off";
    }
}

AimRefusal AimGate(const AimGateIn& in) noexcept {
    if (!in.enabled) return AimRefusal::off;
    if (!in.judged || (in.gate != Gate::match && in.gate != Gate::standin)) return AimRefusal::gate;
    if (!in.slot_valid || in.slot > 2) return AimRefusal::slot;
    if (in.sleeping) return AimRefusal::sleep;
    if (in.carried) return AimRefusal::carried;
    if (in.hidden) return AimRefusal::hidden;
    if (in.riding) return AimRefusal::ride;
    if (in.act_valid && AimBlockedAct(in.act)) return AimRefusal::act;
    return AimRefusal::none;
}

const char* AimSourceName(AimSource value) noexcept {
    switch (value) {
    case AimSource::pad: return "pad";
    case AimSource::flag: return "flag";
    case AimSource::act: return "act";
    default: return "none";
    }
}

AimSource AimFrame(const AimFrameIn& in) noexcept {
    if (!in.live) return AimSource::none;
    if (in.pad_hold) return AimSource::pad;
    if (!in.gate_ok) return AimSource::none;
    if (in.v5_valid && in.v5_aiming) return AimSource::flag;
    if (in.act_aim) return AimSource::act;
    return AimSource::none;
}

bool DryStep(DryState& s, const DryIn& in, std::uint32_t grace_ms) noexcept {
    const bool was = s.dry;
    if (!in.mag_valid || in.remote_reloading || in.mag > 0) {
        s.zero_ms = 0;
        s.dry = false;
    } else {
        if (!s.zero_ms || in.now_ms < s.zero_ms) s.zero_ms = in.now_ms ? in.now_ms : 1;
        s.dry = in.now_ms >= s.zero_ms && in.now_ms - s.zero_ms >= grace_ms;
    }
    if (s.dry && !was) ++s.stops;
    if (!s.dry && was) ++s.clears;
    return s.dry;
}

} // namespace mgscoop::partner_slots
