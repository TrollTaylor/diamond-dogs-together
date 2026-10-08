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
// All public methods are inert in builds without MGS_LOCAL_STOCK46_WRITER_TEST.
void BeginAllocation(std::uint32_t mission) noexcept;
void End() noexcept;
void Observe(const Context& context,void (*log)(const char*) noexcept) noexcept;
}
