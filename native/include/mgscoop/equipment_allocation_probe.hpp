#pragma once
#include <cstdint>

namespace mgscoop::equipment_allocation_probe {
// Original portable policy for an opt-in experiment. The adapter provides
// exact-source receipts and read-only native state, serialized by its existing
// receipt lock. This class cannot call or write game code or allocate assets.
struct Context {
    bool enabled=false, offline=false, lab=false, allocation=false, archive=false;
    bool host_source=false, partner=false, code_gate=false, saves_redirected=false;
    bool allocation_thread=false, terminating=false, native_session=false, cleanup_pending=false;
    bool mission_read=false, supported_mission=false, state_ok=false;
    bool state_valid=false, state_online=false, state_sync=false;
    // A new exact-source receipt is emitted only from the pinned pre-loading
    // insertion after protected Lua checks confirm this mission's selected
    // two-player package, archive direct-read, TPP mode, and S_IS_ONLINE=false.
    // This is distinct from (and cannot be inferred from) ReadState failure.
    bool pre_player_source=false, equipment_manager_ready=false;
    bool player_slot_read=false, player_slot_null=false;
    std::uint32_t mission=0, requested_count=0, count=0, local=0;
};

class Authorization {
    std::uint32_t mission_=0;
    bool active_=false, attempted_=false;
public:
    void Begin(std::uint32_t mission) noexcept { mission_=mission; active_=true; attempted_=false; }
    void End() noexcept { active_=attempted_=false; mission_=0; }
    bool Attempted() const noexcept { return attempted_; }
    const char* Try(const Context& c) noexcept {
        if(!c.enabled) return "disabled";
        if(!active_ || !c.allocation_thread) return "not_allocation_owner";
        if(!c.mission_read || c.mission!=mission_) return "allocation_mission";
        if(attempted_) return "already_attempted";
        // Even a failed read or later partial Lua allocation consumes the
        // accepted owner's attempt. Never retry midway through an allocation.
        attempted_=true;
        if(!c.offline) return "not_offline";
        if(!c.lab) return "lab_inactive";
        if(!c.allocation) return "allocation_disabled";
        if(!c.archive) return "archive_unmounted";
        if(!c.host_source) return "source_unselected";
        if(!c.partner) return "partner_disabled";
        if(!c.code_gate) return "code_gate";
        if(!c.saves_redirected) return "saves_not_redirected";
        if(c.terminating) return "terminating";
        if(c.native_session) return "native_session_owned";
        if(c.cleanup_pending) return "cleanup_pending";
        if(!c.supported_mission) return "unsupported_mission";
        if(!c.pre_player_source) return "pre_player_source";
        if(c.requested_count!=2) return "requested_count";
        if(!c.equipment_manager_ready) return "equipment_manager";
        if(!c.player_slot_read) return "player_slot_read";
        if(c.player_slot_null) {
            // The public group setup precedes player construction. Only an
            // independently successful read of a genuinely null P admits this
            // branch. ReadState's player_absent also means unreadable/invalid
            // P, so its enum alone must never authorize group creation.
            if(c.state_ok || c.state_valid || c.state_online || c.state_sync || c.count || c.local)
                return "null_state_mismatch";
            return nullptr;
        }
        if(!c.state_ok) return "read_state";
        if(!c.state_valid) return "player_invalid";
        if(c.state_online) return "player_online";
        if(c.state_sync) return "player_sync";
        if(c.count!=2) return "player_count";
        if(c.local!=0) return "local_index";
        return nullptr;
    }
};
}
