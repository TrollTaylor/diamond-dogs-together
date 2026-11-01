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
// fb1031 gear (0.14.5 online, the friend's log): the host reloaded Mother Base
// while the link was down (equip_preload=refused; admission=link_not_connected),
// the partner paired 4 min later and for 216 s had no gun and no muzzle flash on
// the host's screen (writer no_resident: their bases were never requested in
// that mission) until a Game Over reload preloaded them. A partner who pairs
// after this mission's allocation (a late join, a link that was down at the
// load) gets, once per mission and link session: a mid-mission load of their
// bases (lab.ini partner_equip_midload; the same read-only preflight as the
// allocation's), else one note that their gun shows after the next load.
struct LateGearInput {
    bool admitted=false;   // EquipAdmissionReason: a connected link (play) or the one-PC lab
    bool manifest=false;   // the peer's manifest of this link session
    bool gameplay=false;   // the writer's gates (a partner-body mission's gameplay, not terminating)
    bool covered=false;    // this allocation's verified preload covered every non-zero remote base
    bool missing=false;    // a writer slot gave up for want of a resident entry (no_resident / no_cache_entry)
    bool midload=false;    // lab.ini partner_equip_midload
    bool attempted=false;  // the mid-mission load of this mission and link session was asked for
    bool returned=false;   // ... and its one public call returned
};
enum class LateGear : unsigned char { none, request, loading, next_load };
// none: nothing to do (not paired, no manifest, not in gameplay, preloaded, or
// resident anyway); request: ask for the mid-mission load; loading: its call
// returned (the writer re-plans); next_load: the gun shows after the next load.
constexpr LateGear DecideLateGear(const LateGearInput& c) noexcept {
    return !c.admitted || !c.manifest || !c.gameplay || c.covered || !c.missing ? LateGear::none
        : c.midload && !c.attempted ? LateGear::request
        : c.midload && c.returned ? LateGear::loading
        : LateGear::next_load;
}
constexpr const char* LateGearName(LateGear g) noexcept {
    return g==LateGear::request ? "request" : g==LateGear::loading ? "loading" : g==LateGear::next_load ? "next_load" : "none";
}
// One mid-mission load per mission and link session (a new pair may ask once
// more), on the runtime owner, only when DecideLateGear asks for it; the note
// once per mission and link session.
class MidLoadAuthorization {
    std::uint32_t mission_=0, link_=0;
    bool attempted_=false, permitted_=false, returned_=false, noted_=false;
public:
    // The mission's gameplay and this link session; a new mission or link session starts over.
    void Track(std::uint32_t mission, std::uint32_t link) noexcept {
        if(mission==mission_ && link==link_) return;
        mission_=mission; link_=link; attempted_=permitted_=returned_=noted_=false;
    }
    void End() noexcept { mission_=link_=0; attempted_=permitted_=returned_=noted_=false; }
    bool Attempted() const noexcept { return attempted_; }
    bool Returned() const noexcept { return returned_; }
    // The query: consumes the one attempt once the decision asks for it, before the preflight verdict.
    const char* Try(bool owner, std::uint32_t mission, std::uint32_t link, LateGear decision, bool preflight_ok) noexcept {
        if(!owner) return "not_runtime_owner";
        if(!mission_ || mission!=mission_ || link!=link_) return "mission";
        if(attempted_) return "already_attempted";
        if(decision!=LateGear::request) return "not_needed";
        attempted_=true;
        if(!preflight_ok) return "midload_preflight";
        permitted_=true;
        return nullptr;
    }
    // The receipt after the one public call: true when it returned (the writer then re-plans).
    bool Receipt(bool owner, std::uint32_t mission, bool returned) noexcept {
        if(!owner || !permitted_ || mission!=mission_) return false;
        permitted_=false;
        returned_=returned;
        return returned_;
    }
    bool TakeNote(LateGear decision) noexcept {
        if(decision!=LateGear::next_load || noted_) return false;
        noted_=true;
        return true;
    }
};
// The writer re-plans its missing-cache slots once per returned mid-mission
// load (the residency generation the observer bumps; 0 = none yet).
inline bool ResidencyRenewal(std::uint32_t& seen, std::uint32_t now) noexcept {
    if(!now || now==seen) return false;
    seen=now;
    return true;
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
// LateGearInput::covered: this allocation already did what it could for this
// partner. Its preload query had their manifest (source remote: a base it
// dropped, such as a type mismatch or an eviction, would fail mid-mission and
// at the next load alike, so no load and no note), or its preloaded ids cover
// every non-zero remote base. Only an allocation WITHOUT their manifest
// (refused link_not_connected, no_manifest, predicted) leaves the late path.
inline bool LateGearCovered(bool, const std::uint16_t (&remote)[3], const std::uint16_t* ids, unsigned count) noexcept {
    unsigned wanted=0;
    const unsigned covered=PreloadCover(remote,ids,count,wanted);
    return wanted && covered==wanted;
}
}
