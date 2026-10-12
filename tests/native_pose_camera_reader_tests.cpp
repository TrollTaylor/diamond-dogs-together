#include "native_pose_camera_reader.hpp"
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
int main(){unsigned passed=0,failed=0;auto Check=[&](bool ok,const char* why)noexcept{if(ok)++passed;else{++failed;std::printf("FAIL %s\n",why);}};
 {Memory m;c::Witness w{};Check(c::Pair(m,m.image,m.player,m.camera,w)&&w.realLocal==0&&w.selected==1&&w.target==0,"typed current camera witness");}
 for(unsigned test=0;test<7;++test){Memory m;c::Witness w{};
  switch(test){case 0:m.Put(m.player,m.image+0x2295491);break;case 1:m.Put(m.camera,m.image+0x23b8251);break;case 2:m.Put(m.camera-0x20,m.image+0x23b8219);break;case 3:m.Put(m.camera+0x348,m.player+0x10);break;case 4:m.Put(m.player+0xb8,m.camera+0x100);break;case 5:m.unreadable=m.camera+0x380;break;case 6:m.flip=m.player+0x218;break;}
  Check(!c::Pair(m,m.image,m.player,m.camera,w),"wrong/replaced/stale/unreadable camera witness refuses");
 }
 {Memory m;c::Witness w{};Check(!c::Pair(m,m.image,UINTPTR_MAX-3,m.camera,w)&&m.calls==0,"player range overflow refuses before reads");}
 {Memory m;c::Witness w{};Check(!c::Pair(m,m.image,m.player,UINTPTR_MAX-3,w)&&m.calls==0,"camera range overflow refuses before reads");}
 {Memory m;m.Put(m.player+0x218,std::uint32_t(0));c::Witness w{};Check(c::Pair(m,m.image,m.player,m.camera,w)&&w.selected==0,"diagnostic witness does not fabricate body1 or grant suppression");}
 {Memory m;m.Put(m.camera+0x380,std::uint32_t(1));c::Witness w{};Check(c::Pair(m,m.image,m.player,m.camera,w)&&w.target==1,"raw camera target is faithfully reported");}
 {const std::uintptr_t image=0x140000000ull;const auto at=image+contract::indexed_return;
  const auto zero=c::Incoming(at,image,0),one=c::Incoming(at,image,1),raw=c::Incoming(at,image,37);
  Check(zero.known&&zero.body==0,"exact indexed caller retains actual body0 despite selected1");
  Check(one.known&&one.body==1,"exact indexed caller retains actual body1");
  Check(raw.known&&raw.body==37,"census never coerces incoming ID into a supported body");
  const auto adjacent=c::Incoming(at+1,image,1);
  Check(!adjacent.known&&adjacent.body==UINT32_MAX,"adjacent caller does not grant incoming argument provenance");
  Check(!c::Incoming(image-1,image,0).known,"caller underflow refuses");
  Check(!c::Incoming(image+0xa01d000,image,0).known,"caller beyond image refuses");
  Check(!c::Incoming(at,0,0).known,"missing image refuses");
  Check(!c::Incoming(UINTPTR_MAX-1,UINTPTR_MAX-8,0).known,"image overflow refuses before subtraction");
 }

 {SenderMemory m;c::SenderWitness w{};const char* why=nullptr;Check(c::SelectedReplica(m,Slice(),m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,w,&why)&&w.primary==m.driverPrimary&&w.body==m.block,
   "exact actual sender/current typed driver filters only qualified body1 global-view callback");
  Check(why&&std::strcmp(why,"camera_selected_replica")==0,"accepted filter reports exact bounded typed stage");}
 for(unsigned test=0;test<25;++test){SenderMemory m;c::SenderWitness w{};auto s=Slice();std::uintptr_t sender=m.driverPrimary,caller=m.image+contract::indexed_return;std::uint32_t incoming=1;
  switch(test){
   case 0:sender+=8;break;case 1:sender=0;break;case 2:m.Put(m.driverPrimary,m.image+0x23bc141);break;
   case 3:m.Put(m.driverPrimary+0x20,m.image+0x23bc171);break;case 4:m.Put(m.driver,m.image+0x23bc1b1);break;
   case 5:m.Put(m.driverPrimary+0x1a0,m.blocks);break;case 6:m.Put(m.block+0x138,m.player+8);break;
   case 7:m.Put(m.slots+8,m.blocks);break;case 8:m.Put(m.pools+1,std::uint8_t(16));break;
   case 9:m.Put(m.block+0xc,std::uint32_t(0));break;case 10:m.Put(m.ids+2,std::uint16_t(0));break;
   case 11:m.Put(m.player+0x204,std::uint32_t(0x101));break;case 12:m.Put(m.player+0xa8,std::uintptr_t(0x1234));break;
   case 13:m.Put(m.image+0x2c56430,m.image+0x1155b60);break;case 14:m.Put(m.image+0x2c56438,std::int32_t(1));break;
   case 15:m.Put(m.image+0x23bc1b0,m.image+0x1156281);break;case 16:m.Put(m.image+0x23bc148,m.image+0x11565d1);break;
   case 17:m.Put(m.image+0x23b84c0,m.image+0x111cba1);break;case 18:m.Put(m.camera+0x20c,std::uint32_t(1));break;
   case 19:m.Put(m.camera+0x380,std::uint32_t(1));break;case 20:m.Put(m.camera+0x384,std::uint32_t(1));break;
   case 21:m.Put(m.player+0x214,std::uint32_t(1));break;case 22:m.Put(m.player+0x218,std::uint32_t(0));break;
   case 23:incoming=0;break;case 24:caller++;break;
  }
  Check(!c::SelectedReplica(m,s,m.image,caller,incoming,m.player,m.block,m.camera,sender,w),"wrong actual sender/owned graph/phase row/body/camera refuses before original");
 }
 {SenderMemory m;c::SenderWitness w{};m.Put(m.image+0x2c5643c,std::uint32_t(0xffffffff));Check(c::SelectedReplica(m,Slice(),m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,w),"undefined phase table padding is ignored, not a signed64 adjustment");}
 for(unsigned test=0;test<16;++test){auto s=Slice();switch(test){
  case 0:s.owned=false;break;case 1:s.job=false;break;case 2:s.worker=false;break;case 3:s.birthCurrent=false;break;
  case 4:s.modelReady=false;break;case 5:s.selectedOwned=false;break;case 6:s.sameLease=false;break;case 7:s.fault=true;break;
  case 8:s.selected=0;break;case 9:s.generation=0;break;case 10:s.epoch=0;break;case 11:s.thread=0;break;
  case 12:s.ownerThread=6;break;case 13:s.sequence=8;break;case 14:s.contextSequence=9;break;case 15:s.birthSerial=0;break;}
  SenderMemory m;c::SenderWitness w{};Check(!c::SelectedReplica(m,s,m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,w)&&m.calls==0,"missing/changed actual writer-job-birth lease refuses before reads");
 }
 for(auto field:{std::uintptr_t(0x50000),std::uintptr_t(0x501a0),std::uintptr_t(0x140000000ull+0x2c56430),std::uintptr_t(0x140000000ull+0x2c56438)}){
  SenderMemory m;c::SenderWitness w{};m.flipSecond=field;Check(!c::SelectedReplica(m,Slice(),m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,w),"sender/owner/table rebinding between actual samples refuses");
 }
 {SenderMemory m;c::SenderWitness w{};m.unreadable=m.driverPrimary+0x1a0;Check(!c::SelectedReplica(m,Slice(),m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,w),"unreadable current sender owner refuses");}
 {constexpr std::uintptr_t at=0x70000,wrapper=0x140abcde0ull;unsigned char bytes[c::shimSize]{};c::Shim(wrapper,bytes);
  const unsigned char prefix[]={0x49,0x89,0xe8,0xff,0x25,0,0,0,0};std::uintptr_t target=0;std::memcpy(&target,bytes+9,8);
  Check(std::memcmp(bytes,prefix,sizeof(prefix))==0&&target==wrapper,"17-byte entry preserves RBP and stack, captures sender in volatile R8 then tail-jumps");
  Memory m;m.cells[at]=std::vector<unsigned char>(bytes,bytes+c::shimSize);Check(c::ShimMatches(m,at,wrapper),"exact owned sender entry matches expected wrapper target");
  for(unsigned i=0;i<c::shimSize;++i){m.cells[at][i]^=1;Check(!c::ShimMatches(m,at,wrapper),"any sender entry opcode or module wrapper-target replacement refuses");m.cells[at][i]^=1;}
  m.unreadable=at;Check(!c::ShimMatches(m,at,wrapper),"unreadable sender entry refuses");
  Check(!c::ShimMatches(m,0,wrapper)&&!c::ShimMatches(m,at,0)&&!c::ShimMatches(m,UINTPTR_MAX-4,wrapper),"missing/overflow entry or missing wrapper refuses");
 }
 for(auto consumer:{std::uint32_t(0),std::uint32_t(1),std::uint32_t(3),std::uint32_t(4),UINT32_MAX}){SenderMemory m;c::SenderWitness w{};auto s=Slice();s.consumer=consumer;
  Check(!c::SelectedReplica(m,s,m.image,m.image+contract::indexed_return,1,m.player,m.block,m.camera,m.driverPrimary,w)&&m.calls==0,"only actual observed drain1 consumer2 may filter");}
 {constexpr std::uintptr_t stub=0x70000,foreign=0x71000;
  Check(c::OwnedEntry(0x111cba0,stub,stub,true),"actual direct and relay camera endpoint both accept only exact RX owned entry");
  Check(!c::OwnedEntry(0x111cba0,stub,stub,false),"direct E9 endpoint cannot bypass altered/unreadable/wrong-protection shim gate");
  Check(!c::OwnedEntry(0x111cba0,foreign,stub,true),"relay endpoint cannot substitute foreign wrapper even with live owned stub");
  Check(!c::OwnedEntry(0x111cba0,0,0,true),"missing camera endpoint refuses");
  Check(c::OwnedEntry(0xff5510,foreign,0,false),"existing other participant endpoint guard is unchanged");
 }
 Code code;std::uintptr_t mismatch=0;Check(c::CodeGate(code,code.image,0xa01d000,mismatch),"complete three-part camera frame and typed initializer gate");
 for(const auto& s:contract::sites){code.corrupt=s.rva+s.size-1;Check(!c::CodeGate(code,code.image,0xa01d000,mismatch)&&mismatch==s.rva,"late chained camera byte replacement refuses");}code.corrupt=0;
 code.wrongSlot=true;Check(!c::CodeGate(code,code.image,0xa01d000,mismatch),"actual camera frame/init slot change refuses");
 namespace px=mgscoop::native_pose_exclusion;px::Gate gate;px::WriteTicket writer;px::ReadTicket camera;
 const px::Admission a{true,true,true,true,true,true,3,4,5};
 Check(gate.TryWrite(a,writer)==px::Status::entered,"own body1 writer starts");
 Check(gate.TryRead(6,camera)==px::Status::busy&&!camera.Armed(),"foreign camera reader cannot sample P218 shadow while writer owns");
 Check(gate.TryRead(5,camera)==px::Status::nested_writer&&!camera.Armed(),"owned camera call cannot bypass exclusion with fabricated reader");
 Check(gate.EndWrite(5,3,4,true,true,writer),"actual complete writer cleanup releases");
 Check(gate.TryRead(6,camera)==px::Status::entered&&gate.EndRead(6,camera),"foreign camera reader enters only after P218 restoration and cleanup");
 Check(gate.TryRead(6,camera)==px::Status::entered,"camera reader begins stock frame");
 px::WriteTicket pending;Check(gate.TryWrite(a,pending)==px::Status::busy&&!pending.Armed(),"body1 writer cannot begin during stock camera frame");
 Check(gate.EndRead(6,camera),"stock camera reader cleanup balanced");
 std::printf("%u passed; %u failed\n",passed,failed);return failed?1:0;
}
