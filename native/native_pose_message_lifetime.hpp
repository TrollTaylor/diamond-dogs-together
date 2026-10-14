#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "native_pose_message_lifetime_contract.hpp"
#include "carry_message_provider_contract.hpp"
namespace mgscoop::native_pose_message_lifetime {
struct Observation {
 std::uintptr_t player=0,provider=0,interface=0,slab=0;
 std::uint32_t index=0,key=0,flags=0;std::uint16_t used=0,freeHead=0,keyEntry=0xffff;
 std::uint16_t arenaFlags=0;std::uint8_t enabled=0;
 unsigned char keyData[0x280]{};
};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
template<class R,class T>bool At(R& r,std::uintptr_t p,T& v)noexcept{return Range(p,sizeof(v))&&r(p,&v,sizeof(v));}
template<class T>T Value(const unsigned char* bytes)noexcept{T v{};std::memcpy(&v,bytes,sizeof(v));return v;}
template<class R>bool CodeGate(R& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept{
 mismatch=0;if(!Range(image,span))return false;unsigned char bytes[4096]{};
 const auto check=[&](const auto& sites)noexcept {for(const auto& s:sites){if(s.rva>span||s.size>span-s.rva){mismatch=s.rva;return false;}
  for(std::size_t off=0;off<s.size;){const auto n=s.size-off<sizeof(bytes)?s.size-off:sizeof(bytes);
   if(!r(image+s.rva+off,bytes,n)){mismatch=s.rva+off;return false;}
   for(std::size_t j=0;j<n;++j)if(bytes[j]!=s.bytes[off+j]){mismatch=s.rva+off+j;return false;}off+=n;}}
  return true;};
 if(!check(native_pose_message_lifetime_contract::sites)||!check(carry_message_provider_contract::sites))return false;
 const auto slots=[&](const auto& ss)noexcept {for(const auto& s:ss){std::uintptr_t v=0;if(s.rva>span||sizeof(v)>span-s.rva||s.target>=span||!At(r,image+s.rva,v)||v!=image+s.target){mismatch=s.rva;return false;}}return true;};
 if(!slots(native_pose_message_lifetime_contract::slots)||!slots(carry_message_provider_contract::slots))return false;
 for(const auto& s:carry_message_provider_contract::constants){std::uint32_t v=0;if(s.rva>span||sizeof(v)>span-s.rva||!At(r,image+s.rva,v)||v!=s.value){mismatch=s.rva;return false;}}return true;
}
// This reader never supplies native invocation/lifetime permission. The actual
// carry lease must retain P, provider, interface, allocation and map mutation
// domain before a null listener can qualify the key-only End notification.
inline bool Map(const Observation& o,std::uint16_t& result)noexcept {
 result=0xffff;if(!o.used||o.used>32||(o.freeHead!=0xffff&&o.freeHead>=64)||o.index>=32)return false;
 bool seen[64]{};bool values[32]{};unsigned active=0,matches=0;std::uint16_t match=0xffff;
 for(unsigned bucket=0;bucket<64;++bucket){auto node=Value<std::uint16_t>(o.keyData+0x200+bucket*2);unsigned depth=0;
  while(node!=0xffff){if(node>=64||depth++==64||seen[node])return false;seen[node]=true;
   const auto* entry=o.keyData+node*8;const auto key=Value<std::uint32_t>(entry);const auto value=Value<std::uint16_t>(entry+4);
   if(key%64!=bucket||value>=32||values[value])return false;values[value]=true;++active;
   if(key==o.key){if(value!=o.index||++matches!=1)return false;match=node;}
   node=Value<std::uint16_t>(entry+6);
  }
 }
 if(active!=o.used||matches!=1)return false;
 auto node=o.freeHead;unsigned available=0;
 while(node!=0xffff){if(node>=64||available++==64||seen[node])return false;seen[node]=true;
  const auto* entry=o.keyData+node*8;
  if(Value<std::uint32_t>(entry)||Value<std::uint16_t>(entry+4)!=0xffff)return false;
  node=Value<std::uint16_t>(entry+6);
 }
 if(active+available!=64)return false;result=match;return true;
}
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,Observation& o)noexcept {
 o={};if(!Range(image,0xa01d000)||image%0x1000||!Range(player,0x388))return false;o.player=player;
 std::uintptr_t p=0,vt=0,head=0,second=0;std::uint64_t size=0,used=0;std::uint32_t n=0,flags=0;std::uint16_t capacity=0;
 if(!At(r,image+0x2a82c30,p)||p!=player||!At(r,player,vt)||vt!=image+0x2295490||!At(r,player+0x204,flags)||!(flags&1)||(flags&0x100)||
  !At(r,player+0xa8,p)||p||!At(r,player+0x208,n)||n!=2||!At(r,player+0x20c,n)||n!=2||!At(r,player+0x214,n)||n||
  !At(r,image+0x2b72c00,o.provider)||!Range(o.provider,0x2370)||(o.provider&7)||!At(r,o.provider,vt)||vt!=image+0x20e9b90||
  !At(r,player+0x148,o.interface)||!At(r,o.interface,vt)||vt!=image+0x20e9b60||!At(r,o.interface+8,p)||p!=o.provider)return false;
 const auto pool=o.provider+0x1c40;if(o.interface<pool||o.interface-pool>=32u*0x28u||(o.interface-pool)%0x28u)return false;
 o.index=static_cast<std::uint32_t>((o.interface-pool)/0x28u);
 if(!At(r,o.interface+0x10,p)||p||!At(r,o.interface+0x18,o.key)||o.key!=0xb8071b05u||!At(r,o.interface+0x20,o.flags)||!(o.flags&1)||
  !At(r,image+0x2b72c08,o.enabled)||o.enabled!=1||!At(r,o.provider+8,o.slab)||!Range(o.slab,0x500)||(o.slab&3)||
  !At(r,o.provider+0x10,head)||head||!At(r,o.provider+0x18,size)||size!=0x500||!At(r,o.provider+0x20,used)||used!=0x500||
  !At(r,o.provider+0x2a,o.arenaFlags)||!(o.arenaFlags&1)||!At(r,o.provider+0x2340,p)||p!=o.slab||
  !At(r,o.provider+0x2360,p)||p!=o.slab||!At(r,o.provider+0x2368,second)||second!=o.slab+0x280||
  !At(r,o.provider+0x2350,p)||p!=second||!At(r,o.provider+0x2348,capacity)||capacity!=64||
  !At(r,o.provider+0x234a,o.used)||!At(r,o.provider+0x234c,o.freeHead)||!r(o.slab,o.keyData,sizeof(o.keyData))||!Map(o,o.keyEntry))return false;
 return true;
}
inline bool SameLease(const Observation& a,const Observation& b)noexcept{return a.player==b.player&&a.provider==b.provider&&a.interface==b.interface&&a.slab==b.slab&&a.index==b.index&&a.key==b.key&&a.flags==b.flags&&a.keyEntry==b.keyEntry&&a.arenaFlags==b.arenaFlags&&a.enabled==b.enabled;}
inline bool Same(const Observation& a,const Observation& b)noexcept{return SameLease(a,b)&&a.used==b.used&&a.freeHead==b.freeHead&&!std::memcmp(a.keyData,b.keyData,sizeof(a.keyData));}
template<class R>bool ReadPair(R& r,std::uintptr_t image,std::uintptr_t player,Observation& o)noexcept {o={};Observation a{},b{};if(!ReadOnce(r,image,player,a)||!ReadOnce(r,image,player,b)||!Same(a,b))return false;o=a;return true;}
inline bool Overlap(std::uintptr_t at,std::size_t n,std::uintptr_t p,std::size_t size)noexcept{return Range(at,n)&&Range(p,size)&&at<p+size&&p<at+n;}
inline bool ProtectsArena(const Observation& o,std::uintptr_t arena)noexcept {
 return Overlap(arena,0x28,o.provider,0x2370)||Overlap(arena,0x28,o.slab,0x500);
}
inline bool ProtectsInterface(const Observation& o,std::uintptr_t object)noexcept {
 return Overlap(object,0x28,o.provider,0x2370)||Overlap(object,0x28,o.slab,0x500);
}
inline bool ProtectsSetter(const Observation& o,std::uintptr_t object)noexcept{return object<=UINTPTR_MAX-0x10&&(Overlap(object+0x10,8,o.provider,0x2370)||Overlap(object+0x10,8,o.slab,0x500));}
} // namespace mgscoop::native_pose_message_lifetime
