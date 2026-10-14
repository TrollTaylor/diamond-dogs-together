#include "native_work_timing.hpp"
#include <thread>
#include <vector>
#include <cstdio>
#include <type_traits>
namespace t=mgscoop::native_work_timing;
unsigned passed=0,failed=0;
void Check(bool ok,const char* label){if(ok)++passed;else{++failed;std::printf("FAIL %s\n",label);}}
int main(){
 static_assert(std::is_trivially_destructible_v<t::Token>,"SEH timers must remain POD");
 Check(t::mask.load()==0&&!t::Begin(t::Kind::pose_begin).active,"worker observer defaults off and starts no clock token");
 Check(t::Microseconds(123456,1000000)==123456,"QPC unit conversion uses actual rate");
 Check(t::Microseconds(15,10)==1500000,"whole and fractional seconds preserved");
 Check(!t::Microseconds(99,0)&&t::Microseconds(UINT64_MAX,1)==UINT64_MAX,"unknown frequency and overflow cannot invent a precise duration");
 SetLastError(0x4242);Check(!t::Configure(t::all+1)&&GetLastError()==0x4242&&t::mask.load()==0,"invalid mask leaves config and LastError unchanged");
 Check(t::Configure(1,false)&&GetLastError()==0x4242&&t::frequency.load()>0,"private worker exact mask obtains QPC frequency and preserves LastError");
 Check(!t::Begin(t::Kind::pose_pre).active,"unselected component performs no clock token");
 auto timer=t::Begin(t::Kind::pose_begin);Check(timer.active&&!timer.hasCycles&&GetLastError()==0x4242,"default wall timing requires no thread-cycle syscall");
 SetLastError(0x5656);t::End(timer);Check(!timer.active&&GetLastError()==0x5656&&t::Read(t::Kind::pose_begin).calls==1,"timer ends once and keeps outgoing native LastError");
 t::End(timer);Check(t::Read(t::Kind::pose_begin).calls==1,"finally duplicate End is idempotent");
 auto crossed=t::Begin(t::Kind::pose_begin);std::thread other([&]{SetLastError(0x6767);t::End(crossed);Check(GetLastError()==0x6767,"cross-thread rejection preserves local LastError");});other.join();
 Check(t::Read(t::Kind::pose_begin).calls==1&&t::Read(t::Kind::pose_begin).invalid==1,"token transferred to another thread is never reported as native work");
 Check(t::Configure(0,false)&&!t::Begin(t::Kind::pose_begin).active,"disable stops all new observations");
 t::Record(t::Kind::lookup_begin,20,500,true,7);auto s=t::Read(t::Kind::lookup_begin);
 Check(s.calls==1&&s.maxTicks==20&&s.eventTicks==20&&s.eventCycles==500&&s.eventThread==7&&s.hasCycles&&s.paired,"maximum reports cycles from same measured event");
 auto& row=t::rows[static_cast<unsigned>(t::Kind::lookup_begin)];row.writing.test_and_set();t::Record(t::Kind::lookup_begin,40,999,true,8);row.writing.clear();s=t::Read(t::Kind::lookup_begin);
 Check(s.calls==2&&s.maxTicks==40&&!s.paired,"busy paired observer never waits and marks independent maximum unpaired");
 t::Record(t::Kind::lookup_begin,60,0,false,9);s=t::Read(t::Kind::lookup_begin);
 Check(s.maxTicks==60&&s.paired&&!s.hasCycles&&s.cycleCalls==2,"wall-only new maximum never inherits older event cycles");
 const auto kind=t::Kind::horse_capture;constexpr unsigned threads=4,each=10000;std::vector<std::thread> workers;
 for(unsigned id=0;id<threads;++id)workers.emplace_back([=]{for(unsigned n=0;n<each;++n)t::Record(kind,1,2,true,id+1);});
 for(auto& worker:workers)worker.join();s=t::Read(kind);
 Check(s.calls==threads*each&&s.ticks==threads*each&&s.maxTicks==1&&s.cycles==threads*each*2,"concurrent native observers retain exact independent aggregates");
 auto& current=t::rows[static_cast<unsigned>(kind)];current.sequence.fetch_add(1);s=t::Read(kind);Check(!s.paired,"unfinished snapshot is bounded and never treated as cycle-qualified maximum");current.sequence.fetch_add(1);
 Check(t::Configure(1,true),"optional private cycles can be selected");
 timer=t::Begin(t::Kind::pose_begin);Check(timer.active,"cycle option retains genuine current thread token");t::End(timer);
 Check(t::Read(t::Kind::pose_begin).calls==2,"optional cycle failure does not discard real wall duration");
 auto active=t::Begin(t::Kind::pose_begin);Check(t::Configure(0,false),"configuration can disable while token is alive");t::End(active);
 Check(t::Read(t::Kind::pose_begin).calls==3,"existing token completes once even after mask disable");
 char line[240]{};SetLastError(0x7878);Check(t::Line(t::Kind::pose_begin,line,sizeof(line))&&GetLastError()==0x7878,"coordinator diagnostic preserves LastError and fits worker status buffer");
 Check(!t::Line(t::Kind::count,line,sizeof(line))&&!t::Line(t::Kind::pose_begin,line,4),"invalid kind and truncated report fail explicitly");
 std::printf("%u passed, %u failed\n",passed,failed);return failed?1:0;
}
