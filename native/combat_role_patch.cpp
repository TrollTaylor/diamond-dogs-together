// Adapted from Dynamite controller and hostage host-condition bypasses, commit
// c90f252f8200765f66c005d6bf4357fda0c0d98e, GPLv3 + Commons Clause 1.0.
// Changes: exact caller/session scoping, original forwarding elsewhere,
// current-build full-body gates and owned-detour composition with respawn.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/combat_role_patch.hpp"
#include "combat_role_contract.hpp"

namespace mgscoop::combat_role {
namespace {
using RoleQuery=bool (*)(void*);
RoleQuery original=nullptr;
void (*logger)(const char*) noexcept=nullptr;
std::atomic<unsigned> overrides[3]{};
std::atomic<bool> active{false};
std::atomic<std::uintptr_t> session{0},installed{0};
std::uintptr_t image=0;
unsigned char owned_body[sizeof(combat_role_contract::role_body)]{};
SRWLOCK lock=SRWLOCK_INIT;
bool Read(std::uintptr_t base,std::size_t span,std::uintptr_t rva,void* out,std::size_t size) noexcept {
    if(!base || rva>span || size>span-rva) return false;
    SIZE_T read=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+rva),out,size,&read) && read==size;
}
bool MatchCaller(std::uintptr_t base,std::size_t span,const combat_role_contract::Body& body) noexcept {
    if(body.size>4096) return false;
    unsigned char bytes[4096]{},digest[32]{};
    if(!Read(base,span,body.rva,bytes,body.size)) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,bytes,static_cast<ULONG>(body.size),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok && std::memcmp(digest,body.sha256,sizeof(digest))==0;
}
__declspec(noinline) bool Query(void* context) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(!active.load(std::memory_order_acquire)) return original(context);
    const auto owned=session.load(std::memory_order_acquire);
    if(!owned || reinterpret_cast<std::uintptr_t>(context)!=owned+0x20) return original(context);
    const unsigned site=caller==image+combat_role_contract::sight_return ? 0u :
        caller==image+combat_role_contract::damage_return ? 1u :
        caller==image+combat_role_contract::hostage_return ? 2u : 3u;
    if(site<3) {
        const bool result=original(context);
        if(!result) {
            if(overrides[site].fetch_add(1,std::memory_order_relaxed)==0 && logger) {
                const DWORD error=GetLastError();
                const char* const receipts[]={
                    "combat_role=override_observed; site=sight original_result=0 scoped_result=1\r\n",
                    "combat_role=override_observed; site=damage original_result=0 scoped_result=1\r\n",
                    "combat_role=override_observed; site=hostage original_result=0 scoped_result=1\r\n"};
                logger(receipts[site]);
                SetLastError(error);
            }
        }
        return true;
    }
    return original(context);
}
}
void SetActive(bool enabled) noexcept {active.store(enabled,std::memory_order_release);}
void BindSession(std::uintptr_t value) noexcept {session.store(value,std::memory_order_release);}
bool HasActiveOwnedSession() noexcept {
    return active.load(std::memory_order_acquire) && session.load(std::memory_order_acquire)!=0;
}
bool InstalledFor(void* base) noexcept {
    return base && installed.load(std::memory_order_acquire)==reinterpret_cast<std::uintptr_t>(base);
}
bool MatchesOwnedBody(void* base,std::size_t span) noexcept {
    if(!InstalledFor(base)) return false;
    unsigned char actual[sizeof(owned_body)]{};
    return Read(reinterpret_cast<std::uintptr_t>(base),span,combat_role_contract::role_rva,actual,sizeof(actual)) &&
        std::memcmp(actual,owned_body,sizeof(actual))==0;
}
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();
    AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    if(installed.load(std::memory_order_acquire)) return InstalledFor(base) && MatchesOwnedBody(base,span);
    const auto candidate=reinterpret_cast<std::uintptr_t>(base);
    using namespace combat_role_contract;
    unsigned char before[sizeof(role_body)]{};
    std::uintptr_t slot=0;
    if(!Read(candidate,span,role_rva,before,sizeof(before)) || std::memcmp(before,role_body,sizeof(before))!=0 ||
       !Read(candidate,span,0x2127068,&slot,sizeof(slot)) || slot!=candidate+role_rva) {
        log("combat_role=refused; reason=role_body_or_slot\r\n"); return false;
    }
    for(const auto& body:callers) if(!MatchCaller(candidate,span,body)) {
        log("combat_role=refused; reason=controller_body\r\n"); return false;
    }
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
       reinterpret_cast<LPCWSTR>(&Install),&pinned)) return false;
    auto* target=reinterpret_cast<void*>(candidate+role_rva);
    image=candidate;
    logger=log;
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Query),reinterpret_cast<void**>(&original))!=MH_OK) return false;
    if(MH_EnableHook(target)!=MH_OK) {
        // Preserve any potentially callable trampoline; policy is inactive.
        log("combat_role=failed; reason=enable inactive_forwarding=1\r\n"); return false;
    }
    // This prologue has sufficient bytes for MinHook's normal five-byte jump;
    // keep an exact owned image for other modules that verify this same body.
    if(!Read(candidate,span,role_rva,owned_body,sizeof(owned_body)) || owned_body[0]!=0xe9 ||
       std::memcmp(owned_body+5,role_body+5,sizeof(role_body)-5)!=0) {
        log("combat_role=failed; reason=owned_detour_shape inactive_forwarding=1\r\n"); return false;
    }
    installed.store(candidate,std::memory_order_release);
    log("combat_role=installed; exact_callers=3 owned_session_only=1 other_host_queries_forwarded=1\r\n");
    return true;
}
}
