-- Horse plan M2, a downed partner on the shared D-Horse (lab key world_horse_cargo with world_horse_share; docs/mounts.md
-- "Downed partner on the horse"). The carrier (CarryRole 1, MGS_COOP_CARGO_V2) stands within 2.5 m of the shared horse
-- (index 0, 6656), facing it, and holds E 450 ms; a horse with a guard on it (IsCarriedHostage) refuses npc_cargo. A
-- guest carrier asks the host over HORSE_SEAT (0x50, seat 2) against the victim's downed generation; the host grants
-- only while that generation is down and not being revived, and grants its own load itself. The carrier's copy plays
-- the stock put-down (RequestLocalPutDown), then SetCargoHold(1) and warps body 1 every tick to the cargo point of its
-- own horse (probe P2: 0.44 m behind the census root, centred). LOADED carries that horse's pose: the victim's copy
-- re-checks its generation, puts its own 6656 there (MGS_COOP_HORSE_DRIVE_V1 place), sets SetCargoLocal(1) and warps
-- body 0 every tick to its own horse's cargo point (M1 moves that horse while the carrier rides). KEEP at 1 Hz holds it.
-- Take-down: on foot within reach of the body, hold R (the stock unload of a guard on the horse, P2) or E for 1 s; both
-- copies lay the body at the carrier's point beside the horse. A horse lost or called away, a stale link, a context
-- change or the victim up or gone lays it down beside the horse. The bleed-out timer keeps running (D-H3). Replaces the
-- offset prototype (op 43 retired); cargo_test 1 runs it in the lab without the cargo key.
do
local W = e.MgsCoopWorld
if type(W) == "table" and type(W.handlers) == "table" and type(W.OP) == "table" then
local C = {testing = false, state = "off", seq = 0, n = {}}
W.horseCargo = C
local OP, HORSE, SEAT = 80, 6656, 2
W.OP.HORSE_SEAT = OP
local CLAIM, GRANT, DENY, LOADED, UNLOADED, KEEP = 1, 2, 3, 4, 5, 6
local CAUSE = {[0] = "ok", "awake", "generation", "npc_cargo", "taken_down", "horse_lost", "stream_lost", "context",
 "victim_gone", "load_timeout", "busy", "call_away", "mid_revive"}
local CODE = {}
for k, v in pairs(CAUSE) do CODE[v] = k end
local HOLD_MS, TAKE_MS, CLAIM_MS, LOAD_MS, KEEP_MS, LOST_MS, HORSE_MS, PLACE_MS = 450, 1000, 1500, 6000, 1000, 3500, 1500, 1000
local REACH_MM, TAKE_MM, CALL_MM, DRIFT_MM, BACK_MM, RISE_MM, SIDE_MM = 2500, 2600, 3000, 500, 440, 1250, 1100
-- Body 1 while this copy's player rides: from the rider's own position (P2: the rider sits 0.10 m ahead of and 0.49 m
-- over the census root, so 0.54 m behind and 0.76 m over the rider is the census cargo point). On foot within 3 m of
-- the horse its height stays 1.1-1.7 m over the carrier's ground (run 7359e4d2: an unridden census drew it ~2 m high).
local RIDE_BACK_MM, RIDE_RISE_MM, GROUND_LO_MM, GROUND_HI_MM, GROUND_MM, NEAR_MM, CAP_MS, STATS_MS = 540, 760, 1100, 1700, 1450, 3000, 4000, 2000
C.BACK_MM, C.RISE_MM, C.SIDE_MM, C.HOLD_MS, C.TAKE_MS = BACK_MM, RISE_MM, SIDE_MM, HOLD_MS, TAKE_MS
C.RIDE_BACK_MM, C.RIDE_RISE_MM, C.GROUND_MM = RIDE_BACK_MM, RIDE_RISE_MM, GROUND_MM
local floor, sin, cos, sqrt, fmt, pi = math.floor, math.sin, math.cos, math.sqrt, string.format, math.pi
local function int(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
local function num(n) return type(n) == "number" and n == n and n > -1e12 and n < 1e12 end
local function round(v) return floor(v + 0.5) end
local function log(s) if type(W.log) == "function" then pcall(W.log, "event=horse_cargo;" .. s) end end
local function capped(kind, s)
 local c = (C.n[kind] or 0) + 1
 C.n[kind] = c
 if c <= 20 then log(s) end
end
local function receipt(text)
 if type(W.receipt) ~= "function" then return nil end
 local ok, r = pcall(W.receipt, text)
 if ok and type(r) == "table" then return r end
end
-- The lab keys (fixed for the process): world_horse_share (on) and world_horse_cargo (cargo), asked until native answers.
function C.keyOn()
 if C.share == nil then
  local r = receipt("return 'MGS_COOP_HORSE_DRIVE_V1' -- cfg")
  if r and (r.on == 0 or r.on == 1) then C.share, C.cargoKey = r.on == 1, r.cargo == 1 end
 end
 return C.share == true and (C.cargoKey == true or C.testing == true)
end
function C.on()
 return W.active ~= false and W.paired == true and (W.eff or 0) >= 3 and (W.mission == 30010 or W.mission == 30020) and
  int(W.epoch, 1, 65535) and int(W.role, 1, 2) and type(W.downed) == "table" and C.keyOn()
end
function C.busy() return C.state == "loading" or C.state == "carrier" or C.state == "cargo" or C.state == "release" end
local function mounted()
 if type(PlayerInfo) ~= "table" or type(PlayerInfo.OrCheckStatus) ~= "function" or type(PlayerStatus) ~= "table" then return false end
 local ok, yes = pcall(PlayerInfo.OrCheckStatus, {PlayerStatus.ON_HORSE})
 return ok and yes == true
end
local function mine()
 local x, y, z = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
 if num(x) and num(y) and num(z) then return x * 1000, y * 1000, z * 1000 end
end
-- This copy's horse: the census pose in mm / mrad (MGS_COOP_HORSE_DRIVE_V1 read).
local function horse()
 local r = receipt(fmt("return 'MGS_COOP_HORSE_DRIVE_V1' -- read %d %d", W.mission, W.epoch))
 if r and r.ready == true and r.realized ~= false and int(r.x, -1000000000, 1000000000) and int(r.y, -1000000000, 1000000000) and
  int(r.z, -1000000000, 1000000000) and int(r.yaw, -31416, 31416) then return r end
end
local function place(p)
 return receipt(fmt("return 'MGS_COOP_HORSE_DRIVE_V1' -- place %d %d %d %d %d %d 0", W.mission, W.epoch, round(p.x), round(p.y),
  round(p.z), round(p.yaw)))
end
-- The cargo point of a horse pose: BACK_MM behind the census root along its heading (sin, cos), RISE_MM up; yaw in rad.
local function cargoPoint(h)
 local a = h.yaw / 1000
 return h.x - BACK_MM * sin(a), h.y + RISE_MM, h.z - BACK_MM * cos(a), a
end
C.cargoPoint = cargoPoint
-- Beside the body on side s (1 the horse's right, -1 its left), at height y (mm).
local function beside(h, s, y)
 local x, _, z, a = cargoPoint(h)
 return {x + s * SIDE_MM * cos(a), y, z - s * SIDE_MM * sin(a), a}
end
local function warp(index, x, y, z, yaw)
 if type(GameObject) ~= "table" or type(GameObject.SendCommand) ~= "function" then return false end
 return pcall(GameObject.SendCommand, {type = "TppPlayer2", index = index}, {id = "Warp", pos = {x / 1000, y / 1000, z / 1000}, rotY = yaw})
end
local function pin(index, h)
 local x, y, z, a = cargoPoint(h)
 return warp(index, x, y, z, a)
end
local function npcCargo()
 if type(GameObject) ~= "table" or type(GameObject.SendCommand) ~= "function" then return false end
 local ok, is = pcall(GameObject.SendCommand, {type = "TppHorse2", index = 0}, {id = "IsCarriedHostage"})
 return ok and is == true
end
-- MGS_COOP_CARGO_V2 "<hold> <putdown> <local>": the carry role (-1 unreadable); C.physical the attachment state.
local function cargo(hold, putdown, loc)
 local r = receipt("return 'MGS_COOP_CARGO_V2' -- " .. (hold and "1" or "0") .. " " .. (putdown and "1" or "0") .. " " .. (loc and "1" or "0"))
 C.physical = nil
 if r and r.ready == true and int(r.role, 0, 2) and type(r.carrying) == "boolean" and type(r.carried) == "boolean" and
  type(r.localCarried) == "boolean" then C.physical = r; return r.role end
 return -1
end
local function send(action, cause, gen, x, y, z, yaw)
 if type(W.tx) ~= "function" then return false end
 C.seq = C.seq % 65535 + 1
 local ym = round(yaw or 0)
 if ym > 31416 or ym < -31416 then ym = 0 end
 return W.tx(OP, C.seq, SEAT, W.role, action, cause or 0, gen or 0, round(x or 0), round(y or 0), round(z or 0), ym)
end
local function rec(role) local d = W.downed; return type(d.records) == "table" and d.records[role] or nil end
local function fresh(r, now, ms) return type(r) == "table" and num(r.at) and now >= r.at and now - r.at < (ms or 1000) end
local function held(key, now)
 local d = W.downed
 local at, on
 if key == "E" then at, on = d.rawHeldAt, d.rawHeld else at, on = d.rawCarryHeldAt, d.rawCarryHeld end
 return on == true and num(at) and now >= at and now - at < 300
end
local function reset()
 C.state, C.heldSince, C.heldDone, C.claimAt, C.loadAt, C.gen, C.hgen, C.h, C.keepAt, C.keepPose = "off", nil, nil, nil, nil, nil, nil, nil, nil, nil
 C.takeSince, C.lostAt, C.park, C.putAt, C.placeAt, C.releaseP, C.releaseAt = nil, nil, nil, nil, nil, nil, nil
 C.takeKey, C.dyFix, C.statsAt = nil, nil, nil
end
-- End this copy's part. why: a CAUSE name (or a local refusal); place: lay the body down (at p, else beside the horse);
-- remote: the partner ended it (nothing is sent back). keep (a take-down on the carrier's copy): body 1 stays held at p
-- until the take-down key is up, so the same R hold never starts a stock pickup of the body just laid down.
local function stop(now, why, place, remote, p, keep)
 local s = C.state
 if s == "carrier" or s == "cargo" then
  local h = C.h
  if place and not p and h then
   local _, y = mine()
   p = beside(h, -1, s == "carrier" and not mounted() and y or h.y)
  end
  if place and p then warp(s == "carrier" and 1 or 0, p[1], p[2], p[3], p[4]) end
  if not remote then send(UNLOADED, CODE[why] or 0, C.gen, p and p[1], p and p[2], p and p[3], p and p[4] * 1000) end
  log(fmt("step=unloaded;seat=2;why=%s;side=%s;placed=%d;x=%d;z=%d%s", why, s, (place and p) and 1 or 0, p and round(p[1] / 10) or 0,
   p and round(p[3] / 10) or 0, remote and ";from=partner" or ""))
 elseif s ~= "off" and s ~= "release" then
  log(fmt("step=refuse;why=%s;state=%s", why, s))
 end
 if keep and s == "carrier" then
  reset()
  C.state, C.releaseP, C.releaseAt = "release", p, now
  return
 end
 if s ~= "off" then cargo(false, false, false) end
 reset()
end
-- After a take-down: hold body 1 on the lay-down point while R or E stays down (at most 3 s), then hand it back.
local function releaseTick(now)
 local p = C.releaseP
 if (held("R", now) or held("E", now)) and now - C.releaseAt < 3000 then
  cargo(true, false, false)
  if p then warp(1, p[1], p[2], p[3], p[4]) end
  return
 end
 cargo(false, false, false)
 log("step=released;seat=2")
 reset()
end
C.stop = stop
local function refuse(why, d)
 capped("refuse_" .. why, fmt("step=refuse;why=%s;dist_cm=%d", why, d and round(d / 10) or -1))
end
-- Every state is bound to the context it began in (a mission, epoch or role change ends it).
local function bind() C.mission, C.epoch, C.role = W.mission, W.epoch, W.role end
local function beginLoad(now)
 bind()
 C.state, C.loadAt = "loading", now
 cargo(false, true, false)
 log(fmt("step=loading;seat=2;gen=%d", C.gen))
end
-- On foot, carrying the downed partner, E held 450 ms within reach of this copy's horse and facing it.
local function offTick(now)
 if not held("E", now) then C.heldSince, C.heldDone = nil, nil; return end
 C.heldSince = C.heldSince or now
 if C.heldDone or now - C.heldSince < HOLD_MS or mounted() then return end
 C.heldDone = true
 if cargo(false, false, false) ~= 1 then return end
 local other = rec(3 - W.role)
 if not (fresh(other, now) and other.down == true and int(other.generation, 1, 4294967295)) then return refuse("awake") end
 local h = horse()
 if not h then return refuse("horse_lost") end
 if h.occupied == true then return refuse("busy") end
 local x, _, z = mine()
 if not x then return refuse("position") end
 local dx, dz = h.x - x, h.z - z
 local d = sqrt(dx * dx + dz * dz)
 if d > REACH_MM then return refuse("far", d) end
 local ry = W.get(vars, "playerRotY")
 local a = ((num(ry) and ry or 0) + 180) % 360 - 180
 a = a * pi / 180
 if d > 300 and sin(a) * dx + cos(a) * dz < 0 then return refuse("facing", d) end
 if npcCargo() then return refuse("npc_cargo", d) end
 C.gen, C.h = other.generation, h
 if W.host then return beginLoad(now) end
 bind()
 C.state, C.claimAt = "claim", now
 send(CLAIM, 0, C.gen, h.x, h.y, h.z, h.yaw)
 log(fmt("step=claim;seat=2;gen=%d;dist_cm=%d", C.gen, round(d / 10)))
end
-- The stock put-down plays; once the attachment is clear, hold body 1 and tell the victim's copy where the horse is.
local function loadTick(now)
 local role = cargo(false, true, false)
 local p = C.physical
 if role == 0 and p and not p.carrying and not p.carried then
  local h = horse()
  if not h then return stop(now, "horse_lost", false) end
  cargo(true, false, false)
  C.state, C.h, C.hgen, C.park, C.keepAt = "carrier", h, h.gen, h, now + KEEP_MS
  send(LOADED, 0, C.gen, h.x, h.y, h.z, h.yaw)
  log(fmt("step=loaded;seat=2;gen=%d;role=carrier;hx=%d;hz=%d", C.gen, round(h.x / 10), round(h.z / 10)))
  pin(1, h)
 elseif now - C.loadAt > LOAD_MS then stop(now, "load_timeout", false) end
end
-- Where body 1 goes on this copy (mm, rad) and how it was found.
local function bodyPoint(h, rides)
 local x, y, z, a = cargoPoint(h)
 local px, py, pz = mine()
 if rides and px then
  return px - RIDE_BACK_MM * sin(a), py + RIDE_RISE_MM, pz - RIDE_BACK_MM * cos(a), a, "rider"
 end
 if px and sqrt((h.x - px) ^ 2 + (h.z - pz) ^ 2) <= NEAR_MM then
  if y > py + GROUND_HI_MM or y < py + GROUND_LO_MM then
   C.dyFix = py + GROUND_MM - h.y
   return x, py + GROUND_MM, z, a, "ground"
  end
  C.dyFix = nil
 elseif C.dyFix then
  return x, h.y + C.dyFix, z, a, "ground_kept"
 end
 return x, y, z, a, "census"
end
local function finishTake(now, h, x, y, z, keep)
 local cx, _, cz, a = cargoPoint(h)
 local side = (cos(a) * (x - cx) - sin(a) * (z - cz)) >= 0 and 1 or -1
 log(fmt("step=take_down;key=%s;side=%d", C.takeKey or "?", side))
 return stop(now, "taken_down", true, false, beside(h, side, y), keep)
end
local function carrierTick(now)
 cargo(true, false, false)
 local other = rec(3 - W.role)
 if not fresh(other, now, LOST_MS) then return stop(now, "stream_lost", true) end
 if other.generation ~= C.gen then return stop(now, "victim_gone", true) end
 if other.down ~= true then return stop(now, "awake", true) end
 local h = horse()
 if not h or (C.hgen and h.gen ~= C.hgen) then
  C.lostAt = C.lostAt or now
  if not h and now - C.lostAt < HORSE_MS then return end
  return stop(now, "horse_lost", true)
 end
 C.lostAt = nil
 local rides = mounted()
 if rides then C.park = nil
 elseif h.pending == 1 then C.h = h; return stop(now, "call_away", true)
 elseif not C.park then C.park = h
 elseif sqrt((h.x - C.park.x) ^ 2 + (h.z - C.park.z) ^ 2) > CALL_MM then C.h = h; return stop(now, "call_away", true) end
 C.h = h
 local bx, by, bz, ba, how = bodyPoint(h, rides)
 warp(1, bx, by, bz, ba)
 if now >= (C.statsAt or 0) then
  C.statsAt = now + STATS_MS
  local _, py = mine()
  capped("pin", fmt("step=pin;how=%s;mounted=%d;hy_cm=%d;py_cm=%d;by_cm=%d", how, rides and 1 or 0, round(h.y / 10),
   py and round(py / 10) or -1, round(by / 10)))
 end
 if now >= C.keepAt then C.keepAt = now + KEEP_MS; send(KEEP, 0, C.gen, h.x, h.y, h.z, h.yaw) end
 if rides then C.takeSince, C.takeKey = nil, nil; return end
 -- Take-down: R or E held 1 s on foot within reach arms it; the body comes off when the key is up (an R still down
 -- over a body on the ground would start a stock pickup: run 7359e4d2), or after 4 s (then held until the key is up).
 local x, y, z = mine()
 local cx, _, cz = cargoPoint(h)
 local near = x and sqrt((cx - x) ^ 2 + (cz - z) ^ 2) <= TAKE_MM
 local key = x and (held("R", now) and "r" or held("E", now) and "e") or nil
 if key and (near or C.takeKey) then
  C.takeSince = C.takeSince or now
  if not C.takeKey and now - C.takeSince >= TAKE_MS then C.takeKey = key; log(fmt("step=take_armed;key=%s", key)) end
  if C.takeKey and now - C.takeSince >= CAP_MS then return finishTake(now, h, x, y, z, true) end
  return
 end
 C.takeSince = nil
 if C.takeKey then return finishTake(now, h, x, y, z, false) end
end
local function cargoTick(now)
 cargo(false, false, true)
 if now - C.keepAt > LOST_MS then return stop(now, "stream_lost", true) end
 local me = rec(W.role)
 if not me or me.generation ~= C.gen then return stop(now, "victim_gone", false) end
 if me.down ~= true then return stop(now, "awake", true) end
 local p = C.physical
 if p and p.localCarried == true and now - C.putAt < LOAD_MS then return end
 local h = horse()
 if not h then
  C.lostAt = C.lostAt or now
  if now - C.lostAt > HORSE_MS then return stop(now, "horse_lost", true) end
  return
 end
 C.lostAt = nil
 -- Unridden, this copy's horse stands on the carrier's pose (M1 places it while the carrier rides).
 local k, S = C.keepPose, W.horseShare
 local tracked = type(S) == "table" and S.track ~= nil
 if k and not tracked and not mounted() and now >= (C.placeAt or 0) and sqrt((h.x - k.x) ^ 2 + (h.z - k.z) ^ 2) > DRIFT_MM then
  C.placeAt = now + PLACE_MS
  local r = place(k)
  local ok = r and r.ready == true
  capped("horse_placed", fmt("step=horse_placed;ok=%d;reason=%s;x=%d;z=%d", ok and 1 or 0, ok and "ok" or tostring(r and r.reason),
   round(k.x / 10), round(k.z / 10)))
  if ok then h = {x = k.x, y = k.y, z = k.z, yaw = k.yaw, gen = h.gen} end
 end
 C.h = h
 local _, wy = cargoPoint(h)
 pin(0, h)
 if now >= (C.statsAt or 0) then
  C.statsAt = now + STATS_MS
  local got = type(me.pos) == "table" and int(me.pos[2], -1000000000, 1000000000) and round(me.pos[2] / 10) or -1
  capped("victim_pin", fmt("step=victim_pin;want_cm=%d;got_cm=%d", round(wy / 10), got))
 end
end
local function onClaim(now, gen, pose)
 if not W.host then return end
 local me, D = rec(W.role), W.downed
 local why = C.state ~= "off" and "busy" or not (me and me.down == true) and "awake" or me.generation ~= gen and "generation" or
  (D.target == W.role and (D.progress or 0) > 0) and "mid_revive" or nil
 if why then
  send(DENY, CODE[why], gen, 0, 0, 0, 0)
  log(fmt("step=deny;seat=2;why=%s;gen=%d", why, gen))
  return
 end
 send(GRANT, 0, gen, pose.x, pose.y, pose.z, pose.yaw)
 log(fmt("step=grant;seat=2;gen=%d", gen))
end
local function onLoaded(now, gen, pose)
 if C.state ~= "off" then return end
 local me = rec(W.role)
 local why = not (me and me.down == true) and "awake" or me.generation ~= gen and "generation" or nil
 if why then
  send(UNLOADED, CODE[why], gen, 0, 0, 0, 0)
  log(fmt("step=refuse;why=%s;state=loaded;gen=%d", why, gen))
  return
 end
 bind()
 C.state, C.gen, C.keepAt, C.keepPose, C.putAt = "cargo", gen, now, pose, now
 log(fmt("step=cargo;seat=2;gen=%d;hx=%d;hz=%d", gen, round(pose.x / 10), round(pose.z / 10)))
end
W.handlers[OP] = function(v)
 if type(v) ~= "table" or #v < 10 or not C.on() then return end
 local seat, role, action, cause, gen = v[2], v[3], v[4], v[5], v[6]
 if seat ~= SEAT or role ~= 3 - W.role or not int(action, 1, 6) or not int(cause, 0, 255) or not int(gen, 0, 4294967295) or
  not int(v[10], -31416, 31416) then return end
 for i = 7, 9 do if not int(v[i], -1000000000, 1000000000) then return end end
 local now = W.now and W.now() or 0
 local pose = {x = v[7], y = v[8], z = v[9], yaw = v[10]}
 if action == CLAIM then return onClaim(now, gen, pose) end
 if action == GRANT then
  if C.state == "claim" and gen == C.gen then beginLoad(now) end
 elseif action == DENY then
  if C.state == "claim" and gen == C.gen then stop(now, CAUSE[cause] or "deny", false, true) end
 elseif action == LOADED then onLoaded(now, gen, pose)
 elseif action == KEEP then
  if C.state == "cargo" and gen == C.gen then C.keepAt, C.keepPose = now, pose end
 elseif action == UNLOADED and (C.state == "cargo" or C.state == "carrier") and gen == C.gen then
  local p = (v[7] ~= 0 or v[9] ~= 0) and {v[7], v[8], v[9], v[10] / 1000} or nil
  stop(now, CAUSE[cause] or "remote_end", true, true, p)
 end
end
function C.tick(now)
 if not C.on() then
  if C.state ~= "off" then stop(now, "stream_lost", true, W.paired ~= true) end
  C.heldSince, C.heldDone = nil, nil
  return
 end
 if C.mission ~= W.mission or C.epoch ~= W.epoch or C.role ~= W.role then
  if C.state ~= "off" then stop(now, "context", true) end
  C.mission, C.epoch, C.role = W.mission, W.epoch, W.role
 end
 local s = C.state
 if s == "off" then return offTick(now) end
 if s == "claim" then
  if now - C.claimAt > CLAIM_MS then stop(now, "claim_timeout", false, true) end
  return
 end
 if s == "loading" then return loadTick(now) end
 if s == "carrier" then return carrierTick(now) end
 if s == "cargo" then return cargoTick(now) end
 if s == "release" then return releaseTick(now) end
end
function C.setTesting(on)
 if type(on) ~= "boolean" then return false end
 if not on and C.state ~= "off" then stop(W.now and W.now() or 0, "context", true) end
 C.testing = on
 return true
end
function C.status()
 return fmt("cargo;state=%s;key=%d;testing=%d;gen=%d;role=%d;busy=%d", C.state, C.keyOn() and 1 or 0, C.testing and 1 or 0, C.gen or 0,
  W.role or 0, C.busy() and 1 or 0)
end
local tick = W.tick
function W.tick(...)
 if tick then tick(...) end
 local now = W.now and W.now() or 0
 local ok, err = pcall(C.tick, now)
 if not ok then log("step=error;" .. tostring(err)); pcall(stop, now, "context", true) end
end
local function hook() pcall(stop, W.now and W.now() or 0, "context", true, true); C.mission, C.epoch, C.role = nil, nil, nil end
if type(W.startHooks) == "table" then W.startHooks[#W.startHooks + 1] = hook end
if type(W.unpairedHooks) == "table" then W.unpairedHooks[#W.unpairedHooks + 1] = hook end
end
end
