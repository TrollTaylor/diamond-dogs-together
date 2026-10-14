#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
namespace mgscoop::native_pose_reject_diagnostic {
enum class Stage:unsigned {arm,batch,validate,pool,begin,admission,busy,capture,pre,before,count};
inline const char* Name(Stage s)noexcept {switch(s){case Stage::arm:return "arm";case Stage::batch:return "batch";case Stage::validate:return "validate";case Stage::pool:return "pool";case Stage::begin:return "begin";case Stage::admission:return "admission";case Stage::busy:return "busy";case Stage::capture:return "capture";case Stage::pre:return "pre";case Stage::before:return "before";default:return "unknown";}}
inline std::uint32_t Bit(Stage s)noexcept{return s<Stage::count?1u<<static_cast<unsigned>(s):0;}
inline bool First(std::uint32_t previous,Stage s)noexcept {const auto bit=Bit(s);return bit&&!(previous&bit);}
struct Snapshot {bool player=false,c0=false;std::uint32_t flags=0,count=0,realized=0,real=UINT32_MAX,selected=UINT32_MAX,active=UINT32_MAX,requested=UINT32_MAX;};
template<class R,class T>bool Read(R& r,std::uintptr_t p,T& v)noexcept{return p&&p<=UINTPTR_MAX-sizeof(v)&&r(p,&v,sizeof(v));}
template<class R>bool Once(R& r,std::uintptr_t image,std::uintptr_t player,Snapshot& s)noexcept {
 s={};if(!image||image>UINTPTR_MAX-0xa01d000||!player||(player&7u)||player>UINTPTR_MAX-0x388)return false;
 std::uintptr_t v=0,iface=0;if(!Read(r,image+0x2a82c30,v)||v!=player||!Read(r,player,v)||v!=image+0x2295490||
  !Read(r,player+0x204,s.flags)||!Read(r,player+0x208,s.count)||!Read(r,player+0x20c,s.realized)||
  !Read(r,player+0x214,s.real)||!Read(r,player+0x218,s.selected))return false;s.player=true;
 if(!Read(r,player+0xc0,iface)||iface<0x20||(iface&7u)||iface>UINTPTR_MAX-0x70||
  !Read(r,iface,v)||v!=image+0x23c7500||!Read(r,iface-0x20,v)||v!=image+0x23c74c8||!Read(r,iface+8,v)||v!=player)return true;
 std::uint8_t active=0,requested=0;if(!Read(r,iface+0x6c,active)||!Read(r,iface+0x6d,requested))return true;
 s.active=active;s.requested=requested;s.c0=true;return true;
}
inline bool Same(const Snapshot& a,const Snapshot& b)noexcept{return a.player==b.player&&a.c0==b.c0&&a.flags==b.flags&&a.count==b.count&&a.realized==b.realized&&a.real==b.real&&a.selected==b.selected&&a.active==b.active&&a.requested==b.requested;}
template<class R>bool Pair(R& r,std::uintptr_t image,std::uintptr_t player,Snapshot& s)noexcept{s={};Snapshot a{},b{};if(!Once(r,image,player,a)||!Once(r,image,player,b)||!Same(a,b))return false;s=b;return true;}
// Observation only: no bit/count/mode is an admission flag or a waiver.
}
