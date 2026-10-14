#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
#include "carry_recipient_read.hpp"
#include "carry_model_lifetime_contract.hpp"
namespace mgscoop::carry_model_preflight_read {
namespace cr=carry_plugin_read;
struct Slot {std::uintptr_t address=0,function=0,owner=0;std::uint64_t event=0;std::int32_t refs=0;std::uint32_t body=0;};
struct Model {
 std::uintptr_t cs=0,entries=0,entry=0,allocation=0,slab=0;
 std::uint32_t count=0,base=0,flags=0,layers=0;
 std::uint8_t treeCount=0,callbackCount=0;
 Slot slot[255]{};
 unsigned ownedCallbacks=0,freeCallbacks=0;
 bool registrationPresent=false;
};
struct Observation {carry_recipient_read::Observation native{};Model body[2];};
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!cr::Range(image,span))return false;unsigned char bytes[8192]{};
 for(const auto& p:carry_model_lifetime_contract::sites){if(p.rva>span||p.size>span-p.rva||p.size>sizeof(bytes)||!r(image+p.rva,bytes,p.size)){mismatch=p.rva;return false;}for(std::size_t j=0;j<p.size;++j)if(bytes[j]!=p.bytes[j]){mismatch=p.rva;return false;}}
 for(const auto& p:carry_model_lifetime_contract::slots){std::uintptr_t v=0;if(p.rva>span||sizeof(v)>span-p.rva||p.target>=span||!cr::Get(r,image+p.rva,v)||v!=image+p.target){mismatch=p.rva;return false;}}return true;
}
// Independently validates the current block/PS/primary relationship. The body
// must still come from the coordinator's exact-gated current paired reader;
// this function never grants scene/thread/lease or native-call admission.
template<class Reader> bool ReadModelOnce(Reader& r,std::uintptr_t image,const cr::Body& b,Model& out,const char** refusal=nullptr) noexcept {
 out={};const auto stage=[&](const char* why) noexcept {if(refusal)*refusal=why;};stage("model_owner");
 std::uintptr_t vt=0,p=0;std::uint32_t count=0,base=0;
 if(b.id>1||!cr::Aligned(image,0x1000)||!cr::Range(image,0xa01d000)||!cr::Aligned(b.block,8)||!cr::Range(b.block,0x140)||!cr::Aligned(b.ps,8)||!cr::Range(b.ps,0x410)||!cr::Aligned(b.primary,8)||!cr::Get(r,b.block,vt)||vt!=image+0x2295210||!cr::Get(r,b.block+8,count)||count!=1||!cr::Get(r,b.block+0xc,base)||base!=b.id||!cr::Get(r,b.block+0x78,p)||p!=b.ps||!cr::Get(r,b.ps,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||!cr::Get(r,b.ps+0x408,p)||p!=b.block||!cr::Get(r,b.ps+8,base)||base!=b.id||!cr::Get(r,b.primary,vt)||vt!=image+0x23b9ac0||!cr::Get(r,b.primary+8,p)||p!=b.block)return false;
 stage("model_cs");
 if(!cr::Get(r,b.block+0x10,out.cs)||!cr::Aligned(out.cs,8)||!cr::Range(out.cs,0x70)||!cr::Get(r,b.ps+0x238,p)||p!=out.cs||!cr::Get(r,out.cs,vt)||vt!=image+0x22e6070||!cr::Get(r,out.cs+8,out.count)||out.count!=1||!cr::Get(r,out.cs+0xc,out.base)||out.base!=b.id||!cr::Get(r,out.cs+0x10,out.entries)||!cr::Aligned(out.entries,16)||!cr::Range(out.entries,0xc0))return false;
 out.entry=out.entries;stage("model_allocation");
 if(!cr::Get(r,out.entry+0x78,out.allocation)||!cr::Aligned(out.allocation,16)||!cr::Get(r,out.entry+0x88,out.slab)||!cr::Get(r,out.entry+0xac,out.flags)||!cr::Get(r,out.entry+0xb0,out.layers)||!cr::Get(r,out.entry+0xb9,out.treeCount)||!cr::Get(r,out.entry+0xba,out.callbackCount))return false;
 // B6D980 stores callback slab = backing allocation + treeCount*(10+C0).
 // This proves the exact current constructor layout, not heap ownership or
 // retention. No calls are permitted from this read-only observation.
 const std::size_t prefix=std::size_t(out.treeCount)*0xd0u,extent=std::size_t(out.callbackCount)*0x60u;
 if(out.callbackCount<2||!cr::Range(out.allocation,prefix+extent)||out.slab!=out.allocation+prefix||!cr::Aligned(out.slab,16)||!cr::Range(out.slab,extent))return false;
 bool a=false,c=false;stage("model_callbacks");
 for(unsigned k=0;k<out.callbackCount;++k){auto& v=out.slot[k];v.address=out.slab+std::uintptr_t(k)*0x60;
  if(!cr::Get(r,v.address,vt)||vt!=image+0x22f6380||!cr::Get(r,v.address+8,v.refs)||v.refs<1||!cr::Get(r,v.address+0x40,v.function)||!cr::Get(r,v.address+0x48,v.owner)||!cr::Get(r,v.address+0x50,v.event)||!cr::Get(r,v.address+0x58,v.body))return false;
  if(!v.function){if(v.owner||v.event||v.body)return false;++out.freeCallbacks;continue;}
  if(v.owner!=b.primary)continue;
  ++out.ownedCallbacks;
  const auto hash=v.event&0xffffffffffffULL;
  if(v.body!=b.id)return false;
  if(v.function==image+0x112fc50&&hash==0x6504d35367f0ULL){if(a)return false;a=true;}
  else if(v.function==image+0x112fcd0&&hash==0x20ca5ed42a91ULL){if(c)return false;c=true;}
  else return false;
 }
 out.registrationPresent=a&&c&&out.ownedCallbacks==2;stage("model_ready");return true;
}
inline bool Same(const Model& a,const Model& b) noexcept {
 if(a.cs!=b.cs||a.entries!=b.entries||a.entry!=b.entry||a.allocation!=b.allocation||a.slab!=b.slab||a.count!=b.count||a.base!=b.base||a.flags!=b.flags||a.layers!=b.layers||a.treeCount!=b.treeCount||a.callbackCount!=b.callbackCount||a.ownedCallbacks!=b.ownedCallbacks||a.freeCallbacks!=b.freeCallbacks||a.registrationPresent!=b.registrationPresent)return false;
 for(unsigned k=0;k<a.callbackCount;++k){const auto& x=a.slot[k];const auto& y=b.slot[k];if(x.address!=y.address||x.function!=y.function||x.owner!=y.owner||x.event!=y.event||x.refs!=y.refs||x.body!=y.body)return false;}return true;
}
template<class Reader> bool ReadModelPair(Reader& r,std::uintptr_t image,const cr::Body& b,Model& out,const char** refusal=nullptr) noexcept {
 out={};Model x{},y{};if(!ReadModelOnce(r,image,b,x,refusal)||!ReadModelOnce(r,image,b,y,refusal))return false;
 if(!Same(x,y)){if(refusal)*refusal="model_changed";return false;}out=x;return true;
}
template<class Reader> bool ReadOnce(Reader& r,const cr::Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
 out={};if(!carry_recipient_read::ReadOnce(r,s,out.native)){if(refusal)*refusal="parent_chain";return false;}
 for(unsigned k=0;k<2;++k)if(!ReadModelOnce(r,s.image,out.native.carrier.body[k],out.body[k],refusal))return false;
 if(out.body[0].cs==out.body[1].cs||out.body[0].entries==out.body[1].entries||out.body[0].allocation==out.body[1].allocation||out.body[0].slab==out.body[1].slab){if(refusal)*refusal="model_alias";return false;}return true;
}
// Numeric data may be exposed for qualification. Pointers and refcounts never
// unlock downed controls or nominate End; a protected lifetime domain remains
// an independent obligation even when both reads match.
} // namespace mgscoop::carry_model_preflight_read
