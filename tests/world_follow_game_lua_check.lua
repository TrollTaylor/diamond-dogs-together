-- W3 startup Continue against exact extracted game function bodies. The engine
-- records calls; no game, real save, external service or transport is used.
-- Args: world_follow.lua, extracted TppMission.lua, extracted TppMain.lua.
local checks = 0
local function check(ok, why) checks = checks + 1; if not ok then error(checks .. ': ' .. why, 2) end end
local function read(path) local f = assert(io.open(path, 'rb')); local s = f:read('*a'); f:close(); return s end
local function slice(s, first, last)
    local a = assert(s:find(first, 1, true), first)
    local b = assert(s:find(last, a + #first, true), last)
    return s:sub(a, b - 1)
end
local missionSource, mainSource = read(arg[2]), read(arg[3])
local continueSource = slice(missionSource, 'function e.ExecuteContinueFromCheckPoint(', 'function e.IncrementRetryCount(')
local loadSource = slice(missionSource, 'function e.RequestLoad(', 'function e.ExecuteReload(')
local reserveSource = slice(mainSource, 'function e.ReservePlayerLoadingPosition(', 'function e.OnReload(')
local OP = {TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13, REJECT = 4}
local locations = {[30010] = 10, [40010] = 10, [30020] = 20, [40020] = 20, [30050] = 50, [40050] = 50}
local function fixture(host, target, level, active)
    local c = {time = 1000, tx = {}, logs = {}, chunkChecks = {}, engine = {}, stages = {}}
    local env = setmetatable({}, {__index = _G})
    local function noop() end
    env.vars = {missionCode = 40020, locationCode = 20, mbLayoutCode = 0, mbClusterId = 2,
        initialPlayerFlag = 1, initialPlayerPosX = 99, initialPlayerPosY = 88, initialPlayerPosZ = 77, initialPlayerRotY = 66}
    env.mvars, env.svars = {}, {mis_missionClearType = 6}
    env.gvars = {usingNormalMissionSlot = false, str_storySequence = 200, solface_groupNumber = 11, hosface_groupNumber = 22,
        rev_revengeRandomValue = 33, rev_revengeLv = {[0] = 1, [1] = 2}, missionStartClock = 43200,
        missionStartWeather = 0, missionStartWeatherNextTime = 123.25, missionStartExtraWeatherInterval = 200,
        heli_missionStartRoute = 444, mis_isStartFromHelispace = false, mis_isStartFromFreePlay = true,
        ply_initialPlayerState = 0, ply_useMissionStartPos = true, ply_missionStartPos = {[0] = 2, [1] = 3, [2] = 4},
        ply_missionStartRot = 0, ply_useMissionStartPosForNoOrderBox = false,
        ply_missionStartPosForNoOrderBox = {[0] = 0, [1] = 0, [2] = 0}, ply_missionStartRotForNoOrderBox = 0,
        mis_checkPoint = 0}
    env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 1, MISSION_ABORT = 2, MISSION_RESTART = 3, CONTINUE_FROM_CHECK_POINT = 4},
        STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 100}, STORY_MISSION_LAYOUT_CODE = {},
        SYS_MISSION_ID = {MTBS_HELI = 40050}, ERROR_ID = {NOW_INSTALLING = 123}}
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
        UnsetRetryFlag = noop, SetRetryFlag = noop, SetRetryFlagWithChickCap = noop}
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
        local m = host and target or 40020
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
    end, VarSaveOnRetry = noop, SaveGameData = function(m) c.savedMission = m end}
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
        ResetEmegerncyMissionSetting = noop, ExecuteOnReturnToMissionCallback = function() return false end}
    env.TppMain = {EnablePause = noop, EnableBlackLoading = noop, DisableGameStatus = noop}
    env.e = env.TppMain
    local function compile(text)
        local chunk = assert(loadstring(text)); setfenv(chunk, env); chunk()
    end
    compile('local e=TppMission\n' .. continueSource .. loadSource)
    compile('local e=TppMain\n' .. reserveSource)
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {},
        level = level or 3, role = host and 1 or 2, host = host, active = active ~= false, mission = 40020,
        epoch = 7, paired = true, eff = 3}
    env.e.MgsCoopWorld = W
    W.get = function(t, k) return t and t[k] end
    W.h = function() return 444 end
    W.now = function() return c.time end
    W.inGame = function() return false end
    W.nextRid = function() c.rid = (c.rid or 0) + 1; return c.rid end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    W.fail = function(text) c.failure = text; W.active = false end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op = op, v = {...}}; return true end
    compile(read(arg[1]))
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
end
local h, blocked = fixture(true, 30020), fixture(false, 30020)
hostContinue(h); transfer(h, blocked, true); blocked.F.tick(blocked.time)
check(not blocked.restores and not blocked.env.mvars.mis_loadRequest and not blocked.loaded,
    'missing completed seed stream prevents actual Continue and all load side effects')
for _, settings in ipairs({{0, true}, {3, false}}) do
    local untouched = fixture(false, 30020, settings[1], settings[2])
    untouched.env.TppMission.ExecuteContinueFromCheckPoint()
    local request = untouched.env.mvars.mis_loadRequest
    check(request and request.nextMission == 40020 and request.currentMission == 40020,
        'level0/inactive vanilla Continue remains its own saved ACC destination')
    untouched.env.TppMission.LoadWithChunkCheck()
    check(untouched.reloaded == 40020 and not untouched.loaded and untouched.chunkChecks[1] == 20,
        'inert Load follows exact vanilla RequestToReload path and original chunk')
end
print(checks .. ' exact-game W3 Continue/load checks passed; no game')
