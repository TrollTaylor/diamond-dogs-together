// Adapted from Dynamite's three local/host trap return patches, revision
// c90f252f8200765f66c005d6bf4357fda0c0d98e, GPLv3 + Commons Clause 1.0.
// Changes: current-build named callback mapping, complete body/table gates,
// original callback execution, and active owned-session scope.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/trap_patch.hpp"
#include "mgscoop/combat_role_patch.hpp"
#include "trap_contract.hpp"

namespace mgscoop::trap_patch {
namespace {
using Predicate=bool (*)(void*,void*);
Predicate originals[2]{};
std::atomic<bool> ready{false};
std::atomic<bool> observed[2]{};
void (*logger)(const char*) noexcept=nullptr;
void* installed_base=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
bool Result(unsigned site,void* context,void* object) {
    const bool result=originals[site](context,object);
    if(!ready.load(std::memory_order_acquire) || !combat_role::HasActiveOwnedSession()) return result;
    if(!result && !observed[site].exchange(true,std::memory_order_relaxed)) {
        const DWORD error=GetLastError();
        logger(site==0 ? "trap_rule=override_observed; predicate=local original_result=0 scoped_result=1\r\n"
                       : "trap_rule=override_observed; predicate=host original_result=0 scoped_result=1\r\n");
        SetLastError(error);
    }
    return true;
}
bool Local(void* context,void* object) {return Result(0,context,object);}
bool Host(void* context,void* object) {return Result(1,context,object);}
bool Read(void* base,std::size_t span,std::uintptr_t rva,void* out,std::size_t size) noexcept {
    if(rva>span || size>span-rva) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+rva,out,size,&read) && read==size;
}
bool Match(void* base,std::size_t span,std::uintptr_t rva,const unsigned char* expected,std::size_t size) noexcept {
    unsigned char bytes[256]{};
    return size<=sizeof(bytes) && Read(base,span,rva,bytes,size) && std::memcmp(bytes,expected,size)==0;
}
}
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(installed_base) return installed_base==base;
    using namespace trap_contract;
    if(!Match(base,span,local_rva,local_body,sizeof(local_body)) ||
       !Match(base,span,host_rva,host_body,sizeof(host_body))) {
        log("trap_rule=refused; reason=callback_body\r\n"); return false;
    }
    for(const auto& slot:slots) {
        std::uintptr_t actual=0;
        if(!Read(base,span,slot.rva,&actual,sizeof(actual)) || actual!=reinterpret_cast<std::uintptr_t>(base)+slot.target) {
            log("trap_rule=refused; reason=callback_table\r\n"); return false;
        }
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
       reinterpret_cast<LPCWSTR>(&Install),&pinned)) return false;
    void* targets[]={static_cast<unsigned char*>(base)+local_rva,static_cast<unsigned char*>(base)+host_rva};
    void* replacements[]={reinterpret_cast<void*>(&Local),reinterpret_cast<void*>(&Host)};
    logger=log;
    for(unsigned i=0;i<2;++i) {
        if(MH_CreateHook(targets[i],replacements[i],reinterpret_cast<void**>(&originals[i]))!=MH_OK) {
            for(unsigned j=0;j<i;++j) MH_RemoveHook(targets[j]);
            log("trap_rule=failed; reason=create_hook\r\n"); return false;
        }
    }
    for(unsigned i=0;i<2;++i) {
        if(MH_EnableHook(targets[i])!=MH_OK) {
            // A forwarding callback may be in flight; retain both trampolines.
            // ready remains false, so a partial installation cannot override.
            log("trap_rule=failed; reason=enable_hook inactive_forwarding=1\r\n"); return false;
        }
    }
    installed_base=base;
    ready.store(true,std::memory_order_release);
    log("trap_rule=installed; callbacks=2 owned_session_required=1 original_calls_preserved=1\r\n");
    return true;
}
}
