#include "mgscoop/partner_support_policy.hpp"
#include "mgscoop/partner_support_equip.hpp"
#include "mgscoop/partner_support_guard.hpp"
#include "mgscoop/partner_fire_filter.hpp"
#include "mgscoop/partner_slots.hpp"
#include <cstdio>
#include <initializer_list>
#include <cstring>
namespace sp=mgscoop::partner_support_policy;
namespace se=mgscoop::partner_support_equip;
namespace sg=mgscoop::partner_support_guard;
namespace sf=mgscoop::partner_fire_filter;
namespace slots=mgscoop::partner_slots;
int main(){
 unsigned checks=0,fail=0;
 const auto check=[&](bool v,const char* label){++checks;if(!v){++fail;std::printf("FAIL %s\n",label);}};
 sp::GateIn good{true,false,true,true,true,true,true,true,true,true,3,12,354};
 check(sp::Admit(good),"grenade admitted");
 {auto x=good;x.live=false;check(!sp::Admit(x),"non-live refused");x=good;x.stale=true;check(!sp::Admit(x),"stale refused");x=good;x.gun_valid=false;check(!sp::Admit(x),"missing gun group refused");x=good;x.p218_window=true;check(!sp::Admit(x),"implicit required pose p218 window refused");}
 check((sp::Strip(0,true,true,true)&0x8058u)==0x8040u,"support pad keeps hold/fire and strips action/cqc/reload");
 check((sp::Strip(0,true,true,false)&0x8078u)==0x8078u,"refused support pad strips action/cqc/hold/fire/reload");
 for(unsigned slot=0;slot<16;++slot)for(unsigned type=0;type<32;++type){auto x=good;x.slot=static_cast<std::uint8_t>(slot);x.type=static_cast<std::uint8_t>(type);
  check(sp::Admit(x)==(slot==3 && type==12),"slot/type matrix incl placed and arms");}
 for(unsigned i=0;i<10;++i){auto x=good;switch(i){case 0:x.enabled=false;break;case 1:x.latched=true;break;case 2:x.guard_installed=false;break;case 3:x.guard_armed=false;break;case 4:x.filter_live=false;break;case 5:x.filter_armed=false;break;case 6:x.slot_valid=false;break;case 7:x.equip_valid=false;break;case 8:x.body_ready=false;break;default:x.record_ready=false;}
  check(!sp::Admit(x),"missing safety owner refuses");}
 for(unsigned equip: {0u,0x800u,0xFFFFu}){auto x=good;x.equip=static_cast<std::uint16_t>(equip);check(!sp::Admit(x),"equip range");}
 for(unsigned id=0;id<4;++id)for(unsigned cls=0;cls<8;++cls)for(unsigned thread=0;thread<3;++thread)for(unsigned window=0;window<2;++window){
  const bool expected=id==1 && (cls==2 || cls==3) && thread==1 && window;
  check(sg::detail::ShouldForce(id,cls,0x1234,0x1234,1,thread,window!=0,true)==expected,"guard policy matrix");}
 check(!sg::detail::ShouldForce(1,2,0x1234,0x5678,1,1,true,true),"wrong P");
 check(!sg::detail::ShouldForce(1,2,0x1234,0x1234,1,1,true,false),"key off");
 for(unsigned owner: {0u,1u,2u,0x201u,0xFFFFu})for(unsigned preview=0;preview<2;++preview){
  (void)preview;check(sf::detail::Drops(1,static_cast<std::uint16_t>(owner))==(owner==1),"full owner incl preview, zero and soldiers");}
 se::Preconditions p{};p.enabled=p.owner=p.gates=p.body_ready=p.safety_ready=true;p.player_flags=1;p.local=0;p.local_id=0;p.inventory_local=0;p.body_id=1;p.equip=354;p.pool=2;p.type=12;p.sub=0;p.restriction=0;
 check(se::Refusal(p)==nullptr,"equip admitted");
 for(unsigned i=0;i<20;++i){auto q=p;const char* reason="";switch(i){
 case 0:q.enabled=false;reason="off";break;case 1:q.owner=false;reason="thread";break;case 2:q.gates=false;reason="admission";break;
 case 3:q.body_ready=false;reason="body";break;case 4:q.safety_ready=false;reason="safety";break;case 5:q.player_flags=0;reason="offline";break;
 case 6:q.player_flags=0x101;reason="offline";break;case 7:q.local=1;reason="p214";break;case 8:q.local_id=1;reason="p218";break;
 case 9:q.inventory_local=1;reason="inventory_local";break;case 10:q.body_id=0;reason="body_id";break;case 11:q.pool=0xFF;reason="pool";break;
 case 12:q.restriction=1;reason="restriction";break;case 13:q.equip=0;reason="equip_range";break;case 14:q.equip=0x800;reason="equip_range";break;
 case 15:q.type=13;reason="type";break;case 16:q.type=14;reason="type";break;case 17:q.sub=8;reason="sub";break;
 case 18:q.request_flags=0;reason="request_flags";break;default:q.request_flags=20;reason="request_flags";}
 const auto why=se::Refusal(q);check(why && std::strcmp(why,reason)==0,"equip refusal");}
 sp::StockLatch latch{};check(!sp::CheckStock(latch,true,true,12,12)&&!latch.off,"stock unchanged");
 check(!sp::CheckStock(latch,false,true,0,12)&&!latch.off,"unread stock is not changed evidence");
 check(sp::CheckStock(latch,true,true,12,11)&&latch.off,"stock change latches");
 check(!sp::CheckStock(latch,true,true,12,12)&&latch.off,"latch persists");
 check(slots::StripBits(slots::Gate::support)==0x40,"support permits HOLD/FIRE only");
 check(slots::StripBits(slots::Gate::unsupported)==0x58,"placed/arms unsupported mask");
 slots::AimGateIn aim{};aim.enabled=aim.judged=aim.slot_valid=true;aim.gate=slots::Gate::support;aim.slot=3;
 check(slots::AimGate(aim)==slots::AimRefusal::none,"support toggle aim");aim.slot=6;check(slots::AimGate(aim)==slots::AimRefusal::slot,"arms no aim");
 std::printf("%u support policy checks, %u failures\n",checks,fail);return fail?1:0;
}
