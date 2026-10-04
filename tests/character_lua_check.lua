-- Exercises lua/coop/character.lua (TppMain.OnAllocate splice) and
-- lua/coop/character_readback.lua (TppMain append) against a simulated engine:
-- native receipts, vars/mvars/gvars, a TppPlayer that mirrors the 1.0.15.4
-- temporary player-type functions, a mirror of TppVarInit.InitializeForContinue
-- (checkpoint continue: vars reloaded, type setting only with the mission's
-- ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING flag), plus a simulated
-- Player.GetPlayerType that counts a crash whenever it runs while the player
-- implementation is absent (the real binding has no null check). Optional
-- arguments add the real extracted TppPlayer.lua (its functions replace the
-- mirror), TppMain.lua (anchor, composed-source compile, OnAllocate and
-- readback lifecycle runs) and TppVarInit.lua (its InitializeForContinue
-- replaces the mirror in the real OnAllocate runs).
-- No MGSV, Steam or native code is involved.
-- Usage: lua51 character_lua_check.lua character.lua character_readback.lua [TppPlayer.lua [TppMain.lua
--        [composed.lua]]] [--varinit=TppVarInit.lua] where composed.lua is the complete DLL TppMain
--        composition written by host_trial_patch_check (after the bootstrap_patch integration).
local checks = 0
local function check(value, why)
    checks = checks + 1
    assert(value, tostring(checks) .. ': ' .. (why or 'check failed'))
end
local function readfile(path)
    local file = assert(io.open(path, 'rb'))
    local text = file:read('*a')
    file:close()
    -- git core.autocrlf checks sources out with CRLF; the DLL composition is LF.
    return (text:gsub('\r\n', '\n'))
end
local positional, VARINIT = {}, nil
for i = 1, #arg do
    if arg[i]:sub(1, 10) == '--varinit=' then VARINIT = arg[i]:sub(11) else positional[#positional + 1] = arg[i] end
end
local CHARACTER, READBACK, REAL_PLAYER, REAL_MAIN, FULL_COMPOSITION = positional[1], positional[2], positional[3],
    positional[4], positional[5]
assert(CHARACTER and READBACK, 'usage: character_lua_check.lua character.lua character_readback.lua [TppPlayer.lua [TppMain.lua]]')
local characterSource = readfile(CHARACTER)
local readbackSource = readfile(READBACK)
-- MSVC limits one raw string literal to 16380 bytes (C2026).
check(#characterSource < 16000 and #readbackSource < 16000, 'sources fit one MSVC string literal')
check(not characterSource:find(')MGS_CHARACTER"', 1, true) and not readbackSource:find(')MGS_CHAR_READ"', 1, true),
    'raw-string delimiters absent')
-- tests/host_trial_patch_check.cpp locates both chunks by these first lines.
local function startsWith(text, prefix) return text:sub(1, #prefix) == prefix end
check(startsWith(characterSource, '-- MGS co-op character selection (original).'), 'character chunk header')
check(startsWith(readbackSource, '-- MGS co-op character readback (original).'), 'readback chunk header')

local QUERY = "return 'MGS_COOP_CHARACTER_V1'"
local ON = "return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'"
local OFF = "return 'MGS_COOP_CHARACTER_BYPASS_OFF_V1'"
local REPORT = "return 'MGS_COOP_CHARACTER_REPORT_V1' -- "
local READ = "return 'MGS_COOP_CHARACTER_READBACK_V1' -- "
local PLAYERS = "return 'MGS_COOP_CHARACTER_PLAYERS_V1'"
local ALWAYS = 'ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING'
local PT = {SNAKE = 0, DD_MALE = 1, DD_FEMALE = 2, AVATAR = 3, LIQUID = 4, OCELOT = 5, QUIET = 6}
local PARTS = {NORMAL = 0, NORMAL_SCARF = 1, HOSPITAL = 3, SNEAKING_SUIT = 2, OCELOT = 26, QUIET = 27}
local CAMO = {OLIVEDRAB = 0, TIGERSTRIPE = 1, HOSPITAL = 2, SPLITTER = 3, OCELOT = 115, QUIET = 116}
local HAND_NORMAL, HAND_ROCKET = 7001, 7002
local BUDDY = {NONE = 0, QUIET = 2, DOG = 3}
local QUIET_TEMP = {playerType = PT.QUIET, partsType = PARTS.QUIET, camoType = CAMO.QUIET, handEquip = HAND_NORMAL,
    faceEquipId = 0}

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
        local v, m = env.vars, env.mvars
        env.typeSettingCalls = env.typeSettingCalls + 1
        if not m.ply_isExistTempPlayerType then P.RestoreTemporaryPlayerType() end
        if env.TppStory.GetCurrentStorySequence() == env.TppDefine.STORY_SEQUENCE.CLEARD_ESCAPE_THE_HOSPITAL then
            v.playerType, v.playerPartsType = PT.SNAKE, PARTS.NORMAL_SCARF
            v.playerCamoType, v.playerHandType = CAMO.TIGERSTRIPE, env.PlayerHandType.NORMAL
        end
        if m.ply_isExistTempPlayerType then P.SaveCurrentPlayerType(); P.ApplyTemporaryPlayerType() end
        if v.missionCode ~= 10010 and v.missionCode ~= 10280 then
            if v.playerCamoType == CAMO.HOSPITAL then v.playerCamoType = CAMO.OLIVEDRAB end
            if v.playerPartsType == PARTS.HOSPITAL then v.playerPartsType = PARTS.NORMAL end
        end
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

-- Mirror of TppVarInit.InitializeForContinue's player-type part (1.0.15.4):
-- TppSave.VarRestoreOnContinueFromCheckPoint reloads vars/gvars from the
-- CHECK_POINT slot, and MissionStartPlayerTypeSetting runs only when the
-- mission's sequence sets ALWAYS_APPLY_TEMPORATY_PLAYER_PARTS_SETTING.
local function continueFromCheckpoint(env, scripts, checkpoint)
    checkpoint = checkpoint or {}
    for key, value in pairs(checkpoint.vars or {}) do env.vars[key] = value end
    for key, value in pairs(checkpoint.gvars or {}) do env.gvars[key] = value end
    if scripts.sequence and scripts.sequence[ALWAYS] then env.TppPlayer.MissionStartPlayerTypeSetting() end
end

-- A stub that answers any field with itself and any call with nil, for the
-- engine tables the real TppPlayer.lua captures at load time.
local stub = setmetatable({}, {__call = function() return nil end})
getmetatable(stub).__index = function() return stub end

local realPlayerChunk = REAL_PLAYER and assert(loadfile(REAL_PLAYER)) or nil
-- Report/readback payloads that reach C.Log's 192-byte cut (would be truncated).
local longPayloads = 0
local realVarInitChunk = VARINIT and assert(loadfile(VARINIT)) or nil

local function fixture(o)
    o = o or {}
    local env = {receipts = {}, reports = {}, readbacks = {}, bypass = o.bypassInitially or false,
        ocelotRevertCalls = 0, typeSettingCalls = 0, registered = {}, getPlayerTypeCalls = 0, crashes = 0,
        truncated = 0, continuing = false}
    env.vars = {missionCode = o.mission or 10036, playerType = o.playerType or PT.SNAKE,
        playerPartsType = o.parts or PARTS.SNEAKING_SUIT, playerCamoType = o.camo or CAMO.SPLITTER,
        handEquip = o.hand or HAND_ROCKET, playerFaceId = o.face or 77, playerFaceEquipId = o.faceEquip or 4,
        buddyType = o.buddyType or BUDDY.NONE,
        sortiePrepPlayerType = o.sortieType or o.playerType or PT.SNAKE, sortiePrepPlayerFaceId = o.sortieFace or 77,
        sortiePrepPlayerSnakePartsType = PARTS.SNEAKING_SUIT, sortiePrepPlayerSnakeCamoType = CAMO.SPLITTER,
        sortiePrepPlayerSnakeFaceEquipId = 4, sortiePrepPlayerDDPartsType = PARTS.NORMAL,
        sortiePrepPlayerDDCamoType = CAMO.TIGERSTRIPE, sortiePrepPlayerDDFaceEquipId = 0}
    env.mvars = {}
    env.gvars = {ply_isUsingTempPlayerType = o.usingTemp or false, ply_lastPlayerTypeUsingTemp = o.lastType,
        ini_isTitleMode = o.titleMode == true}
    -- OnAllocate's mission-script table, reached by the chunk as `n`.
    env.n = o.scripts or {sequence = {}}
    env.PlayerType, env.PlayerPartsType, env.PlayerCamoType = PT, PARTS, CAMO
    env.PlayerHandType = {NONE = 0, NORMAL = 1}
    env.TppEquip = {EQP_HAND_NORMAL = HAND_NORMAL, EQP_SUIT = 99}
    env.BuddyType = BUDDY
    env.TppDefine = {STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 5}}
    env.TppStory = {GetCurrentStorySequence = function() return o.storySequence or 20 end}
    env.TppMission = {IsSubsistenceMission = function()
        return env.vars.missionCode == 11043 or env.vars.missionCode == 11044 end}
    if not o.noMissionStart then env.TppMission.IsMissionStart = function() return not env.continuing end end
    -- Simulated player implementation. Like the binding at 0x9d30b0, which
    -- dereferences [P+0x208] with no null check, GetPlayerType "crashes" (is
    -- counted and raises) whenever it runs while the implementation is absent.
    env.players = {present = o.playersPresent == true, limit = o.limit or 2,
        types = o.instanceTypes or {[0] = 0, [1] = 1}}
    env.Player = {
        GetPlayerType = function(t)
            env.getPlayerTypeCalls = env.getPlayerTypeCalls + 1
            if not env.players.present then
                env.crashes = env.crashes + 1
                error('access violation: player implementation is null')
            end
            if o.readbackThrows then error('readback failure') end
            if t.instanceIndex >= env.players.limit then return 0 end
            local value = env.players.types[t.instanceIndex] or 0
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
        elseif source == PLAYERS then
            -- Native guard (player_type::SampleTypes + FormatPlayersReply).
            if o.playersNative == false then return function() return 'MGS_COOP_CHARACTER_PLAYERS_V1' end end
            return function()
                local players = env.players
                if not players.present then return false, players.stage or 'player_absent' end
                if players.limit > 1 then return true, players.limit, players.types[0], players.types[1] end
                return true, players.limit, players.types[0]
            end
        elseif source:sub(1, #REPORT) == REPORT then
            if o.reportThrows then error('report failure') end
            env.reports[#env.reports + 1] = source:sub(#REPORT + 1)
            if #source - #REPORT >= 192 then longPayloads = longPayloads + 1 end
        elseif source:sub(1, #READ) == READ then
            local payload = source:sub(#READ + 1)
            if #payload >= 192 then longPayloads = longPayloads + 1 end
            -- The native side accepts at most 192 payload bytes; a full body ends with this field.
            if #payload > 192 or payload:sub(-22) ~= 'quiet_ocelot_read_as=0' then env.truncated = env.truncated + 1 end
            env.readbacks[#env.readbacks + 1] = payload
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
        -- Count calls without changing the real functions' behaviour.
        local original = env.TppPlayer.ForceChangePlayerFromOcelot
        env.TppPlayer.ForceChangePlayerFromOcelot = function(...)
            env.ocelotRevertCalls = env.ocelotRevertCalls + 1
            return original(...)
        end
        local setting = env.TppPlayer.MissionStartPlayerTypeSetting
        env.TppPlayer.MissionStartPlayerTypeSetting = function(...)
            env.typeSettingCalls = env.typeSettingCalls + 1
            return setting(...)
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
local readbackChunk = assert(loadstring(readbackSource, '=character_readback.lua'))
-- One OnAllocate: MissionPrepare (optional story temporary type), the chunk,
-- then the vanilla order. New mission: MissionStartPlayerTypeSetting.
-- Checkpoint continue (options.continue): InitializeForContinue. Unexpected
-- path (options.typeSetting == false): neither. Then the end-of-OnAllocate
-- ForceChangePlayerFromOcelot unless options.endCall == false.
local function allocate(env, options)
    options = options or {}
    env.mvars = {}
    if options.mission then env.vars.missionCode = options.mission end
    if options.scripts then env.n = options.scripts end
    env.continuing = options.continue == true
    if options.storyTemp then env.TppPlayer.RegisterTemporaryPlayerType(options.storyTemp) end
    env.receipts, env.reports, env.registered = {}, {}, {}
    setfenv(characterChunk, env)
    characterChunk()
    local status = env.Tpp and env.Tpp.MgsCoop and env.Tpp.MgsCoop.characterStatus
    if options.typeSetting == false then
        -- nothing
    elseif env.continuing then
        continueFromCheckpoint(env, env.n, options.checkpoint)
    else
        env.TppPlayer.MissionStartPlayerTypeSetting()
    end
    if options.endCall ~= false then env.TppPlayer.ForceChangePlayerFromOcelot() end
    return status
end
-- The appended readback module (defined once per TppMain load).
local function readbackModule(env)
    env.e = env.e or {}
    setfenv(readbackChunk, env)
    readbackChunk()
    return env.Tpp.MgsCoop.CharacterReadback
end
local function has(list, value) for _, item in ipairs(list) do if item == value then return true end end return false end
local function hasText(list, text)
    for _, item in ipairs(list) do if item:find(text, 1, true) then return true end end
    return false
end
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
    check(env.n.sequence[ALWAYS] == nil and env.Tpp.MgsCoop.characterAlways == nil, tag .. 'default leaves the mission flag')
    env = F({character = 'default'}); check(allocate(env).reason == 'default', tag .. 'explicit default skips')
    env = F({character = '', usingTemp = true, lastType = PT.DD_MALE})
    allocate(env, {continue = true})
    check(env.n.sequence[ALWAYS] == nil and env.typeSettingCalls == 0 and env.vars.playerType == PT.SNAKE,
        tag .. 'default continue is vanilla (no type setting)')

    -- Native receipt not wired (older DLL): the literal comes back.
    env = F({character = 'quiet', native = false})
    status = allocate(env)
    check(status.reason == 'native_unavailable' and #env.receipts == 1 and #env.registered == 0, tag .. 'unwired native skips')

    -- Quiet in an ordinary story mission (new mission start).
    env = F({character = 'quiet', mission = 10036})
    status = allocate(env, {endCall = false})
    check(status.result == 'apply' and status.applied == 'quiet' and status.type == PT.QUIET and status.bypass and
        status.expect == PT.QUIET, tag .. 'quiet applied')
    check(has(env.receipts, ON) and not has(env.receipts, OFF) and env.bypass, tag .. 'quiet enables the native bypass')
    local temp = env.registered[1]
    check(temp.playerType == PT.QUIET and temp.partsType == PARTS.QUIET and temp.camoType == CAMO.QUIET and
        temp.handEquip == HAND_NORMAL and temp.faceEquipId == 0, tag .. 'quiet temporary type')
    check(env.vars.playerType == PT.QUIET and env.vars.playerPartsType == PARTS.QUIET and env.vars.playerCamoType == CAMO.QUIET,
        tag .. 'quiet applied by MissionStartPlayerTypeSetting')
    check(env.gvars.ply_isUsingTempPlayerType == true and env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE and
        env.gvars.ply_lastPlayerPartsTypeUsingTemp == PARTS.SNEAKING_SUIT, tag .. 'own type saved for restoration')
    check(lastReport(env) ==
        'apply;mission=10036;requested=quiet;applied=quiet;type=6;reason=override;bypass=1;real=0;start=new;always=1',
        tag .. 'quiet report')
    check(env.n.sequence[ALWAYS] == true, tag .. 'continue flag set during the allocation')
    env.TppPlayer.ForceChangePlayerFromOcelot()
    check(env.vars.playerType == PT.QUIET and env.ocelotRevertCalls == 0, tag .. 'OnAllocate revert skipped once')
    check(env.n.sequence[ALWAYS] == nil and env.Tpp.MgsCoop.characterAlways == nil, tag .. 'continue flag restored')
    check(has(env.reports, 'allocated;mission=10036;vars=6;parts=27;camo=116;temp=1;start=new;late=0') and
        lastReport(env) == 'ocelot_reset_skipped;mission=10036', tag .. 'allocated and skip reported')
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
        check(has(env.reports, 'face;mission=' .. mission .. ';face=0'), tag .. 'face reported')
    end
    env = F({character = 'quiet', mission = 10036, buddyType = BUDDY.QUIET, fallback = 'dd_female'})
    status = allocate(env)
    check(status.applied == 'dd_female' and status.reason == 'buddy_quiet', tag .. 'buddy Quiet deployed')
    check(env.vars.playerType == PT.DD_FEMALE and env.vars.playerFaceId == 350, tag .. 'female fallback face')
    env = F({character = 'quiet', mission = 10050, fallback = 'quiet'})
    check(allocate(env).applied == 'dd_male', tag .. 'invalid fallback becomes dd_male')
    env = F({character = 'quiet', mission = 10050, fallback = 'snake'})
    status = allocate(env)
    check(status.applied == 'snake' and status.reason == 'quiet_conflict_sortie_kept' and #env.registered == 0 and
        status.expect == PT.SNAKE and not status.always, tag .. 'snake fallback keeps the sortie Snake')
    check(env.TppPlayer.ForceChangePlayerFromOcelot ~= env.originalRevert and env.n.sequence[ALWAYS] == nil and
        env.ocelotRevertCalls == 1 and not has(env.receipts, ON) and not hasText(env.reports, 'ocelot_reset_skipped'),
        tag .. 'sortie kept without an earlier override: hook armed, no flag, vanilla revert still runs')
    check(has(env.reports, 'allocated;mission=10050;vars=0;parts=2;camo=3;temp=0;start=new;late=0'),
        tag .. 'sortie kept still writes its allocated line')

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
        check(status.result == 'keep' and status.reason == reason and #env.registered == 0 and has(env.receipts, OFF) and
            env.n.sequence[ALWAYS] == nil, tag .. 'story forced ' .. mission)
    end
    env = F({character = 'ocelot', mission = 10240})
    status = allocate(env, {storyTemp = {partsType = PARTS.NORMAL, camoType = CAMO.OLIVEDRAB, playerType = PT.SNAKE,
        handEquip = HAND_NORMAL, faceEquipId = 0}})
    check(status.result == 'keep' and env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.NORMAL,
        tag .. 'Shining Lights temporary Snake wins')
    env = F({character = 'quiet', mission = 10085})
    status = allocate(env, {storyTemp = {playerType = PT.SNAKE}})
    check(status.reason == 'story_temp_type' and env.vars.playerType == PT.SNAKE, tag .. 'any MissionPrepare temporary type wins')

    -- MissionStartPlayerTypeSetting forces Snake while the story sequence is
    -- CLEARD_ESCAPE_THE_HOSPITAL (e.g. 10020 after the prologue); a registered
    -- temporary type would be applied over it, so the chunk must keep.
    for _, name in ipairs({'quiet', 'ocelot', 'dd_female', 'avatar'}) do
        env = F({character = name, mission = 10020, storySequence = 5, playerType = PT.DD_MALE, sortieType = PT.DD_MALE})
        status = allocate(env)
        check(status.result == 'keep' and status.reason == 'story_sequence_snake' and #env.registered == 0 and
            has(env.receipts, OFF) and not has(env.receipts, ON) and not env.bypass, tag .. 'story sequence keeps ' .. name)
        check(env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.NORMAL_SCARF and
            env.vars.playerCamoType == CAMO.TIGERSTRIPE and env.ocelotRevertCalls == 1, tag .. 'hospital-sequence Snake forced')
        check(lastReport(env):find('keep;mission=10020;requested=' .. name .. ';applied=nil;type=nil;reason=story_sequence_snake;bypass=0',
            1, true), tag .. 'story sequence report')
    end
    env = F({character = 'quiet', mission = 30010, storySequence = 5})
    check(allocate(env).reason == 'story_sequence_snake', tag .. 'story sequence applies to free roam too')
    -- Control: the same registration the chunk would make overrides the force.
    env = F({character = '', mission = 10020, storySequence = 5})
    allocate(env, {storyTemp = QUIET_TEMP, endCall = false})
    check(env.vars.playerType == PT.QUIET, tag .. 'a temporary type wins over the story-sequence force (why keep is needed)')
    env = F({character = 'quiet', mission = 10020, storySequence = 6})
    check(allocate(env).result == 'apply', tag .. 'later story sequences apply')

    -- Title mode (TppMain runs MissionStartPlayerTypeSetting for the title scene).
    env = F({character = 'quiet', mission = 30050, titleMode = true})
    status = allocate(env)
    check(status.result == 'skip' and status.reason == 'title_mode' and #env.registered == 0 and has(env.receipts, OFF) and
        not has(env.receipts, ON) and env.vars.playerType == PT.SNAKE and env.n.sequence[ALWAYS] == nil,
        tag .. 'title mode skips')

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
    check(has(env.reports, 'allocated;mission=10036;vars=0;parts=2;camo=3;temp=0;start=new;late=0') and
        env.ocelotRevertCalls == 1 and env.n.sequence[ALWAYS] == nil and env.Tpp.MgsCoop.characterPost.armed == false,
        tag .. 'snake sortie kept: allocated line, hook consumed, vanilla revert ran')
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

    -- Checkpoint continue (title Continue into 30050, the October 3 live run):
    -- InitializeForContinue reloads the checkpoint (Snake) and runs the type
    -- setting only because the chunk set the mission's continue flag.
    env = F({character = 'quiet', mission = 30050})
    status = allocate(env, {continue = true, checkpoint = {vars = {playerType = PT.SNAKE,
        playerPartsType = PARTS.SNEAKING_SUIT, playerCamoType = CAMO.SPLITTER}, gvars = {ply_isUsingTempPlayerType = false}}})
    check(status.applied == 'quiet' and status.always == true and env.bypass and env.typeSettingCalls == 1,
        tag .. 'continue: flag makes InitializeForContinue run the type setting')
    check(env.vars.playerType == PT.QUIET and env.vars.playerPartsType == PARTS.QUIET and env.vars.playerCamoType == CAMO.QUIET and
        env.vars.handEquip == HAND_NORMAL, tag .. 'continue: checkpoint Snake becomes Quiet')
    check(env.gvars.ply_isUsingTempPlayerType == true and env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE and
        env.gvars.ply_lastPlayerCamoTypeUsingTemp == CAMO.SPLITTER, tag .. 'continue: own checkpoint type saved for restoration')
    check(env.ocelotRevertCalls == 0 and env.n.sequence[ALWAYS] == nil and env.Tpp.MgsCoop.characterAlways == nil,
        tag .. 'continue: revert skipped, flag restored after OnAllocate')
    check(hasText(env.reports, 'apply;mission=30050;requested=quiet;applied=quiet;type=6;reason=override;bypass=1;real=0;' ..
        'start=continue;always=1') and has(env.reports, 'allocated;mission=30050;vars=6;parts=27;camo=116;temp=1;start=continue;late=0'),
        tag .. 'continue: decision and allocated reports')
    -- Control (root cause of the live failure): without the flag the
    -- registered temporary type is ignored on continue.
    env = F({character = '', mission = 30050})
    allocate(env, {continue = true, storyTemp = QUIET_TEMP, endCall = false})
    check(env.vars.playerType == PT.SNAKE and env.typeSettingCalls == 0 and env.gvars.ply_isUsingTempPlayerType == false,
        tag .. 'control: vanilla continue ignores a temporary type')
    -- Checkpoint saved while Quiet was applied: stays Quiet, own type kept.
    env = F({character = 'quiet', mission = 30050, playerType = PT.QUIET, parts = PARTS.QUIET, camo = CAMO.QUIET,
        usingTemp = true, lastType = PT.SNAKE})
    status = allocate(env, {continue = true, endCall = false})
    check(status.applied == 'quiet' and status.reason == 'override' and env.bypass, tag .. 'continue requests the bypass')
    env.TppPlayer.ForceChangePlayerFromOcelot()
    check(env.vars.playerType == PT.QUIET and env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE,
        tag .. 'continue keeps the checkpoint Quiet and the first saved type')
    -- The mission's own flag is left alone (and stays set).
    env = F({character = 'quiet', mission = 30050, scripts = {sequence = {[ALWAYS] = true}}})
    status = allocate(env, {continue = true})
    check(status.always == false and env.n.sequence[ALWAYS] == true and env.vars.playerType == PT.QUIET,
        tag .. 'mission flag owned by the mission is untouched')
    check(lastReport(env) == 'ocelot_reset_skipped;mission=30050' and hasText(env.reports, ';always=0'),
        tag .. 'always=0 reported')
    -- A previous false value is put back exactly.
    env = F({character = 'quiet', mission = 30050, scripts = {sequence = {[ALWAYS] = false}}})
    allocate(env, {continue = true})
    check(env.n.sequence[ALWAYS] == false and env.vars.playerType == PT.QUIET, tag .. 'previous false value restored')
    -- Missing sequence table: nothing to flag; the end-of-OnAllocate net applies.
    env = F({character = 'quiet', mission = 30050, scripts = {}})
    status = allocate(env, {continue = true})
    check(status.always == false and env.vars.playerType == PT.QUIET and
        hasText(env.reports, 'allocated;mission=30050;vars=6;parts=27;camo=116;temp=1;start=continue;late=1;before=0'),
        tag .. 'no sequence: late application at the end of OnAllocate')
    -- DD fallback with face on continue.
    env = F({character = 'quiet', mission = 10050})
    allocate(env, {continue = true})
    check(env.vars.playerType == PT.DD_MALE and env.vars.playerFaceId == 0 and env.gvars.ply_lastPlayerFaceIdUsingTemp == 77,
        tag .. 'continue: DD fallback and face')
    -- Checkpoint saved under an earlier override, character now kept from the
    -- own type: the flag makes the continue restore the own type.
    env = F({character = 'snake', mission = 30050, playerType = PT.DD_FEMALE, usingTemp = true, lastType = PT.SNAKE})
    env.gvars.ply_lastPlayerPartsTypeUsingTemp, env.gvars.ply_lastPlayerCamoTypeUsingTemp = PARTS.SNEAKING_SUIT, CAMO.SPLITTER
    status = allocate(env, {continue = true})
    check(status.reason == 'override_sortie_kept' and status.always == true and status.expect == PT.SNAKE and
        #env.registered == 0, tag .. 'stale override: sortie kept with the flag')
    check(env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.SNEAKING_SUIT and
        env.gvars.ply_isUsingTempPlayerType == false and env.n.sequence[ALWAYS] == nil, tag .. 'stale override restored on continue')
    check(has(env.reports, 'allocated;mission=30050;vars=0;parts=2;camo=3;temp=0;start=continue;late=0'),
        tag .. 'stale override allocated report')

    -- Unexpected path (no type setting at all): applied late, the vanilla way.
    env = F({character = 'quiet', mission = 30050})
    allocate(env, {typeSetting = false})
    check(env.vars.playerType == PT.QUIET and env.vars.playerPartsType == PARTS.QUIET and
        env.gvars.ply_isUsingTempPlayerType == true and env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE and
        env.ocelotRevertCalls == 0, tag .. 'late application keeps the vanilla save/restore state')
    check(has(env.reports, 'allocated;mission=30050;vars=6;parts=27;camo=116;temp=1;start=new;late=1;before=0'),
        tag .. 'late application reported')
    -- No late application when another script replaced the registration.
    env = F({character = 'quiet', mission = 30050})
    allocate(env, {typeSetting = false, endCall = false})
    env.mvars.ply_tempPlayerType = PT.SNAKE
    env.TppPlayer.ForceChangePlayerFromOcelot()
    check(env.vars.playerType == PT.SNAKE and hasText(env.reports, 'late=0'), tag .. 'foreign registration is not overridden')

    -- Flag left by an allocation whose end call never ran: the next one restores it.
    env = F({character = 'quiet', mission = 30050})
    local first = env.n
    allocate(env, {endCall = false})
    check(first.sequence[ALWAYS] == true, tag .. 'flag pending without the end call')
    allocate(env, {mission = 40010, scripts = {sequence = {}}})
    check(first.sequence[ALWAYS] == nil and env.Tpp.MgsCoop.characterAlways == nil, tag .. 'next allocation restores it')
    -- A failure after the flag was set restores it as well.
    env = F({character = 'quiet', mission = 30050, reportThrows = true})
    allocate(env, {endCall = false})
    check(env.Tpp.MgsCoop.characterError and env.n.sequence[ALWAYS] == nil, tag .. 'failure restores the flag')

    -- start= reports the OnAllocate path; unknown without TppMission.IsMissionStart.
    env = F({character = 'quiet', mission = 30050, noMissionStart = true})
    status = allocate(env)
    check(hasText(env.reports, ';start=unknown;always=1') and env.vars.playerType == PT.QUIET, tag .. 'start unknown')

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
    check(#env.receipts == 0 and #env.registered == 0 and env.n.sequence[ALWAYS] == nil, tag .. 'missing Tpp.MgsCoop does nothing')
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
    d = C.Decide({character = 'ocelot', mission = 10020, storySnake = true})
    check(d.result == 'keep' and d.reason == 'story_sequence_snake' and not d.bypass, 'story-sequence predicate')
    d = C.Decide({character = 'quiet', mission = 10010, storySnake = true})
    check(d.reason == 'story_hospital', 'mission list reason wins over the sequence reason')
    d = C.Decide({character = 'quiet', mission = 30050, titleMode = true})
    check(d.result == 'skip' and d.reason == 'title_mode', 'title-mode predicate')
    d = C.Decide({character = 'quiet', mission = 40010, titleMode = true})
    check(d.reason == 'title_mode', 'title mode checked before the mission class')
    -- Gather never treats two missing values as equal.
    env.TppDefine, env.TppStory = {STORY_SEQUENCE = {}}, {GetCurrentStorySequence = function() return nil end}
    env.BuddyType, env.vars.buddyType = {}, nil
    local x = C.Gather('quiet', 'dd_male')
    check(x.storySnake == false and x.buddyQuiet == false, 'missing enums never match missing values')
    env.TppDefine, env.TppStory = nil, nil
    env.gvars.ini_isTitleMode = 1
    x = C.Gather('quiet', 'dd_male')
    check(x.storySnake == false and x.titleMode == false, 'missing story tables and non-boolean title flag tolerated')
    env.TppDefine = {STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 5}}
    env.TppStory = {GetCurrentStorySequence = function() return 5 end}
    env.gvars.ini_isTitleMode = true
    x = C.Gather('quiet', 'dd_male')
    check(x.storySnake == true and x.titleMode == true, 'story sequence and title mode gathered')
    env.continuing = true
    check(C.Gather('quiet', 'dd_male').start == 'continue', 'gather: continue path')
    env.continuing = false
    check(C.Gather('quiet', 'dd_male').start == 'new', 'gather: new mission path')
    env.TppMission.IsMissionStart = function() return 1 end
    check(C.Gather('quiet', 'dd_male').start == 'unknown', 'gather: non-boolean start is unknown')
    env.TppMission.IsMissionStart = function() error('x') end
    check(C.Gather('quiet', 'dd_male').start == 'unknown', 'gather: failing start is unknown')
    env.gvars.ply_isUsingTempPlayerType, env.gvars.ply_lastPlayerTypeUsingTemp = true, PT.AVATAR
    x = C.Gather('quiet', 'dd_male')
    check(x.usingTemp == true and x.realType == PT.AVATAR, 'gather: real type from the saved temporary state')
    local E = {PlayerType = PT, PlayerPartsType = PARTS, PlayerCamoType = CAMO, handNormal = HAND_NORMAL}
    local temp, face = C.Plan('dd_female', {realType = PT.AVATAR, sortie = {}}, E)
    check(temp.playerType == PT.DD_FEMALE and temp.partsType == PARTS.NORMAL and temp.camoType == CAMO.OLIVEDRAB and face == 350,
        'plan DD without sortie values')
    check(C.Plan('quiet', {realType = PT.QUIET}, E).playerType == PT.QUIET, 'plan quiet regardless of current type')
    -- SetAlways/RestoreAlways scope.
    local sequence = {}
    check(C.SetAlways(sequence) == true and sequence[ALWAYS] == true, 'set flag')
    check(C.SetAlways(sequence) == false, 'set refuses an already set flag')
    C.RestoreAlways()
    check(sequence[ALWAYS] == nil and env.Tpp.MgsCoop.characterAlways == nil, 'restore flag')
    C.RestoreAlways()
    check(sequence[ALWAYS] == nil, 'restore is idempotent')
    check(C.SetAlways(nil) == false and C.SetAlways('x') == false, 'set ignores a missing sequence')
end

-- Readback and polling (character_readback.lua module). Player.GetPlayerType
-- may run only while started and for indices the native PLAYERS sample
-- covers; the simulated binding counts a crash for any call while the player
-- implementation is absent.
local function body(t)
    return 'verdict=' .. t.verdict .. ';expect=' .. tostring(t.expect) .. ';local=' .. tostring(t.localIndex or 0) ..
        ';vars=' .. t.vars .. ';parts=' .. t.parts .. ';camo=' .. t.camo .. ';temp=' .. t.temp .. ';players=' .. t.players ..
        ';limit=' .. t.limit .. ';i0=' .. t.i0 .. ';i1=' .. t.i1 .. ';raw0=' .. t.raw0 .. ';raw1=' .. t.raw1 ..
        ';quiet_ocelot_read_as=0'
end
-- A Quiet decision whose vars were applied, with overrides.
local function quiet(t)
    local q = {verdict = 'vars_applied', expect = 6, vars = 6, parts = 27, camo = 116, temp = 1, players = 'stopped',
        limit = 'na', i0 = 'na', i1 = 'na', raw0 = 'na', raw1 = 'na'}
    for key, value in pairs(t or {}) do q[key] = value end
    return body(q)
end
do
    -- True when f() never returned a true value in n calls (one check per loop).
    local function never(n, f)
        for _ = 1, n do if f() then return false end end
        return true
    end
    local function receiptCount(env, source)
        local n = 0
        for _, r in ipairs(env.receipts) do if r == source then n = n + 1 end end
        return n
    end

    -- Lab with no coop.ini (default character): nothing beyond the query, ever.
    local env = fixture({character = '', readback = true, playersPresent = false})
    local R = readbackModule(env)
    allocate(env)
    check(type(R) == 'table' and #env.receipts == 1 and env.Tpp.MgsCoop.characterPoll.enabled == false,
        'default character: readback disabled')
    check(R.Started() == false, 'default character: start does not enable readback')
    check(never(300, function() return R.Poll(nil) end), 'default character: poll inert')
    check(#env.receipts == 1 and env.getPlayerTypeCalls == 0 and #env.readbacks == 0, 'default character: no reads')

    -- Skipped decisions (mission class, unknown name, title mode) disable it too.
    for _, o in ipairs({{character = 'quiet', mission = 40010}, {character = 'bogus'},
            {character = 'quiet', mission = 30050, titleMode = true}}) do
        o.readback, o.playersPresent = true, true
        env = fixture(o)
        R = readbackModule(env)
        allocate(env)
        check(env.Tpp.MgsCoop.characterPoll.enabled == false and R.Started() == false and
            env.getPlayerTypeCalls == 0, 'skip decision disables readback: ' .. tostring(o.mission or o.character))
    end

    -- Quiet with lab readback: inert through the whole loading window.
    env = fixture({character = 'quiet', readback = true, playersPresent = false, instanceTypes = {[0] = 6, [1] = 1}})
    R = readbackModule(env)
    allocate(env)
    check(env.Tpp.MgsCoop.characterPoll.enabled == true and env.Tpp.MgsCoop.characterPoll.started == false,
        'apply decision enables, but does not start, the readback')
    env.receipts = {}
    check(never(500, function() return R.Poll(nil) end), 'no poll before mission start')
    check(#env.receipts == 0 and env.getPlayerTypeCalls == 0 and env.crashes == 0, 'loading window: no guard, no binding call')
    check(R.Readback('manual') == quiet(), 'manual readback before start reads nothing native')
    check(env.getPlayerTypeCalls == 0 and receiptCount(env, PLAYERS) == 0, 'manual readback before start: no guard, no call')
    check(env.readbacks[#env.readbacks] == 'manual;' .. quiet(), 'manual readback logged')

    -- Started while the native guard still reports no player: logged, not called.
    env.readbacks = {}
    check(R.Started() == true and env.readbacks[1] == 'start;' .. quiet({players = 'player_absent'}),
        'start with absent player logged from the guard')
    check(env.getPlayerTypeCalls == 0 and env.crashes == 0, 'absent player: binding never called')

    -- Player realized: both indices read, raw values keep QUIET (6).
    env.players.present = true
    check(never(59, function() return R.Poll(nil) end), 'rate limited')
    check(R.Poll(nil) == true and env.readbacks[2] == 'change;' .. quiet({verdict = 'applied', players = 'ready', limit = 2,
        i0 = 0, i1 = 1, raw0 = 6, raw1 = 1}), 'ready readback: GetPlayerType folds 6 to 0, raw keeps 6, verdict applied')
    check(env.getPlayerTypeCalls == 2 and env.crashes == 0, 'one call per sampled index')
    for _ = 1, 60 do R.Poll(nil) end
    check(#env.readbacks == 2 and env.getPlayerTypeCalls == 4, 'unchanged values not repeated (still sampled)')
    env.players.types[1] = 2
    for _ = 1, 59 do R.Poll(nil) end
    check(R.Poll(nil) == true and env.readbacks[3]:find('i1=2;raw0=6;raw1=2', 1, true), 'change logged')
    -- The native slot lagging behind vars, then vars reverted.
    env.players.types[0] = 0
    for _ = 1, 59 do R.Poll(nil) end
    check(R.Poll(nil) == true and env.readbacks[4]:find('^change;verdict=native_pending;expect=6;local=0;vars=6;'),
        'native slot behind vars: native_pending')
    env.vars.playerType, env.vars.playerPartsType, env.vars.playerCamoType = PT.SNAKE, PARTS.SNEAKING_SUIT, CAMO.SPLITTER
    for _ = 1, 59 do R.Poll(nil) end
    check(R.Poll(nil) == true and env.readbacks[5]:find('^change;verdict=not_applied;expect=6;local=0;vars=0;parts=2;camo=3;'),
        'vars reverted: not_applied')
    env.vars.playerType, env.vars.playerPartsType, env.vars.playerCamoType = PT.QUIET, PARTS.QUIET, CAMO.QUIET
    env.players.types[0] = 6
    for i = 6, 80 do R.Poll('tag' .. i) end
    check(#env.readbacks == 64, 'at most 64 readbacks per mission')

    -- Teardown: stopped at OnTerminate; nothing runs while the player goes away.
    R.Stopped()
    env.players.present = false
    local calls = env.getPlayerTypeCalls
    env.receipts = {}
    check(never(300, function() return R.Poll(nil) end), 'no poll after terminate')
    check(R.Readback('late') == quiet() and env.getPlayerTypeCalls == calls and #env.receipts == 1 and
        env.crashes == 0, 'after terminate: only the log receipt, no guard or binding call')

    -- A new allocation starts unstarted, with a fresh limit.
    allocate(env)
    check(R.Poll(nil) == false and env.Tpp.MgsCoop.characterPoll.started == false, 'allocation resets the started state')
    env.players.present = true
    env.readbacks = {}
    check(R.Started() == true and #env.readbacks == 1, 'limit resets per allocation')

    -- Title-mode flag raised while started (defensive): reads stop.
    env.gvars.ini_isTitleMode = true
    calls = env.getPlayerTypeCalls
    check(never(120, function() return R.Poll(nil) end), 'title mode stops polling')
    check(R.Snapshot() == quiet() and env.getPlayerTypeCalls == calls, 'title mode reads nothing')

    -- Instance limit 1: index 1 is never passed to the binding.
    env = fixture({character = 'ocelot', mission = 30010, readback = true, playersPresent = true, limit = 1,
        instanceTypes = {[0] = 5, [1] = 3}})
    R = readbackModule(env)
    allocate(env)
    env.readbacks = {}
    check(R.Started() == true and env.readbacks[1] == 'start;' .. body({verdict = 'applied', expect = 5, vars = 5,
        parts = 26, camo = 115, temp = 1, players = 'ready', limit = 1, i0 = 0, i1 = 'na', raw0 = 5, raw1 = 'na'}) and
        env.getPlayerTypeCalls == 1, 'limit 1 reads index 0 only')
    -- Local index outside the sampled range: vars only.
    env = fixture({character = 'quiet', readback = true, playersPresent = true, localIndex = 3, instanceTypes = {[0] = 0, [1] = 6}})
    R = readbackModule(env)
    allocate(env)
    check(R.Snapshot() == quiet({verdict = 'vars_applied', localIndex = 3}) and R.Started() and
        env.Tpp.MgsCoop.characterReadback:find('^verdict=vars_applied;expect=6;local=3;'), 'local index beyond the sample')
    env = fixture({character = 'quiet', readback = true, playersPresent = true, localIndex = 1, instanceTypes = {[0] = 0, [1] = 6}})
    R = readbackModule(env)
    allocate(env)
    R.Started()
    check(env.Tpp.MgsCoop.characterReadback:find('^verdict=applied;expect=6;local=1;'), 'local index 1 compares raw1')

    -- DLL without the guard: the literal comes back, the binding is never called.
    env = fixture({character = 'quiet', readback = true, playersPresent = true, playersNative = false})
    R = readbackModule(env)
    allocate(env)
    env.readbacks = {}
    R.Started()
    check(env.readbacks[1] == 'start;' .. quiet({players = 'native_unavailable'}) and env.getPlayerTypeCalls == 0,
        'missing native guard: no binding call')

    -- Story-forced keep still reads back (verdict keep); binding failures are err.
    env = fixture({character = 'quiet', mission = 10010, readback = true, playersPresent = true, readbackThrows = true})
    R = readbackModule(env)
    allocate(env)
    check(env.Tpp.MgsCoop.characterStatus.result == 'keep' and R.Started() == true and
        env.Tpp.MgsCoop.characterReadback == body({verdict = 'keep', expect = 'na', vars = 0, parts = 2, camo = 3, temp = 0,
        players = 'ready', limit = 2, i0 = 'err', i1 = 'err', raw0 = 0, raw1 = 1}),
        'keep decision reads back; binding failure reported as err')
    env.Player = nil
    check(R.Snapshot():find('i0=err', 1, true), 'missing Player table tolerated')
    -- Sortie kept: the own type is expected.
    env = fixture({character = 'snake', readback = true, playersPresent = true, instanceTypes = {[0] = 0, [1] = 255}})
    R = readbackModule(env)
    allocate(env)
    R.Started()
    check(env.Tpp.MgsCoop.characterReadback:find('^verdict=applied;expect=0;local=0;vars=0;'), 'sortie kept expects the own type')
    -- Readback stays disabled without lab (query flag false), even when started.
    env = fixture({character = 'quiet', readback = false, playersPresent = true})
    R = readbackModule(env)
    allocate(env)
    check(R.Started() == false and env.getPlayerTypeCalls == 0, 'no lab: readback off')
    -- Longest bodies still fit the 192-byte native payload limit.
    env = fixture({character = 'quiet', readback = true, playersPresent = false, localIndex = 255})
    R = readbackModule(env)
    allocate(env)
    env.players.stage = string.rep('x', 32)
    env.vars.playerType, env.vars.playerPartsType, env.vars.playerCamoType = 255, 255, 255
    R.Started()
    R.Readback('manual')
    env.players.present, env.players.limit, env.players.types = true, 4, {[0] = 255, [1] = 255}
    env.Player.GetPlayerType = function() error('x') end
    R.Readback('manual')
    check(env.truncated == 0 and #env.readbacks == 3, 'readback lines are never truncated')
    -- No Tpp.MgsCoop at module load or later: inert.
    env = fixture({character = 'quiet', noCoop = true})
    env.e = {}
    setfenv(readbackChunk, env); readbackChunk()
    check(#env.receipts == 0, 'readback module without Tpp.MgsCoop does nothing at load')
end

-- Appended readback wrapper: start after the original returns, poll after each
-- update, stop at terminate entry; arguments, results, errors, yields unchanged.
do
    local sentinel = {}
    local env = fixture({character = 'quiet', readback = true})
    env.e = {}
    local order = {}
    local mode = nil
    for _, name in ipairs({'OnMissionGameStart', 'OnUpdate', 'OnTerminate'}) do
        env.e[name] = function(...)
            order[#order + 1] = name
            check(select('#', ...) == 3 and select(1, ...) == 7 and select(2, ...) == nil and select(3, ...) == 9, 'original arguments')
            if mode == 'error' then error(sentinel) end
            if mode == 'yield' then coroutine.yield('original yield') end
            return 1, nil, 3
        end
    end
    setfenv(readbackChunk, env); readbackChunk()
    allocate(env)
    local R = env.Tpp.MgsCoop.CharacterReadback
    for _, name in ipairs({'Started', 'Poll', 'Stopped'}) do
        R[name] = function(...)
            check(select('#', ...) == 0, 'module hooks take no arguments')
            order[#order + 1] = name
        end
    end
    local function returns(...)
        check(select('#', ...) == 3 and select(1, ...) == 1 and select(2, ...) == nil and select(3, ...) == 3, 'original returns')
    end
    returns(env.e.OnMissionGameStart(7, nil, 9))
    returns(env.e.OnUpdate(7, nil, 9))
    returns(env.e.OnTerminate(7, nil, 9))
    check(table.concat(order, ',') == 'OnMissionGameStart,Started,OnUpdate,Poll,Stopped,OnTerminate',
        'start/poll after the originals, stop before terminate')
    for _, name in ipairs({'OnMissionGameStart', 'OnUpdate', 'OnTerminate'}) do
        order, mode = {}, 'error'
        local ok, err = pcall(env.e[name], 7, nil, 9)
        check(not ok and err == sentinel, 'original error unchanged: ' .. name)
        check(order[#order] == name, 'nothing runs after a failed original: ' .. name)
        order, mode = {}, 'yield'
        local co = coroutine.create(function() returns(env.e[name](7, nil, 9)) end)
        local resumed, value = coroutine.resume(co)
        check(resumed and value == 'original yield', 'original yield crosses the wrapper: ' .. name)
        check(coroutine.resume(co) and coroutine.status(co) == 'dead', 'resumed to completion: ' .. name)
    end
    mode = nil
    for _, name in ipairs({'Started', 'Poll', 'Stopped'}) do R[name] = function() error('module failure') end end
    returns(env.e.OnMissionGameStart(7, nil, 9))
    returns(env.e.OnUpdate(7, nil, 9))
    returns(env.e.OnTerminate(7, nil, 9))
    for _, name in ipairs({'Started', 'Poll', 'Stopped'}) do R[name] = nil end
    returns(env.e.OnUpdate(7, nil, 9))
    env.Tpp.MgsCoop = nil
    returns(env.e.OnMissionGameStart(7, nil, 9))
    local bare = fixture({character = ''})
    bare.e = {}
    setfenv(readbackChunk, bare); readbackChunk()
    check(bare.e.OnUpdate == nil and bare.e.OnMissionGameStart == nil and bare.e.OnTerminate == nil,
        'absent callbacks stay absent')

    -- Whole lifecycle through the real module and wrapper: OnUpdate ticks
    -- during loading (player absent), mission start, play, terminate, teardown.
    env = fixture({character = 'quiet', readback = true, playersPresent = false, instanceTypes = {[0] = 6, [1] = 0}})
    env.e = {OnMissionGameStart = function() end, OnUpdate = function() end, OnTerminate = function() end}
    setfenv(readbackChunk, env); readbackChunk()
    allocate(env)
    for _ = 1, 400 do env.e.OnUpdate() end
    check(env.crashes == 0 and env.getPlayerTypeCalls == 0, 'lifecycle: loading window never calls the binding')
    env.players.present = true
    env.readbacks = {}
    env.e.OnMissionGameStart()
    check(env.readbacks[1] == 'start;' .. quiet({verdict = 'applied', players = 'ready', limit = 2, i0 = 0, i1 = 0, raw0 = 6,
        raw1 = 0}), 'lifecycle: start readback after OnMissionGameStart')
    for _ = 1, 600 do env.e.OnUpdate() end
    env.e.OnTerminate()
    env.players.present = false
    local calls = env.getPlayerTypeCalls
    for _ = 1, 400 do env.e.OnUpdate() end
    check(env.crashes == 0 and env.getPlayerTypeCalls == calls and calls == 22, 'lifecycle: teardown never calls the binding')
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
    -- The chunk reads `n`: OnAllocate's parameter, never rebound at that level
    -- before the insertion (the only other `local n` is inside the nested
    -- f30050_sequence.NeedPlayQuietWishGoMission body). The real OnAllocate
    -- runs below confirm it dynamically (continue applies with late=0).
    local allocateAt = main:find('function e.OnAllocate(n)', 1, true)
    local region = allocateAt and main:sub(allocateAt + #'function e.OnAllocate(n)', insertion - 1) or ''
    local nested
    region, nested = region:gsub('function f30050_sequence%.NeedPlayQuietWishGoMission%(%).-\nend\n', '')
    check(allocateAt and allocateAt < first and nested == 1 and not region:find('local n', 1, true) and
        not region:find('for n,', 1, true) and not region:find('[^%w_.]n=') and not region:find('function[^(]*%([^)]*n'),
        'n is the OnAllocate mission table at the insertion')
    -- Continue path order: InitializeForContinue follows the insertion and precedes the end call.
    check(after('TppVarInit.InitializeForContinue(n)') and after('TppVarInit.InitializeForContinue(n)') <
        after('TppPlayer.ForceChangePlayerFromOcelot()'), 'continue path between the insertion and the end call')
    check(main:sub(-8) == 'return e', 'TppMain ends with return e')
    local composed = main:sub(1, insertion - 1) .. '\n' .. characterSource .. '\n' .. main:sub(insertion, -9) ..
        '\n' .. readbackSource .. '\n' .. 'return e'
    check(loadstring(composed, '=TppMain-character') ~= nil, 'composed TppMain compiles')

    -- Execute the real OnAllocate (spliced or vanilla) with the real
    -- TppPlayer; every other engine call is a permissive stub returning nil,
    -- except TppVarInit.InitializeForContinue (real when --varinit is given,
    -- else the mirror) and the checkpoint reload it calls.
    if realPlayerChunk then
        local permissive
        permissive = setmetatable({}, {__call = function() return nil end,
            __index = function(_, key) if type(key) == 'number' then return nil end return permissive end})
        local function lenient(t)
            return setmetatable(t, {__index = function(_, key) if type(key) == 'number' then return nil end return permissive end})
        end
        local function runAllocate(source, o)
            local env = setmetatable({}, {__index = function(_, key)
                local value = rawget(_G, key)
                if value ~= nil then return value end
                return permissive
            end})
            env.vars = {missionCode = o.mission, playerType = o.playerType or PT.SNAKE, playerPartsType = PARTS.SNEAKING_SUIT,
                playerCamoType = CAMO.SPLITTER, handEquip = HAND_ROCKET, playerFaceId = 77, playerFaceEquipId = 4,
                buddyType = BUDDY.NONE, sortiePrepPlayerType = PT.SNAKE, sortiePrepPlayerFaceId = 77,
                sortiePrepPlayerSnakePartsType = PARTS.SNEAKING_SUIT, sortiePrepPlayerSnakeCamoType = CAMO.SPLITTER,
                sortiePrepPlayerSnakeFaceEquipId = 4, sortiePrepPlayerDDPartsType = PARTS.NORMAL,
                sortiePrepPlayerDDCamoType = CAMO.TIGERSTRIPE, sortiePrepPlayerDDFaceEquipId = 0, mbLayoutCode = 0}
            env.mvars, env.gvars, env.svars = {}, {ply_isUsingTempPlayerType = false}, {}
            env.PlayerType, env.PlayerPartsType, env.PlayerCamoType = lenient(PT), lenient(PARTS), lenient(CAMO)
            env.PlayerHandType = lenient({NONE = 0, NORMAL = 1})
            env.TppEquip = lenient({EQP_HAND_NORMAL = HAND_NORMAL, EQP_SUIT = 99})
            env.BuddyType = lenient({QUIET = BUDDY.QUIET, DOG = BUDDY.DOG})
            env.TppDefine = lenient({STORY_SEQUENCE = lenient({CLEARD_ESCAPE_THE_HOSPITAL = 5}),
                DEFAULT_EQUIP_MISSION_BLOCK_GROUP_SIZE = 1777721, PICKABLE_MAX = 10, PLACED_MAX = 10})
            env.TppStory = lenient({GetCurrentStorySequence = function() return o.storySequence or 20 end})
            env.TppMission = lenient({IsFOBMission = function(code) return math.floor(code / 10000) == 5 end,
                IsSubsistenceMission = function() return env.vars.missionCode == 11043 or env.vars.missionCode == 11044 end,
                IsMissionStart = function() return o.continue ~= true end, IsDefiniteMissionClear = function() return false end})
            env.Tpp = lenient({MgsCoop = {}, IsTypeTable = function(v) return type(v) == 'table' end,
                IsTypeFunc = function(v) return type(v) == 'function' end, IsTypeString = function(v) return type(v) == 'string' end,
                IsTypeNumber = function(v) return type(v) == 'number' end, _requireList = {},
                ApendArray = function(a, b) for _, v in ipairs(b or {}) do a[#a + 1] = v end end})
            env.TppScriptVars = lenient({IsSavingOrLoading = function() return false end})
            -- Checkpoint reload (TppSave.VarRestoreOnContinueFromCheckPoint).
            env.checkpointLoads = 0
            env.TppSave = lenient({VarRestoreOnContinueFromCheckPoint = function()
                env.checkpointLoads = env.checkpointLoads + 1
                for key, value in pairs(o.checkpoint or {}) do env.vars[key] = value end
            end})
            env.receipts, env.crashes, env.getPlayerTypeCalls = {}, 0, 0
            env.players = {present = false, limit = 2, types = {[0] = 0, [1] = 0}}
            env.PlayerInfo = lenient({GetLocalPlayerIndex = function() return 0 end})
            env.Player = lenient({GetPlayerType = function(t)
                env.getPlayerTypeCalls = env.getPlayerTypeCalls + 1
                if not env.players.present then env.crashes = env.crashes + 1; error('null player implementation') end
                local value = env.players.types[t.instanceIndex] or 0
                return value >= 5 and 0 or value
            end})
            env.loadstring = function(text)
                env.receipts[#env.receipts + 1] = text
                if text == QUERY then return function() return o.character, 'dd_male', o.readback == true end end
                if text == ON or text == OFF then return function() return true end end
                if text == PLAYERS then
                    return function()
                        if not env.players.present then return false, 'player_absent' end
                        return true, env.players.limit, env.players.types[0], env.players.types[1]
                    end
                end
                return function() return text end
            end
            setfenv(realPlayerChunk, env)
            env.TppPlayer = realPlayerChunk()
            local continueImpl
            if realVarInitChunk then
                setfenv(realVarInitChunk, env)
                continueImpl = realVarInitChunk().InitializeForContinue
            else
                continueImpl = function(scripts)
                    env.TppSave.VarRestoreOnContinueFromCheckPoint()
                    if scripts.sequence and scripts.sequence[ALWAYS] then env.TppPlayer.MissionStartPlayerTypeSetting() end
                end
            end
            env.continueCalls = 0
            env.TppVarInit = lenient({InitializeForContinue = function(...)
                env.continueCalls = env.continueCalls + 1
                return continueImpl(...)
            end})
            local mainChunk = assert(loadstring(source, '=TppMain-run'))
            setfenv(mainChunk, env)
            local mission = {sequence = {MissionPrepare = function()
                if o.storyTemp then env.TppPlayer.RegisterTemporaryPlayerType(o.storyTemp) end
            end}}
            env.main, env.mission = mainChunk(), mission
            local ok, failure = pcall(env.main.OnAllocate, mission)
            check(ok, 'real OnAllocate ran: ' .. tostring(failure))
            check(env.Tpp.MgsCoop.characterError == nil, 'no character error in real OnAllocate')
            return env
        end
        local function reported(env, text)
            for _, item in ipairs(env.receipts) do if item == REPORT .. text then return true end end
            return false
        end
        local spliced = main:sub(1, insertion - 1) .. '\n' .. characterSource .. '\n' .. main:sub(insertion)
        local env = runAllocate(spliced, {mission = 10036, character = 'quiet'})
        check(env.vars.playerType == PT.QUIET and env.vars.playerPartsType == PARTS.QUIET and env.vars.playerCamoType == CAMO.QUIET and
            env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE, 'real OnAllocate ends as Quiet; own type saved')
        check(has(env.receipts, ON) and env.receipts[#env.receipts]:find('ocelot_reset_skipped;mission=10036', 1, true),
            'the real end-of-OnAllocate Ocelot revert was the skipped call')
        check(reported(env, 'allocated;mission=10036;vars=6;parts=27;camo=116;temp=1;start=new;late=0') and
            env.continueCalls == 0 and env.mission.sequence[ALWAYS] == nil, 'real OnAllocate new mission: no late application')
        env = runAllocate(main, {mission = 10036, character = 'quiet', playerType = PT.QUIET})
        check(env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.SNEAKING_SUIT,
            'vanilla OnAllocate reverts Quiet (the skip is what keeps it)')
        env = runAllocate(spliced, {mission = 10050, character = 'quiet'})
        check(env.vars.playerType == PT.DD_MALE and env.vars.playerFaceId == 0 and has(env.receipts, OFF),
            'real OnAllocate applies the DD fallback and face in a Quiet mission')
        env = runAllocate(spliced, {mission = 10240, character = 'ocelot', storyTemp = {partsType = PARTS.NORMAL,
            camoType = CAMO.OLIVEDRAB, playerType = PT.SNAKE, handEquip = HAND_NORMAL, faceEquipId = 0}})
        check(env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.NORMAL, 'real OnAllocate keeps story Snake')
        env = runAllocate(spliced, {mission = 10036, character = ''})
        check(env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.SNEAKING_SUIT and #env.receipts == 1,
            'real OnAllocate with the default character is vanilla')
        env = runAllocate(spliced, {mission = 10020, character = 'quiet', storySequence = 5})
        check(env.vars.playerType == PT.SNAKE and env.vars.playerPartsType == PARTS.NORMAL_SCARF and
            env.vars.playerCamoType == CAMO.TIGERSTRIPE and env.Tpp.MgsCoop.characterStatus.reason == 'story_sequence_snake',
            'real OnAllocate keeps the story-sequence Snake')

        -- Checkpoint continue (title Continue into 30050, as on October 3).
        local checkpoint = {playerType = PT.SNAKE, playerPartsType = PARTS.SNEAKING_SUIT, playerCamoType = CAMO.SPLITTER}
        local label = realVarInitChunk and 'real TppVarInit' or 'mirror InitializeForContinue'
        env = runAllocate(main, {mission = 30050, character = 'quiet', continue = true, checkpoint = checkpoint,
            storyTemp = QUIET_TEMP})
        check(env.continueCalls == 1 and env.checkpointLoads == 1 and env.vars.playerType == PT.SNAKE and
            env.gvars.ply_isUsingTempPlayerType == false, label .. ': vanilla continue ignores a registered Quiet (root cause)')
        env = runAllocate(spliced, {mission = 30050, character = 'quiet', continue = true, checkpoint = checkpoint})
        check(env.continueCalls == 1 and env.checkpointLoads == 1 and env.vars.playerType == PT.QUIET and
            env.vars.playerPartsType == PARTS.QUIET and env.vars.playerCamoType == CAMO.QUIET and
            env.gvars.ply_lastPlayerTypeUsingTemp == PT.SNAKE, label .. ': spliced continue applies Quiet over the checkpoint')
        check(reported(env, 'apply;mission=30050;requested=quiet;applied=quiet;type=6;reason=override;bypass=1;real=0;' ..
            'start=continue;always=1') and reported(env, 'allocated;mission=30050;vars=6;parts=27;camo=116;temp=1;' ..
            'start=continue;late=0') and env.mission.sequence[ALWAYS] == nil,
            label .. ': applied inside InitializeForContinue (late=0), flag restored')
        env = runAllocate(spliced, {mission = 30050, character = '', continue = true, checkpoint = checkpoint})
        check(env.vars.playerType == PT.SNAKE and #env.receipts == 1 and env.mission.sequence[ALWAYS] == nil,
            label .. ': default character continue is vanilla')

        -- Real composed TppMain lifecycle (splice + appended readback) with lab
        -- readback: the real OnUpdate/OnMissionGameStart/OnTerminate drive the
        -- wrapper; the simulated binding crashes while the player is absent.
        -- The flag reached the vanilla type setting on OnAllocate's own `n`
        -- (always=1 and late=0: not the safety net) and the hooked end call
        -- put it back.
        local function allocationEvidence(env, start)
            return reported(env, 'apply;mission=10036;requested=quiet;applied=quiet;type=6;reason=override;bypass=1;real=0;' ..
                'start=' .. start .. ';always=1') and
                reported(env, 'allocated;mission=10036;vars=6;parts=27;camo=116;temp=1;start=' .. start .. ';late=0') and
                env.continueCalls == (start == 'continue' and 1 or 0) and env.mission.sequence[ALWAYS] == nil and
                env.Tpp.MgsCoop.characterAlways == nil
        end
        local function lifecycle(source, character, name, continue)
            env = runAllocate(source, {mission = 10036, character = character, readback = true, continue = continue,
                checkpoint = continue and checkpoint or nil})
            if character == 'quiet' then
                check(allocationEvidence(env, continue and 'continue' or 'new'),
                    name .. ': type set inside OnAllocate (always=1, late=0) and the end call restored the flag')
            else
                check(env.mission.sequence[ALWAYS] == nil and env.Tpp.MgsCoop.characterAlways == nil,
                    name .. ': default character leaves the mission flag')
            end
            env.mvars.seq_demoSequneceList = {}
            for _ = 1, 300 do assert(pcall(env.main.OnUpdate, env.mission)) end
            check(env.crashes == 0 and env.getPlayerTypeCalls == 0, name .. ': loading OnUpdate never reads the player')
            env.players.present, env.players.types = true, {[0] = 6, [1] = 0}
            local ok, failure = pcall(env.main.OnMissionGameStart, env.mission)
            check(ok, name .. ': OnMissionGameStart ran: ' .. tostring(failure))
            local started = env.Tpp.MgsCoop.characterReadback
            for _ = 1, 120 do assert(pcall(env.main.OnUpdate, env.mission)) end
            check(pcall(env.main.OnTerminate, env.mission), name .. ': OnTerminate ran')
            env.players.present = false
            for _ = 1, 300 do assert(pcall(env.main.OnUpdate, env.mission)) end
            check(env.crashes == 0, name .. ': no read while the player is absent (' .. character .. ')')
            if character == 'quiet' then
                check(env.vars.playerType == PT.QUIET and env.getPlayerTypeCalls == 6 and has(env.receipts, PLAYERS) and
                    started and started:find('^verdict=applied;expect=6;local=0;vars=6;parts=27;camo=116;temp=1;players=ready;'),
                    name .. ': Quiet applied; reads only while started; verdict applied')
            else
                check(env.getPlayerTypeCalls == 0 and not has(env.receipts, PLAYERS), name .. ': default character never reads')
            end
        end
        local composedRun = spliced:sub(1, -9) .. '\n' .. readbackSource .. '\n' .. 'return e'
        for _, character in ipairs({'quiet', ''}) do lifecycle(composedRun, character, 'real TppMain') end
        lifecycle(composedRun, 'quiet', 'real TppMain continue', true)
        -- Optional: the complete DLL composition (host_trial_patch_check output).
        if FULL_COMPOSITION then
            local full = readfile(FULL_COMPOSITION)
            check(full:find('\n-- MGS co-op character selection (original).', 1, true) and
                full:find('\n-- MGS co-op character readback (original).', 1, true), 'DLL composition carries both chunks')
            for _, character in ipairs({'quiet', ''}) do lifecycle(full, character, 'DLL composition') end
            lifecycle(full, 'quiet', 'DLL composition continue', true)
            -- Sensitivity: the evidence above fails when another splice shadows
            -- `n` where the chunk runs, or when the end-of-OnAllocate call no
            -- longer reaches the hook.
            local at = full:find(characterSource, 1, true)
            check(at and not full:find(characterSource, at + 1, true), 'DLL composition holds the character chunk once')
            local shadowed = full:sub(1, at - 1) .. 'do local n = nil\n' .. characterSource .. '\nend' ..
                full:sub(at + #characterSource)
            env = runAllocate(shadowed, {mission = 10036, character = 'quiet', continue = true, checkpoint = checkpoint})
            check(not allocationEvidence(env, 'continue') and reported(env, 'allocated;mission=10036;vars=6;parts=27;camo=116;' ..
                'temp=1;start=continue;late=1;before=0'), 'composed check detects a shadowed `n` (safety net only)')
            local endCall = 'TppPlayer.ForceChangePlayerFromOcelot()end'
            local endAt = full:find(endCall, 1, true)
            check(endAt and not full:find(endCall, endAt + 1, true), 'DLL composition holds one end-of-OnAllocate call')
            env = runAllocate(full:sub(1, endAt - 1) .. 'end' .. full:sub(endAt + #endCall),
                {mission = 10036, character = 'quiet', continue = true, checkpoint = checkpoint})
            check(not allocationEvidence(env, 'continue') and env.mission.sequence[ALWAYS] == true,
                'composed check detects a missing end call (flag left set)')
        end
    end
end

check(longPayloads == 0, 'no report or readback payload reaches the 192-byte limit')

print(checks .. ' character Lua checks passed: default/unwired skips, Quiet/Ocelot override with bypass and one-shot revert skip, ' ..
    'checkpoint continue via the scoped ALWAYS_APPLY flag, late application net, Quiet conflicts and fallbacks, ' ..
    'story-forced missions and story sequence, title mode, mission classes, sortie-kept Snake/Avatar/DD, DD faces, ' ..
    'failures, guarded readback/poll lifecycle with verdicts, appended wrapper' ..
    (realPlayerChunk and ', real TppPlayer functions' or '') ..
    (REAL_MAIN and (', real TppMain anchor/composition' .. (realPlayerChunk and '/OnAllocate/lifecycle execution' or '')) or '') ..
    (realVarInitChunk and ', real TppVarInit.InitializeForContinue' or '') ..
    '; no game or native calls')
