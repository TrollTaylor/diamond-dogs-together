-- Enemy round 8 B / round 9 B (docs/world-sync-design.md 7.21, 7.25; protocol sections 38, 42; native lab key lrrp_follow,
-- off unless named; no CFG bit): the LRRP trucks (the patrol trucks of lrrpVehicle) follow the host's. Spliced after
-- world_vehicle_state.lua (W.vehicleState: S.lrrp, S.pose, S.alive, S.on, S.lever); moved out of it in round 9 B.
--   host:  streams each live LRRP truck no player rides (LRRP_FAST 93: 10 Hz moving, 1 Hz still, 50 ms apart).
--   guest: holds its copy (ForceStop) and puts it on the host's every frame while it moves or is manned there, pinned
--          at rest. Its crew follows the host's: out at the same spot (UnsetRelativeVehicle; never StartTravel "", which
--          ended the crew's own LRRP plan), in again when the host's boards (SetRelativeVehicle rideFromBeginning, as
--          TppEnemy.AssignSoldiersToCP seats an LRRP crew). Within 400 m of this player (either end) a move over 30 m waits
--          (the truck stays this copy's own, as 0.15.2) and one of 5..30 m slides 1 m a frame; farther out it jumps.
-- Lever (both roles, debug vehicle_state_status lrrp 0|1): pause / resume lrrp_follow on this copy (a key-off window).
do
local W = e.MgsCoopWorld
local S = type(W) == "table" and W.vehicleState
if type(S) == "table" and type(S.lrrp) == "table" then
local fmt, floor, sqrt, H = string.format, math.floor, math.sqrt, W.handlers
local NULL = 65535
local function integer(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
local function L(...)
  local ok, text = pcall(fmt, ...)
  if ok and type(W.log) == "function" then pcall(W.log, text) end
end
local function now() return type(W.now) == "function" and W.now() or 0 end
local function G() return type(W.vehicleGone) == "table" and W.vehicleGone or nil end
local function linked() return W.paired == true and type(W.eff) == "number" and W.eff >= 3 end
local function send(target, cmd)
  local a = W.api
  if type(a) ~= "table" or type(a.send) ~= "function" then return nil end
  local ok, r1, r2 = pcall(a.send, target, cmd)
  if ok then return r1, r2 end
end
local function hdist(ax, az, bx, bz) local dx, dz = ax - bx, az - bz; return floor(sqrt(dx * dx + dz * dz)) end
local function cpu() local ok, v = pcall(function() return os.clock() end); return ok and type(v) == "number" and v or 0 end
-- This player's x, z in mm, or nil.
local function me()
  local ok, x, z = pcall(function() return vars.playerPosX, vars.playerPosZ end)
  if ok and type(x) == "number" and type(z) == "number" then return x * 1000, z * 1000 end
end
local LF = {OP = 93, SEND_MS = 50, MOVE_MS = 100, REST_MS = 1000, STALE_MS = 3000, JUMP_MM = 5000, SETTLE_MM = 3000,
  MOVING = 500, AHEAD_MS = 100, MAX_AHEAD_MS = 400, UNSEAT_MS = 2000, BOARD_MS = 3000, CMD_MS = 5000, NEAR_MM = 400000,
  STEP_MM = 1000, CATCH_MM = 30000, COST_MS = 10000}
S.lf = LF
W.OP.LRRP_FAST = LF.OP
function LF.reset() LF.cfg, LF.askAt, LF.tr, LF.hs, LF.next, LF.refused, LF.cost = nil, nil, {}, {}, 0, {}, {n = 0, sum = 0, max = 0} end
LF.reset()
function LF.on()
  if not S.on() or LF.paused then return false end
  local t = now()
  if LF.cfg == nil and (not LF.askAt or t - LF.askAt >= 5000) then
    LF.askAt = t
    local ok, r = pcall(W.receipt, "return 'MGS_COOP_LRRP_CFG_V1'")
    if ok and (r == 0 or r == 1) then LF.cfg = r == 1 end
  end
  return LF.cfg == true
end
local function ent(v) return type(W.byId) == "table" and W.byId[v] and fmt("%.0f", W.byId[v]) or tostring(v) end
-- NPC riders of `id`: count, gids, and "r1=hash:x:z;..." (a line of its own: W.log keeps 160 chars).
local function riders(id)
  local r, n, gids, s = send(id, {id = "GetRiderId"}), 0, {}, ""
  if type(r) == "table" then
    for _, v in pairs(r) do
      if integer(v, 0, NULL - 1) then
        n = n + 1
        gids[n] = v
        local okP, p = pcall(W.api.send, v, {id = "GetPosition"})
        local okX, x, z = pcall(function() return p:GetX(), p:GetZ() end)
        s = s .. fmt(";r%d=%s:%s", n, ent(v), okP and okX and fmt("%.1f:%.1f", x, z) or "?")
      end
    end
  end
  return n, gids, s
end
-- Crew positions (both roles): every crew member this copy has seen seated, once a second for 40 s after its rider
-- count changed: event=lrrp;step=crew;id;ent;x;z;in (seated);rl (riders now).
local function crewLog(id, rec, t, n, gids)
  local seated = {}
  rec.crew = rec.crew or {}
  for _, v in ipairs(gids) do rec.crew[v], seated[v] = true, true end
  if rec.lastN ~= n then rec.lastN, rec.chgAt = n, t end
  if not rec.chgAt or t - rec.chgAt > 40000 or t - (rec.crewAt or -1e9) < 1000 then return end
  rec.crewAt = t
  for v in pairs(rec.crew) do
    local x, _, z = S.pose(v)
    L("event=lrrp;step=crew;id=%d;ent=%s;x=%s;z=%s;in=%d;rl=%d", id, ent(v), x and fmt("%.1f", x / 1000) or "?", x and fmt("%.1f", z / 1000) or "?",
      seated[v] and 1 or 0, n)
  end
end
local function ridden(id)
  local g, rp = G(), W.ridePose
  if g and type(g.localVehicle) == "function" and g.localVehicle() == id then return "local" end
  local tr = type(rp) == "table" and type(rp.fast) == "table" and rp.fast.tracks
  if type(tr) == "table" and tr[id] ~= nil then return "partner" end
end
-- The cost of this module per tick (os.clock), one line every 10 s: event=lrrp;step=cost;role;n;avg_ms;max_ms;tracks.
local function cost(t, c0)
  local c, ms = LF.cost, (cpu() - c0) * 1000
  c.n, c.sum, c.max, c.at = c.n + 1, c.sum + ms, math.max(c.max, ms), c.at or t
  if t - c.at >= LF.COST_MS then
    local k = 0
    for _ in pairs(LF.tr) do k = k + 1 end
    L("event=lrrp;step=cost;role=%s;n=%d;avg_ms=%.3f;max_ms=%.3f;tracks=%d", W.host and "host" or "guest", c.n, c.sum / c.n, c.max, k)
    LF.cost = {n = 0, sum = 0, max = 0, at = t}
  end
end
function LF.hostTick(t)
  if t < LF.next then return end
  LF.next = t + LF.SEND_MS
  local best, bh
  for id in pairs(S.lrrp) do
    local h = LF.hs[id] or {sent = -1e9, v = 0, logAt = -1e9}
    LF.hs[id] = h
    local x, y, z, yaw
    if S.alive(id) and not ridden(id) then x, y, z, yaw = S.pose(id) end
    h.x, h.y, h.z, h.yaw = x, y, z, yaw
    if x then
      if not h.pt or t - h.pt >= 250 then
        if h.pt and t > h.pt then h.v = math.min(65535, floor(sqrt((x - h.px) ^ 2 + (z - h.pz) ^ 2) * 1000 / (t - h.pt))) end
        h.px, h.pz, h.pt = x, z, t
      end
      if t - h.sent >= (h.v >= LF.MOVING and LF.MOVE_MS or LF.REST_MS) and (not bh or h.sent < bh.sent) then best, bh = id, h end
    end
  end
  if not best then return end
  local n, gids, list = riders(best)
  crewLog(best, bh, t, n, gids)
  local moving = bh.v >= LF.MOVING
  if W.tx(LF.OP, W.tick32(t), best, bh.x, bh.y, bh.z, bh.yaw, bh.v, math.min(n, 8), moving and 1 or 0) ~= true then return end
  bh.sent, bh.n = t, (bh.n or 0) + 1
  if t - bh.logAt >= ((moving or n > 0) and 250 or 5000) then
    bh.logAt = t
    L("event=lrrp;step=host;id=%d;x=%.2f;y=%.2f;z=%.2f;yaw=%d;v=%d;riders=%d;n=%d", best, bh.x / 1000, bh.y / 1000, bh.z / 1000, bh.yaw, bh.v, n, bh.n)
    if n > 0 then L("event=lrrp;step=riders;id=%d;n=%d%s", best, n, list) end
  end
end
local function force(id, tr, on, why)
  if (tr.stop or false) == on then return end
  tr.stop = on
  send(id, {id = "ForceStop", enabled = on})
  L("event=lrrp;step=%s;id=%d;why=%s", on and "stop" or "release", id, why)
end
-- An error turns lrrp_follow off, never VEHICLE_STATE.
local function lfail(err)
  LF.cfg = false
  pcall(function() for id, tr in pairs(LF.tr) do if tr.stop then send(id, {id = "ForceStop", enabled = false}) end end end)
  LF.tr = {}
  L("event=lrrp;step=error;msg=%s", string.sub(tostring(err), 1, 100))
end
H[LF.OP] = function(...)
  local ok, err = pcall(function(v)
  if W.host or type(v) ~= "table" or #v < 9 or not LF.on() or not linked() then return end
  local id, t = v[2], now()
  if not S.lrrp[id] then
    if not LF.refused[id] then LF.refused[id] = true; L("event=lrrp;step=refused;id=%d;reason=not_lrrp", id) end
    return
  end
  local tr = LF.tr[id]
  if not tr then tr = {jumps = 0, n = 0, logAt = -1e9, crew = {}, cmd = {}, outs = 0, ins = 0}; LF.tr[id] = tr end
  local b = tr.b
  if b and ((v[1] - b.tick) % 4294967296 >= 2147483648 or v[1] == b.tick) then return end
  local off = t - v[1]
  if not tr.off or off < tr.off or off - tr.off > 10000 then tr.off = off else tr.off = tr.off + (off - tr.off) * 0.02 end
  tr.a, tr.b, tr.at = b, {tick = v[1], x = v[3], y = v[4], z = v[5], yaw = v[6], v = v[7], n = v[8], f = v[9]}, t
  end, ...)
  if not ok then lfail(err) end
end
-- A guard this copy may seat: alive and standing here (W.place.state 0), or unknown to the enemy table.
local function fit(v)
  local P, h = W.place, type(W.byId) == "table" and W.byId[v]
  local en = h and type(W.ent) == "table" and W.ent[h]
  if type(P) == "table" and type(P.state) == "function" and en then return P.state(en) == 0 end
  return true
end
-- The crew follows the host's count (rh = b.n): this copy's extra riders get out 2 s after the host's did (at rest),
-- this copy's known crew boards 3 s after the host's did; one command per guard per 5 s.
local function crew(id, tr, t, b, moving, n, gids)
  local seated = {}
  for _, v in ipairs(gids) do tr.crew[v], seated[v] = true, true end
  tr.outAt = (not moving and b.n < n) and (tr.outAt or t) or nil
  tr.inAt = b.n > n and (tr.inAt or t) or nil
  if tr.outAt and t - tr.outAt >= LF.UNSEAT_MS then
    local extra = n - b.n
    for _, v in ipairs(gids) do
      if extra <= 0 then break end
      extra = extra - 1
      if t - (tr.cmd[v] or -1e9) >= LF.CMD_MS then
        tr.cmd[v], tr.outs = t, tr.outs + 1
        send(v, {id = "UnsetRelativeVehicle"})
        L("event=lrrp;step=unseat;id=%d;ent=%s;k=%d;rh=%d;rl=%d", id, ent(v), tr.outs, b.n, n)
      end
    end
  end
  if tr.inAt and t - tr.inAt >= LF.BOARD_MS then
    local want, any = b.n - n, false
    for v in pairs(tr.crew) do
      if want <= 0 then break end
      if not seated[v] and fit(v) then
        any, want = true, want - 1
        if t - (tr.cmd[v] or -1e9) >= LF.CMD_MS then
          tr.cmd[v], tr.ins = t, tr.ins + 1
          send(v, {id = "SetRelativeVehicle", targetId = id, rideFromBeginning = true})
          L("event=lrrp;step=board;id=%d;ent=%s;k=%d;rh=%d;rl=%d", id, ent(v), tr.ins, b.n, n)
        end
      end
    end
    if not any and not tr.noCrew then tr.noCrew = true; L("event=lrrp;step=board_none;id=%d;rh=%d;rl=%d", id, b.n, n) end
  end
end
function LF.frame(t)
  local mx, mz = me()
  for id, tr in pairs(LF.tr) do
    local b, a = tr.b, tr.a
    local busy = ridden(id)
    if t - tr.at > LF.STALE_MS or busy then
      force(id, tr, false, busy and "ride_" .. busy or "stale")
      if not busy then LF.tr[id] = nil; L("event=lrrp;step=stale;id=%d;jumps=%d;n=%d", id, tr.jumps, tr.n) end
    else
      local ahead = math.min(t - (b.tick + tr.off) + LF.AHEAD_MS, LF.MAX_AHEAD_MS)
      local tx, ty, tz = b.x, b.y, b.z
      local dt = a and (b.tick - a.tick) % 4294967296
      local moving = b.f % 2 == 1
      if dt and dt >= 20 and dt <= 1500 and moving then
        tx, ty, tz = tx + (b.x - a.x) * ahead / dt, ty + (b.y - a.y) * ahead / dt, tz + (b.z - a.z) * ahead / dt
      end
      local follow = moving or b.n > 0
      tr.restAt = not follow and (tr.restAt or t) or nil
      local x, _, z = S.pose(id)
      local d = x and hdist(x, z, tx, tz) or -1
      -- How near this player is to either end (mm); a jump there would be seen.
      local near = x and mx and math.min(hdist(x, z, mx, mz), hdist(tx, tz, mx, mz)) or 1e12
      local wait = d > LF.CATCH_MM and near < LF.NEAR_MM
      if wait then
        force(id, tr, false, "near")
        if t - (tr.deferAt or -1e9) >= 2000 then
          tr.deferAt = t
          L("event=lrrp;step=defer;id=%d;d_m=%.1f;near_m=%.1f;rh=%d", id, d / 1000, near / 1000, b.n)
        end
      else
        force(id, tr, true, follow and (b.n > 0 and "riders" or "moving") or "rest")
        if not follow and d > LF.SETTLE_MM then L("event=lrrp;step=settle;id=%d;d_m=%.1f", id, d / 1000) end
        local rp = W.ridePose
        local catch = d > LF.JUMP_MM and near < LF.NEAR_MM
        local px, pz = tx, tz
        if catch then
          px, pz = x + (tx - x) * LF.STEP_MM / d, z + (tz - z) * LF.STEP_MM / d
          if not tr.catching then tr.catching = true; L("event=lrrp;step=catch;id=%d;d_m=%.1f;near_m=%.1f", id, d / 1000, near / 1000) end
        else
          tr.catching = nil
        end
        if x and (follow and d >= 1 or d >= 1000) and type(rp) == "table" and rp.place and rp.place(id, floor(px), floor(ty), floor(pz), b.yaw) then
          tr.n = tr.n + 1
          if d > LF.JUMP_MM and not catch then
            tr.jumps = tr.jumps + 1
            L("event=lrrp;step=jump;id=%d;d_m=%.1f;mode=%s;jumps=%d;near_m=%.1f", id, d / 1000, follow and "follow" or "rest", tr.jumps, near / 1000)
          end
        end
      end
      local logs = t - tr.logAt >= (follow and 250 or 2000)
      if logs or t - (tr.seatAt or 0) >= 500 then
        tr.seatAt = t
        local n, gids, list = riders(id)
        if not wait then crew(id, tr, t, b, moving, n, gids) end
        crewLog(id, tr, t, n, gids)
        if logs then
          tr.logAt = t
          local x2, y2, z2 = S.pose(id)
          L("event=lrrp;step=follow;id=%d;mode=%s;x=%.2f;y=%.2f;z=%.2f;d_m=%.2f;v=%d;rh=%d;rl=%d;jumps=%d;n=%d", id,
            wait and "wait" or follow and "follow" or "rest", (x2 or 0) / 1000, (y2 or 0) / 1000, (z2 or 0) / 1000, d / 1000, b.v, b.n, n, tr.jumps, tr.n)
          if n > 0 then L("event=lrrp;step=riders;id=%d;n=%d%s", id, n, list) end
        end
      end
    end
  end
end
local function releaseAll(why)
  for id, tr in pairs(LF.tr) do force(id, tr, false, why) end
  LF.tr = {}
end
W.startHooks[#W.startHooks + 1] = function()
  pcall(releaseAll, "start")
  LF.reset()
  if LF.on() then L("event=lrrp;step=on;role=%s", W.host and "host" or "guest") end
end
if type(W.unpairedHooks) == "table" then W.unpairedHooks[#W.unpairedHooks + 1] = function() pcall(releaseAll, "unpaired") end end
W.stopHooks[#W.stopHooks + 1] = function() pcall(releaseAll, "stop") end
local tickLF = W.tick
function W.tick(...)
  if tickLF then tickLF(...) end
  if not LF.on() or not linked() then return end
  local t, c0 = now(), cpu()
  local ok, err = pcall(function() if W.host then LF.hostTick(t) else LF.frame(t) end end)
  if not ok then lfail(err) end
  cost(t, c0)
end
-- The lever: debug vehicle_state_status lrrp 0|1 (this copy; any mode: the frame-time window of a hidden run).
local lever = S.lever
function S.lever(a)
  if type(a) == "table" and a[1] == "vehicle_state_status" and a[2] == "lrrp" then
    if #a ~= 3 or (a[3] ~= "0" and a[3] ~= "1") then return false, "lrrp_lever_args" end
    LF.paused = a[3] == "0" or nil
    if LF.paused then pcall(releaseAll, "lever") end
    L("event=lrrp;step=lever;on=%s;role=%s", a[3], W.host and "host" or "guest")
    return true, "lrrp_follow;on=" .. a[3]
  end
  if type(lever) == "function" then return lever(a) end
  return false, "vehicle_state_unavailable"
end
end
end
