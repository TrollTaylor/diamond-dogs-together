#pragma once
#include <cmath>
#include <cstdint>
namespace mgscoop::coop_sleep_policy {
// Scalar plans only. The adapter binds the complete native owner and performs
// fresh before/after reads and CAS; a plan is not a native lifecycle receipt.
struct Sample {
 float health=0,maximumHealth=0,stun=0,maximumStun=0;
 std::uint32_t lifeState=0;
 std::uintptr_t graphNode=0;std::uint64_t graphHash=0;
 std::uint16_t qAction=0;
 std::uint8_t mode=0,requested=0,action=0;
 bool nodeActive=false,graphCurrent=false,sleepGraph=false,carried=false;
};
inline bool Valid(const Sample& s) noexcept {
 return std::isfinite(s.health)&&std::isfinite(s.maximumHealth)&&s.health>0&&s.health<=s.maximumHealth&&
  s.maximumHealth<=65535&&std::isfinite(s.stun)&&std::isfinite(s.maximumStun)&&s.stun>=0&&
  s.maximumStun>0&&s.maximumStun<=65535&&s.stun<=s.maximumStun&&s.lifeState<=2&&s.mode<=6&&s.requested<=6;
}
inline bool SleepAction(std::uint16_t action) noexcept {return action>=0x83&&action<=0x85;}
inline bool CarriedAction(std::uint16_t action) noexcept {return action==0x2a||action==0x2b||action==0x2c;}
inline bool Retired(const Sample& s) noexcept {
 return Valid(s)&&!s.carried&&!CarriedAction(s.qAction)&&s.lifeState==0&&s.mode==0&&s.requested==0&&
  !s.nodeActive&&s.action!=0x19&&!SleepAction(s.qAction)&&s.graphCurrent&&!s.sleepGraph;
}
inline bool Seed(const Sample& s) noexcept {
 return Retired(s)&&s.stun>0&&(s.qAction==1||s.qAction==5);
}
inline bool Maintain(const Sample& s,bool entered,bool waking) noexcept {
 if(!Valid(s)||!entered||waking||s.lifeState!=2||s.stun<=0)return false;
 if(s.carried&&CarriedAction(s.qAction))return true;
 return s.mode==5&&s.requested==0&&s.nodeActive&&s.action==0x19&&s.qAction==0x84&&s.graphCurrent&&s.sleepGraph;
}
// Carry must not interrupt the native fall/collapse graph. Rest is stable;
// an existing carried state may retain permission but never starts a pickup.
inline bool CarryReady(const Sample& s) noexcept {
 if(!Valid(s)||s.lifeState!=2)return false;
 if(s.carried&&CarriedAction(s.qAction))return true;
 return s.mode==5&&s.requested==0&&s.nodeActive&&s.action==0x19&&s.qAction==0x84&&s.graphCurrent&&s.sleepGraph;
}
inline bool Wake(const Sample& s) noexcept {
 return Valid(s)&&!s.carried&&!CarriedAction(s.qAction)&&s.lifeState==2&&s.mode==5&&
  (s.requested==0||s.requested==6)&&s.nodeActive&&s.action==0x19&&s.qAction==0x84&&s.graphCurrent&&s.sleepGraph;
}
// Same current maximum used by original native regeneration and retirement.
// Never clears lifeState directly or authorizes input/graph release.
inline bool WakeStun(const Sample& s,float& target) noexcept {
 target=0;if(!Wake(s))return false;target=s.maximumStun;return true;
}
inline bool Entered(const Sample& s) noexcept {
 return Valid(s)&&s.lifeState==2&&((s.mode==4&&s.qAction==0x83)||(s.mode==5&&s.qAction==0x84)||
  (s.carried&&CarriedAction(s.qAction)));
}
struct Retirement {std::uint64_t firstAt=0,lastAt=0;unsigned observations=0;};
inline bool ObserveRetirement(Retirement& r,const Sample& s,std::uint64_t now) noexcept {
 if(!now||!Retired(s)){r={};return false;}
 if(!r.firstAt||now<r.lastAt){r={now,now,1};return false;}
 if(now!=r.lastAt){r.lastAt=now;if(r.observations<2)++r.observations;}
 return r.observations>=2;
}
} // namespace mgscoop::coop_sleep_policy
