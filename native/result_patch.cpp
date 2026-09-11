// Adapted from Dynamite's game-over and FOB mission-result submission bypasses,
// c90f252f8200765f66c005d6bf4357fda0c0d98e, GPLv3 + Commons Clause 1.0.
// Changes: shared submit hook, two exact callers, full-body gates and a co-op
// latch that survives teardown and resets before the next Mission.Start.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include <MinHook.h>
#include "mgscoop/result_patch.hpp"
#include "result_contract.hpp"

namespace mgscoop::result_patch {
namespace {
using Submit=bool (*)(void*,bool,unsigned int,unsigned int);
Submit original=nullptr;
std::atomic<bool> suppressed{false},observed[2]{};
std::atomic<std::uintptr_t> installed{0};
std::uintptr_t image=0;
void (*logger)(const char*) noexcept=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
__declspec(noinline) bool Request(void* service,bool completed,unsigned int value,unsigned int members) {
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(suppressed.load(std::memory_order_acquire)) {
        const unsigned site=caller==image+result_contract::death_return ? 0u :
            caller==image+result_contract::mission_return ? 1u : 2u;
        if(site<2) {
            const DWORD error=GetLastError();
            if(!observed[site].exchange(true,std::memory_order_relaxed))
                logger(site==0 ? "result_submission=suppressed; site=game_over mapped_call_skipped=1\r\n"
                               : "result_submission=suppressed; site=mission_result mapped_call_skipped=1\r\n");
            SetLastError(error);
            // Neither mapped caller consumes the result. False does not claim
            // an online submission succeeded or mark any native queue complete.
            return false;
        }
    }
    return original(service,completed,value,members);
}
bool Match(void* base,std::size_t span,const result_contract::Body& body) noexcept {
    unsigned char bytes[4096]{},digest[32]{};SIZE_T read=0;
    if(body.size>sizeof(bytes) || body.rva>span || body.size>span-body.rva ||
       !ReadProcessMemory(GetCurrentProcess(),static_cast<unsigned char*>(base)+body.rva,bytes,body.size,&read) || read!=body.size) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,bytes,static_cast<ULONG>(body.size),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    return ok && std::memcmp(digest,body.sha256,sizeof(digest))==0;
}
}
void BeginCoopScene() noexcept {
    if(installed.load(std::memory_order_acquire)) suppressed.store(true,std::memory_order_release);
}
void BeginNextMission() noexcept {suppressed.store(false,std::memory_order_release);}
bool Install(void* base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!base || !log) return false;
    const DWORD incoming=GetLastError();AcquireSRWLockExclusive(&lock);
    struct Finally {DWORD error;~Finally(){ReleaseSRWLockExclusive(&lock);SetLastError(error);}} finally{incoming};
    const auto candidate=reinterpret_cast<std::uintptr_t>(base);
    if(installed.load(std::memory_order_acquire)) return installed.load(std::memory_order_relaxed)==candidate;
    for(const auto& body:result_contract::bodies) if(!Match(base,span,body)) {
        log("result_submission=refused; reason=body_contract\r\n");return false;
    }
    std::uintptr_t slot=0;SIZE_T read=0;
    if(0x255a3e8+sizeof(slot)>span ||
       !ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(candidate+0x255a3e8),&slot,sizeof(slot),&read) ||
       read!=sizeof(slot) || slot!=candidate+0x1e0e950) return false;
    const auto init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
       reinterpret_cast<LPCWSTR>(&Install),&pinned)) return false;
    image=candidate;logger=log;
    auto* target=reinterpret_cast<void*>(candidate+result_contract::submit_rva);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&Request),reinterpret_cast<void**>(&original))!=MH_OK) return false;
    if(MH_EnableHook(target)!=MH_OK) {
        log("result_submission=failed; reason=enable_hook inactive_forwarding=1\r\n");return false;
    }
    installed.store(candidate,std::memory_order_release);
    log("result_submission=installed; exact_callers=2 inactive_forwarding=1\r\n");return true;
}
}
