#include "../native/coop_passenger.hpp"
#include <cstdio>
#include <map>
#include <string>
using namespace mgscoop::coop_passenger::detail;
namespace {
unsigned checks=0,failures=0;
void Check(bool value,const char* what){++checks;if(!value){++failures;std::printf("FAIL: %s\n",what);}}
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T> void Put(std::uintptr_t p,T value){const auto* raw=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[p+i]=raw[i];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n){auto* raw=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto x=bytes.find(p+i);if(x==bytes.end())return false;raw[i]=x->second;}return true;}
};
struct Rig {
 static constexpr std::uintptr_t image=0x140000000,service=0x100000,object=0x200000,records=0x300000,bitmap=0x400000,metadata=0x500000,table=0x600000,variant=0x700000;
 static constexpr std::uint16_t vehicle=0x6401;
 static constexpr std::uintptr_t unit=records+0x80;
 Memory m;
 Rig(){m.Put(service,image+0x22ac8e0);m.Put(image+0x22ac8e0+0x18,image+0xa272a0);
  m.Put(image+0x22ac8e0+0x168,image+0xa27260);m.Put(image+0x22ac8e0+0x170,image+0xa27220);
  m.Put(service+0xb0,object);m.Put(object,image+0x22ed9f0);m.Put(image+0x22ed9f0,image+0xb2dd30);
  m.Put(object+0x1c0,std::uint16_t(2));m.Put(object+0x10,records);m.Put(object+0x28,bitmap);
  m.Put(bitmap,std::uint32_t(2));m.Put(unit+0x6c,std::uint8_t(0x12));m.Put(service+0xb8+8,metadata);
  m.Put(metadata+0x1b7,std::uint8_t(3));m.Put(metadata+0x1d0,table);m.Put(table+2*8,variant);m.Put(variant+0xb5,std::uint8_t(1));
  for(unsigned i=0;i<4;++i)m.Put(unit+0x30+i*2,std::uint16_t(i?0xffff:1));
 }
 bool Read(Unit& u){return ReadUnit(m,image,service,vehicle,unit,u);}
 Unit Read(){Unit u{};Check(Read(u),"valid native unit chain");return u;}
};
Lease Owned(const Unit& u){Lease l{};l.valid=true;l.issued=1000;l.serial=7;l.mission=30010;l.generation=9;l.thread=4;l.epoch=2;l.player=0x800000;l.primary=0x900000;l.record=0xa00000;l.seat=2;l.vehicle=u;return l;}
}
int main(int argc,char** argv){
 Unit u{};Rig valid;Check(valid.Read(u),"typed service, class, allocation and metadata accepted");
 Check(u.vehicle==0x6401&&u.unit==Rig::unit&&u.allocated==2,"indexed unit is bounded to current records");
 Check(u.count==4&&u.baseCount==3&&u.extraCount==1,"capacity derives from both native metadata sources");
 Check(EmptyPassenger(u)==2,"first real empty passenger seat");
 u.rider[2]=0x401;Check(EmptyPassenger(u)==3,"occupied seat2 chooses seat3");
 u.rider[3]=0x402;Check(EmptyPassenger(u)==4,"no passenger seat refuses");
 u.rider[0]=0xffff;Check(EmptyPassenger(u)==4,"empty driver stays stock driver search");
 u=valid.Read();u.rider[0]=0;Check(EmptyPassenger(u)==4,"local driver cannot also be own passenger");
 {
  Rig r;r.m.Put(Rig::service,Rig::image+0x22ac8df);Check(!r.Read(u),"wrong service final class");
  r=Rig{};r.m.Put(Rig::image+0x22ac8e0+0x18,Rig::image+0xa272a1);Check(!r.Read(u),"lookup ABI slot drift");
  r=Rig{};r.m.Put(Rig::image+0x22ac8e0+0x168,Rig::image+0xa27261);Check(!r.Read(u),"base metadata getter drift");
  r=Rig{};r.m.Put(Rig::image+0x22ac8e0+0x170,Rig::image+0xa27221);Check(!r.Read(u),"variant metadata getter drift");
  r=Rig{};r.m.Put(Rig::object,Rig::image+0x22ed930);Check(!r.Read(u),"primary view is not unit interface");
  r=Rig{};r.m.Put(Rig::image+0x22ed9f0,Rig::image+0xb2dd31);Check(!r.Read(u),"allocation count getter drift");
  r=Rig{};r.m.Put(Rig::object+0x1c0,std::uint16_t(0));Check(!r.Read(u),"unconfigured allocation");
  r=Rig{};r.m.Put(Rig::object+0x1c0,std::uint16_t(513));Check(!r.Read(u),"allocation above GO index extent");
  r=Rig{};r.m.Put(Rig::object+0x1c0,std::uint16_t(1));Check(!r.Read(u),"unit beyond allocation");
  r=Rig{};r.m.Put(Rig::bitmap,std::uint32_t(0));Check(!r.Read(u),"despawned unit bit");
  r=Rig{};r.m.Put(Rig::object+0x10,std::uintptr_t(0));Check(!r.Read(u),"missing records");
  r=Rig{};r.m.Put(Rig::object+0x28,std::uintptr_t(0));Check(!r.Read(u),"missing bitmap");
  r=Rig{};r.m.Put(Rig::service+0xc0,std::uintptr_t(0));Check(!r.Read(u),"missing category metadata");
  r=Rig{};r.m.Put(Rig::metadata+0x1d0,std::uintptr_t(0));Check(!r.Read(u),"missing variant table");
  r=Rig{};r.m.Put(Rig::metadata+0x1b7,std::uint8_t(1));Check(r.Read(u)&&u.count==2&&EmptyPassenger(u)==4,"capacity2 census cannot authorize raw seat2");
  r=Rig{};r.m.Put(Rig::metadata+0x1b7,std::uint8_t(4));Check(!r.Read(u),"capacity5 is outside first prototype");
  r=Rig{};r.m.Put(Rig::metadata+0x1b7,std::uint8_t(255));Check(!r.Read(u),"capacity sum does not wrap");
  r=Rig{};r.m.Put(Rig::variant+0xb5,std::uint8_t(0));Check(r.Read(u)&&u.count==3,"authored three-position vehicle");
  r=Rig{};r.m.Put(Rig::table+2*8,std::uintptr_t(0));Check(r.Read(u)&&u.count==3,"native absent variant adds zero");
  r=Rig{};Check(!ReadUnit(r.m,Rig::image,Rig::service,0x5a01,Rig::unit,u),"wrong GameObject class mask");
  Check(!ReadUnit(r.m,Rig::image,Rig::service,Rig::vehicle,Rig::unit+0x80,u),"borrowed pointer not current indexed unit");
 }
 u=valid.Read();const auto l=Owned(u);
 std::uint8_t first=0,last=1;
 Check(OverrideRange(l,u,l.primary,0,1100,true,false,first,last)&&first==2&&last==2,"prequery narrows to exact seat");
 first=0;last=1;Check(OverrideRange(l,u,l.primary,0,1100,false,true,first,last)&&first==2&&last==2,"phase1 commits same exact seat");
 const auto fail=[&](Lease lease,Unit unit,std::uintptr_t owner,unsigned body,std::uint64_t now,bool pre,bool phase,const char* label){
  first=0;last=1;Check(!OverrideRange(lease,unit,owner,body,now,pre,phase,first,last)&&first==0&&last==1,label);
 };
 fail(l,u,l.primary,1,1100,true,false,"body1 cannot reuse local entry purpose");
 fail(l,u,l.primary+8,0,1100,true,false,"changed controller owner");
 fail(l,u,l.primary,0,999,true,false,"clock reversal");
 fail(l,u,l.primary,0,2501,true,false,"absolute candidate expiry");
 fail(l,u,l.primary,0,1100,false,false,"foreign nearest caller");
 auto bad=l;bad.serial=0;fail(bad,u,l.primary,0,1100,true,false,"synthetic zero lease serial");
 bad=l;bad.seat=4;fail(bad,u,l.primary,0,1100,true,false,"sentinel4 cannot be a seat");
 auto moved=u;moved.rider[2]=0x401;fail(l,moved,l.primary,0,1100,true,false,"native admission already occupied");
 moved=u;++moved.records;fail(l,moved,l.primary,0,1100,true,false,"allocation move");
 moved=u;moved.rider[0]=2;fail(l,moved,l.primary,0,1100,true,false,"driver changed");
 moved=u;++moved.liveWord;fail(l,moved,l.primary,0,1100,true,false,"live allocation bitmap changed");
 Check(SameIdentity(l,l),"immutable lease equality");bad=l;++bad.generation;Check(!SameIdentity(l,bad),"cycle replacement rejected");
 bad=l;++bad.issued;Check(!SameIdentity(l,bad),"candidate time cannot be restamped");
 unsigned char gateway[512]{};
 constexpr std::uintptr_t helper=0x180001234,proceed=0x141246a08,refuse=0x141246e73;
 const auto n=BuildGateway(gateway,sizeof(gateway),helper,proceed,refuse);
 Check(n>128&&n<320,"bounded gateway encoding");
 Check(gateway[0]==0x9c&&gateway[1]==0x50&&gateway[12]==0x48,"flags and volatile registers saved");
 Check(gateway[15]==0x80,"128-byte shadow and XMM frame");
 Check(!BuildGateway(gateway,319,helper,proceed,refuse),"short output buffer refused");
 Check(!BuildGateway(gateway,sizeof(gateway),0,proceed,refuse),"null callback refused");
 const auto n2=BuildGateway(gateway,sizeof(gateway),helper,proceed,refuse);Check(n2==n,"gateway stable");
 unsigned jumps=0;std::uintptr_t targets[2]{};
 for(std::size_t i=0;i+14<=n;++i)if(gateway[i]==0xff&&gateway[i+1]==0x25&&gateway[i+2]==0&&gateway[i+3]==0&&gateway[i+4]==0&&gateway[i+5]==0){
  if(jumps<2)std::memcpy(&targets[jumps],gateway+i+6,8);++jumps;
 }
 Check(jumps==2&&targets[0]==proceed&&targets[1]==refuse,"original continuations preserved");
 unsigned pops=0;for(std::size_t i=0;i<n;++i)if(gateway[i]==0x9d)++pops;Check(pops==2,"both exits restore native flags");
 if(argc==2){std::FILE* f=nullptr;if(fopen_s(&f,argv[1],"wb")||!f)return 3;const auto written=std::fwrite(gateway,1,n,f);std::fclose(f);if(written!=n)return 4;}
 std::printf("coop_passenger_tests: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
