#pragma once
#include "native_pose_teardown_diagnostic.hpp"
namespace mgscoop::native_pose_parts_diagnostic {
namespace td=native_pose_teardown_diagnostic;
struct Witness {
 std::uintptr_t q=0,ids=0,pools=0,status=0,blocks=0;
 std::uint32_t mask180=0,mask184=0,flags=0,count=0,realized=0,local=UINT32_MAX,selected=UINT32_MAX;
 std::uint16_t actualId[2]{0xffff,0xffff};std::uint8_t pool[2]{0xff,0xff},parts[2]{0xff,0xff};bool blockMatched[2]{};
};
inline bool Same(const Witness& a,const Witness& b)noexcept {
 return a.q==b.q&&a.ids==b.ids&&a.pools==b.pools&&a.status==b.status&&a.blocks==b.blocks&&a.mask180==b.mask180&&a.mask184==b.mask184&&
 a.flags==b.flags&&a.count==b.count&&a.realized==b.realized&&a.local==b.local&&a.selected==b.selected&&a.actualId[0]==b.actualId[0]&&a.actualId[1]==b.actualId[1]&&
 a.pool[0]==b.pool[0]&&a.pool[1]==b.pool[1]&&a.parts[0]==b.parts[0]&&a.parts[1]==b.parts[1]&&a.blockMatched[0]==b.blockMatched[0]&&a.blockMatched[1]==b.blockMatched[1];
}
template<class R>bool Once(R& r,std::uintptr_t image,std::uintptr_t go,const td::Model (&expected)[2],Witness& w)noexcept {
 w={};const auto P=expected[1].player;if(!image||image>UINTPTR_MAX-0xa01d000||P<0x20||P>UINTPTR_MAX-0x380||go!=P-0x20)return false;
 std::uintptr_t p=0;if(!td::Read(r,image+0x2a82c30,p)||p!=P||!td::Read(r,P,p)||p!=image+0x2295490||!td::Read(r,go,p)||p!=image+0x22953d0||
  !td::Read(r,P+0x60,w.q)||!w.q||w.q>UINTPTR_MAX-0x188||!td::Read(r,w.q,p)||p!=image+0x23ad970||
  !td::Read(r,w.q+0x180,w.mask180)||!td::Read(r,w.q+0x184,w.mask184)||!td::Read(r,P+0x204,w.flags)||
  !td::Read(r,P+0x208,w.count)||w.count<2||w.count>16||!td::Read(r,P+0x20c,w.realized)||!td::Read(r,P+0x214,w.local)||!td::Read(r,P+0x218,w.selected)||
  !td::Read(r,P+0x70,w.ids)||!w.ids||w.ids>UINTPTR_MAX-4||!td::Read(r,P+0x78,w.pools)||!w.pools||w.pools>UINTPTR_MAX-2||
  !td::Read(r,P+0x90,w.status)||!w.status||w.status>UINTPTR_MAX-2||!td::Read(r,P+0x378,w.blocks)||!w.blocks||w.blocks>UINTPTR_MAX-16*0x6a8)return false;
 for(unsigned index=0;index<2;++index){if(!td::Read(r,w.ids+index*2,w.actualId[index])||!td::Read(r,w.pools+index,w.pool[index])||!td::Read(r,w.status+index,w.parts[index]))return false;
  w.blockMatched[index]=w.pool[index]<16&&w.blocks+std::uintptr_t(w.pool[index])*0x6a8==expected[index].block;
 }return true;
}
template<class R>bool Pair(R& r,std::uintptr_t image,std::uintptr_t go,const td::Model (&expected)[2],Witness& w)noexcept {
 w={};Witness a{},b{};if(!Once(r,image,go,expected,a)||!Once(r,image,go,expected,b)||!Same(a,b))return false;w=b;return true;
}
// Native bitmasks index actual loop slot, not the pool byte. These data are
// only a pre-fault witness; no prediction/forwarding/teardown exemption exists.
}
