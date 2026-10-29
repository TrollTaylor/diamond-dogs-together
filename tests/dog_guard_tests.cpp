// D7 dog guard policy (docs/vehicles-companions-design.md 6.12, 8.9): the state byte, the guest's guard plan,
// the lab lever and the lease.
#include "mgscoop/dog_guard.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
 // Install gate (review 2026-10-29): the two slots are patched only on a PC running M1's key companion_host_owned.
 Check(g::InstallRefusal(true,false)!=nullptr&&std::strcmp(g::InstallRefusal(true,false),"key_off")==0,"key off: nothing installed");
 Check(g::InstallRefusal(false,false)!=nullptr&&std::strcmp(g::InstallRefusal(false,false),"key_off")==0,"key off wins over the gate");
 Check(g::InstallRefusal(false,true)!=nullptr&&std::strcmp(g::InstallRefusal(false,true),"partner_gate")==0,"partner gate refused");
 Check(g::InstallRefusal(true,true)==nullptr,"key on and the gate passed: installed");
 // Receipt admission: a full arm needs the paired exact scope; through a link loss an armed guest's lease is held
 // drop-only (no follow, no lab op) until the session or the mission ends; a hold never starts the guard.
 g::Scope sc{};sc.installed=sc.labMode=sc.exact=sc.runtime=sc.mission=sc.configured=sc.paired=sc.linkUp=sc.epoch=true;sc.effective=3;
 auto same=[](const char* a,const char* b){return a&&b&&std::strcmp(a,b)==0;};
 auto a=g::Admit(true,true,1,0,sc);Check(!a.reason&&a.grant==g::Grant::full&&!a.lab,"linked guest: full arm");
 a=g::Admit(true,true,1,1,sc);Check(!a.reason&&a.grant==g::Grant::full&&a.lab,"linked guest: full arm with its lab op");
 a=g::Admit(true,true,0,0,sc);Check(!a.reason&&a.grant==g::Grant::none,"guest arm 0: a disarm, always answered");
 a=g::Admit(false,false,0,0,sc);Check(!a.reason&&a.grant==g::Grant::none,"host read: answered");
 a=g::Admit(false,false,1,0,sc);Check(same(a.reason,"role")&&a.grant==g::Grant::none,"the host never arms");
 a=g::Admit(false,false,2,0,sc);Check(same(a.reason,"role")&&a.grant==g::Grant::none,"the host never holds");
 a=g::Admit(true,false,1,0,sc);Check(same(a.reason,"role"),"a role that is not this copy's is refused");
 a=g::Admit(false,false,0,1,sc);Check(!a.reason&&a.lab&&a.grant==g::Grant::none,"host lab op in the lab, linked");
 auto off=sc;off.labMode=false;a=g::Admit(false,false,0,1,off);Check(same(a.reason,"lab_only")&&!a.lab,"lab op refused in play");
 off=sc;off.installed=false;a=g::Admit(true,true,1,0,off);Check(same(a.reason,"admission")&&a.grant==g::Grant::none,"not installed");
 off=sc;off.exact=false;off.armed=true;a=g::Admit(true,true,1,0,off);Check(same(a.reason,"admission")&&a.grant==g::Grant::none,"not exact: no hold");
 for(int i=0;i<4;++i) {
  auto lost=sc;lost.armed=false;
  if(i==0) lost.paired=false; else if(i==1) lost.linkUp=false; else if(i==2) lost.effective=2; else lost.epoch=false;
  a=g::Admit(true,true,1,0,lost);Check(same(a.reason,"scope")&&a.grant==g::Grant::none,"link lost, never armed: refused (no new arm)");
  lost.armed=true;a=g::Admit(true,true,1,0,lost);Check(same(a.reason,"hold")&&a.grant==g::Grant::hold&&!a.lab,"link lost, armed: held drop-only");
  a=g::Admit(true,true,1,1,lost);Check(a.grant==g::Grant::hold&&!a.lab,"a held lease takes no lab op");
  a=g::Admit(true,true,2,0,lost);Check(same(a.reason,"hold")&&a.grant==g::Grant::hold,"an explicit hold while unpaired");
  a=g::Admit(false,false,0,1,lost);Check(same(a.reason,"scope")&&!a.lab,"host lab op needs the link");
 }
 auto lost=sc;lost.paired=false;lost.armed=false;
 a=g::Admit(true,true,2,0,lost);Check(same(a.reason,"not_armed")&&a.grant==g::Grant::none,"a hold never starts the guard");
 lost.armed=true;
 for(int i=0;i<4;++i) {
  auto end=lost;
  if(i==0) end.terminating=true; else if(i==1) end.mission=false; else if(i==2) end.runtime=false; else end.configured=false;
  a=g::Admit(true,true,2,0,end);Check(same(a.reason,"scope")&&a.grant==g::Grant::none,"mission end, another mission or thread: no hold");
 }
 auto linkedHold=sc;linkedHold.armed=true;a=g::Admit(true,true,2,0,linkedHold);Check(a.grant==g::Grant::hold,"a hold sent as the link returns is still a hold");
 // Reply freshness: the record is reported only from a snapshot under a second old.
 Check(g::Fresh(true,0)&&g::Fresh(true,1000)&&!g::Fresh(true,1001)&&!g::Fresh(false,0),"record fields only when fresh");
 // Log caps per mission: the first 16 drops then every 50th; 64 follow lines.
 Check(g::DropLogged(1)&&g::DropLogged(16)&&!g::DropLogged(17)&&g::DropLogged(50)&&!g::DropLogged(51)&&g::DropLogged(100)&&!g::DropLogged(0),"drop log cap");
 Check(g::FollowLogged(0)&&g::FollowLogged(63)&&!g::FollowLogged(64),"follow log cap");
 std::printf("dog guard policy %u checks PASS\n",checks);
 return 0;
}
