
-- Adapted from Dynamite TppMain.OnAllocate, revision
-- c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
-- Insert after ordinary equipment-group setup, before equipment loading.
do
    local state = Tpp and Tpp.MgsCoop
    if type(state) == "table" and type(state.IsPartnerBodyMission) == "function" and state.IsPartnerBodyMission(vars.missionCode)
        and (state.hostSessionCreated == true or state.clientPrepared == true) then
        state.equipmentPreparationAttempted = true
        state.equipmentPreparationReturned = false
        local available = type(TppPlayer) == "table"
            and type(TppPlayer.SetEquipMissionBlockGroupSize) == "function"
            and type(TppEquip) == "table"
            and type(TppEquip.CreateEquipGhostBlockGroups) == "function"
        if available then
            local ok, failure = pcall(function()
                TppPlayer.SetEquipMissionBlockGroupSize()
                -- One remote equipment group for the immediate two-player target.
                TppEquip.CreateEquipGhostBlockGroups({ ghostCount = 1 })
            end)
            state.equipmentPreparationReturned = ok
            state.equipmentPreparationError = not ok and tostring(failure) or nil
        else
            state.equipmentPreparationError = "Required equipment-loading functions are unavailable"
        end
        -- A returned allocation request is not proof that remote gear loaded.
        -- Original StartLoadingToEquipMissionBlock runs immediately afterward.
        local observed
        if state.equipmentPreparationReturned then
            observed = loadstring("return 'MGS_COOP_EQUIPMENT_GROUPS_RETURNED_V1'")
        else
            observed = loadstring("return 'MGS_COOP_EQUIPMENT_GROUPS_FAILED_V1'")
        end
        if observed then observed() end
    end
end

