#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "carry_dispatch_lifetime_contract.hpp"
namespace mgscoop::carry_dispatch_lifetime {
struct Observation {
 std::uintptr_t image=0,player=0,go=0,registry=0,classNode=0,metadata=0,interface=0,pendingHead=0,pendingNext=0,pendingPrevious=0;
 std::uint64_t pendingCount=0;std::uint32_t metadataFlags=0,mode=0,count=0;
};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& v)noexcept{return Range(p,sizeof(v))&&r(p,&v,sizeof(v));}
template<class R>bool CodeGate(R& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch)noexcept {
 mismatch=0;if(!Range(image,span))return false;unsigned char bytes[8192]{};
 for(const auto& s:carry_dispatch_lifetime_contract::sites){if(s.rva>span||s.size>span-s.rva||s.size>sizeof(bytes)||!r(image+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}}
 for(const auto& s:carry_dispatch_lifetime_contract::slots){std::uintptr_t v=0;if(s.rva>span||sizeof(v)>span-s.rva||!Read(r,image+s.rva,v)||v!=image+s.target){mismatch=s.rva;return false;}}return true;
}
// Directly used installer admission: every actual hook gets a prefix and
// ownership receipt before any native hook is created, including the last one.
template<class H,class P,std::size_t N,class F,class R>
bool InitializeHookReceipts(const H (&hooks)[N],P (&patches)[N],F prefix,R reader)noexcept {
 for(std::size_t n=0;n<N;++n){
  auto& p=patches[n];p.rva=hooks[n].rva;p.wrapper=reinterpret_cast<std::uintptr_t>(hooks[n].wrapper);
  unsigned char current[5]{};
  if(!prefix(p.rva,p.original)||!reader(p.rva,current,sizeof(current))||std::memcmp(current,p.original,sizeof(current)))return false;
 }return true;
}
// The metadata is admitted structurally through the current exact type0
// registry -> currentGO -> final23063F8 owner links. No metadata virtual method
// is called by this reader; this never grants dispatcher invocation permission.
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,Observation& out)noexcept {
 out={};if(!Range(image,0xa01d000)||!Range(player,0x220)||player<0x20||(player&7u))return false;
 out.image=image;out.player=player;out.go=player-0x20;std::uintptr_t v=0,p=0;std::uint32_t flags=0,n=0;
 if(!Read(r,image+0x2a82c30,p)||p!=player||!Read(r,player,v)||v!=image+0x2295490||!Read(r,out.go,v)||v!=image+0x22953d0||
  !Read(r,player+0x204,flags)||!(flags&1u)||(flags&0x100u)||!Read(r,player+0x208,n)||n!=2||!Read(r,player+0x20c,n)||n!=2||
  !Read(r,player+0x214,n)||n||!Read(r,image+0x2c389a0,out.registry)||!Range(out.registry,0x470)||(out.registry&7u)||
  !Read(r,out.registry,v)||v!=image+0x23073c0||!Read(r,out.registry+0x30,out.classNode)||!Range(out.classNode,0x48)||(out.classNode&7u)||
  !Read(r,out.classNode+8,out.metadata)||!Range(out.metadata,0x40)||(out.metadata&7u)||!Read(r,out.metadata+0x20,p)||p!=out.go||
  !Read(r,out.go+8,p)||p!=out.metadata||!Read(r,out.metadata+0x28,out.interface)||!Range(out.interface,0x38)||(out.interface&7u)||
  !Read(r,out.interface,v)||v!=image+0x23063f8||!Read(r,out.interface+0x20,p)||p!=out.go||!Read(r,out.interface+0xc,out.count)||out.count!=2||
  !Read(r,out.interface+0x30,out.mode)||out.mode==2||!Read(r,out.metadata+0x38,out.metadataFlags)||
  !Read(r,out.classNode+0x10,out.pendingHead)||!Range(out.pendingHead,0x10)||(out.pendingHead&7u)||
  !Read(r,out.pendingHead,out.pendingNext)||!Read(r,out.pendingHead+8,out.pendingPrevious)||!Read(r,out.classNode+0x18,out.pendingCount))return false;
 return true;
}
inline bool Identity(const Observation& a,const Observation& b)noexcept{return a.image==b.image&&a.player==b.player&&a.go==b.go&&a.registry==b.registry&&a.classNode==b.classNode&&a.metadata==b.metadata&&a.interface==b.interface&&a.pendingHead==b.pendingHead&&a.count==b.count;}
inline bool Same(const Observation& a,const Observation& b)noexcept{return Identity(a,b)&&a.pendingNext==b.pendingNext&&a.pendingPrevious==b.pendingPrevious&&a.pendingCount==b.pendingCount&&a.metadataFlags==b.metadataFlags&&a.mode==b.mode;}
template<class R>bool ReadPair(R& r,std::uintptr_t image,std::uintptr_t player,Observation& out)noexcept{out={};Observation a{},b{};if(!ReadOnce(r,image,player,a)||!ReadOnce(r,image,player,b)||!Same(a,b))return false;out=a;return true;}
// FullBF9620 first calls BF4480 only for bit4 && !bit8 (BF9653..9662),
// then enters removal only for bit4 && bit1 (BF9685..969A). With a current
// nonnull active metadata/interface and an empty pending sentinel/count,
// both branches can also be skipped when bit8set and bit1clear (healthy3E).
// Forward the original with pre/post current identity; never clear flags,
// skip its native update, or admit a pending replacement.
inline bool NoClassWork(const Observation& o)noexcept{
 const bool currentIdle=!(o.metadataFlags&4u)||((o.metadataFlags&8u)&&!(o.metadataFlags&1u));
 return o.interface&&currentIdle&&!o.pendingCount&&o.pendingHead&&o.pendingNext==o.pendingHead&&o.pendingPrevious==o.pendingHead;
}
enum class Entry:std::uint8_t{interface_ctor,interface_delete,registry_ctor,registry_destruct,registry_delete,registry_replace,class_update,class_register};
inline bool Refuse(const Observation& held,Entry entry,std::uintptr_t object,std::uintptr_t owner=0)noexcept {
 if(!held.interface)return false;
 switch(entry){
  case Entry::interface_ctor:return object==held.interface||owner==held.go;
  case Entry::interface_delete:return object==held.interface;
  case Entry::registry_ctor:case Entry::registry_destruct:case Entry::registry_delete:return object==held.registry;
  case Entry::registry_replace:return true;
  case Entry::class_update:case Entry::class_register:return object==held.classNode;
 }return true;
}
} // namespace mgscoop::carry_dispatch_lifetime
