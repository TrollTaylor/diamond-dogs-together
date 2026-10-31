-- Debug menu Travel and Bring partner here (lua/coop/world_goto.lua "Travel"; docs/debug-menu.md "Travel").
-- Pure fixture: stubbed game bindings, no native calls.
local path = assert(arg[1], "world_goto.lua path")
local count = 0
local function check(v, s) count = count + 1; assert(v, s) end
local function setup(host)
  local c = {now = 1000, calls = {}, tx = {}, notes = {}, logs = {}, state = true, demo = false, foot = true, peer = {mission = 30050, level = 3, linked = true}}
  local function call(name) return function(...) c.calls[#c.calls + 1] = {name, ...} end end
  local W = {mode = 0, host = host, active = true, level = 3, paired = true, OP = {GOTO = 115, GOTO_ACK = 120}, handlers = {},
    api = {str32 = function() return 19 end}, follow = {}}
  W.get = function(t, k) return t and t[k] end
  W.log = function(s) c.logs[#c.logs + 1] = s end
  W.tx = function(op, ...) c.tx[#c.tx + 1] = {op, ...}; return true end
  W.note = function(code, a, b) c.notes[#c.notes + 1] = {code, a, b}; return true end
  W.now = function() return c.now end
  W.peer = function() return c.peer end
  W.tick = function() end
  local loads = {MISSION_FINALIZE = 1, MISSION_ABORT = 2}
  e = {MgsCoopWorld = W, ReservePlayerLoadingPosition = function(kind) c.calls[#c.calls + 1] = {"reserve", kind} end,
    OnAllocate = function() c.calls[#c.calls + 1] = {"allocate"} end}
  vars = {missionCode = 30050, playerPosX = 10, playerPosY = 0.8, playerPosZ = -40, playerRotY = 90}
  gvars = {mis_nextMissionCodeForEmergency = 0, mis_nextMissionCodeForMissionClear = 0, heli_missionStartRoute = 0}
  mvars = {heli_missionStartRoute = 77}
  TppDefine = {MISSION_LOAD_TYPE = loads, MISSION_CLEAR_TYPE = {RIDE_ON_HELICOPTER = 1}, SYS_MISSION_ID = {MTBS_HELI = 40050}}
  TppClock = {DEPLOY_TIME = {CURRENT = 1}}
  TppMission = {CheckMissionState = function() return c.state end, SelectNextMissionHeliStartRoute = call("route"),
    OnEndMissionPreparation = function(...) c.calls[#c.calls + 1] = {"deploy", ...}; c.deployRoute = mvars.heli_missionStartRoute end,
    ReserveMissionClearOnRideOnHelicopter = call("ride_heli"), ReserveMissionClear = call("clear"),
    Load = function(n, cur, o) c.calls[#c.calls + 1] = {"load", n, o} end}
  TppPlayer = {SetInitialPosition = call("initial"), Warp = call("warp")}
  TppDemo = {SetNextMBDemo = call("mb_demo")}
  f30050_sequence = {ReserveMissionClear = call("f30050_clear")}
  DemoDaemon = {IsDemoPlaying = function() return c.demo end}
  PlayerStatus = {STAND = 1, SQUAT = 2, CRAWL = 3}
  PlayerInfo = {OrCheckStatus = function() return c.foot end}
  assert(loadfile(path))()
  c.W, c.T = W, W.travel
  function c.find(name)
    for i = #c.calls, 1, -1 do if c.calls[i][1] == name then return c.calls[i] end end
  end
  function c.tick(dt) c.now = c.now + (dt or 100); W.tick() end
  function c.rx(...) W.handlers[59]({...}, c.now) end
  function c.lasttx() return c.tx[#c.tx] end
  return c
end

-- The host presses Go to Africa in Mother Base: both are told, the host leads two stock hops.
local c = setup(true)
check(c.T and c.W.OP.TRAVEL == 59, "W.travel and OP.TRAVEL 59 (0x3B)")
local ok, msg = c.T.command({"travel", "30020"})
check(ok and msg:find("travel_started;target=30020", 1, true), "host Go to Africa starts")
local t = c.lasttx()
check(t[1] == 59 and t[3] == 2 and t[4] == 30020 and t[5] == -656010 and t[6] == 1200 and t[7] == 877730 and t[8] == 62000,
  "TRAVEL 2 GO carries the target and the Masa Village spot (mm, mdeg)")
check(c.notes[#c.notes][1] == 14 and c.notes[#c.notes][2] == 30020 and c.notes[#c.notes][3] == 1, "note 14: Travelling to Africa")
ok, msg = c.T.command({"travel", "30020"})
check(not ok and msg == "travel_refused;reason=travelling", "a second press while travelling is refused")
c.tick()
check(c.find("f30050_clear") and not c.find("ride_heli"), "Mother Base leaves through f30050_sequence.ReserveMissionClear")
local n = #c.calls; c.tick(); check(#c.calls == n, "hop 1 is called once")
TppMission.Load(40050, 30050, {showLoadingTips = true})
local l = c.find("load")
check(l[2] == 40050 and l[3].waitOnLoadingTipsEnd == false and l[3].showLoadingTips == true, "travel loads skip the loading-tips key wait")
-- In the ACC: 2 s settle, then the on-foot sortie (route 0).
vars.missionCode = 40050; c.tick(500)
check(not c.find("route"), "the ACC settles first")
c.tick(2100)
local r = c.find("route")
check(r and r[2] == 30020 and r[3] == 0 and r[4] == false, "ACC: SelectNextMissionHeliStartRoute(30020, 0, false)")
gvars.mis_nextMissionCodeForMissionClear = 30020
TppMission.OnEndMissionPreparation(1)
check(c.deployRoute == nil, "the route-0 sortie lands on foot (mvars route cleared)")
-- The target's reserve: SetInitialPosition on the spot, 0.5 m up.
vars.missionCode = 30020
e.ReservePlayerLoadingPosition(1)
local i = c.find("initial")
check(i and math.abs(i[2][1] + 656.01) < 1e-6 and math.abs(i[2][2] - 1.70) < 1e-6 and math.abs(i[2][3] - 877.73) < 1e-6 and i[3] == 62,
  "Africa reserve: TppPlayer.SetInitialPosition on the spot")
-- Arrival: on the spot within 3 m -> done.
vars.playerPosX, vars.playerPosY, vars.playerPosZ = -655.5, 1.2, 878
c.tick(200); c.tick(1600)
check(c.T.job == nil and c.logs[#c.logs]:find("step=arrived;mission=30020", 1, true), "arrived and logged")
TppMission.Load(30010, 30020, {})
check(c.find("load")[3].waitOnLoadingTipsEnd == nil, "loads after travel are untouched")

-- A far landing is corrected with a warp.
c = setup(true)
c.T.command({"travel", "30050"})
vars.playerPosX, vars.playerPosY, vars.playerPosZ = 100, 0, 100
c.tick(200); c.tick(1600)
local w = c.find("warp")
check(w and math.abs(w[2].pos[1] - 22.61) < 1e-6 and w[2].rotY == 0, "same mission: Go to Mother Base warps to the deck ladder")
check(c.lasttx()[3] == 2 and c.lasttx()[4] == 30050, "the guest is told (GO 30050)")

-- The guest asks; the host leads; the guest follows (paired) and lands 4 m in front.
c = setup(false)
ok, msg = c.T.command({"travel", "30050"})
check(ok and msg == "travel_asked;target=30050" and c.lasttx()[3] == 1 and c.lasttx()[4] == 30050 and not c.T.job,
  "guest: TRAVEL 1 ASK to the host, nothing moves yet")
vars.missionCode = 30020
c.rx(1, 2, 30050, 22610, 1800, -15550, 0, 0)
check(c.T.job and c.T.job.target == 30050 and not c.T.job.lead, "paired guest: follows the host's travel (world follow)")
c.tick(100)
check(not c.find("ride_heli") and not c.find("f30050_clear"), "a following guest never calls a hop")
vars.missionCode = 30050
e.OnAllocate()
check(c.find("mb_demo"), "Mother Base arrival demo cleared on the follower")
e.ReservePlayerLoadingPosition(1)
i = c.find("initial")
check(i and math.abs(i[2][1] - 22.61) < 1e-6 and math.abs(i[2][3] - (-15.55 + 4)) < 1e-6 and i[3] == 180,
  "the follower lands 4 m in front of the host's spot, facing back")
vars.playerPosX, vars.playerPosY, vars.playerPosZ = 22.61, 1.8, -11.55
c.tick(200); c.tick(1600)
check(c.T.job == nil, "follower arrived")
local any2 = false
for _, x in ipairs(c.notes) do if x[1] == 14 and x[3] == 2 then any2 = true end end
check(not any2, "the asker is not told its partner brought it")

-- The host answers an ASK by leading.
c = setup(true)
c.rx(1, 1, 30010, 0, 0, 0, 0, 0)
check(c.T.job and c.T.job.target == 30010 and c.T.job.lead, "host: ASK 30010 starts its own lead")
local go, ans
for _, x in ipairs(c.tx) do if x[3] == 2 then go = x elseif x[3] == 4 then ans = x end end
check(go and go[4] == 30010 and ans and ans[9] == 0, "host announces GO and answers accepted")
c.rx(2, 1, 30020, 0, 0, 0, 0, 0)
check(c.lasttx()[3] == 4 and c.lasttx()[9] == 1, "a second ASK while travelling: answer 1 (travelling)")

-- Bring partner here, same mission: the receiver warps 3 m in front of the caller, facing them.
c = setup(true)
ok, msg = c.T.command({"bring_partner"})
t = c.lasttx()
check(ok and t[3] == 3 and t[4] == 30050 and t[5] == 10000 and t[6] == 800 and t[7] == -40000 and t[8] == 90000,
  "caller: TRAVEL 3 BRING with its mission and pose")
check(c.notes[#c.notes][3] == 4, "caller note: Bringing your partner to you")
c = setup(false)
c.rx(5, 3, 30050, 10000, 800, -40000, 90000, 0)
w = c.find("warp")
check(w and math.abs(w[2].pos[1] - 13) < 1e-6 and math.abs(w[2].pos[3] + 40) < 1e-6 and w[2].rotY == 270, "receiver warps beside the caller")
check(c.lasttx()[3] == 4 and c.lasttx()[9] == 0 and c.notes[#c.notes][3] == 3, "answer accepted, note: brought you here")
-- Refusals: riding, cutscene, downed.
c = setup(false); c.foot = false
c.rx(6, 3, 30050, 0, 0, 0, 0, 0)
check(not c.find("warp") and c.lasttx()[9] == 3, "riding/carried: refused (3)")
c = setup(false); c.demo = true
c.rx(6, 3, 30050, 0, 0, 0, 0, 0)
check(c.lasttx()[9] == 5, "cutscene: refused (5)")
c = setup(false); c.W.downed = {hud = function() return {down = true} end}
c.rx(6, 3, 30050, 0, 0, 0, 0, 0)
check(c.lasttx()[9] == 4, "downed: refused (4)")
-- The caller hears a refusal.
c = setup(true)
c.rx(7, 4, 30050, 0, 0, 0, 0, 3)
check(c.notes[#c.notes][1] == 14 and c.notes[#c.notes][3] == 5, "a refused answer: note 5")

-- Bring partner here across missions: a guest in Africa is brought by the host in Mother Base (it leads its own hops).
c = setup(false)
vars.missionCode = 30020; c.W.paired = false
c.rx(8, 3, 30050, 22610, 1800, -15550, 0, 0)
check(c.T.job and c.T.job.target == 30050 and c.T.job.lead and c.lasttx()[9] == 0, "guest elsewhere: leads its own travel to the caller")
c.W.follow.token = "outer"
TppMission.ReserveMissionClearOnRideOnHelicopter = function() c.tokenSeen = c.W.follow.token end
c.tick()
check(c.tokenSeen == true and c.W.follow.token == "outer", "the guest's own hop bypasses the follow guard (F.token) and restores it")
check(c.T.job.hop1, "hop 1 recorded")
vars.missionCode = 30050
e.ReservePlayerLoadingPosition(1)
i = c.find("initial")
check(i and math.abs(i[2][3] - (-15.55 + 3)) < 1e-6, "lands 3 m in front of the caller")

-- Refusals at the menu.
c = setup(true); vars.missionCode = 10020
ok, msg = c.T.command({"travel", "30010"})
check(not ok and msg == "travel_refused;reason=mission", "story mission refused")
c = setup(true)
ok, msg = c.T.command({"travel", "10020"})
check(not ok and msg == "travel_target", "only 30010/30020/30050")
c = setup(true); c.peer = {mission = 0, level = 0, linked = false}
ok, msg = c.T.command({"bring_partner"})
check(not ok and msg == "partner_not_linked", "bring needs a linked partner")
ok, msg = c.T.command({"travel", "30010"})
check(ok and msg:find("linked=0", 1, true) and #c.tx == 0, "Go to alone without a partner link")

-- The ACC: refused while mission prep (any sequence but the main game) or the iDroid is open.
c = setup(true); vars.missionCode = 40050
TppSequence = {GetCurrentSequenceName = function() return c.seq end}
TppUiCommand = {IsMbDvcTerminalOpened = function() return c.idroid end}
c.seq = "Seq_Game_MissionPreparation"
c.rx(9, 1, 30010, 0, 0, 0, 0, 0)
check(not c.T.job and c.lasttx()[9] == 2, "ACC mission prep: an ask is refused (2)")
c.seq = "Seq_Game_MainGame"; c.idroid = true
c.rx(10, 1, 30010, 0, 0, 0, 0, 0)
check(not c.T.job and c.lasttx()[9] == 2, "iDroid open: refused (2)")
c.idroid = false
c.rx(11, 1, 30010, 0, 0, 0, 0, 0)
check(c.T.job and c.T.job.target == 30010, "ACC main game: the host leads at once")
c.seq = "Seq_Game_MissionPreparation"; c.tick(100); c.tick(2500)
check(not c.find("route"), "the sortie waits while the ACC is not in its main game")
c.seq = "Seq_Game_MainGame"; c.tick(100); c.tick(2100)
check(c.find("route"), "then the on-foot sortie")
TppSequence, TppUiCommand = nil, nil

-- Timeout: 240 s ends the job with note 6.
c = setup(true)
c.T.command({"travel", "30020"})
c.state = false
c.tick(240001)
check(c.T.job == nil and c.notes[#c.notes][3] == 6, "a trip older than 240 s stops (note 6)")
print("world_travel_lua_check: " .. count .. " checks passed")
