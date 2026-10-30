-- M3 experiments E2 / E17 / E18 (docs/vehicles-companions-design.md section 8): the sortie (relief)
-- vehicle research levers in lua/coop/world_relief.lua against a fake world, fake vars and a fake
-- TppVehicle2 index 0. No game process.
-- Usage: lua51 world_relief_lua_check.lua <world_relief.lua>
-- Before this branch the module did not exist: every check here failed.
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

local function Fixture(mode, opts)
    opts = opts or {}
    local c = {logs = {}, calls = {}, sent = {}, warps = {}}
    local env = setmetatable({}, {__index = _G})
    env._G = env
    local att = {[0] = 0, [1] = 0, [2] = 0, [3] = 0}
    local paint = {}
    for i = 0, 9 do paint[i] = i end
    local u8 = {}
    for i = 0, 459 do u8[i] = i % 7 end
    env.vars = {missionCode = 30010, reliefVehicleCarryType = 0, reliefVehicleCarryAttitude = att,
        reliefVehicleCarryLife = {[0] = 100, [1] = 50}, reliefVehicleCarryAmmo = {[0] = 3}, reliefVehicleCarryState = 0,
        userPresetVehiclePaintType = paint, sortieLoadoutInfoU8buf = u8, currentSortieLoadoutSlot = 1,
        sortieLoadoutInfoSaveFormVersion = 3, currentSortieLoadoutSlotNonRental = 1, playerPosX = 800, playerPosZ = 1630,
        playerPosY = 343, playerVehicleGameObjectId = 65535}
    env.gvars = {heli_missionStartRoute = 1234}
    c.v0 = {alive = opts.alive ~= false, x = 805, y = 343, z = 1636.5, type = 2}
    local function vector(v) return {GetX = function() return v.x end, GetY = function() return v.y end, GetZ = function() return v.z end} end
    env.GameObject = {SendCommand = function(id, cmd)
        c.sent[#c.sent + 1] = {id = id, cmd = cmd}
        if id ~= 25600 then return nil end
        if cmd.id == 'IsAlive' then return c.v0.alive end
        if not c.v0.alive then return nil end
        if cmd.id == 'GetPosition' then return vector(c.v0) end
        if cmd.id == 'GetVehicleType' then return c.v0.type end
        if cmd.id == 'GetRiderId' then return {65535, 65535, 65535, 65535} end
        if cmd.id == 'Seize' then c.v0.alive = false; return nil end
        error('unexpected command ' .. tostring(cmd.id))
    end}
    env.TppMotherBaseManagement = {GetGmp = function() return 123456 end,
        GetResourceUsableCount = function(t) return t.resource == '4wdEast' and 7 or 0 end}
    local vehicle = {
        LoadCarry = function()
            c.calls[#c.calls + 1] = {'LoadCarry', env.vars.reliefVehicleCarryType, att[0], att[3]}
            if opts.loadError then error('load failed') end
            if opts.loadSpawns and env.vars.reliefVehicleCarryType % 256 > 0 then c.v0.alive = true end
        end,
        SaveCarry = function() c.calls[#c.calls + 1] = {'SaveCarry'} end}
    if opts.readonlyVehicle then
        env.Vehicle = setmetatable({}, {__index = vehicle, __newindex = function() error('read only') end})
    else
        env.Vehicle = vehicle
    end
    env.TppVarInit = {InitializeForNewMission = function() c.calls[#c.calls + 1] = {'Init', env.vars.reliefVehicleCarryType} end}
    env.TppMission = {SelectNextMissionHeliStartRoute = function(m, r) c.calls[#c.calls + 1] = {'Route', env.vars.reliefVehicleCarryType, m, r} end}
    for _, name in ipairs({'SeizeReliefVehicleOnAbort', 'SeizeReliefVehicleOnClear', 'SeizeReliefVehicleOnForceGoToMb'}) do
        env.TppMission[name] = function() c.calls[#c.calls + 1] = {name} end
    end
    env.TppTerminal = {OnFulton = function(...) c.calls[#c.calls + 1] = {'OnFulton', ...} end,
        OnFultonVehicle = function(...) c.calls[#c.calls + 1] = {'OnFultonVehicle', ...} end,
        AddTempResource = function(...) c.calls[#c.calls + 1] = {'AddTempResource', ...} end}
    env.TppPlayer = {Warp = function(t) c.warps[#c.warps + 1] = t end}
    local W = {mode = mode, host = opts.guest ~= true, role = opts.guest and 2 or 1}
    function W.get(t, k)
        if t == nil then return nil end
        local ok, v = pcall(function() return t[k] end)
        if ok then return v end
        return nil
    end
    W.log = function(text) c.logs[#c.logs + 1] = text end
    env.e = {MgsCoopWorld = W}
    setfenv(assert(loadstring(SRC)), env)()
    c.env, c.W, c.R = env, W, W.relief
    function c:has(pattern) for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then return l end end end
    function c:count(pattern) local k = 0 for _, l in ipairs(self.logs) do if l:find(pattern, 1, true) then k = k + 1 end end return k end
    function c:run(text)
        local a = {}
        for w in text:gmatch('%S+') do a[#a + 1] = w end
        return self.R.run(a)
    end
    return c
end

-- Module surface and the read-only census (any mode, no hooks).
local c = Fixture(2)
check(type(c.R) == 'table' and type(c.R.run) == 'function' and c.R.RID == 25600, 'W.relief with run() and index 0 = 25600')
local ok, text = c:run('relief_census arrival')
check(ok and text == 'relief;m=30010;t=0;v0=1;vt=2;x=805.0;z=1636.5', 'census receipt names the carry type and index 0: ' .. tostring(text))
check(c:has('event=relief;step=census;tag=arrival;m=30010;role=host;route=1234;ct=0;t=0;cp=0.00,0.00,0.00,0.000'), 'census line: mission, role, route, carry vars')
check(c:has('step=census_v0;tag=arrival;alive=1;vt=2;x=805.00;y=343.00;z=1636.50;rider0=65535;dist=8.2'), 'census_v0: index 0 pose, type, rider, distance')
check(c:has('step=census_res;tag=arrival;gmp=123456;4wdE=7;4wdW=0;'), 'census_res: GMP and the Mother Base vehicle resources')
check(c:has('step=census_paint;tag=arrival;p=0,1,2,3,4,5,6,7,8,9'), 'census_paint: the ten userPresetVehiclePaintType values')
check(c:has('step=census_carry;tag=arrival;life=100,50;ammo=3;st=0'), 'census_carry: life, ammo and state arrays')
check(c:has('step=census_lo;tag=arrival;slot=1;ver=3;nr=1') and c:count('step=census_lo') == 4, 'census_lo: slot header and three byte lines')
-- slot 1 starts at byte 115: 115 % 7 = 3, so the first bytes are 03 04 05 06 00
check(c:has('step=census_lo0;tag=arrival;b=0304050600'), 'census_lo0: the current slot bytes as hex')
check(c.env.Vehicle.LoadCarry ~= nil and c.R.hook == nil, 'the census installs no hook')

-- Play mode: every lever refuses and nothing is wrapped.
local originalLoad = c.env.Vehicle.LoadCarry
for _, cmd in ipairs({'relief_arm 2 805 343 1636.5 0', 'relief_seize skip', 'relief_remove instant', 'relief_face',
    'relief_load 2 805 343 1636.5 0'}) do
    local good, why = c:run(cmd)
    check(not good and why == 'relief_lab_only', 'play refuses ' .. cmd)
end
check(c.env.Vehicle.LoadCarry == originalLoad and c.R.hook == nil and #c.warps == 0 and c.v0.alive, 'play: no hook, no warp, index 0 untouched')

-- Lab: argument checks.
c = Fixture(1)
for _, cmd in ipairs({'relief_arm 11 0 0 0 0', 'relief_arm 2.5 0 0 0 0', 'relief_arm 2 x 0 0 0', 'relief_arm 2 0 0 0', 'relief_seize maybe',
    'relief_remove despawn', 'relief_remove instant loud', 'relief_face 3', 'relief_goto', 'relief_census a b',
    'relief_load 0 1 2 3 0', 'relief_load 11 1 2 3 0', 'relief_load 2 1 2 3', 'relief_load 2 1 2 3 9', 'relief_load 2 1 y 3 0'}) do
    local good, why = c:run(cmd)
    check(not good and why == 'relief_args', 'refused: ' .. cmd .. ' (' .. tostring(why) .. ')')
end
check(c.R.hook == nil, 'refused arguments install nothing')

-- E2 lever: armed in free roam, written at the ACC sortie (where the sortie prep's SetRelief writes), never
-- by the ACC's own mission start (its LoadCarry clears the carry vars).
ok, text = c:run('relief_arm 2 805 343 1636.5 0')
check(ok and text == 'relief_arm;type=2;hook=loadcarry+initnew+sortie', 'arm installs the three hooks: ' .. tostring(text))
check(c:has('step=installed;hook=loadcarry+initnew+sortie;save=1;seize=3;credit=3'), 'all hooks installed once')
check(c:has('step=armed;type=2;x=805.00;y=343.00;z=1636.50;yaw=0.00;hook=loadcarry+initnew+sortie'), 'armed line')
c.env.vars.missionCode = 40010
c.env.TppVarInit.InitializeForNewMission()
c.env.Vehicle.LoadCarry()
check(c.calls[#c.calls][1] == 'LoadCarry' and c.calls[#c.calls][2] == 0, 'in the ACC the original runs and sees no armed type')
check(c:has('step=arm_wait;where=initnew;m=40010') and c:has('step=arm_wait;where=loadcarry;m=40010') and c.R.armed ~= nil,
    'the ACC mission start waits (LoadCarry clears there)')
check(c:has('step=initnew;m=40010;before=0;ct=0;t=0') and c:has('step=loadcarry;m=40010;before=0;ct=0;t=0'), 'the ACC mission start is logged')
c.env.TppMission.SelectNextMissionHeliStartRoute(30010, 77)
check(c.calls[#c.calls][1] == 'Route' and c.calls[#c.calls][2] == 2 and c.calls[#c.calls][3] == 30010 and c.calls[#c.calls][4] == 77,
    'the sortie writes type 2 before the original route call, which keeps its arguments')
check(c:has('step=arm_applied;where=sortie;m=40010;ok=1;before=0;ct=2;t=2;cp=805.00,343.00,1636.50,0.000') and c.R.armed == nil,
    'applied once at the sortie and logged')
c.env.vars.missionCode = 30010
c.env.Vehicle.LoadCarry()
check(c.calls[#c.calls][2] == 2 and c.calls[#c.calls][3] == 805, 'free roam: LoadCarry reads type 2 at the armed position')
check(c:count('step=arm_applied') == 1, 'the arm is consumed')
-- Without the sortie (a copy that never runs the ACC route call), the free-roam mission start applies it.
local s2 = Fixture(1)
s2:run('relief_arm 4 1 2 3 0')
s2.env.TppVarInit.InitializeForNewMission()
check(s2.calls[#s2.calls][1] == 'Init' and s2.calls[#s2.calls][2] == 4 and s2:has('step=arm_applied;where=initnew;m=30010'),
    'free roam: the mission-start fallback writes before the original runs')
c.env.Vehicle.SaveCarry()
check(c.calls[#c.calls][1] == 'SaveCarry' and c:has('step=savecarry;m=30010;before=2;ct=2'), 'SaveCarry passes through, logged')
-- A second lever never wraps twice.
c:run('relief_seize stock')
check(c:count('step=installed') == 1, 'install runs once')

-- A Vehicle table that refuses writes (as the game's does, hidden run 698e67cf): the other hooks stand.
local r = Fixture(1, {readonlyVehicle = true})
ok, text = r:run('relief_arm 3 1 2 3 0')
check(ok and text == 'relief_arm;type=3;hook=initnew+sortie' and r:has('save=0;'), 'read-only Vehicle: initnew+sortie: ' .. tostring(text))
r.env.TppVarInit.InitializeForNewMission()
check(r.calls[#r.calls][1] == 'Init' and r.calls[#r.calls][2] == 3, 'the fallback writes before the original runs')

-- E18 lever: the guest's mission-end seize skipped; stock passes through.
c = Fixture(1, {guest = true})
ok, text = c:run('relief_seize skip')
check(ok and text == 'relief_seize=skip;hooks=3;hook=loadcarry+initnew+sortie', 'seize skip: ' .. tostring(text))
c.env.TppMission.SeizeReliefVehicleOnClear()
check(#c.calls == 0 and c:has('step=seize_skipped;fn=SeizeReliefVehicleOnClear;alive=1'), 'skip: the original never runs, logged with alive')
c:run('relief_seize stock')
c.env.TppMission.SeizeReliefVehicleOnAbort()
check(c.calls[#c.calls][1] == 'SeizeReliefVehicleOnAbort' and c:has('step=seize;fn=SeizeReliefVehicleOnAbort;alive=1'), 'stock: logged, then the original')
c.env.TppTerminal.AddTempResource(77, nil, 1)
check(c.calls[#c.calls][1] == 'AddTempResource' and c.calls[#c.calls][2] == 77 and c:has('step=credit;fn=AddTempResource;args=77,nil,1'),
    'the credit path is logged and passed through')

-- E17 lever: the stock Seize with one method option, never DirectAccount / CheckFultonType.
c = Fixture(1)
c.env.vars.playerVehicleGameObjectId = 25600
local good, why = c:run('relief_remove instant')
check(not good and why == 'relief_ridden' and c.v0.alive, 'never removes index 0 under the local player')
c.env.vars.playerVehicleGameObjectId = 65535
ok, text = c:run('relief_remove instant')
local seize
for _, s in ipairs(c.sent) do if s.cmd.id == 'Seize' then seize = s end end
check(ok and text == 'relief_remove;how=instant;quiet=0;alive=0', 'remove result: ' .. tostring(text))
check(seize and seize.id == 25600 and #seize.cmd.options == 1 and seize.cmd.options[1] == 'Instant', 'Seize {"Instant"} on 25600 only')
check(c:has('step=remove;how=instant;quiet=0;alive_after=0;x=805.0;z=1636.5;gmp=123456'), 'remove line with the resources')
-- Without quiet the stock credit path runs (hidden run fafd3fdd: Instant still sent Fulton/FultonInfo and AddTempResource 13).
c.env.TppTerminal.OnFulton(25600, 0, 0, 13)
check(c.calls[#c.calls][1] == 'OnFulton' and c:has('step=credit;fn=OnFulton;args=25600,0,0,13'), 'not quiet: OnFulton passes')
good, why = c:run('relief_remove fadeout')
check(not good and why == 'relief_absent', 'an absent index 0 refuses')
-- quiet: this copy's TppTerminal.OnFulton for 25600 is skipped once (no credit); other ids and later calls pass.
local q = Fixture(1)
ok, text = q:run('relief_remove fadeout quiet')
check(ok and text == 'relief_remove;how=fadeout;quiet=1;alive=0', 'quiet remove: ' .. tostring(text))
local before = #q.calls
q.env.TppTerminal.OnFulton(1234, 0, 0, 5)
check(#q.calls == before + 1 and q.calls[#q.calls][2] == 1234, 'quiet never skips another id')
q.env.TppTerminal.OnFulton(25600, 0, 0, 13)
check(#q.calls == before + 1 and q:has('step=credit_skipped;fn=OnFulton;args=25600,0,0,13'), 'quiet skips the 25600 credit once')
q.env.TppTerminal.OnFulton(25600, 0, 0, 13)
check(#q.calls == before + 2, 'the skip is used up')
for how, opt in pairs({fadeout = 'FadeOut', fulton = 'Fulton'}) do
    local f = Fixture(1)
    f:run('relief_remove ' .. how)
    local s
    for _, x in ipairs(f.sent) do if x.cmd.id == 'Seize' then s = x end end
    check(s and #s.cmd.options == 1 and s.cmd.options[1] == opt, how .. ' sends only ' .. opt)
end

-- Captures: turn in place toward index 0 (never a move: hidden run fafd3fdd fell through the ground after a warp).
c = Fixture(1)
ok, text = c:run('relief_face')
local w = c.warps[1]
check(ok and #c.warps == 1 and w.pos[1] == 800 and w.pos[2] == 343 and w.pos[3] == 1630 and math.abs(w.rotY - 37.57) < 0.01,
    'relief_face keeps the position and faces the vehicle: ' .. tostring(text))
c.env.vars.playerPosX = 700
good, why = c:run('relief_face')
check(not good and why:match('^relief_far;dist=') and #c.warps == 1, 'farther than 20 m: no warp')
c.v0.alive = false
good, why = c:run('relief_face')
check(not good and why == 'relief_absent', 'face refuses without index 0')

-- M3 item 6 (a guest continue or late join): relief_load writes the carry vars and calls Vehicle.LoadCarry
-- now, mid mission, as the destination's mission start would; free roam only, never over a live index 0.
c = Fixture(1)
local calls0 = #c.calls
good, why = c:run('relief_load 2 805 343 1636.5 2.488')
check(not good and why == 'relief_present' and #c.calls == calls0 and c.env.vars.reliefVehicleCarryType == 0,
    'a live index 0 refuses: nothing written, LoadCarry not called')
c = Fixture(1, {alive = false, loadSpawns = true})
c.env.vars.missionCode = 40020
good, why = c:run('relief_load 2 805 343 1636.5 2.488')
check(not good and why == 'relief_not_free_roam' and #c.calls == 0, 'the ACC refuses (its LoadCarry clears the carry)')
c.env.vars.missionCode = 30020
c:run('relief_arm 3 1 2 3 0')
good, why = c:run('relief_load 2 805 343 1636.5 2.488')
check(not good and why == 'relief_armed_pending' and c.env.vars.reliefVehicleCarryType == 0, 'an armed sortie choice refuses')
c = Fixture(1, {alive = false, loadSpawns = true})
c.env.vars.missionCode = 30020
ok, text = c:run('relief_load 2 805 343 1636.5 2.488')
check(ok and text == 'relief_load;type=2;call=1;alive=1', 'load result: ' .. tostring(text))
check(#c.calls == 1 and c.calls[1][1] == 'LoadCarry' and c.calls[1][2] == 2 and c.calls[1][3] == 805 and c.calls[1][4] == 2.488,
    'LoadCarry called once, after the type and the attitude are written')
check(c:has('event=relief;step=load;m=30020;write=1;call=1;before=0;ct=2;t=2;cp=805.00,343.00,1636.50,2.488;alive_after=1'), 'load line')
check(c.R.hook == nil, 'load installs no hook')
local e = Fixture(1, {alive = false, loadError = true})
good, why = e:run('relief_load 9 805 343 1636.5 0')
check(not good and why == 'relief_load;type=9;call=0;alive=0' and e:has('step=load;m=30010;write=1;call=0;before=0;ct=9;'),
    'a LoadCarry error is reported, not raised')

-- M3: relief_arm takes an optional paint (0..15 or fova0..fova2) for that type's preset (type 2: index 0).
c = Fixture(1)
c.env.Vehicle.paintType = {FOVA_0 = 0, FOVA_1 = 1, FOVA_2 = 2, NONE = 3}
for _, cmd in ipairs({'relief_arm 2 1 2 3 0 16', 'relief_arm 2 1 2 3 0 fova3', 'relief_arm 2 1 2 3 0 x', 'relief_arm 2 1 2 3 0 1 2'}) do
    local good, why = c:run(cmd)
    check(not good and why == 'relief_args', 'refused paint: ' .. cmd)
end
ok, text = c:run('relief_arm 2 1 2 3 0 fova1')
check(ok and c:has('step=armed;type=2;x=1.00;y=2.00;z=3.00;yaw=0.00;hook=loadcarry+initnew+sortie;paint=1'), 'armed with paint fova1 = 1')
c.env.vars.missionCode = 40020
c.env.TppMission.SelectNextMissionHeliStartRoute(30020, 1)
check(c.env.vars.userPresetVehiclePaintType[0] == 1 and c.env.vars.userPresetVehiclePaintType[1] == 1, 'the type 2 preset (index 0) is written')
c:run('relief_census paint')
check(c:has('step=census_paint;tag=paint;p=1,1,2,3,4,5,6,7,8,9;fova=0,1,2;none=3'), 'census_paint lists the paint enum')
-- relief_status and relief_lab go to the M3 module (W.reliefSync), in any mode.
c = Fixture(2)
good, why = c:run('relief_status')
check(not good and why == 'relief_sync_unavailable', 'no M3 module: refused')
c.W.reliefSync = {lever = function(a) return true, 'sync;' .. table.concat(a, ' ') end}
ok, text = c:run('relief_status')
check(ok and text == 'sync;relief_status', 'relief_status routed in play')
ok, text = c:run('relief_lab nonactive vehicle')
check(ok and text == 'sync;relief_lab nonactive vehicle', 'relief_lab routed (the module checks the mode)')

-- M6 E6: vehicle_census (read-only, any mode): the live ids from 25600+first with type, seat count, riders and pose.
c = Fixture(2)
ok, text = c:run('vehicle_census 0 4')
check(ok and text == 'vehicle_census;first=0;n=4;found=1;25600:2:4', 'one live vehicle in 25600..25603: ' .. tostring(text))
check(c:has('event=vehicle_census; id=25600; type=2; seats=4; riders=65535,65535,65535,65535; x=805.0; y=343.0; z=1636.5'),
    'one census line per live vehicle')
for _, s in ipairs(c.sent) do
    check(s.cmd.id == 'GetVehicleType' or s.cmd.id == 'GetRiderId' or s.cmd.id == 'GetPosition', 'read-only commands only: ' .. tostring(s.cmd.id))
end
check(#c.sent == 6 and c.sent[1].id == 25600 and c.sent[6].id == 25603, 'four ids probed, three reads of the live one')
ok, text = c:run('vehicle_census')
check(ok and text:find('vehicle_census;first=0;n=48;found=1;', 1, true), 'the default range 0..47')
for _, bad in ipairs({'vehicle_census 512', 'vehicle_census 0 65', 'vehicle_census 0 0', 'vehicle_census x', 'vehicle_census 1.5', 'vehicle_census 0 4 9'}) do
    ok, text = c:run(bad)
    check(not ok and text == 'vehicle_census_args', 'strict census arguments: ' .. bad)
end

print('world_relief_lua_check: ' .. checks .. ' checks PASS')
