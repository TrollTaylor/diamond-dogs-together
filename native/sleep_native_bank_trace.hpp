#pragma once
// Strict, read-only natural bank receipts. This API has no append callable,
// eligibility bit, gameplay epoch conversion, cache operation or game writer.
#include "sleep_unique_bind.hpp"
#include "sleep_native_bank_trace_contract.hpp"
namespace mgscoop::sleep_native_bank_trace {
namespace ub=sleep_unique_bind;namespace b=ub::base;
inline constexpr std::uint64_t restIds[2]={0xfc500f8dffc6c8d6ull,0xfc53f6ab0bc9caafull};
enum class Phase : unsigned {commonEnter,commonBefore,commonAfter,commonLeave,nativeBefore,nativeAfter,status};
struct Bank {
 std::uintptr_t pointer=0,blob=0,sources=0,restSources[2]{};
 std::uint64_t fileIdentity=0;
 std::uint32_t count=0,restIndices[2]{},restMask=0;
 std::uint8_t loadKind=0;
 bool typed=false,loaded=false,uniqueVerified=false;
};
struct Snapshot {
 ub::Observation owner{};Bank banks[ub::kBankSlotsCeiling]{};
 std::uintptr_t common[32]{};
 std::uint32_t empty=0,firstEmpty=0,commonUniqueSlot=0,boundUniqueSlot=0;
 std::uint8_t part=0xff;
 bool commonUniqueLoaded=false,boundUnique=false;
};
struct Receipt {
 bool ready=false,ownerStable=false,originalResult=false,changedOneSlot=false,incomingReady=false;
 bool preexisting=false,duplicate=false,registrationCompleted=false;
 bool rawConstructor=false,rawReady=false,rawEngineStable=false;
 std::uint64_t rawSerial=0;
 std::uint32_t rawThread=0;
 std::uint64_t rawBeforeTick=0,rawAfterTick=0;
 unsigned ordinal=0,changedSlot=0;
 std::uint64_t frameSerial=0;
 Phase phase=Phase::status;
 ub::Event event{};Snapshot before{},after{};Bank incoming{};
 const char* reason="trace_disabled";
};
inline bool IsCommon(Phase p) noexcept {return p==Phase::commonEnter||p==Phase::commonBefore||p==Phase::commonAfter||p==Phase::commonLeave;}
inline bool IsAdd(Phase p) noexcept {return p==Phase::commonBefore||p==Phase::commonAfter||p==Phase::nativeBefore||p==Phase::nativeAfter;}
inline bool ColdPartRequired(Phase p,std::uintptr_t caller,std::uintptr_t image) noexcept {
 return IsCommon(p)||caller==image+0x9c3fc1;
}
inline bool NaturalCaller(std::uintptr_t image,std::uintptr_t caller) noexcept {
 for(const auto& pin:sleep_native_bank_trace_contract::callers)if(caller==image+pin.returnRva)return true;
 return false;
}
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;unsigned char bytes[8192]{};
 for(const auto& pin:sleep_native_bank_trace_contract::sites){
  if(pin.rva>span||pin.size>span-pin.rva||pin.size>sizeof(bytes)||!b::Range(image+pin.rva,pin.size)||
     !r(image+pin.rva,bytes,pin.size)||std::memcmp(bytes,pin.bytes,pin.size)){mismatch=pin.rva;return false;}
 }return true;
}
// Native1A754C0 looks up the file's header table, then returns sources+index*28.
// These fields diagnose availability; restMask alone is not a selected-payload
// fingerprint. Root's selected GANI reader remains the selection authority.
template<class Reader,class Hasher> bool ReadBank(Reader& r,Hasher& hash,std::uintptr_t image,std::uintptr_t pointer,Bank& out) noexcept {
 out={};out.pointer=pointer;if(!pointer)return true;std::uintptr_t vt=0,loaded=0;
 if(!b::Aligned(pointer,8)||!b::Get(r,pointer,vt))return false;
 if(vt!=image+0x24c2890)return true;
 out.typed=true;
 if(!b::Get(r,pointer+0x30,out.fileIdentity)||!b::Get(r,pointer+0x40,out.loadKind)||
    !b::Get(r,pointer+0x84,out.count)||!b::Get(r,pointer+0x88,out.sources)||
    !b::Get(r,pointer+0x98,out.blob)||!b::Get(r,pointer+0x60,loaded))return false;
 if(out.loadKind!=2||!out.blob||!out.sources)return true;
 std::uint32_t magic=0,count=0;std::uint16_t flags=0;
 if(out.count>4096||!out.count||loaded!=out.blob||!b::Aligned(out.blob,16)||!b::Aligned(out.sources,8)||
    !b::Range(out.sources,std::size_t(out.count)*0x28)||!b::Range(out.blob,0x20+std::size_t(out.count)*0x20)||
    !b::Get(r,out.blob,magic)||magic!=0x0c012b72||!b::Get(r,out.blob+4,count)||count!=out.count||
    !b::Get(r,out.blob+0x12,flags)||!(flags&0x1000u))return false;
 out.loaded=true;
 for(unsigned key=0;key<2;++key){
  unsigned lo=0,countLeft=out.count,found=out.count;
  while(countLeft){const unsigned middle=countLeft/2,mid=lo+middle;std::uint64_t id=0;
   if(!b::Get(r,out.blob+0x20+std::uintptr_t(mid)*0x20,id))return false;
   if(id==restIds[key]){found=mid;break;}
   if(id>restIds[key])countLeft=middle;else {lo=mid+1;countLeft-=middle+1;}
  }
  if(found==out.count)continue;std::uint64_t id=0;
  const auto source=out.sources+std::uintptr_t(found)*0x28;std::uintptr_t owner=0;std::uint32_t index=0;std::uint16_t kind=0;
  if(!b::Get(r,source,id)||id!=restIds[key]||!b::Get(r,source+8,owner)||owner!=pointer||
     !b::Get(r,source+0x24,index)||index!=found||!b::Get(r,source+0x20,kind)||(kind&15u)!=3)return false;
  out.restMask|=1u<<key;out.restIndices[key]=found;out.restSources[key]=source;
 }
 if(out.count==2){std::uintptr_t blob=0,sources=0;std::uint64_t identity=0;
  out.uniqueVerified=ub::UniqueBank(r,hash,image,pointer,blob,sources,identity)&&blob==out.blob&&sources==out.sources&&identity==out.fileIdentity;
 }return true;
}
template<class Reader,class Hasher> bool ReadOnce(Reader& r,Hasher& hash,const ub::Scope& s,const ub::Event& e,Phase phase,Snapshot& out,const char*& why) noexcept {
 out={};why="trace_scope";
 if(ub::ScopeRefusal(s)||!s.coldReadOnly)return false;
 if(e.record!=s.creatorRecord||e.nativeActor!=s.expectedNativeActor||e.creatorInvocation!=s.creatorInvocation)return false;
 if(IsAdd(phase)&&(!NaturalCaller(s.image,e.returnAddress)||!b::Aligned(e.engine,8)||!b::Aligned(e.originalBank,8)))return false;
 auto& o=out.owner;o.player=s.player;o.record=e.record;o.creatorId=s.creatorId;o.nativeActor=e.nativeActor;o.originalBank=e.originalBank;
 std::uintptr_t got=0,vt=0,ids=0,pools=0,parts=0;std::uint32_t flags=0,allocated=0;std::uint16_t logical=0;
 why="trace_player_owner";
 if(!b::Get(r,s.image+0x2a82c30,got)||got!=s.player||!b::Get(r,s.player,vt)||vt!=s.image+0x2295490||
    !b::Get(r,s.player+0x60,got)||got!=s.data||!b::Get(r,s.data,vt)||vt!=s.image+0x23ad970||
    !b::Get(r,s.data+0x1e8,got)||got!=s.state||!b::Get(r,s.player+0x204,flags)||!(flags&1u)||(flags&0x100u)||
    !b::Get(r,s.player+0x208,allocated)||allocated!=2||!b::Get(r,s.player+0x20c,o.realized)||o.realized>2)return false;
 why="trace_current_creator_record";
 if(!b::Get(r,s.player+0x378,o.recordArray)||!b::Aligned(o.recordArray,8)||!b::Get(r,s.player+0x50,o.blockTable)||!b::Aligned(o.blockTable,8)||
    !b::Get(r,s.player+0x70,ids)||!b::Aligned(ids,2)||!b::Get(r,s.player+0x78,pools)||!pools||!b::Get(r,s.player+0x90,parts)||!parts||
    !b::Get(r,ids+s.creatorId*2,logical)||logical!=s.creatorId||!b::Get(r,pools+s.creatorId,o.poolIndex)||o.poolIndex>=16||
    !b::Get(r,parts+s.creatorId,out.part)||(ColdPartRequired(phase,e.returnAddress,s.image)?out.part!=0:(out.part!=0&&out.part!=3))||
    o.record!=o.recordArray+std::uintptr_t(o.poolIndex)*0x6a8||!b::Get(r,o.blockTable+std::uintptr_t(o.poolIndex)*8,got)||got!=o.record||
    !b::Get(r,o.record,vt)||vt!=s.image+0x2295210||!b::Get(r,o.record+0x138,got)||got!=s.player||
    !b::Get(r,o.record+8,o.recordCount)||o.recordCount!=1||!b::Get(r,o.record+0xc,o.recordBase)||o.recordBase!=s.creatorId||
    !b::Get(r,o.record+0x680,o.commonMask))return false;
 why="trace_getter_owner";
 if(!b::Get(r,o.record+0x10,o.getterOwner)||!b::Aligned(o.getterOwner,8)||!b::Get(r,o.getterOwner,o.getterVtable)||o.getterVtable!=s.image+0x22e6070||
    !b::Get(r,o.getterVtable+0x400,o.getterTarget)||o.getterTarget!=s.image+0xaf0a00||
    !b::Get(r,o.getterOwner+8,o.getterCount)||o.getterCount!=1||!b::Get(r,o.getterOwner+0xc,o.getterBase)||o.getterBase!=o.recordBase||
    e.nativeActor<o.getterBase||e.nativeActor-o.getterBase>=o.getterCount||!b::Get(r,o.getterOwner+0x10,o.getterRows)||
    !b::Aligned(o.getterRows,16)||!b::Range(o.getterRows,std::size_t(o.getterCount)*0xc0))return false;
 o.getterRow=o.getterRows+std::uintptr_t(e.nativeActor-o.getterBase)*0xc0;
 why="trace_engine_row_closure";
 if(!b::Get(r,o.getterRow+0x70,o.getterEngine)||!b::Aligned(o.getterEngine,8)||(e.engine&&e.engine!=o.getterEngine))return false;
 o.engine=o.getterEngine;
 if(!b::Get(r,o.engine,o.engineVtable)||o.engineVtable!=s.image+0x24c1988||
    !b::Get(r,o.engine+0x10,o.engineData)||!b::Aligned(o.engineData,8)||
    !b::Get(r,o.engine+0x1c,o.bankCapacity)||!o.bankCapacity||o.bankCapacity>ub::kBankSlotsCeiling||
    !b::Get(r,o.engine+0x98,o.bankArray)||!b::Aligned(o.bankArray,8)||!b::Range(o.bankArray,o.bankCapacity*0x30u))return false;
 out.firstEmpty=o.bankCapacity;why="trace_native_bank_slots";
 for(unsigned i=0;i<o.bankCapacity;++i){
  if(!b::Get(r,o.bankArray+std::uintptr_t(i)*0x30+0x18,o.bound[i])||!ReadBank(r,hash,s.image,o.bound[i],out.banks[i]))return false;
  if(!o.bound[i]){++out.empty;if(out.firstEmpty==o.bankCapacity)out.firstEmpty=i;}
  if(out.banks[i].uniqueVerified){if(out.boundUnique&&o.uniqueBank!=o.bound[i])return false;
   if(!out.boundUnique){out.boundUnique=true;out.boundUniqueSlot=i;o.uniqueBank=o.bound[i];}}
 }
 why="trace_native_common_slots";
 for(unsigned i=0;i<32;++i){
  if(!b::Get(r,s.image+0x2a7f388+std::uintptr_t(i)*0x28,out.common[i]))return false;
  if(i<16||!out.common[i])continue;Bank bank{};if(!ReadBank(r,hash,s.image,out.common[i],bank))return false;
  if(!bank.uniqueVerified)continue;
  if(out.commonUniqueLoaded&&o.uniqueBank&&o.uniqueBank!=bank.pointer)return false;
  out.commonUniqueLoaded=true;out.commonUniqueSlot=i;o.uniqueBank=bank.pointer;o.uniqueBlob=bank.blob;
  o.uniqueSources=bank.sources;o.uniqueFileIdentity=bank.fileIdentity;o.uniqueSlot=i;
 }
 why="trace_typed_snapshot_readonly";return true;
}
inline bool SameBank(const Bank& a,const Bank& c) noexcept {
 if(a.pointer!=c.pointer||a.blob!=c.blob||a.sources!=c.sources||a.fileIdentity!=c.fileIdentity||a.count!=c.count||a.restMask!=c.restMask||
    a.loadKind!=c.loadKind||a.typed!=c.typed||a.loaded!=c.loaded||a.uniqueVerified!=c.uniqueVerified)return false;
 for(unsigned i=0;i<2;++i)if(a.restSources[i]!=c.restSources[i]||a.restIndices[i]!=c.restIndices[i])return false;return true;
}
inline bool Same(const Snapshot& a,const Snapshot& c) noexcept {
 if(!ub::Same(a.owner,c.owner)||a.empty!=c.empty||a.firstEmpty!=c.firstEmpty||a.part!=c.part||
    a.commonUniqueSlot!=c.commonUniqueSlot||a.boundUniqueSlot!=c.boundUniqueSlot||a.commonUniqueLoaded!=c.commonUniqueLoaded||a.boundUnique!=c.boundUnique)return false;
 for(unsigned i=0;i<32;++i)if(a.common[i]!=c.common[i])return false;
 for(unsigned i=0;i<ub::kBankSlotsCeiling;++i)if(!SameBank(a.banks[i],c.banks[i]))return false;return true;
}
template<class Reader,class Hasher> bool ReadPair(Reader& r,Hasher& hash,const ub::Scope& s,const ub::Event& e,Phase phase,Snapshot& out,const char*& why) noexcept {
 #if !defined(MGS_BACKGROUND_WORKER)
 (void)r;(void)hash;(void)s;(void)e;(void)phase;out={};why="trace_worker_disabled";return false;
 #else
 Snapshot first{},second{};if(!ReadOnce(r,hash,s,e,phase,first,why)){out=first;return false;}
 if(!ReadOnce(r,hash,s,e,phase,second,why)||!Same(first,second)){out=second;why="trace_snapshot_changed";return false;}
 out=second;return true;
 #endif
}
inline bool SameOwner(const Snapshot& a,const Snapshot& c) noexcept {
 const auto& x=a.owner;const auto& y=c.owner;
 return x.player==y.player&&x.record==y.record&&x.recordArray==y.recordArray&&x.blockTable==y.blockTable&&x.poolIndex==y.poolIndex&&
  x.creatorId==y.creatorId&&x.nativeActor==y.nativeActor&&x.getterOwner==y.getterOwner&&x.getterVtable==y.getterVtable&&
  x.getterTarget==y.getterTarget&&x.getterRows==y.getterRows&&x.getterRow==y.getterRow&&x.getterBase==y.getterBase&&x.getterCount==y.getterCount&&
  x.engine==y.engine&&x.engineVtable==y.engineVtable&&x.engineData==y.engineData&&x.bankArray==y.bankArray&&x.bankCapacity==y.bankCapacity&&a.part==c.part;
}
inline unsigned FirstAvailableRestSlot(const Snapshot& s,unsigned key) noexcept {
 if(key>1)return s.owner.bankCapacity;
 for(unsigned i=0;i<s.owner.bankCapacity&&i<ub::kBankSlotsCeiling;++i)
  if(s.banks[i].typed&&s.banks[i].loaded&&(s.banks[i].restMask&(1u<<key)))return i;
 return s.owner.bankCapacity;
}
inline void CompleteAdd(Receipt& r) noexcept {
 r.ownerStable=SameOwner(r.before,r.after);if(!r.ownerStable){r.reason="trace_native_owner_changed";return;}
 unsigned changes=0;
 for(unsigned i=0;i<r.before.owner.bankCapacity;++i){
  if(r.before.owner.bound[i]==r.event.originalBank)r.duplicate=true;
  if(r.before.owner.bound[i]!=r.after.owner.bound[i]){++changes;r.changedSlot=i;}
 }
 r.preexisting=r.before.empty!=r.before.owner.bankCapacity;
 r.changedOneSlot=changes==1&&r.changedSlot==r.before.firstEmpty&&r.before.owner.bound[r.changedSlot]==0&&
  r.after.owner.bound[r.changedSlot]==r.event.originalBank&&r.before.empty==r.after.empty+1;
 r.ready=r.originalResult?r.changedOneSlot:changes==0;
 r.reason=r.ready?(r.originalResult?"trace_original_append_observed":"trace_original_append_refused_unchanged"):"trace_original_append_delta_unqualified";
}
inline void CompleteCommon(Receipt& r) noexcept {
 r.ownerStable=SameOwner(r.before,r.after);const auto bit=1u<<r.event.nativeActor;
 r.registrationCompleted=r.ownerStable&&(r.after.owner.commonMask&bit)&&r.after.owner.commonMask==(r.before.owner.commonMask|bit);
 r.preexisting=r.before.empty!=r.before.owner.bankCapacity;r.ready=r.ownerStable&&r.registrationCompleted;
 r.reason=r.ready?"trace_original_common_completed_readonly":"trace_common_completion_unqualified";
}
} // namespace mgscoop::sleep_native_bank_trace
