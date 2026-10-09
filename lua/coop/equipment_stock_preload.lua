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

-- The generalized preload (W2; docs/native-puppet.md "Generalized preload"):
-- the received manifest's base ids, in any partner-body mission, with
-- partner_native_equip=manifest|substitute (native refuses the stock46 query
-- above then, and this one with stock46). Native answers the query with the
-- ids it qualified (a table of one to three stock ids: no eviction, queue
-- room, no entry the local player references) or false, consuming this
-- allocation's attempt before the one public call.
do
    local state = Tpp and Tpp.MgsCoop
    local eligible = type(state) == "table"
        and state.equipmentPreparationMode == "offline_allocation_probe"
        and state.equipmentPreparationReturned == true
        and vars and type(state.IsPartnerBodyMission) == "function"
        and state.IsPartnerBodyMission(vars.missionCode) == true
        and state.playerPackageSelected == true
        and state.playerPackageMission == vars.missionCode
        and state.archiveDirectReadSucceeded == true
        and state.hostSessionCreated ~= true and state.clientPrepared ~= true
        and type(TppEquip) == "table"
        and type(TppEquip.RequestLoadToEquipMissionBlock) == "function"
        and type(loadstring) == "function"
    if eligible then
        local queryOk, ids = pcall(function()
            local query = loadstring("return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_QUERY_V2'")
            return query and query()
        end)
        local valid = queryOk and type(ids) == "table" and #ids >= 1 and #ids <= 3
        if valid then
            for i = 1, #ids do
                local id = ids[i]
                if type(id) ~= "number" or id ~= math.floor(id) or id < 1 or id > 559 then valid = false end
            end
        end
        if valid then
            local request = {}
            for i = 1, #ids do request[i] = ids[i] end
            state.equipPreloadAttempted = true
            state.equipPreloadMission = vars.missionCode
            state.equipPreloadIds = request
            state.equipPreloadReturned = false
            state.equipPreloadVerified = false
            state.equipPreloadError = nil
            local ok, failure = pcall(function()
                TppEquip.RequestLoadToEquipMissionBlock(request)
            end)
            state.equipPreloadReturned = ok
            state.equipPreloadError = not ok and tostring(failure) or nil
            local reported, verified = pcall(function()
                local receipt = loadstring(ok
                    and "return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_RETURNED_V2'"
                    or "return 'MGS_COOP_REMOTE_EQUIP_PRELOAD_FAILED_V2'")
                return receipt and receipt() == true
            end)
            state.equipPreloadVerified = ok and reported and verified == true
            -- Queued does not establish loading; the writer's preflight does.
        end
    end
end
