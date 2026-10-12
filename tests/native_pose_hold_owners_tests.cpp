#include "native_pose_hold_owners.hpp"
#include "native_pose_exclusion.hpp"
#include <iostream>
namespace h=mgscoop::native_pose_hold_owners;
int main(){unsigned n=0,f=0;auto C=[&](bool v,const char* x)noexcept{++n;if(!v){++f;std::cerr<<x<<"\n";}};h::Owners o{};
 C(!h::ClearSleep(o),"no sleep hold cannot clear");C(!h::AddCarry(o,0),"zero lease refused");
 C(h::AddSleep(o)&&o.held==h::sleep,"sleep acquire");C(h::AddCarry(o,8)&&o.held==h::all,"dual purpose acquire");
 C(!h::AddCarry(o,9)&&o.carrySerial==8,"foreign carry cannot replace active owner");
 C(h::ClearSleep(o),"sleep can request own retirement");C(!h::Closed(o,false)&&o.held==h::all,"failed cleanup retains both");
 C(!h::Closed(o,true)&&o.held==h::carry&&o.carrySerial==8,"sleep retirement cannot release carry");
 C(!h::ClearCarry(o,9)&&!o.pending,"foreign lease cannot clear carry");C(h::ClearCarry(o,8),"owned carry clear");
 C(!h::Closed(o,false)&&o.held==h::carry&&o.carrySerial==8,"failed carry cleanup retains exact lease");
 C(h::Closed(o,true)&&!o.held&&!o.carrySerial,"clean carry retirement releases own lease");
 C(h::AddCarry(o,10)&&h::AddSleep(o),"reverse acquire order");C(h::ClearCarry(o,10),"carry own clear while sleep held");
 C(h::Closed(o,true)&&o.held==h::sleep,"carry retirement retains sleep hold");
 C(h::ClearSleep(o)&&!h::Closed(o,true)&&!o.held,"final sleep clear");
 C(h::AddSleep(o)&&h::ClearSleep(o)&&h::AddSleep(o)&&!o.pending,"renewed sleep cancels old pending sleep clear");
 o={8,0,0};C(!h::AddSleep(o)&&!h::AddCarry(o,11),"invalid owner bits never gain lease");
 namespace px=mgscoop::native_pose_exclusion;px::Gate gate;px::WriteTicket wt;px::ReadTicket rt;
 px::Admission a{true,true,true,true,true,true,7,8,9};unsigned published=0;
 C(gate.TryWrite(a,wt)==px::Status::entered,"actual pure writer entered");
 C(gate.EndWritePublish(9,7,8,true,true,wt,[&]()noexcept{
   ++published;C(!gate.Owns(9,7,8,wt)&&wt.Armed(),"reservation excludes reentrant writer release while ticket retained");
   C(gate.TryRead(10,rt)==px::Status::busy,"foreign lifecycle reader excluded during publication");
 }),"retirement published then writer released");
 C(published==1&&gate.Idle(),"publication exactly once before release");
 C(gate.TryRead(10,rt)==px::Status::entered&&gate.EndRead(10,rt),"reader enters only after published release");
 px::Gate reservationFailed;px::WriteTicket rw;C(reservationFailed.TryWrite(a,rw)==px::Status::entered,"reservation-failure writer admitted");
 C(!reservationFailed.EndWritePublishRefusedForTest(9,7,8,true,true,rw,[&]()noexcept{++published;}),"injected reservation CAS refusal");
 C(published==1&&rw.Armed()&&reservationFailed.Owns(9,7,8,rw),"failed reservation keeps hold bookkeeping and writer intact");
 C(reservationFailed.EndWrite(9,7,8,true,true,rw),"failed reservation can retry normal verified release");
 px::Gate failed;px::WriteTicket fw;C(failed.TryWrite(a,fw)==px::Status::entered,"failed-cleanup writer admitted");
 C(!failed.EndWritePublish(9,7,8,false,true,fw,[&]()noexcept{++published;}),"failed cleanup refuses publication");
 C(published==1&&fw.Armed()&&failed.Poisoned(),"failed cleanup retains ownership and holds unchanged");
 std::cout<<n<<" checks; "<<f<<" failures\n";return f?1:0;}
