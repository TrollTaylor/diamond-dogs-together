// Native online-player sleep controller already present in the pinned TPP executable.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "mgscoop/coop_sleep.hpp"
#include "mgscoop/coop_carry.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_defer.hpp"
#include "coop_sleep_policy.hpp"
#include "sleep_plugin_read.hpp"
#include "sleep_graph_read.hpp"
#include "sleep_seed_contract.hpp"
namespace mgscoop::coop_sleep {
namespace {
namespace p=coop_sleep_policy;namespace sp=sleep_plugin_read;
constexpr std::uintptr_t kImageSpan=0xa01d000,kPlayerSlot=0x2a82c30;
std::uintptr_t g_base=0;
enum class Phase : unsigned {empty,partial,held,waking,retired};
struct Identity {
 std::uintptr_t P=0,Q=0,life=0,block=0,ps=0,primary=0,records=0,node=0,bitmap=0,actions=0;
 std::uintptr_t cs=0,entry=0,controller=0,lifetime=0,mog=0;
};
struct Current {Identity owner{};p::Sample sample{};std::uint32_t sequence=0;};
struct State {
 Phase phase=Phase::empty;Identity owner{};std::uint32_t thread=0;
 float maximumHealth=0,maximumStun=0,originalStun=0;
 bool entered=false,wakeRequested=false;
 std::uint64_t seededAt=0,wakeAt=0;
 unsigned keeps=0; p::Retirement retirement{};
 const char* reason="absent";
};
State g_state[2]{};SRWLOCK g_lock=SRWLOCK_INIT;
struct Preserve {DWORD error=GetLastError();~Preserve(){SetLastError(error);}};
bool Bytes(std::uintptr_t at,void* out,std::size_t size) noexcept {SIZE_T n=0;return sp::Range(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&n)&&n==size;}
template<class T> bool Get(std::uintptr_t at,T& value) noexcept {return Bytes(at,&value,sizeof(value));}
bool Same(const Identity& a,const Identity& b) noexcept {
 return a.P==b.P&&a.Q==b.Q&&a.life==b.life&&a.block==b.block&&a.ps==b.ps&&a.primary==b.primary&&
  a.records==b.records&&a.node==b.node&&a.bitmap==b.bitmap&&a.actions==b.actions&&a.cs==b.cs&&
  a.entry==b.entry&&a.controller==b.controller&&a.lifetime==b.lifetime&&a.mog==b.mog;
}
bool Same(const p::Sample& a,const p::Sample& b) noexcept {
 return a.health==b.health&&a.maximumHealth==b.maximumHealth&&a.stun==b.stun&&a.maximumStun==b.maximumStun&&
  a.lifeState==b.lifeState&&a.qAction==b.qAction&&a.mode==b.mode&&a.requested==b.requested&&a.action==b.action&&
  a.nodeActive==b.nodeActive&&a.graphCurrent==b.graphCurrent&&a.sleepGraph==b.sleepGraph&&a.carried==b.carried&&
  a.graphNode==b.graphNode&&a.graphHash==b.graphHash;
}
bool ReadOnce(unsigned id,Current& out) noexcept {
 out={};if(id>=2||!g_base)return false;auto& o=out.owner;auto& s=out.sample;
 out.sequence=partner_input_hook::WindowSeq();if((out.sequence&1u)||partner_defer::InBatch()||partner_input_hook::InDrainJob())return false;
 std::uintptr_t vt=0,owner=0,ids=0,pools=0,parts=0,blocks=0,table=0,lives=0,actions=0;
 std::uint32_t flags=0,count=0,realized=0,local=0,selected=0,one=0,base=0;
 std::uint16_t actual=0;std::uint8_t pool=0,part=0;
 if(!Get(g_base+kPlayerSlot,o.P)||!sp::Aligned(o.P,8)||!Get(o.P,vt)||vt!=g_base+0x2295490||
  !Get(o.P+0x204,flags)||!(flags&1u)||(flags&0x100u)||!Get(o.P+0x208,count)||count!=2||
  !Get(o.P+0x20c,realized)||realized!=2||!Get(o.P+0x214,local)||local||!Get(o.P+0x218,selected)||selected||
  !Get(o.P+0xa8,owner)||owner||!Get(o.P+0x60,o.Q)||!sp::Aligned(o.Q,8)||!Get(o.Q,vt)||vt!=g_base+0x23ad970||
  !Get(o.P+0x70,ids)||!sp::Aligned(ids,2)||!Get(ids+2u*id,actual)||actual!=id||
  !Get(o.P+0x78,pools)||!Get(pools+id,pool)||pool>=16||!Get(o.P+0x90,parts)||!Get(parts+id,part)||part!=3||
  !Get(o.P+0x378,blocks)||!sp::Aligned(blocks,8)||!Get(o.P+0x50,table)||!sp::Aligned(table,8))return false;
 o.block=blocks+std::uintptr_t(pool)*0x6a8;
 if(!Get(table+8u*pool,owner)||owner!=o.block||!Get(o.block,vt)||vt!=g_base+0x2295210||
  !Get(o.block+8,one)||one!=1||!Get(o.block+0xc,base)||base!=id||!Get(o.block+0x138,owner)||owner!=o.P||
  !Get(o.block+0x78,o.ps)||!sp::Aligned(o.ps,8)||!Get(o.ps,vt)||(vt!=g_base+0x23b09f0&&vt!=g_base+0x23b0c70)||
  !Get(o.ps+8,base)||base!=id||!Get(o.ps+0x408,owner)||owner!=o.block||
  !Get(o.Q+8,lives)||!sp::Aligned(lives,4)||!Get(o.Q+0x18,actions)||!sp::Aligned(actions,2))return false;
 o.life=lives+std::uintptr_t(id)*0x4c;o.actions=actions;
 if(!Get(o.life+0x24,s.health)||!Get(o.life+0x28,s.maximumHealth)||!Get(o.life+0x2c,s.stun)||
  !Get(o.life+0x30,s.maximumStun)||!Get(o.life+0x34,s.lifeState)||!Get(actions+2u*id,s.qAction))return false;
 std::uintptr_t iface=0;
 if(!Get(o.ps+0xd8,iface)||!sp::Aligned(iface,8)||!Get(iface,vt))return false;
 const auto adjustment=sp::CompleteOffset(g_base,vt);
 if(adjustment==UINTPTR_MAX||iface<adjustment)return false;o.primary=iface-adjustment;
 if(!sp::Aligned(o.primary,8)||!Get(o.primary,vt)||vt!=g_base+0x23d5c18||
  !Get(o.primary+0x10,vt)||vt!=g_base+0x23d5c40||!Get(o.primary+0x30,vt)||vt!=g_base+0x23d5c70||
  !Get(o.primary+0x48,vt)||vt!=g_base+0x23d5cb0||!Get(o.primary+8,owner)||owner!=o.block||
  !Get(o.primary+0x78,o.records)||!sp::Aligned(o.records,16)||!Get(o.ps+0x500,o.node)||!sp::Aligned(o.node,8)||
  !Get(o.node,vt)||vt!=g_base+0x2306628||!Get(o.primary+0x38,owner)||owner!=o.node||
  !Get(o.node+0x38,owner)||owner!=o.primary+0x30||!Get(o.node+0x20,one)||one!=1||
  !Get(o.node+0x24,base)||base!=id||!Get(o.node+8,o.bitmap)||!sp::Aligned(o.bitmap,4))return false;
 std::uint32_t bits=0;std::uintptr_t callback=0;std::int32_t callbackAdjustment=0;
 if(!Get(o.bitmap,bits)||!Get(o.records,callback)||!Get(o.records+8,callbackAdjustment)||callbackAdjustment||
  !Get(o.records+0x10,s.mode)||!Get(o.records+0x11,s.requested)||s.mode>6||s.requested>6||
  (callback!=sp::ExpectedCallback(g_base,s.mode)&&(s.mode||callback))||
  !Get(o.ps+0x2d8,owner)||!owner||!Get(owner,s.action))return false;
 s.nodeActive=(bits&1u)!=0;
 // Current animation controller identity, using the exact graph reader layout.
 std::uintptr_t entries=0,vector=0,node=0;std::uint32_t layer=0;std::uint8_t reload=0;
 if(!Get(o.ps+0x238,o.cs)||!sp::Aligned(o.cs,8)||!Get(o.cs,vt)||vt!=g_base+0x22e6070||
  !Get(o.block+0x10,owner)||owner!=o.cs||!Get(o.cs+8,one)||one!=1||!Get(o.cs+0xc,base)||base!=id||
  !Get(o.cs+0x10,entries)||!sp::Aligned(entries,16))return false;o.entry=entries;
 if(!Get(o.entry+0x90,vector)||!sp::Aligned(vector,8)||!Get(vector,o.controller)||!sp::Aligned(o.controller,8)||
  !Get(o.controller,vt)||vt!=g_base+0x250ba50||!Get(o.controller+0x10,vt)||vt!=g_base+0x250bc00||
  !Get(o.controller+0xd4,layer)||layer||!Get(o.controller+0x185,reload)||reload||
  !Get(o.controller+0xf0,o.lifetime)||!o.lifetime||!Get(o.controller+0x1b8,o.mog)||!sp::Aligned(o.mog,8)||
  !Get(o.controller+0x1a0,node))return false;
 unsigned char magic[15]{};constexpr unsigned char expected[15]={'F','O','X','M','O','T','I','O','N','G','R','A','P','H',0};
 std::uint32_t version=0,layers=0,header=0,tableOffset=0,nodeCount=0,hashOffset=0;std::int32_t rel=0;
 if(!Bytes(o.mog,magic,sizeof(magic))||std::memcmp(magic,expected,sizeof(magic))||!Get(o.mog+0x10,version)||version!=6||
  !Get(o.mog+0x1c,layers)||!layers||layers>32||!Get(o.mog+0x20,rel)||!sleep_graph_read::Relative(0x1c,rel,0x38,header)||
  !Get(o.mog+header+0xc,nodeCount)||!nodeCount||nodeCount>16384||!Get(o.mog+header+0x10,rel)||
  !sleep_graph_read::Relative(header+0x10,rel,std::size_t(nodeCount)*0x48,tableOffset))return false;
 if(node){
  if(node<o.mog)return false;const auto offset=node-o.mog;
  if(offset<tableOffset||offset-tableOffset>=std::size_t(nodeCount)*0x48||(offset-tableOffset)%0x48||
   !Get(node+0x18,rel)||!sleep_graph_read::Relative(static_cast<std::uint32_t>(offset)+0x18,rel,8,hashOffset))return false;
  std::uint64_t hash=0;if(!Get(o.mog+hashOffset,hash))return false;
  s.graphNode=node;s.graphHash=hash&0xffffffffffffull;
  s.graphCurrent=true;s.sleepGraph=sleep_graph_read::SleepHashCandidate(s.graphHash);
 }
 const auto cargo=coop_carry::HudRole();s.carried=p::CarriedAction(s.qAction)||(id==0&&cargo==4)||(id==1&&cargo==3);
 return p::Valid(s)&&out.sequence==partner_input_hook::WindowSeq()&&!partner_defer::InBatch()&&!partner_input_hook::InDrainJob();
}
bool CurrentBody(unsigned id,Current& out) noexcept {
 Current a{},b{};if(!ReadOnce(id,a)||!ReadOnce(id,b)||a.sequence!=b.sequence||!Same(a.owner,b.owner)||!Same(a.sample,b.sample))return false;
 out=b;return true;
}
bool Owned(unsigned id,const State& s,Current& out) noexcept {
 return s.thread==GetCurrentThreadId()&&CurrentBody(id,out)&&Same(s.owner,out.owner)&&
  s.maximumHealth==out.sample.maximumHealth&&s.maximumStun==out.sample.maximumStun;
}
std::uint32_t Bits(float value) noexcept {std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));return bits;}
bool Cas(std::uintptr_t at,std::uint32_t expected,std::uint32_t desired) noexcept {
 if(!sp::Aligned(at,4))return false;
 __try{return static_cast<std::uint32_t>(_InterlockedCompareExchange(reinterpret_cast<volatile long*>(at),static_cast<long>(desired),static_cast<long>(expected)))==expected;}
 __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Refresh(unsigned id,const State& s,const Current& expected,Current& fresh) noexcept {
 return Owned(id,s,fresh)&&fresh.sequence==expected.sequence&&Same(fresh.sample,expected.sample);
}
bool WakeLocked(unsigned id,State& s) noexcept {
 if(s.phase==Phase::empty)return true;
 Current current{};if(!Owned(id,s,current)){s.reason="owner_read_pending";return false;}
 s.wakeRequested=true;
 if(coop_carry::Installed()){
  const auto attachment=coop_carry::ReadAttachmentState();
  if(!attachment.ready){s.reason="attachment_read_pending";return false;}
  if(attachment.carried[id]||current.sample.carried){
   if(attachment.carried[id])coop_carry::RequestPutDownForBody(id);
   s.reason="putdown_pending";return false;
  }
  if(attachment.carrying[id]){s.reason="carrier_state_pending";return false;}
 }else if(current.sample.carried){s.reason="putdown_pending";return false;}
 if(s.phase==Phase::partial){s.reason="partial_seed_pending";return false;}
 if(p::Retired(current.sample)&&p::ObserveRetirement(s.retirement,current.sample,GetTickCount64())){
  s=State{};return true;
 }
 if(p::Wake(current.sample)){
  // Original life update 11F8F37..11F8F79 clears sleep only when current
  // stun reaches its current maximum. Clearing lifeState with depleted stun
  // causes that original update to start sleep again. Restore only this owned
  // scalar, then let the original life/plugin/graph sequence retire normally.
  Current fresh{};float target=0;
  if(!p::WakeStun(current.sample,target)||!Refresh(id,s,current,fresh)||
     (fresh.sample.stun!=target&&!Cas(fresh.owner.life+0x2c,Bits(fresh.sample.stun),Bits(target)))){
   s.reason="wake_changed";return false;
  }
  s.phase=Phase::waking;if(!s.wakeAt)s.wakeAt=GetTickCount64();s.reason="native_wake_pending";
  Current post{};if(!Owned(id,s,post)||post.sample.stun!=target)s.reason="wake_post_pending";
 }else s.reason="native_retirement_pending";
 return false;
}
} // namespace
bool Configure(const void* image,std::size_t span) noexcept {
 const Preserve preserve;if(!image||span<kImageSpan)return false;
 const auto base=reinterpret_cast<std::uintptr_t>(image);AcquireSRWLockExclusive(&g_lock);
 if(g_base){const bool same=g_base==base;ReleaseSRWLockExclusive(&g_lock);return same;}
 auto reader=partner_input_hook::ReadNativePoseOriginalCode;std::uintptr_t mismatch=0;
 const bool passed=sp::CodeGate(reader,base,span,mismatch)&&sleep_graph_read::CodeGate(reader,base,span,mismatch)&&
  sleep_seed_contract::CodeGate(reader,base,span,mismatch);
 if(passed)g_base=base;ReleaseSRWLockExclusive(&g_lock);return passed;
}
bool Configured() noexcept {return g_base!=0;}
bool Hold(unsigned id,bool on) noexcept {
 const Preserve preserve;if(id>=2)return false;AcquireSRWLockExclusive(&g_lock);auto& s=g_state[id];
 const auto finish=[&](bool result) noexcept {ReleaseSRWLockExclusive(&g_lock);return result;};
 if(!on)return finish(WakeLocked(id,s));
 if(s.phase!=Phase::empty){Current current{};const bool ok=Owned(id,s,current);if(!ok)s.reason="owner_read_pending";return finish(ok&&s.phase!=Phase::partial);}
 Current current{};if(!CurrentBody(id,current)||!p::Seed(current.sample))return finish(false);
 s.owner=current.owner;s.thread=GetCurrentThreadId();s.maximumHealth=current.sample.maximumHealth;
 s.maximumStun=current.sample.maximumStun;s.originalStun=current.sample.stun;
 Current fresh{};if(!Refresh(id,s,current,fresh)||!Cas(fresh.owner.life+0x2c,Bits(fresh.sample.stun),Bits(0.f))){s=State{};return finish(false);}
 s.phase=Phase::partial;s.seededAt=GetTickCount64();s.reason="partial_seed_pending";
 Current between{};auto depleted=fresh.sample;depleted.stun=0;
 if(Owned(id,s,between)&&between.sequence==fresh.sequence&&Same(depleted,between.sample)&&p::Retired(between.sample)&&
  Cas(between.owner.life+0x34,0,2)){
  s.phase=Phase::held;s.reason="native_onset_pending";
  Current post{};const bool proved=Owned(id,s,post)&&post.sample.lifeState==2&&post.sample.stun==0&&
   post.sample.health==fresh.sample.health;
  if(!proved)s.reason="seed_post_pending";
  return finish(proved);
 }
 // Undo only this still-unconsumed first scalar. Regrowth or another native
 // state retains quarantine; it never authorizes a stale overwrite.
 if(Owned(id,s,between)&&between.sequence==fresh.sequence&&between.sample.stun==0&&p::Retired(between.sample)&&
  Cas(between.owner.life+0x2c,Bits(0.f),Bits(s.originalStun)))s=State{};
 return finish(false);
}
bool Held(unsigned id) noexcept {
 if(id>=2)return false;AcquireSRWLockShared(&g_lock);const bool held=g_state[id].phase!=Phase::empty;
 ReleaseSRWLockShared(&g_lock);return held;
}
bool Active(unsigned id) noexcept {return Held(id);}
bool WakeReady(unsigned id) noexcept {
 const Preserve preserve;if(id>=2)return false;AcquireSRWLockShared(&g_lock);
 Current current{};const auto& s=g_state[id];const bool ready=s.phase==Phase::empty||
  (s.wakeRequested&&s.retirement.observations>=2&&Owned(id,s,current)&&p::Retired(current.sample));
 ReleaseSRWLockShared(&g_lock);return ready;
}
bool CarryReady(unsigned id) noexcept {
 const Preserve preserve;if(id>=2)return false;AcquireSRWLockShared(&g_lock);Current current{};
 const bool ready=Owned(id,g_state[id],current)&&p::CarryReady(current.sample);
 ReleaseSRWLockShared(&g_lock);return ready;
}
bool DetailLine(unsigned id,char* out,std::size_t capacity) noexcept {
 const Preserve preserve;if(id>=2||!out||!capacity)return false;AcquireSRWLockShared(&g_lock);
 Current current{};const auto& state=g_state[id];const bool read=CurrentBody(id,current);const bool owned=read&&Same(state.owner,current.owner);
 const auto& s=current.sample;std::uint64_t remembered=0;if(read)Get(current.owner.records+0x18,remembered);
 const int n=std::snprintf(out,capacity,"sleepDetail;b=%u;r=%u;own=%u;ph=%u;m=%u,%u;a=%x;act=%x;life=%u;carry=%u;g=%llx;want=%llx;why=%s",id,read?1u:0u,owned?1u:0u,
  unsigned(state.phase),unsigned(s.mode),unsigned(s.requested),unsigned(s.qAction),unsigned(s.action),s.lifeState,s.carried?1u:0u,
  static_cast<unsigned long long>(s.graphHash),static_cast<unsigned long long>(remembered&0xffffffffffffull),state.reason);
 ReleaseSRWLockShared(&g_lock);return n>=0&&static_cast<std::size_t>(n)<capacity;
}
void Tick() noexcept {
 const Preserve preserve;if(!g_base||!TryAcquireSRWLockExclusive(&g_lock))return;
 for(unsigned id=0;id<2;++id){auto& s=g_state[id];if(s.phase==Phase::empty)continue;
  Current current{};if(!Owned(id,s,current)){s.reason="owner_read_pending";continue;}
  s.entered=s.entered||p::Entered(current.sample);
  if(s.wakeRequested){WakeLocked(id,s);continue;}
  if(current.sample.lifeState==0&&s.entered){
   // Native auto recovery may race a delayed owner tick. Do not reseed the
   // waking or retired graph; canonical sleep/input authority remains held.
   s.phase=Phase::waking;s.reason="native_recovery_observed";
   if(p::ObserveRetirement(s.retirement,current.sample,GetTickCount64()))s.phase=Phase::retired;
   continue;
  }
  if(p::Maintain(current.sample,s.entered,false)){
   Current fresh{};if(Refresh(id,s,current,fresh)&&Cas(fresh.owner.life+0x2c,Bits(fresh.sample.stun),Bits(0.f))){
    Current post{};++s.keeps;s.reason=Owned(id,s,post)&&post.sample.lifeState==2&&post.sample.stun==0&&
     post.sample.health==fresh.sample.health?"native_rest_held":"rest_post_pending";
   }else s.reason="rest_changed";
  }
 }
 ReleaseSRWLockExclusive(&g_lock);
}
Body Read(unsigned id) noexcept {
 const Preserve preserve;Body b{};if(id>=2)return b;AcquireSRWLockShared(&g_lock);
 Current current{};const auto& s=g_state[id];if(CurrentBody(id,current)){
  b.read=true;b.stun=current.sample.stun;b.stunMax=current.sample.maximumStun;b.lifeState=current.sample.lifeState;
  b.action=current.sample.qAction;b.sleeping=current.sample.lifeState==2;b.retired=p::Retired(current.sample);
 }
 b.held=s.phase!=Phase::empty;b.waking=s.phase==Phase::waking;b.reason=s.reason;
 ReleaseSRWLockShared(&g_lock);return b;
}
bool StatusLine(char* out,std::size_t capacity) noexcept {
 if(!out||!capacity)return false;const auto a=Read(0),b=Read(1);unsigned keeps[2]{};
 AcquireSRWLockShared(&g_lock);keeps[0]=g_state[0].keeps;keeps[1]=g_state[1].keeps;ReleaseSRWLockShared(&g_lock);
 const int n=std::snprintf(out,capacity,"coopsleep;b0=%u,h%u,l%u,s%.0f/%.0f,a%x;b1=%u,h%u,l%u,s%.0f/%.0f,a%x;keep=%u,%u;reseed=0,0;wake=%u,%u",
  a.read?1u:0u,a.held?1u:0u,a.lifeState,double(a.stun),double(a.stunMax),unsigned(a.action),
  b.read?1u:0u,b.held?1u:0u,b.lifeState,double(b.stun),double(b.stunMax),unsigned(b.action),keeps[0],keeps[1],
  a.waking?1u:0u,b.waking?1u:0u);
 return n>=0&&static_cast<std::size_t>(n)<capacity;
}
} // namespace mgscoop::coop_sleep
