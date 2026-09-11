#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <malloc.h>
#include <array>
#include <atomic>
#include <cstring>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#include "mgscoop/allocation_bridge.hpp"

using namespace mgscoop::allocation_bridge;
// Separate translation unit: the compiler must honor the public ABI instead
// of making interprocedural assumptions about the function we detour.
void* Original(std::uint64_t, std::uint64_t, std::uint32_t);
namespace {
std::atomic<unsigned> failures{}, checks{}, originals{}, heaps{}, released{};
thread_local std::uintptr_t caller{};
thread_local bool fail_allocation=false, reenter=false;
thread_local bool throw_allocation=false;
thread_local std::uint64_t seen_size{}, seen_alignment{};
thread_local std::uint32_t seen_tag{};
void CheckAt(bool ok, int line) {
    ++checks;
    if(!ok) { ++failures; std::cerr<<"Failed check at line "<<line<<'\n'; }
}
#define Check(ok) CheckAt((ok), __LINE__)
void* Make(std::uint64_t size,std::uint64_t alignment,std::uint32_t tag) {
    Check(GetLastError()==0x1234);
    seen_size=size; seen_alignment=alignment; seen_tag=tag;
    void* result=fail_allocation ? nullptr : _aligned_malloc(static_cast<std::size_t>(size),static_cast<std::size_t>(alignment));
    SetLastError(0x5678);
    return result;
}
__declspec(noinline) void* SiteA(std::uint64_t size=88,std::uint64_t align=8,std::uint32_t tag=115) {
    void* result=Original(size,align,tag);
    // Prevent a tail call: the return address must be inside this caller.
    seen_tag=tag;
    return result;
}
__declspec(noinline) void* SiteB() {
    void* result=Original(144,8,115); seen_tag=115; return result;
}
__declspec(noinline) void* OtherSite() {
    void* result=Original(88,8,115); seen_tag=115; return result;
}
void Free(void* p) { if(p) { _aligned_free(p); ++released; } }
void* Heap(std::uint64_t size,std::uint64_t alignment,std::uint32_t tag) {
    ++heaps;
    if(throw_allocation) throw 42u;
    if(reenter) {
        reenter=false;
        SetLastError(0x1234);
        Free(SiteA()); // Even a selected origin must bypass routing on reentry.
        SetLastError(0x1234);
    }
    return Make(size,alignment,tag);
}
void InvokeA(bool heap,std::uint64_t size=88,std::uint64_t align=8,std::uint32_t tag=115) {
    const auto before=heaps.load();
    SetLastError(0x1234);
    void* p=SiteA(size,align,tag);
    Check(GetLastError()==0x5678);
    Check(seen_size==size && seen_alignment==align && seen_tag==tag);
    Check(heaps.load()==before+(heap ? 1u : 0u));
    Check((p==nullptr)==fail_allocation);
    if(p) Check(reinterpret_cast<std::uintptr_t>(p)%align==0);
    Free(p);
}
}

void AllocationFixtureRecord(std::uintptr_t address) { caller=address; ++originals; }
void* AllocationFixtureMake(std::uint64_t size,std::uint64_t alignment,std::uint32_t tag) {
    return Make(size,alignment,tag);
}

int main() {
    { Scope no_install; Check(!no_install.acquired()); }
    SetLastError(0x1234); Free(SiteA()); const auto a=caller;
    SetLastError(0x1234); Free(SiteB()); const auto b=caller;
    Check(a!=b);
    Plan plan{Heap,{{a,88,8,115},{b,144,8,115}}};
    std::array<unsigned char,32> bytes{};
    std::memcpy(bytes.data(),reinterpret_cast<void*>(&Original),bytes.size());
    auto wrong=bytes; wrong[0]^=1;
    Check(Install(Original,wrong.data(),wrong.size(),plan)==InstallResult::bytes_differ);
    Check(std::memcmp(bytes.data(),reinterpret_cast<void*>(&Original),bytes.size())==0);
    auto invalid=plan; invalid.sites[1]=invalid.sites[0];
    Check(Install(Original,bytes.data(),bytes.size(),invalid)==InstallResult::invalid);
    Check(Install(Original,bytes.data(),bytes.size(),plan)==InstallResult::installed);
    Check(Install(Original,bytes.data(),bytes.size(),plan)==InstallResult::already_installed);
    InvokeA(false);
    {
        Scope scope; Check(scope.acquired());
        Check(!RemoveAfterCallersStopped());
        InvokeA(true);
        { Scope nested; Check(!nested.acquired()); InvokeA(true); }
        InvokeA(false,89); InvokeA(false,88,16); InvokeA(false,88,8,116);
        auto before=heaps.load(); SetLastError(0x1234); Free(OtherSite()); Check(heaps.load()==before);
        before=heaps.load(); SetLastError(0x1234); Free(SiteB()); Check(heaps.load()==before+1);
        fail_allocation=true; InvokeA(true); fail_allocation=false;
        reenter=true; const auto before_original=originals.load(); InvokeA(true);
        Check(originals.load()==before_original+1);
        std::thread outside([] { InvokeA(false); }); outside.join();
    }
    InvokeA(false);
    // A quiet scope on another thread must also retain the trampoline.
    std::promise<void> scope_ready, release_scope;
    auto ready=scope_ready.get_future(), release=release_scope.get_future();
    std::thread held_scope([&] {
        Scope scope; Check(scope.acquired());
        scope_ready.set_value();
        release.wait();
        InvokeA(true);
    });
    ready.wait();
    Check(!RemoveAfterCallersStopped());
    release_scope.set_value(); held_scope.join();
    // Unwinding must release both dispatch recursion state and the scope.
    bool caught=false;
    try {
        Scope scope; Check(scope.acquired()); throw_allocation=true;
        SetLastError(0x1234); Free(SiteA());
    } catch(unsigned value) { caught=(value==42u); }
    throw_allocation=false; Check(caught);
    { Scope scope; Check(scope.acquired()); InvokeA(true); }
    const auto heap_before=heaps.load(), original_before=originals.load();
    std::vector<std::thread> workers;
    for(unsigned i=0;i<4;++i) workers.emplace_back([i] {
        for(unsigned n=0;n<250;++n) {
            if(i%2==0) { Scope scope; Check(scope.acquired()); SetLastError(0x1234); Free(SiteA()); }
            else { SetLastError(0x1234); Free(SiteA()); }
        }
    });
    for(auto& t:workers) t.join();
    Check(heaps.load()==heap_before+500);
    Check(originals.load()==original_before+500);
    Check(RemoveAfterCallersStopped());
    Check(std::memcmp(bytes.data(),reinterpret_cast<void*>(&Original),bytes.size())==0);
    InvokeA(false);
    Check(released.load()+2==originals.load()+heaps.load()); // One null result and one exception.
    std::cout<<checks<<" checks, "<<failures<<" failures; actual fixture detour, no game calls\n";
    return failures ? 1 : 0;
}
