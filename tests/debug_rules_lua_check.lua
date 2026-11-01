-- Game rules (lua/coop/debug_rules.lua, docs/debug-menu.md "Game rules"): Wormhole Fulton off/on (this player only,
-- nothing changes in the save) and weapons at Mother Base (the host decides for both copies; RULES 0x3E).
local path=assert(arg[1]);local f=assert(io.open(path,"rb"));local src=f:read("*a");f:close()
local checks=0
local function check(v,why)checks=checks+1;if not v then error(checks..": "..why,2)end end
check(#src:gsub("\r\n","\n")<=16000 and #src:gsub("\r?\n","\r\n")<=16000,"embedded fragment fits both checkout modes")
local WORM,BASE,SLOT=120,100,20
local MB=4+16
local function F(o)
 o=o or {}
 local c={levels={[WORM]=o.level or 1},sets={},status={},says={},tx={},logs={},saved={},host=o.host~=false,now=0,follow=o.follow~=false,baseRuns=0}
 local env=setmetatable({},{__index=_G});env._G=env
 env.vars={missionCode=o.mission or 30010,playerDisableActionFlag=o.flag or MB,itemLevels={[SLOT]=o.level or 1}}
 env.TppEquip={EQP_IT_Fulton_WormHole=WORM,EQP_IT_InstantStealth=BASE}
 env.Player={GetItemLevel=function(q)return c.levels[q]or 0 end,SetItemLevel=function(q,n)
  c.sets[#c.sets+1]={q,n};c.levels[q]=n;if c.follow then env.vars.itemLevels[q-BASE]=n end end}
 env.PlayerDisableAction={NONE=0,RUN=2,CQC_KNIFE_KILL=4,KILLING_WEAPON=16,CQC=32}
 env.TppUiStatusManager={SetStatus=function(a,b)c.status[#c.status+1]="set:"..a..":"..b end,UnsetStatus=function(a,b)c.status[#c.status+1]="unset:"..a..":"..b end}
 env.TppUiCommand={AnnounceLogView=function(t)c.says[#c.says+1]=t end}
 env.TppMission={CheckMissionState=function()return c.gameplay~=false end}
 env.TppSave={SaveVarsToSlot=function(...)c.saved[#c.saved+1]={level=env.vars.itemLevels[SLOT],flag=env.vars.playerDisableActionFlag,args=select("#",...)};return "saved",7 end}
 local W={host=c.host,mode=o.mode or 1,paired=true,epoch=5,OP={TRAVEL=59},handlers={},pairedHooks={},unpairedHooks={},log=function(s)c.logs[#c.logs+1]=s end,now=function()return c.now end}
 function W.tx(op,...)c.tx[#c.tx+1]={op,...};return c.txOk~=false end
 local P={handles={heal=true},run=function(a)c.baseRuns=c.baseRuns+1;return true,"base:"..a[1]end}
 local D={player=P}
 D.api={get=function(t,k)if t==nil then return nil end;local ok,v=pcall(function()return t[k]end);if ok then return v end end,
  call=function(t,k,...)return assert(t[k])(...)end,hostOnly=function()return c.host end}
 env.e={MgsCoopWorld=W,MgsCoopDebug=D,OnUpdate=function()c.updates=(c.updates or 0)+1;return "u" end}
 if o.noPlayer then D.player=nil end
 setfenv(assert(loadstring(src)),env)()
 c.env,c.W,c.D,c.P=env,W,D,P
 function c:run(...)return self.P.run({...},1)end
 function c:tick(n)for _=1,n or 15 do self.now=self.now+33;self.env.e.OnUpdate()end end
 function c:has(list,text)for _,v in ipairs(list)do if tostring(v):find(text,1,true)then return true end end;return false end
 return c
end
-- Loading: the fragment rides the player module's dispatch; other commands still reach it.
local c=F();check(type(c.D.rules)=="table","D.rules exists")
for _,k in ipairs({"wormhole","mb_weapons","rules","heal"})do check(c.P.handles[k]==true,"handles "..k)end
local ok,msg=c:run("heal");check(ok and msg=="base:heal" and c.baseRuns==1,"the player module's own commands still run")
check(c.env.e.OnUpdate()=="u","the update wrapper keeps the original's results")
c=F({noPlayer=true});check(c.D.rules==nil,"no player module: inert")
-- (A) Wormhole Fulton off: the level goes to 0, a reload's 1 is forced to 0 again, a save writes the real level.
c=F();ok,msg=c:run("wormhole","0")
check(ok and msg:find("wormhole=off",1,true) and msg:find("seen=1",1,true) and c.levels[WORM]==0 and c.env.vars.itemLevels[SLOT]==0,"off: the wormhole level is 0")
check(#c.says==1 and c.says[1]:find("balloon",1,true),"off: one plain sentence names the balloon")
check(c:has(c.logs,"event=rules;step=worm_off;level=1"),"off: logged")
c.levels[WORM]=1;c.env.vars.itemLevels[SLOT]=1;c:tick()
check(c.levels[WORM]==0 and c.env.vars.itemLevels[SLOT]==0,"a checkpoint reload's level is forced to 0 again")
local r={c.env.TppSave.SaveVarsToSlot("slot",2)}
check(r[1]=="saved" and r[2]==7 and c.saved[1].level==1 and c.saved[1].args==2,"a save writes the real level (the development and its use stay)")
check(c.env.vars.itemLevels[SLOT]==0 and c.levels[WORM]==0,"after the save the live level is 0 again")
c.gameplay=false;c.levels[WORM]=1;c:tick();check(c.levels[WORM]==1,"never forced outside gameplay (loading)")
c.gameplay=true;c:tick();check(c.levels[WORM]==0,"forced once gameplay is back")
ok,msg=c:run("wormhole","1")
check(ok and msg=="wormhole=on;level=1" and c.levels[WORM]==1 and c.env.vars.itemLevels[SLOT]==1,"on: the real level is back")
c:tick(60);check(c.levels[WORM]==1,"on: never forced again")
c.env.TppSave.SaveVarsToSlot("slot");check(c.saved[#c.saved].level==1 and c.env.vars.itemLevels[SLOT]==1,"on: the save is untouched")
-- r1101 fulton (world_enemy.lua W.fulton): while a partner's wormhole look is shown here (W.fulton.real set), the off
-- rule leaves the level alone and never takes it for this save's own development.
c=F({level=0});c:run("wormhole","0");c.W.fulton={real=0};c.levels[WORM]=1;c:tick()
check(c.levels[WORM]==1 and not c:has(c.logs,"step=worm_off"),"off: a partner's wormhole look is left alone")
c.W.fulton.real=nil;c:tick();check(c.levels[WORM]==0,"off: forced again once the look is restored")
-- The save fix only when vars.itemLevels follows SetItemLevel (the stock slot): else no var is written.
c=F({follow=false});c:run("wormhole","0");c.env.vars.itemLevels[SLOT]=0
c.env.TppSave.SaveVarsToSlot("slot");check(c.saved[1].level==0 and c.env.vars.itemLevels[SLOT]==0,"an unverified slot is never written")
check(c:has(c.logs,"slot=0"),"the unverified slot is logged")
-- A save without the wormhole: off and on change nothing, and say so.
c=F({level=0});ok,msg=c:run("wormhole","0")
check(ok and msg:find("seen=0",1,true) and #c.sets==0 and c.says[1]:find("no wormhole",1,true),"no wormhole in the save: off sets nothing")
ok,msg=c:run("wormhole","1");check(ok and msg=="wormhole=on;level=0" and #c.sets==0,"no wormhole in the save: on sets nothing")
c=F();check(select(2,c:run("wormhole"))=="command_args" and select(2,c:run("wormhole","2"))=="command_args" and
 select(2,c:run("mb_weapons","x"))=="command_args" and select(2,c:run("rules","1"))=="command_args","bad arguments refuse")
-- The guest's wormhole is its own: no wire op.
c=F({host=false});ok,msg=c:run("wormhole","0");check(ok and #c.tx==0 and c.levels[WORM]==0,"the guest turns its own wormhole off, nothing sent")
-- (B) Weapons at Mother Base: the host's press lifts exactly the stock rule on this copy and sends RULES.
c=F({mission=30050});ok,msg=c:run("mb_weapons","1")
check(ok and msg:find("mb_weapons=on;sent=1;applied=1;mission=30050",1,true),"host: on, sent and applied at Mother Base")
check(c.env.vars.playerDisableActionFlag==0 and c:has(c.status,"unset:EquipHudAll:ALL_KILL_NOUSE"),"the stock no-lethal flag and HUD lifted")
check(#c.tx==1 and c.tx[1][1]==62 and c.tx[1][2]==1 and c.tx[1][3]==1 and c.W.OP.RULES==62,"RULES 62: seq 1, weapons 1")
check(c.says[1]:find("both players",1,true),"the host's sentence names both players")
check(c.says[1]:find("Any kill at Mother Base still fails the mission",1,true),"f30050 fails the mission on any Dead/Dying, not only staff")
c.env.vars.playerDisableActionFlag=MB;c:tick();check(c.env.vars.playerDisableActionFlag==0,"the stock re-set (a cutscene or the Paz room) is lifted again")
c.env.vars.playerDisableActionFlag=MB+2+32;c:tick();check(c.env.vars.playerDisableActionFlag==MB+2+32,"any other flag (Paz room, side ops) is left alone")
c.env.vars.playerDisableActionFlag=0;c.env.TppSave.SaveVarsToSlot("s");check(c.saved[1].flag==MB and c.env.vars.playerDisableActionFlag==0,"a save writes the stock flag")
ok,msg=c:run("mb_weapons","0");check(ok and msg:find("mb_weapons=off",1,true) and c.env.vars.playerDisableActionFlag==MB and
 c.status[#c.status]=="set:EquipHudAll:ALL_KILL_NOUSE","off: the stock rule is back")
check(#c.tx==2 and c.tx[2][2]==2 and c.tx[2][3]==0,"off is sent too")
c=F({mission=30010});ok,msg=c:run("mb_weapons","1");check(ok and msg:find("applied=0;mission=30010",1,true) and c.env.vars.playerDisableActionFlag==MB,"away from Mother Base nothing changes")
c.env.vars.missionCode=30050;c:tick();check(c.env.vars.playerDisableActionFlag==0,"applied on arrival at Mother Base")
c.env.vars.missionCode=30020;c.env.vars.playerDisableActionFlag=MB;c:tick();check(c.env.vars.playerDisableActionFlag==MB,"left alone in another mission")
-- The host resends on every new pairing once it chose; never before.
c=F({mission=30050});c:tick(30);check(#c.tx==0,"never chosen: nothing sent on pairing")
c:run("mb_weapons","1");c.W.epoch=6;c:tick();check(#c.tx==2 and c.tx[2][3]==1,"a new pairing gets the host's choice again")
c:tick(60);check(#c.tx==2,"once per pairing")
c.W.paired=false;c.W.epoch=7;c:tick();check(#c.tx==2,"unpaired: nothing sent")
-- A re-pair into the same host epoch (a guest crash or a long link gap, world-sync-protocol section 5) resends too.
c=F({mission=30050});c:run("mb_weapons","1");c:tick(60);check(#c.tx==1,"chosen at epoch 5: sent once")
c.W.paired=false;for _,f in ipairs(c.W.unpairedHooks)do f("session")end;c:tick(30);check(#c.tx==1,"unpaired: nothing sent")
c.W.paired=true;for _,f in ipairs(c.W.pairedHooks)do f(false)end;c:tick()
check(#c.tx==2 and c.tx[2][3]==1,"a same-epoch re-pair gets the host's choice again")
c:tick(60);check(#c.tx==2,"once per re-pair")
-- The guest: a press is refused; the host's RULES applies on its copy.
c=F({host=false,mission=30050});ok,msg=c:run("mb_weapons","1");check(not ok and msg=="host_only" and #c.tx==0,"the guest's press is refused (host only)")
local h=c.W.handlers[62];check(type(h)=="function","the guest handles RULES")
h({1,0});check(#c.says==0,"the first stock RULES says nothing")
h({2,1});c:tick();check(c.env.vars.playerDisableActionFlag==0 and c.says[1]:find("partner",1,true),"the host's choice applies on the guest's copy, with a sentence")
h({3,0});c:tick();check(c.env.vars.playerDisableActionFlag==MB and #c.says==2,"and goes back with the host's")
h({4,1});c:tick();check(c.env.vars.playerDisableActionFlag==0,"on again")
c.W.paired=false;c:tick(30);check(c.env.vars.playerDisableActionFlag==0,"a short unpaired gap keeps it (a mission load)")
c.now=c.now+61000;c:tick();check(c.env.vars.playerDisableActionFlag==MB,"60 s without the host: stock again")
c=F();h=c.W.handlers[62];c.W.host=true;h({1,1});check(c.D.rules.hostMb==nil,"the host ignores RULES")
-- Status.
c=F({mission=30050});c:run("mb_weapons","1");c:run("wormhole","0");ok,msg=c:run("rules")
check(ok and msg=="rules;wormhole=off;level=0;seen=1;mb_weapons=1;from=self;applied=1","status line")
-- Play (mode 0) runs the same as the lab (0.14.6): the wormhole is each player's own; Mother Base weapons the host's.
for _,role in ipairs({true,false})do
 c=F({mode=0,mission=30050,host=role});ok,msg=c:run("wormhole","0")
 check(ok and msg:find("^wormhole=off") and c.levels[WORM]==0,"play: wormhole off applies")
 ok,msg=c:run("mb_weapons","1")
 if role then check(ok and msg:find("mb_weapons=on;sent=1;applied=1",1,true) and c.env.vars.playerDisableActionFlag==0,"play: the host's weapons at Mother Base apply and are sent")
 else check(not ok and msg=="host_only" and #c.tx==0,"play: the guest's press is still host only") end
 ok,msg=c:run("rules");check(ok and msg:find("^rules;wormhole=off"),"play: status answers")
end
print("debug_rules_lua_check: "..checks.." checks passed")
