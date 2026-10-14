#pragma once
// Private first-common-bind observer. Default behavior is read-only. The root
// hook must bracket natural 9BB960 and preserve its original calls unchanged.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "sleep_plugin_read.hpp"
#include "sleep_unique_bind_contract.hpp"
namespace mgscoop::sleep_unique_bind {
namespace base=sleep_plugin_read;
inline constexpr std::size_t kUniqueBytes=12672,kBankSlotsCeiling=64;
inline constexpr const char* kUniqueSha="f2a4b0b175765c4d2cf9c54b24abf3cd68cf394c8f6aa8e547a5e9cd26e2a672";
inline constexpr unsigned char kUniqueFileHeader[32]={0x72,0x2b,0x01,0x0c,0x02,0,0,0,0x12,0,0x38,0,0,0,0,0,0x08,0,0,0x10,0x60,0,0,0,0,0,0,0,0,0,0,0};
inline constexpr unsigned char kUniqueEntryHeader[2][32]={
 {0xd6,0xc8,0xc6,0xff,0x8d,0x0f,0x50,0xfc,0xf0,0x03,0,0,0x75,0x01,0,0,0,0,0,0,0,0,0,0,0x40,0x1b,0,0,0,0,0,0},
 {0xaf,0xca,0xc9,0x0b,0xab,0xf6,0x53,0xfc,0x80,0x1b,0,0,0x5c,0x01,0,0,0,0,0,0,0,0,0,0,0x40,0x31,0,0,0,0,0,0}};
struct Scope {
 bool exactPe=false,codeGate=false,privateDesktop=false,privateWorker=false,copiedSaveLab=false,exactProfile=false;
 bool creatorActive=false,creatorOwned=false,stopping=false,faulted=false;
 bool coldReadOnly=false; // Natural common-bind observation only; never append authority.
 // Kept false until root has observed and qualified the ORIGINAL getter,
 // creator timing and compact type2 source closure. No inferred permission.
 bool engineGetterQualified=false,nativeTimingQualified=false,compactClosureQualified=false;
 std::uintptr_t image=0,player=0,data=0,state=0,creatorRecord=0;
 std::uint32_t mission=0,ownedMission=0,generation=0,ownedGeneration=0,thread=0,ownerThread=0,creatorId=0,expectedNativeActor=0;
 std::uint16_t epoch=0,ownedEpoch=0;
 std::uint64_t creatorInvocation=0,now=0,poll=0;
};
struct Event {
 std::uintptr_t record=0,engine=0,originalBank=0,returnAddress=0;
 std::uint32_t nativeActor=0;
 std::uint64_t creatorInvocation=0;
};
struct Observation {
 std::uintptr_t player=0,recordArray=0,blockTable=0,record=0,getterOwner=0,getterVtable=0,getterTarget=0;
 std::uintptr_t getterRows=0,getterRow=0,getterEngine=0,engineVtable=0;
 std::uint32_t getterBase=0,getterCount=0;
 std::uintptr_t engine=0,engineData=0,bankArray=0,originalBank=0,uniqueBank=0,uniqueBlob=0,uniqueSources=0;
 std::uint64_t uniqueFileIdentity=0;
 std::uintptr_t common[16]{},bound[kBankSlotsCeiling]{};
 std::uint32_t creatorId=0,nativeActor=0,recordBase=0,recordCount=0,commonMask=0,bankCapacity=0,realized=0,uniqueSlot=0;
 std::uint8_t poolIndex=0;
};
struct Reply {bool ready=false,eligible=false,appended=false;Observation observed{};const char* reason="disabled";};
inline const char* ScopeRefusal(const Scope& s) noexcept {
 if(!s.exactPe||!s.codeGate)return "first_bind_code_gate";
 if(!s.privateDesktop||!s.privateWorker||!s.copiedSaveLab||!s.exactProfile)return "first_bind_private_profile";
 if(!s.creatorActive||!s.creatorOwned||!s.creatorInvocation||s.stopping||s.faulted)return "first_bind_creator_scope";
 if(!s.thread||s.thread!=s.ownerThread)return "first_bind_creator_thread";
 if(!s.coldReadOnly&&(!s.generation||!s.epoch||s.generation!=s.ownedGeneration||s.epoch!=s.ownedEpoch||s.mission!=s.ownedMission||
    (s.mission!=30010&&s.mission!=30020&&s.mission!=30050)))return "first_bind_scene";
 if(!s.poll||s.now<s.poll||s.now-s.poll>250)return "first_bind_scope_expired";
 if(!base::Range(s.image,0xa01d000)||!base::Aligned(s.player,8)||!base::Aligned(s.data,8)||!base::Aligned(s.state,8)||
    !base::Aligned(s.creatorRecord,8)||s.creatorId>1||s.expectedNativeActor>1)return "first_bind_identity";
 return nullptr;
}
inline bool SameScope(const Scope& a,const Scope& b) noexcept {
 return !ScopeRefusal(a)&&!ScopeRefusal(b)&&a.image==b.image&&a.player==b.player&&a.data==b.data&&a.state==b.state&&
 a.creatorRecord==b.creatorRecord&&a.creatorId==b.creatorId&&a.expectedNativeActor==b.expectedNativeActor&&
 a.mission==b.mission&&a.epoch==b.epoch&&a.generation==b.generation&&a.thread==b.thread&&a.creatorInvocation==b.creatorInvocation&&
 a.coldReadOnly==b.coldReadOnly&&a.engineGetterQualified==b.engineGetterQualified&&a.nativeTimingQualified==b.nativeTimingQualified&&a.compactClosureQualified==b.compactClosureQualified;
}
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!base::Range(image,span))return false;unsigned char bytes[2048]{};
 for(const auto& f:sleep_unique_bind_contract::sites){
  if(f.rva>span||f.size>span-f.rva||f.size>sizeof(bytes)||!base::Range(image+f.rva,f.size)||!r(image+f.rva,bytes,f.size)||std::memcmp(bytes,f.bytes,f.size)){mismatch=f.rva;return false;}
 }
 std::uintptr_t getter=0;
 if(!base::Get(r,image+0x22e6070+0x400,getter)||getter!=image+0xaf0a00){mismatch=0x22e6470;return false;}
 return true;
}
// Whole bank hash authenticates the copied master/schema, both compact mains,
// both native rest end chunks and exact sorted retained TPP IDs as one asset.
template<class Reader,class Hasher> bool UniqueBank(Reader& r,Hasher& hash,std::uintptr_t image,std::uintptr_t bank,std::uintptr_t& blob,std::uintptr_t& sources,std::uint64_t& identity) noexcept {
 std::uintptr_t vt=0,loaded=0;std::uint32_t count=0;std::uint8_t kind=0;
 if(!base::Aligned(bank,8)||!base::Get(r,bank,vt)||vt!=image+0x24c2890||!base::Get(r,bank+0x40,kind)||kind!=2||
    !base::Get(r,bank+0x84,count)||count!=2||!base::Get(r,bank+0x88,sources)||!base::Aligned(sources,8)||
    !base::Get(r,bank+0x98,blob)||!base::Aligned(blob,16)||!base::Get(r,bank+0x60,loaded)||loaded!=blob||
    !base::Get(r,bank+0x30,identity)||!base::Range(blob,kUniqueBytes))return false;
 unsigned char bytes[kUniqueBytes]{};char digest[65]{};
 if(!r(blob,bytes,sizeof(bytes))||std::memcmp(bytes,kUniqueFileHeader,32)||!hash(bytes,sizeof(bytes),digest)||digest[64]||std::strcmp(digest,kUniqueSha))return false;
 for(unsigned i=0;i<2;++i){
  if(std::memcmp(bytes+0x20+i*0x20,kUniqueEntryHeader[i],32))return false;
  const auto source=sources+i*0x28;std::uintptr_t owner=0;std::uint64_t id=0;std::uint32_t index=0;std::uint16_t sourceKind=0,frames=0;
  if(!base::Get(r,source,id)||id!=(i?0xfc53f6ab0bc9caafull:0xfc500f8dffc6c8d6ull)||!base::Get(r,source+8,owner)||owner!=bank||
     !base::Get(r,source+0x20,sourceKind)||(sourceKind&15u)!=3||!base::Get(r,source+0x22,frames)||frames!=(i?420:380)||
     !base::Get(r,source+0x24,index)||index!=i)return false;
 }return true;
}
template<class Reader,class Hasher> bool ReadOnce(Reader& r,Hasher& hash,const Scope& s,const Event& e,Observation& o,const char** refusal=nullptr) noexcept {
 o={};const auto why=[&](const char* value) noexcept {if(refusal)*refusal=value;};
 if(const auto* rejected=ScopeRefusal(s)){why(rejected);return false;}
 why("first_bind_natural_call");
 if(e.returnAddress!=s.image+0x9bb9fa||e.record!=s.creatorRecord||e.creatorInvocation!=s.creatorInvocation||
    e.nativeActor!=s.expectedNativeActor||!base::Aligned(e.engine,8)||!base::Aligned(e.originalBank,8))return false;
 o.player=s.player;o.record=e.record;o.engine=e.engine;o.originalBank=e.originalBank;o.creatorId=s.creatorId;o.nativeActor=e.nativeActor;
 std::uintptr_t got=0,vt=0,ids=0,pools=0,parts=0;std::uint32_t allocated=0,flags=0;std::uint16_t id=0;std::uint8_t part=0;
 why("first_bind_player_owner");
 if(!base::Get(r,s.image+0x2a82c30,got)||got!=s.player||!base::Get(r,s.player,vt)||vt!=s.image+0x2295490||
    !base::Get(r,s.player+0x60,got)||got!=s.data||!base::Get(r,s.data,vt)||vt!=s.image+0x23ad970||
    !base::Get(r,s.data+0x1e8,got)||got!=s.state||!base::Get(r,s.player+0x204,flags)||!(flags&1u)||(flags&0x100u)||
    !base::Get(r,s.player+0x208,allocated)||allocated!=2||!base::Get(r,s.player+0x20c,o.realized)||o.realized>2)return false;
 why("first_bind_creator_record");
 if(!base::Get(r,s.player+0x378,o.recordArray)||!base::Aligned(o.recordArray,8)||!base::Get(r,s.player+0x50,o.blockTable)||!base::Aligned(o.blockTable,8)||
    !base::Get(r,s.player+0x70,ids)||!base::Aligned(ids,2)||!base::Get(r,s.player+0x78,pools)||!pools||!base::Get(r,s.player+0x90,parts)||!parts||
    !base::Get(r,ids+s.creatorId*2,id)||id!=s.creatorId||!base::Get(r,pools+s.creatorId,o.poolIndex)||o.poolIndex>=16||
    !base::Get(r,parts+s.creatorId,part)||(s.coldReadOnly?part!=0:part!=3)||o.record!=o.recordArray+std::uintptr_t(o.poolIndex)*0x6a8||
    !base::Get(r,o.blockTable+std::uintptr_t(o.poolIndex)*8,got)||got!=o.record||!base::Get(r,o.record,vt)||vt!=s.image+0x2295210||
    !base::Get(r,o.record+0x138,got)||got!=s.player||!base::Get(r,o.record+8,o.recordCount)||o.recordCount!=1||
    !base::Get(r,o.record+0xc,o.recordBase)||o.recordBase!=s.creatorId||!base::Get(r,o.record+0x680,o.commonMask))return false;
 if(o.commonMask&(1u<<e.nativeActor)){why("first_bind_already_registered");return false;}
 // Only observe the original getter owner/slot. No guessed getter call.
 why("first_bind_original_getter");
 if(!base::Get(r,o.record+0x10,o.getterOwner)||!base::Aligned(o.getterOwner,8)||!base::Get(r,o.getterOwner,o.getterVtable)||
    o.getterVtable!=s.image+0x22e6070||!base::Get(r,o.getterVtable+0x400,o.getterTarget)||o.getterTarget!=s.image+0xaf0a00)return false;
 // Complete straight-line AF0A00: actor-base, table stride C0, row+70.
 // AF1AE0 allocates that exact array from descriptor count/base48/4C.
 // Never call the getter or manufacture its native actor argument.
 why("first_bind_getter_actor_rows");
 if(!base::Get(r,o.getterOwner+8,o.getterCount)||o.getterCount!=o.recordCount||o.getterCount!=1||
    !base::Get(r,o.getterOwner+0xc,o.getterBase)||o.getterBase!=o.recordBase||
    e.nativeActor<o.getterBase||e.nativeActor-o.getterBase>=o.getterCount||
    !base::Get(r,o.getterOwner+0x10,o.getterRows)||!base::Aligned(o.getterRows,16)||
    !base::Range(o.getterRows,std::uintptr_t(o.getterCount)*0xc0u))return false;
 o.getterRow=o.getterRows+std::uintptr_t(e.nativeActor-o.getterBase)*0xc0u;
 why("first_bind_original_getter_result");
 if(!base::Get(r,o.getterRow+0x70,o.getterEngine)||o.getterEngine!=o.engine)return false;
 // Final native ctor LEA1A6AA51: nextRIP1A6AA58 + A56F30 =24C1988.
 // Engine+10 is the real data link initialized at1A6AC99; retain it.
 why("first_bind_native_engine_vtable");
 if(!base::Get(r,o.engine,o.engineVtable)||o.engineVtable!=s.image+0x24c1988)return false;
 why("first_bind_native_engine_data");
 if(!base::Get(r,o.engine+0x10,o.engineData)||!base::Aligned(o.engineData,8))return false;
 why("first_bind_native_bank_capacity");
 if(!base::Get(r,o.engine+0x1c,o.bankCapacity)||!o.bankCapacity||o.bankCapacity>kBankSlotsCeiling)return false;
 why("first_bind_native_bank_array");
 if(!base::Get(r,o.engine+0x98,o.bankArray)||!base::Aligned(o.bankArray,8)||!base::Range(o.bankArray,o.bankCapacity*0x30u))return false;
 for(unsigned i=0;i<o.bankCapacity;++i)if(!base::Get(r,o.bankArray+std::uintptr_t(i)*0x30+0x18,o.bound[i]))return false;
 // AE4470 qualified actual common getter table; reserved slots16..31.
 why("first_bind_unique_loaded_bank");
 for(unsigned i=0;i<16;++i){
  if(!base::Get(r,s.image+0x2a7f388+std::uintptr_t(i+16)*0x28,o.common[i]))return false;
  if(!o.common[i])continue;std::uintptr_t blob=0,sources=0;std::uint64_t identity=0;
  if(!UniqueBank(r,hash,s.image,o.common[i],blob,sources,identity))continue;
  if(o.uniqueBank&&o.uniqueBank!=o.common[i]){why("first_bind_unique_bank_alias");return false;}
  if(!o.uniqueBank){o.uniqueSlot=i+16;o.uniqueBank=o.common[i];o.uniqueBlob=blob;o.uniqueSources=sources;o.uniqueFileIdentity=identity;}
 }
 if(!o.uniqueBank||o.originalBank==o.uniqueBank)return false;
 for(unsigned i=0;i<o.bankCapacity;++i)if(o.bound[i]){why("first_bind_banks_not_empty");return false;}
 why("first_bind_observed");return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 if(a.player!=b.player||a.recordArray!=b.recordArray||a.blockTable!=b.blockTable||a.record!=b.record||a.getterOwner!=b.getterOwner||
 a.getterVtable!=b.getterVtable||a.getterTarget!=b.getterTarget||a.getterRows!=b.getterRows||a.getterRow!=b.getterRow||
 a.getterEngine!=b.getterEngine||a.getterBase!=b.getterBase||a.getterCount!=b.getterCount||a.engineVtable!=b.engineVtable||a.engine!=b.engine||a.engineData!=b.engineData||a.bankArray!=b.bankArray||
 a.originalBank!=b.originalBank||a.uniqueBank!=b.uniqueBank||a.uniqueBlob!=b.uniqueBlob||a.uniqueSources!=b.uniqueSources||a.uniqueFileIdentity!=b.uniqueFileIdentity||
 a.creatorId!=b.creatorId||a.nativeActor!=b.nativeActor||a.recordBase!=b.recordBase||a.recordCount!=b.recordCount||a.commonMask!=b.commonMask||
 a.bankCapacity!=b.bankCapacity||a.realized!=b.realized||a.uniqueSlot!=b.uniqueSlot||a.poolIndex!=b.poolIndex)return false;
 for(unsigned i=0;i<16;++i)if(a.common[i]!=b.common[i])return false;
 for(unsigned i=0;i<kBankSlotsCeiling;++i)if(a.bound[i]!=b.bound[i])return false;return true;
}
template<class Reader,class Hasher> bool Observe(Reader& r,Hasher& hash,const Scope& s,const Event& e,Reply& reply) noexcept {
 reply={};
 #if !defined(MGS_BACKGROUND_WORKER)
 (void)r;(void)hash;(void)s;(void)e;reply.reason="first_bind_worker_disabled";return false;
 #else
 const char* why="first_bind_read";Observation first{},second{};
 if(!ReadOnce(r,hash,s,e,first,&why)){reply.observed=first;reply.reason=why;return false;}
 if(!ReadOnce(r,hash,s,e,second,&why)||!Same(first,second)){reply.observed=second;reply.reason="first_bind_changed";return false;}
 reply.observed=second;reply.ready=true;
 reply.eligible=!s.coldReadOnly&&s.engineGetterQualified&&s.nativeTimingQualified&&s.compactClosureQualified;
 reply.reason=reply.eligible?"first_bind_qualified":"first_bind_observed_readonly";return true;
 #endif
}
// The provided Append callable must be ORIGINAL1A6B8F0 trampoline, never its
// hook and never a pointer writer. The wrapper remains responsible for calling
// the original normal-bank append afterward, including on every refusal.
template<class Reader,class Hasher,class Append,class Resample> bool TryPrepend(Reader& r,Hasher& hash,const Scope& s,const Event& e,Append& append,Resample& current,Reply& reply) noexcept {
 if(!Observe(r,hash,s,e,reply))return false;
 #if !defined(MGS_BACKGROUND_WORKER) || !defined(MGS_LOCAL_MGO_UNIQUE_REST_BIND)
 (void)append;(void)current;reply.reason="first_bind_append_compiled_off";return false;
 #else
 if(s.coldReadOnly){reply.reason="first_bind_cold_observer_has_no_append_authority";return false;}
 if(!reply.eligible){reply.reason="first_bind_qualification_missing";return false;}
 const Scope fresh=current();if(!SameScope(s,fresh)){reply.reason="first_bind_scope_changed";return false;}
 Reply checked{};if(!Observe(r,hash,fresh,e,checked)||!Same(reply.observed,checked.observed)){reply.reason="first_bind_before_append_changed";return false;}
 if(!append(checked.observed.engine,checked.observed.uniqueBank)){reply.reason="first_bind_original_append_refused";return false;}
 reply=checked;reply.appended=true;reply.reason="first_bind_original_append_submitted";
 std::uintptr_t selected=0;
 if(!base::Get(r,checked.observed.bankArray+0x18,selected)||selected!=checked.observed.uniqueBank){reply.reason="first_bind_append_receipt_failed";return false;}
 reply.reason="first_bind_original_subscription_verified";return true;
 #endif
}
} // namespace mgscoop::sleep_unique_bind
