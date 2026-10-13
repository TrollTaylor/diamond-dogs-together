#pragma once
#include "coop_passenger.hpp"
#include "native_pose_carry_participants.hpp"
#include "vehicle_controller_read.hpp"
namespace mgscoop::vehicle_lifetime_read {
namespace pd=coop_passenger::detail;
namespace cp=native_pose_carry_participants;
namespace vc=vehicle_controller_read;
struct UI {
 std::uintptr_t player=0,interface=0,primary=0,manager=0,dispatcher=0,dispatcherVT=0,provider=0,providerVT=0,getter=0;
};
template<class Reader> bool ReadUI(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t player,
 const pd::EngineRead& engine,UI& out) noexcept {
 out={};out.player=player;std::uintptr_t vt=0,owner=0,other=0,slot=0;
 if(!pd::Get(r,player+0xd8,out.interface)||out.interface<0x20||!pd::Range(out.interface,0x50))return false;
 out.primary=out.interface-0x20;
 return pd::Get(r,out.interface,vt)&&vt==image+0x23af700&&
  pd::Get(r,out.primary,vt)&&vt==image+0x23af6d0&&pd::Get(r,out.interface+0x28,owner)&&owner==player&&
  pd::Get(r,image+0x23af700+0x190,slot)&&slot==image+0xfe71b0&&
  pd::Get(r,engine.world+0x98,out.manager)&&pd::Range(out.manager,0x48)&&
  pd::Get(r,out.manager+0x40,out.dispatcher)&&pd::Range(out.dispatcher,0x28)&&
  pd::Get(r,out.dispatcher,out.dispatcherVT)&&pd::InImage(out.dispatcherVT,image,span,8)&&
  pd::Get(r,out.dispatcher+0x20,out.provider)&&pd::Range(out.provider,8)&&
  pd::Get(r,out.interface+0x48,other)&&other==out.provider&&
  pd::Get(r,out.provider,out.providerVT)&&pd::InImage(out.providerVT,image,span,8)&&
  pd::Get(r,out.providerVT,out.getter)&&pd::InImage(out.getter,image,span,1);
}
inline bool Same(const UI& a,const UI& b) noexcept {
 return a.player==b.player&&a.interface==b.interface&&a.primary==b.primary&&a.manager==b.manager&&
  a.dispatcher==b.dispatcher&&a.dispatcherVT==b.dispatcherVT&&a.provider==b.provider&&a.providerVT==b.providerVT&&a.getter==b.getter;
}
struct CallbackCensus {unsigned present[2]{},eventMask[2]{},free[2]{},count[2]{};bool retired[2]{true,true};};
inline bool Callbacks(std::uintptr_t image,const cp::Pair& models,const vc::Observation& vehicle,CallbackCensus& out) noexcept {
 out={};out.retired[0]=out.retired[1]=true;
 if(models.player!=vehicle.player||models.data!=vehicle.data||models.scriptState!=vehicle.state)return false;
 for(unsigned id=0;id<2;++id){const auto& model=models.body[id];const auto& control=vehicle.body[id];
  if(model.id!=id||model.block!=control.block||!control.primary||model.callbackCount<2)return false;
  out.free[id]=model.freeCallbacks;out.count[id]=model.callbackCount;
  for(unsigned n=0;n<model.callbackCount;++n){const auto& cb=model.callbacks[n];
   if(cb.owner==vehicle.body[1-id].primary)return false;
   if(cb.owner!=control.primary){if(cb.function==image+0x124a530)return false;continue;}
   if(cb.function!=image+0x124a530||cb.body!=id||cb.refs<1)return false;
   const auto key=cb.event&0xffffffffffffULL;
   const unsigned bit=key==0x62bcc8f4fcb6ULL?1u:key==0x73cf7bf9301dULL?2u:0u;
   if(!bit||(out.eventMask[id]&bit))return false;
   ++out.present[id];out.eventMask[id]|=bit;out.retired[id]=false;
  }
 }
 return true;
}
// Original vehicle registration/removal matches the low48 event bits in
// B6E550/B6FB80. An actual retirement checks fresh callback slots, rather than
// assuming phase0 or the original exit function's boolean means they retired.
inline bool Retired(const CallbackCensus& c,unsigned body) noexcept {return body<2&&c.retired[body]&&!c.present[body]&&!c.eventMask[body];}
struct Allocation {
 std::uintptr_t object=0,primary=0,slab=0,head=0,records=0,recordHeader=0,bitmap=0,bitmapHolder=0;
 std::uint64_t bytes=0,used=0;std::uint16_t alignment=0,flags=0,count=0;std::uint32_t tag=0;
 unsigned descriptors=0,recordMatches=0;bool within=false;
};
template<class Reader> bool ReadAllocation(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t service,Allocation& out) noexcept {
 out={};std::uintptr_t vt=0,slot=0,other=0;
 if(!pd::Get(r,service,vt)||vt!=image+0x22ac8e0||!pd::Get(r,service+0xb0,out.object)||out.object<0x20)return false;
 out.primary=out.object-0x20;
 if(!pd::Get(r,out.object,vt)||vt!=image+0x22ed9f0||!pd::Get(r,vt,slot)||slot!=image+0xb2dd30||
  !pd::Get(r,out.primary,vt)||vt!=image+0x22ed930||
  !pd::Get(r,out.primary+0x1e0,out.count)||!out.count||out.count>512||
  !pd::Get(r,out.primary+0x30,out.records)||!pd::Range(out.records,std::size_t(out.count)*0x80)||
  !pd::Get(r,out.primary+0x48,out.bitmap)||!pd::Range(out.bitmap,((out.count+31u)/32u)*4u)||
  !pd::Get(r,out.primary+0x1c8,out.bitmapHolder)||!pd::Range(out.bitmapHolder,16)||
  !pd::Get(r,out.bitmapHolder+8,other)||other!=out.bitmap||
  !pd::Get(r,out.primary+0x60,out.slab)||!out.slab||
  !pd::Get(r,out.primary+0x68,out.head)||!out.head||
  !pd::Get(r,out.primary+0x70,out.bytes)||!out.bytes||out.bytes>0x1000000||
  !pd::Get(r,out.primary+0x78,out.used)||out.used>out.bytes||
  !pd::Get(r,out.primary+0x80,out.alignment)||out.alignment<0x80||(out.alignment&(out.alignment-1u))||
  (out.slab&(out.alignment-1u))||!pd::Get(r,out.primary+0x82,out.flags)||!(out.flags&1u)||
  !pd::Get(r,out.primary+0x84,out.tag)||out.tag!=0x3003d||!pd::Range(out.slab,static_cast<std::size_t>(out.bytes)))return false;
 const auto within=[&](std::uintptr_t p,std::uint64_t size) noexcept {
  return pd::Range(p,static_cast<std::size_t>(size))&&p>=out.slab&&p-out.slab<=out.bytes&&size<=out.bytes-(p-out.slab);
 };
 out.within=within(out.records,std::uint64_t(out.count)*0x80);if(!out.within)return false;
 std::uintptr_t seen[128]{},at=out.head;
 while(at){
  if(out.descriptors==128||(at&7u)||!within(at,0x28))return false;
  for(unsigned n=0;n<out.descriptors;++n)if(seen[n]==at)return false;
  seen[out.descriptors++]=at;std::uintptr_t callback=0,next=0,array=0;std::uint64_t stride=0,count=0;
  if(!pd::Get(r,at,callback)||!pd::InImage(callback,image,span,1)||!pd::Get(r,at+8,next)||
   !pd::Get(r,at+0x10,array)||!pd::Get(r,at+0x18,stride)||!pd::Get(r,at+0x20,count))return false;
  if(array==out.records){
   if(callback!=image+0x645940||stride!=0x80||count!=out.count||++out.recordMatches!=1)return false;
   out.recordHeader=at;
  }
  at=next;
 }
 return out.recordMatches==1;
}
inline bool Same(const Allocation& a,const Allocation& b) noexcept {
 return a.object==b.object&&a.primary==b.primary&&a.slab==b.slab&&a.head==b.head&&a.records==b.records&&
  a.recordHeader==b.recordHeader&&a.bitmap==b.bitmap&&a.bitmapHolder==b.bitmapHolder&&a.bytes==b.bytes&&a.used==b.used&&
  a.alignment==b.alignment&&a.flags==b.flags&&a.count==b.count&&a.tag==b.tag&&a.descriptors==b.descriptors&&a.recordMatches==b.recordMatches&&a.within==b.within;
}
}
