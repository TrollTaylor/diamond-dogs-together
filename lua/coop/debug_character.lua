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
 local function report(C,kind,id)
  pcall(C.Log,"REPORT",fmt("switch;mission=%s;id=%.0f;pick=%s;result=%s;type=%s;parts=%s;camo=%s;face=%s",tostring(get(vars,"missionCode")),id,
   tostring(get(coop(),"characterPick")or"default"),kind,tostring(get(vars,"playerType")),tostring(get(vars,"playerPartsType")),
   tostring(get(vars,"playerCamoType")),tostring(get(vars,"playerFaceId"))))
 end
 -- Waits for the engine's parts reload (PlayerStatus.PARTS_ACTIVE, as s10151's mid-mission
 -- swap does) before the final result; the vars must still hold the new type.
 local function wait(C,want,id)
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
  -- The pick holds for later mission loads; "default" hands back to coop.ini.
  M.characterPick=a[2]~="default" and a[2] or nil
  local x=C.Gather(target)
  if target=="story" then target=C.Story(x.quiet) end
  if target=="own" then
   if get(gvars,"ply_isUsingTempPlayerType")~=true then report(C,"kept",id);return true,"character_kept;own" end
   call(TppPlayer,"RestoreTemporaryPlayerType")
   report(C,"switched",id);wait(C,get(vars,"playerType"),id)
   return true,"character_switched;own"
  end
  if (target=="ocelot" or target=="quiet") and A.receipt("return 'MGS_COOP_CHARACTER_BYPASS_ON_V1'")~=true then
   -- The bypass is switched on only while a mission loads: applied at the next load.
   report(C,"next_load",id);return true,"character_next_load;"..target
  end
  local temp,face=C.Plan(target,{sortie=x.sortie},C.Enums())
  if vars.playerType==temp.playerType and vars.playerPartsType==temp.partsType then report(C,"kept",id);return true,"character_kept;"..target end
  -- Your own appearance is kept once (the game's SaveCurrentPlayerType), then the vars the
  -- type setting writes; the engine reloads the parts from them.
  call(TppPlayer,"SaveCurrentPlayerType")
  vars.playerPartsType,vars.playerCamoType,vars.playerFaceEquipId=temp.partsType,temp.camoType,temp.faceEquipId
  if temp.handEquip then vars.handEquip=temp.handEquip end
  if face then vars.playerFaceId=face end
  vars.playerType=temp.playerType
  report(C,"switched",id);wait(C,temp.playerType,id)
  return true,"character_switched;"..target
 end
 -- Merge-friendly dispatch: debug_menu.lua routes D.player's handles to D.player.run.
 local P=D.player
 if type(P)=="table" and type(P.handles)=="table" and type(P.run)=="function" then
  local prior=P.run
  P.handles.character=true
  P.run=function(a,id)if a[1]=="character" then return S.run(a,id)end;return prior(a,id)end
 end
end
