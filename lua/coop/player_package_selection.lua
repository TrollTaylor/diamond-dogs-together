-- Project-authored fragment for the fingerprinted TppPackList.lua module.
-- Enabled only by the native allocation-test mode after archive mounting.
do
    local makeOriginal = e.MakeMissionPackList
    local receipts = 0
    function e.MakeMissionPackList(missionCode, callback)
        -- Selection describes this request, never a previous mission or a
        -- failed rebuild. Clear before invoking a callback that may throw.
        local coop = type(Tpp) == "table" and Tpp.MgsCoop
        if type(coop) == "table" then
            coop.playerPackageSelected = false
            coop.playerPackageMission = nil
        end
        local packs = makeOriginal(missionCode, callback)
        coop = type(Tpp) == "table" and Tpp.MgsCoop
        if type(coop) ~= "table" or not coop.archiveDirectReadSucceeded then return packs end
        local capability = type(coop.partnerMissions) == "table" and coop.partnerMissions[missionCode]
        if type(capability) ~= "table" then return packs end
        local controller, originalMission, replacementMission = capability.controller, capability.original, capability.replacement
        if type(packs) ~= "table" or packs ~= e.missionPackList then return packs end
        -- The normal mission already owns a single-player controller. Replace
        -- its package with our exact copy minus that definition before adding
        -- the multiplayer controller. Demo/hangar variants stay out of scope.
        local controllerCount, missionIndex, missionCount = 0, nil, 0
        for index, path in ipairs(packs) do
            if path == controller then controllerCount = controllerCount + 1 end
            -- A foreign co-op controller or labelled variant is ambiguous,
            -- even if the normal pack is also present. Never load two owners.
            if path == "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk" and path ~= controller
                or path == "/Assets/tpp/pack/player/game_object/mgs_coop_two_player_scarf_controller.fpk" and path ~= controller
                or type(path) == "string" and path:match("^/Assets/tpp/pack/mission2/free/f" .. missionCode .. "/f%d+.*%.fpk$")
                    and path ~= originalMission then return packs end
            for code, other in pairs(coop.partnerMissions) do
                if code ~= missionCode and (path == other.original or path == other.replacement) then return packs end
            end
            if path == originalMission then
                missionIndex, missionCount = index, missionCount + 1
            elseif path == replacementMission then
                return packs -- Unexpected pre-replacement: refuse ambiguous ownership.
            end
        end
        if missionCount ~= 1 or controllerCount > 1 then return packs end
        packs[missionIndex] = replacementMission
        if controllerCount == 0 then packs[#packs+1] = controller end
        coop.playerPackageSelected = true
        coop.playerPackageMission = missionCode
        -- A selection receipt is not a loaded-package or actor-count receipt.
        if receipts < 16 and type(loadstring) == "function" then
            receipts = receipts + 1
            loadstring("return 'MGS_COOP_PACK_SELECTED_V1' -- mission=" .. missionCode .. "; single_controller=true")
        end
        return packs
    end
end
