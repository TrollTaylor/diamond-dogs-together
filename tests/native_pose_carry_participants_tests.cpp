#include "native_pose_carry_participants.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <iostream>
namespace cp=mgscoop::native_pose_carry_participants;
struct Fixture {
 const std::uintptr_t image=0x140000000ull,player=0x10000,data=0x20000,state=0x30000,ids=0x40000,pools=0x41000,parts=0x42000,table=0x43000,blocks=0x50000;
 std::map<std::uintptr_t,std::vector<unsigned char>> cells;std::uintptr_t unreadable=0;unsigned reads=0;bool callbackChanges=false,rebind=false;
 template<class T>void Put(std::uintptr_t p,T value){auto& x=cells[p];x.resize(sizeof(value));std::memcpy(x.data(),&value,sizeof(value));}
 std::uintptr_t Block(unsigned id)const{return blocks+id*0x6a8;}
 std::uintptr_t PS(unsigned id)const{return 0x100000+id*0x100000;}
 std::uintptr_t CS(unsigned id)const{return PS(id)+0x1000;}
 std::uintptr_t Entry(unsigned id)const{return PS(id)+0x2000;}
 std::uintptr_t Slab(unsigned id)const{return PS(id)+0x30d0;}
 std::uintptr_t Carrier(unsigned id)const{return PS(id)+0x4000;}
 std::uintptr_t Recipient(unsigned id)const{return PS(id)+0x9000;}
 bool operator()(std::uintptr_t p,void* out,std::size_t n)noexcept {
  if(p==image+0x2a82c30&&++reads==2){
   if(callbackChanges){Put(Slab(0)+0x40,image+0x123456);Put(Slab(0)+0x48,std::uintptr_t(0x88888));Put(Slab(0)+0x50,std::uint64_t(0xabc));Put(Slab(0)+0x58,std::uint32_t(0));}
   if(rebind)Put(Block(0)+0x10,CS(1));
  }
  if(p==unreadable)return false;const auto i=cells.find(p);if(i==cells.end()||i->second.size()!=n)return false;std::memcpy(out,i->second.data(),n);return true;
 }
 void Node(std::uintptr_t block,unsigned id,unsigned row,std::uintptr_t primary,std::uintptr_t node){
  Put(block+0x190+row*8,node);Put(block+0x3b8+row*8,primary+0x30);Put(node,image+0x2306628);Put(node+0x20,std::uint32_t(1));Put(node+0x24,std::uint32_t(id));Put(node+0x2c,std::uint32_t(row));Put(node+0x38,primary+0x30);Put(primary+0x38,node);
 }
 Fixture(){
  Put(image+0x2a82c30,player);Put(player,image+0x2295490);Put(player+0x204,std::uint32_t(1));Put(player+0xa8,std::uintptr_t(0));Put(player+0x208,std::uint32_t(2));Put(player+0x20c,std::uint32_t(2));Put(player+0x214,std::uint32_t(0));Put(player+0x60,data);Put(data,image+0x23ad970);Put(data+0x1e8,state);Put(state+0x40,std::uint32_t(2));Put(image+0x2c16920,image+0x2c01360);Put(player+0x140,image+0x2c01360);
  Put(player+0x70,ids);Put(player+0x78,pools);Put(player+0x90,parts);Put(player+0x378,blocks);Put(player+0x50,table);
  for(unsigned id=0;id<2;++id){const auto block=Block(id),ps=PS(id),cs=CS(id),entry=Entry(id),carrier=Carrier(id),recipient=Recipient(id),slab=Slab(id);
   Put(ids+id*2,std::uint16_t(id));Put(pools+id,std::uint8_t(id));Put(parts+id,std::uint8_t(3));Put(table+id*8,block);
   Put(block,image+0x2295210);Put(block+8,std::uint32_t(1));Put(block+0xc,std::uint32_t(id));Put(block+0x138,player);Put(block+0x78,ps);Put(block+0x10,cs);Put(block+0x140,image+0x2c01360);
   Put(ps,image+0x23b09f0);Put(ps+8,std::uint32_t(id));Put(ps+0x408,block);Put(ps+0x238,cs);
   Put(cs,image+0x22e6070);Put(cs+8,std::uint32_t(1));Put(cs+0xc,std::uint32_t(id));Put(cs+0x10,entry);
   Put(entry+0x78,ps+0x3000);Put(entry+0x88,slab);Put(entry+0xb9,std::uint8_t(1));Put(entry+0xba,std::uint8_t(2));
   for(unsigned n=0;n<2;++n){const auto at=slab+n*0x60;Put(at,image+0x22f6380);Put(at+8,std::int32_t(1));Put(at+0x40,std::uintptr_t(0));Put(at+0x48,std::uintptr_t(0));Put(at+0x50,std::uint64_t(0));Put(at+0x58,std::uint32_t(0));}
   Put(block+0xb8,carrier);Put(carrier,image+0x23b9ac0);Put(carrier+0x10,image+0x23b9b18);Put(carrier+0x30,image+0x23b9b50);Put(carrier+8,block);Put(carrier+0x68,ps+0x6000);Node(block,id,0x32,carrier,ps+0x5000);
   Put(block+0x438,recipient+0x30);Put(recipient,image+0x23b8d78);Put(recipient+0x10,image+0x23b8da0);Put(recipient+0x30,image+0x23b8dd0);Put(recipient+0x48,image+0x23b8e10);Put(recipient+8,block);Put(recipient+0x88,ps+0xb000);Node(block,id,0x10,recipient,ps+0xa000);
   const auto desc=ps+0xc000;Put(recipient+0x58,desc);Put(desc,image+0x645940);Put(desc+0x10,ps+0xb000);Put(desc+0x18,std::uint64_t(0x60));Put(desc+0x20,std::uint64_t(1));
  }
 }
 bool Pair(cp::Pair& out){reads=0;return cp::ReadPair(*this,image,player,out);}
};
int main(){unsigned n=0,f=0;auto C=[&](bool v,const char* why){++n;if(!v){++f;std::cerr<<why<<"\n";}};cp::Pair pair{};Fixture good;
 C(good.Pair(pair)&&pair.body[0].id==0&&pair.body[0].block==good.Block(0)&&pair.body[0].state==good.PS(0)&&pair.body[0].freeCallbacks==2,"full actual body0 owned graph through exact owner/model/node/descriptor");
 C(pair.body[1].id==1&&pair.body[1].block==good.Block(1)&&pair.body[1].cs==good.CS(1)&&pair.body[1].callbackSlab==good.Slab(1),"full distinct recipient graph");
 {Fixture x;x.Put(x.Block(0)+0x138,x.player+8);C(!x.Pair(pair),"body0 block owner replaced");}
 {Fixture x;x.Put(x.PS(0)+0x408,x.Block(1));C(!x.Pair(pair),"body0 PS owner replaced");}
 {Fixture x;x.Put(x.PS(0)+0x138,std::uintptr_t(0xdead));C(x.Pair(pair),"PS138 padding is deliberately not fabricated as Player owner");}
 {Fixture x;x.Put(x.Block(0)+0x10,x.CS(1));C(!x.Pair(pair),"body0 current CS rebind");}
 {Fixture x;x.Put(x.CS(0)+0xc,std::uint32_t(1));C(!x.Pair(pair),"body0 CS base wrong");}
 {Fixture x;x.Put(x.Entry(0)+0x78,x.PS(1)+0x3000);x.Put(x.Entry(0)+0x88,x.Slab(1));C(!x.Pair(pair),"alias callback allocations cannot retain two participants");}
 {Fixture x;x.Put(x.pools,std::uint8_t(1));C(!x.Pair(pair),"body0 pool alias");}
 {Fixture x;x.Put(x.Carrier(0)+0x38,x.PS(1)+0x5000);C(!x.Pair(pair),"body0 carrier node alias");}
 {Fixture x;x.Put(x.PS(0)+0x5000+0x2c,std::uint32_t(0x33));C(!x.Pair(pair),"wrong carry row33 cannot replace corrected row32");}
 {Fixture x;x.Put(x.PS(0)+0xc000+0x20,std::uint64_t(2));C(!x.Pair(pair),"wrong recipient allocation count");}
 {Fixture x;x.Put(x.player+0x204,std::uint32_t(0x101));C(!x.Pair(pair),"online synchronizer scope excluded");}
 {Fixture x;x.unreadable=x.Slab(0)+8;C(!x.Pair(pair),"unreadable body0 callback baseline");}
 {Fixture x;x.Put(x.Slab(0)+8,std::int32_t(0));C(!x.Pair(pair),"released callback baseline");}
 {Fixture x;x.Put(x.Slab(0)+0x48,std::uintptr_t(0x123));C(!x.Pair(pair),"null function with stale callback owner refuses");}
 {Fixture x;x.callbackChanges=true;C(!x.Pair(pair),"callback metadata cannot change between coherent reads");}
 {Fixture x;x.rebind=true;C(!x.Pair(pair),"body0 rebind between reads refuses");}
 {Fixture x;x.Put(x.Slab(0)+0x40,x.image+0x112fc50);x.Put(x.Slab(0)+0x48,x.Carrier(0));x.Put(x.Slab(0)+0x50,std::uint64_t(0x6504d35367f0ull));C(x.Pair(pair)&&pair.body[0].freeCallbacks==1,"qualified native callback registration updates capacity");}
 {Fixture x;x.Put(x.Slab(0)+0x40,x.image+0x112fc50);x.Put(x.Slab(0)+0x48,x.Carrier(1));x.Put(x.Slab(0)+0x50,std::uint64_t(0x6504d35367f0ull));C(!x.Pair(pair),"opposite participant callback owner cannot alias");}
 {Fixture x;x.Put(x.Slab(0)+0x40,x.image+0x112fc50);x.Put(x.Slab(0)+0x48,x.Carrier(0));x.Put(x.Slab(0)+0x50,std::uint64_t(0xbad));C(!x.Pair(pair),"unknown owned carry callback event refuses");}
 {Fixture x;cp::Pair before{},after{};C(x.Pair(before),"pre-registration identity read");x.Put(x.Slab(0)+0x40,x.image+0x123456);x.Put(x.Slab(0)+0x48,std::uintptr_t(0x88888));x.Put(x.Slab(0)+0x50,std::uint64_t(0xabc));C(x.Pair(after)&&cp::Same(before,after)&&!cp::CoherentCallbacks(before.body[0],after.body[0]),"stable lifetime identity permits native unrelated callbacks across separate frames");}
 C(!cp::ReadPair(good,0,good.player,pair),"zero image refuses full readgraph");
 std::cout<<n<<" checks; "<<f<<" failures\n";return f?1:0;}
