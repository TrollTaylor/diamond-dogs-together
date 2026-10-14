#pragma once
// Exact private cycle-bank and selected-payload diagnostics. No native calls,
// graph requests, lookup choice, actor authority or file/cache mutation.
#include <cmath>
#include <cstring>
#include "sleep_graph_read.hpp"
#include "sleep_motion_asset_contract.hpp"
#include "mgscoop/mgo_standing_cycle_catalog.hpp"
namespace mgscoop::sleep_standing_cycle_asset {
namespace sp=sleep_plugin_read;namespace sg=sleep_graph_read;
inline constexpr std::size_t kBytes=13824,kMainCeiling=32768,kEndCeiling=2048,kBlobCeiling=0x1000000;
inline constexpr char kSha[]="4da3379333b2043f63e2e192de5a5845bbacf3fa0486796042c52a54b1abc25e";
inline constexpr char kPath[]="/Assets/tpp/motion/mtar/player2/mgs_coop_upright_sleep_cycle_v1.mtar";
inline constexpr unsigned char header[32]={0x72,0x2b,0x01,0x0c,0x04,0x00,0x00,0x00,0x12,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x00,0x10,0xa0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
struct Pin {std::uint64_t motion;unsigned index,stockIndex,frames;unsigned char stockHeader[32],cycleHeader[32];
 std::size_t stockMainBytes,mainBytes,endBytes;const char* stockMainSha;const char* mainSha;const char* endSha;};
inline constexpr Pin pins[]={
 {0xfc53714f24c47b07ull,0,1092,100,{0x07,0x7b,0xc4,0x24,0x4f,0x71,0x53,0xfc,0x80,0x7e,0x2b,0x00,0xb9,0x02,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x70,0xe0,0x8c,0x00,0x00,0x00,0x00,0x00},{0x07,0x7b,0xc4,0x24,0x4f,0x71,0x53,0xfc,0x30,0x04,0x00,0x00,0x97,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa0,0x0d,0x00,0x00,0x00,0x00,0x00,0x00},11152,2416,496,"fc0752e35850f930c8ec7ca01516884bd3de82c17e2dc01af831631a6e25bba6","258db3bb20a7a8b70b256315e395e54781b5d643eda95979ac295217a65d1c70","ba29abdc75b40fac416da5728696001b62e82bbbf0849804f721e52e0cb7539f"},
 {0xfc537fe811d39a38ull,1,1111,20,{0x38,0x9a,0xd3,0x11,0xe8,0x7f,0x53,0xfc,0x40,0x30,0x2c,0x00,0x9e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xe0,0xec,0x8c,0x00,0x00,0x00,0x00,0x00},{0x38,0x9a,0xd3,0x11,0xe8,0x7f,0x53,0xfc,0x90,0x0f,0x00,0x00,0xa3,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0x19,0x00,0x00,0x00,0x00,0x00,0x00},2528,2608,448,"6ee95cf17e0b7c3587df003cf1736fa6fa68f577053563fb94fb586dd73213f5","720ac44156fa2a19490422139974418981c6f59208ef7ae01d1f6657ec193f5f","b9e93e4495eaa919bebb70a4736016d14599d76bbefeafb0036dab1df67fe731"},
 {0xfc53f0f44e7c00fdull,2,1262,140,{0xfd,0x00,0x7c,0x4e,0xf4,0xf0,0x53,0xfc,0x20,0x3a,0x2c,0x00,0xe8,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xa0,0xee,0x8c,0x00,0x00,0x00,0x00,0x00},{0xfd,0x00,0x7c,0x4e,0xf4,0xf0,0x53,0xfc,0x80,0x1b,0x00,0x00,0xde,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x29,0x00,0x00,0x00,0x00,0x00,0x00},20096,3552,576,"bd4917665b1fe8e2d08cb8f16de5ac3433f2a0e8f71e512a3ef3184ff426e3ab","cd299d5405e8e43b42bb61e3d3defa7e1b481da2ce86b22beccb06d814a9d932","3440f7e38112837f1b7fbcc7b5f90c98ba18ddf8ee8f8e2b413df283591afd9c"},
 {0xfc53f6ab0bc9caafull,3,1273,420,{0xaf,0xca,0xc9,0x0b,0xab,0xf6,0x53,0xfc,0xf0,0x76,0x20,0x00,0x5b,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x69,0x8c,0x00,0x00,0x00,0x00,0x00},{0xaf,0xca,0xc9,0x0b,0xab,0xf6,0x53,0xfc,0xa0,0x2b,0x00,0x00,0xa2,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0x35,0x00,0x00,0x00,0x00,0x00,0x00},5552,2592,64,"7831afea75c877fbd05bb398daf35f7bf221f010ad64124d03b15f4665f3f93a","71a730c68147a69e872c2be72a877ee93a6be8cd32e0a85ede2a3d77810f90cb","c8b5f9adb0811232238f91648f1c6970852249b49678139debd4c07155a378b8"},
};
struct GraphPin {std::uint64_t hash;unsigned index;};inline constexpr GraphPin graphs[]={
 {0xb1c6aee936a4ull,0},
 {0xb6c742e88a85ull,0},
 {0xeceee0f51e77ull,1},
 {0xf2364261c9d4ull,1},
 {0x40d8fda5ca06ull,2},
 {0xd7654fb0a3f2ull,3},
};
inline const Pin* ForGraph(std::uint64_t hash)noexcept{for(const auto& g:graphs)if(g.hash==hash)return &pins[g.index];return nullptr;}
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;unsigned char bytes[1024]{};
 for(const auto& f:sleep_motion_asset_contract::sites){if(f.rva>span||f.size>span-f.rva||f.size>sizeof(bytes)||!r(image+f.rva,bytes,f.size)||std::memcmp(bytes,f.bytes,f.size)){mismatch=f.rva;return false;}}
 std::uintptr_t target=0;if(!sp::Get(r,image+0x24c2890+0x48,target)||target!=image+0x1a75340){mismatch=0x24c28d8;return false;}return true;
}
template<class Reader,class Hasher> bool VerifyBank(Reader& r,Hasher& hash,std::uintptr_t image,std::uintptr_t bank,std::uintptr_t& blob,std::uintptr_t& sources,std::uint64_t& identity)noexcept {
 blob=sources=0;identity=0;if(!mgo_standing_cycle::Compiled())return false;
 std::uintptr_t vt=0,loaded=0;std::uint32_t count=0;std::uint8_t kind=0;
 if(!sp::Aligned(bank,8)||!sp::Get(r,bank,vt)||vt!=image+0x24c2890||!sp::Get(r,bank+0x40,kind)||kind!=2||
    !sp::Get(r,bank+0x84,count)||count!=4||!sp::Get(r,bank+0x88,sources)||!sp::Aligned(sources,8)||!sp::Range(sources,4*0x28)||
    !sp::Get(r,bank+0x98,blob)||!sp::Aligned(blob,16)||!sp::Get(r,bank+0x60,loaded)||loaded!=blob||
    !sp::Get(r,bank+0x30,identity)||!sp::Range(blob,kBytes))return false;
 unsigned char bytes[kBytes]{};char digest[65]{};
 if(!r(blob,bytes,kBytes)||std::memcmp(bytes,header,32)||!hash(bytes,kBytes,digest)||digest[64]||std::strcmp(digest,kSha))return false;
 for(const auto& p:pins){const auto at=sources+p.index*0x28;std::uintptr_t owner=0;std::uint64_t id=0;std::uint32_t index=0;std::uint16_t sourceKind=0,frames=0;
  if(std::memcmp(bytes+0x20+p.index*32,p.cycleHeader,32)||!sp::Get(r,at,id)||id!=p.motion||!sp::Get(r,at+8,owner)||owner!=bank||
     !sp::Get(r,at+0x20,sourceKind)||(sourceKind&15u)!=3||!sp::Get(r,at+0x22,frames)||frames!=p.frames||!sp::Get(r,at+0x24,index)||index!=p.index)return false;
 }return true;
}
enum class Origin : unsigned {none,stock,standingCandidate=3};
struct Asset {std::uintptr_t owner=0,engine=0,data=0,player=0,node=0,child=0,source=0,bank=0,blob=0,main=0,end=0;
 unsigned slot=0,current=0,childIndex=0,index=0,offset=0,endOffset=0;std::size_t size=0,endSize=0;
 std::uint64_t motion=0;Origin origin=Origin::none;char mainSha[65]{},endSha[65]{};};
template<class Reader> bool BoundBank(Reader& r,std::uintptr_t image,std::uintptr_t engine,std::uintptr_t bank)noexcept {
 std::uintptr_t vt=0,array=0;std::uint32_t capacity=0;
 if(!sp::Get(r,engine,vt)||vt!=image+0x24c1988||!sp::Get(r,engine+0x1c,capacity)||!capacity||capacity>64||
    !sp::Get(r,engine+0x98,array)||!sp::Aligned(array,8)||!sp::Range(array,std::size_t(capacity)*0x30))return false;
 bool found=false;for(unsigned i=0;i<capacity;++i){std::uintptr_t pointer=0;
  if(!sp::Get(r,array+std::uintptr_t(i)*0x30+0x18,pointer))return false;if(pointer==bank)found=true;}
 return found;
}
template<class Reader,class Hasher> bool FromGraph(Reader& r,Hasher& hash,std::uintptr_t image,const sg::Graph& g,Asset& out,const char** refusal=nullptr,bool exactPrivate=false)noexcept {
 out={};const auto why=[&](const char* s)noexcept{if(refusal)*refusal=s;};
 if(!mgo_standing_cycle::Compiled()||!exactPrivate){why("cycle_private_profile");return false;}
 const auto* pin=ForGraph(g.stateHash);if(!g.currentNode||!pin)return true;Asset base{};std::uint16_t maxSlot=0;std::uintptr_t vt=0;
 why("cycle_engine");
 if(!sp::Get(r,g.controller+0x18,base.owner)||!sp::Aligned(base.owner,8)||!sp::Get(r,base.owner+0x10,base.engine)||!sp::Aligned(base.engine,8)||
    !sp::Get(r,base.engine+0x10,base.data)||!sp::Aligned(base.data,8)||!sp::Get(r,g.controller+0xd0,base.slot)||!sp::Get(r,base.data+0x4e,maxSlot)||maxSlot>64||base.slot>maxSlot||
    !sp::Get(r,base.data+0xf8+8u*base.slot,base.player)||!sp::Aligned(base.player,8)||!sp::Get(r,base.player+0x48,base.current)||base.current>1||
    !sp::Get(r,base.player+base.current*0x20,base.node)||!sp::Aligned(base.node,8))return false;
 std::uint16_t children=0;std::uint32_t weights=0;why("cycle_node");
 if(!sp::Get(r,base.node+0xc,children)||!children||children>64||!sp::Get(r,base.node+4,weights)||!weights||weights>0x100000||!sp::Range(base.node,weights+children*sizeof(float)))return false;
 unsigned selected=0;
 for(unsigned child=0;child<children;++child){float weight=0;if(!sp::Get(r,base.node+weights+sizeof(float)*child,weight)||!std::isfinite(weight)||weight<0||weight>1)return false;if(weight==0)continue;
  Asset a=base;a.childIndex=child;std::uint32_t offset=0;
  if(!sp::Get(r,a.node+0x30+sizeof(std::uint32_t)*child,offset)||!offset||offset>0x100000||!sp::Range(a.node,offset+0x90))return false;a.child=a.node+offset;
  if(!sp::Get(r,a.child+0x80,a.source)||!sp::Aligned(a.source,8)||!sp::Get(r,a.source,a.motion))return false;if(a.motion!=pin->motion)continue;
  std::uint16_t kind=0,frames=0;std::uintptr_t sources=0,loaded=0;std::uint32_t count=0;std::uint8_t loadKind=0;why("cycle_source_owner");
  if(!sp::Get(r,a.source+8,a.bank)||!sp::Aligned(a.bank,8)||!sp::Get(r,a.bank,vt)||vt!=image+0x24c2890||!sp::Get(r,a.bank+0x40,loadKind)||loadKind!=2||
     !sp::Get(r,a.bank+0x84,count)||(count!=4&&count!=1285)||!sp::Get(r,a.bank+0x88,sources)||!sp::Aligned(sources,8)||
     !sp::Get(r,a.source+0x24,a.index)||a.index!=(count==4?pin->index:pin->stockIndex)||a.source!=sources+std::uintptr_t(a.index)*0x28||
     !sp::Get(r,a.source+0x20,kind)||(kind&15u)!=3||!sp::Get(r,a.source+0x22,frames)||frames!=pin->frames||
     !sp::Get(r,a.bank+0x98,a.blob)||!sp::Aligned(a.blob,16)||!sp::Get(r,a.bank+0x60,loaded)||loaded!=a.blob||!BoundBank(r,image,a.engine,a.bank))return false;
  if(count==4){std::uintptr_t blob=0,src=0;std::uint64_t identity=0;if(!VerifyBank(r,hash,image,a.bank,blob,src,identity)||blob!=a.blob||src!=sources){why("cycle_bank_hash");return false;}}
  std::uint32_t magic=0,headerCount=0;std::uint16_t flags=0;unsigned char bytesHeader[32]{};
  if(!sp::Get(r,a.blob,magic)||magic!=0x0c012b72||!sp::Get(r,a.blob+4,headerCount)||headerCount!=count||!sp::Get(r,a.blob+0x12,flags)||!(flags&0x1000u)||
     !r(a.blob+0x20+std::uintptr_t(a.index)*32,bytesHeader,32)||std::memcmp(bytesHeader,count==4?pin->cycleHeader:pin->stockHeader,32))return false;
  std::memcpy(&a.offset,bytesHeader+8,4);std::memcpy(&a.endOffset,bytesHeader+0x18,4);a.size=count==4?pin->mainBytes:pin->stockMainBytes;a.endSize=pin->endBytes;
  const auto ceiling=count==4?kBytes:kBlobCeiling;
  if(!a.offset||a.offset>ceiling||a.size>kMainCeiling||a.size>ceiling-a.offset||!a.endOffset||a.endOffset>ceiling||a.endSize>kEndCeiling||a.endSize>ceiling-a.endOffset)return false;
  a.main=a.blob+a.offset;a.end=a.blob+a.endOffset;unsigned char main[kMainCeiling]{},end[kEndCeiling]{};std::uint32_t duration=0;why("cycle_selected_payload");
  if(!r(a.main,main,a.size)||!hash(main,a.size,a.mainSha)||a.mainSha[64]||std::strcmp(a.mainSha,count==4?pin->mainSha:pin->stockMainSha)||
     !r(a.end,end,a.endSize)||!hash(end,a.endSize,a.endSha)||a.endSha[64]||std::strcmp(a.endSha,pin->endSha))return false;
  std::memcpy(&duration,main,4);if(duration!=pin->frames)return false;a.origin=count==4?Origin::standingCandidate:Origin::stock;
  if(++selected>1){why("cycle_multiple_selected_sources");return false;}out=a;
 }return true;
}
inline bool Same(const Asset& a,const Asset& b)noexcept{return a.owner==b.owner&&a.engine==b.engine&&a.data==b.data&&a.player==b.player&&a.node==b.node&&a.child==b.child&&
 a.source==b.source&&a.bank==b.bank&&a.blob==b.blob&&a.main==b.main&&a.end==b.end&&a.slot==b.slot&&a.current==b.current&&a.childIndex==b.childIndex&&
 a.index==b.index&&a.offset==b.offset&&a.endOffset==b.endOffset&&a.size==b.size&&a.endSize==b.endSize&&a.motion==b.motion&&a.origin==b.origin&&!std::strcmp(a.mainSha,b.mainSha)&&!std::strcmp(a.endSha,b.endSha);}
struct Scope {sp::Scope owner;bool codeGate=false,privateCopiedSaveLab=false,exactStandingProfile=false;};
struct Reply {bool ready=false,selected=false;Asset body[2];const char* reason="disabled";};
template<class Reader,class Hasher> bool ReadPair(Reader& r,Hasher& hash,const Scope& s,Reply& out)noexcept {
 out={};if(!mgo_standing_cycle::Compiled()||!s.codeGate||!s.privateCopiedSaveLab||!s.exactStandingProfile){out.reason="cycle_scope";return false;}
 if(const auto* why=sp::ScopeRefusal(s.owner)){out.reason=why;return false;}sg::Observation a{},b{};Asset first[2]{},second[2]{};const char* why="cycle_graph";
 if(!sg::ReadOnce(r,s.owner,a,&why)){out.reason=why;return false;}
 for(unsigned k=0;k<2;++k)if(!FromGraph(r,hash,s.owner.image,a.graph[k],first[k],&why,true)){out.reason=why;return false;}
 if(!sg::ReadOnce(r,s.owner,b,&why)||!sg::Same(a,b)){out.reason="cycle_graph_changed";return false;}
 for(unsigned k=0;k<2;++k)if(!FromGraph(r,hash,s.owner.image,b.graph[k],second[k],&why,true)||!Same(first[k],second[k])){out.reason="cycle_source_changed";return false;}
 for(unsigned k=0;k<2;++k){out.body[k]=second[k];out.selected=out.selected||second[k].origin!=Origin::none;}
 out.ready=true;out.reason=out.selected?"cycle_selected_main_end_verified":"cycle_no_selected_source";return true;
}
} // namespace mgscoop::sleep_standing_cycle_asset
