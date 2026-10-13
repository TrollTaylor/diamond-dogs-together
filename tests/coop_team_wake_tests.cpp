#include "coop_team_wake.hpp"
#include <cstdio>
#include <cstring>
#include <initializer_list>
namespace t=mgscoop::team_wake;
int main(){unsigned checks=0,failed=0;auto ck=[&](bool value,const char* why){++checks;if(!value){++failed;std::printf("FAIL %s\n",why);}};
 t::Receipt r{};auto parse=[&](const char* s){return t::Parse(s,std::strlen(s),r);};
 ck(parse("return 'MGS_COOP_TEAM_V1' -- 30050 7 0 0 1 2 1"),"legacy state parses without grant");
 t::Context c{30050,7,1200,1000,true,true,false};auto d=t::Evaluate(r,c);
 ck(d.known&&d.peerAsleep&&!d.peerWake&&!d.putDown,"V1 never authorizes wake for sleeping peer");
 for(unsigned cause:{1u,2u}){r={30050,7,19,19,false,true,true,true,0,static_cast<std::uint8_t>(cause)};d=t::Evaluate(r,c);
  ck(d.known&&d.peerDown&&d.peerWake&&!d.peerAsleep&&!d.peerCarryable&&d.putDown,"positive exact grant authorizes stock drop for wound or sleep while canonical down remains true");}
 r={30050,7,19,18,false,true,true,true,0,2};d=t::Evaluate(r,c);ck(d.known&&d.peerAsleep&&!d.peerWake&&!d.putDown,"different grant generation preserves native sleep duty");
 r.grantedGeneration=19;
 for(unsigned k=0;k<7;++k){auto q=c;if(k==0)q.admitted=false;if(k==1)q.ownerLive=false;if(k==2)q.terminating=true;if(k==3)q.mission=30020;if(k==4)q.epoch=8;if(k==5)q.now=999;if(k==6)q.now=1501;
  d=t::Evaluate(r,q);bool held=true;if(d.known)held=d.peerAsleep;ck(!d.known&&!d.peerWake&&!d.putDown&&held,"stale/foreign/unowned context cannot release existing duty");}
 auto edge=c;edge.now=1500;ck(t::Evaluate(r,edge).peerWake,"inclusive fresh boundary");
 r.peerGeneration=0;r.grantedGeneration=0;ck(!t::Evaluate(r,c).known,"zero-generation downed V2 is malformed");
 r={30050,7,19,19,false,true,true,true,0,2};r.version2=false;ck(!t::Valid(r),"legacy receipt cannot carry generation authority");
 r={30050,7,19,0,false,false,true,true,0,0};d=t::Evaluate(r,c);ck(d.known&&!d.peerDown&&!d.peerWake&&!d.peerAsleep&&d.putDown,"fresh actual healthy readback can release held presentation");
 ck(parse("return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 1 4294967295 4294967295")&&t::Evaluate(r,c).peerWake,"full unsigned32 generation parses exactly");
 const char* bad[]={"return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 1 19 -1","return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 1 19 4294967296",
 "return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 1 0 19","return 'MGS_COOP_TEAM_V2' -- 30050 0 0 0 1 2 1 19 19",
 "return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 3 1 19 19","return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 0 19 19",
 "return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 1 19 19 extra","return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 1 19 19 ",
 "return 'MGS_COOP_TEAM_V2' -- 30050 7 0 0 1 2 1 19","return 'MGS_COOP_TEAM_V1' -- 30050 7 0 0 1 2 1 19 19"};
 for(const char* s:bad)ck(!parse(s)&&!t::Evaluate(r,c).known,"malformed payload clears parse output and cannot release owner");
 ck(!t::Parse(nullptr,128,r),"null input bounded");std::printf("coop_team_wake_tests %u checks, %u failures; pure fixtures only\n",checks,failed);return failed?1:0;}
