-- Original, separate default-off stock weapon loading experiment.
-- Insert after equipment_loading.lua, before vanilla StartLoading.
-- Stock 46 is the public EQP_WP_East_sr_020, a different sniper from the
-- customized blueprint 794. Public callback A03C40 has only 649 bank rows;
-- blueprint 794 is outside that bank and must never be supplied here.
do
    local state = Tpp and Tpp.MgsCoop
    local eligible = type(state) == "table"
        and state.equipmentPreparationMode == "offline_allocation_probe"
        and state.equipmentPreparationReturned == true
        and vars and vars.missionCode == 30010
        and state.playerPackageSelected == true
        and state.playerPackageMission == vars.missionCode
        and state.archiveDirectReadSucceeded == true
        and state.hostSessionCreated ~= true and state.clientPrepared ~= true
        and type(TppEquip) == "table"
        and TppEquip.EQP_WP_East_sr_020 == 46
        and type(TppEquip.RequestLoadToEquipMissionBlock) == "function"
        and type(loadstring) == "function"
    if eligible then
        local queryOk, accepted = pcall(function()
            local query = loadstring("return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_QUERY_V1'")
            return query and query() == true
        end)
        if queryOk and accepted == true then
            -- Native consumes this allocation's attempt before this call and
            -- independently requires null P, exact bank/cache/queue bounds,
            -- copied saves, the natural allocation owner and source receipts.
            state.stockPreloadAttempted = true
            state.stockPreloadEquipId = 46
            state.stockPreloadMission = vars.missionCode
            state.stockPreloadReturned = false
            state.stockPreloadVerified = false
            state.stockPreloadError = nil
            local ok, failure = pcall(function()
                TppEquip.RequestLoadToEquipMissionBlock({46})
            end)
            state.stockPreloadReturned = ok
            state.stockPreloadError = not ok and tostring(failure) or nil
            local reported, verified = pcall(function()
                local receipt = loadstring(ok
                    and "return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_RETURNED_V1'"
                    or "return 'MGS_COOP_REMOTE_STOCK46_PRELOAD_FAILED_V1'")
                return receipt and receipt() == true
            end)
            state.stockPreloadVerified = ok and reported and verified == true
            -- Returned/queued does not establish package loading or rendering.
            -- There is no runtime loading, inventory write or native dispatch.
        end
    end
end
