// Original experiment using pinned tpp-mod's current-build signature and owner
// path and Dynamite's OnAllocate timing. See docs/host-session-trial.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include "mgscoop/host_session_trial.hpp"
#include "mgscoop/player_ghost_patch.hpp"
#include "mgscoop/player_team_patch.hpp"
#include "mgscoop/combat_role_patch.hpp"
#include "mgscoop/trap_patch.hpp"
#include "mgscoop/soldier_patch.hpp"
#include "mgscoop/result_patch.hpp"
#include "mgscoop/equipment_use_patch.hpp"
#include "mgscoop/teammate_marker_patch.hpp"
#include "mgscoop/game_heap_trial.hpp"
#include "mgscoop/dynamite_session_port.hpp"

namespace {
volatile LONG attempted = 0;
constexpr std::uintptr_t create_rva = 0x7ee590;
constexpr std::size_t create_size = 456;
constexpr unsigned char create_hash[32] = {
    0xf2,0x71,0x11,0x62,0xae,0x63,0x07,0x25,0xd8,0xb8,0x83,0x9a,0x74,0xd1,0x91,0x18,
    0x45,0x65,0xbb,0x5e,0x54,0x92,0xa0,0x00,0x1e,0x19,0x7f,0x7c,0xa1,0xc8,0x49,0x41};
bool Read(std::uintptr_t address, void* output, std::size_t count) noexcept {
    constexpr std::uintptr_t highest=0x00007fffffffffff;
    if(address<0x10000 || count==0 || count>512 || address>highest-count) return false;
    SIZE_T received=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,count,&received) && received==count;
}
template<class T> bool Read(std::uintptr_t address,T& result) noexcept { return Read(address,&result,sizeof(result)); }
bool CodeMatches(std::uintptr_t base,std::size_t span) noexcept {
    if(create_rva>span || create_size>span-create_rva) return false;
    unsigned char body[create_size]{}, digest[32]{};
    if(!Read(base+create_rva,body,sizeof(body))) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,body,sizeof(body),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok && std::memcmp(digest,create_hash,sizeof(digest))==0;
}
// Read the same thread-local slot as the independently decoded game accessor.
// No accessor call, TLS write, allocation, or retained block pointer is needed.
bool ObserveAllocationContext(std::uintptr_t base,std::size_t span,
                              void (*log)(const char*) noexcept) noexcept {
    constexpr std::uintptr_t accessor_rva=0xe63e0, index_rva=0x2c6ba70;
    constexpr unsigned char expected[]={
        0x8b,0x0d,0x8a,0x56,0xb8,0x02,0x65,0x48,0x8b,0x04,0x25,0x58,0x00,0x00,0x00,
        0xba,0x48,0x00,0x00,0x00,0x48,0x8b,0x04,0xc8,0x48,0x8b,0x04,0x02,0xc3};
    unsigned char body[sizeof(expected)]{};
    std::uint32_t index=0, index_after=0;
    std::uintptr_t tls=0, tls_after=0, block=0, block_after=0;
    if(accessor_rva>span || sizeof(expected)>span-accessor_rva ||
       index_rva>span || sizeof(index)>span-index_rva ||
       !Read(base+accessor_rva,body,sizeof(body)) || std::memcmp(body,expected,sizeof(body))!=0 ||
       !Read(base+index_rva,index) || index>=4096) return false;
    const auto vector=static_cast<std::uintptr_t>(__readgsqword(0x58));
    if(!Read(vector+static_cast<std::uintptr_t>(index)*8,tls) ||
       !Read(tls+0x48,block) || !Read(base+index_rva,index_after) ||
       !Read(vector+static_cast<std::uintptr_t>(index)*8,tls_after) ||
       !Read(tls+0x48,block_after)) return false;
    const bool consistent=index==index_after && tls==tls_after && block==block_after &&
        vector==static_cast<std::uintptr_t>(__readgsqword(0x58));
    char line[384]{};
    StringCchPrintfA(line,ARRAYSIZE(line),
        "host_allocation_context=observed; thread=%lu tick_ms=%llu tls_index=%u tls=%p current_block=%p consistent=%u calls=0 writes=0\r\n",
        GetCurrentThreadId(),GetTickCount64(),index,reinterpret_cast<void*>(tls),
        reinterpret_cast<void*>(block),consistent ? 1u : 0u);
    log(line);
    return consistent;
}
}

namespace {
bool HostPrerequisites(std::uintptr_t base,void (*log)(const char*) noexcept,std::uintptr_t* borrowed_target=nullptr) noexcept {
    std::uintptr_t vars=0,server=0,branch=0,target=0,table=0;
    std::uint16_t mission=0;
    std::uint32_t state=0;
    unsigned char flag=0;
    const char* failed="none";
    const auto require=[&](const char* stage,bool ok) noexcept { if(!ok) failed=stage; return ok; };
    if(!require("vars_read",Read(base+0x2c16920,vars)) || !require("vars_identity",vars==base+0x2c01360) ||
       !require("mission_read",Read(vars+0x12,mission)) || !require("mission_value",mission==30050) ||
       !require("server_read",Read(base+0x2befc00,server)) || !require("branch_read",Read(server+0x30,branch)) ||
       !require("target_read",Read(branch+0x28,target)) || !require("target_table_read",Read(target,table)) ||
       !require("target_type",table==base+0x2223508) ||
       !require("state_read",Read(target+0x10,state)) || !require("flag_read",Read(target+0x48,flag)) ||
       !require("state_value",state==0) || !require("flag_value",flag==0)) {
        char line[512]{};
        StringCchPrintfA(line,ARRAYSIZE(line),
            "host_prerequisites=refused; stage=%s thread=%lu tick_ms=%llu mission=%u state=%u flag=%u target=%p target_table=%p\r\n",
            failed,GetCurrentThreadId(),GetTickCount64(),mission,state,static_cast<unsigned>(flag),
            reinterpret_cast<void*>(target),reinterpret_cast<void*>(table));
        log(line); return false;
    }
    if(borrowed_target) *borrowed_target=target;
    log("host_prerequisites=passed; mission=30050\r\n");
    return true;
}

// Player2 construction latches the online-mode status, and allocation consumes
// that flag and session interfaces. Requiring an existing player before early
// host creation conflates two phases. This separate observation is never a
// substitute for the host allocation-lifetime gate below.
bool PlayerReadiness(std::uintptr_t base,void (*log)(const char*) noexcept) noexcept {
    std::uintptr_t player=0,table=0;
    std::uint32_t limit=0;
    const char* failed="none";
    const auto require=[&](const char* stage,bool ok) noexcept { if(!ok) failed=stage; return ok; };
    if(!require("player_read",Read(base+0x2a82c30,player)) ||
       !require("player_present",player!=0) || !require("player_table_read",Read(player,table)) ||
       !require("player_type",table==base+0x2295490) ||
       !require("player_limit_read",Read(player+0x208,limit)) || !require("player_limit_value",limit==2)) {
        char line[384]{};
        StringCchPrintfA(line,ARRAYSIZE(line),
            "player_readiness=not_ready; stage=%s thread=%lu tick_ms=%llu player=%p player_table=%p player_limit=%u\r\n",
            failed,GetCurrentThreadId(),GetTickCount64(),reinterpret_cast<void*>(player),
            reinterpret_cast<void*>(table),limit);
        log(line); return false;
    }
    log("player_readiness=storage_present; player_limit=2 independent_actors=unverified\r\n");
    return true;
}
}

void ObserveHostSessionPreflight(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!log || !verified_base) return;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    if(!CodeMatches(base,span)) { log("host_preflight=refused; stage=code_gate\r\n"); return; }
    if(!ObserveAllocationContext(base,span,log)) { log("host_preflight=refused; stage=context_read\r\n"); return; }
    if(HostPrerequisites(base,log)) PlayerReadiness(base,log);
}

bool PrepareDynamitePlayerLoading(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !log || !HostPrerequisites(reinterpret_cast<std::uintptr_t>(verified_base),log)) return false;
    if(!mgscoop::player_ghost::Install(verified_base,span,log) ||
       !mgscoop::player_team::Install(verified_base,span,log) ||
       !mgscoop::combat_role::Install(verified_base,span,log) ||
       !mgscoop::trap_patch::Install(verified_base,span,log) ||
       !mgscoop::result_patch::Install(verified_base,span,log) ||
       !mgscoop::equipment_use::Install(verified_base,span,log) ||
       !mgscoop::teammate_marker::Install(verified_base,span,log) ||
       !mgscoop::soldier_patch::Activate(verified_base,span,log)) return false;
    mgscoop::result_patch::BeginCoopScene();
    mgscoop::player_team::SetActive(true);
    log("player_team=active; scene=30050 lifecycle=before_Mission_Start\r\n");
    return true;
}

bool TryCreateHostSession(void* verified_base,std::size_t span,void (*log)(const char*) noexcept,bool heap_test,bool create_enabled) noexcept {
    if(!log || !verified_base || InterlockedCompareExchange(&attempted,1,0)!=0) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    if(!CodeMatches(base,span)) { log("host_trial=refused; reason=code_gate\r\n"); return false; }
    if(!ObserveAllocationContext(base,span,log)) {
        log("host_trial=refused; reason=allocation_context_unreadable\r\n"); return false;
    }
    std::uintptr_t target=0;
    if(!HostPrerequisites(base,log,&target)) return false;
    PlayerReadiness(base,log); // Observation only; absence is expected this early.
    if(heap_test) {
        RunGameHeapTrial(verified_base,span,log);
        ObserveAllocationContext(base,span,log);
        HostPrerequisites(base,log);
        return false; // Never set the Lua online flag from a heap test.
    }
    if(create_enabled) return CreateDynamiteHostSession(verified_base,span,target,log);
    log("host_trial=refused; reason=host_v2_not_enabled\r\n");
    return false;
}

bool TryCreateClientSession(void* verified_base,std::size_t span,std::uint64_t host,
                            void (*log)(const char*) noexcept,bool inspect_only) noexcept {
    if(!log || !verified_base || (!host && !inspect_only) || InterlockedCompareExchange(&attempted,1,0)!=0) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(verified_base);
    if(!CodeMatches(base,span) || !ObserveAllocationContext(base,span,log)) return false;
    std::uintptr_t target=0;
    if(!HostPrerequisites(base,log,&target) || !PlayerReadiness(base,log)) return false;
    if(inspect_only) {
        const bool ready=InspectDynamiteClientSession(verified_base,span,target,log);
        log(ready ? "client_loading_check=passed; native_join_calls=0 allocator_hook=0\r\n"
                  : "client_loading_check=failed; native_join_calls=0 allocator_hook=0\r\n");
        return ready; // Caller reports diagnostic success separately from submission.
    }
    return CreateDynamiteClientSession(verified_base,span,target,host,log);
}
