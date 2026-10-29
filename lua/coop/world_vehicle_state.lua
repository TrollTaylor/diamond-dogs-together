-- M4 VEHICLE_STATE (docs/vehicles-companions-design.md 6.4; op 51 = 0x33, host to guest, reliable,
-- world queue, level 3; docs/world-sync-protocol.md section 17). Vehicles nobody drives were per copy:
-- a jeep pushed, left or shot on the host's copy stayed untouched on the guest's (STATUS 7561ab63).
-- With lab.ini world_vehicle_state=1 (CFG r[15] bit 2; off in the lab, on in play unless named) on both copies
-- (30010, 30020; Mother Base 30050 in the lab only until a hidden run covers it, M4 review):
--   host:  scans its TppVehicle2 pool, SLICE ids every TICK_MS (a 24-vehicle pool in 1.5 s), plus at
--          once any id a Damage message names: IsAlive, GetPosition (pos, rotY), GetVehicleType and the
--          native read MGS_COOP_VEHICLE_LIFE_V1 (body life, zero-parts mask, the engine's ene_veh_state
--          word). It sends a live vehicle's state when it moved > 0.5 m / 5 deg or its life, state word
--          or parts changed (at most every MIN_GAP_MS per vehicle), all of them once after each PAIRED
--          (the sweep, flag 2) and each again after REFRESH_MS (flag 4: corrects a guest-local push).
--          Skipped (logged once per reason change): the local player's vehicle, an occupied rider[] seat
--          (an NPC, the local passenger), a vehicle the partner drives here (a RIDE_FAST track latched
--          10 s: world_vehicle.lua V.busy; the guest's seat chain never writes rider[]), an authored LRRP
--          truck (lrrpVehicle in f30010/f30020_enemy.lua: each copy's own AI drives it until M8).
--   guest: applies it only while linked, to the vehicle its locator hash names here (veh_lv_/trc_/wav_
--          and the Mother Base veh_clNN_clNN_0000), or for an unnamed one (never index 0, the relief
--          vehicle: M3) the same id when it is alive, of that type and the nearest of its type to the
--          reported position within 40 m (a named one too must lie within 40 m: far); never an LRRP
--          truck, one this copy lost or is watching (VEHICLE_GONE), and only while nobody rides it here
--          (V.busy) and, for a move, the local player stands more than 3.5 m from where it would go and
--          from where it stands: otherwise held (re-checked every tick) for HOLD_MS.
--          SetPosition{position, rotY} when it is > 0.5 m / 5 deg off (read back: err_mm; a failed
--          placement is refused place_failed), SetBodyLife{life} whenever this copy's body life differs
--          (host-authoritative both ways, so guest-only hits never add up to a guest-only destruction;
--          a host life of 0 is written as 1: destruction is VEHICLE_GONE's).
-- Lab levers (lab mode only; native refuses them outside the one-PC lab): debug vehicle_push <id|nearest>
-- <dx> <dz> (this copy's vehicle moved, a stand-in for a shove), debug vehicle_life <id|nearest> <life>
-- (this copy's body life; 0 is the E5 wreck attempt); debug vehicle_state_status [id] is read-only.
do
local W = e.MgsCoopWorld
if type(W) == "table" then
local S = {}
W.vehicleState = S
local OP = 51
W.OP.VEHICLE_STATE = OP
local fmt, floor, sqrt, abs, H = string.format, math.floor, math.sqrt, math.abs, W.handlers
local NULL, UNKNOWN = 65535, 65535
local TICK_MS, SLICE, MIN_GAP_MS, REFRESH_MS, HOLD_MS = 250, 4, 500, 10000, 10000
local MOVE_MM, YAW_MRAD, FAR_MM, NEAR_PLAYER_MM, LEVER_M = 500, 87, 40000, 3500, 60
S.TICK_MS, S.SLICE, S.MIN_GAP_MS, S.REFRESH_MS, S.HOLD_MS = TICK_MS, SLICE, MIN_GAP_MS, REFRESH_MS, HOLD_MS
S.MOVE_MM, S.YAW_MRAD, S.FAR_MM, S.NEAR_PLAYER_MM = MOVE_MM, YAW_MRAD, FAR_MM, NEAR_PLAYER_MM
local MISSIONS = {[30010] = true, [30020] = true, [30050] = true}
-- The authored LRRP trucks (lrrpVehicle, f30010_enemy.lua:582-725, f30020_enemy.lua:618-752).
local LRRP = {[30010] = {0, 1, 2, 3, 4, 5}, [30020] = {0, 1, 2, 3, 4, 5, 6}}
local MB = {"01_cl00", "02_cl00", "03_cl00", "04_cl00", "05_cl00", "06_cl00", "00_cl04", "00_cl02", "00_cl03", "00_cl01", "00_cl05", "00_cl06"}
local function integer(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
local function L(...)
  local ok, text = pcall(fmt, ...)
  if ok and type(W.log) == "function" then pcall(W.log, text) end
end
local function now() return type(W.now) == "function" and W.now() or 0 end
local function G() return type(W.vehicleGone) == "table" and W.vehicleGone or nil end
function S.reset()
  S.last, S.skipWhy, S.dirty, S.pending, S.outcome, S.types = {}, {}, {}, {}, {}, {}
  S.nameOf, S.idOfHash, S.hashOf, S.lrrp, S.named = {}, {}, {}, {}, nil
  S.cursor, S.pool, S.seq, S.nextTick, S.failed, S.offLogged, S.sweepUntil, S.sweepSent = 0, 32, 0, 0, false, false, nil, 0
  S.stats = {tx = 0, sweep = 0, refresh = 0, rx = 0, applied = 0, moved = 0, life = 0, same = 0, held = 0, refused = 0, skip = 0}
end
S.reset()
-- Mother Base only in the lab (mode 1): no hidden run has covered 30050 (M4 review).
local function mission() return MISSIONS[W.mission] == true and (W.mission ~= 30050 or W.mode == 1) end
function S.on() return W.vehicleStateOn == true and W.active == true and mission() and not S.failed end
local function fail(err)
  S.failed = true
  L("event=vehicle_state;step=error;msg=%s", string.sub(tostring(err), 1, 100))
end
local function guarded(f)
  return function(...)
    local ok, err = pcall(f, ...)
    if not ok then fail(err) end
  end
end
local function linked() return W.paired == true and type(W.eff) == "number" and W.eff >= 3 end
local function send(target, cmd)
  local a = W.api
  if type(a) ~= "table" or type(a.send) ~= "function" then return nil end
  local ok, r1, r2 = pcall(a.send, target, cmd)
  if ok then return r1, r2 end
end
local function milli(v)
  if type(v) ~= "number" or v ~= v then return nil end
  local n = floor(v * 1000 + 0.5)
  if n < -1000000000 or n > 1000000000 then return nil end
  return n
end
-- Pose in mm and the heading in mrad (GetPosition's second return, radians).
function S.pose(id)
  local p, rot = send(id, {id = "GetPosition"})
  if p == nil then return nil end
  local ok, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end)
  if not ok then return nil end
  x, y, z = milli(x), milli(y), milli(z)
  local yaw = type(rot) == "number" and milli(rot) or 0
  if not yaw or yaw < -31416 or yaw > 31416 then yaw = 0 end
  if x and y and z then return x, y, z, yaw end
end
function S.alive(id) local g = G(); return g ~= nil and g.alive(id) == true end
function S.vtype(id)
  if not S.types[id] then local g = G(); S.types[id] = g and g.vtype(id) or 255 end
  return S.types[id]
end
local function hdist(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return floor(sqrt(dx * dx + dz * dz)) end
local function yawDiff(a, b)
  local d = abs(a - b) % 6283
  return d > 3142 and 6283 - d or d
end
-- This copy's authored names: id <-> StrCode32 of the locator (veh_lv_/trc_/wav_, the Mother Base
-- veh_clNN_clNN_0000) and the LRRP trucks. Built once per mission (again while none resolved).
function S.buildNames()
  local a = W.api
  if type(a) ~= "table" or type(a.id) ~= "function" or type(W.h) ~= "function" then return end
  local names = {}
  for _, c in ipairs({{"lv", 32}, {"trc", 16}, {"wav", 16}}) do
    for k = 0, c[2] - 1 do names[#names + 1] = fmt("veh_%s_%04d", c[1], k) end
  end
  if W.mission == 30050 then for _, m in ipairs(MB) do names[#names + 1] = "veh_cl" .. m .. "_0000" end end
  local n = 0
  for _, name in ipairs(names) do
    local ok, id = pcall(a.id, "TppVehicle2", name)
    if ok and integer(id, 25600, 26111) then
      local okH, h = pcall(W.h, name)
      if okH and integer(h, 1, 4294967295) then S.nameOf[id], S.hashOf[id], S.idOfHash[h], n = name, h, id, n + 1 end
    end
  end
  for _, k in ipairs(LRRP[W.mission] or {}) do
    local ok, id = pcall(a.id, "TppVehicle2", fmt("veh_trc_%04d", k))
    if ok and integer(id, 25600, 26111) then S.lrrp[id] = true end
  end
  S.named = n > 0 and n or nil
  S.namesAt = now()
end
-- The native read: id -> {life, zero, parts, state} for [first, first + n), nil when refused.
function S.readLife(first, n)
  if type(W.receipt) ~= "function" then return nil end
  local ok, r = pcall(W.receipt, fmt("return 'MGS_COOP_VEHICLE_LIFE_V1' -- %d %d", first, n))
  if not ok or type(r) ~= "table" or r.refused ~= nil then
    if ok and type(r) == "table" and r.refused ~= S.lifeRefused then S.lifeRefused = r.refused; L("event=vehicle_state;step=life_refused;reason=%s", tostring(r.refused)) end
    return nil
  end
  if integer(r.c, 1, 512) then S.pool = math.min(r.c, 64) end
  return r
end
local function row(t, id)
  local v = type(t) == "table" and t[id]
  if type(v) == "table" and integer(v[1], 0, 65535) and integer(v[2], 0, 0xFFE) and integer(v[3], 0, 12) and integer(v[4], 0, 0x3FF) then return v end
end
-- Why this copy must not take or send `id`'s state now (nil: free).
function S.busy(id)
  if S.lrrp[id] then return "lrrp" end
  local g = G()
  if not g then return "no_module" end
  if g.gone[id] then return "gone_here" end
  if g.watch[id] then return "own_watch" end
  return g.busy(id)
end

-- Host.
local function snapshot(id, t, life)
  local x, y, z, yaw = S.pose(id)
  if not x then return nil end
  local v = row(life, id)
  return {id = id, x = x, y = y, z = z, yaw = yaw, life = v and v[1] or UNKNOWN, zero = v and v[2] or 0, parts = v and v[3] or 0,
    sv = v and v[4] or 1, at = t}
end
function S.hostCheck(id, t, life)
  if not S.alive(id) then S.last[id], S.skipWhy[id] = nil, nil; return end
  local why = S.busy(id)
  if why then
    if S.skipWhy[id] ~= why then
      S.skipWhy[id] = why
      S.stats.skip = S.stats.skip + 1
      L("event=vehicle_state;step=skip;reason=%s;id=%d", why, id)
    end
    return
  end
  if S.skipWhy[id] then L("event=vehicle_state;step=resume;id=%d;was=%s", id, S.skipWhy[id]); S.skipWhy[id] = nil end
  local s = snapshot(id, t, life)
  if not s then return end
  local last = S.last[id]
  local d = last and hdist(s.x, s.z, last.x, last.z) or -1
  local reason = not last and "new" or (d > MOVE_MM or abs(s.y - last.y) > MOVE_MM or yawDiff(s.yaw, last.yaw) > YAW_MRAD) and "moved" or
    s.life ~= last.life and "life" or s.sv ~= last.sv and "state" or (s.zero ~= last.zero or s.parts ~= last.parts) and "parts" or nil
  local sweep = S.sweepUntil ~= nil and t < S.sweepUntil
  local refresh = last ~= nil and t - last.at >= REFRESH_MS
  if not reason and not refresh then return end
  if last and t - last.at < MIN_GAP_MS then return end
  local flags = 1 + (sweep and 2 or 0) + (refresh and not reason and 4 or 0)
  S.seq = S.seq % 65535 + 1
  local sent = W.tx(OP, S.seq, id, S.hashOf[id] or 0, S.vtype(id), flags, s.x, s.y, s.z, s.yaw, s.life, s.sv, s.zero) == true
  if not sent then return end
  S.last[id] = s
  S.stats.tx = S.stats.tx + 1
  if sweep then S.stats.sweep, S.sweepSent = S.stats.sweep + 1, S.sweepSent + 1
  elseif not reason then S.stats.refresh = S.stats.refresh + 1
  else L("event=vehicle_state;step=tx;id=%d;why=%s;d_mm=%d;life=%d;sv=%d;zero=%d;parts=%d;f=%d;seq=%d", id, reason, d, s.life, s.sv, s.zero, s.parts, flags, S.seq) end
end
function S.hostTick(t)
  if not linked() then return end
  local pool = S.pool
  if S.cursor >= pool then S.cursor = 0 end
  local ids, seen = {}, {}
  for id in pairs(S.dirty) do if not seen[id] then ids[#ids + 1], seen[id] = id, true end end
  S.dirty = {}
  local first = 25600 + S.cursor
  local n = math.min(SLICE, pool - S.cursor)
  for i = 0, n - 1 do if not seen[first + i] then ids[#ids + 1], seen[first + i] = first + i, true end end
  S.cursor = (S.cursor + n) % pool
  local life = S.readLife(first, n) or {}
  for _, id in ipairs(ids) do
    if id < first or id >= first + n then
      local one = S.readLife(id, 1)
      if one then life[id] = one[id] end
    end
    S.hostCheck(id, t, life)
  end
  if S.sweepUntil and t >= S.sweepUntil then
    L("event=vehicle_state;step=sweep_done;sent=%d;pool=%d", S.sweepSent, S.pool)
    S.sweepUntil = nil
  end
end

-- Guest.
-- This copy's id for the host's q (nil and why when none may take it). A named one must lie within
-- 40 m of the host's pose too (a locator resolving elsewhere here is never teleported: far).
function S.match(q)
  if q.hash ~= 0 then
    local id = S.idOfHash[q.hash]
    if not id then return nil, "no_locator" end
    if not S.alive(id) then return nil, "absent" end
    local x, _, z = S.pose(id)
    if not x then return nil, "no_match" end
    if hdist(x, z, q.x, q.z) > FAR_MM then return nil, "far" end
    return id
  end
  if q.id % 512 == 0 then return nil, "index0" end
  local g = G()
  if not g or not S.alive(q.id) or S.nameOf[q.id] then return nil, "no_match" end
  local x, _, z = S.pose(q.id)
  if not x then return nil, "no_match" end
  local d = hdist(x, z, q.x, q.z)
  local t = S.vtype(q.id)
  if d > FAR_MM or t ~= 255 and q.vtype ~= 255 and t ~= q.vtype then return nil, "no_match" end
  for _, c in ipairs(g.near({x = q.x, z = q.z, vtype = q.vtype}, d)) do
    if c.id ~= q.id and c.d <= d then return nil, "not_nearest" end
  end
  return q.id
end
-- Deduplicated per id, except an apply and the answer to a host change (flags 1: neither sweep nor refresh).
local function outcome(id, step, line, change)
  if S.outcome[id] == step and step ~= "applied" and not change then return end
  S.outcome[id] = step
  L("%s", line)
end
-- One received state: "applied" | "same" | "hold" (kept for HOLD_MS) | "refused".
function S.consider(q, t)
  if not linked() then return "hold", "unpaired" end
  local id, why = S.match(q)
  -- An LRRP truck (each copy's own AI) or one this copy lost (VEHICLE_GONE) never takes a state.
  if id and (S.lrrp[id] or G() and G().gone[id]) then id, why = nil, S.lrrp[id] and "lrrp" or "gone_here" end
  if not id then
    S.stats.refused = S.stats.refused + 1
    outcome(q.id, "refused:" .. why, fmt("event=vehicle_state;step=refused;id=%d;reason=%s;f=%d", q.id, why, q.flags))
    return "refused", why
  end
  why = S.busy(id)
  local x, y, z, yaw = S.pose(id)
  if not why and not x then why = "pose_unreadable" end
  local d, dyaw = x and hdist(x, z, q.x, q.z) or 0, x and yawDiff(yaw, q.yaw) or 0
  local move = x ~= nil and (d > MOVE_MM or abs(y - q.y) > MOVE_MM or dyaw > YAW_MRAD)
  if not why and move then
    -- Never onto the local player, and never away from them (walking up to it, a GetOn before ON_VEHICLE).
    local px, pz
    pcall(function() px, pz = vars.playerPosX, vars.playerPosZ end)
    if type(px) == "number" and type(pz) == "number" then
      px, pz = floor(px * 1000), floor(pz * 1000)
      if hdist(px, pz, q.x, q.z) < NEAR_PLAYER_MM or hdist(px, pz, x, z) < NEAR_PLAYER_MM then why = "player_near" end
    end
  end
  if why then return "hold", why, id end
  local moved, err = false, -1
  if move then
    local rp = W.ridePose
    if type(rp) == "table" and type(rp.place) == "function" then moved = rp.place(id, q.x, q.y, q.z, q.yaw) == true end
    local x2, _, z2 = S.pose(id)
    if x2 then err = hdist(x2, z2, q.x, q.z) end
  end
  local here = row(S.readLife(id, 1), id)
  local was, lifeSet = here and here[1] or UNKNOWN, false
  local life = q.life ~= UNKNOWN and math.max(q.life, 1) or nil
  if here and life and life ~= here[1] then
    send(id, {id = "SetBodyLife", life = life})
    local after = row(S.readLife(id, 1), id)
    lifeSet = after ~= nil and after[1] == life
  end
  if lifeSet then S.stats.life = S.stats.life + 1 end
  if move and not moved then
    S.stats.refused = S.stats.refused + 1
    outcome(id, "refused:place_failed", fmt("event=vehicle_state;step=refused;id=%d;reason=place_failed;here=%d;d_mm=%d;err_mm=%d;dyaw=%d;life=%d;was=%d;f=%d",
      q.id, id, d, err, dyaw, q.life, was, q.flags))
    return "refused", "place_failed"
  end
  if not moved and not lifeSet then
    S.stats.same = S.stats.same + 1
    outcome(id, "same", fmt("event=vehicle_state;step=same;id=%d;from=%d;d_mm=%d;dyaw=%d;life=%d;here=%d;sv=%d;here_sv=%d;f=%d", id, q.id, d, dyaw,
      q.life, was, q.sv, here and here[4] or -1, q.flags), q.flags == 1)
    return "same"
  end
  S.stats.applied = S.stats.applied + 1
  if moved then S.stats.moved = S.stats.moved + 1 end
  outcome(id, "applied", fmt("event=vehicle_state;step=applied;id=%d;from=%d;moved=%d;d_mm=%d;err_mm=%d;dyaw=%d;life=%d;was=%d;sv=%d;here_sv=%d;zero=%d;here_zero=%d;f=%d",
    id, q.id, moved and 1 or 0, d, err, dyaw, q.life, was, q.sv, here and here[4] or -1, q.zero, here and here[2] or -1, q.flags))
  return "applied"
end
function S.guestConsider(q, t)
  local verdict, why, id = S.consider(q, t)
  if verdict ~= "hold" then S.pending[q.id] = nil; return end
  -- A hold changes the outcome: the line after it is logged again (a scenario waits for that one).
  if id then S.outcome[id] = nil end
  q.since = q.since or t
  if t - q.since >= HOLD_MS then
    S.pending[q.id] = nil
    L("event=vehicle_state;step=hold_timeout;id=%d;reason=%s", q.id, why)
    return
  end
  if q.why ~= why then
    q.why = why
    S.stats.held = S.stats.held + 1
    L("event=vehicle_state;step=hold;id=%d;here=%d;reason=%s", q.id, id or 0, why)
  end
  S.pending[q.id] = q
end
H[OP] = guarded(function(v)
  if W.host or type(v) ~= "table" or #v < 12 then return end
  if not S.on() then
    if not S.offLogged then S.offLogged = true; L("event=vehicle_state;step=ignored;reason=off;id=%d", v[2]) end
    return
  end
  S.stats.rx = S.stats.rx + 1
  if not S.named and (not S.namesAt or now() - S.namesAt >= 5000) then S.buildNames() end
  local old = S.pending[v[2]]
  S.guestConsider({seq = v[1], id = v[2], hash = v[3], vtype = v[4], flags = v[5], x = v[6], y = v[7], z = v[8], yaw = v[9],
    life = v[10], sv = v[11], zero = v[12], since = old and old.since, why = old and old.why}, now())
end)

-- Hooks.
do
  local g = G()
  if g and type(g.observe) == "function" then
    local base = g.observe
    g.observe = function(kind, id, att, a1)
      local ok, err = pcall(base, kind, id, att, a1)
      if kind == "Damage" and W.host and S.on() and integer(id, 25600, 26111) then S.dirty[id] = true end
      if not ok then error(err, 0) end
    end
  end
end
W.startHooks[#W.startHooks + 1] = guarded(function()
  S.reset()
  if W.vehicleStateOn == true and W.active == true and MISSIONS[W.mission] and not mission() then
    L("event=vehicle_state;step=off;mission=%d;reason=mb_lab_only", W.mission)
  end
  if not S.on() then return end
  S.buildNames()
  L("event=vehicle_state;step=on;mission=%d;role=%s;named=%d;lrrp=%d", W.mission, W.host and "host" or "guest", S.named or 0,
    (function() local n = 0; for _ in pairs(S.lrrp) do n = n + 1 end; return n end)())
end)
W.pairedHooks[#W.pairedHooks + 1] = guarded(function()
  if not S.on() or not W.host then return end
  -- The sweep: every eligible vehicle again, at the scan rate (SLICE per TICK_MS).
  S.last, S.skipWhy, S.cursor, S.sweepSent = {}, {}, 0, 0
  S.sweepUntil = now() + (floor(S.pool / SLICE) + 2) * TICK_MS
  L("event=vehicle_state;step=sweep;pool=%d", S.pool)
end)
W.stopHooks[#W.stopHooks + 1] = function(final)
  if final then S.pending, S.dirty = {}, {} end
end
local tick = W.tick
function W.tick(...)
  if tick then tick(...) end
  if not S.on() then return end
  local t = now()
  if t < S.nextTick then return end
  S.nextTick = t + TICK_MS
  local ok, err = pcall(function()
    if not S.named and (not S.namesAt or t - S.namesAt >= 5000) then S.buildNames() end
    if W.host then S.hostTick(t)
    else
      local list = {}
      for _, q in pairs(S.pending) do list[#list + 1] = q end
      for _, q in ipairs(list) do S.guestConsider(q, t) end
    end
  end)
  if not ok then fail(err) end
end

-- Lab levers (debug_vehicle.lua): this copy only.
local function target(arg)
  if arg == "nearest" then
    local px, pz
    pcall(function() px, pz = vars.playerPosX, vars.playerPosZ end)
    if type(px) ~= "number" or type(pz) ~= "number" then return nil, "vehicle_lever_position" end
    local g, best, bestD = G(), nil, nil
    local mine = g and g.localVehicle()
    for i = 0, S.pool - 1 do
      local id = 25600 + i
      if id ~= mine and S.alive(id) then
        local x, _, z = S.pose(id)
        local d = x and sqrt((x / 1000 - px) ^ 2 + (z / 1000 - pz) ^ 2)
        if d and d <= LEVER_M and (not bestD or d < bestD) then best, bestD = id, d end
      end
    end
    if not best then return nil, "vehicle_none" end
    return best
  end
  local id = tonumber(arg)
  if not integer(id, 25600, 26111) then return nil, "vehicle_lever_args" end
  if not S.alive(id) then return nil, "vehicle_not_alive" end
  return id
end
function S.lever(a)
  if a[1] == "vehicle_state_status" then
    if #a > 2 then return false, "vehicle_state_args" end
    return S.status(a[2])
  end
  if W.mode ~= 1 then return false, "vehicle_lever_lab_only" end
  if W.active ~= true or not MISSIONS[W.mission] then return false, "vehicle_lever_context" end
  if #a ~= (a[1] == "vehicle_push" and 4 or 3) then return false, "vehicle_lever_args" end
  local id, why = target(a[2])
  if not id then return false, why end
  local g = G()
  if g and g.localVehicle() == id then return false, "vehicle_lever_local_rider" end
  local x, y, z, yaw = S.pose(id)
  if not x then return false, "vehicle_lever_pose" end
  if a[1] == "vehicle_push" then
    local dx, dz = tonumber(a[3]), tonumber(a[4])
    if type(dx) ~= "number" or type(dz) ~= "number" or abs(dx) > 50 or abs(dz) > 50 then return false, "vehicle_lever_args" end
    local rp = W.ridePose
    local ok = type(rp) == "table" and type(rp.place) == "function" and rp.place(id, x + floor(dx * 1000), y + 300, z + floor(dz * 1000), yaw) == true
    local x2, _, z2 = S.pose(id)
    L("event=vehicle_state;step=lever;id=%d;push=%d,%d;ok=%d;moved_mm=%d", id, floor(dx * 1000), floor(dz * 1000), ok and 1 or 0, x2 and hdist(x2, z2, x, z) or -1)
    if W.host then S.dirty[id] = true end
    return ok, fmt("vehicle_push;id=%d;from=%d,%d;to=%d,%d;ok=%d", id, x, z, x2 or 0, z2 or 0, ok and 1 or 0)
  end
  local life = tonumber(a[3])
  if not integer(life, 0, 65534) then return false, "vehicle_lever_args" end
  local before = row(S.readLife(id, 1), id)
  send(id, {id = "SetBodyLife", life = life})
  local after = row(S.readLife(id, 1), id)
  local alive = S.alive(id)
  L("event=vehicle_state;step=lever;id=%d;life=%d;was=%d;now=%d;alive=%d;sv=%d", id, life, before and before[1] or -1, after and after[1] or -1,
    alive and 1 or 0, after and after[4] or -1)
  if W.host then S.dirty[id] = true end
  return after ~= nil and after[1] == life, fmt("vehicle_life;id=%d;life=%d;was=%d;now=%d;alive=%d;sv=%d", id, life, before and before[1] or -1,
    after and after[1] or -1, alive and 1 or 0, after and after[4] or -1)
end
-- Read-only (debug vehicle_state_status [id]).
function S.status(arg)
  if arg ~= nil then
    local id = tonumber(arg)
    if not integer(id, 25600, 26111) then return false, "vehicle_state_args" end
    local x, y, z, yaw = S.pose(id)
    local v = row(S.readLife(id, 1), id)
    return true, string.sub(fmt("vstate;id=%d;alive=%d;pos=%d,%d,%d;yaw=%d;life=%d;zero=%d;parts=%d;sv=%d;busy=%s;name=%s", id, S.alive(id) and 1 or 0,
      x or 0, y or 0, z or 0, yaw or 0, v and v[1] or -1, v and v[2] or -1, v and v[3] or -1, v and v[4] or -1, tostring((S.busy(id))),
      tostring(S.nameOf[id])), 1, 160)
  end
  local st, pend = S.stats, 0
  for _ in pairs(S.pending) do pend = pend + 1 end
  return true, string.sub(fmt("vstate;on=%d;role=%s;named=%d;pool=%d;tx=%d;sweep=%d;refresh=%d;skip=%d;rx=%d;applied=%d;moved=%d;life=%d;same=%d;held=%d;refused=%d;pending=%d",
    S.on() and 1 or 0, W.host and "host" or "guest", S.named or 0, S.pool, st.tx, st.sweep, st.refresh, st.skip, st.rx, st.applied, st.moved, st.life,
    st.same, st.held, st.refused, pend), 1, 160)
end
end
end
