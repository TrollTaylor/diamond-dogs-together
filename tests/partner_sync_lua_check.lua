-- Exercises lua/coop/partner_sync.lua with simulated native receipts and
-- recording game-binding mocks. No MGSV, Steam, network or real session.
-- Usage: lua51 partner_sync_lua_check.lua lua/coop/partner_sync.lua [composed TppMain-host-trial.lua]
local checks = 0
local function check(value, why)
    checks = checks + 1
    assert(value, tostring(checks) .. ': ' .. (why or 'check failed'))
end
local CFG = "return 'MGS_COOP_PARTNER_CFG_V1'"
local STOP = "return 'MGS_COOP_PARTNER_STOP_V1'"
local TICK = "return 'MGS_COOP_PARTNER_V1' -- "
local LOG = "return 'MGS_COOP_PARTNER_LOG_V1' -- "
local ACTION_CFG = "return 'MGS_COOP_ACTION_EVENTS_CFG_V1'"
local ACTION_CLEAR = "return 'MGS_COOP_ACTION_EVENTS_CLEAR_V1'"
local ACTION_TICK = "return 'MGS_COOP_PARTNER_ACTIONS_V1' -- "
local sentinel = {}
local STATUS = {STAND = 4, SQUAT = 5, CRAWL = 6, WALK = 13, RUN = 14, DASH = 15,
    GUN_FIRE = 7, GUN_FIRE_SUPPRESSOR = 8, ON_HELICOPTER = 16, BINOCLE = 17, SUBJECT = 18, DEAD = 19}
local bootstrapPath = (arg[0]:match('^(.*[/\\])') or '') .. '../lua/coop/bootstrap.lua'
local function newCoop(mission, selected)
    local bootstrapEnv = {e = {}, loadstring = function() return function() end end}
    setmetatable(bootstrapEnv, {__index = _G})
    local chunk = assert(loadfile(bootstrapPath)); setfenv(chunk, bootstrapEnv); chunk()
    local coop = bootstrapEnv.e.MgsCoop
    coop.playerPackageSelected, coop.playerPackageMission, coop.archiveDirectReadSucceeded = selected ~= false, mission, true
    return coop
end

local function starts(text, prefix) return text:sub(1, #prefix) == prefix end
local function remove(env, path)
    local name, key = path:match('^(%w+)%.?(%w*)$')
    if key == '' then env[name] = nil else env[name][key] = nil end
end

-- options: missing = {paths}, errorAt/yieldAt = original callback name,
-- cfg = reply of the CFG receipt (default true), compileFail, cfgThrow.
local function fixture(options)
    options = options or {}
    local c = {cfg = true, reply = nil, receipts = {}, ticks = {}, logs = {}, stops = 0, calls = {}, warps = {},
               status = {}, valid = true, localIndex = 0, order = {}, now = 100}
    c.actionCfg, c.actionQueries, c.actionClears, c.actions, c.actionAccepted = options.actionCfg, 0, 0, {}, true
    if options.cfg ~= nil then c.cfg = options.cfg end
    local env = {e = {}, Tpp = {MgsCoop = newCoop(options.mission or 30050, options.selected)},
        vars = {missionCode = options.mission or 30050, playerPosX = 1.5, playerPosY = 0.25, playerPosZ = -3.0,
        playerRotY = 45.5, playerCameraRotation = {[0] = 10, [1] = 90}, playerType = 0, playerPartsType = 1,
        playerCamoType = 2, playerHandType = 3, playerFaceId = 4}}
    env.TppDefine = {WEAPONSLOT = {PRIMARY_HIP = 0, PRIMARY_BACK = 1, SECONDARY = 2}}
    env.PlayerPad = {HOLD = 256, RELOAD = 512}
    env.PlayerVars = {scannedButtonsDirect = 0}
    env._testClock = c
    env.Time = {GetRawElapsedTimeSinceStartUp = function()
        if c.clockThrow then error('clock unavailable') end
        if c.clockOverride then return c.clockValue end
        return c.now
    end}
    env.bit = {band = function(a, b)
        local value, place = 0, 1
        while a > 0 or b > 0 do
            if a % 2 == 1 and b % 2 == 1 then value = value + place end
            a, b, place = math.floor(a / 2), math.floor(b / 2), place * 2
        end
        return value
    end}
    for _, name in ipairs({'OnMissionGameStart', 'OnUpdate', 'OnTerminate'}) do
        env.e[name] = function(...)
            check(select('#', ...) == 3, 'original argument count')
            check(select(1, ...) == 7 and select(2, ...) == nil and select(3, ...) == 9, 'original arguments')
            c.order[#c.order + 1] = name
            if options.errorAt == name then error(sentinel) end
            if options.yieldAt == name then coroutine.yield('original yield') end
            return 1, nil, 3
        end
    end
    env.loadstring = function(source)
        check(type(source) == 'string' and #source <= (starts(source, ACTION_TICK) and 512 or 200), 'bounded receipt: ' .. tostring(source))
        c.receipts[#c.receipts + 1] = source
        if options.compileFail then return nil, 'fixture compile failure' end
        if source == CFG then
            return function()
                if options.cfgThrow then error(sentinel) end
                return c.cfg
            end
        end
        if source == ACTION_CFG then
            c.actionQueries = c.actionQueries + 1
            return function() return c.actionCfg end
        end
        if source == ACTION_CLEAR then
            c.actionClears = c.actionClears + 1; c.actionCache = nil
            return function() return true end
        end
        if starts(source, ACTION_TICK) then
            c.actions[#c.actions+1] = source
            return function()
                c.actionCache = c.actionAccepted and source or nil
                return c.actionAccepted
            end
        end
        if source == STOP then
            c.stops = c.stops + 1
            c.order[#c.order + 1] = 'STOP'
            return function() return true end
        end
        if starts(source, TICK) then
            c.ticks[#c.ticks + 1] = source:sub(#TICK + 1)
            return function()
                if c.reply then return unpack(c.reply, 1, 14) end
                return 'MGS_COOP_PARTNER_V1'
            end
        end
        if starts(source, LOG) then
            c.logs[#c.logs + 1] = source:sub(#LOG + 1)
            return function() return true end
        end
        error('unexpected receipt ' .. source)
    end
    local function record(text) c.calls[#c.calls + 1] = text; c.order[#c.order + 1] = text end
    env.Player = {
        IsInstanceValid = function() if c.validThrow then error('valid failed') end return c.valid end,
        SetAllInstanceActiveMode = function(on) record('allactive:' .. tostring(on)) end,
        CopyPartsInfoFromLocalPlayer = function(t) record('copy:' .. tostring(t.instanceIndex)) end,
        SetPartsInfoAtInstanceIndex = function(t)
            record(string.format('parts:%s:%s:%s:%s:%s:%s', tostring(t.instanceIndex), tostring(t.type), tostring(t.partsType),
                tostring(t.camoType), tostring(t.handType), tostring(t.faceId)))
        end,
        SetOtherInstanceIsForMirrorMode = function(on) record('mirror:' .. tostring(on)) end,
        RequestToPlayDirectMotion = function(t)
            check(type(t) == 'table' and type(t[1]) == 'table' and t[1][1] == 'coopLoco' and type(t[2]) == 'table' and
                type(t[2][2]) == 'boolean' and t[2][3] == '' and t[2][6] == true, 'dmPlay arg shape')
            c.dmLoop = t[2][2]
            record('dmplay:' .. tostring(t[1][2]) .. ':' .. tostring(t[2][1]))
        end,
        RequestToStopDirectMotion = function() record('dmstop') end,
    }
    env.PlayerInfo = {
        GetLocalPlayerIndex = function() return c.localIndex end,
        OrCheckStatus = function(t)
            check(type(t) == 'table' and #t == 1, 'OrCheckStatus takes a one-entry table')
            return c.status[t[1]] == true
        end,
    }
    env.PlayerStatus = {}
    for k, v in pairs(STATUS) do env.PlayerStatus[k] = v end
    env.GameObject = {SendCommand = function(id, command)
        if c.sendThrow then error('send failed') end
        c.warps[#c.warps + 1] = {id = id, command = command}
    end}
    for _, path in ipairs(options.missing or {}) do remove(env, path) end
    if options.strictPlayer then
        -- A binding table that raises on unknown keys (like a strict userdata).
        local inner = env.Player
        inner.SetAllInstanceActiveMode = nil
        env.Player = setmetatable({}, {__index = function(_, k)
            local v = inner[k]
            if v == nil then error('no such binding ' .. tostring(k)) end
            return v
        end})
    end
    setmetatable(env, {__index = _G})
    if options.actions then
        local actionPath = (arg[0]:match('^(.*[/\\])') or '') .. '../lua/coop/partner_actions.lua'
        local actions = assert(loadfile(actionPath)); setfenv(actions, env); actions()
    end
    local chunk = assert(loadfile(arg[1])); setfenv(chunk, env); chunk()
    return env, c
end
local function call(env, name)
    local function returns(...)
        check(select('#', ...) == 3, 'original return count')
        check(select(1, ...) == 1 and select(2, ...) == nil and select(3, ...) == 3, 'original return values')
    end
    returns(env.e[name](7, nil, 9))
end
local function update(env, count, dt)
    for _ = 1, count or 1 do
        env._testClock.now = env._testClock.now + (dt or 1 / 60)
        call(env, 'OnUpdate')
    end
end
local function count(list, value) local n = 0 for _, v in ipairs(list) do if v == value then n = n + 1 end end return n end
local function hasLog(c, text) for _, v in ipairs(c.logs) do if v == text then return true end end return false end
local function lastTick(c) return c.ticks[#c.ticks] end
local function fields(tick)
    local values = {}
    for v in tick:gmatch('%S+') do values[#values + 1] = v end
    return values
end

-- 1. Inactive unless the CFG receipt answers true.
for _, reply in ipairs({false, 'MGS_COOP_PARTNER_CFG_V1', 1, 'true', 'nil'}) do
    local env, c = fixture({cfg = reply})
    if reply == 'nil' then c.cfg = nil end
    update(env, 3)
    check(#c.receipts == 0, 'nothing before mission start')
    call(env, 'OnMissionGameStart')
    check(#c.receipts == 1 and c.receipts[1] == CFG, 'one CFG receipt at start')
    update(env, 10)
    check(#c.ticks == 0 and #c.calls == 0 and #c.warps == 0 and #c.logs == 0, 'inactive: no ticks, calls or notes')
    call(env, 'OnTerminate')
    check(c.stops == 0, 'inactive: no STOP')
end
do
    local env, c = fixture()
    env.vars.missionCode = 10010
    call(env, 'OnMissionGameStart'); update(env, 5); call(env, 'OnTerminate')
    check(#c.receipts == 0, 'unsupported mission never asks for CFG')
    env, c = fixture({compileFail = true})
    call(env, 'OnMissionGameStart'); update(env, 5)
    check(#c.ticks == 0 and #c.calls == 0, 'CFG compile failure keeps it inactive')
    env, c = fixture({cfgThrow = true})
    call(env, 'OnMissionGameStart'); update(env, 5)
    check(#c.ticks == 0 and #c.logs == 1 and starts(c.logs[1], 'event=error;msg='), 'CFG error contained and noted')
end
-- The real bootstrap whitelist also requires the current normal pack latch.
-- Afghanistan's 30011 demo can retain code 30010, so a whitelist alone fails.
for _, mission in ipairs({30010, 30020, 30050}) do
    local env, c = fixture({mission = mission})
    call(env, 'OnMissionGameStart')
    c.reply = {3, 1, 2000, 0, -1000, 1571, 1, 0}
    update(env, 2); call(env, 'OnTerminate')
    check(#c.ticks == 2 and #c.warps == 2 and c.stops == 1, 'normal free roam partner runs: ' .. mission)
    env, c = fixture({mission = mission, selected = false})
    call(env, 'OnMissionGameStart'); update(env); call(env, 'OnTerminate')
    check(#c.receipts == 0 and #c.calls == 0, 'unselected/variant pack stays inert: ' .. mission)
end
for _, mission in ipairs({30011, 10010, 10036, 40010, 40020, 40050, 50050}) do
    local env, c = fixture({mission = mission})
    call(env, 'OnMissionGameStart'); update(env); call(env, 'OnTerminate')
    check(#c.receipts == 0 and #c.calls == 0, 'excluded partner body mission: ' .. mission)
end
for _, missing in ipairs({'Tpp', 'Tpp.MgsCoop'}) do
    local env, c = fixture({missing = {missing}})
    call(env, 'OnMissionGameStart'); update(env); call(env, 'OnTerminate')
    check(#c.receipts == 0 and #c.calls == 0, 'missing capability owner stays inert: ' .. missing)
end
do
    local env, c = fixture()
    env.Tpp.MgsCoop.IsPartnerBodyMission = nil
    call(env, 'OnMissionGameStart'); update(env); call(env, 'OnTerminate')
    check(#c.receipts == 0 and #c.calls == 0, 'missing capability function stays inert')
end

-- 2. Active: one quiet tick receipt per frame with 11 strict integers.
local env, c = fixture()
call(env, 'OnMissionGameStart')
check(hasLog(c, 'event=started') and #c.logs == 1, 'started noted, nothing missing')
update(env)
check(#c.ticks == 1 and lastTick(c) == '1 1500 250 -3000 4550 9000 0 0 0 0 0', 'tick fields: ' .. tostring(lastTick(c)))
c.status[STATUS.CRAWL], c.status[STATUS.DASH] = true, true
update(env)
check(lastTick(c) == '2 1500 250 -3000 4550 9000 2 3 0 0 0', 'crawl + dash')
c.status = {[STATUS.SQUAT] = true, [STATUS.RUN] = true}
update(env)
check(fields(lastTick(c))[7] == '1' and fields(lastTick(c))[8] == '2', 'squat + run')
c.status = {[STATUS.WALK] = true}
env.vars.playerPosX, env.vars.playerRotY = -0.0004, -179.994
update(env)
check(lastTick(c) == '4 0 250 -3000 -17999 9000 0 1 0 0 0', 'walk, rounding, negative yaw: ' .. lastTick(c))
env.vars.playerCameraRotation = nil
update(env)
check(fields(lastTick(c))[6] == '0', 'missing camera rotation reads 0')
env.vars.playerCameraRotation = setmetatable({}, {__index = function() error('bad index') end})
update(env)
check(fields(lastTick(c))[6] == '0' and #c.logs == 1, 'raising camera rotation reads 0 without stopping')
env.vars.playerPosX = 0 / 0
local before = #c.ticks
update(env)
check(#c.ticks == before, 'non-finite position: no tick')
env.vars.playerPosX = 2e6
update(env)
check(#c.ticks == before, 'position beyond 1e6 m: no tick (keeps %d within 32 bits)')
env.vars.playerPosX, env.vars.playerRotY = 1.5, 1e5
update(env)
check(#c.ticks == before, 'yaw beyond 36000 degrees: no tick')
env.vars.playerRotY = 45.5
env.vars.playerCameraRotation = {[1] = 1e9}
update(env)
check(#c.ticks == before + 1 and fields(lastTick(c))[6] == '0', 'out-of-range camera reads 0')
before = #c.ticks
c.valid = false
update(env)
check(#c.ticks == before, 'invalid instance: no tick')
c.valid = true
env.vars.missionCode = 10010
update(env)
check(#c.ticks == before, 'other mission: no tick')
env.vars.missionCode = 30050
update(env)
check(#c.ticks == before + 1 and #c.calls == 0, 'literal reply (not replaced): nothing done')

-- Gun producer observes actual fire and public aim/reload input requests.
do
    local a, r = fixture(); call(a, 'OnMissionGameStart')
    a.vars.currentInventorySlot, a.vars.weapons = 1, {[0] = 10101, [1] = 30103, [2] = 10004}
    a.PlayerVars.scannedButtonsDirect = 256 + 512; r.status[STATUS.GUN_FIRE] = true; update(a)
    check(fields(lastTick(r))[10] == '11' and fields(lastTick(r))[11] == '30103', 'gun flags and active back equipment')
    check(#r.calls == 0 and #r.warps == 0, 'producer sends observations only')
    a.PlayerVars.scannedButtonsDirect = 0; r.status = {}; update(a, 1, 0.09)
    check(fields(lastTick(r))[10] == '10', 'fire and reload retained after 90ms')
    update(a, 1, 0.02); check(fields(lastTick(r))[10] == '8', 'fire expires after 100ms wall time')
    update(a, 1, 0.19); check(fields(lastTick(r))[10] == '8', 'reload request retained at 300ms')
    update(a, 1, 0.04); check(fields(lastTick(r))[10] == '0', 'reload request expires after one third second')
    r.status = {[STATUS.SUBJECT] = true}; update(a)
    check(fields(lastTick(r))[10] == '0', 'subject alone never invents aiming')
    r.status = {[STATUS.GUN_FIRE_SUPPRESSOR] = true}; update(a)
    check(fields(lastTick(r))[10] == '2', 'suppressed firing uses executed status')
    a.vars.currentInventorySlot = 2; r.status = {}; update(a)
    check(fields(lastTick(r))[10] == '0' and fields(lastTick(r))[11] == '10004', 'weapon switch clears previous gun pulse')
    a.PlayerVars.scannedButtonsDirect = 256 + 512; r.status = {[STATUS.BINOCLE] = true}; update(a)
    check(fields(lastTick(r))[10] == '0', 'binocular mode refuses gun input interpretation')
    r.status = {[STATUS.ON_HELICOPTER] = true}; update(a)
    check(fields(lastTick(r))[10] == '0', 'helicopter input never produces gun actions')
    r.status = {}; a.vars.currentInventorySlot = 3; update(a)
    check(fields(lastTick(r))[10] == '0' and fields(lastTick(r))[11] == '0', 'item slot refused')
    a.vars.currentInventorySlot = 2; a.vars.weapons[2] = 65536; update(a)
    check(fields(lastTick(r))[10] == '0' and fields(lastTick(r))[11] == '0', 'invalid gun ID refused')
    a.vars.weapons[2] = 10004; r.status = {[STATUS.GUN_FIRE] = true}; update(a)
    r.status = {}; a.PlayerVars.scannedButtonsDirect = 0; r.valid = false; update(a); r.valid = true; update(a)
    check(fields(lastTick(r))[10] == '0', 'invalid-instance gap clears retained actions')
    r.status = {[STATUS.GUN_FIRE] = true}; update(a); r.status = {}; call(a, 'OnTerminate'); call(a, 'OnMissionGameStart'); update(a)
    check(fields(lastTick(r))[10] == '0', 'new mission never retains prior firing')
    for _, missing in ipairs({'PlayerPad', 'bit', 'PlayerVars'}) do
        local b, q = fixture({missing = {missing}}); call(b, 'OnMissionGameStart')
        b.vars.currentInventorySlot, b.vars.weapons = 0, {[0] = 10101}; q.status = {[STATUS.GUN_FIRE] = true}; update(b)
        check(fields(lastTick(q))[10] == '2', 'missing input source preserves executed fire only: ' .. missing)
    end
    a, r = fixture(); call(a, 'OnMissionGameStart')
    a.vars.currentInventorySlot, a.vars.weapons = 0, {[0] = 10101}
    a.PlayerVars.scannedButtonsDirect = {}; update(a)
    check(fields(lastTick(r))[10] == '0', 'unreadable button data yields no requested flags')
end

-- Natural owner updates and a separate 30Hz latest-pose consumer. A one-frame
-- event must survive different owner rates and sender phases, then expire.
do
    for _, fps in ipairs({20, 60, 120}) do
        for phase = 1, 3 do
            local a, r = fixture(); call(a, 'OnMissionGameStart')
            a.vars.currentInventorySlot, a.vars.weapons = 0, {[0] = 10101}
            local fireN, reloadN, fireLast, reloadLast, consumed = 0, 0
            for n = 0, 60 do
                local t = n / 120
                if n % (120 / fps) == 0 then
                    r.now = 100 + t
                    r.status = n == 0 and {[STATUS.GUN_FIRE] = true} or {}
                    a.PlayerVars.scannedButtonsDirect = n == 0 and 512 or 0
                    call(a, 'OnUpdate')
                end
                if n % 4 == phase then
                    consumed = tonumber(fields(lastTick(r))[10])
                    if math.floor(consumed / 2) % 2 == 1 then fireN, fireLast = fireN + 1, t end
                    if math.floor(consumed / 8) % 2 == 1 then reloadN, reloadLast = reloadN + 1, t end
                end
            end
            check(fireN >= 2 and fireLast >= 0.06 and fireLast < 0.1 + 1 / fps,
                fps .. 'Hz fire survives 30Hz phase ' .. phase .. ' with bounded wall-time duration')
            check(reloadN >= 9 and reloadLast >= 0.3 and reloadLast < 1 / 3 + 1 / fps,
                fps .. 'Hz reload intent survives 30Hz phase ' .. phase .. ' with bounded duration')
            check(consumed == 0, fps .. 'Hz latest-pose consumer observes both expirations')
        end
    end

    local a, r = fixture(); call(a, 'OnMissionGameStart')
    a.vars.currentInventorySlot, a.vars.weapons = 0, {[0] = 10101}
    r.status = {[STATUS.GUN_FIRE] = true}; a.PlayerVars.scannedButtonsDirect = 512; update(a)
    update(a, 1, 0.2) -- Still observed/requested: deadlines refresh from this observation.
    r.status = {}; a.PlayerVars.scannedButtonsDirect = 0; update(a, 1, 0.09)
    check(fields(lastTick(r))[10] == '10', 'continuous signals refresh deadlines from the latest observation')
    update(a, 1, 0.25)
    check(fields(lastTick(r))[10] == '0', 'refreshed actions still expire by wall time')
    r.status = {[STATUS.GUN_FIRE] = true}; a.PlayerVars.scannedButtonsDirect = 512; update(a)
    r.status = {}; a.PlayerVars.scannedButtonsDirect = 0; update(a, 1, 10)
    check(fields(lastTick(r))[10] == '0', 'long owner gap does not resume an old gun pulse')
    r.status = {[STATUS.GUN_FIRE] = true}; a.PlayerVars.scannedButtonsDirect = 512; update(a)
    r.status = {}; a.PlayerVars.scannedButtonsDirect = 0; r.now = 90; call(a, 'OnUpdate')
    check(fields(lastTick(r))[10] == '0', 'backward raw clock clears retained pulses')
    r.now = 90.05; call(a, 'OnUpdate')
    check(fields(lastTick(r))[10] == '0', 'clock reset cannot revive prior deadlines')
    r.status = {[STATUS.GUN_FIRE] = true}; r.now = 80; call(a, 'OnUpdate')
    r.status = {}; r.now = 80.05; call(a, 'OnUpdate')
    check(fields(lastTick(r))[10] == '2', 'new directly observed fire may latch against the reset clock')
    a.vars.missionCode = 10010; update(a); a.vars.missionCode = 30050; update(a)
    check(fields(lastTick(r))[10] == '0', 'unsupported mission gap clears retained action deadlines')

    for _, bad in ipairs({{value = 0 / 0}, {value = math.huge}, {value = -math.huge}, {value = -1},
        {value = 1e9}, {value = 1e15}, {value = '100'}, {value = {}}, {}, {throws = true}}) do
        local b, q = fixture(); call(b, 'OnMissionGameStart')
        b.vars.currentInventorySlot, b.vars.weapons = 0, {[0] = 10101}
        q.status = {[STATUS.GUN_FIRE] = true}; b.PlayerVars.scannedButtonsDirect = 512; update(b)
        q.clockOverride, q.clockValue, q.clockThrow = true, bad.value, bad.throws
        b.PlayerVars.scannedButtonsDirect = 256 + 512; update(b)
        check(fields(lastTick(q))[10] == '11', 'bad raw clock preserves only direct fire/aim/reload observations')
        q.status = {}; b.PlayerVars.scannedButtonsDirect = 0; update(b)
        check(fields(lastTick(q))[10] == '0', 'bad raw clock discards prior retained actions')
        q.clockOverride, q.clockThrow = false, false; update(b)
        check(fields(lastTick(q))[10] == '0', 'restored raw clock cannot revive cleared pulses')
    end
    for _, missing in ipairs({'Time', 'Time.GetRawElapsedTimeSinceStartUp'}) do
        local b, q = fixture({missing = {missing}}); call(b, 'OnMissionGameStart')
        b.vars.currentInventorySlot, b.vars.weapons = 0, {[0] = 10101}
        q.status = {[STATUS.GUN_FIRE] = true}; b.PlayerVars.scannedButtonsDirect = 256 + 512; update(b)
        check(fields(lastTick(q))[10] == '11', 'missing raw clock keeps direct observations: ' .. missing)
        q.status = {}; b.PlayerVars.scannedButtonsDirect = 0; update(b)
        check(fields(lastTick(q))[10] == '0', 'missing raw clock never falls back to frame-count retention')
    end
end

-- 3. op bits.
env, c = fixture(); call(env, 'OnMissionGameStart')
c.reply = {1, 1, 2000, 0, -1000, 1571, 0, 0}
update(env)
check(#c.warps == 1 and c.warps[1].id.type == 'TppPlayer2' and c.warps[1].id.index == 1, 'Warp addressed to TppPlayer2 index 1')
local warp = c.warps[1].command
check(warp.id == 'Warp' and warp.pos[1] == 2 and warp.pos[2] == 0 and warp.pos[3] == -1 and #warp.pos == 3 and
    math.abs(warp.rotY - 1.571) < 1e-9, 'Warp pos is a plain 3-number table in metres, rotY in radians')
check(#c.calls == 0, 'WARP alone calls nothing else')
c.reply = {2, 1, 0, 0, 0, 0, 1, 0}
update(env, 3)
check(count(c.calls, 'allactive:true') == 3 and count(c.logs, 'event=allactive_set') == 1 and #c.warps == 1,
    'SET_ALLACTIVE is idempotent: called for every op 2 (native re-asserts a lost bit), noted once, no warp')
check(fields(lastTick(c))[9] == '1', 'luaflags bit 0 reports all-active')
c.reply = {4, 1, 0, 0, 0, 0, 1, 0}
update(env, 2)
check(count(c.calls, 'copy:1') == 1 and hasLog(c, 'event=parts_copied;R=1'), 'COPY_PARTS once with instanceIndex 1')
check(fields(lastTick(c))[9] == '3', 'luaflags bit 1 reports parts copied')
c.reply = {10, 1, 0, 0, 0, 0, 1, 0}
local mark = #c.calls
update(env)
check(c.calls[mark + 1] == 'allactive:false' and c.calls[mark + 2] == 'allactive:true', 'CLEAR then SET in one tick')
c.reply = {8, 1, 0, 0, 0, 0, 1, 0}
update(env)
check(c.calls[#c.calls] == 'allactive:false' and hasLog(c, 'event=allactive_cleared'), 'CLEAR_ALLACTIVE')
update(env)
check(c.calls[#c.calls] == 'allactive:false' and count(c.calls, 'allactive:false') == 2, 'CLEAR only while all-active')
check(fields(lastTick(c))[9] == '2', 'luaflags after clear')
-- SET_PARTS (op 16): the 5 look values come from the native reply, never from vars.
c.reply = {16, 1, 0, 0, 0, 0, 1, 0, 6, 27, 116, 0, 0}
mark = #c.calls
update(env)
check(c.calls[mark + 1] == 'parts:1:6:27:116:0:0' and hasLog(c, 'event=parts_set;R=1;type=6;parts=27;camo=116;hand=0;face=0'),
    'SET_PARTS with the reply values (all 6 keys)')
check(fields(lastTick(c))[9] == '2', 'SET_PARTS reports parts copied (luaflags bit 1)')
c.reply = {16, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1, 65535}
update(env)
check(c.calls[#c.calls] == 'parts:1:0:1:0:1:65535', 'Snake look with the largest face id')
for _, bad in ipairs({{4, 0, 0, 0, 0}, {7, 0, 0, 0, 0}, {0, 28, 0, 0, 0}, {0, 1, 117, 0, 0}, {0, 1, 0, 8, 0}, {0, 1, 0, 1, 65536},
                      {0, 1.5, 0, 1, 0}, {-1, 1, 0, 1, 0}, {0, 1, 0, 1}, {'6', 27, 116, 0, 0}}) do
    mark = #c.calls
    local notes = count(c.logs, 'event=parts_set_refused;reason=bad_look')
    c.reply = {16, 1, 0, 0, 0, 0, 1, 0, bad[1], bad[2], bad[3], bad[4], bad[5]}
    update(env)
    check(#c.calls == mark and count(c.logs, 'event=parts_set_refused;reason=bad_look') == notes + 1,
        'bad look refused, nothing called: ' .. table.concat({tostring(bad[1]), tostring(bad[2]), tostring(bad[3]), tostring(bad[4]), tostring(bad[5])}, ','))
end
c.reply = {32, 1, 0, 0, 0, 0, 1, 0}
update(env)
check(c.calls[#c.calls] == 'mirror:true' and hasLog(c, 'event=mirror_on'), 'MIRROR_ON')
c.reply = {64, 1, 0, 0, 0, 0, 1, 0}
update(env)
check(c.calls[#c.calls] == 'mirror:false' and hasLog(c, 'event=mirror_off'), 'MIRROR_OFF (op 64)')
c.reply = {96, 1, 0, 0, 0, 0, 1, 0}
mark = #c.calls
update(env)
check(c.calls[mark + 1] == 'mirror:true' and c.calls[mark + 2] == 'mirror:false', 'MIRROR_ON is handled before MIRROR_OFF')
-- DM_PLAY (128): the 14th reply value is the gani index; {{"coopLoco",R},{GANI[dg],...}}.
c.reply = {128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3}
mark = #c.calls
update(env)
check(c.calls[mark + 1] == 'dmplay:1:/Assets/tpp/motion/SI_game/fani/bodies/snap/snapnon/snapnon_q_wk_lp_l.gani' and
    hasLog(c, 'event=dm_play;gani=3'), 'DM_PLAY with GANI[3] and coopLoco index R')
-- gani 8/9 are graph state names, not gani paths.
c.reply = {128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 9}
update(env)
check(c.calls[#c.calls] == 'dmplay:1:stateStandIdleLeft' and count(c.logs, 'event=dm_play;gani=9') == 1, 'DM_PLAY gani 9 state name')
-- dm=map: 10..12 are the stand walk, run and jog loops (exact resident paths).
for g, name in pairs({[10] = 'snapnon_s_wk_lp_vr1_l', [11] = 'snapnon_s_rn_lp_vr1_l', [12] = 'snapnon_s_jg_lp_vr1'}) do
    c.reply = {128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, g}
    update(env)
    check(c.calls[#c.calls] == 'dmplay:1:/Assets/tpp/motion/SI_game/fani/bodies/snap/snapnon/' .. name .. '.gani' and
        count(c.logs, 'event=dm_play;gani=' .. g) == 1, 'DM_PLAY gani ' .. g .. ' plays ' .. name)
end
-- Resident turn transitions play once, retaining the exact partner index.
for g, name in ipairs({'snapnon_s_tn_l90', 'snapnon_s_tn_r90', 'snapnon_s_tn_l180', 'snapnon_s_tn_r180',
    'snapnon_q_tn_l90', 'snapnon_q_tn_r90', 'snapnon_q_tn_l180', 'snapnon_q_tn_r180'}) do
    local id = g + 12
    c.reply = {128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, id}
    update(env)
    check(c.calls[#c.calls] == 'dmplay:1:/Assets/tpp/motion/SI_game/fani/bodies/snap/snapnon/' .. name .. '.gani' and
        c.dmLoop == false, 'turn clip ' .. id .. ' is a finite transition')
end
-- Resident Upper graph states are manual probes, with finite entry/exit/action requests.
for offset, name in ipairs({'stateGunHold', 'stateGunHoldStart', 'stateGunHoldEnd', 'stateGunFire', 'stateGunReload'}) do
    local id = offset + 20
    c.reply = {128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, id}
    update(env)
    check(c.calls[#c.calls] == 'dmplay:1:' .. name and c.dmLoop == (id == 21), 'gun graph probe ' .. id .. ' shape')
end
-- Exact receiver clips require the staged MTAR preload; all are manual one-shots.
for offset, name in ipairs({
    "snaphg00/snaphg00_s_fre", "snapar00/snapar00_aw1_s_fre", "snapar01/snapar01_aw0_s_fre",
    "snapsr02/snapsr02_s_fre2", "snapsr02/snapsr02_s_fre_rld"}) do
    local id = offset + 25
    c.reply = {128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, id}
    mark = #c.calls
    update(env)
    check(#c.calls == mark + 1 and c.calls[#c.calls] == "dmplay:1:/Assets/tpp/motion/SI_game/fani/bodies/snap/" .. name .. ".gani" and
        c.dmLoop == false, "receiver probe " .. id .. " exact path/instance and finite playback")
end
-- bad gani index refused (31 = partner::max_gani + 1).
for _, bad in ipairs({31, -1, 1.5, '3'}) do
    local notes = count(c.logs, 'event=dm_refused;reason=bad_gani')
    mark = #c.calls
    c.reply = {128, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, bad}
    update(env)
    check(#c.calls == mark and count(c.logs, 'event=dm_refused;reason=bad_gani') == notes + 1, 'DM_PLAY bad gani refused: ' .. tostring(bad))
end
-- DM_STOP (256).
c.reply = {256, 1, 0, 0, 0, 0, 1, 0}
update(env)
check(c.calls[#c.calls] == 'dmstop' and hasLog(c, 'event=dm_stop'), 'DM_STOP')
-- DM_PLAY | SET_PARTS: parts first, then dmplay with the trailing gani.
c.reply = {144, 1, 0, 0, 0, 0, 1, 0, 6, 27, 116, 0, 0, 1}
mark = #c.calls
update(env)
check(c.calls[mark + 1] == 'parts:1:6:27:116:0:0' and
    c.calls[mark + 2] == 'dmplay:1:/Assets/tpp/motion/SI_game/fani/bodies/snap/snapnon/snapnon_q_idl_l.gani', 'SET_PARTS before DM_PLAY')
-- A bad index refusal still comes first (nothing runs, not even DM).
c.reply = {129, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1}
mark = #c.calls
update(env)
check(#c.calls == mark and hasLog(c, 'event=refused;reason=bad_index;R=2'), 'R ~= 1 refusal precedes DM ops')
for _, reply in ipairs({{0, 1, 1, 1, 1, 1, 0, 0}, {-1, 1, 1, 1, 1, 1, 0, 0}, {'1', 1, 1, 1, 1, 1, 0, 0}, {nil, 1}}) do
    mark = #c.calls
    local warps = #c.warps
    c.reply = reply
    update(env)
    check(#c.calls == mark and #c.warps == warps, 'op <= 0 or not a number does nothing')
end
c.reply = {1, 1, 'x', 0, 0, 0, 0, 0}
mark = #c.warps
update(env)
check(#c.warps == mark, 'WARP needs numeric coordinates')
env, c = fixture(); call(env, 'OnMissionGameStart')
c.reply = {7, 1, 1000, 2000, 3000, -3142, 1, 40}
update(env)
check(c.calls[1] == 'allactive:true' and c.calls[2] == 'copy:1' and #c.warps == 1 and c.warps[1].command.pos[3] == 3,
    'op 7: all-active, copy parts, warp')

-- 4. Bad index refusal.
for _, case in ipairs({{R = 2, localIndex = 0, text = '2'}, {R = 0, localIndex = 0, text = '0'}, {R = nil, localIndex = 0, text = 'nil'},
                       {R = 1, localIndex = 1, text = '1'}}) do
    env, c = fixture(); call(env, 'OnMissionGameStart')
    c.localIndex = case.localIndex
    c.reply = {3, case.R, 0, 0, 0, 0, 0, 0}
    update(env)
    check(#c.calls == 0 and #c.warps == 0 and hasLog(c, 'event=refused;reason=bad_index;R=' .. case.text), 'bad index refused: ' .. case.text)
    local ticks = #c.ticks
    c.reply = {3, 1, 0, 0, 0, 0, 0, 0}
    c.localIndex = 0
    update(env, 5)
    check(#c.ticks == ticks and #c.calls == 0, 'refusal stops ticking for the mission')
    call(env, 'OnTerminate')
    check(c.stops == 1, 'STOP still sent after a refusal')
end

-- 5. pcall error path.
env, c = fixture(); call(env, 'OnMissionGameStart')
c.reply = {1, 1, 0, 0, 0, 0, 0, 0}
c.sendThrow = true
update(env)
check(#c.logs == 2 and starts(c.logs[2], 'event=error;msg=') and c.logs[2]:find('send_failed', 1, true), 'error noted, sanitized')
local ticks = #c.ticks
c.sendThrow = false
update(env, 5)
check(#c.ticks == ticks, 'an error stops ticking')
call(env, 'OnTerminate')
check(c.stops == 1, 'STOP still sent after an error')
env, c = fixture(); call(env, 'OnMissionGameStart')
c.validThrow = true
update(env, 3)
check(#c.ticks == 0 and #c.logs == 2 and starts(c.logs[2], 'event=error;msg='), 'binding error contained once')
for _, text in ipairs(c.logs) do
    check(#text <= 160 and not text:find('[^%w_=:,.;%-]'), 'note payload sanitized: ' .. text)
end

-- 6. STOP on terminate, with all-active cleared first.
env, c = fixture(); call(env, 'OnMissionGameStart')
c.reply = {2, 1, 0, 0, 0, 0, 0, 0}
update(env)
call(env, 'OnTerminate')
local order = table.concat(c.order, ',')
check(order:find('allactive:true,OnUpdate', 1, true) ~= nil or order:find('OnUpdate,allactive:true', 1, true) ~= nil, 'set during update')
check(order:find('allactive:false,STOP,OnTerminate', 1, true) ~= nil, 'clear, STOP, then the original OnTerminate: ' .. order)
check(c.stops == 1, 'one STOP')
ticks = #c.ticks
update(env, 3)
check(#c.ticks == ticks, 'no ticks after terminate')
call(env, 'OnTerminate')
check(c.stops == 1, 'STOP only once')
-- The exit clear runs only while the instance is still valid (the binding
-- dereferences P without a null check; pcall cannot catch a native fault).
for _, mode in ipairs({'invalid', 'throw'}) do
    env, c = fixture(); call(env, 'OnMissionGameStart')
    c.reply = {2, 1, 0, 0, 0, 0, 0, 0}
    update(env)
    if mode == 'invalid' then c.valid = false else c.validThrow = true end
    call(env, 'OnTerminate')
    order = table.concat(c.order, ',')
    check(count(c.calls, 'allactive:false') == 0 and hasLog(c, 'event=allactive_kept;reason=invalid_instance'),
        'no clear on an invalid instance: ' .. mode)
    check(order:find('allactive:true,STOP,OnTerminate', 1, true) ~= nil and c.stops == 1,
        'STOP and the original OnTerminate still run: ' .. order)
end
env, c = fixture(); call(env, 'OnMissionGameStart')
c.reply = {2, 1, 0, 0, 0, 0, 0, 0}
update(env)
call(env, 'OnTerminate')
-- A new mission starts fresh.
c.reply = nil
call(env, 'OnMissionGameStart')
update(env)
check(count(c.receipts, CFG) == 2 and fields(lastTick(c))[1] == '1' and fields(lastTick(c))[9] == '0', 'restart resets frame and flags')
-- STOP failure is contained; the original OnTerminate still runs.
env, c = fixture(); call(env, 'OnMissionGameStart')
env.loadstring = function() error(sentinel) end
call(env, 'OnTerminate')
check(c.order[#c.order] == 'OnTerminate', 'original OnTerminate runs after a failing STOP')

-- 7. Missing bindings: noted, skipped, never raised.
env, c = fixture({missing = {'Player.SetAllInstanceActiveMode', 'GameObject', 'PlayerStatus'}})
call(env, 'OnMissionGameStart')
check(hasLog(c, 'event=started'), 'optional bindings missing: still active')
local missingLog
for _, text in ipairs(c.logs) do if starts(text, 'event=missing;names=') then missingLog = text end end
check(missingLog and missingLog:find('Player.SetAllInstanceActiveMode', 1, true) and missingLog:find('GameObject.SendCommand', 1, true) and
    missingLog:find('PlayerStatus.CRAWL', 1, true), 'missing names reported: ' .. tostring(missingLog))
c.status[STATUS.CRAWL] = true
c.reply = {3, 1, 0, 0, 0, 0, 0, 0}
update(env, 3)
check(fields(lastTick(c))[7] == '0', 'missing PlayerStatus: stance 0')
check(count(c.logs, 'event=op_skipped;missing=Player.SetAllInstanceActiveMode') == 1 and
    count(c.logs, 'event=op_skipped;missing=GameObject.SendCommand') == 1, 'each skipped op noted once')
check(fields(lastTick(c))[9] == '0' and #c.ticks == 3, 'skipped op leaves luaflags unset; ticking continues')
env, c = fixture({missing = {'PlayerInfo'}})
call(env, 'OnMissionGameStart'); c.status[STATUS.DASH] = true; c.reply = {1, 1, 0, 0, 0, 0, 0, 0}
update(env)
check(fields(lastTick(c))[8] == '0' and #c.warps == 1, 'missing PlayerInfo: move 0, R checked against 1 only')
env, c = fixture({missing = {'Player.IsInstanceValid'}})
call(env, 'OnMissionGameStart'); update(env, 3)
check(#c.ticks == 0 and c.logs[1]:find('event=inactive;reason=missing_required;names=Player.IsInstanceValid', 1, true) == 1,
    'required binding missing: inactive and noted')
call(env, 'OnTerminate')
check(c.stops == 0, 'never active: no STOP')
env, c = fixture({missing = {'Player'}})
call(env, 'OnMissionGameStart'); update(env, 2)
check(#c.ticks == 0 and #c.logs == 1, 'no Player table at all: inactive')
env, c = fixture({strictPlayer = true})
call(env, 'OnMissionGameStart')
c.reply = {2, 1, 0, 0, 0, 0, 0, 0}
update(env, 2)
check(hasLog(c, 'event=started') and count(c.logs, 'event=op_skipped;missing=Player.SetAllInstanceActiveMode') == 1 and #c.ticks == 2,
    'a binding table that raises on lookup is handled')

-- 8. Original callbacks: arguments, returns, errors and yields are unchanged.
for _, name in ipairs({'OnMissionGameStart', 'OnUpdate', 'OnTerminate'}) do
    for _, activeFirst in ipairs({false, true}) do
        env, c = fixture({errorAt = name})
        if activeFirst and name ~= 'OnMissionGameStart' then
            env.e.OnMissionGameStart(7, nil, 9)
        end
        local ok, err = pcall(function() call(env, name) end)
        check(not ok and err == sentinel, 'original error propagated: ' .. name)
        env, c = fixture({yieldAt = name})
        if activeFirst and name ~= 'OnMissionGameStart' then
            local co0 = coroutine.create(function() call(env, 'OnMissionGameStart') end)
            coroutine.resume(co0); coroutine.resume(co0)
        end
        local co = coroutine.create(function() call(env, name) end)
        local resumed, value = coroutine.resume(co)
        check(resumed and value == 'original yield', 'original yield crosses the wrapper: ' .. name)
        check(coroutine.resume(co), 'original resumes: ' .. name)
    end
end
-- Compose the actual data producer with the actual callback wrapper; the
-- fixture consumes source receipts but performs no native or game playback.
do
    local function armed(reply)
        local env,c = fixture({actions=true,actionCfg=reply})
        env.vars.currentInventorySlot=0; env.vars.weapons={[0]=873}
        call(env,'OnMissionGameStart'); return env,c,env.e.MgsCoopPartnerActions
    end
    local env,c,A = armed(nil)
    update(env,90)
    check(c.actionQueries==1 and A.enabled==false and #c.actions==0, 'compiled-off query nil disables capture without retries')
    env,c,A = armed(false)
    update(env,29)
    check(c.actionQueries==1 and A.enabled==false and #c.actions==0, 'not-ready owner waits for bounded retry cadence')
    c.actionCfg=true; update(env,1)
    check(c.actionQueries==2 and A.enabled==true and #c.actions==1 and c.actionCache~=nil, 'later natural owner query enables raw capture and receipt consumption')
    local function actionFields() return fields(c.actions[#c.actions]:sub(#ACTION_TICK+1)) end
    local baseline=actionFields(); check(baseline[7]=='0' and baseline[8]=='0', 'first enabled sample baselines rather than deriving events from frame')
    c.status[STATUS.GUN_FIRE]=true; update(env,1,0.01)
    c.status[STATUS.GUN_FIRE]=false; update(env,1,0.01)
    c.status[STATUS.GUN_FIRE]=true; update(env,1,0.01)
    local fire=actionFields()
    check(fire[7]=='2' and fire[9]=='2' and fire[10]=='2', 'two raw fire rises under100ms survive old firing latch')
    env.PlayerVars.scannedButtonsDirect=512; update(env,1,0.01); update(env,5,0.01)
    check(actionFields()[8]=='1', 'held reload pad counts one request through composed owner hook')
    local clears=c.actionClears; env.vars.currentInventorySlot=9; update(env,1)
    check(c.actionClears==clears+1 and c.actionCache==nil and A.s==nil, 'nongun transition clears published native cache')
    update(env,3); check(c.actionClears==clears+1, 'nongun clear is idempotent')
    env.vars.currentInventorySlot=0; c.status[STATUS.GUN_FIRE]=false; env.PlayerVars.scannedButtonsDirect=0; update(env,1)
    clears=c.actionClears; c.clockThrow=true; update(env,1)
    check(c.actionClears==clears+1 and c.actionCache==nil, 'missing raw clock cancels published data')
    c.clockThrow=false; update(env,1)
    local generation=tonumber(actionFields()[1]); env.vars.weapons[0]=874; update(env,1)
    check(tonumber(actionFields()[1])>generation and actionFields()[3]=='874' and actionFields()[9]=='0', 'equipment reset carries fresh generation and zero event baseline')
    generation=tonumber(actionFields()[1]); c.status[STATUS.SQUAT]=true; update(env,1)
    check(tonumber(actionFields()[1])>generation and actionFields()[4]=='1', 'stance context resets before publication')
    c.actionAccepted=false; update(env,1); check(c.actionCache==nil, 'native refusal clears old cache and never enables playback')
    c.actionAccepted=true; update(env,1)
    c.actionCfg=false; call(env,'OnMissionGameStart')
    check(A.enabled==false and A.s==nil and c.actionCache==nil, 'mission start reset disables prior capture before fresh query')
    c.actionCfg=true; update(env,30); check(A.enabled==true and c.actionCache~=nil, 'new mission can reauthorize on bounded cadence')
    clears=c.actionClears; call(env,'OnTerminate')
    check(c.actionClears==clears+1 and c.actionCache==nil and A.s==nil, 'terminate clears capture before original callback')
    env,c,A=armed(false); update(env,3600)
    check(c.actionQueries==120 and A.enabled==false and #c.actions==0, 'not-ready queries stop at120 attempts without capture')
end
-- A module without the callbacks installs nothing.
do
    local bare = {e = {}}
    setmetatable(bare, {__index = _G})
    local chunk = assert(loadfile(arg[1])); setfenv(chunk, bare); chunk()
    check(bare.e.OnMissionGameStart == nil and bare.e.OnUpdate == nil and bare.e.OnTerminate == nil, 'no callbacks, no wrappers')
end

-- 9. Optional second argument: the complete DLL TppMain composition written by
-- host_trial_patch_check. The spliced chunk must be byte-identical to the
-- source file, and the composed module (every other co-op chunk included,
-- unknown game globals stubbed, other receipts left literal) must run the
-- 30050 path: inert while CFG stays literal; ticks, ops and STOP (after the
-- session lifecycle's terminating receipt, before the original OnTerminate)
-- once CFG answers true. This also proves that no enclosing local of the
-- composition shadows a global the chunk uses.
local composedRuns = 0
if arg[2] then
    local function slurp(path)
        local file = assert(io.open(path, 'rb'))
        local text = file:read('*a')
        file:close()
        -- git core.autocrlf checks sources out with CRLF; the DLL composition is LF.
        return (text:gsub('\r\n', '\n'))
    end
    local composed, standalone = slurp(arg[2]), slurp(arg[1])
    local spliced = '\n' .. standalone .. '\n'
    local at = composed:find(spliced, 1, true)
    check(at ~= nil and composed:find(spliced, at + 1, true) == nil, 'partner chunk spliced once, byte-identical')
    -- World sync (lua/coop/world_*.lua) is spliced between the partner chunk
    -- and the marker lifecycle chunk (world_sync_lua_check checks its layout).
    check(composed:find('-- Project-authored session feedback.', 1, true) < at and
        composed:find('\n-- MGS co-op world sync W0-W2', at, true) == at + #spliced and
        composed:find('-- Adapted from Dynamite TppMain marker-loading lifecycle', at, true) > at + #spliced,
        'partner chunk directly precedes the world sync chunks, then the marker lifecycle chunk')

    local function run(options)
        local r = {receipts = {}, calls = {}, ticks = {}, notes = {}}
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
            vars = {missionCode = options.mission or 30050, playerPosX = 1.5, playerPosY = 0.25, playerPosZ = -3.0,
                    playerRotY = 45.5, playerCameraRotation = {[1] = 90}, playerType = 0, playerPartsType = 1,
                    playerCamoType = 2, playerHandType = 3, playerFaceId = 4},
            Tpp = setmetatable({IsTypeFunc = function(v) return type(v) == 'function' end,
                                IsTypeTable = function(v) return type(v) == 'table' end,
                                MgsCoop = newCoop(options.mission or 30050, options.selected)},
                               {__index = function(t, k) local v = stub('Tpp.' .. k); rawset(t, k, v); return v end}),
            Player = {
                IsInstanceValid = function() return options.valid ~= false end,
                SetAllInstanceActiveMode = function(on) record('allactive:' .. tostring(on)) end,
                CopyPartsInfoFromLocalPlayer = function(t) record('copy:' .. tostring(t.instanceIndex)) end,
                SetPartsInfoAtInstanceIndex = function(t) record('parts:' .. tostring(t.instanceIndex)) end,
                SetOtherInstanceIsForMirrorMode = function(on) record('mirror:' .. tostring(on)) end,
            },
            PlayerInfo = {GetLocalPlayerIndex = function() return 0 end, OrCheckStatus = function() return false end},
            PlayerStatus = {STAND = 4, SQUAT = 5, CRAWL = 6, WALK = 13, RUN = 14, DASH = 15},
            GameObject = {SendCommand = function(id, command)
                record(string.format('warp:%s:%s:%s', tostring(id.type), tostring(id.index), tostring(command.id)))
            end},
        }
        env.loadstring = function(source)
            r.receipts[#r.receipts + 1] = source
            if source == CFG and options.cfg ~= nil then return function() return options.cfg end end
            if starts(source, TICK) then
                r.ticks[#r.ticks + 1] = source:sub(#TICK + 1)
                if options.reply then return function() return unpack(options.reply, 1, 8) end end
            end
            if starts(source, LOG) then r.notes[#r.notes + 1] = source:sub(#LOG + 1) end
            -- Everything else (and an unanswered receipt) compiles for real: a
            -- receipt then yields its literal string, as when native does not reply.
            return loadstring(source)
        end
        setmetatable(env, {__index = function(t, k)
            local v = _G[k]
            if v ~= nil then return v end
            v = stub(k); rawset(t, k, v); return v
        end})
        local chunk = assert(loadfile(arg[2])); setfenv(chunk, env)
        local mod = chunk()
        check(type(mod) == 'table' and type(mod.OnMissionGameStart) == 'function' and type(mod.OnUpdate) == 'function' and
            type(mod.OnTerminate) == 'function', 'composed module exports the callbacks')
        local mission = {sequence = {OnTerminate = function() record('sequence.OnTerminate') end}}
        local ok, err = pcall(mod.OnMissionGameStart, mission)
        check(ok, 'composed OnMissionGameStart: ' .. tostring(err))
        for _ = 1, options.updates or 3 do
            ok, err = pcall(mod.OnUpdate, mission)
            check(ok, 'composed OnUpdate: ' .. tostring(err))
        end
        ok, err = pcall(mod.OnTerminate, mission)
        check(ok, 'composed OnTerminate: ' .. tostring(err))
        composedRuns = composedRuns + 1
        return r
    end
    local function index(list, value)
        for i, v in ipairs(list) do if v == value then return i end end
        return nil
    end
    local function partnerReceipts(r)
        local n = 0
        for _, v in ipairs(r.receipts) do if v:find('MGS_COOP_PARTNER', 1, true) then n = n + 1 end end
        return n
    end
    local function playerCalls(r)
        local n = 0
        for _, v in ipairs(r.calls) do
            if starts(v, 'allactive:') or starts(v, 'copy:') or starts(v, 'parts:') or starts(v, 'mirror:') or starts(v, 'warp:') then
                n = n + 1
            end
        end
        return n
    end
    -- CFG left literal (every mode other than offline-v1 with partner=1).
    local r = run({reply = {3, 1, 0, 0, 0, 0, 0, 0}})
    check(count(r.receipts, CFG) == 1 and partnerReceipts(r) == 1, 'literal CFG: one CFG receipt, nothing else')
    check(#r.ticks == 0 and playerCalls(r) == 0 and index(r.receipts, STOP) == nil, 'literal CFG: no tick, no call, no STOP')
    check(index(r.calls, 'TppClock.Start') ~= nil and index(r.calls, 'sequence.OnTerminate') ~= nil,
        'literal CFG: original start and terminate ran')
    -- CFG false (offline-v1 refused).
    r = run({cfg = false, reply = {3, 1, 0, 0, 0, 0, 0, 0}})
    check(partnerReceipts(r) == 1 and playerCalls(r) == 0 and index(r.calls, 'sequence.OnTerminate') ~= nil, 'CFG false: inert')
    -- Another mission: not even the CFG receipt.
    r = run({mission = 10036, cfg = true, reply = {3, 1, 0, 0, 0, 0, 0, 0}})
    check(partnerReceipts(r) == 0 and playerCalls(r) == 0, 'other mission: no partner receipt')
    for _, mission in ipairs({30010, 30020}) do
        r = run({mission = mission, cfg = true, reply = {3, 1, 2000, 0, -1000, 1571, 1, 0}, updates = 3})
        check(#r.ticks == 3 and count(r.calls, 'warp:TppPlayer2:1:Warp') == 3 and count(r.receipts, STOP) == 1,
            'composed normal free roam path: ' .. mission)
        r = run({mission = mission, selected = false, cfg = true})
        check(partnerReceipts(r) == 0 and playerCalls(r) == 0, 'composed variant pack stays off: ' .. mission)
    end
    -- Active.
    r = run({cfg = true, reply = {3, 1, 2000, 0, -1000, 1571, 1, 0}, updates = 3})
    check(#r.ticks == 3 and r.ticks[1] == '1 1500 250 -3000 4550 9000 0 0 0 0 0' and r.ticks[2] == '2 1500 250 -3000 4550 9000 0 0 1 0 0',
        'composed ticks: ' .. tostring(r.ticks[1]) .. ' / ' .. tostring(r.ticks[2]))
    check(r.notes[1] == 'event=started' and index(r.notes, 'event=allactive_set') ~= nil, 'composed notes')
    check(count(r.calls, 'allactive:true') == 3 and count(r.calls, 'warp:TppPlayer2:1:Warp') == 3, 'composed ops: set + warp per tick')
    local clear, terminated = index(r.calls, 'allactive:false'), index(r.calls, 'sequence.OnTerminate')
    check(clear ~= nil and terminated ~= nil and clear < terminated, 'all-active cleared before the original OnTerminate')
    local stop, terminating = index(r.receipts, STOP), index(r.receipts, "return 'MGS_COOP_MISSION_TERMINATING_V1'")
    check(stop ~= nil and count(r.receipts, STOP) == 1 and terminating ~= nil and terminating < stop,
        'one STOP, after the session lifecycle terminating receipt')
end

print(checks .. ' partner sync Lua checks passed: inactive unless CFG is true, strict tick format, op bits 1/2/4/8/16/32/64, ' ..
    'bad index refusal, pcall error path, STOP on terminate, missing/raising bindings, original returns/errors/yields' ..
    (composedRuns > 0 and ('; composed TppMain across free roam (' .. composedRuns .. ' runs)') or '') .. '; no game calls')
