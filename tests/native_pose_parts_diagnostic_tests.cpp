#include "native_pose_parts_diagnostic.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
namespace d=mgscoop::native_pose_parts_diagnostic;namespace td=mgscoop::native_pose_teardown_diagnostic;
struct Memory {
 const std::uintptr_t image=0x140000000ull,P=0x10020,Q=0x20000,ids=0x30000,pools=0x31000,status=0x32000,blocks=0x40000;
 td::Model expected[2]{};std::map<std::uintptr_t,std::vector<unsigned char>> cells;std::uintptr_t flip=0;unsigned hits=0;
 template<class T>void Put(std::uintptr_t at,T v){auto& b=cells[at];b.resize(sizeof(v));std::memcpy(b.data(),&v,sizeof(v));}
 bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept {auto i=cells.find(at);if(i==cells.end()||i->second.size()!=n)return false;std::memcpy(out,i->second.data(),n);if(at==flip&&++hits==2)static_cast<unsigned char*>(out)[0]^=1;return true;}
 Memory(){expected[0]={P,blocks+4*0x6a8,0,0,0,0};expected[1]={P,blocks+7*0x6a8,0,0,0,1};Put(image+0x2a82c30,P);Put(P,image+0x2295490);Put(P-0x20,image+0x22953d0);Put(P+0x60,Q);Put(Q,image+0x23ad970);Put(Q+0x180,std::uint32_t(0));Put(Q+0x184,std::uint32_t(0));Put(P+0x204,std::uint32_t(0x1a487));Put(P+0x208,std::uint32_t(2));Put(P+0x20c,std::uint32_t(2));Put(P+0x214,std::uint32_t(0));Put(P+0x218,std::uint32_t(0));Put(P+0x70,ids);Put(P+0x78,pools);Put(P+0x90,status);Put(P+0x378,blocks);for(unsigned id=0;id<2;++id){Put(ids+id*2,std::uint16_t(id));Put(pools+id,std::uint8_t(id?7:4));Put(status+id,std::uint8_t(3));}}
};
int main(){unsigned good=0,bad=0;auto Check=[&](bool ok,const char* why){if(ok)++good;else{++bad;std::printf("FAIL %s\n",why);}};
 {Memory m;d::Witness w{};Check(d::Pair(m,m.image,m.P-0x20,m.expected,w)&&w.parts[1]==3&&w.actualId[1]==1&&w.pool[1]==7&&w.blockMatched[0]&&w.blockMatched[1],"real loop index1 maps to actualID1 and nonliteral pool7/retainedblock");}
 {Memory m;m.Put(m.Q+0x180,std::uint32_t(2));m.Put(m.Q+0x184,std::uint32_t(0x80));d::Witness w{};Check(d::Pair(m,m.image,m.P-0x20,m.expected,w)&&w.mask180==2&&w.mask184==0x80&&w.pool[1]==7,"native dirty masks remain loop-slot bits, not pool-index coercion");}
 {Memory m;m.Put(m.status+1,std::uint8_t(4));m.Put(m.P+0x214,std::uint32_t(1));d::Witness w{};Check(d::Pair(m,m.image,m.P-0x20,m.expected,w)&&w.parts[1]==4&&w.local==1,"effectful candidate status/local are reported without a forwarding rule");}
 {Memory m;m.Put(m.ids+2,std::uint16_t(0xffff));m.Put(m.pools+1,std::uint8_t(0xff));d::Witness w{};Check(d::Pair(m,m.image,m.P-0x20,m.expected,w)&&w.actualId[1]==0xffff&&w.pool[1]==0xff&&!w.blockMatched[1],"invalid ID/pool preserved as raw diagnostic mismatch");}
 for(auto at:{std::uintptr_t(0x10020),std::uintptr_t(0x10000),std::uintptr_t(0x20000)}){Memory m;m.Put(at,std::uintptr_t(0));d::Witness w{};Check(!d::Pair(m,m.image,m.P-0x20,m.expected,w),"wrong P/GO/Q final type refuses witness");}
 {Memory m;m.Put(m.P+0x208,std::uint32_t(1));d::Witness w{};Check(!d::Pair(m,m.image,m.P-0x20,m.expected,w),"one-body count cannot read missing index1 as actual parts state");}
 for(auto at:{std::uintptr_t(0x20180),std::uintptr_t(0x32001),std::uintptr_t(0x31001)}){Memory m;m.flip=at;d::Witness w{};Check(!d::Pair(m,m.image,m.P-0x20,m.expected,w),"dirty/status/pool drift rejects paired snapshot");}
 {Memory m;d::Witness w{};Check(!d::Pair(m,m.image,m.P,m.expected,w),"GO secondary offset is checked rather than assumed");}
 {Memory m;d::Witness w{};Check(!d::Pair(m,UINTPTR_MAX-8,m.P-0x20,m.expected,w),"image arithmetic overflow refuses before reads");}
 std::printf("%u passed; %u failed\n",good,bad);return bad?1:0;}
