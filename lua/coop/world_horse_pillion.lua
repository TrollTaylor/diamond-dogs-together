-- Horse plan M3, ride behind your partner (lab key world_horse_pillion with world_horse_share; docs/mounts.md "Ride behind
-- your partner"). On foot within 2.5 m of the shared D-Horse (index 0, 6656) while the partner rides it on this copy
-- (world_horse_share's track), an E press claims the rear seat: HORSE_SEAT (0x50) seat 1. The ride hold masks ACTION
-- there (probe P5), so E is read from the local pad before the masks (MGS_COOP_HORSE_DRIVE_V1 pad). The host decides
-- under its press lock: a guest's claim is granted while the host rides the shared horse; the host's own press beside the
-- guest's ride commits for itself (GRANT with cause passenger) and blocks a guest claim for 3 s. The passenger's copy
-- masks the pad (movement, sprint, stance, ACTION, CQC, roll; the camera stays free), squats, and puts body 0 every frame
-- on the rear seat of its own placed horse (world_horse_share calls P.placed right after each placement; since M3c
-- BODY0's hold0 + C0 + write watch + pin, held again at its last target in a tick without a placement). The driver's copy leases MGS_COOP_HORSE_DRIVE_V1 seat1 every tick: the partner driver draws body
-- 1 at the rear seat of this copy's ridden horse, seated by the DM overlay. E again (pressed after a release) hops off;
-- the driver getting off, the ride's end on the passenger's copy, a census generation change, no KEEP for 3.5 s (1 Hz both
-- ways), a downed passenger, unpairing or a context change lay the passenger down where the horse's root was 2-4 m back
-- along the path ridden since the seating (else where they stood at the claim, else beside the horse's right).
do
local W = e.MgsCoopWorld
if type(W) == "table" and type(W.handlers) == "table" and type(W.OP) == "table" and type(W.horseShare) == "table" then
local S = W.horseShare
local P = {state = "off", seq = 0, token = 0, n = {}}
W.horsePillion = P
local OP, SEAT, HORSE = W.OP.HORSE_SEAT or 80, 1, 6656
W.OP.HORSE_SEAT = OP
local CLAIM, GRANT, DENY, UNSEATED, KEEP = 1, 2, 3, 5, 6
local CAUSE = {[0] = "ok", "passenger", "driver_off", "horse_lost", "stream_lost", "context", "busy", "not_riding",
 "passenger_downed", "claim_timeout", "track_end", "locked"}
local CODE = {}
for k, v in pairs(CAUSE) do CODE[v] = k end
local CLAIM_MM, CLAIM_MS, KEEP_MS, LOST_MS, LOCK_MS, SIDE_MM, STATS_MS, LAND_MM, HOP_MS = 2500, 2000, 1000, 3500, 3000, 1300, 1000, 300, 300
local MASK, MASK_BUTTONS, FREE_E_MM, DRIVE_MS = "MgsCoopPillion", 0xFFFFFFA7, 6000, 2000
-- M3c: the driver's stall watch (ride keys = the move stick's magnitude over MAG_ON / 1000 for STALL_MS with under MOVE_MM of
-- ridden path) and the same-moment tie (the partner's RIDE_FAST while this copy's player rides, within TIE_MS).
local STALL_MS, MOVE_MM, MAG_ON, TIE_MS, TRACK_GRACE_MS = 2000, 500, 100, 500, 2000
-- Body 0 on the passenger's copy: back mm behind the placed census root along its heading, rise mm over it (the rider
-- sits 0.10 m forward and 0.49 m up of the root, probe P2; the rear seat is 0.45 m behind the rider), squatting; mode 2
-- hold0 with the A0 reset (round 2: lag mostly 6-9 cm, peaks 74-80 cm, crouched), 0 hold0 without a reset, 4 the
-- Lua Warp, 5 both, 3 hold0 with the C0 reset alone, 6 with C0 + the write watch + pin (BODY0 round). Body 1 on the driver's copy: dback mm behind and dup mm over the driver's own root (native RearSeat).
-- Run ba6b4bb5: at 0.6 m body 0 stood on the ground under its target (dy 1-1.8 m), and body 1 at the driver's root height
-- on the ground beside the horse. Runs bdd49a57 / a295d660: 1.2 m / 0.7 m (clear of the horse) changed neither the
-- host-driven horse standing still nor the passenger's copy's horse falling, and floated: 0.8 m / 0.3 m. seat 1: the
-- driver's copy seats body 1 with the DM overlay; 0 (lab lever) not, a test of the stall (the passenger's KEEP carries it).
-- M3c: mode 6 by default (BODY0 run 7c0b9e71: seat_pin lag 0 / dy 0, the passenger squatting behind the driver on their
-- own screen at walk / trot / gallop, no horse knocked down; mode 2 dropped body 0 1-2.3 m onto the horse every run).
local V = {back = 350, rise = 800, mode = 6, dback = 450, dup = 300, seat = 1}
local RESET = {[0] = 0, 1, 1, 2, 1, 1, 14}
P.V = V
local floor, sin, cos, sqrt, fmt = math.floor, math.sin, math.cos, math.sqrt, string.format
local function int(n, lo, hi) return type(n) == "number" and n == floor(n) and n >= lo and n <= hi end
local function num(n) return type(n) == "number" and n == n and n > -1e12 and n < 1e12 end
local function round(v) return floor(v + 0.5) end
local function log(s) if type(W.log) == "function" then pcall(W.log, "event=horse_pillion;" .. s) end end
local function capped(kind, s, cap)
 local c = (P.n[kind] or 0) + 1
 P.n[kind] = c
 if c <= (cap or 20) then log(s) end
end
local function receipt(verb)
 if type(W.receipt) ~= "function" then return nil end
 local ok, r = pcall(W.receipt, "return 'MGS_COOP_HORSE_DRIVE_V1' -- " .. verb)
 if ok and type(r) == "table" then return r end
end
-- The lab key (fixed for the process), asked until native answers.
function P.keyOn()
 if P.key == nil then
  local r = receipt("cfg")
  if r and (r.on == 0 or r.on == 1) then P.key, P.shot = r.on == 1 and r.pillion == 1, r.shot == 1 end
 end
 return P.key == true
end
function P.on() return type(S.on) == "function" and S.on() and int(W.role, 1, 2) and P.keyOn() end
function P.busy() return P.state ~= "off" end
function P.setPose(back, rise, mode, dback, dup, seat)
 if not (int(back, -1000, 1500) and int(rise, -500, 2000) and (mode == 0 or mode == 2 or mode == 3 or mode == 4 or mode == 5 or mode == 6) and int(dback, 0, 1500) and
  int(dup, -500, 1500) and (seat == nil or seat == 0 or seat == 1)) then return false end
 V.back, V.rise, V.mode, V.dback, V.dup, V.seat = back, rise, mode, dback, dup, seat or V.seat
 return true
end
local function mine()
 local x, y, z = W.get(vars, "playerPosX"), W.get(vars, "playerPosY"), W.get(vars, "playerPosZ")
 if num(x) and num(y) and num(z) then return x * 1000, y * 1000, z * 1000 end
end
local function selfDown()
 local d = W.downed
 local r = type(d) == "table" and type(d.records) == "table" and d.records[W.role] or nil
 return type(r) == "table" and r.down == true
end
-- The local E before the pad masks (pad verb); nil when unreadable.
local function padE()
 local r = receipt(fmt("pad %d %d", W.mission, W.epoch))
 if r and r.ready == true then
  local e = r.e == 1
  if e ~= P.lastE then P.lastE = e; capped("pad", fmt("step=pad;e=%d;masked=%d;state=%s", e and 1 or 0, r.masked == 1 and 1 or 0, P.state), 40) end
  P.pad = r
  return e
 end
 local why = r and tostring(r.reason) or "receipt"
 if P.padWhy ~= why then P.padWhy = why; capped("pad_refused", "step=pad_refused;reason=" .. why) end
end
local function send(action, cause, x, y, z, yaw)
 if type(W.tx) ~= "function" then return false end
 P.seq = P.seq % 65535 + 1
 local ym = round(yaw or 0)
 if ym > 31416 or ym < -31416 then ym = 0 end
 return W.tx(OP, P.seq, SEAT, W.role, action, cause or 0, P.token or 0, round(x or 0), round(y or 0), round(z or 0), ym)
end
local function setMask(on)
 local f = W.get(Player, on and "SetPadMask" or "ResetPadMask")
 if type(f) ~= "function" then return false end
 local ok, r
 if on then
  local stick = W.get(PlayerPad, "STICK_L")
  ok, r = pcall(f, {settingName = MASK, except = false, buttons = MASK_BUTTONS, sticks = type(stick) == "number" and stick or 1, triggers = 0})
 else ok, r = pcall(f, {settingName = MASK}) end
 if ok and r ~= false then P.masked = on or nil; return true end
 return false
end
local function stance(name)
 local f, s = W.get(Player, "RequestToSetTargetStance"), W.get(PlayerStance, name)
 if type(f) == "function" and s ~= nil then pcall(f, s) end
end
local function warp(x, y, z, a)
 if type(GameObject) ~= "table" or type(GameObject.SendCommand) ~= "function" then return false end
 return pcall(GameObject.SendCommand, {type = "TppPlayer2", index = 0}, {id = "Warp", pos = {x / 1000, y / 1000, z / 1000}, rotY = a})
end
-- The rear seat of a horse pose h (mm, yaw mrad) for body 0.
local function seatPoint(h)
 local a = h.yaw / 1000
 return h.x - V.back * sin(a), h.y + V.rise, h.z - V.back * cos(a), a
end
P.seatPoint = seatPoint
-- The lay-off point. The skeptic's frames (runs 51f6a2cc / 4f52fa2a): the side point (-x at yaw 0) is the horse's RIGHT,
-- and in 51f6a2cc (a) it was inside the rock wall the horse rode along. So: the newest root of the path ridden since the
-- seating (P.trail, a point per TRAIL_MM) 2-4 m from the root now, ground the horse stood on. Without one (a horse that has
-- not moved 2 m; play runs 16f3f282 / 023f9167: the side point put the passenger inside the rock on the driver's dismount
-- right after a seating) the spot the passenger stood on at the claim (P.foot) within FOOT_MM of the root; the side point
-- only past that (a jump of the placed horse).
local TRAIL_MM, TRAIL_N, BACK_MM, BACK_MAX_MM, FOOT_MM = 250, 24, 2000, 4000, 5000
local function layOff(h)
 local a, T, foot = h.yaw / 1000, P.trail or {}, (P.dy or -900) + LAND_MM
 for i = #T, 1, -1 do
  local p = T[i]
  local d2 = (p[1] - h.x) ^ 2 + (p[3] - h.z) ^ 2
  if d2 >= BACK_MM * BACK_MM then
   if d2 <= BACK_MAX_MM * BACK_MAX_MM then return p[1], p[2] + foot, p[3], a, "trail" end
   break
  end
 end
 local f = P.foot
 if f and (f[1] - h.x) ^ 2 + (f[3] - h.z) ^ 2 <= FOOT_MM * FOOT_MM then return f[1], f[2] + LAND_MM, f[3], a, "foot" end
 return h.x - SIDE_MM * cos(a), h.y + foot, h.z + SIDE_MM * sin(a), a, "side"
end
P.layOff = layOff
local function trail(x, y, z)
 local T = P.trail
 if not T then return end
 local l = T[#T]
 if l and (x - l[1]) ^ 2 + (z - l[3]) ^ 2 < TRAIL_MM * TRAIL_MM then return end
 T[#T + 1] = {x, y, z}
 if #T > TRAIL_N then table.remove(T, 1) end
end
local function receiptRead() return receipt(fmt("read %d %d", W.mission, W.epoch)) end
local function n(v) return type(v) == "number" and v == v and floor(v) or -1 end
local function reset()
 P.state, P.claimAt, P.keepAt, P.rxAt, P.armed, P.prevE, P.h, P.dy, P.want, P.lag, P.statsAt, P.seatN = "off", nil, nil, nil, nil, nil, nil,
  nil, nil, nil, nil, nil
 P.mission, P.epoch, P.role, P.gen, P.leaseWhy, P.eSince, P.drivePath, P.driveAt, P.remoteSeat = nil, nil, nil, nil, nil, nil, nil, nil, nil
 P.keyAt, P.keyPath, P.keyFirst, P.moved, P.wantA, P.placedTick, P.trail, P.foot, P.boxed, P.noTrack = nil, nil, nil, nil, nil, nil, nil, nil, nil, nil
end
-- End this copy's part: why a CAUSE name; remote: the partner ended it (nothing sent back).
local function stop(why, remote)
 local s = P.state
 if s == "seated" then
  local h, x, y, z, a, at = P.h, nil, nil, nil, nil, "none"
  if V.mode == 6 then receipt(fmt("free0 %d %d", W.mission == 30020 and 30020 or 30010, int(W.epoch, 1, 65535) and W.epoch or 1)) end
  if h then x, y, z, a, at = layOff(h); warp(x, y, z, a) end
  setMask(false)
  stance("STAND")
  if not remote then send(UNSEATED, CODE[why] or 0, x, y, z, a and a * 1000) end
  log(fmt("step=unseated;seat=1;why=%s;side=passenger;placed=%d;x=%d;z=%d;at=%s%s", why, x and 1 or 0, x and round(x / 10) or 0,
   z and round(z / 10) or 0, at, remote and ";from=partner" or ""))
 elseif s == "driver" then
  if not remote then send(UNSEATED, CODE[why] or 0) end
  log(fmt("step=unseated;seat=1;why=%s;side=driver;leases=%d%s", why, P.seatN or 0, remote and ";from=partner" or ""))
 elseif s == "claim" then
  log(fmt("step=refuse;seat=1;why=%s;state=claim", why))
 end
 if P.masked then setMask(false) end
 reset()
end
P.stop = stop
local function bind() P.mission, P.epoch, P.role = W.mission, W.epoch, W.role end
local function seated(now, how)
 bind()
 local L = S.last
 P.state, P.keepAt, P.rxAt, P.armed, P.trail = "seated", now + KEEP_MS, now, false, {}
 if L then P.h = {x = L[1], y = L[2], z = L[3], yaw = P.yaw or 0}; trail(L[1], L[2], L[3]) end
 setMask(true)
 stance("SQUAT")
 log(fmt("step=seated;seat=1;how=%s;token=%d;mode=%d", how, P.token, V.mode))
end
local function driver(now, how)
 bind()
 P.state, P.keepAt, P.rxAt, P.seatN = "driver", now + KEEP_MS, now, 0
 log(fmt("step=driver;seat=1;how=%s;token=%d", how, P.token))
end
-- On foot beside the horse the partner rides here, E (pre-mask) claims the rear seat.
local function offTick(now)
 if not S.track or S.localRides() or selfDown() then P.prevE = nil; return end
 local L, x, y, z = S.last, mine()
 if not (L and x) then return end
 local d = sqrt((x - L[1]) ^ 2 + (z - L[3]) ^ 2)
 -- The skeptic (runs 93b59ae4 / 5687aa1c: E masked and nothing claimed at 2.59 / 2.76 m): while the ride hold masks E (on
 -- within 2.5 m, released past 2.8 m) E claims the seat, so a masked E is never lost.
 if d > (S.hold == true and (S.PILLION_FREE_MM or CLAIM_MM) or CLAIM_MM) then
  P.prevE = nil
  -- Past the shrunken ride hold E reaches the game as usual (a pickup, a revive). M3c: read from the raw pad verb with
  -- its ACTION mask bit (the downed poll is not polled while nobody is downed: no e_free line in seven runs).
  if d > FREE_E_MM then P.prevFree = nil; return end
  local e = padE()
  if e == true and P.prevFree == false then
   local r = P.pad
   capped("e_free", fmt("step=e_free;dist_cm=%d;hold=%d;masked=%d", round(d / 10), S.hold and 1 or 0, r and r.masked == 1 and 1 or 0), 10)
  end
  if e ~= nil then P.prevFree = e end
  return
 end
 local HC = W.horseCargo
 if type(HC) == "table" and type(HC.busy) == "function" and HC.busy() then return end
 local e = padE()
 local edge = e == true and P.prevE == false
 if e ~= nil then P.prevE = e end
 if not edge then return end
 P.token = P.token % 65535 + 1
 P.dy, P.foot = y - L[2], {x, y, z}
 if W.host then
  -- The guest drives: the host's own press commits (GRANT, cause passenger) and holds the press lock.
  P.lockUntil = now + LOCK_MS
  send(GRANT, CODE.passenger, L[1], L[2], L[3], P.yaw or 0)
  log(fmt("step=claim;seat=1;dist_cm=%d;self=host", round(d / 10)))
  return seated(now, "host_press")
 end
 bind()
 P.state, P.claimAt = "claim", now
 send(CLAIM, V.seat == 0 and 1 or 0, L[1], L[2], L[3], P.yaw or 0)
 log(fmt("step=claim;seat=1;dist_cm=%d;token=%d", round(d / 10), P.token))
end
-- Body 0 against its last target before the next move: the worst lag of the stats window.
local function noteLag()
 local t, x, y, z = P.want, mine()
 if not (t and x) then return end
 local L = P.lag or {lag = 0, dy = 0, n = 0}
 P.lag = L
 local d = sqrt((x - t[1]) ^ 2 + (z - t[3]) ^ 2)
 if d > L.lag then L.lag = d end
 if math.abs(t[2] - y) > math.abs(L.dy) then L.dy = t[2] - y end
 L.n = L.n + 1
end
local function pin(x, y, z, a)
 noteLag()
 P.want, P.wantA = {x, y, z}, a
 if V.mode == 4 then P.via = "warp"; return warp(x, y, z, a) end
 local r = receipt(fmt("hold0 %d %d %d %d %d %d %d", W.mission, W.epoch, round(x), round(y), round(z), round(a * 1000), RESET[V.mode] or 1))
 if r and r.ready == true then
  P.via = V.mode == 5 and "both" or "hold0"
  if V.mode == 5 then warp(x, y, z, a) end
  return true
 end
 local why = r and tostring(r.reason) or "receipt"
 if P.holdWhy ~= why then P.holdWhy = why; capped("hold0_refused", "step=hold0_refused;reason=" .. why) end
 P.via = "warp"
 return warp(x, y, z, a)
end
-- world_horse_share calls this right after it placed this copy's horse from the driver's stream (the same frame).
function P.placed(x, y, z, yaw)
 P.yaw = yaw
 if P.state ~= "seated" or not (num(x) and num(y) and num(z) and num(yaw)) then return end
 P.placedTick = true
 P.h = {x = x, y = y, z = z, yaw = yaw}
 trail(x, y, z)
 local sx, sy, sz, a = seatPoint(P.h)
 pin(sx, sy, sz, a)
 -- M4 (world_horse_shot): the placed horse's box (mm, mrad) for the seated-shot hook (this copy's own bullets start past it).
 if P.shot then
  local r = receipt(fmt("pbox %d %d %d %d %d %d", W.mission, W.epoch, round(x), round(y), round(z), round(yaw)))
  if not P.boxed then P.boxed = true; log(fmt("step=shot_box;ok=%d;", r and r.ready == true and 1 or 0)) end
 end
end
local function seatedTick(now)
 -- M4 (play run 28015a91, -WanSim bad): a RIDE_FAST gap ended the track (stream_lost) for 0.9 s while the ride went on;
 -- a stream loss is held TRACK_GRACE_MS (body 0 at its last target), a final or any other end is not.
 if not S.track then
  P.noTrack = P.noTrack or now
  if S.endWhy ~= "stream_lost" or now - P.noTrack > TRACK_GRACE_MS then return stop("track_end") end
  capped("track_wait", "step=track_wait;why=stream_lost", 10)
 else
  P.noTrack = nil
 end
 if selfDown() then return stop("passenger_downed") end
 if now - P.rxAt > LOST_MS then return stop("stream_lost") end
 -- M3c: a tick without a placement (a refused place) holds body 0 at its last target: a lapsed hold dropped the body onto
 -- the horse (BODY0: Unconscious ~0.18 s after a 100 ms pin gap; every M3 run with mode 2's drop).
 if not P.placedTick and P.want and P.wantA then
  capped("hold_keep", "step=hold_keep;why=no_place", 5)
  pin(P.want[1], P.want[2], P.want[3], P.wantA)
 end
 P.placedTick = nil
 if now >= P.keepAt then P.keepAt = now + KEEP_MS; send(KEEP, V.seat == 0 and 1 or 0) end
 -- E held HOP_MS again hops off; it counts only after E was up once (the claiming E held on never ends it).
 local e = padE()
 if e == false then P.armed, P.eSince = true, nil
 elseif e == true and P.armed then
  P.eSince = P.eSince or now
  if now - P.eSince >= HOP_MS then return stop("passenger") end
 end
 if now >= (P.statsAt or 0) then
  P.statsAt = now + STATS_MS
  local L, t = P.lag or {lag = 0, dy = 0, n = 0}, P.want
  local r = receiptRead()
  capped("seat_pin", fmt("step=seat_pin;lag_cm=%d;dy_cm=%d;n=%d;via=%s;y_cm=%d;occ=%d;life=%d;max=%d", round(L.lag / 10), round(L.dy / 10),
   L.n, P.via or "none", t and round(t[2] / 10) or -1, r and r.occupied == true and 1 or 0, n(r and r.life), n(r and r.maxlife)), 90)
  P.lag = nil
 end
end
-- The census rider index and the pad words for the driver's lines.
local function driveTail()
 local r, p = receiptRead(), P.pad
 return fmt("rider=%d;occ=%d;held=0x%x;bm=0x%x;mag=%d", n(r and r.rider), r and r.occupied == true and 1 or 0, p and math.max(n(p.held), 0) or 0,
  p and math.max(n(p.bm), 0) or 0, p and n(p.mag) or -1)
end
local function driverTick(now)
 if not S.localRides() then return stop("driver_off") end
 if now - P.rxAt > LOST_MS then return stop("stream_lost") end
 if now >= P.keepAt then P.keepAt = now + KEEP_MS; send(KEEP, 0) end
 padE()
 local path = S.path or 0
 -- The ridden path every DRIVE_MS (runs 306525db / c4428efa: the driver's horse stood still under the ride keys). M3c (run
 -- 7c0b9e71): that 'stall' was the scenario's first ride key 17 s after a stray claim, then a cliff in front of the horse.
 if now >= (P.driveAt or 0) then
  if P.drivePath then capped("drive", fmt("step=drive;moved_cm=%d;leases=%d;%s", round((path - P.drivePath) / 10), P.seatN or 0, driveTail()), 90) end
  P.drivePath, P.driveAt = path, now + DRIVE_MS
 end
 -- The stall watch: ride keys held, the horse under MOVE_MM in STALL_MS; first_move: the first ride key to MOVE_MM of path.
 local p = P.pad
 if p and num(p.mag) and p.mag >= MAG_ON then
  if not P.keyAt then P.keyAt, P.keyPath, P.keyFirst = now, path, P.keyFirst or now end
  if path - P.keyPath >= MOVE_MM then
   if not P.moved then P.moved = true; log(fmt("step=first_move;ms=%d;%s", now - P.keyFirst, driveTail())) end
   P.keyAt, P.keyPath = now, path
  elseif now - P.keyAt >= STALL_MS then
   capped("stall", fmt("step=stall;ms=%d;moved_cm=%d;%s", now - P.keyAt, round((path - P.keyPath) / 10), driveTail()), 20)
   P.keyAt, P.keyPath = now, path
  end
 else P.keyAt, P.keyPath = nil, nil end
 local pose = P.remoteSeat == false and 0 or 1
 local r = receipt(fmt("seat1 %d %d %d %d %d", W.mission, W.epoch, V.dback, V.dup, pose))
 if r and r.ready == true then
  P.seatN = (P.seatN or 0) + 1
  local ok = "ok" .. pose
  if P.leaseWhy ~= ok then P.leaseWhy = ok; log(fmt("step=seat1;ok=1;back_mm=%d;up_mm=%d;pose=%d", V.dback, V.dup, pose)) end
 else
  local why = r and tostring(r.reason) or "receipt"
  if P.leaseWhy ~= why then P.leaseWhy = why; capped("seat1_refused", "step=seat1;ok=0;reason=" .. why) end
  if why == "not_riding" then return stop("driver_off") end
 end
end
local function onClaim(now, token)
 if not W.host then return end
 P.token = token
 local why = P.state ~= "off" and "busy" or now < (P.lockUntil or 0) and "locked" or not S.localRides() and "not_riding" or
  (type(W.horseCargo) == "table" and type(W.horseCargo.busy) == "function" and W.horseCargo.busy()) and "busy" or nil
 if why then
  send(DENY, CODE[why])
  log(fmt("step=deny;seat=1;why=%s;token=%d", why, token))
  return
 end
 send(GRANT, 0)
 log(fmt("step=grant;seat=1;token=%d", token))
 driver(now, "grant")
end
W.handlers[OP] = (function(base)
 return function(v)
  if type(v) ~= "table" or v[2] ~= SEAT then if base then return base(v) end return end
  if #v < 10 or not P.on() then return end
  local role, action, cause, token = v[3], v[4], v[5], v[6]
  if role ~= 3 - W.role or not int(action, 1, 6) or not int(cause, 0, 255) or not int(token, 0, 65535) then return end
  local now = W.now and W.now() or 0
  if action == CLAIM then
   onClaim(now, token)
   if P.state == "driver" then P.remoteSeat = cause ~= 1 end
   return
  end
  if action == GRANT then
   if cause == CODE.passenger then
    -- The host sits behind this copy's player: drive with them, or say this copy's player is not riding.
    P.token = token
    if P.state ~= "off" then stop("busy") end
    if S.localRides() then return driver(now, "host_press") end
    send(UNSEATED, CODE.not_riding)
    log(fmt("step=deny;seat=1;why=not_riding;token=%d", token))
    return reset()
   end
   if P.state == "claim" and token == P.token then return seated(now, "grant") end
  elseif action == DENY then
   if P.state == "claim" and token == P.token then
    log(fmt("step=denied;seat=1;why=%s;token=%d", CAUSE[cause] or "deny", token))
    reset()
   end
  elseif action == KEEP then
   if (P.state == "seated" or P.state == "driver") and token == P.token then P.rxAt = now end
   if P.state == "driver" and token == P.token then P.remoteSeat = cause ~= 1 end
  elseif action == UNSEATED then
   if (P.state == "seated" or P.state == "driver") and token == P.token then stop(CAUSE[cause] or "remote_end", true) end
  end
 end
end)(W.handlers[OP])
-- M3c tie: both players pressed E at the unridden horse at once, each rides its own copy's horse and the partner's
-- RIDE_FAST arrives while this copy's player rides (world_horse_share opens no track). The host drives; the guest should sit
-- behind, but there is no local get-off request yet (no Lua command; the ride states' 0x141230B30 request is unnamed), so
-- the guest's copy logs it and M1's two-driver fallback stays.
local function tieTick(now)
 if not S.localRides() then P.tied = nil; return end
 local at = S.partnerRidesAt
 if P.tied or not (num(at) and now >= at and now - at <= TIE_MS) then return end
 P.tied = true
 capped("tie", fmt("step=tie;role=%s;verdict=%s;state=%s", W.host and "host" or "guest", W.host and "drive" or "yield;getoff=none", P.state), 10)
end
-- M3c: the shared horse hit or knocked down on this copy (the Unconscious of M3's passenger copies): the message, its
-- arguments, this copy's seat state, the horse's life and body 0's height over the horse.
function P.hit(kind, a1, a2, a3)
 local r, x, y, z = receiptRead(), mine()
 local h = P.h
 capped("horse_hit", fmt("step=horse_hit;m=%s;a1=%s;a2=%s;a3=%s;state=%s;life=%d;max=%d;body_dy_cm=%d", kind, tostring(a1), tostring(a2),
  tostring(a3), P.state, n(r and r.life), n(r and r.maxlife), (y and h) and round((y - h.y) / 10) or -1), 40)
end
local observe = W.observe
if type(observe) == "function" then
 function W.observe(class, msg, a0, a1, a2, a3, ...)
  local M, K = W.M, W.kind
  if a0 == HORSE and type(M) == "table" and class == M.GameObject and type(K) == "table" then
   local kind = K[msg]
   if (kind == "Unconscious" or kind == "Damage" or kind == "Dead" or kind == "Conscious") and P.on() then pcall(P.hit, kind, a1, a2, a3) end
  end
  return observe(class, msg, a0, a1, a2, a3, ...)
 end
end
function P.tick(now)
 if not P.on() then
  if P.state ~= "off" then stop("stream_lost", W.paired ~= true) end
  P.prevE = nil
  return
 end
 tieTick(now)
 if P.mission ~= nil and (P.mission ~= W.mission or P.epoch ~= W.epoch or P.role ~= W.role) then return stop("context") end
 local s = P.state
 if s == "off" then return offTick(now) end
 if s == "claim" then
  if now - P.claimAt > CLAIM_MS then stop("claim_timeout") end
  return
 end
 if s == "seated" then return seatedTick(now) end
 if s == "driver" then return driverTick(now) end
end
function P.status()
 return fmt("pillion;state=%s;key=%d;token=%d;role=%d;mode=%d;masked=%d;shot=%d", P.state, P.keyOn() and 1 or 0, P.token or 0, W.role or 0,
  V.mode, P.masked and 1 or 0, P.shot and 1 or 0)
end
local tick = W.tick
function W.tick(...)
 if tick then tick(...) end
 local now = W.now and W.now() or 0
 local ok, err = pcall(P.tick, now)
 if not ok then log("step=error;" .. tostring(err)); pcall(stop, "context") end
end
local function hook() pcall(stop, "context", true); P.mission, P.epoch, P.role = nil, nil, nil end
if type(W.startHooks) == "table" then W.startHooks[#W.startHooks + 1] = hook end
if type(W.unpairedHooks) == "table" then W.unpairedHooks[#W.unpairedHooks + 1] = hook end
end
end
