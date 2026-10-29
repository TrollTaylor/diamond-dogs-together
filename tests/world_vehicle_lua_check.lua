-- 0.13.6 online-vehicle-gone (docs/world-sync-protocol.md section 16): the VEHICLE_GONE
-- module (lua/coop/world_vehicle.lua) against a fake world and fake TppVehicle2 pool, then
-- with world_sync.lua (the observer hook for unmapped ids and CFG r[15]). No game process.
-- Usage: lua51 world_vehicle_lua_check.lua <world_vehicle.lua> [world_sync.lua]
-- Before this branch the module and the hook did not exist: every check here failed.
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
local SRC = slurp(assert(arg[1]))
check(#SRC:gsub('\r\n', '\n') <= 32000, 'the chunk (LF, as CMake splices it) is within the 32000-byte world chunk limit')

-- A fake TppVehicle2 pool: id -> {alive, released, x, y, z (m), type, riders}. As in the engine
-- (TppVehicle2_LuaCommands 0xB305B0 / instance dispatcher 0xB2B710): a released pool slot
-- (Despawn, c:release) answers IsAlive false and nothing else; a wreck (alive false, still in
-- the pool) still answers GetPosition / GetVehicleType / GetRiderId.
local function Fixture(role, on, mode)
    local c = {logs = {}, tx = {}, despawns = {}, seized = {}, clock = 100000, rid = 0, mission = 30050}
    c.pool = {}
    c.names = {}
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.vars = {missionCode = 30050, playerVehicleGameObjectId = 65535, playerPosX = 0, playerPosY = 0, playerPosZ = 0}
    env.PlayerStatus = {ON_VEHICLE = 4}
    env.PlayerInfo = {OrCheckStatus = function(s) return s[1] == 4 and c.riding == true end}
    env.Tpp = {IsVehicle = function(id) return type(id) == 'number' and id >= 25600 and id <= 26111 and not c.notVehicle end}
    local function vector(v) return {GetX = function() return v.x end, GetY = function() return v.y end, GetZ = function() return v.z end} end
    local W = {OP = {RECEIVED = 3, REJECT = 4, RIDE_BODY = 42, RIDE_FAST = 44}, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {},
        active = true, paired = true, eff = 3, mission = 30050, epoch = 7, host = role == 'host', role = role == 'host' and 1 or 2,
        mode = mode or 1, vehicleGoneOn = on ~= false}
    W.now = function() return c.clock end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    W.tx = function(...)
        c.tx[#c.tx + 1] = {...}
        if c.txFail then return false, 5 end
        return true
    end
    W.nextRid = function() c.rid = c.rid % 65535 + 1; return c.rid end
    W.tick = function() c.ticks = (c.ticks or 0) + 1 end
    W.api = {
        send = function(target, cmd)
            if type(target) == 'table' then
                assert(target.type == 'TppVehicle2' and cmd.id == 'Despawn', 'only the TppVehicle2 class Despawn')
                if c.despawnError then error('fixture') end
                c.despawns[#c.despawns + 1] = cmd
                local id = cmd.locator and c.names[cmd.locator] or cmd.index and 25600 + cmd.index
                if id and c.pool[id] then c.pool[id].alive, c.pool[id].released = false, true end
                return true
            end
            local v = c.pool[target]
            if c.sendError then error('fixture') end
            if not v then return nil end
            if cmd.id == 'IsAlive' then return v.alive == true and not v.released end
            if v.released then return nil end
            if cmd.id == 'Seize' then
                if c.seizeError then error('fixture') end
                c.seized[#c.seized + 1] = {id = target, options = cmd.options}
                return nil
            end
            if cmd.id == 'SetBodyLife' then
                -- The engine's own destruction (hidden run d407f115): a wreck, still in the pool.
                c.lifeSet = c.lifeSet or {}
                c.lifeSet[#c.lifeSet + 1] = {id = target, life = cmd.life}
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
            error('unexpected instance command ' .. tostring(cmd.id))
        end,
        id = function(class, name)
            assert(class == 'TppVehicle2', 'vehicle locators only')
            return c.names[name] or 65535
        end,
        str32 = function(s) return #s * 1000 + s:byte(1) end
    }
    W.M, W.kind = {}, {}
    -- world_ride_pose.lua's RIDE_FAST / RIDE_BODY handlers, reduced to their tracks (W.ridePose).
    W.handlers[44] = function(v)
        local rp = W.ridePose
        if rp and v[2] == 3 then rp.fast.tracks[v[3]] = rp.fast.tracks[v[3]] or {samples = {}} end
    end
    W.handlers[42] = function(v)
        local rp = W.ridePose
        if rp and v[2] == 3 then rp.fast.tracks[v[3]] = nil end
    end
    env.e = {MgsCoopWorld = W}
    setfenv(assert(loadstring(SRC)), env)()
    c.env, c.W, c.V, c.H = env, W, W.vehicleGone, W.handlers
    function c:add(id, x, z, t, name) self.pool[id] = {alive = true, x = x, y = 1, z = z, type = t or 1}; if name then self.names[name] = id end end
    function c:release(id) self.pool[id].alive, self.pool[id].released = false, true end
    function c:poll(ms) self.clock = self.clock + (ms or 300); self.W.tick() end
    function c:has(pattern) for _, l in ipairs(self.logs) do if l:find(pattern) then return l end end end
    function c:sent(op) local n = {} for _, t in ipairs(self.tx) do if t[1] == op then n[#n + 1] = t end end return n end
    for _, f in ipairs(W.startHooks) do f() end
    return c
end

-- Module surface.
local c = Fixture('host')
local V = c.V
check(type(V) == 'table' and c.W.OP.VEHICLE_GONE == 46 and c.W.OP.REQ_VEHICLE_GONE == 47, 'W.vehicleGone and the op codes 46 / 47')
check(type(c.H[46]) == 'function' and type(c.H[47]) == 'function' and type(c.H[3]) == 'function' and type(c.H[4]) == 'function',
    'VEHICLE_GONE / REQ handlers and the RECEIVED / REJECT wrappers')
check(V.typed(25600) and V.typed(26111) and not V.typed(25599) and not V.typed(26112) and not V.typed(25603.5) and not V.typed('25603'),
    'a TppVehicle2 id is class 50 (25600..26111)')
check(c.W.kind[c.W.api.str32('VehicleBroken')] == 'VehicleBroken' and c:has('step=on;mission=30050;role=host'),
    'the start hook observes VehicleBroken and logs step=on')

-- Off (the default): inert observer, ignored announcement, REJECT 11 for a request.
c = Fixture('host', false)
c:add(25603, 210.9, 290.1, 1)
c.V.observe('Fulton', 25603, 'local')
c.pool[25603].alive = false
c:poll()
check(#c.tx == 0 and next(c.V.watch) == nil and c.W.kind[c.W.api.str32('VehicleBroken')] == nil, 'off: no watch, nothing sent, no new message kind')
c.H[47]({5, 25603, 1, 1, 210900, 1000, 290100})
local rejects = c:sent(4)
check(#rejects == 1 and rejects[1][2] == 5 and rejects[1][3] == 47 and rejects[1][4] == 11 and rejects[1][5] == 25603,
    'off: the host answers a request with REJECT 11 inactive')
local g = Fixture('guest', false)
g:add(25603, 210.9, 290.1, 1, 'veh_lv_0003')
g.H[46]({1, 25603, 1, 0, 0, 1, 210900, 1000, 290100})
g.H[46]({2, 25603, 1, 0, 0, 1, 210900, 1000, 290100})
check(#g.despawns == 0 and g.pool[25603].alive, 'off: the guest never removes a vehicle')
local ignored = 0
for _, l in ipairs(g.logs) do if l:find('step=ignored;reason=off', 1, true) then ignored = ignored + 1 end end
check(ignored == 1, 'off: one step=ignored line')

-- Host detection: Fulton, read back gone, announced once with the position at the Fulton.
c = Fixture('host')
c:add(25603, 210.9034, 290.1081, 2)
c.V.observe('Fulton', 25603, 'none')
check(c.V.watch[25603] and c.V.watch[25603].x == 210903 and c.V.watch[25603].z == 290108 and c.V.watch[25603].vtype == 2,
    'the host watches a Fultoned vehicle from its pose at the message (mm) and its type')
c.V.observe('FultonInfo', 25603, 'other')
c:poll()
check(#c.tx == 0, 'nothing is sent while the vehicle is still alive (balloon)')
c:release(25603)
c:poll()
local gone = c:sent(46)
check(#gone == 1 and gone[1][2] == 1 and gone[1][3] == 25603 and gone[1][4] == 1 and gone[1][5] == 0 and gone[1][6] == 0 and
    gone[1][7] == 2 and gone[1][8] == 210903 and gone[1][9] == 1000 and gone[1][10] == 290108,
    'VEHICLE_GONE {seq 1, id, cause 1 fulton, actor 0 host, rid 0, type, xyz mm}')
check(c.V.gone[25603] and #c.V.sent == 1 and c:has('step=lost;id=25603;cause=1;role=host'), 'lost once, kept for a re-send')
c.V.observe('Fulton', 25603, 'none')
c:poll()
check(#c:sent(46) == 1, 'a lost id is never announced again by a message')
for _, f in ipairs(c.W.pairedHooks) do f(false) end
check(#c:sent(46) == 2 and c:has('step=resend;n=1'), 'the next PAIRED re-sends the announcement')
-- FultonFailed cancels; a destroyed vehicle (Dead) is cause 2; an alive watch expires.
c:add(25604, 100, 100, 1)
c.V.observe('Fulton', 25604, 'none')
c.V.observe('FultonFailed', 25604, 'none')
c.pool[25604].alive = false
c:poll()
check(c.V.watch[25604] == nil and #c:sent(46) == 2 and c:has('watch_drop;id=25604;reason=fulton_failed'), 'FultonFailed cancels the watch')
c:add(25605, 50, 50, 3)
c.V.observe('Dead', 25605, 'other')
c.pool[25605].alive = false
c:poll()
local last = c:sent(46)[3]
check(last and last[3] == 25605 and last[4] == 2 and last[5] == 0, 'the host announces any destroyed vehicle as cause 2')
c:add(25609, 60, 60, 1)
c.V.observe('Fulton', 25609, 'none')
c.pool[25609] = nil
c:poll()
check(#c:sent(46) == 4 and c:sent(46)[4][3] == 25609, 'an instance that no longer answers (pool slot released) is lost too')
c:add(25606, 0, 0, 1)
c.V.observe('VehicleBroken', 25606, 'none')
for _ = 1, 101 do c:poll(300) end
check(c.V.watch[25606] == nil and #c:sent(46) == 4 and c:has('watch_drop;id=25606;reason=still_alive;dy_mm=0'), 'a watch that stays alive 30 s is dropped')
c.V.observe('Fulton', 4000, 'none')
c.V.observe('Fulton', 25700, 'none')
check(c.V.watch[4000] == nil and c.V.watch[25700] == nil, 'no watch for a non-vehicle id or an id without a pool entry')

-- Guest detection: only its own losses, requested from the host.
g = Fixture('guest')
g:add(25603, 210.9, 290.1, 1)
g.V.observe('Fulton', 25603, 'none')
g.V.observe('FultonInfo', 25603, 'other')
check(g.V.watch[25603] == nil, 'the guest never requests a Fulton that is not its own')
g.V.observe('FultonInfo', 25603, 'local')
check(g.V.watch[25603] ~= nil, 'its own FultonInfo starts a watch')
g:release(25603)
g:poll()
local reqs = g:sent(47)
check(#reqs == 1 and reqs[1][2] == 1 and reqs[1][3] == 25603 and reqs[1][4] == 1 and reqs[1][5] == 1 and reqs[1][6] == 210900 and
    reqs[1][8] == 290100 and g.V.req[1] and #g:sent(46) == 0, 'REQ_VEHICLE_GONE {rid, id, cause 1, type, xyz}; the guest never announces')
g.H[3]({1, 47})
check(g.V.req[1].received, 'RECEIVED marks the request')
g.H[46]({9, 25603, 1, 1, 1, 1, 210900, 1000, 290100})
check(g.V.req[1] == nil and g:has('step=req_done;rid=1;id=25603;result=gone') and #g.despawns == 0,
    'the host announcement with the rid ends the request; the copy already lacks it')
g:add(25604, 10, 10, 1)
g.V.observe('VehicleBroken', 25604, 'none')
check(g.V.watch[25604] == nil, 'a VehicleBroken without a local hit is not the guest\'s')
g.V.observe('Damage', 25604, 'local')
g:poll(5000)
g.V.observe('VehicleBroken', 25604, 'none')
check(g.V.watch[25604] and g.V.watch[25604].cause == 2, 'VehicleBroken within 10 s of a local Damage is the guest\'s (cause 2)')
g.pool[25604].alive = false
g:poll()
local rid2 = g:sent(47)[2][2]
g.H[4]({rid2, 47, 8, 25604, 0, 0})
check(g.V.req[rid2] == nil and g:has('step=req_done;rid=' .. rid2 .. ';id=25604;result=reject;reason=8'), 'a REJECT ends the request')
g:add(25605, 20, 20, 1)
g.V.observe('Dead', 25605, 'local')
g.pool[25605].alive = false
g:poll()
check(#g:sent(47) == 3, 'its own Dead is requested')
g:poll(5000)
check(#g:sent(47) == 4 and g:has('try=2'), 'no RECEIVED within 5 s: sent once more')
g:poll(5000)
check(#g:sent(47) == 4 and g:has('step=req_timeout'), 'then the request times out')
g.W.paired = false
g:add(25606, 30, 30, 1)
g.V.observe('FultonInfo', 25606, 'local')
g:release(25606)
g:poll()
check(#g:sent(47) == 4 and #g.V.unsent == 1, 'unpaired: the request waits')
g.W.paired = true
for _, f in ipairs(g.W.pairedHooks) do f(false) end
check(#g:sent(47) == 5 and #g.V.unsent == 0, 'the next PAIRED sends it')
g.W.paired = false
g:add(25607, 40, 40, 1)
g.V.observe('FultonInfo', 25607, 'local')
g:release(25607)
g:poll()
g.W.paired = true
for _, f in ipairs(g.W.pairedHooks) do f(true) end
check(#g:sent(47) == 5, 'another host epoch drops the waiting requests')

-- Guest receiver: removes its copy only when free; never yanks the local player out.
g = Fixture('guest')
g:add(25603, 210.9, 290.1, 1, 'veh_lv_0003')
g:add(25611, 300, 300, 1)
g.H[46]({1, 25603, 1, 0, 0, 1, 211000, 1000, 290000})
check(#g.despawns == 1 and g.despawns[1].locator == 'veh_lv_0003' and g.despawns[1].index == nil and not g.pool[25603].alive,
    'an authored vehicle is removed with the retail Despawn by locator')
check(g:has('step=applied;id=25603;via=locator;src=rx;ok=1') and g.V.gone[25603], 'applied and read back')
g.H[46]({2, 25611, 1, 0, 0, 1, 300000, 1000, 300000})
check(#g.despawns == 2 and g.despawns[2].index == 11 and g.despawns[2].locator == nil, 'an unnamed (sortie) vehicle by its pool index')
-- M4 wreck parity (E5): with VEHICLE_STATE on too, a destroyed vehicle (cause 2) becomes a wreck here
-- (SetBodyLife 0, read back IsAlive false); a Fulton (cause 1) is still a Despawn; without VEHICLE_STATE
-- a destroyed one is a Despawn as before.
do
    local w = Fixture('guest')
    w.W.vehicleStateOn = true
    w:add(25608, 100, 100, 1, 'veh_lv_0000')
    w:add(25609, 200, 200, 1, 'veh_lv_0001')
    w.H[46]({1, 25608, 2, 0, 0, 1, 100000, 1000, 100000})
    check(#w.despawns == 0 and w.lifeSet and #w.lifeSet == 1 and w.lifeSet[1].id == 25608 and w.lifeSet[1].life == 0 and
        not w.pool[25608].alive and not w.pool[25608].released and w:has('step=applied;id=25608;via=wreck;src=rx;ok=1'),
        'a destroyed vehicle becomes a wreck (SetBodyLife 0), read back')
    w.H[46]({2, 25609, 1, 0, 0, 1, 200000, 1000, 200000})
    check(#w.despawns == 1 and w.despawns[1].locator == 'veh_lv_0001' and #w.lifeSet == 1, 'a Fultoned vehicle is still despawned')
    local h = Fixture('host')
    h.W.vehicleStateOn = true
    h:add(25610, 300, 300, 1, 'veh_lv_0002')
    h.H[47]({9, 25610, 2, 1, 300000, 1000, 300000})
    check(#h.despawns == 0 and h.lifeSet and h.lifeSet[1].id == 25610 and h:has('step=applied;id=25610;via=wreck;src=req;ok=1'),
        'the host wrecks its copy for a guest\'s destroyed request')
    local o = Fixture('guest')
    o:add(25608, 100, 100, 1, 'veh_lv_0000')
    o.H[46]({1, 25608, 2, 0, 0, 1, 100000, 1000, 100000})
    check(#o.despawns == 1 and not o.lifeSet, 'without VEHICLE_STATE a destroyed vehicle is despawned (0.13.6)')
end
g:add(25600, 5, 5, 1)
g.H[46]({3, 25600, 1, 0, 0, 1, 5000, 1000, 5000})
check(#g.despawns == 2 and g.pool[25600].alive and g:has('step=refused;id=25600;src=rx;reason=index0'), 'index 0 (the relief vehicle) is refused')
g:add(25612, 50, 50, 1)
g.riding, g.env.vars.playerVehicleGameObjectId = true, 25612
g.H[46]({4, 25612, 1, 0, 0, 1, 50000, 1000, 50000})
check(#g.despawns == 2 and g.pool[25612].alive and g.V.held[25612] and g:has('step=hold;id=25612;src=rx;reason=local_rider'),
    'the local player\'s own vehicle is held, never removed under it')
for _ = 1, 10 do g:poll() end
check(#g.despawns == 2 and g.pool[25612].alive, 'still held while the local player rides')
g.riding, g.env.vars.playerVehicleGameObjectId = false, 65535
g:poll()
check(#g.despawns == 3 and not g.pool[25612].alive and g.V.held[25612] == nil, 'removed once the local player got out')
g:add(25613, 60, 60, 1)
g.pool[25613].riders = {65535, 65535, 41, 65535}
g.H[46]({5, 25613, 1, 0, 0, 1, 60000, 1000, 60000})
check(g:has('step=hold;id=25613;src=rx;reason=occupied') and g.pool[25613].alive, 'an occupied rider[] seat (back-seat passenger, soldier) holds it')
g.pool[25613].riders = nil
g.W.ridePose = {fast = {tracks = {[25613] = {samples = {}}}}}
g:poll()
check(g:has('step=hold;id=25613;src=rx;reason=partner_ride') and g.pool[25613].alive, 'a live partner RIDE_FAST track holds it')
for _ = 1, 400 do g:poll(300) end
check(g.V.held[25613] == nil and g.pool[25613].alive and g:has('step=hold_timeout;id=25613;src=rx;reason=partner_ride'),
    'a hold ends after 120 s without removing anything')
g.W.ridePose = nil
g:add(25614, 100, 100, 1)
g.H[46]({6, 25614, 1, 0, 0, 1, 150000, 1000, 100000})
check(g.pool[25614].alive and g:has('step=refused;id=25614;src=rx;reason=far'), 'another vehicle at that pool index (50 m away): refused far')
g:add(25615, 400, 400, 2)
g.H[46]({7, 25615, 1, 0, 0, 1, 400000, 1000, 400000})
check(g.pool[25615].alive and g:has('step=refused;id=25615;src=rx;reason=type'), 'another vehicle type at that index: refused')
g:add(25616, 500, 500, 1)
g:add(25617, 704, 700, 1)
g.H[46]({8, 25616, 1, 0, 0, 1, 700000, 1000, 700000})
check(not g.pool[25617].alive and g.pool[25616].alive and g:has('step=matched;id=25617;from=25616'),
    'the pool index differs: the one vehicle of that type within 5 m of the position is the one removed')
g:add(25623, 1500, 1500, 1)
g:add(25624, 1706, 1700, 1)
g.H[46]({12, 25623, 1, 0, 0, 1, 1700000, 1000, 1700000})
check(g.pool[25623].alive and g.pool[25624].alive and g:has('step=refused;id=25623;src=rx;reason=far'), 'a candidate 6 m away is not matched (5 m)')
g:add(25618, 900, 900, 1)
g:add(25619, 903, 900, 1)
g.H[46]({9, 25620, 1, 0, 0, 1, 901000, 1000, 900000})
check(g.pool[25618].alive and g.pool[25619].alive and g:has('reason=ambiguous'), 'two candidates within 5 m: ambiguous, nothing removed')
g.H[46]({10, 25621, 1, 0, 0, 1, 1200000, 1000, 1200000})
check(g:has('step=absent;id=25621;src=rx') and g.V.gone[25621], 'nothing at that index or position: absent (consistent already)')
local before = #g.despawns
g.H[46]({1, 25603, 1, 0, 0, 1, 211000, 1000, 290000})
check(#g.despawns == before, 'a re-sent announcement of an applied id does nothing')
g:add(25622, 1000, 1000, 1)
g.despawnError = true
g.H[46]({11, 25622, 1, 0, 0, 1, 1000000, 1000, 1000000})
g.despawnError = false
check(g.pool[25622].alive and g:has('step=refused;id=25622;src=rx;reason=send_error') and not g.V.gone[25622],
    'a native Despawn error removes nothing and is logged')

-- Host receiver: the guest's request is applied and announced with the rid.
c = Fixture('host')
c:add(25603, 210.9, 290.1, 1)
c.H[47]({7, 25603, 1, 1, 210900, 1000, 290100})
local ann = c:sent(46)
check(#c.despawns == 1 and c.despawns[1].index == 3 and #ann == 1 and ann[1][3] == 25603 and ann[1][5] == 1 and ann[1][6] == 7,
    'the host removes its copy and announces it with actor 1 and the guest\'s rid')
c:add(25604, 20, 20, 1)
c.riding, c.env.vars.playerVehicleGameObjectId = true, 25604
c.H[47]({8, 25604, 1, 1, 20000, 1000, 20000})
check(c.pool[25604].alive and #c:sent(46) == 1 and c:has('step=hold;id=25604;src=req;reason=local_rider'), 'the host driving it: held')
c.riding, c.env.vars.playerVehicleGameObjectId = false, 65535
c:poll()
ann = c:sent(46)
check(not c.pool[25604].alive and #ann == 2 and ann[2][6] == 8, 'removed and announced once the host got out')
c:add(25605, 30, 30, 1)
c.riding, c.env.vars.playerVehicleGameObjectId = true, 25605
c.H[47]({9, 25605, 1, 1, 30000, 1000, 30000})
for _ = 1, 401 do c:poll(300) end
rejects = c:sent(4)
check(c.pool[25605].alive and #rejects == 1 and rejects[1][2] == 9 and rejects[1][4] == 9, 'held past 120 s: REJECT 9 busy')
c.riding = false
c:add(25606, 40, 40, 1)
c.H[47]({10, 25606, 1, 1, 99000, 1000, 99000})
rejects = c:sent(4)
check(c.pool[25606].alive and rejects[2][2] == 10 and rejects[2][4] == 8, 'far: REJECT 8')
c.notVehicle = true
c.H[47]({11, 25607, 1, 1, 0, 0, 0})
c.notVehicle = false
rejects = c:sent(4)
check(rejects[3][2] == 11 and rejects[3][4] == 1, 'not a vehicle: REJECT 1')
c.H[47]({12, 25603, 1, 1, 210900, 1000, 290100})
ann = c:sent(46)
check(#c.despawns == 2 and ann[#ann][3] == 25603 and ann[#ann][6] == 12, 'a request for an id the host already lost is answered with the announcement')
c.H[47]({13, 25640, 1, 1, 0, 0, 0})
ann = c:sent(46)
check(ann[#ann][3] == 25640 and ann[#ann][6] == 13 and c:has('step=absent;id=25640;src=req'), 'the host copy lacks it too: announced (consistent)')

-- Lab lever.
c = Fixture('host', true, 2)
c:add(25603, 3, 0, 1)
local ok, msg = c.V.lever(false)
check(not ok and msg == 'vehicle_remove_lab_only' and c.pool[25603].alive, 'the lever refuses outside mode=lab')
c = Fixture('host', false)
c:add(25603, 3, 0, 1)
ok, msg = c.V.lever(true)
check(not ok and msg == 'vehicle_remove_key_off', 'sync needs the key')
c:add(25604, 2, 0, 1)
c.pool[25604].riders = {41, 65535, 65535, 65535}
c:add(25605, 1, 0, 1)
c.riding, c.env.vars.playerVehicleGameObjectId = true, 25605
c:add(25606, 70, 0, 1)
ok, msg = c.V.lever(false)
check(ok and msg:find('vehicle_remove;id=25603;via=index;sync=0;alive=0', 1, true) and not c.pool[25603].alive and c.pool[25604].alive and
    c.pool[25605].alive and c.pool[25606].alive and #c.tx == 0 and c.V.gone[25603] == 'lab',
    'without sync: the nearest free vehicle on this copy only (never the local one, an occupied one or one beyond 60 m)')
check(c.V.status():find('gone=25603L', 1, true), 'status marks a lab-only removal')
c.V.observe('Fulton', 25603, 'none')
check(c.V.watch[25603] == nil, 'a lab-only removal is never announced')
c = Fixture('host')
c:add(25607, 4, 4, 1)
ok, msg = c.V.lever(true)
ann = c:sent(46)
check(ok and #ann == 1 and ann[1][3] == 25607 and ann[1][4] == 3 and ann[1][5] == 2, 'host sync: announced as cause 3 lab, actor 2')
g = Fixture('guest')
g:add(25607, 4, 4, 1)
ok, msg = g.V.lever(true)
reqs = g:sent(47)
check(ok and #reqs == 1 and reqs[1][3] == 25607 and reqs[1][4] == 3, 'guest sync: requested as cause 3')
ok, msg = g.V.lever(false)
check(not ok and msg:find('vehicle_none', 1, true), 'nothing left nearby')
check(g.V.status():find('vgone;on=1;role=guest;gone=25607', 1, true), 'status names the role and the lost ids')
c = Fixture('host', false)
c:add(25603, 3, 0, 1)
ok, msg = c.V.lever(false, true)
check(not ok and msg == 'vehicle_remove_key_off' and #c.seized == 0, 'fulton needs the key')
g = Fixture('guest')
g:add(25608, 3, 0, 1)
ok, msg = g.V.lever(true, true)
check(ok and msg:find('vehicle_remove;id=25608;via=seize;sync=1;fulton=1', 1, true) and #g.seized == 1 and g.seized[1].id == 25608 and
    g.seized[1].options[1] == 'Fulton' and #g.seized[1].options == 1 and #g.despawns == 0 and g.V.watch[25608].cause == 1,
    'fulton: the stock Seize {options={"Fulton"}} on this copy, watched as a Fulton (no Despawn)')
g.V.observe('FultonInfo', 25608, 'local')
check(g:has('step=watch;id=25608;m=Seize') and not g:has('step=watch;id=25608;m=FultonInfo'), 'the stock messages that follow do not start a second watch')
g:release(25608)
g:poll()
reqs = g:sent(47)
check(#reqs == 1 and reqs[1][3] == 25608 and reqs[1][4] == 1 and g:has('step=lost;id=25608;cause=1;role=guest'), 'the readback loses it: requested as cause 1')
g = Fixture('guest', true, 2)
g:add(25608, 3, 0, 1)
ok, msg = g.V.lever(true, true)
check(not ok and msg == 'vehicle_remove_lab_only' and #g.seized == 0, 'fulton is lab only')

-- Error isolation: a failing poll turns only this module off.
c = Fixture('host')
c:add(25603, 1, 1, 1)
c.V.observe('Fulton', 25603, 'none')
c:release(25603)
c.W.tx = function() error('fixture') end
local tickOk = pcall(c.W.tick)
c.clock = c.clock + 300
tickOk = tickOk and pcall(c.W.tick)
check(tickOk and c.V.failed and c:has('step=error') and not c.V.on(), 'a module error is logged once and only this module stops')

-- 0.13.6 review fixes. Each block below failed against the first module (e27dab2).
-- A wreck at the reported spot is already gone: never matched to a live neighbour (guest rx, host req).
g = Fixture('guest')
g:add(25603, 210, 290, 1); g.pool[25603].alive = false
g:add(25604, 213, 290, 1)
g.H[46]({1, 25603, 2, 0, 0, 1, 210000, 1000, 290000})
check(g.pool[25604].alive and #g.despawns == 0 and not g:has('step=matched') and g:has('step=absent;id=25603;src=rx;reason=wreck'),
    'guest: a wreck at that id is consistent already; the neighbour 3 m away stays')
c = Fixture('host')
c:add(25603, 210, 290, 1); c.pool[25603].alive = false
c:add(25604, 213, 290, 1)
c.H[47]({5, 25603, 2, 1, 210000, 1000, 290000})
ann = c:sent(46)
check(c.pool[25604].alive and #c.despawns == 0 and #ann == 1 and ann[1][3] == 25603 and ann[1][6] == 5,
    'host: a wreck answers the request (announced as the requested id), the neighbour stays')
-- A held item whose id this copy lost itself meanwhile is done (never matched to another).
g = Fixture('guest')
g:add(25605, 50, 50, 1, 'veh_lv_0005')
g:add(25606, 52, 50, 1)
g.riding, g.env.vars.playerVehicleGameObjectId = true, 25605
g.H[46]({1, 25605, 1, 0, 0, 1, 50000, 1000, 50000})
check(g.V.held[25605] and g:has('reason=local_rider'), 'held under the local rider')
g.riding, g.env.vars.playerVehicleGameObjectId = false, 65535
g.V.gone[25605] = true; g:release(25605)
g:poll()
check(g.V.held[25605] == nil and g.pool[25606].alive and #g.despawns == 0 and g:has('step=absent;id=25605;src=rx;reason=lost_here'),
    'a held id this copy lost itself: done, the neighbour 2 m away stays')
-- Swapped pool indices within 40 m: the id is trusted only as the nearest of its type.
c = Fixture('host')
c:add(25610, 520, 500, 1)
c:add(25612, 500, 500, 1)
c.H[47]({7, 25610, 1, 1, 500000, 1000, 500000})
ann = c:sent(46)
check(c.pool[25610].alive and not c.pool[25612].alive and c:has('step=matched;id=25612;from=25610') and #ann == 1 and ann[1][3] == 25610 and ann[1][6] == 7,
    'host: index 10 is another jeep 20 m away; the one at the spot (index 12) goes, answered as the guest\'s 25610')
c.H[47]({8, 25610, 1, 1, 500000, 1000, 500000})
ann = c:sent(46)
check(c.pool[25610].alive and #ann == 2 and ann[2][3] == 25610 and ann[2][6] == 8 and #c.despawns == 1,
    'a re-request of an answered (matched) id is answered again and removes nothing more')
g = Fixture('guest')
g:add(25620, 310, 300, 1)
g:add(25621, 300, 318, 1)
g:add(25622, 302, 300, 1)
g.H[46]({1, 25620, 1, 0, 0, 1, 302000, 1000, 300000})
check(g.pool[25620].alive and not g.pool[25622].alive and g:has('step=matched;id=25622;from=25620'),
    'guest: the announced index is 8 m off and another of its type sits at the spot: that one goes')
g:add(25624, 800, 800, 1)
g:add(25625, 802, 800, 1)
g.H[46]({3, 25625, 1, 0, 0, 1, 800000, 1000, 800000})
check(g.pool[25624].alive and g.pool[25625].alive and g:has('step=refused;id=25625;src=rx;reason=ambiguous'),
    'the announced index 2 m off, another of its type at the spot: both within 5 m, ambiguous, nothing removed')
g:add(25630, 600, 600, 1)
g:add(25631, 600, 606, 1)
g.H[46]({2, 25630, 1, 0, 0, 1, 600000, 1000, 603000})
check(g.pool[25630].alive and g.pool[25631].alive and g:has('step=refused;id=25630;src=rx;reason=ambiguous'),
    'another of its type as near as the announced index (3 m each): ambiguous, nothing removed')
g:add(25632, 1100, 1100, 1)
g:add(25633, 1100, 1117, 1)
g.H[46]({4, 25632, 1, 0, 0, 1, 1100000, 1000, 1110000})
check(g.pool[25632].alive and g.pool[25633].alive and g:has('step=refused;id=25632;src=rx;reason=not_nearest'),
    'the announced index 10 m off, another of its type 7 m off (none within 5 m): refused not_nearest')
-- The host's answer to a matched request is in the guest's index space: the guest removes nothing more.
g = Fixture('guest')
g:add(25610, 210, 290, 1)
g:add(25612, 230, 290, 1)
g.V.observe('FultonInfo', 25610, 'local')
g:release(25610)
g:poll()
local req = g:sent(47)[1]
c = Fixture('host')
c:add(25610, 500, 500, 1)
c:add(25612, 210, 290, 1)
c.H[47]({req[2], req[3], req[4], req[5], req[6], req[7], req[8]})
ann = c:sent(46)[1]
check(ann and ann[3] == 25610 and ann[6] == req[2] and not c.pool[25612].alive and c.pool[25610].alive,
    'host: matched its 25612, answered as 25610 with the rid')
g.H[46]({ann[2], ann[3], ann[4], ann[5], ann[6], ann[7], ann[8], ann[9], ann[10]})
check(g.pool[25612].alive and #g.despawns == 0 and g.V.req[req[2]] == nil and g:has('step=req_done;rid=' .. req[2] .. ';id=25610;result=gone'),
    'guest: the answer ends its request; its 25612 (another jeep 20 m away) stays')
-- A re-sent announcement with an old rid never closes another pending request.
g = Fixture('guest')
g:add(25605, 20, 20, 1)
g.V.observe('Dead', 25605, 'local')
g.pool[25605].alive = false
g:poll()
local rid5 = g:sent(47)[1][2]
g.H[46]({3, 25640, 1, 1, rid5, 1, 900000, 1000, 900000})
check(g.V.req[rid5] and g.V.req[rid5].id == 25605 and not g:has('req_done'), 'GONE{25640, rid} leaves the pending request for 25605 open')
-- Never while unlinked; the partner's ride is latched past an unpair and a datagram gap.
g = Fixture('guest')
g.W.ridePose = {fast = {tracks = {}}}
g:add(25613, 300, 300, 1, 'veh_lv_0005')
g.H[44]({1, 3, 25613, 300000, 1000, 300000, 0})
g.H[46]({2, 25613, 1, 0, 0, 1, 300000, 1000, 300000})
check(g.V.held[25613] and g:has('step=hold;id=25613;src=rx;reason=partner_ride'), 'held while the partner drives it')
g.W.paired, g.W.eff, g.W.ridePose.fast.tracks = false, 0, {}
g:poll(); g:poll(5000); g:poll(6000)
check(g.pool[25613].alive and #g.despawns == 0 and g:has('step=hold;id=25613;src=rx;reason=unpaired'), 'unpaired: held, nothing removed')
g.W.paired, g.W.eff = true, 3
g:poll()
check(g.pool[25613].alive and #g.despawns == 0, 're-paired, no track yet: the latch still holds it')
g:poll(9000)
check(g.pool[25613].alive, 'within 10 s of the re-pair: still held')
g:poll(2000)
check(not g.pool[25613].alive and g:has('step=applied;id=25613;via=locator;src=rx;ok=1'), 'no datagram for 10 s linked: removed')
g = Fixture('guest')
g.W.ridePose = {fast = {tracks = {}}}
g:add(25614, 300, 300, 1, 'veh_lv_0006')
g.H[44]({1, 3, 25614, 300000, 1000, 300000, 0})
g.W.ridePose.fast.tracks = {}
g:poll(2000)
g.H[46]({2, 25614, 1, 0, 0, 1, 300000, 1000, 300000})
check(g.pool[25614].alive and g:has('step=hold;id=25614;src=rx;reason=partner_ride'), 'a stale track (datagram gap): the latch holds it')
g.H[42]({7, 3, 25614, 300000, 1000, 300000, 0, 0, 65535})
g:poll()
check(not g.pool[25614].alive, 'the exit RIDE_BODY (occupied 0) releases it: removed')
-- The index form only at Mother Base: elsewhere an unnamed / unscanned vehicle is refused.
g = Fixture('guest')
g.W.mission = 30010
g:add(25611, 300, 300, 1)
g:add(25615, 400, 400, 1, 'veh_wav_0002')
g.H[46]({1, 25611, 1, 0, 0, 1, 300000, 1000, 300000})
g.H[46]({2, 25615, 1, 0, 0, 1, 400000, 1000, 400000})
check(g.pool[25611].alive and g:has('step=refused;id=25611;src=rx;reason=no_locator') and not g.pool[25615].alive and
    g.despawns[1].locator == 'veh_wav_0002' and #g.despawns == 1, 'outside 30050: no index Despawn (no_locator); veh_wav_* by its locator')
-- A Fulton counts only once the instance leaves the pool, or after 10 s not alive with no FultonFailed.
c = Fixture('host')
c:add(25607, 10, 10, 1)
c.V.observe('Fulton', 25607, 'none')
c.pool[25607].alive = false
c:poll()
check(#c:sent(46) == 0 and c.V.watch[25607] and c:has('step=settle;id=25607'), 'IsAlive false while still in the pool: settling')
c.V.observe('FultonFailed', 25607, 'none')
c.pool[25607].alive = true
for _ = 1, 50 do c:poll(300) end
check(#c:sent(46) == 0 and c.pool[25607].alive and c:has('watch_drop;id=25607;reason=fulton_failed'), 'a failed balloon: nothing announced')
c:add(25608, 20, 20, 1)
c.V.observe('Fulton', 25608, 'none')
c.pool[25608].alive = false
c:poll(); c:poll(9000)
check(#c:sent(46) == 0, 'not yet after 9 s')
c:poll(1500)
check(#c:sent(46) == 1 and c:sent(46)[1][3] == 25608, 'not alive for 10 s: lost')
-- A request for a vehicle this copy is losing itself waits for its own watch, then is answered.
c = Fixture('host')
c:add(25609, 30, 30, 1)
c:add(25610, 31, 30, 1)
c.V.observe('Fulton', 25609, 'none')
c.H[47]({9, 25609, 1, 1, 30000, 1000, 30000})
check(c.V.held[25609] and c:has('step=hold;id=25609;src=req;reason=own_watch') and c.pool[25610].alive, 'own watch: held')
c:release(25609)
c:poll()
ann = c:sent(46)
check(#ann == 2 and ann[1][3] == 25609 and ann[1][6] == 0 and ann[2][3] == 25609 and ann[2][6] == 9 and c.pool[25610].alive and #c.despawns == 0,
    'its own loss is announced, then the request is answered; the neighbour stays')
-- Handler errors turn only this module off (world sync goes on).
g = Fixture('guest')
g.V.decide = function() error('fixture') end
g:add(25603, 1, 1, 1)
local rxOk = pcall(g.H[46], {1, 25603, 1, 0, 0, 1, 1000, 1000, 1000})
check(rxOk and g.V.failed and g:has('step=error') and not g.V.on(), 'a VEHICLE_GONE handler error is caught')
c = Fixture('host')
c.V.decide = function() error('fixture') end
c:add(25603, 1, 1, 1)
local reqOk = pcall(c.H[47], {1, 25603, 1, 1, 1000, 1000, 1000})
c.H[47]({2, 25603, 1, 1, 1000, 1000, 1000})
rejects = c:sent(4)
check(reqOk and c.V.failed and c:has('step=error') and #rejects == 1 and rejects[1][4] == 11, 'a REQ handler error is caught; later requests get REJECT 11')
g = Fixture('guest')
g.V.req = setmetatable({}, {__index = function() error('fixture') end})
local recOk = pcall(g.H[3], {1, 47})
check(recOk and g.V.failed, 'a RECEIVED wrapper error is caught')

-- With world_sync.lua: the observer hook (an unmapped id goes to the vehicle module) and CFG r[15].
if arg[2] then
    local syncSrc = slurp(arg[2])
    local function Composed(r15, role)
        local logs, clock = {}, 5000
        local env = setmetatable({}, {__index = _G})
        env._G = env
        env.vars, env.mvars, env.svars = {missionCode = 30050}, {}, {}
        env.Fox = {StrCode32 = function(s) local h = 0; for i = 1, #s do h = (h * 31 + s:byte(i)) % 2147483647 end; return h end}
        env.Time = {GetRawElapsedTimeSinceStartUp = function() return clock / 1000 end}
        env.GameObject = {NULL_ID = 65535, GetGameObjectId = function() return 65535 end,
            SendCommand = function(id, cmd)
                if cmd.id == 'IsAlive' then return id == 25603 end
                if cmd.id == 'GetPosition' then return {GetX = function() return 1 end, GetY = function() return 2 end, GetZ = function() return 3 end} end
                if cmd.id == 'GetVehicleType' then return 1 end
            end}
        env.TppMission = {CheckMissionState = function() return true end, IsMissionStart = function() return true end}
        env.Mission = {ON_MESSAGE_RESULT_RESEND = 9}
        env.Tpp = {IsVehicle = function(id) return id >= 25600 and id <= 26111 end}
        env.e = {}
        setfenv(assert(loadstring(syncSrc)), env)()
        local W = env.e.MgsCoopWorld
        setfenv(assert(loadstring(SRC)), env)()
        W.cfg = function() return {3, role, 1, 1, 7, 0, 1, 1, 0, 0, 0, 0, 0, 0, r15} end
        W.log = function(text) logs[#logs + 1] = text end
        W.begin()
        return W, logs, env
    end
    local W, logs = Composed(1, 2)
    check(W.vehicleGoneOn == true and W.vehicleGone.on() and W.kind[W.M.VehicleBroken] == 'VehicleBroken',
        'CFG r[15] = 1 turns the module on and adds the VehicleBroken message')
    W.attClass = function(att) return att == 77 and 'local' or 'other' end
    W.observe(W.M.GameObject, W.M.FultonInfo, 25603, 77)
    check(W.vehicleGone.watch[25603] ~= nil, 'W.observe hands an unmapped vehicle id to the vehicle module (the guest\'s own FultonInfo)')
    local seen = false
    for _, l in ipairs(logs) do if l:find('event=obs;m=FultonInfo;ent=0;id=25603;att=local', 1, true) then seen = true end end
    check(seen, 'the unchanged event=obs line still comes first')
    W.vehicleGone.observe = function() error('fixture') end
    local observed = pcall(W.observe, W.M.GameObject, W.M.Fulton, 25604, 1)
    check(observed and not W.failed, 'an error in the vehicle hook never fails world sync')
    W = Composed(0, 1)
    check(W.vehicleGoneOn == false and not W.vehicleGone.on() and W.M.VehicleBroken == nil, 'CFG r[15] = 0 (and an older loader without it): off')
    W = Composed(nil, 1)
    check(W.vehicleGoneOn == false, 'a missing r[15] is off')
end
print('world_vehicle_lua_check: ' .. checks .. ' checks PASS')
