#pragma once
// Pure read-only schema. Caller must capture/recheck a genuine current owner
// around these reads. No hint grants native input, loading or a writer purpose.
#ifndef MGS_HORSE_FLOW_TEST_HEADER
#define MGS_HORSE_FLOW_TEST_HEADER "horse_cargo_natural_flow.hpp"
#endif
#include MGS_HORSE_FLOW_TEST_HEADER
namespace mgscoop::carry_horse_hint_read {
namespace flow=horse_cargo_natural_flow;
struct Hint {
 std::uint16_t victim=0xffff,near=0xffff,mount=0xffff,flags=0,gate4=0,gate6=0,gate8=0;
 std::uint8_t phase=0,closure=0,next=0;
 std::uint32_t prompt=0,action=0;
};
inline bool Same(const Hint& a,const Hint& b) noexcept {
 return a.victim==b.victim&&a.near==b.near&&a.mount==b.mount&&a.flags==b.flags&&a.gate4==b.gate4&&
  a.gate6==b.gate6&&a.gate8==b.gate8&&a.phase==b.phase&&a.closure==b.closure&&a.next==b.next&&
  a.prompt==b.prompt&&a.action==b.action;
}
template<class Reader>bool Read(Reader& read,const partner_input_hook::NativeHorseFlowOwner& owner,Hint& h) noexcept {
 h={};if(!flow::SameOwner(owner,owner))return false;const auto at=owner.carrierRecord[0];
 return flow::cr::Range(at,0x6c0)&&flow::cr::Get(read,at+0x78,h.victim)&&flow::cr::Get(read,at+0x6a0,h.near)&&
  flow::cr::Get(read,at+0x538,h.mount)&&flow::cr::Get(read,at+0x6a2,h.flags)&&flow::cr::Get(read,at+0x6a4,h.gate4)&&
  flow::cr::Get(read,at+0x6a6,h.gate6)&&flow::cr::Get(read,at+0x6a8,h.gate8)&&flow::cr::Get(read,at+0x81,h.phase)&&
  flow::cr::Get(read,at+0x380,h.closure)&&flow::cr::Get(read,at+0x381,h.next)&&
  flow::cr::Get(read,at+0x6ac,h.prompt)&&flow::cr::Get(read,at+0x6b0,h.action);
}
inline const char* Meaning(const Hint& h) noexcept {
 if(h.phase>=0x17&&h.phase<=0x19)return "stock_load_phase";
 if(h.phase==7)return "stock_unload_phase";
 if(h.near==0xffff||(h.near>>9)!=13)return "no_horse_candidate";
 // 112B7F4 geometry success sets near, then 112B80E gates on bit8000.
 if(!(h.gate6&0x8000u))return "candidate_gate_missing";
 // 112B875 sets prompt7/actionD and explicitly returns false. These are
 // presentation values; no field-only snapshot proves finder/input success.
 if(h.prompt==7&&h.action==0xdu)return "candidate_input_blocked";
 return "candidate_unqualified";
}
inline bool Line(const Hint& h,char* out,std::size_t capacity) noexcept {
 if(!out||!capacity)return false;
 const int n=std::snprintf(out,capacity,
  "horseHint;t=%u;near=%x;mount=%x;p=%u;cl=%u>%u;f=%x;g=%x,%x,%x;ui=%u,%u;why=%s",
  unsigned(h.victim),unsigned(h.near),unsigned(h.mount),unsigned(h.phase),unsigned(h.closure),unsigned(h.next),
  unsigned(h.flags),unsigned(h.gate4),unsigned(h.gate6),unsigned(h.gate8),h.prompt,h.action,Meaning(h));
 return n>=0&&static_cast<std::size_t>(n)<capacity;
}
}
