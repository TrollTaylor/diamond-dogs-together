#pragma once
// Read-only natural callback telemetry. Unregistered: root owns hook installation.
// A sample establishes observation only, never cargo/writer permission.
#include <atomic>
#include "horse_physical_profile_read.hpp"
namespace mgscoop::horse_physical_callback_probe {
using Original=void(*)(void*,void*); //1AD8250: PRIMARY, actual pose context.
struct RawWindow {std::uintptr_t self=0,poseContext=0,caller=0;std::uint32_t thread=0;};
struct Sample {std::uint16_t body=0xffff;horse_physical_profile_read::Profile profile{};};
// The coordinator constructs the current paired census from actual P/Q in
// the callback. Stale Lua scope is not an authority source for a later writer.
template<class Reader> bool SampleCurrent(Reader& r,const carry_plugin_read::Scope& scope,const RawWindow& raw,Sample& out) noexcept {
    out={};if(!raw.self||!raw.poseContext||!raw.caller||raw.thread!=scope.thread)return false;
    horse_player_physical_read::Observation physical{};
    if(!horse_player_physical_read::Observe(r,scope,physical))return false;
    for(std::uint16_t body=0;body<2;++body){
        if(physical.body[body].component!=raw.self)continue;
        if(!horse_physical_profile_read::Observe(r,scope.image,physical.body[body],out.profile))return false;
        out.body=body;return true;
    }
    return false;
}
using SampleFn=bool(*)(void*,const RawWindow&,Sample&) noexcept;
struct Row {std::uint32_t calls=0,caller=0,thread=0,count=0,mode=UINT32_MAX,flags=0,context=0;};
struct AtomicRow {
    std::atomic_flag writer=ATOMIC_FLAG_INIT;
    std::atomic<std::uint32_t> sequence{0},calls{0},caller{0},thread{0},count{0},mode{UINT32_MAX},flags{0},context{0};
    bool Put(const RawWindow& raw,const Sample& sample,std::uintptr_t image) noexcept {
        if(writer.test_and_set(std::memory_order_acquire))return false;
        sequence.fetch_add(1,std::memory_order_acq_rel);
        calls.fetch_add(1,std::memory_order_relaxed);
        caller.store(raw.caller>=image&&raw.caller-image<=UINT32_MAX?static_cast<std::uint32_t>(raw.caller-image):UINT32_MAX,std::memory_order_relaxed);
        thread.store(raw.thread,std::memory_order_relaxed);count.store(sample.profile.profileCount,std::memory_order_relaxed);
        mode.store(sample.profile.mode,std::memory_order_relaxed);flags.store(sample.profile.flags,std::memory_order_relaxed);
        context.store(raw.poseContext?1u:0u,std::memory_order_relaxed);sequence.fetch_add(1,std::memory_order_release);
        writer.clear(std::memory_order_release);return true;
    }
    bool Read(Row& out) const noexcept {
        for(unsigned tries=0;tries<3;++tries){const auto before=sequence.load(std::memory_order_acquire);if(before&1u)continue;
            out={calls.load(std::memory_order_relaxed),caller.load(std::memory_order_relaxed),thread.load(std::memory_order_relaxed),
              count.load(std::memory_order_relaxed),mode.load(std::memory_order_relaxed),flags.load(std::memory_order_relaxed),context.load(std::memory_order_relaxed)};
            if(before==sequence.load(std::memory_order_acquire))return true;
        }
        return false;
    }
};
struct Probe {
    std::uintptr_t image=0;std::atomic<bool> installed{false};AtomicRow body[2];
    std::atomic<std::uint32_t> rejected{0};
    std::atomic<std::uint32_t> rawCalls{0},rawCaller{0},rawThread{0};
    std::atomic<std::uintptr_t> candidateSelf[2]{};
    std::atomic<std::uint32_t> candidateThread{0};
    // Refresh only from root's fresh fully typed paired Lua census, and clear
    // on stale/changed mission, epoch, generation or player identity. This is
    // a performance filter only; every accepted sample revalidates ownership.
    void Candidates(const horse_player_physical_read::Observation& paired,std::uint32_t ownerThread,bool ready) noexcept {
        candidateThread.store(ready?ownerThread:0,std::memory_order_release);
        candidateSelf[0].store(ready?paired.body[0].component:0,std::memory_order_release);
        candidateSelf[1].store(ready?paired.body[1].component:0,std::memory_order_release);
    }
    // All entries, including another engine thread or an unknown component,
    // are observed separately. These counters never assert Player ownership.
    void Enter(const RawWindow& raw) noexcept {
        rawCalls.fetch_add(1,std::memory_order_relaxed);
        rawCaller.store(raw.caller>=image&&raw.caller-image<=UINT32_MAX?static_cast<std::uint32_t>(raw.caller-image):UINT32_MAX,std::memory_order_relaxed);
        rawThread.store(raw.thread,std::memory_order_relaxed);
    }
    void Observe(const RawWindow& raw,SampleFn sample,void* context) noexcept {
        Sample out{};
        if(!raw.self||!raw.poseContext||!raw.caller||!raw.thread||raw.thread!=candidateThread.load(std::memory_order_acquire)||
          (raw.self!=candidateSelf[0].load(std::memory_order_acquire)&&raw.self!=candidateSelf[1].load(std::memory_order_acquire))||
          !sample||!sample(context,raw,out)||out.body>1||out.profile.component!=raw.self||
          !body[out.body].Put(raw,out,image)){rejected.fetch_add(1,std::memory_order_relaxed);return;}
    }
    bool Line(char* out,std::size_t capacity) const noexcept {
        if(!out||!capacity)return false;Row a{},b{};
        if(!body[0].Read(a)||!body[1].Read(b))return false;
        const int n=std::snprintf(out,capacity,"physCb;on=%u;b0=%u,%x,%u,%u,%u,%x,%u;b1=%u,%x,%u,%u,%u,%x,%u;reject=%u",
          installed.load()?1u:0u,a.calls,a.caller,a.thread,a.count,a.mode,a.flags,a.context,
          b.calls,b.caller,b.thread,b.count,b.mode,b.flags,b.context,rejected.load());
        return n>=0&&static_cast<std::size_t>(n)<capacity;
    }
    bool RawLine(char* out,std::size_t capacity) const noexcept {
        if(!out||!capacity)return false;
        const int n=std::snprintf(out,capacity,"physRaw;on=%u;calls=%u;caller=%x;thread=%u;reject=%u",installed.load()?1u:0u,
          rawCalls.load(),rawCaller.load(),rawThread.load(),rejected.load());
        return n>=0&&static_cast<std::size_t>(n)<capacity;
    }
};
}
