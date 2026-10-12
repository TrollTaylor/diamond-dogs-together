#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>
namespace mgscoop::native_pose_carry_participants {
struct Callback {std::uintptr_t function=0,owner=0;std::uint64_t event=0;std::int32_t refs=0;std::uint32_t body=0;};
struct Body {
 std::uint32_t id=0,pool=0;std::uintptr_t block=0,state=0,cs=0,entry=0;
 std::uintptr_t carrier=0,carrierNode=0,carrierRecords=0,recipient=0,recipientNode=0,recipientRecords=0;
 std::uintptr_t modelAllocation=0,callbackSlab=0;std::uint8_t callbackCount=0,treeCount=0;Callback callbacks[255]{};unsigned freeCallbacks=0;
};
struct Pair {std::uintptr_t player=0,go=0,data=0,scriptState=0,vars=0;Body body[2];};
inline bool Range(std::uintptr_t p,std::size_t n)noexcept{return p&&n&&p<=std::numeric_limits<std::uintptr_t>::max()-n;}
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& v)noexcept{return Range(p,sizeof(v))&&r(p,&v,sizeof(v));}
inline bool SameBody(const Body& a,const Body& b)noexcept{return a.id==b.id&&a.pool==b.pool&&a.block==b.block&&a.state==b.state&&a.cs==b.cs&&a.entry==b.entry&&a.carrier==b.carrier&&a.carrierNode==b.carrierNode&&a.carrierRecords==b.carrierRecords&&a.recipient==b.recipient&&a.recipientNode==b.recipientNode&&a.recipientRecords==b.recipientRecords&&a.modelAllocation==b.modelAllocation&&a.callbackSlab==b.callbackSlab&&a.callbackCount==b.callbackCount&&a.treeCount==b.treeCount;}
inline bool CoherentCallbacks(const Body& a,const Body& b)noexcept {
 if(a.callbackCount!=b.callbackCount||a.freeCallbacks!=b.freeCallbacks)return false;
 for(unsigned i=0;i<a.callbackCount;++i){const auto& x=a.callbacks[i];const auto& y=b.callbacks[i];
  if(x.function!=y.function||x.owner!=y.owner||x.event!=y.event||x.refs!=y.refs||x.body!=y.body)return false;
 }return true;
}
inline bool Same(const Pair& a,const Pair& b)noexcept{return a.player==b.player&&a.go==b.go&&a.data==b.data&&a.scriptState==b.scriptState&&a.vars==b.vars&&SameBody(a.body[0],b.body[0])&&SameBody(a.body[1],b.body[1]);}
// Typed owner reader only. Runtime caller must separately own the actual writer,
// exact code/creator receipts and paired current generation before retaining it.
template<class R>bool ReadModel(R& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t block,unsigned id,Body& b)noexcept{
 b={};if(id>1||!image||!player||!block)return false;b.id=id;b.block=block;
 std::uintptr_t vt=0,p=0;std::uint32_t count=0,base=0;
 if(!Read(r,block,vt)||vt!=image+0x2295210||!Read(r,block+8,count)||count!=1||!Read(r,block+0xc,base)||base!=id||
  !Read(r,block+0x138,p)||p!=player||!Read(r,block+0x78,b.state)||!b.state||!Read(r,b.state,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||
  !Read(r,b.state+0x408,p)||p!=block||!Read(r,b.state+8,base)||base!=id||
  !Read(r,block+0x10,b.cs)||!b.cs||!Read(r,b.state+0x238,p)||p!=b.cs||!Read(r,b.cs,vt)||vt!=image+0x22e6070||
  !Read(r,b.cs+8,count)||count!=1||!Read(r,b.cs+0xc,base)||base!=id||!Read(r,b.cs+0x10,b.entry)||!b.entry||(b.entry&15u)||!Range(b.entry,0xc0))return false;
 if(!Read(r,b.entry+0x78,b.modelAllocation)||!b.modelAllocation||(b.modelAllocation&15u)||!Read(r,b.entry+0x88,b.callbackSlab)||
  !Read(r,b.entry+0xb9,b.treeCount)||!Read(r,b.entry+0xba,b.callbackCount)||b.callbackCount<2||
  !Range(b.modelAllocation,std::size_t(b.treeCount)*0xd0+std::size_t(b.callbackCount)*0x60)||b.callbackSlab!=b.modelAllocation+std::size_t(b.treeCount)*0xd0)return false;
 for(unsigned n=0;n<b.callbackCount;++n){auto& cb=b.callbacks[n];const auto slot=b.callbackSlab+std::uintptr_t(n)*0x60;
  if(!Read(r,slot,vt)||vt!=image+0x22f6380||!Read(r,slot+8,cb.refs)||cb.refs<1||
     !Read(r,slot+0x40,cb.function)||!Read(r,slot+0x48,cb.owner)||!Read(r,slot+0x50,cb.event)||!Read(r,slot+0x58,cb.body))return false;
  if(!cb.function){if(cb.owner||cb.event||cb.body)return false;++b.freeCallbacks;}
 }
 return true;
}
template<class R>bool Node(R& r,std::uintptr_t image,std::uintptr_t block,unsigned id,unsigned row,std::uintptr_t primary,std::uintptr_t& node)noexcept{
 std::uintptr_t vt=0,p=0;std::uint32_t one=0,base=0,index=0;
 return Read(r,block+0x190+row*8,node)&&node&&Read(r,node,vt)&&vt==image+0x2306628&&Read(r,node+0x20,one)&&one==1&&Read(r,node+0x24,base)&&base==id&&Read(r,node+0x2c,index)&&index==row&&Read(r,node+0x38,p)&&p==primary+0x30&&Read(r,block+0x3b8+row*8,p)&&p==primary+0x30&&Read(r,primary+0x38,p)&&p==node;
}
template<class R>bool ReadOnce(R& r,std::uintptr_t image,std::uintptr_t player,Pair& out)noexcept{
 out={};if(!Range(image,0xa01d000)||!player||player<0x20)return false;out.player=player;out.go=player-0x20;
 std::uintptr_t p=0,vt=0,ids=0,pools=0,parts=0,blocks=0,table=0;std::uint32_t n=0,flags=0;
 if(!Read(r,image+0x2a82c30,p)||p!=player||!Read(r,player,vt)||vt!=image+0x2295490||!Read(r,player+0x204,flags)||!(flags&1u)||(flags&0x100u)||
  !Read(r,player+0xa8,p)||p||!Read(r,player+0x208,n)||n!=2||!Read(r,player+0x20c,n)||n!=2||!Read(r,player+0x214,n)||n||
  !Read(r,player+0x60,out.data)||!out.data||!Read(r,out.data,vt)||vt!=image+0x23ad970||!Read(r,out.data+0x1e8,out.scriptState)||!out.scriptState||!Read(r,out.scriptState+0x40,n)||n!=2||
  !Read(r,image+0x2c16920,out.vars)||out.vars!=image+0x2c01360||!Read(r,player+0x140,p)||p!=out.vars||
  !Read(r,player+0x70,ids)||!ids||!Read(r,player+0x78,pools)||!pools||!Read(r,player+0x90,parts)||!parts||!Read(r,player+0x378,blocks)||!Range(blocks,16*0x6a8)||!Read(r,player+0x50,table)||!table)return false;
 for(unsigned id=0;id<2;++id){auto& b=out.body[id];std::uint16_t actual=0;std::uint8_t pool=0,part=0;
  if(!Read(r,ids+id*2,actual)||actual!=id||!Read(r,pools+id,pool)||pool>=16||!Read(r,parts+id,part)||part!=3)return false;
  const auto block=blocks+std::uintptr_t(pool)*0x6a8;if(!Read(r,table+pool*8,p)||p!=block||!ReadModel(r,image,player,block,id,b))return false;b.pool=pool;
  if(!Read(r,block+0x140,p)||p!=out.vars||!Read(r,block+0xb8,b.carrier)||!b.carrier||!Read(r,b.carrier,vt)||vt!=image+0x23b9ac0||
   !Read(r,b.carrier+0x10,vt)||vt!=image+0x23b9b18||!Read(r,b.carrier+0x30,vt)||vt!=image+0x23b9b50||!Read(r,b.carrier+8,p)||p!=block||
   !Node(r,image,block,id,0x32,b.carrier,b.carrierNode)||!Read(r,b.carrier+0x68,b.carrierRecords)||!Range(b.carrierRecords,0x6c0)||(b.carrierRecords&15u))return false;
  std::uintptr_t phase=0,descriptor=0,callback=0;std::uint64_t stride=0,count=0;
  if(!Read(r,block+0x438,phase)||phase<0x30)return false;b.recipient=phase-0x30;
  if(!Read(r,b.recipient,vt)||vt!=image+0x23b8d78||!Read(r,b.recipient+0x10,vt)||vt!=image+0x23b8da0||!Read(r,b.recipient+0x30,vt)||vt!=image+0x23b8dd0||
   !Read(r,b.recipient+0x48,vt)||vt!=image+0x23b8e10||!Read(r,b.recipient+8,p)||p!=block||!Node(r,image,block,id,0x10,b.recipient,b.recipientNode)||
   !Read(r,b.recipient+0x88,b.recipientRecords)||!Range(b.recipientRecords,0x60)||(b.recipientRecords&15u)||!Read(r,b.recipient+0x58,descriptor)||!descriptor||
   !Read(r,descriptor,callback)||callback!=image+0x645940||!Read(r,descriptor+0x10,p)||p!=b.recipientRecords||!Read(r,descriptor+0x18,stride)||stride!=0x60||!Read(r,descriptor+0x20,count)||count!=1)return false;
 }
 for(unsigned id=0;id<2;++id){const auto& b=out.body[id];bool eventA=false,eventB=false;
  for(unsigned callbackIndex=0;callbackIndex<b.callbackCount;++callbackIndex){const auto& cb=b.callbacks[callbackIndex];
   if(cb.owner==out.body[1-id].carrier)return false;
   if(cb.owner!=b.carrier)continue;if(cb.body!=id)return false;
   const auto key=cb.event&0xffffffffffffULL;
   if(cb.function==image+0x112fc50&&key==0x6504d35367f0ULL){if(eventA)return false;eventA=true;}
   else if(cb.function==image+0x112fcd0&&key==0x20ca5ed42a91ULL){if(eventB)return false;eventB=true;}
   else return false;
  }
 }
 return out.body[0].pool!=out.body[1].pool&&out.body[0].cs!=out.body[1].cs&&out.body[0].entry!=out.body[1].entry&&out.body[0].modelAllocation!=out.body[1].modelAllocation&&out.body[0].callbackSlab!=out.body[1].callbackSlab&&out.body[0].carrier!=out.body[1].carrier&&out.body[0].recipient!=out.body[1].recipient;
}
template<class R>bool ReadPair(R& r,std::uintptr_t image,std::uintptr_t player,Pair& out)noexcept{out={};Pair a{},b{};if(!ReadOnce(r,image,player,a)||!ReadOnce(r,image,player,b)||!Same(a,b)||!CoherentCallbacks(a.body[0],b.body[0])||!CoherentCallbacks(a.body[1],b.body[1]))return false;out=a;return true;}
inline bool MatchesTeardown(const Pair& p,std::uintptr_t object,unsigned kind)noexcept{
 for(const auto& b:p.body)if(kind==0?object==b.block:kind==1?object==b.cs:kind==2?object==b.entry:object==p.go)return true;return false;
}
template<class R>bool Retired(R& r,std::uintptr_t image,const Pair& p)noexcept{
 // Exact Q-owned descriptor list; a merely readable queue pointer cannot
 // establish retirement or permit lifetime release.
 if(!Range(image,0xa01d000))return false;
 std::uintptr_t queued=0,slab=0,head=0,vt=0;std::uint64_t bytes=0,used=0;std::uint16_t flags=0;
 if(p.data<0x20||!Read(r,p.data-0x20,vt)||vt!=image+0x23ad938||
    !Read(r,p.data+0x1a0,slab)||!slab||(slab&15u)||!Read(r,p.data+0x1b0,bytes)||bytes<0x80||bytes>0x100000||
    !Range(slab,static_cast<std::size_t>(bytes))||!Read(r,p.data+0x1b8,used)||used>bytes||
    !Read(r,p.data+0x1c2,flags)||!(flags&1u)||!Read(r,p.data+0x1a8,head)||
    !Read(r,p.data+0x1d8,queued)||!queued||(queued&15u))return false;
 const auto within=[&](std::uintptr_t at,std::size_t n)noexcept{return Range(at,n)&&at>=slab&&at-slab<=bytes&&n<=bytes-(at-slab);};
 if(!within(queued,0x80))return false;std::uintptr_t seen[64]{};unsigned depth=0,matches=0;
 while(head){
  if(depth==64||(head&7u)||!within(head,0x28))return false;
  for(unsigned n=0;n<depth;++n)if(seen[n]==head)return false;seen[depth++]=head;
  std::uintptr_t callback=0,next=0,array=0;std::uint64_t stride=0,count=0;
  if(!Read(r,head,callback)||callback<image+0x1000||callback>=image+0x208d000||!Read(r,head+8,next)||
     !Read(r,head+0x10,array)||!Read(r,head+0x18,stride)||!Read(r,head+0x20,count))return false;
  if(array==queued){if(callback!=image+0x645940||stride!=0x40||count!=2||++matches!=1)return false;}
  head=next;
 }
 if(matches!=1)return false;
 for(const auto& b:p.body){std::uintptr_t bitmap=0,value=0;std::uint32_t bits=0;std::uint8_t current=0,next=0,kind=0;std::uint16_t id=0;
  std::uint32_t resourceKind=0;std::uintptr_t stateRecords=0;
  if(!Read(r,b.carrierNode+8,bitmap)||!Read(r,bitmap,bits)||(bits&1u)||
   !Read(r,b.carrierNode+0x10,bitmap)||!Read(r,bitmap,bits)||(bits&1u)||
   !Read(r,b.carrierRecords+0x370,value)||value||!Read(r,b.carrierRecords+0x240,resourceKind)||resourceKind||
   !Read(r,b.carrierRecords+0x248,value)||value||!Read(r,b.carrierRecords+0x250,value)||value||!Read(r,b.carrierRecords+0x258,value)||value||
   !Read(r,b.state+0x280,stateRecords)||!stateRecords||!Read(r,stateRecords,bits)||(bits&(1u<<22))||
   !Read(r,b.carrierRecords+0x380,current)||current||!Read(r,b.carrierRecords+0x381,next)||next||
   !Read(r,b.recipientNode+8,bitmap)||!Read(r,bitmap,bits)||(bits&1u)||!Read(r,b.recipientNode+0x10,bitmap)||!Read(r,bitmap,bits)||(bits&1u)||
   !Read(r,b.recipientRecords+0x28,value)||value||!Read(r,b.recipientRecords+0x20,value)||value||!Read(r,b.recipientRecords+0x30,id)||id!=0xffff||
   !Read(r,b.recipientRecords+0x10,current)||current||!Read(r,b.recipientRecords+0x11,next)||next||!Read(r,queued+b.id*0x40+8,kind)||(kind==14||kind==15))return false;
  for(unsigned n=0;n<b.callbackCount;++n){const auto slot=b.callbackSlab+std::uintptr_t(n)*0x60;std::uintptr_t owner=0;
   if(!Read(r,slot+0x48,owner)||owner==p.body[0].carrier||owner==p.body[1].carrier)return false;
  }
 }
 return true;
}
} // namespace mgscoop::native_pose_carry_participants
