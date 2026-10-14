// Existing current asset fixture plus independently bound-bank policy checks.
#include "standing_selection_bank_read.hpp"
#include "sleep_standing_cycle_asset.hpp"
#include <map>
#include <vector>
#include <iostream>
#include <cstdlib>
namespace {
namespace ar=mgscoop::sleep_standing_cycle_asset;using Addr=std::uintptr_t;
unsigned checks=0;void Check(bool ok,const char* why){++checks;if(!ok){std::cerr<<"FAIL "<<why<<'\n';std::exit(1);}}
struct Memory {std::map<Addr,std::vector<unsigned char>> bytes;
 void Bytes(Addr at,const void* p,std::size_t n){const auto* b=static_cast<const unsigned char*>(p);bytes[at].assign(b,b+n);}
 template<class T>void Put(Addr at,T v){Bytes(at,&v,sizeof(v));}
 bool operator()(Addr at,void* out,std::size_t n)noexcept{auto i=bytes.upper_bound(at);if(i==bytes.begin())return false;--i;
  if(at<i->first||at-i->first>i->second.size()||n>i->second.size()-(at-i->first))return false;std::memcpy(out,i->second.data()+at-i->first,n);return true;}
};
struct Fixture {
 static constexpr Addr image=0x140000000,controller=0x50000,owner=0x51000,engine=0x52000,data=0x53000,player=0x54000,node=0x55000,child=0x55200,sources=0x60000,bank=0x90000,blob=0x1000000;
 Memory memory;mgscoop::sleep_graph_read::Graph graph{};std::vector<unsigned char> payload;unsigned selected=0,hashCalls=0;bool badWhole=false,badMain=false,badEnd=false;
 Fixture(unsigned index):payload(ar::kBytes,0xaa),selected(index){const auto& pin=ar::pins[index];
  for(const auto& g:ar::graphs)if(g.index==index){graph.stateHash=g.hash;break;}graph.currentNode=true;graph.controller=controller;
  memory.Put(controller+0x18,owner);memory.Put(owner+0x10,engine);memory.Put(engine+0x10,data);memory.Put(controller+0xd0,std::uint32_t{0});
  memory.Put(engine,image+0x24c1988);memory.Put(engine+0x1c,std::uint32_t{1});memory.Put(engine+0x98,Addr{0x58000});memory.Put(Addr{0x58018},bank);
  memory.Put(data+0x4e,std::uint16_t{0});memory.Put(data+0xf8,player);memory.Put(player+0x48,std::uint32_t{0});memory.Put(player,node);
  memory.Put(node+0xc,std::uint16_t{1});memory.Put(node+4,std::uint32_t{0x100});memory.Put(node+0x100,1.f);memory.Put(node+0x30,std::uint32_t{0x200});memory.Put(child+0x80,sources+index*0x28);
  memory.Put(bank,image+0x24c2890);memory.Put(bank+0x40,std::uint8_t{2});memory.Put(bank+0x84,std::uint32_t{4});memory.Put(bank+0x88,sources);memory.Put(bank+0x98,blob);memory.Put(bank+0x60,blob);memory.Put(bank+0x30,std::uint64_t{123});
  std::memcpy(payload.data(),ar::header,32);
  for(const auto& p:ar::pins){const auto src=sources+p.index*0x28;memory.Put(src,p.motion);memory.Put(src+8,bank);memory.Put(src+0x20,std::uint16_t{3});memory.Put(src+0x22,std::uint16_t(p.frames));memory.Put(src+0x24,std::uint32_t(p.index));
   std::memcpy(payload.data()+0x20+p.index*32,p.cycleHeader,32);std::uint32_t offset=0;std::memcpy(&offset,p.cycleHeader+8,4);std::memcpy(payload.data()+offset,&p.frames,4);}
  memory.Bytes(blob,payload.data(),payload.size());(void)pin;
 }
 bool Hash(const void* bytes,std::size_t n,char(&out)[65])noexcept{++hashCalls;const auto& p=ar::pins[selected];const auto* b=static_cast<const unsigned char*>(bytes);
  if(n==payload.size()){if(badWhole||std::memcmp(bytes,payload.data(),n))return false;std::memcpy(out,ar::kSha,65);return true;}
  if(n==p.mainBytes){if(badMain)return false;for(std::size_t i=4;i<n;++i)if(b[i]!=0xaa)return false;std::memcpy(out,p.mainSha,65);return true;}
  if(n==p.endBytes){if(badEnd)return false;for(std::size_t i=0;i<n;++i)if(b[i]!=0xaa)return false;std::memcpy(out,p.endSha,65);return true;}return false;
 }
 bool Read(ar::Asset& out,bool profile=true,const char** why=nullptr){auto h=[&](const void* b,std::size_t n,char(&digest)[65])noexcept{return Hash(b,n,digest);};return ar::FromGraph(memory,h,image,graph,out,why,profile);}
};
}
int AssetRegressionMain(){ar::Asset out{};const char* why=nullptr;
 for(unsigned index=0;index<4;++index){Fixture f(index);const bool ok=f.Read(out);
  if(!mgscoop::mgo_standing_cycle::Compiled()){Check(!ok&&!f.hashCalls,"cycle_off_no_source_read");continue;}
  Check(ok&&out.origin==ar::Origin::standingCandidate&&out.index==index&&out.motion==ar::pins[index].motion,"all_four_actual_selected_indices");
  Check(f.hashCalls==3&&out.endSize==ar::pins[index].endBytes,"whole_bank_actual_main_and_original_end_all_fingerprinted");
 }
 if(mgscoop::mgo_standing_cycle::Compiled()){
  Fixture wrong(1);Check(!wrong.Read(out,false)&&!wrong.hashCalls,"private_profile_required_before_payload_read");
  Fixture count(3);count.memory.Put(Fixture::bank+0x84,std::uint32_t{2});Check(!count.Read(out),"two_rest_bank_not_cycle");
  Fixture index(0);index.memory.Put(Fixture::sources+0x24,std::uint32_t{1092});Check(!index.Read(out),"retail_index_not_compact_index");
  Fixture alias(2);alias.memory.Put(Fixture::sources+0x28+8,Fixture::bank+8);Check(!alias.Read(out),"all_four_source_owners_required");
  Fixture whole(0);whole.badWhole=true;Check(!whole.Read(out,true,&why)&&std::strcmp(why,"cycle_bank_hash")==0,"whole_bank_changed_refused");
  Fixture main(3);main.badMain=true;Check(!main.Read(out),"actual_main_changed_refused");
  Fixture end(2);end.badEnd=true;Check(!end.Read(out),"original_end_markers_changed_refused");
  Fixture unbound(2);unbound.memory.Put(Addr{0x58018},Addr{0});Check(!unbound.Read(out),"selected_source_bank_must_still_be_bound_to_actual_engine");
  Fixture idle(0);idle.memory.Put(Fixture::node+0x100,0.f);Check(idle.Read(out)&&out.origin==ar::Origin::none&&!idle.hashCalls,"bank_load_is_not_effective_selection");
  Fixture unsupported(3);unsupported.graph.stateHash=0xc31867300b57ull;Check(unsupported.Read(out)&&out.origin==ar::Origin::none&&!unsupported.hashCalls,"uncovered_crouch_sleep_not_claimed");
 }
 std::cout<<"sleep_standing_cycle_asset_tests: "<<checks<<" detached checks; no game calls\n";
return 0;
}

namespace bp=mgscoop::standing_selection_policy;
namespace br=mgscoop::standing_selection_bank_read;
bp::Owner Owned(){bp::Owner o{};o.image=Fixture::image;o.engine=Fixture::engine;o.engineData=Fixture::data;o.banks=0x58000;o.capacity=1;
o.player=o.data=o.state=o.record=o.ps=o.primary=o.records=o.node=o.cs=o.rows=o.controller=o.mog=1;
o.birth=o.lifecycle=o.registration=o.generation=o.sceneEpoch=o.thread=1;o.body=1;o.sequence=0;return o;}
void Registered(Fixture& f){for(unsigned i=16;i<32;++i)f.memory.Put(Fixture::image+0x2a7f388+i*0x28,Addr{0});f.memory.Put(Fixture::image+0x2a7f388+17*0x28,Fixture::bank);f.memory.Put(Addr{1}+0x680,std::uint32_t{2});f.memory.Put(Fixture::bank+0x30,bp::kFileIdentity);}
int main(){AssetRegressionMain();bp::Bank bank{};auto owner=Owned();
for(unsigned index=0;index<4;++index){Fixture f(index);Registered(f);auto hash=[&](const void* b,std::size_t n,char(&h)[65])noexcept{return f.Hash(b,n,h);};
const auto read=br::Read(f.memory,hash,owner,bp::kFileIdentity,17,bank);
if(!mgscoop::mgo_standing_cycle::Compiled()){Check(!read&&!f.hashCalls,"bankOFF no payload hash");continue;}
Check(read&&bank.count==4&&bank.verifiedEntries==15&&bank.slot==0&&bank.fileIdentity==bp::kFileIdentity,"actual bound4 all native descriptors");
Check(bank.descriptors[index]==Fixture::sources+index*40&&bank.master==Fixture::blob+0xa0,"compact native source/master retained");}
if(mgscoop::mgo_standing_cycle::Compiled()){
Fixture f(1);Registered(f);auto hash=[&](const void* b,std::size_t n,char(&h)[65])noexcept{return f.Hash(b,n,h);};
Check(!br::Read(f.memory,hash,owner,0,17,bank),"independent native file identity required");
Check(!br::Read(f.memory,hash,owner,bp::kFileIdentity+1,17,bank),"different identity must not select matching-content bank");
Check(!br::Read(f.memory,hash,owner,bp::kFileIdentity,2,bank),"engine slot is not common registration index");
f.memory.Put(Fixture::image+0x2a7f388+18*0x28,Fixture::bank);Check(!br::Read(f.memory,hash,owner,bp::kFileIdentity,17,bank),"common resource alias refused");f.memory.Put(Fixture::image+0x2a7f388+18*0x28,Addr{0});
f.memory.Put(Addr{1}+0x680,std::uint32_t{0});Check(!br::Read(f.memory,hash,owner,bp::kFileIdentity,17,bank),"native common completion not fabricated");f.memory.Put(Addr{1}+0x680,std::uint32_t{2});
f.memory.Put(Addr{0x58048},Fixture::bank);f.memory.Put(Fixture::engine+0x1c,std::uint32_t{2});owner.capacity=2;
Check(!br::Read(f.memory,hash,owner,bp::kFileIdentity,17,bank),"same exact bank alias in two slots refused");
owner.capacity=1;f.memory.Put(Fixture::engine+0x1c,std::uint32_t{1});owner.engineData++;
Check(!br::Read(f.memory,hash,owner,bp::kFileIdentity,17,bank),"current engine data owner replaced");
owner=Owned();f.memory.Put(Fixture::engine,Fixture::image+0x2501988);
Check(!br::Read(f.memory,hash,owner,bp::kFileIdentity,17,bank),"wrong old engine class rejected");
}
std::cout<<"standing_selection_bank_tests: "<<checks<<" total detached checks\n";return 0;}
