// Original bounded allocation bridge. No game addresses or automatic install.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstring>
#include "MinHook.h"
#include "mgscoop/allocation_bridge.hpp"

namespace mgscoop::allocation_bridge {
namespace {
SRWLOCK install_lock = SRWLOCK_INIT;
std::atomic<Allocate> original{};
std::atomic<unsigned> active{};
std::atomic<unsigned> scopes{};
Allocate target{};
Plan plan{}; // Immutable while callers can enter the bridge.
thread_local bool scope_active = false;
thread_local bool dispatch_active = false;

struct Enter {
    bool previous = dispatch_active;
    Enter() noexcept { active.fetch_add(1, std::memory_order_acq_rel); dispatch_active = true; }
    ~Enter() { dispatch_active = previous; active.fetch_sub(1, std::memory_order_release); }
};

void* Dispatch(std::uint64_t size, std::uint64_t alignment, std::uint32_t tag) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const DWORD error = GetLastError();
    Enter entered;
    Allocate selected = original.load(std::memory_order_acquire);
    if(scope_active && !entered.previous) {
        for(const auto& site : plan.sites) {
            if(caller == site.return_address && size == site.size &&
               alignment == site.alignment && tag == site.tag) {
                selected = plan.heap;
                break;
            }
        }
    }
    SetLastError(error);
    // Successful install publishes the original before enabling the hook;
    // removal requires stopped callers. Never retry a null allocation result.
    return selected ? selected(size,alignment,tag) : nullptr;
}
}

Scope::Scope() noexcept {
    AcquireSRWLockShared(&install_lock);
    if(!scope_active && original.load(std::memory_order_acquire)) {
        scopes.fetch_add(1, std::memory_order_acq_rel);
        scope_active = true;
        acquired_ = true;
    }
    ReleaseSRWLockShared(&install_lock);
}
Scope::~Scope() {
    if(acquired_) {
        scope_active = false;
        scopes.fetch_sub(1, std::memory_order_release);
    }
}

InstallResult Install(Allocate requested, const unsigned char* expected,
                      std::size_t count, const Plan& requested_plan) noexcept {
    if(!requested || !expected || count<14 || count>96 || !requested_plan.heap ||
       requested_plan.heap==requested || requested_plan.sites[0].return_address==requested_plan.sites[1].return_address)
        return InstallResult::invalid;
    for(const auto& site:requested_plan.sites)
        if(!site.return_address || !site.size || !site.alignment ||
           (site.alignment & (site.alignment-1))) return InstallResult::invalid;
    AcquireSRWLockExclusive(&install_lock);
    auto result=InstallResult::hook_failed;
    if(target) result=InstallResult::already_installed;
    else {
        unsigned char bytes[96]{};
        SIZE_T read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(requested),bytes,count,&read) ||
           read!=count || std::memcmp(bytes,expected,count)!=0) result=InstallResult::bytes_differ;
        else {
            const auto initialized=MH_Initialize();
            if(initialized==MH_OK || initialized==MH_ERROR_ALREADY_INITIALIZED) {
                void* trampoline=nullptr;
                if(MH_CreateHook(reinterpret_cast<void*>(requested),reinterpret_cast<void*>(&Dispatch),&trampoline)==MH_OK) {
                    plan=requested_plan;
                    target=requested;
                    original.store(reinterpret_cast<Allocate>(trampoline),std::memory_order_release);
                    if(MH_EnableHook(reinterpret_cast<void*>(target))==MH_OK) result=InstallResult::installed;
                    else if(MH_RemoveHook(reinterpret_cast<void*>(target))==MH_OK) {
                        original.store(nullptr,std::memory_order_release); target=nullptr; plan={};
                    }
                    // Failed removal retains ownership and immutable state.
                }
            }
        }
    }
    ReleaseSRWLockExclusive(&install_lock);
    return result;
}

bool RemoveAfterCallersStopped() noexcept {
    AcquireSRWLockExclusive(&install_lock);
    bool removed=false;
    if(target && active.load(std::memory_order_acquire)==0 && scopes.load(std::memory_order_acquire)==0) {
        const auto disabled=MH_DisableHook(reinterpret_cast<void*>(target));
        if((disabled==MH_OK || disabled==MH_ERROR_DISABLED) &&
           MH_RemoveHook(reinterpret_cast<void*>(target))==MH_OK) {
            original.store(nullptr,std::memory_order_release); target=nullptr; plan={}; removed=true;
        }
    }
    ReleaseSRWLockExclusive(&install_lock);
    return removed;
}
}
