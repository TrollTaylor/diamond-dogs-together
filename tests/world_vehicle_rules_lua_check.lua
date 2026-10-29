-- The user's vehicle decisions of 2026-10-29 (docs/vehicles-companions-design.md section 10 D11, D12, D14; section 8.9):
-- lua/coop/world_vehicle_rules.lua on top of world_vehicle.lua (VEHICLE_GONE) and world_vehicle_state.lua (the life
-- read), against a fake world, a fake TppVehicle2 pool, a fake iDroid and a fake TppTerminal. No game process.
-- Usage: lua51 world_vehicle_rules_lua_check.lua <world_vehicle_rules.lua> <world_vehicle.lua> <world_vehicle_state.lua>
-- Written before the module: without it (and without world_vehicle.lua's V.unattributed hook and seize readback)
-- every check after the surface check failed. The review fixes (2026-10-29, the user took the reviewer's picks) were
-- written first too: 300 m, cause 4 for an unattributed break (REJECT 11 from a host without the key), a hold instead of a
-- refusal for an occupied seat, an unknown guest position and the rate, the guest's position at the request's arrival,
-- the guest's queue, the VEHICLES drop item, the status tail and V.remove without an id. The blast rule (run 7762536a: the
-- host-side wreck's later explosion hit the host beside it for 3500 of 7800 life) was written first as well: a Despawn
-- instead of the wreck while the host's player or the partner stands within 15 m of the host's copy.
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(checks .. ': ' .. why, 2) end
end
local function slurp(path)
    local f = assert(io.open(path, 'rb'))
    local s = f:read('*a')
    f:close()
    return s
end
local RULES, GONE, STATE = slurp(assert(arg[1])), slurp(assert(arg[2])), slurp(assert(arg[3]))
check(#RULES:gsub('\r\n', '\n') <= 32000, 'the chunk (LF, as CMake splices it) is within the 32000-byte world chunk limit')
local function str32(s) local h = 7; for i = 1, #s do h = (h * 33 + s:byte(i)) % 4294967291 end; return h end
local RID = 25600

-- opts: role 'host'|'guest', brk, credit, loadout (the three keys), gone (VEHICLE_GONE, default on), state (VEHICLE_STATE),
-- wreck (the receiver wreck key), mission (default 30020), mode (default 1 lab), paired (default true).
local function Fixture(role, opts)
    opts = opts or {}
    local c = {logs = {}, tx = {}, despawns = {}, seized = {}, lifeSet = {}, menus = {}, notes = {}, credits = {}, clock = 100000}
    c.pool, c.names, c.mission = {}, {}, opts.mission or 30020
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.vars = {missionCode = c.mission, playerVehicleGameObjectId = 65535, playerPosX = 0, playerPosY = 0, playerPosZ = 0}
    env.PlayerStatus = {ON_VEHICLE = 4}
    env.PlayerInfo = {OrCheckStatus = function(s) return s[1] == 4 and c.riding == true end}
    env.Tpp = {IsVehicle = function(id) return type(id) == 'number' and id >= 25600 and id <= 26111 end}
    env.TppUiCommand = {
        SetMbTopMenuItemActive = function(name, on) c.menus[#c.menus + 1] = {name, on} end,
        IsMbDvcTerminalOpened = function() return c.idroid == true end}
    -- The engine's credit path: OnFultonVehicle(id, _, _, resource, _, player) -> AddTempResource.
    env.TppTerminal = {OnFultonVehicle = function(...) c.credits[#c.credits + 1] = {...}; return 'stock' end}
    local function vector(v) return {GetX = function() return v.x end, GetY = function() return v.y end, GetZ = function() return v.z end} end
    local W = {OP = {RECEIVED = 3, REJECT = 4, RIDE_BODY = 42, RIDE_FAST = 44}, handlers = {}, startHooks = {}, pairedHooks = {},
        unpairedHooks = {}, stopHooks = {}, active = true, paired = opts.paired ~= false, eff = 3, mission = c.mission, epoch = 7,
        host = role == 'host', role = role == 'host' and 1 or 2, mode = opts.mode or 1, vehicleGoneOn = opts.gone ~= false,
        vehicleStateOn = opts.state == true, vehicleWreckOn = opts.wreck == true, vehicleBreakOn = opts.brk == true,
        vehicleCreditOn = opts.credit == true, vehicleLoadoutOn = opts.loadout == true}
    W.now = function() return c.clock end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    W.get = function(t, k)
        if t == nil then return nil end
        local ok, v = pcall(function() return t[k] end)
        if ok then return v end
    end
    W.tx = function(...)
        c.tx[#c.tx + 1] = {...}
        return true
    end
    c.rid = 0
    W.nextRid = function() c.rid = c.rid % 65535 + 1; return c.rid end
    W.tick = function() c.ticks = (c.ticks or 0) + 1 end
    W.inGame = function() return c.inGame ~= false end
    W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
    W.observe = function() c.observed = (c.observed or 0) + 1 end
    W.h = str32
    W.downed = {records = {}}
    W.receipt = function(source)
        local first, n = source:match("^return 'MGS_COOP_VEHICLE_LIFE_V1' %-%- (%d+) (%d+)$")
        assert(first, 'only the life receipt: ' .. source)
        local r = {c = 24}
        for id = tonumber(first), tonumber(first) + tonumber(n) - 1 do
            local v = c.pool[id]
            if v and not v.released then r[id] = {v.life or 2300, 0, 4, 1} end
        end
        return r
    end
    W.api = {
        send = function(target, cmd)
            if type(target) == 'table' then
                assert(target.type == 'TppVehicle2' and cmd.id == 'Despawn', 'only the TppVehicle2 class Despawn')
                c.despawns[#c.despawns + 1] = cmd
                local id = cmd.locator and c.names[cmd.locator] or cmd.index and 25600 + cmd.index
                if id and c.pool[id] then c.pool[id].alive, c.pool[id].released = false, true end
                return true
            end
            local v = c.pool[target]
            if not v then return nil end
            if cmd.id == 'IsAlive' then return v.alive == true and not v.released end
            if v.released then return nil end
            if cmd.id == 'Seize' then
                -- The engine removes it and runs the stock Fulton credit (E17); the module must have marked it gone first.
                c.seized[#c.seized + 1] = {id = target, options = cmd.options, markedGone = W.vehicleGone.gone[target] ~= nil}
                if not c.seizeFails then
                    v.alive, v.released = false, true
                    env.TppTerminal.OnFultonVehicle(target, 0, 0, v.res or 14, nil, nil)
                end
                return nil
            end
            if cmd.id == 'SetBodyLife' then
                c.lifeSet[#c.lifeSet + 1] = {id = target, life = cmd.life, markedGone = W.vehicleGone.gone[target] ~= nil}
                v.life = cmd.life
                if cmd.life == 0 and not c.wreckFails then v.alive = false end
                return nil
            end
            if cmd.id == 'GetPosition' then
                if v.nopose then return nil end
                return vector(v), 0
            end
            if cmd.id == 'GetVehicleType' then return v.type, 0 end
            if cmd.id == 'GetRiderId' then
                local r = {}
                for i = 1, 4 do r[i] = v.riders and v.riders[i] or 65535 end
                return r
            end
            error('unexpected instance command ' .. tostring(cmd.id))
        end,
        id = function(class, name)
            assert(class == 'TppVehicle2', 'vehicle locators only')
            return c.names[name] or 65535
        end,
        str32 = str32,
    }
    W.ridePose = {fast = {tracks = {}}, place = function() return true end}
    W.M, W.kind = {}, {}
    env.e = {MgsCoopWorld = W}
    setfenv(assert(loadstring(GONE)), env)()
    setfenv(assert(loadstring(STATE)), env)()
    setfenv(assert(loadstring(RULES)), env)()
    c.env, c.W, c.V, c.R, c.H = env, W, W.vehicleGone, W.vehicleRules, W.handlers
    function c:add(id, x, z, t, name, extra)
        self.pool[id] = {alive = true, x = x, y = 1, z = z, type = t or 1, life = 2300}
        for k, v in pairs(extra or {}) do self.pool[id][k] = v end
        if name then self.names[name] = id end
    end
    function c:poll(ms) self.clock = self.clock + (ms or 300); self.W.tick() end
    function c:polls(n, ms) for _ = 1, n do self:poll(ms) end end
    function c:has(pattern) for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then return l end end end
    function c:count(pattern) local n = 0; for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then n = n + 1 end end; return n end
    function c:sent(op) local n = {} for _, t in ipairs(self.tx) do if t[1] == op then n[#n + 1] = t end end return n end
    function c:menu(name) for i = #self.menus, 1, -1 do if self.menus[i][1] == name then return self.menus[i][2] end end end
    -- The guest's DOWNED_STATUS record on the host (world_downed.lua D.records[2]): pos in mm, at = receive time.
    function c:guestAt(x, z, age) self.W.downed.records[2] = {pos = {x * 1000, 1000, z * 1000}, at = self.clock - (age or 0)} end
    for _, f in ipairs(W.startHooks) do f() end
    return c
end

-- Module surface and the start line.
local c = Fixture('guest')
check(type(c.R) == 'table' and type(c.V.unattributed) == 'function', 'W.vehicleRules and the V.unattributed hook')
check(c.R.BREAK_MM == 300000 and c.R.FRESH_MS == 3000 and c.R.RATE_N == 4 and c.R.RATE_MS == 20000 and c.R.CREDIT_MS == 10000 and
    c.R.ASK_N == 6 and c.R.QUEUE_N == 32 and c.R.UNATTRIBUTED == 4 and c.R.BLAST_MM == 15000,
    'rules: 300 m, a guest position at most 3 s old, 4 applied per 20 s (more wait), 6 asked per 60 s (more queue), cause 4, ' ..
    'no wreck within 15 m of a player')
check(not c:has('event=vehicle_rules;step=on'), 'every key off: no start line')
c = Fixture('host', {brk = true, credit = true, loadout = true})
check(c:has('event=vehicle_rules;step=on;mission=30020;role=host;break=1;credit=1;loadout=1;gone=1') and c:has('step=credit_hook;ok=1'),
    'the start line names the keys; the credit key installs the OnFultonVehicle hook')

-- Keys off: today's behaviour (0.13.6 / M4).
local g = Fixture('guest')
g:add(25608, -653, 877.7, 1, 'veh_lv_0000')
g.V.observe('VehicleBroken', 25608, 'none')
check(g.V.watch[25608] == nil and not g:has('break_seen'), 'break key off: an unattributed break is not requested')
local h = Fixture('host')
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-400, 877.7)
h.H[47]({9, 25608, 2, 1, -653000, 1000, 877700})
check(#h.despawns == 1 and #h.lifeSet == 0 and not h:has('break_judge') and h:has('step=applied;id=25608;via=locator;src=req;ok=1'),
    'break key off: a guest destruction request is applied as before (a Despawn, no judge)')
g = Fixture('guest')
g:add(25610, 100, 100, 1, 'veh_lv_0002')
g.H[46]({1, 25610, 1, 0, 0, 1, 100000, 1000, 100000})
check(#g.seized == 0 and #g.despawns == 1 and #g.credits == 0, 'credit key off: a Fulton loss is a Despawn on the receiver, no credit')
g = Fixture('guest')
g:polls(12)
check(#g.menus == 0, 'loadout key off: the iDroid is never touched')

-- D12, guest: an unattributed break is requested as a destruction (cause 2).
g = Fixture('guest', {brk = true})
g:add(25608, -653, 877.7, 1, 'veh_lv_0000')
g.V.observe('VehicleBroken', 25608, 'none')
check(g.V.watch[25608] and g.V.watch[25608].cause == 2 and g:has('event=vehicle_rules;step=break_seen;id=25608'), 'the break is watched as cause 2')
g.pool[25608].alive = false
g:poll()
local reqs = g:sent(47)
check(#reqs == 1 and reqs[1][3] == 25608 and reqs[1][4] == 4 and reqs[1][6] == -653000 and reqs[1][8] == 877700 and g.V.gone[25608] and
    g:has('event=vehicle_gone;step=req;rid=1;id=25608;cause=4;sent=1'),
    'REQ_VEHICLE_GONE {rid, id, cause 4 (unattributed), type, the guest copy\'s xyz}; lost here')
g:add(25600, 5, 5, 1)
g.V.observe('VehicleBroken', RID, 'none')
check(g.V.watch[RID] == nil and not g:has('break_seen;id=25600'), 'never index 0 (the sortie vehicle is M3\'s)')
-- Its own removal or wreck (the receiver's wreck key) is not a loss: within 5 s of V.remove nothing is requested.
g = Fixture('guest', {brk = true, wreck = true})
g:add(25609, 200, 200, 1, 'veh_lv_0001')
g.H[46]({1, 25609, 2, 0, 0, 1, 200000, 1000, 200000})
g.V.gone[25609] = nil
g.V.observe('VehicleBroken', 25609, 'none')
check(g.V.watch[25609] == nil and #g:sent(47) == 0, 'the receiver\'s own wreck is never requested back')
-- At most 6 asked per 60 s; more wait in a queue (never dropped) and are asked as the window frees.
g = Fixture('guest', {brk = true})
for i = 1, 7 do g:add(25610 + i, 10 * i, 10, 1); g.V.observe('VehicleBroken', 25610 + i, 'none') end
check(g:count('step=break_seen') == 7 and g.V.watch[25617] ~= nil and not g:has('break_skip'), 'guest: every break is watched')
for i = 1, 7 do g.pool[25610 + i].alive = false end
g:poll()
check(#g:sent(47) == 6 and g:count('event=vehicle_rules;step=break_queued;id=') == 1 and g:has(';n=1') and #g.R.queue == 1 and
    g.V.gone[25617] and g.V.gone[25611], 'guest: 6 asked, the 7th queued (all lost here)')
for _, r in ipairs(g:sent(47)) do g.H[3]({r[2], 47}) end
g:polls(10, 1000)
check(#g:sent(47) == 6, 'the queue waits for the window')
g.clock = g.clock + 50000
g:poll()
local q7 = g:sent(47)
local ids7 = {}
for _, t in ipairs(q7) do ids7[t[3]] = true end
local all7 = true
for i = 1, 7 do all7 = all7 and ids7[25610 + i] == true end
check(#q7 == 7 and q7[7][4] == 4 and all7 and g:count('event=vehicle_rules;step=break_ask;id=') == 1 and #g.R.queue == 0,
    'and asked once the window frees (each of the 7 asked once)')
-- A full queue (32) skips further breaks (logged).
g = Fixture('guest', {brk = true})
for i = 1, 40 do g:add(25620 + i, 10 * i, 10, 1); g.V.observe('VehicleBroken', 25620 + i, 'none'); g.pool[25620 + i].alive = false end
g:poll()
check(g:count('step=break_seen') == 40 and #g:sent(47) == 6 and #g.R.queue == 32 and g:count('step=break_skip;id=') == 2 and
    g:has(';reason=queue_full'), 'guest: 6 asked, 32 queued, the rest skipped (logged)')
-- The host's REJECT for its destruction request: logged as a divergence (the guest's copy keeps its wreck).
g = Fixture('guest', {brk = true})
g:add(25608, -653, 877.7, 1, 'veh_lv_0000')
g.V.observe('VehicleBroken', 25608, 'none')
g.pool[25608].alive = false
g:poll()
local rid = g:sent(47)[1][2]
g.H[4]({rid, 47, 8, 25608, 0, 0})
check(g:has('event=vehicle_rules;step=break_refused;id=25608;rid=' .. rid .. ';reason=8;here=wreck') and
    g:has('step=req_done;rid=' .. rid .. ';id=25608;result=reject;reason=8') and g.V.req[rid] == nil, 'a refused break: logged, the request ended')
g.H[4]({77, 47, 8, 25608, 0, 0})
check(g:count('break_refused') == 1, 'a REJECT for another rid is not ours')

-- D12, host: the judge.
local function hostReq(x, id, rid2, cause, xm, zm)
    x.H[47]({rid2 or 9, id or 25608, cause or 4, 1, (xm or -653) * 1000, 1000, (zm or 877.7) * 1000})
end
-- The host's own key off: an unattributed break (cause 4) is never applied (REJECT 11, key parity decided by the host).
h = Fixture('host')
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-650, 877.7)
hostReq(h)
local r11 = h:sent(4)
check(#r11 == 1 and r11[1][2] == 9 and r11[1][4] == 11 and h.pool[25608].alive and #h.lifeSet == 0 and #h.despawns == 0 and
    not h:has('break_judge') and h:has('step=refused;id=25608;src=req;reason=break_off'), 'host key off: cause 4 refused with REJECT 11')
-- Near: the guest within 300 m of the reported pose -> the host's copy becomes the same wreck, answered gone.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-640, 860)
hostReq(h)
check(h:has('event=vehicle_rules;step=break_judge;id=25608;rid=9;ok=1;why=near;d_mm=21') and #h.lifeSet == 1 and h.lifeSet[1].life == 0 and
    h.lifeSet[1].markedGone and #h.despawns == 0 and not h.pool[25608].alive and not h.pool[25608].released,
    'near: judged ok, the host\'s copy wrecked (SetBodyLife 0; marked gone first)')
check(h:has('event=vehicle_gone;step=applied;id=25608;via=wreck;src=req;ok=1') and h:count('break_judge') == 1, 'read back as a wreck; one judge line')
local ans = h:sent(46)
check(#ans == 1 and ans[1][3] == 25608 and ans[1][4] == 2 and ans[1][5] == 1 and ans[1][6] == 9 and #h:sent(4) == 0,
    'answered with VEHICLE_GONE {cause 2, actor 1, the rid}; no REJECT')
h.V.observe('VehicleBroken', 25608, 'none')
check(h.V.watch[25608] == nil, 'its own wreck is never watched as a host loss')
hostReq(h, 25608, 10)
check(#h.lifeSet == 1 and #h:sent(46) == 2 and h:count('break_judge') == 1, 'a repeated request is answered again, never wrecked twice')
check(not h:has('break_safe'), 'nobody within 15 m (the host at 0,0; the guest 22 m away): the wreck')
-- The blast rule (run 7762536a): the host's own player within 15 m of its copy -> a Despawn (no explosion), answered gone.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h.env.vars.playerPosX, h.env.vars.playerPosZ = -661, 877.7
h:guestAt(-640, 860)
hostReq(h)
check(h:has('step=break_judge;id=25608;rid=9;ok=1;why=near') and h:has('event=vehicle_rules;step=break_safe;id=25608;near=host;d_mm=8000;form=despawn') and
    #h.lifeSet == 0 and #h.despawns == 1 and h.despawns[1].locator == 'veh_lv_0000' and h.pool[25608].released,
    'the host 8 m from its copy: a Despawn, never SetBodyLife 0')
check(h:has('event=vehicle_gone;step=applied;id=25608;via=locator;src=req;ok=1') and #h:sent(46) == 1 and h:sent(46)[1][4] == 2 and
    #h:sent(4) == 0, 'read back and answered gone (cause 2, no REJECT)')
-- The partner (the guest's DOWNED_STATUS position) within 15 m, the host far: a Despawn too.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-650, 877.7)
hostReq(h)
check(h:has('step=break_safe;id=25608;near=partner;d_mm=3000;form=despawn') and #h.lifeSet == 0 and #h.despawns == 1,
    "the partner 3 m from the host's copy: a Despawn")
-- Exactly 15 m is near; 15.1 m is not.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h.env.vars.playerPosX, h.env.vars.playerPosZ = -668, 877.7
h:guestAt(-653, 860)
hostReq(h)
check(h:has('step=break_safe;id=25608;near=host;d_mm=15000') and #h.lifeSet == 0, '15 m: near')
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h.env.vars.playerPosX, h.env.vars.playerPosZ = -668.1, 877.7
h:guestAt(-653, 860)
hostReq(h)
check(not h:has('break_safe') and #h.lifeSet == 1 and #h.despawns == 0, '15.1 m (and the guest 17.7 m): the wreck')
-- A position that cannot be read (the host's copy or the host's player): the Despawn (never a blast nobody checked).
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-640, 860)
h.V.decide = function() return 'apply' end
h.pool[25608].nopose = true
hostReq(h)
check(h:has('step=break_safe;id=25608;near=unknown;d_mm=-1;form=despawn') and #h.lifeSet == 0 and #h.despawns == 1,
    "the host's copy without a readable position: the Despawn")
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-640, 860)
h.env.vars.playerPosX = nil
hostReq(h)
check(h:has('step=break_safe;id=25608;near=unknown;d_mm=-1;form=despawn') and #h.lifeSet == 0 and #h.despawns == 1,
    "the host's own position unreadable: the Despawn")
-- The receiver side never runs the blast rule (the guest's wreck key is VEHICLE_GONE's own, lab only).
g = Fixture('guest', {brk = true, wreck = true})
g:add(25609, 200, 200, 1, 'veh_lv_0001')
g.H[46]({1, 25609, 2, 0, 0, 1, 200000, 1000, 200000})
check(not g:has('break_safe') and #g.lifeSet == 1, 'guest receiver: the wreck key as before, no blast rule')
-- An attributed destruction (cause 2) is judged the same way with the key on.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-640, 860)
hostReq(h, 25608, 9, 2)
check(h:has('step=break_judge;id=25608;rid=9;ok=1;why=near') and not h.pool[25608].alive, 'cause 2 with the key: judged and wrecked')
-- 300 m covers a ranged kill (a launcher from 150 m).
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-503, 877.7)
hostReq(h)
check(h:has('ok=1;why=near;d_mm=150000') and not h.pool[25608].alive, 'the guest 150 m away: near')
-- Far: refused with REJECT 8; the host's copy untouched.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-300, 877.7)
hostReq(h)
local rj = h:sent(4)
check(h:has('step=break_judge;id=25608;rid=9;ok=0;why=guest_far;d_mm=353000') and h.pool[25608].alive and #h.lifeSet == 0 and
    #h.despawns == 0 and #rj == 1 and rj[1][2] == 9 and rj[1][3] == 47 and rj[1][4] == 8 and #h:sent(46) == 0 and
    h:has('event=vehicle_gone;step=refused;id=25608;src=req;reason=guest_far') and h:count('break_judge') == 1,
    'far (more than 300 m): REJECT 8, nothing removed, one judge line')
-- No fresh guest position: held (re-checked every 250 ms), judged once one comes; REJECT 9 after 120 s without one.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-650, 877.7, 3500)
hostReq(h)
check(h.V.held[25608] and #h:sent(4) == 0 and h:has('event=vehicle_rules;step=break_hold;id=25608;rid=9;why=guest_unknown') and
    h.pool[25608].alive and not h:has('break_judge'), 'a guest position older than 3 s: held, not refused')
h:polls(4)
check(h:count('step=break_hold;id=25608') == 1, 'logged once')
h:guestAt(-650, 877.7)
h:poll()
check(h:has('step=break_judge;id=25608;rid=9;ok=1;why=near;d_mm=3000') and not h.pool[25608].alive and h.V.held[25608] == nil,
    'a fresh position: judged near and applied')
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
hostReq(h)
for _ = 1, 401 do h:poll(300) end
local r9 = h:sent(4)
check(#r9 == 1 and r9[1][4] == 9 and h.pool[25608].alive and h:has('step=hold_timeout;id=25608;src=req;reason=guest_unknown'),
    'never a position: REJECT 9 after 120 s')
-- Heavy damage on the host's copy: applied although the guest is far (VEHICLE_STATE's reads give the peak).
h = Fixture('host', {brk = true, state = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h.W.vehicleState.readLife(25608, 1)
h.pool[25608].life = 500
h:guestAt(-300, 877.7)
hostReq(h)
check(h:has('ok=1;why=heavy;d_mm=353000;life=500;peak=2300') and not h.pool[25608].alive, 'heavy: a quarter of the peak or less')
h = Fixture('host', {brk = true, state = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h.W.vehicleState.readLife(25608, 1)
h.pool[25608].life = 600
h:guestAt(-300, 877.7)
hostReq(h)
check(h:has('ok=0;why=guest_far;d_mm=353000;life=600;peak=2300') and h.pool[25608].alive, 'more than a quarter: not heavy')
-- The host seated in its copy: refused at once (never held, never removed later).
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h.riding, h.env.vars.playerVehicleGameObjectId = true, 25608
h:guestAt(-650, 877.7)
hostReq(h)
h:polls(3)
check(h:has('step=break_judge;id=25608;rid=9;ok=0;why=ridden;by=local_rider') and h.pool[25608].alive and #h:sent(4) == 1 and
    h:sent(4)[1][4] == 8 and h.V.held[25608] == nil and #h.lifeSet == 0, 'the host seated in it: REJECT 8 at once, never held')
-- Another filled seat (D-Dog, a soldier): held like any occupied vehicle, judged and applied once the seat is free.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000', {riders = {65535, 9728}})
h:guestAt(-650, 877.7)
hostReq(h)
check(h.V.held[25608] and h:has('step=hold;id=25608;src=req;reason=occupied') and not h:has('break_judge') and #h:sent(4) == 0 and
    h.pool[25608].alive, 'D-Dog seated (an occupied seat): held, not refused')
h.clock = h.clock + 3500
h:guestAt(-250, 877.7)
h.pool[25608].riders = nil
h:poll()
check(h:has('step=break_judge;id=25608;rid=9;ok=1;why=near;d_mm=3000') and not h.pool[25608].alive and #h:sent(46) == 1 and #h:sent(4) == 0,
    'the seat free: judged against the guest\'s position at the request (3 m, it walked 400 m away since), applied')
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000', {riders = {4321}})
h:guestAt(-650, 877.7)
hostReq(h)
for _ = 1, 401 do h:poll(300) end
local r9b = h:sent(4)
check(#r9b == 1 and r9b[1][4] == 9 and h.pool[25608].alive and h:has('step=hold_timeout;id=25608;src=req;reason=occupied'),
    'a soldier who stays seated: REJECT 9 after 120 s (VEHICLE_GONE\'s hold cap)')
-- ... and a copy that broke on the host too is answered as gone.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000', {riders = {4321}})
h.pool[25608].alive = false
h:guestAt(-650, 877.7)
hostReq(h)
check(h:has('event=vehicle_gone;step=absent;id=25608;src=req;reason=wreck') and #h:sent(46) == 1 and #h:sent(4) == 0 and #h.lifeSet == 0,
    'occupied but broken on the host too: answered gone, no REJECT')
-- The partner's ride on the host's copy is held as before (the guest's own jeep broke under it), then judged against the
-- closer of the guest's position at the request and now.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h.W.ridePose.fast.tracks[25608] = {}
h:guestAt(-650, 877.7)
hostReq(h)
check(h.V.held[25608] and h:has('step=hold;id=25608;src=req;reason=partner_ride') and not h:has('break_judge'), 'partner ride: held')
h.W.ridePose.fast.tracks[25608] = nil
h.clock = h.clock + 10000
h:guestAt(-250, 877.7)
h:poll(200)
check(h:has('ok=1;why=near;d_mm=3000') and not h.pool[25608].alive, 'free again: the guest sprinted 400 m away meanwhile, still near')
-- At most 4 applied per 20 s: the fifth waits (held) and is applied once the window frees (never refused).
h = Fixture('host', {brk = true})
for i = 0, 4 do h:add(25608 + i, 10 * i, 0, 1, string.format('veh_lv_%04d', i)) end
h:guestAt(20, 0)
for i = 0, 4 do hostReq(h, 25608 + i, 20 + i, 4, 10 * i, 0) end
check(h:count('ok=1;why=near') == 4 and h:has('event=vehicle_rules;step=break_hold;id=25612;rid=24;why=rate;n=4') and h.pool[25612].alive and
    h.V.held[25612] and #h:sent(4) == 0 and h:count('step=break_judge;id=25612') == 0, '4 per 20 s: the fifth held, no judge line yet')
h:polls(10)
check(h.pool[25612].alive and h:count('step=break_hold;id=25612') == 1, 'held while the window is full (logged once)')
h.clock = h.clock + 20000
h:guestAt(20, 0)
h:poll()
check(not h.pool[25612].alive and h:count('step=break_judge;id=25612;rid=24;ok=1;why=near') == 1 and #h:sent(4) == 0 and #h:sent(46) == 5,
    'applied once the window frees: one judge line, no REJECT')
-- A wreck that does not take on the host falls back to the Despawn (world_vehicle.lua readback).
h = Fixture('host', {brk = true})
h.wreckFails = true
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-640, 860)
hostReq(h)
h:poll(1000); h:poll(1100)
check(h:has('step=fallback;id=25608;from=wreck;via=locator;ok=1') and h.pool[25608].released, 'a wreck that does not take: the Despawn')
-- Index 0 is never judged or wrecked here (M3; VEHICLE_GONE refuses it, world_relief_sync.lua owns it when on).
h = Fixture('host', {brk = true})
h:add(RID, 5, 5, 1)
h:guestAt(5, 5)
hostReq(h, RID, 9, 2, 5, 5)
check(h.pool[RID].alive and #h.lifeSet == 0 and h:has('step=refused;id=25600;src=req;reason=index0'), 'index 0: refused index0')
-- A Fulton request (cause 1) is never judged.
h = Fixture('host', {brk = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
hostReq(h, 25608, 9, 1)
check(not h:has('break_judge') and #h.despawns == 1, 'a guest Fulton is not a break: no judge')
-- V.remove without an id never raises (an error there would turn the vehicle module off).
local okNil, gotNil = pcall(h.V.remove, nil, 2)
check(okNil and gotNil == false, 'V.remove(nil): refused without an error')

-- D14: the receiver credits the Fultoned vehicle through its own engine path (Seize Instant), once.
g = Fixture('guest', {credit = true})
g:add(25610, 100, 100, 1, 'veh_lv_0002', {res = 14})
g.H[46]({1, 25610, 1, 0, 0, 1, 100000, 1000, 100000})
check(#g.seized == 1 and g.seized[1].id == 25610 and g.seized[1].options[1] == 'Instant' and #g.seized[1].options == 1 and
    g.seized[1].markedGone and #g.despawns == 0, 'guest receiver: the stock Seize {"Instant"} (never DirectAccount), marked gone first')
check(g:has('event=vehicle_rules;step=credit_seize;id=25610;role=guest;mission=30020') and
    g:has('event=vehicle_gone;step=applied;id=25610;via=seize;src=rx;ok=1'), 'logged and read back')
check(#g.credits == 1 and g.credits[1][1] == 25610 and g.credits[1][4] == 14 and
    g:has('event=vehicle_rules;step=credit;fn=OnFultonVehicle;id=25610;res=14;via=receiver;role=guest;mission=30020'),
    'the engine\'s OnFultonVehicle ran once on the receiver, logged via=receiver')
g.V.observe('FultonInfo', 25610, 'local')
g.V.observe('Fulton', 25610, 'none')
check(g.V.watch[25610] == nil and #g:sent(47) == 0, 'its own Seize\'s Fulton messages are never a guest loss (no request back)')
g.H[46]({1, 25610, 1, 0, 0, 1, 100000, 1000, 100000})
g.H[46]({2, 25610, 1, 0, 0, 1, 100000, 1000, 100000})
check(#g.seized == 1 and #g.credits == 1, 'a re-sent announcement credits nothing more')
g:polls(40, 300)
check(not g:has('credit_missing'), 'the credit was seen')
-- The Fultoning copy's own credit is logged too (via=own).
g.env.TppTerminal.OnFultonVehicle(25611, 0, 0, 16, nil, 0)
check(g:has('step=credit;fn=OnFultonVehicle;id=25611;res=16;via=own;role=guest'), 'the Fultoning copy\'s own credit: via=own')
-- The host receives the guest's Fulton request: its own Seize credits the host's save.
h = Fixture('host', {credit = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000', {res = 14})
hostReq(h, 25608, 9, 1)
check(#h.seized == 1 and #h.despawns == 0 and #h.credits == 1 and h:has('step=credit;fn=OnFultonVehicle;id=25608;res=14;via=receiver;role=host') and
    h:has('step=applied;id=25608;via=seize;src=req;ok=1'), 'host receiver: Seize Instant, credited once')
local a46 = h:sent(46)
check(#a46 == 1 and a46[1][3] == 25608 and a46[1][4] == 1 and a46[1][5] == 1 and a46[1][6] == 9, 'answered gone (cause 1, actor 1, the rid)')
hostReq(h, 25608, 10, 1)
check(#h.seized == 1 and #h.credits == 1 and #h:sent(46) == 2, 'a repeated request: answered, never credited twice')
-- A destroyed vehicle (cause 2) is never seized for a credit.
g = Fixture('guest', {credit = true})
g:add(25610, 100, 100, 1, 'veh_lv_0002')
g.H[46]({1, 25610, 2, 0, 0, 1, 100000, 1000, 100000})
check(#g.seized == 0 and #g.despawns == 1 and #g.credits == 0, 'a destruction is a Despawn (no credit)')
-- Never index 0 (the sortie vehicle keeps M3's one credit, the host's).
g = Fixture('guest', {credit = true})
g:add(RID, 5, 5, 1)
g.H[46]({1, RID, 1, 0, 0, 1, 5000, 1000, 5000})
check(#g.seized == 0 and #g.credits == 0 and g.pool[RID].alive and g:has('step=refused;id=25600;src=rx;reason=index0'), 'index 0: no Seize, no credit')
-- A Seize that does not take: the Despawn after 2 s, and a missing credit is logged after 10 s.
g = Fixture('guest', {credit = true})
g.seizeFails = true
g:add(25610, 100, 100, 1, 'veh_lv_0002')
g.H[46]({1, 25610, 1, 0, 0, 1, 100000, 1000, 100000})
g:poll(1000); g:poll(1100)
check(g:has('step=fallback;id=25610;from=seize;via=locator;ok=1') and g.pool[25610].released, 'a Seize still alive after 2 s: the Despawn')
g:polls(40, 300)
check(g:has('event=vehicle_rules;step=credit_missing;id=25610;role=guest') and #g.credits == 0, 'no credit seen: credit_missing')
-- VEHICLE_GONE off: the credit key does nothing (it rides on VEHICLE_GONE).
g = Fixture('guest', {credit = true, gone = false})
g:add(25610, 100, 100, 1, 'veh_lv_0002')
g.H[46]({1, 25610, 1, 0, 0, 1, 100000, 1000, 100000})
check(#g.seized == 0 and #g.despawns == 0 and not g.R.creditOn(), 'VEHICLE_GONE off: nothing removed, nothing credited')

-- D11: the guest's LOADOUT and VEHICLES drop items inactive in co-op (the user's pick, 2026-10-29: only the host picks
-- vehicles, whatever world_relief says), the refusal and its toast.
g = Fixture('guest', {loadout = true})
g:poll()
check(g:menu('MSN_DROP_LOADOUT') == false and g:menu('MSN_DROP_VEHICLE') == false and #g.menus == 2,
    'the LOADOUT and VEHICLES items are set inactive (nothing else in the iDroid changes)')
check(g:has('event=vehicle_rules;step=guard;path=idroid_drop_loadout;via=menu_inactive;mission=30020') and
    g:has('event=vehicle_rules;step=guard;path=idroid_drop_vehicle;via=menu_inactive;mission=30020') and g.R.ownsVehicleDrop() == true,
    'guard lines; this module owns the VEHICLES item (world_relief_sync.lua leaves it alone)')
g:polls(20)
check(g:count('step=guard;path=idroid_drop_loadout') == 1 and g:count('step=guard;path=idroid_drop_vehicle') == 1 and #g.menus > 2,
    'repeated every 2 s, logged once per mission')
local nm = #g.menus
g.idroid = true
g:polls(20)
check(#g.menus == nm, 'never re-applied while the iDroid is open')
g.idroid = false
g.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_LOADOUT'))
check(g:has('event=vehicle_rules;step=refused;path=idroid_drop_loadout;via=menu_inactive;mission=30020') and #g.notes == 1 and
    g.notes[1][1] == 13 and g.notes[1][2] == 5 and g.observed == 1, 'selected: refused, NOTE 13 a 5, the stock observer still runs')
g.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_LOADOUT'))
check(#g.notes == 1 and g:count('step=refused;path=idroid_drop_loadout;') == 2, 'a toast at most every 3 s (each refusal logged)')
g.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_VEHICLE'))
check(g:has('event=vehicle_rules;step=refused;path=idroid_drop_vehicle;via=menu_inactive;mission=30020') and #g.notes == 2 and
    g.notes[2][1] == 13 and g.notes[2][2] == 4, 'VEHICLES selected: refused, NOTE 13 a 4 (its own toast)')
g.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_BUDDY_DOG'))
check(#g.notes == 2 and g:count('step=refused') == 3, 'other inactive items are not this module\'s')
-- Any co-op mission (not only free roam): a story mission with world sync on.
g = Fixture('guest', {loadout = true, mission = 10036, gone = false})
g:poll()
check(g:menu('MSN_DROP_LOADOUT') == false and g:menu('MSN_DROP_VEHICLE') == false and
    g:has('step=guard;path=idroid_drop_loadout;via=menu_inactive;mission=10036'), 'any co-op mission')
-- The first pass applies even while the iDroid is open.
g = Fixture('guest', {loadout = true})
g.idroid = true
g:poll()
check(g:menu('MSN_DROP_LOADOUT') == false and g:menu('MSN_DROP_VEHICLE') == false, 'the first pass applies with the iDroid open')
-- A link drop inside the co-op mission keeps them inactive; the final stop activates them again (TppTerminal.SetUp's state).
g = Fixture('guest', {loadout = true})
for _, f in ipairs(g.W.pairedHooks) do f(false) end
g:poll()
g.W.paired = false
g:polls(10)
check(g:menu('MSN_DROP_LOADOUT') == false and g:menu('MSN_DROP_VEHICLE') == false and not g:has('step=unguard'),
    'unpaired inside the co-op mission: still inactive')
for _, f in ipairs(g.W.stopHooks) do f(true) end
check(g:menu('MSN_DROP_LOADOUT') == true and g:menu('MSN_DROP_VEHICLE') == true and
    g:has('event=vehicle_rules;step=unguard;path=idroid_drop_loadout;mission=30020'), 'final stop: active again')
g:polls(10)
check(g:menu('MSN_DROP_LOADOUT') == true and g:menu('MSN_DROP_VEHICLE') == true, 'and stays active until the next pairing')
-- Never paired: a solo mission is never touched; a paused game (not in game) waits.
g = Fixture('guest', {loadout = true, paired = false})
g:polls(10)
check(#g.menus == 0, 'never paired: untouched')
g = Fixture('guest', {loadout = true})
g.inGame = false
g:polls(10)
check(#g.menus == 0, 'not in game: untouched')
-- The host's iDroid is never touched (its loadout drop works), whatever the key.
h = Fixture('host', {loadout = true})
h:polls(20)
h.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_LOADOUT'))
check(#h.menus == 0 and #h.notes == 0 and not h:has('idroid_drop_loadout') and h.R.ownsVehicleDrop() == false,
    'the host: no menu change, no refusal, never the VEHICLES item\'s owner')
check(Fixture('guest').R.ownsVehicleDrop() == false, 'the key off: world_relief_sync.lua keeps the VEHICLES item')

-- Status (debug vehicle_gone_status, read only): the module's counters after VEHICLE_GONE's.
h = Fixture('host', {brk = true, credit = true})
h:add(25608, -653, 877.7, 1, 'veh_lv_0000')
h:guestAt(-650, 877.7)
hostReq(h)
local st = h.V.status()
check(st:find('^vgone;on=1;role=host;') and st:find(';vr=1100;j=1;a=1;r=0;q=0;s=0;c=0', 1, true) and #st <= 160, 'status: ' .. st)
-- A long gone list: the base part is cut, never the module's counters.
h = Fixture('host', {brk = true})
for i = 1, 40 do h.V.gone[25700 + i] = true end
st = h.V.status()
local tail = ';vr=1000;j=0;a=0;r=0;q=0;s=0;c=0'
check(#st <= 160 and st:sub(-#tail) == tail, 'status with a long gone list keeps the counters: ' .. st)
print('world_vehicle_rules_lua_check: ' .. checks .. ' checks PASS')
