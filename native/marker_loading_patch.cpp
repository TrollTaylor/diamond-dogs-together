// Adapted from Dynamite c90f252f8200765f66c005d6bf4357fda0c0d98e,
// GPLv3 + Commons Clause 1.0. Exact-build mapping and lifecycle gates are new.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/marker_loading_patch.hpp"
#include "marker_loading_contract.hpp"
namespace mgscoop::marker_loading {
namespace {
using Fixed=void (*)(void*,const void*);
using Follow=bool (*)(void*,const void*,unsigned short);
using Remove=void (*)(void*,unsigned int);
Fixed fixed_original=nullptr;
Follow follow_original=nullptr;
Remove remove_original=nullptr;
std::atomic<bool> active{false};
void* installed_base=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
void FixedHook(void* self,const void* pos) {if(!active.load(std::memory_order_acquire)) fixed_original(self,pos);}
bool FollowHook(void* self,const void* pos,unsigned short id) {
    return active.load(std::memory_order_acquire) ? true : follow_original(self,pos,id);
}
void RemoveHook(void* self,unsigned int index) {if(!active.load(std::memory_order_acquire)) remove_original(self,index);}
bool Read(void* base,std::size_t span,std::uintptr_t rva,void* output,std::size_t size) noexcept {
    if(rva>span || size>span-rva)return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+rva,output,size,&read) && read==size;
}
bool Match(void* base,std::size_t span,std::uintptr_t rva,const unsigned char* expected,std::size_t size) noexcept {
    unsigned char bytes[512]{};
    return size<=sizeof(bytes) && Read(base,span,rva,bytes,size) && std::memcmp(bytes,expected,size)==0;
}
}
bool Begin(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log)return false;
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{error};
    if(installed_base) {
        if(installed_base!=base)return false;
        active.store(true,std::memory_order_release);return true;
    }
    using namespace marker_loading_contract;
    if(!Match(base,span,fixed_rva,fixed_body,sizeof(fixed_body)) ||
       !Match(base,span,follow_rva,follow_body,sizeof(follow_body)) ||
       !Match(base,span,remove_rva,remove_body,sizeof(remove_body))) {
        log("marker_loading=refused; reason=method_body\r\n");return false;
    }
    for(const auto& slot:slots) {
        std::uintptr_t actual=0;
        if(!Read(base,span,slot.rva,&actual,sizeof(actual)) || actual!=reinterpret_cast<std::uintptr_t>(base)+slot.target) {
            log("marker_loading=refused; reason=interface_entry\r\n");return false;
        }
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED)return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
       reinterpret_cast<LPCWSTR>(&Begin),&pinned))return false;
    void* targets[]={static_cast<unsigned char*>(base)+fixed_rva,static_cast<unsigned char*>(base)+follow_rva,static_cast<unsigned char*>(base)+remove_rva};
    void* hooks[]={reinterpret_cast<void*>(&FixedHook),reinterpret_cast<void*>(&FollowHook),reinterpret_cast<void*>(&RemoveHook)};
    void** originals[]={reinterpret_cast<void**>(&fixed_original),reinterpret_cast<void**>(&follow_original),reinterpret_cast<void**>(&remove_original)};
    for(unsigned i=0;i<3;++i) {
        if(MH_CreateHook(targets[i],hooks[i],originals[i])!=MH_OK) {
            for(unsigned j=0;j<i;++j)MH_RemoveHook(targets[j]);
            log("marker_loading=failed; reason=create_hook\r\n");return false;
        }
    }
    for(unsigned i=0;i<3;++i) {
        if(MH_EnableHook(targets[i])!=MH_OK) {
            log("marker_loading=failed; reason=enable_hook inactive_forwarding=1\r\n");return false;
        }
    }
    installed_base=base;
    active.store(true,std::memory_order_release);
    log("marker_loading=installed; hooks=3 guard=active shared_marker_sync=0\r\n");return true;
}
void End() noexcept {active.store(false,std::memory_order_release);}
bool IsActive() noexcept {return active.load(std::memory_order_acquire);}
}
