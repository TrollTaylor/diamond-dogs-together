-- Original F9 character switch (docs/characters.md "Switch in game"): your own character,
-- in place now and at every later mission load of this game session (Tpp.MgsCoop
-- .characterPick, read by character.lua). The game's own temporary-type gvars keep your
-- save's own character (the next mission without an override restores it). The partner
-- sees the new look through the existing look frames (partner_look); nothing is sent here.
do
 local D,A=e.MgsCoopDebug,e.MgsCoopDebug.api
 local get,call,fmt=A.get,A.call,string.format
 local S={handles={character=true}};D.character=S
 local KEY={snake=1,dd_male=1,dd_female=1,avatar=1,ocelot=1,quiet=1}
 -- coop.ini characters that let the native Ocelot/Quiet bypass on (character_receipt).
 local BYPASS={ocelot=1,quiet=1,story=1}
 -- Pure: the character a press asks for from the button and coop.ini's character.
 -- -> target (a name, "story" or "own") or nil and a refusal.
 function S.Resolve(name,configured)
  if name=="default" then
   if KEY[configured] or configured=="story" then return configured end
   return "own"
  end
  if not KEY[name] then return nil,"character_args" end
  if (name=="ocelot" or name=="quiet") and not BYPASS[configured] then return nil,"character_needs_launcher_pick" end
  return name
 end
 local function coop()return get(Tpp,"MgsCoop")end
 -- The lab readback compares against characterStatus.expect: its change line then shows the native type.
 local function expect(t)local st=get(coop(),"characterStatus");if type(st)=="table" then st.expect=t end end
 local function report(C,kind,id)
  pcall(C.Log,"REPORT",fmt("switch;mission=%s;id=%.0f;pick=%s;result=%s;type=%s;parts=%s;camo=%s;face=%s",tostring(get(vars,"missionCode")),id,
   tostring(get(coop(),"characterPick")or"default"),kind,tostring(get(vars,"playerType")),tostring(get(vars,"playerPartsType")),
   tostring(get(vars,"playerCamoType")),tostring(get(vars,"playerFaceId"))))
 end
 -- Waits for the engine's parts reload (PlayerStatus.PARTS_ACTIVE, as s10151's mid-mission
 -- swap does) before the final result; the vars must still hold the new type.
 local function wait(C,want,id)
  if not want then return end
  D.pending={kind="character",id=id,mission=D.mission,frames=0,poll=function()
   local q=D.pending
   if get(vars,"playerType")~=want then D.pending=nil;report(C,"reverted",q.id);A.note(q.id,false,"character_reverted;type="..tostring(get(vars,"playerType")));return end
   local active=get(PlayerStatus,"PARTS_ACTIVE")
   local ready=type(active)=="number" and call(PlayerInfo,"OrCheckStatus",{active})==true
   if q.frames>=30 and (ready or type(active)~="number") then
    D.pending=nil;report(C,"ready",q.id)
    A.note(q.id,true,fmt("character_ready;type=%d;parts=%s",want,ready and "active" or "unverified"))
   elseif q.frames>=900 then D.pending=nil;report(C,"timeout",q.id);A.note(q.id,false,"character_parts_timeout") end
  end}
 end
 function S.run(a,id)
  if #a~=2 then return false,"character_args" end
  local M=coop();local C=get(M,"Character")
  if type(M)~="table" or type(get(C,"Plan"))~="function" or type(get(C,"Enums"))~="function" then return false,"character_unavailable" end
  local configured=A.receipt("return 'MGS_COOP_CHARACTER_V1'")
  if type(configured)~="string" or configured=="MGS_COOP_CHARACTER_V1" then configured="" end
  local target,why=S.Resolve(a[2],configured)
  if not target then return false,why end
  -- The pick holds for later mission loads ("default" hands back to coop.ini); stored
  -- only once the switch went through (a throwing binding leaves none).
  local function done(kind,r,want)
   M.characterPick=a[2]~="default" and a[2] or nil
   report(C,kind,id);wait(C,want,id);return true,r
  end
  local x=C.Gather(target)
  if target=="story" then target=C.Story(x.quiet) end
  -- The Quiet rules win in place as at load (C.Decide).
  if target=="quiet" and (C.QUIET_CONFLICT[x.mission] or x.buddyQuiet) then
   return false,"character_quiet_unavailable;reason="..(C.QUIET_CONFLICT[x.mission] and "quiet_conflict" or "buddy_quiet")
  end
  -- The bypass is switched on only while a mission loads: else applied at the next load.
  local function off()return A.receipt("return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'")~=true end
  if target=="own" then
   if get(gvars,"ply_isUsingTempPlayerType")~=true then return done("kept","character_kept;own") end
   local own,E=get(gvars,"ply_lastPlayerTypeUsingTemp"),C.Enums().PlayerType
   if (own==E.OCELOT or own==E.QUIET) and off() then return done("next_load","character_next_load;own") end
   call(TppPlayer,"RestoreTemporaryPlayerType")
   expect(get(vars,"playerType"))
   return done("switched","character_switched;own",get(vars,"playerType"))
  end
  if (target=="ocelot" or target=="quiet") and off() then return done("next_load","character_next_load;"..target) end
  local temp,face=C.Plan(target,{sortie=x.sortie},C.Enums())
  if vars.playerType==temp.playerType and vars.playerPartsType==temp.partsType then return done("kept","character_kept;"..target) end
  -- Your own appearance is kept once (the game's SaveCurrentPlayerType), then the vars the
  -- type setting writes; the engine reloads the parts from them.
  call(TppPlayer,"SaveCurrentPlayerType")
  vars.playerPartsType,vars.playerCamoType,vars.playerFaceEquipId=temp.partsType,temp.camoType,temp.faceEquipId
  if temp.handEquip then vars.handEquip=temp.handEquip end
  if face then vars.playerFaceId=face end
  vars.playerType=temp.playerType
  expect(temp.playerType)
  return done("switched","character_switched;"..target,temp.playerType)
 end
 -- FM1 watch lever (docs/world-sync-design.md 7.15): this copy's camera toward the partner's character (1.1 m up, or the
 -- metres given) through the stock Player.RequestToSetCameraRotation at any distance (enemy_aim_nearest stops at 60 m).
 local function aim(a)
  local up=#a==1 and 1.1 or #a==2 and tonumber(a[2])
  if type(up)~="number" or up<0 or up>2 then return false,"aim_args" end
  local pp=D.partnerPose
  if type(pp)~="table" or type(pp[1])~="number" or type(pp[2])~="number" or type(pp[3])~="number" then return false,"partner_pose_unavailable" end
  if type(get(Player,"RequestToSetCameraRotation"))~="function" then return false,"camera_unavailable" end
  local p=A.pose()
  local dx,dz,dy=pp[1]/1000-p[1],pp[3]/1000-p[3],(pp[2]/1000+up)-(p[2]+1.5)
  local d=math.sqrt(dx*dx+dz*dz)
  local yaw,pitch=math.deg(math.atan2(dx,dz)),math.deg(math.atan2(-dy,d))
  call(Player,"RequestToSetCameraRotation",{rotX=pitch,rotY=yaw,interpTime=0.2})
  return true,fmt("aim_partner;d=%.1f;rotY=%.1f;rotX=%.1f;age_ms=%s",d,yaw,pitch,tostring(pp[4]))
 end
 -- Merge-friendly dispatch: debug_menu.lua routes D.player's handles to D.player.run.
 local P=D.player
 if type(P)=="table" and type(P.handles)=="table" and type(P.run)=="function" then
  local prior=P.run
  P.handles.character=true;P.handles.aim_partner=true
  P.run=function(a,id)
   if a[1]=="character" then return S.run(a,id)end
   if a[1]=="aim_partner" then return aim(a)end
   return prior(a,id)
  end
 end
end
