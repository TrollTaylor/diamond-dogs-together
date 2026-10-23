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
c=G();c.W.vehicleGone={lever=function(sync)calls[#calls+1]=sync;return true,"vehicle_remove;id=25603"end,status=function()return "vgone;on=1"end}
ok,msg=lever(c,"vehicle_remove nearest");check(ok and msg=="vehicle_remove;id=25603" and calls[1]==false,"vehicle_remove nearest: this copy only")
ok,msg=lever(c,"vehicle_remove nearest sync");check(ok and calls[2]==true,"vehicle_remove nearest sync: through the world op")
ok,msg=lever(c,"vehicle_gone_status");check(ok and msg=="vgone;on=1" and #calls==2,"vehicle_gone_status is read-only")
for _,bad in ipairs({"vehicle_remove","vehicle_remove 25603","vehicle_remove nearest now","vehicle_remove nearest sync 1","vehicle_gone_status x"})do
 ok,msg=lever(c,bad);check(not ok and msg=="vehicle_args" and #calls==2,"strict vehicle-gone arguments: "..bad)
end
print("debug_vehicle_lua_check: "..checks.." checks PASS")
