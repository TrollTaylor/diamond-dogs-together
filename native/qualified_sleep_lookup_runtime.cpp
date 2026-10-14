#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <intrin.h>
#include <cstdio>
#include <cstring>
#include "qualified_sleep_lookup_runtime.hpp"
#include "qualified_sleep_lookup_phase.hpp"
#include "qualified_sleep_lookup_contract.hpp"
#include "coop_carry_original_view.hpp"
#include "sleep_plugin_read.hpp"
#include "sleep_graph_read.hpp"
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER)
#include "MinHook.h"
#endif
#pragma intrinsic(_ReturnAddress)
namespace mgscoop::qualified_sleep_lookup_runtime {
namespace q=qualified_sleep_lookup_observer;namespace view=coop_carry_original_view;
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_STANDING_LOOKUP_OBSERVER)
namespace {
using Raw=std::uintptr_t(*)(std::uintptr_t,std::uint64_t);
using Setup=bool(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
struct Cache {q::Owner owner{};q::Path path=q::Path::raw;std::uint64_t inputHash=0,outputHash=0;std::uintptr_t key=0;bool filled=false;};
struct Runtime {
 std::uintptr_t image=0;std::size_t span=0;wchar_t directory[32768]{};Authorization authorization{};Log log=nullptr;
 Raw raw=nullptr;Setup setup=nullptr;bool gate=false;volatile LONG installed=0,exposed=0;
 volatile LONG64 rawCalls=0,setupCalls=0,uncovered=0,suppressed=0,fullSamples=0,logs=0,unknown=0;
 SRWLOCK bookLock=SRWLOCK_INIT,cacheLock=SRWLOCK_INIT;view::Book book{};Cache cache[32]{};q::Owner lastOwner[2]{};
};
Runtime runtime;thread_local bool inHook=false;
struct HookLease {HookLease(){inHook=true;}~HookLease(){inHook=false;}};
bool Bytes(std::uintptr_t at,void* out,std::size_t n) noexcept {q::ErrorPreserve preserve{};SIZE_T got=0;return out&&q::Range(at,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,n,&got)&&got==n;}
struct Reader {bool operator()(std::uintptr_t at,void* out,std::size_t n) noexcept{return Bytes(at,out,n);}};
bool HeadsLive() noexcept {
 Reader read{};bool okay=false;if(TryAcquireSRWLockShared(&runtime.bookLock)){okay=view::Live(read,runtime.book,runtime.book.patches[0])&&view::Live(read,runtime.book,runtime.book.patches[1]);ReleaseSRWLockShared(&runtime.bookLock);}return okay;
}
q::Gate Gate() noexcept {q::ErrorPreserve preserve{};q::Gate g{};g.image=runtime.image;g.exactPe=runtime.authorization.exactRetailPe;g.fullCode=runtime.gate;
 g.privateCurrent=InterlockedCompareExchange(&runtime.installed,0,0)==1&&runtime.authorization.privateWorkerCurrent&&runtime.authorization.privateWorkerCurrent()&&HeadsLive();
 g.copiedSaveLab=runtime.authorization.copiedSaveLab;g.standingCycleProfile=g.privateCurrent&&runtime.authorization.profileCurrent&&runtime.authorization.profileCurrent(runtime.directory);return g;}
std::uint64_t Mix(std::uint64_t h,std::uint64_t x) noexcept{return (h^x)*1099511628211ull;}
// Cheap coverage filter; uncovered and foreign calls retain normal behavior.
// A malformed spec is observed as unknown only within the bounded sample cap.
bool InputKey(const q::Event& e,std::uintptr_t& key,std::uint64_t& hash) noexcept {
 Reader r{};hash=1469598103934665603ull;key=e.engine;
 if(e.path==q::Path::raw){hash=Mix(hash,e.id);return q::Covered(e.id);}
 if(!q::Aligned(e.spec,8)||!q::Aligned(e.context,8))return false;
 std::int32_t n=0;if(!q::Get(r,e.spec+0x200,n)||!q::Get(r,e.context+0x18,key)||n<0||n>int(q::kSpecs))return false;
 hash=Mix(hash,unsigned(n));bool covered=false;
 for(int i=0;i<n;++i){std::uint64_t id=0;if(!q::Get(r,e.spec+std::uintptr_t(i)*0x20,id))return false;covered|=q::Covered(id);hash=Mix(hash,id);}return covered;
}
bool ReadCache(const q::Event& e,std::uintptr_t key,std::uint64_t input,Cache& out,q::Owner& phaseOwner) noexcept {
 bool found=false;if(!TryAcquireSRWLockShared(&runtime.cacheLock))return false;
 for(const auto& c:runtime.cache)if(c.filled&&c.path==e.path&&c.key==key&&c.inputHash==input){out=c;found=true;break;}
 if(found&&out.owner.body<2)phaseOwner=runtime.lastOwner[out.owner.body];ReleaseSRWLockShared(&runtime.cacheLock);return found;
}
bool SameCheapPhase(const q::Owner& owner) noexcept {
 if(!owner.known||!owner.phase.observed)return false;Reader read{};std::uintptr_t p=0,records=0,callback=0,node=0;std::uint8_t mode=0,request=0,counter=0;std::uint64_t remembered=0;
 const auto& s=owner.phase;std::uint32_t currentLocal=UINT32_MAX;
 return q::Get(read,runtime.image+0x2a82c30,p)&&p==owner.player&&q::Get(read,p+0x214,currentLocal)&&currentLocal==owner.currentLocal&&q::Get(read,s.primary+0x78,records)&&records==s.records&&
  q::Get(read,records,callback)&&callback==s.callback&&q::Get(read,records+0x10,mode)&&mode==s.mode&&
  q::Get(read,records+0x11,request)&&request==s.request&&q::Get(read,records+0x12,counter)&&counter==s.counter&&
  q::Get(read,records+0x18,remembered)&&(remembered&0xffffffffffffull)==s.rememberedGraph&&
  (!s.graphObserved||(q::Get(read,s.controller+0x1a0,node)&&node==s.graphNode));
}
// This hash detects a changed ACTUAL returned source before deciding whether
// another full post-only diagnostic is needed. It grants no source ownership.
bool OutputKey(const q::Event& e,std::uintptr_t raw,bool setup,std::uint64_t& hash) noexcept {
 Reader r{};hash=1469598103934665603ull;std::uintptr_t viewAt=0;std::int32_t n=1,index=0;
 if(e.path==q::Path::setup){if(!setup)return false;if(!q::Get(r,e.spec+0x200,n)||n<0||n>int(q::kSpecs)||!q::Get(r,e.destination+0x48,index)||(index!=0&&index!=1)||!q::Get(r,e.destination+std::uintptr_t(index)*0x20,viewAt)||!q::Aligned(viewAt,8))return false;}
 for(int i=0;i<n;++i){std::uintptr_t source=raw,main=0,end=0,bank=0,blob=0;
  if(e.path==q::Path::setup){std::int32_t offset=0;if(!q::Get(r,viewAt+0x30+std::uintptr_t(i)*4,offset)||offset<=0||offset>0x400000||!q::Range(viewAt,unsigned(offset)+0x90)||
   !q::Get(r,viewAt+unsigned(offset)+0x80,source)||!q::Get(r,viewAt+unsigned(offset)+0x58,main)||!q::Get(r,viewAt+unsigned(offset)+0x88,end))return false;}
  if(!q::Aligned(source,8)||!q::Range(source,0x28)||!q::Get(r,source+8,bank)||!q::Aligned(bank,8)||!q::Range(bank,0xa0)||!q::Get(r,bank+0x98,blob))return false;
  hash=Mix(Mix(Mix(Mix(Mix(hash,source),bank),blob),main),end);
 }return true;
}
void Report(const q::Receipt& receipt,std::uintptr_t key,std::uint64_t input) noexcept {
 q::ErrorPreserve error{};const auto& a=receipt.after.inputObserved?receipt.after:receipt.before;
 const auto& owner=a.owner;std::uint64_t output=0;OutputKey(receipt.event,receipt.rawResult,receipt.setupResult,output);
 bool duplicate=false;
 if(TryAcquireSRWLockExclusive(&runtime.cacheLock)){
  Cache* chosen=nullptr;for(auto& c:runtime.cache)if(c.filled&&c.path==receipt.event.path&&c.key==key&&c.inputHash==input){chosen=&c;break;}
  if(!chosen)for(auto& c:runtime.cache)if(!c.filled){chosen=&c;break;}
  if(chosen){duplicate=chosen->filled&&q::SameOwner(chosen->owner,owner)&&chosen->owner.phase.mode==owner.phase.mode&&chosen->owner.phase.request==owner.phase.request&&
   chosen->owner.phase.counter==owner.phase.counter&&chosen->owner.phase.rememberedGraph==owner.phase.rememberedGraph&&chosen->owner.phase.currentGraph==owner.phase.currentGraph&&chosen->outputHash==output;
   *chosen={owner,receipt.event.path,input,output,key,true};}
  if(owner.known&&owner.body<2)runtime.lastOwner[owner.body]=owner;ReleaseSRWLockExclusive(&runtime.cacheLock);
 }
 if(duplicate){InterlockedIncrement64(&runtime.suppressed);return;}
 if(!owner.known&&InterlockedIncrement64(&runtime.unknown)>16)return;
 const auto snapshot=InterlockedIncrement64(&runtime.logs);if(snapshot>256||!runtime.log)return;
 char line[2300]{};const auto& phase=owner.phase;const auto& pre=receipt.before.owner.phase;
 StringCchPrintfA(line,ARRAYSIZE(line),"sleep_lookup=original_forward_observation;snapshot=%llu;path=%s;thread=%lu;callsite=%llx;body=%u;owner_known=%u;local_observed=%u;P=%llx;record=%llx;engine=%llx;engine_arg=%llx;destination_arg=%llx;spec_arg=%llx;context_arg=%llx;bank_array=%llx;spec_count=%d;spec_layer=%u;mode=%u;request=%u;counter=%u;callback=%llx;remembered=%llx;current_graph=%llx;phase_observed=%u;graph_observed=%u;before_phase_observed=%u;before_mode=%u;before_request=%u;before_counter=%u;before_remembered=%llx;before_current_graph=%llx;q=%u;life=%u;result=%llx;setup_result=%u;owner_stable=%u;after_only=%u;output_observed=%u;epoch=unknown;generation=unknown;native_thread_authority=unknown;selection=0;append=0;why=%s\r\n",
  static_cast<unsigned long long>(snapshot),receipt.event.path==q::Path::raw?"raw":"setup",GetCurrentThreadId(),static_cast<unsigned long long>(receipt.event.returnAddress-runtime.image),owner.body,owner.known?1u:0u,owner.currentLocal,
  static_cast<unsigned long long>(owner.player),static_cast<unsigned long long>(owner.record),static_cast<unsigned long long>(owner.engine),static_cast<unsigned long long>(receipt.event.engine),static_cast<unsigned long long>(receipt.event.destination),static_cast<unsigned long long>(receipt.event.spec),static_cast<unsigned long long>(receipt.event.context),static_cast<unsigned long long>(owner.banks),a.specCount,a.specLayer,
  unsigned(phase.mode),unsigned(phase.request),unsigned(phase.counter),static_cast<unsigned long long>(phase.callback?phase.callback-runtime.image:0),static_cast<unsigned long long>(phase.rememberedGraph),static_cast<unsigned long long>(phase.currentGraph),
  phase.observed?1u:0u,phase.graphObserved?1u:0u,pre.observed?1u:0u,unsigned(pre.mode),unsigned(pre.request),unsigned(pre.counter),static_cast<unsigned long long>(pre.rememberedGraph),static_cast<unsigned long long>(pre.currentGraph),unsigned(phase.qAction),phase.life,static_cast<unsigned long long>(receipt.rawResult),receipt.setupResult?1u:0u,receipt.identityStable?1u:0u,receipt.afterOnly?1u:0u,a.outputObserved?1u:0u,a.reason);runtime.log(line);
 if(a.inputObserved)for(unsigned i=0;i<unsigned(a.specCount);++i){const auto& s=a.selected[i];char row[900]{};
  StringCchPrintfA(row,ARRAYSIZE(row),"sleep_lookup=original_source;snapshot=%llu;path=%s;body=%u;spec_index=%u;full_id=%llx;covered=%u;observed=%u;bank_bound=%u;slot=%u;source_index=%u;descriptor=%llx;bank=%llx;blob=%llx;file_identity=%llx;bank_count=%u;gani=%llx;master=%llx;main=%llx;end=%llx;selection=0;payload_hash=unverified\r\n",
   static_cast<unsigned long long>(snapshot),receipt.event.path==q::Path::raw?"raw":"setup",owner.body,i,static_cast<unsigned long long>(a.ids[i]),a.covered[i]?1u:0u,s.observed?1u:0u,s.bankBound?1u:0u,s.slot,s.index,
   static_cast<unsigned long long>(s.descriptor),static_cast<unsigned long long>(s.bank),static_cast<unsigned long long>(s.blob),static_cast<unsigned long long>(s.fileIdentity),s.bankCount,
   static_cast<unsigned long long>(s.gani),static_cast<unsigned long long>(s.master),static_cast<unsigned long long>(s.main),static_cast<unsigned long long>(s.end));runtime.log(row);}
 if(!a.inputObserved){Reader read{};std::int32_t count=1;if(receipt.event.path==q::Path::setup&&(!q::Get(read,receipt.event.spec+0x200,count)||count<0||count>int(q::kSpecs)))count=-1;
  char summary[280]{};StringCchPrintfA(summary,ARRAYSIZE(summary),"sleep_lookup=opaque_native_spec;snapshot=%llu;path=%s;spec_count=%d;owner=unknown;schema_only=1;source_ownership=unknown;selection=0\r\n",static_cast<unsigned long long>(snapshot),receipt.event.path==q::Path::raw?"raw":"setup",count);runtime.log(summary);
  for(int i=0;i<count;++i){std::uint64_t id=receipt.event.id;if(receipt.event.path==q::Path::setup&&!q::Get(read,receipt.event.spec+std::uintptr_t(i)*0x20,id))break;char row[240]{};
   StringCchPrintfA(row,ARRAYSIZE(row),"sleep_lookup=opaque_native_id;snapshot=%llu;spec_index=%u;full_id=%llx;covered=%u;owner=unknown;descriptor=unqualified;selection=0\r\n",static_cast<unsigned long long>(snapshot),unsigned(i),static_cast<unsigned long long>(id),q::Covered(id)?1u:0u);runtime.log(row);}
 }
}
bool Begin(const q::Event& e,std::uintptr_t& key,std::uint64_t& input,Cache& cached,bool& full) noexcept {
 q::ErrorPreserve preserve{};full=false;if(!InputKey(e,key,input)){InterlockedIncrement64(&runtime.uncovered);return false;}
 if(!q::Natural({true,true,true,true,true,false,runtime.image},e))return false;
 q::Owner phaseOwner{};const bool have=ReadCache(e,key,input,cached,phaseOwner);
 full=!have||!SameCheapPhase(phaseOwner);
 if(full&&InterlockedIncrement64(&runtime.fullSamples)>256){InterlockedIncrement64(&runtime.suppressed);return false;}return true;
}
void AfterOnly(const q::Event& e,std::uintptr_t raw,bool setup,std::uintptr_t key,std::uint64_t input,const Cache& cached) noexcept {
 q::ErrorPreserve preserve{};std::uint64_t output=0;if(!OutputKey(e,raw,setup,output)||output==cached.outputHash){InterlockedIncrement64(&runtime.suppressed);return;}
 if(InterlockedIncrement64(&runtime.fullSamples)>256)return;
 q::Receipt receipt{};receipt.event=e;receipt.rawResult=raw;receipt.setupResult=setup;receipt.afterOnly=true;
 Reader read{};q::NativePhaseReader phase{};const auto g=Gate();q::Input(read,phase,g,e,receipt.after);q::CurrentResult(read,g,e,raw,setup,receipt.after);Report(receipt,key,input);
}
__declspec(noinline) std::uintptr_t RawHook(std::uintptr_t engine,std::uint64_t id) {
 if(inHook||q::inObserver)return runtime.raw(engine,id);HookLease lease{};InterlockedIncrement64(&runtime.rawCalls);
 q::Event event{};event.engine=engine;event.id=id;event.returnAddress=reinterpret_cast<std::uintptr_t>(_ReturnAddress());std::uintptr_t key=0;std::uint64_t input=0;Cache cache{};bool full=false;
 if(!Begin(event,key,input,cache,full))return runtime.raw(engine,id);
 if(!full){const auto result=runtime.raw(engine,id);AfterOnly(event,result,false,key,input,cache);return result;}
 Reader read{};q::NativePhaseReader phase{};const auto gate=Gate();auto report=[&](const q::Receipt& receipt) noexcept {Report(receipt,key,input);};return q::RawForward(runtime.raw,read,phase,report,gate,event);
}
__declspec(noinline) bool SetupHook(std::uintptr_t destination,std::uintptr_t spec,std::uintptr_t arg3,std::uintptr_t arg4,std::uintptr_t context) {
 if(inHook||q::inObserver)return runtime.setup(destination,spec,arg3,arg4,context);HookLease lease{};InterlockedIncrement64(&runtime.setupCalls);
 q::Event event{};event.path=q::Path::setup;event.destination=destination;event.spec=spec;event.arg3=arg3;event.arg4=arg4;event.context=context;event.returnAddress=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
 std::uintptr_t key=0;std::uint64_t input=0;Cache cache{};bool full=false;if(!Begin(event,key,input,cache,full))return runtime.setup(destination,spec,arg3,arg4,context);
 if(!full){const auto result=runtime.setup(destination,spec,arg3,arg4,context);AfterOnly(event,0,result,key,input,cache);return result;}
 Reader read{};q::NativePhaseReader phase{};const auto gate=Gate();auto report=[&](const q::Receipt& receipt) noexcept {Report(receipt,key,input);};return q::SetupForward(runtime.setup,read,phase,report,gate,event);
}
bool CodeGate(const Authorization& a,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 unsigned char bytes[8192]{};for(const auto& s:qualified_sleep_lookup_contract::sites){if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!a.readOriginal(image+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}}
 auto read=a.readOriginal;return sleep_plugin_read::CodeGate(read,image,span,mismatch)&&sleep_graph_read::CodeGate(read,image,span,mismatch);
}
}
bool Prepare(std::uintptr_t image,std::size_t span,const wchar_t* directory,const Authorization& a,Log log) noexcept {
 q::ErrorPreserve preserve{};if(Prepared())return runtime.image==image&&q::Admitted(Gate());
 if(InterlockedCompareExchange(&runtime.exposed,0,0)||!image||span!=0xa01d000||!directory||!log||!a.exactRetailPe||!a.offline||!a.copiedSaveLab||!a.privateWorkerCurrent||!a.privateWorkerCurrent()||!a.profileCurrent||!a.profileCurrent(directory)||!a.readOriginal)return false;
 std::uintptr_t mismatch=0;if(!CodeGate(a,image,span,mismatch)){char line[180]{};StringCchPrintfA(line,ARRAYSIZE(line),"sleep_lookup=prepare_refused;stage=complete_original_gate;rva=%llx;selection=0\r\n",static_cast<unsigned long long>(mismatch));log(line);return false;}
 if(FAILED(StringCchCopyW(runtime.directory,ARRAYSIZE(runtime.directory),directory)))return false;runtime.image=image;runtime.span=span;runtime.authorization=a;runtime.log=log;runtime.gate=true;
 Reader read{};if(!view::Bind(runtime.book,image,span)||!view::Capture(read,runtime.book,0,view::Kind::detour,image+0x1a6c300,reinterpret_cast<std::uintptr_t>(&RawHook),qualified_sleep_lookup_contract::bytes_0,5)||
  !view::Capture(read,runtime.book,1,view::Kind::detour,image+0x1a80670,reinterpret_cast<std::uintptr_t>(&SetupHook),qualified_sleep_lookup_contract::bytes_1,5))return false;
 const auto initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
 void* raw=reinterpret_cast<void*>(image+0x1a6c300);void* setup=reinterpret_cast<void*>(image+0x1a80670);
 if(MH_CreateHook(raw,reinterpret_cast<void*>(&RawHook),reinterpret_cast<void**>(&runtime.raw))!=MH_OK)return false;
 if(MH_CreateHook(setup,reinterpret_cast<void*>(&SetupHook),reinterpret_cast<void**>(&runtime.setup))!=MH_OK){MH_RemoveHook(raw);return false;}
 runtime.book.patches[0].created=runtime.book.patches[1].created=true;runtime.book.patches[0].trampoline=reinterpret_cast<std::uintptr_t>(runtime.raw);runtime.book.patches[1].trampoline=reinterpret_cast<std::uintptr_t>(runtime.setup);
 // Originals and book records live for the private process. After ANY native
 // entry exposure, never free trampolines, relays, callbacks or their receipts.
 InterlockedExchange(&runtime.exposed,1);
 if(MH_EnableHook(raw)!=MH_OK)return false;
 AcquireSRWLockExclusive(&runtime.bookLock);const bool rawProof=view::Activated(read,runtime.book,0);ReleaseSRWLockExclusive(&runtime.bookLock);if(!rawProof)return false;
 if(MH_EnableHook(setup)!=MH_OK)return false;
 AcquireSRWLockExclusive(&runtime.bookLock);const bool setupProof=view::Activated(read,runtime.book,1);ReleaseSRWLockExclusive(&runtime.bookLock);if(!setupProof)return false;
 InterlockedExchange(&runtime.installed,1);log("sleep_lookup=observer_installed;profile=mgo_standing_cycle_v1;raw=1a6c300;setup=1a80670;original_forward_only=1;selection=0;append=0;clone=0;max_full_samples=256;max_logs=256\r\n");return true;
}
bool Prepared() noexcept {q::ErrorPreserve preserve{};return InterlockedCompareExchange(&runtime.installed,0,0)==1&&HeadsLive();}
bool OverlayOriginal(std::uintptr_t at,void* out,std::size_t n) noexcept {q::ErrorPreserve preserve{};Reader read{};if(!TryAcquireSRWLockShared(&runtime.bookLock))return false;const bool okay=view::Overlay(read,runtime.book,at,out,n);ReleaseSRWLockShared(&runtime.bookLock);return okay;}
bool Status(char* out,std::size_t n) noexcept {q::ErrorPreserve preserve{};if(!out||!n)return false;const int wrote=std::snprintf(out,n,"sleepLookup;on=%u;raw=%lld;setup=%lld;uncovered=%lld;full=%lld;logs=%lld;dupe=%lld;unknown=%lld;select=0",Prepared()?1u:0u,
 InterlockedCompareExchange64(&runtime.rawCalls,0,0),InterlockedCompareExchange64(&runtime.setupCalls,0,0),InterlockedCompareExchange64(&runtime.uncovered,0,0),InterlockedCompareExchange64(&runtime.fullSamples,0,0),InterlockedCompareExchange64(&runtime.logs,0,0),InterlockedCompareExchange64(&runtime.suppressed,0,0),InterlockedCompareExchange64(&runtime.unknown,0,0));return wrote>=0&&std::size_t(wrote)<n;}
#else
bool Prepare(std::uintptr_t,std::size_t,const wchar_t*,const Authorization&,Log) noexcept{return false;}
bool Prepared() noexcept{return false;}
bool OverlayOriginal(std::uintptr_t,void*,std::size_t) noexcept{return true;}
bool Status(char* out,std::size_t n) noexcept {return out&&n&&SUCCEEDED(StringCchCopyA(out,n,"sleepLookup;on=0;select=0;why=compiled_off"));}
#endif
}
