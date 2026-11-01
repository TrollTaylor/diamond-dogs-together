-- debug_character.lua (F9 character switch) against the real character.lua planner.
-- usage: debug_character_lua_check.lua debug_character.lua character.lua
local function slurp(path)local f=assert(io.open(assert(path),"rb"));local s=f:read("*a");f:close();return s end
local src,charSrc=slurp(arg[1]),slurp(arg[2])
local checks=0
local function check(v,why)checks=checks+1;if not v then error(checks..": "..why,2)end end
check(#src:gsub("\r\n","\n")<=16000 and #src:gsub("\r?\n","\r\n")<=16000,"embedded fragment fits both checkout modes")
local PT={SNAKE=0,DD_MALE=1,DD_FEMALE=2,AVATAR=3,OCELOT=5,QUIET=6}
local PARTS={NORMAL=0,NORMAL_SCARF=1,SNEAKING_SUIT=2,OCELOT=26,QUIET=27}
local CAMO={OLIVEDRAB=0,TIGERSTRIPE=1,SPLITTER=3,OCELOT=115,QUIET=116}
local HAND_NORMAL,HAND_ROCKET=1,9
-- o.configured: coop.ini character; o.bypass: what BYPASS_ON answers; o.partsActive.
local function F(o)
 o=o or {}
 local c={receipts={},reports={},notes={},calls={},o=o}
 local env=setmetatable({},{__index=_G});env._G=env
 env.vars={missionCode=30050,playerType=o.playerType or PT.SNAKE,playerPartsType=o.parts or PARTS.SNEAKING_SUIT,
  playerCamoType=o.camo or CAMO.SPLITTER,handEquip=HAND_ROCKET,playerFaceId=77,playerFaceEquipId=4,
  sortiePrepPlayerType=o.sortieType or PT.SNAKE,sortiePrepPlayerFaceId=o.sortieFace or 77,
  sortiePrepPlayerSnakePartsType=PARTS.SNEAKING_SUIT,sortiePrepPlayerSnakeCamoType=CAMO.SPLITTER,sortiePrepPlayerSnakeFaceEquipId=4,
  sortiePrepPlayerDDPartsType=PARTS.NORMAL,sortiePrepPlayerDDCamoType=CAMO.TIGERSTRIPE,sortiePrepPlayerDDFaceEquipId=0}
 env.gvars={ply_isUsingTempPlayerType=o.usingTemp==true,ply_lastPlayerTypeUsingTemp=o.lastType,ini_isTitleMode=false}
 env.mvars={}
 env.PlayerType,env.PlayerPartsType,env.PlayerCamoType=PT,PARTS,CAMO
 env.TppEquip={EQP_HAND_NORMAL=HAND_NORMAL}
 env.TppDefine={STORY_SEQUENCE={CLEARD_ESCAPE_THE_HOSPITAL=5}}
 env.TppStory={GetCurrentStorySequence=function()return 20 end}
 env.TppMission={IsSubsistenceMission=function()return false end,IsMissionStart=function()return true end}
 env.BuddyType={QUIET=3};env.BuddyCommonFlag={BUDDY_QUIET_LOST=41}
 env.TppBuddyService={DidObtainBuddyType=function()return o.quietOnTeam==true end,CheckBuddyCommonFlag=function()return false end,
  IsDeadBuddyType=function()return false end,CanSortieBuddyType=function()return o.quietOnTeam==true end}
 env.TppQuest={IsCleard=function()return false end}
 env.PlayerStatus={PARTS_ACTIVE=o.noPartsStatus and nil or 77}
 env.PlayerInfo={OrCheckStatus=function(s)check(s[1]==77,"only PARTS_ACTIVE is polled");return c.partsActive==true end}
 -- The vanilla save/restore (stock TppPlayer.lua 1.0.15.4).
 env.TppPlayer={SaveCurrentPlayerType=function()
  c.calls[#c.calls+1]="save"
  if not env.gvars.ply_isUsingTempPlayerType then
   env.gvars.ply_lastPlayerTypeUsingTemp,env.gvars.ply_lastPlayerPartsTypeUsingTemp=env.vars.playerType,env.vars.playerPartsType
   env.gvars.ply_lastPlayerFaceIdUsingTemp=env.vars.playerFaceId
  end
  env.gvars.ply_isUsingTempPlayerType=true
 end,RestoreTemporaryPlayerType=function()
  c.calls[#c.calls+1]="restore"
  if env.gvars.ply_isUsingTempPlayerType then
   env.vars.playerType,env.vars.playerPartsType=env.gvars.ply_lastPlayerTypeUsingTemp,env.gvars.ply_lastPlayerPartsTypeUsingTemp
   env.gvars.ply_isUsingTempPlayerType=false
  end
 end,RegisterTemporaryPlayerType=function()error("the switch never registers a mission temporary type")end}
 env.loadstring=function(text)
  local q=text:match("^return '([%w_]+)'")
  if q=="MGS_COOP_CHARACTER_V1" then return function()return c.loading and "" or o.configured or "","dd_male",false end end
  if q=="MGS_COOP_CHARACTER_BYPASS_ON_V1" then c.receipts[#c.receipts+1]="on";return function()return o.bypass==true end end
  if q=="MGS_COOP_CHARACTER_BYPASS_OFF_V1" then return function()return true end end
  if q=="MGS_COOP_CHARACTER_REPORT_V1" then c.reports[#c.reports+1]=text:match(" %-%- (.*)$");return function()end end
  return loadstring(text)
 end
 env.Tpp={MgsCoop={}}
 -- The real character chunk defines Tpp.MgsCoop.Character (coop.ini default: a skip).
 local n={sequence={}};env.n=n
 c.loading=true
 local chunk=assert(loadstring(charSrc,"=character.lua"));setfenv(chunk,env);chunk()
 c.loading=false;c.reports={}
 check(type(env.Tpp.MgsCoop.Character)=="table","character chunk defined its planner")
 local D={mission=30050}
 D.api={get=function(t,k)if t==nil then return nil end;return t[k]end,call=function(t,k,...)return assert(t[k],k)(...)end,
  receipt=function(text)local f=env.loadstring(text);if type(f)=="function"then return f()end end,
  note=function(id,ok,msg)c.notes[#c.notes+1]={id=id,ok=ok,msg=msg}end}
 D.player={handles={heal=true},run=function(a)return true,"player:"..a[1]end}
 env.e={MgsCoopDebug=D}
 setfenv(assert(loadstring(src,"=debug_character.lua")),env)()
 c.env,c.D,c.S,c.M=env,D,D.character,env.Tpp.MgsCoop
 function c:run(name)return self.D.player.run({"character",name},11)end
 function c:poll(frames)for _=1,frames do local q=self.D.pending;if not q then return end;q.frames=q.frames+1;q.poll()end end
 return c
end
local function hasReport(c,text)for _,r in ipairs(c.reports)do if r:find(text,1,true)then return true end end;return false end

-- Dispatch rides on D.player without touching debug_menu.lua.
local c=F()
check(c.D.player.handles.character==true and c.D.player.handles.heal==true,"character handled through D.player")
local ok,msg=c.D.player.run({"heal"},3)
check(ok and msg=="player:heal","other player commands still reach debug_player")

-- Pure resolution.
local R=c.S.Resolve
check(R("snake","snake")=="snake" and R("dd_female","")=="dd_female" and R("avatar","quiet")=="avatar","plain characters for anyone")
check(R("default","snake")=="snake" and R("default","dd_male")=="dd_male" and R("default","story")=="story" and
 R("default","")=="own" and R("default","default")=="own","default goes back to coop.ini, else the own type")
local t,why=R("ocelot","snake");check(t==nil and why=="character_needs_launcher_pick","host Snake cannot take Ocelot")
t,why=R("quiet","dd_male");check(t==nil and why=="character_needs_launcher_pick","no Quiet without a bypass-capable pick")
check(R("quiet","story")=="quiet" and R("ocelot","quiet")=="ocelot","Ocelot/Quiet where the bypass is allowed")
t,why=R("liquid","snake");check(t==nil and why=="character_args","unknown names refused")
ok,msg=c.S.run({"character"},1);check(not ok and msg=="character_args","one argument")

-- Host (coop.ini snake) becomes a female DD soldier in place, then the poll waits for the parts.
c=F({configured="snake"})
ok,msg=c:run("dd_female")
check(ok and msg=="character_switched;dd_female","switched in place")
local v,g=c.env.vars,c.env.gvars
check(v.playerType==PT.DD_FEMALE and v.playerPartsType==PARTS.NORMAL and v.playerCamoType==CAMO.TIGERSTRIPE and
 v.playerFaceId==350 and v.playerFaceEquipId==0,"DD female with the sortie DD outfit and the default female face")
check(g.ply_isUsingTempPlayerType==true and g.ply_lastPlayerTypeUsingTemp==PT.SNAKE and g.ply_lastPlayerFaceIdUsingTemp==77,
 "own Snake kept in the save's temporary-type gvars")
check(c.M.characterPick=="dd_female","the pick holds for later loads")
check(c.M.characterStatus.expect==PT.DD_FEMALE,"the lab readback now expects the new type (its change line shows raw0)")
check(c.D.pending and c.D.pending.kind=="character" and c.D.pending.id==11,"waits for the parts reload")
check(hasReport(c,"switch;mission=30050;id=11;pick=dd_female;result=switched;type=2;parts=0;camo=1;face=350"),"switch reported")
c.partsActive=true;c:poll(29);check(c.D.pending~=nil and #c.notes==0,"no final result before 30 frames")
c:poll(1);check(c.D.pending==nil and c.notes[1].ok and c.notes[1].msg=="character_ready;type=2;parts=active","ready after PARTS_ACTIVE")
-- Parts never come back: timeout.
c=F({configured="snake"});c:run("dd_male");c:poll(900)
check(c.notes[1] and not c.notes[1].ok and c.notes[1].msg=="character_parts_timeout","parts timeout reported")
-- Something writes the type back: reverted.
c=F({configured="snake"});c:run("avatar");c.env.vars.playerType=PT.SNAKE;c:poll(1)
check(c.notes[1] and not c.notes[1].ok and c.notes[1].msg=="character_reverted;type=0","revert reported")
-- A matching sortie staff member keeps their face.
c=F({configured="snake",sortieType=PT.DD_MALE,sortieFace=192});c:run("dd_male")
check(c.env.vars.playerType==PT.DD_MALE and c.env.vars.playerFaceId==192,"sortie DD face reused")
-- Already that character: nothing written, pick stored.
c=F({configured="snake"});ok,msg=c:run("snake")
check(ok and msg=="character_kept;snake" and #c.calls==0 and c.D.pending==nil and c.M.characterPick=="snake","already Snake: kept")

-- Ocelot/Quiet: refused for a host Snake, nothing stored or written.
c=F({configured="snake"});ok,msg=c:run("ocelot")
check(not ok and msg=="character_needs_launcher_pick" and c.M.characterPick==nil and #c.receipts==0 and c.env.vars.playerType==PT.SNAKE,
 "host cannot be Ocelot: refused cleanly")
-- A Quiet joiner whose bypass is off this mission: applied at the next load.
c=F({configured="quiet",playerType=PT.DD_MALE,parts=PARTS.NORMAL});ok,msg=c:run("quiet")
check(ok and msg=="character_next_load;quiet" and c.M.characterPick=="quiet" and c.env.vars.playerType==PT.DD_MALE and #c.receipts==1,
 "bypass refused mid-mission: next load")
-- Bypass already on: Quiet in place with her parts and the normal hand.
c=F({configured="quiet",bypass=true,playerType=PT.DD_MALE,parts=PARTS.NORMAL});ok,msg=c:run("quiet")
check(ok and c.env.vars.playerType==PT.QUIET and c.env.vars.playerPartsType==PARTS.QUIET and c.env.vars.playerCamoType==CAMO.QUIET and
 c.env.vars.handEquip==HAND_NORMAL,"Quiet in place while the bypass is on")
-- default with coop.ini story resolves Quiet on the team.
c=F({configured="story",bypass=true,quietOnTeam=true});ok,msg=c:run("default")
check(ok and msg=="character_switched;quiet" and c.M.characterPick==nil,"default resolves story")

-- Back to my default: the host's coop.ini Snake; a guest with no coop.ini character gets the own type back.
c=F({configured="snake",playerType=PT.DD_FEMALE,parts=PARTS.NORMAL,usingTemp=true,lastType=PT.SNAKE});c.M.characterPick="dd_female"
ok,msg=c:run("default")
check(ok and msg=="character_switched;snake" and c.env.vars.playerType==PT.SNAKE and c.M.characterPick==nil,"host default is Snake")
c=F({configured="",playerType=PT.DD_FEMALE,usingTemp=true,lastType=PT.AVATAR});c.env.gvars.ply_lastPlayerPartsTypeUsingTemp=PARTS.SNEAKING_SUIT
ok,msg=c:run("default")
check(ok and msg=="character_switched;own" and c.env.vars.playerType==PT.AVATAR and c.env.gvars.ply_isUsingTempPlayerType==false and
 c.calls[1]=="restore","no coop.ini character: the save's own type restored")
c=F({configured=""});ok,msg=c:run("default")
check(ok and msg=="character_kept;own" and #c.calls==0,"own type already: kept")

-- Quiet rules still win in place (low note char-1): Quiet as the buddy or a Quiet-conflict mission refuses, nothing stored.
c=F({configured="quiet",bypass=true,playerType=PT.DD_MALE,parts=PARTS.NORMAL});c.env.vars.buddyType=3;ok,msg=c:run("quiet")
check(not ok and msg=="character_quiet_unavailable;reason=buddy_quiet" and c.M.characterPick==nil and c.env.vars.playerType==PT.DD_MALE and
 #c.calls==0,"Quiet refused while Quiet is the buddy")
c=F({configured="quiet",bypass=true,playerType=PT.DD_MALE,parts=PARTS.NORMAL});c.env.vars.missionCode=10050;ok,msg=c:run("quiet")
check(not ok and msg=="character_quiet_unavailable;reason=quiet_conflict" and c.M.characterPick==nil,"Quiet refused in a Quiet-conflict mission")
-- Back to my default never restores a saved Ocelot/Quiet mid-mission while the bypass is off (low note char-3).
c=F({configured="",playerType=PT.SNAKE,usingTemp=true,lastType=PT.OCELOT});c.M.characterPick="snake";ok,msg=c:run("default")
check(ok and msg=="character_next_load;own" and #c.calls==0 and c.env.vars.playerType==PT.SNAKE and c.M.characterPick==nil and
 c.env.gvars.ply_isUsingTempPlayerType==true,"saved Ocelot not restored with the bypass off: next load")
c=F({configured="",bypass=true,playerType=PT.SNAKE,usingTemp=true,lastType=PT.OCELOT});ok,msg=c:run("default")
check(ok and msg=="character_switched;own" and c.calls[1]=="restore","saved Ocelot restored while the bypass is on")
-- A binding that throws leaves no pick behind (low note char-5).
c=F({configured="snake"});c.env.TppPlayer.SaveCurrentPlayerType=nil
ok=pcall(c.run,c,"dd_male");check(not ok and c.M.characterPick==nil,"a failed switch stores no pick")
-- No planner yet (before the first mission load): refused.
c=F();c.M.Character=nil;ok,msg=c:run("snake")
check(not ok and msg=="character_unavailable","needs the character chunk")
print("debug_character_lua_check: "..checks.." checks PASS")
