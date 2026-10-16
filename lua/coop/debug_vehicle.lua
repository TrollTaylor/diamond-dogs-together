-- Original read-only vehicle seat census on the natural paired Lua owner.
-- Exact retail GetRiderId returns native seat IDs, never inferred peer riders.
do
 local D,A=e.MgsCoopDebug,e.MgsCoopDebug.api
 local get,call,integer,fmt=A.get,A.call,A.integer,string.format
 local V={handles={vehicle_seats=true,vehicle_names=true,seat_dm=true}};D.vehicle=V
 function V.stop()end -- No native state, borrowed objects or cached IDs.
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
 -- seat_dm <state> <seat 0..3> <locator> arms the four seat dwords on body 1 through
 -- the native seat receipt and plays the seated state there; seat_dm stop ends it.
 -- Only a TppVehicle2 locator (type 50) is accepted: a horse or walker locator
 -- would anchor body 1 to a stale matrix.
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
 function V.run(a,id)
  local f=a[1]=="vehicle_names" and names or a[1]=="seat_dm" and seatDm or read
  local ok,yes,why=pcall(f,a)
  if not ok then return false,"vehicle_binding" end
  return yes,why
 end
end
