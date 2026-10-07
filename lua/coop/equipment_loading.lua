
-- Adapted from Dynamite TppMain.OnAllocate, revision
-- c90f252f8200765f66c005d6bf4357fda0c0d98e; GPLv3 + Commons Clause 1.0.
-- Insert after ordinary equipment-group setup, before equipment loading.
do
    local state = Tpp and Tpp.MgsCoop
    local supported = type(state) == "table" and type(state.IsPartnerBodyMission) == "function"
        and vars and state.IsPartnerBodyMission(vars.missionCode) == true
    local online = supported and (state.hostSessionCreated == true or state.clientPrepared == true)
    local offline = false
    if supported and not online and type(loadstring) == "function" then
        -- Default-off native authorization accepts only the current natural
        -- allocation owner with redirected saves and a hash-approved two-slot
        -- package. It consumes this allocation's attempt before either call.
        -- This does not create a session or set S_IS_ONLINE.
        local ok, accepted = pcall(function()
            -- This public group setup runs before player construction. Prove
            -- the selected package and mission state here; native separately
            -- requires either a genuinely null P or its full actor contract.
            if state.playerPackageSelected ~= true
                or state.playerPackageMission ~= vars.missionCode
                or state.archiveDirectReadSucceeded ~= true then return false end
            if type(TppSystemUtility) ~= "table"
                or type(TppSystemUtility.GetCurrentGameMode) ~= "function"
                or TppSystemUtility.GetCurrentGameMode() ~= "TPP" then return false end
            if type(TppGameStatus) ~= "table" or type(TppGameStatus.IsSet) ~= "function"
                or TppGameStatus.IsSet("Mission", "S_IS_ONLINE") ~= false then return false end
            local query = loadstring("return 'MGS_COOP_OFFLINE_EQUIPMENT_GROUPS_PREPLAYER_V2'")
            return query and query() == true
        end)
        offline = ok and accepted == true
    end
    if online or offline then
        state.equipmentPreparationAttempted = true
        state.equipmentPreparationReturned = false
        state.equipmentPreparationError = nil
        state.equipmentPreparationMode = offline and "offline_allocation_probe" or "native_session"
        local available = type(TppPlayer) == "table"
            and type(TppPlayer.SetEquipMissionBlockGroupSize) == "function"
            and type(TppEquip) == "table"
            and type(TppEquip.CreateEquipGhostBlockGroups) == "function"
        local size = mvars and mvars.ply_equipMissionBlockGroupSize
        local validSize = type(size) == "number" and size == size and size > 0 and size < math.huge
        if offline and not validSize then
            state.equipmentPreparationError = "Offline equipment block size is missing or invalid"
        elseif available then
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

