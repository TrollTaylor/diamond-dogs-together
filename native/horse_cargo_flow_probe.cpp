#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include "horse_cargo_flow_probe.hpp"
#include "native_work_timing.hpp"
#include "horse_cargo_natural_flow.hpp"
#include "mgscoop/downed_player.hpp"
#include "mgscoop/world_link.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "vehicle_native_protection.hpp"
namespace mgscoop::horse_cargo_flow_probe {
namespace nf=horse_cargo_natural_flow;namespace hk=partner_input_hook;namespace horse=buddy_capacity_census;
namespace {
std::atomic<bool> prepared{false};std::uintptr_t image=0;std::size_t span=0;
SRWLOCK resultLock=SRWLOCK_INIT;nf::Result latest{};unsigned calls=0;
struct Preserve {DWORD value=GetLastError();~Preserve() noexcept {SetLastError(value);}};
bool Read(void*,std::uintptr_t at,void* out,std::size_t size) noexcept {SIZE_T done=0;
 return at&&size&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,size,&done)&&done==size;}
bool Buddy(std::uintptr_t base,std::uintptr_t& control,std::uint8_t& active) noexcept {
 auto raw=[](std::uintptr_t at,void* out,std::size_t size) noexcept {return Read(nullptr,at,out,size);};
 std::uintptr_t selector=0;std::uint8_t pending=0;
 return nf::cr::Get(raw,base+0x2c28930,selector)&&nf::cr::Get(raw,selector+0x10,control)&&
  nf::cr::Get(raw,control+0x175,active)&&active==1&&nf::cr::Get(raw,control+0x17f,pending)&&!pending;
}
bool Capture(const hk::NativeHorseFlowOwner& owner,const hk::NativeHorseFlowCall& call,nf::Sample& out,const char*& why) noexcept {
 native_work_timing::Scope measured(native_work_timing::Kind::horse_capture);
 out={};out.owner=owner;why="horse_flow_private";const auto now=GetTickCount64();
 if(!prepared.load(std::memory_order_acquire)||!worker_scenario::Compiled()||!worker_scenario::ThreadAdmitted()||
    owner.births.image!=image||owner.births.thread!=GetCurrentThreadId()||!owner.at||now<owner.at||now-owner.at>250||
    !nf::SameOwner(owner,owner))return false;
 auto original=vehicle_native_protection::ReadOriginal;std::uintptr_t mismatch=0;
 if(!nf::CodeGate(original,image,span,mismatch)){why="horse_flow_pins";return false;}
 const auto down=downed_player::GetSnapshot();const auto world=world_link::GetStatus();
 if(!down.ready||!down.armed||!down.lastPoll||now<down.lastPoll||now-down.lastPoll>750||
    !down.generation||!world.configured||!world.paired||!world.link_up||world.effective<3||!world.epoch||world.epoch!=down.epoch||
    (down.mission!=30010&&down.mission!=30020&&down.mission!=30050))return false;
 auto raw=[](std::uintptr_t at,void* outBytes,std::size_t size) noexcept {return Read(nullptr,at,outBytes,size);};
 std::uintptr_t vt=0,buddy=0;std::uint8_t active=0;
 if(call.service!=image+0x2c38008||!nf::cr::Get(raw,call.service,vt)||vt!=image+0x2305e70||
    !nf::ReadPacket(raw,call.message,out.packet)||!nf::ReadPlayer(raw,owner,out.carrier,out.player)||!Buddy(image,buddy,active))return false;
 const auto source=[&](horse::Census& census,const char*& reason) noexcept {
  const auto current=world_link::GetStatus();const auto own=downed_player::GetSnapshot();std::uintptr_t lateBuddy=0;std::uint8_t lateActive=0;
  if(!worker_scenario::ThreadAdmitted()||!current.configured||!current.paired||!current.link_up||current.effective<3||
     current.epoch!=world.epoch||own.mission!=down.mission||own.epoch!=down.epoch||own.generation!=down.generation||
     !own.ready||!own.armed||!Buddy(image,lateBuddy,lateActive)||lateBuddy!=buddy||lateActive!=active){reason="horse_flow_scene_changed";return false;}
  const horse::Authorization auth{true,true,true,true,true,true};
  const horse::Owner actual{owner.births.player,down.mission,down.generation,owner.births.thread,1,
   down.epoch,GetTickCount64(),true,true,false,false};
  reason=horse::ReadPlayPose({nullptr,Read},image,auth,actual,owner.births.thread,GetTickCount64(),census);return !reason;
 };
 if(!nf::hn::ObserveCurrent(raw,image,source,static_cast<std::uint16_t>(call.target),out.horse,why))return false;
 why="horse_flow_captured";return true;
}
struct Pending {bool active=false;nf::Sample before{};hk::NativeHorseFlowCall call{};};
thread_local Pending pending{};
void Publish(const nf::Result& value) noexcept {AcquireSRWLockExclusive(&resultLock);latest=value;++calls;ReleaseSRWLockExclusive(&resultLock);}
void Before(const hk::NativeHorseFlowOwner& owner,const hk::NativeHorseFlowCall& call) noexcept {
 Preserve preserve;if(pending.active)return;
 nf::Sample sample{};const char* why="horse_flow_capture";
 if(!Capture(owner,call,sample,why)){nf::Result failed{};failed.reason=why;Publish(failed);return;}
 if(!nf::NaturalSource(sample,call)){nf::Result failed{};failed.reason="horse_flow_source";Publish(failed);return;}
 pending={true,sample,call};
}
void After(const hk::NativeHorseFlowOwner* owner,const hk::NativeHorseFlowCall& call) noexcept {
 Preserve preserve;
 if(!pending.active||call.message!=pending.call.message||call.result!=pending.call.result||
    call.caller!=pending.call.caller||call.target!=pending.call.target)return;
 const auto accepted=pending;pending={};nf::Sample after{};const char* why="horse_flow_lost_read";std::int32_t reply=-1;
 auto raw=[](std::uintptr_t at,void* out,std::size_t size) noexcept {return Read(nullptr,at,out,size);};
 if(!owner||!Capture(*owner,call,after,why)||!nf::cr::Get(raw,call.result,reply)){
  nf::Result failed{};failed.kind=accepted.before.packet.kind;failed.victim=accepted.before.packet.body;
  failed.horse=accepted.before.horse.gameId;failed.reason=why;Publish(failed);return;}
 Publish(nf::Complete(accepted.before,after,call,reply));
}
void Refused(const hk::NativeHorseFlowCall& call) noexcept {
 Preserve preserve;
 if(!prepared.load(std::memory_order_acquire)||!worker_scenario::Compiled()||!worker_scenario::ThreadAdmitted())return;
 nf::Result failed{};failed.reason="horse_flow_owner_unavailable";
 failed.caller=call.caller>=image&&call.caller-image<=UINT32_MAX?static_cast<std::uint32_t>(call.caller-image):0;
 failed.horse=static_cast<std::uint16_t>(call.target);
 // No packet, slot or Player fields are sampled without a genuine owner.
 Publish(failed);
}
const hk::NativeHorseFlowCallbacks callbacks{Before,After,Refused};
}
bool Prepare(std::uintptr_t base,std::size_t size) noexcept {
 Preserve preserve;
 if(!worker_scenario::Compiled()||!worker_scenario::ThreadAdmitted()||!base||size<0xa01d000)return false;
 if(prepared.load(std::memory_order_acquire))return image==base&&span==size;
 hk::NativePoseBirthCensus current{};
 if(!hk::ReadNativePoseBirthCensus(current)||current.image!=base)return false;
 auto original=vehicle_native_protection::ReadOriginal;std::uintptr_t mismatch=0;
 if(!nf::CodeGate(original,base,size,mismatch))return false;
 image=base;span=size;
 if(!hk::RegisterNativeHorseFlowCallbacks(callbacks))return false;
 prepared.store(true,std::memory_order_release);return true;
}
bool Line(char* out,std::size_t capacity) noexcept {
 Preserve preserve;nf::Result copy{};unsigned count=0;
 AcquireSRWLockShared(&resultLock);copy=latest;count=calls;ReleaseSRWLockShared(&resultLock);
 return nf::Line(copy,count,out,capacity);
}
}
