// Detached synthetic count-two selection fixtures. No native calls or real
// game payload. Runtime uses cryptographic hashing; this fake hasher tests
// bounded dispatch and exact pin comparisons only.
#include "sleep_motion_asset_read.hpp"
#include <map>
#include <vector>
#include <iostream>
#include <cstdlib>
namespace {
namespace ar=mgscoop::sleep_motion_asset_read;
namespace ub=mgscoop::sleep_unique_bind;
using Addr=std::uintptr_t;
unsigned checks=0;
void Check(bool ok,const char* why){++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Memory {
 std::map<Addr,std::vector<unsigned char>> regions;
 void Bytes(Addr at,const void* source,std::size_t n){const auto* b=static_cast<const unsigned char*>(source);regions[at].assign(b,b+n);}
 template<class T> void Put(Addr at,T value){Bytes(at,&value,sizeof(value));}
 bool operator()(Addr at,void* out,std::size_t n) noexcept {
  auto i=regions.upper_bound(at);if(i==regions.begin())return false;--i;
  if(at<i->first||at-i->first>i->second.size()||n>i->second.size()-(at-i->first))return false;
  std::memcpy(out,i->second.data()+(at-i->first),n);return true;
 }
};
struct Fixture {
 static constexpr Addr image=0x140000000,controller=0x50000,owner=0x51000,engine=0x52000,data=0x53000;
 static constexpr Addr player=0x54000,node=0x55000,child=0x55200,sources=0x60000,bank=0x90000,blob=0x1000000;
 Memory memory; mgscoop::sleep_graph_read::Graph graph{};
 unsigned selected=0,hashCalls=0;bool badWholeHash=false,badMainHash=false;
 std::vector<unsigned char> expected;
 explicit Fixture(unsigned index=0):selected(index),expected(ub::kUniqueBytes,0xaa){
  const ar::Pin& pin=ar::pins[index?0:1];
  graph.controller=controller;graph.currentNode=true;graph.stateHash=pin.graph;
  memory.Put(controller+0x18,owner);memory.Put(owner+0x10,engine);memory.Put(engine+0x10,data);
  memory.Put(controller+0xd0,std::uint32_t{0});memory.Put(data+0x4e,std::uint16_t{0});memory.Put(data+0xf8,player);
  memory.Put(player+0x48,std::uint32_t{0});memory.Put(player,node);
  memory.Put(node+0xc,std::uint16_t{1});memory.Put(node+4,std::uint32_t{0x100});memory.Put(node+0x100,1.f);
  memory.Put(node+0x30,std::uint32_t{0x200});memory.Put(child+0x80,sources+index*0x28);
  memory.Put(bank,image+0x24c2890);memory.Put(bank+0x40,std::uint8_t{2});memory.Put(bank+0x84,std::uint32_t{2});
  memory.Put(bank+0x88,sources);memory.Put(bank+0x98,blob);memory.Put(bank+0x60,blob);memory.Put(bank+0x30,std::uint64_t{123});
  std::memcpy(expected.data(),ub::kUniqueFileHeader,32);
  for(unsigned i=0;i<2;++i){
   const ar::Pin& entry=ar::pins[i?0:1];const Addr source=sources+i*0x28;
   memory.Put(source,entry.motion);memory.Put(source+8,bank);memory.Put(source+0x20,std::uint16_t{3});
   const std::uint16_t frames=static_cast<std::uint16_t>(i?420:380);
   memory.Put(source+0x22,frames);memory.Put(source+0x24,std::uint32_t{i});
   std::memcpy(expected.data()+0x20+i*32,ub::kUniqueEntryHeader[i],32);
   std::uint32_t offset=0;std::memcpy(&offset,ub::kUniqueEntryHeader[i]+8,4);
   const std::uint32_t duration=frames;std::memcpy(expected.data()+offset,&duration,4);
  }
  memory.Bytes(blob,expected.data(),expected.size());
 }
 bool Hash(const void* input,std::size_t n,char (&out)[65]) noexcept {
  ++hashCalls;const auto* b=static_cast<const unsigned char*>(input);
  if(n==expected.size()){
   if(badWholeHash||std::memcmp(b,expected.data(),n))return false;
   std::memcpy(out,ub::kUniqueSha,65);return true;
  }
  const ar::Pin& pin=ar::pins[selected?0:1];
  if(n!=pin.patchedSize||badMainHash)return false;
  for(std::size_t i=4;i<n;++i)if(b[i]!=0xaa)return false;
  std::memcpy(out,pin.patchedSha,65);return true;
 }
 bool Read(ar::Asset& a,bool profile=true,const char** why=nullptr){
  auto h=[&](const void* b,std::size_t n,char (&out)[65])noexcept{return Hash(b,n,out);};
  return ar::FromGraph(memory,h,image,graph,a,why,profile);
 }
};
}
int main(){
 ar::Asset a{};const char* reason=nullptr;
 Fixture first;Check(first.Read(a)&&a.origin==ar::Origin::mgo_reconstruction&&a.index==0&&a.size==5968,"unique_retained_C318_selects_index0");
 Check(first.hashCalls==2&&a.motion==ar::pins[1].motion&&a.source==Fixture::sources,"whole_bank_and_actual_selected_main_both_checked");
 Fixture second(1);Check(second.Read(a)&&a.origin==ar::Origin::mgo_reconstruction&&a.index==1&&a.size==5568,"unique_retained_D765_selects_index1");
 Fixture publicProfile;Check(!publicProfile.Read(a,false)&&!publicProfile.hashCalls,"compact_bank_forbidden_without_exact_private_profile");
 Fixture wrongIndex;wrongIndex.memory.Put(Fixture::sources+0x24,std::uint32_t{13});Check(!wrongIndex.Read(a),"resident_index_not_reused_in_unique_bank");
 Fixture wrongPeer;wrongPeer.memory.Put(Fixture::sources+0x28+8,Fixture::bank+8);Check(!wrongPeer.Read(a),"both_compact_entries_require_native_bank_owner");
 Fixture wrongCount;wrongCount.memory.Put(Fixture::bank+0x84,std::uint32_t{3});Check(!wrongCount.Read(a),"only_count2_or_original1285");
 Fixture wrongArray;wrongArray.memory.Put(Fixture::bank+0x88,Fixture::sources+8);Check(!wrongArray.Read(a),"selected_source_must_belong_to_actual_array");
 Fixture wrongBlob;wrongBlob.memory.Put(Fixture::bank+0x60,Fixture::blob+16);Check(!wrongBlob.Read(a),"effective_loaded_blob_identity");
 Fixture whole;whole.badWholeHash=true;Check(!whole.Read(a,true,&reason)&&std::strcmp(reason,"asset_unique_bank_hash_changed")==0,"whole_compact_bank_required_before_main");
 Fixture main;main.badMainHash=true;Check(!main.Read(a),"selected_component_hash_required_after_whole_bank");
 Fixture changed;changed.memory.regions[Fixture::blob][1009]^=1;Check(!changed.Read(a),"actual_payload_change_refused");
 Fixture weight;weight.memory.Put(Fixture::node+0x100,0.f);Check(weight.Read(a)&&a.origin==ar::Origin::none&&!weight.hashCalls,"asset_load_does_not_prove_selection");
 Fixture unrelated;unrelated.graph.stateHash=0;Check(unrelated.Read(a)&&a.origin==ar::Origin::none&&!unrelated.hashCalls,"different_graph_not_claimed_as_reused_sleep");
 Check(!ar::Scope{}.privateUniqueRestProfile,"unique_profile_acceptance_default_false");
 std::cout<<"sleep_unique_asset_read_tests: "<<checks<<" synthetic checks passed; no game operations\n";
}
