// Adapted from Dynamite GetPatches: DoesFobGhostPlayer mission check bypass,
// c90f252f8200765f66c005d6bf4357fda0c0d98e. GPLv3 with Commons Clause 1.0.
// See third_party/dynamite/LICENSE. Changes: current-build complete-body gate,
// pre-Mission.Start installation and an aligned in-memory atomic replacement.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include "mgscoop/player_ghost_patch.hpp"
#include "player_ghost_contract.hpp"

namespace mgscoop::player_ghost {
namespace {
SRWLOCK lock=SRWLOCK_INIT;
void* owned_body=nullptr;
constexpr short original_branch=0x0d75;
constexpr short replacement_branch=static_cast<short>(0x9066);
}
bool Install(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !log || body_rva>span || sizeof(expected_body)>span-body_rva) return false;
    const DWORD incoming=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {
        DWORD error;
        ~Finally() { ReleaseSRWLockExclusive(&lock); SetLastError(error); }
    } finally{incoming};
    auto* body=static_cast<unsigned char*>(verified_base)+body_rva;
    auto* branch=reinterpret_cast<volatile short*>(body+branch_offset);
    unsigned char current[sizeof(expected_body)]{};
    SIZE_T read=0;
    if((reinterpret_cast<std::uintptr_t>(branch)&1) ||
       !ReadProcessMemory(GetCurrentProcess(),body,current,sizeof(current),&read) || read!=sizeof(current)) {
        log("player_ghost=refused; reason=body_unreadable_or_alignment\r\n"); return false;
    }
    if(owned_body==body) {
        current[branch_offset]=0x75; current[branch_offset+1]=0x0d;
        short actual=0;
        if(!ReadProcessMemory(GetCurrentProcess(),const_cast<short*>(branch),&actual,sizeof(actual),&read) ||
           read!=sizeof(actual) || actual!=replacement_branch || std::memcmp(current,expected_body,sizeof(current))) {
            log("player_ghost=refused; reason=owned_patch_changed\r\n"); return false;
        }
        return true;
    }
    if(owned_body || std::memcmp(current,expected_body,sizeof(current))) {
        log("player_ghost=refused; reason=body_mismatch\r\n"); return false;
    }
    DWORD protection=0;
    if(!VirtualProtect(const_cast<short*>(branch),2,PAGE_EXECUTE_READWRITE,&protection)) {
        log("player_ghost=refused; reason=page_protection\r\n"); return false;
    }
    const bool changed=_InterlockedCompareExchange16(branch,replacement_branch,original_branch)==original_branch;
    const bool flushed=changed && FlushInstructionCache(GetCurrentProcess(),const_cast<short*>(branch),2);
    DWORD ignored=0;
    if(!flushed && changed) {
        _InterlockedCompareExchange16(branch,original_branch,replacement_branch);
        FlushInstructionCache(GetCurrentProcess(),const_cast<short*>(branch),2);
    }
    const bool restored=VirtualProtect(const_cast<short*>(branch),2,protection,&ignored)!=0;
    if(changed && flushed) owned_body=body;
    if(!changed || !flushed || !restored) {
        log("player_ghost=failed; reason=exchange_flush_or_protection_restore\r\n"); return false;
    }
    log("player_ghost=installed; branch_rva=9c176a bytes=2 retained_checks=4 disk_writes=0\r\n");
    return true;
}
}
