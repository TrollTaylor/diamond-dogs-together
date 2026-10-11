#define main PriorSleepFixtureMain
#include "sleep_plugin_tests.cpp"
#undef main
#include "sleep_graph_read.hpp"
#include <fstream>
#include <vector>
namespace graph=mgscoop::sleep_graph_read;
Memory GraphFixture(){auto m=Fixture();const auto s=S();
 for(unsigned k=0;k<2;++k){const auto ps=std::uintptr_t(0x50000+k*0x1000),block=std::uintptr_t(0x40000+k*0x6a8),cs=std::uintptr_t(0xe0000+k*0x1000),entry=std::uintptr_t(0xf0000+k*0x1000),vector=std::uintptr_t(0x110000+k*0x1000),controller=std::uintptr_t(0x120000+k*0x1000);
  m.Put(ps+0x238,cs);m.Put(block+0x10,cs);m.Put(cs,s.image+0x22e6070);m.Put<std::uint32_t>(cs+8,1);m.Put<std::uint32_t>(cs+0xc,k);m.Put(cs+0x10,entry);m.Put(entry+0x90,vector);m.Put(vector,controller);m.Put(controller,s.image+0x250ba50);m.Put(controller+0x10,s.image+0x250bc00);m.Put<std::uint32_t>(controller+0xd4,0);m.Put<std::uint8_t>(controller+0x185,0);m.Put<std::uintptr_t>(controller+0xf0,0x130000);m.Put<std::uintptr_t>(controller+0x1b8,0x200000);m.Put<std::uintptr_t>(controller+0x1a0,0x200168);
 }
 const char magic[15]="FOXMOTIONGRAPH";for(unsigned k=0;k<15;++k)m.Put<std::uint8_t>(0x200000+k,static_cast<std::uint8_t>(magic[k]));
 m.Put<std::uint32_t>(0x200010,6);m.Put<std::uint32_t>(0x20001c,4);m.Put<std::int32_t>(0x200020,0x20);m.Put<std::uint32_t>(0x200048,3);m.Put<std::int32_t>(0x20004c,0xd4);m.Put<std::int32_t>(0x200180,0x780);m.Put<std::uint64_t>(0x200900,0xa7a7c31867300b57ull);return m;
}
int main(int argc,char** argv){const auto s=S();auto m=GraphFixture();graph::Reply out{};
 Check(graph::ReadPair(m,s,out)&&out.ready&&out.bodies[0].currentNode&&out.bodies[0].stateHash==0xc31867300b57ull&&out.bodies[0].index==1&&out.bodies[0].count==3&&out.bodies[0].sleepHashCandidate,"typed live graph0 current-state hash observed; high16 masked");
 auto no=m;no.Put<std::uintptr_t>(0x1201a0,0);Check(graph::ReadPair(no,s,out)&&out.ready&&!out.bodies[0].currentNode&&!out.bodies[0].sleepHashCandidate,"initialized graph with no current node is not sleep selection");
 auto ordinary=m;ordinary.Put<std::uint64_t>(0x200900,0x1234567890abull);Check(graph::ReadPair(ordinary,s,out)&&!out.bodies[0].sleepHashCandidate&&out.bodies[0].stateHash==0x1234567890abull,"other current state remains diagnostic evidence");
 for(auto x:{Mutation{0xe0000,0,8,"graph_cs0"},{0x40010,0xe1000,8,"graph_cs0"},{0xe0008,2,4,"graph_cs0"},{0xe000c,1,4,"graph_cs0"},{0x120000,0x142509b00,8,"graph_controller0"},{0x120010,0x142509ca8,8,"graph_controller0"},{0x1200d4,1,4,"graph_controller0"},{0x120185,1,1,"graph_controller0"},{0x1200f0,0,8,"graph_controller0"},{0x1201b8,0xfffffffffffffff0ull,8,"graph_controller0"},{0x200000,0,1,"graph_header0"},{0x200010,5,4,"graph_header0"},{0x20001c,0,4,"graph_header0"},{0x20001c,33,4,"graph_header0"},{0x200020,0x7fffffff,4,"graph_header0"},{0x200020,0x80000000,4,"graph_header0"},{0x200048,16385,4,"graph_header0"},{0x20004c,0x7fffffff,4,"graph_header0"},{0x1201a0,0x200169,8,"graph_current0"},{0x1201a0,0x2001f8,8,"graph_current0"},{0x1201a0,0x100000,8,"graph_current0"},{0x200180,0x80000000,4,"graph_current0"},{0x111000,0x120000,8,"graph_aliases"}}){auto n=GraphFixture();Change(n,x);out.ready=true;out.bodies[0].stateHash=0x999;Check(!graph::ReadPair(n,s,out)&&!out.ready&&out.bodies[0].stateHash==0&&!std::strcmp(out.reason,x.label),"invalid lifecycle/header/range/binding refuses with constant stage");}
 auto partial=GraphFixture();partial.bytes.erase(0x200903);Check(!graph::ReadPair(partial,s,out)&&!out.ready,"partial hash read refuses");
 auto race=GraphFixture();race.flip=true;race.flipAt=0x200900;race.flipValue=0x99;Check(!graph::ReadPair(race,s,out)&&!std::strcmp(out.reason,"graph_changed"),"current graph state transition between snapshots refused");
 auto layers=GraphFixture();layers.flip=true;layers.flipAt=0x20001c;layers.flipValue=3;Check(!graph::ReadPair(layers,s,out)&&!std::strcmp(out.reason,"graph_changed"),"live graph metadata mutation refused");
 auto stale=s;stale.ownedGeneration++;Check(!graph::ReadPair(m,stale,out),"old owner generation refuses graph read");stale=s;stale.batch=true;Check(!graph::ReadPair(m,stale,out),"input batch refuses graph read");
 std::uint32_t relative=0;Check(!graph::Relative(1,-2,8,relative),"negative relative range cannot underflow");Check(!graph::Relative(0x3fffff,0,8,relative),"hash cannot straddle ceiling");
 Memory gate;std::uintptr_t mismatch=0;
 for(const auto& site:mgscoop::sleep_graph_contract::sites)for(std::size_t j=0;j<site.size;++j)gate.bytes[s.image+site.rva+j]=site.bytes[j];
 for(const auto& slot:mgscoop::sleep_graph_contract::slots)gate.Put<std::uintptr_t>(s.image+slot.vtable_rva+slot.offset,s.image+slot.target_rva);
 Check(graph::CodeGate(gate,s.image,0xa01d000,mismatch),"complete graph contract and slots accepted");
 for(const auto& site:mgscoop::sleep_graph_contract::sites){gate.bytes[s.image+site.rva]^=1;Check(!graph::CodeGate(gate,s.image,0xa01d000,mismatch),"any exact complete graph function byte change refused");gate.bytes[s.image+site.rva]^=1;}
 for(const auto& slot:mgscoop::sleep_graph_contract::slots){gate.Put<std::uintptr_t>(s.image+slot.vtable_rva+slot.offset,s.image+slot.target_rva+1);Check(!graph::CodeGate(gate,s.image,0xa01d000,mismatch),"any graph getter/owner slot change refused");gate.Put<std::uintptr_t>(s.image+slot.vtable_rva+slot.offset,s.image+slot.target_rva);}

 if(argc>1){std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> asset((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());Check(asset.size()==732872,"frozen resident MOG asset size");
  struct AssetMemory {
   Memory control;const std::vector<unsigned char>* asset=nullptr;
   bool operator()(std::uintptr_t at,void* out,std::size_t count) noexcept {
    if(at>=0x200000&&at-0x200000<asset->size()){const auto offset=at-0x200000;if(count>asset->size()-offset)return false;std::memcpy(out,asset->data()+offset,count);return true;}return control(at,out,count);
   }
  } assetReader{GraphFixture(),&asset};
  struct Known {std::uint32_t offset,index;std::uint64_t hash;};
  for(const auto& state:{Known{0x9288,517,0x531872a59848ull},Known{0x97e0,536,0x565a6ead28c6ull},Known{0xf420,864,0x89349f0ff8a2ull},Known{0x11868,993,0x9e3bd6cce5f5ull},Known{0x13cf8,1123,0xb1c6aee936a4ull},Known{0x14400,1148,0xb62a05a085a4ull},Known{0x144d8,1151,0xb6c742e88a85ull},Known{0x14ef8,1187,0xbd5546f99202ull},Known{0x15a38,1227,0xc31867300b57ull},Known{0x15b10,1230,0xc37aca2100e9ull},Known{0x17b20,1344,0xd7654fb0a3f2ull},Known{0x196f8,1443,0xe66c3c2d192aull},Known{0x19818,1447,0xe6d3ed730317ull},Known{0x19860,1448,0xe6d6abd46423ull},Known{0x1a628,1497,0xeceee0f51e77ull},Known{0x1b0d8,1535,0xf2364261c9d4ull}}){assetReader.control.Put<std::uintptr_t>(0x1201a0,0x200000+state.offset);Check(graph::ReadPair(assetReader,s,out)&&out.bodies[0].count==1871&&out.bodies[0].index==state.index&&out.bodies[0].stateHash==state.hash&&out.bodies[0].sleepHashCandidate,"actual frozen MOG node layout and candidate hash decoded");}
 }

 std::printf("%u passed, %u failed\n",good,bad);return bad?1:0;
}
