#pragma once
// Observational private-worker timing only. No native permission, clock-age
// conversion, allocation, logging, ownership receipt or gameplay write.
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace mgscoop::native_work_timing {
enum class Kind:unsigned {pose_begin,pose_pre,pose_helpers,pose_before,native_job,pose_after,pose_post,pose_end,
 vars_map,original_view,lookup_begin,lookup_gate,lookup_phase,lookup_output,lookup_hook,lookup_report,
 horse_capture,horse_natural,horse_physical,activation_parts,activation_final,activation_no_witness,activation_eligible,count};
inline constexpr unsigned count=static_cast<unsigned>(Kind::count);
inline constexpr const char* names[]={"pose_begin","pose_pre","pose_helpers","pose_before","native_job","pose_after","pose_post","pose_end",
 "vars_map","original_view","lookup_begin","lookup_gate","lookup_phase","lookup_output","lookup_hook","lookup_report",
 "horse_capture","horse_natural","horse_physical","activation_parts","activation_final","activation_no_witness","activation_eligible"};
inline constexpr std::uint64_t all=(std::uint64_t(1)<<count)-1,core=(std::uint64_t(1)<<10)-1;
struct Row {
 std::atomic<std::uint64_t> calls{0},ticks{0},maxTicks{0},cycleCalls{0},cycles{0},invalid{0};
 std::atomic_flag writing=ATOMIC_FLAG_INIT;
 std::atomic<std::uint64_t> sequence{0},eventTicks{0},eventCycles{0};
 std::atomic<std::uint32_t> eventThread{0};std::atomic<bool> eventHasCycles{false};
};
inline Row rows[count]{};
inline std::atomic<std::uint64_t> mask{0},frequency{0};
inline std::atomic<bool> cpuCycles{false};
inline void Max(std::atomic<std::uint64_t>& value,std::uint64_t next)noexcept {
 auto prior=value.load(std::memory_order_relaxed);
 while(prior<next&&!value.compare_exchange_weak(prior,next,std::memory_order_relaxed)){}
}
inline void Record(Kind kind,std::uint64_t ticks,std::uint64_t cycles,bool hasCycles,std::uint32_t thread)noexcept {
 const auto n=static_cast<unsigned>(kind);if(n>=count)return;auto& r=rows[n];
 r.calls.fetch_add(1,std::memory_order_relaxed);r.ticks.fetch_add(ticks,std::memory_order_relaxed);Max(r.maxTicks,ticks);
 if(hasCycles){r.cycleCalls.fetch_add(1,std::memory_order_relaxed);r.cycles.fetch_add(cycles,std::memory_order_relaxed);}
 // Try-only observer lock. A busy collector can lose a paired maximum, never
 // delay native work. The independent maxTicks remains exact and must match
 // eventTicks before any cycle/wall interpretation of that maximum.
 if(r.writing.test_and_set(std::memory_order_acquire))return;
 if(ticks>r.eventTicks.load(std::memory_order_relaxed)){
  r.sequence.fetch_add(1,std::memory_order_acq_rel);
  r.eventCycles.store(cycles,std::memory_order_relaxed);r.eventThread.store(thread,std::memory_order_relaxed);
  r.eventHasCycles.store(hasCycles,std::memory_order_relaxed);r.eventTicks.store(ticks,std::memory_order_relaxed);
  r.sequence.fetch_add(1,std::memory_order_release);
 }r.writing.clear(std::memory_order_release);
}
struct Snapshot {
 std::uint64_t calls=0,ticks=0,maxTicks=0,cycleCalls=0,cycles=0,invalid=0,eventTicks=0,eventCycles=0;
 std::uint32_t eventThread=0;bool paired=false,hasCycles=false;
};
inline Snapshot Read(Kind kind)noexcept {
 Snapshot out{};const auto n=static_cast<unsigned>(kind);if(n>=count)return out;const auto& r=rows[n];
 out.calls=r.calls.load(std::memory_order_relaxed);out.ticks=r.ticks.load(std::memory_order_relaxed);
 out.maxTicks=r.maxTicks.load(std::memory_order_relaxed);out.cycleCalls=r.cycleCalls.load(std::memory_order_relaxed);
 out.cycles=r.cycles.load(std::memory_order_relaxed);out.invalid=r.invalid.load(std::memory_order_relaxed);
 for(unsigned attempt=0;attempt<3;++attempt){const auto before=r.sequence.load(std::memory_order_acquire);if(before&1u)continue;
  out.eventTicks=r.eventTicks.load(std::memory_order_relaxed);out.eventCycles=r.eventCycles.load(std::memory_order_relaxed);
  out.eventThread=r.eventThread.load(std::memory_order_relaxed);out.hasCycles=r.eventHasCycles.load(std::memory_order_relaxed);
  if(before==r.sequence.load(std::memory_order_acquire)){out.paired=before&&out.eventTicks==out.maxTicks;break;}
 }return out;
}
// POD token is safe in functions using Windows SEH. End exactly once, including
// __finally on unwinds; it preserves the actual outgoing LastError.
struct Token {Kind kind=Kind::count;std::uint64_t start=0,startCycles=0;std::uint32_t thread=0;bool active=false,hasCycles=false;};
inline bool Configure(std::uint64_t wanted,bool cycles=false)noexcept {
 #if defined(_WIN32) && defined(MGS_BACKGROUND_WORKER)
 const DWORD incoming=GetLastError();LARGE_INTEGER rate{};const bool okay=!(wanted&~all)&&QueryPerformanceFrequency(&rate)&&rate.QuadPart>0;
 if(okay){frequency.store(static_cast<std::uint64_t>(rate.QuadPart),std::memory_order_release);cpuCycles.store(cycles,std::memory_order_relaxed);mask.store(wanted,std::memory_order_release);}
 SetLastError(incoming);return okay;
 #else
 (void)wanted;(void)cycles;return false;
 #endif
}
inline Token Begin(Kind kind)noexcept {
 Token out{};
 #if defined(_WIN32) && defined(MGS_BACKGROUND_WORKER)
 const auto n=static_cast<unsigned>(kind);if(n>=count||!(mask.load(std::memory_order_relaxed)&(std::uint64_t(1)<<n)))return out;
 const DWORD incoming=GetLastError();LARGE_INTEGER now{};
 if(QueryPerformanceCounter(&now)&&now.QuadPart>=0){out.kind=kind;out.start=static_cast<std::uint64_t>(now.QuadPart);out.thread=GetCurrentThreadId();out.active=true;
  if(cpuCycles.load(std::memory_order_relaxed)){ULONG64 cycles=0;out.hasCycles=QueryThreadCycleTime(GetCurrentThread(),&cycles)!=FALSE;out.startCycles=cycles;}}
 SetLastError(incoming);
 #else
 (void)kind;
 #endif
 return out;
}
inline void End(Token& token)noexcept {
 if(!token.active)return;token.active=false;
 #if defined(_WIN32) && defined(MGS_BACKGROUND_WORKER)
 const DWORD incoming=GetLastError();LARGE_INTEGER now{};ULONG64 cycles=0;
 const bool same=token.thread==GetCurrentThreadId();
 const bool clock=QueryPerformanceCounter(&now)&&now.QuadPart>=0&&static_cast<std::uint64_t>(now.QuadPart)>=token.start;
 const bool cycle=token.hasCycles&&same&&QueryThreadCycleTime(GetCurrentThread(),&cycles)&&cycles>=token.startCycles;
 if(clock&&same)Record(token.kind,static_cast<std::uint64_t>(now.QuadPart)-token.start,
  cycle?cycles-token.startCycles:0,cycle,token.thread);
 else if(static_cast<unsigned>(token.kind)<count)rows[static_cast<unsigned>(token.kind)].invalid.fetch_add(1,std::memory_order_relaxed);
 SetLastError(incoming);
 #endif
}
struct Scope {Token token{};explicit Scope(Kind kind)noexcept:token(Begin(kind)){}~Scope(){End(token);}Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;};
inline std::uint64_t Microseconds(std::uint64_t ticks,std::uint64_t rate)noexcept {
 if(!rate)return 0;const auto seconds=ticks/rate,remainder=ticks%rate;
 if(seconds>UINT64_MAX/1000000||remainder>UINT64_MAX/1000000)return UINT64_MAX;
 const auto fraction=remainder*1000000/rate;
 return seconds*1000000>UINT64_MAX-fraction?UINT64_MAX:seconds*1000000+fraction;
}
// Explicit coordinator query only, never called automatically from a hook.
// Cycle counts are CPU cycles, not converted to microseconds or peer time.
inline bool Line(Kind kind,char* out,std::size_t capacity)noexcept {
 const auto n=static_cast<unsigned>(kind);if(n>=count||!out||!capacity)return false;
 #if defined(_WIN32)
 const DWORD incoming=GetLastError();
 #endif
 const auto s=Read(kind);const auto rate=frequency.load(std::memory_order_acquire);
 const int length=std::snprintf(out,capacity,"nwork;k=%s;n=%llu;us=%llu;max=%llu;pair=%u;cyc=%llu;ct=%u;bad=%llu;m=%llx",
  names[n],static_cast<unsigned long long>(s.calls),static_cast<unsigned long long>(Microseconds(s.ticks,rate)),
  static_cast<unsigned long long>(Microseconds(s.maxTicks,rate)),s.paired?1u:0u,
  static_cast<unsigned long long>(s.paired&&s.hasCycles?s.eventCycles:0),s.paired?s.eventThread:0,
  static_cast<unsigned long long>(s.invalid),static_cast<unsigned long long>(mask.load(std::memory_order_relaxed)));
 #if defined(_WIN32)
 SetLastError(incoming);
 #endif
 return length>=0&&static_cast<std::size_t>(length)<capacity;
}
}
