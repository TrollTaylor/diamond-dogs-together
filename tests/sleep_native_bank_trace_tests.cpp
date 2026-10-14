// Synthetic detached ownership fixtures. No game, Win32, proprietary payload
// or native calls. Compile with and without MGS_BACKGROUND_WORKER and optional
// MGS_LOCAL_MGO_UNIQUE_REST_BIND to check all admission configurations.
#include "sleep_unique_bind_adapter.hpp"
#include <map>
#include <vector>
#include <iostream>
#include <cstdlib>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
namespace {
namespace ub=mgscoop::sleep_unique_bind;using Addr=std::uintptr_t;
unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Memory {
 std::map<Addr,std::vector<unsigned char>> regions;unsigned reads=0;Addr mutateAt=0;bool mutate=false;
 void Bytes(Addr at,const void* source,std::size_t size){const auto* b=static_cast<const unsigned char*>(source);regions[at].assign(b,b+size);}
 template<class T>void Put(Addr at,T value){Bytes(at,&value,sizeof(value));}
 bool operator()(Addr at,void* out,std::size_t size) noexcept {
  ++reads;auto i=regions.upper_bound(at);if(i==regions.begin())return false;--i;
  if(at<i->first||at-i->first>i->second.size()||size>i->second.size()-(at-i->first))return false;
  std::memcpy(out,i->second.data()+(at-i->first),size);
  if(mutate&&at==mutateAt&&size==sizeof(Addr)){Addr changed=0;std::memcpy(out,&changed,size);mutate=false;}
  return true;
 }
};
struct Fixture {
 static constexpr Addr image=0x140000000,player=0x10000,data=0x11000,state=0x12000,recordArray=0x20000,table=0x28000;
 static constexpr Addr ids=0x29000,pools=0x29100,parts=0x29200,getterOwner=0x30000,getterVT=image+0x22e6070,getterRows=0x32000;
 static constexpr Addr engine=0x40000,engineData=0x41000,banks=0x50000,original=0x60000,unique=0x70000,sources=0x71000,blob=0x80000;
 Memory memory;ub::Scope scope{};ub::Event event{};unsigned hashes=0,appends=0;bool badHash=false,appendRefused=false,changeRowsAfterHash=false;
 explicit Fixture(unsigned body=0){
  const Addr record=recordArray+body*0x6a8;
  scope.exactPe=scope.codeGate=scope.privateDesktop=scope.privateWorker=scope.copiedSaveLab=scope.exactProfile=true;
  scope.creatorActive=scope.creatorOwned=true;scope.image=image;scope.player=player;scope.data=data;scope.state=state;scope.creatorRecord=record;
  scope.mission=scope.ownedMission=30010;scope.generation=scope.ownedGeneration=8;scope.epoch=scope.ownedEpoch=2;
  scope.thread=scope.ownerThread=10;scope.creatorId=scope.expectedNativeActor=body;scope.creatorInvocation=66;scope.now=scope.poll=1000;
  event.record=record;event.engine=engine;event.originalBank=original;event.returnAddress=image+0x9bb9fa;event.nativeActor=body;event.creatorInvocation=66;
  memory.Put(image+0x2a82c30,player);memory.Put(player,image+0x2295490);memory.Put(player+0x60,data);memory.Put(data,image+0x23ad970);memory.Put(data+0x1e8,state);
  memory.Put(player+0x204,std::uint32_t{1});memory.Put(player+0x208,std::uint32_t{2});memory.Put(player+0x20c,std::uint32_t{body+1});
  memory.Put(player+0x378,recordArray);memory.Put(player+0x50,table);memory.Put(player+0x70,ids);memory.Put(player+0x78,pools);memory.Put(player+0x90,parts);
  memory.Put(ids+body*2,std::uint16_t(body));memory.Put(pools+body,std::uint8_t(body));memory.Put(parts+body,std::uint8_t{3});memory.Put(table+body*8,record);
  memory.Put(record,image+0x2295210);memory.Put(record+0x138,player);memory.Put(record+8,std::uint32_t{1});memory.Put(record+0xc,std::uint32_t(body));memory.Put(record+0x680,std::uint32_t{0});
  memory.Put(record+0x10,getterOwner);memory.Put(getterOwner,getterVT);memory.Put(getterVT+0x400,image+0xaf0a00);
  memory.Put(getterOwner+8,std::uint32_t{1});memory.Put(getterOwner+0xc,std::uint32_t(body));memory.Put(getterOwner+0x10,getterRows);memory.Put(getterRows+0x70,engine);
  memory.Put(engine,image+0x24c1988);memory.Put(engine+0x10,engineData);memory.Put(engine+0x1c,std::uint32_t{8});memory.Put(engine+0x98,banks);
  for(unsigned i=0;i<8;++i)memory.Put(banks+i*0x30+0x18,Addr{0});
  for(unsigned i=0;i<16;++i)memory.Put(image+0x2a7f388+Addr(i+16)*0x28,Addr{0});memory.Put(image+0x2a7f388+Addr(17)*0x28,unique);
  memory.Put(unique,image+0x24c2890);memory.Put(unique+0x40,std::uint8_t{2});memory.Put(unique+0x84,std::uint32_t{2});memory.Put(unique+0x88,sources);
  memory.Put(unique+0x98,blob);memory.Put(unique+0x60,blob);memory.Put(unique+0x30,std::uint64_t{0xaabbcc});
  std::vector<unsigned char> fake(ub::kUniqueBytes,0xa5);std::memcpy(fake.data(),ub::kUniqueFileHeader,32);
  for(unsigned i=0;i<2;++i){
   std::memcpy(fake.data()+0x20+i*32,ub::kUniqueEntryHeader[i],32);
   const Addr src=sources+i*0x28;memory.Put(src,i?0xfc53f6ab0bc9caafull:0xfc500f8dffc6c8d6ull);memory.Put(src+8,unique);
   memory.Put(src+0x20,std::uint16_t{3});memory.Put(src+0x22,std::uint16_t(i?420:380));memory.Put(src+0x24,std::uint32_t(i));
  }
  memory.Bytes(blob,fake.data(),fake.size());
 }
 // Fake hasher checks fixture bytes then emits the fixed expected identity.
 // This checks policy only; the234 independent offline asset checks use real
 // SHA256 and original installed asset bytes; runtime must use BCrypt SHA256.
 bool Hash(const void* b,std::size_t n,char (&out)[65]) noexcept {
  ++hashes;if(n!=ub::kUniqueBytes||badHash)return false;
  const auto* p=static_cast<const unsigned char*>(b);
  if(std::memcmp(p,ub::kUniqueFileHeader,32))return false;
  for(unsigned i=0;i<2;++i)if(std::memcmp(p+0x20+i*32,ub::kUniqueEntryHeader[i],32))return false;
  for(std::size_t i=96;i<n;++i)if(p[i]!=0xa5)return false;
  std::memcpy(out,ub::kUniqueSha,65);if(changeRowsAfterHash&&hashes==1){memory.Put(getterOwner+0x10,getterRows+0xc0);memory.Put(getterRows+0xc0+0x70,engine);}return true;
 }
 bool Append(Addr e,Addr b) noexcept {++appends;if(appendRefused||e!=engine||b!=unique)return false;memory.Put(banks+0x18,unique);return true;}
 bool Read(ub::Observation& o){auto hash=[&](const void* p,std::size_t n,char (&s)[65])noexcept{return Hash(p,n,s);};return ub::ReadOnce(memory,hash,scope,event,o);}
 bool ReadReason(ub::Observation& o,const char*& why){auto hash=[&](const void* p,std::size_t n,char (&v)[65])noexcept{return Hash(p,n,v);};return ub::ReadOnce(memory,hash,scope,event,o,&why);}
 bool Observe(ub::Reply& o){auto hash=[&](const void* p,std::size_t n,char (&s)[65])noexcept{return Hash(p,n,s);};return ub::Observe(memory,hash,scope,event,o);}
 bool Prepend(ub::Reply& o,bool changed=false){auto hash=[&](const void* p,std::size_t n,char (&s)[65])noexcept{return Hash(p,n,s);};auto append=[&](Addr e,Addr b)noexcept{return Append(e,b);};auto current=[&]()noexcept{auto now=scope;if(changed)++now.generation;return now;};return ub::TryPrepend(memory,hash,scope,event,append,current,o);}
};
}

namespace {
namespace bt=mgscoop::sleep_native_bank_trace;namespace an=mgscoop::sleep_unique_bind_native;
namespace raw=mgscoop::sleep_raw_constructor_trace;
struct TraceFixture:Fixture {
 unsigned reports=0,traceReports=0,commonCalls=0,originalCalls=0;bool originalRefuses=false,nested=false,insideNested=false;
 DWORD originalLastError=0;
 bool scopeUnavailable=false,rawPrivate=true;
 unsigned rawScopes=0;
 std::vector<bt::Receipt> receipts;
 TraceFixture(unsigned body=0):Fixture(body){
  scope.coldReadOnly=true;memory.Put(parts+body,std::uint8_t{0});
  for(unsigned i=0;i<16;++i)memory.Put(image+0x2a7f388+Addr(i)*0x28,Addr{0});
  memory.Put(original,image+0x24c2890);memory.Put(original+0x30,std::uint64_t{0x123456});
  memory.Put(original+0x40,std::uint8_t{2});memory.Put(original+0x84,std::uint32_t{1285});
  constexpr Addr originalBlob=0xa0000,originalSources=0x62000;
  memory.Put(original+0x88,originalSources);memory.Put(original+0x98,originalBlob);memory.Put(original+0x60,originalBlob);
  std::vector<unsigned char> tableBytes(0x20+1285*0x20);const std::uint32_t magic=0x0c012b72,count=1285;const std::uint16_t flags=0x1000;
  std::memcpy(tableBytes.data(),&magic,4);std::memcpy(tableBytes.data()+4,&count,4);std::memcpy(tableBytes.data()+0x12,&flags,2);
  for(unsigned i=0;i<1285;++i){
   const std::uint64_t id=i<13?std::uint64_t(i+1):(i<1273?bt::restIds[0]+i-13:bt::restIds[1]+i-1273);
   std::memcpy(tableBytes.data()+0x20+i*0x20,&id,8);
  }
  memory.Bytes(originalBlob,tableBytes.data(),tableBytes.size());
  for(unsigned k=0;k<2;++k){const unsigned i=k?1273u:13u;const auto src=originalSources+Addr(i)*0x28;
   memory.Put(src,bt::restIds[k]);memory.Put(src+8,original);memory.Put(src+0x20,std::uint16_t{3});memory.Put(src+0x24,std::uint32_t(i));}
 }
 bool Pair(bt::Snapshot& out,bt::Phase phase=bt::Phase::nativeBefore,const char** refusal=nullptr){
  auto hash=[&](const void* p,std::size_t n,char(&s)[65])noexcept{return Hash(p,n,s);};
  auto call=event;call.returnAddress=image+0x9c3fc1;const char* why=nullptr;
  const bool ok=bt::ReadPair(memory,hash,scope,call,phase,out,why);if(refusal)*refusal=why;return ok;
 }
};
TraceFixture* current=nullptr;
bool Read(void* context,Addr at,void* out,std::size_t n)noexcept{SetLastError(2201);return static_cast<TraceFixture*>(context)->memory(at,out,n);}
bool Hash(void* context,const void* bytes,std::size_t n,char* out)noexcept{char digest[65]{};const bool ok=static_cast<TraceFixture*>(context)->Hash(bytes,n,digest);if(ok)std::memcpy(out,digest,65);return ok;}
bool Scope(void* context,const ub::Event& e,ub::Scope& s)noexcept{
 SetLastError(2202);const auto& f=*static_cast<TraceFixture*>(context);s=f.scope;
 if(f.scopeUnavailable)return false;
 if(e.nativeActor!=UINT32_MAX&&e.nativeActor!=f.scope.creatorId)return false;
 if(e.record&&e.record!=f.event.record)return false;
 if(e.engine&&e.engine!=TraceFixture::engine)return false;
 s.creatorInvocation=e.creatorInvocation?e.creatorInvocation:77;return true;
}
void Report(void* context,const ub::Event&,const ub::Reply&)noexcept{SetLastError(2203);++static_cast<TraceFixture*>(context)->reports;}
void TraceReport(void* context,const bt::Receipt& r)noexcept{SetLastError(2204);auto& f=*static_cast<TraceFixture*>(context);++f.traceReports;f.receipts.push_back(r);}
bool RawScope(void* context,const ub::Event&,raw::Scope& s)noexcept{
 SetLastError(2205);auto& f=*static_cast<TraceFixture*>(context);++f.rawScopes;s={};
 s.image=TraceFixture::image;s.exactPe=s.codeGate=s.copiedSaveLab=s.exactProfile=true;s.privateCurrent=f.rawPrivate;s.thread=10;s.now=1000;
 return true;
}
bool OriginalAppend(Addr engine,Addr bank){
 auto& f=*current;f.originalLastError=GetLastError();++f.originalCalls;SetLastError(8000+f.originalCalls);Check(engine==TraceFixture::engine,"original_engine_argument_preserved");
 if(f.originalRefuses)return false;std::uint32_t capacity=0;Check(ub::base::Get(f.memory,engine+0x1c,capacity),"original_capacity_fixture");
 for(unsigned i=0;i<capacity;++i){Addr ptr=1;Check(ub::base::Get(f.memory,TraceFixture::banks+Addr(i)*0x30+0x18,ptr),"original_slot_fixture");
  if(!ptr){f.memory.Put(TraceFixture::banks+Addr(i)*0x30+0x18,bank);return true;}}
 return false;
}
void OriginalCommon(Addr record,std::uint32_t actor){
 auto& f=*current;++f.commonCalls;Check(record==f.event.record&&actor==f.scope.creatorId,"original_common_arguments_preserved");
 if(f.nested&&!f.insideNested){f.insideNested=true;an::CommonBindHook(record,actor);f.insideNested=false;}
 Check(an::DetachedNaturalAppend(TraceFixture::engine,TraceFixture::original,TraceFixture::image+0x9bb9fa),"normal_bank_original_result");
 Check(an::DetachedNaturalAppend(TraceFixture::engine,TraceFixture::unique,TraceFixture::image+0x9bb9fa),"unique_bank_original_result");
 f.memory.Put(record+0x680,std::uint32_t(1u<<actor));
}
an::Configuration Config(TraceFixture& f){an::Configuration c{};c.context=&f;c.image=TraceFixture::image;c.originalCommon=OriginalCommon;c.originalAppend=OriginalAppend;
 c.read=Read;c.hash=Hash;c.scope=Scope;c.report=Report;c.traceScope=Scope;c.traceReport=TraceReport;c.privateInstallationAdmitted=true;return c;}
}
int main(){
 TraceFixture f;bt::Snapshot sample{};const char* why=nullptr;
 #if !defined(MGS_BACKGROUND_WORKER)
 Check(!f.Pair(sample,bt::Phase::nativeBefore,&why)&&std::strcmp(why,"trace_worker_disabled")==0,"release_trace_compiled_off");
 Check(f.memory.reads==0,"release_refuses_without_memory_reads");auto disabled=Config(f);Check(!an::Configure(&disabled),"release_wrapper_refused");
 std::cout<<"native bank trace release checks="<<checks<<'\n';return 0;
 #else
 const bool initial=f.Pair(sample,bt::Phase::nativeBefore,&why);
 if(!initial)std::cerr<<"Initial refusal: "<<(why?why:"none")<<'\n';
 Check(initial&&sample.empty==8&&sample.commonUniqueLoaded&&sample.commonUniqueSlot==17,"cold_actual_owner_and_late_common_unique");
 Check(sample.owner.getterRows==Fixture::getterRows&&sample.owner.engineVtable==Fixture::image+0x24c1988,"typed_original_cs_row_engine");
 TraceFixture other(1);Check(other.Pair(sample)&&sample.owner.creatorId==1&&sample.owner.getterBase==1,"body1_native_base_boundary");
 TraceFixture preexisting;preexisting.memory.Put(Fixture::banks+0x18,Fixture::original);
 Check(preexisting.Pair(sample)&&sample.empty==7&&sample.firstEmpty==1&&sample.banks[0].restMask==3,"initial_stock_rest_bank_observed_not_refused");
 Check(sample.banks[0].restIndices[0]==13&&sample.banks[0].restIndices[1]==1273,"native_full_id_lookup_matches_header_and_source_indices");
 preexisting.memory.Put(Fixture::banks+0x30+0x18,Fixture::unique);
 Check(preexisting.Pair(sample)&&sample.boundUnique&&sample.boundUniqueSlot==1,"unique_common_slot17_is_engine_slot1");
 Check(bt::FirstAvailableRestSlot(sample,0)==0&&bt::FirstAvailableRestSlot(sample,1)==0,"first_matching_stock_bank_has_precedence");
 TraceFixture reversed;reversed.memory.Put(Fixture::banks+0x18,Fixture::unique);reversed.memory.Put(Fixture::banks+0x30+0x18,Fixture::original);
 Check(reversed.Pair(sample)&&bt::FirstAvailableRestSlot(sample,1)==0&&sample.banks[0].uniqueVerified,"ordering_diagnostic_distinguishes_unique_first");
 TraceFixture absent;absent.memory.Put(Fixture::image+0x2a7f388+17*0x28,Addr{0});
 Check(absent.Pair(sample)&&!sample.commonUniqueLoaded,"unique_unloaded_at_constructor_is_valid_readonly_receipt");
 TraceFixture badPart;badPart.memory.Put(Fixture::parts,std::uint8_t{3});
 Check(!badPart.Pair(sample),"constructor_must_be_native_cold_part0");
 auto later=badPart.event;later.returnAddress=Fixture::image+0x9c91fe;
 auto laterHash=[&](const void* p,std::size_t n,char(&out)[65])noexcept{return badPart.Hash(p,n,out);};
 Check(bt::ReadPair(badPart.memory,laterHash,badPart.scope,later,bt::Phase::nativeBefore,sample,why)&&sample.part==3,"late_receipt_part3_readonly_no_constructor_authority");
 Check(!bt::ReadPair(badPart.memory,laterHash,badPart.scope,later,bt::Phase::commonBefore,sample,why),"common_constructor_phase_never_borrows_gameplay_part3");
 TraceFixture foreign;foreign.scope.privateDesktop=false;Check(!foreign.Pair(sample)&&foreign.memory.reads==0,"visible_desktop_refusal_before_reads");
 TraceFixture forged;forged.scope.coldReadOnly=false;Check(!forged.Pair(sample),"gameplay_scope_not_constructor_authority");
 TraceFixture alias;alias.memory.Put(Fixture::getterRows+0x70,Fixture::engine+8);Check(!alias.Pair(sample),"engine_event_must_match_actual_cs_row");
 TraceFixture old;old.memory.Put(Fixture::engine,Fixture::image+0x2501988);Check(!old.Pair(sample),"old_miscalculated_type_refused");
 TraceFixture drift;drift.changeRowsAfterHash=true;Check(!drift.Pair(sample)&&std::strcmp(why?why:"","trace_worker_disabled")!=0,"typed_row_changed_between_reads_refused");
 TraceFixture header;header.memory.Put(Addr{0xa0004},std::uint32_t{1284});header.memory.Put(Fixture::banks+0x18,Fixture::original);Check(!header.Pair(sample),"loaded_bank_header_count_must_close");
 TraceFixture source;source.memory.Put(Addr{0x62000}+Addr{1273}*0x28+8,Fixture::unique);source.memory.Put(Fixture::banks+0x18,Fixture::original);Check(!source.Pair(sample),"header_hit_requires_actual_source_owner");
 TraceFixture wrongCaller;auto call=wrongCaller.event;call.returnAddress=Fixture::image+0x9c3fc2;
 auto wrongHash=[&](const void* p,std::size_t n,char(&out)[65])noexcept{return wrongCaller.Hash(p,n,out);};
 Check(!bt::ReadPair(wrongCaller.memory,wrongHash,wrongCaller.scope,call,bt::Phase::nativeBefore,sample,why),"neighbor_callsite_refused");
 TraceFixture code;for(const auto& pin:mgscoop::sleep_native_bank_trace_contract::sites)code.memory.Bytes(Fixture::image+pin.rva,pin.bytes,pin.size);
 Addr mismatch=0;Check(bt::CodeGate(code.memory,Fixture::image,0xa01d000,mismatch),"all_complete_natural_caller_and_lookup_functions_pinned");
 code.memory.regions[Fixture::image+0x9c3cd0].back()^=1;
 Check(!bt::CodeGate(code.memory,Fixture::image,0xa01d000,mismatch)&&mismatch==0x9c3cd0,"whole_constructor_end_change_refused");
 bt::Receipt full{};full.event=f.event;Check(f.Pair(full.before),"delta_before_snapshot");
 full.after=full.before;full.originalResult=false;bt::CompleteAdd(full);Check(full.ready&&!full.changedOneSlot,"original_false_preserved_unchanged_slots");
 full.after.owner.bound[0]=Fixture::original;--full.after.empty;full.originalResult=true;bt::CompleteAdd(full);Check(full.ready&&full.changedOneSlot&&full.changedSlot==0,"original_first_empty_slot_delta");
 full.after.owner.bound[1]=Fixture::unique;--full.after.empty;bt::CompleteAdd(full);Check(!full.ready,"multiple_unexplained_slot_changes_refused");
 auto c=Config(f);current=&f;Check(an::Configure(&c),"private_readonly_wrapper_configured");
 SetLastError(7000);
 Check(an::DetachedNaturalAppend(Fixture::engine,Fixture::original,Fixture::image+0x9c3fc1),"constructor_original_append_forwarded");
 Check(f.originalLastError==7000&&GetLastError()==8001,"observer_preserves_original_incoming_and_returned_win32_error");
 Check(f.originalCalls==1&&f.traceReports==1&&f.receipts.back().ready&&f.receipts.back().changedSlot==0,"actual_initial_slot_call_and_result_receipt");
 an::CommonBindHook(f.event.record,0);
 Check(f.originalCalls==3&&f.commonCalls==1&&f.reports==1&&f.traceReports==5,"every_common_add_observed_and_original_called_exactly_once");
 Check(GetLastError()==8003,"common_after_observer_preserves_last_original_error");
 Check(f.receipts[1].phase==bt::Phase::commonEnter&&f.receipts[1].before.empty==7,"common_entry_preserves_preexisting_stock_bank");
 Check(f.receipts[2].ordinal==1&&f.receipts[3].ordinal==2&&f.receipts[2].duplicate,"natural_order_and_native_duplicate_behavior_preserved");
 Check(f.receipts[4].phase==bt::Phase::commonLeave&&f.receipts[4].registrationCompleted&&f.receipts[4].after.boundUniqueSlot==2,"original_common_mask_and_full_final_bank_order");
 f.originalRefuses=true;
 Check(!an::DetachedNaturalAppend(Fixture::engine,Fixture::unique,Fixture::image+0x9c91fe),"original_false_forwarded_under_observer");
 Check(f.receipts.back().ready&&!f.receipts.back().originalResult&&f.originalCalls==4,"failed_append_receipt_preserves_unchanged_bank_list");
 Check(GetLastError()==8004,"failed_original_result_and_error_survive_after_diagnostic");
 const auto reportsBefore=f.traceReports;
 Check(!an::DetachedNaturalAppend(Fixture::engine,Fixture::original,Fixture::image+0x9c3fc2)&&f.traceReports==reportsBefore&&f.originalCalls==5,"foreign_return_only_forwarded_never_observed");
 an::Unconfigure();Check(!an::DetachedNaturalAppend(Fixture::engine,Fixture::original,Fixture::image+0x9c3fc1)&&f.originalCalls==6&&f.traceReports==reportsBefore,"unconfigured_original_fallback_preserved");
 TraceFixture nesting;nesting.nested=true;auto nestedConfig=Config(nesting);current=&nesting;Check(an::Configure(&nestedConfig),"nested_wrapper_configured");
 an::CommonBindHook(nesting.event.record,0);Check(nesting.originalCalls==4&&nesting.commonCalls==2,"nested_original_call_counts_preserved");
 Check(nesting.receipts.size()==8&&nesting.receipts[0].frameSerial!=nesting.receipts[1].frameSerial&&nesting.receipts[5].frameSerial==nesting.receipts[0].frameSerial&&nesting.receipts[5].ordinal==1,"tls_nested_frames_restore_outer_ordinal_and_serial");
 an::Unconfigure();auto half=Config(nesting);half.traceReport=nullptr;Check(!an::Configure(&half),"partial_diagnostic_callback_config_refused");
 auto writer=Config(nesting);writer.observeOnly=false;Check(!an::Configure(&writer),"diagnostic_cannot_enable_append_mode");
 // Separate opaque constructor receipt: absolutely no actor/creator map.
 TraceFixture opaque;opaque.scopeUnavailable=true;auto opaqueConfig=Config(opaque);opaqueConfig.rawConstructorScope=RawScope;current=&opaque;
 Check(an::Configure(&opaqueConfig),"exact_readonly_raw_callback_configured");SetLastError(7100);
 Check(an::DetachedNaturalAppend(Fixture::engine,Fixture::original,Fixture::image+0x9c3fc1),"unknown_actor_original_stock_append_forwarded");
 Check(opaque.receipts.size()==1&&opaque.receipts[0].rawConstructor&&opaque.receipts[0].rawReady&&opaque.receipts[0].rawEngineStable,"unknown_actor_typed_engine_receipt");
 const auto& r=opaque.receipts[0];Check(r.event.nativeActor==UINT32_MAX&&!r.event.record&&!r.event.creatorInvocation&&!r.ready&&!r.ownerStable&&!r.registrationCompleted,"raw_never_manufactures_actor_scope_or_ready");
 Check(r.before.owner.engine==Fixture::engine&&!r.before.owner.player&&!r.before.owner.getterOwner&&!r.before.owner.recordArray,"raw_native_fields_do_not_populate_canonical_owner");
 Check(r.before.empty==8&&r.after.empty==7&&r.changedOneSlot&&r.changedSlot==0&&r.incoming.restMask==3&&r.after.banks[0].restIndices[1]==1273,"raw_empty_before_and_stock_rest_indices_after");
 Check(r.before.commonUniqueLoaded&&r.before.commonUniqueSlot==17&&r.before.owner.uniqueFileIdentity==0xaabbcc,"raw_unique_loaded_timing_before_original_stock");
 Check(opaque.rawScopes==2&&opaque.originalCalls==1&&opaque.originalLastError==7100&&GetLastError()==8001,"raw_keeps_original_count_and_win32_errors");
 opaque.originalRefuses=true;Check(!an::DetachedNaturalAppend(Fixture::engine,Fixture::original,Fixture::image+0x9c3fc1)&&opaque.receipts.back().rawReady&&!opaque.receipts.back().originalResult,"raw_original_false_unchanged_preserved");
 const auto rawBefore=opaque.rawScopes;
 Check(!an::DetachedNaturalAppend(Fixture::engine,Fixture::original,Fixture::image+0x9c91fe)&&opaque.rawScopes==rawBefore&&!opaque.receipts.back().rawConstructor,"raw_never_falls_back_at_late_or_neighbor_callers");
 an::Unconfigure();
 raw::Scope rawScope{};RawScope(&opaque,opaque.event,rawScope);
 auto rawEvent=opaque.event;rawEvent.record=0;rawEvent.nativeActor=UINT32_MAX;rawEvent.creatorInvocation=0;rawEvent.returnAddress=Fixture::image+0x9c3fc1;
 auto rawHash=[&](const void* p,std::size_t n,char(&out)[65])noexcept{return opaque.Hash(p,n,out);};bt::Bank incoming{};
 opaque.memory.regions.erase(Fixture::image+0x2a82c30);
 for(auto p=opaque.memory.regions.begin();p!=opaque.memory.regions.end();)if(p->first>=Fixture::player&&p->first<Fixture::engine)p=opaque.memory.regions.erase(p);else ++p;
 Check(raw::ReadPair(opaque.memory,rawHash,rawScope,rawEvent,sample,incoming,why),"raw_pair_has_no_actor_map_dependency");
 auto actorForged=rawEvent;actorForged.nativeActor=0;Check(!raw::ReadPair(opaque.memory,rawHash,rawScope,actorForged,sample,incoming,why),"raw_forged_known_actor_refused");
 auto recordForged=rawEvent;recordForged.record=opaque.event.record;Check(!raw::ReadPair(opaque.memory,rawHash,rawScope,recordForged,sample,incoming,why),"raw_forged_record_refused");
 auto serialForged=rawEvent;serialForged.creatorInvocation=88;Check(!raw::ReadPair(opaque.memory,rawHash,rawScope,serialForged,sample,incoming,why),"raw_forged_creator_invocation_refused");
 auto neighbor=rawEvent;++neighbor.returnAddress;Check(!raw::ReadPair(opaque.memory,rawHash,rawScope,neighbor,sample,incoming,why),"raw_exact_natural_constructor_callsite_only");
 auto normal=rawScope;normal.exactProfile=false;Check(!raw::ReadPair(opaque.memory,rawHash,normal,rawEvent,sample,incoming,why),"raw_normal_profile_refused");
 auto visible=rawScope;visible.privateCurrent=false;const unsigned readsBeforePrivateRefusal=opaque.memory.reads;
 Check(!raw::ReadPair(opaque.memory,rawHash,visible,rawEvent,sample,incoming,why)&&opaque.memory.reads==readsBeforePrivateRefusal,"raw_visible_or_expired_worker_refused_without_memory_reads");
 opaque.memory.Put(Fixture::engine,Fixture::image+0x2501988);Check(!raw::ReadPair(opaque.memory,rawHash,rawScope,rawEvent,sample,incoming,why)&&std::strcmp(why,"raw_constructor_engine_vtable")==0,"raw_exact_engine_final_type_retained");
 opaque.memory.Put(Fixture::engine,Fixture::image+0x24c1988);
 TraceFixture unloaded;unloaded.memory.Put(Fixture::image+0x2a7f388+17*0x28,Addr{0});
 auto unloadedHash=[&](const void* p,std::size_t n,char(&out)[65])noexcept{return unloaded.Hash(p,n,out);};
 Check(raw::ReadPair(unloaded.memory,unloadedHash,rawScope,rawEvent,sample,incoming,why)&&!sample.commonUniqueLoaded&&sample.empty==8,"raw_unique_unavailable_before_stock_is_reported_honestly");
 auto orphan=Config(unloaded);orphan.traceScope=nullptr;orphan.traceReport=nullptr;orphan.rawConstructorScope=RawScope;
 Check(!an::Configure(&orphan),"raw_cannot_bypass_configured_private_readonly_trace");
 std::cout<<"native bank trace worker checks="<<checks<<'\n';return 0;
 #endif
}
