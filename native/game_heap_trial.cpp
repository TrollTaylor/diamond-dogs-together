// Original bounded paired-allocation experiment; docs/game-heap-trial.md.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include "mgscoop/game_heap_trial.hpp"
#include "mgscoop/heap_trial_core.hpp"

namespace {
struct Context { std::uintptr_t base; };
bool Read(void*,std::uintptr_t address,void* output,std::size_t size) noexcept {
    if(address<0x10000 || size==0 || size>512 || address>0x00007fffffffffff-size) return false;
    SIZE_T count=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),output,size,&count) && count==size;
}
bool Match(std::uintptr_t base,std::size_t span,std::uintptr_t rva,std::size_t size,const char* expected) noexcept {
    if(rva>span || size>span-rva || size>512) return false;
    unsigned char bytes[512]{},digest[32]{};
    if(!Read(nullptr,base+rva,bytes,size)) return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok) ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok) ok=BCryptHashData(hash,bytes,static_cast<ULONG>(size),0)>=0;
    if(ok) ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    constexpr char hex[]="0123456789ABCDEF";
    for(unsigned i=0;ok && i<32;++i)
        ok=expected[2*i]==hex[digest[i]>>4] && expected[2*i+1]==hex[digest[i]&15];
    return ok;
}
void* Allocate(void* opaque,std::uint64_t size,std::uint64_t alignment,std::uint32_t tag) noexcept {
    const auto& c=*static_cast<Context*>(opaque);
    using Fn=void* (*)(std::uint64_t,std::uint64_t,std::uint32_t);
    return reinterpret_cast<Fn>(c.base+0xdbab0)(size,alignment,tag);
}
bool Release(void* opaque,void* pointer,std::uint32_t& status) noexcept {
    const auto& c=*static_cast<Context*>(opaque);
    // Same dispatch called by BlockHeapFree: RCX=status, RDX=allocation,
    // RAX=the status pointer. Its heap branch calls the native deallocator.
    using Fn=std::uint32_t* (*)(std::uint32_t*,void*);
    return reinterpret_cast<Fn>(c.base+0xdbfd0)(&status,pointer)==&status;
}
}

bool RunGameHeapTrial(void* verified_base,std::size_t span,void (*log)(const char*) noexcept) noexcept {
    if(!verified_base || !log) return false;
    const DWORD incoming=GetLastError();
    Context context{reinterpret_cast<std::uintptr_t>(verified_base)};
    const bool matched=Match(context.base,span,0xdbab0,104,"2A8E312DA8B88A39A24827E23ADC5A66D6659DB21D5E1E644BB19BEE9FB3F5B9") &&
        Match(context.base,span,0xdbfd0,333,"6E2D596197C811F405E34605E120E3FF4652D09527B51AC7BBE7A5E7392F8787") &&
        Match(context.base,span,0xdcbc0,22,"EDEE2B02EE2E212432112FF50F7E4585699CAC6085099DF205DDAFEC55D1E189");
    if(!matched) { log("heap_trial=refused; reason=code_gate allocations=0\r\n"); SetLastError(incoming); return false; }
    log("heap_trial=begin; sizes=88,144 alignment=8 tag=115 allocator_hook=0 host_calls=0\r\n");
    const auto report=mgscoop::heap_trial::Run({&context,Allocate,Read,Release});
    for(unsigned i=0;i<2;++i) {
        const auto& s=report.samples[i];
        char line[512]{};
        StringCchPrintfA(line,ARRAYSIZE(line),
            "heap_sample=%u pointer=%p header_read=%u header_valid=%u delta=%u tag=%u total=%u marker=%x free_returned=%u free_status=%x\r\n",
            i,reinterpret_cast<void*>(s.address),s.header_read ? 1u:0u,s.header_valid ? 1u:0u,
            s.header[0],s.header[1],s.header[2],s.header[3],s.free_returned ? 1u:0u,s.free_status);
        log(line);
    }
    char line[256]{};
    StringCchPrintfA(line,ARRAYSIZE(line),
        "heap_trial=%s; allocated=%u released=%u thread=%lu tick_ms=%llu host_calls=0 payload_writes=0\r\n",
        report.passed ? "passed":"failed",report.allocated,report.released,GetCurrentThreadId(),GetTickCount64());
    log(line); SetLastError(incoming); return report.passed;
}
