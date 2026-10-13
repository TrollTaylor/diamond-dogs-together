#pragma once
#include <cstddef>
#include <cstdint>
namespace mgscoop::carry_queue_lifetime {
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=UINTPTR_MAX-n;}
inline bool Overlap(std::uintptr_t a,std::size_t an,std::uintptr_t b,std::size_t bn)noexcept{return !Range(a,an)||!Range(b,bn)||(a<b+bn&&b<a+an);}
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& x)noexcept{return Range(p,sizeof(x))&&r(p,&x,sizeof(x));}
struct Descriptor {std::uintptr_t node=0,callback=0,next=0,array=0;std::uint64_t stride=0,count=0;};
struct Owner {std::uintptr_t player=0,data=0,root=0,slab=0,head=0,queued=0,snapshot=0,resource0=0,resource1=0;std::uint64_t bytes=0,used=0;std::uint16_t flags=0,alignment=0;unsigned descriptors=0;Descriptor list[64]{};};
inline bool Same(const Owner& a,const Owner& b)noexcept{
 if(a.player!=b.player||a.data!=b.data||a.root!=b.root||a.slab!=b.slab||a.head!=b.head||a.queued!=b.queued||a.snapshot!=b.snapshot||a.resource0!=b.resource0||a.resource1!=b.resource1||a.bytes!=b.bytes||a.used!=b.used||a.flags!=b.flags||a.alignment!=b.alignment||a.descriptors!=b.descriptors)return false;
 for(unsigned n=0;n<a.descriptors;++n){const auto& x=a.list[n];const auto& y=b.list[n];if(x.node!=y.node||x.callback!=y.callback||x.next!=y.next||x.array!=y.array||x.stride!=y.stride||x.count!=y.count)return false;}return true;
}
inline bool Within(const Owner& o,std::uintptr_t p,std::size_t n)noexcept{return Range(p,n)&&p>=o.slab&&p-o.slab<=o.bytes&&n<=o.bytes-(p-o.slab);}
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,Owner& o)noexcept{
 o={};std::uintptr_t p=0,vt=0;std::uint32_t count=0,flags=0;unsigned queueDescriptors=0;
 if(!Range(image,0xa01d000)||!Range(player,0x220)||!Read(r,image+0x2a82c30,p)||p!=player||!Read(r,player,vt)||vt!=image+0x2295490||
  !Read(r,player+0x204,flags)||!(flags&1u)||(flags&0x100u)||!Read(r,player+0x208,count)||count!=2||!Read(r,player+0x20c,count)||count!=2||
  !Read(r,player+0x214,count)||count||!Read(r,player+0x60,o.data)||o.data<0x20||!Range(o.data-0x20,0x220))return false;
 o.player=player;o.root=o.data-0x20;
 if(!Read(r,o.root,vt)||vt!=image+0x23ad938||!Read(r,o.data,vt)||vt!=image+0x23ad970||
  !Read(r,o.root+0x1e8,o.resource0)||!Range(o.resource0,0x18)||!Read(r,o.root+0x1f0,o.resource1)||!Range(o.resource1,0x18)||
  !Read(r,o.data+0x1e8,o.snapshot)||!Range(o.snapshot,0x44)||!Read(r,o.snapshot+0x40,count)||count!=2||
  !Read(r,o.data+0x1a0,o.slab)||!o.slab||(o.slab&15u)||!Read(r,o.data+0x1b0,o.bytes)||o.bytes<0x80||o.bytes>0x100000||!Range(o.slab,static_cast<std::size_t>(o.bytes))||
  !Read(r,o.data+0x1b8,o.used)||o.used>o.bytes||!Read(r,o.data+0x1c0,o.alignment)||o.alignment<0x10||(o.alignment&(o.alignment-1))||
  !Read(r,o.data+0x1c2,o.flags)||!(o.flags&1u)||!Read(r,o.data+0x1a8,o.head)||!Read(r,o.data+0x1d8,o.queued)||!o.queued||(o.queued&15u)||!Within(o,o.queued,0x80)||
  Overlap(o.root,0x220,o.slab,static_cast<std::size_t>(o.bytes)))return false;
 for(auto node=o.head;node;){
  if(o.descriptors==64||(node&7u)||!Within(o,node,0x28))return false;
  for(unsigned n=0;n<o.descriptors;++n)if(o.list[n].node==node)return false;
  if(Overlap(node,0x28,o.queued,0x80))return false;
  auto& d=o.list[o.descriptors++];d.node=node;
  if(!Read(r,node,d.callback)||d.callback<image+0x1000||d.callback>=image+0x208d000||!Read(r,node+8,d.next)||
   !Read(r,node+0x10,d.array)||!Read(r,node+0x18,d.stride)||!Read(r,node+0x20,d.count)||!d.stride||d.count>0x100000||d.stride>0x100000||
   (d.count&&(!Within(o,d.array,static_cast<std::size_t>(d.stride*d.count))||d.stride*d.count>o.bytes)))return false;
  if(d.count&&d.array!=o.queued&&Overlap(d.array,static_cast<std::size_t>(d.stride*d.count),o.queued,0x80))return false;
  if(d.array==o.queued){if(d.callback!=image+0x645940||d.stride!=0x40||d.count!=2||++queueDescriptors!=1)return false;}
  node=d.next;
 }
 return queueDescriptors==1;
}
template<class R>bool ReadOwner(R& r,std::uintptr_t image,std::uintptr_t player,Owner& o)noexcept{Owner a{},b{};o={};if(!ReadOnce(r,image,player,a)||!ReadOnce(r,image,player,b)||!Same(a,b))return false;o=b;return true;}
inline bool ProtectsObject(const Owner& o,std::uintptr_t p,bool allocator=false)noexcept{return o.root&&p==(allocator?o.root+0x1c0:o.root);}
inline bool ProtectedRange(const Owner& o,std::uintptr_t p,std::size_t n)noexcept{
 return Overlap(p,n,o.root,0x220)||Overlap(p,n,o.slab,static_cast<std::size_t>(o.bytes))||Overlap(p,n,o.snapshot,0x44)||Overlap(p,n,o.resource0,0x18)||Overlap(p,n,o.resource1,0x18);
}
// Generic allocator disposal is admitted only with its complete known footprint
// and pure no-op descriptor callbacks. Unknown callbacks are never forwarded
// merely because the header differs from the retained Q header.
template<class R>bool ForeignFree(R& r,std::uintptr_t image,std::uintptr_t header,const Owner& held)noexcept{
 if(!Range(header,0x28)||ProtectedRange(held,header,0x28))return false;
 std::uintptr_t base=0,head=0;std::uint64_t bytes=0,used=0;std::uint16_t flags=0;
 if(!Read(r,header,base)||!Read(r,header+8,head)||!Read(r,header+0x10,bytes)||!Read(r,header+0x18,used)||!Read(r,header+0x22,flags))return false;
 if(!base)return !head; // exact native path performs header resets only
 if(!bytes||bytes>0x100000||used>bytes||!(flags&1u)||!Range(base,static_cast<std::size_t>(bytes))||
  Overlap(header,0x28,base,static_cast<std::size_t>(bytes))||ProtectedRange(held,base,static_cast<std::size_t>(bytes)))return false;
 std::uintptr_t seen[64]{};unsigned depth=0;
 const auto within=[&](std::uintptr_t p,std::size_t n)noexcept{return Range(p,n)&&p>=base&&p-base<=bytes&&n<=bytes-(p-base);};
 while(head){
  if(depth==64||(head&7u)||!within(head,0x28))return false;for(unsigned n=0;n<depth;++n)if(seen[n]==head)return false;seen[depth++]=head;
  std::uintptr_t callback=0,next=0,array=0;std::uint64_t stride=0,count=0;
  if(!Read(r,head,callback)||!Read(r,head+8,next)||!Read(r,head+0x10,array)||!Read(r,head+0x18,stride)||!Read(r,head+0x20,count)||count>0x100000||stride>0x100000)return false;
  if(count&&(callback!=image+0x645940||!stride||stride*count>bytes||!within(array,static_cast<std::size_t>(stride*count))))return false;
  head=next;
 }
 return true;
}
} // namespace mgscoop::carry_queue_lifetime
