#include "native_pose_vehicle_camera_foot.hpp"
#include "native_pose_exclusion.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
namespace c=mgscoop::native_pose_camera_reader;
namespace contract=mgscoop::native_pose_camera_reader_contract;
struct Memory {
 std::uintptr_t image=0x140000000ull,player=0x10000,camera=0x20000,unreadable=0,flip=0;
 unsigned calls=0;std::uintptr_t flipSecond=0;unsigned flipHits=0;std::map<std::uintptr_t,std::vector<unsigned char>> cells;
 template<class T>void Put(std::uintptr_t at,T value){auto& v=cells[at];v.resize(sizeof(value));std::memcpy(v.data(),&value,sizeof(value));}
 bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept {
  ++calls;if(at==unreadable)return false;
  auto i=cells.find(at);if(i==cells.end()||i->second.size()!=n)return false;
  std::memcpy(out,i->second.data(),n);if(flipSecond==at&&++flipHits>=2)static_cast<unsigned char*>(out)[0]^=1;if(flip==at&&calls>10)static_cast<unsigned char*>(out)[0]^=1;return true;
 }
 Memory(){Put(player,image+0x2295490);Put(player+0xb8,camera);Put(camera,image+0x23b8250);Put(camera-0x20,image+0x23b8218);Put(camera+0x348,player);Put(player+0x214,std::uint32_t(0));Put(player+0x218,std::uint32_t(1));Put(camera+0x20c,std::uint32_t(0));Put(camera+0x380,std::uint32_t(0));Put(camera+0x384,std::uint32_t(0));}
};
struct Code {
 std::uintptr_t image=0x140000000ull,corrupt=0;bool wrongSlot=false;
 bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept {
  const auto rva=at-image;
  for(const auto& s:contract::sites)if(rva==s.rva&&n==s.size){std::memcpy(out,s.bytes,n);if(corrupt>=rva&&corrupt-rva<n)static_cast<unsigned char*>(out)[corrupt-rva]^=1;return true;}
  for(const auto& s:contract::slots)if(rva==s.rva&&n==8){const auto v=image+s.target+(wrongSlot?1u:0u);std::memcpy(out,&v,8);return true;}return false;
 }
};
struct SenderMemory:Memory {
 std::uintptr_t blocks=0x30000,block=blocks+0x6a8,driverPrimary=0x50000,driver=driverPrimary+0x38,ids=0x60000,pools=0x61000,slots=0x62000;
 SenderMemory(){
  Put(player+0x204,std::uint32_t(1));Put(player+0xa8,std::uintptr_t(0));Put(player+0x208,std::uint32_t(2));Put(player+0x20c,std::uint32_t(2));
  Put(player+0x70,ids);Put(ids,std::uint16_t(0));Put(ids+2,std::uint16_t(1));Put(player+0x78,pools);Put(pools+1,std::uint8_t(1));
  Put(player+0x378,blocks);Put(player+0x50,slots);Put(slots+8,block);Put(block,image+0x2295210);Put(block+8,std::uint32_t(1));Put(block+0xc,std::uint32_t(1));Put(block+0x138,player);
  Put(block+0x70,driver);Put(driverPrimary,image+0x23bc140);Put(driverPrimary+0x20,image+0x23bc170);Put(driver,image+0x23bc1b0);Put(driverPrimary+0x1a0,block);
  Put(image+0x23bc1b0,image+0x1156280);Put(image+0x23bc148,image+0x11565d0);Put(image+0x23b84c0,image+0x111cba0);
  Put(image+0x2c56430,image+0x11559d0);Put(image+0x2c56438,std::int32_t(0));Put(image+0x2c5643c,std::uint32_t(0xdeadbeef));
 }
};
c::Slice Slice(){c::Slice s{};s.owned=s.job=s.worker=s.birthCurrent=s.modelReady=s.selectedOwned=s.sameLease=true;s.selected=1;s.consumer=2;s.generation=3;s.epoch=4;s.thread=s.ownerThread=5;s.sequence=s.contextSequence=7;s.birthSerial=9;return s;}
struct FootMemory:SenderMemory {
 std::uintptr_t vehiclePrimary=0x70000,node=0x71000,storage=0x80000,record=storage+0x30,bitmap=0x90000,map=0x91000;
 FootMemory(){
  Put(camera+0x20c,std::uint32_t(4));Put(block+0x128,vehiclePrimary);
  Put(vehiclePrimary,image+0x23cbea8);Put(vehiclePrimary+0x10,image+0x23cbee0);
  Put(vehiclePrimary+0x30,image+0x23cbf10);Put(vehiclePrimary+0x48,image+0x23cbf50);Put(vehiclePrimary+8,block);
  Put(vehiclePrimary+0x38,node);Put(node,image+0x2306628);Put(node+0x20,std::uint32_t(1));
  Put(node+0x24,std::uint32_t(1));Put(node+0x2c,std::uint32_t(0x23));Put(node+0x38,vehiclePrimary+0x30);
  Put(vehiclePrimary+0x50,storage);Put(vehiclePrimary+0x58,storage);Put(vehiclePrimary+0x60,std::uintptr_t(0x270));
  Put(vehiclePrimary+0x68,std::uintptr_t(0x270));Put(vehiclePrimary+0x70,std::uint16_t(16));
  Put(vehiclePrimary+0x72,std::uint16_t(1));Put(vehiclePrimary+0x74,std::uint32_t(0x1c0048));
  Put(vehiclePrimary+0x78,record);Put(storage,image+0x645940);Put(storage+0x10,record);
  Put(storage+0x18,std::uintptr_t(0x240));Put(storage+0x20,std::uintptr_t(1));
  Put(node+8,bitmap);Put(bitmap,std::uint32_t(1));Put(record+0x40,std::uintptr_t(0));Put(record+0x48,std::int32_t(0));
  Put(record+0x54,std::uint8_t(0));Put(record+0x55,std::uint8_t(0));
  Put(record+0x84,std::uint16_t(0xffff));Put(record+0x86,std::uint16_t(0xffff));
  Put(player+0x120,map);Put(map+8,std::uintptr_t(0));
 }
};
namespace foot=mgscoop::native_pose_vehicle_camera_foot;
foot::Scope FootScope(){foot::Scope s{};s.camera=Slice();s.enabled=s.exactNative=true;return s;}
int main(){unsigned passed=0,failed=0;auto Check=[&](bool ok,const char* why)noexcept{if(ok)++passed;else{++failed;std::printf("FAIL %s\n",why);}};
 auto Filter=[&](FootMemory& m,foot::Scope s)noexcept{auto sample=[&]()noexcept{return s;};foot::Receipt out{};
  return foot::Filter(m,sample,m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,out);};
 {FootMemory m;Check(Filter(m,FootScope()),"exact mode4 genuine selected1 foot callback may only be negatively filtered");}
 {FootMemory m;m.Put(m.record+0x40,m.image+0x645940);Check(Filter(m,FootScope()),"native idle callback sentinel is accepted, with phase0 and absent attachment");}
 {FootMemory m;auto s=FootScope();s.enabled=false;Check(!Filter(m,s)&&m.calls==0,"default OFF refuses before native reads");}
 for(unsigned n=0;n<12;++n){FootMemory m;auto s=FootScope();switch(n){
  case 0:s.exactNative=false;break;case 1:s.held=true;break;case 2:s.pending=true;break;case 3:s.retained=true;break;
  case 4:s.stopping=true;break;case 5:s.camera.fault=true;break;case 6:s.camera.birthCurrent=false;break;
  case 7:s.camera.modelReady=false;break;case 8:s.camera.sameLease=false;break;case 9:s.camera.worker=false;break;
  case 10:s.camera.consumer=1;break;case 11:s.camera.job=false;break;}
  Check(!Filter(m,s)&&m.calls==0,"held/pending/cleanup/retained/unknown writer or lifetime never becomes foot permission");
 }
 for(unsigned mode:{0u,1u,2u,3u,5u,UINT32_MAX}){FootMemory m;m.Put(m.camera+0x20c,mode);
  Check(!Filter(m,FootScope()),"only exact observed mode4 has the separate negative foot filter");}
 {FootMemory m;auto s=FootScope();auto sample=[&]()noexcept{return s;};foot::Receipt out{};
  Check(!foot::Filter(m,sample,m.image,m.image+contract::indexed_return,0,m.player,m.block,m.camera,m.driverPrimary,out),"body0 remains outside selected1 foot filter");
  Check(!foot::Filter(m,sample,m.image,m.image+contract::indexed_return+1,1,m.player,m.block,m.camera,m.driverPrimary,out),"adjacent/unqualified native caller remains refused");
  Check(!foot::Filter(m,sample,m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary+8,out),"foreign sender never uses mode4 filter");
 }
 for(unsigned n=0;n<33;++n){FootMemory m;switch(n){
  case 0:m.Put(m.block+8,std::uint32_t(2));break;case 1:m.Put(m.block+0xc,std::uint32_t(0));break;
  case 2:m.Put(m.vehiclePrimary,m.image+0x23cbea9);break;case 3:m.Put(m.vehiclePrimary+8,m.blocks);break;
  case 4:m.Put(m.vehiclePrimary+0x10,m.image+0x23cbee1);break;case 5:m.Put(m.vehiclePrimary+0x30,m.image+0x23cbf11);break;
  case 6:m.Put(m.vehiclePrimary+0x48,m.image+0x23cbf51);break;case 7:m.Put(m.node,m.image+0x2306629);break;
  case 8:m.Put(m.node+0x20,std::uint32_t(2));break;case 9:m.Put(m.node+0x24,std::uint32_t(0));break;
  case 10:m.Put(m.node+0x2c,std::uint32_t(0x24));break;case 11:m.Put(m.node+0x38,m.vehiclePrimary);break;
  case 12:m.Put(m.vehiclePrimary+0x50,m.storage+1);break;case 13:m.Put(m.vehiclePrimary+0x58,m.storage+8);break;
  case 14:m.Put(m.vehiclePrimary+0x60,std::uintptr_t(0x271));break;case 15:m.Put(m.vehiclePrimary+0x68,std::uintptr_t(0x200));break;
  case 16:m.Put(m.vehiclePrimary+0x70,std::uint16_t(8));break;case 17:m.Put(m.vehiclePrimary+0x72,std::uint16_t(0));break;
  case 18:m.Put(m.vehiclePrimary+0x74,std::uint32_t(0x1c0049));break;case 19:m.Put(m.vehiclePrimary+0x78,m.record+8);break;
  case 20:m.Put(m.storage,m.image+0x645941);break;case 21:m.Put(m.storage+0x10,m.record+8);break;
  case 22:m.Put(m.storage+0x18,std::uintptr_t(0x241));break;case 23:m.Put(m.storage+0x20,std::uintptr_t(2));break;
  case 24:m.Put(m.record+0x40,m.image+0x1251200);break;case 25:m.Put(m.record+0x48,std::int32_t(1));break;
  case 26:m.Put(m.record+0x54,std::uint8_t(3));break;case 27:m.Put(m.record+0x55,std::uint8_t(6));break;
  case 28:m.Put(m.record+0x84,std::uint16_t(25608));break;case 29:m.Put(m.record+0x86,std::uint16_t(25608));break;
  case 30:m.Put(m.map+8,std::uintptr_t(0x92000));break;case 31:m.Put(m.player+0x120,std::uintptr_t(0));break;
  case 32:m.Put(m.node+8,m.bitmap+1);break;
 }
  Check(!Filter(m,FootScope()),"unknown/replaced/active vehicle node and nonempty physical attachment are not foot");
 }
 for(unsigned n=0;n<10;++n){FootMemory m;auto s=FootScope();unsigned samples=0;
  auto sample=[&]()noexcept{auto out=s;if(++samples>=2){switch(n){
   case 0:out.held=true;break;case 1:out.pending=true;break;case 2:out.retained=true;break;
   case 3:out.camera.generation++;break;case 4:out.camera.epoch++;break;case 5:out.camera.thread++;break;
   case 6:out.camera.sequence+=2;break;case 7:out.camera.birthSerial++;break;case 8:out.camera.birthCurrent=false;break;
   case 9:out.enabled=false;break;
  }}return out;};foot::Receipt out{};
  Check(!foot::Filter(m,sample,m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,out),"late lease/lifetime/ownership/epoch change never filters an unqualified callback");
 }
 for(auto at:{std::uintptr_t(0x70000),std::uintptr_t(0x71000),std::uintptr_t(0x80000),std::uintptr_t(0x80070),std::uintptr_t(0x91008)}){
  FootMemory m;m.flipSecond=at;Check(!Filter(m,FootScope()),"rebinding between current foot snapshots refuses");
 }
 {FootMemory m;m.unreadable=m.vehiclePrimary+0x78;Check(!Filter(m,FootScope()),"unreadable vehicle foot holder cannot authorize filtering");}
 {FootMemory m;foot::Foot out{};Check(!foot::FootOnce(m,m.image,m.player,UINTPTR_MAX-3,out)&&m.calls==0,"overflow refuses without memory read");}
 // Keep the real native job accounted as executed. Only its redundant void
 // camera leaf is omitted; unknown callbacks would still use the existing refusal.
 {FootMemory m;unsigned nativeJobs=0,cameraCalls=0,filtered=0;for(unsigned frame=0;frame<100;++frame){
    ++nativeJobs;if(Filter(m,FootScope()))++filtered;else++cameraCalls;
   }Check(nativeJobs==100&&filtered==100&&cameraCalls==0,"mode4 filter preserves every actual job, so no fabricated execution or health exemption is needed");
  m.Put(m.camera+0x20c,std::uint32_t(0));c::SenderWitness sender{};
  Check(c::SelectedReplica(m,Slice(),m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,sender),"normal foot mode0 filtering resumes through the unchanged existing proof after exit");
  Check(!Filter(m,FootScope()),"mode0 exit never leaves sticky mode4 filter state");
 }
 std::printf("%u passed; %u failed\n",passed,failed);return failed?1:0;
}
