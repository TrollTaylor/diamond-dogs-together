bool NativePoseCarryQueueLifetimeLive() noexcept {return PoseQueuePatchesLive();}
bool ReadNativePoseCarryQueueContext(NativePoseCarryQueueContext& out) noexcept {
 out={};NativePoseJobContext job{},after{};
 if(g_pose_bindings.enabled||!ReadNativePoseOwnedJobContext(job)||!PoseOwned()||!t_pose.job||!g_pose_queue_bound||
  g_pose_owners.held!=(holds::sleep|holds::carry)||g_pose_owners.pending||g_pose_fault||g_shutdown||g_pose_clear||
  !g_pose_owners.carrySerial||!NativePoseCarryRecipientLeaseCurrent({g_pose_owners.carrySerial,g_pose_generation,g_pose_epoch})||
  !PoseQueueCurrent()||!job.selectedBodyOwned||job.selectedBody!=1||job.player!=g_pose_queue_owner.player||job.player!=g_pose_carry_pair.player||
  !RequireNativePoseOwnedJob(reinterpret_cast<void*>(job.go),g_pose_pool)||!ReadNativePoseOwnedJobContext(after)||
  after.player!=job.player||after.go!=job.go||after.block!=job.block||after.state!=job.state||after.modelCS!=job.modelCS||after.modelEntry!=job.modelEntry||
  after.thread!=job.thread||after.sequence!=job.sequence||after.generation!=job.generation||after.sceneEpoch!=job.sceneEpoch||after.birthSerial!=job.birthSerial||
  after.pp!=job.pp||after.source!=job.source||after.windowPPVT!=job.windowPPVT||after.consumer!=job.consumer||!PoseQueueCurrent())return false;
 out.job=job;out.root=g_pose_queue_owner.root;out.slab=g_pose_queue_owner.slab;out.queued=g_pose_queue_owner.queued;
 out.snapshot=g_pose_queue_owner.snapshot;out.bytes=g_pose_queue_owner.bytes;out.carrySerial=g_pose_owners.carrySerial;return true;
}
