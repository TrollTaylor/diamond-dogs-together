#pragma once
#include "sleep_parts_policy.hpp"
namespace mgscoop::sleep_parts_diagnostic {
namespace sl=sleep_partner_lease;
inline std::uint32_t Age(std::uint64_t now,std::uint64_t at)noexcept {
 if(!at||now<at)return UINT32_MAX;const auto age=now-at;return age>UINT32_MAX?UINT32_MAX:static_cast<std::uint32_t>(age);
}
// Diagnostic only. Native permission continues to use the unchanged
// sleep_parts_policy::CurrentSample predicate; this function cannot admit it.
inline const char* Explain(bool preparing,std::uint64_t preparedAt,const sl::Sample& prepared,const sl::Lease& l,
 const sl::Sample& a,std::uint64_t now)noexcept {
 const auto& own=l.phase==sl::Phase::empty?prepared.owner:l.owner;
 if(!now)return "clock";if(!sl::Same(own,a.owner))return "owner";if(!a.graphCurrent)return "graph_current";
 if(!std::isfinite(a.health)||!std::isfinite(a.maximumHealth)||a.health<=0||a.health>a.maximumHealth||a.maximumHealth<1||a.maximumHealth>65535)return "health";
 if(!sl::ValidStun(a))return "stun";if(a.stance<1||a.stance>3)return "stance";if(a.cause!=2)return "cause";if(a.lifeState>2)return "life";
 if(l.phase==sl::Phase::empty){
  if(!preparing||!preparedAt||now<preparedAt||now-preparedAt>sl::kAcquireMs)return "preparation";
  if(sl::Bits(prepared.maximumStun)!=sl::Bits(a.maximumStun))return "prepare_maximum";
  return sl::RetiredSample(a)?nullptr:"prepare_sample";
 }
 if(l.phase!=sl::Phase::seeded&&l.phase!=sl::Phase::active&&l.phase!=sl::Phase::wakePending&&l.phase!=sl::Phase::retired)return "phase";
 if(!l.lastSeen||now<l.lastSeen||now-l.lastSeen>sl::kFreshMs)return "last_seen";
 if(!sl::SameMaximum(l,a))return "maximum";
 if(!l.seededAt||now<l.seededAt||(!l.nativeEntered&&now-l.seededAt>sl::kAcquireMs))return "onset_time";
 if(l.wakeAt&&(now<l.wakeAt||now-l.wakeAt>sl::kRetireMs))return "wake_time";
 if(l.phase==sl::Phase::retired)return l.nativeEntered&&sl::RetiredSample(a)?nullptr:"retired_sample";
 if(a.mode==0){
  if(a.lifeState==2)return !l.nativeEntered&&l.phase==sl::Phase::seeded&&!a.nodeActive&&!a.graphSleepCandidate&&a.action==0x44&&
   (a.qAction==1||a.qAction==5)&&a.stance==1&&(a.requested==0||a.requested==4)?nullptr:"seed_pending";
  return l.nativeEntered&&(sl::RetiredSample(a)||sl::RecoveryHolder(l,a,now))?nullptr:"mode0_not_retired";
 }
 if(!a.nodeActive)return "node_active";if(a.action!=0x19)return "ps_action";if(a.qAction!=std::uint16_t(0x7f+a.mode))return "q_action";
 if(a.mode==4)return a.lifeState==2&&(a.requested==0||a.requested==5)?nullptr:"mode4_state_request";
 if(a.mode==5)return (a.requested==0||a.requested==6)&&(a.lifeState==2||(a.lifeState==0&&l.wakeAuthorized))?nullptr:"mode5_state_request_grant";
 if(a.mode==6){if(a.lifeState!=0||!l.nativeEntered)return "mode6_state";return a.requested==0?nullptr:"mode6_request";}
 return "mode_unknown";
}
}
