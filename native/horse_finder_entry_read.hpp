#pragma once
// Raw callback-owned diagnostic only. pair=0 means it has NO paired
// birth/shared/writer grant. It never specializes any argument or engine AL.
#include "carry_plugin_read.hpp"
#include <cstdio>
namespace mgscoop::horse_finder_entry_read {
namespace cr=carry_plugin_read;
struct Entry {
 std::uintptr_t primary=0,node=0,owner=0,record=0;
 std::uint16_t victim=0xffff,previous=0xffff,nearHorse=0xffff,mount=0xffff,f4=0,f6=0,f8=0;
 std::uint32_t actor=UINT32_MAX,prompt=0,action=0;std::uint8_t phase=0,state=0,next=0;
};
template<class Reader>bool Read(Reader& r,std::uintptr_t image,std::uintptr_t self,std::uint32_t actor,Entry& e)noexcept {
 e={};e.primary=self;e.actor=actor;std::uintptr_t vt=0,value=0;std::uint32_t one=0,base=UINT32_MAX;
 if(actor>1||!cr::Aligned(self,8)||!cr::Range(self,0x78)||!cr::Get(r,self,vt)||vt!=image+0x23b9ac0||
  !cr::Get(r,self+0x30,vt)||vt!=image+0x23b9b50||!cr::Get(r,self+8,e.owner)||!cr::Range(e.owner,0x550)||
  !cr::Get(r,e.owner,vt)||vt!=image+0x2295210||!cr::Get(r,e.owner+8,one)||one!=1||
  !cr::Get(r,e.owner+0xc,base)||base!=actor||!cr::Get(r,e.owner+0xb8,value)||value!=self||
  !cr::Get(r,self+0x38,e.node)||!cr::Aligned(e.node,8)||!cr::Range(e.node,0x40)||
  !cr::Get(r,e.node,vt)||vt!=image+0x2306628||!cr::Get(r,e.node+0x20,one)||one!=1||
  !cr::Get(r,e.node+0x24,base)||base!=actor||!cr::Get(r,e.owner+0x320,value)||value!=e.node||
  !cr::Get(r,e.node+0x38,value)||value!=self+0x30||!cr::Get(r,self+0x68,e.record)||
  !cr::Aligned(e.record,16)||!cr::Range(e.record,0x6c0))return false;
 return cr::Get(r,e.record+0x78,e.victim)&&cr::Get(r,e.record+0x7c,e.previous)&&cr::Get(r,e.record+0x81,e.phase)&&
  cr::Get(r,e.record+0x380,e.state)&&cr::Get(r,e.record+0x381,e.next)&&cr::Get(r,e.record+0x538,e.mount)&&
  cr::Get(r,e.record+0x6a0,e.nearHorse)&&cr::Get(r,e.record+0x6a4,e.f4)&&cr::Get(r,e.record+0x6a6,e.f6)&&
  cr::Get(r,e.record+0x6a8,e.f8)&&cr::Get(r,e.record+0x6ac,e.prompt)&&cr::Get(r,e.record+0x6b0,e.action);
}
inline bool SameSource(const Entry& a,const Entry& b)noexcept {
 return a.primary==b.primary&&a.node==b.node&&a.owner==b.owner&&a.record==b.record&&a.actor==b.actor&&a.victim==b.victim;
}
struct Snapshot {Entry before{},after{};std::uint64_t at=0;std::uint32_t callerRva=0;std::uint8_t input=0xff;bool read=false,al=false;};
inline bool Line(const Snapshot& s,std::uint64_t now,char* out,std::size_t capacity)noexcept {
 if(!out||!capacity)return false;
 const bool fresh=s.read&&s.at&&now>=s.at&&now-s.at<=1500;
 const int n=std::snprintf(out,capacity,"horseFinder;raw=%u;pair=0;c=%x;t=%x;in=%u;e=%x,%x,%x;x=%x,%x,%x;near=%x,%x;ui=%u,%u;al=%u",
 fresh?1u:0u,s.callerRva,unsigned(s.before.victim),unsigned(s.input),unsigned(s.before.f4),unsigned(s.before.f6),unsigned(s.before.f8),
 unsigned(s.after.f4),unsigned(s.after.f6),unsigned(s.after.f8),unsigned(s.before.nearHorse),unsigned(s.after.nearHorse),
 s.before.prompt,s.after.prompt,s.al?1u:0u);
 return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
