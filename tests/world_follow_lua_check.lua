-- W3 flow tests with recording Lua APIs. No game or real transport.
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(tostring(checks) .. ': ' .. why, 2) end
end
local OP = {TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13, REJECT = 4}
local function fixture(role, level)
    local c = {calls = {}, tx = {}, logs = {}, time = 1000, inGame = true}
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
    env.svars = {mis_missionClearType = 6}
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
        OnEndMissionPreparation = function(t, cluster) c.deploy = t; c.cluster = cluster; record('deploy') end,
        Load = function(m) record('load'); c.loaded = m; c.seedAtLoad = env.gvars.solface_groupNumber end,
        LoadWithChunkCheck = function() record('load_check'); env.TppMission.Load(env.mvars.mis_loadRequest.nextMission); env.mvars.mis_loadRequest = nil end,
        ReserveMissionClearOnRideOnHelicopter = function() record('heli') end,
        AbortForOutOfMissionArea = function() record('area') end,
        ExecuteRestartMission = function(...) c.restartArgs = select('#', ...); record('restart') end,
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
g.env.mvars.mis_loadRequest = {nextMission = 40050}
g.env.TppMission.LoadWithChunkCheck()
check(g.loaded == 40050 and g.seedAtLoad == 111 and g.env.mvars.mis_loadRequest == nil, 'host seeds applied before original Load and request cleanup')
check(g.env.gvars.isContinueFromTitle == false, 'repeated seed preparation preserves vanilla title-continue cleanup')
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
for missing = 19, 27 do
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
print('world_follow_lua_check: ' .. checks .. ' checks passed')
