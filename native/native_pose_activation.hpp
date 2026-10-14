#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "native_pose_birth.hpp"
#include "native_pose_activation_contract.hpp"
namespace mgscoop::native_pose_activation {
namespace birth=native_pose_birth;
// An actor/resource attachment completed by 9CB200 is distinct from a block
// constructed by 9C3CD0. No constructor Completion is manufactured here.
struct ActivationCompletion {
 birth::Identity before{},after{};
 std::uintptr_t partsGo=0,rowBefore=0,rowAfter=0,factoryEntry=0;
 std::uint64_t frame=0,operation=0,lifecycleEpoch=0;
 std::uint32_t body=UINT32_MAX,threadBefore=0,threadAfter=0;
 bool installedBefore=false,installedAfter=false,workerBefore=false,workerAfter=false;
 bool exactActivationCaller=false,normalReturn=false,alTrue=false;
 bool bit14Before=false,bit14After=false,exactFactoryCaller=false;
 bool factoryNormal=false,factoryOutputZero=false,factoryOutputIdentity=false;
 unsigned factoryCalls=0;
};
struct Pending {ActivationCompletion completion{};bool ready=false;};
struct FinalCompletion {
 birth::Identity identity{};std::uintptr_t row=0,partsGo=0;
 std::uint64_t frame=0,operation=0;std::uint32_t thread=0;
 bool originalPartsReturned=false,installed=false,worker=false;
 bool parts3=false,bit14=false,pairedNativeResourceClosure=false;
};
struct Receipt {
 birth::Identity identity{};std::uintptr_t row=0;
 std::uint32_t thread=0,body=UINT32_MAX;
 std::uint64_t serial=0,frame=0;bool ready=false;
};
inline Pending Stage(const ActivationCompletion& c,std::uint64_t epoch) noexcept {
 if(c.body>1||!c.frame||!c.operation||!c.lifecycleEpoch||c.lifecycleEpoch!=epoch||
  !c.partsGo||c.before.player<0x20||c.partsGo!=c.before.player-0x20||
  !birth::Same(c.before,c.after)||!c.rowBefore||c.rowBefore!=c.rowAfter||
  c.factoryEntry!=c.after.entry||!c.threadBefore||c.threadBefore!=c.threadAfter||
  !c.installedBefore||!c.installedAfter||!c.workerBefore||!c.workerAfter||
  !c.exactActivationCaller||!c.normalReturn||!c.alTrue||c.bit14Before||!c.bit14After||
  c.factoryCalls!=1||!c.exactFactoryCaller||!c.factoryNormal||
  !c.factoryOutputZero||!c.factoryOutputIdentity)return {};
 return {c,true};
}
inline Receipt Complete(const Pending& p,const FinalCompletion& c,std::uint64_t serial) noexcept {
 const auto& a=p.completion;
 if(!p.ready||!serial||!c.originalPartsReturned||!c.installed||!c.worker||
  !c.parts3||!c.bit14||!c.pairedNativeResourceClosure||
  c.frame!=a.frame||c.operation!=a.operation||c.thread!=a.threadAfter||c.partsGo!=a.partsGo||
  c.row!=a.rowAfter||!birth::Same(a.after,c.identity))return {};
 return {c.identity,c.row,c.thread,a.body,serial,c.frame,true};
}
inline birth::Receipt Observed(const Receipt& r) noexcept {
 if(!r.ready)return {};
 return {r.identity,r.thread,r.serial,true,birth::Origin::actorActivation};
}
inline bool Current(const Receipt& r,const birth::Identity& identity) noexcept {
 return birth::Current(Observed(r),identity);
}
inline bool Matches(const birth::Identity& identity,std::uintptr_t at,unsigned kind) noexcept {
 if(!at)return false;
 return kind==0?at==identity.block:kind==1?at==identity.cs:kind==2?at==identity.entry:
  kind==4?identity.player>=0x20&&at==identity.player-0x20:false;
}
// The original entry hooks retire genuine completed/pending evidence. An ABA
// to the same address cannot recover a cleared witness. Other-body retirement
// may advance the global constructor epoch but does not erase this completed
// factory witness; matching invalidation remains mandatory until parts return.
inline bool Invalidate(Pending& p,Receipt& r,std::uintptr_t at,unsigned kind) noexcept {
 bool changed=false;
 if(p.ready&&Matches(p.completion.after,at,kind)){p={};changed=true;}
 if(r.ready&&Matches(r.identity,at,kind)){r={};changed=true;}
 return changed;
}
inline void DiscardFrame(Pending& p,std::uint64_t frame) noexcept {
 if(p.ready&&p.completion.frame==frame)p={};
}
inline birth::Receipt Effective(const birth::Receipt& constructor,const Receipt& activation) noexcept {
 const auto actor=Observed(activation);
 return actor.ready&&(!constructor.ready||actor.serial>constructor.serial)?actor:constructor;
}
template<class R>bool CodeGate(R& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept {
 mismatch=0;if(!image||!span||image>std::numeric_limits<std::uintptr_t>::max()-span)return false;
 unsigned char buffer[8192]{};
 for(const auto& s:native_pose_activation_contract::sites){
  if(s.rva>span||s.size>span-s.rva||s.size>sizeof(buffer)||
   !r(image+s.rva,buffer,s.size)||std::memcmp(buffer,s.bytes,s.size)){mismatch=s.rva;return false;}
 }
 for(const auto& s:native_pose_activation_contract::slots){std::uintptr_t value=0;
  if(s.rva>span||sizeof(value)>span-s.rva||s.target>=span||
   !r(image+s.rva,&value,sizeof(value))||value!=image+s.target){mismatch=s.rva;return false;}
 }return true;
}
}
