#pragma once
#include "standing_selection_policy.hpp"
#include <type_traits>
#include <atomic>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace mgscoop::standing_selection_dispatch {
namespace p=standing_selection_policy;
inline bool Compiled() noexcept {
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_OFFLINE_EQUIPMENT_TEST) && defined(MGS_LOCAL_STOCK46_PRELOAD_TEST) && defined(MGS_LOCAL_STOCK46_WRITER_TEST) && defined(MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_SELECTION) && !defined(MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER)
 return true;
#else
 return false;
#endif
}
struct ErrorPreserve {
#if defined(_WIN32)
 DWORD value=GetLastError();~ErrorPreserve(){SetLastError(value);}
#endif
};
// Root must bind these only after its actual hidden observer proves the native
// frame and first20 episode. The current MAIN observer supplies NONE of them.
// Every validation must read current actual ownership; no cached observer row,
// Lua phase or archive profile can produce a witness/permission.
struct Provider {
 void* context=nullptr;
 bool(*before)(void*,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,p::Snapshot&,p::Snapshot&,p::OnsetWitness&) noexcept=nullptr;
 bool(*current)(void*,const p::Snapshot&,p::Snapshot&,p::Snapshot&) noexcept=nullptr;
 bool(*after)(void*,const p::Snapshot&,p::Snapshot&,p::Snapshot&,std::uintptr_t&) noexcept=nullptr;
};
struct Plan {Provider provider{};p::Episode* episode=nullptr;p::Snapshot initial{};bool probeTried=false,probeAdmitted=false;};
inline thread_local Plan* active=nullptr;
struct Scope {Plan* previous=active;explicit Scope(Plan& plan) noexcept {active=&plan;}~Scope(){active=previous;}};
inline std::uintptr_t Probe(std::uintptr_t actualContext,std::uint64_t actualId,unsigned path) noexcept {
 ErrorPreserve error{};auto* plan=active;
 if(!plan||path!=1||actualContext!=plan->initial.call.context||actualId!=plan->initial.call.id||!plan->provider.current||!plan->episode)return 0;
 plan->probeTried=true;p::Snapshot a{},b{};
 if(!plan->provider.current(plan->provider.context,plan->initial,a,b)||p::Qualify(*plan->episode,a,b)||
  a.call.frameSerial!=plan->initial.call.frameSerial||a.call.jobSerial!=plan->initial.call.jobSerial||a.call.context!=actualContext||a.call.id!=actualId)return 0;
 plan->probeAdmitted=true;return a.bank.bank;
}
struct EpisodeLease {
 std::atomic_flag* flag=nullptr;bool owned=false;
 explicit EpisodeLease(std::atomic_flag& f) noexcept :flag(&f),owned(!f.test_and_set(std::memory_order_acquire)){}
 void Close() noexcept {if(owned){owned=false;flag->clear(std::memory_order_release);}}
 ~EpisodeLease(){Close();}EpisodeLease(const EpisodeLease&)=delete;EpisodeLease& operator=(const EpisodeLease&)=delete;
};
struct Dispatcher {Provider provider{};p::Episode episodes[2]{};std::uint64_t lastOnsetSerial[2]{};std::atomic_flag busy[2]={ATOMIC_FLAG_INIT,ATOMIC_FLAG_INIT};bool activationCurrent=false;};
using NativeSetup=bool(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
inline bool NativeCall(NativeSetup function,Plan& plan,std::uintptr_t destination,std::uintptr_t spec,
 std::uintptr_t arg3,std::uintptr_t arg4,std::uintptr_t context,EpisodeLease* lease=nullptr) {
 // /EHsc does not promise C++ destructor cleanup on a native SEH unwind.
 // This POD-only boundary restores TLS with __finally and never handles,
 // translates, retries or swallows the original exception.
 Plan* previous=active;
#if defined(_WIN32) && defined(_MSC_VER)
 __try {active=&plan;return function(destination,spec,arg3,arg4,context);}
 __finally {active=previous;if(AbnormalTermination()){if(plan.episode)*plan.episode={};if(lease)lease->Close();}}
#else
 (void)lease;Scope scope{plan};return function(destination,spec,arg3,arg4,context);
#endif
}
// Exact raw two-argument ABI is preserved. This first stage deliberately
// never selects on raw callers: no raw caller's native purpose is qualified
// by the setup-only1A6D88D frame contract. The raw clone is independently
// constructed/tested for a later separately witnessed path, not enabled.
template<class Raw> std::uintptr_t RawForward(Raw original,std::uintptr_t engine,std::uint64_t id) {
 return original(engine,id);
}
template<class Setup,class Clone> bool SetupDispatch(Dispatcher& d,Setup original,Clone clone,
 std::uintptr_t destination,std::uintptr_t spec,std::uintptr_t arg3,std::uintptr_t arg4,std::uintptr_t context) {
 if(!Compiled()||!d.activationCurrent||active||!d.provider.before||!d.provider.current||!d.provider.after)return original(destination,spec,arg3,arg4,context);
 p::Snapshot first{},second{};p::OnsetWitness witness{};bool observed=false;
 {ErrorPreserve error{};observed=d.provider.before(d.provider.context,destination,spec,arg3,arg4,context,first,second,witness);}
 if(!observed||first.owner.body>=2)return original(destination,spec,arg3,arg4,context);
 EpisodeLease lease{d.busy[first.owner.body]};if(!lease.owned)return original(destination,spec,arg3,arg4,context);
 auto& episode=d.episodes[first.owner.body];
 if(first.call.id==p::kInitial&&!episode.live){if(witness.nativeRequestSerial<=d.lastOnsetSerial[first.owner.body]||!p::Begin(episode,witness,first))return original(destination,spec,arg3,arg4,context);d.lastOnsetSerial[first.owner.body]=witness.nativeRequestSerial;}
 if(p::Qualify(episode,first,second)){episode={};return original(destination,spec,arg3,arg4,context);}
 Plan plan{d.provider,&episode,first,false,false};bool result=false;
 // This is ONE original-equivalent setup invocation. It retains all five
 // actual arguments and original native allocation/decoder/end operations.
 // A candidate miss resumes clone's stock search; never retries the setup.
 if constexpr(std::is_convertible_v<Clone,NativeSetup>)result=NativeCall(static_cast<NativeSetup>(clone),plan,destination,spec,arg3,arg4,context,&lease);
 else {Scope scope{plan};result=clone(destination,spec,arg3,arg4,context);} // detached fixture callable only
 {ErrorPreserve error{};p::Snapshot a{},b{};std::uintptr_t descriptor=0;
  if(!result||!plan.probeAdmitted||!d.provider.after(d.provider.context,first,a,b,descriptor)||!p::Selected(episode,a,b,descriptor))episode={};}
 return result;
}
} // namespace mgscoop::standing_selection_dispatch
