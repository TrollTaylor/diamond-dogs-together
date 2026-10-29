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
// Two-PC test 2026-10-23 item 2: the host pressed Continue right after a save
// handoff while the relaunched guest was still at its title, so the host's
// allocation had no manifest (equip_preload=refused; reason=no_manifest), the
// manifest came 20 s later, nothing loaded the guest's bases and its guns never
// showed in that whole visit (backoff no_resident x6, refused). The preload is
// a mission-block load at the allocation only (vanilla calls
// RequestLoadToEquipMissionBlock in MissionPrepare), so a late manifest cannot
// load anything until the next allocation.
// The source of the allocation preload's ids: the peer's manifest of this link
// session first. Without one, a peer that restarted on this copy's save (this
// host handed its save over in this process: the token partner_restarting)
// continues that save, so its first loadout is this copy's own: this copy's own
// last sent manifest predicts its base ids (the writer still waits for the real
// manifest before it binds anything). Nothing is guessed otherwise.
enum class PreloadSource : unsigned char { none, remote, predicted };
constexpr PreloadSource ChoosePreloadSource(bool remote, bool peer_on_handed_save, bool own) noexcept {
    return remote ? PreloadSource::remote : peer_on_handed_save && own ? PreloadSource::predicted : PreloadSource::none;
}
constexpr const char* PreloadSourceName(PreloadSource s) noexcept {
    return s==PreloadSource::remote ? "remote" : s==PreloadSource::predicted ? "predicted" : "none";
}
// The late-manifest receipt: how many of the remote's non-zero base ids the
// allocation's preload covered (`wanted` = the non-zero ones).
inline unsigned PreloadCover(const std::uint16_t (&remote)[3], const std::uint16_t* ids, unsigned count, unsigned& wanted) noexcept {
    unsigned covered=0; wanted=0;
    for(const std::uint16_t id : remote) {
        if(!id) continue;
        ++wanted;
        for(unsigned i=0;ids && i<count;++i) if(ids[i]==id) { ++covered; break; }
    }
    return covered;
}
}
