#include "mgscoop/buddy_capacity_census.hpp"
#include "mgscoop/buddy_capacity_catalog.hpp"
#include <cmath>
namespace mgscoop::buddy_capacity_census {
namespace {
constexpr std::uintptr_t MAX=0x7fffffffffff;
bool Read(Reader r,std::uintptr_t p,void* out,std::size_t n) noexcept{return r.read&&p&&n&&p<=MAX&&n<=MAX-p&&r.read(r.context,p,out,n);}
template<class T>bool At(Reader r,std::uintptr_t p,std::size_t off,T& out) noexcept{return p&&p<=MAX&&off<=MAX-p&&Read(r,p+off,&out,sizeof(out));}
}
bool Compiled() noexcept {
#if defined(MGS_BACKGROUND_WORKER) && MGS_BACKGROUND_WORKER && defined(MGS_LOCAL_BUDDY_CAPACITY_TEST) && MGS_LOCAL_BUDDY_CAPACITY_TEST
 return true;
#else
 return false;
#endif
}
const char* ReadCensus(Reader r,std::uintptr_t base,Authorization auth,const Owner& o,std::uint32_t thread,std::uint64_t now,bool play,Census& out) noexcept {
 if(!r.read||!base||base>MAX-0xa01d000||!auth.exact_executable||!auth.exact_code_pins)return "admission";
 if(!play&&(!Compiled()||!auth.private_worker||!auth.offline||!auth.copied_saves||!auth.candidate_catalog))return "admission";
 if(!o.P||!o.generation||!o.buddy_cycle||!o.epoch||!o.thread||thread!=o.thread||!o.field_ready||!o.horse_active||o.demo_playing||o.terminating||
   (o.mission!=30010&&o.mission!=30020&&o.mission!=30050)||now<o.refreshed_ms||now-o.refreshed_ms>250)return "scope";
 std::uintptr_t P{},vt{},registry{},entry{},descriptor{},handle{},primary{},selector{},current{};
 std::uint16_t mission{},classIndex{},index{};std::uint32_t flags{},count{},local{};std::uint8_t active{},pending{};
 if(!At(r,base,0x2a82c30,P)||P!=o.P||!At(r,base,0x2c01372,mission)||mission!=o.mission||!At(r,P,0,vt)||vt!=base+0x2295490||
  !At(r,P,0x204,flags)||!(flags&1u)||(flags&0x100u)||!At(r,P,0x208,count)||count!=2||!At(r,P,0x20c,count)||count!=2||!At(r,P,0x214,local)||local>1)return "player_scope";
 Census s{};
 if(!At(r,base,0x2c28930,selector)||!At(r,selector,0x10,s.controller)||!At(r,s.controller,0x175,active)||active!=1||!At(r,s.controller,0x17f,pending)||pending)return "horse_selection";
 // Closed BF99E0 registry lookup and actual HorseSd Lua delegate 9E6060:
 // outer+50 handle -> primary+10 -> exact E4C450 cast -> secondary+20.
 if(!At(r,base,0x2c389a0,registry)||!At(r,registry,0,vt)||vt!=base+0x23073c0||!At(r,registry,0x30+13u*8u,entry)||
  !At(r,entry,8,descriptor)||!At(r,descriptor,0x20,s.outer)||!At(r,s.outer,0,vt)||vt!=base+0x229b880||
  !At(r,s.outer,0x1c,classIndex)||classIndex!=13||!At(r,s.outer,0x40,s.declared_total)||s.declared_total<1||s.declared_total>2||
  !At(r,s.outer,0x44,s.declared_realized)||s.declared_realized>s.declared_total||!At(r,s.outer,0x50,handle)||!At(r,handle,0x10,primary)||primary>MAX-0x20)return "horse_registry";
 s.receiver=primary+0x20;
 if(!At(r,s.receiver,0,vt)||(vt!=base+0x23624e0&&vt!=base+0x2362650))return "receiver_secondary_vt";
 const auto secondaryVt=vt;
 if(!At(r,primary,0,vt)||(secondaryVt==base+0x23624e0?vt!=base+0x23624a8:vt!=base+0x2362620))return "receiver_primary_vt";
 const auto primaryVt=vt;
 if(!At(r,s.receiver,8,s.class_index)||s.class_index!=13||!At(r,s.receiver,0xc,s.count)||s.count!=s.declared_total||
  !At(r,s.receiver,0x68,s.component)||s.component<0x20)return "receiver_count";
 if(!At(r,s.component,0,vt)||vt!=base+0x2336cf0||!At(r,s.component-0x20,0,vt)||vt!=base+0x2367b80)return "component_vt";
 if(!At(r,s.component,0x10,count)||count!=s.count||!At(r,s.component,8,s.records)||!s.records||s.records>MAX-s.count*0x2f0u)return "component_count";
 if(!At(r,s.receiver,0x18,s.kind)||!At(r,s.kind,0x40,count)||count!=s.count||!At(r,s.kind,0x20,s.positions)||!s.positions||s.positions>MAX-s.count*16u||
  !At(r,s.kind,0x44,s.realized_count)||s.realized_count>s.count||!At(r,s.kind,0x28,s.realized_indices))return "realization";
 if(s.realized_count&&(!s.realized_indices||s.realized_indices>MAX-s.realized_count*2u))return "realized_array";
 for(unsigned j=0;j<s.realized_count;++j){if(!At(r,s.realized_indices,j*2u,index)||index>=s.count||(s.realized_mask&(1u<<index)))return "realized_index";s.realized_mask|=1u<<index;}
 for(unsigned i=0;i<s.count;++i){auto& actor=s.actors[i];std::uint8_t occupied{};actor.record=s.records+i*0x2f0u;actor.game_id=static_cast<std::uint16_t>((13u<<9)|i);actor.realized=(s.realized_mask&(1u<<i))!=0;
  if(!At(r,actor.record,0,actor.ride_link)||!At(r,actor.record,0xe,actor.rider)||!At(r,actor.record,0x10,actor.pending_rider)||!At(r,actor.record,0x2dd,occupied)||
   !Read(r,s.positions+i*16u,actor.position.data(),sizeof(actor.position)))return "actor_record";
  actor.occupied=occupied!=0;for(float f:actor.position)if(!std::isfinite(f)||std::fabs(f)>1000000.f)return "position";
  if(actor.occupied&&(!actor.ride_link||actor.rider==0xffff))return "rider_record";
 }
 // Revalidate all ownership and array identities before exposing count/index1.
 if(!At(r,base,0x2a82c30,current)||current!=P||!At(r,base,0x2c01372,mission)||mission!=o.mission||!At(r,P,0,vt)||vt!=base+0x2295490||
  !At(r,P,0x204,flags)||!(flags&1u)||(flags&0x100u)||!At(r,P,0x208,count)||count!=2||!At(r,P,0x20c,count)||count!=2||!At(r,P,0x214,local)||local>1||
  !At(r,base,0x2c28930,current)||current!=selector||!At(r,selector,0x10,current)||current!=s.controller||!At(r,s.controller,0x175,active)||active!=1||!At(r,s.controller,0x17f,pending)||pending||
  !At(r,base,0x2c389a0,current)||current!=registry||!At(r,registry,0,vt)||vt!=base+0x23073c0||!At(r,registry,0x30+13u*8u,current)||current!=entry||
  !At(r,entry,8,current)||current!=descriptor||!At(r,descriptor,0x20,current)||current!=s.outer||!At(r,s.outer,0,vt)||vt!=base+0x229b880||
  !At(r,s.outer,0x1c,classIndex)||classIndex!=13||!At(r,s.outer,0x40,count)||count!=s.declared_total||!At(r,s.outer,0x44,count)||count!=s.declared_realized||
  !At(r,s.outer,0x50,current)||current!=handle||!At(r,handle,0x10,current)||current!=primary||!At(r,primary,0,vt)||vt!=primaryVt||!At(r,s.receiver,0,vt)||vt!=secondaryVt||
  !At(r,s.receiver,8,classIndex)||classIndex!=13||!At(r,s.receiver,0xc,count)||count!=s.count||!At(r,s.receiver,0x68,current)||current!=s.component||
  !At(r,s.component,0,vt)||vt!=base+0x2336cf0||!At(r,s.component-0x20,0,vt)||vt!=base+0x2367b80||!At(r,s.component,0x10,count)||count!=s.count||
  !At(r,s.component,8,current)||current!=s.records||!At(r,s.receiver,0x18,current)||current!=s.kind||!At(r,s.kind,0x40,count)||count!=s.count||
  !At(r,s.kind,0x44,count)||count!=s.realized_count||!At(r,s.kind,0x20,current)||current!=s.positions||!At(r,s.kind,0x28,current)||current!=s.realized_indices)return "identity_changed";
 for(unsigned j=0,mask=0;j<s.realized_count;++j){if(!At(r,s.realized_indices,j*2u,index)||index>=s.count||(mask&(1u<<index)))return "identity_changed";mask|=1u<<index;if(j+1==s.realized_count&&mask!=s.realized_mask)return "identity_changed";}
 // A read-only census cannot publish an idle actor if a natural mount or
 // pending ownership handoff changed while the pointer chain was validated.
 for(unsigned i=0;i<s.count;++i){std::uintptr_t link{};std::uint16_t rider{},pendingRider{};std::uint8_t occupied{};
  if(!At(r,s.actors[i].record,0,link)||link!=s.actors[i].ride_link||!At(r,s.actors[i].record,0xe,rider)||rider!=s.actors[i].rider||
   !At(r,s.actors[i].record,0x10,pendingRider)||pendingRider!=s.actors[i].pending_rider||!At(r,s.actors[i].record,0x2dd,occupied)||
   (occupied!=0)!=s.actors[i].occupied)return "rider_changed";
 }
 // Controller+0x38 is the retained main FPK hash copied by 0xa4428b. Read it
 // twice after the typed census is stable. Unknown, zero and a changing hash
 // refuse the receipt; a match only names the requested pack.
 std::uint64_t pack{}, again{};
 if(!At(r,s.controller,0x38,pack)||!At(r,s.controller,0x38,again))return "pack_unknown";
 if(pack!=again)return "pack_changed";
 s.pack_token=buddy_capacity::MatchHorseMainPack(pack);
 if(!s.pack_token)return "pack_unknown";
 s.pack_hash=pack;
 out=s;return nullptr;
}
const char* ReadCurrentCensus(Reader r,std::uintptr_t base,Authorization auth,const Owner& o,std::uint32_t thread,std::uint64_t now,Census& out) noexcept {
 return ReadCensus(r,base,auth,o,thread,now,false,out);
}
const char* ReadPlayPose(Reader r,std::uintptr_t base,Authorization auth,const Owner& o,std::uint32_t thread,std::uint64_t now,Census& out) noexcept {
 return ReadCensus(r,base,auth,o,thread,now,true,out);
}
}
