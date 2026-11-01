-- Spawn vehicle, synced (lua/coop/debug_vspawn.lua, docs/debug-menu.md "Spawn vehicle"): the host brings one of the
-- area's own vehicles of that class 6 m in front of whoever presses, on both copies (VSPAWN 0x44, world note 20).
local path=assert(arg[1]);local f=assert(io.open(path,"rb"));local src=f:read("*a");f:close()
local checks=0
local function check(v,why)checks=checks+1;if not v then error(checks..": "..why,2)end end
check(#src:gsub("\r\n","\n")<=16000 and #src:gsub("\r?\n","\r\n")<=16000,"embedded fragment fits both checkout modes")
local OP,NULL=68,65535
-- One copy. o.host (default true), o.key (default true), o.mission (30010), o.paired (true).
local function F(o)
 o=o or {}
 local c={tx={},notes={},logs={},sets={},respawns={},host=o.host~=false,key=o.key~=false,onFoot=true,baseRuns=0}
 local env=setmetatable({},{__index=_G});env._G=env
 env.vars={missionCode=o.mission or 30010,playerPosX=0,playerPosY=10,playerPosZ=0,playerRotY=0}
 env.mvars={ene_lrrpVehicle={sol_lrrp_0000="veh_lv_0002"}}
 env.f30010_enemy={VEHICLE_SPAWN_LIST={{locator="veh_lv_0000",type=1},{locator="veh_lv_0001",type=1},{locator="veh_lv_0002",type=1},
  {locator="veh_trc_0000",type=3},{locator="veh_trc_0001",type=3}}}
 env.f30020_enemy={VEHICLE_SPAWN_LIST={{locator="veh_lv_0000",type=2}}}
 -- Vehicles of this copy: id, pose, alive, riders.
 c.v={veh_lv_0000={id=25601,x=100,y=0,z=0,alive=true},veh_lv_0001={id=25602,x=500,y=0,z=0,alive=true},
  veh_lv_0002={id=25603,x=900,y=0,z=0,alive=true},veh_trc_0000={id=25604,x=300,y=0,z=0,alive=true},
  veh_trc_0001={id=25605,x=50,y=0,z=0,alive=true}}
 local function byId(id)for k,v in pairs(c.v)do if v.id==id then return k,v end end end
 env.Vector3=function(x,y,z)return {x=x,y=y,z=z}end
 env.GameObject={NULL_ID=NULL,GetGameObjectId=function(t,n)local v=t=="TppVehicle2" and c.v[n];return v and v.id or NULL end,
  SendCommand=function(id,cmd)
   if type(id)=="table" then
    if id.type=="TppVehicle2" and cmd.id=="Respawn" then c.respawns[#c.respawns+1]=cmd.name;local v=c.v[cmd.name];if v and not c.noRespawn then v.alive=true;v.gone=nil end end
    return true
   end
   local name,v=byId(id);if not v then return nil end
   if cmd.id=="IsAlive" then return v.alive==true end
   if cmd.id=="GetPosition" then if v.gone then return nil end;return {GetX=function()return v.x end,GetY=function()return v.y end,GetZ=function()return v.z end}end
   if cmd.id=="GetRiderId" then return {v.rider or NULL,NULL,NULL,NULL} end
   if cmd.id=="SetPosition" then c.sets[#c.sets+1]={name=name,x=cmd.position.x,y=cmd.position.y,z=cmd.position.z,rotY=cmd.rotY}
    if not c.stuck then v.x,v.y,v.z=cmd.position.x,cmd.position.y,cmd.position.z end;return true end
  end}
 env.TppMission={CheckMissionState=function()return c.gameplay~=false end}
 env.PlayerStatus={STAND=1,SQUAT=2,CRAWL=3,ON_VEHICLE=4,ON_HORSE=5}
 -- c.riding / c.horse: in a vehicle or on the horse with the stance still STAND (hidden run 5cdbfada D).
 env.PlayerInfo={OrCheckStatus=function(s)if s[1]==4 then return c.onFoot==false or c.riding==true end
  if s[1]==5 then return c.horse==true end;return c.onFoot end}
 local W={host=c.host,active=true,mode=o.mode or 2,paired=o.paired~=false,epoch=5,OP={},handlers={},
  MISSIONS={[30010]={areaEnemy="f30010_enemy"},[30020]={areaEnemy="f30020_enemy"},[30050]={}},
  log=function(s)c.logs[#c.logs+1]=s end,note=function(code,a,b)c.notes[#c.notes+1]={code,a,b};return true end}
 function W.tx(op,...)c.tx[#c.tx+1]={op,...};return c.txOk~=false end
 local P={handles={heal=true},run=function(a)c.baseRuns=c.baseRuns+1;return true,"base:"..a[1]end}
 local D={player=P,host=c.host}
 D.api={get=function(t,k)if t==nil then return nil end;local ok,v=pcall(function()return t[k]end);if ok then return v end end,
  call=function(t,k,...)return assert(t[k])(...)end,hostOnly=function()return c.host end,
  receipt=function(t)if t=="return 'MGS_COOP_VSPAWN_CFG_V1'" then return c.key and 1 or 0 end end,
  pose=function()local q=env.vars;return {q.playerPosX,q.playerPosY,q.playerPosZ,q.playerRotY}end}
 env.e={MgsCoopWorld=W,MgsCoopDebug=D,OnUpdate=function()c.updates=(c.updates or 0)+1;return "u" end}
 if o.noPlayer then D.player=nil end
 setfenv(assert(loadstring(src)),env)()
 c.env,c.W,c.D,c.P=env,W,D,P
 function c:run(...)return self.P.run({...},1)end
 function c:tick(n)for _=1,n or 1 do self.env.e.OnUpdate()end end
 function c:has(text)for _,v in ipairs(self.logs)do if tostring(v):find(text,1,true)then return true end end;return false end
 function c:note(a,b)for _,n in ipairs(self.notes)do if n[1]==20 and n[2]==a and n[3]==b then return true end end;return false end
 return c
end
-- Deliver every VSPAWN this copy sent since `from` to the other copy's handler.
local function deliver(a,b,from)for i=from or 1,#a.tx do local t=a.tx[i];if t[1]==OP then b.W.handlers[OP]({select(2,unpack(t))})end end;return #a.tx+1 end
-- Loading: rides the player module's dispatch; op 68 is registered.
local c=F();check(type(c.D.vspawn)=="table","D.vspawn exists")
for _,k in ipairs({"vspawn","vspawn_status","heal"})do check(c.P.handles[k]==true,"handles "..k)end
check(c.W.OP.VSPAWN==OP and type(c.W.handlers[OP])=="function","VSPAWN 68 registered with a handler")
local ok,msg=c:run("heal");check(ok and msg=="base:heal" and c.baseRuns==1,"the player module's own commands still run")
check(c.env.e.OnUpdate()=="u","the update wrapper keeps the original's results")
check(F({noPlayer=true}).D.vspawn==nil,"no player module: inert")
ok,msg=c:run("vspawn","tank");check(not ok and msg:find("vspawn_args",1,true),"only lv and trc")
-- Key off (lab key debug_vspawn named 0; on by default with the gun profile): refused, a plain toast, nothing moved or sent.
c=F({key=false});ok,msg=c:run("vspawn","lv")
check(not ok and msg=="vspawn_refused;reason=off" and c:note(3,1) and #c.sets==0 and #c.tx==0,"key off: refused, note 20 a=3 b=1, nothing moved")
-- Host press (30010, at 0,10,0 facing yaw 0): the farthest free light vehicle that is not an LRRP vehicle (veh_lv_0002
-- is soldier-driven), 6 m in front, 0.8 m up, facing the presser's way; VSPAWN kind 2 to the guest; a toast for the host.
local h,g=F(),F({host=false})
ok,msg=h:run("vspawn","lv")
check(ok and msg:find("vspawn;class=lv;loc=veh_lv_0001",1,true) and msg:find("sent=1",1,true),"host press: veh_lv_0001 brought ("..tostring(msg)..")")
local s=h.sets[1]
check(#h.sets==1 and s.name=="veh_lv_0001" and math.abs(s.x)<1e-6 and math.abs(s.z-6)<1e-6 and math.abs(s.y-10.8)<1e-6 and s.rotY==0,
 "SetPosition 6 m in front, 0.8 m up, rotY = the presser's yaw (degrees)")
local t=h.tx[1]
check(t and t[1]==OP and t[3]==2 and t[4]==1 and t[5]==1 and t[6]==0 and t[7]==10800 and t[8]==6000 and t[9]==0 and t[10]==0,
 "VSPAWN kind 2, class 1, locator number 1, xyz mm, yaw mdeg, reason 0")
check(h:note(1,1) and h:has("event=vspawn;step=pick;loc=veh_lv_0001") and h:has("step=apply;loc=veh_lv_0001"),"host: toast a=1 and the pick/apply log lines")
-- The guest's copy: the same locator at the same pose; its toast says the partner spawned it.
local at=deliver(h,g)
check(#g.sets==1 and g.sets[1].name=="veh_lv_0001" and math.abs(g.sets[1].z-6)<1e-6 and math.abs(g.sets[1].y-10.8)<1e-6 and g.sets[1].rotY==0,
 "guest: the same vehicle at the same spot facing the same way")
check(g:note(2,1) and g:has("step=rx;kind=2") and g:has("step=apply;loc=veh_lv_0001"),"guest: toast a=2 and rx/apply lines")
-- Readback on both copies.
h:tick(20);g:tick(20)
check(h:has("step=placed;loc=veh_lv_0001") and h:has("ok=1") and g:has("step=placed;loc=veh_lv_0001"),"both copies read the pose back (step=placed ok=1)")
-- A second press brings another vehicle (an earlier spawn goes last).
ok,msg=h:run("vspawn","lv");check(ok and msg:find("loc=veh_lv_0000",1,true),"second press: veh_lv_0000, not the one already brought")
-- An occupied vehicle (a soldier or a player in a seat) is never taken.
h=F();h.v.veh_lv_0001.rider=1234;ok,msg=h:run("vspawn","lv");check(ok and msg:find("loc=veh_lv_0000",1,true),"an occupied vehicle is skipped")
-- An LRRP (soldier-driven) vehicle only when no other is free (Afghanistan's trucks are all LRRP trucks), never ridden.
h=F();h.v.veh_lv_0001.rider=1234;h.v.veh_lv_0000.rider=77;ok,msg=h:run("vspawn","lv")
check(ok and msg:find("loc=veh_lv_0002",1,true) and h:has("step=pick;loc=veh_lv_0002") and h:has("lrrp=1"),"the free LRRP vehicle comes last, not never")
h=F();h.v.veh_lv_0001.rider=1234;h.v.veh_lv_0000.rider=77;h.v.veh_lv_0002.rider=5;ok,msg=h:run("vspawn","lv")
check(not ok and msg=="vspawn_refused;reason=none_free" and h:note(3,3),"no free vehicle of that class: refused, note b=3")
-- A wreck or removed vehicle is used only when no live one is free: Respawn{name} first, then moved once alive.
h=F();h.v.veh_trc_0000.alive=false;h.v.veh_trc_0001.alive=false;h.v.veh_trc_0001.rider=5
ok,msg=h:run("vspawn","trc")
check(ok and msg:find("loc=veh_trc_0000",1,true) and h.respawns[1]=="veh_trc_0000","no live truck free: the wreck is re-created by Respawn{name}")
h:tick(3);check(#h.sets==1 and h.sets[1].name=="veh_trc_0000","then moved once it answers IsAlive")
-- Review r1101: the guest gets the wreck's move only once the host's own re-creation is read back in place.
check(msg:find("sent=later",1,true) and #h.tx==0 and not h:note(1,2),"a wreck: nothing sent and no toast before the host's copy has it")
h:tick(20);t=h.tx[1];check(t and t[3]==2 and t[4]==2 and t[5]==0 and h:note(1,2),"read back in place: kind 2 sent, the host's toast")
-- A re-creation that never takes: nothing sent, the press toasts failed (b=7); an ask is answered kind 4 reason 7.
h=F();h.noRespawn=true;h.v.veh_trc_0000.alive=false;h.v.veh_trc_0001.alive=false;h.v.veh_trc_0001.rider=5
ok,msg=h:run("vspawn","trc");h:tick(100)
check(h:has("ok=0;reason=respawn") and #h.tx==0 and #h.sets==0 and h:note(3,7) and not h:note(1,2),"a respawn that never takes: nothing sent, toast b=7")
h,g=F(),F({host=false});h.noRespawn=true;h.v.veh_trc_0000.alive=false;h.v.veh_trc_0001.alive=false;h.v.veh_trc_0001.rider=5
g:run("vspawn","trc");deliver(g,h);check(#h.tx==0,"an ask for a wreck: nothing sent before the host's copy has it")
h:tick(100);t=h.tx[1];check(t and t[3]==4 and t[10]==7 and #h.tx==1,"the ask's wreck never took: kind 4 reason 7")
deliver(h,g);check(g:note(3,7) and #g.sets==0 and g.D.vspawn.ask==nil,"the guest toasts b=7, nothing moved there")
-- Re-creating a lost vehicle clears VEHICLE_GONE's record of it on the host (gone and the PAIRED re-send list).
h=F();h.v.veh_trc_0000.alive=false;h.v.veh_trc_0001.alive=false;h.v.veh_trc_0001.rider=5
h.W.vehicleGone={busy=function(id)return id==25605 and "occupied" or nil end,gone={[25604]=true},sent={{id=25604,seq=1},{id=25601,seq=2}}}
h:run("vspawn","trc");check(h.W.vehicleGone.gone[25604]==nil and #h.W.vehicleGone.sent==1 and h.W.vehicleGone.sent[1].id==25601,
 "the re-created truck leaves V.gone and V.sent (no VEHICLE_GONE re-send at PAIRED)")
-- Guest press: it asks with its own pose; the host brings the vehicle in front of the GUEST, answers kind 3; both toast.
h,g=F(),F({host=false});g.env.vars.playerPosX,g.env.vars.playerPosZ,g.env.vars.playerRotY=20,40,90
ok,msg=g:run("vspawn","trc")
check(ok and msg:find("vspawn_asked;class=trc",1,true) and #g.sets==0,"guest press: an ask, nothing moved yet")
t=g.tx[1];check(t and t[1]==OP and t[3]==1 and t[4]==2 and t[5]==255 and t[6]==20000 and t[8]==40000 and t[9]==90000,"VSPAWN kind 1: class 2, no locator, the guest's pose")
deliver(g,h)
check(#h.sets==1 and h.sets[1].name=="veh_trc_0000" and math.abs(h.sets[1].x-26)<1e-6 and math.abs(h.sets[1].z-40)<1e-6 and h.sets[1].rotY==90,
 "host: the truck 6 m in front of the guest, facing the guest's way (veh_trc_0001 is nearer to the spot, so the far one)")
t=h.tx[1];check(t and t[3]==3 and t[4]==2 and t[5]==0 and t[6]==26000,"host answers kind 3 with the locator and pose")
check(h:note(2,2),"host: toast a=2 (your partner spawned a truck)")
deliver(h,g)
check(#g.sets==1 and g.sets[1].name=="veh_trc_0000" and math.abs(g.sets[1].x-26)<1e-6 and g:note(1,2) and g.D.vspawn.ask==nil,"guest: the same truck; toast a=1; ask done")
-- The host with the key off refuses an ask (kind 4, reason 1); the guest toasts it.
h,g=F({key=false}),F({host=false});g:run("vspawn","lv");deliver(g,h);deliver(h,g)
check(#h.sets==0 and h.tx[1] and h.tx[1][3]==4 and h.tx[1][10]==1 and g:note(3,1) and g.D.vspawn.ask==nil,"host key off: kind 4 reason 1, the guest's toast")
-- An unanswered ask times out after about 5 s (150 updates).
g=F({host=false});g:run("vspawn","lv");g:tick(160);check(g:note(3,8) and g.D.vspawn.ask==nil,"no answer: note b=8")
-- Guest refusals: unpaired.
g=F({host=false,paired=false});ok,msg=g:run("vspawn","lv");check(not ok and msg=="vspawn_refused;reason=unpaired" and g:note(3,6),"guest unpaired: refused")
-- Mother Base and story missions: refused (free roam only).
h=F({mission=30050});ok,msg=h:run("vspawn","lv");check(not ok and msg=="vspawn_refused;reason=mission" and h:note(3,2),"Mother Base: refused, b=2")
-- A class the area has no authored vehicle of: not loaded here.
h=F({mission=30020});ok,msg=h:run("vspawn","trc");check(not ok and msg=="vspawn_refused;reason=not_loaded" and h:note(3,4),"no truck here: not_loaded")
-- Riding or not in gameplay: busy.
h=F();h.onFoot=false;ok,msg=h:run("vspawn","lv");check(not ok and msg=="vspawn_refused;reason=busy" and h:note(3,5),"riding: refused, b=5")
-- Hidden run 5cdbfada D: the host's press while driving the spawned jeep was taken (the stance still reads STAND in a
-- vehicle) and brought another jeep. ON_VEHICLE / ON_HORSE come first; a guest in a back seat (the seats module's
-- passenger seat, BOARD_SEAT lpv, without ON_VEHICLE) is riding too.
-- The refusal line names the check that fired (by=on_vehicle / on_horse / seat / state / stance).
h=F();h.riding=true;ok,msg=h:run("vspawn","lv");check(not ok and msg=="vspawn_refused;reason=busy" and h:note(3,5) and #h.sets==0 and #h.tx==0,"driving with the stance STAND: refused, nothing moved")
check(h:has("step=refused;reason=busy;by=on_vehicle"),"the driving refusal logs by=on_vehicle")
h=F();h.horse=true;ok,msg=h:run("vspawn","trc");check(not ok and msg=="vspawn_refused;reason=busy" and #h.sets==0 and h:has("by=on_horse"),"on the horse: refused (by=on_horse)")
-- Review r1101: ridePose.seat as world_ride_pose.lua has it. S.sat is `R.localVehicle() or r and seatVehicle(r.lpv)`,
-- a boolean: false (not nil) whenever the BOARD_SEAT receipt answers and this player sits nowhere, and native always
-- answers it ({ok=true,on=0} outside a live race, else lpv 65535 on foot). Only a true sat is a back seat.
local function seat(c,r)c.W.ridePose={seat={board=function(cmd)return cmd=="poll" and r or nil end,
 sat=function(q)local v=q and q.lpv;return nil or q and (type(v)=="number" and v==math.floor(v) and v>=25600 and v<=26111)end}}end
for _,r in ipairs({{ok=true,on=0},{ok=true,on=1,lpv=65535}})do
 local g=F({host=false});seat(g,r);ok,msg=g:run("vspawn","lv")
 check(ok and msg:find("vspawn_asked;class=lv",1,true) and #g.tx==1,"guest on foot, the receipt answering on="..r.on..": the ask goes out")
 h=F();seat(h,r);ok,msg=h:run("vspawn","lv")
 check(ok and msg:find("vspawn;class=lv;loc=veh_lv_0001",1,true) and #h.sets==1,"host on foot, the receipt answering on="..r.on..": spawned")
end
local g=F({host=false});seat(g,{ok=true,on=1,lpv=25602});ok,msg=g:run("vspawn","lv")
check(not ok and msg=="vspawn_refused;reason=busy" and #g.tx==0 and g:has("step=refused;reason=busy;by=seat"),"a guest in a back seat: refused (by=seat), no ask sent")
-- Review r1101: the host's press goes out first and is applied only when sent; unpaired it is refused (b=6).
h=F({paired=false});ok,msg=h:run("vspawn","lv")
check(not ok and msg=="vspawn_refused;reason=unpaired" and #h.sets==0 and #h.tx==0 and h:note(3,6) and not h:note(1,1),"host unpaired: refused, nothing moved")
h=F();h.txOk=false;ok,msg=h:run("vspawn","lv")
check(not ok and msg=="vspawn_refused;reason=failed" and #h.sets==0 and h:note(3,7) and not h:note(1,1),"host press not sent: refused (b=7), nothing moved")
-- Never onto a player (world_vehicle_state.lua holds 3.5 m): the host's press checks the partner's last pose (D.partnerPose
-- mm), an ask the host itself; the guest checks itself before applying and answers the host kind 4.
h=F();h.D.partnerPose={1000,10000,5000,80};ok,msg=h:run("vspawn","lv")
check(not ok and msg=="vspawn_refused;reason=in_way" and #h.sets==0 and #h.tx==0 and h:note(3,10),"the partner stands on the spot: refused (b=10)")
h=F();h.D.partnerPose={4000,10000,6000,80};ok,msg=h:run("vspawn","lv");check(ok and #h.sets==1,"the partner 4 m off the spot: spawned")
h,g=F(),F({host=false});g.env.vars.playerPosX,g.env.vars.playerPosZ,g.env.vars.playerRotY=20,40,90
h.env.vars.playerPosX,h.env.vars.playerPosZ=27,41;g:run("vspawn","trc");deliver(g,h)
t=h.tx[1];check(#h.sets==0 and t and t[3]==4 and t[10]==10,"an ask whose spot the host stands on: kind 4 reason 10, nothing moved")
deliver(h,g);check(g:note(3,10) and g.D.vspawn.ask==nil,"the guest toasts b=10")
h,g=F(),F({host=false});g.env.vars.playerPosZ=6.5;h:run("vspawn","lv");deliver(h,g)
t=g.tx[1];check(#g.sets==0 and g:has("step=skip;reason=in_way") and t and t[3]==4 and t[10]==10,"the guest stands on the host's spot: not moved, kind 4 reason 10")
deliver(g,h);check(h:note(3,10) and h:has("step=partner_refused"),"the host toasts b=10")
-- The guest's own key off: the host's move is not applied there; the host hears it (b=9: only on its screen).
h,g=F(),F({host=false,key=false});h:run("vspawn","lv");deliver(h,g)
t=g.tx[1];check(#g.sets==0 and g:has("step=skip;reason=off") and t and t[3]==4 and t[10]==1,"guest key off: not applied, kind 4 reason 1")
deliver(g,h);check(h:note(3,9),"the host toasts b=9 (the vehicle is only on its screen)")
-- The guest's copy has it occupied (its own LRRP crew, a soldier): never moved there, the host hears it. The partner's
-- ride (the host just left it: RIDE_MS latch) is the same vehicle on both copies and is moved.
h,g=F(),F({host=false});g.v.veh_lv_0001.rider=4321;h:run("vspawn","lv");deliver(h,g)
t=g.tx[1];check(#g.sets==0 and g:has("step=apply;loc=veh_lv_0001;id=25602;ok=0;reason=occupied") and t and t[3]==4 and t[10]==9,
 "guest copy occupied: not moved, kind 4 reason 9")
deliver(g,h);check(h:note(3,9),"the host toasts b=9")
h,g=F(),F({host=false});g.W.vehicleGone={busy=function()return "partner_ride" end};h:run("vspawn","lv");deliver(h,g)
check(#g.sets==1 and #g.tx==0,"guest: the partner's latched ride is moved")
-- The pick skips a vehicle VEHICLE_GONE watches (being lost: a Fulton balloon in flight) or holds (a request).
for _,k in ipairs({"watch","held"})do
 h=F();h.W.vehicleGone={busy=function()end,[k]={[25602]={}}};ok,msg=h:run("vspawn","lv")
 check(ok and msg:find("loc=veh_lv_0000",1,true),"the pick skips a vehicle in VEHICLE_GONE's "..k)
end
-- The host's copy did not take the pose: the readback says so (ok=0) after 90 updates.
h=F();h.stuck=true;h:run("vspawn","lv");h:tick(100);check(h:has("step=placed;loc=veh_lv_0001") and h:has("ok=0"),"a move that does not take logs ok=0")
-- A handler error is logged and stays inside the fragment (never world sync's).
h=F();h.env.GameObject.SendCommand=function()error("boom")end;h.env.GameObject.GetGameObjectId=function()error("boom")end
h.W.handlers[OP]({1,1,1,255,0,0,0,0,0});check(true,"a throwing binding does not escape the handler")
-- Status.
h=F();h:run("vspawn","lv");ok,msg=h:run("vspawn_status")
check(ok and msg:find("vspawn;on=1;role=host;mission=30010;recent=veh_lv_0001",1,true),"status: key, role, mission, spawned locators")
-- The play scenario names the key and covers both presses, the ride and a refusal.
if arg[2] then
 local sf=assert(io.open(arg[2],"rb"));local sc=sf:read("*a");sf:close()
 check(sc:find("labsetting debug_vspawn=1",1,true) and sc:find("vspawn lv",1,true) and sc:find("vspawn trc",1,true),"scenario names the key and both classes")
 -- Review r1101: D presses on both copies while riding (the host driving, the guest in the back seat) and names the check.
 local d=sc:match("# %-%-%- D:.-\nstate")
 check(d and d:find("host run vspawn lv",1,true) and d:find("guest run vspawn trc",1,true) and d:find("by=on_vehicle",1,true)
  and d:find("guest expect ^vspawn_refused;reason=busy$",1,true) and d:find("by=(seat|on_vehicle)",1,true),"scenario D: both riders refused, by= named")
end
print("debug_vspawn_lua_check: "..checks.." checks passed")
