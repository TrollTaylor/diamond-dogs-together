// Adapted from Dynamite's IsDefenseTeamByOnlineFobLocalHook and
// IsOffenseTeamByOnlineFobLocalHook and GetFobDefensePlayerInstanceIndexHook,
// c90f252f8200765f66c005d6bf4357fda0c0d98e.
// GPLv3 with Commons Clause 1.0; third_party/dynamite/LICENSE.
// Changes: exact current-build bodies, explicit mission lifetime, and original
// forwarding outside that lifetime. Native session host authority is untouched.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/player_team_patch.hpp"
#include "player_ghost_contract.hpp"
#include "player_index_contract.hpp"
#include "targeting_contract.hpp"
#include "mgscoop/enemy_targeting.hpp"
#include "mgscoop/combat_role_patch.hpp"
#include "mgscoop/soldier_patch.hpp"

namespace mgscoop::player_team {
namespace {
using Predicate=bool (*)();
Predicate defense_original=nullptr,offense_original=nullptr;
using PlayerIndex=unsigned int (*)(void*);
PlayerIndex defender_index_original=nullptr;
PlayerIndex offense_index_original=nullptr;
std::atomic<bool> active{false};
SRWLOCK lock=SRWLOCK_INIT;
void* installed_base=nullptr;
bool Defense() { return active.load(std::memory_order_acquire) ? false : defense_original(); }
bool Offense() { return active.load(std::memory_order_acquire) ? true : offense_original(); }
unsigned int DefenderIndex(void* context) {
    return active.load(std::memory_order_acquire) ? player_index::coop_defender_index : defender_index_original(context);
}
unsigned int OffenseIndex(void* context) {
    if(active.load(std::memory_order_acquire)) {
        const auto index=enemy_targeting::SelectedIndex();
        if(index<2) return index;
    }
    return offense_index_original(context);
}
bool Match(void* base,std::size_t span,std::uintptr_t rva,const unsigned char* expected,std::size_t size) noexcept {
    if(rva>span || size>span-rva || size>256) return false;
    unsigned char buffer[256]{}; SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+rva,buffer,size,&read) &&
        read==size && std::memcmp(buffer,expected,size)==0;
}
}
bool SetActive(bool enabled) noexcept {
    // Stop all policy overrides before attempting fallible branch restoration.
    combat_role::SetActive(enabled);
    active.store(enabled,std::memory_order_release);
    if(!enabled) {
        enemy_targeting::Clear();
        return soldier_patch::Deactivate();
    }
    return true;
}
bool IsActive() noexcept { return active.load(std::memory_order_acquire); }
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally { DWORD error; ~Finally(){ ReleaseSRWLockExclusive(&lock); SetLastError(error); } } finally{incoming};
    if(installed_base) return installed_base==base;
    if(!enemy_targeting::Initialize(base,span)) {
        log("player_team=refused; reason=targeting_contract\r\n"); return false;
    }
    using namespace player_ghost;
    if(!Match(base,span,defense_rva,defense_body,sizeof(defense_body)) ||
       !Match(base,span,offense_rva,offense_body,sizeof(offense_body)) ||
       !Match(base,span,player_index::defense_rva,player_index::defense_body,sizeof(player_index::defense_body))) {
        log("player_team=refused; reason=body_mismatch\r\n"); return false;
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Install),&pinned)) return false;
    struct Hook { std::uintptr_t rva; void* replacement; void** original; };
    const Hook hooks[]={
        {defense_rva,reinterpret_cast<void*>(&Defense),reinterpret_cast<void**>(&defense_original)},
        {offense_rva,reinterpret_cast<void*>(&Offense),reinterpret_cast<void**>(&offense_original)},
        {player_index::defense_rva,reinterpret_cast<void*>(&DefenderIndex),reinterpret_cast<void**>(&defender_index_original)},
        {targeting_contract::offense_rva,reinterpret_cast<void*>(&OffenseIndex),reinterpret_cast<void**>(&offense_index_original)},
    };
    constexpr std::size_t count=sizeof(hooks)/sizeof(hooks[0]);
    auto address=[base](const Hook& hook) { return static_cast<unsigned char*>(base)+hook.rva; };
    for(std::size_t i=0;i<count;++i) {
        if(MH_CreateHook(address(hooks[i]),hooks[i].replacement,hooks[i].original)!=MH_OK) {
            // None can have run yet; these disabled trampolines can be freed.
            for(std::size_t j=0;j<i;++j) MH_RemoveHook(address(hooks[j]));
            log("player_team=failed; reason=create_hook\r\n"); return false;
        }
    }
    // No active policy until all four forwarding hooks are installed.
    for(std::size_t i=0;i<count;++i) {
        if(MH_EnableHook(address(hooks[i]))!=MH_OK) {
            // Keep every trampoline alive once enabling starts: an earlier
            // hook may be in flight. Inactive hooks retain original behavior.
            log("player_team=failed; reason=enable_hook inactive_forwarding=1\r\n"); return false;
        }
    }
    installed_base=base;
    log("player_team=installed; predicates=2 defender_index=15 offense_index=expiring_host_selection inactive_forwarding=1 session_host_authority_unchanged=1\r\n");
    return true;
}
}
