// Adapted from Dynamite's full CloseSession mission-condition bypass,
// c90f252f8200765f66c005d6bf4357fda0c0d98e, GPLv3 + Commons Clause 1.0.
// Changes: current-build full wrapper/accessor gates, owned close-call scope,
// atomic displacement exchange and restoration. All native dispatch stays in
// the original game wrapper; no concrete service method is reimplemented.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cstring>
#include "mgscoop/full_close_patch.hpp"
#include "full_close_contract.hpp"

namespace mgscoop::full_close_patch {
namespace {
using namespace full_close_contract;
SRWLOCK lock=SRWLOCK_INIT;
void* owner=nullptr;
std::size_t image_span=0;
DWORD owner_thread=0,original_protection=0;
bool active=false,flush_pending=false,protection_pending=false;
void (*logger)(const char*) noexcept=nullptr;

template<class T> bool Read(std::uintptr_t address,T& value) noexcept {
    if(address<0x10000 || address>0x00007fffffffffff-sizeof(T)) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(T),&read) && read==sizeof(T);
}
bool Match(void* base,std::size_t span,const Body& body,bool normalize=false) noexcept {
    unsigned char bytes[256]{};SIZE_T read=0;
    if(body.size>sizeof(bytes) || body.rva>span || body.size>span-body.rva ||
       !ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+body.rva,bytes,body.size,&read) || read!=body.size) return false;
    if(normalize) {
        if(bytes[displacement_offset]!=(active ? 0x00:0x13)) return false;
        bytes[displacement_offset]=0x13;
    }
    return std::memcmp(bytes,body.bytes,body.size)==0;
}
bool ImageAddress(std::uintptr_t address,std::size_t size,bool executable) noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(owner);
    if(address<base || address-base>image_span || size>image_span-(address-base)) return false;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       info.Type!=MEM_IMAGE || info.AllocationBase!=owner || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
    const DWORD protection=info.Protect&0xff;
    return !executable || protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
        protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
bool ContextReady(bool& service_present) noexcept {
    const auto base=reinterpret_cast<std::uintptr_t>(owner);
    std::uintptr_t registry=0,registry_table=0,context=0,gm=0,vars=0,service=0,table=0,method=0;
    std::uint16_t mission=0;
    // BFF050's existing registry must be present: do not invoke its allocator
    // or manufacture a context just to make the altered branch available.
    if(!Read(base+0x2c3a8a0,registry) || !registry || !Read(registry,registry_table) || registry_table!=base+0x2308648 ||
       !Read(registry+8,context) || !context || !Read(context+0x98,gm) || !gm ||
       !Read(gm+0x10,vars) || vars!=base+0x2c01360 || !Read(vars+0x12,mission) || mission!=30050 ||
       !Read(gm+0x190,service)) return false;
    if(service && (!Read(service,table) || !ImageAddress(table,0x20,false) ||
                   !Read(table+0x18,method) || !ImageAddress(method,1,true))) return false;
    // Structural checks do not identify the concrete service type or prove a
    // lifetime lease. The original wrapper, not this module, dispatches it.
    std::uintptr_t registry_after=0,context_after=0,gm_after=0,service_after=0;
    if(!Read(base+0x2c3a8a0,registry_after) || registry_after!=registry ||
       !Read(registry+8,context_after) || context_after!=context ||
       !Read(context+0x98,gm_after) || gm_after!=gm ||
       !Read(gm+0x190,service_after) || service_after!=service) return false;
    service_present=service!=0;
    return true;
}
bool Apply(bool enabled) noexcept {
    if(!Match(owner,image_span,bodies[0],true)) {
        logger("full_close=refused; reason=wrapper_changed\r\n");return false;
    }
    auto* byte=reinterpret_cast<volatile char*>(static_cast<unsigned char*>(owner)+wrapper_rva+displacement_offset);
    bool exchanged=true;
    DWORD old=0,ignored=0;
    if(enabled!=active) {
        if(!VirtualProtect(const_cast<char*>(byte),1,PAGE_EXECUTE_READWRITE,&old)) return false;
        if(!original_protection) original_protection=old;
        protection_pending=true;
        const char before=static_cast<char>(active ? 0x00:0x13);
        const char after=static_cast<char>(enabled ? 0x00:0x13);
        exchanged=_InterlockedCompareExchange8(byte,after,before)==before;
        if(exchanged) {active=enabled;flush_pending=true;}
    }
    bool flushed=true,restored=true;
    if(flush_pending) {
        flushed=FlushInstructionCache(GetCurrentProcess(),const_cast<char*>(byte),1)!=0;
        if(flushed) flush_pending=false;
    }
    if(protection_pending) {
        restored=VirtualProtect(const_cast<char*>(byte),1,original_protection,&ignored)!=0;
        if(restored) protection_pending=false;
    }
    return exchanged && flushed && restored;
}
bool Restore() noexcept {
    if(!active && !flush_pending && !protection_pending) return true;
    const bool ok=Apply(false);
    logger(ok ? "full_close=inactive; original_mission_branch_restored=1\r\n"
              : "full_close=restore_failed; pending_owned_restoration=1\r\n");
    return ok;
}
}
bool Activate(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(owner && (owner!=base || image_span!=span || owner_thread!=GetCurrentThreadId())) return false;
    if(active || flush_pending || protection_pending) return false;
    for(const auto& body:bodies) if(!Match(base,span,body)) {
        log("full_close=refused; reason=wrapper_or_accessor_contract\r\n");return false;
    }
    if(!owner) {owner=base;image_span=span;owner_thread=GetCurrentThreadId();logger=log;}
    bool service_present=false;
    if(!ContextReady(service_present)) {
        log("full_close=refused; reason=existing_native_context\r\n");return false;
    }
    if(!Apply(true)) {
        log("full_close=activation_failed; rollback_requested=1\r\n");
        Restore();return false;
    }
    log(service_present ? "full_close=armed; original_service_dispatch_selected=1 execution_unverified=1\r\n"
                        : "full_close=armed; original_null_service_fallback_selected=1 execution_unverified=1\r\n");
    return true;
}
bool Deactivate() noexcept {
    const DWORD incoming=GetLastError();AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(!owner) return true;
    if(owner_thread!=GetCurrentThreadId()) {
        logger("full_close=restore_refused; reason=owner_thread\r\n");return false;
    }
    return Restore();
}
}
