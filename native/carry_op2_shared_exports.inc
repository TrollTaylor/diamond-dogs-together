bool ReadNativePoseCarryOp2Origin(NativePoseCarryOp2Context& out)noexcept{return PoseCarryOp2Query(out);}
bool BindNativePoseCarryOp2Validator(NativePoseCarryOp2Validator value)noexcept{
 if(!value||g_pose_bindings.enabled||g_pose_test||g_pose_required||g_pose_held||g_pose_force_a_serial)return false;
 if(g_pose_op2_sleep_current)return g_pose_op2_sleep_current==value;g_pose_op2_sleep_current=value;return true;
}
bool BindNativePoseCarryOp2TreeValidator(NativePoseCarryOp2TreeValidator value)noexcept{
 if(!value||g_pose_bindings.enabled||g_pose_test||g_pose_required||g_pose_held||g_pose_force_a_serial)return false;
 if(g_pose_op2_tree_current)return g_pose_op2_tree_current==value;g_pose_op2_tree_current=value;return true;
}
