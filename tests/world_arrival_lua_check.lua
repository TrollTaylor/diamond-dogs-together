-- arrival-prevent (docs/world-sync-design.md 9.2): lua/coop/world_arrival.lua with world_follow_seed.lua and
-- world_follow.lua on recording game APIs: the arrival helicopter watchdog (guest way out, host hint), the free-roam
-- route-start restart, the start-state and landing-message logs and lab probe guest_frozen_heli. No game process.
-- Usage: lua51 world_arrival_lua_check.lua <world_arrival.lua> <world_follow_seed.lua> <world_follow.lua>
local checks = 0
local function check(value, why)
    checks = checks + 1
    if not value then error(tostring(checks) .. ': ' .. why, 2) end
end
local OP = {TRANSITION = 9, SEED = 10, SEEDS_END = 11, REQ_MISSION = 12, FOLLOW_ACK = 13, REJECT = 4}
local function fixture(role, mission)
    local c = {calls = {}, tx = {}, logs = {}, notes = {}, time = 1000, inGame = true, link = 1}
    local env = setmetatable({}, {__index = _G})
    local function record(name) c.calls[#c.calls + 1] = name end
    env.vars = {missionCode = mission or 30050, locationCode = 50, mbLayoutCode = 3, mbClusterId = 0,
        initialPlayerFlag = 1, initialPlayerPosX = 9.116, initialPlayerPosY = 8.75, initialPlayerPosZ = -42.43, initialPlayerRotY = 0,
        playerVehicleGameObjectId = 77, playerPosX = 9.116, playerPosY = 8.75, playerPosZ = -42.43}
    env.mvars = {}
    env.gvars = {heli_missionStartRoute = 2691558836, ply_initialPlayerState = 1, ply_useMissionStartPos = true,
        ply_missionStartPos = {[0] = 9.116, [1] = 8.75, [2] = -42.43}, ply_missionStartRot = 0}
    env.svars = {ply_isUsedPlayerInitialAction = false}
    env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 1, MISSION_ABORT = 2, MISSION_RESTART = 3, CONTINUE_FROM_CHECK_POINT = 4},
        INITIAL_PLAYER_STATE = {ON_FOOT = 0, RIDEON_HELICOPTER = 1}}
    env.DemoDaemon = {IsDemoPlaying = function() return c.demo == true end}
    env.TppDemo = {GetMBDemoName = function() return c.mbDemo end, SetNextMBDemo = function(n) c.mbDemo = n end}
    env.Tpp = {IsHelicopter = function(id) return id == 77 end}
    env.Fox = {StrCode32 = function(s) return 'h:' .. s end}
    env.TppPlayer = {ResetInitialPosition = function() record('reset_initial'); env.vars.initialPlayerFlag = 0 end,
        ResetMissionStartPosition = function() record('reset_start'); env.gvars.ply_useMissionStartPos = false end}
    env.e = {ReservePlayerLoadingPosition = function() record('reserve_position') end,
        OnMessage = function(s, class, msg) record('message'); return c.messageResult end,
        OnAllocate = function() return 3 end}
    env.TppMission = {
        RestartMission = function() record('restart_menu') end,
        ExecuteRestartMission = function() record('restart') end,
        ResetMBFreeStartPositionToCommand = function()
            record('mb_foot')
            env.gvars.heli_missionStartRoute, env.gvars.ply_initialPlayerState = 0, 0
        end}
    local W = {OP = OP, handlers = {}, startHooks = {}, pairedHooks = {}, unpairedHooks = {}, stopHooks = {},
        level = 3, role = role, host = role == 1, active = true, mission = mission or 30050, epoch = 7, paired = true, eff = 3,
        mode = 1, probe = {handlers = {}, NAMES = {}}}
    env.e.MgsCoopWorld = W
    W.get = function(t, k) return t and t[k] end
    W.cfg = function() return {3, W.role, 1, c.link, 7, 0, 1, 55} end
    W.h = function() return 4294967295 end
    W.now = function() return c.time end
    W.inGame = function() return c.inGame end
    W.nextRid = function() c.rid = (c.rid or 0) + 1; return c.rid end
    W.log = function(s) c.logs[#c.logs + 1] = s end
    W.tx = function(op, ...) c.tx[#c.tx + 1] = {op = op, v = {...}}; return true end
    W.fail = function(s) c.failure = s; W.active = false end
    W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
    W.peer = function() return nil end
    for _, path in ipairs({arg[2], arg[3], arg[1]}) do
        local chunk = assert(loadfile(path)); setfenv(chunk, env); chunk()
    end
    W.follow.install({3, role})
    for _, f in ipairs(W.startHooks) do f() end
    for _, f in ipairs(W.pairedHooks) do f(false) end
    c.W, c.env, c.F, c.G, c.A = W, env, W.follow, W.arrivalWatch, W.follow.arrival
    return c
end
local function count(c, text)
    local n = 0
    for _, s in ipairs(c.logs) do if s:find(text, 1, true) then n = n + 1 end end
    return n
end
local function called(c, name)
    local n = 0
    for _, v in ipairs(c.calls) do if v == name then n = n + 1 end end
    return n
end
-- One tick at c.time + dt (the follow tick drives the watchdog).
local function at(c, dt) c.time = 1000 + dt; c.F.tick(c.time) end
local function line(c, text) for _, s in ipairs(c.logs) do if s:find(text, 1, true) then return s end end end

-- The friend's 0.14.5 arrival at Mother Base: a linked guest in its helicopter that never moves.
local g = fixture(2)
check(g.G and type(g.A.field) == 'function', 'the watchdog module loads after world_follow')
at(g, 1000)
at(g, 20999)
check(called(g, 'restart_menu') == 0, 'not before 20 s still')
at(g, 21000)
check(called(g, 'restart_menu') == 1 and #g.tx == 0 and g.A.pending and g.A.pending.mission == 30050 and
    count(g, 'event=arrival;step=unstick;why=static_heli;m=30050;still_ms=20000;pos=9116,8750,-42430') == 1,
    'still for 20 s in game: the guest restarts locally (the way out), logged, nothing to the host')
g.time = g.time + 1500
g.env.TppMission.ExecuteRestartMission()
g.env.e.ReservePlayerLoadingPosition(3)
check(called(g, 'restart') == 1 and called(g, 'mb_foot') == 1 and #g.tx == 0 and count(g, 'step=mb_foot;at=restart') == 1,
    'the fade restart passes and lands the guest on foot at Command')
at(g, 60000)
check(called(g, 'restart_menu') == 1, 'one way out per mission')

-- A helicopter that flies in, a cutscene, out of game, a landing, on foot: never fires.
local moving = fixture(2)
at(moving, 1000)
moving.env.vars.playerPosZ = moving.env.vars.playerPosZ + 1.5
at(moving, 2000)
at(moving, 60000)
check(called(moving, 'restart_menu') == 0 and count(moving, 'step=watch_end;why=moving;m=30050') == 1, 'a moving helicopter ends the watch')
local scene = fixture(2)
at(scene, 1000)
scene.demo = true
at(scene, 15000)
at(scene, 30000)
check(called(scene, 'restart_menu') == 0, 'a cutscene holds the clock')
scene.demo = false
at(scene, 31000)
at(scene, 50999)
check(called(scene, 'restart_menu') == 0, 'the 20 s start again after the cutscene')
at(scene, 51000)
check(called(scene, 'restart_menu') == 1, 'still 20 s after the cutscene: the way out')
local menu = fixture(2)
at(menu, 1000)
menu.inGame = false
at(menu, 22000)
check(called(menu, 'restart_menu') == 0, 'out of game (menus, loading) never counts')
local landed = fixture(2)
at(landed, 1000)
local r = landed.env.e.OnMessage(nil, 'h:GameObject', 'h:LandedAtLandingZone', 1, 2)
at(landed, 2000)
at(landed, 40000)
check(r == nil and called(landed, 'message') == 1 and called(landed, 'restart_menu') == 0 and
    count(landed, 'step=heli_msg;m=LandedAtLandingZone') == 1 and count(landed, 'step=watch_end;why=landed') == 1,
    'LandedAtLandingZone (logged) ends the watch; the original message handler still runs')
local walker = fixture(2)
walker.env.vars.playerVehicleGameObjectId = 0
at(walker, 1000)
at(walker, 6000)
at(walker, 40000)
check(called(walker, 'restart_menu') == 0 and count(walker, 'step=watch_end;why=on_foot') == 1, 'on foot for 5 s ends the watch')
local late = fixture(2)
late.env.vars.playerVehicleGameObjectId = 0
at(late, 1000)
late.env.vars.playerVehicleGameObjectId = 77
at(late, 3000)
at(late, 23000)
check(called(late, 'restart_menu') == 1, 'a short on-foot read at the start does not end the watch')
local old = fixture(2)
at(old, 181000)
at(old, 230000)
check(called(old, 'restart_menu') == 0 and count(old, 'step=watch_end;why=window') == 1, 'only the arrival (first 180 s) is watched')
local solo = fixture(2)
solo.link = 0
at(solo, 1000); at(solo, 21000)
check(called(solo, 'restart_menu') == 0 and count(solo, 'step=watch_end;why=solo') == 1, 'an unlinked guest copy is left alone')
local over = fixture(2)
over.env.svars.mis_isDefiniteGameOver = true
at(over, 1000); at(over, 21000)
check(called(over, 'restart_menu') == 0 and count(over, 'why=game_over') == 1, 'a game over is left alone')
local off = fixture(2)
off.G.on = false
at(off, 1000); at(off, 30000)
check(called(off, 'restart_menu') == 0 and count(off, 'step=unstick') == 0, 'G.on false: the watchdog is off (the logs stay)')
local acc = fixture(2, 40050)
at(acc, 1000); at(acc, 30000)
check(called(acc, 'restart_menu') == 0 and count(acc, 'event=arrival') == 0, 'the helicopter space (ACC) is not watched')

-- Free roam: the way out restarts at the route start (no stored start position), not at Command.
local field = fixture(2, 30020)
at(field, 1000); at(field, 21000)
check(called(field, 'restart_menu') == 1 and count(field, 'step=unstick;why=static_heli;m=30020') == 1, 'free roam: the same watchdog')
field.time = field.time + 1500
field.env.TppMission.ExecuteRestartMission()
field.env.vars.missionCode = 30020
field.env.e.ReservePlayerLoadingPosition(3)
check(called(field, 'restart') == 1 and #field.tx == 0 and called(field, 'mb_foot') == 0 and called(field, 'reset_initial') == 1 and
    called(field, 'reset_start') == 1 and field.env.gvars.heli_missionStartRoute == 2691558836 and
    count(field, 'step=field_route;at=restart;route=2691558836') == 1,
    'free roam: the restart Reserve clears the stored start position and keeps the helicopter route')

-- The host in the same state (a 0.14.5 save): a hint toast (note 15), never an action.
local host = fixture(1)
at(host, 1000); at(host, 21000); at(host, 60000)
check(called(host, 'restart_menu') == 0 and #host.notes == 1 and host.notes[1][1] == 15 and host.notes[1][2] == 30050 and
    host.notes[1][3] == 1 and count(host, 'step=host_hint;why=static_heli;m=30050') == 1, 'host: one hint toast, no restart')
local hostSolo = fixture(1)
hostSolo.link = 0
at(hostSolo, 1000); at(hostSolo, 21000)
check(#hostSolo.notes == 0, 'an unlinked host gets no co-op hint')

-- The start state (both roles) and the landing messages, bounded.
local s = fixture(2)
s.mbDemo = 'LongTimeNoSee_DdogSuperHighLikability'
for _, f in ipairs(s.W.startHooks) do f() end
check(line(s, 'event=arrival;step=start;m=30050;role=guest;flag=1;pos=9116,8750,-42430;rot=0;state=1;used=0;route=2691558836') and
    line(s, 'event=arrival;step=start_pos;use=1;mpos=9116,8750,-42430;mrot=0;layout=3;cluster=0;demo=LongTimeNoSee_DdogSuperHighLikability;heli=1'),
    'the start state is logged (initial and mission start positions, state, route, layout, cluster, MB demo)')
for _, l in ipairs(s.logs) do check(#l <= 160, 'every arrival line fits W.log (160)') end
for _ = 1, 20 do s.env.e.OnMessage(nil, 'h:GameObject', 'h:OnPlayerHeliHatchOpen') end
s.env.e.OnMessage(nil, 'h:GameObject', 'h:Dead')
check(count(s, 'step=heli_msg;m=OnPlayerHeliHatchOpen') == 16 and count(s, 'm=Dead') == 0, 'landing messages: 16 lines a mission, others ignored')
s.messageResult = 'resend'
s.W.api = {resend = 'resend'}
local before = count(s, 'step=heli_msg')
check(s.env.e.OnMessage(nil, 'h:GameObject', 'h:LandedAtLandingZone') == 'resend' and count(s, 'step=heli_msg') == before,
    'a message the game sends again is observed once (the resend result passes through)')

-- Lab probe guest_frozen_heli (id 20): the next sortie keeps the host's 0.14.5 start position seeds.
local lab = fixture(2)
check(lab.W.probe.NAMES[20] == 'guest_frozen_heli', 'probe 20 is guest_frozen_heli')
lab.W.probe.handlers.guest_frozen_heli({name = 'guest_frozen_heli'})
check(lab.A.legacyPose == true and count(lab, 'event=probe;id=guest_frozen_heli;step=done;result=ok') == 1, 'lab guest: armed')
lab.F.current, lab.W.mission = {41, 1, 30050, 40050}, 40050
for _, f in ipairs(lab.W.startHooks) do f() end
check(lab.F.arrivedMission == 40050 and lab.A.legacyPose == true, 'kept across the ACC hop')
lab.F.current, lab.W.mission = {42, 6, 40050, 30050}, 30050
for _, f in ipairs(lab.W.startHooks) do f() end
check(lab.F.arrivedMission == 30050 and lab.A.legacyPose == nil, 'cleared once the guest arrived in Mother Base or free roam')
local play = fixture(2)
play.W.mode = 0
play.W.probe.handlers.guest_frozen_heli({name = 'guest_frozen_heli'})
local hostProbe = fixture(1)
hostProbe.W.probe.handlers.guest_frozen_heli({name = 'guest_frozen_heli'})
check(play.A.legacyPose == nil and hostProbe.A.legacyPose == nil and count(play, 'result=refused') == 1 and
    count(hostProbe, 'result=refused') == 1, 'play mode and the host: refused')
print('world_arrival_lua_check: ' .. checks .. ' checks passed')
