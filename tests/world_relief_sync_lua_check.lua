-- M3 sortie vehicle on both copies (docs/vehicles-companions-design.md 6.3, route A):
-- lua/coop/world_relief_sync.lua with world_follow_seed.lua, world_follow.lua and world_vehicle.lua
-- on recording game APIs and a fake TppVehicle2 index 0. No game process.
-- Usage: lua51 world_relief_sync_lua_check.lua <world_relief_sync.lua> <world_follow_seed.lua>
--        <world_follow.lua> <world_vehicle.lua>
-- Written before the module: every check failed (no relief seed keys, no W.reliefSync, no op 52).
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(tostring(checks) .. ': ' .. why, 2) end
end
local function slurp(path)
    local f = assert(io.open(path, 'rb'))
    local s = f:read('*a')
    f:close()
    return s
end
check(#slurp(assert(arg[1])):gsub('\r\n', '\n') <= 32000, 'the chunk is within the 32000-byte world chunk limit')
local OP = {RECEIVED = 3, REJECT = 4, TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13,
    RIDE_BODY = 42, RIDE_FAST = 44}
local RID = 25600
local function str32(s)
    local h = 7
    for i = 1, #s do h = (h * 31 + s:byte(i)) % 2147483647 end
    return h
end
local function Fixture(role, o)
    o = o or {}
    local c = {calls = {}, tx = {}, logs = {}, notes = {}, saves = {}, menus = {}, sent = {}, time = 1000, inGame = true,
        idroid = false}
    local env = setmetatable({}, {__index = _G})
    local att = {[0] = 0, [1] = 0, [2] = 0, [3] = 0}
    local life, ammo, paint = {}, {[0] = 0, [1] = 0, [2] = 0}, {}
    for i = 0, 11 do life[i] = 0 end
    for i = 0, 9 do paint[i] = 0 end
    env.vars = {missionCode = o.mission or 30020, locationCode = 20, mbLayoutCode = 0, mbClusterId = 0, playerPosX = 1225,
        playerPosY = 19, playerPosZ = -130, playerRotY = 0, playerVehicleGameObjectId = 65535,
        reliefVehicleCarryType = o.ct or 0, reliefVehicleCarryAttitude = att, reliefVehicleCarryLife = life,
        reliefVehicleCarryAmmo = ammo, reliefVehicleCarryState = 0, userPresetVehiclePaintType = paint}
    env.mvars, env.gvars, env.svars = {}, {}, {}
    c.v0 = {alive = o.alive == true, x = 1227.24, y = 19.43, z = -127.05, rot = -1.571, type = 2, riders = {65535, 65535, 65535, 65535}}
    local function vector(v) return {GetX = function() return v.x end, GetY = function() return v.y end, GetZ = function() return v.z end} end
    local function rec(name, ...) c.calls[#c.calls + 1] = {name, ...} end
    env.GameObject = {SendCommand = function(id, cmd)
        c.sent[#c.sent + 1] = {id = id, cmd = cmd}
        if id ~= RID then return nil end
        if cmd.id == 'IsAlive' then
            if c.v0.goneAt and c.time >= c.v0.goneAt then c.v0.alive, c.v0.goneAt = false, nil end
            return c.v0.alive
        end
        if not c.v0.alive then return nil end
        if cmd.id == 'GetPosition' then return vector(c.v0), c.v0.rot end
        if cmd.id == 'GetVehicleType' then return c.v0.type end
        if cmd.id == 'GetRiderId' then return c.v0.riders end
        if cmd.id == 'SetPosition' then
            c.v0.x, c.v0.y, c.v0.z, c.v0.rot = cmd.position.x, cmd.position.y, cmd.position.z, cmd.rotY
            c.placed = (c.placed or 0) + 1
            return true
        end
        if cmd.id == 'Seize' then
            if o.slowSeize then c.v0.goneAt = c.time + 1000 else c.v0.alive = false end
            return nil
        end
        error('unexpected command ' .. tostring(cmd.id))
    end, GetGameObjectId = function() return 65535 end}
    env.Tpp = {IsVehicle = function(id) return type(id) == 'number' and id >= 25600 and id < 26112 end}
    env.PlayerStatus = {ON_VEHICLE = 9}
    env.PlayerInfo = {OrCheckStatus = function() return c.riding == true end}
    env.Vehicle = {LoadCarry = function()
        rec('LoadCarry', env.vars.reliefVehicleCarryType, att[0], att[1], att[2], att[3], paint[0], paint[1])
        if o.loadSpawns and env.vars.reliefVehicleCarryType % 256 > 0 then c.v0.alive = true end
    end}
    env.Vector3 = function(x, y, z) return {x = x, y = y, z = z} end
    env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 1, MISSION_ABORT = 2, MISSION_RESTART = 3, CONTINUE_FROM_CHECK_POINT = 4}}
    env.TppMission = {Load = function(m) rec('load', m, env.vars.reliefVehicleCarryType) end,
        UpdateCheckPoint = function() rec('checkpoint'); return true end}
    for _, name in ipairs({'SeizeReliefVehicleOnAbort', 'SeizeReliefVehicleOnClear', 'SeizeReliefVehicleOnForceGoToMb'}) do
        env.TppMission[name] = function() rec(name) end
    end
    env.TppTerminal = {OnFulton = function(...) rec('OnFulton', ...) end}
    env.TppSave = {SaveVarsToSlot = function(slot) c.saves[#c.saves + 1] = {slot = slot, ct = env.vars.reliefVehicleCarryType,
        x = att[0], paint0 = paint[0], life0 = life[0]} end}
    env.TppUiCommand = {SetMbTopMenuItemActive = function(menu, on) c.menus[#c.menus + 1] = {menu, on} end,
        IsMbDvcTerminalOpened = function() return c.idroid end}
    env.e = {}
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {}, bindHooks = {},
        level = 3, role = role, host = role == 1, active = true, mission = env.vars.missionCode, epoch = 7, paired = o.paired ~= false,
        eff = 3, mode = o.mode or 1, vehicleGoneOn = o.gone == true}
    env.e.MgsCoopWorld = W
    W.get = function(t, k) if t == nil then return nil end return t[k] end
    c.cfg = {3, role, 1, 1, 7, 0, 1, 55, 0, 0, 0, 0, 0, 0, 0, 0, o.on ~= false and 1 or 0}
    W.cfg = function() return c.cfg end
    W.h = function(s) return str32(s) end
    W.now = function() return c.time end
    W.tick32 = function(n) return n end
    W.inGame = function() return c.inGame end
    W.nextRid = function() return 1 end
    W.log = function(s) c.logs[#c.logs + 1] = s end
    W.receipt = function() return nil end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op = op, v = {...}}; return true end
    W.fail = function(s) c.failure = s end
    W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
    W.peer = function() return nil end
    W.n = function(v) return v end
    W.observe = function(class, msg, a0) c.observed = (c.observed or 0) + 1 end
    W.tick = function() c.baseTicks = (c.baseTicks or 0) + 1 end
    W.api = {str32 = str32, send = env.GameObject.SendCommand, id = env.GameObject.GetGameObjectId}
    for _, path in ipairs({arg[2], arg[3], arg[4], arg[1]}) do
        local chunk = assert(loadfile(path)); setfenv(chunk, env); chunk()
    end
    W.follow.install(c.cfg)
    for _, f in ipairs(W.bindHooks) do f(W.api) end
    for _, f in ipairs(W.startHooks) do f() end
    c.W, c.env, c.RS, c.F, c.S, c.V = W, env, W.reliefSync, W.follow, W.followSeeds, W.vehicleGone
    function c.find(s) for i = #c.logs, 1, -1 do if c.logs[i]:find(s, 1, true) then return c.logs[i] end end end
    function c.count(s) local k = 0; for _, l in ipairs(c.logs) do if l:find(s, 1, true) then k = k + 1 end end; return k end
    function c.called(name) local k = 0; for _, v in ipairs(c.calls) do if v[1] == name then k = k + 1 end end; return k end
    function c.last(name) for i = #c.calls, 1, -1 do if c.calls[i][1] == name then return c.calls[i] end end end
    function c.tick(ms) c.time = c.time + (ms or 250); W.tick() end
    function c.run(ms, step) local t = 0; while t < ms do c.tick(step or 250); t = t + (step or 250) end end
    function c.txOf(op) local out = {}; for _, t in ipairs(c.tx) do if t.op == op then out[#out + 1] = t end end; return out end
    return c
end
-- Seeds as the host would send them: the relief keys from a host copy with type 2 (east LV), its
-- carry pose at the Africa savannahEast_N LZ and paint 1 on its type's preset (index 0).
local function hostSeeds(ct)
    local h = Fixture(1, {ct = ct or 514})
    local a = h.env.vars.reliefVehicleCarryAttitude
    a[0], a[1], a[2], a[3] = 1227.17, 25.84, -127.05, -1.571
    h.env.vars.userPresetVehiclePaintType[0] = 1
    h.env.vars.reliefVehicleCarryLife[0] = 2300
    local values = {}
    for k, key in ipairs(h.S.keys) do
        if key[6] == 'relief' then
            local n = key[2] or 1
            local data = h.env.vars[key[1]]
            for i = 0, n - 1 do values[#values + 1] = {k, i, h.S.encode(key, key[2] and data[i] or data)} end
        end
    end
    return values
end
local function follow(c, values, mission)
    c.F.seeds[5] = {mission = mission or 30020, values = values, ready = true, digest = 0}
    c.F.current = {5, 6, 40020, mission or 30020, 0, 0, 0, 0, 0, 0, 1}
    c.env.TppMission.Load(mission or 30020)
end
local function restart(c, mission)
    c.env.vars.missionCode = mission or c.env.vars.missionCode
    c.W.mission = c.env.vars.missionCode
    c.F.current = nil
    for _, f in ipairs(c.W.startHooks) do f() end
end

-- The seed schema: six relief keys (31 values), always sent, vars, inside the 1..127 key index.
local c = Fixture(2)
local want = {reliefVehicleCarryType = {nil, nil}, reliefVehicleCarryAttitude = {4, 1000}, reliefVehicleCarryLife = {12, nil},
    reliefVehicleCarryAmmo = {3, nil}, reliefVehicleCarryState = {nil, nil}, userPresetVehiclePaintType = {10, nil}}
local n, values = 0, 0
for _, key in ipairs(c.S.keys) do
    if key[6] == 'relief' then
        n = n + 1
        check(want[key[1]] and key[2] == want[key[1]][1] and key[3] == want[key[1]][2] and key[4] == true,
            'relief key shape: ' .. key[1])
        values = values + (key[2] or 1)
    end
end
check(n == 6 and values == 31 and #c.S.keys <= 127, 'six relief keys, 31 values, within the 1..127 seed index')
check(#hostSeeds() == 31, 'a host snapshot carries the 31 relief values')
-- An unreadable relief key never blocks travel: it goes as 0, logged.
c.env.vars.reliefVehicleCarryState = nil
local all, only = c.S.keys, {}
for _, key in ipairs(all) do if key[6] == 'relief' then only[#only + 1] = key end end
c.S.keys = only
local snap = c.S.snapshot()
c.S.keys = all
check(snap ~= nil and #snap == 31 and c.find('event=follow;step=seed_substitute;key=reliefVehicleCarryState:0'), 'unreadable relief key: 0, logged')
c.env.vars.reliefVehicleCarryState = 0
-- The attitude round-trips through the seed encoding (negative values too).
local attKey
for _, key in ipairs(c.S.keys) do if key[1] == 'reliefVehicleCarryAttitude' then attKey = key end end
check(math.abs(c.S.decode(attKey, c.S.encode(attKey, -127.05)) + 127.05) < 0.001, 'negative attitude round-trips')

-- Guest, key off: the relief keys are not applied (its own carry and paint stay).
c = Fixture(2, {on = false, ct = 257})
follow(c, hostSeeds())
check(c.env.vars.reliefVehicleCarryType == 257 and c.env.vars.userPresetVehiclePaintType[0] == 0 and c.RS.own == nil,
    'off: no relief seed applied, nothing kept')
check(not c.find('event=relief;'), 'off: no relief line')

-- Guest, key on: its own values are kept, then the host's carry and paint are applied before Load.
c = Fixture(2, {ct = 257})
c.env.vars.userPresetVehiclePaintType[0] = 3
follow(c, hostSeeds())
local load = c.last('load')
check(load and load[2] == 30020 and load[3] == 514, 'Load runs with the host carry type in vars')
local a = c.env.vars.reliefVehicleCarryAttitude
check(math.abs(a[0] - 1227.17) < 0.001 and math.abs(a[2] + 127.05) < 0.001 and math.abs(a[3] + 1.571) < 0.001,
    'the host carry attitude is seeded')
check(c.env.vars.userPresetVehiclePaintType[0] == 1 and c.env.vars.reliefVehicleCarryLife[0] == 2300, 'paint and life are seeded')
check(c.RS.own and c.RS.own.reliefVehicleCarryType == 257 and c.RS.own.userPresetVehiclePaintType[0] == 3, 'own values kept')
check(c.find('event=relief;role=guest;step=own_kept;why=seed;own_type=1;'), 'own kept is logged')
check(c.find('event=relief;role=guest;step=seed_apply;type=2;ct=514;paint=1;kind=6;mission=30020;'), 'seed apply is logged')
-- D8 (b): a save writes the guest's own values; the live (host) values come back afterwards.
c.env.TppSave.SaveVarsToSlot(3)
local sv = c.saves[#c.saves]
check(sv.slot == 3 and sv.ct == 257 and sv.paint0 == 3 and sv.x == 0 and sv.life0 == 0, 'the save writes the own carry and paint')
check(c.env.vars.reliefVehicleCarryType == 514 and c.env.vars.userPresetVehiclePaintType[0] == 1, 'live host values after the save')
check(c.find('event=relief;role=guest;step=save_own;slot=3;own_type=1;live_type=2;n=1;'), 'save_own is logged')
-- Arrival: the guest's own engine draws index 0 at mission start; logged with its pose and paint.
restart(c, 30020)
c.v0.alive = true
c.tick()
check(c.find('event=relief;role=guest;step=arrived;type=2;vt=2;paint=1;x=1227.24;y=19.43;z=-127.05;yaw=-1.571;mission=30020;source=seed;'),
    'the guest arrival line')
-- A solo load (not following the host) restores the own values.
c.W.paired = false
c.env.TppMission.Load(30010)
check(c.env.vars.reliefVehicleCarryType == 257 and c.env.vars.userPresetVehiclePaintType[0] == 3 and c.RS.own == nil,
    'a solo load restores the own carry and paint')
check(c.find('event=relief;role=guest;step=own_restored;why=solo;own_type=1;'), 'own restored is logged')

-- Host, key on: the sortie seeds carry its carry (logged at Load); its own arrival is logged.
c = Fixture(1, {ct = 514})
c.env.vars.reliefVehicleCarryAttitude[0] = 1227.17
c.env.vars.userPresetVehiclePaintType[0] = 1
c.F.outgoing = {5, 6, 40020, 30020, 0, 0, 0, 0, 0, 0, 55, session = 55}
c.env.TppMission.Load(30020)
check(c.find('event=relief;role=host;step=seed;type=2;ct=514;paint=1;cp=1227.17,0.00,0.00,0.000;mission=30020;'), 'host seed line')
check(c.RS.own == nil, 'the host keeps nothing')
restart(c, 30020)
c.v0.alive = true
c.tick()
check(c.find('event=relief;role=host;step=arrived;type=2;vt=2;paint=1;') and c.find('source=carry;'), 'the host arrival line')

-- Host: VEHICLE_SPAWN (op 52) in free roam while paired: on change and every 5 s.
c = Fixture(1, {ct = 514, alive = true})
a = c.env.vars.reliefVehicleCarryAttitude
a[0], a[1], a[2], a[3] = 1227.17, 25.84, -127.05, -1.571
c.env.vars.userPresetVehiclePaintType[0] = 1
c.tick()
local sp = c.txOf(52)
check(#sp == 1, 'one VEHICLE_SPAWN at once')
local v = sp[1].v
check(#v == 11 and v[2] == 30020 and v[3] == RID and v[4] == 1 and v[5] == 514 and v[6] == 1 and v[7] == 3,
    'fields: mission, id, cause relief, carry type, paint, alive + carry pose')
check(v[8] == 1227170 and v[9] == 25840 and v[10] == -127050 and v[11] == -1571, 'the carry pose (vehicle within 1.5 m of it), mm / mrad')
c.run(4000)
check(#c.txOf(52) == 1, 'nothing more within 5 s while unchanged')
c.run(1500)
check(#c.txOf(52) == 2, 'refreshed after 5 s')
-- The vehicle moved away from its carry pose: the live pose, 0.5 m up (it settles on the ground).
c.v0.x, c.v0.z, c.v0.rot = 1300, -100, 0.5
c.run(5250)
v = c.txOf(52)[#c.txOf(52)].v
check(v[7] == 1 and v[8] == 1300000 and v[9] == 19930 and v[10] == -100000 and v[11] == 500, 'live pose + 0.5 m when moved')
-- Lost: sent at once with alive 0.
local before = #c.txOf(52)
c.v0.alive = false
c.tick()
check(#c.txOf(52) == before + 1 and c.txOf(52)[before + 1].v[7] == 0, 'a loss is sent at once (alive 0)')
-- Not in the ACC, not unpaired, not with the key off.
for _, setup in ipairs({function(x) restart(x, 40020) end, function(x) x.W.paired = false end, function(x) x.cfg[17] = 0; restart(x) end}) do
    local h = Fixture(1, {ct = 514, alive = true})
    setup(h)
    h.run(6000)
    check(#h.txOf(52) == 0, 'no VEHICLE_SPAWN in the ACC, unpaired or with the key off')
end

-- Guest: the host has index 0, this copy's mission never had it (a host continue, a late join):
-- after 10 s the guest writes the host's carry and calls Vehicle.LoadCarry once.
local function hostState(x, alive, ct, flags)
    local H = x.W.handlers[52]
    H({9, 30020, RID, 1, ct or 514, 1, flags or (alive == false and 0 or 3), 1227170, 25840, -127050, -1571}, x.time)
end
c = Fixture(2, {ct = 0, loadSpawns = true})
check(type(c.W.handlers[52]) == 'function', 'the guest handles VEHICLE_SPAWN')
hostState(c)
check(c.find('event=relief;role=guest;step=host_state;alive=1;type=2;mission=30020;'), 'host state logged on change')
c.run(9000)
check(c.called('LoadCarry') == 0, 'nothing within 10 s')
hostState(c)
c.run(1500)
local lc = c.last('LoadCarry')
check(lc and lc[2] == 514 and math.abs(lc[3] - 1227.17) < 0.001 and math.abs(lc[4] - 25.84) < 0.001 and math.abs(lc[6] + 1.571) < 0.001,
    'LoadCarry after the host carry type and pose are written')
check(lc[7] == 1, 'the host paint is written to the type preset (type 2: index 0)')
check(c.find('event=relief;role=guest;step=load;why=host_has;type=2;ct=514;paint=1;write=1;call=1;'), 'load line')
check(c.RS.own and c.RS.own.reliefVehicleCarryType == 0, 'own carry kept before the write')
c.tick()
check(c.find('step=arrived;type=2;') and c.find('source=load;'), 'the loaded arrival is logged')
check(not c.placed and not c.find('step=placed;'), 'a load that appears at the host pose (within 3 m) is not moved')
-- The load draws it at the drop point while the host's vehicle stands elsewhere (run 8a81f776; flag 2 clear here), so
-- the arrival sets it to the host's pose.
c = Fixture(2, {ct = 0, loadSpawns = true})
for _ = 1, 3 do c.W.handlers[52]({9, 30020, RID, 1, 514, 1, 1, 1201730, 17410, -128630, -1637}, c.time); c.run(5000) end
c.tick()
check(c.placed == 1 and math.abs(c.v0.x - 1201.73) < 0.001 and math.abs(c.v0.z + 128.63) < 0.001 and math.abs(c.v0.rot + 1.637) < 0.001,
    'the loaded vehicle is placed at the host pose')
check(c.find('step=placed;why=load;ok=1;x=1201.73;y=17.41;z=-128.63;yaw=-1.637;'), 'placed line')
-- The same when the host stands on its carry pose (flag 2) but this copy draws it elsewhere (another drop point).
c = Fixture(2, {ct = 0, loadSpawns = true})
for _ = 1, 3 do c.W.handlers[52]({9, 30020, RID, 1, 514, 1, 3, 263570, 1560, -228880, -524}, c.time); c.run(5000) end
c.tick()
check(c.placed == 1 and math.abs(c.v0.x - 263.57) < 0.001 and c.find('step=placed;why=load;ok=1;x=263.57;'), 'a far draw is placed at the carry pose too')
hostState(c)
c.run(30000)
check(c.called('LoadCarry') == 1, 'loaded once')
-- Never over a live index 0, never in the ACC, never when this copy lost its own (VEHICLE_GONE owns that).
c = Fixture(2, {ct = 0, loadSpawns = true})
restart(c, 40020)
c.W.handlers[52]({9, 40020, RID, 1, 514, 1, 3, 0, 0, 0, 0}, c.time)
c.run(12000)
check(c.called('LoadCarry') == 0, 'never in the ACC')
c = Fixture(2, {ct = 514, alive = true, loadSpawns = true})
c.tick()
c.v0.alive = false
for _ = 1, 4 do hostState(c); c.run(5000) end
check(c.called('LoadCarry') == 0 and c.find('event=relief;role=guest;step=reconcile_skip;reason=lost_here;'), 'lost here: no reload')
c = Fixture(2, {ct = 0, loadSpawns = true})
hostState(c)
c.inGame = false
c.run(12000)
check(c.called('LoadCarry') == 0, 'never outside gameplay')
-- The host has none for 15 s while this copy has one (its own, or one the host lost): removed, no credit.
c = Fixture(2, {ct = 514, alive = true})
c.tick()
for _ = 1, 4 do hostState(c, false, 0); c.run(5000) end
local seize
for _, s in ipairs(c.sent) do if s.cmd.id == 'Seize' then seize = s end end
check(seize and seize.id == RID and #seize.cmd.options == 1 and seize.cmd.options[1] == 'Instant', 'Seize {"Instant"} on 25600 only')
check(c.find('event=relief;role=guest;step=remove;via=seize_instant;why=host_none;ok=1;'), 'remove line')
check(c.find('event=relief;role=guest;step=remove;via=seize_instant;why=host_none;ok=1;mission=30020;'), 'the remove line names the mission')
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(c.called('OnFulton') == 0 and c.find('event=relief;role=guest;step=credit_skipped;fn=OnFulton;why=quiet;mission=30020;'),
    'its OnFulton credit skipped once')
-- Review of 2026-10-29: a co-op guest never credits index 0 (the host's vehicle); within 30 s of its own removal a
-- second OnFulton is still that removal (no request to the host).
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(c.called('OnFulton') == 0 and #c.txOf(47) == 0 and c.find('step=credit_skipped;fn=OnFulton;why=own_removal;'),
    'a co-op guest never credits index 0; its own removal is never a loss')
c.env.TppTerminal.OnFulton(1234)
check(c.called('OnFulton') == 1, 'other ids always pass')
-- ...but never under a rider (the local player in it).
c = Fixture(2, {ct = 514, alive = true})
c.riding, c.env.vars.playerVehicleGameObjectId = true, RID
for _ = 1, 4 do hostState(c, false, 0); c.run(5000) end
check(not c.find('step=remove;'), 'never removed under the local player')
-- A different type here than the host's: this copy's own is removed (gone a second later, as the game's
-- Seize Instant), then the host's is loaded.
c = Fixture(2, {ct = 257, alive = true, loadSpawns = true, slowSeize = true})
c.tick()
for _ = 1, 3 do hostState(c); c.run(5000) end
check(c.find('step=remove;via=seize_instant;why=type;'), 'a type mismatch removes this copy own')
for _ = 1, 6 do hostState(c); c.run(5000) end
check(c.last('LoadCarry') and c.last('LoadCarry')[2] == 514, 'then the host type is loaded')
check(not c.find('reason=lost_here'), 'a removal for the type is not a loss here')
-- The same type parked more than 20 m from the host's (a guest continue of its own save): placed at the host's pose.
c = Fixture(2, {ct = 514, alive = true, loadSpawns = true, slowSeize = true})
c.v0.x = 1260
c.tick()
for _ = 1, 4 do hostState(c); c.run(5000) end
check(c.find('step=placed;why=far;ok=1;x=1227.17;') and c.placed == 1 and not c.find('step=remove;') and c.called('LoadCarry') == 0,
    'far: placed at the host pose, not removed')
-- Within 20 m (or driven: VEHICLE_GONE's busy latch), nothing is done.
c = Fixture(2, {ct = 514, alive = true})
c.v0.x = 1240
c.tick()
for _ = 1, 6 do hostState(c); c.run(5000) end
check(not c.find('step=remove;') and c.called('LoadCarry') == 0, 'within 20 m: left to RIDE_FAST / VEHICLE_STATE')
-- Host: an unreadable pose of a live index 0 is never sent as (0, 0, 0).
c = Fixture(1, {ct = 514, alive = true})
c.env.GameObject.SendCommand = function(id, cmd) if cmd.id == 'IsAlive' then return true end return nil end
c.run(6000)
check(#c.txOf(52) == 0, 'no VEHICLE_SPAWN while the live pose is unreadable')

-- Mission end (E18): the guest skips the three stock seizes while in co-op; the host's run.
c = Fixture(2, {ct = 514, alive = true})
for _, name in ipairs({'SeizeReliefVehicleOnAbort', 'SeizeReliefVehicleOnClear', 'SeizeReliefVehicleOnForceGoToMb'}) do
    c.env.TppMission[name]()
    check(c.called(name) == 0 and c.find('event=relief;role=guest;step=seize_skipped;fn=' .. name .. ';alive=1;mission=30020;'), 'guest skips ' .. name)
end
-- After an unpair, a guest copy whose relief vehicle came from the host still skips it.
c = Fixture(2, {ct = 0})
follow(c, hostSeeds())
restart(c, 30020)
c.W.paired = false
c.env.TppMission.SeizeReliefVehicleOnClear()
check(c.called('SeizeReliefVehicleOnClear') == 0, 'seeded this mission: skipped though unpaired')
-- Solo (never paired, nothing seeded), key off, or the host: the stock seize runs.
c = Fixture(2, {paired = false})
c.env.TppMission.SeizeReliefVehicleOnClear()
check(c.called('SeizeReliefVehicleOnClear') == 1, 'solo guest: stock seize')
c = Fixture(2, {on = false})
c.env.TppMission.SeizeReliefVehicleOnClear()
check(c.called('SeizeReliefVehicleOnClear') == 1, 'key off: stock seize')
c = Fixture(1, {ct = 514, alive = true})
c.env.TppMission.SeizeReliefVehicleOnClear()
check(c.called('SeizeReliefVehicleOnClear') == 1 and c.find('event=relief;role=host;step=seize;fn=SeizeReliefVehicleOnClear;alive=1;mission=30020;'),
    'host: the stock seize runs (logged, with the mission it ends)')

-- VEHICLE_GONE (world_vehicle.lua) for index 0: the receiver removes it with Seize {"Instant"} and no credit
-- (it refused index0 before); its own removal's FultonInfo is never taken for a guest loss.
local function gone(x)
    x.W.handlers[46]({1, RID, 1, 0, 0, 2, 1227240, 19430, -127050}, x.time)
end
c = Fixture(2, {ct = 514, alive = true, gone = true})
gone(c)
seize = nil
for _, s in ipairs(c.sent) do if s.cmd.id == 'Seize' then seize = s end end
check(seize and seize.cmd.options[1] == 'Instant' and not c.v0.alive, 'VEHICLE_GONE removes index 0 with Seize Instant')
check(c.find('event=relief;role=guest;step=remove;via=seize_instant;why=vehicle_gone;ok=1;') and not c.find('reason=index0'),
    'logged; not refused as index0')
check(c.V.gone[RID] == true, 'VEHICLE_GONE marks it gone here')
c.V.observe('FultonInfo', RID, 'local')
check(not c.find('event=vehicle_gone;step=watch;id=25600'), 'its own removal is not watched as a guest loss')
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(c.called('OnFulton') == 0, 'no credit for the receiver removal')
-- Key off: index 0 is still refused (today's behaviour).
c = Fixture(2, {ct = 514, alive = true, gone = true, on = false})
gone(c)
check(c.v0.alive and c.find('reason=index0'), 'key off: VEHICLE_GONE still refuses index 0')

-- Refusal (D1 a): the guest's iDroid vehicle drop is inactive in a co-op free-roam mission. The LOADOUT drop is left
-- to the user's decision D11 (review of 2026-10-29): active unless the lab lever relief_lab loadout on blocks it.
c = Fixture(2)
c.tick()
local function menu(x, name)
    for i = #x.menus, 1, -1 do if x.menus[i][1] == name then return x.menus[i][2] end end
end
check(menu(c, 'MSN_DROP_VEHICLE') == false and menu(c, 'MSN_DROP_LOADOUT') == nil, 'only the vehicle drop is inactive (D11 pending)')
check(c.find('event=relief;role=guest;step=guard;path=idroid_drop;via=menu_inactive;items=1;'), 'guard line')
c.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_LOADOUT'))
check(#c.notes == 0 and not c.find('path=idroid_drop_loadout'), 'an inactive LOADOUT item is not ours without the lever')
local ok, text = c.RS.lever({'relief_lab', 'loadout', 'on'})
check(ok and text == 'relief_lab;loadout=on' and menu(c, 'MSN_DROP_LOADOUT') == false and c.find('step=loadout_block;on=1;'),
    'lab lever: the LOADOUT drop blocked too')
c.run(5000)
check(c.count('step=guard;path=idroid_drop') == 1, 'logged once per mission')
local nm = #c.menus
c.idroid = true
c.run(5000)
check(#c.menus == nm, 'never re-applied while the iDroid is open')
c.idroid = false
-- Selecting the inactive item: refused, logged, a toast (NOTE 13 a 4: vehicle, 5: loadout); other items pass.
c.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_VEHICLE'))
check(c.find('event=relief;role=guest;step=refused;path=idroid_drop_vehicle;via=menu_inactive;'), 'vehicle refusal line')
check(#c.notes == 1 and c.notes[1][1] == 13 and c.notes[1][2] == 4, 'vehicle toast NOTE 13 a 4')
check(c.observed == 2, 'the message still reaches the stock observer')
c.time = c.time + 4000
c.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_LOADOUT'))
check(c.find('step=refused;path=idroid_drop_loadout;') and c.notes[2][2] == 5, 'loadout refusal and toast NOTE 13 a 5')
c.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_BUDDY_DOG'))
check(#c.notes == 2, 'another inactive item: no relief toast')
c.W.observe(str32('Terminal'), str32('MbDvcActSelectNonActiveMenu'), str32('MSN_DROP_VEHICLE'))
check(#c.notes == 2 and c.count('path=idroid_drop_vehicle') == 2, 'toasts at most every 3 s (the refusal is still logged)')
ok, text = c.RS.lever({'relief_lab', 'loadout', 'off'})
check(ok and menu(c, 'MSN_DROP_LOADOUT') == true and c.find('step=loadout_block;on=0;'), 'lab lever off: LOADOUT active again')
-- Unpaired inside the co-op mission (a link drop): the vehicle drop stays inactive (review of 2026-10-29); the
-- session's final stop activates it (TppTerminal.SetUp leaves every drop item active: the stock state).
c = Fixture(2)
for _, f in ipairs(c.W.pairedHooks) do f(false) end
c.tick()
for _, f in ipairs(c.W.unpairedHooks) do f(1) end
c.W.paired = false
c.run(5000)
check(menu(c, 'MSN_DROP_VEHICLE') == false and not c.find('step=unguard;'), 'unpaired in the co-op mission: still inactive')
for _, f in ipairs(c.W.stopHooks) do f(true) end
c.run(3000)
check(menu(c, 'MSN_DROP_VEHICLE') == true and c.find('event=relief;role=guest;step=unguard;path=idroid_drop;'), 'final stop: active again')
-- A solo load ends the co-op mission: active again too.
c = Fixture(2)
for _, f in ipairs(c.W.pairedHooks) do f(false) end
c.tick()
c.W.paired = false
c.env.TppMission.Load(30010)
c.run(3000)
check(menu(c, 'MSN_DROP_VEHICLE') == true, 'solo load: active again')
-- The first pass of a mission applies even while the iDroid is open (it was skipped before); later passes wait.
c = Fixture(2)
c.idroid = true
c.tick()
check(menu(c, 'MSN_DROP_VEHICLE') == false, 'the first pass applies with the iDroid open')
-- The host and a key-off guest never grey anything.
for _, x in ipairs({Fixture(1), Fixture(2, {on = false})}) do
    x.run(3000)
    check(#x.menus == 0, 'no menu change on the host or with the key off')
end

-- Levers: relief_status (any mode, read only); relief_lab nonactive (lab only) runs the refusal path.
c = Fixture(2, {ct = 514, alive = true})
c.tick()
ok, text = c.RS.lever({'relief_status'})
check(ok and text:find('relief_sync;on=1;role=guest;type=2;ct=514;v0=1;', 1, true) and text:find('greyed=1', 1, true), 'status: ' .. tostring(text))
ok, text = c.RS.lever({'relief_lab', 'nonactive', 'vehicle'})
check(ok and c.find('step=refused;path=idroid_drop_vehicle;'), 'lab lever: the refusal path')
c = Fixture(2, {mode = 2})
c.tick()
ok, text = c.RS.lever({'relief_lab', 'nonactive', 'vehicle'})
check(not ok and text == 'relief_lab_only', 'the lab lever refuses in play')
ok, text = c.RS.lever({'relief_lab', 'loadout', 'on'})
check(not ok and text == 'relief_lab_only' and c.RS.loadout == nil, 'the loadout lever refuses in play')
ok, text = c.RS.lever({'relief_status'})
check(ok, 'status in play')

-- Lab lever relief_lab forget (guest): this copy's index 0 removed without a credit and without a VEHICLE_GONE
-- request, then reloaded from the host's VEHICLE_SPAWN (the late-join path, step=load) although it had one.
c = Fixture(2, {ct = 514, alive = true, loadSpawns = true, gone = true, slowSeize = true})
c.tick()
ok, text = c.RS.lever({'relief_lab', 'forget'})
check(ok and text == 'relief_lab;forget' and c.find('step=remove;via=seize_instant;why=lab_forget;ok=1;'), 'forget removes: ' .. tostring(text))
c.V.observe('FultonInfo', RID, 'local')
check(not c.find('event=vehicle_gone;step=watch;id=25600'), 'its own removal is not a guest loss (no request to the host)')
for _ = 1, 4 do hostState(c); c.run(5000) end
check(c.find('step=load;why=host_has;type=2;') and c.last('LoadCarry') and not c.find('reason=lost_here'), 'forget: reloaded from the host values')
c.tick()
check(c.find('source=load;'), 'the reloaded arrival')
c = Fixture(1, {ct = 514, alive = true})
ok, text = c.RS.lever({'relief_lab', 'forget'})
check(not ok and text == 'relief_guest_only', 'forget is guest only')
c = Fixture(2, {ct = 514, alive = true, mode = 2})
ok, text = c.RS.lever({'relief_lab', 'forget'})
check(not ok and text == 'relief_lab_only', 'forget is lab only')
c = Fixture(2, {ct = 0})
ok, text = c.RS.lever({'relief_lab', 'forget'})
check(not ok and text == 'relief_absent', 'forget needs index 0')

-- Review of 2026-10-29 (F1): a guest-side loss of index 0 is the host's to credit, whatever world_vehicle_gone says.
-- The guest's own Fulton: its OnFulton for 25600 never credits on the guest; REQ_VEHICLE_GONE (op 47) asks the host.
for _, vg in ipairs({false, true}) do
    c = Fixture(2, {ct = 514, alive = true, gone = vg})
    c.tick()
    c.V.observe('FultonInfo', RID, 'local')
    c.v0.alive = false
    c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
    check(c.called('OnFulton') == 0, 'guest Fulton: no credit on the guest (vehicle_gone=' .. tostring(vg) .. ')')
    check(c.find('event=relief;role=guest;step=credit_skipped;fn=OnFulton;why=guest_fulton;mission=30020;'), 'credit_skipped guest_fulton line')
    local rq = c.txOf(47)
    check(#rq == 1 and rq[1].v[1] == 1 and rq[1].v[2] == RID and rq[1].v[3] == 1, 'one REQ_VEHICLE_GONE for 25600, cause 1 (fulton)')
    check(rq[1].v[5] == 1227240 and rq[1].v[6] == 19430 and rq[1].v[7] == -127050, 'the request carries this copy last pose (mm)')
    check(c.find('event=relief;role=guest;step=lost_tx;rid=1;cause=1;try=1;sent=1;mission=30020;'), 'lost_tx line')
    check(not c.find('event=vehicle_gone;step=watch;id=25600'), 'VEHICLE_GONE never watches index 0 on the guest while relief is on')
    check(c.V.gone[RID] ~= nil, 'marked gone here: a VEHICLE_GONE for it is never matched to another vehicle')
    c.W.handlers[46]({1, RID, 1, 0, 0, 2, 1227240, 19430, -127050}, c.time)
    check(not c.find('step=matched;') and c.count('step=remove;') == 0, 'the host announcement of the same loss removes nothing more here')
end
-- The host answers with its own Seize {"Fulton"}: the stock credit runs once, on the host.
c = Fixture(1, {ct = 514, alive = true, gone = true})
c.W.handlers[47]({5, RID, 1, 2, 1227240, 19430, -127050}, c.time)
seize = nil
for _, s in ipairs(c.sent) do if s.cmd.id == 'Seize' then seize = s end end
check(seize and seize.id == RID and #seize.cmd.options == 1 and seize.cmd.options[1] == 'Fulton', 'host: Seize {"Fulton"} on its own 25600')
check(c.find('event=relief;role=host;step=lost_rx;rid=5;cause=1;mission=30020;') and c.find('event=relief;role=host;step=host_fulton;ok=1;rid=5;'),
    'host lost_rx and host_fulton lines')
check(not c.find('event=vehicle_gone;step=rx_req;rid=5;'), 'VEHICLE_GONE does not take index 0 while relief is on')
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(c.called('OnFulton') == 1 and c.find('event=relief;role=host;step=credit;fn=OnFulton;mission=30020;'), 'the host credits it once')
c.tick()
check(#c.txOf(52) >= 1 and c.txOf(52)[#c.txOf(52)].v[7] == 0, 'the host then reports alive 0')
c.W.handlers[47]({6, 25601, 1, 2, 0, 0, 0}, c.time)
check(c.find('event=vehicle_gone;step=rx_req;rid=6;id=25601;'), 'other ids still go to VEHICLE_GONE')
-- Key off on the host: VEHICLE_GONE keeps index 0 (its own rules).
c = Fixture(1, {ct = 514, alive = true, gone = true, on = false})
c.W.handlers[47]({5, RID, 1, 2, 0, 0, 0}, c.time)
check(c.find('event=vehicle_gone;step=rx_req;rid=5;id=25600;') and not c.find('step=lost_rx;'), 'key off: VEHICLE_GONE handles it')
-- The host rides it: refused (REJECT 8) and kept.
c = Fixture(1, {ct = 514, alive = true})
c.riding, c.env.vars.playerVehicleGameObjectId = true, RID
c.W.handlers[47]({5, RID, 1, 2, 0, 0, 0}, c.time)
local rj = c.txOf(4)
check(#rj == 1 and rj[1].v[1] == 5 and rj[1].v[2] == 47 and rj[1].v[3] == 8 and rj[1].v[4] == RID, 'the host rides it: REJECT 8')
check(c.find('event=relief;role=host;step=lost_refused;rid=5;reason=local_rider;') and c.v0.alive, 'refused and kept')
-- Absent on the host: nothing to remove (its VEHICLE_SPAWN already says alive 0).
c = Fixture(1, {ct = 0})
c.W.handlers[47]({5, RID, 1, 2, 0, 0, 0}, c.time)
check(c.find('event=relief;role=host;step=lost_absent;rid=5;') and #c.txOf(4) == 0, 'absent: no removal, no refusal')
-- A guest destruction (cause 2): the host removes its own quietly (a destruction credits nothing).
c = Fixture(1, {ct = 514, alive = true})
c.W.handlers[47]({5, RID, 2, 2, 0, 0, 0}, c.time)
check(c.find('step=remove;via=seize_instant;why=guest_destroyed;ok=1;') and not c.v0.alive, 'cause 2: removed quietly on the host')
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(c.called('OnFulton') == 0, 'and not credited')

-- Guest: the host refuses (it rides the vehicle): the reconcile loads the host's again here.
c = Fixture(2, {ct = 514, alive = true, loadSpawns = true})
c.tick()
c.v0.alive = false
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
for _ = 1, 3 do hostState(c); c.run(4000) end
check(c.called('LoadCarry') == 0 and c.find('reason=lost_here'), 'while the request is open: no reload')
c.W.handlers[4]({1, 47, 8, RID, 0, 0}, c.time)
check(c.find('event=relief;role=guest;step=lost_refused;rid=1;reason=8;'), 'the host refusal is logged')
for _ = 1, 4 do hostState(c); c.run(5000) end
check(c.last('LoadCarry') and c.find('step=load;why=host_has;type=2;'), 'refused: the host vehicle is loaded here again')
-- Guest: the host's VEHICLE_SPAWN says alive 0: done, nothing more.
c = Fixture(2, {ct = 514, alive = true})
c.tick()
c.v0.alive = false
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
hostState(c, false, 514)
check(c.find('event=relief;role=guest;step=lost_done;rid=1;'), 'the host loss ends the request')
c.run(60000)
check(#c.txOf(47) == 1 and not c.find('step=remove;') and not c.find('step=lost_timeout;'), 'no resend, no removal, no timeout')
-- Guest: no answer while the host keeps it: one resend after 20 s, then a timeout after 45 s and a reload here.
c = Fixture(2, {ct = 514, alive = true, loadSpawns = true})
c.tick()
c.v0.alive = false
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
for _ = 1, 5 do hostState(c); c.run(5000) end
check(#c.txOf(47) == 2 and c.find('step=lost_tx;rid=1;cause=1;try=2;sent=1;'), 'resent once after 20 s')
for _ = 1, 8 do hostState(c); c.run(5000) end
check(c.find('event=relief;role=guest;step=lost_timeout;rid=1;tries=2;') and c.last('LoadCarry'), 'unanswered: timeout, then reloaded here')
check(#c.txOf(47) == 2, 'at most two tries')
-- The host's RECEIVED stops the resend (the timeout still runs).
c = Fixture(2, {ct = 514, alive = true})
c.tick()
c.v0.alive = false
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
c.W.handlers[3]({1, 47}, c.time)
for _ = 1, 6 do hostState(c); c.run(5000) end
check(#c.txOf(47) == 1, 'received: no resend')
-- Unpaired inside the co-op mission: no credit; the request waits for the next pairing.
c = Fixture(2, {ct = 514, alive = true})
for _, f in ipairs(c.W.pairedHooks) do f(false) end
c.tick()
c.W.paired = false
c.v0.alive = false
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(c.called('OnFulton') == 0 and #c.txOf(47) == 0 and c.find('step=lost_tx;rid=0;cause=1;try=0;sent=0;mission=30020;'),
    'unpaired: no credit, the request waits')
c.W.paired = true
for _, f in ipairs(c.W.pairedHooks) do f(false) end
c.tick()
check(#c.txOf(47) == 1 and c.find('step=lost_tx;rid=1;cause=1;try=1;sent=1;'), 'sent once paired again')
-- A destruction by the guest's own player (Dead, att local): a request with cause 2, once.
c = Fixture(2, {ct = 514, alive = true})
c.tick()
c.v0.alive = false
c.V.observe('Dead', RID, 'local')
c.V.observe('Dead', RID, 'local')
check(#c.txOf(47) == 1 and c.txOf(47)[1].v[3] == 2 and c.find('step=lost_tx;rid=1;cause=2;'), 'guest destroyed it: one request, cause 2')
-- Destroyed by anything else on this copy (its unsynced guards): not the guest's to report.
c = Fixture(2, {ct = 514, alive = true})
c.tick()
c.V.observe('Dead', RID, 'none')
check(#c.txOf(47) == 0, 'a destruction by another attacker is no request')
-- This copy's own quiet removal is never a guest loss (its Dead or OnFulton within 30 s).
c = Fixture(2, {ct = 514, alive = true, slowSeize = true})
c.tick()
c.RS.removeQuiet('host_none')
c.V.observe('Dead', RID, 'local')
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(#c.txOf(47) == 0 and c.called('OnFulton') == 0, 'its own removal: no request, no credit')
-- A solo guest (never paired, nothing seeded) keeps the stock credit.
c = Fixture(2, {ct = 514, alive = true, paired = false})
c.env.TppTerminal.OnFulton(RID, 0, 0, 13)
check(c.called('OnFulton') == 1 and #c.txOf(47) == 0, 'solo guest: the stock credit, no request')

-- Review F5: a copy removed for host_none is loaded again when the host's comes back.
c = Fixture(2, {ct = 514, alive = true, loadSpawns = true})
c.tick()
for _ = 1, 4 do hostState(c, false, 0); c.run(5000) end
check(c.find('step=remove;via=seize_instant;why=host_none;'), 'removed for host_none')
for _ = 1, 6 do hostState(c); c.run(5000) end
check(c.last('LoadCarry') and c.find('step=load;why=host_has;') and not c.find('reason=lost_here'), 'the host vehicle back: loaded again')
-- Placements have their own budget: four far placements, then a host loss is still mirrored.
c = Fixture(2, {ct = 514, alive = true})
c.tick()
for _ = 1, 4 do
    c.v0.x = 1260
    for _ = 1, 5 do hostState(c); c.run(5000) end
end
check(c.count('step=placed;why=far;') == 4 and not c.find('step=reconcile_giveup;'), 'four placements, no give-up')
for _ = 1, 5 do hostState(c, false, 0); c.run(5000) end
check(c.find('step=remove;via=seize_instant;why=host_none;'), 'a host loss after placements is still mirrored')

-- Review F6: the receiver removes index 0 quietly for any VEHICLE_GONE cause (no M4 wreck rule yet).
c = Fixture(2, {ct = 514, alive = true, gone = true})
c.W.vehicleStateOn = true
check(c.V.remove(RID, 2) == true and c.find('step=remove;via=seize_instant;why=vehicle_gone;'), 'cause 2: removed quietly too')

print('world_relief_sync_lua_check: ' .. checks .. ' checks PASS')
