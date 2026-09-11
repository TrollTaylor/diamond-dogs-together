-- Project-authored fragment for the fingerprinted TppPackList.lua module.
-- Enabled only by the native allocation-test mode after archive mounting.
do
    local controller = "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk"
    local originalMission = "/Assets/tpp/pack/mission2/free/f30050/f30050.fpk"
    local replacementMission = "/Assets/tpp/pack/mission2/free/f30050/mgs_coop_f30050.fpk"
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
        -- Initial allocation experiment; campaign support is tracked separately.
        if missionCode ~= 30050 then return packs end
        coop = type(Tpp) == "table" and Tpp.MgsCoop
        if type(coop) ~= "table" or not coop.archiveDirectReadSucceeded then return packs end
        if type(packs) ~= "table" or packs ~= e.missionPackList then return packs end
        -- The normal mission already owns a single-player controller. Replace
        -- its package with our exact copy minus that definition before adding
        -- the multiplayer controller. Demo/hangar variants stay out of scope.
        local found, missionIndex, missionCount = false, nil, 0
        for index, path in ipairs(packs) do
            if path == controller then found = true end
            if path == originalMission then
                missionIndex, missionCount = index, missionCount + 1
            elseif path == replacementMission then
                return packs -- Unexpected pre-replacement: refuse ambiguous ownership.
            end
        end
        if missionCount ~= 1 then return packs end
        packs[missionIndex] = replacementMission
        if not found then packs[#packs+1] = controller end
        coop.playerPackageSelected = true
        coop.playerPackageMission = missionCode
        -- A selection receipt is not a loaded-package or actor-count receipt.
        if receipts < 16 and type(loadstring) == "function" then
            receipts = receipts + 1
            loadstring("return 'MGS_COOP_PACK_SELECTED_V1' -- mission=30050; single_controller=true")
        end
        return packs
    end
end
