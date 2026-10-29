-- Original normal-release D-Horse test controls on the existing Lua owner.
-- Stock CallBuddy(BuddyType.HORSE, Vector3, radians), never force mount or
-- per-instance Horse commands. No index-1 read without the research census.
-- walker_spawn / walker_status: the same for D-Walker (WALKER_GEAR).
do
 local D,A=e.MgsCoopDebug,e.MgsCoopDebug.api
 local get,call,fmt=A.get,A.call,string.format
 local S={handles={horse_spawn=true,horse_status=true,walker_spawn=true,walker_status=true,cargo_status=true,cargo_test=true,sleep_clock=true,
  seed_buddy=true,companion_status=true,companion_cell=true,companion_lab=true},nextCall=0};D.buddy=S
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
  return fmt("horse;selected=%s;available=%d;actor0=%d;localMounted=%d;nativeCount=unverified;remoteRider=unverified;arrival=%s;retry=%s",r.name,r.exists and 1 or 0,r.actor,r.mounted and 1 or 0,
   r.kind==1 and r.exists and r.mounted and "mounted" or "unverified",r.mounted and "none" or "Recall")
 end
 -- Without a deployed buddy the buddy block is not loaded and CallBuddy has
 -- nothing to call. Deploy D-Horse the way the retail free-roam scripts do:
 -- reserve the horse at its spawn point, then load the block.
 local function deployHorse(id)
  local W=get(e,"MgsCoopWorld");local api=get(W,"api")
  local reserve,load=get(TppBuddy2BlockController,"ReserveCallBuddy"),get(TppBuddy2BlockController,"Load")
  local horse,normal=get(BuddyType,"HORSE"),get(BuddyInitStatus,"NORMAL")
  if type(reserve)~="function" or type(load)~="function" or not A.integer(horse,0,255) or not A.integer(normal,0,255) or
   type(get(api,"buddyVector"))~="function" or type(get(W,"now"))~="function" then return false,"buddy_block_inactive" end
  local can=get(TppBuddyService,"CanSortieBuddyType")
  if type(can)=="function" then
   local ok,yes=pcall(can,horse)
   if ok and yes==false then return false,"horse_not_unlocked" end
  end
  local down=get(W,"downed")
  if get(down,"maskOwned") or get(down,"nativePending") then return false,"player_downed" end
  local now=W.now()
  if not A.finite(now) or now<0 then return false,"buddy_clock" end
  if now<S.nextCall then return false,"horse_cooldown" end
  local p=A.pose();local rad=((p[4]+180)%360-180)*math.pi/180
  local x,y,z=p[1]+4*math.cos(rad),p[2],p[3]-4*math.sin(rad)
  if not A.finite(x) or not A.finite(y) or not A.finite(z) or math.abs(x)>999996 or math.abs(y)>999996 or math.abs(z)>999996 then return false,"horse_position" end
  local made,v=pcall(api.buddyVector,x,y,z)
  if not made or not v then return false,"horse_vector" end
  S.nextCall=now+2000
  if not pcall(reserve,horse,normal,v,rad) then return false,"horse_reserve_refused" end
  if not pcall(load) then return false,"horse_block_load_refused" end
  pcall(function() vars.buddyType=horse end)
  local q={kind="spawn",id=id,mission=D.mission,frames=0}
  q.poll=function()
   local current,owner,why=context()
   if not current and why=="buddy_block_inactive" and q.frames<900 then return end
   if not current then D.pending=nil;A.note(id,false,why or "buddy_context_changed");return end
   local s,why=sample(current)
   if not s then D.pending=nil;A.note(id,false,why);return end
   if s.kind==1 and s.exists then D.pending=nil;A.note(id,true,"horse_deployed;"..report(s))
   elseif q.frames>=900 then D.pending=nil;A.note(id,false,"horse_load_timeout;"..report(s)) end
  end
  D.pending=q
  return true,"submitted_horse_deploy;block=loading;arrival=unverified;close_F9;mount=normal_action"
 end
 -- D-Walker lab lever for the mount hide (docs/mounts.md): the same stock
 -- CallBuddy with BuddyType.WALKER_GEAR four metres to the right, the horse
 -- lever's guards and dog yield; an active buddy block only, never a mount.
 -- Refused while the local player rides: ON_HORSE / ON_VEHICLE, or the gait
 -- statuses PlayerInfo 0x1412CE870 sets for a horse ride (0x79) and a D-Walker
 -- drive (0x7D, no ON_*). The get-on / leave acts 122/123/126 set none.
 local MOUNTED={"ON_HORSE","ON_VEHICLE","HORSE_IDLE","HORSE_TROT","HORSE_CANTER","HORSE_GALLOP"}
 local function walkerSample(W)
  local a=W.api
  if not A.integer(a.buddyWalker,0,255) or type(a.buddyType)~="function" then return nil,"walker_binding" end
  local t=a.buddyType()
  if not A.integer(t,0,4) then return nil,"buddy_type" end
  local ids={}
  for i,n in ipairs(MOUNTED) do ids[i]=get(PlayerStatus,n);if not A.integer(ids[i],0,65535) then return nil,"walker_posture_binding" end end
  local mounted=call(PlayerInfo,"OrCheckStatus",ids)
  if type(mounted)~="boolean" then return nil,"walker_posture" end
  local exists=call(GameObject,"DoesGameObjectExistWithTypeName","TppWalkerGear2")
  if type(exists)~="boolean" then return nil,"walker_availability" end
  local actor=65535
  if t==a.buddyWalker and exists then
   if type(a.buddyId)~="function" then return nil,"walker_id_binding" end
   actor=a.buddyId("TppWalkerGear2",0)
   if actor~=22528 then return nil,fmt("walker_actor0;id=%d",A.integer(actor,0,65535) and actor or -1) end
  end
  return {walker=t==a.buddyWalker,name=({[0]="none",[1]="horse",[2]="dog",[3]="quiet",[4]="walker"})[t],mounted=mounted,exists=exists,actor=actor}
 end
 local function walkerReport(r)
  return fmt("walker;selected=%s;available=%d;actor0=%d;localMounted=%d;arrival=unverified",r.name,r.exists and 1 or 0,r.actor,r.mounted and 1 or 0)
 end
 local function walker(cmd,id)
  local W,B,why=context()
  if not W then return false,why=="buddy_block_inactive" and "walker_block_inactive" or why end
  local r,why=walkerSample(W);if not r then return false,why end
  if cmd=="walker_status" then return true,walkerReport(r) end
  if r.mounted then return false,"player_mounted" end
  local down=get(W,"downed")
  if get(down,"maskOwned") or get(down,"nativePending") then return false,"player_downed" end
  if type(W.now)~="function" then return false,"buddy_clock" end
  local now=W.now()
  if not A.finite(now) or now<0 then return false,"buddy_clock" end
  if now<S.nextCall then return false,"walker_cooldown" end
  local api=W.api
  if type(api.buddyCall)~="function" or type(api.buddyVector)~="function" then return false,"walker_binding" end
  local p=A.pose();local rad=((p[4]+180)%360-180)*math.pi/180
  local x,y,z=p[1]+4*math.cos(rad),p[2],p[3]-4*math.sin(rad)
  if not A.finite(x) or not A.finite(y) or not A.finite(z) or math.abs(x)>999996 or math.abs(y)>999996 or math.abs(z)>999996 then return false,"walker_position" end
  local made,v=pcall(api.buddyVector,x,y,z)
  local read,vx,vy,vz=pcall(function()return v:GetX(),v:GetY(),v:GetZ()end)
  if not made or not read or not A.finite(vx) or not A.finite(vy) or not A.finite(vz) or
   math.abs(vx-x)>.001 or math.abs(vy-y)>.001 or math.abs(vz-z)>.001 then return false,"walker_vector" end
  local current,owner,why=context()
  if current~=W or owner~=B then return false,why or "buddy_context_changed" end
  local fresh,why=walkerSample(W)
  if not fresh or fresh.name~=r.name or fresh.mounted or fresh.exists~=r.exists or fresh.actor~=r.actor then return false,why or "buddy_context_changed" end
  local owned=B.s and B.s.owned;local yielded=false
  if owned then
   if type(B.yieldForRide)~="function" then return false,"dog_borrowed" end
   local ok,result=pcall(B.yieldForRide)
   if not ok or result==false or (B.s and B.s.owned) then return false,"dog_borrowed" end
   yielded=true
  end
  S.nextCall=now+2000
  local ok,result=pcall(api.buddyCall,api.buddyWalker,v,rad)
  if not ok or result~=true then
   if yielded and type(B.resumeFollow)=="function" then pcall(B.resumeFollow) end
   return false,"walker_call_refused"
  end
  if yielded and type(B.keepRideYield)=="function" then pcall(B.keepRideYield) end
  local q={kind="spawn",id=id,mission=D.mission,frames=0}
  q.poll=function()
   local current,owner,why=context()
   if not current and why=="buddy_block_inactive" and get(e,"MgsCoopWorld")==W and get(W,"buddy")==B and q.frames<600 then return end
   if current~=W or owner~=B then D.pending=nil;A.note(id,false,why or "buddy_context_changed");return end
   local s,why=walkerSample(W)
   if not s then D.pending=nil;A.note(id,false,why);return end
   if s.walker and s.exists then D.pending=nil;A.note(id,true,"walker_selected;"..walkerReport(s))
   elseif q.frames>=600 then D.pending=nil;A.note(id,false,"walker_load_timeout;"..walkerReport(s)) end
  end
  D.pending=q
  return true,"submitted_walker;scope=local;stock_loader;arrival=unverified;mount=normal_action"
 end
 -- M1 (lua/coop/world_companion.lua): companion_status, seed_buddy <kind> (lab), companion_cell [goto]
 -- (the Mother Base Quiet cell, E20); only the host summons (F9): the guest's is refused with a toast.
 local function companion() local W=get(e,"MgsCoopWorld");return W,get(W,"companion") end
 function S.run(a,id)
  local cmd=a[1]
  if cmd=="companion_status" or cmd=="seed_buddy" or cmd=="companion_cell" or cmd=="companion_lab" then
   local _,C=companion()
   if type(get(C,"lever"))~="function" then return false,"companion_unavailable" end
   return C.lever(a,id,A,D.mission)
  end
  if cmd=="horse_spawn" or cmd=="walker_spawn" then
   local W,C=companion()
   if type(get(C,"live"))=="function" and C.live()==true and get(W,"host")~=true and type(get(C,"refuse"))=="function" then
    pcall(C.refuse,"f9_"..cmd,nil);return false,"host_only;companion=host_owned"
   end
  end
  if cmd=="cargo_test" and #a==2 and (a[2]=="0" or a[2]=="1") then
   local C=get(get(e,"MgsCoopWorld"),"horseCargo")
   if type(get(C,"setTesting"))~="function" then return false,"cargo_unavailable" end
   local ok,yes=pcall(C.setTesting,a[2]=="1")
   return ok and yes==true,ok and "cargo;testing="..a[2]..";attachment=offset" or "cargo_test_refused"
  end
  if cmd=="sleep_clock" and #a==1 then
   local d=get(get(e,"MgsCoopWorld"),"downed")
   if type(get(d,"sleepClockStatus"))~="function" then return false,"sleep_clock_unavailable" end
   local ok,line=pcall(d.sleepClockStatus)
   return ok and type(line)=="string" and #line<=160,ok and line or "sleep_clock_refused"
  end
  if cmd=="cargo_status" and #a==1 then
   local C=get(get(e,"MgsCoopWorld"),"horseCargo")
   if type(get(C,"status"))~="function" then return false,"cargo_unavailable" end
   local ok,line=pcall(C.status)
   return ok and type(line)=="string" and #line<=160,ok and line or "cargo_status_refused"
  end
  if (cmd=="walker_spawn" or cmd=="walker_status") and #a==1 then return walker(cmd,id) end
  if #a~=1 or (cmd~="horse_spawn" and cmd~="horse_status") then return false,"horse_args" end
  local W,B,why=context()
  if not W and why=="buddy_block_inactive" and cmd=="horse_spawn" then return deployHorse(id) end
  if not W then return false,why end
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
  -- Repeating this stock request on the selected horse asks it to recall;
  -- native pending requests refuse the call before changing selection.
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
  S.lastGoal={x,y,z}
  local ok,result=pcall(api.buddyCall,1,v,rad)
  if not ok or result~=true then
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
   -- Active type is set before the engine finishes its pack wait. This
   -- result confirms selected type only; it never declares nearby arrival.
   if current.kind==1 and current.exists then
    D.pending=nil;A.note(id,true,"horse_selected;"..report(current))
   elseif q.frames>=600 then D.pending=nil;A.note(id,false,"horse_load_timeout;"..report(current)) end
  end
  D.pending=q
  return true,r.kind==1 and "submitted_horse_recall;arrival=unverified;close_F9;mount=normal_action" or "submitted_horse;scope=local;stock_loader;arrival=unverified;close_F9;mount=normal_action"
 end
end
