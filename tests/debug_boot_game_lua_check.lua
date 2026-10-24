-- The developer test boot (lua/coop/debug_boot.lua) against the game's own
-- init_sequence.lua and title_sequence.lua (extracted read-only, pinned by
-- SHA-256 in cmake/lua_checks.cmake; docs/test-boot.md): the new-game branch
-- deletes the three logo splash screens without the logo sequence, the wrapped
-- StartPressStartMenu leaves the PRESS START UI out and moves on to
-- Seq_Game_PushStart, the hidden menu stays hidden, and the menu's Continue /
-- RestartHeli selects reach the chunk check and the ACC game sequence.
-- arg[1] debug_boot.lua, arg[2] init_sequence.lua, arg[3] title_sequence.lua.
local checks = 0
local function check(v, why) checks = checks + 1; if not v then error(checks .. ": " .. why, 2) end end
local function slurp(p) local f = assert(io.open(p, "rb")); local s = f:read("*a"); f:close(); return s end
local function run()
 local r = {calls = {}, logs = {}}
 local function record(name) r.calls[#r.calls + 1] = name end
 -- Auto stubs: any unknown table field is a recording callable table.
 local function stub(name)
  return setmetatable({}, {__index = function(t, k)
   if type(k) ~= "string" then return nil end
   local child = stub(name .. "." .. k); rawset(t, k, child); return child
  end, __call = function() record(name) end})
 end
 local env = {}
 env.vars = {missionCode = 40010}
 env.gvars = {ini_isTitleMode = true, ini_isReturnToTitle = false, title_nextMissionCode = 30010, str_storySequence = 100}
 env.mvars = {}
 env.TppDefine = setmetatable({Enum = function(t) local e = {}; for i, v in ipairs(t) do e[v] = i - 1 end; return e end,
  MISSION_LOAD_TYPE = {MISSION_FINALIZE = 0}, STORY_SEQUENCE = {CLEARD_ESCAPE_THE_HOSPITAL = 2}, SAVE_SLOT = {SAVING = 1, GLOBAL = 2},
  SAVE_FILE_INFO = setmetatable({}, {__index = function() return {slot = 3} end})}, {__index = function(t, k) local v = stub("TppDefine." .. k); rawset(t, k, v); return v end})
 env.Fox = {StrCode32 = function(s) return #s end, Log = function() end, Error = function() end, Warning = function() end,
  GetDebugLevel = function() return 0 end, DEBUG_LEVEL_QA_RELEASE = 1, GetPlatformName = function() return "Windows" end}
 env.Tpp = setmetatable({StrCode32Table = function(t) return t end, IsMaster = function() return true end}, {__index = function(t, k) local v = stub("Tpp." .. k); rawset(t, k, v); return v end})
 env.TppSequence = {SetNextSequence = function(s) record("next:" .. s) end, GetCurrentSequenceName = function() return r.seq or "" end,
  RegisterSequences = function() end, RegisterSequenceTable = function() end}
 r.splash = {konamiLogo = 11, kjpLogo = 12, foxLogo = 13}
 env.SplashScreen = {GetSplashScreenWithName = function(n) return r.splash[n] end, Delete = function(id) record("delete:" .. tostring(id)) end,
  Create = function() return 1 end, Show = function(id) record("show:" .. tostring(id)) end, SetStateCallback = function() end}
 env.TppUiCommand = setmetatable({StartPressStartMenu = function() record("ui:press_start") end, StartTitleMenu = function() record("ui:title_menu") end,
  StartPreTitleSequence = function() record("ui:pre_title") end, ErasePopup = function() record("ui:erase") end, IsShowPopup = function() return false end},
  {__index = function(t, k) local v = stub("TppUiCommand." .. k); rawset(t, k, v); return v end})
 env.TppSave = setmetatable({IsNewGame = function() return false end, IsGameDataLoadResultOK = function() return true end},
  {__index = function(t, k) local v = stub("TppSave." .. k); rawset(t, k, v); return v end})
 env.TppMission = setmetatable({IsHelicopterSpace = function(m) return math.floor(m / 10000) == 4 end},
  {__index = function(t, k) local v = stub("TppMission." .. k); rawset(t, k, v); return v end})
 env.loadstring = function(source)
  if source == "return 'MGS_COOP_BOOT_CFG_V1'" then return function() if r.reply then return unpack(r.reply) end return 0 end end
  local log = source:match("^return 'MGS_COOP_BOOT_LOG_V1' %-%- (.*)$")
  if log then return function() r.logs[#r.logs + 1] = log; return true end end
  return loadstring(source)
 end
 setmetatable(env, {__index = function(t, k)
  local v = _G[k]
  if v ~= nil then return v end
  v = stub(k); rawset(t, k, v); return v
 end})
 local function module(path)
  local chunk = assert(loadstring(slurp(path), "@" .. path))
  setfenv(chunk, env)
  return chunk()
 end
 r.init = module(arg[2])
 r.title = module(arg[3])
 env.title_sequence = r.title
 local e = {OnUpdate = function() end, OnAllocate = function() end, ReservePlayerLoadingPosition = function() end}
 env.e = e
 local boot = assert(loadstring(slurp(arg[1]), "@" .. arg[1]))
 setfenv(boot, env)
 boot()
 r.env, r.B = env, e.MgsCoopBoot
 return r
end
local function has(list, s) for _, v in ipairs(list) do if v == s then return true end end return false end
-- The game's own logo branch (not a new game) without the boot.
local r = run()
r.init.StartPreTitleSequence()
check(has(r.calls, "next:Seq_Demo_ShowKonamiAndFoxLogo") and not has(r.calls, "ui:pre_title"), "a saved game shows the logo sequence")
-- Fast boot: the new-game branch.
r = run()
r.reply = {1, 1, 0, 0, 0, 0, 0, 0, -1, 0, 1}
r.B.wrapInit(r.init)
r.init.StartPreTitleSequence()
check(has(r.calls, "delete:11") and has(r.calls, "delete:12") and has(r.calls, "delete:13") and has(r.calls, "ui:pre_title") and
 not has(r.calls, "next:Seq_Demo_ShowKonamiAndFoxLogo") and r.env.TppSave.IsNewGame() == false, "fast boot: no logos, pre-title at once, IsNewGame restored")
-- The title: the wrapped press start, the hidden menu, the selects.
local seqs = r.title.AddTitleSequenceTable({})
for _, name in ipairs({"Seq_Demo_StartHasTitleMission", "Seq_Game_PushStart", "Seq_Game_TitleMenu", "Seq_Demo_CheckDlc", "Seq_Game_ChunkLoading", "Seq_Game_ChunkInstalled"}) do
 check(type(seqs[name]) == "table", "title sequence " .. name)
end
check(type(seqs.Seq_Game_TitleMenu.OnSelectContinue) == "function" and type(seqs.Seq_Game_TitleMenu.OnSelectRestartHeli) == "function",
 "the menu's Continue and RestartHeli selects")
r.B.wrapTitle(r.title)
r.calls = {}
seqs.Seq_Demo_StartHasTitleMission.OnEndShowSplashScreen()
check(has(r.calls, "next:Seq_Game_PushStart") and not has(r.calls, "ui:press_start") and r.env.mvars.title_hideTitleMenuOnInitialize == true,
 "OnEndShowSplashScreen: no PRESS START, on to Seq_Game_PushStart")
r.calls = {}
seqs.Seq_Game_PushStart.OnUpdate(seqs.Seq_Game_PushStart)
check(has(r.calls, "next:Seq_Game_TitleMenu"), "PushStart moves on to the menu without a popup")
r.calls = {}
seqs.Seq_Game_TitleMenu.OnEnter()
check(not has(r.calls, "ui:title_menu"), "the hidden menu stays hidden")
seqs.Seq_Game_TitleMenu.OnSelectContinue()
check(has(r.calls, "next:Seq_Demo_CheckDlc") and r.title.GetOnSelectFlag() == 0, "Continue selects SELECT_CONTINUE")
seqs.Seq_Game_TitleMenu.OnSelectRestartHeli()
check(r.title.GetOnSelectFlag() == 1, "RestartHeli selects SELECT_RESTART_HELI")
r.env.mvars.title_missionGameSequenceName = "Seq_Game_MainGame"
r.calls = {}
seqs.Seq_Game_ChunkInstalled.OnEnter(seqs.Seq_Game_ChunkInstalled)
check(has(r.calls, "next:Seq_Game_MainGame") and r.env.gvars.ini_isTitleMode == false, "RestartHeli leaves title mode into the ACC game sequence")
-- Not a boot: the wrapper hands the press start back to the game.
r = run()
r.B.wrapTitle(r.title)
r.title.StartPressStartMenu()
check(has(r.calls, "ui:press_start"), "without the loader's answer the PRESS START UI shows")
print("debug_boot_game_lua_check: " .. checks .. " checks PASS")
