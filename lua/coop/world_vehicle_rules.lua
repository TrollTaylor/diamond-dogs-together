-- The user's vehicle decisions of 2026-10-29 (original; docs/vehicles-companions-design.md section 10 D11, D12, D14,
-- results in section 8.9; docs/world-sync-protocol.md section 19). Spliced after world_vehicle_state.lua (its
-- VEHICLE_GONE and VEHICLE_STATE helpers); world_relief_sync.lua, later, still owns index 0 (the sortie vehicle).
-- Three lab and play keys in the CFG r[15] vehicle bitmask (world_sync.lua), each off unless named until its proof:
--   D11 world_loadout_block (bit 32): the guest's iDroid LOADOUT supply drop (MSN_DROP_LOADOUT) is inactive while it is
--       paired in co-op (any mission world sync runs), and inside that mission across a link drop until its final stop;
--       selecting it logs the refusal and shows NOTE 13 a 5. The host's iDroid and every other item stay as they are.
--       Not tied to world_relief: a loadout delivery can carry a vehicle whatever that key says (SetRelief 0x1416BC4B0
--       from the loadout-delivery path, section 8.4), and with world_relief off nothing gives the host's copy that
--       vehicle (section 8.4 run 5874afcd: each copy's index 0 is its own then).
--   D12 world_vehicle_break (bit 8; with VEHICLE_GONE): a vehicle that breaks on the guest's copy without attribution
--       (the game posts no Damage message for a vehicle hit, 35ba7929) is requested as a destruction: world_vehicle.lua
--       asks V.unattributed, then REQ_VEHICLE_GONE cause 2 as for any guest loss (no wire change; never index 0; at
--       most 6 per 60 s). The host judges every guest destruction request: refused while someone rides its copy
--       (local_rider, occupied: a copy that broke too is answered as gone first), else applied only when the guest's
--       player (its DOWNED_STATUS position, world_downed.lua, at most 3 s old) stands within 60 m of the pose the guest
--       reported, or the host's own copy is down to a quarter of the highest body life it read; at most 4 applied per
--       20 s. Applied: the host's copy becomes the same wreck (SetBodyLife 0, the engine's own destruction; read back,
--       the Despawn if it does not take). Refused: REJECT 8; the guest's copy keeps its wreck (no engine command undoes
--       one: SetBodyLife writes the life word only, IsAlive reads rec+0x70 bits nothing clears), logged break_refused.
--   D14 world_vehicle_credit (bit 16; with VEHICLE_GONE): the receiver of a Fulton loss (cause 1) removes its copy with
--       the stock Seize {"Instant"} instead of a Despawn, so its own engine runs TppTerminal.OnFulton ->
--       OnFultonVehicle -> AddTempResource (E17: no GMP charged, the temporary resource commits at the mission end) and
--       the vehicle is credited once to each co-op save. Never index 0: the sortie vehicle keeps M3's host-only credit.
do
local W = e.MgsCoopWorld
local G = type(W) == "table" and W.vehicleGone
if type(G) == "table" then
local VR = {}
W.vehicleRules = VR
local fmt, floor, sqrt, H = string.format, math.floor, math.sqrt, W.handlers
local LOADOUT, BREAK_MM, FRESH_MS, RATE_N, RATE_MS, ASK_N, ASK_MS = "MSN_DROP_LOADOUT", 60000, 3000, 4, 20000, 6, 60000
local OWN_MS, CREDIT_MS, GREY_MS, TOAST_MS = 5000, 10000, 2000, 3000
VR.BREAK_MM, VR.FRESH_MS, VR.RATE_N, VR.RATE_MS, VR.ASK_N, VR.CREDIT_MS = BREAK_MM, FRESH_MS, RATE_N, RATE_MS, ASK_N, CREDIT_MS
local function L(...)
  local ok, text = pcall(fmt, ...)
  if ok and type(W.log) == "function" then pcall(W.log, "event=vehicle_rules;" .. text) end
end
local function now() return W.now() end
local function role() return W.host and "host" or "guest" end
function VR.breakOn() return W.vehicleBreakOn == true and G.on() end
function VR.creditOn() return W.vehicleCreditOn == true and G.on() end
function VR.loadoutOn() return W.vehicleLoadoutOn == true and W.active == true and not W.host end
function VR.reset()
  VR.applied, VR.asked, VR.peak, VR.credits, VR.own = {}, {}, {}, {}, {}
  VR.st = {judged = 0, applied = 0, refused = 0, asked = 0, seized = 0, credited = 0}
  VR.nextTick, VR.greyAt, VR.greyLogged = 0, nil, nil
end
VR.reset()
-- Entries of list newer than ms (oldest first).
local function window(list, t, ms)
  local keep = {}
  for _, at in ipairs(list) do if t - at < ms then keep[#keep + 1] = at end end
  return keep
end

-- D12, guest: world_vehicle.lua V.observe asks this for a VehicleBroken it cannot attribute.
function G.unattributed(id, t)
  if W.host or not VR.breakOn() or id % 512 == 0 then return false end
  -- A break this copy caused itself (its own wreck or removal) is never a loss to request.
  if VR.own[id] and t - VR.own[id] < OWN_MS then return false end
  VR.asked = window(VR.asked, t, ASK_MS)
  if #VR.asked >= ASK_N then L("step=break_skip;id=%d;reason=rate", id); return false end
  VR.asked[#VR.asked + 1], VR.st.asked = t, VR.st.asked + 1
  L("step=break_seen;id=%d", id)
  return true
end
-- D12, host: the guest's player position from its DOWNED_STATUS (mm, at most FRESH_MS old), or nil.
function VR.guestPos(t)
  local D = W.downed
  local r = type(D) == "table" and type(D.records) == "table" and D.records[2]
  if type(r) ~= "table" or type(r.pos) ~= "table" or type(r.at) ~= "number" or t - r.at > FRESH_MS then return nil end
  local x, z = r.pos[1], r.pos[3]
  if type(x) == "number" and type(z) == "number" then return x, z end
end
-- This copy's body life of id and the highest it read this mission (VEHICLE_STATE's native read), or nil.
function VR.life(id)
  local S = W.vehicleState
  if type(S) ~= "table" or type(S.readLife) ~= "function" then return nil end
  local r = S.readLife(id, 1)
  local v = type(r) == "table" and r[id]
  if type(v) == "table" and type(v[1]) == "number" and v[1] < 65535 then return v[1], VR.peak[id] end
end
-- q: the guest's request (q.x, q.z in mm: the guest's copy where it broke). true, why | false, why.
function VR.plausible(q, t)
  local gx, gz = VR.guestPos(t)
  local d = gx and floor(sqrt((gx - q.x) ^ 2 + (gz - q.z) ^ 2)) or nil
  local life, peak = VR.life(q.id)
  local near, heavy = d ~= nil and d <= BREAK_MM, life ~= nil and peak ~= nil and peak > 0 and life * 4 <= peak
  local why = near and "near" or heavy and "heavy" or d and "guest_far" or "guest_unknown"
  VR.st.judged = VR.st.judged + 1
  L("step=break_judge;id=%d;rid=%d;ok=%d;why=%s;d_mm=%d;life=%d;peak=%d", q.id, q.rid or 0, (near or heavy) and 1 or 0, why, d or -1,
    life or -1, peak or -1)
  return near or heavy, why
end
local baseDecide = G.decide
G.decide = function(q, ...)
  local verdict, why = baseDecide(q, ...)
  if not (W.host and q.src == "req" and q.cause == 2 and VR.breakOn()) then return verdict, why end
  -- Never under a rider of the host's copy (a broken copy was answered as gone above, before any hold).
  if verdict == "hold" and (why == "local_rider" or why == "occupied") then
    VR.st.refused = VR.st.refused + 1
    L("step=break_judge;id=%d;rid=%d;ok=0;why=ridden;by=%s", q.id, q.rid or 0, why)
    return "refuse", "ridden"
  end
  if verdict ~= "apply" then return verdict, why end
  local t = now()
  if q.plausible == nil then q.plausible, q.pwhy = VR.plausible(q, t) end
  if not q.plausible then VR.st.refused = VR.st.refused + 1; return "refuse", q.pwhy end
  VR.applied = window(VR.applied, t, RATE_MS)
  if #VR.applied >= RATE_N then
    VR.st.refused = VR.st.refused + 1
    L("step=break_judge;id=%d;rid=%d;ok=0;why=rate;n=%d", q.id, q.rid or 0, #VR.applied)
    return "refuse", "rate"
  end
  VR.applied[#VR.applied + 1], VR.st.applied = t, VR.st.applied + 1
  return "apply"
end

-- Removal forms (both keys need VEHICLE_GONE on). The id is marked gone before the command: the engine's own
-- messages for it (VehicleBroken; Fulton, FultonInfo, Neutralize with att=local) are never taken for a loss here.
local baseRemove = G.remove
local function form(id, cmd, via, cause, ...)
  local a, had = W.api, G.gone[id]
  G.gone[id] = had or true
  if type(a) ~= "table" or type(a.send) ~= "function" or not pcall(a.send, id, cmd) then
    G.gone[id] = had
    return baseRemove(id, cause, ...)
  end
  return true, via
end
G.remove = function(id, cause, ...)
  local t = now()
  VR.own[id] = t
  if type(id) ~= "number" or id % 512 == 0 then return baseRemove(id, cause, ...) end
  -- D12: the host applies a guest's destruction as the same wreck on its own copy.
  if cause == 2 and W.host and VR.breakOn() then return form(id, {id = "SetBodyLife", life = 0}, "wreck", cause, ...) end
  -- D14: the receiver's own engine Fulton credit (Seize Instant: no balloon, removed at once, credited once).
  if cause == 1 and VR.creditOn() then
    -- Expected before the command: the engine may run its credit inside it.
    VR.credits[id] = {at = t}
    local ok, via = form(id, {id = "Seize", options = {"Instant"}}, "seize", cause, ...)
    if ok and via == "seize" then
      VR.st.seized = VR.st.seized + 1
      L("step=credit_seize;id=%d;role=%s;mission=%d", id, role(), W.mission or 0)
    elseif not VR.credits[id].done then VR.credits[id] = nil end
    return ok, via
  end
  return baseRemove(id, cause, ...)
end
-- D14: TppTerminal.OnFultonVehicle(id, _, _, resource, _, player) is where a vehicle Fulton becomes a temporary
-- resource (TppTerminal.AddTempResource); each call is logged, the receiver's own (after its Seize) as via=receiver.
function VR.credited(id, res)
  if not G.typed(id) then return end
  local c = VR.credits[id]
  local mine = c ~= nil and not c.done and now() - c.at <= CREDIT_MS
  if mine then c.done, VR.st.credited = true, VR.st.credited + 1 end
  L("step=credit;fn=OnFultonVehicle;id=%d;res=%s;via=%s;role=%s;mission=%d", id, tostring(res), mine and "receiver" or "own", role(),
    W.mission or 0)
end
function VR.install()
  if VR.installed then return end
  VR.installed = true
  local T = W.get(_G, "TppTerminal")
  local f = W.get(T, "OnFultonVehicle")
  if type(f) ~= "function" then L("step=credit_hook;ok=0;reason=binding"); return end
  local wrapped = function(...)
    local id, _, _, res = ...
    pcall(VR.credited, id, res)
    return f(...)
  end
  local ok = pcall(function() T.OnFultonVehicle = wrapped end) and W.get(T, "OnFultonVehicle") == wrapped
  L("step=credit_hook;ok=%d", ok and 1 or 0)
end
local function creditTick(t)
  for id, c in pairs(VR.credits) do
    if c.done then VR.credits[id] = nil
    elseif t - c.at > CREDIT_MS then
      VR.credits[id] = nil
      L("step=credit_missing;id=%d;role=%s", id, role())
    end
  end
end
-- D12, guest: the host refused its destruction request (REJECT for a cause 2 REQ_VEHICLE_GONE).
local REQ, REJ = W.OP.REQ_VEHICLE_GONE or 47, W.OP.REJECT or 4
local baseReject = H[REJ]
H[REJ] = function(v, ...)
  local q = not W.host and type(v) == "table" and v[2] == REQ and G.req[v[1]]
  if baseReject then baseReject(v, ...) end
  if type(q) == "table" and type(q.w) == "table" and q.w.cause == 2 then
    L("step=break_refused;id=%d;rid=%d;reason=%s;here=wreck", q.id, v[1], tostring(v[3]))
  end
end
-- VEHICLE_STATE's life reads also give each vehicle's highest life this mission (D12's heavy-damage rule).
local S = W.vehicleState
if type(S) == "table" and type(S.readLife) == "function" then
  local baseRead = S.readLife
  S.readLife = function(...)
    local r = baseRead(...)
    if type(r) == "table" then
      for id, v in pairs(r) do
        if type(id) == "number" and type(v) == "table" and type(v[1]) == "number" and v[1] < 65535 and v[1] > (VR.peak[id] or 0) then
          VR.peak[id] = v[1]
        end
      end
    end
    return r
  end
end

-- D11, guest: the LOADOUT item inactive (TppUiCommand.SetMbTopMenuItemActive; TppTerminal.SetUp at a mission start
-- leaves every item active, so the guard repeats every 2 s while the iDroid is closed; the first pass of a mission
-- applies even while it is open). Unguard sets it active again: the state SetUp leaves.
local function menu(on)
  local f = W.get(TppUiCommand, "SetMbTopMenuItemActive")
  return type(f) == "function" and pcall(f, LOADOUT, on) == true
end
function VR.coopNow() return W.paired == true or VR.coop ~= nil and VR.coop == W.mission and not VR.stopped end
function VR.unguard()
  if not VR.greyed then return end
  VR.greyed = nil
  menu(true)
  L("step=unguard;path=idroid_drop_loadout;mission=%d", W.mission or 0)
end
local function guard(t)
  if not (VR.loadoutOn() and W.inGame() and VR.coopNow()) then VR.unguard(); return end
  if t < (VR.greyAt or 0) then return end
  VR.greyAt = t + GREY_MS
  local open = VR.greyed and W.get(TppUiCommand, "IsMbDvcTerminalOpened")
  if type(open) == "function" then
    local ok, yes = pcall(open)
    if ok and yes == true then return end
  end
  if menu(false) then
    VR.greyed = true
    if VR.greyLogged ~= W.mission then
      VR.greyLogged = W.mission
      L("step=guard;path=idroid_drop_loadout;via=menu_inactive;mission=%d", W.mission or 0)
    end
  end
end
-- Terminal MbDvcActSelectNonActiveMenu (arg: StrCode32 of the item) for the LOADOUT item while this module greys it.
function VR.refuse()
  L("step=refused;path=idroid_drop_loadout;via=menu_inactive;mission=%d", W.mission or 0)
  local t = now()
  if t >= (VR.toastAt or 0) then
    VR.toastAt = t + TOAST_MS
    if type(W.note) == "function" then pcall(W.note, 13, 5, 0) end
  end
end
local observe = W.observe
if type(observe) == "function" then
  function W.observe(class, msg, a0, ...)
    local ids = VR.ids
    if VR.greyed and ids and class == ids.terminal and msg == ids.nonActive and a0 == ids.loadout then pcall(VR.refuse) end
    return observe(class, msg, a0, ...)
  end
end

function VR.tick(t)
  if t < VR.nextTick then return end
  VR.nextTick = t + 250
  guard(t)
  creditTick(t)
end
local tick = W.tick
function W.tick(...)
  if tick then tick(...) end
  if not W.active then return end
  local t = now()
  local ok, err = pcall(VR.tick, t)
  if not ok and t >= (VR.errAt or 0) then VR.errAt = t + 10000; L("step=error;msg=%s", string.sub(tostring(err), 1, 80)) end
end
W.startHooks[#W.startHooks + 1] = function()
  VR.reset()
  local s = type(W.api) == "table" and W.api.str32
  if type(s) == "function" then
    local ok, t, m, l = pcall(function() return s("Terminal"), s("MbDvcActSelectNonActiveMenu"), s(LOADOUT) end)
    if ok then VR.ids = {terminal = t, nonActive = m, loadout = l} end
  end
  if VR.creditOn() then pcall(VR.install) end
  local on = W.vehicleBreakOn == true or W.vehicleCreditOn == true or W.vehicleLoadoutOn == true
  if on then
    L("step=on;mission=%d;role=%s;break=%d;credit=%d;loadout=%d;gone=%d", W.mission or 0, role(), VR.breakOn() and 1 or 0,
      VR.creditOn() and 1 or 0, W.vehicleLoadoutOn == true and 1 or 0, G.on() and 1 or 0)
  end
end
W.pairedHooks[#W.pairedHooks + 1] = function() VR.coop, VR.stopped = W.mission, nil end
W.stopHooks[#W.stopHooks + 1] = function(final)
  if final then VR.stopped = true; pcall(VR.unguard) end
end
-- debug vehicle_gone_status (read only) carries this module's counters too: vr=<break><credit><loadout><greyed>,
-- j judged, a applied, r refused (host), q asked (guest), s Seize credits sent, c credits seen after them.
local baseStatus = G.status
if type(baseStatus) == "function" then
  G.status = function(...)
    local st = VR.st
    return string.sub(fmt("%s;vr=%d%d%d%d;j=%d;a=%d;r=%d;q=%d;s=%d;c=%d", tostring(baseStatus(...)), VR.breakOn() and 1 or 0,
      VR.creditOn() and 1 or 0, W.vehicleLoadoutOn == true and 1 or 0, VR.greyed and 1 or 0, st.judged, st.applied, st.refused, st.asked,
      st.seized, st.credited), 1, 160)
  end
end
end
end
