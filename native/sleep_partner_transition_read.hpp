#pragma once
#include "sleep_partner_owned_read.hpp"
namespace mgscoop::sleep_partner_transition_read {
namespace ro=sleep_partner_owned_read;
struct Observation {
 std::uintptr_t player=0,entry=0,ui=0,cam=0,pad=0,source=0,selector=0,request=0,records=0;
 std::uint32_t buttons=0,computedButtons=0,hint=0,hintFlags=0,camFlags=0,requestStride=0,requestLayers=0;
 std::uint8_t hintPending=0,hintPriority=0,parameterCount=0;
};
// Dedicated real odd-body1 supplemental reader. No local0 named mask, even
// sequence projection, native calls or scalar writes. All object layouts are
// the existing complete transition contract; the owned PP shadow type comes
// exclusively from the actual successful Pre receipt.
template<class Reader> bool ReadOnce(Reader& r,const ro::Scope& s,Observation& out,const char** refusal=nullptr) noexcept {
 out={};out.player=s.player;out.entry=s.modelEntry;const auto stage=[&](const char* why) noexcept {if(refusal)*refusal=why;};
 stage("partner_transition_scope");if(ro::ScopeRefusal(s))return false;std::uintptr_t vt=0,owner=0;
 stage("partner_transition_ui");
 if(!ro::Get(r,s.player+0xd8,out.ui)||!ro::Aligned(out.ui,8)||out.ui<0x20||!ro::Range(out.ui,0x3f4)||!ro::Get(r,out.ui,vt)||vt!=s.image+0x23af700||!ro::Get(r,out.ui-0x20,vt)||vt!=s.image+0x23af6d0||!ro::Get(r,out.ui+0x28,owner)||owner!=s.player||!ro::Get(r,out.ui+0x3c0,out.hint)||!ro::Get(r,out.ui+0x3c4,out.hintFlags)||!ro::Get(r,out.ui+0x3f0,out.hintPending)||!ro::Get(r,out.ui+0x3f3,out.hintPriority))return false;
 stage("partner_transition_camera");
 if(!ro::Get(r,s.player+0xb8,out.cam)||!ro::Aligned(out.cam,8)||out.cam<0x20||!ro::Range(out.cam,0x350)||!ro::Get(r,out.cam,vt)||vt!=s.image+0x23b8250||!ro::Get(r,out.cam-0x20,vt)||vt!=s.image+0x23b8218||!ro::Get(r,out.cam+0x348,owner)||owner!=s.player||!ro::Get(r,out.cam+0x32c,out.camFlags))return false;
 stage("partner_transition_pad");
 if(!ro::Get(r,s.player+0xb0,out.pad)||out.pad!=s.pp||!ro::Get(r,out.pad,vt)||vt!=s.windowPPVT||!ro::Get(r,out.pad+0x280,owner)||owner!=s.player||!ro::Get(r,out.pad+8,out.source)||out.source!=s.source||!ro::Get(r,out.source,vt)||vt!=s.image+0x21534e0||!ro::Get(r,out.pad+0x28,out.buttons))return false;
 std::uint32_t held=0,accumulated=0;if(!ro::Get(r,out.source+0x48,held)||!ro::Get(r,out.pad+0x460,accumulated))return false;out.computedButtons=(held|accumulated)&~out.buttons;
 stage("partner_transition_selector");
 if(!ro::Get(r,s.player+0xd0,out.selector)||!ro::Aligned(out.selector,8)||!ro::Range(out.selector,0x3ea0)||!ro::Get(r,out.selector,vt)||vt!=s.image+0x22e8f00||!ro::Get(r,out.selector+0x10,owner)||owner!=s.player)return false;
 stage("partner_transition_requests");
 if(!ro::Get(r,s.modelEntry+0xb0,out.requestLayers)||!out.requestLayers||out.requestLayers>32||!ro::Get(r,s.modelEntry+0x98,out.request)||!ro::Aligned(out.request,8)||!ro::Range(out.request,0x18)||!ro::Get(r,out.request+8,out.parameterCount)||!ro::Get(r,out.request+0xc,out.requestStride)||out.requestStride!=0x28u+24u*out.parameterCount||!ro::Get(r,out.request+0x10,out.records)||!ro::Aligned(out.records,8)||!ro::Range(out.records,std::size_t(out.requestStride)*out.requestLayers))return false;
 unsigned char bytes[0x1810]{};if(out.requestStride>sizeof(bytes)||!r(out.records,bytes,out.requestStride))return false;stage("partner_transition_ready");return true;
}
inline bool SameOwner(const Observation& a,const Observation& b) noexcept {
 return a.player==b.player&&a.entry==b.entry&&a.ui==b.ui&&a.cam==b.cam&&a.pad==b.pad&&a.source==b.source&&a.selector==b.selector&&a.request==b.request&&a.records==b.records&&a.parameterCount==b.parameterCount&&a.requestStride==b.requestStride&&a.requestLayers==b.requestLayers;
}
inline bool Same(const Observation& a,const Observation& b) noexcept {
 return SameOwner(a,b)&&a.buttons==b.buttons&&a.computedButtons==b.computedButtons&&a.hint==b.hint&&a.hintFlags==b.hintFlags&&a.hintPending==b.hintPending&&a.hintPriority==b.hintPriority&&a.camFlags==b.camFlags;
}
}
