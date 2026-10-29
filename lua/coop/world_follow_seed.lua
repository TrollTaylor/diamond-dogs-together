-- Original offline W3 complete seed schema. Loaded before world_follow.lua.
do
    local W = e.MgsCoopWorld
    local S = {keys = {{"solface_groupNumber"}, {"hosface_groupNumber"}, {"rev_revengeRandomValue"},
        {"rev_revengeLv", "revenge"}, {"missionStartClock"}, {"missionStartWeather"},
        {"missionStartWeatherNextTime", nil, 1000}, {"missionStartExtraWeatherInterval", nil, 1000},
        {"heli_missionStartRoute"}, {"mis_isStartFromHelispace", nil, "bool"}, {"mis_isStartFromFreePlay", nil, "bool"},
        {"ply_initialPlayerState"}, {"ply_useMissionStartPos", nil, "bool"}, {"ply_missionStartPos", 3, 1000},
        {"ply_missionStartRot", nil, 1}, {"ply_useMissionStartPosForNoOrderBox", nil, "bool"},
        {"ply_missionStartPosForNoOrderBox", 3, 1000}, {"ply_missionStartRotForNoOrderBox", nil, 1},
        {"initialPlayerFlag", nil, nil, true}, {"initialPlayerPosX", nil, 1000, true},
        {"initialPlayerPosY", nil, 1000, true}, {"initialPlayerPosZ", nil, 1000, true},
        {"initialPlayerRotY", nil, 1000, true}, {"mis_checkPoint"},
        {"ini_isTitleMode", nil, "bool", nil, true}, {"ini_isReturnToTitle", nil, "bool", nil, true},
        {"isContinueFromTitle", nil, "bool", nil, true}, {"ply_isUsedPlayerInitialAction", nil, "bool", "svars"}}}
    -- M1 host-owned companion (docs/vehicles-companions-design.md 6.1): the host's buddy vars (types
    -- and counts from the declarations at 0x14099EA60). Always sent; key[6] "buddy": the guest applies
    -- them only with companion_host_owned (lua/coop/world_companion.lua keeps its own for its saves).
    for _, k in ipairs({{"buddyType"}, {"initialBuddyStatus"}, {"initialBuddyPos", 3, 1000}, {"initialBuddyRotY", nil, 1000},
        {"buddyQuietEquipType"}, {"buddyQuietCostumeType"}, {"buddyDogFlag"}, {"buddyDogMode"}, {"buddyDogEquipType"},
        {"buddyDogEyeType"}, {"buddyDogColorType"}, {"buddyGearAmmoCount", 2}, {"buddyGearStockAmmoCount", 2},
        {"buddyGearBodyGrade"}, {"buddyGearEquipGrade"}, {"buddyGearReloadGrade"}, {"buddyGearHeadType"}, {"buddyGearHeadGrade"},
        {"buddyGearArmType"}, {"buddyGearArmGrade"}, {"buddyGearMainWPType"}, {"buddyGearMainWPGrade"}, {"buddyGearSubWPType"},
        {"buddyGearSubWPGrade"}, {"buddyGearColoring0"}, {"buddyGearColoring1"}, {"buddyHorseType"}, {"buddyHorseColorType"},
        {"buddyHorseHeadDesignType"}, {"buddyHorseFootDesignType"},
        -- The sortie's buddy: TppBuddyService.ResetVarsMissionStart (0xA536D0, InitializeForNewMission, before the
        -- block Load) copies these into the live vars (run 6? of M1: without them the guest loaded its own buddy).
        {"buddyTypeMissionStart"}, {"initialBuddyStatusMissionStart"}, {"initialBuddyPosMissionStart", 3, 1000},
        {"initialBuddyRotYMissionStart", nil, 1000}}) do
        S.keys[#S.keys + 1] = {k[1], k[2], k[3], true, nil, "buddy"}
    end
    W.followSeeds = S
    local floor = math.floor
    function S.data(key) return key[4] == "svars" and svars or key[4] and vars or gvars end
    function S.dims(key)
        if key[2] ~= "revenge" then return key[2] or 1 end
        local n = W.get(W.get(TppRevenge, "REVENGE_TYPE"), "MAX")
        return type(n) == "number" and n == floor(n) and n >= 1 and n <= 128 and n or nil
    end
    function S.encode(key, value)
        if key[3] == "bool" then return type(value) == "boolean" and (value and 1 or 0) or nil end
        if type(value) ~= "number" or value ~= value then return nil end
        local n = floor(value * (key[3] or 1) + 0.5)
        if n < -2147483648 or n > 4294967295 then return nil end
        return n < 0 and n + 4294967296 or n
    end
    function S.decode(key, value)
        if key[3] == "bool" then return value == 1 end
        if key[3] and value >= 2147483648 then value = value - 4294967296 end
        return value / (key[3] or 1)
    end
    function S.checksum(d, k, i, v) return (d * 31 + k * 1024 + i + v) % 4294967296 end
    function S.expected()
        local count = 0
        for _, key in ipairs(S.keys) do
            local n = S.dims(key)
            if not n then return nil end
            count = count + n
        end
        return count
    end
    function S.snapshot()
        local values, digest = {}, 0
        for k, key in ipairs(S.keys) do
            local n, data = S.dims(key), W.get(S.data(key), key[1])
            if not n then return nil, key[1] end
            for i = 0, n - 1 do
                local value = S.encode(key, key[2] and W.get(data, i) or data)
                -- A buddy key never blocks travel: an unreadable one goes as 0 (logged).
                if value == nil and key[6] == "buddy" then
                    value = 0
                    W.log("event=follow;step=seed_substitute;key=" .. key[1] .. ":" .. i)
                end
                if value == nil then return nil, key[1] .. ":" .. i end
                values[#values + 1] = {k, i, value}
                digest = S.checksum(digest, k, i, value)
            end
        end
        return values, digest
    end
    function S.accept(s, k, i, value)
        local key = S.keys[k]
        local n = key and S.dims(key)
        if not n or type(i) ~= "number" or i ~= floor(i) or i < 0 or i >= n or
            type(value) ~= "number" or value ~= floor(value) or value < 0 or value > 4294967295 or
            (key[3] == "bool" and value > 1) then return false end
        s.coverage = s.coverage or {}
        local id = k * 256 + i
        if s.coverage[id] then return false end
        s.coverage[id] = true
        s.values[#s.values + 1] = {k, i, value}
        s.digest = S.checksum(s.digest, k, i, value)
        return true
    end
    function S.complete(s)
        local expected = S.expected()
        if not expected or #s.values ~= expected or not s.coverage then return false end
        for k, key in ipairs(S.keys) do
            for i = 0, S.dims(key) - 1 do
                if not s.coverage[k * 256 + i] then return false end
            end
        end
        return true
    end
    function S.context(s, id)
        local function flag(name)
            local value = W.get(gvars, name)
            return type(value) == "boolean" and (value and 1 or 0) or -1
        end
        local title, returning, continuing = flag("ini_isTitleMode"), flag("ini_isReturnToTitle"), flag("isContinueFromTitle")
        for _, v in ipairs(s.values) do
            local key = S.keys[v[1]]
            if key[5] then gvars[key[1]] = S.decode(key, v[3]) end
        end
        W.log(string.format("event=follow;step=title_context;id=%.0f;title_before=%d;title_after=%d;return_before=%d;return_after=%d;continue_before=%d;continue_after=%d",
            id, title, flag("ini_isTitleMode"), returning, flag("ini_isReturnToTitle"), continuing, flag("isContinueFromTitle")))
    end
end
