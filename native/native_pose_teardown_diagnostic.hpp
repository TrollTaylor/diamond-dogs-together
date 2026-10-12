#pragma once
#include <cstdint>
#include <cstddef>
namespace mgscoop::native_pose_teardown_diagnostic {
// Pointer fields remain internal; public logging copies only roles/booleans/RVAs.
struct Model {std::uintptr_t player=0,block=0,state=0,cs=0,entry=0;unsigned id=0;};
inline bool Same(const Model& a,const Model& b)noexcept{return a.player==b.player&&a.block==b.block&&a.state==b.state&&a.cs==b.cs&&a.entry==b.entry&&a.id==b.id;}
inline std::uint32_t Roles(std::uintptr_t object,const Model& m)noexcept {
 if(!object)return 0;return (object==m.block?1u:0u)|(object==m.state?2u:0u)|(object==m.cs?4u:0u)|(object==m.entry?8u:0u)|
  (m.player>=0x20&&object==m.player-0x20?16u:0u)|(object==m.player?32u:0u);
}
inline std::uint32_t Rva(std::uintptr_t image,std::uintptr_t at)noexcept{return image&&at>=image&&at-image<0xa01d000?static_cast<std::uint32_t>(at-image):0;}
template<class R,class T>bool Read(R& r,std::uintptr_t at,T& v)noexcept{return at&&at<=UINTPTR_MAX-sizeof(v)&&r(at,&v,sizeof(v));}
template<class R>bool Current(R& r,std::uintptr_t image,const Model& expected,Model& m)noexcept {
 m={};if(!image||image>UINTPTR_MAX-0xa01d000||!expected.player||expected.player>UINTPTR_MAX-0x388||!expected.block||
  expected.block>UINTPTR_MAX-0x410||expected.id>1)return false;
 std::uintptr_t value=0;std::uint32_t count=0,base=0;
 if(!Read(r,image+0x2a82c30,value)||value!=expected.player||!Read(r,expected.player,value)||value!=image+0x2295490||
  !Read(r,expected.block,value)||value!=image+0x2295210||!Read(r,expected.block+0x138,value)||value!=expected.player||
  !Read(r,expected.block+8,count)||!count||count>4||!Read(r,expected.block+0xc,base)||base>expected.id||expected.id-base>=count)return false;
 m.player=expected.player;m.block=expected.block;m.id=expected.id;
 if(!Read(r,m.block+0x78,m.state)||!m.state||m.state>UINTPTR_MAX-0x410||!Read(r,m.state,value)||(value!=image+0x23b09f0&&value!=image+0x23b0c70)||
  !Read(r,m.state+0x408,value)||value!=m.block||!Read(r,m.block+0x10,m.cs)||!m.cs||m.cs>UINTPTR_MAX-0x18||
  !Read(r,m.state+0x238,value)||value!=m.cs||!Read(r,m.cs,value)||value!=image+0x22e6070||
  !Read(r,m.cs+8,count)||!count||count>4||!Read(r,m.cs+0xc,base)||base>m.id||m.id-base>=count||
  !Read(r,m.cs+0x10,value)||!value||(value&7u)||value>UINTPTR_MAX-0xc0*count)return false;
 m.entry=value+std::uintptr_t(m.id-base)*0xc0;return true;
}
struct Snapshot {Model body[2]{};bool typed[2]{},same[2]{};std::uint32_t roles[2]{},objectTypeRva=0,slot=UINT32_MAX,slotFunctionRva=0,count=UINT32_MAX,base=UINT32_MAX;bool slotMatched=false,rangeKnown=false;};
inline bool Same(const Snapshot& a,const Snapshot& b)noexcept {
 return Same(a.body[0],b.body[0])&&Same(a.body[1],b.body[1])&&a.typed[0]==b.typed[0]&&a.typed[1]==b.typed[1]&&a.same[0]==b.same[0]&&a.same[1]==b.same[1]&&
 a.roles[0]==b.roles[0]&&a.roles[1]==b.roles[1]&&a.objectTypeRva==b.objectTypeRva&&a.slot==b.slot&&a.slotFunctionRva==b.slotFunctionRva&&a.slotMatched==b.slotMatched&&a.count==b.count&&a.base==b.base&&a.rangeKnown==b.rangeKnown;
}
template<class R>bool Once(R& r,std::uintptr_t image,std::uintptr_t object,std::uint32_t hook,const Model (&expected)[2],Snapshot& s)noexcept {
 s={};if(!image||image>UINTPTR_MAX-0xa01d000||!object)return false;
 for(unsigned id=0;id<2;++id){s.typed[id]=Current(r,image,expected[id],s.body[id]);if(s.typed[id]){s.same[id]=Same(expected[id],s.body[id]);s.roles[id]=Roles(object,s.body[id]);}}
 std::uintptr_t vt=0,fn=0;if(Read(r,object,vt))s.objectTypeRva=Rva(image,vt);
 if(s.objectTypeRva==0x2295210&&object<=UINTPTR_MAX-0x10&&Read(r,object+8,s.count)&&Read(r,object+0xc,s.base))s.rangeKnown=true;
 if(hook==0xaf0140&&s.objectTypeRva==0x22e6070)s.slot=0x548;
 else if(hook==0x9cc850&&s.objectTypeRva==0x2295210)s.slot=0xf8;
 else if((hook==0x9bb7a0||hook==0x9c2f70)&&s.objectTypeRva==0x22953d0)s.slot=hook==0x9bb7a0?0xb8:8;
 if(s.slot!=UINT32_MAX&&vt<=UINTPTR_MAX-s.slot&&Read(r,vt+s.slot,fn)){
  s.slotFunctionRva=Rva(image,fn);s.slotMatched=s.slotFunctionRva==(hook==0x9c2f70?0x9c2f60:hook);
 }return true;
}
template<class R>bool Pair(R& r,std::uintptr_t image,std::uintptr_t object,std::uint32_t hook,const Model (&expected)[2],Snapshot& s)noexcept {
 s={};Snapshot a{},b{};if(!Once(r,image,object,hook,expected,a)||!Once(r,image,object,hook,expected,b)||!Same(a,b))return false;s=b;return true;
}
// A diagnostic result never grants permission or changes a held-lifetime guard.
}
