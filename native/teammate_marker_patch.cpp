// Adapted from Dynamite GetUiMarkerTypeFromSystemType2Hook, unknown321,
// c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
// Full terms: third_party/dynamite/LICENSE. Changes: retail executable mapping,
// complete code/table gates and active owned-session scope.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/teammate_marker_patch.hpp"
#include "mgscoop/combat_role_patch.hpp"
#include "teammate_marker_contract.hpp"

namespace mgscoop::teammate_marker {
namespace {
using Converter=unsigned int (*)(void*,void*);
Converter original=nullptr;
std::atomic<bool> ready{false},observed{false};
void (*logger)(const char*) noexcept=nullptr;
void* installed_base=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
unsigned int Convert(void* owner,void* record) {
    // Some original cases consume record flags; preserve those side effects.
    const unsigned int result=original(owner,record);
    if(result!=teammate_marker_contract::fob_enemy || !ready.load(std::memory_order_acquire) ||
       !combat_role::HasActiveOwnedSession()) return result;
    if(!observed.exchange(true,std::memory_order_relaxed)) {
        const DWORD error=GetLastError();
        logger("teammate_marker=remap_observed; original=0x200005 replacement=0x1c rendered_effect=unverified\r\n");
        SetLastError(error);
    }
    return teammate_marker_contract::buddy;
}
bool Read(void* base,std::size_t span,std::uintptr_t rva,void* out,std::size_t size) noexcept {
    if(rva>span || size>span-rva) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+rva,out,size,&read) && read==size;
}
bool Match(void* base,std::size_t span,const teammate_marker_contract::Body& body) noexcept {
    unsigned char bytes[4096]{},digest[32]{};
    if(body.size>sizeof(bytes) || !Read(base,span,body.rva,bytes,body.size)) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,bytes,static_cast<ULONG>(body.size),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok && std::memcmp(digest,body.sha256,sizeof(digest))==0;
}
}
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD error=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{error};
    if(installed_base) return installed_base==base;
    using namespace teammate_marker_contract;
    for(const auto& body:bodies) {
        if(!Match(base,span,body)) {
            log("teammate_marker=refused; reason=converter_owner_or_caller_body\r\n"); return false;
        }
    }
    std::uintptr_t slot=0;
    if(!Read(base,span,converter_slot,&slot,sizeof(slot)) || slot!=reinterpret_cast<std::uintptr_t>(base)+converter_rva) {
        log("teammate_marker=refused; reason=converter_interface\r\n"); return false;
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&Install),&pinned)) return false;
    void* target=static_cast<unsigned char*>(base)+converter_rva;
    logger=log;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Convert),reinterpret_cast<void**>(&original))!=MH_OK) {
        log("teammate_marker=failed; reason=create_hook\r\n"); return false;
    }
    if(MH_EnableHook(target)!=MH_OK) {
        // Retain the trampoline after any enable attempt; the policy stays off.
        log("teammate_marker=failed; reason=enable_hook inactive_forwarding=1\r\n"); return false;
    }
    installed_base=base;
    ready.store(true,std::memory_order_release);
    log("teammate_marker=installed; owned_session_required=1 original_call_preserved=1\r\n");
    return true;
}
}
