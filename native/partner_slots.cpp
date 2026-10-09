#include "mgscoop/partner_slots.hpp"
#include "mgscoop/partner_native.hpp"

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

Plan MakePlan(const PlanIn& in) noexcept {
    Plan p{};
    for (unsigned s = 0; s < kSlots; ++s) {
        const Handle& h = in.body[s];
        Tier t = Tier::unknown;
        if (!in.body_ok) t = Tier::unknown;
        else if (!h.filled) t = Tier::empty;
        else if (in.remote_base[s] == 0) t = Tier::unknown;
        else if (h.equip == in.remote_base[s]) t = Tier::same;
        else if (!in.remote_type_ok[s]) t = Tier::unknown;
        else if (h.type == in.remote_type[s]) t = Tier::sub;
        else t = Tier::other;
        p.tier[s] = t;
        if (t == Tier::same || t == Tier::sub) {
            p.equip[s] = h.equip;
            p.ok_mask = static_cast<std::uint8_t>(p.ok_mask | (1u << s));
        }
    }
    return p;
}

bool BulletType(std::uint8_t type) noexcept { return type == 4; }

const char* GateName(Gate gate) noexcept {
    switch (gate) {
    case Gate::mismatch: return "mismatch";
    case Gate::switching: return "switching";
    case Gate::projectile: return "projectile";
    default: return "match";
    }
}

Gate Judge(const GateIn& in) noexcept {
    if (in.remote_equip != in.body_equip && in.remote_base != in.body_equip) return Gate::mismatch;
    if (in.switching) return Gate::switching;
    if (!in.type_ok || !BulletType(in.body_type)) return Gate::projectile;
    return Gate::match;
}

std::uint32_t StripBits(Gate gate) noexcept {
    switch (gate) {
    case Gate::mismatch:
    case Gate::switching: return partner_native::kPadFire | partner_native::kPadReload;
    case Gate::projectile: return partner_native::kPadFire;
    default: return 0u;
    }
}

} // namespace mgscoop::partner_slots
