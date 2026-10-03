// Adapted from Dynamite's soldier action-task and CP-member branch changes,
// c90f252f8200765f66c005d6bf4357fda0c0d98e, GPLv3 + Commons Clause 1.0.
// Changes: full current-build body gates, scene-thread activation/restoration,
// single-byte atomic exchanges and explicit partial-failure recovery.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include "mgscoop/soldier_patch.hpp"
#include "soldier_contract.hpp"

namespace mgscoop::soldier_patch {
namespace {
using namespace soldier_contract;
struct State {bool active=false;bool flush_pending=false;DWORD protection=0;};
State states[2];
void* owner=nullptr;
std::size_t image_span=0;
DWORD owner_thread=0;
void (*logger)(const char*) noexcept=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
bool Match(const Body& body,std::size_t branch=1024,bool active=false) noexcept {
    unsigned char bytes[1024]{};SIZE_T read=0;
    if(body.size>sizeof(bytes) || body.rva>image_span || body.size>image_span-body.rva ||
       !ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(owner)+body.rva,bytes,body.size,&read) ||
       read!=body.size) return false;
    if(branch<body.size) {
        if(bytes[branch]!=(active ? 0xeb:0x74)) return false;
        bytes[branch]=0x74;
    }
    return std::memcmp(bytes,body.bytes,body.size)==0;
}
bool Apply(unsigned index,bool enabled) noexcept {
    const auto& rule=rules[index];auto& state=states[index];
    if(!Match(rule.body,rule.branch,state.active)) {
        logger("soldier_rules=refused; reason=branch_body_changed\r\n");return false;
    }
    auto* opcode=reinterpret_cast<volatile char*>(static_cast<unsigned char*>(owner)+rule.body.rva+rule.branch);
    DWORD old=0,ignored=0;
    bool exchanged=true;
    if(enabled!=state.active) {
        if(!VirtualProtect(const_cast<char*>(opcode),1,PAGE_EXECUTE_READWRITE,&old)) return false;
        if(!state.protection) state.protection=old;
        const char before=static_cast<char>(state.active ? 0xeb:0x74);
        const char after=static_cast<char>(enabled ? 0xeb:0x74);
        exchanged=_InterlockedCompareExchange8(opcode,after,before)==before;
        if(exchanged) {
            // Track actual bytes even if flushing/protection restoration fails.
            state.active=enabled;
            state.flush_pending=true;
        }
    }
    bool flushed=true;
    if(state.flush_pending) {
        flushed=FlushInstructionCache(GetCurrentProcess(),const_cast<char*>(opcode),1)!=0;
        if(flushed) state.flush_pending=false;
    }
    const bool restored=!state.protection || VirtualProtect(const_cast<char*>(opcode),1,state.protection,&ignored)!=0;
    return exchanged && flushed && restored;
}
bool Restore() noexcept {
    // Do not short circuit: attempt the second restoration if the first fails.
    const bool first=Apply(0,false),second=Apply(1,false);
    const bool ok=first && second;
    logger(ok ? "soldier_rules=inactive; original_branches_restored=1\r\n"
              : "soldier_rules=restore_failed; original_branches_not_confirmed=1\r\n");
    return ok;
}
}
bool AdoptMissionThread(std::uint32_t previous_thread) noexcept {
    const DWORD incoming=GetLastError();AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(!owner) return true;
    const auto current=GetCurrentThreadId();
    if(!previous_thread || (owner_thread!=previous_thread && owner_thread!=current)) return false;
    owner_thread=current; // Never clear active/pending byte ownership here.
    return true;
}
bool Activate(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(owner && (owner!=base || owner_thread!=GetCurrentThreadId() || image_span!=span)) return false;
    if(!owner) {owner=base;image_span=span;owner_thread=GetCurrentThreadId();logger=log;}
    // Validate every dependency and both branches before changing either byte.
    bool valid=true;
    for(const auto& body:support) valid=Match(body) && valid;
    for(unsigned i=0;i<2;++i) valid=Match(rules[i].body,rules[i].branch,states[i].active) && valid;
    if(!valid) {log("soldier_rules=refused; reason=body_contract\r\n");return false;}
    if(!Apply(0,true) || !Apply(1,true)) {
        log("soldier_rules=activation_failed; rollback_requested=1\r\n");
        Restore();return false;
    }
    log("soldier_rules=active; normal_cp_dispatch=1 action_suppression_bypassed=1 disk_writes=0\r\n");
    return true;
}
bool Deactivate() noexcept {
    const DWORD incoming=GetLastError();AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(!owner) return true;
    if(owner_thread!=GetCurrentThreadId()) {
        logger("soldier_rules=restore_refused; reason=owner_thread\r\n");return false;
    }
    return Restore();
}
}
