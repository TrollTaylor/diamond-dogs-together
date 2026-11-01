-- Game rules (docs/debug-menu.md "Game rules"; lab and play). F9 commands on the player module's dispatch:
--   wormhole 0|1   this player's Fulton: 0 = the balloon again. The wormhole is the item level
--                  EQP_IT_Fulton_WormHole (stock TppMission reads Player.GetItemLevel >= 1); while off it is forced
--                  to 0 in gameplay (again after a reload). The development stays, and a save writes the real level.
--   mb_weapons 0|1 host only, both copies (RULES 0x3E): at Mother Base the stock no-lethal rule of f30050
--                  (vars.playerDisableActionFlag = CQC_KNIFE_KILL + KILLING_WEAPON, HUD EquipHudAll ALL_KILL_NOUSE)
--                  is lifted, as f30050's own Seq_DEBUG_GimmickCheck does. Any other flag is left alone.
--   rules          status.
do
 local D, W = e.MgsCoopDebug, e.MgsCoopWorld
 local P = type(D) == "table" and D.player
 if type(P) == "table" and type(P.handles) == "table" and type(P.run) == "function" and type(D.api) == "table" then
  local A = D.api
  local get, fmt = A.get, string.format
  local R = {wormOff = false, mb = false, seq = 0, tick = 0}
  D.rules = R
  local OP = type(W) == "table" and type(W.OP) == "table" and W.OP or nil
  if OP then OP.RULES = OP.RULES or 62 end
  local function log(s)
   if type(W) == "table" and type(W.log) == "function" then pcall(W.log, "event=rules;" .. s) end
  end
  local function say(text)
   local f = get(TppUiCommand, "AnnounceLogView")
   if type(f) == "function" then pcall(f, text) end
  end
  local function now()
   if type(W) == "table" and type(W.now) == "function" then
    local ok, v = pcall(W.now)
    if ok and type(v) == "number" then return v end
   end
   return R.tick * 33
  end
  local function setVar(k, v) return pcall(function() vars[k] = v end) end
  local function gameplay()
   local f = get(TppMission, "CheckMissionState")
   if type(f) ~= "function" then return false end
   local ok, v = pcall(f)
   return ok and v == true
  end
  -- (A) Wormhole
  local function equip() return get(TppEquip, "EQP_IT_Fulton_WormHole") end
  local function level()
   local f, q = get(Player, "GetItemLevel"), equip()
   if type(f) ~= "function" or type(q) ~= "number" then return nil end
   local ok, v = pcall(f, q)
   if ok and type(v) == "number" then return v end
  end
  local function setLevel(n)
   local f, q = get(Player, "SetItemLevel"), equip()
   if type(f) ~= "function" or type(q) ~= "number" then return false end
   return (pcall(f, q, n))
  end
  -- The stock slot: vars.itemLevels[equip - EQP_IT_InstantStealth] (TppMain's suit line).
  local function slot()
   local q, base = equip(), get(TppEquip, "EQP_IT_InstantStealth")
   if type(q) == "number" and type(base) == "number" then return q - base end
  end
  local function slotLevel(i)
   local v = i and get(get(vars, "itemLevels"), i)
   if type(v) == "number" then return v end
  end
  local function setSlot(i, v) return pcall(function() vars.itemLevels[i] = v end) end
  -- Off: a level >= 1 read here is the game's own (this module only ever writes 0), so it is remembered.
  function R.worm()
   if not R.wormOff or not gameplay() then return end
   local l = level()
   if not l or l < 1 then return end
   local i = slot()
   local before = slotLevel(i)
   R.wormSeen = l
   if not setLevel(0) then log("step=worm_failed;level=" .. l); return end
   -- The save fix writes vars only when this slot followed the change (l -> 0).
   if before == l and slotLevel(i) == 0 then R.slotOk = true end
   log(fmt("step=worm_off;level=%d;now=%s;slot=%d;mission=%s", l, tostring(level()), R.slotOk and 1 or 0, tostring(get(vars, "missionCode"))))
  end
  -- (B) Weapons at Mother Base
  local function mbDefault()
   local a, b = get(PlayerDisableAction, "CQC_KNIFE_KILL"), get(PlayerDisableAction, "KILLING_WEAPON")
   if type(a) == "number" and type(b) == "number" then return a + b end
  end
  local function none()
   local n = get(PlayerDisableAction, "NONE")
   return type(n) == "number" and n or 0
  end
  local function hud(set)
   local f = get(TppUiStatusManager, set and "SetStatus" or "UnsetStatus")
   if type(f) == "function" then pcall(f, "EquipHudAll", "ALL_KILL_NOUSE") end
  end
  -- The guest follows the host's choice (RULES); the host, or a copy without world sync, its own.
  function R.want()
   if type(W) == "table" and W.host == false then return R.hostMb == true end
   return R.mb == true
  end
  function R.mbTick()
   local def = mbDefault()
   if get(vars, "missionCode") ~= 30050 or not def then R.mbForced = nil; return end
   local flag = get(vars, "playerDisableActionFlag")
   if R.want() then
    if flag == def and setVar("playerDisableActionFlag", none()) then
     hud(false)
     if not R.mbForced then log(fmt("step=mb_on;flag=%d;by=%s", def, R.mb and "self" or "host")) end
     R.mbForced = true
    end
   elseif R.mbForced then
    R.mbForced = nil
    if flag == none() then setVar("playerDisableActionFlag", def) end
    hud(true)
    log("step=mb_off")
   end
  end
  local function send()
   if not (OP and type(W.tx) == "function" and W.host == true and W.paired == true) then return false end
   R.seq = R.seq % 65535 + 1
   local ok = W.tx(OP.RULES, R.seq, R.mb and 1 or 0) == true
   if ok then R.sentEpoch = W.epoch else R.failAt = R.tick end
   return ok
  end
  -- Every PAIRED (a same-epoch re-pair too: a guest crash or a long link gap) gets the host's choice again.
  if type(W) == "table" and type(W.pairedHooks) == "table" then
   W.pairedHooks[#W.pairedHooks + 1] = function() R.sentEpoch = nil end
  end
  if OP and type(W.handlers) == "table" then
   W.handlers[OP.RULES] = function(v)
    if W.host ~= false or type(v) ~= "table" then return end
    local mb = v[2] == 1
    log(fmt("step=rx;seq=%s;mb_weapons=%d", tostring(v[1]), mb and 1 or 0))
    if (R.hostMb == true) ~= mb then
     say(mb and "Your partner allowed weapons at Mother Base." or "Your partner turned weapons at Mother Base off.")
    end
    R.hostMb, R.hostAt = mb, now()
    pcall(R.mbTick)
   end
  end
  -- Saves keep the real state: the wormhole level and the stock Mother Base flag, live again afterwards.
  local save = get(TppSave, "SaveVarsToSlot")
  if type(save) == "function" then
   TppSave.SaveVarsToSlot = function(...)
    local undo, i = {}, slot()
    if R.wormOff and R.slotOk and R.wormSeen and slotLevel(i) == 0 and setSlot(i, R.wormSeen) then
     undo[#undo + 1] = function() setSlot(i, 0) end
    end
    local def = mbDefault()
    if R.mbForced and def and get(vars, "playerDisableActionFlag") == none() and setVar("playerDisableActionFlag", def) then
     undo[#undo + 1] = function() setVar("playerDisableActionFlag", none()) end
    end
    if #undo > 0 and (R.saves or 0) < 3 then
     R.saves = (R.saves or 0) + 1
     log(fmt("step=save;fixed=%d;n=%d", #undo, R.saves))
    end
    local r = {pcall(save, ...)}
    for k = #undo, 1, -1 do undo[k]() end
    if not r[1] then error(r[2], 0) end
    return unpack(r, 2)
   end
  end
  function R.status()
   return fmt("rules;wormhole=%s;level=%s;seen=%d;mb_weapons=%d;from=%s;applied=%d", R.wormOff and "off" or "on", tostring(level()),
    R.wormSeen or 0, R.want() and 1 or 0, (type(W) == "table" and W.host == false) and "host" or "self", R.mbForced and 1 or 0)
  end
  function R.command(a)
   local c, v = a[1], a[2]
   if c == "rules" and #a == 1 then return true, R.status() end
   if c == "rules" or #a ~= 2 or (v ~= "0" and v ~= "1") then return false, "command_args" end
   -- Lab only until a play run proves both rules (STATUS 2026-10-31): play refuses in plain words.
   if type(W) ~= "table" or W.mode ~= 1 then say("Not available yet in this build."); return false, "not_available_yet" end
   if c == "wormhole" then
    if v == "0" then
     R.wormOff = true
     R.worm()
     say(R.wormSeen and "Wormhole Fulton off: you use the balloon Fulton again (only you)."
      or "Wormhole Fulton off (your save has no wormhole Fulton yet).")
     return true, fmt("wormhole=off;level=%s;seen=%d", tostring(level()), R.wormSeen or 0)
    end
    R.wormOff = false
    local l = level()
    if R.wormSeen and l and l < R.wormSeen and setLevel(R.wormSeen) then l = level() end
    log(fmt("step=worm_on;level=%s", tostring(l)))
    say((l or 0) >= 1 and "Wormhole Fulton on." or "Wormhole Fulton on (your save has no wormhole Fulton yet).")
    return true, fmt("wormhole=on;level=%s", tostring(l))
   end
   if not A.hostOnly() then return false, "host_only" end
   R.mb, R.chosen = v == "1", true
   local sent = send()
   pcall(R.mbTick)
   say(R.mb and "Weapons allowed at Mother Base for both players. Any kill at Mother Base still fails the mission."
    or "Mother Base rules are back: no lethal weapons, for both players.")
   return true, fmt("mb_weapons=%s;sent=%d;applied=%d;mission=%s", R.mb and "on" or "off", sent and 1 or 0, R.mbForced and 1 or 0,
    tostring(get(vars, "missionCode")))
  end
  for k in pairs({wormhole = true, mb_weapons = true, rules = true}) do P.handles[k] = true end
  local base = P.run
  P.run = function(a, id)
   local c = type(a) == "table" and a[1]
   if c == "wormhole" or c == "mb_weapons" or c == "rules" then return R.command(a) end
   return base(a, id)
  end
  local function tick()
   R.tick = R.tick + 1
   if R.tick % 15 ~= 0 then return end
   if type(W) == "table" then
    -- The host's choice again after each PAIRED or epoch (once it chose); a refused send waits 2 s.
    if W.host == true and R.chosen and W.paired == true and R.sentEpoch ~= W.epoch and R.tick - (R.failAt or -1e9) >= 60 then send() end
    -- The guest keeps the host's choice through a mission load; 60 s without the pairing drops it.
    if W.host == false and R.hostMb ~= nil then
     if W.paired == true then R.hostAt = now() elseif now() - (R.hostAt or 0) > 60000 then R.hostMb = nil; log("step=host_rule_dropped") end
    end
   end
   R.worm()
   R.mbTick()
  end
  local update = e.OnUpdate
  if type(update) == "function" then
   local function updated(...)
    pcall(tick)
    return ...
   end
   e.OnUpdate = function(...) return updated(update(...)) end
  end
 end
end
