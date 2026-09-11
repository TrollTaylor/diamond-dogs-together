// Adapted from Dynamite UiControllerImplSetNoUseEquipIdHook and
// EquipCrossEvCallIsItemNoUseHook, revision
// c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
// Changes: current-executable mapping, complete body/interface gates and scene scope.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/equipment_use_patch.hpp"
#include "mgscoop/player_team_patch.hpp"
#include "equipment_use_contract.hpp"

namespace mgscoop::equipment_use {
namespace {
using Setter=void (*)(void*,bool,unsigned int);
using Predicate=bool (*)(void*,unsigned int);
Setter original=nullptr;
Predicate predicate_original=nullptr;
std::atomic<bool> ready{false};
std::atomic<bool> observed{false};
std::atomic<bool> predicate_observed{false};
void (*logger)(const char*) noexcept=nullptr;
void* installed_base=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
void SetNoUse(void* controller,bool enabled,unsigned int equipment) {
    if(ready.load(std::memory_order_acquire) && player_team::IsActive()) {
        equipment=9999; // Same sentinel for enable and disable as upstream.
        if(!observed.exchange(true,std::memory_order_relaxed)) {
            const DWORD incoming=GetLastError();
            logger("equipment_use=override_observed; restriction_id=9999 gameplay_effect=unverified\r\n");
            SetLastError(incoming);
        }
    }
    original(controller,enabled,equipment);
}
bool IsItemNoUse(void* selector,unsigned int equipment) {
    const bool result=predicate_original(selector,equipment);
    if(!ready.load(std::memory_order_acquire) || !player_team::IsActive()) return result;
    if(result && !predicate_observed.exchange(true,std::memory_order_relaxed)) {
        const DWORD incoming=GetLastError();
        logger("equipment_selection=override_observed; original_refused=1 scoped_refused=0 gameplay_effect=unverified\r\n");
        SetLastError(incoming);
    }
    return false;
}
bool Read(void* base,std::size_t span,std::uintptr_t rva,void* out,std::size_t count) noexcept {
    if(rva>span || count>span-rva) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+rva,out,count,&read) && read==count;
}
bool Match(void* base,std::size_t span,std::uintptr_t rva,const unsigned char* expected,std::size_t count) noexcept {
    unsigned char body[512]{};
    return count<=sizeof(body) && Read(base,span,rva,body,count) && std::memcmp(body,expected,count)==0;
}
}
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error; ~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(installed_base) return installed_base==base;
    using namespace equipment_use_contract;
    if(!Match(base,span,setter_rva,setter_body,sizeof(setter_body)) ||
       !Match(base,span,predicate_rva,predicate_body,sizeof(predicate_body)) ||
       !Match(base,span,selection_caller_rva,selection_caller_body,sizeof(selection_caller_body))) {
        log("equipment_use=refused; reason=setter_predicate_or_caller_body\r\n"); return false;
    }
    for(const auto slot:slots) {
        std::uintptr_t actual=0;
        if(!Read(base,span,slot,&actual,sizeof(actual)) || actual!=reinterpret_cast<std::uintptr_t>(base)+setter_rva) {
            log("equipment_use=refused; reason=setter_interface\r\n"); return false;
        }
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Install),&pinned)) return false;
    void* targets[]={static_cast<unsigned char*>(base)+setter_rva,static_cast<unsigned char*>(base)+predicate_rva};
    void* replacements[]={reinterpret_cast<void*>(&SetNoUse),reinterpret_cast<void*>(&IsItemNoUse)};
    void** originals[]={reinterpret_cast<void**>(&original),reinterpret_cast<void**>(&predicate_original)};
    logger=log;
    for(unsigned i=0;i<2;++i) {
        if(MH_CreateHook(targets[i],replacements[i],originals[i])!=MH_OK) {
            for(unsigned j=0;j<i;++j) MH_RemoveHook(targets[j]);
            log("equipment_use=failed; reason=create_hook\r\n"); return false;
        }
    }
    for(unsigned i=0;i<2;++i) {
        if(MH_EnableHook(targets[i])!=MH_OK) {
            // Retain both trampolines once enabling has been attempted.
            // No partial installation can override either game behavior.
            log("equipment_use=failed; reason=enable_hook inactive_forwarding=1\r\n"); return false;
        }
    }
    installed_base=base;
    ready.store(true,std::memory_order_release);
    log("equipment_use=installed; hooks=2 scene_scope=player_rules sentinel=9999 original_calls_preserved=1\r\n");
    return true;
}
}
