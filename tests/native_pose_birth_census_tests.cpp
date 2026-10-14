#include "native_pose_birth_census.hpp"
#include <cstdio>
namespace bc=mgscoop::native_pose_birth_census;namespace b=mgscoop::native_pose_birth;namespace c=mgscoop::native_pose_carry_participants;
int main(){unsigned checks=0,failures=0;const auto check=[&](bool value){++checks;if(!value)++failures;};
 c::Pair p{};p.player=10;p.data=20;p.scriptState=30;
 for(unsigned k=0;k<2;++k){p.body[k].id=k;p.body[k].block=40+k;p.body[k].state=50+k;p.body[k].cs=60+k;p.body[k].entry=70+k;}
 const b::Receipt a{{10,40,50,60,70},9,0x100000007ull,true},d{{10,41,51,61,71},9,0x200000007ull,true};
 check(bc::Current(p,a,d));auto bad=d;bad.ready=false;check(!bc::Current(p,a,bad));bad=d;bad.serial=a.serial;check(!bc::Current(p,a,bad));
 bad=d;bad.identity.entry=70;check(!bc::Current(p,a,bad));bad=d;bad.identity.player=11;check(!bc::Current(p,a,bad));
 bad=d;bad.thread=0;check(!bc::Current(p,a,bad));auto changed=p;changed.body[1].id=0;check(!bc::Current(changed,a,d));
 changed=p;changed.body[1].block=40;check(!bc::Current(changed,a,d));changed=p;changed.body[1].cs=60;check(!bc::Current(changed,a,d));
 changed=p;changed.data=0;check(!bc::Current(changed,a,d));changed=p;changed.scriptState=0;check(!bc::Current(changed,a,d));
 std::printf("birth census: %u checks, %u failures\n",checks,failures);return failures?1:0;}
