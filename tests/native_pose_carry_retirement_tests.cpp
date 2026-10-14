#include "native_pose_carry_participants.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <iostream>
namespace cp=mgscoop::native_pose_carry_participants;
struct Fixture {
 const std::uintptr_t image=0x140000000ull,data=0x10000,slab=0x20000,queue=slab+0x100,descriptor=slab+0x800;
 cp::Pair pair{};std::map<std::uintptr_t,std::vector<unsigned char>> cells;std::uintptr_t unreadable=0;
 template<class T>void Put(std::uintptr_t p,T value){auto& x=cells[p];x.resize(sizeof(value));std::memcpy(x.data(),&value,sizeof(value));}
 bool operator()(std::uintptr_t p,void* out,std::size_t n)noexcept {if(p==unreadable)return false;const auto i=cells.find(p);if(i==cells.end()||i->second.size()!=n)return false;std::memcpy(out,i->second.data(),n);return true;}
 Fixture(){
  pair.player=0x9000;pair.data=data;Put(data-0x20,image+0x23ad938);Put(data+0x1a0,slab);Put(data+0x1b0,std::uint64_t(0x1000));Put(data+0x1b8,std::uint64_t(0x200));Put(data+0x1c2,std::uint16_t(1));Put(data+0x1a8,descriptor);Put(data+0x1d8,queue);
  Put(descriptor,image+0x645940);Put(descriptor+8,std::uintptr_t(0));Put(descriptor+0x10,queue);Put(descriptor+0x18,std::uint64_t(0x40));Put(descriptor+0x20,std::uint64_t(2));
  for(unsigned id=0;id<2;++id){auto& b=pair.body[id];b.id=id;const auto base=std::uintptr_t(0x100000+id*0x100000);
   b.carrierNode=base;b.carrierRecords=base+0x1000;b.state=base+0x2000;b.recipientNode=base+0x3000;b.recipientRecords=base+0x4000;b.callbackSlab=base+0x5000;b.carrier=base+0x6000;b.callbackCount=2;
   Put(b.carrierNode+8,base+0x9000);Put(base+0x9000,std::uint32_t(0));Put(b.carrierNode+0x10,base+0x9004);Put(base+0x9004,std::uint32_t(0));
   Put(b.carrierRecords+0x370,std::uintptr_t(0));Put(b.carrierRecords+0x240,std::uint32_t(0));for(auto off:{0x248,0x250,0x258})Put(b.carrierRecords+off,std::uintptr_t(0));
   Put(b.carrierRecords+0x380,std::uint8_t(0));Put(b.carrierRecords+0x381,std::uint8_t(0));Put(b.state+0x280,base+0xa000);Put(base+0xa000,std::uint32_t(0));
   Put(b.recipientNode+8,base+0x9008);Put(base+0x9008,std::uint32_t(0));Put(b.recipientNode+0x10,base+0x900c);Put(base+0x900c,std::uint32_t(0));
   Put(b.recipientRecords+0x20,std::uintptr_t(0));Put(b.recipientRecords+0x28,std::uintptr_t(0));Put(b.recipientRecords+0x30,std::uint16_t(0xffff));Put(b.recipientRecords+0x10,std::uint8_t(0));Put(b.recipientRecords+0x11,std::uint8_t(0));
   Put(queue+id*0x40+8,std::uint8_t(0));for(unsigned slot=0;slot<2;++slot)Put(b.callbackSlab+slot*0x60+0x48,std::uintptr_t(0));
  }
 }
 bool Retired()noexcept{return cp::Retired(*this,image,pair);}
};
int main(){unsigned n=0,f=0;auto C=[&](bool v,const char* why){++n;if(!v){++f;std::cerr<<why<<"\n";}};
 Fixture ok;C(ok.Retired(),"typed bounded queue plus both inactive participants");
 {Fixture x;x.Put(x.data-0x20,x.image+0x23ad939);C(!x.Retired(),"wrong Q primary type");}
 {Fixture x;x.Put(x.data+0x1d8,std::uintptr_t(0xdead000));C(!x.Retired(),"readable-looking foreign queue outside owned slab");}
 {Fixture x;x.Put(x.descriptor+8,x.descriptor);C(!x.Retired(),"descriptor cycle");}
 {Fixture x;x.Put(x.descriptor+0x20,std::uint64_t(1));C(!x.Retired(),"wrong request count");}
 {Fixture x;x.Put(x.descriptor+0x18,std::uint64_t(0x20));C(!x.Retired(),"wrong packet stride");}
 {Fixture x;x.Put(x.descriptor,std::uintptr_t(x.image+0x645941));C(!x.Retired(),"wrong allocation callback");}
 {Fixture x;x.Put(x.data+0x1b8,std::uint64_t(0x1001));C(!x.Retired(),"used bytes exceed backing extent");}
 {Fixture x;x.Put(x.data+0x1c2,std::uint16_t(0));C(!x.Retired(),"uninitialized Q allocation");}
 for(unsigned id=0;id<2;++id){
  for(auto kind:{std::uint8_t(14),std::uint8_t(15)}){Fixture x;x.Put(x.queue+id*0x40+8,kind);C(!x.Retired(),"carry queued request cannot release lifetime");}
  {Fixture x;x.Put(x.pair.body[id].carrierRecords+0x248,std::uintptr_t(0x1230));C(!x.Retired(),"carrier resource still retained");}
  {Fixture x;x.Put(x.pair.body[id].carrierNode+0x10,std::uintptr_t(0xdead0));C(!x.Retired(),"unreadable carrier end pending bitmap");}
  {Fixture x;const auto base=std::uintptr_t(0x100000+id*0x100000);x.Put(base+0x9004,std::uint32_t(1));C(!x.Retired(),"carrier end scheduled but not retired");}
  {Fixture x;const auto base=std::uintptr_t(0x100000+id*0x100000);x.Put(base+0xa000,std::uint32_t(1u<<22));C(!x.Retired(),"PS carrier bit remains");}
  {Fixture x;x.Put(x.pair.body[id].recipientRecords+0x28,x.pair.body[1-id].carrierRecords);C(!x.Retired(),"reciprocal raw binding remains");}
  {Fixture x;x.Put(x.pair.body[id].callbackSlab+0x60+0x48,x.pair.body[1-id].carrier);C(!x.Retired(),"opposite participant callback remains");}
  {Fixture x;x.unreadable=x.pair.body[id].recipientRecords+0x10;C(!x.Retired(),"lost native current record");}
 }
 C(!cp::Retired(ok,0,ok.pair),"zero image cannot qualify retirement");
 std::cout<<n<<" checks; "<<f<<" failures\n";return f?1:0;
}
