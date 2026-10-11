-- Original read-only vehicle seat census on the natural paired Lua owner.
-- Exact retail GetRiderId returns native seat IDs, never inferred peer riders.
do
 local D,A=e.MgsCoopDebug,e.MgsCoopDebug.api
 local get,call,integer,fmt=A.get,A.call,A.integer,string.format
 local V={handles={vehicle_seats=true}};D.vehicle=V
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
 function V.run(a,id)
  local ok,yes,why=pcall(read,a)
  if not ok then return false,"vehicle_binding" end
  return yes,why
 end
end
