// Read-only private-worker diagnostics. No cargo writer is registered.
#include "horse_physical_callback_forward.hpp"
#include "horse_physical_probe_glue.hpp"
#include "horse_physical_natural_cache.hpp"
#include "mgscoop/partner_defer.hpp"
#include <intrin.h>
#include "mgscoop/worker_scenario.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "MinHook.h"
namespace mgscoop::horse_physical_probe_glue {
namespace {
namespace cb=horse_physical_callback_probe;
cb::Probe probe{};cb::Original original=nullptr;cb::SampleFn sampler=nullptr;
void* samplerContext=nullptr;std::uintptr_t imageBase=0;
std::atomic<unsigned> inFlight{0};bool created=false,enabled=false;
namespace nr=horse_physical_natural_read;
nr::Cache naturalCache{};
struct AtomicNaturalRow {
 static constexpr std::size_t count=(sizeof(nr::Row)+7)/8;
 std::atomic<std::uint64_t> words[count]{},sequence{0};std::atomic_flag writer=ATOMIC_FLAG_INIT;
 void Put(const nr::Row& row) noexcept {
  if(writer.test_and_set(std::memory_order_acquire))return;
  std::uint64_t copy[count]{};std::memcpy(copy,&row,sizeof(row));sequence.fetch_add(1,std::memory_order_acq_rel);
  for(std::size_t k=0;k<count;++k)words[k].store(copy[k],std::memory_order_relaxed);
  sequence.fetch_add(1,std::memory_order_release);writer.clear(std::memory_order_release);
 }
 bool Read(nr::Row& row) const noexcept {
  for(unsigned retry=0;retry<3;++retry){const auto before=sequence.load(std::memory_order_acquire);if(before&1u)continue;
   std::uint64_t copy[count]{};for(std::size_t k=0;k<count;++k)copy[k]=words[k].load(std::memory_order_relaxed);
   if(before!=sequence.load(std::memory_order_acquire))continue;
   std::memcpy(&row,copy,sizeof(row));if(!before)row={};return true;}return false;
 }
} naturalRows[2];
std::atomic<std::uintptr_t> naturalSelf[2]{};
std::atomic<std::uint64_t> naturalLast[2]{};
std::uint32_t NaturalWindow() noexcept{return partner_input_hook::WindowSeq();}
bool NaturalBatch() noexcept{return partner_defer::InBatch();}
void ObserveNatural(const cb::RawWindow& raw) noexcept {
 native_work_timing::Scope measured(native_work_timing::Kind::horse_natural);
 unsigned body=2;for(unsigned k=0;k<2;++k)if(raw.self&&raw.self==naturalSelf[k].load(std::memory_order_acquire))body=k;
 if(body>1)return;
 const auto now=GetTickCount64();auto previous=naturalLast[body].load(std::memory_order_acquire);
 if(now>=previous&&now-previous<200)return;
 if(!naturalLast[body].compare_exchange_strong(previous,now,std::memory_order_acq_rel))return;
 auto reader=[](std::uintptr_t at,void* out,std::size_t size) noexcept {SIZE_T done=0;return at&&size&&
  ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&done)&&done==size;};
 nr::Row row{};nr::ObserveCached(reader,naturalCache,raw,GetTickCount64(),NaturalWindow,NaturalBatch,row);naturalRows[body].Put(row);
}

void HookPhysical(void* self,void* poseContext) {
    const DWORD incoming=GetLastError();
    const cb::RawWindow raw{reinterpret_cast<std::uintptr_t>(self),reinterpret_cast<std::uintptr_t>(poseContext),
      reinterpret_cast<std::uintptr_t>(_ReturnAddress()),GetCurrentThreadId()};
    inFlight.fetch_add(1,std::memory_order_acq_rel);
    __try {if(!cb::nesting)ObserveNatural(raw);SetLastError(incoming);cb::ForwardOriginal(original,raw,&probe,sampler,samplerContext);}
    __finally {inFlight.fetch_sub(1,std::memory_order_release);}
}
struct PreserveError {DWORD value=GetLastError();~PreserveError() noexcept {SetLastError(value);}};
}
bool Install(const void* image,std::size_t span,bool privateAdmission,cb::SampleFn sample,void* context) noexcept {
    const PreserveError preserve;
    if(!privateAdmission||!worker_scenario::Compiled()||!worker_scenario::ThreadAdmitted()||!image||!sample)return false;
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    if(created)return enabled&&imageBase==base&&sampler==sample&&samplerContext==context;
    auto code=partner_input_hook::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
    if(!horse_player_physical_read::CodeGate(code,base,span,mismatch)||
       !horse_physical_profile_read::CodeGate(code,base,span,mismatch)||!nr::CodeGate(code,base,span,mismatch))return false;
    imageBase=base;probe.image=base;sampler=sample;samplerContext=context;
    void* target=reinterpret_cast<void*>(base+0x1ad8250);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&HookPhysical),reinterpret_cast<void**>(&original))!=MH_OK){
        original=nullptr;imageBase=0;sampler=nullptr;samplerContext=nullptr;return false;
    }
    created=true;
    if(!original||MH_EnableHook(target)!=MH_OK){
        MH_DisableHook(target);MH_RemoveHook(target);created=false;original=nullptr;
        imageBase=0;sampler=nullptr;samplerContext=nullptr;return false;
    }
    enabled=true;probe.installed.store(true,std::memory_order_release);return true;
}
bool Line(char* out,std::size_t capacity) noexcept {const PreserveError preserve;return probe.Line(out,capacity);}
bool RawLine(char* out,std::size_t capacity) noexcept {const PreserveError preserve;return probe.RawLine(out,capacity);}
void RefreshCandidates(const horse_player_physical_read::Observation& current,std::uint32_t ownerThread,bool ready) noexcept {
    probe.Candidates(current,ownerThread,ready);
}
void PublishNatural(const nr::Published& pair) noexcept {
 if(!enabled||!pair.admitted||!pair.exactPins||pair.image!=imageBase)return;
 if(naturalCache.Publish(pair))for(unsigned k=0;k<2;++k)naturalSelf[k].store(pair.binding[k].component,std::memory_order_release);
}
void ClearNatural() noexcept {for(auto& self:naturalSelf)self.store(0,std::memory_order_release);naturalCache.Clear();}
bool NaturalLine(unsigned body,bool life,char* out,std::size_t capacity) noexcept {
 const PreserveError preserve;if(body>1)return false;nr::Row row{};if(!naturalRows[body].Read(row))return false;
 return life?nr::LifeLine(row,out,capacity):nr::Line(row,out,capacity);
}
bool RemoveIfQuiescent() noexcept {
    const PreserveError preserve;ClearNatural();if(!created)return true;
    void* target=reinterpret_cast<void*>(imageBase+0x1ad8250);
    if(enabled){if(MH_DisableHook(target)!=MH_OK)return false;enabled=false;probe.installed.store(false,std::memory_order_release);}
    if(inFlight.load(std::memory_order_acquire))return false;
    if(MH_RemoveHook(target)!=MH_OK)return false;
    created=false;original=nullptr;imageBase=0;sampler=nullptr;samplerContext=nullptr;return true;
}
}
