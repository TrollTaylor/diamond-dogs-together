// D7 dog guard policy (docs/vehicles-companions-design.md 6.12, 8.9): the state byte, the guest's guard plan,
// the lab lever and the lease.
#include "mgscoop/dog_guard.hpp"
#include <cstdio>
#include <cstdlib>
namespace g=mgscoop::dog_guard;
namespace {
unsigned checks{};
void Check(bool v,const char* text){++checks;if(!v){std::fprintf(stderr,"FAIL %u %s\n",checks,text);std::exit(1);}}
g::Record Healthy(){g::Record r{};r.life=10000;r.max=10000;r.stun=10000;r.stunMax=10000;return r;}
}
int main(){
 // Encode: the host's record -> the wire byte.
 auto r=Healthy();
 Check(g::Encode(r)==(0x80|31),"full life: valid, normal, 31");
 r.life=5000;Check(g::Encode(r)==(0x80|16),"half life rounds up to 16/31");
 r.life=1;Check(g::Encode(r)==(0x80|1),"any life left reads at least 1");
 r.life=0;Check(g::Encode(r)==(0x80|(2<<5)),"life 0 reads down with life 0");
 r=Healthy();r.state=g::kStateDown;Check(((g::Encode(r)>>5)&3)==2,"state 2 reads down");
 r=Healthy();r.state=g::kStateStunned;r.stun=0;Check(g::Encode(r)==(0x80|(1<<5)|31),"stunned keeps its life");
 r=Healthy();r.max=0;Check(g::Encode(r)==0,"no maximum: unreadable, invalid byte");
 r=Healthy();r.state=7;Check(g::Encode(r)==(0x80|31),"an unknown state reads normal");
 // Decode.
 auto f=g::Decode(0);Check(!f.valid,"0 (an older host) is not valid");
 f=g::Decode(0x80|(1<<5)|9);Check(f.valid&&f.state==1&&f.life31==9,"fields decode");
 f=g::Decode(0x80|(3<<5)|31);Check(f.valid&&f.state==3,"state 3 decodes as is (treated as normal by Guard)");
 // Guard without a valid host byte: pending damage is dropped, nothing else changes.
 auto local=Healthy();local.pending=3000;local.pendingStun=500;
 auto p=g::Guard(local,0,false);
 Check(p.write&&p.clearedPending&&p.next.pending==0&&p.next.pendingStun==0,"pending life and stun damage dropped");
 Check(p.next.life==10000&&!p.kill&&!p.life,"no host byte: life untouched");
 local=Healthy();p=g::Guard(local,0,false);Check(!p.write,"nothing pending, no byte: no write");
 local=Healthy();local.flags=g::kFlagKill|0x10;p=g::Guard(local,0,false);
 Check(p.write&&p.clearedKill&&p.next.flags==0x10,"a forced death the module did not issue is undone, other bits kept");
 p=g::Guard(local,0,true);Check(!p.clearedKill&&!p.write,"the module's own kill stays");
 // A copy already down is the restore path's: untouched.
 local=Healthy();local.state=g::kStateDown;local.life=0;local.pending=200;local.flags=g::kFlagKill;
 p=g::Guard(local,0x80|31,false);Check(!p.write&&!p.kill,"a down copy is left alone");
 // Valid host byte: life follows in 31ths, both ways.
 local=Healthy();p=g::Guard(local,0x80|16,false);
 Check(p.write&&p.life&&p.next.life==5162,"life follows the host (16/31 of 10000, rounded up)");
 local.life=2000;p=g::Guard(local,0x80|31,false);Check(p.write&&p.next.life==10000,"and heals back to full with the host");
 local=Healthy();p=g::Guard(local,0x80|31,false);Check(!p.write,"same life: no write");
 local=Healthy();local.max=7000;local.life=7000;p=g::Guard(local,0x80|1,false);Check(p.next.life==226,"another maximum scales");
 local=Healthy();p=g::Guard(local,0x80|0,false);Check(p.next.life==1,"alive with life 0 in the byte keeps 1");
 // Stun: asleep follows, waking follows.
 local=Healthy();p=g::Guard(local,0x80|(1<<5)|31,false);Check(p.write&&p.stun&&p.next.stun==0,"host asleep: stun 0");
 local=Healthy();local.state=g::kStateStunned;local.stun=0;p=g::Guard(local,0x80|31,false);
 Check(p.write&&p.stun&&p.next.stun==10000,"host awake again: stun back to its maximum");
 local=Healthy();local.stun=4000;p=g::Guard(local,0x80|31,false);Check(!p.stun&&!p.write,"awake on both: local stun untouched");
 // Down: the kill flag once.
 local=Healthy();p=g::Guard(local,0x80|(2<<5),false);
 Check(p.write&&p.kill&&(p.next.flags&g::kFlagKill)&&p.next.life==10000,"host down: the kill flag, life left to the stock update");
 p=g::Guard(local,0x80|(2<<5),true);Check(!p.kill&&!p.write,"kill issued once");
 local=Healthy();local.flags=g::kFlagKill;p=g::Guard(local,0x80|(2<<5),false);Check(p.kill&&!p.clearedKill,"down keeps a set kill flag");
 // Alive host: a local kill flag is undone and life still follows.
 local=Healthy();local.flags=g::kFlagKill;local.pending=9999;p=g::Guard(local,0x80|20,false);
 Check(p.clearedKill&&p.clearedPending&&p.next.life==6452&&p.next.flags==0,"alive host: kill undone, pending dropped, life follows");
 // Lab lever.
 local=Healthy();p=g::Lab(local,1,30);Check(p.write&&p.next.life==3000,"lab life 30%");
 p=g::Lab(local,1,0);Check(p.write&&p.kill&&(p.next.flags&g::kFlagKill),"lab life 0: the kill flag");
 p=g::Lab(local,2,0);Check(p.write&&p.next.stun==0,"lab stun 0");
 p=g::Lab(local,2,100);Check(!p.write,"lab stun 100 on a rested dog: no write");
 p=g::Lab(local,1,101);Check(!p.write,"lab pct over 100 refused");
 p=g::Lab(local,3,10);Check(!p.write,"unknown lab op refused");
 local.state=g::kStateDown;p=g::Lab(local,1,50);Check(!p.write,"lab leaves a down dog alone");
 local=Healthy();local.max=0;p=g::Lab(local,1,50);Check(!p.write,"lab needs a maximum");
 // Lease.
 Check(!g::Armed(5,0),"never armed");
 Check(g::Armed(100,100)&&g::Armed(10,100)&&!g::Armed(101,100),"armed through the lease frame inclusive");
 std::printf("dog guard policy %u checks PASS\n",checks);
 return 0;
}
