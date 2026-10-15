-- W3 startup Continue against exact extracted game function bodies. The engine
-- records calls; no game, real save, external service or transport is used.
-- Args: world_follow.lua, extracted TppMission.lua, extracted TppMain.lua, world_follow_seed.lua, extracted TppPlayer.lua,
-- world_handoff.lua (join-fix phase 2: composed after world_follow, as in the splice).
local checks = 0
local function check(ok, why) checks = checks + 1; if not ok then error(checks .. ': ' .. why, 2) end end
local function read(path) local f = assert(io.open(path, 'rb')); local s = f:read('*a'); f:close(); return s end
local function slice(s, first, last)
    local a = assert(s:find(first, 1, true), first)
    local b = assert(s:find(last, a + #first, true), last)
    return s:sub(a, b - 1)
end
local missionSource, mainSource = read(arg[2]), read(arg[3])
local worldPath = (arg[1]:gsub('world_follow%.lua$', 'world_sync.lua'))
local stopSource = slice(read(worldPath), 'function W.stop(final)', '-- Control dispatch')
local continueSource = slice(missionSource, 'function e.ExecuteContinueFromCheckPoint(', 'function e.IncrementRetryCount(')
local loadSource = slice(missionSource, 'function e.RequestLoad(', 'function e.ExecuteReload(')
local reserveSource = slice(mainSource, 'function e.ReservePlayerLoadingPosition(', 'function e.OnReload(')
-- The sortie deploy: the vanilla OnEndMissionPreparation (and GoToEmergencyMission)
-- reserve their clear through e.ReserveMissionClear, which world_follow wraps.
local deploySource = slice(missionSource, 'function e.ReserveMissionClear(', 'function e.MissionGameEnd(') ..
    slice(missionSource, 'function e.OnEndMissionPreparation(', 'function e.GetNextMissionCodeForEmergency(') ..
    slice(missionSource, 'function e.GoToEmergencyMission(', 'function e.RequestLoad(') ..
    slice(missionSource, 'function e.SetNextMissionCodeForMissionClear(', 'function e.GetNextMissionCodeForMissionClear(')
local allocationTitleSource = slice(mainSource, 'if gvars.ini_isTitleMode then', 'TppUiCommand.ExcludeNonPermissionContents()')
local initialActionSource = slice(read(arg[5]), 'if(gvars.ply_initialPlayerState==TppDefine.INITIAL_PLAYER_STATE.RIDEON_HELICOPTER)',
    'mvars.ply_locationStationTable={}')
local OP = {TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13, REJECT = 4, MISSION_END = 7}
local locations = {[30010] = 10, [40010] = 10, [30020] = 20, [40020] = 20, [30050] = 50, [40050] = 50}
local function fixture(host, target, level, active, deploy)
    local c = {time = 1000, tx = {}, logs = {}, chunkChecks = {}, engine = {}, stages = {}, configLevel = level or 3}
    local env = setmetatable({}, {__index = _G})
    local function noop() end
    env.vars = {missionCode = 40020, locationCode = 20, mbLayoutCode = 0, mbClusterId = 2,
        initialPlayerFlag = 1, initialPlayerPosX = 99, initialPlayerPosY = 88, initialPlayerPosZ = 77, initialPlayerRotY = 66}
    env.mvars, env.svars = {}, {mis_missionClearType = 6, ply_isUsedPlayerInitialAction = host}
    env.gvars = {usingNormalMissionSlot = false, str_storySequence = 200, solface_groupNumber = 11, hosface_groupNumber = 22,
        rev_revengeRandomValue = 33, rev_revengeLv = {[0] = 1, [1] = 2}, missionStartClock = 43200,
        missionStartWeather = 0, missionStartWeatherNextTime = 123.25, missionStartExtraWeatherInterval = 200,
        heli_missionStartRoute = 444, mis_isStartFromHelispace = false, mis_isStartFromFreePlay = true,
        ply_initialPlayerState = 0, ply_useMissionStartPos = true, ply_missionStartPos = {[0] = 2, [1] = 3, [2] = 4},
        ply_missionStartRot = 0, ply_useMissionStartPosForNoOrderBox = false,
        ply_missionStartPosForNoOrderBox = {[0] = 0, [1] = 0, [2] = 0}, ply_missionStartRotForNoOrderBox = 0,
        mis_checkPoint = 0, ini_isTitleMode = false, ini_isReturnToTitle = false, isContinueFromTitle = false}
    env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 1, MISSION_ABORT = 2, MISSION_RESTART = 3, CONTINUE_FROM_CHECK_POINT = 4},
        STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 100}, STORY_MISSION_LAYOUT_CODE = {},
        SYS_MISSION_ID = {MTBS_HELI = 40050}, ERROR_ID = {NOW_INSTALLING = 123},
        INITIAL_PLAYER_STATE = {ON_FOOT = 0, RIDEON_HELICOPTER = 1}}
    env.PlayerInitialAction = {FROM_HELI_SPACE = 100}
    env.GameObject = {NULL_ID = 65535, GetGameObjectId = function(kind, name) return kind == 'TppHeli2' and name == 'SupportHeli' and 998 or 65535 end}
    env.TppRevenge = {REVENGE_TYPE = {MAX = 2}}
    env.PlayerFlag = {USE_VARS_FOR_INITIAL_POS = 1}
    env.GameOverMenu = {POPUP_RESULT_YES = 1, STEALTH_ASSIST_POPUP = 2, PERFECT_STEALTH_POPUP = 3}
    env.GameConfig = {GetStealthAssistEnabled = function() return false end}
    env.DemoDaemon = {IsDemoPlaying = function() return false end}
    env.TppSystemUtility = {GetCurrentGameMode = function() return 'TPP' end}
    env.TppQuest = {OnMissionGameEnd = noop}
    env.TppWeather = {OnEndMissionPrepareFunction = noop, StoreToSVars = noop}
    env.TppMarker = {StoreMarkerLocator = noop}
    env.TppRadioCommand = {StoreRadioState = noop}
    env.TppPlayer = {StoreSupplyCbox = noop, StoreSupportAttack = noop, StorePlayerDecoyInfos = noop,
        UnsetRetryFlag = noop, SetRetryFlag = noop, SetRetryFlagWithChickCap = noop,
        SetStartStatusRideOnHelicopter = function() env.gvars.ply_initialPlayerState = 1 end,
        SetInitialPosition = function(pos, rot)
            env.vars.initialPlayerFlag = 1
            env.vars.initialPlayerPosX, env.vars.initialPlayerPosY, env.vars.initialPlayerPosZ, env.vars.initialPlayerRotY = pos[1], pos[2], pos[3], rot
        end,
        SetMissionStartPosition = function(pos, rot)
            env.gvars.ply_useMissionStartPos = true
            env.gvars.ply_missionStartPos = {[0] = pos[1], [1] = pos[2], [2] = pos[3]}
            env.gvars.ply_missionStartRot = rot
        end,
        ResetNoOrderBoxMissionStartPosition = function() env.gvars.ply_useMissionStartPosForNoOrderBox = false end}
    env.TppUiStatusManager = {SetStatus = noop}
    env.TppUiCommand = {IsShowPopup = function() return false end, ErasePopup = noop}
    env.TppUI = {ShowAccessIcon = noop}
    env.TppEneFova = {InitializeUniqueSetting = noop}
    env.TppLocation = {IsMotherBase = function() return env.vars.locationCode == 50 end,
        ApplyPlatformParamToMbStage = function() return false end, ModifyMbsLayoutCode = function(n) return n end}
    env.TppMotherBaseManagement = {GetMbsTopologyType = function() return 0 end}
    env.TppPackList = {GetLocationNameFormMissionCode = function(m)
        local l = locations[m]; return l == 10 and 'AFGH' or l == 20 and 'MAFR' or l == 50 and 'MTBS' or nil
    end}
    env.Tpp = {GetChunkIndex = function(l) c.chunkChecks[#c.chunkChecks + 1] = l; return l end, ShowChunkInstallingPopup = noop}
    env.Chunk = {STATE_INSTALLED = 1, INSTALL_SPEED_NORMAL = 1, INSTALL_SPEED_FAST = 2,
        GetChunkState = function() return c.chunkLoading and 0 or 1 end, SetChunkInstallSpeed = noop, PrefetchChunk = noop}
    env.SplashScreen = {GetSplashScreenWithName = function() return false end}
    env.TppEnemy = {StoreSVars = noop, IsLoadedDefaultSoldier2CommonPackage = function() return true end,
        UnloadSoldier2CommonBlock = noop, PreMissionLoad = function(nextMission, currentMission)
            c.preload = {nextMission, currentMission, env.vars.missionCode, env.vars.locationCode}
        end}
    env.TppSave = {VarRestoreOnContinueFromCheckPoint = function()
        c.restores = (c.restores or 0) + 1
        c.restoreTitle = {env.gvars.ini_isTitleMode, env.gvars.ini_isReturnToTitle, env.gvars.isContinueFromTitle}
        env.svars.ply_isUsedPlayerInitialAction = host
        local m = host and target or c.ownTarget or 40020
        env.vars.missionCode, env.vars.locationCode = m, locations[m]
        if host then
            env.vars.initialPlayerFlag = 1
            env.vars.initialPlayerPosX, env.vars.initialPlayerPosY, env.vars.initialPlayerPosZ, env.vars.initialPlayerRotY =
                -123.125, 34.625, 456.875, -45.125
        else
            env.vars.initialPlayerFlag = 1
            env.vars.initialPlayerPosX, env.vars.initialPlayerPosY, env.vars.initialPlayerPosZ, env.vars.initialPlayerRotY = 99, 88, 77, 66
            env.gvars.solface_groupNumber = 999
        end
        env.gvars.usingNormalMissionSlot = true
        return 17, nil, 19
    end, VarSaveOnRetry = noop, SaveGameData = function(m) c.savedMission = m end}
    env.TppSave.VarRestoreOnMissionStart = env.TppSave.VarRestoreOnContinueFromCheckPoint
    env.Mission = {LoadLocation = function() c.engine[#c.engine + 1] = 'location' end,
        LoadMission = function()
            c.engine[#c.engine + 1] = 'mission'
            c.loaded = {env.vars.missionCode, env.vars.locationCode, env.vars.initialPlayerPosX, env.vars.initialPlayerPosY,
                env.vars.initialPlayerPosZ, env.vars.initialPlayerRotY, env.gvars.solface_groupNumber}
        end,
        RequestToReload = function() c.engine[#c.engine + 1] = 'reload'; c.reloaded = env.vars.missionCode end}
    env.StageBlockCurrentPositionSetter = {SetEnable = function(enabled) c.stageEnabled = enabled end,
        SetPosition = function(x, z)
            c.stages[#c.stages + 1] = {env.vars.missionCode, env.vars.locationCode, x, z, env.vars.initialPlayerPosY,
                env.vars.initialPlayerRotY, env.gvars.solface_groupNumber}
        end, DisablePosition = function() c.stageDisabled = true end}
    env.TppMission = {SafeStopSettingOnMissionReload = noop, IncrementRetryCount = noop,
        IsFOBMission = function() return false end, IsHardMission = function() return false end,
        IsHelicopterSpace = function(m) return m == 40010 or m == 40020 or m == 40050 end,
        SetIsStartFromHelispace = function() env.gvars.mis_isStartFromHelispace = true end,
        ResetIsStartFromFreePlay = function() env.gvars.mis_isStartFromFreePlay = false end,
        ResetEmegerncyMissionSetting = noop, ExecuteOnReturnToMissionCallback = function() return false end}
    env.TppMission.IsMissionStart = function() return c.isMissionStart == true end
    env.TppPlayer.MissionStartPlayerTypeSetting = function() c.playerTypeInits = (c.playerTypeInits or 0) + 1 end
    env.TppSave.VarSave = noop
    env.TppVarInit = {InitializeForContinue = function()
        c.continueInits = (c.continueInits or 0) + 1
        env.TppSave.VarRestoreOnContinueFromCheckPoint()
    end, InitializeForNewMission = function()
        c.missionInits = (c.missionInits or 0) + 1
        env.TppSave.VarRestoreOnMissionStart()
    end, ClearIsContinueFromTitle = function()
        c.titleClears = (c.titleClears or 0) + 1
        env.gvars.isContinueFromTitle = false
    end}
    env.TppMain = {EnablePause = noop, EnableBlackLoading = noop, DisableGameStatus = noop}
    env.e = env.TppMain
    local function compile(text)
        local chunk = assert(loadstring(text)); setfenv(chunk, env); chunk()
    end
    compile('local e=TppMission\n' .. continueSource .. loadSource)
    if deploy then
        env.TppDefine.MISSION_CLEAR_TYPE, env.TppDefine.STORY_MISSION_CLUSTER_ID = {FROM_HELISPACE = 3}, {}
        compile('local e=TppMission\n' .. deploySource)
    end
    compile('local e=TppMain\n' .. reserveSource)
    compile('function TppMain.TestAllocationTitleContext(n)\n' .. allocationTitleSource .. '\nend')
    compile('function TppPlayer.TestInitialAction(a)\nlocal i=GameObject.GetGameObjectId\nlocal n=GameObject.NULL_ID\n' .. initialActionSource .. '\nend')
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {},
        level = level or 3, role = host and 1 or 2, host = host, active = active ~= false, mission = 40020,
        epoch = 7, paired = true, eff = 3, isStarted = true}
    env.e.MgsCoopWorld = W
    W.get = function(t, k) return t and t[k] end
    W.cfg = function()
        local r = {c.configLevel, c.configRole or W.role, 1, c.link or 1, 7, 0, 1, c.session or 55}
        -- A phase-2 loader (partner scene r[9..11], handoff r[12], lock r[13]) only when a test sets c.lock.
        if c.lock ~= nil then r[9], r[10], r[11], r[12], r[13] = 30010, 3, 9, c.handoff or 0, c.lock end
        return r
    end
    W.h = function() return 444 end
    W.now = function() return c.time end
    W.inGame = function() return false end
    W.nextRid = function() c.rid = (c.rid or 0) + 1; return c.rid end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    W.fail = function(text) c.failure = text; W.active = false end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op = op, v = {...}}; return true end
    -- SAVE_V1 receipts (world_handoff): recorded as integer lists.
    c.saves = {}
    W.n = function(v) if type(v) == 'number' and v == math.floor(v) and v >= 0 and v < 4294967296 then return string.format('%.0f', v) end end
    W.receipt = function(source)
        local body = assert(source:match("^return 'MGS_COOP_WORLD_SAVE_V1' %-%- (%d+ %d+ %d+ %d+ %d+ %d+)$"), source)
        local v = {}
        for n in body:gmatch('%d+') do v[#v + 1] = tonumber(n) end
        c.saves[#c.saves + 1] = v
        return true
    end
    -- NOTE recorder; W.peer stays nil (an older loader: the legacy follow behaviour).
    c.notes = {}
    W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
    compile('local W=e.MgsCoopWorld\nlocal OP=W.OP\n' .. stopSource)
    compile(read(arg[4]))
    compile(read(arg[1]))
    compile(read(assert(arg[6], 'world_handoff.lua argument')))
    W.follow.install({level or 3, W.role})
    for _, f in ipairs(W.startHooks) do f() end
    for _, f in ipairs(W.pairedHooks) do f(false) end
    c.env, c.W, c.F = env, W, W.follow
    return c
end
local function hostContinue(h)
    h.env.TppMission.ExecuteContinueFromCheckPoint()
    check(h.env.mvars.mis_loadRequest and h.env.mvars.mis_loadRequest.currentMission == 40020,
        'real host Continue captures ACC source before VarRestore')
    h.env.TppMission.LoadWithChunkCheck()
    for _ = 1, 4 do h.F.tick(h.time) end
    check(h.loaded and h.loaded[1] ~= 40020, 'host actual Load consumes restored checkpoint destination')
end
local function transfer(h, g, omitEnd)
    for _, p in ipairs(h.tx) do
        if (p.op == OP.TRANSITION or p.op == OP.SEED or p.op == OP.SEEDS_END) and not (omitEnd and p.op == OP.SEEDS_END) then
            g.W.handlers[p.op](p.v, g.time)
        end
    end
end
local titleHost, titleGuest = fixture(true, 30050), fixture(false, 30050)
titleHost.env.gvars.isContinueFromTitle = true
titleGuest.env.gvars.ini_isTitleMode, titleGuest.env.gvars.ini_isReturnToTitle = true, true
hostContinue(titleHost); transfer(titleHost, titleGuest); titleGuest.F.tick(titleGuest.time)
check(titleGuest.F.current and titleGuest.restoreTitle and titleGuest.restoreTitle[1] == false and
    titleGuest.restoreTitle[2] == false and titleGuest.restoreTitle[3] == true,
    'authenticated title Continue copies host title context before the actual guest checkpoint restore')
titleGuest.env.TppMission.LoadWithChunkCheck(); titleGuest.W.stop(true)
titleGuest.env.TppMain.TestAllocationTitleContext({})
check(titleGuest.continueInits == 1 and titleGuest.titleClears == 1 and not titleGuest.playerTypeInits and
    titleGuest.env.vars.missionCode == 30050 and titleGuest.env.vars.initialPlayerPosX == -123.125,
    'exact OnAllocate title decision enters checkpoint initialization after host-only Continue, preserving host spawn')
check(titleGuest.env.gvars.ini_isTitleMode == false and titleGuest.env.gvars.ini_isReturnToTitle == false and
    titleGuest.env.gvars.isContinueFromTitle == false,
    'actual allocation branch clears the one-shot Continue-from-title context')
titleGuest.env.TppMain.StageBlockCurrentPosition()
titleGuest.env.TppSave.VarRestoreOnContinueFromCheckPoint()
check(titleGuest.env.gvars.isContinueFromTitle == false and titleGuest.env.gvars.ini_isTitleMode == false,
    'later StageBlock and restore seed reapplication cannot revive cleared title context')
local titleLogs = 0
for _, line in ipairs(titleGuest.logs) do
    if line:find('step=title_context;', 1, true) then
        titleLogs = titleLogs + 1
        check(#line < 256 and line:find('title_before=1;title_after=0', 1, true) and
            line:find('return_before=1;return_after=0', 1, true) and line:find('continue_before=0;continue_after=1', 1, true),
            'bounded title receipt records actual before/after state')
    end
end
check(titleLogs == 1, 'accepted guest transition applies title context exactly once')
local landedHost, oldHeliGuest = fixture(true, 30020), fixture(false, 30020)
landedHost.env.gvars.ply_initialPlayerState, oldHeliGuest.env.gvars.ply_initialPlayerState = 1, 1
oldHeliGuest.env.TppPlayer.TestInitialAction({})
check(oldHeliGuest.env.vars.initialPlayerAction == oldHeliGuest.env.PlayerInitialAction.FROM_HELI_SPACE and
    oldHeliGuest.env.vars.initialPlayerPairGameObjectId == 998,
    'exact player Init branch reproduces old guest helicopter action despite a valid initial position flag')
oldHeliGuest.env.vars.initialPlayerAction, oldHeliGuest.env.vars.initialPlayerPairGameObjectId = nil, nil
hostContinue(landedHost); transfer(landedHost, oldHeliGuest); oldHeliGuest.F.tick(oldHeliGuest.time)
oldHeliGuest.env.TppMission.LoadWithChunkCheck(); oldHeliGuest.W.stop(true)
oldHeliGuest.env.TppMain.TestAllocationTitleContext({})
oldHeliGuest.env.TppPlayer.TestInitialAction({})
check(oldHeliGuest.env.svars.ply_isUsedPlayerInitialAction == true and oldHeliGuest.env.vars.initialPlayerAction == nil and
    oldHeliGuest.env.vars.initialPlayerPairGameObjectId == nil and oldHeliGuest.env.vars.initialPlayerPosX == -123.125,
    'mandatory host initial-action SVar survives second checkpoint restore and prevents old guest helicopter startup')
local vanillaHeli = fixture(false, 30020, 0)
vanillaHeli.env.gvars.ply_initialPlayerState = 1
vanillaHeli.env.TppSave.VarRestoreOnContinueFromCheckPoint(); vanillaHeli.env.TppPlayer.TestInitialAction({})
check(vanillaHeli.env.svars.ply_isUsedPlayerInitialAction == false and
    vanillaHeli.env.vars.initialPlayerAction == vanillaHeli.env.PlayerInitialAction.FROM_HELI_SPACE,
    'level0 keeps the exact vanilla old-save helicopter initial action')
for _, target in ipairs({30020, 30010, 30050}) do
    local h, g = fixture(true, target), fixture(false, target)
    hostContinue(h); transfer(h, g); g.F.tick(g.time)
    check(not g.failure and g.restores == 1 and g.F.current, 'actual guest Continue runs its own restore exactly once')
    local request = g.env.mvars.mis_loadRequest
    check(request and request.nextMission == target and request.currentMission == 40020,
        'guest RequestLoad preserves real ACC source while using host target ' .. target)
    local stage = g.stages[#g.stages]
    check(stage and stage[1] == target and stage[2] == locations[target] and stage[3] == -123.125 and
        stage[4] == 456.875 and stage[5] == 34.625 and stage[6] == -45.125 and stage[7] == 11 and not g.stageDisabled,
        'destination, exact host checkpoint pose and seeds reach real StageBlock before its original call')
    g.chunkLoading = true; g.env.TppMission.LoadWithChunkCheck()
    check(g.env.mvars.mis_loadRequest == request and not g.loaded and g.chunkChecks[#g.chunkChecks] == locations[target],
        'actual chunk gate waits for host destination location ' .. locations[target])
    g.chunkLoading = false; g.env.TppMission.LoadWithChunkCheck()
    check(g.loaded and g.loaded[1] == target and g.loaded[2] == locations[target] and g.loaded[3] == -123.125 and
        g.loaded[4] == 34.625 and g.loaded[5] == 456.875 and g.loaded[6] == -45.125 and g.loaded[7] == 11,
        'actual Mission.LoadMission reads corrected globals and checkpoint spawn ' .. target)
    check(g.engine[1] == 'location' and g.engine[2] == 'mission' and not g.reloaded and
        g.preload[1] == target and g.preload[2] == 40020 and not g.env.mvars.mis_loadRequest,
        'source/target difference loads new assets rather than reloading ACC and clears accepted request')
    g.W.stop(true)
    check(not g.W.active and not g.W.isStarted and g.F.current,
        'real world stop retains accepted pending guest travel across allocation gap')
    g.env.TppSave.VarRestoreOnContinueFromCheckPoint()
    check(g.env.vars.missionCode == target and g.env.vars.locationCode == locations[target] and
        g.env.vars.initialPlayerPosX == -123.125 and g.env.gvars.solface_groupNumber == 11,
        'second checkpoint restore after real W.stop(true) cannot overwrite authoritative destination or spawn')
    local a, b, c = g.env.TppSave.VarRestoreOnMissionStart()
    check(a == 17 and b == nil and c == 19 and g.env.vars.missionCode == target and
        g.env.vars.initialPlayerPosX == -123.125 and g.env.gvars.solface_groupNumber == 11,
        'mission-start restore also preserves exact original returns while reapplying authoritative seeds')
    g.env.vars.initialPlayerPosX, g.env.vars.initialPlayerPosZ, g.env.vars.initialPlayerRotY = 900, 901, 902
    g.env.TppMain.StageBlockCurrentPosition()
    local restage = g.stages[#g.stages]
    check(restage[3] == -123.125 and restage[4] == 456.875 and restage[6] == -45.125,
        'StageBlock choke replaces stale local deployment preparation with host seed pose')
end
local deploymentHost, deploymentGuest = fixture(true, 30020), fixture(false, 30020)
deploymentHost.env.vars.missionCode, deploymentHost.F.deploying = 30020, true
deploymentHost.env.mvars.mis_helicopterMissionStartPosition = {-123.125, 34.625, 456.875}
deploymentHost.env.TppMain.ReservePlayerLoadingPosition(1, true)
deploymentHost.env.TppMission.Load(30020, 40020)
for _ = 1, 4 do deploymentHost.F.tick(deploymentHost.time) end
transfer(deploymentHost, deploymentGuest)
deploymentGuest.F.current = deploymentGuest.F.queue[1]
deploymentGuest.F.current.session = 55
deploymentGuest.env.mvars.mis_helicopterMissionStartPosition = {900, 901, 902}
deploymentGuest.env.TppMain.ReservePlayerLoadingPosition(1, true)
local stage = deploymentGuest.stages[#deploymentGuest.stages]
check(deploymentHost.F.outgoing[2] == 6 and stage[3] == -123.125 and stage[4] == 456.875 and stage[5] == 34.625,
    'real source-helicopter deployment Reserve cannot stage stale guest landing-zone prep over host seed pose')
local h, blocked = fixture(true, 30020), fixture(false, 30020)
blocked.env.gvars.ini_isTitleMode, blocked.env.gvars.ini_isReturnToTitle = true, true
hostContinue(h); transfer(h, blocked, true); blocked.F.tick(blocked.time)
check(not blocked.restores and not blocked.env.mvars.mis_loadRequest and not blocked.loaded,
    'missing completed seed stream prevents actual Continue and all load side effects')
check(blocked.env.gvars.ini_isTitleMode and blocked.env.gvars.ini_isReturnToTitle and not blocked.env.gvars.isContinueFromTitle,
    'incomplete seed stream cannot dismiss the guest title context')
for _, settings in ipairs({{0, true}, {3, false}}) do
    local untouched = fixture(false, 30020, settings[1], settings[2])
    untouched.env.gvars.ini_isTitleMode, untouched.env.gvars.ini_isReturnToTitle = true, true
    untouched.env.TppMission.ExecuteContinueFromCheckPoint()
    local request = untouched.env.mvars.mis_loadRequest
    check(request and request.nextMission == 40020 and request.currentMission == 40020,
        'level0/inactive vanilla Continue remains its own saved ACC destination')
    untouched.env.TppMission.LoadWithChunkCheck()
    check(untouched.reloaded == 40020 and not untouched.loaded and untouched.chunkChecks[1] == 20,
        'inert Load follows exact vanilla RequestToReload path and original chunk')
    untouched.env.TppMain.TestAllocationTitleContext({})
    check(untouched.env.gvars.ini_isTitleMode and untouched.env.gvars.ini_isReturnToTitle and
        untouched.playerTypeInits == 1 and not untouched.continueInits and not untouched.titleClears,
        'level0/inactive exact allocation retains vanilla title branch and flags')
end
local function invalidStream(filter, duplicateKey, why)
    local host, guest = fixture(true, 30020), fixture(false, 30020)
    guest.env.gvars.ini_isTitleMode, guest.env.gvars.ini_isReturnToTitle = true, true
    hostContinue(host)
    local count, digest = 0, 0
    for _, p in ipairs(host.tx) do
        if p.op == OP.TRANSITION then guest.W.handlers[p.op](p.v, guest.time)
        elseif p.op == OP.SEED and filter(p.v) then
            guest.W.handlers[p.op](p.v, guest.time)
            local v = p.v
            count, digest = count + 1, (digest * 31 + v[3] * 1024 + v[4] + v[5]) % 4294967296
            if v[3] == duplicateKey and v[4] == 0 then
                guest.W.handlers[p.op](p.v, guest.time)
                count, digest = count + 1, (digest * 31 + v[3] * 1024 + v[4] + v[5]) % 4294967296
            end
        end
    end
    guest.W.handlers[OP.SEEDS_END]({host.F.outgoing[1], 30020, count, digest}, guest.time)
    guest.F.tick(guest.time)
    check(guest.failure and not guest.restores and not guest.env.mvars.mis_loadRequest and not guest.loaded and
        guest.env.gvars.ini_isTitleMode and guest.env.gvars.ini_isReturnToTitle and not guest.env.gvars.isContinueFromTitle,
        why .. ' prevents every real Continue/VarRestore/load API')
end
invalidStream(function() return false end, nil, 'self-consistent empty stream')
invalidStream(function(v) return v[3] == 1 end, nil, 'self-consistent partial stream')
invalidStream(function(v) return v[3] < 19 end, nil, 'stream lacking all checkpoint pose/hash keys19..24')
invalidStream(function() return true end, 1, 'self-consistent duplicate key/index stream')
for missing = 25, 27 do
    invalidStream(function(v) return v[3] ~= missing end, nil, 'stream missing mandatory title-context key ' .. missing)
end
invalidStream(function(v) return v[3] ~= 28 end, nil, 'stream missing mandatory initial-action SVar key28')
local unreadable = fixture(true, 30020)
unreadable.env.TppMission.ExecuteContinueFromCheckPoint()
local request = unreadable.env.mvars.mis_loadRequest
unreadable.env.gvars.hosface_groupNumber = nil
unreadable.env.TppMission.LoadWithChunkCheck()
check(request and unreadable.env.mvars.mis_loadRequest == request and not unreadable.loaded and #unreadable.engine == 0,
    'actual host chunk/load wrapper retains pending request when mandatory seed is unreadable')
for _, key in ipairs({'ini_isTitleMode', 'ini_isReturnToTitle', 'isContinueFromTitle'}) do
    local host = fixture(true, 30020)
    host.env.TppMission.ExecuteContinueFromCheckPoint()
    local pending = host.env.mvars.mis_loadRequest
    host.env.gvars[key] = nil
    host.env.TppMission.LoadWithChunkCheck()
    check(pending and host.env.mvars.mis_loadRequest == pending and not host.loaded and #host.engine == 0,
        'actual host retains load request when required title flag is unreadable: ' .. key)
end
local noAction = fixture(true, 30020)
noAction.env.TppMission.ExecuteContinueFromCheckPoint()
local actionRequest = noAction.env.mvars.mis_loadRequest
noAction.env.svars.ply_isUsedPlayerInitialAction = nil
noAction.env.TppMission.LoadWithChunkCheck()
check(actionRequest and noAction.env.mvars.mis_loadRequest == actionRequest and not noAction.loaded and #noAction.engine == 0,
    'host holds actual load request when mandatory initial-action SVar is unreadable')
local heldHost, disabled = fixture(true, 30020), fixture(false, 30020)
hostContinue(heldHost); transfer(heldHost, disabled); disabled.F.tick(disabled.time)
disabled.env.TppMission.LoadWithChunkCheck(); disabled.W.stop(true)
disabled.configLevel = 0
disabled.env.TppSave.VarRestoreOnContinueFromCheckPoint()
check(disabled.env.vars.missionCode == 40020 and disabled.env.vars.initialPlayerPosX == 99 and not disabled.F.current,
    'explicit native configuration level0 cancels pending inactive travel and restores vanilla checkpoint')
for _, change in ipairs({'session', 'link'}) do
    local host, guest = fixture(true, 30020), fixture(false, 30020)
    hostContinue(host); transfer(host, guest); guest.F.tick(guest.time)
    guest.env.TppMission.LoadWithChunkCheck(); guest.W.stop(true)
    if change == 'session' then guest.session = 99 else guest.link = 0 end
    guest.env.TppSave.VarRestoreOnContinueFromCheckPoint()
    check(guest.env.vars.missionCode == 40020 and guest.env.vars.initialPlayerPosX == 99 and not guest.F.current,
        'fresh native ' .. change .. ' change during allocation gap discards old authenticated travel before restore setup')
end
local downHost, beforeReplay = fixture(true, 30020), fixture(false, 30020)
hostContinue(downHost); transfer(downHost, beforeReplay)
beforeReplay.link = 0; beforeReplay.F.tick(beforeReplay.time)
check(not beforeReplay.F.current and not beforeReplay.restores and not beforeReplay.env.mvars.mis_loadRequest,
    'link-down check before first replay leaves actual Continue/restore/load untouched')
beforeReplay.link = 1; beforeReplay.F.tick(beforeReplay.time)
check(beforeReplay.F.current and beforeReplay.F.current.session == 55 and beforeReplay.restores == 1,
    'first replay binds the current native session after link returns')
-- A story or emergency destination chosen again (docs/join-fix-phase1-spec.md C5): the
-- exact vanilla deploy reserves its own clear through the wrapped e.ReserveMissionClear.
for _, case in ipairs({{10020, 0, 10020}, {30020, 50050, 50050}}) do
    local d = fixture(true, 30020, 3, true, true)
    local g = d.env.gvars
    g.mis_nextMissionCodeForMissionClear, g.mis_nextMissionCodeForEmergency, g.mis_nextMissionStartRouteForEmergency = case[1], case[2], 77
    d.env.TppMission.OnEndMissionPreparation(5, nil)
    check(d.env.mvars.mis_isReserveMissionClear == nil and #d.notes == 1 and d.notes[1][1] == 4 and d.notes[1][2] == case[1],
        'exact-game deploy to ' .. case[3] .. ': the first choice is refused')
    d.time = d.time + 1500
    d.env.TppMission.OnEndMissionPreparation(5, nil)
    check(d.env.mvars.mis_isReserveMissionClear == true and d.env.svars.mis_isDefiniteMissionClear == true and
        g.mis_nextMissionCodeForMissionClear == case[3] and d.env.svars.mis_missionClearType == 3 and d.env.mvars.mis_selectedDeployTime == 5 and
        #d.notes == 2 and d.notes[2][1] == 3 and d.notes[2][3] == 3 and d.F.pass == nil,
        'exact-game deploy to ' .. case[3] .. ': chosen again, the vanilla clear is reserved')
end
-- Join-fix phase 2 (world_handoff composed): the host's real title Continue reports
-- its restored checkpoint target (SAVE op 2, the provenance binding).
local reporter = fixture(true, 30050)
hostContinue(reporter)
local continues = {}
for _, v in ipairs(reporter.saves) do if v[1] == 2 then continues[#continues + 1] = v end end
check(#continues == 1 and continues[1][2] == 30050 and continues[1][3] == 0 and continues[1][5] == 0 and continues[1][6] == 0,
    'actual host Continue sends one SAVE op 2 with the restored checkpoint mission')
-- An unarmed, linked guest at a 40050 title with the host in 30010: the actual
-- ExecuteContinueFromCheckPoint is refused before any vanilla side effect, and
-- the same Continue 2 s later runs the vanilla path on its own save.
local function lockedGuest()
    local g = fixture(false, 30050)
    g.W.paired, g.F.armed = false, false
    g.W.mission, g.env.vars.missionCode, g.env.vars.locationCode = 40050, 40050, 50
    g.env.gvars.ini_isTitleMode, g.env.gvars.ini_isReturnToTitle = true, true
    for _, f in ipairs(g.W.startHooks) do f() end
    g.lock, g.ownTarget = 1, 30050
    g.W.peer = function() return {mission = 30010, level = 3, wait = 9, linked = true, session = 55} end
    return g
end
local locked = lockedGuest()
check(locked.env.TppMission.ExecuteContinueFromCheckPoint() == nil and not locked.restores and not locked.env.mvars.mis_loadRequest and
    not locked.loaded and #locked.engine == 0 and locked.env.gvars.usingNormalMissionSlot == false and
    #locked.notes == 1 and locked.notes[1][1] == 7 and locked.notes[1][2] == 30010 and locked.notes[1][3] == 1,
    'locked guest title Continue: refused before VarRestore, load or any vanilla state change (note 7 b=1)')
check(#locked.tx == 0, 'the refusal sends no world op')
locked.time = locked.time + 2000
locked.env.TppMission.ExecuteContinueFromCheckPoint()
local request = locked.env.mvars.mis_loadRequest
check(locked.restores == 1 and request and request.nextMission == 30050 and locked.env.vars.missionCode == 30050,
    'the repeat 2 s later runs the vanilla Continue into its own checkpoint')
local continued, landing
for _, v in ipairs(locked.saves) do if v[1] == 2 then continued = v end end
for _, v in ipairs(locked.notes) do if v[1] == 11 then landing = v end end
check(continued and continued[2] == 30050 and continued[3] == 1 and landing and landing[2] == 30050 and landing[3] == 30010,
    'its title Continue reports op 2 (title=1) and warns that it lands away from the host (note 11)')
local unlocked = lockedGuest()
unlocked.lock = 0
unlocked.env.TppMission.ExecuteContinueFromCheckPoint()
check(unlocked.restores == 1 and #unlocked.notes == 0, 'travel lock off (r13=0): the vanilla Continue runs at once')
print(checks .. ' exact-game W3 Continue/load checks passed; no game')
