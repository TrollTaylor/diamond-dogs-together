#pragma once
// Exact natural9C3FC1 diagnostic when canonical actor tables do not yet exist.
// No player, creator, epoch, generation, birth or append authority in this API.
#include "sleep_native_bank_trace.hpp"
namespace mgscoop::sleep_raw_constructor_trace {
namespace bt=sleep_native_bank_trace;namespace ub=sleep_unique_bind;namespace b=ub::base;
struct Scope {
 bool exactPe=false,codeGate=false,privateCurrent=false,copiedSaveLab=false,exactProfile=false;
 std::uintptr_t image=0;
 std::uint32_t thread=0;
 std::uint64_t now=0;
};
inline const char* Refusal(const Scope& s,const ub::Event& e) noexcept {
 if(!s.exactPe||!s.codeGate)return "raw_constructor_code_gate";
 if(!s.privateCurrent||!s.copiedSaveLab||!s.exactProfile)return "raw_constructor_private_profile";
 if(!b::Range(s.image,0xa01d000)||!s.thread||!s.now)return "raw_constructor_scope";
 if(e.returnAddress!=s.image+0x9c3fc1||e.record||e.nativeActor!=UINT32_MAX||e.creatorInvocation||
    !b::Aligned(e.engine,8)||!b::Aligned(e.originalBank,8))return "raw_constructor_exact_unknown_call";
 return nullptr;
}
inline bool SameScope(const Scope& a,const Scope& z) noexcept {
 return a.exactPe==z.exactPe&&a.codeGate==z.codeGate&&a.privateCurrent==z.privateCurrent&&
  a.copiedSaveLab==z.copiedSaveLab&&a.exactProfile==z.exactProfile&&a.image==z.image&&a.thread==z.thread&&
  z.now>=a.now&&z.now-a.now<=250;
}
template<class Reader,class Hasher> bool ReadOnce(Reader& r,Hasher& hash,const Scope& s,const ub::Event& e,bt::Snapshot& out,bt::Bank& incoming,const char*& why) noexcept {
 out={};incoming={};if(const auto* refused=Refusal(s,e)){why=refused;return false;}
 // Only native engine fields are populated. All actor/getter/creator fields
 // stay zero; unknown actor remains UINT32_MAX in the ORIGINAL event.
 auto& o=out.owner;o.engine=e.engine;o.originalBank=e.originalBank;
 why="raw_constructor_engine_vtable";
 if(!b::Get(r,e.engine,o.engineVtable)||o.engineVtable!=s.image+0x24c1988)return false;
 why="raw_constructor_engine_data";
 if(!b::Get(r,e.engine+0x10,o.engineData)||!b::Aligned(o.engineData,8))return false;
 why="raw_constructor_bank_capacity";
 if(!b::Get(r,e.engine+0x1c,o.bankCapacity)||!o.bankCapacity||o.bankCapacity>ub::kBankSlotsCeiling)return false;
 why="raw_constructor_bank_array";
 if(!b::Get(r,e.engine+0x98,o.bankArray)||!b::Aligned(o.bankArray,8)||!b::Range(o.bankArray,o.bankCapacity*0x30u))return false;
 why="raw_constructor_incoming_mtar";
 if(!bt::ReadBank(r,hash,s.image,e.originalBank,incoming)||!incoming.typed)return false;
 out.firstEmpty=o.bankCapacity;why="raw_constructor_bound_slots";
 for(unsigned i=0;i<o.bankCapacity;++i){
  if(!b::Get(r,o.bankArray+std::uintptr_t(i)*0x30+0x18,o.bound[i])||!bt::ReadBank(r,hash,s.image,o.bound[i],out.banks[i]))return false;
  if(!o.bound[i]){++out.empty;if(out.firstEmpty==o.bankCapacity)out.firstEmpty=i;}
  if(out.banks[i].uniqueVerified){if(out.boundUnique&&o.uniqueBank!=o.bound[i])return false;
   if(!out.boundUnique){out.boundUnique=true;out.boundUniqueSlot=i;o.uniqueBank=o.bound[i];}}
 }
 why="raw_constructor_common_unique";
 for(unsigned i=16;i<32;++i){
  if(!b::Get(r,s.image+0x2a7f388+std::uintptr_t(i)*0x28,out.common[i]))return false;
  if(!out.common[i])continue;bt::Bank bank{};
  if(!bt::ReadBank(r,hash,s.image,out.common[i],bank))return false;
  if(!bank.uniqueVerified)continue;
  if(out.commonUniqueLoaded&&o.uniqueBank&&o.uniqueBank!=bank.pointer)return false;
  out.commonUniqueLoaded=true;out.commonUniqueSlot=i;o.uniqueBank=bank.pointer;o.uniqueBlob=bank.blob;
  o.uniqueSources=bank.sources;o.uniqueFileIdentity=bank.fileIdentity;o.uniqueSlot=i;
 }
 why="raw_constructor_typed_readonly";return true;
}
template<class Reader,class Hasher> bool ReadPair(Reader& r,Hasher& hash,const Scope& s,const ub::Event& e,bt::Snapshot& out,bt::Bank& incoming,const char*& why) noexcept {
 #if !defined(MGS_BACKGROUND_WORKER)
 (void)r;(void)hash;(void)s;(void)e;out={};incoming={};why="raw_constructor_worker_disabled";return false;
 #else
 bt::Snapshot first{},second{};bt::Bank a{},z{};
 if(!ReadOnce(r,hash,s,e,first,a,why)){out=first;incoming=a;return false;}
 if(!ReadOnce(r,hash,s,e,second,z,why)||!bt::Same(first,second)||!bt::SameBank(a,z)){
  out=second;incoming=z;why="raw_constructor_snapshot_changed";return false;}
 out=second;incoming=z;return true;
 #endif
}
inline void CompleteAdd(bt::Receipt& r) noexcept {
 const auto& a=r.before.owner;const auto& z=r.after.owner;
 r.rawEngineStable=a.engine==z.engine&&a.engineVtable==z.engineVtable&&a.engineData==z.engineData&&a.bankArray==z.bankArray&&a.bankCapacity==z.bankCapacity;
 if(!r.rawEngineStable){r.reason="raw_constructor_engine_changed";return;}
 unsigned changes=0;
 for(unsigned i=0;i<a.bankCapacity;++i){
  if(a.bound[i]==r.event.originalBank)r.duplicate=true;
  if(a.bound[i]!=z.bound[i]){++changes;r.changedSlot=i;}
 }
 r.preexisting=r.before.empty!=a.bankCapacity;
 r.changedOneSlot=changes==1&&r.changedSlot==r.before.firstEmpty&&!a.bound[r.changedSlot]&&
  z.bound[r.changedSlot]==r.event.originalBank&&r.before.empty==r.after.empty+1;
 r.rawReady=r.originalResult?r.changedOneSlot:changes==0;
 r.reason=r.rawReady?(r.originalResult?"raw_constructor_original_append_observed":"raw_constructor_original_false_unchanged"):"raw_constructor_original_delta_unqualified";
 // Intentionally never set ready/ownerStable/registrationCompleted: an
 // opaque native object receipt cannot substitute for genuine actor closure.
}
} // namespace mgscoop::sleep_raw_constructor_trace
