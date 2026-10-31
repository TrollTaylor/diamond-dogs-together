-- W3 flow tests with recording Lua APIs. No game or real transport.
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(tostring(checks) .. ': ' .. why, 2) end
end
local OP = {TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13, REJECT = 4}
local function fixture(role, level)
    local c = {calls = {}, tx = {}, logs = {}, notes = {}, time = 1000, inGame = true}
    local env = setmetatable({}, {__index = _G})
    env.math = setmetatable({randomseed = function(seed) c.randomSeeds = (c.randomSeeds or 0) + 1; c.randomSeed = seed end}, {__index = math})
    env.vars = {missionCode = 30050, locationCode = 50, mbLayoutCode = 0, mbClusterId = 2,
        initialPlayerFlag = 1, initialPlayerPosX = -123.125, initialPlayerPosY = 34.625,
        initialPlayerPosZ = 456.875, initialPlayerRotY = -45.125}
    env.mvars = {mis_selectedDeployTime = 0}
    env.gvars = {solface_groupNumber = 111, hosface_groupNumber = 222, heli_missionStartRoute = 4294967295,
        rev_revengeRandomValue = 333, rev_revengeLv = {[0] = 1, [1] = 2}, missionStartClock = 43200,
        missionStartWeather = 0, missionStartWeatherNextTime = 123.25, missionStartExtraWeatherInterval = 200,
        mis_isStartFromHelispace = true, mis_isStartFromFreePlay = false, ply_initialPlayerState = 0,
        ply_useMissionStartPos = true, ply_missionStartPos = {[0] = -2.25, [1] = 3.5, [2] = 10},
        ply_missionStartRot = -90, ply_useMissionStartPosForNoOrderBox = false,
        ply_missionStartPosForNoOrderBox = {[0] = 0, [1] = 0, [2] = 0}, ply_missionStartRotForNoOrderBox = 0,
        mis_checkPoint = 4294967295, ini_isTitleMode = false, ini_isReturnToTitle = false, isContinueFromTitle = true}
    env.svars = {mis_missionClearType = 6, ply_isUsedPlayerInitialAction = true}
    env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 1, MISSION_ABORT = 2, MISSION_RESTART = 3, CONTINUE_FROM_CHECK_POINT = 4}}
    env.TppRevenge = {REVENGE_TYPE = {MAX = 2}}
    env.DemoDaemon = {IsDemoPlaying = function() return c.demo == true end}
    local function record(name) c.calls[#c.calls + 1] = name end
    env.e = {ReservePlayerLoadingPosition = function() record('reserve_position'); return 7, nil, 9 end,
        OnAllocate = function() coroutine.yield('allocate'); return 3, nil, 5 end}
    env.TppMission = {
        ReserveMissionClear = function(t) c.clear = t; record('clear'); return c.refuse ~= true end,
        AbortMission = function(t) c.abort = t; record('abort') end,
        SelectNextMissionHeliStartRoute = function(m, route) c.route = route; c.next = m; record('route') end,
        OnEndMissionPreparation = function(t, cluster)
            c.deploy = t; c.cluster = cluster; record('deploy')
            if c.deployError then error(c.deployError, 0) end
            -- c.nested: game-shaped (TppMission.lua OnEndMissionPreparation / GoToEmergencyMission),
            -- the clear goes through the (wrapped) TppMission.ReserveMissionClear.
            if c.nested then
                local emergency = env.gvars.mis_nextMissionCodeForEmergency
                env.TppMission.ReserveMissionClear{missionClearType = 3, nextClusterId = cluster,
                    nextMissionId = (emergency and emergency ~= 0) and emergency or env.gvars.mis_nextMissionCodeForMissionClear}
            end
        end,
        Load = function(m) record('load'); c.loaded = m; c.seedAtLoad = env.gvars.solface_groupNumber end,
        LoadWithChunkCheck = function() record('load_check'); env.TppMission.Load(env.mvars.mis_loadRequest.nextMission); env.mvars.mis_loadRequest = nil end,
        ReserveMissionClearOnRideOnHelicopter = function() record('heli') end,
        AbortForOutOfMissionArea = function() record('area') end,
        ExecuteRestartMission = function(...) c.restartArgs = select('#', ...); record('restart') end,
        -- The pause menu's own entries (fb1031 arrival: the stuck guest's way out).
        RestartMission = function() record('restart_menu') end,
        ContinueFromCheckPoint = function() record('continue_menu') end,
        ExecuteContinueFromCheckPoint = function() record('continue') end,
        Reload = function() record('reload') end,
        ReturnToMission = function() record('return') end,
        GameOverReturnToTitle = function() record('title') end}
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {},
        level = level or 3, role = role, host = role == 1, active = true, mission = 30050, epoch = 7, paired = true, eff = 3}
    env.e.MgsCoopWorld = W
    W.get = function(t, k) return t and t[k] end
    W.cfg = function() return {c.configLevel or W.level, c.configRole or W.role, 1, c.link or 1, 7, 0, 1, c.session or 55} end
    W.h = function() return 4294967295 end
    W.now = function() return c.time end
    W.inGame = function() return c.inGame end
    W.nextRid = function() c.rid = (c.rid or 0) + 1; return c.rid end
    W.log = function(s) c.logs[#c.logs + 1] = s end
    W.tx = function(op, ...)
        if c.failOp == op then c.failed = (c.failed or 0) + 1; return false end
        if op == OP.SEED and c.failSeedAfter and #c.tx >= c.failSeedAfter then c.failed = (c.failed or 0) + 1; return false end
        c.tx[#c.tx + 1] = {op = op, v = {...}}; return true
    end
    W.fail = function(s) c.failure = s; W.active = false end
    -- NOTE recorder and the partner view (c.peer nil: an older loader, the legacy hold).
    W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
    W.peer = function() return c.peer end
    for _, path in ipairs({arg[2], arg[1]}) do
        local chunk = assert(loadfile(path)); setfenv(chunk, env); chunk()
    end
    W.follow.install({level or 3, role})
    for _, f in ipairs(W.startHooks) do f() end
    for _, f in ipairs(W.pairedHooks) do f(false) end
    c.W, c.env, c.F = W, env, W.follow
    return c
end
local function send(c, op, v) c.W.handlers[op](v, c.time) end
local function transfer(h, g)
    for _, p in ipairs(h.tx) do if p.op == OP.TRANSITION or p.op == OP.SEED or p.op == OP.SEEDS_END then send(g, p.op, p.v) end end
end
local function donor(target)
    local h = fixture(1)
    h.env.vars.missionCode = target
    h.env.e.ReservePlayerLoadingPosition(1)
    h.env.TppMission.Load(target)
    for _ = 1, 4 do h.F.tick(h.time) end
    return h
end
local function seedStream(g, target, id, filter, duplicateKey)
    local h, count, digest = donor(target), 0, 0
    for _, p in ipairs(h.tx) do
        if p.op == OP.SEED and (not filter or filter(p.v)) then
            local v = {id, target, p.v[3], p.v[4], p.v[5]}
            send(g, OP.SEED, v)
            count, digest = count + 1, (digest * 31 + v[3] * 1024 + v[4] + v[5]) % 4294967296
            if v[3] == duplicateKey and v[4] == 0 then
                send(g, OP.SEED, v)
                count, digest = count + 1, (digest * 31 + v[3] * 1024 + v[4] + v[5]) % 4294967296
            end
        end
    end
    send(g, OP.SEEDS_END, {id, target, count, digest})
end
local h, g = fixture(1), fixture(2)
g.env.gvars.ini_isTitleMode, g.env.gvars.ini_isReturnToTitle, g.env.gvars.isContinueFromTitle = true, true, false
h.env.vars.missionCode = 40050
local a, b, d = h.env.e.ReservePlayerLoadingPosition(1)
check(a == 7 and b == nil and d == 9, 'original choke-point returns preserved')
h.env.TppMission.Load(40050)
h.F.tick(h.time)
check(#h.tx > 20 and h.tx[1].op == OP.TRANSITION and h.tx[#h.tx].op == OP.SEEDS_END, 'host publishes transition then complete seed stream')
transfer(h, g)
g.F.tick(g.time)
check(g.env.gvars.ini_isTitleMode == false and g.env.gvars.ini_isReturnToTitle == false and g.env.gvars.isContinueFromTitle == true,
    'authorized replay copies one-time host title context before guest mission transition')
local titleLog
for _, entry in ipairs(g.logs) do if entry:find('step=title_context', 1, true) then titleLog = entry end end
check(titleLog and #titleLog <= 160 and titleLog:find('title_before=1;title_after=0', 1, true) and
    titleLog:find('return_before=1;return_after=0', 1, true) and titleLog:find('continue_before=0;continue_after=1', 1, true),
    'bounded replay context receipt records all observed before/after flags')
check(g.clear and g.clear.nextMissionId == 40050 and g.clear.nextHeliRoute == 4294967295, 'guest replays host clear and unsigned route')
g.env.gvars.solface_groupNumber = 999
g.env.gvars.isContinueFromTitle = false
g.env.svars.ply_isUsedPlayerInitialAction = false
g.env.mvars.mis_loadRequest = {nextMission = 40050}
g.env.TppMission.LoadWithChunkCheck()
check(g.loaded == 40050 and g.seedAtLoad == 111 and g.env.mvars.mis_loadRequest == nil, 'host seeds applied before original Load and request cleanup')
check(g.env.gvars.isContinueFromTitle == false, 'repeated seed preparation preserves vanilla title-continue cleanup')
check(g.env.svars.ply_isUsedPlayerInitialAction == true, 'host saved player initial-action flag replaces old guest helicopter entry')
check(g.env.gvars.ply_missionStartPos[0] == -2.25 and g.env.gvars.ply_missionStartRot == -90 and
    g.env.gvars.missionStartWeatherNextTime == 123.25 and g.env.gvars.rev_revengeRandomValue == 333,
    'signed positions, rotations, scalar revenge seed and float weather round trip')
check(g.env.vars.initialPlayerFlag == 1 and g.env.vars.initialPlayerPosX == -123.125 and
    g.env.vars.initialPlayerRotY == -45.125 and g.env.gvars.mis_checkPoint == 4294967295 and
    g.env.vars.missionCode == 40050 and g.env.vars.locationCode == 50,
    'saved initial pose, checkpoint hash and authoritative destination round trip')
g.W.mission, g.env.vars.missionCode = 40050, 40050
for _, f in ipairs(g.W.startHooks) do f() end
check(g.F.current == nil and #g.F.queue == 0 and g.F.last == 1, 'arrival retires one transition')
check(g.tx[#g.tx].op == OP.FOLLOW_ACK and g.tx[#g.tx].v[3] == 2, 'arrival acknowledgement')
send(g, OP.TRANSITION, h.tx[1].v)
check(#g.F.queue == 0, 'completed transition cannot replay twice')
local blocked = fixture(2)
check(blocked.env.TppMission.ReserveMissionClear{nextMissionId = 40050} == false, 'guest clear has vanilla refusal shape')
blocked.env.TppMission.ReserveMissionClearOnRideOnHelicopter()
blocked.env.TppMission.AbortForOutOfMissionArea()
blocked.env.TppMission.ExecuteRestartMission()
blocked.env.TppMission.ExecuteContinueFromCheckPoint()
check(#blocked.calls == 0 and blocked.tx[1].op == OP.REQ_MISSION, 'guest heli, boundary and captured menu methods are suppressed')
local off = fixture(2, 0)
off.env.TppMission.ReserveMissionClearOnRideOnHelicopter()
check(off.calls[1] == 'heli' and #off.tx == 0, 'world zero transparent')
local wait = fixture(2)
local t = {5, 1, 30050, 40050, 6, 10, 0, 2, 0, 0, 8}
send(wait, OP.TRANSITION, t)
wait.F.tick(wait.time)
check(#wait.calls == 0 and wait.F.current == nil, 'guest waits for complete seed stream')
send(wait, OP.SEED, {5, 40050, 1, 0, 111})
wait.F.tick(wait.time)
check(#wait.calls == 0, 'partial seeds do not reserve a clear')
local complete = donor(40050)
for _, p in ipairs(complete.tx) do
    if p.op == OP.SEED and p.v[3] ~= 1 then send(wait, p.op, {5, 40050, p.v[3], p.v[4], p.v[5]})
    elseif p.op == OP.SEEDS_END then send(wait, p.op, {5, 40050, p.v[3], p.v[4]}) end
end
wait.F.tick(wait.time)
check(wait.F.current and wait.calls[1] == 'clear', 'complete checked stream permits replay')
wait.F.seeds[5].ready = false
wait.env.mvars.mis_loadRequest = {nextMission = 40050}
wait.env.TppMission.LoadWithChunkCheck()
check(wait.env.mvars.mis_loadRequest ~= nil and wait.loaded == nil, 'load barrier retains the vanilla load request')
local bad = fixture(2)
send(bad, OP.SEED, {1, 40050, 1, 0, 42})
send(bad, OP.SEEDS_END, {1, 40050, 1, 99})
check(bad.failure and #bad.calls == 0, 'corrupt snapshot stops follow without game calls')
local solo = fixture(1)
solo.W.paired = false
solo.env.TppMission.OnEndMissionPreparation(2, nil)
check(#solo.calls == 0 and solo.F.hostDeploy ~= nil, 'host sortie waits for guest ready')
solo.W.paired = true
solo.F.tick(solo.time)
check(solo.deploy == 2 and solo.cluster == nil and solo.F.hostDeploy == nil, 'ready releases exactly one held sortie with nil args')
local co = coroutine.create(function() return solo.env.e.OnAllocate(1, nil, 3) end)
local ok, yielded = coroutine.resume(co)
check(ok and yielded == 'allocate', 'original allocation can yield through wrapper')
local ok2, x, y, z = coroutine.resume(co)
check(ok2 and x == 3 and y == nil and z == 5, 'allocation returns preserved')
local unsupported = fixture(2)
send(unsupported, OP.TRANSITION, {1, 5, 30050, 30050, 0, 0, 0, 2, 0, 0, 8})
check(#unsupported.F.queue == 0 and unsupported.tx[1].v[3] == 0, 'mid-mission reload remains unsupported')
local reconnect = fixture(2)
reconnect.F.last = 5
for _, f in ipairs(reconnect.W.unpairedHooks) do f(1) end
send(reconnect, OP.TRANSITION, {1, 1, 30050, 40050, 6, 0, 0, 2, 0, 0, 8})
check(#reconnect.F.queue == 1 and reconnect.F.last == 0, 'new host process starts a fresh travel sequence')
local pressure, receiver = fixture(1), fixture(2)
pressure.failOp = OP.TRANSITION
pressure.env.vars.missionCode = 40050
pressure.env.e.ReservePlayerLoadingPosition(1)
pressure.env.TppMission.Load(40050)
check(#pressure.tx == 0 and pressure.F.outgoing.values and not pressure.F.outgoing.announced,
    'transition queue pressure keeps a frozen local seed stream')
pressure.env.gvars.solface_groupNumber = 999
pressure.failOp, pressure.failSeedAfter = nil, 4
pressure.F.tick(pressure.time)
check(#pressure.tx == 4 and pressure.F.outgoing.i == 4, 'seed queue pressure retains next unsent item')
pressure.failSeedAfter, pressure.failOp = nil, OP.SEEDS_END
pressure.F.tick(pressure.time)
check(not pressure.F.outgoing.sent and pressure.F.outgoing.i == #pressure.F.outgoing.values + 1,
    'end queue pressure retains completion')
local beforeEnd = #pressure.tx
pressure.failOp = nil
pressure.F.tick(pressure.time)
check(pressure.F.outgoing.sent and #pressure.tx == beforeEnd + 1 and pressure.tx[#pressure.tx].op == OP.SEEDS_END,
    'completion retry does not duplicate accepted seeds')
transfer(pressure, receiver)
receiver.F.tick(receiver.time)
receiver.env.TppMission.Load(40050)
check(receiver.loaded == 40050 and receiver.seedAtLoad == 111 and not receiver.failure,
    'backpressured travel freezes host seeds and guest validates the complete ordered stream')
local mixed = fixture(1)
mixed.W.paired = false
mixed.env.TppMission.OnEndMissionPreparation(1, 2)
mixed.W.paired, mixed.W.eff = true, 2
mixed.F.tick(mixed.time)
check(mixed.calls[1] == 'deploy' and mixed.F.hostDeploy == nil and not mixed.F.deploying,
    'lower negotiated level releases held sortie without enabling travel')
local mixedGuest = fixture(2)
mixedGuest.W.eff = 2
for _, f in ipairs(mixedGuest.W.pairedHooks) do f(false) end
mixedGuest.env.TppMission.ReserveMissionClearOnRideOnHelicopter()
check(mixedGuest.calls[1] == 'heli' and #mixedGuest.tx == 0, 'lower negotiated level preserves vanilla guest travel')
local story = fixture(1)
check(story.env.TppMission.ReserveMissionClear{nextMissionId = 10020} == false and #story.calls == 0,
    'story mission clear is refused before changing vanilla state')
story.env.gvars.mis_nextMissionCodeForMissionClear = 10020
story.env.TppMission.OnEndMissionPreparation(1, 2)
check(#story.calls == 0 and story.F.hostDeploy == nil, 'story deployment is refused before original preparation')
local nested = fixture(2)
local tokenHeld = false
nested.F.run(function()
    nested.F.run(function() end)
    tokenHeld = nested.F.token
end)
check(tokenHeld and not nested.F.token, 'nested replay restores the outer replay token')
local restart = fixture(2)
send(restart, OP.TRANSITION, {1, 3, 30050, 30050, 0, 0, 0, 2, 0, 0, 8})
seedStream(restart, 30050, 1)
restart.F.tick(restart.time)
check(restart.calls[1] == 'restart' and restart.restartArgs == 0,
    'ordinary ExecuteRestartMission receives no truthy return-to-mission argument')
local noReload = fixture(1)
noReload.env.TppMission.Reload()
noReload.env.TppMission.ReturnToMission()
check(#noReload.calls == 0, 'unsupported host midmission reload and ReturnToMission are refused before side effects')
local deadAbort = fixture(2)
deadAbort.inGame = false
deadAbort.env.svars.mis_isDefiniteGameOver = true
send(deadAbort, OP.TRANSITION, {1, 2, 30050, 40050, 0, 0, 0, 2, 0, 1, 8})
seedStream(deadAbort, 40050, 1)
deadAbort.F.tick(deadAbort.time)
check(deadAbort.calls[1] == 'abort' and deadAbort.abort.nextMissionId == 40050 and deadAbort.abort.isNoSave == true and
    deadAbort.abort.isAlreadyGameOver == true and deadAbort.abort.isNoFade == true,
    'host game-over abort replays while gameplay is disabled with original noSave option')
local menuAbort = fixture(1)
menuAbort.env.vars.missionCode, menuAbort.env.mvars.mis_abortWithSave = 40050, false
menuAbort.env.e.ReservePlayerLoadingPosition(2)
check(menuAbort.tx[1].v[10] == 1, 'host menu abort captures actual choke-point no-save state')
local function invalidStream(filter, why)
    local g = fixture(2)
    send(g, OP.TRANSITION, {1, 1, 30050, 40050, 6, 10, 0, 2, 0, 0, 8})
    seedStream(g, 40050, 1, filter)
    g.F.tick(g.time)
    check(g.failure and not g.F.current and #g.F.queue == 0 and next(g.F.seeds) == nil and #g.calls == 0 and not g.loaded, why)
end
invalidStream(function() return false end, 'self-consistent empty count0/digest0 cannot authorize travel')
invalidStream(function(v) return v[3] == 1 end, 'self-consistent one-field stream cannot authorize travel')
invalidStream(function(v) return v[3] < 19 end, 'old complete18-key stream without checkpoint pose/hashes is refused')
for missing = 19, 28 do
    invalidStream(function(v) return v[3] ~= missing end, 'missing mandatory seed key' .. missing .. ' is refused')
end
invalidStream(function(v) return not (v[3] == 4 and v[4] == 1) end,
    'self-consistent stream missing one required revenge array cell is refused')
local duplicate = fixture(2)
send(duplicate, OP.TRANSITION, {1, 1, 30050, 40050, 6, 10, 0, 2, 0, 0, 8})
seedStream(duplicate, 40050, 1, nil, 1)
duplicate.F.tick(duplicate.time)
check(duplicate.failure and not duplicate.F.current and #duplicate.F.queue == 0 and next(duplicate.F.seeds) == nil and #duplicate.calls == 0,
    'self-consistent count/digest with duplicate seed key/index cannot pass coverage')
for _, missing in ipairs({'hosface_groupNumber', 'rev_revengeLv', 'initialPlayerPosZ'}) do
    local unreadable = fixture(1)
    if missing == 'initialPlayerPosZ' then unreadable.env.vars[missing] = nil
    elseif missing == 'rev_revengeLv' then unreadable.env.gvars[missing][1] = nil
    else unreadable.env.gvars[missing] = nil end
    unreadable.env.vars.missionCode = 40050
    unreadable.env.e.ReservePlayerLoadingPosition(1)
    unreadable.env.mvars.mis_loadRequest = {nextMission = 40050, currentMission = 30050}
    local request = unreadable.env.mvars.mis_loadRequest
    unreadable.env.TppMission.LoadWithChunkCheck()
    check(not unreadable.loaded and unreadable.env.mvars.mis_loadRequest == request,
        'host unreadable mandatory field ' .. missing .. ' retains request without Load')
    local ended = false
    for _, p in ipairs(unreadable.tx) do if p.op == OP.SEEDS_END then ended = true end end
    check(not ended, 'host unreadable ' .. missing .. ' never emits a partial completion marker')
end
local handoff = fixture(2)
transfer(donor(40050), handoff); handoff.F.tick(handoff.time)
handoff.W.active = false
handoff.env.vars.missionCode = 40050
handoff.configLevel = 0
local alloc = coroutine.create(function() return handoff.env.e.OnAllocate() end)
check(coroutine.resume(alloc) and not handoff.randomSeeds and not handoff.F.current,
    'explicit level0 pending allocation cannot seed RNG and clears accepted travel')
local disconnected = fixture(1)
disconnected.env.vars.missionCode = 40050
disconnected.env.e.ReservePlayerLoadingPosition(1)
disconnected.env.TppMission.Load(40050)
disconnected.F.current = disconnected.F.outgoing
for _, f in ipairs(disconnected.W.unpairedHooks) do f(5) end
local orphan = coroutine.create(function() return disconnected.env.e.OnAllocate() end)
check(coroutine.resume(orphan) and not disconnected.randomSeeds and not disconnected.F.current and not disconnected.F.outgoing,
    'session reason5 clears retained current/outgoing before allocation and never applies stale RNG')
local negotiated = fixture(1)
negotiated.env.vars.missionCode = 40050
negotiated.env.e.ReservePlayerLoadingPosition(1)
negotiated.env.TppMission.Load(40050)
negotiated.W.eff = 2
local limited = coroutine.create(function() return negotiated.env.e.OnAllocate() end)
check(coroutine.resume(limited) and not negotiated.randomSeeds,
    'paired effective level2 cannot seed allocation from stale level3 outgoing travel')
local down = fixture(2)
transfer(donor(40050), down); down.link = 0; down.F.tick(down.time)
check(not down.F.current and #down.calls == 0, 'link-down configuration defers first travel replay before any vanilla call')
down.link = 1; down.F.tick(down.time)
check(down.F.current and down.F.current.session == 55 and down.calls[1] == 'clear',
    'reconnected live session is captured before first authorized replay')
-- Join-fix phase 1: the bounded host sortie hold, unsupported destinations,
-- the sticky guest arm and title progress (docs/join-fix-phase1-spec.md C2-C9).
local function called(c, name)
    local n = 0
    for _, v in ipairs(c.calls) do if v == name then n = n + 1 end end
    return n
end
local function noted(c, code, a, b)
    local n = 0
    for _, v in ipairs(c.notes) do if v[1] == code and (a == nil or v[2] == a) and (b == nil or v[3] == b) then n = n + 1 end end
    return n
end
local function logged(c, text)
    for _, s in ipairs(c.logs) do if s:find(text, 1, true) then return s end end
    return nil
end
local function heli(target)
    local c = fixture(1)
    c.W.mission, c.env.vars.missionCode, c.W.paired = 40050, 40050, false
    c.env.gvars.mis_nextMissionCodeForMissionClear, c.env.gvars.mis_nextMissionCodeForEmergency = target or 30020, 0
    return c
end
local elsewhere = {mission = 10020, level = 3, wait = 9, linked = true, session = 55}
-- Nobody can come: no link.
local lone = heli(); lone.link = 0
lone.env.TppMission.OnEndMissionPreparation(2, nil)
check(called(lone, 'deploy') == 1 and lone.deploy == 2 and lone.F.hostDeploy == nil and not lone.F.deploying and
    noted(lone, 3, 30020, 1) == 1 and logged(lone, 'step=deploy_alone;reason=no_partner;target=30020'),
    'no link: the sortie leaves at once with the no_partner note')
-- The partner is linked but in another mission.
local away = heli(); away.peer = elsewhere
away.env.TppMission.OnEndMissionPreparation(2, nil)
check(called(away, 'deploy') == 1 and away.F.hostDeploy == nil and noted(away, 3, 30020, 2) == 1 and
    logged(away, 'step=deploy_alone;reason=partner_elsewhere;target=30020'), 'partner in another mission: leaves at once')
for _, p in ipairs({{mission = 40050, level = 3, wait = 3, linked = false, session = 55},
    {mission = 40050, level = 2, wait = 10, linked = true, session = 55}}) do
    local c = heli(); c.peer = p
    c.env.TppMission.OnEndMissionPreparation(2, nil)
    check(called(c, 'deploy') == 1 and noted(c, 3, 30020, 1) == 1, 'unlinked or lower-level partner cannot come: alone')
end
-- The guest is still replaying the host's 30050 -> 40050 transition (verdict RC4(b)).
local function following()
    local c = fixture(1)
    c.env.vars.missionCode = 40050
    c.env.e.ReservePlayerLoadingPosition(1)
    check(c.F.outgoing and c.F.outgoing[3] == 30050 and c.F.outgoing[4] == 40050 and c.F.outgoing.session == 55,
        'host published the transition into the helicopter')
    c.W.mission, c.W.paired = 40050, false
    c.env.gvars.mis_nextMissionCodeForMissionClear, c.env.gvars.mis_nextMissionCodeForEmergency = 30020, 0
    c.peer = {mission = 30050, level = 3, wait = 9, linked = true, session = 55}
    c.env.TppMission.OnEndMissionPreparation(2, nil)
    return c
end
local f1 = following()
check(called(f1, 'deploy') == 0 and f1.F.hostDeploy and f1.F.hostDeploy.target == 30020 and f1.F.hostDeploy.mission == 40050 and
    noted(f1, 2, 30020, 0) == 1 and logged(f1, 'step=deploy_wait;reason=ready;target=30020;partner=30050'),
    'a guest still following the host transition holds the sortie with a note')
send(f1, OP.FOLLOW_ACK, {f1.F.outgoing[1], 40050, 2})
f1.peer = {mission = 40050, level = 3, wait = 10, linked = true, session = 55}
f1.time = f1.time + 500; f1.F.tick(f1.time)
check(f1.F.outgoing.ack == 2 and called(f1, 'deploy') == 0 and f1.F.hostDeploy, 'arrived guest in this helicopter keeps the hold')
f1.W.paired = true; f1.F.tick(f1.time)
check(called(f1, 'deploy') == 1 and f1.deploy == 2 and f1.F.hostDeploy == nil and f1.F.deploying == true and noted(f1, 3) == 0,
    'pairing releases the held sortie as a kind-6 follow')
local f0 = following()
check(called(f0, 'deploy') == 0 and f0.F.hostDeploy, 'held while the guest follows')
send(f0, OP.FOLLOW_ACK, {f0.F.outgoing[1], 30050, 0})
f0.F.tick(f0.time); f0.time = f0.time + 499; f0.F.tick(f0.time)
check(called(f0, 'deploy') == 0, 'the hold is re-checked every 500 ms')
f0.time = f0.time + 1; f0.F.tick(f0.time)
check(called(f0, 'deploy') == 1 and f0.F.hostDeploy == nil and noted(f0, 3, 30020, 2) == 1 and not f0.F.deploying,
    'refused follow: the next check releases alone with reason 2')
local fs = following(); fs.peer.session = 99; fs.time = fs.time + 500; fs.F.tick(fs.time)
check(called(fs, 'deploy') == 1 and noted(fs, 3, 30020, 2) == 1, 'another link session is not the following guest')
-- A second choice of the deployment (1 s debounce) flies alone with the new arguments.
local again = heli()
again.env.TppMission.OnEndMissionPreparation(2, nil)
check(again.F.hostDeploy and called(again, 'deploy') == 0 and noted(again, 2, 30020, 0) == 1, 'legacy loader: held')
again.time = again.time + 999
again.env.TppMission.OnEndMissionPreparation(5, 6)
check(called(again, 'deploy') == 0 and again.F.hostDeploy and again.F.hostDeploy[1] == 2 and #again.notes == 1,
    'a second choice within 999 ms changes nothing')
again.time = again.time + 1
again.env.TppMission.OnEndMissionPreparation(5, 6)
check(called(again, 'deploy') == 1 and again.deploy == 5 and again.cluster == 6 and again.F.hostDeploy == nil and
    noted(again, 3, 30020, 3) == 1 and logged(again, 'step=deploy_alone;reason=repeat;target=30020'),
    'a second choice after 1 s calls the original once with its own arguments')
again.time = again.time + 1000; again.F.tick(again.time)
check(called(again, 'deploy') == 1, 'no second release after the repeat')
-- The 120 s bound.
local slow = heli()
slow.env.TppMission.OnEndMissionPreparation(2, nil)
slow.time = slow.time + 119999; slow.F.tick(slow.time)
check(called(slow, 'deploy') == 0 and slow.F.hostDeploy, 'held up to 120 s')
slow.time = slow.time + 500; slow.F.tick(slow.time)
check(called(slow, 'deploy') == 1 and slow.deploy == 2 and noted(slow, 3, 30020, 4) == 1 and
    logged(slow, 'step=deploy_alone;reason=timeout;target=30020'), 'after 120 s the sortie leaves alone')
local bound = heli()
bound.env.TppMission.OnEndMissionPreparation(2, nil)
bound.time = bound.time + 120000; bound.F.tick(bound.time)
check(called(bound, 'deploy') == 1 and noted(bound, 3, 30020, 4) == 1, 'released at exactly 120000 ms')
-- The note is re-posted every 3 s.
local nag = heli()
nag.env.TppMission.OnEndMissionPreparation(2, nil)
nag.time = nag.time + 2999; nag.F.tick(nag.time)
check(noted(nag, 2) == 1, 'no repeat before 3 s')
local nag2 = heli()
nag2.env.TppMission.OnEndMissionPreparation(2, nil)
nag2.time = nag2.time + 3000; nag2.F.tick(nag2.time)
nag2.time = nag2.time + 500; nag2.F.tick(nag2.time)
check(noted(nag2, 2, 30020, 0) == 2 and called(nag2, 'deploy') == 0, 'one more wait note after 3 s')
-- A stale hold (another mission) is dropped without the original.
local stale = heli()
stale.env.TppMission.OnEndMissionPreparation(2, nil)
stale.W.mission = 30020; stale.F.tick(stale.time)
check(stale.F.hostDeploy == nil and called(stale, 'deploy') == 0 and logged(stale, 'step=deploy_held;reason=stale;mission=40050'),
    'mission change drops the held sortie')
-- Link loss keeps the hold (F.clear no longer clears it); the next check leaves alone.
local lost = heli()
lost.env.TppMission.OnEndMissionPreparation(2, nil)
for _, f in ipairs(lost.W.unpairedHooks) do f(1) end
check(lost.F.hostDeploy, 'link-session unpair keeps the hold for the tick')
lost.link = 0; lost.time = lost.time + 500; lost.F.tick(lost.time)
check(called(lost, 'deploy') == 1 and noted(lost, 3, 30020, 1) == 1, 'link loss releases alone')
-- World level 0 pushes UNPAIRED 5 and LEVEL 0 together: W.active is false and F.tick
-- no longer runs, so world_sync's inactive update releases the hold through F.hold.
local off2 = heli()
off2.env.TppMission.OnEndMissionPreparation(2, nil)
for _, f in ipairs(off2.W.unpairedHooks) do f(5) end
off2.W.active = false
off2.F.hold()
check(called(off2, 'deploy') == 1 and off2.F.deploying == false and off2.F.hostDeploy == nil and
    logged(off2, 'step=deploy_alone;reason=disabled;target=30020') and noted(off2, 3) == 0,
    'world level 0 releases the held sortie without a note')
-- W.fail during a hold (F.enabled still true), even right after pairing: released, never a kind-6 follow.
local failedHold = heli()
failedHold.env.TppMission.OnEndMissionPreparation(2, nil)
failedHold.W.paired, failedHold.W.eff = true, 3
failedHold.W.fail('module error')
check(failedHold.F.enabled and not failedHold.W.active and called(failedHold, 'deploy') == 0, 'W.fail keeps the hold for the inactive update')
failedHold.F.hold(failedHold.time)
check(called(failedHold, 'deploy') == 1 and failedHold.deploy == 2 and failedHold.F.deploying == false and failedHold.F.hostDeploy == nil and
    logged(failedHold, 'step=deploy_alone;reason=disabled;target=30020') and noted(failedHold, 3) == 0,
    'W.fail during a hold releases it once without enabling travel')
failedHold.F.hold(failedHold.time + 1000)
check(called(failedHold, 'deploy') == 1, 'released once')
-- Unsupported destination: refused once with a note, allowed when chosen again 1-30 s later.
local s1 = fixture(1)
s1.env.gvars.mis_nextMissionCodeForMissionClear = 10020
s1.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s1, 'deploy') == 0 and #s1.notes == 1 and noted(s1, 4, 10020, 0) == 1 and
    logged(s1, 'step=refused;reason=unsupported_mission;target=10020;emergency=nil;fn=OnEndMissionPreparation'),
    'first unsupported choice refused with the note')
s1.time = s1.time + 500
s1.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s1, 'deploy') == 0 and #s1.notes == 1, 'a choice 500 ms later is refused again without a new note')
s1.time = s1.time + 500
s1.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s1, 'deploy') == 1 and s1.deploy == 1 and s1.cluster == 2 and noted(s1, 3, 10020, 3) == 1 and not s1.F.deploying and
    s1.F.refused == nil and logged(s1, 'step=deploy_alone;reason=repeat;target=10020'), 'chosen again after 1 s: allowed')
local s2 = fixture(1)
s2.env.gvars.mis_nextMissionCodeForMissionClear = 10020
s2.env.TppMission.OnEndMissionPreparation(1, 2)
s2.time = s2.time + 30000
s2.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s2, 'deploy') == 1, 'chosen again at 30 s: allowed')
local s3 = fixture(1)
s3.env.gvars.mis_nextMissionCodeForMissionClear = 10020
s3.env.TppMission.OnEndMissionPreparation(1, 2)
s3.time = s3.time + 30001
s3.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s3, 'deploy') == 0 and noted(s3, 4, 10020, 0) == 2 and s3.F.refused.at == s3.time, 'after 30 s it is refused again')
local s4 = fixture(1)
s4.env.gvars.mis_nextMissionCodeForMissionClear, s4.env.gvars.mis_nextMissionCodeForEmergency = 30010, 50050
s4.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s4, 'deploy') == 0 and noted(s4, 4, 30010, 0) == 1, 'an emergency destination is refused the same way')
local s5 = heli(10020); s5.link = 0
s5.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s5, 'deploy') == 1 and noted(s5, 3, 10020, 1) == 1 and #s5.notes == 1, 'nobody can come: allowed at once')
local s6 = heli(10020); s6.peer = {mission = 10036, level = 3, wait = 9, linked = true, session = 55}
s6.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s6, 'deploy') == 1 and noted(s6, 3, 10020, 2) == 1, 'partner elsewhere: allowed at once')
-- Online 2026-10-22 P7: into the partner's own story mission it is a join (b=5), not "flying alone".
local s6j = heli(10020); s6j.peer = elsewhere
s6j.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s6j, 'deploy') == 1 and noted(s6j, 3, 10020, 5) == 1 and noted(s6j, 3, 10020, 2) == 0 and
    logged(s6j, 'step=deploy_alone;reason=join_partner;target=10020'), 'into the partner mission: note 3 b=5 (join_partner)')
-- The supported sortie into the partner's scene says so too.
local s6m = heli(30050); s6m.peer = {mission = 30050, level = 3, wait = 9, linked = true, session = 55}
s6m.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s6m, 'deploy') == 1 and noted(s6m, 3, 30050, 5) == 1, 'into the partner scene: note 3 b=5')
local s7 = heli(10020)
s7.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(s7, 'deploy') == 0 and s7.F.hostDeploy == nil and noted(s7, 4, 10020, 0) == 1, 'legacy loader unpaired: refused once')
local r1 = fixture(1)
check(r1.env.TppMission.ReserveMissionClear{nextMissionId = 10020} == false and called(r1, 'clear') == 0 and
    noted(r1, 4, 10020, 0) == 1 and logged(r1, 'step=refused;reason=unsupported_mission'), 'iDroid clear to a story mission refused once')
r1.time = r1.time + 500
check(r1.env.TppMission.ReserveMissionClear{nextMissionId = 10020} == false and #r1.notes == 1, 'refused again within 1 s, no note')
r1.time = r1.time + 500
check(r1.env.TppMission.ReserveMissionClear{nextMissionId = 10020} == true and called(r1, 'clear') == 1 and
    noted(r1, 3, 10020, 3) == 1, 'iDroid clear chosen again: the original runs')
local r2 = fixture(1); r2.W.paired, r2.link = false, 0
r2.env.TppMission.AbortMission{nextMissionId = 10020}
check(called(r2, 'abort') == 1 and noted(r2, 3, 10020, 1) == 1, 'abort to a story mission with nobody linked: allowed at once')
local r3 = fixture(1)
r3.env.TppMission.ReserveMissionClear{nextMissionId = 30020}
check(called(r3, 'clear') == 1 and #r3.notes == 0, 'supported destinations are untouched')
-- The vanilla deploy reserves its clear through the wrapped ReserveMissionClear
-- (GoToEmergencyMission too): once the sortie is allowed, that inner clear passes.
local function logCount(c, text)
    local n = 0
    for _, s in ipairs(c.logs) do if s:find(text, 1, true) then n = n + 1 end end
    return n
end
local n1 = heli(10020); n1.W.paired, n1.nested = true, true
n1.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(n1, 'deploy') == 0 and called(n1, 'clear') == 0 and #n1.notes == 1 and noted(n1, 4, 10020, 0) == 1,
    'game-shaped deploy, paired: the first story choice is refused')
n1.time = n1.time + 1500
n1.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(n1, 'deploy') == 1 and called(n1, 'clear') == 1 and n1.clear.nextMissionId == 10020 and n1.clear.nextClusterId == 2 and
    #n1.notes == 2 and n1.notes[1][1] == 4 and n1.notes[2][1] == 3 and n1.notes[2][2] == 10020 and n1.notes[2][3] == 3 and
    n1.F.pass == nil and not n1.F.deploying, 'chosen again: the vanilla deploy and its inner clear run, notes 4 then 3')
local n2 = heli(30010); n2.W.paired, n2.nested = true, true
n2.env.gvars.mis_nextMissionCodeForEmergency = 50050
n2.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(n2, 'clear') == 0 and noted(n2, 4, 30010, 0) == 1, 'game-shaped emergency: refused first')
n2.time = n2.time + 1500
n2.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(n2, 'deploy') == 1 and called(n2, 'clear') == 1 and n2.clear.nextMissionId == 50050 and #n2.notes == 2,
    'emergency chosen again: the inner clear to the emergency mission runs')
local n3 = heli(10020); n3.link, n3.nested = 0, true
n3.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(n3, 'deploy') == 1 and called(n3, 'clear') == 1 and #n3.notes == 1 and noted(n3, 3, 10020, 1) == 1 and
    logCount(n3, 'step=deploy_alone') == 1, 'nobody can come: one note and one deploy_alone line')
local n4 = heli(30010); n4.nested = true
n4.peer = {mission = 40050, level = 3, wait = 10, linked = true, session = 55}
n4.env.TppMission.OnEndMissionPreparation(1, 2)
check(n4.F.hostDeploy and called(n4, 'deploy') == 0 and noted(n4, 2, 30010, 0) == 1, 'partner in this helicopter: held')
n4.env.gvars.mis_nextMissionCodeForMissionClear = 10020
n4.time = n4.time + 1500
n4.env.TppMission.OnEndMissionPreparation(1, 2)
check(called(n4, 'deploy') == 1 and called(n4, 'clear') == 1 and n4.clear.nextMissionId == 10020 and n4.F.hostDeploy == nil and
    noted(n4, 3, 10020, 3) == 1 and noted(n4, 4) == 0, 'held, then a story destination chosen: the repeat press leaves with it')
local n5 = heli(); n5.nested = true
n5.env.TppMission.OnEndMissionPreparation(2, nil)
n5.W.paired = true; n5.F.tick(n5.time)
check(called(n5, 'clear') == 1 and n5.clear.nextMissionId == 30020 and n5.F.deploying == true and n5.F.pass == nil,
    'a released hold runs the inner clear (supported target) and restores the pass flag')
check(r1.env.TppMission.ReserveMissionClear{nextMissionId = 10020} == false and called(r1, 'clear') == 1,
    'outside the vanilla deploy the iDroid refusal still applies')
local boom = heli(10020); boom.deployError = 'vanilla failure'
local okBoom, errBoom = pcall(boom.F.originalDeploy, 1, 2)
check(not okBoom and errBoom == 'vanilla failure' and boom.F.pass == nil and called(boom, 'deploy') == 1,
    'a vanilla deploy error is re-raised unchanged and the pass flag is restored')
check(boom.env.TppMission.ReserveMissionClear{nextMissionId = 10020} == false and called(boom, 'clear') == 0,
    'after the error the refusal applies again')
-- The sticky guest arm.
local function armedGuest()
    local c = fixture(2)
    check(c.F.armed, 'paired guest is armed')
    c.W.paired = false
    for _, f in ipairs(c.W.unpairedHooks) do f(4) end
    check(c.F.armed and c.F.unpairedAt == c.time, 'remote_end keeps the arm and starts the unpaired timer')
    return c
end
local sticky = armedGuest()
local t0 = sticky.time
sticky.time = t0 + 119999; sticky.F.tick(sticky.time)
check(sticky.F.armed, 'armed until 120 s unpaired')
sticky.time = t0 + 120000; sticky.F.tick(sticky.time)
check(sticky.F.armed, 'the disarm check runs at most once a second')
sticky.time = t0 + 120999; sticky.F.tick(sticky.time)
check(not sticky.F.armed and logged(sticky, 'step=disarm;reason=unpaired_timeout;partner=nil'), 'disarmed after 120 s unpaired')
local sticky2 = armedGuest()
sticky2.time = sticky2.time + 120000; sticky2.F.tick(sticky2.time)
check(not sticky2.F.armed, 'disarmed at exactly 120000 ms')
check(sticky2.env.TppMission.ReserveMissionClearOnRideOnHelicopter() == nil and called(sticky2, 'heli') == 1,
    'disarmed guest travel is vanilla again')
local busy = armedGuest()
busy.time = busy.time + 60000
send(busy, OP.TRANSITION, {1, 1, 30050, 40050, 6, 0, 0, 2, 0, 0, 8})
busy.time = busy.time + 90000; busy.F.tick(busy.time)
check(busy.F.armed and #busy.F.queue == 1, 'a queued transition keeps the arm past 120 s')
local gone = armedGuest()
gone.F.tick(gone.time)
check(gone.F.armed, 'no peer view: armed')
gone.peer = {mission = 10020, level = 3, wait = 9, linked = true, session = 55}
gone.time = gone.time + 999; gone.F.tick(gone.time)
check(gone.F.armed, 'partner mission checked once a second')
gone.time = gone.time + 1; gone.F.tick(gone.time)
check(not gone.F.armed and logged(gone, 'step=disarm;reason=partner_mission;partner=10020'), 'unsupported partner mission disarms')
local loading = armedGuest()
loading.peer = {mission = 0, level = 3, wait = 8, linked = true, session = 55}
loading.F.tick(loading.time)
check(loading.F.armed, 'a loading partner keeps the arm')
local repaired = armedGuest()
for _, f in ipairs(repaired.W.pairedHooks) do f(false) end
check(repaired.F.unpairedAt == nil and repaired.F.armed, 'pairing clears the unpaired timer')
repaired.W.paired = true
repaired.time = repaired.time + 200000; repaired.F.tick(repaired.time)
check(repaired.F.armed and not logged(repaired, 'step=disarm'), 'never disarmed while paired')
local restarted = armedGuest()
restarted.time = restarted.time + 100000
for _, f in ipairs(restarted.W.startHooks) do f() end
restarted.time = restarted.time + 100000; restarted.F.tick(restarted.time)
check(restarted.F.armed, 'each guest mission start restarts the unpaired timer')
-- Title progress (title-mode helicopter, storySnake read).
local function title(sequence, titleMode)
    local c = fixture(1)
    c.env.TppDefine.STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 100}
    if sequence then c.env.TppStory = {GetCurrentStorySequence = function() return sequence end} end
    c.env.gvars.ini_isTitleMode = titleMode ~= false
    c.W.mission, c.env.vars.missionCode = 40010, 40010
    for _, f in ipairs(c.W.startHooks) do f() end
    return c
end
local story = title(100)
check(#story.notes == 1 and noted(story, 1, 40010, 1) == 1 and story.F.title and story.F.title.story == true and
    logged(story, 'step=progress;title=40010;story_start=1'), 'story-start save at the title helicopter: note')
story.time = story.time + 14999; story.F.tick(story.time)
check(#story.notes == 1, 'no repeat before 15 s')
story.time = story.time + 1; story.F.tick(story.time)
check(noted(story, 1, 40010, 1) == 2, 'repeated after 15 s')
story.env.gvars.ini_isTitleMode = false
story.time = story.time + 15000; story.F.tick(story.time)
check(story.F.title == nil and noted(story, 1) == 2, 'leaving the title clears it without a repeat')
local ok = title(200)
check(#ok.notes == 1 and noted(ok, 1, 40010, 0) == 1 and logged(ok, 'step=progress;title=40010;story_start=0'), 'progressed save: one ok note')
ok.time = ok.time + 30000; ok.F.tick(ok.time)
check(#ok.notes == 1, 'an ok note is never repeated')
check(#title(nil).notes == 0 and title(nil).F.title == nil, 'TppStory missing: no note')
check(#title(100, false).notes == 0, 'not in title mode: no note')
local mb = fixture(1)
mb.env.TppDefine.STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 100}
mb.env.TppStory = {GetCurrentStorySequence = function() return 100 end}
mb.env.gvars.ini_isTitleMode = true
for _, f in ipairs(mb.W.startHooks) do f() end
check(#mb.notes == 0, 'never keyed on a non-helicopter mission')
-- An older loader without W.note/W.peer keeps every old behaviour.
local old = heli()
old.W.note, old.W.peer = nil, nil
old.env.TppMission.OnEndMissionPreparation(2, nil)
check(old.F.hostDeploy and called(old, 'deploy') == 0, 'no W.peer: the legacy hold')
old.W.paired = true; old.F.tick(old.time)
check(called(old, 'deploy') == 1, 'no W.note: released without errors')
-- Join-fix phase 2 C6: world_handoff's title replay hold (F.replayHold) keeps a queued
-- transition; the 120 s timeout still refuses it (docs/join-fix-phase2-spec.md).
local function heldReplay()
    local c = fixture(2)
    send(c, OP.TRANSITION, {1, 1, 30050, 40050, 6, 10, 0, 2, 0, 0, 8})
    seedStream(c, 40050, 1)
    c.hold = true
    c.F.replayHold = function() c.holdCalls = (c.holdCalls or 0) + 1; return c.hold end
    return c
end
local held = heldReplay()
held.F.tick(held.time); held.time = held.time + 60000; held.F.tick(held.time)
check(#held.F.queue == 1 and not held.F.current and #held.calls == 0 and held.holdCalls == 2,
    'a replay hold keeps the queued transition without vanilla calls')
held.hold = false; held.F.tick(held.time)
check(held.F.current and held.calls[1] == 'clear' and held.tx[#held.tx].op == OP.FOLLOW_ACK and held.tx[#held.tx].v[3] == 1,
    'replay hold false: the replay runs')
local expired = heldReplay()
expired.time = expired.time + 120001; expired.F.tick(expired.time)
check(#expired.F.queue == 0 and expired.tx[#expired.tx].op == OP.FOLLOW_ACK and expired.tx[#expired.tx].v[3] == 0 and
    #expired.calls == 0 and not expired.holdCalls and logged(expired, 'step=refused;reason=timeout'),
    'the 120 s timeout still refuses a held transition with FOLLOW_ACK 0')
local idleHold = fixture(2)
idleHold.F.replayHold = function() idleHold.holdCalls = (idleHold.holdCalls or 0) + 1; return true end
idleHold.F.tick(idleHold.time)
check(not idleHold.holdCalls, 'no queued transition: the hold is never asked')
-- D11 pre-pair gap (0.14.4 review, finding game-d11-prepair-gap): a start that replays the host's transition into this
-- mission records it (F.arrivedMission, read by world_vehicle_rules.lua and world_relief_sync.lua), any other start clears it.
local arrival = fixture(2)
arrival.F.current = {41, 1, 30050, 30020}
arrival.W.mission = 30020
for _, f in ipairs(arrival.W.startHooks) do f() end
check(arrival.F.arrivedMission == 30020 and logged(arrival, 'step=arrived;id=41;mission=30020'),
    'the follow arrival marks the mission (F.arrivedMission)')
arrival.W.mission = 30010
for _, f in ipairs(arrival.W.startHooks) do f() end
check(arrival.F.arrivedMission == nil, 'a later start without a replayed transition (a solo load) clears it')
arrival.F.current = {42, 1, 30010, 30020}
for _, f in ipairs(arrival.W.startHooks) do f() end
check(arrival.F.arrivedMission == nil and logged(arrival, 'step=refused;reason=wrong_destination'), 'a wrong destination is no arrival')
-- fb1031 arrival (online 0.14.5: the guest stayed in its arrival helicopter at Mother Base, act 102, after the host's
-- sortie and after both of its own title Continues; its pause-menu Restart/Continue were refused host_only).
-- A linked co-op guest never starts Mother Base riding the helicopter (vanilla TppMission.ResetMBFreeStartPositionToCommand,
-- the on-foot Command start of the vanilla MB restart), its queued MB demo is cleared, and a guest riding a helicopter at
-- Mother Base restarts locally (then on foot) instead of the host_only refusal.
local function count(c, text)
    local n = 0
    for _, s in ipairs(c.logs) do if s:find(text, 1, true) then n = n + 1 end end
    return n
end
local function mbApis(c, demo)
    c.mbDemo = demo
    c.env.TppDefine.INITIAL_PLAYER_STATE = {ON_FOOT = 0, RIDEON_HELICOPTER = 1}
    c.env.TppDemo = {GetMBDemoName = function() return c.mbDemo end,
        SetNextMBDemo = function(name) c.mbDemo = name; c.calls[#c.calls + 1] = 'mb_demo' end}
    c.env.TppMission.ResetMBFreeStartPositionToCommand = function()
        c.calls[#c.calls + 1] = 'mb_foot'
        c.env.gvars.heli_missionStartRoute, c.env.gvars.ply_initialPlayerState, c.env.vars.mbClusterId = 0, 0, 0
    end
    c.env.Tpp = {IsHelicopter = function(id) return id == 77 end}
    return c
end
-- The host's sortie seeds into 30050: its start status (1 rides the helicopter in) and initial-action flag.
local function mbSeeds(g, id, state, used)
    local h = fixture(1)
    h.env.vars.missionCode, h.env.gvars.ply_initialPlayerState, h.env.svars.ply_isUsedPlayerInitialAction = 30050, state, used
    h.env.e.ReservePlayerLoadingPosition(1)
    h.env.TppMission.Load(30050)
    for _ = 1, 4 do h.F.tick(h.time) end
    local n, digest = 0, 0
    for _, p in ipairs(h.tx) do
        if p.op == OP.SEED then
            local v = {id, 30050, p.v[3], p.v[4], p.v[5]}
            send(g, OP.SEED, v)
            n, digest = n + 1, (digest * 31 + v[3] * 1024 + v[4] + v[5]) % 4294967296
        end
    end
    send(g, OP.SEEDS_END, {id, 30050, n, digest})
end
-- fb1031 review: the broad arrival rule (every linked guest helicopter arrival at 30050 reset to Command, the guest MB
-- demo cleared at seeds and allocation) is off by default (F.arrival.auto); these checks run it switched on.
local function mbAuto(c, demo) mbApis(c, demo); c.F.arrival.auto = true; return c end
do
    -- The kind-6 sortie replay into 30050 with the host's helicopter route (the online case).
    local mf = mbAuto(fixture(2), 'LongTimeNoSee_DdogSuperHighLikability')
    mf.W.mission, mf.env.vars.missionCode = 40050, 40050
    send(mf, OP.TRANSITION, {1, 6, 40050, 30050, 0, 4294967295, 0, 2, 0, 0, 8})
    mbSeeds(mf, 1, 1, false)
    mf.F.tick(mf.time)
    check(mf.F.current and called(mf, 'route') == 1 and called(mf, 'deploy') == 1, 'kind 6 replays the host sortie')
    -- Vanilla ExecuteMissionFinalize: vars.missionCode is the destination, the start status the helicopter.
    mf.env.vars.missionCode, mf.env.gvars.ply_initialPlayerState = 30050, 1
    mf.env.e.ReservePlayerLoadingPosition(1)
    check(called(mf, 'mb_foot') >= 1 and mf.env.gvars.heli_missionStartRoute == 0 and mf.env.gvars.ply_initialPlayerState == 0,
        'the guest arrival at Mother Base starts on foot (vanilla Command start)')
    check(mf.mbDemo == nil and count(mf, 'step=mb_demo;') == 1 and count(mf, 'cleared=LongTimeNoSee_DdogSuperHighLikability') == 1,
        'the guest queued MB demo is cleared once and logged')
    local before = called(mf, 'mb_foot')
    mf.env.TppMission.Load(30050)
    check(mf.loaded == 30050 and called(mf, 'mb_foot') == before + 1 and mf.env.gvars.heli_missionStartRoute == 0 and
        mf.env.gvars.ply_initialPlayerState == 0 and mf.env.vars.mbClusterId == 0,
        'the host seeds (its helicopter route) never put the guest back in the helicopter')
    check(count(mf, 'step=mb_foot;at=seeds;route=4294967295') >= 1, 'the on-foot start is logged with the replaced route')
    -- A vanilla reset that throws never stops world sync (the seed apply runs inside the load wrappers' pcall).
    local bad = mbAuto(fixture(2))
    bad.env.TppMission.ResetMBFreeStartPositionToCommand = function() error('reset_failed') end
    bad.W.mission, bad.env.vars.missionCode = 40050, 40050
    send(bad, OP.TRANSITION, {1, 6, 40050, 30050, 0, 4294967295, 0, 2, 0, 0, 8})
    mbSeeds(bad, 1, 1, false)
    bad.F.tick(bad.time)
    bad.env.TppMission.Load(30050)
    check(bad.loaded == 30050 and bad.failure == nil and bad.W.active, 'a failing reset leaves the load and world sync running')
    -- The host keeps its own arrival.
    local host = mbAuto(fixture(1), 'QuietReturnToMB')
    host.env.gvars.ply_initialPlayerState = 1
    host.env.e.ReservePlayerLoadingPosition(1)
    check(called(host, 'mb_foot') == 0 and host.mbDemo == 'QuietReturnToMB' and host.env.gvars.heli_missionStartRoute == 4294967295,
        'the host arrival and demo are untouched')
    -- Route 0 and on foot (the lab boot, a travel landing): nothing to change.
    local boot = mbAuto(fixture(2))
    boot.env.gvars.heli_missionStartRoute = 0
    boot.env.e.ReservePlayerLoadingPosition(1)
    check(called(boot, 'mb_foot') == 0 and count(boot, 'step=mb_foot') == 0, 'an on-foot start is left alone')
    -- Elsewhere, unlinked or older games: unchanged.
    local field = mbAuto(fixture(2), 'X')
    field.env.vars.missionCode, field.env.gvars.ply_initialPlayerState = 30010, 1
    field.env.e.ReservePlayerLoadingPosition(1)
    check(called(field, 'mb_foot') == 0 and field.mbDemo == 'X', 'free roam keeps its helicopter arrival')
    local alone = mbAuto(fixture(2), 'X')
    alone.link = 0
    alone.env.e.ReservePlayerLoadingPosition(4)
    check(called(alone, 'mb_foot') == 0 and alone.mbDemo == 'X', 'no link: a solo guest copy keeps vanilla')
    local oldGame = fixture(2)
    oldGame.env.e.ReservePlayerLoadingPosition(3)
    check(called(oldGame, 'mb_foot') == 0 and oldGame.failure == nil, 'no vanilla reset function: nothing happens')
    -- A restart / Continue of a guest save bound to Mother Base in the helicopter (its checkpoint taken in the
    -- helicopter: vanilla VarSaveOnUpdateCheckPoint leaves ply_isUsedPlayerInitialAction false).
    for _, kind in ipairs({3, 4}) do
        local cont = mbAuto(fixture(2), 'X')
        cont.env.gvars.ply_initialPlayerState, cont.env.svars.ply_isUsedPlayerInitialAction = 1, false
        cont.env.e.ReservePlayerLoadingPosition(kind)
        check(called(cont, 'mb_foot') == 1 and cont.mbDemo == nil and count(cont, 'step=mb_foot;at=reserve') == 1,
            'kind ' .. kind .. ': a guest save that would ride in starts on foot')
    end
    -- A Continue whose helicopter entry was used (a checkpoint on foot; the W4 shared Continue keeps the host spawn).
    local used = mbAuto(fixture(2), 'X')
    used.env.gvars.ply_initialPlayerState, used.env.svars.ply_isUsedPlayerInitialAction = 1, true
    used.env.e.ReservePlayerLoadingPosition(4)
    check(called(used, 'mb_foot') == 0 and used.env.vars.initialPlayerPosX == -123.125 and used.mbDemo == nil,
        'a checkpoint Continue on foot keeps its spawn (the demo is still cleared)')
    local restart = mbAuto(fixture(2), 'X')
    restart.env.gvars.ply_initialPlayerState, restart.env.svars.ply_isUsedPlayerInitialAction = 1, true
    restart.env.e.ReservePlayerLoadingPosition(3)
    check(called(restart, 'mb_foot') == 1, 'a restart replays the helicopter entry: on foot whatever the flag')
    local walked = mbAuto(fixture(2), 'X')
    walked.env.gvars.ply_initialPlayerState = 0
    walked.env.e.ReservePlayerLoadingPosition(1)
    check(called(walked, 'mb_foot') == 0, 'a start status on foot (route set) is left alone')
    -- The allocation (f30050 MissionPrepare reads the queued demo): cleared for a linked guest only.
    local alloc = mbAuto(fixture(2), 'X')
    alloc.env.gvars.f30050_missionPackIndex = 0
    local co = coroutine.create(function() return alloc.env.e.OnAllocate(1) end)
    local ok, yielded = coroutine.resume(co)
    check(ok and yielded == 'allocate' and alloc.mbDemo == nil and count(alloc, 'step=mb_demo;at=allocate') == 1,
        'allocation clears the guest MB demo and still yields through')
    local hostAlloc = mbAuto(fixture(1), 'X')
    coroutine.resume(coroutine.create(function() return hostAlloc.env.e.OnAllocate(1) end))
    check(hostAlloc.mbDemo == 'X', 'the host demo stays queued')
end
do
    -- The way out: a guest riding a helicopter at Mother Base restarts locally (the menu's Restart or Continue).
    local stuck = mbApis(fixture(2))
    stuck.env.vars.playerVehicleGameObjectId = 77
    stuck.env.TppMission.RestartMission()
    check(called(stuck, 'restart_menu') == 1 and #stuck.tx == 0 and count(stuck, 'step=local_restart;fn=RestartMission;why=heli') == 1,
        'stuck guest: Restart runs locally, no host_only request')
    stuck.time = stuck.time + 1500
    stuck.env.TppMission.ExecuteRestartMission()
    check(called(stuck, 'restart') == 1 and #stuck.tx == 0, 'the fade callback restart passes too')
    local cont = mbApis(fixture(2))
    cont.env.vars.playerVehicleGameObjectId = 77
    cont.env.TppMission.ContinueFromCheckPoint()
    check(called(cont, 'restart_menu') == 1 and called(cont, 'continue_menu') == 0 and #cont.tx == 0 and
        count(cont, 'step=local_restart;fn=ContinueFromCheckPoint;why=heli') == 1, 'stuck guest: Continue runs the local restart')
    local foot = mbApis(fixture(2))
    foot.env.TppMission.RestartMission()
    check(called(foot, 'restart_menu') == 0 and foot.tx[1].op == OP.REQ_MISSION, 'a guest on foot still asks the host')
    local late = mbApis(fixture(2))
    late.env.TppMission.ExecuteRestartMission()
    check(called(late, 'restart') == 0 and late.tx[1].op == OP.REQ_MISSION, 'no pending local restart: Execute still asks the host')
    local africa = mbApis(fixture(2))
    africa.W.mission, africa.env.vars.missionCode, africa.env.vars.playerVehicleGameObjectId = 30020, 30020, 77
    africa.env.TppMission.RestartMission()
    check(called(africa, 'restart_menu') == 0 and africa.tx[1].op == OP.REQ_MISSION, 'free roam keeps the host_only rule')
    local over = mbApis(fixture(2))
    over.env.vars.playerVehicleGameObjectId = 77
    over.env.svars.mis_isDefiniteGameOver = true
    over.env.TppMission.RestartMission()
    check(called(over, 'restart_menu') == 0 and over.tx[1].op == OP.REQ_MISSION, 'a game over keeps the host_only rule')
end
do
    -- fb1031 review (default): a linked guest's helicopter arrival at Mother Base stays vanilla (the 0.14.5 arrival
    -- that works: riding in with the host's route and cluster); its queued MB demo stays queued.
    local mf = mbApis(fixture(2), 'X')
    mf.W.mission, mf.env.vars.missionCode = 40050, 40050
    send(mf, OP.TRANSITION, {1, 6, 40050, 30050, 0, 4294967295, 0, 2, 0, 0, 8})
    mbSeeds(mf, 1, 1, false)
    mf.F.tick(mf.time)
    mf.env.vars.missionCode, mf.env.gvars.ply_initialPlayerState = 30050, 1
    mf.env.e.ReservePlayerLoadingPosition(1)
    mf.env.TppMission.Load(30050)
    check(mf.F.arrival.auto ~= true and mf.loaded == 30050 and called(mf, 'mb_foot') == 0 and count(mf, 'step=mb_foot') == 0 and
        mf.env.gvars.ply_initialPlayerState == 1 and mf.env.gvars.heli_missionStartRoute == 4294967295 and mf.mbDemo == 'X',
        'default: the guest rides in with the host route, no Command reset, its MB demo kept')
    for _, kind in ipairs({1, 3, 4}) do
        local r = mbApis(fixture(2), 'X')
        r.env.gvars.ply_initialPlayerState, r.env.svars.ply_isUsedPlayerInitialAction = 1, false
        r.env.e.ReservePlayerLoadingPosition(kind)
        check(called(r, 'mb_foot') == 0 and r.mbDemo == 'X', 'default: kind ' .. kind .. ' Reserve without a stuck-guest restart is vanilla')
    end
    local alloc = mbApis(fixture(2), 'X')
    alloc.env.gvars.f30050_missionPackIndex = 0
    coroutine.resume(coroutine.create(function() return alloc.env.e.OnAllocate(1) end))
    check(alloc.mbDemo == 'X' and count(alloc, 'step=mb_demo') == 0, 'default: allocation leaves the guest MB demo queued')
    -- The way out lands on foot: the stuck guest's local restart Reserve resets to Command (the menu restart passes no
    -- return-to-mission flag, so vanilla ExecuteRestartMission does not), and clears its queued demo before the pack list.
    for _, menu in ipairs({'RestartMission', 'ContinueFromCheckPoint'}) do
        local out = mbApis(fixture(2), 'X')
        out.env.vars.playerVehicleGameObjectId, out.env.gvars.ply_initialPlayerState = 77, 1
        out.env.TppMission[menu]()
        out.time = out.time + 1500
        out.env.TppMission.ExecuteRestartMission()
        out.env.e.ReservePlayerLoadingPosition(3)
        check(called(out, 'mb_foot') == 1 and out.env.gvars.ply_initialPlayerState == 0 and out.env.gvars.heli_missionStartRoute == 0 and
            out.mbDemo == nil and count(out, 'step=mb_foot;at=restart') == 1, 'way out (' .. menu .. '): the local restart starts on foot at Command')
    end
    local stale = mbApis(fixture(2), 'X')
    stale.env.vars.playerVehicleGameObjectId, stale.env.gvars.ply_initialPlayerState = 77, 1
    stale.env.TppMission.RestartMission()
    stale.time = stale.time + 31000
    stale.env.e.ReservePlayerLoadingPosition(3)
    check(called(stale, 'mb_foot') == 0 and stale.mbDemo == 'X', 'a restart Reserve 31 s after the way out is vanilla')
    local away = mbApis(fixture(2), 'X')
    away.env.vars.playerVehicleGameObjectId, away.env.gvars.ply_initialPlayerState = 77, 1
    away.env.TppMission.RestartMission()
    away.env.e.ReservePlayerLoadingPosition(1)
    check(called(away, 'mb_foot') == 0, 'only the restart Reserve takes the way-out reset')
    -- Switched on, allocation never clears a demo whose packs the vanilla 30050 pack list already chose
    -- (f30051 demo stage = 1, battle hangar = 2): the packs and the demo state must agree.
    for _, index in ipairs({1, 2}) do
        local stage = mbAuto(fixture(2), 'X')
        stage.env.gvars.f30050_missionPackIndex = index
        coroutine.resume(coroutine.create(function() return stage.env.e.OnAllocate(1) end))
        check(stage.mbDemo == 'X' and count(stage, 'step=mb_demo') == 0, 'auto: pack index ' .. index .. ' keeps its demo at allocation')
    end
end
print('world_follow_lua_check: ' .. checks .. ' checks passed')
