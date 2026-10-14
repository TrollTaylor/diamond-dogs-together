#pragma once
#include "qualified_sleep_lookup_observer.hpp"
namespace mgscoop::qualified_sleep_lookup_observer {
inline bool Relative(std::uint32_t field,std::int32_t displacement,std::size_t n,std::uint32_t& out) noexcept {
 const auto sum=std::int64_t(field)+displacement;if(sum<0||sum>0x400000||n>0x400000-std::size_t(sum))return false;out=static_cast<std::uint32_t>(sum);return true;
}
// Current typed Sleep/graph facts only. This never calls the native getter,
// acquires gameplay locks, manufactures generation/owner-thread, or grants
// a standing episode. Missing/replaced graph remains graphObserved=false.
struct NativePhaseReader {
 template<class R> void operator()(R& r,std::uintptr_t image,const Owner& owner,Phase& out) const noexcept {
  out={};if(!owner.known)return;Phase p{};std::uintptr_t vt=0,parent=0,iface=0,bitmap=0,actionBytes=0,lives=0,actions=0;
  std::uint32_t base=0,count=0,bits=0;std::int32_t adjustment=0;
  if(!Get(r,owner.record+0x78,p.ps)||!Aligned(p.ps,8)||!Get(r,p.ps,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||
   !Get(r,p.ps+8,base)||base!=owner.body||!Get(r,p.ps+0x408,parent)||parent!=owner.record||
   !Get(r,p.ps+0xd8,iface)||!Aligned(iface,8)||!Get(r,iface,vt))return;
  std::uintptr_t offset=UINTPTR_MAX;
  if(vt==image+0x23d5c18)offset=0;else if(vt==image+0x23d5c40)offset=0x10;else if(vt==image+0x23d5c70)offset=0x30;else if(vt==image+0x23d5cb0)offset=0x48;
  if(offset==UINTPTR_MAX||iface<offset)return;p.primary=iface-offset;
  if(!Aligned(p.primary,8)||!Get(r,p.primary,vt)||vt!=image+0x23d5c18||!Get(r,p.primary+0x10,vt)||vt!=image+0x23d5c40||
   !Get(r,p.primary+0x30,vt)||vt!=image+0x23d5c70||!Get(r,p.primary+0x48,vt)||vt!=image+0x23d5cb0||
   !Get(r,p.primary+8,parent)||parent!=owner.record||!Get(r,p.primary+0x78,p.records)||!Aligned(p.records,16)||
   !Get(r,p.ps+0x500,p.node)||!Aligned(p.node,8)||!Get(r,p.node,vt)||vt!=image+0x2306628||
   !Get(r,p.node+0x20,count)||count!=1||!Get(r,p.node+0x24,base)||base!=owner.body||
   !Get(r,p.primary+0x38,parent)||parent!=p.node||!Get(r,p.node+0x38,parent)||parent!=p.primary+0x30||
   !Get(r,p.node+8,bitmap)||!Aligned(bitmap,4)||!Get(r,bitmap,bits)||
   !Get(r,p.records,p.callback)||!Get(r,p.records+8,adjustment)||adjustment||!Get(r,p.records+0x10,p.mode)||p.mode>6||
   !Get(r,p.records+0x11,p.request)||p.request>6||!Get(r,p.records+0x12,p.counter)||!Get(r,p.records+0x14,p.flags)||
   !Get(r,p.records+0x18,p.rememberedGraph)||!Get(r,p.records+0x30,p.recordFlags))return;
  constexpr std::uintptr_t callbacks[]={0x645940,0x12ca3b0,0x12c9a80,0x12ca040,0x12cb2b0,0x12caa10,0x12caf50};
  if(p.callback!=image+callbacks[p.mode]&&(p.mode||p.callback))return;
  if(!Get(r,p.ps+0x2d8,actionBytes)||!actionBytes||!Get(r,actionBytes,p.action)||
   !Get(r,owner.data+8,lives)||!Aligned(lives,4)||!Get(r,owner.data+0x18,actions)||!Aligned(actions,2)||
   !Get(r,lives+std::uintptr_t(owner.body)*0x4c+0x34,p.life)||p.life>2||!Get(r,actions+std::uintptr_t(owner.body)*2,p.qAction))return;
  p.nodeActive=(bits&1u)!=0;p.rememberedGraph&=0xffffffffffffull;p.observed=true;out=p;
  std::uintptr_t cs=0,vector=0,lifetime=0,node=0;std::uint32_t layer=0;std::uint8_t reload=0;
  if(!Get(r,p.ps+0x238,cs)||cs!=owner.cs||!Get(r,owner.rows+0x90,vector)||!Aligned(vector,8)||
   !Get(r,vector,p.controller)||!Aligned(p.controller,8)||!Get(r,p.controller,vt)||vt!=image+0x250ba50||
   !Get(r,p.controller+0x10,vt)||vt!=image+0x250bc00||!Get(r,p.controller+0xd4,layer)||layer||
   !Get(r,p.controller+0x185,reload)||reload||!Get(r,p.controller+0xf0,lifetime)||!lifetime||
   !Get(r,p.controller+0x1b8,p.mog)||!Aligned(p.mog,8)||!Get(r,p.controller+0x1a0,node))return;
  constexpr unsigned char expected[]={'F','O','X','M','O','T','I','O','N','G','R','A','P','H',0};unsigned char magic[15]{};
  std::uint32_t version=0,layers=0,header=0,table=0,nodes=0,hash=0;std::int32_t rel=0;
  if(!r(p.mog,magic,sizeof(magic))||std::memcmp(magic,expected,sizeof(magic))||!Get(r,p.mog+0x10,version)||version!=6||
   !Get(r,p.mog+0x1c,layers)||!layers||layers>32||!Get(r,p.mog+0x20,rel)||!Relative(0x1c,rel,0x38,header)||
   !Get(r,p.mog+header+0xc,nodes)||!nodes||nodes>16384||!Get(r,p.mog+header+0x10,rel)||!Relative(header+0x10,rel,std::size_t(nodes)*0x48,table)||!node||node<p.mog)return;
  const auto at=node-p.mog;
  if(at<table||at-table>=std::size_t(nodes)*0x48||(at-table)%0x48||!Get(r,node+0x18,rel)||
   !Relative(static_cast<std::uint32_t>(at)+0x18,rel,8,hash)||!Get(r,p.mog+hash,p.currentGraph))return;
  p.currentGraph&=0xffffffffffffull;p.graphNode=node;p.graphObserved=true;out=p;
 }
};
} // namespace mgscoop::qualified_sleep_lookup_observer
