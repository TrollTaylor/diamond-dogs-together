// Synthetic detached ownership fixtures. No game, Win32, proprietary payload
// or native calls. Compile with and without MGS_BACKGROUND_WORKER and optional
// MGS_LOCAL_MGO_UNIQUE_REST_BIND to check all admission configurations.
#include "sleep_unique_bind.hpp"
#include <map>
#include <vector>
#include <iostream>
#include <cstdlib>
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
int main(){
 ub::Observation observed{};ub::Reply reply{};
 Fixture f;Check(f.Read(observed)&&observed.uniqueBank==Fixture::unique&&observed.uniqueSlot==17&&observed.recordArray==Fixture::recordArray,"exact_creator0_first_binding");
 Fixture other(1);Check(other.Read(observed)&&observed.creatorId==1&&observed.recordBase==1,"exact_creator1_first_binding");
 Fixture indexed(1);indexed.event.nativeActor=0;indexed.scope.expectedNativeActor=0;
 Check(!indexed.Read(observed)&&observed.getterRow==0,"native_actor_argument_preserved_but_getter_underflow_refused");
 Fixture wrongGetter;wrongGetter.memory.Put(Fixture::getterOwner,Fixture::image+0x22e6078);Check(!wrongGetter.Read(observed),"foreign_getter_vtable_refused");
 Fixture wrongSlot;wrongSlot.memory.Put(Fixture::getterVT+0x400,Fixture::image+0xaf0a20);Check(!wrongSlot.Read(observed),"neighbor_getter_leaf_refused");
 Fixture emptyRows;emptyRows.memory.Put(Fixture::getterOwner+8,std::uint32_t{0});Check(!emptyRows.Read(observed),"getter_no_actor_rows");
 Fixture wideRows;wideRows.memory.Put(Fixture::getterOwner+8,std::uint32_t{2});Check(!wideRows.Read(observed),"getter_count_must_match_single_creator_record");
 Fixture shiftedBase;shiftedBase.memory.Put(Fixture::getterOwner+0xc,std::uint32_t{1});Check(!shiftedBase.Read(observed),"getter_base_must_match_current_creator");
 Fixture upper;upper.event.nativeActor=upper.scope.expectedNativeActor=1;Check(!upper.Read(observed)&&observed.getterRow==0,"native_argument_above_actual_getter_rows_refused");
 Fixture missingRows;missingRows.memory.Put(Fixture::getterOwner+0x10,Addr{0});Check(!missingRows.Read(observed),"null_getter_array_refused");
 Fixture interiorRows;interiorRows.memory.Put(Fixture::getterOwner+0x10,Fixture::getterRows+8);Check(!interiorRows.Read(observed),"native16byte_getter_array_alignment");
 Fixture rowAlias;rowAlias.memory.Put(Fixture::getterRows+0x70,Fixture::engine+8);Check(!rowAlias.Read(observed),"original_returned_engine_must_equal_exact_row70");
 Fixture oldVtable;oldVtable.memory.Put(Fixture::engine,Fixture::image+0x2501988);Check(!oldVtable.Read(observed),"old_miscalculated_engine_vtable_refused");
 Fixture absentData;absentData.memory.Put(Fixture::engine+0x10,Addr{0});Check(!absentData.Read(observed),"real_engine_data_link_retained");
 Fixture zeroCapacity;zeroCapacity.memory.Put(Fixture::engine+0x1c,std::uint32_t{0});Check(!zeroCapacity.Read(observed),"zero_native_bank_capacity_refused");
 Fixture nullBanks;nullBanks.memory.Put(Fixture::engine+0x98,Addr{0});Check(!nullBanks.Read(observed),"null_native_bank_array_refused");
 Fixture tuple;Check(tuple.Read(observed)&&observed.getterRows==Fixture::getterRows&&observed.getterRow==Fixture::getterRows&&observed.getterEngine==Fixture::engine&&observed.engineVtable==Fixture::image+0x24c1988,"complete_original_getter_native_engine_tuple");
 Fixture tuple1(1);Check(tuple1.Read(observed)&&observed.getterBase==1&&observed.getterCount==1&&observed.getterRow==Fixture::getterRows,"body1_current_count_base_boundary");
 Fixture gate;for(const auto& site:mgscoop::sleep_unique_bind_contract::sites)gate.memory.Bytes(Fixture::image+site.rva,site.bytes,site.size);
 Addr mismatch=0;Check(ub::CodeGate(gate.memory,Fixture::image,0xa01d000,mismatch),"actual_getter_leaf_added_to_full_original_code_gate");
 gate.memory.regions[Fixture::image+0xaf0a00][20]^=1;Check(!ub::CodeGate(gate.memory,Fixture::image,0xa01d000,mismatch)&&mismatch==0xaf0a00,"full_leaf_return_changed_refused");
 gate.memory.regions[Fixture::image+0xaf0a00][20]^=1;gate.memory.Put(Fixture::getterVT+0x400,Fixture::image+0xaf0a20);Check(!ub::CodeGate(gate.memory,Fixture::image,0xa01d000,mismatch)&&mismatch==0x22e6470,"actual_getter_vtable_slot_pin_changed_refused");
 const char* why=nullptr;
 Check(!oldVtable.ReadReason(observed,why)&&std::strcmp(why,"first_bind_native_engine_vtable")==0,"diagnostic_exact_native_vtable_field");
 Check(!absentData.ReadReason(observed,why)&&std::strcmp(why,"first_bind_native_engine_data")==0,"diagnostic_exact_native_data_field");
 Check(!zeroCapacity.ReadReason(observed,why)&&std::strcmp(why,"first_bind_native_bank_capacity")==0,"diagnostic_exact_native_capacity_field");
 Check(!nullBanks.ReadReason(observed,why)&&std::strcmp(why,"first_bind_native_bank_array")==0,"diagnostic_exact_native_bank_array_field");
 #if defined(MGS_BACKGROUND_WORKER)
 Fixture changedRows;changedRows.changeRowsAfterHash=true;Check(!changedRows.Observe(reply)&&std::strcmp(reply.reason,"first_bind_changed")==0,"actual_controller_rows_changed_between_coherent_receipts");
 #endif
 Fixture privateOff;privateOff.scope.privateDesktop=false;Check(!privateOff.Read(observed)&&!privateOff.memory.reads,"visible_desktop_refused_before_reads");
 Fixture profileOff;profileOff.scope.exactProfile=false;Check(!profileOff.Read(observed),"normal_profile_refused");
 Fixture copiedOff;copiedOff.scope.copiedSaveLab=false;Check(!copiedOff.Read(observed),"real_saves_refused");
 Fixture stale;stale.scope.now+=251;Check(!stale.Read(observed),"stale_creator_scope");
 Fixture thread;thread.scope.thread++;Check(!thread.Read(observed),"creator_thread_mismatch");
 Fixture retired;retired.scope.creatorActive=false;Check(!retired.Read(observed),"retired_creator_invocation");
 Fixture caller;caller.event.returnAddress++;Check(!caller.Read(observed),"foreign_append_callsite");
 Fixture serial;serial.event.creatorInvocation++;Check(!serial.Read(observed),"foreign_creator_serial");
 Fixture actor;actor.event.nativeActor=1;Check(!actor.Read(observed),"foreign_native_actor_argument");
 Fixture base;base.memory.Put(base.event.record+0xc,std::uint32_t{1});Check(!base.Read(observed),"foreign_record_id");
 Fixture owner;owner.memory.Put(owner.event.record+0x138,Fixture::player+8);Check(!owner.Read(observed),"foreign_record_owner");
 Fixture array;array.memory.Put(Fixture::player+0x378,Fixture::recordArray+0x6a8);Check(!array.Read(observed),"wrong_current_player_record_array");
 Fixture oldOffset;oldOffset.memory.Put(Fixture::player+0x378,Addr{0});oldOffset.memory.Put(Fixture::player+0x398,Fixture::recordArray);Check(!oldOffset.Read(observed),"other_container_398_never_substituted");
 Fixture active;active.memory.Put(active.event.record+0x680,std::uint32_t{1});Check(!active.Read(observed),"original_registration_already_done");
 Fixture slots;slots.memory.Put(Fixture::engine+0x1c,std::uint32_t{65});Check(!slots.Read(observed),"bank_capacity_ceiling");
 Fixture nonempty;nonempty.memory.Put(Fixture::banks+0x18,Fixture::original);Check(!nonempty.Read(observed)&&nonempty.hashes>0,"no_live_bank_remove_or_reorder");
 Fixture lateSlot;lateSlot.memory.Put(Fixture::banks+7*0x30+0x18,Fixture::original);Check(!lateSlot.Read(observed),"all_slots_empty_required");
 Fixture engineType;engineType.memory.Put(Fixture::engine,Fixture::image+0x250ba50);Check(!engineType.Read(observed),"foreign_motion_controller_vtable");
 Fixture fileType;fileType.memory.Put(Fixture::unique,Fixture::image+0x24c28b8);Check(!fileType.Read(observed),"interior_file_vtable_slot_refused");
 Fixture sourceAlias;sourceAlias.memory.Put(Fixture::sources+8,Fixture::unique+8);Check(!sourceAlias.Read(observed),"source_bank_owner");
 Fixture sourceId;sourceId.memory.Put(Fixture::sources,std::uint64_t{1});Check(!sourceId.Read(observed),"retained_tpp_motion_id_required");
 Fixture sourceIndex;sourceIndex.memory.Put(Fixture::sources+0x24,std::uint32_t{1});Check(!sourceIndex.Read(observed),"exact_two_entry_index");
 Fixture sourceFrames;sourceFrames.memory.Put(Fixture::sources+0x22,std::uint16_t{381});Check(!sourceFrames.Read(observed),"native_frames_required");
 Fixture sourceKind;sourceKind.memory.Put(Fixture::sources+0x20,std::uint16_t{2});Check(!sourceKind.Read(observed),"compact_mtar_source_required");
 Fixture fileCount;fileCount.memory.Put(Fixture::unique+0x84,std::uint32_t{1285});Check(!fileCount.Read(observed),"full_resident_not_unique_bank");
 Fixture notLoaded;notLoaded.memory.Put(Fixture::unique+0x60,Fixture::blob+16);Check(!notLoaded.Read(observed),"effective_loaded_blob_identity");
 Fixture corrupt;corrupt.memory.regions[Fixture::blob][1008]^=1;Check(!corrupt.Read(observed),"whole_bank_hash_admission");
 Fixture header;header.memory.regions[Fixture::blob][0x20+12]^=1;Check(!header.Read(observed),"exact_relocated_entry_header");
 Fixture sameNormal;sameNormal.event.originalBank=Fixture::unique;Check(!sameNormal.Read(observed),"unique_bank_already_original_call_argument");
 Fixture missing;missing.memory.Put(Fixture::image+0x2a7f388+Addr(17)*0x28,Addr{0});Check(!missing.Read(observed),"asset_not_registered_no_guess_loader");
 Fixture cold;cold.scope.coldReadOnly=true;cold.memory.Put(Fixture::parts,std::uint8_t{0});cold.scope.mission=cold.scope.ownedMission=0;cold.scope.epoch=cold.scope.ownedEpoch=0;cold.scope.generation=cold.scope.ownedGeneration=0;
 Check(cold.Read(observed),"natural_cold_first_bind_has_no_fabricated_gameplay_epoch");
 Fixture coldOther(1);coldOther.scope.coldReadOnly=true;coldOther.memory.Put(Fixture::parts+1,std::uint8_t{0});
 Check(coldOther.Read(observed)&&observed.creatorId==1,"actual_first_bind_creator1_part0");
 Fixture finishedCold;finishedCold.scope.coldReadOnly=true;
 Check(!finishedCold.Read(observed),"completed_part3_does_not_claim_first_constructor_phase");
 Fixture unknownCold;unknownCold.scope.coldReadOnly=true;unknownCold.memory.Put(Fixture::parts,std::uint8_t{2});
 Check(!unknownCold.Read(observed),"unknown_constructor_phase_refused");
 Fixture prematureGame;prematureGame.memory.Put(Fixture::parts,std::uint8_t{0});
 Check(!prematureGame.Read(observed),"cold_part0_does_not_grant_gameplay_or_append_scope");
 Fixture strict;strict.scope.epoch=0;Check(!strict.Read(observed),"strict_append_scene_not_relaxed");
 Fixture coldQualified;coldQualified.scope.coldReadOnly=true;coldQualified.memory.Put(Fixture::parts,std::uint8_t{0});coldQualified.scope.engineGetterQualified=coldQualified.scope.nativeTimingQualified=coldQualified.scope.compactClosureQualified=true;
 #if defined(MGS_BACKGROUND_WORKER)
 Check(coldQualified.Observe(reply)&&!reply.eligible&&!coldQualified.Prepend(reply)&&!coldQualified.appends,"cold_observation_never_grants_native_append_authority");
 #endif
 Fixture ro;
 #if defined(MGS_BACKGROUND_WORKER)
 Check(ro.Observe(reply)&&reply.ready&&!reply.eligible&&!reply.appended,"worker_readonly_observation");
 Check(!ro.Prepend(reply)&&!ro.appends,"qualification_default_off");
 Fixture qualified;qualified.scope.engineGetterQualified=qualified.scope.nativeTimingQualified=qualified.scope.compactClosureQualified=true;
 #if defined(MGS_LOCAL_MGO_UNIQUE_REST_BIND)
 Check(qualified.Prepend(reply)&&reply.appended&&qualified.appends==1,"private_qualified_original_append_only");
 Check(!qualified.Prepend(reply)&&qualified.appends==1,"repeated_append_refused_without_removal");
 Fixture changed;changed.scope.engineGetterQualified=changed.scope.nativeTimingQualified=changed.scope.compactClosureQualified=true;
 Check(!changed.Prepend(reply,true)&&!changed.appends,"fresh_creator_scope_before_original_append");
 Fixture rejected;rejected.scope.engineGetterQualified=rejected.scope.nativeTimingQualified=rejected.scope.compactClosureQualified=true;rejected.appendRefused=true;
 Check(!rejected.Prepend(reply)&&rejected.appends==1&&!reply.appended,"original_append_refusal_receipt");
 #else
 Check(!qualified.Prepend(reply)&&!qualified.appends&&std::strcmp(reply.reason,"first_bind_append_compiled_off")==0,"worker_append_compiled_off_even_if_qualified");
 #endif
 #else
 Check(!ro.Observe(reply)&&!ro.memory.reads&&std::strcmp(reply.reason,"first_bind_worker_disabled")==0,"release_observer_has_no_reads");
 Check(!ro.Prepend(reply)&&!ro.appends,"release_append_has_no_calls");
 #endif
 std::cout<<"sleep_unique_bind_tests: "<<checks<<" synthetic policy checks passed; no game operations\n";
}
