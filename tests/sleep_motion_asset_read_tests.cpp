// Detached synthetic pointer/ownership fixtures. No game data, Win32 or calls.
#include "sleep_motion_asset_read.hpp"
#include <map>
#include <vector>
#include <iostream>
#include <cstdlib>
namespace {
namespace ar=mgscoop::sleep_motion_asset_read;
using Addr=std::uintptr_t;
unsigned checks=0;
void Check(bool ok,const char* name){++checks;if(!ok){std::cerr<<"FAIL "<<name<<'\n';std::exit(1);}}
struct Memory {
 std::map<Addr,std::vector<unsigned char>> regions;
 void Bytes(Addr at,const void* source,std::size_t count){auto& b=regions[at];const auto* p=static_cast<const unsigned char*>(source);b.assign(p,p+count);}
 template<class T> void Put(Addr at,T value){Bytes(at,&value,sizeof(value));}
 bool operator()(Addr at,void* out,std::size_t count) noexcept {
  auto i=regions.upper_bound(at);if(i==regions.begin())return false;--i;
  if(at<i->first||at-i->first>i->second.size()||count>i->second.size()-(at-i->first))return false;
  std::memcpy(out,i->second.data()+(at-i->first),count);return true;
 }
};
struct Fixture {
 static constexpr Addr image=0x140000000,controller=0x50000,owner=0x51000,engine=0x52000,data=0x53000;
 static constexpr Addr player=0x54000,node=0x55000,child=0x55200,array=0x60000,bank=0x90000,blob=0x1000000;
 Memory memory; mgscoop::sleep_graph_read::Graph graph{};const ar::Pin& pin=ar::pins[1];bool patched=true;
 Addr source=array+std::uintptr_t(pin.index)*0x28;
 unsigned hashCalls=0;bool hashChanged=false;
 explicit Fixture(bool candidate=true):patched(candidate){
  graph.controller=controller;graph.currentNode=true;graph.stateHash=pin.graph;
  memory.Put(controller+0x18,owner);memory.Put(owner+0x10,engine);memory.Put(engine+0x10,data);
  memory.Put(controller+0xd0,std::uint32_t{0});memory.Put(data+0x4e,std::uint16_t{0});memory.Put(data+0xf8,player);
  memory.Put(player+0x48,std::uint32_t{0});memory.Put(player,node);
  memory.Put(node+0xc,std::uint16_t{1});memory.Put(node+4,std::uint32_t{0x100});memory.Put(node+0x100,1.f);
  memory.Put(node+0x30,std::uint32_t{0x200});memory.Put(child+0x80,source);
  memory.Put(source,pin.motion);memory.Put(source+8,bank);memory.Put(source+0x20,std::uint16_t{3});
  memory.Put(source+0x22,std::uint16_t{380});memory.Put(source+0x24,pin.index);
  memory.Put(bank,image+0x24c2890);memory.Put(bank+0x40,std::uint8_t{2});memory.Put(bank+0x84,std::uint32_t{1285});
  memory.Put(bank+0x88,array);memory.Put(bank+0x98,blob);memory.Put(bank+0x60,blob);
  memory.Put(blob,std::uint32_t{0x0c012b72});memory.Put(blob+4,std::uint32_t{1285});memory.Put(blob+0x12,std::uint16_t{0x1000});
  SetPayload();
 }
 void SetPayload(std::uint32_t frames=380){
  const auto* header=patched?pin.patchedHeader:pin.originalHeader;
  memory.Bytes(blob+0x20+std::uintptr_t(pin.index)*0x20,header,32);
  std::uint32_t offset=0;std::memcpy(&offset,header+8,4);
  std::vector<unsigned char> fake(patched?pin.patchedSize:pin.originalSize,0xaa);std::memcpy(fake.data(),&frames,4);
  memory.Bytes(blob+offset,fake.data(),fake.size());
 }
 // Recording hasher tests dispatch/ownership/pin comparison only. Its result
 // is deliberately synthetic; runtime must use real BCrypt SHA256.
 bool Hash(const void* bytes,std::size_t size,char (&out)[65]) noexcept {
  ++hashCalls;if(size!=(patched?pin.patchedSize:pin.originalSize))return false;
  const auto* b=static_cast<const unsigned char*>(bytes);for(std::size_t i=4;i<size;++i)if(b[i]!=0xaa)return false;
  std::memcpy(out,hashChanged?pin.originalSha:(patched?pin.patchedSha:pin.originalSha),65);return true;
 }
 bool Read(ar::Asset& out){auto hasher=[&](const void* b,std::size_t n,char (&s)[65]) noexcept {return Hash(b,n,s);};return ar::FromGraph(memory,hasher,image,graph,out);}
};
}
int main(){
 ar::Asset asset{};
 Fixture candidate;Check(candidate.Read(asset)&&asset.origin==ar::Origin::mgo_reconstruction&&asset.size==5968,"candidate_selected_header_hash");
 Check(candidate.hashCalls==1&&asset.source==candidate.source,"one_bounded_selected_source");
 Fixture original(false);Check(original.Read(asset)&&asset.origin==ar::Origin::tpp&&asset.size==5872,"original_selected_header_hash");
 Fixture wrongVtable;wrongVtable.memory.Put(Fixture::bank,Fixture::image+0x24c28b8);Check(!wrongVtable.Read(asset),"interior_vtable_slot_refused");
 Fixture wrongAlias;wrongAlias.memory.Put(Fixture::bank+0x88,Fixture::array+8);Check(!wrongAlias.Read(asset),"source_array_ownership_refused");
 Fixture wrongIndex;wrongIndex.memory.Put(wrongIndex.source+0x24,std::uint32_t{14});Check(!wrongIndex.Read(asset),"wrong_entry_index_refused");
 Fixture wrongCount;wrongCount.memory.Put(Fixture::bank+0x84,std::uint32_t{1284});Check(!wrongCount.Read(asset),"wrong_bank_count_refused");
 Fixture wrongKind;wrongKind.memory.Put(wrongKind.source+0x20,std::uint16_t{2});Check(!wrongKind.Read(asset),"wrong_source_kind_refused");
 Fixture unknownHeader;unsigned char changed[32]{};std::memcpy(changed,unknownHeader.pin.patchedHeader,32);changed[12]^=1;
 unknownHeader.memory.Bytes(Fixture::blob+0x20+std::uintptr_t(unknownHeader.pin.index)*0x20,changed,32);Check(!unknownHeader.Read(asset),"changed_header_refused");
 Fixture changedHash;changedHash.hashChanged=true;Check(!changedHash.Read(asset),"candidate_needs_candidate_payload_hash");
 Fixture changedFrames;changedFrames.SetPayload(381);Check(!changedFrames.Read(asset),"source_frame_identity_refused");
 Fixture zeroWeight;zeroWeight.memory.Put(Fixture::node+0x100,0.f);Check(zeroWeight.Read(asset)&&asset.origin==ar::Origin::none&&zeroWeight.hashCalls==0,"unweighted_source_not_selected");
 Fixture tooMany;tooMany.memory.Put(Fixture::node+0xc,std::uint16_t{65});Check(!tooMany.Read(asset)&&tooMany.hashCalls==0,"child_count_ceiling_before_read");
 Fixture wrongSlot;wrongSlot.memory.Put(Fixture::controller+0xd0,std::uint32_t{1});Check(!wrongSlot.Read(asset),"inclusive_slot_bound");
 Fixture wrongBuffer;wrongBuffer.memory.Put(Fixture::player+0x48,std::uint32_t{2});Check(!wrongBuffer.Read(asset),"double_buffer_index_only");
 Fixture nonRest;nonRest.graph.stateHash=0;Check(nonRest.Read(asset)&&asset.origin==ar::Origin::none&&nonRest.hashCalls==0,"non_rest_graph_not_selection");
 Fixture absent;absent.graph.currentNode=false;Check(absent.Read(asset)&&asset.origin==ar::Origin::none&&absent.hashCalls==0,"null_current_node_not_selection");
 std::cout<<"sleep_motion_asset_read_tests: "<<checks<<" synthetic checks passed; no game operations\n";
}
