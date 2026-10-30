-- Original read-only vehicle seat census on the natural paired Lua owner.
-- Exact retail GetRiderId returns native seat IDs, never inferred peer riders.
do
 local D,A=e.MgsCoopDebug,e.MgsCoopDebug.api
 local get,call,integer,fmt=A.get,A.call,A.integer,string.format
 local V={handles={vehicle_seats=true,vehicle_names=true,seat_dm=true,vehicle_remove=true,vehicle_gone_status=true,goto_partner_vehicle=true,vehicle_door=true,
 relief_census=true,relief_arm=true,relief_seize=true,relief_remove=true,relief_face=true,relief_load=true,vehicle_census=true,
  relief_status=true,relief_lab=true,vehicle_push=true,vehicle_life=true,vehicle_state_status=true}};D.vehicle=V
 function V.stop()V.last=nil end -- No native state or borrowed objects; only the last door target.
 local function name(v)return type(v)=="string" and #v>0 and #v<=64 and v:match("^[%w_]+$")~=nil end
 local function context()
  local W=get(e,"MgsCoopWorld");local m=get(vars,"missionCode")
  if type(W)~="table" or W.active~=true or W.paired~=true or not integer(W.eff,3,3) then return nil,"vehicle_world_inactive" end
  if not (m==30010 or m==30020 or m==30050) or W.mission~=m or D.mission~=m or not integer(W.epoch,1,65535) or not integer(W.role,1,2) then return nil,"vehicle_scope" end
  if type(W.api)~="table" or call(TppMission,"CheckMissionState")~=true or call(DemoDaemon,"IsDemoPlaying")~=false or call(Player,"IsInstanceValid")~=true then return nil,"vehicle_context" end
  local sts={get(PlayerStatus,"STAND"),get(PlayerStatus,"SQUAT"),get(PlayerStatus,"CRAWL"),get(PlayerStatus,"ON_VEHICLE")}
  for i=1,4 do if not integer(sts[i],0,65535) then return nil,"vehicle_posture_binding" end end
  if call(PlayerInfo,"OrCheckStatus",sts)~=true then return nil,"vehicle_posture" end
  return {world=W,api=W.api,mission=m,epoch=W.epoch,role=W.role}
 end
 local function same(c)
  local n,why=context()
  return n and n.world==c.world and n.api==c.api and n.mission==c.mission and n.epoch==c.epoch and n.role==c.role,why
 end
 local function declared(c,locator)
  local settings=get(mvars,"ene_vehicleSettings");local v=get(settings,locator)
  if type(v)=="table" and integer(get(v,"type"),0,255) then return {table=settings,entry=v,type=v.type} end
  local feature=get(get(c.world,"MISSIONS"),c.mission);local mod=get(feature,"areaEnemy")
  if not name(mod) then return nil,"vehicle_not_authored" end
  local owner=get(_G,mod);local list=get(owner,"VEHICLE_SPAWN_LIST")
  if type(list)~="table" then return nil,"vehicle_not_authored" end
  local found
  for i=1,129 do
   local q=get(list,i);if q==nil then break end
   if i>128 then return nil,"vehicle_descriptors_overflow" end
   if type(q)~="table" then return nil,"vehicle_descriptor_invalid" end
   if get(q,"locator")==locator then
    if found or not integer(get(q,"type"),0,255) then return nil,"vehicle_descriptor_invalid" end
    found={table=list,entry=q,type=q.type,owner=owner,module=mod,index=i}
   end
  end
  return found,found and nil or "vehicle_not_authored"
 end
 local function target(c,locator)
  local null=get(GameObject,"NULL_ID")
  if null~=65535 then return nil,"vehicle_null_binding" end
  local id,desc
  if locator then
   local why;desc,why=declared(c,locator)
   if not desc then return nil,why end
   id=call(GameObject,"GetGameObjectId","TppVehicle2",locator)
  else
   if call(PlayerInfo,"OrCheckStatus",{PlayerStatus.ON_VEHICLE})~=true then return nil,"vehicle_not_riding" end
   id=get(vars,"playerVehicleGameObjectId")
  end
  if not integer(id,0,65534) or call(Tpp,"IsVehicle",id)~=true then return nil,"vehicle_typed_id" end
  return {id=id,descriptor=desc}
 end
 local function validate(c,t,locator)
  if not same(c) then return false,"vehicle_context_changed" end
  local n,why=target(c,locator)
  if not n or n.id~=t.id then return false,why or "vehicle_identity_changed" end
  local a,b=t.descriptor,n.descriptor
  if a and (not b or a.table~=b.table or a.entry~=b.entry or a.type~=b.type or a.owner~=b.owner or a.index~=b.index) then return false,"vehicle_descriptor_changed" end
  return true
 end
 local function seats(id)
  local r=call(GameObject,"SendCommand",id,{id="GetRiderId"})
  if type(r)~="table" then return nil,"vehicle_seats_unreadable" end
  local n,seen,out=0,{},{}
  for k,v in pairs(r)do
   n=n+1
   if n>16 or not integer(k,1,16) or not integer(v,0,65535) then return nil,"vehicle_seats_invalid" end
   if v~=65535 and seen[v] then return nil,"vehicle_rider_duplicate" end
   seen[v]=v~=65535 or nil;out[k]=v
  end
  if n<1 then return nil,"vehicle_seats_unavailable" end
  for i=1,n do if out[i]==nil then return nil,"vehicle_seats_invalid" end end
  return out
 end
 local function read(a)
  if #a<1 or #a>2 or a[1]~="vehicle_seats" or (a[2]~=nil and not name(a[2])) then return false,"vehicle_args" end
  local locator=a[2];local c,why=context();if not c then return false,why end
  local t,why=target(c,locator);if not t then return false,why end
  if not validate(c,t,locator) then return false,"vehicle_context_changed" end
  local localIndex=call(PlayerInfo,"GetLocalPlayerIndex")
  if not integer(localIndex,0,3) then return false,"vehicle_player_index" end
  local localId=call(GameObject,"GetGameObjectIdByIndex","TppPlayer2",localIndex)
  if not integer(localId,0,65534) then return false,"vehicle_player_id" end
  local first,why=seats(t.id);if not first then return false,why end
  local good,why=validate(c,t,locator);if not good then return false,why end
  local second,why=seats(t.id);if not second then return false,why end
  local good,why=validate(c,t,locator);if not good then return false,why end
  if #first~=#second then return false,"vehicle_seats_changed" end
  local riders,slot={},0
  for i=1,#first do
   if first[i]~=second[i] then return false,"vehicle_seats_changed" end
   riders[i]=tostring(second[i]);if second[i]==localId then slot=i end
  end
  if call(PlayerInfo,"GetLocalPlayerIndex")~=localIndex or call(GameObject,"GetGameObjectIdByIndex","TppPlayer2",localIndex)~=localId or not validate(c,t,locator) then return false,"vehicle_context_changed" end
  local text=fmt("vehicle;id=%d;seats=%d;riders=%s;localSeat=%d;shared=unverified",t.id,#second,table.concat(riders,","),slot)
  if #text>160 then return false,"vehicle_receipt_overflow" end
  return true,text
 end
 -- The seated rider levers (docs/anim-round1-seat.md). vehicle_names: every
 -- resolvable authored veh_lv_/veh_trc_ locator as runs "<index range>@<first id>"
 -- (ids consecutive within a run), the local vehicle id and native seat.
 local function names(a)
  if #a~=1 then return false,"vehicle_args" end
  local c,why=context();if not c then return false,why end
  local out={}
  for _,class in ipairs({{"lv",32},{"trc",16}})do
   local runs,run={},nil
   for k=0,class[2]-1 do
    local got=call(GameObject,"GetGameObjectId","TppVehicle2",fmt("veh_%s_%04d",class[1],k))
    if integer(got,0,65534) then
     if run and run.last==k-1 and run.id+(k-run.first)==got then run.last=k
     else run={first=k,last=k,id=got};runs[#runs+1]=run end
    else run=nil end
   end
   for i,r in ipairs(runs)do runs[i]=r.first==r.last and fmt("%d@%d",r.first,r.id) or fmt("%d-%d@%d",r.first,r.last,r.id) end
   out[#out+1]=class[1].."="..(#runs>0 and table.concat(runs,",") or "none")
  end
  local mine=get(e,"MgsCoopPartnerRideSeat");local vid,seat
  if type(mine)=="function" then vid,seat=mine() end
  local text=fmt("vehicle_names;%s;local=%d;seat=%d",table.concat(out,";"),integer(vid,0,65534) and vid or 65535,integer(seat,0,255) and seat or 255)
  if #text>160 then text=text:sub(1,160) end
  return true,text
 end
 -- seat_dm <state> <seat 0..3> <locator>: body 1's seat dwords (native seat receipt) and the seated state;
 -- seat_dm stop ends it. TppVehicle2 locators (type 50) only: a horse/walker one anchors a stale matrix.
 local function seatDm(a)
  if a[2]=="stop" and #a==2 then
   call(Player,"RequestToStopDirectMotion")
   if A.receipt("return 'MGS_COOP_PARTNER_SEAT_V1' -- stop")~=true then return false,"seat_dm_stop_refused" end
   return true,"seat_dm=stop"
  end
  if #a~=4 or type(a[2])~="string" or not a[2]:match("^state%w+$") or #a[2]>64 or a[2]=="stateDirectPlayRideRearLeftSeatOnVehicle" then return false,"seat_dm_args" end
  if not a[3]:match("^[0-3]$") or not name(a[4]) then return false,"seat_dm_args" end
  local c,why=context();if not c then return false,why end
  if c.mission~=30010 and c.mission~=30020 then return false,"seat_dm_mission" end
  local seat=tonumber(a[3])
  local vid=call(GameObject,"GetGameObjectId","TppVehicle2",a[4])
  if not integer(vid,0,65534) or math.floor(vid/512)~=50 then return false,"seat_dm_locator" end
  if A.receipt(fmt("return 'MGS_COOP_PARTNER_SEAT_V1' -- arm %d %d",seat,vid))~=true then return false,"seat_dm_refused" end
  call(Player,"RequestToPlayDirectMotion",{{"coopRide",1},{a[2],true,a[4],"CNP_ppos_c","MTP_GLOBAL_C",true}})
  return true,fmt("seat_dm;id=%d;seat=%d;name=%s;state=%s",vid,seat,a[4],a[2])
 end
 -- 0.13.6 online-vehicle-gone (lua/coop/world_vehicle.lua): vehicle_remove nearest [sync|fulton], the
 -- lab lever (this copy only, synced as a lab loss, or the engine's own vehicle Fulton watched like a
 -- local one; native and Lua refuse it outside mode=lab), and the read-only vehicle_gone_status.
 local function gone(a)
  local G=get(get(e,"MgsCoopWorld"),"vehicleGone")
  if type(G)~="table" or type(G.lever)~="function" then return false,"vehicle_gone_unavailable" end
  if a[1]=="vehicle_gone_status" and #a==1 then return true,G.status() end
  if a[1]=="vehicle_remove" and a[2]=="nearest" and (#a==2 or #a==3 and (a[3]=="sync" or a[3]=="fulton")) then return G.lever(a[3]~=nil,a[3]=="fulton") end
  return false,"vehicle_args"
 end
 -- M4 (world_vehicle_state.lua checks the arguments): vehicle_push, vehicle_life (lab only), vehicle_state_status.
 local function state(a)
  local S=get(get(e,"MgsCoopWorld"),"vehicleState")
  if type(S)~="table" or type(S.lever)~="function" then return false,"vehicle_state_unavailable" end
  return S.lever(a)
 end
 -- G4 truck test levers (docs/native-puppet.md "Truck test levers"; lab.ini debug_vehicle_door=1, lab only).
 -- door(): this copy's stock door spot of one vehicle (mm, mrad); door 0 is the driver's, 1 the passenger's.
 local DOOR={driver=0,passenger=1}
 local function door(target,seat,cm)
  local r=A.receipt(fmt("return 'MGS_COOP_VEHICLE_DOOR_V1' -- %s %d %d",target,seat,cm))
  if type(r)~="table" then return nil,"door_receipt" end
  if r.ok~=true then return nil,"door_"..(name(r.reason) and #r.reason<=24 and r.reason or "refused") end
  for _,k in ipairs({"id","pseat","type","seats","rider0","x","y","z","face","dx","dy","dz","ax","az"})do
   if not integer(r[k],-1000000000,1000000000) then return nil,"door_answer" end
  end
  return r
 end
 -- a[i..]: [driver|passenger] [cm 0..150] (default 60 cm outside the door point).
 local function doorArgs(a,i,seat)
  local cm=60
  if DOOR[a[i]] then seat=DOOR[a[i]];i=i+1 end
  if a[i] then
   if not a[i]:match("^%d%d?%d?$") or tonumber(a[i])>150 then return nil end
   cm=tonumber(a[i]);i=i+1
  end
  if a[i]~=nil then return nil end
  return seat,cm
 end
 local function goDoor(r,id,text)
  local ok,msg=A.warp({r.x/1000,r.y/1000,r.z/1000,math.deg(r.face/1000)},id)
  local W=get(e,"MgsCoopWorld")
  if type(W)=="table" and type(W.log)=="function" then pcall(W.log,text) end
  return ok,msg..";"..text
 end
 -- nil: the key is off and no door argument was given (debug_player.lua keeps the old 1.9 m spot).
 function V.gotoTruck(a,id)
  local seat,cm=doorArgs(a,3,0)
  if not seat then return false,"goto_vehicle_args" end
  local me,free,any=A.pose(),nil,nil
  for k=0,5 do
   local nm=fmt("veh_trc_%04d",k)
   local okI,vid=pcall(GameObject.GetGameObjectId,"TppVehicle2",nm)
   if okI and integer(vid,0,65534) then
    local okR,riders=pcall(GameObject.SendCommand,vid,{id="GetRiderId"})
    local okP,pos=pcall(GameObject.SendCommand,vid,{id="GetPosition"})
    local okX,x,z=false,nil,nil
    if okP and pos~=nil then okX,x,z=pcall(function()return pos:GetX(),pos:GetZ()end) end
    if okX and A.finite(x) and A.finite(z) then
     local c={vid,(x-me[1])^2+(z-me[3])^2,nm}
     if not any or c[2]<any[2] then any=c end
     -- a driver seat taken (an AI driver) never offers the stock ride icon
     if not (okR and type(riders)=="table" and integer(riders[1],0,65534)) and (not free or c[2]<free[2]) then free=c end
    end
   end
  end
  local pick=free or any
  if not pick then
   if #a==2 then return nil end
   return false,"vehicle_none"
  end
  local best,bestD,bestName=pick[1],pick[2],pick[3]
  local r,why=door(best,seat,cm)
  if not r then
   if why=="door_off" and #a==2 then return nil end
   return false,why
  end
  if not free then return false,fmt("vehicle_driver_taken;id=%d;rider0=%d",best,r.rider0) end
  V.last={tostring(best),seat}
  return goDoor(r,id,fmt("goto_vehicle;name=%s;id=%d;dist=%.0f;door=%d;type=%d;rider0=%d;out=%d",bestName,best,math.sqrt(bestD),seat,r.type,r.rider0,cm))
 end
 local function gotoPartner(a,id)
  local seat,cm=doorArgs(a,2,1)
  if not seat then return false,"goto_partner_vehicle_args" end
  local r,why=door("partner",seat,cm)
  if not r then return false,why end
  V.last={"partner",seat}
  local f=get(get(get(e,"MgsCoopWorld"),"ridePose"),"fast")
  local t=type(f)=="table" and type(f.tracks)=="table" and f.tracks[r.id]~=nil
  return goDoor(r,id,fmt("goto_partner_vehicle;id=%d;pseat=%d;door=%d;type=%d;rider0=%d;fast=%d;out=%d",r.id,r.pseat,seat,r.type,r.rider0,t and 1 or 0,cm))
 end
 -- vehicle_door [partner|veh_<name> [driver|passenger]] (no argument: the last goto's vehicle and door;
 -- no door: passenger for partner, as goto_partner_vehicle, else driver).
 local function doorReport(a)
  local target,seat
  if #a==1 then
   if not V.last then return false,"vehicle_door_args" end
   target,seat=V.last[1],V.last[2]
  else
   if #a>3 or (a[3] and DOOR[a[3]]==nil) then return false,"vehicle_door_args" end
   seat=DOOR[a[3] or (a[2]=="partner" and "passenger" or "driver")]
   if a[2]=="partner" then target="partner"
   elseif name(a[2]) and a[2]:match("^veh_") then
    local vid=call(GameObject,"GetGameObjectId","TppVehicle2",a[2])
    if not integer(vid,0,65534) then return false,"vehicle_door_locator" end
    target=tostring(vid)
   else return false,"vehicle_door_args" end
  end
  local r,why=door(target,seat,0)
  if not r then return false,why end
  local p=A.pose()
  local dx,dy,dz=p[1]-r.dx/1000,p[2]-r.dy/1000,p[3]-r.dz/1000
  local d3,dot=math.sqrt(dx*dx+dy*dy+dz*dz),(dx*r.ax+dz*r.az)/1000
  return true,fmt("vehicle_door;id=%d;type=%d;seat=%d;rider0=%d;gate=%s;d3=%.2f;dot=%.2f;dy=%.2f",r.id,r.type,seat,r.rider0,
   d3>=1.4 and "far" or dot<0 and "inside" or "pass",d3,dot,dy)
 end
 -- M3 experiments E2/E17/E18 (lua/coop/world_relief.lua W.relief): the sortie vehicle census and lab levers.
 local function relief(a)
  local R=get(get(e,"MgsCoopWorld"),"relief")
  if type(R)~="table" or type(R.run)~="function" then return false,"relief_unavailable" end
  return R.run(a)
 end
 function V.run(a,id)
  local f=(a[1]:sub(1,7)=="relief_" or a[1]=="vehicle_census") and relief or a[1]=="vehicle_names" and names or a[1]=="seat_dm" and seatDm or a[1]=="goto_partner_vehicle" and gotoPartner or
   a[1]=="vehicle_door" and doorReport or (a[1]=="vehicle_remove" or a[1]=="vehicle_gone_status") and gone or
   (a[1]=="vehicle_push" or a[1]=="vehicle_life" or a[1]=="vehicle_state_status") and state or read
  local ok,yes,why=pcall(f,a,id)
  if not ok then return false,"vehicle_binding" end
  return yes,why
 end
end
