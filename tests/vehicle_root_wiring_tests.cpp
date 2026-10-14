#include <cstdio>
#include <cstdlib>
#include <string>
#include "vehicle_root_wiring.hpp"
namespace v=mgscoop::vehicle_root_wiring;
namespace s=mgscoop::vehicle_mirror_state;
unsigned checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %u: %s\n",checks,why);std::exit(1);}}
v::Identity Identity(){v::Identity a{};a.image=1;a.player=2;a.data=3;a.state=4;a.block[0]=5;a.block[1]=6;
 a.cs[0]=7;a.cs[1]=8;a.entry[0]=9;a.entry[1]=10;a.birth[0]=11;a.birth[1]=12;a.lifecycle=13;
 a.generation=14;a.sceneEpoch=15;a.thread=16;a.mission=30010;a.epoch=17;return a;}
int main(){
 auto a=Identity();v::Cache cache{};std::uint16_t ids[v::names]{};
 for(unsigned k=0;k<v::names;++k)ids[k]=static_cast<std::uint16_t>(0x6400+k);
 std::uint16_t id=0;Check(!v::productionActivation,"production refuses unproven sender backlog age");
 Check(!v::Resolve(cache,a,{s::NameKind::light,0},100,id),"disabled/unpublished cache grants no name");
 const auto nonce=v::Begin(cache,a,100);Check(nonce==1,"positive challenge");
 Check(v::Accept(cache,a,nonce,ids,130),"current challenge accepts bounded lookup results");
 Check(v::Resolve(cache,a,{s::NameKind::light,0},130,id)&&id==0x6400,"light mapping");
 Check(v::Resolve(cache,a,{s::NameKind::light,12},130,id)&&id==0x640c,"last light");
 Check(v::Resolve(cache,a,{s::NameKind::truck,0},130,id)&&id==0x640d,"truck mapping uses independent authored name");
 Check(v::Resolve(cache,a,{s::NameKind::truck,5},130,id)&&id==0x6412,"last truck");
 Check(!v::Resolve(cache,a,{s::NameKind::light,13},130,id),"out-of-range light refuses");
 Check(!v::Resolve(cache,a,{s::NameKind::truck,6},130,id),"out-of-range truck refuses");
 Check(!v::Resolve(cache,a,{static_cast<s::NameKind>(0),0},130,id),"invalid kind refuses");
 Check(!v::Resolve(cache,a,{s::NameKind::light,0},129,id),"backward clock refuses");
 Check(v::Resolve(cache,a,{s::NameKind::light,0},880,id),"750ms cache boundary");
 Check(!v::Resolve(cache,a,{s::NameKind::light,0},881,id),"expired name does not refresh on read");
 Check(!v::Accept(cache,a,nonce,ids,150),"challenge is single-use");
 for(unsigned k=0;k<16;++k){auto b=a;
  switch(k){case 0:++b.image;break;case 1:++b.player;break;case 2:++b.data;break;case 3:++b.state;break;
  case 4:++b.block[0];break;case 5:++b.block[1];break;case 6:++b.cs[0];break;case 7:++b.cs[1];break;
  case 8:++b.entry[0];break;case 9:++b.entry[1];break;case 10:++b.birth[0];break;case 11:++b.birth[1];break;
  case 12:++b.lifecycle;break;case 13:++b.generation;break;case 14:++b.sceneEpoch;break;case 15:++b.thread;break;}
  Check(!v::Resolve(cache,b,{s::NameKind::light,0},200,id),"changed native identity refuses stale mapping");
 }
 auto b=a;b.epoch++;Check(!v::Resolve(cache,b,{s::NameKind::light,0},200,id),"world epoch mismatch");
 b=a;b.mission=30020;Check(!v::Resolve(cache,b,{s::NameKind::light,0},200,id),"mission mismatch");
 auto token=v::Begin(cache,a,1000);Check(!v::Accept(cache,a,token+1,ids,1010),"wrong challenge consumes pending");
 Check(!v::Accept(cache,a,token,ids,1011),"wrong challenge cannot later replay");
 token=v::Begin(cache,a,1100);Check(!v::Accept(cache,a,token,ids,1601),"lookup challenge age is query time");
 token=v::Begin(cache,a,1700);ids[1]=ids[0];Check(!v::Accept(cache,a,token,ids,1710),"alias vehicle names refuse");ids[1]=0x6401;
 token=v::Begin(cache,a,1800);ids[0]=ids[1]=0xffff;Check(v::Accept(cache,a,token,ids,1810),"multiple absent names allowed");
 Check(!v::Resolve(cache,a,{s::NameKind::light,0},1811,id),"absent name refuses");
 token=v::Begin(cache,a,1900);b=a;b.birth[0]++;Check(!v::Accept(cache,b,token,ids,1910),"birth replacement while Lua looks up names refuses");
 cache.serial=UINT32_MAX;Check(!v::Begin(cache,a,2000),"challenge cannot wrap");
 std::string text=v::publish;text+="123";for(unsigned k=0;k<v::names;++k)text+=" "+std::to_string(0x6400+k);
 std::uint32_t parsed=0;Check(v::Parse(text.data(),text.size(),parsed,ids)&&parsed==123&&ids[18]==0x6412,"exact bounded publish parser");
 Check(!v::Parse((text+" ").data(),text.size()+1,parsed,ids),"trailing text refuses");
 auto bad=text;bad.replace(bad.find("123"),3,"4294967296");Check(!v::Parse(bad.data(),bad.size(),parsed,ids),"nonce overflow refuses");
 bad=text;bad.replace(bad.find("25600"),5,"65536");Check(!v::Parse(bad.data(),bad.size(),parsed,ids),"GameObjectId overflow refuses");
 bad=text;bad.replace(bad.find("123"),3,"-1");Check(!v::Parse(bad.data(),bad.size(),parsed,ids),"signed challenge refuses");
 Check(!v::Parse(text.data(),text.size()-6,parsed,ids),"truncated vector refuses");
 mgscoop::partner_net::ReliableOrigin origin{};origin.incarnation=100;origin.session=1;origin.peer={0x7f000001,7777};origin.localRole=0;origin.peerRole=1;
 s::Link link{};Check(v::Link(origin,30010,2,link)&&link.serial==100,"link uses captured incarnation, not resettable local session");
 auto newer=origin;newer.incarnation++;Check(!v::SameOrigin(origin,newer),"same session counter/new actual incarnation refuses");
 newer=origin;newer.peer.port++;Check(!v::SameOrigin(origin,newer),"different authenticated peer refuses");
 newer=origin;newer.localRole=1;newer.peerRole=0;Check(!v::SameOrigin(origin,newer),"role swap refuses");
 newer=origin;newer.acceptedTick=9000;Check(v::SameOrigin(origin,newer),"queue acceptance age is separate from channel identity");
 Check(!v::Link(origin,30010,0,link),"no paired epoch refuses");
 Check(!v::Link(origin,10010,2,link),"unsupported mission refuses");
 ids[0]=0x6400;ids[1]=0x6401;cache.serial=0;token=v::Begin(cache,a,3000);
 Check(v::Accept(cache,a,token,ids,3001),"owned advisory cache fixture");
 mgscoop::partner_input_hook::NativePoseJobContext x{};x.modelReady=x.selectedBodyOwned=true;x.selectedBody=1;x.sequence=11;
 x.player=a.player;x.block=a.block[1];x.modelCS=a.cs[1];x.modelEntry=a.entry[1];x.birthSerial=a.birth[1];
 x.generation=a.generation;x.sceneEpoch=a.sceneEpoch;x.thread=a.thread;
 Check(v::OwnedCache(cache,x,a.mission,a.epoch,3002),"genuine current selected1 receipt can read advisory local names");
 auto y=x;y.selectedBody=0;Check(!v::OwnedCache(cache,y,a.mission,a.epoch,3002),"local0 cannot impersonate owned selected1");
 y=x;y.sequence=12;Check(!v::OwnedCache(cache,y,a.mission,a.epoch,3002),"even cold context cannot impersonate writer");
 y=x;y.birthSerial++;Check(!v::OwnedCache(cache,y,a.mission,a.epoch,3002),"owned changed body birth refuses");
 y=x;y.modelCS++;Check(!v::OwnedCache(cache,y,a.mission,a.epoch,3002),"owned changed model refuses");
 y=x;y.sceneEpoch++;Check(!v::OwnedCache(cache,y,a.mission,a.epoch,3002),"owned changed scene refuses");
 y=x;y.thread++;Check(!v::OwnedCache(cache,y,a.mission,a.epoch,3002),"foreign owned thread refuses");
 Check(!v::OwnedCache(cache,x,a.mission,a.epoch,3800),"owned writer cannot freshen expired Lua lookup");
 std::printf("%u wiring cache/origin checks passed; no game\n",checks);
}
