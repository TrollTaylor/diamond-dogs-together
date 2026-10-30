-- The user's vehicle decisions of 2026-10-29 (original; docs/vehicles-companions-design.md section 10 D11, D12, D14,
-- results in sections 8.11 and 8.12; docs/world-sync-protocol.md section 19). Spliced after world_vehicle_state.lua (its
-- VEHICLE_GONE and VEHICLE_STATE helpers); world_relief_sync.lua, later, still owns index 0 (the sortie vehicle).
-- Three keys in the CFG r[15] vehicle bitmask (world_sync.lua), on in play unless named, off in the lab unless named:
--   D11 world_loadout_block (bit 32): the guest's iDroid supply drops that can bring a vehicle, LOADOUT
--       (MSN_DROP_LOADOUT, NOTE 13 a 5) and, since the user's pick of the review (only the host picks vehicles), VEHICLES
--       (MSN_DROP_VEHICLE, NOTE 13 a 4), are inactive while the guest is paired in co-op (any mission world sync runs),
--       and inside that mission across a link drop until its final stop; a host-led follow's arrival (world_follow.lua)
--       marks the mission co-op before PAIRED, a mark that lapses PREPAIR_MS later without PAIRED (logged
--       prepair_lapsed: the host's game failed or quit while loading); selecting one logs the refusal and shows its
--       toast. Whatever world_relief says: this module then owns the VEHICLES item (VR.ownsVehicleDrop) and
--       world_relief_sync.lua leaves it alone. The host's iDroid and every other item stay as they are.
--   D12 world_vehicle_break (bit 8; with VEHICLE_GONE): a vehicle that breaks on the guest's copy without attribution
--       (the game posts no Damage message for a vehicle hit, 35ba7929) is requested as a destruction with cause 4
--       (REQ_VEHICLE_GONE; never index 0; never a break the guest's own copy caused; at most 6 asked per 60 s, more wait
--       in a queue of 32). A host without its own key answers cause 4 with REJECT 11 (world_vehicle.lua). The host judges
--       every guest destruction request: refused at once while its own player sits in its copy (ridden); any other
--       filled seat (D-Dog, a soldier) or the partner's ride is VEHICLE_GONE's hold (re-checked every 250 ms, REJECT 9
--       after 120 s); then applied only when the guest's player (its DOWNED_STATUS position, world_downed.lua, at most
--       3 s old: the closer of the one when the request arrived and the current one) stands within 300 m of the pose the
--       guest reported (a launcher or rifle range), or the host's own copy is down to a quarter of the highest body life
--       it read; held while no position is known; at most 4 applied per 20 s, more wait; an unnamed copy (no Despawn
--       outside Mother Base) is held (blast_near) while a player stands within its blast distance. Applied: the host's copy
--       becomes the same wreck (SetBodyLife 0, the engine's own destruction; read back, the Despawn if it does not
--       take), unless the host's own player or the partner (its DOWNED_STATUS position) stands within the blast
--       distance of the host's copy, or a position cannot be read: then a Despawn, logged break_safe (run 7762536a: the
--       wreck's later explosion, about 8 s after SetBodyLife 0, hit the host beside it for 3500 of 7800 life). The blast
--       distance (VR.blastMm) is BLAST_MM for a light vehicle (that jeep) and HEAVY_BLAST_MM for every other type, an
--       unknown type or a missing Vehicle.type binding (run ac0f7656: the armoured vehicle's own destruction threw the
--       guest flat 25 m away; the review of 2026-10-29). Refused: REJECT 8; the
--       guest's copy keeps its wreck (no engine command undoes one: SetBodyLife writes the life word only, IsAlive
--       reads rec+0x70 bits nothing clears), logged break_refused.
--       D12 crew (world_vehicle_crew, bit 64; the user's pick of 2026-10-29: wreck at once): a hold whose only reason is
--       an NPC crew (every filled seat a soldier: world sync's roster or a TppSoldier2 id; never a player, D-Dog, a
--       buddy or the partner's ride) is judged at once and applied as the wreck, so both copies end with one; the
--       engine's blast decides the crew on the host's copy (their deaths are host outcomes, synced as usual; on the
--       guest's copy they died unattributed already, so nothing counts twice). Held (crew_near) while a player stands
--       within the blast distance of the host's copy: a Despawn under seated soldiers is untested. Logged break_crew.
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
local BREAK_MM, FRESH_MS, RATE_N, RATE_MS, ASK_N, ASK_MS, QUEUE_N = 300000, 3000, 4, 20000, 6, 60000, 32
local OWN_MS, CREDIT_MS, GREY_MS, TOAST_MS, ARRIVAL_MS, UNATTRIBUTED, BLAST_MM = 5000, 10000, 2000, 3000, 130000, 4, 15000
VR.BREAK_MM, VR.FRESH_MS, VR.RATE_N, VR.RATE_MS, VR.ASK_N, VR.QUEUE_N, VR.CREDIT_MS = BREAK_MM, FRESH_MS, RATE_N, RATE_MS, ASK_N, QUEUE_N, CREDIT_MS
local PREPAIR_MS, HEAVY_BLAST_MM = 300000, 50000
VR.UNATTRIBUTED, VR.BLAST_MM, VR.HEAVY_BLAST_MM, VR.PREPAIR_MS = UNATTRIBUTED, BLAST_MM, HEAVY_BLAST_MM, PREPAIR_MS
-- The guest's iDroid drop items this module sets inactive: name, NOTE 13 a (the toast), log path.
local ITEMS = {{"MSN_DROP_LOADOUT", 5, "idroid_drop_loadout"}, {"MSN_DROP_VEHICLE", 4, "idroid_drop_vehicle"}}
local function L(...)
  local ok, text = pcall(fmt, ...)
  if ok and type(W.log) == "function" then pcall(W.log, "event=vehicle_rules;" .. text) end
end
local function now() return W.now() end
local function role() return W.host and "host" or "guest" end
function VR.breakOn() return W.vehicleBreakOn == true and G.on() end
-- world_vehicle.lua's REQ handler applies a cause 4 request only while this key runs on the host.
G.breakOn = VR.breakOn
function VR.creditOn() return W.vehicleCreditOn == true and G.on() end
function VR.loadoutOn() return W.vehicleLoadoutOn == true and W.active == true and not W.host end
function VR.crewOn() return W.vehicleCrewOn == true and VR.breakOn() end
-- The guest's VEHICLES drop item is this module's while its key runs (world_relief_sync.lua asks).
function VR.ownsVehicleDrop() return W.vehicleLoadoutOn == true and not W.host end
function VR.reset()
  VR.applied, VR.asked, VR.queue, VR.peak, VR.credits, VR.own, VR.unatt, VR.arrival = {}, {}, {}, {}, {}, {}, {}, {}
  VR.st = {judged = 0, applied = 0, refused = 0, asked = 0, seized = 0, credited = 0}
  VR.nextTick, VR.greyAt, VR.greyLogged, VR.toastAt = 0, nil, nil, {}
end
VR.reset()
-- Entries of list newer than ms (oldest first).
local function window(list, t, ms)
  local keep = {}
  for _, at in ipairs(list) do if t - at < ms then keep[#keep + 1] = at end end
  return keep
end
local function dist(x, z, q) return floor(sqrt((x - q.x) ^ 2 + (z - q.z) ^ 2)) end

-- D12, guest: world_vehicle.lua V.observe asks this for a VehicleBroken it cannot attribute.
function G.unattributed(id, t)
  if W.host or not VR.breakOn() or id % 512 == 0 then return false end
  -- A break this copy caused itself (its own wreck or removal) is never a loss to request.
  if VR.own[id] and t - VR.own[id] < OWN_MS then return false end
  VR.unatt[id] = true
  L("step=break_seen;id=%d", id)
  return true
end
-- D12, guest: the request for an unattributed break goes out as cause 4, at most ASK_N per ASK_MS; more wait in a queue
-- (QUEUE_N; beyond it a break is skipped, logged) and go out in order as the window frees. Retries pass straight through.
local baseRequest = G.request
local function ask(id, w, t)
  VR.asked[#VR.asked + 1], VR.st.asked, w.asked = t, VR.st.asked + 1, true
  return baseRequest(id, w, t)
end
G.request = function(id, w, t, ...)
  if W.host or type(w) ~= "table" or w.asked then return baseRequest(id, w, t, ...) end
  if w.cause == 2 and VR.unatt[id] then VR.unatt[id], w.unatt, w.cause = nil, true, UNATTRIBUTED end
  if not w.unatt then return baseRequest(id, w, t, ...) end
  VR.asked = window(VR.asked, t, ASK_MS)
  if #VR.asked < ASK_N and #VR.queue == 0 then return ask(id, w, t) end
  if #VR.queue >= QUEUE_N then L("step=break_skip;id=%d;reason=queue_full", id); return end
  VR.queue[#VR.queue + 1] = {id = id, w = w, at = t}
  L("step=break_queued;id=%d;n=%d", id, #VR.queue)
end
local function drain(t)
  if W.host or #VR.queue == 0 then return end
  VR.asked = window(VR.asked, t, ASK_MS)
  while #VR.queue > 0 and #VR.asked < ASK_N do
    local q = table.remove(VR.queue, 1)
    L("step=break_ask;id=%d;queued_ms=%d", q.id, floor(t - q.at))
    ask(q.id, q.w, t)
  end
end
-- D12, host: the guest's player position from its DOWNED_STATUS (mm, at most FRESH_MS old), or nil.
function VR.guestPos(t)
  local D = W.downed
  local r = type(D) == "table" and type(D.records) == "table" and D.records[2]
  if type(r) ~= "table" or type(r.pos) ~= "table" or type(r.at) ~= "number" or t - r.at > FRESH_MS then return nil end
  local x, z = r.pos[1], r.pos[3]
  if type(x) == "number" and type(z) == "number" then return x, z end
end
-- D12, host: where the guest stood when its request arrived (by the guest's id; the first of a repeated request): a
-- request held a while (a seat, the partner's ride) is judged against the closer of that and the current position.
local REQ, REJ = W.OP.REQ_VEHICLE_GONE or 47, W.OP.REJECT or 4
local function arrived(v)
  if not W.host or type(v) ~= "table" or type(v[2]) ~= "number" then return end
  local t = now()
  local x, z = VR.guestPos(t)
  local a = VR.arrival[v[2]]
  if x and (a == nil or t - a.at > ARRIVAL_MS) then VR.arrival[v[2]] = {x = x, z = z, at = t} end
end
local baseReq = H[REQ]
H[REQ] = function(v, ...)
  pcall(arrived, v)
  if baseReq then return baseReq(v, ...) end
end
-- This copy's body life of id and the highest it read this mission (VEHICLE_STATE's native read), or nil.
function VR.life(id)
  local S = W.vehicleState
  if type(S) ~= "table" or type(S.readLife) ~= "function" then return nil end
  local r = S.readLife(id, 1)
  local v = type(r) == "table" and r[id]
  if type(v) == "table" and type(v[1]) == "number" and v[1] < 65535 then return v[1], VR.peak[id] end
end
-- q: the guest's request (q.x, q.z in mm: the guest's copy where it broke). true | false | nil (no position), why,
-- d_mm, life, peak.
function VR.plausible(q, t)
  local gx, gz = VR.guestPos(t)
  local d = gx and dist(gx, gz, q) or nil
  local a = VR.arrival[q.from or q.id]
  if a then
    local da = dist(a.x, a.z, q)
    if d == nil or da < d then d = da end
  end
  local life, peak = VR.life(q.id)
  local heavy = life ~= nil and peak ~= nil and peak > 0 and life * 4 <= peak
  if d ~= nil and d <= BREAK_MM then return true, "near", d, life, peak end
  if heavy then return true, "heavy", d, life, peak end
  if d ~= nil then return false, "guest_far", d, life, peak end
  return nil, "guest_unknown", d, life, peak
end
-- One judge line per request, at its outcome (applied or refused); a wait is a break_hold line, once per reason.
local function judged(q, ok)
  VR.st.judged = VR.st.judged + 1
  if not ok then VR.st.refused = VR.st.refused + 1 end
  L("step=break_judge;id=%d;rid=%d;ok=%d;why=%s;d_mm=%d;life=%d;peak=%d", q.id, q.rid or 0, ok and 1 or 0, q.pwhy, q.pd or -1,
    q.plife or -1, q.ppeak or -1)
end
local function held(q, why, extra)
  if q.vrHold == why then return end
  q.vrHold = why
  L("step=break_hold;id=%d;rid=%d;why=%s%s", q.id, q.rid or 0, why, extra or "")
end
-- D12 crew: a rider id is an NPC soldier when world sync's roster names it (W.byId) or its type index is TppSoldier2.
function VR.soldier(v)
  if type(W.byId) == "table" and W.byId[v] ~= nil then return true end
  local f, want = W.get(W.get(_G, "GameObject"), "GetTypeIndex"), W.get(W.get(_G, "TppGameObject"), "GAME_OBJECT_TYPE_SOLDIER2")
  if type(f) ~= "function" or type(want) ~= "number" then return false end
  local ok, t = pcall(f, v)
  return ok and t == want
end
-- The number of filled seats of this copy's id when every one is an NPC soldier and the partner does not ride it, else
-- nil (an empty, unreadable or other rider keeps VEHICLE_GONE's hold).
function VR.crew(id)
  local a = W.api
  local ok, r = pcall(a.send, id, {id = "GetRiderId"})
  if not ok or type(r) ~= "table" then return nil end
  local n = 0
  for _, v in pairs(r) do
    if type(v) ~= "number" then return nil end
    if v ~= 65535 then
      if not VR.soldier(v) then return nil end
      n = n + 1
    end
  end
  local rp = W.ridePose
  local tracks = type(rp) == "table" and type(rp.fast) == "table" and rp.fast.tracks
  if n == 0 or type(tracks) == "table" and tracks[id] ~= nil or G.rides[id] and now() - G.rides[id] < G.RIDE_MS then return nil end
  return n
end
local baseDecide = G.decide
G.decide = function(q, ...)
  local verdict, why = baseDecide(q, ...)
  if not (W.host and q.src == "req" and q.cause == 2 and VR.breakOn()) then return verdict, why end
  -- Never under the host's own player (a copy that broke there too was answered as gone above). Any other filled seat
  -- (D-Dog, a soldier) or the partner's ride stays VEHICLE_GONE's hold until the seat is free (REJECT 9 after 120 s).
  if verdict == "hold" and why == "local_rider" then
    VR.st.refused = VR.st.refused + 1
    L("step=break_judge;id=%d;rid=%d;ok=0;why=ridden;by=%s", q.id, q.rid or 0, why)
    return "refuse", "ridden"
  end
  -- D12 crew: an NPC crew alone no longer holds; a player within the blast distance of the host's copy does.
  if verdict == "hold" and why == "occupied" and VR.crewOn() then
    local n = VR.crew(q.id)
    if n then
      local who, d, b = VR.nearBlast(q.id, now())
      if who then held(q, "crew_near", fmt(";near=%s;d_mm=%d;crew=%d;blast_mm=%d", who, d or -1, n, b)); return "hold", "crew_near" end
      q.crew, verdict = n, "apply"
    end
  end
  if verdict ~= "apply" then return verdict, why end
  local t = now()
  if q.ok == nil then
    local ok, pwhy, d, life, peak = VR.plausible(q, t)
    if ok == nil then held(q, pwhy); return "hold", pwhy end
    q.ok, q.pwhy, q.pd, q.plife, q.ppeak = ok, pwhy, d, life, peak
    if not ok then judged(q, false); return "refuse", pwhy end
  end
  if not q.ok then return "refuse", q.pwhy end
  -- An unnamed copy has no Despawn outside Mother Base (VEHICLE_GONE: no_locator, REJECT 8): a player within the blast
  -- holds it (blast_near) until clear, never the break_safe Despawn that cannot run (index 0 is VEHICLE_GONE's refusal).
  if q.named == nil then q.named = G.locator(q.id) ~= nil or W.mission == 30050 end
  if not q.named and q.id % 512 ~= 0 then
    local who, d, b = VR.nearBlast(q.id, t)
    if who then held(q, "blast_near", fmt(";near=%s;d_mm=%d;blast_mm=%d", who, d or -1, b)); return "hold", "blast_near" end
  end
  VR.applied = window(VR.applied, t, RATE_MS)
  if #VR.applied >= RATE_N then held(q, "rate", fmt(";n=%d", #VR.applied)); return "hold", "rate" end
  VR.applied[#VR.applied + 1], VR.st.applied = t, VR.st.applied + 1
  judged(q, true)
  if q.crew then L("step=break_crew;id=%d;rid=%d;crew=%d", q.id, q.rid or 0, q.crew) end
  return "apply"
end

-- D12, host: the blast distance of this copy of id (mm): BLAST_MM for a light vehicle (Vehicle.type EASTERN_ or
-- WESTERN_LIGHT_VEHICLE, compared by name), HEAVY_BLAST_MM for any other type, an unknown one or no binding.
function VR.blastMm(id)
  local types, t = W.get(W.get(_G, "Vehicle"), "type"), G.vtype(id)
  if type(types) == "table" and type(t) == "number" and t ~= 255 and
    (t == W.get(types, "EASTERN_LIGHT_VEHICLE") or t == W.get(types, "WESTERN_LIGHT_VEHICLE")) then return BLAST_MM end
  return HEAVY_BLAST_MM
end
-- D12, host: who stands within the blast distance of this copy of id now: "host" (its own player), "partner" (the
-- guest's fresh DOWNED_STATUS position: the host's blast can reach body 1 too), "unknown" (the copy's or the host's
-- position cannot be read), or nil (nobody: the wreck is safe to run); then the distance and the blast distance used.
function VR.nearBlast(id, t)
  local b = VR.blastMm(id)
  local x, _, z = G.pose(id)
  if type(x) ~= "number" or type(z) ~= "number" then return "unknown", -1, b end
  local q = {x = x, z = z}
  local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
  if type(px) ~= "number" or type(pz) ~= "number" then return "unknown", -1, b end
  local d = dist(px * 1000, pz * 1000, q)
  if d <= b then return "host", d, b end
  local gx, gz = VR.guestPos(t)
  if gx then
    d = dist(gx, gz, q)
    if d <= b then return "partner", d, b end
  end
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
  if type(id) ~= "number" then return false, "args" end
  local t = now()
  VR.own[id] = t
  if id % 512 == 0 then return baseRemove(id, cause, ...) end
  -- D12: the host applies a guest's destruction as the same wreck on its own copy, or as a Despawn while a player
  -- stands within the blast (break_safe).
  if cause == 2 and W.host and VR.breakOn() then
    local who, d, b = VR.nearBlast(id, t)
    if who then
      L("step=break_safe;id=%d;near=%s;d_mm=%d;form=despawn;blast_mm=%d", id, who, d or -1, b)
      return G.despawn(id)
    end
    return form(id, {id = "SetBodyLife", life = 0}, "wreck", cause, ...)
  end
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
-- D12, guest: the host refused its unattributed destruction request (REJECT for a cause 4 REQ_VEHICLE_GONE).
local baseReject = H[REJ]
H[REJ] = function(v, ...)
  local q = not W.host and type(v) == "table" and v[2] == REQ and G.req[v[1]]
  if baseReject then baseReject(v, ...) end
  if type(q) == "table" and type(q.w) == "table" and q.w.unatt then
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

-- D11, guest: the drop items inactive (TppUiCommand.SetMbTopMenuItemActive; TppTerminal.SetUp at a mission start leaves
-- every item active, so the guard repeats every 2 s while the iDroid is closed; the first pass of a mission applies even
-- while it is open). Unguard sets them active again: the state SetUp leaves.
local function menu(on)
  local f = W.get(TppUiCommand, "SetMbTopMenuItemActive")
  if type(f) ~= "function" then return false end
  local all = true
  for _, it in ipairs(ITEMS) do if pcall(f, it[1], on) ~= true then all = false end end
  return all
end
function VR.coopNow() return W.paired == true or VR.coop ~= nil and VR.coop == W.mission and not VR.stopped end
function VR.unguard()
  if not VR.greyed then return end
  VR.greyed = nil
  menu(true)
  for _, it in ipairs(ITEMS) do L("step=unguard;path=%s;mission=%d", it[3], W.mission or 0) end
end
local function guard(t)
  -- The pre-pair mark (an arrival without PAIRED yet) lapses after PREPAIR_MS: the guest plays this mission alone.
  if VR.prepairAt and t - VR.prepairAt >= PREPAIR_MS then
    VR.prepairAt = nil
    if W.paired ~= true then VR.stopped = true; L("step=prepair_lapsed;mission=%d;ms=%d", W.mission or 0, PREPAIR_MS) end
  end
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
      for _, it in ipairs(ITEMS) do L("step=guard;path=%s;via=menu_inactive;mission=%d", it[3], W.mission or 0) end
    end
  end
end
-- Terminal MbDvcActSelectNonActiveMenu (arg: StrCode32 of the item) for an item this module greys: its own toast at most
-- every 3 s.
function VR.refuse(it)
  L("step=refused;path=%s;via=menu_inactive;mission=%d", it[3], W.mission or 0)
  local t = now()
  if t >= (VR.toastAt[it[1]] or 0) then
    VR.toastAt[it[1]] = t + TOAST_MS
    if type(W.note) == "function" then pcall(W.note, 13, it[2], 0) end
  end
end
local observe = W.observe
if type(observe) == "function" then
  function W.observe(class, msg, a0, ...)
    local ids = VR.ids
    if VR.greyed and ids and class == ids.terminal and msg == ids.nonActive then
      for i, it in ipairs(ITEMS) do if a0 == ids.items[i] then pcall(VR.refuse, it) end end
    end
    return observe(class, msg, a0, ...)
  end
end

function VR.tick(t)
  if t < VR.nextTick then return end
  VR.nextTick = t + 250
  guard(t)
  creditTick(t)
  drain(t)
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
  -- D11 pre-pair gap (0.14.4 review): a host-led follow into this mission (world_follow.lua's arrival) makes it co-op at
  -- once, so the guest's items are inactive before PAIRED; the last mission's final stop no longer carries over.
  local F = W.follow
  VR.prepairAt = nil
  if not W.host and type(F) == "table" and F.arrivedMission ~= nil and F.arrivedMission == W.mission then
    VR.coop, VR.stopped, VR.prepairAt = W.mission, nil, W.paired ~= true and now() or nil
  end
  local s = type(W.api) == "table" and W.api.str32
  if type(s) == "function" then
    local ok, ids = pcall(function()
      local r = {terminal = s("Terminal"), nonActive = s("MbDvcActSelectNonActiveMenu"), items = {}}
      for i, it in ipairs(ITEMS) do r.items[i] = s(it[1]) end
      return r
    end)
    if ok then VR.ids = ids end
  end
  if VR.creditOn() then pcall(VR.install) end
  local on = W.vehicleBreakOn == true or W.vehicleCreditOn == true or W.vehicleLoadoutOn == true
  if on then
    L("step=on;mission=%d;role=%s;break=%d;credit=%d;loadout=%d;gone=%d%s", W.mission or 0, role(), VR.breakOn() and 1 or 0,
      VR.creditOn() and 1 or 0, W.vehicleLoadoutOn == true and 1 or 0, G.on() and 1 or 0, W.vehicleCrewOn == true and ";crew=1" or "")
  end
end
W.pairedHooks[#W.pairedHooks + 1] = function() VR.coop, VR.stopped, VR.prepairAt = W.mission, nil, nil end
W.stopHooks[#W.stopHooks + 1] = function(final)
  if final then VR.stopped, VR.queue, VR.prepairAt = true, {}, nil; pcall(VR.unguard) end
end
-- debug vehicle_gone_status (read only) carries this module's counters too: vr=<break><credit><loadout><greyed>,
-- j judged, a applied, r refused (host), q asked (guest), s Seize credits sent, c credits seen after them. The base
-- part is cut to fit 160 characters, never the counters.
local baseStatus = G.status
if type(baseStatus) == "function" then
  G.status = function(...)
    local st = VR.st
    local tail = fmt(";vr=%d%d%d%d;j=%d;a=%d;r=%d;q=%d;s=%d;c=%d", VR.breakOn() and 1 or 0, VR.creditOn() and 1 or 0,
      W.vehicleLoadoutOn == true and 1 or 0, VR.greyed and 1 or 0, st.judged, st.applied, st.refused, st.asked, st.seized, st.credited)
    return string.sub(tostring(baseStatus(...)), 1, 160 - #tail) .. tail
  end
end
end
end
