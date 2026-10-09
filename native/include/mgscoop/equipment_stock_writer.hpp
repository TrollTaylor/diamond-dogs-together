#pragma once
#include <cstddef>
#include <cstdint>

namespace mgscoop::equipment_stock_writer {
// The change-equip request's stock/ammo fields (request +10 / +14). The
// qualified writer requests 0 / 0; MGS_LOCAL_STOCK46_AMMO_TEST (research only,
// default OFF, its own qualification run) requests a loaded magazine so a
// native reload has stock: ammo 30, stock 210 (the debug refill's values for
// this weapon class). Changing a qualified native call's arguments is a new
// native path: never enable it in a normal build.
struct RequestAmounts {
    std::uint16_t stock, ammo;
};
inline constexpr RequestAmounts kQualifiedAmounts{0, 0};
inline constexpr RequestAmounts kAmmoTestAmounts{210, 30};
inline constexpr RequestAmounts RequestAmountsFor(bool ammo_test) noexcept { return ammo_test ? kAmmoTestAmounts : kQualifiedAmounts; }
#if defined(MGS_LOCAL_STOCK46_AMMO_TEST) && MGS_LOCAL_STOCK46_AMMO_TEST
inline constexpr bool kAmmoTest = true;
#else
inline constexpr bool kAmmoTest = false;
#endif
// Separate copied-save experiment. The owner supplies existing runtime/code,
// source, lab/save and session guards; the implementation re-reads every native
// equipment/resource owner before its sole stock46 instance1 dispatch.
struct Context {
    bool enabled=false, owner=false, offline_gates=false, preload_queued=false;
    bool body_ready=false;
    std::uint32_t mission=0, body_epoch=0;
    std::uintptr_t base=0, player=0, remote_block=0;
    std::size_t image_span=0;
};
class Authorization {
    bool active_=false,attempted_=false;
    std::uint32_t mission_=0;
public:
    void Begin(std::uint32_t mission) noexcept {active_=true;attempted_=false;mission_=mission;}
    void End() noexcept {active_=false;attempted_=false;mission_=0;}
    bool Attempted() const noexcept {return attempted_;}
    bool CanObserveReadiness(const Context& c) const noexcept {
        return c.enabled && active_ && !attempted_ && c.owner && c.offline_gates && c.body_ready &&
            c.preload_queued && c.mission==30010 && c.mission==mission_ && c.player && c.remote_block && c.base && c.image_span;
    }
    const char* Try(const Context& c) noexcept {
        if(!c.enabled) return "disabled";
        if(!active_ || !c.owner) return "not_runtime_owner";
        if(c.mission!=30010 || c.mission!=mission_) return "allocation_mission";
        if(attempted_) return "already_attempted";
        if(!c.body_ready || !c.preload_queued) return "not_ready";
        // A bounded read-only readiness pass precedes this mutation lease.
        // Warp/body epochs never reset the allocation authorization.
        attempted_=true;
        if(!c.offline_gates) return "offline_owner_gates";
        if(!c.player || !c.remote_block || !c.base || !c.image_span) return "body_lease";
        return nullptr;
    }
};
class Readiness {
    unsigned observations_=0;
public:
    static constexpr unsigned limit=50;
    void Reset() noexcept {observations_=0;}
    unsigned Count() const noexcept {return observations_;}
    // Observation count limits waiting; only the strict preflight result is
    // readiness. Failed reads never authorize a native dispatch.
    const char* Observe(bool ready) noexcept {
        if(observations_==limit) return "readiness_limit";
        if(ready) return nullptr;
        if(observations_<limit) ++observations_;
        return observations_==limit ? "readiness_limit" : "readiness_wait";
    }
};
// Every generated SHA-256 gate (native/equipment_stock_writer_pins.hpp,
// tools/generate_equip_writer_pins.py) holds in the running image; mismatch
// names the first differing span. Pure reads; available in every build.
bool PinsIntact(std::uintptr_t base,std::size_t span,std::uintptr_t& mismatch) noexcept;
// All public methods are inert in builds without MGS_LOCAL_STOCK46_WRITER_TEST.
void BeginAllocation(std::uint32_t mission) noexcept;
void End() noexcept;
void Observe(const Context& context,void (*log)(const char*) noexcept) noexcept;

// --- The generalized writer (W3; docs/native-puppet.md "Generalized writer").
// Per remote slot (0 hip, 1 back, 2 secondary) one dispatch per allocation of
// the same qualified ChangeEquip call stock46 uses, for the target a tier
// picked: A1 the manifest's base id itself (its existing protected cache
// entry), C a resident protected stock gun of the same type (same receiver
// first, then the lowest id), D refuse and keep the slot empty. A filled slot
// is never replaced; a cache entry is never built or evicted here. One
// dispatch in flight at a time, each behind the full stock46-shape preflight
// with the target's own derived data and its own readback.
enum class Tier : std::uint8_t { none, a1, c, d };
inline const char* TierName(Tier tier) noexcept {
    switch(tier) {
    case Tier::a1: return "a1";
    case Tier::c: return "c";
    case Tier::d: return "d";
    default: return "none";
    }
}
enum class SlotState : std::uint8_t { idle, plan_wait, readiness, tracking, bound, queued, refused, unverified };
inline const char* SlotStateName(SlotState state) noexcept {
    switch(state) {
    case SlotState::plan_wait: return "plan_wait";
    case SlotState::readiness: return "readiness";
    case SlotState::tracking: return "tracking";
    case SlotState::bound: return "bound";
    case SlotState::queued: return "queued";
    case SlotState::refused: return "refused";
    case SlotState::unverified: return "unverified";
    default: return "idle";
    }
}
inline constexpr unsigned kSlots=3;
struct SlotStatus {
    std::uint16_t want=0;       // the manifest's base id of this slot (0 unknown)
    std::uint16_t equip=0;      // the planned equip (A1 want, C the substitute)
    Tier tier=Tier::none;
    SlotState state=SlotState::idle;
    bool optional=false;        // the target's main FPK carries the optional asset (family O) or not (N)
    std::uint8_t type=0,cache_index=0xFF;
    std::uint32_t handle=0;     // the expected handle once dispatched
    char reason[32]{};
};
struct ResidentEntry {
    std::uint16_t equip=0;std::uint8_t type=0,receiver=0,flags=0,index=0;std::uint32_t refs=0;
};
struct Status {
    bool active=false;          // an allocation is authorized
    std::uint32_t mission=0;
    bool substitute=false;      // tier C allowed (partner_native_equip=substitute)
    SlotStatus slots[kSlots]{};
    std::uint32_t dispatches=0,bound=0,refused=0;
    bool census_ok=false;
    std::uint8_t resident_count=0;  // used entries of the 48 (the first 48 listed)
    ResidentEntry resident[48]{};
};
struct GeneralContext {
    bool enabled=false,owner=false,gates=false,body_ready=false,substitute=false,manifest=false;
    std::uint32_t mission=0,body_epoch=0;
    std::uintptr_t base=0,player=0,remote_block=0;
    std::size_t image_span=0;
    std::uint16_t want[kSlots]{};   // the received manifest's base ids
    std::uint8_t remote_slot=0xFF;  // the remote's current slot (dispatched last), 0xFF unknown
    // Why `gates` is false (a static string: the first failing admission or owner gate).
    const char* gate_reason=nullptr;
};
// Order of the slots this allocation dispatches: the remote's current slot
// last (ChangeEquip's flags 4 leaves body 1 on the last dispatched slot).
inline void SlotOrder(std::uint8_t remote_slot,std::uint8_t (&order)[kSlots]) noexcept {
    if(remote_slot>=kSlots) {order[0]=0;order[1]=2;order[2]=1;return;}
    unsigned n=0;
    for(std::uint8_t s=0;s<kSlots;++s) if(s!=remote_slot) order[n++]=s;
    order[n]=remote_slot;
}
void ObserveGeneral(const GeneralContext& context,void (*log)(const char*) noexcept) noexcept;
// A copy of the generalized writer's state (zeroed when the option is off).
void ReadStatus(Status& out) noexcept;
}
