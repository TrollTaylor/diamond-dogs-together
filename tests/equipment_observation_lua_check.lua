-- Runs the actual receipt against zero-based game-like readonly objects.
-- Host Lua fixture only: no executable, game API, native dispatch or saves.
local checks = 0
local function check(value) checks = checks + 1; assert(value, checks) end
local function fixture(options)
    options = options or {}
    local writes, calls, reads = 0, 0, {}
    local parts = {}; for i = 0, 35 do parts[i] = i + 10 end
    local data = {
        missionCode = 30010, currentInventorySlot = 1,
        weapons = {[0]=871, [1]=873, [2]=875},
        customizedWeapon = {[0]=111, [1]=222, [2]=333}, chimeraParts = parts,
    }
    if options.edit then options.edit(data) end
    local function object(t, name)
        if t == nil then return nil end
        return setmetatable({}, {
            __index = function(_, key)
                reads[#reads + 1] = name .. ":" .. tostring(key)
                if options.failure == name .. ":" .. tostring(key) then error("unreadable") end
                return t[key]
            end,
            __newindex = function() writes = writes + 1; error("write forbidden") end,
        })
    end
    local wrapped = {}
    for key, value in pairs(data) do
        wrapped[key] = type(value) == "table" and object(value, key) or value
    end
    local env = {vars = object(wrapped, "vars"),
        TppDefine = {WEAPONSLOT = object({PRIMARY_HIP=0, PRIMARY_BACK=1, SECONDARY=2}, "WEAPONSLOT")},
        Player = setmetatable({}, {__index=function() calls=calls+1; error("game API forbidden") end}),
        TppEquip = setmetatable({}, {__index=function() calls=calls+1; error("game API forbidden") end}),
    }
    if options.noVars then env.vars = nil end
    if options.noDefinitions then env.TppDefine = nil end
    setmetatable(env, {__index=_G})
    local function run()
        local chunk = assert(loadfile(arg[1])); setfenv(chunk, env)
        local ok, result = pcall(chunk)
        check(ok and type(result) == "string" and #result <= 768)
        check(writes == 0 and calls == 0)
        return result
    end
    return run, data, reads
end
local run, data, reads = fixture()
local receipt = run()
check(receipt:find("MGS_COOP_EQUIPMENT_OBSERVATION_V1;mission=30010;active=1;slots=871,873,875;base=111,222,333;parts=", 1, true) == 1)
local expected = {}; for i=0,35 do expected[i+1]=tostring(i+10) end
check(receipt:find(";parts=" .. table.concat(expected, ",") .. ";complete=1;missing=0;faults=0;invalid=0;", 1, true) ~= nil)
check(receipt:find(";read_only=1;remote_readiness_unverified=1;receiver_mapping_unverified=1", 1, true) ~= nil)
check(data.customizedWeapon[0] == 111 and data.weapons[0] == 871 and data.chimeraParts[35] == 45)
check(run() == receipt)
local seen = {}; for _, entry in ipairs(reads) do seen[entry]=true end
check(seen["customizedWeapon:0"] and seen["customizedWeapon:2"] and not seen["customizedWeapon:3"])
check(seen["chimeraParts:0"] and seen["chimeraParts:35"] and not seen["chimeraParts:36"])
check(seen["weapons:0"] and seen["weapons:1"] and seen["weapons:2"])

for _, options in ipairs({{noVars=true}, {noDefinitions=true},
    {failure="vars:customizedWeapon"}, {failure="customizedWeapon:0"},
    {failure="chimeraParts:35"}, {failure="WEAPONSLOT:PRIMARY_HIP"},
    {edit=function(d) d.customizedWeapon=nil end}, {edit=function(d) d.chimeraParts=nil end},
    {edit=function(d) d.chimeraParts[0]=nil end}, {edit=function(d) d.weapons[0]=nil end}}) do
    run = fixture(options); local result=run()
    check(result:find(";complete=0;", 1, true) ~= nil)
    check(result:find("?", 1, true) ~= nil)
end
for _, value in ipairs({-1, 256, 1.5, math.huge, -math.huge, 0/0, "25", true, {}}) do
    run = fixture({edit=function(d) d.chimeraParts[0]=value end})
    local result=run()
    check(result:find(";parts=?,11,12,", 1, true) ~= nil)
    check(result:find(";complete=0;missing=0;faults=0;invalid=1;", 1, true) ~= nil)
end
for _, value in ipairs({-1, 65536, 1.5, math.huge, 0/0, "111", false}) do
    run = fixture({edit=function(d) d.customizedWeapon[0]=value end})
    check(run():find(";base=?,222,333;", 1, true) ~= nil)
end
run = fixture({edit=function(d)
    d.customizedWeapon[0]=0; d.customizedWeapon[1]=65535
    d.chimeraParts[0]=0; d.chimeraParts[35]=255
end})
check(run():find(";complete=1;", 1, true) ~= nil) -- Zero is valid data, not a missing array element.
print("equipment observation Lua checks: " .. checks)
