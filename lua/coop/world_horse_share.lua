-- Horse plan M1, the shared D-Horse (lab key world_horse_share; docs/mounts.md "Shared D-Horse"). The session's
-- D-Horse is index 0 (6656) on both copies. The rider beats the owner: the copy whose player rides it streams the
-- census pose (MGS_COOP_HORSE_DRIVE_V1 read) in RIDE_FAST (op 44, kind 3, id 6656, 20 Hz; the vehicle track code of
-- world_ride_pose.lua) and one final reliable RIDE_BODY (op 42, kind 1, rider 65534) at the dismount. The other copy
-- renders the track 100 ms behind and places its 6656 every frame (place, anchored under body 1 once native shows it
-- seated), so the partner rides in the saddle on this screen; the final pose leaves the horse where it was left, then
-- nothing moves it and the first E mounts it. Within 4 m of a horse the partner rides, an ACTION-only pad mask
-- (MgsCoopHorseHold, probe P5) stops a second native driver. A census generation change, a stale track or a context
-- change ends it; Travel / Bring partner here and the guest's checkpoint wait while anyone rides it. Both PCs must run
-- the key (a copy without it drops 6656 from RIDE_FAST and refuses RIDE_BODY not_mirror, as before).
do
local W = e.MgsCoopWorld
if type(W) == "table" and type(W.handlers) == "table" and type(W.OP) == "table" and type(W.ridePose) == "table" then
local R = W.ridePose
local S = {sent = 0, seq = 0, n = {}, refused = {}}
W.horseShare = S
local FAST, BODY = W.OP.RIDE_FAST or 44, W.OP.RIDE_BODY or 42
local HORSE, FINAL, MASK = 6656, 65534, "MgsCoopHorseHold"
local SEND_MS, CLOSED_MS, STATS_MS, HOLD_MM, FREE_MM, LEAP_MM = 50, 700, 2000, 4000, 4500, 300
local fmt, floor, sqrt = string.format, math.floor, math.sqrt
S.HOLD_MM, S.FREE_MM, S.SEND_MS = HOLD_MM, FREE_MM, SEND_MS
local function log(s) if type(W.log) == "function" then W.log("event=horse_share;" .. s) end end
local function now() return W.now and W.now() or 0 end
local function int(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
local function round(v) return floor(v + 0.5) end
local function receipt(verb)
 if type(W.receipt) ~= "function" then return nil end
 local ok, r = pcall(W.receipt, "return 'MGS_COOP_HORSE_DRIVE_V1' -- " .. verb)
 if ok and type(r) == "table" then return r end
end
-- The lab key (fixed for the process): asked until native answers.
function S.keyOn()
 if S.key == nil then
  local r = receipt("cfg")
  if r and (r.on == 1 or r.on == 0) then S.key = r.on == 1 end
 end
 return S.key == true
end
function S.on()
 return W.active ~= false and W.paired == true and (W.eff or 0) >= 3 and (W.mission == 30010 or W.mission == 30020) and
  int(W.epoch, 1, 65535) and S.keyOn()
end
function S.localRides()
 if type(PlayerInfo) ~= "table" or type(PlayerInfo.OrCheckStatus) ~= "function" or type(PlayerStatus) ~= "table" then return false end
 local ok, yes = pcall(PlayerInfo.OrCheckStatus, {PlayerStatus.ON_HORSE})
 return ok and yes == true
end
-- The partner rides the shared horse (a live track) / anyone rides it (Travel, Bring, the guest's checkpoint).
function S.partnerRides() return S.track ~= nil end
function S.busy()
 local C = W.horseCargo
 return S.track ~= nil or S.sendId ~= nil or (type(C) == "table" and type(C.busy) == "function" and C.busy() == true)
end
local function capped(kind, line)
 local c = (S.n[kind] or 0) + 1
 S.n[kind] = c
 if c <= 20 then log(line) end
end
local function setHold(on)
 if (S.hold == true) == on then return true end
 local f = W.get(Player, on and "SetPadMask" or "ResetPadMask")
 if type(f) ~= "function" then return false end
 local bit = W.get(PlayerPad, "ACTION")
 local ok, r
 if on then ok, r = pcall(f, {settingName = MASK, except = false, buttons = type(bit) == "number" and bit or 0x20, sticks = 0, triggers = 0})
 else ok, r = pcall(f, {settingName = MASK}) end
 if ok and r ~= false then S.hold = on or nil; return true end
 return false
end
local function release(why)
 if S.hold and setHold(false) and type(W.log) == "function" then W.log("event=horse_hold;step=released;why=" .. why) end
end
local function statsLine(t, why)
 local s = S.stats
 if not s or s.placed == 0 and s.refused == 0 then return end
 local e = s.errs
 table.sort(e)
 local p50 = #e > 0 and e[math.max(1, floor(#e * 0.5 + 0.5))] or -1
 if type(W.log) == "function" then
  W.log(fmt("horse_place=stats;placed=%d;err_cm=%d;err_max_cm=%d;up_cm=%d;leap=%d;anchored=%d;gap_cm=%d;refused=%d;why=%s",
   s.placed, round(p50 / 10), round((e[#e] or -10) / 10), s.n > 0 and round(s.up / s.n / 10) or 0, s.leap, s.anchored,
   round(s.gap / 10), s.refused, why))
 end
 S.stats = {placed = 0, refused = 0, leap = 0, anchored = 0, gap = 0, errs = {}, up = 0, n = 0, at = t}
end
-- The partner's ride ended (why: final, stream_lost, generation, two_drivers, context).
local function endTrack(t, why)
 if not S.track then return end
 statsLine(t, why)
 S.track, S.closedAt, S.last, S.lastB, S.gen = nil, t, nil, nil, nil
 release(why)
 log(fmt("step=end;why=%s", why))
end
function S.reset(why)
 local t = now()
 endTrack(t, why or "reset")
 release(why or "reset")
 S.sendId, S.lastPose, S.sendGen, S.closedAt, S.twoDrivers = nil, nil, nil, nil, nil
end
local function read()
 local r = receipt(fmt("read %d %d", W.mission, W.epoch))
 if r and r.ready == true and int(r.x, -1000000000, 1000000000) and int(r.y, -1000000000, 1000000000) and
  int(r.z, -1000000000, 1000000000) and int(r.yaw, -31416, 31416) then return r end
end
local function place(x, y, z, yaw, anchor)
 return receipt(fmt("place %d %d %d %d %d %d %d", W.mission, W.epoch, round(x), round(y), round(z), round(yaw), anchor and 1 or 0))
end
-- Rider side: 20 Hz while this copy's player rides record 0, one final reliable pose after the dismount.
function S.send(t)
 local rides = S.localRides()
 local h = (rides or S.sendId) and read() or nil
 if rides and h and h.occupied == true then
  if S.sendGen and h.gen ~= S.sendGen then log("step=end;why=generation;side=rider") end
  if not S.sendId or S.sendGen ~= h.gen then S.sendId, S.sendGen, S.next = HORSE, h.gen, 0; log(fmt("step=ride;gen=%d", h.gen or 0)) end
  S.lastPose = h
  if t >= (S.next or 0) then
   S.next = t + SEND_MS
   if W.tx(FAST, W.tick32(t), 3, HORSE, h.x, h.y, h.z, h.yaw) then S.sent = S.sent + 1 end
  end
 elseif S.sendId and not rides then
  local p = h or S.lastPose
  if p then
   S.seq = S.seq % 65535 + 1
   local ok = W.tx(BODY, S.seq, 1, HORSE, p.x, p.y, p.z, p.yaw, 0, FINAL)
   log(fmt("step=final_sent;ok=%d;sent=%d;x=%d;z=%d", ok and 1 or 0, S.sent, round(p.x / 10), round(p.z / 10)))
  end
  S.sendId, S.lastPose, S.sendGen = nil, nil, nil
 end
end
-- Viewer side: place the partner's ride every frame; the ride hold near it.
function S.apply(t)
 local tr = S.track
 if not tr then return end
 local x, y, z, yaw, state = R.trackPose(tr, t)
 if not x then return endTrack(t, "stream_lost") end
 if S.localRides() then
  -- Two drivers (a same-moment E race): neither copy moves the other's horse; native keeps the hide.
  if not S.twoDrivers then S.twoDrivers = true; log("step=two_drivers") end
  return
 end
 S.twoDrivers = nil
 local r = place(x, y, z, yaw, true)
 -- M2: a downed partner loaded on this horse follows the pose just written (world_horse_cargo.lua).
 local HC = W.horseCargo
 if r and r.ready == true and type(HC) == "table" and type(HC.placed) == "function" then pcall(HC.placed, r.x, r.y, r.z, yaw) end
 local s = S.stats
 if not r or r.ready ~= true then
  s.refused = s.refused + 1
  local why = r and tostring(r.reason) or "receipt"
  if S.refused[why] ~= tr then S.refused[why] = tr; capped("refuse", "step=refuse;reason=" .. why) end
 else
  if S.gen and r.gen ~= S.gen then return endTrack(t, "generation") end
  S.gen = r.gen
  s.placed = s.placed + 1
  if r.anchored == 1 then s.anchored = s.anchored + 1 end
  local gap = sqrt((r.x - x) ^ 2 + (r.z - z) ^ 2)
  if gap > s.gap then s.gap = gap end
  -- Readback before this write against the last write: the steady census offset (err_cm in xz, up_cm); a leap is that
  -- offset rising by more than 0.3 m from one frame to the next (run 05232637: the census sits 0.3-0.45 m over each write).
  local L, B = S.last, S.lastB
  local off = L and r.by - L[2] or nil
  if off then
   s.errs[#s.errs + 1] = sqrt((r.bx - L[1]) ^ 2 + (r.bz - L[3]) ^ 2)
   s.up, s.n = s.up + off, s.n + 1
   if B and off - B > LEAP_MM then s.leap = s.leap + 1 end
  end
  S.lastB = off
  if not L then log(fmt("step=placed;state=%s;anchored=%d;gen=%d", tostring(state), r.anchored or 0, r.gen or 0)) end
  S.last = {r.x, r.y, r.z}
 end
 if t - (s.at or t) >= STATS_MS then statsLine(t, "tick") end
 -- The ride hold (P5): within 4 m of the ridden horse the local E cannot mount it a second time.
 local px, pz = W.get(vars, "playerPosX"), W.get(vars, "playerPosZ")
 local L = S.last
 if L and type(px) == "number" and type(pz) == "number" then
  local d = sqrt((px * 1000 - L[1]) ^ 2 + (pz * 1000 - L[3]) ^ 2)
  if not S.hold and d < HOLD_MM then
   if setHold(true) and type(W.log) == "function" then W.log(fmt("event=horse_hold;step=action_masked;dist_cm=%d", round(d / 10))) end
  elseif S.hold and d > FREE_MM then release("far") end
 end
end
local fastBase, bodyBase = W.handlers[FAST], W.handlers[BODY]
W.handlers[FAST] = function(v)
 if type(v) == "table" and #v == 7 and v[2] == 3 and v[3] == HORSE then
  if not S.on() or not int(v[1], 0, 4294967295) then return end
  local t = now()
  if S.closedAt and t - S.closedAt < CLOSED_MS then return end
  local tr = S.track
  if not tr then
   tr = {samples = {}}
   S.track, S.stats, S.last, S.lastB, S.gen = tr, {placed = 0, refused = 0, leap = 0, anchored = 0, gap = 0, errs = {}, up = 0, n = 0, at = t},
    nil, nil, nil
   S.refused = {}
   log("step=track;from=partner")
  end
  R.trackPush(tr, t, v[1], v[4], v[5], v[6], v[7])
  return
 end
 if fastBase then return fastBase(v) end
end
W.handlers[BODY] = function(v)
 if type(v) == "table" and #v == 9 and v[2] == 1 and v[3] == HORSE and v[9] == FINAL and S.on() then
  local t = now()
  if S.localRides() then log("step=final_ignored;why=local_rides"); return end
  local r = place(v[4], v[5], v[6], v[7], false)
  local ok = r and r.ready == true
  log(fmt("step=final;placed=%d;reason=%s;x=%d;z=%d", ok and 1 or 0, ok and "ok" or tostring(r and r.reason), round(v[4] / 10), round(v[6] / 10)))
  endTrack(t, "final")
  S.closedAt = t
  return
 end
 if bodyBase then return bodyBase(v) end
end
-- Travel and Bring partner here wait while anyone rides the shared horse (reason on_foot, 3).
local T = W.travel
if type(T) == "table" and type(T.blocked) == "function" then
 local blocked = T.blocked
 T.blocked = function(...)
  local b = blocked(...)
  if b then return b end
  if S.busy() then return 3 end
 end
end
local tick = W.tick
function W.tick(...)
 if tick then tick(...) end
 local t = now()
 local ok, err = pcall(function()
  if S.mission ~= W.mission or S.epoch ~= W.epoch then
   if S.mission ~= nil then S.reset("context") end
   S.mission, S.epoch = W.mission, W.epoch
  end
  if not S.on() then
   if S.track or S.sendId or S.hold then S.reset("off") end
   return
  end
  -- M1 seeds (world_companion.lua, both keys): the host's horse is the guest's shown companion.
  local C = W.companion
  if type(C) == "table" and type(C.HOLD) == "table" and not C.HOLD.HORSE then C.HOLD.HORSE = true end
  S.send(t)
  S.apply(t)
 end)
 if not ok then log("step=error;" .. tostring(err)); pcall(S.reset, "error") end
end
if type(W.startHooks) == "table" then W.startHooks[#W.startHooks + 1] = function() S.reset("start") end end
if type(W.unpairedHooks) == "table" then W.unpairedHooks[#W.unpairedHooks + 1] = function() S.reset("unpaired") end end
end
end
