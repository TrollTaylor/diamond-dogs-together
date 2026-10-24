-- The developer test boot (lua/coop/debug_boot.lua, docs/test-boot.md) against
-- stubbed game bindings: inert without the loader's answer; fast boot takes the
-- game's own new-game logo branch, the title's PressStart and menu selects; a
-- boot target restarts in the ACC, sorties on foot and lands on its spot.
local path = assert(arg[1]); local f = assert(io.open(path, "rb")); local src = f:read("*a"); f:close()
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ": " .. why, 2) end end
check(#src:gsub("\r\n", "\n") <= 16000 and #src:gsub("\r?\n", "\r\n") <= 16000, "embedded fragment fits both checkout modes")
local function has(list, pattern) for _, v in ipairs(list) do if v:find(pattern) then return true end end return false end
local function count(list, pattern) local n = 0; for _, v in ipairs(list) do if v:find(pattern) then n = n + 1 end end return n end
-- reply: the CFG answer (nil = the literal string, as without the loader).
local function F(reply)
 local c = {logs = {}, calls = {}, seq = "", clock = 0, popup = false, splash = {}, inGame = false, foot = false}
 local function call(name) c.calls[#c.calls + 1] = name end
 local env = setmetatable({}, {__index = _G}); env._G = env
 env.loadstring = function(source)
  if source == "return 'MGS_COOP_BOOT_CFG_V1'" and reply then return function() return unpack(reply) end end
  local log = source:match("^return 'MGS_COOP_BOOT_LOG_V1' %-%- (.*)$")
  if log then return function() c.logs[#c.logs + 1] = log; return true end end
  return loadstring(source)
 end
 env.vars = {missionCode = 1, playerPosX = 0, playerPosY = 0, playerPosZ = 0, playerRotY = 0, clock = 0}
 env.gvars = {ini_isTitleMode = false, mis_nextMissionCodeForEmergency = 0, mis_nextMissionCodeForMissionClear = 0, heli_missionStartRoute = 0}
 env.mvars = {}
 env.Time = {GetRawElapsedTimeSinceStartUp = function() return c.clock / 1000 end}
 env.TppSequence = {GetCurrentSequenceName = function() return c.seq end, SetNextSequence = function(s) call("next:" .. s) end}
 env.TppSave = {IsNewGame = function() return false end}
 env.SplashScreen = {GetSplashScreenWithName = function(n) return c.splash[n] end, Delete = function(id) call("delete:" .. tostring(id)); for k, v in pairs(c.splash) do if v == id then c.splash[k] = nil end end end}
 env.TppUiCommand = {IsShowPopup = function() return c.popup end, ErasePopup = function() call("erase"); c.popup = false end}
 env.TppMission = {CheckMissionState = function() return c.inGame end,
  SelectNextMissionHeliStartRoute = function(m, r, a) call(("route:%d:%s:%s"):format(m, tostring(r), tostring(a))); env.gvars.mis_nextMissionCodeForMissionClear = m; env.gvars.heli_missionStartRoute = r end,
  OnEndMissionPreparation = function(t, cl) call(("deploy:%s:%s:%s"):format(tostring(t), tostring(cl), tostring(env.mvars.heli_missionStartRoute))) end,
  Load = function(n, cur, o) c.loadOptions = o; call("load:" .. tostring(n)) end}
 env.TppPlayer = {SetInitialPosition = function(p, y) c.initial = {p[1], p[2], p[3], y}; call("initial") end,
  Warp = function(t) c.warp = t; call("warp"); env.vars.playerPosX, env.vars.playerPosY, env.vars.playerPosZ = t.pos[1], t.pos[2], t.pos[3] end}
 env.DemoDaemon = {IsDemoPlaying = function() return false end}
 env.Player = {IsInstanceValid = function() return true end}
 env.PlayerStatus = {STAND = 1, SQUAT = 2, CRAWL = 3}
 env.PlayerInfo = {OrCheckStatus = function() return c.foot end}
 env.TppDemo = {GetMBDemoName = function() return c.mbDemo end, SetNextMBDemo = function(n) call("mbdemo:" .. tostring(n)); c.mbDemo = nil end}
 env.TppClock = {DEPLOY_TIME = {CURRENT = 0}, SetTime = function(s) call("time:" .. s) end}
 env.TppWeather = {ForceRequestWeather = function(w, s) call(("weather:%d:%d"):format(w, s)) end}
 env.TppDefine = {MISSION_LOAD_TYPE = {MISSION_FINALIZE = 0, CONTINUE_FROM_CHECK_POINT = 3}, WEATHER = {SUNNY = 0, CLOUDY = 1, RAINY = 2, SANDSTORM = 3, FOGGY = 4}}
 local e = {}
 e.OnUpdate = function() call("update") end
 e.OnAllocate = function() call("allocate") end
 e.ReservePlayerLoadingPosition = function(kind) call("reserve:" .. tostring(kind)); return "r1", nil, "r3" end
 env.e = e
 setfenv(assert(loadstring(src)), env)()
 c.env, c.e, c.B = env, e, e.MgsCoopBoot
 function c.frame(ms) c.clock = c.clock + (ms or 100); e.OnUpdate() end
 function c.mission(m, title) env.vars.missionCode = m; env.gvars.ini_isTitleMode = title == true; c.calls = {}; e.OnAllocate({sequence = c.sequence}) end
 return c
end
-- Without the loader (literal receipt) or with "return 0": inert, originals untouched.
for _, reply in ipairs({false, {0}}) do
 local c = F(reply or nil)
 c.sequence = {StartPreTitleSequence = function() end}
 local start = c.sequence.StartPreTitleSequence
 c.mission(1)
 c.seq = "Seq_Demo_WaitCopyRightLogo"; c.splash.cesa = 7
 for _ = 1, 3 do c.frame() end
 check(c.sequence.StartPreTitleSequence == start and #c.logs == 0 and not has(c.calls, "^delete"), "inert boot changes nothing")
 c.env.TppMission.Load(30010, 40010, {x = 1})
 check(c.loadOptions.x == 1 and c.loadOptions.waitOnLoadingTipsEnd == nil, "inert boot keeps the load options")
 local a, b, d = c.e.ReservePlayerLoadingPosition(0)
 check(a == "r1" and b == nil and d == "r3" and not c.initial, "reserve passes its results through")
end
-- Fast boot (host, no target).
local c = F({1, 1, 0, 0, 0, 0, 0, 0, -1, 0, 1})
local seen
c.sequence = {StartPreTitleSequence = function() seen = c.env.TppSave.IsNewGame() end}
c.mission(1)
check(has(c.logs, "^fast;step=config;ms=%d+;fast=1;target=0;host=1;pos=0$"), "config logged")
c.seq = "Seq_Demo_WaitCopyRightLogo"; c.splash.cesa = 7
c.frame()
check(has(c.calls, "^delete:7$") and has(c.logs, "^fast;step=copyright") and has(c.logs, "^trace;step=seq;ms=%d+;mission=1;title=0;name=Seq_Demo_WaitCopyRightLogo$"),
 "the copyright splash is deleted and the sequence traced")
c.seq = "Seq_Demo_LogInKonamiServer"; c.popup = true; c.frame(); c.popup = true; c.frame()
check(count(c.calls, "^erase$") == 2 and count(c.logs, "^fast;step=popup;.*seq=Seq_Demo_LogInKonamiServer") == 1, "the offline login notice is closed (logged once)")
c.sequence.StartPreTitleSequence()
check(seen == true and c.env.TppSave.IsNewGame() == false and has(c.logs, "^fast;step=logos;.*skipped=1"), "the logos take the game's new-game branch")
-- Title.
c.env.title_sequence = {StartPressStartMenu = function() c.calls[#c.calls + 1] = "press_ui" end}
c.mission(40010, true)
check(c.env.title_sequence.StartPressStartMenu ~= nil, "title wrapped")
c.seq = "Seq_Demo_StartHasTitleMission"; c.frame()
c.env.title_sequence.StartPressStartMenu()
check(c.env.mvars.title_hideTitleMenuOnInitialize == true and has(c.calls, "^next:Seq_Game_PushStart$") and not has(c.calls, "^press_ui$"),
 "no PRESS START: the title moves on as its PressStart message does")
c.seq = "Seq_Game_PushStart"; c.popup = true; c.frame()
check(has(c.calls, "^erase$"), "the title's offline notice is closed")
local selects = {}
c.env.mvars.seq_sequenceTable = {Seq_Game_TitleMenu = {OnSelectContinue = function() selects[#selects + 1] = "continue" end,
 OnSelectRestartHeli = function() selects[#selects + 1] = "heli" end}}
c.seq = "Seq_Game_TitleMenu"; c.frame(); c.frame(1000)
check(#selects == 1 and selects[1] == "continue", "Continue once")
c.frame(2100)
check(#selects == 2 and has(c.logs, "^fast;step=continue;.*try=2$"), "and again 3 s later while the menu stays")
c.env.TppMission.Load(30010, 40010, {showLoadingTips = false})
check(c.loadOptions.showLoadingTips == false and c.loadOptions.waitOnLoadingTipsEnd == false, "a boot load skips the tips wait, other options kept")
c.mission(30010)
c.inGame, c.foot = true, true; c.seq = "Seq_Game_MainGame"; c.frame()
check(has(c.logs, "^fast;step=gameplay;.*mission=30010;via=continue$") and c.B.done, "gameplay in the save's mission ends the boot")
c.env.TppMission.Load(30020, 30010, nil)
check(c.loadOptions == nil, "after the boot loads are untouched")
-- Boot target 30020 with a spot (host): RestartHeli, the ACC sortie on foot, the spot, time and weather.
c = F({1, 1, 30020, 1, -510730, 5090, 1183020, 90000, 43200, 1, 1})
c.env.title_sequence = {StartPressStartMenu = function() end}
c.mission(40010, true)
selects = {}
c.env.mvars.seq_sequenceTable = {Seq_Game_TitleMenu = {OnSelectContinue = function() selects[#selects + 1] = "continue" end,
 OnSelectRestartHeli = function() selects[#selects + 1] = "heli"; c.env.gvars.ini_isTitleMode = false end}}
c.seq = "Seq_Game_TitleMenu"; c.frame()
check(selects[1] == "heli" and has(c.logs, "^fast;step=restart_heli"), "a boot target restarts in the ACC")
c.seq = "Seq_Game_MainGame"; c.inGame = true; c.env.mvars.heli_missionStartRoute = 1234
c.frame(); c.frame(1000)
check(not has(c.calls, "^route"), "the ACC settles 2 s first")
c.frame(1500)
check(has(c.calls, "^route:30020:0:false$") and has(c.calls, "^deploy:0:nil:nil$") and has(c.logs, "^place;step=deploy;.*from=40010;target=30020;route=0"),
 "the sortie on foot: route 0, no LZ left from the ACC")
c.frame(3000)
check(count(c.calls, "^deploy") == 1, "one sortie")
c.env.vars.missionCode = 30020
local r1 = c.e.ReservePlayerLoadingPosition(0)
check(r1 == "r1" and c.initial and math.abs(c.initial[1] + 510.73) < 1e-6 and math.abs(c.initial[2] - 5.59) < 1e-6 and
 math.abs(c.initial[3] - 1183.02) < 1e-6 and c.initial[4] == 90 and has(c.logs, "^place;step=initial"), "the host starts on the spot")
c.initial = nil; c.e.ReservePlayerLoadingPosition(3)
check(not c.initial, "only the mission-finalize load")
c.mission(30020)
c.inGame, c.foot = true, true
c.env.vars.playerPosX, c.env.vars.playerPosZ = 0, 0
c.frame(); c.frame(1600)
check(has(c.calls, "^warp$") and has(c.logs, "^place;step=warp;.*try=1"), "a missed start is corrected with a warp")
c.frame(); c.frame(1600)
check(has(c.calls, "^time:12:00:00$") and has(c.calls, "^weather:0:1$") and has(c.logs, "^place;step=gameplay;.*via=spot;err_m=0.00;.*weather=sunny"),
 "on the spot, then the host's time and weather")
-- The guest with world follow: no sortie of its own, it lands 4 m in front of the host facing back.
c = F({1, 1, 30020, 1, 0, 0, 0, 0, -1, 0, 2})
c.env.e.MgsCoopWorld = {follow = {enabled = true}, active = true, level = 3}
c.mission(40010)
c.seq = "Seq_Game_MainGame"; c.inGame = true
for _ = 1, 30 do c.frame(200) end
check(not has(c.calls, "^route") and count(c.logs, "^place;step=follow_host") == 1, "the guest waits for the host's sortie")
c.env.vars.missionCode = 30020; c.e.ReservePlayerLoadingPosition(0)
check(math.abs(c.initial[1]) < 1e-6 and math.abs(c.initial[3] - 4) < 1e-6 and c.initial[4] == 180, "the guest's spot")
c.mission(30020); c.inGame, c.foot = true, true
c.env.vars.playerPosX, c.env.vars.playerPosZ = 0.5, 3.5
c.frame(); c.frame(1600)
check(not has(c.calls, "^time") and not has(c.calls, "^weather") and has(c.logs, "^place;step=gameplay;.*weather=keep"), "the guest sets no time or weather")
-- Without world follow the guest sorties itself.
c = F({1, 0, 30010, 0, 0, 0, 0, 0, -1, 0, 2})
c.mission(40010); c.seq = "Seq_Game_MainGame"; c.inGame = true
c.frame(); c.frame(2100)
check(has(c.calls, "^route:30010:0:false$"), "a guest without follow sorties itself")
c.env.vars.missionCode = 30010; c.e.ReservePlayerLoadingPosition(0)
check(math.abs(c.initial[1] - 1448.61) < 1e-6 and math.abs(c.initial[3] - 1470.4) < 1e-6, "the default 30010 spot (guest 4 m ahead)")
-- Mother Base: the arrival demo is cleared; the ACC targets.
c = F({1, 1, 30050, 0, 0, 0, 0, 0, -1, 0, 1})
c.mbDemo = "SomeDemo"
c.mission(30050)
check(has(c.calls, "^mbdemo:nil$") and has(c.logs, "^place;step=mb_demo;.*cleared=SomeDemo"), "the MB arrival demo is cleared")
c = F({1, 1, 40050, 0, 0, 0, 0, 0, -1, 0, 1})
c.mission(40050); c.seq = "Seq_Game_MainGame"; c.inGame = true
c.frame(); c.frame(2100)
check(c.B.done and has(c.logs, "^place;step=gameplay;.*mission=40050;via=acc"), "the ACC target is reached by RestartHeli")
c = F({1, 1, 40050, 0, 0, 0, 0, 0, -1, 0, 1})
c.mission(40010); c.seq = "Seq_Game_MainGame"; c.inGame = true
c.frame(); c.frame(2100)
check(c.B.done and has(c.logs, "^place;step=refused;.*reason=acc_differs") and not has(c.calls, "^route"), "another ACC is refused, never flown")
-- A Continue into a helicopter save ends a fast boot in the ACC.
c = F({1, 1, 0, 0, 0, 0, 0, 0, -1, 0, 1})
c.mission(40050); c.seq = "Seq_Game_MainGame"; c.inGame = true
c.frame(); c.frame(2100)
check(c.B.done and has(c.logs, "^fast;step=gameplay;.*via=acc"), "fast boot into the ACC")
-- Log payloads keep to the receipt alphabet and length.
for _, v in ipairs(c.logs) do check(#v <= 160 and not v:find("[^%w_=:,.;%-]"), "log payload bounded and sanitized") end
print("debug_boot_lua_check: " .. checks .. " checks PASS")
