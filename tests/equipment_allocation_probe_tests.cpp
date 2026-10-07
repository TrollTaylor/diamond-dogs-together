#include "mgscoop/equipment_allocation_probe.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

namespace {
using namespace mgscoop::equipment_allocation_probe;
unsigned checks=0;
void Check(bool value) { ++checks; if(!value) { std::fprintf(stderr,"failed check %u\n",checks); std::exit(1); } }
Context Ready() {
    Context c{};
    c.enabled=c.offline=c.lab=c.allocation=c.archive=c.host_source=c.partner=c.code_gate=true;
    c.saves_redirected=c.allocation_thread=c.mission_read=c.supported_mission=c.state_ok=c.state_valid=true;
    c.pre_player_source=c.equipment_manager_ready=c.player_slot_read=true;
    c.mission=30010; c.requested_count=2; c.count=2; c.local=0;
    return c;
}
bool Is(const char* actual,const char* expected) { return actual && std::strcmp(actual,expected)==0; }
}
int main() {
    Authorization gate;
    auto c=Ready(); c.enabled=false;
    Check(Is(gate.Try(c),"disabled") && !gate.Attempted());
    c=Ready(); Check(Is(gate.Try(c),"not_allocation_owner") && !gate.Attempted());
    gate.Begin(30010); c.allocation_thread=false;
    Check(Is(gate.Try(c),"not_allocation_owner") && !gate.Attempted());
    c=Ready(); c.mission_read=false;
    Check(Is(gate.Try(c),"allocation_mission") && !gate.Attempted());
    c=Ready(); c.mission=30020;
    Check(Is(gate.Try(c),"allocation_mission") && !gate.Attempted());
    c=Ready(); Check(gate.Try(c)==nullptr && gate.Attempted());
    Check(Is(gate.Try(c),"already_attempted"));
    gate.End(); Check(!gate.Attempted() && Is(gate.Try(c),"not_allocation_owner"));
    struct Field { bool Context::* member; bool value; const char* reason; };
    const Field fields[]={
        {&Context::offline,false,"not_offline"}, {&Context::lab,false,"lab_inactive"},
        {&Context::allocation,false,"allocation_disabled"}, {&Context::archive,false,"archive_unmounted"},
        {&Context::host_source,false,"source_unselected"}, {&Context::partner,false,"partner_disabled"},
        {&Context::code_gate,false,"code_gate"}, {&Context::saves_redirected,false,"saves_not_redirected"},
        {&Context::terminating,true,"terminating"}, {&Context::native_session,true,"native_session_owned"},
        {&Context::cleanup_pending,true,"cleanup_pending"}, {&Context::supported_mission,false,"unsupported_mission"},
        {&Context::pre_player_source,false,"pre_player_source"}, {&Context::player_slot_read,false,"player_slot_read"},
        {&Context::equipment_manager_ready,false,"equipment_manager"},
        {&Context::state_ok,false,"read_state"}, {&Context::state_valid,false,"player_invalid"},
        {&Context::state_online,true,"player_online"}, {&Context::state_sync,true,"player_sync"},
    };
    for(const auto& field:fields) {
        gate.Begin(30010); c=Ready(); c.*field.member=field.value;
        Check(Is(gate.Try(c),field.reason) && gate.Attempted());
        c=Ready(); Check(Is(gate.Try(c),"already_attempted"));
        gate.End(); Check(!gate.Attempted());
    }
    for(unsigned count:{0u,1u,3u,4u,0xffffffffu}) {
        gate.Begin(30010); c=Ready(); c.count=count;
        Check(Is(gate.Try(c),"player_count") && gate.Attempted());
    }
    for(unsigned local:{1u,2u,0xffffffffu}) {
        gate.Begin(30010); c=Ready(); c.local=local;
        Check(Is(gate.Try(c),"local_index") && gate.Attempted());
    }
    for(unsigned requested:{0u,1u,3u,4u,0xffffffffu}) {
        gate.Begin(30010); c=Ready(); c.requested_count=requested;
        Check(Is(gate.Try(c),"requested_count") && gate.Attempted());
    }
    // Null P is legitimate only at this source-proven pre-player stage. It
    // is not an actor-count or readiness result. All other guards stay in force.
    const auto PrePlayer=[] {
        auto value=Ready(); value.player_slot_null=true;
        value.state_ok=value.state_valid=false; value.count=0;
        return value;
    };
    for(unsigned mission:{30010u,30020u,30050u}) {
        gate.Begin(mission); c=PrePlayer(); c.mission=mission;
        Check(gate.Try(c)==nullptr && gate.Attempted());
        Check(Is(gate.Try(c),"already_attempted"));
        gate.End(); Check(!gate.Attempted());
    }
    for(const auto& field:fields) {
        // Validity is intentionally absent only for the independently null P.
        if(field.member==&Context::state_ok || field.member==&Context::state_valid) continue;
        gate.Begin(30010); c=PrePlayer(); c.*field.member=field.value;
        const bool contradictory=field.member==&Context::state_online || field.member==&Context::state_sync;
        Check(Is(gate.Try(c),contradictory ? "null_state_mismatch" : field.reason) && gate.Attempted());
        c=PrePlayer(); Check(Is(gate.Try(c),"already_attempted"));
    }
    for(bool Context::* field:{&Context::state_ok,&Context::state_valid}) {
        gate.Begin(30010); c=PrePlayer(); c.*field=true;
        Check(Is(gate.Try(c),"null_state_mismatch") && gate.Attempted());
    }
    gate.Begin(30010); c=PrePlayer(); c.count=2;
    Check(Is(gate.Try(c),"null_state_mismatch") && gate.Attempted());
    gate.Begin(30010); c=PrePlayer(); c.local=1;
    Check(Is(gate.Try(c),"null_state_mismatch") && gate.Attempted());
    // Ambiguous player_absent, unreadable P and a not-yet-valid non-null P do
    // not meet the null proof, regardless of a zero count/local diagnostic.
    gate.Begin(30010); c=PrePlayer(); c.player_slot_null=false;
    Check(Is(gate.Try(c),"read_state") && gate.Attempted());
    gate.Begin(30010); c=PrePlayer(); c.player_slot_read=false;
    Check(Is(gate.Try(c),"player_slot_read") && gate.Attempted());
    // A normal new allocation resets a prior accepted or refused attempt.
    for(unsigned mission:{30010u,30020u,30050u}) {
        gate.Begin(mission); c=Ready(); c.mission=mission;
        Check(gate.Try(c)==nullptr && gate.Attempted());
        Check(Is(gate.Try(c),"already_attempted"));
        gate.End(); Check(Is(gate.Try(c),"not_allocation_owner") && !gate.Attempted());
    }
    std::printf("%u equipment allocation authorization checks passed (portable policy; no game calls)\n",checks);
}
