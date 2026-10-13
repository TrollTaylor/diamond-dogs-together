// Include after origin wrappers, inside the actual core anonymous namespace.
namespace ql=carry_queue_lifetime;
ql::Owner g_pose_queue_owner{};bool g_pose_queue_bound=false;
void PoseQueuePublishClear()noexcept{PoseDeferredPublishClear();g_pose_queue_owner={};g_pose_queue_bound=false;}
using PoseQueueCtorFn=void*(*)(void*);
using PoseQueueDeleteFn=void*(*)(void*,std::uint32_t);
using PoseQueueRaw1Fn=std::uintptr_t(*)(void*);
using PoseQueueRaw2Fn=std::uintptr_t(*)(void*,void*);
using PoseQueueCountFn=std::uintptr_t(*)(void*,std::uint32_t);
PoseQueueCtorFn g_pose_queue_ctor=nullptr;PoseQueueDeleteFn g_pose_queue_delete=nullptr;
PoseQueueRaw1Fn g_pose_queue_destruct=nullptr,g_pose_queue_release=nullptr,g_pose_queue_free=nullptr,g_pose_queue_allocator_free=nullptr;
PoseQueueRaw2Fn g_pose_queue_init=nullptr;PoseQueueCountFn g_pose_queue_allocate=nullptr;
bool PoseQueueCode(std::uintptr_t& mismatch)noexcept{
 mismatch=0;unsigned char bytes[8192]{};
 for(const auto& s:carry_queue_lifetime_contract::sites){if(s.rva>0xa01d000||s.size>0xa01d000-s.rva||s.size>sizeof(bytes)||
 !PoseOriginalCode(g_base+s.rva,bytes,s.size)||std::memcmp(bytes,s.bytes,s.size)){mismatch=s.rva;return false;}}
 for(const auto& s:carry_queue_lifetime_contract::slots){std::uintptr_t got=0;if(s.rva>0xa01d000||8>0xa01d000-s.rva||!PoseRead(g_base+s.rva,got)||got!=g_base+s.target){mismatch=s.rva;return false;}}return true;
}
bool PoseQueuePatchesLive()noexcept{
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_original_view.load(std::memory_order_acquire))return false;
 for(unsigned n=0;n<groups::queueCount;++n)if(!PosePatchOwned(g_pose_patches[groups::queueFirst+n]))return false;
 std::uintptr_t mismatch=0;return PoseQueueCode(mismatch);
}
bool PoseQueueCurrent()noexcept{
 if(!g_pose_queue_bound||!(g_pose_owners.held&holds::carry)||!PoseQueuePatchesLive())return false;
 ql::Owner fresh{};auto reader=PoseReader;return ql::ReadOwner(reader,g_base,g_pose_player,fresh)&&ql::Same(g_pose_queue_owner,fresh)&&fresh.data==g_pose_carry_pair.data;
}
bool PoseQueueCapture(const cp::Pair& pair,ql::Owner& out)noexcept{
 out={};if(!PoseOwned()||!t_pose.job||!PoseWorker()||!PoseQueuePatchesLive()||g_pose_fault)return false;
 auto reader=PoseReader;return ql::ReadOwner(reader,g_base,pair.player,out)&&out.data==pair.data;
}
bool PoseQueueAdmit(std::uintptr_t object,bool genericFree)noexcept{
 if(!(g_pose_owners.held&holds::carry))return true;
 // The caller already owns a real writer/shared ticket. Retained state is
 // never inspected on a pre-admission path and remains held on all refusals.
 if(!PoseQueueCurrent()){PoseFail("pose_carry_q_owner_changed");return false;}
 if(genericFree){auto reader=PoseReader;if(!ql::ForeignFree(reader,g_base,object,g_pose_queue_owner)){
   PoseFail("pose_carry_q_allocator_disposal");return false;}return true;}
 if(ql::ProtectedRange(g_pose_queue_owner,object,0x220)){PoseFail("pose_carry_q_lifecycle");return false;}return true;
}
bool PoseQueueAfter()noexcept{
 if(!(g_pose_owners.held&holds::carry))return true;
 if(!PoseQueueCurrent()){PoseFail("pose_carry_q_post_owner_changed");return false;}return !g_pose_fault;
}
void* PoseQueueCtor(void* object)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* returned=object;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_ctor(object);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){
  SetLastError(incoming);returned=g_pose_queue_ctor(object);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
std::uintptr_t PoseQueueDestruct(void* object)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uintptr_t returned=0;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_destruct(object);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){
  SetLastError(incoming);returned=g_pose_queue_destruct(object);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
void* PoseQueueDelete(void* object,std::uint32_t flags)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;void* returned=object;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_delete(object,flags);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){
  SetLastError(incoming);returned=g_pose_queue_delete(object,flags);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
std::uintptr_t PoseQueueInit(void* object,void* descriptor)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uintptr_t returned=0;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_init(object,descriptor);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){
  SetLastError(incoming);returned=g_pose_queue_init(object,descriptor);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
std::uintptr_t PoseQueueAllocate(void* object,std::uint32_t count)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uintptr_t returned=0;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_allocate(object,count);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){
  SetLastError(incoming);returned=g_pose_queue_allocate(object,count);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
std::uintptr_t PoseQueueRelease(void* object)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uintptr_t returned=0;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_release(object);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){
  SetLastError(incoming);returned=g_pose_queue_release(object);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
std::uintptr_t PoseQueueFree(void* object)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uintptr_t returned=0;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_free(object);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),false)){
  SetLastError(incoming);returned=g_pose_queue_free(object);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
std::uintptr_t PoseQueueAllocatorFree(void* object)noexcept{
 const DWORD incoming=GetLastError();DWORD outgoing=incoming;std::uintptr_t returned=0;
 if(PoseOwned()){
  if(!PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),true)){SetLastError(incoming);return returned;}
  SetLastError(incoming);returned=g_pose_queue_allocator_free(object);outgoing=GetLastError();
  PoseQueueAfter();SetLastError(outgoing);return returned;
 }
 PoseSharedLease read{};if(!PoseEnterShared(read)){SetLastError(incoming);return returned;}
 __try{if(PoseQueueAdmit(reinterpret_cast<std::uintptr_t>(object),true)){
  SetLastError(incoming);returned=g_pose_queue_allocator_free(object);outgoing=GetLastError();PoseQueueAfter();
 }}__finally{PoseExitShared(read);SetLastError(outgoing);}return returned;
}
