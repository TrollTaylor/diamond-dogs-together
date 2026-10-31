// The game window's close against the partner's mission teardown (window_close.hpp): runs 8567f888 and
// e6ef6e4d faulted at close (rva 0xbf421a, then 0x1be23eb) after the window thread let the close through
// with the partner mission still live on the mission thread.
#include "mgscoop/window_close.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
namespace w=mgscoop::window_close;
namespace {
unsigned checks{};
void Check(bool v,const char* text){++checks;if(!v){std::fprintf(stderr,"FAIL %u %s\n",checks,text);std::exit(1);}}
constexpr std::uint64_t t0=1000000;
}
int main(){
 // The verdict, from the crash runs' own lines: partner on, mission 30020, window thread 27160, mission thread 69476.
 Check(w::NeedsMissionThread(true,30020,true,69476,27160),"8567f888: a partner mission on another thread is torn down there first");
 Check(!w::NeedsMissionThread(true,30020,true,27160,27160),"the mission thread itself closing tears down inline");
 Check(!w::NeedsMissionThread(true,0,true,69476,27160),"no partner mission: nothing to hand over");
 Check(!w::NeedsMissionThread(false,30020,true,69476,27160),"partner off: nothing to hand over");
 Check(!w::NeedsMissionThread(true,30020,false,69476,27160),"loading or terminating: the mission thread ticks no partner");
 Check(!w::NeedsMissionThread(true,30020,true,0,27160),"no mission thread yet");

 // The close waits for the mission thread's next partner tick.
 w::ResetForTest();
 Check(!w::Quiet(),"nothing requested: the partner tick runs");
 w::Request();
 Check(w::Quiet(),"requested: the partner tick and CFG stay quiet");
 Check(w::OnClose(t0)==w::Close::hold,"the first WM_CLOSE is held");
 Check(w::OnClose(t0+10)==w::Close::hold,"a second WM_CLOSE while held is swallowed too");
 Check(w::OnTimer(t0+10)==w::Timer::wait,"the hold timer waits");
 Check(w::Claim(),"the mission thread claims the teardown");
 Check(!w::Claim(),"once");
 Check(w::OnClose(t0+20)==w::Close::hold,"still held while the mission thread tears down");
 Check(w::Finish(),"the teardown ends the hold: the mission thread posts the one WM_CLOSE");
 Check(w::OnClose(t0+30)==w::Close::forward,"the posted WM_CLOSE reaches the game");
 Check(w::OnTimer(t0+40)==w::Timer::stop,"the timer stops without a second close");
 Check(w::OnClose(t0+50)==w::Close::forward,"later closes go on");

 // The mission thread never ticks (a pause, a hang): the hold ends after kHoldMs, exactly once.
 w::ResetForTest();
 w::Request();
 Check(w::OnClose(t0)==w::Close::hold,"held");
 Check(w::OnTimer(t0+w::kHoldMs-1)==w::Timer::wait,"held up to kHoldMs");
 Check(w::OnTimer(t0+w::kHoldMs)==w::Timer::release,"the timer ends the hold at kHoldMs and posts the one WM_CLOSE");
 Check(w::OnTimer(t0+w::kHoldMs+1)==w::Timer::stop,"once");
 Check(w::OnClose(t0+w::kHoldMs+2)==w::Close::forward,"the posted WM_CLOSE reaches the game");
 Check(w::Claim(),"a late tick still tears down before the engine does");
 Check(!w::Finish(),"but posts no second close");

 // A duplicate WM_CLOSE after the deadline ends the hold itself; the timer then posts nothing.
 w::ResetForTest();
 w::Request();
 Check(w::OnClose(t0)==w::Close::hold,"held");
 Check(w::OnClose(t0+w::kHoldMs)==w::Close::forward,"a WM_CLOSE at the deadline goes on");
 Check(w::OnTimer(t0+w::kHoldMs)==w::Timer::stop,"the timer posts nothing more");

 // No partner mission: the close goes straight on and the tick is never quieted.
 w::ResetForTest();
 Check(w::OnClose(t0)==w::Close::forward,"no request: the WM_CLOSE goes on");
 Check(!w::Claim()&&!w::Quiet(),"nothing to claim");

 // WM_DESTROY first (DestroyWindow without a WM_CLOSE): it cannot wait; the mission thread may still tear down.
 w::ResetForTest();
 w::Request();
 w::OnDestroy();
 Check(w::OnClose(t0)==w::Close::forward,"nothing is held after WM_DESTROY");
 Check(w::Claim()&&!w::Finish(),"the teardown still runs and posts no close");

 // The teardown finished before the WM_CLOSE was looked at: no hold at all.
 w::ResetForTest();
 w::Request();
 Check(w::Claim(),"claimed");
 Check(!w::Finish(),"nothing held yet: nothing to post");
 Check(w::OnClose(t0)==w::Close::forward,"the WM_CLOSE goes on");

 // Races: the mission thread's Finish against the timer and against a late WM_CLOSE. One WM_CLOSE in every order.
 for(unsigned i=0;i<2000;++i){
  w::ResetForTest();
  w::Request();
  Check(w::OnClose(t0)==w::Close::hold,"race: held");
  std::atomic<unsigned> released{0};
  std::thread mission([&]{if(w::Claim()&&w::Finish())released.fetch_add(1);});
  std::thread window([&]{
   if(i&1){if(w::OnTimer(t0+w::kHoldMs)==w::Timer::release)released.fetch_add(1);}
   else if(w::OnClose(t0+w::kHoldMs)==w::Close::forward)released.fetch_add(1);
  });
  mission.join();window.join();
  if(released.load()!=1){std::fprintf(stderr,"race %u: %u releases\n",i,released.load());Check(false,"race: exactly one WM_CLOSE goes on");}
 }
 for(unsigned i=0;i<2000;++i){
  // The window thread looks at its first WM_CLOSE while the mission thread finishes.
  w::ResetForTest();
  w::Request();
  std::atomic<unsigned> released{0};
  std::thread mission([&]{if(w::Claim()&&w::Finish())released.fetch_add(1);});
  if(w::OnClose(t0)==w::Close::forward)released.fetch_add(1);
  mission.join();
  // Exactly one, and never left to the timer: a hold the mission thread had already finished would cost kHoldMs.
  if(released.load()!=1){std::fprintf(stderr,"first-close race %u: %u releases\n",i,released.load());Check(false,"first-close race: exactly one WM_CLOSE goes on, without the timer");}
 }
 std::printf("window_close_tests: %u checks passed\n",checks);
 return 0;
}
