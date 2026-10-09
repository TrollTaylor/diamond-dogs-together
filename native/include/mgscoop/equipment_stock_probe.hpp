#pragma once
#include <cstdint>

namespace mgscoop::equipment_stock_probe {
// Separate opt-in loading experiment, with no native dispatch or memory writes.
// The adapter supplies all existing copied-save/allocation/source guards and a
// bounded read-only preflight for public stock46. A returned request is not
// evidence that packages or a partner's model have loaded.
struct Context {
    bool enabled=false, allocation_owner=false, mission_read=false;
    bool offline_owner_gates=false, groups_authorized=false, groups_returned=false;
    bool preflight_ok=false;
    std::uint32_t mission=0;
};
class Authorization {
    std::uint32_t mission_=0;
    bool active_=false, attempted_=false, permitted_=false, queued_=false;
public:
    void Begin(std::uint32_t mission) noexcept {
        mission_=mission; active_=true; attempted_=permitted_=queued_=false;
    }
    void End() noexcept {
        mission_=0; active_=attempted_=permitted_=queued_=false;
    }
    void SealAllocation() noexcept { active_=permitted_=false; }
    bool Attempted() const noexcept { return attempted_; }
    bool QueuedFor(std::uint32_t mission) const noexcept { return queued_ && mission_==mission; }
    const char* Try(const Context& c) noexcept {
        if(!c.enabled) return "disabled";
        if(!active_ || !c.allocation_owner) return "not_allocation_owner";
        if(!c.mission_read || c.mission!=mission_ || c.mission!=30010) return "allocation_mission";
        if(attempted_) return "already_attempted";
        attempted_=true;
        if(!c.offline_owner_gates) return "offline_owner_gates";
        if(!c.groups_authorized) return "groups_not_authorized";
        if(!c.groups_returned) return "groups_not_returned";
        if(!c.preflight_ok) return "stock_preflight";
        permitted_=true;
        return nullptr;
    }
    bool Receipt(bool owner, std::uint32_t mission, bool returned, bool verified) noexcept {
        if(!active_ || !owner || mission!=mission_ || !permitted_) return false;
        permitted_=false;
        queued_=returned && verified;
        return queued_;
    }
};
// The generalized preload (W2): the same one-attempt-per-allocation shape for
// the received manifest's base ids in any partner-body mission. The adapter
// supplies the admission (lab or play), the allocation owner, the mission
// and allocation receipts; `ids` is how many ids passed the per-id filter.
struct GeneralContext {
    bool enabled=false, allocation_owner=false, mission_read=false, supported=false;
    bool owner_gates=false, groups_authorized=false, groups_returned=false;
    bool manifest=false, preflight_ok=false;
    unsigned ids=0;
    std::uint32_t mission=0;
};
class GeneralAuthorization {
    std::uint32_t mission_=0;
    bool active_=false, attempted_=false, permitted_=false, queued_=false;
public:
    void Begin(std::uint32_t mission) noexcept {
        mission_=mission; active_=true; attempted_=permitted_=queued_=false;
    }
    void End() noexcept {
        mission_=0; active_=attempted_=permitted_=queued_=false;
    }
    void SealAllocation() noexcept { active_=permitted_=false; }
    bool Attempted() const noexcept { return attempted_; }
    bool QueuedFor(std::uint32_t mission) const noexcept { return queued_ && mission_==mission; }
    const char* Try(const GeneralContext& c) noexcept {
        if(!c.enabled) return "disabled";
        if(!active_ || !c.allocation_owner) return "not_allocation_owner";
        if(!c.mission_read || c.mission!=mission_ || !c.supported) return "allocation_mission";
        if(attempted_) return "already_attempted";
        // The first mission of a session has no manifest yet: nothing to load,
        // nothing spent (tiers A1 and C still apply to what is resident).
        if(!c.manifest) return "no_manifest";
        if(!c.ids) return "no_ids";
        attempted_=true;
        if(!c.owner_gates) return "owner_gates";
        if(!c.groups_authorized) return "groups_not_authorized";
        if(!c.groups_returned) return "groups_not_returned";
        if(!c.preflight_ok) return "preload_preflight";
        permitted_=true;
        return nullptr;
    }
    bool Receipt(bool owner, std::uint32_t mission, bool returned, bool verified) noexcept {
        if(!active_ || !owner || mission!=mission_ || !permitted_) return false;
        permitted_=false;
        queued_=returned && verified;
        return queued_;
    }
};
}
