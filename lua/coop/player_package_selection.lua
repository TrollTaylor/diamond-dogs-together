-- Project-authored fragment for the fingerprinted TppPackList.lua module.
-- Enabled only by the native allocation-test mode after archive mounting.
do
    local makeOriginal = e.MakeMissionPackList
    local receipts = 0
    -- Mother Base layout packs: TppPackList.AddFOBLayoutPack adds
    -- f30050_ly%03d.fpk (vars.mbLayoutCode) right after f30050.fpk. The game
    -- data holds exactly ly001..ly003; none defines a player object, locator or
    -- controller (only locators bound to f30050.fpkd's objects, which our
    -- replacement keeps), so one of them may follow the replaced mission pack
    -- (docs/player-package-selection.md "Mother Base layouts").
    local layoutPrefix = "/Assets/tpp/pack/mission2/free/f30050/f30050_ly"
    local layouts = {[layoutPrefix .. "001.fpk"] = "f30050_ly001", [layoutPrefix .. "002.fpk"] = "f30050_ly002",
        [layoutPrefix .. "003.fpk"] = "f30050_ly003"}
    -- Diagnostics: why a partner mission's pack list was left alone, and that
    -- list, as bounded load-buffer receipts (24 refusals, 6 lists of 96 lines).
    -- scene= names what the player sees: demo_stage (f30051), hangar
    -- (f30050_hanger_btgNN), story (another labelled variant such as f30011),
    -- layout (a layout pack outside the accepted shape) or other.
    local refusals, listed = 0, 0
    -- State is separate from capped diagnostics: every request clears the last
    -- result before vanilla can throw, then selected/refused reports its result.
    local function state(missionCode, result, scene)
        if type(loadstring) ~= "function" then return end
        local code = type(missionCode) == "number" and missionCode >= 1 and missionCode <= 999999
            and missionCode == math.floor(missionCode) and missionCode or 0
        loadstring("return 'MGS_COOP_PACK_STATE_V1' -- mission=" .. code .. "; result=" .. result .. "; scene=" .. (scene or "none"))
    end
    local function list(missionCode, packs)
        if listed >= 6 or type(packs) ~= "table" or type(loadstring) ~= "function" then return end
        listed = listed + 1
        for index, path in ipairs(packs) do
            if index > 96 then break end
            loadstring("return 'MGS_COOP_PACK_LIST_V1' -- mission=" .. tostring(missionCode) .. "; i=" .. index
                .. "; path=" .. string.sub(tostring(path), 1, 160))
        end
    end
    local function refuse(missionCode, reason, path, packs, scene)
        state(missionCode, "refused", scene or "other")
        if refusals < 24 and type(loadstring) == "function" then
            refusals = refusals + 1
            loadstring("return 'MGS_COOP_PACK_REFUSED_V1' -- mission=" .. tostring(missionCode) .. "; reason="
                .. string.sub(reason, 1, 32) .. "; scene=" .. (scene or "other") .. "; path=" .. string.sub(tostring(path or ""), 1, 120))
        end
        list(missionCode, packs)
    end
    local function variantScene(path)
        if type(path) ~= "string" then return "other" end
        if path:match("/f30051%.fpk$") then return "demo_stage" end
        if path:match("_hanger_btg%d+%.fpk$") then return "hangar" end
        if path:match("^/Assets/tpp/pack/mission2/") then return "story" end
        return "other"
    end
    function e.MakeMissionPackList(missionCode, callback)
        state(missionCode, "clear")
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
        if type(packs) ~= "table" or packs ~= e.missionPackList then refuse(missionCode, "not_mission_list", "", packs); return packs end
        -- The normal mission already owns a single-player controller. Replace
        -- its package with our exact copy minus that definition before adding
        -- the multiplayer controller. Demo/hangar variants stay out of scope.
        local controllerCount, missionIndex, missionCount = 0, nil, 0
        local layoutIndex, layoutCount = nil, 0
        for index, path in ipairs(packs) do
            if path == controller then controllerCount = controllerCount + 1 end
            if type(path) == "string" and string.sub(path, 1, #layoutPrefix) == layoutPrefix then
                -- Before the variant clause: f%d+.*%.fpk also matches f30050_ly003.fpk.
                if missionCode ~= 30050 then refuse(missionCode, "layout_mission", path, packs, "layout"); return packs end
                if not layouts[path] then refuse(missionCode, "layout_unknown", path, packs, "layout"); return packs end
                layoutIndex, layoutCount = index, layoutCount + 1
            -- A foreign co-op controller or labelled variant is ambiguous,
            -- even if the normal pack is also present. Never load two owners.
            elseif path == "/Assets/tpp/pack/player/game_object/mgs_coop_four_player_controller.fpk" and path ~= controller
                or path == "/Assets/tpp/pack/player/game_object/mgs_coop_two_player_scarf_controller.fpk" and path ~= controller
                or type(path) == "string" and path:match("^/Assets/tpp/pack/mission2/free/f" .. missionCode .. "/f%d+.*%.fpk$")
                    and path ~= originalMission then refuse(missionCode, "foreign_or_variant", path, packs, variantScene(path)); return packs end
            for code, other in pairs(coop.partnerMissions) do
                if code ~= missionCode and (path == other.original or path == other.replacement) then refuse(missionCode, "other_mission", path, packs); return packs end
            end
            if path == originalMission then
                missionIndex, missionCount = index, missionCount + 1
            elseif path == replacementMission then
                refuse(missionCode, "pre_replaced", path, packs)
                return packs -- Unexpected pre-replacement: refuse ambiguous ownership.
            end
        end
        if missionCount ~= 1 or controllerCount > 1 then
            refuse(missionCode, "count_mission" .. missionCount .. "_controller" .. controllerCount, originalMission, packs)
            return packs
        end
        -- At most one layout, directly after the mission pack (AddFOBLayoutPack's
        -- only position); no layout is the pre-layout fixture shape.
        if layoutCount > 1 then refuse(missionCode, "layout_count" .. layoutCount, packs[layoutIndex], packs, "layout"); return packs end
        if layoutIndex and layoutIndex ~= missionIndex + 1 then refuse(missionCode, "layout_order", packs[layoutIndex], packs, "layout"); return packs end
        -- Only the mission pack is swapped: the layout keeps its place and the
        -- controller is appended last.
        packs[missionIndex] = replacementMission
        if controllerCount == 0 then packs[#packs+1] = controller end
        coop.playerPackageSelected = true
        coop.playerPackageMission = missionCode
        state(missionCode, "selected")
        -- A selection receipt is not a loaded-package or actor-count receipt.
        if receipts < 16 and type(loadstring) == "function" then
            receipts = receipts + 1
            loadstring("return 'MGS_COOP_PACK_SELECTED_V1' -- mission=" .. missionCode .. "; single_controller=true; layout="
                .. (layoutIndex and layouts[packs[layoutIndex]] or "none"))
        end
        return packs
    end
end
