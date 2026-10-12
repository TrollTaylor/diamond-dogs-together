#pragma once
#include <atomic>
#include <cstdint>
#include <type_traits>
namespace mgscoop::native_pose_exclusion {
// Pure synchronization proposal. No native calls, addresses, memory writes or
// hook installation. Exact admission/typed cleanup and complete participant
// coverage remain mandatory integration prerequisites.
enum class Status : std::uint8_t { entered,busy,nested_writer,unqualified,stopping,poisoned,invalid,saturated };
enum class JobResult : std::uint8_t { executed_windowed,executed_legacy,suppressed };
inline bool MayForwardRefusedJob(bool nativePoseRequired) noexcept { return !nativePoseRequired; }
struct Admission {
 bool privateWorker=false,copiedSaves=false,exactGates=false,body1Owned=false;
 bool poseRequired=false,allParticipantsInstalled=false;
 std::uint32_t generation=0,sceneEpoch=0,thread=0;
};
class Gate;
class ReadTicket {
 friend class Gate;
 const Gate* gate_=nullptr;std::uint32_t thread_=0;bool armed_=false;
public:
 ReadTicket() noexcept=default;
 ReadTicket(const ReadTicket&)=delete;ReadTicket& operator=(const ReadTicket&)=delete;
 ReadTicket(ReadTicket&&)=delete;ReadTicket& operator=(ReadTicket&&)=delete;
 bool Armed() const noexcept { return armed_; }
};
class WriteTicket {
 friend class Gate;
 const Gate* gate_=nullptr;std::uint32_t thread_=0,generation_=0,sceneEpoch_=0;bool armed_=false;
public:
 WriteTicket() noexcept=default;
 WriteTicket(const WriteTicket&)=delete;WriteTicket& operator=(const WriteTicket&)=delete;
 WriteTicket(WriteTicket&&)=delete;WriteTicket& operator=(WriteTicket&&)=delete;
 bool Armed() const noexcept { return armed_; }
};
static_assert(std::is_trivially_destructible_v<ReadTicket>);
static_assert(std::is_trivially_destructible_v<WriteTicket>);
class Gate {
 // One atomic word is the exclusion: low32 reader count OR high32 writer owner.
 // Independent telemetry counters cannot confer permission to run originals.
 std::atomic<std::uint64_t> word_{0};
 std::atomic<bool> closing_{false},poisoned_{false};
 static constexpr std::uint64_t Writer(std::uint32_t thread) noexcept { return std::uint64_t(thread)<<32; }
public:
 Gate() noexcept=default;Gate(const Gate&)=delete;Gate& operator=(const Gate&)=delete;
 Status TryRead(std::uint32_t thread,ReadTicket& ticket) noexcept {
  if(!thread||ticket.armed_)return Status::invalid;
  if(poisoned_.load(std::memory_order_acquire))return Status::poisoned;
  auto seen=word_.load(std::memory_order_acquire);
  for(;;){
   const auto writer=static_cast<std::uint32_t>(seen>>32);
   if(writer)return writer==thread?Status::nested_writer:Status::busy;
   if(seen>=0x7fffffffu)return Status::saturated;
   if(word_.compare_exchange_weak(seen,seen+1,std::memory_order_acq_rel,std::memory_order_acquire)){
    ticket.gate_=this;ticket.thread_=thread;ticket.armed_=true;return Status::entered;
   }
  }
 }
 bool EndRead(std::uint32_t thread,ReadTicket& ticket) noexcept {
  if(!ticket.armed_||ticket.gate_!=this||ticket.thread_!=thread)return false;
  auto seen=word_.load(std::memory_order_acquire);
  for(;;){
   if(!seen||(seen>>32))return false;
   if(word_.compare_exchange_weak(seen,seen-1,std::memory_order_acq_rel,std::memory_order_acquire)){
    ticket.armed_=false;return true;
   }
  }
 }
 Status TryWrite(const Admission& a,WriteTicket& ticket) noexcept {
  if(ticket.armed_||!a.thread)return Status::invalid;
  if(!a.privateWorker||!a.copiedSaves||!a.exactGates||!a.body1Owned||
     !a.poseRequired||!a.allParticipantsInstalled||!a.generation||!a.sceneEpoch)return Status::unqualified;
  if(poisoned_.load(std::memory_order_acquire))return Status::poisoned;
  if(closing_.load(std::memory_order_acquire))return Status::stopping;
  std::uint64_t seen=0;
  if(!word_.compare_exchange_strong(seen,Writer(a.thread),std::memory_order_acq_rel,std::memory_order_acquire))
   return static_cast<std::uint32_t>(seen>>32)==a.thread?Status::nested_writer:Status::busy;
  if(closing_.load(std::memory_order_acquire)||poisoned_.load(std::memory_order_acquire)){
   seen=Writer(a.thread);const bool released=word_.compare_exchange_strong(seen,0,std::memory_order_acq_rel,std::memory_order_acquire);
   if(!released){poisoned_.store(true,std::memory_order_release);return Status::poisoned;}
   return poisoned_.load(std::memory_order_acquire)?Status::poisoned:Status::stopping;
  }
  ticket.gate_=this;ticket.thread_=a.thread;ticket.generation_=a.generation;ticket.sceneEpoch_=a.sceneEpoch;ticket.armed_=true;
  return Status::entered;
 }
 bool Owns(std::uint32_t thread,std::uint32_t generation,std::uint32_t sceneEpoch,const WriteTicket& ticket) const noexcept {
  return ticket.armed_&&ticket.gate_==this&&thread&&ticket.thread_==thread&&
   ticket.generation_==generation&&ticket.sceneEpoch_==sceneEpoch&&word_.load(std::memory_order_acquire)==Writer(thread);
 }
 // These booleans must be actual complete typed Post/C0/UI cleanup and the
 // exact-once EndWindow receipt, never assumed true because a job returned.
 // Publish mod-only retirement bookkeeping after validated cleanup while
 // readers remain excluded. The callback must not call native code or mutate
 // this gate; its writes happen-before the release admitting lifecycle readers.
private:
 template<class Publish> bool EndWritePublishImpl(std::uint32_t thread,std::uint32_t generation,std::uint32_t sceneEpoch,
               bool cleanupComplete,bool windowClosed,WriteTicket& ticket,Publish&& publish,bool refuseReservation) noexcept {
  static_assert(noexcept(publish()),"retirement publication must be noexcept");
  if(!Owns(thread,generation,sceneEpoch,ticket))return false;
  if(!cleanupComplete||!windowClosed||poisoned_.load(std::memory_order_acquire)){
   closing_.store(true,std::memory_order_release);poisoned_.store(true,std::memory_order_release);
   return false; // retain writer/ticket until owned private process cleanup
  }
  // Reserve retirement atomically before publication. No reader can enter
  // this high32 writer state; Owns is false so reentrant release cannot run.
  auto seen=Writer(thread);const auto committed=seen|0x80000000ull;
  if(refuseReservation||!word_.compare_exchange_strong(seen,committed,std::memory_order_acq_rel,std::memory_order_acquire))return false;
  publish(); // strictly mod bookkeeping, no gate/native calls
  word_.store(0,std::memory_order_release); // infallible after reservation
  ticket.armed_=false;return true;
 }
public:
 template<class Publish> bool EndWritePublish(std::uint32_t thread,std::uint32_t generation,std::uint32_t sceneEpoch,
               bool cleanupComplete,bool windowClosed,WriteTicket& ticket,Publish&& publish) noexcept {
  return EndWritePublishImpl(thread,generation,sceneEpoch,cleanupComplete,windowClosed,ticket,
    static_cast<Publish&&>(publish),false);
 }
 // Refusal-only synthetic seam: inject failure at the reservation boundary.
 // This never grants a writer, suppresses checks or admits native code.
 template<class Publish> bool EndWritePublishRefusedForTest(std::uint32_t thread,std::uint32_t generation,std::uint32_t sceneEpoch,
               bool cleanupComplete,bool windowClosed,WriteTicket& ticket,Publish&& publish) noexcept {
  return EndWritePublishImpl(thread,generation,sceneEpoch,cleanupComplete,windowClosed,ticket,
    static_cast<Publish&&>(publish),true);
 }
 bool EndWrite(std::uint32_t thread,std::uint32_t generation,std::uint32_t sceneEpoch,
               bool cleanupComplete,bool windowClosed,WriteTicket& ticket) noexcept {
  return EndWritePublish(thread,generation,sceneEpoch,cleanupComplete,windowClosed,ticket,[]()noexcept{});
 }
 // Stop new writers, retaining active cleanup authority. Normal readers may
 // still enter after a healthy writer finishes. Native teardown must use its
 // own reader ticket and wait until the writer completes before destruction.
 void StopNewWriters() noexcept { closing_.store(true,std::memory_order_release); }
 bool Idle() const noexcept { return word_.load(std::memory_order_acquire)==0; }
 bool Poisoned() const noexcept { return poisoned_.load(std::memory_order_acquire); }
};
} // namespace mgscoop::native_pose_exclusion
