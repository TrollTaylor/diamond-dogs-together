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
        {"initialPlayerRotY", nil, 1000, true}, {"mis_checkPoint"}}}
    W.followSeeds = S
    local floor = math.floor
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
            local n, data = S.dims(key), W.get(key[4] and vars or gvars, key[1])
            if not n then return nil, key[1] end
            for i = 0, n - 1 do
                local value = S.encode(key, key[2] and W.get(data, i) or data)
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
end
