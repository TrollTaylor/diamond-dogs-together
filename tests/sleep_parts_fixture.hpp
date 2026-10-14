#define main PriorSleepPluginFixtureMain
#include "sleep_plugin_tests.cpp"
#undef main
#include "sleep_partner_owned_graph.hpp"
#include "sleep_loaded_requests.hpp"
#include "sleep_partner_transition_read.hpp"
#include <cmath>
namespace owned=mgscoop::sleep_partner_owned_read;namespace og=mgscoop::sleep_partner_owned_graph;namespace tr=mgscoop::sleep_partner_transition_read;
owned::Scope OS(){owned::Scope s{};s.exactGate=s.privateWorker=s.copiedSaves=s.required=s.ownedWindow=s.serialized=s.participantsLive=s.selectedBodyOwned=s.modelReady=s.pairedReady=s.fieldReady=true;s.image=0x140000000;s.player=0x10000;s.data=0x20000;s.state=0x30000;s.go=0xffe0;s.block=0x406a8;s.ps=0x51000;s.modelCS=0xe1000;s.modelEntry=0xf1000;s.pp=0x150000;s.source=0x170000;s.windowPPVT=0x160000;s.mission=s.ownedMission=30050;s.epoch=s.ownedEpoch=2;s.generation=s.ownedGeneration=17;s.armGeneration=91;s.armEpoch=9;s.thread=s.ownerThread=88;s.sequenceBefore=s.sequenceAfter=11;s.now=1000;s.poll=900;s.birthSerial=107;return s;}
Memory OwnedFixture(){auto m=Fixture();const auto s=OS();m.Put(s.image+0x2a82c30,s.player);m.Put(s.go,s.image+0x22953d0);m.Put<std::uint32_t>(s.player+0x218,1);m.Put(s.player+0xb0,s.pp);m.Put(s.pp,s.windowPPVT);m.Put(s.pp+8,s.source);m.Put(s.pp+0x280,s.player);m.Put(s.source,s.image+0x21534e0);m.Put<std::uint32_t>(s.pp+0x28,0);m.Put<std::uint32_t>(s.pp+0x30,0);m.Put<std::uint32_t>(s.pp+0x460,0);
 for(unsigned k=0;k<6;++k){m.Put<float>(s.pp+0x10+k*4,0);m.Put<float>(s.source+0x58+k*4,0);}for(unsigned k=0;k<4;++k)m.Put<std::uint32_t>(s.source+0x48+k*4,0);m.Put<std::uintptr_t>(s.data+0x30,0x190000);
 for(unsigned k=0;k<2;++k){const auto ps=std::uintptr_t(0x50000+k*0x1000),block=std::uintptr_t(0x40000+k*0x6a8),cs=std::uintptr_t(0xe0000+k*0x1000),entry=std::uintptr_t(0xf0000+k*0x1000),vector=std::uintptr_t(0x110000+k*0x1000),controller=std::uintptr_t(0x120000+k*0x1000),life=std::uintptr_t(0xc0000+k*0x4c);
  m.Put(ps+0x238,cs);m.Put(block+0x10,cs);m.Put(cs,s.image+0x22e6070);m.Put<std::uint32_t>(cs+8,1);m.Put<std::uint32_t>(cs+0xc,k);m.Put(cs+0x10,entry);m.Put(entry+0x90,vector);m.Put(vector,controller);m.Put(controller,s.image+0x250ba50);m.Put(controller+0x10,s.image+0x250bc00);m.Put<std::uint32_t>(controller+0xd4,0);m.Put<std::uint8_t>(controller+0x185,0);m.Put<std::uintptr_t>(controller+0xf0,0x130000+k*0x1000);m.Put<std::uintptr_t>(controller+0x1b8,0x200000);m.Put<std::uintptr_t>(controller+0x1a0,0x200120);
  m.Put<float>(life+0x24,6600);m.Put<float>(life+0x28,6600);m.Put<float>(life+0x2c,3300);m.Put<float>(life+0x30,3300);m.Put<std::uintptr_t>(ps+0x298,0x180000+k*0x100);m.Put<std::uint32_t>(0x180000+k*0x100,0x11);
  // PS2D0 is deliberately unrelated to the exact native packed-stance array.
  m.Put<std::uintptr_t>(ps+0x2d0,0x1a0000+k*0x100);m.Put<std::uint8_t>(0x1a0000+k*0x100,3);m.Put<std::uint8_t>(0x190000+k,1);m.Put<std::uint8_t>(0xa0000+k*0x1000,0x44);m.Put<std::uint16_t>(0xd0000+k*2,1);
  // This is intentionally unrelated. The qualified chain is PS408->block138.
  m.Put<std::uintptr_t>(ps+0x138,0xdeadbeef);
 }

 const std::uintptr_t ui=0x220020,cam=0x230020,selector=0x240000,request=0x250000,records=0x260000;
 m.Put(s.player+0xd8,ui);m.Put(ui,s.image+0x23af700);m.Put(ui-0x20,s.image+0x23af6d0);m.Put(ui+0x28,s.player);m.Put<std::uint32_t>(ui+0x3c0,0);m.Put<std::uint32_t>(ui+0x3c4,0);m.Put<std::uint8_t>(ui+0x3f0,0);m.Put<std::uint8_t>(ui+0x3f3,0);
 m.Put(s.player+0xb8,cam);m.Put(cam,s.image+0x23b8250);m.Put(cam-0x20,s.image+0x23b8218);m.Put(cam+0x348,s.player);m.Put<std::uint32_t>(cam+0x32c,0);
 m.Put(s.player+0xd0,selector);m.Put(selector,s.image+0x22e8f00);m.Put(selector+0x10,s.player);m.Put<std::uint32_t>(s.modelEntry+0xb0,4);m.Put(s.modelEntry+0x98,request);m.Put<std::uint8_t>(request+8,2);m.Put<std::uint32_t>(request+0xc,0x58);m.Put(request+0x10,records);for(unsigned k=0;k<0x58;++k)m.Put<std::uint8_t>(records+k,0);
 const char magic[15]="FOXMOTIONGRAPH";for(unsigned k=0;k<15;++k)m.Put<std::uint8_t>(0x200000+k,static_cast<std::uint8_t>(magic[k]));m.Put<std::uint32_t>(0x200010,6);m.Put<std::uint32_t>(0x20001c,4);m.Put<std::int32_t>(0x200020,0x20);m.Put<std::uint32_t>(0x200048,17);m.Put<std::int32_t>(0x20004c,0xd4);
 for(unsigned k=0;k<17;++k){const auto field=0x120+k*0x48+0x18,pool=0xf00+k*8;m.Put<std::int32_t>(0x200000+field,static_cast<std::int32_t>(pool-field));m.Put<std::uint64_t>(0x200000+pool,k?mgscoop::sleep_loaded_requests::hashes[k-1]:0x1234567890abull);}return m;
}
