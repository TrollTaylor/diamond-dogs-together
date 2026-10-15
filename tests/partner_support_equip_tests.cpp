#include <initializer_list>
#include "mgscoop/partner_support_equip.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace se=mgscoop::partner_support_equip;
unsigned checks=0;
void Check(bool ok,const char* label) {++checks;if(!ok){std::fprintf(stderr,"failed: %s\n",label);std::exit(1);}}
void Is(const se::Preconditions& p,const char* reason) {
    const char* actual=se::Refusal(p);
    Check(reason ? actual && std::strcmp(actual,reason)==0 : !actual,reason ? reason : "qualified type12");
}
int main() {
    se::Preconditions good{};good.enabled=good.owner=good.gates=good.body_ready=good.safety_ready=true;
    good.player_flags=1;good.local=good.local_id=good.inventory_local=0;
    good.body_id=1;good.pool=1;good.equip=354;good.type=12;good.sub=0;good.restriction=0;
    Is(good,nullptr);
    auto p=good;p.enabled=false;Is(p,"off");p=good;p.owner=false;Is(p,"thread");
    p=good;p.gates=false;Is(p,"admission");p=good;p.body_ready=false;Is(p,"body");
    p=good;p.safety_ready=false;Is(p,"safety");p=good;p.player_flags=0;Is(p,"offline");
    p=good;p.player_flags=0x101;Is(p,"offline");p=good;p.local=1;Is(p,"p214");
    p=good;p.local_id=1;Is(p,"p218");p=good;p.inventory_local=1;Is(p,"inventory_local");
    for(auto id:{0u,2u,0xFFFFu}) {p=good;p.body_id=static_cast<std::uint16_t>(id);Is(p,"body_id");}
    p=good;p.pool=0xFF;Is(p,"pool");p=good;p.restriction=1;Is(p,"restriction");
    for(auto equip:{0u,0x800u,0xFFFFu}) {p=good;p.equip=static_cast<std::uint16_t>(equip);Is(p,"equip_range");}
    for(auto type:{0u,1u,3u,9u,13u,14u,17u}) {p=good;p.type=static_cast<std::uint8_t>(type);Is(p,"type");}
    for(auto sub:{8u,15u,255u}) {p=good;p.sub=static_cast<std::uint8_t>(sub);Is(p,"sub");}
    p=good;p.request_flags=0;Is(p,"request_flags");p=good;p.request_flags=20;Is(p,"request_flags");
    for(auto equip:{354u,349u,395u,411u,366u,370u,378u,388u}) {p=good;p.equip=static_cast<std::uint16_t>(equip);Is(p,nullptr);}
    std::printf("partner_support_equip_tests: %u checks (pure preconditions; no gameplay evidence)\n",checks);
}
