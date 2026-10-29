// D7 dog guard (docs/vehicles-companions-design.md 6.12, 8.9): two exact vtable slots of the D-Dog classes.
// ProcessDamages (0x231AD38) drops the hits on instance 0 while the guest's lease is armed; the life update
// (0x231FD60) applies the guard plan to record 0 before the stock update (guest) and publishes record 0 after
// it (both roles: the host's state byte). Original code; no engine calls, no disk writes.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include "mgscoop/dog_guard.hpp"
#include "mgscoop/near_code.hpp"
#include "mgscoop/link_crypto.hpp"
namespace mgscoop::dog_guard {
namespace {
using ProcessFn=void (*)(void*,std::uint32_t,void*,std::uint32_t);
using LifeFn=void (*)(void*);
std::uintptr_t base{};
ProcessFn originalProcess{};
LifeFn originalLife{};
bool processInstalled{},lifeInstalled{};
void (*logger)(const char*) noexcept{};
SRWLOCK lock=SRWLOCK_INIT;
struct Shared {
 bool guest{},killIssued{},snap{};std::uint8_t follow{},labOp{},labPct{},lastLogged{0xff};
 Record rec{};std::uint64_t snapTick{};
};
Shared shared{};
std::atomic<std::uint64_t> frames{},armedUntil{};
std::atomic<unsigned> drops{},hits{},applied{},kills{},cleared{},dropLogs{},followLogs{};
std::atomic<std::uint32_t> lastHit{0xffffffffu};
struct LastErrorState {
 DWORD value{GetLastError()};
 void Restore() const noexcept {SetLastError(value);}
 void Capture() noexcept {value=GetLastError();}
 ~LastErrorState() noexcept {Restore();}
};
template<class T> bool Peek(std::uintptr_t p,T& out) noexcept {
 SIZE_T n=0;return p&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(p),&out,sizeof(out),&n)&&n==sizeof(out);
}
bool ReadRecord(std::uintptr_t r,Record& out) noexcept {
 unsigned char b[kRecordSize]{};
 if(!r||!Peek(r,b)) return false;
 auto u16=[&](std::size_t o){std::uint16_t v{};std::memcpy(&v,b+o,2);return v;};
 out.life=u16(kLife);out.max=u16(kMax);out.pending=u16(kPending);out.stun=u16(kStun);out.stunMax=u16(kStunMax);
 out.pendingStun=u16(kPendingStun);out.state=b[kState];out.flags=b[kFlags];
 return true;
}
// Only the fields the plan may change; never max or state.
bool WriteRecord(std::uintptr_t r,const Record& was,const Record& next) noexcept {
 __try {
  auto* p=reinterpret_cast<volatile unsigned char*>(r);
  if(next.life!=was.life) *reinterpret_cast<volatile std::uint16_t*>(p+kLife)=next.life;
  if(next.pending!=was.pending) *reinterpret_cast<volatile std::uint16_t*>(p+kPending)=next.pending;
  if(next.stun!=was.stun) *reinterpret_cast<volatile std::uint16_t*>(p+kStun)=next.stun;
  if(next.pendingStun!=was.pendingStun) *reinterpret_cast<volatile std::uint16_t*>(p+kPendingStun)=next.pendingStun;
  if(next.flags!=was.flags) p[kFlags]=next.flags;
  return true;
 } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void LogLine(const char* text) noexcept {if(logger) logger(text);}
// The dog life controller's secondary interface: vtable, records pointer, count 1..4 (a copy holds one dog).
bool LifeIdentity(std::uintptr_t self,std::uintptr_t& records) noexcept {
 std::uintptr_t vt{};std::uint32_t count{};
 return Peek(self,vt)&&vt==base+kLifeVtable&&Peek(self+8,records)&&records&&Peek(self+0x48,count)&&count>=1&&count<=4;
}
__declspec(noinline) void ProcessHook(void* self,std::uint32_t index,void* list,std::uint32_t count) noexcept {
 LastErrorState error;
 std::uintptr_t vt{};
 const bool drop=count&&index==0&&Armed(frames.load(std::memory_order_relaxed),armedUntil.load(std::memory_order_relaxed))&&
  Peek(reinterpret_cast<std::uintptr_t>(self),vt)&&vt==base+kProcessVtable;
 if(drop) {
  std::uint16_t attacker{0xffff},attack{0xffff};
  const auto h=reinterpret_cast<std::uintptr_t>(list);
  Peek(h+0x22,attacker);Peek(h+0x28,attack);
  lastHit.store((std::uint32_t(attacker)<<16)|attack,std::memory_order_relaxed);
  const unsigned n=drops.fetch_add(1,std::memory_order_relaxed)+1;
  const unsigned total=hits.fetch_add(count,std::memory_order_relaxed)+count;
  if(n<=16||n%50==0) {
   char line[192]{};
   std::snprintf(line,sizeof(line),"dog_guard=drop;n=%u;hits=%u;count=%u;attacker=%u;attack=%u;index=0;frame=%llu\r\n",n,total,count,
    unsigned(attacker),unsigned(attack),static_cast<unsigned long long>(frames.load(std::memory_order_relaxed)));
   LogLine(line);
  }
  return;
 }
 error.Restore();
 originalProcess(self,index,list,count);
 error.Capture();
}
__declspec(noinline) void LifeHook(void* self) noexcept {
 LastErrorState error;
 const auto s=reinterpret_cast<std::uintptr_t>(self);
 std::uintptr_t records{};
 const bool ours=LifeIdentity(s,records);
 if(ours) {
  const auto frame=frames.fetch_add(1,std::memory_order_relaxed)+1;
  Record r{};
  if(ReadRecord(records,r)) {
   AcquireSRWLockExclusive(&lock);
   const bool guest=shared.guest,killIssued=shared.killIssued;
   const std::uint8_t follow=shared.follow,labOp=shared.labOp,labPct=shared.labPct;
   shared.labOp=0;
   ReleaseSRWLockExclusive(&lock);
   Record now=r;
   if(guest&&Armed(frame,armedUntil.load(std::memory_order_relaxed))) {
    const Plan p=Guard(now,follow,killIssued);
    if(p.write&&WriteRecord(records,now,p.next)) {
     applied.fetch_add(1,std::memory_order_relaxed);
     if(p.kill) {
      kills.fetch_add(1,std::memory_order_relaxed);
      AcquireSRWLockExclusive(&lock);shared.killIssued=true;ReleaseSRWLockExclusive(&lock);
     }
     if(p.clearedKill) cleared.fetch_add(1,std::memory_order_relaxed);
     bool log=p.kill||p.clearedKill;
     AcquireSRWLockExclusive(&lock);
     if(shared.lastLogged!=follow) {shared.lastLogged=follow;log=true;}
     ReleaseSRWLockExclusive(&lock);
     if(log&&followLogs.fetch_add(1,std::memory_order_relaxed)<64) {
      char line[224]{};
      std::snprintf(line,sizeof(line),"dog_guard=follow;byte=0x%02x;life=%u>%u;max=%u;stun=%u>%u;state=%u;kill=%u;cleared_kill=%u;pending=%u\r\n",
       unsigned(follow),unsigned(now.life),unsigned(p.next.life),unsigned(now.max),unsigned(now.stun),unsigned(p.next.stun),
       unsigned(now.state),p.kill?1u:0u,p.clearedKill?1u:0u,p.clearedPending?1u:0u);
      LogLine(line);
     }
     now=p.next;
    }
   }
   if(labOp) {
    const Plan p=Lab(now,labOp,labPct);
    const bool wrote=p.write&&WriteRecord(records,now,p.next);
    char line[192]{};
    std::snprintf(line,sizeof(line),"dog_guard=lab;op=%u;pct=%u;life=%u>%u;stun=%u>%u;kill=%u;wrote=%u;guest=%u\r\n",unsigned(labOp),
     unsigned(labPct),unsigned(now.life),unsigned(p.next.life),unsigned(now.stun),unsigned(p.next.stun),p.kill?1u:0u,wrote?1u:0u,guest?1u:0u);
    LogLine(line);
   }
  }
 }
 error.Restore();
 originalLife(self);
 error.Capture();
 if(ours) {
  Record after{};
  if(ReadRecord(records,after)) {
   AcquireSRWLockExclusive(&lock);
   shared.rec=after;shared.snap=true;shared.snapTick=GetTickCount64();
   ReleaseSRWLockExclusive(&lock);
  }
 }
}
bool PinsMatch(std::uintptr_t image,std::size_t span) noexcept {
 static unsigned char bytes[4096];
 constexpr char hex[]="0123456789abcdef";
 for(const auto& pin:kPins) {
  if(!pin.bytes||pin.bytes>sizeof(bytes)||pin.rva>span||pin.bytes>span-pin.rva) return false;
  SIZE_T n=0;
  if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(image+pin.rva),bytes,pin.bytes,&n)||n!=pin.bytes) return false;
  std::uint8_t digest[32]{};link::Sha256Digest(bytes,pin.bytes,digest);
  for(unsigned i=0;i<32;++i) if(hex[digest[i]>>4]!=pin.sha256[i*2]||hex[digest[i]&15]!=pin.sha256[i*2+1]) return false;
 }
 return true;
}
Status Snapshot() noexcept {
 Status s{};s.installed=processInstalled&&lifeInstalled;s.frames=frames.load(std::memory_order_relaxed);
 s.armed=Armed(s.frames,armedUntil.load(std::memory_order_relaxed));
 s.drops=drops.load(std::memory_order_relaxed);s.hits=hits.load(std::memory_order_relaxed);
 s.applied=applied.load(std::memory_order_relaxed);s.kills=kills.load(std::memory_order_relaxed);s.cleared=cleared.load(std::memory_order_relaxed);
 const auto h=lastHit.load(std::memory_order_relaxed);s.lastAttacker=static_cast<std::uint16_t>(h>>16);s.lastAttack=static_cast<std::uint16_t>(h&0xffffu);
 AcquireSRWLockShared(&lock);
 s.snapshot=shared.snap;s.rec=shared.rec;
 const auto now=GetTickCount64();
 s.ageMs=shared.snap&&now>=shared.snapTick?static_cast<std::uint32_t>(now-shared.snapTick>999999?999999:now-shared.snapTick):999999u;
 ReleaseSRWLockShared(&lock);
 return s;
}
} // namespace
bool CodeGate(const void* image,std::size_t span) noexcept {
 if(!image) return false;
 const auto a=reinterpret_cast<std::uintptr_t>(image);
 std::uintptr_t p{},l{};
 return PinsMatch(a,span)&&kProcessSlot<=span-8&&kLifeSlot<=span-8&&Peek(a+kProcessSlot,p)&&p==a+kProcess&&
  Peek(a+kLifeSlot,l)&&l==a+kLifeUpdate;
}
bool Install(void* image,std::size_t span,void (*log)(const char*) noexcept) noexcept {
 if(base) return base==reinterpret_cast<std::uintptr_t>(image)&&processInstalled&&lifeInstalled;
 if(!log) return false;
 if(!CodeGate(image,span)) {log("dog_guard=refused;reason=code_gate\r\n");return false;}
 base=reinterpret_cast<std::uintptr_t>(image);logger=log;
 originalProcess=reinterpret_cast<ProcessFn>(base+kProcess);
 originalLife=reinterpret_cast<LifeFn>(base+kLifeUpdate);
 std::uintptr_t now{};
 const char* why=near_code::PatchQword(reinterpret_cast<void*>(base+kProcessSlot),base+kProcess,reinterpret_cast<std::uintptr_t>(&ProcessHook));
 processInstalled=!why&&Peek(base+kProcessSlot,now)&&now==reinterpret_cast<std::uintptr_t>(&ProcessHook);
 if(processInstalled) {
  why=near_code::PatchQword(reinterpret_cast<void*>(base+kLifeSlot),base+kLifeUpdate,reinterpret_cast<std::uintptr_t>(&LifeHook));
  lifeInstalled=!why&&Peek(base+kLifeSlot,now)&&now==reinterpret_cast<std::uintptr_t>(&LifeHook);
 }
 if(!processInstalled||!lifeInstalled) {log("dog_guard=refused;reason=vtable_patch\r\n");Shutdown();return false;}
 log("dog_guard=installed;slots=2;scope=guest_instance0_lease;disk_writes=0\r\n");
 return true;
}
bool Installed() noexcept {return base&&processInstalled&&lifeInstalled;}
void Shutdown() noexcept {
 if(!base) return;
 armedUntil.store(0,std::memory_order_relaxed);
 bool clean=true;std::uintptr_t now{};
 if(lifeInstalled) {
  const char* why=near_code::PatchQword(reinterpret_cast<void*>(base+kLifeSlot),reinterpret_cast<std::uintptr_t>(&LifeHook),base+kLifeUpdate);
  lifeInstalled=!(!why&&Peek(base+kLifeSlot,now)&&now==base+kLifeUpdate);
  if(lifeInstalled) clean=false;
 }
 if(processInstalled) {
  const char* why=near_code::PatchQword(reinterpret_cast<void*>(base+kProcessSlot),reinterpret_cast<std::uintptr_t>(&ProcessHook),base+kProcess);
  processInstalled=!(!why&&Peek(base+kProcessSlot,now)&&now==base+kProcess);
  if(processInstalled) clean=false;
 }
 if(clean) base=0; // originals are retained: an already-entered call still completes.
}
Status Control(const Request& q) noexcept {
 bool disarmed=false;
 AcquireSRWLockExclusive(&lock);
 if(q.guest&&q.arm&&Installed()) {
  shared.guest=true;shared.follow=q.follow;
  armedUntil.store(frames.load(std::memory_order_relaxed)+kLeaseFrames,std::memory_order_relaxed);
 } else {
  disarmed=armedUntil.load(std::memory_order_relaxed)!=0;
  armedUntil.store(0,std::memory_order_relaxed);
  shared.guest=q.guest;shared.follow=0;
 }
 const auto f=Decode(shared.follow);
 if(!f.valid||f.state!=2) shared.killIssued=false;
 if(q.labOp) {shared.labOp=q.labOp;shared.labPct=q.labPct;}
 ReleaseSRWLockExclusive(&lock);
 if(disarmed&&!q.arm) LogLine(q.guest?"dog_guard=disarmed;why=lua\r\n":"dog_guard=disarmed;why=host\r\n");
 return Snapshot();
}
Status Read() noexcept {return Snapshot();}
void Disarm(const char* why) noexcept {
 const bool was=armedUntil.exchange(0,std::memory_order_relaxed)!=0;
 AcquireSRWLockExclusive(&lock);
 shared.follow=0;shared.killIssued=false;shared.labOp=0;shared.snap=false;
 ReleaseSRWLockExclusive(&lock);
 if(was&&logger) {
  char line[96]{};
  std::snprintf(line,sizeof(line),"dog_guard=disarmed;why=%s\r\n",why?why:"native");
  logger(line);
 }
}
} // namespace mgscoop::dog_guard
