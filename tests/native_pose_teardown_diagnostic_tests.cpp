#include "native_pose_teardown_diagnostic.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
namespace d=mgscoop::native_pose_teardown_diagnostic;
struct Memory {
 const std::uintptr_t image=0x140000000ull,P=0x10000;d::Model expected[2]{};
 std::map<std::uintptr_t,std::vector<unsigned char>> cells;std::uintptr_t flip=0;unsigned hits=0;
 template<class T>void Put(std::uintptr_t at,T value){auto& b=cells[at];b.resize(sizeof(value));std::memcpy(b.data(),&value,sizeof(value));}
 bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept {auto it=cells.find(at);if(it==cells.end()||it->second.size()!=n)return false;std::memcpy(out,it->second.data(),n);if(at==flip&&++hits==2)static_cast<unsigned char*>(out)[0]^=1;return true;}
 Memory(){Put(image+0x2a82c30,P);Put(P,image+0x2295490);
  for(unsigned id=0;id<2;++id){auto& e=expected[id];e={P,0x20000+id*0x10000,0x40000+id*0x10000,0x60000+id*0x10000,0x80000+id*0x10000,id};
   Put(e.block,image+0x2295210);Put(e.block+8,std::uint32_t(1));Put(e.block+0xc,std::uint32_t(id));Put(e.block+0x138,P);Put(e.block+0x78,e.state);Put(e.block+0x10,e.cs);
   Put(e.state,image+0x23b0c70);Put(e.state+0x408,e.block);Put(e.state+0x238,e.cs);
   Put(e.cs,image+0x22e6070);Put(e.cs+8,std::uint32_t(1));Put(e.cs+0xc,std::uint32_t(id));Put(e.cs+0x10,e.entry);
  }Put(P-0x20,image+0x22953d0);Put(image+0x22e6070+0x548,image+0xaf0140);Put(image+0x2295210+0xf8,image+0x9cc850);Put(image+0x22953d0+0xb8,image+0x9bb7a0);Put(image+0x22953d0+8,image+0x9c2f60);
 }
};
int main(){unsigned good=0,bad=0;auto Check=[&](bool ok,const char* why){if(ok)++good;else{++bad;std::printf("FAIL %s\n",why);}};
 for(unsigned id=0;id<2;++id){Memory m;d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[id].cs,0xaf0140,m.expected,s)&&s.typed[0]&&s.typed[1]&&s.same[id]&&s.roles[id]==4&&s.slotMatched&&s.slot==0x548,"exact CS deleting slot and body role are paired without pointer exports");}
 {Memory m;d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].block,0x9cc850,m.expected,s)&&s.rangeKnown&&s.count==1&&s.base==1&&s.slotMatched&&s.roles[1]==1,"indexed rebind receipt reports native raw range and exact f8 slot");}
 {Memory m;d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].block,0x9bfc90,m.expected,s)&&s.rangeKnown&&s.slot==UINT32_MAX&&!s.slotMatched,"direct indexed retire has no fabricated vtable slot");}
 {Memory m;d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].entry,0xb6cd60,m.expected,s)&&s.roles[1]==8&&s.slot==UINT32_MAX&&s.objectTypeRva==0,"plain model entry records have no invented object type");}
 {Memory m;d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].state,0x9bfc90,m.expected,s)&&s.roles[1]==2&&!s.rangeKnown,"PS role is distinct from block/indexed target");}
 for(auto hook:{std::uint32_t(0x9cd030),std::uint32_t(0x9bb7a0),std::uint32_t(0x9c2f70)}){Memory m;d::Snapshot s{};Check(d::Pair(m,m.image,m.P-0x20,hook,m.expected,s)&&s.roles[0]==16&&s.roles[1]==16&&(hook==0x9cd030?!s.slotMatched:s.slotMatched),"parts and whole-GO paths retain exact method classification");}
 {Memory m;m.Put(m.expected[1].block+8,std::uint32_t(0));m.Put(m.expected[1].block+0xc,std::uint32_t(9));d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].block,0x9cc850,m.expected,s)&&!s.typed[1]&&s.rangeKnown&&s.count==0&&s.base==9,"out-of-range native metadata is logged raw and never treated as permission");}
 {Memory m;m.Put(m.image+0x22e6070+0x548,m.image+0xaf0141);d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].cs,0xaf0140,m.expected,s)&&!s.slotMatched&&s.slotFunctionRva==0xaf0141,"wrong current slot records mismatch without claiming qualification");}
 {Memory m;m.Put(m.expected[1].block+0x138,m.P+8);d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].block,0x9cc850,m.expected,s)&&!s.typed[1]&&s.roles[1]==0&&d::Roles(m.expected[1].block,m.expected[1])==1,"wrong current owner separates retained role from typed current role");}
 {Memory m;m.expected[1].entry+=0x10;d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].cs,0xaf0140,m.expected,s)&&s.typed[1]&&!s.same[1],"fresh model entry replacement remains visible despite typed CS");}
 for(auto field:{std::uintptr_t(0x30000+0x10),std::uintptr_t(0x70000+0x10)}){Memory m;m.flip=field;d::Snapshot s{};Check(!d::Pair(m,m.image,m.expected[1].cs,0xaf0140,m.expected,s),"torn block-CS or CS-entry identity rejects coherent diagnostic label");}
 {Memory m;m.Put(m.image+0x2a82c30,m.P+8);d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].cs,0xaf0140,m.expected,s)&&!s.typed[0]&&!s.typed[1],"changed global player does not invent current body ownership");}
 {Memory m;d::Snapshot s{};Check(!d::Pair(m,UINTPTR_MAX-8,m.expected[1].cs,0xaf0140,m.expected,s),"image overflow refuses diagnostic reads");}
 {Memory m;m.Put(m.expected[1].state+0x138,std::uintptr_t(0xdead));d::Snapshot s{};Check(d::Pair(m,m.image,m.expected[1].cs,0xaf0140,m.expected,s)&&s.typed[1],"unqualified PS138 padding is irrelevant to actual PS408/block138 ownership");}
 Check(d::Rva(0x140000000ull,0x10000)==0&&d::Rva(0x140000000ull,0x1409c2a2aull)==0x9c2a2a,"caller/type exports are bounded RVAs and never raw pointers");
 std::printf("%u passed; %u failed\n",good,bad);return bad?1:0;}
