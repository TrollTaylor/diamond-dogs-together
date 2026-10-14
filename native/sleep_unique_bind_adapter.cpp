// No hook installation, archive operations or game calls at static init.
#include "sleep_unique_bind_adapter.hpp"
#include <atomic>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#endif
namespace mgscoop::sleep_unique_bind_native {
namespace ub=sleep_unique_bind;
namespace bt=sleep_native_bank_trace;
namespace raw=sleep_raw_constructor_trace;
namespace {
std::atomic<const Configuration*> configuration{nullptr};
std::atomic<CommonBind> fallbackCommon{nullptr};
std::atomic<AddMtar> fallbackAppend{nullptr};
std::atomic<std::uint64_t> frameSerial{0};
std::atomic<std::uint64_t> rawSerial{0};
struct Frame {
 std::uintptr_t record=0;std::uint32_t nativeActor=0;bool attempted=false;Frame* previous=nullptr;
 std::uint64_t serial=0;unsigned ordinal=0;bool initialReady=false;
 ub::Scope initialScope{};bt::Snapshot initial{};
};
thread_local Frame* currentFrame=nullptr;
struct FrameLease {
 Frame& frame;
 explicit FrameLease(Frame& f) noexcept:frame(f){frame.previous=currentFrame;currentFrame=&frame;}
 ~FrameLease(){currentFrame=frame.previous;}
};
struct Read {const Configuration* c;bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept{return c->read(c->context,at,out,n);}};
struct Hash {const Configuration* c;bool operator()(const void* bytes,std::size_t n,char (&out)[65])noexcept{return c->hash(c->context,bytes,n,out);}};
struct ObserverErrorPreserve {
 #if defined(_WIN32)
 DWORD error=GetLastError();~ObserverErrorPreserve(){SetLastError(error);}
 #endif
};
bool TraceEnabled(const Configuration* c) noexcept {
 #if defined(MGS_BACKGROUND_WORKER)
 return c&&c->observeOnly&&c->traceScope&&c->traceReport;
 #else
 (void)c;return false;
 #endif
}
bool TraceSample(const Configuration* c,ub::Event& event,bt::Phase phase,ub::Scope& scope,bt::Snapshot& snapshot,const char*& why) noexcept {
 scope={};if(!c->traceScope(c->context,event,scope)||scope.image!=c->image){why="trace_scope_provider_refused";return false;}
 // The provider derives the unknown actor through current typed rows. A
 // supplied known native argument is never changed or manufactured.
 if(event.nativeActor==UINT32_MAX){event.nativeActor=scope.expectedNativeActor;event.record=scope.creatorRecord;}
 event.creatorInvocation=scope.creatorInvocation;Read read{c};Hash hash{c};
 return bt::ReadPair(read,hash,scope,event,phase,snapshot,why);
}
bool ForwardAppend(const Configuration* c,std::uintptr_t engine,std::uintptr_t bank,std::uintptr_t returned){
 Frame* f=currentFrame;
 if(f&&!f->attempted&&returned==c->image+0x9bb9fa){
  const ObserverErrorPreserve errors{};
  f->attempted=true;
  ub::Event event{f->record,engine,bank,returned,f->nativeActor,f->serial};ub::Scope scope{};
  const bool supplied=c->scope(c->context,event,scope);
  if(supplied&&scope.image==c->image){
   event.creatorInvocation=scope.creatorInvocation;Read read{c};Hash hash{c};ub::Reply reply{};
   if(c->observeOnly)ub::Observe(read,hash,scope,event,reply);
   else {
    auto append=[&](std::uintptr_t owner,std::uintptr_t unique)noexcept{return c->originalAppend(owner,unique);};
    auto fresh=[&]()noexcept{ub::Scope now{};if(!c->scope(c->context,event,now))now.faulted=true;return now;};
    ub::TryPrepend(read,hash,scope,event,append,fresh,reply);
   }
   c->report(c->context,event,reply);
  }else {ub::Reply refused{};refused.reason="first_bind_scope_provider_refused";c->report(c->context,event,refused);}
 }
 // Every traced path forwards the original once. Unknown/foreign callers
 // still forward, and never become native-frame or writer authority.
 if(!TraceEnabled(c)||!bt::NaturalCaller(c->image,returned))return c->originalAppend(engine,bank);
 const bool common=f&&returned==c->image+0x9bb9fa;
 bt::Receipt receipt{};receipt.phase=common?bt::Phase::commonAfter:bt::Phase::nativeAfter;
 receipt.frameSerial=common?f->serial:0;
 receipt.ordinal=common?++f->ordinal:0;
 receipt.event={common?f->record:0,engine,bank,returned,common?f->nativeActor:UINT32_MAX,common?f->serial:0};
 ub::Scope beforeScope{},afterScope{};raw::Scope rawBefore{},rawAfter{};const char* why="trace_before";
 bool beforeReady=false;
 {const ObserverErrorPreserve errors{};
  beforeReady=TraceSample(c,receipt.event,common?bt::Phase::commonBefore:bt::Phase::nativeBefore,beforeScope,receipt.before,why);
  if(beforeReady){Read read{c};Hash hash{c};receipt.incomingReady=bt::ReadBank(read,hash,c->image,bank,receipt.incoming);}
  else if(!common&&returned==c->image+0x9c3fc1&&receipt.event.nativeActor==UINT32_MAX&&!receipt.event.record&&
          c->rawConstructorScope&&!std::strcmp(why,"trace_scope_provider_refused")){
   receipt.rawConstructor=true;receipt.rawSerial=rawSerial.fetch_add(1,std::memory_order_relaxed)+1;
   if(c->rawConstructorScope(c->context,receipt.event,rawBefore)&&rawBefore.image==c->image){
    receipt.rawThread=rawBefore.thread;receipt.rawBeforeTick=rawBefore.now;
    Read read{c};Hash hash{c};beforeReady=raw::ReadPair(read,hash,rawBefore,receipt.event,receipt.before,receipt.incoming,why);
    receipt.incomingReady=beforeReady;
   }else why="raw_constructor_scope_provider_refused";
  }
 }
 receipt.originalResult=c->originalAppend(engine,bank);
 // The original bool is returned regardless of any observer failure.
 {const ObserverErrorPreserve errors{};bool afterReady=false;
  if(receipt.rawConstructor){
   if(beforeReady){
    if(c->rawConstructorScope(c->context,receipt.event,rawAfter)&&rawAfter.image==c->image){
     receipt.rawAfterTick=rawAfter.now;
     Read read{c};Hash hash{c};bt::Bank incoming{};
     afterReady=raw::ReadPair(read,hash,rawAfter,receipt.event,receipt.after,incoming,why);
     if(afterReady&&!bt::SameBank(receipt.incoming,incoming)){afterReady=false;why="raw_constructor_incoming_changed";}
    }else why="raw_constructor_after_scope_refused";
   }
   if(beforeReady&&afterReady&&raw::SameScope(rawBefore,rawAfter))raw::CompleteAdd(receipt);
   else receipt.reason=beforeReady?(afterReady?"raw_constructor_scope_changed":why):why;
  }
  else {
   if(beforeReady)afterReady=TraceSample(c,receipt.event,receipt.phase,afterScope,receipt.after,why);
   if(beforeReady&&afterReady&&ub::SameScope(beforeScope,afterScope)){
   bt::CompleteAdd(receipt);if(!receipt.incomingReady){receipt.ready=false;receipt.reason="trace_incoming_bank_unqualified";}
   }
   else receipt.reason=beforeReady?(afterReady?"trace_scope_changed":why):why;
  }
  c->traceReport(c->context,receipt);
 }
 return receipt.originalResult;
}
}
bool Configure(const Configuration* c) noexcept {
 #if !defined(MGS_BACKGROUND_WORKER)
 (void)c;return false;
 #else
 if(!c||!c->privateInstallationAdmitted||!ub::base::Range(c->image,0xa01d000)||!c->originalCommon||!c->originalAppend||!c->read||!c->hash||!c->scope||!c->report)return false;
 if((c->traceScope==nullptr)!=(c->traceReport==nullptr)||(c->traceReport&&!c->observeOnly))return false;
 if(c->rawConstructorScope&&(!c->traceScope||!c->traceReport||!c->observeOnly))return false;
 #if !defined(MGS_LOCAL_MGO_UNIQUE_REST_BIND)
 if(!c->observeOnly)return false;
 #endif
 const Configuration* empty=nullptr;
 if(!configuration.compare_exchange_strong(empty,c,std::memory_order_release,std::memory_order_relaxed))return false;
 fallbackCommon.store(c->originalCommon,std::memory_order_release);fallbackAppend.store(c->originalAppend,std::memory_order_release);return true;
 #endif
}
void Unconfigure() noexcept {configuration.store(nullptr,std::memory_order_release);}
void CommonBindHook(std::uintptr_t record,std::uint32_t nativeActor){
 const auto* c=configuration.load(std::memory_order_acquire);
 // A hook must never be installed without its original trampoline configured.
 // Parent disables hooks before unconfiguring, while no game callback runs.
 if(!c||!c->originalCommon){if(auto original=fallbackCommon.load(std::memory_order_acquire))original(record,nativeActor);return;}
 Frame frame{};frame.record=record;frame.nativeActor=nativeActor;
 frame.serial=frameSerial.fetch_add(1,std::memory_order_relaxed)+1;FrameLease lease(frame);
 if(TraceEnabled(c)){
  const ObserverErrorPreserve errors{};
  ub::Event event{record,0,0,0,nativeActor,frame.serial};const char* why="trace_common_enter";
  frame.initialReady=TraceSample(c,event,bt::Phase::commonEnter,frame.initialScope,frame.initial,why);
  bt::Receipt receipt{};receipt.phase=bt::Phase::commonEnter;receipt.frameSerial=frame.serial;receipt.event=event;
  receipt.before=frame.initial;receipt.ready=frame.initialReady;receipt.reason=why;c->traceReport(c->context,receipt);
 }
 c->originalCommon(record,nativeActor);
 if(TraceEnabled(c)){
  const ObserverErrorPreserve errors{};
  bt::Receipt receipt{};receipt.phase=bt::Phase::commonLeave;receipt.frameSerial=frame.serial;receipt.ordinal=frame.ordinal;
  receipt.event={record,frame.initial.owner.engine,0,0,nativeActor,frame.serial};receipt.before=frame.initial;
  ub::Scope after{};const char* why="trace_common_leave";
  const bool done=TraceSample(c,receipt.event,receipt.phase,after,receipt.after,why);
  if(frame.initialReady&&done&&ub::SameScope(frame.initialScope,after))bt::CompleteCommon(receipt);
  else receipt.reason=done?(frame.initialReady?"trace_common_scope_changed":"trace_common_enter_unqualified"):why;
  c->traceReport(c->context,receipt);
 }
}
bool AddMtarHook(std::uintptr_t engine,std::uintptr_t bank){
 #if defined(_MSC_VER)
 const auto returned=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 #elif defined(__GNUC__)
 const auto returned=reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
 #else
 const std::uintptr_t returned=0;
 #endif
 const auto* c=configuration.load(std::memory_order_acquire);
 if(!c||!c->originalAppend){if(auto original=fallbackAppend.load(std::memory_order_acquire))return original(engine,bank);return false;}
 // Preserve ORIGINAL normal registration on success, refusal, compiled-off
 // append, missing scope and all calls outside qualified common-bind frame.
 return ForwardAppend(c,engine,bank,returned);
}
#if defined(MGS_BANK_TRACE_DETACHED_TEST)
bool DetachedNaturalAppend(std::uintptr_t engine,std::uintptr_t bank,std::uintptr_t returned){
 const auto* c=configuration.load(std::memory_order_acquire);
 if(!c||!c->originalAppend){if(auto original=fallbackAppend.load(std::memory_order_acquire))return original(engine,bank);return false;}
 return ForwardAppend(c,engine,bank,returned);
}
#endif
} // namespace mgscoop::sleep_unique_bind_native
