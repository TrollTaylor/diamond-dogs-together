#include "native_pose_exclusion.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace mgscoop::native_pose_exclusion;
static unsigned checks=0;
static void Check(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
static Admission Admitted(std::uint32_t tid=7){return {true,true,true,true,true,true,3,9,tid};}
int main(){
 Check(!MayForwardRefusedJob(true),"pose refusal suppresses");
 Check(MayForwardRefusedJob(false),"ordinary legacy policy unchanged");
 {
  Gate g;ReadTicket r;WriteTicket w;
  Check(g.TryRead(8,r)==Status::entered,"foreign first admitted");
  Check(g.TryWrite(Admitted(),w)==Status::busy,"writer refuses existing original");
  Check(!w.Armed(),"refused writer has no cleanup token");
  Check(!g.EndRead(7,r)&&r.Armed(),"wrong thread cannot forget original");
  Check(g.EndRead(8,r),"foreign returns before exclusive");
  Check(g.TryWrite(Admitted(),w)==Status::entered,"exclusive acquired after original");
  ReadTicket foreign,same;
  Check(g.TryRead(8,foreign)==Status::busy&&!foreign.Armed(),"foreign original cannot race");
  Check(g.TryRead(7,same)==Status::nested_writer&&!same.Armed(),"same owner never waits");
  WriteTicket nested;
  Check(g.TryWrite(Admitted(),nested)==Status::nested_writer&&!nested.Armed(),"nested pose suppressed");
  Check(!g.EndWrite(7,4,9,true,true,w)&&w.Armed(),"generation drift retains cleanup");
  g.StopNewWriters();
  Check(g.Owns(7,3,9,w),"shutdown retains old writer authority");
  Check(g.EndWrite(7,3,9,true,true,w)&&g.Idle(),"all restoration and close precede release");
  Check(!g.EndWrite(7,3,9,true,true,w),"consumed writer is not released twice");
  Check(g.TryWrite(Admitted(),nested)==Status::stopping,"shutdown prevents new writer");
  Check(g.TryRead(8,foreign)==Status::entered,"normal original resumes after cleanup");
  Check(g.EndRead(8,foreign),"normal original balances");
 }
 {
  Gate g;WriteTicket w;Check(g.TryWrite(Admitted(),w)==Status::entered,"cleanup-fault experiment");
  Check(!g.EndWrite(7,3,9,false,true,w)&&w.Armed()&&g.Poisoned()&&!g.Idle(),"post/UI/C0 fault retains exclusion");
  ReadTicket r;Check(g.TryRead(8,r)==Status::poisoned&&!r.Armed(),"fault never forwards foreign original");
  Check(!g.EndWrite(7,3,9,true,true,w),"later fabricated cleanup cannot clear poison");
 }
 {
  Gate g;WriteTicket w;auto a=Admitted();
  a.allParticipantsInstalled=false;Check(g.TryWrite(a,w)==Status::unqualified&&g.Idle(),"incomplete hooks cannot grant permission");
  a=Admitted();a.privateWorker=false;Check(g.TryWrite(a,w)==Status::unqualified,"ordinary game cannot activate");
  a=Admitted();a.copiedSaves=false;Check(g.TryWrite(a,w)==Status::unqualified,"real save cannot activate");
  a=Admitted();a.poseRequired=false;Check(g.TryWrite(a,w)==Status::unqualified,"unleased native job cannot activate");
 }
 // Deterministic live interleaving: reader attempts while writer held, then
 // re-enters only after the actual writer CAS release. No native code involved.
 {
  Gate g;WriteTicket w;Check(g.TryWrite(Admitted(),w)==Status::entered,"interleaved writer acquired");
  std::atomic<unsigned> step{0};std::atomic<bool> refused{false},entered{false},balanced{false};
  std::thread r([&]{
   ReadTicket t;refused.store(g.TryRead(8,t)==Status::busy&&!t.Armed());step.store(1);
   while(step.load()!=2)std::this_thread::yield();
   entered.store(g.TryRead(8,t)==Status::entered);balanced.store(g.EndRead(8,t));step.store(3);
  });
  while(step.load()!=1)std::this_thread::yield();
  Check(refused.load(),"actual foreign entry waits for writer");
  Check(g.EndWrite(7,3,9,true,true,w),"actual exclusion release");
  step.store(2);r.join();
  Check(entered.load()&&balanced.load()&&g.Idle(),"foreign reenters only after typed cleanup");
 }
 std::printf("native_pose_exclusion: %u checks passed\n",checks);
}
