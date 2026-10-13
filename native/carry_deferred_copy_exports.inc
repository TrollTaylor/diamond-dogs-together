bool ReadNativePoseDeferredCarryRequest(NativePoseDeferredCarryContext& out)noexcept{return PoseDeferredQuery(out);}
bool BindNativePoseDeferredCarryValidator(NativePoseDeferredCarryValidator value)noexcept{
 if(!value||g_pose_bindings.enabled||g_pose_test||g_pose_required||g_pose_held||g_pose_force_a_serial)return false;
 if(g_pose_deferred_sleep_current)return g_pose_deferred_sleep_current==value;g_pose_deferred_sleep_current=value;return true;
}
bool NativePoseDeferredCarryHooksLive()noexcept{return PoseDeferredPatchesLive();}
bool CopyNativePoseDeferredCarryRequest()noexcept{return PoseDeferredCopyRequest();}
