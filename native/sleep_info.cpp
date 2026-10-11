#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <strsafe.h>
#include <cstring>
#include <limits>
#include "mgscoop/sleep_info.hpp"
#include "mgscoop/downed_player.hpp"
#include "mgscoop/partner_input_hook.hpp"
#include "mgscoop/partner_defer.hpp"
#include "sleep_plugin_read.hpp"
#include "sleep_graph_read.hpp"
namespace mgscoop::sleep_info {
namespace plugin=sleep_plugin_read;
namespace {
SRWLOCK lock=SRWLOCK_INIT;
partner_player::Addresses addresses{};
bool configured=false,verified=false,stopped=false;
std::uint32_t boundThread=0;
std::uint64_t lastQuery=0;
unsigned readRefusals=0;
void (*logger)(const char*) noexcept=nullptr;
struct Preserve {DWORD value=GetLastError();~Preserve(){SetLastError(value);}};
bool ReadBytes(std::uintptr_t at,void* target,std::size_t size) noexcept {
 SIZE_T count=0;return plugin::Range(at,size)&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),target,size,&count)&&count==size;
}
bool ReadIdentity(partner_player::State& out) noexcept {
 out={};return partner_player::ReadState(addresses,out)==partner_player::Refusal::none&&out.count==2&&out.local==0&&out.ids[0]==0&&out.ids[1]==1&&out.realized_count==2&&(out.flags&1u)&&!(out.flags&0x100u)&&!out.sync&&out.arrays_ok&&out.bits_ok&&out.slots_ok&&partner_player::Realized(out,0)&&partner_player::Realized(out,1)&&out.parts[0]==3&&out.parts[1]==3&&out.pool[0]!=out.pool[1];
}
bool SameState(const partner_player::State& a,const partner_player::State& b) noexcept {
 if(a.P!=b.P||a.Q!=b.Q||a.S!=b.S||a.exists!=b.exists||a.local_bits!=b.local_bits||a.slots!=b.slots||a.sync!=b.sync||a.realizer!=b.realizer||a.flags!=b.flags||a.count!=b.count||a.realized_count!=b.realized_count||a.local!=b.local||a.rbits!=b.rbits||a.s0!=b.s0||a.s8!=b.s8)return false;
 for(unsigned k=0;k<2;++k)if(a.ids[k]!=b.ids[k]||a.pool[k]!=b.pool[k]||a.parts[k]!=b.parts[k])return false;
 return a.P&&a.Q&&a.S&&a.count==2;
}
bool SameCycle(const downed_player::Snapshot& a,const downed_player::Snapshot& b) noexcept {
 return a.ready==b.ready&&a.armed==b.armed&&a.mission==b.mission&&a.epoch==b.epoch&&a.generation==b.generation&&a.down==b.down&&a.cause==b.cause;
}
plugin::Scope Scope(std::uint32_t mission,std::uint16_t epoch,bool admitted,std::uint32_t ownerThread,std::uint32_t sequence,const downed_player::Snapshot& own,const partner_player::State& body) noexcept {
 plugin::Scope s{};s.exactGate=verified&&!stopped;s.admitted=admitted;s.ready=own.ready;s.armed=own.armed;
 s.image=addresses.image_base;s.player=body.P;s.data=body.Q;s.state=body.S;
 s.mission=mission;s.ownedMission=own.mission;s.epoch=epoch;s.ownedEpoch=own.epoch;s.generation=own.generation;s.ownedGeneration=own.generation;
 s.thread=GetCurrentThreadId();s.ownerThread=ownerThread;s.now=GetTickCount64();s.poll=own.lastPoll;
 s.sequenceBefore=sequence;s.sequenceAfter=partner_input_hook::WindowSeq();s.batch=partner_defer::InBatch();return s;
}
const char* Refusal(const plugin::Scope& s) noexcept {
 // Caller may check admission before there is a typed body. Identity is then
 // checked separately by ReadIdentity and the pure plugin reader.
 if(!verified||stopped)return "disabled";
 if(!s.admitted||!s.ready||!s.armed)return "scope";
 if(!s.ownerThread||s.thread!=s.ownerThread)return "owner_thread";
 if(boundThread&&boundThread!=s.ownerThread)return "owner_changed";
 if((s.mission!=30010&&s.mission!=30020&&s.mission!=30050)||!s.epoch||!s.generation||s.mission!=s.ownedMission||s.epoch!=s.ownedEpoch)return "scene";
 if(!s.poll||s.now<s.poll||s.now-s.poll>750)return "scope_expired";
 if(s.batch)return "batch";
 if((s.sequenceBefore&1u)||s.sequenceBefore!=s.sequenceAfter)return "input_window";
 return nullptr;
}
void LogRefusal(const char* stage,const plugin::Observation& seen,std::uintptr_t at,std::uintptr_t value,std::size_t size,bool read) noexcept {
 if(!logger||readRefusals>=32)return;++readRefusals;
 const char* object="other";std::uintptr_t offset=0;
 const auto locate=[&](std::uintptr_t start,std::size_t length,const char* name) noexcept {
  if(start&&at>=start&&at-start<length){object=name;offset=at-start;return true;}return false;
 };
 bool located=locate(seen.player,0x388,"player")||locate(seen.data,0x1f0,"data")||locate(seen.state,0x48,"state");
 for(unsigned k=0;k<2&&!located;++k){const auto& b=seen.body[k];
  located=locate(b.block,0x6a8,k?"block1":"block0")||locate(b.ps,0x510,k?"ps1":"ps0")||locate(b.node,0x70,k?"node1":"node0")||locate(b.primary,0x80,k?"primary1":"primary0")||locate(b.records,0x40,k?"record1":"record0")||locate(b.bitmap,4,k?"bitmap1":"bitmap0")||locate(b.actionBytes,1,k?"action1":"action0")||locate(b.lifeRecord,0x4c,k?"life1":"life0");
 }
 const auto imageValue=read&&value>=addresses.image_begin&&value<addresses.image_end?value-addresses.image_base:0;
 char line[256]{};StringCchPrintfA(line,ARRAYSIZE(line),"sleep_plugin_read_refused;stage=%s;object=%s;offset=0x%llx;size=%u;read=%u;image_value=0x%llx;zero=%u\r\n",stage,object,static_cast<unsigned long long>(offset),unsigned(size),read?1u:0u,static_cast<unsigned long long>(imageValue),read&&value==0?1u:0u);logger(line);
}
}
bool CodeGate(const void* image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 const Preserve preserve;auto reader=ReadBytes;const auto base=reinterpret_cast<std::uintptr_t>(image);
 return plugin::CodeGate(reader,base,span,mismatch)&&sleep_graph_read::CodeGate(reader,base,span,mismatch);
}
void Configure(const partner_player::Addresses& source,bool passed,void (*log)(const char*) noexcept) noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);
 // First configuration owns identity; repeated configuration cannot clear a
 // refusal or reopen a shutdown module.
 if(!configured){addresses=source;verified=passed;logger=log;configured=true;}
 ReleaseSRWLockExclusive(&lock);
}
bool Observe(std::uint32_t mission,std::uint16_t epoch,bool admitted,std::uint32_t ownerThread,Reply& out) noexcept {
 const Preserve preserve;out={};if(!TryAcquireSRWLockExclusive(&lock)){out.reason="busy";return false;}
 const auto finish=[&](const char* reason,bool ok=false) noexcept {if(!ok)out={};out.reason=reason;out.ready=ok;ReleaseSRWLockExclusive(&lock);return ok;};
 const auto sequence=partner_input_hook::WindowSeq();const auto own=downed_player::GetSnapshot();partner_player::State first{},second{},after{},finalState{};
 if(const char* why=Refusal(Scope(mission,epoch,admitted,ownerThread,sequence,own,first)))return finish(why);
 const auto now=GetTickCount64();if(lastQuery&&now>=lastQuery&&now-lastQuery<100)return finish("throttled");
 if(!ReadIdentity(first)||!ReadIdentity(second))return finish("body_chain");
 if(!SameState(first,second))return finish("body_changed");
 auto scope=Scope(mission,epoch,admitted,ownerThread,sequence,own,second);
 if(const char* why=plugin::ScopeRefusal(scope))return finish(why);
 std::uintptr_t lastAt=0,lastValue=0;std::size_t lastSize=0;bool lastRead=false;
 auto reader=[&](std::uintptr_t at,void* target,std::size_t size) noexcept {
  lastAt=at;lastValue=0;lastSize=size;lastRead=ReadBytes(at,target,size);
  if(lastRead&&size<=sizeof(lastValue))std::memcpy(&lastValue,target,size);return lastRead;
 };
 plugin::Observation a{},b{},c{};const char* why="sleep_chain";
 if(!plugin::ReadOnce(reader,scope,a,&why)||!plugin::ReadOnce(reader,scope,b,&why)){
  LogRefusal(why,b.player?b:a,lastAt,lastValue,lastSize,lastRead);return finish(why);
 }
 if(!plugin::Same(a,b))return finish("sleep_changed");
 sleep_graph_read::Observation ga{},gb{},gc{};const char* graphWhy="graph_chain";
 const bool graphRead=sleep_graph_read::ReadOnce(reader,scope,ga,&graphWhy)&&sleep_graph_read::ReadOnce(reader,scope,gb,&graphWhy);
 const bool graphStable=graphRead&&sleep_graph_read::Same(ga,gb);
 out.graphReason=graphRead?(graphStable?"graph_observed":"graph_changed"):graphWhy;
 const auto ownAfter=downed_player::GetSnapshot();if(!SameCycle(own,ownAfter))return finish("scope_changed");
 scope=Scope(mission,epoch,admitted,ownerThread,sequence,ownAfter,second);
 if(const char* reason=plugin::ScopeRefusal(scope))return finish(reason);
 if(!ReadIdentity(after)||!SameState(second,after))return finish("body_changed");
 if(!plugin::ReadOnce(reader,scope,c,&why)){
  LogRefusal(why,c,lastAt,lastValue,lastSize,lastRead);return finish(why);
 }
 if(!plugin::Same(b,c))return finish("sleep_changed");
 if(graphStable&&sleep_graph_read::ReadOnce(reader,scope,gc,&graphWhy)&&sleep_graph_read::Same(gb,gc)){
  out.graphReady=true;out.graphReason="graph_observed";
  for(unsigned k=0;k<2;++k){const auto& g=gc.graph[k];out.graphCount[k]=g.count;out.graphIndex[k]=g.index;out.graphHash[k]=g.stateHash;out.graphCurrent[k]=g.currentNode;out.graphSleepCandidate[k]=g.sleepHashCandidate;}
 }else if(graphStable)out.graphReason=graphWhy;
 if(!ReadIdentity(finalState)||!SameState(after,finalState))return finish("body_changed");
 const auto finalOwn=downed_player::GetSnapshot();if(!SameCycle(own,finalOwn))return finish("scope_changed");
 scope=Scope(mission,epoch,admitted,ownerThread,sequence,finalOwn,finalState);
 if(const char* reason=plugin::ScopeRefusal(scope))return finish(reason);
 for(unsigned k=0;k<2;++k){const auto& x=c.body[k];auto& y=out.bodies[k];
  y.nodeState=x.nodeState;y.nodeKind=x.nodeKind;y.recordFlags=x.recordFlags;y.subtype=x.subtype;y.lifeState=x.lifeState;y.flags=x.flags;y.qAction=x.qAction;y.mode=x.mode;y.requested=x.requested;y.counter=x.counter;y.action=x.action;y.nodeActive=x.nodeActive;y.callbackInitialized=x.callbackInitialized;
 }
 out.generation=own.generation;boundThread=ownerThread;lastQuery=now;return finish("sleep_observed",true);
}
void Shutdown() noexcept {
 const Preserve preserve;AcquireSRWLockExclusive(&lock);stopped=true;ReleaseSRWLockExclusive(&lock);
}
} // namespace mgscoop::sleep_info
