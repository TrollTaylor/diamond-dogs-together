#include "player_tree_closure_read.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
namespace cl=mgscoop::player_tree_closure;namespace pins=mgscoop::player_tree_closure_contract;namespace tree=mgscoop::player_op2_tree;
struct Regions {
 std::map<std::uintptr_t,std::vector<unsigned char>> values;std::uintptr_t denied=0;
 void Add(std::uintptr_t at,std::size_t bytes){auto& v=values[at];if(v.size()<bytes)v.resize(bytes);}
 template<class T>void Set(std::uintptr_t at,T value){
  for(auto& v:values)if(at>=v.first&&at-v.first<=v.second.size()&&sizeof(T)<=v.second.size()-(at-v.first)){std::memcpy(v.second.data()+at-v.first,&value,sizeof(value));return;}
  std::abort();
 }
 bool Read(std::uintptr_t at,void* out,std::size_t bytes)noexcept{
  if(at==denied)return false;
  for(const auto& v:values)if(at>=v.first&&at-v.first<=v.second.size()&&bytes<=v.second.size()-(at-v.first)){std::memcpy(out,v.second.data()+at-v.first,bytes);return true;}
  return false;
 }
};
struct Code:Regions {
 static constexpr std::uintptr_t image=0x140000000ull;unsigned begins=0;bool driftOnLate=false;
 Code(){for(const auto& s:pins::sites){Add(image+s.rva,s.size);for(std::size_t k=0;k<s.size;++k)Set<unsigned char>(image+s.rva+k,static_cast<unsigned char>((pins::Hex(s.bytes[2*k])<<4)|pins::Hex(s.bytes[2*k+1])));}}
 bool operator()(std::uintptr_t at,void* out,std::size_t bytes)noexcept{
  if(at==image+pins::sites[0].rva&&++begins==2&&driftOnLate)Set<unsigned char>(at,0x90);
  return Read(at,out,bytes);
 }
};
struct Memory:Regions {
 static constexpr std::uintptr_t image=Code::image,player=0x100000,block=0x110000,state=0x120000,otherState=0x121000;
 static constexpr std::uintptr_t root=0x130000,psNode=0x130100,clientNode=0x130400,nullNode=0x130c00,carrierNode=0x133200;
 static constexpr std::uintptr_t rootPhase=0x140000,psPhase=0x141000,clientPrimary=0x144000,nullPrimary=0x14c000,carrierPhase=0x152000;
 static constexpr std::uintptr_t cell=0x170000,client=0x180000,clientSlab=0x190000,nullSlab=0x150000;
 unsigned blockReads=0;std::uintptr_t changeAt=0;unsigned changeRead=0;std::uintptr_t changeValue=0;
 void Node(unsigned row,std::uintptr_t node,std::uintptr_t phase,std::uintptr_t phaseVt,unsigned id,std::uintptr_t parent){
  Add(node,0x70);Add(phase,0x10);Set<std::uintptr_t>(node,image+0x2306628);Set<std::uint32_t>(node+0x20,1);Set<std::uint32_t>(node+0x24,id);
  Set<std::uint32_t>(node+0x2c,row);Set<std::uintptr_t>(node+0x38,phase);Set<std::uintptr_t>(node+0x58,parent);Set<std::uintptr_t>(phase,image+phaseVt);Set<std::uintptr_t>(phase+8,node);
  Set<std::uintptr_t>(block+0x190+row*8,node);Set<std::uintptr_t>(block+0x3b8+row*8,phase);
 }
 explicit Memory(unsigned id=1){
  for(const auto& s:pins::slots){Add(image+s.rva,sizeof(std::uintptr_t));Set<std::uintptr_t>(image+s.rva,image+s.target);}
  Add(image+0x2a82c30,8);Set<std::uintptr_t>(image+0x2a82c30,player);Add(player,0x410);Set<std::uintptr_t>(player,image+0x2295490);
  Add(block,0x6a8);Set<std::uintptr_t>(block,image+0x2295210);Set<std::uint32_t>(block+8,1);Set<std::uint32_t>(block+0xc,id);Set<std::uintptr_t>(block+0x138,player);Set<std::uintptr_t>(block+0x78,state);
  for(const auto at:{state,otherState}){Add(at,0x410);Set<std::uintptr_t>(at,image+0x23b09f0);Set<std::uint32_t>(at+8,id);Set<std::uintptr_t>(at+0x408,block);}
  Add(clientPrimary,0x60);Add(nullPrimary,0x70);
  Node(0,root,rootPhase,0x23bc170,id,0);Node(1,psNode,psPhase,0x23b0e50,id,root);
  Node(4,clientNode,clientPrimary+0x30,0x23cd7f0,id,psNode);Node(0x12,nullNode,nullPrimary+0x30,0x23d0000,id,psNode);Node(0x32,carrierNode,carrierPhase,0x23b9b50,id,psNode);
  Set<std::uintptr_t>(block+0x210,clientNode);
  Set<std::uintptr_t>(root+0x50,psNode);Set<std::uintptr_t>(psNode+0x50,clientNode);Set<std::uintptr_t>(clientNode+0x40,nullNode);Set<std::uintptr_t>(nullNode+0x40,carrierNode);
  Set<std::uintptr_t>(clientPrimary,image+0x23cd798);Set<std::uintptr_t>(clientPrimary+0x10,image+0x23cd7c0);Set<std::uintptr_t>(clientPrimary+8,block);
  Add(cell,0x18);Add(client,0x68);Add(clientSlab,0x110);Set<std::uintptr_t>(clientPrimary+0x40,cell);Set<std::uintptr_t>(clientPrimary+0x48,client+0x20);Set<std::uintptr_t>(cell+0x10,client);
  Set<std::uintptr_t>(client,image+0x2347e50);Set<std::uintptr_t>(client+8,0x3dfd3f482579ull);Set<std::uintptr_t>(client+0x10,cell);Set<std::uintptr_t>(client+0x20,image+0x2347e80);
  Set<std::uintptr_t>(client+0x28,clientSlab);Set<std::uintptr_t>(client+0x30,clientSlab);Set<std::uint64_t>(client+0x38,0x110);Set<std::uint64_t>(client+0x40,0x110);Set<std::uint16_t>(client+0x4a,1);Set<std::uintptr_t>(client+0x50,clientSlab+0x30);Set<std::uint32_t>(client+0x58,id);
  Set<std::uintptr_t>(clientSlab,image+0x645940);Set<std::uintptr_t>(clientSlab+0x10,clientSlab+0x30);Set<std::uint64_t>(clientSlab+0x18,0xe0);Set<std::uint64_t>(clientSlab+0x20,1);
  Set<std::uintptr_t>(nullPrimary,image+0x23cffa8);Set<std::uintptr_t>(nullPrimary+0x10,image+0x23cffd0);Set<std::uintptr_t>(nullPrimary+8,block);Add(nullSlab,0x60);
  Set<std::uintptr_t>(nullPrimary+0x40,nullSlab);Set<std::uintptr_t>(nullPrimary+0x48,nullSlab);Set<std::uint64_t>(nullPrimary+0x50,0x60);Set<std::uint64_t>(nullPrimary+0x58,0x60);Set<std::uint16_t>(nullPrimary+0x62,1);Set<std::uintptr_t>(nullPrimary+0x68,nullSlab+0x30);
  Set<std::uintptr_t>(nullSlab,image+0x645940);Set<std::uintptr_t>(nullSlab+0x10,nullSlab+0x30);Set<std::uint64_t>(nullSlab+0x18,0x30);Set<std::uint64_t>(nullSlab+0x20,1);
 }
 bool operator()(std::uintptr_t at,void* out,std::size_t bytes)noexcept{
  if(at==block&&++blockReads==changeRead&&changeAt)Set<std::uintptr_t>(changeAt,changeValue);
  return Read(at,out,bytes);
 }
};
int main(){unsigned checks=0,failed=0;const auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::cerr<<name<<'\n';}};
 cl::Observation observed{};const char* why="unset";std::uintptr_t mismatch=0;
 const auto read=[&](Memory& data,Code& code,cl::Operation op=cl::Operation::recipientOp2){return cl::ReadPair(data,code,Memory::image,0xa01d000,Memory::player,Memory::block,op,observed,why,mismatch);};
 {Memory data;Code code;check(read(data,code)&&observed.count==5&&observed.needsRow4Lifetime&&observed.needsNullWitness&&!observed.needsRetainedCarrierReply,"op2 currentbody1 carries exact conditional native effects without call permission");}
 {Memory data(0);Code code;data.Set<std::uintptr_t>(Memory::nullSlab+0x50,0xdeadbeef);check(read(data,code,cl::Operation::carrierReply)&&observed.needsRetainedCarrierReply&&observed.needsRow4Lifetime&&!observed.needsNullWitness,"op1 currentbody0 needs retained carrier reply; row12 earlyreject does not need null state");}
 for(const auto& pair:cl::pairs){tree::Node n{};n.row=pair.handler==0xff4180?1u:(pair.handler==0x1279e90?4u:(pair.handler==0x1288cc0?0x12u:(pair.handler==0x11301b0?0x32u:2u)));n.phaseVtRva=pair.phase;n.handlerRva=pair.handler;
  check(cl::ClassifyNode(n,cl::Operation::carrierReply)!=cl::Effect::unqualified&&cl::ClassifyNode(n,cl::Operation::recipientOp2)!=cl::Effect::unqualified,"every pinned currentphase-handler pair is classified independently of candidate factory metadata");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::player,Memory::image+0x22953d0);check(!read(data,code)&&observed.count==0,"wrong current Player root");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::image+0x2a82c30,0);check(!read(data,code),"currentglobal owner");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::state+0x408,0);check(!read(data,code),"currentstate block backlink");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::client+8,0);check(!read(data,code),"current external client classhash");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::client+0x10,0);check(!read(data,code),"current external client plaincell backlink");}
 {Memory data;Code code;data.Set<std::uint32_t>(Memory::clientSlab+0x108,8);check(!read(data,code),"native row4 transition bit refuses callback-producing branch");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::nullSlab+0x50,0xdeadbeef);check(!read(data,code),"op2 nonnull native record cannot masquerade as hash rejection");}
 {Memory data(0);Code code;data.Set<std::uintptr_t>(Memory::carrierPhase,Memory::image+0x23bc170);check(!read(data,code,cl::Operation::carrierReply),"op1 intended carrier reply cannot be replaced by mapped RET0");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::carrierPhase,Memory::image+0x23bc170);check(read(data,code)&&!observed.needsRetainedCarrierReply,"op2 mapped registered RET0 is harmless without claiming its creator type");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::carrierPhase,Memory::image+0x23b9b58);check(!read(data,code),"unknown phase identity refuses while raw census remains independent");}
 {Memory data;Code code;data.Set<std::uint32_t>(Memory::carrierNode+0x2c,0x33);data.Set<std::uintptr_t>(Memory::block+0x190+0x33*8,Memory::carrierNode);data.Set<std::uintptr_t>(Memory::block+0x3b8+0x33*8,Memory::carrierPhase);check(!read(data,code),"carrier known handler on wrong registered row refuses");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::psNode+0x58,0);check(!read(data,code),"actual native tree parent");}
 {Memory data;Code code;data.Set<std::uintptr_t>(Memory::nullNode+0x40,Memory::root);check(!read(data,code),"actual native tree cycle");}
 {Memory data;Code code;data.changeRead=4;data.changeAt=Memory::nullSlab+0x50;data.changeValue=0xdeadbeef;check(!read(data,code)&&observed.count==0,"late null record mutation is caught by composed second witness");}
 {Memory data;Code code;data.changeRead=4;data.changeAt=Memory::clientSlab+0x108;data.changeValue=8;check(!read(data,code),"late row4 activity cannot reuse initial inactive witness");}
 {Memory data;Code code;data.changeRead=4;data.changeAt=Memory::carrierNode+0x40;data.changeValue=Memory::root;check(!read(data,code),"tree bracket after witness refuses newly introduced cycle");}
 {Memory data;Code code;code.driftOnLate=true;check(!read(data,code)&&std::strcmp(why,"late_code_or_slot_changed")==0,"late original-code drift refuses after typed witness pair");}
 {Memory data;Code code;check(!read(data,code,static_cast<cl::Operation>(3)),"unknown operation never native permission");}
 {Memory data(0);Code code;check(!read(data,code),"operation2 targetbody1 never projected from actualbody0");}
 {Memory data;Code code;check(!read(data,code,cl::Operation::carrierReply),"operation1 targetbody0 separate from source ownedbody1");}
 for(const auto& site:pins::sites){Memory data;Code code;code.Set<unsigned char>(Memory::image+site.rva+site.size-1,static_cast<unsigned char>(((pins::Hex(site.bytes[2*(site.size-1)])<<4)|pins::Hex(site.bytes[2*(site.size-1)+1]))^1u));
  check(!read(data,code)&&mismatch==site.rva+site.size-1,"complete function final byte is pinned; no prologue-only gate");}
 for(const auto& slot:pins::slots){Memory data;Code code;data.Set<std::uintptr_t>(Memory::image+slot.rva,Memory::image+0x645940);
  if(slot.target==0x645940)data.Set<std::uintptr_t>(Memory::image+slot.rva,0);
  check(!read(data,code)&&mismatch==slot.rva,"current actual table cells cannot use original-code overlay");}
 {Memory data;tree::Observation census{};const char* reason="unset";data.Set<std::uintptr_t>(Memory::image+0x23b9b78,Memory::image+0x123456);check(tree::ReadPair(data,Memory::image,Memory::player,Memory::block,1,census,reason)&&census.count==5&&census.nodes[4].handlerRva==0x123456,"numerical unknown-handler census remains available independently of closure classification");}
 {cl::Observation a{},b{};a.tree.count=b.tree.count=71;check(!cl::Same(a,b),"forged currenttree count cannot read out of bounds");}
 {cl::Observation a{},b{};a.count=b.count=71;check(!cl::Same(a,b),"forged classified count cannot read out of bounds");}
 {cl::Observation a{},b{};a.tree.count=1;check(!cl::Same(a,b),"classification and currenttree counts must agree");}
 {cl::Observation a{},b{};b.tree.count=1;check(!cl::Same(a,b),"paired classification count mismatch refuses before tree comparison");}
 {tree::Node n{};n.row=70;n.phaseVtRva=0x23bc170;n.handlerRva=0x645940;check(cl::ClassifyNode(n,cl::Operation::recipientOp2)==cl::Effect::unqualified,"out of bounds row refused even for native RET0");}
 {Memory data;Code code;check(!pins::CodeGate(code,data,Memory::image,0x1000,mismatch),"caller image-span must cover complete code and current tables");}
 {Memory data;Code code;check(!pins::CodeGate(code,data,0,0xa01d000,mismatch),"missing image base fails before reads");}
 std::cout<<checks<<" source assertions, "<<failed<<" failures\n";return failed?1:0;
}
