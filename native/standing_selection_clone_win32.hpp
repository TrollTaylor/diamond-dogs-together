#pragma once
// Isolated default-OFF allocation/registration support. No engine invocation,
// hook installation, episode provider, profile opt-in or native bank mutation.
#include "standing_selection_clone.hpp"
#include "standing_selection_dispatch.hpp"
#include "qualified_sleep_lookup_contract.hpp"
#include "sleep_unique_bind_contract.hpp"
#if defined(_WIN32)
#include <atomic>
namespace mgscoop::standing_selection_clone_win32 {
namespace b=standing_selection_clone;
struct CloneSet {
 void* allocation=nullptr;PRUNTIME_FUNCTION table=nullptr;
 std::atomic<unsigned> active{0};std::atomic<bool> accepting{false};
 bool registered=false,ready=false;
 CloneSet() noexcept=default;CloneSet(const CloneSet&)=delete;CloneSet& operator=(const CloneSet&)=delete;
 // Never auto-unregister/free at DLL unload while an invocation may be live.
 // Root must close admission and retire on a proven quiescent lifecycle.
 ~CloneSet()=default;
};
inline bool Acquire(CloneSet& s) noexcept {
 // These TWO atomics intentionally use the single SC order. A successful
 // second admission read orders its prior count increment before Close's
 // false store/count read, preventing a stale zero from freeing live code.
 if(!s.accepting.load(std::memory_order_seq_cst))return false;
  auto count=s.active.load(std::memory_order_seq_cst);bool incremented=false;
  for(unsigned attempt=0;attempt<16&&count<1024;++attempt){if(s.active.compare_exchange_weak(count,count+1,std::memory_order_seq_cst)){incremented=true;break;}}
  if(!incremented)return false;if(!s.accepting.load(std::memory_order_seq_cst)){s.active.fetch_sub(1,std::memory_order_seq_cst);return false;}return true;
}
struct Lease {
 CloneSet* set=nullptr;explicit Lease(CloneSet& s) noexcept {if(Acquire(s))set=&s;}
 ~Lease(){if(set)set->active.fetch_sub(1,std::memory_order_seq_cst);}Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
};
template<class Reader>bool ExactCode(Reader& read,std::uintptr_t image) noexcept {
 using namespace standing_selection_clone_contract;unsigned char copy[8192]{};
 if(!read(image+rawRva,copy,sizeof(rawBytes))||std::memcmp(copy,rawBytes,sizeof(rawBytes))||
  !read(image+setupRva,copy,sizeof(setupBytes))||std::memcmp(copy,setupBytes,sizeof(setupBytes)))return false;
 for(const auto& p:extraPins){if(p.size>sizeof(copy)||!read(image+p.rva,copy,p.size)||std::memcmp(copy,p.data,p.size))return false;}
 // Complete original target .pdata, every native chained unwind record,
 // downstream decoder/allocator/refcount routines and exact GS handler.
 for(const auto& p:qualified_sleep_lookup_contract::sites){if(p.size>sizeof(copy)||!read(image+p.rva,copy,p.size)||std::memcmp(copy,p.bytes,p.size))return false;}
 for(const auto& p:sleep_unique_bind_contract::sites){if(p.size>sizeof(copy)||!read(image+p.rva,copy,p.size)||std::memcmp(copy,p.bytes,p.size))return false;}return true;
}
inline void* AllocateNear(std::uintptr_t image) noexcept {
 SYSTEM_INFO info{};GetSystemInfo(&info);const auto step=std::uintptr_t(info.dwAllocationGranularity);if(!step)return nullptr;
 // All copied RIP-data operands must fit signedrel32 back into the original
 // image. Build checks each one; proximity is never assumed from allocation.
 std::uintptr_t seed=0;if(!b::Add(image,0xb000000,seed))return nullptr;const auto center=seed&~(step-1);const std::uintptr_t extent=0x30000000;
 for(std::uintptr_t distance=0;distance<extent;distance+=step){for(unsigned side=0;side<2;++side){
   if(side&&(!distance||distance>center))continue;const auto address=side?center-distance:center+distance;
   if(address<center&& !side)continue;MEMORY_BASIC_INFORMATION region{};
   if(!VirtualQuery(reinterpret_cast<void*>(address),&region,sizeof(region))||region.State!=MEM_FREE||region.RegionSize<b::kBytes)continue;
   if(void* memory=VirtualAlloc(reinterpret_cast<void*>(address),b::kBytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE))return memory;
  }}return nullptr;
}
template<class Reader>bool Prepare(CloneSet& set,std::uintptr_t game,Reader& originalView,bool fullPeAndUnwindGate) noexcept {
 if(!standing_selection_dispatch::Compiled()||set.allocation||set.registered||set.ready||!fullPeAndUnwindGate||!ExactCode(originalView,game))return false;
 void* memory=AllocateNear(game);if(!memory)return false;b::Image image{};const b::Addresses addresses{game,reinterpret_cast<std::uintptr_t>(memory),reinterpret_cast<std::uintptr_t>(&standing_selection_dispatch::Probe)};
 if(!b::Build(addresses,image)){VirtualFree(memory,0,MEM_RELEASE);return false;}
 std::memcpy(memory,image.bytes.data(),b::kBytes);DWORD old=0;
 if(!VirtualProtect(memory,b::kBytes,PAGE_EXECUTE_READ,&old)||!FlushInstructionCache(GetCurrentProcess(),memory,b::kBytes)){VirtualFree(memory,0,MEM_RELEASE);return false;}
 auto* functions=reinterpret_cast<PRUNTIME_FUNCTION>(static_cast<unsigned char*>(memory)+b::kTable);
 if(!RtlAddFunctionTable(functions,image.functions,reinterpret_cast<DWORD64>(memory))){VirtualFree(memory,0,MEM_RELEASE);return false;}
 set.allocation=memory;set.table=functions;set.registered=true;set.ready=true;
 // Admission remains false; this function cannot activate a dispatcher.
 return true;
}
inline bool CloseQuiescent(CloneSet& set) noexcept {
 set.accepting.store(false,std::memory_order_seq_cst);if(set.active.load(std::memory_order_seq_cst))return false;
 if(set.registered&&!RtlDeleteFunctionTable(set.table))return false;set.registered=false;
 if(set.allocation&&!VirtualFree(set.allocation,0,MEM_RELEASE))return false;set.allocation=nullptr;set.table=nullptr;set.ready=false;return true;
}
inline bool InvokeSetup(CloneSet& set,standing_selection_dispatch::Dispatcher& dispatcher,standing_selection_dispatch::NativeSetup original,
 std::uintptr_t destination,std::uintptr_t spec,std::uintptr_t arg3,std::uintptr_t arg4,std::uintptr_t context) {
 if(!Acquire(set))return original(destination,spec,arg3,arg4,context);
 if(!set.ready||!set.allocation){set.active.fetch_sub(1,std::memory_order_seq_cst);return original(destination,spec,arg3,arg4,context);}
 // Acquire serializes Close. These fields cannot be cleared while we hold
 // the reference. An SEH finally also retires it on native unwind.
 const auto clone=reinterpret_cast<standing_selection_dispatch::NativeSetup>(static_cast<unsigned char*>(set.allocation)+b::kSetup);
 __try {return standing_selection_dispatch::SetupDispatch(dispatcher,original,clone,destination,spec,arg3,arg4,context);}
 __finally {set.active.fetch_sub(1,std::memory_order_seq_cst);}
}
} // namespace mgscoop::standing_selection_clone_win32
#endif
