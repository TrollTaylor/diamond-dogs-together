-- Horse probes (lab only, CFG mode 1; horse plan section 4), F9 commands of the player module; one probe at a time.
-- rec <ms> / play <mode> <ms> (P1: census pose every frame, replayed by SetCallHorse sg4 sg10 sg20 yaw20 chase1 chase2;
-- play_stats err_cm, leap). hold 0|1 (P5: ACTION mask, raw E). hostage, watch <ms> (P2). pin <dy_cm> <ms> (P4: body 1
-- warped every frame). cargo_pose p c b d r (M2). stop, status.
do
 local D, W = e.MgsCoopDebug, e.MgsCoopWorld
 local P = type(D) == "table" and D.player
 if type(P) == "table" and type(P.handles) == "table" and type(P.run) == "function" and type(D.api) == "table" then
 local A = D.api
 local get, fmt, floor, sqrt, sin, cos, pi = A.get, string.format, math.floor, math.sqrt, math.sin, math.cos, math.pi
 local HORSE, MASK, MAX_SAMPLES, MAX_MS = 6656, "MgsCoopHorseHold", 1800, 60000
 local PERIOD = {sg4 = 250, sg10 = 100, sg20 = 50, yaw20 = 50, chase1 = 1000, chase2 = 500}
 local H = {track = {}, hold = false}
 D.horse = H
 local function finite(n) return type(n) == "number" and n == n and n > -1e9 and n < 1e9 end
 local function round(n) return floor(n + 0.5) end
 local function now()
  if type(W) ~= "table" or type(W.now) ~= "function" then return 0 end
  local ok, v = pcall(W.now); return ok and finite(v) and v or 0
 end
 local function log(s) if type(W) == "table" and type(W.log) == "function" then pcall(W.log, s) end end
 local function lab() return type(W) == "table" and W.mode == 1 end
 local function wrap(a) while a > pi do a = a - 2 * pi end; while a < -pi do a = a + 2 * pi end; return a end
 function H.period(mode) return PERIOD[mode] end
 function H.interp(track, t)
  local n = #track
  if n == 0 then return nil end
  local first, last = track[1], track[n]
  if t <= first.t then return first.x, first.y, first.z, first.yaw end
  if t >= last.t then return last.x, last.y, last.z, last.yaw end
  local lo, hi = 1, n
  while hi - lo > 1 do
   local mid = floor((lo + hi) / 2)
   if track[mid].t <= t then lo = mid else hi = mid end
  end
  local a, b = track[lo], track[hi]
  local f = b.t > a.t and (t - a.t) / (b.t - a.t) or 0
  return a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f, wrap(a.yaw + wrap(b.yaw - a.yaw) * f)
 end
 -- SetCallHorse warps to the start, heading atan2(goal - start) (0x140E6AA90).
 function H.goal(mode, x, y, z, yaw, at)
  if mode == "yaw20" then return {x, y, z}, {x + 0.02 * sin(yaw), y, z + 0.02 * cos(yaw)} end
  if mode == "chase1" or mode == "chase2" then
   return at and {at.x, at.y, at.z} or {x, y, z}, {x + 3 * sin(yaw), y, z + 3 * cos(yaw)}
  end
  return {x, y, z}, {x, y, z}
 end
 function H.stats(list)
  local s = {}
  for i = 1, #list do s[i] = list[i] end
  table.sort(s)
  local n = #s
  if n == 0 then return {p50 = -1, p95 = -1, max = -1, n = 0} end
  local function q(p) return s[math.min(n, math.max(1, floor(p * n + 0.5)))] end
  return {p50 = q(0.5), p95 = q(0.95), max = s[n], n = n}
 end
 local function census()
  if type(W) ~= "table" or type(W.receipt) ~= "function" or not finite(W.mission) or not finite(W.epoch) then
   H.why = "context"; return nil
  end
  local ok, r = pcall(W.receipt, fmt("return 'MGS_COOP_RIDE_POSE_V1' -- %d %d 1", W.mission, W.epoch))
  if not ok or type(r) ~= "table" or r.ready ~= true or type(r.actors) ~= "table" then
   H.why = ok and type(r) == "table" and tostring(r.reason) or "receipt"; return nil
  end
  for i = 1, #r.actors do
   local a = r.actors[i]
   if type(a) == "table" and a.id == HORSE and a.realized == true and finite(a.x) and finite(a.y) and finite(a.z) then
    return {x = a.x / 1000, y = a.y / 1000, z = a.z / 1000, yaw = finite(a.yaw) and a.yaw / 1000 or 0, occupied = a.occupied == true}
   end
  end
  H.why = "no_horse"
 end
 local function mounted() local ok, y = pcall(function() return PlayerInfo.OrCheckStatus({PlayerStatus.ON_HORSE}) end); return ok and y == true end
 local function send(id, cmd)
  local s = type(W) == "table" and type(W.api) == "table" and W.api.send
  if type(s) ~= "function" then return false end
  return pcall(s, id, cmd)
 end
 local function vec(p)
  local v = type(W) == "table" and type(W.api) == "table" and W.api.buddyVector
  if type(v) ~= "function" then return nil end
  local ok, r = pcall(v, p[1], p[2], p[3])
  return ok and r or nil
 end
 local function localPos()
  local x, y, z = get(vars, "playerPosX"), get(vars, "playerPosY"), get(vars, "playerPosZ")
  if finite(x) and finite(y) and finite(z) then return x, y, z end
 end
 local function frame(h, x, y, z)
  local dx, dz, s, c = x - h.x, z - h.z, sin(h.yaw), cos(h.yaw)
  return round((dx * s + dz * c) * 100), round((dx * c - dz * s) * 100), round((y - h.y) * 100)
 end
 local function gpos(id)
  local ok, v = send(id, {id = "GetPosition"})
  if not ok or v == nil then return nil end
  local read, x, y, z = pcall(function() return v:GetX(), v:GetY(), v:GetZ() end)
  if read and finite(x) and finite(y) and finite(z) then return x, y, z end
 end
 local function nearestGuard(h)
  local defs, idf, null = get(mvars, "ene_soldierDefine"), get(GameObject, "GetGameObjectId"), get(GameObject, "NULL_ID")
  if type(defs) ~= "table" or type(idf) ~= "function" then return nil end
  local best, bd, seen, count = nil, 16, {}, 0
  for _, group in pairs(defs) do
   if type(group) == "table" then
    for _, name in ipairs(group) do
     if type(name) == "string" and not seen[name] and count < 512 then
      seen[name], count = true, count + 1
      local ok, id = pcall(idf, "TppSoldier2", name)
      if ok and finite(id) and id ~= null then
       local x, _, z = gpos(id)
       if x then
        local d = (x - h.x) ^ 2 + (z - h.z) ^ 2
        if d < bd then best, bd = id, d end
       end
      end
     end
    end
   end
  end
  return best
 end
 local function setHold(on)
  local f = get(Player, on and "SetPadMask" or "ResetPadMask")
  if type(f) ~= "function" then return false end
  local bit = get(PlayerPad, "ACTION")
  local ok, r
  if on then ok, r = pcall(f, {settingName = MASK, except = false, buttons = type(bit) == "number" and bit or 0x20, sticks = 0, triggers = 0})
  else ok, r = pcall(f, {settingName = MASK}) end
  if ok and r ~= false then H.hold, H.raw, H.rawOn = on, false, true; return true end
  return false
 end
 local function cargo(hold)
  if type(W) == "table" and type(W.receipt) == "function" then
   pcall(W.receipt, "return 'MGS_COOP_CARGO_V2' -- " .. (hold and "1" or "0") .. " 0 0")
  end
 end
 local function finish(why)
  local p = H.probe
  if not p then return end
  H.probe = nil
  if p.kind == "rec" then
   local n = #H.track
   log(fmt("horse_probe=rec;step=done;n=%d;dur_ms=%d;path_cm=%d;occ=%d;miss=%d;why=%s", n,
    n > 0 and round(H.track[n].t - H.track[1].t) or 0, round(p.path * 100), p.occ, p.miss, why))
  elseif p.kind == "play" then
   local e, y = H.stats(p.errs), H.stats(p.yaws)
   log(fmt("horse_probe=play_stats;mode=%s;n=%d;sends=%d;err_p50_cm=%d;err_p95_cm=%d;err_max_cm=%d;leap=%d;dy_max_cm=%d;yaw_p50_deg=%d;miss=%d;why=%s",
    p.mode, e.n, p.sends, round(e.p50), round(e.p95), round(e.max), p.leap, round(p.dyMax), round(y.p50), p.miss, why))
  elseif p.kind == "watch" then
   log(fmt("horse_probe=watch;step=done;n=%d;why=%s", p.n, why))
  elseif p.kind == "pin" then
   cargo(false)
   log(fmt("horse_probe=pin;step=end;n=%d;why=%s", p.n, why))
  end
 end
 local function hostage()
  local id = {type = "TppHorse2", index = 0}
  local ok1, is = send(id, {id = "IsCarriedHostage"})
  local ok2, got = send(id, {id = "GetCarriedHostage"})
  local ok3, ride = send(id, {id = "GetRidePlayer"})
  return ok1 and tostring(is) or "err", ok2 and tostring(got) or "err", ok3 and tostring(ride) or "err"
 end
 local function tickPlay(p, t)
  local rt = t - p.t0
  if rt >= p.ms or rt > p.dur then return finish("timer") end
  local x, y, z, yaw = H.interp(H.track, rt + H.track[1].t)
  local h = census()
  if h then
   local err, dy = sqrt((h.x - x) ^ 2 + (h.z - z) ^ 2) * 100, (h.y - (p.sy or y)) * 100
   p.errs[#p.errs + 1] = err
   local yd = math.abs(wrap(h.yaw - (p.syaw or yaw))) * 180 / pi
   p.yaws[#p.yaws + 1] = yd
   if dy > p.dyMax then p.dyMax = dy end
   if dy > 30 and not p.up then p.up, p.leap = true, p.leap + 1 elseif dy <= 30 then p.up = false end
   if t >= p.nextLog then
    p.nextLog = t + 500
    log(fmt("horse_probe=play;mode=%s;rt=%d;err_cm=%d;dy_cm=%d;yaw_deg=%d;occ=%d", p.mode, round(rt), round(err), round(dy), round(yd), h.occupied and 1 or 0))
   end
  else p.miss = p.miss + 1 end
  if t >= p.next then
   p.next = p.next + p.period
   if p.next <= t then p.next = t + p.period end
   local s, g = H.goal(p.mode, x, y, z, yaw, h)
   p.sy, p.syaw = s[2], (p.mode:sub(1, 2) == "sg") and 0 or yaw
   local sv, gv = vec(s), vec(g)
   if sv and gv and send({type = "TppHorse2", index = 0}, {id = "SetCallHorse", startPosition = sv, goalPosition = gv}) then
    p.sends = p.sends + 1
   end
  end
 end
 local function tick()
  local t = now()
  if H.rawOn then
   local d = type(W) == "table" and W.downed
   local at = type(d) == "table" and d.rawHeldAt
   local raw = type(d) == "table" and d.rawHeld == true and finite(at) and t >= at and t - at < 300 or false
   if raw ~= H.raw then
    H.raw = raw
    local h, x, _, z = census(), localPos()
    log(fmt("horse_probe=raw_e;held=%d;dist_cm=%d;mounted=%d", raw and 1 or 0,
     (h and x) and round(sqrt((h.x - x) ^ 2 + (h.z - z) ^ 2) * 100) or -1, mounted() and 1 or 0))
   end
  end
  local p = H.probe
  if not p then return end
  if type(W) == "table" and W.mission ~= p.mission then return finish("mission") end
  if p.kind == "rec" then
   if t - p.t0 >= p.ms or #H.track >= MAX_SAMPLES then return finish("timer") end
   local h = census()
   if not h then p.miss = p.miss + 1; return end
   local last = H.track[#H.track]
   if last then p.path = p.path + sqrt((h.x - last.x) ^ 2 + (h.z - last.z) ^ 2) end
   if h.occupied then p.occ = p.occ + 1 end
   H.track[#H.track + 1] = {t = t - p.t0, x = h.x, y = h.y, z = h.z, yaw = h.yaw}
  elseif p.kind == "play" then
   tickPlay(p, t)
  elseif p.kind == "watch" then
   if t - p.t0 >= p.ms then return finish("timer") end
   if t < p.next then return end
   p.next, p.n = t + 250, p.n + 1
   local h = census()
   if not h then log("horse_probe=watch;h=none;why=" .. tostring(H.why)); return end
   local px, py, pz = localPos()
   local pf, pr, pu = 0, 0, 0
   if px then pf, pr, pu = frame(h, px, py, pz) end
   if not p.guard and t >= p.scanAt then p.guard, p.scanAt = nearestGuard(h), t + 2000 end
   local g = "none"
   if p.guard then
    local gx, gy, gz = gpos(p.guard)
    if gx then local f, r, u = frame(h, gx, gy, gz); g = fmt("%d,%d,%d", f, r, u) else p.guard = nil end
   end
   local is = hostage()
   log(fmt("horse_probe=watch;t=%d;h=%d,%d,%d,%d;p=%d,%d,%d;m=%d;g=%s;is=%s;occ=%d", round(t - p.t0), round(h.x * 100), round(h.y * 100),
    round(h.z * 100), round(h.yaw * 1000), pf, pr, pu, mounted() and 1 or 0, g, is, h.occupied and 1 or 0))
  elseif p.kind == "pin" then
   if t - p.t0 >= p.ms then return finish("timer") end
   cargo(true)
   local b = p.base
   if send({type = "TppPlayer2", index = 1}, {id = "Warp", pos = {b[1], b[2], b[3]}, rotY = 0}) then p.n = p.n + 1 end
  end
 end
 local function whole(s, lo, hi) local n = tonumber(s); if n and n == floor(n) and n >= lo and n <= hi then return n end end
 local function start(p, line)
  finish("replaced")
  p.t0, p.mission = now(), type(W) == "table" and W.mission or nil
  H.probe = p
  log(line)
  return true, line
 end
 function H.command(a)
  if not lab() then return false, "horse_probe;refused=mode" end
  local sub = a[2]
  if sub == "status" and #a == 2 then
   return true, fmt("horse_probe=status;probe=%s;hold=%d;track=%d;why=%s", H.probe and H.probe.kind or "none", H.hold and 1 or 0,
    #H.track, tostring(H.why or "none"))
  elseif sub == "stop" and #a == 2 then
   finish("stop")
   if H.hold then setHold(false) end
   H.rawOn = nil
   return true, "horse_probe=stopped"
  elseif sub == "hold" and #a == 3 and (a[3] == "0" or a[3] == "1") then
   local ok = setHold(a[3] == "1")
   local line = fmt("horse_probe=hold;on=%s;ok=%d;mounted=%d", a[3], ok and 1 or 0, mounted() and 1 or 0)
   log(line)
   return ok, line
  elseif sub == "hostage" and #a == 2 then
   local is, got, ride = hostage()
   local line = fmt("horse_probe=hostage;is=%s;get=%s;ride=%s;mounted=%d", is, got, ride, mounted() and 1 or 0)
   log(line)
   return true, line
  elseif sub == "rec" and #a == 3 then
   local ms = whole(a[3], 100, MAX_MS)
   if not ms then return false, "horse_probe;refused=args" end
   H.track = {}
   return start({kind = "rec", ms = ms, occ = 0, path = 0, miss = 0}, "horse_probe=rec;step=start;ms=" .. ms)
  elseif sub == "play" and #a == 4 then
   local ms, period = whole(a[4], 100, MAX_MS), PERIOD[a[3]]
   if not ms then return false, "horse_probe;refused=args" end
   if #H.track < 2 then return false, "horse_probe;refused=no_track" end
   if not period then return false, "horse_probe;refused=mode_name" end
   if mounted() then return false, "horse_probe;refused=mounted" end
   local dur = H.track[#H.track].t - H.track[1].t
   return start({kind = "play", mode = a[3], period = period, ms = ms, dur = dur, next = 0, nextLog = 0, errs = {}, yaws = {},
    sends = 0, leap = 0, dyMax = -1e9, miss = 0, up = false}, fmt("horse_probe=play;step=start;mode=%s;n=%d;dur_ms=%d", a[3], #H.track, round(dur)))
  elseif sub == "watch" and #a == 3 then
   local ms = whole(a[3], 100, MAX_MS)
   if not ms then return false, "horse_probe;refused=args" end
   return start({kind = "watch", ms = ms, next = 0, n = 0, scanAt = 0}, "horse_probe=watch;step=start;ms=" .. ms)
  elseif sub == "pin" and #a == 4 then
   local dy, ms = whole(a[3], -500, 500), whole(a[4], 100, MAX_MS)
   if not dy or not ms then return false, "horse_probe;refused=args" end
   local d = type(W) == "table" and W.downed
   local r = type(d) == "table" and type(d.records) == "table" and type(W.role) == "number" and d.records[3 - W.role]
   local q = type(r) == "table" and r.pos
   if type(q) ~= "table" or not finite(q[1]) or not finite(q[2]) or not finite(q[3]) then return false, "horse_probe;refused=no_partner" end
   local b = {q[1] / 1000, q[2] / 1000 + dy / 100, q[3] / 1000}
   return start({kind = "pin", ms = ms, n = 0, base = b},
    fmt("horse_probe=pin;step=start;dy_cm=%d;at=%d,%d,%d", dy, round(b[1] * 100), round(b[2] * 100), round(b[3] * 100)))
  elseif sub == "cargo_pose" and #a == 7 then
   local C, p, q, b, d, r = W.horseCargo, whole(a[3], 0, 3000), whole(a[4], 0, 3000), whole(a[5], -1000, 1500), whole(a[6], 0, 20000),
    whole(a[7], 0, 3)
   if type(C) ~= "table" or not (p and q and b and d and r) or not C.setVictimPose(p, q, b, d, r) then return false, "horse_probe;refused=args" end
   return true, fmt("horse_probe=cargo_pose;placed_mm=%d;census_mm=%d;back_mm=%d;down_mmps=%d;reset=%d", p, q, b, d, r)
  end
  return false, "horse_probe;refused=args"
 end
 function H.stopAll() finish("reset"); if H.hold then setHold(false) end end
 P.handles.horse_probe = true
 local base, stop = P.run, P.stop
 P.run = function(a, id)
  if type(a) == "table" and a[1] == "horse_probe" then return H.command(a) end
  return base(a, id)
 end
 P.stop = function(...)
  pcall(H.stopAll)
  if type(stop) == "function" then return stop(...) end
 end
 local update = e.OnUpdate
 if type(update) == "function" then
  local function updated(...)
   local ok, err = pcall(tick)
   if not ok then log("horse_probe=error;" .. tostring(err)); pcall(finish, "error") end
   return ...
  end
  e.OnUpdate = function(...) return updated(update(...)) end
 end
 end
end
