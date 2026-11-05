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
    case Gate::support: return "support";
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

const char* MismatchWhyName(Mismatch why) noexcept {
    switch (why) {
    case Mismatch::plan: return "plan";
    case Mismatch::slot: return "slot";
    default: return "equip";
    }
}

Mismatch MismatchWhy(bool plan_ok, const Plan& plan, unsigned remote_slot, unsigned body_slot) noexcept {
    if (!plan_ok || remote_slot >= kSlots) return Mismatch::plan;
    const unsigned want = plan.map[remote_slot];
    if (want >= kSlots || !(plan.ok_mask & (1u << want))) return Mismatch::plan;
    return body_slot != want ? Mismatch::slot : Mismatch::equip;
}

EpisodeVerdict EpisodeStep(Episode& e, const EpisodeIn& in) noexcept {
    if (in.target != e.target || in.seq != e.seq) e = Episode{in.target, in.seq};
    if (in.body == in.want) {
        e.reached = true;
        e.left_ms = 0;
        return EpisodeVerdict::same;
    }
    if (e.reached && !e.left_ms) e.left_ms = in.now_ms ? in.now_ms : 1;
    if (in.switching) return EpisodeVerdict::wait;
    if (e.writes < kEpisodeWrites + e.rearms) return EpisodeVerdict::write;
    if (in.rearm && e.reached && e.rearms < kEpisodeRearms && in.now_ms >= e.left_ms + kRearmAfterMs &&
        (!e.rearm_ms || in.now_ms >= e.rearm_ms + kRearmGapMs)) {
        ++e.rearms;
        e.rearm_ms = in.now_ms ? in.now_ms : 1;
        return EpisodeVerdict::drift;
    }
    return EpisodeVerdict::wait;
}

std::uint32_t StripBits(Gate gate) noexcept {
    switch (gate) {
    case Gate::mismatch:
    case Gate::switching: return partner_native::kPadFire | partner_native::kPadReload;
    case Gate::projectile: return partner_native::kPadFire;
    case Gate::support: return partner_native::kPadReload;
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
    if (!in.judged || (in.gate != Gate::match && in.gate != Gate::standin && in.gate != Gate::support)) return AimRefusal::gate;
    // Support aims in its lane: slot 3, or slot 6 for an admitted arm (WS4 v1;
    // the driver judges Gate::support at slot 6 only for the arm lane).
    if (!in.slot_valid || (in.gate == Gate::support ? in.slot != 3 && in.slot != 6 : in.slot > 2)) return AimRefusal::slot;
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

const char* TruthVerdictName(TruthVerdict value) noexcept {
    switch (value) {
    case TruthVerdict::nostate: return "nostate";
    case TruthVerdict::aim: return "aim";
    case TruthVerdict::keep: return "keep";
    case TruthVerdict::lane: return "lane";
    case TruthVerdict::other: return "other";
    case TruthVerdict::loco: return "loco";
    case TruthVerdict::act: return "act";
    case TruthVerdict::draw: return "draw";
    default: return "off";
    }
}

// Each interval is credited to the state that held during it (the previous
// tick's refused), the last one up to the tick that saw the release.
TruthRunOut TruthRunStep(TruthRun& r, bool hold, bool refused, std::uint16_t act, std::uint8_t flags, std::uint64_t now_ms) noexcept {
    TruthRunOut o{};
    if (r.open) {
        const std::uint32_t dt = now_ms >= r.last_ms ? static_cast<std::uint32_t>(now_ms - r.last_ms) : 0u;
        if (r.last_refused) r.refused_ms += dt;
        else r.admitted_ms += dt;
        r.last_ms = now_ms;
        if (!hold) {
            r.open = false;
            o.ended = true;
            o.refused_ms = r.refused_ms;
            o.admitted_ms = r.admitted_ms;
            o.len_ms = now_ms >= r.since_ms ? static_cast<std::uint32_t>(now_ms - r.since_ms) : 0u;
            o.first_act = r.first_act;
            o.last_act = r.last_act;
            o.flags = r.flags;
            return o;
        }
        r.last_act = act;
        r.flags = static_cast<std::uint8_t>(r.flags | flags);
        r.last_refused = refused;
        return o;
    }
    if (!hold) return o;
    r.open = true;
    r.since_ms = r.last_ms = now_ms;
    r.refused_ms = r.admitted_ms = 0;
    r.first_act = r.last_act = act;
    r.flags = flags;
    r.last_refused = refused;
    ++r.runs;
    return o;
}

const char* TruthRunResult(const TruthRunOut& out) noexcept {
    if (!out.admitted_ms) return "refused";
    if (!out.refused_ms) return "admitted";
    return "late";
}

// AimBlockedAct, hit 29, damage 45, and the climb/hang acts (ladder 21, pipe 22,
// wall climb 23, step_on 24, ledge hang 54..56): no hand is free for a gun.
bool TruthBodyOwnedAct(std::uint16_t act) noexcept {
    return AimBlockedAct(act) || act == 29 || act == 45 || (act >= 21 && act <= 24) || (act >= 54 && act <= 56);
}

TruthOut TruthStep(TruthState& s, const TruthIn& in) noexcept {
    TruthOut o{};
    const std::uint32_t refuse = partner_pad::kHold | partner_native::kPadFire;
    if (!in.enabled) {
        s.aim_ms = 0;
        s.valid_ms = 0;
        o.verdict = TruthVerdict::off;
    } else if (!in.act_valid) {
        // Review animtruth-5: a pose gap up to gap_ms after the last live act keeps a
        // refusal and its strip (a lifted strip would turn a refused HOLD into a fresh
        // press for the gap); a longer loss fails open.
        const bool refused = s.last == TruthVerdict::loco || s.last == TruthVerdict::act;
        if (refused && in.gap_ms && s.valid_ms && in.now_ms >= s.valid_ms && in.now_ms - s.valid_ms <= in.gap_ms) {
            o.verdict = s.last;
            o.strip = refuse;
        } else {
            o.verdict = TruthVerdict::nostate;
        }
    } else if (AimAct(in.act)) {
        s.aim_ms = in.now_ms ? in.now_ms : 1;
        o.verdict = TruthVerdict::aim;
    } else if (TruthBodyOwnedAct(in.act)) {
        o.verdict = TruthVerdict::act;
        o.strip = refuse;
    } else if (!in.slot_valid || in.slot > 2 || in.support_lane) {
        o.verdict = TruthVerdict::lane;
    } else if (in.act >= 1 && in.act <= 6) {
        const bool keep = s.aim_ms && in.now_ms >= s.aim_ms && in.now_ms - s.aim_ms < kTruthKeepMs;
        o.verdict = keep ? TruthVerdict::keep : in.in_hand ? TruthVerdict::draw : TruthVerdict::loco;
        if (o.verdict == TruthVerdict::loco) o.strip = refuse;
    } else {
        o.verdict = TruthVerdict::other;
    }
    if (in.enabled && in.act_valid) s.valid_ms = in.now_ms ? in.now_ms : 1;
    o.changed = o.verdict != s.last;
    if (o.changed && o.strip) ++s.refusals;
    if (o.changed && o.verdict == TruthVerdict::aim) ++s.confirms;
    s.last = o.verdict;
    return o;
}

std::uint32_t TruthBoxStrip(bool remote_valid, std::uint16_t remote_act, bool body_valid, std::uint16_t body_act) noexcept {
    const auto box = [](std::uint16_t act) noexcept { return act == 77 || act == 78; };
    return remote_valid && body_valid && box(remote_act) && !box(body_act) ? partner_pad::kEvade : 0u;
}

const char* LocalAimEventName(LocalAimEvent value) noexcept {
    switch (value) {
    case LocalAimEvent::aimed: return "aimed";
    case LocalAimEvent::no_aim: return "no_aim";
    default: return "none";
    }
}

LocalAimOut LocalAimStep(LocalAimState& s, const LocalAimIn& in, std::uint32_t no_aim_ms) noexcept {
    LocalAimOut o{};
    if (!in.enabled || !in.hold) {
        s.run = false;
        return o;
    }
    if (!s.run) {
        s.run = true;
        s.aimed = s.reported = false;
        s.since_ms = in.now_ms;
        ++s.runs;
    }
    const std::uint32_t ms = in.now_ms >= s.since_ms ? static_cast<std::uint32_t>(in.now_ms - s.since_ms) : 0u;
    if (in.act_valid && AimAct(in.act)) {
        // The first aim act of the run (also after a no_aim: a late aim is reported with its delay).
        if (!s.aimed) {
            s.aimed = s.reported = true;
            ++s.aimed_runs;
            o.event = LocalAimEvent::aimed;
            o.ms = ms;
        }
    } else if (!s.aimed && !s.reported && ms >= no_aim_ms) {
        s.reported = true;
        ++s.no_aim_runs;
        o.event = LocalAimEvent::no_aim;
        o.ms = ms;
    }
    return o;
}

} // namespace mgscoop::partner_slots
