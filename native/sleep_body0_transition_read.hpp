#pragma once
#include "sleep_plugin_read.hpp"
namespace mgscoop::sleep_body0_transition_read {
namespace plugin=sleep_plugin_read;
struct Observation {
 std::uintptr_t player=0,entry=0,ui=0,cam=0,pad=0,source=0,selector=0,request=0,records=0;
 std::uint32_t aggregateButtons=0,hint=0,hintFlags=0,camFlags=0,requestStride=0,requestLayers=0;
 std::uint8_t hintPending=0,hintPriority=0,parameterCount=0;
};
// Supplemental pure typed-object checks only. Caller must retain its exact PE,
// plugin/graph/seed/transition gates and worker copied-save owner lease, freshly
// verify the complete named mask/zero-cap receipt, and compare graph/owner
// observations before and after this read. No engine calls or memory writes.
// Existing-owned retirement may use this reader under its retained owner lease;
// this function itself grants no admission and cannot authorize a scalar write.
template<class Reader> bool ReadLocalOwnedOnce(Reader& r,std::uintptr_t image,std::uintptr_t player,std::uintptr_t entry,Observation& out,const char** refusal=nullptr,bool requireMask=true) noexcept {
 out={};out.player=player;out.entry=entry;const auto stage=[&](const char* why) noexcept {if(refusal)*refusal=why;};
 std::uintptr_t vt=0,owner=0;stage("transition_owner");
 if(!plugin::Aligned(image,0x1000)||!plugin::Aligned(player,8)||!plugin::Range(player,0xe0)||!plugin::Aligned(entry,16)||!plugin::Range(entry,0xc0))return false;
 stage("transition_ui");
 if(!plugin::Get(r,player+0xd8,out.ui)||!plugin::Aligned(out.ui,8)||out.ui<0x20||!plugin::Range(out.ui,0x3f4)||!plugin::Get(r,out.ui,vt)||vt!=image+0x23af700||!plugin::Get(r,out.ui-0x20,vt)||vt!=image+0x23af6d0||!plugin::Get(r,out.ui+0x28,owner)||owner!=player||!plugin::Get(r,out.ui+0x3c0,out.hint)||!plugin::Get(r,out.ui+0x3c4,out.hintFlags)||!plugin::Get(r,out.ui+0x3f0,out.hintPending)||!plugin::Get(r,out.ui+0x3f3,out.hintPriority))return false;
 stage("transition_camera");
 if(!plugin::Get(r,player+0xb8,out.cam)||!plugin::Aligned(out.cam,8)||out.cam<0x20||!plugin::Range(out.cam,0x350)||!plugin::Get(r,out.cam,vt)||vt!=image+0x23b8250||!plugin::Get(r,out.cam-0x20,vt)||vt!=image+0x23b8218||!plugin::Get(r,out.cam+0x348,owner)||owner!=player||!plugin::Get(r,out.cam+0x32c,out.camFlags))return false;
 stage("transition_pad");
 if(!plugin::Get(r,player+0xb0,out.pad)||!plugin::Aligned(out.pad,8)||!plugin::Range(out.pad,0x464)||!plugin::Get(r,out.pad,vt)||vt!=image+0x23c8d40||!plugin::Get(r,out.pad+0x280,owner)||owner!=player||!plugin::Get(r,out.pad+8,out.source)||!plugin::Aligned(out.source,8)||!plugin::Range(out.source,0x4c)||!plugin::Get(r,out.pad+0x28,out.aggregateButtons))return false;
 // PP vt20 computes (source+48 or PP+460) & ~PP+28. The named mask
 // record alone is insufficient until its aggregate has been applied.
 std::uint32_t sourceBits=0,derivedBits=0;if(!plugin::Get(r,out.source+0x48,sourceBits)||!plugin::Get(r,out.pad+0x460,derivedBits)||(requireMask&&out.aggregateButtons!=0xffffffffu))return false;
 stage("transition_selector");
 if(!plugin::Get(r,player+0xd0,out.selector)||!plugin::Aligned(out.selector,8)||!plugin::Range(out.selector,0x3ea0)||!plugin::Get(r,out.selector,vt)||vt!=image+0x22e8f00||!plugin::Get(r,out.selector+0x10,owner)||owner!=player)return false;
 stage("transition_requests");
 if(!plugin::Get(r,entry+0xb0,out.requestLayers)||!out.requestLayers||out.requestLayers>32||!plugin::Get(r,entry+0x98,out.request)||!plugin::Aligned(out.request,8)||!plugin::Range(out.request,0x18)||!plugin::Get(r,out.request+8,out.parameterCount)||!plugin::Get(r,out.request+0xc,out.requestStride)||out.requestStride!=0x28u+24u*out.parameterCount||!plugin::Get(r,out.request+0x10,out.records)||!plugin::Aligned(out.records,8)||!plugin::Range(out.records,std::size_t(out.requestStride)*out.requestLayers))return false;
 // Only native layer0 is requested. Its entire constructor-sized record is
 // read, including all three optional parameter tables, without assuming a
 // vtable for this plain owned request buffer or exposing its address to Lua.
 unsigned char bytes[0x1810]{};if(out.requestStride>sizeof(bytes)||!r(out.records,bytes,out.requestStride))return false;
 stage("transition_ready");return true;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 return a.player==b.player&&a.entry==b.entry&&a.ui==b.ui&&a.cam==b.cam&&a.pad==b.pad&&a.source==b.source&&a.selector==b.selector&&a.request==b.request&&a.records==b.records&&a.aggregateButtons==b.aggregateButtons&&a.parameterCount==b.parameterCount&&a.requestStride==b.requestStride&&a.requestLayers==b.requestLayers;
}
} // namespace mgscoop::sleep_body0_transition_read
