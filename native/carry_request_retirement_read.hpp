#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "carry_recipient_read.hpp"
#include "carry_request_retirement.hpp"
#include "carry_request_retirement_contract.hpp"
namespace mgscoop::carry_request_retirement_read {
namespace cr=carry_plugin_read;
struct Observation {
 cr::Observation parent{};
 std::uintptr_t root=0,slab=0,descriptor=0,array=0;
 std::uint64_t bytes=0,used=0;
 carry_request_payload::RecipientRequest packet[2]{};
};
// Require this supplemental gate AND the existing paired player/carrier and
// recipient gates before Scope.exactGate. Observers never authorize writes.
template<class Reader>bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!cr::Range(image,span))return false;unsigned char buffer[8192]{};
 for(const auto& p:carry_request_retirement_contract::sites){if(p.rva>span||p.size>span-p.rva||p.size>sizeof(buffer)||!r(image+p.rva,buffer,p.size)){mismatch=p.rva;return false;}for(std::size_t j=0;j<p.size;++j)if(buffer[j]!=p.bytes[j]){mismatch=p.rva;return false;}}
 for(const auto& p:carry_request_retirement_contract::slots){std::uintptr_t v=0;if(p.rva>span||sizeof(v)>span-p.rva||!cr::Get(r,image+p.rva,v)||v!=image+p.target){mismatch=p.rva;return false;}}return true;
}
inline bool Within(const Observation& o,std::uintptr_t p,std::size_t bytes) noexcept {return cr::Range(p,bytes)&&p>=o.slab&&p-o.slab<=o.bytes&&bytes<=o.bytes-(p-o.slab);}
template<class Reader>bool ReadOnce(Reader& r,const cr::Scope& s,Observation& out) noexcept {
 out={};if(!cr::ReadOnce(r,s,out.parent)||s.data<0x20)return false;out.root=s.data-0x20;
 std::uintptr_t vt=0,head=0;std::uint16_t flags=0;
 if(!cr::Get(r,out.root,vt)||vt!=s.image+0x23ad938||!cr::Get(r,s.data+0x1a0,out.slab)||!cr::Aligned(out.slab,16)||!cr::Get(r,s.data+0x1b0,out.bytes)||out.bytes<0x80||out.bytes>0x100000||!cr::Range(out.slab,static_cast<std::size_t>(out.bytes))||!cr::Get(r,s.data+0x1b8,out.used)||out.used>out.bytes||!cr::Get(r,s.data+0x1c2,flags)||!(flags&1)||!cr::Get(r,s.data+0x1a8,head)||!cr::Get(r,s.data+0x1d8,out.array)||!cr::Aligned(out.array,16)||!Within(out,out.array,0x80))return false;
 // FDE560 produces the callback/next/array/stride/count allocation descriptor
 // in the exact Q-owned slab. Follow only this bounded allocator-owned list.
 std::uintptr_t seen[64]{};unsigned depth=0,matches=0;
 while(head){
  if(depth==64||!cr::Aligned(head,8)||!Within(out,head,0x28))return false;
  for(unsigned i=0;i<depth;++i)if(seen[i]==head)return false;seen[depth++]=head;
  std::uintptr_t callback=0,next=0,array=0;std::uint64_t stride=0,count=0;
  if(!cr::Get(r,head,callback)||callback<s.image+0x1000||callback>=s.image+0x208d000||!cr::Get(r,head+8,next)||!cr::Get(r,head+0x10,array)||!cr::Get(r,head+0x18,stride)||!cr::Get(r,head+0x20,count))return false;
  if(array==out.array){if(callback!=s.image+0x645940||stride!=0x40||count!=2||++matches!=1)return false;out.descriptor=head;}
  head=next;
 }
 if(matches!=1||!r(out.array,&out.packet[0],sizeof(out.packet[0]))||!r(out.array+0x40,&out.packet[1],sizeof(out.packet[1])))return false;
 return cr::ScopeRefusal(s)==nullptr;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 return cr::Same(a.parent,b.parent)&&a.root==b.root&&a.slab==b.slab&&a.descriptor==b.descriptor&&a.array==b.array&&a.bytes==b.bytes&&a.used==b.used&&std::memcmp(a.packet,b.packet,sizeof(a.packet))==0;
}
template<class Reader>bool ReadPair(Reader& r,const cr::Scope& s,Observation& out) noexcept {out={};Observation a{},b{};if(!ReadOnce(r,s,a)||!ReadOnce(r,s,b)||!Same(a,b))return false;out=a;return true;}
} // namespace mgscoop::carry_request_retirement_read
