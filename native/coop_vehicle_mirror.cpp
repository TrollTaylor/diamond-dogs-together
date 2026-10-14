#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "coop_vehicle_mirror.hpp"
#include "vehicle_mirror_native_read.hpp"
#include "vehicle_mirror_clones.hpp"
#include "vehicle_mirror_contract.hpp"
#include "vehicle_registry_read.hpp"
#include "vehicle_mirror_hook_read.hpp"
#include "vehicle_mirror_close_read.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/link_crypto.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "mgscoop/downed_player.hpp"
#include "mgscoop/partner_player.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "MinHook.h"
namespace mgscoop::coop_vehicle_mirror {
namespace {
namespace state=vehicle_mirror_state;
namespace native=vehicle_mirror_native_read;
namespace clones=vehicle_mirror_clones;
namespace pd=coop_passenger::detail;
namespace ph=partner_input_hook;
namespace hooks=vehicle_mirror_hook_read;
using BodyObservation=vehicle_controller_read::Observation;
struct Preserve {DWORD value=GetLastError();void Restore() const noexcept{SetLastError(value);}void Capture() noexcept{value=GetLastError();}~Preserve(){Restore();}};
bool ReadBytes(std::uintptr_t p,void* out,std::size_t size) noexcept {
 SIZE_T done=0;return pd::Range(p,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),out,size,&done)&&done==size;
}
template<class T> bool Read(std::uintptr_t p,T& out) noexcept{return ReadBytes(p,&out,sizeof(T));}
std::uintptr_t g_base=0;std::size_t g_span=0;Bindings g_bindings{};Log g_log=nullptr;
std::atomic<bool> g_installed{false},g_enabled{false},g_stopped{false};
std::atomic<std::uint64_t> g_installSerial{0},g_installTransactions{0};
std::atomic<const char*> g_reason{"off"};
std::atomic<unsigned> g_rx{0},g_tx{0},g_claims{0},g_imports{0},g_binds{0},g_poses{0},g_remotePhysics{0},g_uiSuppressed{0};
state::Inbox g_inbox{};state::Lease g_lease{};state::Receipt g_local{};
state::Link g_localLink{};std::uint32_t g_localSequence=0,g_localMount=0;
std::uint64_t g_localAt=0,g_sentAt=0;Purpose g_purpose{};bool g_clearing=false;
bool g_vehicleRetained=false;state::Unit g_retainedVehicle{};
ph::NativePoseJobContext g_clearContext{};
SRWLOCK g_inboxLock=SRWLOCK_INIT;
SRWLOCK g_replyLock=SRWLOCK_INIT;ReplicaRide g_reply{};state::Unit g_localUnit{};
void ClearReply() noexcept {AcquireSRWLockExclusive(&g_replyLock);g_reply={};ReleaseSRWLockExclusive(&g_replyLock);}
using StartFn=bool(*)(void*,std::uint32_t);
using UpdateFn=void(*)(void*,std::uint32_t);
using PhaseFn=void(*)(void*,std::uint32_t,std::uint32_t,std::uintptr_t);
using AdmitFn=bool(*)(void*,std::uint8_t,std::uint16_t);
using BindFn=void(*)(void*,std::uint32_t,void*,void*);

StartFn g_start=nullptr,g_remotePredicate=nullptr,g_startClone=nullptr,g_predicateClone=nullptr;
UpdateFn g_update=nullptr,g_init=nullptr,g_updateClone=nullptr;
PhaseFn g_phase3=nullptr;
hooks::Hook g_hooks[5]{};
unsigned char g_originalHeads[5][16]{};
struct Clone {
 void* page=nullptr;std::size_t size=0;unsigned char expected[512]{};RUNTIME_FUNCTION table{};
};
Clone g_clones[3]{};
struct Frame {
 partner_input_hook::NativePoseJobContext context{};native::OwnedScope owner{};
 BodyObservation bodies{};pd::EngineRead engine{};carry_queue_lifetime::Owner queue{};
 ph::NativePoseVehicleOwnership ownership{};
 std::uintptr_t service=0;state::Link link{};state::Scope stateScope{};
};
bool SameContext(const partner_input_hook::NativePoseJobContext& a,const partner_input_hook::NativePoseJobContext& b) noexcept {
 return a.player==b.player&&a.go==b.go&&a.block==b.block&&a.state==b.state&&a.modelCS==b.modelCS&&a.modelEntry==b.modelEntry&&
 a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.thread==b.thread&&a.sequence==b.sequence&&a.consumer==b.consumer&&
 a.modelReady&&b.modelReady&&a.selectedBodyOwned&&b.selectedBodyOwned&&a.selectedBody==1&&b.selectedBody==1&&
 a.pp==b.pp&&a.source==b.source&&a.windowPPVT==b.windowPPVT&&a.birthSerial==b.birthSerial;
}
bool PatchesLive() noexcept {
 if(!g_installed.load(std::memory_order_acquire)||!g_installSerial.load(std::memory_order_acquire))return false;
 auto read=ReadBytes;unsigned char bytes[512]{};
 for(const auto& h:g_hooks)if(!hooks::Live(read,g_base,h))return false;
 for(const auto& c:g_clones)if(!c.page||!ReadBytes(reinterpret_cast<std::uintptr_t>(c.page),bytes,c.size)||std::memcmp(bytes,c.expected,c.size))return false;
 return true;
}
bool Gate() noexcept {
 unsigned char bytes[8192]{};
 for(const auto& p:vehicle_mirror_contract::pins){
  if(p.rva>g_span||p.size>g_span-p.rva)return false;
  link::Sha256 sha{};link::Sha256Init(sha);
  for(std::size_t off=0;off<p.size;){const auto left=p.size-off;const auto n=left<sizeof(bytes)?left:sizeof(bytes);
   if(!g_bindings.readOriginal(g_base+p.rva+off,bytes,n))return false;link::Sha256Update(sha,bytes,n);off+=n;
  }
  unsigned char digest[32]{};link::Sha256Final(sha,digest);if(std::memcmp(digest,p.sha,32))return false;
 }
 std::uintptr_t mismatch=0;auto read=g_bindings.readOriginal;
 if(!vehicle_controller_read::CodeGate(read,g_base,g_span,mismatch))return false;
 const struct {std::uintptr_t slot,target;} slots[]={{0x23ad970+0x38,0xfdef40},{0x23ad970+0x40,0xfe00d0},{0x23aa770,0xfd4910},
 {0x23cbf10,0x124a5b0},{0x23cbf10+8,0x125bfb0},{0x22ac8e0+0x220,0xa29530},{0x22ac8e0+0x228,0xa29470},
 {0x23063f8+0x10,0xbf5450},{0x22ed930+0x70,0xb33420}};
 for(const auto& s:slots){std::uintptr_t got=0;if(!Read(g_base+s.slot,got)||got!=g_base+s.target)return false;}return true;
}
template<std::size_t N,std::size_t U,std::size_t R,std::size_t B> bool BuildClone(Clone& c,
 const unsigned char(&code)[N],const unsigned char(&unwind)[U],const clones::Relocation(&relocations)[R],
 std::size_t branchOffset,const unsigned char(&branch)[B]) noexcept {
 if(N>sizeof(c.expected)||U>64||branchOffset>N||B>N-branchOffset)return false;
 c.page=near_code::AllocateNear(g_base+0x124aab0,4096);if(!c.page)return false;
 const auto at=reinterpret_cast<std::uintptr_t>(c.page);auto* raw=static_cast<unsigned char*>(c.page);
 std::memcpy(raw,code,N);
 for(const auto& r:relocations){std::int32_t displacement=0;
  if(r.offset>N||r.size>N-r.offset||r.field>r.size||4>r.size-r.field||
   !near_code::Rel32(at+r.offset+r.size,g_base+r.target,displacement)){VirtualFree(c.page,0,MEM_RELEASE);c.page=nullptr;return false;}
  std::memcpy(raw+r.offset+r.field,&displacement,4);
 }
 std::memcpy(raw+branchOffset,branch,B);std::memcpy(c.expected,raw,N);c.size=N;
 constexpr DWORD unwindAt=0x400;std::memcpy(raw+unwindAt,unwind,U);c.table={0,static_cast<DWORD>(N),unwindAt};
 DWORD old=0;if(!VirtualProtect(c.page,4096,PAGE_EXECUTE_READ,&old)){VirtualFree(c.page,0,MEM_RELEASE);c.page=nullptr;return false;}
 FlushInstructionCache(GetCurrentProcess(),c.page,4096);
 if(!RtlAddFunctionTable(&c.table,1,at)){VirtualFree(c.page,0,MEM_RELEASE);c.page=nullptr;return false;}
 // Registered function table and executable allocation remain process lifetime.
 return true;
}
bool WorldService(std::uintptr_t world,std::uintptr_t& service) noexcept {
 std::uintptr_t manager=0,registered=0,vt=0;
 return Read(world+0x98,manager)&&manager&&Read(manager+0x2a8,service)&&service&&Read(g_base+0x2c20730,registered)&&
 service==registered&&Read(service,vt)&&vt==g_base+0x22ac8e0;
}
bool ReadFrame(Frame& f,bool requirePurpose=true) noexcept {
 f={};if(!g_enabled.load()||g_stopped.load()||!worker_scenario::ThreadAdmitted()||!PatchesLive()||
 !ph::NativePoseWholePlayerLifetimeLive()||!ph::NativePoseLocalCacheIsolationLive()||
 !vehicle_native_protection::ProtectionLive())return false;
 // Real core ownership is independent of a fresh wire channel. No cached
 // fault-inclusive guard receipt grants a call or an exit request.
 if(requirePurpose&&(!g_purpose.serial||g_clearing||!ph::NativePoseVehicleCleanupCurrent(g_purpose)||
  !ph::ReadNativePoseVehicleOwnership(f.ownership)))return false;
 if(!ph::ReadNativePoseOwnedJobContext(f.context)||f.context.thread!=GetCurrentThreadId())return false;
 f.owner.image=g_base;f.owner.player=f.context.player;f.owner.context=f.context;
 if(!Read(f.owner.player+0x60,f.owner.data)||!Read(f.owner.data+0x1e8,f.owner.state))return false;
 auto read=ReadBytes;BodyObservation first{};pd::EngineRead a{},b{};carry_queue_lifetime::Owner q{};
 if(!native::ReadOwnedOnce(read,f.owner,first)||first.body[1].block!=f.context.block||
 !carry_queue_lifetime::ReadOwner(read,g_base,f.owner.player,q)||q.data!=f.owner.data||
 !pd::ReadEngine(read,g_base,g_span,f.owner.player,a)||!WorldService(a.world,f.service))return false;
 ph::NativePoseJobContext middle{};
 if(!ph::ReadNativePoseOwnedJobContext(middle)||!SameContext(f.context,middle)||
 !native::ReadOwnedOnce(read,f.owner,f.bodies)||!vehicle_controller_read::Same(first,f.bodies)||
 !pd::ReadEngine(read,g_base,g_span,f.owner.player,b)||!pd::Same(a,b)||
 !carry_queue_lifetime::ReadOwner(read,g_base,f.owner.player,f.queue)||!carry_queue_lifetime::Same(q,f.queue))return false;
 ph::NativePoseJobContext final{};
 if(!ph::ReadNativePoseOwnedJobContext(final)||!SameContext(f.context,final))return false;
 if(requirePurpose){ph::NativePoseVehicleOwnership again{};
  if(!ph::NativePoseVehicleCleanupCurrent(g_purpose)||!ph::ReadNativePoseVehicleOwnership(again)||
   again.lease.serial!=g_purpose.serial||again.lease.generation!=g_purpose.generation||again.lease.sceneEpoch!=g_purpose.sceneEpoch||
   again.vehicle!=g_retainedVehicle.vehicle||again.player!=f.context.player||again.block[1]!=f.context.block||
   again.birth[1]!=f.context.birthSerial||again.controller[1]!=f.bodies.body[1].primary||
   again.controllerRecord[1]!=f.bodies.body[1].record||again.queueRoot!=f.queue.root||again.queued!=f.queue.queued||
   again.sequence!=f.context.sequence||again.thread!=f.context.thread||again.purposes!=4u||again.pending||
   f.ownership.lease.serial!=again.lease.serial||f.ownership.birth[0]!=again.birth[0]||f.ownership.birth[1]!=again.birth[1]||
   f.ownership.birthLifecycle!=again.birthLifecycle)return false;
  ph::NativePoseVehicleGuardBinding binding{};
  if(!ph::ReadNativePoseVehicleGuardBinding(binding)||binding.protection.vehicle!=g_retainedVehicle.vehicle||
   binding.protection.service!=g_retainedVehicle.service||binding.protection.vehicleObject!=g_retainedVehicle.object||
   binding.protection.unit!=g_retainedVehicle.unit||binding.protection.records!=g_retainedVehicle.records||
   binding.protection.bitmap!=g_retainedVehicle.bitmap||binding.protection.metadata!=g_retainedVehicle.metadata||
   binding.protection.variantTable!=g_retainedVehicle.variantTable||binding.protection.variant!=g_retainedVehicle.variant||
   binding.protection.allocated!=g_retainedVehicle.allocated||binding.protection.type!=g_retainedVehicle.type||
   binding.protection.seats!=g_retainedVehicle.count)return false;
  f.ownership=again;
 }
 state::Link live{},late{};
 const bool linked=g_bindings.link(live)&&state::Valid(live)&&g_bindings.link(late)&&state::Same(live,late);
 if(linked)f.link=late;
 f.engine=b;auto& scope=f.stateScope;scope.current=true;scope.driveCurrent=linked;scope.link=f.link;
 scope.generation=f.context.generation;scope.thread=f.context.thread;scope.sequence=f.context.sequence;
 scope.player=f.context.player;scope.primary=f.bodies.body[1].primary;scope.record=f.bodies.body[1].record;
 scope.purpose=g_purpose.serial;scope.now=GetTickCount64();return true;
}
bool UnitFor(const Frame& f,std::uint16_t id,std::uintptr_t expected,state::Unit& out) noexcept {
 auto read=ReadBytes;state::Unit a{},b{};partner_input_hook::NativePoseJobContext after{};
 return pd::ReadUnit(read,g_base,f.service,id,expected,a)&&pd::ReadUnit(read,g_base,f.service,id,expected,b)&&pd::Same(a,b)&&
 partner_input_hook::ReadNativePoseOwnedJobContext(after)&&SameContext(f.context,after)&&(out=b,true);
}
bool LookupReady(const Frame& f,const state::Unit& unit) noexcept {
 auto read=ReadBytes;vehicle_registry_read::Lookup a{},b{};
 partner_input_hook::NativePoseJobContext after{};
 return vehicle_registry_read::Observe(read,g_base,g_span,f.engine,unit,a)&&vehicle_registry_read::Callable(read,g_base,unit,a)&&
 vehicle_registry_read::Observe(read,g_base,g_span,f.engine,unit,b)&&vehicle_registry_read::Callable(read,g_base,unit,b)&&
 vehicle_registry_read::Same(a,b)&&partner_input_hook::ReadNativePoseOwnedJobContext(after)&&SameContext(f.context,after);
}
bool InboxCopy(state::Inbox& out) noexcept {
 if(!TryAcquireSRWLockShared(&g_inboxLock))return false;out=g_inbox;ReleaseSRWLockShared(&g_inboxLock);return true;
}
bool ReplicaCurrent(Frame& f) noexcept {
 return ReadFrame(f)&&g_lease.phase!=state::Phase::idle&&state::Owned(g_lease,f.stateScope)&&
 f.bodies.body[1].primary==g_lease.primary&&f.bodies.body[1].record==g_lease.record;
}
bool SourceCurrent(const Frame& f) noexcept {
 state::Inbox inbox{};return InboxCopy(inbox)&&state::DriveOwned(g_lease,f.stateScope,inbox);
}
struct Backend {
 bool Resolve(state::Name name,state::Unit& out) noexcept {
  Frame f{};std::uint16_t id=0xffff;return ReadFrame(f)&&f.stateScope.driveCurrent&&
   g_bindings.resolve(name,id)&&id==g_retainedVehicle.vehicle&&UnitFor(f,id,g_retainedVehicle.unit,out)&&LookupReady(f,out);
 }
 bool Current(const state::Unit& expected,state::Unit& out) noexcept {Frame f{};
  return ReadFrame(f)&&state::SameAllocation(expected,g_retainedVehicle)&&UnitFor(f,expected.vehicle,expected.unit,out);
 }
 bool Admit(const state::Unit& unit,std::uint8_t seat,std::uint16_t rider) noexcept {
  Frame f{};state::Unit current{};
  if(rider!=1||!ReplicaCurrent(f)||!SourceCurrent(f)||!ph::NativePoseVehicleLeaseCurrent(g_purpose)||
   !UnitFor(f,unit.vehicle,unit.unit,current)||!pd::Same(unit,current)||
   seat>=current.count||current.rider[seat]!=0xffff||!LookupReady(f,current)||
   !g_vehicleRetained||!state::SameAllocation(g_retainedVehicle,current))return false;
  // Original wrapper owns natural occupancy/admission. No A29530 call,
  // direct rider/phase writes or fabricated native success return.
  const bool accepted=reinterpret_cast<AdmitFn>(g_base+0xcb9470)(reinterpret_cast<void*>(current.unit),seat,rider);
  if(accepted)g_claims.fetch_add(1);return accepted;
 }
 bool Submit(std::uint16_t vehicle,std::uint8_t phase,std::uint8_t seat,std::uint8_t cycle) noexcept {
  Frame f{};state::Unit current{};if(!ReplicaCurrent(f)||vehicle!=g_lease.unit.vehicle||seat!=g_lease.source.seat||
   (phase!=2&&phase!=3&&phase!=6)||!UnitFor(f,vehicle,g_lease.unit.unit,current)||current.rider[seat]!=1)return false;
  if(phase!=6&&(!SourceCurrent(f)||!ph::NativePoseVehicleLeaseCurrent(g_purpose)))return false;
  if(phase==6&&!ph::NativePoseVehicleCleanupCurrent(g_purpose))return false;
  const ph::NativePoseVehicleRequest request{phase,cycle,seat};
  return ph::SubmitNativePoseVehicleRequest(g_purpose,request);
 }
 bool Bound(const state::Lease& l) noexcept {
  Frame f{};state::Unit unit{};if(!ReplicaCurrent(f)||!UnitFor(f,l.unit.vehicle,l.unit.unit,unit))return false;
  const auto& body=f.bodies.body[1];return f.engine.bound[1]==unit.unit&&unit.rider[l.source.seat]==1&&
   body.raw[0x14]==l.source.phase&&vehicle_controller_read::Word(body.raw+0x44)==l.unit.vehicle;
 }
 bool Attached(const state::Lease& l) noexcept;
 bool Exited(const state::Lease& l) noexcept {
  Frame f{};state::Unit unit{};return ReplicaCurrent(f)&&UnitFor(f,l.unit.vehicle,l.unit.unit,unit)&&
   f.bodies.body[1].raw[0x14]==0&&!f.bodies.body[1].raw[0x15]&&!f.engine.bound[1]&&unit.rider[l.source.seat]==0xffff;
 }
 bool Release(std::uint64_t purpose) noexcept {
  Frame f{};if(purpose!=g_purpose.serial||!ReadFrame(f)||!ph::NativePoseVehicleCleanupCurrent(g_purpose)||
   !ph::RequestNativePoseVehicleLeaseClear(g_purpose))return false;
  g_clearContext=f.context;g_clearing=true;return true; // held until actual Closed/publish
 }
};
std::uint64_t g_posePurpose=0;std::uint32_t g_poseMount=0;
bool Backend::Attached(const state::Lease& l) noexcept {return Bound(l)&&g_posePurpose==l.purpose&&g_poseMount==l.source.mount;}
__declspec(noinline) bool HookStart(void* primary,std::uint32_t id) {
 Preserve preserve;Frame f{};
 if(id==1&&g_lease.phase==state::Phase::queued&&ReplicaCurrent(f)&&reinterpret_cast<std::uintptr_t>(primary)==g_lease.primary){
  preserve.Restore();const bool result=g_startClone(primary,id);preserve.Capture();if(result)g_imports.fetch_add(1);return result;
 }preserve.Restore();const bool result=g_start(primary,id);preserve.Capture();return result;
}
__declspec(noinline) void HookUpdate(void* primary,std::uint32_t id) {
 Preserve preserve;Frame f{};
 if(id==1&&g_lease.phase!=state::Phase::idle&&g_lease.phase!=state::Phase::quarantined&&ReplicaCurrent(f)&&
  reinterpret_cast<std::uintptr_t>(primary)==g_lease.primary){preserve.Restore();g_updateClone(primary,id);preserve.Capture();return;}
 preserve.Restore();g_update(primary,id);preserve.Capture();
}
__declspec(noinline) void HookInit(void* iface,std::uint32_t id) {
 Preserve preserve;Frame before{};
 const bool ours=id==1&&g_lease.phase==state::Phase::queued&&ReplicaCurrent(before)&&
  reinterpret_cast<std::uintptr_t>(iface)==g_lease.primary+0x30&&
  vehicle_controller_read::Word(before.bodies.body[1].raw+0x46)==g_lease.unit.vehicle;
 preserve.Restore();g_init(iface,id);preserve.Capture();
 if(ours){Frame after{};state::Unit unit{};
  if(!ReplicaCurrent(after)||!UnitFor(after,g_lease.unit.vehicle,g_lease.unit.unit,unit)||unit.rider[g_lease.source.seat]!=1||
   vehicle_controller_read::Word(after.bodies.body[1].raw+0x44)!=g_lease.unit.vehicle||!LookupReady(after,unit)){g_lease.phase=state::Phase::quarantined;return;}
  // Original natural phase1 uses the same binder after determining its native
  // vehicle. Here the proven peer kind9 initializer set active84 first.
  reinterpret_cast<BindFn>(g_base+0x125ad00)(reinterpret_cast<void*>(g_lease.primary),1,
   reinterpret_cast<void*>(g_lease.record),reinterpret_cast<void*>(after.engine.registry));
  Frame bound{};if(!ReplicaCurrent(bound)||bound.engine.bound[1]!=unit.unit){g_lease.phase=state::Phase::quarantined;return;}
  g_binds.fetch_add(1);
 }
}
__declspec(noinline) bool HookRemotePredicate(void* primary,std::uint32_t id) {
 Preserve preserve;const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());Frame f{};
 // Exact stock event0/1/9 nonlocal predicates only. Phase6 keeps the original
 // native exit-animation and indexed physical removal; no fake exit result.
 const bool site=caller==g_base+0x124e200||caller==g_base+0x1250e20||caller==g_base+0x1250fce||caller==g_base+0x1251096;
 if(site&&id==1&&g_lease.phase!=state::Phase::idle&&g_lease.phase!=state::Phase::quarantined&&ReplicaCurrent(f)&&
  reinterpret_cast<std::uintptr_t>(primary)==g_lease.primary){g_remotePhysics.fetch_add(1);preserve.Restore();const bool result=g_predicateClone(primary,id);preserve.Capture();return result;}
 preserve.Restore();const bool result=g_remotePredicate(primary,id);preserve.Capture();return result;
}
__declspec(noinline) void HookPhase3(void* primary,std::uint32_t body,std::uint32_t event,std::uintptr_t extra) {
 Preserve preserve;Frame f{};state::Unit unit{};
 // The complete stock case9 begins124EE96 and only invokes local-player
 // P214/P120/camera paths. Its original nonselected-body branch skips them.
 // It has no native seat, binder, physical pose or CS callback effects.
 if(g_enabled.load()&&body==1&&event==9&&g_lease.source.phase==3&&
  reinterpret_cast<std::uintptr_t>(primary)==g_lease.primary&&ReplicaCurrent(f)&&
  f.bodies.body[1].raw[0x14]==3&&UnitFor(f,g_lease.unit.vehicle,g_lease.unit.unit,unit)&&
  f.engine.bound[1]==unit.unit&&unit.rider[0]==1){
  g_uiSuppressed.fetch_add(1);return; // same stock void nonselected continuation
 }
 preserve.Restore();g_phase3(primary,body,event,extra);preserve.Capture();
}
bool Cold(BodyObservation& out,state::Link& live,pd::EngineRead& engine,std::uintptr_t& service) noexcept {
 if(!worker_scenario::ThreadAdmitted()||!PatchesLive()||!g_bindings.link(live)||!state::Valid(live))return false;
 const auto snapshot=downed_player::GetSnapshot();if(!snapshot.ready||!snapshot.armed||snapshot.down||!snapshot.generation||
 snapshot.mission!=live.mission||snapshot.epoch!=live.epoch||!pd::Fresh(GetTickCount64(),snapshot.lastPoll,750))return false;
 partner_player::State player{};if(partner_player::ReadState(partner_player::AddressesFor(reinterpret_cast<void*>(g_base)),player)!=partner_player::Refusal::none)return false;
 const auto seq=partner_input_hook::WindowSeq();if(seq&1u)return false;vehicle_controller_read::Scope scope{};
 scope.exactGate=scope.admitted=scope.ready=scope.armed=true;scope.image=g_base;scope.player=player.P;scope.data=player.Q;scope.state=player.S;
 scope.mission=scope.ownedMission=live.mission;scope.epoch=scope.ownedEpoch=live.epoch;scope.generation=scope.ownedGeneration=snapshot.generation;
 scope.thread=scope.ownerThread=GetCurrentThreadId();scope.now=GetTickCount64();scope.poll=snapshot.lastPoll;scope.sequenceBefore=scope.sequenceAfter=seq;
 auto read=ReadBytes;BodyObservation first{};pd::EngineRead a{};
 if(!vehicle_controller_read::ReadOnce(read,scope,first)||!pd::ReadEngine(read,g_base,g_span,player.P,a)||!WorldService(a.world,service)||
 !vehicle_controller_read::ReadOnce(read,scope,out)||!vehicle_controller_read::Same(first,out)||
 !pd::ReadEngine(read,g_base,g_span,player.P,engine)||!pd::Same(a,engine))return false;
 const auto after=downed_player::GetSnapshot();state::Link final{};
 return after.ready&&after.armed&&!after.down&&after.generation==snapshot.generation&&after.mission==snapshot.mission&&after.epoch==snapshot.epoch&&
 partner_input_hook::WindowSeq()==seq&&g_bindings.link(final)&&state::Same(live,final);
}
bool NameFor(std::uint16_t vehicle,state::Name& name) noexcept {
 unsigned matches=0;
 for(unsigned kind=1;kind<=2;++kind)for(unsigned index=0;index<(kind==1?13u:6u);++index){
  const state::Name candidate{static_cast<state::NameKind>(kind),static_cast<std::uint16_t>(index)};std::uint16_t id=0xffff;
  if(g_bindings.resolve(candidate,id)&&id==vehicle){name=candidate;++matches;}
 }return matches==1;
}
bool Send(const state::Receipt& receipt) noexcept {
 unsigned char bytes[state::wireSize]{};if(!state::Encode(receipt,bytes,sizeof(bytes))||!g_bindings.send(g_localLink,bytes,sizeof(bytes)))return false;
 g_tx.fetch_add(1);g_sentAt=GetTickCount64();return true;
}
} // namespace

bool Install(const void* image,std::size_t span,bool accepted,const Bindings& bindings,Log log) noexcept {
 const Preserve preserve;if(g_installed.load())return true;
 if(g_base||!image||!accepted||span!=0xa01d000||!worker_scenario::Compiled()||!worker_scenario::ThreadAdmitted()||
 !bindings.link||!bindings.resolve||!bindings.send||
 !bindings.readOriginal)return false;
 g_base=reinterpret_cast<std::uintptr_t>(image);g_span=span;g_bindings=bindings;g_log=log;
 if(!Gate()){g_reason.store("code_gate");return false;}
 if(!BuildClone(g_clones[0],clones::code0,clones::unwind0,clones::reloc0,clones::branchOffset0,clones::branch0)||
 !BuildClone(g_clones[1],clones::code1,clones::unwind1,clones::reloc1,clones::branchOffset1,clones::branch1)||
 !BuildClone(g_clones[2],clones::code2,clones::unwind2,clones::reloc2,clones::branchOffset2,clones::branch2))return false;
 g_startClone=reinterpret_cast<StartFn>(g_clones[0].page);g_updateClone=reinterpret_cast<UpdateFn>(g_clones[1].page);g_predicateClone=reinterpret_cast<StartFn>(g_clones[2].page);
 const auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return false;
 void* wrappers[]={reinterpret_cast<void*>(&HookStart),reinterpret_cast<void*>(&HookUpdate),reinterpret_cast<void*>(&HookInit),reinterpret_cast<void*>(&HookRemotePredicate),reinterpret_cast<void*>(&HookPhase3)};
 void** originals[]={reinterpret_cast<void**>(&g_start),reinterpret_cast<void**>(&g_update),reinterpret_cast<void**>(&g_init),reinterpret_cast<void**>(&g_remotePredicate),reinterpret_cast<void**>(&g_phase3)};
 const std::uintptr_t rvas[]={0x124aab0,0x12471b0,0x125bfb0,0x114db20,0x124e140};
 for(unsigned i=0;i<5;++i){g_hooks[i].rva=rvas[i];g_hooks[i].detour=reinterpret_cast<std::uintptr_t>(wrappers[i]);}
 for(unsigned i=0;i<5;++i)if(!g_bindings.readOriginal(g_base+g_hooks[i].rva,g_originalHeads[i],16))return false;
 unsigned made=0;for(;made<5;++made)if(MH_CreateHook(reinterpret_cast<void*>(g_base+g_hooks[made].rva),wrappers[made],originals[made])!=MH_OK)break;
 if(made!=5){while(made)MH_RemoveHook(reinterpret_cast<void*>(g_base+g_hooks[--made].rva));return false;}
 unsigned enabled=0;for(;enabled<5;++enabled)if(MH_EnableHook(reinterpret_cast<void*>(g_base+g_hooks[enabled].rva))!=MH_OK)break;
 if(enabled!=5){for(unsigned i=0;i<enabled;++i)MH_DisableHook(reinterpret_cast<void*>(g_base+g_hooks[i].rva));for(const auto& h:g_hooks)MH_RemoveHook(reinterpret_cast<void*>(g_base+h.rva));return false;}
 auto raw=ReadBytes;
 for(unsigned i=0;i<5;++i){g_hooks[i].trampoline=reinterpret_cast<std::uintptr_t>(*originals[i]);
  if(!hooks::Seal(raw,g_base,g_hooks[i])){g_reason.store("install_ownership");g_stopped.store(true);return false;}}
 const auto serial=g_installTransactions.fetch_add(1,std::memory_order_acq_rel)+1;
 if(!serial){g_reason.store("install_serial_exhausted");g_stopped.store(true);return false;}
 g_installSerial.store(serial,std::memory_order_release);g_installed.store(true,std::memory_order_release);
 if(!PatchesLive()){g_reason.store("install_ownership");g_stopped.store(true);return false;}
g_reason.store("off");if(g_log)g_log("vehicle_mirror=installed; enabled=0; native_gate=private\r\n");return true;
}
bool SetEnabled(bool on) noexcept {
 const Preserve preserve;if(!g_installed.load()||g_stopped.load())return false;
 if(!on){if(g_purpose.serial||g_lease.phase!=state::Phase::idle)return false;g_enabled.store(false);return true;}
 if(!worker_scenario::ThreadAdmitted()||!PatchesLive()||!vehicle_native_protection::ProtectionLive()||
 !partner_input_hook::NativePoseWholePlayerLifetimeLive()||!partner_input_hook::NativePoseLocalCacheIsolationLive())return false;
 g_enabled.store(true);return true;
}
bool IsMessage(const unsigned char* bytes,std::size_t size) noexcept{return bytes&&size==state::wireSize&&bytes[0]=='V';}
bool ReceiveAuthenticated(const state::Link& messageLink,const unsigned char* bytes,std::size_t size,
 std::uint64_t authenticatedAcceptedTick) noexcept {
 const Preserve preserve;state::Receipt r{};state::Link live{};
 if(!g_installed.load()||!state::Decode(bytes,size,r)||!state::Fresh(GetTickCount64(),authenticatedAcceptedTick)||
  !g_bindings.link(live)||!state::Valid(messageLink)||!state::Same(messageLink,live))return false;
 AcquireSRWLockExclusive(&g_inboxLock);
 const bool accepted=state::ReceiveCaptured(g_inbox,live,messageLink,r,authenticatedAcceptedTick,GetTickCount64());
 ReleaseSRWLockExclusive(&g_inboxLock);
 if(accepted)g_rx.fetch_add(1);return accepted;
}
bool Before() noexcept {
 const Preserve preserve;if(!g_enabled.load())return true;if(g_clearing)return true;
 state::Inbox inbox{};Frame before{};
 const bool inboxRead=InboxCopy(inbox);
 if(!ReadFrame(before,g_purpose.serial!=0)){g_reason.store("owned_native_frame");return false;}
 if(!g_purpose.serial){
  if(!inboxRead||!before.stateScope.driveCurrent||!state::Same(before.link,inbox.link)||
   !state::Fresh(GetTickCount64(),inbox.received)||inbox.receipt.event!=state::Event::mounted)return true;
  std::uint16_t localVehicle=0xffff;state::Unit unit{};
  // Resolve the authored logical name before acquiring the genuine core hold.
  if(!g_bindings.resolve(inbox.receipt.name,localVehicle)||!UnitFor(before,localVehicle,0,unit)||!LookupReady(before,unit)){
   g_reason.store("resolve_local_vehicle");return true;}
  if(!ph::AcquireNativePoseVehicleLease(localVehicle,g_purpose)||!g_purpose.serial||
   g_purpose.generation!=before.context.generation||g_purpose.sceneEpoch!=before.context.sceneEpoch){
   g_reason.store(ph::NativePoseVehicleRefusalName());return false;}
  g_retainedVehicle=unit;g_vehicleRetained=true;
 }
 Frame current{};if(!ReadFrame(current)){g_reason.store("purpose_current");return false;}
 Backend backend{};const state::Receipt* driver=nullptr;
 if(current.stateScope.driveCurrent&&pd::Fresh(GetTickCount64(),g_localAt,750)&&state::Same(g_localLink,current.link)&&
  g_local.event==state::Event::mounted&&g_local.phase==3){
  state::Unit unit{};const auto id=vehicle_controller_read::Word(current.bodies.body[0].raw+0x44);
  if(current.bodies.body[0].raw[0x14]==3&&UnitFor(current,id,current.engine.bound[0],unit)&&unit.rider[0]==0)driver=&g_local;
 }
 // An unavailable/stale source requests native cleanup, not native identity
 // failure. ReadFrame still proves writer, Arm, both births and every owner.
 const bool attached=state::Step(backend,current.stateScope,inbox,g_lease,driver);
 AcquireSRWLockExclusive(&g_replyLock);g_reply.exiting=g_lease.exitPending;
 if(g_lease.exitPending)g_reply.driveAuthorized=false;ReleaseSRWLockExclusive(&g_replyLock);
 if(g_lease.phase==state::Phase::idle&&!g_clearing){
  if(!backend.Release(g_purpose.serial)){g_reason.store("empty_purpose_clear");return false;}}
 g_reason.store(g_lease.phase==state::Phase::quarantined?"quarantined":
  g_lease.exitPending?"native_exit":attached?"attached":"pending");
 return g_lease.phase!=state::Phase::quarantined;
}
void Closed(const ph::NativePoseJobContext& context,bool cleanupComplete) noexcept {
 const Preserve preserve;if(!g_clearing)return;
 if(!cleanupComplete){g_reason.store("cleanup_fault");return;}
 // Current(lease) deliberately needs a writer and is false outside it. Its
 // false result cannot prove release. The actual Closed context and core
 // successful even publication must instead show the hold is gone.
 const auto status=ph::NativePoseWindowStatus();
 if(status.held){g_reason.store("cleanup_pending");return;}
 if(!vehicle_mirror_close_read::Published(g_clearContext,context,ph::WindowSeq(),cleanupComplete,status)){
  g_reason.store("cleanup_publication");return;}
 g_purpose={};g_lease={};g_clearing=false;g_vehicleRetained=false;g_retainedVehicle={};g_clearContext={};
 g_posePurpose=0;g_poseMount=0;g_reason.store("retired");ClearReply();
}
void NativePoseAfter(std::uintptr_t primary,unsigned body,std::uintptr_t record,std::uintptr_t caller) noexcept {
 const Preserve preserve;if(!g_enabled.load()||body!=1||primary!=g_lease.primary||record!=g_lease.record||
 (caller!=g_base+0x124ee91&&caller!=g_base+0x125107d&&caller!=g_base+0x1250bf2))return;
 Frame f{};state::Unit unit{};if(!ReplicaCurrent(f)||!UnitFor(f,g_lease.unit.vehicle,g_lease.unit.unit,unit)||
 f.engine.bound[1]!=unit.unit||unit.rider[g_lease.source.seat]!=1||f.bodies.body[1].raw[0x14]!=g_lease.source.phase||
 f.ownership.callbackMask[1]!=3u)return;
 g_posePurpose=g_lease.purpose;g_poseMount=g_lease.source.mount;g_poses.fetch_add(1);
 ReplicaRide reply{};reply.attached=true;reply.name=g_lease.source.name;reply.link=g_lease.link;
 reply.phase=g_lease.source.phase;reply.seat=g_lease.source.seat;reply.mount=g_lease.source.mount;reply.observed=GetTickCount64();
 reply.lease=g_purpose;reply.player=f.context.player;reply.block=f.context.block;reply.birth=f.context.birthSerial;
 reply.exiting=g_lease.exitPending;reply.driveAuthorized=!reply.exiting&&SourceCurrent(f);
 AcquireSRWLockExclusive(&g_replyLock);g_reply=reply;ReleaseSRWLockExclusive(&g_replyLock);
}
bool ReadReplicaRide(ReplicaRide& out) noexcept {
 const Preserve preserve;out={};if(!g_enabled.load()||g_stopped.load())return false;
 AcquireSRWLockShared(&g_replyLock);const auto reply=g_reply;ReleaseSRWLockShared(&g_replyLock);
 if(!reply.attached||!reply.lease.serial||!worker_scenario::ThreadAdmitted()||!PatchesLive()||
  !vehicle_native_protection::ProtectionLive())return false;
 ph::PoseSharedLease shared{};const ph::PoseSharedLease* ticket=nullptr;
 if(!ph::NativePoseOwnedWindow()){if(!ph::EnterNativePoseShared(shared))return false;ticket=&shared;}
 ph::NativePoseVehicleOwnership owner{},late{};ph::NativePoseVehicleGuardBinding guard{};
 auto read=ReadBytes;state::Unit unit{},again{};
 const bool nativeCurrent=ph::ReadNativePoseVehicleOwnership(owner,ticket)&&ph::ReadNativePoseVehicleGuardBinding(guard,ticket)&&
  owner.lease.serial==reply.lease.serial&&owner.lease.generation==reply.lease.generation&&
  owner.lease.sceneEpoch==reply.lease.sceneEpoch&&owner.player==reply.player&&owner.block[1]==reply.block&&owner.birth[1]==reply.birth&&
  owner.currentVehicle[1]==guard.protection.vehicle&&owner.nativeBinding[1]==guard.protection.unit&&
  owner.seat[1]==reply.seat&&((owner.phase[1]==reply.phase&&owner.callbackMask[1]==3u)||owner.phase[1]==6)&&
  pd::ReadUnit(read,g_base,guard.protection.service,guard.protection.vehicle,guard.protection.unit,unit)&&
  pd::ReadUnit(read,g_base,guard.protection.service,guard.protection.vehicle,guard.protection.unit,again)&&pd::Same(unit,again)&&
  reply.seat<unit.count&&unit.rider[reply.seat]==1&&ph::ReadNativePoseVehicleOwnership(late,ticket)&&
  late.lease.serial==owner.lease.serial&&late.birth[0]==owner.birth[0]&&late.birth[1]==owner.birth[1]&&
  late.phase[1]==owner.phase[1]&&late.currentVehicle[1]==owner.currentVehicle[1]&&late.nativeBinding[1]==owner.nativeBinding[1];
 if(ticket)ph::ExitNativePoseShared(shared);if(!nativeCurrent)return false;
 state::Link current{};state::Inbox inbox{};
 const bool drive=!reply.exiting&&owner.phase[1]==reply.phase&&g_bindings.link(current)&&state::Same(current,reply.link)&&
  InboxCopy(inbox)&&state::Same(inbox.link,current)&&state::Fresh(GetTickCount64(),inbox.received)&&
  inbox.receipt.event==state::Event::mounted&&inbox.receipt.mount==reply.mount&&
  inbox.receipt.phase==reply.phase&&inbox.receipt.seat==reply.seat&&state::Same(inbox.receipt.name,reply.name);
 out=reply;out.driveAuthorized=drive;out.exiting=reply.exiting||owner.phase[1]==6;return true;
}
bool ReadNativeDemand(NativeDemand& out) noexcept {
 const Preserve preserve;out={};if(!g_enabled.load()||g_stopped.load()||!worker_scenario::ThreadAdmitted()||
  !PatchesLive()||!vehicle_native_protection::ProtectionLive())return false;
 // Enter the real shared domain. Guard retention alone is never permission
 // to retain a job; ReadOwnership independently proves current native owners.
 if(ph::NativePoseVehicleHeld()){
  ph::PoseSharedLease ticket{};if(!ph::EnterNativePoseShared(ticket))return false;
  ph::NativePoseVehicleOwnership a{},b{};
  const bool current=ph::ReadNativePoseVehicleOwnership(a,&ticket)&&g_purpose.serial&&g_vehicleRetained&&
   a.lease.serial==g_purpose.serial&&a.lease.generation==g_purpose.generation&&a.lease.sceneEpoch==g_purpose.sceneEpoch&&
   a.vehicle==g_retainedVehicle.vehicle&&a.purposes==4u&&!a.pending&&
   a.thread==GetCurrentThreadId()&&ph::ReadNativePoseVehicleOwnership(b,&ticket)&&
   b.lease.serial==a.lease.serial&&b.lease.generation==a.lease.generation&&b.lease.sceneEpoch==a.lease.sceneEpoch&&
   b.player==a.player&&b.data==a.data&&b.state==a.state&&b.block[1]==a.block[1]&&b.pool[1]==a.pool[1]&&
   b.birth[0]==a.birth[0]&&b.birth[1]==a.birth[1]&&b.birthLifecycle==a.birthLifecycle&&b.sequence==a.sequence&&
   b.phase[1]==a.phase[1]&&b.nextPhase[1]==a.nextPhase[1]&&b.currentVehicle[1]==a.currentVehicle[1]&&
   b.nativeBinding[1]==a.nativeBinding[1]&&b.seat[1]==a.seat[1]&&b.purposes==a.purposes&&b.pending==a.pending;
  ph::ExitNativePoseShared(ticket);if(!current)return false;
  out.retained=true;out.lease=b.lease;out.player=b.player;out.data=b.data;out.state=b.state;
  out.block=b.block[1];out.birth=b.birth[1];out.generation=b.lease.generation;out.sceneEpoch=b.lease.sceneEpoch;
  out.pool=static_cast<std::uint8_t>(b.pool[1]);return true;
 }
 state::Inbox inbox{};BodyObservation bodies{},lateBodies{};state::Link live{},lateLink{};
 pd::EngineRead engine{},lateEngine{};std::uintptr_t service=0,lateService=0;ph::NativePoseBirthCensus birth{},lateBirth{};
 if(!InboxCopy(inbox)||!state::Fresh(GetTickCount64(),inbox.received)||inbox.receipt.event!=state::Event::mounted||
  !Cold(bodies,live,engine,service)||!state::Same(live,inbox.link)||!ph::ReadNativePoseBirthCensus(birth)||
  birth.player!=bodies.player||birth.data!=bodies.data||birth.state!=bodies.state||birth.thread!=GetCurrentThreadId())return false;
 std::uint16_t id=0xffff;state::Unit unit{},again{};auto reader=ReadBytes;vehicle_registry_read::Lookup lookup{},lateLookup{};
 if(!g_bindings.resolve(inbox.receipt.name,id)||!pd::ReadUnit(reader,g_base,service,id,0,unit)||
  !pd::ReadUnit(reader,g_base,service,id,unit.unit,again)||!pd::Same(unit,again)||inbox.receipt.seat>=unit.count||
  (unit.rider[inbox.receipt.seat]!=0xffff&&unit.rider[inbox.receipt.seat]!=1)||
  !vehicle_registry_read::Observe(reader,g_base,g_span,engine,unit,lookup)||!vehicle_registry_read::Callable(reader,g_base,unit,lookup)||
  !vehicle_registry_read::Observe(reader,g_base,g_span,engine,unit,lateLookup)||!vehicle_registry_read::Same(lookup,lateLookup)||
  !vehicle_registry_read::Callable(reader,g_base,unit,lateLookup)||
  !Cold(lateBodies,lateLink,lateEngine,lateService)||!vehicle_controller_read::Same(bodies,lateBodies)||
  !state::Same(live,lateLink)||!pd::Same(engine,lateEngine)||service!=lateService||
  !ph::ReadNativePoseBirthCensus(lateBirth)||lateBirth.player!=birth.player||lateBirth.data!=birth.data||lateBirth.state!=birth.state||
  lateBirth.generation!=birth.generation||lateBirth.sceneEpoch!=birth.sceneEpoch||lateBirth.thread!=birth.thread||
  lateBirth.sequence!=birth.sequence||lateBirth.lifecycle!=birth.lifecycle)return false;
 for(unsigned k=0;k<2;++k)if(lateBirth.birth[k]!=birth.birth[k]||lateBirth.block[k]!=birth.block[k]||
  lateBirth.cs[k]!=birth.cs[k]||lateBirth.entry[k]!=birth.entry[k])return false;
 out.player=birth.player;out.data=birth.data;out.state=birth.state;out.block=birth.block[1];out.birth=birth.birth[1];
 out.generation=birth.generation;out.sceneEpoch=birth.sceneEpoch;out.pool=bodies.body[1].pool;return true;
}
void TickLocal() noexcept {
 const Preserve preserve;if(!g_enabled.load()||g_stopped.load())return;
 BodyObservation bodies{};pd::EngineRead engine{};std::uintptr_t service=0;state::Link live{};
 if(!Cold(bodies,live,engine,service))return;
 if(!state::Same(live,g_localLink)){g_localLink=live;g_local={};g_localUnit={};g_localMount=g_localSequence=0;g_localAt=g_sentAt=0;}
 const auto& body=bodies.body[0];const auto phase=body.raw[0x14];const auto seat=body.raw[0x17]&15u;const auto id=vehicle_controller_read::Word(body.raw+0x44);
 state::Unit unit{},again{};auto read=ReadBytes;state::Name name{};
 const bool mounted=((phase==3&&seat==0)||(phase==2&&(seat==2||seat==3)))&&engine.bound[0]&&
  pd::ReadUnit(read,g_base,service,id,engine.bound[0],unit)&&pd::ReadUnit(read,g_base,service,id,engine.bound[0],again)&&pd::Same(unit,again)&&
  seat<unit.count&&unit.rider[seat]==0&&NameFor(id,name);
 BodyObservation finalBodies{};pd::EngineRead finalEngine{};state::Link finalLink{};std::uintptr_t finalService=0;
 if(!Cold(finalBodies,finalLink,finalEngine,finalService)||!vehicle_controller_read::Same(bodies,finalBodies)||
  !pd::Same(engine,finalEngine)||!state::Same(live,finalLink)||service!=finalService)return;
 if(mounted){state::Unit finalUnit{};std::uint16_t resolved=0xffff;
  if(!g_bindings.resolve(name,resolved)||resolved!=id||!pd::ReadUnit(read,g_base,service,id,unit.unit,finalUnit)||!pd::Same(unit,finalUnit))return;
 }
 if(mounted){
  state::Inbox peer{};std::uint32_t driverMount=0;
  if(phase==2){if(!InboxCopy(peer)||!state::Same(live,peer.link)||!state::Fresh(GetTickCount64(),peer.received)||
    peer.receipt.event!=state::Event::mounted||peer.receipt.phase!=3||!state::Same(peer.receipt.name,name)||unit.rider[0]!=1)return;
   driverMount=peer.receipt.mount;
  }
  const bool fresh=g_local.event!=state::Event::mounted||!g_local.mount||!state::Same(name,g_local.name)||g_local.seat!=seat||
   !state::SameAllocation(g_localUnit,unit)||g_local.driverMount!=driverMount;
  if(fresh){if(g_localMount==UINT32_MAX||g_localSequence==UINT32_MAX)return;++g_localMount;g_local={};g_local.name=name;g_local.mount=g_localMount;
   g_local.role=live.localRole;g_local.mission=live.mission;g_local.epoch=live.epoch;g_local.event=state::Event::mounted;g_local.phase=phase;g_local.seat=static_cast<std::uint8_t>(seat);
  }
  g_local.driverMount=driverMount;g_localUnit=unit;g_local.cycle=body.raw[0x16];g_localAt=GetTickCount64();
 }else if(g_local.mount&&g_local.event==state::Event::mounted&&phase==0&&!engine.bound[0]){
  std::uint16_t previous=0xffff;if(!g_bindings.resolve(g_local.name,previous)||!pd::ReadUnit(read,g_base,service,previous,0,unit)||unit.rider[g_local.seat]!=0xffff)return;
  g_local.event=state::Event::exit;g_local.phase=0;g_localAt=GetTickCount64();
 }
 if(g_local.mount&&GetTickCount64()-g_sentAt>=250){if(g_localSequence==UINT32_MAX)return;g_local.sequence=++g_localSequence;Send(g_local);}
}
bool StatusLine(char* out,std::size_t cap) noexcept {
 const Preserve preserve;if(!out||!cap)return false;
 const int n=_snprintf_s(out,cap,_TRUNCATE,"vmirror;on=%u;en=%u;why=%s;state=%u;hold=%u;rx=%u;tx=%u;claim=%u;import=%u;bind=%u;pose=%u;nonlocal=%u;ui=%u",
 g_installed.load()?1u:0u,g_enabled.load()?1u:0u,g_reason.load(),unsigned(g_lease.phase),g_purpose.serial?1u:0u,
 g_rx.load(),g_tx.load(),g_claims.load(),g_imports.load(),g_binds.load(),g_poses.load(),g_remotePhysics.load(),g_uiSuppressed.load());return n>=0;
}
bool Shutdown() noexcept {
 const Preserve preserve;if(g_purpose.serial||g_lease.phase!=state::Phase::idle)return false;g_enabled.store(false);g_stopped.store(true);return true;
}
bool ReadUIEvent9Receipt(vehicle_native_protection::UIEvent9Receipt& out) noexcept {
 const Preserve preserve;out={};if(!PatchesLive())return false;auto read=ReadBytes;
 vehicle_native_protection::UIEvent9Receipt first{},last{};
 const auto serial=g_installSerial.load(std::memory_order_acquire);
 if(!hooks::Receipt(read,g_base,serial,g_hooks[4],first)||!hooks::Receipt(read,g_base,serial,g_hooks[4],last)||
  !hooks::Same(first,last)||!PatchesLive())return false;out=last;return true;
}
bool OverlayOriginal(std::uintptr_t at,void* bytes,std::size_t size) noexcept {
 const Preserve preserve;if(!bytes||!pd::Range(at,size))return false;
 if(!g_installed.load())return true;if(!PatchesLive())return false;
 auto read=ReadBytes;return hooks::Overlay(read,g_base,g_hooks,g_originalHeads,at,bytes,size);
}
} // namespace mgscoop::coop_vehicle_mirror
