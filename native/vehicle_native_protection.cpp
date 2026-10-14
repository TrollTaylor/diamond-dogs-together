#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "vehicle_native_protection.hpp"
#include "sleep_unique_bind_runtime.hpp"
#include "qualified_sleep_lookup_runtime.hpp"
#include "native_motion_frame_runtime.hpp"
#include "coop_vehicle_mirror.hpp"
#include "mgscoop/coop_carry.hpp"
#include "vehicle_native_protection_read.hpp"
#include "vehicle_native_protection_contract.hpp"
#include "vehicle_native_protection_gate_read.hpp"
#include "mgscoop/native_pose_window.hpp"
#include "mgscoop/worker_scenario.hpp"
#include "MinHook.h"
namespace mgscoop::vehicle_native_protection {
namespace {
namespace ph=partner_input_hook;
namespace c=vehicle_native_protection_contract;
using Word=std::uintptr_t;
using Void1=void(*)(void*);
using Void2=void(*)(void*,const void*);
using Index=void(*)(void*,std::uint32_t);
using Flag=void(*)(void*,std::uint32_t,std::uint8_t);
using Delete=void*(*)(void*,std::uint32_t);
using Getter=void*(*)(void*);
using Register=void(*)(void*,std::uint32_t,const void*);
struct Hook {Word rva=0,detour=0,trampoline=0,relay=0;unsigned char original[16]{},installed[16]{},relayBytes[14]{},trampolineBytes[64]{};bool made=false,enabled=false;};
Hook g_hooks[24]{};
Word g_image=0;std::size_t g_span=0;Bindings g_bindings{};
std::atomic<bool> g_installed{false},g_registered{false};
std::atomic<std::uint64_t> g_serial{0};
std::atomic<std::uint64_t> g_transactions{0};
std::atomic<std::uint32_t> g_refused{0},g_shadows{0},g_filtered{0};
std::atomic<const char*> g_reason{"off"};
thread_local pure::Cleanup t_cleanup{};
struct CallbackRegistration {std::uint64_t lease=0,birth[2]{};Word entry=0,owner=0,function=0,descriptor=0;std::uint64_t event=0;unsigned body=0,depth=0;};
thread_local CallbackRegistration t_registration{};
alignas(16) thread_local unsigned char t_shadow[0x550]{};
bool Bytes(Word at,void* out,std::size_t n)noexcept {
 SIZE_T got=0;return coop_passenger::detail::Range(at,n)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,n,&got)&&got==n;
}
bool Relay(Word entry,Word detour,Word& relay,unsigned char bytes[14])noexcept {
 unsigned char head[5]{};if(!Bytes(entry,head,sizeof(head))||head[0]!=0xe9)return false;
 std::int32_t displacement=0;std::memcpy(&displacement,head+1,4);relay=entry+5+static_cast<std::intptr_t>(displacement);
 if(!Bytes(relay,bytes,14)||bytes[0]!=0xff||bytes[1]!=0x25||bytes[2]||bytes[3]||bytes[4]||bytes[5])return false;
 Word target=0;std::memcpy(&target,bytes+6,8);return target==detour;
}
bool HookLive(const Hook& h)noexcept {
 unsigned char head[16]{},relayBytes[14]{},trampoline[64]{};Word relay=0;
 return h.enabled&&h.trampoline&&Bytes(h.trampoline,trampoline,sizeof(trampoline))&&!std::memcmp(trampoline,h.trampolineBytes,sizeof(trampoline))&&
  Bytes(g_image+h.rva,head,16)&&!std::memcmp(head,h.installed,16)&&
  Relay(g_image+h.rva,h.detour,relay,relayBytes)&&relay==h.relay&&!std::memcmp(relayBytes,h.relayBytes,14);
}
bool HeadsLive()noexcept {
 if(!g_installed.load(std::memory_order_acquire)||!g_serial.load())return false;
 for(const auto& h:g_hooks)if(!HookLive(h))return false;return true;
}
bool Event9Live()noexcept {
 UIEvent9Receipt a{},b{};unsigned char head[16]{},relay[14]{};Word target=0;
 if(!g_bindings.event9||!g_bindings.event9(a)||!a.installationSerial||a.image!=g_image||a.entry!=g_image+0x124e140||!a.detour||
  !Bytes(a.entry,head,16)||std::memcmp(head,a.entryBytes,16)||!Relay(a.entry,a.detour,target,relay)||target!=a.relay||
  std::memcmp(relay,a.relayBytes,14)||!g_bindings.event9(b))return false;
 return a.installationSerial==b.installationSerial&&a.image==b.image&&a.entry==b.entry&&a.detour==b.detour&&a.relay==b.relay&&
  !std::memcmp(a.entryBytes,b.entryBytes,16)&&!std::memcmp(a.relayBytes,b.relayBytes,14);
}
bool CoreHeadsLive()noexcept {
 if(!g_registered.load()||!ph::NativePoseWholePlayerLifetimeLive())return false;
 unsigned char actual[16]{},original[16]{};
 for(const auto rva:c::coreHeads){if(!Bytes(g_image+rva,actual,16)||actual[0]!=0xe9||
   !ph::ReadNativePoseOriginalCode(g_image+rva,original,16)||!std::memcmp(actual,original,5))return false;}
 return true;
}
bool Gate()noexcept {
 auto original=ReadOriginal;auto raw=Bytes;
 // Full pinned spans are still checked on every invocation; one bounded
 // read per small function avoids repeatedly rebuilding the original view.
 return vehicle_native_protection_gate_read::Gate(original,raw,g_image,g_span);
}
void Fault(const char* why)noexcept {g_reason.store(why);g_refused.fetch_add(1);if(g_bindings.fault)g_bindings.fault(why);}
struct Domain {ph::PoseSharedLease shared{};ph::NativePoseVehicleGuardBinding binding{};bool entered=false,guard=false;};
bool Enter(Domain& d)noexcept {
 d={};if(ph::NativePoseOwnedWindow()){d.entered=true;d.guard=ph::ReadNativePoseVehicleGuardBinding(d.binding);return true;}
 if(!ph::EnterNativePoseShared(d.shared))return false;
 d.entered=true;d.guard=ph::ReadNativePoseVehicleGuardBinding(d.binding,&d.shared);return true;
}
void Leave(Domain& d)noexcept {if(d.shared.tracked)ph::ExitNativePoseShared(d.shared);d.entered=false;}
bool Refuse(Domain& d,Word rva,void* object,std::uint32_t index=UINT32_MAX)noexcept {
 if(!d.guard)return false;
 const bool denied=(rva==0xbff120||rva==0xdcbc0)?pure::Free(d.binding,reinterpret_cast<Word>(object)):
  pure::Mutation(d.binding,rva,reinterpret_cast<Word>(object),index);
 if(denied)Fault("vehicle_owned_lifetime_refused");return denied;
}
bool Current(ph::NativePoseVehicleOwnership& o)noexcept {
 return ph::NativePoseOwnedWindow()&&ph::ReadNativePoseVehicleOwnership(o)&&o.thread==GetCurrentThreadId()&&
  (o.sequence&1u)&&(o.purposes&4u)&&HeadsLive()&&CoreHeadsLive()&&Event9Live();
}
bool CleanupOwner(void* object,unsigned body,pure::Cleanup& out)noexcept {
 out={};ph::NativePoseVehicleOwnership o{};pure::Witness w{};auto read=Bytes;
 if(body!=1||!Current(o)||reinterpret_cast<Word>(object)!=o.controller[1]+0x30||
  !ph::NativePoseVehicleCleanupCurrent(o.lease)||!pure::Observe(read,o,w))return false;
 out.ownership=o;out.protection=pure::Receipt(o,w,g_serial.load(),ph::nativeVehicleAllGuards);
 out.controllerInterface=reinterpret_cast<Word>(object);out.thread=GetCurrentThreadId();out.depth=1;return true;
}
bool CleanupLeaf(Word rva,void* object,Word caller,unsigned body)noexcept {
 ph::NativePoseVehicleOwnership o{};pure::Witness w{};auto read=Bytes;
 return t_cleanup.depth&&Current(o)&&ph::NativePoseVehicleCleanupCurrent(o.lease)&&pure::Observe(read,o,w)&&
  w.camera.interface==t_cleanup.protection.cameraInterface&&w.ui.interface==t_cleanup.protection.uiInterface&&
  w.ui.provider==t_cleanup.protection.uiProvider&&
  pure::CleanupLeaf(t_cleanup,o,rva,reinterpret_cast<Word>(object),caller,body,GetCurrentThreadId());
}
// Generated wrappers below contain only exact typed entry filters. All
// unrelated calls forward their original arguments once, and preserve its
// return and LastError. Refusal retains the original physical seat obligation.
// GENERATED_WRAPPERS
__declspec(noinline) void Hook0(void* object,const void* config) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void2>(g_hooks[0].trampoline)(object,config);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xb2e840,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void2>(g_hooks[0].trampoline)(object,config);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook1(void* object) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[1].trampoline)(object);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xb2d750,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[1].trampoline)(object);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook2(void* object) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[2].trampoline)(object);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xb361b0,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[2].trampoline)(object);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook3(void* object) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[3].trampoline)(object);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xb2f630,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[3].trampoline)(object);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook4(void* object,std::uint32_t index,const void* descriptor) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Register>(g_hooks[4].trampoline)(object,index,descriptor);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xbf98d0,object,index)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Register>(g_hooks[4].trampoline)(object,index,descriptor);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook5(void* object,const void* config) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void2>(g_hooks[5].trampoline)(object,config);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0x16f9580,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void2>(g_hooks[5].trampoline)(object,config);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook6(void* object) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[6].trampoline)(object);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0x16fb950,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[6].trampoline)(object);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void* Hook7(void* object,std::uint32_t flags) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);const auto result=reinterpret_cast<Delete>(g_hooks[7].trampoline)(object,flags);return result;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return object;}
 if(Refuse(d,0x16f63c0,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return object;}
 DWORD outgoing=incoming;void* result=nullptr;
 __try{SetLastError(incoming);result=reinterpret_cast<Delete>(g_hooks[7].trampoline)(object,flags);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}
 return result;
}
__declspec(noinline) void Hook8(void* object,std::uint32_t index) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Index>(g_hooks[8].trampoline)(object,index);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0x16f7c30,object,index)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Index>(g_hooks[8].trampoline)(object,index);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook9(void* object,std::uint32_t index) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Index>(g_hooks[9].trampoline)(object,index);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0x16f82c0,object,index)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Index>(g_hooks[9].trampoline)(object,index);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook10(void* object,std::uint32_t index) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Index>(g_hooks[10].trampoline)(object,index);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xb2ac60,object,index)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Index>(g_hooks[10].trampoline)(object,index);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void* Hook11(void* object,std::uint32_t flags) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);const auto result=reinterpret_cast<Delete>(g_hooks[11].trampoline)(object,flags);return result;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return object;}
 if(Refuse(d,0x129e1c0,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return object;}
 DWORD outgoing=incoming;void* result=nullptr;
 __try{SetLastError(incoming);result=reinterpret_cast<Delete>(g_hooks[11].trampoline)(object,flags);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}
 return result;
}
__declspec(noinline) void Hook12(void* object,const void* config) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void2>(g_hooks[12].trampoline)(object,config);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0x129e9d0,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void2>(g_hooks[12].trampoline)(object,config);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook13(void* object) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[13].trampoline)(object);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0x94b650,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[13].trampoline)(object);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook18(void* object) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[18].trampoline)(object);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xbff120,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[18].trampoline)(object);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook19(void* object) {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[19].trampoline)(object);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(Refuse(d,0xdcbc0,object,UINT32_MAX)){Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;
 __try{SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[19].trampoline)(object);outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}

}
__declspec(noinline) void Hook20() {
 const DWORD incoming=GetLastError();Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);reinterpret_cast<void(*)()>(g_hooks[20].trampoline)();return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 if(d.guard&&pure::Held(d.binding)&&d.binding.protection.uiProvider==g_image+0x2a5d110){Fault("vehicle_ui_provider_reinitialize");Leave(d);SetLastError(incoming);return;}
 DWORD outgoing=incoming;__try{SetLastError(incoming);reinterpret_cast<void(*)()>(g_hooks[20].trampoline)();outgoing=GetLastError();}__finally{Leave(d);SetLastError(outgoing);}
}
__declspec(noinline) void Hook14(void* object,std::uint32_t body) {
 const DWORD incoming=GetLastError();Domain d{};
 const auto original=reinterpret_cast<Index>(g_hooks[14].trampoline);
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);original(object,body);return;}
 if(!Enter(d)){Fault("vehicle_domain_unavailable");SetLastError(incoming);return;}
 const auto previous=t_cleanup;t_cleanup={};pure::Cleanup current{};
 const bool owned=body==1&&d.guard&&pure::Held(d.binding)&&reinterpret_cast<Word>(object)==d.binding.retained.controller[1]+0x30;
 if(owned&&!CleanupOwner(object,body,current)){t_cleanup=previous;Leave(d);Fault("vehicle_cleanup_owner_refused");SetLastError(incoming);return;}
 if(owned)t_cleanup=current;DWORD outgoing=incoming;
 __try{SetLastError(incoming);original(object,body);outgoing=GetLastError();}
 __finally{t_cleanup=previous;Leave(d);SetLastError(outgoing);}
}
__declspec(noinline) void Hook15(void* object) {
 const DWORD incoming=GetLastError();const auto caller=reinterpret_cast<Word>(_ReturnAddress());
 if(CleanupLeaf(0x129f590,object,caller,1)){g_filtered.fetch_add(1);SetLastError(incoming);return;}
 if(t_cleanup.depth==1&&caller==g_image+0x124cc44&&reinterpret_cast<Word>(object)==t_cleanup.protection.cameraInterface){
  Fault("vehicle_cleanup_camera_scope_lost");SetLastError(incoming);return;}
 SetLastError(incoming);reinterpret_cast<Void1>(g_hooks[15].trampoline)(object);
}
__declspec(noinline) void Hook16(void* object,std::uint32_t body,std::uint8_t bit) {
 const DWORD incoming=GetLastError();const auto caller=reinterpret_cast<Word>(_ReturnAddress());
 if(CleanupLeaf(0xfe7000,object,caller,body)){g_filtered.fetch_add(1);SetLastError(incoming);return;}
 if(t_cleanup.depth==1&&body==1&&caller==g_image+0x124ccfe&&reinterpret_cast<Word>(object)==t_cleanup.protection.uiInterface){
  Fault("vehicle_cleanup_ui_scope_lost");SetLastError(incoming);return;}
 SetLastError(incoming);reinterpret_cast<Flag>(g_hooks[16].trampoline)(object,body,bit);
}
__declspec(noinline) void* Hook17(void* object) {
 const DWORD incoming=GetLastError();const auto caller=reinterpret_cast<Word>(_ReturnAddress());
 SetLastError(incoming);void* result=reinterpret_cast<Getter>(g_hooks[17].trampoline)(object);const DWORD outgoing=GetLastError();
 const bool candidate=t_cleanup.depth==1&&caller==g_image+0x124ca59&&reinterpret_cast<Word>(object)==t_cleanup.protection.uiInterface;
 const bool qualified=CleanupLeaf(0xfe71b0,object,caller,1);
 if(candidate&&!qualified)Fault("vehicle_cleanup_ui_scope_lost");
 if(candidate){
  if(reinterpret_cast<Word>(result)!=t_cleanup.protection.uiProvider+0x10)Fault("vehicle_ui_buffer_changed");
  // Exact complete124C850 uses RAX only for the four DWORD stores544/4A4/4CC/4F4
  // at124CA59..CA6B; then ceases to use this pointer. No global provider writes.
  g_shadows.fetch_add(1);result=t_shadow;
 }
 SetLastError(outgoing);return result;
}

using CallbackAdd=void(*)(void*,std::uint32_t,std::uint64_t,const void*);
using CallbackRemove=void(*)(void*,std::uint64_t,const void*);
bool Descriptor(const void* data,Word& function,Word& owner)noexcept {
 auto address=reinterpret_cast<Word>(data);return Bytes(address,&function,8)&&Bytes(address+8,&owner,8);
}
__declspec(noinline) void Hook21(void* entry,std::uint32_t body,std::uint64_t event,const void* descriptor) {
 const DWORD incoming=GetLastError();const auto original=reinterpret_cast<CallbackAdd>(g_hooks[21].trampoline);Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);original(entry,body,event,descriptor);return;}
 if(!Enter(d)){Fault("vehicle_callback_domain");SetLastError(incoming);return;}
 Word function=0,owner=0;ph::NativePoseVehicleOwnership o{};const auto previous=t_registration;t_registration={};
 const bool candidate=d.guard&&pure::Held(d.binding)&&reinterpret_cast<Word>(entry)==d.binding.retained.entry[1];
 if(candidate&&(!Descriptor(descriptor,function,owner))){t_registration=previous;Leave(d);Fault("vehicle_callback_descriptor");SetLastError(incoming);return;}
 const bool related=candidate&&(owner==d.binding.retained.controller[1]||function==g_image+0x124a530);
 if(related){
  if(!pure::VehicleCallback(d.binding,reinterpret_cast<Word>(entry),owner,function,body,event)||!Current(o)||
   o.entry[1]!=reinterpret_cast<Word>(entry)||o.controller[1]!=owner){t_registration=previous;Leave(d);Fault("vehicle_callback_add_refused");SetLastError(incoming);return;}
  t_registration.lease=o.lease.serial;t_registration.birth[0]=o.birth[0];t_registration.birth[1]=o.birth[1];
  t_registration.entry=reinterpret_cast<Word>(entry);t_registration.owner=owner;t_registration.function=function;
  t_registration.descriptor=reinterpret_cast<Word>(descriptor);t_registration.event=event;t_registration.body=body;t_registration.depth=1;
 }
 DWORD outgoing=incoming;__try{SetLastError(incoming);original(entry,body,event,descriptor);outgoing=GetLastError();}
 __finally{t_registration=previous;Leave(d);SetLastError(outgoing);}
}
__declspec(noinline) void Hook22(void* entry,std::uint64_t event,const void* owner) {
 const DWORD incoming=GetLastError();const auto caller=reinterpret_cast<Word>(_ReturnAddress());Domain d{};
 const auto original=reinterpret_cast<CallbackRemove>(g_hooks[22].trampoline);
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);original(entry,event,owner);return;}
 if(!Enter(d)){Fault("vehicle_callback_domain");SetLastError(incoming);return;}
 const bool related=d.guard&&pure::Held(d.binding)&&reinterpret_cast<Word>(entry)==d.binding.retained.entry[1]&&
  reinterpret_cast<Word>(owner)==d.binding.retained.controller[1];
 if(related){ph::NativePoseVehicleOwnership o{};
  if(!pure::Event(event)||caller!=g_image+0xaf37d4||!Current(o)||!ph::NativePoseVehicleCleanupCurrent(o.lease)||
   !pure::CleanupCurrent(t_cleanup,o,GetCurrentThreadId())){Leave(d);Fault("vehicle_callback_remove_refused");SetLastError(incoming);return;}}
 DWORD outgoing=incoming;__try{SetLastError(incoming);original(entry,event,owner);outgoing=GetLastError();}
 __finally{Leave(d);SetLastError(outgoing);}
}
__declspec(noinline) void Hook23(void* slot,std::uint32_t body,std::uint64_t event,const void* descriptor) {
 const DWORD incoming=GetLastError();const auto original=reinterpret_cast<CallbackAdd>(g_hooks[23].trampoline);Domain d{};
 if(!ph::NativePoseVehicleHeld()){SetLastError(incoming);original(slot,body,event,descriptor);return;}
 if(!Enter(d)){Fault("vehicle_callback_domain");SetLastError(incoming);return;}
 if(d.guard&&pure::CallbackSlot(d.binding,reinterpret_cast<Word>(slot))){
  Word function=0,owner=0,oldFunction=0,oldOwner=0;ph::NativePoseVehicleOwnership o{};
  const bool readable=Descriptor(descriptor,function,owner)&&Bytes(reinterpret_cast<Word>(slot)+0x40,&oldFunction,8)&&
   Bytes(reinterpret_cast<Word>(slot)+0x48,&oldOwner,8);
  const bool related=readable&&(owner==d.binding.retained.controller[1]||function==g_image+0x124a530||
   oldOwner==d.binding.retained.controller[1]||oldFunction==g_image+0x124a530);
  if(!readable||(related&&(!Current(o)||!pure::VehicleCallback(d.binding,o.entry[1],owner,function,body,event)||
   t_registration.depth!=1||t_registration.lease!=o.lease.serial||t_registration.birth[0]!=o.birth[0]||t_registration.birth[1]!=o.birth[1]||
   t_registration.entry!=o.entry[1]||t_registration.owner!=owner||t_registration.function!=function||
   t_registration.descriptor!=reinterpret_cast<Word>(descriptor)||t_registration.body!=body||t_registration.event!=event))){
    Leave(d);Fault("vehicle_callback_slot_refused");SetLastError(incoming);return;}}
 DWORD outgoing=incoming;__try{SetLastError(incoming);original(slot,body,event,descriptor);outgoing=GetLastError();}
 __finally{Leave(d);SetLastError(outgoing);}
}

void ConfigureHooks() noexcept {
g_hooks[0].rva=0xb2e840;g_hooks[0].detour=reinterpret_cast<Word>(&Hook0);
g_hooks[1].rva=0xb2d750;g_hooks[1].detour=reinterpret_cast<Word>(&Hook1);
g_hooks[2].rva=0xb361b0;g_hooks[2].detour=reinterpret_cast<Word>(&Hook2);
g_hooks[3].rva=0xb2f630;g_hooks[3].detour=reinterpret_cast<Word>(&Hook3);
g_hooks[4].rva=0xbf98d0;g_hooks[4].detour=reinterpret_cast<Word>(&Hook4);
g_hooks[5].rva=0x16f9580;g_hooks[5].detour=reinterpret_cast<Word>(&Hook5);
g_hooks[6].rva=0x16fb950;g_hooks[6].detour=reinterpret_cast<Word>(&Hook6);
g_hooks[7].rva=0x16f63c0;g_hooks[7].detour=reinterpret_cast<Word>(&Hook7);
g_hooks[8].rva=0x16f7c30;g_hooks[8].detour=reinterpret_cast<Word>(&Hook8);
g_hooks[9].rva=0x16f82c0;g_hooks[9].detour=reinterpret_cast<Word>(&Hook9);
g_hooks[10].rva=0xb2ac60;g_hooks[10].detour=reinterpret_cast<Word>(&Hook10);
g_hooks[11].rva=0x129e1c0;g_hooks[11].detour=reinterpret_cast<Word>(&Hook11);
g_hooks[12].rva=0x129e9d0;g_hooks[12].detour=reinterpret_cast<Word>(&Hook12);
g_hooks[13].rva=0x94b650;g_hooks[13].detour=reinterpret_cast<Word>(&Hook13);
g_hooks[14].rva=0x124c850;g_hooks[14].detour=reinterpret_cast<Word>(&Hook14);
g_hooks[15].rva=0x129f590;g_hooks[15].detour=reinterpret_cast<Word>(&Hook15);
g_hooks[16].rva=0xfe7000;g_hooks[16].detour=reinterpret_cast<Word>(&Hook16);
g_hooks[17].rva=0xfe71b0;g_hooks[17].detour=reinterpret_cast<Word>(&Hook17);
g_hooks[18].rva=0xbff120;g_hooks[18].detour=reinterpret_cast<Word>(&Hook18);
g_hooks[19].rva=0xdcbc0;g_hooks[19].detour=reinterpret_cast<Word>(&Hook19);
g_hooks[20].rva=0x1ffe200;g_hooks[20].detour=reinterpret_cast<Word>(&Hook20);
g_hooks[21].rva=0xb6e550;g_hooks[21].detour=reinterpret_cast<Word>(&Hook21);
g_hooks[22].rva=0xb6fb80;g_hooks[22].detour=reinterpret_cast<Word>(&Hook22);
g_hooks[23].rva=0xb9aba0;g_hooks[23].detour=reinterpret_cast<Word>(&Hook23);
}
bool Provider(ph::NativePoseVehicleProtectionQuery query,const ph::NativePoseVehicleOwnership& o,ph::NativePoseVehicleProtection& out)noexcept {
 const DWORD error=GetLastError();out={};bool accepted=false;pure::Witness w{};auto read=Bytes;
 if(o.image==g_image&&HeadsLive()&&CoreHeadsLive()&&Event9Live()&&Gate()&&pure::Observe(read,o,w)){
  const auto& b=w.controllers.body[1];
  bool peerSeat=false;for(unsigned seat=0;seat<w.unit.count;++seat)if(w.unit.rider[seat]==1)peerSeat=true;
  const bool retired=!peerSeat&&vehicle_lifetime_read::Retired(w.callbacks,1)&&!w.engine.bound[1]&&
   !(b.bitmapValue&1u)&&!b.raw[0x14]&&!b.raw[0x15]&&
   vehicle_controller_read::Word(b.raw+0x44)==0xffff&&vehicle_controller_read::Word(b.raw+0x46)==0xffff;
  if(query!=ph::NativePoseVehicleProtectionQuery::retired||retired){
   out=pure::Receipt(o,w,g_serial.load(),ph::nativeVehicleAllGuards);accepted=true;}}
 if(!accepted)g_reason.store("vehicle_typed_protection_refused");SetLastError(error);return accepted;
}
ph::NativePoseVehicleAllocatorDisposition Allocator(const ph::NativePoseVehicleGuardBinding& b,Word header)noexcept {
 return pure::Allocator(b,header)?ph::NativePoseVehicleAllocatorDisposition::refuse:ph::NativePoseVehicleAllocatorDisposition::unchanged;
}
}
bool ReadOriginal(Word at,void* out,std::size_t size)noexcept {
 const DWORD error=GetLastError();bool okay=ph::ReadNativePoseOriginalCode(at,out,size);
 if(okay)okay=coop_passenger::OverlayOriginal(at,out,size);
 if(okay)okay=coop_carry::OverlayOriginal(at,out,size);
 if(okay){auto* bytes=static_cast<unsigned char*>(out);
  for(const auto& h:g_hooks){if(!h.enabled)continue;const auto begin=g_image+h.rva,end=begin+5;
   if(at<end&&begin<at+size){if(!HookLive(h)){okay=false;break;}const auto lo=at>begin?at:begin,hi=at+size<end?at+size:end;
    std::memcpy(bytes+(lo-at),h.original+(lo-begin),hi-lo);}}}
 if(okay)okay=coop_vehicle_mirror::OverlayOriginal(at,out,size);
 if(okay)okay=sleep_unique_bind_runtime::OverlayOriginal(at,out,size);
 if(okay)okay=qualified_sleep_lookup_runtime::OverlayOriginal(at,out,size);
 if(okay)okay=native_motion_frame_runtime::OverlayOriginal(at,out,size);
 SetLastError(error);return okay;
}
bool Install(const void* image,std::size_t span,bool accepted,const Bindings& bindings)noexcept {
 const DWORD error=GetLastError();if(g_installed.load()){const bool okay=ProtectionLive();SetLastError(error);return okay;}
 if(!accepted||!image||span!=0xa01d000||!bindings.event9||!bindings.fault||!worker_scenario::ThreadAdmitted()){
  g_reason.store("vehicle_protection_private_gate");SetLastError(error);return false;}
 g_image=reinterpret_cast<Word>(image);g_span=span;g_bindings=bindings;ConfigureHooks();
 if(!Gate()){g_reason.store("vehicle_protection_code_gate");SetLastError(error);return false;}
 for(auto& h:g_hooks){if(!ph::ReadNativePoseOriginalCode(g_image+h.rva,h.original,16)||
   MH_CreateHook(reinterpret_cast<void*>(g_image+h.rva),reinterpret_cast<void*>(h.detour),reinterpret_cast<void**>(&h.trampoline))!=MH_OK){
    for(auto& old:g_hooks)if(old.made)MH_RemoveHook(reinterpret_cast<void*>(g_image+old.rva));
    g_reason.store("vehicle_protection_create");SetLastError(error);return false;}h.made=true;
  if(!Bytes(h.trampoline,h.trampolineBytes,sizeof(h.trampolineBytes))){
   for(auto& old:g_hooks)if(old.made)MH_RemoveHook(reinterpret_cast<void*>(g_image+old.rva));
   g_reason.store("vehicle_protection_trampoline");SetLastError(error);return false;}}
 ph::NativePoseVehicleProtectionBindings provider{};provider.provider=Provider;provider.readOriginal=ReadOriginal;provider.allocator=Allocator;
 if(!ph::RegisterNativePoseVehicleProtectionProvider(provider)){
  for(auto& h:g_hooks)MH_RemoveHook(reinterpret_cast<void*>(g_image+h.rva));g_reason.store("vehicle_protection_bind");SetLastError(error);return false;}
 g_registered.store(true);
 for(auto& h:g_hooks){if(MH_EnableHook(reinterpret_cast<void*>(g_image+h.rva))!=MH_OK||!Bytes(g_image+h.rva,h.installed,16)||
   !Relay(g_image+h.rva,h.detour,h.relay,h.relayBytes)){
    // Already exposed trampolines/relays are retained until this private
    // process exits. No concurrent callback may jump into freed scratch code.
    g_reason.store("vehicle_protection_install_incomplete");SetLastError(error);return false;}h.enabled=true;}
 const auto serial=g_transactions.fetch_add(1,std::memory_order_acq_rel)+1;
 if(!serial){g_reason.store("vehicle_protection_serial_exhausted");SetLastError(error);return false;}
 g_serial.store(serial,std::memory_order_release);g_installed.store(true,std::memory_order_release);
 const bool okay=HeadsLive()&&CoreHeadsLive();g_reason.store(okay?"vehicle_protection_off":"vehicle_protection_ownership");
 SetLastError(error);return okay;
}
bool Installed()noexcept {const DWORD e=GetLastError();const bool okay=HeadsLive();SetLastError(e);return okay;}
bool ProtectionLive()noexcept {const DWORD e=GetLastError();const bool okay=HeadsLive()&&CoreHeadsLive()&&Event9Live()&&Gate();SetLastError(e);return okay;}
bool StatusLine(char* out,std::size_t n)noexcept {
 const DWORD e=GetLastError();const bool native=HeadsLive(),core=CoreHeadsLive(),ui=Event9Live();
 const int wrote=std::snprintf(out,n,"vehicleProtect;on=%u;serial=%llu;heads=%u;core=%u;ui=%u;deny=%u;shadow=%u;leaf=%u;why=%s",
  g_installed.load()?1u:0u,static_cast<unsigned long long>(g_serial.load()),native?1u:0u,core?1u:0u,ui?1u:0u,
  g_refused.load(),g_shadows.load(),g_filtered.load(),g_reason.load());SetLastError(e);return wrote>=0&&static_cast<std::size_t>(wrote)<n;
}
}
