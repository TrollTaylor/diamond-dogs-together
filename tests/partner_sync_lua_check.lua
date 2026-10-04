-- Exercises lua/coop/partner_sync.lua with simulated native receipts and
-- recording game-binding mocks. No MGSV, Steam, network or real session.
-- Usage: lua51 partner_sync_lua_check.lua lua/coop/partner_sync.lua
local checks = 0
local function check(value, why)
    checks = checks + 1
    assert(value, tostring(checks) .. ': ' .. (why or 'check failed'))
end
local CFG = "return 'MGS_COOP_PARTNER_CFG_V1'"
local STOP = "return 'MGS_COOP_PARTNER_STOP_V1'"
local TICK = "return 'MGS_COOP_PARTNER_V1' -- "
local LOG = "return 'MGS_COOP_PARTNER_LOG_V1' -- "
local sentinel = {}
local STATUS = {STAND = 4, SQUAT = 5, CRAWL = 6, WALK = 13, RUN = 14, DASH = 15}

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
               status = {}, valid = true, localIndex = 0, order = {}}
    if options.cfg ~= nil then c.cfg = options.cfg end
    local env = {e = {}, vars = {missionCode = 30050, playerPosX = 1.5, playerPosY = 0.25, playerPosZ = -3.0,
        playerRotY = 45.5, playerCameraRotation = {[0] = 10, [1] = 90}, playerType = 0, playerPartsType = 1,
        playerCamoType = 2, playerHandType = 3, playerFaceId = 4}}
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
        check(type(source) == 'string' and #source <= 200, 'receipt within 200 bytes: ' .. tostring(source))
        c.receipts[#c.receipts + 1] = source
        if options.compileFail then return nil, 'fixture compile failure' end
        if source == CFG then
            return function()
                if options.cfgThrow then error(sentinel) end
                return c.cfg
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
                if c.reply then return unpack(c.reply, 1, 8) end
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
local function update(env, count) for _ = 1, count or 1 do call(env, 'OnUpdate') end end
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
    check(#c.receipts == 0, 'only Mother Base 30050 asks for CFG')
    env, c = fixture({compileFail = true})
    call(env, 'OnMissionGameStart'); update(env, 5)
    check(#c.ticks == 0 and #c.calls == 0, 'CFG compile failure keeps it inactive')
    env, c = fixture({cfgThrow = true})
    call(env, 'OnMissionGameStart'); update(env, 5)
    check(#c.ticks == 0 and #c.logs == 1 and starts(c.logs[1], 'event=error;msg='), 'CFG error contained and noted')
end

-- 2. Active: one quiet tick receipt per frame with 9 strict integers.
local env, c = fixture()
call(env, 'OnMissionGameStart')
check(hasLog(c, 'event=started') and #c.logs == 1, 'started noted, nothing missing')
update(env)
check(#c.ticks == 1 and lastTick(c) == '1 1500 250 -3000 4550 9000 0 0 0', 'tick fields: ' .. tostring(lastTick(c)))
c.status[STATUS.CRAWL], c.status[STATUS.DASH] = true, true
update(env)
check(lastTick(c) == '2 1500 250 -3000 4550 9000 2 3 0', 'crawl + dash')
c.status = {[STATUS.SQUAT] = true, [STATUS.RUN] = true}
update(env)
check(fields(lastTick(c))[7] == '1' and fields(lastTick(c))[8] == '2', 'squat + run')
c.status = {[STATUS.WALK] = true}
env.vars.playerPosX, env.vars.playerRotY = -0.0004, -179.994
update(env)
check(lastTick(c) == '4 0 250 -3000 -17999 9000 0 1 0', 'walk, rounding, negative yaw: ' .. lastTick(c))
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
check(count(c.calls, 'allactive:true') == 1 and hasLog(c, 'event=allactive_set') and #c.warps == 1, 'SET_ALLACTIVE once, no warp')
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
c.reply = {16, 1, 0, 0, 0, 0, 1, 0}
update(env, 2)
check(count(c.calls, 'parts:1:0:1:2:3:4') == 2 and hasLog(c, 'event=parts_set_explicit'), 'SET_PARTS_EXPLICIT from vars each time')
c.reply = {32, 1, 0, 0, 0, 0, 1, 0}
update(env)
check(c.calls[#c.calls] == 'mirror:true' and hasLog(c, 'event=mirror_on'), 'MIRROR_ON')
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
-- A module without the callbacks installs nothing.
do
    local bare = {e = {}}
    setmetatable(bare, {__index = _G})
    local chunk = assert(loadfile(arg[1])); setfenv(chunk, bare); chunk()
    check(bare.e.OnMissionGameStart == nil and bare.e.OnUpdate == nil and bare.e.OnTerminate == nil, 'no callbacks, no wrappers')
end

print(checks .. ' partner sync Lua checks passed: inactive unless CFG is true, strict tick format, op bits 1/2/4/8/16/32, ' ..
    'bad index refusal, pcall error path, STOP on terminate, missing/raising bindings, original returns/errors/yields; no game calls')
