#include "vehicle_lifetime_read.hpp"
#include <map>
#include <cstdio>
namespace life=mgscoop::vehicle_lifetime_read;
namespace cp=mgscoop::native_pose_carry_participants;
namespace vc=mgscoop::vehicle_controller_read;
struct Memory {
 std::map<std::uintptr_t,unsigned char> bytes;
 template<class T> void Put(std::uintptr_t p,T value){const auto* raw=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(T);++i)bytes[p+i]=raw[i];}
 bool operator()(std::uintptr_t p,void* out,std::size_t n){auto* raw=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto x=bytes.find(p+i);if(x==bytes.end())return false;raw[i]=x->second;}return true;}
};
struct Rig {
 static constexpr std::uintptr_t image=0x140000000,service=0x20000,primary=0x40000,slab=0x80000,head=0x81000,records=0x82000,bitmap=0x90000,bitsHeader=0xa0000;
 Memory m;
 Rig(){m.Put(service,image+0x22ac8e0);m.Put(service+0xb0,primary+0x20);m.Put(primary+0x20,image+0x22ed9f0);m.Put(image+0x22ed9f0,image+0xb2dd30);
  m.Put(primary,image+0x22ed930);m.Put(primary+0x1e0,std::uint16_t(23));m.Put(primary+0x30,records);m.Put(primary+0x48,bitmap);
  m.Put(primary+0x1c8,bitsHeader);m.Put(bitsHeader+8,bitmap);m.Put(primary+0x60,slab);m.Put(primary+0x68,head);
  m.Put(primary+0x70,std::uint64_t(0x10000));m.Put(primary+0x78,std::uint64_t(0x10000));m.Put(primary+0x80,std::uint16_t(0x80));
  m.Put(primary+0x82,std::uint16_t(1));m.Put(primary+0x84,std::uint32_t(0x3003d));m.Put(head,image+0x645940);m.Put(head+8,std::uintptr_t(0));
  m.Put(head+0x10,records);m.Put(head+0x18,std::uint64_t(0x80));m.Put(head+0x20,std::uint64_t(23));
 }
 bool Read(life::Allocation& out){return life::ReadAllocation(m,image,0xa01d000,service,out);}
};
struct Models {
 cp::Pair pair{};vc::Observation control{};
 Models(){pair.player=control.player=0x10000;pair.data=control.data=0x20000;pair.scriptState=control.state=0x30000;
  for(unsigned id=0;id<2;++id){pair.body[id].id=id;pair.body[id].block=control.body[id].block=0x40000+id*0x1000;
   control.body[id].primary=0x50000+id*0x1000;pair.body[id].callbackCount=2;pair.body[id].freeCallbacks=2;}
 }
 void Mount(unsigned id){auto& b=pair.body[id];b.freeCallbacks=0;
  b.callbacks[0]={Rig::image+0x124a530,control.body[id].primary,0x95558436a7c8ULL,1,id};
  b.callbacks[1]={Rig::image+0x124a530,control.body[id].primary,0x376fd458e70aULL,1,id};}
 bool Read(life::CallbackCensus& out){return life::Callbacks(Rig::image,pair,control,out);}
};
struct UIRig {
 static constexpr std::uintptr_t image=Rig::image,player=0x100000,ui=0x200000,world=0x300000,manager=0x400000,dispatcher=0x500000,provider=0x600000;
 mgscoop::coop_passenger::detail::EngineRead engine{};Memory m;
 UIRig(){engine.world=world;m.Put(player+0xd8,ui+0x20);m.Put(ui,image+0x23af6d0);m.Put(ui+0x20,image+0x23af700);
  m.Put(ui+0x20+0x28,player);m.Put(image+0x23af700+0x190,image+0xfe71b0);m.Put(world+0x98,manager);m.Put(manager+0x40,dispatcher);
  m.Put(dispatcher,image+0x2200010);m.Put(dispatcher+0x20,provider);m.Put(ui+0x20+0x48,provider);
  m.Put(provider,image+0x2200000);m.Put(image+0x2200000,image+0xee0000);
 }
 bool Read(life::UI& out){return life::ReadUI(m,image,0xa01d000,player,engine,out);}
};
unsigned checks=0,failures=0;void Check(bool x,const char* name){++checks;if(!x){++failures;std::printf("FAIL %s\n",name);}}
int main(){life::Allocation a{},b{};Rig r;
 Check(r.Read(a)&&a.recordMatches==1&&a.descriptors==1,"synthetic complete native allocation descriptor");
 Check(r.Read(b)&&life::Same(a,b),"same actual allocation identity closure");
 r=Rig{};r.m.Put(Rig::primary+0x60,std::uintptr_t(0x80001));Check(!r.Read(b),"unaligned real holder refuses");
 r=Rig{};r.m.Put(Rig::primary+0x82,std::uint16_t(0));Check(!r.Read(b),"native holder not allocated refuses");
 r=Rig{};r.m.Put(Rig::primary+0x84,std::uint32_t(0x3003e));Check(!r.Read(b),"wrong typed allocation tag refuses");
 r=Rig{};r.m.Put(Rig::head+0x18,std::uint64_t(0x90));Check(!r.Read(b),"record descriptor stride must be native80");
 r=Rig{};r.m.Put(Rig::head+0x20,std::uint64_t(24));Check(!r.Read(b),"descriptor count closes current23 allocator");
 r=Rig{};r.m.Put(Rig::head+8,Rig::head);Check(!r.Read(b),"cyclic native descriptor list refuses");
 r=Rig{};r.m.Put(Rig::head+0x10,Rig::records+0x10);Check(!r.Read(b),"exact records descriptor required");
 r=Rig{};r.m.Put(Rig::primary+0x70,std::uint64_t(0x1000));r.m.Put(Rig::primary+0x78,std::uint64_t(0x1000));Check(!r.Read(b),"records cannot lie outside retained slab");
 r=Rig{};r.m.Put(Rig::bitsHeader+8,Rig::bitmap+4);Check(!r.Read(b),"current bitmap alias must match native query");
 Models models;life::CallbackCensus c{};
 Models oldPayload;oldPayload.Mount(1);oldPayload.pair.body[1].callbacks[0].event=0x62bcc8f4fcb6ULL;
 Check(!oldPayload.Read(c),"descriptor payload is not the registered event key");
 oldPayload=Models{};oldPayload.Mount(1);oldPayload.pair.body[1].callbacks[1].event=0x73cf7bf9301dULL;
 Check(!oldPayload.Read(c),"second descriptor payload is not the registered event key");
 Check(models.Read(c)&&life::Retired(c,0)&&life::Retired(c,1),"unmounted callbacks retired without native invocation");
 models.Mount(0);Check(models.Read(c)&&c.present[0]==2&&c.eventMask[0]==3&&life::Retired(c,1),"stock native driver callback pair classified");
 models.Mount(1);Check(models.Read(c)&&c.present[1]==2&&c.eventMask[1]==3,"actual body1 callback pair independently owned");
 models=Models{};models.Mount(1);models.pair.body[1].callbacks[0].owner=models.control.body[0].primary;Check(!models.Read(c),"cross body owner refuses");
 models=Models{};models.Mount(1);models.pair.body[1].callbacks[0].body=0;Check(!models.Read(c),"wrong actual body refuses");
 models=Models{};models.Mount(1);models.pair.body[1].callbacks[1].event=0x95558436a7c8ULL;Check(!models.Read(c),"duplicate callback event refuses");
 models=Models{};models.Mount(1);models.pair.body[1].callbacks[0].function+=0x10;Check(!models.Read(c),"unknown callback on retained controller refuses");
 models=Models{};models.Mount(1);models.pair.body[1].callbacks[0].event|=0xabcd000000000000ULL;Check(models.Read(c),"native low48 event matching preserved");
 models=Models{};models.Mount(1);models.pair.body[1].callbacks[0].owner+=0x100;Check(!models.Read(c),"vehicle thunk with unrelated owner refuses");
 models=Models{};models.pair.data+=8;Check(!models.Read(c),"model and vehicle controller same Q required");
 models=Models{};models.Mount(1);Check(models.Read(c)&&!life::Retired(c,1),"phase-independent callback lifetime remains held");
 UIRig ui;life::UI ua{},ub{};
 Check(ui.Read(ua)&&ua.provider==UIRig::provider&&ua.getter==Rig::image+0xee0000,"observed inner UI getter no invocation or unproved class assertion");
 Check(ui.Read(ub)&&life::Same(ua,ub),"typed UI owner chain same across current reads");
 ui=UIRig{};ui.m.Put(UIRig::ui+0x20+0x28,UIRig::player+8);Check(!ui.Read(ub),"UI exact player owner required");
 ui=UIRig{};ui.m.Put(UIRig::ui+0x20+0x48,UIRig::provider+8);Check(!ui.Read(ub),"UI inner matches constructor dispatcher binding");
 ui=UIRig{};ui.m.Put(UIRig::image+0x2200000,std::uintptr_t(0x900000));Check(!ui.Read(ub),"unknown out of binary getter refuses");
 ui=UIRig{};ui.m.Put(UIRig::ui+0x20,UIRig::image+0x23af708);Check(!ui.Read(ub),"exact final UI secondary class required");
 ub=ua;++ub.provider;Check(!life::Same(ua,ub),"UI provider replacement invalidates ownership closure");
 std::printf("Vehicle lifetime: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
