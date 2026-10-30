-- The guest's vehicle damage report (the vehicle fixes of 2026-10-29; original: docs/vehicles-companions-design.md 8.14,
-- docs/world-sync-protocol.md section 20; the 0.14.4 review finding game-vstate-heals-guest-damage). Spliced after
-- world_vehicle_rules.lua; it needs VEHICLE_GONE (world_vehicle.lua) and VEHICLE_STATE (world_vehicle_state.lua).
-- VEHICLE_STATE writes the host's body life over the guest's copy, so damage the guest did to its own copy short of a
-- destruction was undone at the host's next state (run 4bbbfde4: 1602 back to 2270). With lab.ini world_vehicle_damage=1
-- (CFG r[15] bit 128; lab and play, off in both unless named) on both copies, wherever VEHICLE_STATE runs:
--   guest: reads its copy's body life (VEHICLE_STATE's native read, the pool every TICK_MS) and keeps, per vehicle, the
--          life it last agreed with (its first read, any raise, every life VEHICLE_STATE wrote or found, each report). A
--          drop below that goes to the host as VEHICLE_DAMAGE (op 58: seq, id, locator hash, type, flags 1 the guest
--          rides it / 2 sent before a VEHICLE_STATE write, life, drop, xyz) when the vehicle still lives (life 1 or more:
--          a destruction is D12's), is not index 0 (M3), an LRRP truck, one this copy lost or watches, or the partner's
--          ride here (the host drives it: the host's copy takes its own damage), the guest's player stands within
--          NEAR_MM of it (D12's 300 m: a rifle or launcher range) and fired within FIRE_MS (PlayerStatus GUN_FIRE or
--          GUN_FIRE_SUPPRESSOR, sampled every frame; the lab lever vehicle_life stands in for a hit): each copy's own
--          guards hit vehicles on that copy, so a drop without the guest's fire is never sent (run 846c1147: the guards'
--          rounds on both copies added up and broke the jeep). Who hit it is still not known [I]. One report per vehicle
--          per GAP_MS (drops in between add up), RATE_N per second over all vehicles. VEHICLE_STATE asks before it writes
--          a life (S.lifeHook): an unreported drop goes out first, and a raise is skipped (logged keep) while a report is
--          in flight (HOLD_MS, until the host's state carries a life below the one before the drop) or a drop waits for
--          its gap; a drop that may not be reported is healed by VEHICLE_STATE as before.
--   host:  applies a report only while linked, to the copy VEHICLE_STATE's match names (the locator hash, or the same
--          unnamed id near the pose; never index 0), never an LRRP truck, one it lost or watches, or the vehicle its own
--          player rides (its copy takes its own damage), and only while the guest's DOWNED_STATUS position (at most
--          FRESH_MS old, as D12's judge) lies within NEAR_MM of its copy, at most HOST_N reports per vehicle and
--          HOST_ALL_N over all vehicles per second (more: refused, rate; the guest's own limits are the guest's). The
--          life becomes max(reported life, here - drop, here - floor(here / 2), 1) when that is lower than here: never
--          more than the guest's own drop, at most half of this copy's life per report (logged cap=1: a guard's hit
--          inside the guest's fire window is reported too), never 0. The lower life wins only when this copy's own
--          hit came first; AI fire on the two copies is separate rounds and adds up (run 846c1147), which is why the
--          key is off in play unless named. SetBodyLife; VEHICLE_STATE then sends that life at once (why=life). What
--          the host took is counted (D.given): D12's heavy rule (world_vehicle_rules.lua) counts own damage only.
-- A partner without op 58 (0.14.4 and older) skips it by its length; a host without the key ignores it (logged once),
-- and the guest takes the host's life again after HOLD_MS, as before.
do
local W = e.MgsCoopWorld
local S = type(W) == "table" and W.vehicleState
local G = type(W) == "table" and W.vehicleGone
if type(S) == "table" and type(G) == "table" then
local D = {}
W.vehicleDamage = D
local OP = 58
W.OP.VEHICLE_DAMAGE = OP
local fmt, floor, sqrt, max, min, H = string.format, math.floor, math.sqrt, math.max, math.min, W.handlers
local TICK_MS, GAP_MS, HOLD_MS, RATE_N, NEAR_MM, FRESH_MS, FIRE_MS = 250, 500, 3000, 8, 300000, 3000, 3000
local HOST_N, HOST_ALL_N = 3, 16
D.TICK_MS, D.GAP_MS, D.HOLD_MS, D.RATE_N, D.NEAR_MM, D.FRESH_MS, D.FIRE_MS = TICK_MS, GAP_MS, HOLD_MS, RATE_N, NEAR_MM, FRESH_MS, FIRE_MS
D.HOST_N, D.HOST_ALL_N = HOST_N, HOST_ALL_N
local function L(...)
  local ok, text = pcall(fmt, ...)
  if ok and type(W.log) == "function" then pcall(W.log, "event=vehicle_damage;" .. text) end
end
local function now() return W.now() end
local function role() return W.host and "host" or "guest" end
function D.reset()
  D.known, D.pend, D.lastTx, D.sent, D.why, D.refusedWhy, D.given, D.took, D.tookAll = {}, {}, {}, {}, {}, {}, {}, {}, {}
  D.seq, D.nextTick, D.failed, D.offLogged, D.fireAt = 0, 0, false, false, nil
  D.st = {tx = 0, keep = 0, rx = 0, applied = 0, same = 0, refused = 0}
end
D.reset()
function D.on() return W.vehicleDamageOn == true and S.on() and not D.failed end
local function linked() return W.paired == true and type(W.eff) == "number" and W.eff >= 3 end
local function fail(err)
  D.failed = true
  L("step=error;msg=%s", string.sub(tostring(err), 1, 100))
end
local function guarded(f)
  return function(...)
    local ok, err = pcall(f, ...)
    if not ok then fail(err) end
  end
end
local function hdist(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return floor(sqrt(dx * dx + dz * dz)) end
-- A live body life from a VEHICLE_STATE read (a wreck reads 65535; 0 is the destruction itself), or nil.
local function lifeOf(r, id)
  local v = type(r) == "table" and r[id]
  local life = type(v) == "table" and v[1]
  if type(life) == "number" and life >= 1 and life <= 65534 and life == floor(life) then return life end
end
local function partnerRide(id)
  local rp = W.ridePose
  local tracks = type(rp) == "table" and type(rp.fast) == "table" and rp.fast.tracks
  if type(tracks) == "table" and tracks[id] ~= nil then return true end
  local at = type(G.rides) == "table" and G.rides[id]
  return type(at) == "number" and now() - at < (G.RIDE_MS or 10000)
end

-- Guest.
-- The guest's own fire now (PlayerStatus GUN_FIRE / GUN_FIRE_SUPPRESSOR, as partner_actions.lua reads it).
function D.firing()
  local f = W.get(PlayerInfo, "OrCheckStatus")
  local a, b = W.get(PlayerStatus, "GUN_FIRE"), W.get(PlayerStatus, "GUN_FIRE_SUPPRESSOR")
  if type(f) ~= "function" or type(a) ~= "number" or type(b) ~= "number" then return false end
  local ok, yes = pcall(f, {a, b})
  return ok and yes == true
end
-- Why this copy's drop on id is not the guest's to report (nil: report it).
function D.skip(id)
  if id % 512 == 0 then return "index0" end
  if S.lrrp[id] then return "lrrp" end
  if G.gone[id] then return "gone_here" end
  if G.watch[id] then return "own_watch" end
  if partnerRide(id) then return "partner_ride" end
end
-- Reports this copy's life `life` of id when it is below the agreed one: true when sent (agreed at life, in flight).
-- A drop that may not be reported is agreed (VEHICLE_STATE heals it); one that waits (the gap, the rate) is kept.
function D.report(id, life, t, flags)
  local k = D.known[id]
  if not k or life >= k then return false end
  local why = D.skip(id)
  -- A wreck's life word reads near 65535 and still falls (run 247a4eed: 65488 -> 65484): only a live vehicle reports.
  if not why and not S.alive(id) then why = "not_alive" end
  local x, y, z = S.pose(id)
  if not why and not x then why = "pose_unreadable" end
  if not why then
    local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
    if type(px) ~= "number" or type(pz) ~= "number" then why = "position"
    elseif hdist(floor(px * 1000), floor(pz * 1000), x, z) > NEAR_MM then why = "far"
    elseif D.fireAt == nil or t - D.fireAt > FIRE_MS then why = "no_fire" end
  end
  if why then
    D.known[id] = life
    if D.why[id] ~= why then D.why[id] = why; L("step=skip;id=%d;reason=%s;life=%d;drop=%d", id, why, life, k - life) end
    return false
  end
  D.why[id] = nil
  if t - (D.lastTx[id] or t - GAP_MS) < GAP_MS then return false end
  local keep = {}
  for _, at in ipairs(D.sent) do if t - at < 1000 then keep[#keep + 1] = at end end
  D.sent = keep
  if #D.sent >= RATE_N then return false end
  if G.localVehicle() == id then flags = flags + 1 end
  local seq, drop = D.seq % 65535 + 1, min(k - life, 65535)
  if W.tx(OP, seq, id, S.hashOf[id] or 0, S.vtype(id), flags, life, drop, x, y, z) ~= true then return false end
  D.seq, D.known[id], D.lastTx[id], D.pend[id] = seq, life, t, {life = life, base = k, at = t}
  D.sent[#D.sent + 1], D.st.tx = t, D.st.tx + 1
  L("step=tx;id=%d;life=%d;drop=%d;f=%d;seq=%d", id, life, drop, flags, seq)
  return true
end
-- The pool, 32 ids per read (the native read takes 1..32).
function D.guestTick(t)
  if not linked() then return end
  for first = 0, S.pool - 1, 32 do
    local n = min(32, S.pool - first)
    local r = S.readLife(25600 + first, n)
    if type(r) ~= "table" then return end
    for i = first, first + n - 1 do
      local id = 25600 + i
      local life = lifeOf(r, id)
      if not life then D.known[id], D.pend[id] = nil, nil
      else
        local k = D.known[id]
        if k == nil or life > k then D.known[id] = life
        elseif life < k then D.report(id, life, t, 0) end
      end
    end
  end
end
-- VEHICLE_STATE (world_vehicle_state.lua S.consider) asks before it writes the host's `life` over this copy's `here`:
-- an unreported drop goes out first; true keeps this copy's lower life (a drop waiting for its gap, or a report in
-- flight that the host's state does not carry yet). Anything else (a refused send, the rate) takes the host's life.
function S.lifeHook(id, here, life, q, t)
  if W.host or not D.on() then return false end
  local k = D.known[id]
  if k and here < k then D.report(id, here, t, 2) end
  if life <= here then return false end
  -- Waiting: a drop that could not go out only because its vehicle's last report is under GAP_MS old.
  local last = D.lastTx[id]
  local p, waiting = D.pend[id], D.known[id] ~= nil and here < D.known[id] and last ~= nil and t - last < GAP_MS
  if waiting or p and t - p.at < HOLD_MS and life >= p.base then
    D.st.keep = D.st.keep + 1
    if not p or p.kept ~= life then
      if p then p.kept = life end
      L("step=keep;id=%d;here=%d;host=%d;f=%d", id, here, life, type(q) == "table" and q.flags or 0)
    end
    return true
  end
  return false
end
-- ... and after it: the life this copy now has is the one it agrees with.
function S.lifeSeen(id, life)
  if W.host or not D.on() or type(life) ~= "number" then return end
  D.known[id] = life >= 1 and life <= 65534 and life or nil
end

-- Host.
local function guestPos(t)
  local Dn = W.downed
  local r = type(Dn) == "table" and type(Dn.records) == "table" and Dn.records[2]
  if type(r) ~= "table" or type(r.pos) ~= "table" or type(r.at) ~= "number" or t - r.at > FRESH_MS then return nil end
  local x, z = r.pos[1], r.pos[3]
  if type(x) == "number" and type(z) == "number" then return x, z end
end
-- The host's own window: true while id (or all vehicles) took its limit in the last second.
local function recent(list, t)
  local keep = {}
  for _, at in ipairs(list or {}) do if t - at < 1000 then keep[#keep + 1] = at end end
  return keep
end
-- "applied" | "same" | "refused", why, this copy's id, the guest's distance (mm), the life here before, the life now, cap.
function D.apply(q, t)
  if not linked() then return "refused", "unpaired" end
  local id, why = S.match(q)
  if not id then return "refused", why end
  if S.lrrp[id] then return "refused", "lrrp", id end
  if G.gone[id] then return "refused", "gone_here", id end
  if G.watch[id] then return "refused", "own_watch", id end
  if G.localVehicle() == id then return "refused", "local_rider", id end
  local x, _, z = S.pose(id)
  if not x then return "refused", "pose_unreadable", id end
  local gx, gz = guestPos(t)
  if not gx then return "refused", "guest_unknown", id end
  local d = hdist(gx, gz, x, z)
  if d > NEAR_MM then return "refused", "guest_far", id, d end
  D.took[id], D.tookAll = recent(D.took[id], t), recent(D.tookAll, t)
  if #D.took[id] >= HOST_N or #D.tookAll >= HOST_ALL_N then return "refused", "rate", id, d end
  local here = lifeOf(S.readLife(id, 1), id)
  if not here then return "refused", "not_alive", id, d end
  local half = here - floor(here / 2)
  local life = max(q.life, here - q.drop, half, 1)
  if life >= here then return "same", nil, id, d, here, q.life end
  local took = D.took[id]
  local a = W.api
  if type(a) ~= "table" or type(a.send) ~= "function" or not pcall(a.send, id, {id = "SetBodyLife", life = life}) then
    return "refused", "send_error", id, d
  end
  S.dirty[id] = true
  took[#took + 1], D.tookAll[#D.tookAll + 1] = t, t
  D.given[id] = (D.given[id] or 0) + here - life
  return "applied", nil, id, d, here, lifeOf(S.readLife(id, 1), id) or life, life == half and max(q.life, here - q.drop) < half
end
H[OP] = guarded(function(v)
  if not W.host or type(v) ~= "table" or #v < 10 then return end
  if not D.on() then
    if not D.offLogged then D.offLogged = true; L("step=ignored;reason=off;id=%d", v[2]) end
    return
  end
  D.st.rx = D.st.rx + 1
  if not S.named and (not S.namesAt or now() - S.namesAt >= 5000) then S.buildNames() end
  local q = {seq = v[1], id = v[2], hash = v[3], vtype = v[4], flags = v[5], life = v[6], drop = v[7], x = v[8], y = v[9], z = v[10]}
  local verdict, why, id, d, was, life, cap = D.apply(q, now())
  if verdict == "refused" then
    D.st.refused = D.st.refused + 1
    local key = id or q.id
    if D.refusedWhy[key] ~= why then
      D.refusedWhy[key] = why
      L("step=refused;id=%d;reason=%s;d_mm=%d;life=%d;drop=%d;f=%d", key, why, d or -1, q.life, q.drop, q.flags)
    end
    return
  end
  D.refusedWhy[id] = nil
  if verdict == "same" then D.st.same = D.st.same + 1 else D.st.applied = D.st.applied + 1 end
  L("step=%s;id=%d;from=%d;life=%d;was=%d;drop=%d;d_mm=%d;f=%d%s", verdict, id, q.id, life, was, q.drop, d, q.flags, cap and ";cap=1" or "")
end)

W.startHooks[#W.startHooks + 1] = guarded(function()
  D.reset()
  if D.on() then L("step=on;mission=%d;role=%s", W.mission or 0, role()) end
end)
W.stopHooks[#W.stopHooks + 1] = function(final)
  if final then D.known, D.pend, D.lastTx = {}, {}, {} end
end
-- The lab lever vehicle_life on the guest (world_vehicle_state.lua, lab only) stands in for the guest's own hit.
local baseLever = S.lever
if type(baseLever) == "function" then
  S.lever = function(a, ...)
    if not W.host and D.on() and type(a) == "table" and a[1] == "vehicle_life" then D.fireAt = now(); L("step=lever_hit") end
    return baseLever(a, ...)
  end
end
local tick = W.tick
function W.tick(...)
  if tick then tick(...) end
  if W.host or not D.on() then return end
  local t = now()
  if D.firing() then D.fireAt = t end
  if t < D.nextTick then return end
  D.nextTick = t + TICK_MS
  local ok, err = pcall(D.guestTick, t)
  if not ok then fail(err) end
end
end
end
