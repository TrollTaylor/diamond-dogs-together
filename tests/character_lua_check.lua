-- Exercises lua/coop/character.lua (TppMain.OnAllocate splice) and
-- lua/coop/character_readback.lua (TppMain append) against a simulated engine:
-- native receipts, vars/mvars/gvars and a TppPlayer that mirrors the 1.0.15.4
-- temporary player-type functions. Optional arguments add the real extracted
-- TppPlayer.lua (its functions replace the mirror) and TppMain.lua (anchor and
-- composed-source compile checks). No MGSV, Steam or native code is involved.
-- Usage: lua51 character_lua_check.lua character.lua character_readback.lua [TppPlayer.lua [TppMain.lua]]
local checks = 0
local function check(value, why)
    checks = checks + 1
    assert(value, tostring(checks) .. ': ' .. (why or 'check failed'))
end
local function readfile(path)
    local file = assert(io.open(path, 'rb'))
    local text = file:read('*a')
    file:close()
    return text
end
local CHARACTER, READBACK, REAL_PLAYER, REAL_MAIN = arg[1], arg[2], arg[3], arg[4]
assert(CHARACTER and READBACK, 'usage: character_lua_check.lua character.lua character_readback.lua [TppPlayer.lua [TppMain.lua]]')
local characterSource = readfile(CHARACTER)
local readbackSource = readfile(READBACK)
-- MSVC limits one raw string literal to 16380 bytes (C2026).
check(#characterSource < 16000 and #readbackSource < 16000, 'sources fit one MSVC string literal')
check(not characterSource:find(')MGS_CHARACTER"', 1, true) and not readbackSource:find(')MGS_CHARACTER_READBACK"', 1, true),
    'raw-string delimiters absent')

local QUERY = "return 'MGS_COOP_CHARACTER_V1'"
local ON = "return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'"
local OFF = "return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'"
local REPORT = "return 'MGS_COOP_CHARACTER_REPORT_V1' -- "
local READ = "return 'MGS_COOP_CHARACTER_READBACK_V1' -- "
local PT = {SNAKE = 0, DD_MALE = 1, DD_FEMALE = 2, AVATAR = 3, LIQUID = 4, OCELOT = 5, QUIET = 6}
local PARTS = {NORMAL = 0, NORMAL_SCARF = 1, HOSPITAL = 3, SNEAKING_SUIT = 2, OCELOT = 26, QUIET = 27}
local CAMO = {OLIVEDRAB = 0, TIGERSTRIPE = 1, HOSPITAL = 2, SPLITTER = 3, OCELOT = 115, QUIET = 116}
local HAND_NORMAL, HAND_ROCKET = 7001, 7002
local BUDDY = {NONE = 0, QUIET = 2, DOG = 3}

-- Mirror of the vanilla temporary-type functions (TppPlayer.lua, 1.0.15.4).
local function mirrorPlayer(env)
    local P = {}
    function P.RegisterTemporaryPlayerType(t)
        if type(t) ~= 'table' then return end
        local m = env.mvars
        m.ply_isExistTempPlayerType = true
        if t.partsType then m.ply_tempPartsType = t.partsType end
        if t.camoType then m.ply_tempCamoType = t.camoType end
        if t.playerType then m.ply_tempPlayerType = t.playerType end
        if t.handEquip then m.ply_tempPlayerHandEquip = t.handEquip end
        if t.faceEquipId then m.ply_tempPlayerFaceEquipId = t.faceEquipId end
    end
    function P.SaveCurrentPlayerType()
        local v, g = env.vars, env.gvars
        if not g.ply_isUsingTempPlayerType then
            g.ply_lastPlayerPartsTypeUsingTemp, g.ply_lastPlayerCamoTypeUsingTemp = v.playerPartsType, v.playerCamoType
            g.ply_lastPlayerHandTypeUsingTemp, g.ply_lastPlayerTypeUsingTemp = v.handEquip, v.playerType
            g.ply_lastPlayerFaceIdUsingTemp, g.ply_lastPlayerFaceEquipIdUsingTemp = v.playerFaceId, v.playerFaceEquipId
        end
        g.ply_isUsingTempPlayerType = true
    end
    function P.ApplyTemporaryPlayerType()
        local v, m = env.vars, env.mvars
        if m.ply_tempPartsType then v.playerPartsType = m.ply_tempPartsType end
        if m.ply_tempCamoType then v.playerCamoType = m.ply_tempCamoType end
        if m.ply_tempPlayerType then v.playerType = m.ply_tempPlayerType end
        if m.ply_tempPlayerHandEquip then v.handEquip = m.ply_tempPlayerHandEquip end
        if m.ply_tempPlayerFaceEquipId then v.playerFaceEquipId = m.ply_tempPlayerFaceEquipId end
    end
    function P.RestoreTemporaryPlayerType()
        local v, g = env.vars, env.gvars
        if g.ply_isUsingTempPlayerType then
            v.playerPartsType, v.playerCamoType = g.ply_lastPlayerPartsTypeUsingTemp, g.ply_lastPlayerCamoTypeUsingTemp
            v.playerType, v.playerFaceId = g.ply_lastPlayerTypeUsingTemp, g.ply_lastPlayerFaceIdUsingTemp
            v.playerFaceEquipId, v.handEquip = g.ply_lastPlayerFaceEquipIdUsingTemp, g.ply_lastPlayerHandTypeUsingTemp
            g.ply_isUsingTempPlayerType = false
        end
    end
    function P.MissionStartPlayerTypeSetting()
        if not env.mvars.ply_isExistTempPlayerType then P.RestoreTemporaryPlayerType() end
        if env.mvars.ply_isExistTempPlayerType then P.SaveCurrentPlayerType(); P.ApplyTemporaryPlayerType() end
    end
    function P.IsCurrentPlayerOcelot() return env.vars.playerType == PT.OCELOT end
    function P.IsCurrentPlayerQuiet() return env.vars.playerType == PT.QUIET end
    function P.ForceChangePlayerFromOcelot()
        local v = env.vars
        env.ocelotRevertCalls = env.ocelotRevertCalls + 1
        if P.IsCurrentPlayerOcelot() or P.IsCurrentPlayerQuiet() then
            if v.sortiePrepPlayerType == PT.SNAKE or v.sortiePrepPlayerType == PT.AVATAR then
                v.playerPartsType, v.playerCamoType = v.sortiePrepPlayerSnakePartsType, v.sortiePrepPlayerSnakeCamoType
                v.playerFaceEquipId = v.sortiePrepPlayerSnakeFaceEquipId
            else
                v.playerPartsType, v.playerCamoType = v.sortiePrepPlayerDDPartsType, v.sortiePrepPlayerDDCamoType
                v.playerFaceEquipId = v.sortiePrepPlayerDDFaceEquipId
            end
            v.playerType, v.playerFaceId = v.sortiePrepPlayerType, v.sortiePrepPlayerFaceId
        end
    end
    return P
end

-- A stub that answers any field with itself and any call with nil, for the
-- engine tables the real TppPlayer.lua captures at load time.
local stub = setmetatable({}, {__call = function() return nil end})
getmetatable(stub).__index = function() return stub end

local realPlayerChunk = REAL_PLAYER and assert(loadfile(REAL_PLAYER)) or nil

local function fixture(o)
    o = o or {}
    local env = {receipts = {}, reports = {}, readbacks = {}, bypass = o.bypassInitially or false,
        ocelotRevertCalls = 0, registered = {}}
    env.vars = {missionCode = o.mission or 10036, playerType = o.playerType or PT.SNAKE,
        playerPartsType = o.parts or PARTS.SNEAKING_SUIT, playerCamoType = o.camo or CAMO.SPLITTER,
        handEquip = o.hand or HAND_ROCKET, playerFaceId = o.face or 77, playerFaceEquipId = o.faceEquip or 4,
        buddyType = o.buddyType or BUDDY.NONE,
        sortiePrepPlayerType = o.sortieType or o.playerType or PT.SNAKE, sortiePrepPlayerFaceId = o.sortieFace or 77,
        sortiePrepPlayerSnakePartsType = PARTS.SNEAKING_SUIT, sortiePrepPlayerSnakeCamoType = CAMO.SPLITTER,
        sortiePrepPlayerSnakeFaceEquipId = 4, sortiePrepPlayerDDPartsType = PARTS.NORMAL,
        sortiePrepPlayerDDCamoType = CAMO.TIGERSTRIPE, sortiePrepPlayerDDFaceEquipId = 0}
    env.mvars = {}
    env.gvars = {ply_isUsingTempPlayerType = o.usingTemp or false, ply_lastPlayerTypeUsingTemp = o.lastType}
    env.PlayerType, env.PlayerPartsType, env.PlayerCamoType = PT, PARTS, CAMO
    env.PlayerHandType = {NONE = 0, NORMAL = 1}
    env.TppEquip = {EQP_HAND_NORMAL = HAND_NORMAL, EQP_SUIT = 99}
    env.BuddyType = BUDDY
    env.TppDefine = {STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 5}}
    env.TppStory = {GetCurrentStorySequence = function() return 20 end}
    env.TppMission = {IsSubsistenceMission = function()
        return env.vars.missionCode == 11043 or env.vars.missionCode == 11044 end}
    env.instanceTypes = o.instanceTypes or {[0] = 0, [1] = 1}
    env.Player = {
        GetPlayerType = function(t)
            if o.readbackThrows then error('readback failure') end
            local value = env.instanceTypes[t.instanceIndex] or 0
            return (value >= 5) and 0 or value
        end,
        SetItemLevel = function() end, ResetDirtyEffect = function() end}
    env.PlayerInfo = {GetLocalPlayerIndex = function() return o.localIndex or 0 end}
    if not o.noCoop then
        env.Tpp = {MgsCoop = {}, IsTypeTable = function(v) return type(v) == 'table' end,
            IsTypeFunc = function(v) return type(v) == 'function' end,
            IsTypeString = function(v) return type(v) == 'string' end,
            IsTypeNumber = function(v) return type(v) == 'number' end}
    end
    env.loadstring = function(source)
        env.receipts[#env.receipts + 1] = source
        if o.compileNil then return nil end
        if source == QUERY then
            if o.native == false then return function() return 'MGS_COOP_CHARACTER_V1' end end
            return function() return o.character or '', o.fallback or 'dd_male', o.readback == true end
        elseif source == ON then
            return function()
                if o.bypassRefused then return false end
                env.bypass = true; return true
            end
        elseif source == OFF then
            return function() env.bypass = false; return true end
        elseif source:sub(1, #REPORT) == REPORT then
            env.reports[#env.reports + 1] = source:sub(#REPORT + 1)
        elseif source:sub(1, #READ) == READ then
            env.readbacks[#env.readbacks + 1] = source:sub(#READ + 1)
        end
        return function() return source end
    end
    setmetatable(env, {__index = _G})
    if realPlayerChunk and o.real then
        -- Real-script mode: unknown fields of engine tables answer with the stub
        -- (load-time captures such as TppDefine.WEATHER), on per-fixture copies.
        local function lenient(t)
            local copy = {}
            for key, value in pairs(t) do copy[key] = value end
            return setmetatable(copy, {__index = function() return stub end})
        end
        for _, name in ipairs({'PlayerType', 'PlayerPartsType', 'PlayerCamoType', 'PlayerHandType', 'TppEquip',
                'BuddyType', 'TppDefine', 'TppStory', 'TppMission', 'Player', 'PlayerInfo', 'Tpp'}) do
            if rawget(env, name) ~= nil then env[name] = lenient(env[name]) end
        end
        local realEnv = setmetatable({}, {__index = function(_, key)
            local value = env[key]
            if value ~= nil then return value end
            return stub
        end, __newindex = function(_, key, value) env[key] = value end})
        setfenv(realPlayerChunk, realEnv)
        env.TppPlayer = realPlayerChunk()
        env.realPlayer = true
        -- Count reverts without changing the real function's behaviour.
        local original = env.TppPlayer.ForceChangePlayerFromOcelot
        env.TppPlayer.ForceChangePlayerFromOcelot = function(...)
            env.ocelotRevertCalls = env.ocelotRevertCalls + 1
            return original(...)
        end
    else
        env.TppPlayer = mirrorPlayer(env)
    end
    local register = env.TppPlayer.RegisterTemporaryPlayerType
    env.TppPlayer.RegisterTemporaryPlayerType = function(t)
        env.registered[#env.registered + 1] = t
        if o.registerThrows then error('register failure') end
        return register(t)
    end
    env.originalRevert = env.TppPlayer.ForceChangePlayerFromOcelot
    return env
end

local characterChunk = assert(loadstring(characterSource, '=character.lua'))
local function allocate(env, options)
    options = options or {}
    env.mvars = {}
    if options.mission then env.vars.missionCode = options.mission end
    if options.storyTemp then env.TppPlayer.RegisterTemporaryPlayerType(options.storyTemp) end
    env.receipts, env.reports, env.registered = {}, {}, {}
    setfenv(characterChunk, env)
    characterChunk()
    local status = env.Tpp and env.Tpp.MgsCoop and env.Tpp.MgsCoop.characterStatus
    -- Continue the vanilla OnAllocate order: type setting, then the end call.
    if options.continue ~= true then env.TppPlayer.MissionStartPlayerTypeSetting() end
    if options.endCall ~= false then env.TppPlayer.ForceChangePlayerFromOcelot() end
    return status
end
local function has(list, value) for _, item in ipairs(list) do if item == value then return true end end return false end
local function lastReport(env) return env.reports[#env.reports] or '' end

local function scenarios(real)
    local tag = real and 'real TppPlayer: ' or 'mirror: '
    local function F(o) o = o or {}; o.real = real; return fixture(o) end

    -- Default character: one query, nothing else, vanilla restoration intact.
    local env = F({character = ''})
    local status = allocate(env)
    check(status.result == 'skip' and status.reason == 'default', tag .. 'default skips')
    check(#env.receipts == 1 and env.receipts[1] == QUERY, tag .. 'default sends only the query')
    check(#env.registered == 0 and env.vars.playerType == PT.SNAKE and env.ocelotRevertCalls == 1, tag .. 'default untouched')
    check(env.TppPlayer.ForceChangePlayerFromOcelot == env.originalRevert, tag .. 'default installs no hook')
    for _, name in ipairs({'default'}) do
        env = F({character = name}); check(allocate(env).reason == 'default', tag .. 'explicit default skips')
    end

    -- Native receipt not wired (older DLL): the literal comes back.
    env = F({character = 'quiet', native = false})
    status = allocate(env)
    check(status.reason == 'native_unavailable' and #env.receipts == 1 and #env.registered == 0, tag .. 'unwired native skips')

    -- Quiet in an ordinary story mission.
    env = F({character = 'quiet', mission = 10036})
    status = allocate(env, {endCall = false})
    check(status.result == 'apply' and status.applied == 'quiet' and status.type == PT.QUIET and status.bypass, tag .. 'quiet applied')
    check(has(env.receipts, ON) and not has(env.receipts, OFF) and env.bypass, tag .. 'quiet enables the native bypass')
    local temp = env.registered[1]
    check(temp.playerType == PT.QUIET and temp.partsType == PARTS.QUIET and temp.camoType == CAMO.QUIET and
        temp.handEquip == HAND_NORMAL and temp.faceEquipId == 0, tag .. 'quiet temporary type')
    check(env.vars.playerType == PT.QUIET and env.vars.playerPartsType == PARTS.QUIET and env.vars.playerCamoType == CAMO.QUIET,
        tag .. 'quiet applied by MissionStartPlayerTypeSetting')
    check(env.gvars.ply_isUsingTempPlayerType == true and env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE and
        env.gvars.ply_lastPlayerPartsTypeUsingTemp == PARTS.SNEAKING_SUIT, tag .. 'own type saved for restoration')
    check(lastReport(env):find('apply;mission=10036;requested=quiet;applied=quiet;type=6;reason=override;bypass=1;real=0', 1, true),
        tag .. 'quiet report')
    env.TppPlayer.ForceChangePlayerFromOcelot()
    check(env.vars.playerType == PT.QUIET and env.ocelotRevertCalls == 0, tag .. 'OnAllocate revert skipped once')
    check(lastReport(env):find('ocelot_reset_skipped;mission=10036', 1, true), tag .. 'skip reported')
    env.TppPlayer.ForceChangePlayerFromOcelot()
    check(env.vars.playerType == PT.SNAKE and env.ocelotRevertCalls == 1, tag .. 'later reverts (abort preparation) run')
    local hook = env.TppPlayer.ForceChangePlayerFromOcelot
    check(hook ~= env.originalRevert and hook == env.Tpp.MgsCoop.characterPostHook, tag .. 'hook installed')

    -- Next mission keeps one hook and restores through the vanilla path.
    env.vars.playerType = PT.QUIET
    status = allocate(env, {mission = 30010})
    check(status.applied == 'quiet' and env.TppPlayer.ForceChangePlayerFromOcelot == hook, tag .. 'hook installed once')
    check(env.vars.playerType == PT.QUIET and env.ocelotRevertCalls == 1, tag .. 'free roam keeps quiet')
    check(env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE, tag .. 'consecutive overrides keep the first saved type')
    status = allocate(env, {mission = 40010})
    check(status.result == 'skip' and status.reason == 'mission_class' and has(env.receipts, OFF) and not env.bypass,
        tag .. 'heli disables the bypass')
    check(#env.registered == 0 and env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.SNEAKING_SUIT and
        env.vars.playerCamoType == CAMO.SPLITTER and env.vars.handEquip == HAND_ROCKET and env.vars.playerFaceEquipId == 4 and
        env.gvars.ply_isUsingTempPlayerType == false, tag .. 'vanilla restore returns the own appearance')

    -- Ocelot in free roam.
    env = F({character = 'ocelot', mission = 30020})
    status = allocate(env)
    check(status.applied == 'ocelot' and env.bypass and env.vars.playerType == PT.OCELOT and
        env.vars.playerPartsType == PARTS.OCELOT and env.vars.playerCamoType == CAMO.OCELOT, tag .. 'ocelot applied')

    -- Quiet conflicts substitute the fallback.
    for _, mission in ipairs({10050, 11050, 10260}) do
        env = F({character = 'quiet', mission = mission})
        status = allocate(env)
        check(status.applied == 'dd_male' and status.reason == 'quiet_conflict' and not status.bypass, tag .. 'story quiet conflict')
        check(has(env.receipts, OFF) and not has(env.receipts, ON), tag .. 'conflict never enables the bypass')
        check(env.vars.playerType == PT.DD_MALE and env.vars.playerFaceId == 0 and env.vars.playerPartsType == PARTS.NORMAL and
            env.vars.playerCamoType == CAMO.TIGERSTRIPE, tag .. 'DD fallback with default male face and sortie DD outfit')
        check(env.gvars.ply_lastPlayerFaceIdUsingTemp == 77, tag .. 'own face saved before the DD face')
    end
    env = F({character = 'quiet', mission = 10036, buddyType = BUDDY.QUIET, fallback = 'dd_female'})
    status = allocate(env)
    check(status.applied == 'dd_female' and status.reason == 'buddy_quiet', tag .. 'buddy Quiet deployed')
    check(env.vars.playerType == PT.DD_FEMALE and env.vars.playerFaceId == 350, tag .. 'female fallback face')
    env = F({character = 'quiet', mission = 10050, fallback = 'quiet'})
    check(allocate(env).applied == 'dd_male', tag .. 'invalid fallback becomes dd_male')
    env = F({character = 'quiet', mission = 10050, fallback = 'snake'})
    status = allocate(env)
    check(status.applied == 'snake' and status.reason == 'quiet_conflict_sortie_kept' and #env.registered == 0,
        tag .. 'snake fallback keeps the sortie Snake')

    -- Bypass refused natively: fall back, never leave a Quiet request.
    env = F({character = 'quiet', mission = 10036, bypassRefused = true})
    status = allocate(env)
    check(status.applied == 'dd_male' and status.reason == 'bypass_unavailable' and has(env.receipts, OFF), tag .. 'refused bypass')
    check(env.vars.playerType == PT.DD_MALE and env.ocelotRevertCalls == 1, tag .. 'refused bypass keeps the revert')

    -- Story-forced missions keep their own type.
    for mission, reason in pairs({[10010] = 'story_hospital', [10280] = 'story_hospital', [10030] = 'story_temp_snake',
            [10240] = 'story_temp_snake', [10151] = 'story_snake_for_dd', [11043] = 'story_subsistence',
            [11044] = 'story_subsistence'}) do
        env = F({character = 'quiet', mission = mission})
        status = allocate(env)
        check(status.result == 'keep' and status.reason == reason and #env.registered == 0 and has(env.receipts, OFF),
            tag .. 'story forced ' .. mission)
    end
    env = F({character = 'ocelot', mission = 10240})
    status = allocate(env, {storyTemp = {partsType = PARTS.NORMAL, camoType = CAMO.OLIVEDRAB, playerType = PT.SNAKE,
        handEquip = HAND_NORMAL, faceEquipId = 0}})
    check(status.result == 'keep' and env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.NORMAL,
        tag .. 'Shining Lights temporary Snake wins')
    env = F({character = 'quiet', mission = 10085})
    status = allocate(env, {storyTemp = {playerType = PT.SNAKE}})
    check(status.reason == 'story_temp_type' and env.vars.playerType == PT.SNAKE, tag .. 'any MissionPrepare temporary type wins')

    -- Mission classes outside story/free roam.
    for _, mission in ipairs({1, 5, 6000, 40010, 40050, 50050, 65000}) do
        env = F({character = 'quiet', mission = mission})
        status = allocate(env)
        check(status.reason == 'mission_class' and #env.registered == 0 and has(env.receipts, OFF), tag .. 'class ' .. mission)
    end

    -- Snake / Avatar / DD against the sortie selection.
    env = F({character = 'snake', playerType = PT.SNAKE})
    status = allocate(env)
    check(status.reason == 'override_sortie_kept' and #env.registered == 0, tag .. 'snake sortie kept')
    env = F({character = 'snake', playerType = PT.DD_MALE, sortieType = PT.DD_MALE})
    status = allocate(env)
    check(env.registered[1].playerType == PT.SNAKE and env.registered[1].partsType == PARTS.SNEAKING_SUIT and
        env.vars.playerType == PT.SNAKE and env.vars.playerCamoType == CAMO.SPLITTER and env.vars.playerFaceEquipId == 4,
        tag .. 'snake from DD uses the sortie Snake outfit')
    env = F({character = 'avatar', playerType = PT.AVATAR})
    check(allocate(env).reason == 'override_sortie_kept', tag .. 'avatar sortie kept')
    env = F({character = 'avatar', playerType = PT.SNAKE})
    allocate(env)
    check(env.vars.playerType == PT.AVATAR and env.vars.playerPartsType == PARTS.SNEAKING_SUIT, tag .. 'avatar from snake')
    env = F({character = 'dd_male', playerType = PT.DD_FEMALE, sortieType = PT.DD_FEMALE})
    status = allocate(env)
    check(status.reason == 'override_sortie_kept' and env.vars.playerType == PT.DD_FEMALE, tag .. 'any DD sortie staff kept')
    env = F({character = 'dd_male', playerType = PT.SNAKE, sortieType = PT.DD_MALE, sortieFace = 123})
    allocate(env)
    check(env.vars.playerType == PT.DD_MALE and env.vars.playerFaceId == 123, tag .. 'matching sortie DD face reused')
    env = F({character = 'dd_female', playerType = PT.QUIET, usingTemp = true, lastType = PT.DD_FEMALE})
    env.gvars.ply_lastPlayerFaceIdUsingTemp = 360
    status = allocate(env)
    check(status.reason == 'override_sortie_kept' and env.vars.playerType == PT.DD_FEMALE and env.vars.playerFaceId == 360,
        tag .. 'real type comes from the saved temporary state')

    -- Checkpoint continue: no type setting call, the saved Quiet is protected.
    env = F({character = 'quiet', mission = 10036, playerType = PT.QUIET})
    status = allocate(env, {continue = true, endCall = false})
    check(status.applied == 'quiet' and env.bypass, tag .. 'continue requests the bypass')
    env.TppPlayer.ForceChangePlayerFromOcelot()
    check(env.vars.playerType == PT.QUIET, tag .. 'continue keeps the checkpoint Quiet')

    -- The armed skip belongs to its mission only.
    env = F({character = 'quiet', mission = 10036})
    allocate(env, {endCall = false})
    env.vars.missionCode = 40010
    env.TppPlayer.ForceChangePlayerFromOcelot()
    check(env.vars.playerType == PT.SNAKE and env.ocelotRevertCalls == 1, tag .. 'mission change disarms the skip')

    -- Unknown names and hostile text from the receipt are sanitized.
    env = F({character = 'qu iet;x'})
    status = allocate(env)
    check(status.reason == 'unknown_character' and has(env.receipts, OFF), tag .. 'unknown character')
    check(lastReport(env):find('requested=qu_iet;x', 1, true) and not lastReport(env):find(' ', 1, true), tag .. 'sanitized report')

    -- Failures never escape into OnAllocate.
    env = F({character = 'quiet', registerThrows = true})
    allocate(env)
    check(env.Tpp.MgsCoop.characterError and lastReport(env) == 'error;reason=lua_error', tag .. 'register failure contained')
    env = F({character = 'quiet', compileNil = true})
    check(allocate(env).reason == 'default', tag .. 'uncompilable receipt treated as default')
    env = F({character = 'quiet', noCoop = true})
    setfenv(characterChunk, env); characterChunk()
    check(#env.receipts == 0 and #env.registered == 0, tag .. 'missing Tpp.MgsCoop does nothing')
end

scenarios(false)
if realPlayerChunk then scenarios(true) end

-- Pure decision table.
do
    local env = fixture({character = ''})
    allocate(env)
    local C = env.Tpp.MgsCoop.Character
    local d = C.Decide({character = 'quiet', fallback = 'avatar', mission = 10036})
    check(d.result == 'apply' and d.applied == 'quiet' and d.fallback == 'avatar' and d.bypass, 'decide quiet')
    d = C.Decide({character = 'quiet', fallback = 'avatar', mission = 10260})
    check(d.applied == 'avatar' and not d.bypass, 'decide conflict')
    d = C.Decide({character = 'ocelot', mission = 10260})
    check(d.applied == 'ocelot' and d.bypass, 'Ocelot has no Quiet conflict')
    d = C.Decide({character = 'quiet', mission = nil})
    check(d.reason == 'mission_class', 'missing mission code')
    d = C.Decide({character = 'snake', mission = 10036, subsistence = true})
    check(d.reason == 'story_subsistence', 'subsistence predicate')
    local E = {PlayerType = PT, PlayerPartsType = PARTS, PlayerCamoType = CAMO, handNormal = HAND_NORMAL}
    local temp, face = C.Plan('dd_female', {realType = PT.AVATAR, sortie = {}}, E)
    check(temp.playerType == PT.DD_FEMALE and temp.partsType == PARTS.NORMAL and temp.camoType == CAMO.OLIVEDRAB and face == 350,
        'plan DD without sortie values')
    check(C.Plan('quiet', {realType = PT.QUIET}, E).playerType == PT.QUIET, 'plan quiet regardless of current type')
end

-- Readback and polling.
do
    local env = fixture({character = '', readback = false, instanceTypes = {[0] = 6, [1] = 1}, playerType = PT.QUIET})
    allocate(env)
    local C = env.Tpp.MgsCoop.Character
    check(C.Readback('manual') == 'local=0;vars=6;i0=0;i1=1;quiet_ocelot_read_as=0', 'readback maps quiet to 0')
    check(env.readbacks[#env.readbacks] == 'manual;local=0;vars=6;i0=0;i1=1;quiet_ocelot_read_as=0', 'readback logged')
    check(C.Poll('start') == false and C.Poll(nil) == false, 'poll disabled without native readback')
    env = fixture({character = '', readback = true, localIndex = 1, instanceTypes = {[0] = 0, [1] = 2}, playerType = PT.DD_FEMALE})
    allocate(env)
    C = env.Tpp.MgsCoop.Character
    env.readbacks = {}
    check(C.Poll('start') == true and env.readbacks[1] == 'start;local=1;vars=2;i0=0;i1=2;quiet_ocelot_read_as=0', 'start readback')
    for _ = 1, 59 do check(C.Poll(nil) == false, 'rate limited') end
    check(C.Poll(nil) == false and #env.readbacks == 1, 'unchanged values not repeated')
    env.instanceTypes[0] = 1
    for _ = 1, 59 do C.Poll(nil) end
    check(C.Poll(nil) == true and env.readbacks[2] == 'change;local=1;vars=2;i0=1;i1=2;quiet_ocelot_read_as=0', 'change logged')
    for i = 3, 70 do C.Poll('tag' .. i) end
    check(#env.readbacks == 64, 'at most 64 readbacks per mission')
    allocate(env)
    env.readbacks = {}
    check(env.Tpp.MgsCoop.Character ~= C, 'module table is replaced on allocation')
    check(env.Tpp.MgsCoop.Character.Poll('start') == true and #env.readbacks == 1, 'limit resets per allocation')
    env = fixture({character = '', readback = true, readbackThrows = true})
    allocate(env)
    check(env.Tpp.MgsCoop.Character.Readback('x'):find('i0=err;i1=err', 1, true), 'readback failure reported as err')
    env.Player = nil
    check(env.Tpp.MgsCoop.Character.Snapshot():find('i0=err', 1, true), 'missing Player table tolerated')
end

-- Appended readback wrapper: arguments, results, errors and yields unchanged.
do
    local sentinel = {}
    local env = fixture({character = '', readback = true})
    allocate(env)
    local polled = {}
    env.Tpp.MgsCoop.Character.Poll = function(tag) polled[#polled + 1] = tag or 'update' end
    local events = {}
    env.e = {}
    local mode = nil
    for _, name in ipairs({'OnMissionGameStart', 'OnUpdate'}) do
        env.e[name] = function(...)
            events[#events + 1] = name
            check(select('#', ...) == 3 and select(1, ...) == 7 and select(2, ...) == nil and select(3, ...) == 9, 'original arguments')
            if mode == 'error' then error(sentinel) end
            if mode == 'yield' then coroutine.yield('original yield') end
            return 1, nil, 3
        end
    end
    local chunk = assert(loadstring(readbackSource, '=character_readback.lua'))
    setfenv(chunk, env); chunk()
    local function returns(...)
        check(select('#', ...) == 3 and select(1, ...) == 1 and select(2, ...) == nil and select(3, ...) == 3, 'original returns')
    end
    returns(env.e.OnMissionGameStart(7, nil, 9))
    returns(env.e.OnUpdate(7, nil, 9))
    check(polled[1] == 'start' and polled[2] == 'update' and #events == 2, 'poll before each original')
    mode = 'error'
    local ok, err = pcall(env.e.OnUpdate, 7, nil, 9)
    check(not ok and err == sentinel, 'original error unchanged')
    mode = 'yield'
    local co = coroutine.create(function() returns(env.e.OnUpdate(7, nil, 9)) end)
    local resumed, value = coroutine.resume(co)
    check(resumed and value == 'original yield' and coroutine.resume(co), 'original yield crosses the wrapper')
    mode = nil
    env.Tpp.MgsCoop.Character.Poll = function() error('poll failure') end
    returns(env.e.OnUpdate(7, nil, 9))
    env.Tpp.MgsCoop.Character = nil
    returns(env.e.OnMissionGameStart(7, nil, 9))
    local bare = fixture({character = ''})
    bare.e = {}
    setfenv(chunk, bare); chunk()
    check(bare.e.OnUpdate == nil and bare.e.OnMissionGameStart == nil, 'absent callbacks stay absent')
end

-- Real TppMain: unique anchor and a compilable composition.
if REAL_MAIN then
    local main = readfile(REAL_MAIN)
    local anchor = 'if t(n.sequence.MissionPrepare)then\nn.sequence.MissionPrepare()end\n' ..
        'if t(n.sequence.OnEndMissionPrepareSequence)then\n' ..
        'TppSequence.SetOnEndMissionPrepareFunction(n.sequence.OnEndMissionPrepareSequence)end\nend\n'
    local count, at, first = 0, 1, nil
    while true do
        local found = main:find(anchor, at, true)
        if not found then break end
        count, at, first = count + 1, found + 1, first or found
    end
    check(#main == 21634 and count == 1, 'TppMain anchor is unique in the fingerprinted source')
    local insertion = first + #anchor
    local function after(text) return main:find(text, insertion, true) end
    check(main:find('Mission.Start()', 1, true) < first, 'after Mission.Start (host trial anchor)')
    check(after('TppPlayer.MissionStartPlayerTypeSetting()') and after('TppEquip.StartLoadingToEquipMissionBlock()') and
        after('TppPlayer.ForceChangePlayerFromOcelot()'), 'before type setting, equipment anchor and Ocelot revert')
    check(not main:sub(1, insertion - 1):find('MissionStartPlayerTypeSetting', 1, true), 'no type setting before the insertion')
    check(main:sub(-8) == 'return e', 'TppMain ends with return e')
    local composed = main:sub(1, insertion - 1) .. '\n' .. characterSource .. '\n' .. main:sub(insertion, -9) ..
        '\n' .. readbackSource .. '\n' .. 'return e'
    check(loadstring(composed, '=TppMain-character') ~= nil, 'composed TppMain compiles')
end

print(checks .. ' character Lua checks passed: default/unwired skips, Quiet/Ocelot override with bypass and one-shot revert skip, ' ..
    'Quiet conflicts and fallbacks, story-forced missions, mission classes, sortie-kept Snake/Avatar/DD, DD faces, continue, ' ..
    'failures, readback/poll, appended wrapper' .. (realPlayerChunk and ', real TppPlayer functions' or '') ..
    (REAL_MAIN and ', real TppMain anchor/composition' or '') .. '; no game or native calls')
