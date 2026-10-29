-- M2 D-Dog hold and the companion lab levers (original; docs/vehicles-companions-design.md 6.2, 6.12; lab key
-- companion_host_owned). Split from world_buddy.lua (B.hostCompanion, B.guestCompanion, the E3/E19 watch and levers)
-- and world_companion.lua (C.lever, C.seedLever) when both neared the 32000-byte world chunk limit (review
-- 2026-10-29). Spliced after world_companion.lua; each half runs only when its module loaded.
do
local W = e.MgsCoopWorld
local floor, sqrt, min, fmt = math.floor, math.sqrt, math.min, string.format
if type(W) == "table" and type(W.buddy) == "table" and type(W.buddy.lib) == "table" then
local B, lib = W.buddy, W.buddy.lib
local finite, integer, vector, yaw, mm, kind = lib.finite, lib.integer, lib.vector, lib.yaw, lib.mm, lib.kind
local CPOSE, CACK = W.OP.COMPANION_POSE, W.OP.COMPANION_ACK
local companion = B.companion
-- M2 host: the dog read every 100 ms (the receipt no longer needs body 1); sent when it moved 5 cm, its kind or
-- presence changed, or 500 ms passed. present=0 only when the dog itself is unreadable.
function B.hostCompanion(now)
local s, a = B.s, W.api
if now < (s.nextRead or 0) then return end
s.nextRead = now + 100
local k, present, x, y, z = kind(a.buddyType()), 0, 0, 0, 0
if k == 1 then
 local id; id, x, y, z = B.dog()
 if id then present = 1; s.hostPos = s.localPos else x, y, z = 0, 0, 0 end
end
local last = s.sent
local moved = last and present == 1 and (x - last.x) ^ 2 + (y - last.y) ^ 2 + (z - last.z) ^ 2 >= 0.0025
if last and last.k == k and last.present == present and not moved and now < (s.nextIdle or 0) then return end
if W.tx(CPOSE, W.tick32(now), k, 0, present, mm(x), mm(y), mm(z), mm(yaw()), 0) then
 s.sent, s.nextIdle, s.poseTx = {k = k, present = present, x = x, y = y, z = z, yaw = yaw()}, now + 500, (s.poseTx or 0) + 1
end
end
W.handlers[CPOSE] = function(v, now)
if W.host or not W.paired or W.eff < 3 or #v ~= 10 then return end
if not integer(v[1], 0, 4294967295) or not integer(v[2], 0, 4) or not integer(v[3], 0, 1) or not integer(v[4], 0, 1)
 or not integer(v[5], -1000000000, 1000000000) or not integer(v[6], -1000000000, 1000000000)
 or not integer(v[7], -1000000000, 1000000000) or not integer(v[8], -3142, 3142) or not integer(v[9], 0, 255)
 or not integer(v[10], 0, 65535) then return end
local s, p = B.s, B.s.cpose
if p then
 local d = (v[1] - p.tick) % 4294967296
 if d == 0 or d >= 2147483648 then return end
end
local r = {tick = v[1], kind = v[2], present = v[4] == 1, x = v[5] / 1000, y = v[6] / 1000, z = v[7] / 1000, yaw = v[8] / 1000,
 at = now - v[10], vx = 0, vz = 0, speed = 0}
if r.present and p and p.present and p.kind == r.kind then
 local dt = ((r.tick - p.tick) % 4294967296) / 1000
 -- Only consecutive 10 Hz samples (a moving dog); a sample after a pause carries no velocity.
 if dt > 0 and dt <= 0.3 then
  r.vx, r.vz = (r.x - p.x) / dt, (r.z - p.z) / dt
  r.speed = sqrt(r.vx * r.vx + r.vz * r.vz)
 end
end
s.cpose, s.cposeN = r, (s.cposeN or 0) + 1
if r.kind == 1 and r.present then s.hostPos = r end
end
W.handlers[CACK] = function(v, now)
if not W.host or not W.paired or W.eff < 3 or #v ~= 4 then return end
if not (integer(v[1], 1, 65535) and integer(v[2], 0, 4) and integer(v[3], 0, 7) and integer(v[4], 0, 65535)) then return end
local s = B.s
if s.remoteResult ~= v[3] or now == nil or now >= (s.remoteLogAt or 0) then
 s.remoteLogAt = (now or 0) + 10000
 W.log(fmt("event=companion_ack;role=host;result=%d;error_cm=%d", v[3], v[4]))
end
s.remoteResult, s.remoteError = v[3], v[4]
end
-- Guest COMPANION_ACK results: 0 holding at the host's pose, 1 loading or unreadable, 2 refused or another kind,
-- 3 stale (held at the last pose), 5 held where it stands (no host pose yet).
function B.cack(result, errorCm, now)
local s = B.s
if now < (s.nextCack or 0) and s.cackResult == result then return end
s.cackSeq = (s.cackSeq or 0) % 65535 + 1
if W.tx(CACK, s.cackSeq, 1, result, min(errorCm or 65535, 65535)) then s.nextCack, s.cackResult = now + 1000, result end
end
-- The proof's tracking: 2 s windows of the host dog's speed (walk below 3.5 m/s: 3 m; run and dash: 5 m) and the error.
function B.track(now, errorCm, speed)
local s = B.s
local t = s.track or {from = now, n = 0, max = 0, sum = 0, speed = 0, errs = {}}
s.track = t
t.n, t.max, t.sum, t.speed = t.n + 1, math.max(t.max, errorCm), t.sum + errorCm, t.speed + speed
t.errs[#t.errs + 1] = errorCm
if now - t.from < 2000 then return end
s.track = nil
table.sort(t.errs)
local v = t.speed / t.n
local band = v < 0.5 and "idle" or v < 3.5 and "walk" or "run"
if band == "idle" and now < (s.idleTrackAt or 0) then return end
if band == "idle" then s.idleTrackAt = now + 10000 end
W.log(fmt("event=companion_track;band=%s;n=%d;speed_cms=%d;err_max_cm=%d;err_p90_cm=%d;err_mean_cm=%d;within=%d;leg=%s", band, t.n,
 floor(v * 100 + 0.5), t.max, t.errs[math.max(1, math.ceil(#t.errs * 0.9))], floor(t.sum / t.n + 0.5),
 t.max <= (band == "run" and 500 or 300) and 1 or 0, s.leg or "-") .. fmt(";mode=%s;warps=%d", B.holdMode, s.warps or 0))
end
-- Lab lever companion_lab mark <label>: names the following track windows (the proof's legs) and starts a new one.
function B.mark(label)
local s = B.s
s.leg, s.track, s.idleTrackAt = label, nil, nil
W.log("event=companion_mark;role=" .. (W.host and "host" or "guest") .. ";leg=" .. label)
return true
end
-- E3 / E19 watch (guest, lab): the nearest soldier to the held dog (8 of the roster per hold, those within 30 m
-- re-read every second), seconds with one within 3 m / 5 m, the buddy vars on change.
local function soldier(id)
local ok, p = pcall(W.api.send, id, {id = "GetPosition"})
if ok then return vector(p) end
end
function B.watch(now, x, y, z)
local s = B.s
local w = s.watch or {i = 0, near = {}, s3 = 0, s5 = 0, min = 65535}
s.watch = w
local order, ent = W.entOrder or {}, W.ent or {}
local dead, flag, pre = W.get(vars, "deadBuddyType"), W.get(vars, "buddyCommonFlag"), s.preVars
-- D7 candidate (repair): within 10 s of the dog's Dead message, write back the flags it had before.
if s.repairOn and pre and now - (s.deadAt or -1e9) <= 10000 and (dead ~= pre[1] or flag ~= pre[2]) then
 pcall(function() vars.deadBuddyType, vars.buddyCommonFlag = pre[1], pre[2] end)
 s.repairs = (s.repairs or 0) + 1
 W.log(fmt("event=companion_repair;role=guest;step=vars;dead=%s>%s;flag=%s>%s", tostring(dead), tostring(pre[1]), tostring(flag),
  tostring(pre[2])))
 dead, flag = W.get(vars, "deadBuddyType"), W.get(vars, "buddyCommonFlag")
end
if now - (s.deadAt or -1e9) > 10000 then s.lastVars = {dead, flag} end
local key = fmt("dead=%s;flag=%s;active=%s", tostring(dead), tostring(flag), tostring(W.api.buddyType()))
if key ~= w.vars then w.vars = key; W.log("event=mirror_vars;" .. key) end
for _ = 1, min(8, #order) do
 w.i = w.i % #order + 1
 local h = order[w.i]
 local e = ent[h]
 if e and type(e.id) == "number" then
  local sx, sy, sz = soldier(e.id)
  if sx then w.near[h] = (sx - x) ^ 2 + (sy - y) ^ 2 + (sz - z) ^ 2 <= 900 or nil end
 end
end
if now < (w.nextNear or 0) then return end
local dt = w.lastNear and (now - w.lastNear) / 1000 or 0
w.nextNear, w.lastNear = now + 1000, now
local best, bestH = 65535, nil
for h in pairs(w.near) do
 local e = ent[h]
 local sx, sy, sz
 if e and type(e.id) == "number" then sx, sy, sz = soldier(e.id) end
 if sx then
  local d = floor(sqrt((sx - x) ^ 2 + (sy - y) ^ 2 + (sz - z) ^ 2) * 100 + 0.5)
  if d < best then best, bestH = d, h end
 end
end
w.bestPos = nil
if bestH and best < 3000 then
 local e = ent[bestH]
 local sx, sy, sz = soldier(e.id)
 if sx then w.bestPos = {sx, sy, sz} end
end
if best <= 300 then w.s3 = w.s3 + dt end
if best <= 500 then w.s5 = w.s5 + dt end
w.min = min(w.min, best)
if bestH and best <= 1000 and now >= (w.logAt or 0) then
 w.logAt = now + 5000
 W.log(fmt("event=mirror_near;ent=%.0f;d_cm=%d;s3=%d;s5=%d;engaged=%d;hurt=%d", bestH, best, floor(w.s3), floor(w.s5),
  W.mirrorCount or 0, s.hurt or 0))
end
end
-- E19: a damage message whose victim is this copy's dog (world_sync.lua W.observe, unmapped ids); both roles.
function W.companionHurt(msg, id, cls)
if id ~= 9728 or not companion() then return end
local s = B.s
if msg == "Dead" then s.deadAt, s.preVars = W.now(), s.lastVars end
s.hurt = (s.hurt or 0) + 1
if s.hurt <= 60 then W.log(fmt("event=companion_hurt;role=%s;msg=%s;att=%s;n=%d", W.host and "host" or "guest", tostring(msg), tostring(cls), s.hurt)) end
end
-- E3 lab lever companion_lab near_guard on|off|shield (guest): hold the dog 1.5 m from the nearest soldier (on), or
-- (E19, shield) 1 m from the guest toward it: in the line of fire of a guard shooting at a prone guest.
local NEAR = {[true] = "guard", on = "guard", shield = "shield", [false] = false, off = false}
function B.nearGuard(on)
if W.mode ~= 1 or NEAR[on] == nil then return false end
B.s.nearGuard, B.s.sentHold = NEAR[on] or nil, nil
W.log("event=companion_near_guard;role=guest;on=" .. (NEAR[on] == "shield" and "shield" or NEAR[on] and 1 or 0))
return true
end
-- D7 candidate (guest lab lever companion_lab repair on|off; default off): undo the dog's death flags (B.watch).
function B.repair(on)
if W.mode ~= 1 then return false end
B.s.repairOn = on == true or nil
W.log("event=companion_repair;role=guest;on=" .. (on and 1 or 0))
return true
end
-- E19 protection candidate (guest lab lever companion_lab protect [off]): SetDisableDamage on the held dog, once per
-- dog; off sends every flag false once to a protected dog.
function B.protect(on)
local s = B.s
local function send(v)
 local ok, r = pcall(W.api.send, s.id, {id = "SetDisableDamage", life = v, faint = v, sleep = v})
 W.log(fmt("event=companion_protect;role=guest;id=%d;ok=%d;result=%s;on=%d", s.id, ok and 1 or 0, tostring(r), v and 1 or 0))
 return ok
end
if on ~= nil then
 local was = s.protected
 s.protectOn, s.protected = on == true, nil
 if on == false then return was ~= nil and s.id ~= nil and send(false) end
end
if not s.protectOn or s.protected == s.id or not s.id then return s.protected ~= nil end
s.protected = s.id
return send(true)
end
-- Hold modes (design 3.2, 8.2; lab lever companion_lab hold <warp|marker|stop>). LuaAiStayAndSnarl sets mode 1/2 (look,
-- useMarker false: the s10040 stop AI, skipped by the Dog AI) or 3/4 (useMarker), +1 with dash; warp=1 places the dog.
-- warp (default): 1/2 placed whenever the target moved 5 cm, the dog is 30 cm off it, or 1 s passed; marker: 3/4, the
-- dog walks itself (lagged 7-15 m), re-sent on a 0.75 m change or every 1 s; stop: 1/2, no placement.
B.HOLD, B.holdMode = {marker = true, stop = true, warp = true}, "warp"
function B.setHold(mode)
if not B.HOLD[mode] then return false end
B.holdMode, B.s.sentHold, B.s.farSince = mode, nil, nil
W.log("event=companion_hold;role=" .. (W.host and "host" or "guest") .. ";mode=" .. mode)
return true
end
-- M2 guest: the hold. Target: the host dog's latest pose led by its age (at most 300 ms) while fresh (1 s);
-- else the last target (hold_last); with no host pose yet, where the dog stands (hold_here). Never its own AI.
function B.guestCompanion(now)
local s, a, r = B.s, W.api, B.s.cpose
if a.buddyType() ~= a.buddyDog then return end
if now < s.nextApply then return end
s.nextApply = now + 100
local id, x, y, z = B.dog()
if not id then B.cack(1, 65535, now); B.log("dog_unreadable"); return end
s.owned, s.id, s.ownedMission, s.ownedEpoch = true, id, W.mission, W.epoch
local fresh = r and r.kind == 1 and r.present and now >= r.at and now - r.at <= 1000
local why, result
if fresh then
 -- Lead by the sample age while the stream is live (a quiet stream: the dog stopped, no extrapolation).
 local age = (now - r.at) / 1000
 if age > 0.35 then age = 0 end
 s.hold = {x = r.x + r.vx * age, y = r.y, z = r.z + r.vz * age, yaw = r.yaw}
 why, result = "target_submitted", 0
elseif s.hold then why, result = "hold_last", 3
else s.hold, why, result = {x = x, y = y, z = z, yaw = yaw()}, "hold_here", 5 end
-- E3 lab lever: the nearest soldier (B.watch) + 1.5 m instead of the host's pose.
local g = s.nearGuard and s.watch and s.watch.bestPos
if g and s.nearGuard == "shield" then
 local px, py, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
 local d = finite(px, 1000000) and finite(py, 1000000) and finite(pz, 1000000) and sqrt((g[1] - px) ^ 2 + (g[3] - pz) ^ 2) or 0
 if d > 0.5 then s.hold, why = {x = px + (g[1] - px) / d, y = py, z = pz + (g[3] - pz) / d, yaw = s.hold.yaw}, "shield" end
elseif g then s.hold, why = {x = g[1] + 1.5, y = g[2], z = g[3], yaw = s.hold.yaw}, "near_guard" end
local t = s.hold
local dx, dy, dz = t.x - x, t.y - y, t.z - z
local distance = sqrt(dx * dx + dy * dy + dz * dz)
local errorCm = min(floor(distance * 100 + 0.5), 65535)
local mode = B.holdMode
-- SetWarpPosition beyond 15 m, or when the dog has not closed a 6 m gap for 2 s (not in warp mode: it places itself).
local stuck = mode ~= "warp" and fresh and distance > 6
if stuck then s.farSince = s.farSince or now else s.farSince = nil end
if fresh and (distance > 15 or (stuck and now - s.farSince >= 2000)) then
 a.send(id, {id = "SetWarpPosition", pos = a.buddyVector(t.x, t.y, t.z)})
 s.warps, s.farSince = (s.warps or 0) + 1, nil
end
-- Warp mode: a dog over 1 m off an unchanged target for 200 ms (4-6 m after a run leg) gets the stock warp (500 ms).
local p0 = s.sentHold
local drift = mode == "warp" and p0 and distance > 1 and (t.x - p0.x) ^ 2 + (t.y - p0.y) ^ 2 + (t.z - p0.z) ^ 2 < 0.0025
if drift then s.driftSince = s.driftSince or now else s.driftSince = nil end
if drift and now - s.driftSince >= 200 and now >= (s.driftAt or 0) then
 a.send(id, {id = "SetWarpPosition", pos = a.buddyVector(t.x, t.y, t.z)})
 s.warps, s.driftSince, s.driftAt = (s.warps or 0) + 1, nil, now + 500
end
s.aiHeld = true
local warp = mode == "warp"
local dash = not warp and (distance > 2 or (fresh and r.speed > 3)) and 1 or 0
local last = s.sentHold
if not last or now >= last.at + 1000 or last.dash ~= dash or (warp and distance > 0.3) or
 (t.x - last.x) ^ 2 + (t.y - last.y) ^ 2 + (t.z - last.z) ^ 2 >= (warp and 0.0025 or 0.5625) then
 s.sentHold = {x = t.x, y = t.y, z = t.z, at = now, dash = dash}
 if a.send(id, {id = "LuaAiStayAndSnarl", position = a.buddyVector(t.x, t.y, t.z),
  look = a.buddyVector(t.x + math.sin(t.yaw) * 2, t.y, t.z + math.cos(t.yaw) * 2), useMarker = mode == "marker",
  dash = dash, warp = warp and 1 or 0, ignoreMarker = true}) == false then
  B.cack(2, errorCm, now); B.log("target_refused", errorCm); return
 end
end
if fresh then B.track(now, errorCm, r.speed) end
B.protect()
if W.mode == 1 then pcall(B.watch, now, x, y, z) end
B.cack(result, errorCm, now); B.log(why, errorCm)
end
end
local C = type(W) == "table" and W.companion
if type(C) == "table" and type(C.lib) == "table" then
local lib = C.lib
local BT, TBC, active, blockActive, v3, typeName = lib.BT, lib.TBC, lib.active, lib.blockActive, lib.vector, C.typeName
-- E20: the Quiet cell (its radio locator, else the Medical main plant's centre).
function C.cellPos()
 local ok, p = pcall(Tpp.GetLocator, "quiet_AssetIdentifier", "radio_pos")
 if ok and type(p) == "table" and type(p[1]) == "number" and type(p[2]) == "number" and type(p[3]) == "number" then
  return {p[1], p[2], p[3]}, "radio"
 end
 local good, x, y, z = pcall(function()
  local c = mtbs_cluster.GetDemoCenter("Medical", "plnt0")
  if type(c) == "table" then return c[1], c[2], c[3] end
  return c:GetX(), c:GetY(), c:GetZ()
 end)
 if good and type(x) == "number" and type(y) == "number" and type(z) == "number" then return {x, y, z}, "plnt0" end
end
local SEED = {none = "NONE", horse = "HORSE", dog = "DOG", quiet = "QUIET", walker = "WALKER_GEAR"}
-- debug_buddy.lua routes companion_status, seed_buddy <kind> and companion_cell [goto] here.
function C.lever(a, id, A, mission)
 if a[1] == "companion_status" and #a == 1 then return true, C.status() end
 if a[1] == "seed_buddy" and #a == 2 then return C.seedLever(a[2]) end
 if a[1] == "companion_cell" and (#a == 1 or (#a == 2 and a[2] == "goto")) then
  if mission ~= 30050 then return false, "cell_mission" end
  local p, src = C.cellPos()
  if not p then return false, "cell_unavailable" end
  if #a == 1 then return true, fmt("cell;src=%s;pos=%.2f,%.2f,%.2f", src, p[1], p[2], p[3]) end
  -- Inside the cell: 0.8 m from the radio toward the Medical plant's centre, on the radio's floor
  -- (2.5 m east of it was the sea: run 9e8f22a7 ended MISSION FAILED). Before the radio locator
  -- loads: 1 m above the plant's centre.
  if src == "radio" then
   local c = C.cellCentre
   local dx, dz = c and c[1] - p[1] or 0, c and c[3] - p[3] or 1
   local d = math.sqrt(dx * dx + dz * dz)
   if d < 0.01 then dx, dz, d = 0, 1, 1 end
   return A.warp({p[1] + 0.8 * dx / d, p[2] + 0.05, p[3] + 0.8 * dz / d, math.deg(math.atan2(-dx, -dz))}, id)
  end
  C.cellCentre = p
  return A.warp({p[1], p[2] + 1, p[3], 0}, id)
 end
 -- Lab: the host calls its companion (the loaded type, or <kind>: a swap) 5 m in front of it, a
 -- dog also warped there (4 m to the side was off both cameras: runs 6306df68, 9e8f22a7).
 if a[1] == "companion_lab" and a[2] == "call_here" and (#a == 2 or #a == 3) then
  if W.mode ~= 1 or not W.host then return false, "host_lab_only" end
  local t, p = active(), C.front(5)
  if #a == 3 then t = SEED[a[3]] and BT(SEED[a[3]]) end
  if not t or t == BT("NONE") or not p then return false, "call_here_none" end
  local v = v3(p[1], p[2], p[3])
  C.token = true
  local ok, res = pcall(TBC("CallBuddy"), t, v, p[4])
  C.token = nil
  local warp = 0
  if ok and res ~= false and t == BT("DOG") then
   local okId, dog = pcall(W.get(GameObject, "GetGameObjectIdByIndex"), "TppBuddyDog2", 0)
   if okId and dog == 9728 and pcall(W.get(GameObject, "SendCommand"), dog, {id = "SetWarpPosition", pos = v}) then warp = 1 end
  end
  return ok and res ~= false, fmt("call_here;kind=%s;result=%s;warp=%d", typeName(t), tostring(res), warp)
 end
 -- Lab: the late-join case on the host (Quiet already out when the lock applies): the lock is
 -- lifted, Quiet called beside the host, and 8 s later the tick sends her away (delete_buddy).
 if a[1] == "companion_lab" and a[2] == "quiet_out" and #a == 2 then
  if W.mode ~= 1 or not W.host then return false, "host_lab_only" end
  local Q, px, py, pz = BT("QUIET"), W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
  if not Q or type(px) ~= "number" or type(py) ~= "number" or type(pz) ~= "number" then return false, "quiet_out_args" end
  pcall(W.get(TppBuddyService, "UnsetDisableBuddyType"), Q)
  C.quietDisabled, C.deleteAt = nil, W.now() + 8000
  local v, ok, r = v3(px + 3, py, pz), false, nil
  C.token = true
  if blockActive() then ok, r = pcall(TBC("CallBuddy"), Q, v, 0)
  else ok = pcall(TBC("ReserveCallBuddy"), Q, W.get(BuddyInitStatus, "NORMAL") or 0, v, 0) and pcall(TBC("Load")) end
  C.token = nil
  return ok, fmt("quiet_out;call=%s;guest_quiet=%s", tostring(r), tostring(C.guestQuiet))
 end
 -- E19 (guest, lab): the protection candidate on the held dog (world_buddy.lua B.protect: SetDisableDamage).
 if a[1] == "companion_lab" and a[2] == "protect" and (#a == 2 or (#a == 3 and a[3] == "off")) then
  if W.mode ~= 1 or W.host then return false, "guest_lab_only" end
  local B = W.buddy
  if type(W.get(B, "protect")) ~= "function" then return false, "protect_unavailable" end
  local ok, r = pcall(B.protect, #a == 2)
  return ok, fmt("protect;on=%d;result=%s", #a == 2 and 1 or 0, tostring(r))
 end
 -- Lab (guest): the dog hold mode of world_buddy.lua (marker, stop, warp).
 if a[1] == "companion_lab" and a[2] == "hold" and #a == 3 then
  if W.mode ~= 1 or W.host then return false, "guest_lab_only" end
  local B = W.buddy
  if type(W.get(B, "setHold")) ~= "function" then return false, "hold_unavailable" end
  local ok, r = pcall(B.setHold, a[3])
  return ok and r == true, "hold;mode=" .. tostring(a[3])
 end
 -- Lab (guest, E3): hold the dog beside the nearest soldier instead of the host dog's pose; shield (E19): 1 m from
 -- the guest toward that soldier.
 if a[1] == "companion_lab" and a[2] == "near_guard" and #a == 3 and (a[3] == "on" or a[3] == "off" or a[3] == "shield") then
  if W.mode ~= 1 or W.host then return false, "guest_lab_only" end
  local B = W.buddy
  if type(W.get(B, "nearGuard")) ~= "function" then return false, "near_guard_unavailable" end
  local ok, r = pcall(B.nearGuard, a[3] == "shield" and "shield" or a[3] == "on")
  return ok and r == true, "near_guard;on=" .. (a[3] == "shield" and "shield" or a[3] == "on" and 1 or 0)
 end
 -- Lab (guest, D7 candidate): write back the dog's dead-buddy flags at its Dead message (world_buddy.lua B.repair).
 if a[1] == "companion_lab" and a[2] == "repair" and #a == 3 and (a[3] == "on" or a[3] == "off") then
  if W.mode ~= 1 or W.host then return false, "guest_lab_only" end
  local B = W.buddy
  if type(W.get(B, "repair")) ~= "function" then return false, "repair_unavailable" end
  local ok, r = pcall(B.repair, a[3] == "on")
  return ok and r == true, "repair;on=" .. (a[3] == "on" and 1 or 0)
 end
 -- Lab: names the following track windows of world_buddy.lua (the proof's legs: walk, run, dash, away, ...).
 if a[1] == "companion_lab" and a[2] == "mark" and #a == 3 and type(a[3]) == "string" and a[3]:match("^[a-z0-9_]+$") and #a[3] <= 16 then
  if W.mode ~= 1 then return false, "lab_only" end
  local B = W.buddy
  if type(W.get(B, "mark")) ~= "function" then return false, "mark_unavailable" end
  return pcall(B.mark, a[3]) and true or false, "mark;leg=" .. a[3]
 end
 return false, "companion_args"
end
function C.seedLever(kind)
 if W.mode ~= 1 then return false, "lab_only" end
 local t = SEED[kind] and BT(SEED[kind])
 if not t then return false, "seed_args" end
 local S = TppBuddyService
 local okO, obtained = pcall(W.get(S, "DidObtainBuddyType") or W.get(TppBuddy2BlockController, "DidObtainBuddyType"), t)
 local okS, sortie = pcall(W.get(S, "CanSortieBuddyType"), t)
 vars.buddyType, vars.buddyTypeMissionStart = t, t
 if C.own then C.own.buddyType, C.own.buddyTypeMissionStart = t, t end
 local ok = W.follow.run(TppMission.UpdateCheckPoint, {atCurrentPosition = true, ignoreAlert = true, permitHelicopter = true})
 return ok, fmt("seed_buddy;kind=%s;vars_type=%d;checkpoint=%d;obtained=%s;sortie=%s", kind, t, ok and 1 or 0,
  okO and tostring(obtained) or "?", okS and tostring(sortie) or "?")
end
end
end
