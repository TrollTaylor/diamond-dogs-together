-- Exercises lua/coop/world_*.lua (world sync W0-W2, docs/world-sync-protocol.md)
-- with a simulated native side and recording game-binding fakes. No MGSV, no
-- network, no real session.
-- Usage: lua51 world_sync_lua_check.lua <world_sync.lua> <world_enemy.lua> <world_outcome.lua>
--        <world_phase.lua> <world_env.lua> <world_probe.lua> <world_goto.lua> [composed TppMain-host-trial.lua]
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(tostring(checks) .. ': ' .. (why or 'check failed'), 2) end
end
local FILES = {arg[1], arg[2], arg[3], arg[4], arg[5], arg[6], arg[7]}
for i = 1, 7 do assert(FILES[i], 'usage: seven world chunks') end
local PRE = "return 'MGS_COOP_WORLD_"
local OP = {HELLO = 1, READY = 2, RECEIVED = 3, REJECT = 4, RESYNC = 5, RESYNC_END = 6, MISSION_END = 7, PROBE_SYNC = 8,
    OUTCOME = 16, REQ_OUTCOME = 17, PHASE = 18, REQ_PHASE = 19, AREA = 20, DIGEST = 21, GDIGEST = 22, PVEC = 23,
    CLOCK = 32, WEATHER = 33, LINK = 112, PAIRED = 113, UNPAIRED = 114, GOTO = 115, PROBE = 116, PROBE_FIRE = 117,
    RESYNC_NEEDED = 118, LEVEL = 119, GOTO_ACK = 120}
local LIFE = {DEAD = 3, DYING = 1, SLEEP = 4, FAINT = 2}  -- a permutation inside 1..4 (never the wire values)
local PHASE = {SNEAK = 10, CAUTION = 11, EVASION = 12, ALERT = 13}
local NULL_ID = 65535
local RESEND = {}
local sentinel = {}
local function starts(s, p) return s:sub(1, #p) == p end
local function has(list, pattern)
    for _, v in ipairs(list) do if v:find(pattern, 1, true) then return v end end
    return nil
end
local function count(list, pattern)
    local n = 0
    for _, v in ipairs(list) do if v:find(pattern, 1, true) then n = n + 1 end end
    return n
end
-- Deterministic StrCode32 stand-in: values above 2^31, and some negative
-- (signed) results like the engine binding can return.
local function str32(s)
    local h = 2166136261
    for i = 1, #s do h = ((h * 31) + s:byte(i) * 7919 + i) % 4294967296 end
    h = (h * 2654435761) % 4294967296
    if h >= 2147483648 and h % 5 == 0 then return h - 4294967296 end
    return h
end
local function u32(s) local v = str32(s) if v < 0 then v = v + 4294967296 end return v end
local function vec(x, y, z) return {GetX = function() return x end, GetY = function() return y end, GetZ = function() return z end} end

-- options: role (1 host / 2 guest), level, mode, mission, literal (CFG stays literal),
-- cpList (shape), defs (soldierDefine), errorAt / yieldAt (original callback), consts
local function fixture(options)
    options = options or {}
    local c = {receipts = {}, tx = {}, logs = {}, rx = {}, calls = {}, time = 100, life = {}, recovered = {}, phase = {},
        pos = {}, inGame = true, missionStart = true, demo = false, order = {}, txReply = nil, seq = 'Seq_Game_MainGame',
        flags = 3, original = {}, peer = options.peer}
    local mission = options.mission or 30010
    local defs = options.defs or {cp_a = {'sol_a1', 'sol_a2', 'sol_a3', lrrpVehicle = 'veh_x', lrrpTravelPlan = 'travel_x'},
        cp_b = {'sol_b1', 'sol_b2', 'sol_b3'}, quest_cp = {'sol_q1', 'sol_q2'}}
    local ids, names, nextId = {}, {}, 100
    local function idOf(name)
        if not ids[name] then ids[name], names[nextId] = nextId, name; nextId = nextId + 1 end
        return ids[name]
    end
    for cp, list in pairs(defs) do
        idOf(cp)
        for _, s in ipairs(list) do
            local id = idOf(s)
            c.life[id] = 0
            c.pos[id] = vec(1, 0, 1)
        end
        c.phase[cp] = PHASE.SNEAK
    end
    c.ids, c.names = ids, names
    local cpList = options.cpList or {[1] = {cp_a = true}, [2] = {cp_b = true}}
    local function record(text) c.calls[#c.calls + 1] = text end
    local env = {}
    env.e = {}
    for _, name in ipairs({'OnInitialize', 'OnMissionGameStart', 'OnUpdate', 'OnTerminate'}) do
        env.e[name] = function(...)
            check(select('#', ...) == 3, 'original argument count ' .. name)
            check(select(2, ...) == nil and select(3, ...) == 9, 'original arguments ' .. name)
            c.order[#c.order + 1] = name
            if options.errorAt == name then error(sentinel) end
            if options.yieldAt == name then coroutine.yield('original yield') end
            return 1, nil, 3
        end
    end
    env.e.OnMessage = function(s, class, msg, a0, a1, a2, a3, ...)
        c.order[#c.order + 1] = 'OnMessage'
        c.lastMessage = {s, class, msg, a0, a1, a2, a3, select('#', ...)}
        if options.errorAt == 'OnMessage' then error(sentinel) end
        if c.messageReply ~= nil then return c.messageReply end
        return nil
    end
    -- Native side.
    env.loadstring = function(source)
        check(type(source) == 'string' and #source < 256, 'receipt under 256 bytes: ' .. tostring(source))
        c.receipts[#c.receipts + 1] = source
        if not starts(source, PRE) then return loadstring(source) end
        if options.literal then return loadstring(source) end
        local body = source:match("^return 'MGS_COOP_WORLD_%u+_V1' %-%- (.*)$")
        if starts(source, PRE .. "CFG_V1'") then
            local m, phase = body:match('^(%d+) (%d)$')
            check(m ~= nil, 'CFG args ' .. source)
            if phase == '1' then c.cfgStarts = (c.cfgStarts or 0) + 1 else c.cfgQueries = (c.cfgQueries or 0) + 1 end
            return function()
                local p = c.peer
                if p then
                    -- A join-fix loader: session, then remote_mission, remote_level and the raw pair-wait code.
                    return options.level or 3, options.role or 1, 1, p.link or 1, 5, phase == '1' and (options.probe or 0) or 0,
                        options.mode or 1, p.session or 7, p.mission, p.level, p.wait
                end
                return options.level or 3, options.role or 1, 1, 1, 5, phase == '1' and (options.probe or 0) or 0, options.mode or 1
            end
        elseif starts(source, PRE .. "NOTE_V1'") then
            check(body:match('^%d+ %d+ %d+$'), 'NOTE integers only: ' .. body)
            for n in body:gmatch('%S+') do check(n == '0' or not n:find('^0'), 'NOTE no leading zeros: ' .. n) end
            c.notes = c.notes or {}
            c.notes[#c.notes + 1] = body
            return function() return true end
        elseif starts(source, PRE .. "TX_V1'") then
            check(body:match('^[%-%d][%-%d ]*$') and not body:find('  ') and not body:find('e') and not body:find('%.'),
                'TX integers only: ' .. body)
            local v = {}
            for n in body:gmatch('%S+') do
                check(n == '0' or (not n:find('^%-?0') and n ~= '-0'), 'no leading zeros / -0: ' .. n)
                v[#v + 1] = tonumber(n)
            end
            local op = table.remove(v, 1)
            c.tx[#c.tx + 1] = {op = op, v = v, text = body, at = c.time}
            return function()
                if c.txReply then return unpack(c.txReply) end
                return 1
            end
        elseif starts(source, PRE .. "RX_V1'") then
            c.rxMasks = c.rxMasks or {}
            c.rxMasks[#c.rxMasks + 1] = tonumber(body)
            return function()
                local item = table.remove(c.rx, 1)
                if item and (item.queue == 0 or body == '3') then return item.queue, item.op, unpack(item.v) end
                if item then table.insert(c.rx, 1, item) end
                return 0, 0, c.flags, 0, 0, 5
            end
        elseif starts(source, PRE .. "LOG_V1'") then
            c.logs[#c.logs + 1] = body
            return function() return true end
        end
        error('unexpected receipt ' .. source)
    end
    env.vars = {missionCode = mission, locationCode = 10, clock = 43200, weather = 0, playerPosX = 0, playerPosY = 0, playerPosZ = 0}
    env.mvars = {ene_soldierDefine = defs}
    env.svars = {mis_isDefiniteGameOver = false, mis_isDefiniteMissionClear = false}
    env.gvars = {mis_nextMissionCodeForEmergency = 0, mis_nextMissionCodeForMissionClear = 0, heli_missionStartRoute = 0}
    env.Fox = {StrCode32 = str32}
    env.Time = {GetRawElapsedTimeSinceStartUp = function() return c.time / 1000 end}
    env.Mission = {ON_MESSAGE_RESULT_RESEND = RESEND}
    env.GameObject = {NULL_ID = NULL_ID,
        GetGameObjectId = function(a, b) return ids[b or a] or NULL_ID end,
        GetTypeIndex = function(id) return id == 7 and 22 or 1 end,
        SendCommand = function(id, cmd)
            local k = cmd.id
            if k == 'GetPosition' then return c.pos[id] end
            if k == 'GetLifeStatus' then return c.life[id] end
            if k == 'GetPhase' then return c.phase[cmd.cpName] end
            record(k .. ':' .. tostring(names[id] or id) .. ':' .. tostring(cmd.state or cmd.phase or ''))
            if k == 'ChangeLifeState' and not c.inert then c.life[id] = cmd.state end
            if k == 'RequestForceFulton' and not c.inert then c.fultonAsked = (c.fultonAsked or 0) + 1 end
            if k == 'SetPhase' and not c.inert then c.phase[names[id]] = cmd.phase end
            if k == 'RecoveryLife' then c.life[id] = 0 end
        end}
    env.TppGameObject = {NPC_LIFE_STATE_DEAD = LIFE.DEAD, NPC_LIFE_STATE_DYING = LIFE.DYING, NPC_LIFE_STATE_SLEEP = LIFE.SLEEP,
        NPC_LIFE_STATE_FAINT = LIFE.FAINT, PHASE_SNEAK = PHASE.SNEAK, PHASE_CAUTION = PHASE.CAUTION, PHASE_EVASION = PHASE.EVASION,
        PHASE_ALERT = PHASE.ALERT, GAME_OBJECT_TYPE_BUDDYDOG2 = 22, GAME_OBJECT_TYPE_BUDDYQUIET2 = 23}
    for k, v in pairs(options.consts or {}) do env.TppGameObject[k] = v end
    env.TppEnemy = {
        GetLifeStatus = function(id) if c.unreadable and c.unreadable[id] then return nil end return env.GameObject.SendCommand(id, {id = 'GetLifeStatus'}) end,
        IsRecovered = function(name) return c.recovered[name] end,
        GetPhase = function(cp) return c.phase[cp] end,
        SetOutOfArea = function(list, out) record('SetOutOfArea:' .. tostring(out)) end,
        GetStatus = function() return 0 end}
    env.Tpp = {IsLocalPlayer = function(id) return id == 0 end}
    env.TppMission = {
        CheckMissionState = function() return c.inGame end,
        IsMissionStart = function() return c.missionStart end,
        IsHelicopterSpace = function(m) return math.floor(m / 10000) == 4 end,
        ReserveMissionClearOnRideOnHelicopter = function()
            record('RideOnHelicopter')
            env.svars.mis_isDefiniteMissionClear = true
        end,
        ReserveMissionClear = function(t)
            record('ReserveMissionClear:' .. tostring(t.missionClearType) .. ':' .. tostring(t.nextMissionId))
            env.svars.mis_isDefiniteMissionClear = true
        end,
        SelectNextMissionHeliStartRoute = function(m, route, emergency)
            record('SelectNextMissionHeliStartRoute:' .. m .. ':' .. string.format('%.0f', route) .. ':' .. tostring(emergency))
            env.gvars.mis_nextMissionCodeForMissionClear, env.gvars.heli_missionStartRoute = m, route
        end,
        OnEndMissionPreparation = function(t, cl)
            record('OnEndMissionPreparation:' .. tostring(t) .. ':' .. tostring(cl))
            env.svars.mis_isDefiniteMissionClear = true
        end,
        GetSyncMissionStatus = function() return true, true, false, true end}
    env.TppSequence = {GetCurrentSequenceName = function() return c.seq end}
    env.TppDemo = {SetNextMBDemo = function(...) record('SetNextMBDemo:' .. select('#', ...)) end}
    env.TppDefine = {WEATHER = {SUNNY = 0, CLOUDY = 1, RAINY = 2, SANDSTORM = 3, FOGGY = 4, POURING = 5},
        MISSION_CLEAR_TYPE = {RIDE_ON_HELICOPTER = 6}, SYS_MISSION_ID = {MTBS_HELI = 40050}}
    env.TppClock = {DEPLOY_TIME = {CURRENT = 0}, AddTime = function(n) env.vars.clock = env.vars.clock + n end}
    env.TppWeather = {ForceRequestWeather = function(t, i) record('Force:' .. t .. ':' .. tostring(i)); env.vars.weather = t end,
        CancelForceRequestWeather = function() record('CancelForce') end}
    env.WeatherManager = {PauseNewWeatherChangeRandom = function(b) record('Pause:' .. tostring(b)) end}
    env.DemoDaemon = {IsDemoPlaying = function() return c.demo end}
    env.TppNetworkUtil = {IsHost = function() return true end}
    if not options.noF30050 then
        env.f30050_sequence = {ReserveMissionClear = function()
            record('f30050.ReserveMissionClear')
            env.TppDemo.SetNextMBDemo()
            env.TppMission.ReserveMissionClear{missionClearType = 6, nextMissionId = 40050}
        end}
    end
    local enemy = {cpList = cpList, soldierDefine = defs}
    enemy.UpdateOutOfAreaSetting = function(area, extra)
        c.areaCalls = (c.areaCalls or 0) + 1
        c.areaArg = {area, extra}
        if c.areaError then error('area failure') end
        local current = enemy.cpList[area]
        c.areaSeen = {}
        for cp in pairs(enemy.soldierDefine) do
            local inside = cp == 'quest_cp'
            if type(current) == 'table' then
                inside = inside or current[cp] == true
                for _, n in ipairs(current) do if n == cp then inside = true end end
            end
            c.areaSeen[cp] = inside
        end
        return 'area', area
    end
    c.enemy = enemy
    env['f' .. mission .. '_enemy'] = enemy
    for k, v in pairs(options.env or {}) do env[k] = v end
    for _, path in ipairs(options.missing or {}) do
        local a, b = path:match('^(%w+)%.?(%w*)$')
        if b == '' then env[a] = nil else env[a][b] = nil end
    end
    setmetatable(env, {__index = _G})
    for i = 1, 7 do
        local chunk = assert(loadfile(FILES[i]))
        setfenv(chunk, env)
        chunk()
    end
    c.W = env.e.MgsCoopWorld
    return env, c
end

local F = {}
function F.call(env, name, ...)
    local function returns(...)
        check(select('#', ...) == 3 and select(1, ...) == 1 and select(2, ...) == nil and select(3, ...) == 3, 'original returns ' .. name)
    end
    returns(env.e[name](7, nil, 9))
end
function F.init(env, c) F.call(env, 'OnInitialize') end
function F.start(env, c, prepare)
    -- OnInitialize gets subScripts as its first argument.
    local ok = env.e.OnInitialize({enemy = c.enemy}, nil, 9)
    check(ok == 1, 'OnInitialize returns')
    if prepare then prepare() end
    F.call(env, 'OnMissionGameStart')
end
function F.tick(env, c, n, dt)
    for _ = 1, n or 1 do
        c.time = c.time + (dt or 33)
        F.call(env, 'OnUpdate')
    end
end
function F.push(c, queue, op, ...) c.rx[#c.rx + 1] = {queue = queue, op = op, v = {...}} end
function F.msg(env, class, msg, ...)
    return env.e.OnMessage('self', str32(class), str32(msg), ...)
end
function F.tx(c, op)
    local out = {}
    for _, t in ipairs(c.tx) do if t.op == op then out[#out + 1] = t end end
    return out
end
function F.clear(c) c.tx, c.logs, c.calls = {}, {}, {} end
function F.pair(env, c, eff, match)
    F.push(c, 0, OP.PAIRED, 5, c.W.mission or 30010, 77, 8, match or 1, eff or 3)
    F.tick(env, c, 1)
end
function F.h(name) return u32(name) end
function F.callCount(c, text) local n = 0 for _, v in ipairs(c.calls) do if v == text then n = n + 1 end end return n end

-- 1. A literal CFG: two CFG loads per mission, nothing installed, no tick receipts.
do
    local env, c = fixture({literal = true})
    local original = c.enemy.UpdateOutOfAreaSetting
    F.start(env, c)
    F.tick(env, c, 5)
    F.msg(env, 'GameObject', 'Dead', 100, 0, 1)
    F.call(env, 'OnTerminate')
    local world = 0
    for _, r in ipairs(c.receipts) do if starts(r, PRE) then world = world + 1 end end
    check(world == 2 and starts(c.receipts[1], PRE .. "CFG_V1' -- 30010 0") and starts(c.receipts[2], PRE .. "CFG_V1' -- 30010 1"),
        'literal CFG: exactly the query and the start receipt')
    check(c.enemy.UpdateOutOfAreaSetting == original and not c.W.active and #c.calls == 0, 'literal CFG: nothing wrapped or called')
    check(c.order[#c.order] == 'OnTerminate', 'originals ran')
end

-- 2. Receipts, number formatting, bad values.
do
    local env, c = fixture({role = 1})
    F.start(env, c)
    local W = c.W
    check(W.active and W.isStarted and W.w1, 'active with W1 on')
    check(has(c.logs, 'event=started;level=3;role=1;mission=30010;soldiers=8;cps=3;roster='), 'started line: lrrpVehicle skipped')
    local hello = F.tx(c, OP.HELLO)[1]
    check(hello and hello.v[1] == 30010 and hello.v[2] == 10 and hello.v[4] == 8 and hello.v[5] == 3, 'HELLO fields')
    check(F.tx(c, OP.READY)[1] and F.tx(c, OP.READY)[1].v[2] == hello.v[3], 'READY carries the roster digest')
    check(W.n(-0) == '0' and W.n(4294967295) == '4294967295' and W.n(-2147483648) == '-2147483648' and W.n(0.5) == nil and
        W.n(4294967296) == nil and W.n(0 / 0) == nil and W.n(1 / 0) == nil, 'W.n')
    local big = 0
    for _, h in ipairs(W.entOrder) do if h > 2147483648 then big = big + 1 end end
    check(big > 0, 'some hashes above 2^31')
    check(not hello.text:find('e+', 1, true), 'no exponent formatting')
    F.pair(env, c, 3)
    env.vars.clock = 90000
    F.clear(c)
    F.tick(env, c, 40, 33)
    local d = F.tx(c, OP.DIGEST)[1]
    check(d and d.v[2] == 3600, 'clock 90000 is sent as 3600')
    -- A NaN position: the op is not sent, bad_value logged, world sync stays active.
    c.pos[c.ids.sol_a1] = vec(0 / 0, 0, 0)
    F.clear(c)
    c.life[c.ids.sol_a1] = LIFE.DEAD
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a1, 0, 5)
    F.tick(env, c, 1)
    check(has(c.logs, 'event=commit;ent=' .. string.format('%.0f', u32('sol_a1')) .. ';state=5;ver=1;actor=host'), 'committed')
    check(#F.tx(c, OP.OUTCOME) == 0 and has(c.logs, 'event=bad_value;op=16;field=9') and W.active, 'NaN position: not sent, still active')
end

-- 2b. NOTE receipts and the partner view (CFG r[9..11]; docs/join-fix-phase1-spec.md K1/K16).
do
    local env, c = fixture({role = 1})
    F.start(env, c)
    local W = c.W
    local before = #c.receipts
    check(W.note(2, 30010, 0) == true and c.receipts[#c.receipts] == "return 'MGS_COOP_WORLD_NOTE_V1' -- 2 30010 0" and
        #c.receipts == before + 1, 'W.note sends exactly one NOTE receipt and returns true')
    check(W.note(3, 4294967295, 4) == true and c.notes[#c.notes] == '3 4294967295 4', 'W.note formats u32 without exponent')
    before = #c.receipts
    check(W.note(2, -1, 0) == false and W.note(2, 1.5, 0) == false and W.note(-1, 1, 1) == false and W.note(2, nil, 0) == false and
        W.note(2, 0 / 0, 0) == false and W.note(2, 1, 4294967296) == false and #c.receipts == before,
        'W.note sends nothing for a negative, fractional, missing or oversized value')
    check(W.peer() == nil, 'W.peer is nil with the 7-value CFG of an older loader')
    local env2, c2 = fixture({role = 1, peer = {mission = 30010, level = 3, wait = 9}})
    F.start(env2, c2)
    local p = c2.W.peer()
    check(p and p.mission == 30010 and p.level == 3 and p.wait == 9 and p.session == 7 and p.linked == true,
        'W.peer reads remote mission, level, raw wait and the session')
    c2.peer = {mission = 0, level = 3, wait = 3}
    p = c2.W.peer()
    check(p and p.mission == 0 and p.wait == 3 and p.linked == false, 'no HELLO yet: not linked')
    for _, wait in ipairs({0, 7, 8, 10}) do
        c2.peer = {mission = 30010, level = 3, wait = wait}
        check(c2.W.peer().linked == true, 'linked for raw wait ' .. wait)
    end
    c2.peer = {mission = 30010, level = 3, wait = 9, link = 0, session = 0}
    p = c2.W.peer()
    check(p.session == nil and p.linked == false, 'link down: no session, not linked')
    local env3, c3 = fixture({role = 1, literal = true})
    F.start(env3, c3)
    check(c3.W.peer() == nil and c3.W.note(1, 40010, 0) == false, 'literal receipts: no peer view, note not handled')
end

-- 3. The observer: original first, arguments and returns passed through, RESEND skipped.
do
    local env, c = fixture({role = 2})
    F.start(env, c)
    F.clear(c)
    local a, b, n = F.msg(env, 'GameObject', 'Dead', c.ids.sol_b1, 0, 9, 4, 'x', 'y')
    check(c.lastMessage[1] == 'self' and c.lastMessage[4] == c.ids.sol_b1 and c.lastMessage[7] == 4 and c.lastMessage[8] == 2,
        'original got every argument')
    check(a == nil and b == nil, 'original returns passed through')
    check(count(c.logs, 'event=obs;m=Dead;ent=' .. string.format('%.0f', u32('sol_b1'))) == 1, 'one obs line per Dead')
    c.messageReply = RESEND
    local r = F.msg(env, 'GameObject', 'Dead', c.ids.sol_b1, 0, 9)
    check(r == RESEND and count(c.logs, 'event=obs;m=Dead') == 1 and c.W.stats.resend == 1, 'RESEND: not observed, returned')
    c.messageReply = nil
    env.e.OnMessage('self', u32('GameObject'), u32('Dead'), c.ids.sol_b1, 0, 9)
    if str32('GameObject') ~= u32('GameObject') or str32('Dead') ~= u32('Dead') then
        check(count(c.logs, 'event=obs;m=Dead') == 1, 'raw StrCode32 values are compared, never the normalised hash')
    end
    F.msg(env, 'GameObject', 'Damage', c.ids.sol_b1, 1, 0)
    check(not has(c.logs, 'm=Damage'), 'Damage is recorded, not logged')
end

-- 4. Roster: ipairs only, sorted, stable.
do
    local _, c1 = fixture()
    local env2, c2 = fixture()
    local env1 = nil
    env1, c1 = fixture()
    F.start(env1, c1)
    F.start(env2, c2)
    check(c1.W.roster == c2.W.roster and #c1.W.entOrder == 8 and #c1.W.cpOrder == 3, 'digest stable, 8 soldiers, 3 CPs')
    local d = 0
    local cps = {'cp_a', 'cp_b', 'quest_cp'}
    local sol = {cp_a = {'sol_a1', 'sol_a2', 'sol_a3'}, cp_b = {'sol_b1', 'sol_b2', 'sol_b3'}, quest_cp = {'sol_q1', 'sol_q2'}}
    for _, cp in ipairs(cps) do
        d = (d * 31 + u32(cp)) % 4294967296
        for _, s in ipairs(sol[cp]) do d = (d * 31 + u32(s)) % 4294967296 end
    end
    check(c1.W.roster == d, 'digest = (d*31 + hash) over sorted CPs and their soldiers')
    check(c1.W.ent[u32('veh_x')] == nil and c1.W.ent[u32('travel_x')] == nil, 'string keys are not soldiers')
end


local function hs(name) return string.format('%.0f', u32(name)) end

-- 5. Host: commits from its own world, the echo, backward only after reached, UNKNOWN never.
do
    local env, c = fixture({role = 1})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.clear(c)
    c.life[c.ids.sol_a1] = LIFE.DEAD
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a1, 0, 5)
    F.tick(env, c, 1)
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a1') .. ';state=5;ver=1;actor=host;rid=0;cause=1'), 'host commit ver 1')
    local out = F.tx(c, OP.OUTCOME)
    check(#out == 1 and out[1].v[1] == u32('sol_a1') and out[1].v[3] == 5 and out[1].v[4] == 1 and out[1].v[6] == 0 and
        out[1].v[9] == 1000 and out[1].v[11] == 1000, 'ENEMY_OUTCOME with the position in mm')
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a1, 0, 5)
    F.tick(env, c, 3)
    check(count(c.logs, 'event=commit;ent=' .. hs('sol_a1')) == 1, 'a repeated message never commits twice')
    -- A guest request committed and applied to the host world.
    F.clear(c)
    F.push(c, 1, OP.REQ_OUTCOME, 21, u32('sol_a2'), 1, 2, 0, 3, 0, 0, 1, 2, 3)
    F.tick(env, c, 1)
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a2') .. ';state=2;ver=1;actor=guest;rid=21;cause=6'), 'guest request committed')
    out = F.tx(c, OP.OUTCOME)
    check(#out == 1 and out[1].v[7] == 21 and out[1].v[6] == 1, 'the outcome carries the rid and actor guest')
    check(F.callCount(c, 'ChangeLifeState:sol_a2:' .. LIFE.SLEEP) == 1, 'applied with the SLEEP constant')
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_a2)
    F.tick(env, c, 10, 33)
    check(has(c.logs, 'event=apply;ent=' .. hs('sol_a2') .. ';state=2;ver=1;src=commit;result=ok;readback=2'), 'host apply ok')
    check(count(c.logs, 'event=commit;ent=' .. hs('sol_a2')) == 1, 'the applied echo does not commit again')
    -- Backward: a local SLEEP that wakes up (reached) commits NORMAL with flags 2.
    F.clear(c)
    c.life[c.ids.sol_a3] = LIFE.SLEEP
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_a3)
    F.tick(env, c, 1)
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a3') .. ';state=2;ver=1;actor=host'), 'local sleep committed')
    c.life[c.ids.sol_a3] = 0
    F.tick(env, c, 10)
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a3') .. ';state=0;ver=2;actor=host;rid=0;cause=9'), 'swept wake-up committed')
    local back = nil
    for _, t in ipairs(F.tx(c, OP.OUTCOME)) do if t.v[1] == u32('sol_a3') and t.v[3] == 0 then back = t end end
    check(back and back.v[8] == 2, 'backward flags 2')
    -- A failed apply never reaches: no false wake-up.
    F.clear(c)
    c.inert = true
    F.push(c, 1, OP.REQ_OUTCOME, 22, u32('sol_q1'), 1, 2, 0, 3, 0, 0, 1, 2, 3)
    F.tick(env, c, 100, 33)
    c.inert = false
    check(has(c.logs, 'event=apply;ent=' .. hs('sol_q1') .. ';state=2;ver=1;src=commit;result=fail') and
        has(c.logs, 'event=ledger;ent=' .. hs('sol_q1') .. ';op=hold;reason=apply_failed'), 'failed apply held')
    check(F.callCount(c, 'ChangeLifeState:sol_q1:' .. LIFE.SLEEP) == 2, 'one retry at +1 s')
    check(not has(c.logs, 'event=commit;ent=' .. hs('sol_q1') .. ';state=0'), 'no false wake-up from a failed apply')
    -- UNKNOWN never commits.
    c.unreadable = {[c.ids.sol_b1] = true}
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_b1, 0, 5)
    F.tick(env, c, 40)
    check(not has(c.logs, 'event=commit;ent=' .. hs('sol_b1')), 'an unreadable soldier never commits')
end

-- 5b. A guest commit is never swept backward before a readback reached it.
do
    local env, c = fixture({role = 1})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    c.life[c.ids.sol_a3] = LIFE.SLEEP
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_a3)
    F.tick(env, c, 1)
    c.life[c.ids.sol_a3] = 0
    F.tick(env, c, 10)
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a3') .. ';state=0;ver=2;actor=host'), 'woke up: NORMAL v2 (reached)')
    F.clear(c)
    c.inert = true
    F.push(c, 1, OP.REQ_OUTCOME, 23, u32('sol_a3'), 1, 2, 2, 3, 0, 0, 1, 2, 3)
    F.tick(env, c, 100, 33)
    c.inert = false
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a3') .. ';state=2;ver=3;actor=guest') and
        has(c.logs, 'event=apply;ent=' .. hs('sol_a3') .. ';state=2;ver=3;src=commit;result=fail'), 'guest SLEEP v3, the apply failed')
    check(not has(c.logs, 'event=commit;ent=' .. hs('sol_a3') .. ';state=0;ver=4'), 'no backward sweep over the guest outcome')
    -- Once the host's soldier reads back at the committed state, the sweep follows it again.
    c.life[c.ids.sol_a3] = LIFE.SLEEP
    F.push(c, 1, OP.AREA, 1, 30010)
    c.enemy.UpdateOutOfAreaSetting(1)
    F.tick(env, c, 3)
    c.life[c.ids.sol_a3] = 0
    F.tick(env, c, 10)
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a3') .. ';state=0;ver=4;actor=host;rid=0;cause=9'), 'reached again: swept')
end

-- 6. Guest: attribution, requests, the apply rule and fultons.
do
    local env, c = fixture({role = 2})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    local W = c.W
    check(F.tx(c, OP.RESYNC)[1] and F.tx(c, OP.RESYNC)[1].v[2] == 7, 'guest asks for RESYNC 7 at PAIRED')
    F.push(c, 1, OP.AREA, 2, 30010)  -- the host is in area 2: both areas active here
    F.tick(env, c, 1)
    F.clear(c)
    c.life[c.ids.sol_a1] = LIFE.DEAD
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a1, 0, 77)
    F.tick(env, c, 1)
    local req = F.tx(c, OP.REQ_OUTCOME)[1]
    check(req and req.v[2] == u32('sol_a1') and req.v[4] == 5 and req.v[5] == 0 and req.v[6] == 1 and req.v[7] == 77,
        'Dead by the local player: REQ_OUTCOME DEAD with attackId')
    local rid = req.v[1]
    check(has(c.logs, 'event=req;rid=' .. rid .. ';ent=' .. hs('sol_a1') .. ';state=5;seen=0'), 'req line')
    F.push(c, 0, OP.RECEIVED, rid, OP.REQ_OUTCOME)
    F.push(c, 1, OP.OUTCOME, u32('sol_a1'), 1, 5, 1, 1, 1, rid, 0, 0, 0, 0)
    F.tick(env, c, 2)
    check(has(c.logs, 'event=req_done;rid=' .. rid .. ';ms=') and has(c.logs, ';result=outcome'), 'req_done')
    check(has(c.logs, 'event=apply;ent=' .. hs('sol_a1') .. ';state=5;ver=1;src=outcome;result=terminal_local;readback=5'),
        'a local terminal state is never commanded')
    -- Another attacker inside the lastLocal window: divergence, no request.
    F.clear(c)
    F.msg(env, 'GameObject', 'Damage', c.ids.sol_a2, 5, 0)
    c.life[c.ids.sol_a2] = LIFE.DEAD
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a2, 55, 5)
    F.tick(env, c, 1)
    check(#F.tx(c, OP.REQ_OUTCOME) == 0 and has(c.logs, 'event=divergence;ent=' .. hs('sol_a2') .. ';state=5;why=not_attributed'),
        'another attacker: divergence')
    -- Unconscious (no attacker slot) after local damage: requested.
    F.clear(c)
    F.msg(env, 'GameObject', 'Damage', c.ids.sol_a3, 5, 0)
    c.time = c.time + 5000
    c.life[c.ids.sol_a3] = LIFE.SLEEP
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_a3)
    F.tick(env, c, 1)
    req = F.tx(c, OP.REQ_OUTCOME)[1]
    check(req and req.v[2] == u32('sol_a3') and req.v[4] == 2 and req.v[6] == 3, 'Unconscious inside the lastLocal window')
    -- Fulton without FultonInfo: nothing; FultonInfo by the local player + IsRecovered: FULTONED.
    F.clear(c)
    F.msg(env, 'GameObject', 'Fulton', c.ids.sol_b1, 0, 0, 0)
    F.tick(env, c, 2)
    check(#F.tx(c, OP.REQ_OUTCOME) == 0, 'a Fulton message alone is not a fulton')
    c.recovered.sol_b1 = true
    F.msg(env, 'GameObject', 'FultonInfo', c.ids.sol_b1, 0, 0)
    F.tick(env, c, 1)
    req = F.tx(c, OP.REQ_OUTCOME)[1]
    check(req and req.v[2] == u32('sol_b1') and req.v[4] == 6 and req.v[6] == 5, 'FultonInfo by the local player: FULTONED')
    -- A quest auto-fulton (another fultoner): divergence.
    F.clear(c)
    c.recovered.sol_b2 = true
    F.msg(env, 'GameObject', 'FultonInfo', c.ids.sol_b2, NULL_ID, 0)
    F.tick(env, c, 1)
    check(#F.tx(c, OP.REQ_OUTCOME) == 0 and has(c.logs, 'event=divergence;ent=' .. hs('sol_b2') .. ';state=6'), 'quest fulton: divergence')
    -- REJECT 2 applies the host's terminal state.
    F.clear(c)
    c.life[c.ids.sol_q1] = LIFE.SLEEP
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_q1)
    W.attribOverride[u32('sol_q1')] = c.time
    F.tick(env, c, 1)
    req = F.tx(c, OP.REQ_OUTCOME)[1]
    check(req and req.v[4] == 2, 'override attributes')
    F.push(c, 0, OP.REJECT, req.v[1], OP.REQ_OUTCOME, 2, u32('sol_q1'), 5, 1)
    F.tick(env, c, 2)
    check(has(c.logs, 'event=req_done;rid=' .. req.v[1] .. ';result=reject;reason=2;op=17;state=5;ver=1'), 'reject logged')
    check(F.callCount(c, 'ChangeLifeState:sol_q1:' .. LIFE.DEAD) == 1, 'REJECT 2 applies the host state')
    -- known follows ver: SLEEP v1, wake v2 (backward, skipped), a new local knockout requests again.
    F.clear(c)
    F.push(c, 1, OP.OUTCOME, u32('sol_b3'), 1, 2, 1, 3, 0, 0, 0, 0, 0, 0)
    F.tick(env, c, 15)
    check(has(c.logs, 'event=apply;ent=' .. hs('sol_b3') .. ';state=2;ver=1;src=outcome;result=ok;readback=2'), 'outcome applied')
    c.life[c.ids.sol_b3] = 0
    F.push(c, 1, OP.OUTCOME, u32('sol_b3'), 1, 0, 2, 9, 0, 0, 2, 0, 0, 0)
    F.tick(env, c, 1)
    check(has(c.logs, 'event=apply;ent=' .. hs('sol_b3') .. ';state=0;ver=2;src=outcome;result=skip_backward'), 'wake-up skipped')
    W.attribOverride[u32('sol_b3')] = c.time
    c.life[c.ids.sol_b3] = LIFE.SLEEP
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_b3)
    F.tick(env, c, 1)
    req = F.tx(c, OP.REQ_OUTCOME)[1]
    check(req and req.v[2] == u32('sol_b3') and req.v[5] == 2, 'a second knockout is requested again (seen ver 2)')
    -- No RECEIVED within 3 s: dropped, RESYNC 1.
    F.clear(c)
    F.tick(env, c, 100, 33)
    check(has(c.logs, 'event=req_timeout;rid=' .. req.v[1]) and F.tx(c, OP.RESYNC)[1] and F.tx(c, OP.RESYNC)[1].v[2] == 1,
        'RECEIVED timeout: RESYNC 1')
    -- FULTONED from the host: deferred while a local fulton is pending; never resent after a Fulton message.
    F.clear(c)
    F.msg(env, 'GameObject', 'Fulton', c.ids.sol_q2, 0, 0, 0)
    F.push(c, 1, OP.OUTCOME, u32('sol_q2'), 1, 6, 1, 5, 0, 0, 0, 0, 0, 0)
    F.tick(env, c, 1)
    check(has(c.logs, 'event=ledger;ent=' .. hs('sol_q2') .. ';op=hold;reason=fulton_pending') and not c.fultonAsked,
        'FULTONED deferred while fulton_pending')
    F.msg(env, 'GameObject', 'FultonFailed', c.ids.sol_q2)
    F.tick(env, c, 15)
    check(c.fultonAsked == 1, 'after FultonFailed the force fulton is sent once')
    F.msg(env, 'GameObject', 'Fulton', c.ids.sol_q2, 0, 0, 0)
    F.tick(env, c, 250, 33)
    check(c.fultonAsked == 1, 'never resent after the Fulton message')
    c.recovered.sol_q2 = true
    F.tick(env, c, 10)
    check(has(c.logs, 'event=apply;ent=' .. hs('sol_q2') .. ';state=6;ver=1;src=outcome;result=ok;readback=6'), 'FULTONED readback')
end

-- 6b. A force fulton without any Fulton message is retried once at +5 s.
do
    local env, c = fixture({role = 2})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.push(c, 1, OP.OUTCOME, u32('sol_a1'), 1, 6, 1, 5, 0, 0, 0, 0, 0, 0)
    F.tick(env, c, 1)
    check(c.fultonAsked == 1, 'force fulton')
    F.tick(env, c, 200, 33)
    check(c.fultonAsked == 2, 'one retry without a Fulton message')
    F.tick(env, c, 500, 33)
    check(c.fultonAsked == 2 and has(c.logs, 'event=apply;ent=' .. hs('sol_a1') .. ';state=6;ver=1;src=outcome;result=fail'),
        'failed after 20 s, no further retry')
end

-- 6c. A request whose answer cannot come (UNPAIRED, REJECT 5/6/9, timeout) is
-- held and sent again after the next RESYNC_END.
do
    local env, c = fixture({role = 2})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    local s = c.W.enemy.s
    local function kill(name)
        c.life[c.ids[name]] = LIFE.DEAD
        F.msg(env, 'GameObject', 'Dead', c.ids[name], 0, 0)
        F.tick(env, c, 1)
        local list = F.tx(c, OP.REQ_OUTCOME)
        return list[#list].v[1]
    end
    F.clear(c)
    local r1 = kill('sol_a1')
    F.push(c, 0, OP.RECEIVED, r1, OP.REQ_OUTCOME)
    F.push(c, 0, OP.UNPAIRED, 1)
    F.tick(env, c, 1)
    check(#s.held == 1 and next(s.req) == nil and has(c.logs, 'event=req_hold;n=1'), 'UNPAIRED: the outstanding request is held')
    F.pair(env, c, 3)
    F.clear(c)
    local r2 = kill('sol_a2')
    F.push(c, 0, OP.REJECT, r2, OP.REQ_OUTCOME, 9, u32('sol_a2'), 0, 0)
    F.tick(env, c, 1)
    local rs = F.tx(c, OP.RESYNC)
    check(#s.held == 2 and next(s.req) == nil and #rs == 1 and rs[1].v[2] == 1, 'REJECT 9 (busy): held, RESYNC 1')
    local r3 = kill('sol_a3')
    F.push(c, 0, OP.REJECT, r3, OP.REQ_OUTCOME, 3, u32('sol_a3'), 0, 0)
    F.tick(env, c, 1)
    check(#s.held == 2, 'REJECT 3 is final (not held)')
    F.clear(c)
    F.push(c, 1, OP.RESYNC_END, 1, 7, 0, 0)
    F.tick(env, c, 1)
    local req = F.tx(c, OP.REQ_OUTCOME)
    check(#req == 2 and req[1].v[2] == u32('sol_a1') and req[2].v[2] == u32('sol_a2') and #s.held == 0, 'both sent again after RESYNC_END')
    -- No RECEIVED within 3 s: held, RESYNC 1 (once per 10 s), sent again at RESYNC_END.
    F.clear(c)
    c.time = c.time + 10000
    F.tick(env, c, 100, 33)
    check(#s.held == 2 and has(c.logs, 'event=req_timeout;rid=') and #F.tx(c, OP.RESYNC) == 1, 'timeout: held')
end

-- 7. Host request validation, REJECT 11 after a continue, no world drain outside the game.
do
    local env, c = fixture({role = 1})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.clear(c)
    c.life[c.ids.sol_a1] = LIFE.DEAD
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a1, 0, 5)
    F.push(c, 1, OP.REQ_OUTCOME, 31, u32('sol_a1'), 1, 5, 0, 1, 0, 0, 0, 0, 0)
    F.push(c, 1, OP.REQ_OUTCOME, 32, u32('nobody'), 1, 5, 0, 1, 0, 0, 0, 0, 0)
    F.push(c, 1, OP.REQ_OUTCOME, 33, u32('sol_a2'), 1, 2, 0, 3, 0, 0, 0, 0, 0)
    F.push(c, 1, OP.REQ_OUTCOME, 34, u32('sol_a2'), 1, 2, 0, 3, 0, 0, 0, 0, 0)
    F.tick(env, c, 1)
    local rej = F.tx(c, OP.REJECT)
    check(#rej == 3, 'three rejects')
    check(rej[1].v[1] == 31 and rej[1].v[3] == 2 and rej[1].v[5] == 5 and rej[1].v[6] == 1,
        'the host own kill committed first (C32): the guest request is REJECT terminal with DEAD v1')
    check(rej[2].v[1] == 32 and rej[2].v[3] == 1, 'unknown entity')
    check(rej[3].v[1] == 34 and rej[3].v[3] == 3 and rej[3].v[5] == 2, 'not forward')
    check(has(c.logs, 'event=commit;ent=' .. hs('sol_a1') .. ';state=5;ver=1;actor=host'), 'actor host')
    -- No world drain while CheckMissionState is not true.
    c.inGame = false
    c.rxMasks = {}
    F.push(c, 1, OP.REQ_OUTCOME, 35, u32('sol_b1'), 1, 2, 0, 3, 0, 0, 0, 0, 0)
    F.tick(env, c, 3)
    check(c.rxMasks[1] == 1 and #c.rx == 1, 'mask 1 while not in game: the request waits')
    c.inGame = true
    F.tick(env, c, 1)
    check(#c.rx == 0 and has(c.logs, 'event=commit;ent=' .. hs('sol_b1') .. ';state=2;ver=1;actor=guest;rid=35'), 'answered later')
    -- A continue: W1 off for the mission, requests answered with REJECT 11, W2 continues.
    local env2, c2 = fixture({role = 1})
    c2.missionStart = false
    F.start(env2, c2)
    F.pair(env2, c2, 3)
    check(has(c2.logs, 'event=inactive;reason=continue;scope=w1') and not c2.W.w1 and c2.W.active, 'continue: W1 off, active')
    F.push(c2, 1, OP.REQ_OUTCOME, 36, u32('sol_b1'), 1, 2, 0, 3, 0, 0, 0, 0, 0)
    F.tick(env2, c2, 1)
    check(F.tx(c2, OP.REJECT)[1] and F.tx(c2, OP.REJECT)[1].v[3] == 11, 'REJECT 11 inactive')
    F.tick(env2, c2, 40)
    check(#F.tx(c2, OP.DIGEST) >= 1, 'digests continue')
    -- A game over seen in the tick turns W1 off.
    local env3, c3 = fixture({role = 1})
    F.start(env3, c3)
    env3.svars.mis_isDefiniteGameOver = true
    F.tick(env3, c3, 1)
    check(has(c3.logs, 'event=inactive;reason=game_over;scope=w1') and not c3.W.w1, 'game over: W1 off')
end

-- 8. The ledger: out of area, held; applied when the area becomes active; dropped at stop.
do
    local env, c = fixture({role = 2})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.clear(c)
    F.push(c, 1, OP.OUTCOME, u32('sol_b1'), 1, 5, 1, 1, 0, 0, 0, 0, 0, 0)
    F.tick(env, c, 3)
    check(has(c.logs, 'event=ledger;ent=' .. hs('sol_b1') .. ';op=hold;reason=out_of_area;waited_ms=0') and
        F.callCount(c, 'ChangeLifeState:sol_b1:' .. LIFE.DEAD) == 0, 'out of area: held')
    F.push(c, 1, OP.AREA, 2, 30010)
    F.tick(env, c, 15)
    check(has(c.logs, 'event=ledger;ent=' .. hs('sol_b1') .. ';op=applied;reason=out_of_area;waited_ms=') and
        has(c.logs, 'event=apply;ent=' .. hs('sol_b1') .. ';state=5;ver=1;src=outcome;result=ok;readback=5'),
        'applied once the partner area joins the union')
    F.push(c, 1, OP.AREA, 1, 30010)
    F.tick(env, c, 80)
    F.push(c, 1, OP.OUTCOME, u32('sol_b2'), 1, 5, 1, 1, 0, 0, 0, 0, 0, 0)
    F.tick(env, c, 3)
    check(has(c.logs, 'event=ledger;ent=' .. hs('sol_b2') .. ';op=hold;reason=out_of_area'), 'held again')
    F.call(env, 'OnTerminate')
    check(has(c.logs, 'event=ledger;ent=' .. hs('sol_b2') .. ';op=dropped;reason=stop') and has(c.logs, 'event=stop'), 'dropped at stop')
    check(F.tx(c, OP.MISSION_END)[1] and F.tx(c, OP.MISSION_END)[1].v[1] == 30010, 'MISSION_END at stop')
end

-- 8b. A deferred apply (readback in flight) is retried once per frame; the queue stays bounded.
do
    local env, c = fixture({role = 2})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.push(c, 1, OP.OUTCOME, u32('sol_a1'), 1, 6, 1, 5, 0, 0, 0, 0, 0, 0)
    F.tick(env, c, 1)
    local s = c.W.enemy.s
    check(s.rb[u32('sol_a1')] ~= nil, 'force fulton readback in flight')
    F.push(c, 1, OP.OUTCOME, u32('sol_a1'), 1, 6, 2, 8, 0, 0, 1, 0, 0, 0)
    F.tick(env, c, 30)
    check(#s.queue == 1 and s.qHead == 1, 'one deferred entry, no growth')
    c.recovered.sol_a1 = true
    F.tick(env, c, 30)
    check(#s.queue == 0 and has(c.logs, 'event=apply;ent=' .. hs('sol_a1') .. ';state=6;ver=2;src=resync;result=terminal_local'),
        'applied once the readback ended')
end

-- 9. The out-of-area wrapper.
do
    local env, c = fixture({role = 1, cpList = {[1] = {'cp_a'}, [2] = {'cp_b'}}})
    local original = c.enemy.UpdateOutOfAreaSetting
    env.e.OnInitialize({enemy = c.enemy}, nil, 9)
    check(c.enemy.UpdateOutOfAreaSetting ~= original, 'wrapped at OnInitialize (level 2+, 30010)')
    local r1, r2 = c.enemy.UpdateOutOfAreaSetting(1, 'x')
    check(r1 == 'area' and r2 == 1 and c.areaArg[1] == 1 and c.areaArg[2] == 'x' and c.areaSeen.cp_a and not c.areaSeen.cp_b and
        c.areaSeen.quest_cp and c.W.areaState.localArea == 1, 'a preparation call passes through and records the area')
    env.e.OnInitialize({enemy = c.enemy}, nil, 9)
    c.enemy.UpdateOutOfAreaSetting(1)
    check(c.areaCalls == 2, 'wrapped once per table')
    F.call(env, 'OnMissionGameStart')
    F.pair(env, c, 3)
    check(F.tx(c, OP.AREA)[1] and F.tx(c, OP.AREA)[1].v[1] == 1, 'PLAYER_AREA at PAIRED')
    local list1 = c.enemy.cpList[1]
    F.push(c, 1, OP.AREA, 2, 30010)
    F.tick(env, c, 2)
    check(c.areaSeen.cp_a and c.areaSeen.cp_b and c.enemy.cpList[1] == list1, 'union substituted and restored')
    check(has(c.logs, 'event=area;shape=array'), 'array shape logged')
    c.areaError = true
    local ok, err = pcall(c.enemy.UpdateOutOfAreaSetting, 1)
    c.areaError = false
    check(not ok and tostring(err):find('area failure', 1, true) and c.enemy.cpList[1] == list1, 'an error is re-raised, cpList restored')
    F.tick(env, c, 70)
    F.clear(c)
    c.enemy.UpdateOutOfAreaSetting(2)
    F.tick(env, c, 1)
    check(#F.tx(c, OP.AREA) == 1 and F.tx(c, OP.AREA)[1].v[1] == 2, 'PLAYER_AREA on a local area change')
    c.enemy.UpdateOutOfAreaSetting(1)
    F.tick(env, c, 5)
    check(#F.tx(c, OP.AREA) == 1, 'PLAYER_AREA at most once per 2 s')
    F.tick(env, c, 70)
    check(#F.tx(c, OP.AREA) == 2 and F.tx(c, OP.AREA)[2].v[1] == 1, 'the pending change sent after the window')
    F.clear(c)
    c.W.area.simulate(2)
    F.tick(env, c, 70)
    check(#F.tx(c, OP.AREA) == 0, 'simulate sends no PLAYER_AREA')
    -- UNPAIRED: the partner's area leaves the union at once (the original reruns for the local area).
    c.enemy.UpdateOutOfAreaSetting(1)
    check(c.areaSeen.cp_b, 'union while paired')
    local calls = c.areaCalls
    F.push(c, 0, OP.UNPAIRED, 4)
    F.tick(env, c, 1)
    check(c.areaCalls == calls + 1 and c.areaArg[1] == 1 and c.areaSeen.cp_a and not c.areaSeen.cp_b and
        c.W.areaState.partner == nil, 'UNPAIRED: the original reruns for the local area alone')
    c.W.areaState.partner = 2
    c.enemy.UpdateOutOfAreaSetting(1)
    check(not c.areaSeen.cp_b, 'no union while unpaired')
end

-- 10. Alert: no latch, decays never request, escalations broadcast again, floor rate limit.
do
    local envg, g = fixture({role = 2})
    F.start(envg, g, function() g.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(envg, g, 3)
    F.clear(g)
    F.push(g, 1, OP.PHASE, u32('cp_a'), 3, 1, 0, 0, 0)
    F.tick(envg, g, 1)
    check(F.callCount(g, 'SetPhase:cp_a:' .. PHASE.ALERT) == 1, 'guest applies the host ALERT')
    F.msg(envg, 'GameObject', 'ChangePhase', g.ids.cp_a, PHASE.ALERT)
    F.tick(envg, g, 40)
    check(#F.tx(g, OP.REQ_PHASE) == 0 and g.W.cp[u32('cp_a')].own == 0, 'a host phase never raises ownPhase (no latch)')
    local vec = F.tx(g, OP.PVEC)[1]
    check(vec and vec.v[1] == 3 and vec.v[3] == 1 and vec.v[4] == 0, 'guest vector carries its own phases only')
    F.msg(envg, 'GameObject', 'ChangePhase', g.ids.cp_a, PHASE.EVASION)
    F.tick(envg, g, 1)
    check(#F.tx(g, OP.REQ_PHASE) == 0, 'a decay never requests')
    F.msg(envg, 'GameObject', 'ChangePhase', g.ids.cp_b, PHASE.CAUTION)
    F.tick(envg, g, 1)
    local rq = F.tx(g, OP.REQ_PHASE)
    check(#rq == 1 and rq[1].v[2] == u32('cp_b') and rq[1].v[3] == 1, 'an own escalation requests')
    F.msg(envg, 'GameObject', 'ChangePhase', g.ids.cp_b, PHASE.SNEAK)
    F.msg(envg, 'GameObject', 'ChangePhase', g.ids.cp_b, PHASE.CAUTION)
    F.tick(envg, g, 1)
    check(#F.tx(g, OP.REQ_PHASE) == 1, 'the same phase again within 30 s does not request again')
    F.msg(envg, 'GameObject', 'ChangePhase', g.ids.cp_b, PHASE.ALERT)
    F.tick(envg, g, 1)
    check(#F.tx(g, OP.REQ_PHASE) == 2 and has(g.logs, 'event=req_phase;rid='), 'a higher one requests')
    -- Host: own escalations broadcast every time; floor follows the guest's own phases only.
    local envh, h = fixture({role = 1})
    F.start(envh, h, function() h.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(envh, h, 3)
    F.clear(h)
    F.msg(envh, 'GameObject', 'ChangePhase', h.ids.cp_a, PHASE.ALERT)
    F.msg(envh, 'GameObject', 'ChangePhase', h.ids.cp_a, PHASE.SNEAK)
    F.msg(envh, 'GameObject', 'ChangePhase', h.ids.cp_a, PHASE.ALERT)
    F.tick(envh, h, 1)
    local ph = F.tx(h, OP.PHASE)
    check(#ph == 2 and ph[1].v[2] == 3 and ph[1].v[3] == 1 and ph[2].v[2] == 3 and ph[2].v[3] == 2 and
        has(h.logs, 'event=phase;cp=' .. hs('cp_a') .. ';phase=3;ver=2;src=host;rid=0'), 'a second ALERT after a decay is broadcast')
    h.phase.cp_a = PHASE.SNEAK
    -- The guest's own ALERT on cp_b (vector): floor, at most once per 10 s,
    -- while the guest keeps reporting it (PHASE_VEC once a second).
    h.inert = true
    local function guestVec(word, seconds)
        for _ = 1, seconds do
            F.push(h, 1, OP.PVEC, 3, 0, 1, word, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
            F.tick(envh, h, 30, 33)
        end
    end
    F.push(h, 1, OP.PVEC, 3, 0, 1, 3 * 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    F.tick(envh, h, 1)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 1 and has(h.logs, 'event=floor;cp=' .. hs('cp_b') .. ';host=0;guest=3'),
        'floor to the guest own phase')
    guestVec(3 * 4, 4)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 1, 'floor rate limit (10 s)')
    guestVec(3 * 4, 8)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 2, 'floor again after 10 s')
    -- No report for 5 s (the guest's W1 went off, or the vectors stopped): the floor ends.
    F.tick(envh, h, 600, 33)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 2 and h.W.cp[u32('cp_b')].guestOwn == 0, 'a stale guest phase never floors')
    -- Unpaired: the guest's phases are cleared at once and the floor stops.
    guestVec(3 * 4, 1)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 3, 'floor again with a fresh report')
    F.push(h, 0, OP.UNPAIRED, 1)
    F.tick(envh, h, 400, 33)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 3 and h.W.cp[u32('cp_b')].guestOwn == 0 and h.W.eff == 0,
        'UNPAIRED clears the guest phases (eff 0, no floor)')
    F.pair(envh, h, 3)
    h.inert = false
    F.push(h, 1, OP.PVEC, 3, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
    F.tick(envh, h, 1)
    F.clear(h)
    F.tick(envh, h, 400, 33)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 0 and not has(h.logs, 'event=floor'), 'no floor after the guest decays')
    -- REQ_PHASE: SetPhase, and the escalation broadcast carries the rid.
    h.phase.cp_b = PHASE.SNEAK
    F.tick(envh, h, 2)
    F.clear(h)
    F.push(h, 1, OP.REQ_PHASE, 51, u32('cp_b'), 3, 0, 0, 0, 0, 0)
    F.tick(envh, h, 1)
    check(F.callCount(h, 'SetPhase:cp_b:' .. PHASE.ALERT) == 1, 'REQ_PHASE applied')
    F.msg(envh, 'GameObject', 'ChangePhase', h.ids.cp_b, PHASE.ALERT)
    F.tick(envh, h, 1)
    ph = F.tx(h, OP.PHASE)
    check(#ph == 1 and ph[1].v[5] == 51 and has(h.logs, 'src=req;rid=51'), 'src=req with rid')
end

-- 10b. PHASE_VEC with 77 CPs (pack on the host, unpack on the guest) and the 192-CP limit.
do
    local defs = {}
    for i = 0, 76 do defs[string.format('cp_%02d', i)] = {string.format('s_%02d', i)} end
    local envh, h = fixture({role = 1, defs = defs})
    F.start(envh, h)
    F.pair(envh, h, 3)
    local W = h.W
    for i, cp in ipairs(W.cpOrder) do W.cp[cp].localPhase = i % 4; h.phase[W.cp[cp].name] = PHASE.SNEAK + i % 4 end
    F.clear(h)
    F.tick(envh, h, 40)
    local vec = F.tx(h, OP.PVEC)[1]
    check(vec and vec.v[1] == 77 and vec.v[3] == 5 and #vec.v == 15 and vec.v[9] == 0, 'PHASE_VEC: 77 CPs in 5 words')
    for _, w in ipairs(vec.v) do check(w >= 0 and w <= 4294967295, 'words are u32') end
    local envg, g = fixture({role = 2, defs = defs})
    F.start(envg, g)
    F.pair(envg, g, 3)
    g.inert = true
    F.push(g, 1, OP.PVEC, unpack(vec.v))
    F.tick(envg, g, 1)
    local same = true
    for i, cp in ipairs(g.W.cpOrder) do if g.W.cp[cp].hostPhase ~= i % 4 then same = false end end
    check(same, 'the guest unpacks every phase')
    local big = {}
    for i = 0, 192 do big[string.format('cp_%03d', i)] = {string.format('s_%03d', i)} end
    local envb, b = fixture({role = 1, defs = big})
    F.start(envb, b)
    check(has(b.logs, 'event=phase_vec;disabled=1;cps=193'), 'more than 192 CPs disables the vector')
end

-- 11. Digest mismatch -> RESYNC; the host resync stream; queued requests re-checked after RESYNC_END.
do
    local env, c = fixture({role = 2})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.clear(c)
    for i = 1, 3 do
        F.push(c, 1, OP.DIGEST, i, 43200, 2000, 0, 0, 9, 1, 3, 1, 10)
        F.tick(env, c, 1)
    end
    check(has(c.logs, 'event=digest_mismatch;what=outcomes;n=3') and F.tx(c, OP.RESYNC)[1] and F.tx(c, OP.RESYNC)[1].v[2] == 1,
        '3 mismatching digests: RESYNC 1')
    F.clear(c)
    for i = 4, 6 do
        F.push(c, 1, OP.DIGEST, i, 43200, 2000, 0, 0, 9, 1, 3, 1, 10)
        F.tick(env, c, 1)
    end
    check(#F.tx(c, OP.RESYNC) == 0, 'at most one RESYNC per 10 s')
    -- A request made while unpaired is held, re-checked after RESYNC_END, then sent.
    F.push(c, 0, OP.UNPAIRED, 1)
    F.tick(env, c, 1)
    F.clear(c)
    c.life[c.ids.sol_a1] = LIFE.SLEEP
    c.W.attribOverride[u32('sol_a1')] = c.time
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_a1)
    c.life[c.ids.sol_a2] = LIFE.SLEEP
    c.W.attribOverride[u32('sol_a2')] = c.time
    F.msg(env, 'GameObject', 'Unconscious', c.ids.sol_a2)
    F.tick(env, c, 1)
    check(#F.tx(c, OP.REQ_OUTCOME) == 0, 'held while unpaired')
    F.pair(env, c, 3)
    c.life[c.ids.sol_a2] = 0  -- woke up meanwhile: no longer forward
    F.push(c, 1, OP.RESYNC_END, 1, 7, 0, 0)
    F.tick(env, c, 1)
    local req = F.tx(c, OP.REQ_OUTCOME)
    check(#req == 1 and req[1].v[2] == u32('sol_a1') and has(c.logs, 'event=resync;what=7;count=0;src=end;held=2'),
        'held requests re-checked after RESYNC_END')
end
do
    local defs = {}
    for i = 0, 39 do defs[string.format('cp_%02d', i)] = {string.format('s_%02d', i)} end
    local env, c = fixture({role = 1, defs = defs})
    F.start(env, c)
    F.pair(env, c, 3)
    for i = 0, 39 do
        local id = c.ids[string.format('s_%02d', i)]
        c.life[id] = LIFE.DEAD
        F.msg(env, 'GameObject', 'Dead', id, 0, 5)
    end
    F.tick(env, c, 1)
    check(count(c.logs, 'event=commit;') == 40, '40 commits')
    F.clear(c)
    F.push(c, 0, OP.RESYNC, 9, 7)
    F.tick(env, c, 1)
    local out = F.tx(c, OP.OUTCOME)
    check(#out == 32 and out[1].v[8] == 1 and out[1].v[5] == 8 and #F.tx(c, OP.RESYNC_END) == 0, 'at most 32 per frame, flags 1')
    F.tick(env, c, 1)
    local stream = 0
    for _, t in ipairs(c.tx) do if t.op == OP.OUTCOME or t.op == OP.CLOCK or t.op == OP.WEATHER then stream = stream + 1 end end
    local done = F.tx(c, OP.RESYNC_END)[1]
    check(done and done.v[1] == 9 and done.v[2] == 7 and done.v[3] == 42 and done.v[4] == 40 and stream == 42,
        'RESYNC_END{rid, what, count, outcomeSeq} after the outcomes, the clock and the weather')
end

-- 11b. Re-pairing in a mission: the same host epoch keeps W1; another host
do
    local env, c = fixture({role = 1})
    F.start(env, c)
    F.pair(env, c, 3)
    local h = u32('sol_a1')
    c.W.ent[h].committed, c.W.ent[h].ver = 5, 1
    c.W.env.s.resync = {rid = 10, what = 1, items = {{1, h}}, i = 1, count = 0}
    c.txReply = {0, 8}
    c.W.env.resyncTick(c.time)
    check(c.W.env.s.resync.i == 1 and c.W.env.s.resync.count == 0,
        'resync queue pressure retains unsent item and count')
    c.txReply = nil
    local original = c.W.tx
    c.W.tx = function(op, ...)
        if op == OP.RESYNC_END then return false end
        return original(op, ...)
    end
    c.W.env.resyncTick(c.time)
    check(c.W.env.s.resync.i == 2 and c.W.env.s.resync.count == 1,
        'resync end queue pressure retains accepted stream')
    c.W.tx = original
    c.W.env.resyncTick(c.time)
    check(c.W.env.s.resync == nil, 'resync only retires after completion accepted')
end

-- Re-pairing keeps W1 only for the same authoritative world.
-- epoch is another host world, so W1 goes off (held requests dropped).
do
    local env, c = fixture({role = 2})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.push(c, 0, OP.UNPAIRED, 1)
    F.tick(env, c, 1)
    F.pair(env, c, 3)
    check(c.W.w1 and not has(c.logs, 'reason=epoch'), 'the same epoch again: W1 stays on')
    F.push(c, 0, OP.UNPAIRED, 3)
    F.tick(env, c, 1)
    c.life[c.ids.sol_a1] = LIFE.DEAD
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a1, 0, 0)
    F.tick(env, c, 1)
    check(#c.W.enemy.s.held == 1, 'a kill while unpaired is held')
    F.clear(c)
    F.push(c, 0, OP.PAIRED, 6, 30010, 77, 8, 1, 3)
    F.tick(env, c, 1)
    check(not c.W.w1 and c.W.active and has(c.logs, 'event=inactive;reason=epoch;scope=w1') and #c.W.enemy.s.held == 0 and
        #F.tx(c, OP.REQ_OUTCOME) == 0, 'another host epoch: W1 off for the mission')
    check(F.tx(c, OP.RESYNC)[1] and F.tx(c, OP.RESYNC)[1].v[2] == 7, 'clock and weather still resync')
end

-- 12. Clock.
do
    local env, c = fixture({role = 2})
    F.start(env, c)
    F.pair(env, c, 3)
    F.clear(c)
    local function digest(clock, lat)
        F.push(c, 1, OP.DIGEST, c.time, clock, 0, 0, 0, 0, 0, 3, 1, lat or 0)
        F.tick(env, c, 1)
    end
    env.vars.clock = 43200
    digest(43259)
    digest(43259)
    check(env.vars.clock == 43200 and not has(c.logs, 'event=clock;'), '59 s: no correction')
    digest(43261)
    check(env.vars.clock == 43200, 'one digest over 60 s is not enough')
    digest(43261)
    check(env.vars.clock == 43261 and has(c.logs, 'event=clock;cause=digest;diff=-61;applied=1'), '61 s twice: corrected')
    env.vars.clock = 86390
    digest(10)
    digest(10)
    check(env.vars.clock == 86390, 'across midnight the difference is 20 s')
    env.vars.clock = 1000
    F.push(c, 1, OP.CLOCK, 5000, 2000, 1, 1, 500)
    F.tick(env, c, 1)
    check(env.vars.clock == 5010 and has(c.logs, 'event=clock;cause=jump;diff=-4010;applied=1'), 'CLOCK at once, + rate x latency')
    c.demo = true
    F.push(c, 1, OP.CLOCK, 9000, 2000, 1, 1, 0)
    F.tick(env, c, 1)
    check(env.vars.clock == 5010, 'suspended during a demo')
    c.demo = false
    check(has(c.logs, 'event=clock_check;diff='), 'clock_check line')
    -- Host: the per-frame delta against 20 x min(dt, 0.25); a 5 s frame gap is no jump.
    local envh, h = fixture({role = 1})
    F.start(envh, h)
    F.pair(envh, h, 3)
    F.clear(h)
    for _ = 1, 30 do
        envh.vars.clock = envh.vars.clock + 0.66
        F.tick(envh, h, 1, 33)
    end
    check(#F.tx(h, OP.CLOCK) == 0, 'normal flow: no CLOCK')
    envh.vars.clock = envh.vars.clock + 100
    F.tick(envh, h, 1, 5000)
    check(#F.tx(h, OP.CLOCK) == 0, 'a 5 s frame gap gives no false jump')
    envh.vars.clock = envh.vars.clock + 3600
    F.tick(envh, h, 1, 33)
    local ck = F.tx(h, OP.CLOCK)
    check(#ck == 1 and ck[1].v[3] == 1 and has(h.logs, 'event=clock_tx;cause=jump'), 'a jump sends CLOCK cause 1')
    F.clear(h)
    for _ = 1, 40 do
        envh.vars.clock = (envh.vars.clock + 30) % 86400
        F.tick(envh, h, 1, 50)
    end
    local fast = 0
    for _, t in ipairs(F.tx(h, OP.CLOCK)) do if t.v[3] == 2 then fast = fast + 1 end end
    check(fast >= 4 and fast <= 9, 'fast mode: CLOCK cause 2 at most 4 Hz')
    for _ = 1, 40 do
        envh.vars.clock = envh.vars.clock + 1
        F.tick(envh, h, 1, 50)
    end
    local ended = false
    for _, t in ipairs(F.tx(h, OP.CLOCK)) do if t.v[3] == 4 then ended = true end end
    check(ended, 'fast_end after 1 s at <= 30x')
end

-- 13. Weather.
do
    local env, c = fixture({role = 1})
    F.start(env, c)
    F.pair(env, c, 3)
    F.clear(c)
    env.vars.weather = 2
    F.tick(env, c, 1)
    local w = F.tx(c, OP.WEATHER)
    check(#w == 1 and w[1].v[1] == 2 and w[1].v[2] == 20 and w[1].v[3] == 65535 and w[1].v[4] == 255 and w[1].v[5] == 1,
        'host weather change: WEATHER{2, 20, none, none, change}')
    F.tick(env, c, 5)
    check(#F.tx(c, OP.WEATHER) == 1, 'sent once per change')
    local env2, c2 = fixture({role = 1, mission = 30020})
    F.start(env2, c2)
    F.pair(env2, c2, 3)
    F.clear(c2)
    env2.vars.weather = 4
    F.tick(env2, c2, 1)
    check(F.tx(c2, OP.WEATHER)[1] and F.tx(c2, OP.WEATHER)[1].v[1] == 255, '30020 never mirrors FOGGY (255 = release)')
    local env3, c3 = fixture({role = 1, mission = 30050})
    F.start(env3, c3)
    F.pair(env3, c3, 3)
    F.clear(c3)
    env3.vars.weather = 2
    F.tick(env3, c3, 40)
    local mbWeather = F.tx(c3, OP.WEATHER)
    check(#mbWeather == 1 and mbWeather[1].v[1] == 2 and F.tx(c3, OP.DIGEST)[1].v[4] == 2 and not c3.W.w1,
        '30050: actual host rain is sent once and in digests; enemy authority stays off')
    local mbEnv, mbGuest = fixture({role = 2, mission = 30050})
    F.start(mbEnv, mbGuest)
    F.pair(mbEnv, mbGuest, 3)
    check(F.callCount(mbGuest, 'Pause:true') == 1, '30050: guest random weather paused only while paired at level 3')
    F.clear(mbGuest)
    mbGuest.demo = true
    F.push(mbGuest, 1, OP.WEATHER, unpack(mbWeather[1].v))
    F.tick(mbEnv, mbGuest, 1)
    check(F.callCount(mbGuest, 'Force:2:20') == 0 and mbEnv.vars.weather == 0,
        '30050: host weather waits during a guest demo')
    mbGuest.demo = false
    F.tick(mbEnv, mbGuest, 1)
    check(F.callCount(mbGuest, 'Force:2:20') == 1 and mbEnv.vars.weather == 2 and
        has(mbGuest.logs, 'event=weather;type=2;cause=1;applied=1') and
        has(mbGuest.logs, 'event=weather_readback;type=2;matches=1'),
        '30050: resumed guest applies the transmitted host weather and reports actual readback')
    -- Vanilla MB scenes can force SUNNY then cancel locally after the
    -- initial resync. Unchanged host rain must recover from digests alone.
    mbGuest.demo = true
    mbEnv.TppWeather.ForceRequestWeather(0, 0)
    mbEnv.TppWeather.CancelForceRequestWeather()
    F.clear(mbGuest)
    local function mbDigest()
        F.push(mbGuest, 1, OP.DIGEST, mbGuest.time, mbEnv.vars.clock, 0, 2, 0, 0, 0, 3, 1, 0)
        F.tick(mbEnv, mbGuest, 1)
    end
    for _ = 1, 3 do mbDigest() end
    check(mbEnv.vars.weather == 0 and F.callCount(mbGuest, 'Force:2:20') == 0,
        '30050: drift recovery does not override weather during a demo')
    mbGuest.demo = false
    mbGuest.time = mbGuest.time + 30000 -- past the previous interpolation plus grace
    mbDigest(); mbDigest()
    check(F.callCount(mbGuest, 'Force:2:20') == 0, '30050: two mismatches do not re-force weather')
    mbDigest()
    check(F.callCount(mbGuest, 'Force:2:20') == 1 and mbEnv.vars.weather == 2 and #F.tx(c3, OP.WEATHER) == 1 and
        has(mbGuest.logs, 'event=weather;type=2;cause=3;applied=1'),
        '30050: three digests restore unchanged host rain after a local SUNNY force and cancellation')
    F.clear(mbGuest)
    F.push(mbGuest, 0, OP.LEVEL, 0)
    F.tick(mbEnv, mbGuest, 1)
    check(F.callCount(mbGuest, 'CancelForce') == 1 and F.callCount(mbGuest, 'Pause:false') == 1 and not mbGuest.W.active,
        '30050: level 0 releases our weather force and random pause')
    F.clear(mbGuest)
    F.push(mbGuest, 1, OP.WEATHER, 1, 20, 65535, 255, 1)
    F.tick(mbEnv, mbGuest, 5)
    check(#mbGuest.calls == 0, '30050: disabled world cannot apply later weather')
    for _, options in ipairs({{role = 2, mission = 30050, level = 2}, {role = 2, mission = 30050, level = 0},
                              {role = 2, mission = 10010}, {role = 2, mission = 40050},
                              {role = 2, mission = 30050, missing = {'TppWeather.ForceRequestWeather'}}}) do
        local refusedEnv, refused = fixture(options)
        F.start(refusedEnv, refused)
        F.pair(refusedEnv, refused, options.level or 3)
        F.clear(refused)
        F.push(refused, 1, OP.WEATHER, 2, 20, 65535, 255, 1)
        F.tick(refusedEnv, refused, 5)
        check(#F.tx(refused, OP.WEATHER) == 0 and F.callCount(refused, 'Force:2:20') == 0 and
            F.callCount(refused, 'Pause:true') == 0 and refusedEnv.vars.weather == 0,
            'weather remains inert for lower levels, story/ACC missions and missing public bindings')
    end
    -- Guest: force, release, pause re-asserted, re-force after 3 mismatches, cleanup.
    local eg, g = fixture({role = 2})
    F.start(eg, g)
    F.pair(eg, g, 3)
    check(F.callCount(g, 'Pause:true') == 1, 'random weather paused at PAIRED')
    F.clear(g)
    F.push(g, 1, OP.WEATHER, 2, 20, 65535, 255, 1)
    F.tick(eg, g, 1)
    check(F.callCount(g, 'Force:2:20') == 1 and has(g.logs, 'event=weather;type=2;cause=1;applied=1'), 'guest forces the host weather')
    F.push(g, 1, OP.WEATHER, 255, 20, 65535, 255, 1)
    F.tick(eg, g, 1)
    check(F.callCount(g, 'CancelForce') == 1 and F.callCount(g, 'Pause:false') == 1, '255: force cancelled, random weather unpaused')
    F.clear(g)
    eg.vars.weather = 0
    g.time = g.time + 30000  -- past interp + 5 s since the last force
    for i = 1, 3 do
        F.push(g, 1, OP.DIGEST, i, 43200, 2000, 3, 0, 0, 0, 3, 1, 0)
        F.tick(eg, g, 1)
    end
    check(F.callCount(g, 'Pause:true') == 3, 'pause re-asserted every digest')
    check(F.callCount(g, 'Force:3:20') == 1 and has(g.logs, 'event=weather;type=3;cause=3;applied=1'), 're-forced after 3 mismatches')
    check(has(g.logs, 'event=weather_check;local='), 'weather_check line')
    F.clear(g)
    g.W.fail('test failure')
    check(not g.W.active and count(g.logs, 'event=error;msg=test_failure') == 1, 'W.fail logs once')
    check(F.callCount(g, 'CancelForce') == 1 and F.callCount(g, 'Pause:false') == 1, 'W.fail immediately releases weather and random pause')
    g.W.fail('duplicate failure')
    check(F.callCount(g, 'CancelForce') == 1 and F.callCount(g, 'Pause:false') == 1, 'duplicate failure cannot repeat owned weather cleanup')
    F.clear(g)
    F.call(eg, 'OnTerminate')
    check(F.callCount(g, 'CancelForce') == 0 and F.callCount(g, 'Pause:false') == 0, 'termination after W.fail leaves released weather alone')
    local el, l = fixture({role = 2})
    F.start(el, l)
    F.pair(el, l, 3)
    F.push(l, 1, OP.WEATHER, 1, 20, 65535, 255, 1)
    F.tick(el, l, 1)
    F.clear(l)
    F.push(l, 0, OP.LEVEL, 0)
    F.tick(el, l, 1)
    check(F.callCount(l, 'CancelForce') == 1 and F.callCount(l, 'Pause:false') == 1 and not l.W.active and
        has(l.logs, 'event=inactive;reason=level0'), 'LEVEL 0: cleanup and off')
    F.clear(l)
    F.tick(el, l, 5)
    check(#l.tx == 0 and #l.receipts > 0, 'nothing after LEVEL 0')
end

-- 14. The lab goto.
do
    local env, c = fixture({role = 1, mission = 30050})
    F.start(env, c)
    F.clear(c)
    F.push(c, 0, OP.GOTO, 1, 30010, 0)
    F.tick(env, c, 1)
    check(F.callCount(c, 'f30050.ReserveMissionClear') == 1 and F.callCount(c, 'SetNextMBDemo:0') == 1 and
        F.callCount(c, 'ReserveMissionClear:6:40050') == 1, 'step 1: the heli boarding (SetNextMBDemo with no argument)')
    F.tick(env, c, 1)
    local ack = F.tx(c, OP.GOTO_ACK)
    check(#ack == 1 and ack[1].v[1] == 1 and ack[1].v[2] == 1 and has(c.logs, 'event=goto;step=1;result=reserved'), 'GOTO_ACK{1, 1}')
    local env2, c2 = fixture({role = 1, mission = 30050, noF30050 = true})
    F.start(env2, c2)
    c2.inGame = false
    F.push(c2, 0, OP.GOTO, 1, 30020, 1)
    F.tick(env2, c2, 3)
    check(#F.tx(c2, OP.GOTO_ACK) == 1 and F.tx(c2, OP.GOTO_ACK)[1].v[2] == 2, 'waiting once while not in game')
    c2.inGame = true
    F.tick(env2, c2, 2)
    check(c2.calls[1] == 'SetNextMBDemo:0' and c2.calls[2] == 'ReserveMissionClear:6:40050', 'fallback: SetNextMBDemo, then ReserveMissionClear')
    -- Step 2 at the ACC.
    local env3, c3 = fixture({role = 2, mission = 40050})
    F.start(env3, c3)
    c3.seq = 'Seq_Game_Opening'
    F.clear(c3)
    F.push(c3, 0, OP.GOTO, 2, 30010, 0)
    F.tick(env3, c3, 200, 33)
    ack = F.tx(c3, OP.GOTO_ACK)
    check(#ack == 1 and ack[1].v[1] == 2 and ack[1].v[2] == 2 and #c3.calls == 0, 'step 2 waits for Seq_Game_MainGame')
    c3.seq = 'Seq_Game_MainGame'
    F.tick(env3, c3, 1)
    local route = str32('lz_drp_field_N0000|rt_drp_field_N_0000')
    check(c3.calls[1] == 'SelectNextMissionHeliStartRoute:30010:' .. string.format('%.0f', route) .. ':false' and
        c3.calls[2] == 'OnEndMissionPreparation:0:nil', 'step 2: the sortie calls with the route hash and DEPLOY_TIME.CURRENT')
    check(has(c3.logs, 'event=goto;step=2;next=30010;route=' .. string.format('%.0f', route)), 'readback logged')
    F.tick(env3, c3, 1)
    ack = F.tx(c3, OP.GOTO_ACK)
    check(#ack == 2 and ack[2].v[2] == 1, 'GOTO_ACK{2, 1}')
    local env4, c4 = fixture({role = 1, mission = 40050})
    F.start(env4, c4)
    env4.gvars.mis_nextMissionCodeForEmergency = 50050
    F.push(c4, 0, OP.GOTO, 2, 30010, 0)
    F.tick(env4, c4, 120, 33)
    ack = F.tx(c4, OP.GOTO_ACK)
    check(#ack == 2 and ack[1].v[2] == 2 and ack[2].v[2] == 0 and #c4.calls == 0 and
        has(c4.logs, 'event=goto;step=2;result=refused;reason=not_heli_space'), 'an emergency code refuses step 2')
    -- Step 1 in free roam: the helicopter ride (a continued mission starts fresh).
    local env6, c6 = fixture({role = 1, mission = 30010})
    F.start(env6, c6)
    F.clear(c6)
    F.push(c6, 0, OP.GOTO, 1, 30010, 0)
    F.tick(env6, c6, 2)
    check(F.callCount(c6, 'RideOnHelicopter') == 1 and F.callCount(c6, 'f30050.ReserveMissionClear') == 0 and
        has(c6.logs, 'event=goto;step=1;call=ride_heli') and F.tx(c6, OP.GOTO_ACK)[1].v[2] == 1, 'free roam: ride the helicopter')
    local env5, c5 = fixture({role = 1, mission = 30050, mode = 2})
    F.start(env5, c5)
    F.push(c5, 0, OP.GOTO, 1, 30010, 0)
    F.tick(env5, c5, 2)
    check(F.tx(c5, OP.GOTO_ACK)[1] and F.tx(c5, OP.GOTO_ACK)[1].v[2] == 0 and #c5.calls == 0 and
        has(c5.logs, 'event=goto;step=1;result=refused;reason=mode'), 'refused in play')
end

-- 15. Probes.
do
    local env, c = fixture({role = 1, probe = 9})
    F.start(env, c, function() c.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(env, c, 3)
    F.tick(env, c, 200, 33)
    check(F.callCount(c, 'ChangeLifeState:sol_a1:' .. LIFE.DEAD) == 0, 'not before t0 + 10 s')
    F.tick(env, c, 120, 33)
    check(F.callCount(c, 'ChangeLifeState:sol_a1:' .. LIFE.DEAD) + F.callCount(c, 'ChangeLifeState:sol_a2:' .. LIFE.DEAD) +
        F.callCount(c, 'ChangeLifeState:sol_a3:' .. LIFE.DEAD) + F.callCount(c, 'ChangeLifeState:sol_q1:' .. LIFE.DEAD) +
        F.callCount(c, 'ChangeLifeState:sol_q2:' .. LIFE.DEAD) == 1 and has(c.logs, 'event=probe;id=kill_near;step=done;result=ok'),
        'kill_near at t0 + 10 s')
    -- race_kill: the host sends PROBE_SYNC; both fire on PROBE_FIRE; the guest attributes its own kill.
    local envh, h = fixture({role = 1})
    F.start(envh, h, function() h.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(envh, h, 3)
    F.push(h, 0, OP.PROBE, 7, 0)
    F.tick(envh, h, 400, 33)
    local ps = F.tx(h, OP.PROBE_SYNC)[1]
    check(ps and ps.v[1] == 7 and ps.v[2] == 1 and ps.v[4] == 3000 and has(h.logs, 'event=probe;id=race_kill;step=start'),
        'race_kill: PROBE_SYNC{7, 1, D, 3000}')
    local d = h.names[0] and nil
    for name, id in pairs(h.ids) do if u32(name) == ps.v[3] then d = name end end
    check(d and h.W.ent[ps.v[3]], 'D is a mapped soldier')
    F.push(h, 0, OP.PROBE_FIRE, 7, 1, ps.v[3], 3000)
    F.tick(envh, h, 1)
    check(F.callCount(h, 'ChangeLifeState:' .. d .. ':' .. LIFE.DEAD) == 1 and has(h.logs, 'event=probe;id=race_kill;step=fire;ent='),
        'host fires')
    local envg, g = fixture({role = 2})
    F.start(envg, g, function() g.enemy.UpdateOutOfAreaSetting(1) end)
    F.pair(envg, g, 3)
    F.clear(g)
    F.push(g, 0, OP.PROBE_FIRE, 7, 1, ps.v[3], 3000)
    F.tick(envg, g, 1)
    F.msg(envg, 'GameObject', 'Dead', g.ids[d], NULL_ID, 0)
    F.tick(envg, g, 1)
    check(F.callCount(g, 'ChangeLifeState:' .. d .. ':' .. LIFE.DEAD) == 1 and F.tx(g, OP.REQ_OUTCOME)[1] and
        F.tx(g, OP.REQ_OUTCOME)[1].v[4] == 5, 'guest fires and its kill is attributed (override)')
    -- Refusals.
    F.push(g, 0, OP.PROBE, 7, 0)
    F.tick(envg, g, 400, 33)
    check(has(g.logs, 'event=probe;id=race_kill;step=done;result=refused;reason=role'), 'race_kill refused on the guest')
    local envp, p = fixture({role = 1, mode = 2, probe = 9})
    F.start(envp, p)
    F.pair(envp, p, 3)
    F.tick(envp, p, 400, 33)
    check(has(p.logs, 'event=probe;id=kill_near;step=done;result=refused;reason=mode') and #p.calls == 0, 'probes refused in play')
    -- p12 is read-only.
    local env12, c12 = fixture({role = 1, probe = 5})
    F.start(env12, c12)
    F.tick(env12, c12, 400, 33)
    check(has(c12.logs, 'event=probe;id=p12;step=result;is_host=true;sync=true,true,false,true') and #c12.calls == 0, 'p12 read-only')
    -- p3: the constants line, FAINT with its constant, the reads.
    local env3, c3 = fixture({role = 1, probe = 2})
    F.start(env3, c3, function() c3.enemy.UpdateOutOfAreaSetting(1) end)
    F.tick(env3, c3, 600, 33)
    check(has(c3.logs, 'event=probe;id=p3;step=consts;dead=3;dying=1;sleep=4;faint=2;ls_normal=0'), 'p3 constants line')
    check(count(c3.logs, 'event=probe;id=p3;step=read;ms=') == 3 and has(c3.logs, ';life=2;status=0'), 'p3 FAINT reads')
end

-- 16. Errors and originals.
do
    local env, c = fixture({role = 1})
    F.start(env, c)
    F.pair(env, c, 3)
    c.W.enemy.tick = function() error('boom') end
    F.tick(env, c, 3)
    check(count(c.logs, 'event=error;msg=') == 1 and has(c.logs, 'boom') and not c.W.active and not c.W.observing,
        'one error line, inactive afterwards')
    local before = #c.receipts
    F.tick(env, c, 3)
    F.msg(env, 'GameObject', 'Dead', c.ids.sol_a1, 0, 5)
    check(#c.receipts == before, 'no receipts after the error')
    F.clear(c)
    F.call(env, 'OnTerminate')
    check(F.tx(c, OP.MISSION_END)[1] and has(c.logs, 'event=stop'), 'stop cleanup still runs')
    for _, at in ipairs({'OnMissionGameStart', 'OnUpdate', 'OnTerminate', 'OnInitialize'}) do
        local e2, c2 = fixture({errorAt = at})
        if at ~= 'OnMissionGameStart' and at ~= 'OnInitialize' then F.start(e2, c2) end
        local ok, err = pcall(e2.e[at], 7, nil, 9)
        check(not ok and err == sentinel, 'original error propagates: ' .. at)
        local e3, c3 = fixture({yieldAt = at})
        if at ~= 'OnMissionGameStart' and at ~= 'OnInitialize' then F.start(e3, c3) end
        local co = coroutine.create(e3.e[at])
        local okY, value = coroutine.resume(co, 7, nil, 9)
        check(okY and value == 'original yield', 'original yield passes: ' .. at)
        local okR, a, b, d = coroutine.resume(co)
        check(okR and a == 1 and b == nil and d == 3, 'returns after the yield: ' .. at)
    end
    local e4, c4 = fixture({errorAt = 'OnMessage'})
    F.start(e4, c4)
    local ok, err = pcall(F.msg, e4, 'GameObject', 'Dead', 1, 0, 0)
    check(not ok and err == sentinel, 'OnMessage error propagates')
    -- Bad constants and missing bindings switch W1 off (logged), the rest keeps running.
    local e5, c5 = fixture({consts = {NPC_LIFE_STATE_FAINT = 7}})
    F.start(e5, c5)
    check(has(c5.logs, 'event=inactive;reason=bad_const;dead=3;dying=1;sleep=4;faint=7') and not c5.W.w1 and c5.W.active,
        'FAINT=7 switches W1 off')
    local e6, c6 = fixture({missing = {'TppEnemy.GetLifeStatus'}})
    F.start(e6, c6)
    check(not c6.W.w1 and c6.W.active and has(c6.logs, 'event=missing;names=TppEnemy.GetLifeStatus'), 'a missing W1 binding')
    local e7, c7 = fixture({missing = {'Fox'}})
    F.start(e7, c7)
    check(not c7.W.active and has(c7.logs, 'event=inactive;reason=missing_required;names=Fox.StrCode32'), 'a missing required binding')
end

-- The real world wrapper must retry owned downed cleanup after it becomes
-- inactive, while leaving RX, simulation and other stop hooks alone.
do
    local coopDir = assert(FILES[1]:match('^(.*[/\\])'), 'world chunk directory')
    for _, reason in ipairs({'error', 'level0'}) do
        local env, c = fixture({role = 2})
        local n = {down = false, generation = 0, cause = 0}
        local stops, resets, hooks, polls = 0, 0, 0, 0
        local refusingReset = true
        local previousLoad = env.loadstring
        env.loadstring = function(source)
            if starts(source, "return 'MGS_COOP_DOWNED_HUD_V1'") or starts(source, "return 'MGS_COOP_DOWNED_HUD_V2'") or starts(source, "return 'MGS_COOP_DOWNED_HUD_V3'") then
                c.receipts[#c.receipts + 1] = source
                return function() return true end
            end
            if starts(source, "return 'MGS_COOP_DOWNED_V1'") then
                c.receipts[#c.receipts + 1] = source
                local m, ep, command = source:match('%-%- (%d+) (%d+) (%d+)')
                check(m == '30010' and ep == '5', 'cleanup retains its actual mission and epoch')
                return function()
                    command = tonumber(command)
                    if command == 3 then stops = stops + 1; n.down, n.cause = false, 0
                    else polls = polls + 1 end
                    return {ready = true, down = n.down, held = false, applied = true,
                        generation = n.generation, cause = n.cause, pos = {0, 0, 0}}
                end
            end
            return previousLoad(source)
        end
        env.Tpp.MgsCoop = {playerPackageSelected = true}
        env.PlayerInfo = {GetLocalPlayerIndex = function() return 0 end}
        env.Player = {
            IsInstanceValid = function() return true end,
            SetPadMask = function() c.mask = true end,
            ResetPadMask = function()
                resets = resets + 1
                if refusingReset then error('temporary reset refusal') end
                c.mask = false
            end,
        }
        local downedChunk = assert(loadfile(coopDir .. 'world_downed.lua'))
        setfenv(downedChunk, env); downedChunk()
        local sleepChunk = assert(loadfile(coopDir .. 'world_partner_sleep.lua'))
        setfenv(sleepChunk, env); sleepChunk()
        F.start(env, c); F.pair(env, c, 3)
        n.down, n.generation, n.cause = true, 1, 2
        F.tick(env, c, 1)
        check(c.mask and c.W.downed.maskOwned and c.W.downed.active, 'composed downed policy owns a confirmed crawling lock')
        c.W.stopHooks[#c.W.stopHooks + 1] = function() hooks = hooks + 1 end
        F.clear(c)
        if reason == 'error' then
            c.W.enemy.tick = function() error('downed world failure') end
            F.tick(env, c, 1)
            check(c.W.failed and count(c.logs, 'event=error;msg=') == 1, 'failed natural update invokes immediate owner cleanup')
        else
            F.push(c, 0, OP.LEVEL, 0); F.tick(env, c, 1)
        end
        check(not c.W.active and not c.W.downed.active and c.mask and c.W.downed.maskOwned and
            stops == 1 and hooks == 1, reason .. ': failed reset retains only owned pending cleanup')
        check(F.callCount(c, 'Pause:false') == 1, reason .. ': environmental cleanup runs immediately')
        local beforePolls, beforeRx = polls, #c.rx
        F.push(c, 0, OP.LINK, 1)
        F.clear(c)
        local beforeUpdates = #c.order
        F.tick(env, c, 2)
        check(c.mask and c.W.downed.maskOwned and resets >= 3 and stops == 1 and hooks == 1,
            reason .. ': inactive updates retry only pending named-mask release')
        check(polls == beforePolls and #c.rx == beforeRx + 1 and #c.tx == 0 and #c.calls == 0 and
            #c.order == beforeUpdates + 2, reason .. ': cleanup preserves originals without RX, simulation or repeated stop hooks')
        refusingReset = false
        F.tick(env, c, 1)
        check(not c.mask and not c.W.downed.maskOwned and stops == 1 and hooks == 1,
            reason .. ': next owner update finishes a previously refused mask release')
        local beforeReceipts, beforeResets = #c.receipts, resets
        F.tick(env, c, 3)
        check(#c.receipts == beforeReceipts and resets == beforeResets and #c.rx == beforeRx + 1,
            reason .. ': completed inactive cleanup is inert')
    end
end
-- The world and debug chunks inside the composed TppMain (exact splice;
-- inert with literal native receipts).
local composedRuns = 0
if arg[8] then
    local function slurp(path)
        local file = assert(io.open(path, 'rb'))
        local text = file:read('*a')
        file:close()
        return (text:gsub('\r\n', '\n'))
    end
    local composed = slurp(arg[8])
    local at = 1
    local partnerEnd = composed:find('\n' .. slurp(FILES[1]) .. '\n', 1, true)
    check(partnerEnd and composed:find('-- MGS co-op offline partner spike (original).', 1, true) < partnerEnd,
        'world chunks after the partner chunk')
    for i = 1, 7 do
        local spliced = '\n' .. slurp(FILES[i]) .. '\n'
        local found = composed:find(spliced, at, true)
        check(found ~= nil and composed:find(spliced, found + 1, true) == nil, 'chunk ' .. i .. ' spliced once, byte-identical')
        check(i == 1 or found == at, 'chunk ' .. i .. ' directly follows the previous one')
        at = found + #spliced
    end
    for i = 9, #arg do
        local spliced = '\n' .. slurp(arg[i]) .. '\n'
        check(composed:find(spliced, at, true) == at and not composed:find(spliced, at + 1, true), 'extension chunk spliced once')
        at = at + #spliced
    end
    local coopDir = assert(FILES[1]:match('^(.*[/\\])'), 'world chunk directory')
    for _, name in ipairs({'debug_menu.lua', 'debug_equipment.lua', 'debug_enemy.lua', 'debug_player.lua', 'debug_spawn.lua', 'debug_buddy.lua', 'debug_vehicle.lua'}) do
        local spliced = '\n' .. slurp(coopDir .. name) .. '\n'
        check(composed:find(spliced, at, true) == at and not composed:find(spliced, at + 1, true),
            name .. ' spliced once, byte-identical, directly after the previous chunk')
        at = at + #spliced
    end
    check(composed:find('-- Adapted from Dynamite TppMain marker-loading lifecycle', at, true) == at, 'then the marker lifecycle chunk')
    local function run(disableDebug)
        local r = {receipts = {}, calls = {}}
        local function record(text) r.calls[#r.calls + 1] = text end
        local function stub(name)
            local t = {}
            return setmetatable(t, {
                __index = function(_, k)
                    if type(k) ~= 'string' then return nil end
                    local child = stub(name .. '.' .. k)
                    rawset(t, k, child)
                    return child
                end,
                __call = function() record(name) end,
            })
        end
        local env = {
            vars = {missionCode = 30010, playerPosX = 0, playerPosY = 0, playerPosZ = 0, playerRotY = 0, playerCameraRotation = {[1] = 0}},
            Tpp = setmetatable({IsTypeFunc = function(v) return type(v) == 'function' end,
                IsTypeTable = function(v) return type(v) == 'table' end, MgsCoop = {}},
                {__index = function(t, k) local v = stub('Tpp.' .. k); rawset(t, k, v); return v end}),
        }
        env.loadstring = function(source)
            r.receipts[#r.receipts + 1] = source
            return loadstring(source)
        end
        setmetatable(env, {__index = function(t, k)
            local v = _G[k]
            if v ~= nil then return v end
            v = stub(k); rawset(t, k, v); return v
        end})
        local chunk = assert(loadfile(arg[8]))
        setfenv(chunk, env)
        local mod = chunk()
        check(type(mod) == 'table' and type(mod.MgsCoopWorld) == 'table', 'composed module publishes e.MgsCoopWorld')
        check(type(mod.MgsCoopWorld.downed) == 'table' and
            type(mod.MgsCoopWorld.downed.partnerSleepPublish) == 'function' and
            type(mod.MgsCoopWorld.downed.partnerSleepClear) == 'function',
            'actual composed TppMain includes passive accepted-peer sleep adapter after downed')
        check(type(mod.MgsCoopDebug) == 'table' and type(mod.MgsCoopDebug.equipment) == 'table',
            'composed module publishes debug core and equipment')
        if disableDebug then
            -- Execute the same composition with only its optional debug
            -- callbacks inert, to compare the original game's call trace.
            mod.MgsCoopDebug.update, mod.MgsCoopDebug.stop = function() end, function() end
        end
        local mission = {sequence = {OnTerminate = function() record('sequence.OnTerminate') end}}
        local ok, err = pcall(mod.OnMissionGameStart, mission)
        check(ok, 'composed OnMissionGameStart: ' .. tostring(err))
        for _ = 1, 3 do
            ok, err = pcall(mod.OnUpdate, mission)
            check(ok, 'composed OnUpdate: ' .. tostring(err))
        end
        ok, err = pcall(mod.OnTerminate, mission)
        check(ok, 'composed OnTerminate: ' .. tostring(err))
        composedRuns = composedRuns + 1
        return r
    end
    local r = run()
    local world = {}
    for _, s in ipairs(r.receipts) do if starts(s, PRE) then world[#world + 1] = s end end
    check(#world == 1 and world[1] == PRE .. "CFG_V1' -- 30010 1", 'literal CFG in the composition: only the start receipt (' ..
        #world .. ')')
    local terminated = false
    for _, v in ipairs(r.calls) do if v == 'sequence.OnTerminate' then terminated = true end end
    check(terminated, 'the original OnTerminate ran')
    local baseline = run(true)
    check(#r.calls == #baseline.calls, 'literal debug receipts add no game-binding calls')
    for i, name in ipairs(r.calls) do
        check(name == baseline.calls[i], 'literal debug receipts preserve game-binding call order: ' .. i)
    end
    local debugCmd, debugState = 0, 0
    local baselineWorld = {}
    for _, s in ipairs(r.receipts) do
        if s == "return 'MGS_COOP_DEBUG_CMD_V1'" then debugCmd = debugCmd + 1
        elseif starts(s, "return 'MGS_COOP_DEBUG_STATE_V1' -- ") then debugState = debugState + 1
        elseif starts(s, "return 'MGS_COOP_DEBUG_") then check(false, 'idle debug composition emitted an action receipt') end
    end
    for _, s in ipairs(baseline.receipts) do if starts(s, PRE) then baselineWorld[#baselineWorld + 1] = s end end
    -- STATE_V1 is rate limited: the first idle update publishes it once, then cleanup reports it.
    check(debugCmd == 3 and debugState == 2, 'idle debug composition only polls, publishes state once and reports cleanup state')
    check(#baselineWorld == #world and baselineWorld[1] == world[1], 'idle debug preserves literal world CFG receipt')
end

print(checks .. ' world sync Lua checks passed: inert literal CFG (2 loads per mission), receipts < 256 bytes, %.0f hashes, ' ..
    'observer after the original (RESEND skipped), roster digest (ipairs only), host commits / echo / sweep, guest ' ..
    'attribution and fultons, REJECT/known/timeouts, ledger and area union, alert without latch, PHASE_VEC, digests and ' ..
    'resync, clock and weather, lab goto, probes, error policy' ..
    (composedRuns > 0 and ('; composed TppMain (' .. composedRuns .. ' run)') or '') .. '; no game calls')
