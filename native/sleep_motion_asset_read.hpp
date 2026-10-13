#pragma once
// Pure, bounded selected-rest diagnostic. No engine calls or writes. Raw
// GANI bytes remain in local memory and are never emitted or stored by this API.
#include <cmath>
#include <cstring>
#include "sleep_graph_read.hpp"
#include "sleep_motion_asset_contract.hpp"
namespace mgscoop::sleep_motion_asset_read {
namespace sg=sleep_graph_read;namespace sp=sleep_plugin_read;
enum class Origin : unsigned {none,tpp,mgo_reconstruction};
struct Pin {
 std::uint64_t graph,motion;std::uint32_t index;
 unsigned char originalHeader[32],patchedHeader[32];
 std::size_t originalSize,patchedSize;const char* originalSha;const char* patchedSha;
};
inline constexpr Pin pins[]={
 {0xd7654fb0a3f2ull,0xfc53f6ab0bc9caafull,1273,{0xaf,0xca,0xc9,0x0b,0xab,0xf6,0x53,0xfc,0xf0,0x76,0x20,0x00,0x5b,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x69,0x8c,0x00,0x00,0x00,0x00,0x00},{0xaf,0xca,0xc9,0x0b,0xab,0xf6,0x53,0xfc,0xa0,0x14,0x92,0x00,0x5c,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x60,0x2a,0x92,0x00,0x00,0x00,0x00,0x00},5552,5568,"7831afea75c877fbd05bb398daf35f7bf221f010ad64124d03b15f4665f3f93a","45960124fee9670eca5e74af41833467d4b43d0412bd67f0b0d3873156dbdec5"},
 {0xc31867300b57ull,0xfc500f8dffc6c8d6ull,13,{0xd6,0xc8,0xc6,0xff,0x8d,0x0f,0x50,0xfc,0xf0,0xcc,0x21,0x00,0x6f,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x7d,0x8c,0x00,0x00,0x00,0x00,0x00},{0xd6,0xc8,0xc6,0xff,0x8d,0x0f,0x50,0xfc,0xa0,0x2a,0x92,0x00,0x75,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0x41,0x92,0x00,0x00,0x00,0x00,0x00},5872,5968,"bb2c595664dfdc3aaa5af3b8dd28ca9821245f82ede069cbc03df53d7f2c4dcb","acbe510d5aec8455ed01d6cc704b1970cca69faddaaf0c387b76974712b3f67b"}
};
inline constexpr std::size_t kBlobCeiling=0x1000000,kMainCeiling=8192,kChildrenCeiling=64;
struct Asset {
 std::uintptr_t owner=0,engine=0,data=0,player=0,node=0,child=0,source=0,bank=0,blob=0,main=0;
 std::uint32_t slot=0,current=0,childIndex=0,index=0,offset=0;std::size_t size=0;
 std::uint64_t motion=0;Origin origin=Origin::none;
 char sha256[65]{};
};
struct Reply {bool ready=false,selected=false;Asset body[2];const char* reason="disabled";};
struct Scope {sp::Scope owner;bool codeGate=false,privateCopiedSaveLab=false;};
inline const Pin* ForGraph(std::uint64_t hash) noexcept {for(const auto& p:pins)if(p.graph==hash)return &p;return nullptr;}
template<class Reader> bool CodeGate(Reader& r,std::uintptr_t image,std::size_t span,std::uintptr_t& mismatch) noexcept {
 mismatch=0;unsigned char bytes[1024]{};
 for(const auto& f:sleep_motion_asset_contract::sites){
  if(f.rva>span||f.size>span-f.rva||f.size>sizeof(bytes)||!sp::Range(image+f.rva,f.size)||!r(image+f.rva,bytes,f.size)){mismatch=f.rva;return false;}
  if(std::memcmp(bytes,f.bytes,f.size)){mismatch=f.rva;return false;}
 }
 std::uintptr_t target=0;
 if(!sp::Get(r,image+0x24c2890+0x48,target)||target!=image+0x1a75340){mismatch=0x24c28d8;return false;}
 return true;
}
// Caller supplies a bounded cryptographic SHA256(buffer,count,char[65]) -> bool.
// Do not replace it with a pointer/address or graph-ID acceptance check.
template<class Reader,class Hasher> bool FromGraph(Reader& r,Hasher& hash,std::uintptr_t image,const sg::Graph& g,Asset& out,const char** refusal=nullptr) noexcept {
 out={};const auto why=[&](const char* s) noexcept {if(refusal)*refusal=s;};
 if(!g.currentNode)return true;const auto* pin=ForGraph(g.stateHash);if(!pin)return true;
 Asset base{};std::uint16_t maximumSlot=0;std::uintptr_t vt=0;
 why("asset_engine");
 // Complete getter 1C60D40 plus caller 1C73BE0: controller+18->+10,
 // engine+10; +4E is the inclusive bound used by 1C73BE0, F8 the pointers.
 if(!sp::Get(r,g.controller+0x18,base.owner)||!sp::Aligned(base.owner,8)||
    !sp::Get(r,base.owner+0x10,base.engine)||!sp::Aligned(base.engine,8)||
    !sp::Get(r,base.engine+0x10,base.data)||!sp::Aligned(base.data,8)||
    !sp::Get(r,g.controller+0xd0,base.slot)||!sp::Get(r,base.data+0x4e,maximumSlot)||maximumSlot>64||base.slot>maximumSlot||
    !sp::Get(r,base.data+0xf8+8u*base.slot,base.player)||!sp::Aligned(base.player,8)||
    !sp::Get(r,base.player+0x48,base.current)||base.current>1||
    !sp::Get(r,base.player+std::uintptr_t(base.current)*0x20,base.node)||!sp::Aligned(base.node,8))return false;
 std::uint16_t children=0;std::uint32_t weightsOffset=0;
 why("asset_current_node");
 // AF0A20 and 1A7F770, including their secondary branch returns, use
 // node+4 (base-relative weight table), +C (count), +30 (child offsets).
 if(!sp::Get(r,base.node+0xc,children)||!children||children>kChildrenCeiling||
    !sp::Get(r,base.node+4,weightsOffset)||!weightsOffset||weightsOffset>0x100000||
    !sp::Range(base.node,weightsOffset+std::size_t(children)*sizeof(float)))return false;
 unsigned selected=0;
 for(unsigned childIndex=0;childIndex<children;++childIndex){
  float weight=0;if(!sp::Get(r,base.node+weightsOffset+sizeof(float)*childIndex,weight)||!std::isfinite(weight)||weight<0||weight>1)return false;
  if(weight==0)continue;
  Asset a=base;a.childIndex=childIndex;std::uint32_t childOffset=0;
  if(!sp::Get(r,a.node+0x30+sizeof(std::uint32_t)*childIndex,childOffset)||!childOffset||childOffset>0x100000||
     !sp::Range(a.node,childOffset+0x90))return false;a.child=a.node+childOffset;
  if(!sp::Get(r,a.child+0x80,a.source)||!sp::Aligned(a.source,8))return false;
  std::uint16_t kind=0,frames=0;
  if(!sp::Get(r,a.source,a.motion)||a.motion!=pin->motion)continue;
  why("asset_source_owner");
  std::uintptr_t array=0,loaded=0;std::uint32_t count=0;std::uint8_t loadKind=0;
  if(!sp::Get(r,a.source+0x20,kind)||(kind&15u)!=3||!sp::Get(r,a.source+0x22,frames)||
     !sp::Get(r,a.source+8,a.bank)||!sp::Aligned(a.bank,8)||!sp::Get(r,a.bank,vt)||vt!=image+0x24c2890||
     !sp::Get(r,a.bank+0x40,loadKind)||loadKind!=2||!sp::Get(r,a.bank+0x84,count)||count!=1285||
     !sp::Get(r,a.bank+0x88,array)||!sp::Aligned(array,8)||!sp::Get(r,a.source+0x24,a.index)||a.index!=pin->index||
     a.source!=array+std::uintptr_t(a.index)*0x28||!sp::Get(r,a.bank+0x98,a.blob)||!sp::Aligned(a.blob,16)||
     !sp::Get(r,a.bank+0x60,loaded)||loaded!=a.blob)return false;
  std::uint32_t signature=0,fileCount=0;std::uint16_t flags=0;
  if(!sp::Get(r,a.blob,signature)||signature!=0x0c012b72||!sp::Get(r,a.blob+4,fileCount)||fileCount!=count||
     !sp::Get(r,a.blob+0x12,flags)||!(flags&0x1000u))return false;
  unsigned char header[32]{};
  if(!r(a.blob+0x20+std::uintptr_t(a.index)*0x20,header,sizeof(header)))return false;
  const bool original=!std::memcmp(header,pin->originalHeader,sizeof(header)),patched=!std::memcmp(header,pin->patchedHeader,sizeof(header));
  if(!original&&!patched){why("asset_unknown_header");return false;}
  std::memcpy(&a.offset,header+8,4);a.size=original?pin->originalSize:pin->patchedSize;
  if(!a.offset||a.offset>kBlobCeiling||!a.size||a.size>kMainCeiling||a.size>kBlobCeiling-a.offset||!sp::Range(a.blob,a.offset+a.size))return false;
  a.main=a.blob+a.offset;unsigned char bytes[kMainCeiling]{};std::uint32_t actualFrames=0;
  if(!r(a.main,bytes,a.size)||!hash(bytes,a.size,a.sha256)||a.sha256[64])return false;
  std::memcpy(&actualFrames,bytes,4);if(actualFrames!=frames)return false;
  if(std::strcmp(a.sha256,original?pin->originalSha:pin->patchedSha)){why("asset_payload_hash_changed");return false;}
  a.origin=original?Origin::tpp:Origin::mgo_reconstruction;
  if(++selected>1){why("asset_multiple_rest_sources");return false;}out=a;
 }
 if(!selected)why("asset_no_selected_rest_source");
 return true;
}
inline bool Same(const Asset& a,const Asset& b) noexcept {
 return a.owner==b.owner&&a.engine==b.engine&&a.data==b.data&&a.player==b.player&&a.node==b.node&&a.child==b.child&&
  a.source==b.source&&a.bank==b.bank&&a.blob==b.blob&&a.main==b.main&&a.slot==b.slot&&a.current==b.current&&
  a.childIndex==b.childIndex&&a.index==b.index&&a.offset==b.offset&&a.size==b.size&&a.motion==b.motion&&a.origin==b.origin&&!std::strcmp(a.sha256,b.sha256);
}
template<class Reader,class Hasher> bool ReadPair(Reader& r,Hasher& hash,const Scope& s,Reply& reply) noexcept {
 #if !defined(MGS_BACKGROUND_WORKER)
 (void)r;(void)hash;(void)s;reply={};reply.reason="asset_worker_disabled";return false;
 #else
 reply={};if(!s.codeGate||!s.privateCopiedSaveLab){reply.reason="asset_scope";return false;}
 if(const auto* why=sp::ScopeRefusal(s.owner)){reply.reason=why;return false;}
 sg::Observation a{},b{};Asset first[2]{},second[2]{};const char* why="asset_graph";
 if(!sg::ReadOnce(r,s.owner,a,&why)){reply.reason=why;return false;}
 for(unsigned k=0;k<2;++k)if(!FromGraph(r,hash,s.owner.image,a.graph[k],first[k],&why)){reply.reason=why;return false;}
 if(!sg::ReadOnce(r,s.owner,b,&why)||!sg::Same(a,b)){reply.reason="asset_graph_changed";return false;}
 for(unsigned k=0;k<2;++k)if(!FromGraph(r,hash,s.owner.image,b.graph[k],second[k],&why)||!Same(first[k],second[k])){reply.reason="asset_source_changed";return false;}
 for(unsigned k=0;k<2;++k){reply.body[k]=second[k];reply.selected=reply.selected||second[k].origin!=Origin::none;}
 reply.ready=true;reply.reason=reply.selected?"asset_selected_payload_verified":"asset_no_selected_rest_source";return true;
 #endif
}
} // namespace mgscoop::sleep_motion_asset_read
