local path=assert(arg[1]);local f=assert(io.open(path,"rb"));local src=f:read("*a");f:close()
local checks=0
local function check(v,why)checks=checks+1;if not v then error(checks..": "..why,2)end end
check(#src:gsub("\r\n","\n")<=16000 and #src:gsub("\r?\n","\r\n")<=16000,"embedded fragment fits both checkout modes")
local function copy(t)local q={};for k,v in pairs(t)do q[k]=v end;return q end
local function F()
 local c={riders={0,65535,41,65535},reads=0,lookups=0,current=12345,localIndex=0,localId=0,onVehicle=true,typed=true,logs={}}
 local env=setmetatable({},{__index=_G});env._G=env
 env.vars={missionCode=30010,playerVehicleGameObjectId=c.current};env.mvars={ene_vehicleSettings={veh_auth={type=1}}}
 env.f30010_enemy={VEHICLE_SPAWN_LIST={{locator="veh_lv_0000",type=1},{locator="veh_trc_0000",type=3}}}
 local function good()return c.gameplay~=false end
 env.TppMission={CheckMissionState=good};env.DemoDaemon={IsDemoPlaying=function()return c.demo==true end};env.Player={IsInstanceValid=function()return c.valid~=false end}
 env.PlayerStatus={STAND=1,SQUAT=2,CRAWL=3,ON_VEHICLE=4}
 env.PlayerInfo={OrCheckStatus=function(s)if #s==1 then check(s[1]==4,"current target checks only ON_VEHICLE");return c.onVehicle end;return c.posture~=false end,GetLocalPlayerIndex=function()return c.localIndex end}
 env.Tpp={IsVehicle=function(id)check(id==c.current or id==12346,"only selected or authored id type queries");return c.typed end}
 env.GameObject={NULL_ID=65535,GetGameObjectId=function(t,n)
  c.lookups=c.lookups+1;check(t=="TppVehicle2"and (n=="veh_auth"or n=="veh_lv_0000"or n=="veh_trc_0000"),"only authored named lookup")
  if c.lookupError then error("fixture")end;return c.lookupId or 12345
 end,GetGameObjectIdByIndex=function(t,i)check(t=="TppPlayer2" and i==c.localIndex,"exact current local player index only");return c.localId end,
 SendCommand=function(id,q)
  check(id==c.current and q.id=="GetRiderId"and next(q,"id")==nil,"only selected native read command with no mutation fields")
  c.reads=c.reads+1;if c.readHook then c.readHook(c)end
  if c.errorRead then error("fixture")end
  if c.badResult~=nil then return c.badResult end
  if c.nextResult and c.reads==2 then return c.nextResult end
  return copy(c.riders)
 end}
 local W={active=true,paired=true,eff=3,mission=30010,epoch=7,role=1,api={},MISSIONS={[30010]={areaEnemy="f30010_enemy"}}};local D={mission=30010}
 D.api={get=function(t,k)return t and t[k]end,call=function(t,k,...)return assert(t[k])(...)end,integer=function(v,lo,hi)return type(v)=="number"and v==v and v==math.floor(v)and v>=lo and v<=hi end}
 env.e={MgsCoopWorld=W,MgsCoopDebug=D};setfenv(assert(loadstring(src)),env)()
 c.env,c.W,c.D,c.V=env,W,D,D.vehicle
 function c:run(locator)return self.V.run(locator and {"vehicle_seats",locator}or{"vehicle_seats"},3)end
 return c
end
local c=F();local ok,msg=c:run();check(ok and msg=="vehicle;id=12345;seats=4;riders=0,65535,41,65535;localSeat=1;shared=unverified" and c.reads==2 and c.lookups==0,"driver census is exact native IDs and keeps sharing unverified")
c=F();c.riders={41,0};ok,msg=c:run();check(ok and msg:find("localSeat=2",1,true),"native passenger seat recorded without driver inference")
c=F();c.riders={41,65535};ok,msg=c:run();check(ok and msg:find("localSeat=0",1,true),"absent local player is absent, not synthesized")
for _,riders in ipairs({{65535},{0,1},{65535,65535,65535,65535},{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15}})do
 c=F();c.riders=riders;ok,msg=c:run();check(ok and #msg<=160 and c.reads==2,"bounded real seat count and exact occupants")
end
for _,locator in ipairs({"veh_auth","veh_lv_0000","veh_trc_0000"})do
 c=F();c.onVehicle=false;ok,msg=c:run(locator);check(ok and c.reads==2 and c.lookups>=4,"only authored named targets available on foot "..locator)
end
c=F();c.onVehicle=false;ok,msg=c:run();check(not ok and msg=="vehicle_not_riding"and c.reads==0,"no implicit numeric target or index scan on foot")
for _,locator in ipairs({"12345","veh_unknown","../../veh_auth","veh_auth;run","","a b",string.rep("x",65)})do
 c=F();ok,msg=c:run(locator);check(not ok and c.reads==0 and c.lookups==0,"untrusted or unauthored locator refuses before native queries")
end
for _,bad in ipairs({true,1,"4",{}, {[1]=0,[3]=41}, {[0]=1}, {[1]=-1}, {[1]=65536}, {[1]=0.5}, {[1]=0/0}, {[1]="0"}, {[1]=true}, {[1]=0,[2]=0}, {[1]=41,[2]=41}, {["n"]=2}})do
 c=F();c.badResult=bad;ok,msg=c:run();check(not ok and c.reads==1,"invalid or duplicate native seat results fail closed")
end
c=F();for i=1,17 do c.riders[i]=i end;ok,msg=c:run();check(not ok and msg=="vehicle_seats_invalid"and c.reads==1,"native seat array capped at sixteen")
for _,change in ipairs({{0,65535,65535,65535},{0,65535},{65535,0,41,65535}})do
 c=F();c.nextResult=change;ok,msg=c:run();check(not ok and msg=="vehicle_seats_changed"and c.reads==2,"seat occupancy/count change never returns stale census")
end
for _,change in ipairs({"mission","epoch","role","api","world","paired","eff","vehicle","descriptor","module"})do
 c=F();c.readHook=function(s)
  if s.reads~=1 then return end
  if change=="mission"then s.W.mission=30020
  elseif change=="epoch"then s.W.epoch=8
  elseif change=="role"then s.W.role=2
  elseif change=="api"then s.W.api={}
  elseif change=="world"then s.env.e.MgsCoopWorld={}
  elseif change=="paired"then s.W.paired=false
  elseif change=="eff"then s.W.eff=2
  elseif change=="vehicle"then s.env.vars.playerVehicleGameObjectId=12346
  elseif change=="descriptor"then s.env.mvars.ene_vehicleSettings.veh_auth={type=1}
  elseif change=="module"then s.env.f30010_enemy={VEHICLE_SPAWN_LIST={{locator="veh_lv_0000",type=1}}}end
 end
 local locator=change=="descriptor"and"veh_auth"or change=="module"and"veh_lv_0000"or nil
 ok,msg=c:run(locator);check(not ok and c.reads==1,"owner or descriptor change aborts before second native read "..change)
end
for _,change in ipairs({"localIndex","localId","typed","lookupId"})do
 c=F();c.readHook=function(s)if s.reads==2 then
  if change=="localIndex"then s.localIndex=1
  elseif change=="localId"then s.localId=1
  elseif change=="typed"then s.typed=false
  else s.lookupId=12346 end
 end end
 ok,msg=c:run(change=="lookupId"and"veh_auth"or nil);check(not ok and c.reads==2,"final local identity/type changes refuse "..change)
end
for _,guard in ipairs({"inactive","unpaired","level","mission","epoch","role","gameplay","demo","invalidPlayer","posture","postureBinding","debugMission","worldApi"})do
 c=F()
 if guard=="inactive"then c.W.active=false elseif guard=="unpaired"then c.W.paired=false elseif guard=="level"then c.W.eff=2
 elseif guard=="mission"then c.env.vars.missionCode=10010 elseif guard=="epoch"then c.W.epoch=0 elseif guard=="role"then c.W.role=3
 elseif guard=="gameplay"then c.gameplay=false elseif guard=="demo"then c.demo=true elseif guard=="invalidPlayer"then c.valid=false
 elseif guard=="posture"then c.posture=false elseif guard=="postureBinding"then c.env.PlayerStatus.ON_VEHICLE=nil
 elseif guard=="debugMission"then c.D.mission=30020 else c.W.api=nil end
 ok,msg=c:run();check(not ok and c.reads==0 and c.lookups==0,"lifecycle refusal has no native seat read "..guard)
end
for _,bad in ipairs({-1,0.5,4,65535,"0",0/0})do c=F();c.localIndex=bad;ok,msg=c:run();check(not ok and c.reads==0,"invalid native local index refuses")end
for _,bad in ipairs({-1,65535,"0",0/0})do c=F();c.localId=bad;ok,msg=c:run();check(not ok and c.reads==0,"invalid native local ID refuses")end
c=F();c.typed=false;ok,msg=c:run();check(not ok and c.reads==0,"wrong GameObject type cannot query seats")
c=F();c.env.vars.playerVehicleGameObjectId=65535;ok,msg=c:run();check(not ok and c.reads==0,"null ridden identity refuses")
c=F();c.errorRead=true;ok,msg=c:run();check(not ok and msg=="vehicle_binding"and c.reads==1,"protected native Lua error refuses")
c=F();c.lookupError=true;ok,msg=c:run("veh_auth");check(not ok and msg=="vehicle_binding"and c.reads==0,"lookup errors refuse before seat reader")
c=F();c.V.stop();ok,msg=c:run();check(ok and c.reads==2,"stop owns no state to restore and next read has fresh identity")
-- The seated rider levers (docs/anim-round1-seat.md): vehicle_names and seat_dm.
local function G()
 local c=F()
 c.ids={veh_lv_0000=25608,veh_lv_0001=25609,veh_lv_0002=25610,veh_lv_0005=25613,veh_trc_0000=25621,veh_trc_0001=25622,horse_0000=6656,veh_walker=22529}
 c.receipts,c.seatOk,c.plays,c.stops,c.names={},true,{},0,{}
 c.env.GameObject.GetGameObjectId=function(t,n)
  check(t=="TppVehicle2","vehicle type lookups only");c.lookups=c.lookups+1;c.names[#c.names+1]=n;return c.ids[n] or 65535
 end
 c.D.api.receipt=function(text)c.receipts[#c.receipts+1]=text;return c.seatOk end
 c.env.Player.RequestToPlayDirectMotion=function(t)c.plays[#c.plays+1]=t end
 c.env.Player.RequestToStopDirectMotion=function()c.stops=c.stops+1 end
 c.env.e.MgsCoopPartnerRideSeat=function()return 25608,0 end
 return c
end
local function lever(c,text)local a={};for w in text:gmatch("%S+")do a[#a+1]=w end;return c.V.run(a,3)end
c=G();ok,msg=lever(c,"vehicle_names")
check(ok and msg=="vehicle_names;lv=0-2@25608,5@25613;trc=0-1@25621;local=25608;seat=0" and c.lookups==48 and c.reads==0,
 "vehicle names: runs of consecutive ids, the local vehicle and seat "..tostring(msg))
c=G();c.env.e.MgsCoopPartnerRideSeat=nil;c.ids={};ok,msg=lever(c,"vehicle_names")
check(ok and msg=="vehicle_names;lv=none;trc=none;local=65535;seat=255","no authored vehicle resolves")
c=G();ok,msg=lever(c,"vehicle_names extra");check(not ok and msg=="vehicle_args" and c.lookups==0,"vehicle names takes no argument")
c=G();ok,msg=lever(c,"seat_dm stateVehicleBackLeftLoop 2 veh_lv_0000")
local p=c.plays[1]
check(ok and msg=="seat_dm;id=25608;seat=2;name=veh_lv_0000;state=stateVehicleBackLeftLoop" and
 c.receipts[1]=="return 'MGS_COOP_PARTNER_SEAT_V1' -- arm 2 25608" and #c.plays==1 and p[1][1]=="coopRide" and p[1][2]==1 and
 p[2][1]=="stateVehicleBackLeftLoop" and p[2][2]==true and p[2][3]=="veh_lv_0000" and p[2][4]=="CNP_ppos_c" and p[2][5]=="MTP_GLOBAL_C" and
 p[2][6]==true and p[3]==nil,"seat_dm arms natively, then plays the targeted seated state on body 1")
c=G();c.seatOk=false;ok,msg=lever(c,"seat_dm stateVehicleFrontLeftLoop 0 veh_lv_0000")
check(not ok and msg=="seat_dm_refused" and #c.plays==0 and #c.receipts==1,"a native refusal plays nothing")
for _,bad in ipairs({"seat_dm stateVehicleFrontLeftLoop 0 horse_0000","seat_dm stateVehicleFrontLeftLoop 0 veh_walker",
 "seat_dm stateVehicleFrontLeftLoop 0 veh_lv_0031"})do
 c=G();ok,msg=lever(c,bad);check(not ok and msg=="seat_dm_locator" and #c.receipts==0 and #c.plays==0,"never a horse, walker or unresolved locator: "..bad)
end
for _,bad in ipairs({"seat_dm stateDirectPlayRideRearLeftSeatOnVehicle 2 veh_lv_0000","seat_dm stateVehicleFrontLeftLoop 4 veh_lv_0000",
 "seat_dm stateVehicleFrontLeftLoop 0","seat_dm vehicleFrontLeftLoop 0 veh_lv_0000","seat_dm stateVehicleFrontLeftLoop 0 veh/lv",
 "seat_dm stateVehicleFrontLeftLoop 01 veh_lv_0000","seat_dm state_x 0 veh_lv_0000","seat_dm stop now"})do
 c=G();ok,msg=lever(c,bad);check(not ok and msg=="seat_dm_args" and c.lookups==0 and #c.receipts==0,"strict seat_dm arguments: "..bad)
end
c=G();c.env.vars.missionCode=30050;c.W.mission=30050;c.D.mission=30050;c.W.MISSIONS[30050]={}
ok,msg=lever(c,"seat_dm stateVehicleFrontLeftLoop 0 veh_lv_0000")
check(not ok and msg=="seat_dm_mission" and #c.receipts==0,"seat_dm is free-roam only")
c=G();ok,msg=lever(c,"seat_dm stop")
check(ok and msg=="seat_dm=stop" and c.stops==1 and c.receipts[1]=="return 'MGS_COOP_PARTNER_SEAT_V1' -- stop","seat_dm stop")
c=G();c.seatOk=false;ok,msg=lever(c,"seat_dm stop");check(not ok and msg=="seat_dm_stop_refused" and c.stops==1,"stop refused natively")
c=G();c.env.Player.RequestToPlayDirectMotion=nil;ok,msg=lever(c,"seat_dm stateVehicleFrontLeftLoop 0 veh_lv_0000")
check(not ok and msg=="vehicle_binding","a missing DM binding is a binding refusal")
-- 0.13.6 online-vehicle-gone: vehicle_remove nearest [sync] and vehicle_gone_status route to
-- lua/coop/world_vehicle.lua (W.vehicleGone); anything else is vehicle_args (before: command_args).
c=G();check(c.V.handles.vehicle_remove and c.V.handles.vehicle_gone_status,"the vehicle-gone levers are handled here")
ok,msg=lever(c,"vehicle_remove nearest");check(not ok and msg=="vehicle_gone_unavailable" and c.reads==0,"no module: refused, nothing read")
local calls={}
local fultons={}
c=G();c.W.vehicleGone={lever=function(sync,fulton)calls[#calls+1]=sync;fultons[#calls]=fulton;return true,"vehicle_remove;id=25603"end,status=function()return "vgone;on=1"end}
ok,msg=lever(c,"vehicle_remove nearest");check(ok and msg=="vehicle_remove;id=25603" and calls[1]==false and not fultons[1],"vehicle_remove nearest: this copy only")
ok,msg=lever(c,"vehicle_remove nearest sync");check(ok and calls[2]==true and not fultons[2],"vehicle_remove nearest sync: through the world op")
ok,msg=lever(c,"vehicle_remove nearest fulton");check(ok and calls[3]==true and fultons[3]==true,"vehicle_remove nearest fulton: the engine's Fulton, synced")
ok,msg=lever(c,"vehicle_gone_status");check(ok and msg=="vgone;on=1" and #calls==3,"vehicle_gone_status is read-only")
for _,bad in ipairs({"vehicle_remove","vehicle_remove 25603","vehicle_remove nearest now","vehicle_remove nearest sync 1","vehicle_remove nearest fulton sync","vehicle_gone_status x"})do
 ok,msg=lever(c,bad);check(not ok and msg=="vehicle_args" and #calls==3,"strict vehicle-gone arguments: "..bad)
end
-- M4 VEHICLE_STATE: vehicle_push / vehicle_life / vehicle_state_status route to lua/coop/world_vehicle_state.lua
-- (W.vehicleState, which checks the arguments: world_vehicle_state_lua_check).
c=G();check(c.V.handles.vehicle_push and c.V.handles.vehicle_life and c.V.handles.vehicle_state_status,"the vehicle-state levers are handled here")
ok,msg=lever(c,"vehicle_push 25608 3 0");check(not ok and msg=="vehicle_state_unavailable" and c.reads==0,"no module: refused, nothing read")
local sargs={}
c=G();c.W.vehicleState={lever=function(a)sargs[#sargs+1]=table.concat(a," ");return true,"vstate"end}
ok,msg=lever(c,"vehicle_push 25608 3 0");check(ok and msg=="vstate" and sargs[1]=="vehicle_push 25608 3 0","vehicle_push <id> <dx> <dz>")
ok,msg=lever(c,"vehicle_push nearest -2.5 1");check(ok and sargs[2]=="vehicle_push nearest -2.5 1","vehicle_push nearest with signed metres")
ok,msg=lever(c,"vehicle_life 25608 0");check(ok and sargs[3]=="vehicle_life 25608 0","vehicle_life <id> <life>")
ok,msg=lever(c,"vehicle_state_status");check(ok and sargs[4]=="vehicle_state_status","vehicle_state_status")
ok,msg=lever(c,"vehicle_state_status 25608");check(ok and sargs[5]=="vehicle_state_status 25608","vehicle_state_status <id>")
-- G4 truck test levers (docs/native-puppet.md "Truck test levers"): goto_vehicle truck (gotoTruck), goto_partner_vehicle
-- and vehicle_door over a stubbed MGS_COOP_VEHICLE_DOOR_V1 receipt (lab.ini debug_vehicle_door=1; doorOff = its refusal).
-- The stub's door: point (99, 2, 2) m, outward axis +x, the stand spot cm further out at y 1.5, facing the seat (-x).
local function H()
 local c=F()
 c.trucks={veh_trc_0000={25602,{100,0,0},65535},veh_trc_0001={25603,{10,0,0},41},veh_trc_0002={25604,{50,0,0},65535}}
 c.doors,c.warps,c.wlogs,c.me={},{},{},{0,0,0}
 c.answer=function(target,seat,cm)
  return {ok=true,id=tonumber(target) or 25602,peer=target=="partner" and 1 or 0,pseat=target=="partner" and 0 or 255,type=3,seats=2,
   rider0=65535,x=99000+cm*10,y=1500,z=2000,face=-1571,dx=99000,dy=2000,dz=2000,ax=1000,az=0,rx=100000,ry=1000,rz=0}
 end
 local byId={}
 for _,t in pairs(c.trucks)do byId[t[1]]=t end
 c.env.GameObject={NULL_ID=65535,GetGameObjectId=function(t,n)check(t=="TppVehicle2","vehicle lookups only");local v=c.trucks[n];return v and v[1] or 65535 end,
  SendCommand=function(id,q)
   local t=byId[id];check(t~=nil and next(q,"id")==nil,"only listed trucks, no mutation fields")
   if q.id=="GetRiderId"then return {t[3],65535}end
   check(q.id=="GetPosition","read-only truck commands only")
   local p=t[2];return {GetX=function()return p[1]end,GetY=function()return p[2]end,GetZ=function()return p[3]end}
  end}
 c.D.api.receipt=function(text)
  c.doors[#c.doors+1]=text
  local target,seat,cm=text:match("^return 'MGS_COOP_VEHICLE_DOOR_V1' %-%- (%w+) ([01]) (%d+)$")
  check(target~=nil and (target=="partner" or tonumber(target)),"strict door receipt grammar: "..text)
  if c.doorOff then return {ok=false,reason=c.doorOff}end
  return c.answer(target,tonumber(seat),tonumber(cm))
 end
 c.D.api.pose=function()return {c.me[1],c.me[2],c.me[3],0}end
 c.D.api.finite=function(n)return type(n)=="number" and n==n and math.abs(n)<1e15 end
 c.D.api.warp=function(p,id)c.warps[#c.warps+1]={p,id};return true,"submitted_warp"end
 c.W.log=function(s)c.wlogs[#c.wlogs+1]=s end
 return c
end
local function truck(c,...)return c.V.gotoTruck({"goto_vehicle","truck",...},9)end
local function near(a,b)return math.abs(a-b)<1e-6 end
c=H();ok,msg=truck(c)
local w=c.warps[1]
check(ok and c.doors[1]=="return 'MGS_COOP_VEHICLE_DOOR_V1' -- 25604 0 60" and #c.warps==1 and w[2]==9 and near(w[1][1],99.6) and
 near(w[1][2],1.5) and near(w[1][3],2) and math.abs(w[1][4]+90)<0.02 and
 msg=="submitted_warp;goto_vehicle;name=veh_trc_0002;id=25604;dist=50;door=0;type=3;rider0=65535;out=60" and
 c.wlogs[1]=="goto_vehicle;name=veh_trc_0002;id=25604;dist=50;door=0;type=3;rider0=65535;out=60" and
 c.V.last[1]=="25604" and c.V.last[2]==0,"the nearest truck with a free driver seat (not veh_trc_0001: an AI driver), its driver door 60 cm out "..tostring(msg))
c=H();ok,msg=truck(c,"passenger","30")
check(ok and c.doors[1]=="return 'MGS_COOP_VEHICLE_DOOR_V1' -- 25604 1 30" and near(c.warps[1][1][1],99.3) and msg:find(";door=1;",1,true),
 "the passenger door and the distance argument")
c=H();ok,msg=truck(c,"0");check(ok and c.doors[1]:sub(-9)=="25604 0 0","0 cm: the door point itself")
for _,bad in ipairs({{"left"},{"151"},{"60","driver"},{"0601"},{"-5"},{"driver","passenger"},{"6.5"}})do
 c=H();ok,msg=truck(c,unpack(bad));check(not ok and msg=="goto_vehicle_args" and #c.doors==0 and #c.warps==0,"strict door arguments: "..table.concat(bad," "))
end
c=H();c.doorOff="off";ok,msg=truck(c);check(ok==nil and msg==nil and #c.warps==0,"key off: nil (debug_player.lua keeps the old 1.9 m spot)")
c=H();c.doorOff="off";ok,msg=truck(c,"60");check(not ok and msg=="door_off" and #c.warps==0,"key off with a door argument refuses")
c=H();c.doorOff="lab_only";ok,msg=truck(c);check(not ok and msg=="door_lab_only" and #c.warps==0,"outside the lab: refused, no fallback")
c=H();c.doorOff="bad reason!";ok,msg=truck(c);check(not ok and msg=="door_refused","an odd native reason is not echoed")
c=H();for _,t in pairs(c.trucks)do t[3]=41 end;ok,msg=truck(c)
check(not ok and msg=="vehicle_driver_taken;id=25603;rider0=65535" and c.doors[1]:sub(-10)=="25603 0 60" and #c.warps==0,
 "every driver seat taken: the nearest truck is named, no warp")
c=H();for _,t in pairs(c.trucks)do t[3]=41 end;c.doorOff="off";ok,msg=truck(c);check(ok==nil,"taken drivers with the key off: the old spot")
c=H();c.trucks={};ok,msg=truck(c);check(ok==nil and #c.doors==0,"no truck, no argument: the old path reports it")
c=H();c.trucks={};ok,msg=truck(c,"60");check(not ok and msg=="vehicle_none","no truck with a door argument")
c=H();c.answer=function()return {ok=true,id=25604,pseat=255,type=3,seats=2,rider0=65535,x=1,y=1,z=1,dx=1,dy=1,dz=1,ax=1,az=0}end
ok,msg=truck(c);check(not ok and msg=="door_answer" and #c.warps==0,"an answer without face refuses")
c=H();c.D.api.receipt=function()return nil end;ok,msg=truck(c);check(not ok and msg=="door_receipt","no receipt answer")
-- goto_partner_vehicle: the vehicle of the partner's live ride packet, the passenger door by default.
c=H();ok,msg=c.V.run({"goto_partner_vehicle"},7)
check(ok and c.doors[1]=="return 'MGS_COOP_VEHICLE_DOOR_V1' -- partner 1 60" and c.warps[1][2]==7 and
 msg=="submitted_warp;goto_partner_vehicle;id=25602;pseat=0;door=1;type=3;rider0=65535;fast=0;out=60" and c.V.last[1]=="partner" and
 c.V.last[2]==1,"goto_partner_vehicle "..tostring(msg))
c=H();c.W.ridePose={fast={tracks={[25602]={samples={}}}}};ok,msg=c.V.run({"goto_partner_vehicle","driver","40"},7)
check(ok and c.doors[1]:sub(-12)=="partner 0 40" and msg:find(";door=0;",1,true) and msg:find(";fast=1;out=40",1,true),
 "the driver door, 40 cm, and the RIDE_FAST track of that vehicle")
c=H();c.doorOff="partner_ride";ok,msg=c.V.run({"goto_partner_vehicle"},7)
check(not ok and msg=="door_partner_ride" and #c.warps==0,"no live vehicle ride from the partner: refused")
for _,bad in ipairs({{"40","driver"},{"front"},{"200"},{"driver","1","2"}})do
 c=H();ok,msg=c.V.run({"goto_partner_vehicle",unpack(bad)},7)
 check(not ok and msg=="goto_partner_vehicle_args" and #c.doors==0,"strict partner arguments: "..table.concat(bad," "))
end
-- vehicle_door: the stock truck gate (1.4 m of the door point, outer side) at the local position.
c=H();ok,msg=c.V.run({"vehicle_door"},7);check(not ok and msg=="vehicle_door_args" and #c.doors==0,"no target before a goto")
c=H();truck(c);c.me={99.6,1.5,2};ok,msg=c.V.run({"vehicle_door"},7)
check(ok and c.doors[2]=="return 'MGS_COOP_VEHICLE_DOOR_V1' -- 25604 0 0" and
 msg=="vehicle_door;id=25604;type=3;seat=0;rider0=65535;gate=pass;d3=0.78;dot=0.60;dy=-0.50","the last goto's door, gate pass "..tostring(msg))
c.me={98.8,1.5,2};ok,msg=c.V.run({"vehicle_door"},7);check(ok and msg:find(";gate=inside;",1,true),"inside the door point: inside")
c.me={101,0,2};ok,msg=c.V.run({"vehicle_door"},7);check(ok and msg:find(";gate=far;",1,true),"beyond 1.4 m: far")
c.V.stop();ok,msg=c.V.run({"vehicle_door"},7);check(not ok and msg=="vehicle_door_args","stop forgets the last door")
c=H();ok,msg=c.V.run({"vehicle_door","partner","passenger"},7);check(ok and c.doors[1]:sub(-11)=="partner 1 0","partner, passenger door")
c=H();ok,msg=c.V.run({"vehicle_door","partner"},7);check(ok and c.doors[1]:sub(-11)=="partner 1 0","partner defaults to the passenger door (goto_partner_vehicle's)")
c=H();ok,msg=c.V.run({"vehicle_door","partner","driver"},7);check(ok and c.doors[1]:sub(-11)=="partner 0 0","partner, driver door named")
c=H();ok,msg=c.V.run({"vehicle_door","veh_trc_0000"},7);check(ok and c.doors[1]:sub(-9)=="25602 0 0","a locator, the driver door")
c=H();ok,msg=c.V.run({"vehicle_door","veh_trc_0009"},7);check(not ok and msg=="vehicle_door_locator" and #c.doors==0,"an unresolved locator")
for _,bad in ipairs({{"bogus"},{"partner","left"},{"partner","driver","x"},{"12345"}})do
 c=H();ok,msg=c.V.run({"vehicle_door",unpack(bad)},7);check(not ok and msg=="vehicle_door_args" and #c.doors==0,"strict report arguments: "..table.concat(bad," "))
end
-- M3 experiments E2/E17/E18: relief_* route to lua/coop/world_relief.lua (W.relief), whole argument list, nothing read here.
c=G();for _,k in ipairs({"relief_census","relief_arm","relief_seize","relief_remove","relief_face","relief_load","relief_status","relief_lab"})do check(c.V.handles[k],"handled here: "..k)end
ok,msg=lever(c,"relief_census");check(not ok and msg=="relief_unavailable" and c.reads==0 and c.lookups==0,"no world module: refused, nothing read")
local got={}
c=G();c.W.relief={run=function(a)got[#got+1]=table.concat(a," ");return true,"relief;m=30010"end}
ok,msg=lever(c,"relief_census arrival");check(ok and msg=="relief;m=30010" and got[1]=="relief_census arrival","census routes with its tag")
ok,msg=lever(c,"relief_arm 2 805 343 1636.5 0");check(ok and got[2]=="relief_arm 2 805 343 1636.5 0" and c.reads==0,"arm routes its five arguments")
ok,msg=lever(c,"relief_unknown");check(ok and got[3]=="relief_unknown","any relief_ word goes to the module, which checks it")
check(c.V.handles.vehicle_census,"handled here: vehicle_census")
ok,msg=lever(c,"vehicle_census 0 8");check(ok and got[4]=="vehicle_census 0 8","M6 E6: vehicle_census routes to the world module")
print("debug_vehicle_lua_check: "..checks.." checks PASS")
