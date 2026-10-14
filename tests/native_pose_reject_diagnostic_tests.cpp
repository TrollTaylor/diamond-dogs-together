#include "native_pose_reject_diagnostic.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <cstdio>
namespace d=mgscoop::native_pose_reject_diagnostic;
struct Memory {
 const std::uintptr_t image=0x140000000ull,player=0x10000,iface=0x20020;
 std::map<std::uintptr_t,std::vector<unsigned char>> cells;std::uintptr_t flip=0;unsigned hits=0;
 template<class T>void Put(std::uintptr_t at,T value){auto& b=cells[at];b.resize(sizeof(value));std::memcpy(b.data(),&value,sizeof(value));}
 bool operator()(std::uintptr_t at,void* out,std::size_t n)noexcept {auto i=cells.find(at);if(i==cells.end()||i->second.size()!=n)return false;std::memcpy(out,i->second.data(),n);if(at==flip&&++hits==2)static_cast<unsigned char*>(out)[0]^=1;return true;}
 Memory(){Put(image+0x2a82c30,player);Put(player,image+0x2295490);Put(player+0x204,std::uint32_t(1));Put(player+0x208,std::uint32_t(2));Put(player+0x20c,std::uint32_t(2));Put(player+0x214,std::uint32_t(0));Put(player+0x218,std::uint32_t(1));Put(player+0xc0,iface);Put(iface,image+0x23c7500);Put(iface-0x20,image+0x23c74c8);Put(iface+8,player);Put(iface+0x6c,std::uint8_t(5));Put(iface+0x6d,std::uint8_t(0));}
};
int main(){unsigned good=0,bad=0;auto Check=[&](bool ok,const char* why){if(ok)++good;else{++bad;std::printf("FAIL %s\n",why);}};
 {Memory m;d::Snapshot s{};Check(d::Pair(m,m.image,m.player,s)&&s.player&&s.c0&&s.active==5&&s.requested==0&&s.selected==1,"actual paired typed fields report active vs requested separately");}
 {Memory m;m.Put(m.player+0x204,std::uint32_t(0x40001));d::Snapshot s{};Check(d::Pair(m,m.image,m.player,s)&&s.flags==0x40001,"alt-dispatch sleep hypothesis is observed without granting a guard waiver");}
 {Memory m;m.Put(m.player+0x208,std::uint32_t(1));m.Put(m.player+0x20c,std::uint32_t(3));m.Put(m.player+0x214,std::uint32_t(1));d::Snapshot s{};Check(d::Pair(m,m.image,m.player,s)&&s.count==1&&s.realized==3&&s.real==1,"raw failed-admission values remain faithful diagnostic data");}
 for(unsigned test=0;test<4;++test){Memory m;switch(test){case 0:m.Put(m.iface,m.image+0x23c7501);break;case 1:m.Put(m.iface-0x20,m.image+0x23c74c9);break;case 2:m.Put(m.iface+8,m.player+8);break;case 3:m.cells.erase(m.iface+0x6c);break;}
  d::Snapshot s{};Check(d::Pair(m,m.image,m.player,s)&&s.player&&!s.c0&&s.active==UINT32_MAX&&s.requested==UINT32_MAX,"invalid/unreadable C0 never fabricates standing mode while typed player remains reportable");}
 for(auto at:{std::uintptr_t(0x10000),std::uintptr_t(0x140000000ull+0x2a82c30)}){Memory m;m.Put(at,std::uintptr_t(0));d::Snapshot s{};Check(!d::Pair(m,m.image,m.player,s)&&!s.player,"wrong current global player or final type refuses snapshot");}
 for(auto at:{std::uintptr_t(0x10204),std::uintptr_t(0x20020+0x6c)}){Memory m;m.flip=at;d::Snapshot s{};Check(!d::Pair(m,m.image,m.player,s)&&!s.player,"Pflags or C0 active drift across paired reads refuses coherent label");}
 {Memory m;d::Snapshot s{};Check(!d::Pair(m,UINTPTR_MAX-8,m.player,s)&&!d::Pair(m,m.image,UINTPTR_MAX-8,s),"overflow identities refuse before dereference");}
 std::uint32_t seen=0;for(auto stage:{d::Stage::validate,d::Stage::capture}){Check(d::First(seen,stage),"first different failure stage logs independently");seen|=d::Bit(stage);Check(!d::First(seen,stage),"repeated same-stage failure stays bounded");}
 Check(!d::First(seen,d::Stage::count),"invalid stage cannot create an unbounded logging bit");
 std::printf("%u passed; %u failed\n",good,bad);return bad?1:0;}
