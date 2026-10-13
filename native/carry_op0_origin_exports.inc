// Insert in partner_input_hook public namespace, outside its anonymous namespace.
bool ReadNativePoseSubmittedCarryHandshake(NativePoseSubmittedCarryContext& out) noexcept {return PoseSubmittedCarryQuery(out);}
bool BindNativePoseSubmittedCarryValidator(NativePoseSubmittedCarryValidator value) noexcept {
 if(!value||g_pose_bindings.enabled||g_pose_test||g_pose_required||g_pose_held||g_pose_force_a_serial)return false;
 if(g_pose_submitted_sleep_current)return g_pose_submitted_sleep_current==value;g_pose_submitted_sleep_current=value;return true;
}
bool NativePoseCarrySenderHooksLive() noexcept {
 if(g_pose_bindings.enabled||!g_pose_installed||!g_pose_carry_dispatch||!g_pose_carry_recipient_message||!g_pose_original_view.load(std::memory_order_acquire))return false;
 const PosePatch* dispatch=nullptr;const PosePatch* recipient=nullptr;
 for(const auto& p:g_pose_patches){if(p.rva==0xbf3a80)dispatch=&p;else if(p.rva==0x11268b0)recipient=&p;}
 std::uintptr_t mismatch=0;return dispatch&&recipient&&PosePatchOwned(*dispatch)&&PosePatchOwned(*recipient)&&PoseCarryOriginCode(mismatch);
}
