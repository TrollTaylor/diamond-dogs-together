#include "mgscoop/buddy_pose.hpp"
#include <cmath>
namespace mgscoop::buddy_pose {
namespace {
constexpr std::uintptr_t MAX=0x7fffffffffff;
bool Read(Reader r,std::uintptr_t p,void* out,std::size_t n) noexcept {return r.read&&p&&n&&p<=MAX&&n<=MAX-p&&r.read(r.context,p,out,n);}
template<class T> bool At(Reader r,std::uintptr_t p,std::size_t off,T& out) noexcept {return p&&p<=MAX&&off<=MAX-p&&Read(r,p+off,&out,sizeof(out));}
}
const char* ReadPose(Reader r,std::uintptr_t base,const Scope& s,std::uint32_t thread,std::uint64_t now,std::uint16_t id,Pose& out) noexcept {
 if(!s.exact_executable||!s.exact_code_pins||!r.read||!base||base>MAX-0xa01d000)return "admission";
 if(!s.P||!s.generation||!s.epoch||!s.thread||thread!=s.thread||(!s.paired&&!s.cleanup_only)||!s.field_ready||s.demo_playing||s.terminating||
  (s.mission!=30010&&s.mission!=30020&&s.mission!=30050)||now<s.refreshed_ms||now-s.refreshed_ms>250)return "scope";
 // Only the known Dog class and logical instance zero. Retail dog manager
 // and this replication policy are singleton; a declaration is not existence.
 if(id!=0x2600)return "dog_id";
 std::uintptr_t P{},vt{},registry{},entry{},descriptor{},handle{},selector{},secondary{};
 std::uint16_t mission{},classIndex{},index{};std::uint32_t flags{},count{},local{};std::uint8_t active{},pending{};
 if(!At(r,base,0x2a82c30,P)||P!=s.P||!At(r,base,0x2c01372,mission)||mission!=s.mission||
  !At(r,P,0,vt)||vt!=base+0x2295490||!At(r,P,0x204,flags)||!(flags&1u)||(flags&0x100u)||
  !At(r,P,0x208,count)||count!=2||!At(r,P,0x20c,count)||count!=2||!At(r,P,0x214,local)||local>1)return "player_scope";
 Pose p{};p.id=id;
 // Same pointer walk as A46030 GetActiveBuddyType; no native accessor called.
 if(!At(r,base,0x2c28930,selector)||!At(r,selector,0x10,p.controller)||
  !At(r,p.controller,0x175,active)||active!=2||!At(r,p.controller,0x17f,pending)||pending)return "dog_selection";
 // Exact BF99E0 indexed registry lookup, performed only as bounded reads.
 if(!At(r,base,0x2c389a0,registry)||!At(r,registry,0,vt)||vt!=base+0x23073c0||
  !At(r,registry,0x30+(id>>9)*8u,entry)||!At(r,entry,8,descriptor)||!At(r,descriptor,0x20,p.outer)||
  !At(r,p.outer,0,vt)||vt!=base+0x22bdf30||!At(r,p.outer,0x1c,classIndex)||classIndex!=(id>>9)||
  !At(r,p.outer,0x70,count)||count!=1||!At(r,p.outer,0x80,handle)||!At(r,handle,0x10,p.primary))return "dog_registry";
 if(!At(r,p.primary,0,vt)||vt!=base+0x2315d80||p.primary>MAX-0x20)return "dog_primary_vt";
 secondary=p.primary+0x20;
 if(!At(r,secondary,0,vt)||vt!=base+0x2315db0||!At(r,secondary,8,count)||count!=1||
  !At(r,secondary,0x10,p.kind)||!At(r,p.kind,0x40,count)||count!=1)return "dog_secondary";
 std::uintptr_t indices{};
 if(!At(r,p.kind,0x44,count)||count!=1||!At(r,p.kind,0x28,indices)||!At(r,indices,0,index)||index!=0||
  !At(r,p.kind,0x20,p.positions)||!Read(r,p.positions,p.position.data(),sizeof(p.position)))return "dog_unrealized";
 for(const float f:p.position)if(!std::isfinite(f)||std::fabs(f)>1000000.f)return "dog_position";
 // Same-thread read plus end-to-end identity recheck: no cache survives a
 // registry replacement, load, dog switch or pointer-chain retirement.
 std::uintptr_t current{};std::uint16_t currentMission{};
 if(!At(r,base,0x2a82c30,current)||current!=s.P||!At(r,base,0x2c01372,currentMission)||currentMission!=s.mission||
  !At(r,P,0,vt)||vt!=base+0x2295490||!At(r,P,0x204,flags)||!(flags&1u)||(flags&0x100u)||
  !At(r,P,0x208,count)||count!=2||!At(r,P,0x20c,count)||count!=2||!At(r,P,0x214,local)||local>1||
  !At(r,base,0x2c389a0,current)||current!=registry||!At(r,registry,0,vt)||vt!=base+0x23073c0||!At(r,registry,0x30+(id>>9)*8u,current)||current!=entry||
  !At(r,entry,8,current)||current!=descriptor||!At(r,descriptor,0x20,current)||current!=p.outer||
  !At(r,p.outer,0,vt)||vt!=base+0x22bdf30||!At(r,p.outer,0x1c,classIndex)||classIndex!=(id>>9)||
  !At(r,p.outer,0x70,count)||count!=1||!At(r,p.outer,0x80,current)||current!=handle||!At(r,handle,0x10,current)||current!=p.primary||
  !At(r,p.primary,0,vt)||vt!=base+0x2315d80||!At(r,secondary,0,vt)||vt!=base+0x2315db0||
  !At(r,secondary,8,count)||count!=1||!At(r,secondary,0x10,current)||current!=p.kind||
  !At(r,p.kind,0x40,count)||count!=1||!At(r,p.kind,0x44,count)||count!=1||
  !At(r,p.kind,0x28,current)||current!=indices||!At(r,indices,0,index)||index!=0||!At(r,p.kind,0x20,current)||current!=p.positions||
  !At(r,base,0x2c28930,current)||current!=selector||!At(r,selector,0x10,current)||current!=p.controller||!At(r,p.controller,0x175,active)||active!=2||
  !At(r,p.controller,0x17f,pending)||pending)return "dog_changed";
 out=p;return nullptr;
}
} // namespace
