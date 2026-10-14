#include "mgscoop/native_pose_local_carry_slice.hpp"
#include <cstdio>
using namespace mgscoop::partner_input_hook;
int main(){unsigned n=0,bad=0;auto ck=[&](bool v,const char* s){++n;if(!v){++bad;std::printf("FAIL %s\n",s);}};
 NativePoseLocalCarryContext a{};a.image=0x140000000;a.player=0x500000;a.go=0x600000;a.data=0x700000;
 a.block1=0x800000;a.state1=0x900000;a.modelCS1=0xa00000;a.modelEntry1=0xb00000;
 a.armGeneration=3;a.armEpoch=7;a.thread=11;a.sequence=19;a.birth1=31;a.carrierBirth0=37;a.sliceNonce=41;a.carrySerial=43;
 a.sleepHeld=true;a.carryLease={43,3,7};ck(SameNativePoseLocalCarrySlice(a,a),"unchanged exact installed-domain receipt");
 auto change=[&](auto fn,const char* name){auto b=a;fn(b);ck(!SameNativePoseLocalCarrySlice(a,b),name);};
 change([](auto& b){++b.image;},"image replacement");change([](auto& b){++b.player;},"player replacement");
 change([](auto& b){++b.go;},"GO replacement");change([](auto& b){++b.data;},"owner data replacement");
 change([](auto& b){++b.block1;},"recipient block replacement");change([](auto& b){++b.state1;},"recipient state replacement");
 change([](auto& b){++b.modelCS1;},"model owner replacement");change([](auto& b){++b.modelEntry1;},"model entry replacement");
 change([](auto& b){++b.armGeneration;},"Arm generation changed");change([](auto& b){++b.armEpoch;},"scene changed");
 change([](auto& b){++b.thread;},"foreign thread");change([](auto& b){++b.sequence;},"window ended/replaced");
 change([](auto& b){++b.birth1;},"recipient lifetime changed");change([](auto& b){++b.carrierBirth0;},"carrier lifetime changed");
 change([](auto& b){++b.sliceNonce;},"another exclusive slice");change([](auto& b){++b.carrySerial;},"another carry purpose");
 change([](auto& b){b.realLocal=1;},"real selection changed");change([](auto& b){b.selected=1;},"selected shadow changed");
 change([](auto& b){b.sleepHeld=false;},"sleep lease retired before native request");
 change([](auto& b){++b.carryLease.serial;},"lease serial changed");change([](auto& b){++b.carryLease.generation;},"lease generation changed");
 change([](auto& b){++b.carryLease.sceneEpoch;},"lease scene changed");
 // This pure equality deliberately accepts identical empty structures. It is
 // not an admission function; runtime first obtains two real guarded receipts.
 NativePoseLocalCarryContext empty{};ck(SameNativePoseLocalCarrySlice(empty,empty),"equality cannot fabricate writer admission");
 std::printf("native_pose_local_carry_slice_tests %u checks, %u failures (pure only)\n",n,bad);return bad?1:0;}
