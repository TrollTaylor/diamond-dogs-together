#pragma once
#include "mgscoop/native_pose_window.hpp"
namespace mgscoop::vehicle_mirror_close_read {
namespace ph=partner_input_hook;
inline bool SameJob(const ph::NativePoseJobContext& a,const ph::NativePoseJobContext& b) noexcept {
 return a.player==b.player&&a.go==b.go&&a.block==b.block&&a.state==b.state&&a.modelCS==b.modelCS&&a.modelEntry==b.modelEntry&&
  a.generation==b.generation&&a.sceneEpoch==b.sceneEpoch&&a.thread==b.thread&&a.consumer==b.consumer&&
  a.held==b.held&&a.press==b.press&&a.release==b.release&&a.magnitude==b.magnitude&&
  a.modelReady&&b.modelReady&&a.selectedBodyOwned&&b.selectedBodyOwned&&a.selectedBody==1&&b.selectedBody==1&&
  a.pp==b.pp&&a.source==b.source&&a.windowPPVT==b.windowPPVT&&a.birthSerial==b.birthSerial&&
  a.player&&a.go&&a.block&&a.state&&a.modelCS&&a.modelEntry&&a.generation&&a.sceneEpoch&&a.thread&&a.pp&&a.source&&a.windowPPVT&&a.birthSerial;
}
// Called only with the real Closed callback's immutable copy. The core
// increments odd -> even before EndWritePublish, then issues this callback.
// A false Current() outside the writer has no bearing on successful release.
inline bool Published(const ph::NativePoseJobContext& requested,const ph::NativePoseJobContext& closed,
 std::uint32_t actualPublishedSequence,bool actualCleanupComplete,const ph::NativePoseStatus& actualStatus) noexcept {
 return actualCleanupComplete&&actualStatus.installed&&!actualStatus.held&&!actualStatus.owned&&!actualStatus.faulted&&
  (requested.sequence&1u)&&!(closed.sequence&1u)&&closed.sequence==requested.sequence+1u&&
  closed.sequence==actualPublishedSequence&&SameJob(requested,closed);
}
} // namespace mgscoop::vehicle_mirror_close_read
