#pragma once
#include "vehicle_controller_read.hpp"
namespace mgscoop::vehicle_pose_core_read {
using namespace vehicle_controller_read;
struct CoreScope {std::uintptr_t image=0,player=0,data=0,state=0;std::uint32_t selected=0;};
template<class Reader> bool ReadOnce(Reader& r,const CoreScope& s,Observation& out,const char** failure=nullptr) noexcept {
 out={};if(failure)*failure=nullptr;
 const auto fail=[failure](const char* why) noexcept {if(failure)*failure=why;return false;};
 if(!Range(s.image,0xa01d000)||!s.player||!s.data||!s.state||s.selected>1)return fail("core_identity");out.player=s.player;out.data=s.data;out.state=s.state;
 std::uintptr_t v=0,q=0,st=0,sync=0;std::uint32_t n=0,realized=0,local=0,selected=0;
 if(!Get(r,s.player,v)||v!=s.image+0x2295490||!Get(r,s.player+0x60,q)||q!=s.data||!Get(r,q,v)||v!=s.image+0x23ad970||!Get(r,q+0x1e8,st)||st!=s.state||!Get(r,st+0x40,n)||n!=2)return fail("player_identity");
 if(!Get(r,s.player+0xa8,sync)||sync||!Get(r,s.player+0x204,out.flags)||!(out.flags&1u)||(out.flags&0x100u)||!Get(r,s.player+0x208,n)||n!=2||!Get(r,s.player+0x20c,realized)||realized!=2||!Get(r,s.player+0x214,local)||local||!Get(r,s.player+0x218,selected)||selected!=s.selected)return fail("player_provider_flags");
 if(!Get(r,s.player+0x70,out.ids)||!Aligned(out.ids,2)||!Get(r,s.player+0x78,out.pool)||!out.pool||!Get(r,s.player+0x90,out.parts)||!out.parts||!Get(r,s.player+0x378,out.blocks)||!Aligned(out.blocks,8)||!Get(r,s.player+0x50,out.blockTable)||!Aligned(out.blockTable,8)||!Range(out.ids,4)||!Range(out.pool,2)||!Range(out.parts,2)||!Range(out.blocks,16*0x6a8)||!Range(out.blockTable,16*8))return fail("body_metadata");
 for(unsigned k=0;k<2;++k){auto& b=out.body[k];std::uint16_t id=0;
  if(!Get(r,out.ids+k*2,id)||id!=k||!Get(r,out.pool+k,b.pool)||b.pool>=16||!Get(r,out.parts+k,b.part)||b.part!=3)return fail(k?"body1_metadata":"body0_metadata");
  b.id=id;b.block=out.blocks+std::uintptr_t(b.pool)*0x6a8;
  std::uintptr_t other=0,owner=0;std::uint32_t one=0,base=0,row=0;
  if(!Get(r,out.blockTable+b.pool*8,other)||other!=b.block||!Get(r,b.block,v)||v!=s.image+0x2295210||!Get(r,b.block+0x138,owner)||owner!=s.player||!Get(r,b.block+8,one)||one!=1||!Get(r,b.block+0xc,base)||base!=id||!Get(r,b.block+0x128,b.primary)||!Aligned(b.primary,8)||!Range(b.primary,0x88))return fail(k?"body1_block_owner":"body0_block_owner");
  if(!Get(r,b.primary,v)||v!=s.image+0x23cbea8||!Get(r,b.primary+0x10,v)||v!=s.image+0x23cbee0||!Get(r,b.primary+0x30,v)||v!=s.image+0x23cbf10||!Get(r,b.primary+0x48,v)||v!=s.image+0x23cbf50||!Get(r,b.primary+8,owner)||owner!=b.block)return fail(k?"body1_controller_owner":"body0_controller_owner");
  if(!Get(r,b.primary+0x38,b.node)||!Aligned(b.node,8)||!Range(b.node,0x40)||!Get(r,b.node,v)||v!=s.image+0x2306628||!Get(r,b.node+0x20,one)||one!=1||!Get(r,b.node+0x24,base)||base!=id||!Get(r,b.node+0x2c,row)||row!=0x23||!Get(r,b.node+0x38,owner)||owner!=b.primary+0x30)return fail(k?"body1_node_owner":"body0_node_owner");
  std::uintptr_t allocation=0,used=0;std::uint16_t alignment=0,allocationFlags=0;std::uint32_t tag=0;
  if(!Get(r,b.primary+0x50,b.storage)||!Aligned(b.storage,16)||!Range(b.storage,0x270)||!Get(r,b.primary+0x58,b.header)||b.header!=b.storage||!Get(r,b.primary+0x60,allocation)||allocation!=0x270||!Get(r,b.primary+0x68,used)||used!=allocation||!Get(r,b.primary+0x70,alignment)||alignment!=16||!Get(r,b.primary+0x72,allocationFlags)||!(allocationFlags&1u)||!Get(r,b.primary+0x74,tag)||tag!=0x1c0048||!Get(r,b.primary+0x78,b.record)||b.record!=b.storage+0x30)return fail(k?"body1_allocation_holder":"body0_allocation_holder");
  std::uintptr_t stride=0,count=0;
  if(!Get(r,b.header,v)||v!=s.image+0x645940||!Get(r,b.header+0x10,other)||other!=b.record||!Get(r,b.header+0x18,stride)||stride!=0x240||!Get(r,b.header+0x20,count)||count!=1)return fail(k?"body1_allocation_header":"body0_allocation_header");
  if(!Get(r,b.node+8,b.bitmap)||!Aligned(b.bitmap,4)||!Get(r,b.bitmap,b.bitmapValue))return fail(k?"body1_active_bitmap":"body0_active_bitmap");
  // Native member-pointer dispatch uses signed32+48; the upper32 at+4C
  // is copied from unspecified stack padding. Never read or compare it.
  if(!Range(b.record+0x40,sizeof(b.raw))||!r(b.record+0x40,b.raw,0x0c)||!r(b.record+0x50,b.raw+0x10,0x38))return fail(k?"body1_record_read":"body0_record_read");
  const unsigned phase=b.raw[0x14],next=b.raw[0x15];const auto callback=Pointer(b.raw);const auto adjust=Adjustment(b.raw+8);
  if(phase>7||next>7||adjust||((callback!=s.image+vehicle_controller_contract::phase_callbacks[phase])&&!(phase==0&&callback==0)))return fail(k?"body1_phase_callback":"body0_phase_callback");
  if(!VehicleWord(Word(b.raw+0x44))||!VehicleWord(Word(b.raw+0x46)))return fail(k?"body1_target_class":"body0_target_class");
 }
 const auto& a=out.body[0];const auto& b=out.body[1];
 if(a.pool==b.pool||a.primary==b.primary||a.node==b.node||a.storage==b.storage||a.record==b.record||a.bitmap==b.bitmap)return fail("body_alias");return true;
}
}
