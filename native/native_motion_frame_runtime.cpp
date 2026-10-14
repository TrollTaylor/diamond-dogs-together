#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include "native_motion_frame_runtime.hpp"
#include "native_motion_stack_observer.hpp"
#include "native_motion_frame_ref_contract.hpp"
#include "qualified_sleep_lookup_phase.hpp"
#include "qualified_sleep_lookup_contract.hpp"
#include "coop_carry_original_view.hpp"
#include "sleep_plugin_read.hpp"
#include "sleep_graph_read.hpp"
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER)
#include "MinHook.h"
#endif
#pragma intrinsic(_ReturnAddress)
namespace mgscoop::native_motion_frame_runtime {
namespace q=qualified_sleep_lookup_observer;namespace f=native_motion_frame_observer;namespace view=coop_carry_original_view;
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_STANDING_CYCLE_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER) && defined(MGS_LOCAL_MGO_STANDING_FRAME_OBSERVER)
namespace {
using Callback=std::uintptr_t(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
struct Cache {q::Owner owner{};std::uint64_t payload=0,remembered=0,graph=0;std::uint8_t mode=0;bool known=false;};
struct Runtime {
 std::uintptr_t image=0;std::size_t span=0;wchar_t directory[32768]{};Authorization authorization{};Log log=nullptr;
 Callback original=nullptr;bool gate=false;volatile LONG installed=0,exposed=0;
 volatile LONG64 calls=0,samples=0,logs=0,unknown=0,suppressed=0,serial=0;
 volatile LONG64 allEntries=0,filtered=0,unreadable=0,reentered=0,rawEntries=0;
 SRWLOCK bookLock=SRWLOCK_INIT,cacheLock=SRWLOCK_INIT;view::Book book{};Cache cache[2]{};
};
Runtime runtime;
struct Local {bool active=false;Current current{};std::uintptr_t stack[12]{};unsigned stackCount=0;};
thread_local Local local{};
bool Bytes(std::uintptr_t at,void* out,std::size_t n) noexcept {q::ErrorPreserve error{};SIZE_T got=0;return out&&q::Range(at,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,n,&got)&&got==n;}
struct Reader {bool operator()(std::uintptr_t at,void* out,std::size_t n) noexcept{return Bytes(at,out,n);}};
bool HeadLive() noexcept {Reader read{};bool okay=false;if(TryAcquireSRWLockShared(&runtime.bookLock)){okay=view::Live(read,runtime.book,runtime.book.patches[0]);ReleaseSRWLockShared(&runtime.bookLock);}return okay;}
q::Gate Gate() noexcept {q::ErrorPreserve error{};q::Gate g{};g.image=runtime.image;g.exactPe=runtime.authorization.exactRetailPe;g.fullCode=runtime.gate;
 g.privateCurrent=InterlockedCompareExchange(&runtime.installed,0,0)==1&&runtime.authorization.privateWorkerCurrent&&runtime.authorization.privateWorkerCurrent()&&HeadLive();
 g.copiedSaveLab=runtime.authorization.copiedSaveLab;g.standingCycleProfile=g.privateCurrent&&runtime.authorization.profileCurrent&&runtime.authorization.profileCurrent(runtime.directory);return g;}
bool BirthMatches(const partner_input_hook::NativePoseBirthCensus& b,const f::Frame& frame) noexcept {
 const auto& o=frame.owner;if(!frame.typedCurrent||o.body>=2)return false;
 return b.image==runtime.image&&b.player==o.player&&b.data==o.data&&b.block[o.body]==o.record&&b.cs[o.body]==o.cs&&b.entry[o.body]==o.rows&&
  b.birth[o.body]&&b.lifecycle&&b.generation&&b.sceneEpoch&&!(b.sequence&1u);
}
bool Changed(const f::Frame& frame) noexcept {
 if(!frame.typedCurrent||frame.owner.body>=2)return InterlockedIncrement64(&runtime.unknown)<=16;
 const auto& o=frame.owner;const auto& p=o.phase;bool changed=true;
 if(TryAcquireSRWLockExclusive(&runtime.cacheLock)){auto& old=runtime.cache[o.body];changed=!old.known||!q::SameOwner(old.owner,o)||old.payload!=frame.payloadSecondary||old.mode!=p.mode||old.remembered!=p.rememberedGraph||old.graph!=p.currentGraph;
  if(changed)old={o,frame.payloadSecondary,p.rememberedGraph,p.currentGraph,p.mode,true};ReleaseSRWLockExclusive(&runtime.cacheLock);}return changed;
}
void Report(const f::Receipt& receipt) noexcept {
 q::ErrorPreserve error{};const auto count=InterlockedIncrement64(&runtime.logs);if(count>256||!runtime.log)return;
 const auto& s=receipt.before;const auto& o=s.owner;const auto& p=o.phase;char line[2000]{};
 const auto& birth=local.current.birth;
 StringCchPrintfA(line,ARRAYSIZE(line),"sleep_frame=original_callback;serial=%llu;thread=%lu;caller=%llx;cs_arg=%llx;actor_arg=%llx;payload=%llx;context=%llx;row_arg=%llx;engine_arg=%llx;typed=%u;body=%u;actor_match=%u;owner_stable=%u;mode=%u;request=%u;counter=%u;q=%u;life=%u;remembered=%llx;current_graph=%llx;payload0=%llx;full_motion=%llx;payload10=%x;payload14=%x;payload18=%x;payload1c=%x;payload20=%x;payload22=%x;original_rax=%llx;birth_match=%u;birth=%llu;lifecycle=%llu;generation=%u;scene_epoch=%u;census_thread=%u;original_job_retention=unknown;lease=0;selection=0;why=%s\r\n",
  static_cast<unsigned long long>(local.current.serial),s.event.thread,static_cast<unsigned long long>(s.event.caller-runtime.image),
  static_cast<unsigned long long>(s.event.cs),static_cast<unsigned long long>(s.event.actorArgument),static_cast<unsigned long long>(s.event.payload),static_cast<unsigned long long>(s.event.context),
  static_cast<unsigned long long>(s.rowArgument),static_cast<unsigned long long>(s.engineArgument),s.typedCurrent?1u:0u,o.body,s.actorArgumentMatches?1u:0u,receipt.stable?1u:0u,
  unsigned(p.mode),unsigned(p.request),unsigned(p.counter),unsigned(p.qAction),p.life,static_cast<unsigned long long>(p.rememberedGraph),static_cast<unsigned long long>(p.currentGraph),
  static_cast<unsigned long long>(s.payloadId),static_cast<unsigned long long>(s.payloadSecondary),s.payload10,s.payload14,s.payload18,s.payload1c,unsigned(s.payload20),unsigned(s.payload22),
  static_cast<unsigned long long>(receipt.originalResult),local.current.birthMatches?1u:0u,static_cast<unsigned long long>(o.body<2?birth.birth[o.body]:0),
  static_cast<unsigned long long>(birth.lifecycle),birth.generation,birth.sceneEpoch,birth.thread,s.reason);runtime.log(line);
 const auto& after=receipt.after;char close[640]{};
 StringCchPrintfA(close,ARRAYSIZE(close),"sleep_frame=callback_return;serial=%llu;typed=%u;body=%u;cs=%llx;row=%llx;engine=%llx;owner_stable=%u;birth_after_match=%u;birth_stable=%u;after_sequence=%u;after_birth=%llu;after_lifecycle=%llu;after_generation=%u;after_scene_epoch=%u;job_fence=unknown;selection=0;lease=0\r\n",
  static_cast<unsigned long long>(local.current.serial),after.typedCurrent?1u:0u,after.owner.body,static_cast<unsigned long long>(after.event.cs),static_cast<unsigned long long>(after.rowArgument),static_cast<unsigned long long>(after.engineArgument),receipt.stable?1u:0u,
  local.current.afterBirthMatches?1u:0u,local.current.birthStable?1u:0u,local.current.afterBirth.sequence,static_cast<unsigned long long>(after.owner.body<2?local.current.afterBirth.birth[after.owner.body]:0),static_cast<unsigned long long>(local.current.afterBirth.lifecycle),local.current.afterBirth.generation,local.current.afterBirth.sceneEpoch);runtime.log(close);
 char refs[420]{};StringCchPrintfA(refs,ARRAYSIZE(refs),"sleep_frame=native_existing_ref_snapshot;serial=%llu;before_read=%u;after_read=%u;engine_refs_before=%d;engine_refs_after=%d;row_flags_before=%x;row_flags_after=%x;row_owns_engine_before=%u;row_owns_engine_after=%u;borrowed_original_callback=1;new_retain=0;fence=unknown;selection=0;lease=0\r\n",static_cast<unsigned long long>(local.current.serial),local.current.nativeRefsBefore?1u:0u,local.current.nativeRefsAfter?1u:0u,local.current.engineRefsBefore,local.current.engineRefsAfter,local.current.rowFlagsBefore,local.current.rowFlagsAfter,(local.current.rowFlagsBefore>>3)&1u,(local.current.rowFlagsAfter>>3)&1u);runtime.log(refs);
 for(unsigned i=0;i<local.stackCount;++i){const auto pc=local.stack[i];char row[230]{};const bool retail=pc>=runtime.image&&pc-runtime.image<runtime.span;
  StringCchPrintfA(row,ARRAYSIZE(row),"sleep_frame=actual_stack;serial=%llu;index=%u;pc=%llx;retail=%u;rva=%llx;task_owner=unknown;selection=0\r\n",static_cast<unsigned long long>(local.current.serial),i,
   static_cast<unsigned long long>(pc),retail?1u:0u,static_cast<unsigned long long>(retail?pc-runtime.image:0));runtime.log(row);}
}
bool Begin(const f::Event& event,f::Receipt& receipt) noexcept {
 q::ErrorPreserve error{};Reader read{};q::NativePhaseReader phase{};const auto gate=Gate();
 const bool typed=f::Read(read,phase,gate,event,receipt.before);
 local.current.frame=receipt.before;
 if(!Changed(receipt.before)){InterlockedIncrement64(&runtime.suppressed);return false;}
 if(InterlockedIncrement64(&runtime.samples)>256)return false;
 // Exact existing native engine refcounter and row ownership bit from full
 // 16090/21EB0/21EA0/B6C0F0. Snapshot only: it grants no fence/lease.
 if(typed)local.current.nativeRefsBefore=q::Get(read,receipt.before.owner.engine+8,local.current.engineRefsBefore)&&q::Get(read,receipt.before.owner.rows+0xac,local.current.rowFlagsBefore);
 if(typed&&runtime.authorization.readBirth&&runtime.authorization.readBirth(local.current.birth))local.current.birthMatches=BirthMatches(local.current.birth,receipt.before);
 local.stackCount=CaptureStackBackTrace(0,ARRAYSIZE(local.stack),reinterpret_cast<void**>(local.stack),nullptr);
 return true;
}
std::uintptr_t Invoke(std::uintptr_t cs,std::uintptr_t actor,std::uintptr_t payload,std::uintptr_t context,const f::Event& event) {
 f::Receipt receipt{};const bool sampled=Begin(event,receipt);
 const auto result=runtime.original(cs,actor,payload,context);receipt.originalResult=result;
 if(sampled){q::ErrorPreserve error{};Reader read{};q::NativePhaseReader phase{};f::Read(read,phase,Gate(),event,receipt.after);receipt.stable=f::Stable(receipt.before,receipt.after);
  if(runtime.authorization.readBirth&&runtime.authorization.readBirth(local.current.afterBirth)){local.current.afterBirthMatches=BirthMatches(local.current.afterBirth,receipt.after);
   const auto& a=local.current.birth;const auto& z=local.current.afterBirth;const auto body=receipt.before.owner.body;
   local.current.birthStable=local.current.birthMatches&&local.current.afterBirthMatches&&body<2&&a.sequence==z.sequence&&a.birth[body]==z.birth[body]&&a.lifecycle==z.lifecycle&&a.generation==z.generation&&a.sceneEpoch==z.sceneEpoch;}
  if(receipt.after.typedCurrent)local.current.nativeRefsAfter=q::Get(read,receipt.after.owner.engine+8,local.current.engineRefsAfter)&&q::Get(read,receipt.after.owner.rows+0xac,local.current.rowFlagsAfter);
  Report(receipt);}
 return result;
}
void RawEntry(std::uintptr_t cs,std::uintptr_t actor,std::uintptr_t payload,std::uintptr_t context,std::uintptr_t caller,std::uint64_t entry,bool readable,std::uint64_t motion)noexcept {
 q::ErrorPreserve error{};if(entry>8||!q::Admitted(Gate())||!runtime.log)return;
 Reader read{};std::uint64_t payload0=0;const bool firstReadable=q::Get(read,payload,payload0);void* stack[12]{};
 const auto count=CaptureStackBackTrace(0,ARRAYSIZE(stack),stack,nullptr);InterlockedIncrement64(&runtime.rawEntries);
 char line[620]{};const bool retail=caller>=runtime.image&&caller-runtime.image<runtime.span;
 StringCchPrintfA(line,ARRAYSIZE(line),"sleep_frame=raw_callback_entry;entry=%llu;thread=%lu;caller_pc=%llx;caller_retail=%u;caller_rva=%llx;cs_arg=%llx;actor_arg=%llx;payload=%llx;context=%llx;payload0_read=%u;payload8_read=%u;payload0=%llx;payload8=%llx;covered=%u;owner=unqualified;lease=0;selection=0;append=0\r\n",
  static_cast<unsigned long long>(entry),GetCurrentThreadId(),static_cast<unsigned long long>(caller),retail?1u:0u,static_cast<unsigned long long>(retail?caller-runtime.image:0),static_cast<unsigned long long>(cs),static_cast<unsigned long long>(actor),static_cast<unsigned long long>(payload),static_cast<unsigned long long>(context),firstReadable?1u:0u,readable?1u:0u,static_cast<unsigned long long>(payload0),static_cast<unsigned long long>(motion),readable&&q::Covered(motion)?1u:0u);runtime.log(line);
 for(unsigned i=0;i<count;++i){const auto pc=reinterpret_cast<std::uintptr_t>(stack[i]);const bool inRetail=pc>=runtime.image&&pc-runtime.image<runtime.span;char row[260]{};
  StringCchPrintfA(row,ARRAYSIZE(row),"sleep_frame=raw_entry_stack;entry=%llu;index=%u;pc=%llx;retail=%u;rva=%llx;task_owner=unknown;selection=0;lease=0\r\n",static_cast<unsigned long long>(entry),i,static_cast<unsigned long long>(pc),inRetail?1u:0u,static_cast<unsigned long long>(inRetail?pc-runtime.image:0));runtime.log(row);}
}
__declspec(noinline) std::uintptr_t CallbackHook(std::uintptr_t cs,std::uintptr_t actor,std::uintptr_t payload,std::uintptr_t context) {
 // Count every actual entry BEFORE recursion, unreadable or ID filters.
 const auto entry=static_cast<std::uint64_t>(InterlockedIncrement64(&runtime.allEntries));
 if(local.active){InterlockedIncrement64(&runtime.reentered);return runtime.original(cs,actor,payload,context);}
 std::uint64_t motion=0;Reader read{};const bool readable=q::Range(payload,16)&&q::Get(read,payload+8,motion);
 RawEntry(cs,actor,payload,context,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),entry,readable,motion);
 const auto classification=native_motion_stack_observer::Classify(readable,motion);
 if(classification==native_motion_stack_observer::PayloadClass::unreadable){InterlockedIncrement64(&runtime.unreadable);return runtime.original(cs,actor,payload,context);}
 if(classification==native_motion_stack_observer::PayloadClass::uncovered){InterlockedIncrement64(&runtime.filtered);return runtime.original(cs,actor,payload,context);}
 local={};local.active=true;local.current.serial=static_cast<std::uint64_t>(InterlockedIncrement64(&runtime.serial));InterlockedIncrement64(&runtime.calls);
 const f::Event event{cs,payload,context,reinterpret_cast<std::uintptr_t>(_ReturnAddress()),actor,GetCurrentThreadId()};
 // POD-only boundary allows exact nativeSEH propagation with /EHsc.
 __try {return Invoke(cs,actor,payload,context,event);}__finally {local={};}
}
bool CodeGate(const Authorization& a,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 unsigned char bytes[8192]{};
 for(const auto& p:native_motion_frame_contract::pins){if(p.rva>span||p.size>span-p.rva||p.size>sizeof(bytes)||!a.readOriginal(image+p.rva,bytes,p.size)||std::memcmp(bytes,p.data,p.size)){mismatch=p.rva;return false;}}
 for(const auto& p:native_motion_frame_ref_contract::pins){if(p.rva>span||p.size>span-p.rva||p.size>sizeof(bytes)||!a.readOriginal(image+p.rva,bytes,p.size)||std::memcmp(bytes,p.data,p.size)){mismatch=p.rva;return false;}}
 for(const auto& p:qualified_sleep_lookup_contract::sites){if(p.rva>span||p.size>span-p.rva||p.size>sizeof(bytes)||!a.readOriginal(image+p.rva,bytes,p.size)||std::memcmp(bytes,p.bytes,p.size)){mismatch=p.rva;return false;}}
 auto read=a.readOriginal;return sleep_plugin_read::CodeGate(read,image,span,mismatch)&&sleep_graph_read::CodeGate(read,image,span,mismatch);
}
}
bool Prepare(std::uintptr_t image,std::size_t span,const wchar_t* directory,const Authorization& a,Log log) noexcept {
 q::ErrorPreserve error{};if(Prepared())return runtime.image==image&&q::Admitted(Gate());
 if(InterlockedCompareExchange(&runtime.exposed,0,0)||!image||span!=0xa01d000||!directory||!log||!a.exactRetailPe||!a.offline||!a.copiedSaveLab||!a.privateWorkerCurrent||!a.privateWorkerCurrent()||!a.profileCurrent||!a.profileCurrent(directory)||!a.readOriginal)return false;
 std::uintptr_t mismatch=0;if(!CodeGate(a,image,span,mismatch)){char line[180]{};StringCchPrintfA(line,ARRAYSIZE(line),"sleep_frame=prepare_refused;rva=%llx;lease=0;selection=0\r\n",static_cast<unsigned long long>(mismatch));log(line);return false;}
 if(FAILED(StringCchCopyW(runtime.directory,ARRAYSIZE(runtime.directory),directory)))return false;
 runtime.image=image;runtime.span=span;runtime.authorization=a;runtime.log=log;runtime.gate=true;Reader read{};
 if(!view::Bind(runtime.book,image,span)||!view::Capture(read,runtime.book,0,view::Kind::detour,image+0xaf2f90,reinterpret_cast<std::uintptr_t>(&CallbackHook),native_motion_frame_contract::pin0,5))return false;
 const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;void* target=reinterpret_cast<void*>(image+0xaf2f90);
 if(MH_CreateHook(target,reinterpret_cast<void*>(&CallbackHook),reinterpret_cast<void**>(&runtime.original))!=MH_OK)return false;
 runtime.book.patches[0].created=true;runtime.book.patches[0].trampoline=reinterpret_cast<std::uintptr_t>(runtime.original);
 InterlockedExchange(&runtime.exposed,1);if(MH_EnableHook(target)!=MH_OK)return false;
 AcquireSRWLockExclusive(&runtime.bookLock);const bool active=view::Activated(read,runtime.book,0);ReleaseSRWLockExclusive(&runtime.bookLock);if(!active)return false;
 InterlockedExchange(&runtime.installed,1);log("sleep_frame=observer_installed;callback=af2f90;cs_slot=3d8;nested_update_return=af3037;original_only=1;job_retention=unknown;selection=0;lease=0\r\n");return true;
}
bool Prepared() noexcept {q::ErrorPreserve error{};return InterlockedCompareExchange(&runtime.installed,0,0)==1&&HeadLive();}
bool ReadCurrent(Current& out) noexcept {q::ErrorPreserve error{};out={};if(!local.active||!q::Admitted(Gate()))return false;out=local.current;return true;}
bool OverlayOriginal(std::uintptr_t at,void* out,std::size_t n) noexcept {q::ErrorPreserve error{};Reader read{};if(!TryAcquireSRWLockShared(&runtime.bookLock))return false;const bool result=view::Overlay(read,runtime.book,at,out,n);ReleaseSRWLockShared(&runtime.bookLock);return result;}
bool Status(char* out,std::size_t n) noexcept {q::ErrorPreserve error{};if(!out||!n)return false;const int count=std::snprintf(out,n,"sleepFrame;on=%u;all=%lld;calls=%lld;flt=%lld;bad=%lld;re=%lld;raw=%lld;samples=%lld;logs=%lld;dupe=%lld;lease=0;select=0",Prepared()?1u:0u,
 InterlockedCompareExchange64(&runtime.allEntries,0,0),InterlockedCompareExchange64(&runtime.calls,0,0),InterlockedCompareExchange64(&runtime.filtered,0,0),InterlockedCompareExchange64(&runtime.unreadable,0,0),InterlockedCompareExchange64(&runtime.reentered,0,0),InterlockedCompareExchange64(&runtime.rawEntries,0,0),
 InterlockedCompareExchange64(&runtime.samples,0,0),InterlockedCompareExchange64(&runtime.logs,0,0),InterlockedCompareExchange64(&runtime.suppressed,0,0));return count>=0&&std::size_t(count)<n;}
#else
bool Prepare(std::uintptr_t,std::size_t,const wchar_t*,const Authorization&,Log) noexcept{return false;}
bool Prepared() noexcept{return false;}
bool ReadCurrent(Current& out) noexcept{out={};return false;}
bool OverlayOriginal(std::uintptr_t,void*,std::size_t) noexcept{return true;}
bool Status(char* out,std::size_t n) noexcept{return out&&n&&SUCCEEDED(StringCchCopyA(out,n,"sleepFrame;on=0;lease=0;select=0;why=compiled_off"));}
#endif
}
