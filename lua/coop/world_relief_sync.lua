-- Sortie (relief) vehicle on both copies (original; docs/vehicles-companions-design.md 6.3, M3,
-- route A). With the lab key world_relief (CFG r[17], RS.on(); default off) the host's sortie
-- vehicle (TppVehicle2 index 0, 25600) is the session's one relief vehicle, drawn by each copy's
-- own engine from the host's choice:
--   seeds    the follow seeds carry the host's relief carry vars and paint presets
--            (world_follow_seed.lua key[6] "relief"); the guest applies them before its
--            TppMission.Load (world_follow.lua), so its destination LoadCarry requests the same pack
--            at the same drop point (E2). Its own values are kept and every TppSave.SaveVarsToSlot
--            writes them (D8 b); a solo load gives them back.
--   state    the host sends VEHICLE_SPAWN (op 52) in free roam on change and every 5 s. A guest copy
--            whose mission never had index 0 while the host's has it (a host continue, a late join)
--            writes the host's carry and calls Vehicle.LoadCarry mid mission (section 8.3); the load
--            can draw it at the mission's drop point instead (run 8a81f776), so one that appears more
--            than 3 m from the host's pose is set there (SetPosition), as is one parked more than 20 m
--            off; one the host lacks for 15 s, or of another type, is removed (no credit).
--   removal  VEHICLE_GONE (world_vehicle.lua) removes index 0 on the receiver with Seize {"Instant"}
--            and a one-time skip of that copy's TppTerminal.OnFulton for 25600 (E17), never Despawn.
--   end      the guest skips TppMission.SeizeReliefVehicleOn* (E18): one Mother Base credit.
--   refusal  the guest's iDroid vehicle and loadout drops (MSN_DROP_VEHICLE; MSN_DROP_LOADOUT, whose
--            delivery can carry a vehicle) are inactive while paired in free roam; selecting one
--            logs the refusal and shows a toast (NOTE 13 a 4 / 5; D1 a).
do
local W = e.MgsCoopWorld
if type(W) == "table" and type(W.followSeeds) == "table" then
local RS = {seq = 0}
W.reliefSync = RS
local OP, H, fmt, floor, sqrt, pi = W.OP, W.handlers, string.format, math.floor, math.sqrt, math.pi
OP.VEHICLE_SPAWN = 52
local RID, LIFT = 25600, 0.5
local FREE = {[30010] = true, [30020] = true, [30050] = true}
-- Relief type t -> its userPresetVehiclePaintType index (the loadout vehicle id - 0x0D, static 0x14095D230).
local PAINT = {1, 0, 3, 2, 5, 7, 4, 6, 9, 8}
local SEIZE = {"SeizeReliefVehicleOnAbort", "SeizeReliefVehicleOnClear", "SeizeReliefVehicleOnForceGoToMb"}
local MENUS = {{"MSN_DROP_VEHICLE", 4, "idroid_drop_vehicle"}, {"MSN_DROP_LOADOUT", 5, "idroid_drop_loadout"}}
RS.RID, RS.PAINT, RS.MENUS = RID, PAINT, MENUS
local function L(text) W.log("event=relief;role=" .. (W.host and "host" or "guest") .. ";" .. text) end
local function note(a) if type(W.note) == "function" then pcall(W.note, 13, a, 0) end end
local function n(v) return type(v) == "number" and v == v and v > -1e12 and v < 1e12 end
local function mm(v) return floor(v * 1000 + 0.5) end
function RS.on() return RS.key == true and (W.level or 0) >= 3 end
function RS.applies() return RS.on() and not W.host end
local function readKey(cfg) RS.key = type(cfg) == "table" and cfg[17] == 1 end
local function send(cmd)
 local f = W.get(GameObject, "SendCommand")
 if type(f) ~= "function" then return nil end
 local ok, a, b = pcall(f, RID, cmd)
 if ok then return a, b end
end
function RS.alive() return send({id = "IsAlive"}) == true end
-- Index 0's position (m) and heading (rad: GetPosition's second return), or nil.
function RS.pose()
 local p, rot = send({id = "GetPosition"})
 if p == nil then return nil end
 local ok, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end)
 if ok and n(x) and n(y) and n(z) then return x, y, z, n(rot) and rot or 0 end
end
function RS.carry()
 local ct, a, p = W.get(vars, "reliefVehicleCarryType"), W.get(vars, "reliefVehicleCarryAttitude"), {}
 for i = 0, 3 do local v = W.get(a, i); p[i + 1] = n(v) and v or 0 end
 return n(ct) and ct >= 0 and floor(ct) or 0, p
end
function RS.paint(t)
 local i = PAINT[t]
 local v = i and W.get(W.get(vars, "userPresetVehiclePaintType"), i)
 return n(v) and v >= 0 and v <= 255 and floor(v) or 0
end

-- D8 (b): the relief keys of the follow seeds, read and written on vars.
local function keys()
 if RS.keyList then return RS.keyList end
 local list = {}
 for _, key in ipairs(W.followSeeds.keys) do if key[6] == "relief" then list[#list + 1] = key end end
 RS.keyList = list
 return list
end
function RS.snapshot()
 local s = {}
 for _, key in ipairs(keys()) do
  local v = W.get(vars, key[1])
  if key[2] then
   if v == nil then return nil, key[1] end
   local t = {}
   for i = 0, key[2] - 1 do
    local x = W.get(v, i)
    if not n(x) then return nil, key[1] end
    t[i] = x
   end
   v = t
  elseif not n(v) then return nil, key[1] end
  s[key[1]] = v
 end
 return s
end
function RS.restore(s)
 for _, key in ipairs(keys()) do
  local v = s[key[1]]
  if v ~= nil then pcall(function()
   if key[2] then
    local a = vars[key[1]]
    for i = 0, key[2] - 1 do a[i] = v[i] end
   else vars[key[1]] = v end
  end) end
 end
end
function RS.keep(why)
 if RS.own then return true end
 local s, bad = RS.snapshot()
 if not s then L("step=own_refused;key=" .. tostring(bad) .. ";"); return false end
 RS.own = s
 L(fmt("step=own_kept;why=%s;own_type=%d;", why, s.reliefVehicleCarryType % 256))
 return true
end
function RS.release(why)
 if not RS.own then return end
 RS.restore(RS.own)
 L(fmt("step=own_restored;why=%s;own_type=%d;", why, RS.own.reliefVehicleCarryType % 256))
 RS.own = nil
end
-- Guest: the host's relief seeds were applied for `mission` (follow kind `kind`).
function RS.seeded(mission, kind)
 local ct = RS.carry()
 RS.coop = mission
 if RS.expect and RS.expect.mission == mission and RS.expect.ct == ct then return end
 RS.expect = {mission = mission, ct = ct}
 L(fmt("step=seed_apply;type=%d;ct=%d;paint=%d;kind=%d;mission=%d;", ct % 256, ct, RS.paint(ct % 256), kind or 0, mission))
end
-- TppMission.Load (world_follow.lua, before the seeds): coop is a co-op travel.
function RS.load(nextMission, coop)
 if W.host then
  if coop and RS.on() then
   local ct, p = RS.carry()
   L(fmt("step=seed;type=%d;ct=%d;paint=%d;cp=%.2f,%.2f,%.2f,%.3f;mission=%d;", ct % 256, ct, RS.paint(ct % 256), p[1], p[2], p[3],
    p[4], nextMission or 0))
  end
 elseif not coop then
  RS.coop = nil
  RS.release("solo")
 end
end

-- Quiet removal of this copy's index 0: Seize {"Instant"} (never DirectAccount / CheckFultonType) and
-- this copy's TppTerminal.OnFulton for 25600 skipped once within 30 s (E17: no Mother Base credit).
-- VEHICLE_GONE's own bookkeeping marks it gone (its FultonInfo, att local, is not a guest loss).
function RS.removeQuiet(why)
 RS.install()
 RS.quiet = W.now() + 30000
 local G = W.vehicleGone
 if type(G) == "table" and type(G.gone) == "table" and not G.gone[RID] then G.gone[RID] = "relief" end
 local f = W.get(GameObject, "SendCommand")
 local ok = type(f) == "function" and pcall(f, RID, {id = "Seize", options = {"Instant"}})
 L(fmt("step=remove;via=seize_instant;why=%s;ok=%d;", why, ok and 1 or 0))
 return ok == true, "seize_instant"
end
-- Guest: this copy's index 0 to the host's pose (the RIDE_FAST placement, world_ride_pose.lua R.place:
-- SetPosition {position, rotY}); the live pose arrives 0.5 m up, so it settles on the ground.
function RS.placeAt(h, why)
 local rp, ok = W.ridePose, false
 if type(rp) == "table" and type(rp.place) == "function" then
  local good, r = pcall(rp.place, RID, mm(h.x), mm(h.y), mm(h.z), mm(h.yaw))
  ok = good and r == true
 end
 if not ok then
  local f = W.get(GameObject, "SendCommand")
  local made, v = pcall(Vector3, h.x, h.y, h.z)
  if type(f) == "function" and made and v then
   local good, r = pcall(f, RID, {id = "SetPosition", position = v, rotY = h.yaw})
   ok = good and r ~= false
  end
 end
 L(fmt("step=placed;why=%s;ok=%d;x=%.2f;y=%.2f;z=%.2f;yaw=%.3f;", why, ok and 1 or 0, h.x, h.y, h.z, h.yaw))
 return ok
end
-- Guest: the host's carry and pose (VEHICLE_SPAWN), then the stock Vehicle.LoadCarry mid mission.
function RS.loadHost(h, why)
 local f = W.get(Vehicle, "LoadCarry")
 if type(f) ~= "function" then L("step=load_refused;reason=binding;"); return false end
 RS.keep("load")
 local t = h.ct % 256
 local okW = pcall(function()
  vars.reliefVehicleCarryType = h.ct
  local a = vars.reliefVehicleCarryAttitude
  a[0], a[1], a[2], a[3] = h.x, h.y, h.z, h.yaw
  if PAINT[t] then vars.userPresetVehiclePaintType[PAINT[t]] = h.paint end
 end)
 local G = W.vehicleGone
 if type(G) == "table" and type(G.gone) == "table" and G.gone[RID] == "relief" then G.gone[RID] = nil end
 RS.reload = nil
 local okC = okW and pcall(f)
 RS.coop, RS.arrive = W.mission, {ct = h.ct, deadline = W.now() + 30000, source = "load", place = h}
 L(fmt("step=load;why=%s;type=%d;ct=%d;paint=%d;write=%d;call=%d;x=%.2f;y=%.2f;z=%.2f;yaw=%.3f;", why, t, h.ct, h.paint,
  okW and 1 or 0, okC and 1 or 0, h.x, h.y, h.z, h.yaw))
 return okC
end
local function busy()
 local G = W.vehicleGone
 if type(G) == "table" and type(G.busy) == "function" then
  local ok, why = pcall(G.busy, RID)
  if ok then return why end
  return "busy_error"
 end
 if W.get(vars, "playerVehicleGameObjectId") == RID then return "local_rider" end
end

-- Both roles: the arrival line once index 0 answers after the mission start or a load.
local function arrival(now)
 local a = RS.arrive
 if not a or a.done or not W.inGame() then return end
 if RS.alive() then
  a.done = true
  local x, y, z, yaw = RS.pose()
  local ct = RS.carry()
  L(fmt("step=arrived;type=%d;vt=%s;paint=%d;x=%.2f;y=%.2f;z=%.2f;yaw=%.3f;mission=%d;source=%s;", ct % 256,
   tostring(send({id = "GetVehicleType"})), RS.paint(ct % 256), x or 0, y or 0, z or 0, yaw or 0, W.mission, a.source))
  -- A load draws the vehicle where the relief record already points when it holds the same type (the
  -- mission's drop point, run 8a81f776): more than 3 m from the host's pose, it is set there.
  local h = a.place
  if h and x and sqrt((x - h.x) ^ 2 + (z - h.z) ^ 2) > 3 then RS.placeAt(h, "load") end
 elseif now > a.deadline then
  a.done = true
  L(fmt("step=arrive_none;type=%d;mission=%d;source=%s;", a.ct % 256, W.mission, a.source))
 end
end
-- Host: VEHICLE_SPAWN on change and every 5 s (paired, free roam, in game). The pose is the carry
-- attitude while the vehicle stands within 1.5 m of it (flag 2: the guest's LoadCarry then gives the
-- host's exact pose, run 612afff7), else its live pose 2 m up.
local function hostTick(now)
 if not (W.paired and FREE[W.mission] and W.inGame()) then RS.txAlive = nil; return end
 local alive = RS.alive()
 if now < (RS.txAt or 0) and alive == RS.txAlive then return end
 local ct, p = RS.carry()
 local flags, x, y, z, yaw = alive and 1 or 0, 0, 0, 0, 0
 if alive then
  local vx, vy, vz, vr = RS.pose()
  if not vx then return end
  if sqrt((vx - p[1]) ^ 2 + (vz - p[3]) ^ 2) <= 1.5 then x, y, z, yaw, flags = p[1], p[2], p[3], p[4], 3
  else x, y, z, yaw = vx, vy + LIFT, vz, vr end
 end
 yaw = (yaw + pi) % (2 * pi) - pi
 RS.seq = RS.seq % 65535 + 1
 if W.tx(OP.VEHICLE_SPAWN, RS.seq, W.mission, RID, 1, ct, RS.paint(ct % 256), flags, mm(x), mm(y), mm(z), mm(yaw)) then
  RS.txAt, RS.txAlive = now + 5000, alive
  if RS.txLogged ~= flags * 256 + ct % 256 then
   RS.txLogged = flags * 256 + ct % 256
   L(fmt("step=state_tx;alive=%d;type=%d;carry_pose=%d;", alive and 1 or 0, ct % 256, flags >= 2 and 1 or 0))
  end
 end
end
H[OP.VEHICLE_SPAWN] = function(v, now)
 if W.host or #v ~= 11 or v[3] ~= RID or v[4] ~= 1 then return end
 local h = {mission = v[2], ct = v[5], paint = v[6], alive = v[7] % 2 == 1, carryPose = v[7] >= 2, x = v[8] / 1000, y = v[9] / 1000,
  z = v[10] / 1000, yaw = v[11] / 1000, at = now}
 local was = RS.hostState
 RS.hostState = h
 if not was or was.alive ~= h.alive or was.ct ~= h.ct or was.mission ~= h.mission then
  L(fmt("step=host_state;alive=%d;type=%d;mission=%d;", h.alive and 1 or 0, h.ct % 256, h.mission))
 end
end
-- Guest: index 0 here against the host's (8.3 continue / late join; a host loss VEHICLE_GONE missed;
-- this copy's own of another type: removed, then loaded; one parked more than 20 m from the host's: placed).
local function reconcile(now)
 local h = RS.hostState
 if not h or now - h.at > 15000 or h.mission ~= W.mission or not FREE[W.mission] or not W.inGame() or
  (RS.arrive and not RS.arrive.done) then RS.diffAt = nil; return end
 local mine, want, other = RS.alive(), h.alive and h.ct % 256 > 0, nil
 if mine and want then
  if RS.carry() % 256 ~= h.ct % 256 then other = "type"
  else
   local x, _, z = RS.pose()
   if x and (h.x ~= 0 or h.z ~= 0) and sqrt((x - h.x) ^ 2 + (z - h.z) ^ 2) > 20 then other = "far" end
  end
 end
 if mine == want and not other then RS.diffAt = nil; return end
 local G = W.vehicleGone
 if want and not mine and (RS.seen == W.mission and RS.reload ~= W.mission or type(G) == "table" and type(G.gone) == "table" and
  (G.gone[RID] == true or type(G.watch) == "table" and G.watch[RID])) then
  -- This copy had index 0 this mission and lost it: VEHICLE_GONE owns that, never a respawn here.
  if RS.skipLogged ~= W.mission then RS.skipLogged = W.mission; L("step=reconcile_skip;reason=lost_here;") end
  return
 end
 RS.diffAt = RS.diffAt or now
 if now - RS.diffAt < (want and 10000 or 15000) or now < (RS.fixAt or 0) then return end
 if (RS.fixes or 0) >= 3 then
  if not RS.gaveUp then RS.gaveUp = true; L("step=reconcile_giveup;") end
  return
 end
 if want and not mine then
  RS.fixAt, RS.fixes = now + 20000, (RS.fixes or 0) + 1
  RS.loadHost(h, "host_has")
  return
 end
 local why = busy()
 if why then
  if RS.busyLogged ~= why then RS.busyLogged = why; L("step=remove_wait;reason=" .. why .. ";") end
  return
 end
 RS.fixAt, RS.fixes = now + 20000, (RS.fixes or 0) + 1
 if other == "far" then RS.placeAt(h, "far"); return end
 RS.removeQuiet(other or "host_none")
 if other then RS.reload = W.mission end
end
-- Guest: the iDroid drop items inactive while paired in free roam (re-applied every 2 s while the
-- iDroid is closed: TppTerminal.SetUp at a mission start sets every item active again).
local function menuActive(on)
 local f = W.get(TppUiCommand, "SetMbTopMenuItemActive")
 if type(f) ~= "function" then return false end
 local ok = true
 for _, m in ipairs(MENUS) do ok = pcall(f, m[1], on) and ok end
 return ok
end
function RS.unguard()
 if not RS.greyed then return end
 RS.greyed = nil
 menuActive(true)
 L("step=unguard;path=idroid_drop;")
end
local function guard(now)
 if not (W.paired and W.inGame() and FREE[W.mission]) then RS.unguard(); return end
 if now < (RS.greyAt or 0) then return end
 RS.greyAt = now + 2000
 local open = W.get(TppUiCommand, "IsMbDvcTerminalOpened")
 if type(open) == "function" then
  local ok, yes = pcall(open)
  if ok and yes == true then return end
 end
 if menuActive(false) then
  RS.greyed = true
  if RS.greyLogged ~= W.mission then RS.greyLogged = W.mission; L("step=guard;path=idroid_drop;via=menu_inactive;") end
 end
end
-- Terminal MbDvcActSelectNonActiveMenu (arg: StrCode32 of the menu name, TppRadio.PlaySelectBuddy).
function RS.nonActive(menu)
 local ids = RS.ids
 if not ids then return false end
 for i, m in ipairs(MENUS) do
  if ids.menu[i] == menu then
   L(fmt("step=refused;path=%s;via=menu_inactive;", m[3]))
   local now = W.now()
   if now >= (RS.toastAt or 0) then RS.toastAt = now + 3000; note(m[2]) end
   return true
  end
 end
 return false
end
local observe = W.observe
if type(observe) == "function" then
 function W.observe(class, msg, a0, ...)
  local ids = RS.ids
  if RS.greyed and ids and class == ids.terminal and msg == ids.nonActive then pcall(RS.nonActive, a0) end
  return observe(class, msg, a0, ...)
 end
end

function RS.tick(now)
 if not RS.on() then RS.unguard(); return end
 if now < (RS.nextTick or 0) then return end
 RS.nextTick = now + 250
 if RS.alive() then RS.seen = W.mission end
 arrival(now)
 if W.host then hostTick(now) else guard(now); reconcile(now) end
end
local tick = W.tick
function W.tick(...)
 if tick then tick(...) end
 if not W.active then return end
 local now = W.now()
 local ok, err = pcall(RS.tick, now)
 if not ok and now >= (RS.errAt or 0) then RS.errAt = now + 10000; L("step=error;msg=" .. string.sub(tostring(err), 1, 80)) end
end

-- Game hooks (once, with the key): the guest's mission-end seize skip, the quiet OnFulton skip, D8 (b).
local function wrap(t, name, make)
 local original = W.get(t, name)
 if type(original) ~= "function" then return false end
 local f = make(original)
 return pcall(function() t[name] = f end) and W.get(t, name) == f
end
function RS.skipSeize() return RS.key == true and not W.host and (W.paired == true or RS.coop ~= nil and RS.coop == W.get(vars, "missionCode")) end
function RS.install()
 if RS.installed then return end
 RS.installed = true
 local hooks = 0
 for _, name in ipairs(SEIZE) do
  if wrap(TppMission, name, function(o) return function(...)
   local alive = RS.alive() and 1 or 0
   if RS.skipSeize() then L(fmt("step=seize_skipped;fn=%s;alive=%d;", name, alive)); return end
   if RS.key and alive == 1 then L(fmt("step=seize;fn=%s;alive=1;", name)) end
   return o(...)
  end end) then hooks = hooks + 1 end
 end
 if wrap(TppTerminal, "OnFulton", function(o) return function(id, ...)
  if id == RID and RS.quiet then
   local live = W.now() <= RS.quiet
   RS.quiet = nil
   if live then L("step=credit_skipped;fn=OnFulton;"); return end
  end
  if id == RID and RS.key then L("step=credit;fn=OnFulton;") end
  return o(id, ...)
 end end) then hooks = hooks + 1 end
 if wrap(TppSave, "SaveVarsToSlot", function(o) return function(...)
  local own = RS.own
  if not own then return o(...) end
  local live = RS.snapshot()
  if not live then return o(...) end
  RS.restore(own)
  local r = {pcall(o, ...)}
  RS.restore(live)
  RS.saves = (RS.saves or 0) + 1
  local now = W.now()
  if RS.saves <= 3 or now - (RS.saveLogAt or -1e9) >= 5000 then
   RS.saveLogAt = now
   L(fmt("step=save_own;slot=%s;own_type=%d;live_type=%d;n=%d;", tostring((...)), own.reliefVehicleCarryType % 256,
    live.reliefVehicleCarryType % 256, RS.saves))
  end
  if not r[1] then error(r[2], 0) end
  return unpack(r, 2)
 end end) then hooks = hooks + 1 end
 L(fmt("step=installed;hooks=%d;", hooks))
end
-- VEHICLE_GONE's receiver removal: index 0 by Seize Instant instead of today's index0 refusal (a
-- destroyed one stays M4's wreck when VEHICLE_STATE is on).
local G = W.vehicleGone
if type(G) == "table" and type(G.remove) == "function" then
 local base = G.remove
 G.remove = function(id, cause, ...)
  if id == RID and RS.on() and not (cause == 2 and W.vehicleStateOn == true) then return RS.removeQuiet("vehicle_gone") end
  return base(id, cause, ...)
 end
end

W.follow.installHooks[#W.follow.installHooks + 1] = function(cfg)
 readKey(cfg)
 if RS.key then
  local ok, err = pcall(RS.install)
  if not ok then W.log("event=relief;step=error;at=install;msg=" .. string.sub(tostring(err), 1, 80)) end
 end
end
W.startHooks[#W.startHooks + 1] = function()
 readKey(W.cfg(W.mission, 0))
 RS.hostState, RS.diffAt, RS.fixAt, RS.fixes, RS.gaveUp, RS.txAt, RS.txAlive, RS.txLogged = nil, nil, nil, 0, nil, nil, nil, nil
 RS.nextTick, RS.greyAt, RS.greyed, RS.skipLogged, RS.busyLogged, RS.quiet, RS.seen, RS.arrive, RS.reload = nil, nil, nil, nil, nil, nil, nil, nil, nil
 local s = W.api and W.api.str32
 if type(s) == "function" then
  RS.ids = {terminal = s("Terminal"), nonActive = s("MbDvcActSelectNonActiveMenu"), menu = {}}
  for i, m in ipairs(MENUS) do RS.ids.menu[i] = s(m[1]) end
 end
 if not RS.on() or not FREE[W.mission] then return end
 local ct = RS.carry()
 if not W.host and RS.expect and RS.expect.mission == W.mission and RS.expect.ct % 256 > 0 then
  RS.arrive = {ct = RS.expect.ct, deadline = W.now() + 30000, source = "seed"}
 elseif ct % 256 > 0 then
  RS.arrive = {ct = ct, deadline = W.now() + 30000, source = W.host and "carry" or "own"}
 end
end
W.pairedHooks[#W.pairedHooks + 1] = function()
 RS.txAt = nil
 if not W.host and RS.on() then RS.coop = W.mission end
end
W.unpairedHooks[#W.unpairedHooks + 1] = function() pcall(RS.unguard) end
W.stopHooks[#W.stopHooks + 1] = function(final) if final then pcall(RS.unguard) end end

-- debug relief_status (read only, any mode), relief_lab nonactive vehicle|loadout (lab: the refusal path
-- of a selected inactive drop item) and relief_lab forget (lab, guest; below).
function RS.status()
 local ct = RS.carry()
 local h = RS.hostState
 return fmt("relief_sync;on=%d;role=%s;type=%d;ct=%d;v0=%d;own=%s;greyed=%d;host_alive=%s;host_type=%s;fixes=%d;saves=%d;coop=%s",
  RS.on() and 1 or 0, W.host and "host" or "guest", ct % 256, ct, RS.alive() and 1 or 0,
  RS.own and tostring(RS.own.reliefVehicleCarryType % 256) or "-", RS.greyed and 1 or 0, h and (h.alive and "1" or "0") or "-",
  h and tostring(h.ct % 256) or "-", RS.fixes or 0, RS.saves or 0, tostring(RS.coop))
end
function RS.lever(a)
 if a[1] == "relief_status" and #a == 1 then return true, RS.status() end
 if a[1] == "relief_lab" and #a == 3 and a[2] == "nonactive" and (a[3] == "vehicle" or a[3] == "loadout") then
  if W.mode ~= 1 then return false, "relief_lab_only" end
  if not RS.ids then return false, "relief_ids" end
  local ok = RS.nonActive(RS.ids.menu[a[3] == "vehicle" and 1 or 2])
  return ok, "relief_lab;nonactive=" .. a[3]
 end
 -- relief_lab forget (lab, guest): this copy's index 0 removed (no credit, no VEHICLE_GONE request) and
 -- the reconcile allowed to load the host's again from VEHICLE_SPAWN: the late-join path on demand.
 if a[1] == "relief_lab" and #a == 2 and a[2] == "forget" then
  if W.mode ~= 1 then return false, "relief_lab_only" end
  if W.host then return false, "relief_guest_only" end
  if not RS.alive() then return false, "relief_absent" end
  local ok = RS.removeQuiet("lab_forget")
  RS.reload, RS.diffAt, RS.fixAt, RS.fixes, RS.gaveUp = W.mission, nil, nil, 0, nil
  return ok, "relief_lab;forget"
 end
 return false, "relief_args"
end
end
end
