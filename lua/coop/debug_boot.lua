-- The developer test boot (docs/test-boot.md). lab_fast_boot: no logo splash
-- screens, no copyright wait, the offline notice closed, no press-start and no
-- title menu (Continue is taken here). lab_boot: after the title this copy lands
-- in a mission at a spot, with the time and weather set. Inert unless the
-- loader's boot receipt answers, which it does only for the owned private
-- worker of a background-worker build in lab mode with copied saves.
-- Every step is the game's own code: init_sequence.StartPreTitleSequence's
-- new-game branch (it deletes the logo splash screens), the title's PressStart
-- transition and its menu's OnSelectContinue / OnSelectRestartHeli (the ACC
-- without a load), the lab goto's ACC sortie (SelectNextMissionHeliStartRoute +
-- OnEndMissionPreparation) on foot (route 0) with TppPlayer.SetInitialPosition
-- after the mission's own ReservePlayerLoadingPosition, TppMission.Load's
-- waitOnLoadingTipsEnd=false and TppDemo.SetNextMBDemo() for Mother Base.
-- While world follow runs the guest never sorties: it enters the ACC and
-- replays the host's sortie (world_follow kind 6), landing 4 m in front of it.
do
 local update, allocate, reserve = e.OnUpdate, e.OnAllocate, e.ReservePlayerLoadingPosition
 local B = {}
 e.MgsCoopBoot = B
 local fmt, floor, sqrt, sin, cos, rad = string.format, math.floor, math.sqrt, math.sin, math.cos, math.rad
 local function get(t, k)
  if t == nil then return nil end
  local ok, v = pcall(function() return t[k] end)
  if ok then return v end
 end
 local function call(t, k, ...)
  local f = get(t, k)
  if type(f) ~= "function" then error("missing_" .. k, 0) end
  return f(...)
 end
 local function try(t, k, ...)
  local f = get(t, k)
  if type(f) ~= "function" then return false end
  return pcall(f, ...)
 end
 local function receipt(text)
  local f = loadstring(text)
  if type(f) == "function" then return f() end
 end
 function B.ms()
  local ok, v = try(Time, "GetRawElapsedTimeSinceStartUp")
  return ok and type(v) == "number" and floor(v * 1000) or -1
 end
 -- "lab_boot=<kind>; step=<step>; ms=<game clock ms>[; ...]" in script-observer.log.
 function B.log(kind, step, extra)
  local text = fmt("%s;step=%s;ms=%d", kind, step, B.ms()) .. (extra and (";" .. extra) or "")
  receipt("return 'MGS_COOP_BOOT_LOG_V1' -- " .. text:gsub("[^%w_=:,.;%-]", "_"):sub(1, 160))
 end
 local log = B.log
 local WEATHER = {"SUNNY", "CLOUDY", "RAINY", "SANDSTORM", "FOGGY"}
 -- Without a lab_boot position: the game's own free-roam fail-safe starts
 -- (TppPlayer.FailSafeInitialPositionForFreePlay) and the command-platform deck
 -- where Mother Base continues start (scenarios/partner-traverse-ladder-mb.txt).
 local SPOT = {[30010] = {1448.61, 337.787, 1466.4, 0}, [30020] = {-510.73, 5.09, 1183.02, 0}, [30050] = {9.0, 0.8, -41.1, 0}}
 -- Asked again at each allocation and update until the loader answers.
 function B.config()
  if B.cfg then return B.cfg end
  local r = {receipt("return 'MGS_COOP_BOOT_CFG_V1'")}
  if r[1] ~= 1 or type(r[11]) ~= "number" then return nil end
  local c = {fast = r[2] == 1, host = r[11] == 1, clock = r[9] >= 0 and r[9] or nil, weather = WEATHER[r[10]]}
  if r[3] ~= 0 then c.target = r[3] end
  if r[4] == 1 then c.pos = {r[5] / 1000, r[6] / 1000, r[7] / 1000, r[8] / 1000} elseif c.target then c.pos = SPOT[c.target] end
  B.cfg = c
  log(c.target and "place" or "fast", "config", fmt("fast=%d;target=%d;host=%d;pos=%d", c.fast and 1 or 0, c.target or 0,
   c.host and 1 or 0, r[4] == 1 and 1 or 0))
  return c
 end
 -- This copy's spot: the host on the named spot, the guest 4 m in front of it facing back.
 function B.spot(c)
  local p = c.pos
  if not p then return nil end
  if c.host then return {p[1], p[2] + 0.5, p[3], p[4]} end
  local y = rad(p[4])
  return {p[1] + 4 * sin(y), p[2] + 0.5, p[3] + 4 * cos(y), (p[4] + 180) % 360}
 end
 local function title() return get(gvars, "ini_isTitleMode") == true and floor((get(vars, "missionCode") or 0) / 10000) == 4 end
 local function seqName()
  local ok, v = try(TppSequence, "GetCurrentSequenceName")
  return ok and type(v) == "string" and v or ""
 end
 local function yes(t, k, ...)
  local ok, v = try(t, k, ...)
  return ok and v == true
 end
 local function boot(c) return c and (c.fast or c.target) and not B.done end
 -- init_sequence.StartPreTitleSequence with the new-game decision: its own
 -- branch deletes the three logo splash screens and starts the pre-title at once.
 local WRAPPED = setmetatable({}, {__mode = "k"})
 function B.wrapInit(s)
  local start = get(s, "StartPreTitleSequence")
  if type(start) ~= "function" or WRAPPED[start] then return end
  local f = function(...)
   local c = B.config()
   if not (c and c.fast) or type(get(TppSave, "IsNewGame")) ~= "function" then return start(...) end
   local real = TppSave.IsNewGame
   TppSave.IsNewGame = function() return true end
   local ok, err = pcall(start, ...)
   TppSave.IsNewGame = real
   log("fast", "logos", ok and "skipped=1" or "error=" .. tostring(err):sub(1, 60))
   if not ok then error(err, 0) end
  end
  WRAPPED[f] = true
  s.StartPreTitleSequence = f
 end
 -- title_sequence.StartPressStartMenu (Seq_Demo_StartHasTitleMission, after the
 -- logos and the MB sync): no PRESS START; the menu stays hidden and the title
 -- goes on as its PressStart message does.
 function B.wrapTitle(t)
  local press = get(t, "StartPressStartMenu")
  if type(press) ~= "function" or WRAPPED[press] then return end
  local f = function(...)
   if not (boot(B.config()) and title()) then return press(...) end
   mvars.title_hideTitleMenuOnInitialize = true
   call(TppSequence, "SetNextSequence", "Seq_Game_PushStart")
   log("fast", "press_start", "mission=" .. tostring(get(vars, "missionCode")))
  end
  WRAPPED[f] = true
  t.StartPressStartMenu = f
 end
 function B.allocate(n)
  B.settle, B.select, B.wait, B.erased = nil, nil, nil, nil
  local m = get(vars, "missionCode")
  -- The init mission's sequence module, for a wrap as soon as the loader answers.
  if m == 1 then B.initSeq = get(n, "sequence") end
  local c = B.config()
  if not boot(c) then return end
  if m == 1 and c.fast then pcall(B.wrapInit, B.initSeq or init_sequence) end
  if type(title_sequence) == "table" then pcall(B.wrapTitle, title_sequence) end
  -- Mother Base: no arrival demo (f30050 MissionPrepare reads the queued one).
  if m == 30050 then
   local ok, name = try(TppDemo, "GetMBDemoName")
   try(TppDemo, "SetNextMBDemo")
   log("place", "mb_demo", "cleared=" .. tostring(ok and name or "none"))
  end
 end
 -- The boot target's on-foot start, after the mission's own decision (route 0: ON_FOOT).
 function B.reserve(kind)
  local c = B.config()
  if not (c and c.target and c.pos and not B.done) or kind ~= get(get(TppDefine, "MISSION_LOAD_TYPE"), "MISSION_FINALIZE") or
   get(vars, "missionCode") ~= c.target then return end
  local p = B.spot(c)
  call(TppPlayer, "SetInitialPosition", {p[1], p[2], p[3]}, p[4])
  log("place", "initial", fmt("x=%.2f;y=%.2f;z=%.2f;yaw=%.1f", p[1], p[2], p[3], p[4]))
 end
 -- The boot's sortie lands on foot: no LZ route left over from the ACC.
 local deploy = get(TppMission, "OnEndMissionPreparation")
 if type(deploy) == "function" then
  TppMission.OnEndMissionPreparation = function(...)
   local c = B.config()
   if c and c.target and not B.done and get(gvars, "mis_nextMissionCodeForMissionClear") == c.target and
    get(gvars, "heli_missionStartRoute") == 0 and type(mvars) == "table" then mvars.heli_missionStartRoute = nil end
   return deploy(...)
  end
 end
 -- Boot loads skip the loading-tips key wait (the game's own Load option).
 local load = get(TppMission, "Load")
 if type(load) == "function" then
  TppMission.Load = function(nextMission, current, options)
   if not boot(B.config()) then return load(nextMission, current, options) end
   local o = {}
   if type(options) == "table" then for k, v in pairs(options) do o[k] = v end end
   o.waitOnLoadingTipsEnd = false
   log("fast", "load", "next=" .. tostring(nextMission))
   return load(nextMission, current, o)
  end
 end
 local function pose()
  local p = {get(vars, "playerPosX"), get(vars, "playerPosY"), get(vars, "playerPosZ"), get(vars, "playerRotY")}
  for i = 1, 4 do if type(p[i]) ~= "number" or p[i] ~= p[i] then return nil end end
  return p
 end
 local function onFoot()
  local ok, demo = try(DemoDaemon, "IsDemoPlaying")
  if not ok or demo ~= false or not yes(Player, "IsInstanceValid") then return false end
  return yes(PlayerInfo, "OrCheckStatus", {get(PlayerStatus, "STAND"), get(PlayerStatus, "SQUAT"), get(PlayerStatus, "CRAWL")})
 end
 -- Init (mission 1): the copyright splash and the popups the sequence only waits on.
 function B.init(c, seq, now)
  if not c.fast then return end
  pcall(B.wrapInit, B.initSeq or init_sequence)
  local ok, cesa = try(SplashScreen, "GetSplashScreenWithName", "cesa")
  if ok and cesa then try(SplashScreen, "Delete", cesa); log("fast", "copyright") end
  if (seq == "Seq_Demo_ConfirmAutoSave" or seq == "Seq_Demo_LogInKonamiServer" or seq == "Seq_Demo_ShowDlcErrorNetwork") and yes(TppUiCommand, "IsShowPopup") then
   try(TppUiCommand, "ErasePopup")
   if not B.erased then B.erased = true; log("fast", "popup", "seq=" .. seq) end
  elseif seq == "Seq_Demo_ShowDlcAnnouncePopup" and now - B.seqAt >= 1000 and not B.erased then
   B.erased = true
   try(TppUiCommand, "ErasePopup"); try(TppSequence, "SetNextSequence", "Seq_Demo_CheckPatchDlc")
   log("fast", "dlc_announce")
  end
 end
 -- Title: the offline notice, then the menu's own Continue (or RestartHeli for a boot target), every 3 s.
 function B.title(c, seq, now)
  if type(title_sequence) == "table" then pcall(B.wrapTitle, title_sequence) end
  if seq == "Seq_Game_PushStart" and yes(TppUiCommand, "IsShowPopup") then
   try(TppUiCommand, "ErasePopup")
   if not B.erased then B.erased = true; log("fast", "popup", "seq=" .. seq) end
   return
  end
  if seq ~= "Seq_Game_TitleMenu" or (B.select and now - B.select < 3000) then return end
  local menu = get(get(mvars, "seq_sequenceTable"), "Seq_Game_TitleMenu")
  local name = c.target and "OnSelectRestartHeli" or "OnSelectContinue"
  if type(get(menu, name)) ~= "function" then return end
  B.select, B.tries = now, (B.tries or 0) + 1
  log("fast", c.target and "restart_heli" or "continue", fmt("mission=%d;try=%d", get(vars, "missionCode") or 0, B.tries))
  menu[name]()
 end
 function B.finish(c, how, extra)
  B.done = true
  log(c.target and "place" or "fast", "gameplay", fmt("mission=%d;via=%s", get(vars, "missionCode") or 0, how) .. (extra and (";" .. extra) or ""))
 end
 -- The ACC in game (RestartHeli, or a Continue into a helicopter save): the host,
 -- or a copy without world follow, sorties on foot to a free-roam target.
 function B.acc(c, m, now)
  if not yes(TppMission, "CheckMissionState") then return end
  B.settle = B.settle or now
  if now - B.settle < 2000 then return end
  if not c.target or c.target == m then return B.finish(c, "acc") end
  if floor(c.target / 10000) == 4 then
   B.done = true
   return log("place", "refused", fmt("reason=acc_differs;mission=%d;target=%d", m, c.target))
  end
  local W = e.MgsCoopWorld
  local F = get(W, "follow")
  if not c.host and get(F, "enabled") and get(W, "active") and (get(W, "level") or 0) >= 3 then
   if not B.wait then B.wait = true; log("place", "follow_host", "mission=" .. m) end
   return
  end
  if B.deployed or get(gvars, "mis_nextMissionCodeForEmergency") ~= 0 then return end
  B.deployed = true
  call(TppMission, "SelectNextMissionHeliStartRoute", c.target, 0, false)
  call(TppMission, "OnEndMissionPreparation", get(get(TppClock, "DEPLOY_TIME"), "CURRENT") or 0, c.target == 30050 and 0 or nil)
  log("place", "deploy", fmt("from=%d;target=%d;route=0", m, c.target))
 end
 -- On foot in a free roam: the spot check (a warp when the start missed it), then time and weather (host).
 function B.field(c, m, now)
  if not yes(TppMission, "CheckMissionState") or not onFoot() then return end
  if not c.target then return B.finish(c, "continue") end
  if m ~= c.target then return end
  B.settle = B.settle or now
  if now - B.settle < 1500 then return end
  local p, want = pose(), B.spot(c)
  if not p then return end
  local dx, dz = p[1] - want[1], p[3] - want[3]
  local err = sqrt(dx * dx + dz * dz)
  if err > 3 and (B.warps or 0) < 3 then
   B.warps, B.settle = (B.warps or 0) + 1, now
   call(TppPlayer, "Warp", {pos = {want[1], want[2], want[3]}, rotY = want[4]})
   return log("place", "warp", fmt("err_m=%.1f;try=%d", err, B.warps))
  end
  if c.host and c.clock then try(TppClock, "SetTime", fmt("%02d:%02d:00", floor(c.clock / 3600), floor(c.clock / 60) % 60)) end
  local w = c.host and c.weather and get(get(TppDefine, "WEATHER"), c.weather)
  if type(w) == "number" then try(TppWeather, "ForceRequestWeather", w, 1) end
  B.finish(c, "spot", fmt("err_m=%.2f;x=%.2f;y=%.2f;z=%.2f;clock=%d;weather=%s", err, p[1], p[2], p[3], get(vars, "clock") or -1,
   c.host and c.weather and c.weather:lower() or "keep"))
 end
 function B.tick()
  local c = B.config()
  if not boot(c) then return end
  local m, seq, now = get(vars, "missionCode") or 0, seqName(), B.ms()
  if seq ~= B.seq or m ~= B.mission then
   B.seq, B.mission, B.seqAt, B.erased = seq, m, now, nil
   log("trace", "seq", fmt("mission=%d;title=%d;name=%s", m, title() and 1 or 0, seq))
  end
  if m == 1 then return B.init(c, seq, now) end
  if title() then return B.title(c, seq, now) end
  if floor(m / 10000) == 4 then return B.acc(c, m, now) end
  if floor(m / 10000) == 3 then return B.field(c, m, now) end
 end
 local function updated(...)
  pcall(B.tick)
  return ...
 end
 if type(update) == "function" then e.OnUpdate = function(...) return updated(update(...)) end end
 if type(allocate) == "function" then e.OnAllocate = function(...) pcall(B.allocate, ...); return allocate(...) end end
 if type(reserve) == "function" then e.ReservePlayerLoadingPosition = function(kind, ...)
  local r = {n = 0}
  local function keep(...) r.n = select("#", ...); for i = 1, r.n do r[i] = select(i, ...) end end
  keep(reserve(kind, ...))
  local ok, err = pcall(B.reserve, kind)
  if not ok then pcall(log, "place", "error", "fn=reserve;msg=" .. tostring(err):sub(1, 60)) end
  return unpack(r, 1, r.n)
 end end
end
