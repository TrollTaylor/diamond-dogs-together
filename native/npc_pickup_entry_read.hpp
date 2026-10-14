#pragma once
// Raw actual native predicate entry observation only; pair0 grants no owner,
// native admission or write. Reads the precise AF1AA0 matrix source directly.
#include "carry_plugin_read.hpp"
#include <cmath>
#include <cstring>
#include <cstdio>
namespace mgscoop::npc_pickup_entry_read {
namespace cr=carry_plugin_read;
inline constexpr unsigned char matrixGetterBytes[]={0x2b,0x51,0xc,0x48,0x8b,0x41,0x10,0x48,0x8d,0x14,0x52,0x48,0xc1,0xe2,0x6,0x48,0x8b,0x4c,0x2,0x68,0xf,0x28,0x41,0x40,0x41,0xf,0x29,0x0,0xf,0x28,0x49,0x50,0x41,0xf,0x29,0x48,0x10,0xf,0x28,0x41,0x60,0x41,0xf,0x29,0x40,0x20,0xf,0x28,0x49,0x70,0x41,0xf,0x29,0x48,0x30,0xc3};
struct Entry {
 std::uintptr_t primary=0,owner=0,state=0,cs=0,entry=0,matrixOwner=0;
 std::uint32_t actor=UINT32_MAX;std::uint16_t target=0xffff;std::uint8_t stance=255;
 float position[3]{},matrix[16]{},d2=0,dy=0,dot=0,rangeSquared=0;
};
template<class Reader>bool Read(Reader& r,std::uintptr_t image,std::uintptr_t self,std::uint32_t actor,
 std::uint16_t target,std::uintptr_t pos,Entry& e)noexcept {
 e={};e.primary=self;e.actor=actor;e.target=target;std::uintptr_t vt=0,p=0,player=0,data=0,stance=0;
 std::uint32_t one=0,base=UINT32_MAX;unsigned char code[sizeof(matrixGetterBytes)]{};
 if(actor!=0||(target&0xfe00u)!=0x400u||!cr::Aligned(self,8)||!cr::Range(self,0x78)||
  !cr::Get(r,self,vt)||vt!=image+0x23b9ac0||!cr::Get(r,self+8,e.owner)||!cr::Range(e.owner,0x550)||
  !cr::Get(r,e.owner,vt)||vt!=image+0x2295210||!cr::Get(r,e.owner+8,one)||one!=1||!cr::Get(r,e.owner+0xc,base)||base!=actor||
  !cr::Get(r,e.owner+0xb8,p)||p!=self||!cr::Get(r,e.owner+0x78,e.state)||!cr::Aligned(e.state,8)||
  !cr::Get(r,e.state,vt)||(vt!=image+0x23b09f0&&vt!=image+0x23b0c70)||!cr::Get(r,e.state+8,base)||base!=actor||
  !cr::Get(r,e.state+0x408,p)||p!=e.owner||!cr::Get(r,e.state+0x238,e.cs)||!cr::Aligned(e.cs,8)||
  !cr::Get(r,e.owner+0x10,p)||p!=e.cs||!cr::Get(r,e.cs,vt)||vt!=image+0x22e6070||
  !cr::Get(r,vt+0xb0,p)||p!=image+0xaf1aa0||!r(p,code,sizeof(code))||std::memcmp(code,matrixGetterBytes,sizeof(code))||
  !cr::Get(r,e.cs+8,one)||one!=1||!cr::Get(r,e.cs+0xc,base)||base!=actor||!cr::Get(r,e.cs+0x10,e.entry)||!cr::Aligned(e.entry,16)||
  !cr::Get(r,e.entry+0x68,e.matrixOwner)||!cr::Aligned(e.matrixOwner,16)||!cr::Range(e.matrixOwner,0x80)||
  !r(e.matrixOwner+0x40,e.matrix,sizeof(e.matrix))||!cr::Range(pos,sizeof(e.position))||!r(pos,e.position,sizeof(e.position))||
  !cr::Get(r,e.owner+0x138,player)||!cr::Get(r,player,vt)||vt!=image+0x2295490||!cr::Get(r,player+0x60,data)||
  !cr::Get(r,data,vt)||vt!=image+0x23ad970||!cr::Get(r,data+0x30,stance)||!cr::Get(r,stance+actor,e.stance))return false;
 for(float v:e.position)if(!std::isfinite(v)||std::fabs(v)>200000.f)return false;
 for(float v:e.matrix)if(!std::isfinite(v)||std::fabs(v)>200000.f)return false;
 const float dx=e.position[0]-e.matrix[12],dz=e.position[2]-e.matrix[14];
 e.d2=dx*dx+dz*dz;e.dy=e.position[1]-e.matrix[13];e.dot=dx*e.matrix[8]+dz*e.matrix[10];
 e.rangeSquared=e.stance==1?3.4225001335f:3.0625f;
 return std::isfinite(e.d2)&&std::isfinite(e.dot)&&std::isfinite(e.dy);
}
inline bool SameSource(const Entry& a,const Entry& b)noexcept {
 return a.primary==b.primary&&a.owner==b.owner&&a.state==b.state&&a.cs==b.cs&&a.entry==b.entry&&a.matrixOwner==b.matrixOwner&&
  a.actor==b.actor&&a.target==b.target&&a.stance==b.stance&&std::memcmp(a.position,b.position,sizeof(a.position))==0&&
  std::memcmp(a.matrix,b.matrix,sizeof(a.matrix))==0;
}
struct Snapshot{Entry before{},after{};std::uint64_t at=0;std::uint32_t callerRva=0;bool raw=false,al=false;};
inline bool Line(const Snapshot& s,std::uint64_t now,char* out,std::size_t cap)noexcept {
 if(!out||!cap)return false;const bool fresh=s.raw&&s.at&&now>=s.at&&now-s.at<=1500;
 const int n=std::snprintf(out,cap,"carryNpcGate;raw=%u;pair=0;t=%x;c=%x;al=%u;s=%u;d2=%.3f;lim=%.3f;rawDY=%.3f;dot=%.3f;behind=%u;far=%u",
 fresh?1u:0u,unsigned(s.before.target),s.callerRva,s.al?1u:0u,unsigned(s.before.stance),double(s.before.d2),double(s.before.rangeSquared),
 double(s.before.dy),double(s.before.dot),s.before.dot<0?1u:0u,s.before.d2>s.before.rangeSquared?1u:0u);
 // rawDY is target search-point minus native scene origin. The original's
 // effective height may come from112F7B0/112C140/112E7D0, so do not label it
 // the final height gate or infer AL from these preliminary metrics.
 return n>=0&&static_cast<std::size_t>(n)<cap;
}
}

