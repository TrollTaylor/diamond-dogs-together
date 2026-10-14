// Root integration: exact private read-only first common-bank observer.
// No live unload. Both originals forward unchanged on every intercepted call.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <strsafe.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <climits>
#include "sleep_unique_bind_runtime.hpp"
#include "sleep_unique_bind_adapter.hpp"
#include "mgscoop/archive_mount.hpp"
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER)
#include "MinHook.h"
#endif
namespace mgscoop::sleep_unique_bind_runtime {
namespace ub=sleep_unique_bind;namespace an=sleep_unique_bind_native;
namespace bt=sleep_native_bank_trace;
namespace raw=sleep_raw_constructor_trace;
#if defined(MGS_BACKGROUND_WORKER) && defined(MGS_LOCAL_MGO_UNIQUE_REST_OBSERVER)
namespace {
struct Runtime {
 std::uintptr_t image=0;std::size_t span=0;wchar_t directory[32768]{};
 Authorization authorization{};Log log=nullptr;an::Configuration adapter{};
 unsigned char commonPatch[32]{},appendPatch[32]{};
 bool exactGate=false;volatile LONG installed=0;volatile LONG64 invocation=0;
 SRWLOCK statusLock=SRWLOCK_INIT;ub::Reply last[2]{};ub::Event events[2]{};
 unsigned seen[2]{};const char* reason="never";
 bt::Receipt traceLast[2]{};unsigned traceSeen[2]{};
};
Runtime runtime;
bool Read(void*,std::uintptr_t at,void* out,std::size_t n) noexcept {
 const DWORD error=GetLastError();SIZE_T done=0;const bool ok=out&&ub::base::Range(at,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,n,&done)&&done==n;
 SetLastError(error);return ok;
}
bool Hash(void*,const void* bytes,std::size_t n,char* out) noexcept {
 if(!bytes||!n||n>ub::kUniqueBytes||!out)return false;
 BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[32]{};
 bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
 if(ok)ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
 if(ok)ok=BCryptHashData(hash,const_cast<unsigned char*>(static_cast<const unsigned char*>(bytes)),static_cast<ULONG>(n),0)>=0;
 if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
 if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
 if(!ok)return false;constexpr char alphabet[]="0123456789abcdef";
 for(unsigned i=0;i<32;++i){out[2*i]=alphabet[digest[i]>>4];out[2*i+1]=alphabet[digest[i]&15];}out[64]=0;return true;
}
bool HooksOwned() noexcept {
 unsigned char a[32]{},b[32]{};
 return InterlockedCompareExchange(&runtime.installed,0,0)==1&&
  Read(nullptr,runtime.image+0x9bb960,a,sizeof(a))&&Read(nullptr,runtime.image+0x1a6b8f0,b,sizeof(b))&&
  !std::memcmp(a,runtime.commonPatch,sizeof(a))&&!std::memcmp(b,runtime.appendPatch,sizeof(b));
}
bool CurrentPrivate() noexcept {
 const DWORD error=GetLastError();const bool ok=runtime.exactGate&&runtime.authorization.privateWorkerCurrent&&runtime.authorization.privateWorkerCurrent()&&
  MountedMgoUniqueRestContext(runtime.directory)&&HooksOwned();
 SetLastError(error);return ok;
}
bool Scope(void*,const ub::Event& event,ub::Scope& out) noexcept {
 out={};if(!CurrentPrivate()||event.nativeActor>1)return false;
 out.exactPe=runtime.authorization.exactRetailPe;out.codeGate=runtime.exactGate;
 out.privateDesktop=out.privateWorker=out.exactProfile=true;out.copiedSaveLab=runtime.authorization.copiedSaveLab;
 // Only diagnostics use this fresh typed owner scope. Common hook receipts
 // identify their actual frame; explicit status reads do not claim a frame.
 // No Lua epoch/generation, gameplay or append authority is manufactured.
 out.coldReadOnly=true;out.creatorActive=true;out.image=runtime.image;
 out.creatorRecord=event.record;out.expectedNativeActor=event.nativeActor;
 out.thread=out.ownerThread=GetCurrentThreadId();out.now=out.poll=GetTickCount64();
 out.creatorInvocation=event.creatorInvocation?event.creatorInvocation:static_cast<std::uint64_t>(InterlockedIncrement64(&runtime.invocation));
 auto reader=[](std::uintptr_t at,void* bytes,std::size_t n)noexcept{return Read(nullptr,at,bytes,n);};
 std::uintptr_t p=0,owner=0,q=0,state=0,vt=0;std::uint32_t id=0,count=0;
 if(!ub::base::Get(reader,runtime.image+0x2a82c30,p)||!ub::base::Aligned(p,8)||
    !ub::base::Get(reader,event.record,vt)||vt!=runtime.image+0x2295210||
    !ub::base::Get(reader,event.record+0x138,owner)||owner!=p||!ub::base::Get(reader,event.record+8,count)||count!=1||
    !ub::base::Get(reader,event.record+0xc,id)||id>1||
    !ub::base::Get(reader,p+0x60,q)||!ub::base::Aligned(q,8)||!ub::base::Get(reader,q+0x1e8,state)||!ub::base::Aligned(state,8))return false;
 out.player=p;out.data=q;out.state=state;out.creatorId=id;out.creatorOwned=true;
 return true;
}
bool TraceScope(void*,const ub::Event& event,ub::Scope& out) noexcept {
 if(event.nativeActor!=UINT32_MAX)return Scope(nullptr,event,out);
 out={};if(!CurrentPrivate()||!ub::base::Aligned(event.engine,8))return false;
 auto read=[](std::uintptr_t at,void* bytes,std::size_t n)noexcept{return Read(nullptr,at,bytes,n);};
 std::uintptr_t p=0,records=0,pools=0,table=0;
 if(!ub::base::Get(read,runtime.image+0x2a82c30,p)||!ub::base::Aligned(p,8)||
    !ub::base::Get(read,p+0x378,records)||!ub::base::Aligned(records,8)||
    !ub::base::Get(read,p+0x78,pools)||!pools||!ub::base::Get(read,p+0x50,table)||!ub::base::Aligned(table,8))return false;
 bool found=false;ub::Scope chosen{};
 for(unsigned body=0;body<2;++body){
  std::uint8_t pool=0xff;std::uintptr_t record=0,owner=0,vt=0,rows=0,engine=0;std::uint32_t count=0,base=0;
  if(!ub::base::Get(read,pools+body,pool)||pool>=16)continue;
  record=records+std::uintptr_t(pool)*0x6a8;std::uintptr_t mapped=0;
  if(!ub::base::Get(read,table+std::uintptr_t(pool)*8,mapped)||mapped!=record||
     !ub::base::Get(read,record+0x10,owner)||!ub::base::Aligned(owner,8)||!ub::base::Get(read,owner,vt)||vt!=runtime.image+0x22e6070||
     !ub::base::Get(read,owner+8,count)||count!=1||!ub::base::Get(read,owner+0xc,base)||base!=body||
     !ub::base::Get(read,owner+0x10,rows)||!ub::base::Aligned(rows,16)||!ub::base::Get(read,rows+0x70,engine)||engine!=event.engine)continue;
  ub::Event current=event;current.record=record;current.nativeActor=body;ub::Scope scope{};
  if(!Scope(nullptr,current,scope))continue;if(found)return false;chosen=scope;found=true;
 }
 if(found)out=chosen;return found;
}
bool RawConstructorScope(void*,const ub::Event& event,raw::Scope& out) noexcept {
 out={};if(!CurrentPrivate()||event.returnAddress!=runtime.image+0x9c3fc1||event.record||event.nativeActor!=UINT32_MAX||event.creatorInvocation)return false;
 out.exactPe=runtime.authorization.exactRetailPe;out.codeGate=runtime.exactGate;
 out.privateCurrent=true;out.copiedSaveLab=runtime.authorization.copiedSaveLab;out.exactProfile=true;
 out.image=runtime.image;out.thread=GetCurrentThreadId();out.now=GetTickCount64();
 // Opaque typed native arguments only. Do not assign a player, creator,
 // epoch, generation, owner thread or synthesized actor mapping here.
 return true;
}
const char* PhaseName(bt::Phase phase) noexcept {
 switch(phase){case bt::Phase::commonEnter:return "common_enter";case bt::Phase::commonLeave:return "common_leave";
 case bt::Phase::commonBefore:return "common_before";case bt::Phase::commonAfter:return "common_after";
 case bt::Phase::nativeBefore:return "native_before";case bt::Phase::nativeAfter:return "native_after";default:return "status";}
}
void TraceBankRows(const bt::Receipt& receipt,const bt::Snapshot& snapshot,const char* stage) noexcept {
 for(unsigned slot=0;slot<snapshot.owner.bankCapacity&&slot<ub::kBankSlotsCeiling;++slot){
  const auto& bank=snapshot.banks[slot];if(!bank.pointer)continue;unsigned commonSlot=32;
  for(unsigned c=0;c<32;++c)if(snapshot.common[c]==bank.pointer){commonSlot=c;break;}
  char line[760]{};StringCchPrintfA(line,ARRAYSIZE(line),
   "unique_bind=natural_bank_slot;body=%u;frame=%llu;phase=%s;stage=%s;ordinal=%u;engine=%llx;slot=%u;bank=%llx;file_identity=%llx;typed=%u;loaded=%u;count=%u;rest_mask=%x;rest_indices=%u,%u;rest_sources=%llx,%llx;unique_verified=%u;common_slot=%u;append_authority=0;selected=unverified\r\n",
   receipt.event.nativeActor,static_cast<unsigned long long>(receipt.frameSerial),PhaseName(receipt.phase),stage,receipt.ordinal,
   static_cast<unsigned long long>(snapshot.owner.engine),slot,static_cast<unsigned long long>(bank.pointer),static_cast<unsigned long long>(bank.fileIdentity),
   bank.typed?1u:0u,bank.loaded?1u:0u,bank.count,bank.restMask,bank.restIndices[0],bank.restIndices[1],
   static_cast<unsigned long long>(bank.restSources[0]),static_cast<unsigned long long>(bank.restSources[1]),bank.uniqueVerified?1u:0u,commonSlot);
  runtime.log(line);
 }
}
void TraceReport(void*,const bt::Receipt& receipt) noexcept {
 if(!runtime.log)return;
 if(receipt.rawConstructor){
  static volatile LONG rawSeen=0;const auto seen=static_cast<unsigned>(InterlockedIncrement(&rawSeen));if(seen>128)return;
  const auto& a=receipt.before;const auto& z=receipt.after;const auto& incoming=receipt.incoming;
  char line[2100]{};StringCchPrintfA(line,ARRAYSIZE(line),
   "unique_bind=raw_constructor_trace;seen=%u;call_serial=%llu;thread=%u;tick_before=%llu;tick_after=%llu;callsite=%llx;body=unknown;record=0;engine_arg=%llx;engine_vt=%llx;engine_data=%llx;bank_array=%llx;capacity=%u;empty_before=%u;empty_after=%u;first_empty=%u;default_bank=%llx;default_file_identity=%llx;default_typed=%u;default_loaded=%u;default_count=%u;default_rest_mask=%x;default_rest_indices=%u,%u;common_unique_before=%u;common_unique_after=%u;unique_slot_before=%u;unique_slot_after=%u;unique_bank_before=%llx;unique_bank_after=%llx;unique_identity_before=%llx;original_result=%u;raw_engine_stable=%u;slot_changed=%u;changed_slot=%u;preexisting=%u;duplicate=%u;typed_ready=%u;reason=%s;owner_closure=unavailable;builder_lifetime=append_call_only;append_authority=0;selected=unverified\r\n",
   seen,static_cast<unsigned long long>(receipt.rawSerial),receipt.rawThread,static_cast<unsigned long long>(receipt.rawBeforeTick),static_cast<unsigned long long>(receipt.rawAfterTick),static_cast<unsigned long long>(receipt.event.returnAddress-runtime.image),
   static_cast<unsigned long long>(receipt.event.engine),static_cast<unsigned long long>(a.owner.engineVtable),static_cast<unsigned long long>(a.owner.engineData),
   static_cast<unsigned long long>(a.owner.bankArray),a.owner.bankCapacity,a.empty,z.empty,a.firstEmpty,
   static_cast<unsigned long long>(receipt.event.originalBank),static_cast<unsigned long long>(incoming.fileIdentity),incoming.typed?1u:0u,incoming.loaded?1u:0u,incoming.count,
   incoming.restMask,incoming.restIndices[0],incoming.restIndices[1],a.commonUniqueLoaded?1u:0u,z.commonUniqueLoaded?1u:0u,a.commonUniqueSlot,z.commonUniqueSlot,
   static_cast<unsigned long long>(a.owner.uniqueBank),static_cast<unsigned long long>(z.owner.uniqueBank),static_cast<unsigned long long>(a.owner.uniqueFileIdentity),
   receipt.originalResult?1u:0u,receipt.rawEngineStable?1u:0u,receipt.changedOneSlot?1u:0u,receipt.changedSlot,receipt.preexisting?1u:0u,receipt.duplicate?1u:0u,receipt.rawReady?1u:0u,receipt.reason);
  runtime.log(line);
  for(unsigned phase=0;phase<2;++phase){const auto& snapshot=phase?z:a;
   for(unsigned slot=0;slot<snapshot.owner.bankCapacity&&slot<ub::kBankSlotsCeiling;++slot){const auto& bank=snapshot.banks[slot];if(!bank.pointer)continue;
    char row[730]{};StringCchPrintfA(row,ARRAYSIZE(row),
     "unique_bind=raw_constructor_slot;call_serial=%llu;stage=%s;body=unknown;engine=%llx;slot=%u;bank=%llx;file_identity=%llx;typed=%u;loaded=%u;count=%u;rest_mask=%x;rest_indices=%u,%u;rest_sources=%llx,%llx;unique_verified=%u;owner_closure=unavailable;append_authority=0;selected=unverified\r\n",
     static_cast<unsigned long long>(receipt.rawSerial),phase?"after":"before",static_cast<unsigned long long>(snapshot.owner.engine),slot,
     static_cast<unsigned long long>(bank.pointer),static_cast<unsigned long long>(bank.fileIdentity),bank.typed?1u:0u,bank.loaded?1u:0u,bank.count,bank.restMask,
     bank.restIndices[0],bank.restIndices[1],static_cast<unsigned long long>(bank.restSources[0]),static_cast<unsigned long long>(bank.restSources[1]),bank.uniqueVerified?1u:0u);
    runtime.log(row);
   }
  }
  return; // Never populate the actor-scoped last/seen/readiness status.
 }
 const unsigned body=receipt.event.nativeActor;unsigned seen=0;
 if(body<2&&TryAcquireSRWLockExclusive(&runtime.statusLock)){
  runtime.traceLast[body]=receipt;if(runtime.traceSeen[body]<UINT_MAX)++runtime.traceSeen[body];seen=runtime.traceSeen[body];ReleaseSRWLockExclusive(&runtime.statusLock);
 }else if(body>=2){static volatile LONG unmatched=0;seen=static_cast<unsigned>(InterlockedIncrement(&unmatched));}
 if(!seen||seen>128)return;
 const auto& a=receipt.before;const auto& z=receipt.after;const auto& incoming=receipt.incoming;
 const char* nativeScope=bt::IsCommon(receipt.phase)?"bracketed_common":(receipt.phase==bt::Phase::status?"current_readonly":"append_call_only");
 char line[2100]{};StringCchPrintfA(line,ARRAYSIZE(line),
  "unique_bind=natural_bank_trace;body=%u;seen=%u;frame=%llu;phase=%s;ordinal=%u;callsite=%llx;record=%llx;engine_arg=%llx;owner_engine=%llx;P=%llx;P378=%llx;engine_vt=%llx;engine_data=%llx;part=%u;getter_owner=%llx;getter_vt=%llx;getter_target=%llx;getter_count=%u;getter_base=%u;getter_rows=%llx;getter_row=%llx;capacity=%u;empty_before=%u;empty_after=%u;mask_before=%x;mask_after=%x;common_unique_before=%u;common_unique_after=%u;common_unique_slot=%u;bound_unique_after=%u;bound_unique_slot=%u;bank=%llx;file_identity=%llx;incoming_ready=%u;incoming_rest_mask=%x;original_result=%u;owner_stable=%u;slot_changed=%u;changed_slot=%u;preexisting=%u;duplicate=%u;registration_completed=%u;ready=%u;reason=%s;native_scope=%s;append_authority=0;selected=unverified\r\n",
  body,seen,static_cast<unsigned long long>(receipt.frameSerial),PhaseName(receipt.phase),receipt.ordinal,
  static_cast<unsigned long long>(receipt.event.returnAddress?receipt.event.returnAddress-runtime.image:0),static_cast<unsigned long long>(receipt.event.record),
  static_cast<unsigned long long>(receipt.event.engine),static_cast<unsigned long long>(a.owner.engine),static_cast<unsigned long long>(a.owner.player),
  static_cast<unsigned long long>(a.owner.recordArray),static_cast<unsigned long long>(a.owner.engineVtable),static_cast<unsigned long long>(a.owner.engineData),unsigned(a.part),
  static_cast<unsigned long long>(a.owner.getterOwner),static_cast<unsigned long long>(a.owner.getterVtable),static_cast<unsigned long long>(a.owner.getterTarget),a.owner.getterCount,a.owner.getterBase,
  static_cast<unsigned long long>(a.owner.getterRows),static_cast<unsigned long long>(a.owner.getterRow),
  a.owner.bankCapacity,a.empty,z.empty,a.owner.commonMask,z.owner.commonMask,a.commonUniqueLoaded?1u:0u,z.commonUniqueLoaded?1u:0u,
  z.commonUniqueSlot,z.boundUnique?1u:0u,z.boundUniqueSlot,static_cast<unsigned long long>(receipt.event.originalBank),static_cast<unsigned long long>(incoming.fileIdentity),
  receipt.incomingReady?1u:0u,incoming.restMask,receipt.originalResult?1u:0u,receipt.ownerStable?1u:0u,receipt.changedOneSlot?1u:0u,receipt.changedSlot,
  receipt.preexisting?1u:0u,receipt.duplicate?1u:0u,receipt.registrationCompleted?1u:0u,receipt.ready?1u:0u,receipt.reason,nativeScope);
 runtime.log(line);
 // Capture initial bank provenance and full final order, including the
 // preexisting slot. No original file pointer or subscription is modified.
 if(receipt.phase==bt::Phase::commonEnter||receipt.phase==bt::Phase::nativeAfter)TraceBankRows(receipt,a,"before");
 if(receipt.phase==bt::Phase::commonLeave||receipt.phase==bt::Phase::nativeAfter||receipt.phase==bt::Phase::status)TraceBankRows(receipt,z,"after");
}
void Report(void*,const ub::Event& event,const ub::Reply& reply) noexcept {
 if(event.nativeActor>1)return;std::uint32_t actualCreator=0;
 if(!Read(nullptr,event.record+0xc,&actualCreator,sizeof(actualCreator))||actualCreator>1)return;
 const unsigned k=actualCreator;
 if(!TryAcquireSRWLockExclusive(&runtime.statusLock))return;
 runtime.events[k]=event;runtime.last[k]=reply;if(runtime.seen[k]<UINT_MAX)++runtime.seen[k];runtime.reason=reply.reason;
 const unsigned seen=runtime.seen[k];ReleaseSRWLockExclusive(&runtime.statusLock);
 if(!runtime.log||seen>16)return;
 // Diagnostic copies of the actual first-bind maps. These fields never
 // grant readiness, append authority, a pose writer or a finished-model receipt.
 const auto& o=reply.observed;
 std::uintptr_t canonical=0,array=0,table=0,ids=0,pools=0,parts=0,tableRecord=0;
 std::uint8_t part=0xff,pool=0xff;std::uint16_t logical=0xffff;
 const bool maps=Read(nullptr,runtime.image+0x2a82c30,&canonical,sizeof(canonical))&&
  canonical==o.player&&Read(nullptr,canonical+0x378,&array,sizeof(array))&&
  Read(nullptr,canonical+0x50,&table,sizeof(table))&&Read(nullptr,canonical+0x70,&ids,sizeof(ids))&&
  Read(nullptr,canonical+0x78,&pools,sizeof(pools))&&Read(nullptr,canonical+0x90,&parts,sizeof(parts))&&
  Read(nullptr,ids+k*2,&logical,sizeof(logical))&&Read(nullptr,pools+k,&pool,sizeof(pool))&&
  Read(nullptr,parts+k,&part,sizeof(part))&&pool<16&&Read(nullptr,table+pool*8,&tableRecord,sizeof(tableRecord));
 if(maps){char diagnostic[420]{};StringCchPrintfA(diagnostic,ARRAYSIZE(diagnostic),
  "unique_bind=creator_map_diagnostic;body=%u;native_actor=%u;seen=%u;part=%u;pool=%u;logical=%u;table_record=%llx;event_record=%llx;array_record=%llx;append=0;ready=unchanged\r\n",
  k,event.nativeActor,seen,unsigned(part),unsigned(pool),unsigned(logical),static_cast<unsigned long long>(tableRecord),
  static_cast<unsigned long long>(event.record),static_cast<unsigned long long>(array+std::uintptr_t(pool)*0x6a8));runtime.log(diagnostic);}
 unsigned empty=0;for(unsigned i=0;i<o.bankCapacity&&i<ub::kBankSlotsCeiling;++i)if(!o.bound[i])++empty;
 char line[1800]{};
 StringCchPrintfA(line,ARRAYSIZE(line),"unique_bind=first_common_observation;body=%u;native_actor=%u;seen=%u;cold_scene=1;ready=%u;eligible=0;append=0;record=%llx;P=%llx;P378=%llx;id=%u;pool=%u;realized=%u;mask=%x;getter_owner=%llx;getter_vt=%llx;getter_target=%llx;getter_count=%u;getter_base=%u;getter_rows=%llx;getter_row=%llx;getter_engine=%llx;engine=%llx;engine_vt=%llx;engine_data=%llx;bank_array=%llx;capacity=%u;empty=%u;original_bank=%llx;unique_slot=%u;unique_bank=%llx;unique_sources=%llx;unique_blob=%llx;file_identity=%llx;callsite=%llx;reason=%s;effective_selection=unverified\r\n",
  k,event.nativeActor,seen,reply.ready?1u:0u,static_cast<unsigned long long>(event.record),static_cast<unsigned long long>(o.player),static_cast<unsigned long long>(o.recordArray),o.creatorId,o.poolIndex,o.realized,o.commonMask,
  static_cast<unsigned long long>(o.getterOwner),static_cast<unsigned long long>(o.getterVtable),static_cast<unsigned long long>(o.getterTarget),o.getterCount,o.getterBase,static_cast<unsigned long long>(o.getterRows),static_cast<unsigned long long>(o.getterRow),static_cast<unsigned long long>(o.getterEngine),static_cast<unsigned long long>(event.engine),static_cast<unsigned long long>(o.engineVtable),static_cast<unsigned long long>(o.engineData),static_cast<unsigned long long>(o.bankArray),o.bankCapacity,empty,
  static_cast<unsigned long long>(event.originalBank),o.uniqueSlot,static_cast<unsigned long long>(o.uniqueBank),static_cast<unsigned long long>(o.uniqueSources),static_cast<unsigned long long>(o.uniqueBlob),static_cast<unsigned long long>(o.uniqueFileIdentity),static_cast<unsigned long long>(event.returnAddress-runtime.image),reply.reason);
 runtime.log(line);
}
bool Status(char* out,std::size_t n,bool source) noexcept {
 if(!out||!n)return false;
 if(!TryAcquireSRWLockShared(&runtime.statusLock))return SUCCEEDED(StringCchCopyA(out,n,"sleepBind;ready=0;why=status_busy"));
 const bool owned=HooksOwned();int wrote=0;
 if(source)wrote=std::snprintf(out,n,"bindSource;on=%u;seen=%u,%u;slot=%u,%u;getter=%llx,%llx;count2=%u,%u;selected=unverified",
  owned?1u:0u,runtime.seen[0],runtime.seen[1],runtime.last[0].observed.uniqueSlot,runtime.last[1].observed.uniqueSlot,
  static_cast<unsigned long long>(runtime.last[0].observed.getterTarget?runtime.last[0].observed.getterTarget-runtime.image:0),
  static_cast<unsigned long long>(runtime.last[1].observed.getterTarget?runtime.last[1].observed.getterTarget-runtime.image:0),
  runtime.last[0].observed.uniqueBank?1u:0u,runtime.last[1].observed.uniqueBank?1u:0u);
 else wrote=std::snprintf(out,n,"sleepBind;on=%u;seen=%u,%u;ready=%u,%u;capacity=%u,%u;append=0;cold=1;why=%s",owned?1u:0u,
  runtime.seen[0],runtime.seen[1],runtime.last[0].ready?1u:0u,runtime.last[1].ready?1u:0u,
  runtime.last[0].observed.bankCapacity,runtime.last[1].observed.bankCapacity,runtime.reason);
 ReleaseSRWLockShared(&runtime.statusLock);return wrote>=0&&static_cast<std::size_t>(wrote)<n;
}
}
bool Prepare(std::uintptr_t image,std::size_t span,const wchar_t* directory,const Authorization& auth,Log log) noexcept {
 if(Prepared())return runtime.image==image&&CurrentPrivate();
 if(!image||!span||!directory||!log||!auth.exactRetailPe||!auth.offline||!auth.copiedSaveLab||!auth.privateWorkerCurrent||!auth.privateWorkerCurrent()||!MountedMgoUniqueRestContext(directory))return false;
 auto bounded=[](std::uintptr_t at,void* out,std::size_t n)noexcept{return Read(nullptr,at,out,n);};std::uintptr_t mismatch=0;
 if(!ub::CodeGate(bounded,image,span,mismatch)){char line[160]{};StringCchPrintfA(line,ARRAYSIZE(line),"unique_bind=refused;stage=immutable_pre_hook_gate;rva=%llx;append=0\r\n",static_cast<unsigned long long>(mismatch));log(line);return false;}
 if(!bt::CodeGate(bounded,image,span,mismatch)){char line[180]{};StringCchPrintfA(line,ARRAYSIZE(line),"unique_bind=refused;stage=natural_bank_trace_whole_caller_gate;rva=%llx;append=0\r\n",static_cast<unsigned long long>(mismatch));log(line);return false;}
 if(FAILED(StringCchCopyW(runtime.directory,ARRAYSIZE(runtime.directory),directory)))return false;
 runtime.image=image;runtime.span=span;runtime.authorization=auth;runtime.log=log;runtime.exactGate=true;
 const MH_STATUS initialized=MH_Initialize();if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED)return false;
 void* common=reinterpret_cast<void*>(image+0x9bb960);void* append=reinterpret_cast<void*>(image+0x1a6b8f0);
 if(MH_CreateHook(common,reinterpret_cast<void*>(&an::CommonBindHook),reinterpret_cast<void**>(&runtime.adapter.originalCommon))!=MH_OK)return false;
 if(MH_CreateHook(append,reinterpret_cast<void*>(&an::AddMtarHook),reinterpret_cast<void**>(&runtime.adapter.originalAppend))!=MH_OK){MH_RemoveHook(common);return false;}
 runtime.adapter.image=image;runtime.adapter.read=Read;runtime.adapter.hash=Hash;runtime.adapter.scope=Scope;runtime.adapter.report=Report;
 runtime.adapter.traceScope=TraceScope;runtime.adapter.traceReport=TraceReport;
 runtime.adapter.rawConstructorScope=RawConstructorScope;
 runtime.adapter.privateInstallationAdmitted=true;runtime.adapter.observeOnly=true;
 if(!an::Configure(&runtime.adapter)){MH_RemoveHook(append);MH_RemoveHook(common);return false;}
 // No engine calls are made. Enabling the exact native appender first ensures
 // a natural common invocation cannot bypass observation once its hook is on.
 if(MH_EnableHook(append)!=MH_OK){an::Unconfigure();MH_RemoveHook(append);MH_RemoveHook(common);return false;}
 if(MH_EnableHook(common)!=MH_OK){MH_DisableHook(append);an::Unconfigure();MH_RemoveHook(append);MH_RemoveHook(common);return false;}
 if(!Read(nullptr,image+0x9bb960,runtime.commonPatch,sizeof(runtime.commonPatch))||!Read(nullptr,image+0x1a6b8f0,runtime.appendPatch,sizeof(runtime.appendPatch))){
  MH_DisableHook(common);MH_DisableHook(append);an::Unconfigure();MH_RemoveHook(append);MH_RemoveHook(common);return false;
 }
 InterlockedExchange(&runtime.installed,1);
 log("unique_bind=observer_installed;scope=exact_private_mgo_unique_rest_v1;cold_scene_diagnostic=1;natural_callsite=9bb9fa;append=0;cache_changes=0;effective_selection=unverified\r\n");return true;
}
bool Prepared() noexcept {return InterlockedCompareExchange(&runtime.installed,0,0)==1;}
bool SourceStatus(char* out,std::size_t n) noexcept {return Status(out,n,true);}
bool BindingStatus(char* out,std::size_t n) noexcept {return Status(out,n,false);}
bool BankTraceStatus(char* out,std::size_t n,bool fresh) noexcept {
 if(!out||!n)return false;
 bt::Receipt last[2]{};unsigned seen[2]{};
 if(!TryAcquireSRWLockShared(&runtime.statusLock))return SUCCEEDED(StringCchCopyA(out,n,"bankTrace;on=1;ready=0;why=status_busy"));
 for(unsigned k=0;k<2;++k){last[k]=runtime.traceLast[k];seen[k]=runtime.traceSeen[k];}ReleaseSRWLockShared(&runtime.statusLock);
 const bool current=CurrentPrivate();
 if(fresh&&!current)for(auto& receipt:last){receipt={};receipt.reason="trace_current_private_refused";}
 if(fresh&&current){
  auto read=[](std::uintptr_t at,void* bytes,std::size_t size)noexcept{return Read(nullptr,at,bytes,size);};
  auto hash=[](const void* bytes,std::size_t size,char(&digest)[65])noexcept{return Hash(nullptr,bytes,size,digest);};
  std::uintptr_t p=0,records=0,pools=0;
  const bool maps=ub::base::Get(read,runtime.image+0x2a82c30,p)&&ub::base::Aligned(p,8)&&
   ub::base::Get(read,p+0x378,records)&&ub::base::Aligned(records,8)&&ub::base::Get(read,p+0x78,pools)&&pools;
  for(unsigned body=0;body<2;++body){
   bt::Receipt receipt{};receipt.phase=bt::Phase::status;receipt.reason="trace_current_status_scope_refused";receipt.event.nativeActor=body;
   std::uint8_t pool=0xff;ub::Scope scope{};
   if(maps&&ub::base::Get(read,pools+body,pool)&&pool<16){receipt.event.record=records+std::uintptr_t(pool)*0x6a8;
    if(Scope(nullptr,receipt.event,scope)){receipt.event.creatorInvocation=scope.creatorInvocation;
     const char* why="trace_status";receipt.ready=bt::ReadPair(read,hash,scope,receipt.event,receipt.phase,receipt.after,why);receipt.reason=why;
     receipt.event.engine=receipt.after.owner.engine;}
   }
   last[body]=receipt;TraceReport(nullptr,receipt);
  }
 }
 const int wrote=std::snprintf(out,n,"bankTrace;on=%u;fresh=%u;seen=%u,%u;ok=%u,%u;cap=%u,%u;empty=%u,%u;unique=%u:%u,%u:%u;auth=0;sel=unverified",
  Prepared()?1u:0u,fresh?1u:0u,seen[0],seen[1],last[0].ready?1u:0u,last[1].ready?1u:0u,last[0].after.owner.bankCapacity,last[1].after.owner.bankCapacity,
  last[0].after.empty,last[1].after.empty,last[0].after.boundUnique?1u:0u,last[0].after.boundUniqueSlot,last[1].after.boundUnique?1u:0u,last[1].after.boundUniqueSlot);
 return wrote>=0&&static_cast<std::size_t>(wrote)<n;
}
#else
bool Prepare(std::uintptr_t,std::size_t,const wchar_t*,const Authorization&,Log) noexcept {return false;}
bool Prepared() noexcept {return false;}
bool SourceStatus(char* out,std::size_t n) noexcept {return out&&n&&SUCCEEDED(StringCchCopyA(out,n,"bindSource;on=0;why=private_unique_observer_compiled_off"));}
bool BindingStatus(char* out,std::size_t n) noexcept {return out&&n&&SUCCEEDED(StringCchCopyA(out,n,"sleepBind;on=0;append=0;why=private_unique_observer_compiled_off"));}
bool BankTraceStatus(char* out,std::size_t n,bool) noexcept {return out&&n&&SUCCEEDED(StringCchCopyA(out,n,"bankTrace;on=0;append_authority=0;why=private_unique_observer_compiled_off"));}
#endif
}
