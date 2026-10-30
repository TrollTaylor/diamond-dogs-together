-- The guest's vehicle damage report (the vehicle fixes of 2026-10-29; docs/vehicles-companions-design.md 8.14,
-- docs/world-sync-protocol.md section 20; the 0.14.4 review finding game-vstate-heals-guest-damage):
-- lua/coop/world_vehicle_damage.lua on top of world_vehicle.lua (VEHICLE_GONE), world_vehicle_state.lua (VEHICLE_STATE,
-- its life hooks) and world_vehicle_rules.lua, against a fake world and a fake TppVehicle2 pool. No game process.
-- Usage: lua51 world_vehicle_damage_lua_check.lua <world_vehicle_damage.lua> <world_vehicle.lua> <world_vehicle_state.lua>
--        <world_vehicle_rules.lua>
-- Written before the module (and before VEHICLE_STATE's life hooks): without them every check after the chunk size failed.
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
local DMG, GONE, STATE, RULES = slurp(assert(arg[1])), slurp(assert(arg[2])), slurp(assert(arg[3])), slurp(assert(arg[4]))
check(#DMG:gsub('\r\n', '\n') <= 32000, 'the chunk (LF, as CMake splices it) is within the 32000-byte world chunk limit')
local function str32(s) local h = 7; for i = 1, #s do h = (h * 33 + s:byte(i)) % 4294967291 end; return h end
local OP, STATE_OP = 58, 51

-- opts: role 'host'|'guest', damage (the key, default on), state (VEHICLE_STATE, default on), paired (default true).
local function Fixture(role, opts)
    opts = opts or {}
    local c = {logs = {}, tx = {}, lifeSet = {}, clock = 100000}
    c.pool, c.names = {}, {}
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.vars = {missionCode = 30020, playerVehicleGameObjectId = 65535, playerPosX = 0, playerPosY = 0, playerPosZ = 0}
    env.PlayerStatus = {ON_VEHICLE = 4}
    env.PlayerInfo = {OrCheckStatus = function(s) return s[1] == 4 and c.riding == true end}
    env.Tpp = {IsVehicle = function(id) return type(id) == 'number' and id >= 25600 and id <= 26111 end}
    local function vector(v) return {GetX = function() return v.x end, GetY = function() return v.y end, GetZ = function() return v.z end} end
    local W = {OP = {RECEIVED = 3, REJECT = 4, RIDE_BODY = 42, RIDE_FAST = 44}, handlers = {}, startHooks = {}, pairedHooks = {},
        unpairedHooks = {}, stopHooks = {}, active = true, paired = opts.paired ~= false, eff = 3, mission = 30020, epoch = 7,
        host = role == 'host', role = role == 'host' and 1 or 2, mode = 1, vehicleGoneOn = true, vehicleStateOn = opts.state ~= false,
        vehicleBreakOn = true, vehicleDamageOn = opts.damage ~= false}
    W.now = function() return c.clock end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    W.get = function(t, k)
        if t == nil then return nil end
        local ok, v = pcall(function() return t[k] end)
        if ok then return v end
    end
    W.tx = function(...)
        if c.txFails then return false end
        c.tx[#c.tx + 1] = {...}
        return true
    end
    c.rid = 0
    W.nextRid = function() c.rid = c.rid % 65535 + 1; return c.rid end
    W.tick = function() end
    W.inGame = function() return true end
    W.note = function() return true end
    W.observe = function() end
    W.h = str32
    W.downed = {records = {}}
    W.receipt = function(source)
        local first, n = source:match("^return 'MGS_COOP_VEHICLE_LIFE_V1' %-%- (%d+) (%d+)$")
        assert(first, 'only the life receipt: ' .. source)
        -- The native read takes 1..32 ids (game_script_observer.cpp SelectVehicleLifeReceipt).
        if tonumber(n) < 1 or tonumber(n) > 32 then return {refused = 'request'} end
        c.reads = (c.reads or 0) + 1
        local r = {c = c.poolCount or 24}
        for id = tonumber(first), tonumber(first) + tonumber(n) - 1 do
            local v = c.pool[id]
            if v and not v.released then r[id] = {v.alive and v.life or 65535, 0, 4, 1} end
        end
        return r
    end
    W.api = {
        send = function(target, cmd)
            if type(target) == 'table' then
                assert(target.type == 'TppVehicle2' and cmd.id == 'Despawn', 'only the TppVehicle2 class Despawn')
                return true
            end
            local v = c.pool[target]
            if not v then return nil end
            if cmd.id == 'IsAlive' then return v.alive == true and not v.released end
            if v.released then return nil end
            if cmd.id == 'SetBodyLife' then
                c.lifeSet[#c.lifeSet + 1] = {id = target, life = cmd.life}
                v.life = cmd.life
                if cmd.life == 0 then v.alive = false end
                return nil
            end
            if cmd.id == 'GetPosition' then return vector(v), 0 end
            if cmd.id == 'GetVehicleType' then return v.type, 0 end
            if cmd.id == 'GetRiderId' then
                local r = {}
                for i = 1, 4 do r[i] = v.riders and v.riders[i] or 65535 end
                return r
            end
            if cmd.id == 'SetPosition' then return nil end
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
    function c:add(id, x, z, name, extra)
        self.pool[id] = {alive = true, x = x, y = 1, z = z, type = 1, life = 2300}
        for k, v in pairs(extra or {}) do self.pool[id][k] = v end
        if name then self.names[name] = id end
    end
    -- The pool is filled before the chunks run their start hooks (VEHICLE_STATE resolves the locator names there).
    for _, a in ipairs(opts.pool or {}) do c:add(a[1], a[2], a[3], a[4], a[5]) end
    setfenv(assert(loadstring(GONE)), env)()
    setfenv(assert(loadstring(STATE)), env)()
    setfenv(assert(loadstring(RULES)), env)()
    setfenv(assert(loadstring(DMG)), env)()
    c.env, c.W, c.V, c.S, c.D, c.H = env, W, W.vehicleGone, W.vehicleState, W.vehicleDamage, W.handlers
    function c:poll(ms) self.clock = self.clock + (ms or 250); self.W.tick() end
    function c:polls(n, ms) for _ = 1, n do self:poll(ms) end end
    function c:has(pattern) for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then return l end end end
    function c:count(pattern) local n = 0; for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then n = n + 1 end end; return n end
    function c:sent(op) local n = {} for _, t in ipairs(self.tx) do if t[1] == op then n[#n + 1] = t end end return n end
    function c:at(x, z) self.env.vars.playerPosX, self.env.vars.playerPosZ = x, z end
    -- The guest's DOWNED_STATUS record on the host (world_downed.lua D.records[2]): pos in mm, at = receive time.
    function c:guestAt(x, z, age) self.W.downed.records[2] = {pos = {x * 1000, 1000, z * 1000}, at = self.clock - (age or 0)} end
    -- The host's VEHICLE_STATE for id at life, flags (1 a change, 4 a refresh).
    function c:state(id, life, flags, hash)
        local v = self.pool[id]
        local function mm(m) return math.floor(m * 1000 + 0.5) end
        self.H[STATE_OP]({(self.stateSeq or 0) + 1, id, hash or 0, 1, flags or 1, mm(v.x), mm(v.y), mm(v.z), 0, life, 1, 0})
        self.stateSeq = (self.stateSeq or 0) + 1
    end
    for _, f in ipairs(W.startHooks) do f() end
    return c
end
local JEEP = {25608, -653, 877.7, 'veh_lv_0000'}

-- Module surface.
local g = Fixture('guest', {pool = {JEEP}})
check(type(g.D) == 'table' and g.W.OP.VEHICLE_DAMAGE == OP, 'W.vehicleDamage and op 58 (VEHICLE_DAMAGE)')
check(g.D.NEAR_MM == 300000 and g.D.GAP_MS == 500 and g.D.HOLD_MS == 3000 and g.D.RATE_N == 8 and g.D.FRESH_MS == 3000 and g.D.TICK_MS == 250,
    'rules: 300 m, one report per vehicle per 500 ms, a raise held 3 s while a report is in flight, 8 reports per second, a guest ' ..
    'position at most 3 s old, a 250 ms scan')
check(g:has('event=vehicle_damage;step=on;mission=30020;role=guest'), 'the start line')
check(not Fixture('guest', {damage = false}):has('event=vehicle_damage;step=on'), 'key off: no start line')
check(not Fixture('guest', {state = false}):has('event=vehicle_damage;step=on') and not Fixture('guest', {state = false}).D.on(),
    'VEHICLE_STATE off: the module stays off (it rides on VEHICLE_STATE)')

-- Guest: a drop on its own copy near its player is reported.
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
check(#g:sent(OP) == 0 and g.D.known[25608] == 2300, 'the first read is the baseline (nothing sent)')
g.pool[25608].life = 1500
g:poll()
local r = g:sent(OP)
check(#r == 1 and r[1][2] == 1 and r[1][3] == 25608 and r[1][4] == str32('veh_lv_0000') and r[1][5] == 1 and r[1][6] == 0 and
    r[1][7] == 1500 and r[1][8] == 800 and r[1][9] == -653000 and r[1][11] == 877700,
    'VEHICLE_DAMAGE {seq 1, id, locator hash, type, flags 0, life 1500, drop 800, the copy\'s xyz}')
check(g:has('event=vehicle_damage;step=tx;id=25608;life=1500;drop=800;f=0;seq=1') and g.D.known[25608] == 1500, 'logged; agreed at 1500')
g:polls(8)
check(#g:sent(OP) == 1, 'nothing more while the life stays')
-- Drops within 500 ms of a report add up into the next one.
g.pool[25608].life = 1400
g:poll(100)
g.pool[25608].life = 1300
g:poll(100)
check(#g:sent(OP) == 1, 'within 500 ms of the last report: waits')
g:poll(400)
r = g:sent(OP)
check(#r == 2 and r[2][7] == 1300 and r[2][8] == 200, 'then one report of both drops (200)')
-- A raise (VEHICLE_STATE or the engine) moves the agreed life up without a report.
g.pool[25608].life = 2000
g:poll(600)
check(#g:sent(OP) == 2 and g.D.known[25608] == 2000, 'a raise is agreed, never reported')
-- A destruction (life 0, a wreck) is D12's: no report.
g.pool[25608].life = 0
g.pool[25608].alive = false
g:poll(600)
check(#g:sent(OP) == 2 and g.D.known[25608] == nil, 'a destruction: no damage report (D12 requests it)')

-- Guest: who may report.
local function guest1(extra, setup)
    local x = Fixture('guest', {pool = {{25608, -653, 877.7, 'veh_lv_0000', extra}}})
    x:at(-650, 877.7)
    if setup then setup(x) end
    x:polls(2)
    x.pool[25608].life = 1500
    x:poll(600)
    return x
end
g = guest1(nil, function(x) x:at(-1000, 877.7) end)
check(#g:sent(OP) == 0 and g:has('event=vehicle_damage;step=skip;id=25608;reason=far;life=1500;drop=800') and g.D.known[25608] == 1500,
    'the guest\'s player 347 m away: not reported (logged far); this copy keeps it until VEHICLE_STATE says otherwise')
g = guest1(nil, function(x) x.W.ridePose.fast.tracks[25608] = {} end)
check(#g:sent(OP) == 0 and g:has('step=skip;id=25608;reason=partner_ride'), 'the host drives it (the partner\'s ride here): never reported')
g = guest1(nil, function(x) x.S.lrrp[25608] = true end)
check(#g:sent(OP) == 0 and g:has('reason=lrrp'), 'an LRRP truck (each copy\'s own AI): never reported')
g = guest1(nil, function(x) x.V.gone[25608] = true end)
check(#g:sent(OP) == 0, 'a vehicle this copy lost: never reported')
g = Fixture('guest', {pool = {{25600, 5, 5}}})
g:at(5, 5)
g:polls(2)
g.pool[25600].life = 1000
g:poll(600)
check(#g:sent(OP) == 0 and g:has('reason=index0'), 'index 0 (the sortie vehicle, M3): never reported')
g = guest1({riders = {4321}})
check(#g:sent(OP) == 1 and g:sent(OP)[1][6] == 0, 'an NPC-crewed copy the guest shoots: reported')
g = guest1(nil, function(x) x.riding, x.env.vars.playerVehicleGameObjectId = true, 25608 end)
check(#g:sent(OP) == 1 and g:sent(OP)[1][6] == 1, 'the guest\'s own ride: reported with flag 1')
g = guest1(nil, function(x) x.W.vehicleDamageOn = false end)
check(#g:sent(OP) == 0, 'key off: never reported')
-- Unlinked: nothing is read or sent; the drop goes out once linked again.
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
g.W.paired = false
g.pool[25608].life = 1500
g:polls(4, 600)
check(#g:sent(OP) == 0, 'unpaired: nothing sent')
g.W.paired = true
g:poll(600)
check(#g:sent(OP) == 1 and g:sent(OP)[1][8] == 800, 'linked again: the drop goes out')
-- At most 8 reports per second over all vehicles.
local many = {}
for i = 1, 12 do many[i] = {25608 + i, -653 + i, 877.7} end
g = Fixture('guest', {pool = many})
g:at(-650, 877.7)
g:polls(2)
for i = 1, 12 do g.pool[25608 + i].life = 2000 end
g:poll(600)
check(#g:sent(OP) == 8, '12 drops at once: 8 reports in the first second')
g:poll(1000)
check(#g:sent(OP) == 12, 'the other 4 in the next')

-- A pool of more than 32 (the native read's limit) is read in pieces: a drop on the 40th slot is reported too.
g = Fixture('guest', {pool = {{25639, -653, 877.7}}})
g.poolCount = 48
g.S.readLife(25600, 1)
g:at(-650, 877.7)
g:polls(2)
g.pool[25639].life = 1500
g:poll(600)
check(g.S.pool == 48 and #g:sent(OP) == 1 and g:sent(OP)[1][3] == 25639, 'a pool of 48: read as 32 + 16, the 40th slot reported')
-- Guest: VEHICLE_STATE never heals a reported drop while the report is in flight.
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
g.pool[25608].life = 1500
g:poll()
g.clock = g.clock + 100
g:state(25608, 2300, 4, str32('veh_lv_0000'))
check(g.pool[25608].life == 1500 and #g.lifeSet == 0 and g:has('event=vehicle_damage;step=keep;id=25608;here=1500;host=2300;f=4'),
    'the host\'s refresh with the old life 100 ms after the report: kept at 1500 (logged keep)')
-- The host applied it: its state now carries 1500, the same as here.
g.clock = g.clock + 300
g:state(25608, 1500, 1, str32('veh_lv_0000'))
check(g.pool[25608].life == 1500 and #g.lifeSet == 0 and g:has('event=vehicle_state;step=same;id=25608'), 'the host\'s applied life: same')
-- The host's own damage afterwards lowers this copy and is never reported back.
g.clock = g.clock + 1000
g:state(25608, 1200, 1, str32('veh_lv_0000'))
g:polls(4)
check(g.pool[25608].life == 1200 and #g:sent(OP) == 1 and g.D.known[25608] == 1200, 'the host\'s own damage: applied here, never reported back')
-- A host that never applies it (an older partner, a refusal): the raise is held only HOLD_MS.
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
g.pool[25608].life = 1500
g:poll()
g.clock = g.clock + 3100
g:state(25608, 2300, 4, str32('veh_lv_0000'))
check(g.pool[25608].life == 2300 and g.D.known[25608] == 2300 and not g:has('step=keep'),
    'after 3 s the host\'s life is taken again (0.14.4 behaviour with a host that ignores the op)')
-- A drop the scan has not seen yet is reported before VEHICLE_STATE writes (flag 2), and kept.
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
g.pool[25608].life = 1700
g:state(25608, 2300, 4, str32('veh_lv_0000'))
r = g:sent(OP)
check(#r == 1 and r[1][6] == 2 and r[1][7] == 1700 and r[1][8] == 600 and g.pool[25608].life == 1700 and g:has('step=keep;id=25608'),
    'an unscanned drop before the host\'s state: reported first (flag 2), then kept')
-- A send that fails (the world queue) never keeps the lower life: VEHICLE_STATE takes the host's life again.
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
g.txFails = true
g.pool[25608].life = 1700
g:state(25608, 2300, 4, str32('veh_lv_0000'))
check(#g:sent(OP) == 0 and g.pool[25608].life == 2300 and not g:has('step=keep'), 'a failed send: the host life is taken (never kept)')
-- A drop that may not be reported (far) is healed by VEHICLE_STATE as in 0.14.4.
g = Fixture('guest', {pool = {JEEP}})
g:at(-1000, 877.7)
g:polls(2)
g.pool[25608].life = 1700
g:state(25608, 2300, 4, str32('veh_lv_0000'))
check(#g:sent(OP) == 0 and g.pool[25608].life == 2300, 'a drop far from the guest: healed by VEHICLE_STATE (not plausible)')
-- Within 500 ms of a report a new drop cannot go out yet: VEHICLE_STATE keeps it too (it goes out after the gap).
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
g.pool[25608].life = 1500
g:poll()
g.pool[25608].life = 1400
g.clock = g.clock + 200
g:state(25608, 1500, 1, str32('veh_lv_0000'))
check(g.pool[25608].life == 1400 and #g:sent(OP) == 1, 'a second drop within the gap: kept, not raised to the host\'s 1500')
g:poll(400)
check(#g:sent(OP) == 2 and g:sent(OP)[2][7] == 1400 and g:sent(OP)[2][8] == 100, 'and reported after the gap')

-- Host: applies the guest's report.
local function host1(extra)
    local h = Fixture('host', {pool = {{25608, -653, 877.7, 'veh_lv_0000', extra}}})
    h:guestAt(-650, 877.7)
    return h
end
local function report(h, life, drop, id, hash, x, z, flags)
    h.H[OP]({7, id or 25608, hash or str32('veh_lv_0000'), 1, flags or 0, life, drop, (x or -653) * 1000, 1000, (z or 877.7) * 1000})
end
local h = host1()
report(h, 1500, 800)
check(h.pool[25608].life == 1500 and #h.lifeSet == 1 and h.lifeSet[1].life == 1500 and
    h:has('event=vehicle_damage;step=applied;id=25608;from=25608;life=1500;was=2300;drop=800;d_mm=3000;f=0'),
    'the guest\'s drop on the named jeep: the host\'s copy at 1500 (SetBodyLife), logged applied')
check(h.S.dirty[25608] == true, 'VEHICLE_STATE sends the applied life at once (the id is dirty)')
h:poll()
local st = h:sent(STATE_OP)
check(#st >= 1 and st[#st][3] == 25608 and st[#st][11] == 1500, 'the host\'s next VEHICLE_STATE carries life 1500')
-- A hit both copies took counts once: the host's copy already as low or lower is the same.
report(h, 1600, 700)
check(h.pool[25608].life == 1500 and #h.lifeSet == 1 and h:has('event=vehicle_damage;step=same;id=25608;from=25608;life=1600;was=1500'),
    'the host\'s copy already lower: same, nothing written')
-- Never more than the guest's own drop below the host's life (a guest copy that was lower before its hit).
h = host1()
report(h, 500, 300)
check(h.pool[25608].life == 2000 and h:has('step=applied;id=25608;from=25608;life=2000;was=2300;drop=300'),
    'reported life 500 after a 300 drop, the host at 2300: 2000 (the guest\'s drop, not its life)')
-- Never 0 (a destruction is D12's).
h = host1()
report(h, 1, 65535)
check(h.pool[25608].life == 1 and h.pool[25608].alive, 'a drop to 1: life 1, never the wreck')
-- Refusals (logged once per vehicle and reason).
local function refused(h2, reason) return h2:has('event=vehicle_damage;step=refused;id=25608;reason=' .. reason) end
h = host1()
h.W.downed.records[2] = nil
report(h, 1500, 800)
check(h.pool[25608].life == 2300 and refused(h, 'guest_unknown'), 'no guest position: refused (guest_unknown)')
h = host1()
h:guestAt(-650, 877.7, 3500)
report(h, 1500, 800)
check(h.pool[25608].life == 2300 and refused(h, 'guest_unknown'), 'a guest position older than 3 s: refused')
h = host1()
h:guestAt(-1000, 877.7)
report(h, 1500, 800)
check(h.pool[25608].life == 2300 and h:has('step=refused;id=25608;reason=guest_far;d_mm=347000'), 'the guest 347 m away: refused (guest_far)')
report(h, 1400, 900)
check(h:count('step=refused;id=25608;reason=guest_far') == 1, 'logged once per reason')
h = host1()
h.riding, h.env.vars.playerVehicleGameObjectId = true, 25608
report(h, 1500, 800)
check(h.pool[25608].life == 2300 and refused(h, 'local_rider'), 'the host\'s own player rides it: refused (its copy takes its own damage)')
h = host1()
h.S.lrrp[25608] = true
report(h, 1500, 800)
check(h.pool[25608].life == 2300 and refused(h, 'lrrp'), 'an LRRP truck: refused')
h = host1()
h.V.gone[25608] = true
report(h, 1500, 800)
check(h.pool[25608].life == 2300 and refused(h, 'gone_here'), 'lost here: refused')
h = host1()
report(h, 1500, 800, 25608, str32('veh_lv_0009'))
check(h.pool[25608].life == 2300 and refused(h, 'no_locator'), 'a locator this copy does not have: refused')
h = Fixture('host', {pool = {{25600, 5, 5}}})
h:guestAt(5, 5)
report(h, 1000, 1300, 25600, 0, 5, 5)
check(h.pool[25600].life == 2300 and h:has('step=refused;id=25600;reason=index0'), 'index 0: refused (M3)')
h = host1()
h.W.paired = false
report(h, 1500, 800)
check(h.pool[25608].life == 2300 and refused(h, 'unpaired'), 'unlinked: refused')
h = host1()
h.W.vehicleDamageOn = false
report(h, 1500, 800)
report(h, 1400, 900)
check(h.pool[25608].life == 2300 and h:count('event=vehicle_damage;step=ignored;reason=off;id=25608') == 1, 'host key off: ignored (logged once)')
-- An NPC crew or the guest's own ride on the host's copy: applied (the guest's damage to that vehicle).
h = host1({riders = {4321}})
report(h, 1500, 800)
check(h.pool[25608].life == 1500, 'a soldier-crewed copy: applied')
h = host1()
h.W.ridePose.fast.tracks[25608] = {}
report(h, 1500, 800, nil, nil, nil, nil, 1)
check(h.pool[25608].life == 1500 and h:has('step=applied;id=25608;from=25608;life=1500;was=2300;drop=800;d_mm=3000;f=1'),
    'the guest\'s own ride (flag 1): applied')
-- The guest never handles op 58; the host never scans.
g = Fixture('guest', {pool = {JEEP}})
report(g, 1500, 800)
check(g.pool[25608].life == 2300 and not g:has('step=applied'), 'the guest ignores op 58')
h = host1()
h:polls(4)
h.pool[25608].life = 1500
h:polls(4)
check(#h:sent(OP) == 0, 'the host never reports')
-- The final stop forgets every agreed life.
g = Fixture('guest', {pool = {JEEP}})
g:at(-650, 877.7)
g:polls(2)
for _, f in ipairs(g.W.stopHooks) do f(true) end
check(next(g.D.known) == nil and next(g.D.pend) == nil, 'final stop: nothing kept')
print('world_vehicle_damage_lua_check: ' .. checks .. ' checks PASS')
