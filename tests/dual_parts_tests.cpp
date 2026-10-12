#include "dual_parts_fixture.hpp"
#include "dual_parts_local_profile.hpp"
#include "dual_parts_local_profile_contract.hpp"
#include "dual_parts_core_policy.hpp"
#include "native_pose_exclusion.hpp"
#include <cstdio>
namespace prof=mgscoop::dual_parts_local_profile;
namespace dual=mgscoop::dual_parts_core_policy;
namespace pd=mgscoop::native_pose_parts_diagnostic;
namespace birth=mgscoop::native_pose_birth;
namespace gate=mgscoop::native_pose_exclusion;
namespace pins=mgscoop::dual_parts_local_profile_contract;
struct ProfileFixture:CarryFixture {
 static constexpr std::uintptr_t manager=0x600000,selection=0x700000;
 static constexpr std::uintptr_t Array(unsigned k) noexcept {return 0x500000+k*0x100;}
 bool profileTear=false;
 ProfileFixture(){
  Put(player-0x20,image+0x22953d0);Put(player+0x218,std::uint32_t(0));Put(player+0x204,std::uint32_t(0x1a487));
  Put(data+0x180,std::uint32_t(0));Put(data+0x184,std::uint32_t(0));
  Put(image+0x2c3a8a0,manager);Put(manager,image+0x2308648);Put(manager+8,image+0x2c6c0c0);
  Put(image+0x2c6c0c0+0x98,image+0x2c00d40);Put(image+0x2c00d40,image+0x2289588);Put(image+0x2c00d40+0x10,image+0x2c01360);
  Put(image+0x2c00d40+0x1a0,image+0x2b56d90);Put(image+0x2b56d90,image+0x25564f0);Put(player+0x48,image+0x2c1bf20);Put(image+0x2c1bf20,image+0x22973c0);
  Put(image+0x25564f0+0x240,image+0x1e02ba0);Put(image+0x22973c0+0x290,image+0x9d19f0);
  const auto vars=image+0x2c01360;Put(vars+0xfb,std::uint8_t(0));Put(vars+0xf8,std::uint8_t(2));Put(vars+0xf9,std::uint8_t(14));Put(vars+0xfc,std::uint16_t(9));Put(vars+0xfe,std::uint8_t(5));
  Put(player-0x20+0x4f4,std::uint32_t(9));Put(player-0x20+0x4f0,std::uint32_t(0));Put(image+0x2c1bf20+0x194,std::uint32_t(0));
  constexpr unsigned offsets[]={0x40,0x48,0x50,0x58,0x60,0x68};for(unsigned k=0;k<6;++k)Put(data+offsets[k],Array(k));
  Put(Array(0),std::uint8_t(0));Put(Array(1),std::uint8_t(2));Put(Array(2),std::uint8_t(14));Put(Array(3),std::uint8_t(2));Put(Array(4),std::uint16_t(9));Put(Array(5),std::uint8_t(5));
  Put(data+0xc0,selection);Put(selection+8,std::uint16_t(0x203));
 }
 bool operator()(std::uintptr_t p,void* out,std::size_t n) noexcept {
  if(profileTear&&p==image+0x2a82c30&&reads==1)Put(Array(3),std::uint8_t(3));
  return CarryFixture::operator()(p,out,n);
 }
 bool Models(cp::Pair& out){reads=0;return cp::ReadPair(*this,image,player,out);}
 bool Profile(cp::Pair& pair,prof::Profile& out,const char** why=nullptr){reads=0;return prof::ReadPair(*this,image,pair,out,why);}
};
dual::Context Context(const cp::Pair& p){dual::Context c{};c.image=0x140000000;c.player=p.player;c.go=p.go;c.data=p.data;c.state=p.scriptState;c.vars=p.vars;c.generation=91;c.sceneEpoch=9;c.thread=88;c.sequence=12;c.callerRva=0x9c2a2a;c.purposes=3;c.carrySerial=11;c.sharedSerial=43;c.birthSerial[0]=106;c.birthSerial[1]=107;c.flags=0x1a487;c.local0ProfileCurrent=true;
 for(unsigned k=0;k<2;++k){c.block[k]=p.body[k].block;c.ps[k]=p.body[k].state;c.modelCS[k]=p.body[k].cs;c.modelEntry[k]=p.body[k].entry;c.pool[k]=p.body[k].pool;c.status[k]=3;}return c;}
int main(){unsigned checks=0,failed=0;const auto Check=[&](bool ok,const char* why){++checks;if(!ok){++failed;std::printf("FAILED %s\n",why);}};
 ProfileFixture good;cp::Pair pair{};prof::Profile p{};const char* why=nullptr;
 Check(good.Models(pair),"both current owned models from complete typed participant graph");
 Check(good.Profile(pair,p,&why)&&p.parts==2&&p.camo==14&&p.selector==0x203&&p.currentEncoded==2,"actual native desired/current ordinary mapped profile");
 Check(!prof::Refusal(p),"source branch permits no model reload");
 {auto x=good;x.Put(x.player-0x20+0x4f4,std::uint32_t(0xff));Check(!x.Profile(pair,p),"default-face provider is unqualified");}
 for(auto code:{1u,2u,5u,6u}){auto x=good;x.Put(x.image+0x2c01360+0xfb,std::uint8_t(code));Check(!x.Profile(pair,p),"custom/special body profile refused");}
 for(auto code:{26u,27u}){auto x=good;x.Put(x.image+0x2c01360+0xf8,std::uint8_t(code));Check(!x.Profile(pair,p),"special parts normalizer refused");}
 for(auto code:{0x73u,0x74u}){auto x=good;x.Put(x.image+0x2c01360+0xf9,std::uint8_t(code));Check(!x.Profile(pair,p),"special camo normalizer refused");}
 {auto x=good;x.Put(x.image+0x2c1bf20+0x194,std::uint32_t(1));Check(!x.Profile(pair,p),"actual local0 native refresh bit refuses");}
 {auto x=good;x.Put(x.image+0x2c1bf20+0x194,std::uint32_t(2));Check(x.Profile(pair,p),"local1 refresh is independently blocked by parts mask; does not forge bit0");}
 for(unsigned k=0;k<6;++k){auto x=good;if(k==4)x.Put(x.Array(k),std::uint16_t(10));else x.Put(x.Array(k),std::uint8_t(4));Check(!x.Profile(pair,p),"each actual profile field mismatch refuses");}
 {auto x=good;x.Put(x.image+0x2c01360+0xf8,std::uint8_t(3));Check(!x.Profile(pair,p),"mapped/requested suit normalizer pending refused");}
 {auto x=good;x.Put(x.player-0x20+0x4f0,std::uint32_t(0x100));x.Put(x.Array(5),std::uint8_t(0));Check(x.Profile(pair,p),"actual equipment flag zeros only auxiliary");}
 {auto x=good;x.Put(x.image+0x2c01360+0xfb,std::uint8_t(3));x.Put(x.Array(0),std::uint8_t(3));Check(x.Profile(pair,p),"ordinary type3 supported with actual comparison");}
 constexpr unsigned values[]={2,3,4,5,1,6,7};for(unsigned k=0;k<7;++k){auto x=good;x.Put(x.selection+8,std::uint16_t(0x203+k));x.Put(x.Array(3),std::uint8_t(values[k]));Check(x.Profile(pair,p),"all seven exact native encoded-selector branches");}
 {auto x=good;x.Put(x.selection+8,std::uint16_t(200));x.Put(x.Array(3),std::uint8_t(1));Check(x.Profile(pair,p),"outside encoded selector range uses native default1");}
 {auto x=good;x.Put(x.image+0x2c01360+0xf8,std::uint8_t(3));x.Put(x.image+0x2c01360+0xf9,std::uint8_t(0));x.Put(x.Array(1),std::uint8_t(3));x.Put(x.Array(2),std::uint8_t(0));x.Put(x.Array(3),std::uint8_t(0));x.Put(x.Array(5),std::uint8_t(0));Check(x.Profile(pair,p),"native zero-map fallback plus unencoded class and auxiliary classifier");}
 // Actual cold30e50571 tuple: selected0 Snake ordinary camo15/parts8,
 // native encoded3 corresponds to RAW0x204(516), not decimal204(0xCC).
 {auto x=good;const auto vars=x.image+0x2c01360;x.Put(vars+0xf8,std::uint8_t(8));x.Put(vars+0xf9,std::uint8_t(15));
  x.Put(vars+0xfc,std::uint16_t(1));x.Put(vars+0xfe,std::uint8_t(2));x.Put(x.player-0x20+0x4f4,std::uint32_t(1));x.Put(x.image+0x2c1bf20+0x194,std::uint32_t(254));
  x.Put(x.Array(1),std::uint8_t(8));x.Put(x.Array(2),std::uint8_t(15));x.Put(x.Array(3),std::uint8_t(3));x.Put(x.Array(4),std::uint16_t(1));x.Put(x.Array(5),std::uint8_t(2));
  x.Put(x.selection+8,std::uint16_t(0x204));Check(x.Profile(pair,p)&&p.selector==516&&p.currentEncoded==3,"actual healthy raw0x204 cold tuple permits exact currentprofile");
  x.Put(x.Array(3),std::uint8_t(2));Check(!x.Profile(pair,p),"actual raw0x204 still refuses a genuinely different encoded field");}
 for(auto selector:{0u,202u,203u,204u,205u,206u,207u,208u,209u,210u,0x202u,0x20au,0xffffu}){
  auto x=good;x.Put(x.selection+8,static_cast<std::uint16_t>(selector));x.Put(x.Array(3),std::uint8_t(1));
  Check(x.Profile(pair,p),"unsigned native subtraction uses default1 outside RAW0x203..0x209, including old decimal literals");}
 {auto x=good;x.Put(x.selection+8,std::uint16_t(204));x.Put(x.Array(3),std::uint8_t(3));
  Check(!x.Profile(pair,p),"decimal204 cannot borrow hexadecimal0x204 encoded3 permission");}
 // Explicit ownership/type mutations; no synthetic alternate/base VT accepted.
 {auto x=good;x.Put(x.manager,x.image+0x24c72e8);Check(!x.Profile(pair,p),"locator manager base type refuses");}
 {auto x=good;x.Put(x.manager+8,x.image+0x2c6c0c8);Check(!x.Profile(pair,p),"actual BFF050 locator replacement refuses");}
 {auto x=good;x.Put(x.image+0x2c00d40,x.image+0x2294c30);Check(!x.Profile(pair,p),"old guessed Player provider type refuses");}
 {auto x=good;x.Put(x.image+0x2b56d90,x.image+0x2556230);Check(!x.Profile(pair,p),"base/destructor parts-map VT refuses");}
 {auto x=good;x.Put(x.image+0x2c1bf20,x.image+0x2296f20);Check(!x.Profile(pair,p),"base/destructor script VT refuses");}
 {auto x=good;x.Put(x.image+0x25564f0+0x240,x.image+0x1e02ba1);Check(!x.Profile(pair,p),"live parts getter slot mismatch refuses");}
 {auto x=good;x.Put(x.image+0x22973c0+0x290,x.image+0x9c53d0);Check(!x.Profile(pair,p),"unrelated Player secondary290 does not replace script getter");}
 {auto x=good;x.Put(x.player+0x218,std::uint32_t(1));Check(!x.Profile(pair,p),"selected1 odd writer cannot project shared local0 profile");}
 {auto x=good;x.unreadable=x.image+0x2c00d40+0x1a0;Check(!x.Profile(pair,p)&&!p.player,"unreadable provider resets pair output");}
 {auto x=good;x.Put(x.image+0x2c01360+0xfb,std::uint8_t(3));x.Put(x.image+0x2c01360+0xf8,std::uint8_t(3));x.Put(x.image+0x2c01360+0xf9,std::uint8_t(0));x.Put(x.Array(0),std::uint8_t(3));x.Put(x.Array(2),std::uint8_t(0));x.Put(x.Array(3),std::uint8_t(0));x.Put(x.Array(5),std::uint8_t(0));x.Put(x.data+0x48,x.Array(0));Check(!x.Profile(pair,p),"current-array alias refuses independently matching type/parts bytes");}
 {auto x=good;x.Put(x.data+0x60,x.Array(4)+1);Check(!x.Profile(pair,p),"misaligned word current-array refuses");}
 {auto x=good;x.unreadable=x.selection+8;Check(!x.Profile(pair,p),"missing actual selector record refuses");}
 {auto x=good;x.profileTear=true;Check(!x.Profile(pair,p),"current profile tear between actual reads refuses");}
 {auto x=good;x.Put(x.Block(0)+0x138,x.player+8);Check(!x.Models(pair),"dual actual local0 model owner replacement refuses");}
 Check(good.Models(pair),"restore typed dual fixture");
 {const bool retainedClosedJob=true;gate::Gate actual;gate::ReadTicket ticket;
  Check(retainedClosedJob&&actual.TryRead(88,ticket)==gate::Status::entered&&actual.OwnsRead(88,ticket)&&dual::SharedDomains(false,false,false),"real even ticket survives deliberately retained Closed job history");
  Check(!dual::SharedDomains(true,false,false)&&!dual::SharedDomains(false,true,false)&&!dual::SharedDomains(false,false,true),"actual opened/owned body/local carrier domains still refuse");
  Check(actual.EndRead(88,ticket),"shared history regression cleans actual ticket");
 }
 auto c=Context(pair);Check(dual::ContextValid(c),"actual-shaped even dual context");
 for(unsigned k=0;k<15;++k){auto bad=c;if(k==0)bad.sequence|=1;if(k==1)bad.purposes=1;if(k==2)bad.pending=2;if(k==3)bad.carrySerial=0;if(k==4)bad.sharedSerial=0;if(k==5)bad.birthSerial[0]=0;if(k==6)bad.birthSerial[1]=bad.birthSerial[0];if(k==7)bad.dirty=1;if(k==8)bad.local0ProfileCurrent=false;if(k==9)bad.status[0]=1;if(k==10)bad.pool[1]=bad.pool[0];if(k==11)bad.modelCS[0]=bad.modelCS[1];if(k==12)bad.callerRva=0x9c2b51;if(k==13)bad.profileRefresh=1;if(k==14)bad.flags|=0x4000;Check(!dual::ContextValid(bad),"dual scope/lifetime/current profile mismatch never forwards");}
 {auto after=c;after.profileFace++;Check(!mgscoop::partner_input_hook::SameNativePoseDualPartsPoll(c,after),"late desired/current profile value change refuses even if each independently matches");}
 birth::Receipt a{{pair.player,pair.body[0].block,pair.body[0].state,pair.body[0].cs,pair.body[0].entry},77,106,true};birth::Receipt b{{pair.player,pair.body[1].block,pair.body[1].state,pair.body[1].cs,pair.body[1].entry},77,107,true};
 Check(dual::BirthsCurrent(pair,a,b,106,107),"two exact sealed constructor births required");
 {auto bad=a;bad.identity.cs=pair.body[1].cs;Check(!dual::BirthsCurrent(pair,bad,b,106,107),"local0 CS birth rebind refuses");}
 {auto bad=b;bad.serial++;Check(!dual::BirthsCurrent(pair,a,bad,106,107),"local1 lifecycle serial replacement refuses");}
 Check(!dual::BirthsCurrent(pair,a,b,107,107),"one birth receipt cannot stand for both models");
 pd::Witness w{};w.count=w.realized=2;w.local=w.selected=0;w.flags=0x1a487;for(unsigned k=0;k<2;++k){w.actualId[k]=static_cast<std::uint16_t>(k);w.pool[k]=static_cast<std::uint8_t>(pair.body[k].pool);w.parts[k]=3;w.blockMatched[k]=true;}
 Check(dual::NoWork(w),"both retained model slots clean");for(unsigned k=0;k<4;++k){auto late=w;if(k==0)late.mask180=1;if(k==1)late.mask184=2;if(k==2)late.parts[0]=1;if(k==3)late.blockMatched[0]=false;Check(!dual::NoWork(late),"local0 work cannot use Sleep-only remote no-op permission");}
 {gate::Gate actual,foreign;gate::ReadTicket ticket;Check(actual.TryRead(88,ticket)==gate::Status::entered&&actual.OwnsRead(88,ticket),"real noncopyable shared gate receipt");Check(!foreign.OwnsRead(88,ticket)&&!actual.OwnsRead(89,ticket),"cross-gate/thread projected receipt refuses");Check(actual.EndRead(88,ticket)&&!actual.OwnsRead(88,ticket),"ended real shared ticket invalidates permission");}
 {CarryFixture code;std::uintptr_t mismatch=0;for(const auto& site:pins::sites){auto& bytes=code.cells[code.image+site.rva];bytes.assign(site.bytes,site.bytes+site.size);}for(const auto& slot:pins::slots)code.Put(code.image+slot.rva,code.image+slot.target);
 Check(pins::CodeGate(code,code.image,0xa01d000,mismatch),"all full native profile/provider gates and exact slots");const auto last=pins::sites[sizeof(pins::sites)/sizeof(pins::sites[0])-1];code.cells[code.image+last.rva].back()^=1;Check(!pins::CodeGate(code,code.image,0xa01d000,mismatch),"last complete code byte cannot bypass gate");code.cells[code.image+last.rva].back()^=1;code.Put(code.image+pins::slots[0].rva,code.image+pins::slots[0].target+1);Check(!pins::CodeGate(code,code.image,0xa01d000,mismatch),"live slot drift refuses full qualification");Check(!pins::CodeGate(code,code.image,0x1000,mismatch),"truncated image refuses");}
 std::printf("dual_parts_tests: %u checks; %u failures\n",checks,failed);return failed?1:0;
}
