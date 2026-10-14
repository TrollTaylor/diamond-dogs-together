#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "owned_flag_map_contract.hpp"
namespace mgscoop::owned_flag_map {
inline constexpr std::size_t limit=4096;
struct Observation {
 std::uintptr_t header=0,storage=0;std::uint16_t capacity=0,used=0,free=0xffff;
 unsigned char head[16]{},data[limit*10]{};
};
struct Protected {std::uintptr_t provider=0,slab=0,word=0;};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
inline bool Overlap(std::uintptr_t a,std::size_t n,std::uintptr_t b,std::size_t m)noexcept{return Range(a,n)&&Range(b,m)&&a<b+m&&b<a+n;}
template<class T>T Value(const unsigned char* p)noexcept{T v{};std::memcpy(&v,p,sizeof(v));return v;}
template<class T>void Put(unsigned char* p,T v)noexcept{std::memcpy(p,&v,sizeof(v));}
inline bool Topology(const Observation& o)noexcept{
 if(!o.capacity||o.capacity>limit||o.used>o.capacity||(o.free!=0xffff&&o.free>=o.capacity))return false;
 bool seen[limit]{};unsigned active=0,freeCount=0;
 for(unsigned bucket=0;bucket<o.capacity;++bucket){auto node=Value<std::uint16_t>(o.data+std::size_t(o.capacity)*8+bucket*2);unsigned depth=0;
  while(node!=0xffff){if(node>=o.capacity||depth++>=o.capacity||seen[node])return false;seen[node]=true;++active;
   const auto* item=o.data+node*8;const auto key=Value<std::uint32_t>(item);if(key%o.capacity!=bucket)return false;
   // Native insertion does not replace a duplicate key. Reject duplicates so
   // no ambiguous native chain can be nominated as a safe owned operation.
   auto prior=Value<std::uint16_t>(o.data+std::size_t(o.capacity)*8+bucket*2);
   while(prior!=node){if(prior==0xffff||Value<std::uint32_t>(o.data+prior*8)==key)return false;prior=Value<std::uint16_t>(o.data+prior*8+6);}
   node=Value<std::uint16_t>(item+6);
  }
 }
 if(active!=o.used)return false;auto node=o.free;
 while(node!=0xffff){if(node>=o.capacity||freeCount++>=o.capacity||seen[node])return false;seen[node]=true;
  // Both native leaves consume only free-next+6 before overwriting a free node.
  // Its unused key/value bytes are not semantic input; retain them in the exact postprediction.
  const auto* item=o.data+node*8;node=Value<std::uint16_t>(item+6);}
 return active+freeCount==o.capacity;
}
inline bool Disjoint(const Observation& o,const Protected& p)noexcept{
 if(!Range(p.provider,0x2370)||!Range(p.slab,0x500)||!Range(p.word,2))return false;
 const auto n=std::size_t(o.capacity)*10;
 return !Overlap(o.header,16,p.provider,0x2370)&&!Overlap(o.storage,n,p.provider,0x2370)&&
 !Overlap(o.header,16,p.slab,0x500)&&!Overlap(o.storage,n,p.slab,0x500)&&
 !Overlap(o.header,16,p.word,2)&&!Overlap(o.storage,n,p.word,2);
}
template<class R>bool ReadOnce(R& r,std::uintptr_t address,const Protected& protect,Observation& o)noexcept{
 o={};if(!Range(address,16)||(address&7u)||!r(address,o.head,sizeof(o.head)))return false;o.header=address;
 o.storage=Value<std::uintptr_t>(o.head);o.capacity=Value<std::uint16_t>(o.head+8);o.used=Value<std::uint16_t>(o.head+10);o.free=Value<std::uint16_t>(o.head+12);
 const auto bytes=std::size_t(o.capacity)*10;
 if(!o.capacity||o.capacity>limit||(o.storage&7u)||!Range(o.storage,bytes)||Overlap(o.header,16,o.storage,bytes)||!Disjoint(o,protect)||!r(o.storage,o.data,bytes)||!Topology(o))return false;return true;
}
inline bool Same(const Observation& a,const Observation& b)noexcept{return a.header==b.header&&a.storage==b.storage&&a.capacity==b.capacity&&a.used==b.used&&a.free==b.free&&!std::memcmp(a.head,b.head,sizeof(a.head))&&!std::memcmp(a.data,b.data,std::size_t(a.capacity)*10);}
template<class R>bool ReadPair(R& r,std::uintptr_t address,const Protected& p,Observation& o)noexcept{
 o={};Observation a{},b{};if(!ReadOnce(r,address,p,a)||!ReadOnce(r,address,p,b)||!Same(a,b))return false;o=a;return true;
}
inline bool PredictDelete(const Observation& before,std::uint32_t key,Observation& after,std::uint32_t& result)noexcept{
 after=before;result=0xffff;if(!Topology(before))return false;const auto bucket=std::size_t(before.capacity)*8+(key%before.capacity)*2;
 auto node=Value<std::uint16_t>(before.data+bucket),previous=std::uint16_t{0xffff};
 while(node!=0xffff){const auto off=std::size_t(node)*8;const auto next=Value<std::uint16_t>(before.data+off+6);
  if(Value<std::uint32_t>(before.data+off)==key){result=Value<std::uint16_t>(before.data+off+4);
   Put(after.data+(previous==0xffff?bucket:std::size_t(previous)*8+6),next);
   Put(after.data+off,std::uint32_t{});Put(after.data+off+4,std::uint16_t{0xffff});Put(after.data+off+6,before.free);
   after.used=static_cast<std::uint16_t>(before.used-1);after.free=node;Put(after.head+10,after.used);Put(after.head+12,after.free);return Topology(after);}
  previous=node;node=next;
 }return true;
}
inline bool PredictInsert(const Observation& before,std::uint32_t key,std::uint64_t value,Observation& after,std::uint32_t& result)noexcept{
 after=before;result=0xffff;if(!Topology(before))return false;
 // Native checks full capacity BEFORE its duplicate-key search.
 if(before.used>=before.capacity)return true;
 const auto bucket=std::size_t(before.capacity)*8+(key%before.capacity)*2;auto node=Value<std::uint16_t>(before.data+bucket),previous=std::uint16_t{0xffff};
 while(node!=0xffff){const auto off=std::size_t(node)*8;if(Value<std::uint32_t>(before.data+off)==key){result=Value<std::uint16_t>(before.data+off+4);return true;}previous=node;node=Value<std::uint16_t>(before.data+off+6);}
 if(before.free==0xffff||before.free>=before.capacity)return false;const auto off=std::size_t(before.free)*8;
 after.free=Value<std::uint16_t>(before.data+off+6);after.used=static_cast<std::uint16_t>(before.used+1);
 Put(after.head+10,after.used);Put(after.head+12,after.free);Put(after.data+(previous==0xffff?bucket:std::size_t(previous)*8+6),before.free);
 Put(after.data+off,key);Put(after.data+off+4,static_cast<std::uint16_t>(value));Put(after.data+off+6,std::uint16_t{0xffff});result=static_cast<std::uint16_t>(value);return Topology(after);
}
// Exact source provenance, including both branches of the SET-flags sibling.
enum class Origin : std::uint8_t {none,clear,set};
inline Origin Source(std::uintptr_t image,std::uintptr_t caller,bool insert)noexcept{
 if(!image||image>UINTPTR_MAX-0x85c6c6)return Origin::none;
 if(caller==image+(insert?0x85c6c6u:0x85c6b2u))return Origin::clear;
 if(caller==image+(insert?0x85c579u:0x85c562u))return Origin::set;return Origin::none;
}
inline bool Find(const Observation& o,std::uint32_t key,std::uint16_t& value)noexcept{
 value=0xffff;if(!Topology(o))return false;auto node=Value<std::uint16_t>(o.data+std::size_t(o.capacity)*8+(key%o.capacity)*2);
 while(node!=0xffff){if(Value<std::uint32_t>(o.data+node*8)==key){value=Value<std::uint16_t>(o.data+node*8+4);return true;}node=Value<std::uint16_t>(o.data+node*8+6);}return false;
}
// If the native caller genuinely deleted a non-sentinel value, OR can set
// bits but cannot clear any old bit. The original mask is held in native EBP;
// this exact superset predicate covers every legal16-bit OR mask without
// reading or inventing a register/stack argument.
inline bool SetOperand(std::uint64_t value,std::uint16_t previous)noexcept{return value<=0xffff&&previous!=0xffff&&(static_cast<std::uint16_t>(value)&previous)==previous;}
inline bool PredictSet(const Observation& before,std::uint32_t key,std::uint64_t value,bool insert,
 bool continuedDelete,std::uint16_t previous,Observation& after,std::uint32_t& result)noexcept{
 if(!insert){if(continuedDelete)return false;return PredictDelete(before,key,after,result)&&result!=0xffff&&after.used+1==before.used;}
 if(value>0xffff||!Topology(before))return false;std::uint16_t current=0xffff;const bool present=Find(before,key,current);
 if(continuedDelete){if(present||!SetOperand(value,previous))return false;}
 else if(present&&current!=0xffff)return false; // this exact source must have deleted it first
 if(!PredictInsert(before,key,value,after,result))return false;
 if(present||before.used==before.capacity)return result==0xffff&&Same(before,after);
 return after.used==before.used+1&&result==static_cast<std::uint16_t>(value);
}
// Exact85C620 caller invokes Delete only after a non-FFFF value lookup,
// then Insert after that deletion using a zero-extended16bit masked value.
// The generic native leaves support other cases; this owned branch does not.
inline bool PredictOrigin(const Observation& before,std::uint32_t key,std::uint64_t value,bool insert,Observation& after,std::uint32_t& result)noexcept{
 if(insert){if(value>0xffff||!PredictInsert(before,key,value,after,result))return false;
  return result!=0xffff&&after.used==before.used+1;}
 if(!PredictDelete(before,key,after,result))return false;
 return result!=0xffff&&after.used+1==before.used;
}
template<class R>bool CodeGate(R& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept{
 mismatch=0;unsigned char bytes[512]{};if(!Range(image,span))return false;
 for(const auto& site:owned_flag_map_contract::sites){if(site.rva>span||site.size>span-site.rva||site.size>sizeof(bytes)||!r(image+site.rva,bytes,site.size)||std::memcmp(bytes,site.bytes,site.size)){mismatch=site.rva;return false;}}return true;
}
} // namespace mgscoop::owned_flag_map
