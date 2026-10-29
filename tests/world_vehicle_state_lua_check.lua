-- M4 VEHICLE_STATE (docs/vehicles-companions-design.md 6.4, docs/world-sync-protocol.md section 17): the
-- module (lua/coop/world_vehicle_state.lua) on top of world_vehicle.lua (its pool helpers) against a fake
-- world, a fake TppVehicle2 pool and a fake native life read, then with world_sync.lua (CFG r[15] bit 2
-- and the Damage observer). No game process.
-- Usage: lua51 world_vehicle_state_lua_check.lua <world_vehicle_state.lua> <world_vehicle.lua> [world_sync.lua]
-- Before this branch the module, op 51 and the r[15] bitmask did not exist: every check here failed.
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
local GONE = slurp(assert(arg[2]))
check(#SRC:gsub('\r\n', '\n') <= 32000, 'the chunk (LF, as CMake splices it) is within the 32000-byte world chunk limit')
local function str32(s) local h = 7; for i = 1, #s do h = (h * 33 + s:byte(i)) % 4294967291 end; return h end

-- A fake pool: id -> {alive, x, y, z (m), yaw (rad), type, riders, life, zero, parts, sv}.
local function Fixture(role, opts)
    opts = opts or {}
    local c = {logs = {}, tx = {}, placed = {}, lifeSet = {}, clock = 100000, mission = opts.mission or 30020}
    c.pool, c.names = {}, {}
    local env = setmetatable({}, {__index = _G})
    env._G = env
    env.vars = {missionCode = c.mission, playerVehicleGameObjectId = 65535, playerPosX = 5000, playerPosY = 0, playerPosZ = 5000}
    env.PlayerStatus = {ON_VEHICLE = 4}
    env.PlayerInfo = {OrCheckStatus = function(s) return s[1] == 4 and c.riding == true end}
    env.Tpp = {IsVehicle = function(id) return type(id) == 'number' and id >= 25600 and id <= 26111 end}
    local function vector(v) return {GetX = function() return v.x end, GetY = function() return v.y end, GetZ = function() return v.z end} end
    local W = {OP = {RECEIVED = 3, REJECT = 4, RIDE_BODY = 42, RIDE_FAST = 44}, handlers = {}, startHooks = {}, pairedHooks = {},
        unpairedHooks = {}, stopHooks = {}, active = true, paired = opts.paired ~= false, eff = 3, mission = c.mission, epoch = 7,
        host = role == 'host', role = role == 'host' and 1 or 2, mode = opts.mode or 1, vehicleGoneOn = opts.gone == true,
        vehicleStateOn = opts.on ~= false}
    W.now = function() return c.clock end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    W.tx = function(...)
        c.tx[#c.tx + 1] = {...}
        if c.txFail then return false, 5 end
        return true
    end
    W.nextRid = function() return 1 end
    W.tick = function() c.ticks = (c.ticks or 0) + 1 end
    W.h = function(name) return str32(name) end
    W.receipt = function(source)
        local first, n = source:match("^return 'MGS_COOP_VEHICLE_LIFE_V1' %-%- (%d+) (%d+)$")
        assert(first, 'only the life receipt: ' .. source)
        c.reads = (c.reads or 0) + 1
        if c.lifeRefused then return {refused = c.lifeRefused} end
        local r = {c = c.poolCount or 24}
        for id = tonumber(first), tonumber(first) + tonumber(n) - 1 do
            local v = c.pool[id]
            if v and not v.released then r[id] = {v.life or 3000, v.zero or 0, v.parts or 4, v.sv or 1} end
        end
        return r
    end
    W.api = {
        send = function(target, cmd)
            local v = c.pool[target]
            if not v then return nil end
            if cmd.id == 'IsAlive' then return v.alive == true and not v.released end
            if v.released then return nil end
            if cmd.id == 'GetPosition' then return vector(v), v.yaw or 0 end
            if cmd.id == 'GetVehicleType' then return v.type, 0 end
            if cmd.id == 'GetRiderId' then
                local r = {}
                for i = 1, 4 do r[i] = v.riders and v.riders[i] or 65535 end
                return r
            end
            if cmd.id == 'SetBodyLife' then
                c.lifeSet[#c.lifeSet + 1] = {id = target, life = cmd.life}
                if not c.lifeIgnored then v.life = cmd.life end
                return nil
            end
            error('unexpected instance command ' .. tostring(cmd.id))
        end,
        id = function(class, name)
            assert(class == 'TppVehicle2', 'vehicle locators only')
            return c.names[name] or 65535
        end,
        str32 = str32,
    }
    W.ridePose = {fast = {tracks = {}}, place = function(id, x, y, z, yaw)
        c.placed[#c.placed + 1] = {id = id, x = x, y = y, z = z, yaw = yaw}
        local v = c.pool[id]
        if not v or c.placeFail then return false end
        v.x, v.y, v.z, v.yaw = x / 1000 + (c.drift or 0), y / 1000, z / 1000, yaw / 1000
        return true
    end}
    W.M, W.kind = {}, {}
    env.e = {MgsCoopWorld = W}
    setfenv(assert(loadstring(GONE)), env)()
    setfenv(assert(loadstring(SRC)), env)()
    c.env, c.W, c.S, c.H = env, W, W.vehicleState, W.handlers
    function c:add(id, x, z, t, name, extra)
        self.pool[id] = {alive = true, x = x, y = 1, z = z, yaw = 0, type = t or 1, life = 3000, parts = 4}
        for k, v in pairs(extra or {}) do self.pool[id][k] = v end
        if name then self.names[name] = id end
    end
    function c:poll(ms) self.clock = self.clock + (ms or 250); self.W.tick() end
    function c:polls(n, ms) for _ = 1, n do self:poll(ms) end end
    function c:has(pattern) for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then return l end end end
    function c:count(pattern) local n = 0; for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then n = n + 1 end end; return n end
    function c:sent(op) local n = {} for _, t in ipairs(self.tx) do if t[1] == op then n[#n + 1] = t end end return n end
    function c:start() for _, f in ipairs(self.W.startHooks) do f() end end
    function c:pair() for _, f in ipairs(self.W.pairedHooks) do f(false) end end
    return c
end
local function state(sent, id) for i = #sent, 1, -1 do if sent[i][3] == id then return sent[i] end end end

-- Module surface.
local c = Fixture('host')
check(type(c.S) == 'table' and c.W.OP.VEHICLE_STATE == 51 and type(c.H[51]) == 'function', 'W.vehicleState, op 51 and its handler')
check(c.S.MOVE_MM == 500 and c.S.YAW_MRAD == 87 and c.S.SLICE == 4 and c.S.TICK_MS == 250 and c.S.REFRESH_MS == 10000 and c.S.HOLD_MS == 10000,
    'thresholds: 0.5 m, 5 deg, 4 ids per 250 ms, refresh 10 s, hold 10 s')

-- Off (the default): the host sends nothing, the guest ignores (one line) and never moves a vehicle.
c = Fixture('host', {on = false})
c:add(25608, -653.0, 877.7, 1, 'veh_lv_0000')
c:start(); c:pair(); c:polls(12)
check(#c.tx == 0 and not c:has('step=on'), 'off: no scan, nothing sent')
local g = Fixture('guest', {on = false})
g:add(25608, -653.0, 877.7, 1, 'veh_lv_0000')
g:start()
g.H[51]({1, 25608, str32('veh_lv_0000'), 1, 1, -650000, 1000, 877700, 0, 3000, 1, 0})
g.H[51]({2, 25608, str32('veh_lv_0000'), 1, 1, -650000, 1000, 877700, 0, 3000, 1, 0})
check(#g.placed == 0 and g:count('step=ignored;reason=off') == 1, 'off: the guest never moves a vehicle; one step=ignored line')

-- Host start: names (Africa: veh_lv_/veh_trc_), the LRRP trucks veh_trc_0000..0006.
c = Fixture('host')
c:add(25608, -653.0, 877.7, 1, 'veh_lv_0000')
c:add(25609, -600.0, 900.0, 1, 'veh_lv_0001')
c:add(25602, -700.0, 850.0, 4, 'veh_trc_0000')
c:add(25610, -640.0, 870.0, 1, 'veh_lv_0002', {riders = {12345}})
c:add(25611, -660.0, 860.0, 1, 'veh_lv_0003')
c:add(25612, -670.0, 865.0, 1, 'veh_lv_0004')
c:add(25613, -680.0, 866.0, 4, 'veh_trc_0007', {alive = false})
c:add(25600, -500.0, 800.0, 1)
c:start()
check(c:has('event=vehicle_state;step=on;mission=30020;role=host;named=7;lrrp=1'), 'step=on names the authored vehicles and the LRRP truck')
c.riding = true; c.env.vars.playerVehicleGameObjectId = 25611
c.W.ridePose.fast.tracks[25612] = {samples = {}}
c:pair()
check(c:has('event=vehicle_state;step=sweep;pool=32'), 'PAIRED starts the sweep')
c:polls(12)
local sent = c:sent(51)
local ids = {}
for _, t in ipairs(sent) do ids[t[3]] = (ids[t[3]] or 0) + 1 end
check(ids[25608] == 1 and ids[25609] == 1 and ids[25600] == 1, 'the sweep sends every free live vehicle once (named and unnamed)')
check(not ids[25602] and c:has('step=skip;reason=lrrp;id=25602'), 'an LRRP truck is skipped (reason=lrrp)')
check(not ids[25610] and c:has('step=skip;reason=occupied;id=25610'), 'an occupied rider[] seat (an NPC) is skipped')
check(not ids[25611] and c:has('step=skip;reason=local_rider;id=25611'), 'the local player\'s vehicle is skipped')
check(not ids[25612] and c:has('step=skip;reason=partner_ride;id=25612'), 'a vehicle the partner drives here (RIDE_FAST track) is skipped')
check(not ids[25613], 'a wreck (IsAlive false) is not sent (VEHICLE_GONE\'s job)')
local s = state(sent, 25608)
check(s[2] >= 1 and s[4] == str32('veh_lv_0000') and s[5] == 1 and s[6] == 3 and s[7] == -653000 and s[8] == 1000 and s[9] == 877700 and
    s[10] == 0 and s[11] == 3000 and s[12] == 1 and s[13] == 0 and #s == 13,
    'VEHICLE_STATE fields (12): seq, id, locator hash, type, flags 1|2 (sweep), pose mm, yaw, life, state word, zero mask')
check(state(sent, 25600)[4] == 0, 'an unnamed vehicle carries hash 0')
check(c:has('step=sweep_done;sent=3;pool=24'), 'sweep_done counts the sweep (the life read sets the pool size)')
check(not c:has('step=tx;'), 'sweep sends are counted, not logged one by one')

local function changes(c) local n = {}; for _, t in ipairs(c:sent(51)) do if t[6] % 8 < 4 then n[#n + 1] = t end end; return n end
-- Changes: below the thresholds nothing; a push, a turn, a life change send once (logged); MIN_GAP_MS.
local before = #changes(c)
c.pool[25608].x = -652.6
c:polls(8)
check(#changes(c) == before, 'a 0.4 m move is below the 0.5 m threshold')
c.pool[25608].x = -650.0
c:polls(8)
local after = changes(c)
check(#after == before + 1 and state(after, 25608)[7] == -650000 and state(after, 25608)[6] == 1 and
    c:has('event=vehicle_state;step=tx;id=25608;why=moved;d_mm=3000;life=3000;sv=1;zero=0;parts=4;f=1;'), 'a 3 m push is sent once (why=moved)')
c.pool[25608].yaw = 0.1
c:polls(8)
check(#changes(c) == before + 2 and c:has('id=25608;why=moved;d_mm=0;'), 'a 5.7 deg turn is sent')
c.pool[25609].life = 2400
c.pool[25609].sv = 0x11
c:polls(8)
check(#changes(c) == before + 3 and state(changes(c), 25609)[11] == 2400 and state(changes(c), 25609)[12] == 0x11 and
    c:has('step=tx;id=25609;why=life;d_mm=0;life=2400;sv=17;'), 'a body life change is sent with the state word')
c.pool[25609].zero = 8; c.pool[25609].parts = 3
c:polls(8)
check(c:has('step=tx;id=25609;why=parts;'), 'a part broken (zero mask) is sent')
-- Damage observed: the id is read at once (outside the slice) and its change sent at the next tick.
local reads = c.reads
c.pool[25600].life = 1000
c.S.cursor = 8
c.W.vehicleGone.observe('Damage', 25600, 'local', 1)
c:poll()
check(c:has('step=tx;id=25600;why=life;') and c.reads >= reads + 2, 'a Damage message checks that vehicle at the next tick')
-- MIN_GAP_MS: two changes 250 ms apart send once, the second at the next pass after the gap.
before = #changes(c)
c.pool[25609].x = -590.0
c.W.vehicleGone.observe('Damage', 25609, 'local', 1)
c:poll()
c.pool[25609].x = -580.0
c.W.vehicleGone.observe('Damage', 25609, 'local', 1)
c:poll()
check(#changes(c) == before + 1, 'at most one send per vehicle per 500 ms')
c:polls(8)
check(state(changes(c), 25609)[7] == -580000, 'the later pose follows after the gap')
-- Refresh: unchanged vehicles again after 10 s, flag 4, not logged.
local logsBefore = #c.logs
before = #c:sent(51)
c:polls(44)
local refreshed = 0
for i = before + 1, #c:sent(51) do if c:sent(51)[i][6] == 5 then refreshed = refreshed + 1 end end
check(refreshed >= 3 and #c.logs == logsBefore, 'every free vehicle is refreshed after 10 s (flags 1|4), silently')
-- The partner's ride ends: the skip resumes; its last pose is sent as a change.
c.W.ridePose.fast.tracks[25612] = nil
c.W.handlers[42]({1, 3, 25612, 0, 0, 0, 0, 0, 0})
c.pool[25612].x = -400.0
c:polls(8)
check(c:has('step=resume;id=25612;was=partner_ride') and state(c:sent(51), 25612) and state(c:sent(51), 25612)[7] == -400000,
    'after the partner\'s exit the vehicle is sent from where the ride left it')
-- Unlinked: nothing goes out.
c.W.paired = false
before = #c:sent(51)
c.pool[25608].x = -600.0
c:polls(8)
check(#c:sent(51) == before, 'unlinked: no scan')
c.W.paired = true
-- The life read refused (pins): life 65535 unknown, one line.
c.lifeRefused = 'life_code'
c.pool[25608].x = -610.0
c:polls(8)
check(state(c:sent(51), 25608)[11] == 65535 and c:count('step=life_refused;reason=life_code') == 1, 'an unreadable life is sent as 65535, logged once')

-- Guest: matched by locator (other pool index here), moved, read back; within the thresholds same.
g = Fixture('guest')
g:add(25605, -653.0, 877.7, 1, 'veh_lv_0000')
g:add(25606, -600.0, 900.0, 1, 'veh_lv_0001', {life = 3000})
g:add(25602, -700.0, 850.0, 4, 'veh_trc_0000')
g:add(25601, -500.0, 800.0, 1)
g:start()
local H = g.H[51]
H({1, 25608, str32('veh_lv_0000'), 1, 3, -650000, 1000, 877700, 0, 3000, 1, 0})
check(#g.placed == 1 and g.placed[1].id == 25605 and g.placed[1].x == -650000 and g.pool[25605].x == -650,
    'the locator names this copy\'s vehicle (another pool index): SetPosition through world_ride_pose')
check(g:has('event=vehicle_state;step=applied;id=25605;from=25608;moved=1;d_mm=3000;err_mm=0;dyaw=0;life=3000;was=3000;sv=1;here_sv=1;zero=0;here_zero=0;f=3'),
    'step=applied names this copy\'s id, the host\'s id, the distance and the readback error')
H({2, 25608, str32('veh_lv_0000'), 1, 5, -649800, 1000, 877700, 0, 3000, 1, 0})
H({3, 25608, str32('veh_lv_0000'), 1, 5, -649900, 1000, 877700, 0, 3000, 1, 0})
check(#g.placed == 1 and g:count('step=same;id=25605;from=25608;') == 1, 'within 0.5 m nothing moves; one step=same line')
-- Body life: the host's lower life is written and read back; a higher one never heals this copy.
H({4, 25609, str32('veh_lv_0001'), 1, 1, -600000, 1000, 900000, 0, 2400, 0x11, 0})
check(#g.lifeSet == 1 and g.lifeSet[1].id == 25606 and g.lifeSet[1].life == 2400 and g.pool[25606].life == 2400 and
    g:has('step=applied;id=25606;from=25609;moved=0;d_mm=0;err_mm=-1;dyaw=0;life=2400;was=3000;sv=17;here_sv=1;'), 'SetBodyLife takes the host\'s lower life')
g.pool[25606].life = 1000
H({5, 25609, str32('veh_lv_0001'), 1, 1, -600000, 1000, 900000, 0, 2400, 0x11, 0})
check(#g.lifeSet == 1 and g.pool[25606].life == 1000, 'this copy keeps its own extra damage')
g.lifeIgnored = true
local applied = g:count('step=applied;id=25606;')
H({6, 25609, str32('veh_lv_0001'), 1, 1, -600000, 1000, 900000, 0, 500, 0x11, 0})
check(#g.lifeSet == 2 and g.pool[25606].life == 1000 and g:count('step=applied;id=25606;') == applied, 'a life write that does not read back is not applied')
g.lifeIgnored = nil
H({7, 25609, str32('veh_lv_0001'), 1, 1, -600000, 1000, 900000, 0, 65535, 1, 0})
check(#g.lifeSet == 2, 'an unknown host life (65535) is never written')
-- Refusals: an LRRP truck, an unknown locator, a wreck here, an unnamed index 0.
H({8, 25602, str32('veh_trc_0000'), 4, 1, -690000, 1000, 850000, 0, 3000, 1, 0})
check(g:has('step=refused;id=25602;reason=lrrp') and #g.placed == 1, 'an LRRP truck is refused')
H({9, 25620, str32('veh_lv_0009'), 1, 1, 0, 0, 0, 0, 3000, 1, 0})
check(g:has('step=refused;id=25620;reason=no_locator'), 'a locator this copy lacks is refused')
H({10, 25600, 0, 1, 1, -500000, 1000, 800000, 0, 3000, 1, 0})
check(g:has('step=refused;id=25600;reason=index0'), 'unnamed index 0 (the relief vehicle, M3) is refused')
-- Unnamed: the same id, alive, same type, nearest of its type within 40 m.
H({11, 25601, 0, 1, 1, -505000, 1000, 800000, 0, 3000, 1, 0})
check(g.placed[#g.placed].id == 25601 and g:has('step=applied;id=25601;from=25601;moved=1;d_mm=5000;'), 'an unnamed vehicle: the same id when it is the nearest of its type')
g:add(25603, -509.0, 800.0, 1)
H({12, 25601, 0, 1, 1, -509500, 1000, 800000, 0, 3000, 1, 0})
check(g:has('step=refused;id=25601;reason=not_nearest'), 'another vehicle of that type nearer to the position: not_nearest')
H({13, 25601, 0, 4, 1, -505000, 1000, 800000, 0, 3000, 1, 0})
check(g:has('step=refused;id=25601;reason=no_match'), 'another type: no_match')
-- Holds: the local rider, an occupied seat, the host's RIDE_FAST track, the player in the way, unlinked.
g.riding = true; g.env.vars.playerVehicleGameObjectId = 25605
local placed = #g.placed
H({14, 25608, str32('veh_lv_0000'), 1, 1, -640000, 1000, 877700, 0, 3000, 1, 0})
check(#g.placed == placed and g:has('step=hold;id=25608;here=25605;reason=local_rider'), 'never under the local rider: held')
g:polls(4)
check(#g.placed == placed, 'held while the local player rides it')
g.riding = false; g.env.vars.playerVehicleGameObjectId = 65535
g:poll()
check(#g.placed == placed + 1 and g.pool[25605].x == -640, 'applied at the next tick after the get-off')
g.pool[25605].riders = {4321}
H({15, 25608, str32('veh_lv_0000'), 1, 1, -630000, 1000, 877700, 0, 3000, 1, 0})
check(g:has('step=hold;id=25608;here=25605;reason=occupied'), 'an occupied seat (an NPC here): held')
g.pool[25605].riders = nil
g.W.ridePose.fast.tracks[25605] = {samples = {}}
g:poll()
check(g:has('step=hold;id=25608;here=25605;reason=partner_ride') and g.pool[25605].x == -640, 'the host drives it here (RIDE_FAST track): held')
g.W.ridePose.fast.tracks[25605] = nil
g:polls(4)
check(g.pool[25605].x == -640, 'latched 10 s past the track')
g:polls(40)
check(g.pool[25605].x == -630 or g:has('step=hold_timeout;id=25608;reason=partner_ride'), 'after the latch or the hold time it ends')
g.env.vars.playerPosX, g.env.vars.playerPosZ = -620.5, 877.7
H({16, 25608, str32('veh_lv_0000'), 1, 1, -621000, 1000, 877700, 0, 3000, 1, 0})
check(g:has('step=hold;id=25608;here=25605;reason=player_near'), 'never onto the local player (3.5 m): held')
g.env.vars.playerPosX = -610.0
g:poll()
check(g.pool[25605].x == -621, 'applied once the player stands clear')
g.env.vars.playerPosX = 5000
g.W.paired = false
H({17, 25608, str32('veh_lv_0000'), 1, 1, -611000, 1000, 877700, 0, 3000, 1, 0})
check(g:has('step=hold;id=25608;here=0;reason=unpaired') and g.pool[25605].x == -621, 'unlinked: held')
g.W.paired = true
g:poll()
check(g.pool[25605].x == -611, 'applied once linked again')
g.env.vars.playerPosX, g.env.vars.playerPosZ = -601.0, 877.7
H({18, 25608, str32('veh_lv_0000'), 1, 1, -601000, 1000, 877700, 0, 3000, 1, 0})
g:polls(41)
check(g:has('step=hold_timeout;id=25608;reason=player_near') and g.pool[25605].x == -611, 'a hold ends after 10 s (the next refresh retries)')
g.env.vars.playerPosX = 5000
-- VEHICLE_GONE's loss here wins.
g.W.vehicleGone.gone[25606] = true
H({19, 25609, str32('veh_lv_0001'), 1, 1, -590000, 1000, 900000, 0, 3000, 1, 0})
check(g:has('step=refused;id=25609;reason=gone_here'), 'a vehicle this copy lost (VEHICLE_GONE) is refused')
-- A handler error turns only this module off.
local okErr = pcall(H, {20, 25608, str32('veh_lv_0000'), 1, 1, 'x', 0, 0, 0, 0, 1, 0, 4})
check(okErr and g:has('event=vehicle_state;step=error;') and not g.S.on(), 'a handler error is caught and turns the module off')

-- Levers: lab only, this copy only.
c = Fixture('host')
c:add(25608, -653.0, 877.7, 1, 'veh_lv_0000')
c:start()
c.env.vars.playerPosX, c.env.vars.playerPosZ = -655.0, 877.0
local ok, msg = c.S.lever({'vehicle_push', '25608', '3', '0'})
check(ok and msg == 'vehicle_push;id=25608;from=-653000,877700;to=-650000,877700;ok=1' and c.placed[1].y == 1300 and c.S.dirty[25608],
    'vehicle_push moves this copy\'s vehicle (y + 0.3 m) and marks it for the next scan')
ok, msg = c.S.lever({'vehicle_push', 'nearest', '0', '-2'})
check(ok and msg:find('vehicle_push;id=25608;', 1, true) and c.pool[25608].z == 875.7, 'vehicle_push nearest')
ok, msg = c.S.lever({'vehicle_life', '25608', '0'})
check(ok and msg == 'vehicle_life;id=25608;life=0;was=3000;now=0;alive=1;sv=1' and c:has('step=lever;id=25608;life=0;was=3000;now=0;alive=1;sv=1'),
    'vehicle_life sets this copy\'s body life and reads it back (0: the E5 wreck attempt)')
for _, bad in ipairs({{'vehicle_push', '25608', '3'}, {'vehicle_push', '25608', '60', '0'}, {'vehicle_push', 'x', '1', '1'},
    {'vehicle_push', '25608', 'x', '0'}, {'vehicle_push', '25608', '1', '1', '1'}, {'vehicle_life', '25608'},
    {'vehicle_life', '25608', '-1'}, {'vehicle_life', '25608', '1.5'}, {'vehicle_life', '25599', '10'}, {'vehicle_life', '25608', 'ten'}}) do
    ok, msg = c.S.lever(bad)
    check(not ok and msg == 'vehicle_lever_args', 'strict lever arguments: ' .. table.concat(bad, ' '))
end
c.riding = true; c.env.vars.playerVehicleGameObjectId = 25608
ok, msg = c.S.lever({'vehicle_push', '25608', '1', '1'})
check(not ok and msg == 'vehicle_lever_local_rider', 'never the vehicle the local player rides')
c.riding = false
c.W.mode = 2
ok, msg = c.S.lever({'vehicle_push', '25608', '1', '1'})
check(not ok and msg == 'vehicle_lever_lab_only', 'the levers are lab only')
ok, msg = c.S.lever({'vehicle_state_status'})
check(ok and msg:find('vstate;on=1;role=host;named=1;pool=', 1, true), 'vehicle_state_status (read-only, play too)')
ok, msg = c.S.lever({'vehicle_state_status', 'x'})
check(not ok and msg == 'vehicle_state_args', 'vehicle_state_status x is refused')
ok, msg = c.S.lever({'vehicle_state_status', '25608', '1'})
check(not ok and msg == 'vehicle_state_args', 'vehicle_state_status takes one id at most')
ok, msg = c.S.lever({'vehicle_state_status', '25608'})
check(ok and msg:find('vstate;id=25608;alive=1;pos=', 1, true) and msg:find('name=veh_lv_0000', 1, true), 'vehicle_state_status <id>')

-- Mother Base: the veh_clNN_clNN_0000 locators are names too.
c = Fixture('host', {mission = 30050})
c.W.mission = 30050
c:add(25601, 10.0, 20.0, 1, 'veh_cl01_cl00_0000')
c:start()
check(c:has('step=on;mission=30050;role=host;named=1;lrrp=0'), 'Mother Base jeeps are named by their cluster locators')

-- With world_sync.lua: CFG r[15] is the vehicle modules bitmask; the Damage observer reaches the host scan.
if arg[3] then
    local syncSrc = slurp(arg[3])
    local function Composed(r15, role)
        local logs, clock = {}, 5000
        local env = setmetatable({}, {__index = _G})
        env._G = env
        env.vars, env.mvars, env.svars = {missionCode = 30020}, {}, {}
        env.Fox = {StrCode32 = str32}
        env.Time = {GetRawElapsedTimeSinceStartUp = function() return clock / 1000 end}
        env.GameObject = {NULL_ID = 65535, GetGameObjectId = function() return 65535 end,
            SendCommand = function(id, cmd)
                if cmd.id == 'IsAlive' then return id == 25608 end
                if cmd.id == 'GetPosition' then return {GetX = function() return 1 end, GetY = function() return 2 end, GetZ = function() return 3 end} end
                if cmd.id == 'GetVehicleType' then return 1 end
            end}
        env.TppMission = {CheckMissionState = function() return true end, IsMissionStart = function() return true end}
        env.Mission = {ON_MESSAGE_RESULT_RESEND = 9}
        env.Tpp = {IsVehicle = function(id) return id >= 25600 and id <= 26111 end}
        env.e = {}
        setfenv(assert(loadstring(syncSrc)), env)()
        local W = env.e.MgsCoopWorld
        setfenv(assert(loadstring(GONE)), env)()
        setfenv(assert(loadstring(SRC)), env)()
        W.cfg = function() return {3, role, 1, 1, 7, 0, 1, 1, 0, 0, 0, 0, 0, 0, r15} end
        W.log = function(text) logs[#logs + 1] = text end
        W.begin()
        return W, logs
    end
    local W = Composed(2, 1)
    check(W.vehicleStateOn == true and W.vehicleGoneOn == false and W.vehicleState.on() and not W.vehicleGone.on(),
        'CFG r[15] = 2: VEHICLE_STATE alone')
    W.attClass = function() return 'local' end
    W.observe(W.M.GameObject, W.M.Damage, 25608, 0, 77)
    check(W.vehicleState.dirty[25608] == true, 'a Damage message on a vehicle id reaches the host scan through W.observe')
    W.vehicleGone.observe = function() error('fixture') end
    check(pcall(W.observe, W.M.GameObject, W.M.Damage, 25609, 0, 77) and not W.failed, 'a vehicle hook error never fails world sync')
    W = Composed(3, 2)
    check(W.vehicleStateOn and W.vehicleGoneOn, 'CFG r[15] = 3: both vehicle modules')
    W = Composed(1, 1)
    check(not W.vehicleStateOn and W.vehicleGoneOn and not W.vehicleState.on(), 'CFG r[15] = 1: VEHICLE_GONE alone (0.13.6 meaning kept)')
    W = Composed(0, 1)
    check(not W.vehicleStateOn and not W.vehicleGoneOn, 'CFG r[15] = 0: off')
    W = Composed(nil, 1)
    check(not W.vehicleStateOn and not W.vehicleGoneOn, 'a missing r[15] (an older loader) is off')
    W = Composed(7, 1)
    check(not W.vehicleStateOn and not W.vehicleGoneOn, 'an out-of-range r[15] is off')
end
print('world_vehicle_state_lua_check: ' .. checks .. ' checks PASS')
