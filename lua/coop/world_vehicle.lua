-- Host-authoritative vehicle removal (0.13.6 online-vehicle-gone; VEHICLE_GONE op 46,
-- REQ_VEHICLE_GONE op 47, docs/world-sync-protocol.md section 16). Vehicles are not
-- world-synced: in the 2026-10-22 two-PC session the guest Fultoned jeep 25603, the
-- host drove his copy of it, the guest's seat-by-id read failed (local_read) and hid
-- him. With lab.ini world_vehicle_gone=1 (CFG r[15]; default off) on both copies:
--   host:     a vehicle its copy loses (Fulton/FultonInfo, Dead, VehicleBroken, then
--             IsAlive read back false within 30 s) is announced as VEHICLE_GONE.
--   guest:    one it loses by its own hand (FultonInfo or Dead by the local player,
--             VehicleBroken within 10 s of a local Damage) is requested with
--             REQ_VEHICLE_GONE; the host removes its copy and announces it (actor 1,
--             the rid), or answers REJECT (1 not a vehicle, 8 far/type/index 0,
--             9 still held after 120 s, 11 off).
--   receiver: removes its copy (TppVehicle2 Despawn by the authored locator, else by
--             the pool index) only for a live vehicle within 40 m of the reported
--             position (or the one vehicle of that type within 5 m of it) and only
--             while nobody rides it: never the local player's vehicle, an occupied
--             rider[] seat or a vehicle the partner drives (a live RIDE_FAST track).
--             Otherwise held and re-checked every 250 ms for 120 s.
-- Lab lever (debug vehicle_remove nearest [sync], lab mode only): removes the nearest
-- free vehicle on this copy only (the divergence), or with sync through the path above.
do
local W = e.MgsCoopWorld
if type(W) == "table" then
local V = {}
W.vehicleGone = V
local GONE, REQ = 46, 47
W.OP.VEHICLE_GONE, W.OP.REQ_VEHICLE_GONE = GONE, REQ
local fmt, floor, sqrt, H = string.format, math.floor, math.sqrt, W.handlers
local NULL, FAR_MM, MATCH_MM, NEAR_M, SCAN = 65535, 40000, 5000, 60, 127
local WATCH_MS, POLL_MS, HOLD_MS, HIT_MS, ACK_MS, REQ_MS, READ_MS = 30000, 250, 120000, 10000, 5000, 150000, 2000
V.FAR_MM, V.MATCH_MM, V.HOLD_MS, V.WATCH_MS, V.ACK_MS = FAR_MM, MATCH_MM, HOLD_MS, WATCH_MS, ACK_MS
local FULTON, DESTROYED, LAB = 1, 2, 3
local MISSIONS = {[30010] = true, [30020] = true, [30050] = true}
local function integer(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
local function L(...)
  local ok, text = pcall(fmt, ...)
  if ok and type(W.log) == "function" then pcall(W.log, text) end
end
local function now() return type(W.now) == "function" and W.now() or 0 end
local function count(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end
function V.reset()
  V.watch, V.gone, V.sent, V.held, V.req, V.unsent, V.hit, V.reads = {}, {}, {}, {}, {}, {}, {}, {}
  V.seq, V.nextPoll, V.failed, V.offLogged = 0, 0, false, false
end
V.reset()
function V.on() return W.vehicleGoneOn == true and W.active == true and MISSIONS[W.mission] == true and not V.failed end
-- A TppVehicle2 id: class 50, 25600 + pool index 0..511 (the wire range).
function V.typed(id) return integer(id, 25600, 26111) end
local function send(target, cmd)
  local a = W.api
  if type(a) ~= "table" or type(a.send) ~= "function" then return nil end
  local ok, r1, r2 = pcall(a.send, target, cmd)
  if ok then return r1, r2 end
end
local function isVehicle(id)
  if not V.typed(id) or type(Tpp) ~= "table" or type(Tpp.IsVehicle) ~= "function" then return false end
  local ok, yes = pcall(Tpp.IsVehicle, id)
  return ok and yes == true
end
V.isVehicle = isVehicle
-- IsAlive (instance dispatcher 0x140B2B710, StrCode32 0xED0E1BD6): record+0x70 & 0x800004
-- clear; an index no longer in the pool answers nothing, which reads as not alive.
function V.alive(id) return isVehicle(id) and send(id, {id = "IsAlive"}) == true end
local function milli(v)
  if type(v) ~= "number" or v ~= v then return nil end
  local n = floor(v * 1000 + 0.5)
  if n < -1000000000 or n > 1000000000 then return nil end
  return n
end
function V.pose(id)
  local p = send(id, {id = "GetPosition"})
  if p == nil then return nil end
  local ok, x, y, z = pcall(function() return p:GetX(), p:GetY(), p:GetZ() end)
  if not ok then return nil end
  x, y, z = milli(x), milli(y), milli(z)
  if x and y and z then return x, y, z end
end
-- GetVehicleType (0xC04CB63A): the type nibble; 255 unknown.
function V.vtype(id)
  local t = send(id, {id = "GetVehicleType"})
  return integer(t, 0, 254) and t or 255
end
local function dist(x, z, q) local dx, dz = x - q.x, z - q.z; return floor(sqrt(dx * dx + dz * dz)) end
-- The vehicle the local player drives or rides with ON_VEHICLE, or nil.
function V.localVehicle()
  local riding, id = false, nil
  pcall(function()
    riding = PlayerInfo.OrCheckStatus({PlayerStatus.ON_VEHICLE}) == true
    id = vars.playerVehicleGameObjectId
  end)
  if riding and V.typed(id) then return id end
end
-- Why this copy must not remove `id` now (nil: free). The local player's vehicle, any
-- occupied rider[] seat (a passenger, the local back-seat rider, a soldier; body 1 is
-- never in rider[]) or a live RIDE_FAST track (the partner drives it on this copy).
function V.busy(id)
  if V.localVehicle() == id then return "local_rider" end
  local r = send(id, {id = "GetRiderId"})
  if type(r) ~= "table" then return "riders_unreadable" end
  for _, v in pairs(r) do
    if not integer(v, 0, NULL) then return "riders_unreadable" end
    if v ~= NULL then return "occupied" end
  end
  local rp = W.ridePose
  local tracks = type(rp) == "table" and type(rp.fast) == "table" and rp.fast.tracks
  if type(tracks) == "table" and tracks[id] ~= nil then return "partner_ride" end
end
-- The authored locator of `id` (veh_lv_0000..0031, veh_trc_0000..0015), or nil.
function V.locator(id)
  local a = W.api
  if type(a) ~= "table" or type(a.id) ~= "function" then return nil end
  for _, c in ipairs({{"lv", 32}, {"trc", 16}}) do
    for k = 0, c[2] - 1 do
      local name = fmt("veh_%s_%04d", c[1], k)
      local ok, got = pcall(a.id, "TppVehicle2", name)
      if ok and got == id then return name end
    end
  end
end
-- Removes this copy's vehicle with the class command Despawn (0x140B30AF0, StrCode32
-- 0xE22EC430): by its authored locator (the retail TppEnemy.DespawnVehicle form), else
-- by its pool index (ParseLuaSpawnIndex 0x140B304E0, which refuses index 0, the relief
-- vehicle: refused here as index0).
function V.remove(id)
  local a = W.api
  if type(a) ~= "table" or type(a.send) ~= "function" then return false, "binding" end
  local name, cmd, via = V.locator(id), nil, nil
  if name then cmd, via = {id = "Despawn", locator = name}, "locator"
  elseif id % 512 == 0 then return false, "index0"
  else cmd, via = {id = "Despawn", index = id % 512}, "index" end
  if not pcall(a.send, {type = "TppVehicle2"}, cmd) then return false, "send_error" end
  return true, via
end
-- Live vehicles within r_mm of q's position (the same type when both are known), nearest first.
function V.near(q, r_mm)
  local out = {}
  for i = 0, SCAN do
    local id = 25600 + i
    if V.alive(id) then
      local x, _, z = V.pose(id)
      if x then
        local d, t = dist(x, z, q), V.vtype(id)
        if d <= r_mm and (t == 255 or q.vtype == 255 or t == q.vtype) then out[#out + 1] = {id = id, d = d} end
      end
    end
  end
  table.sort(out, function(a, b) return a.d < b.d end)
  return out
end
local function decideId(q)
  if not isVehicle(q.id) then return "refuse", "not_vehicle" end
  if not V.alive(q.id) then return "absent" end
  local x, _, z = V.pose(q.id)
  if not x then return "hold", "pose_unreadable" end
  q.d = dist(x, z, q)
  if q.d > FAR_MM then return "refuse", "far" end
  local t = V.vtype(q.id)
  if t ~= 255 and q.vtype ~= 255 and t ~= q.vtype then return "refuse", "type" end
  local why = V.busy(q.id)
  if why then return "hold", why end
  return "apply"
end
-- q = {id, x, y, z, vtype}: "apply" | "hold", why | "refuse", why | "absent". Pool indices
-- can differ between copies: an absent, far or other-type id is matched to the one live
-- vehicle of that type within 5 m of the reported position (two: ambiguous). An id this
-- copy lost itself never gets here (VEHICLE_GONE returns early for V.gone).
function V.decide(q)
  if not V.typed(q.id) then return "refuse", "not_vehicle" end
  local verdict, why = decideId(q)
  if verdict == "absent" or verdict == "refuse" and (why == "far" or why == "type") then
    local c = V.near(q, MATCH_MM)
    if #c > 1 then return "refuse", "ambiguous" end
    if #c == 1 and c[1].id ~= q.id then
      q.from, q.id = q.from or q.id, c[1].id
      L("event=vehicle_gone;step=matched;id=%d;from=%d;d_mm=%d", q.id, q.from, c[1].d)
      verdict, why = decideId(q)
    end
  end
  return verdict, why
end
local function reject(q, reason)
  if q.src == "req" and W.paired then W.tx(W.OP.REJECT, q.rid, REQ, reason, q.id, 0, 0) end
end
-- Host: announces a vehicle its copy lost (kept for a re-send at the next PAIRED).
function V.broadcast(id, w, actor, rid)
  V.seq = V.seq % 65535 + 1
  local a = {seq = V.seq, id = id, cause = w.cause or LAB, actor = actor or 0, rid = rid or 0, vtype = w.vtype or 255,
    x = w.x or 0, y = w.y or 0, z = w.z or 0}
  local sent = W.paired == true and W.tx(GONE, a.seq, id, a.cause, a.actor, a.rid, a.vtype, a.x, a.y, a.z) == true
  if #V.sent < 64 then V.sent[#V.sent + 1] = a end
  L("event=vehicle_gone;step=tx;id=%d;cause=%d;actor=%d;rid=%d;seq=%d;sent=%d", id, a.cause, a.actor, a.rid, a.seq, sent and 1 or 0)
end
-- Guest: asks the host to remove its copy of a vehicle this copy lost.
function V.request(id, w, t)
  if not W.paired then
    V.unsent[#V.unsent + 1] = {id = id, w = w}
    L("event=vehicle_gone;step=req;rid=0;id=%d;cause=%d;sent=0;reason=unpaired", id, w.cause)
    return
  end
  local rid = W.nextRid()
  local ok = W.tx(REQ, rid, id, w.cause, w.vtype or 255, w.x, w.y, w.z) == true
  w.tries = (w.tries or 0) + 1
  if ok then V.req[rid] = {id = id, w = w, at = t} else V.unsent[#V.unsent + 1] = {id = id, w = w} end
  L("event=vehicle_gone;step=req;rid=%d;id=%d;cause=%d;sent=%d;try=%d", rid, id, w.cause, ok and 1 or 0, w.tries)
end
-- This copy lost `id` (read back): the host announces it, the guest requests it.
function V.confirmed(id, w, t)
  V.gone[id] = true
  L("event=vehicle_gone;step=lost;id=%d;cause=%d;role=%s", id, w.cause, W.host and "host" or "guest")
  if W.host then V.broadcast(id, w, w.actor or 0, 0) else V.request(id, w, t) end
end
local function readback(id, t)
  local q = V.reads[id]
  if not q then return end
  local alive = V.alive(id)
  if alive and t - q.at < READ_MS then return end
  V.reads[id] = nil
  L("event=vehicle_gone;step=applied;id=%d;via=%s;src=%s;ok=%d;ms=%d", id, q.via, q.src, alive and 0 or 1, floor(t - q.at))
end
-- One received item (src rx: the host's VEHICLE_GONE on the guest; req: the guest's
-- request on the host). Held items come back here every poll until done.
function V.consider(q, t)
  local verdict, why = V.decide(q)
  if q.from then V.held[q.from] = nil end
  if verdict == "hold" then
    q.since = q.since or t
    if t - q.since >= HOLD_MS then
      V.held[q.id] = nil
      L("event=vehicle_gone;step=hold_timeout;id=%d;src=%s;reason=%s", q.id, q.src, why)
      reject(q, 9)
      return
    end
    if q.why ~= why then q.why = why; L("event=vehicle_gone;step=hold;id=%d;src=%s;reason=%s", q.id, q.src, why) end
    V.held[q.id] = q
    return
  end
  V.held[q.id] = nil
  if verdict == "refuse" then
    L("event=vehicle_gone;step=refused;id=%d;src=%s;reason=%s;d_mm=%d", q.id, q.src, why, q.d or -1)
    reject(q, why == "not_vehicle" and 1 or 8)
    return
  end
  if verdict == "absent" then
    V.gone[q.id] = true
    L("event=vehicle_gone;step=absent;id=%d;src=%s", q.id, q.src)
    if q.src == "req" then V.broadcast(q.id, q, 1, q.rid) end
    return
  end
  local ok, via = V.remove(q.id)
  if not ok then
    L("event=vehicle_gone;step=refused;id=%d;src=%s;reason=%s;d_mm=%d", q.id, q.src, via, q.d or -1)
    reject(q, 8)
    return
  end
  V.gone[q.id] = true
  q.via, q.at = via, t
  V.reads[q.id] = q
  readback(q.id, t)
  if q.src == "req" then V.broadcast(q.id, q, 1, q.rid) end
end
-- Observer hook (world_sync.lua W.observe, ids no soldier map names).
function V.observe(kind, id, att)
  if not V.on() or not V.typed(id) or V.gone[id] then return end
  local t = now()
  if kind == "Damage" then
    if att == "local" then V.hit[id] = t end
    return
  end
  if kind == "FultonFailed" then
    if V.watch[id] then V.watch[id] = nil; L("event=vehicle_gone;step=watch_drop;id=%d;reason=fulton_failed", id) end
    return
  end
  local cause = (kind == "Fulton" or kind == "FultonInfo") and FULTON or (kind == "Dead" or kind == "VehicleBroken") and DESTROYED or nil
  if not cause or V.watch[id] then return end
  if not W.host then
    local hit = V.hit[id]
    local mine = (kind == "FultonInfo" or kind == "Dead") and att == "local" or kind == "VehicleBroken" and hit ~= nil and t - hit <= HIT_MS
    if not mine then return end
  end
  if not isVehicle(id) then return end
  local x, y, z = V.pose(id)
  if not x then L("event=vehicle_gone;step=watch_drop;id=%d;reason=no_pose", id); return end
  V.watch[id] = {cause = cause, at = t, x = x, y = y, z = z, vtype = V.vtype(id)}
  L("event=vehicle_gone;step=watch;id=%d;m=%s;cause=%d;att=%s", id, kind, cause, tostring(att))
end
function V.poll(t)
  for id, w in pairs(V.watch) do
    -- Gone: IsAlive false, or the instance no longer answers GetPosition (its pool slot released).
    local _, y = nil, nil
    if V.alive(id) then _, y = V.pose(id) end
    if not y then
      V.watch[id] = nil
      V.confirmed(id, w, t)
    elseif t - w.at >= WATCH_MS then
      V.watch[id] = nil
      L("event=vehicle_gone;step=watch_drop;id=%d;reason=still_alive;dy_mm=%d", id, y - w.y)
    end
  end
  local held = {}
  for _, q in pairs(V.held) do held[#held + 1] = q end
  for _, q in ipairs(held) do V.consider(q, t) end
  for id in pairs(V.reads) do readback(id, t) end
  local retry = {}
  for rid, q in pairs(V.req) do
    if (not q.received and t - q.at >= ACK_MS) or t - q.at >= REQ_MS then
      V.req[rid] = nil
      if not q.received and q.w.tries < 2 then retry[#retry + 1] = q
      else L("event=vehicle_gone;step=req_timeout;rid=%d;id=%d;received=%d", rid, q.id, q.received and 1 or 0) end
    end
  end
  for _, q in ipairs(retry) do V.request(q.id, q.w, t) end
end
H[GONE] = function(v)
  if W.host or type(v) ~= "table" or #v < 9 then return end
  local t, id, rid = now(), v[2], v[5]
  local q = rid ~= 0 and V.req[rid]
  if q then
    V.req[rid] = nil
    L("event=vehicle_gone;step=req_done;rid=%d;id=%d;result=gone;ms=%d", rid, q.id, floor(t - q.at))
  end
  if not V.on() then
    if not V.offLogged then V.offLogged = true; L("event=vehicle_gone;step=ignored;reason=off;id=%d", id) end
    return
  end
  L("event=vehicle_gone;step=rx;id=%d;seq=%d;cause=%d;actor=%d;rid=%d", id, v[1], v[3], v[4], rid)
  if V.gone[id] or V.held[id] then return end
  V.consider({id = id, cause = v[3], actor = v[4], rid = rid, vtype = v[6], x = v[7], y = v[8], z = v[9], src = "rx"}, t)
end
H[REQ] = function(v)
  if not W.host or type(v) ~= "table" or #v < 7 then return end
  local t, rid, id = now(), v[1], v[2]
  local q = {id = id, cause = v[3], actor = 1, rid = rid, vtype = v[4], x = v[5], y = v[6], z = v[7], src = "req"}
  L("event=vehicle_gone;step=rx_req;rid=%d;id=%d;cause=%d", rid, id, v[3])
  if not V.on() then
    L("event=vehicle_gone;step=refused;id=%d;src=req;reason=off;d_mm=-1", id)
    return reject(q, 11)
  end
  if V.gone[id] then return V.broadcast(id, q, 1, rid) end
  if V.held[id] then V.held[id].rid = rid; return end
  V.consider(q, t)
end
local received, rejected = H[W.OP.RECEIVED], H[W.OP.REJECT]
H[W.OP.RECEIVED] = function(v, t)
  if received then received(v, t) end
  local q = type(v) == "table" and v[2] == REQ and V.req[v[1]]
  if q then q.received = true end
end
H[W.OP.REJECT] = function(v, t)
  if rejected then rejected(v, t) end
  local q = type(v) == "table" and v[2] == REQ and V.req[v[1]]
  if q then
    V.req[v[1]] = nil
    L("event=vehicle_gone;step=req_done;rid=%d;id=%d;result=reject;reason=%d", v[1], q.id, v[3] or 0)
  end
end
W.startHooks[#W.startHooks + 1] = function()
  V.reset()
  if not V.on() then return end
  local M, str32 = W.M, type(W.api) == "table" and W.api.str32
  if type(M) == "table" and type(W.kind) == "table" and type(str32) == "function" then
    local ok, h = pcall(str32, "VehicleBroken")
    if ok and type(h) == "number" then M.VehicleBroken = h; W.kind[h] = "VehicleBroken" end
  end
  L("event=vehicle_gone;step=on;mission=%d;role=%s", W.mission, W.host and "host" or "guest")
end
W.pairedHooks[#W.pairedHooks + 1] = function(epochChanged)
  if not V.on() then return end
  if W.host then
    for _, a in ipairs(V.sent) do W.tx(GONE, a.seq, a.id, a.cause, a.actor, a.rid, a.vtype, a.x, a.y, a.z) end
    if #V.sent > 0 then L("event=vehicle_gone;step=resend;n=%d", #V.sent) end
    return
  end
  local list = V.unsent
  V.unsent = {}
  if epochChanged then return end
  for _, u in ipairs(list) do V.request(u.id, u.w, now()) end
end
W.unpairedHooks[#W.unpairedHooks + 1] = function()
  if W.host then return end
  for _, q in pairs(V.req) do V.unsent[#V.unsent + 1] = {id = q.id, w = q.w} end
  V.req = {}
end
W.stopHooks[#W.stopHooks + 1] = function(final)
  if final then V.watch, V.held, V.reads = {}, {}, {} end
end
local tick = W.tick
function W.tick(...)
  if tick then tick(...) end
  if not V.on() then return end
  local t = now()
  if t < V.nextPoll then return end
  V.nextPoll = t + POLL_MS
  local ok, err = pcall(V.poll, t)
  if not ok then V.failed = true; L("event=vehicle_gone;step=error;msg=%s", string.sub(tostring(err), 1, 100)) end
end
-- Lab lever: the nearest free vehicle (within 60 m, never the local player's or one
-- with an occupied rider[] seat). Without sync only this copy loses it; with sync it
-- is lost as cause 3 (lab) through the announcement / request above.
function V.lever(sync)
  if W.mode ~= 1 then return false, "vehicle_remove_lab_only" end
  if W.active ~= true or not MISSIONS[W.mission] then return false, "vehicle_remove_context" end
  if sync and not V.on() then return false, "vehicle_remove_key_off" end
  local px, pz
  pcall(function() px, pz = vars.playerPosX, vars.playerPosZ end)
  if type(px) ~= "number" or type(pz) ~= "number" then return false, "vehicle_remove_position" end
  local mine, best, bestD, busy = V.localVehicle(), nil, nil, 0
  for i = 0, SCAN do
    local id = 25600 + i
    if id ~= mine and V.alive(id) then
      local x, y, z = V.pose(id)
      local d = x and sqrt((x / 1000 - px) ^ 2 + (z / 1000 - pz) ^ 2)
      if d and d <= NEAR_M and (not bestD or d < bestD) then
        local r, free = send(id, {id = "GetRiderId"}), true
        if type(r) ~= "table" then free = false else for _, s in pairs(r) do if s ~= NULL then free = false end end end
        if free then best, bestD = {id = id, x = x, y = y, z = z}, d else busy = busy + 1 end
      end
    end
  end
  if not best then return false, fmt("vehicle_none;occupied=%d", busy) end
  best.vtype, best.cause, best.actor = V.vtype(best.id), LAB, 2
  local ok, via = V.remove(best.id)
  if not ok then return false, "vehicle_remove_" .. tostring(via) end
  local alive = V.alive(best.id)
  V.gone[best.id] = sync and true or "lab"
  L("event=vehicle_gone;step=lever;id=%d;via=%s;sync=%d;alive=%d;dist_m=%.1f", best.id, via, sync and 1 or 0, alive and 1 or 0, bestD)
  if sync then V.confirmed(best.id, best, now()) end
  return not alive, fmt("vehicle_remove;id=%d;via=%s;sync=%d;alive=%d;dist=%.1f", best.id, via, sync and 1 or 0, alive and 1 or 0, bestD)
end
-- Read-only report (debug vehicle_gone_status): ids lost here (L: lab lever, this copy only).
function V.status()
  local ids = {}
  for id, how in pairs(V.gone) do ids[#ids + 1] = how == "lab" and id .. "L" or tostring(id) end
  table.sort(ids)
  local function keys(t) local o = {}; for k in pairs(t) do o[#o + 1] = tostring(k) end; table.sort(o); return #o > 0 and table.concat(o, ",") or "none" end
  return string.sub(fmt("vgone;on=%d;role=%s;gone=%s;held=%s;watch=%s;req=%d", V.on() and 1 or 0, W.host and "host" or "guest",
    #ids > 0 and table.concat(ids, ",") or "none", keys(V.held), keys(V.watch), count(V.req)), 1, 160)
end
end
end
