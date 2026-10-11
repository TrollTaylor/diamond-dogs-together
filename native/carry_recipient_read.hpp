#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#ifndef MGS_CARRY_PARENT_READER_HEADER
#define MGS_CARRY_PARENT_READER_HEADER "carry_plugin_read.hpp"
#endif
#include MGS_CARRY_PARENT_READER_HEADER
#include "carry_owned_handshake_contract.hpp"
namespace mgscoop::carry_recipient_read {
namespace cr=carry_plugin_read;
struct Recipient {
 std::uintptr_t node=0,phase=0,primary=0,records=0,record=0,descriptor=0,carrierRecord=0,resource=0,helper=0;
 std::uint16_t carrier=0xffff;
 std::uint8_t current=0,next=0,sequence=0,requestSequence=0,offlineOwned=0;
 std::uint32_t flags=0;
 std::uint64_t clip=0;
 bool active=false,endPending=false;
};
struct Observation { cr::Observation carrier{};Recipient recipient[2]; };
// Exact full-code proof pins the generic builder and recipient allocation,
// constructor, node binder, message receiver and natural carry/drop handlers.
// This is read-only; neither readiness nor a coherent binding proves attachment.
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;if(!cr::Range(image,span))return false;unsigned char buffer[8192]{};
 for(const auto& p:carry_owned_handshake_contract::sites){
  if(p.rva>span||p.size>span-p.rva||p.size>sizeof(buffer)||!r(image+p.rva,buffer,p.size)){mismatch=p.rva;return false;}
  for(std::size_t j=0;j<p.size;++j)if(buffer[j]!=p.bytes[j]){mismatch=p.rva;return false;}
 }
 for(const auto& p:carry_owned_handshake_contract::slots){
  std::uintptr_t v=0;
  if(p.rva>span||sizeof(v)>span-p.rva||!cr::Get(r,image+p.rva,v)||v!=image+p.target){mismatch=p.rva;return false;}
 }return true;
}
template<class Reader> bool ReadOnce(Reader& r,const cr::Scope& s,Observation& out) noexcept {
 out={};if(!cr::ReadOnce(r,s,out.carrier))return false;
 for(unsigned id=0;id<2;++id){auto& v=out.recipient[id];const auto block=out.carrier.body[id].block;
  std::uintptr_t vt=0,p=0,owner=0,bitmap=0;std::uint32_t n=0,base=0,bits=0;std::uint64_t stride=0,count=0;
  if(!cr::Get(r,block+0x210,v.node)||!cr::Aligned(v.node,8)||!cr::Range(v.node,0x40)||!cr::Get(r,v.node,vt)||vt!=s.image+0x2306628||!cr::Get(r,v.node+0x20,n)||n!=1||!cr::Get(r,v.node+0x24,base)||base!=id||!cr::Get(r,v.node+0x2c,n)||n!=0x10||!cr::Get(r,v.node+0x38,v.phase)||!cr::Aligned(v.phase,8)||v.phase<0x30||!cr::Get(r,block+0x438,p)||p!=v.phase)return false;
  v.primary=v.phase-0x30;
  if(!cr::Range(v.primary,0x90)||!cr::Get(r,v.primary,vt)||vt!=s.image+0x23b8d78||!cr::Get(r,v.primary+0x10,vt)||vt!=s.image+0x23b8da0||!cr::Get(r,v.phase,vt)||vt!=s.image+0x23b8dd0||!cr::Get(r,v.primary+0x48,vt)||vt!=s.image+0x23b8e10||!cr::Get(r,v.primary+8,owner)||owner!=block||!cr::Get(r,v.primary+0x38,p)||p!=v.node||!cr::Get(r,v.primary+0x88,v.records)||!cr::Aligned(v.records,16)||!cr::Range(v.records,0x60))return false;
  // The current allocation's linked descriptor is produced by1125160. It
  // carries the exact array address, stride and count for this one-body block.
  if(!cr::Get(r,v.primary+0x58,v.descriptor)||!cr::Aligned(v.descriptor,8)||!cr::Range(v.descriptor,0x28)||!cr::Get(r,v.descriptor,p)||p!=s.image+0x645940||!cr::Get(r,v.descriptor+0x10,p)||p!=v.records||!cr::Get(r,v.descriptor+0x18,stride)||stride!=0x60||!cr::Get(r,v.descriptor+0x20,count)||count!=1)return false;
  v.record=v.records;
  if(!cr::Get(r,v.node+8,bitmap)||!cr::Aligned(bitmap,4)||!cr::Get(r,bitmap,bits))return false;v.active=(bits&1u)!=0;
  if(!cr::Get(r,v.node+0x10,bitmap)||!cr::Aligned(bitmap,4)||!cr::Get(r,bitmap,bits))return false;v.endPending=(bits&1u)!=0;
  if(!cr::Get(r,v.record+0x10,v.current)||!cr::Get(r,v.record+0x11,v.next)||!cr::Get(r,v.record+0x12,v.sequence)||!cr::Get(r,v.record+0x14,v.requestSequence)||!cr::Get(r,v.record+0x20,v.resource)||!cr::Get(r,v.record+0x28,v.carrierRecord)||!cr::Get(r,v.record+0x30,v.carrier)||!cr::Get(r,v.record+0x50,v.clip)||!cr::Get(r,v.record+0x58,v.offlineOwned)||!cr::Get(r,v.record+0x5c,v.flags)||!cr::Get(r,v.primary+0x80,v.helper))return false;
 }
 const auto& a=out.recipient[0];const auto& b=out.recipient[1];
 return a.node!=b.node&&a.primary!=b.primary&&a.records!=b.records&&a.descriptor!=b.descriptor;
}
inline bool SameRecipient(const Recipient& a,const Recipient& b) noexcept {
 return a.node==b.node&&a.phase==b.phase&&a.primary==b.primary&&a.records==b.records&&a.record==b.record&&a.descriptor==b.descriptor&&a.carrierRecord==b.carrierRecord&&a.resource==b.resource&&a.helper==b.helper&&a.carrier==b.carrier&&a.current==b.current&&a.next==b.next&&a.sequence==b.sequence&&a.requestSequence==b.requestSequence&&a.offlineOwned==b.offlineOwned&&a.flags==b.flags&&a.clip==b.clip&&a.active==b.active&&a.endPending==b.endPending;
}
template<class Reader> bool ReadPair(Reader& r,const cr::Scope& s,Observation& out) noexcept {
 out={};Observation a{},b{};if(!ReadOnce(r,s,a)||!ReadOnce(r,s,b)||!cr::Same(a.carrier,b.carrier))return false;
 for(unsigned id=0;id<2;++id)if(!SameRecipient(a.recipient[id],b.recipient[id]))return false;
 out=a;return true;
}
inline bool CoherentBinding(const Observation& a,unsigned carried,unsigned carrier) noexcept {
 if(carried>=2||carrier>=2||carried==carrier)return false;
 const auto& t=a.recipient[carried];const auto& c=a.carrier.body[carrier];
 return t.active&&!t.endPending&&t.carrierRecord==c.record&&t.carrier==carrier&&c.nodeActive&&c.previous==carried&&(c.psFlags&(1u<<22))!=0;
}
} // namespace mgscoop::carry_recipient_read

