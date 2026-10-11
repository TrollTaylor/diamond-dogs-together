-- Original normal-release D-Horse test controls on the existing Lua owner.
-- Stock CallBuddy(BuddyType.HORSE, Vector3, radians), never force mount or
-- per-instance Horse commands. No index-1 read without the research census.
do
 local D,A=e.MgsCoopDebug,e.MgsCoopDebug.api
 local get,call,fmt=A.get,A.call,string.format
 local S={handles={horse_spawn=true,horse_status=true},nextCall=0};D.buddy=S
 function S.stop()S.nextCall=0 end
 local function context()
  local W=get(e,"MgsCoopWorld");local B=get(W,"buddy")
  if type(get(B,"guard"))~="function" or type(get(W,"api"))~="table" then return nil,nil,"buddy_unavailable" end
  local ok,yes,why=pcall(B.guard,true)
  if not ok or yes~=true then return nil,nil,type(why)=="string" and why or "buddy_context" end
  return W,B
 end
 local function sample(W)
  local a=W.api
  if a.buddyHorse~=1 or type(a.buddyType)~="function" then return nil,"horse_binding" end
  local t=a.buddyType()
  if not A.integer(t,0,4) then return nil,"buddy_type" end
  local name=({[0]="none",[1]="horse",[2]="dog",[3]="quiet",[4]="walker"})[t]
  local horse=get(PlayerStatus,"ON_HORSE")
  if not A.integer(horse,0,65535) then return nil,"horse_posture_binding" end
  local mounted=call(PlayerInfo,"OrCheckStatus",{horse})
  if type(mounted)~="boolean" then return nil,"horse_posture" end
  -- Class existence is read-only; it is not a realized count or seat receipt.
  local exists=call(GameObject,"DoesGameObjectExistWithTypeName","TppHorse2")
  if type(exists)~="boolean" then return nil,"horse_availability" end
  local actor=65535
  if t==1 and exists then
   if type(a.buddyId)~="function" then return nil,"horse_id_binding" end
   actor=a.buddyId("TppHorse2",0)
   if actor~=6656 then return nil,fmt("horse_actor0;id=%d",A.integer(actor,0,65535) and actor or -1) end
   if a.buddyType()~=1 or call(GameObject,"DoesGameObjectExistWithTypeName","TppHorse2")~=true then return nil,"horse_changed" end
  end
  return {kind=t,name=name,mounted=mounted,exists=exists,actor=actor}
 end
 local function report(r)
  return fmt("horse;selected=%s;available=%d;actor0=%d;localMounted=%d;nativeCount=unverified;remoteRider=unverified",r.name,r.exists and 1 or 0,r.actor,r.mounted and 1 or 0)
 end
 function S.run(a,id)
  local cmd=a[1]
  if #a~=1 or (cmd~="horse_spawn" and cmd~="horse_status") then return false,"horse_args" end
  local W,B,why=context();if not W then return false,why end
  local r,why=sample(W);if not r then return false,why end
  if cmd=="horse_status" then return true,report(r) end
  if r.mounted then return false,"horse_already_mounted" end
  local down=get(W,"downed")
  if get(down,"maskOwned") or get(down,"nativePending") then return false,"player_downed" end
  if type(W.now)~="function" then return false,"buddy_clock" end
  local now=W.now()
  if not A.finite(now) or now<0 then return false,"buddy_clock" end
  if now<S.nextCall then return false,"horse_cooldown" end
  local api=W.api
  if type(api.buddyCall)~="function" or type(api.buddyVector)~="function" then return false,"horse_binding" end
  local p=A.pose();local rad=((p[4]+180)%360-180)*math.pi/180
  -- Four metres to the right of the player; retain the exact ground height.
  local x,y,z=p[1]+4*math.cos(rad),p[2],p[3]-4*math.sin(rad)
  if not A.finite(x) or not A.finite(y) or not A.finite(z) or math.abs(x)>999996 or math.abs(y)>999996 or math.abs(z)>999996 then return false,"horse_position" end
  local made,v=pcall(api.buddyVector,x,y,z)
  local read,vx,vy,vz=pcall(function()return v:GetX(),v:GetY(),v:GetZ()end)
  if not made or not read or not A.finite(vx) or not A.finite(vy) or not A.finite(vz) or
   math.abs(vx-x)>.001 or math.abs(vy-y)>.001 or math.abs(vz-z)>.001 then return false,"horse_vector" end
  local current,owner,why=context()
  if current~=W or owner~=B then return false,why or "buddy_context_changed" end
  local fresh,why=sample(W)
  if not fresh or fresh.kind~=r.kind or fresh.mounted or fresh.exists~=r.exists or fresh.actor~=r.actor then return false,why or "buddy_context_changed" end
  local owned=B.s and B.s.owned;local yielded=false
  if owned then
   if type(B.yieldForRide)~="function" then return false,"dog_borrowed" end
   local ok,result=pcall(B.yieldForRide)
   if not ok or result==false or (B.s and B.s.owned) then return false,"dog_borrowed" end
   yielded=true
  end
  S.nextCall=now+2000
  local ok,result=pcall(api.buddyCall,1,v,rad)
  if not ok or result==false then
   if yielded and type(B.resumeFollow)=="function" then pcall(B.resumeFollow) end
   return false,"horse_call_refused"
  end
  if yielded and type(B.keepRideYield)=="function" then pcall(B.keepRideYield) end
  local q={kind="spawn",id=id,mission=D.mission,frames=0}
  q.poll=function()
   local current,owner,why=context()
   if not current and why=="buddy_block_inactive" and get(e,"MgsCoopWorld")==W and get(W,"buddy")==B and q.frames<600 then return end
   if current~=W or owner~=B then D.pending=nil;A.note(id,false,why or "buddy_context_changed");return end
   local current,why=sample(W)
   if not current then D.pending=nil;A.note(id,false,why);return end
   if current.kind==1 and current.exists then
    D.pending=nil;A.note(id,true,"horse_selected;"..report(current)..";render_unverified")
   elseif q.frames>=600 then D.pending=nil;A.note(id,false,"horse_load_timeout;"..report(current)) end
  end
  D.pending=q
  return true,"submitted_horse;scope=local;stock_loader;mount=normal_action"
 end
end
