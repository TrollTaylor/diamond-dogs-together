bool ReadNativePoseQueuedCarryHandshake(NativePoseQueuedCarryContext& out)noexcept{return PoseQueuedQuery(out);}
bool BindNativePoseQueuedCarryValidator(NativePoseQueuedCarryValidator value)noexcept{
 if(!value||g_pose_bindings.enabled||g_pose_test||g_pose_required||g_pose_held||g_pose_force_a_serial)return false;
 if(g_pose_queued_sleep_current)return g_pose_queued_sleep_current==value;g_pose_queued_sleep_current=value;return true;
}
bool BindNativePoseCarryTreeValidator(NativePoseCarryTreeValidator value)noexcept{
 if(!value||g_pose_bindings.enabled||g_pose_test||g_pose_required||g_pose_held||g_pose_force_a_serial)return false;
 if(g_pose_carry_tree_current)return g_pose_carry_tree_current==value;g_pose_carry_tree_current=value;return true;
}
